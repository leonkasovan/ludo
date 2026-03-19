#include "http_module.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include <lua.h>
#include <lauxlib.h>
#include <curl/curl.h>

/* ------------------------------------------------------------------ */
/* Per-Lua-state session data (stored as a light userdata in registry) */
/* ------------------------------------------------------------------ */

#define HTTP_SESSION_KEY "ludo.http.session"

/* Maximum response body buffered in memory (8 MB) */
#define HTTP_MAX_BODY (8 * 1024 * 1024)

typedef struct {
    char  cookie_file[512]; /* path to cookiejar, empty = in-memory */
    char  last_url[4096];   /* final URL after redirects             */
} HttpSession;

/* ------------------------------------------------------------------ */
/* Dynamic string buffer for curl write callback                       */
/* ------------------------------------------------------------------ */

typedef struct {
    char  *data;
    size_t len;
    size_t cap;
    int    overflow; /* set to 1 if body exceeded HTTP_MAX_BODY     */
} StrBuf;

static int strbuf_append(StrBuf *b, const char *src, size_t n) {
    if (b->overflow) return 0;
    if (b->len + n + 1 > HTTP_MAX_BODY) {
        b->overflow = 1;
        return 0;
    }
    if (b->len + n + 1 > b->cap) {
        size_t new_cap = (b->cap == 0) ? 4096 : b->cap * 2;
        while (new_cap < b->len + n + 1) new_cap *= 2;
        char *tmp = (char *)realloc(b->data, new_cap);
        if (!tmp) return 0;
        b->data = tmp;
        b->cap  = new_cap;
    }
    memcpy(b->data + b->len, src, n);
    b->len       += n;
    b->data[b->len] = '\0';
    return 1;
}

static void strbuf_free(StrBuf *b) {
    free(b->data);
    b->data = NULL;
    b->len  = 0;
    b->cap  = 0;
}

/* ------------------------------------------------------------------ */
/* curl callbacks                                                       */
/* ------------------------------------------------------------------ */

static size_t curl_write_body(char *ptr, size_t size, size_t nmemb, void *ud) {
    StrBuf *buf = (StrBuf *)ud;
    size_t total = size * nmemb;
    strbuf_append(buf, ptr, total);
    return total; /* always claim we consumed all — prevents curl abort */
}

static size_t curl_write_headers(char *ptr, size_t size, size_t nmemb, void *ud) {
    StrBuf *buf = (StrBuf *)ud;
    size_t total = size * nmemb;
    strbuf_append(buf, ptr, total);
    return total;
}

/* ------------------------------------------------------------------ */
/* Helpers                                                              */
/* ------------------------------------------------------------------ */

static HttpSession *get_session(lua_State *L) {
    lua_pushlightuserdata(L, (void *)HTTP_SESSION_KEY);
    lua_gettable(L, LUA_REGISTRYINDEX);
    HttpSession *s = (HttpSession *)lua_touserdata(L, -1);
    lua_pop(L, 1);
    return s;
}

/*
 * Apply the options table (at stack index `opts_idx`) to `curl`.
 * Pushes a struct curl_slist* for custom headers (caller must free it).
 */
static struct curl_slist *apply_options(lua_State *L, CURL *curl, int opts_idx) {
    struct curl_slist *headers = NULL;

    if (opts_idx == 0 || lua_type(L, opts_idx) != LUA_TTABLE)
        return NULL;

    /* user_agent */
    lua_getfield(L, opts_idx, "user_agent");
    if (lua_isstring(L, -1))
        curl_easy_setopt(curl, CURLOPT_USERAGENT, lua_tostring(L, -1));
    lua_pop(L, 1);

    /* follow_redirects (default true) */
    lua_getfield(L, opts_idx, "follow_redirects");
    int follow = lua_isnil(L, -1) ? 1 : lua_toboolean(L, -1);
    lua_pop(L, 1);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, (long)follow);
    if (follow) curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 10L);

    /* timeout */
    lua_getfield(L, opts_idx, "timeout");
    if (lua_isnumber(L, -1))
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, (long)lua_tonumber(L, -1));
    lua_pop(L, 1);

    /* headers table */
    lua_getfield(L, opts_idx, "headers");
    if (lua_istable(L, -1)) {
        lua_pushnil(L);
        while (lua_next(L, -2)) {
            if (lua_isstring(L, -2) && lua_isstring(L, -1)) {
                char hdr[1024];
                snprintf(hdr, sizeof(hdr), "%s: %s",
                         lua_tostring(L, -2), lua_tostring(L, -1));
                headers = curl_slist_append(headers, hdr);
            }
            lua_pop(L, 1);
        }
    }
    lua_pop(L, 1);
    if (headers)
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

    /* cookies */
    lua_getfield(L, opts_idx, "cookies");
    if (lua_isstring(L, -1)) {
        const char *cpath = lua_tostring(L, -1);
        curl_easy_setopt(curl, CURLOPT_COOKIEFILE, cpath);
        curl_easy_setopt(curl, CURLOPT_COOKIEJAR,  cpath);
    }
    lua_pop(L, 1);

    return headers;
}

