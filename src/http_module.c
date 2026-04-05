#include "http_module.h"
#include "dm_log.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include <lua.h>
#include <lauxlib.h>
#include <curl/curl.h>

#ifdef DEBUG
static int http_curl_debug_cb(CURL *handle, curl_infotype type,
                              char *data, size_t size, void *userp)
{
    (void)handle; (void)userp;
    if (type == CURLINFO_HEADER_OUT || type == CURLINFO_HEADER_IN) {
        /* Log each line of the header block */
        size_t start = 0;
        const char *tag = (type == CURLINFO_HEADER_OUT) ? "http >" : "http <";
        while (start < size) {
            size_t end = start;
            while (end < size && data[end] != '\r' && data[end] != '\n') end++;
            if (end > start)
                dm_log("[%s] %.*s", tag, (int)(end - start), data + start);
            while (end < size && (data[end] == '\r' || data[end] == '\n')) end++;
            start = end;
        }
        return 0;
    }
    if (type != CURLINFO_TEXT) return 0;
    char buf[256];
    size_t n = size < sizeof(buf) - 1 ? size : sizeof(buf) - 1;
    memcpy(buf, data, n);
    while (n > 0 && (buf[n-1] == '\n' || buf[n-1] == '\r')) n--;
    buf[n] = '\0';
    if (n > 0) dm_log("[http] %s", buf);
    return 0;
}
#endif

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

    /* http_version (optional): numeric 1 => HTTP/1.1, 2 => HTTP/2.0 */
    lua_getfield(L, opts_idx, "http_version");
    if (lua_isnumber(L, -1)) {
        int hv = (int)lua_tointeger(L, -1);
        if (hv == 1) {
            curl_easy_setopt(curl, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_1_1);
        } else if (hv == 2) {
            curl_easy_setopt(curl, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_2_0);
        }
    }
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
        const char *eol = strpbrk(p, "\r\n");
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
        p = eol;
        while (*p == '\r' || *p == '\n') p++;
        if (*p == '\0') break;
    }
}

/* ------------------------------------------------------------------ */
/* SHA-256 (standalone, no external dependency)                        */
/* ------------------------------------------------------------------ */

#include <stdint.h>

static const uint32_t sha256_k[64] = {
    0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,
    0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
    0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,
    0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
    0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,
    0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
    0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,
    0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
    0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,
    0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
    0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,
    0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
    0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,
    0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
    0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,
    0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2,
};

#define SHA256_ROTR(x,n) (((x) >> (n)) | ((x) << (32 - (n))))
#define SHA256_CH(x,y,z) (((x) & (y)) ^ (~(x) & (z)))
#define SHA256_MAJ(x,y,z)(((x) & (y)) ^ ((x) & (z)) ^ ((y) & (z)))
#define SHA256_S0(x) (SHA256_ROTR(x,2)  ^ SHA256_ROTR(x,13) ^ SHA256_ROTR(x,22))
#define SHA256_S1(x) (SHA256_ROTR(x,6)  ^ SHA256_ROTR(x,11) ^ SHA256_ROTR(x,25))
#define SHA256_s0(x) (SHA256_ROTR(x,7)  ^ SHA256_ROTR(x,18) ^ ((x) >> 3))
#define SHA256_s1(x) (SHA256_ROTR(x,17) ^ SHA256_ROTR(x,19) ^ ((x) >> 10))

typedef struct {
    uint32_t state[8];
    uint64_t count;
    uint8_t  buf[64];
    size_t   buf_len;
} Sha256Ctx;

static void sha256_init(Sha256Ctx *ctx) {
    ctx->state[0] = 0x6a09e667u; ctx->state[1] = 0xbb67ae85u;
    ctx->state[2] = 0x3c6ef372u; ctx->state[3] = 0xa54ff53au;
    ctx->state[4] = 0x510e527fu; ctx->state[5] = 0x9b05688cu;
    ctx->state[6] = 0x1f83d9abu; ctx->state[7] = 0x5be0cd19u;
    ctx->count = 0; ctx->buf_len = 0;
}

