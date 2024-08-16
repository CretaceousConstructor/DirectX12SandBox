#include "FrameResource.h"

   //**********************************************************************************
    // frames infight(2) < back buffer count(3)
    // frames           :0 1 2 3 4 5 6 7 8
    // frames(modulo)   :0 1 0 1 0 1 0 1 0
    // backbuffer index :0 1 2 0 1 2 0 1 2... ...

    // frames infight(3) > back buffer count(2)
    // frames           :0 1 2 3 4 5 6 7 8
    // frames(modulo)   :0 1 2 0 1 2 0 1 2
    // backbuffer index :0 1 0 1 0 1 0 1 0


// Renderer
namespace Anni {

FrameResource::FrameResource(
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
    const D3D12_RECT& scissor_rect)

    : m_pp_device(pp_device)
    , m_pp_swapChain(pp_swapChain)
    , m_dxcUtils(dxc_utils)
    , m_dxcCompiler(dxc_compiler)
    , m_includeHandler(include_handler)
    , m_frame_resource_fence_value(0)
    , m_backBuffer(back_buffer)
    , m_backBufferRenderTargetViews(back_buffer_rendertarget_views)
    , m_mappedSceneConstantBuffer(nullptr)
    , m_mappedLightConstantBuffer(nullptr)
    , m_sponza(sponza)
    , m_METAX(METAX)
    , m_viewPort(viewport)
    , m_scissorRect(scissor_rect)
    , m_cbvSrvUavIncrementSize(pp_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV))
    , m_samplerIncrementSize(pp_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER))
    , m_rtvIncrementSize(pp_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV))
    , m_dsvIncrementSize(pp_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV))
{
    InitCommandLists();
    InitSyncObject();
    InitDescriptorHeap();
    InitConstBuffer();
    InitShadowPass();
    InitScenePass();
    SetupLights();
    SetupCamera();
}

