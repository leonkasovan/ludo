/* zip_module.c — Lua "zip" module for the Ludo download manager.
 *
 * Exposes two calling conventions:
 *   status [, errmsg] = zip.create(output_path, {file1, file2, ...})
 *   status [, errmsg] = zip.create(output_path, directory [, glob_filter])
 *
 * Uses zlib 1.2.8 raw DEFLATE; implements the ZIP 2.0 format (PKZIP spec)
 * directly — no minizip dependency required.
 */

#include "zip_module.h"
#include "dm_log.h"

#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>

#include <zlib.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>

#ifdef _WIN32
#  include <windows.h>
#  include "platform_utils.h"
#else
#  include <dirent.h>
#  include <sys/stat.h>
#endif

/* ------------------------------------------------------------------ */
/* Internal constants                                                   */
/* ------------------------------------------------------------------ */

#define ZIP_CHUNK       65536   /* deflate I/O buffer (bytes)          */
#define ZIP_MAX_ENTRIES 4096    /* maximum files per archive           */
#define ZIP_MAX_NAME    512     /* maximum in-archive path length      */

/* ------------------------------------------------------------------ */
/* Per-entry record                                                     */
/* ------------------------------------------------------------------ */

typedef struct {
    char     name[ZIP_MAX_NAME]; /* in-archive path (forward slashes)  */
    uint32_t crc;
    uint32_t comp_size;
    uint32_t uncomp_size;
    uint32_t local_offset;       /* byte offset of local header        */
    uint16_t method;             /* 0 = store, 8 = deflate             */
    uint16_t dos_time;
    uint16_t dos_date;
} ZipEntry;

typedef struct {
    FILE     *fp;
    ZipEntry  entries[ZIP_MAX_ENTRIES];
    int       count;
    char      errmsg[256];
} ZipWriter;

/* ------------------------------------------------------------------ */
/* Little-endian helpers                                                */
/* ------------------------------------------------------------------ */

static int zw_u16(ZipWriter *w, uint16_t v)
{
    unsigned char b[2];
    b[0] = (unsigned char)(v & 0xFF);
    b[1] = (unsigned char)(v >> 8);
    return fwrite(b, 1, 2, w->fp) == 2 ? 0 : -1;
}

static int zw_u32(ZipWriter *w, uint32_t v)
{
    unsigned char b[4];
    b[0] = (unsigned char)(v & 0xFF);
    b[1] = (unsigned char)((v >>  8) & 0xFF);
    b[2] = (unsigned char)((v >> 16) & 0xFF);
    b[3] = (unsigned char)((v >> 24) & 0xFF);
    return fwrite(b, 1, 4, w->fp) == 4 ? 0 : -1;
}

/* ------------------------------------------------------------------ */
/* DOS timestamp for current local time                                 */
/* ------------------------------------------------------------------ */

static void current_dos_time(uint16_t *dt, uint16_t *dd)
{
    time_t     now = time(NULL);
    struct tm *t   = localtime(&now);
    if (!t) { *dt = 0; *dd = 0; return; }
    *dt = (uint16_t)(((t->tm_hour & 0x1F) << 11) |
                     ((t->tm_min  & 0x3F) <<  5) |
                     ((t->tm_sec / 2)     & 0x1F));
    *dd = (uint16_t)((((t->tm_year - 80) & 0x7F) << 9) |
                     (((t->tm_mon + 1)   & 0x0F) << 5) |
                       (t->tm_mday       & 0x1F));
}

/* ------------------------------------------------------------------ */
/* Portable file open helpers (UTF-8 paths on Windows)                  */
/* ------------------------------------------------------------------ */

static FILE *open_rb(const char *path)
{
#ifdef _WIN32
    wchar_t *wp = utf8_to_wide_dup(path);
    FILE    *fp = NULL;
    if (!wp) return NULL;
    fp = _wfopen(wp, L"rb");
    free(wp);
    return fp;
#else
    return fopen(path, "rb");
#endif
}

static FILE *open_wb(const char *path)
{
#ifdef _WIN32
    wchar_t *wp = utf8_to_wide_dup(path);
    FILE    *fp = NULL;
    if (!wp) return NULL;
    fp = _wfopen(wp, L"wb");
    free(wp);
    return fp;
#else
    return fopen(path, "wb");
#endif
}

