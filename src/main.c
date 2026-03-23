#include "gui.h"
#include "lua_engine.h"
#include "download_manager.h"
#include "thread_queue.h"

#include "ui.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* Global URL task queue — shared between main.c, gui.c, and workers  */
/* ------------------------------------------------------------------ */

TaskQueue g_url_queue;

/* ------------------------------------------------------------------ */
/* Entry point                                                          */
/* ------------------------------------------------------------------ */

#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>

static int wide_to_utf8(const wchar_t *src, char *dst, size_t dst_sz)
{
    int needed;

    if (!src || !dst || dst_sz == 0) return 0;
    needed = WideCharToMultiByte(CP_UTF8, 0, src, -1, NULL, 0, NULL, NULL);
    if (needed <= 0 || (size_t)needed > dst_sz) return 0;
    return WideCharToMultiByte(CP_UTF8, 0, src, -1, dst, (int)dst_sz, NULL, NULL) > 0;
}

static wchar_t *utf8_to_wide_dup(const char *src)
{
    int needed;
    wchar_t *dst;

    if (!src) return NULL;
    needed = MultiByteToWideChar(CP_UTF8, 0, src, -1, NULL, 0);
    if (needed <= 0) return NULL;
    dst = (wchar_t *)malloc((size_t)needed * sizeof(wchar_t));
    if (!dst) return NULL;
    if (MultiByteToWideChar(CP_UTF8, 0, src, -1, dst, needed) <= 0) {
        free(dst);
        return NULL;
    }
    return dst;
}

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE hPrev,
                   LPSTR lpCmdLine, int nCmdShow)
{
    (void)hInst; (void)hPrev; (void)lpCmdLine; (void)nCmdShow;
#else
int main(int argc, char *argv[])
{
    (void)argc; (void)argv;
#endif

    /* -------------------------------------------------------------- */
    /* 1. Initialise libui                                              */
    /* -------------------------------------------------------------- */
    uiInitOptions opts;
    memset(&opts, 0, sizeof(opts));
    opts.Size = sizeof(uiInitOptions);
    const char *err = uiInit(&opts);
    if (err) {
        fprintf(stderr, "uiInit error: %s\n", err);
#ifdef _WIN32
        wchar_t *werr = utf8_to_wide_dup(err);
        MessageBoxW(NULL, werr ? werr : L"uiInit error", L"LUDO startup error", MB_ICONERROR | MB_OK);
        free(werr);
#endif
        uiFreeInitError(err);
        return 1;
    }

    /* -------------------------------------------------------------- */
    /* 2. Initialise the URL task queue                                 */
    /* -------------------------------------------------------------- */
    if (task_queue_init(&g_url_queue, 256) != 0) {
        fprintf(stderr, "Failed to initialise task queue\n");
        uiUninit();
        return 1;
    }

    /* -------------------------------------------------------------- */
    /* 3. Initialise the download manager (2 concurrent downloads)     */
    /* -------------------------------------------------------------- */
    download_manager_init(2, "./downloads/");

    /* -------------------------------------------------------------- */
    /* 4. Initialise the Lua plugin engine and load plugins             */
    /* -------------------------------------------------------------- */
    lua_engine_init();
    lua_engine_load_plugins("plugins");

    /* --------------------------------------------------------------------- */
    /* 5. Build, display the GUI and sync it with the download manager state */
    /* --------------------------------------------------------------------- */
    gui_create();
    download_manager_sync_ui();

    /* --------------------------------------------------------------------- */
    /* 5b. Process command-line arguments (URLs)                             */
    /* --------------------------------------------------------------------- */
#ifdef _WIN32
    /* Convert wide Windows argv to UTF-8 so URLs survive non-ASCII input. */
    int argc_w = 0;
    LPWSTR *argv_w = CommandLineToArgvW(GetCommandLineW(), &argc_w);
    if (argv_w) {
        for (int i = 1; i < argc_w; i++) {
            char arg_utf8[4096];
            if (wide_to_utf8(argv_w[i], arg_utf8, sizeof(arg_utf8)) && arg_utf8[0] != '\0') {
                task_queue_push(&g_url_queue, arg_utf8);
            }
        }
        LocalFree(argv_w);
    }
#else
    /* On Linux/macOS, use standard argc/argv from main() */
    for (int i = 1; i < argc; i++) {
        if (argv[i] && strlen(argv[i]) > 0) {
            task_queue_push(&g_url_queue, argv[i]);
        }
    }
#endif

    /* -------------------------------------------------------------- */
    /* 6. Enter the libui event loop (blocks until window is closed)  */
    /* -------------------------------------------------------------- */
    uiMain();

    /* -------------------------------------------------------------- */
    /* 7. Tear down (in reverse order)                                  */
    /* -------------------------------------------------------------- */
    task_queue_shutdown(&g_url_queue);
    gui_shutdown();
    download_manager_shutdown();
    lua_engine_shutdown();
    task_queue_destroy(&g_url_queue);

    uiUninit();
    return 0;
}
