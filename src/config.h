#ifndef LUDO_CONFIG_H
#define LUDO_CONFIG_H

#include <stddef.h>

#define LUDO_GUI_DOWNLOADS_TABLE_COLUMN_COUNT 8
#define LUDO_GUI_SNIPPET_TABLE_COLUMN_COUNT   1

typedef enum {
    LUDO_GUI_WINDOW_MAIN = 0,
    LUDO_GUI_WINDOW_ADD_URLS,
    LUDO_GUI_WINDOW_HTTP_TEST,
    LUDO_GUI_WINDOW_LUA_TEST
} LudoGuiWindowId;

typedef enum {
    LUDO_GUI_TABLE_DOWNLOADS = 0,
    LUDO_GUI_TABLE_SNIPPETS
} LudoGuiTableId;

typedef struct {
    int width;
    int height;
    int pos_x;
    int pos_y;
} LudoGuiWindowConfig;

typedef struct {
    LudoGuiWindowConfig main_window;
    LudoGuiWindowConfig add_urls_window;
    LudoGuiWindowConfig http_test_window;
    LudoGuiWindowConfig lua_test_window;
    int downloads_table_widths[LUDO_GUI_DOWNLOADS_TABLE_COLUMN_COUNT];
    int snippet_table_widths[LUDO_GUI_SNIPPET_TABLE_COLUMN_COUNT];
} LudoGuiConfig;

typedef struct {
    int  max_download_retry;
    int  max_thread;
    int  url_queue_capacity;
    int  download_queue_capacity;
    int  max_redirect;
    char output_dir[1024];
    char plugin_dir[1024];
    LudoGuiConfig gui;
} LudoConfig;

int ludo_config_init(const char *path);
void ludo_config_shutdown(void);

const LudoConfig *ludo_config_get(void);
const char *ludo_config_path(void);

int ludo_config_save(void);
int ludo_config_set_output_dir(const char *output_dir);
void ludo_config_set_window_position(LudoGuiWindowId window_id, int x, int y);
void ludo_config_set_window_size(LudoGuiWindowId window_id, int width, int height);
void ludo_config_set_table_column_width(LudoGuiTableId table_id, int column, int width);

#endif /* LUDO_CONFIG_H */
