#include "gui.h"
#include "lua_engine.h"
#include "http_module.h"
#include "ludo_module.h"
#include "../third_party/libuilua/libuilua.h"
#include "../third_party/lua-5.2.4/src/lua.h"
#include "../third_party/lua-5.2.4/src/lauxlib.h"
#include "../third_party/lua-5.2.4/src/lualib.h"
#include "download_manager.h"
#include "thread_queue.h"

#include "ui.h"
#include "version.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <ctype.h>

#if defined(__linux__) || defined(__unix__)
#include <gtk/gtk.h>
#endif

#ifdef _WIN32
#include <windows.h>
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

/* ------------------------------------------------------------------ */
/* Download row UI element                                              */
/* ------------------------------------------------------------------ */

typedef struct {
    int             download_id;
    int             selected;      /* checkbox selection state */
    char            filename[512]; /* filename or URL snippet */
    DownloadState   state;
    int             progress_pct;  /* 0-100 */
    double          speed_bps;
    int64_t         total_bytes;
    int64_t         downloaded_bytes;
    time_t          start_time;
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

typedef struct LuaTestCtx {
    uiMultilineEntry *script_entry;
    uiMultilineEntry *output_entry;
    uiWindow *win;
} LuaTestCtx;

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

    ludo_mutex_t     log_mutex;

#ifdef _WIN32
    void            *toolbar_icon_ctx;
    HICON            app_icon;
    int              app_icon_from_file;
#endif
} g_gui;

/* Task queue shared with worker threads (initialised in main.c) */
extern TaskQueue g_url_queue;

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
            gui_log(LOG_INFO, msg);
            download_manager_add(task.url,
                                 download_manager_get_output_dir(),
                                 DOWNLOAD_NOW);
        }
    }
    return NULL;
}

/* Auto-size table columns based on content (simple char-count heuristic) */
static void autosize_downloads_table(void) {
    if (!g_downloads_table) return;
    if (!g_gui.window) return;

    /* Desired percentage widths for columns 0..6: id,name,size,status,started,speed,progress */
    const int percents[7] = {1,35,20,8,8,10,14};

    int win_w = 0, win_h = 0;
    uiWindowContentSize(g_gui.window, &win_w, &win_h);
    if (win_w <= 0) return;

    /* Account for some padding/margins inside the window */
    int available = win_w - 40; if (available < 200) available = win_w;

    for (int col = 0; col < 7; col++) {
        int w = (available * percents[col]) / 100;
        /* Enforce sensible minimums */
        if (col == 0) { if (w < 20) w = 20; }
        else if (col == 1) { if (w < 140) w = 140; }
        else if (col == 2) { if (w < 80) w = 80; }
        else if (col == 3) { if (w < 70) w = 70; }
        else if (col == 4) { if (w < 70) w = 70; }
        else if (col == 5) { if (w < 80) w = 80; }
        else if (col == 6) { if (w < 110) w = 110; }
        uiTableColumnSetWidth(g_downloads_table, col, w);
    }
}

/* Window position/size changed handler — recompute column widths */
static void on_main_window_pos_changed(uiWindow *w, void *data) {
    (void)w; (void)data;
    autosize_downloads_table();
}

/* When the main window gains focus, check clipboard for a URL and paste it into the URL entry
   if the entry is currently empty. Only implemented on Windows for now. */
static void on_main_window_focus_changed(uiWindow *w, void *data) {
    (void)w; (void)data;
    /* Only act when window is focused */
    if (!uiWindowFocused(g_gui.window)) return;

    /* If user already entered something, do not overwrite */
    char *cur = uiEntryText(g_gui.url_entry);
    int should_paste = 0;
    if (!cur || cur[0] == '\0') should_paste = 1;
    if (cur) uiFreeText(cur);
    if (!should_paste) return;
    /* Paste from clipboard if it contains a valid URL */
    /* This uses the shared helper below. */
    /* Note: helper will re-check entry emptiness to avoid race conditions. */
    (void)w;
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
    if (strchr(s, ' ') || strchr(s, '\t') || strchr(s, '\n')) return 0;
    if (!strchr(s, '.')) return 0;
    if (strlen(s) < 3) return 0;
    return 1;
}

