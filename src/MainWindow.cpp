#include "MainWindow.h"
#include <shlobj.h>
#include <knownfolders.h>
#include <fstream>
#include <nlohmann/json.hpp>

#include <thread>
#include "DictionaryClient.h"

using json = nlohmann::json;

#define WM_TRIGGER_LOOKUP (WM_USER + 2)
#define WM_SHOW_POPUP     (WM_USER + 3)

HWND MainWindow::s_hwndMain = nullptr;
HHOOK MainWindow::s_hMouseHook = nullptr;
DWORD MainWindow::s_lastClickTime = 0;

MainWindow::MainWindow() :
    m_hwnd(nullptr),
    m_nid({}),
    m_isVisible(true),
    m_pRenderer(nullptr),
    m_isRecording(false),
    m_tempHotkey({}),
    m_hoverToggle(false),
    m_hoverHotkey(false),
    m_hHookThread(nullptr),
    m_hookThreadId(0) {
}

MainWindow::~MainWindow() {
    RemoveTrayIcon();
    if (m_hHookThread) {
        PostThreadMessageW(m_hookThreadId, WM_QUIT, 0, 0);
        WaitForSingleObject(m_hHookThread, 1000);
        CloseHandle(m_hHookThread);
        m_hHookThread = nullptr;
    }
    if (m_hwnd) {
        UnregisterHotKey(m_hwnd, 1);
    }
    if (m_pRenderer) {
        delete m_pRenderer;
        m_pRenderer = nullptr;
    }
}

void MainWindow::LoadSettingsData() {
    // Determine the user's roaming AppData directory
    PWSTR pszPath = nullptr;
    HRESULT hr = SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &pszPath);
    if (SUCCEEDED(hr)) {
        std::wstring path(pszPath);
        CoTaskMemFree(pszPath);
        
        // Ensure folder exists
        path += L"\\LookUp";
        CreateDirectoryW(path.c_str(), nullptr);
        
        m_settingsPath = path + L"\\config.json";
        
        // Try reading from file
        std::ifstream file(m_settingsPath);
        if (file.is_open()) {
            try {
                json j;
                file >> j;
                if (j.contains("middleClickTrigger")) m_settings.middleClickTrigger = j["middleClickTrigger"].get<bool>();
                if (j.contains("ctrl")) m_settings.ctrl = j["ctrl"].get<bool>();
                if (j.contains("shift")) m_settings.shift = j["shift"].get<bool>();
                if (j.contains("alt")) m_settings.alt = j["alt"].get<bool>();
                if (j.contains("win")) m_settings.win = j["win"].get<bool>();
                if (j.contains("vkCode")) m_settings.vkCode = j["vkCode"].get<UINT>();
            } catch (...) {
                // File corrupted or bad structure, fall back to defaults
            }
        }
    }
}

void MainWindow::SaveSettingsData() {
    if (m_settingsPath.empty()) return;
    
    json j;
    j["middleClickTrigger"] = m_settings.middleClickTrigger;
    j["ctrl"] = m_settings.ctrl;
    j["shift"] = m_settings.shift;
    j["alt"] = m_settings.alt;
    j["win"] = m_settings.win;
    j["vkCode"] = m_settings.vkCode;
    
    std::ofstream file(m_settingsPath);
    if (file.is_open()) {
        file << j.dump(4);
    }
}

void MainWindow::UpdateRegisteredHotkey() {
    if (!m_hwnd) return;
    
    UnregisterHotKey(m_hwnd, 1);
    
    if (m_settings.vkCode != 0) {
        UINT modifiers = MOD_NOREPEAT;
        if (m_settings.ctrl) modifiers |= MOD_CONTROL;
        if (m_settings.shift) modifiers |= MOD_SHIFT;
        if (m_settings.alt) modifiers |= MOD_ALT;
        if (m_settings.win) modifiers |= MOD_WIN;
        
        RegisterHotKey(m_hwnd, 1, modifiers, m_settings.vkCode);
    }
}

