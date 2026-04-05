#include "config.h"
#include "platform_utils.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static struct {
    LudoConfig config;
    char       path[1024];
    int        initialized;
    int        dirty;
} g_config;

static void gui_window_set_defaults(LudoGuiWindowConfig *window, int width, int height) {
    if (!window) return;

    window->width = width;
    window->height = height;
    window->pos_x = -1;
    window->pos_y = -1;
}

static void config_set_defaults(LudoConfig *cfg) {
    static const int default_download_widths[LUDO_GUI_DOWNLOADS_TABLE_COLUMN_COUNT] = {
        20, 228, 100, 70, 70, 80, 121, 25
    };

    if (!cfg) return;

    memset(cfg, 0, sizeof(*cfg));
    cfg->max_download_retry    = 3;
    cfg->max_thread            = 2;
    cfg->url_queue_capacity    = 256;
    cfg->download_queue_capacity = 256;
    cfg->max_redirect          = 10;
    strncpy(cfg->output_dir, "downloads/", sizeof(cfg->output_dir) - 1);
    strncpy(cfg->plugin_dir, "plugins", sizeof(cfg->plugin_dir) - 1);
    gui_window_set_defaults(&cfg->gui.main_window, 800, 600);
    gui_window_set_defaults(&cfg->gui.add_urls_window, 700, 400);
    gui_window_set_defaults(&cfg->gui.http_test_window, 900, 700);
    gui_window_set_defaults(&cfg->gui.lua_test_window, 960, 640);
    memcpy(cfg->gui.downloads_table_widths,
           default_download_widths,
           sizeof(cfg->gui.downloads_table_widths));
    cfg->gui.snippet_table_widths[0] = 220;
}

static char *trim(char *s) {
    char *end;

    if (!s) return s;
    while (*s && isspace((unsigned char)*s)) s++;
    if (*s == '\0') return s;

    end = s + strlen(s) - 1;
    while (end > s && isspace((unsigned char)*end)) {
        *end = '\0';
        end--;
    }
    return s;
}

static int key_equals(const char *lhs, const char *rhs) {
    unsigned char a;
    unsigned char b;

    if (!lhs || !rhs) return 0;
    while (*lhs && *rhs) {
        a = (unsigned char)*lhs++;
        b = (unsigned char)*rhs++;
        if (tolower(a) != tolower(b)) return 0;
    }
    return *lhs == '\0' && *rhs == '\0';
}

static int clamp_min(int value, int min_value) {
    return value < min_value ? min_value : value;
}

static LudoGuiWindowConfig *config_window_mut(LudoConfig *cfg, LudoGuiWindowId window_id) {
    if (!cfg) return NULL;

    switch (window_id) {
    case LUDO_GUI_WINDOW_MAIN:
        return &cfg->gui.main_window;
    case LUDO_GUI_WINDOW_ADD_URLS:
        return &cfg->gui.add_urls_window;
    case LUDO_GUI_WINDOW_HTTP_TEST:
        return &cfg->gui.http_test_window;
    case LUDO_GUI_WINDOW_LUA_TEST:
        return &cfg->gui.lua_test_window;
    default:
        return NULL;
    }
}

static int *config_table_widths_mut(LudoConfig *cfg, LudoGuiTableId table_id, size_t *count) {
    if (count) *count = 0;
    if (!cfg) return NULL;

    switch (table_id) {
    case LUDO_GUI_TABLE_DOWNLOADS:
        if (count) *count = LUDO_GUI_DOWNLOADS_TABLE_COLUMN_COUNT;
        return cfg->gui.downloads_table_widths;
    case LUDO_GUI_TABLE_SNIPPETS:
        if (count) *count = LUDO_GUI_SNIPPET_TABLE_COLUMN_COUNT;
        return cfg->gui.snippet_table_widths;
    default:
        return NULL;
    }
}

static int key_matches_window_field(const char *key, const char *prefix, const char *suffix) {
    char expected_key[64];

    if (!key || !prefix || !suffix) return 0;
    if (snprintf(expected_key, sizeof(expected_key), "%s%s", prefix, suffix) < 0) return 0;
    return key_equals(key, expected_key);
}

static int config_try_apply_window_kv(LudoGuiWindowConfig *window,
                                      const char *prefix,
                                      const char *key,
                                      const char *value) {
    int parsed;

    if (!window || !prefix || !key || !value) return 0;

    parsed = atoi(value);
    if (key_matches_window_field(key, prefix, "Width")) {
        if (parsed >= 240) window->width = parsed;
        return 1;
    }
    if (key_matches_window_field(key, prefix, "Height")) {
        if (parsed >= 160) window->height = parsed;
        return 1;
    }
    if (key_matches_window_field(key, prefix, "PosX")) {
        window->pos_x = parsed;
        return 1;
    }
    if (key_matches_window_field(key, prefix, "PosY")) {
        window->pos_y = parsed;
        return 1;
    }

    return 0;
}

