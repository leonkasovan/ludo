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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

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
    uiBox          *row_box;       /* horizontal container for this row */
    uiLabel        *label_name;    /* filename or URL snippet           */
    uiLabel        *label_status;  /* status text                       */
    uiProgressBar  *progress_bar;
} DownloadRow;

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

/* Select latest active download for pause/resume/remove actions. */
static int pick_target_download_id(void) {
    if (g_gui.active_download_id > 0) {
        return g_gui.active_download_id;
    }

    Download *list = download_manager_get_list();
    if (!list) return -1;

    for (Download *d = list; d; d = d->next) {
        if (d->status.state == DOWNLOAD_STATE_RUNNING ||
            d->status.state == DOWNLOAD_STATE_QUEUED ||
            d->status.state == DOWNLOAD_STATE_PAUSED) {
            return d->status.id;
        }
    }
    return list->status.id;
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

    DownloadRow *r = &g_gui.rows[g_gui.row_count++];
    r->download_id = id;

    /* Row: [filename label (stretchy)] [status label] [progress bar] */
    r->row_box = uiNewHorizontalBox();
    uiBoxSetPadded(r->row_box, 1);

    r->label_name = uiNewLabel(filename[0] ? filename : "...");
    uiBoxAppend(r->row_box, uiControl(r->label_name), 1 /* stretchy */);

    r->label_status = uiNewLabel("Queued");
    uiBoxAppend(r->row_box, uiControl(r->label_status), 0);

    r->progress_bar = uiNewProgressBar();
    uiProgressBarSetValue(r->progress_bar, 0);
    uiBoxAppend(r->row_box, uiControl(r->progress_bar), 0);

    uiBoxAppend(g_gui.downloads_box, uiControl(r->row_box), 0);
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
    int id = pick_target_download_id();
    if (id <= 0) {
        gui_log(LOG_ERROR, "Pause failed: no active download");
        return;
    }
    download_manager_pause(id);
    gui_log(LOG_INFO, "Paused selected download");
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
    int id = pick_target_download_id();
    if (id <= 0) {
        gui_log(LOG_ERROR, "Remove failed: no download selected");
        return;
    }
    download_manager_remove(id);
    gui_log(LOG_INFO, "Removed selected download from list");
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
    uiWindowOnClosing(win, (int (*)(uiWindow *, void *))free, ctx);
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
    uiWindowOnClosing(win, (int (*)(uiWindow *, void *))free, ctx);
}

static void on_about_clicked(uiButton *sender, void *data) {
    (void)sender; (void)data;
    uiMsgBox(g_gui.window,
             "About LUDO",
             "LUDO - LUa DOwnloader\n\n"
             "A lightweight download manager with plugin support,\n"
             "resume, progress tracking, and session persistence.");
}

/* ------------------------------------------------------------------ */
/* Progress callback (invoked on main thread via uiQueueMain)          */
/* ------------------------------------------------------------------ */