/* Read clipboard (platform-specific) and paste URL into the URL entry if valid and if
   the entry is currently empty. This is safe to call at startup or on focus. */
void paste_clipboard_url_if_valid(void) {
    if (!g_gui.url_entry) return;
    char *cur = uiEntryText(g_gui.url_entry);
    int should_paste = 0;
    if (!cur || cur[0] == '\0') should_paste = 1;
    if (cur) uiFreeText(cur);
    if (!should_paste) return;

    /* Platform-specific clipboard retrieval */
#ifdef _WIN32
    if (!IsClipboardFormatAvailable(CF_UNICODETEXT) && !IsClipboardFormatAvailable(CF_TEXT)) return;
    if (!OpenClipboard(NULL)) return;
    char *utf8 = NULL;
    HANDLE h = GetClipboardData(CF_UNICODETEXT);
    if (h) {
        wchar_t *wtext = (wchar_t *)GlobalLock(h);
        if (wtext) {
            int needed = WideCharToMultiByte(CP_UTF8, 0, wtext, -1, NULL, 0, NULL, NULL);
            if (needed > 0) {
                utf8 = (char *)malloc((size_t)needed);
                if (utf8) WideCharToMultiByte(CP_UTF8, 0, wtext, -1, utf8, needed, NULL, NULL);
            }
            GlobalUnlock(h);
        }
    } else {
        HANDLE h2 = GetClipboardData(CF_TEXT);
        if (h2) {
            char *ansi = (char *)GlobalLock(h2);
            if (ansi) {
                utf8 = _strdup(ansi);
                GlobalUnlock(h2);
            }
        }
    }
    CloseClipboard();
    if (!utf8) return;
    char *s = utf8;
    while (*s && isspace((unsigned char)*s)) s++;
    if (is_valid_url(s)) uiEntrySetText(g_gui.url_entry, s);
    free(utf8);
#elif defined(__linux__) || defined(__unix__)
    gchar *txt = gtk_clipboard_wait_for_text(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD));
    if (!txt) return;
    char *s = txt;
    while (*s && isspace((unsigned char)*s)) s++;
    if (is_valid_url(s)) uiEntrySetText(g_gui.url_entry, s);
    g_free(txt);
#else
    /* macOS or other platforms: not implemented */
#endif
}

/* ------------------------------------------------------------------ */
/* uiQueueMain helpers                                                  */
/* ------------------------------------------------------------------ */

typedef struct { LogLevel level; char msg[1024]; } LogPkt;

