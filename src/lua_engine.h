#ifndef LUDO_LUA_ENGINE_H
#define LUDO_LUA_ENGINE_H

#include <lua.h>

/*
 * Lua plugin engine.
 *
 * Each worker thread that processes URLs should acquire a single lua_State
 * once and reuse it across all URLs — this avoids the overhead of VM
 * creation/destruction per URL (see lua_engine_process_url_l).
 *
 * Lifecycle:
 *   lua_engine_init()           — called once at startup (main thread)
 *   lua_engine_load_plugins()   — scans a directory for *.lua plugins
 *   lua_engine_process_url()    — called from a worker thread per URL
 *                                  (creates/destroys state per call)
 *   lua_engine_process_url_l()  — same but reuses a caller-managed state
 *   lua_engine_create_state()   — acquire a fresh Lua state
 *   lua_engine_close_state()    — destroy a Lua state
 *   lua_engine_shutdown()       — called once at exit (main thread)
 */

/* Initialise the engine (must be called before all others). */
void lua_engine_init(void);

/*
 * Scan `plugin_dir` for *.lua files and load each one into an internal
 * plugin registry.  Safe to call multiple times.
 */
void lua_engine_load_plugins(const char *plugin_dir);

/*
 * Process a single URL:
 *   1. Iterate over loaded plugins calling plugin.validate(url).
 *   2. On first match, call plugin.process(url).
 *      The process() function is expected to call ludo.newDownload()
 *      internaly to enqueue work.
 *   3. If no plugin matches, attempt a direct download.
 *
 * Must be called from a worker (non-GUI) thread.
 * Returns 1 if a plugin handled the URL, 0 otherwise.
 */
int lua_engine_process_url(const char *url);

/*
 * Same as lua_engine_process_url but reuses an existing lua_State.
 * The caller owns the state lifecycle (create/close).
 */
int lua_engine_process_url_l(lua_State *L, const char *url);

/* Create a fresh Lua state pre-loaded with http, ludo, zip, aes modules. */
lua_State *lua_engine_create_state(void);
/* Destroy a Lua state created by lua_engine_create_state. */
void lua_engine_close_state(lua_State *L);

/* Release all engine resources. */
void lua_engine_shutdown(void);

/* Log basic info about loaded plugins to the GUI log. */
void lua_engine_info(void);

/* Run a specific Lua script file headlessly. Returns 1 on success, 0 on error. */
int lua_engine_run_script(const char *path);

#endif /* LUDO_LUA_ENGINE_H */
