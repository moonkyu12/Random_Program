#define WIN32_LEAN_AND_MEAN

#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <winhttp.h>
#include <winsock2.h>
#include <ws2tcpip.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "shell32.lib")

#define APP_TITLE "School Random Program"
#define APP_TITLE_W L"School Random Program"
#define RAW_HOST L"raw.githubusercontent.com"
#define RAW_PREFIX L"/moonkyu12/Random_Page/main/"
#define REQUEST_TIMEOUT_MS 4000
#define MAX_PAYLOAD_SIZE (16 * 1024 * 1024)

typedef struct Route {
    const char *path;
    const char *filename;
    const char *content_type;
} Route;

typedef struct Payload {
    unsigned char *data;
    DWORD size;
} Payload;

static const Route ROUTES[] = {
    {"/", "index.html", "text/html; charset=utf-8"},
    {"/index.html", "index.html", "text/html; charset=utf-8"},
    {"/style.css", "style.css", "text/css; charset=utf-8"},
    {"/script.js", "script.js", "application/javascript; charset=utf-8"},
};

static const char *CACHE_FILES[] = {"index.html", "style.css", "script.js"};
static Payload g_cache[3];
static CRITICAL_SECTION g_cache_lock;
static volatile LONG g_running = 1;

static int cache_index_for(const char *filename) {
    for (int i = 0; i < 3; i++) {
        if (strcmp(CACHE_FILES[i], filename) == 0) {
            return i;
        }
    }
    return -1;
}

static void free_payload(Payload *payload) {
    if (payload->data) {
        free(payload->data);
    }
    payload->data = NULL;
    payload->size = 0;
}

static wchar_t *utf8_to_wide(const char *text) {
    int len = MultiByteToWideChar(CP_UTF8, 0, text, -1, NULL, 0);
    if (len <= 0) {
        return NULL;
    }
    wchar_t *out = (wchar_t *)calloc((size_t)len, sizeof(wchar_t));
    if (!out) {
        return NULL;
    }
    MultiByteToWideChar(CP_UTF8, 0, text, -1, out, len);
    return out;
}

static int join_path(wchar_t *out, DWORD out_count, const wchar_t *base, const wchar_t *name) {
    int written = swprintf(out, out_count, L"%ls\\%ls", base, name);
    return written > 0 && (DWORD)written < out_count;
}

static int get_exe_dir(wchar_t *out, DWORD out_count) {
    DWORD len = GetModuleFileNameW(NULL, out, out_count);
    if (len == 0 || len >= out_count) {
        return 0;
    }
    wchar_t *slash = wcsrchr(out, L'\\');
    if (slash) {
        *slash = L'\0';
    }
    return 1;
}

static int get_cache_dir(wchar_t *out, DWORD out_count) {
    wchar_t local_app_data[MAX_PATH];
    DWORD len = GetEnvironmentVariableW(L"LOCALAPPDATA", local_app_data, MAX_PATH);
    if (len > 0 && len < MAX_PATH) {
        int written = swprintf(out, out_count, L"%ls\\%ls\\live_repo_cache", local_app_data, APP_TITLE_W);
        return written > 0 && (DWORD)written < out_count;
    }

    wchar_t profile[MAX_PATH];
    len = GetEnvironmentVariableW(L"USERPROFILE", profile, MAX_PATH);
    if (len == 0 || len >= MAX_PATH) {
        return 0;
    }
    int written = swprintf(out, out_count, L"%ls\\.cache\\school-random-program\\live_repo_cache", profile);
    return written > 0 && (DWORD)written < out_count;
}

static int read_file_w(const wchar_t *path, Payload *payload) {
    HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) {
        return 0;
    }

    LARGE_INTEGER size;
    if (!GetFileSizeEx(file, &size) || size.QuadPart <= 0 || size.QuadPart > MAX_PAYLOAD_SIZE) {
        CloseHandle(file);
        return 0;
    }

    unsigned char *data = (unsigned char *)malloc((size_t)size.QuadPart);
    if (!data) {
        CloseHandle(file);
        return 0;
    }

    DWORD read = 0;
    BOOL ok = ReadFile(file, data, (DWORD)size.QuadPart, &read, NULL);
    CloseHandle(file);
    if (!ok || read != (DWORD)size.QuadPart) {
        free(data);
        return 0;
    }

    payload->data = data;
    payload->size = read;
    return 1;
}

