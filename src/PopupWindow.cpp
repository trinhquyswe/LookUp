#include "PopupWindow.h"
#include <cmath>
#include <windowsx.h>

// C++/WinRT audio projection headers
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Media.Playback.h>
#include <winrt/Windows.Media.Core.h>

PopupWindow::PopupWindow() :
    m_hwnd(nullptr),
    m_hwndParent(nullptr),
    m_pRenderer(nullptr),
    m_animationTime(0.0),
    m_countdownTime(0.0),
    m_totalDuration(5.0),
    m_mouseOver(false),
    m_isShowing(false),
    m_lastTick({}),
    m_frequency({}),
    m_slideOffset(30.0f),
    m_opacity(0.0f),
    m_hoverListen(false),
    m_hoverCopy(false),
    m_hoverClose(false) {
    QueryPerformanceFrequency(&m_frequency);
}

PopupWindow::~PopupWindow() {
    if (m_hwnd) {
        KillTimer(m_hwnd, TIMER_POPUP_ANIM);
    }
    if (m_pRenderer) {
        delete m_pRenderer;
        m_pRenderer = nullptr;
    }
}

bool PopupWindow::Create(HINSTANCE hInstance, HWND hwndParent) {
    m_hwndParent = hwndParent;

    // Register class for popup
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(WNDCLASSEXW);
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = L"LookUpPopupWindowClass";
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr; // Transparent

    if (!RegisterClassExW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        return false;
    }

    // Borderless overlay, topmost, does not activate, no taskbar presence
    int popupW = 420;
    int popupH = 240;

    m_hwnd = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
        L"LookUpPopupWindowClass",
        L"LookUpPopup",
        WS_POPUP,
        0, 0, popupW, popupH,
        hwndParent,
        nullptr,
        hInstance,
        this
    );

    if (!m_hwnd) return false;

    // Extend DWM frame into client area to achieve per-pixel alpha transparency support
    MARGINS margins = { -1, -1, -1, -1 };
    DwmExtendFrameIntoClientArea(m_hwnd, &margins);

    return true;
}

void PopupWindow::Show(const std::wstring& word, const std::wstring& phonetic, const std::wstring& definition, const std::wstring& audioUrl) {
    m_word = word;
    m_phonetic = phonetic;
    m_definition = definition;
    m_audioUrl = audioUrl;

    // Reset animation state
    m_animationTime = 0.0;
    m_totalDuration = 5.0; // Show for 5 seconds
    m_countdownTime = m_totalDuration;
    m_mouseOver = false;
    m_isShowing = true;
    m_slideOffset = 30.0f;
    m_opacity = 0.0f;

    m_hoverListen = false;
    m_hoverCopy = false;
    m_hoverClose = false;

    // Calculate dimensions
    int popupW = 420;
    int popupH = 240;

    // Align popup relative to mouse cursor
    POINT pt;
    GetCursorPos(&pt);

    int screenW = GetSystemMetrics(SM_CXSCREEN);
    int screenH = GetSystemMetrics(SM_CYSCREEN);

    int x = pt.x - (popupW / 2);
    int y = pt.y - popupH - 15; // 15px above cursor

    // Restrain within visible screen boundary
    if (x < 10) x = 10;
    if (x + popupW > screenW - 10) x = screenW - popupW - 10;
    if (y < 10) {
        y = pt.y + 20; // Flip below cursor if there is no top space
    }
    if (y + popupH > screenH - 10) y = screenH - popupH - 10;

    // Reposition and show without stealing active window focus
    SetWindowPos(
        m_hwnd, 
        HWND_TOPMOST, 
        x, y, 
        popupW, popupH, 
        SWP_SHOWWINDOW | SWP_NOACTIVATE
    );

    // Run high-precision timer loop at 60 FPS (~16ms)
    QueryPerformanceCounter(&m_lastTick);
    SetTimer(m_hwnd, TIMER_POPUP_ANIM, 16, nullptr);

    InvalidateRect(m_hwnd, nullptr, FALSE);
}

void PopupWindow::Hide() {
    if (m_isShowing) {
        KillTimer(m_hwnd, TIMER_POPUP_ANIM);
        ShowWindow(m_hwnd, SW_HIDE);
        m_isShowing = false;
    }
}