bool MainWindow::Create(HINSTANCE hInstance, int nCmdShow) {
    LoadSettingsData();

    // Register Window Class
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(WNDCLASSEXW);
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = L"LookUpMainWindowClass";
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
    wc.hbrBackground = nullptr; // Prevent background brush repaint flicker

    if (!RegisterClassExW(&wc)) {
        return false;
    }

    // Get screen dimensions to center window
    int screenWidth = GetSystemMetrics(SM_CXSCREEN);
    int screenHeight = GetSystemMetrics(SM_CYSCREEN);
    int windowWidth = 800;
    int windowHeight = 600;
    int xPos = (screenWidth - windowWidth) / 2;
    int yPos = (screenHeight - windowHeight) / 2;

    HWND hwnd = CreateWindowExW(
        0,
        L"LookUpMainWindowClass",
        L"LookUp - Dashboard",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX, // Fixed size window for crisp pixel layout
        xPos, yPos,
        windowWidth, windowHeight,
        nullptr,
        nullptr,
        hInstance,
        this // Pass pointer to this instance
    );

    if (!hwnd) return false;

    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);
    
    return true;
}

int MainWindow::RunMessageLoop() {
    MSG msg = {};
    while (GetMessage(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    return static_cast<int>(msg.wParam);
}

LRESULT CALLBACK MainWindow::WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    if (uMsg == WM_NCCREATE) {
        CREATESTRUCTW* pCreate = reinterpret_cast<CREATESTRUCTW*>(lParam);
        MainWindow* pThis = reinterpret_cast<MainWindow*>(pCreate->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(pThis));
        pThis->m_hwnd = hwnd;
    }
    
    MainWindow* pThis = reinterpret_cast<MainWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (pThis) {
        return pThis->HandleMessage(uMsg, wParam, lParam);
    }
    
    return DefWindowProcW(hwnd, uMsg, wParam, lParam);
}

LRESULT MainWindow::HandleMessage(UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
        case WM_CREATE: {
            m_pRenderer = new D2DRenderer();
            if (FAILED(m_pRenderer->Initialize(m_hwnd))) {
                return -1; // Fail window creation
            }
            
            // Set up relative assets font load
            wchar_t exePath[MAX_PATH];
            if (GetModuleFileNameW(nullptr, exePath, MAX_PATH)) {
                wchar_t* lastSlash = wcsrchr(exePath, L'\\');
                if (lastSlash) {
                    *lastSlash = L'\0';
                }
                wchar_t fontPath[MAX_PATH];
                StringCchPrintfW(fontPath, MAX_PATH, L"%s\\assets\\fonts\\Inter.ttf", exePath);
                
                // Load font file into local process font table
                D2DRenderer::LoadPrivateFont(fontPath);
            }

            SetupTrayIcon();
            UpdateRegisteredHotkey();
            
            // Initialize popup window
            m_popupWindow.Create(GetModuleHandle(nullptr), m_hwnd);

            // Spawn low-level mouse hook thread
            s_hwndMain = m_hwnd;
            m_hHookThread = CreateThread(nullptr, 0, MouseHookThreadProc, m_hwnd, 0, &m_hookThreadId);
            return 0;
        }
        case WM_SIZE: {
            UINT width = LOWORD(lParam);
            UINT height = HIWORD(lParam);
            if (m_pRenderer) {
                m_pRenderer->Resize(width, height);
            }
            return 0;
        }
        case WM_PAINT: {
            RenderUI();
            return 0;
        }
        case WM_MOUSEMOVE: {
            int x = GET_X_LPARAM(lParam);
            int y = GET_Y_LPARAM(lParam);

            // Bounds hit-testing (Card is centered)
            // Card boundaries: left = 60, top = 160, width = 680, height = 340
            // Toggle Switch bounds: left = 620, top = 220, right = 680, bottom = 250
            // Hotkey Box bounds: left = 480, top = 340, right = 680, bottom = 380
            bool overToggle = (x >= 620 && x <= 680 && y >= 220 && y <= 250);
            bool overHotkey = (x >= 480 && x <= 680 && y >= 340 && y <= 380);

            bool changed = (overToggle != m_hoverToggle) || (overHotkey != m_hoverHotkey);
            m_hoverToggle = overToggle;
            m_hoverHotkey = overHotkey;

            if (changed) {
                // Trigger redrawing to display hover states immediately
                InvalidateRect(m_hwnd, nullptr, FALSE);
            }
            return 0;
        }
        case WM_SETCURSOR: {
            if (LOWORD(lParam) == HTCLIENT) {
                if (m_hoverToggle || m_hoverHotkey) {
                    SetCursor(LoadCursor(nullptr, IDC_HAND));
                    return TRUE;
                }
            }
            break;
        }
        case WM_LBUTTONDOWN: {
            int x = GET_X_LPARAM(lParam);
            int y = GET_Y_LPARAM(lParam);

            // Toggle Switch hit-tested
            if (x >= 620 && x <= 680 && y >= 220 && y <= 250) {
                m_settings.middleClickTrigger = !m_settings.middleClickTrigger;
                SaveSettingsData();
                InvalidateRect(m_hwnd, nullptr, FALSE);
            }
            // Hotkey container hit-tested
            else if (x >= 480 && x <= 680 && y >= 340 && y <= 380) {
                m_isRecording = true;
                m_tempHotkey = { false, false, false, false, 0 }; // Clear
                InvalidateRect(m_hwnd, nullptr, FALSE);
            }
            // Cancel recording when clicking elsewhere
            else {
                if (m_isRecording) {
                    m_isRecording = false;
                    InvalidateRect(m_hwnd, nullptr, FALSE);
                }
            }
            return 0;
        }
        case WM_KEYDOWN:
        case WM_SYSKEYDOWN: {
            if (m_isRecording) {
                bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
                bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
                bool alt = (GetKeyState(VK_MENU) & 0x8000) != 0;
                bool win = ((GetKeyState(VK_LWIN) & 0x8000) != 0) || ((GetKeyState(VK_RWIN) & 0x8000) != 0);

                UINT vk = static_cast<UINT>(wParam);

                if (vk == VK_ESCAPE) {
                    m_isRecording = false;
                    InvalidateRect(m_hwnd, nullptr, FALSE);
                    return 0;
                }

                // If modifier key itself, update modifiers layout but do not finalize yet
                if (vk == VK_CONTROL || vk == VK_SHIFT || vk == VK_MENU || vk == VK_LWIN || vk == VK_RWIN) {
                    m_tempHotkey.ctrl = ctrl;
                    m_tempHotkey.shift = shift;
                    m_tempHotkey.alt = alt;
                    m_tempHotkey.win = win;
                    m_tempHotkey.vkCode = 0;
                    InvalidateRect(m_hwnd, nullptr, FALSE);
                } else {
                    // Valid normal key completes hotkey assignment
                    m_settings.ctrl = ctrl;
                    m_settings.shift = shift;
                    m_settings.alt = alt;
                    m_settings.win = win;
                    m_settings.vkCode = vk;
                    
                    m_isRecording = false;
                    SaveSettingsData();
                    UpdateRegisteredHotkey();
                    
                    // Show confirmation notification
                    std::wstring hotkeyStr = GetHotkeyString(false);
                    std::wstring msg = L"Keyboard trigger shortcut updated to: " + hotkeyStr;
                    ShowTrayNotification(L"Shortcut Updated", msg.c_str());
                    
                    InvalidateRect(m_hwnd, nullptr, FALSE);
                }
                return 0; // Prevent routing keys to OS/standard windows features
            }
            break;
        }
        case WM_HOTKEY: {
            if (wParam == 1) {
                TriggerAsyncLookup();
            }
            return 0;
        }
        case WM_TRIGGER_LOOKUP: {
            if (m_settings.middleClickTrigger) {
                TriggerAsyncLookup();
            }
            return 0;
        }
        case WM_SHOW_POPUP: {
            DictionaryEntry* pEntry = reinterpret_cast<DictionaryEntry*>(wParam);
            if (pEntry) {
                m_popupWindow.Show(pEntry->word, pEntry->phonetic, pEntry->definition, pEntry->audioUrl);
                delete pEntry;
            }
            return 0;
        }
        case WM_CLOSE: {
            ShowWindow(m_hwnd, SW_HIDE);
            m_isVisible = false;
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
                    ShowWindow(m_hwnd, SW_SHOW);
                    ShowWindow(m_hwnd, SW_RESTORE);
                    SetForegroundWindow(m_hwnd);
                    m_isVisible = true;
                    break;
                }
                case WM_RBUTTONUP: {
                    ShowContextMenu();
                    break;
                }
            }
            return 0;
        }
        case WM_COMMAND: {
            int wmId = LOWORD(wParam);
            switch (wmId) {
                case ID_TRAY_SHOW: {
                    ShowWindow(m_hwnd, SW_SHOW);
                    ShowWindow(m_hwnd, SW_RESTORE);
                    SetForegroundWindow(m_hwnd);
                    m_isVisible = true;
                    break;
                }
                case ID_TRAY_EXIT: {
                    DestroyWindow(m_hwnd);
                    break;
                }
            }
            return 0;
        }
    }
    return DefWindowProcW(m_hwnd, uMsg, wParam, lParam);
}

