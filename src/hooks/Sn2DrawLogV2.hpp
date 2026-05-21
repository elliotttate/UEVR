// Sn2DrawLogV2.hpp
//
// Extended semantic draw-log schema for SN2 right-eye / fog investigation.
// Adds frame_index, cmdlist_id, pass_tag, cb fingerprints, descriptor heap
// revision, root signature content hash, view_idx (explicit), pso_root_sig_hash.
//
// This module is HEADER-ONLY so it can be included into D3D12Hook.cpp at a single
// call site (sn2_log_render_name_pso3069) without restructuring the hook file.
//
// Gating: emits only when env UEVR_SN2_DRAWLOG_V2=1.
//
// Companion files:
//   E:/Github/Subnautica 2/moddingkit/render_names/sn2_render_dictionary.json
//   E:/Github/Subnautica 2/moddingkit/render_names/pso3069_shader_semantics.json
//
#pragma once

#include <atomic>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <unordered_map>
#include <spdlog/spdlog.h>
#include <d3d12.h>

namespace sn2_draw_log_v2 {

// --- frame index & descriptor heap revision (incremented elsewhere) ---

inline std::atomic<uint64_t> g_frame_index{0};        // bumped in Present hook
inline std::atomic<uint64_t> g_desc_heap_revision{0}; // bumped in CopyDescriptors hook

// --- env gating (declared before the counter hooks so they can short-circuit) ---

inline bool enabled() {
    static const bool e = [](){
        const char* v = std::getenv("UEVR_SN2_DRAWLOG_V2");
        return v != nullptr && v[0] != '\0' && v[0] != '0';
    }();
    return e;
}

// Counter bumps short-circuit when V2 is off so we don't pay the atomic-rmw
// cost on every Present / CopyDescriptors call in shipping configurations.
inline void on_present()        { if (enabled()) g_frame_index.fetch_add(1, std::memory_order_relaxed); }
inline void on_copy_descriptors() { if (enabled()) g_desc_heap_revision.fetch_add(1, std::memory_order_relaxed); }

inline uint64_t current_frame()  { return g_frame_index.load(std::memory_order_relaxed); }
inline uint64_t current_desc_rev(){ return g_desc_heap_revision.load(std::memory_order_relaxed); }

// --- pass tag from PS CRC (until we have a full PSO table) ---

inline const char* pass_tag_from_ps_crc(uint32_t ps_crc) {
    switch (ps_crc) {
        case 0x166DBA88u: return "basepass.uwewater";
        case 0x18B0D90Au: return "basepass.variant2";
        case 0x6E79C7F3u: return "sky.raymarch";
        case 0x8733F2E0u: return "post.candidate";
        case 0xE85849AAu: return "post.copyrect";
        default:          return "unknown";
    }
}

// --- 64-bit hash of root signature contents (best-effort from blob pointer) ---
// We don't always have the blob; fall back to the GPU VA of the bound RS object.
inline uint64_t root_sig_hash(uint64_t bound_rs_gpu_va) {
    // FNV-1a over the 8 bytes of the bound-RS pointer. Stable for the lifetime
    // of the process; doesn't survive a re-create. For cross-session stability,
    // the consumer should pair this with `pso_dxil_hash` from the render dict.
    uint64_t h = 0xcbf29ce484222325ULL;
    for (int i = 0; i < 8; ++i) {
        h ^= (bound_rs_gpu_va >> (i*8)) & 0xFF;
        h *= 0x100000001b3ULL;
    }
    return h;
}

// --- cb fingerprint (first 32 bytes of CBV pointed at by GPU VA) ---
//
// NOTE: Reading bytes at a D3D12 GPU VA from the CPU is not directly possible —
// CBVs live in upload heaps mapped on the GPU. The cb pointer ITSELF is a hash
// surrogate: distinct CBs at different GPU VAs are distinguishable by the VA.
// If we want actual content, we need a parallel CPU-side mirror of recent
// CBV uploads (out of scope for V2). For now we hash the GPU VA + the
// allocation generation if available.

inline uint64_t cb_fingerprint(uint64_t cb_gpu_va) {
    uint64_t h = 0xcbf29ce484222325ULL;
    for (int i = 0; i < 8; ++i) {
        h ^= (cb_gpu_va >> (i*8)) & 0xFF;
        h *= 0x100000001b3ULL;
    }
    return h;
}

// --- the emit function ---

struct DrawLogV2Args {
    uint64_t seq;                         // the [SN2-RenderName] seq the row joins on
    int      view_idx;                    // 0=left, 1=right, -1=unknown
    const char* eye_name;                 // "left"/"right"/"other"
    uint64_t cmdlist_id;                  // = (uintptr_t)command_list
    uint32_t ps_crc;
    const char* pso_dxil_hash;            // from render dictionary, null if not known
    const char* pso_entry_function;       // from render dictionary, null if not known
    uint64_t root_sig_gpu_va;             // state.last_graphics_root_signature
    uint64_t rtv0_handle;
    float    vp_x, vp_y, vp_w, vp_h;
    int      viewport_bucket;             // -1, 0, 1
    uint64_t cb_root4_gpu;                // View cb in pso3069
    uint64_t cb_root5_gpu;                // TranslucentBasePass cb
    uint64_t cb_root6_gpu;                // VirtualShadowMap cb
    uint64_t cb_root7_gpu;                // Material cb
};

inline void emit(const DrawLogV2Args& a) {
    if (!enabled()) return;

    const uint64_t frame_idx = current_frame();
    const uint64_t desc_rev  = current_desc_rev();
    const uint64_t rs_hash   = root_sig_hash(a.root_sig_gpu_va);
    const uint64_t cb0_fp    = cb_fingerprint(a.cb_root4_gpu);
    const uint64_t cb1_fp    = cb_fingerprint(a.cb_root5_gpu);
    const uint64_t cb2_fp    = cb_fingerprint(a.cb_root6_gpu);
    const uint64_t cb3_fp    = cb_fingerprint(a.cb_root7_gpu);

    SPDLOG_WARN(
        "[SN2-DrawV2] seq={} frame_idx={} desc_rev={} cmdlist_id=0x{:x} "
        "view_idx={} eye={} viewport_bucket={} vp=({:.1f},{:.1f},{:.1f},{:.1f}) "
        "ps_crc=0x{:08x} pso_dxil_hash={} entry={} pass_tag={} "
        "pso_root_sig_hash=0x{:016x} root_sig_gpu_va=0x{:x} rtv0=0x{:x} "
        "cb0_gpu=0x{:x} cb0_fp=0x{:016x} cb1_gpu=0x{:x} cb1_fp=0x{:016x} "
        "cb2_gpu=0x{:x} cb2_fp=0x{:016x} cb3_gpu=0x{:x} cb3_fp=0x{:016x}",
        a.seq,
        frame_idx,
        desc_rev,
        a.cmdlist_id,
        a.view_idx,
        a.eye_name ? a.eye_name : "?",
        a.viewport_bucket,
        a.vp_x, a.vp_y, a.vp_w, a.vp_h,
        a.ps_crc,
        a.pso_dxil_hash ? a.pso_dxil_hash : "?",
        a.pso_entry_function ? a.pso_entry_function : "?",
        pass_tag_from_ps_crc(a.ps_crc),
        rs_hash,
        a.root_sig_gpu_va,
        a.rtv0_handle,
        a.cb_root4_gpu, cb0_fp,
        a.cb_root5_gpu, cb1_fp,
        a.cb_root6_gpu, cb2_fp,
        a.cb_root7_gpu, cb3_fp);
}

// --- override-fired ground-truth log ---
//
// Records the 4 timestamps that matter for "did our override actually land
// on the consumer draw" diagnosis:
//   intent_set_at_frame    : frame at which UEVR queued the intent (left-eye seq)
//   cl_recorded_at_frame   : frame at which CopyDescriptorsSimple ran on cmdlist
//   consumer_draw_at_frame : frame at which the patched binding was sampled
//   gpu_executed_at_frame  : frame at which ExecuteCommandLists ran the batch
//
// For the pso3069 fog scratch path, the first three are the SAME frame (intent
// + record + draw all happen in the right-eye consumer's emit). gpu_executed
// is filled by the ExecuteCommandLists hook. If a downstream reader sees
// gpu_executed_at_frame > consumer_draw_at_frame, the GPU ran the override on a
// frame AFTER the draw consumed it — i.e. the override missed.
//
// Emit format (JSONL-friendly): one line per fire. Joined to [SN2-RenderName]
// by `seq`. Gated by UEVR_SN2_DRAWLOG_V2=1 (same env as the V2 schema).

struct OverrideFireArgs {
    uint64_t    seq;                          // joins to [SN2-RenderName] seq
    const char* override_kind;                // "fog_scratch_root0_slots89" / "cb_redirect" / etc.
    uint64_t    intent_set_at_frame;          // when intent was captured (left-eye snapshot)
    uint64_t    cl_recorded_at_frame;         // when CopyDescriptorsSimple ran
    uint64_t    consumer_draw_at_frame;       // when the right-eye consumer draw fired
    uint64_t    cmdlist_id;
    uint32_t    target_ps_crc;
    int         target_view_idx;
    uint32_t    redirected_root;
    const char* redirected_slots_label;       // e.g. "8,9"
    uint64_t    left_seq;                     // left-side snapshot sequence (intent uniqueness)
    bool        scratch_table_bound;          // true if we actually bound the scratch table
};

inline void emit_override_fired(const OverrideFireArgs& a) {
    if (!enabled()) return;
    SPDLOG_WARN(
        "[SN2-OverrideFired] seq={} kind={} intent_set_at_frame={} cl_recorded_at_frame={} "
        "consumer_draw_at_frame={} gpu_executed_at_frame=PENDING cmdlist_id=0x{:x} "
        "target_ps_crc=0x{:08x} target_view_idx={} redirected_root={} redirected_slots={} "
        "left_seq={} scratch_table_bound={}",
        a.seq,
        a.override_kind ? a.override_kind : "?",
        a.intent_set_at_frame,
        a.cl_recorded_at_frame,
        a.consumer_draw_at_frame,
        a.cmdlist_id,
        a.target_ps_crc,
        a.target_view_idx,
        a.redirected_root,
        a.redirected_slots_label ? a.redirected_slots_label : "?",
        a.left_seq,
        a.scratch_table_bound ? 1 : 0);
}

// Called from the ExecuteCommandLists hook (or close to it). Logs the GPU-side
// frame index at which the cmdlist actually executed. Indexer joins this to
// the per-fire OverrideFired row by cmdlist_id. Multiple fires sharing the same
// cmdlist all resolve to the same gpu_executed_at_frame.
inline void emit_execute_command_lists(uint64_t cmdlist_id, uint32_t num_cmdlists) {
    if (!enabled()) return;
    SPDLOG_WARN(
        "[SN2-OverrideExec] frame_idx={} cmdlist_id_first=0x{:x} num_cmdlists={}",
        current_frame(),
        cmdlist_id,
        num_cmdlists);
}

// --- heap-slot lifetime tracker ---
//
// Per descriptor heap CPU slot, record:
//   first_seen_at_revision : the desc_heap_revision when we first observed this slot
//   last_write_at_revision : the desc_heap_revision at the most recent CopyDescriptors
//                            into this slot
//   write_count            : total observed writes
//
// Indexed by raw CPU handle (SIZE_T). Capped to ~64K entries to keep memory
// bounded; entries past the cap are dropped via simple insertion-order
// eviction (FIFO via two-pointer; we don't need LRU correctness here, just
// bounded memory).
//
// To query at draw-log time: call snapshot_slot(cpu_handle) which returns the
// current record by value. Logging is single-shot per call site.

namespace slot_lifetime {

struct Record {
    uint64_t first_seen_at_revision = 0;
    uint64_t last_write_at_revision = 0;
    uint64_t write_count = 0;
};

inline std::mutex& mu() {
    static std::mutex m;
    return m;
}

inline std::unordered_map<uint64_t, Record>& table() {
    static std::unordered_map<uint64_t, Record> t;
    return t;
}

inline constexpr size_t k_cap = 65536;

inline void on_write(uint64_t cpu_handle) {
    // Early-out before touching the global mutex / map when V2 is off. Without
    // this gate, every CopyDescriptors call (thousands per frame in UE5
    // bindless mode) acquires this mutex and probes the unordered_map, which
    // is wasted CPU + adds contention with the renderer's own paths.
    if (!enabled()) return;
    std::scoped_lock _{mu()};
    auto& t = table();
    if (t.size() >= k_cap) {
        // Drop a single random-ish entry to make room. Cheap & bounded.
        auto it = t.begin();
        if (it != t.end()) t.erase(it);
    }
    auto& r = t[cpu_handle];
    const uint64_t rev = current_desc_rev();
    if (r.write_count == 0) r.first_seen_at_revision = rev;
    r.last_write_at_revision = rev;
    r.write_count++;
}

inline Record snapshot(uint64_t cpu_handle) {
    std::scoped_lock _{mu()};
    auto& t = table();
    auto it = t.find(cpu_handle);
    if (it == t.end()) return Record{};
    return it->second;
}

inline size_t size() {
    std::scoped_lock _{mu()};
    return table().size();
}

} // namespace slot_lifetime

// Emit slot lifetimes for an array of (root_param, slot_idx, cpu_handle) tuples.
// Called once per pso3069 draw log row, after the existing [SN2-RenderName].
struct SlotLifetimeEmitArg {
    uint32_t root;
    uint32_t slot;
    uint64_t cpu_handle;
    const char* semantic_name; // optional, may be null
};

inline void emit_slot_lifetimes(uint64_t seq, const SlotLifetimeEmitArg* arr, size_t n) {
    if (!enabled()) return;
    for (size_t i = 0; i < n; ++i) {
        const auto rec = slot_lifetime::snapshot(arr[i].cpu_handle);
        SPDLOG_WARN(
            "[SN2-SlotLifetime] seq={} root={} slot={} semantic={} cpu_handle=0x{:x} "
            "first_seen_rev={} last_write_rev={} write_count={} table_size={}",
            seq,
            arr[i].root,
            arr[i].slot,
            arr[i].semantic_name ? arr[i].semantic_name : "?",
            arr[i].cpu_handle,
            rec.first_seen_at_revision,
            rec.last_write_at_revision,
            rec.write_count,
            slot_lifetime::size());
    }
}

// --- pso dxil hash lookup (mini in-process render-dictionary mirror) ---
//
// Subset of moddingkit/render_names/sn2_render_dictionary.json. Keep in sync
// when adding new PSOs. The full dictionary is the source of truth; this is
// just a fast O(1) lookup for the draw-log hot path.
struct PsoDictEntry {
    uint32_t    ps_crc;
    const char* dxil_hash;
    const char* entry_function;
};

inline const PsoDictEntry* lookup_pso(uint32_t ps_crc) {
    static constexpr PsoDictEntry k_table[] = {
        { 0x166DBA88u, "e739f41c284b20e07a28dc30e34a47ea", "MainPS" },                   // pso3069
        { 0x18B0D90Au, "18b0d90a86d86dd8647c9eab739075a5", "MainPS" },                   // pso3113
        { 0x6E79C7F3u, "6e79c7f3b5776b62b67cd355cdd36f6d", "RenderSkyAtmosphereRayMarchingPS" }, // pso2993
    };
    for (const auto& e : k_table) {
        if (e.ps_crc == ps_crc) return &e;
    }
    return nullptr;
}

} // namespace sn2_draw_log_v2
