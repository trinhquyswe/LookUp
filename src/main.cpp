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

#include "D2DRenderer.h"

// Custom window messages
#define WM_TRAYICON (WM_USER + 1)

// Menu and Tray IDs
#define TRAY_ICON_ID 1
#define ID_TRAY_SHOW 1001
#define ID_TRAY_EXIT 1002

// Global and local definitions
const wchar_t CLASS_NAME[] = L"LookUpMainWindowClass";
const wchar_t WINDOW_TITLE[] = L"LookUp - Dashboard";

// Structure to hold Application State
struct AppState {
    HWND hwndMain = nullptr;
    NOTIFYICONDATAW nid = {};
    bool isVisible = true;
    D2DRenderer* renderer = nullptr;
    wchar_t loadedFontPath[MAX_PATH] = {};

    ~AppState() {
        if (renderer) {
            delete renderer;
            renderer = nullptr;
        }
        if (loadedFontPath[0] != L'\0') {
            D2DRenderer::UnloadPrivateFont(loadedFontPath);
        }
    }
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
    wc.hbrBackground = nullptr; // Set to nullptr to avoid Win32 window background flicker

    if (!RegisterClassExW(&wc)) {
        MessageBoxW(nullptr, L"Failed to register window class.", L"LookUp Error", MB_OK | MB_ICONERROR);
        CoUninitialize();
        return 1;
    }

    // Allocate AppState on heap
    AppState* state = new AppState();

    // Determine font path relative to the executable
    wchar_t exePath[MAX_PATH];
    if (GetModuleFileNameW(nullptr, exePath, MAX_PATH)) {
        wchar_t* lastSlash = wcsrchr(exePath, L'\\');
        if (lastSlash) {
            *lastSlash = L'\0';
        }
        StringCchPrintfW(state->loadedFontPath, MAX_PATH, L"%s\\assets\\fonts\\Inter.ttf", exePath);
        
        // Load custom font (adds to process-local system font table)
        if (!D2DRenderer::LoadPrivateFont(state->loadedFontPath)) {
            // If local copy fails, log warning. DirectWrite will fallback to Segoe UI
            state->loadedFontPath[0] = L'\0'; 
        }
    }

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
        state                           // Pass state to WM_NCCREATE
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
                // Initialize Direct2D Renderer
                state->renderer = new D2DRenderer();
                if (FAILED(state->renderer->Initialize(hwnd))) {
                    delete state->renderer;
                    state->renderer = nullptr;
                    return -1; // Fails window creation
                }
                SetupTrayIcon(hwnd, state);
            }
            return 0;
        }
        case WM_SIZE: {
            UINT width = LOWORD(lParam);
            UINT height = HIWORD(lParam);
            if (state && state->renderer) {
                state->renderer->Resize(width, height);
            }
            return 0;
        }
        case WM_PAINT: {
            PAINTSTRUCT ps;
            BeginPaint(hwnd, &ps);

            if (state && state->renderer) {
                if (SUCCEEDED(state->renderer->BeginDraw())) {
                    ID2D1HwndRenderTarget* rt = state->renderer->GetRenderTarget();
                    RECT rc;
                    GetClientRect(hwnd, &rc);

                    FLOAT width = static_cast<FLOAT>(rc.right - rc.left);
                    FLOAT height = static_cast<FLOAT>(rc.bottom - rc.top);

                    // 1. Create a beautiful gradient brush for the background
                    ID2D1LinearGradientBrush* pGradientBrush = nullptr;
                    ID2D1GradientStopCollection* pStops = nullptr;
                    D2D1_GRADIENT_STOP stops[2];
                    stops[0].color = D2D1::ColorF(0.05f, 0.06f, 0.12f, 1.0f); // Very dark indigo
                    stops[0].position = 0.0f;
                    stops[1].color = D2D1::ColorF(0.12f, 0.14f, 0.28f, 1.0f); // Sleek medium blue
                    stops[1].position = 1.0f;

                    HRESULT hr = rt->CreateGradientStopCollection(
                        stops, 
                        2, 
                        D2D1_GAMMA_2_2, 
                        D2D1_EXTEND_MODE_CLAMP, 
                        &pStops
                    );

                    if (SUCCEEDED(hr)) {
                        hr = rt->CreateLinearGradientBrush(
                            D2D1::LinearGradientBrushProperties(
                                D2D1::Point2F(0.0f, 0.0f),
                                D2D1::Point2F(width, height)
                            ),
                            pStops,
                            &pGradientBrush
                        );
                    }

                    // Draw background gradient
                    if (pGradientBrush) {
                        rt->FillRectangle(D2D1::RectF(0.0f, 0.0f, width, height), pGradientBrush);
                    } else {
                        rt->Clear(D2D1::ColorF(0.07f, 0.08f, 0.14f, 1.0f));
                    }

                    // 2. Draw a subtle glowing inner border
                    ID2D1SolidColorBrush* pGlowBrush = nullptr;
                    rt->CreateSolidColorBrush(D2D1::ColorF(0.38f, 0.49f, 0.96f, 0.25f), &pGlowBrush);
                    if (pGlowBrush) {
                        rt->DrawRectangle(
                            D2D1::RectF(10.0f, 10.0f, width - 10.0f, height - 10.0f), 
                            pGlowBrush, 
                            2.0f
                        );
                    }

                    // 3. Draw premium text layout using "Inter" font
                    IDWriteTextFormat* pTitleFormat = nullptr;
                    hr = state->renderer->CreateTextFormat(
                        L"Inter",
                        32.0f,
                        DWRITE_FONT_WEIGHT_BOLD,
                        DWRITE_FONT_STYLE_NORMAL,
                        DWRITE_FONT_STRETCH_NORMAL,
                        &pTitleFormat
                    );

                    if (SUCCEEDED(hr)) {
                        pTitleFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
                        pTitleFormat->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

                        ID2D1SolidColorBrush* pTextBrush = nullptr;
                        rt->CreateSolidColorBrush(D2D1::ColorF(D2D1::ColorF::White), &pTextBrush);

                        if (pTextBrush) {
                            const wchar_t text[] = L"LookUp\n\nDirect2D + DirectWrite (Inter Font)\nInitialized Successfully";
                            D2D1_RECT_F textRect = D2D1::RectF(20.0f, 20.0f, width - 20.0f, height - 20.0f);
                            rt->DrawTextW(
                                text,
                                ARRAYSIZE(text) - 1,
                                pTitleFormat,
                                textRect,
                                pTextBrush
                            );
                            SafeRelease(&pTextBrush);
                        }
                        SafeRelease(&pTitleFormat);
                    }

                    // Clean up paint resources
                    SafeRelease(&pGlowBrush);
                    SafeRelease(&pGradientBrush);
                    SafeRelease(&pStops);

                    state->renderer->EndDraw();
                }
            }

            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_CLOSE: {
            ShowWindow(hwnd, SW_HIDE);
            if (state) {
                state->isVisible = false;
            }
            return 0;
        }
        case WM_DESTROY: {
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
    
    StringCchCopyW(state->nid.szTip, ARRAYSIZE(state->nid.szTip), L"LookUp - Native Dictionary");
    
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

        SetForegroundWindow(hwnd);

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
