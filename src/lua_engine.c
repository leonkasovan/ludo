#include "lua_engine.h"
#include "http_module.h"
#include "ludo_module.h"
#include "zip_module.h"
#include "aes128.h"
#ifdef BUILD_CONSOLE
#include "console_log.h"
#else
#include "libuilua.h"
#include "gui.h"
#endif
#include "thread_queue.h"

#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <wchar.h>

#include "platform_utils.h"

#ifdef _WIN32
#include <windows.h>

static FILE *fopen_utf8(const char *path, const char *mode) {
    FILE *fp = NULL;
    wchar_t *wpath = utf8_to_wide_dup(path);
    wchar_t wmode[4] = {0};
    size_t i;

    if (!wpath) return NULL;
    for (i = 0; mode[i] != '\0' && i + 1 < sizeof(wmode) / sizeof(wmode[0]); i++) {
        wmode[i] = (wchar_t)(unsigned char)mode[i];
    }
    fp = _wfopen(wpath, wmode);
    free(wpath);
    return fp;
}
#else
#include <dirent.h>

static FILE *fopen_utf8(const char *path, const char *mode) {
    return fopen(path, mode);
}
#endif

static int lua_loadfile_utf8(lua_State *L, const char *path) {
    FILE *f = fopen_utf8(path, "rb");
    char *chunk = NULL;
    char *chunkname = NULL;
    long file_size;
    int status = LUA_ERRFILE;

    if (!f) {
        lua_pushfstring(L, "failed to open %s", path);
        return status;
    }
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        lua_pushfstring(L, "failed to seek %s", path);
        return status;
    }
    file_size = ftell(f);
    if (file_size < 0) {
        fclose(f);
        lua_pushfstring(L, "failed to size %s", path);
        return status;
    }
    if (fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        lua_pushfstring(L, "failed to rewind %s", path);
        return status;
    }

    chunk = (char *)malloc((size_t)file_size + 1);
    if (!chunk) {
        fclose(f);
        lua_pushliteral(L, "out of memory");
        return LUA_ERRMEM;
    }
    if (file_size > 0 && fread(chunk, 1, (size_t)file_size, f) != (size_t)file_size) {
        free(chunk);
        fclose(f);
        lua_pushfstring(L, "failed to read %s", path);
        return status;
    }
    chunk[file_size] = '\0';
    fclose(f);

    chunkname = (char *)malloc(strlen(path) + 2);
    if (!chunkname) {
        free(chunk);
        lua_pushliteral(L, "out of memory");
        return LUA_ERRMEM;
    }
    chunkname[0] = '@';
    strcpy(chunkname + 1, path);

    status = luaL_loadbufferx(L, chunk, (size_t)file_size, chunkname, NULL);
    free(chunkname);
    free(chunk);
    return status;
}

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
    int         initialized;
} g_engine;

/* ------------------------------------------------------------------ */
/* Lua state factory                                                    */
/* ------------------------------------------------------------------ */

lua_State *lua_engine_create_state(void) {
    lua_State *L = luaL_newstate();
    if (!L) return NULL;

    luaL_openlibs(L);

    /* Prepend lib/ directory to package.path so require("ftcsv") etc. work
     * from both tool scripts and plugins. Relative to the process CWD (the
     * directory that contains the executable). */
    lua_getglobal(L, "package");
    lua_getfield(L, -1, "path");
    {
        const char *existing = lua_tostring(L, -1);
        char new_path[2048];
        snprintf(new_path, sizeof(new_path), "lualib/?.lua;%s",
                 existing ? existing : "");
        lua_pop(L, 1);
        lua_pushstring(L, new_path);
        lua_setfield(L, -2, "path");
    }
    lua_pop(L, 1);

    http_module_register(L);
    ludo_module_register(L);
    zip_module_register(L);
    aes128_module_register(L);
    ludo_module_set_current_source_url(L, NULL);

    /* Register libui Lua bindings as "ui" */
#ifndef BUILD_CONSOLE
    luaL_requiref(L, "ui", luaopen_libuilua, 1);
    lua_pop(L, 1);
#endif

    return L;
}