void MainWindow::SetupTrayIcon() {
    m_nid.cbSize = sizeof(NOTIFYICONDATAW);
    m_nid.hWnd = m_hwnd;
    m_nid.uID = TRAY_ICON_ID;
    m_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    m_nid.uCallbackMessage = WM_TRAYICON;
    m_nid.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
    
    StringCchCopyW(m_nid.szTip, ARRAYSIZE(m_nid.szTip), L"LookUp - Native Dictionary");
    Shell_NotifyIconW(NIM_ADD, &m_nid);
}

void MainWindow::RemoveTrayIcon() {
    Shell_NotifyIconW(NIM_DELETE, &m_nid);
}

void MainWindow::ShowContextMenu() {
    HMENU hMenu = CreatePopupMenu();
    if (hMenu) {
        AppendMenuW(hMenu, MF_STRING, ID_TRAY_SHOW, L"Show Dashboard");
        AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(hMenu, MF_STRING, ID_TRAY_EXIT, L"Exit");

        POINT pt;
        GetCursorPos(&pt);
        SetForegroundWindow(m_hwnd);

        TrackPopupMenu(
            hMenu,
            TPM_RIGHTBUTTON | TPM_LEFTALIGN,
            pt.x, pt.y,
            0,
            m_hwnd,
            nullptr
        );
        DestroyMenu(hMenu);
    }
}

