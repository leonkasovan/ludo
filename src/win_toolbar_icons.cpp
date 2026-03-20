#ifdef _WIN32

#include "win_toolbar_icons.h"

#include <windows.h>
#include <gdiplus.h>
#include <vector>
#include <string>

#pragma comment(lib, "gdiplus.lib")

struct IconCtx {
    ULONG_PTR token;
    std::vector<HBITMAP> bitmaps;
};

static bool utf8_to_wide(const char *src, std::wstring &out) {
    if (!src) return false;
    int len = MultiByteToWideChar(CP_UTF8, 0, src, -1, nullptr, 0);
    if (len <= 0) return false;
    out.resize(static_cast<size_t>(len));
    if (MultiByteToWideChar(CP_UTF8, 0, src, -1, &out[0], len) <= 0) return false;
    return true;
}

extern "C" void *ludo_icons_init(void) {
    IconCtx *ctx = new IconCtx();
    ctx->token = 0;

    Gdiplus::GdiplusStartupInput input;
    if (Gdiplus::GdiplusStartup(&ctx->token, &input, nullptr) != Gdiplus::Ok) {
        delete ctx;
        return nullptr;
    }

    return ctx;
}

extern "C" void ludo_icons_set_button_png(void *opaque, uintptr_t hwnd_value, const char *png_path_utf8) {
    if (!opaque || !hwnd_value || !png_path_utf8) return;

    IconCtx *ctx = static_cast<IconCtx *>(opaque);
    HWND hwnd = reinterpret_cast<HWND>(hwnd_value);

    std::wstring wpath;
    if (!utf8_to_wide(png_path_utf8, wpath)) return;

    Gdiplus::Bitmap bmp(wpath.c_str());
    if (bmp.GetLastStatus() != Gdiplus::Ok) return;

    HBITMAP hbm = nullptr;
    if (bmp.GetHBITMAP(Gdiplus::Color(0, 0, 0, 0), &hbm) != Gdiplus::Ok || !hbm) {
        return;
    }

    LONG_PTR style = GetWindowLongPtr(hwnd, GWL_STYLE);
    style &= ~BS_TEXT;
    style |= BS_BITMAP;
    SetWindowLongPtr(hwnd, GWL_STYLE, style);
    SendMessage(hwnd, BM_SETIMAGE, IMAGE_BITMAP, reinterpret_cast<LPARAM>(hbm));
    InvalidateRect(hwnd, nullptr, TRUE);

    ctx->bitmaps.push_back(hbm);
}

extern "C" void ludo_icons_set_button_size(uintptr_t hwnd_value, int width, int height) {
    if (!hwnd_value || width <= 0 || height <= 0) return;
    HWND hwnd = reinterpret_cast<HWND>(hwnd_value);
    SetWindowPos(hwnd, nullptr, 0, 0, width, height,
                 SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    InvalidateRect(hwnd, nullptr, TRUE);
}

extern "C" void ludo_icons_shutdown(void *opaque) {
    if (!opaque) return;
    IconCtx *ctx = static_cast<IconCtx *>(opaque);

    for (HBITMAP hbm : ctx->bitmaps) {
        if (hbm) DeleteObject(hbm);
    }
    ctx->bitmaps.clear();

    if (ctx->token) {
        Gdiplus::GdiplusShutdown(ctx->token);
        ctx->token = 0;
    }

    delete ctx;
}

#endif
