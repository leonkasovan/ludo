#ifndef LIBUILUA_H
#define LIBUILUA_H

#include <lua.h>

/* Opens the "ui" library into the given Lua state. */
int luaopen_libuilua(lua_State *L);

/* Destroys all windows created through the ui.NewWindow() Lua binding.
   Called during app shutdown so tool-script windows don't outlive the
   main window. */
void libuilua_destroy_all_windows(void);

/* Posts WM_CLOSE to all tracked windows so their OnClosing handlers fire,
   allowing Lua tool scripts to break out of their event loops. */
void libuilua_request_close_all_windows(void);

#endif /* LIBUILUA_H */