void FrameResource::RecordCommandsAndExecute(ID3D12CommandQueue* direct_queue)
{
    // TODO: loop through all threads, multithread recording
    constexpr int thread_index = 0;

    //**********************************************************************************
    // YOU MUST WAIT FOR CURRENT FRAME RESOURCE DONE USING BY LAST EXECUTION
    const UINT64 currentCPUSideFrameResourceFenceValue = m_frame_resource_fence_value;
    if (m_frame_fence->GetCompletedValue() < currentCPUSideFrameResourceFenceValue) {
        m_frame_fence->SetEventOnCompletion(currentCPUSideFrameResourceFenceValue,
            m_fenceEventFrame);
        WaitForSingleObjectEx(m_fenceEventFrame, INFINITE, FALSE);
    }

    OnUpdatePerFrame();
 
    // GET BACK BUFFER INDEX:
    // GetCurrentBackBufferIndex: It's just a counter that increments every time you call Present()
    const UINT64 current_back_buffer_index = m_pp_swapChain->GetCurrentBackBufferIndex();

    // Reset allocator and command list
    // 1. CommandAllocator::Reset function note: This method returns E_FAIL if there is an actively recording command list referencing the command allocator. The debug layer will also issue an error in this case.
    // 2. So you should ensure that you don't call Reset until the GPU is done executing command lists associated with the allocator.

    ThrowIfFailed(m_commandListsAllocators[thread_index]->Reset());
    ID3D12GraphicsCommandList* p_command_list = m_commandLists[thread_index].Get();
    // command list 提交到queue以后就可以直接Reset，没有问题
    ThrowIfFailed(p_command_list->Reset(m_commandListsAllocators[thread_index].Get(), nullptr));

    // ******************************
    // Shadow pass*******************
    // ******************************
    // Assume all data from models doing data transfer in the copy queue have been in required resource states.

    // Set pipeline state and root signature
    p_command_list->SetPipelineState(m_shadowMapPSO.Get());
    p_command_list->SetGraphicsRootSignature(m_rootSignatureShadowMap.Get());

    auto per_frame_current_cbv_srv_uav_heap_cpu_handle = m_currentCbvSrvUavHeapCpuHandle;
    auto per_frame_current_sampler_cpu_handle = m_currentSamplerCpuHandle;

    const std::array pp_heaps { m_cbvSrvUavHeapShaderVisible.Get(), m_samplerHeapShaderVisible.Get() };
    p_command_list->SetDescriptorHeaps(pp_heaps.size(), pp_heaps.data());

    // WARNING: models must copy needed descriptors into these two shader-visiable heaps;
    m_sponza.CopyAllDescriptorsTo(m_cbvSrvUavHeapShaderVisible.Get(), m_samplerHeapShaderVisible.Get(), &per_frame_current_cbv_srv_uav_heap_cpu_handle, &per_frame_current_sampler_cpu_handle);
    // m_METAX.(m_cbvSrvUavHeapShaderVisible.Get(), m_samplerHeapShaderVisible.Get(), &per_frame_current_cbv_srv_uav_heap_cpu_handle, &per_frame_current_sampler_cpu_handle);

    // Shdow map pass只需要绑定Local matrices buffer; Scene const buffer; Light const buffer;

    // std::array<CD3DX12_ROOT_PARAMETER1, 3> rootParameters;
    //// LOCAL MATRICES BUFFER
    // CD3DX12_DESCRIPTOR_RANGE1 range;
    // range.Init(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 1, 1, 1);
    // rootParameters[0].InitAsDescriptorTable(1, &range);

    //// SCENE CONST BUFFER
    // rootParameters[1].InitAsConstantBufferView(0, 0);
    //// Light CONST BUFFER
    // rootParameters[2].InitAsConstantBufferView(1, 0);

    p_command_list->SetGraphicsRootConstantBufferView(1, m_sceneConstantBuffer->GetGPUVirtualAddress());
    p_command_list->SetGraphicsRootConstantBufferView(2, m_lightConstantBuffer->GetGPUVirtualAddress());

    p_command_list->RSSetViewports(1, &m_viewPort);
    p_command_list->RSSetScissorRects(1, &m_scissorRect);
    p_command_list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    p_command_list->OMSetStencilRef(0);
    p_command_list->OMSetRenderTargets( // No render target needed for the shadow pass.
        0,
        nullptr,
        FALSE,
        &m_cpuHandleToShadowMap);

    p_command_list->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(m_shadowPassShadowMap.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_DEPTH_WRITE));
    p_command_list->ClearDepthStencilView(m_cpuHandleToShadowMap, D3D12_CLEAR_FLAG_DEPTH, 1.f, 0, 0, nullptr);

    // TODO: m_METAX.Draw(top_matrix_on_model,ctx); //这个用不了sponza的shader :(，还得给他单独搞个pso，或者把所有index变成有符号数，根据负数判断
    // TODO: use multiple threads distribute these draw calls
    //   Distribute objects over threads by drawing only 1/NumContexts
    //   objects per worker (i.e. every object such that objectnum %
    //   NumContexts == threadIndex).

    for (auto&& [index, render_object] : std::ranges::views::enumerate(m_sponza.m_draw_ctx.OpaqueSurfaces)) {
        // if (render_object.material_index == -1) {
        //     assert(false, "这里得想办法安排一个null material，里面的index全部是invalid，然后全部用白色渲染，或者就要再搞一个PSO，专门用来把模型涂成全部白色");
        // }
        p_command_list->SetGraphicsRootDescriptorTable(0, m_sponza.GetGPUDescHandleToLocalMatricesBuffer().Offset(index, m_cbvSrvUavIncrementSize));
        p_command_list->IASetVertexBuffers(0, 1, &render_object.vertex_buffer_view);
        p_command_list->IASetIndexBuffer(&render_object.index_buffer_view);
        p_command_list->DrawIndexedInstanced(render_object.index_count, 1, render_object.first_index, 0, 0);
    }

    // ************************************************************
    // Scene Pass  SM6.6 [RootSignature(BindlessRootSignature)]
    // ************************************************************

    // Indicate that the back buffer will be used as a render target.
    p_command_list->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(m_backBuffer[current_back_buffer_index].Get(), D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET));

    // Transition the shadow map from writeable to readable.
    p_command_list->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(m_shadowPassShadowMap.Get(), D3D12_RESOURCE_STATE_DEPTH_WRITE, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE));

    constexpr FLOAT clear_color[4] { 0.f, 0.f, 0.f, 1.f };
    p_command_list->ClearRenderTargetView(m_backBufferRenderTargetViews[current_back_buffer_index], clear_color, 0, nullptr);

    p_command_list->SetPipelineState(m_scenePSO.Get());
    p_command_list->SetGraphicsRootSignature(m_rootSignatureScene.Get());

    // WARNING: models must copy needed descriptors into these two shader-visiable heaps before call SetGraphicsRootSignature function;
    // const std::array pp_heaps { m_cbvSrvUavHeapShaderVisible.Get(), m_samplerHeapShaderVisible.Get() };
    // p_command_list->SetDescriptorHeaps(pp_heaps.size(), pp_heaps.data());

    // scene const buffer
    p_command_list->SetGraphicsRootConstantBufferView(3, m_sceneConstantBuffer->GetGPUVirtualAddress());

    // light const buffer
    p_command_list->SetGraphicsRootConstantBufferView(4, m_lightConstantBuffer->GetGPUVirtualAddress());

    // SRVs for texture，shadowmap 恰好安在heap中的第一个
    p_command_list->SetGraphicsRootDescriptorTable(5, CD3DX12_GPU_DESCRIPTOR_HANDLE(m_cbvSrvUavHeapShaderVisible->GetGPUDescriptorHandleForHeapStart()));

    // sampler设置，第一个是采样shadow map的sampler（已安装）
    p_command_list->SetGraphicsRootDescriptorTable(7, m_samplerHeapShaderVisible->GetGPUDescriptorHandleForHeapStart());

    p_command_list->RSSetViewports(1, &m_viewPort);
    p_command_list->RSSetScissorRects(1, &m_scissorRect);
    p_command_list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    p_command_list->OMSetStencilRef(0);

    const D3D12_CPU_DESCRIPTOR_HANDLE rtv_handle = m_backBufferRenderTargetViews[current_back_buffer_index];

    p_command_list->ClearDepthStencilView(m_cpuHandleToSceneDepthBuffer, D3D12_CLEAR_FLAG_DEPTH, 1.f, 0, 0, nullptr);
    p_command_list->OMSetRenderTargets(1, &rtv_handle, FALSE, &m_cpuHandleToSceneDepthBuffer);

    // TODO: use multiple threads distribute these draw calls
    //   Distribute objects over threads by drawing only 1/NumContexts
    //   objects per worker (i.e. every object such that objectnum %
    //   NumContexts == threadIndex).

    // sponza drawing
    {
        p_command_list->SetGraphicsRootDescriptorTable(2, m_sponza.GetGPUDescHandleToMaterialConstantsBuffer());
        // SRVs for bindless textures
        p_command_list->SetGraphicsRootDescriptorTable(6, m_sponza.GetGPUDescHandleToTexturesTable());
        // bindless sampelrs for model
        p_command_list->SetGraphicsRootDescriptorTable(8, m_sponza.GetGPUDescHandleToSamplers());

        for (auto&& [index, render_object] : std::ranges::views::enumerate(m_sponza.m_draw_ctx.OpaqueSurfaces)) {
            // if (render_object.material_index == -1) {
            //     assert(false, "这里得想办法安排一个null material，里面的index全部是invalid，然后全部用白色渲染，或者就要再搞一个PSO，专门用来把模型涂成全部白色");
            // }
            p_command_list->IASetVertexBuffers(0, 1, &render_object.vertex_buffer_view);
            p_command_list->IASetIndexBuffer(&render_object.index_buffer_view);

            // The change made to a root constant will **BE RECORDED INTO THE COMMAND LIST**, makes a root constant very suitable for samll, very dynamic data(changing very draw call)
            p_command_list->SetGraphicsRoot32BitConstant(0, render_object.material_index, 0);
            // material constant structured bindlss buffer

            // Local matrices buffer, change every draw call by creating as many views as number of the matrices. But we still only got on big buffer for all matrices
            p_command_list->SetGraphicsRootDescriptorTable(1, m_sponza.GetGPUDescHandleToLocalMatricesBuffer().Offset(index, m_cbvSrvUavIncrementSize));

            p_command_list->DrawIndexedInstanced(render_object.index_count, 1, render_object.first_index, 0, 0);
        }
    }

    p_command_list->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(m_backBuffer[current_back_buffer_index].Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT));

    ThrowIfFailed(p_command_list->Close());

    const std::vector<ID3D12CommandList*> submitted_commands { m_commandLists[thread_index].Get() };
    direct_queue->ExecuteCommandLists(1, submitted_commands.data());

  //  {

  //      thread_index = 1;
		//p_command_list = m_commandLists[thread_index].Get();
  //      // command list 提交到queue以后就可以直接Reset，没有问题
  //      ThrowIfFailed(p_command_list->Reset(m_commandListsAllocators[thread_index].Get(), nullptr));

  //      // ******************************
  //      // Shadow pass*******************
  //      // ******************************
  //      // Assume all data from models doing data transfer in the copy queue have been in required resource states.

  //      // Set pipeline state and root signature
  //      p_command_list->SetPipelineState(m_shadowMapPSO.Get());
  //      p_command_list->SetGraphicsRootSignature(m_rootSignatureShadowMap.Get());

  //      auto per_frame_current_cbv_srv_uav_heap_cpu_handle = m_currentCbvSrvUavHeapCpuHandle;
  //      auto per_frame_current_sampler_cpu_handle = m_currentSamplerCpuHandle;

  //      const std::array pp_heaps { m_cbvSrvUavHeapShaderVisible.Get(), m_samplerHeapShaderVisible.Get() };
  //      p_command_list->SetDescriptorHeaps(pp_heaps.size(), pp_heaps.data());

  //      // WARNING: models must copy needed descriptors into these two shader-visiable heaps;
  //      m_sponza.CopyAllDescriptorsTo(m_cbvSrvUavHeapShaderVisible.Get(), m_samplerHeapShaderVisible.Get(), &per_frame_current_cbv_srv_uav_heap_cpu_handle, &per_frame_current_sampler_cpu_handle);
  //      // m_METAX.(m_cbvSrvUavHeapShaderVisible.Get(), m_samplerHeapShaderVisible.Get(), &per_frame_current_cbv_srv_uav_heap_cpu_handle, &per_frame_current_sampler_cpu_handle);

  //      // Shdow map pass只需要绑定Local matrices buffer; Scene const buffer; Light const buffer;

  //      // std::array<CD3DX12_ROOT_PARAMETER1, 3> rootParameters;
  //      //// LOCAL MATRICES BUFFER
  //      // CD3DX12_DESCRIPTOR_RANGE1 range;
  //      // range.Init(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 1, 1, 1);
  //      // rootParameters[0].InitAsDescriptorTable(1, &range);

  //      //// SCENE CONST BUFFER
  //      // rootParameters[1].InitAsConstantBufferView(0, 0);
  //      //// Light CONST BUFFER
  //      // rootParameters[2].InitAsConstantBufferView(1, 0);

  //      p_command_list->SetGraphicsRootConstantBufferView(1, m_sceneConstantBuffer->GetGPUVirtualAddress());
  //      p_command_list->SetGraphicsRootConstantBufferView(2, m_lightConstantBuffer->GetGPUVirtualAddress());

  //      p_command_list->RSSetViewports(1, &m_viewPort);
  //      p_command_list->RSSetScissorRects(1, &m_scissorRect);
  //      p_command_list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  //      p_command_list->OMSetStencilRef(0);
  //      p_command_list->OMSetRenderTargets( // No render target needed for the shadow pass.
  //          0,
  //          nullptr,
  //          FALSE,
  //          &m_cpuHandleToShadowMap);

  //      p_command_list->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(m_shadowPassShadowMap.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_DEPTH_WRITE));
  //      p_command_list->ClearDepthStencilView(m_cpuHandleToShadowMap, D3D12_CLEAR_FLAG_DEPTH, 1.f, 0, 0, nullptr);

  //      // TODO: m_METAX.Draw(top_matrix_on_model,ctx); //这个用不了sponza的shader :(，还得给他单独搞个pso，或者把所有index变成有符号数，根据负数判断
  //      // TODO: use multiple threads distribute these draw calls
  //      //   Distribute objects over threads by drawing only 1/NumContexts
  //      //   objects per worker (i.e. every object such that objectnum %
  //      //   NumContexts == threadIndex).

  //      for (auto&& [index, render_object] : std::ranges::views::enumerate(m_sponza.m_draw_ctx.OpaqueSurfaces)) {
  //          // if (render_object.material_index == -1) {
  //          //     assert(false, "这里得想办法安排一个null material，里面的index全部是invalid，然后全部用白色渲染，或者就要再搞一个PSO，专门用来把模型涂成全部白色");
  //          // }
  //          p_command_list->SetGraphicsRootDescriptorTable(0, m_sponza.GetGPUDescHandleToLocalMatricesBuffer().Offset(index, m_cbvSrvUavIncrementSize));
  //          p_command_list->IASetVertexBuffers(0, 1, &render_object.vertex_buffer_view);
  //          p_command_list->IASetIndexBuffer(&render_object.index_buffer_view);
  //          p_command_list->DrawIndexedInstanced(render_object.index_count, 1, render_object.first_index, 0, 0);
  //      }

  //      // ************************************************************
  //      // Scene Pass  SM6.6 [RootSignature(BindlessRootSignature)]
  //      // ************************************************************

  //      // Indicate that the back buffer will be used as a render target.
  //       p_command_list->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(m_backBuffer[current_back_buffer_index].Get(), D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET));

  //      // Transition the shadow map from writeable to readable.
  //      p_command_list->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(m_shadowPassShadowMap.Get(), D3D12_RESOURCE_STATE_DEPTH_WRITE, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE));

  //      constexpr FLOAT clear_color[4] { 0.f, 0.f, 0.f, 1.f };
  //      p_command_list->ClearRenderTargetView(m_backBufferRenderTargetViews[current_back_buffer_index], clear_color, 0, nullptr);

  //      p_command_list->SetPipelineState(m_scenePSO.Get());
  //      p_command_list->SetGraphicsRootSignature(m_rootSignatureScene.Get());

  //      // WARNING: models must copy needed descriptors into these two shader-visiable heaps before call SetGraphicsRootSignature function;
  //      // const std::array pp_heaps { m_cbvSrvUavHeapShaderVisible.Get(), m_samplerHeapShaderVisible.Get() };
  //      // p_command_list->SetDescriptorHeaps(pp_heaps.size(), pp_heaps.data());

  //      // scene const buffer
  //      p_command_list->SetGraphicsRootConstantBufferView(3, m_sceneConstantBuffer->GetGPUVirtualAddress());

  //      // light const buffer
  //      p_command_list->SetGraphicsRootConstantBufferView(4, m_lightConstantBuffer->GetGPUVirtualAddress());

  //      // SRVs for texture，shadowmap 恰好安在heap中的第一个
  //      p_command_list->SetGraphicsRootDescriptorTable(5, CD3DX12_GPU_DESCRIPTOR_HANDLE(m_cbvSrvUavHeapShaderVisible->GetGPUDescriptorHandleForHeapStart()));

  //      // sampler设置，第一个是采样shadow map的sampler（已安装）
  //      p_command_list->SetGraphicsRootDescriptorTable(7, m_samplerHeapShaderVisible->GetGPUDescriptorHandleForHeapStart());

  //      p_command_list->RSSetViewports(1, &m_viewPort);
  //      p_command_list->RSSetScissorRects(1, &m_scissorRect);
  //      p_command_list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  //      p_command_list->OMSetStencilRef(0);

  //      const D3D12_CPU_DESCRIPTOR_HANDLE rtv_handle = m_backBufferRenderTargetViews[current_back_buffer_index];

  //      p_command_list->ClearDepthStencilView(m_cpuHandleToSceneDepthBuffer, D3D12_CLEAR_FLAG_DEPTH, 1.f, 0, 0, nullptr);
  //      p_command_list->OMSetRenderTargets(1, &rtv_handle, FALSE, &m_cpuHandleToSceneDepthBuffer);

  //      // TODO: use multiple threads distribute these draw calls
  //      //   Distribute objects over threads by drawing only 1/NumContexts
  //      //   objects per worker (i.e. every object such that objectnum %
  //      //   NumContexts == threadIndex).

  //      // sponza drawing
  //      {
  //          p_command_list->SetGraphicsRootDescriptorTable(2, m_sponza.GetGPUDescHandleToMaterialConstantsBuffer());
  //          // SRVs for bindless textures
  //          p_command_list->SetGraphicsRootDescriptorTable(6, m_sponza.GetGPUDescHandleToTexturesTable());
  //          // bindless sampelrs for model
  //          p_command_list->SetGraphicsRootDescriptorTable(8, m_sponza.GetGPUDescHandleToSamplers());

  //          for (auto&& [index, render_object] : std::ranges::views::enumerate(m_sponza.m_draw_ctx.OpaqueSurfaces)) {
  //              // if (render_object.material_index == -1) {
  //              //     assert(false, "这里得想办法安排一个null material，里面的index全部是invalid，然后全部用白色渲染，或者就要再搞一个PSO，专门用来把模型涂成全部白色");
  //              // }
  //              p_command_list->IASetVertexBuffers(0, 1, &render_object.vertex_buffer_view);
  //              p_command_list->IASetIndexBuffer(&render_object.index_buffer_view);

  //              // The change made to a root constant will **BE RECORDED INTO THE COMMAND LIST**, makes a root constant very suitable for samll, very dynamic data(changing very draw call)
  //              p_command_list->SetGraphicsRoot32BitConstant(0, render_object.material_index, 0);
  //              // material constant structured bindlss buffer

  //              // Local matrices buffer, change every draw call by creating as many views as number of the matrices. But we still only got on big buffer for all matrices
  //              p_command_list->SetGraphicsRootDescriptorTable(1, m_sponza.GetGPUDescHandleToLocalMatricesBuffer().Offset(index, m_cbvSrvUavIncrementSize));

  //              p_command_list->DrawIndexedInstanced(render_object.index_count, 1, render_object.first_index, 0, 0);
  //          }
  //      }

  //      p_command_list->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(m_backBuffer[current_back_buffer_index].Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT));

  //      ThrowIfFailed(p_command_list->Close());

  //      const std::vector<ID3D12CommandList*> submitted_commands { m_commandLists[thread_index].Get() };
  //      direct_queue->ExecuteCommandLists(1, submitted_commands.data());
  //  }


    // DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING must be specified when creating the swap chain. Additionally, the DXGI_PRESENT_ALLOW_TEARING flag must be used when presenting the swap chain with a sync-interval of 0.
    m_pp_swapChain->Present(0, DXGI_PRESENT_ALLOW_TEARING);

    // Signal and increment the fence value.
    ThrowIfFailed(direct_queue->Signal(m_frame_fence.Get(), (m_frame_resource_fence_value + 1)));
    ++m_frame_resource_fence_value;
}