void gui_on_progress(const ProgressUpdate *update, void *user_data) {
    (void)user_data;

    g_gui.active_download_id = update->status.id;

    DownloadRow *r = find_row(update->status.id);
    if (!r) {
        /* New entry — create a row for it */
        r = add_row(update->status.id, update->status.filename);
        if (!r) return;
    }

    /* Update filename label if we now know it */
    if (update->status.filename[0]) {
        uiLabelSetText(r->label_name, update->status.filename);
    }

    /* Progress bar (0–100) */
    int pct = (int)update->status.progress;
    if (pct < 0)   pct = 0;
    if (pct > 100) pct = 100;
    uiProgressBarSetValue(r->progress_bar, pct);

    /* Status label */
    switch (update->status.state) {
        case DOWNLOAD_STATE_QUEUED:
            uiLabelSetText(r->label_status, "Queued");
            break;
        case DOWNLOAD_STATE_RUNNING: {
            char buf[64];
            snprintf(buf, sizeof(buf), "%d%%", pct);
            uiLabelSetText(r->label_status, buf);
            break;
        }
        case DOWNLOAD_STATE_PAUSED:
            uiLabelSetText(r->label_status, "Paused");
            break;
        case DOWNLOAD_STATE_COMPLETED:
            uiLabelSetText(r->label_status, "Done");
            uiProgressBarSetValue(r->progress_bar, 100);
            break;
        case DOWNLOAD_STATE_FAILED: {
            char buf[288];
            snprintf(buf, sizeof(buf), "Failed: %s", update->error_msg);
            uiLabelSetText(r->label_status, buf);
            uiProgressBarSetValue(r->progress_bar, 0); /* indeterminate */
            break;
        }
    }
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

/* ------------------------------------------------------------------ */
/* gui_create                                                           */
/* ------------------------------------------------------------------ */

void gui_create(void) {
    memset(&g_gui, 0, sizeof(g_gui));

    /* Outer window */
    g_gui.window = uiNewWindow("LUDO - LUa DOwnloader", 800, 600, 0);
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
    uiBox *toolbar = uiNewHorizontalBox();
    uiBoxSetPadded(toolbar, 0);

    g_gui.tb_add = uiNewButton("Add");
    uiButtonOnClicked(g_gui.tb_add, on_add_clicked, NULL);
    uiBoxAppend(toolbar, uiControl(g_gui.tb_add), 0);

    g_gui.tb_pause = uiNewButton("Pause");
    uiButtonOnClicked(g_gui.tb_pause, on_pause_clicked, NULL);
    uiBoxAppend(toolbar, uiControl(g_gui.tb_pause), 0);

    g_gui.tb_resume = uiNewButton("Resume");
    uiButtonOnClicked(g_gui.tb_resume, on_resume_clicked, NULL);
    uiBoxAppend(toolbar, uiControl(g_gui.tb_resume), 0);

    g_gui.tb_remove = uiNewButton("Remove");
    uiButtonOnClicked(g_gui.tb_remove, on_remove_clicked, NULL);
    uiBoxAppend(toolbar, uiControl(g_gui.tb_remove), 0);

    uiBoxAppend(toolbar, uiControl(uiNewVerticalSeparator()), 0);

    g_gui.tb_plugin = uiNewButton("Plugin");
    uiButtonOnClicked(g_gui.tb_plugin, on_plugin_clicked, NULL);
    uiBoxAppend(toolbar, uiControl(g_gui.tb_plugin), 0);

    g_gui.tb_setting = uiNewButton("Setting");
    uiButtonOnClicked(g_gui.tb_setting, on_setting_clicked, NULL);
    uiBoxAppend(toolbar, uiControl(g_gui.tb_setting), 0);

    g_gui.tb_http = uiNewButton("HTTP");
    uiButtonOnClicked(g_gui.tb_http, on_http_clicked, NULL);
    uiBoxAppend(toolbar, uiControl(g_gui.tb_http), 0);

    g_gui.tb_lua = uiNewButton("Lua");
    uiButtonOnClicked(g_gui.tb_lua, on_lua_clicked, NULL);
    uiBoxAppend(toolbar, uiControl(g_gui.tb_lua), 0);

    g_gui.tb_about = uiNewButton("About");
    uiButtonOnClicked(g_gui.tb_about, on_about_clicked, NULL);
    uiBoxAppend(toolbar, uiControl(g_gui.tb_about), 0);

    uiBoxAppend(root, uiControl(toolbar), 0);

#ifdef _WIN32
    toolbar_icons_init();
#endif

    uiBoxAppend(root, uiControl(uiNewHorizontalSeparator()), 0);

    /* ---- URL input row ---- */
    uiBox *input_row = uiNewHorizontalBox();
    uiBoxSetPadded(input_row, 1);

    g_gui.url_entry = uiNewEntry();
    uiEntrySetText(g_gui.url_entry, "https://");
    uiBoxAppend(input_row, uiControl(g_gui.url_entry), 1 /* stretchy */);

    uiBoxAppend(root, uiControl(input_row), 0);

    /* ---- Downloads list ---- */
    g_gui.downloads_box = uiNewVerticalBox();
    uiBoxSetPadded(g_gui.downloads_box, 1);

    uiGroup *downloads_group = uiNewGroup("Downloads");
    uiGroupSetMargined(downloads_group, 1);
    uiGroupSetChild(downloads_group, uiControl(g_gui.downloads_box));

    /* Wrap in a scrollable area via a non-wrapping multiline entry is not
       ideal, but libui-ng lacks a ScrollView for arbitrary controls.
       Instead we place at most MAX_DOWNLOAD_ROWS rows and rely on the
       window being resizable. */
    uiBoxAppend(root, uiControl(downloads_group), 0);

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

    /* Register progress callback with the download manager */
    download_manager_set_progress_cb(gui_on_progress, NULL);

    /* Start a worker thread that feeds URLs from the queue to the Lua engine */
    ludo_thread_t worker;
    ludo_thread_create(&worker, url_worker_thread, NULL);
    /* We intentionally don't join this thread — it exits when the queue
       is shut down during download_manager_shutdown(). */

    gui_log(LOG_INFO, "Ready. Paste a URL and click Add.");
}
