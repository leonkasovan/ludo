#include "gui.h"
#include "lua_engine.h"
#include "http_module.h"
#include "ludo_module.h"
#include "../third_party/libuilua/libuilua.h"
#include "../third_party/lua-5.2.4/src/lua.h"
#include "../third_party/lua-5.2.4/src/lauxlib.h"
#include "../third_party/lua-5.2.4/src/lualib.h"
#include "download_manager.h"
#include "config.h"
#include "thread_queue.h"
#include "ui.h"
#include "version.h"
#include "dm_log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <ctype.h>

#if defined(__linux__) || defined(__unix__)
#include <gtk/gtk.h>
#include <dirent.h>
#include <spawn.h>
#include <unistd.h>
extern char **environ;
#endif

#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>
#include "win_toolbar_icons.h"

#define IDI_APP_ICON 101
#endif

/* ------------------------------------------------------------------ */
/* Constants                                                            */
/* ------------------------------------------------------------------ */

#define MAX_DOWNLOAD_ROWS 64

#define ICON_ADD      "add.png"
#define ICON_PAUSE    "pause.png"
#define ICON_RESUME   "resume.png"
#define ICON_REMOVE   "remove.png"
#define ICON_PLUGIN   "plugin.png"
#define ICON_SETTING  "setting.png"
#define ICON_HTTP     "http.png"
#define ICON_LUA      "lua.png"
#define ICON_ABOUT    "about.png"

#define TOOLBAR_BUTTON_SIZE 40

#include "platform_utils.h"

#ifdef _WIN32

static FILE *gui_fopen_utf8(const char *path, const char *mode) {
    FILE *fp = NULL;
    wchar_t *wpath = utf8_to_wide_dup(path);
    wchar_t wmode[8] = {0};
    size_t i;

    if (!wpath) return NULL;
    for (i = 0; mode[i] != '\0' && i + 1 < sizeof(wmode) / sizeof(wmode[0]); i++) {
        wmode[i] = (wchar_t)(unsigned char)mode[i];
    }
    fp = _wfopen(wpath, wmode);
    free(wpath);
    return fp;
}
#endif

#ifndef _WIN32
static FILE *gui_fopen_utf8(const char *path, const char *mode) {
    return fopen(path, mode);
}
#endif

/* ------------------------------------------------------------------ */
/* Sound Helper                                                         */
/* ------------------------------------------------------------------ */
#ifdef _WIN32
#include <mmsystem.h>
/* NOTE: You must link against winmm.lib (e.g., add -lwinmm to your Makefile) */
static void play_sound(const char *filepath) {
    if (!filepath || filepath[0] == '\0') {
        gui_log(LOG_WARNING, "play_sound: empty filepath");
        return;
    }
    // gui_log(LOG_INFO, "play_sound: attempting to play: %s", filepath);

    /* Check file existence using the UTF-8 aware fopen helper */
    FILE *f = gui_fopen_utf8(filepath, "rb");
    if (!f) {
        gui_log(LOG_WARNING, "play_sound: file not found: %s", filepath);
    } else {
        fclose(f);
    }

    wchar_t *wpath = utf8_to_wide_dup(filepath);
    BOOL ok = FALSE;
    if (wpath) {
        /* SND_ASYNC plays sound in background, SND_NODEFAULT prevents beep on missing file */
        ok = PlaySoundW(wpath, NULL, SND_FILENAME | SND_ASYNC | SND_NODEFAULT);
        free(wpath);
    } else {
        ok = PlaySoundA(filepath, NULL, SND_FILENAME | SND_ASYNC | SND_NODEFAULT);
    }

    if (!ok) {
        DWORD err = GetLastError();
        gui_log(LOG_ERROR, "play_sound: PlaySound failed for %s (res=%d, GetLastError=%lu)", filepath, (int)ok, (unsigned long)err);
    } else {
        // gui_log(LOG_INFO, "play_sound: PlaySound succeeded for %s", filepath);
    }
}
#elif defined(__APPLE__)
static void play_sound(const char *filepath) {
    if (!filepath || filepath[0] == '\0') {
        gui_log(LOG_WARNING, "play_sound: empty filepath");
        return;
    }
    // gui_log(LOG_INFO, "play_sound: attempting to play: %s", filepath);

    FILE *f = gui_fopen_utf8(filepath, "rb");
    if (!f) {
        gui_log(LOG_WARNING, "play_sound: file not found: %s", filepath);
    } else {
        fclose(f);
    }

    char cmd[2048];
    /* afplay is the native macOS command-line audio player */
    snprintf(cmd, sizeof(cmd), "afplay \"%s\" >/dev/null 2>&1 &", filepath);
    int rc = system(cmd);
    if (rc == -1) {
        gui_log(LOG_ERROR, "play_sound: failed to execute afplay for %s", filepath);
    } else {
        gui_log(LOG_INFO, "play_sound: executed: %s (rc=%d)", filepath, rc);
    }
}
#else
static void play_sound(const char *filepath) {
    if (!filepath || filepath[0] == '\0') {
        gui_log(LOG_WARNING, "play_sound: empty filepath");
        return;
    }
    // gui_log(LOG_INFO, "play_sound: attempting to play: %s", filepath);

    FILE *f = gui_fopen_utf8(filepath, "rb");
    if (!f) {
        gui_log(LOG_WARNING, "play_sound: file not found: %s", filepath);
    } else {
        fclose(f);
    }

    /* aplay is standard for ALSA. paplay can be used for PulseAudio.
       Running it with '&' ensures it doesn't block the GUI thread. */
    char *args[] = { "aplay", "-q", (char *)filepath, NULL };
    pid_t pid;
    if (posix_spawnp(&pid, "aplay", NULL, NULL, args, environ) != 0) {
        gui_log(LOG_ERROR, "play_sound: failed to execute aplay/paplay for %s", filepath);
    } else {
        // gui_log(LOG_INFO, "play_sound: executed aplay/paplay for %s (pid=%d)", filepath, (int)pid);
    }
}
#endif

/* ------------------------------------------------------------------ */
/* Download row UI element                                              */
/* ------------------------------------------------------------------ */

typedef struct {
    DownloadStatus  status;
    int             selected;      /* checkbox selection state */
} DownloadRow;

/* Table model for downloads */
static uiTableModel *g_downloads_model = NULL;
static uiTable *g_downloads_table = NULL;
static uiTableModelHandler g_downloads_mh;

/* Context structs for test windows (file scope so callbacks may be static) */
typedef struct HttpTestCtx {
    uiEntry *url_entry;
    uiMultilineEntry *header_entry;
    uiMultilineEntry *header_resp_entry;
    uiMultilineEntry *output_entry;
    uiWindow *win;
} HttpTestCtx;

typedef struct {
    char name[256];
    char path[1024];
} SnippetEntry;

typedef struct LuaTestCtx LuaTestCtx;

typedef struct {
    uiTableModelHandler handler;
    LuaTestCtx *ctx;
} SnippetTableModel;

typedef struct LuaTestCtx {
    uiMultilineEntry *script_entry;
    uiMultilineEntry *output_entry;
    uiWindow *win;
    uiTableModel *snippet_model;
    uiTable *snippet_table;
    SnippetTableModel snippet_model_handler;
    SnippetEntry *snippets;
    int snippet_count;
} LuaTestCtx;

static HttpTestCtx *g_active_http_test_ctx = NULL;

/* ------------------------------------------------------------------ */
/* GUI state                                                            */
/* ------------------------------------------------------------------ */

static struct {
    uiWindow        *window;
    uiEntry         *url_entry;
    uiButton        *tb_add;
    uiButton        *tb_pause;
    uiButton        *tb_resume;
    uiButton        *tb_remove;
    uiButton        *tb_plugin;
    uiButton        *tb_setting;
    uiButton        *tb_http;
    uiButton        *tb_lua;
    uiButton        *tb_about;
    uiBox           *downloads_box; /* vbox; one row per download       */
    uiMultilineEntry *log_view;

    DownloadRow      rows[MAX_DOWNLOAD_ROWS];
    int              row_count;
    int              active_download_id;
    int              shutdown_requested;
    ludo_thread_t    url_worker;
    int              url_worker_started;

#ifdef _WIN32
    void            *toolbar_icon_ctx;
    HICON            app_icon;
    int              app_icon_from_file;
#endif
} g_gui;

/* Task queue shared with worker threads (initialised in main.c) */
extern TaskQueue g_url_queue;

static const LudoGuiWindowId g_main_window_config_id = LUDO_GUI_WINDOW_MAIN;
static const LudoGuiWindowId g_add_urls_window_config_id = LUDO_GUI_WINDOW_ADD_URLS;
static const LudoGuiWindowId g_http_test_window_config_id = LUDO_GUI_WINDOW_HTTP_TEST;
static const LudoGuiWindowId g_lua_test_window_config_id = LUDO_GUI_WINDOW_LUA_TEST;

static const LudoGuiWindowConfig *gui_window_config(LudoGuiWindowId window_id) {
    const LudoConfig *cfg = ludo_config_get();

    if (!cfg) return NULL;

    switch (window_id) {
    case LUDO_GUI_WINDOW_MAIN:
        return &cfg->gui.main_window;
    case LUDO_GUI_WINDOW_ADD_URLS:
        return &cfg->gui.add_urls_window;
    case LUDO_GUI_WINDOW_HTTP_TEST:
        return &cfg->gui.http_test_window;
    case LUDO_GUI_WINDOW_LUA_TEST:
        return &cfg->gui.lua_test_window;
    default:
        return NULL;
    }
}

static const int *gui_table_widths(LudoGuiTableId table_id, size_t *count) {
    const LudoConfig *cfg = ludo_config_get();

    if (count) *count = 0;
    if (!cfg) return NULL;

    switch (table_id) {
    case LUDO_GUI_TABLE_DOWNLOADS:
        if (count) *count = LUDO_GUI_DOWNLOADS_TABLE_COLUMN_COUNT;
        return cfg->gui.downloads_table_widths;
    case LUDO_GUI_TABLE_SNIPPETS:
        if (count) *count = LUDO_GUI_SNIPPET_TABLE_COLUMN_COUNT;
        return cfg->gui.snippet_table_widths;
    default:
        return NULL;
    }
}

static void gui_get_window_size(LudoGuiWindowId window_id,
                                int default_width,
                                int default_height,
                                int *width,
                                int *height) {
    const LudoGuiWindowConfig *window_cfg = gui_window_config(window_id);

    if (width) *width = window_cfg ? window_cfg->width : default_width;
    if (height) *height = window_cfg ? window_cfg->height : default_height;
}

static void gui_apply_window_position(uiWindow *window, LudoGuiWindowId window_id) {
    const LudoGuiWindowConfig *window_cfg;

    if (!window) return;

    window_cfg = gui_window_config(window_id);
    if (!window_cfg) return;
    if (window_cfg->pos_x == -1 && window_cfg->pos_y == -1) return;

    uiWindowSetPosition(window, window_cfg->pos_x, window_cfg->pos_y);
}

