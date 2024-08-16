#pragma once

#include "AnniMath.h"
#include "CrossWindow/CrossWindow.h"
#include "FrameResource.h"
#include "GltfModel.h"

#include <array>
#include <chrono>
#include <vector>

// Renderer
namespace Anni {

class Renderer {
    // Public interface
public:
    // Render
    void OnRender();
    // Resize the window and internal data structures
    void OnReSize(unsigned width, unsigned height);
    // Update
    void OnUpdateGlobal(const std::vector<xwin::KeyboardData>& keyboard_data);

public:
    Renderer(xwin::Window& window);
    Renderer() = delete;
    Renderer(const Renderer&) = delete;
    Renderer(Renderer&&) = delete;
    Renderer& operator=(const Renderer&) = delete;
    Renderer& operator=(Renderer&&) = delete;

    ~Renderer();

protected:
    void initAPIWinPtrInit(xwin::Window& window);
    void initAPICreateFactory();
    void initAPICreateAdapter();
    void initAPICreateDevices();
    void initAPICreateCommandQueues();
    void initAPICreateSwapChain(xwin::Window& window);
    void createBackBufferRTVDescriptorHeap();
    void initAPIDXCompiler();
    void initAPICreateGlobalSyncPrimitives();

    //***********************************************
protected:
    // Initialize DX12 Graphics API
    void initializeAPI(xwin::Window& window);
    void initializeResources();
    void initializeScene();
    void initSceneModels();
    void initializeFrameResources();
    void initializeGlobalCommands();
    //void createCommandList();

protected:
    void destroyAPI();
    void destroyResources();
    // void setupCommands();
    void destroyCommands();

    void initBackBuffer();
    void destroyFrameBuffer();
    // void createSynchronization();
    void setupSwapchain(unsigned width, unsigned height);

protected:
    // Windows system
    xwin::Window* m_Window;
    unsigned m_Width, m_Height;

    D3D12_VIEWPORT m_Viewport;
    D3D12_RECT m_ScissorRect;

    // Timer
    std::chrono::time_point<std::chrono::steady_clock> tStart, tEnd;

    // Current Frame number
    UINT m_GlobalFrameNum;

    // D3D12 Initialization
#if defined(_DEBUG)
    WRL::ComPtr<ID3D12Debug1> m_DebugController;
    WRL::ComPtr<ID3D12DebugDevice> m_DebugDevice;
#endif

    WRL::ComPtr<IDXGIFactory4> m_Factory;
    WRL::ComPtr<IDXGIAdapter1> m_Adapter;
    WRL::ComPtr<ID3D12Device> m_Device;

    WRL::ComPtr<ID3D12CommandQueue> m_MainDirectQueue;
    WRL::ComPtr<ID3D12CommandAllocator> m_MainDirectCommandAllocator;
    WRL::ComPtr<ID3D12GraphicsCommandList> m_MainDirectCommandList;

    WRL::ComPtr<ID3D12CommandQueue> m_MainCopyQueue;
    WRL::ComPtr<ID3D12CommandAllocator> m_CopyCommandAllocator;
    WRL::ComPtr<ID3D12GraphicsCommandList> m_CopyCommandList;

    WRL::ComPtr<IDXGISwapChain3> m_Swapchain;
    WRL::ComPtr<ID3D12Resource> m_BackBuffer[BACKBUFFER_COUNT];
    D3D12_CPU_DESCRIPTOR_HANDLE m_BackBufferRenderTargetViews[BACKBUFFER_COUNT];
    WRL::ComPtr<ID3D12DescriptorHeap> m_BackBufferRtvDescHeap;

    // Resources
    std::unique_ptr<GltfModel> m_sponza;
    std::unique_ptr<GltfModel> m_METAX;

    // For protection of backbuffer
    HANDLE m_fenceEventBackBuffer[BACKBUFFER_COUNT];
    WRL::ComPtr<ID3D12Fence> m_fenceBackBuffer[BACKBUFFER_COUNT];
    UINT64 m_fenceValuesBackBuffer[BACKBUFFER_COUNT];

    // UINT m_FrameIndex;
    // HANDLE m_FenceEvent;

    WRL::ComPtr<ID3D12Fence> m_fenceGlobal;
    UINT64 m_fenceValueGlobal{0};
    HANDLE m_fenceEventGlobal;

    // DXC compiler
    WRL::ComPtr<IDxcUtils> m_dxcUtils;
    WRL::ComPtr<IDxcCompiler> m_dxcCompiler;
    WRL::ComPtr<IDxcIncludeHandler> m_includeHandler;

protected:
    std::array<std::unique_ptr<FrameResource>, FRAME_INFLIGHT_COUNT>
        m_frame_resources;
};

}