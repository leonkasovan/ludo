#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <signal.h>
#include <time.h>
#include <stdarg.h>
#include <ctype.h>

#ifdef _WIN32
    #include <windows.h>
    #include <io.h>
    #include <fcntl.h>
    #define SET_BINARY_MODE() _setmode(_fileno(stdin), _O_BINARY); _setmode(_fileno(stdout), _O_BINARY)
    #define PATH_SEP '\\'
#else
    #include <unistd.h>
    #include <sys/stat.h>
    #include <sys/time.h>
    #include <limits.h>
    #define SET_BINARY_MODE() /* not needed on Unix */
    #define PATH_SEP '/'
#endif

#include "ipc_abstraction.h"
#include "../../third_party/cJSON/cJSON.h"

#define LUDO_NATIVE_SERVER_NAME "fdm_ipc"
#define LUDO_MAIN_WINDOW_TITLE "LUDO - LUa DOwnloader"

static const char *g_default_settings_json =
    "{"
    "\"browser\":{"
    "  \"menu\":{"
    "    \"dllink\":\"1\","
    "    \"dlall\":\"1\","
    "    \"dlselected\":\"1\","
    "    \"dlpage\":\"1\","
    "    \"dlvideo\":\"1\","
    "    \"dlYtChannel\":\"0\""
    "  },"
    "  \"monitor\":{"
    "    \"enable\":\"1\","
    "    \"allowDownload\":\"1\","
    "    \"skipSmallerThan\":\"1048576\","
    "    \"skipExtensions\":\"\","
    "    \"catchExtensions\":\"\","
    "    \"skipServersEnabled\":\"0\","
    "    \"skipServers\":\"\","
    "    \"skipIfKeyPressed\":\"0\""
    "  }"
    "}"
    "}";

static FILE *g_log_file = NULL;
static char g_exe_dir[1024] = ".";
static char g_log_path[1200] = "wenativehost.log";
static char g_settings_path[1200] = "wenativehost_settings.json";
static ipc_handle_t g_ludo_ipc = IPC_INVALID_HANDLE;
static int g_ludo_connected = 0;
static volatile int keep_running = 1;

static void log_msg(const char *fmt, ...);

static void sleep_ms(unsigned int ms) {
#ifdef _WIN32
    Sleep(ms);
#else
    usleep(ms * 1000U);
#endif
}

static void join_path(char *out, size_t out_sz, const char *dir, const char *name) {
    size_t len;

    if (!out || out_sz == 0) return;
    out[0] = '\0';
    if (!dir || !dir[0]) {
        snprintf(out, out_sz, "%s", name ? name : "");
        return;
    }

    len = strlen(dir);
    if (len > 0 && (dir[len - 1] == '/' || dir[len - 1] == '\\'))
        snprintf(out, out_sz, "%s%s", dir, name ? name : "");
    else
        snprintf(out, out_sz, "%s%c%s", dir, PATH_SEP, name ? name : "");
}

static void init_runtime_paths(void) {
#ifdef _WIN32
    char module_path[1024];
    DWORD len = GetModuleFileNameA(NULL, module_path, (DWORD)sizeof(module_path));
    if (len > 0 && len < sizeof(module_path)) {
        char *slash = strrchr(module_path, '\\');
        if (slash) {
            *slash = '\0';
            strncpy(g_exe_dir, module_path, sizeof(g_exe_dir) - 1);
            g_exe_dir[sizeof(g_exe_dir) - 1] = '\0';
        }
    }
#else
    char module_path[PATH_MAX];
    ssize_t len = readlink("/proc/self/exe", module_path, sizeof(module_path) - 1);
    if (len > 0) {
        char *slash;
        module_path[len] = '\0';
        slash = strrchr(module_path, '/');
        if (slash) {
            *slash = '\0';
            strncpy(g_exe_dir, module_path, sizeof(g_exe_dir) - 1);
            g_exe_dir[sizeof(g_exe_dir) - 1] = '\0';
        }
    }
#endif

    join_path(g_log_path, sizeof(g_log_path), g_exe_dir, "wenativehost.log");
    join_path(g_settings_path, sizeof(g_settings_path), g_exe_dir,
              "wenativehost_settings.json");
}

