#ifndef LUDO_MODULE_H
#define LUDO_MODULE_H

#include <lua.h>

typedef struct uiMultilineEntry uiMultilineEntry;

typedef struct {
    uiMultilineEntry *http_response_content;
    uiMultilineEntry *http_response_header;
    uiMultilineEntry *lua_output;
} LudoTesterBindings;

/*
 * Registers the "ludo" library into the given Lua state.
 * After this call Lua scripts can call:
 *
 *   id, status, headers = ludo.newDownload(url, output_dir, mode)
 *              mode: ludo.DOWNLOAD_NOW | ludo.DOWNLOAD_QUEUE
 *   ludo.setting.maxDownloadRetry
 *   ludo.setting.maxThread
 *   ludo.pauseDownload(id)
 *   ludo.removeDownload(id)
 *   ludo.logError(msg)
 *   ludo.logSuccess(msg)
 *   ludo.logInfo(msg)
 *   dir = ludo.getOutputDirectory()
 */
void ludo_module_register(lua_State *L);
void ludo_module_set_tester_bindings(lua_State *L, const LudoTesterBindings *bindings);

#endif /* LUDO_MODULE_H */
