#include "SferaD3D11Renderer.h"

template <typename T>
static void SferaSafeRelease(T*& Object)
{
    if (Object)
    {
        Object->Release();
        Object = nullptr;
    }
}

bool SferaD3D11Renderer::Initialize(HWND InWindowHandle)
{
    WindowHandle = InWindowHandle;
    if (!WindowHandle)
    {
        return false;
    }

    RECT ClientRect = {};
    GetClientRect(WindowHandle, &ClientRect);
    const UINT Width = static_cast<UINT>((std::max)(1L, ClientRect.right - ClientRect.left));
    const UINT Height = static_cast<UINT>((std::max)(1L, ClientRect.bottom - ClientRect.top));

    DXGI_SWAP_CHAIN_DESC Desc = {};
    Desc.BufferDesc.Width = Width;
    Desc.BufferDesc.Height = Height;
    Desc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    Desc.BufferDesc.RefreshRate.Numerator = 60;
    Desc.BufferDesc.RefreshRate.Denominator = 1;
    Desc.SampleDesc.Count = 1;
    Desc.SampleDesc.Quality = 0;
    Desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    Desc.BufferCount = 2;
    Desc.OutputWindow = WindowHandle;
    Desc.Windowed = TRUE;
    Desc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
    Desc.Flags = 0;

    UINT Flags = 0;
#if defined(_DEBUG)
    Flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

    D3D_FEATURE_LEVEL RequestedLevels[] =
    {
        D3D_FEATURE_LEVEL_11_1,
        D3D_FEATURE_LEVEL_11_0,
        D3D_FEATURE_LEVEL_10_1,
        D3D_FEATURE_LEVEL_10_0
    };
    D3D_FEATURE_LEVEL CreatedLevel = D3D_FEATURE_LEVEL_11_0;

    HRESULT Hr = D3D11CreateDeviceAndSwapChain(
        nullptr,
        D3D_DRIVER_TYPE_HARDWARE,
        nullptr,
        Flags,
        RequestedLevels,
        ARRAYSIZE(RequestedLevels),
        D3D11_SDK_VERSION,
        &Desc,
        &SwapChain,
        &Device,
        &CreatedLevel,
        &DeviceContext);

#if defined(_DEBUG)
    if (FAILED(Hr))
    {
        Flags &= ~D3D11_CREATE_DEVICE_DEBUG;
        Hr = D3D11CreateDeviceAndSwapChain(
            nullptr,
            D3D_DRIVER_TYPE_HARDWARE,
            nullptr,
            Flags,
            RequestedLevels,
            ARRAYSIZE(RequestedLevels),
            D3D11_SDK_VERSION,
            &Desc,
            &SwapChain,
            &Device,
            &CreatedLevel,
            &DeviceContext);
    }
#endif

    if (FAILED(Hr))
    {
        Shutdown();
        return false;
    }

    bInitialized = CreateBackBuffer();
    if (!bInitialized)
    {
        Shutdown();
    }
    return bInitialized;
}

void SferaD3D11Renderer::Shutdown()
{
    ReleaseBackBuffer();
    SferaSafeRelease(SwapChain);
    SferaSafeRelease(DeviceContext);
    SferaSafeRelease(Device);
    WindowHandle = nullptr;
    bInitialized = false;
}

bool SferaD3D11Renderer::CreateBackBuffer()
{
    if (!SwapChain || !Device)
    {
        return false;
    }

    ID3D11Texture2D* BackBuffer = nullptr;
    HRESULT Hr = SwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&BackBuffer));
    if (FAILED(Hr) || !BackBuffer)
    {
        return false;
    }

    Hr = Device->CreateRenderTargetView(BackBuffer, nullptr, &BackBufferView);
    BackBuffer->Release();
    return SUCCEEDED(Hr) && BackBufferView != nullptr;
}

void SferaD3D11Renderer::ReleaseBackBuffer()
{
    if (DeviceContext)
    {
        DeviceContext->OMSetRenderTargets(0, nullptr, nullptr);
    }
    SferaSafeRelease(BackBufferView);
}

bool SferaD3D11Renderer::BeginFrame(float R, float G, float B, float A)
{
    if (!bInitialized || !DeviceContext || !BackBufferView)
    {
        return false;
    }

    const float ClearColor[4] = { R, G, B, A };
    DeviceContext->OMSetRenderTargets(1, &BackBufferView, nullptr);
    DeviceContext->ClearRenderTargetView(BackBufferView, ClearColor);
    return true;
}

void SferaD3D11Renderer::Present()
{
    if (SwapChain)
    {
        SwapChain->Present(1, 0);
    }
}
