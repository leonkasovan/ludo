#ifndef LUDO_MODULE_H
#define LUDO_MODULE_H

#include <lua.h>

/*
 * Registers the "ludo" library into the given Lua state.
 * After this call Lua scripts can call:
 *
 *   id = ludo.newDownload(url, output_dir, mode)
 *              mode: ludo.DOWNLOAD_NOW | ludo.DOWNLOAD_QUEUE
 *   ludo.pauseDownload(id)
 *   ludo.removeDownload(id)
 *   ludo.logError(msg)
 *   ludo.logSuccess(msg)
 *   ludo.logInfo(msg)
 *   dir = ludo.getOutputDirectory()
 */
void ludo_module_register(lua_State *L);

#endif /* LUDO_MODULE_H */
