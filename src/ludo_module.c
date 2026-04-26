#include "ludo_module.h"
#include "config.h"
#include "download_manager.h"
#include "gui.h"
#include "ui.h"

#include <lauxlib.h>
#include <lualib.h>
#include <string.h>

static char ludo_tester_bindings_key;
static char ludo_current_source_url_key;

/* ------------------------------------------------------------------ */
/* ludo.newDownload(url, output_dir, mode [, filename])               */
/*   -> id, status, output_path                                        */
/* ------------------------------------------------------------------ */

static int lua_ludo_new_download(lua_State *L) {
    const char *url          = luaL_checkstring(L, 1);
    const char *output_dir   = luaL_optstring(L, 2, NULL);
    const char *original_url;
    int         mode         = (int)luaL_optinteger(L, 3, DOWNLOAD_NOW);
    const char *hint_filename = luaL_optstring(L, 4, NULL);
    DownloadAddResult result;

    /* Collect optional 5th argument: headers table {"Name": "Value", ...} */
    char headers_buf[4096] = {0};
    const char *extra_headers = NULL;
    if (lua_istable(L, 5)) {
        int pos = 0;
        lua_pushnil(L);
        while (lua_next(L, 5) != 0) {
            const char *key = lua_tostring(L, -2);
            const char *val = lua_tostring(L, -1);
            if (key && val && pos < (int)sizeof(headers_buf) - 128)
                pos += snprintf(headers_buf + pos, sizeof(headers_buf) - pos,
                                "%s: %s\n", key, val);
            lua_pop(L, 1);
        }
        if (pos > 0) extra_headers = headers_buf;
    }

    /* Use default output dir if none provided */
    if (!output_dir || output_dir[0] == '\0')
        output_dir = download_manager_get_output_dir();

    lua_pushlightuserdata(L, (void *)&ludo_current_source_url_key);
    lua_gettable(L, LUA_REGISTRYINDEX);
    original_url = lua_tostring(L, -1);
    memset(&result, 0, sizeof(result));
    int id = download_manager_add(url, output_dir, (DownloadMode)mode, original_url,
                                  hint_filename, extra_headers, NULL, &result);
    lua_pop(L, 1);
    if (id < 0) result.id = id;
    lua_pushinteger(L, (lua_Integer)result.id);
    lua_pushinteger(L, (lua_Integer)result.status_code);
    lua_pushstring(L, result.output_path);
    return 3;
}

/* ludo.pauseDownload(id) */
static int lua_ludo_pause_download(lua_State *L) {
    int id = (int)luaL_checkinteger(L, 1);
    download_manager_pause(id);
    return 0;
}

/* ludo.removeDownload(id) */
static int lua_ludo_remove_download(lua_State *L) {
    int id = (int)luaL_checkinteger(L, 1);
    download_manager_remove(id);
    return 0;
}

/* ------------------------------------------------------------------ */
/* Logging helpers                                                      */
/* ------------------------------------------------------------------ */

static LudoTesterBindings *get_tester_bindings(lua_State *L) {
    LudoTesterBindings *bindings;

    lua_pushlightuserdata(L, (void *)&ludo_tester_bindings_key);
    lua_gettable(L, LUA_REGISTRYINDEX);
    bindings = (LudoTesterBindings *)lua_touserdata(L, -1);
    lua_pop(L, 1);
    return bindings;
}

static uiMultilineEntry *require_tester_entry(lua_State *L,
                                              uiMultilineEntry *entry,
                                              const char *name) {
    if (!entry) {
        luaL_error(L, "%s is only available while the matching tester window is open", name);
        return NULL;
    }
    return entry;
}

static int lua_ludo_http_tester_response_content(lua_State *L) {
    LudoTesterBindings *bindings = get_tester_bindings(L);
    uiMultilineEntry *entry = require_tester_entry(
        L,
        bindings ? bindings->http_response_content : NULL,
        "ludo.http_tester.response.content()"
    );
    char *text = uiMultilineEntryText(entry);

    lua_pushstring(L, text ? text : "");
    if (text) uiFreeText(text);
    return 1;
}

static int lua_ludo_http_tester_response_header(lua_State *L) {
    LudoTesterBindings *bindings = get_tester_bindings(L);
    uiMultilineEntry *entry = require_tester_entry(
        L,
        bindings ? bindings->http_response_header : NULL,
        "ludo.http_tester.response.header()"
    );
    char *text = uiMultilineEntryText(entry);

    lua_pushstring(L, text ? text : "");
    if (text) uiFreeText(text);
    return 1;
}

static void append_multiline_output(uiMultilineEntry *entry, const char *text, int newline) {
    if (!entry || !text) return;
    uiMultilineEntryAppend(entry, text);
    if (newline) uiMultilineEntryAppend(entry, "\n");
}

static int lua_ludo_lua_tester_println(lua_State *L) {
    LudoTesterBindings *bindings = get_tester_bindings(L);
    uiMultilineEntry *entry = require_tester_entry(
        L,
        bindings ? bindings->lua_output : NULL,
        "ludo.lua_tester.println()"
    );
    size_t len = 0;
    const char *msg;

    if (lua_gettop(L) == 0) {
        append_multiline_output(entry, "", 1);
        return 0;
    }

    msg = luaL_tolstring(L, 1, &len);
    (void)len;
    append_multiline_output(entry, msg ? msg : "", 1);
    lua_pop(L, 1);
    return 0;
}

