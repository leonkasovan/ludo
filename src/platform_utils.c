#include "platform_utils.h"
#include <stdlib.h>

#ifdef _WIN32

wchar_t *utf8_to_wide_dup(const char *src) {
    int needed;
    wchar_t *dst;

    if (!src) return NULL;
    needed = MultiByteToWideChar(CP_UTF8, 0, src, -1, NULL, 0);
    if (needed <= 0) return NULL;
    dst = (wchar_t *)malloc((size_t)needed * sizeof(wchar_t));
    if (!dst) return NULL;
    if (MultiByteToWideChar(CP_UTF8, 0, src, -1, dst, needed) <= 0) {
        free(dst);
        return NULL;
    }
    return dst;
}

int wide_to_utf8(const wchar_t *src, char *dst, size_t dst_sz) {
    int needed;

    if (!src || !dst || dst_sz == 0) return 0;
    needed = WideCharToMultiByte(CP_UTF8, 0, src, -1, NULL, 0, NULL, NULL);
    if (needed <= 0 || (size_t)needed > dst_sz) return 0;
    return WideCharToMultiByte(CP_UTF8, 0, src, -1, dst, (int)dst_sz, NULL, NULL) > 0;
}

#endif /* _WIN32 */

FILE *fopen_utf8(const char *path, const char *mode) {
#ifdef _WIN32
    FILE *fp = NULL;
    wchar_t *wpath = utf8_to_wide_dup(path);
    wchar_t wmode[8] = {0};
    size_t i;

    if (!wpath) return NULL;
    for (i = 0; mode[i] != '\0' && i + 1 < 7; i++) {
        wmode[i] = (wchar_t)(unsigned char)mode[i];
    }
    fp = _wfopen(wpath, wmode);
    free(wpath);
    return fp;
#else
    return fopen(path, mode);
#endif
}
