#include "download_manager.h"
#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include <errno.h>
#include <wchar.h>
#include <zlib.h>

#ifdef _WIN32
#include <sys/stat.h>
#else
#include <sys/stat.h>
#endif

#include <curl/curl.h>
#include "ui.h"
#include "gui.h"

/* Path constants for persistence */
#define DB_PATH      "dl_items.db"
#define ARCHIVE_PATH "dl_history.gz"
#define DB_MAX_BYTES 10240   /* 10 KB — archive trigger */

/* ------------------------------------------------------------------ */
/* Debug logging (writes to dm_debug.log; thread-safe, runtime toggle) */
/* ------------------------------------------------------------------ */

#include <stdarg.h>
#include "dm_log.h"

static FILE *g_log_fp = NULL;
static ludo_mutex_t g_log_mutex;
static int g_log_mutex_init = 0;
static int g_log_enabled = 0; /* runtime toggle: enabled in DEBUG or when LUDO_DEBUG=1 */

/* Forward declarations for platform-aware helpers defined later in this file */
static FILE *dm_fopen_utf8(const char *path, const char *mode);
#ifdef _WIN32
static int dm_stat_utf8(const char *path, struct _stat64 *st);
#else
static int dm_stat_utf8(const char *path, struct stat *st);
#endif
static int dm_rename_utf8(const char *old_path, const char *new_path);
static int dm_remove_utf8(const char *path);

void dm_log_init(void) {
    if (!g_log_mutex_init) { ludo_mutex_init(&g_log_mutex); g_log_mutex_init = 1; }

#ifdef DEBUG
    g_log_enabled = 1;
#else
    /* Allow users to enable debug logging at runtime via environment variable */
    const char *env = getenv("LUDO_DEBUG");
    g_log_enabled = (env && (env[0] == '1' || env[0] == 'y' || env[0] == 'Y' ||
                             env[0] == 't' || env[0] == 'T'));
#endif

    if (!g_log_enabled) return;

    g_log_fp = dm_fopen_utf8("ludo.log", "a");
    if (!g_log_fp) { g_log_enabled = 0; return; }
    /* Header line so runs are separated */
    fprintf(g_log_fp, "\n===== dm session start =====\n");
    fflush(g_log_fp);
}

void dm_log(const char *fmt, ...) {
    if (!g_log_fp) return;
    if (!g_log_mutex_init) return;

    ludo_mutex_lock(&g_log_mutex);
    /* Timestamp */
    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    char ts[32];
    strftime(ts, sizeof(ts), "%H:%M:%S", tm_info);
    fprintf(g_log_fp, "[%s] ", ts);

    va_list ap;
    va_start(ap, fmt);
    vfprintf(g_log_fp, fmt, ap);
    va_end(ap);
    fprintf(g_log_fp, "\n");
    fflush(g_log_fp);
    ludo_mutex_unlock(&g_log_mutex);
}

void dm_log_close(void) {
    if (!g_log_fp) return;
    if (!g_log_mutex_init) return;

    ludo_mutex_lock(&g_log_mutex);
    fprintf(g_log_fp, "===== dm session end =====\n");
    fclose(g_log_fp);
    g_log_fp = NULL;
    ludo_mutex_unlock(&g_log_mutex);
}

/* ------------------------------------------------------------------ */
/* Internal types                                                       */
/* ------------------------------------------------------------------ */

typedef struct HeaderCtx HeaderCtx;

/* WriteCtx extended: includes offset already on disk before this request */
typedef struct {
    FILE  *fp;
    int    download_id;
    Download *download;
    int   *pause_flag;
    int   *cancel_flag;
    int64_t resume_offset;  
    time_t  last_ui_tick;   
    CURL   *curl;                   /* ADDED: For speed optimization */
    size_t  bytes_since_last_flush; /* ADDED: Flush tracker */
    HeaderCtx *header_ctx;
    const char *path;
    int     resume_checked;
} WriteCtx;

/* curl debug callback — routes libcurl verbose output to dm_log
 * (avoids writing to stderr which is NULL in a WIN32/no-console app) */
#ifdef DEBUG
static void dm_log_header_block(const char *prefix, const char *data, size_t size)
{
    size_t start = 0;

    while (start < size) {
        size_t end = start;
        while (end < size && data[end] != '\r' && data[end] != '\n') end++;
        if (end > start) {
            dm_log("[%s] %.*s", prefix, (int)(end - start), data + start);
        }
        while (end < size && (data[end] == '\r' || data[end] == '\n')) end++;
        start = end;
    }
}

static int curl_debug_cb(CURL *handle, curl_infotype type,
                         char *data, size_t size, void *userp)
{
    (void)handle; (void)userp;

    if (type == CURLINFO_HEADER_OUT) {
        dm_log_header_block("curl request header", data, size);
        return 0;
    }

    if (type != CURLINFO_TEXT) return 0;

    /* data is NOT null-terminated; copy and trim trailing newline */
    char buf[256];
    size_t n = size < sizeof(buf) - 1 ? size : sizeof(buf) - 1;
    memcpy(buf, data, n);
    while (n > 0 && (buf[n-1] == '\n' || buf[n-1] == '\r')) n--;
    buf[n] = '\0';
    if (n > 0) dm_log("[curl] %s", buf);
    return 0;
}
#endif

/* Context for the response-header callback */
struct HeaderCtx {
    char filename[512];   /* set if Content-Disposition provides one */
    int  has_filename;
    int  download_id;     /* filled by perform_download so header_cb can dispatch */
    char raw_headers[8192];
    size_t raw_headers_len;
    int64_t content_length; /* parsed or obtained from HEAD */
    long response_code;   /* parsed from the HTTP status line */
};

/* Forward declare gui_dispatch_update so header_cb may call it before its
   static definition later in this file. */
static void gui_dispatch_update(const ProgressUpdate *update);
static void header_ctx_append_line(HeaderCtx *hctx, const char *line);
static void decode_filename_component(const char *src, char *dst, size_t dst_sz);
static void header_ctx_set_filename(HeaderCtx *hctx, const char *src, size_t len);
/* curl header callback — extracts filename from Content-Disposition */
static size_t header_cb(char *buf, size_t size, size_t nitems, void *userdata)
{
    size_t total = size * nitems;
    HeaderCtx *hctx = (HeaderCtx *)userdata;

    /* Work on a null-terminated copy so we can use str functions safely */
    if (total >= 4096) return total;   /* ignore absurdly long headers */
    char line[4096];
    memcpy(line, buf, total);
    line[total] = '\0';
    /* Strip trailing \r\n */
    for (size_t i = total; i > 0; i--) {
        if (line[i - 1] == '\r' || line[i - 1] == '\n') line[i - 1] = '\0';
        else break;
    }

    header_ctx_append_line(hctx, line);
#ifdef DEBUG
    dm_log("[curl response header] %s", line);
#endif

    if (strncmp(line, "HTTP/", 5) == 0) {
        long code = 0;
        if (sscanf(line, "HTTP/%*s %ld", &code) == 1) {
            hctx->response_code = code;
        }
    }

    /* Content-Disposition: ...; filename="foo.zip" or filename=foo.zip
     * Skip RFC 5987 extended notation (filename*=) — only handle plain filename= */
    {
        char *cd = NULL;
        if ((cd = strstr(line, "Content-Disposition:")) != NULL ||
            (cd = strstr(line, "content-disposition:")) != NULL) {

            /* Search for plain 'filename=' but NOT 'filename*=' */
            char *search = cd;
            char *fn = NULL;
            while ((fn = strstr(search, "filename=")) != NULL) {
                /* Make sure the char before '=' is not '*' (RFC 5987 extended) */
                if (fn > search && *(fn - 1) == '*') {
                    search = fn + 9;
#ifdef DEBUG
                    dm_log("[header_cb] skipping filename*= (RFC 5987 extended)");
#endif
                    continue;
                }
                break;
            }

            if (fn) {
                fn += 9; /* skip "filename=" */
#ifdef DEBUG
                dm_log("[header_cb] filename= value starts with: %.80s", fn);
#endif
                /* Handle quoted value */
                if (*fn == '"') {
                    fn++;
                    char *end = strchr(fn, '"');
                    if (end) {
                        header_ctx_set_filename(hctx, fn, (size_t)(end - fn));
                    }
                } else {
                    /* unquoted value — ends at whitespace or ; */
                    size_t len = strcspn(fn, " ;\r\n");
                    if (len > 0) {
                        header_ctx_set_filename(hctx, fn, len);
                    }
                }
            }
        }
    }

    return total;
}

