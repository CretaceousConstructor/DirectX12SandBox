#include "AnniUtils.h"

namespace Anni {

namespace Constants {



    D3D12_INPUT_ELEMENT_DESC StandardVertexDescription[5] = {

        { "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 0,
            D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },

        { "POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 16,
            D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "NORMAL", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 32,
            D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 48,
            D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "TANGENT", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 64,
            D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    };

}

WRL::ComPtr<IDxcBlob> DXC::LoadFileAsDxcBlob(const std::wstring& filename, IDxcUtils* dxc_utils)
{
    const HANDLE h_file = CreateFileW(filename.c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h_file == INVALID_HANDLE_VALUE) {
        throw std::runtime_error("Failed to open file");
    }

    LARGE_INTEGER file_size;
    if (!GetFileSizeEx(h_file, &file_size)) {
        CloseHandle(h_file);
        throw std::runtime_error("Failed to get file size");
    }

    std::vector<char> buffer(file_size.QuadPart);

    DWORD bytes_read;
    if (!ReadFile(h_file, buffer.data(), static_cast<DWORD>(file_size.QuadPart), &bytes_read, nullptr)) {
        CloseHandle(h_file);
        throw std::runtime_error("Failed to read file");
    }
    CloseHandle(h_file);

    WRL::ComPtr<IDxcBlobEncoding> source_blob;
    const HRESULT hr = dxc_utils->CreateBlob(buffer.data(), buffer.size(), CP_ACP, &source_blob);
    if (FAILED(hr)) {
        throw std::runtime_error("Failed to create DXC blob");
    }

    return source_blob;
}

Microsoft::WRL::ComPtr<IDxcBlob> DXC::CompileShader(const std::wstring& filename, const std::wstring& entryPoint, const std::wstring& profile, IDxcUtils* dxcUtils, IDxcCompiler* dxcCompiler, IDxcIncludeHandler* includeHandler)
{
	const WRL::ComPtr<IDxcBlob> source_blob = LoadFileAsDxcBlob(filename, dxcUtils);

    // Define compiler arguments
    std::vector<LPCWSTR> arguments = {
        L"/T", profile.c_str(),
        L"/E", entryPoint.c_str(),
        L"/Fo", (filename + L".cso").c_str(),
		L"/Zi", L" ",   //for debug info
		L"/Od", L" ",   //for debug info
    };

    WRL::ComPtr<IDxcOperationResult> result;
    HRESULT hr = dxcCompiler->Compile(
        source_blob.Get(),
        filename.c_str(),
        entryPoint.c_str(),
        profile.c_str(),
        arguments.data(),
        static_cast<uint32_t>(arguments.size()),
        nullptr, // No extra arguments
        0, // No extra arguments count
        includeHandler,
        result.ReleaseAndGetAddressOf());

    if (FAILED(hr)) {
        throw std::runtime_error("Failed to compile shader");
    }

    WRL::ComPtr<IDxcBlob> blob;
    WRL::ComPtr<IDxcBlob> errors;

    if (SUCCEEDED(hr))
    {
        result->GetStatus(&hr);
    }
    if (FAILED(hr)) {
        if (result) {
            WRL::ComPtr<IDxcBlobEncoding> errorsBlob;
            hr = result->GetErrorBuffer(errorsBlob.ReleaseAndGetAddressOf());
            if (SUCCEEDED(hr) && errorsBlob) {
                wprintf(L"Compilation failed with errors:\n%hs\n",
                    (const char*)errorsBlob->GetBufferPointer());
            }
        }
        // Handle compilation error...
        assert(false);
    }

    hr = result->GetResult(blob.ReleaseAndGetAddressOf());
    if (FAILED(hr)) {
        assert(false);
    }

    return blob;
}

std::vector<char> ReadFileAsBytes(const std::string& filename)
{
    std::ifstream file(filename, std::ios::ate | std::ios::binary);

    // const bool exists = (bool)file;
    if (!file || !file.is_open()) {
        throw std::runtime_error("failed to open file!");
    }

    const size_t fileSize = file.tellg();
    std::vector<char> buffer(fileSize);

    file.seekg(0);
    file.read(buffer.data(), fileSize);

    file.close();

    return buffer;
}

void ThrowIfFailed(HRESULT hr)
{
    if (FAILED(hr)) {
        throw std::exception();
    }
}

}
