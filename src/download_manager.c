#include "download_manager.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <errno.h>
#include <zlib.h>

#ifdef _WIN32
#include <sys/stat.h>
#else
#include <sys/stat.h>
#endif

#include <curl/curl.h>
#include "ui.h"

/* Path constants for persistence */
#define DB_PATH      "dl_items.db"
#define ARCHIVE_PATH "dl_history.gz"
#define DB_MAX_BYTES 10240   /* 10 KB — archive trigger */

/* ------------------------------------------------------------------ */
/* Debug logging (writes to dm_debug.log; safe from any thread)        */
/* ------------------------------------------------------------------ */

#include <stdarg.h>
#include "dm_log.h"

FILE *g_log_fp = NULL;

#ifdef _WIN32
#include <windows.h>   /* for CRITICAL_SECTION */
static CRITICAL_SECTION g_log_cs;
static int g_log_cs_init = 0;
#endif

void dm_log_init(void) {
#ifdef _WIN32
    if (!g_log_cs_init) { InitializeCriticalSection(&g_log_cs); g_log_cs_init = 1; }
#endif
    g_log_fp = fopen("dm_debug.log", "a");
    if (!g_log_fp) return;
    /* Header line so runs are separated */
    fprintf(g_log_fp, "\n===== dm session start =====\n");
    fflush(g_log_fp);
}

void dm_log(const char *fmt, ...) {
    if (!g_log_fp) return;
#ifdef _WIN32
    if (g_log_cs_init) EnterCriticalSection(&g_log_cs);
#endif
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
#ifdef _WIN32
    if (g_log_cs_init) LeaveCriticalSection(&g_log_cs);
#endif
}

void dm_log_close(void) {
    if (!g_log_fp) return;
#ifdef _WIN32
    if (g_log_cs_init) EnterCriticalSection(&g_log_cs);
#endif
    fprintf(g_log_fp, "===== dm session end =====\n");
    fclose(g_log_fp);
    g_log_fp = NULL;
#ifdef _WIN32
    if (g_log_cs_init) LeaveCriticalSection(&g_log_cs);
#endif
}

/* ------------------------------------------------------------------ */
/* Internal types                                                       */
/* ------------------------------------------------------------------ */

typedef struct HeaderCtx HeaderCtx;

