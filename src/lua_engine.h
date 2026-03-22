#ifndef LUDO_LUA_ENGINE_H
#define LUDO_LUA_ENGINE_H

/*
 * Lua plugin engine.
 *
 * Each worker thread that processes a URL gets its own lua_State so that
 * concurrent script execution is safe without locking.
 *
 * Lifecycle:
 *   lua_engine_init()           — called once at startup (main thread)
 *   lua_engine_load_plugins()   — scans a directory for *.lua plugins
 *   lua_engine_process_url()    — called from a worker thread per URL
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

/* Release all engine resources. */
void lua_engine_shutdown(void);

/* Log basic info about loaded plugins to the GUI log. */
void lua_engine_info(void);

#endif /* LUDO_LUA_ENGINE_H */