static int config_try_apply_table_kv(int *widths,
                                     size_t count,
                                     const char *prefix,
                                     const char *key,
                                     const char *value) {
    char expected_key[64];
    int parsed;

    if (!widths || count == 0 || !prefix || !key || !value) return 0;

    parsed = atoi(value);
    for (size_t i = 0; i < count; i++) {
        if (snprintf(expected_key, sizeof(expected_key), "%sColumn%zuWidth", prefix, i) < 0) {
            continue;
        }
        if (key_equals(key, expected_key)) {
            if (parsed >= 20) widths[i] = parsed;
            return 1;
        }
    }

    return 0;
}

static int config_save(const LudoConfig *cfg, const char *path) {
    FILE *fp;

    if (!cfg || !path || path[0] == '\0') return 0;

#ifdef _WIN32
    // use _wfopen via the same pattern as dm_fopen_utf8 or fopen_utf8
    wchar_t *wpath = utf8_to_wide_dup(path);
    const wchar_t *wmode = L"w";
    if (!wpath) return 0;
    fp = _wfopen(wpath, wmode);
    free(wpath);
#else
    fp = fopen(path, "w");
#endif
    if (!fp) return 0;

    fprintf(fp, "[ludo]\n");
    fprintf(fp, "maxDownloadRetry=%d\n", cfg->max_download_retry);
    fprintf(fp, "maxThread=%d\n", cfg->max_thread);
    fprintf(fp, "urlQueueCapacity=%d\n", cfg->url_queue_capacity);
    fprintf(fp, "downloadQueueCapacity=%d\n", cfg->download_queue_capacity);
    fprintf(fp, "maxRedirect=%d\n", cfg->max_redirect);
    fprintf(fp, "outputDir=%s\n", cfg->output_dir);
    fprintf(fp, "pluginDir=%s\n", cfg->plugin_dir);
    fprintf(fp, "\n[gui]\n");
    fprintf(fp, "mainWindowWidth=%d\n", cfg->gui.main_window.width);
    fprintf(fp, "mainWindowHeight=%d\n", cfg->gui.main_window.height);
    fprintf(fp, "mainWindowPosX=%d\n", cfg->gui.main_window.pos_x);
    fprintf(fp, "mainWindowPosY=%d\n", cfg->gui.main_window.pos_y);
    fprintf(fp, "addUrlsWindowWidth=%d\n", cfg->gui.add_urls_window.width);
    fprintf(fp, "addUrlsWindowHeight=%d\n", cfg->gui.add_urls_window.height);
    fprintf(fp, "addUrlsWindowPosX=%d\n", cfg->gui.add_urls_window.pos_x);
    fprintf(fp, "addUrlsWindowPosY=%d\n", cfg->gui.add_urls_window.pos_y);
    fprintf(fp, "httpTestWindowWidth=%d\n", cfg->gui.http_test_window.width);
    fprintf(fp, "httpTestWindowHeight=%d\n", cfg->gui.http_test_window.height);
    fprintf(fp, "httpTestWindowPosX=%d\n", cfg->gui.http_test_window.pos_x);
    fprintf(fp, "httpTestWindowPosY=%d\n", cfg->gui.http_test_window.pos_y);
    fprintf(fp, "luaTestWindowWidth=%d\n", cfg->gui.lua_test_window.width);
    fprintf(fp, "luaTestWindowHeight=%d\n", cfg->gui.lua_test_window.height);
    fprintf(fp, "luaTestWindowPosX=%d\n", cfg->gui.lua_test_window.pos_x);
    fprintf(fp, "luaTestWindowPosY=%d\n", cfg->gui.lua_test_window.pos_y);
    for (size_t i = 0; i < LUDO_GUI_DOWNLOADS_TABLE_COLUMN_COUNT; i++) {
        fprintf(fp, "downloadsTableColumn%zuWidth=%d\n", i, cfg->gui.downloads_table_widths[i]);
    }
    for (size_t i = 0; i < LUDO_GUI_SNIPPET_TABLE_COLUMN_COUNT; i++) {
        fprintf(fp, "snippetTableColumn%zuWidth=%d\n", i, cfg->gui.snippet_table_widths[i]);
    }

    fclose(fp);
    g_config.dirty = 0;
    return 1;
}