static void log_on_main(void *data) {
    LogPkt *pkt = (LogPkt *)data;

    const char *prefix;
    switch (pkt->level) {
        case LOG_SUCCESS: prefix = "[OK]   "; break;
        case LOG_ERROR:   prefix = "[ERR]  "; break;
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

void gui_log(LogLevel level, const char *msg) {
    LogPkt *pkt = (LogPkt *)malloc(sizeof(LogPkt));
    if (!pkt) return;
    pkt->level = level;
    strncpy(pkt->msg, msg, sizeof(pkt->msg) - 1);
    pkt->msg[sizeof(pkt->msg) - 1] = '\0';
    // Also log to stderr for crash diagnosis
    fprintf(stderr, "[gui_log][%d] %s\n", (int)level, msg);
    fflush(stderr);
    uiQueueMain(log_on_main, pkt);
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
        char *saveptr = NULL;
        char *line = strtok_r(lines, "\n", &saveptr);
        while (line) {
            char *colon = strchr(line, ':');
            if (colon) {
                *colon = '\0';
                const char *k = line;
                const char *v = colon + 1;
                while (*v == ' ') v++;
                lua_pushstring(L, k);
                lua_pushstring(L, v);
                lua_settable(L, -3);
            }
            line = strtok_r(NULL, "\n", &saveptr);
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
    luaL_requiref(L, "ui", luaopen_libuilua, 1);
    lua_pop(L, 1);

    int res = luaL_loadstring(L, script);
    if (res == LUA_OK) res = lua_pcall(L, 0, LUA_MULTRET, 0);
    if (res != LUA_OK) {
        const char *err = lua_tostring(L, -1);
        gui_log(LOG_ERROR, err ? err : "(nil)");
        uiMultilineEntrySetText(ctx->output_entry, err ? err : "Lua error");
        lua_close(L);
        uiFreeText((char*)script);
        return;
    }

    int nret = lua_gettop(L);
    char buf[4096] = {0};
    for (int i = 1; i <= nret; i++) {
        size_t len;
        const char *s = lua_tolstring(L, i, &len);
        if (!s) {
            luaL_tolstring(L, i, &len);
            s = lua_tostring(L, -1);
            lua_pop(L, 1);
        }
        if (s) {
            strncat(buf, s, sizeof(buf) - strlen(buf) - 2);
            if (i < nret) strncat(buf, "\t", sizeof(buf) - strlen(buf) - 2);
        }
    }
    uiMultilineEntrySetText(ctx->output_entry, buf);
    lua_close(L);
    uiFreeText((char*)script);
}

/* ------------------------------------------------------------------ */
/* Download row management (main thread only)                          */
/* ------------------------------------------------------------------ */

static DownloadRow *find_row(int id) {
    for (int i = 0; i < g_gui.row_count; i++) {
        if (g_gui.rows[i].download_id == id) return &g_gui.rows[i];
    }
    return NULL;
}

static int find_row_index(int id) {
    for (int i = 0; i < g_gui.row_count; i++) {
        if (g_gui.rows[i].download_id == id) return i;
    }
    return -1;
}

/* ------------------------------------------------------------------ */
/* Downloads table model handlers                                      */
/* ------------------------------------------------------------------ */
static int downloads_modelNumColumns(uiTableModelHandler *mh, uiTableModel *m) {
    (void)mh; (void)m; return 7;
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
    switch (column) {
        case 0: return uiNewTableValueInt(r->selected);
        case 1: return uiNewTableValueString(r->filename[0] ? r->filename : "");
        case 2: {
            /* Show downloaded / total if available */
            if (r->total_bytes > 0) {
                double dl = (double)r->downloaded_bytes;
                double tot = (double)r->total_bytes;
                if (tot >= (1LL<<30)) {
                    snprintf(buf, sizeof(buf), "%.2f GB / %.2f GB", dl/1024.0/1024.0/1024.0, tot/1024.0/1024.0/1024.0);
                } else if (tot >= (1LL<<20)) {
                    snprintf(buf, sizeof(buf), "%.2f MB / %.2f MB", dl/1024.0/1024.0, tot/1024.0/1024.0);
                } else {
                    snprintf(buf, sizeof(buf), "%lld B / %lld B", (long long)r->downloaded_bytes, (long long)r->total_bytes);
                }
            } else if (r->downloaded_bytes > 0) {
                /* Only downloaded known */
                if (r->downloaded_bytes >= (1LL<<20))
                    snprintf(buf, sizeof(buf), "%.2f MB", r->downloaded_bytes/1024.0/1024.0);
                else
                    snprintf(buf, sizeof(buf), "%lld B", (long long)r->downloaded_bytes);
            } else {
                snprintf(buf, sizeof(buf), "-");
            }
            return uiNewTableValueString(buf);
        }
        case 3: {
            /* Status string */
            const char *st = "?";
            switch (r->state) {
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
            if (r->start_time) {
                struct tm *t = localtime(&r->start_time);
                snprintf(buf, sizeof(buf), "%02d:%02d:%02d", t->tm_hour, t->tm_min, t->tm_sec);
            } else snprintf(buf, sizeof(buf), "-");
            return uiNewTableValueString(buf);
        }
        case 5: {
            if (r->speed_bps > 0.0) {
                if (r->speed_bps >= 1024.0*1024.0) snprintf(buf, sizeof(buf), "%.2f MB/s", r->speed_bps/1024.0/1024.0);
                else if (r->speed_bps >= 1024.0) snprintf(buf, sizeof(buf), "%.2f KB/s", r->speed_bps/1024.0);
                else snprintf(buf, sizeof(buf), "%.0f B/s", r->speed_bps);
            } else snprintf(buf, sizeof(buf), "-");
            return uiNewTableValueString(buf);
        }
        case 6: return uiNewTableValueInt(r->progress_pct);
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
}

/* Select latest active download for pause/resume/remove actions. */
static int pick_target_download_id(void) {
    if (g_gui.active_download_id > 0) {
        return g_gui.active_download_id;
    }

    /* BEST PRACTICE: Iterate the local GUI state, not the shared backend list */
    for (int i = 0; i < g_gui.row_count; i++) {
        DownloadState state = g_gui.rows[i].state;
        if (state == DOWNLOAD_STATE_RUNNING || state == DOWNLOAD_STATE_QUEUED || state == DOWNLOAD_STATE_PAUSED) {
            return g_gui.rows[i].download_id;
        }
    }
    
    /* Fallback to the first row if nothing active */
    if (g_gui.row_count > 0) return g_gui.rows[0].download_id;
    return -1;
}

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
            hicon = (HICON)LoadImageA(NULL, candidates[i], IMAGE_ICON, 0, 0,
                                      LR_LOADFROMFILE | LR_DEFAULTSIZE);
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
        FILE *f = fopen(out, "rb");
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
    if (g_gui.row_count >= MAX_DOWNLOAD_ROWS) return NULL;

    DownloadRow *r = &g_gui.rows[g_gui.row_count];
    r->download_id = id;
    r->selected = 0;
    strncpy(r->filename, filename ? filename : "", sizeof(r->filename)-1);
    r->filename[sizeof(r->filename)-1] = '\0';
    r->state = DOWNLOAD_STATE_QUEUED;
    r->progress_pct = 0;
    r->speed_bps = 0.0;
    r->total_bytes = 0;
    r->downloaded_bytes = 0;
    r->start_time = 0;
    int idx = g_gui.row_count;
    g_gui.row_count++;
    if (g_downloads_model)
        uiTableModelRowInserted(g_downloads_model, idx);
    /* Adjust column widths to fit content */
    autosize_downloads_table();
    return r;
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
    gui_log(LOG_INFO, msg);
}

static void on_pause_clicked(uiButton *sender, void *data) {
    (void)sender; (void)data;
    int acted = 0;
    for (int i = 0; i < g_gui.row_count; i++) {
        DownloadRow *r = &g_gui.rows[i];
        if (r->selected) {
            if (r->download_id > 0) {
                download_manager_pause(r->download_id);
                acted++;
            }
            r->selected = 0; /* clear selection */
        }
    }
        if (acted > 0) {
        gui_log(LOG_INFO, "Paused checked downloads");
        if (g_downloads_model) for (int j = 0; j < g_gui.row_count; j++) uiTableModelRowChanged(g_downloads_model, j);
    } else {
        gui_log(LOG_ERROR, "Pause failed: no checked downloads");
    }
}

static void on_resume_clicked(uiButton *sender, void *data) {
    (void)sender; (void)data;
    int id = pick_target_download_id();
    if (id <= 0) {
        gui_log(LOG_ERROR, "Resume failed: no paused download");
        return;
    }
    download_manager_resume(id);
    gui_log(LOG_INFO, "Resume requested for selected download");
}

static void on_remove_clicked(uiButton *sender, void *data) {
    (void)sender; (void)data;
    int acted = 0;

    /* BEST PRACTICE: Iterate backwards when deleting from an array */
    for (int i = g_gui.row_count - 1; i >= 0; i--) {
        DownloadRow *r = &g_gui.rows[i];
        if (r->selected) {
            if (r->download_id > 0) {
                // /* 1. Tell backend to pause and safely remove */
                // download_manager_pause(r->download_id); 
                download_manager_remove(r->download_id);
                acted++;
            }
            
            /* 2. Remove from GUI array by shifting remaining elements left */
            for (int j = i; j < g_gui.row_count - 1; j++) {
                g_gui.rows[j] = g_gui.rows[j + 1];
            }
            g_gui.row_count--;

            /* 3. Notify the table model that this exact row index was deleted */
            if (g_downloads_model) {
                uiTableModelRowDeleted(g_downloads_model, i);
            }
        }
    }

    if (acted > 0) {
        gui_log(LOG_INFO, "Removed checked downloads");
        autosize_downloads_table(); /* Re-adjust column sizing */
    } else {
        gui_log(LOG_ERROR, "Remove failed: no checked downloads");
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
    gui_log(LOG_SUCCESS, msg);
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

    char msg[1024];
    snprintf(msg, sizeof(msg), "Default output directory changed to: %s", folder);
    gui_log(LOG_SUCCESS, msg);
    uiFreeText(folder);
}

static int on_child_window_closing(uiWindow *w, void *data) {
    (void)data;
    uiControlDestroy(uiControl(w));
    return 0; /* Returning 0 is important; it tells libui not to do default processing */
}

static void on_http_clicked(uiButton *sender, void *data) {
    (void)sender; (void)data;

    // --- HTTP Test Window ---
    uiWindow *win = uiNewWindow("HTTP Request Tester", 900, 700, 0);
    uiWindowSetMargined(win, 1);

    uiBox *vbox = uiNewVerticalBox();
    uiBoxSetPadded(vbox, 1);

    uiEntry *url_entry = uiNewEntry();
    uiEntrySetText(url_entry, "https://facebook.com");
    uiBoxAppend(vbox, uiControl(url_entry), 0);

    uiMultilineEntry *header_entry = uiNewMultilineEntry();
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
    if (!ctx) { gui_log(LOG_ERROR, "[HTTP DEBUG] ctx alloc failed"); return; }
    ctx->url_entry = url_entry;
    ctx->header_entry = header_entry;
    ctx->header_resp_entry = header_resp_entry;
    ctx->output_entry = output_entry;
    ctx->win = win;

    uiButtonOnClicked(send_btn, http_test_on_send, ctx);
    uiWindowSetChild(win, uiControl(vbox));
    uiControlShow(uiControl(win));
    uiWindowOnClosing(win, (int (*)(uiWindow *, void *))on_child_window_closing, ctx);
}

static void on_lua_clicked(uiButton *sender, void *data) {
    (void)sender; (void)data;
    // --- Lua Test Window ---
    uiWindow *win = uiNewWindow("Lua Script Tester", 520, 420, 0);
    uiWindowSetMargined(win, 1);

    uiBox *vbox = uiNewVerticalBox();
    uiBoxSetPadded(vbox, 1);

    uiMultilineEntry *script_entry = uiNewMultilineEntry();
    // uiMultilineEntrySetPlaceholder(script_entry, ...) is not available in libui-ng; skip placeholder.
    uiBoxAppend(vbox, uiControl(script_entry), 1);

    uiButton *exec_btn = uiNewButton("Execute Lua Script");
    uiBoxAppend(vbox, uiControl(exec_btn), 0);

    uiMultilineEntry *output_entry = uiNewMultilineEntry();
    uiMultilineEntrySetReadOnly(output_entry, 1);
    uiBoxAppend(vbox, uiControl(output_entry), 1);

    LuaTestCtx *ctx = malloc(sizeof(LuaTestCtx));
    if (!ctx) { gui_log(LOG_ERROR, "[LUA DEBUG] ctx alloc failed"); return; }
    ctx->script_entry = script_entry;
    ctx->output_entry = output_entry;
    ctx->win = win;

    uiButtonOnClicked(exec_btn, lua_test_on_exec, ctx);

    uiWindowSetChild(win, uiControl(vbox));
    uiControlShow(uiControl(win));
    uiWindowOnClosing(win, (int (*)(uiWindow *, void *))on_child_window_closing, ctx);
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

    DownloadRow *r = find_row(update->status.id);
    if (!r) {
        r = add_row(update->status.id, update->status.filename);
        if (!r) return;
    }

    /* Update stored data fields */
    if (update->status.filename[0]) strncpy(r->filename, update->status.filename, sizeof(r->filename)-1);
    r->filename[sizeof(r->filename)-1] = '\0';
    DownloadState prev_state = r->state;
    r->state = update->status.state;
    int pct = (int)update->status.progress;
    if (pct < 0) pct = 0; if (pct > 100) pct = 100;
    r->progress_pct = pct;
    r->speed_bps = update->status.speed_bps;
    r->total_bytes = update->status.total_bytes;
    r->downloaded_bytes = update->status.downloaded_bytes;
    /* Ensure start_time is set when download transitions to RUNNING */
    if (r->start_time == 0) {
        if (r->state == DOWNLOAD_STATE_RUNNING) {
            r->start_time = update->status.start_time ? update->status.start_time : time(NULL);
        }
    } else {
        /* keep existing start_time */
    }

    int idx = find_row_index(update->status.id);
    if (idx >= 0 && g_downloads_model) uiTableModelRowChanged(g_downloads_model, idx);
    /* Recompute column widths when content changes */
    autosize_downloads_table();
}

/* ------------------------------------------------------------------ */
/* Window close callback                                                */
/* ------------------------------------------------------------------ */

static int on_window_close(uiWindow *w, void *data) {
    (void)w; (void)data;
#ifdef _WIN32
    toolbar_icons_shutdown();
    if (g_gui.app_icon && g_gui.app_icon_from_file) {
        DestroyIcon(g_gui.app_icon);
        g_gui.app_icon = NULL;
        g_gui.app_icon_from_file = 0;
    }
#endif
    uiQuit();
    return 1; /* destroy window */
}

static int on_should_quit(void *data) {
    (void)data;
    uiControlDestroy(uiControl(g_gui.window));
    return 1;
}

/* ========================================================================= */
/* Menu Wrappers & Callbacks                                                 */
/* ========================================================================= */

static void menu_pause_cb(uiMenuItem *sender, uiWindow *w, void *data)   { on_pause_clicked(NULL, NULL); }
static void menu_resume_cb(uiMenuItem *sender, uiWindow *w, void *data)  { on_resume_clicked(NULL, NULL); }
static void menu_remove_cb(uiMenuItem *sender, uiWindow *w, void *data)  { on_remove_clicked(NULL, NULL); }
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
    item = uiMenuAppendItem(menu, "Pause\tCtrl+P");
    uiMenuItemOnClicked(item, menu_pause_cb, NULL);
    item = uiMenuAppendItem(menu, "Resume\tCtrl+R");
    uiMenuItemOnClicked(item, menu_resume_cb, NULL);
    item = uiMenuAppendItem(menu, "Remove\tDel");
    uiMenuItemOnClicked(item, menu_remove_cb, NULL);

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

/* ------------------------------------------------------------------ */
/* gui_create                                                           */
/* ------------------------------------------------------------------ */

void gui_create(void) {
    /* IMPORTANT: Setup menus BEFORE creating the main window, as some platforms (e.g. macOS) require the menu to exist first for proper integration. */
    setup_menus();
    
    memset(&g_gui, 0, sizeof(g_gui));

    /* Outer window */
    g_gui.window = uiNewWindow("LUDO - LUa DOwnloader", 800, 600, 1);
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

    /* Append columns in new order: id, name, size, status, started, speed, progress */
    uiTableAppendCheckboxColumn(t, "#", 0, uiTableModelColumnAlwaysEditable);
    uiTableAppendTextColumn(t, "Name", 1, uiTableModelColumnNeverEditable, NULL);
    uiTableAppendTextColumn(t, "Size", 2, uiTableModelColumnNeverEditable, NULL);
    uiTableAppendTextColumn(t, "Status", 3, uiTableModelColumnNeverEditable, NULL);
    uiTableAppendTextColumn(t, "Started", 4, uiTableModelColumnNeverEditable, NULL);
    uiTableAppendTextColumn(t, "Speed", 5, uiTableModelColumnNeverEditable, NULL);
    uiTableAppendProgressBarColumn(t, "Progress", 6);

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
    uiControlShow(uiControl(g_gui.window));
    /* Apply percentage column widths initially and on resize */
    uiWindowOnPositionChanged(g_gui.window, on_main_window_pos_changed, NULL);
    autosize_downloads_table();
    /* Apply clipboard check now (on creation) and also when window is focused */
    paste_clipboard_url_if_valid();
    uiWindowOnFocusChanged(g_gui.window, on_main_window_focus_changed, NULL);

    /* Register progress callback with the download manager */
    download_manager_set_progress_cb(gui_on_progress, NULL);

    /* Start a worker thread that feeds URLs from the queue to the Lua engine */
    ludo_thread_t worker;
    ludo_thread_create(&worker, url_worker_thread, NULL);
    /* We intentionally don't join this thread — it exits when the queue
       is shut down during download_manager_shutdown(). */
    
    {
        char msg[256];
        snprintf(msg, sizeof(msg), "LUDO v%s - %s (%s)", LUDO_VERSION_STR, LUDO_DESCRIPTION, LUDO_COMPANY);
        gui_log(LOG_INFO, msg);
        lua_engine_info();
    }
    
    gui_log(LOG_INFO, "Ready. Paste a URL and click [Add Download].");
}
