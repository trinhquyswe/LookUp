#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <strsafe.h>
#include <objbase.h>

// Custom window messages
#define WM_TRAYICON (WM_USER + 1)

// Menu and Tray IDs
#define TRAY_ICON_ID 1
#define ID_TRAY_SHOW 1001
#define ID_TRAY_EXIT 1002

// Global and local definitions
const wchar_t CLASS_NAME[] = L"LookUpMainWindowClass";
const wchar_t WINDOW_TITLE[] = L"LookUp - Dashboard";

// Structure to hold Application State (will grow in subsequent phases)
struct AppState {
    HWND hwndMain = nullptr;
    NOTIFYICONDATAW nid = {};
    bool isVisible = true;
};

// Function prototypes
LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
void ShowContextMenu(HWND hwnd);
void SetupTrayIcon(HWND hwnd, AppState* state);
void RemoveTrayIcon(AppState* state);

int WINAPI wWinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE hPrevInstance, _In_ PWSTR lpCmdLine, _In_ int nCmdShow) {
    UNREFERENCED_PARAMETER(hPrevInstance);
    UNREFERENCED_PARAMETER(lpCmdLine);

    // Initialize COM library (needed for Direct2D, DirectWrite, and C++/WinRT audio later)
    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    if (FAILED(hr)) {
        MessageBoxW(nullptr, L"Failed to initialize COM library.", L"LookUp Error", MB_OK | MB_ICONERROR);
        return 1;
    }

    // Register the window class
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(WNDCLASSEXW);
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = CLASS_NAME;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hIcon = LoadIcon(nullptr, IDI_APPLICATION); // Will use custom icon in later phases
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);

    if (!RegisterClassExW(&wc)) {
        MessageBoxW(nullptr, L"Failed to register window class.", L"LookUp Error", MB_OK | MB_ICONERROR);
        CoUninitialize();
        return 1;
    }

    // Allocate AppState on heap to associate with HWND
    AppState* state = new AppState();

    // Define window size and center it
    int screenWidth = GetSystemMetrics(SM_CXSCREEN);
    int screenHeight = GetSystemMetrics(SM_CYSCREEN);
    int windowWidth = 800;
    int windowHeight = 600;
    int xPos = (screenWidth - windowWidth) / 2;
    int yPos = (screenHeight - windowHeight) / 2;

    // Create the window
    HWND hwnd = CreateWindowExW(
        0,                              // Optional window styles
        CLASS_NAME,                     // Window class
        WINDOW_TITLE,                   // Window text
        WS_OVERLAPPEDWINDOW,            // Window style
        xPos, yPos,                     // Position
        windowWidth, windowHeight,       // Size
        nullptr,                        // Parent window    
        nullptr,                        // Menu
        hInstance,                      // Instance handle
        state                           // Additional application data
    );

    if (hwnd == nullptr) {
        MessageBoxW(nullptr, L"Failed to create window.", L"LookUp Error", MB_OK | MB_ICONERROR);
        delete state;
        CoUninitialize();
        return 1;
    }

    state->hwndMain = hwnd;

    // Show the window
    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);

    // Run the message loop
    MSG msg = {};
    while (GetMessage(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    delete state;
    CoUninitialize();
    return 0;
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    AppState* state = reinterpret_cast<AppState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));

    switch (uMsg) {
        case WM_NCCREATE: {
            CREATESTRUCTW* pCreate = reinterpret_cast<CREATESTRUCTW*>(lParam);
            state = reinterpret_cast<AppState*>(pCreate->lpCreateParams);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
            break;
        }
        case WM_CREATE: {
            if (state) {
                SetupTrayIcon(hwnd, state);
            }
            return 0;
        }
        case WM_CLOSE: {
            // Minimize or Hide to System Tray instead of destroying the window directly
            ShowWindow(hwnd, SW_HIDE);
            if (state) {
                state->isVisible = false;
            }
            return 0; // Prevent default window destruction
        }
        case WM_DESTROY: {
            if (state) {
                RemoveTrayIcon(state);
            }
            PostQuitMessage(0);
            return 0;
        }
        case WM_TRAYICON: {
            switch (LOWORD(lParam)) {
                case WM_LBUTTONDBLCLK:
                case WM_LBUTTONUP: {
                    ShowWindow(hwnd, SW_SHOW);
                    ShowWindow(hwnd, SW_RESTORE);
                    SetForegroundWindow(hwnd);
                    if (state) {
                        state->isVisible = true;
                    }
                    break;
                }
                case WM_RBUTTONUP: {
                    ShowContextMenu(hwnd);
                    break;
                }
            }
            return 0;
        }
        case WM_COMMAND: {
            // Handle context menu command selections
            int wmId = LOWORD(wParam);
            switch (wmId) {
                case ID_TRAY_SHOW: {
                    ShowWindow(hwnd, SW_SHOW);
                    ShowWindow(hwnd, SW_RESTORE);
                    SetForegroundWindow(hwnd);
                    if (state) {
                        state->isVisible = true;
                    }
                    break;
                }
                case ID_TRAY_EXIT: {
                    DestroyWindow(hwnd);
                    break;
                }
            }
            return 0;
        }
    }
    return DefWindowProcW(hwnd, uMsg, wParam, lParam);
}

void SetupTrayIcon(HWND hwnd, AppState* state) {
    state->nid.cbSize = sizeof(NOTIFYICONDATAW);
    state->nid.hWnd = hwnd;
    state->nid.uID = TRAY_ICON_ID;
    state->nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP | NIF_INFO;
    state->nid.uCallbackMessage = WM_TRAYICON;
    state->nid.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
    
    // Set Tooltip
    StringCchCopyW(state->nid.szTip, ARRAYSIZE(state->nid.szTip), L"LookUp - Native Dictionary");
    
    // Set Balloon notifications for activation
    StringCchCopyW(state->nid.szInfoTitle, ARRAYSIZE(state->nid.szInfoTitle), L"LookUp Running");
    StringCchCopyW(state->nid.szInfo, ARRAYSIZE(state->nid.szInfo), L"LookUp is now active. Double-click the tray icon to open the dashboard.");
    state->nid.dwInfoFlags = NIIF_INFO;

    Shell_NotifyIconW(NIM_ADD, &state->nid);
}

void RemoveTrayIcon(AppState* state) {
    Shell_NotifyIconW(NIM_DELETE, &state->nid);
}

void ShowContextMenu(HWND hwnd) {
    HMENU hMenu = CreatePopupMenu();
    if (hMenu) {
        AppendMenuW(hMenu, MF_STRING, ID_TRAY_SHOW, L"Show Dashboard");
        AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(hMenu, MF_STRING, ID_TRAY_EXIT, L"Exit");

        POINT pt;
        GetCursorPos(&pt);

        // Required to ensure the menu behaves properly and closes when clicked outside
        SetForegroundWindow(hwnd);

        // TrackPopupMenu blocks until an item is selected or the menu is dismissed
        TrackPopupMenu(
            hMenu,
            TPM_RIGHTBUTTON | TPM_LEFTALIGN,
            pt.x, pt.y,
            0,
            hwnd,
            nullptr
        );

        DestroyMenu(hMenu);
    }
}
