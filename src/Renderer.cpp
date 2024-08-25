#include "Renderer.h"

// Renderer
namespace Anni {
Renderer::Renderer(xwin::Window& window)
    : m_GlobalFrameNum(0)
{

    initializeAPI(window);
    initializeGlobalCommands();
    initializeResources();

    // setupCommands();
    tStart = std::chrono::high_resolution_clock::now();
}

Renderer::~Renderer()
{
    //if (m_Swapchain) {
    //    m_Swapchain->SetFullscreenState(false, nullptr);
    //    m_Swapchain->Release();
    //    m_Swapchain = nullptr;
    //}

    //destroyCommands();
    //destroyFrameBuffer();
    //destroyResources();
    //destroyAPI();
}

void Renderer::initializeAPI(xwin::Window& window)
{
    initAPIWinPtrInit(window);
    initAPICreateFactory();
    initAPICreateAdapter();
    initAPICreateDevices();
    initAPICreateCommandQueues();
    initAPICreateSwapChain(window);
    initAPIDXCompiler();
    initAPICreateGlobalSyncPrimitives();
}

void Renderer::initializeFrameResources()
{
    for (auto&& [frame_index, p_frame_resource] : std::views::enumerate(m_frame_resources)) {
        p_frame_resource = std::make_unique<FrameResource>(m_Device.Get(), m_Swapchain.Get(), m_dxcUtils.Get(), m_dxcCompiler.Get(), m_includeHandler.Get(),

            m_BackBuffer, m_BackBufferRenderTargetViews, *m_sponza, *m_METAX, m_Viewport, m_ScissorRect);
    }
}

void Renderer::initializeGlobalCommands()
{
    //***********************************************************
    ThrowIfFailed(m_Device->CreateCommandAllocator(
        D3D12_COMMAND_LIST_TYPE_DIRECT,
        IID_PPV_ARGS(m_MainDirectCommandAllocator.ReleaseAndGetAddressOf())));

    ThrowIfFailed(m_Device->CreateCommandList(
        0, D3D12_COMMAND_LIST_TYPE_DIRECT, m_MainDirectCommandAllocator.Get(),
        nullptr,
        IID_PPV_ARGS(m_MainDirectCommandList.ReleaseAndGetAddressOf())));

    m_MainDirectCommandList->Close();

    //***********************************************************
    ThrowIfFailed(m_Device->CreateCommandAllocator(
        D3D12_COMMAND_LIST_TYPE_COPY,
        IID_PPV_ARGS(m_CopyCommandAllocator.ReleaseAndGetAddressOf())));

    ThrowIfFailed(m_Device->CreateCommandList(
        0, D3D12_COMMAND_LIST_TYPE_COPY, m_CopyCommandAllocator.Get(), nullptr,
        IID_PPV_ARGS(m_CopyCommandList.ReleaseAndGetAddressOf())));

    m_CopyCommandList->Close();
}

void Renderer::initializeScene() { initSceneModels(); }

void Renderer::initSceneModels()
{
    ThrowIfFailed(m_CopyCommandAllocator->Reset());
    ThrowIfFailed(m_CopyCommandList->Reset(m_CopyCommandAllocator.Get(), nullptr));

    ThrowIfFailed(m_MainDirectCommandAllocator->Reset());
    ThrowIfFailed(m_MainDirectCommandList->Reset(m_MainDirectCommandAllocator.Get(), nullptr));

    {
        const auto cwd = std::filesystem::current_path();
        const std::string working_path = cwd.string() + ("\\");

        // wchar的hlsl文件路径
        // const std::string sponza_path = working_path + R"(\assets\gltfModels\Sponza\glTF\Sponza.gltf)";
        // const std::string MATEX_path = working_path +  R"(\assets\gltfModels\METAX\untitled.gltf)";

        const std::string sponza_path = working_path + "assets\\gltfModels\\Sponza\\glTF\\Sponza.gltf";
        const std::string MATEX_path = working_path +  "assets\\gltfModels\\METAX\\untitled.gltf";

        m_sponza = std::make_unique<GltfModel>(m_Device.Get(), m_CopyCommandList.Get());
        m_METAX = std::make_unique<GltfModel>(m_Device.Get(), m_CopyCommandList.Get());

        m_sponza->LoadFromFile(sponza_path);
        //m_METAX->LoadFromFile(MATEX_path);
    }

    {
        m_CopyCommandList->Close();
        const std::array<ID3D12CommandList*, 1> lists_to_submit { m_CopyCommandList.Get() };

        m_MainCopyQueue->ExecuteCommandLists(lists_to_submit.size(), lists_to_submit.data());
        m_MainCopyQueue->Signal(m_fenceGlobal.Get(), ++m_fenceValueGlobal);

        // Check if the GPU has already completed the work
        if (m_fenceGlobal->GetCompletedValue() < m_fenceValueGlobal) {
            // Instruct the fence to signal the event when it reaches the current fence value
            ThrowIfFailed(m_fenceGlobal->SetEventOnCompletion(m_fenceValueGlobal, m_fenceEventGlobal));

            // Wait for the event (blocks until the GPU completes the work)
            WaitForSingleObject(m_fenceEventGlobal, INFINITE);
        }
    }
    // Layout transition from copy dst or common to SRV()
    // 似乎COPY QUEUE目前只支持 三种 resource states状态：copy dest，copy source， common
    {
        m_sponza->TransitionResrouceStateFromCopyToGraphics(m_MainDirectCommandList.Get());
        m_MainDirectCommandList->Close();
        const std::array<ID3D12CommandList*, 1> lists_to_submit { m_MainDirectCommandList.Get() };

        m_MainDirectQueue->ExecuteCommandLists(lists_to_submit.size(), lists_to_submit.data());
        m_MainDirectQueue->Signal(m_fenceGlobal.Get(), ++m_fenceValueGlobal);

        // Check if the GPU has already completed the work
        if (m_fenceGlobal->GetCompletedValue() < m_fenceValueGlobal) {
            // Instruct the fence to signal the event when it reaches the current fence value
            ThrowIfFailed(m_fenceGlobal->SetEventOnCompletion(m_fenceValueGlobal, m_fenceEventGlobal));

            // Wait for the event (blocks until the GPU completes the work)
            WaitForSingleObject(m_fenceEventGlobal, INFINITE);
        }
    }
}

void Renderer::destroyAPI()
{
    // this ensures all command lists are recycled
    ThrowIfFailed(m_MainDirectCommandAllocator->Reset());

#if defined(_DEBUG)
    constexpr D3D12_RLDO_FLAGS flags = D3D12_RLDO_SUMMARY | D3D12_RLDO_DETAIL | D3D12_RLDO_IGNORE_INTERNAL;

    m_DebugDevice->ReportLiveDeviceObjects(flags);
#endif
}
void Renderer::initBackBuffer()
{

    CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(m_BackBufferRtvDescHeap->GetCPUDescriptorHandleForHeapStart());
    // Create a RTV for each backbuffer.
    // Install descriptor to descriptor heap
    for (UINT n = 0; n < BACKBUFFER_COUNT; n++) {
        ThrowIfFailed(m_Swapchain->GetBuffer(
            n, IID_PPV_ARGS(m_BackBuffer[n].ReleaseAndGetAddressOf())));

        m_Device->CreateRenderTargetView(m_BackBuffer[n].Get(), nullptr, rtvHandle);
        m_BackBufferRenderTargetViews[n] = rtvHandle;
        const auto RtvDescriptorSize = m_Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
        rtvHandle.Offset(RtvDescriptorSize);
    }

    // Back buffer protection
    for (auto i = 0; i < BACKBUFFER_COUNT; i++) {
        m_fenceValuesBackBuffer[i] = 0;
        ThrowIfFailed(m_Device->CreateFence(
            m_fenceValuesBackBuffer[i], D3D12_FENCE_FLAG_NONE,
            IID_PPV_ARGS(m_fenceBackBuffer[i].ReleaseAndGetAddressOf())));

        m_fenceEventBackBuffer[i] = CreateEvent(nullptr, FALSE, FALSE, nullptr);

        if (!m_fenceEventBackBuffer[i]) {
            ThrowIfFailed(HRESULT_FROM_WIN32(GetLastError()));
        }
    }
}

void Renderer::destroyFrameBuffer()
{
    for (auto& m_RenderTarget : m_BackBuffer) {
        m_RenderTarget.Reset();
    }
    // m_BackBufferRtvDescHeap.Reset();
}

void Renderer::initializeResources()
{
    initializeScene();
    initializeFrameResources();

    //    // 1. CREATE THE ROOT SIGNATURE.
    //    {
    //        // Version shit
    //        D3D12_FEATURE_DATA_ROOT_SIGNATURE featureData = {};
    //        // This is the highest version the sample supports. If
    //        // CheckFeatureSupport succeeds, the HighestVersion returned will
    //        not be
    //        // greater than this.
    //        featureData.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_1;
    //        if
    //        (FAILED(m_Device->CheckFeatureSupport(D3D12_FEATURE_ROOT_SIGNATURE,
    //                                                 &featureData,
    //                                                 sizeof(featureData))))
    //        {
    //            featureData.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_0;
    //        }
    //
    //        // 描述descriptor table中的一段范围
    //        D3D12_DESCRIPTOR_RANGE1 ranges[1];
    //        ranges[0].BaseShaderRegister = 0; // b0
    //        ranges[0].RangeType =
    //            D3D12_DESCRIPTOR_RANGE_TYPE_CBV; //
    //            只能存放一种特定类型的描述符,can
    //                                             // only be set to a single
    //                                             type at
    //                                             // a time
    //        ranges[0].NumDescriptors = 1;
    //        ranges[0].RegisterSpace = 0; // space0
    //        ranges[0].OffsetInDescriptorsFromTableStart =
    //            0; // 填入 某个descriptor table（算一个root
    //               // parameter）中的范围，在descriptor table中的的起始索引
    //        ranges[0].Flags = D3D12_DESCRIPTOR_RANGE_FLAG_NONE;
    //
    //        D3D12_ROOT_PARAMETER1 rootParameters[1];
    //        rootParameters[0].ParameterType =
    //            D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE; // a descriptor
    //            table in
    //                                                        // Direct3D 12
    //                                                        cannot
    //                                                        // contain
    //                                                        different
    //                                                        // types of
    //                                                        descriptors.
    //        rootParameters[0].ShaderVisibility =
    //        D3D12_SHADER_VISIBILITY_VERTEX;
    //        rootParameters[0].DescriptorTable.NumDescriptorRanges = 1;
    //        rootParameters[0].DescriptorTable.pDescriptorRanges = ranges;
    //
    //        D3D12_VERSIONED_ROOT_SIGNATURE_DESC rootSignatureDesc;
    //        rootSignatureDesc.Version = D3D_ROOT_SIGNATURE_VERSION_1_1;
    //        rootSignatureDesc.Desc_1_1.Flags =
    //            D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
    //        rootSignatureDesc.Desc_1_1.NumParameters = 1;
    //        rootSignatureDesc.Desc_1_1.pParameters = rootParameters;
    //        rootSignatureDesc.Desc_1_1.NumStaticSamplers = 0;
    //        rootSignatureDesc.Desc_1_1.pStaticSamplers = nullptr;
    //
    //        WRL::ComPtr<ID3DBlob> signature;
    //        WRL::ComPtr<ID3DBlob> error;
    //        try
    //        {
    //            ThrowIfFailed(D3D12SerializeVersionedRootSignature(
    //                &rootSignatureDesc, signature.ReleaseAndGetAddressOf(),
    //                error.ReleaseAndGetAddressOf())); // It converts the root
    //                                                  // signature definition
    //                                                  (which
    //                                                  // includes descriptor
    //                                                  tables,
    //                                                  // root parameters, and
    //                                                  static
    //                                                  // samplers) into a byte
    //                                                  array
    //                                                  // that can be loaded by
    //                                                  the
    //                                                  // GPU later.
    //            ThrowIfFailed(m_Device->CreateRootSignature(
    //                0, signature->GetBufferPointer(),
    //                signature->GetBufferSize(),
    //                IID_PPV_ARGS(m_RootSignature.ReleaseAndGetAddressOf())));
    //            m_RootSignature->SetName(L"Hello Triangle Root Signature");
    //        }
    //        catch (std::exception& e)
    //        {
    //            const char* errStr = (const char*)error->GetBufferPointer();
    //            std::cout << errStr;
    //        }
    //    }
    //
    //    // 2. CREATE THE PSO.
    //    // Create the pipeline state, which includes compiling and loading
    //    shaders.
    //    {
    //        WRL::ComPtr<ID3DBlob> vertex_shader;
    //        WRL::ComPtr<ID3DBlob> pixel_shader;
    //        WRL::ComPtr<ID3DBlob> errors;
    //
    // #if defined(_DEBUG)
    //        // Enable better shader debugging with the graphics debugging
    //        tools. UINT compile_flags = D3DCOMPILE_DEBUG |
    //        D3DCOMPILE_SKIP_OPTIMIZATION;
    // #else
    //        UINT compile_flags = 0;
    // #endif
    //
    //        std::string path{};
    //        char pBuf[1024];
    //
    //        _getcwd(pBuf, 1024);
    //        path = pBuf;
    //        path += "\\";
    //        std::wstring wpath = std::wstring(path.begin(), path.end());
    //
    //        // dxbc是编译以后的中间形式的代码
    //        std::string vert_compiled_path = path;
    //        std::string frag_compiled_path = path;
    //
    //        vert_compiled_path += "assets\\triangle.vert.dxbc";
    //        frag_compiled_path += "assets\\triangle.frag.dxbc";
    //
    //        // 编译hlsl文件到dxbc
    // #define COMPILESHADERS
    // #ifdef COMPILESHADERS
    //        // hlsl文件路径
    //        std::wstring vert_path = wpath + L"assets\\triangle.vert.hlsl";
    //        std::wstring frag_path = wpath + L"assets\\triangle.frag.hlsl";
    //
    //        try
    //        {
    //            ThrowIfFailed(D3DCompileFromFile(
    //                vert_path.c_str(), nullptr, nullptr, "main", "vs_5_0",
    //                compile_flags, 0, vertex_shader.ReleaseAndGetAddressOf(),
    //                errors.ReleaseAndGetAddressOf()));
    //            ThrowIfFailed(D3DCompileFromFile(
    //                frag_path.c_str(), nullptr, nullptr, "main", "ps_5_0",
    //                compile_flags, 0, pixel_shader.ReleaseAndGetAddressOf(),
    //                errors.ReleaseAndGetAddressOf()));
    //        }
    //        catch (std::exception& e)
    //        {
    //            const char* errStr = (const char*)errors->GetBufferPointer();
    //            std::cout << errStr;
    //        }
    //
    //        std::ofstream vs_out(vert_compiled_path,std::ios::out |
    //        std::ios::binary); std::ofstream
    //        fs_out(frag_compiled_path,std::ios::out | std::ios::binary);
    //
    //        vs_out.write((const char*)vertex_shader->GetBufferPointer(),
    //                     vertex_shader->GetBufferSize());
    //        fs_out.write((const char*)pixel_shader->GetBufferPointer(),
    //                     pixel_shader->GetBufferSize());
    //
    // #else
    //        std::vector<char> vs_byte_code_data =
    //            ReadFileAsBytes(vert_compiled_path); // dxbc格式的文件
    //        std::vector<char> fs_byte_code_data =
    //        ReadFileAsBytes(frag_compiled_path);
    //
    // #endif
    //
    //        // 3. DEFINE THE VERTEX INPUT LAYOUT.
    //        D3D12_INPUT_ELEMENT_DESC inputElementDescs[] = {
    //            {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,
    //             D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
    //            {"COLOR", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12,
    //             D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0}};
    //
    //        // 4. CREATE THE UBO.
    //        {
    //            // Note: using upload heaps to transfer static data like vert
    //            // buffers is not recommended. Every time the GPU needs it,
    //            the
    //            // upload heap will be marshalled over. Please read up on
    //            Default
    //            // Heap usage. An upload heap is used here for code simplicity
    //            and
    //            // because there are very few verts to actually transfer.
    //
    //            // 4.1 the description of heap used for mem allocation of
    //            actual
    //            // resource(uniform buffer, upload from cpu to gpu)
    //            D3D12_HEAP_PROPERTIES heapProps;
    //            heapProps.Type = D3D12_HEAP_TYPE_UPLOAD;
    //            heapProps.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    //            heapProps.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
    //            heapProps.CreationNodeMask = 1;
    //            heapProps.VisibleNodeMask = 1;
    //
    //            // 4.2 create descriptor heap(once bound, will be just in GPU
    //            // memory, it's D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE) for
    //            CBV
    //            // SRV UAV
    //            D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
    //            heapDesc.NumDescriptors = 1;
    //            heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    //            heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    //            ThrowIfFailed(m_Device->CreateDescriptorHeap(
    //                &heapDesc,
    //                IID_PPV_ARGS(
    //                    m_UniformBufferDescHeap.ReleaseAndGetAddressOf())));
    //
    //            // 4.5 create UBO
    //            D3D12_RESOURCE_DESC uboResourceDesc;
    //            uboResourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    //            uboResourceDesc.Alignment = 0;
    //            uboResourceDesc.Width = (sizeof(uboVS) + 255) & ~255;
    //            uboResourceDesc.Height = 1;
    //            uboResourceDesc.DepthOrArraySize = 1;
    //            uboResourceDesc.MipLevels = 1;
    //            uboResourceDesc.Format = DXGI_FORMAT_UNKNOWN;
    //            uboResourceDesc.SampleDesc.Count = 1;
    //            uboResourceDesc.SampleDesc.Quality = 0;
    //            uboResourceDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    //            uboResourceDesc.Flags = D3D12_RESOURCE_FLAG_NONE;
    //
    //            ThrowIfFailed(m_Device->CreateCommittedResource(
    //                &heapProps, D3D12_HEAP_FLAG_NONE, &uboResourceDesc,
    //                D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
    //                IID_PPV_ARGS(m_UniformBuffer.ReleaseAndGetAddressOf())));
    //            m_UniformBufferDescHeap->SetName(
    //                L"Constant Buffer Upload Resource Heap");
    //
    //            // 4.6 create const buffer view
    //            D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc = {};
    //            cbvDesc.BufferLocation =
    //            m_UniformBuffer->GetGPUVirtualAddress(); cbvDesc.SizeInBytes =
    //                (sizeof(uboVS) + 255) &
    //                ~255; // CB size is required to be 256-byte aligned.
    //
    //            // 4.7 Install descriptor in descriptor heap
    //            D3D12_CPU_DESCRIPTOR_HANDLE cbvHandle(
    //                m_UniformBufferDescHeap->GetCPUDescriptorHandleForHeapStart());
    //            cbvHandle.ptr =
    //                cbvHandle.ptr +
    //                m_Device->GetDescriptorHandleIncrementSize(
    //                                    D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV)
    //                                    * 0;
    //            m_Device->CreateConstantBufferView(&cbvDesc, cbvHandle);
    //
    //            // 4.8 CPU to GPU buffer transfer
    //            // We do not intend to read from this resource on the CPU.
    //            (End is
    //            // less than or equal to begin)
    //            D3D12_RANGE readRange;
    //            readRange.Begin = 0;
    //            readRange.End = 0;
    //
    //            ThrowIfFailed(m_UniformBuffer->Map(
    //                0, &readRange,
    //                reinterpret_cast<void**>(&m_MappedUniformBuffer)));
    //            memcpy(m_MappedUniformBuffer, &uboVS, sizeof(uboVS));
    //            m_UniformBuffer->Unmap(0, &readRange);
    //        }
    //
    //        // 5. DESCRIBE AND CREATE THE GRAPHICS PIPELINE STATE OBJECT
    //        (PSO). D3D12_GRAPHICS_PIPELINE_STATE_DESC pso_desc = {};
    //        pso_desc.InputLayout = {inputElementDescs,
    //        _countof(inputElementDescs)}; pso_desc.pRootSignature =
    //        m_RootSignature.Get();
    //
    //        D3D12_SHADER_BYTECODE vs_bytecode;
    //        D3D12_SHADER_BYTECODE ps_bytecode;
    //
    // #ifdef COMPILESHADERS
    //        vs_bytecode.pShaderBytecode = vertex_shader->GetBufferPointer();
    //        vs_bytecode.BytecodeLength = vertex_shader->GetBufferSize();
    //
    //        ps_bytecode.pShaderBytecode = pixel_shader->GetBufferPointer();
    //        ps_bytecode.BytecodeLength = pixel_shader->GetBufferSize();
    // #else
    //        vsBytecode.pShaderBytecode = vs_byte_code_data.data();
    //        vsBytecode.BytecodeLength = vs_byte_code_Data.size();
    //
    //        psBytecode.pShaderBytecode = fs_byte_code_data.data();
    //        psBytecode.BytecodeLength = fs_byte_code_data.size();
    // #endif
    //
    //        pso_desc.VS = vs_bytecode;
    //        pso_desc.PS = ps_bytecode;
    //
    //        D3D12_RASTERIZER_DESC raster_desc;
    //        raster_desc.FillMode = D3D12_FILL_MODE_SOLID;
    //        raster_desc.CullMode = D3D12_CULL_MODE_NONE;
    //        raster_desc.FrontCounterClockwise = FALSE;
    //        raster_desc.DepthBias = D3D12_DEFAULT_DEPTH_BIAS;
    //        raster_desc.DepthBiasClamp = D3D12_DEFAULT_DEPTH_BIAS_CLAMP;
    //        raster_desc.SlopeScaledDepthBias =
    //            D3D12_DEFAULT_SLOPE_SCALED_DEPTH_BIAS;
    //        raster_desc.DepthClipEnable = TRUE;
    //        raster_desc.MultisampleEnable = FALSE;
    //        raster_desc.AntialiasedLineEnable = FALSE;
    //        raster_desc.ForcedSampleCount = 0;
    //        raster_desc.ConservativeRaster =
    //            D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF;
    //
    //        pso_desc.RasterizerState = raster_desc;
    //
    //        D3D12_BLEND_DESC blend_desc;
    //        blend_desc.AlphaToCoverageEnable = FALSE;
    //        blend_desc.IndependentBlendEnable = FALSE;
    //        constexpr D3D12_RENDER_TARGET_BLEND_DESC
    //            default_render_target_blend_desc = {
    //                FALSE,
    //                FALSE,
    //                D3D12_BLEND_ONE,
    //                D3D12_BLEND_ZERO,
    //                D3D12_BLEND_OP_ADD,
    //                D3D12_BLEND_ONE,
    //                D3D12_BLEND_ZERO,
    //                D3D12_BLEND_OP_ADD,
    //                D3D12_LOGIC_OP_NOOP,
    //                D3D12_COLOR_WRITE_ENABLE_ALL,
    //            };
    //        for (UINT i = 0; i < D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT; ++i)
    //        {
    //            blend_desc.RenderTarget[i] = default_render_target_blend_desc;
    //        }
    //
    //        pso_desc.BlendState = blend_desc;
    //        pso_desc.DepthStencilState.DepthEnable = FALSE;
    //        pso_desc.DepthStencilState.StencilEnable = FALSE;
    //        pso_desc.SampleMask = UINT_MAX;
    //        pso_desc.PrimitiveTopologyType =
    //        D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE; pso_desc.NumRenderTargets
    //        = 1; pso_desc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    //        pso_desc.SampleDesc.Count = 1;
    //        try
    //        {
    //            ThrowIfFailed(m_Device->CreateGraphicsPipelineState(
    //                &pso_desc,
    //                IID_PPV_ARGS(m_PipelineState.ReleaseAndGetAddressOf())));
    //        }
    //        catch (std::exception& e)
    //        {
    //            std::cout << "Failed to create Graphics Pipeline!" << e.what()
    //                      << std::endl;
    //        }
    //
    //        if (vertex_shader)
    //        {
    //            vertex_shader.Reset();
    //        }
    //
    //        if (pixel_shader)
    //        {
    //            pixel_shader.Reset();
    //        }
    //    }
    //
    //    // 6. create command list(command buffer) and record
    //    createCommandList();
    //
    //    // Command lists are created in the recording state, but there is
    //    nothing
    //    // to record yet. The main loop expects it to be closed, so close it
    //    now. ThrowIfFailed(m_MainDirectCommandList->Close());
    //
    //    // Create the vertex buffer.
    //    {
    //        constexpr UINT vertex_buffer_size = sizeof(m_VertexBufferData);
    //
    //        // Note: using upload heaps to transfer static data like vert
    //        buffers is
    //        // not recommended. Every time the GPU needs it, the upload heap
    //        will be
    //        // marshalled over. Please read up on Default Heap usage. An
    //        upload heap
    //        // is used here for code simplicity and because there are very few
    //        verts
    //        // to actually transfer.
    //        D3D12_HEAP_PROPERTIES heap_props;
    //        heap_props.Type = D3D12_HEAP_TYPE_UPLOAD;
    //        heap_props.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    //        heap_props.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
    //        heap_props.CreationNodeMask = 1;
    //        heap_props.VisibleNodeMask = 1;
    //
    //        D3D12_RESOURCE_DESC vertex_buffer_resource_desc;
    //        vertex_buffer_resource_desc.Dimension =
    //        D3D12_RESOURCE_DIMENSION_BUFFER;
    //        vertex_buffer_resource_desc.Alignment = 0;
    //        vertex_buffer_resource_desc.Width = vertex_buffer_size;
    //        vertex_buffer_resource_desc.Height = 1;
    //        vertex_buffer_resource_desc.DepthOrArraySize = 1;
    //        vertex_buffer_resource_desc.MipLevels = 1;
    //        vertex_buffer_resource_desc.Format = DXGI_FORMAT_UNKNOWN;
    //        vertex_buffer_resource_desc.SampleDesc.Count = 1;
    //        vertex_buffer_resource_desc.SampleDesc.Quality = 0;
    //        vertex_buffer_resource_desc.Layout =
    //        D3D12_TEXTURE_LAYOUT_ROW_MAJOR; vertex_buffer_resource_desc.Flags
    //        = D3D12_RESOURCE_FLAG_NONE;
    //
    //        ThrowIfFailed(m_Device->CreateCommittedResource(
    //            &heap_props, D3D12_HEAP_FLAG_NONE,
    //            &vertex_buffer_resource_desc,
    //            D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
    //            IID_PPV_ARGS(m_VertexBuffer.GetAddressOf())));
    //
    //        // Copy the triangle data to the vertex buffer.
    //        UINT8* p_vertex_data_begin;
    //
    //        // We do not intend to read from this resource on the CPU.
    //        D3D12_RANGE read_range;
    //        read_range.Begin = 0;
    //        read_range.End = 0;
    //
    //        ThrowIfFailed(m_VertexBuffer->Map(
    //            0, &read_range,
    //            reinterpret_cast<void**>(&p_vertex_data_begin)));
    //        memcpy(p_vertex_data_begin, m_VertexBufferData,
    //               sizeof(m_VertexBufferData));
    //        m_VertexBuffer->Unmap(0, nullptr);
    //
    //        // Initialize the vertex buffer view.
    //        m_VertexBufferView.BufferLocation =
    //            m_VertexBuffer->GetGPUVirtualAddress();
    //        m_VertexBufferView.StrideInBytes = sizeof(Vertex);
    //        m_VertexBufferView.SizeInBytes = vertex_buffer_size;
    //    }
    //
    //    // Create the index buffer.
    //    {
    //        constexpr UINT index_buffer_size = sizeof(m_IndexBufferData);
    //
    //        // Note: using upload heaps to transfer static data like vert
    //        buffers is
    //        // not recommended. Every time the GPU needs it, the upload heap
    //        will be
    //        // marshalled over. Please read up on Default Heap usage. An
    //        upload heap
    //        // is used here for code simplicity and because there are very few
    //        verts
    //        // to actually transfer.
    //
    //        D3D12_HEAP_PROPERTIES heap_props;
    //        heap_props.Type = D3D12_HEAP_TYPE_UPLOAD;
    //        heap_props.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    //        heap_props.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
    //        heap_props.CreationNodeMask = 1;
    //        heap_props.VisibleNodeMask = 1;
    //
    //        D3D12_RESOURCE_DESC index_buffer_resource_desc;
    //        index_buffer_resource_desc.Dimension =
    //        D3D12_RESOURCE_DIMENSION_BUFFER;
    //        index_buffer_resource_desc.Alignment = 0;
    //        index_buffer_resource_desc.Width = index_buffer_size;
    //        index_buffer_resource_desc.Height = 1;
    //        index_buffer_resource_desc.DepthOrArraySize = 1;
    //        index_buffer_resource_desc.MipLevels = 1;
    //        index_buffer_resource_desc.Format = DXGI_FORMAT_UNKNOWN;
    //        index_buffer_resource_desc.SampleDesc.Count = 1;
    //        index_buffer_resource_desc.SampleDesc.Quality = 0;
    //        index_buffer_resource_desc.Layout =
    //        D3D12_TEXTURE_LAYOUT_ROW_MAJOR; index_buffer_resource_desc.Flags =
    //        D3D12_RESOURCE_FLAG_NONE;
    //
    //        ThrowIfFailed(m_Device->CreateCommittedResource(
    //            &heap_props, D3D12_HEAP_FLAG_NONE,
    //            &index_buffer_resource_desc,
    //            D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
    //            IID_PPV_ARGS(m_IndexBuffer.GetAddressOf())));
    //
    //        // Copy the triangle data to the index buffer.
    //        UINT8* p_index_data_begin;
    //
    //        // We do not intend to read from this resource on the CPU.
    //        D3D12_RANGE read_range;
    //        read_range.Begin = 0;
    //        read_range.End = 0;
    //
    //        ThrowIfFailed(m_IndexBuffer->Map(
    //            0, &read_range,
    //            reinterpret_cast<void**>(&p_index_data_begin)));
    //        memcpy(p_index_data_begin, m_IndexBufferData,
    //               sizeof(m_IndexBufferData));
    //        m_IndexBuffer->Unmap(0, nullptr);
    //
    //        // Initialize the index buffer view.
    //        m_IndexBufferView.BufferLocation =
    //            m_IndexBuffer->GetGPUVirtualAddress();
    //        m_IndexBufferView.Format = DXGI_FORMAT_R32_UINT;
    //        m_IndexBufferView.SizeInBytes = index_buffer_size;
    //    }
    //
    //    // Create synchronization objects and wait until assets have been
    //    uploaded
    //    // to the GPU.
    //    {
    //        // m_FenceValue = 1;
    //
    //        // Create an event handle to use for frame synchronization.
    //        m_FenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    //        if (m_FenceEvent == nullptr)
    //        {
    //            ThrowIfFailed(HRESULT_FROM_WIN32(GetLastError()));
    //        }
    //
    //        // Wait for the command list to execute; we are reusing the same
    //        command
    //        // list in our main loop but for now, we just want to wait for
    //        setup to
    //        // complete before continuing.
    //        // Signal and increment the fence value.
    //
    //        const UINT64 fence = m_FenceValue;
    //        ThrowIfFailed(m_MainDirectQueue->Signal(m_Fence.Get(), fence));
    //        m_FenceValue++;
    //
    //        // Wait until the previous frame is finished.
    //        if (m_Fence->GetCompletedValue() < fence)
    //        {
    //            ThrowIfFailed(m_Fence->SetEventOnCompletion(fence,
    //            m_FenceEvent)); WaitForSingleObject(m_FenceEvent, INFINITE);
    //        }
    //
    //        m_FrameIndex = m_Swapchain->GetCurrentBackBufferIndex();
    //    }
}

void Renderer::destroyResources()
{
    //// Sync
    // CloseHandle(m_FenceEvent);
}

// void Renderer::createCommandList()
//{
//     // Create the command list.
//     ThrowIfFailed(m_Device->CreateCommandList(
//         0, D3D12_COMMAND_LIST_TYPE_DIRECT, m_MainDirectCommandAllocator.Get(),
//         nullptr,
//         IID_PPV_ARGS(m_MainDirectCommandList.ReleaseAndGetAddressOf())));
//     m_MainDirectCommandList->SetName(L"Main Direct Command List");
// }

// void Renderer::setupCommands()
//{
//     // Command list allocators can only be reset when the associated
//     // command lists have finished execution on the GPU; apps should use fences
//     // to determine GPU execution progress.
//     ThrowIfFailed(m_MainDirectCommandAllocator->Reset());
//
//     // However, when ExecuteCommandList() is called on a particular command
//     // list, that command list can then be reset at any time and must be before
//     // re-recording.
//     ThrowIfFailed(m_MainDirectCommandList->Reset(
//         m_MainDirectCommandAllocator.Get(), m_PipelineState.Get()));
//
//     // Set necessary state.
//     m_MainDirectCommandList->SetGraphicsRootSignature(m_RootSignature.Get());
//     m_MainDirectCommandList->RSSetViewports(1, &m_Viewport);
//     m_MainDirectCommandList->RSSetScissorRects(1, &m_ScissorRect);
//
//     ID3D12DescriptorHeap* pDescriptorHeaps[] = { m_UniformBufferDescHeap.Get() };
//     m_MainDirectCommandList->SetDescriptorHeaps(_countof(pDescriptorHeaps),
//         pDescriptorHeaps);
//
//     const D3D12_GPU_DESCRIPTOR_HANDLE srvHandle(
//         m_UniformBufferDescHeap->GetGPUDescriptorHandleForHeapStart());
//     m_MainDirectCommandList->SetGraphicsRootDescriptorTable(0, srvHandle);
//
//     // Indicate that the back buffer will be used as a OnRender target.
//     D3D12_RESOURCE_BARRIER render_target_barrier;
//     render_target_barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
//     render_target_barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
//     render_target_barrier.Transition.pResource = m_BackBuffer[m_FrameIndex].Get();
//     render_target_barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
//     render_target_barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
//     render_target_barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
//
//     m_MainDirectCommandList->ResourceBarrier(1, &render_target_barrier);
//
//     D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle(
//         m_BackBufferRtvDescHeap->GetCPUDescriptorHandleForHeapStart());
//     rtvHandle.ptr = rtvHandle.ptr + (m_FrameIndex * m_RtvDescriptorSize);
//     m_MainDirectCommandList->OMSetRenderTargets(1, &rtvHandle, FALSE, nullptr);
//
//     // Record commands.
//     const float clearColor[] = { 0.2f, 0.2f, 0.2f, 1.0f };
//     m_MainDirectCommandList->ClearRenderTargetView(rtvHandle, clearColor, 0,
//         nullptr);
//     m_MainDirectCommandList->IASetPrimitiveTopology(
//         D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
//     m_MainDirectCommandList->IASetVertexBuffers(0, 1, &m_VertexBufferView);
//     m_MainDirectCommandList->IASetIndexBuffer(&m_IndexBufferView);
//
//     m_MainDirectCommandList->DrawIndexedInstanced(3, 1, 0, 0, 0);
//
//     // Indicate that the back buffer will now be used to present.
//     D3D12_RESOURCE_BARRIER presentBarrier;
//     presentBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
//     presentBarrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
//     presentBarrier.Transition.pResource = m_BackBuffer[m_FrameIndex].Get();
//     presentBarrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
//     presentBarrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
//     presentBarrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
//
//     m_MainDirectCommandList->ResourceBarrier(1, &presentBarrier);
//
//     ThrowIfFailed(m_MainDirectCommandList->Close());
// }

void Renderer::destroyCommands()
{


    // if (m_MainDirectCommandList) {
    //     m_MainDirectCommandList->Reset(m_MainDirectCommandAllocator.Get(),
    //         m_PipelineState.Get());
    //     m_MainDirectCommandList->ClearState(m_PipelineState.Get());
    //     ThrowIfFailed(m_MainDirectCommandList->Close());
    //     ID3D12CommandList* ppCommandLists[] = { m_MainDirectCommandList.Get() };
    //     m_MainDirectQueue->ExecuteCommandLists(_countof(ppCommandLists),
    //         ppCommandLists);

    //    // Wait for GPU to finish work
    //    // const UINT64 fence = m_FenceValue;
    //    // ThrowIfFailed(m_MainDirectQueue->Signal(m_Fence.Get(), fence));
    //    // m_FenceValue++;
    //    // if (m_Fence->GetCompletedValue() < fence)
    //    //{
    //    //    ThrowIfFailed(m_Fence->SetEventOnCompletion(fence, m_FenceEvent));
    //    //    WaitForSingleObject(m_FenceEvent, INFINITE);
    //    //}

    //    m_MainDirectCommandList.Reset();
    //}
}

void Renderer::setupSwapchain(unsigned width, unsigned height)
{

    m_ScissorRect.left = 0;
    m_ScissorRect.top = 0;
    m_ScissorRect.right = static_cast<LONG>(m_Width);
    m_ScissorRect.bottom = static_cast<LONG>(m_Height);

    m_Viewport.TopLeftX = 0.0f;
    m_Viewport.TopLeftY = 0.0f;
    m_Viewport.Width = static_cast<float>(m_Width);
    m_Viewport.Height = static_cast<float>(m_Height);

    m_Viewport.MinDepth = 0.f;
    m_Viewport.MaxDepth = 1.f;

    if (m_Swapchain) {
        m_Swapchain->ResizeBuffers(BACKBUFFER_COUNT, m_Width, m_Height,
            BACKBUFFER_FORMAT, 0);
    } else {
        DXGI_SWAP_CHAIN_DESC1 swapchainDesc = {};
        swapchainDesc.BufferCount = BACKBUFFER_COUNT;
        swapchainDesc.Width = width;
        swapchainDesc.Height = height;
        swapchainDesc.Format = BACKBUFFER_FORMAT;
        swapchainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        swapchainDesc.SwapEffect = SWAP_CHAIN_SWAP_EFFECT;
        swapchainDesc.SampleDesc.Count = 1;
        swapchainDesc.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING;
        // Swap chain needs the queue so that it can force a flush on it.
        WRL::ComPtr<IDXGISwapChain1> swapchain = xgfx::createSwapchain(
            m_Window, m_Factory.Get(), m_MainDirectQueue.Get(), &swapchainDesc);
        const HRESULT swapchain_support = swapchain->QueryInterface(
            __uuidof(IDXGISwapChain3), (void**)swapchain.GetAddressOf());

        if (SUCCEEDED(swapchain_support)) {
            ThrowIfFailed(swapchain.As(&m_Swapchain));
        }
    }
}

void Renderer::OnReSize(unsigned width, unsigned height)
{

    m_Width = std::clamp(width, 1u, 0xffffu);
    m_Height = std::clamp(height, 1u, 0xffffu);

    // WAITING FOR THE FRAME TO COMPLETE BEFORE CONTINUING IS NOT BEST PRACTICE.
    // This is code implemented as such for simplicity. The
    // D3D12HelloFrameBuffering sample illustrates how to use fences for
    // efficient resource usage and to maximize GPU utilization.

    // Signal and increment the fence value.
    // const UINT64 fence = m_FenceValue; // fence is 1
    // ThrowIfFailed(m_MainDirectQueue->Signal(m_Fence.Get(), fence));
    // m_FenceValue++; // fence value is 2

    //// Wait until the previous frame is finished.
    // if (m_Fence->GetCompletedValue() < fence) // 0
    //{
    //     ThrowIfFailed(m_Fence->SetEventOnCompletion(fence, m_FenceEvent));
    //     WaitForSingleObjectEx(m_FenceEvent, INFINITE, false);
    // }

    destroyFrameBuffer();
    setupSwapchain(width, height);
    initBackBuffer();
}

void Renderer::OnUpdateGlobal(const std::vector<xwin::KeyboardData>& keyboard_data)
{
    for (const auto& one_frame : m_frame_resources) {
        one_frame->OnUpdateGlobalState(keyboard_data);
    }
}

void Renderer::initAPICreateFactory()
{
    // CREATE FACTORY 创建工厂
    UINT dxgi_factory_flags = 0;
#if defined(_DEBUG)
    WRL::ComPtr<ID3D12Debug> debug_controller;
    ThrowIfFailed(D3D12GetDebugInterface(
        IID_PPV_ARGS(debug_controller.ReleaseAndGetAddressOf())));
    ThrowIfFailed(debug_controller.As(&m_DebugController));

    assert(m_DebugController);
    m_DebugController->EnableDebugLayer();
    m_DebugController->SetEnableGPUBasedValidation(true);

    dxgi_factory_flags |= DXGI_CREATE_FACTORY_DEBUG;
    debug_controller.Reset();

#endif
    ThrowIfFailed(CreateDXGIFactory2(
        dxgi_factory_flags, IID_PPV_ARGS(m_Factory.ReleaseAndGetAddressOf())));
}

void Renderer::initAPICreateAdapter()
{
    // CREATE ADAPTER 创建
    for (UINT adapter_index = 0;
         DXGI_ERROR_NOT_FOUND != m_Factory->EnumAdapters1(adapter_index, m_Adapter.ReleaseAndGetAddressOf());
         ++adapter_index) {
        DXGI_ADAPTER_DESC1 desc_adaptor;
        m_Adapter->GetDesc1(&desc_adaptor);

        if (desc_adaptor.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) {
            // Don't select the Basic Render Driver adapter.
            continue;
        }

        // Check to see if the adapter supports Direct3D 12, but don't create
        // the actual device yet.
        if (SUCCEEDED(D3D12CreateDevice(m_Adapter.Get(), D3D_FEATURE_LEVEL_12_0,
                _uuidof(ID3D12Device), nullptr))) {
            break;
        }
        // if we won't use this adapter, it'll be released at the start of the
        // next loop by "ReleaseAndGetAddressOf" function.
    }
}

void Renderer::initAPICreateDevices()
{

    // CREATE DEVICE
    ThrowIfFailed(
        D3D12CreateDevice(m_Adapter.Get(), D3D_FEATURE_LEVEL_12_0,
            IID_PPV_ARGS(m_Device.ReleaseAndGetAddressOf())));
    m_Device->SetName(L"Anni Device");

#if defined(_DEBUG)
    // Get debug device
    ThrowIfFailed(m_Device.As(&m_DebugDevice));
#endif
    assert(m_DebugDevice);
}

void Renderer::initAPICreateCommandQueues()
{
    // CREATE DIRECT COMMAND QUEUE
    D3D12_COMMAND_QUEUE_DESC queue_desc = {};
    queue_desc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
    queue_desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;

    ThrowIfFailed(m_Device->CreateCommandQueue(
        &queue_desc, IID_PPV_ARGS(m_MainDirectQueue.ReleaseAndGetAddressOf())));

    // CREATE COPY COMMAND QUEUE(only common, cpy dest, cpy src states supported on copy queue)
    queue_desc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
    queue_desc.Type = D3D12_COMMAND_LIST_TYPE_COPY;

    ThrowIfFailed(m_Device->CreateCommandQueue(
        &queue_desc, IID_PPV_ARGS(m_MainCopyQueue.ReleaseAndGetAddressOf())));
}

// void Renderer::initAPICreateCommandAllocator()
//{
//     // CREATE COMMAND ALLOCATOR
//     ThrowIfFailed(m_Device->CreateCommandAllocator(
//         D3D12_COMMAND_LIST_TYPE_DIRECT,
//         IID_PPV_ARGS(m_MainDirectCommandAllocator.ReleaseAndGetAddressOf())));
// }
//
void Renderer::initAPICreateGlobalSyncPrimitives()
{
    // SYNC PRIMITIVES
    ThrowIfFailed(m_Device->CreateFence(
        m_fenceValueGlobal, D3D12_FENCE_FLAG_NONE,
        IID_PPV_ARGS(m_fenceGlobal.ReleaseAndGetAddressOf())));

    // Create a Win32 event
    m_fenceEventGlobal = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    if (m_fenceEventGlobal == nullptr) {
        throw std::runtime_error("Failed to create fence event");
    }
}

void Renderer::initAPICreateSwapChain(xwin::Window& window)
{
    // CREATE SWAPCHAIN
    const xwin::WindowDesc wdesc = window.getDesc();

    m_Width = std::clamp(wdesc.width, 1u, 0xffffu);
    m_Height = std::clamp(wdesc.height, 1u, 0xffffu);

    setupSwapchain(m_Width, m_Height);
    createBackBufferRTVDescriptorHeap();
    initBackBuffer();
}

void Renderer::initAPIDXCompiler()
{

    ThrowIfFailed(DxcCreateInstance(CLSID_DxcUtils, IID_PPV_ARGS(&m_dxcUtils)));
    ThrowIfFailed(DxcCreateInstance(CLSID_DxcCompiler, IID_PPV_ARGS(&m_dxcCompiler)));
    ThrowIfFailed(m_dxcUtils->CreateDefaultIncludeHandler(&m_includeHandler));
}

void Renderer::createBackBufferRTVDescriptorHeap()
{
    // Create descriptor heap for back buffer.
    // Describe and create a Render target view (RTV) descriptor heap.
    D3D12_DESCRIPTOR_HEAP_DESC rtv_heap_desc = {};
    rtv_heap_desc.NumDescriptors = BACKBUFFER_COUNT;
    rtv_heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    rtv_heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    ThrowIfFailed(m_Device->CreateDescriptorHeap(
        &rtv_heap_desc, IID_PPV_ARGS(m_BackBufferRtvDescHeap.ReleaseAndGetAddressOf())));
}

void Renderer::initAPIWinPtrInit(xwin::Window& window)
{
    // WINDOW POINTER INIT (The renderer needs the window when resizing the
    // swapchain) window指针初始化
    m_Window = &window;
}

void Renderer::OnRender()
{
    tEnd = std::chrono::high_resolution_clock::now();
    const float time_elapsed = std::chrono::duration<float, std::milli>(tEnd - tStart).count();
    tStart = std::chrono::high_resolution_clock::now();

    m_frame_resources[m_GlobalFrameNum % FRAME_INFLIGHT_COUNT]->RecordCommandsAndExecute(m_MainDirectQueue.Get());

    ++m_GlobalFrameNum;
}

//
}