void MainWindow::ShowTrayNotification(const wchar_t* title, const wchar_t* message) {
    NOTIFYICONDATAW nid = m_nid;
    nid.uFlags |= NIF_INFO;
    StringCchCopyW(nid.szInfoTitle, ARRAYSIZE(nid.szInfoTitle), title);
    StringCchCopyW(nid.szInfo, ARRAYSIZE(nid.szInfo), message);
    nid.dwInfoFlags = NIIF_INFO;
    Shell_NotifyIconW(NIM_MODIFY, &nid);
}

void MainWindow::RenderUI() {
    PAINTSTRUCT ps;
    BeginPaint(m_hwnd, &ps);

    if (m_pRenderer && SUCCEEDED(m_pRenderer->BeginDraw())) {
        ID2D1HwndRenderTarget* rt = m_pRenderer->GetRenderTarget();
        RECT rc;
        GetClientRect(m_hwnd, &rc);

        FLOAT width = static_cast<FLOAT>(rc.right - rc.left);
        FLOAT height = static_cast<FLOAT>(rc.bottom - rc.top);

        // 1. Sleek Background Gradient
        ID2D1LinearGradientBrush* pGradientBrush = nullptr;
        ID2D1GradientStopCollection* pStops = nullptr;
        D2D1_GRADIENT_STOP stops[2];
        stops[0].color = D2D1::ColorF(0.04f, 0.05f, 0.10f, 1.0f); // #0a0c1a
        stops[0].position = 0.0f;
        stops[1].color = D2D1::ColorF(0.09f, 0.10f, 0.22f, 1.0f); // #171a38
        stops[1].position = 1.0f;

        HRESULT hr = rt->CreateGradientStopCollection(stops, 2, D2D1_GAMMA_2_2, D2D1_EXTEND_MODE_CLAMP, &pStops);
        if (SUCCEEDED(hr)) {
            rt->CreateLinearGradientBrush(
                D2D1::LinearGradientBrushProperties(D2D1::Point2F(0.0f, 0.0f), D2D1::Point2F(width, height)),
                pStops,
                &pGradientBrush
            );
        }

        if (pGradientBrush) {
            rt->FillRectangle(D2D1::RectF(0.0f, 0.0f, width, height), pGradientBrush);
        } else {
            rt->Clear(D2D1::ColorF(0.07f, 0.08f, 0.14f, 1.0f));
        }

        // 2. Draw Top Headers (Branding)
        IDWriteTextFormat* pTextTitle = nullptr;
        IDWriteTextFormat* pTextSub = nullptr;
        IDWriteTextFormat* pLabelFormat = nullptr;
        IDWriteTextFormat* pDescFormat = nullptr;
        IDWriteTextFormat* pHotkeyTextFormat = nullptr;

        m_pRenderer->CreateTextFormat(L"Inter", 30.0f, DWRITE_FONT_WEIGHT_BOLD, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, &pTextTitle);
        m_pRenderer->CreateTextFormat(L"Inter", 12.0f, DWRITE_FONT_WEIGHT_REGULAR, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, &pTextSub);
        m_pRenderer->CreateTextFormat(L"Inter", 15.0f, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, &pLabelFormat);
        m_pRenderer->CreateTextFormat(L"Inter", 11.0f, DWRITE_FONT_WEIGHT_REGULAR, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, &pDescFormat);
        m_pRenderer->CreateTextFormat(L"Inter", 13.0f, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, &pHotkeyTextFormat);

        ID2D1SolidColorBrush* pWhiteBrush = nullptr;
        ID2D1SolidColorBrush* pGrayBrush = nullptr;
        rt->CreateSolidColorBrush(D2D1::ColorF(D2D1::ColorF::White), &pWhiteBrush);
        rt->CreateSolidColorBrush(D2D1::ColorF(0.55f, 0.60f, 0.75f, 1.0f), &pGrayBrush);

        if (pTextTitle && pWhiteBrush) {
            pTextTitle->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
            D2D1_RECT_F titleRect = D2D1::RectF(0.0f, 40.0f, width, 90.0f);
            rt->DrawTextW(L"LookUp Dashboard", 16, pTextTitle, titleRect, pWhiteBrush);
        }
        if (pTextSub && pGrayBrush) {
            pTextSub->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
            D2D1_RECT_F subRect = D2D1::RectF(0.0f, 95.0f, width, 120.0f);
            rt->DrawTextW(L"Win32 + Direct2D Native Configuration", 36, pTextSub, subRect, pGrayBrush);
        }

        // 3. Draw Center Glassmorphic Card
        FLOAT cardL = 60.0f;
        FLOAT cardT = 160.0f;
        FLOAT cardR = width - 60.0f;
        FLOAT cardB = height - 100.0f;

        ID2D1SolidColorBrush* pCardBrush = nullptr;
        ID2D1SolidColorBrush* pBorderBrush = nullptr;
        rt->CreateSolidColorBrush(D2D1::ColorF(0.09f, 0.11f, 0.23f, 0.6f), &pCardBrush);
        rt->CreateSolidColorBrush(D2D1::ColorF(0.25f, 0.30f, 0.55f, 0.3f), &pBorderBrush);

        D2D1_ROUNDED_RECT roundedCard = D2D1::RoundedRect(D2D1::RectF(cardL, cardT, cardR, cardB), 12.0f, 12.0f);
        if (pCardBrush) {
            rt->FillRoundedRectangle(roundedCard, pCardBrush);
        }
        if (pBorderBrush) {
            rt->DrawRoundedRectangle(roundedCard, pBorderBrush, 1.5f);
        }

        // 4. Row 1: Middle-Click Switch Trigger
        FLOAT r1Y = cardT + 30.0f;
        if (pLabelFormat && pWhiteBrush) {
            D2D1_RECT_F lRect = D2D1::RectF(cardL + 40.0f, r1Y, cardL + 400.0f, r1Y + 25.0f);
            rt->DrawTextW(L"Enable Middle-Click OCR Hook", 28, pLabelFormat, lRect, pWhiteBrush);
        }
        if (pDescFormat && pGrayBrush) {
            D2D1_RECT_F dRect = D2D1::RectF(cardL + 40.0f, r1Y + 25.0f, cardL + 450.0f, r1Y + 60.0f);
            rt->DrawTextW(L"Double-click scroll wheel on any word to trigger lookup.", 56, pDescFormat, dRect, pGrayBrush);
        }

        // Draw Switch Switch Box (left = 620, top = 220, right = 680, bottom = 250)
        ID2D1SolidColorBrush* pSwitchBg = nullptr;
        if (m_settings.middleClickTrigger) {
            rt->CreateSolidColorBrush(D2D1::ColorF(0.38f, 0.49f, 0.96f, 1.0f), &pSwitchBg); // Indigo Active
        } else {
            rt->CreateSolidColorBrush(D2D1::ColorF(0.16f, 0.18f, 0.32f, 1.0f), &pSwitchBg); // Slate Gray
        }

        D2D1_ROUNDED_RECT switchRect = D2D1::RoundedRect(D2D1::RectF(620.0f, 220.0f, 680.0f, 250.0f), 15.0f, 15.0f);
        if (pSwitchBg) {
            rt->FillRoundedRectangle(switchRect, pSwitchBg);
            SafeRelease(&pSwitchBg);
        }

        // Draw Toggle Knob (White circle)
        ID2D1SolidColorBrush* pKnobBrush = nullptr;
        rt->CreateSolidColorBrush(D2D1::ColorF(D2D1::ColorF::White), &pKnobBrush);
        if (pKnobBrush) {
            FLOAT knobX = m_settings.middleClickTrigger ? 665.0f : 635.0f;
            rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(knobX, 235.0f), 11.0f, 11.0f), pKnobBrush);
            SafeRelease(&pKnobBrush);
        }

        // 5. Row 2: Global Keyboard Shortcut
        FLOAT r2Y = cardT + 150.0f;
        if (pLabelFormat && pWhiteBrush) {
            D2D1_RECT_F lRect2 = D2D1::RectF(cardL + 40.0f, r2Y, cardL + 400.0f, r2Y + 25.0f);
            rt->DrawTextW(L"Global Capture Hotkey", 22, pLabelFormat, lRect2, pWhiteBrush);
        }
        if (pDescFormat && pGrayBrush) {
            D2D1_RECT_F dRect2 = D2D1::RectF(cardL + 40.0f, r2Y + 25.0f, cardL + 450.0f, r2Y + 60.0f);
            rt->DrawTextW(L"Press global keyboard shortcut to capture custom screen region.", 62, pDescFormat, dRect2, pGrayBrush);
        }

        // Draw Hotkey Container (left = 480, top = 340, right = 680, bottom = 380)
        ID2D1SolidColorBrush* pHotkeyBg = nullptr;
        ID2D1SolidColorBrush* pHotkeyBorder = nullptr;
        
        if (m_isRecording) {
            rt->CreateSolidColorBrush(D2D1::ColorF(0.18f, 0.14f, 0.32f, 1.0f), &pHotkeyBg);
            rt->CreateSolidColorBrush(D2D1::ColorF(0.65f, 0.55f, 0.95f, 1.0f), &pHotkeyBorder);
        } else {
            rt->CreateSolidColorBrush(D2D1::ColorF(0.12f, 0.14f, 0.26f, 1.0f), &pHotkeyBg);
            if (m_hoverHotkey) {
                rt->CreateSolidColorBrush(D2D1::ColorF(0.38f, 0.49f, 0.96f, 1.0f), &pHotkeyBorder);
            } else {
                rt->CreateSolidColorBrush(D2D1::ColorF(0.22f, 0.26f, 0.46f, 1.0f), &pHotkeyBorder);
            }
        }

        D2D1_ROUNDED_RECT hotkeyRect = D2D1::RoundedRect(D2D1::RectF(480.0f, 340.0f, 680.0f, 380.0f), 6.0f, 6.0f);
        if (pHotkeyBg) {
            rt->FillRoundedRectangle(hotkeyRect, pHotkeyBg);
            SafeRelease(&pHotkeyBg);
        }
        if (pHotkeyBorder) {
            rt->DrawRoundedRectangle(hotkeyRect, pHotkeyBorder, 1.5f);
            SafeRelease(&pHotkeyBorder);
        }

        // Draw Hotkey Text
        std::wstring hotkeyText = GetHotkeyString(m_isRecording);
        if (pHotkeyTextFormat) {
            pHotkeyTextFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
            pHotkeyTextFormat->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
            
            ID2D1SolidColorBrush* pHotkeyTextBrush = nullptr;
            if (m_isRecording) {
                rt->CreateSolidColorBrush(D2D1::ColorF(0.80f, 0.75f, 0.95f, 1.0f), &pHotkeyTextBrush);
            } else {
                rt->CreateSolidColorBrush(D2D1::ColorF(D2D1::ColorF::White), &pHotkeyTextBrush);
            }

            if (pHotkeyTextBrush) {
                D2D1_RECT_F textRect = D2D1::RectF(480.0f, 340.0f, 680.0f, 380.0f);
                rt->DrawTextW(hotkeyText.c_str(), static_cast<UINT>(hotkeyText.length()), pHotkeyTextFormat, textRect, pHotkeyTextBrush);
                SafeRelease(&pHotkeyTextBrush);
            }
        }

        // Clean up formatting resources
        SafeRelease(&pWhiteBrush);
        SafeRelease(&pGrayBrush);
        SafeRelease(&pCardBrush);
        SafeRelease(&pBorderBrush);
        
        SafeRelease(&pTextTitle);
        SafeRelease(&pTextSub);
        SafeRelease(&pLabelFormat);
        SafeRelease(&pDescFormat);
        SafeRelease(&pHotkeyTextFormat);

        // Discard buffers
        m_pRenderer->EndDraw();
    }

    EndPaint(m_hwnd, &ps);
}

