#ifndef LUDO_WIN_TOOLBAR_ICONS_H
#define LUDO_WIN_TOOLBAR_ICONS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Returns opaque context pointer, or NULL on failure. */
void *ludo_icons_init(void);

/* Applies a PNG image to a native Win32 button HWND. */
void ludo_icons_set_button_png(void *ctx, uintptr_t hwnd, const char *png_path_utf8);

/* Force native Win32 button dimensions (pixels). */
void ludo_icons_set_button_size(uintptr_t hwnd, int width, int height);

/* Frees all loaded icon bitmaps and shuts down GDI+. */
void ludo_icons_shutdown(void *ctx);

#ifdef __cplusplus
}
#endif

#endif /* LUDO_WIN_TOOLBAR_ICONS_H */
