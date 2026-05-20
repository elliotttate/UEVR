#include <Windows.h>
#include <dxcapi.h>
#include <wrl/client.h>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <regex>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <nlohmann/json.hpp>

using Microsoft::WRL::ComPtr;
using json = nlohmann::json;

namespace {
using DxcCreateInstanceProc = HRESULT(WINAPI*)(REFCLSID, REFIID, LPVOID*);

struct DxcRuntime {
    HMODULE dxil_module{};
    HMODULE dxcompiler_module{};
    DxcCreateInstanceProc create_instance{};
    std::filesystem::path loaded_from{};
    std::string error{};

    ~DxcRuntime() {
        if (dxcompiler_module != nullptr) {
            FreeLibrary(dxcompiler_module);
        }
        if (dxil_module != nullptr) {
            FreeLibrary(dxil_module);
        }
    }

    bool load(const std::filesystem::path& explicit_path = {}) {
        std::vector<std::filesystem::path> dirs{};

        if (!explicit_path.empty()) {
            dirs.emplace_back(std::filesystem::is_directory(explicit_path) ? explicit_path : explicit_path.parent_path());
        }

        wchar_t env_path[32768]{};
        const auto env_len = GetEnvironmentVariableW(L"UEVR_DXC_PATH", env_path, static_cast<DWORD>(std::size(env_path)));
        if (env_len > 0 && env_len < std::size(env_path)) {
            std::filesystem::path path{std::wstring_view{env_path, env_len}};
            dirs.emplace_back(std::filesystem::is_directory(path) ? path : path.parent_path());
        }

        wchar_t module_path[MAX_PATH]{};
        const auto module_len = GetModuleFileNameW(nullptr, module_path, static_cast<DWORD>(std::size(module_path)));
        if (module_len > 0 && module_len < std::size(module_path)) {
            dirs.emplace_back(std::filesystem::path{std::wstring_view{module_path, module_len}}.parent_path());
        }

        const std::filesystem::path windows_kits_root{L"C:\\Program Files (x86)\\Windows Kits\\10"};
        const auto bin_root = windows_kits_root / "bin";
        if (std::filesystem::exists(bin_root)) {
            std::vector<std::filesystem::path> versions{};
            for (const auto& entry : std::filesystem::directory_iterator(bin_root)) {
                if (entry.is_directory()) {
                    versions.emplace_back(entry.path());
                }
            }
            std::sort(versions.begin(), versions.end(), std::greater<>{});
            for (const auto& version : versions) {
                dirs.emplace_back(version / "x64");
            }
        }

        dirs.erase(std::remove_if(dirs.begin(), dirs.end(), [](const auto& dir) {
            return dir.empty();
        }), dirs.end());
        dirs.erase(std::unique(dirs.begin(), dirs.end()), dirs.end());

        for (const auto& dir : dirs) {
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
                error = "dxcompiler.dll is missing DxcCreateInstance";
                FreeLibrary(dxcompiler_module);
                dxcompiler_module = nullptr;
                if (dxil_module != nullptr) {
                    FreeLibrary(dxil_module);
                    dxil_module = nullptr;
                }
                continue;
            }

            loaded_from = dxcompiler_path;
            error.clear();
            return true;
        }

        error = "DXC runtime not found";
        return false;
    }

    template <typename T>
    HRESULT create(REFCLSID clsid, ComPtr<T>& out) const {
        return create_instance(clsid, IID_PPV_ARGS(&out));
    }
};

struct Options {
    std::string command{};
    std::filesystem::path input{};
    std::filesystem::path patch{};
    std::filesystem::path output{};
    std::filesystem::path report{};
    std::filesystem::path copy_parts_from{};
    std::filesystem::path dxc_path{};
    bool copy_root_signature{true};
    bool copy_all_non_dxil_parts{};
    bool validate{true};
};

std::string hr_string(HRESULT hr) {
    std::ostringstream ss{};
    ss << "HRESULT 0x" << std::hex << std::uppercase << static_cast<uint32_t>(hr);
    return ss.str();
}

std::wstring widen(std::string_view value) {
    if (value.empty()) {
        return {};
    }
    const int needed = MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0);
    if (needed <= 0) {
        return std::wstring{value.begin(), value.end()};
    }
    std::wstring out(static_cast<size_t>(needed), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), out.data(), needed);
    return out;
}

bool read_file(const std::filesystem::path& path, std::vector<uint8_t>& out, std::string& error) {
    std::ifstream file{path, std::ios::binary};
    if (!file) {
        error = "failed to open " + path.string();
        return false;
    }
    file.seekg(0, std::ios::end);
    const auto end = file.tellg();
    if (end < 0) {
        error = "failed to size " + path.string();
        return false;
    }
    file.seekg(0, std::ios::beg);
    out.resize(static_cast<size_t>(end));
    if (!out.empty()) {
        file.read(reinterpret_cast<char*>(out.data()), static_cast<std::streamsize>(out.size()));
    }
    if (!file) {
        error = "failed to read " + path.string();
        return false;
    }
    return true;
}

