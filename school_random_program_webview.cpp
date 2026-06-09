#define WIN32_LEAN_AND_MEAN
#define UNICODE
#define _UNICODE

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <windows.h>
#include <shlobj.h>
#include <winhttp.h>
#include <winsock2.h>
#include <wrl.h>
#include <WebView2.h>

#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")

using Microsoft::WRL::Callback;
using Microsoft::WRL::ComPtr;

namespace {

constexpr wchar_t kAppTitle[] = L"School Random Program";
constexpr wchar_t kRawHost[] = L"raw.githubusercontent.com";
constexpr wchar_t kRawPrefix[] = L"/moonkyu12/Random_Page/main/";
constexpr int kWindowWidth = 1440;
constexpr int kWindowHeight = 900;
constexpr DWORD kTimeoutMs = 4000;
constexpr size_t kMaxPayloadSize = 16 * 1024 * 1024;

struct Route {
    const char* path;
    const char* filename;
    const char* contentType;
};

const Route kRoutes[] = {
    {"/", "index.html", "text/html; charset=utf-8"},
    {"/index.html", "index.html", "text/html; charset=utf-8"},
    {"/style.css", "style.css", "text/css; charset=utf-8"},
    {"/script.js", "script.js", "application/javascript; charset=utf-8"},
};

const char* kCacheFiles[] = {"index.html", "style.css", "script.js"};

std::mutex g_cacheMutex;
std::map<std::string, std::vector<unsigned char>> g_cache;
std::atomic_bool g_running{true};
SOCKET g_serverSocket = INVALID_SOCKET;
HWND g_mainWindow = nullptr;
ComPtr<ICoreWebView2Controller> g_webviewController;
ComPtr<ICoreWebView2> g_webview;

std::wstring Utf8ToWide(const std::string& text) {
    if (text.empty()) {
        return {};
    }
    int count = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, nullptr, 0);
    std::wstring out(static_cast<size_t>(count - 1), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, out.data(), count);
    return out;
}

std::filesystem::path ExeDir() {
    wchar_t path[MAX_PATH * 2]{};
    GetModuleFileNameW(nullptr, path, static_cast<DWORD>(std::size(path)));
    std::filesystem::path exe(path);
    return exe.parent_path();
}

std::filesystem::path CacheDir() {
    wchar_t localAppData[MAX_PATH]{};
    DWORD len = GetEnvironmentVariableW(L"LOCALAPPDATA", localAppData, static_cast<DWORD>(std::size(localAppData)));
    if (len > 0 && len < std::size(localAppData)) {
        return std::filesystem::path(localAppData) / kAppTitle / L"live_repo_cache";
    }

    wchar_t profile[MAX_PATH]{};
    len = GetEnvironmentVariableW(L"USERPROFILE", profile, static_cast<DWORD>(std::size(profile)));
    if (len > 0 && len < std::size(profile)) {
        return std::filesystem::path(profile) / L".cache" / L"school-random-program" / L"live_repo_cache";
    }
    return ExeDir() / L"live_repo_cache";
}

bool ReadFileBytes(const std::filesystem::path& path, std::vector<unsigned char>& out) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return false;
    }
    file.seekg(0, std::ios::end);
    std::streamoff size = file.tellg();
    if (size <= 0 || static_cast<size_t>(size) > kMaxPayloadSize) {
        return false;
    }
    file.seekg(0, std::ios::beg);
    out.resize(static_cast<size_t>(size));
    file.read(reinterpret_cast<char*>(out.data()), size);
    return file.good();
}

void WriteFileBytes(const std::filesystem::path& path, const std::vector<unsigned char>& data) {
    std::error_code ignored;
    std::filesystem::create_directories(path.parent_path(), ignored);
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (file) {
        file.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
    }
}

bool ReadDiskCache(const std::string& filename, std::vector<unsigned char>& out) {
    return ReadFileBytes(CacheDir() / Utf8ToWide(filename), out);
}

void StorePayload(const std::string& filename, const std::vector<unsigned char>& payload) {
    {
        std::lock_guard<std::mutex> lock(g_cacheMutex);
        g_cache[filename] = payload;
    }
    WriteFileBytes(CacheDir() / Utf8ToWide(filename), payload);
}

bool GetCachedPayload(const std::string& filename, std::vector<unsigned char>& out) {
    {
        std::lock_guard<std::mutex> lock(g_cacheMutex);
        auto found = g_cache.find(filename);
        if (found != g_cache.end()) {
            out = found->second;
            return true;
        }
    }

    if (!ReadDiskCache(filename, out)) {
        return false;
    }
    StorePayload(filename, out);
    return true;
}

