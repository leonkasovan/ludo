#ifndef LIBUILUA_H
#define LIBUILUA_H

#include <lua.h>

/* Opens the "ui" library into the given Lua state. */
int luaopen_libuilua(lua_State *L);

#endif /* LIBUILUA_H */
