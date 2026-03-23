#ifndef LUDO_GUI_H
#define LUDO_GUI_H

#include "download_manager.h"

/* Log message severity */
typedef enum {
    LOG_INFO    = 0,
    LOG_SUCCESS = 1,
    LOG_WARNING = 2,
    LOG_ERROR   = 3
} LogLevel;

/*
 * Build and display the main application window.
 * Must be called from the GUI (main) thread after uiInit().
 */
void gui_create(void);

/*
 * Append a message to the on-screen log panel.
 * Thread-safe: routes to the main thread via uiQueueMain when needed.
 */
void gui_log(LogLevel level, const char *fmt, ...);

/*
 * Progress update callback — registered with download_manager.
 * Called on the GUI thread via uiQueueMain.
 */
void gui_on_progress(const ProgressUpdate *update, void *user_data);

#endif /* LUDO_GUI_H */
