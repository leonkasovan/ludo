#include "download_manager.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
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

static FILE *g_log_fp = NULL;

#ifdef _WIN32
#include <windows.h>   /* for CRITICAL_SECTION */
static CRITICAL_SECTION g_log_cs;
static int g_log_cs_init = 0;
#endif

static void dm_log_init(void) {
#ifdef _WIN32
    if (!g_log_cs_init) { InitializeCriticalSection(&g_log_cs); g_log_cs_init = 1; }
#endif
    g_log_fp = fopen("dm_debug.log", "a");
    if (!g_log_fp) return;
    /* Header line so runs are separated */
    fprintf(g_log_fp, "\n===== dm session start =====\n");
    fflush(g_log_fp);
}

static void dm_log(const char *fmt, ...) {
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

static void dm_log_close(void) {
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

/* WriteCtx extended: includes offset already on disk before this request */
typedef struct {
    FILE  *fp;
    int    download_id;
    int   *pause_flag;
    int   *cancel_flag;
    int64_t resume_offset;  /* bytes already on disk before CURLOPT_RESUME_FROM_LARGE */
    time_t  last_ui_tick;   /* wall-clock second of last GUI dispatch */
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
typedef struct {
    char filename[512];   /* set if Content-Disposition provides one */
    int  has_filename;
} HeaderCtx;

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
                        hctx->has_filename  = 1;
                        dm_log("[header_cb] parsed filename (quoted): %s", hctx->filename);
                    }
                } else {
                    /* unquoted value — ends at whitespace or ; */
                    size_t len = strcspn(fn, " ;\r\n");
                    if (len > 0) {
                        if (len >= sizeof(hctx->filename)) len = sizeof(hctx->filename) - 1;
                        memcpy(hctx->filename, fn, len);
                        hctx->filename[len] = '\0';
                        hctx->has_filename  = 1;
                        dm_log("[header_cb] parsed filename (unquoted): %s", hctx->filename);
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

    /* Pause if requested */
    if (*ctx->pause_flag) return CURL_PROGRESSFUNC_CONTINUE;

    /* Throttle GUI updates to once per second to avoid flooding uiQueueMain */
    time_t now = time(NULL);
    if (now == ctx->last_ui_tick) return 0;
    ctx->last_ui_tick = now;

    ludo_mutex_lock(&g_mgr.list_mutex);
    Download *d = NULL;
    for (Download *it = g_mgr.list; it; it = it->next) {
        if (it->status.id == ctx->download_id) { d = it; break; }
    }
    if (d) {
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
        d->status.state = DOWNLOAD_STATE_RUNNING;
    }
    ludo_mutex_unlock(&g_mgr.list_mutex);

    if (d) {
        ProgressUpdate upd = {0};
        upd.status.id       = ctx->download_id;
        upd.status.state    = DOWNLOAD_STATE_RUNNING;
        upd.status.progress = d->status.progress;
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
    size_t written = fwrite(ptr, size, nmemb, ctx->fp);
    if (written != nmemb) {
        dm_log("[write_cb] id=%d fwrite partial: expected %zu, wrote %zu (errno=%d)",
               ctx->download_id, nmemb, written, errno);
    }
    return written;
}

/* Perform the actual file download for entry `d` */
static void perform_download(Download *d) {
    dm_log("[perform_download] id=%d url=%s", d->status.id, d->url);
    dm_log("[perform_download] output_dir=%s  filename=%s", d->output_dir, d->status.filename);

    /* ---- Build output path ---- */
    const char *sep = "";
    size_t dlen = strlen(d->output_dir);
    if (dlen > 0) {
        char last = d->output_dir[dlen - 1];
        if (last != '/' && last != '\\') sep = "/";
    }
    char path[2048];
    snprintf(path, sizeof(path), "%s%s%s", d->output_dir, sep, d->status.filename);
    dm_log("[perform_download] id=%d output path=%s", d->status.id, path);

    /* ---- Resume support: check if partial file exists ---- */
    curl_off_t resume_from = 0;
    {
        struct stat st;
        if (stat(path, &st) == 0 && st.st_size > 0) {
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

    WriteCtx ctx = { fp, d->status.id, &pause_flag, &cancel_flag, (int64_t)resume_from, 0 };
    HeaderCtx hctx;
    memset(&hctx, 0, sizeof(hctx));

    CURL *curl = curl_easy_init();
    if (!curl) {
        dm_log("[perform_download] id=%d curl_easy_init() returned NULL", d->status.id);
        fclose(fp);
        return;
    }
    dm_log("[perform_download] id=%d curl handle ok, setting options", d->status.id);

    curl_easy_setopt(curl, CURLOPT_URL,               d->url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION,     write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA,         &ctx);
    curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION,  xfer_info_cb);
    curl_easy_setopt(curl, CURLOPT_XFERINFODATA,      &ctx);
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS,        0L);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION,    1L);
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS,         10L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT,
                     "LUDO/1.0 (LUa DOwnloader)");
    curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION,    header_cb);
    curl_easy_setopt(curl, CURLOPT_HEADERDATA,        &hctx);
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
    if (res == CURLE_OK && hctx.has_filename) {
        char new_path[2048];
        snprintf(new_path, sizeof(new_path), "%s%s%s",
                 d->output_dir, sep, hctx.filename);
        dm_log("[perform_download] id=%d renaming  %s  ->  %s", d->status.id, path, new_path);
        /* Only rename if different */
        if (strcmp(new_path, path) != 0) {
            fclose(fp);
            fp = NULL;
            int rv = rename(path, new_path);
            dm_log("[perform_download] id=%d rename() returned %d (errno=%d)",
                   d->status.id, rv, errno);
        } else {
            dm_log("[perform_download] id=%d paths identical, no rename needed", d->status.id);
        }
        ludo_mutex_lock(&g_mgr.list_mutex);
        strncpy(d->status.filename, hctx.filename, sizeof(d->status.filename) - 1);
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
    dm_log("[perform_download] id=%d curl cleaned up", d->status.id);

    /* Record finish time and final state */
    ludo_mutex_lock(&g_mgr.list_mutex);
    d->status.end_time = time(NULL);
    if (res == CURLE_OK) {
        d->status.state    = DOWNLOAD_STATE_COMPLETED;
        d->status.progress = 100.0;
    } else if (d->status.state != DOWNLOAD_STATE_PAUSED) {
        d->status.state = DOWNLOAD_STATE_FAILED;
    }
    DownloadState final_state = d->status.state;
    time_t start_ts  = d->status.start_time;
    time_t finish_ts = d->status.end_time;
    ludo_mutex_unlock(&g_mgr.list_mutex);

    dm_log("[perform_download] id=%d final state=%d filename=%s",
           d->status.id, (int)final_state, d->status.filename);

    ProgressUpdate upd = {0};
    upd.status.id         = d->status.id;
    upd.status.state      = final_state;
    upd.status.progress   = (final_state == DOWNLOAD_STATE_COMPLETED) ? 100.0 : d->status.progress;
    upd.status.start_time = start_ts;
    upd.status.end_time   = finish_ts;
    if (res != CURLE_OK) {
        strncpy(upd.error_msg, curl_easy_strerror(res), sizeof(upd.error_msg) - 1);
    }
    strncpy(upd.status.filename, d->status.filename, sizeof(upd.status.filename) - 1);
    gui_dispatch_update(&upd);
    dm_log("[perform_download] id=%d done", d->status.id);
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
    FILE *f = fopen(path, "rb");
    if (!f) return;

    char line[8192];
    while (fgets(line, sizeof(line), f)) {
        Download *d = (Download *)calloc(1, sizeof(Download));
        if (!d) break;

        int    state;
        long long start_ll, end_ll, total_ll, dl_ll;
        int parsed = sscanf(line,
            "%d\t%4095[^\t]\t%1023[^\t]\t%511[^\t]\t%d\t%lf\t%lf\t%lld\t%lld\t%lld\t%lld",
            &d->status.id,
            d->url,
            d->output_dir,
            d->status.filename,
            &state,
            &d->status.progress,
            &d->status.speed_bps,
            &total_ll,
            &dl_ll,
            &start_ll,
            &end_ll);

        if (parsed < 11) {
            free(d);
            continue;
        }

        d->status.state            = (DownloadState)state;
        d->status.total_bytes      = (int64_t)total_ll;
        d->status.downloaded_bytes = (int64_t)dl_ll;
        d->status.start_time       = (time_t)start_ll;
        d->status.end_time         = (time_t)end_ll;

        /* Keep next_id ahead of any loaded id */
        if (d->status.id >= g_mgr.next_id) g_mgr.next_id = d->status.id + 1;

        /* Prepend to list (order reversed, but acceptable) */
        d->next    = g_mgr.list;
        g_mgr.list = d;
    }
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
        /* Allocate new Download record */
        Download *d = (Download *)calloc(1, sizeof(Download));
        if (!d) continue;

        ludo_mutex_lock(&g_mgr.list_mutex);
        d->status.id = g_mgr.next_id++;
        strncpy(d->url, task.url, sizeof(d->url) - 1);
        /* Use per-task output_dir when provided, else the global default */
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
    dm_log_close();
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
        if (d->status.id == id && d->status.state == DOWNLOAD_STATE_RUNNING) {
            d->status.state = DOWNLOAD_STATE_PAUSED;
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
            task_queue_push(&g_mgr.queue, d->url);
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
            Download *to_free = *prev;
            *prev = to_free->next;
            free(to_free);
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
