#pragma once

#include "AnniMath.h"
#include "AnniUtils.h"
#include <fastgltf/glm_element_traits.hpp>
#include <fastgltf/parser.hpp>
#include <fastgltf/tools.hpp>
#include <fastgltf/types.hpp>
#include <fastgltf/util.hpp>
#include <unordered_map>
#include <codecvt>

namespace Anni {

struct MeshAsset // 所有三角形的集合（比如模型的一个部件，比如一个车轮，一个引擎），这些三角形公用一个vertex
                 // buffer和index buffer
{
    struct GeoSurface // 同材质所有的三角形的集合
    {
        uint32_t startIndex;
        uint32_t count;
        uint32_t materialIndex; // 材质的index，最重要的索引
    };

    struct GPUMeshBuffers // 一个mesh用到的所有buffer
    {
        WRL::ComPtr<ID3D12Resource> m_vertexBuffer;
        WRL::ComPtr<ID3D12Resource> m_indexBuffer;
        WRL::ComPtr<ID3D12Resource> m_vertexBufferUpload;
        WRL::ComPtr<ID3D12Resource> m_indexBufferUpload;

        D3D12_VERTEX_BUFFER_VIEW m_vertexBufferView;
        D3D12_INDEX_BUFFER_VIEW m_indexBufferView;

        std::vector<uint32_t> m_cpuIndices;
        std::vector<StandardVertex> m_cpuVertices;
    };

    // holds the resources needed for a mesh_asset
    std::string name;
    std::vector<GeoSurface> surfaces; // 同材质三角形集合
    GPUMeshBuffers mesh_buffers; // all buffer
};

// 用来记录最终的渲染
struct RenderObject {
    uint32_t index_count;
    uint32_t first_index;

    glm::mat4 final_transform;
    uint32_t material_index;

    D3D12_VERTEX_BUFFER_VIEW vertex_buffer_view;
    D3D12_INDEX_BUFFER_VIEW index_buffer_view;
};

struct DrawContext {
    std::vector<RenderObject> OpaqueSurfaces;
    // TODO: process TransparentSurfaces
    std::vector<RenderObject> TransparentSurfaces;
};

class IRenderable {
public:
    IRenderable() = default;

    virtual void Draw(const glm::mat4& top_matrix, DrawContext& ctx) = 0;
    virtual ~IRenderable() = default;
};

struct Node : public IRenderable {

public:
    Node()
        : IRenderable()
    {
    }
    virtual ~Node() override = default;

    // parent pointer must be a weak pointer to avoid circular dependencies
    std::weak_ptr<Node> parent;
    std::vector<std::shared_ptr<Node>> children;

    glm::mat4 local_transform;
    glm::mat4 world_transform;

    void RefreshTransform(const glm::mat4& parent_matrix)
    {

        world_transform = parent_matrix * local_transform;
        for (const auto c : children) {
            c->RefreshTransform(world_transform);
        }
    }

    virtual void Draw(const glm::mat4& top_matrix, DrawContext& ctx) override
    {
        // draw children
        for (const auto& c : children) {
            c->Draw(top_matrix, ctx);
        }
    }
};

struct MeshNode : public Node {
public:
    MeshNode() = default;

    virtual ~MeshNode() override = default;

    // observer pointer
    const MeshAsset* mesh_asset;
    virtual void Draw(const glm::mat4& top_matrix, DrawContext& ctx) override
    {
        const glm::mat4 node_matrix = top_matrix * world_transform;

        for (auto& s : mesh_asset->surfaces) {
            RenderObject def;
            def.index_count = s.count;
            def.first_index = s.startIndex;
            def.index_buffer_view = mesh_asset->mesh_buffers.m_indexBufferView;
            def.vertex_buffer_view = mesh_asset->mesh_buffers.m_vertexBufferView;
            def.material_index = s.materialIndex;
            def.final_transform = node_matrix;

            // if (s.material->data.passType == MaterialPassType::Transparent)
            //{
            //	ctx.TransparentSurfaces.push_back(def);
            // }
            // else
            {
                ctx.OpaqueSurfaces.push_back(def);
            }
        }

        // recurse down(call Draw() as Node(e.g. base class's virtual function), which iterate the Node's all children Nodes.)
        Node::Draw(top_matrix, ctx);
    }
};

// cbv srv uav heap 排布
// 所有材质的 srv
// 所有material const buffer(structured buffer)的 srv
// 所有loca matrices buffer的 cbv

// sampler heap 排布
// 所有的sampler

class GltfModel : IRenderable {
public:
    DrawContext m_draw_ctx;

public:
    void LoadFromFile(std::string gltf_file_path);
    void TransitionResrouceStateFromCopyToGraphics(ID3D12GraphicsCommandList* pp_direct_cmd_list);
    void Draw(const glm::mat4& top_matrix, DrawContext& ctx) final;