static void gui_capture_window_geometry(uiWindow *window, LudoGuiWindowId window_id) {
    int width = 0;
    int height = 0;
    int pos_x = 0;
    int pos_y = 0;

    if (!window) return;

    uiWindowContentSize(window, &width, &height);
    if (width > 0 && height > 0) {
        ludo_config_set_window_size(window_id, width, height);
    }

    uiWindowPosition(window, &pos_x, &pos_y);
    ludo_config_set_window_position(window_id, pos_x, pos_y);
}

static void gui_apply_table_widths(uiTable *table, LudoGuiTableId table_id) {
    size_t column_count = 0;
    const int *widths = gui_table_widths(table_id, &column_count);

    if (!table || !widths) return;

    for (size_t column = 0; column < column_count; column++) {
        if (widths[column] > 0) {
            uiTableColumnSetWidth(table, (int)column, widths[column]);
        }
    }
}

static void gui_capture_table_widths(uiTable *table, LudoGuiTableId table_id) {
    size_t column_count = 0;
    const int *widths = gui_table_widths(table_id, &column_count);

    if (!table || !widths) return;

    for (size_t column = 0; column < column_count; column++) {
        int width = uiTableColumnWidth(table, (int)column);
        if (width > 0) {
            ludo_config_set_table_column_width(table_id, (int)column, width);
        }
    }
}

static void on_window_position_changed(uiWindow *window, void *data) {
    const LudoGuiWindowId *window_id = (const LudoGuiWindowId *)data;

    if (!window_id) return;
    gui_capture_window_geometry(window, *window_id);
}

static void on_window_content_size_changed(uiWindow *window, void *data) {
    const LudoGuiWindowId *window_id = (const LudoGuiWindowId *)data;

    if (!window_id) return;
    gui_capture_window_geometry(window, *window_id);
}

static void gui_enable_window_persistence(uiWindow *window, const LudoGuiWindowId *window_id) {
    if (!window || !window_id) return;

    gui_apply_window_position(window, *window_id);
    uiWindowOnPositionChanged(window, on_window_position_changed, (void *)window_id);
    uiWindowOnContentSizeChanged(window, on_window_content_size_changed, (void *)window_id);
}

static void persist_main_window_state(void) {
    gui_capture_window_geometry(g_gui.window, LUDO_GUI_WINDOW_MAIN);
    gui_capture_table_widths(g_downloads_table, LUDO_GUI_TABLE_DOWNLOADS);
    ludo_config_save();
}

static void persist_add_urls_window_state(uiWindow *window) {
    gui_capture_window_geometry(window, LUDO_GUI_WINDOW_ADD_URLS);
    ludo_config_save();
}

static void persist_http_test_window_state(uiWindow *window) {
    gui_capture_window_geometry(window, LUDO_GUI_WINDOW_HTTP_TEST);
    ludo_config_save();
}

static void persist_lua_test_window_state(LuaTestCtx *ctx) {
    if (!ctx) return;

    gui_capture_window_geometry(ctx->win, LUDO_GUI_WINDOW_LUA_TEST);
    gui_capture_table_widths(ctx->snippet_table, LUDO_GUI_TABLE_SNIPPETS);
    ludo_config_save();
}

/* ------------------------------------------------------------------ */
/* Worker thread: pulls URLs, drives Lua engine                         */
/* ------------------------------------------------------------------ */

static void *url_worker_thread(void *arg) {
    (void)arg;
    URLTask task;
    while (task_queue_pop(&g_url_queue, &task)) {
        int handled = lua_engine_process_url(task.url);
        if (!handled) {
            /* No plugin matched: attempt a direct download */
            char msg[512];
            snprintf(msg, sizeof(msg),
                     "[ludo] No plugin for URL — starting direct download: %s",
                     task.url);
            gui_log(LOG_INFO, "%s", msg);
            download_manager_add(task.url,
                                 download_manager_get_output_dir(),
                                 DOWNLOAD_NOW,
                                 NULL,
                                 NULL,
                                 NULL,  /* extra_headers */
                                 NULL);
        }
    }
    return NULL;
}

/* Auto-size table columns based on content (simple char-count heuristic) */
/* Window position/size changed handler — recompute column widths */
/* When the main window gains focus, check clipboard for a URL and paste it into the URL entry
   if the entry is currently empty. Only implemented on Windows for now. */
static void on_main_window_focus_changed(uiWindow *w, void *data) {
    (void)w; (void)data;
    /* Only act when window is focused */
    if (!uiWindowFocused(g_gui.window)) return;

    /* Paste from clipboard if it contains a valid URL */
    /* This uses the shared helper below. */
    /* Note: helper will re-check entry emptiness to avoid race conditions. */
    extern void paste_clipboard_url_if_valid(void);
    paste_clipboard_url_if_valid();
}

/* Simple URL validator used before pasting clipboard contents. */
static int is_valid_url(const char *s) {
    if (!s) return 0;
    while (*s && isspace((unsigned char)*s)) s++;
    if (strncmp(s, "http://", 7) == 0) s += 7;
    else if (strncmp(s, "https://", 8) == 0) s += 8;
    else return 0;
    /* Reject whitespace including CR which may be present on Windows clipboard */
    if (strchr(s, ' ') || strchr(s, '\t') || strchr(s, '\r') || strchr(s, '\n')) return 0;
    if (!strchr(s, '.')) return 0;
    if (strlen(s) < 3) return 0;
    return 1;
}

/* Helper function to get clipboard text */
char *get_clipboard_text(void) {
    char *clipboard_text = NULL;
#ifdef _WIN32
    if (!IsClipboardFormatAvailable(CF_UNICODETEXT) && !IsClipboardFormatAvailable(CF_TEXT)) return NULL;
    if (!OpenClipboard(NULL)) return NULL;
    HANDLE h = GetClipboardData(CF_UNICODETEXT);
    if (h) {
        wchar_t *wtext = (wchar_t *)GlobalLock(h);
        if (wtext) {
            int needed = WideCharToMultiByte(CP_UTF8, 0, wtext, -1, NULL, 0, NULL, NULL);
            if (needed > 0) {
                clipboard_text = (char *)malloc((size_t)needed);
                if (clipboard_text) WideCharToMultiByte(CP_UTF8, 0, wtext, -1, clipboard_text, needed, NULL, NULL);
            }
            GlobalUnlock(h);
        }
    } else {
        HANDLE h2 = GetClipboardData(CF_TEXT);
        if (h2) {
            char *ansi = (char *)GlobalLock(h2);
            if (ansi) {
                clipboard_text = _strdup(ansi);
                GlobalUnlock(h2);
            }
        }
    }
    CloseClipboard();
#elif defined(__linux__) || defined(__unix__)
    gchar *txt = gtk_clipboard_wait_for_text(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD));
    if (txt) clipboard_text = txt; /* gtk memory handled properly when freed below */
#endif    
    return clipboard_text;
}

void free_clipboard_text(char *text) {
#ifdef _WIN32
    free(text);
#elif defined(__linux__) || defined(__unix__)
    g_free(text);
#endif
}

void paste_clipboard_url_if_valid(void) {
    if (!g_gui.url_entry) return;

    char *clipboard_text = get_clipboard_text();
    if (!clipboard_text) return;

    /* Use only up to the first newline and trim whitespace. */
    char *line_end = strchr(clipboard_text, '\n');
    if (line_end) *line_end = '\0';

    /* Trim leading whitespace */
    char *s = clipboard_text;
    while (*s && isspace((unsigned char)*s)) s++;

    /* Trim trailing whitespace (in-place) */
    size_t slen = strlen(s);
    while (slen > 0 && isspace((unsigned char)s[slen - 1])) { s[slen - 1] = '\0'; slen--; }

    if (s[0] != '\0' && is_valid_url(s)) {
        uiEntrySetText(g_gui.url_entry, s);
    }

    free_clipboard_text(clipboard_text);
}

/* ------------------------------------------------------------------ */
/* uiQueueMain helpers                                                  */
/* ------------------------------------------------------------------ */

typedef struct { LogLevel level; char msg[1024]; } LogPkt;

static void log_on_main(void *data) {
    LogPkt *pkt = (LogPkt *)data;

    const char *prefix;
    switch (pkt->level) {
        case LOG_SUCCESS: prefix = "[SUCCESS] "; break;
        case LOG_WARNING: prefix = "[WARNING] "; break;
        case LOG_ERROR:   prefix = "[ERROR]  "; break;
        default:          prefix = "[INFO] "; break;
    }

    /* Build timestamped line */
    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    char line[1200];
    snprintf(line, sizeof(line), "%02d:%02d:%02d %s%s\n",
             t->tm_hour, t->tm_min, t->tm_sec,
             prefix, pkt->msg);

    uiMultilineEntryAppend(g_gui.log_view, line);
    free(pkt);
}

void gui_log(LogLevel level, const char *fmt, ...) {
    LogPkt *pkt = (LogPkt *)malloc(sizeof(LogPkt));
    if (!pkt) return;
    pkt->level = level;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(pkt->msg, sizeof(pkt->msg), fmt, ap);
    va_end(ap);
    pkt->msg[sizeof(pkt->msg) - 1] = '\0';
#ifdef DEBUG
    // Also log to stderr for crash diagnosis
    fprintf(stderr, "[gui_log][%d] %s\n", (int)level, pkt->msg);
    fflush(stderr);
#endif

    /* Build the prefix for the log file */
    const char *prefix;
    switch (pkt->level) {
        case LOG_SUCCESS: prefix = "[SUCCESS] "; break;
        case LOG_WARNING: prefix = "[WARNING] "; break;
        case LOG_ERROR:   prefix = "[ERROR]  "; break;
        default:          prefix = "[INFO] "; break;
    }

    /* Queue to GUI if it was initialized, otherwise free the packet */
    if (g_gui.log_view) {
        uiQueueMain(log_on_main, pkt);
    } else {
        dm_log("%s%s", prefix, pkt->msg);
        free(pkt);
    }
}

static void append_multiline_text(uiMultilineEntry *entry, const char *text, int newline) {
    if (!entry || !text) return;
    uiMultilineEntryAppend(entry, text);
    if (newline) uiMultilineEntryAppend(entry, "\n");
}

static int read_text_file_utf8(const char *path, char **out_text) {
    FILE *f;
    char *buf = NULL;
    long size;
    size_t read_bytes;

    if (!out_text) return 0;
    *out_text = NULL;

    f = gui_fopen_utf8(path, "rb");
    if (!f) return 0;
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return 0;
    }
    size = ftell(f);
    if (size < 0) {
        fclose(f);
        return 0;
    }
    if (fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        return 0;
    }

    buf = (char *)malloc((size_t)size + 1);
    if (!buf) {
        fclose(f);
        return 0;
    }
    read_bytes = (size > 0) ? fread(buf, 1, (size_t)size, f) : 0;
    fclose(f);
    if (size > 0 && read_bytes != (size_t)size) {
        free(buf);
        return 0;
    }
    buf[size] = '\0';

    if ((size_t)size >= 3 &&
        (unsigned char)buf[0] == 0xEF &&
        (unsigned char)buf[1] == 0xBB &&
        (unsigned char)buf[2] == 0xBF) {
        memmove(buf, buf + 3, (size_t)size - 2);
    }

    *out_text = buf;
    return 1;
}