/*
 * Parse the raw header buffer into a Lua table on top of the stack.
 * Each header line "Key: Value\r\n" becomes headers["Key"] = "Value".
 */
static void push_headers_table(lua_State *L, const char *raw) {
    lua_newtable(L);
    const char *p = raw;
    while (p && *p) {
        const char *eol = strstr(p, "\r\n");
        if (!eol) eol = p + strlen(p);
        /* Skip status line */
        if (strncmp(p, "HTTP/", 5) != 0) {
            const char *colon = memchr(p, ':', (size_t)(eol - p));
            if (colon) {
                /* Key */
                size_t klen = (size_t)(colon - p);
                lua_pushlstring(L, p, klen);
                /* Value: skip leading space */
                const char *val = colon + 1;
                while (*val == ' ') val++;
                size_t vlen = (size_t)(eol - val);
                lua_pushlstring(L, val, vlen);
                lua_settable(L, -3);
            }
        }
        p = (*eol == '\r') ? eol + 2 : eol + 1;
        if (*p == '\0') break;
    }
}

/*
 * Shared implementation for GET / HEAD / POST.
 * method: 0 = GET, 1 = HEAD, 2 = POST
 * Stack on entry:
 *   1  url    (string)
 *   2  [post_body] (string, POST only)
 *   2/3 [options] (table, optional)
 * Returns: body (or "") , status_code , headers_table
 */
static int http_request(lua_State *L, int method) {
    const char *url = luaL_checkstring(L, 1);

    /* POST body at arg 2, options at 2 or 3 */
    const char *post_body = NULL;
    int opts_idx = 0;
    if (method == 2) {
        post_body = luaL_optstring(L, 2, "");
        if (lua_istable(L, 3)) opts_idx = 3;
    } else {
        if (lua_istable(L, 2)) opts_idx = 2;
    }

    HttpSession *session = get_session(L);

    CURL *curl = curl_easy_init();
    if (!curl) return luaL_error(L, "curl_easy_init failed");

    StrBuf body_buf    = {0};
    StrBuf headers_buf = {0};

    curl_easy_setopt(curl, CURLOPT_URL,            url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION,  curl_write_body);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA,      &body_buf);
    curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, curl_write_headers);
    curl_easy_setopt(curl, CURLOPT_HEADERDATA,     &headers_buf);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS,      10L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT,
                     "Mozilla/5.0 LUDO/1.0");
    /* Always enable cookie engine */
    curl_easy_setopt(curl, CURLOPT_COOKIEFILE, ""); /* activate in-memory jar */

    /* Apply session cookiejar if set */
    if (session && session->cookie_file[0]) {
        curl_easy_setopt(curl, CURLOPT_COOKIEFILE, session->cookie_file);
        curl_easy_setopt(curl, CURLOPT_COOKIEJAR,  session->cookie_file);
    }

    if (method == 1) {
        curl_easy_setopt(curl, CURLOPT_NOBODY,   1L);
    } else if (method == 2) {
        curl_easy_setopt(curl, CURLOPT_POST,     1L);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, post_body);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long)strlen(post_body));
    }

    struct curl_slist *custom_hdrs = apply_options(L, curl, opts_idx);

    CURLcode res = curl_easy_perform(curl);

    long status = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);

    /* Store last effective URL */
    if (session) {
        char *eff_url = NULL;
        curl_easy_getinfo(curl, CURLINFO_EFFECTIVE_URL, &eff_url);
        if (eff_url)
            strncpy(session->last_url, eff_url,
                    sizeof(session->last_url) - 1);
    }

    curl_slist_free_all(custom_hdrs);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) {
        strbuf_free(&body_buf);
        strbuf_free(&headers_buf);
        return luaL_error(L, "http request failed: %s",
                          curl_easy_strerror(res));
    }

    /* Return: body, status, headers */
    lua_pushlstring(L, body_buf.data ? body_buf.data : "",
                    body_buf.data ? body_buf.len : 0);
    lua_pushinteger(L, (lua_Integer)status);
    push_headers_table(L, headers_buf.data ? headers_buf.data : "");

    strbuf_free(&body_buf);
    strbuf_free(&headers_buf);
    return 3;
}

/* ------------------------------------------------------------------ */
/* Lua-callable functions                                               */
/* ------------------------------------------------------------------ */

static int lua_http_get(lua_State *L)  { return http_request(L, 0); }
static int lua_http_head(lua_State *L) { return http_request(L, 1); }
static int lua_http_post(lua_State *L) { return http_request(L, 2); }