static int lua_ludo_lua_tester_printf(lua_State *L) {
    LudoTesterBindings *bindings = get_tester_bindings(L);
    uiMultilineEntry *entry = require_tester_entry(
        L,
        bindings ? bindings->lua_output : NULL,
        "ludo.lua_tester.printf()"
    );
    int nargs = lua_gettop(L);
    const char *msg;

    lua_getglobal(L, "string");
    lua_getfield(L, -1, "format");
    lua_remove(L, -2);
    lua_insert(L, 1);
    if (lua_pcall(L, nargs, 1, 0) != LUA_OK) {
        return lua_error(L);
    }

    msg = lua_tostring(L, -1);
    append_multiline_output(entry, msg ? msg : "", 0);
    lua_pop(L, 1);
    return 0;
}

static int lua_ludo_log_error(lua_State *L) {
    const char *msg = luaL_checkstring(L, 1);
    gui_log(LOG_ERROR, "%s", msg);
    return 0;
}

static int lua_ludo_log_success(lua_State *L) {
    const char *msg = luaL_checkstring(L, 1);
    gui_log(LOG_SUCCESS, "%s", msg);
    return 0;
}

static int lua_ludo_log_info(lua_State *L) {
    const char *msg = luaL_checkstring(L, 1);
    gui_log(LOG_INFO, "%s", msg);
    return 0;
}

/* ludo.getOutputDirectory() -> string */
static int lua_ludo_get_output_dir(lua_State *L) {
    lua_pushstring(L, download_manager_get_output_dir());
    return 1;
}

/* ------------------------------------------------------------------ */
/* Module registration                                                  */
/* ------------------------------------------------------------------ */

static const luaL_Reg ludo_funcs[] = {
    { "newDownload",       lua_ludo_new_download    },
    { "pauseDownload",     lua_ludo_pause_download  },
    { "removeDownload",    lua_ludo_remove_download },
    { "logError",          lua_ludo_log_error       },
    { "logSuccess",        lua_ludo_log_success     },
    { "logInfo",           lua_ludo_log_info        },
    { "getOutputDirectory",lua_ludo_get_output_dir  },
    { NULL,                NULL                     }
};

void ludo_module_register(lua_State *L) {
    const LudoConfig *cfg = ludo_config_get();
    luaL_newlib(L, ludo_funcs);

    lua_newtable(L);
    lua_newtable(L);
    lua_pushcfunction(L, lua_ludo_http_tester_response_content);
    lua_setfield(L, -2, "content");
    lua_pushcfunction(L, lua_ludo_http_tester_response_header);
    lua_setfield(L, -2, "header");
    lua_setfield(L, -2, "response");
    lua_setfield(L, -2, "http_tester");

    lua_newtable(L);
    lua_pushcfunction(L, lua_ludo_lua_tester_println);
    lua_setfield(L, -2, "println");
    lua_pushcfunction(L, lua_ludo_lua_tester_printf);
    lua_setfield(L, -2, "printf");
    lua_setfield(L, -2, "lua_tester");

    /* Constants */
    lua_pushinteger(L, DOWNLOAD_NOW);
    lua_setfield(L, -2, "DOWNLOAD_NOW");

    lua_pushinteger(L, DOWNLOAD_QUEUE);
    lua_setfield(L, -2, "DOWNLOAD_QUEUE");

    lua_newtable(L);
    lua_pushinteger(L, cfg ? cfg->max_download_retry : 0);
    lua_setfield(L, -2, "maxDownloadRetry");
    lua_pushinteger(L, cfg ? cfg->max_thread : 0);
    lua_setfield(L, -2, "maxThread");
    lua_pushinteger(L, cfg ? cfg->url_queue_capacity : 0);
    lua_setfield(L, -2, "urlQueueCapacity");
    lua_pushinteger(L, cfg ? cfg->download_queue_capacity : 0);
    lua_setfield(L, -2, "downloadQueueCapacity");
    lua_pushinteger(L, cfg ? cfg->max_redirect : 0);
    lua_setfield(L, -2, "maxRedirect");
    lua_pushstring(L, cfg ? cfg->output_dir : "");
    lua_setfield(L, -2, "outputDir");
    lua_pushstring(L, cfg ? cfg->plugin_dir : "");
    lua_setfield(L, -2, "pluginDir");
    lua_setfield(L, -2, "setting");

    lua_setglobal(L, "ludo");
}

void ludo_module_set_tester_bindings(lua_State *L, const LudoTesterBindings *bindings) {
    LudoTesterBindings *stored;

    if (!bindings) return;
    stored = (LudoTesterBindings *)lua_newuserdata(L, sizeof(*stored));
    *stored = *bindings;
    lua_pushlightuserdata(L, (void *)&ludo_tester_bindings_key);
    lua_pushvalue(L, -2);
    lua_settable(L, LUA_REGISTRYINDEX);
    lua_pop(L, 1);
}

void ludo_module_set_current_source_url(lua_State *L, const char *url) {
    lua_pushlightuserdata(L, (void *)&ludo_current_source_url_key);
    if (url && url[0] != '\0')
        lua_pushstring(L, url);
    else
        lua_pushnil(L);
    lua_settable(L, LUA_REGISTRYINDEX);
}
