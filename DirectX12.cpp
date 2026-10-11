#include <windows.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <iostream>
#include <string>

#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")

using Microsoft::WRL::ComPtr;

class D3D12Renderer
{
public:
    static const UINT FrameCount = 2;

    struct Vertex
    {
        float position[2];
    };

    bool Initialize(HWND hwnd, int width, int height)
    {
        m_width = width;
        m_height = height;

#if defined(_DEBUG)
        ComPtr<ID3D12Debug> debugController;
        if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController))))
        {
            debugController->EnableDebugLayer();
        }
#endif

        ComPtr<IDXGIFactory4> factory;
        if (FAILED(CreateDXGIFactory2(0, IID_PPV_ARGS(&factory))))
        {
            std::cerr << "[Error] Failed to create DXGI Factory." << std::endl;
            return false;
        }

        if (FAILED(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&m_device))))
        {
            std::cerr << "[Error] Failed to create D3D12 Device." << std::endl;
            return false;
        }

        D3D12_COMMAND_QUEUE_DESC queueDesc = {};
        queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
        if (FAILED(m_device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&m_commandQueue))))
        {
            return false;
        }

        DXGI_SWAP_CHAIN_DESC1 swapChainDesc = {};
        swapChainDesc.BufferCount = FrameCount;
        swapChainDesc.Width = m_width;
        swapChainDesc.Height = m_height;
        swapChainDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        swapChainDesc.SampleDesc.Count = 1;

        ComPtr<IDXGISwapChain1> swapChain1;
        if (FAILED(factory->CreateSwapChainForHwnd(
            m_commandQueue.Get(), hwnd, &swapChainDesc, nullptr, nullptr, &swapChain1)))
        {
            std::cerr << "[Error] Failed to create Swap Chain." << std::endl;
            return false;
        }

        swapChain1.As(&m_swapChain);
        m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();

        D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {};
        rtvHeapDesc.NumDescriptors = FrameCount;
        rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        if (FAILED(m_device->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&m_rtvHeap))))
        {
            return false;
        }
        m_rtvDescriptorSize = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

        CreateRenderTargetViews();

        if (FAILED(m_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&m_commandAllocator))))
        {
            return false;
        }

        if (FAILED(m_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, m_commandAllocator.Get(), nullptr, IID_PPV_ARGS(&m_commandList))))
        {
            return false;
        }
        m_commandList->Close();

        if (!CreatePipelineState())
        {
            return false;
        }

        if (!CreateVertexBuffer())
        {
            return false;
        }

        if (FAILED(m_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_fence))))
        {
            return false;
        }
        m_fenceValue = 1;
        m_fenceEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);

        UpdateViewportAndScissor();
        std::cout << "[Info] DirectX 12 renderer initialized successfully." << std::endl;
        return true;
    }

    void Render()
    {
        m_commandAllocator->Reset();
        m_commandList->Reset(m_commandAllocator.Get(), m_pipelineState.Get());

        D3D12_RESOURCE_BARRIER barrier = {};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource = m_renderTargets[m_frameIndex].Get();
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        m_commandList->ResourceBarrier(1, &barrier);

        D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = m_rtvHeap->GetCPUDescriptorHandleForHeapStart();
        rtvHandle.ptr += static_cast<SIZE_T>(m_frameIndex) * m_rtvDescriptorSize;

        const float clearColor[] = { 0.0f, 0.0f, 0.0f, 1.0f };
        m_commandList->ClearRenderTargetView(rtvHandle, clearColor, 0, nullptr);
        m_commandList->OMSetRenderTargets(1, &rtvHandle, FALSE, nullptr);

        m_commandList->RSSetViewports(1, &m_viewport);
        m_commandList->RSSetScissorRects(1, &m_scissorRect);

        m_commandList->SetGraphicsRootSignature(m_rootSignature.Get());

        float aspectRatio = (m_height > 0) ? (static_cast<float>(m_width) / static_cast<float>(m_height)) : 1.0f;
        m_commandList->SetGraphicsRoot32BitConstants(0, 1, &aspectRatio, 0);

        m_commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        m_commandList->IASetVertexBuffers(0, 1, &m_vertexBufferView);

        m_commandList->DrawInstanced(3, 1, 0, 0);

        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
        m_commandList->ResourceBarrier(1, &barrier);

        m_commandList->Close();

        ID3D12CommandList* ppCommandLists[] = { m_commandList.Get() };
        m_commandQueue->ExecuteCommandLists(_countof(ppCommandLists), ppCommandLists);

        m_swapChain->Present(1, 0);

        WaitForGpu();

        m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();
    }

    void Resize(int width, int height)
    {
        if (width <= 0 || height <= 0 || !m_swapChain) return;
        m_width = width;
        m_height = height;

        WaitForGpu();

        for (UINT i = 0; i < FrameCount; ++i)
        {
            m_renderTargets[i].Reset();
        }

        m_swapChain->ResizeBuffers(FrameCount, m_width, m_height, DXGI_FORMAT_R8G8B8A8_UNORM, 0);
        m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();

        CreateRenderTargetViews();
        UpdateViewportAndScissor();
    }

    void Shutdown()
    {
        WaitForGpu();
        if (m_fenceEvent)
        {
            CloseHandle(m_fenceEvent);
            m_fenceEvent = nullptr;
        }
    }