static int write_file_w(const wchar_t *path, const unsigned char *data, DWORD size) {
    wchar_t dir[MAX_PATH * 2];
    wcsncpy(dir, path, (MAX_PATH * 2) - 1);
    dir[(MAX_PATH * 2) - 1] = L'\0';
    wchar_t *slash = wcsrchr(dir, L'\\');
    if (slash) {
        *slash = L'\0';
        SHCreateDirectoryExW(NULL, dir, NULL);
    }

    HANDLE file = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) {
        return 0;
    }

    DWORD written = 0;
    BOOL ok = WriteFile(file, data, size, &written, NULL);
    CloseHandle(file);
    return ok && written == size;
}

static int cache_file_path(const char *filename, wchar_t *out, DWORD out_count) {
    wchar_t cache_dir[MAX_PATH * 2];
    wchar_t *wide_name = utf8_to_wide(filename);
    if (!wide_name) {
        return 0;
    }
    int ok = get_cache_dir(cache_dir, MAX_PATH * 2) && join_path(out, out_count, cache_dir, wide_name);
    free(wide_name);
    return ok;
}

static int read_disk_cache(const char *filename, Payload *payload) {
    wchar_t path[MAX_PATH * 2];
    if (!cache_file_path(filename, path, MAX_PATH * 2)) {
        return 0;
    }
    return read_file_w(path, payload);
}

static int write_disk_cache(const char *filename, const unsigned char *data, DWORD size) {
    wchar_t path[MAX_PATH * 2];
    if (!cache_file_path(filename, path, MAX_PATH * 2)) {
        return 0;
    }
    return write_file_w(path, data, size);
}

static int read_local_payload(const char *filename, Payload *payload) {
    wchar_t exe_dir[MAX_PATH * 2];
    wchar_t path[MAX_PATH * 2];
    wchar_t *wide_name = utf8_to_wide(filename);
    if (!wide_name) {
        return 0;
    }

    int ok = 0;
    if (get_exe_dir(exe_dir, MAX_PATH * 2) && join_path(path, MAX_PATH * 2, exe_dir, wide_name)) {
        ok = read_file_w(path, payload);
    }
    if (!ok) {
        ok = read_file_w(wide_name, payload);
    }

    free(wide_name);
    return ok;
}

static int fetch_remote_payload(const char *filename, Payload *payload) {
    wchar_t remote_path[512];
    wchar_t *wide_name = utf8_to_wide(filename);
    if (!wide_name) {
        return 0;
    }
    swprintf(remote_path, 512, L"%ls%ls", RAW_PREFIX, wide_name);
    free(wide_name);

    HINTERNET session = WinHttpOpen(L"SchoolRandomProgram/1.0",
                                    WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                    WINHTTP_NO_PROXY_NAME,
                                    WINHTTP_NO_PROXY_BYPASS,
                                    0);
    if (!session) {
        return 0;
    }

    WinHttpSetTimeouts(session, REQUEST_TIMEOUT_MS, REQUEST_TIMEOUT_MS, REQUEST_TIMEOUT_MS, REQUEST_TIMEOUT_MS);
    HINTERNET connect = WinHttpConnect(session, RAW_HOST, INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!connect) {
        WinHttpCloseHandle(session);
        return 0;
    }

    HINTERNET request = WinHttpOpenRequest(connect, L"GET", remote_path, NULL,
                                           WINHTTP_NO_REFERER,
                                           WINHTTP_DEFAULT_ACCEPT_TYPES,
                                           WINHTTP_FLAG_SECURE);
    if (!request) {
        WinHttpCloseHandle(connect);
        WinHttpCloseHandle(session);
        return 0;
    }

    int ok = 0;
    unsigned char *buffer = NULL;
    DWORD used = 0;
    DWORD capacity = 0;

    if (WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0, NULL, 0, 0, 0) &&
        WinHttpReceiveResponse(request, NULL)) {
        DWORD status = 0;
        DWORD status_size = sizeof(status);
        WinHttpQueryHeaders(request,
                            WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                            WINHTTP_HEADER_NAME_BY_INDEX,
                            &status,
                            &status_size,
                            WINHTTP_NO_HEADER_INDEX);
        if (status == 200) {
            for (;;) {
                DWORD available = 0;
                if (!WinHttpQueryDataAvailable(request, &available) || available == 0) {
                    break;
                }
                if (used + available > MAX_PAYLOAD_SIZE) {
                    break;
                }
                if (used + available > capacity) {
                    DWORD next_capacity = capacity == 0 ? 8192 : capacity;
                    while (next_capacity < used + available) {
                        next_capacity *= 2;
                    }
                    unsigned char *next = (unsigned char *)realloc(buffer, next_capacity);
                    if (!next) {
                        break;
                    }
                    buffer = next;
                    capacity = next_capacity;
                }
                DWORD read = 0;
                if (!WinHttpReadData(request, buffer + used, available, &read)) {
                    break;
                }
                used += read;
            }
            ok = used > 0;
        }
    }

    WinHttpCloseHandle(request);
    WinHttpCloseHandle(connect);
    WinHttpCloseHandle(session);

    if (!ok) {
        free(buffer);
        return 0;
    }
    payload->data = buffer;
    payload->size = used;
    return 1;
}

