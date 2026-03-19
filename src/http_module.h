#ifndef LUDO_HTTP_MODULE_H
#define LUDO_HTTP_MODULE_H

#include <lua.h>

/*
 * Registers the "http" library into the given Lua state.
 * After this call Lua scripts can call:
 *
 *   body, status, headers = http.get(url [, options])
 *   status, headers        = http.head(url [, options])
 *   body, status, headers  = http.post(url, body [, options])
 *   http.set_cookie(filepath)
 *   http.clear_cookies()
 *   last_url               = http.get_last_url()
 *   encoded                = http.url_encode(str)
 *   decoded                = http.url_decode(str)
 *   tbl                    = http.parse_url(url)
 *
 * The options table may contain:
 *   user_agent       (string)
 *   follow_redirects (boolean, default true)
 *   timeout          (number, seconds)
 *   headers          (table of string->string)
 *   cookies          (string, path to cookiejar file)
 */
void http_module_register(lua_State *L);

#endif /* LUDO_HTTP_MODULE_H */