static void trim_snippet_name(const char *filename, char *out, size_t out_sz) {
    size_t len = strlen(filename);

    if (len > 4 && strcmp(filename + len - 4, ".lua") == 0) {
        len -= 4;
    }
    if (len >= out_sz) len = out_sz - 1;
    memcpy(out, filename, len);
    out[len] = '\0';
}

static int append_snippet_entry(LuaTestCtx *ctx, const char *filename, const char *path) {
    SnippetEntry *tmp;

    tmp = (SnippetEntry *)realloc(ctx->snippets, (size_t)(ctx->snippet_count + 1) * sizeof(*tmp));
    if (!tmp) return 0;

    ctx->snippets = tmp;
    trim_snippet_name(filename, ctx->snippets[ctx->snippet_count].name,
                      sizeof(ctx->snippets[ctx->snippet_count].name));
    strncpy(ctx->snippets[ctx->snippet_count].path, path,
            sizeof(ctx->snippets[ctx->snippet_count].path) - 1);
    ctx->snippets[ctx->snippet_count].path[sizeof(ctx->snippets[ctx->snippet_count].path) - 1] = '\0';
    ctx->snippet_count++;
    return 1;
}

static int compare_snippets(const void *a, const void *b) {
    const SnippetEntry *sa = (const SnippetEntry *)a;
    const SnippetEntry *sb = (const SnippetEntry *)b;
    return strcmp(sa->name, sb->name);
}

static void load_snippets_from_dir(LuaTestCtx *ctx, const char *dir_path) {
#ifdef _WIN32
    wchar_t pattern[1024];
    wchar_t *dir_w = utf8_to_wide_dup(dir_path);
    WIN32_FIND_DATAW fd;
    HANDLE hfind;

    if (!dir_w) return;
    if (_snwprintf(pattern, sizeof(pattern) / sizeof(pattern[0]), L"%ls\\*.lua", dir_w) < 0) {
        free(dir_w);
        return;
    }

    hfind = FindFirstFileW(pattern, &fd);
    if (hfind == INVALID_HANDLE_VALUE) {
        free(dir_w);
        return;
    }
    do {
        char filename_utf8[256];
        char full_path_utf8[1024];
        wchar_t full_path[1024];

        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        if (_snwprintf(full_path, sizeof(full_path) / sizeof(full_path[0]), L"%ls\\%ls", dir_w, fd.cFileName) < 0) {
            continue;
        }
        if (!wide_to_utf8(fd.cFileName, filename_utf8, sizeof(filename_utf8))) continue;
        if (!wide_to_utf8(full_path, full_path_utf8, sizeof(full_path_utf8))) continue;
        if (!append_snippet_entry(ctx, filename_utf8, full_path_utf8)) break;
    } while (FindNextFileW(hfind, &fd));
    FindClose(hfind);
    free(dir_w);
#else
    DIR *dir = opendir(dir_path);
    struct dirent *ent;

    if (!dir) return;
    while ((ent = readdir(dir)) != NULL) {
        size_t name_len = strlen(ent->d_name);
        char full_path[1024];

        if (name_len < 5) continue;
        if (strcmp(ent->d_name + name_len - 4, ".lua") != 0) continue;
        snprintf(full_path, sizeof(full_path), "%s/%s", dir_path, ent->d_name);
        full_path[sizeof(full_path) - 1] = '\0';
        if (!append_snippet_entry(ctx, ent->d_name, full_path)) break;
    }
    closedir(dir);
#endif

    if (ctx->snippet_count > 1) {
        qsort(ctx->snippets, (size_t)ctx->snippet_count, sizeof(ctx->snippets[0]), compare_snippets);
    }
}

static int snippet_model_num_columns(uiTableModelHandler *mh, uiTableModel *m) {
    (void)mh;
    (void)m;
    return 1;
}

static uiTableValueType snippet_model_column_type(uiTableModelHandler *mh, uiTableModel *m, int column) {
    (void)mh;
    (void)m;
    (void)column;
    return uiTableValueTypeString;
}

static int snippet_model_num_rows(uiTableModelHandler *mh, uiTableModel *m) {
    SnippetTableModel *model = (SnippetTableModel *)mh;
    (void)m;
    return (model && model->ctx) ? model->ctx->snippet_count : 0;
}

static uiTableValue *snippet_model_cell_value(uiTableModelHandler *mh, uiTableModel *m, int row, int column) {
    SnippetTableModel *model = (SnippetTableModel *)mh;
    (void)m;
    (void)column;
    if (!model || !model->ctx || row < 0 || row >= model->ctx->snippet_count) {
        return uiNewTableValueString("");
    }
    return uiNewTableValueString(model->ctx->snippets[row].name);
}

static void snippet_model_set_cell_value(uiTableModelHandler *mh, uiTableModel *m,
                                         int row, int column, const uiTableValue *val) {
    (void)mh;
    (void)m;
    (void)row;
    (void)column;
    (void)val;
}

static void load_snippet_into_editor(LuaTestCtx *ctx, int row) {
    char *snippet_text = NULL;

    if (!ctx || row < 0 || row >= ctx->snippet_count) return;
    if (!read_text_file_utf8(ctx->snippets[row].path, &snippet_text)) {
        gui_log(LOG_ERROR, "Failed to load snippet: %s", ctx->snippets[row].path);
        return;
    }

    uiMultilineEntrySetText(ctx->script_entry, snippet_text);
    free(snippet_text);
}

static void on_snippet_row_double_clicked(uiTable *t, int row, void *data) {
    LuaTestCtx *ctx = (LuaTestCtx *)data;
    (void)t;
    load_snippet_into_editor(ctx, row);
}

static int destroy_http_test_window(uiWindow *w, void *data) {
    HttpTestCtx *ctx = (HttpTestCtx *)data;

    persist_http_test_window_state(w);
    if (g_active_http_test_ctx == ctx) {
        g_active_http_test_ctx = NULL;
    }
    if (ctx) free(ctx);
    uiControlDestroy(uiControl(w));
    return 0;
}

static int destroy_lua_test_window(uiWindow *w, void *data) {
    LuaTestCtx *ctx = (LuaTestCtx *)data;

    if (ctx) {
        persist_lua_test_window_state(ctx);
        free(ctx->snippets);
        if (ctx->snippet_model) {
            uiFreeTableModel(ctx->snippet_model);
        }
        free(ctx);
    }
    uiControlDestroy(uiControl(w));
    return 0;
}

/* Static callbacks for test windows (must be file-level, not nested) */
static void http_test_on_send(uiButton *b, void *ud) {
    (void)b;
    HttpTestCtx *ctx = (HttpTestCtx *)ud;
    if (!ctx) return;
    const char *url = uiEntryText(ctx->url_entry);
    const char *headers = uiMultilineEntryText(ctx->header_entry);

    lua_State *L = luaL_newstate();
    if (!L) {
        gui_log(LOG_ERROR, "[HTTP DEBUG] luaL_newstate failed");
        if (url) uiFreeText((char*)url);
        if (headers) uiFreeText((char*)headers);
        return;
    }
    luaL_openlibs(L);
    http_module_register(L);

    lua_getglobal(L, "http");
    lua_getfield(L, -1, "get");
    lua_pushstring(L, url ? url : "");
    lua_newtable(L); /* options */
    if (headers && headers[0]) {
        lua_newtable(L);
        char *lines = strdup(headers);
        char *p = lines;
        while (p && *p) {
            char *end = strchr(p, '\n');
            if (end) *end = '\0'; /* Terminate the current line */
            
            char *colon = strchr(p, ':');
            if (colon) {
                *colon = '\0';
                const char *k = p;
                const char *v = colon + 1;
                while (*v == ' ' || *v == '\t') v++; /* Trim leading space */
                lua_pushstring(L, k);
                lua_pushstring(L, v);
                lua_settable(L, -3);
            }
            p = end ? end + 1 : NULL; /* Move to next line */
        }
        free(lines);
        lua_setfield(L, -2, "headers");
    }

    int res = lua_pcall(L, 2, 3, 0);
    if (res != LUA_OK) {
        const char *err = lua_tostring(L, -1);
        gui_log(LOG_ERROR, err ? err : "(nil)");
        uiMultilineEntrySetText(ctx->output_entry, err ? err : "Lua error");
        lua_close(L);
        if (url) uiFreeText((char*)url);
        if (headers) uiFreeText((char*)headers);
        return;
    }

    /* Build header response string from headers table (stack -1) */
    if (lua_istable(L, -1)) {
        int hdr_idx = lua_gettop(L);
        char *hdr_out = NULL;
        size_t hdr_len = 0;
        lua_pushnil(L);
        while (lua_next(L, hdr_idx) != 0) {
            const char *k = lua_tostring(L, -2);
            const char *v = lua_tostring(L, -1);
            size_t klen = k ? strlen(k) : 0;
            size_t vlen = v ? strlen(v) : 0;
            size_t pair_len = klen + 2 + vlen + 1; /* "Key: Value\n" */
            char *tmp = (char *)realloc(hdr_out, hdr_len + pair_len + 1);
            if (!tmp) { free(hdr_out); hdr_out = NULL; hdr_len = 0; break; }
            hdr_out = tmp;
            if (klen) memcpy(hdr_out + hdr_len, k, klen);
            hdr_len += klen;
            hdr_out[hdr_len++] = ':';
            hdr_out[hdr_len++] = ' ';
            if (vlen) memcpy(hdr_out + hdr_len, v, vlen);
            hdr_len += vlen;
            hdr_out[hdr_len++] = '\n';
            hdr_out[hdr_len] = '\0';
            lua_pop(L, 1); /* pop value, keep key for next */
        }
        uiMultilineEntrySetText(ctx->header_resp_entry, hdr_out ? hdr_out : "");
        free(hdr_out);
    } else {
        uiMultilineEntrySetText(ctx->header_resp_entry, "");
    }

    size_t body_len = 0;
    const char *body = lua_tolstring(L, -3, &body_len);
    int status = (int)lua_tointeger(L, -2);

    /* Build dynamic output: [HTTP <code>]\n<body>. Avoid fixed-size truncation. */
    const char *status_fmt = "[HTTP %d]\n";
    char status_hdr[64];
    int hdr_len = snprintf(status_hdr, sizeof(status_hdr), status_fmt, status);
    if (hdr_len < 0) hdr_len = 0;

    size_t out_len = (size_t)hdr_len + (body_len ? body_len : 0);
    char *out = (char *)malloc(out_len + 1);
    if (out) {
        if (hdr_len > 0) memcpy(out, status_hdr, (size_t)hdr_len);
        if (body && body_len > 0) memcpy(out + hdr_len, body, body_len);
        out[out_len] = '\0';
        uiMultilineEntrySetText(ctx->output_entry, out);
        free(out);
    } else {
        /* Fallback: show status only */
        uiMultilineEntrySetText(ctx->output_entry, status_hdr);
    }

    lua_close(L);
    if (url) uiFreeText((char*)url);
    if (headers) uiFreeText((char*)headers);
}