static void store_payload(const char *filename, const Payload *payload) {
    int idx = cache_index_for(filename);
    if (idx < 0) {
        return;
    }

    unsigned char *copy = (unsigned char *)malloc(payload->size);
    if (!copy) {
        return;
    }
    memcpy(copy, payload->data, payload->size);

    EnterCriticalSection(&g_cache_lock);
    free_payload(&g_cache[idx]);
    g_cache[idx].data = copy;
    g_cache[idx].size = payload->size;
    LeaveCriticalSection(&g_cache_lock);

    write_disk_cache(filename, payload->data, payload->size);
}

static int get_cached_payload(const char *filename, Payload *payload) {
    int idx = cache_index_for(filename);
    if (idx < 0) {
        return 0;
    }

    EnterCriticalSection(&g_cache_lock);
    if (g_cache[idx].data) {
        payload->data = (unsigned char *)malloc(g_cache[idx].size);
        if (payload->data) {
            memcpy(payload->data, g_cache[idx].data, g_cache[idx].size);
            payload->size = g_cache[idx].size;
        }
        LeaveCriticalSection(&g_cache_lock);
        return payload->data != NULL;
    }
    LeaveCriticalSection(&g_cache_lock);

    Payload disk = {0};
    if (!read_disk_cache(filename, &disk)) {
        return 0;
    }
    store_payload(filename, &disk);
    *payload = disk;
    return 1;
}

static void warm_cache_from_disk(void) {
    for (int i = 0; i < 3; i++) {
        Payload payload = {0};
        if (read_disk_cache(CACHE_FILES[i], &payload)) {
            store_payload(CACHE_FILES[i], &payload);
            free_payload(&payload);
        }
    }
}

static void refresh_cache_from_remote_once(void) {
    for (int i = 0; i < 3; i++) {
        Payload payload = {0};
        if (fetch_remote_payload(CACHE_FILES[i], &payload)) {
            store_payload(CACHE_FILES[i], &payload);
            free_payload(&payload);
            printf("updated cache: %s\n", CACHE_FILES[i]);
        } else {
            printf("remote unavailable: %s\n", CACHE_FILES[i]);
        }
    }
}

static const Route *find_route(const char *path) {
    for (size_t i = 0; i < sizeof(ROUTES) / sizeof(ROUTES[0]); i++) {
        if (strcmp(ROUTES[i].path, path) == 0) {
            return &ROUTES[i];
        }
    }
    return NULL;
}

static void send_all(SOCKET client, const char *data, int len) {
    int sent = 0;
    while (sent < len) {
        int n = send(client, data + sent, len - sent, 0);
        if (n <= 0) {
            return;
        }
        sent += n;
    }
}

static void send_payload_response(SOCKET client, const Route *route, const Payload *payload, const char *source) {
    char header[1024];
    int header_len = snprintf(header, sizeof(header),
                              "HTTP/1.1 200 OK\r\n"
                              "Content-Type: %s\r\n"
                              "Cache-Control: no-store\r\n"
                              "X-Source: %s\r\n"
                              "Content-Length: %lu\r\n"
                              "Connection: close\r\n"
                              "\r\n",
                              route->content_type,
                              source,
                              (unsigned long)payload->size);
    send_all(client, header, header_len);
    send_all(client, (const char *)payload->data, (int)payload->size);
}

static void send_error_response(SOCKET client, int code, const char *text) {
    char body[256];
    int body_len = snprintf(body, sizeof(body), "%d %s\n", code, text);
    char header[512];
    int header_len = snprintf(header, sizeof(header),
                              "HTTP/1.1 %d %s\r\n"
                              "Content-Type: text/plain; charset=utf-8\r\n"
                              "Content-Length: %d\r\n"
                              "Connection: close\r\n"
                              "\r\n",
                              code,
                              text,
                              body_len);
    send_all(client, header, header_len);
    send_all(client, body, body_len);
}

