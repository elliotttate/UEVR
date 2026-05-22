// Sn2FrameCapture.cpp — implementation
//
// Orchestrates all UEVR capture sub-modules around a common capture sequence.
// Triggers each sub-module's existing trigger mechanism + emits a manifest
// pointing at everything produced.

#include "Sn2FrameCapture.hpp"
#include "Sn2CaptureSidecar.hpp"
#include "Sn2ResourceReadback.hpp"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>

#include <Windows.h>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

namespace sn2_frame_cpp_export {
void arm_capture();
bool env_enabled();
}

namespace sn2_frame_capture {

namespace {

std::string env_str(const char* name) {
    char buf[2048]{};
    const auto len = GetEnvironmentVariableA(name, buf, sizeof(buf));
    if (len == 0 || len >= sizeof(buf)) return {};
    return std::string{buf, len};
}

std::string now_iso8601() {
    using namespace std::chrono;
    auto t = system_clock::to_time_t(system_clock::now());
    std::tm gm{};
    gmtime_s(&gm, &t);
    std::ostringstream ss;
    ss << std::put_time(&gm, "%Y-%m-%dT%H:%M:%SZ");
    return ss.str();
}

struct State {
    std::atomic<bool> trigger_pending{false};
    std::atomic<uint64_t> seq{0};
    std::string last_manifest;
};

State& state() { static State s; return s; }

}  // namespace

bool env_enabled() {
    static const bool e = []() {
        return !env_str("UEVR_SN2_FRAME_CAPTURE_DIR").empty();
    }();
    return e;
}

const std::string& base_dir() {
    static const std::string s = env_str("UEVR_SN2_FRAME_CAPTURE_DIR");
    return s;
}

const std::string& trigger_file_path() {
    static const std::string s = []() -> std::string {
        const auto v = env_str("UEVR_SN2_FRAME_CAPTURE_TRIGGER_FILE");
        return v.empty() ? std::string{"C:\\tmp\\uevr_frame_cap.txt"} : v;
    }();
    return s;
}

void request_capture_next_frame() {
    state().trigger_pending.store(true, std::memory_order_release);
}

namespace {

void execute_capture(uint64_t seq, uint64_t frame_count) {
    namespace fs = std::filesystem;
    char dir_name[64];
    std::snprintf(dir_name, sizeof(dir_name), "capture_%04llu",
                  static_cast<unsigned long long>(seq));
    fs::path capture_dir = fs::path(base_dir()) / dir_name;
    std::error_code ec;
    fs::create_directories(capture_dir, ec);

    nlohmann::json manifest;
    manifest["schema_version"] = 1;
    manifest["seq"] = seq;
    manifest["frame_count"] = frame_count;
    manifest["emitted_at_iso8601"] = now_iso8601();
    manifest["base_dir"] = base_dir();
    manifest["capture_dir"] = capture_dir.string();
    manifest["pid"] = static_cast<uint32_t>(GetCurrentProcessId());

    nlohmann::json artifacts = nlohmann::json::object();

    // 1) Sidecar — always emit if enabled. Sn2CaptureSidecar writes to its
    //    own configured dir; we record the (expected) path here.
    if (sn2_capture_sidecar::env_enabled()) {
        sn2_capture_sidecar::emit(seq);
        const auto dir = env_str("UEVR_SN2_CAPTURE_SIDECAR_DIR");
        char sidecar_file[64];
        std::snprintf(sidecar_file, sizeof(sidecar_file),
                      "sidecar_seq%04llu.json",
                      static_cast<unsigned long long>(seq));
        char donor_file[64];
        std::snprintf(donor_file, sizeof(donor_file),
                      "synth_donor_cb_seq%04llu.bin",
                      static_cast<unsigned long long>(seq));
        artifacts["sidecar"] = {
            {"module", "Sn2CaptureSidecar"},
            {"output_dir", dir},
            {"sidecar_path", dir + "\\" + sidecar_file},
            {"binary_path", dir + "\\" + donor_file},
        };
    }

    // 2) Trigger eye screenshot — Sn2EyeScreenshot uses its own file trigger.
    //    We can't directly call its API, but we can write its trigger file.
    {
        const auto trigger = env_str("UEVR_SN2_EYE_SCREENSHOT_TRIGGER_FILE");
        if (!trigger.empty()) {
            std::ofstream f(trigger);
            if (f.good()) {
                f << "request";
                artifacts["eye_screenshot"] = {
                    {"module", "Sn2EyeScreenshot"},
                    {"output_dir_env", "UEVR_SN2_EYE_SCREENSHOT_OUTPUT_DIR"},
                    {"files", {"left.ppm", "right.ppm", "backbuffer.ppm", "done.txt"}},
                };
            }
        }
    }

    // 2b) Resource readback — copy contents of configured/mirrored resources
    //     to disk. Asynchronous: trigger queues, on_present drains.
    if (sn2_resource_readback::env_enabled()) {
        const auto queued = sn2_resource_readback::trigger(seq);
        const auto d = env_str("UEVR_SN2_RES_READBACK_DIR");
        artifacts["resource_readback"] = {
            {"module", "Sn2ResourceReadback"},
            {"output_dir", d},
            {"queued_this_capture", queued},
            {"filename_pattern", "res_seq####_0x*_*x*x*_fmt*.bin/.json"},
        };
    }

    // 3) Arm Sn2FrameCppExport — captures next frame's D3D12 events to JSON
    if (sn2_frame_cpp_export::env_enabled()) {
        sn2_frame_cpp_export::arm_capture();
        artifacts["frame_cpp_export"] = {
            {"module", "Sn2FrameCppExport"},
            {"output_dir_env", "UEVR_SN2_FRAME_CAPTURE_DIR"},
            {"files", {"events.json", "capture_frame.cpp", "main.cpp",
                       "CMakeLists.txt", "README.md"}},
        };
    }

    // 4) State inspector / CB dumper / PSO bytecode dumper / mesh dump are
    //    all env-driven on their own filter sets — they may already be
    //    emitting per their own configuration. We just enumerate them in
    //    the manifest so the analyzer knows where to look.
    {
        const auto d = env_str("UEVR_SN2_STATE_INSPECTOR_DIR");
        if (!d.empty()) {
            artifacts["state_inspector"] = {
                {"module", "Sn2StateInspector"},
                {"output_dir", d},
                {"filename_pattern", "state_0x*_eye*_seq*.json"},
            };
        }
    }
    {
        const auto d = env_str("UEVR_SN2_CB_DUMP_DIR");
        if (!d.empty()) {
            artifacts["cb_dumps"] = {
                {"module", "Sn2CbDumper"},
                {"output_dir", d},
                {"filename_pattern", "G_0x*_root*_eye*_seq*.bin/.json"},
            };
        }
    }
    {
        const auto d = env_str("UEVR_SN2_PSO_BYTECODE_DIR");
        if (!d.empty()) {
            artifacts["pso_bytecode"] = {
                {"module", "Sn2PsoBytecodeDumper"},
                {"output_dir", d},
                {"filename_pattern", "pso_0x*_PS.dxbc / pso_0x*_CS.dxbc"},
            };
        }
    }
    {
        const auto d = env_str("UEVR_SN2_MESH_DUMP_DIR");
        if (!d.empty()) {
            artifacts["mesh_dumps"] = {
                {"module", "Sn2MeshDump"},
                {"output_dir", d},
                {"filename_pattern", "mesh_0x*_seq*.json"},
            };
        }
    }
    {
        const char* v = std::getenv("UEVR_SN2_GPU_COUNTERS");
        if (v && v[0] && v[0] != '0') {
            auto p = env_str("UEVR_SN2_GPU_COUNTERS_DUMP");
            if (p.empty()) p = "C:\\tmp\\sn2_gpu_counters.json";
            artifacts["gpu_counters"] = {
                {"module", "Sn2GpuCounters"},
                {"output_path", p},
            };
        }
    }
    {
        const auto d = env_str("UEVR_SN2_RES_TIMELINE_DIR");
        if (!d.empty()) {
            artifacts["resource_timeline"] = {
                {"module", "Sn2ResourceTimeline"},
                {"output_dir", d},
                {"filename_pattern", "res_0x*.json"},
            };
        }
    }
    {
        const char* v = std::getenv("UEVR_SN2_DESCRIPTOR_LINEAGE");
        if (v && v[0] && v[0] != '0') {
            auto p = env_str("UEVR_SN2_DESCRIPTOR_LINEAGE_DUMP");
            if (p.empty()) p = "C:\\tmp\\desc_lineage.json";
            artifacts["descriptor_lineage"] = {
                {"module", "Sn2DescriptorLineage"},
                {"output_path", p},
            };
        }
    }
    {
        const auto d = env_str("UEVR_SN2_EYE_SCREENSHOT_OUTPUT_DIR");
        if (!d.empty()) {
            // eye_screenshot was added earlier; merge resolved dir
            if (artifacts.contains("eye_screenshot")) {
                artifacts["eye_screenshot"]["output_dir"] = d;
            }
        }
    }

    manifest["artifacts"] = artifacts;

    // Also dump useful frame metadata: viewport, screen size, render time
    // (placeholder for future readback metadata).
    manifest["frame_metadata"] = {
        {"note", "Per-frame metadata can be enriched by hooking Sn2GpuCounters / RTV captures."},
    };

    // Write manifest.
    const auto manifest_path = (capture_dir / "manifest.json").string();
    std::ofstream f(manifest_path);
    if (f.good()) f << manifest.dump(2);

    {
        auto& s = state();
        s.last_manifest = manifest_path;
    }

    SPDLOG_WARN("[SN2-FrameCapture] capture #{} (frame={}) -> {}",
                seq, frame_count, manifest_path);
}

}  // namespace

void on_present(uint64_t frame_count) {
    if (!env_enabled()) return;
    auto& s = state();
    // Poll trigger file every 30 frames (~0.5s @ 60fps).
    if ((frame_count % 30) == 0) {
        DWORD attrs = GetFileAttributesA(trigger_file_path().c_str());
        if (attrs != INVALID_FILE_ATTRIBUTES &&
            !(attrs & FILE_ATTRIBUTE_DIRECTORY)) {
            s.trigger_pending.store(true, std::memory_order_release);
            DeleteFileA(trigger_file_path().c_str());
        }
    }
    if (s.trigger_pending.exchange(false, std::memory_order_acq_rel)) {
        const auto seq = s.seq.fetch_add(1, std::memory_order_relaxed) + 1;
        execute_capture(seq, frame_count);
    }
}

uint64_t capture_count() {
    return state().seq.load(std::memory_order_relaxed);
}

const std::string& last_manifest_path() {
    return state().last_manifest;
}

}  // namespace sn2_frame_capture