void FrameResource::OnUpdateGlobalState(const std::vector<xwin::KeyboardData>& keyboard_data)
{

    m_sceneConstBufferCpuSide.ambientColor = { 0.2f, 0.2f, 0.2f, 1.0f };
    m_sceneConstBufferCpuSide.model = glm::mat4(1.0f);

    for (const auto& key_datum : keyboard_data) {
        if (key_datum.key == xwin::Key::W) {
            m_camera.eye.z += 0.05f;
        }
        if (key_datum.key == xwin::Key::S) {

            m_camera.eye.z -= 0.05f;
        }

        if (key_datum.key == xwin::Key::A) {
            m_camera.eye.x -= 0.05f;
        }

        if (key_datum.key == xwin::Key::D) {
            m_camera.eye.x += 0.05f;
        }

        if (key_datum.key == xwin::Key::J) {
            m_camera.eye.y -= 0.05f;
        }

        if (key_datum.key == xwin::Key::K) {
            m_camera.eye.y += 0.05f;
        }
    }
}

void FrameResource::OnUpdatePerFrame()
{

    // The scene pass is drawn from the camera.
    // 目前shadow pass只用第一盏灯生成深度图。
    m_lightCameras[0].Get3DViewProjMatrices(&m_lightConstBufferCpuSide.lights[0].view, &m_lightConstBufferCpuSide.lights[0].projection, 60.0f, m_viewPort.Width, m_viewPort.Height, 0.1f, 800.f);
    m_lightCameras[1].Get3DViewProjMatrices(&m_lightConstBufferCpuSide.lights[1].view, &m_lightConstBufferCpuSide.lights[1].projection, 60.0f, m_viewPort.Width, m_viewPort.Height, 0.1f, 800.f);
    m_lightCameras[2].Get3DViewProjMatrices(&m_lightConstBufferCpuSide.lights[2].view, &m_lightConstBufferCpuSide.lights[2].projection, 60.0f, m_viewPort.Width, m_viewPort.Height, 0.1f, 800.f);

    m_camera.Get3DViewProjMatrices(&m_sceneConstBufferCpuSide.view, &m_sceneConstBufferCpuSide.projection, 60.0f, m_viewPort.Width, m_viewPort.Height);

    memcpy(m_mappedLightConstantBuffer, &m_lightConstBufferCpuSide, sizeof(m_lightConstBufferCpuSide));
    memcpy(m_mappedSceneConstantBuffer, &m_sceneConstBufferCpuSide, sizeof(m_sceneConstBufferCpuSide));
}

