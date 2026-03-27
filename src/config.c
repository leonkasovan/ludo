#include "config.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static struct {
    LudoConfig config;
    char       path[1024];
    int        initialized;
} g_config;

static void config_set_defaults(LudoConfig *cfg) {
    if (!cfg) return;

    memset(cfg, 0, sizeof(*cfg));
    cfg->max_download_retry    = 3;
    cfg->max_thread            = 2;
    cfg->url_queue_capacity    = 256;
    cfg->download_queue_capacity = 256;
    cfg->max_redirect          = 10;
    strncpy(cfg->output_dir, "downloads/", sizeof(cfg->output_dir) - 1);
    strncpy(cfg->plugin_dir, "plugins", sizeof(cfg->plugin_dir) - 1);
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

static int config_save(const LudoConfig *cfg, const char *path) {
    FILE *fp;

    if (!cfg || !path || path[0] == '\0') return 0;

    fp = fopen(path, "w");
    if (!fp) return 0;

    fprintf(fp, "[ludo]\n");
    fprintf(fp, "maxDownloadRetry=%d\n", cfg->max_download_retry);
    fprintf(fp, "maxThread=%d\n", cfg->max_thread);
    fprintf(fp, "urlQueueCapacity=%d\n", cfg->url_queue_capacity);
    fprintf(fp, "downloadQueueCapacity=%d\n", cfg->download_queue_capacity);
    fprintf(fp, "maxRedirect=%d\n", cfg->max_redirect);
    fprintf(fp, "outputDir=%s\n", cfg->output_dir);
    fprintf(fp, "pluginDir=%s\n", cfg->plugin_dir);

    fclose(fp);
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
    }
}

int ludo_config_init(const char *path) {
    FILE *fp;
    char line[2048];

    config_set_defaults(&g_config.config);
    memset(g_config.path, 0, sizeof(g_config.path));

    if (!path || path[0] == '\0') path = "config.ini";
    strncpy(g_config.path, path, sizeof(g_config.path) - 1);
    g_config.path[sizeof(g_config.path) - 1] = '\0';

    fp = fopen(g_config.path, "r");
    if (!fp) {
        g_config.initialized = 1;
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
    return 1;
}

void ludo_config_shutdown(void) {
    g_config.initialized = 0;
}

const LudoConfig *ludo_config_get(void) {
    return g_config.initialized ? &g_config.config : NULL;
}

const char *ludo_config_path(void) {
    return g_config.path;
}

int ludo_config_set_output_dir(const char *output_dir) {
    if (!g_config.initialized || !output_dir || output_dir[0] == '\0') return 0;

    strncpy(g_config.config.output_dir, output_dir, sizeof(g_config.config.output_dir) - 1);
    g_config.config.output_dir[sizeof(g_config.config.output_dir) - 1] = '\0';
    return config_save(&g_config.config, g_config.path);
}