bool read_text(const std::filesystem::path& path, std::string& out, std::string& error) {
    std::vector<uint8_t> bytes{};
    if (!read_file(path, bytes, error)) {
        return false;
    }
    out.assign(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    return true;
}

bool write_file(const std::filesystem::path& path, const void* data, size_t size, std::string& error) {
    std::error_code ec{};
    if (!path.parent_path().empty()) {
        std::filesystem::create_directories(path.parent_path(), ec);
        if (ec) {
            error = "failed to create " + path.parent_path().string() + ": " + ec.message();
            return false;
        }
    }

    std::ofstream file{path, std::ios::binary | std::ios::trunc};
    if (!file) {
        error = "failed to open " + path.string() + " for writing";
        return false;
    }
    if (size > 0) {
        file.write(static_cast<const char*>(data), static_cast<std::streamsize>(size));
    }
    if (!file) {
        error = "failed to write " + path.string();
        return false;
    }
    return true;
}

bool write_text(const std::filesystem::path& path, std::string_view text, std::string& error) {
    return write_file(path, text.data(), text.size(), error);
}

std::string lower_ascii(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

std::optional<int> resource_class_value(std::string_view value) {
    const auto lowered = lower_ascii(std::string{value});
    if (lowered == "srv" || lowered == "texture" || lowered == "t") {
        return 0;
    }
    if (lowered == "uav" || lowered == "unordered_access" || lowered == "u") {
        return 1;
    }
    if (lowered == "cbv" || lowered == "cbuffer" || lowered == "constant_buffer" || lowered == "b") {
        return 2;
    }
    if (lowered == "sampler" || lowered == "s") {
        return 3;
    }

    try {
        return std::stoi(std::string{value});
    } catch (...) {
        return std::nullopt;
    }
}

std::string resource_class_name(int value) {
    switch (value) {
    case 0: return "srv";
    case 1: return "uav";
    case 2: return "cbv";
    case 3: return "sampler";
    default: return std::to_string(value);
    }
}

std::optional<int> optional_int(const json& value, std::initializer_list<const char*> keys) {
    for (const auto* key : keys) {
        if (!value.contains(key) || value.at(key).is_null()) {
            continue;
        }

        if (value.at(key).is_number_integer()) {
            return value.at(key).get<int>();
        }

        if (value.at(key).is_string()) {
            try {
                return std::stoi(value.at(key).get<std::string>());
            } catch (...) {
                return std::nullopt;
            }
        }
    }

    return std::nullopt;
}

std::optional<bool> optional_bool(const json& value, std::initializer_list<const char*> keys) {
    for (const auto* key : keys) {
        if (!value.contains(key) || value.at(key).is_null()) {
            continue;
        }

        if (value.at(key).is_boolean()) {
            return value.at(key).get<bool>();
        }

        if (value.at(key).is_string()) {
            const auto lowered = lower_ascii(value.at(key).get<std::string>());
            if (lowered == "true" || lowered == "1" || lowered == "yes") {
                return true;
            }
            if (lowered == "false" || lowered == "0" || lowered == "no") {
                return false;
            }
        }
    }

    return std::nullopt;
}

std::optional<int> optional_resource_class(const json& value, std::initializer_list<const char*> keys) {
    for (const auto* key : keys) {
        if (!value.contains(key) || value.at(key).is_null()) {
            continue;
        }

        if (value.at(key).is_number_integer()) {
            return value.at(key).get<int>();
        }

        if (value.at(key).is_string()) {
            return resource_class_value(value.at(key).get<std::string>());
        }
    }

    return std::nullopt;
}

std::string optional_string(const json& value, std::initializer_list<const char*> keys, std::string fallback = {}) {
    for (const auto* key : keys) {
        if (value.contains(key) && value.at(key).is_string()) {
            return value.at(key).get<std::string>();
        }
    }

    return fallback;
}

bool int_filter_matches(std::optional<int> expected, std::string_view actual) {
    if (!expected.has_value()) {
        return true;
    }

    try {
        return std::stoi(std::string{actual}) == *expected;
    } catch (...) {
        return false;
    }
}

bool bool_filter_matches(std::optional<bool> expected, std::string_view actual) {
    if (!expected.has_value()) {
        return true;
    }

    return (*expected && actual == "true") || (!*expected && actual == "false");
}

bool line_matches_rule_filters(const json& op, std::string_view line, std::string_view result_name) {
    const auto handle = optional_string(op, {"handle", "result", "name"});
    if (!handle.empty() && handle != result_name) {
        return false;
    }

    const auto name_regex = optional_string(op, {"handle_regex", "name_regex", "line_regex"});
    if (!name_regex.empty()) {
        try {
            if (!std::regex_search(line.begin(), line.end(), std::regex{name_regex})) {
                return false;
            }
        } catch (...) {
            return false;
        }
    }

    return true;
}

bool blob_to_file(IDxcBlob* blob, const std::filesystem::path& output, std::string& error) {
    if (blob == nullptr) {
        error = "internal error: null blob";
        return false;
    }
    return write_file(output, blob->GetBufferPointer(), blob->GetBufferSize(), error);
}

std::string operation_errors(IDxcOperationResult* result) {
    if (result == nullptr) {
        return {};
    }

    ComPtr<IDxcBlobEncoding> errors{};
    if (FAILED(result->GetErrorBuffer(&errors)) || errors == nullptr || errors->GetBufferSize() == 0) {
        return {};
    }

    const auto* text = static_cast<const char*>(errors->GetBufferPointer());
    return std::string{text, text + errors->GetBufferSize()};
}

bool check_operation(IDxcOperationResult* result, const char* what, std::string& error) {
    HRESULT status = E_FAIL;
    if (result == nullptr || FAILED(result->GetStatus(&status))) {
        error = std::string{what} + " did not return a status";
        return false;
    }

    if (FAILED(status)) {
        error = std::string{what} + " failed: " + hr_string(status);
        const auto details = operation_errors(result);
        if (!details.empty()) {
            error += "\n" + details;
        }
        return false;
    }

    return true;
}

bool make_blob(DxcRuntime& dxc, const void* data, size_t size, UINT32 code_page, ComPtr<IDxcBlobEncoding>& blob, std::string& error) {
    ComPtr<IDxcUtils> utils{};
    HRESULT hr = dxc.create(CLSID_DxcUtils, utils);
    if (FAILED(hr)) {
        error = "failed to create IDxcUtils: " + hr_string(hr);
        return false;
    }

    hr = utils->CreateBlobFromPinned(data, static_cast<UINT32>(size), code_page, &blob);
    if (FAILED(hr)) {
        error = "failed to create DXC blob: " + hr_string(hr);
        return false;
    }

    return true;
}

bool disassemble(DxcRuntime& dxc, const std::filesystem::path& input, std::string& text, std::string& error) {
    std::vector<uint8_t> bytes{};
    if (!read_file(input, bytes, error)) {
        return false;
    }

    ComPtr<IDxcCompiler3> compiler{};
    HRESULT hr = dxc.create(CLSID_DxcCompiler, compiler);
    if (FAILED(hr)) {
        error = "failed to create IDxcCompiler3: " + hr_string(hr);
        return false;
    }

    DxcBuffer buffer{bytes.data(), bytes.size(), 0};
    ComPtr<IDxcResult> result{};
    hr = compiler->Disassemble(&buffer, IID_PPV_ARGS(&result));
    if (FAILED(hr)) {
        error = "IDxcCompiler3::Disassemble failed: " + hr_string(hr);
        return false;
    }

    HRESULT status = E_FAIL;
    if (FAILED(result->GetStatus(&status)) || FAILED(status)) {
        error = "DXIL disassembly failed: " + hr_string(status);
        ComPtr<IDxcBlobUtf8> errors{};
        if (SUCCEEDED(result->GetOutput(DXC_OUT_ERRORS, IID_PPV_ARGS(&errors), nullptr)) && errors != nullptr) {
            error += "\n";
            error += errors->GetStringPointer();
        }
        return false;
    }

    ComPtr<IDxcBlobUtf8> disassembly{};
    hr = result->GetOutput(DXC_OUT_DISASSEMBLY, IID_PPV_ARGS(&disassembly), nullptr);
    if (FAILED(hr) || disassembly == nullptr) {
        error = "failed to get disassembly: " + hr_string(hr);
        return false;
    }

    text.assign(disassembly->GetStringPointer(), disassembly->GetStringLength());
    return true;
}

bool validate_container(DxcRuntime& dxc, IDxcBlob* blob, std::string& error) {
    ComPtr<IDxcValidator> validator{};
    HRESULT hr = dxc.create(CLSID_DxcValidator, validator);
    if (FAILED(hr)) {
        error = "failed to create IDxcValidator: " + hr_string(hr);
        return false;
    }

    ComPtr<IDxcOperationResult> result{};
    hr = validator->Validate(blob, DxcValidatorFlags_InPlaceEdit, &result);
    if (FAILED(hr)) {
        error = "IDxcValidator::Validate failed: " + hr_string(hr);
        return false;
    }

    return check_operation(result.Get(), "DXIL validation", error);
}

bool copy_container_parts(DxcRuntime& dxc, IDxcBlob* assembled, const std::filesystem::path& original_path, bool copy_root_signature, bool copy_all_non_dxil_parts, ComPtr<IDxcBlob>& out, std::string& error) {
    out = assembled;
    if (original_path.empty() || (!copy_root_signature && !copy_all_non_dxil_parts)) {
        return true;
    }

    std::vector<uint8_t> original_bytes{};
    if (!read_file(original_path, original_bytes, error)) {
        return false;
    }

    ComPtr<IDxcBlobEncoding> original_blob{};
    if (!make_blob(dxc, original_bytes.data(), original_bytes.size(), 0, original_blob, error)) {
        return false;
    }

    ComPtr<IDxcContainerReflection> reflection{};
    HRESULT hr = dxc.create(CLSID_DxcContainerReflection, reflection);
    if (FAILED(hr)) {
        error = "failed to create IDxcContainerReflection: " + hr_string(hr);
        return false;
    }

    hr = reflection->Load(original_blob.Get());
    if (FAILED(hr)) {
        error = "failed to load original container for reflection: " + hr_string(hr);
        return false;
    }

    ComPtr<IDxcContainerBuilder> builder{};
    hr = dxc.create(CLSID_DxcContainerBuilder, builder);
    if (FAILED(hr)) {
        error = "failed to create IDxcContainerBuilder: " + hr_string(hr);
        return false;
    }

    hr = builder->Load(assembled);
    if (FAILED(hr)) {
        error = "failed to load assembled container into builder: " + hr_string(hr);
        return false;
    }

    UINT32 part_count = 0;
    hr = reflection->GetPartCount(&part_count);
    if (FAILED(hr)) {
        error = "failed to get original container part count: " + hr_string(hr);
        return false;
    }

    for (UINT32 i = 0; i < part_count; ++i) {
        UINT32 kind = 0;
        if (FAILED(reflection->GetPartKind(i, &kind))) {
            continue;
        }

        const bool should_copy = copy_all_non_dxil_parts
            ? (kind != DXC_PART_DXIL && kind != DXC_PART_SHADER_HASH)
            : (copy_root_signature && kind == DXC_PART_ROOT_SIGNATURE);
        if (!should_copy) {
            continue;
        }

        ComPtr<IDxcBlob> part{};
        if (FAILED(reflection->GetPartContent(i, &part)) || part == nullptr) {
            continue;
        }

        (void)builder->RemovePart(kind);
        hr = builder->AddPart(kind, part.Get());
        if (FAILED(hr)) {
            error = "failed to copy container part: " + hr_string(hr);
            return false;
        }
    }

    ComPtr<IDxcOperationResult> result{};
    hr = builder->SerializeContainer(&result);
    if (FAILED(hr)) {
        error = "failed to serialize rebuilt container: " + hr_string(hr);
        return false;
    }

    if (!check_operation(result.Get(), "container serialization", error)) {
        return false;
    }

    return SUCCEEDED(result->GetResult(&out)) && out != nullptr;
}

bool assemble_text(DxcRuntime& dxc, std::string_view text, const std::filesystem::path& copy_parts_from, bool copy_root_signature, bool copy_all_non_dxil_parts, bool validate, ComPtr<IDxcBlob>& output, std::string& error) {
    ComPtr<IDxcBlobEncoding> text_blob{};
    if (!make_blob(dxc, text.data(), text.size(), DXC_CP_UTF8, text_blob, error)) {
        return false;
    }

    ComPtr<IDxcAssembler> assembler{};
    HRESULT hr = dxc.create(CLSID_DxcAssembler, assembler);
    if (FAILED(hr)) {
        error = "failed to create IDxcAssembler: " + hr_string(hr);
        return false;
    }

    ComPtr<IDxcOperationResult> assemble_result{};
    hr = assembler->AssembleToContainer(text_blob.Get(), &assemble_result);
    if (FAILED(hr)) {
        error = "IDxcAssembler::AssembleToContainer failed: " + hr_string(hr);
        return false;
    }

    if (!check_operation(assemble_result.Get(), "DXIL assembly", error)) {
        return false;
    }

    ComPtr<IDxcBlob> assembled{};
    hr = assemble_result->GetResult(&assembled);
    if (FAILED(hr) || assembled == nullptr) {
        error = "failed to get assembled container: " + hr_string(hr);
        return false;
    }

    ComPtr<IDxcBlob> rebuilt{};
    if (!copy_container_parts(dxc, assembled.Get(), copy_parts_from, copy_root_signature, copy_all_non_dxil_parts, rebuilt, error)) {
        return false;
    }

    if (validate && !validate_container(dxc, rebuilt.Get(), error)) {
        return false;
    }

    output = rebuilt;
    return true;
}

std::string replace_all(std::string text, std::string_view find, std::string_view replace, size_t max_count, size_t& count) {
    count = 0;
    if (find.empty()) {
        return text;
    }

    size_t pos = 0;
    while ((pos = text.find(find, pos)) != std::string::npos) {
        if (max_count != 0 && count >= max_count) {
            break;
        }

        text.replace(pos, find.size(), replace);
        pos += replace.size();
        ++count;
    }

    return text;
}

bool apply_patch_file(const std::filesystem::path& patch_path, std::string& text, json& report, std::string& error) {
    std::string patch_text{};
    if (!read_text(patch_path, patch_text, error)) {
        return false;
    }

    json patch{};
    try {
        patch = json::parse(patch_text);
    } catch (const std::exception& e) {
        error = std::string{"failed to parse patch JSON: "} + e.what();
        return false;
    }

    json operations = json::array();
    auto patches = patch.contains("patches") ? patch.at("patches") : json::array();
    if (patches.empty() && patch.contains("replacements")) {
        patches = patch.at("replacements");
    }

    if (!patches.is_array()) {
        error = "patches must be an array";
        return false;
    }

    for (const auto& op : patches) {
        const auto kind = op.value("kind", std::string{"replace_text"});
        const auto required = op.value("required", true);
        size_t count = 0;

        try {
            if (kind == "replace_text") {
                const auto find = op.at("find").get<std::string>();
                const auto replace = op.value("replace", std::string{});
                const size_t max_count = op.value("count", 0);
                text = replace_all(std::move(text), find, replace, max_count, count);
            } else if (kind == "replace_regex") {
                const auto find = op.at("find").get<std::string>();
                const auto replace = op.value("replace", std::string{});
                const size_t max_count = op.value("count", 0);
                std::regex re{find};
                std::string out{};
                std::sregex_iterator it{text.begin(), text.end(), re};
                std::sregex_iterator end{};
                size_t last = 0;
                for (; it != end; ++it) {
                    if (max_count != 0 && count >= max_count) {
                        break;
                    }
                    out.append(text, last, static_cast<size_t>(it->position()) - last);
                    out += it->format(replace);
                    last = static_cast<size_t>(it->position() + it->length());
                    ++count;
                }
                out.append(text, last, std::string::npos);
                text = std::move(out);
            } else if (kind == "insert_before" || kind == "insert_after") {
                const auto anchor = op.at("anchor").get<std::string>();
                const auto insert = op.at("insert").get<std::string>();
                const size_t max_count = op.value("count", 0);
                size_t pos = 0;
                while ((pos = text.find(anchor, pos)) != std::string::npos) {
                    if (max_count != 0 && count >= max_count) {
                        break;
                    }
                    const auto insert_pos = kind == "insert_before" ? pos : pos + anchor.size();
                    text.insert(insert_pos, insert);
                    pos = insert_pos + insert.size() + anchor.size();
                    ++count;
                }
            } else {
                error = "unknown patch kind: " + kind;
                return false;
            }
        } catch (const std::exception& e) {
            error = std::string{"patch operation failed: "} + e.what();
            return false;
        }

        operations.push_back({
            {"kind", kind},
            {"count", count},
            {"required", required},
            {"note", op.value("note", std::string{})},
        });

        if (required && count == 0) {
            error = "required patch operation did not match: " + kind;
            return false;
        }
    }

    report["operations"] = std::move(operations);
    return true;
}

json analyze_dxil_text(std::string_view text) {
    json handles = json::array();
    json cbuffer_loads = json::array();
    json samples = json::array();

    const std::regex result_re{R"(^\s*(%[-A-Za-z0-9_.$]+)\s*=)"};
    const std::regex create_handle_re{
        R"((@dx\.op\.createHandle[^(]*\(\s*i32\s+([0-9]+)\s*,\s*i8\s+([0-9]+)\s*,\s*i32\s+([^,\)]+)\s*,\s*i32\s+([^,\)]+)\s*,\s*i1\s+(true|false)\s*\)))"};
    const std::regex create_handle_from_binding_re{
        R"((@dx\.op\.createHandleFromBinding[^(]*\(\s*i32\s+([0-9]+)\s*,\s*%dx\.types\.ResBind\s*\{\s*i32\s+(-?[0-9]+)\s*,\s*i32\s+(-?[0-9]+)\s*,\s*i32\s+(-?[0-9]+)\s*,\s*i8\s+([0-9]+)\s*\}\s*,\s*i32\s+([^,\)]+)\s*,\s*i1\s+(true|false)\s*\)))"};
    const std::regex cbuffer_load_re{
        R"((@dx\.op\.cbufferLoad(?:Legacy)?\.[^(]+\(\s*i32\s+([0-9]+)\s*,\s*%dx\.types\.Handle\s+(%[-A-Za-z0-9_.$]+)\s*,\s*i32\s+([^,\)]+)\s*\)))"};
    const std::regex sample_re{R"(@dx\.op\.(?:sample|sampleBias|sampleCmp|sampleCmpLevelZero|sampleGrad|sampleLevel)\.)"};

    std::istringstream stream{std::string{text}};
    std::string line{};
    size_t line_number = 0;
    while (std::getline(stream, line)) {
        ++line_number;

        std::smatch result_match{};
        const std::string result_name =
            std::regex_search(line, result_match, result_re) ? result_match[1].str() : std::string{};

        std::smatch match{};
        if (std::regex_search(line, match, create_handle_re)) {
            const auto resource_class = std::stoi(match[3].str());
            handles.push_back({
                {"line", line_number},
                {"result", result_name},
                {"op", "createHandle"},
                {"resource_class", resource_class_name(resource_class)},
                {"resource_class_value", resource_class},
                {"range_id", match[4].str()},
                {"index", match[5].str()},
                {"non_uniform", match[6].str() == "true"},
            });
        } else if (std::regex_search(line, match, create_handle_from_binding_re)) {
            const auto resource_class = std::stoi(match[6].str());
            handles.push_back({
                {"line", line_number},
                {"result", result_name},
                {"op", "createHandleFromBinding"},
                {"resource_class", resource_class_name(resource_class)},
                {"resource_class_value", resource_class},
                {"lower_bound", std::stoi(match[3].str())},
                {"range_size", std::stoi(match[4].str())},
                {"space", std::stoi(match[5].str())},
                {"index", match[7].str()},
                {"non_uniform", match[8].str() == "true"},
            });
        }

        if (std::regex_search(line, match, cbuffer_load_re)) {
            cbuffer_loads.push_back({
                {"line", line_number},
                {"result", result_name},
                {"handle", match[3].str()},
                {"index", match[4].str()},
                {"op", line.find("cbufferLoadLegacy") != std::string::npos ? "cbufferLoadLegacy" : "cbufferLoad"},
            });
        }

        if (std::regex_search(line, sample_re)) {
            samples.push_back({
                {"line", line_number},
                {"result", result_name},
            });
        }
    }

    return {
        {"create_handles", std::move(handles)},
        {"cbuffer_loads", std::move(cbuffer_loads)},
        {"samples", std::move(samples)},
    };
}

bool replace_call_segment(std::string& line, const std::smatch& match, std::string replacement) {
    if (match.empty()) {
        return false;
    }

    line.replace(static_cast<size_t>(match.position(1)), static_cast<size_t>(match.length(1)), replacement);
    return true;
}

bool apply_redirect_handle_rule(const json& op, std::string& text, size_t& count, std::string& error) {
    const auto from_class = optional_resource_class(op, {"from_resource_class", "resource_class", "class"});
    const auto to_class = optional_resource_class(op, {"to_resource_class"});
    const auto from_range_id = optional_int(op, {"from_range_id", "range_id"});
    const auto to_range_id = optional_int(op, {"to_range_id"});
    const auto from_index = optional_int(op, {"from_index", "index"});
    const auto to_index = optional_int(op, {"to_index"});
    const auto from_lower_bound = optional_int(op, {"from_lower_bound", "lower_bound"});
    const auto to_lower_bound = optional_int(op, {"to_lower_bound"});
    const auto from_range_size = optional_int(op, {"from_range_size", "range_size"});
    const auto to_range_size = optional_int(op, {"to_range_size"});
    const auto from_space = optional_int(op, {"from_space", "space"});
    const auto to_space = optional_int(op, {"to_space"});
    const auto from_non_uniform = optional_bool(op, {"from_non_uniform", "non_uniform"});
    const auto to_non_uniform = optional_bool(op, {"to_non_uniform"});

    const std::regex result_re{R"(^\s*(%[-A-Za-z0-9_.$]+)\s*=)"};
    const std::regex create_handle_re{
        R"((@dx\.op\.createHandle[^(]*\(\s*i32\s+([0-9]+)\s*,\s*i8\s+([0-9]+)\s*,\s*i32\s+([^,\)]+)\s*,\s*i32\s+([^,\)]+)\s*,\s*i1\s+(true|false)\s*\)))"};
    const std::regex create_handle_from_binding_re{
        R"((@dx\.op\.createHandleFromBinding[^(]*\(\s*i32\s+([0-9]+)\s*,\s*%dx\.types\.ResBind\s*\{\s*i32\s+(-?[0-9]+)\s*,\s*i32\s+(-?[0-9]+)\s*,\s*i32\s+(-?[0-9]+)\s*,\s*i8\s+([0-9]+)\s*\}\s*,\s*i32\s+([^,\)]+)\s*,\s*i1\s+(true|false)\s*\)))"};

    std::istringstream in{text};
    std::ostringstream out{};
    std::string line{};
    count = 0;

    while (std::getline(in, line)) {
        const bool had_newline = !in.eof();
        std::smatch result_match{};
        const std::string result_name =
            std::regex_search(line, result_match, result_re) ? result_match[1].str() : std::string{};

        std::smatch match{};
        if (std::regex_search(line, match, create_handle_re) &&
            line_matches_rule_filters(op, line, result_name) &&
            int_filter_matches(from_class, match[3].str()) &&
            int_filter_matches(from_range_id, match[4].str()) &&
            int_filter_matches(from_index, match[5].str()) &&
            bool_filter_matches(from_non_uniform, match[6].str())) {
            const auto resource_class = to_class.value_or(std::stoi(match[3].str()));
            const auto range_id = to_range_id.has_value() ? std::to_string(*to_range_id) : match[4].str();
            const auto index = to_index.has_value() ? std::to_string(*to_index) : match[5].str();
            const auto non_uniform = to_non_uniform.has_value() ? (*to_non_uniform ? "true" : "false") : match[6].str();
            std::ostringstream replacement{};
            replacement
                << "@dx.op.createHandle(i32 " << match[2].str()
                << ", i8 " << resource_class
                << ", i32 " << range_id
                << ", i32 " << index
                << ", i1 " << non_uniform
                << ")";
            replace_call_segment(line, match, replacement.str());
            ++count;
        } else if (std::regex_search(line, match, create_handle_from_binding_re) &&
            line_matches_rule_filters(op, line, result_name) &&
            int_filter_matches(from_class, match[6].str()) &&
            int_filter_matches(from_lower_bound, match[3].str()) &&
            int_filter_matches(from_range_size, match[4].str()) &&
            int_filter_matches(from_space, match[5].str()) &&
            int_filter_matches(from_index, match[7].str()) &&
            bool_filter_matches(from_non_uniform, match[8].str())) {
            const auto lower_bound = to_lower_bound.value_or(std::stoi(match[3].str()));
            const auto range_size = to_range_size.value_or(std::stoi(match[4].str()));
            const auto space = to_space.value_or(std::stoi(match[5].str()));
            const auto resource_class = to_class.value_or(std::stoi(match[6].str()));
            const auto index = to_index.has_value() ? std::to_string(*to_index) : match[7].str();
            const auto non_uniform = to_non_uniform.has_value() ? (*to_non_uniform ? "true" : "false") : match[8].str();
            std::ostringstream replacement{};
            replacement
                << "@dx.op.createHandleFromBinding(i32 " << match[2].str()
                << ", %dx.types.ResBind { i32 " << lower_bound
                << ", i32 " << range_size
                << ", i32 " << space
                << ", i8 " << resource_class
                << " }, i32 " << index
                << ", i1 " << non_uniform
                << ")";
            replace_call_segment(line, match, replacement.str());
            ++count;
        }

        out << line;
        if (had_newline) {
            out << '\n';
        }
    }

    text = out.str();
    error.clear();
    return true;
}

bool apply_rewrite_cbuffer_load_rule(const json& op, std::string& text, size_t& count, std::string& error) {
    const auto handle = optional_string(op, {"handle"});
    const auto handle_regex = optional_string(op, {"handle_regex"});
    const auto from_index = optional_int(op, {"from_index", "index", "from_register_index", "register_index"});
    const auto to_index = optional_int(op, {"to_index", "to_register_index"});
    if (!to_index.has_value()) {
        error = "rewrite_cbuffer_load_index requires to_index/to_register_index";
        return false;
    }

    std::optional<std::regex> handle_re{};
    if (!handle_regex.empty()) {
        try {
            handle_re.emplace(handle_regex);
        } catch (const std::exception& e) {
            error = std::string{"invalid handle_regex: "} + e.what();
            return false;
        }
    }

    const std::regex cbuffer_load_re{
        R"((@dx\.op\.cbufferLoad(?:Legacy)?\.[^(]+\(\s*i32\s+([0-9]+)\s*,\s*%dx\.types\.Handle\s+(%[-A-Za-z0-9_.$]+)\s*,\s*i32\s+([^,\)]+)\s*\)))"};

    std::istringstream in{text};
    std::ostringstream out{};
    std::string line{};
    count = 0;

    while (std::getline(in, line)) {
        const bool had_newline = !in.eof();
        std::smatch match{};
        if (std::regex_search(line, match, cbuffer_load_re)) {
            const auto actual_handle = match[3].str();
            const bool handle_match =
                (handle.empty() || handle == actual_handle) &&
                (!handle_re.has_value() || std::regex_search(actual_handle, *handle_re) || std::regex_search(line, *handle_re));

            if (handle_match && int_filter_matches(from_index, match[4].str())) {
                const auto old_segment = match[1].str();
                const auto needle = std::string{"i32 "} + match[4].str() + ")";
                auto replacement = old_segment;
                const auto pos = replacement.rfind(needle);
                if (pos != std::string::npos) {
                    replacement.replace(pos, needle.size(), "i32 " + std::to_string(*to_index) + ")");
                    replace_call_segment(line, match, replacement);
                    ++count;
                }
            }
        }

        out << line;
        if (had_newline) {
            out << '\n';
        }
    }

    text = out.str();
    error.clear();
    return true;
}

std::string literal_ir_value(const json& op, std::string_view dxil_type) {
    if (op.contains("value_ir") && op.at("value_ir").is_string()) {
        return op.at("value_ir").get<std::string>();
    }

    if (op.contains("value") && op.at("value").is_string()) {
        return op.at("value").get<std::string>();
    }

    if (op.contains("value") && op.at("value").is_number()) {
        std::ostringstream ss{};
        if (dxil_type == "f32" || dxil_type == "float") {
            ss << std::setprecision(9) << op.at("value").get<double>();
        } else {
            ss << op.at("value").get<int64_t>();
        }
        return ss.str();
    }

    return {};
}

bool apply_replace_extract_literal_rule(const json& op, std::string& text, size_t& count, std::string& error) {
    const auto load = optional_string(op, {"load", "source", "cbuffer_load"});
    const auto result = optional_string(op, {"result", "destination"});
    const auto component = optional_int(op, {"component"});
    const auto requested_type = optional_string(op, {"type", "value_type"});

    const std::regex extract_re{
        R"(^(\s*)(%[-A-Za-z0-9_.$]+)\s*=\s*extractvalue\s+%dx\.types\.CBufRet\.([A-Za-z0-9]+)\s+(%[-A-Za-z0-9_.$]+)\s*,\s*([0-9]+)(.*)$)"};

    std::istringstream in{text};
    std::ostringstream out{};
    std::string line{};
    count = 0;

    while (std::getline(in, line)) {
        const bool had_newline = !in.eof();
        std::smatch match{};
        if (std::regex_match(line, match, extract_re)) {
            const auto actual_result = match[2].str();
            const auto actual_type = lower_ascii(match[3].str());
            const auto actual_load = match[4].str();
            const auto actual_component = match[5].str();
            const bool matches =
                (load.empty() || load == actual_load) &&
                (result.empty() || result == actual_result) &&
                int_filter_matches(component, actual_component) &&
                (requested_type.empty() || requested_type == actual_type);

            if (matches) {
                const auto value = literal_ir_value(op, actual_type);
                if (value.empty()) {
                    error = "replace_cbuffer_extract_literal requires value or value_ir";
                    return false;
                }

                std::ostringstream replacement{};
                replacement << match[1].str() << actual_result << " = ";
                if (actual_type == "f32" || actual_type == "float") {
                    replacement << "fadd fast float 0.000000e+00, " << value;
                } else if (actual_type == "i32" || actual_type == "u32") {
                    replacement << "add i32 0, " << value;
                } else if (actual_type == "i16" || actual_type == "u16") {
                    replacement << "add i16 0, " << value;
                } else if (actual_type == "i64" || actual_type == "u64") {
                    replacement << "add i64 0, " << value;
                } else {
                    error = "unsupported CBufRet type for literal replacement: " + actual_type;
                    return false;
                }
                replacement << " ; uevr dxil-transform replace_cbuffer_extract_literal";
                line = replacement.str();
                ++count;
            }
        }

        out << line;
        if (had_newline) {
            out << '\n';
        }
    }

    text = out.str();
    error.clear();
    return true;
}

bool apply_single_text_operation(const json& op, std::string& text, size_t& count, std::string& error) {
    const auto kind = op.value("kind", std::string{"replace_text"});

    try {
        if (kind == "replace_text") {
            const auto find = op.at("find").get<std::string>();
            const auto replace = op.value("replace", std::string{});
            const size_t max_count = op.value("count", 0);
            text = replace_all(std::move(text), find, replace, max_count, count);
        } else if (kind == "replace_regex") {
            const auto find = op.at("find").get<std::string>();
            const auto replace = op.value("replace", std::string{});
            const size_t max_count = op.value("count", 0);
            std::regex re{find};
            std::string out{};
            std::sregex_iterator it{text.begin(), text.end(), re};
            std::sregex_iterator end{};
            size_t last = 0;
            for (; it != end; ++it) {
                if (max_count != 0 && count >= max_count) {
                    break;
                }
                out.append(text, last, static_cast<size_t>(it->position()) - last);
                out += it->format(replace);
                last = static_cast<size_t>(it->position() + it->length());
                ++count;
            }
            out.append(text, last, std::string::npos);
            text = std::move(out);
        } else if (kind == "insert_before" || kind == "insert_after") {
            const auto anchor = op.at("anchor").get<std::string>();
            const auto insert = op.at("insert").get<std::string>();
            const size_t max_count = op.value("count", 0);
            size_t pos = 0;
            while ((pos = text.find(anchor, pos)) != std::string::npos) {
                if (max_count != 0 && count >= max_count) {
                    break;
                }
                const auto insert_pos = kind == "insert_before" ? pos : pos + anchor.size();
                text.insert(insert_pos, insert);
                pos = insert_pos + insert.size() + anchor.size();
                ++count;
            }
        } else if (kind == "require_regex") {
            const auto find = op.at("find").get<std::string>();
            std::regex re{find};
            count = static_cast<size_t>(std::distance(std::sregex_iterator{text.begin(), text.end(), re}, std::sregex_iterator{}));
        } else {
            error = "unknown text operation: " + kind;
            return false;
        }
    } catch (const std::exception& e) {
        error = std::string{"text operation failed: "} + e.what();
        return false;
    }

    return true;
}

bool apply_transform_file(const std::filesystem::path& transform_path, std::string& text, json& report, std::string& error) {
    std::string transform_text{};
    if (!read_text(transform_path, transform_text, error)) {
        return false;
    }

    json transform{};
    try {
        transform = json::parse(transform_text);
    } catch (const std::exception& e) {
        error = std::string{"failed to parse transform JSON: "} + e.what();
        return false;
    }

    json rules{};
    if (transform.is_array()) {
        rules = transform;
    } else if (transform.contains("transforms")) {
        rules = transform.at("transforms");
    } else if (transform.contains("rules")) {
        rules = transform.at("rules");
    } else if (transform.contains("patches")) {
        rules = transform.at("patches");
    } else if (transform.contains("kind")) {
        rules = json::array({transform});
    } else {
        error = "transform JSON must be an array, contain transforms[]/rules[], or be a single rule object";
        return false;
    }

    if (!rules.is_array()) {
        error = "DXIL transform rules must be an array";
        return false;
    }

    report["analysis_before"] = analyze_dxil_text(text);

    json operations = json::array();
    for (const auto& op : rules) {
        const auto kind = op.value("kind", std::string{});
        const auto required = op.value("required", true);
        size_t count = 0;
        bool ok = false;

        if (kind == "redirect_handle" || kind == "redirect_resource_handle") {
            ok = apply_redirect_handle_rule(op, text, count, error);
        } else if (kind == "rewrite_cbuffer_load_index" ||
                   kind == "redirect_cbuffer_load" ||
                   kind == "rewrite_cbuffer_index") {
            ok = apply_rewrite_cbuffer_load_rule(op, text, count, error);
        } else if (kind == "replace_cbuffer_extract_literal" ||
                   kind == "replace_extract_literal") {
            ok = apply_replace_extract_literal_rule(op, text, count, error);
        } else if (kind == "replace_text" ||
                   kind == "replace_regex" ||
                   kind == "insert_before" ||
                   kind == "insert_after" ||
                   kind == "require_regex") {
            ok = apply_single_text_operation(op, text, count, error);
        } else {
            error = "unknown DXIL transform kind: " + kind;
            return false;
        }

        if (!ok) {
            return false;
        }

        operations.push_back({
            {"kind", kind},
            {"count", count},
            {"required", required},
            {"note", op.value("note", std::string{})},
        });

        if (required && count == 0) {
            error = "required DXIL transform rule did not match: " + kind;
            return false;
        }
    }

    report["operations"] = std::move(operations);
    report["analysis_after"] = analyze_dxil_text(text);
    return true;
}

void write_report_if_requested(const Options& options, const json& report) {
    if (options.report.empty()) {
        return;
    }

    std::string error{};
    const auto text = report.dump(2);
    (void)write_text(options.report, text, error);
}

void usage() {
    std::cerr
        << "dxil-patch commands:\n"
        << "  dxil-patch disasm <input.dxbc> -o <output.ll>\n"
        << "  dxil-patch asm <input.ll> -o <output.dxbc> [--copy-parts-from <original.dxbc>] [--no-validate]\n"
        << "  dxil-patch patch <input.dxbc> <patch.json> -o <output.dxbc> [--report <report.json>]\n"
        << "  dxil-patch transform <input.dxbc> <transform.json> -o <output.dxbc> [--report <report.json>]\n"
        << "  dxil-patch validate <input.dxbc>\n";
}

bool parse_args(int argc, char** argv, Options& options) {
    if (argc < 3) {
        return false;
    }

    options.command = argv[1];
    options.input = widen(argv[2]);
    int i = 3;

    if (options.command == "patch" || options.command == "transform") {
        if (argc < 4) {
            return false;
        }
        options.patch = widen(argv[3]);
        i = 4;
    }

    for (; i < argc; ++i) {
        const std::string arg = argv[i];
        auto take_path = [&](std::filesystem::path& out) -> bool {
            if (i + 1 >= argc) {
                return false;
            }
            out = widen(argv[++i]);
            return true;
        };

        if (arg == "-o" || arg == "--output") {
            if (!take_path(options.output)) return false;
        } else if (arg == "--report") {
            if (!take_path(options.report)) return false;
        } else if (arg == "--copy-parts-from") {
            if (!take_path(options.copy_parts_from)) return false;
        } else if (arg == "--dxc-path") {
            if (!take_path(options.dxc_path)) return false;
        } else if (arg == "--copy-all-parts") {
            options.copy_all_non_dxil_parts = true;
        } else if (arg == "--no-copy-root-signature") {
            options.copy_root_signature = false;
        } else if (arg == "--no-validate") {
            options.validate = false;
        } else {
            return false;
        }
    }

    if ((options.command == "disasm" || options.command == "asm" || options.command == "patch" || options.command == "transform") && options.output.empty()) {
        return false;
    }

    return options.command == "disasm" ||
           options.command == "asm" ||
           options.command == "patch" ||
           options.command == "transform" ||
           options.command == "validate";
}
} // namespace

int main(int argc, char** argv) {
    Options options{};
    if (!parse_args(argc, argv, options)) {
        usage();
        return 2;
    }

    json report{
        {"ok", false},
        {"command", options.command},
        {"input", options.input.string()},
        {"output", options.output.string()},
    };

    DxcRuntime dxc{};
    if (!dxc.load(options.dxc_path)) {
        report["error"] = dxc.error;
        write_report_if_requested(options, report);
        std::cerr << dxc.error << "\n";
        return 1;
    }
    report["dxcompiler"] = dxc.loaded_from.string();

    std::string error{};
    bool ok = false;

    if (options.command == "disasm") {
        std::string text{};
        ok = disassemble(dxc, options.input, text, error) && write_text(options.output, text, error);
    } else if (options.command == "asm") {
        std::string text{};
        ComPtr<IDxcBlob> assembled{};
        ok = read_text(options.input, text, error) &&
             assemble_text(dxc, text, options.copy_parts_from, options.copy_root_signature, options.copy_all_non_dxil_parts, options.validate, assembled, error) &&
             blob_to_file(assembled.Get(), options.output, error);
    } else if (options.command == "patch") {
        std::string text{};
        ComPtr<IDxcBlob> assembled{};
        options.copy_parts_from = options.copy_parts_from.empty() ? options.input : options.copy_parts_from;
        ok = disassemble(dxc, options.input, text, error) &&
             apply_patch_file(options.patch, text, report, error) &&
             assemble_text(dxc, text, options.copy_parts_from, options.copy_root_signature, options.copy_all_non_dxil_parts, options.validate, assembled, error) &&
             blob_to_file(assembled.Get(), options.output, error);
        report["patch"] = options.patch.string();
    } else if (options.command == "transform") {
        std::string text{};
        ComPtr<IDxcBlob> assembled{};
        options.copy_parts_from = options.copy_parts_from.empty() ? options.input : options.copy_parts_from;
        ok = disassemble(dxc, options.input, text, error) &&
             apply_transform_file(options.patch, text, report, error) &&
             assemble_text(dxc, text, options.copy_parts_from, options.copy_root_signature, options.copy_all_non_dxil_parts, options.validate, assembled, error) &&
             blob_to_file(assembled.Get(), options.output, error);
        report["transform"] = options.patch.string();
    } else if (options.command == "validate") {
        std::vector<uint8_t> bytes{};
        ComPtr<IDxcBlobEncoding> blob{};
        ok = read_file(options.input, bytes, error) &&
             make_blob(dxc, bytes.data(), bytes.size(), 0, blob, error) &&
             validate_container(dxc, blob.Get(), error);
    }

    report["ok"] = ok;
    if (!ok) {
        report["error"] = error;
        write_report_if_requested(options, report);
        std::cerr << error << "\n";
        return 1;
    }

    write_report_if_requested(options, report);
    return 0;
}
