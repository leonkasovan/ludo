#include "platform_utils.h"
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32

#include <wchar.h>

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

int chdir_to_app_dir(void) {
    wchar_t exe[4096];
    DWORD len = GetModuleFileNameW(NULL, exe, (DWORD)(sizeof(exe) / sizeof(exe[0])));
    wchar_t *slash;

    if (len == 0 || len >= sizeof(exe) / sizeof(exe[0])) return 0;
    slash = wcsrchr(exe, L'\\');
    if (!slash) return 0;
    *slash = L'\0';
    return SetCurrentDirectoryW(exe) ? 1 : 0;
}

int path_absolute(const char *path, char *dst, size_t dst_sz) {
    wchar_t *wpath;
    wchar_t *wfull;
    DWORD needed;
    int ok = 0;

    if (!path || !dst || dst_sz == 0) return 0;
    wpath = utf8_to_wide_dup(path);
    if (!wpath) return 0;

    needed = GetFullPathNameW(wpath, 0, NULL, NULL);
    wfull = needed ? (wchar_t *)malloc((size_t)needed * sizeof(wchar_t)) : NULL;
    if (wfull && GetFullPathNameW(wpath, needed, wfull, NULL) > 0)
        ok = wide_to_utf8(wfull, dst, dst_sz);

    free(wfull);
    free(wpath);
    return ok;
}

#else /* !_WIN32 */

#include <unistd.h>

int chdir_to_app_dir(void) {
    char exe[4096];
    ssize_t len = readlink("/proc/self/exe", exe, sizeof(exe) - 1);
    char *slash;

    if (len <= 0) return 0;
    exe[len] = '\0';
    slash = strrchr(exe, '/');
    if (!slash) return 0;
    *slash = '\0';
    return chdir(exe) == 0;
}

int path_absolute(const char *path, char *dst, size_t dst_sz) {
    char cwd[4096];

    if (!path || !dst || dst_sz == 0) return 0;
    if (path[0] == '/') {
        if (strlen(path) + 1 > dst_sz) return 0;
        strcpy(dst, path);
        return 1;
    }
    if (!getcwd(cwd, sizeof(cwd))) return 0;
    if (strlen(cwd) + 1 + strlen(path) + 1 > dst_sz) return 0;
    snprintf(dst, dst_sz, "%s/%s", cwd, path);
    return 1;
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
