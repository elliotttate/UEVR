// Sn2CaptureSidecar.cpp — implementation
//
// Snapshot UEVR state to JSON sidecar. Pulls from existing modules:
// - Sn2DupConfigFile entries
// - Sn2UweFogMirrorHook::registry mirrors
// - Sn2MagicInk active skips
// - Sn2RightCbSynth donor + GPU_VA
// - Process env vars (UEVR_SN2_*)

#include "Sn2CaptureSidecar.hpp"
#include "Sn2UweFogMirrorHook.hpp"
#include "Sn2MagicInk.hpp"
#include "Sn2RightCbSynth.hpp"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <vector>

#include <Windows.h>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

namespace sn2_capture_sidecar {

namespace {

std::string env_str(const char* name) {
    char buf[2048]{};
    const auto len = GetEnvironmentVariableA(name, buf, sizeof(buf));
    if (len == 0 || len >= sizeof(buf)) return {};
    return std::string{buf, len};
}

std::string now_iso8601() {
    using namespace std::chrono;
    auto now = system_clock::now();
    auto t = system_clock::to_time_t(now);
    std::tm gm{};
    gmtime_s(&gm, &t);
    std::ostringstream ss;
    ss << std::put_time(&gm, "%Y-%m-%dT%H:%M:%SZ");
    return ss.str();
}

// Collect all UEVR_SN2_* env vars from process environment.
nlohmann::json collect_env_vars() {
    nlohmann::json out = nlohmann::json::object();
    LPCH env = GetEnvironmentStringsA();
    if (env == nullptr) return out;
    char* p = env;
    while (*p) {
        const std::string entry{p};
        const auto eq = entry.find('=');
        if (eq != std::string::npos) {
            const auto name = entry.substr(0, eq);
            if (name.rfind("UEVR_SN2_", 0) == 0 ||
                name.rfind("UEVR_SUBNAUTICA2_", 0) == 0 ||
                name == "UEVR_ENABLE_D3D12_DIAGNOSTIC_COMMAND_LIST_HOOKS") {
                out[name] = entry.substr(eq + 1);
            }
        }
        p += entry.size() + 1;
    }
    FreeEnvironmentStringsA(env);
    return out;
}

}  // namespace

bool env_enabled() {
    static const bool e = []() {
        return !env_str("UEVR_SN2_CAPTURE_SIDECAR_DIR").empty();
    }();
    return e;
}

const std::string& output_dir() {
    static const std::string s = env_str("UEVR_SN2_CAPTURE_SIDECAR_DIR");
    return s;
}

void emit(uint64_t seq) {
    if (!env_enabled()) return;
    namespace fs = std::filesystem;
    std::error_code ec;
    fs::create_directories(output_dir(), ec);

    nlohmann::json doc;
    doc["schema_version"] = 1;
    doc["emitted_at_iso8601"] = now_iso8601();
    doc["seq"] = seq;
    doc["env_vars"] = collect_env_vars();

    // Process-info
    doc["pid"] = static_cast<uint32_t>(GetCurrentProcessId());

    // dup_cfg path (resolved from env)
    doc["dup_cfg_file"] = env_str("UEVR_SN2_DUP_CONFIG_FILE");

    // Synth donor state (best-effort — accessor is in Sn2RightCbSynth)
    {
        nlohmann::json synth;
        synth["donor_crc"] = env_str("UEVR_SN2_RIGHT_CB_SYNTH_DONOR");
        synth["donor_root"] = env_str("UEVR_SN2_RIGHT_CB_SYNTH_ROOT");
        synth["size"] = env_str("UEVR_SN2_RIGHT_CB_SYNTH_SIZE");
        doc["synth_donor"] = synth;
    }

    // Magic ink file path (the live-reload skip file)
    doc["magic_ink_file"] = env_str("UEVR_SN2_MAGIC_INK_SKIP_FILE");

    // Debug color override file (if active)
    doc["debug_color_override_file"] = env_str("UEVR_SN2_DEBUG_COLOR_OVERRIDE_FILE");

    // Active mirrors inventory.
    {
        nlohmann::json mirrors = nlohmann::json::array();
        auto& reg = sn2_uwe_fog_mirror::registry();
        std::scoped_lock _{reg.mu};
        for (const auto& [game_ptr, m] : reg.map) {
            nlohmann::json me;
            me["game_ptr"] = reinterpret_cast<uintptr_t>(game_ptr);
            me["mirror_ptr"] = reinterpret_cast<uintptr_t>(m.resource.Get());
            me["dim"] = {
                {"width", static_cast<unsigned>(m.desc.Width)},
                {"height", static_cast<unsigned>(m.desc.Height)},
                {"depth_or_array", static_cast<unsigned>(m.desc.DepthOrArraySize)}
            };
            me["format"] = static_cast<unsigned>(m.desc.Format);
            me["flags"] = static_cast<unsigned>(m.desc.Flags);
            me["dimension"] = static_cast<unsigned>(m.desc.Dimension);
            me["has_rtv"] = m.has_rtv;
            me["seq"] = m.seq;
            mirrors.push_back(me);
        }
        doc["active_mirrors"] = mirrors;
    }

    // Magic ink skip list.
    {
        const auto skip_set = sn2_magic_ink::skip_crcs();
        nlohmann::json arr = nlohmann::json::array();
        for (uint32_t crc : skip_set) {
            char buf[16];
            std::snprintf(buf, sizeof(buf), "0x%08x", crc);
            arr.push_back(buf);
        }
        doc["magic_ink_skips"] = arr;
        doc["magic_ink_eye"] = sn2_magic_ink::skip_eye_bucket();
    }

    // Synth donor CB content — dump as side-binary the bridge can SetBufferOverrideGPU with.
    if (sn2_right_cb_synth::initialized() && sn2_right_cb_synth::snapshot_count() > 0) {
        std::vector<uint8_t> synth_bytes(4096);
        const size_t n = sn2_right_cb_synth::copy_snapshot_bytes(synth_bytes.data(), synth_bytes.size());
        if (n > 0) {
            synth_bytes.resize(n);
            char bin_name[64];
            std::snprintf(bin_name, sizeof(bin_name),
                          "synth_donor_cb_seq%04llu.bin",
                          static_cast<unsigned long long>(seq));
            const auto bin_path = (fs::path(output_dir()) / bin_name).string();
            std::ofstream bf(bin_path, std::ios::binary);
            if (bf.good()) {
                bf.write(reinterpret_cast<const char*>(synth_bytes.data()),
                         synth_bytes.size());
            }
            doc["synth_donor_cb_dump"] = bin_name;
            doc["synth_donor_cb_size"] = n;
            doc["synth_donor_gpu_va"] = static_cast<uint64_t>(sn2_right_cb_synth::get_right_va());
            doc["synth_donor_snapshot_count"] = sn2_right_cb_synth::snapshot_count();
        }
    }

    char fname[64];
    std::snprintf(fname, sizeof(fname), "sidecar_seq%04llu.json",
                  static_cast<unsigned long long>(seq));
    const auto path = (fs::path(output_dir()) / fname).string();
    std::ofstream f(path);
    if (f.good()) f << doc.dump(2);

    static std::atomic<uint64_t> count{0};
    const auto n = count.fetch_add(1, std::memory_order_relaxed) + 1;
    SPDLOG_WARN("[SN2-CaptureSidecar] #{} seq={} wrote {}", n, seq, path);
}

}  // namespace sn2_capture_sidecar