void lua_engine_close_state(lua_State *L) {
    lua_close(L);
}

/*
 * Backward-compatible wrapper — creates a state internally.
 * Prefer lua_engine_process_url_l with a reusable state for batch workloads.
 */
static lua_State *create_lua_state(void) {
    return lua_engine_create_state();
}

/* ------------------------------------------------------------------ */
/* Plugin loading helpers                                               */
/* ------------------------------------------------------------------ */

/*
 * Load a .lua file, pcall it, and verify it returns a table.
 * On success leaves the table on top of the stack and returns 1.
 * On failure pops any partial result and returns 0.
 */
static int load_plugin_file(lua_State *L, const char *path) {
    if (lua_loadfile_utf8(L, path) != LUA_OK) return 0;
    if (lua_pcall(L, 0, 1, 0) != LUA_OK) {
        lua_pop(L, 1);
        return 0;
    }
    if (!lua_istable(L, -1)) {
        lua_pop(L, 1);
        return 0;
    }
    return 1;
}

/*
 * Attempt to load and execute the plugin at `path` in state `L`.
 * Returns 1 if the file defines a table with .validate and .process
 * functions (the plugin "contract"), 0 otherwise.
 */
static int plugin_check_contract(lua_State *L, const char *path) {
    if (!load_plugin_file(L, path)) {
        const char *err = lua_tostring(L, -1);
        char msg[1024];
        snprintf(msg, sizeof(msg), "[lua_engine] failed to load %.380s: %.580s",
             path, err ? err : "(unknown error)");
        gui_log(LOG_ERROR, msg);
        if (lua_gettop(L) > 0) lua_pop(L, 1);
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
    g_engine.initialized = 1;
}

void lua_engine_load_plugins(const char *plugin_dir) {
#ifdef _WIN32
    wchar_t pattern[512];
    wchar_t base_dir[512];
    WIN32_FIND_DATAW fd;
    wchar_t *plugin_dir_w = utf8_to_wide_dup(plugin_dir);
    HANDLE hFind;

    if (!plugin_dir_w) return;
    if (_snwprintf(base_dir, sizeof(base_dir) / sizeof(base_dir[0]), L"%ls", plugin_dir_w) < 0) {
        free(plugin_dir_w);
        return;
    }
    if (_snwprintf(pattern, sizeof(pattern) / sizeof(pattern[0]), L"%ls\\*.lua", plugin_dir_w) < 0) {
        free(plugin_dir_w);
        return;
    }

    hFind = FindFirstFileW(pattern, &fd);
    if (hFind == INVALID_HANDLE_VALUE) {
        free(plugin_dir_w);
        return;
    }
    /* Build local list first to avoid leaving g_engine.plugins empty mid-scan */
    PluginEntry local_list[MAX_PLUGINS];
    int local_count = 0;
    do {
        wchar_t full_path[512];
        char utf8_path[512];
        lua_State *check_L;

        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        if (_snwprintf(full_path, sizeof(full_path) / sizeof(full_path[0]), L"%ls\\%ls", base_dir, fd.cFileName) < 0)
            continue;
        if (!wide_to_utf8(full_path, utf8_path, sizeof(utf8_path)))
            continue;

        check_L = create_lua_state();
        if (!check_L) continue;
        if (!plugin_check_contract(check_L, utf8_path)) {
            lua_close(check_L);
            continue;
        }
        lua_close(check_L);

        if (local_count < MAX_PLUGINS) {
            memcpy(local_list[local_count].path, utf8_path, sizeof(utf8_path));
            local_count++;
        } else {
            gui_log(LOG_WARNING, "[lua_engine] MAX_PLUGINS (%d) reached; skipping %s",
                    MAX_PLUGINS, utf8_path);
        }
    } while (FindNextFileW(hFind, &fd));
    FindClose(hFind);
    free(plugin_dir_w);

    /* Atomically install the new plugin list */
    ludo_mutex_lock(&g_engine.mutex);
    g_engine.count = local_count;
    memcpy(g_engine.plugins, local_list, (size_t)local_count * sizeof(PluginEntry));
    memset(g_engine.plugins + local_count, 0,
           (MAX_PLUGINS - local_count) * sizeof(PluginEntry));
    ludo_mutex_unlock(&g_engine.mutex);
#else
    DIR *dir = opendir(plugin_dir);
    if (!dir) return;
    struct dirent *ent;
    PluginEntry local_list[MAX_PLUGINS];
    int local_count = 0;
    while ((ent = readdir(dir))) {
        char path[sizeof(g_engine.plugins[0].path)];
        lua_State *check_L;
        size_t nlen = strlen(ent->d_name);

        if (nlen < 5) continue;
        if (strcmp(ent->d_name + nlen - 4, ".lua") != 0) continue;

        snprintf(path, sizeof(path), "%s/%s", plugin_dir, ent->d_name);

        check_L = create_lua_state();
        if (!check_L) continue;
        if (!plugin_check_contract(check_L, path)) {
            lua_close(check_L);
            continue;
        }
        lua_close(check_L);

        if (local_count < MAX_PLUGINS) {
            memcpy(local_list[local_count].path, path, sizeof(path));
            local_count++;
        } else {
            gui_log(LOG_WARNING, "[lua_engine] MAX_PLUGINS (%d) reached; skipping %s",
                    MAX_PLUGINS, path);
        }
    }
    closedir(dir);

    /* Atomically install the new plugin list */
    ludo_mutex_lock(&g_engine.mutex);
    g_engine.count = local_count;
    memcpy(g_engine.plugins, local_list, (size_t)local_count * sizeof(PluginEntry));
    memset(g_engine.plugins + local_count, 0,
           (MAX_PLUGINS - local_count) * sizeof(PluginEntry));
    ludo_mutex_unlock(&g_engine.mutex);
#endif
    /* Move generic.lua to the end so specific hoster plugins get first chance */
    ludo_mutex_lock(&g_engine.mutex);
    if (g_engine.count > 1) {
        int generic_idx = -1;
        for (int i = 0; i < g_engine.count; i++) {
            size_t plen = strlen(g_engine.plugins[i].path);
            const char *suffix = "/generic.lua";
            size_t slen = strlen(suffix);
            /* Check for Windows \\generic.lua too */
            if ((plen >= slen && strcmp(g_engine.plugins[i].path + plen - slen, suffix) == 0) ||
                (plen >= slen && g_engine.plugins[i].path[plen - slen] == '\\' &&
                 strcmp(g_engine.plugins[i].path + plen - slen + 1, "generic.lua") == 0)) {
                generic_idx = i;
                break;
            }
        }
        if (generic_idx >= 0 && generic_idx < g_engine.count - 1) {
            PluginEntry tmp = g_engine.plugins[generic_idx];
            memmove(&g_engine.plugins[generic_idx], &g_engine.plugins[generic_idx + 1],
                    (size_t)(g_engine.count - generic_idx - 1) * sizeof(PluginEntry));
            g_engine.plugins[g_engine.count - 1] = tmp;
        }
    }
    ludo_mutex_unlock(&g_engine.mutex);
}

int lua_engine_process_url_l(lua_State *L, const char *url) {
    int handled = 0;
    int count;
    PluginEntry plugins[MAX_PLUGINS];

    ludo_module_set_current_source_url(L, url);
    lua_settop(L, 0); /* reset stack for reuse */

    ludo_mutex_lock(&g_engine.mutex);
    count = g_engine.count;
    memcpy(plugins, g_engine.plugins, (size_t)count * sizeof(PluginEntry));
    ludo_mutex_unlock(&g_engine.mutex);

    for (int i = 0; i < count; i++) {
        int matches;

        if (!load_plugin_file(L, plugins[i].path)) {
            const char *e = lua_tostring(L, -1);
            char msg[512];
            snprintf(msg, sizeof(msg), "[lua_engine] load error %.190s: %.280s",
                     plugins[i].path, e ? e : "?");
            gui_log(LOG_ERROR, "%s", msg);
            if (lua_gettop(L) > 0) lua_pop(L, 1);
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
                     "[lua_engine] validate error in %.180s: %.270s",
                     plugins[i].path, e ? e : "?");
            gui_log(LOG_ERROR, "%s", msg);
            lua_pop(L, 2); /* pop error + plugin table */
            continue;
        }
        matches = lua_toboolean(L, -1);
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
                     "[lua_engine] process error in %.180s: %.270s",
                     plugins[i].path, e ? e : "?");
            gui_log(LOG_ERROR, "%s", msg);
            lua_pop(L, 2);
            continue;
        }
        lua_pop(L, 1); /* pop process() return value */
        lua_pop(L, 1); /* pop plugin table */

        handled = 1;
        break; /* first matching plugin wins */
    }

    lua_settop(L, 0); /* clean stack for next URL */
    return handled;
}

