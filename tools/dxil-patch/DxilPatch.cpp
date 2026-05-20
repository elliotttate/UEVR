#include <Windows.h>
#include <dxcapi.h>
#include <wrl/client.h>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <regex>
#include <sstream>
#include <string>
#include <string_view>
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
        << "  dxil-patch validate <input.dxbc>\n";
}

bool parse_args(int argc, char** argv, Options& options) {
    if (argc < 3) {
        return false;
    }

    options.command = argv[1];
    options.input = widen(argv[2]);
    int i = 3;

    if (options.command == "patch") {
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

    if ((options.command == "disasm" || options.command == "asm" || options.command == "patch") && options.output.empty()) {
        return false;
    }

    return options.command == "disasm" || options.command == "asm" || options.command == "patch" || options.command == "validate";
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
