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

// Static fog-pipeline classification, shared by the .rdc comments string and the
// .rdc-keyed JSON manifest. Producers fill the volumetric froxel; consumers
// sample it; the composite blends the result into scene color. These CRCs are
// the SN2 SingleLayerWater / UWE fog chain identified across many RE sessions.
struct CrcRole {
    const char* crc;   // lowercase 0x… DXIL crc32
    const char* name;  // shader name
    const char* role;  // producer | consumer | composite
};

const CrcRole kFogCrcRoles[] = {
    // Producers — write the integrated light-scattering / froxel volume.
    {"0xd1d94ed1", "MaterialSetupCS",        "producer"},
    {"0xd1f85c42", "LightScatteringCS",      "producer"},
    {"0x3402487c", "FinalIntegrationCS",     "producer"},
    {"0x0930dd4e", "UWEFogResolveCS",        "producer"},
    // Consumers — sample the froxel (SingleLayerWater + underwater teal draw).
    {"0xb9be2499", "SLW",                    "consumer"},
    {"0xde7c3822", "SLW",                    "consumer"},
    {"0x4a4eb78c", "SLW_VolumeOverlay-pathB","consumer"},
    {"0x13b00f0c", "UnderwaterTealDraw",     "consumer"},
    // Composite — blends fog into scene color.
    {"0x4e86dc09", "Composite",              "composite"},
};

// SN2_* SetName prefixes the resource-naming agent emits, mapped to a coarse
// role so replay python can regex-classify resources straight from the .rdc.
struct PrefixRole {
    const char* prefix;
    const char* role;
};

const PrefixRole kResourceNamePrefixRoles[] = {
    {"SN2_IntegratedLightScattering_froxel", "froxel"},
    {"SN2_SceneColorSBS",                    "scene-color"},
    {"SN2_Backbuffer_",                      "present"},
    {"SN2_OpenXR_Array_Slice",               "VR-eye"},
    {"SN2_PSO|",                             "pso"},
};

const char* kWorkingHypothesis =
    "Right eye runs the full fog chain (producers + consumers fire for both "
    "eyes) but reads an empty / wrong-region froxel via SV_Position screen-half "
    "addressing (reg254 = 1/2560) — the bug is fog DATA, not a missing draw.";

const char* kSuccessCriterion =
    "Right eye shows its OWN underwater teal with its OWN parallax, stable "
    "across frames, with no left-eye borrowing.";

// Build the three classification maps shared by comments + manifest.
nlohmann::json build_crc_role_map() {
    nlohmann::json m = nlohmann::json::object();
    for (const auto& e : kFogCrcRoles) {
        m[e.crc] = {{"name", e.name}, {"role", e.role}};
    }
    return m;
}

nlohmann::json build_eye_map() {
    // Froxel dims are family-shared; each eye samples its own screen-half.
    const nlohmann::json froxel_dims = {107, 30, 48};
    nlohmann::json m = nlohmann::json::object();
    m["left"] = {
        {"view_index", 0},
        {"expected_froxel_role", "filled (own screen-half)"},
        {"froxel_dims", froxel_dims},
    };
    m["right"] = {
        {"view_index", 1},
        {"expected_froxel_role", "filled (own screen-half) — currently empty (bug)"},
        {"froxel_dims", froxel_dims},
    };
    return m;
}

nlohmann::json build_prefix_role_map() {
    nlohmann::json m = nlohmann::json::object();
    for (const auto& e : kResourceNamePrefixRoles) {
        m[e.prefix] = e.role;
    }
    return m;
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
    doc["capture_artifact_dir"] = env_str("UEVR_SN2_CAPTURE_ARTIFACT_DIR");
    doc["capture_truth_path"] = env_str("UEVR_SN2_CAPTURE_TRUTH_PATH").empty()
        ? env_str("UEVR_SN2_BINDLESS_FOG_TRACE_PATH")
        : env_str("UEVR_SN2_CAPTURE_TRUTH_PATH");

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

std::string emit_rdc_manifest(const std::string& rdc_path) {
    if (rdc_path.empty()) return {};

    nlohmann::json doc;
    doc["schema_version"] = 1;
    doc["kind"] = "uevr_sn2_rdc_manifest";
    doc["emitted_at_iso8601"] = now_iso8601();
    doc["pid"] = static_cast<uint32_t>(GetCurrentProcessId());
    doc["rdc_path"] = rdc_path;

    // (a) crc -> {name, role}
    doc["crc_roles"] = build_crc_role_map();
    // (b) eye -> {view_index, expected_froxel_role, froxel_dims}
    doc["eyes"] = build_eye_map();
    // (c) resource_name_prefix -> role
    doc["resource_name_prefix_roles"] = build_prefix_role_map();

    // Froxel / shader-register reference constants (RE-confirmed).
    doc["froxel"] = {
        {"dims", {107, 30, 48}},
        {"format", "R11G11B10F"},
        {"note", "family-shared; each eye samples its own screen-half via SV_Position"},
    };
    doc["shader_registers"] = {
        {"reg148", "ViewRectMin"},
        {"reg254", "VolumetricFogSVPosToVolumeUV (= 1/2560)"},
        {"reg257", "grid dims"},
    };

    // Live UEVR_SN2_* env snapshot (reuses the same collector as emit()).
    doc["env_vars"] = collect_env_vars();

    // Working hypothesis + success criterion for replay tooling / humans.
    doc["working_hypothesis"] = kWorkingHypothesis;
    doc["success_criterion"] = kSuccessCriterion;

    // Write "<rdc_path>.uevr.json" right beside the capture.
    const std::string manifest_path = rdc_path + ".uevr.json";
    std::error_code ec;
    std::filesystem::create_directories(
        std::filesystem::path{manifest_path}.parent_path(), ec);
    std::ofstream f(manifest_path);
    if (!f.good()) {
        SPDLOG_WARN("[SN2-CaptureSidecar] rdc-manifest open failed: {}", manifest_path);
        return {};
    }
    f << doc.dump(2);

    static std::atomic<uint64_t> count{0};
    const auto n = count.fetch_add(1, std::memory_order_relaxed) + 1;
    SPDLOG_WARN("[SN2-CaptureSidecar] rdc-manifest #{} wrote {}", n, manifest_path);
    return manifest_path;
}

}  // namespace sn2_capture_sidecar
