#include "gui.h"
#include "lua_engine.h"
#include "download_manager.h"
#include "thread_queue.h"

#include "ui.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* ------------------------------------------------------------------ */
/* Constants                                                            */
/* ------------------------------------------------------------------ */

#define MAX_DOWNLOAD_ROWS 64

/* ------------------------------------------------------------------ */
/* Download row UI element                                              */
/* ------------------------------------------------------------------ */

typedef struct {
    int             download_id;
    uiBox          *row_box;       /* horizontal container for this row */
    uiLabel        *label_name;    /* filename or URL snippet           */
    uiLabel        *label_status;  /* status text                       */
    uiProgressBar  *progress_bar;
} DownloadRow;

/* ------------------------------------------------------------------ */
/* GUI state                                                            */
/* ------------------------------------------------------------------ */

static struct {
    uiWindow        *window;
    uiEntry         *url_entry;
    uiButton        *add_btn;
    uiBox           *downloads_box; /* vbox; one row per download       */
    uiMultilineEntry *log_view;

    DownloadRow      rows[MAX_DOWNLOAD_ROWS];
    int              row_count;

    ludo_mutex_t     log_mutex;
} g_gui;

/* Task queue shared with worker threads (initialised in main.c) */
extern TaskQueue g_url_queue;

/* ------------------------------------------------------------------ */
/* Worker thread: pulls URLs, drives Lua engine                         */
/* ------------------------------------------------------------------ */

static void *url_worker_thread(void *arg) {
    (void)arg;
    URLTask task;
    while (task_queue_pop(&g_url_queue, &task)) {
        int handled = lua_engine_process_url(task.url);
        if (!handled) {
            /* No plugin matched: attempt a direct download */
            char msg[512];
            snprintf(msg, sizeof(msg),
                     "[ludo] No plugin for URL — starting direct download: %s",
                     task.url);
            gui_log(LOG_INFO, msg);
            download_manager_add(task.url,
                                 download_manager_get_output_dir(),
                                 DOWNLOAD_NOW);
        }
    }
    return NULL;
}

/* ------------------------------------------------------------------ */
/* uiQueueMain helpers                                                  */
/* ------------------------------------------------------------------ */

typedef struct { LogLevel level; char msg[1024]; } LogPkt;

static void log_on_main(void *data) {
    LogPkt *pkt = (LogPkt *)data;

    const char *prefix;
    switch (pkt->level) {
        case LOG_SUCCESS: prefix = "[OK]   "; break;
        case LOG_ERROR:   prefix = "[ERR]  "; break;
        default:          prefix = "[INFO] "; break;
    }

    /* Build timestamped line */
    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    char line[1200];
    snprintf(line, sizeof(line), "%02d:%02d:%02d %s%s\n",
             t->tm_hour, t->tm_min, t->tm_sec,
             prefix, pkt->msg);

    uiMultilineEntryAppend(g_gui.log_view, line);
    free(pkt);
}

void gui_log(LogLevel level, const char *msg) {
    LogPkt *pkt = (LogPkt *)malloc(sizeof(LogPkt));
    if (!pkt) return;
    pkt->level = level;
    strncpy(pkt->msg, msg, sizeof(pkt->msg) - 1);
    pkt->msg[sizeof(pkt->msg) - 1] = '\0';
    uiQueueMain(log_on_main, pkt);
}

/* ------------------------------------------------------------------ */
/* Download row management (main thread only)                          */
/* ------------------------------------------------------------------ */

static DownloadRow *find_row(int id) {
    for (int i = 0; i < g_gui.row_count; i++) {
        if (g_gui.rows[i].download_id == id) return &g_gui.rows[i];
    }
    return NULL;
}

static DownloadRow *add_row(int id, const char *filename) {
    if (g_gui.row_count >= MAX_DOWNLOAD_ROWS) return NULL;

    DownloadRow *r = &g_gui.rows[g_gui.row_count++];
    r->download_id = id;

    /* Row: [filename label (stretchy)] [status label] [progress bar] */
    r->row_box = uiNewHorizontalBox();
    uiBoxSetPadded(r->row_box, 1);

    r->label_name = uiNewLabel(filename[0] ? filename : "...");
    uiBoxAppend(r->row_box, uiControl(r->label_name), 1 /* stretchy */);

    r->label_status = uiNewLabel("Queued");
    uiBoxAppend(r->row_box, uiControl(r->label_status), 0);

    r->progress_bar = uiNewProgressBar();
    uiProgressBarSetValue(r->progress_bar, 0);
    uiBoxAppend(r->row_box, uiControl(r->progress_bar), 0);

    uiBoxAppend(g_gui.downloads_box, uiControl(r->row_box), 0);
    return r;
}

/* ------------------------------------------------------------------ */
/* Progress callback (invoked on main thread via uiQueueMain)          */
/* ------------------------------------------------------------------ */

