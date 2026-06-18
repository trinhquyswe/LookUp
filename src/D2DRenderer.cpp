#include "D2DRenderer.h"

D2DRenderer::D2DRenderer() :
    m_hwnd(nullptr),
    m_pD2DFactory(nullptr),
    m_pDWriteFactory(nullptr),
    m_pRenderTarget(nullptr) {
}

D2DRenderer::~D2DRenderer() {
    Release();
}

HRESULT D2DRenderer::Initialize(HWND hwnd) {
    m_hwnd = hwnd;
    return CreateDeviceIndependentResources();
}

void D2DRenderer::Release() {
    ReleaseDeviceResources();
    SafeRelease(&m_pD2DFactory);
    SafeRelease(&m_pDWriteFactory);
}

HRESULT D2DRenderer::CreateDeviceIndependentResources() {
    HRESULT hr = S_OK;

    // Create D2D Factory if not already created
    if (!m_pD2DFactory) {
        hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &m_pD2DFactory);
    }

    // Create DirectWrite Factory if not already created
    if (SUCCEEDED(hr) && !m_pDWriteFactory) {
        hr = DWriteCreateFactory(
            DWRITE_FACTORY_TYPE_SHARED,
            __uuidof(IDWriteFactory),
            reinterpret_cast<IUnknown**>(&m_pDWriteFactory)
        );
    }

    return hr;
}

HRESULT D2DRenderer::CreateDeviceResources() {
    HRESULT hr = S_OK;

    if (!m_pRenderTarget) {
        RECT rc;
        GetClientRect(m_hwnd, &rc);

        D2D1_SIZE_U size = D2D1::SizeU(
            rc.right - rc.left,
            rc.bottom - rc.top
        );

        // Create HWND Render Target
        hr = m_pD2DFactory->CreateHwndRenderTarget(
            D2D1::RenderTargetProperties(),
            D2D1::HwndRenderTargetProperties(m_hwnd, size),
            &m_pRenderTarget
        );
    }

    return hr;
}

void D2DRenderer::ReleaseDeviceResources() {
    SafeRelease(&m_pRenderTarget);
}

HRESULT D2DRenderer::BeginDraw() {
    HRESULT hr = CreateDeviceResources();
    if (SUCCEEDED(hr)) {
        m_pRenderTarget->BeginDraw();
    }
    return hr;
}

HRESULT D2DRenderer::EndDraw() {
    HRESULT hr = m_pRenderTarget->EndDraw();
    
    // If the device was lost, discard device-dependent resources so they are recreated on the next frame
    if (hr == D2DERR_RECREATE_TARGET) {
        hr = S_OK;
        ReleaseDeviceResources();
    }
    
    return hr;
}

void D2DRenderer::Resize(UINT width, UINT height) {
    if (m_pRenderTarget) {
        m_pRenderTarget->Resize(D2D1::SizeU(width, height));
    }
}

HRESULT D2DRenderer::CreateTextFormat(
    const wchar_t* fontFamilyName,
    FLOAT fontSize,
    DWRITE_FONT_WEIGHT fontWeight,
    DWRITE_FONT_STYLE fontStyle,
    DWRITE_FONT_STRETCH fontStretch,
    IDWriteTextFormat** ppTextFormat
) {
    if (!m_pDWriteFactory) {
        return E_UNEXPECTED;
    }

    return m_pDWriteFactory->CreateTextFormat(
        fontFamilyName,
        nullptr, // Use system font collection (which includes private loaded fonts)
        fontWeight,
        fontStyle,
        fontStretch,
        fontSize,
        L"", // Locale name (empty defaults to user locale)
        ppTextFormat
    );
}

bool D2DRenderer::LoadPrivateFont(const wchar_t* fontPath) {
    // Check if file exists first
    DWORD fileAttr = GetFileAttributesW(fontPath);
    if (fileAttr == INVALID_FILE_ATTRIBUTES || (fileAttr & FILE_ATTRIBUTE_DIRECTORY)) {
        return false;
    }

    int count = AddFontResourceExW(fontPath, FR_PRIVATE, nullptr);
    return count > 0;
}

void D2DRenderer::UnloadPrivateFont(const wchar_t* fontPath) {
    RemoveFontResourceExW(fontPath, FR_PRIVATE, nullptr);
}
