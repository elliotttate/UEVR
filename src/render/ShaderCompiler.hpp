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

struct ShaderReflectionVariableInfo {
    std::string name{};
    uint32_t start_offset{};
    uint32_t size{};
    uint32_t flags{};
    std::string type_name{};
    std::string type_class{};
    std::string type_kind{};
    uint32_t rows{};
    uint32_t columns{};
    uint32_t elements{};
    uint32_t members{};
};

struct ShaderReflectionConstantBufferInfo {
    std::string name{};
    std::string type{};
    uint32_t size{};
    std::vector<ShaderReflectionVariableInfo> variables{};
};

struct ShaderReflectionResourceBindingInfo {
    std::string name{};
    std::string type{};
    std::string return_type{};
    std::string dimension{};
    uint32_t bind_point{};
    uint32_t bind_count{};
    uint32_t space{};
    uint32_t flags{};
};

struct ShaderReflectionSignatureParamInfo {
    std::string semantic_name{};
    uint32_t semantic_index{};
    uint32_t register_index{};
    std::string system_value{};
    std::string component_type{};
    uint32_t mask{};
    uint32_t read_write_mask{};
    uint32_t stream{};
};

struct ShaderReflectionInfo {
    bool ok{};
    std::string error{};
    std::string creator{};
    uint32_t instruction_count{};
    uint32_t constant_buffer_count{};
    uint32_t bound_resource_count{};
    uint32_t input_parameter_count{};
    uint32_t output_parameter_count{};
    std::vector<ShaderReflectionConstantBufferInfo> constant_buffers{};
    std::vector<ShaderReflectionResourceBindingInfo> bound_resources{};
    std::vector<ShaderReflectionSignatureParamInfo> input_parameters{};
    std::vector<ShaderReflectionSignatureParamInfo> output_parameters{};
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
    ShaderReflectionInfo reflection{};
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