private:
    void CreateRenderTargetViews()
    {
        D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = m_rtvHeap->GetCPUDescriptorHandleForHeapStart();
        for (UINT i = 0; i < FrameCount; ++i)
        {
            m_swapChain->GetBuffer(i, IID_PPV_ARGS(&m_renderTargets[i]));
            m_device->CreateRenderTargetView(m_renderTargets[i].Get(), nullptr, rtvHandle);
            rtvHandle.ptr += m_rtvDescriptorSize;
        }
    }

    void UpdateViewportAndScissor()
    {
        m_viewport = { 0.0f, 0.0f, static_cast<float>(m_width), static_cast<float>(m_height), 0.0f, 1.0f };
        m_scissorRect = { 0, 0, m_width, m_height };
    }

    bool CreatePipelineState()
    {
        D3D12_ROOT_PARAMETER rootParam = {};
        rootParam.ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        rootParam.Constants.ShaderRegister = 0;
        rootParam.Constants.RegisterSpace = 0;
        rootParam.Constants.Num32BitValues = 1;
        rootParam.ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;

        D3D12_ROOT_SIGNATURE_DESC rootSigDesc = {};
        rootSigDesc.NumParameters = 1;
        rootSigDesc.pParameters = &rootParam;
        rootSigDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

        ComPtr<ID3DBlob> signature;
        ComPtr<ID3DBlob> error;
        if (FAILED(D3D12SerializeRootSignature(&rootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1, &signature, &error)))
        {
            return false;
        }
        if (FAILED(m_device->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS(&m_rootSignature))))
        {
            return false;
        }

        const char* shaderSource = R"(
            cbuffer SceneConstants : register(b0) {
                float aspectRatio;
            };

            struct VSInput {
                float2 position : POSITION;
            };
            struct PSInput {
                float4 position : SV_POSITION;
            };

            PSInput VSMain(VSInput input) {
                PSInput output;
                output.position = float4(input.position.x / aspectRatio, input.position.y, 0.0f, 1.0f);
                return output;
            }

            float4 PSMain(PSInput input) : SV_TARGET {
                return float4(1.0f, 1.0f, 1.0f, 1.0f);
            }
        )";

        ComPtr<ID3DBlob> vertexShader;
        ComPtr<ID3DBlob> pixelShader;
        if (FAILED(D3DCompile(shaderSource, strlen(shaderSource), nullptr, nullptr, nullptr, "VSMain", "vs_5_0", 0, 0, &vertexShader, &error)))
        {
            if (error) std::cerr << static_cast<char*>(error->GetBufferPointer()) << std::endl;
            return false;
        }
        if (FAILED(D3DCompile(shaderSource, strlen(shaderSource), nullptr, nullptr, nullptr, "PSMain", "ps_5_0", 0, 0, &pixelShader, &error)))
        {
            if (error) std::cerr << static_cast<char*>(error->GetBufferPointer()) << std::endl;
            return false;
        }

        D3D12_INPUT_ELEMENT_DESC inputElementDescs[] = {
            { "POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
        };

        D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
        psoDesc.InputLayout = { inputElementDescs, _countof(inputElementDescs) };
        psoDesc.pRootSignature = m_rootSignature.Get();
        psoDesc.VS = { vertexShader->GetBufferPointer(), vertexShader->GetBufferSize() };
        psoDesc.PS = { pixelShader->GetBufferPointer(), pixelShader->GetBufferSize() };

        psoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
        psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;

        psoDesc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

        psoDesc.SampleMask = UINT_MAX;
        psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        psoDesc.NumRenderTargets = 1;
        psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
        psoDesc.SampleDesc.Count = 1;

        return SUCCEEDED(m_device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&m_pipelineState)));
    }

    bool CreateVertexBuffer()
    {
        Vertex triangleVertices[] = {
            {  0.0f,          0.6f },
            {  0.519615f,   -0.3f },
            { -0.519615f,   -0.3f }
        };

        const UINT vertexBufferSize = sizeof(triangleVertices);

        D3D12_HEAP_PROPERTIES heapProps = {};
        heapProps.Type = D3D12_HEAP_TYPE_UPLOAD;

        D3D12_RESOURCE_DESC resDesc = {};
        resDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        resDesc.Width = vertexBufferSize;
        resDesc.Height = 1;
        resDesc.DepthOrArraySize = 1;
        resDesc.MipLevels = 1;
        resDesc.Format = DXGI_FORMAT_UNKNOWN;
        resDesc.SampleDesc.Count = 1;
        resDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

        if (FAILED(m_device->CreateCommittedResource(
            &heapProps, D3D12_HEAP_FLAG_NONE, &resDesc,
            D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
            IID_PPV_ARGS(&m_vertexBuffer))))
        {
            return false;
        }

        UINT8* pVertexDataBegin = nullptr;
        D3D12_RANGE readRange = { 0, 0 };
        if (FAILED(m_vertexBuffer->Map(0, &readRange, reinterpret_cast<void**>(&pVertexDataBegin))))
        {
            return false;
        }
        memcpy(pVertexDataBegin, triangleVertices, sizeof(triangleVertices));
        m_vertexBuffer->Unmap(0, nullptr);

        m_vertexBufferView.BufferLocation = m_vertexBuffer->GetGPUVirtualAddress();
        m_vertexBufferView.StrideInBytes = sizeof(Vertex);
        m_vertexBufferView.SizeInBytes = vertexBufferSize;

        return true;
    }

    void WaitForGpu()
    {
        m_commandQueue->Signal(m_fence.Get(), m_fenceValue);
        if (m_fence->GetCompletedValue() < m_fenceValue)
        {
            m_fence->SetEventOnCompletion(m_fenceValue, m_fenceEvent);
            WaitForSingleObject(m_fenceEvent, INFINITE);
        }
        m_fenceValue++;
    }

    int m_width = 0;
    int m_height = 0;
    UINT m_frameIndex = 0;
    UINT m_rtvDescriptorSize = 0;

    ComPtr<ID3D12Device> m_device;
    ComPtr<ID3D12CommandQueue> m_commandQueue;
    ComPtr<IDXGISwapChain3> m_swapChain;
    ComPtr<ID3D12DescriptorHeap> m_rtvHeap;
    ComPtr<ID3D12Resource> m_renderTargets[FrameCount];
    ComPtr<ID3D12CommandAllocator> m_commandAllocator;
    ComPtr<ID3D12GraphicsCommandList> m_commandList;
    ComPtr<ID3D12RootSignature> m_rootSignature;
    ComPtr<ID3D12PipelineState> m_pipelineState;
    ComPtr<ID3D12Resource> m_vertexBuffer;
    D3D12_VERTEX_BUFFER_VIEW m_vertexBufferView{};

    ComPtr<ID3D12Fence> m_fence;
    UINT64 m_fenceValue = 0;
    HANDLE m_fenceEvent = nullptr;

    D3D12_VIEWPORT m_viewport{};
    D3D12_RECT m_scissorRect{};
};