void FrameResource::InitCommandLists()
{
    for (UINT i = 0; i < NumContexts; i++) {
        // Create command list allocators for worker threads.
        // One alloc is for the shadow pass command list, and one is for the
        // scene pass.

        ThrowIfFailed(m_pp_device->CreateCommandAllocator(
            D3D12_COMMAND_LIST_TYPE_DIRECT,
            IID_PPV_ARGS(&m_commandListsAllocators[i])));

        ThrowIfFailed(
            m_pp_device->CreateCommandList(0,
                D3D12_COMMAND_LIST_TYPE_DIRECT,
                m_commandListsAllocators[i].Get(),
                nullptr,
                IID_PPV_ARGS(&m_commandLists[i])));

        ThrowIfFailed(m_commandLists[i]->Close());
    }
}

void FrameResource::InitSyncObject()
{
    assert(m_pp_device);

    // Create Fence for guarding current frame resource.
    ThrowIfFailed(m_pp_device->CreateFence(
        m_frame_resource_fence_value,
        D3D12_FENCE_FLAG_NONE,
        IID_PPV_ARGS(m_frame_fence.ReleaseAndGetAddressOf())));

    m_fenceEventFrame = CreateEvent(nullptr, FALSE, FALSE, nullptr);

    if (!m_fenceEventFrame) {
        ThrowIfFailed(HRESULT_FROM_WIN32(GetLastError()));
    }
}