/* ------------------------------------------------------------------ */
/* Local file header (30 + namelen bytes)                               */
/* ------------------------------------------------------------------ */

static int zw_write_local_header(ZipWriter *w, const ZipEntry *e)
{
    uint16_t nl = (uint16_t)strlen(e->name);
    if (fwrite("\x50\x4b\x03\x04", 1, 4, w->fp) != 4) return -1;
    if (zw_u16(w, 20)           < 0) return -1; /* version needed: 2.0  */
    if (zw_u16(w, 0)            < 0) return -1; /* general purpose bits  */
    if (zw_u16(w, e->method)    < 0) return -1;
    if (zw_u16(w, e->dos_time)  < 0) return -1;
    if (zw_u16(w, e->dos_date)  < 0) return -1;
    if (zw_u32(w, e->crc)       < 0) return -1;
    if (zw_u32(w, e->comp_size) < 0) return -1;
    if (zw_u32(w, e->uncomp_size) < 0) return -1;
    if (zw_u16(w, nl)           < 0) return -1;
    if (zw_u16(w, 0)            < 0) return -1; /* extra field length    */
    if (fwrite(e->name, 1, nl, w->fp) != nl) return -1;
    return 0;
}

/* Seek back into the previously written local header and patch         *
 * the CRC-32 / sizes once compression is complete.                     *
 * The fields sit at local_offset + 14 (skipping sig + versions +       *
 * flags + method + time + date = 4+2+2+2+2+2 = 14 bytes).             */
static int zw_patch_local_header(ZipWriter *w, const ZipEntry *e)
{
    long save = ftell(w->fp);
    if (fseek(w->fp, (long)e->local_offset + 14, SEEK_SET) != 0) return -1;
    if (zw_u32(w, e->crc)        < 0) goto err;
    if (zw_u32(w, e->comp_size)  < 0) goto err;
    if (zw_u32(w, e->uncomp_size) < 0) goto err;
    if (fseek(w->fp, save, SEEK_SET) != 0) goto err;
    return 0;
err:
    fseek(w->fp, save, SEEK_SET);
    return -1;
}

/* ------------------------------------------------------------------ */
/* Compress one file and append it to the archive                       */
/* ------------------------------------------------------------------ */

static int zw_add_file(ZipWriter *w, const char *src_path,
                       const char *entry_name)
{
    FILE          *in   = NULL;
    unsigned char *ibuf = NULL;
    unsigned char *obuf = NULL;
    z_stream       strm;
    ZipEntry      *e;
    int            ret  = -1;

    if (w->count >= ZIP_MAX_ENTRIES) {
        snprintf(w->errmsg, sizeof(w->errmsg),
                 "too many entries (limit %d)", ZIP_MAX_ENTRIES);
        return -1;
    }

    in = open_rb(src_path);
    if (!in) {
        snprintf(w->errmsg, sizeof(w->errmsg),
                 "cannot open: %.240s", src_path);
        return -1;
    }

    ibuf = (unsigned char *)malloc(ZIP_CHUNK);
    obuf = (unsigned char *)malloc(ZIP_CHUNK);
    if (!ibuf || !obuf) {
        snprintf(w->errmsg, sizeof(w->errmsg), "out of memory");
        goto cleanup;
    }

    e = &w->entries[w->count];
    memset(e, 0, sizeof(*e));
    strncpy(e->name, entry_name, ZIP_MAX_NAME - 1);
    current_dos_time(&e->dos_time, &e->dos_date);
    e->method       = Z_DEFLATED;
    e->crc          = (uint32_t)crc32(0L, Z_NULL, 0);
    e->local_offset = (uint32_t)ftell(w->fp);

    /* Write local header with placeholder CRC/sizes; patched below.    */
    if (zw_write_local_header(w, e) < 0) {
        snprintf(w->errmsg, sizeof(w->errmsg), "write error (local header)");
        goto cleanup;
    }

    /* Raw DEFLATE: -MAX_WBITS suppresses the zlib/gzip wrapper.        */
    memset(&strm, 0, sizeof(strm));
    if (deflateInit2(&strm, Z_DEFAULT_COMPRESSION, Z_DEFLATED,
                     -MAX_WBITS, 8, Z_DEFAULT_STRATEGY) != Z_OK) {
        snprintf(w->errmsg, sizeof(w->errmsg), "deflateInit2 failed");
        goto cleanup;
    }

    {
        int flush;
        do {
            size_t have;
            strm.avail_in = (uInt)fread(ibuf, 1, ZIP_CHUNK, in);
            if (ferror(in)) {
                deflateEnd(&strm);
                snprintf(w->errmsg, sizeof(w->errmsg),
                         "read error: %.220s", src_path);
                goto cleanup;
            }
            flush = feof(in) ? Z_FINISH : Z_NO_FLUSH;
            e->crc         = (uint32_t)crc32(e->crc, ibuf, strm.avail_in);
            e->uncomp_size += (uint32_t)strm.avail_in;
            strm.next_in   = ibuf;

            do {
                strm.avail_out = ZIP_CHUNK;
                strm.next_out  = obuf;
                if (deflate(&strm, flush) == Z_STREAM_ERROR) {
                    deflateEnd(&strm);
                    snprintf(w->errmsg, sizeof(w->errmsg), "deflate error");
                    goto cleanup;
                }
                have = ZIP_CHUNK - strm.avail_out;
                if (have > 0) {
                    if (fwrite(obuf, 1, have, w->fp) != have) {
                        deflateEnd(&strm);
                        snprintf(w->errmsg, sizeof(w->errmsg),
                                 "write error (data)");
                        goto cleanup;
                    }
                    e->comp_size += (uint32_t)have;
                }
            } while (strm.avail_out == 0);
        } while (flush != Z_FINISH);
    }

    deflateEnd(&strm);

    if (zw_patch_local_header(w, e) < 0) {
        snprintf(w->errmsg, sizeof(w->errmsg),
                 "write error (patch header)");
        goto cleanup;
    }

    w->count++;
    ret = 0;

cleanup:
    free(ibuf);
    free(obuf);
    fclose(in);
    return ret;
}