    UINT32 GetNumberOfSamplers() const;
    UINT32 GetNumberOfTextures() const;
    UINT32 GetNumberOfMaterial() const;
    UINT32 GetNumberOfMatricesRenderObjects() const;

    CD3DX12_GPU_DESCRIPTOR_HANDLE GetGPUDescHandleToMaterialConstantsBuffer() const;
    CD3DX12_GPU_DESCRIPTOR_HANDLE GetGPUDescHandleToLocalMatricesBuffer() const;
    CD3DX12_GPU_DESCRIPTOR_HANDLE GetGPUDescHandleToTexturesTable() const;
    CD3DX12_GPU_DESCRIPTOR_HANDLE GetGPUDescHandleToSamplers() const;

    void CopyAllDescriptorsTo(
        ID3D12DescriptorHeap* shader_visible_cbv_srv_uva_heap,
        ID3D12DescriptorHeap* shader_visible_sampler_heap,
        CD3DX12_CPU_DESCRIPTOR_HANDLE* dest_shader_visible_cbv_srv_uav_heap_handle,
        CD3DX12_CPU_DESCRIPTOR_HANDLE* dest_shader_visible_sampler_heap_handle);

    GltfModel(ID3D12Device* pp_device, ID3D12GraphicsCommandList* pp_copy_cmd_list);
    GltfModel() = delete;
    ~GltfModel();

    // void CopyAllSamplerDescriptorsTo(ID3D12DescriptorHeap* dest_shader_visible_heap, UINT offset_in_dest_descriptor) const
    //{
    //     // Define the number of descriptors and descriptor handles
    //     std::vector<D3D12_CPU_DESCRIPTOR_HANDLE> srcHandles(m_num_samplers);
    //     std::vector<D3D12_CPU_DESCRIPTOR_HANDLE> destHandles(m_num_samplers);

    //    for (UINT i = 0; i < m_num_samplers; ++i) {
    //        srcHandles[i] = m_samplerHeapShaderVisible->GetCPUDescriptorHandleForHeapStart();
    //        destHandles[i] = dest_shader_visible_heap->GetCPUDescriptorHandleForHeapStart();
    //        srcHandles[i].ptr += (i * m_samplerDescriptorSize);
    //        destHandles[i].ptr += ((i + offset_in_dest_descriptor) * m_samplerDescriptorSize);
    //    }

    //    // Copy descriptors
    //    m_pp_device->CopyDescriptors(
    //        m_num_samplers, // Number of destination descriptor ranges
    //        destHandles.data(), // Destination descriptor handles
    //        nullptr, // Destination sizes (optional)
    //        m_num_samplers, // Number of source descriptor ranges
    //        srcHandles.data(), // Source descriptor handles
    //        nullptr, // Source sizes (optional)
    //        D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER // Descriptor heap type
    //    );
    //}

    // void CopyAllSRVDescriptorsTo(ID3D12DescriptorHeap* dest_shader_visible_heap, UINT offset_in_dest_descriptor) const
    //{
    //     const auto num_textures = GetNumberOfTextures();

    //    // Define the number of descriptors and descriptor handles
    //    std::vector<D3D12_CPU_DESCRIPTOR_HANDLE> srcHandles(num_textures);
    //    std::vector<D3D12_CPU_DESCRIPTOR_HANDLE> destHandles(num_textures);

    //    // Initialize descriptor handles (for illustration; normally you'd get these from the heaps)
    //    for (UINT i = 0; i < num_textures; ++i) {
    //        srcHandles[i] = m_cbvSrvUavHeapShaderVisible->GetCPUDescriptorHandleForHeapStart();
    //        destHandles[i] = dest_shader_visible_heap->GetCPUDescriptorHandleForHeapStart();
    //        srcHandles[i].ptr += (i * m_samplerDescriptorSize);
    //        destHandles[i].ptr += ((i + offset_in_dest_descriptor) * m_samplerDescriptorSize);
    //    }

    //    // Copy descriptors
    //    m_pp_device->CopyDescriptors(
    //        num_textures, // Number of destination descriptor ranges
    //        destHandles.data(), // Destination descriptor handles
    //        nullptr, // Destination sizes (optional)
    //        num_textures, // Number of source descriptor ranges
    //        srcHandles.data(), // Source descriptor handles
    //        nullptr, // Source sizes (optional)
    //        D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV // Descriptor heap type
    //    );
    //}

    // void CopyAllCBVDescriptorsTo(ID3D12DescriptorHeap* dest_shader_visible_heap, UINT offset_in_dest_descriptor) const
    //{
    //     constexpr auto num_cb = m_num_material_views;