static int file_exists(const char *path) {
#ifdef _WIN32
    DWORD attr = GetFileAttributesA(path);
    return attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY);
#else
    struct stat st;
    return path && stat(path, &st) == 0 && S_ISREG(st.st_mode);
#endif
}

static void reset_ludo_connection(void) {
    if (g_ludo_connected) {
        ipc_close(g_ludo_ipc);
        g_ludo_ipc = IPC_INVALID_HANDLE;
        g_ludo_connected = 0;
    }
}

static void log_msg(const char *fmt, ...) {
    va_list args;
    time_t now;
    struct tm *tm_info;
    char timebuf[20];

    if (!g_log_file) return;

    va_start(args, fmt);
    now = time(NULL);
    tm_info = localtime(&now);
    strftime(timebuf, sizeof(timebuf), "%Y-%m-%d %H:%M:%S", tm_info);
    fprintf(g_log_file, "[%s] ", timebuf);
    vfprintf(g_log_file, fmt, args);
    fprintf(g_log_file, "\n");
    fflush(g_log_file);
    va_end(args);
}

static void log_init(void) {
    g_log_file = fopen(g_log_path, "a");
    if (g_log_file) {
        setvbuf(g_log_file, NULL, _IONBF, 0);
        log_msg("=== wenativehost started ===");
        log_msg("Executable directory: %s", g_exe_dir);
    }
}

static void log_close(void) {
    if (g_log_file) {
        log_msg("=== wenativehost shutting down ===");
        fclose(g_log_file);
        g_log_file = NULL;
    }
}

typedef struct {
    uint32_t length;
    uint8_t *data;
} native_message_t;

static int native_read_message(native_message_t *msg) {
    uint32_t length;

    if (fread(&length, sizeof(uint32_t), 1, stdin) != 1) {
        log_msg("stdin closed while reading message length");
        return 0;
    }
    if (length == 0 || length > 5 * 1024 * 1024) {
        log_msg("Invalid browser message length: %u", length);
        return 0;
    }

    msg->data = (uint8_t *)malloc(length + 1);
    if (!msg->data) {
        log_msg("malloc failed for browser message (%u bytes)", length);
        return 0;
    }

    if (fread(msg->data, 1, length, stdin) != length) {
        log_msg("stdin closed while reading %u message bytes", length);
        free(msg->data);
        return 0;
    }

    msg->data[length] = '\0';
    msg->length = length;
    log_msg("Browser -> host: %s", (const char *)msg->data);
    return 1;
}

static int native_write_message(const char *json_str) {
    uint32_t length = (uint32_t)strlen(json_str);

    log_msg("Host -> browser: %s", json_str);
    if (fwrite(&length, sizeof(uint32_t), 1, stdout) != 1)
        return 0;
    if (fwrite(json_str, 1, length, stdout) != length)
        return 0;
    fflush(stdout);
    return 1;
}

static char *load_saved_settings_json(void) {
    FILE *fp;
    long size;
    char *buf;
    size_t read_bytes;

    fp = fopen(g_settings_path, "rb");
    if (!fp) return NULL;
    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        return NULL;
    }
    size = ftell(fp);
    if (size <= 0) {
        fclose(fp);
        return NULL;
    }
    if (fseek(fp, 0, SEEK_SET) != 0) {
        fclose(fp);
        return NULL;
    }

    buf = (char *)malloc((size_t)size + 1);
    if (!buf) {
        fclose(fp);
        return NULL;
    }

    read_bytes = fread(buf, 1, (size_t)size, fp);
    fclose(fp);
    if (read_bytes != (size_t)size) {
        free(buf);
        return NULL;
    }
    buf[size] = '\0';
    return buf;
}