/* Data packet posted to the GUI thread via uiQueueMain */
typedef struct {
    ProgressUpdate      update;
    progress_callback_t cb;
    void               *user_data;
} GuiUpdatePkt;

/* ------------------------------------------------------------------ */
/* Module-level state                                                   */
/* ------------------------------------------------------------------ */

static struct {
    Download           *list;          /* singly-linked list of downloads */
    int                 next_id;
    ludo_mutex_t        list_mutex;

    TaskQueue           queue;         /* URL tasks from GUI         */
    ludo_thread_t      *workers;
    int                 num_workers;

    progress_callback_t progress_cb;
    void               *progress_cb_data;

    char                output_dir[1024];

    int                 initialized;
    int                 running;
    int                 shutting_down;
    int                 shutdown_complete;
} g_mgr;

/* ------------------------------------------------------------------ */
/* Helpers                                                              */
/* ------------------------------------------------------------------ */

/* Called on the GUI/main thread by uiQueueMain */
static void gui_update_on_main(void *data) {
    GuiUpdatePkt *p = (GuiUpdatePkt *)data;
    p->cb(&p->update, p->user_data);
    free(p);
}

static void gui_dispatch_update(const ProgressUpdate *update) {
    if (!g_mgr.progress_cb) return;

    GuiUpdatePkt *pkt = (GuiUpdatePkt *)malloc(sizeof(GuiUpdatePkt));
    if (!pkt) return;
    pkt->update    = *update;
    pkt->cb        = g_mgr.progress_cb;
    pkt->user_data = g_mgr.progress_cb_data;

    uiQueueMain(gui_update_on_main, pkt);
}

static void header_ctx_append_line(HeaderCtx *hctx, const char *line) {
    size_t len;
    size_t copy_len;

    if (!hctx || !line || line[0] == '\0') return;

    len = strlen(line);
    if (hctx->raw_headers_len >= sizeof(hctx->raw_headers) - 1) return;

    copy_len = len;
    if (hctx->raw_headers_len + copy_len + 1 >= sizeof(hctx->raw_headers)) {
        copy_len = (sizeof(hctx->raw_headers) - 1) - hctx->raw_headers_len;
    }
    if (copy_len > 0) {
        memcpy(hctx->raw_headers + hctx->raw_headers_len, line, copy_len);
        hctx->raw_headers_len += copy_len;
    }
    if (hctx->raw_headers_len < sizeof(hctx->raw_headers) - 1) {
        hctx->raw_headers[hctx->raw_headers_len++] = '\n';
    }
    hctx->raw_headers[hctx->raw_headers_len] = '\0';
}

static int dm_hex_value(int ch) {
    if (ch >= '0' && ch <= '9') return ch - '0';
    ch = tolower((unsigned char)ch);
    if (ch >= 'a' && ch <= 'f') return 10 + (ch - 'a');
    return -1;
}

static void decode_filename_component(const char *src, char *dst, size_t dst_sz) {
    size_t pos = 0;

    if (!dst || dst_sz == 0) return;
    dst[0] = '\0';
    if (!src) return;

    while (*src != '\0' && pos + 1 < dst_sz) {
        unsigned char ch = (unsigned char)*src++;
        if (ch == '%' && src[0] != '\0' && src[1] != '\0') {
            int hi = dm_hex_value((unsigned char)src[0]);
            int lo = dm_hex_value((unsigned char)src[1]);
            if (hi >= 0 && lo >= 0) {
                ch = (unsigned char)((hi << 4) | lo);
                src += 2;
            }
        }

        if (ch < 32 || ch == '/' || ch == '\\' || ch == ':' || ch == '*' ||
            ch == '?' || ch == '"' || ch == '<' || ch == '>' || ch == '|') {
            ch = '_';
        }

        dst[pos++] = (char)ch;
    }

    dst[pos] = '\0';
    if (dst[0] == '\0') {
        strncpy(dst, "download", dst_sz - 1);
        dst[dst_sz - 1] = '\0';
    }
}

static void header_ctx_set_filename(HeaderCtx *hctx, const char *src, size_t len) {
    char raw[512];
    char decoded[512];
    ProgressUpdate upd;

    if (!hctx || !src || len == 0 || hctx->has_filename) return;

    if (len >= sizeof(raw)) len = sizeof(raw) - 1;
    memcpy(raw, src, len);
    raw[len] = '\0';
    decode_filename_component(raw, decoded, sizeof(decoded));

    strncpy(hctx->filename, decoded, sizeof(hctx->filename) - 1);
    hctx->filename[sizeof(hctx->filename) - 1] = '\0';
    hctx->has_filename = 1;

#ifdef DEBUG
    dm_log("[header_cb] parsed filename: raw='%s' decoded='%s'", raw, hctx->filename);
#endif

    memset(&upd, 0, sizeof(upd));
    upd.status.id = hctx->download_id;
    strncpy(upd.status.filename, hctx->filename, sizeof(upd.status.filename) - 1);
    upd.status.filename[sizeof(upd.status.filename) - 1] = '\0';
    upd.status.state = DOWNLOAD_STATE_RUNNING;
    gui_dispatch_update(&upd);
}

/* Derive filename from URL (last path segment) */
static void filename_from_url(const char *url, char *out, size_t out_sz) {
    char encoded[1024];
    const char *last_slash = strrchr(url, '/');
    const char *start = last_slash ? last_slash + 1 : url;
    /* strip query string */
    const char *q = strpbrk(start, "?#");
    size_t len = q ? (size_t)(q - start) : strlen(start);
    if (len == 0) {
        strncpy(out, "download", out_sz - 1);
        out[out_sz - 1] = '\0';
        return;
    }
    if (len >= sizeof(encoded)) len = sizeof(encoded) - 1;
    memcpy(encoded, start, len);
    encoded[len] = '\0';
    decode_filename_component(encoded, out, out_sz);
}

static void build_download_path(const Download *d, char *path, size_t path_sz) {
    const char *sep = "";
    size_t dlen = strlen(d->output_dir);
    if (dlen > 0) {
        char last = d->output_dir[dlen - 1];
        if (last != '/' && last != '\\') sep = "/";
    }
    snprintf(path, path_sz, "%s%s%s", d->output_dir, sep, d->status.filename);
}

