#include "render/ShaderCompiler.hpp"

#include <algorithm>
#include <cstring>
#include <cwchar>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <optional>
#include <sstream>
#include <vector>

#include <Windows.h>
#include <d3dcompiler.h>
#include <d3d12shader.h>
#include <dxcapi.h>
#include <oleauto.h>
#include <wrl/client.h>

#include <spdlog/spdlog.h>

namespace {
using Microsoft::WRL::ComPtr;

std::wstring to_wstring(const std::string& value) {
    if (value.empty()) {
        return {};
    }

    const auto size = MultiByteToWideChar(CP_UTF8, 0, value.c_str(), static_cast<int>(value.size()), nullptr, 0);
    if (size <= 0) {
        return std::wstring{value.begin(), value.end()};
    }

    std::wstring out(static_cast<size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, value.c_str(), static_cast<int>(value.size()), out.data(), size);
    return out;
}

std::string hr_to_string(HRESULT hr) {
    std::ostringstream ss{};
    ss << "HRESULT 0x" << std::hex << std::uppercase << static_cast<uint32_t>(hr);
    return ss.str();
}

uint32_t read_u32_le(const uint8_t* data) {
    uint32_t value{};
    std::memcpy(&value, data, sizeof(value));
    return value;
}

std::string bytes_to_hex(const uint8_t* data, size_t size) {
    std::ostringstream ss{};
    ss << std::hex << std::setfill('0');
    for (size_t i = 0; i < size; ++i) {
        ss << std::setw(2) << static_cast<unsigned>(data[i]);
    }
    return ss.str();
}

std::string fourcc_to_string(const uint8_t* data) {
    std::string out(4, '\0');
    for (size_t i = 0; i < 4; ++i) {
        const char ch = static_cast<char>(data[i]);
        out[i] = (ch >= 32 && ch <= 126) ? ch : '?';
    }
    return out;
}

int shader_model_major(std::string_view profile) {
    const auto underscore = profile.find('_');
    if (underscore == std::string_view::npos || underscore + 1 >= profile.size()) {
        return 0;
    }

    const auto major_char = profile[underscore + 1];
    if (major_char < '0' || major_char > '9') {
        return 0;
    }

    return major_char - '0';
}

using DxcCreateInstanceProc = HRESULT(WINAPI*)(REFCLSID, REFIID, LPVOID*);

struct DxcRuntime {
    std::once_flag init_once{};
    HMODULE dxil_module{};
    HMODULE dxcompiler_module{};
    DxcCreateInstanceProc create_instance{};
    ComPtr<IDxcUtils> utils{};
    ComPtr<IDxcCompiler3> compiler{};
    std::filesystem::path loaded_from{};
    std::string failure_reason{};

    ~DxcRuntime() {
        compiler.Reset();
        utils.Reset();

        if (dxcompiler_module != nullptr) {
            FreeLibrary(dxcompiler_module);
        }

        if (dxil_module != nullptr) {
            FreeLibrary(dxil_module);
        }
    }

    static DxcRuntime& instance() {
        static DxcRuntime runtime{};
        return runtime;
    }

    bool ensure_loaded() {
        std::call_once(init_once, [this] { load(); });
        return compiler != nullptr && utils != nullptr;
    }

    HRESULT create(REFCLSID clsid, REFIID riid, void** out) {
        if (out == nullptr) {
            return E_POINTER;
        }

        *out = nullptr;
        if (!ensure_loaded() || create_instance == nullptr) {
            return E_FAIL;
        }

        return create_instance(clsid, riid, out);
    }

    std::vector<std::filesystem::path> candidate_directories() {
        std::vector<std::filesystem::path> dirs{};

        wchar_t module_path[MAX_PATH]{};
        const auto module_len = GetModuleFileNameW(nullptr, module_path, MAX_PATH);
        if (module_len > 0) {
            dirs.emplace_back(std::filesystem::path(std::wstring_view{module_path, module_len}).parent_path());
        }

        wchar_t env_buffer[32768]{};
        if (const auto env_len = GetEnvironmentVariableW(L"UEVR_DXC_PATH", env_buffer, std::size(env_buffer)); env_len > 0 && env_len < std::size(env_buffer)) {
            auto env_path = std::filesystem::path(std::wstring_view{env_buffer, env_len});
            dirs.emplace_back(std::filesystem::is_directory(env_path) ? env_path : env_path.parent_path());
        }

        const std::filesystem::path windows_kits_root{L"C:\\Program Files (x86)\\Windows Kits\\10"};
        const auto bin_root = windows_kits_root / "bin";
        if (std::filesystem::exists(bin_root)) {
            std::vector<std::filesystem::path> version_dirs{};
            for (const auto& entry : std::filesystem::directory_iterator(bin_root)) {
                if (!entry.is_directory()) {
                    continue;
                }

                version_dirs.emplace_back(entry.path());
            }

            std::sort(version_dirs.begin(), version_dirs.end(), std::greater<>{});
            for (const auto& version_dir : version_dirs) {
                dirs.emplace_back(version_dir / "x64");
            }
        }

        const auto redist_root = windows_kits_root / "Redist" / "D3D";
        if (std::filesystem::exists(redist_root)) {
            dirs.emplace_back(redist_root / "x64");
            std::vector<std::filesystem::path> redist_version_dirs{};
            for (const auto& entry : std::filesystem::directory_iterator(redist_root)) {
                if (!entry.is_directory()) {
                    continue;
                }
                redist_version_dirs.emplace_back(entry.path());
            }

            std::sort(redist_version_dirs.begin(), redist_version_dirs.end(), std::greater<>{});
            for (const auto& version_dir : redist_version_dirs) {
                dirs.emplace_back(version_dir / "x64");
            }
        }

        dirs.erase(std::remove_if(dirs.begin(), dirs.end(), [](const auto& dir) {
            return dir.empty();
        }), dirs.end());
        dirs.erase(std::unique(dirs.begin(), dirs.end()), dirs.end());

        return dirs;
    }

