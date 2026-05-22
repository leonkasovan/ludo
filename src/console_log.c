#include "console_log.h"
#include "dm_log.h"

#include <stdarg.h>
#include <stdio.h>
#include <time.h>

/* Console-mode logging: writes timestamped messages to stderr/stdout
 * and to ludo.log. Called from both main thread and worker threads. */
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

    char msg[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);

    fprintf(out, "[%s] [%s] %s\n", ts, prefix, msg);
    fflush(out);

    static const char *log_prefixes[] = { "[INFO] ", "[SUCCESS] ", "[WARNING] ", "[ERROR]  " };
    dm_log("%s%s", log_prefixes[level], msg);
}

/* In console mode, no UI event loop — execute callback directly. */
void uiQueueMain(void (*f)(void *data), void *data) {
    if (f) f(data);
}