static void config_apply_kv(LudoConfig *cfg, const char *key, const char *value) {
    int parsed;

    if (!cfg || !key || !value) return;

    if (key_equals(key, "maxDownloadRetry")) {
        parsed = atoi(value);
        cfg->max_download_retry = clamp_min(parsed, 0);
        return;
    }
    if (key_equals(key, "maxThread")) {
        parsed = atoi(value);
        cfg->max_thread = clamp_min(parsed, 1);
        return;
    }
    if (key_equals(key, "urlQueueCapacity")) {
        parsed = atoi(value);
        cfg->url_queue_capacity = clamp_min(parsed, 1);
        return;
    }
    if (key_equals(key, "downloadQueueCapacity")) {
        parsed = atoi(value);
        cfg->download_queue_capacity = clamp_min(parsed, 1);
        return;
    }
    if (key_equals(key, "maxRedirect")) {
        parsed = atoi(value);
        cfg->max_redirect = clamp_min(parsed, 0);
        return;
    }
    if (key_equals(key, "outputDir")) {
        strncpy(cfg->output_dir, value, sizeof(cfg->output_dir) - 1);
        cfg->output_dir[sizeof(cfg->output_dir) - 1] = '\0';
        return;
    }
    if (key_equals(key, "pluginDir")) {
        strncpy(cfg->plugin_dir, value, sizeof(cfg->plugin_dir) - 1);
        cfg->plugin_dir[sizeof(cfg->plugin_dir) - 1] = '\0';
        return;
    }
    if (config_try_apply_window_kv(&cfg->gui.main_window, "mainWindow", key, value)) return;
    if (config_try_apply_window_kv(&cfg->gui.add_urls_window, "addUrlsWindow", key, value)) return;
    if (config_try_apply_window_kv(&cfg->gui.http_test_window, "httpTestWindow", key, value)) return;
    if (config_try_apply_window_kv(&cfg->gui.lua_test_window, "luaTestWindow", key, value)) return;
    if (config_try_apply_table_kv(cfg->gui.downloads_table_widths,
                                  LUDO_GUI_DOWNLOADS_TABLE_COLUMN_COUNT,
                                  "downloadsTable",
                                  key,
                                  value)) {
        return;
    }
    config_try_apply_table_kv(cfg->gui.snippet_table_widths,
                              LUDO_GUI_SNIPPET_TABLE_COLUMN_COUNT,
                              "snippetTable",
                              key,
                              value);
}

int ludo_config_init(const char *path) {
    FILE *fp;
    char line[2048];

    config_set_defaults(&g_config.config);
    memset(g_config.path, 0, sizeof(g_config.path));

    if (!path || path[0] == '\0') path = "config.ini";
    strncpy(g_config.path, path, sizeof(g_config.path) - 1);
    g_config.path[sizeof(g_config.path) - 1] = '\0';
    g_config.dirty = 0;

#ifdef _WIN32
    wchar_t *wpath = utf8_to_wide_dup(g_config.path);
    const wchar_t *wmode = L"r";
    if (!wpath) return 0;
    fp = _wfopen(wpath, wmode);
    free(wpath);
#else
    fp = fopen(g_config.path, "r");
#endif
    if (!fp) {
        g_config.initialized = 1;
        g_config.dirty = 1;
        return config_save(&g_config.config, g_config.path);
    }

    while (fgets(line, sizeof(line), fp)) {
        char *trimmed = trim(line);
        char *eq;
        char *key;
        char *value;

        if (trimmed[0] == '\0' || trimmed[0] == ';' || trimmed[0] == '#') continue;
        if (trimmed[0] == '[') continue;

        eq = strchr(trimmed, '=');
        if (!eq) continue;

        *eq = '\0';
        key = trim(trimmed);
        value = trim(eq + 1);
        config_apply_kv(&g_config.config, key, value);
    }

    fclose(fp);
    g_config.initialized = 1;
    g_config.dirty = 0;
    return 1;
}

void ludo_config_shutdown(void) {
    g_config.initialized = 0;
    g_config.dirty = 0;
}

const LudoConfig *ludo_config_get(void) {
    return g_config.initialized ? &g_config.config : NULL;
}

const char *ludo_config_path(void) {
    return g_config.path;
}

int ludo_config_save(void) {
    if (!g_config.initialized) return 0;
    if (!g_config.dirty) return 1;
    return config_save(&g_config.config, g_config.path);
}

int ludo_config_set_output_dir(const char *output_dir) {
    if (!g_config.initialized || !output_dir || output_dir[0] == '\0') return 0;

    if (strncmp(g_config.config.output_dir, output_dir, sizeof(g_config.config.output_dir)) == 0) {
        return 1;
    }

    strncpy(g_config.config.output_dir, output_dir, sizeof(g_config.config.output_dir) - 1);
    g_config.config.output_dir[sizeof(g_config.config.output_dir) - 1] = '\0';
    g_config.dirty = 1;
    return ludo_config_save();
}

void ludo_config_set_window_position(LudoGuiWindowId window_id, int x, int y) {
    LudoGuiWindowConfig *window;

    if (!g_config.initialized) return;

    window = config_window_mut(&g_config.config, window_id);
    if (!window) return;
    if (window->pos_x == x && window->pos_y == y) return;

    window->pos_x = x;
    window->pos_y = y;
    g_config.dirty = 1;
}

void ludo_config_set_window_size(LudoGuiWindowId window_id, int width, int height) {
    LudoGuiWindowConfig *window;

    if (!g_config.initialized) return;
    if (width < 240 || height < 160) return;

    window = config_window_mut(&g_config.config, window_id);
    if (!window) return;
    if (window->width == width && window->height == height) return;

    window->width = width;
    window->height = height;
    g_config.dirty = 1;
}

void ludo_config_set_table_column_width(LudoGuiTableId table_id, int column, int width) {
    size_t count;
    int *widths;

    if (!g_config.initialized) return;
    if (column < 0 || width < 20) return;

    widths = config_table_widths_mut(&g_config.config, table_id, &count);
    if (!widths || (size_t)column >= count) return;
    if (widths[column] == width) return;

    widths[column] = width;
    g_config.dirty = 1;
}
