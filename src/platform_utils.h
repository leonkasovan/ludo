#ifndef LUDO_PLATFORM_UTILS_H
#define LUDO_PLATFORM_UTILS_H

/* platform_utils.h — shared Windows UTF-8/UTF-16 conversion helpers.
 *
 * These functions are used by gui.c, lua_engine.c, and main.c.
 * Keeping them in one place ensures bug-fixes propagate everywhere.
 */

#include <stddef.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

/* UTF-8 aware fopen — on Windows wraps _wfopen, on POSIX wraps fopen. */
FILE *fopen_utf8(const char *path, const char *mode);

#ifdef _WIN32
#include <windows.h>

/* Allocate and return a NUL-terminated wide-char copy of src (UTF-8 -> UTF-16).
   Caller must free() the returned pointer. Returns NULL on failure. */
wchar_t *utf8_to_wide_dup(const char *src);

/* Convert src (UTF-16) to dst (UTF-8), writing at most dst_sz bytes including
   the NUL terminator.  Returns 1 on success, 0 on failure. */
int wide_to_utf8(const wchar_t *src, char *dst, size_t dst_sz);

#endif /* _WIN32 */

#ifdef __cplusplus
}
#endif

#endif /* LUDO_PLATFORM_UTILS_H */