static void lua_test_on_exec(uiButton *b, void *ud) {
    (void)b;
    LuaTestCtx *ctx = (LuaTestCtx *)ud;
    if (!ctx) return;
    const char *script = uiMultilineEntryText(ctx->script_entry);
    if (!script) return;

    lua_State *L = luaL_newstate();
    if (!L) {
        gui_log(LOG_ERROR, "[LUA DEBUG] luaL_newstate failed");
        uiFreeText((char*)script);
        return;
    }
    luaL_openlibs(L);
    http_module_register(L);
    ludo_module_register(L);
    {
        LudoTesterBindings bindings;
        memset(&bindings, 0, sizeof(bindings));
        bindings.lua_output = ctx->output_entry;
        if (g_active_http_test_ctx) {
            bindings.http_response_content = g_active_http_test_ctx->output_entry;
            bindings.http_response_header = g_active_http_test_ctx->header_resp_entry;
        }
        ludo_module_set_tester_bindings(L, &bindings);
    }
    luaL_requiref(L, "ui", luaopen_libuilua, 1);
    lua_pop(L, 1);

    uiMultilineEntrySetText(ctx->output_entry, "");

    int res = luaL_loadstring(L, script);
    if (res == LUA_OK) res = lua_pcall(L, 0, LUA_MULTRET, 0);
    if (res != LUA_OK) {
        const char *err = lua_tostring(L, -1);
        gui_log(LOG_ERROR, err ? err : "(nil)");
        append_multiline_text(ctx->output_entry, err ? err : "Lua error", 1);
        lua_close(L);
        uiFreeText((char*)script);
        return;
    }

    int nret = lua_gettop(L);
    char buf[8192] = {0};
    size_t offset = 0;
    
    for (int i = 1; i <= nret; i++) {
        size_t len;
        const char *s = lua_tolstring(L, i, &len);
        if (!s) {
            luaL_tolstring(L, i, &len);
            s = lua_tostring(L, -1);
            lua_pop(L, 1);
        }
        if (s && offset < sizeof(buf) - 1) {
            int written = snprintf(buf + offset, sizeof(buf) - offset, "%s%s", 
                                   s, (i < nret) ? "\t" : "");
            if (written > 0) offset += written;
        }
    }
    if (buf[0]) {
        char *existing = uiMultilineEntryText(ctx->output_entry);
        int needs_newline = existing && existing[0] != '\0';
        if (existing) uiFreeText(existing);
        if (needs_newline) append_multiline_text(ctx->output_entry, "", 1);
        append_multiline_text(ctx->output_entry, buf, 1);
    } else {
        char *existing = uiMultilineEntryText(ctx->output_entry);
        int has_output = existing && existing[0] != '\0';
        if (existing) uiFreeText(existing);
        if (!has_output) {
            uiMultilineEntrySetText(ctx->output_entry, "Execution finished (no output).");
        }
    }
    lua_close(L);
    uiFreeText((char*)script);
}

/* ------------------------------------------------------------------ */
/* Download row management (main thread only)                          */
/* ------------------------------------------------------------------ */

static DownloadRow *find_row(int id) {
    for (int i = 0; i < g_gui.row_count; i++) {
        if (g_gui.rows[i].status.id == id) return &g_gui.rows[i];
    }
    return NULL;
}

static int find_row_index(int id) {
    for (int i = 0; i < g_gui.row_count; i++) {
        if (g_gui.rows[i].status.id == id) return i;
    }
    return -1;
}

/* ------------------------------------------------------------------ */
/* Downloads table model handlers                                      */
/* ------------------------------------------------------------------ */
static int downloads_modelNumColumns(uiTableModelHandler *mh, uiTableModel *m) {
    (void)mh; (void)m; return 9;
}

static uiTableValueType downloads_modelColumnType(uiTableModelHandler *mh, uiTableModel *m, int column) {
    (void)mh; (void)m;
    switch (column) {
        case 0: return uiTableValueTypeInt;    /* checkbox */
        case 1: return uiTableValueTypeString; /* Name */
        case 2: return uiTableValueTypeString; /* Size */
        case 3: return uiTableValueTypeString; /* Status */
        case 4: return uiTableValueTypeString; /* Started */
        case 5: return uiTableValueTypeString; /* Speed */
        case 6: return uiTableValueTypeInt;    /* Progress */
        case 7: return uiTableValueTypeString; /* Button Text */
        case 8: return uiTableValueTypeInt;    /* Button Clickable Boolean */
    }
    return uiTableValueTypeString;
}

static int downloads_modelNumRows(uiTableModelHandler *mh, uiTableModel *m) {
    (void)mh; (void)m;
    return g_gui.row_count;
}

static uiTableValue *downloads_modelCellValue(uiTableModelHandler *mh, uiTableModel *m, int row, int column) {
    (void)mh; (void)m;
    if (row < 0 || row >= g_gui.row_count) return NULL;
    DownloadRow *r = &g_gui.rows[row];
    char buf[128];
    time_t now = time(NULL);
    switch (column) {
        case 0: return uiNewTableValueInt(r->selected);
        case 1: return uiNewTableValueString(r->status.filename[0] ? r->status.filename : "");
        case 2: {
            if (r->status.state == DOWNLOAD_STATE_COMPLETED && r->status.total_bytes > 0) {
                /* Format strictly as a single size */
                if (r->status.total_bytes >= (1LL<<30))
                    snprintf(buf, sizeof(buf), "%.2f GB", r->status.total_bytes/1024.0/1024.0/1024.0);
                else if (r->status.total_bytes >= (1LL<<20))
                    snprintf(buf, sizeof(buf), "%.2f MB", r->status.total_bytes/1024.0/1024.0);
                else
                    snprintf(buf, sizeof(buf), "%lld B", (long long)r->status.total_bytes);
            } else if (r->status.total_bytes > 0) {
                /* Active: Show Downloaded / Total */
                double dl = (double)r->status.downloaded_bytes, tot = (double)r->status.total_bytes;
                if (tot >= (1LL<<30))
                    snprintf(buf, sizeof(buf), "%.2f / %.2f GB", dl/1073741824.0, tot/1073741824.0);
                else if (tot >= (1LL<<20))
                    snprintf(buf, sizeof(buf), "%.2f / %.2f MB", dl/1048576.0, tot/1048576.0);
                else
                    snprintf(buf, sizeof(buf), "%lld / %lld B",
                             (long long)r->status.downloaded_bytes,
                             (long long)r->status.total_bytes);
            } else if (r->status.downloaded_bytes > 0) {
                if (r->status.downloaded_bytes >= (1LL<<20))
                    snprintf(buf, sizeof(buf), "%.2f MB", r->status.downloaded_bytes/1048576.0);
                else
                    snprintf(buf, sizeof(buf), "%lld B", (long long)r->status.downloaded_bytes);
            } else {
                snprintf(buf, sizeof(buf), "-");
            }
            return uiNewTableValueString(buf);
        }
        case 3: {
            /* Status string */
            const char *st = "?";
            switch (r->status.state) {
                case DOWNLOAD_STATE_QUEUED: st = "Queued"; break;
                case DOWNLOAD_STATE_RUNNING: st = "Running"; break;
                case DOWNLOAD_STATE_PAUSED: st = "Paused"; break;
                case DOWNLOAD_STATE_COMPLETED: st = "Completed"; break;
                case DOWNLOAD_STATE_FAILED: st = "Failed"; break;
                default: st = "Unknown"; break;
            }
            return uiNewTableValueString(st);
        }
        case 4: {
            if (r->status.start_time) {
                struct tm *t = localtime(&r->status.start_time);
                // if less than 24 hours, show HH:MM:SS, else show date
                if (now - r->status.start_time >= 24*3600) {
                    snprintf(buf, sizeof(buf), "%04d-%02d-%02d", t->tm_year + 1900, t->tm_mon + 1, t->tm_mday);
                } else {
                    snprintf(buf, sizeof(buf), "%02d:%02d:%02d", t->tm_hour, t->tm_min, t->tm_sec);
                }
            } else snprintf(buf, sizeof(buf), "-");
            return uiNewTableValueString(buf);
        }
        case 5: {
            if (r->status.speed_bps > 0.0) {
                if (r->status.speed_bps >= 1024.0*1024.0) snprintf(buf, sizeof(buf), "%.2f MB/s", r->status.speed_bps/1024.0/1024.0);
                else if (r->status.speed_bps >= 1024.0) snprintf(buf, sizeof(buf), "%.2f KB/s", r->status.speed_bps/1024.0);
                else snprintf(buf, sizeof(buf), "%.0f B/s", r->status.speed_bps);
            } else snprintf(buf, sizeof(buf), "-");
            return uiNewTableValueString(buf);
        }
        case 6: {
            int pct = (int)r->status.progress;
            if (pct < 0) pct = 0;
            if (pct > 100) pct = 100;
            return uiNewTableValueInt(pct);
        }
        case 7: {
            /* Button text based on state */
            const char *btn = "⚙";
            switch (r->status.state) {
                case DOWNLOAD_STATE_COMPLETED: 
                case DOWNLOAD_STATE_FAILED:    btn = "❌"; break; /* Remove */
                case DOWNLOAD_STATE_QUEUED: 
                case DOWNLOAD_STATE_RUNNING:   btn = "⏸"; break; /* Pause */
                case DOWNLOAD_STATE_PAUSED:    btn = "▶"; break;  /* Resume */
            }
            return uiNewTableValueString(btn);
        }
        case 8: {
            /* Button clickability. 1 = enabled, 0 = disabled */
            return uiNewTableValueInt(1);
        }
    }
    return NULL;
}

static void downloads_modelSetCellValue(uiTableModelHandler *mh, uiTableModel *m, int row, int column, const uiTableValue *val) {
    (void)mh; (void)m;
    if (row < 0 || row >= g_gui.row_count) return;
    DownloadRow *r = &g_gui.rows[row];
    if (column == 0) {
        r->selected = uiTableValueInt(val);
        if (g_downloads_model) uiTableModelRowChanged(g_downloads_model, row);
    }
    /* NEW: Handle the inline button click */
    else if (column == 7) {
        int target_id = r->status.id; /* BEST PRACTICE: Store before shifting the array! */
        
        /* Determine action based on state */
        if (r->status.state == DOWNLOAD_STATE_COMPLETED || r->status.state == DOWNLOAD_STATE_FAILED) {
            /* Remove the download.  download_manager_remove dispatches a final
             * marked_for_removal progress update; gui_on_progress deletes the row. */
            download_manager_remove(target_id);
            gui_log(LOG_INFO, "Scheduled removal of download id=%d via inline button", target_id);
        } else if (r->status.state == DOWNLOAD_STATE_QUEUED || r->status.state == DOWNLOAD_STATE_RUNNING) {
            download_manager_pause(target_id);
        } else if (r->status.state == DOWNLOAD_STATE_PAUSED || r->status.state == DOWNLOAD_STATE_FAILED) {
            download_manager_resume(target_id);
        }
    }
}