static void probe_download_head(const char *url, HeaderCtx *hctx) {
    const LudoConfig *cfg = ludo_config_get();
    CURL *head;
    curl_off_t cl = 0;

    if (!url || !hctx) return;

    head = curl_easy_init();
    if (!head) return;

    curl_easy_setopt(head, CURLOPT_URL, url);
    curl_easy_setopt(head, CURLOPT_NOBODY, 1L);
    curl_easy_setopt(head, CURLOPT_HEADERFUNCTION, header_cb);
    curl_easy_setopt(head, CURLOPT_HEADERDATA, hctx);
    curl_easy_setopt(head, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(head, CURLOPT_MAXREDIRS, (long)(cfg ? cfg->max_redirect : 10));
    curl_easy_setopt(head, CURLOPT_USERAGENT,
                     "Mozilla/5.0 (Windows NT 10.0; Win64; x64)");
#ifdef DEBUG
    curl_easy_setopt(head, CURLOPT_VERBOSE, 1L);
    curl_easy_setopt(head, CURLOPT_DEBUGFUNCTION, curl_debug_cb);
    curl_easy_setopt(head, CURLOPT_DEBUGDATA, NULL);
#endif
    curl_easy_perform(head);
    if (curl_easy_getinfo(head, CURLINFO_CONTENT_LENGTH_DOWNLOAD_T, &cl) == CURLE_OK && cl > 0) {
        hctx->content_length = (int64_t)cl;
    }
    curl_easy_cleanup(head);
}

static int download_should_retry(CURLcode res, long http_code) {
    if (res == CURLE_OPERATION_TIMEDOUT ||
        res == CURLE_COULDNT_CONNECT ||
        res == CURLE_COULDNT_RESOLVE_HOST ||
        res == CURLE_GOT_NOTHING ||
        res == CURLE_RECV_ERROR ||
        res == CURLE_SEND_ERROR ||
        res == CURLE_PARTIAL_FILE) {
        return 1;
    }

    if (res == CURLE_OK) {
        if (http_code == 408 || http_code == 409 || http_code == 425 ||
            http_code == 429 || (http_code >= 500 && http_code <= 599)) {
            return 1;
        }
    }

    return 0;
}

#ifdef _WIN32
static wchar_t *dm_utf8_to_wide_dup(const char *src) {
    int needed;
    wchar_t *dst;

    if (!src) return NULL;
    needed = MultiByteToWideChar(CP_UTF8, 0, src, -1, NULL, 0);
    if (needed <= 0) return NULL;
    dst = (wchar_t *)malloc((size_t)needed * sizeof(wchar_t));
    if (!dst) return NULL;
    if (MultiByteToWideChar(CP_UTF8, 0, src, -1, dst, needed) <= 0) {
        free(dst);
        return NULL;
    }
    return dst;
}

static const wchar_t *dm_wmode(const char *mode) {
    if (strcmp(mode, "rb") == 0) return L"rb";
    if (strcmp(mode, "wb") == 0) return L"wb";
    if (strcmp(mode, "ab") == 0) return L"ab";
    if (strcmp(mode, "r") == 0) return L"r";
    if (strcmp(mode, "w") == 0) return L"w";
    if (strcmp(mode, "a") == 0) return L"a";
    return NULL;
}

static FILE *dm_fopen_utf8(const char *path, const char *mode) {
    FILE *fp = NULL;
    wchar_t *wpath = dm_utf8_to_wide_dup(path);
    const wchar_t *wmode = dm_wmode(mode);

    if (!wpath || !wmode) {
        free(wpath);
        return NULL;
    }
    fp = _wfopen(wpath, wmode);
    free(wpath);
    return fp;
}

static int dm_stat_utf8(const char *path, struct _stat64 *st) {
    int rv;
    wchar_t *wpath = dm_utf8_to_wide_dup(path);

    if (!wpath) return -1;
    rv = _wstat64(wpath, st);
    free(wpath);
    return rv;
}

static int dm_rename_utf8(const char *old_path, const char *new_path) {
    int rv;
    wchar_t *wold = dm_utf8_to_wide_dup(old_path);
    wchar_t *wnew = dm_utf8_to_wide_dup(new_path);

    if (!wold || !wnew) {
        free(wold);
        free(wnew);
        return -1;
    }
    rv = MoveFileExW(wold, wnew, MOVEFILE_REPLACE_EXISTING);
    free(wold);
    free(wnew);
    return rv;
}

static int dm_remove_utf8(const char *path) {
    int rv;
    wchar_t *wpath = dm_utf8_to_wide_dup(path);

    if (!wpath) return -1;
    rv = _wremove(wpath);
    free(wpath);
    return rv;
}
#else
static FILE *dm_fopen_utf8(const char *path, const char *mode) {
    return fopen(path, mode);
}

static int dm_stat_utf8(const char *path, struct stat *st) {
    return stat(path, st);
}

static int dm_rename_utf8(const char *old_path, const char *new_path) {
    return rename(old_path, new_path);
}

static int dm_remove_utf8(const char *path) {
    return remove(path);
}
#endif

static void sync_download_size_from_disk(Download *d) {
    if (!d || d->status.filename[0] == '\0') return;

    char path[2048];
#ifdef _WIN32
    struct _stat64 st;
#else
    struct stat st;
#endif

    build_download_path(d, path, sizeof(path));
    if (dm_stat_utf8(path, &st) != 0) return;

    d->status.downloaded_bytes = (int64_t)st.st_size;
    if (d->status.total_bytes > 0) {
        d->status.progress = (100.0 * (double)st.st_size) / (double)d->status.total_bytes;
        if (d->status.progress < 0.0) d->status.progress = 0.0;
        if (d->status.progress > 100.0) d->status.progress = 100.0;
    }
}

/* curl progress callback (CURLOPT_XFERINFOFUNCTION) */
static int xfer_info_cb(void *clientp, curl_off_t dltotal, curl_off_t dlnow,
                        curl_off_t ultotal, curl_off_t ulnow)
{
    (void)ultotal; (void)ulnow;
    WriteCtx *ctx = (WriteCtx *)clientp;

    /* Check cancellation */
    if (*ctx->cancel_flag) return 1; /* aborts transfer */
    if (ctx->download && ctx->download->stop_requested) return 1;

    /* Throttle GUI updates to once per second to avoid flooding uiQueueMain */
    time_t now = time(NULL);
    if (now == ctx->last_ui_tick) return 0;
    time_t prev_tick = ctx->last_ui_tick;
    ctx->last_ui_tick = now;

    ludo_mutex_lock(&g_mgr.list_mutex);
    Download *d = NULL;
    for (Download *it = g_mgr.list; it; it = it->next) {
        if (it->status.id == ctx->download_id) { d = it; break; }
    }
    if (d) {
        /* Honor requested pause set via download_manager_pause() */
        if (d->status.state == DOWNLOAD_STATE_PAUSED) {
            ludo_mutex_unlock(&g_mgr.list_mutex);
            return 1; /* Returning non-zero aborts the libcurl transfer gracefully */
        }
        /* dlnow/dltotal are relative to this request (post-resume);
           add the already-downloaded offset to get absolute counts. */
        int64_t abs_now   = (int64_t)dlnow   + ctx->resume_offset;
        int64_t abs_total = (dltotal > 0)
                              ? (int64_t)dltotal + ctx->resume_offset
                              : d->status.total_bytes;
        d->status.downloaded_bytes = abs_now;
        if (abs_total > 0) d->status.total_bytes = abs_total;
        d->status.progress = (d->status.total_bytes > 0)
                        ? (100.0 * (double)abs_now / (double)d->status.total_bytes)
                        : 0.0;
        /* Only mark RUNNING if not paused (checked above) */
        d->status.state = DOWNLOAD_STATE_RUNNING;

        /* Compute instantaneous speed (bytes/sec) via libcurl's moving average */
        curl_off_t curl_speed = 0;
        if (curl_easy_getinfo(ctx->curl, CURLINFO_SPEED_DOWNLOAD_T, &curl_speed) == CURLE_OK) {
            d->status.speed_bps = (double)curl_speed;
        } else {
            d->status.speed_bps = 0.0;
        }
    }
    ProgressUpdate upd = {0};
    if (d) {
        upd.status = d->status;  // copy under lock
    }
    int has_update = (d != NULL);
    ludo_mutex_unlock(&g_mgr.list_mutex);

    if (has_update) {
        gui_dispatch_update(&upd);
    }

    return 0;
}

/* curl write callback — streams bytes straight to disk */
static size_t write_cb(char *ptr, size_t size, size_t nmemb, void *userdata) {
    WriteCtx *ctx = (WriteCtx *)userdata;
    if (*ctx->cancel_flag) {
        dm_log("[write_cb] id=%d cancelled, aborting", ctx->download_id);
        return 0;
    }
    if (ctx->download && ctx->download->stop_requested) {
        dm_log("[write_cb] id=%d stop requested, aborting", ctx->download_id);
        return 0;
    }
    if (!ctx->fp) {
        dm_log("[write_cb] id=%d fp is NULL!", ctx->download_id);
        return 0;
    }

    if (ctx->resume_offset > 0 && !ctx->resume_checked) {
        long response_code = ctx->header_ctx ? ctx->header_ctx->response_code : 0;
        if (response_code == 200) {
            dm_log("[write_cb] id=%d server ignored resume request; restarting from byte 0", ctx->download_id);
            fclose(ctx->fp);
            ctx->fp = dm_fopen_utf8(ctx->path, "wb");
            if (!ctx->fp) {
                dm_log("[write_cb] id=%d reopen failed for fresh download: %s (errno=%d)",
                       ctx->download_id, ctx->path, errno);
                return 0;
            }
            ctx->resume_offset = 0;
        } else if (response_code != 206) {
            dm_log("[write_cb] id=%d unexpected resume response code %ld; aborting to avoid corruption",
                   ctx->download_id, response_code);
            return 0;
        }
        ctx->resume_checked = 1;
    }
    
    size_t written = fwrite(ptr, size, nmemb, ctx->fp);
    size_t bytes_written = written * size;
    
    /* BEST PRACTICE: Periodic Flushing */
    ctx->bytes_since_last_flush += bytes_written;
    if (ctx->bytes_since_last_flush >= 64 * 1024) {
        fflush(ctx->fp);
        ctx->bytes_since_last_flush = 0;
    }
    
    if (written != nmemb) {
        dm_log("[write_cb] id=%d fwrite partial: expected %zu, wrote %zu",
               ctx->download_id, nmemb, written);
    }
    return written;
}

static void perform_download(Download *d) {
    const LudoConfig *cfg = ludo_config_get();
    const char *sep = "";
    size_t dlen;
    HeaderCtx hctx;
    char path[2048];
    CURLcode res = CURLE_OK;
    long http_code = 0;
    int transfer_success = 0;
    int should_skip;
    int max_attempts = cfg ? (cfg->max_download_retry + 1) : 1;

    dm_log("[perform_download] id=%d url=%s", d->status.id, d->url);
    dm_log("[perform_download] output_dir=%s  filename=%s", d->output_dir, d->status.filename);

    ludo_mutex_lock(&g_mgr.list_mutex);
    should_skip = g_mgr.shutting_down || d->stop_requested || d->status.state == DOWNLOAD_STATE_PAUSED;
    ludo_mutex_unlock(&g_mgr.list_mutex);
    if (should_skip) return;

    dlen = strlen(d->output_dir);
    if (dlen > 0) {
        char last = d->output_dir[dlen - 1];
        if (last != '/' && last != '\\') sep = "/";
    }

    memset(&hctx, 0, sizeof(hctx));
    hctx.download_id = d->status.id;
    if (d->has_preflight) {
        hctx.response_code = d->preflight_status_code;
        hctx.content_length = d->preflight_content_length;
        if (d->preflight_filename[0] != '\0') {
            strncpy(hctx.filename, d->preflight_filename, sizeof(hctx.filename) - 1);
            hctx.filename[sizeof(hctx.filename) - 1] = '\0';
            hctx.has_filename = 1;
        }
    } else {
        probe_download_head(d->url, &hctx);
    }

    if (hctx.has_filename) {
        ludo_mutex_lock(&g_mgr.list_mutex);
        strncpy(d->status.filename, hctx.filename, sizeof(d->status.filename) - 1);
        d->status.filename[sizeof(d->status.filename) - 1] = '\0';
        ludo_mutex_unlock(&g_mgr.list_mutex);
    }

    snprintf(path, sizeof(path), "%s%s%s", d->output_dir, sep, d->status.filename);
    http_code = hctx.response_code;

    /* [OPTIMIZATION 1]: Initialize the CURL handle OUTSIDE the retry loop 
     * to enable DNS caching and TLS/TCP connection reuse. */
    CURL *curl = curl_easy_init();
    if (!curl) {
        ProgressUpdate upd = {0};
        upd.status.id = d->status.id;
        upd.status.state = DOWNLOAD_STATE_FAILED;
        strncpy(upd.status.filename, d->status.filename, sizeof(upd.status.filename) - 1);
        snprintf(upd.error_msg, sizeof(upd.error_msg), "Failed to initialize curl handle");
        ludo_mutex_lock(&g_mgr.list_mutex);
        d->status.state = DOWNLOAD_STATE_FAILED;
        ludo_mutex_unlock(&g_mgr.list_mutex);
        gui_dispatch_update(&upd);
        return;
    }

    for (int attempt = 0; attempt < max_attempts; attempt++) {
        curl_off_t resume_from = 0;
        FILE *fp = NULL;
        int pause_flag = 0;
        int cancel_flag = 0;
        HeaderCtx hctx_local;
        WriteCtx ctx;

        memset(&hctx_local, 0, sizeof(hctx_local));
        memcpy(&hctx_local, &hctx, sizeof(hctx_local));
        hctx_local.download_id = d->status.id;

        {
#ifdef _WIN32
            struct _stat64 st;
#else
            struct stat st;
#endif
            if (dm_stat_utf8(path, &st) == 0 && st.st_size > 0) {
                if (hctx.content_length > 0 && (int64_t)st.st_size == hctx.content_length) {
                    ProgressUpdate upd = {0};
                    ludo_mutex_lock(&g_mgr.list_mutex);
                    d->status.downloaded_bytes = (int64_t)st.st_size;
                    d->status.total_bytes = (int64_t)st.st_size;
                    d->status.state = DOWNLOAD_STATE_COMPLETED;
                    d->status.progress = 100.0;
                    ludo_mutex_unlock(&g_mgr.list_mutex);
                    upd.status.id = d->status.id;
                    upd.status.state = DOWNLOAD_STATE_COMPLETED;
                    upd.status.progress = 100.0;
                    upd.status.downloaded_bytes = d->status.downloaded_bytes;
                    upd.status.total_bytes = d->status.total_bytes;
                    strncpy(upd.status.filename, d->status.filename, sizeof(upd.status.filename) - 1);
                    gui_dispatch_update(&upd);
                    
                    curl_easy_cleanup(curl); /* Cleanup on early exit */
                    return;
                }
                resume_from = (curl_off_t)st.st_size;
            }
        }

        fp = dm_fopen_utf8(path, resume_from > 0 ? "ab" : "wb");
        if (!fp) {
            ProgressUpdate upd = {0};
            upd.status.id = d->status.id;
            upd.status.state = DOWNLOAD_STATE_FAILED;
            snprintf(upd.error_msg, sizeof(upd.error_msg), "Cannot open output file: %s", path);
            ludo_mutex_lock(&g_mgr.list_mutex);
            d->status.state = DOWNLOAD_STATE_FAILED;
            ludo_mutex_unlock(&g_mgr.list_mutex);
            gui_dispatch_update(&upd);
            
            curl_easy_cleanup(curl); /* Cleanup on early exit */
            return;
        }

        ctx.fp = fp;
        ctx.download_id = d->status.id;
        ctx.download = d;
        ctx.pause_flag = &pause_flag;
        ctx.cancel_flag = &cancel_flag;
        ctx.resume_offset = (int64_t)resume_from;
        ctx.last_ui_tick = 0;
        ctx.curl = curl;
        ctx.bytes_since_last_flush = 0;
        ctx.header_ctx = &hctx_local;
        ctx.path = path;
        ctx.resume_checked = 0;

        ludo_mutex_lock(&g_mgr.list_mutex);
        d->curl_handle = (void *)curl;
        d->fp = fp;
        d->stop_requested = 0;
        d->status.start_time = time(NULL);
        ludo_mutex_unlock(&g_mgr.list_mutex);

        /* [OPTIMIZATION 2]: Reset options to ensure a clean slate for the retry 
         * without terminating the underlying pooled connections. */
        curl_easy_reset(curl);

        curl_easy_setopt(curl, CURLOPT_URL, d->url);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_cb);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &ctx);
        curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, xfer_info_cb);
        curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &ctx);
        curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(curl, CURLOPT_MAXREDIRS, (long)(cfg ? cfg->max_redirect : 10));
        curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, "gzip, deflate, zstd, br");
        curl_easy_setopt(curl, CURLOPT_USERAGENT,
                         "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/146.0.0.0 Safari/537.36 Edg/146.0.0.0");
        curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, header_cb);
        curl_easy_setopt(curl, CURLOPT_HEADERDATA, &hctx_local);