/* ------------------------------------------------------------------ */
/* Central directory + End of Central Directory record                  */
/* ------------------------------------------------------------------ */

static int zw_finalize(ZipWriter *w)
{
    uint32_t cd_offset = (uint32_t)ftell(w->fp);
    uint32_t cd_size   = 0;
    int      i;

    for (i = 0; i < w->count; i++) {
        const ZipEntry *e  = &w->entries[i];
        uint16_t        nl = (uint16_t)strlen(e->name);
        uint32_t        entry_start = (uint32_t)ftell(w->fp);

        if (fwrite("\x50\x4b\x01\x02", 1, 4, w->fp) != 4) return -1;
        if (zw_u16(w, 20)            < 0) return -1; /* version made by  */
        if (zw_u16(w, 20)            < 0) return -1; /* version needed   */
        if (zw_u16(w, 0)             < 0) return -1; /* general purpose  */
        if (zw_u16(w, e->method)     < 0) return -1;
        if (zw_u16(w, e->dos_time)   < 0) return -1;
        if (zw_u16(w, e->dos_date)   < 0) return -1;
        if (zw_u32(w, e->crc)        < 0) return -1;
        if (zw_u32(w, e->comp_size)  < 0) return -1;
        if (zw_u32(w, e->uncomp_size) < 0) return -1;
        if (zw_u16(w, nl)            < 0) return -1;
        if (zw_u16(w, 0)             < 0) return -1; /* extra len        */
        if (zw_u16(w, 0)             < 0) return -1; /* comment len      */
        if (zw_u16(w, 0)             < 0) return -1; /* disk number      */
        if (zw_u16(w, 0)             < 0) return -1; /* int attrs        */
        if (zw_u32(w, 0)             < 0) return -1; /* ext attrs        */
        if (zw_u32(w, e->local_offset) < 0) return -1;
        if (fwrite(e->name, 1, nl, w->fp) != nl) return -1;
        cd_size += (uint32_t)((uint32_t)ftell(w->fp) - entry_start);
    }

    /* End of central directory record */
    if (fwrite("\x50\x4b\x05\x06", 1, 4, w->fp) != 4) return -1;
    if (zw_u16(w, 0)                        < 0) return -1; /* disk num    */
    if (zw_u16(w, 0)                        < 0) return -1; /* start disk  */
    if (zw_u16(w, (uint16_t)w->count)       < 0) return -1; /* entries/disk*/
    if (zw_u16(w, (uint16_t)w->count)       < 0) return -1; /* total       */
    if (zw_u32(w, cd_size)                  < 0) return -1;
    if (zw_u32(w, cd_offset)                < 0) return -1;
    if (zw_u16(w, 0)                        < 0) return -1; /* comment len */
    return 0;
}

