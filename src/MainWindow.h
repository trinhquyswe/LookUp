#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <windowsx.h>
#include <shellapi.h>
#include <strsafe.h>
#include <string>
#include "D2DRenderer.h"
#include "PopupWindow.h"

// Custom window messages
#define WM_TRAYICON (WM_USER + 1)

// Menu and Tray IDs
#define TRAY_ICON_ID 1
#define ID_TRAY_SHOW 1001
#define ID_TRAY_EXIT 1002

struct Settings {
    bool middleClickTrigger = true;
    bool ctrl = true;
    bool shift = true;
    bool alt = false;
    bool win = false;
    UINT vkCode = 'D'; // Default: Ctrl + Shift + D
};

class MainWindow {
public:
    MainWindow();
    ~MainWindow();

    // Create the window and show it
    bool Create(HINSTANCE hInstance, int nCmdShow);
    
    // Run the standard message loop until WM_QUIT
    int RunMessageLoop();

private:
    // Static router to member handler
    static LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
    LRESULT HandleMessage(UINT uMsg, WPARAM wParam, LPARAM lParam);

    // Tray management
    void SetupTrayIcon();
    void RemoveTrayIcon();
    void ShowContextMenu();
    void ShowTrayNotification(const wchar_t* title, const wchar_t* message);

    // Settings storage & Hotkey management
    void LoadSettingsData();
    void SaveSettingsData();
    void UpdateRegisteredHotkey();

    // Custom UI Rendering
    void RenderUI();
    std::wstring GetHotkeyString(bool useTemp = false) const;
    void TriggerAsyncLookup();
    std::wstring CaptureAndOcrWord();

private:
    HWND m_hwnd;
    NOTIFYICONDATAW m_nid;
    bool m_isVisible;
    D2DRenderer* m_pRenderer;
    std::wstring m_settingsPath;

    // Settings and recording state
    Settings m_settings;
    bool m_isRecording;
    Settings m_tempHotkey; // Modifiers captured while recording

    // Interactive button hover states
    bool m_hoverToggle;
    bool m_hoverHotkey;

    // Layered popup window helper
    PopupWindow m_popupWindow;

    // Low-level mouse hook thread management
    HANDLE m_hHookThread;
    DWORD m_hookThreadId;

    static HWND s_hwndMain;
    static HHOOK s_hMouseHook;
    static DWORD s_lastClickTime;

    static LRESULT CALLBACK MouseHookProc(int nCode, WPARAM wParam, LPARAM lParam);
    static DWORD WINAPI MouseHookThreadProc(LPVOID lpParam);
};