bool ReadLocalPayload(const std::string& filename, std::vector<unsigned char>& out) {
    std::filesystem::path wideName = Utf8ToWide(filename);
    return ReadFileBytes(ExeDir() / wideName, out) || ReadFileBytes(wideName, out);
}

bool FetchRemotePayload(const std::string& filename, std::vector<unsigned char>& out) {
    std::wstring remotePath = std::wstring(kRawPrefix) + Utf8ToWide(filename);
    HINTERNET session = WinHttpOpen(L"SchoolRandomProgram/1.0",
                                    WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                    WINHTTP_NO_PROXY_NAME,
                                    WINHTTP_NO_PROXY_BYPASS,
                                    0);
    if (!session) {
        return false;
    }
    WinHttpSetTimeouts(session, kTimeoutMs, kTimeoutMs, kTimeoutMs, kTimeoutMs);

    HINTERNET connect = WinHttpConnect(session, kRawHost, INTERNET_DEFAULT_HTTPS_PORT, 0);
    HINTERNET request = connect ? WinHttpOpenRequest(connect,
                                                     L"GET",
                                                     remotePath.c_str(),
                                                     nullptr,
                                                     WINHTTP_NO_REFERER,
                                                     WINHTTP_DEFAULT_ACCEPT_TYPES,
                                                     WINHTTP_FLAG_SECURE)
                                : nullptr;
    bool ok = false;
    if (request &&
        WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0, nullptr, 0, 0, 0) &&
        WinHttpReceiveResponse(request, nullptr)) {
        DWORD status = 0;
        DWORD statusSize = sizeof(status);
        WinHttpQueryHeaders(request,
                            WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                            WINHTTP_HEADER_NAME_BY_INDEX,
                            &status,
                            &statusSize,
                            WINHTTP_NO_HEADER_INDEX);
        if (status == 200) {
            out.clear();
            for (;;) {
                DWORD available = 0;
                if (!WinHttpQueryDataAvailable(request, &available) || available == 0) {
                    break;
                }
                if (out.size() + available > kMaxPayloadSize) {
                    out.clear();
                    break;
                }
                size_t oldSize = out.size();
                out.resize(oldSize + available);
                DWORD read = 0;
                if (!WinHttpReadData(request, out.data() + oldSize, available, &read)) {
                    out.clear();
                    break;
                }
                out.resize(oldSize + read);
            }
            ok = !out.empty();
        }
    }

    if (request) WinHttpCloseHandle(request);
    if (connect) WinHttpCloseHandle(connect);
    WinHttpCloseHandle(session);
    return ok;
}

void WarmCacheFromDisk() {
    for (const char* filename : kCacheFiles) {
        std::vector<unsigned char> payload;
        if (ReadDiskCache(filename, payload)) {
            StorePayload(filename, payload);
        }
    }
}

void RefreshCacheFromRemoteOnce() {
    for (const char* filename : kCacheFiles) {
        std::vector<unsigned char> payload;
        if (FetchRemotePayload(filename, payload)) {
            StorePayload(filename, payload);
        }
    }
}

const Route* FindRoute(const std::string& path) {
    for (const auto& route : kRoutes) {
        if (path == route.path) {
            return &route;
        }
    }
    return nullptr;
}

void SendAll(SOCKET client, const char* data, int length) {
    int sent = 0;
    while (sent < length) {
        int n = send(client, data + sent, length - sent, 0);
        if (n <= 0) {
            return;
        }
        sent += n;
    }
}

void SendPayloadResponse(SOCKET client, const Route& route, const std::vector<unsigned char>& payload, const char* source) {
    char header[1024]{};
    int headerLen = snprintf(header,
                             sizeof(header),
                             "HTTP/1.1 200 OK\r\n"
                             "Content-Type: %s\r\n"
                             "Cache-Control: no-store\r\n"
                             "X-Source: %s\r\n"
                             "Content-Length: %zu\r\n"
                             "Connection: close\r\n\r\n",
                             route.contentType,
                             source,
                             payload.size());
    SendAll(client, header, headerLen);
    SendAll(client, reinterpret_cast<const char*>(payload.data()), static_cast<int>(payload.size()));
}

