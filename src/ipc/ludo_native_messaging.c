#include "ludo_native_messaging.h"

#include "../download_manager.h"
#include "../gui.h"
#include "../thread_queue.h"
#include "ipc_abstraction.h"
#include "../../third_party/cJSON/cJSON.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#ifdef _WIN32
#include <windows.h>
#include <sddl.h>
#else
#include <errno.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#endif

#define LUDO_NATIVE_SERVER_NAME "fdm_ipc"
#define LUDO_NATIVE_MAX_MESSAGE (5 * 1024 * 1024)

extern TaskQueue g_url_queue;

typedef struct {
    ludo_thread_t thread;
    int started;
    volatile int stop_requested;
#ifndef _WIN32
    int listen_fd;
    int client_fd;
#endif
} ludo_native_server_t;

static ludo_native_server_t g_native_server = {
#ifndef _WIN32
    .listen_fd = -1,
    .client_fd = -1,
#endif
};

static int str_is_one(const char *s) {
    return s && strcmp(s, "1") == 0;
}

static void append_header_line(char *dst, size_t dst_sz,
                               const char *name, const char *value) {
    size_t used;

    if (!dst || dst_sz == 0 || !name || !value || value[0] == '\0') return;
    used = strlen(dst);
    if (used >= dst_sz - 1) return;
    snprintf(dst + used, dst_sz - used, "%s: %s\n", name, value);
}

static void extract_host_lower(const char *url, char *out, size_t out_sz) {
    const char *start;
    const char *end;
    size_t len = 0;

    if (!out || out_sz == 0) return;
    out[0] = '\0';
    if (!url) return;

    start = strstr(url, "://");
    start = start ? start + 3 : url;
    end = start;
    while (*end && *end != '/' && *end != ':' && *end != '?' && *end != '#')
        end++;

    while (start < end && len + 1 < out_sz) {
        out[len++] = (char)tolower((unsigned char)*start++);
    }
    out[len] = '\0';
}

static int host_matches_suffix(const char *host, const char *suffix) {
    size_t host_len;
    size_t suffix_len;

    if (!host || !suffix) return 0;
    host_len = strlen(host);
    suffix_len = strlen(suffix);
    if (host_len < suffix_len) return 0;
    if (strcmp(host + host_len - suffix_len, suffix) != 0) return 0;
    return host_len == suffix_len || host[host_len - suffix_len - 1] == '.';
}

static int url_is_plugin_video_host(const char *url) {
    static const char *const hosts[] = {
        "facebook.com",
        "fb.watch",
        "instagram.com",
        "tiktok.com",
        "twitter.com",
        "x.com"
    };
    char host[256];

    extract_host_lower(url, host, sizeof(host));
    if (host[0] == '\0') return 0;

    for (size_t i = 0; i < sizeof(hosts) / sizeof(hosts[0]); i++) {
        if (host_matches_suffix(host, hosts[i]))
            return 1;
    }
    return 0;
}

static int route_url_to_plugin_worker(const char *url, const char *reason) {
    if (!url || url[0] == '\0') return 0;
    task_queue_push(&g_url_queue, url);
    gui_log(LOG_INFO, "Browser extension queued %s via plugin worker: %s",
            reason ? reason : "URL", url);
    return 1;
}

static int enqueue_direct_download(const char *url, const char *original_url,
                                   const char *http_referer,
                                   const char *http_cookies,
                                   const char *user_agent,
                                   const char *document_url,
                                   const char *post_data) {
    char headers[4096] = {0};
    int id;

    if ((!http_referer || http_referer[0] == '\0') &&
        document_url && document_url[0] != '\0') {
        http_referer = document_url;
    }

    append_header_line(headers, sizeof(headers), "Referer", http_referer);
    append_header_line(headers, sizeof(headers), "Cookie", http_cookies);
    append_header_line(headers, sizeof(headers), "User-Agent", user_agent);

    id = download_manager_add(
        url,
        download_manager_get_output_dir(),
        DOWNLOAD_NOW,
        original_url && original_url[0] ? original_url : NULL,
        NULL,
        headers[0] ? headers : NULL,
        post_data && post_data[0] ? post_data : NULL,
        NULL
    );
    if (id > 0) {
        gui_log(LOG_INFO, "Browser extension added direct download #%d: %s", id, url);
        return 1;
    }
    gui_log(LOG_WARNING, "Browser extension failed to add direct download: %s", url);
    return 0;
}