    void load() {
        for (const auto& dir : candidate_directories()) {
            const auto dxcompiler_path = dir / "dxcompiler.dll";
            if (!std::filesystem::exists(dxcompiler_path)) {
                continue;
            }

            const auto dxil_path = dir / "dxil.dll";
            if (std::filesystem::exists(dxil_path)) {
                dxil_module = LoadLibraryW(dxil_path.c_str());
            }

            dxcompiler_module = LoadLibraryW(dxcompiler_path.c_str());
            if (dxcompiler_module == nullptr) {
                if (dxil_module != nullptr) {
                    FreeLibrary(dxil_module);
                    dxil_module = nullptr;
                }
                continue;
            }

            create_instance = reinterpret_cast<DxcCreateInstanceProc>(GetProcAddress(dxcompiler_module, "DxcCreateInstance"));
            if (create_instance == nullptr) {
                failure_reason = "dxcompiler.dll is missing DxcCreateInstance";
                FreeLibrary(dxcompiler_module);
                dxcompiler_module = nullptr;
                if (dxil_module != nullptr) {
                    FreeLibrary(dxil_module);
                    dxil_module = nullptr;
                }
                continue;
            }

            HRESULT hr = create_instance(CLSID_DxcUtils, IID_PPV_ARGS(&utils));
            if (FAILED(hr)) {
                failure_reason = "Failed to create IDxcUtils: " + hr_to_string(hr);
                FreeLibrary(dxcompiler_module);
                dxcompiler_module = nullptr;
                if (dxil_module != nullptr) {
                    FreeLibrary(dxil_module);
                    dxil_module = nullptr;
                }
                utils.Reset();
                continue;
            }

            hr = create_instance(CLSID_DxcCompiler, IID_PPV_ARGS(&compiler));
            if (FAILED(hr)) {
                failure_reason = "Failed to create IDxcCompiler3: " + hr_to_string(hr);
                compiler.Reset();
                utils.Reset();
                FreeLibrary(dxcompiler_module);
                dxcompiler_module = nullptr;
                if (dxil_module != nullptr) {
                    FreeLibrary(dxil_module);
                    dxil_module = nullptr;
                }
                continue;
            }

            loaded_from = dxcompiler_path;
            failure_reason.clear();
            spdlog::info("[ShaderCompiler] DXC loaded from: {}", dxcompiler_path.string());
            return;
        }

        failure_reason = failure_reason.empty() ? "DXC runtime not found" : failure_reason;
        spdlog::error("[ShaderCompiler] DXC NOT LOADED: {}", failure_reason);
    }
};

uint32_t fourcc_value(std::string_view fourcc) {
    if (fourcc.size() != 4) {
        return 0;
    }

    return static_cast<uint32_t>(static_cast<uint8_t>(fourcc[0])) |
        (static_cast<uint32_t>(static_cast<uint8_t>(fourcc[1])) << 8) |
        (static_cast<uint32_t>(static_cast<uint8_t>(fourcc[2])) << 16) |
        (static_cast<uint32_t>(static_cast<uint8_t>(fourcc[3])) << 24);
}

std::string bstr_to_utf8(BSTR value) {
    if (value == nullptr) {
        return {};
    }

    const auto chars = SysStringLen(value);
    if (chars == 0) {
        return {};
    }

    const auto required = WideCharToMultiByte(CP_UTF8, 0, value, static_cast<int>(chars), nullptr, 0, nullptr, nullptr);
    if (required <= 0) {
        return {};
    }

    std::string out(static_cast<size_t>(required), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value, static_cast<int>(chars), out.data(), required, nullptr, nullptr);
    return out;
}

bool create_blob_from_bytes(
    DxcRuntime& runtime,
    const void* data,
    size_t size,
    UINT32 code_page,
    ComPtr<IDxcBlobEncoding>& blob,
    std::string& error_out
) {
    if (data == nullptr || size == 0) {
        error_out = "Input blob is empty";
        return false;
    }

    if (size > UINT32_MAX) {
        error_out = "Input blob is larger than DXC's 32-bit blob API limit";
        return false;
    }

    if (!runtime.ensure_loaded()) {
        error_out = runtime.failure_reason;
        return false;
    }

    const auto hr = runtime.utils->CreateBlob(data, static_cast<UINT32>(size), code_page, &blob);
    if (FAILED(hr) || blob == nullptr) {
        error_out = "DXC failed to create blob: " + hr_to_string(hr);
        return false;
    }

    return true;
}

bool read_operation_result(
    IDxcOperationResult* op,
    std::vector<uint8_t>& out,
    std::string& error_out
) {
    if (op == nullptr) {
        error_out = "DXC operation returned no result object";
        return false;
    }

    HRESULT status = E_FAIL;
    op->GetStatus(&status);

    ComPtr<IDxcBlobEncoding> errors{};
    std::string error_text{};
    if (SUCCEEDED(op->GetErrorBuffer(&errors)) && errors != nullptr && errors->GetBufferPointer() != nullptr && errors->GetBufferSize() > 0) {
        error_text.assign(static_cast<const char*>(errors->GetBufferPointer()), errors->GetBufferSize());
    }

    if (FAILED(status)) {
        error_out = !error_text.empty() ? error_text : ("DXC operation failed: " + hr_to_string(status));
        return false;
    }

    ComPtr<IDxcBlob> result{};
    const auto hr = op->GetResult(&result);
    if (FAILED(hr) || result == nullptr || result->GetBufferPointer() == nullptr || result->GetBufferSize() == 0) {
        error_out = "DXC operation returned no output blob: " + hr_to_string(hr);
        return false;
    }

    const auto* first = static_cast<const uint8_t*>(result->GetBufferPointer());
    out.assign(first, first + result->GetBufferSize());
    return true;
}

bool validate_container_bytes(
    DxcRuntime& runtime,
    std::vector<uint8_t>& bytes,
    std::string& error_out
) {
    if (bytes.empty()) {
        error_out = "Cannot validate an empty shader container";
        return false;
    }

    ComPtr<IDxcBlobEncoding> blob{};
    if (!create_blob_from_bytes(runtime, bytes.data(), bytes.size(), DXC_CP_ACP, blob, error_out)) {
        return false;
    }

    ComPtr<IDxcValidator> validator{};
    auto hr = runtime.create(CLSID_DxcValidator, IID_PPV_ARGS(&validator));
    if (FAILED(hr) || validator == nullptr) {
        error_out = "Failed to create IDxcValidator: " + hr_to_string(hr);
        return false;
    }

    ComPtr<IDxcOperationResult> validate_result{};
    hr = validator->Validate(blob.Get(), DxcValidatorFlags_InPlaceEdit, &validate_result);
    if (FAILED(hr) || validate_result == nullptr) {
        error_out = "DXIL validator call failed: " + hr_to_string(hr);
        return false;
    }

    std::vector<uint8_t> validated{};
    if (!read_operation_result(validate_result.Get(), validated, error_out)) {
        return false;
    }

    bytes = std::move(validated);
    return true;
}

std::string cbuffer_type_to_string(D3D_CBUFFER_TYPE type) {
    switch (type) {
    case D3D_CT_CBUFFER: return "cbuffer";
    case D3D_CT_TBUFFER: return "tbuffer";
    case D3D_CT_INTERFACE_POINTERS: return "interface_pointers";
    case D3D_CT_RESOURCE_BIND_INFO: return "resource_bind_info";
    default: return "unknown";
    }
}

std::string variable_class_to_string(D3D_SHADER_VARIABLE_CLASS cls) {
    switch (cls) {
    case D3D_SVC_SCALAR: return "scalar";
    case D3D_SVC_VECTOR: return "vector";
    case D3D_SVC_MATRIX_ROWS: return "matrix_rows";
    case D3D_SVC_MATRIX_COLUMNS: return "matrix_columns";
    case D3D_SVC_OBJECT: return "object";
    case D3D_SVC_STRUCT: return "struct";
    case D3D_SVC_INTERFACE_CLASS: return "interface_class";
    case D3D_SVC_INTERFACE_POINTER: return "interface_pointer";
    default: return "unknown";
    }
}

std::string variable_type_to_string(D3D_SHADER_VARIABLE_TYPE type) {
    switch (type) {
    case D3D_SVT_VOID: return "void";
    case D3D_SVT_BOOL: return "bool";
    case D3D_SVT_INT: return "int";
    case D3D_SVT_FLOAT: return "float";
    case D3D_SVT_STRING: return "string";
    case D3D_SVT_TEXTURE: return "texture";
    case D3D_SVT_TEXTURE1D: return "texture1d";
    case D3D_SVT_TEXTURE2D: return "texture2d";
    case D3D_SVT_TEXTURE3D: return "texture3d";
    case D3D_SVT_TEXTURECUBE: return "texturecube";
    case D3D_SVT_SAMPLER: return "sampler";
    case D3D_SVT_SAMPLER1D: return "sampler1d";
    case D3D_SVT_SAMPLER2D: return "sampler2d";
    case D3D_SVT_SAMPLER3D: return "sampler3d";
    case D3D_SVT_SAMPLERCUBE: return "samplercube";
    case D3D_SVT_PIXELSHADER: return "pixelshader";
    case D3D_SVT_VERTEXSHADER: return "vertexshader";
    case D3D_SVT_UINT: return "uint";
    case D3D_SVT_UINT8: return "uint8";
    case D3D_SVT_GEOMETRYSHADER: return "geometryshader";
    case D3D_SVT_RASTERIZER: return "rasterizer";
    case D3D_SVT_DEPTHSTENCIL: return "depthstencil";
    case D3D_SVT_BLEND: return "blend";
    case D3D_SVT_BUFFER: return "buffer";
    case D3D_SVT_CBUFFER: return "cbuffer";
    case D3D_SVT_TBUFFER: return "tbuffer";
    case D3D_SVT_TEXTURE1DARRAY: return "texture1darray";
    case D3D_SVT_TEXTURE2DARRAY: return "texture2darray";
    case D3D_SVT_RENDERTARGETVIEW: return "rendertargetview";
    case D3D_SVT_DEPTHSTENCILVIEW: return "depthstencilview";
    case D3D_SVT_TEXTURE2DMS: return "texture2dms";
    case D3D_SVT_TEXTURE2DMSARRAY: return "texture2dmsarray";
    case D3D_SVT_TEXTURECUBEARRAY: return "texturecubearray";
    case D3D_SVT_HULLSHADER: return "hullshader";
    case D3D_SVT_DOMAINSHADER: return "domainshader";
    case D3D_SVT_INTERFACE_POINTER: return "interface_pointer";
    case D3D_SVT_COMPUTESHADER: return "computeshader";
    case D3D_SVT_DOUBLE: return "double";
    case D3D_SVT_RWTEXTURE1D: return "rwtexture1d";
    case D3D_SVT_RWTEXTURE1DARRAY: return "rwtexture1darray";
    case D3D_SVT_RWTEXTURE2D: return "rwtexture2d";
    case D3D_SVT_RWTEXTURE2DARRAY: return "rwtexture2darray";
    case D3D_SVT_RWTEXTURE3D: return "rwtexture3d";
    case D3D_SVT_RWBUFFER: return "rwbuffer";
    case D3D_SVT_BYTEADDRESS_BUFFER: return "byteaddressbuffer";
    case D3D_SVT_RWBYTEADDRESS_BUFFER: return "rwbyteaddressbuffer";
    case D3D_SVT_STRUCTURED_BUFFER: return "structuredbuffer";
    case D3D_SVT_RWSTRUCTURED_BUFFER: return "rwstructuredbuffer";
    case D3D_SVT_APPEND_STRUCTURED_BUFFER: return "appendstructuredbuffer";
    case D3D_SVT_CONSUME_STRUCTURED_BUFFER: return "consumestructuredbuffer";
    case D3D_SVT_MIN8FLOAT: return "min8float";
    case D3D_SVT_MIN10FLOAT: return "min10float";
    case D3D_SVT_MIN16FLOAT: return "min16float";
    case D3D_SVT_MIN12INT: return "min12int";
    case D3D_SVT_MIN16INT: return "min16int";
    case D3D_SVT_MIN16UINT: return "min16uint";
    default: return "unknown";
    }
}

std::string input_type_to_string(D3D_SHADER_INPUT_TYPE type) {
    switch (type) {
    case D3D_SIT_CBUFFER: return "cbv";
    case D3D_SIT_TBUFFER: return "tbuffer";
    case D3D_SIT_TEXTURE: return "srv";
    case D3D_SIT_SAMPLER: return "sampler";
    case D3D_SIT_UAV_RWTYPED: return "uav";
    case D3D_SIT_STRUCTURED: return "srv";
    case D3D_SIT_UAV_RWSTRUCTURED: return "uav";
    case D3D_SIT_BYTEADDRESS: return "srv";
    case D3D_SIT_UAV_RWBYTEADDRESS: return "uav";
    case D3D_SIT_UAV_APPEND_STRUCTURED: return "uav";
    case D3D_SIT_UAV_CONSUME_STRUCTURED: return "uav";
    case D3D_SIT_UAV_RWSTRUCTURED_WITH_COUNTER: return "uav";
    case D3D_SIT_RTACCELERATIONSTRUCTURE: return "acceleration_structure";
    case D3D_SIT_UAV_FEEDBACKTEXTURE: return "uav";
    default: return "unknown";
    }
}

std::string return_type_to_string(D3D_RESOURCE_RETURN_TYPE type) {
    switch (type) {
    case D3D_RETURN_TYPE_UNORM: return "unorm";
    case D3D_RETURN_TYPE_SNORM: return "snorm";
    case D3D_RETURN_TYPE_SINT: return "sint";
    case D3D_RETURN_TYPE_UINT: return "uint";
    case D3D_RETURN_TYPE_FLOAT: return "float";
    case D3D_RETURN_TYPE_MIXED: return "mixed";
    case D3D_RETURN_TYPE_DOUBLE: return "double";
    case D3D_RETURN_TYPE_CONTINUED: return "continued";
    default: return "unknown";
    }
}

std::string srv_dimension_to_string(D3D_SRV_DIMENSION dimension) {
    switch (dimension) {
    case D3D_SRV_DIMENSION_UNKNOWN: return "unknown";
    case D3D_SRV_DIMENSION_BUFFER: return "buffer";
    case D3D_SRV_DIMENSION_TEXTURE1D: return "texture1d";
    case D3D_SRV_DIMENSION_TEXTURE1DARRAY: return "texture1darray";
    case D3D_SRV_DIMENSION_TEXTURE2D: return "texture2d";
    case D3D_SRV_DIMENSION_TEXTURE2DARRAY: return "texture2darray";
    case D3D_SRV_DIMENSION_TEXTURE2DMS: return "texture2dms";
    case D3D_SRV_DIMENSION_TEXTURE2DMSARRAY: return "texture2dmsarray";
    case D3D_SRV_DIMENSION_TEXTURE3D: return "texture3d";
    case D3D_SRV_DIMENSION_TEXTURECUBE: return "texturecube";
    case D3D_SRV_DIMENSION_TEXTURECUBEARRAY: return "texturecubearray";
    case D3D_SRV_DIMENSION_BUFFEREX: return "bufferex";
    default: return "unknown";
    }
}

std::string system_value_to_string(D3D_NAME value) {
    switch (value) {
    case D3D_NAME_UNDEFINED: return "undefined";
    case D3D_NAME_POSITION: return "position";
    case D3D_NAME_CLIP_DISTANCE: return "clip_distance";
    case D3D_NAME_CULL_DISTANCE: return "cull_distance";
    case D3D_NAME_RENDER_TARGET_ARRAY_INDEX: return "render_target_array_index";
    case D3D_NAME_VIEWPORT_ARRAY_INDEX: return "viewport_array_index";
    case D3D_NAME_VERTEX_ID: return "vertex_id";
    case D3D_NAME_PRIMITIVE_ID: return "primitive_id";
    case D3D_NAME_INSTANCE_ID: return "instance_id";
    case D3D_NAME_IS_FRONT_FACE: return "is_front_face";
    case D3D_NAME_SAMPLE_INDEX: return "sample_index";
    case D3D_NAME_FINAL_QUAD_EDGE_TESSFACTOR: return "final_quad_edge_tessfactor";
    case D3D_NAME_FINAL_QUAD_INSIDE_TESSFACTOR: return "final_quad_inside_tessfactor";
    case D3D_NAME_FINAL_TRI_EDGE_TESSFACTOR: return "final_tri_edge_tessfactor";
    case D3D_NAME_FINAL_TRI_INSIDE_TESSFACTOR: return "final_tri_inside_tessfactor";
    case D3D_NAME_FINAL_LINE_DETAIL_TESSFACTOR: return "final_line_detail_tessfactor";
    case D3D_NAME_FINAL_LINE_DENSITY_TESSFACTOR: return "final_line_density_tessfactor";
    case D3D_NAME_TARGET: return "target";
    case D3D_NAME_DEPTH: return "depth";
    case D3D_NAME_COVERAGE: return "coverage";
    case D3D_NAME_DEPTH_GREATER_EQUAL: return "depth_greater_equal";
    case D3D_NAME_DEPTH_LESS_EQUAL: return "depth_less_equal";
    case D3D_NAME_STENCIL_REF: return "stencil_ref";
    case D3D_NAME_INNER_COVERAGE: return "inner_coverage";
    default: return "unknown";
    }
}

std::string component_type_to_string(D3D_REGISTER_COMPONENT_TYPE type) {
    switch (type) {
    case D3D_REGISTER_COMPONENT_UNKNOWN: return "unknown";
    case D3D_REGISTER_COMPONENT_UINT32: return "uint32";
    case D3D_REGISTER_COMPONENT_SINT32: return "sint32";
    case D3D_REGISTER_COMPONENT_FLOAT32: return "float32";
    default: return "unknown";
    }
}

render::ShaderReflectionSignatureParamInfo signature_param_to_info(const D3D12_SIGNATURE_PARAMETER_DESC& desc) {
    render::ShaderReflectionSignatureParamInfo out{};
    out.semantic_name = desc.SemanticName != nullptr ? desc.SemanticName : "";
    out.semantic_index = desc.SemanticIndex;
    out.register_index = desc.Register;
    out.system_value = system_value_to_string(desc.SystemValueType);
    out.component_type = component_type_to_string(desc.ComponentType);
    out.mask = desc.Mask;
    out.read_write_mask = desc.ReadWriteMask;
    out.stream = desc.Stream;
    return out;
}

render::ShaderReflectionInfo reflect_shader_bytecode(const void* bytecode, size_t bytecode_size) {
    render::ShaderReflectionInfo out{};

    ComPtr<ID3D12ShaderReflection> reflection{};
    HRESULT hr = E_FAIL;

    // Prefer DXC for DXIL blobs (SM 6+). If dxcompiler isn't loadable at all,
    // we still want to try D3DReflect (which handles DXBC/SM5 via d3dcompiler)
    // before giving up — otherwise SM5 reflection silently dies on systems
    // without DXC.
    auto& runtime = DxcRuntime::instance();
    std::string dxc_error{};
    if (runtime.ensure_loaded()) {
        const DxcBuffer buffer{
            .Ptr = bytecode,
            .Size = bytecode_size,
            .Encoding = DXC_CP_ACP
        };
        hr = runtime.utils->CreateReflection(&buffer, IID_PPV_ARGS(&reflection));
    } else {
        dxc_error = runtime.failure_reason;
    }

    if (FAILED(hr) || reflection == nullptr) {
        // Fall back to d3dcompiler (DXBC/SM5; also accepts some DXIL on
        // recent d3dcompiler_47 builds).
        reflection.Reset();
        hr = D3DReflect(bytecode, bytecode_size, IID_PPV_ARGS(&reflection));
    }

    if (FAILED(hr) || reflection == nullptr) {
        out.error = "Shader reflection failed: " + hr_to_string(hr);
        if (!dxc_error.empty()) {
            out.error += " (dxc unavailable: " + dxc_error + ")";
        }
        return out;
    }

    D3D12_SHADER_DESC desc{};
    hr = reflection->GetDesc(&desc);
    if (FAILED(hr)) {
        out.error = "ID3D12ShaderReflection::GetDesc failed: " + hr_to_string(hr);
        return out;
    }

    out.ok = true;
    out.creator = desc.Creator != nullptr ? desc.Creator : "";
    out.instruction_count = desc.InstructionCount;
    out.constant_buffer_count = desc.ConstantBuffers;
    out.bound_resource_count = desc.BoundResources;
    out.input_parameter_count = desc.InputParameters;
    out.output_parameter_count = desc.OutputParameters;

    out.constant_buffers.reserve(desc.ConstantBuffers);
    for (UINT i = 0; i < desc.ConstantBuffers; ++i) {
        ID3D12ShaderReflectionConstantBuffer* cbuffer = reflection->GetConstantBufferByIndex(i);
        if (cbuffer == nullptr) {
            continue;
        }

        D3D12_SHADER_BUFFER_DESC buffer_desc{};
        if (FAILED(cbuffer->GetDesc(&buffer_desc))) {
            continue;
        }

        render::ShaderReflectionConstantBufferInfo cbuffer_info{};
        cbuffer_info.name = buffer_desc.Name != nullptr ? buffer_desc.Name : "";
        cbuffer_info.type = cbuffer_type_to_string(buffer_desc.Type);
        cbuffer_info.size = buffer_desc.Size;
        cbuffer_info.variables.reserve(buffer_desc.Variables);

        for (UINT var_index = 0; var_index < buffer_desc.Variables; ++var_index) {
            ID3D12ShaderReflectionVariable* variable = cbuffer->GetVariableByIndex(var_index);
            if (variable == nullptr) {
                continue;
            }

            D3D12_SHADER_VARIABLE_DESC variable_desc{};
            if (FAILED(variable->GetDesc(&variable_desc))) {
                continue;
            }

            render::ShaderReflectionVariableInfo variable_info{};
            variable_info.name = variable_desc.Name != nullptr ? variable_desc.Name : "";
            variable_info.start_offset = variable_desc.StartOffset;
            variable_info.size = variable_desc.Size;
            variable_info.flags = variable_desc.uFlags;

            if (ID3D12ShaderReflectionType* type = variable->GetType(); type != nullptr) {
                D3D12_SHADER_TYPE_DESC type_desc{};
                if (SUCCEEDED(type->GetDesc(&type_desc))) {
                    variable_info.type_name = type_desc.Name != nullptr ? type_desc.Name : "";
                    variable_info.type_class = variable_class_to_string(type_desc.Class);
                    variable_info.type_kind = variable_type_to_string(type_desc.Type);
                    variable_info.rows = type_desc.Rows;
                    variable_info.columns = type_desc.Columns;
                    variable_info.elements = type_desc.Elements;
                    variable_info.members = type_desc.Members;
                }
            }

            cbuffer_info.variables.emplace_back(std::move(variable_info));
        }

        out.constant_buffers.emplace_back(std::move(cbuffer_info));
    }

    out.bound_resources.reserve(desc.BoundResources);
    for (UINT i = 0; i < desc.BoundResources; ++i) {
        D3D12_SHADER_INPUT_BIND_DESC bind_desc{};
        if (FAILED(reflection->GetResourceBindingDesc(i, &bind_desc))) {
            continue;
        }

        render::ShaderReflectionResourceBindingInfo resource{};
        resource.name = bind_desc.Name != nullptr ? bind_desc.Name : "";
        resource.type = input_type_to_string(bind_desc.Type);
        resource.return_type = return_type_to_string(bind_desc.ReturnType);
        resource.dimension = srv_dimension_to_string(bind_desc.Dimension);
        resource.bind_point = bind_desc.BindPoint;
        resource.bind_count = bind_desc.BindCount;
        resource.space = bind_desc.Space;
        resource.flags = bind_desc.uFlags;
        out.bound_resources.emplace_back(std::move(resource));
    }

    out.input_parameters.reserve(desc.InputParameters);
    for (UINT i = 0; i < desc.InputParameters; ++i) {
        D3D12_SIGNATURE_PARAMETER_DESC param{};
        if (SUCCEEDED(reflection->GetInputParameterDesc(i, &param))) {
            out.input_parameters.emplace_back(signature_param_to_info(param));
        }
    }

    out.output_parameters.reserve(desc.OutputParameters);
    for (UINT i = 0; i < desc.OutputParameters; ++i) {
        D3D12_SIGNATURE_PARAMETER_DESC param{};
        if (SUCCEEDED(reflection->GetOutputParameterDesc(i, &param))) {
            out.output_parameters.emplace_back(signature_param_to_info(param));
        }
    }

    return out;
}

std::vector<render::ShaderRecoveredSourceInfo> recover_sources_from_pdb_or_dxil(
    const void* bytecode,
    size_t bytecode_size,
    std::string& error_out
) {
    std::vector<render::ShaderRecoveredSourceInfo> out{};
    auto& runtime = DxcRuntime::instance();
    if (!runtime.ensure_loaded()) {
        error_out = runtime.failure_reason;
        return out;
    }

    ComPtr<IDxcBlobEncoding> blob{};
    if (!create_blob_from_bytes(runtime, bytecode, bytecode_size, DXC_CP_ACP, blob, error_out)) {
        return out;
    }

    ComPtr<IDxcPdbUtils> pdb{};
    auto hr = runtime.create(CLSID_DxcPdbUtils, IID_PPV_ARGS(&pdb));
    if (FAILED(hr) || pdb == nullptr) {
        error_out = "Failed to create IDxcPdbUtils: " + hr_to_string(hr);
        return out;
    }

    hr = pdb->Load(blob.Get());
    if (FAILED(hr)) {
        error_out = "IDxcPdbUtils::Load failed: " + hr_to_string(hr);
        return out;
    }

    UINT32 source_count = 0;
    hr = pdb->GetSourceCount(&source_count);
    if (FAILED(hr)) {
        error_out = "IDxcPdbUtils::GetSourceCount failed: " + hr_to_string(hr);
        return out;
    }

    constexpr size_t MAX_RECOVERED_SOURCE_CHARS = 128 * 1024;
    out.reserve(source_count);
    for (UINT32 i = 0; i < source_count; ++i) {
        render::ShaderRecoveredSourceInfo source{};

        BSTR name = nullptr;
        if (SUCCEEDED(pdb->GetSourceName(i, &name)) && name != nullptr) {
            source.name = bstr_to_utf8(name);
            SysFreeString(name);
        }

        ComPtr<IDxcBlobEncoding> source_blob{};
        if (SUCCEEDED(pdb->GetSource(i, &source_blob)) && source_blob != nullptr &&
            source_blob->GetBufferPointer() != nullptr && source_blob->GetBufferSize() > 0) {
            const size_t take = std::min<size_t>(source_blob->GetBufferSize(), MAX_RECOVERED_SOURCE_CHARS);
            source.text.assign(static_cast<const char*>(source_blob->GetBufferPointer()), take);
            if (take < source_blob->GetBufferSize()) {
                source.text += "\n/* recovered source truncated */\n";
            }
        }

        out.emplace_back(std::move(source));
    }

    return out;
}

render::ShaderCompileResult compile_with_dxc(const render::ShaderCompileRequest& request) {
    render::ShaderCompileResult result{};
    result.compiler = "dxc";

    auto& runtime = DxcRuntime::instance();
    if (!runtime.ensure_loaded()) {
        result.error = runtime.failure_reason;
        return result;
    }

    UINT32 code_page = DXC_CP_UTF8;
    ComPtr<IDxcBlobEncoding> source_blob{};
    auto hr = runtime.utils->LoadFile(request.source_path.c_str(), &code_page, &source_blob);
    if (FAILED(hr) || source_blob == nullptr) {
        result.error = "DXC failed to load " + request.source_path.string() + ": " + hr_to_string(hr);
        return result;
    }

    // arg_storage MUST NOT reallocate while we're capturing c_str() pointers into args.
    // emplace_back() on std::vector<std::wstring> can move all elements to a new buffer,
    // invalidating every previously-stored .c_str() pointer. Symptoms include DXC
    // reporting "invalid profile" (the -T arg pointed at freed memory) or
    // "error reading '<garbled>'" (the source-path arg pointed at freed memory).
    std::vector<std::wstring> arg_storage{};
    arg_storage.reserve(32);
    std::vector<LPCWSTR> args{};
    args.reserve(32);
    auto push_arg = [&](std::wstring value) {
        arg_storage.emplace_back(std::move(value));
        args.emplace_back(arg_storage.back().c_str());
    };

    push_arg(request.source_path.wstring());
    push_arg(L"-E");
    push_arg(to_wstring(request.entry_point));
    push_arg(L"-T");
    push_arg(to_wstring(request.profile));
    push_arg(L"-HV");
    push_arg(L"2021");

    if (request.warnings_as_errors) {
        push_arg(L"-WX");
    }

    if (request.strict_mode) {
        push_arg(L"-Ges");
        push_arg(L"-Zpc");
    }

    if (request.debug_info) {
        push_arg(L"-Zi");
        // Embed the debug info (source, line tables, named bindings) directly in
        // the DXIL container instead of emitting a side PDB. RenderDoc then gets
        // source-level stepping + named entrypoint/bindings for shaders we author,
        // with no PDB-path lookup. Only meaningful when debug isn't stripped.
        if (!request.strip_debug) {
            push_arg(L"-Qembed_debug");
        }
    }

    if (request.strip_reflection) {
        push_arg(L"-Qstrip_reflect");
    }

    if (request.strip_debug) {
        push_arg(L"-Qstrip_debug");
    }

    ComPtr<IDxcIncludeHandler> include_handler{};
    hr = runtime.utils->CreateDefaultIncludeHandler(&include_handler);
    if (FAILED(hr) || include_handler == nullptr) {
        result.error = "DXC failed to create include handler: " + hr_to_string(hr);
        return result;
    }

    const DxcBuffer source_buffer{
        .Ptr = source_blob->GetBufferPointer(),
        .Size = source_blob->GetBufferSize(),
        .Encoding = code_page
    };

    ComPtr<IDxcResult> compile_result{};
    hr = runtime.compiler->Compile(&source_buffer, args.data(), static_cast<uint32_t>(args.size()), include_handler.Get(), IID_PPV_ARGS(&compile_result));
    if (FAILED(hr) || compile_result == nullptr) {
        result.error = "DXC compile call failed: " + hr_to_string(hr);
        return result;
    }

    HRESULT status = E_FAIL;
    compile_result->GetStatus(&status);

    ComPtr<IDxcBlobUtf8> errors{};
    if (SUCCEEDED(compile_result->GetOutput(DXC_OUT_ERRORS, IID_PPV_ARGS(&errors), nullptr)) && errors != nullptr && errors->GetStringLength() > 0) {
        result.notes.assign(errors->GetStringPointer(), errors->GetStringLength());
    }

    if (FAILED(status)) {
        result.error = !result.notes.empty() ? result.notes : ("DXC compile failed: " + hr_to_string(status));
        result.notes.clear();
        return result;
    }

    ComPtr<IDxcBlob> object{};
    hr = compile_result->GetOutput(DXC_OUT_OBJECT, IID_PPV_ARGS(&object), nullptr);
    if (FAILED(hr) || object == nullptr) {
        result.error = "DXC returned no object output: " + hr_to_string(hr);
        return result;
    }

    result.bytecode.assign(
        static_cast<const uint8_t*>(object->GetBufferPointer()),
        static_cast<const uint8_t*>(object->GetBufferPointer()) + object->GetBufferSize()
    );
    result.succeeded = true;
    result.notes = "Loaded DXC from " + runtime.loaded_from.string();
    return result;
}

render::ShaderCompileResult compile_with_fxc(const render::ShaderCompileRequest& request) {
    render::ShaderCompileResult result{};
    result.compiler = "fxc";

    ComPtr<ID3DBlob> shader_blob{};
    ComPtr<ID3DBlob> error_blob{};

    UINT flags = 0;
    if (request.strict_mode) {
        flags |= D3DCOMPILE_ENABLE_STRICTNESS;
    }
    if (request.debug_info) {
        flags |= D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
    } else {
        flags |= D3DCOMPILE_OPTIMIZATION_LEVEL3;
    }
    if (request.warnings_as_errors) {
        flags |= D3DCOMPILE_WARNINGS_ARE_ERRORS;
    }

    const auto hr = D3DCompileFromFile(
        request.source_path.wstring().c_str(),
        nullptr,
        D3D_COMPILE_STANDARD_FILE_INCLUDE,
        request.entry_point.c_str(),
        request.profile.c_str(),
        flags,
        0,
        &shader_blob,
        &error_blob
    );

    if (FAILED(hr) || shader_blob == nullptr) {
        if (error_blob != nullptr && error_blob->GetBufferPointer() != nullptr) {
            result.error.assign(static_cast<const char*>(error_blob->GetBufferPointer()), error_blob->GetBufferSize());
        } else {
            result.error = "FXC compile failed: " + hr_to_string(hr);
        }
        return result;
    }

    result.bytecode.assign(
        static_cast<const uint8_t*>(shader_blob->GetBufferPointer()),
        static_cast<const uint8_t*>(shader_blob->GetBufferPointer()) + shader_blob->GetBufferSize()
    );
    result.succeeded = true;
    return result;
}

} // namespace