int lua_engine_process_url(const char *url) {
    lua_State *L = lua_engine_create_state();
    if (!L) {
        gui_log(LOG_ERROR, "[lua_engine] failed to create Lua state");
        return 0;
    }
    int handled = lua_engine_process_url_l(L, url);
    lua_engine_close_state(L);
    return handled;
}

void lua_engine_shutdown(void) {
    if (!g_engine.initialized) return;
    ludo_mutex_destroy(&g_engine.mutex);
    g_engine.initialized = 0;
}

void lua_engine_info(void) {
    char msg[256];
    snprintf(msg, sizeof(msg), "%d plugin(s) loaded ", g_engine.count);
    gui_log(LOG_INFO, "%s", msg);
}

/* Run a specific Lua script file headlessly. Returns 1 on success, 0 on error. */
int lua_engine_run_script(const char *path) {
    gui_log(LOG_INFO, "[lua_engine] run_script: enter (%s)", path);
    lua_State *L = create_lua_state();
    if (!L) {
        gui_log(LOG_ERROR, "[lua_engine] failed to create Lua state for script");
        return 0;
    }

    /* Prepend the script's directory to package.path so require() finds
       sibling .lua files (e.g. ftcsv.lua next to the tool script). */
    {
        const char *last_fwd  = strrchr(path, '/');
        const char *last_back = strrchr(path, '\\');
        const char *last_sep  = (last_fwd > last_back) ? last_fwd : last_back;
        if (last_sep) {
            char dir[512];
            size_t dir_len = (size_t)(last_sep - path);
            if (dir_len < sizeof(dir)) {
                memcpy(dir, path, dir_len);
                dir[dir_len] = '\0';
                lua_getglobal(L, "package");
                lua_getfield(L, -1, "path");
                {
                    const char *existing = lua_tostring(L, -1);
                    char new_path[2048];
                    snprintf(new_path, sizeof(new_path), "%s/?.lua;%s", dir, existing ? existing : "");
                    lua_pop(L, 1);
                    lua_pushstring(L, new_path);
                    lua_setfield(L, -2, "path");
                }
                lua_pop(L, 1);
            }
        }
    }

    /* Load the file */
    if (lua_loadfile_utf8(L, path) != LUA_OK) {
        const char *e = lua_tostring(L, -1);
        gui_log(LOG_ERROR, "[lua_engine] load error %s: %s", path, e ? e : "?");
        lua_close(L);
        return 0;
    }

    /* Execute the chunk */
    gui_log(LOG_INFO, "[lua_engine] run_script: calling lua_pcall");
    if (lua_pcall(L, 0, 0, 0) != LUA_OK) {
        const char *e = lua_tostring(L, -1);
        gui_log(LOG_ERROR, "[lua_engine] exec error %s: %s", path, e ? e : "?");
        lua_close(L);
        return 0;
    }
    gui_log(LOG_INFO, "[lua_engine] run_script: lua_pcall returned");

    gui_log(LOG_INFO, "[lua_engine] run_script: calling lua_close");
    lua_close(L);
    gui_log(LOG_INFO, "[lua_engine] run_script: exit");
    return 1;
}