static const char *cjson_get_string(cJSON *obj, const char *key) {
    cJSON *it;
    if (!obj || !key) return NULL;
    it = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (!cJSON_IsString(it) || !it->valuestring) return NULL;
    return it->valuestring;
}

static int cjson_get_string_into(cJSON *obj, const char *key, char *out, size_t out_sz) {
    const char *s;
    if (!out || out_sz == 0) return 0;
    out[0] = '\0';
    s = cjson_get_string(obj, key);
    if (!s) return 0;
    snprintf(out, out_sz, "%s", s);
    return 1;
}

static cJSON *cjson_get_object(cJSON *obj, const char *key) {
    cJSON *it;
    if (!obj || !key) return NULL;
    it = cJSON_GetObjectItemCaseSensitive(obj, key);
    return cJSON_IsObject(it) ? it : NULL;
}

static cJSON *cjson_get_array(cJSON *obj, const char *key) {
    cJSON *it;
    if (!obj || !key) return NULL;
    it = cJSON_GetObjectItemCaseSensitive(obj, key);
    return cJSON_IsArray(it) ? it : NULL;
}

static int handle_create_downloads(const char *id, cJSON *create_downloads,
                                   char *response, size_t response_sz) {
    cJSON *downloads_array;
    int success_count = 0;
    int total_count = 0;
    char caught_flag[16] = {0};
    int catched_downloads = 0;

    if (cjson_get_string_into(create_downloads, "catchedDownloads",
                              caught_flag, sizeof(caught_flag))) {
        catched_downloads = str_is_one(caught_flag);
    }

    downloads_array = cjson_get_array(create_downloads, "downloads");
    if (!downloads_array) {
        snprintf(response, response_sz,
                 "{\"id\":\"%s\",\"error\":\"missing_downloads\",\"result\":\"0\"}",
                 id ? id : "");
        return 1;
    }

    for (cJSON *download_obj = downloads_array->child;
         download_obj != NULL;
         download_obj = download_obj->next) {
        char url[4096] = {0};
        char original_url[4096] = {0};
        char http_referer[4096] = {0};
        char http_cookies[4096] = {0};
        char user_agent[1024] = {0};
        char document_url[4096] = {0};
        char post_data[4096] = {0};
        char youtube_mode[32] = {0};
        int should_route_plugin;
        int added = 0;

        if (!cJSON_IsObject(download_obj))
            continue;

        total_count++;
        if (!cjson_get_string_into(download_obj, "url", url, sizeof(url)) ||
            url[0] == '\0') {
            continue;
        }

        cjson_get_string_into(download_obj, "originalUrl",
                              original_url, sizeof(original_url));
        cjson_get_string_into(download_obj, "httpReferer",
                              http_referer, sizeof(http_referer));
        cjson_get_string_into(download_obj, "httpCookies",
                              http_cookies, sizeof(http_cookies));
        cjson_get_string_into(download_obj, "userAgent",
                              user_agent, sizeof(user_agent));
        cjson_get_string_into(download_obj, "documentUrl",
                              document_url, sizeof(document_url));
        cjson_get_string_into(download_obj, "httpPostData",
                              post_data, sizeof(post_data));
        cjson_get_string_into(download_obj, "youtubeChannelVideosDownload",
                              youtube_mode, sizeof(youtube_mode));

        should_route_plugin = !catched_downloads && url_is_plugin_video_host(url);

        if (youtube_mode[0] != '\0' && strcmp(youtube_mode, "0") != 0 &&
            !should_route_plugin) {
            gui_log(LOG_WARNING,
                    "Browser extension requested unsupported video-page download: %s",
                    url);
            continue;
        }

        if (should_route_plugin) {
            added = route_url_to_plugin_worker(url, "video/page URL");
        } else {
            added = enqueue_direct_download(url, original_url,
                                            http_referer, http_cookies,
                                            user_agent, document_url,
                                            post_data);
        }

        if (added)
            success_count++;
    }

    snprintf(response, response_sz,
             "{\"id\":\"%s\",\"error\":\"%s\",\"result\":\"%s\"}",
             id ? id : "",
             success_count > 0 ? "" : "no_downloads_added",
             success_count > 0 ? "1" : "0");

    gui_log(success_count > 0 ? LOG_SUCCESS : LOG_WARNING,
            "Browser extension create_downloads handled %d/%d item(s)",
            success_count, total_count);
    return 1;
}

