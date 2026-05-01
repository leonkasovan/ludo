#include "console_log.h"

#include <stdarg.h>
#include <stdio.h>
#include <time.h>

/* Console-mode logging: writes timestamped messages to stderr/stdout.
 * Called from both main thread and worker threads. */
void gui_log(LogLevel level, const char *fmt, ...) {
    const char *prefix;
    FILE *out;

    switch (level) {
    case LOG_INFO:    prefix = "INFO";  out = stdout; break;
    case LOG_SUCCESS: prefix = "OK";    out = stdout; break;
    case LOG_WARNING: prefix = "WARN";  out = stderr; break;
    case LOG_ERROR:   prefix = "ERROR"; out = stderr; break;
    default:          prefix = "LOG";   out = stderr; break;
    }

    time_t now = time(NULL);
    struct tm *ptm = localtime(&now);
    char ts[32] = "";
    if (ptm)
        strftime(ts, sizeof(ts), "%H:%M:%S", ptm);

    fprintf(out, "[%s] [%s] ", ts, prefix);

    va_list ap;
    va_start(ap, fmt);
    vfprintf(out, fmt, ap);
    va_end(ap);

    fprintf(out, "\n");
    fflush(out);
}

/* In console mode, no UI event loop — execute callback directly. */
void uiQueueMain(void (*f)(void *data), void *data) {
    if (f) f(data);
}