/* Select latest active download for pause/resume/remove actions. */
/*static int pick_target_download_id(void) {
    if (g_gui.active_download_id > 0) {
        return g_gui.active_download_id;
    }

    // BEST PRACTICE: Iterate the local GUI state, not the shared backend list
    for (int i = 0; i < g_gui.row_count; i++) {
        DownloadState state = g_gui.rows[i].state;
        if (state == DOWNLOAD_STATE_RUNNING || state == DOWNLOAD_STATE_QUEUED || state == DOWNLOAD_STATE_PAUSED) {
            return g_gui.rows[i].download_id;
        }
    }
    
    // Fallback to the first row if nothing active
    if (g_gui.row_count > 0) return g_gui.rows[0].download_id;
    return -1;
}*/

#ifdef _WIN32
static void set_main_window_icon(void) {
    HWND hwnd = (HWND)uiControlHandle(uiControl(g_gui.window));
    if (!hwnd) return;

    HINSTANCE hinst = GetModuleHandle(NULL);
    HICON hicon = (HICON)LoadImage(hinst, MAKEINTRESOURCE(IDI_APP_ICON),
                                   IMAGE_ICON, 0, 0, LR_DEFAULTSIZE);
    int from_file = 0;

    if (!hicon) {
        const char *candidates[] = {
            "res/icon/ludo.ico",
            "../res/icon/ludo.ico",
            "../../res/icon/ludo.ico"
        };
        for (size_t i = 0; i < sizeof(candidates) / sizeof(candidates[0]); i++) {
            wchar_t *candidate_w = utf8_to_wide_dup(candidates[i]);
            if (!candidate_w) continue;
            hicon = (HICON)LoadImageW(NULL, candidate_w, IMAGE_ICON, 0, 0,
                                      LR_LOADFROMFILE | LR_DEFAULTSIZE);
            free(candidate_w);
            if (hicon) {
                from_file = 1;
                break;
            }
        }
    }

    if (!hicon) return;

    SendMessage(hwnd, WM_SETICON, ICON_SMALL, (LPARAM)hicon);
    SendMessage(hwnd, WM_SETICON, ICON_BIG, (LPARAM)hicon);
    g_gui.app_icon = hicon;
    g_gui.app_icon_from_file = from_file;
}

static int resolve_toolbar_png_path(const char *name, char *out, size_t out_sz) {
    const char *candidates[] = {
        "res/toolbars/32",
        "../res/toolbars/32",
        "../../res/toolbars/32"
    };
    for (size_t i = 0; i < sizeof(candidates) / sizeof(candidates[0]); i++) {
        snprintf(out, out_sz, "%s/%s", candidates[i], name);
        FILE *f = gui_fopen_utf8(out, "rb");
        if (f) {
            fclose(f);
            return 1;
        }
    }
    return 0;
}

static void toolbar_icons_init(void) {
    g_gui.toolbar_icon_ctx = ludo_icons_init();
    if (!g_gui.toolbar_icon_ctx) {
        return;
    }

    /* 32x32 icon with 4px visual padding all around. */
    ludo_icons_set_button_size((uintptr_t)uiControlHandle(uiControl(g_gui.tb_add)), TOOLBAR_BUTTON_SIZE, TOOLBAR_BUTTON_SIZE);
    ludo_icons_set_button_size((uintptr_t)uiControlHandle(uiControl(g_gui.tb_pause)), TOOLBAR_BUTTON_SIZE, TOOLBAR_BUTTON_SIZE);
    ludo_icons_set_button_size((uintptr_t)uiControlHandle(uiControl(g_gui.tb_resume)), TOOLBAR_BUTTON_SIZE, TOOLBAR_BUTTON_SIZE);
    ludo_icons_set_button_size((uintptr_t)uiControlHandle(uiControl(g_gui.tb_remove)), TOOLBAR_BUTTON_SIZE, TOOLBAR_BUTTON_SIZE);
    ludo_icons_set_button_size((uintptr_t)uiControlHandle(uiControl(g_gui.tb_plugin)), TOOLBAR_BUTTON_SIZE, TOOLBAR_BUTTON_SIZE);
    ludo_icons_set_button_size((uintptr_t)uiControlHandle(uiControl(g_gui.tb_setting)), TOOLBAR_BUTTON_SIZE, TOOLBAR_BUTTON_SIZE);
    ludo_icons_set_button_size((uintptr_t)uiControlHandle(uiControl(g_gui.tb_http)), TOOLBAR_BUTTON_SIZE, TOOLBAR_BUTTON_SIZE);
    ludo_icons_set_button_size((uintptr_t)uiControlHandle(uiControl(g_gui.tb_lua)), TOOLBAR_BUTTON_SIZE, TOOLBAR_BUTTON_SIZE);
    ludo_icons_set_button_size((uintptr_t)uiControlHandle(uiControl(g_gui.tb_about)), TOOLBAR_BUTTON_SIZE, TOOLBAR_BUTTON_SIZE);

    char path[512];
    if (resolve_toolbar_png_path(ICON_ADD, path, sizeof(path))) {
        ludo_icons_set_button_png(g_gui.toolbar_icon_ctx,
                                  (uintptr_t)uiControlHandle(uiControl(g_gui.tb_add)),
                                  path);
    }
    if (resolve_toolbar_png_path(ICON_PAUSE, path, sizeof(path))) {
        ludo_icons_set_button_png(g_gui.toolbar_icon_ctx,
                                  (uintptr_t)uiControlHandle(uiControl(g_gui.tb_pause)),
                                  path);
    }
    if (resolve_toolbar_png_path(ICON_RESUME, path, sizeof(path))) {
        ludo_icons_set_button_png(g_gui.toolbar_icon_ctx,
                                  (uintptr_t)uiControlHandle(uiControl(g_gui.tb_resume)),
                                  path);
    }
    if (resolve_toolbar_png_path(ICON_REMOVE, path, sizeof(path))) {
        ludo_icons_set_button_png(g_gui.toolbar_icon_ctx,
                                  (uintptr_t)uiControlHandle(uiControl(g_gui.tb_remove)),
                                  path);
    }
    if (resolve_toolbar_png_path(ICON_PLUGIN, path, sizeof(path))) {
        ludo_icons_set_button_png(g_gui.toolbar_icon_ctx,
                                  (uintptr_t)uiControlHandle(uiControl(g_gui.tb_plugin)),
                                  path);
    }
    if (resolve_toolbar_png_path(ICON_SETTING, path, sizeof(path))) {
        ludo_icons_set_button_png(g_gui.toolbar_icon_ctx,
                                  (uintptr_t)uiControlHandle(uiControl(g_gui.tb_setting)),
                                  path);
    
    }
    if (resolve_toolbar_png_path(ICON_HTTP, path, sizeof(path))) {
        ludo_icons_set_button_png(g_gui.toolbar_icon_ctx,
                                  (uintptr_t)uiControlHandle(uiControl(g_gui.tb_http)),
                                  path);
    }
    if (resolve_toolbar_png_path(ICON_LUA, path, sizeof(path))) {
        ludo_icons_set_button_png(g_gui.toolbar_icon_ctx,
                                  (uintptr_t)uiControlHandle(uiControl(g_gui.tb_lua)),
                                  path);
    }
    if (resolve_toolbar_png_path(ICON_ABOUT, path, sizeof(path))) {
        ludo_icons_set_button_png(g_gui.toolbar_icon_ctx,
                                  (uintptr_t)uiControlHandle(uiControl(g_gui.tb_about)),
                                  path);
    }
}

static void toolbar_icons_shutdown(void) {
    if (g_gui.toolbar_icon_ctx) {
        ludo_icons_shutdown(g_gui.toolbar_icon_ctx);
        g_gui.toolbar_icon_ctx = NULL;
    }
}
#endif

static DownloadRow *add_row(int id, const char *filename) {
    if (g_gui.row_count >= MAX_DOWNLOAD_ROWS) {
        static int warned = 0;
        if (!warned) { gui_log(LOG_WARNING, "MAX_DOWNLOAD_ROWS reached; further downloads will not appear in the table."); warned = 1; }
        return NULL;
    }

    DownloadRow *r = &g_gui.rows[g_gui.row_count];
    memset(r, 0, sizeof(*r));
    r->status.id = id;
    r->selected = 0;
    strncpy(r->status.filename, filename ? filename : "", sizeof(r->status.filename)-1);
    r->status.filename[sizeof(r->status.filename)-1] = '\0';
    r->status.state = DOWNLOAD_STATE_QUEUED;
    int idx = g_gui.row_count;
    g_gui.row_count++;
    if (g_downloads_model)
        uiTableModelRowInserted(g_downloads_model, idx);
    return r;
}

/* Context for Add URLs Window */
typedef struct {
    uiWindow *win;
    uiMultilineEntry *urls_entry;
} AddUrlsCtx;

/* Free dialog context on user-initiated close. */
static int on_child_window_closing(uiWindow *w, void *data) {
    persist_add_urls_window_state(w);
    if (data) free(data);
    uiControlDestroy(uiControl(w));
    return 0;
}

static void on_add_urls_submit(uiButton *b, void *ud) {
    (void)b;
    AddUrlsCtx *ctx = (AddUrlsCtx *)ud;
    char *text = uiMultilineEntryText(ctx->urls_entry);
    
    if (text) {
        char *p = text;
        while (p && *p) {
            char *end = strchr(p, '\n');
            if (end) *end = '\0'; /* Temporarily terminate the line */
            
            char *url = p;
            while (*url && isspace((unsigned char)*url)) url++; /* Trim leading */
            size_t url_len = strlen(url);
            if (url_len > 0) {
                char *tail = url + url_len - 1;
                while (tail >= url && isspace((unsigned char)*tail)) { *tail = '\0'; tail--; } /* Trim trailing */
            }

            if (is_valid_url(url)) {
                task_queue_push(&g_url_queue, url);
                gui_log(LOG_INFO, "Queued: %s", url);
            }
            p = end ? end + 1 : NULL;
        }
        uiFreeText(text);
    }
    persist_add_urls_window_state(ctx->win);
    uiControlDestroy(uiControl(ctx->win));
    free(ctx);
}