void SendErrorResponse(SOCKET client, int code, const char* text) {
    char body[256]{};
    int bodyLen = snprintf(body, sizeof(body), "%d %s\n", code, text);
    char header[512]{};
    int headerLen = snprintf(header,
                             sizeof(header),
                             "HTTP/1.1 %d %s\r\n"
                             "Content-Type: text/plain; charset=utf-8\r\n"
                             "Content-Length: %d\r\n"
                             "Connection: close\r\n\r\n",
                             code,
                             text,
                             bodyLen);
    SendAll(client, header, headerLen);
    SendAll(client, body, bodyLen);
}

void HandleClient(SOCKET client) {
    char request[4096]{};
    int received = recv(client, request, sizeof(request) - 1, 0);
    if (received <= 0) {
        closesocket(client);
        return;
    }

    char method[16]{};
    char rawPath[1024]{};
    if (sscanf_s(request, "%15s %1023s", method, static_cast<unsigned>(std::size(method)), rawPath, static_cast<unsigned>(std::size(rawPath))) != 2 ||
        strcmp(method, "GET") != 0) {
        SendErrorResponse(client, 405, "Method Not Allowed");
        closesocket(client);
        return;
    }

    std::string path(rawPath);
    size_t query = path.find('?');
    if (query != std::string::npos) {
        path.resize(query);
    }

    const Route* route = FindRoute(path);
    if (!route) {
        SendErrorResponse(client, 404, "Not Found");
        closesocket(client);
        return;
    }

    std::vector<unsigned char> payload;
    if (GetCachedPayload(route->filename, payload)) {
        SendPayloadResponse(client, *route, payload, "cache");
    } else if (ReadLocalPayload(route->filename, payload)) {
        SendPayloadResponse(client, *route, payload, "local");
    } else {
        SendErrorResponse(client, 502, "Cannot load source from cache.");
    }
    closesocket(client);
}