#ifdef DEBUG
        curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);
        curl_easy_setopt(curl, CURLOPT_DEBUGFUNCTION, curl_debug_cb);
        curl_easy_setopt(curl, CURLOPT_DEBUGDATA, NULL);
#endif
        
        if (resume_from > 0) {
            curl_easy_setopt(curl, CURLOPT_RESUME_FROM_LARGE, resume_from);
        } else if (strstr(d->url, "videoplayback")) {
            /* Some servers (e.g. YouTube) require a Range header even for fresh downloads of certain URLs */
            curl_easy_setopt(curl, CURLOPT_RANGE, "0-");
        }

        res = curl_easy_perform(curl);
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);

        fclose(fp);
        
        ludo_mutex_lock(&g_mgr.list_mutex);
        d->curl_handle = NULL;
        d->fp = NULL;
        ludo_mutex_unlock(&g_mgr.list_mutex);

        if (res == CURLE_OK && http_code < 400) {
            transfer_success = 1;
            if (hctx_local.has_filename) {
                ludo_mutex_lock(&g_mgr.list_mutex);
                strncpy(d->status.filename, hctx_local.filename, sizeof(d->status.filename) - 1);
                d->status.filename[sizeof(d->status.filename) - 1] = '\0';
                ludo_mutex_unlock(&g_mgr.list_mutex);
            }
            break;
        }

        ludo_mutex_lock(&g_mgr.list_mutex);
        should_skip = g_mgr.shutting_down || d->stop_requested || d->status.state == DOWNLOAD_STATE_PAUSED;
        ludo_mutex_unlock(&g_mgr.list_mutex);
        
        if (should_skip) break;
        if (!download_should_retry(res, http_code) || attempt + 1 >= max_attempts) break;
    }

    /* [OPTIMIZATION 3]: Cleanup the handle once the entire transfer process is resolved */
    curl_easy_cleanup(curl);

    if (transfer_success) {
        char final_path[2048];
#ifdef _WIN32
        struct _stat64 st;
#else
        struct stat st;
#endif
        snprintf(final_path, sizeof(final_path), "%s%s%s", d->output_dir, sep, d->status.filename);
        if (dm_stat_utf8(final_path, &st) == 0) {
            ludo_mutex_lock(&g_mgr.list_mutex);
            d->status.downloaded_bytes = (int64_t)st.st_size;
            d->status.total_bytes = (int64_t)st.st_size;
            ludo_mutex_unlock(&g_mgr.list_mutex);
        }
    }

    ludo_mutex_lock(&g_mgr.list_mutex);
    d->status.end_time = time(NULL);
    if (transfer_success) {
        d->status.state = DOWNLOAD_STATE_COMPLETED;
        d->status.progress = 100.0;
    } else if (d->status.state != DOWNLOAD_STATE_QUEUED && d->status.state != DOWNLOAD_STATE_PAUSED) {
        d->status.state = DOWNLOAD_STATE_FAILED;
    }
    if (d->status.state != DOWNLOAD_STATE_PAUSED) d->stop_requested = 0;

    {
        int is_deleted = d->marked_for_removal;
        int final_id = d->status.id;
        DownloadState final_state = d->status.state;
        time_t start_ts = d->status.start_time;
        time_t finish_ts = d->status.end_time;
        double final_progress = d->status.progress;
        int64_t final_downloaded_bytes = d->status.downloaded_bytes;
        int64_t final_total_bytes = d->status.total_bytes;
        double final_speed_bps = d->status.speed_bps;
        char final_filename[sizeof(d->status.filename)];
        ProgressUpdate upd = {0};

        if (is_deleted) {
            Download **prev = &g_mgr.list;
            while (*prev) {
                if (*prev == d) {
                    *prev = d->next;
                    break;
                }
                prev = &(*prev)->next;
            }
        }

        strncpy(final_filename, d->status.filename, sizeof(final_filename) - 1);
        final_filename[sizeof(final_filename) - 1] = '\0';
        ludo_mutex_unlock(&g_mgr.list_mutex);

        if (is_deleted) {
            free(d);

            /* Drive GUI row deletion via the progress callback so the GUI
             * never has to race against the worker when deciding whether
             * the row is still live.  All fields were captured above while
             * the mutex was held, so this is safe after free(d). */
            upd.status.id               = final_id;
            upd.status.state            = DOWNLOAD_STATE_FAILED; /* cancelled = failed */
            upd.status.progress         = final_progress;
            upd.status.start_time       = start_ts;
            upd.status.end_time         = finish_ts;
            upd.status.downloaded_bytes = final_downloaded_bytes;
            upd.status.total_bytes      = final_total_bytes;
            upd.status.speed_bps        = 0.0;
            strncpy(upd.status.filename, final_filename, sizeof(upd.status.filename) - 1);
            upd.marked_for_removal      = 1;
            gui_dispatch_update(&upd);
            return;
        }

        upd.status.id = final_id;
        upd.status.state = final_state;
        upd.status.progress = (final_state == DOWNLOAD_STATE_COMPLETED) ? 100.0 : final_progress;
        upd.status.start_time = start_ts;
        upd.status.end_time = finish_ts;
        upd.status.downloaded_bytes = final_downloaded_bytes;
        upd.status.total_bytes = final_total_bytes;
        upd.status.speed_bps = final_speed_bps;
        strncpy(upd.status.filename, final_filename, sizeof(upd.status.filename) - 1);
        
        if (!transfer_success) {
            if (res != CURLE_OK) {
                strncpy(upd.error_msg, curl_easy_strerror(res), sizeof(upd.error_msg) - 1);
            } else if (http_code >= 400) {
                snprintf(upd.error_msg, sizeof(upd.error_msg), "HTTP %ld", http_code);
            }
        }
        gui_dispatch_update(&upd);
    }
}

