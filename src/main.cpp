#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <objbase.h>
#include "MainWindow.h"

int WINAPI wWinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE hPrevInstance, _In_ PWSTR lpCmdLine, _In_ int nCmdShow) {
    UNREFERENCED_PARAMETER(hPrevInstance);
    UNREFERENCED_PARAMETER(lpCmdLine);

    // Initialize COM library (needed for Direct2D, DirectWrite, and C++/WinRT audio later)
    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    if (FAILED(hr)) {
        MessageBoxW(nullptr, L"Failed to initialize COM library.", L"LookUp Error", MB_OK | MB_ICONERROR);
        return 1;
    }

    MainWindow app;
    if (!app.Create(hInstance, nCmdShow)) {
        MessageBoxW(nullptr, L"Failed to create main application window.", L"LookUp Error", MB_OK | MB_ICONERROR);
        CoUninitialize();
        return 1;
    }

    int result = app.RunMessageLoop();

    CoUninitialize();
    return result;
}