static void show_add_urls_window(const char *initial_text) {
    int window_width = 700;
    int window_height = 400;

    gui_get_window_size(LUDO_GUI_WINDOW_ADD_URLS, 700, 400, &window_width, &window_height);
    uiWindow *win = uiNewWindow("Add Multiple URLs", window_width, window_height, 0);
    uiWindowSetMargined(win, 1);
    
    uiBox *vbox = uiNewVerticalBox();
    uiBoxSetPadded(vbox, 1);
    
    uiLabel *lbl = uiNewLabel("Enter URLs (one per line):");
    uiBoxAppend(vbox, uiControl(lbl), 0);
    
    uiMultilineEntry *me = uiNewMultilineEntry();
    if (initial_text) uiMultilineEntrySetText(me, initial_text);
    uiBoxAppend(vbox, uiControl(me), 1);
    
    uiButton *btn = uiNewButton("Add to Queue");
    uiBoxAppend(vbox, uiControl(btn), 0);
    
    AddUrlsCtx *ctx = malloc(sizeof(AddUrlsCtx));
    if (!ctx) {
        gui_log(LOG_ERROR, "Failed to allocate Add URLs dialog context.");
        uiControlDestroy(uiControl(win));
        return;
    }
    ctx->win = win;
    ctx->urls_entry = me;
    
    uiButtonOnClicked(btn, on_add_urls_submit, ctx);
    uiWindowOnClosing(win, (int (*)(uiWindow *, void *))on_child_window_closing, ctx);
    
    uiWindowSetChild(win, uiControl(vbox));
    gui_enable_window_persistence(win, &g_add_urls_window_config_id);
    uiControlShow(uiControl(win));
}

static void on_add_clicked(uiButton *sender, void *data) {
    (void)sender; (void)data;

    char *url_text = uiEntryText(g_gui.url_entry);
    if (!url_text || url_text[0] == '\0') {
        uiFreeText(url_text);
        return;
    }

    char url[4096];
    strncpy(url, url_text, sizeof(url) - 1);
    url[sizeof(url) - 1] = '\0';
    uiFreeText(url_text);

    if (strncmp(url, "http://", 7) != 0 && strncmp(url, "https://", 8) != 0) {
        gui_log(LOG_ERROR, "Invalid URL — must start with http:// or https://");
        return;
    }

    task_queue_push(&g_url_queue, url);
    uiEntrySetText(g_gui.url_entry, "");
    char msg[512];
    snprintf(msg, sizeof(msg), "Queued: %s", url);
    gui_log(LOG_INFO, "%s", msg);
}

static void on_pause_clicked(uiButton *sender, void *data) {
    (void)sender; (void)data;
    int acted = 0;
    for (int i = 0; i < g_gui.row_count; i++) {
        DownloadRow *r = &g_gui.rows[i];
        if (r->selected) {
            if (r->status.id > 0) {
                if (download_manager_pause(r->status.id)) {
                    acted++;
                }
            }
            r->selected = 0; /* clear selection */
        }
    }
    if (acted > 0) {
        gui_log(LOG_INFO, "Paused checked downloads");
        if (g_downloads_model) for (int j = 0; j < g_gui.row_count; j++) uiTableModelRowChanged(g_downloads_model, j);
    } else {
        gui_log(LOG_WARNING, "No action taken.");
    }
}

static void on_resume_clicked(uiButton *sender, void *data) {
    (void)sender; (void)data;
    int acted = 0;
    for (int i = 0; i < g_gui.row_count; i++) {
        DownloadRow *r = &g_gui.rows[i];
        if (r->selected) {
            if (r->status.id > 0) {
                if (download_manager_resume(r->status.id)) {
                    acted++;
                }
            }
            r->selected = 0; /* clear selection */
        }
    }
    if (acted > 0) {
        gui_log(LOG_INFO, "Resumed checked downloads");
        if (g_downloads_model) for (int j = 0; j < g_gui.row_count; j++) uiTableModelRowChanged(g_downloads_model, j);
    } else {
        gui_log(LOG_WARNING, "No action taken.");
    }
}

static void on_remove_clicked(uiButton *sender, void *data) {
    (void)sender; (void)data;
    int acted = 0;

    /* BEST PRACTICE: Iterate backwards when deleting from an array */
    for (int i = g_gui.row_count - 1; i >= 0; i--) {
        DownloadRow *r = &g_gui.rows[i];
        if (r->selected) {
            if (r->status.id > 0) {
                if (download_manager_remove(r->status.id)) {
                    acted++;
                    /* download_manager_remove dispatches a final marked_for_removal
                     * progress update for all states (immediately for stopped ones,
                     * deferred via the worker for RUNNING/QUEUED).  Either way,
                     * gui_on_progress owns the row deletion — skip it here to
                     * prevent a use-after-free / dangling-state mismatch. */
                    continue;
                }
            }

            /* Fallback: backend remove was not applicable (id == 0) or failed.
             * Clean up the GUI row directly so it does not become a zombie. */
            for (int j = i; j < g_gui.row_count - 1; j++) {
                g_gui.rows[j] = g_gui.rows[j + 1];
            }
            g_gui.row_count--;

            if (g_downloads_model) {
                uiTableModelRowDeleted(g_downloads_model, i);
            }
        }
    }

    if (acted > 0) {
        gui_log(LOG_INFO, "Removed checked downloads");
    } else {
        gui_log(LOG_WARNING, "No action taken.");
    }
}

static void on_plugin_clicked(uiButton *sender, void *data) {
    (void)sender; (void)data;
    char *folder = uiOpenFolder(g_gui.window);
    if (!folder || folder[0] == '\0') {
        if (folder) uiFreeText(folder);
        return;
    }

    lua_engine_load_plugins(folder);

    char msg[1024];
    snprintf(msg, sizeof(msg), "Plugins reloaded from: %s", folder);
    gui_log(LOG_SUCCESS, "%s", msg);
    uiFreeText(folder);
}

static void on_setting_clicked(uiButton *sender, void *data) {
    (void)sender; (void)data;
    char *folder = uiOpenFolder(g_gui.window);
    if (!folder || folder[0] == '\0') {
        if (folder) uiFreeText(folder);
        return;
    }

    download_manager_set_output_dir(folder);
    ludo_config_set_output_dir(folder);

    char msg[1024];
    snprintf(msg, sizeof(msg), "Default output directory changed to: %s", folder);
    gui_log(LOG_SUCCESS, "%s", msg);
    uiFreeText(folder);
}

static void on_http_clicked(uiButton *sender, void *data) {
    (void)sender; (void)data;

    // --- HTTP Test Window ---
    int window_width = 900;
    int window_height = 700;

    gui_get_window_size(LUDO_GUI_WINDOW_HTTP_TEST, 900, 700, &window_width, &window_height);
    uiWindow *win = uiNewWindow("HTTP Request Tester", window_width, window_height, 0);
    uiWindowSetMargined(win, 1);

    uiBox *vbox = uiNewVerticalBox();
    uiBoxSetPadded(vbox, 1);

    uiEntry *url_entry = uiNewEntry();
    uiEntrySetText(url_entry, "https://www.andersonkenya1.net/files/file/93-spongebob-squarepants/");
    uiBoxAppend(vbox, uiControl(url_entry), 0);

    uiMultilineEntry *header_entry = uiNewMultilineEntry();
    uiMultilineEntrySetText(header_entry, "upgrade-insecure-requests: 1");
    uiBoxAppend(vbox, uiControl(header_entry), 0);

    uiButton *send_btn = uiNewButton("Send HTTP Request");
    uiBoxAppend(vbox, uiControl(send_btn), 0);

    uiMultilineEntry *header_resp_entry = uiNewMultilineEntry();
    uiMultilineEntrySetReadOnly(header_resp_entry, 1);
    uiBoxAppend(vbox, uiControl(header_resp_entry), 1);

    uiMultilineEntry *output_entry = uiNewMultilineEntry();
    uiMultilineEntrySetReadOnly(output_entry, 1);
    uiBoxAppend(vbox, uiControl(output_entry), 1);

    HttpTestCtx *ctx = malloc(sizeof(HttpTestCtx));
    if (!ctx) {
        gui_log(LOG_ERROR, "[HTTP DEBUG] ctx alloc failed");
        uiControlDestroy(uiControl(win));
        return;
    }
    ctx->url_entry = url_entry;
    ctx->header_entry = header_entry;
    ctx->header_resp_entry = header_resp_entry;
    ctx->output_entry = output_entry;
    ctx->win = win;
    g_active_http_test_ctx = ctx;

    uiButtonOnClicked(send_btn, http_test_on_send, ctx);
    uiWindowSetChild(win, uiControl(vbox));
    gui_enable_window_persistence(win, &g_http_test_window_config_id);
    uiControlShow(uiControl(win));
    uiWindowOnClosing(win, destroy_http_test_window, ctx);
}

static void on_lua_clicked(uiButton *sender, void *data) {
    (void)sender; (void)data;
    // --- Lua Test Window ---
    int window_width = 960;
    int window_height = 640;

    gui_get_window_size(LUDO_GUI_WINDOW_LUA_TEST, 960, 640, &window_width, &window_height);
    uiWindow *win = uiNewWindow("Lua Script Tester", window_width, window_height, 0);
    uiWindowSetMargined(win, 1);

    uiBox *content_box = uiNewHorizontalBox();
    uiBoxSetPadded(content_box, 1);

    uiBox *snippet_box = uiNewVerticalBox();
    uiBoxSetPadded(snippet_box, 1);

    uiLabel *snippet_label = uiNewLabel("Snippets");
    uiBoxAppend(snippet_box, uiControl(snippet_label), 0);

    uiBox *editor_box = uiNewVerticalBox();
    uiBoxSetPadded(editor_box, 1);

    uiMultilineEntry *script_entry = uiNewMultilineEntry();
    // uiMultilineEntrySetPlaceholder(script_entry, ...) is not available in libui-ng; skip placeholder.
    uiBoxAppend(editor_box, uiControl(script_entry), 1);

    uiButton *exec_btn = uiNewButton("Execute Lua Script");
    uiBoxAppend(editor_box, uiControl(exec_btn), 0);

    uiMultilineEntry *output_entry = uiNewMultilineEntry();
    uiMultilineEntrySetReadOnly(output_entry, 1);
    uiBoxAppend(editor_box, uiControl(output_entry), 1);

    LuaTestCtx *ctx = malloc(sizeof(LuaTestCtx));
    if (!ctx) {
        gui_log(LOG_ERROR, "[LUA DEBUG] ctx alloc failed");
        uiControlDestroy(uiControl(win));
        return;
    }
    memset(ctx, 0, sizeof(*ctx));
    ctx->script_entry = script_entry;
    ctx->output_entry = output_entry;
    ctx->win = win;
    load_snippets_from_dir(ctx, "snippets");
    ctx->snippet_model_handler.ctx = ctx;
    ctx->snippet_model_handler.handler.NumColumns = snippet_model_num_columns;
    ctx->snippet_model_handler.handler.ColumnType = snippet_model_column_type;
    ctx->snippet_model_handler.handler.NumRows = snippet_model_num_rows;
    ctx->snippet_model_handler.handler.CellValue = snippet_model_cell_value;
    ctx->snippet_model_handler.handler.SetCellValue = snippet_model_set_cell_value;
    ctx->snippet_model = uiNewTableModel(&ctx->snippet_model_handler.handler);
    {
        uiTableParams snippet_params;
        memset(&snippet_params, 0, sizeof(snippet_params));
        snippet_params.Model = ctx->snippet_model;
        ctx->snippet_table = uiNewTable(&snippet_params);
        uiTableAppendTextColumn(ctx->snippet_table, "Snippet", 0, uiTableModelColumnNeverEditable, NULL);
        uiTableHeaderSetVisible(ctx->snippet_table, 0);
        uiTableSetSelectionMode(ctx->snippet_table, uiTableSelectionModeOne);
        uiTableOnRowDoubleClicked(ctx->snippet_table, on_snippet_row_double_clicked, ctx);
        gui_apply_table_widths(ctx->snippet_table, LUDO_GUI_TABLE_SNIPPETS);
        uiBoxAppend(snippet_box, uiControl(ctx->snippet_table), 1);
    }

    uiBoxAppend(content_box, uiControl(snippet_box), 0);
    uiBoxAppend(content_box, uiControl(editor_box), 1);

    uiButtonOnClicked(exec_btn, lua_test_on_exec, ctx);

    uiWindowSetChild(win, uiControl(content_box));
    gui_enable_window_persistence(win, &g_lua_test_window_config_id);
    uiControlShow(uiControl(win));
    uiWindowOnClosing(win, destroy_lua_test_window, ctx);
}