void FrameResource::InitDescriptorHeap()
{
    // Shader visible cbv srv uav heap and shader visible sampler heap
    {
        D3D12_DESCRIPTOR_HEAP_DESC cbv_srv_uav_desc = {};
        // shadow map SRV + unbouned material constants buffer(bunch of indices used by meshes) + unbouned number of SRVs of textures
        cbv_srv_uav_desc.NumDescriptors = 1 + m_sponza.GetNumberOfTextures() + m_sponza.GetNumberOfMaterial() + TEMP_LOCAL_MATRICES_CBV_COUNT + m_METAX.GetNumberOfTextures() + m_METAX.GetNumberOfMaterial() + TEMP_LOCAL_MATRICES_CBV_COUNT;

        cbv_srv_uav_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        cbv_srv_uav_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        ThrowIfFailed(m_pp_device->CreateDescriptorHeap(
            &cbv_srv_uav_desc, IID_PPV_ARGS(&m_cbvSrvUavHeapShaderVisible)));
        // NAME_D3D12_OBJECT(m_cbvSrvUavHeapShaderVisible);

        // (sampler of shadow map) + (unbounded number of samplers of textures)
        D3D12_DESCRIPTOR_HEAP_DESC sampler_desc = {};
        sampler_desc.NumDescriptors = 1 + m_sponza.GetNumberOfSamplers() + m_METAX.GetNumberOfSamplers();
        sampler_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER;
        sampler_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        ThrowIfFailed(m_pp_device->CreateDescriptorHeap(
            &sampler_desc, IID_PPV_ARGS(&m_samplerHeapShaderVisible)));
        // NAME_D3D12_OBJECT(m_cbvSrvUavHeapShaderVisible);
    }

    // RTV heap(Not being used for now)
    {
        // D3D12_DESCRIPTOR_HEAP_DESC rtv_heap_desc = {};
        // rtv_heap_desc.NumDescriptors = ;)
        // rtv_heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        // rtv_heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        // ThrowIfFailed(m_pp_device->CreateDescriptorHeap(
        //     &rtv_heap_desc, IID_PPV_ARGS(m_rtvHeap.ReleaseAndGetAddressOf())));
    }
    // DSV heap
    {
        D3D12_DESCRIPTOR_HEAP_DESC dsv_heap_desc = {};
        dsv_heap_desc.NumDescriptors = 1 + 1; // shadow map as depth buffer + scene drawing depth buffer
        dsv_heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
        dsv_heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        ThrowIfFailed(m_pp_device->CreateDescriptorHeap(
            &dsv_heap_desc, IID_PPV_ARGS(m_dsvHeap.ReleaseAndGetAddressOf())));
    }
}

void FrameResource::InitConstBuffer()
{
    // Create the scene constant buffers.
    {
        constexpr UINT scene_constant_buffer_size = (sizeof(SceneConstBuffer) + (D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT - 1)) & ~(D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT - 1); // must be a multiple 256 bytes

        ThrowIfFailed(m_pp_device->CreateCommittedResource(
            &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
            D3D12_HEAP_FLAG_NONE,
            &CD3DX12_RESOURCE_DESC::Buffer(scene_constant_buffer_size),
            D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr,
            IID_PPV_ARGS(m_sceneConstantBuffer.ReleaseAndGetAddressOf())));

        // Map the constant buffers and cache their heap pointers.
        const CD3DX12_RANGE read_range(0, 0); // We do not intend to read from this resource on the CPU.
        ThrowIfFailed(m_sceneConstantBuffer->Map(0, &read_range, reinterpret_cast<void**>(&m_mappedSceneConstantBuffer)));
    }

    // TODO: set cpu side const buffer.
    // Create the light constant buffers.
    {
        constexpr UINT light_constant_buffer_size = (sizeof(LightConstBuffer) + (D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT - 1)) & ~(D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT - 1); // must be a multiple 256 bytes

        ThrowIfFailed(m_pp_device->CreateCommittedResource(
            &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
            D3D12_HEAP_FLAG_NONE,
            &CD3DX12_RESOURCE_DESC::Buffer(light_constant_buffer_size),
            D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr,
            IID_PPV_ARGS(m_lightConstantBuffer.ReleaseAndGetAddressOf())));

        const CD3DX12_RANGE read_range(0, 0); // We do not intend to read from this resource on the CPU.
        ThrowIfFailed(m_lightConstantBuffer->Map(0, &read_range, reinterpret_cast<void**>(&m_mappedLightConstantBuffer)));
    }
}

void FrameResource::SetupLights()
{
    // Setup lights.
    for (int i = 0; i < NumLights; i++) {
        // Set up each of the light positions and directions (they all start
        // in the same place).
        m_lightConstBufferCpuSide.lights[i].position = { 0.0f, 6.0f, 0.0f, 1.0f };
        m_lightConstBufferCpuSide.lights[i].direction = { -1., 0.f, 0.0f, 0.0f };
        m_lightConstBufferCpuSide.lights[i].falloff = { 800.0f, 1.0f, 0.0f, 1.0f };
        m_lightConstBufferCpuSide.lights[i].color = { 0.7f, 0.7f, 0.7f, 1.0f };

        const glm::vec4 eye = m_lightConstBufferCpuSide.lights[i].position;
        const glm::vec4 at = eye + m_lightConstBufferCpuSide.lights[i].direction;
        constexpr glm::vec4 up = glm::vec4(0.0f, 1.0f, 0.0f, 0.0f);

        m_lightCameras[i].Set(eye, at, up);
    }
}

void FrameResource::SetupCamera()
{
    //*view = glm::lookAtLH(glm::vec3(0.f,6.f,0.f), glm::vec3(-10.f,8.f,0.f), glm::vec3(0.f,1.f,0.f));
    m_camera.Set(glm::vec4(0.f, 6.f, 0.f, 1.f), glm::vec4(-10.f, 8.f, 0.f, 1.f), glm::vec4(0.f, 1.f, 0.f, 1.f));
}

void FrameResource::InitShadowPass()
{
    InitShadowPassRootSignature();
    InitShadowPassShaders();
    InitShadowPassPSO();
    InitShadowMap();
}