/* WriteCtx extended: includes offset already on disk before this request */
typedef struct {
    FILE  *fp;
    int    download_id;
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
static int curl_debug_cb(CURL *handle, curl_infotype type,
                         char *data, size_t size, void *userp)
{
    (void)handle; (void)userp;
    /* Only log informational text lines, skip raw data/header blobs
       which are already captured by header_cb */
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

/* Context for the response-header callback */
struct HeaderCtx {
    char filename[512];   /* set if Content-Disposition provides one */
    int  has_filename;
    int  download_id;     /* filled by perform_download so header_cb can dispatch */
    int64_t content_length; /* parsed or obtained from HEAD */
    long response_code;   /* parsed from the HTTP status line */
};

/* Forward declare gui_dispatch_update so header_cb may call it before its
   static definition later in this file. */
static void gui_dispatch_update(const ProgressUpdate *update);
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

    dm_log("[header_cb] %s", line);

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
                    dm_log("[header_cb] skipping filename*= (RFC 5987 extended)");
                    continue;
                }
                break;
            }

            if (fn) {
                fn += 9; /* skip "filename=" */
                dm_log("[header_cb] filename= value starts with: %.80s", fn);
                /* Handle quoted value */
                if (*fn == '"') {
                    fn++;
                    char *end = strchr(fn, '"');
                    if (end) {
                        size_t len = (size_t)(end - fn);
                        if (len >= sizeof(hctx->filename)) len = sizeof(hctx->filename) - 1;
                        memcpy(hctx->filename, fn, len);
                        hctx->filename[len] = '\0';
                        if (!hctx->has_filename) {
                            hctx->has_filename  = 1;
                            dm_log("[header_cb] parsed filename (quoted): %s", hctx->filename);
                            /* Notify GUI immediately so Name column updates without waiting */
                            ProgressUpdate upd = {0};
                            upd.status.id = hctx->download_id;
                            strncpy(upd.status.filename, hctx->filename, sizeof(upd.status.filename) - 1);
                            upd.status.filename[sizeof(upd.status.filename) - 1] = '\0';
                            upd.status.state = DOWNLOAD_STATE_RUNNING;
                            gui_dispatch_update(&upd);
                        }
                    }
                } else {
                    /* unquoted value — ends at whitespace or ; */
                    size_t len = strcspn(fn, " ;\r\n");
                    if (len > 0) {
                        if (len >= sizeof(hctx->filename)) len = sizeof(hctx->filename) - 1;
                        memcpy(hctx->filename, fn, len);
                        hctx->filename[len] = '\0';
                        if (!hctx->has_filename) {
                            hctx->has_filename  = 1;
                            dm_log("[header_cb] parsed filename (unquoted): %s", hctx->filename);
                            /* Notify GUI immediately so Name column updates without waiting */
                            ProgressUpdate upd = {0};
                            upd.status.id = hctx->download_id;
                            strncpy(upd.status.filename, hctx->filename, sizeof(upd.status.filename) - 1);
                            upd.status.filename[sizeof(upd.status.filename) - 1] = '\0';
                            upd.status.state = DOWNLOAD_STATE_RUNNING;
                            gui_dispatch_update(&upd);
                        }
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

    int                 running;
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

/* Derive filename from URL (last path segment) */
static void filename_from_url(const char *url, char *out, size_t out_sz) {
    const char *last_slash = strrchr(url, '/');
    const char *start = last_slash ? last_slash + 1 : url;
    /* strip query string */
    const char *q = strchr(start, '?');
    size_t len = q ? (size_t)(q - start) : strlen(start);
    if (len == 0) {
        strncpy(out, "download", out_sz - 1);
        out[out_sz - 1] = '\0';
        return;
    }
    if (len >= out_sz) len = out_sz - 1;
    memcpy(out, start, len);
    out[len] = '\0';
}

/* curl progress callback (CURLOPT_XFERINFOFUNCTION) */
static int xfer_info_cb(void *clientp, curl_off_t dltotal, curl_off_t dlnow,
                        curl_off_t ultotal, curl_off_t ulnow)
{
    (void)ultotal; (void)ulnow;
    WriteCtx *ctx = (WriteCtx *)clientp;

    /* Check cancellation */
    if (*ctx->cancel_flag) return 1; /* aborts transfer */

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
    ludo_mutex_unlock(&g_mgr.list_mutex);

    if (d) {
        ProgressUpdate upd = {0};
        upd.status.id            = ctx->download_id;
        upd.status.state         = DOWNLOAD_STATE_RUNNING;
        upd.status.progress      = d->status.progress;
        upd.status.downloaded_bytes = d->status.downloaded_bytes;
        upd.status.total_bytes   = d->status.total_bytes;
        upd.status.speed_bps     = d->status.speed_bps;
        upd.status.start_time    = d->status.start_time;
        fflush(ctx->fp); /* ensure progress is saved to disk in case of crash */
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
    if (!ctx->fp) {
        dm_log("[write_cb] id=%d fp is NULL!", ctx->download_id);
        return 0;
    }

    if (ctx->resume_offset > 0 && !ctx->resume_checked) {
        long response_code = ctx->header_ctx ? ctx->header_ctx->response_code : 0;
        if (response_code == 200) {
            dm_log("[write_cb] id=%d server ignored resume request; restarting from byte 0", ctx->download_id);
            fclose(ctx->fp);
            ctx->fp = fopen(ctx->path, "wb");
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

/* Perform the actual file download for entry `d` */
static void perform_download(Download *d) {
    dm_log("[perform_download] id=%d url=%s", d->status.id, d->url);
    dm_log("[perform_download] output_dir=%s  filename=%s", d->output_dir, d->status.filename);

    /* ---- Preflight HEAD request to obtain filename and content-length ---- */
    const char *sep = "";
    size_t dlen = strlen(d->output_dir);
    if (dlen > 0) {
        char last = d->output_dir[dlen - 1];
        if (last != '/' && last != '\\') sep = "/";
    }

    HeaderCtx hctx;
    memset(&hctx, 0, sizeof(hctx));
    hctx.download_id = d->status.id;

    CURL *head = curl_easy_init();
    if (head) {
        curl_easy_setopt(head, CURLOPT_URL, d->url);
        curl_easy_setopt(head, CURLOPT_NOBODY, 1L);
        curl_easy_setopt(head, CURLOPT_HEADERFUNCTION, header_cb);
        curl_easy_setopt(head, CURLOPT_HEADERDATA, &hctx);
        curl_easy_setopt(head, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(head, CURLOPT_USERAGENT,
                         "Mozilla/5.0 (Windows NT 10.0; Win64; x64)");
        curl_easy_perform(head);
        curl_off_t cl = 0;
        if (curl_easy_getinfo(head, CURLINFO_CONTENT_LENGTH_DOWNLOAD_T, &cl) == CURLE_OK) {
            if (cl > 0) hctx.content_length = (int64_t)cl;
        }
        /* If header parsing provided a filename, update the download record */
        if (hctx.has_filename) {
            ludo_mutex_lock(&g_mgr.list_mutex);
            strncpy(d->status.filename, hctx.filename, sizeof(d->status.filename)-1);
            d->status.filename[sizeof(d->status.filename)-1] = '\0';
            ludo_mutex_unlock(&g_mgr.list_mutex);
        }
        curl_easy_cleanup(head);
    }

    /* Build final output path (may have been updated from headers) */
    char path[2048];
    snprintf(path, sizeof(path), "%s%s%s", d->output_dir, sep, d->status.filename);
    dm_log("[perform_download] id=%d output path=%s", d->status.id, path);

    /* Decide whether to skip, resume, or start fresh based on existing file */
    curl_off_t resume_from = 0;
    {
        struct stat st;
        if (stat(path, &st) == 0 && st.st_size > 0) {
            if (hctx.content_length > 0 && (int64_t)st.st_size == hctx.content_length) {
                /* File already complete — mark completed and notify GUI */
                ludo_mutex_lock(&g_mgr.list_mutex);
                d->status.downloaded_bytes = (int64_t)st.st_size;
                d->status.total_bytes = (int64_t)st.st_size;
                d->status.state = DOWNLOAD_STATE_COMPLETED;
                d->status.progress = 100.0;
                ludo_mutex_unlock(&g_mgr.list_mutex);

                ProgressUpdate upd = {0};
                upd.status.id = d->status.id;
                upd.status.state = DOWNLOAD_STATE_COMPLETED;
                upd.status.progress = 100.0;
                upd.status.downloaded_bytes = d->status.downloaded_bytes;
                upd.status.total_bytes = d->status.total_bytes;
                strncpy(upd.status.filename, d->status.filename, sizeof(upd.status.filename)-1);
                gui_dispatch_update(&upd);
                dm_log("[perform_download] id=%d existing file matches content-length; skipping download", d->status.id);
                return;
            }
            /* Otherwise attempt resume from existing size */
            resume_from = (curl_off_t)st.st_size;
            dm_log("[perform_download] id=%d partial file found, resume_from=%lld",
                   d->status.id, (long long)resume_from);
        } else {
            dm_log("[perform_download] id=%d no partial file, starting fresh", d->status.id);
        }
    }

    FILE *fp = fopen(path, resume_from > 0 ? "ab" : "wb");
    if (!fp) {
        dm_log("[perform_download] id=%d fopen failed for: %s (errno=%d)",
               d->status.id, path, errno);
        ProgressUpdate upd = {0};
        upd.status.id    = d->status.id;
        upd.status.state = DOWNLOAD_STATE_FAILED;
        snprintf(upd.error_msg, sizeof(upd.error_msg),
                 "Cannot open output file: %s", path);
        ludo_mutex_lock(&g_mgr.list_mutex);
        d->status.state = DOWNLOAD_STATE_FAILED;
        ludo_mutex_unlock(&g_mgr.list_mutex);
        gui_dispatch_update(&upd);
        return;
    }
    dm_log("[perform_download] id=%d file opened (%s)",
           d->status.id, resume_from > 0 ? "append" : "write");

    int pause_flag  = 0;
    int cancel_flag = 0;

    HeaderCtx hctx_local;
    /* Seed main-transfer header context with any info learned from the HEAD preflight */
    memcpy(&hctx_local, &hctx, sizeof(hctx_local));
    hctx_local.download_id = d->status.id;

    CURL *curl = curl_easy_init();
    if (!curl) {
        dm_log("[perform_download] id=%d curl_easy_init() returned NULL", d->status.id);
        fclose(fp);
        ProgressUpdate upd = {0};
        upd.status.id = d->status.id;
        upd.status.state = DOWNLOAD_STATE_FAILED;
        strncpy(upd.status.filename, d->status.filename, sizeof(upd.status.filename) - 1);
        snprintf(upd.error_msg, sizeof(upd.error_msg), "Failed to initialize curl");
        ludo_mutex_lock(&g_mgr.list_mutex);
        d->status.state = DOWNLOAD_STATE_FAILED;
        ludo_mutex_unlock(&g_mgr.list_mutex);
        gui_dispatch_update(&upd);
        return;
    }
    /* Publish handle so other threads can pause/resume via curl_easy_pause */
    ludo_mutex_lock(&g_mgr.list_mutex);
    WriteCtx ctx = { fp, d->status.id, &pause_flag, &cancel_flag, (int64_t)resume_from, 0, curl, 0, &hctx_local, path, 0 };
    d->curl_handle = (void *)curl;
    d->fp = fp; /* also publish file handle for pause logic */
    ludo_mutex_unlock(&g_mgr.list_mutex);
    dm_log("[perform_download] id=%d curl handle ok, setting options", d->status.id);

    curl_easy_setopt(curl, CURLOPT_URL,               d->url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION,     write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA,         &ctx);
    curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION,  xfer_info_cb);
    curl_easy_setopt(curl, CURLOPT_XFERINFODATA,      &ctx);
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS,        0L);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION,    1L);
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS,         10L);
    curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING,   "gzip, deflate, zstd, br");
    curl_easy_setopt(curl, CURLOPT_USERAGENT,
                     "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/146.0.0.0 Safari/537.36 Edg/146.0.0.0");
    curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION,    header_cb);
    curl_easy_setopt(curl, CURLOPT_HEADERDATA,        &hctx_local);
    /* Route libcurl verbose output to dm_log instead of stderr
       (stderr is NULL in a WIN32/no-console app and would crash) */
    curl_easy_setopt(curl, CURLOPT_VERBOSE,           1L);
    curl_easy_setopt(curl, CURLOPT_DEBUGFUNCTION,     curl_debug_cb);
    curl_easy_setopt(curl, CURLOPT_DEBUGDATA,         NULL);

    /* If partial file exists, attempt HTTP range resume */
    if (resume_from > 0) {
        curl_easy_setopt(curl, CURLOPT_RESUME_FROM_LARGE, resume_from);
        dm_log("[perform_download] id=%d CURLOPT_RESUME_FROM_LARGE = %lld",
               d->status.id, (long long)resume_from);
    }

    /* Record start time */
    ludo_mutex_lock(&g_mgr.list_mutex);
    d->status.start_time = time(NULL);
    ludo_mutex_unlock(&g_mgr.list_mutex);

    dm_log("[perform_download] id=%d calling curl_easy_perform...", d->status.id);
    CURLcode res = curl_easy_perform(curl);
    dm_log("[perform_download] id=%d curl_easy_perform returned %d (%s)",
           d->status.id, (int)res, curl_easy_strerror(res));

    /* Log final effective URL (after all redirects) */
    {
        char *eff_url = NULL;
        curl_easy_getinfo(curl, CURLINFO_EFFECTIVE_URL, &eff_url);
        if (eff_url) dm_log("[perform_download] id=%d effective URL: %s", d->status.id, eff_url);
    }

    /* Log HTTP response code */
    {
        long http_code = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
        dm_log("[perform_download] id=%d HTTP response code: %ld", d->status.id, http_code);
    }

    /* Update filename from Content-Disposition if server provided one */
    if (res == CURLE_OK && hctx_local.has_filename) {
        char new_path[2048];
        snprintf(new_path, sizeof(new_path), "%s%s%s",
                 d->output_dir, sep, hctx_local.filename);
        dm_log("[perform_download] id=%d renaming  %s  ->  %s", d->status.id, path, new_path);
        /* Only rename if different */
        if (strcmp(new_path, path) != 0) {
            fclose(fp);
            fp = NULL;
            int rv = rename(path, new_path);
            dm_log("[perform_download] id=%d rename() returned %d (errno=%d)",
                   d->status.id, rv, errno);
            if (rv != 0) {
                if (errno == EEXIST) {
                    int rv = rename(path, new_path);
                    dm_log("[perform_download] id=%d rename() returned %d (errno=%d)",
                        d->status.id, rv, errno);
                    
                    if (rv != 0) {
                        /* Handle file collision (common on Windows) */
                        struct stat st_new, st_old;
                        if (stat(new_path, &st_new) == 0 && stat(path, &st_old) == 0) {
                            if (st_new.st_size == st_old.st_size) {
                                dm_log("[perform_download] id=%d Target exists with exact same size. Discarding temp.", d->status.id);
                                remove(path);
                            } else {
                                dm_log("[perform_download] id=%d Target exists with different size. Overwriting...", d->status.id);
                                remove(new_path);            // 1. Delete the old file
                                rename(path, new_path);      // 2. Rename the new one
                            }
                        }
                    }
                }
            }
        } else {
            dm_log("[perform_download] id=%d paths identical, no rename needed", d->status.id);
        }
        ludo_mutex_lock(&g_mgr.list_mutex);
        strncpy(d->status.filename, hctx_local.filename, sizeof(d->status.filename) - 1);
        d->status.filename[sizeof(d->status.filename) - 1] = '\0';
        ludo_mutex_unlock(&g_mgr.list_mutex);
    }

    /* Update total_bytes from Content-Length via curl */
    {
        curl_off_t cl = 0;
        if (curl_easy_getinfo(curl, CURLINFO_CONTENT_LENGTH_DOWNLOAD_T, &cl) == CURLE_OK) {
            dm_log("[perform_download] id=%d Content-Length = %lld", d->status.id, (long long)cl);
            if (cl > 0) {
                ludo_mutex_lock(&g_mgr.list_mutex);
                d->status.total_bytes = (int64_t)cl + (int64_t)resume_from;
                ludo_mutex_unlock(&g_mgr.list_mutex);
            }
        }
    }

    if (fp) {
        dm_log("[perform_download] id=%d closing file", d->status.id);
        fclose(fp);
        fp = NULL;
    }
    curl_easy_cleanup(curl);
    ludo_mutex_lock(&g_mgr.list_mutex);
    d->curl_handle = NULL;
    d->fp = NULL;
    ludo_mutex_unlock(&g_mgr.list_mutex);
    dm_log("[perform_download] id=%d curl cleaned up", d->status.id);

    /* If finished successfully, probe the final file size on disk and
       ensure downloaded_bytes/total_bytes reflect the actual size so the
       GUI shows "size/size" when completed. */
    if (res == CURLE_OK) {
        char final_path[2048];
        snprintf(final_path, sizeof(final_path), "%s%s%s",
                 d->output_dir, sep, d->status.filename);
        struct stat st;
        if (stat(final_path, &st) == 0) {
            ludo_mutex_lock(&g_mgr.list_mutex);
            d->status.downloaded_bytes = (int64_t)st.st_size;
            d->status.total_bytes = (int64_t)st.st_size;
            ludo_mutex_unlock(&g_mgr.list_mutex);
            dm_log("[perform_download] id=%d final file size = %lld", d->status.id, (long long)st.st_size);
        } else {
            dm_log("[perform_download] id=%d stat failed for %s (errno=%d)", d->status.id, final_path, errno);
        }
    }

    /* Record finish time and final state */
    ludo_mutex_lock(&g_mgr.list_mutex);
    d->status.end_time = time(NULL);
    if (res == CURLE_OK) {
        d->status.state    = DOWNLOAD_STATE_COMPLETED;
        d->status.progress = 100.0;
    } else if (d->status.state != DOWNLOAD_STATE_QUEUED) {
        d->status.state = DOWNLOAD_STATE_FAILED;
    }
    
    /* GARBAGE COLLECTION CHECK */
    int is_deleted = d->marked_for_removal;
    if (is_deleted) {
        /* Unlink from the master list safely */
        Download **prev = &g_mgr.list;
        while (*prev) {
            if (*prev == d) {
                *prev = d->next;
                break;
            }
            prev = &(*prev)->next;
        }
    }

    int final_id = d->status.id;
    DownloadState final_state = d->status.state;
    time_t start_ts  = d->status.start_time;
    time_t finish_ts = d->status.end_time;
    double final_progress = d->status.progress;
    int64_t final_downloaded_bytes = d->status.downloaded_bytes;
    int64_t final_total_bytes = d->status.total_bytes;
    double final_speed_bps = d->status.speed_bps;
    char final_filename[sizeof(d->status.filename)];
    strncpy(final_filename, d->status.filename, sizeof(final_filename) - 1);
    final_filename[sizeof(final_filename) - 1] = '\0';
    ludo_mutex_unlock(&g_mgr.list_mutex);

    if (is_deleted) {
        dm_log("[perform_download] ID %d collected. Freeing memory safely.", final_id);
        free(d);
        return; /* CRITICAL: Do not dispatch GUI updates for deleted memory */
    }
    
    dm_log("[perform_download] id=%d final state=%d filename=%s",
           final_id, (int)final_state, final_filename);

    ProgressUpdate upd = {0};
    upd.status.id         = final_id;
    upd.status.state      = final_state;
    upd.status.progress   = (final_state == DOWNLOAD_STATE_COMPLETED) ? 100.0 : final_progress;
    upd.status.start_time = start_ts;
    upd.status.end_time   = finish_ts;
    /* Preserve totals/speeds so GUI can show Size/Speed after completion */
    upd.status.downloaded_bytes = final_downloaded_bytes;
    upd.status.total_bytes = final_total_bytes;
    upd.status.speed_bps = final_speed_bps;
    if (res != CURLE_OK) {
        strncpy(upd.error_msg, curl_easy_strerror(res), sizeof(upd.error_msg) - 1);
    }
    strncpy(upd.status.filename, final_filename, sizeof(upd.status.filename) - 1);
    gui_dispatch_update(&upd);
    dm_log("[perform_download] id=%d done", final_id);
}

/* ------------------------------------------------------------------ */
/* Persistence — dl_items.db + dl_history.gz                           */
/* ------------------------------------------------------------------ */

/*
 * File format: one record per line, fields separated by TAB.
 * Fields: id  url  output_dir  filename  state  progress  speed_bps
 *         total_bytes  downloaded_bytes  start_time  end_time
 */

static void db_save(const char *path) {
    FILE *f = fopen(path, "wb");
    if (!f) return;

    ludo_mutex_lock(&g_mgr.list_mutex);
    for (Download *d = g_mgr.list; d; d = d->next) {
        fprintf(f, "%d\t%s\t%s\t%s\t%d\t%.2f\t%.2f\t%lld\t%lld\t%lld\t%lld\n",
                d->status.id,
                d->url,
                d->output_dir,
                d->status.filename,
                (int)d->status.state,
                d->status.progress,
                d->status.speed_bps,
                (long long)d->status.total_bytes,
                (long long)d->status.downloaded_bytes,
                (long long)(int64_t)d->status.start_time,
                (long long)(int64_t)d->status.end_time);
    }
    ludo_mutex_unlock(&g_mgr.list_mutex);
    fclose(f);
}

static void db_load(const char *path) {
    /* Robust loader that can handle very long lines (long URLs with large
       query strings). Uses a dynamically growing buffer to read one logical
       line at a time. */
    FILE *f = fopen(path, "rb");
    if (!f) return;

    char *line = NULL;
    size_t cap = 0;
    while (1) {
        /* Read a line into a dynamic buffer, growing until a newline or EOF */
        if (line == NULL) {
            cap = 8192;
            line = (char *)malloc(cap);
            if (!line) break;
            line[0] = '\0';
        }
        size_t len = strlen(line);
        char *res = fgets(line + len, (int)(cap - len), f);
        if (!res) {
            if (len == 0) break; /* EOF */
            /* last line without newline — proceed */
        } else {
            /* If the buffer didn't end with a newline and we filled it, grow */
            len = strlen(line);
            if (len > 0 && line[len-1] != '\n') {
                /* Need more space */
                size_t newcap = cap * 2;
                char *tmp = (char *)realloc(line, newcap);
                if (!tmp) break;
                line = tmp;
                cap = newcap;
                continue; /* continue reading into enlarged buffer */
            }
        }

        /* Strip trailing CR/LF */
        line[strcspn(line, "\r\n")] = 0;

        Download *d = (Download *)calloc(1, sizeof(Download));
        if (!d) break;

        char *p = line;
        char *fields[11];
        for (int i = 0; i < 11; i++) {
            fields[i] = p;
            if (p) {
                p = strchr(p, '\t');
                if (p) { *p = '\0'; p++; }
            }
        }

        /* If we didn't find all 11 fields, skip it */
        if (!fields[10]) {
            free(d);
            /* reset buffer for next iteration */
            free(line);
            line = NULL;
            cap = 0;
            continue;
        }

        d->status.id = atoi(fields[0]);
        strncpy(d->url, fields[1], sizeof(d->url)-1);
        d->url[sizeof(d->url)-1] = '\0';
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

        /* SAFETY: If the app crashed, reset active states so they can be resumed.
           Paused state folded into QUEUED. */
        if (d->status.state == DOWNLOAD_STATE_RUNNING || d->status.state == DOWNLOAD_STATE_QUEUED) {
            d->status.state = DOWNLOAD_STATE_QUEUED;
        }

        if (d->status.id >= g_mgr.next_id) g_mgr.next_id = d->status.id + 1;

        d->next = g_mgr.list;
        g_mgr.list = d;

        /* reset buffer for next line */
        free(line);
        line = NULL;
        cap = 0;
    }

    free(line);
    fclose(f);
}

/*
 * If dl_items.db exceeds DB_MAX_BYTES, append its contents to
 * dl_history.gz and then truncate the db file.
 */
static void db_archive_if_needed(const char *db_path, const char *gz_path) {
    /* Measure db size */
    FILE *f = fopen(db_path, "rb");
    if (!f) return;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return; }
    long db_size = ftell(f);
    if (db_size <= 0 || db_size < DB_MAX_BYTES) { fclose(f); return; }
    rewind(f);

    /* Read entire db into memory */
    char *buf = (char *)malloc((size_t)db_size);
    if (!buf) { fclose(f); return; }
    size_t got = fread(buf, 1, (size_t)db_size, f);
    fclose(f);

    /* Append to the gzip archive */
    gzFile gz = gzopen(gz_path, "ab");   /* "a" = append mode */
    if (!gz) { free(buf); return; }
    gzwrite(gz, buf, (unsigned)got);
    gzclose(gz);
    free(buf);

    /* Truncate/clear the db now that contents are archived */
    f = fopen(db_path, "wb");
    if (f) fclose(f);
}

/* ------------------------------------------------------------------ */
/* Worker thread                                                        */
/* ------------------------------------------------------------------ */
static void *worker_thread(void *arg) {
    (void)arg;
    URLTask task;
    while (task_queue_pop(&g_mgr.queue, &task)) {
        
        /* Check if this is a command to resume an existing download */
        if (strncmp(task.url, "INTERNAL_RESUME:", 16) == 0) {
            int resume_id = atoi(task.url + 16);
            ludo_mutex_lock(&g_mgr.list_mutex);
            Download *res_d = NULL;
            for (Download *it = g_mgr.list; it; it = it->next) {
                if (it->status.id == resume_id) { res_d = it; break; }
            }
            ludo_mutex_unlock(&g_mgr.list_mutex);
            
            if (res_d && res_d->status.state == DOWNLOAD_STATE_QUEUED) {
                perform_download(res_d);
            }
            continue;
        }

        /* --- Standard logic for NEW downloads --- */
        Download *d = (Download *)calloc(1, sizeof(Download));
        if (!d) continue;

        ludo_mutex_lock(&g_mgr.list_mutex);
        d->status.id = g_mgr.next_id++;
        strncpy(d->url, task.url, sizeof(d->url) - 1);
        if (task.output_dir[0] != '\0')
            strncpy(d->output_dir, task.output_dir, sizeof(d->output_dir) - 1);
        else
            strncpy(d->output_dir, g_mgr.output_dir, sizeof(d->output_dir) - 1);
        filename_from_url(task.url, d->status.filename, sizeof(d->status.filename));
        d->status.state = DOWNLOAD_STATE_QUEUED;

        /* Prepend to list */
        d->next      = g_mgr.list;
        g_mgr.list   = d;
        ludo_mutex_unlock(&g_mgr.list_mutex);

        /* Notify GUI of new entry */
        ProgressUpdate upd = {0};
        upd.status.id       = d->status.id;
        upd.status.state    = DOWNLOAD_STATE_QUEUED;
        upd.status.progress = 0.0;
        strncpy(upd.status.filename, d->status.filename, sizeof(upd.status.filename) - 1);
        gui_dispatch_update(&upd);

        perform_download(d);
    }
    return NULL;
}

/* ------------------------------------------------------------------ */
/* Public API                                                           */
/* ------------------------------------------------------------------ */

void download_manager_init(int num_workers, const char *output_dir) {
    dm_log_init();
    dm_log("[init] num_workers=%d  output_dir=%s",
           num_workers, output_dir ? output_dir : "(null -> ./downloads/)");

    memset(&g_mgr, 0, sizeof(g_mgr));
    g_mgr.next_id     = 1;
    g_mgr.num_workers = num_workers;
    g_mgr.running     = 1;

    strncpy(g_mgr.output_dir, output_dir ? output_dir : "./downloads/",
            sizeof(g_mgr.output_dir) - 1);

    ludo_mutex_init(&g_mgr.list_mutex);
    task_queue_init(&g_mgr.queue, 256);

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

    g_mgr.workers = (ludo_thread_t *)calloc((size_t)num_workers,
                                             sizeof(ludo_thread_t));
    for (int i = 0; i < num_workers; i++) {
        ludo_thread_create(&g_mgr.workers[i], worker_thread, NULL);
    }
}

void download_manager_shutdown(void) {
    task_queue_shutdown(&g_mgr.queue);
    for (int i = 0; i < g_mgr.num_workers; i++) {
        ludo_thread_join(g_mgr.workers[i]);
    }
    free(g_mgr.workers);
    g_mgr.workers = NULL;

    /* Persist current download list, then archive if large */
    dm_log("[shutdown] saving db");
    db_save(DB_PATH);
    db_archive_if_needed(DB_PATH, ARCHIVE_PATH);
    dm_log("[shutdown] db saved");

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
    dm_log("[shutdown] complete");
}

int download_manager_add(const char *url, const char *output_dir, DownloadMode mode)
{
    (void)mode; /* DOWNLOAD_NOW vs DOWNLOAD_QUEUE handled via queue order */
    if (!url || url[0] == '\0') return -1;

    URLTask task;
    memset(&task, 0, sizeof(task));
    strncpy(task.url, url, sizeof(task.url) - 1);
    if (output_dir && output_dir[0] != '\0')
        strncpy(task.output_dir, output_dir, sizeof(task.output_dir) - 1);

    task_queue_push_task(&g_mgr.queue, &task);
    return 0; /* actual ID assigned in worker */
}

void download_manager_pause(int id) {
    ludo_mutex_lock(&g_mgr.list_mutex);
    for (Download *d = g_mgr.list; d; d = d->next) {
        if (d->status.id == id && (d->status.state == DOWNLOAD_STATE_RUNNING || d->status.state == DOWNLOAD_STATE_QUEUED)) {
            d->status.state = DOWNLOAD_STATE_PAUSED;
            dm_log("[download_manager_pause] id=%d marked as paused", d->status.id);
            break;
        }
    }
    ludo_mutex_unlock(&g_mgr.list_mutex);
}

void download_manager_resume(int id) {
    ludo_mutex_lock(&g_mgr.list_mutex);
    for (Download *d = g_mgr.list; d; d = d->next) {
        if (d->status.id == id && d->status.state == DOWNLOAD_STATE_PAUSED) {
            d->status.state = DOWNLOAD_STATE_QUEUED;
            dm_log("[download_manager_resume] id=%d queued for resume", d->status.id);

            /* Push an internal task to tell the worker thread to resume this ID */
            URLTask task;
            memset(&task, 0, sizeof(task));
            snprintf(task.url, sizeof(task.url), "INTERNAL_RESUME:%d", d->status.id);
            task_queue_push_task(&g_mgr.queue, &task);
            
            /* Notify GUI to update UI state immediately */
            ProgressUpdate upd = {0};
            upd.status = d->status;
            gui_dispatch_update(&upd);
            
            break;
        }
    }
    ludo_mutex_unlock(&g_mgr.list_mutex);
}

void download_manager_remove(int id) {
    ludo_mutex_lock(&g_mgr.list_mutex);
    Download **prev = &g_mgr.list;
    while (*prev) {
        if ((*prev)->status.id == id) {
            Download *target = *prev;
            
            /* If a worker thread is touching this, defer the free() */
                if (target->status.state == DOWNLOAD_STATE_RUNNING || 
                target->status.state == DOWNLOAD_STATE_QUEUED) {
                
                target->status.state = DOWNLOAD_STATE_PAUSED; /* Forces curl to abort */
                target->marked_for_removal = 1;               /* Tells worker to free it */
                dm_log("[Remove] ID %d is active. Marked for garbage collection.", id);
                
            } else {
                /* Safe to delete immediately */
                *prev = target->next;
                free(target);
                dm_log("[Remove] ID %d removed immediately.", id);
            }
            break;
        }
        prev = &(*prev)->next;
    }
    ludo_mutex_unlock(&g_mgr.list_mutex);
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

Download *download_manager_find(int id) {
    ludo_mutex_lock(&g_mgr.list_mutex);
    Download *found = NULL;
    for (Download *d = g_mgr.list; d; d = d->next) {
        if (d->status.id == id) { found = d; break; }
    }
    ludo_mutex_unlock(&g_mgr.list_mutex);
    return found;
}

void download_manager_sync_ui(void) {
    ludo_mutex_lock(&g_mgr.list_mutex);
    for (Download *d = g_mgr.list; d; d = d->next) {
        ProgressUpdate upd = {0};
        upd.status = d->status;
        /* Re-dispatch the loaded status to the GUI */
        gui_dispatch_update(&upd);
    }
    ludo_mutex_unlock(&g_mgr.list_mutex);
}
