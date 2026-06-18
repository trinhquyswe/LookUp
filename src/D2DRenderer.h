#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d2d1.h>
#include <dwrite.h>

// Safe Release helper template
template <class T>
inline void SafeRelease(T** ppT) {
    if (*ppT) {
        (*ppT)->Release();
        *ppT = nullptr;
    }
}

class D2DRenderer {
public:
    D2DRenderer();
    ~D2DRenderer();

    // Initialize Direct2D/Write factories and bind the render target to the window
    HRESULT Initialize(HWND hwnd);
    
    // Release all allocated resources
    void Release();

    // Prepare for rendering commands
    HRESULT BeginDraw();
    
    // End rendering commands and present to screen
    HRESULT EndDraw();

    // Resize the Direct2D render target when window size changes
    void Resize(UINT width, UINT height);

    // Factory/Target Accessors
    ID2D1Factory* GetD2DFactory() const { return m_pD2DFactory; }
    IDWriteFactory* GetDWriteFactory() const { return m_pDWriteFactory; }
    ID2D1HwndRenderTarget* GetRenderTarget() const { return m_pRenderTarget; }

    // Helper to quickly create custom typography formats
    HRESULT CreateTextFormat(
        const wchar_t* fontFamilyName,
        FLOAT fontSize,
        DWRITE_FONT_WEIGHT fontWeight,
        DWRITE_FONT_STYLE fontStyle,
        DWRITE_FONT_STRETCH fontStretch,
        IDWriteTextFormat** ppTextFormat
    );

    // Installs a private font file for use by the application process
    static bool LoadPrivateFont(const wchar_t* fontPath);
    
    // Uninstalls a private font file
    static void UnloadPrivateFont(const wchar_t* fontPath);

private:
    // Create resources not tied to a specific graphics adapter
    HRESULT CreateDeviceIndependentResources();
    
    // Create resources tied to the graphics adapter
    HRESULT CreateDeviceResources();
    
    // Discard device-specific resources on error/target recreation
    void ReleaseDeviceResources();

private:
    HWND m_hwnd;
    ID2D1Factory* m_pD2DFactory;
    IDWriteFactory* m_pDWriteFactory;
    ID2D1HwndRenderTarget* m_pRenderTarget;
};
