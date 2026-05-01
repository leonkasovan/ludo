#ifndef LUDO_CONSOLE_LOG_H
#define LUDO_CONSOLE_LOG_H

/* Console-mode replacements for gui.h / ui.h functions.
 * Built when BUILD_CONSOLE is defined (ludocon target). */

typedef enum {
    LOG_INFO    = 0,
    LOG_SUCCESS = 1,
    LOG_WARNING = 2,
    LOG_ERROR   = 3
} LogLevel;

void gui_log(LogLevel level, const char *fmt, ...);

/* Stub for uiQueueMain — in console mode we don't have a GUI event loop,
 * so just call the callback directly on the calling thread. */
void uiQueueMain(void (*f)(void *data), void *data);

#endif