void gui_on_progress(const ProgressUpdate *update, void *user_data) {
    (void)user_data;

    DownloadRow *r = find_row(update->download_id);
    if (!r) {
        /* New entry — create a row for it */
        r = add_row(update->download_id, update->filename);
        if (!r) return;
    }

    /* Update filename label if we now know it */
    if (update->filename[0]) {
        uiLabelSetText(r->label_name, update->filename);
    }

    /* Progress bar (0–100) */
    int pct = (int)update->progress;
    if (pct < 0)   pct = 0;
    if (pct > 100) pct = 100;
    uiProgressBarSetValue(r->progress_bar, pct);

    /* Status label */
    switch (update->state) {
        case DOWNLOAD_STATE_QUEUED:
            uiLabelSetText(r->label_status, "Queued");
            break;
        case DOWNLOAD_STATE_RUNNING: {
            char buf[64];
            snprintf(buf, sizeof(buf), "%d%%", pct);
            uiLabelSetText(r->label_status, buf);
            break;
        }
        case DOWNLOAD_STATE_PAUSED:
            uiLabelSetText(r->label_status, "Paused");
            break;
        case DOWNLOAD_STATE_COMPLETED:
            uiLabelSetText(r->label_status, "Done");
            uiProgressBarSetValue(r->progress_bar, 100);
            break;
        case DOWNLOAD_STATE_FAILED: {
            char buf[288];
            snprintf(buf, sizeof(buf), "Failed: %s", update->error_msg);
            uiLabelSetText(r->label_status, buf);
            uiProgressBarSetValue(r->progress_bar, -1); /* indeterminate */
            break;
        }
    }
}

/* ------------------------------------------------------------------ */
/* Button callback                                                      */
/* ------------------------------------------------------------------ */

static void on_add_clicked(uiButton *sender, void *data) {
    (void)sender; (void)data;

    char *url_text = uiEntryText(g_gui.url_entry);
    if (!url_text || url_text[0] == '\0') {
        uiFreeText(url_text);
        return;
    }

    /* Trim leading/trailing whitespace */
    char url[4096];
    strncpy(url, url_text, sizeof(url) - 1);
    url[sizeof(url) - 1] = '\0';
    uiFreeText(url_text);

    /* Reject obviously non-URL strings */
    if (strncmp(url, "http://", 7) != 0 &&
        strncmp(url, "https://", 8) != 0)
    {
        gui_log(LOG_ERROR, "Invalid URL — must start with http:// or https://");
        return;
    }

    /* Push to the worker queue */
    task_queue_push(&g_url_queue, url);

    /* Clear the entry field */
    uiEntrySetText(g_gui.url_entry, "");

    char msg[512];
    snprintf(msg, sizeof(msg), "Queued: %s", url);
    gui_log(LOG_INFO, msg);
}

/* ------------------------------------------------------------------ */
/* Window close callback                                                */
/* ------------------------------------------------------------------ */

static int on_window_close(uiWindow *w, void *data) {
    (void)w; (void)data;
    uiQuit();
    return 1; /* destroy window */
}

static int on_should_quit(void *data) {
    (void)data;
    uiControlDestroy(uiControl(g_gui.window));
    return 1;
}

/* ------------------------------------------------------------------ */
/* gui_create                                                           */
/* ------------------------------------------------------------------ */

void gui_create(void) {
    /* Outer window */
    g_gui.window = uiNewWindow("LUDO - LUa DOwnloader", 800, 600, 0);
    uiWindowSetMargined(g_gui.window, 1);
    uiWindowOnClosing(g_gui.window, on_window_close, NULL);
    uiOnShouldQuit(on_should_quit, NULL);

    /* Root vertical box */
    uiBox *root = uiNewVerticalBox();
    uiBoxSetPadded(root, 1);

    /* ---- URL input row ---- */
    uiBox *input_row = uiNewHorizontalBox();
    uiBoxSetPadded(input_row, 1);

    g_gui.url_entry = uiNewEntry();
    uiEntrySetText(g_gui.url_entry, "https://");
    uiBoxAppend(input_row, uiControl(g_gui.url_entry), 1 /* stretchy */);

    g_gui.add_btn = uiNewButton("Add Download");
    uiButtonOnClicked(g_gui.add_btn, on_add_clicked, NULL);
    uiBoxAppend(input_row, uiControl(g_gui.add_btn), 0);

    uiBoxAppend(root, uiControl(input_row), 0);

    /* ---- Downloads list ---- */
    g_gui.downloads_box = uiNewVerticalBox();
    uiBoxSetPadded(g_gui.downloads_box, 1);

    /* Wrap in a scrollable area via a non-wrapping multiline entry is not
       ideal, but libui-ng lacks a ScrollView for arbitrary controls.
       Instead we place at most MAX_DOWNLOAD_ROWS rows and rely on the
       window being resizable. */
    uiBoxAppend(root, uiControl(g_gui.downloads_box), 0);

    /* ---- Separator label ---- */
    uiBoxAppend(root, uiControl(uiNewLabel("── Log ──")), 0);

    /* ---- Log area ---- */
    g_gui.log_view = uiNewMultilineEntry();
    uiMultilineEntrySetReadOnly(g_gui.log_view, 1);
    uiBoxAppend(root, uiControl(g_gui.log_view), 1 /* stretchy */);

    uiWindowSetChild(g_gui.window, uiControl(root));
    uiControlShow(uiControl(g_gui.window));

    /* Register progress callback with the download manager */
    download_manager_set_progress_cb(gui_on_progress, NULL);

    /* Start a worker thread that feeds URLs from the queue to the Lua engine */
    ludo_thread_t worker;
    ludo_thread_create(&worker, url_worker_thread, NULL);
    /* We intentionally don't join this thread — it exits when the queue
       is shut down during download_manager_shutdown(). */
}