/* ------------------------------------------------------------------ */
/* Persistence — dl_items.db + dl_history.gz                           */
/* ------------------------------------------------------------------ */

/*
 * File format: one record per line, fields separated by TAB.
 * Fields: id  url  output_dir  filename  state  progress  speed_bps
 *         total_bytes  downloaded_bytes  start_time  end_time
 */
/* Helper: copy a field while replacing tabs/CR/LF with spaces so the
   on-disk TAB-separated format remains stable. Buffers are sized to
   match the in-memory field sizes. */
static void sanitize_field(const char *src, char *dst, size_t dst_sz) {
    size_t pos = 0;
    if (!dst || dst_sz == 0) return;
    dst[0] = '\0';
    if (!src) return;
    while (*src != '\0' && pos + 1 < dst_sz) {
        char c = *src++;
        if (c == '\t' || c == '\r' || c == '\n') c = ' ';
        dst[pos++] = c;
    }
    dst[pos] = '\0';
}

static void db_save_and_archive(void) {
    /* Check if the existing DB has crossed the archive threshold */
#ifdef _WIN32
    struct _stat64 st;
#else
    struct stat st;
#endif
     /* Only trigger archiving when the existing DB exceeds the size threshold.
         Previously this was replaced by an existence-only check which prevented
         the intended size-based archival behavior. */
     int trigger_archive = (dm_stat_utf8(DB_PATH, &st) == 0 && st.st_size >= DB_MAX_BYTES);

     /* Write to a temporary file first for atomic safety against crashes.
         Use text mode so newline semantics are consistent with db_load on
         Windows (CRLF normalization when writing/reading in text mode). */
     FILE *f_db = dm_fopen_utf8(DB_PATH ".tmp", "w");
    if (!f_db) return;

    gzFile f_gz = NULL;
    if (trigger_archive) {
        f_gz = gzopen(ARCHIVE_PATH, "ab");
        dm_log("[shutdown] DB size > 10KB. Moving completed tasks to archive...");
    }


    ludo_mutex_lock(&g_mgr.list_mutex);
    for (Download *d = g_mgr.list; d; d = d->next) {
        /* Determine if the task is finished */
        int is_finished = (d->status.state == DOWNLOAD_STATE_COMPLETED);

        /* TASK 1: Update size from disk for active/paused items before saving */
        if (!is_finished) sync_download_size_from_disk(d);

        char s_url[4096];
        char s_output_dir[1024];
        char s_filename[512];
        char s_original_url[4096];
        int has_distinct_original;
        /* Prefer the preserved user-supplied original URL for the primary
           DB URL field so redirects do not overwrite what the user entered. */
        const char *url_to_save = (d->original_url[0] != '\0') ? d->original_url : d->url;
        sanitize_field(url_to_save, s_url, sizeof(s_url));
        sanitize_field(d->output_dir, s_output_dir, sizeof(s_output_dir));
        sanitize_field(d->status.filename, s_filename, sizeof(s_filename));
        sanitize_field(d->original_url, s_original_url, sizeof(s_original_url));
        has_distinct_original = (s_original_url[0] != '\0' && strcmp(s_original_url, s_url) != 0);

        if (trigger_archive && f_gz && is_finished) {
            if (has_distinct_original) {
                gzprintf(f_gz, "%d\t%s\t%s\t%s\t%d\t%.2f\t%.2f\t%lld\t%lld\t%lld\t%lld\t%s\n",
                         d->status.id, s_url, s_output_dir, s_filename,
                         (int)d->status.state, d->status.progress, d->status.speed_bps,
                         (long long)d->status.total_bytes, (long long)d->status.downloaded_bytes,
                         (long long)d->status.start_time, (long long)d->status.end_time,
                         s_original_url);
            } else {
                gzprintf(f_gz, "%d\t%s\t%s\t%s\t%d\t%.2f\t%.2f\t%lld\t%lld\t%lld\t%lld\n",
                         d->status.id, s_url, s_output_dir, s_filename,
                         (int)d->status.state, d->status.progress, d->status.speed_bps,
                         (long long)d->status.total_bytes, (long long)d->status.downloaded_bytes,
                         (long long)d->status.start_time, (long long)d->status.end_time);
            }
        } else {
            if (has_distinct_original) {
                fprintf(f_db, "%d\t%s\t%s\t%s\t%d\t%.2f\t%.2f\t%lld\t%lld\t%lld\t%lld\t%s\n",
                        d->status.id, s_url, s_output_dir, s_filename,
                        (int)d->status.state, d->status.progress, d->status.speed_bps,
                        (long long)d->status.total_bytes, (long long)d->status.downloaded_bytes,
                        (long long)d->status.start_time, (long long)d->status.end_time,
                        s_original_url);
            } else {
                fprintf(f_db, "%d\t%s\t%s\t%s\t%d\t%.2f\t%.2f\t%lld\t%lld\t%lld\t%lld\n",
                        d->status.id, s_url, s_output_dir, s_filename,
                        (int)d->status.state, d->status.progress, d->status.speed_bps,
                        (long long)d->status.total_bytes, (long long)d->status.downloaded_bytes,
                        (long long)d->status.start_time, (long long)d->status.end_time);
            }
        }
    }
    ludo_mutex_unlock(&g_mgr.list_mutex);

    fclose(f_db);
    if (f_gz) gzclose(f_gz);

    /* Atomic replace: ensures data isn't corrupted if power is lost during save */
#ifdef _WIN32
    wchar_t *tmp_w = dm_utf8_to_wide_dup(DB_PATH ".tmp");
    wchar_t *db_w  = dm_utf8_to_wide_dup(DB_PATH);
    if (tmp_w && db_w) MoveFileExW(tmp_w, db_w, MOVEFILE_REPLACE_EXISTING);
    free(tmp_w); free(db_w);
#else
    rename(DB_PATH ".tmp", DB_PATH);
#endif
}

