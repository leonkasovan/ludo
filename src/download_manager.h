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

/* Mutable runtime fields shared by Download and ProgressUpdate.
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

typedef struct Download {
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
    struct Download *next;
} Download;

/* ------------------------------------------------------------------ */
/* Progress callback (invoked on main/GUI thread via uiQueueMain)       */
/* ------------------------------------------------------------------ */

typedef struct {
    DownloadStatus   status;
    char             error_msg[256];
} ProgressUpdate;

typedef struct {
    int  id;
    long status_code;
    char output_path[2048];
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
   When result is non-NULL, fills it with preflight status/output-path data. */
int  download_manager_add(const char *url, const char *output_dir, DownloadMode mode,
                          const char *original_url, DownloadAddResult *result);

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
Download *download_manager_find(int id);

/* Re-dispatch the current status of all downloads to the GUI */
void download_manager_sync_ui(void);

#endif /* LUDO_DOWNLOAD_MANAGER_H */