static void sha256_transform(uint32_t *state, const uint8_t *block) {
    uint32_t w[64], a, b, c, d, e, f, g, h, t1, t2;
    int i;
    for (i = 0; i < 16; i++)
        w[i] = ((uint32_t)block[i*4]<<24)|((uint32_t)block[i*4+1]<<16)|
               ((uint32_t)block[i*4+2]<<8)|(uint32_t)block[i*4+3];
    for (i = 16; i < 64; i++)
        w[i] = SHA256_s1(w[i-2]) + w[i-7] + SHA256_s0(w[i-15]) + w[i-16];
    a=state[0]; b=state[1]; c=state[2]; d=state[3];
    e=state[4]; f=state[5]; g=state[6]; h=state[7];
    for (i = 0; i < 64; i++) {
        t1 = h + SHA256_S1(e) + SHA256_CH(e,f,g) + sha256_k[i] + w[i];
        t2 = SHA256_S0(a) + SHA256_MAJ(a,b,c);
        h=g; g=f; f=e; e=d+t1; d=c; c=b; b=a; a=t1+t2;
    }
    state[0]+=a; state[1]+=b; state[2]+=c; state[3]+=d;
    state[4]+=e; state[5]+=f; state[6]+=g; state[7]+=h;
}

static void sha256_update(Sha256Ctx *ctx, const uint8_t *data, size_t len) {
    while (len > 0) {
        size_t room = 64 - ctx->buf_len;
        size_t take = len < room ? len : room;
        memcpy(ctx->buf + ctx->buf_len, data, take);
        ctx->buf_len += take; ctx->count += take;
        data += take; len -= take;
        if (ctx->buf_len == 64) { sha256_transform(ctx->state, ctx->buf); ctx->buf_len = 0; }
    }
}

static void sha256_final(Sha256Ctx *ctx, uint8_t *digest) {
    uint64_t bit_count = ctx->count * 8;
    uint8_t pad = 0x80;
    sha256_update(ctx, &pad, 1);
    while (ctx->buf_len != 56) { uint8_t z = 0; sha256_update(ctx, &z, 1); }
    for (int i = 7; i >= 0; i--) { uint8_t bv = (uint8_t)(bit_count >> (i * 8)); sha256_update(ctx, &bv, 1); }
    for (int i = 0; i < 8; i++) {
        digest[i*4+0]=(ctx->state[i]>>24)&0xFF; digest[i*4+1]=(ctx->state[i]>>16)&0xFF;
        digest[i*4+2]=(ctx->state[i]>>8)&0xFF;  digest[i*4+3]=ctx->state[i]&0xFF;
    }
}

/* http.sha256(str) → 32-byte raw binary digest */
static int http_sha256(lua_State *L) {
    size_t in_len;
    const unsigned char *in = (const unsigned char *)luaL_checklstring(L, 1, &in_len);
    Sha256Ctx ctx;
    uint8_t digest[32];
    sha256_init(&ctx);
    sha256_update(&ctx, in, in_len);
    sha256_final(&ctx, digest);
    lua_pushlstring(L, (const char *)digest, 32);
    return 1;
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
static int http_base64_encode(lua_State *L) {
    size_t in_len;
    const unsigned char *in = (const unsigned char *)luaL_checklstring(L, 1, &in_len);
    static const char tbl[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    luaL_Buffer b;
    luaL_buffinit(L, &b);
    size_t i = 0;
    while (i + 2 < in_len) {
        unsigned v = (in[i] << 16) | (in[i+1] << 8) | in[i+2];
        luaL_addchar(&b, tbl[(v >> 18) & 0x3F]);
        luaL_addchar(&b, tbl[(v >> 12) & 0x3F]);
        luaL_addchar(&b, tbl[(v >> 6) & 0x3F]);
        luaL_addchar(&b, tbl[v & 0x3F]);
        i += 3;
    }
    if (i < in_len) {
        unsigned v = in[i] << 16;
        luaL_addchar(&b, tbl[(v >> 18) & 0x3F]);
        if (i + 1 < in_len) {
            v |= in[i+1] << 8;
            luaL_addchar(&b, tbl[(v >> 12) & 0x3F]);
            luaL_addchar(&b, tbl[(v >> 6) & 0x3F]);
            luaL_addchar(&b, '=');
        } else {
            luaL_addchar(&b, tbl[(v >> 12) & 0x3F]);
            luaL_addchar(&b, '=');
            luaL_addchar(&b, '=');
        }
    }
    luaL_pushresult(&b);
    return 1;
}

static int http_base64_decode(lua_State *L) {
    size_t in_len;
    const char *in = luaL_checklstring(L, 1, &in_len);
    unsigned char dtable[256] = {0};
    for (int i = 0; i < 64; i++) dtable[(unsigned char)"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/"[i]] = i + 1;
    luaL_Buffer b;
    luaL_buffinit(L, &b);
    size_t i = 0;
    unsigned v = 0, valb = 0;
    while (i < in_len) {
        unsigned char c = (unsigned char)in[i++];
        if (c == '=') break;
        if (!dtable[c]) continue;
        v = (v << 6) | (dtable[c] - 1);
        valb += 6;
        if (valb >= 8) {
            valb -= 8;
            luaL_addchar(&b, (char)((v >> valb) & 0xFF));
        }
    }
    luaL_pushresult(&b);
    return 1;
}

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
    curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, "gzip, deflate, zstd, br");
    curl_easy_setopt(curl, CURLOPT_USERAGENT,
                     "Mozilla/5.0 LUDO/1.0");
    /* Always enable cookie engine */
    curl_easy_setopt(curl, CURLOPT_COOKIEFILE, ""); /* activate in-memory jar */
#ifdef DEBUG
    curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);
    curl_easy_setopt(curl, CURLOPT_DEBUGFUNCTION, http_curl_debug_cb);
    curl_easy_setopt(curl, CURLOPT_DEBUGDATA, NULL);
    dm_log("[http_request] %s", url);