LRESULT CALLBACK PopupWindow::WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    if (uMsg == WM_NCCREATE) {
        CREATESTRUCTW* pCreate = reinterpret_cast<CREATESTRUCTW*>(lParam);
        PopupWindow* pThis = reinterpret_cast<PopupWindow*>(pCreate->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(pThis));
        pThis->m_hwnd = hwnd;
    }

    PopupWindow* pThis = reinterpret_cast<PopupWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (pThis) {
        return pThis->HandleMessage(uMsg, wParam, lParam);
    }
    return DefWindowProcW(hwnd, uMsg, wParam, lParam);
}

LRESULT PopupWindow::HandleMessage(UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
        case WM_CREATE: {
            m_pRenderer = new D2DRenderer();
            if (FAILED(m_pRenderer->Initialize(m_hwnd))) {
                return -1;
            }
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
        case WM_TIMER: {
            if (wParam == TIMER_POPUP_ANIM) {
                UpdateFrame();
            }
            return 0;
        }
        case WM_MOUSEMOVE: {
            int x = GET_X_LPARAM(lParam);
            int y = GET_Y_LPARAM(lParam);

            // Check if mouse is hovering client area to pause/resume countdown
            if (!m_mouseOver) {
                m_mouseOver = true;
                TRACKMOUSEEVENT tme = { sizeof(TRACKMOUSEEVENT) };
                tme.dwFlags = TME_LEAVE;
                tme.hwndTrack = m_hwnd;
                TrackMouseEvent(&tme);
            }

            // Hit test buttons (Adjust coordinates relative to current slide animation)
            // Bounding box of buttons once animation settles:
            // Close: x [370, 390], y [25, 45]
            // Listen: x [30, 100], y [180, 205]
            // Copy: x [110, 170], y [180, 205]
            FLOAT offset = m_slideOffset;
            bool overClose = (x >= 370 && x <= 390 && y >= (25 + offset) && y <= (45 + offset));
            bool overListen = (x >= 30 && x <= 100 && y >= (180 + offset) && y <= (205 + offset));
            bool overCopy = (x >= 110 && x <= 170 && y >= (180 + offset) && y <= (205 + offset));

            if (overClose != m_hoverClose || overListen != m_hoverListen || overCopy != m_hoverCopy) {
                m_hoverClose = overClose;
                m_hoverListen = overListen;
                m_hoverCopy = overCopy;
                InvalidateRect(m_hwnd, nullptr, FALSE);
            }
            return 0;
        }
        case WM_MOUSELEAVE: {
            m_mouseOver = false;
            m_hoverClose = false;
            m_hoverListen = false;
            m_hoverCopy = false;
            InvalidateRect(m_hwnd, nullptr, FALSE);
            return 0;
        }
        case WM_SETCURSOR: {
            if (LOWORD(lParam) == HTCLIENT) {
                if (m_hoverClose || m_hoverListen || m_hoverCopy) {
                    SetCursor(LoadCursor(nullptr, IDC_HAND));
                    return TRUE;
                }
            }
            break;
        }
        case WM_LBUTTONDOWN: {
            if (m_hoverClose) {
                Hide();
            }
            else if (m_hoverCopy) {
                // Copy dictionary entry info to clipboard
                if (OpenClipboard(m_hwnd)) {
                    EmptyClipboard();
                    std::wstring copyText = m_word + L" [" + m_phonetic + L"]: " + m_definition;
                    HGLOBAL hg = GlobalAlloc(GMEM_MOVEABLE, (copyText.length() + 1) * sizeof(wchar_t));
                    if (hg) {
                        wchar_t* p = static_cast<wchar_t*>(GlobalLock(hg));
                        if (p) {
                            wcscpy_s(p, copyText.length() + 1, copyText.c_str());
                            GlobalUnlock(hg);
                            SetClipboardData(CF_UNICODETEXT, hg);
                        }
                    }
                    CloseClipboard();

                    // Short notification confirm
                    m_definition = L"Copied to clipboard! " + m_definition.substr(0, min(m_definition.length(), 20)) + L"...";
                    InvalidateRect(m_hwnd, nullptr, FALSE);
                }
            }
            else if (m_hoverListen) {
                if (!m_audioUrl.empty()) {
                    static winrt::Windows::Media::Playback::MediaPlayer mediaPlayer = nullptr;
                    try {
                        winrt::Windows::Foundation::Uri uri(m_audioUrl);
                        winrt::Windows::Media::Core::MediaSource source = 
                            winrt::Windows::Media::Core::MediaSource::CreateFromUri(uri);
                        
                        if (!mediaPlayer) {
                            mediaPlayer = winrt::Windows::Media::Playback::MediaPlayer();
                        }
                        mediaPlayer.Source(source);
                        mediaPlayer.Play();
                    } catch (...) {
                        // Playback error
                    }
                }
                InvalidateRect(m_hwnd, nullptr, FALSE);
            }
            return 0;
        }
        case WM_DESTROY: {
            return 0;
        }
    }
    return DefWindowProcW(m_hwnd, uMsg, wParam, lParam);
}

