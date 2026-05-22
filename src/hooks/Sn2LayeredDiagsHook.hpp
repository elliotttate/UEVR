// Sn2LayeredDiagsHook.hpp
//
// Layered live diagnostics that build on Sn2RootSigDumpHook + Sn2MissingPassDiffHook.
//
// THIS FILE IS A SCAFFOLD. The four diagnostics below have agreed-upon
// schemas and env-var gates but the per-call recording sites are TODO-marked
// — wire them into D3D12Hook the same way Sn2MissingPassDiffHook::record()
// is wired into draw_indexed_instanced.
//
// PARTS
// -----
//
// (4) ResourceBindingLedger
//     Hook SetGraphicsRoot{CBV,SRV,UAV} + SetGraphicsRootDescriptorTable.
//     Log GPU_VA / size keyed by (PSO_ptr, root_param, eye_bucket).
//     Output: per-frame CSV.
//
// (5) TextureWriteHistory
//     For ONE configured ID3D12Resource* (UEVR_SN2_TEX_WRITE_HISTORY_RESOURCE_ID),
//     log every draw/dispatch/copy that targets it with PSO + eye_bucket.
//     Output: per-frame JSONL.
//
// (6) VisiblePixelMapper
//     "Force one PSO to write red" mode. Env-list of PS_CRCs → each one writes
//     solid color so you can visually identify which on-screen region each
//     PSO produces. Implemented via PSO bytecode replacement at PSO creation
//     time (similar to existing sn2_pso3069_subst).
//
// (7) ViewCbContentDumper
//     Periodically Map() and dump the LEFT and RIGHT View CB contents to disk,
//     byte-diffed. Confirms the matrices are actually distinct per eye.
//
// All four read sn2_root_sig_dump output (the JSON map) to look up "which
// root param holds View CB" for a given PSO — no more hardcoding.

#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <spdlog/spdlog.h>

namespace sn2_resource_binding_ledger {

inline bool env_enabled() {
    static const bool e = []() {
        const char* v = std::getenv("UEVR_SN2_BINDING_LEDGER_CSV");
        return v != nullptr && v[0] != '\0';
    }();
    return e;
}

inline const std::string& output_path() {
    static const std::string p = []() {
        const char* v = std::getenv("UEVR_SN2_BINDING_LEDGER_CSV");
        return std::string{v != nullptr ? v : ""};
    }();
    return p;
}

struct BindingRecord {
    uint64_t frame{};
    uintptr_t pso{};
    uint32_t ps_crc{};
    int8_t eye_bucket{-1};
    uint32_t root_param{};
    char kind{'C'};  // 'C'BV, 'S'RV, 'U'AV, 'T'able
    uint64_t gpu_va{};
    uint32_t value_count{};  // for tables: descriptor count
};

inline std::mutex& mu() { static std::mutex m; return m; }
inline std::vector<BindingRecord>& buffer() { static std::vector<BindingRecord> b; return b; }
inline std::atomic<uint64_t>& current_frame() { static std::atomic<uint64_t> f{0}; return f; }

// Hot path. Call from each SetGraphicsRoot{CBV,SRV,UAV,DescriptorTable} hook.
inline void record_root_cbv(uintptr_t pso, uint32_t ps_crc, int eye_bucket,
                            uint32_t root_param, uint64_t gpu_va) {
    if (!env_enabled()) return;
    std::scoped_lock _{mu()};
    buffer().push_back(BindingRecord{
        current_frame().load(std::memory_order_relaxed),
        pso, ps_crc, static_cast<int8_t>(eye_bucket),
        root_param, 'C', gpu_va, 1});
}

inline void record_root_table(uintptr_t pso, uint32_t ps_crc, int eye_bucket,
                              uint32_t root_param, uint64_t base_gpu_handle) {
    if (!env_enabled()) return;
    std::scoped_lock _{mu()};
    buffer().push_back(BindingRecord{
        current_frame().load(std::memory_order_relaxed),
        pso, ps_crc, static_cast<int8_t>(eye_bucket),
        root_param, 'T', base_gpu_handle, 0});
}

inline void on_present() {
    if (!env_enabled()) return;
    const auto f = current_frame().fetch_add(1, std::memory_order_relaxed) + 1;
    // Flush every ~60 frames.
    if ((f % 60) != 0) return;
    std::scoped_lock _{mu()};
    if (buffer().empty()) return;
    std::ofstream out{output_path(), std::ios::app};
    if (!out.good()) {
        buffer().clear();
        return;
    }
    static std::atomic<bool> header{false};
    if (!header.exchange(true, std::memory_order_relaxed)) {
        out << "# uevr.sn2.binding_ledger.v1\n";
        out << "# columns: frame,pso,ps_crc,eye,root_param,kind,gpu_va,count\n";
    }
    char buf[160];
    for (const auto& r : buffer()) {
        std::snprintf(buf, sizeof(buf),
                      "%llu,0x%llx,0x%08x,%d,%u,%c,0x%llx,%u\n",
                      static_cast<unsigned long long>(r.frame),
                      static_cast<unsigned long long>(r.pso),
                      r.ps_crc,
                      static_cast<int>(r.eye_bucket),
                      r.root_param,
                      r.kind,
                      static_cast<unsigned long long>(r.gpu_va),
                      r.value_count);
        out << buf;
    }
    buffer().clear();
}

}  // namespace sn2_resource_binding_ledger