static void db_load(const char *path) {
    /* Robust loader that can handle very long lines (long URLs with large
       query strings). Uses a dynamically growing buffer to read one logical
       line at a time. */
    /* Open in text mode so CRLF is normalized on Windows. We still
       defensively strip CR/LF later to be robust on all platforms. */
    FILE *f = dm_fopen_utf8(path, "r");
    if (!f) return;

    char *line = NULL;
    size_t cap = 0;
    while (1) {
        /* Ensure buffer exists */
        if (line == NULL) {
            cap = 8192;
            line = (char *)malloc(cap);
            if (!line) break;
            line[0] = '\0';
        }

        /* Determine how much space remains and read into it */
        size_t len = strlen(line);
        size_t avail = cap - len;
        if (avail < 2) {
            /* Need at least space for one char + NUL */
            size_t newcap = cap * 2;
            char *tmp = (char *)realloc(line, newcap);
            if (!tmp) { free(line); line = NULL; cap = 0; break; }
            line = tmp; cap = newcap; len = strlen(line); avail = cap - len;
        }

        char *res = fgets(line + len, (int)avail, f);
        if (!res) {
            if (feof(f)) {
                if (len == 0) break; /* clean EOF */
                /* EOF after a partial line: process what we have */
            } else {
                /* I/O error: abort parsing */
                free(line); line = NULL; cap = 0; break;
            }
        } else {
            /* If we read data but didn't encounter a newline and the buffer
               was filled, grow and continue reading the logical line. */
            len = strlen(line);
            if (len > 0 && line[len - 1] != '\n' && !feof(f)) {
                /* Buffer filled without newline => enlarge and keep reading */
                size_t newcap = cap * 2;
                char *tmp = (char *)realloc(line, newcap);
                if (!tmp) { free(line); line = NULL; cap = 0; break; }
                line = tmp; cap = newcap;
                continue; /* continue reading into enlarged buffer */
            }
        }

        /* Strip trailing CR/LF. Works in both Windows and Unix */
        line[strcspn(line, "\r\n")] = 0;

        Download *d = (Download *)calloc(1, sizeof(Download));
        if (!d) break;

        char *p = line;
        char *fields[12];
        for (int i = 0; i < 12; i++) {
            fields[i] = p;
            if (p) {
                p = strchr(p, '\t');
                if (p) { *p = '\0'; p++; }
            }
        }

        /* If we didn't find at least the first 11 fields, skip the line */
        if (!fields[10]) {
            free(d);
            line[0] = '\0'; /* reset without realloc */
            continue;
        }

        d->status.id = g_mgr.next_id++; // assign fresh sequential ID
        /* When a legacy row carries both the redirected runtime URL and the
           original URL, restore the original as the canonical URL in memory
           so the next save rewrites the DB with the user-entered value. */
        {
            const char *canonical_url = (fields[11] && fields[11][0] != '\0')
                                          ? fields[11]
                                          : fields[1];
            strncpy(d->url, canonical_url, sizeof(d->url)-1);
            d->url[sizeof(d->url)-1] = '\0';
            strncpy(d->original_url, canonical_url, sizeof(d->original_url)-1);
            d->original_url[sizeof(d->original_url)-1] = '\0';
        }
        strncpy(d->output_dir, fields[2], sizeof(d->output_dir)-1);
        d->output_dir[sizeof(d->output_dir)-1] = '\0';
        strncpy(d->status.filename, fields[3], sizeof(d->status.filename)-1);
        d->status.filename[sizeof(d->status.filename)-1] = '\0';
        d->status.state = (DownloadState)atoi(fields[4]);
        d->status.progress = atof(fields[5]);
        d->status.speed_bps = atof(fields[6]);
        d->status.total_bytes = atoll(fields[7]);
        d->status.downloaded_bytes = atoll(fields[8]);
        d->status.start_time = (time_t)atoll(fields[9]);
        d->status.end_time = (time_t)atoll(fields[10]);

        /* SAFETY: Reset active states so they can be resumed. */
        if (d->status.state == DOWNLOAD_STATE_RUNNING || 
            d->status.state == DOWNLOAD_STATE_QUEUED) {
            
            d->status.state = DOWNLOAD_STATE_QUEUED;
            
            /* TASK 2: Sync size with file on disk if it exists */
            if (d->status.filename[0] != '\0') {
                char local_path[2048];
                const char *sep = "";
                size_t dlen = strlen(d->output_dir);
                if (dlen > 0) {
                    char last = d->output_dir[dlen - 1];
                    if (last != '/' && last != '\\') sep = "/";
                }
                snprintf(local_path, sizeof(local_path), "%s%s%s", d->output_dir, sep, d->status.filename);
#ifdef _WIN32
                struct _stat64 st;
#else
                struct stat st;
#endif
                if (dm_stat_utf8(local_path, &st) == 0) {
                    d->status.downloaded_bytes = (int64_t)st.st_size;
                    if (d->status.total_bytes > 0) {
                        d->status.progress = (100.0 * (double)st.st_size) / (double)d->status.total_bytes;
                    }
                } else {
                    d->status.downloaded_bytes = 0;
                    d->status.progress = 0.0;
                }
            }
        }

        if (d->status.id >= g_mgr.next_id) g_mgr.next_id = d->status.id + 1;

        d->next = g_mgr.list;
        g_mgr.list = d;

        line[0] = '\0'; /* reset without realloc */
    }

    free(line);
    fclose(f);
}

