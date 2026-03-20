#include "lua_engine.h"
#include "http_module.h"
#include "ludo_module.h"
#include "libuilua.h"
#include "gui.h"
#include "thread_queue.h"

#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <dirent.h>
#endif

/* ------------------------------------------------------------------ */
/* Plugin registry                                                      */
/* ------------------------------------------------------------------ */

#define MAX_PLUGINS 64

typedef struct {
    char path[512];  /* path to the .lua file */
} PluginEntry;

static struct {
    PluginEntry plugins[MAX_PLUGINS];
    int         count;
    ludo_mutex_t mutex;  /* protects the plugin list only */
} g_engine;

/* ------------------------------------------------------------------ */
/* Lua state factory                                                    */
/* ------------------------------------------------------------------ */

/*
 * Create a fresh lua_State with standard libs plus our custom http/ludo
 * modules registered.  Each worker thread owns an independent state.
 */
static lua_State *create_lua_state(void) {
    lua_State *L = luaL_newstate();
    if (!L) return NULL;

    luaL_openlibs(L);
    http_module_register(L);
    ludo_module_register(L);

    /* Register libui Lua bindings as "ui" */
    luaL_requiref(L, "ui", luaopen_libuilua, 1);
    lua_pop(L, 1);

    return L;
}

/* ------------------------------------------------------------------ */
/* Plugin loading helpers                                               */
/* ------------------------------------------------------------------ */

/*
 * Attempt to load and execute the plugin at `path` in state `L`.
 * Returns 1 if the file defines a table with .validate and .process
 * functions (the plugin "contract"), 0 otherwise.
 */
static int plugin_check_contract(lua_State *L, const char *path) {
    if (luaL_dofile(L, path) != LUA_OK) {
        const char *err = lua_tostring(L, -1);
        char msg[1024];
        snprintf(msg, sizeof(msg), "[lua_engine] failed to load %s: %s",
                 path, err ? err : "(unknown error)");
        gui_log(LOG_ERROR, msg);
        lua_pop(L, 1);
        return 0;
    }
    /* The file must leave a table on top of the stack with the plugin */
    if (!lua_istable(L, -1)) {
        lua_pop(L, 1);
        return 0;
    }
    lua_getfield(L, -1, "validate");
    int has_validate = lua_isfunction(L, -1);
    lua_pop(L, 1);
    lua_getfield(L, -1, "process");
    int has_process = lua_isfunction(L, -1);
    lua_pop(L, 1);
    lua_pop(L, 1); /* pop plugin table */

    return has_validate && has_process;
}

/* ------------------------------------------------------------------ */
/* Public API                                                           */
/* ------------------------------------------------------------------ */

void lua_engine_init(void) {
    memset(&g_engine, 0, sizeof(g_engine));
    ludo_mutex_init(&g_engine.mutex);
}

void lua_engine_load_plugins(const char *plugin_dir) {
#ifdef _WIN32
    char pattern[512];
    snprintf(pattern, sizeof(pattern), "%s\\*.lua", plugin_dir);
    WIN32_FIND_DATAA fd;
    HANDLE hFind = FindFirstFileA(pattern, &fd);
    if (hFind == INVALID_HANDLE_VALUE) return;
    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        ludo_mutex_lock(&g_engine.mutex);
        if (g_engine.count < MAX_PLUGINS) {
            snprintf(g_engine.plugins[g_engine.count].path,
                     sizeof(g_engine.plugins[0].path),
                     "%s\\%s", plugin_dir, fd.cFileName);
            g_engine.count++;
        }
        ludo_mutex_unlock(&g_engine.mutex);
    } while (FindNextFileA(hFind, &fd));
    FindClose(hFind);
#else
    DIR *dir = opendir(plugin_dir);
    if (!dir) return;
    struct dirent *ent;
    while ((ent = readdir(dir))) {
        size_t nlen = strlen(ent->d_name);
        if (nlen < 5) continue;
        if (strcmp(ent->d_name + nlen - 4, ".lua") != 0) continue;
        ludo_mutex_lock(&g_engine.mutex);
        if (g_engine.count < MAX_PLUGINS) {
            snprintf(g_engine.plugins[g_engine.count].path,
                     sizeof(g_engine.plugins[0].path),
                     "%s/%s", plugin_dir, ent->d_name);
            g_engine.count++;
        }
        ludo_mutex_unlock(&g_engine.mutex);
    }
    closedir(dir);