static int lua_http_set_cookie(lua_State *L) {
    const char *path = luaL_checkstring(L, 1);
    HttpSession *s = get_session(L);
    if (s) {
        strncpy(s->cookie_file, path, sizeof(s->cookie_file) - 1);
        s->cookie_file[sizeof(s->cookie_file) - 1] = '\0';
    }
    return 0;
}

static int lua_http_clear_cookies(lua_State *L) {
    HttpSession *s = get_session(L);
    if (s) {
        s->cookie_file[0] = '\0';
        s->last_url[0]    = '\0';
    }
    return 0;
}

static int lua_http_get_last_url(lua_State *L) {
    HttpSession *s = get_session(L);
    lua_pushstring(L, (s && s->last_url[0]) ? s->last_url : "");
    return 1;
}

/* http.url_encode(str) */
static int lua_http_url_encode(lua_State *L) {
    size_t len;
    const char *s = luaL_checklstring(L, 1, &len);
    CURL *curl = curl_easy_init();
    if (!curl) return luaL_error(L, "curl_easy_init failed");
    char *enc = curl_easy_escape(curl, s, (int)len);
    if (enc) {
        lua_pushstring(L, enc);
        curl_free(enc);
    } else {
        lua_pushstring(L, s);
    }
    curl_easy_cleanup(curl);
    return 1;
}

/* http.url_decode(str) */
static int lua_http_url_decode(lua_State *L) {
    size_t len;
    const char *s = luaL_checklstring(L, 1, &len);
    CURL *curl = curl_easy_init();
    if (!curl) return luaL_error(L, "curl_easy_init failed");
    int out_len = 0;
    char *dec = curl_easy_unescape(curl, s, (int)len, &out_len);
    if (dec) {
        lua_pushlstring(L, dec, (size_t)out_len);
        curl_free(dec);
    } else {
        lua_pushstring(L, s);
    }
    curl_easy_cleanup(curl);
    return 1;
}

/* http.parse_url(url) -> {scheme, host, path, query, port} */
static int lua_http_parse_url(lua_State *L) {
    const char *url = luaL_checkstring(L, 1);

    /* Manual parse: scheme://[host[:port]][/path][?query] */
    lua_newtable(L);

    /* scheme */
    const char *cs = strstr(url, "://");
    if (cs) {
        lua_pushlstring(L, url, (size_t)(cs - url));
        lua_setfield(L, -2, "scheme");
        url = cs + 3;
    } else {
        lua_pushstring(L, "");
        lua_setfield(L, -2, "scheme");
    }

    /* host[:port] until first '/' */
    const char *slash = strchr(url, '/');
    const char *q     = strchr(url, '?');
    const char *host_end = slash ? slash : (q ? q : url + strlen(url));
    const char *colon = memchr(url, ':', (size_t)(host_end - url));
    if (colon) {
        lua_pushlstring(L, url, (size_t)(colon - url));
        lua_setfield(L, -2, "host");
        lua_pushlstring(L, colon + 1, (size_t)(host_end - colon - 1));
        lua_setfield(L, -2, "port");
    } else {
        lua_pushlstring(L, url, (size_t)(host_end - url));
        lua_setfield(L, -2, "host");
        lua_pushstring(L, "");
        lua_setfield(L, -2, "port");
    }

    if (slash) {
        const char *path_end = q ? q : slash + strlen(slash);
        lua_pushlstring(L, slash, (size_t)(path_end - slash));
        lua_setfield(L, -2, "path");
    } else {
        lua_pushstring(L, "/");
        lua_setfield(L, -2, "path");
    }

    if (q) {
        lua_pushstring(L, q + 1);
        lua_setfield(L, -2, "query");
    } else {
        lua_pushstring(L, "");
        lua_setfield(L, -2, "query");
    }

    return 1;
}

/* ------------------------------------------------------------------ */
/* Module registration                                                  */
/* ------------------------------------------------------------------ */

static const luaL_Reg http_funcs[] = {
    { "get",          lua_http_get         },
    { "head",         lua_http_head        },
    { "post",         lua_http_post        },
    { "set_cookie",   lua_http_set_cookie  },
    { "clear_cookies",lua_http_clear_cookies },
    { "get_last_url", lua_http_get_last_url },
    { "url_encode",   lua_http_url_encode  },
    { "url_decode",   lua_http_url_decode  },
    { "parse_url",    lua_http_parse_url   },
    { NULL,           NULL                 }
};

void http_module_register(lua_State *L) {
    /* Create and stash a per-state session object */
    HttpSession *session = (HttpSession *)calloc(1, sizeof(HttpSession));
    lua_pushlightuserdata(L, (void *)HTTP_SESSION_KEY);
    lua_pushlightuserdata(L, session);
    lua_settable(L, LUA_REGISTRYINDEX);

    /* Register http.* table as a global */
    luaL_newlib(L, http_funcs);
    lua_setglobal(L, "http");
}