static int save_settings_json(const char *json_text) {
    FILE *fp;
    size_t len;

    if (!json_text || json_text[0] == '\0') return 0;
    fp = fopen(g_settings_path, "wb");
    if (!fp) return 0;
    len = strlen(json_text);
    if (fwrite(json_text, 1, len, fp) != len) {
        fclose(fp);
        return 0;
    }
    fclose(fp);
    log_msg("Saved extension settings to %s", g_settings_path);
    return 1;
}

static int url_is_supported_video_host(const char *url) {
    static const char *const hosts[] = {
        "facebook.com",
        "fb.watch",
        "instagram.com",
        "tiktok.com",
        "twitter.com",
        "x.com"
    };
    char host[256];
    const char *start;
    const char *end;
    size_t len = 0;

    if (!url) return 0;

    start = strstr(url, "://");
    start = start ? start + 3 : url;
    end = start;
    while (*end && *end != '/' && *end != ':' && *end != '?' && *end != '#')
        end++;

    while (start < end && len + 1 < sizeof(host)) {
        host[len++] = (char)tolower((unsigned char)*start++);
    }
    host[len] = '\0';

    for (size_t i = 0; i < sizeof(hosts) / sizeof(hosts[0]); i++) {
        size_t host_len = strlen(host);
        size_t suffix_len = strlen(hosts[i]);
        if (host_len < suffix_len) continue;
        if (strcmp(host + host_len - suffix_len, hosts[i]) != 0) continue;
        if (host_len == suffix_len || host[host_len - suffix_len - 1] == '.')
            return 1;
    }
    return 0;
}

#ifdef _WIN32
static int focus_ludo_window(int show_window) {
    HWND hwnd = FindWindowA(NULL, LUDO_MAIN_WINDOW_TITLE);
    if (!hwnd) return 0;

    if (show_window) {
        if (IsIconic(hwnd))
            ShowWindow(hwnd, SW_RESTORE);
        else
            ShowWindow(hwnd, SW_SHOW);
        SetForegroundWindow(hwnd);
        BringWindowToTop(hwnd);
    } else {
        ShowWindow(hwnd, SW_MINIMIZE);
    }
    return 1;
}
#else
static int focus_ludo_window(int show_window) {
    (void)show_window;
    return 0;
}
#endif

static int launch_process_at(const char *exe_path) {
#ifdef _WIN32
    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    char command_line[2048];
    char workdir[1024];
    char *slash;

    memset(&si, 0, sizeof(si));
    memset(&pi, 0, sizeof(pi));
    si.cb = sizeof(si);

    strncpy(workdir, exe_path, sizeof(workdir) - 1);
    slash = strrchr(workdir, '\\');
    if (slash) *slash = '\0';

    snprintf(command_line, sizeof(command_line), "\"%s\"", exe_path);
    if (!CreateProcessA(exe_path, command_line, NULL, NULL, FALSE, 0,
                        NULL, slash ? workdir : NULL, &si, &pi)) {
        log_msg("CreateProcess failed for %s (error %lu)", exe_path, GetLastError());
        return 0;
    }

    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    log_msg("Launched Ludo: %s", exe_path);
    return 1;
#else
    pid_t pid = fork();
    if (pid == 0) {
        execl(exe_path, exe_path, (char *)NULL);
        _exit(127);
    }
    if (pid < 0)
        return 0;
    log_msg("Launched Ludo: %s", exe_path);
    return 1;
#endif
}

static int try_launch_ludo(void) {
    const char *env_path = getenv("LUDO_EXE");
    char candidate[1024];
    static const char *const candidates[] = {
#ifdef _WIN32
        "ludo.exe",
        "ludo-debug.exe",
        "..\\ludo.exe",
        "..\\ludo-debug.exe",
        "..\\..\\build\\ludo.exe",
        "..\\..\\build\\ludo-debug.exe",
        "..\\..\\build\\Debug\\ludo.exe",
        "..\\..\\build\\Debug\\ludo-debug.exe",
        "..\\..\\build\\Release\\ludo.exe",
        "..\\..\\build\\Release\\ludo-debug.exe"
#else
        "ludo",
        "ludo-debug",
        "../ludo",
        "../ludo-debug",
        "../../build/ludo",
        "../../build/ludo-debug"
#endif
    };

    if (env_path && file_exists(env_path))
        return launch_process_at(env_path);

    for (size_t i = 0; i < sizeof(candidates) / sizeof(candidates[0]); i++) {
        join_path(candidate, sizeof(candidate), g_exe_dir, candidates[i]);
        if (file_exists(candidate))
            return launch_process_at(candidate);
    }

    log_msg("Could not locate a Ludo executable relative to %s", g_exe_dir);
    return 0;
}