void FrameResource::InitShadowPassRootSignature()
{
    // Create the root signature for shadow pass
    D3D12_FEATURE_DATA_ROOT_SIGNATURE feature_data = {};
    feature_data.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_1;
    // SM 6.6 is required so does is this shit.

    assert(!FAILED(m_pp_device->CheckFeatureSupport(D3D12_FEATURE_ROOT_SIGNATURE, &feature_data, sizeof(feature_data))));

    std::array<CD3DX12_ROOT_PARAMETER1, 3> root_parameters;

    // LOCAL MATRICES BUFFER
    CD3DX12_DESCRIPTOR_RANGE1 range;
    range.Init(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 1, 1, 1);
    root_parameters[0].InitAsDescriptorTable(1, &range);

    // SCENE CONST BUFFER
    root_parameters[1].InitAsConstantBufferView(0, 0);
    // Light CONST BUFFER
    root_parameters[2].InitAsConstantBufferView(1, 0);

    CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSignatureDesc;
    rootSignatureDesc.Init_1_1(
        root_parameters.size(),
        root_parameters.data(),
        0,
        nullptr,
        D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

    WRL::ComPtr<ID3DBlob> signature;
    WRL::ComPtr<ID3DBlob> error;
    ThrowIfFailed(D3DX12SerializeVersionedRootSignature(
        &rootSignatureDesc, feature_data.HighestVersion, &signature, &error));
    ThrowIfFailed(m_pp_device->CreateRootSignature(
        0,
        signature->GetBufferPointer(),
        signature->GetBufferSize(),
        IID_PPV_ARGS(m_rootSignatureShadowMap.ReleaseAndGetAddressOf())));
}

void FrameResource::InitScenePassRootSignature()
{
    // Create the root signature for shadow pass
    D3D12_FEATURE_DATA_ROOT_SIGNATURE feature_data = {};
    feature_data.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_1;
    // SM 6.6 is required so does is this shit.

    assert(!FAILED(m_pp_device->CheckFeatureSupport(D3D12_FEATURE_ROOT_SIGNATURE, &feature_data, sizeof(feature_data))));

    // cbv srv uav shader visible heaps安排：
    // 整个帧使用的的cbv
    // 整个帧使用的的srv
    // 整个帧使用的的uav

    // 模型1的所有srv
    // 模型1的所有cbv
    // 模型1的所有uav

    // 模型2的所有srv
    // 模型2的所有cbv
    // 模型2的所有uav

    // sampler shader visible heaps安排：
    // 整个帧使用的的sampler
    // 模型1的所有sampler
    // 模型2的所有sampler

    CD3DX12_DESCRIPTOR_RANGE1 ranges[6];
    // 注意这里还有个默认参数：offsetInDescriptorsFromTableStart为D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND，可以指定当前range在整个descriptor table中的起始位置
    ranges[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 1, 1, 1); // local matrices buffer
    ranges[1].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, UINT_MAX, 0, 1, D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE); // material constatns structured bindless buffer table
    ranges[2].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0); // shadow map

    ranges[3].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, UINT_MAX, 0, 2, D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE); // texture table

    ranges[4].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER, 1, 0, 0); // shadow map sammpler
    ranges[5].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER, UINT_MAX, 0, 1, D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE); // samplers table

    // 按照使用频率从第一个到最后一个
    std::array<CD3DX12_ROOT_PARAMETER1, 9> rootParameters;
    // space 1 for model, space 0 for frame

    // ROOT CONSTANT FORI **INDEXING INTO MATERIAL CONST BUFFER**
    rootParameters[0].InitAsConstants(1, 0, 1);

    // LOCAL MATRIX CONST BUFFER
    rootParameters[1].InitAsDescriptorTable(1, &ranges[0]);

    // MATERIAL CONSTANTS BUFFER
    rootParameters[2].InitAsDescriptorTable(1, &ranges[1]);

    // SCENE CONST BUFFER
    rootParameters[3].InitAsConstantBufferView(0, 0);

    // LIGHT CONST BUFFER
    rootParameters[4].InitAsConstantBufferView(1, 0);

    // SHADOW MAP
    rootParameters[5].InitAsDescriptorTable(1, &ranges[2], D3D12_SHADER_VISIBILITY_PIXEL);

    // UNBOUND TEXTURES
    rootParameters[6].InitAsDescriptorTable(1, &ranges[3], D3D12_SHADER_VISIBILITY_ALL);

    // SHADOW MAP SAMPLER
    rootParameters[7].InitAsDescriptorTable(1, &ranges[4], D3D12_SHADER_VISIBILITY_ALL);

    //  UNBOUND SAMPLERS
    rootParameters[8].InitAsDescriptorTable(1, &ranges[5], D3D12_SHADER_VISIBILITY_ALL);

    CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSignatureDesc;
    rootSignatureDesc.Init_1_1(
        rootParameters.size(),
        rootParameters.data(),
        0,
        nullptr,
        D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT | D3D12_ROOT_SIGNATURE_FLAG_CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED | D3D12_ROOT_SIGNATURE_FLAG_SAMPLER_HEAP_DIRECTLY_INDEXED);

    WRL::ComPtr<ID3DBlob> signature;
    WRL::ComPtr<ID3DBlob> error;
    ThrowIfFailed(D3DX12SerializeVersionedRootSignature(
        &rootSignatureDesc, feature_data.HighestVersion, &signature, &error));
    ThrowIfFailed(m_pp_device->CreateRootSignature(
        0,
        signature->GetBufferPointer(),
        signature->GetBufferSize(),
        IID_PPV_ARGS(m_rootSignatureScene.ReleaseAndGetAddressOf())));

    // NAME_D3D12_OBJECT(m_rootSignature);
}

void FrameResource::InitScenePassShaders()
{

#if defined(_DEBUG)
    // Enable better shader debugging with the graphics debugging tools.
    constexpr UINT compile_flags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#else
    constexpr UINT compile_flags = D3DCOMPILE_OPTIMIZATION_LEVEL3;
#endif

    WRL::ComPtr<ID3DBlob> errors;
    const auto cwd = std::filesystem::current_path();
    const std::string working_path = cwd.string() + ("\\");
    const std::wstring w_working_path = std::wstring(working_path.begin(), working_path.end());

    //// dxbc是编译以后的中间形式的代码
    // std::string vert_compiled_path = working_path + "assets\\shaders\\scenePass.vert.dxbc";
    // std::string frag_compiled_path = working_path + "assets\\shaders\\scenePass.frag.dxbc";

    //-----默认会编译.hlsl文件到.dxbc----->
    // wchar的hlsl文件路径
    const std::wstring vert_path = w_working_path + L"assets\\shaders\\scenePass.vert.hlsl";
    const std::wstring frag_path = w_working_path + L"assets\\shaders\\scenePass.frag.hlsl";

    m_sceneVertexShader = DXC::CompileShader(
        vert_path, // Path to your shader file
        L"main", // Entry point function name
        L"vs_6_6", // Shader profile
        m_dxcUtils,
        m_dxcCompiler,
        m_includeHandler);

    m_scenePixelShader = DXC::CompileShader(
        frag_path, // Path to your shader file
        L"main", // Entry point function name
        L"ps_6_6", // Shader profile
        m_dxcUtils,
        m_dxcCompiler,
        m_includeHandler);

    // try {
    //     ThrowIfFailed(
    //         D3DCompileFromFile(vert_path.c_str(),
    //             nullptr,
    //             nullptr,
    //             "main",
    //             // TODO: enable shader model 6.6
    //             "vs_6_6",
    //             compile_flags,
    //             0,
    //             m_shadowVertexShader.ReleaseAndGetAddressOf(),
    //             errors.ReleaseAndGetAddressOf()));
    //     ThrowIfFailed(
    //         D3DCompileFromFile(frag_path.c_str(),
    //             nullptr,
    //             nullptr,
    //             "main",
    //             "ps_5_0",
    //             compile_flags,
    //             0,
    //             m_shadowPixelShader.ReleaseAndGetAddressOf(),
    //             errors.ReleaseAndGetAddressOf()));
    // } catch (std::exception&) {
    //     const char* errStr = static_cast<const char*>(errors->GetBufferPointer());
    //     std::cout << errStr;
    // }

    //// 写入dxbc文件中
    // std::ofstream vs_out(vert_compiled_path, std::ios::out | std::ios::binary);
    // std::ofstream fs_out(frag_compiled_path, std::ios::out | std::ios::binary);

    // vs_out.write(
    //     static_cast<const char*>(m_shadowVertexShader->GetBufferPointer()),
    //     m_shadowVertexShader->GetBufferSize());
    // fs_out.write(
    //     static_cast<const char*>(m_shadowPixelShader->GetBufferPointer()),
    //     m_shadowPixelShader->GetBufferSize());
}