/* ------------------------------------------------------------------ */
/* Worker thread                                                        */
/* ------------------------------------------------------------------ */
static void *worker_thread(void *arg) {
    (void)arg;
    URLTask task;
    while (task_queue_pop(&g_mgr.queue, &task)) {
        Download *d = NULL;
        ludo_mutex_lock(&g_mgr.list_mutex);
        for (Download *it = g_mgr.list; it; it = it->next) {
            if (it->status.id == task.download_id) {
                d = it;
                break;
            }
        }
        ludo_mutex_unlock(&g_mgr.list_mutex);
        if (!d) continue;
        if (task.is_resume_task && d->status.state != DOWNLOAD_STATE_QUEUED) continue;
        perform_download(d);
    }
    return NULL;
}

/* ------------------------------------------------------------------ */
/* Public API                                                           */
/* ------------------------------------------------------------------ */

void download_manager_init(int num_workers, const char *output_dir) {
    const LudoConfig *cfg = ludo_config_get();
    dm_log_init();
    dm_log("[init] num_workers=%d  output_dir=%s",
           num_workers, output_dir ? output_dir : "(null -> ./downloads/)");

    memset(&g_mgr, 0, sizeof(g_mgr));
    g_mgr.initialized  = 1;
    g_mgr.next_id     = 1;
    g_mgr.num_workers = num_workers;
    g_mgr.running     = 1;

    strncpy(g_mgr.output_dir, output_dir ? output_dir : "./downloads/",
            sizeof(g_mgr.output_dir) - 1);

    ludo_mutex_init(&g_mgr.list_mutex);
    task_queue_init(&g_mgr.queue, cfg ? cfg->download_queue_capacity : 256);

    curl_global_init(CURL_GLOBAL_DEFAULT);
    {
        const curl_version_info_data *vi = curl_version_info(CURLVERSION_NOW);
        dm_log("[init] curl encodings: zlib=%s zstd=%s brotli=%s",
               (vi && (vi->features & CURL_VERSION_LIBZ)) ? "on" : "off",
               (vi && (vi->features & CURL_VERSION_ZSTD)) ? "on" : "off",
               (vi && (vi->features & CURL_VERSION_BROTLI)) ? "on" : "off");
    }

    /* Restore previous session */
    dm_log("[init] loading db: %s", DB_PATH);
    db_load(DB_PATH);
    dm_log("[init] db loaded");

    /* At startup, notify the GUI only about tasks that are not completed
       so the UI doesn't re-display finished entries. */
    ludo_mutex_lock(&g_mgr.list_mutex);
    for (Download *d = g_mgr.list; d; d = d->next) {
        if (d->status.state != DOWNLOAD_STATE_COMPLETED) {
            ProgressUpdate upd = {0};
            upd.status = d->status;
            gui_dispatch_update(&upd);
        }
    }
    ludo_mutex_unlock(&g_mgr.list_mutex);

    g_mgr.workers = (ludo_thread_t *)calloc((size_t)num_workers,
                                            sizeof(ludo_thread_t));
    if (!g_mgr.workers) {
        g_mgr.num_workers = 0;
        dm_log("[init] failed to allocate worker handles");
        return;
    }
    for (int i = 0; i < num_workers; i++) {
        if (ludo_thread_create(&g_mgr.workers[i], worker_thread, NULL) != 0) {
            g_mgr.num_workers = i;
            dm_log("[init] failed to start worker %d", i);
            break;
        }
    }
}

void download_manager_prepare_for_shutdown(void) {
    int paused_count = 0;

    if (!g_mgr.initialized) return;

    ludo_mutex_lock(&g_mgr.list_mutex);
    if (g_mgr.shutting_down) {
        ludo_mutex_unlock(&g_mgr.list_mutex);
        return;
    }

    g_mgr.shutting_down = 1;
    for (Download *d = g_mgr.list; d; d = d->next) {
        if (d->status.state == DOWNLOAD_STATE_RUNNING || d->status.state == DOWNLOAD_STATE_QUEUED) {
            d->status.state = DOWNLOAD_STATE_PAUSED;
            d->status.speed_bps = 0.0;
            d->stop_requested = 1;
            sync_download_size_from_disk(d);
            paused_count++;
            continue;
        }

        if (d->status.state == DOWNLOAD_STATE_PAUSED) {
            d->status.speed_bps = 0.0;
            d->stop_requested = 1;
            sync_download_size_from_disk(d);
        }
    }
    ludo_mutex_unlock(&g_mgr.list_mutex);

    if (paused_count > 0) {
        gui_log(LOG_INFO, "Closing app: pausing %d active download(s) and saving progress.", paused_count);
    }
}

void download_manager_shutdown(void) {
    if (!g_mgr.initialized || g_mgr.shutdown_complete) return;

    dm_log("[shutdown] begin");
    download_manager_prepare_for_shutdown();
    task_queue_shutdown(&g_mgr.queue);
    for (int i = 0; i < g_mgr.num_workers; i++) {
        ludo_thread_join(g_mgr.workers[i]);
    }
    free(g_mgr.workers);
    g_mgr.workers = NULL;

    /* Persist current download list, then archive if large */
    db_save_and_archive();

    /* Free download list */
    ludo_mutex_lock(&g_mgr.list_mutex);
    Download *it = g_mgr.list;
    while (it) {
        Download *next = it->next;
        free(it);
        it = next;
    }
    g_mgr.list = NULL;
    ludo_mutex_unlock(&g_mgr.list_mutex);

    task_queue_destroy(&g_mgr.queue);
    ludo_mutex_destroy(&g_mgr.list_mutex);
    curl_global_cleanup();
    g_mgr.shutdown_complete = 1;
    g_mgr.initialized = 0;
    dm_log("[shutdown] complete");
    dm_log_close();
}

