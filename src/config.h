#ifndef LUDO_CONFIG_H
#define LUDO_CONFIG_H

#include <stddef.h>

typedef struct {
    int  max_download_retry;
    int  max_thread;
    int  url_queue_capacity;
    int  download_queue_capacity;
    int  max_redirect;
    char output_dir[1024];
    char plugin_dir[1024];
} LudoConfig;

int ludo_config_init(const char *path);
void ludo_config_shutdown(void);

const LudoConfig *ludo_config_get(void);
const char *ludo_config_path(void);

int ludo_config_set_output_dir(const char *output_dir);

#endif /* LUDO_CONFIG_H */