#endif

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

/* http.read_cookie(filepath, name) -> value or nil
 *
 * Read a Netscape-format cookie file and return the value of a named
 * cookie.  Returns nil if the file cannot be opened or the cookie is
 * not found.  This is a pure file-parsing helper — it works on both
 * the session cookie file (set via http.set_cookie) and user-exported
 * cookie files.
 *
 * Netscape format (one cookie per line, TAB-separated):
 *   domain \t flag \t path \t secure \t expiry \t name \t value
 * Lines starting with '#' are comments and are skipped.
 */
static int lua_http_read_cookie(lua_State *L) {
    const char *filepath = luaL_checkstring(L, 1);
    const char *name     = luaL_checkstring(L, 2);

    FILE *f = fopen(filepath, "r");
    if (!f) {
        lua_pushnil(L);
        return 1;
    }

    char line[4096];
    while (fgets(line, sizeof(line), f)) {
        /* Skip comment lines and empty lines.
         * Exception: libcurl encodes HttpOnly cookies as lines starting with
         * "#HttpOnly_" followed by the domain — strip that prefix and parse. */
        if (line[0] == '#') {
            if (strncmp(line, "#HttpOnly_", 10) == 0)
                memmove(line, line + 10, strlen(line + 10) + 1);
            else
                continue;
        }
        if (line[0] == '\n' || line[0] == '\r') continue;

        /* Parse: domain \t flag \t path \t secure \t expiry \t name \t value */
        char *fields[7];
        int n = 0;
        char *tok = line;
        while (n < 7) {
            fields[n] = tok;
            char *tab = strchr(tok, '\t');
            if (tab) {
                *tab = '\0';
                tok = tab + 1;
                n++;
            } else {
                n++;
                break;
            }
        }
        if (n < 7) continue;

        /* Strip trailing \r\n from the value field */
        size_t vlen = strlen(fields[6]);
        while (vlen > 0 && (fields[6][vlen-1] == '\r' || fields[6][vlen-1] == '\n'))
            fields[6][--vlen] = '\0';

        if (strcmp(fields[5], name) == 0) {
            lua_pushlstring(L, fields[6], vlen);
            fclose(f);
            return 1;
        }
    }

    fclose(f);
    lua_pushnil(L);
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
    { "base64_encode", http_base64_encode  },
    { "base64_decode", http_base64_decode  },
    { "sha256",        http_sha256         },
    { "read_cookie",  lua_http_read_cookie },
    { NULL,           NULL                 }
};

void http_module_register(lua_State *L) {
    /* Create and stash a per-state session object */
    HttpSession *session = (HttpSession *)lua_newuserdata(L, sizeof(HttpSession));
    memset(session, 0, sizeof(HttpSession));
    lua_pushlightuserdata(L, (void *)HTTP_SESSION_KEY);
    lua_pushvalue(L, -2);
    lua_settable(L, LUA_REGISTRYINDEX);
    lua_pop(L, 1);

    /* Register http.* table as a global */
    luaL_newlib(L, http_funcs);
    lua_setglobal(L, "http");
}