static int ensure_ludo_connection(void) {
    if (g_ludo_connected)
        return 1;

    for (int attempt = 0; attempt < 3; attempt++) {
        g_ludo_ipc = ipc_connect(LUDO_NATIVE_SERVER_NAME);
        if (g_ludo_ipc != IPC_INVALID_HANDLE) {
            g_ludo_connected = 1;
            log_msg("Connected to Ludo IPC server");
            return 1;
        }
        sleep_ms(200);
    }

    if (!try_launch_ludo())
        return 0;

    for (int attempt = 0; attempt < 40; attempt++) {
        g_ludo_ipc = ipc_connect(LUDO_NATIVE_SERVER_NAME);
        if (g_ludo_ipc != IPC_INVALID_HANDLE) {
            g_ludo_connected = 1;
            log_msg("Connected to Ludo IPC server after launch");
            return 1;
        }
        sleep_ms(250);
    }

    log_msg("Timed out waiting for Ludo IPC server");
    return 0;
}

static int forward_to_ludo(const char *json_message, char **response_out) {
    uint8_t *resp_data = NULL;
    uint32_t resp_len = 0;

    if (response_out) *response_out = NULL;

    if (!ensure_ludo_connection())
        return 0;

    if (ipc_send(g_ludo_ipc, (const uint8_t *)json_message,
                 (uint32_t)strlen(json_message)) != IPC_OK) {
        log_msg("IPC send failed, resetting connection");
        reset_ludo_connection();
        return 0;
    }

    if (ipc_recv(g_ludo_ipc, &resp_data, &resp_len) != IPC_OK) {
        log_msg("IPC receive failed, resetting connection");
        reset_ludo_connection();
        return 0;
    }

    if (response_out) {
        char *copy = (char *)malloc(resp_len + 1);
        if (!copy) {
            free(resp_data);
            return 0;
        }
        memcpy(copy, resp_data, resp_len);
        copy[resp_len] = '\0';
        *response_out = copy;
    }

    log_msg("Ludo -> host: %.*s", (int)resp_len, (const char *)resp_data);
    free(resp_data);
    return 1;
}

static void write_handshake_response(const char *id) {
    char resp[256];
    snprintf(resp, sizeof(resp),
             "{\"id\":\"%s\",\"error\":\"\",\"version\":\"1.0.0.1\"}",
             id ? id : "");
    native_write_message(resp);
}

static void write_ui_strings_response(const char *id) {
    char resp[256];
    snprintf(resp, sizeof(resp),
             "{\"id\":\"%s\",\"type\":\"ui_strings\",\"error\":\"\",\"strings\":{}}",
             id ? id : "");
    native_write_message(resp);
}

static void write_query_settings_response(const char *id) {
    char *saved_settings = load_saved_settings_json();
    const char *settings_json = saved_settings ? saved_settings : g_default_settings_json;
    size_t needed = strlen(id ? id : "") + strlen(settings_json) + 128;
    char *resp = (char *)malloc(needed);

    if (!resp) {
        free(saved_settings);
        return;
    }

    snprintf(resp, needed,
             "{\"id\":\"%s\",\"type\":\"query_settings\",\"error\":\"\","
             "\"settings\":%s}",
             id ? id : "", settings_json);
    native_write_message(resp);
    free(resp);
    free(saved_settings);
}

static void write_post_settings_response(const char *id, int success) {
    char resp[256];
    snprintf(resp, sizeof(resp),
             "{\"id\":\"%s\",\"type\":\"post_settings\",\"error\":\"%s\","
             "\"result\":\"%s\"}",
             id ? id : "",
             success ? "" : "settings_save_failed",
             success ? "1" : "0");
    native_write_message(resp);
}

