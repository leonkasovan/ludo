#include "ludo_module.h"
#include "download_manager.h"
#include "gui.h"

#include <lauxlib.h>
#include <lualib.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* ludo.newDownload(url, output_dir, mode) -> download_id              */
/* ------------------------------------------------------------------ */

static int lua_ludo_new_download(lua_State *L) {
    const char *url        = luaL_checkstring(L, 1);
    const char *output_dir = luaL_optstring(L, 2, NULL);
    int         mode       = (int)luaL_optinteger(L, 3, DOWNLOAD_NOW);

    /* Use default output dir if none provided */
    if (!output_dir || output_dir[0] == '\0')
        output_dir = download_manager_get_output_dir();

    int id = download_manager_add(url, output_dir, (DownloadMode)mode);
    lua_pushinteger(L, (lua_Integer)id);
    return 1;
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

static int lua_ludo_log_error(lua_State *L) {
    const char *msg = luaL_checkstring(L, 1);
    gui_log(LOG_ERROR, msg);
    return 0;
}

static int lua_ludo_log_success(lua_State *L) {
    const char *msg = luaL_checkstring(L, 1);
    gui_log(LOG_SUCCESS, msg);
    return 0;
}

static int lua_ludo_log_info(lua_State *L) {
    const char *msg = luaL_checkstring(L, 1);
    gui_log(LOG_INFO, msg);
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
    luaL_newlib(L, ludo_funcs);

    /* Constants */
    lua_pushinteger(L, DOWNLOAD_NOW);
    lua_setfield(L, -2, "DOWNLOAD_NOW");

    lua_pushinteger(L, DOWNLOAD_QUEUE);
    lua_setfield(L, -2, "DOWNLOAD_QUEUE");

    lua_setglobal(L, "ludo");
}