#endif

    char msg[256];
    snprintf(msg, sizeof(msg), "[lua_engine] loaded %d plugin(s) from %s",
             g_engine.count, plugin_dir);
    gui_log(LOG_INFO, msg);
}

int lua_engine_process_url(const char *url) {
    /* Each call gets a fresh Lua state (thread-safe, no shared state). */
    lua_State *L = create_lua_state();
    if (!L) {
        gui_log(LOG_ERROR, "[lua_engine] failed to create Lua state");
        return 0;
    }

    int handled = 0;

    ludo_mutex_lock(&g_engine.mutex);
    int count = g_engine.count;
    PluginEntry plugins[MAX_PLUGINS];
    memcpy(plugins, g_engine.plugins, (size_t)count * sizeof(PluginEntry));
    ludo_mutex_unlock(&g_engine.mutex);

    for (int i = 0; i < count; i++) {
        /* Load the plugin file; it should return a table */
        if (luaL_loadfile(L, plugins[i].path) != LUA_OK) {
            const char *e = lua_tostring(L, -1);
            char msg[512];
            snprintf(msg, sizeof(msg), "[lua_engine] load error %s: %s",
                     plugins[i].path, e ? e : "?");
            gui_log(LOG_ERROR, msg);
            lua_pop(L, 1);
            continue;
        }
        if (lua_pcall(L, 0, 1, 0) != LUA_OK) {
            const char *e = lua_tostring(L, -1);
            char msg[512];
            snprintf(msg, sizeof(msg), "[lua_engine] exec error %s: %s",
                     plugins[i].path, e ? e : "?");
            gui_log(LOG_ERROR, msg);
            lua_pop(L, 1);
            continue;
        }
        if (!lua_istable(L, -1)) {
            lua_pop(L, 1);
            continue;
        }

        /* Call plugin.validate(url) */
        lua_getfield(L, -1, "validate");
        if (!lua_isfunction(L, -1)) {
            lua_pop(L, 2); /* pop validate + plugin table */
            continue;
        }
        lua_pushstring(L, url);
        if (lua_pcall(L, 1, 1, 0) != LUA_OK) {
            const char *e = lua_tostring(L, -1);
            char msg[512];
            snprintf(msg, sizeof(msg),
                     "[lua_engine] validate error in %s: %s",
                     plugins[i].path, e ? e : "?");
            gui_log(LOG_ERROR, msg);
            lua_pop(L, 2); /* pop error + plugin table */
            continue;
        }
        int matches = lua_toboolean(L, -1);
        lua_pop(L, 1); /* pop result */

        if (!matches) {
            lua_pop(L, 1); /* pop plugin table */
            continue;
        }

        /* Plugin matched — call plugin.process(url) */
        lua_getfield(L, -1, "process");
        if (!lua_isfunction(L, -1)) {
            lua_pop(L, 2);
            continue;
        }
        lua_pushstring(L, url);
        if (lua_pcall(L, 1, 1, 0) != LUA_OK) {
            const char *e = lua_tostring(L, -1);
            char msg[512];
            snprintf(msg, sizeof(msg),
                     "[lua_engine] process error in %s: %s",
                     plugins[i].path, e ? e : "?");
            gui_log(LOG_ERROR, msg);
            lua_pop(L, 2);
            continue;
        }
        lua_pop(L, 1); /* pop process() return value */
        lua_pop(L, 1); /* pop plugin table */

        /* Count this as handled even if process returned nil; the script
           called ludo.newDownload() internally as a side effect. */
        handled = 1;
        break; /* first matching plugin wins */
    }

    lua_close(L);
    return handled;
}

void lua_engine_shutdown(void) {
    ludo_mutex_destroy(&g_engine.mutex);
}