void FrameResource::InitDepthBuffer()
{
    // CREATE THE DEPTH STENCIL.
    const CD3DX12_RESOURCE_DESC shadow_texture_desc(
        D3D12_RESOURCE_DIMENSION_TEXTURE2D,
        0,
        static_cast<UINT>(m_viewPort.Width),
        static_cast<UINT>(m_viewPort.Height),
        1,
        1,
        DXGI_FORMAT_D32_FLOAT,
        1,
        0,
        D3D12_TEXTURE_LAYOUT_UNKNOWN,
        D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL | D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE);

    D3D12_CLEAR_VALUE clear_value; // Performance tip: Tell the runtime at
                                   // resource creation the desired clear value.
    clear_value.Format = DXGI_FORMAT_D32_FLOAT;
    clear_value.DepthStencil.Depth = 1.0f;
    clear_value.DepthStencil.Stencil = 0;

    ThrowIfFailed(m_pp_device->CreateCommittedResource(
        &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
        D3D12_HEAP_FLAG_NONE,
        &shadow_texture_desc,
        D3D12_RESOURCE_STATE_DEPTH_WRITE,
        &clear_value,
        IID_PPV_ARGS(m_scenePassDepthBuffer.ReleaseAndGetAddressOf())));

    // NAME_D3D12_OBJECT(m_depthStencil);

    // CREATE THE DEPTH STENCIL VIEW.
    CD3DX12_CPU_DESCRIPTOR_HANDLE dsv_cpu_uhandle(m_dsvHeap->GetCPUDescriptorHandleForHeapStart());
    dsv_cpu_uhandle.Offset(1, m_dsvIncrementSize);
    m_pp_device->CreateDepthStencilView(
        m_scenePassDepthBuffer.Get(), nullptr, dsv_cpu_uhandle);
    m_cpuHandleToSceneDepthBuffer = dsv_cpu_uhandle;
}

void FrameResource::InitRenderTargetsAndRenderTargetViews() const
{
    // we only have back buffers as render targets, no need to create extra.
}

void FrameResource::InitShadowPassShaders()
{
#if defined(_DEBUG)
    // Enable better shader debugging with the graphics debugging tools.
    constexpr UINT compile_flags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#else
    constexpr UINT compile_flags = D3DCOMPILE_OPTIMIZATION_LEVEL3;
#endif

    WRL::ComPtr<ID3DBlob> errors;
    const auto cwd = std::filesystem::current_path();
    const std::string working_path = cwd.string() + ("\\");
    const std::wstring w_working_path = std::wstring(working_path.begin(), working_path.end());

    // dxbc是编译以后的中间形式的代码
    // std::string vert_compiled_path = working_path + "assets\\shaders\\shadowPass.vert.dxbc";
    // std::string frag_compiled_path = working_path + "assets\\shaders\\shadowPass.frag.dxbc";

    //-----默认会编译.hlsl文件到.dxbc----->
    // wchar的hlsl文件路径
    const std::wstring vert_path = w_working_path + L"assets\\shaders\\shadowPass.vert.hlsl";
    // const std::wstring frag_path = w_working_path + L"assets\\shaders\\shadowPass.frag.hlsl";

    // Compile shaders
    m_shadowVertexShader = DXC::CompileShader(
        vert_path, // Path to your shader file
        L"main", // Entry point function name
        L"vs_6_6", // Shader profile
        m_dxcUtils,
        m_dxcCompiler,
        m_includeHandler);

    // try {
    //     WRL::ComPtr<IDxcBlob> shader_blob = DXC::LoadFileAsDxcBlob(vert_path, m_dxcUtils);

    //} catch (std::exception&) {
    //    const char* errStr = static_cast<const char*>(errors->GetBufferPointer());
    //    std::cout << errStr;
    //}

    // try {
    //     ThrowIfFailed(
    //         D3DCompileFromFile(vert_path.c_str(),
    //             nullptr,
    //             nullptr,
    //             "main",
    //             // TODO: enable shader model 6.6
    //             "vs_6_6",
    //             compile_flags,
    //             0,
    //             m_shadowVertexShader.ReleaseAndGetAddressOf(),
    //             errors.ReleaseAndGetAddressOf()));
    //     ThrowIfFailed(
    //         D3DCompileFromFile(frag_path.c_str(),
    //             nullptr,
    //             nullptr,
    //             "main",
    //             "ps_5_0",
    //             compile_flags,
    //             0,
    //             m_shadowPixelShader.ReleaseAndGetAddressOf(),
    //             errors.ReleaseAndGetAddressOf()));
    // } catch (std::exception&) {
    //     const char* errStr = static_cast<const char*>(errors->GetBufferPointer());
    //     std::cout << errStr;
    // }

    //// 写入dxbc文件中
    // std::ofstream vs_out(vert_compiled_path, std::ios::out | std::ios::binary);
    // std::ofstream fs_out(frag_compiled_path, std::ios::out | std::ios::binary);

    // vs_out.write(
    //     static_cast<const char*>(m_shadowVertexShader->GetBufferPointer()),
    //     m_shadowVertexShader->GetBufferSize());
    // fs_out.write(
    //     static_cast<const char*>(m_shadowPixelShader->GetBufferPointer()),
    //     m_shadowPixelShader->GetBufferSize());
}

void FrameResource::InitShadowPassPSO()
{
    // Describe and create the PSO for rendering the shadow map.
    D3D12_INPUT_LAYOUT_DESC input_layout_desc;
    input_layout_desc.pInputElementDescs = Constants::StandardVertexDescription;
    input_layout_desc.NumElements = std::size(Constants::StandardVertexDescription);

    D3D12_GRAPHICS_PIPELINE_STATE_DESC pso_desc {};
    pso_desc.InputLayout = input_layout_desc;
    pso_desc.pRootSignature = m_rootSignatureShadowMap.Get();

    CD3DX12_DEPTH_STENCIL_DESC depth_stencil_desc(D3D12_DEFAULT);
    depth_stencil_desc.DepthEnable = true;
    depth_stencil_desc.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
    depth_stencil_desc.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
    depth_stencil_desc.StencilEnable = FALSE;

    pso_desc.VS.BytecodeLength = m_shadowVertexShader->GetBufferSize();
    pso_desc.VS.pShaderBytecode = m_shadowVertexShader->GetBufferPointer();

    pso_desc.PS = CD3DX12_SHADER_BYTECODE(nullptr, 0);

    auto shadow_pass_rs = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);

    shadow_pass_rs.DepthBias = 0;
    shadow_pass_rs.DepthBiasClamp = 0.0f;
    shadow_pass_rs.SlopeScaledDepthBias = 1.0f;

    // shadow_pass_rs.DepthClipEnable = true;
    // shadow_pass_rs.DepthBias = 100000;
    // shadow_pass_rs.DepthBiasClamp = 0.0f;
    // shadow_pass_rs.SlopeScaledDepthBias = 1.0f;

    pso_desc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
    pso_desc.RasterizerState = shadow_pass_rs;
    pso_desc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
    pso_desc.DepthStencilState = depth_stencil_desc;
    pso_desc.SampleMask = UINT_MAX;
    pso_desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    pso_desc.NumRenderTargets = 0;
    pso_desc.RTVFormats[0] = DXGI_FORMAT_UNKNOWN;
    pso_desc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
    pso_desc.SampleDesc.Count = 1;

    ThrowIfFailed(m_pp_device->CreateGraphicsPipelineState(
        &pso_desc, IID_PPV_ARGS(m_shadowMapPSO.ReleaseAndGetAddressOf())));

    // NAME_D3D12_OBJECT(m_pipelineState);
}