namespace render {
ShaderCompileResult compile_shader_file(const ShaderCompileRequest& request) {
    ShaderCompileResult result{};

    if (request.source_path.empty()) {
        result.error = "Shader source path is empty";
        return result;
    }

    const auto major = shader_model_major(request.profile);

    auto try_dxc = [&]() {
        return compile_with_dxc(request);
    };

    auto try_fxc = [&]() {
        return compile_with_fxc(request);
    };

    switch (request.preferred_backend) {
    case ShaderCompilerBackend::Dxc:
        return try_dxc();
    case ShaderCompilerBackend::Fxc:
        return try_fxc();
    case ShaderCompilerBackend::Auto:
    default:
        break;
    }

    if (major >= 6) {
        return try_dxc();
    }

    return try_fxc();
}

ShaderBytecodeInspection inspect_shader_bytecode(
    const void* bytecode,
    size_t bytecode_size,
    bool disassemble,
    size_t max_disassembly_chars
) {
    ShaderBytecodeInspection result{};
    result.bytecode_size = static_cast<uint32_t>(std::min<size_t>(bytecode_size, UINT32_MAX));

    if (bytecode == nullptr || bytecode_size == 0) {
        result.error = "Shader bytecode is empty";
        return result;
    }

    const auto* bytes = static_cast<const uint8_t*>(bytecode);

    if (bytecode_size >= 32 && std::memcmp(bytes, "DXBC", 4) == 0) {
        result.container = true;
        result.container_kind = "DXBC";
        result.container_hash = bytes_to_hex(bytes + 4, 16);
        result.container_version = read_u32_le(bytes + 20);
        result.declared_size = read_u32_le(bytes + 24);
        const auto chunk_count = read_u32_le(bytes + 28);

        for (uint32_t i = 0; i < chunk_count; ++i) {
            const size_t offset_table_pos = 32ull + static_cast<size_t>(i) * sizeof(uint32_t);
            if (offset_table_pos + sizeof(uint32_t) > bytecode_size) {
                result.error = "DXBC chunk offset table is truncated";
                break;
            }

            const auto chunk_offset = read_u32_le(bytes + offset_table_pos);
            if (static_cast<size_t>(chunk_offset) + 8 > bytecode_size) {
                result.chunks.push_back({ "????", chunk_offset, 0 });
                result.error = "DXBC chunk points outside the container";
                continue;
            }

            ShaderContainerChunkInfo chunk{};
            chunk.fourcc = fourcc_to_string(bytes + chunk_offset);
            chunk.offset = chunk_offset;
            chunk.size = read_u32_le(bytes + chunk_offset + 4);
            result.chunks.push_back(std::move(chunk));
        }

        for (const auto& chunk : result.chunks) {
            if (chunk.fourcc == "DXIL") {
                result.container_kind = "DXIL";
                break;
            }
        }
    } else {
        result.container_kind = "raw";
    }

    if (disassemble) {
        auto& runtime = DxcRuntime::instance();
        if (!runtime.ensure_loaded()) {
            result.error = result.error.empty() ? runtime.failure_reason : (result.error + "; " + runtime.failure_reason);
        } else {
            const DxcBuffer buffer{
                .Ptr = bytecode,
                .Size = bytecode_size,
                .Encoding = DXC_CP_ACP
            };

            ComPtr<IDxcResult> disasm_result{};
            const auto hr = runtime.compiler->Disassemble(&buffer, IID_PPV_ARGS(&disasm_result));
            if (FAILED(hr) || disasm_result == nullptr) {
                const auto err = "DXC disassemble call failed: " + hr_to_string(hr);
                result.error = result.error.empty() ? err : (result.error + "; " + err);
            } else {
                HRESULT status = E_FAIL;
                disasm_result->GetStatus(&status);

                ComPtr<IDxcBlobUtf8> errors{};
                std::string error_text{};
                if (SUCCEEDED(disasm_result->GetOutput(DXC_OUT_ERRORS, IID_PPV_ARGS(&errors), nullptr)) &&
                    errors != nullptr &&
                    errors->GetStringLength() > 0) {
                    error_text.assign(errors->GetStringPointer(), errors->GetStringLength());
                }

                ComPtr<IDxcBlobUtf8> disassembly{};
                const auto out_hr = disasm_result->GetOutput(DXC_OUT_DISASSEMBLY, IID_PPV_ARGS(&disassembly), nullptr);
                if (FAILED(status) || FAILED(out_hr) || disassembly == nullptr) {
                    const auto err = !error_text.empty()
                        ? error_text
                        : ("DXC disassemble failed: " + hr_to_string(FAILED(status) ? status : out_hr));
                    result.error = result.error.empty() ? err : (result.error + "; " + err);
                } else {
                    const size_t take = std::min<size_t>(disassembly->GetStringLength(), max_disassembly_chars);
                    result.disassembly.assign(disassembly->GetStringPointer(), take);
                    if (take < disassembly->GetStringLength()) {
                        result.disassembly += "\n/* disassembly truncated */\n";
                    }
                    result.compiler = "dxc:" + runtime.loaded_from.string();
                }
            }
        }
    }

    if (result.container && result.container_kind == "DXIL") {
        std::string pdb_error{};
        result.recovered_sources = recover_sources_from_pdb_or_dxil(bytecode, bytecode_size, pdb_error);
        if (!pdb_error.empty() && !result.error.empty()) {
            result.error += "; PDB/source recovery: " + pdb_error;
        }
    }

    result.reflection = reflect_shader_bytecode(bytecode, bytecode_size);

    result.ok = result.error.empty() || !result.chunks.empty() || !result.disassembly.empty();
    return result;
}

ShaderContainerEditResult edit_shader_container(const ShaderContainerEditRequest& request) {
    ShaderContainerEditResult result{};
    result.compiler = "dxc-container-builder";

    auto& runtime = DxcRuntime::instance();
    if (!runtime.ensure_loaded()) {
        result.error = runtime.failure_reason;
        return result;
    }

    ComPtr<IDxcBlobEncoding> input_blob{};
    if (!create_blob_from_bytes(runtime, request.bytecode, request.bytecode_size, DXC_CP_ACP, input_blob, result.error)) {
        return result;
    }

    ComPtr<IDxcContainerBuilder> builder{};
    auto hr = runtime.create(CLSID_DxcContainerBuilder, IID_PPV_ARGS(&builder));
    if (FAILED(hr) || builder == nullptr) {
        result.error = "Failed to create IDxcContainerBuilder: " + hr_to_string(hr);
        return result;
    }

    hr = builder->Load(input_blob.Get());
    if (FAILED(hr)) {
        result.error = "IDxcContainerBuilder::Load failed: " + hr_to_string(hr);
        return result;
    }

    for (const auto& edit : request.edits) {
        const auto fourcc = fourcc_value(edit.fourcc);
        if (fourcc == 0) {
            result.error = "Invalid container fourcc: " + edit.fourcc;
            return result;
        }

        if (edit.remove) {
            hr = builder->RemovePart(fourcc);
            if (FAILED(hr) && HRESULT_CODE(hr) != ERROR_NOT_FOUND) {
                result.error = "IDxcContainerBuilder::RemovePart(" + edit.fourcc + ") failed: " + hr_to_string(hr);
                return result;
            }
            continue;
        }

        ComPtr<IDxcBlobEncoding> part_blob{};
        if (!create_blob_from_bytes(runtime, edit.data.data(), edit.data.size(), DXC_CP_ACP, part_blob, result.error)) {
            result.error = "Failed to create replacement part " + edit.fourcc + ": " + result.error;
            return result;
        }

        hr = builder->RemovePart(fourcc);
        if (FAILED(hr) && HRESULT_CODE(hr) != ERROR_NOT_FOUND) {
            result.error = "IDxcContainerBuilder::RemovePart(" + edit.fourcc + ") failed before replacement: " + hr_to_string(hr);
            return result;
        }

        hr = builder->AddPart(fourcc, part_blob.Get());
        if (FAILED(hr)) {
            result.error = "IDxcContainerBuilder::AddPart(" + edit.fourcc + ") failed: " + hr_to_string(hr);
            return result;
        }
    }

    ComPtr<IDxcOperationResult> serialize_result{};
    hr = builder->SerializeContainer(&serialize_result);
    if (FAILED(hr) || serialize_result == nullptr) {
        result.error = "IDxcContainerBuilder::SerializeContainer failed: " + hr_to_string(hr);
        return result;
    }

    if (!read_operation_result(serialize_result.Get(), result.bytecode, result.error)) {
        return result;
    }

    if (request.validate_and_sign && !validate_container_bytes(runtime, result.bytecode, result.error)) {
        return result;
    }

    result.succeeded = true;
    return result;
}

ShaderContainerEditResult patch_dxil_text(const ShaderDxilTextPatchRequest& request) {
    ShaderContainerEditResult result{};
    result.compiler = "dxc-assembler";

    if (request.patches.empty()) {
        result.error = "DXIL text patch contains no replacements";
        return result;
    }

    auto inspection = inspect_shader_bytecode(
        request.bytecode,
        request.bytecode_size,
        true,
        request.max_disassembly_chars);
    if (inspection.disassembly.empty()) {
        result.error = inspection.error.empty() ? "DXC produced no disassembly for text patching" : inspection.error;
        return result;
    }

    std::string patched = std::move(inspection.disassembly);
    for (const auto& patch : request.patches) {
        if (patch.find.empty()) {
            result.error = "DXIL text patch has an empty find string";
            return result;
        }

        size_t replaced = 0;
        size_t pos = 0;
        while ((pos = patched.find(patch.find, pos)) != std::string::npos) {
            patched.replace(pos, patch.find.size(), patch.replace);
            pos += patch.replace.size();
            ++replaced;
        }

        if (replaced == 0) {
            result.error = "DXIL text patch did not match: " + patch.find.substr(0, 120);
            return result;
        }
    }

    auto& runtime = DxcRuntime::instance();
    if (!runtime.ensure_loaded()) {
        result.error = runtime.failure_reason;
        return result;
    }

    ComPtr<IDxcBlobEncoding> text_blob{};
    if (!create_blob_from_bytes(runtime, patched.data(), patched.size(), DXC_CP_UTF8, text_blob, result.error)) {
        return result;
    }

    ComPtr<IDxcAssembler> assembler{};
    auto hr = runtime.create(CLSID_DxcAssembler, IID_PPV_ARGS(&assembler));
    if (FAILED(hr) || assembler == nullptr) {
        result.error = "Failed to create IDxcAssembler: " + hr_to_string(hr);
        return result;
    }

    ComPtr<IDxcOperationResult> assemble_result{};
    hr = assembler->AssembleToContainer(text_blob.Get(), &assemble_result);
    if (FAILED(hr) || assemble_result == nullptr) {
        result.error = "IDxcAssembler::AssembleToContainer failed: " + hr_to_string(hr);
        return result;
    }

    if (!read_operation_result(assemble_result.Get(), result.bytecode, result.error)) {
        return result;
    }

    if (request.validate_and_sign && !validate_container_bytes(runtime, result.bytecode, result.error)) {
        return result;
    }

    result.succeeded = true;
    return result;
}
} // namespace render