std::wstring MainWindow::GetHotkeyString(bool useTemp) const {
    const Settings& src = useTemp ? m_tempHotkey : m_settings;
    
    if (useTemp && !src.ctrl && !src.shift && !src.alt && !src.win && src.vkCode == 0) {
        return L"Press key combo...";
    }
    
    std::wstring str;
    if (src.ctrl) str += L"Ctrl + ";
    if (src.shift) str += L"Shift + ";
    if (src.alt) str += L"Alt + ";
    if (src.win) str += L"Win + ";
    
    if (src.vkCode == 0) {
        if (!str.empty() && useTemp) {
            str += L"...";
        } else {
            str = L"None";
        }
    } else if (src.vkCode >= 'A' && src.vkCode <= 'Z') {
        str += static_cast<wchar_t>(src.vkCode);
    } else if (src.vkCode >= '0' && src.vkCode <= '9') {
        str += static_cast<wchar_t>(src.vkCode);
    } else if (src.vkCode >= VK_F1 && src.vkCode <= VK_F12) {
        str += L"F" + std::to_wstring(src.vkCode - VK_F1 + 1);
    } else {
        switch (src.vkCode) {
            case VK_SPACE: str += L"Space"; break;
            case VK_RETURN: str += L"Enter"; break;
            case VK_TAB: str += L"Tab"; break;
            case VK_OEM_3: str += L"`"; break;
            default: str += L"Key " + std::to_wstring(src.vkCode); break;
        }
    }
    return str;
}

