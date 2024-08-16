#pragma once

#include "AnniMath.h"
#include "AnniUtils.h"
#include "Camera.h"
#include "CrossWindow/Graphics.h"
#include "GltfModel.h"

namespace Anni {
namespace Constants {
    extern D3D12_INPUT_ELEMENT_DESC StandardVertexDescription[5];
}

class FrameResource {
public:
    void RecordCommandsAndExecute(ID3D12CommandQueue* direct_queue);
    void OnUpdateGlobalState(const std::vector<xwin::KeyboardData>& keyboard_data);
    void OnUpdatePerFrame();

public:
    FrameResource(
        ID3D12Device* pp_device,
        IDXGISwapChain3* pp_swapChain,

        IDxcUtils* dxc_utils,
        IDxcCompiler* dxc_compiler,
        IDxcIncludeHandler* include_handler,

        WRL::ComPtr<ID3D12Resource> (&back_buffer)[BACKBUFFER_COUNT],
        D3D12_CPU_DESCRIPTOR_HANDLE (&back_buffer_rendertarget_views)[BACKBUFFER_COUNT],
        // Models in scene
        GltfModel& sponza,
        GltfModel& METAX,
        const D3D12_VIEWPORT& viewport,
        const D3D12_RECT& scissor_rect);
    FrameResource() = delete;
    FrameResource(const FrameResource&) = delete;
    FrameResource(FrameResource&&) = delete;
    FrameResource& operator=(const FrameResource&) = delete;
    FrameResource& operator=(FrameResource&&) = delete;
    ~FrameResource() = default;

private:
    void InitCommandLists();
    void InitSyncObject();
    void InitDescriptorHeap();
    void InitConstBuffer();
    void SetupLights();
    void SetupCamera();

private:
    void InitShadowPass();
    void InitShadowPassRootSignature();
    void InitShadowPassShaders();
    void InitShadowPassPSO();
    void InitShadowMap();

private:
    void InitScenePass();
    void InitScenePassRootSignature();
    void InitScenePassShaders();
    void InitDepthBuffer();
    void InitRenderTargetsAndRenderTargetViews() const;
    void InitShadowMapSampler();
    void InitScenePassPSO();

private:
    static constexpr UINT NumContexts = 3;
    static constexpr UINT NumLights = 3;

    struct LightState {
        glm::float4 position;
        glm::float4 direction;
        glm::float4 color;
        glm::float4 falloff;

        glm::mat4 view;
        glm::mat4 projection;
    };

    struct LightConstBuffer {
        LightState lights[NumLights];
    };

    struct SceneConstBuffer {
        glm::mat4 model;
        glm::mat4 view;
        glm::mat4 projection;
        glm::float4 ambientColor;
    };

private:
    // OBSERVER POINTER OF D3D COMPONENTS.
    ID3D12Device* m_pp_device;
    IDXGISwapChain3* m_pp_swapChain;

    IDxcUtils* m_dxcUtils;
    IDxcCompiler* m_dxcCompiler;
    IDxcIncludeHandler* m_includeHandler;

    // ID3D12CommandQueue* m_direct_queue;

    // SYNC OBJECTS FRAME RESOURCE.
    WRL::ComPtr<ID3D12Fence> m_frame_fence;
    HANDLE m_fenceEventFrame;
    UINT m_frame_resource_fence_value;

    // BACK BUFFER
    WRL::ComPtr<ID3D12Resource> (&m_backBuffer)[BACKBUFFER_COUNT];
    // BACK BUFFER AS RENDER TARGET VIEW
    D3D12_CPU_DESCRIPTOR_HANDLE(&m_backBufferRenderTargetViews)
    [BACKBUFFER_COUNT];

    // SHADER VISIBLE DESCRIPTOR HEAPS
    WRL::ComPtr<ID3D12DescriptorHeap> m_cbvSrvUavHeapShaderVisible;
    CD3DX12_CPU_DESCRIPTOR_HANDLE m_currentCbvSrvUavHeapCpuHandle;

    WRL::ComPtr<ID3D12DescriptorHeap> m_samplerHeapShaderVisible;
    CD3DX12_CPU_DESCRIPTOR_HANDLE m_currentSamplerCpuHandle;

    // DIRECT BINDING HEAPS(NO NEED TO BE CREATED AS SHADER VISIBLE)
    WRL::ComPtr<ID3D12DescriptorHeap> m_rtvHeap;
    WRL::ComPtr<ID3D12DescriptorHeap> m_dsvHeap;

    // PSO
    WRL::ComPtr<IDxcBlob> m_shadowVertexShader;
    // WRL::ComPtr<IDxcBlob> m_shadowPixelShader;
    WRL::ComPtr<ID3D12RootSignature> m_rootSignatureShadowMap;
    WRL::ComPtr<ID3D12PipelineState> m_shadowMapPSO;

    WRL::ComPtr<IDxcBlob> m_sceneVertexShader;
    WRL::ComPtr<IDxcBlob> m_scenePixelShader;
    WRL::ComPtr<ID3D12RootSignature> m_rootSignatureScene;
    WRL::ComPtr<ID3D12PipelineState> m_scenePSO;

    // CONST BUFFER
    WRL::ComPtr<ID3D12Resource> m_sceneConstantBuffer;
    SceneConstBuffer* m_mappedSceneConstantBuffer;
    SceneConstBuffer m_sceneConstBufferCpuSide;
    // D3D12_CPU_DESCRIPTOR_HANDLE m_sceneCbvHeapHandle;

    WRL::ComPtr<ID3D12Resource> m_lightConstantBuffer;
    LightConstBuffer* m_mappedLightConstantBuffer;
    LightConstBuffer m_lightConstBufferCpuSide;
    // D3D12_CPU_DESCRIPTOR_HANDLE m_lightCbvHeapHandle;

    // COMMANDS RELATED
    WRL::ComPtr<ID3D12CommandAllocator> m_commandListsAllocators[NumContexts];
    WRL::ComPtr<ID3D12GraphicsCommandList> m_commandLists[NumContexts];

    // SHADOW MAP FOR SHADOW PASS AND DEPTH BUFFER FOR SCENE PASS
    WRL::ComPtr<ID3D12Resource> m_shadowPassShadowMap;
    D3D12_CPU_DESCRIPTOR_HANDLE m_cpuHandleToShadowMap;

    WRL::ComPtr<ID3D12Resource> m_scenePassDepthBuffer;
    D3D12_CPU_DESCRIPTOR_HANDLE m_cpuHandleToSceneDepthBuffer;

    // MODELS
    GltfModel& m_sponza;
    GltfModel& m_METAX;

    // CAMERAS
    Camera m_lightCameras[NumLights];
    Camera m_camera;

    // WINDOW RELATED
    const D3D12_VIEWPORT& m_viewPort;
    const D3D12_RECT& m_scissorRect;

    // INCREMENT SIZE
    UINT m_cbvSrvUavIncrementSize;
    UINT m_samplerIncrementSize;
    UINT m_rtvIncrementSize;
    UINT m_dsvIncrementSize;
};
// End of class FrameResource

} // namespace Anni