int download_manager_add(const char *url, const char *output_dir, DownloadMode mode,
                         const char *original_url, const char *hint_filename,
                         DownloadAddResult *result)
{
    (void)mode; /* DOWNLOAD_NOW vs DOWNLOAD_QUEUE handled via queue order */
    Download *d;
    URLTask task;
    ProgressUpdate upd;

    if (result) memset(result, 0, sizeof(*result));
    if (!url || url[0] == '\0') return -1;

    d = (Download *)calloc(1, sizeof(Download));
    if (!d) return -1;

    ludo_mutex_lock(&g_mgr.list_mutex);
    if (g_mgr.shutting_down) {
        ludo_mutex_unlock(&g_mgr.list_mutex);
        free(d);
        return -1;
    }

    d->status.id = g_mgr.next_id++;
    strncpy(d->url, url, sizeof(d->url) - 1);
    /* Preserve the original user-supplied URL so redirects or plugin-resolved
       mirror URLs do not overwrite what the user entered. */
    if (original_url && original_url[0] != '\0')
        strncpy(d->original_url, original_url, sizeof(d->original_url) - 1);
    else
        strncpy(d->original_url, url, sizeof(d->original_url) - 1);
    d->original_url[sizeof(d->original_url) - 1] = '\0';
    dm_log("[add] url=%s", d->url);
    dm_log("[add] original_url=%s", d->original_url);
    if (output_dir && output_dir[0] != '\0')
        strncpy(d->output_dir, output_dir, sizeof(d->output_dir) - 1);
    else
        strncpy(d->output_dir, g_mgr.output_dir, sizeof(d->output_dir) - 1);
    if (hint_filename && hint_filename[0] != '\0') {
        char safe[sizeof(d->status.filename)];
        decode_filename_component(hint_filename, safe, sizeof(safe));
        strncpy(d->status.filename, safe, sizeof(d->status.filename) - 1);
    } else
        filename_from_url(url, d->status.filename, sizeof(d->status.filename));
    d->status.filename[sizeof(d->status.filename) - 1] = '\0';
    d->status.state = DOWNLOAD_STATE_QUEUED;
    d->status.start_time = time(NULL);
    d->next = g_mgr.list;
    g_mgr.list = d;
    ludo_mutex_unlock(&g_mgr.list_mutex);

    memset(&upd, 0, sizeof(upd));
    upd.status.id = d->status.id;
    upd.status.state = DOWNLOAD_STATE_QUEUED;
    upd.status.progress = 0.0;
    strncpy(upd.status.filename, d->status.filename, sizeof(upd.status.filename) - 1);
    gui_dispatch_update(&upd);

    /* When the caller wants preflight data (result != NULL), perform a
       synchronous HEAD request so we can return the real server HTTP status
       code.  Plugins rely on checking status == 200 to determine whether the
       server accepted the URL.  The result is cached on the Download so the
       worker thread skips a redundant HEAD request when it later calls
       perform_download(). */
    if (result) {
        HeaderCtx hctx;
        memset(&hctx, 0, sizeof(hctx));
        hctx.download_id = d->status.id;
        probe_download_head(url, &hctx);

        ludo_mutex_lock(&g_mgr.list_mutex);
        d->has_preflight            = 1;
        d->preflight_status_code    = hctx.response_code;
        d->preflight_content_length = hctx.content_length;
        if (hctx.has_filename) {
            strncpy(d->preflight_filename, hctx.filename,
                    sizeof(d->preflight_filename) - 1);
            d->preflight_filename[sizeof(d->preflight_filename) - 1] = '\0';
            strncpy(d->status.filename, hctx.filename,
                    sizeof(d->status.filename) - 1);
            d->status.filename[sizeof(d->status.filename) - 1] = '\0';
        }
        ludo_mutex_unlock(&g_mgr.list_mutex);

        result->id          = d->status.id;
        result->status_code = hctx.response_code;
        build_download_path(d, result->output_path, sizeof(result->output_path));
    }

    memset(&task, 0, sizeof(task));
    task.download_id = d->status.id;
    strncpy(task.url, url, sizeof(task.url) - 1);
    strncpy(task.output_dir, d->output_dir, sizeof(task.output_dir) - 1);
    task_queue_push_task(&g_mgr.queue, &task);
    return d->status.id;
}

bool download_manager_pause(int id) {
    bool result = false;
    ludo_mutex_lock(&g_mgr.list_mutex);
    for (Download *d = g_mgr.list; d; d = d->next) {
        if (d->status.id == id && (d->status.state == DOWNLOAD_STATE_RUNNING || d->status.state == DOWNLOAD_STATE_QUEUED)) {
            d->status.state = DOWNLOAD_STATE_PAUSED;
            d->status.speed_bps = 0.0;
            d->stop_requested = 1;
            gui_log(LOG_SUCCESS, "Download id=%d marked as paused", d->status.id);
            result = true;
            break;
        }
    }
    ludo_mutex_unlock(&g_mgr.list_mutex);
    return result;
}

bool download_manager_resume(int id) {
    bool result = false;
    ludo_mutex_lock(&g_mgr.list_mutex);
    if (g_mgr.shutting_down) {
        ludo_mutex_unlock(&g_mgr.list_mutex);
        return false;
    }
    for (Download *d = g_mgr.list; d; d = d->next) {
        if (d->status.id == id && (d->status.state == DOWNLOAD_STATE_PAUSED || d->status.state == DOWNLOAD_STATE_FAILED)) {
            d->status.state = DOWNLOAD_STATE_QUEUED;
            d->status.speed_bps = 0.0;
            d->stop_requested = 0;
            gui_log(LOG_INFO, "Resuming %s", d->status.filename);
            result = true;

            /* Push an internal task to tell the worker thread to resume this ID */
            URLTask task;
            memset(&task, 0, sizeof(task));
            task.download_id = d->status.id;
            task.is_resume_task = 1;
            task_queue_push_task(&g_mgr.queue, &task);
            
            /* Notify GUI to update UI state immediately */
            ProgressUpdate upd = {0};
            upd.status = d->status;
            gui_dispatch_update(&upd);
            
            break;
        }
    }
    ludo_mutex_unlock(&g_mgr.list_mutex);
    return result;
}

bool download_manager_remove(int id) {
    bool result = false;
    ProgressUpdate final_upd;
    int dispatch_final = 0;

    ludo_mutex_lock(&g_mgr.list_mutex);
    Download **prev = &g_mgr.list;
    while (*prev) {
        if ((*prev)->status.id == id) {
            Download *target = *prev;
            
            /* If a worker thread is touching this, defer the free() */
                if (target->status.state == DOWNLOAD_STATE_RUNNING || 
                target->status.state == DOWNLOAD_STATE_QUEUED) {
                
                target->status.state = DOWNLOAD_STATE_PAUSED; /* Forces curl to abort */
                target->status.speed_bps = 0.0;
                target->stop_requested = 1;
                target->marked_for_removal = 1;               /* Tells worker to free it */
                gui_log(LOG_INFO, "Removing %s for garbage collection.", target->status.filename);
                result = true;
            } else {
                /* Safe to delete immediately.  Capture data before free() so we
                 * can dispatch the final GUI notification outside the lock. */
                memset(&final_upd, 0, sizeof(final_upd));
                final_upd.status             = target->status;
                final_upd.marked_for_removal = 1;
                dispatch_final = 1;

                *prev = target->next;
                gui_log(LOG_SUCCESS, "%s removed", target->status.filename);
                free(target);
                result = true;
            }
            break;
        }
        prev = &(*prev)->next;
    }
    ludo_mutex_unlock(&g_mgr.list_mutex);

    /* Notify the GUI outside the lock; gui_on_progress will delete the row. */
    if (dispatch_final)
        gui_dispatch_update(&final_upd);

    return result;
}

Download *download_manager_get_list(void) {
    return g_mgr.list; /* caller should hold list_mutex before reading */
}

void download_manager_set_progress_cb(progress_callback_t cb, void *user_data) {
    g_mgr.progress_cb      = cb;
    g_mgr.progress_cb_data = user_data;
}

const char *download_manager_get_output_dir(void) {
    return g_mgr.output_dir;
}

void download_manager_set_output_dir(const char *output_dir) {
    if (!output_dir || output_dir[0] == '\0') return;

    ludo_mutex_lock(&g_mgr.list_mutex);
    strncpy(g_mgr.output_dir, output_dir, sizeof(g_mgr.output_dir) - 1);
    g_mgr.output_dir[sizeof(g_mgr.output_dir) - 1] = '\0';
    ludo_mutex_unlock(&g_mgr.list_mutex);
}

int download_manager_find_status(int id, DownloadStatus *out) {
    int found = 0;
    ludo_mutex_lock(&g_mgr.list_mutex);
    for (Download *d = g_mgr.list; d; d = d->next) {
        if (d->status.id == id) { *out = d->status; found = 1; break; }
    }
    ludo_mutex_unlock(&g_mgr.list_mutex);
    return found;
}

void download_manager_sync_ui(void) {
    ludo_mutex_lock(&g_mgr.list_mutex);
    for (Download *d = g_mgr.list; d; d = d->next) {
        /* Only notify GUI about non-completed tasks to avoid re-displaying
           finished items at startup or on explicit sync. */
        if (d->status.state == DOWNLOAD_STATE_COMPLETED) continue;
        ProgressUpdate upd = {0};
        upd.status = d->status;
        /* Re-dispatch the loaded status to the GUI */
        gui_dispatch_update(&upd);
    }
    ludo_mutex_unlock(&g_mgr.list_mutex);
}
