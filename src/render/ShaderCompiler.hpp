#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace render {
enum class ShaderCompilerBackend : uint8_t {
    Auto,
    Dxc,
    Fxc,
};

struct ShaderCompileRequest {
    std::filesystem::path source_path{};
    std::string entry_point{"main"};
    std::string profile{};
    ShaderCompilerBackend preferred_backend{ShaderCompilerBackend::Auto};
    bool warnings_as_errors{true};
    bool strict_mode{true};
    bool debug_info{
#if defined(_DEBUG)
        true
#else
        false
#endif
    };
    bool strip_reflection{true};
    bool strip_debug{
#if defined(_DEBUG)
        false
#else
        true
#endif
    };
};

struct ShaderCompileResult {
    bool succeeded{};
    std::string compiler{};
    std::string notes{};
    std::string error{};
    std::vector<uint8_t> bytecode{};
};

struct ShaderContainerChunkInfo {
    std::string fourcc{};
    uint32_t offset{};
    uint32_t size{};
};

struct ShaderRecoveredSourceInfo {
    std::string name{};
    std::string text{};
};

struct ShaderBytecodeInspection {
    bool ok{};
    bool container{};
    std::string container_kind{};
    std::string container_hash{};
    uint32_t container_version{};
    uint32_t declared_size{};
    uint32_t bytecode_size{};
    std::string compiler{};
    std::string error{};
    std::vector<ShaderContainerChunkInfo> chunks{};
    std::string disassembly{};
    std::vector<ShaderRecoveredSourceInfo> recovered_sources{};
};

struct ShaderContainerEdit {
    std::string fourcc{};
    std::vector<uint8_t> data{};
    bool remove{};
};

struct ShaderTextPatch {
    std::string find{};
    std::string replace{};
};

struct ShaderContainerEditRequest {
    const void* bytecode{};
    size_t bytecode_size{};
    std::vector<ShaderContainerEdit> edits{};
    bool validate_and_sign{true};
};

struct ShaderContainerEditResult {
    bool succeeded{};
    std::string compiler{};
    std::string error{};
    std::vector<uint8_t> bytecode{};
};

struct ShaderDxilTextPatchRequest {
    const void* bytecode{};
    size_t bytecode_size{};
    std::vector<ShaderTextPatch> patches{};
    bool validate_and_sign{true};
    size_t max_disassembly_chars{2 * 1024 * 1024};
};

ShaderCompileResult compile_shader_file(const ShaderCompileRequest& request);
ShaderBytecodeInspection inspect_shader_bytecode(const void* bytecode, size_t bytecode_size, bool disassemble, size_t max_disassembly_chars = 128 * 1024);
ShaderContainerEditResult edit_shader_container(const ShaderContainerEditRequest& request);
ShaderContainerEditResult patch_dxil_text(const ShaderDxilTextPatchRequest& request);
} // namespace render
