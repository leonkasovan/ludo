// WebView2 control for the libui-ng Windows backend (see PLAN.md).
// COM handler pattern proven under MinGW-w64 by C:\Projects\whatsapp-lite\main.c.
// Do not use WRL Callback<> lambdas here - hand-written handler classes only.
#include "uipriv_windows.hpp"
#include <WebView2.h>

struct uiWebView;

// ---- COM refcount base (no WRL) -------------------------------------------

struct WvUnknown
{
	ULONG ref;

	WvUnknown() : ref(1) {}
	virtual ~WvUnknown() {}

	ULONG WINAPI AddRefImpl() { return InterlockedIncrement(&ref); }
	ULONG WINAPI ReleaseImpl()
	{
		ULONG c = InterlockedDecrement(&ref);
		if (c == 0)
			delete this;
		return c;
	}
};

// One queued SetHTML/Navigate/Eval call made before async init finished.
struct PendingOp
{
	int kind;		// 0 = SetHTML, 1 = Navigate, 2 = Eval
	WCHAR *payload;		// uiprivAlloc'd UTF-16
};

// ---- environment completed handler ----------------------------------------

struct EnvHandler : WvUnknown, ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler
{
	uiWebView *owner;

	EnvHandler(uiWebView *v) : owner(v) {}

	HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **ppv) override;
	ULONG STDMETHODCALLTYPE AddRef() override { return AddRefImpl(); }
	ULONG STDMETHODCALLTYPE Release() override { return ReleaseImpl(); }
	HRESULT STDMETHODCALLTYPE Invoke(HRESULT errorCode, ICoreWebView2Environment *env) override;
};

// ---- controller completed handler -----------------------------------------

struct CtrlHandler : WvUnknown, ICoreWebView2CreateCoreWebView2ControllerCompletedHandler
{
	uiWebView *owner;

	CtrlHandler(uiWebView *v) : owner(v) {}

	HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **ppv) override;
	ULONG STDMETHODCALLTYPE AddRef() override { return AddRefImpl(); }
	ULONG STDMETHODCALLTYPE Release() override { return ReleaseImpl(); }
	HRESULT STDMETHODCALLTYPE Invoke(HRESULT errorCode, ICoreWebView2Controller *controller) override;
};

// ---- the control ------------------------------------------------------------

#define pendingHTML 0
#define pendingNavigate 1
#define pendingEval 2

struct uiWebView {
	uiWindowsControl c;
	HWND hwnd;			// libui container HWND
	ICoreWebView2Environment *env;
	ICoreWebView2Controller *controller;
	ICoreWebView2 *core;
	HWND fallback;			// STATIC shown when the runtime is missing
	std::vector<PendingOp> pending;
	BOOL ready;
	BOOL initFailed;
	EnvHandler *envHandler;
	CtrlHandler *ctrlHandler;
};

static void webviewSetBounds(uiWebView *v);
static void webviewFlush(uiWebView *v);
static void webviewQueue(uiWebView *v, int kind, const char *utf8);

// ---- handler implementations ------------------------------------------------

HRESULT STDMETHODCALLTYPE EnvHandler::QueryInterface(REFIID riid, void **ppv)
{
	if (!ppv)
		return E_POINTER;
	if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler)) {
		*ppv = static_cast<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler *>(this);
		AddRef();
		return S_OK;
	}
	*ppv = NULL;
	return E_NOINTERFACE;
}

HRESULT STDMETHODCALLTYPE EnvHandler::Invoke(HRESULT errorCode, ICoreWebView2Environment *env)
{
	if (owner == NULL)
		return errorCode;		// control destroyed while initializing
	uiWebView *v = owner;
	if (FAILED(errorCode) || env == NULL) {
		v->initFailed = TRUE;
		return errorCode;
	}
	v->env = env;
	env->AddRef();
	HRESULT hr = env->CreateCoreWebView2Controller(v->hwnd, v->ctrlHandler);
	if (FAILED(hr))
		v->initFailed = TRUE;
	return hr;
}

HRESULT STDMETHODCALLTYPE CtrlHandler::QueryInterface(REFIID riid, void **ppv)
{
	if (!ppv)
		return E_POINTER;
	if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_ICoreWebView2CreateCoreWebView2ControllerCompletedHandler)) {
		*ppv = static_cast<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler *>(this);
		AddRef();
		return S_OK;
	}
	*ppv = NULL;
	return E_NOINTERFACE;
}

