// WebView2 is Windows-only. These stubs keep the cross-platform Lua
// bindings (libuilua.c references uiNewWebView on every GUI platform)
// linking; the control degrades to an inert placeholder label.
#include "../ui.h"

uiWebView *uiNewWebView(void)
{
	return (uiWebView *) uiNewLabel("WebView is only available on Windows (WebView2).");
}

void uiWebViewSetHTML(uiWebView *v, const char *html)
{
	(void) v;
	(void) html;
}

void uiWebViewNavigate(uiWebView *v, const char *url)
{
	(void) v;
	(void) url;
}

void uiWebViewEval(uiWebView *v, const char *js)
{
	(void) v;
	(void) js;
}