void PopupWindow::UpdateFrame() {
    LARGE_INTEGER currentTick;
    QueryPerformanceCounter(&currentTick);
    double dt = static_cast<double>(currentTick.QuadPart - m_lastTick.QuadPart) / m_frequency.QuadPart;
    m_lastTick = currentTick;

    // Clamp delta time to avoid large skips (e.g. if window is paused/moved)
    if (dt > 0.1) dt = 0.1;

    // 1. Entry slide & fade animations (settles over 0.25 seconds)
    if (m_animationTime < 0.25) {
        m_animationTime += dt;
        if (m_animationTime > 0.25) m_animationTime = 0.25;

        // Easing function: Cubic Out
        double t = m_animationTime / 0.25;
        double ease = 1.0 - pow(1.0 - t, 3);

        m_slideOffset = static_cast<FLOAT>(25.0 * (1.0 - ease));
        m_opacity = static_cast<FLOAT>(ease);
    }

    // 2. Decrement display countdown progress if mouse isn't hovering
    if (!m_mouseOver) {
        m_countdownTime -= dt;
        if (m_countdownTime <= 0.0) {
            m_countdownTime = 0.0;
            Hide(); // Closes the window automatically
            return;
        }
    }

    InvalidateRect(m_hwnd, nullptr, FALSE);
}