HRESULT STDMETHODCALLTYPE CtrlHandler::Invoke(HRESULT errorCode, ICoreWebView2Controller *controller)
{
	if (owner == NULL) {
		if (controller != NULL)
			controller->Close();
		return errorCode;		// control destroyed while initializing
	}
	uiWebView *v = owner;
	if (FAILED(errorCode) || controller == NULL) {
		v->initFailed = TRUE;
		return errorCode;
	}
	v->controller = controller;
	controller->AddRef();
	HRESULT hr = controller->get_CoreWebView2(&v->core);
	if (FAILED(hr) || v->core == NULL) {
		v->initFailed = TRUE;
		return hr;
	}
	ICoreWebView2Settings *settings = NULL;
	if (SUCCEEDED(v->core->get_Settings(&settings)) && settings != NULL) {
		settings->put_IsScriptEnabled(TRUE);
		settings->put_AreDefaultScriptDialogsEnabled(TRUE);
		settings->put_IsStatusBarEnabled(FALSE);
		settings->Release();
	}
	if (v->fallback != NULL) {
		DestroyWindow(v->fallback);
		v->fallback = NULL;
	}
	v->ready = TRUE;
	webviewSetBounds(v);
	webviewFlush(v);
	return S_OK;
}

// ---- helpers ----------------------------------------------------------------

static void webviewSetBounds(uiWebView *v)
{
	RECT r;

	uiWindowsEnsureGetClientRect(v->hwnd, &r);
	if (v->controller != NULL) {
		v->controller->put_Bounds(r);
		// WebView2 creates its browser window hidden when the parent chain
		// is hidden at controller-creation time (e.g. the control lives in
		// an inactive uiTab page) and never re-syncs afterwards. Force it
		// to follow our own visibility on every layout pass.
		v->controller->put_IsVisible(IsWindowVisible(v->hwnd));
	}
	if (v->fallback != NULL)
		uiWindowsEnsureMoveWindowDuringResize(v->fallback, r.left, r.top, r.right - r.left, r.bottom - r.top);
}

static void webviewApply(uiWebView *v, const PendingOp *op)
{
	switch (op->kind) {
	case pendingHTML:
		v->core->NavigateToString(op->payload);
		break;
	case pendingNavigate:
		v->core->Navigate(op->payload);
		break;
	case pendingEval:
		v->core->ExecuteScript(op->payload, NULL);
		break;
	}
}

static void webviewFlush(uiWebView *v)
{
	for (size_t i = 0; i < v->pending.size(); i++) {
		webviewApply(v, &(v->pending[i]));
		uiprivFree(v->pending[i].payload);
	}
	v->pending.clear();
}

static void webviewQueue(uiWebView *v, int kind, const char *utf8)
{
	PendingOp op;

	op.kind = kind;
	op.payload = toUTF16(utf8);
	v->pending.push_back(op);
}

// Shows a plain STATIC message inside the container when WebView2 is not
// available (runtime or loader missing); the app keeps running.
static void webviewShowFallback(uiWebView *v)
{
	WCHAR *wtext;
	RECT r;

	wtext = toUTF16("WebView2 Runtime not available.");
	v->fallback = CreateWindowExW(0, L"STATIC", wtext,
		WS_CHILD | WS_VISIBLE | SS_CENTER | SS_CENTERIMAGE,
		0, 0, 0, 0, v->hwnd, NULL, hInstance, NULL);
	uiprivFree(wtext);
	uiWindowsEnsureGetClientRect(v->hwnd, &r);
	MoveWindow(v->fallback, r.left, r.top, r.right - r.left, r.bottom - r.top, TRUE);
}

// ---- uiControl plumbing (spinbox.cpp pattern) --------------------------------

static void uiWebViewDestroy(uiControl *c)
{
	uiWebView *v = uiWebView(c);

	// make in-flight async callbacks no-ops
	if (v->envHandler != NULL)
		v->envHandler->owner = NULL;
	if (v->ctrlHandler != NULL)
		v->ctrlHandler->owner = NULL;
	if (v->controller != NULL)
		v->controller->Close();
	if (v->core != NULL)
		v->core->Release();
	if (v->controller != NULL)
		v->controller->Release();
	if (v->env != NULL)
		v->env->Release();
	for (size_t i = 0; i < v->pending.size(); i++)
		uiprivFree(v->pending[i].payload);
	v->pending.clear();
	if (v->envHandler != NULL)
		v->envHandler->Release();
	if (v->ctrlHandler != NULL)
		v->ctrlHandler->Release();
	uiWindowsEnsureDestroyWindow(v->hwnd);
	uiFreeControl(uiControl(v));
}

