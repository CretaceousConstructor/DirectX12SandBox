#include "GltfModel.h"
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION

#include "stb_image.h"

namespace Anni {

void GltfModel::Draw(const glm::mat4& top_matrix, DrawContext& ctx)
{
    // create renderables from the scenenodes
    for (const auto& n : m_topNodes) {
        n->Draw(top_matrix, ctx);
    }
}

UINT32 GltfModel::GetNumberOfSamplers() const
{
    return m_num_samplers;
}

UINT32 GltfModel::GetNumberOfTextures() const
{
    return m_texturesImages.size();
}

UINT32 GltfModel::GetNumberOfMaterial() const
{
    return m_num_material_views;
}

UINT32 GltfModel::GetNumberOfMatricesRenderObjects() const
{
    return m_draw_ctx.OpaqueSurfaces.size();
}

CD3DX12_GPU_DESCRIPTOR_HANDLE GltfModel::GetGPUDescHandleToMaterialConstantsBuffer() const
{
    return m_gpu_desc_handle_to_mat_consts_srvs;
}

CD3DX12_GPU_DESCRIPTOR_HANDLE GltfModel::GetGPUDescHandleToLocalMatricesBuffer() const
{
    return m_gpu_desc_handle_to_local_matrices_cbv;
}

CD3DX12_GPU_DESCRIPTOR_HANDLE GltfModel::GetGPUDescHandleToTexturesTable() const
{
    return m_gpu_desc_handle_to_texture_srvs;
}

CD3DX12_GPU_DESCRIPTOR_HANDLE GltfModel::GetGPUDescHandleToSamplers() const
{
    return m_gpu_desc_handle_to_sampler;
}

void GltfModel::CopyAllDescriptorsTo(ID3D12DescriptorHeap* shader_visible_cbv_srv_uva_heap, ID3D12DescriptorHeap* shader_visible_sampler_heap, CD3DX12_CPU_DESCRIPTOR_HANDLE* dest_shader_visible_cbv_srv_uav_heap_handle, CD3DX12_CPU_DESCRIPTOR_HANDLE* dest_shader_visible_sampler_heap_handle)
{

    // cbv srv uav heap 排布
    // 所有材质的 srv
    // 所有material const buffer(structured buffer)的 srv
    // 所有loca matrices buffer的 cbv

    // sampler heap 排布
    // 所有的sampler

    // 先copy 所有的sampler
    {
        // Step 1: Get the starting GPU and CPU descriptor handles from the heap
        const D3D12_GPU_DESCRIPTOR_HANDLE gpu_handle_start = shader_visible_sampler_heap->GetGPUDescriptorHandleForHeapStart();
        const D3D12_CPU_DESCRIPTOR_HANDLE cpu_handle_start = shader_visible_sampler_heap->GetCPUDescriptorHandleForHeapStart();

        // Step 2: Calculate the index of the CPU handle within the heap
        const ptrdiff_t index = (dest_shader_visible_sampler_heap_handle->ptr - cpu_handle_start.ptr) / m_samplerDescriptorSize;

        // Step 3: Apply the same index to the GPU handle
        m_gpu_desc_handle_to_sampler = gpu_handle_start;
        m_gpu_desc_handle_to_sampler.ptr += index * m_samplerDescriptorSize;

        m_pp_device->CopyDescriptorsSimple(
            m_num_samplers,
            *dest_shader_visible_sampler_heap_handle,
            m_samplerHeap->GetCPUDescriptorHandleForHeapStart(),
            D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);

        dest_shader_visible_sampler_heap_handle->Offset(m_num_samplers, m_samplerDescriptorSize);
    }

    // 先copy 所有的 cbv srv uav，然后再计算索引

    ////////                    bindless                  bindless          每次draw call用index索引进行替换,TEMP_LOCAL_MATRICES_CBV_COUNT是magic number
    const UINT copy_cout = m_texturesImages.size() + m_num_material_views + TEMP_LOCAL_MATRICES_CBV_COUNT;
    {
        m_pp_device->CopyDescriptorsSimple(
            copy_cout,
            *dest_shader_visible_cbv_srv_uav_heap_handle,
            m_cbvSrvUavHeap->GetCPUDescriptorHandleForHeapStart(),
            D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    }

    // 计算索引
    // CD3DX12_GPU_DESCRIPTOR_HANDLE m_gpu_desc_handle_to_texture_srvs;
    // CD3DX12_GPU_DESCRIPTOR_HANDLE m_gpu_desc_handle_to_mat_consts_srvs;
    // CD3DX12_GPU_DESCRIPTOR_HANDLE m_gpu_desc_handle_to_local_matrices_cbv;
    {

        // Step 1: Get the starting GPU and CPU descriptor handles from the heap
        const D3D12_GPU_DESCRIPTOR_HANDLE gpuHandleStart = shader_visible_cbv_srv_uva_heap->GetGPUDescriptorHandleForHeapStart();
        const D3D12_CPU_DESCRIPTOR_HANDLE cpuHandleStart = shader_visible_cbv_srv_uva_heap->GetCPUDescriptorHandleForHeapStart();

        // Step 2: Calculate the index of the CPU handle within the heap
        const ptrdiff_t index = (dest_shader_visible_cbv_srv_uav_heap_handle->ptr - cpuHandleStart.ptr) / m_cbvSrvUavDescriptorSize;

        // Step 3: Apply the same index to the GPU handle
        m_gpu_desc_handle_to_texture_srvs = gpuHandleStart;
        m_gpu_desc_handle_to_texture_srvs.ptr += index * m_cbvSrvUavDescriptorSize;

        m_gpu_desc_handle_to_mat_consts_srvs = m_gpu_desc_handle_to_texture_srvs;
        m_gpu_desc_handle_to_mat_consts_srvs.Offset(m_texturesImages.size(), m_cbvSrvUavDescriptorSize);

        m_gpu_desc_handle_to_local_matrices_cbv = m_gpu_desc_handle_to_mat_consts_srvs;
        m_gpu_desc_handle_to_local_matrices_cbv.Offset(m_num_material_views, m_cbvSrvUavDescriptorSize);
    }

    // 别忘了最后offset传入的shader visible heap的handle
    dest_shader_visible_cbv_srv_uav_heap_handle->Offset(copy_cout, m_cbvSrvUavDescriptorSize);
}

GltfModel::~GltfModel()
{
    for (const auto& p_tex : m_4_com_tex_temp_buffers) {
        stbi_image_free(p_tex);
    }
}

D3D12_FILTER GltfModel::ExtractFilterAndMipMapMode(const fastgltf::Filter min_filter, const fastgltf::Filter mag_filter, const fastgltf::Filter mip_map_mode)
{
    // TODO:
    return D3D12_FILTER_MIN_MAG_MIP_POINT;
}

D3D12_TEXTURE_ADDRESS_MODE GltfModel::ExtractAddressMode(const fastgltf::Wrap warp)
{
    switch (warp) {
    case fastgltf::Wrap::ClampToEdge:
        return D3D12_TEXTURE_ADDRESS_MODE_CLAMP;

    case fastgltf::Wrap::MirroredRepeat:
        return D3D12_TEXTURE_ADDRESS_MODE_MIRROR;

    case fastgltf::Wrap::Repeat:
        return D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    default:
        assert(false);
    }
    return {};
}

GltfModel::GltfModel(ID3D12Device* pp_device, ID3D12GraphicsCommandList* pp_copy_cmd_list)
    : IRenderable()
    , m_num_samplers(0)
    , m_num_material_views(0)
    , m_samplerDescriptorSize(0)
    , m_cbvSrvUavDescriptorSize(0)
    , m_pp_device(pp_device)
    , m_copyCommandList(pp_copy_cmd_list)
    , m_materialConstantDataBuffer()
    , materialConstBufferMappedGPUAddress(nullptr)
    , m_localMatricesDataBuffer()
    , localMatricesBufferMappedGPUAddress(nullptr)
    , m_gpu_desc_handle_to_texture_srvs()
    , m_gpu_desc_handle_to_mat_consts_srvs()
    , m_gpu_desc_handle_to_local_matrices_cbv()
    , m_gpu_desc_handle_to_sampler()
{
    m_samplerDescriptorSize = m_pp_device->GetDescriptorHandleIncrementSize(
        D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);

    m_cbvSrvUavDescriptorSize = m_pp_device->GetDescriptorHandleIncrementSize(
        D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
}

void GltfModel::LoadFromFile(const std::string gltf_file_path)
{
    fastgltf::Parser parser {};

    constexpr auto gltfOptions = fastgltf::Options::DontRequireValidAssetMember | fastgltf::Options::AllowDouble | fastgltf::Options::LoadGLBBuffers | fastgltf::Options::LoadExternalBuffers;
    // fastgltf::Options::LoadExternalImages;

    fastgltf::Asset gltf;
    std::filesystem::path path_filesys = gltf_file_path;

    fastgltf::GltfDataBuffer data;
    data.loadFromFile(gltf_file_path);

    //> LOAD_RAW GLTF RAW FILE LOADING
    auto type = determineGltfFileType(&data);
    if (type == fastgltf::GltfType::glTF) {
        auto load = parser.loadGLTF(&data, path_filesys.parent_path(), gltfOptions);
        if (load) {
            gltf = std::move(load.get());
        } else {
            std::cerr << "Failed to load glTF: " << to_underlying(load.error())
                      << '\n';
            assert(false);
        }
    } else if (type == fastgltf::GltfType::GLB) {
        auto load = parser.loadBinaryGLTF(&data, path_filesys.parent_path(),
            gltfOptions);
        if (load) {
            gltf = std::move(load.get());
        } else {
            std::cerr << "Failed to load glTF: " << to_underlying(load.error())
                      << '\n';
            assert(false);
        }
    } else {
        std::cerr << "Failed to determine glTF container" << '\n';
        assert(false);
    }
    //< load_raw

    //> LOAD_SAMPLERS
    m_num_samplers = gltf.samplers.size();

    // Describe and create a sampler descriptor heap.
    D3D12_DESCRIPTOR_HEAP_DESC samplerHeapDesc = {};
    samplerHeapDesc.NumDescriptors = m_num_samplers;
    samplerHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER;
    samplerHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    ThrowIfFailed(m_pp_device->CreateDescriptorHeap(
        &samplerHeapDesc,
        IID_PPV_ARGS(m_samplerHeap.ReleaseAndGetAddressOf())));

    // m_samplerDescriptorSize = m_pp_device->GetDescriptorHandleIncrementSize(
    //	D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);
    //  NAME_D3D12_OBJECT(m_samplerHeap);

    CD3DX12_CPU_DESCRIPTOR_HANDLE samplerHandle(
        m_samplerHeap->GetCPUDescriptorHandleForHeapStart());

    for (fastgltf::Sampler& sampler : gltf.samplers) {
        // Describe and create the wrapping sampler, which is used for
        // sampling diffuse/normal maps.
        D3D12_SAMPLER_DESC sampler_desc = {};
        sampler_desc.Filter = ExtractFilterAndMipMapMode(
            sampler.minFilter.value_or(fastgltf::Filter::Nearest),
            sampler.magFilter.value_or(fastgltf::Filter::Nearest),
            sampler.minFilter.value_or(fastgltf::Filter::Nearest));

        sampler_desc.AddressU = ExtractAddressMode(sampler.wrapS);
        sampler_desc.AddressV = ExtractAddressMode(sampler.wrapT);
        sampler_desc.AddressW = sampler_desc.AddressU;

        sampler_desc.MinLOD = 0;
        sampler_desc.MaxLOD = D3D12_FLOAT32_MAX;
        sampler_desc.MipLODBias = 0.0f;
        sampler_desc.MaxAnisotropy = 1;
        sampler_desc.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
        sampler_desc.BorderColor[0] = sampler_desc.BorderColor[1] = sampler_desc.BorderColor[2] = sampler_desc.BorderColor[3] = 0;
        m_pp_device->CreateSampler(&sampler_desc, samplerHandle);

        // Move the handle to the next slot in the descriptor heap.
        samplerHandle.Offset(m_samplerDescriptorSize);
    }
    //< load_SAMPLERS

    //> LOAD ALL TEXTURES
    // Describe and create a cbvSrvUav descriptor heap.
    D3D12_DESCRIPTOR_HEAP_DESC cbv_srv_uav_heap_desc = {};
    // TODO: 修改cbv的数目，目前暂时算作200
    cbv_srv_uav_heap_desc.NumDescriptors = gltf.images.size() + gltf.materials.size() + TEMP_LOCAL_MATRICES_CBV_COUNT; // 200 extra for cbv
    cbv_srv_uav_heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    cbv_srv_uav_heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    ThrowIfFailed(m_pp_device->CreateDescriptorHeap(
        &cbv_srv_uav_heap_desc,
        IID_PPV_ARGS(m_cbvSrvUavHeap.ReleaseAndGetAddressOf())));
    //  NAME_D3D12_OBJECT(m_cbvSrvUavHeap);

    CD3DX12_CPU_DESCRIPTOR_HANDLE cbvSrvUavHandle(
        m_cbvSrvUavHeap->GetCPUDescriptorHandleForHeapStart());

    m_texturesImages.resize(gltf.images.size());
    m_textureImageUploads.resize(gltf.images.size());

    for (const auto [img_index, image] :
        std::ranges::views::enumerate(gltf.images)) {
        int width, height, nrChannels;
        // std::string img_name = image.name.c_str();

        constexpr auto loading_format_of_image = DXGI_FORMAT_R8G8B8A8_UNORM; // TODO: 难崩，怎么用UNORM读的
        //
        //        const auto img_visitor = fastgltf::visitor {
        //            [](auto& arg) {},
        //
        //            [&](const fastgltf::sources::URI& img_loca_Path_URI) {
        //                assert(img_loca_Path_URI.fileByteOffset == 0); // We don't support offsets with stbi.
        //                assert(img_loca_Path_URI.uri
        //                           .isLocalPath()); // We're only capable of loading
        //                                            // local files.
        //
        //                const std::string img_local_path(
        //                    img_loca_Path_URI.uri.path().begin(),
        //                    img_loca_Path_URI.uri.path().end()); // Thanks C++.
        //
        //                const std::filesystem::path absolute_path = path_filesys.parent_path().append(img_local_path);
        //
        //                unsigned char* const tex_data = stbi_load(absolute_path.generic_string().c_str(), &width,
        //                    &height, &nrChannels, 4);
        //
        //                if (tex_data) {
        //
        //                    const UINT64 t_width = width;
        //                    const UINT t_height = height;
        //                    constexpr UINT16 depthOrArraySize = 1;
        //
        //                    constexpr auto format_of_image = loading_format_of_image;
        //
        //                    constexpr UINT16 dummy_mip_levels = 1;
        //                    const CD3DX12_RESOURCE_DESC tex_desc(
        //                        D3D12_RESOURCE_DIMENSION_TEXTURE2D, 0, t_width,
        //                        t_height, depthOrArraySize, dummy_mip_levels,
        //                        format_of_image, 1, 0, D3D12_TEXTURE_LAYOUT_UNKNOWN,
        //                        D3D12_RESOURCE_FLAG_NONE);
        //
        //                    ThrowIfFailed(m_pp_device->CreateCommittedResource(
        //                        &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
        //                        D3D12_HEAP_FLAG_NONE, &tex_desc,
        //                        D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
        //                        IID_PPV_ARGS(m_texturesImages[img_index]
        //                                         .ReleaseAndGetAddressOf())));
        //
        //                    // NAME_D3D12_OBJECT_INDEXED(m_texturesImages, i);
        //
        //                    {
        //                        const UINT subresourceCount = tex_desc.DepthOrArraySize * tex_desc.MipLevels;
        //                        const UINT64 uploadBufferSize = GetRequiredIntermediateSize(
        //                            m_texturesImages[img_index].Get(), 0,
        //                            subresourceCount);
        //
        //                        ThrowIfFailed(m_pp_device->CreateCommittedResource(
        //                            &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
        //                            D3D12_HEAP_FLAG_NONE,
        //                            &CD3DX12_RESOURCE_DESC::Buffer(uploadBufferSize),
        //                            D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
        //                            IID_PPV_ARGS(m_textureImageUploads[img_index]
        //                                             .ReleaseAndGetAddressOf())));
        //
        //                        // Copy data to the intermediate upload heap and then
        //                        // schedule a copy from the upload heap to the
        //                        // Texture2D.
        //                        D3D12_SUBRESOURCE_DATA texture_data = {};
        //                        texture_data.pData = tex_data;
        //                        texture_data.RowPitch = width * nrChannels;
        //                        texture_data.SlicePitch = width * height * nrChannels;
        //
        //                        UpdateSubresources(
        //                            m_copyCommandList,
        //                            m_texturesImages[img_index].Get(),
        //                            m_textureImageUploads[img_index].Get(), 0, 0,
        //                            subresourceCount, &texture_data);
        //                        // m_copyCommandList->ResourceBarrier(
        //                        //     1, &CD3DX12_RESOURCE_BARRIER::Transition(m_texturesImages[img_index].Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE));
        //                    }
        //
        //                    stbi_image_free(tex_data);
        //                }
        //            },
        //            [&](const fastgltf::sources::Vector& vector) {
        //                unsigned char* tex_data = stbi_load_from_memory(
        //                    vector.bytes.data(), static_cast<int>(vector.bytes.size()),
        //                    &width, &height, &nrChannels, 4);
        //                if (tex_data) {
        //                    const UINT64 t_width = width;
        //                    const UINT t_height = height;
        //                    constexpr UINT16 depthOrArraySize = 1;
        //                    constexpr UINT16 dummy_mip_levels = 1;
        //
        //                    constexpr auto format_of_image = loading_format_of_image;
        //
        //                    const CD3DX12_RESOURCE_DESC tex_desc(
        //                        D3D12_RESOURCE_DIMENSION_TEXTURE2D, 0, t_width,
        //                        t_height, depthOrArraySize, dummy_mip_levels,
        //                        format_of_image, 1, 0, D3D12_TEXTURE_LAYOUT_UNKNOWN,
        //                        D3D12_RESOURCE_FLAG_NONE);
        //
        //                    ThrowIfFailed(m_pp_device->CreateCommittedResource(
        //                        &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
        //                        D3D12_HEAP_FLAG_NONE, &tex_desc,
        //                        D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
        //                        IID_PPV_ARGS(m_texturesImages[img_index]
        //                                         .ReleaseAndGetAddressOf())));
        //
        //                    // NAME_D3D12_OBJECT_INDEXED(m_texturesImages, i);
        //
        //                    {
        //                        const UINT subresourceCount = tex_desc.DepthOrArraySize * tex_desc.MipLevels;
        //                        const UINT64 uploadBufferSize = GetRequiredIntermediateSize(
        //                            m_texturesImages[img_index].Get(), 0,
        //                            subresourceCount);
        //
        //                        ThrowIfFailed(m_pp_device->CreateCommittedResource(
        //                            &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
        //                            D3D12_HEAP_FLAG_NONE,
        //                            &CD3DX12_RESOURCE_DESC::Buffer(uploadBufferSize),
        //                            D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
        //                            IID_PPV_ARGS(m_textureImageUploads[img_index]
        //                                             .ReleaseAndGetAddressOf())));
        //
        //                        // Copy data to the intermediate upload heap and then
        //                        // schedule a copy from the upload heap to the
        //                        // Texture2D.
        //                        D3D12_SUBRESOURCE_DATA texture_data = {};
        //                        texture_data.pData = tex_data;
        //                        texture_data.RowPitch = width * nrChannels;
        //                        texture_data.SlicePitch = width * height * nrChannels;
        //
        //                        UpdateSubresources(
        //                            m_copyCommandList,
        //                            m_texturesImages[img_index].Get(),
        //                            m_textureImageUploads[img_index].Get(), 0, 0,
        //                            subresourceCount, &texture_data);
        //                        // m_copyCommandList->ResourceBarrier(
        //                        //     1, &CD3DX12_RESOURCE_BARRIER::Transition(m_texturesImages[img_index].Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE));
        //                    }
        //
        //                    stbi_image_free(tex_data);
        //                }
        //            },
        //
        //            [&](const fastgltf::sources::BufferView& view) {
        //                auto& asset = gltf;
        //
        //                const auto& bufferView = asset.bufferViews[view.bufferViewIndex];
        //                auto& buffer = asset.buffers[bufferView.bufferIndex];
        //
        //                std::visit(
        //                    fastgltf::visitor {
        //                        // We only care about VectorWithMime here, because we
        //                        // specify LoadExternalBuffers, meaning all buffers
        //                        // are already loaded into a vector.
        //                        [](auto&) {},
        //                        [&](const fastgltf::sources::Vector& vector) {
        //                            unsigned char* tex_data = stbi_load_from_memory(
        //                                vector.bytes.data() + bufferView.byteOffset,
        //                                static_cast<int>(bufferView.byteLength), &width,
        //                                &height, &nrChannels, 4);
        //                            if (tex_data) {
        //                                const UINT64 t_width = width;
        //                                const UINT t_height = height;
        //                                constexpr UINT16 depthOrArraySize = 1;
        //                                constexpr UINT16 dummy_mip_levels = 1;
        //
        //                                constexpr auto format_of_image = loading_format_of_image;
        //
        //                                const CD3DX12_RESOURCE_DESC tex_desc(
        //                                    D3D12_RESOURCE_DIMENSION_TEXTURE2D, 0,
        //                                    t_width, t_height, depthOrArraySize,
        //                                    dummy_mip_levels, format_of_image, 1, 0,
        //                                    D3D12_TEXTURE_LAYOUT_UNKNOWN,
        //                                    D3D12_RESOURCE_FLAG_NONE);
        //
        //                                ThrowIfFailed(
        //                                    m_pp_device->CreateCommittedResource(
        //                                        &CD3DX12_HEAP_PROPERTIES(
        //                                            D3D12_HEAP_TYPE_DEFAULT),
        //                                        D3D12_HEAP_FLAG_NONE, &tex_desc,
        //                                        D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
        //                                        IID_PPV_ARGS(
        //                                            m_texturesImages[img_index]
        //                                                .ReleaseAndGetAddressOf())));
        //
        //                                // NAME_D3D12_OBJECT_INDEXED(m_texturesImages,
        //                                // i);
        //
        //                                {
        //                                    const UINT subresourceCount = tex_desc.DepthOrArraySize * tex_desc.MipLevels;
        //                                    const UINT64 uploadBufferSize = GetRequiredIntermediateSize(
        //                                        m_texturesImages[img_index].Get(),
        //                                        0, subresourceCount);
        //
        //                                    ThrowIfFailed(
        //                                        m_pp_device->CreateCommittedResource(
        //                                            &CD3DX12_HEAP_PROPERTIES(
        //                                                D3D12_HEAP_TYPE_UPLOAD),
        //                                            D3D12_HEAP_FLAG_NONE,
        //                                            &CD3DX12_RESOURCE_DESC::Buffer(
        //                                                uploadBufferSize),
        //                                            D3D12_RESOURCE_STATE_GENERIC_READ,
        //                                            nullptr,
        //                                            IID_PPV_ARGS(
        //                                                m_textureImageUploads[img_index]
        //                                                    .ReleaseAndGetAddressOf())));
        //
        //                                    // Copy data to the intermediate upload heap
        //                                    // and then schedule a copy from the upload
        //                                    // heap to the Texture2D.
        //                                    D3D12_SUBRESOURCE_DATA texture_data = {};
        //                                    texture_data.pData = tex_data;
        //                                    texture_data.RowPitch = width * nrChannels;
        //                                    texture_data.SlicePitch = width * height * nrChannels;
        //
        //                                    UpdateSubresources(
        //                                        m_copyCommandList,
        //                                        m_texturesImages[img_index].Get(),
        //                                        m_textureImageUploads[img_index].Get(),
        //                                        0, 0, subresourceCount, &texture_data);
        //
        //                                    /*
        //*       m_copyCommandList->ResourceBarrier(
        //   1,
        //   &CD3DX12_RESOURCE_BARRIER::Transition(
        //       m_texturesImages[img_index].Get(),
        //       D3D12_RESOURCE_STATE_COPY_DEST,
        //       D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE));*/
        //                                }
        //
        //                                stbi_image_free(tex_data);
        //                            }
        //                        } },
        //                    buffer.data);
        //            }
        //        };
        //
        //        // image.data的类型就是
        //        // using DataSource = std::variant<std::monostate, sources::BufferView, sources::URI, sources::Vector, sources::CustomBuffer, sources::ByteView, sources::Fallback>;
        //        std::visit(img_visitor, image.data);

        {
            const fastgltf::sources::URI* p_img_loca_Path_URI = std::get_if<fastgltf::sources::URI>(&image.data);
            if (p_img_loca_Path_URI) {
                const auto& img_loca_Path_URI = *p_img_loca_Path_URI;

                assert(img_loca_Path_URI.fileByteOffset == 0); // We don't support offsets with stbi.
                assert(img_loca_Path_URI.uri
                           .isLocalPath()); // We're only capable of loading
                                            // local files.

                const std::string img_local_path(
                    img_loca_Path_URI.uri.path().begin(),
                    img_loca_Path_URI.uri.path().end()); // Thanks C++.

                const std::filesystem::path absolute_path = path_filesys.parent_path().append(img_local_path);

                unsigned char* temp_tex_data = stbi_load(absolute_path.generic_string().c_str(), &width,
                    &height, &nrChannels, 4);

                assert(nrChannels == 4 || nrChannels == 3);
                if (temp_tex_data) {
                    const UINT64 t_width = width;
                    const UINT t_height = height;
                    constexpr UINT16 depth_or_array_size = 1;
                    constexpr auto format_of_image = loading_format_of_image;

                    m_4_com_tex_temp_buffers.push_back(temp_tex_data);

                    constexpr UINT16 dummy_mip_levels = 1;
                    const CD3DX12_RESOURCE_DESC tex_desc(
                        D3D12_RESOURCE_DIMENSION_TEXTURE2D, 0, t_width,
                        t_height, depth_or_array_size, dummy_mip_levels,
                        format_of_image, 1, 0, D3D12_TEXTURE_LAYOUT_UNKNOWN,
                        D3D12_RESOURCE_FLAG_NONE);

                    ThrowIfFailed(m_pp_device->CreateCommittedResource(
                        &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
                        D3D12_HEAP_FLAG_NONE, &tex_desc,
                        D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                        IID_PPV_ARGS(m_texturesImages[img_index].ReleaseAndGetAddressOf())));

                    // Convert std::string to std::wstring
                    std::wstring_convert<std::codecvt_utf8<wchar_t>> converter;
                    std::wstring wide_string = converter.from_bytes(img_local_path);
                    m_texturesImages[img_index]->SetName(wide_string.c_str());

                    {
                        const UINT subresourceCount = tex_desc.DepthOrArraySize * tex_desc.MipLevels;
                        const UINT64 uploadBufferSize = GetRequiredIntermediateSize(
                            m_texturesImages[img_index].Get(), 0,
                            subresourceCount);

                        ThrowIfFailed(m_pp_device->CreateCommittedResource(
                            &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
                            D3D12_HEAP_FLAG_NONE,
                            &CD3DX12_RESOURCE_DESC::Buffer(uploadBufferSize),
                            D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                            IID_PPV_ARGS(m_textureImageUploads[img_index]
                                             .ReleaseAndGetAddressOf())));

                        // Copy data to the intermediate upload heap and then
                        // schedule a copy from the upload heap to the
                        // Texture2D.
                        D3D12_SUBRESOURCE_DATA texture_data = {};
                        texture_data.pData = m_4_com_tex_temp_buffers.back();
                        texture_data.RowPitch = t_width * 4;
                        texture_data.SlicePitch = t_width * t_height * 4;

                        UpdateSubresources(
                            m_copyCommandList,
                            m_texturesImages[img_index].Get(),
                            m_textureImageUploads[img_index].Get(), 0, 0,
                            subresourceCount, &texture_data);
                    }
                }
            }
        }

        {
            fastgltf::sources::Vector* p_vector = std::get_if<fastgltf::sources::Vector>(&image.data);
            if (p_vector) {
                assert(false, "channel number is wrong!");
                fastgltf::sources::Vector& vector = *p_vector;

                unsigned char* tex_data = stbi_load_from_memory(
                    vector.bytes.data(), static_cast<int>(vector.bytes.size()),
                    &width, &height, &nrChannels, 4);
                if (tex_data) {
                    const UINT64 t_width = width;
                    const UINT t_height = height;
                    constexpr UINT16 depthOrArraySize = 1;
                    constexpr UINT16 dummy_mip_levels = 1;

                    constexpr auto format_of_image = loading_format_of_image;

                    const CD3DX12_RESOURCE_DESC tex_desc(
                        D3D12_RESOURCE_DIMENSION_TEXTURE2D, 0, t_width,
                        t_height, depthOrArraySize, dummy_mip_levels,
                        format_of_image, 1, 0, D3D12_TEXTURE_LAYOUT_UNKNOWN,
                        D3D12_RESOURCE_FLAG_NONE);

                    ThrowIfFailed(m_pp_device->CreateCommittedResource(
                        &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
                        D3D12_HEAP_FLAG_NONE, &tex_desc,
                        D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                        IID_PPV_ARGS(m_texturesImages[img_index]
                                         .ReleaseAndGetAddressOf())));

                    // NAME_D3D12_OBJECT_INDEXED(m_texturesImages, i);

                    {
                        const UINT subresourceCount = tex_desc.DepthOrArraySize * tex_desc.MipLevels;
                        const UINT64 uploadBufferSize = GetRequiredIntermediateSize(
                            m_texturesImages[img_index].Get(), 0,
                            subresourceCount);

                        ThrowIfFailed(m_pp_device->CreateCommittedResource(
                            &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
                            D3D12_HEAP_FLAG_NONE,
                            &CD3DX12_RESOURCE_DESC::Buffer(uploadBufferSize),
                            D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                            IID_PPV_ARGS(m_textureImageUploads[img_index]
                                             .ReleaseAndGetAddressOf())));

                        // Copy data to the intermediate upload heap and then
                        // schedule a copy from the upload heap to the
                        // Texture2D.
                        D3D12_SUBRESOURCE_DATA texture_data = {};
                        texture_data.pData = tex_data;
                        texture_data.RowPitch = width * nrChannels;
                        texture_data.SlicePitch = width * height * nrChannels;

                        UpdateSubresources(
                            m_copyCommandList,
                            m_texturesImages[img_index].Get(),
                            m_textureImageUploads[img_index].Get(), 0, 0,
                            subresourceCount, &texture_data);
                        // m_copyCommandList->ResourceBarrier(
                        //     1, &CD3DX12_RESOURCE_BARRIER::Transition(m_texturesImages[img_index].Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE));
                    }
                }
            }
        }

        {
            fastgltf::sources::BufferView* p_view = std::get_if<fastgltf::sources::BufferView>(&image.data);
            if (p_view) {
                assert(false, "channel number is wrong!");
                fastgltf::sources::BufferView& view = *p_view;

                auto& asset = gltf;

                const auto& bufferView = asset.bufferViews[view.bufferViewIndex];
                auto& buffer = asset.buffers[bufferView.bufferIndex];

                std::visit(
                    fastgltf::visitor {
                        // We only care about VectorWithMime here, because we
                        // specify LoadExternalBuffers, meaning all buffers
                        // are already loaded into a vector.
                        [](auto&) {},
                        [&](const fastgltf::sources::Vector& vector) {
                            unsigned char* tex_data = stbi_load_from_memory(
                                vector.bytes.data() + bufferView.byteOffset,
                                static_cast<int>(bufferView.byteLength), &width,
                                &height, &nrChannels, 4);
                            if (tex_data) {
                                const UINT64 t_width = width;
                                const UINT t_height = height;
                                constexpr UINT16 depthOrArraySize = 1;
                                constexpr UINT16 dummy_mip_levels = 1;

                                constexpr auto format_of_image = loading_format_of_image;

                                const CD3DX12_RESOURCE_DESC tex_desc(
                                    D3D12_RESOURCE_DIMENSION_TEXTURE2D, 0,
                                    t_width, t_height, depthOrArraySize,
                                    dummy_mip_levels, format_of_image, 1, 0,
                                    D3D12_TEXTURE_LAYOUT_UNKNOWN,
                                    D3D12_RESOURCE_FLAG_NONE);

                                ThrowIfFailed(
                                    m_pp_device->CreateCommittedResource(
                                        &CD3DX12_HEAP_PROPERTIES(
                                            D3D12_HEAP_TYPE_DEFAULT),
                                        D3D12_HEAP_FLAG_NONE, &tex_desc,
                                        D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                        IID_PPV_ARGS(
                                            m_texturesImages[img_index]
                                                .ReleaseAndGetAddressOf())));

                                // NAME_D3D12_OBJECT_INDEXED(m_texturesImages,
                                // i);

                                {
                                    const UINT subresourceCount = tex_desc.DepthOrArraySize * tex_desc.MipLevels;
                                    const UINT64 uploadBufferSize = GetRequiredIntermediateSize(
                                        m_texturesImages[img_index].Get(),
                                        0, subresourceCount);

                                    ThrowIfFailed(
                                        m_pp_device->CreateCommittedResource(
                                            &CD3DX12_HEAP_PROPERTIES(
                                                D3D12_HEAP_TYPE_UPLOAD),
                                            D3D12_HEAP_FLAG_NONE,
                                            &CD3DX12_RESOURCE_DESC::Buffer(
                                                uploadBufferSize),
                                            D3D12_RESOURCE_STATE_GENERIC_READ,
                                            nullptr,
                                            IID_PPV_ARGS(
                                                m_textureImageUploads[img_index]
                                                    .ReleaseAndGetAddressOf())));

                                    // Copy data to the intermediate upload heap
                                    // and then schedule a copy from the upload
                                    // heap to the Texture2D.
                                    D3D12_SUBRESOURCE_DATA texture_data = {};
                                    texture_data.pData = tex_data;
                                    texture_data.RowPitch = width * nrChannels;
                                    texture_data.SlicePitch = width * height * nrChannels;

                                    UpdateSubresources(
                                        m_copyCommandList,
                                        m_texturesImages[img_index].Get(),
                                        m_textureImageUploads[img_index].Get(),
                                        0, 0, subresourceCount, &texture_data);

                                    /*
*       m_copyCommandList->ResourceBarrier(
   1,
   &CD3DX12_RESOURCE_BARRIER::Transition(
       m_texturesImages[img_index].Get(),
       D3D12_RESOURCE_STATE_COPY_DEST,
       D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE));*/
                                }
                            }
                        } },
                    buffer.data);
            }
        }

        // put it into a std::map
        // assert(m_namesToTextures
        //           .try_emplace(img_name, m_texturesImages[img_index].Get())
        //           .second);

        // Describe and create an SRV.
        UINT dummy_mip_levels = 1;
        D3D12_SHADER_RESOURCE_VIEW_DESC srv_desc = {};
        srv_desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srv_desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srv_desc.Format = loading_format_of_image;
        srv_desc.Texture2D.MipLevels = dummy_mip_levels;
        srv_desc.Texture2D.MostDetailedMip = 0;
        srv_desc.Texture2D.ResourceMinLODClamp = 0.0f;
        m_pp_device->CreateShaderResourceView(m_texturesImages[img_index].Get(), &srv_desc, cbvSrvUavHandle);

        cbvSrvUavHandle.Offset(m_cbvSrvUavDescriptorSize);
    }

    //> LOAD_MATERIAL_CONST_BUFFER
    // create material const buffer to hold material constants data
    m_num_material_views = gltf.materials.size();
    ThrowIfFailed(m_pp_device->CreateCommittedResource(
        &CD3DX12_HEAP_PROPERTIES(
            D3D12_HEAP_TYPE_UPLOAD),

        D3D12_HEAP_FLAG_NONE, // no flags
        &CD3DX12_RESOURCE_DESC::Buffer(
            (sizeof(MaterialConstants) * gltf.materials.size() + (D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT - 1)) & ~(D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT - 1)),

        // size of the resource heap. Must be a multiple of 64KB
        // for single-textures and constant buffers
        D3D12_RESOURCE_STATE_GENERIC_READ, // will be data that is read from, so
                                           // we keep it in the generic read
                                           // state
        nullptr, // we do not have use an optimized clear value for constant
                 // buffers
        IID_PPV_ARGS(m_materialConstBuffer.ReleaseAndGetAddressOf())));

    // m_materialConstBuffer->SetName(L"Structured Buffer for Upload Resource Heap");

    m_materialConstantDataBuffer = cbvSrvUavHandle;

    // 用bindless，但是也可以用一个structured buffer绑定element等于0 到 element等于materials.size() - 1;的范围，这样就只用一个srv slot，每次换模型就换绑
    for (auto i = 0; i < gltf.materials.size(); i++) {
        D3D12_SHADER_RESOURCE_VIEW_DESC srv_desc = {};
        srv_desc.Format = DXGI_FORMAT_UNKNOWN;
        srv_desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srv_desc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
        srv_desc.Buffer.FirstElement = i;
        srv_desc.Buffer.NumElements = 1; // Number of elements in the buffer
        srv_desc.Buffer.StructureByteStride = sizeof(MaterialConstants); // ByteAddressBuffer uses 0 for StructureByteStride
        srv_desc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;

        m_pp_device->CreateShaderResourceView(m_materialConstBuffer.Get(), &srv_desc, cbvSrvUavHandle);
        cbvSrvUavHandle.Offset(m_cbvSrvUavDescriptorSize);
    }

    CD3DX12_RANGE read_range(
        0, 0); // We do not intend to read from this resource on the CPU. (End
               // is less than or equal to begin)
    ThrowIfFailed(m_materialConstBuffer->Map(
        0, &read_range,
        reinterpret_cast<void**>(&materialConstBufferMappedGPUAddress)));

    //< load_material_const_buffer

    //> LOAD_MATERIAL AND FILL MATERIAL CONST DATA
    for (const auto [mat_index, mat] :
        std::ranges::views::enumerate(gltf.materials)) {
        // auto newMat = std::make_shared<GLTFMaterialInstance>();
        // materials.push_back(newMat);
        // result_gltf.materials[mat.name.c_str() + std::to_string(mat_index)] =
        // newMat;

        MaterialConstants constants;
        constants.colorFactors.x = mat.pbrData.baseColorFactor[0];
        constants.colorFactors.y = mat.pbrData.baseColorFactor[1];
        constants.colorFactors.z = mat.pbrData.baseColorFactor[2];
        constants.colorFactors.w = mat.pbrData.baseColorFactor[3];

        constants.metalRoughFactors.x = mat.pbrData.metallicFactor;
        constants.metalRoughFactors.y = mat.pbrData.roughnessFactor;

        // TODO: alpha blending, emissve, blooming
        //  auto passType = MaterialPassType::MainColor;
        //   if (mat.alphaMode == fastgltf::AlphaMode::Blend) {
        //     passType = MaterialPassType::Transparent;
        //   }

        // install textures index
        if (mat.pbrData.baseColorTexture.has_value()) {
            constants.albedoIndex = gltf.textures[mat.pbrData.baseColorTexture.value().textureIndex]
                                        .imageIndex.value();
            constants.albedoSamplerIndex = gltf.textures[mat.pbrData.baseColorTexture.value().textureIndex]
                                               .samplerIndex.value();
        }

        if (mat.pbrData.metallicRoughnessTexture.has_value()) {
            constants.metalRoughIndex = gltf.textures[mat.pbrData.metallicRoughnessTexture.value()
                                                          .textureIndex]
                                            .imageIndex.value();
            constants.metalRoughSamplerIndex = gltf.textures[mat.pbrData.metallicRoughnessTexture.value()
                                                                 .textureIndex]
                                                   .samplerIndex.value();
        }

        if (mat.normalTexture.has_value()) {
            constants.normalIndex = gltf.textures[mat.normalTexture.value().textureIndex]
                                        .imageIndex.value();
            constants.normalSamplerIndex = gltf.textures[mat.normalTexture.value().textureIndex]
                                               .samplerIndex.value();
        }

        if (mat.emissiveTexture.has_value()) {
            constants.emissiveIndex = gltf.textures[mat.emissiveTexture.value().textureIndex]
                                          .imageIndex.value();
            constants.emissiveSamplerIndex = gltf.textures[mat.emissiveTexture.value().textureIndex]
                                                 .samplerIndex.value();
        }

        if (mat.occlusionTexture.has_value()) {
            constants.occlusionIndex = gltf.textures[mat.occlusionTexture.value().textureIndex]
                                           .imageIndex.value();
            constants.occlusionSamplerIndex = gltf.textures[mat.occlusionTexture.value().textureIndex]
                                                  .samplerIndex.value();
        }

        // write current material parameters to buffer
        memcpy((materialConstBufferMappedGPUAddress + mat_index * sizeof(MaterialConstants)),
            &constants, sizeof(MaterialConstants));
    }
    m_materialConstBuffer->Unmap(0, nullptr);
    //< load_material

    //> LOAD_NODES
    // use the same vectors for all meshes so that the memory doesn't reallocate
    // as often
    std::vector<uint32_t> indices;
    std::vector<StandardVertex> vertices;

    m_meshes = std::make_unique<MeshAsset[]>(gltf.meshes.size());
    for (auto [mesh_index, mesh] : std::ranges::views::enumerate(gltf.meshes)) {
        // auto newmesh = std::make_shared<MeshAsset>();
        // meshes.push_back(newmesh);
        // result_gltf.x6c xzvn26cxmesh.name.c_str() +
        // std::to_string(mesh_index)] = newmesh;
        m_meshes[mesh_index].name = mesh.name.append(std::to_string(mesh_index));

        // clear the mesh_asset arrays each mesh_asset, we don't want to merge them by error
        indices.clear();
        vertices.clear();

        // process geosurface
        for (auto&& p : mesh.primitives) {
            MeshAsset::GeoSurface newSurface;
            newSurface.startIndex = static_cast<uint32_t>(indices.size());
            newSurface.count = static_cast<uint32_t>(
                gltf.accessors[p.indicesAccessor.value()].count);

            size_t initial_vtx = vertices.size();

            // load indexes
            {
                fastgltf::Accessor& indexaccessor = gltf.accessors[p.indicesAccessor.value()];
                indices.reserve(indices.size() + indexaccessor.count);

                fastgltf::iterateAccessor<std::uint32_t>(
                    gltf, indexaccessor,
                    [&](std::uint32_t idx) {
                        indices.push_back(idx + initial_vtx);
                    });
            }

            // load vertex positions
            {
                fastgltf::Accessor& posAccessor = gltf.accessors[p.findAttribute("POSITION")->second];
                vertices.resize(vertices.size() + posAccessor.count);

                fastgltf::iterateAccessorWithIndex<glm::vec3>(
                    gltf, posAccessor,
                    [&](glm::vec3 v, size_t index) -> void {
                        StandardVertex newvtx;
                        newvtx.position = v;
                        newvtx.normal = { 0, 1, 0 };
                        newvtx.color = glm::vec4 { 1.f };
                        newvtx.uv = glm::float2 { 0.f, 0.f };
                        newvtx.tangent = { 1, 0, 0 };
                        vertices[initial_vtx + index] = newvtx;
                    });
            }

            // load vertex normals
            auto normals = p.findAttribute("NORMAL");
            if (normals != p.attributes.end()) {
                fastgltf::iterateAccessorWithIndex<glm::vec3>(
                    gltf, gltf.accessors[normals->second],
                    [&](glm::vec3 v, size_t index) {
                        vertices[initial_vtx + index].normal = v;
                    });
            }

            // load UVs
            auto uv = p.findAttribute("TEXCOORD_0");
            if (uv != p.attributes.end()) {
                fastgltf::iterateAccessorWithIndex<glm::vec2>(
                    gltf, gltf.accessors[uv->second],
                    [&](glm::vec2 v, size_t index) {
                        vertices[initial_vtx + index].uv.x = v.x;
                        vertices[initial_vtx + index].uv.y = v.y;
                    });
            }

            // load vertex colors
            auto colors = p.findAttribute("COLOR_0");
            if (colors != p.attributes.end()) {
                fastgltf::iterateAccessorWithIndex<glm::vec4>(
                    gltf, gltf.accessors[colors->second],
                    [&](glm::vec4 v, size_t index) {
                        vertices[initial_vtx + index].color = v;
                    });
            }

            // load tangent
            auto tangent = p.findAttribute("TANGENT");
            if (tangent != p.attributes.end()) {
                fastgltf::iterateAccessorWithIndex<glm::vec3>(
                    gltf, gltf.accessors[tangent->second],
                    [&](glm::vec3 v, size_t index) {
                        vertices[initial_vtx + index].tangent = v;
                    });
            }

            // load material index
            if (p.materialIndex.has_value()) {
                newSurface.materialIndex = p.materialIndex.value();
            } else {
                assert(false);
                newSurface.materialIndex = UINT32_MAX;
            }

            // glm::vec3 minpos = vertices[initial_vtx].position;
            // glm::vec3 maxpos = vertices[initial_vtx].position;
            // for (auto i = initial_vtx; i < vertices.size(); i++)
            //{
            //     minpos = min(minpos, vertices[i].position);
            //     maxpos = max(maxpos, vertices[i].position);
            // }

            // newSurface.bounds.origin = (maxpos + minpos) / 2.f;
            // newSurface.bounds.extents = (maxpos - minpos) / 2.f;
            // newSurface.bounds.sphereRadius =
            // length(newSurface.bounds.extents);

            m_meshes[mesh_index].surfaces.push_back(newSurface);
        }

        // Create the vertex buffer.

        {
            const UINT32 vertex_data_size = vertices.size() * sizeof(StandardVertex);

            ThrowIfFailed(m_pp_device->CreateCommittedResource(
                &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
                D3D12_HEAP_FLAG_NONE,
                &CD3DX12_RESOURCE_DESC::Buffer(vertex_data_size),
                D3D12_RESOURCE_STATE_COMMON, nullptr,
                IID_PPV_ARGS(
                    m_meshes[mesh_index]
                        .mesh_buffers.m_vertexBuffer.ReleaseAndGetAddressOf())));

            // NAME_D3D12_OBJECT(m_vertexBuffer);
            {
                ThrowIfFailed(m_pp_device->CreateCommittedResource(
                    &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
                    D3D12_HEAP_FLAG_NONE,
                    &CD3DX12_RESOURCE_DESC::Buffer(vertex_data_size),
                    D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                    IID_PPV_ARGS(m_meshes[mesh_index]
                                     .mesh_buffers.m_vertexBufferUpload
                                     .ReleaseAndGetAddressOf())));

                // Copy data to the upload heap and then schedule a copy
                // from the upload heap to the vertex buffer.
                m_meshes[mesh_index].mesh_buffers.m_cpuVertices = std::move(vertices);
                D3D12_SUBRESOURCE_DATA vertex_data = {};
                vertex_data.pData = m_meshes[mesh_index].mesh_buffers.m_cpuVertices.data();
                vertex_data.RowPitch = vertex_data_size;
                vertex_data.SlicePitch = vertex_data.RowPitch;

                // PIXBeginEvent(
                //     commandList.Get(), 0,
                //     L"Copy vertex buffer data to default resource...");

                UpdateSubresources<1>(
                    m_copyCommandList,
                    m_meshes[mesh_index].mesh_buffers.m_vertexBuffer.Get(),
                    m_meshes[mesh_index].mesh_buffers.m_vertexBufferUpload.Get(),
                    0, 0, 1, &vertex_data);

                // fdsaklfdsa
                // m_copyCommandList->ResourceBarrier(
                //     1,
                //     &CD3DX12_RESOURCE_BARRIER::Transition(
                //         m_meshes[mesh_index].mesh_buffers.m_vertexBuffer.Get(),
                //         D3D12_RESOURCE_STATE_COPY_DEST,
                //         D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER));

                // PIXEndEvent(commandList.Get());
            }

            // Initialize the vertex buffer view.
            m_meshes[mesh_index].mesh_buffers.m_vertexBufferView.BufferLocation = m_meshes[mesh_index]
                                                                                      .mesh_buffers.m_vertexBuffer->GetGPUVirtualAddress();
            m_meshes[mesh_index].mesh_buffers.m_vertexBufferView.SizeInBytes = vertex_data_size;
            m_meshes[mesh_index].mesh_buffers.m_vertexBufferView.StrideInBytes = sizeof(StandardVertex);
        }

        // Create the index buffer.
        {
            const UINT32 indices_data_size = indices.size() * sizeof(uint32_t);

            ThrowIfFailed(m_pp_device->CreateCommittedResource(
                &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
                D3D12_HEAP_FLAG_NONE,
                &CD3DX12_RESOURCE_DESC::Buffer(indices_data_size),
                D3D12_RESOURCE_STATE_COMMON, nullptr,
                IID_PPV_ARGS(
                    m_meshes[mesh_index]
                        .mesh_buffers.m_indexBuffer.ReleaseAndGetAddressOf())));

            // NAME_D3D12_OBJECT(m_indexBuffer);
            {
                ThrowIfFailed(m_pp_device->CreateCommittedResource(
                    &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
                    D3D12_HEAP_FLAG_NONE,
                    &CD3DX12_RESOURCE_DESC::Buffer(indices_data_size),
                    D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                    IID_PPV_ARGS(m_meshes[mesh_index]
                                     .mesh_buffers.m_indexBufferUpload
                                     .ReleaseAndGetAddressOf())));

                // Copy data to the upload heap and then schedule a copy
                // from the upload heap to the index buffer.

                m_meshes[mesh_index].mesh_buffers.m_cpuIndices = std::move(indices);

                D3D12_SUBRESOURCE_DATA index_data = {};
                index_data.pData = m_meshes[mesh_index].mesh_buffers.m_cpuIndices.data();
                index_data.RowPitch = indices_data_size;
                index_data.SlicePitch = index_data.RowPitch;

                // PIXBeginEvent(m_copyCommandList.Get(), 0,
                //               L"Copy index buffer data to default
                //               resource...");

                UpdateSubresources<1>(
                    m_copyCommandList,
                    m_meshes[mesh_index].mesh_buffers.m_indexBuffer.Get(),
                    m_meshes[mesh_index].mesh_buffers.m_indexBufferUpload.Get(),
                    0, 0, 1, &index_data);

                // m_copyCommandList->ResourceBarrier(
                //     1, &CD3DX12_RESOURCE_BARRIER::Transition(m_meshes[mesh_index].mesh_buffers.m_indexBuffer.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_INDEX_BUFFER));

                // PIXEndEvent(commandList.Get());
            }

            // Initialize the index buffer view.
            m_meshes[mesh_index].mesh_buffers.m_indexBufferView.BufferLocation = m_meshes[mesh_index]
                                                                                     .mesh_buffers.m_indexBuffer->GetGPUVirtualAddress();
            m_meshes[mesh_index].mesh_buffers.m_indexBufferView.SizeInBytes = indices_data_size;
            m_meshes[mesh_index].mesh_buffers.m_indexBufferView.Format = DXGI_FORMAT_R32_UINT;
        }
    }
    //> load_nodes

    // LOAD ALL NODES AND THEIR MESHES
    for (auto [node_index, node] : std::ranges::views::enumerate(gltf.nodes)) {
        std::shared_ptr<Node> new_node;

        // find if the node has a mesh_asset, and if it does then hook it to the mesh_asset
        // pointer and allocate it with the meshnode class
        if (node.meshIndex.has_value()) {
            new_node = std::make_shared<MeshNode>();
            dynamic_cast<MeshNode*>(new_node.get())->mesh_asset = &m_meshes[*node.meshIndex];
        } else {
            new_node = std::make_shared<Node>();
        }

        m_nodes.push_back(new_node);
        // result_gltf.nodes[node.name.c_str() + std::to_string(node_index)] =
        // newNode;

        std::visit(
            fastgltf::visitor {
                [&](const fastgltf::Node::TransformMatrix& matrix) {
                    memcpy(&new_node->local_transform, matrix.data(),
                        sizeof(matrix));
                },

                [&](const fastgltf::Node::TRS& transform) {
                    const glm::vec3 tl(transform.translation[0],
                        transform.translation[1],
                        transform.translation[2]);
                    const glm::quat rot(
                        transform.rotation[3], transform.rotation[0],
                        transform.rotation[1], transform.rotation[2]);
                    const glm::vec3 sc(transform.scale[0], transform.scale[1],
                        transform.scale[2]);

                    const glm::mat4 tm = translate(glm::mat4(1.f), tl);
                    const glm::mat4 rm = glm::toMat4(rot);
                    const glm::mat4 sm = scale(glm::mat4(1.f), sc);
                    new_node->local_transform = tm * rm * sm;
                } },
            node.transform);
    }
    //< load_nodes

    //> LOAD_SCENE_GRAPH
    // run loop again to setup final_transform hierarchy
    for (auto i = 0; i < gltf.nodes.size(); i++) {
        fastgltf::Node& node_from_gltf = gltf.nodes[i];
        std::shared_ptr<Node>& node_to_node = m_nodes[i];

        for (auto& c : node_from_gltf.children) {
            node_to_node->children.push_back(m_nodes[c]);
            m_nodes[c]->parent = node_to_node;
        }
    }

    // find the top nodes, with no parents
    for (auto& node : m_nodes) {
        if (node->parent.lock() == nullptr) {
            m_topNodes.push_back(node.get());
            node->RefreshTransform(glm::mat4 { 1.f });
        }
    }
    //< load_scene_graph

    //> PRE RECORD DRAW CONTEXT
    constexpr glm::mat4 top_matrix_on_model = glm::mat4(1.0);
    this->Draw(top_matrix_on_model, m_draw_ctx);
    //< pre record draw context

    //> CREATE LOCAL MATRIX BUFFER
    // 每一个渲染记录都有一个final matrix
    constexpr UINT aligned_size_cbuffer = (sizeof(glm::mat4) + (D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT - 1)) & ~(D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT - 1);
    const UINT size_of_local_matrices_buffer = aligned_size_cbuffer * m_draw_ctx.OpaqueSurfaces.size();

    ThrowIfFailed(m_pp_device->CreateCommittedResource(
        &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
        D3D12_HEAP_FLAG_NONE, // no flags
        //&CD3DX12_RESOURCE_DESC::Buffer((size_of_local_matrices_buffer + (D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT - 1)) & ~(D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT - 1)),
        &CD3DX12_RESOURCE_DESC::Buffer((size_of_local_matrices_buffer)),

        // size of the resource heap. Must be a multiple of 64KB
        // for single-textures and constant buffers
        D3D12_RESOURCE_STATE_GENERIC_READ, // will be data that is read from, so
                                           // we keep it in the generic read
                                           // state
        nullptr, // we do not have use an optimized clear value for constant
                 // buffers
        IID_PPV_ARGS(m_localMatricesBuffer.ReleaseAndGetAddressOf())));

    // m_localMatricesBuffer->SetName(L"Constant Buffer Upload Resource Heap");

    m_localMatricesDataBuffer = cbvSrvUavHandle;

    for (auto i = 0; i < m_draw_ctx.OpaqueSurfaces.size(); i++) {
        // Describe and create the local matrices buffer view (CBV) and cache the GPU descriptor handle.
        D3D12_CONSTANT_BUFFER_VIEW_DESC cbv_desc = {};
        cbv_desc.SizeInBytes = aligned_size_cbuffer;
        cbv_desc.BufferLocation = m_localMatricesBuffer->GetGPUVirtualAddress() + (i * aligned_size_cbuffer);

        m_pp_device->CreateConstantBufferView(&cbv_desc, cbvSrvUavHandle);
        cbvSrvUavHandle.Offset(m_cbvSrvUavDescriptorSize);
    }

    CD3DX12_RANGE read_range_0(
        0, 0); // We do not intend to read from this resource on the CPU. (End
               // is less than or equal to begin)
    ThrowIfFailed(m_localMatricesBuffer->Map(
        0, &read_range_0,
        reinterpret_cast<void**>(&localMatricesBufferMappedGPUAddress)));

    for (auto i = 0; i < m_draw_ctx.OpaqueSurfaces.size(); i++) {
        memcpy((localMatricesBufferMappedGPUAddress + (i * aligned_size_cbuffer)), &m_draw_ctx.OpaqueSurfaces[i].final_transform, sizeof(glm::mat4));
    }

    m_localMatricesBuffer->Unmap(0, nullptr);
    //> create local matrix buffer
}

void GltfModel::TransitionResrouceStateFromCopyToGraphics(ID3D12GraphicsCommandList* pp_direct_cmd_list)
{
    // note: the caller have to make sure copy has finished, or else data racing happen.
    for (auto& tex_image : m_texturesImages) {
        pp_direct_cmd_list->ResourceBarrier(1,
            &CD3DX12_RESOURCE_BARRIER::Transition(tex_image.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE));
    }
}

}
