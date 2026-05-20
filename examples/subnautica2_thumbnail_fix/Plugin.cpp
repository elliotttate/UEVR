// Subnautica 2 save-thumbnail VR crop fix, packaged as a UEVR plugin.
//
// The save-thumbnail readback task allocates from crop bounds:
//   (CropMaxY - CropMinY) * (CropMaxX - CropMinX)
// but copies TargetWidth * TargetHeight pixels. In VR the render target is
// double-wide while the crop rect is one eye (e.g. Target=3360, Crop=1680),
// so the copy overruns the allocation and crashes on save. This patch
// normalizes the task's expected dimensions to the crop rectangle before
// allocation/copy.
//
// Original game version: Subnautica2-CL-112084. The byte signature guard in
// patch_bytes() will refuse to patch any other build.

#include <Windows.h>
#include <Psapi.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <string>

#include "uevr/Plugin.hpp"

using namespace uevr;

namespace {

std::string lower_copy(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

std::string current_exe_name() {
    wchar_t buffer[MAX_PATH]{};
    const DWORD len = GetModuleFileNameW(nullptr, buffer, MAX_PATH);
    if (len == 0) {
        return {};
    }
    const std::filesystem::path path{std::wstring{buffer, len}};
    const auto name = path.filename().string();
    return lower_copy(name);
}

bool patch_bytes(uintptr_t address, const uint8_t* expected, const uint8_t* replacement, size_t size, const char* name) {
    auto* target = reinterpret_cast<uint8_t*>(address);

    if (std::memcmp(target, replacement, size) == 0) {
        API::get()->log_info("Patch already applied: %s", name);
        return true;
    }

    if (std::memcmp(target, expected, size) != 0) {
        API::get()->log_warn("Patch skipped, byte signature mismatch: %s", name);
        return false;
    }

    DWORD old_protect{};
    if (!VirtualProtect(target, size, PAGE_EXECUTE_READWRITE, &old_protect)) {
        API::get()->log_error("Patch failed, VirtualProtect refused write access: %s", name);
        return false;
    }

    std::memcpy(target, replacement, size);
    FlushInstructionCache(GetCurrentProcess(), target, size);

    DWORD restored{};
    VirtualProtect(target, size, old_protect, &restored);

    API::get()->log_info("Patch applied: %s", name);
    return true;
}

void apply_subnautica2_save_thumbnail_patch() {
    auto* module = GetModuleHandleW(nullptr);
    if (module == nullptr) {
        API::get()->log_error("Subnautica 2 save-thumbnail patch skipped, main module unavailable");
        return;
    }

    // Subnautica2-CL-112084:
    constexpr uintptr_t kRva = 0x5A77F32;
    constexpr std::array<uint8_t, 27> kExpected = {
        0x8B, 0x43, 0x04,             // mov eax, [rbx+04h]       ; TargetWidth
        0x89, 0x45, 0x07,             // mov [rbp+07h], eax
        0x8B, 0x43, 0x08,             // mov eax, [rbx+08h]       ; TargetHeight
        0x89, 0x45, 0x0B,             // mov [rbp+0Bh], eax
        0x8B, 0x4B, 0x14,             // mov ecx, [rbx+14h]       ; CropMaxX
        0x2B, 0x4B, 0x0C,             // sub ecx, [rbx+0Ch]       ; CropWidth
        0x8B, 0x43, 0x18,             // mov eax, [rbx+18h]       ; CropMaxY
        0x2B, 0x43, 0x10,             // sub eax, [rbx+10h]       ; CropHeight
        0x0F, 0xAF, 0xC8,             // imul ecx, eax
    };
    constexpr std::array<uint8_t, 27> kPatch = {
        0x8B, 0x4B, 0x14,             // mov ecx, [rbx+14h]       ; CropMaxX
        0x2B, 0x4B, 0x0C,             // sub ecx, [rbx+0Ch]       ; CropWidth
        0x89, 0x4D, 0x07,             // mov [rbp+07h], ecx       ; ExpectedWidth = CropWidth
        0x8B, 0x43, 0x18,             // mov eax, [rbx+18h]       ; CropMaxY
        0x2B, 0x43, 0x10,             // sub eax, [rbx+10h]       ; CropHeight
        0x89, 0x45, 0x0B,             // mov [rbp+0Bh], eax       ; ExpectedHeight = CropHeight
        0x0F, 0xAF, 0xC8,             // imul ecx, eax
        0x90, 0x90, 0x90, 0x90, 0x90, 0x90,
    };

    const auto address = reinterpret_cast<uintptr_t>(module) + kRva;
    patch_bytes(address, kExpected.data(), kPatch.data(), kPatch.size(),
                "Subnautica 2 save thumbnail VR crop dimensions");
}

} // namespace

class Subnautica2ThumbnailFixPlugin : public uevr::Plugin {
public:
    Subnautica2ThumbnailFixPlugin() = default;

    void on_initialize() override {
        const auto exe = current_exe_name();
        API::get()->log_info("Subnautica2ThumbnailFix loaded into %s",
                             exe.empty() ? "<unknown>" : exe.c_str());

        if (exe != "subnautica2-win64-shipping.exe") {
            API::get()->log_info("Subnautica 2 patch skipped, host is not subnautica2-win64-shipping.exe");
            return;
        }

        apply_subnautica2_save_thumbnail_patch();
    }
};

std::unique_ptr<Subnautica2ThumbnailFixPlugin> g_plugin{new Subnautica2ThumbnailFixPlugin()};
