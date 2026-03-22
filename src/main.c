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
        MessageBoxA(NULL, err, "LUDO startup error", MB_ICONERROR | MB_OK);
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
    /* On Windows with WinMain, __argc and __argv are globally provided by MSVCRT */
    for (int i = 1; i < __argc; i++) {
        if (__argv[i] && strlen(__argv[i]) > 0) {
            task_queue_push(&g_url_queue, __argv[i]);
        }
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
    download_manager_shutdown();
    lua_engine_shutdown();
    task_queue_destroy(&g_url_queue);

    uiUninit();
    return 0;
}