void PopupWindow::RenderUI() {
    if (!m_pRenderer) return;

    if (SUCCEEDED(m_pRenderer->BeginDraw())) {
        ID2D1HwndRenderTarget* rt = m_pRenderer->GetRenderTarget();
        
        // Fully clear with transparent brush so DWM controls alpha blend
        rt->Clear(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.0f));

        // Bounding Box calculations
        FLOAT cardL = 10.0f;
        FLOAT cardT = 10.0f + m_slideOffset;
        FLOAT cardR = 410.0f;
        FLOAT cardB = 230.0f + m_slideOffset;

        // Create Glassmorphic Card fill brush
        ID2D1SolidColorBrush* pCardBrush = nullptr;
        ID2D1SolidColorBrush* pBorderBrush = nullptr;

        // Apply global animation opacity to color alpha values
        rt->CreateSolidColorBrush(D2D1::ColorF(0.07f, 0.08f, 0.14f, 0.88f * m_opacity), &pCardBrush);
        rt->CreateSolidColorBrush(D2D1::ColorF(0.38f, 0.49f, 0.96f, 0.35f * m_opacity), &pBorderBrush);

        D2D1_ROUNDED_RECT cardRect = D2D1::RoundedRect(D2D1::RectF(cardL, cardT, cardR, cardB), 12.0f, 12.0f);
        
        if (pCardBrush) {
            rt->FillRoundedRectangle(cardRect, pCardBrush);
        }
        if (pBorderBrush) {
            rt->DrawRoundedRectangle(cardRect, pBorderBrush, 1.5f);
        }

        // Draw Typography
        IDWriteTextFormat* pFormatWord = nullptr;
        IDWriteTextFormat* pFormatPhonetic = nullptr;
        IDWriteTextFormat* pFormatDesc = nullptr;
        IDWriteTextFormat* pBtnFormat = nullptr;

        m_pRenderer->CreateTextFormat(L"Inter", 20.0f, DWRITE_FONT_WEIGHT_BOLD, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, &pFormatWord);
        m_pRenderer->CreateTextFormat(L"Inter", 11.0f, DWRITE_FONT_WEIGHT_REGULAR, DWRITE_FONT_STYLE_ITALIC, DWRITE_FONT_STRETCH_NORMAL, &pFormatPhonetic);
        m_pRenderer->CreateTextFormat(L"Inter", 12.0f, DWRITE_FONT_WEIGHT_REGULAR, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, &pFormatDesc);
        m_pRenderer->CreateTextFormat(L"Inter", 11.0f, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, &pBtnFormat);

        // Brushes
        ID2D1SolidColorBrush* pWhiteBrush = nullptr;
        ID2D1SolidColorBrush* pGrayBrush = nullptr;
        rt->CreateSolidColorBrush(D2D1::ColorF(D2D1::ColorF::White, m_opacity), &pWhiteBrush);
        rt->CreateSolidColorBrush(D2D1::ColorF(0.65f, 0.70f, 0.85f, m_opacity), &pGrayBrush);

        // 1. Draw Word
        if (pFormatWord && pWhiteBrush) {
            D2D1_RECT_F rectWord = D2D1::RectF(30.0f, cardT + 20.0f, 350.0f, cardT + 48.0f);
            rt->DrawTextW(m_word.c_str(), static_cast<UINT>(m_word.length()), pFormatWord, rectWord, pWhiteBrush);
        }

        // 2. Draw Phonetics
        if (pFormatPhonetic && pGrayBrush) {
            D2D1_RECT_F rectPhonetic = D2D1::RectF(30.0f, cardT + 48.0f, 350.0f, cardT + 65.0f);
            rt->DrawTextW(m_phonetic.c_str(), static_cast<UINT>(m_phonetic.length()), pFormatPhonetic, rectPhonetic, pGrayBrush);
        }

        // 3. Draw Definition Text with Wrapping
        if (pFormatDesc && pWhiteBrush) {
            pFormatDesc->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);
            D2D1_RECT_F rectDesc = D2D1::RectF(30.0f, cardT + 78.0f, 390.0f, cardT + 160.0f);
            rt->DrawTextW(m_definition.c_str(), static_cast<UINT>(m_definition.length()), pFormatDesc, rectDesc, pWhiteBrush);
        }

        // 4. Draw Interactive Buttons
        // Close Button (top-right of card)
        ID2D1SolidColorBrush* pCloseBrush = nullptr;
        if (m_hoverClose) {
            rt->CreateSolidColorBrush(D2D1::ColorF(0.95f, 0.35f, 0.35f, m_opacity), &pCloseBrush); // Highlight light red
        } else {
            rt->CreateSolidColorBrush(D2D1::ColorF(0.55f, 0.60f, 0.75f, m_opacity), &pCloseBrush);
        }

        if (pBtnFormat && pCloseBrush) {
            pBtnFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
            pBtnFormat->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
            D2D1_RECT_F closeRect = D2D1::RectF(370.0f, cardT + 20.0f, 390.0f, cardT + 40.0f);
            rt->DrawTextW(L"X", 1, pBtnFormat, closeRect, pCloseBrush);
            SafeRelease(&pCloseBrush);
        }

        // Listen & Copy Buttons (bottom)
        ID2D1SolidColorBrush* pBtnBg = nullptr;
        ID2D1SolidColorBrush* pBtnBorder = nullptr;

        // Draw Listen Button
        if (m_hoverListen) {
            rt->CreateSolidColorBrush(D2D1::ColorF(0.38f, 0.49f, 0.96f, 0.20f * m_opacity), &pBtnBg);
            rt->CreateSolidColorBrush(D2D1::ColorF(0.38f, 0.49f, 0.96f, m_opacity), &pBtnBorder);
        } else {
            rt->CreateSolidColorBrush(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.0f), &pBtnBg);
            rt->CreateSolidColorBrush(D2D1::ColorF(0.25f, 0.30f, 0.55f, 0.40f * m_opacity), &pBtnBorder);
        }

        D2D1_ROUNDED_RECT btnListenRect = D2D1::RoundedRect(D2D1::RectF(30.0f, cardT + 170.0f, 95.0f, cardT + 195.0f), 4.0f, 4.0f);
        if (pBtnBg) rt->FillRoundedRectangle(btnListenRect, pBtnBg);
        if (pBtnBorder) rt->DrawRoundedRectangle(btnListenRect, pBtnBorder, 1.0f);
        SafeRelease(&pBtnBg);
        SafeRelease(&pBtnBorder);

        if (pBtnFormat && pWhiteBrush) {
            pBtnFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
            pBtnFormat->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
            D2D1_RECT_F textListenRect = D2D1::RectF(30.0f, cardT + 170.0f, 95.0f, cardT + 195.0f);
            rt->DrawTextW(L"Listen", 6, pBtnFormat, textListenRect, pWhiteBrush);
        }

        // Draw Copy Button
        if (m_hoverCopy) {
            rt->CreateSolidColorBrush(D2D1::ColorF(0.38f, 0.49f, 0.96f, 0.20f * m_opacity), &pBtnBg);
            rt->CreateSolidColorBrush(D2D1::ColorF(0.38f, 0.49f, 0.96f, m_opacity), &pBtnBorder);
        } else {
            rt->CreateSolidColorBrush(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.0f), &pBtnBg);
            rt->CreateSolidColorBrush(D2D1::ColorF(0.25f, 0.30f, 0.55f, 0.40f * m_opacity), &pBtnBorder);
        }

        D2D1_ROUNDED_RECT btnCopyRect = D2D1::RoundedRect(D2D1::RectF(105.0f, cardT + 170.0f, 165.0f, cardT + 195.0f), 4.0f, 4.0f);
        if (pBtnBg) rt->FillRoundedRectangle(btnCopyRect, pBtnBg);
        if (pBtnBorder) rt->DrawRoundedRectangle(btnCopyRect, pBtnBorder, 1.0f);
        SafeRelease(&pBtnBg);
        SafeRelease(&pBtnBorder);

        if (pBtnFormat && pWhiteBrush) {
            pBtnFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
            pBtnFormat->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
            D2D1_RECT_F textCopyRect = D2D1::RectF(105.0f, cardT + 170.0f, 165.0f, cardT + 195.0f);
            rt->DrawTextW(L"Copy", 4, pBtnFormat, textCopyRect, pWhiteBrush);
        }

        // 5. Draw Progress Bar Countdown at the bottom of the card
        FLOAT barL = 30.0f;
        FLOAT barT = cardB - 20.0f;
        FLOAT barR = 390.0f;
        FLOAT barB = cardB - 17.0f;

        ID2D1SolidColorBrush* pBarBg = nullptr;
        ID2D1SolidColorBrush* pBarActive = nullptr;

        rt->CreateSolidColorBrush(D2D1::ColorF(0.16f, 0.18f, 0.32f, 0.40f * m_opacity), &pBarBg);
        rt->CreateSolidColorBrush(D2D1::ColorF(0.38f, 0.49f, 0.96f, 0.90f * m_opacity), &pBarActive);

        D2D1_ROUNDED_RECT progressBg = D2D1::RoundedRect(D2D1::RectF(barL, barT, barR, barB), 1.5f, 1.5f);
        if (pBarBg) {
            rt->FillRoundedRectangle(progressBg, pBarBg);
        }

        // Active segment calculation
        double percentage = m_countdownTime / m_totalDuration;
        if (percentage > 1.0) percentage = 1.0;
        if (percentage < 0.0) percentage = 0.0;

        FLOAT activeR = barL + static_cast<FLOAT>((barR - barL) * percentage);
        if (activeR > barL + 1.0f) {
            D2D1_ROUNDED_RECT progressActive = D2D1::RoundedRect(D2D1::RectF(barL, barT, activeR, barB), 1.5f, 1.5f);
            if (pBarActive) {
                rt->FillRoundedRectangle(progressActive, pBarActive);
            }
        }

        // Clean brushes
        SafeRelease(&pWhiteBrush);
        SafeRelease(&pGrayBrush);
        SafeRelease(&pBarBg);
        SafeRelease(&pBarActive);

        // Clean layouts
        SafeRelease(&pFormatWord);
        SafeRelease(&pFormatPhonetic);
        SafeRelease(&pFormatDesc);
        SafeRelease(&pBtnFormat);

        SafeRelease(&pCardBrush);
        SafeRelease(&pBorderBrush);

        m_pRenderer->EndDraw();
    }
}