namespace sn2_texture_write_history {

inline uintptr_t target_resource_ptr() {
    static const uintptr_t p = []() -> uintptr_t {
        const char* v = std::getenv("UEVR_SN2_TEX_WRITE_HISTORY_RESOURCE_PTR");
        if (v == nullptr || *v == '\0') return 0;
        return static_cast<uintptr_t>(std::strtoull(v, nullptr, 0));
    }();
    return p;
}

inline bool env_enabled() { return target_resource_ptr() != 0; }

inline const std::string& output_path() {
    static const std::string p = []() {
        const char* v = std::getenv("UEVR_SN2_TEX_WRITE_HISTORY_JSONL");
        return std::string{v != nullptr ? v : ""};
    }();
    return p;
}

// Call from each draw/dispatch/copy hook that has a chance to write to this
// resource. Cheap if target not configured (single atomic load).
inline void record_write(uintptr_t resource, uintptr_t pso, uint32_t ps_or_cs_crc,
                         int eye_bucket, const char* kind /* "draw"|"dispatch"|"copy" */) {
    if (!env_enabled() || resource != target_resource_ptr()) return;
    static std::mutex mu;
    static std::ofstream out;
    static bool opened = false;
    std::scoped_lock _{mu};
    if (!opened) {
        out.open(output_path(), std::ios::app);
        opened = true;
        if (out.good()) out << "{\"schema\":\"uevr.sn2.tex_write_history.v1\"}\n";
    }
    if (!out.good()) return;
    char buf[256];
    std::snprintf(buf, sizeof(buf),
                  "{\"pso\":\"0x%llx\",\"crc\":\"0x%08x\",\"eye\":%d,\"kind\":\"%s\"}\n",
                  static_cast<unsigned long long>(pso),
                  ps_or_cs_crc,
                  eye_bucket,
                  kind);
    out << buf;
    out.flush();
}

}  // namespace sn2_texture_write_history


namespace sn2_view_cb_content_dumper {

inline bool env_enabled() {
    static const bool e = []() {
        const char* v = std::getenv("UEVR_SN2_VIEW_CB_DUMP_DIR");
        return v != nullptr && v[0] != '\0';
    }();
    return e;
}

// TODO: hook the engine's CB-upload Map() / WriteToSubresource path to grab
// per-eye View CB contents. Dump LEFT + RIGHT byte ranges to numbered files
// once per N frames so a diff can be made offline. The agent's Round 2
// finding (276-byte SLW cbuffer at pool 2038 offset 1919744 LEFT / 1922560
// RIGHT with diverging f@120,124,196,220) is the surface to dump first.
inline void on_present() {
    if (!env_enabled()) return;
    // TODO
}

}  // namespace sn2_view_cb_content_dumper


namespace sn2_visible_pixel_mapper {

inline bool env_enabled() {
    static const bool e = []() {
        const char* v = std::getenv("UEVR_SN2_VISIBLE_PIXEL_MAPPER_CRCS");
        return v != nullptr && v[0] != '\0';
    }();
    return e;
}

// TODO: at create_graphics_pipeline_state time, if the PS CRC is in the
// configured list, substitute the PS bytecode with one that writes solid
// magenta (or similar) to RTV slot 0. Pattern mirrors existing
// sn2_pso3069_subst::replace_ps_if_match in D3D12Hook.cpp.
//
// Cheaper alternative (no PSO replacement): inject a magenta-clear
// ClearRenderTargetView call before each tagged draw — easier but only works
// when the draw fully overwrites the target.

}  // namespace sn2_visible_pixel_mapper