static DWORD WINAPI client_thread(LPVOID arg) {
    SOCKET client = (SOCKET)(UINT_PTR)arg;
    char request[4096];
    int received = recv(client, request, sizeof(request) - 1, 0);
    if (received <= 0) {
        closesocket(client);
        return 0;
    }
    request[received] = '\0';

    char method[16] = {0};
    char raw_path[1024] = {0};
    if (sscanf(request, "%15s %1023s", method, raw_path) != 2 ||
        strcmp(method, "GET") != 0) {
        send_error_response(client, 405, "Method Not Allowed");
        closesocket(client);
        return 0;
    }

    char *query = strchr(raw_path, '?');
    if (query) {
        *query = '\0';
    }

    const Route *route = find_route(raw_path);
    if (!route) {
        send_error_response(client, 404, "Not Found");
        closesocket(client);
        return 0;
    }

    Payload payload = {0};
    if (get_cached_payload(route->filename, &payload)) {
        send_payload_response(client, route, &payload, "cache");
        free_payload(&payload);
    } else if (read_local_payload(route->filename, &payload)) {
        send_payload_response(client, route, &payload, "local");
        free_payload(&payload);
    } else {
        send_error_response(client, 502, "Cannot load source from cache.");
    }

    closesocket(client);
    return 0;
}

static int start_server(SOCKET *out_socket, int *out_port) {
    SOCKET server = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (server == INVALID_SOCKET) {
        return 0;
    }

    struct sockaddr_in addr;
    ZeroMemory(&addr, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;

    if (bind(server, (struct sockaddr *)&addr, sizeof(addr)) == SOCKET_ERROR ||
        listen(server, SOMAXCONN) == SOCKET_ERROR) {
        closesocket(server);
        return 0;
    }

    int len = sizeof(addr);
    if (getsockname(server, (struct sockaddr *)&addr, &len) == SOCKET_ERROR) {
        closesocket(server);
        return 0;
    }

    *out_socket = server;
    *out_port = ntohs(addr.sin_port);
    return 1;
}

static DWORD WINAPI server_thread(LPVOID arg) {
    SOCKET server = (SOCKET)(UINT_PTR)arg;
    while (InterlockedCompareExchange(&g_running, 1, 1)) {
        SOCKET client = accept(server, NULL, NULL);
        if (client == INVALID_SOCKET) {
            break;
        }
        HANDLE thread = CreateThread(NULL, 0, client_thread, (LPVOID)(UINT_PTR)client, 0, NULL);
        if (thread) {
            CloseHandle(thread);
        } else {
            closesocket(client);
        }
    }
    return 0;
}

static BOOL WINAPI console_handler(DWORD event) {
    if (event == CTRL_C_EVENT || event == CTRL_CLOSE_EVENT || event == CTRL_BREAK_EVENT) {
        InterlockedExchange(&g_running, 0);
        return TRUE;
    }
    return FALSE;
}

int main(void) {
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        fprintf(stderr, "WSAStartup failed\n");
        return 1;
    }

    InitializeCriticalSection(&g_cache_lock);
    SetConsoleCtrlHandler(console_handler, TRUE);

    warm_cache_from_disk();
    refresh_cache_from_remote_once();

    SOCKET server = INVALID_SOCKET;
    int port = 0;
    if (!start_server(&server, &port)) {
        fprintf(stderr, "Failed to start local server\n");
        DeleteCriticalSection(&g_cache_lock);
        WSACleanup();
        return 1;
    }

    HANDLE thread = CreateThread(NULL, 0, server_thread, (LPVOID)(UINT_PTR)server, 0, NULL);
    if (!thread) {
        fprintf(stderr, "Failed to start server thread\n");
        closesocket(server);
        DeleteCriticalSection(&g_cache_lock);
        WSACleanup();
        return 1;
    }

    char url[128];
    snprintf(url, sizeof(url), "http://127.0.0.1:%d/", port);
    printf("%s running at %s\n", APP_TITLE, url);
    printf("Press Ctrl+C to stop.\n");
    ShellExecuteA(NULL, "open", url, NULL, NULL, SW_SHOWNORMAL);

    while (InterlockedCompareExchange(&g_running, 1, 1)) {
        Sleep(200);
    }

    closesocket(server);
    WaitForSingleObject(thread, 2000);
    CloseHandle(thread);

    for (int i = 0; i < 3; i++) {
        free_payload(&g_cache[i]);
    }
    DeleteCriticalSection(&g_cache_lock);
    WSACleanup();
    return 0;
}