bool StartServer(int& port) {
    g_serverSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (g_serverSocket == INVALID_SOCKET) {
        return false;
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;

    if (bind(g_serverSocket, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR ||
        listen(g_serverSocket, SOMAXCONN) == SOCKET_ERROR) {
        closesocket(g_serverSocket);
        g_serverSocket = INVALID_SOCKET;
        return false;
    }

    int len = sizeof(addr);
    if (getsockname(g_serverSocket, reinterpret_cast<sockaddr*>(&addr), &len) == SOCKET_ERROR) {
        closesocket(g_serverSocket);
        g_serverSocket = INVALID_SOCKET;
        return false;
    }

    port = ntohs(addr.sin_port);
    std::thread([] {
        while (g_running) {
            SOCKET client = accept(g_serverSocket, nullptr, nullptr);
            if (client == INVALID_SOCKET) {
                break;
            }
            std::thread(HandleClient, client).detach();
        }
    }).detach();
    return true;
}

void ResizeWebView() {
    if (!g_webviewController || !g_mainWindow) {
        return;
    }
    RECT bounds{};
    GetClientRect(g_mainWindow, &bounds);
    g_webviewController->put_Bounds(bounds);
}

void ToggleFullscreen(HWND hwnd) {
    static WINDOWPLACEMENT placement{sizeof(WINDOWPLACEMENT)};
    static LONG_PTR previousStyle = 0;
    LONG_PTR style = GetWindowLongPtrW(hwnd, GWL_STYLE);

    if (style & WS_OVERLAPPEDWINDOW) {
        MONITORINFO monitorInfo{sizeof(MONITORINFO)};
        if (GetWindowPlacement(hwnd, &placement) &&
            GetMonitorInfoW(MonitorFromWindow(hwnd, MONITOR_DEFAULTTOPRIMARY), &monitorInfo)) {
            previousStyle = style;
            SetWindowLongPtrW(hwnd, GWL_STYLE, style & ~WS_OVERLAPPEDWINDOW);
            SetWindowPos(hwnd,
                         HWND_TOP,
                         monitorInfo.rcMonitor.left,
                         monitorInfo.rcMonitor.top,
                         monitorInfo.rcMonitor.right - monitorInfo.rcMonitor.left,
                         monitorInfo.rcMonitor.bottom - monitorInfo.rcMonitor.top,
                         SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
        }
    } else {
        SetWindowLongPtrW(hwnd, GWL_STYLE, previousStyle ? previousStyle : (style | WS_OVERLAPPEDWINDOW));
        SetWindowPlacement(hwnd, &placement);
        SetWindowPos(hwnd, nullptr, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
    }
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
    switch (message) {
        case WM_SIZE:
            ResizeWebView();
            return 0;
        case WM_KEYDOWN:
            if (wparam == VK_F11) {
                ToggleFullscreen(hwnd);
                return 0;
            }
            break;
        case WM_DESTROY:
            g_running = false;
            if (g_serverSocket != INVALID_SOCKET) {
                closesocket(g_serverSocket);
                g_serverSocket = INVALID_SOCKET;
            }
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(hwnd, message, wparam, lparam);
}

bool CreateMainWindow(HINSTANCE instance) {
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = WndProc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hIcon = static_cast<HICON>(LoadImageW(nullptr, (ExeDir() / L"app_icon.ico").c_str(), IMAGE_ICON, 32, 32, LR_LOADFROMFILE));
    wc.hIconSm = static_cast<HICON>(LoadImageW(nullptr, (ExeDir() / L"app_icon.ico").c_str(), IMAGE_ICON, 16, 16, LR_LOADFROMFILE));
    wc.lpszClassName = L"SchoolRandomProgramWindow";
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);

    if (!RegisterClassExW(&wc)) {
        return false;
    }

    RECT rect{0, 0, kWindowWidth, kWindowHeight};
    AdjustWindowRect(&rect, WS_OVERLAPPEDWINDOW, FALSE);
    g_mainWindow = CreateWindowExW(0,
                                   wc.lpszClassName,
                                   kAppTitle,
                                   WS_OVERLAPPEDWINDOW,
                                   CW_USEDEFAULT,
                                   CW_USEDEFAULT,
                                   rect.right - rect.left,
                                   rect.bottom - rect.top,
                                   nullptr,
                                   nullptr,
                                   instance,
                                   nullptr);
    return g_mainWindow != nullptr;
}

void CreateWebView(const std::wstring& url) {
    std::filesystem::path userData = CacheDir() / L"webview2_user_data";
    std::error_code ignored;
    std::filesystem::create_directories(userData, ignored);

    HRESULT hr = CreateCoreWebView2EnvironmentWithOptions(
        nullptr,
        userData.c_str(),
        nullptr,
        Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
            [url](HRESULT result, ICoreWebView2Environment* environment) -> HRESULT {
                if (FAILED(result) || !environment) {
                    MessageBoxW(g_mainWindow, L"WebView2 runtime load failed.", kAppTitle, MB_ICONERROR);
                    return result;
                }
                return environment->CreateCoreWebView2Controller(
                    g_mainWindow,
                    Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
                        [url](HRESULT controllerResult, ICoreWebView2Controller* controller) -> HRESULT {
                            if (FAILED(controllerResult) || !controller) {
                                MessageBoxW(g_mainWindow, L"WebView2 controller creation failed.", kAppTitle, MB_ICONERROR);
                                return controllerResult;
                            }
                            g_webviewController = controller;
                            g_webviewController->get_CoreWebView2(&g_webview);
                            ResizeWebView();
                            if (g_webview) {
                                g_webview->Navigate(url.c_str());
                            }
                            return S_OK;
                        })
                        .Get());
            })
            .Get());

    if (FAILED(hr)) {
        MessageBoxW(g_mainWindow, L"WebView2 is not installed. Install Microsoft Edge WebView2 Runtime.", kAppTitle, MB_ICONERROR);
    }
}

}  // namespace

int APIENTRY wWinMain(HINSTANCE instance, HINSTANCE, LPWSTR, int showCommand) {
    HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(com)) {
        MessageBoxW(nullptr, L"COM initialization failed.", kAppTitle, MB_ICONERROR);
        return 1;
    }

    WSADATA wsa{};
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        MessageBoxW(nullptr, L"Network initialization failed.", kAppTitle, MB_ICONERROR);
        CoUninitialize();
        return 1;
    }

    WarmCacheFromDisk();
    RefreshCacheFromRemoteOnce();

    int port = 0;
    if (!StartServer(port)) {
        MessageBoxW(nullptr, L"Local server failed to start.", kAppTitle, MB_ICONERROR);
        WSACleanup();
        CoUninitialize();
        return 1;
    }

    if (!CreateMainWindow(instance)) {
        MessageBoxW(nullptr, L"Window creation failed.", kAppTitle, MB_ICONERROR);
        g_running = false;
        closesocket(g_serverSocket);
        WSACleanup();
        CoUninitialize();
        return 1;
    }

    wchar_t url[128]{};
    swprintf_s(url, L"http://127.0.0.1:%d/", port);
    CreateWebView(url);

    ShowWindow(g_mainWindow, showCommand);
    UpdateWindow(g_mainWindow);

    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }

    g_webview.Reset();
    g_webviewController.Reset();
    WSACleanup();
    CoUninitialize();
    return static_cast<int>(message.wParam);
}
