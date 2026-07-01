#define WIN32_LEAN_AND_MEAN
#define UNICODE
#define _UNICODE

#include <algorithm>
#include <atomic>
#include <filesystem>
#include <string>
#include <windows.h>
#include <shlobj.h>
#include <wrl.h>
#include <WebView2.h>

#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")

using Microsoft::WRL::Callback;
using Microsoft::WRL::ComPtr;

namespace {

    constexpr wchar_t kAppTitle[] = L"School Random Program";
    constexpr wchar_t kTargetUrl[] = L"https://randompagea.netlify.app/";
    constexpr int kWindowWidth = 1440;
    constexpr int kWindowHeight = 900;

    HWND g_mainWindow = nullptr;
    ComPtr<ICoreWebView2Controller> g_webviewController;
    ComPtr<ICoreWebView2> g_webview;

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
            return std::filesystem::path(localAppData) / kAppTitle / L"webview_cache";
        }

        wchar_t profile[MAX_PATH]{};
        len = GetEnvironmentVariableW(L"USERPROFILE", profile, static_cast<DWORD>(std::size(profile)));
        if (len > 0 && len < std::size(profile)) {
            return std::filesystem::path(profile) / L".cache" / L"school-random-program" / L"webview_cache";
        }
        return ExeDir() / L"webview_cache";
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
        static WINDOWPLACEMENT placement{ sizeof(WINDOWPLACEMENT) };
        static LONG_PTR previousStyle = 0;
        LONG_PTR style = GetWindowLongPtrW(hwnd, GWL_STYLE);

        if (style & WS_OVERLAPPEDWINDOW) {
            MONITORINFO monitorInfo{ sizeof(MONITORINFO) };
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
        }
        else {
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

        RECT rect{ 0, 0, kWindowWidth, kWindowHeight };
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

                                // [핵심 해결책]: WebView2가 독점한 키보드 이벤트 중 F11만 부모 창으로 가로챕니다.
                                g_webviewController->add_AcceleratorKeyPressed(
                                    Callback<ICoreWebView2AcceleratorKeyPressedEventHandler>(
                                        [](ICoreWebView2Controller* sender, ICoreWebView2AcceleratorKeyPressedEventArgs* args) -> HRESULT {
                                            COREWEBVIEW2_KEY_EVENT_KIND kind;
                                            args->get_KeyEventKind(&kind);
                                            
                                            if (kind == COREWEBVIEW2_KEY_EVENT_KIND_KEY_DOWN || 
                                                kind == COREWEBVIEW2_KEY_EVENT_KIND_SYSTEM_KEY_DOWN) {
                                                UINT key;
                                                args->get_VirtualKey(&key);
                                                
                                                if (key == VK_F11) {
                                                    ToggleFullscreen(g_mainWindow);
                                                    args->put_Handled(TRUE); // 브라우저 내부 기능 동작 방지 및 이벤트 소비 완료 처리
                                                    return S_OK;
                                                }
                                            }
                                            return S_OK;
                                        }).Get(), nullptr);

                                if (g_webview) {
                                    g_webview->Navigate(url.c_str());
                                }
                                return S_OK;
                            })
                        .Get());
                }
            ).Get());

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

    if (!CreateMainWindow(instance)) {
        MessageBoxW(nullptr, L"Window creation failed.", kAppTitle, MB_ICONERROR);
        CoUninitialize();
        return 1;
    }

    CreateWebView(kTargetUrl);

    ShowWindow(g_mainWindow, showCommand);
    UpdateWindow(g_mainWindow);

    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }

    g_webview.Reset();
    g_webviewController.Reset();
    CoUninitialize();
    return static_cast<int>(message.wParam);
}