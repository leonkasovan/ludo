#ifndef LUDO_DOWNLOAD_MANAGER_H
#define LUDO_DOWNLOAD_MANAGER_H

#include <stdint.h>
#include <stdio.h>
#include <stdbool.h>
#include "thread_queue.h"

/* ------------------------------------------------------------------ */
/* Download state and mode                                              */
/* ------------------------------------------------------------------ */

typedef enum {
    DOWNLOAD_NOW   = 0,  /* start immediately */
    DOWNLOAD_QUEUE = 1   /* add to end of queue */
} DownloadMode;

typedef enum {
    DOWNLOAD_STATE_QUEUED    = 0,
    DOWNLOAD_STATE_RUNNING   = 1,
    DOWNLOAD_STATE_PAUSED    = 2,
    DOWNLOAD_STATE_COMPLETED = 3,
    DOWNLOAD_STATE_FAILED    = 4
} DownloadState;

/* ------------------------------------------------------------------ */
/* Download item                                                        */
/* ------------------------------------------------------------------ */

/* Mutable runtime fields shared by Download, ProgressUpdate, and GUI rows.
   Embed this in both so field declarations only exist once. */
typedef struct {
    int            id;
    char           filename[512];     /* derived from URL or Content-Disposition */
    DownloadState  state;
    double         progress;          /* 0.0 – 100.0 */
    double         speed_bps;         /* bytes per second */
    int64_t        total_bytes;
    int64_t        downloaded_bytes;
    time_t         start_time;
    time_t         end_time;
} DownloadStatus;

typedef struct Download Download;

struct Download {
    DownloadStatus   status;
    char             url[4096];
    char             original_url[4096]; /* preserve user-supplied URL */
    char             output_dir[1024];
    long             preflight_status_code;
    int64_t          preflight_content_length;
    int              has_preflight;
    char             preflight_filename[512];
    void            *curl_handle;    /* opaque pointer to active CURL handle, if any */
    FILE            *fp;             /* active file handle for writing, if any */
    size_t           bytes_since_last_flush;
    volatile int     stop_requested;
    int              marked_for_removal;
    char             extra_headers[4096]; /* optional "Name: Value\n" pairs appended to GET request */
    Download        *next;
};

/* ------------------------------------------------------------------ */
/* Progress callback (invoked on main/GUI thread via uiQueueMain)       */
/* ------------------------------------------------------------------ */

typedef struct {
    DownloadStatus   status;
    char             error_msg[256];
    int              marked_for_removal; /* 1 = backend freed this download; GUI must delete its row */
} ProgressUpdate;

typedef struct {
    int  id;
    long status_code;  /* Real HTTP status from a synchronous HEAD preflight
                          (e.g. 200, 206, 403, 404).  0 if the probe could not
                          connect at all.  Only populated when result != NULL is
                          passed to download_manager_add(). */
    char output_path[2048]; /* provisional path based on URL-derived filename;
                               may be updated via progress callback when the
                               worker resolves the real Content-Disposition name */
} DownloadAddResult;

/* Callback registered by the GUI to receive progress updates */
typedef void (*progress_callback_t)(const ProgressUpdate *update, void *user_data);

/* ------------------------------------------------------------------ */
/* Manager API                                                          */
/* ------------------------------------------------------------------ */

/* Initialise with a number of concurrent worker threads.
   output_dir is the default download destination. */
void download_manager_init(int num_workers, const char *output_dir);
void download_manager_prepare_for_shutdown(void);
void download_manager_shutdown(void);

/* Queue a new download. Returns the assigned download ID (>0) or -1 on error.
   hint_filename: optional base filename (without directory) to use instead of
                  the URL-derived name. Pass NULL to keep the default behaviour.
   When result is non-NULL, fills it with preflight status/output-path data. */
int  download_manager_add(const char *url, const char *output_dir, DownloadMode mode,
                          const char *original_url, const char *hint_filename,
                          const char *extra_headers,
                          DownloadAddResult *result);

/* Pause / resume / remove — may be called from the GUI thread */
bool download_manager_pause(int id);
bool download_manager_resume(int id);
bool download_manager_remove(int id);

/* Access the global list (call only from GUI/main thread) */
Download *download_manager_get_list(void);

/* Register a progress callback invoked (via uiQueueMain) on the GUI thread */
void download_manager_set_progress_cb(progress_callback_t cb, void *user_data);

/* Retrieve configured default output directory */
const char *download_manager_get_output_dir(void);

/* Update configured default output directory for future downloads */
void download_manager_set_output_dir(const char *output_dir);

/* Lookup a single download by id (thread-safe read) */
int download_manager_find_status(int id, DownloadStatus *out);

/* Re-dispatch the current status of all downloads to the GUI */
void download_manager_sync_ui(void);

#endif /* LUDO_DOWNLOAD_MANAGER_H */