void FrameResource::InitShadowMap()
{
    // DESCRIBE AND CREATE THE SHADOW MAP TEXTURE.
    const CD3DX12_RESOURCE_DESC shadow_tex_desc(
        D3D12_RESOURCE_DIMENSION_TEXTURE2D,
        0,
        static_cast<UINT>(m_viewPort.Width),
        static_cast<UINT>(m_viewPort.Height),
        1,
        1,
        DXGI_FORMAT_R32_TYPELESS,

        1,
        0,
        D3D12_TEXTURE_LAYOUT_UNKNOWN,
        D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL);

    D3D12_CLEAR_VALUE clear_value; // Performance tip: Tell the runtime at
                                   // resource creation the desired clear value.
    clear_value.Format = DXGI_FORMAT_D32_FLOAT;
    clear_value.DepthStencil.Depth = 1.0f;
    clear_value.DepthStencil.Stencil = 0;

    ThrowIfFailed(m_pp_device->CreateCommittedResource(
        &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
        D3D12_HEAP_FLAG_NONE,
        &shadow_tex_desc,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
        &clear_value,
        IID_PPV_ARGS(m_shadowPassShadowMap.ReleaseAndGetAddressOf())));

    // NAME_D3D12_OBJECT(m_shadowTexture);

    D3D12_DEPTH_STENCIL_VIEW_DESC depth_stencil_view_desc = {};
    depth_stencil_view_desc.Format = DXGI_FORMAT_D32_FLOAT;
    depth_stencil_view_desc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
    depth_stencil_view_desc.Texture2D.MipSlice = 0;

    // CREATE THE SHADOW MAP DEPTH STENCIL VIEW.
    m_pp_device->CreateDepthStencilView(
        m_shadowPassShadowMap.Get(),
        &depth_stencil_view_desc,
        m_dsvHeap->GetCPUDescriptorHandleForHeapStart());
    m_cpuHandleToShadowMap = m_dsvHeap->GetCPUDescriptorHandleForHeapStart();

    // CREATE THE SHADOW MAP SHADER RESOURCE VIEW.安装到heap最开始
    D3D12_SHADER_RESOURCE_VIEW_DESC shadow_srv_desc = {};
    shadow_srv_desc.Format = DXGI_FORMAT_R32_FLOAT;
    shadow_srv_desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    shadow_srv_desc.Texture2D.MipLevels = 1;
    shadow_srv_desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;

    m_currentCbvSrvUavHeapCpuHandle = m_cbvSrvUavHeapShaderVisible->GetCPUDescriptorHandleForHeapStart();
    CD3DX12_CPU_DESCRIPTOR_HANDLE shadow_map_srv_heap_handle(m_currentCbvSrvUavHeapCpuHandle);

    m_pp_device->CreateShaderResourceView(m_shadowPassShadowMap.Get(), &shadow_srv_desc, shadow_map_srv_heap_handle);
    m_currentCbvSrvUavHeapCpuHandle.Offset(1, m_rtvIncrementSize);
}

void FrameResource::InitShadowMapSampler()
{
    // Describe and create the point clamping sampler, which is
    // used for the shadow map.
    m_currentSamplerCpuHandle = m_samplerHeapShaderVisible->GetCPUDescriptorHandleForHeapStart();

    D3D12_SAMPLER_DESC clamp_sampler_desc = {};
    clamp_sampler_desc.Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
    clamp_sampler_desc.AddressU = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
    clamp_sampler_desc.AddressV = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
    clamp_sampler_desc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
    clamp_sampler_desc.MipLODBias = 0.0f;
    clamp_sampler_desc.MaxAnisotropy = 1;
    clamp_sampler_desc.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
    clamp_sampler_desc.BorderColor[0] = clamp_sampler_desc.BorderColor[1] = clamp_sampler_desc.BorderColor[2] = clamp_sampler_desc.BorderColor[3] = 0.f;
    clamp_sampler_desc.MinLOD = 0;
    clamp_sampler_desc.MaxLOD = D3D12_FLOAT32_MAX;
    m_pp_device->CreateSampler(&clamp_sampler_desc, m_currentSamplerCpuHandle);
    m_currentSamplerCpuHandle.Offset(1, m_samplerIncrementSize);
}

void FrameResource::InitScenePassPSO()
{
    // Describe and create the PSO for rendering the shadow map.
    D3D12_INPUT_LAYOUT_DESC input_layout_desc;
    input_layout_desc.pInputElementDescs = Constants::StandardVertexDescription;
    input_layout_desc.NumElements = std::size(Constants::StandardVertexDescription);

    D3D12_GRAPHICS_PIPELINE_STATE_DESC pso_desc {};
    pso_desc.InputLayout = input_layout_desc;
    pso_desc.pRootSignature = m_rootSignatureScene.Get();

    CD3DX12_DEPTH_STENCIL_DESC depth_stencil_desc(D3D12_DEFAULT);
    depth_stencil_desc.DepthEnable = true;
    depth_stencil_desc.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
    depth_stencil_desc.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
    depth_stencil_desc.StencilEnable = FALSE;

    pso_desc.VS.BytecodeLength = m_sceneVertexShader->GetBufferSize();
    pso_desc.VS.pShaderBytecode = m_sceneVertexShader->GetBufferPointer();

    pso_desc.PS.BytecodeLength = m_scenePixelShader->GetBufferSize();
    pso_desc.PS.pShaderBytecode = m_scenePixelShader->GetBufferPointer();

    auto scenePassRS = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
    pso_desc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
    pso_desc.RasterizerState = scenePassRS;

    pso_desc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
    pso_desc.DepthStencilState = depth_stencil_desc;
    pso_desc.SampleMask = UINT_MAX;
    pso_desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    pso_desc.NumRenderTargets = 1;
    pso_desc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    pso_desc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
    pso_desc.SampleDesc.Count = 1;

    ThrowIfFailed(m_pp_device->CreateGraphicsPipelineState(
        &pso_desc, IID_PPV_ARGS(m_scenePSO.ReleaseAndGetAddressOf())));

    // NAME_D3D12_OBJECT(m_pipelineState);
}

void FrameResource::InitScenePass()
{
    // Texture descriptors copy offset by 4,  前4个分别是给scene const buffer; Light const buffer; Material const; Shadow map
    InitScenePassRootSignature();
    InitScenePassShaders();
    InitDepthBuffer();
    InitRenderTargetsAndRenderTargetViews();
    InitShadowMapSampler();
    InitScenePassPSO();
}

} // namespace Anni