static void write_key_state_response(const char *id) {
    char resp[256];
    snprintf(resp, sizeof(resp),
             "{\"id\":\"%s\",\"type\":\"key_state\",\"error\":\"\",\"pressed\":0}",
             id ? id : "");
    native_write_message(resp);
}

static void write_basic_result_response(const char *id, const char *type,
                                        const char *error, const char *result) {
    char resp[256];
    snprintf(resp, sizeof(resp),
             "{\"id\":\"%s\",\"type\":\"%s\",\"error\":\"%s\",\"result\":\"%s\"}",
             id ? id : "", type ? type : "", error ? error : "",
             result ? result : "");
    native_write_message(resp);
}

static void write_video_sniffer_response(const char *id, const char *error,
                                         const char *result) {
    char resp[256];
    snprintf(resp, sizeof(resp),
             "{\"id\":\"%s\",\"error\":\"%s\","
             "\"videoSniffer\":{\"result\":\"%s\"}}",
             id ? id : "", error ? error : "", result ? result : "0");
    native_write_message(resp);
}

static const char *cjson_get_string(cJSON *obj, const char *key) {
    cJSON *it;
    if (!obj || !key) return NULL;
    it = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (!cJSON_IsString(it) || !it->valuestring) return NULL;
    return it->valuestring;
}

static cJSON *cjson_get_object(cJSON *obj, const char *key) {
    cJSON *it;
    if (!obj || !key) return NULL;
    it = cJSON_GetObjectItemCaseSensitive(obj, key);
    return cJSON_IsObject(it) ? it : NULL;
}

