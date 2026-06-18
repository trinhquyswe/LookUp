#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <dwmapi.h>
#include <string>
#include "D2DRenderer.h"

// Timer ID for animation updates
#define TIMER_POPUP_ANIM 2001

class PopupWindow {
public:
    PopupWindow();
    ~PopupWindow();

    // Create the borderless, transparent window
    bool Create(HINSTANCE hInstance, HWND hwndParent);
    
    // Shows the pop-up dictionary card at cursor coordinates
    void Show(const std::wstring& word, const std::wstring& phonetic, const std::wstring& definition);
    
    // Hides the popup immediately
    void Hide();

    // Sizing/layout getters
    HWND GetHwnd() const { return m_hwnd; }
    bool IsShowing() const { return m_isShowing; }

private:
    static LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
    LRESULT HandleMessage(UINT uMsg, WPARAM wParam, LPARAM lParam);

    // Repaint event
    void RenderUI();
    
    // Frame updates for slide/fade and countdown progress
    void UpdateFrame();

private:
    HWND m_hwnd;
    HWND m_hwndParent;
    D2DRenderer* m_pRenderer;

    // Display state
    std::wstring m_word;
    std::wstring m_phonetic;
    std::wstring m_definition;

    // Timer & Animation parameters
    double m_animationTime;       // Elapsed animation time (sec)
    double m_countdownTime;       // Time remaining on display timer (sec)
    double m_totalDuration;       // Initial countdown duration (sec)
    
    bool m_mouseOver;             // Tracks mouse hover to pause timer
    bool m_isShowing;             // Visibility state
    
    LARGE_INTEGER m_lastTick;
    LARGE_INTEGER m_frequency;

    // Render properties derived from animation frames
    FLOAT m_slideOffset;          // Easing offset
    FLOAT m_opacity;              // Fade alpha value

    // UI interactive button hover regions
    bool m_hoverListen;
    bool m_hoverCopy;
    bool m_hoverClose;
};