uiWindowsControlAllDefaultsExceptDestroy(uiWebView)

static void uiWebViewMinimumSize(uiWindowsControl *c, int *width, int *height)
{
	// the WebView has no intrinsic size; stretch to whatever the layout gives
	*width = 100;
	*height = 100;
}

static void onResize(uiWindowsControl *c)
{
	webviewSetBounds(uiWebView(c));
}

// Builds %LOCALAPPDATA%\Ludo\WebView2 as the user data folder; falls back to
// ".\LudoWebView2" next to the exe when LOCALAPPDATA is somehow unset.
static WCHAR *webviewDataDir(void)
{
	WCHAR base[MAX_PATH];
	WCHAR *dir;
	DWORD n;

	n = GetEnvironmentVariableW(L"LOCALAPPDATA", base, MAX_PATH);
	if (n == 0 || n >= MAX_PATH) {
		dir = (WCHAR *) uiprivAlloc(15 * sizeof (WCHAR), "WCHAR[]");
		lstrcpynW(dir, L".\\LudoWebView2", 15);
		CreateDirectoryW(dir, NULL);
		return dir;
	}
	dir = (WCHAR *) uiprivAlloc((n + 15) * sizeof (WCHAR), "WCHAR[]");
	lstrcpynW(dir, base, (int) n + 1);
	lstrcatW(dir, L"\\Ludo\\WebView2");
	// create intermediate folders too (LOCALAPPDATA itself already exists)
	WCHAR *p = dir + 1;		// skip drive letter
	for (; *p != L'\0'; p++)
		if (*p == L'\\') {
			*p = L'\0';
			CreateDirectoryW(dir, NULL);
			*p = L'\\';
		}
	CreateDirectoryW(dir, NULL);
	return dir;
}

// ---- public API --------------------------------------------------------------

void uiWebViewSetHTML(uiWebView *v, const char *html)
{
	if (v->initFailed)
		return;
	if (!v->ready) {
		webviewQueue(v, pendingHTML, html);
		return;
	}
	WCHAR *w = toUTF16(html);
	v->core->NavigateToString(w);
	uiprivFree(w);
}

void uiWebViewNavigate(uiWebView *v, const char *url)
{
	if (v->initFailed)
		return;
	if (!v->ready) {
		webviewQueue(v, pendingNavigate, url);
		return;
	}
	WCHAR *w = toUTF16(url);
	v->core->Navigate(w);
	uiprivFree(w);
}

void uiWebViewEval(uiWebView *v, const char *js)
{
	if (v->initFailed)
		return;
	if (!v->ready) {
		webviewQueue(v, pendingEval, js);
		return;
	}
	WCHAR *w = toUTF16(js);
	v->core->ExecuteScript(w, NULL);
	uiprivFree(w);
}

uiWebView *uiNewWebView(void)
{
	uiWebView *v;

	uiWindowsNewControl(uiWebView, v);

	v->hwnd = uiWindowsMakeContainer(uiWindowsControl(v), onResize);
	v->env = NULL;
	v->controller = NULL;
	v->core = NULL;
	v->fallback = NULL;
	v->ready = FALSE;
	v->initFailed = FALSE;
	v->envHandler = NULL;
	v->ctrlHandler = NULL;

	WCHAR *dataDir = webviewDataDir();
	v->envHandler = new EnvHandler(v);
	v->ctrlHandler = new CtrlHandler(v);
	HRESULT hr = CreateCoreWebView2EnvironmentWithOptions(NULL, dataDir, NULL, v->envHandler);
	uiprivFree(dataDir);
	if (FAILED(hr)) {
		// loader present but runtime missing, or COM not initialized:
		// degrade to a static message instead of failing the app
		v->initFailed = TRUE;
		webviewShowFallback(v);
		v->envHandler->Release();
		v->ctrlHandler->Release();
		v->envHandler = NULL;
		v->ctrlHandler = NULL;
	}

	return v;
}