/* ------------------------------------------------------------------ */
/* Case-insensitive glob matcher (* = any sequence, ? = any char)      */
/* ------------------------------------------------------------------ */

static int glob_match(const char *pat, const char *str)
{
    while (*pat && *str) {
        if (*pat == '*') {
            while (*pat == '*') pat++;
            if (!*pat) return 1;
            while (*str) {
                if (glob_match(pat, str)) return 1;
                str++;
            }
            return 0;
        }
        if (*pat == '?') {
            pat++; str++;
        } else {
            /* ASCII case-insensitive comparison */
            char pc = (*pat >= 'A' && *pat <= 'Z') ? (*pat + 32) : *pat;
            char sc = (*str >= 'A' && *str <= 'Z') ? (*str + 32) : *str;
            if (pc != sc) return 0;
            pat++; str++;
        }
    }
    while (*pat == '*') pat++;
    return (!*pat && !*str);
}

/* ------------------------------------------------------------------ */
/* Basename helper: returns pointer past the last path separator        */
/* ------------------------------------------------------------------ */

static const char *path_basename(const char *path)
{
    const char *last = path;
    const char *p    = path;
    while (*p) {
        if (*p == '/' || *p == '\\') last = p + 1;
        p++;
    }
    return last;
}

/* Replace all backslashes with forward slashes in place.               */
static void slash_normalise(char *s)
{
    for (; *s; s++)
        if (*s == '\\') *s = '/';
}

/* ------------------------------------------------------------------ */
/* Recursive directory scan                                             */
/* ------------------------------------------------------------------ */

#ifdef _WIN32

static int scan_dir(ZipWriter *w, const char *base,
                    const char *rel, const char *filter)
{
    char            pattern[1024];
    wchar_t        *wp;
    HANDLE          hf;
    WIN32_FIND_DATAW fd;

    if (rel && rel[0])
        snprintf(pattern, sizeof(pattern), "%s\\%s\\*", base, rel);
    else
        snprintf(pattern, sizeof(pattern), "%s\\*", base);

    wp = utf8_to_wide_dup(pattern);
    if (!wp) return -1;
    hf = FindFirstFileW(wp, &fd);
    free(wp);

    if (hf == INVALID_HANDLE_VALUE) {
        snprintf(w->errmsg, sizeof(w->errmsg),
                 "cannot open dir: %.220s", pattern);
        return -1;
    }

    do {
        char name8[MAX_PATH];
        char child_rel[512];
        char child_full[1024];

        if (!wide_to_utf8(fd.cFileName, name8, sizeof(name8))) continue;
        if (strcmp(name8, ".") == 0 || strcmp(name8, "..") == 0) continue;

        if (rel && rel[0])
            snprintf(child_rel,  sizeof(child_rel),  "%s/%s", rel, name8);
        else
            snprintf(child_rel,  sizeof(child_rel),  "%s", name8);

        snprintf(child_full, sizeof(child_full), "%s\\%s", base, child_rel);

        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            if (scan_dir(w, base, child_rel, filter) < 0) {
                FindClose(hf);
                return -1;
            }
        } else {
            if (!filter || filter[0] == '\0' || glob_match(filter, name8)) {
                char entry[512];
                snprintf(entry, sizeof(entry), "%s", child_rel);
                slash_normalise(entry);
                if (zw_add_file(w, child_full, entry) < 0) {
                    FindClose(hf);
                    return -1;
                }
            }
        }
    } while (FindNextFileW(hf, &fd));

    FindClose(hf);
    return 0;
}

#else /* POSIX */