class Window
{
public:
    Window(const std::wstring& title, int width, int height)
        : m_title(title), m_width(width), m_height(height)
    {
    }

    ~Window()
    {
        if (m_hwnd)
        {
            DestroyWindow(m_hwnd);
        }
        UnregisterClassW(L"GameEngineWindow", GetModuleHandle(nullptr));
    }

    bool Initialize(bool startFullscreen = false)
    {
        HINSTANCE hInst = GetModuleHandle(nullptr);

        WNDCLASSEXW wc = { sizeof(WNDCLASSEXW) };
        wc.style = CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc = StaticWndProc;
        wc.hInstance = hInst;
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
        wc.lpszClassName = L"GameEngineWindow";

        if (!RegisterClassExW(&wc))
        {
            std::cerr << "[Error] Failed to register window class." << std::endl;
            return false;
        }

        RECT rc = { 0, 0, m_width, m_height };
        AdjustWindowRect(&rc, WS_OVERLAPPEDWINDOW, FALSE);

        m_hwnd = CreateWindowExW(
            0,
            wc.lpszClassName,
            m_title.c_str(),
            WS_OVERLAPPEDWINDOW,
            CW_USEDEFAULT, CW_USEDEFAULT,
            rc.right - rc.left,
            rc.bottom - rc.top,
            nullptr,
            nullptr,
            hInst,
            this
        );

        if (!m_hwnd)
        {
            std::cerr << "[Error] Failed to create window." << std::endl;
            return false;
        }

        if (startFullscreen)
        {
            SetFullscreen(true);
        }

        ShowWindow(m_hwnd, SW_SHOW);
        return true;
    }