    //    // Define the number of descriptors and descriptor handles
    //    std::vector<D3D12_CPU_DESCRIPTOR_HANDLE> srcHandles(num_cb);
    //    std::vector<D3D12_CPU_DESCRIPTOR_HANDLE> destHandles(num_cb);

    //    // Initialize descriptor handles (for illustration; normally you'd get these from the heaps)
    //    for (UINT i = 0; i < num_cb; ++i) {
    //        srcHandles[i] = m_cbvSrvUavHeapShaderVisible->GetCPUDescriptorHandleForHeapStart();
    //        destHandles[i] = dest_shader_visible_heap->GetCPUDescriptorHandleForHeapStart();
    //        srcHandles[i].ptr += ((i + m_texturesImages.size()) * m_samplerDescriptorSize);
    //        destHandles[i].ptr += ((i + offset_in_dest_descriptor) * m_samplerDescriptorSize);
    //    }

    //    // Copy descriptors
    //    m_pp_device->CopyDescriptors(
    //        num_cb, // Number of destination descriptor ranges
    //        destHandles.data(), // Destination descriptor handles
    //        nullptr, // Destination sizes (optional)
    //        num_cb, // Number of source descriptor ranges
    //        srcHandles.data(), // Source descriptor handles
    //        nullptr, // Source sizes (optional)
    //        D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV // Descriptor heap type
    //    );
    //}

private:
    static D3D12_FILTER ExtractFilterAndMipMapMode(const fastgltf::Filter min_filter,
        const fastgltf::Filter mag_filter,
        const fastgltf::Filter mip_map_mode);

    static D3D12_TEXTURE_ADDRESS_MODE ExtractAddressMode(const fastgltf::Wrap warp);

    struct MaterialConstants {
        glm::vec4 colorFactors;
        glm::vec4 metalRoughFactors;

        INT32 albedoIndex { -1 };
        INT32 albedoSamplerIndex { -1 };

        INT32 metalRoughIndex { -1 };
        INT32 metalRoughSamplerIndex { -1 };

        INT32 normalIndex { -1 };
        INT32 normalSamplerIndex { -1 };

        INT32 emissiveIndex { -1 };
        INT32 emissiveSamplerIndex { -1 };

        INT32 occlusionIndex { -1 };
        INT32 occlusionSamplerIndex { -1 };
        INT32 padding0;
        INT32 padding1;
        //glm::vec4 extra[11];
    };

private:
    UINT m_num_samplers;
    UINT m_num_material_views;

private:
    UINT m_samplerDescriptorSize;
    UINT m_cbvSrvUavDescriptorSize;

private:
    // Observer pointer of device
    ID3D12Device* m_pp_device;

private:
    // For copy command list(observer pointer)
    ID3D12GraphicsCommandList* m_copyCommandList;

    // CPU readable sampler heap
    WRL::ComPtr<ID3D12DescriptorHeap> m_samplerHeap;
    // CPU readable cbv srv uav heap
    WRL::ComPtr<ID3D12DescriptorHeap> m_cbvSrvUavHeap;

    // Texture Images
    std::vector<unsigned char*>  m_4_com_tex_temp_buffers;

    std::vector<WRL::ComPtr<ID3D12Resource>> m_texturesImages;
    std::vector<WRL::ComPtr<ID3D12Resource>> m_textureImageUploads;
    std::unordered_map<std::string, ID3D12Resource*> m_namesToTextures;


    // Material Constants Buffer
    WRL::ComPtr<ID3D12Resource> m_materialConstBuffer;
    CD3DX12_CPU_DESCRIPTOR_HANDLE m_materialConstantDataBuffer;
    UINT8* materialConstBufferMappedGPUAddress;

    // Local Matrices Buffer
    WRL::ComPtr<ID3D12Resource> m_localMatricesBuffer;
    CD3DX12_CPU_DESCRIPTOR_HANDLE m_localMatricesDataBuffer;
    UINT8* localMatricesBufferMappedGPUAddress;

    // Nodes
    std::vector<std::shared_ptr<Node>> m_nodes;

    // Meshes
    std::unique_ptr<MeshAsset[]> m_meshes;

    // Array of observer pointers
    std::vector<Node*> m_topNodes;

    // Installed GPU descriptor handle
    CD3DX12_GPU_DESCRIPTOR_HANDLE m_gpu_desc_handle_to_texture_srvs;
    CD3DX12_GPU_DESCRIPTOR_HANDLE m_gpu_desc_handle_to_mat_consts_srvs;
    CD3DX12_GPU_DESCRIPTOR_HANDLE m_gpu_desc_handle_to_local_matrices_cbv;
    CD3DX12_GPU_DESCRIPTOR_HANDLE m_gpu_desc_handle_to_sampler;

};

}