static int scan_dir(ZipWriter *w, const char *base,
                    const char *rel, const char *filter)
{
    char        dirpath[1024];
    DIR        *d;
    struct dirent *de;

    if (rel && rel[0])
        snprintf(dirpath, sizeof(dirpath), "%s/%s", base, rel);
    else
        snprintf(dirpath, sizeof(dirpath), "%s", base);

    d = opendir(dirpath);
    if (!d) {
        snprintf(w->errmsg, sizeof(w->errmsg),
                 "cannot open dir: %.220s", dirpath);
        return -1;
    }

    while ((de = readdir(d)) != NULL) {
        char       child_rel[512];
        char       child_full[1024];
        struct stat st;

        if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0)
            continue;

        if (rel && rel[0])
            snprintf(child_rel,  sizeof(child_rel),  "%s/%s", rel, de->d_name);
        else
            snprintf(child_rel,  sizeof(child_rel),  "%s", de->d_name);

        snprintf(child_full, sizeof(child_full), "%s/%s", base, child_rel);

        if (stat(child_full, &st) != 0) continue;

        if (S_ISDIR(st.st_mode)) {
            if (scan_dir(w, base, child_rel, filter) < 0) {
                closedir(d);
                return -1;
            }
        } else {
            if (!filter || filter[0] == '\0' ||
                glob_match(filter, de->d_name)) {
                if (zw_add_file(w, child_full, child_rel) < 0) {
                    closedir(d);
                    return -1;
                }
            }
        }
    }
    closedir(d);
    return 0;
}

#endif /* _WIN32 / POSIX */

/* ------------------------------------------------------------------ */
/* Lua: zip.create(output_path, arg2 [, filter])                       */
/*                                                                      */
/* arg2 may be:                                                         */
/*   table  — array of source file paths (basenames used as entry names)*/
/*   string — root directory to pack recursively                        */
/* ------------------------------------------------------------------ */

static int lua_zip_create(lua_State *L)
{
    const char *out_path = luaL_checkstring(L, 1);
    ZipWriter  *w;
    int         ok;

    if (!lua_istable(L, 2) && !lua_isstring(L, 2)) {
        return luaL_error(L,
            "bad argument #2 to 'zip.create': "
            "expected table (files) or string (directory)");
    }

    w = (ZipWriter *)calloc(1, sizeof(ZipWriter));
    if (!w) {
        lua_pushinteger(L, -1);
        lua_pushliteral(L, "out of memory");
        return 2;
    }

    w->fp = open_wb(out_path);
    if (!w->fp) {
        free(w);
        lua_pushinteger(L, -1);
        lua_pushfstring(L, "cannot create output file: %s", out_path);
        return 2;
    }

    if (lua_istable(L, 2)) {
        /* Form 1: list of explicit file paths */
        int i, n = (int)lua_rawlen(L, 2);
        for (i = 1; i <= n; i++) {
            const char *fpath;
            const char *ename;
            lua_rawgeti(L, 2, i);
            fpath = lua_tostring(L, -1);
            lua_pop(L, 1);
            if (!fpath) continue;
            ename = path_basename(fpath);
            if (zw_add_file(w, fpath, ename) < 0) goto err;
        }
    } else {
        /* Form 2: directory [, glob_filter] */
        const char *dir    = lua_tostring(L, 2);
        const char *filter = luaL_optstring(L, 3, NULL);
        if (scan_dir(w, dir, NULL, filter) < 0) goto err;
    }

    ok = zw_finalize(w);
    fclose(w->fp);

    if (ok < 0) {
        free(w);
        lua_pushinteger(L, -1);
        lua_pushliteral(L, "write error while finalising archive");
        return 2;
    }

    dm_log("[zip] created %s (%d entries)", out_path, w->count);
    free(w);
    lua_pushinteger(L, 0);
    return 1;

err:
    {
        char msg[256];
        strncpy(msg, w->errmsg, sizeof(msg) - 1);
        msg[sizeof(msg) - 1] = '\0';
        fclose(w->fp);
        dm_log("[zip] error: %s", msg);
        free(w);
        lua_pushinteger(L, -1);
        lua_pushstring(L, msg);
        return 2;
    }
}

/* ------------------------------------------------------------------ */
/* Module registration                                                  */
/* ------------------------------------------------------------------ */

static const luaL_Reg zip_funcs[] = {
    { "create", lua_zip_create },
    { NULL,     NULL           }
};

void zip_module_register(lua_State *L)
{
    luaL_newlib(L, zip_funcs);
    lua_setglobal(L, "zip");
}
