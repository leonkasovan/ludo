#ifndef DM_LOG_H
#define DM_LOG_H

#include <stdarg.h>

#ifdef __cplusplus
extern "C" {
#endif

void dm_log_init(void);
void dm_log(const char *fmt, ...);
void dm_log_close(void);

#ifdef __cplusplus
}
#endif

#endif // DM_LOG_H
