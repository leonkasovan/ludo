#include "gui.h"
#include "lua_engine.h"
#include "download_manager.h"
#include "config.h"
#include "ipc/ludo_native_messaging.h"
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

#include "platform_utils.h"

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE hPrev,
                   LPSTR lpCmdLine, int nCmdShow)
{
    (void)hInst; (void)hPrev; (void)lpCmdLine; (void)nCmdShow;
#else
int main(int argc, char *argv[])
{
    (void)argc; (void)argv;
#endif

    if (!ludo_config_init("config.ini")) {
        fprintf(stderr, "Failed to load config.ini\n");
    }
    const LudoConfig *cfg = ludo_config_get();
    // dm_log_init();

    /* -------------------------------------------------------------- */
    /* Early-Check: Headless script mode                              */
    /* -------------------------------------------------------------- */
    int run_script = 0;
    char *script_path = NULL;

#ifdef _WIN32
    int argc_w = 0;
    LPWSTR *argv_w_pre = CommandLineToArgvW(GetCommandLineW(), &argc_w);
    if (argv_w_pre) {
        for (int i = 1; i < argc_w; i++) {
            char arg_utf8[4096];
            if (wide_to_utf8(argv_w_pre[i], arg_utf8, sizeof(arg_utf8)) && arg_utf8[0] != '\0') {
                if (strcmp(arg_utf8, "--script") == 0 || strcmp(arg_utf8, "-s") == 0) {
                    if (i + 1 < argc_w) {
                        char next_utf8[4096];
                        if (wide_to_utf8(argv_w_pre[i+1], next_utf8, sizeof(next_utf8))) {
                            script_path = malloc(strlen(next_utf8) + 1);
                            strcpy(script_path, next_utf8);
                            run_script = 1;
                        }
                    }
                    break;
                }
            }
        }
        LocalFree(argv_w_pre);
    }
#else
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--script") == 0 || strcmp(argv[i], "-s") == 0) {
            if (i + 1 < argc) {
                script_path = malloc(strlen(argv[i+1]) + 1);
                strcpy(script_path, argv[i+1]);
                run_script = 1;
            }
            break;
        }
    }
#endif

    if (run_script && script_path) {
        /* Run headlessly and exit.
         * Initialise subsystems that scripts/plugins may use to avoid
         * crashes (task queue and download manager), and load plugins
         * so script code can call plugin APIs.
         */
        if (task_queue_init(&g_url_queue, cfg ? cfg->url_queue_capacity : 256) != 0) {
            fprintf(stderr, "Failed to initialise task queue\n");
            ludo_config_shutdown();
            return 1;
        }

        if (!download_manager_init(cfg ? cfg->max_thread : 2,
                                   cfg ? cfg->output_dir : "downloads/")) {
            fprintf(stderr, "Failed to initialise download manager\n");
            task_queue_shutdown(&g_url_queue);
            task_queue_destroy(&g_url_queue);
            free(script_path);
            ludo_config_shutdown();
            return 1;
        }

        lua_engine_init();
        lua_engine_load_plugins(cfg ? cfg->plugin_dir : "plugins");

        lua_engine_run_script(script_path);

        /* Shutdown in a consistent order with the GUI mode teardown. */
        task_queue_shutdown(&g_url_queue);
        download_manager_shutdown();
        lua_engine_shutdown();
        task_queue_destroy(&g_url_queue);

        free(script_path);
        ludo_config_shutdown();
        return 0;
    }

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
    if (task_queue_init(&g_url_queue, cfg ? cfg->url_queue_capacity : 256) != 0) {
        fprintf(stderr, "Failed to initialise task queue\n");
        uiUninit();
        ludo_config_shutdown();
        return 1;
    }

    /* -------------------------------------------------------------- */
    /* 3. Initialise the download manager (2 concurrent downloads)     */
    /* -------------------------------------------------------------- */
    if (!download_manager_init(cfg ? cfg->max_thread : 2,
                               cfg ? cfg->output_dir : "downloads/")) {
        fprintf(stderr, "Failed to initialise download manager\n");
        task_queue_shutdown(&g_url_queue);
        task_queue_destroy(&g_url_queue);
        uiUninit();
        ludo_config_shutdown();
        return 1;
    }

    /* -------------------------------------------------------------- */
    /* 4. Initialise the Lua plugin engine and load plugins             */
    /* -------------------------------------------------------------- */
    lua_engine_init();
    lua_engine_load_plugins(cfg ? cfg->plugin_dir : "plugins");
    ludo_native_messaging_start();

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
    ludo_native_messaging_stop();
    task_queue_shutdown(&g_url_queue);
    if (!gui_is_shutdown_requested()) gui_shutdown();
    download_manager_shutdown();
    lua_engine_shutdown();
    task_queue_destroy(&g_url_queue);
    gui_cleanup();
    ludo_config_shutdown();
    uiUninit();
    // dm_log_close();
    return 0;
}
