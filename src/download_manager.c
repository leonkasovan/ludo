#include "download_manager.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <curl/curl.h>
#include "ui.h"

/* ------------------------------------------------------------------ */
/* Internal types                                                       */
/* ------------------------------------------------------------------ */

/* Data passed to curl write callback for file streaming */
typedef struct {
    FILE  *fp;
    int    download_id;
    int   *pause_flag;
    int   *cancel_flag;
} WriteCtx;

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

    ludo_mutex_lock(&g_mgr.list_mutex);
    Download *d = NULL;
    for (Download *it = g_mgr.list; it; it = it->next) {
        if (it->id == ctx->download_id) { d = it; break; }
    }
    if (d) {
        d->downloaded_bytes = (int64_t)dlnow;
        d->total_bytes      = (int64_t)dltotal;
        d->progress         = (dltotal > 0)
                                ? (100.0 * (double)dlnow / (double)dltotal)
                                : 0.0;
        d->state            = DOWNLOAD_STATE_RUNNING;
    }
    ludo_mutex_unlock(&g_mgr.list_mutex);

    if (d) {
        ProgressUpdate upd = {0};
        upd.download_id = ctx->download_id;
        upd.state       = DOWNLOAD_STATE_RUNNING;
        upd.progress    = d->progress;
        gui_dispatch_update(&upd);
    }

    return 0;
}

/* curl write callback — streams bytes straight to disk */
static size_t write_cb(char *ptr, size_t size, size_t nmemb, void *userdata) {
    WriteCtx *ctx = (WriteCtx *)userdata;
    if (*ctx->cancel_flag) return 0;
    return fwrite(ptr, size, nmemb, ctx->fp);
}

/* Perform the actual file download for entry `d` */
static void perform_download(Download *d) {
    /* Build output path */
    char path[2048];
    snprintf(path, sizeof(path), "%s%s%s",
             d->output_dir,
             (d->output_dir[strlen(d->output_dir) - 1] == '/' ||
              d->output_dir[strlen(d->output_dir) - 1] == '\\') ? "" : "/",
             d->filename);

    FILE *fp = fopen(path, "wb");
    if (!fp) {
        ProgressUpdate upd = {0};
        upd.download_id = d->id;
        upd.state       = DOWNLOAD_STATE_FAILED;
        snprintf(upd.error_msg, sizeof(upd.error_msg),
                 "Cannot open output file: %s", path);
        ludo_mutex_lock(&g_mgr.list_mutex);
        d->state = DOWNLOAD_STATE_FAILED;
        ludo_mutex_unlock(&g_mgr.list_mutex);
        gui_dispatch_update(&upd);
        return;
    }

    int pause_flag  = 0;
    int cancel_flag = 0;

    WriteCtx ctx = { fp, d->id, &pause_flag, &cancel_flag };

    CURL *curl = curl_easy_init();
    if (!curl) {
        fclose(fp);
        return;
    }

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
    /* Allow resuming downloads */
    /* (we don't handle resume logic here for brevity) */

    CURLcode res = curl_easy_perform(curl);

    fclose(fp);
    curl_easy_cleanup(curl);

    ludo_mutex_lock(&g_mgr.list_mutex);
    if (res == CURLE_OK) {
        d->state    = DOWNLOAD_STATE_COMPLETED;
        d->progress = 100.0;
    } else if (d->state != DOWNLOAD_STATE_PAUSED) {
        d->state = DOWNLOAD_STATE_FAILED;
    }
    DownloadState final_state = d->state;
    ludo_mutex_unlock(&g_mgr.list_mutex);

    ProgressUpdate upd = {0};
    upd.download_id = d->id;
    upd.state       = final_state;
    upd.progress    = (final_state == DOWNLOAD_STATE_COMPLETED) ? 100.0 : d->progress;
    if (res != CURLE_OK) {
        strncpy(upd.error_msg, curl_easy_strerror(res), sizeof(upd.error_msg) - 1);
    }
    snprintf(upd.filename, sizeof(upd.filename), "%s", d->filename);
    gui_dispatch_update(&upd);
}

/* Worker thread: pull tasks from queue and download them */
static void *worker_thread(void *arg) {
    (void)arg;
    URLTask task;
    while (task_queue_pop(&g_mgr.queue, &task)) {
        /* Allocate new Download record */
        Download *d = (Download *)calloc(1, sizeof(Download));
        if (!d) continue;

        ludo_mutex_lock(&g_mgr.list_mutex);
        d->id    = g_mgr.next_id++;
        strncpy(d->url, task.url, sizeof(d->url) - 1);
        /* Use per-task output_dir when provided, else the global default */
        if (task.output_dir[0] != '\0')
            strncpy(d->output_dir, task.output_dir, sizeof(d->output_dir) - 1);
        else
            strncpy(d->output_dir, g_mgr.output_dir, sizeof(d->output_dir) - 1);
        filename_from_url(task.url, d->filename, sizeof(d->filename));
        d->state = DOWNLOAD_STATE_QUEUED;

        /* Prepend to list */
        d->next      = g_mgr.list;
        g_mgr.list   = d;
        ludo_mutex_unlock(&g_mgr.list_mutex);

        /* Notify GUI of new entry */
        ProgressUpdate upd = {0};
        upd.download_id = d->id;
        upd.state       = DOWNLOAD_STATE_QUEUED;
        upd.progress    = 0.0;
        strncpy(upd.filename, d->filename, sizeof(upd.filename) - 1);
        gui_dispatch_update(&upd);

        perform_download(d);
    }
    return NULL;
}

/* ------------------------------------------------------------------ */
/* Public API                                                           */
/* ------------------------------------------------------------------ */

void download_manager_init(int num_workers, const char *output_dir) {
    memset(&g_mgr, 0, sizeof(g_mgr));
    g_mgr.next_id     = 1;
    g_mgr.num_workers = num_workers;
    g_mgr.running     = 1;

    strncpy(g_mgr.output_dir, output_dir ? output_dir : "./downloads/",
            sizeof(g_mgr.output_dir) - 1);

    ludo_mutex_init(&g_mgr.list_mutex);
    task_queue_init(&g_mgr.queue, 256);

    curl_global_init(CURL_GLOBAL_DEFAULT);

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
        if (d->id == id && d->state == DOWNLOAD_STATE_RUNNING) {
            d->state = DOWNLOAD_STATE_PAUSED;
            break;
        }
    }
    ludo_mutex_unlock(&g_mgr.list_mutex);
}

void download_manager_resume(int id) {
    ludo_mutex_lock(&g_mgr.list_mutex);
    for (Download *d = g_mgr.list; d; d = d->next) {
        if (d->id == id && d->state == DOWNLOAD_STATE_PAUSED) {
            d->state = DOWNLOAD_STATE_QUEUED;
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
        if ((*prev)->id == id) {
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

Download *download_manager_find(int id) {
    ludo_mutex_lock(&g_mgr.list_mutex);
    Download *found = NULL;
    for (Download *d = g_mgr.list; d; d = d->next) {
        if (d->id == id) { found = d; break; }
    }
    ludo_mutex_unlock(&g_mgr.list_mutex);
    return found;
}
