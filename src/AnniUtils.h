#pragma once
#include "d3dx12.h"
#include "glm/gtx/compatibility.hpp"
#include <cassert>
#include <dxcapi.h>
#include <dxgi.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <ranges>
#include <stdexcept>
#include <vector>
#include <wrl/client.h>

namespace Anni {
namespace WRL = Microsoft::WRL;
}

namespace Anni {

namespace DXC {
    WRL::ComPtr<IDxcBlob> LoadFileAsDxcBlob(const std::wstring& filename, IDxcUtils* dxc_utils);

    Microsoft::WRL::ComPtr<IDxcBlob> CompileShader(
        const std::wstring& filename,
        const std::wstring& entryPoint,
        const std::wstring& profile,
        IDxcUtils* dxcUtils,
        IDxcCompiler* dxcCompiler,
        IDxcIncludeHandler* includeHandler);

}

// Constants

constexpr UINT TEMP_LOCAL_MATRICES_CBV_COUNT = 500;
constexpr UINT FRAME_INFLIGHT_COUNT = 2;
constexpr UINT BACKBUFFER_COUNT = 3;
constexpr DXGI_FORMAT BACKBUFFER_FORMAT = DXGI_FORMAT_R8G8B8A8_UNORM;
constexpr DXGI_SWAP_EFFECT SWAP_CHAIN_SWAP_EFFECT = DXGI_SWAP_EFFECT_FLIP_DISCARD;

struct StandardVertex {

    glm::float4 color;

    glm::float3 position;
    float padding0;

    glm::float3 normal;
    float padding1;

    glm::float2 uv;
    float padding2;
    float padding3;

    glm::float3 tangent;
    float padding4;
};

// Common Utils
std::vector<char> ReadFileAsBytes(const std::string& filename);

// Helper functions
void ThrowIfFailed(HRESULT hr);

}