void MainWindow::TriggerAsyncLookup() {
    std::wstring word = L"Lookup"; // default fallback

    // Try to get highlighting/copied word from clipboard
    if (OpenClipboard(m_hwnd)) {
        HANDLE hData = GetClipboardData(CF_UNICODETEXT);
        if (hData) {
            wchar_t* pText = static_cast<wchar_t*>(GlobalLock(hData));
            if (pText) {
                word = pText;
                
                // Trim leading/trailing spaces
                while (!word.empty() && iswspace(word.front())) word.erase(0, 1);
                while (!word.empty() && iswspace(word.back())) word.pop_back();

                // Select first word if multiple
                size_t firstSpace = word.find(L' ');
                if (firstSpace != std::wstring::npos) {
                    word = word.substr(0, firstSpace);
                }

                // Trim punctuation symbols
                while (!word.empty() && iswpunct(word.front())) word.erase(0, 1);
                while (!word.empty() && iswpunct(word.back())) word.pop_back();

                GlobalUnlock(hData);
            }
        }
        CloseClipboard();
    }

    if (word.empty()) word = L"Lookup";

    // Run query asynchronously on a worker thread
    std::thread([this, word]() {
        DictionaryEntry entry;
        if (DictionaryClient::Lookup(word, entry)) {
            DictionaryEntry* pEntry = new DictionaryEntry(entry);
            PostMessageW(m_hwnd, WM_SHOW_POPUP, reinterpret_cast<WPARAM>(pEntry), 0);
        } else {
            std::wstring failMsg = L"Definition not found in database for the word \"" + word + L"\".";
            DictionaryEntry* pEntry = new DictionaryEntry{ word, L"", failMsg, L"" };
            PostMessageW(m_hwnd, WM_SHOW_POPUP, reinterpret_cast<WPARAM>(pEntry), 0);
        }
    }).detach();
}

LRESULT CALLBACK MainWindow::MouseHookProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode >= 0) {
        if (wParam == WM_MBUTTONDOWN) {
            DWORD currentTime = GetTickCount();
            // Detect manual double-click interval
            if (currentTime - s_lastClickTime <= GetDoubleClickTime()) {
                PostMessageW(s_hwndMain, WM_TRIGGER_LOOKUP, 0, 0);
            }
            s_lastClickTime = currentTime;
        }
    }
    return CallNextHookEx(nullptr, nCode, wParam, lParam);
}

DWORD WINAPI MainWindow::MouseHookThreadProc(LPVOID lpParam) {
    HWND hwndMain = reinterpret_cast<HWND>(lpParam);
    s_hwndMain = hwndMain;

    s_hMouseHook = SetWindowsHookExW(WH_MOUSE_LL, MouseHookProc, GetModuleHandle(nullptr), 0);
    if (!s_hMouseHook) {
        return 0;
    }

    // Worker thread message pump keeping hook alive
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    UnhookWindowsHookEx(s_hMouseHook);
    s_hMouseHook = nullptr;
    return 0;
}