static int handle_video_sniffer(const char *id, cJSON *video_sniffer,
                                char *response, size_t response_sz) {
    char name[128] = {0};
    char web_page_url[4096] = {0};
    int supported;

    if (!cjson_get_string_into(video_sniffer, "name", name, sizeof(name))) {
        snprintf(response, response_sz,
                 "{\"id\":\"%s\",\"error\":\"missing_sniffer_name\","
                 "\"videoSniffer\":{\"result\":\"0\"}}",
                 id ? id : "");
        return 1;
    }

    cjson_get_string_into(video_sniffer, "webPageUrl",
                          web_page_url, sizeof(web_page_url));
    if (web_page_url[0] == '\0') {
        cjson_get_string_into(video_sniffer, "frameUrl",
                              web_page_url, sizeof(web_page_url));
    }

    supported = url_is_plugin_video_host(web_page_url);

    if (strcmp(name, "CreateVideoDownloadFromUrl") == 0 && supported) {
        route_url_to_plugin_worker(web_page_url, "video page");
    }

    snprintf(response, response_sz,
             "{\"id\":\"%s\",\"error\":\"%s\","
             "\"videoSniffer\":{\"result\":\"%s\"}}",
             id ? id : "",
             (strcmp(name, "CreateVideoDownloadFromUrl") == 0 && !supported)
                 ? "unsupported_video_page"
                 : "",
             supported ? "1" : "0");
    return 1;
}

static int handle_native_message(const char *json, char *response, size_t response_sz) {
    char id[64] = {0};
    char type[128] = {0};
    cJSON *root = NULL;
    const char *id_s;
    const char *type_s;

    if (!json || !response || response_sz == 0) return 0;

    root = cJSON_Parse(json);
    if (!root) {
        snprintf(response, response_sz,
                 "{\"id\":\"\",\"error\":\"invalid_json\"}");
        return 1;
    }

    id_s = cjson_get_string(root, "id");
    if (id_s) snprintf(id, sizeof(id), "%s", id_s);

    type_s = cjson_get_string(root, "type");
    if (!type_s || type_s[0] == '\0') {
        snprintf(response, response_sz,
                 "{\"id\":\"%s\",\"error\":\"missing_type\"}", id);
        cJSON_Delete(root);
        return 1;
    }
    snprintf(type, sizeof(type), "%s", type_s);

    if (strcmp(type, "create_downloads") == 0) {
        cJSON *span = cjson_get_object(root, "create_downloads");
        if (!span) {
            snprintf(response, response_sz,
                     "{\"id\":\"%s\",\"error\":\"missing_create_downloads\","
                     "\"result\":\"0\"}", id);
            cJSON_Delete(root);
            return 1;
        }
        {
            int rc = handle_create_downloads(id, span, response, response_sz);
            cJSON_Delete(root);
            return rc;
        }
    }

    if (strcmp(type, "video_sniffer") == 0) {
        cJSON *span = cjson_get_object(root, "video_sniffer");
        if (!span) {
            snprintf(response, response_sz,
                     "{\"id\":\"%s\",\"error\":\"missing_video_sniffer\","
                     "\"videoSniffer\":{\"result\":\"0\"}}", id);
            cJSON_Delete(root);
            return 1;
        }
        {
            int rc = handle_video_sniffer(id, span, response, response_sz);
            cJSON_Delete(root);
            return rc;
        }
    }

    snprintf(response, response_sz,
             "{\"id\":\"%s\",\"error\":\"unknown_task\"}", id);
    cJSON_Delete(root);
    return 1;
}

static int recv_native_message(ipc_handle_t handle, uint8_t **out_data, uint32_t *out_len) {
    return ipc_recv(handle, out_data, out_len);
}

static void handle_client(ipc_handle_t handle) {
    while (!g_native_server.stop_requested) {
        uint8_t *msg_data = NULL;
        uint32_t msg_len = 0;
        char response[1024];

        if (recv_native_message(handle, &msg_data, &msg_len) != IPC_OK)
            break;
        if (!msg_data || msg_len == 0 || msg_len > LUDO_NATIVE_MAX_MESSAGE) {
            free(msg_data);
            break;
        }

        response[0] = '\0';
        if (handle_native_message((const char *)msg_data, response, sizeof(response)) &&
            response[0] != '\0') {
            ipc_send(handle, (const uint8_t *)response, (uint32_t)strlen(response));
        }
        free(msg_data);
    }
}

#ifdef _WIN32