    void SetFullscreen(bool fullscreen)
    {
        if (m_isFullscreen == fullscreen) return;
        m_isFullscreen = fullscreen;

        DWORD style = static_cast<DWORD>(GetWindowLongPtrW(m_hwnd, GWL_STYLE));

        if (m_isFullscreen)
        {
            MONITORINFO mi = { sizeof(mi) };
            if (GetWindowPlacement(m_hwnd, &m_prevPlacement) &&
                GetMonitorInfoW(MonitorFromWindow(m_hwnd, MONITOR_DEFAULTTONEAREST), &mi))
            {
                SetWindowLongPtrW(m_hwnd, GWL_STYLE, style & ~WS_OVERLAPPEDWINDOW);

                int monitorWidth  = mi.rcMonitor.right - mi.rcMonitor.left;
                int monitorHeight = mi.rcMonitor.bottom - mi.rcMonitor.top;

                SetWindowPos(m_hwnd, HWND_TOP,
                    mi.rcMonitor.left, mi.rcMonitor.top,
                    monitorWidth,
                    monitorHeight,
                    SWP_NOOWNERZORDER | SWP_FRAMECHANGED);

                std::cout << "[Window] Switched to fullscreen mode ("
                          << monitorWidth << "x" << monitorHeight << ")" << std::endl;
            }
        }
        else
        {
            SetWindowLongPtrW(m_hwnd, GWL_STYLE, style | WS_OVERLAPPEDWINDOW);
            SetWindowPlacement(m_hwnd, &m_prevPlacement);
            SetWindowPos(m_hwnd, nullptr, 0, 0, 0, 0,
                SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER |
                SWP_NOOWNERZORDER | SWP_FRAMECHANGED);

            std::cout << "[Window] Switched to windowed mode." << std::endl;
        }
    }

    void ToggleFullscreen()
    {
        SetFullscreen(!m_isFullscreen);
    }

    bool ProcessMessages()
    {
        MSG msg = {};
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE))
        {
            if (msg.message == WM_QUIT)
            {
                return false;
            }
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        return true;
    }

    HWND GetHwnd() const { return m_hwnd; }
    int GetWidth() const { return m_width; }
    int GetHeight() const { return m_height; }
    void SetRenderer(D3D12Renderer* renderer) { m_renderer = renderer; }

private:
    static LRESULT CALLBACK StaticWndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
    {
        Window* pWindow = nullptr;
        if (uMsg == WM_CREATE)
        {
            auto* pCreate = reinterpret_cast<CREATESTRUCTW*>(lParam);
            pWindow = reinterpret_cast<Window*>(pCreate->lpCreateParams);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(pWindow));
        }
        else
        {
            pWindow = reinterpret_cast<Window*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        }

        return pWindow ? pWindow->WndProc(hwnd, uMsg, wParam, lParam) : DefWindowProcW(hwnd, uMsg, wParam, lParam);
    }

    LRESULT WndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
    {
        switch (uMsg)
        {
        case WM_SIZE:
            if (wParam != SIZE_MINIMIZED)
            {
                m_width = LOWORD(lParam);
                m_height = HIWORD(lParam);
                if (m_renderer)
                {
                    m_renderer->Resize(m_width, m_height);
                }
            }
            return 0;

        case WM_KEYDOWN:
            if (wParam == VK_F11)
            {
                ToggleFullscreen();
                return 0;
            }
            if (wParam == VK_ESCAPE && m_isFullscreen)
            {
                SetFullscreen(false);
                return 0;
            }
            break;

        case WM_SYSKEYDOWN:
            if (wParam == VK_RETURN && (lParam & (1 << 29)))
            {
                ToggleFullscreen();
                return 0;
            }
            break;

        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
        }

        return DefWindowProcW(hwnd, uMsg, wParam, lParam);
    }

    std::wstring m_title;
    int m_width = 0;
    int m_height = 0;
    HWND m_hwnd = nullptr;
    bool m_isFullscreen = false;
    WINDOWPLACEMENT m_prevPlacement{ sizeof(WINDOWPLACEMENT) };
    D3D12Renderer* m_renderer = nullptr;
};

int main()
{
    SetConsoleOutputCP(CP_UTF8);

    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    Window window(L"Game Engine - DirectX 12", 1280, 720);
    if (!window.Initialize())
    {
        return -1;
    }

    D3D12Renderer renderer;
    if (!renderer.Initialize(window.GetHwnd(), window.GetWidth(), window.GetHeight()))
    {
        return -1;
    }

    window.SetRenderer(&renderer);

    std::cout << "[Info] Ready to render! ([F11] or [Alt+Enter] to toggle fullscreen)" << std::endl;

    while (window.ProcessMessages())
    {
        renderer.Render();
    }

    renderer.Shutdown();
    std::cout << "[Info] Application exiting cleanly." << std::endl;
    return 0;
}