static void on_about_clicked(uiButton *sender, void *data) {
    (void)sender; (void)data;
    uiMsgBox(g_gui.window,
             "About LUDO",
             "LUDO\n\n"
             "Lua powered and streamlined download manager");
}

/* ------------------------------------------------------------------ */
/* Progress callback (invoked on main thread via uiQueueMain)          */
/* ------------------------------------------------------------------ */

void gui_on_progress(const ProgressUpdate *update, void *user_data) {
    (void)user_data;

    g_gui.active_download_id = update->status.id;

    /* Backend has fully freed this download.  Delete the GUI row and bail out
     * before any find_row / add_row call — that avoids resurrecting a ghost
     * row when a deferred worker cleanup arrives after on_remove_clicked. */
    if (update->marked_for_removal) {
        int idx = find_row_index(update->status.id);
        if (idx >= 0) {
            for (int j = idx; j < g_gui.row_count - 1; j++)
                g_gui.rows[j] = g_gui.rows[j + 1];
            g_gui.row_count--;
            if (g_downloads_model) uiTableModelRowDeleted(g_downloads_model, idx);
        }
        return;
    }

    DownloadRow *r = find_row(update->status.id);
    if (!r) {
        r = add_row(update->status.id, update->status.filename);
        if (!r) return;
    }

    /* Update stored data fields */
    DownloadState prev_state = r->status.state;
    {
        char prev_filename[sizeof(r->status.filename)];
        time_t prev_start_time = r->status.start_time;

        strncpy(prev_filename, r->status.filename, sizeof(prev_filename) - 1);
        prev_filename[sizeof(prev_filename) - 1] = '\0';
        r->status = update->status;
        if (update->status.filename[0] == '\0') {
            strncpy(r->status.filename, prev_filename, sizeof(r->status.filename) - 1);
            r->status.filename[sizeof(r->status.filename) - 1] = '\0';
        }
        if (r->status.start_time <= 0) {
            if (prev_start_time > 0) {
                r->status.start_time = prev_start_time;
            } else if (r->status.state == DOWNLOAD_STATE_RUNNING) {
                r->status.start_time = time(NULL);
            }
        }
    }
    /* --- SOUND NOTIFICATION HOOKS --- */
    if (prev_state != r->status.state) {
        if (prev_state == DOWNLOAD_STATE_QUEUED && r->status.state == DOWNLOAD_STATE_RUNNING) {
            play_sound("res/sounds/start_download.wav");
        } 
        else if (prev_state != DOWNLOAD_STATE_COMPLETED && r->status.state == DOWNLOAD_STATE_COMPLETED) {
            play_sound("res/sounds/complete_download.wav");
        } 
        else if (prev_state != DOWNLOAD_STATE_FAILED && r->status.state == DOWNLOAD_STATE_FAILED) {
            play_sound("res/sounds/error_download.wav");
        }
    }
    /* -------------------------------- */

    int idx = find_row_index(update->status.id);
    if (idx >= 0 && g_downloads_model) uiTableModelRowChanged(g_downloads_model, idx);
}

/* ------------------------------------------------------------------ */
/* Window close callback                                                */
/* ------------------------------------------------------------------ */

static void begin_app_shutdown(void) {
    if (g_gui.shutdown_requested) return;

    persist_main_window_state();
    g_gui.shutdown_requested = 1;
    task_queue_shutdown(&g_url_queue);
    gui_shutdown();
    download_manager_shutdown();
    lua_engine_shutdown();

#ifdef _WIN32
    toolbar_icons_shutdown();
    if (g_gui.app_icon && g_gui.app_icon_from_file) {
        DestroyIcon(g_gui.app_icon);
        g_gui.app_icon = NULL;
        g_gui.app_icon_from_file = 0;
    }
#endif
}

void gui_shutdown(void) {
    if (!g_gui.url_worker_started) return;

    ludo_thread_join(g_gui.url_worker);
    g_gui.url_worker_started = 0;
}

int gui_is_shutdown_requested(void) {
    return g_gui.shutdown_requested;
}

static int on_window_close(uiWindow *w, void *data) {
    (void)w; (void)data;
    begin_app_shutdown();
    uiQuit();
    return 1; /* destroy window */
}

static int on_should_quit(void *data) {
    (void)data;
    begin_app_shutdown();
    uiControlDestroy(uiControl(g_gui.window));
    return 1;
}

/* ========================================================================= */
/* Menu Wrappers & Callbacks                                                 */
/* ========================================================================= */
static void menu_add_urls_cb(uiMenuItem *sender, uiWindow *w, void *data) {
    (void)sender; (void)w; (void)data;
    char *clipboard = get_clipboard_text();
    if (clipboard && clipboard[0] != '\0') {
        // check if clipboard contains "https://" or  "http://" before showing the dialog
        if (strstr(clipboard, "http://") || strstr(clipboard, "https://")) {
            show_add_urls_window(clipboard);
        } else {
            show_add_urls_window(NULL);
        }
        free_clipboard_text(clipboard);
    }
}
static void menu_pause_cb(uiMenuItem *sender, uiWindow *w, void *data)   { on_pause_clicked(NULL, NULL); }
static void menu_resume_cb(uiMenuItem *sender, uiWindow *w, void *data)  { on_resume_clicked(NULL, NULL); }
static void menu_remove_cb(uiMenuItem *sender, uiWindow *w, void *data)  { on_remove_clicked(NULL, NULL); }
static void menu_open_folder_cb(uiMenuItem *sender, uiWindow *w, void *data) {
    (void)sender; (void)w; (void)data;
    const char *dir = download_manager_get_output_dir();
    char abs_path[4096];

#ifdef _WIN32
    /* Resolve to absolute path for Windows Explorer */
    {
        wchar_t *dir_w = utf8_to_wide_dup(dir);
        wchar_t wide_buf[4096];
        wchar_t *abs_path_w = NULL;
        if (dir_w) {
            abs_path_w = _wfullpath(wide_buf, dir_w, sizeof(wide_buf) / sizeof(wide_buf[0]));
            if (!abs_path_w) abs_path_w = dir_w;
            if (wide_to_utf8(abs_path_w, abs_path, sizeof(abs_path))) {
                abs_path[sizeof(abs_path) - 1] = '\0';
            }
        }
        if (!abs_path_w || (INT_PTR)ShellExecuteW(NULL, L"open", abs_path_w, NULL, NULL, SW_SHOWNORMAL) <= 32) {
            gui_log(LOG_ERROR, "Failed to open download folder.");
        }
        free(dir_w);
    }
#else
    /* Resolve to absolute path for POSIX */
    if (!realpath(dir, abs_path)) {
        strncpy(abs_path, dir, sizeof(abs_path));
        abs_path[sizeof(abs_path) - 1] = '\0';
    }
    {
        pid_t pid;
#ifdef __APPLE__
        char *argv[] = { "open", abs_path, NULL };
#else
        char *argv[] = { "xdg-open", abs_path, NULL };
#endif
        if (posix_spawnp(&pid, argv[0], NULL, NULL, argv, environ) != 0) {
            gui_log(LOG_ERROR, "Failed to open download folder.");
        }
    }
#endif
}
static void menu_setting_cb(uiMenuItem *sender, uiWindow *w, void *data) { on_setting_clicked(NULL, NULL); }
static void menu_plugin_cb(uiMenuItem *sender, uiWindow *w, void *data)  { on_plugin_clicked(NULL, NULL); }
static void menu_http_cb(uiMenuItem *sender, uiWindow *w, void *data) { on_http_clicked(NULL, NULL); }
static void menu_lua_cb(uiMenuItem *sender, uiWindow *w, void *data)  { on_lua_clicked(NULL, NULL); }
static void menu_lua_ref_cb(uiMenuItem *sender, uiWindow *w, void *data) {
    uiMsgBox(w, "LUA Reference", "Ludo Lua API:\n- ludo.download(url, [dir])\n- ludo.log(msg)\n- ludo.get_clipboard()");
}

static void menu_about_cb(uiMenuItem *sender, uiWindow *w, void *data) {
    uiMsgBox(w, "About", "Ludo Lua powered Download Manager\nVersion 1.0\nA lightweight C/Lua download manager.");
}

/* ========================================================================= */
/* Menu Initialization (Must run BEFORE uiNewWindow)                         */
/* ========================================================================= */

static void setup_menus(void) {
    uiMenu *menu;
    uiMenuItem *item;

    /* ---- 1. Download Menu ---- */
    menu = uiNewMenu("Download");
    item = uiMenuAppendItem(menu, "Add URLs\tCtrl+U");
    uiMenuItemOnClicked(item, menu_add_urls_cb, NULL);
    uiMenuAppendSeparator(menu);
    item = uiMenuAppendItem(menu, "Pause\tCtrl+P");
    uiMenuItemOnClicked(item, menu_pause_cb, NULL);
    item = uiMenuAppendItem(menu, "Resume\tCtrl+R");
    uiMenuItemOnClicked(item, menu_resume_cb, NULL);
    item = uiMenuAppendItem(menu, "Remove\tCtrl+Del");
    uiMenuItemOnClicked(item, menu_remove_cb, NULL);
    uiMenuAppendSeparator(menu); // Add a visual separator
    item = uiMenuAppendItem(menu, "Open Folder\tCtrl+O");
    uiMenuItemOnClicked(item, menu_open_folder_cb, NULL);

    /* ---- 2. Tools Menu ---- */
    menu = uiNewMenu("Tools");
    item = uiMenuAppendItem(menu, "Setting\tCtrl+S");
    uiMenuItemOnClicked(item, menu_setting_cb, NULL);
    item = uiMenuAppendItem(menu, "Plugins\tCtrl+Shift+P");
    uiMenuItemOnClicked(item, menu_plugin_cb, NULL);
    
    /* Assuming on_http_clicked and on_lua_clicked were already updated to menu signatures */
    item = uiMenuAppendItem(menu, "HTTP Tester\tCtrl+H");
    uiMenuItemOnClicked(item, menu_http_cb, NULL);
    item = uiMenuAppendItem(menu, "LUA Tester\tCtrl+L");
    uiMenuItemOnClicked(item, menu_lua_cb, NULL);

    /* ---- 3. Help Menu ---- */
    menu = uiNewMenu("Help");
    item = uiMenuAppendItem(menu, "LUA Reference\tF1");
    uiMenuItemOnClicked(item, menu_lua_ref_cb, NULL);
    
    /* Cross-platform native standard for About */
    item = uiMenuAppendAboutItem(menu); 
    uiMenuItemOnClicked(item, menu_about_cb, NULL);
}