static SECURITY_ATTRIBUTES *pipe_security_attributes(void) {
    static SECURITY_ATTRIBUTES sa;
    static PSECURITY_DESCRIPTOR sd = NULL;
    static int initialized = 0;

    if (!initialized) {
        initialized = 1;
        if (ConvertStringSecurityDescriptorToSecurityDescriptorA(
                "D:(A;;GA;;;WD)",
                SDDL_REVISION_1,
                &sd,
                NULL)) {
            sa.nLength = sizeof(sa);
            sa.lpSecurityDescriptor = sd;
            sa.bInheritHandle = FALSE;
        }
    }

    return sd ? &sa : NULL;
}

static void *native_server_thread(void *arg) {
    (void)arg;

    while (!g_native_server.stop_requested) {
        HANDLE pipe = CreateNamedPipeA(
            "\\\\.\\pipe\\" LUDO_NATIVE_SERVER_NAME,
            PIPE_ACCESS_DUPLEX,
            PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT,
            PIPE_UNLIMITED_INSTANCES,
            64 * 1024,
            64 * 1024,
            0,
            pipe_security_attributes()
        );
        BOOL connected;

        if (pipe == INVALID_HANDLE_VALUE) {
            Sleep(250);
            continue;
        }

        connected = ConnectNamedPipe(pipe, NULL)
            ? TRUE
            : (GetLastError() == ERROR_PIPE_CONNECTED);
        if (connected && !g_native_server.stop_requested) {
            handle_client(pipe);
        }

        FlushFileBuffers(pipe);
        DisconnectNamedPipe(pipe);
        CloseHandle(pipe);
    }

    return NULL;
}

#else

static void socket_path_for_server(char *out, size_t out_sz) {
    snprintf(out, out_sz, "/tmp/%s.sock", LUDO_NATIVE_SERVER_NAME);
}

static void *native_server_thread(void *arg) {
    char socket_path[256];
    struct sockaddr_un addr;

    (void)arg;
    socket_path_for_server(socket_path, sizeof(socket_path));
    unlink(socket_path);

    g_native_server.listen_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (g_native_server.listen_fd == -1)
        return NULL;

    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, socket_path, sizeof(addr.sun_path) - 1);

    if (bind(g_native_server.listen_fd, (struct sockaddr *)&addr, sizeof(addr)) == -1 ||
        listen(g_native_server.listen_fd, 8) == -1) {
        close(g_native_server.listen_fd);
        g_native_server.listen_fd = -1;
        unlink(socket_path);
        return NULL;
    }

    while (!g_native_server.stop_requested) {
        fd_set rfds;
        struct timeval tv;
        int ready;

        FD_ZERO(&rfds);
        FD_SET(g_native_server.listen_fd, &rfds);
        tv.tv_sec = 0;
        tv.tv_usec = 250000;

        ready = select(g_native_server.listen_fd + 1, &rfds, NULL, NULL, &tv);
        if (ready < 0) {
            if (errno == EINTR) continue;
            break;
        }
        if (ready == 0 || !FD_ISSET(g_native_server.listen_fd, &rfds))
            continue;

        g_native_server.client_fd = accept(g_native_server.listen_fd, NULL, NULL);
        if (g_native_server.client_fd == -1)
            continue;

        handle_client(g_native_server.client_fd);
        close(g_native_server.client_fd);
        g_native_server.client_fd = -1;
    }

    if (g_native_server.client_fd != -1) {
        close(g_native_server.client_fd);
        g_native_server.client_fd = -1;
    }
    if (g_native_server.listen_fd != -1) {
        close(g_native_server.listen_fd);
        g_native_server.listen_fd = -1;
    }
    unlink(socket_path);
    return NULL;
}

#endif

int ludo_native_messaging_start(void) {
    if (g_native_server.started)
        return 1;

    g_native_server.stop_requested = 0;
    if (ludo_thread_create(&g_native_server.thread, native_server_thread, NULL) != 0) {
        gui_log(LOG_WARNING, "Failed to start browser-extension IPC server.");
        return 0;
    }

    g_native_server.started = 1;
    gui_log(LOG_INFO, "Browser-extension IPC server started.");
    return 1;
}

void ludo_native_messaging_stop(void) {
    if (!g_native_server.started)
        return;

    g_native_server.stop_requested = 1;

#ifdef _WIN32
    CancelSynchronousIo(g_native_server.thread);
#else
    if (g_native_server.client_fd != -1) {
        shutdown(g_native_server.client_fd, SHUT_RDWR);
    }
    if (g_native_server.listen_fd != -1) {
        shutdown(g_native_server.listen_fd, SHUT_RDWR);
    }
#endif

    ludo_thread_join(g_native_server.thread);
    g_native_server.started = 0;
    gui_log(LOG_INFO, "Browser-extension IPC server stopped.");
}
