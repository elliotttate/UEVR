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