static int g_sort_col = -1;
static int g_sort_asc = 1;

static int cmp_rows(const void *a, const void *b) {
    DownloadRow *ra = (DownloadRow *)a;
    DownloadRow *rb = (DownloadRow *)b;
    int res = 0;
    switch (g_sort_col) {
        case 1: res = strcmp(ra->status.filename, rb->status.filename); break; /* Name */
        case 2: res = (ra->status.total_bytes > rb->status.total_bytes) - (ra->status.total_bytes < rb->status.total_bytes); break; /* Size */
        case 3: res = (ra->status.state > rb->status.state) - (ra->status.state < rb->status.state); break; /* Status */
        case 4: res = (ra->status.start_time > rb->status.start_time) - (ra->status.start_time < rb->status.start_time); break; /* Added */
        case 5: res = (ra->status.speed_bps > rb->status.speed_bps) - (ra->status.speed_bps < rb->status.speed_bps); break; /* Speed */
    }
    return g_sort_asc ? res : -res;
}

static void on_header_clicked(uiTable *t, int column, void *data) {
    (void)t; (void)data;
    /* Skip sorting for Checkbox (0), Progress (6), and Remove Button (7) */
    if (column == 0 || column >= 6) return; 

    if (g_sort_col == column) {
        g_sort_asc = !g_sort_asc; /* Reverse direction */
    } else {
        g_sort_col = column; 
        g_sort_asc = 1; 
    }

    qsort(g_gui.rows, g_gui.row_count, sizeof(DownloadRow), cmp_rows);

    /* Notify the model that every row has changed. libui-ng exposes no
       bulk-invalidation call (uiTableModelRowChanged maps 1:1 to LVM_UPDATE
       on Windows), so we send one notification per row. Row count is bounded
       by MAX_DOWNLOAD_ROWS (64), so the overhead is acceptable. */
    for (int i = 0; i < g_gui.row_count; i++) {
        uiTableModelRowChanged(g_downloads_model, i);
    }
}

/* ------------------------------------------------------------------ */
/* gui_create                                                           */
/* ------------------------------------------------------------------ */

void gui_create(void) {
    /* IMPORTANT: Setup menus BEFORE creating the main window, as some platforms (e.g. macOS) require the menu to exist first for proper integration. */
    setup_menus();
    
    memset(&g_gui, 0, sizeof(g_gui));

    /* Outer window */
    int main_window_width = 800;
    int main_window_height = 600;

    gui_get_window_size(LUDO_GUI_WINDOW_MAIN, 800, 600, &main_window_width, &main_window_height);
    g_gui.window = uiNewWindow("LUDO - LUa DOwnloader", main_window_width, main_window_height, 1);
    uiWindowSetMargined(g_gui.window, 1);
    uiWindowOnClosing(g_gui.window, on_window_close, NULL);
    uiOnShouldQuit(on_should_quit, NULL);

#ifdef _WIN32
    set_main_window_icon();
#endif

    /* Root vertical box */
    uiBox *root = uiNewVerticalBox();
    uiBoxSetPadded(root, 1);

    // uiLabel *title = uiNewLabel("Download Manager");
    // uiBoxAppend(root, uiControl(title), 0);

    /* ---- Toolbar ---- */
    // uiBox *toolbar = uiNewHorizontalBox();
    // uiBoxSetPadded(toolbar, 0);

    // g_gui.tb_add = uiNewButton("Add");
    // uiButtonOnClicked(g_gui.tb_add, on_add_clicked, NULL);
    // uiBoxAppend(toolbar, uiControl(g_gui.tb_add), 0);

    // g_gui.tb_pause = uiNewButton("Pause");
    // uiButtonOnClicked(g_gui.tb_pause, on_pause_clicked, NULL);
    // uiBoxAppend(toolbar, uiControl(g_gui.tb_pause), 0);

    // g_gui.tb_resume = uiNewButton("Resume");
    // uiButtonOnClicked(g_gui.tb_resume, on_resume_clicked, NULL);
    // uiBoxAppend(toolbar, uiControl(g_gui.tb_resume), 0);

    // g_gui.tb_remove = uiNewButton("Remove");
    // uiButtonOnClicked(g_gui.tb_remove, on_remove_clicked, NULL);
    // uiBoxAppend(toolbar, uiControl(g_gui.tb_remove), 0);

    // uiBoxAppend(toolbar, uiControl(uiNewVerticalSeparator()), 0);

    // g_gui.tb_plugin = uiNewButton("Plugin");
    // uiButtonOnClicked(g_gui.tb_plugin, on_plugin_clicked, NULL);
    // uiBoxAppend(toolbar, uiControl(g_gui.tb_plugin), 0);

    // g_gui.tb_setting = uiNewButton("Setting");
    // uiButtonOnClicked(g_gui.tb_setting, on_setting_clicked, NULL);
    // uiBoxAppend(toolbar, uiControl(g_gui.tb_setting), 0);

    // g_gui.tb_http = uiNewButton("HTTP");
    // uiButtonOnClicked(g_gui.tb_http, on_http_clicked, NULL);
    // uiBoxAppend(toolbar, uiControl(g_gui.tb_http), 0);

    // g_gui.tb_lua = uiNewButton("Lua");
    // uiButtonOnClicked(g_gui.tb_lua, on_lua_clicked, NULL);
    // uiBoxAppend(toolbar, uiControl(g_gui.tb_lua), 0);

    // g_gui.tb_about = uiNewButton("About");
    // uiButtonOnClicked(g_gui.tb_about, on_about_clicked, NULL);
    // uiBoxAppend(toolbar, uiControl(g_gui.tb_about), 0);

    // uiBoxAppend(root, uiControl(toolbar), 0);

#ifdef _WIN32
    // toolbar_icons_init();
#endif

    // uiBoxAppend(root, uiControl(uiNewHorizontalSeparator()), 0);

    /* ---- URL input row ---- */
    uiBox *input_row = uiNewHorizontalBox();
    uiBoxSetPadded(input_row, 1);

    /* ---- URL Label ---- */
    uiLabel *url_label = uiNewLabel("URL");
    uiBoxAppend(input_row, uiControl(url_label), 0);

    /* ---- URL Entry and Add Button ---- */
    g_gui.url_entry = uiNewEntry();
    uiEntrySetText(g_gui.url_entry, "");
    uiBoxAppend(input_row, uiControl(g_gui.url_entry), 1 /* stretchy */);

    g_gui.tb_add = uiNewButton(" +  Add Download ");
    uiButtonOnClicked(g_gui.tb_add, on_add_clicked, NULL);
    uiBoxAppend(input_row, uiControl(g_gui.tb_add), 0);

    uiBoxAppend(root, uiControl(input_row), 0);

    /* Table model handler */
    memset(&g_downloads_mh, 0, sizeof(g_downloads_mh));
    g_downloads_mh.NumColumns = downloads_modelNumColumns;
    g_downloads_mh.ColumnType = downloads_modelColumnType;
    g_downloads_mh.NumRows = downloads_modelNumRows;
    g_downloads_mh.CellValue = downloads_modelCellValue;
    g_downloads_mh.SetCellValue = downloads_modelSetCellValue;

    /* Create the table model and table */
    g_downloads_model = uiNewTableModel(&g_downloads_mh);
    uiTableParams tp;
    memset(&tp, 0, sizeof(tp));
    tp.Model = g_downloads_model;
    tp.RowBackgroundColorModelColumn = -1;
    uiTable *t = uiNewTable(&tp);
    g_downloads_table = t;

    /* Handle header clicks for sorting */
    uiTableHeaderOnClicked(t, on_header_clicked, NULL);

    /* Append columns in new order: id, name, size, status, added, speed, progress */
    uiTableAppendCheckboxColumn(t, "#", 0, uiTableModelColumnAlwaysEditable);
    uiTableAppendTextColumn(t, "Name", 1, uiTableModelColumnNeverEditable, NULL);
    uiTableAppendTextColumn(t, "Size", 2, uiTableModelColumnNeverEditable, NULL);
    uiTableAppendTextColumn(t, "Status", 3, uiTableModelColumnNeverEditable, NULL);
    uiTableAppendTextColumn(t, "Added", 4, uiTableModelColumnNeverEditable, NULL);
    uiTableAppendTextColumn(t, "Speed", 5, uiTableModelColumnNeverEditable, NULL);
    uiTableAppendProgressBarColumn(t, "Progress", 6);
    uiTableAppendButtonColumn(t, "@", 7, 8);
    gui_apply_table_widths(t, LUDO_GUI_TABLE_DOWNLOADS);

    uiBoxAppend(root, uiControl(t), 1);

    uiBoxAppend(root, uiControl(uiNewHorizontalSeparator()), 0);

    /* ---- Log area ---- */
    g_gui.log_view = uiNewMultilineEntry();
    uiMultilineEntrySetReadOnly(g_gui.log_view, 1);

    uiGroup *log_group = uiNewGroup("Activity Log");
    uiGroupSetMargined(log_group, 1);
    uiGroupSetChild(log_group, uiControl(g_gui.log_view));
    uiBoxAppend(root, uiControl(log_group), 1 /* stretchy */);

    uiWindowSetChild(g_gui.window, uiControl(root));
    gui_enable_window_persistence(g_gui.window, &g_main_window_config_id);
    uiControlShow(uiControl(g_gui.window));
    /* Apply clipboard check now (on creation) and also when window is focused */
    paste_clipboard_url_if_valid();
    uiWindowOnFocusChanged(g_gui.window, on_main_window_focus_changed, NULL);

    /* Register progress callback with the download manager */
    download_manager_set_progress_cb(gui_on_progress, NULL);

    /* Start a worker thread that feeds URLs from the queue to the Lua engine */
    if (ludo_thread_create(&g_gui.url_worker, url_worker_thread, NULL) == 0) {
        g_gui.url_worker_started = 1;
    } else {
        gui_log(LOG_ERROR, "Failed to start URL worker thread.");
    }
    /* We intentionally don't join this thread — it exits when the queue
       is shut down during download_manager_shutdown(). */
    
    {
        char msg[256];
        snprintf(msg, sizeof(msg), "LUDO v%s - %s (%s)", LUDO_VERSION_STR, LUDO_DESCRIPTION, LUDO_COMPANY);
        gui_log(LOG_INFO, "%s", msg);
        lua_engine_info();
    }
    
    gui_log(LOG_INFO, "Ready. Paste a URL and click [Add Download].");
}