static void handle_message(const native_message_t *msg) {
    const char *json = (const char *)msg->data;
    char id[64] = {0};
    char type[128] = {0};
    cJSON *root = NULL;
    const char *id_s;
    const char *type_s;

    if (!json) return;

    root = cJSON_Parse(json);
    if (!root) {
        write_basic_result_response("", "parse_error", "invalid_json", "0");
        return;
    }

    id_s = cjson_get_string(root, "id");
    if (id_s) {
        snprintf(id, sizeof(id), "%s", id_s);
    }

    type_s = cjson_get_string(root, "type");
    if (!type_s || type_s[0] == '\0') {
        write_handshake_response(id);
        cJSON_Delete(root);
        return;
    }
    snprintf(type, sizeof(type), "%s", type_s);

    if (strcmp(type, "handshake") == 0) {
        write_handshake_response(id);
        cJSON_Delete(root);
        return;
    }

    if (strcmp(type, "ui_strings") == 0) {
        write_ui_strings_response(id);
        cJSON_Delete(root);
        return;
    }

    if (strcmp(type, "query_settings") == 0) {
        write_query_settings_response(id);
        cJSON_Delete(root);
        return;
    }

    if (strcmp(type, "post_settings") == 0) {
        int success = 0;
        cJSON *post_settings = cjson_get_object(root, "post_settings");
        if (post_settings) {
            char *settings_json = cJSON_PrintUnformatted(post_settings);
            if (settings_json) {
                success = save_settings_json(settings_json);
                cJSON_free(settings_json);
            }
        }
        write_post_settings_response(id, success);
        cJSON_Delete(root);
        return;
    }

    if (strcmp(type, "key_state") == 0) {
        write_key_state_response(id);
        cJSON_Delete(root);
        return;
    }

    if (strcmp(type, "fdm_json_task") == 0) {
        char json_task[512] = {0};
        const char *json_task_s = cjson_get_string(root, "json");
        if (json_task_s) snprintf(json_task, sizeof(json_task), "%s", json_task_s);
        if (strstr(json_task, "optionsClick")) {
            if (!focus_ludo_window(1))
                try_launch_ludo();
        }
        write_basic_result_response(id, type, "", "1");
        cJSON_Delete(root);
        return;
    }

    if (strcmp(type, "window") == 0) {
        char action[32] = {0};
        int success = 0;
        cJSON *window_obj = cjson_get_object(root, "window");
        const char *action_s = window_obj ? cjson_get_string(window_obj, "action") : NULL;

        if (action_s) snprintf(action, sizeof(action), "%s", action_s);

        if (strcmp(action, "hide") == 0)
            success = focus_ludo_window(0);
        else
            success = focus_ludo_window(1) || try_launch_ludo();

        write_basic_result_response(id, type,
                                    success ? "" : "window_action_failed",
                                    success ? "1" : "0");
        cJSON_Delete(root);
        return;
    }

    if (strcmp(type, "video_sniffer") == 0) {
        char name[128] = {0};
        char web_page_url[4096] = {0};
        cJSON *vs = cjson_get_object(root, "video_sniffer");
        const char *name_s;
        const char *web_s;
        const char *frame_s;

        if (!vs || !(name_s = cjson_get_string(vs, "name")) || name_s[0] == '\0') {
            write_video_sniffer_response(id, "missing_video_sniffer", "0");
            cJSON_Delete(root);
            return;
        }
        snprintf(name, sizeof(name), "%s", name_s);

        web_s = cjson_get_string(vs, "webPageUrl");
        if (web_s) snprintf(web_page_url, sizeof(web_page_url), "%s", web_s);
        if (web_page_url[0] == '\0') {
            frame_s = cjson_get_string(vs, "frameUrl");
            if (frame_s) snprintf(web_page_url, sizeof(web_page_url), "%s", frame_s);
        }

        if (strcmp(name, "IsVideoFlash") == 0) {
            write_video_sniffer_response(id, "",
                                         url_is_supported_video_host(web_page_url)
                                             ? "1"
                                             : "0");
            cJSON_Delete(root);
            return;
        }

        if (strcmp(name, "CreateVideoDownloadFromUrl") == 0) {
            char *ludo_resp = NULL;
            if (forward_to_ludo(json, &ludo_resp) && ludo_resp) {
                native_write_message(ludo_resp);
                free(ludo_resp);
            } else {
                write_video_sniffer_response(id, "ludo_not_running", "0");
            }
            cJSON_Delete(root);
            return;
        }

        write_video_sniffer_response(id, "unsupported_video_sniffer", "0");
        cJSON_Delete(root);
        return;
    }

    if (strcmp(type, "create_downloads") == 0) {
        char *ludo_resp = NULL;
        if (forward_to_ludo(json, &ludo_resp) && ludo_resp) {
            native_write_message(ludo_resp);
            free(ludo_resp);
        } else {
            write_basic_result_response(id, type, "ludo_not_running", "0");
        }
        cJSON_Delete(root);
        return;
    }

    if (strcmp(type, "browser_proxy") == 0 ||
        strcmp(type, "network_request_notification") == 0 ||
        strcmp(type, "browser_download_state_report") == 0 ||
        strcmp(type, "shutdown") == 0) {
        write_basic_result_response(id, type, "", "1");
        cJSON_Delete(root);
        return;
    }

    write_basic_result_response(id, type, "unknown_task", "0");
    cJSON_Delete(root);
}

#ifdef _WIN32
static BOOL WINAPI console_handler(DWORD ctrl) {
    (void)ctrl;
    keep_running = 0;
    return TRUE;
}
#else
static void signal_handler(int sig) {
    (void)sig;
    keep_running = 0;
}
#endif

int main(int argc, char *argv[]) {
    (void)argc;
    (void)argv;

    init_runtime_paths();
    log_init();
    SET_BINARY_MODE();

#ifdef _WIN32
    SetConsoleCtrlHandler(console_handler, TRUE);
#else
    signal(SIGTERM, signal_handler);
    signal(SIGINT, signal_handler);
#endif

    if (ipc_init() != IPC_OK) {
        log_msg("ipc_init failed");
        log_close();
        return 1;
    }

    while (keep_running) {
        native_message_t msg = {0};
        if (!native_read_message(&msg))
            break;
        handle_message(&msg);
        free(msg.data);
    }

    reset_ludo_connection();
    ipc_cleanup();
    log_close();
    return 0;
}
