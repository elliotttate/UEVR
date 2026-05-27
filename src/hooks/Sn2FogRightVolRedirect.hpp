// Sn2FogRightVolRedirect.hpp
//
// PLAN A-i (2026-05-24): per-eye fog-volume SRV redirect.
//
// ROOT (clean no-UEVR capture + live probe confirmed): the game's visible
// underwater-fog reconstruction passes — UWEFogReconstructCS (0xf996b96b) and
// UWEFogResolveCS (0x0930dd4e) — bind the LEFT eye's IntegratedLightScattering
// volume on the RIGHT-eye run. The right eye's own volume IS built correctly
// (FinalIntegrationCS) and consumed correctly by the per-eye MainCS (0x5af52812,
// binds it at SRV slots 11/12), but the reconstruct/resolve ignore it and sample
// the left volume → frustum-mismatched → black right-eye water.
//
// FIX: capture the RIGHT volume's SRV pair from the right-eye MainCS dispatch,
// then on the right-eye reconstruct (slots 3/4) / resolve (slots 0/1) rebind
// those slots to the cached right-volume SRVs. Uses the same scratch-table
// CopyDescriptorsSimple mechanism as the existing Approach-C alias redirect.
//
// GATING
//   UEVR_SN2_FOG_RIGHT_VOL_REDIRECT=1     enable
//   UEVR_SN2_FOG_RV_MAIN_CRC=0x5af52812   producer/per-eye MainCS that binds right vol (capture source)
//   UEVR_SN2_FOG_RV_RECON_CRC=0xf996b96b  UWEFogReconstructCS
//   UEVR_SN2_FOG_RV_RESOLVE_CRC=0x0930dd4e UWEFogResolveCS
//   UEVR_SN2_FOG_RV_MAIN_SLOTS=11,12      right-vol SRV pair slots in MainCS root-0 table
//   UEVR_SN2_FOG_RV_RECON_SLOTS=3,4       fog-vol slots in UWEFogReconstructCS root-0 table
//   UEVR_SN2_FOG_RV_RESOLVE_SLOTS=0,1     fog-vol slots in UWEFogResolveCS root-0 table
//   UEVR_SN2_FOG_RV_LOG=1                 verbose log (first 64 + every 600th)
//
// The caller (D3D12Hook Dispatch hook) resolves the root-0 table CPU base +
// stride + GPU handle from tls_bindless_heap (exactly like Approach-C), passes
// them in, and restores the original table after the dispatch.

#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <mutex>
#include <string>

#include <d3d12.h>
#include <wrl/client.h>
#include <spdlog/spdlog.h>

namespace sn2_fog_right_vol_redirect {

inline bool env_truthy(const char* name) {
    const char* v = std::getenv(name);
    return v && v[0] && v[0] != '0';
}

inline uint32_t env_crc(const char* name, uint32_t fallback) {
    const char* v = std::getenv(name);
    if (!v || !*v) return fallback;
    char* end = nullptr;
    const auto n = std::strtoul(v, &end, 0);
    return end != v ? static_cast<uint32_t>(n) : fallback;
}

inline std::array<uint32_t, 2> env_slots(const char* name, uint32_t a, uint32_t b) {
    std::array<uint32_t, 2> out{a, b};
    const char* v = std::getenv(name);
    if (!v || !*v) return out;
    char* end = nullptr;
    const auto x = std::strtoul(v, &end, 0);
    if (end != v) {
        out[0] = static_cast<uint32_t>(x);
        while (*end == ',' || *end == ' ') ++end;
        char* end2 = nullptr;
        const auto y = std::strtoul(end, &end2, 0);
        if (end2 != end) out[1] = static_cast<uint32_t>(y);
    }
    return out;
}

inline bool srv_redirect_enabled() { static const bool e = env_truthy("UEVR_SN2_FOG_RIGHT_VOL_REDIRECT"); return e; }
inline bool cb_redirect_enabled()  { static const bool e = env_truthy("UEVR_SN2_FOG_RV_CB_REDIRECT"); return e; }
// THE FIX: the right-eye composite (ExponentialPixelMain) reads a NaN in cb2 (graphics
// root param 5 = shared fog params). Redirect it to the left draw's clean cb2.
inline bool composite_cb_enabled() { static const bool e = env_truthy("UEVR_SN2_FOG_RV_COMPOSITE_CB"); return e; }
// THE REAL FIX (2026-05-24, geometry-proven): the right-eye composite
// (ExponentialPixelMain, PS 0x4E86DC09) samples its full-SBS-width fog textures
// (scattering 141384 / transmittance 141386, both 1264x712 R11G11B10F) at SBS
// screen-UV [0.5,1.0], but UWEFogResolveCS wrote the right eye's fog EYE-LOCAL
// into the LEFT half [0,0.5] (same place the left eye's content lives in its own
// texture). So the composite samples the black right half -> black right eye.
// FIX: just before the right composite draw, copy each fog texture's left-half
// content into its right half (via a scratch, since same-resource copy needs one
// state). Then the engine's own (correct) composite samples [0.5,1.0] and finds
// the right eye's fog. Offset = width/2 is geometrically exact (texel X in the
// right half maps back to eye-local texel X-width/2 = the resolve output for
// right-screen-pixel X). No new shader, no SRV redirect.
inline bool composite_shift_enabled() { static const bool e = env_truthy("UEVR_SN2_FOG_RIGHT_COMPOSITE_SHIFT"); return e; }
// CROSS-EYE copy (2026-05-24, evidence: max_tex=2 within-eye copy leaves the right
// eye's water column white-top/dark-middle — because the right eye's OWN fog content
// is garbage at mid-depth slices, the long-documented producer-intrinsic bug. So
// repositioning the right eye's own fog can't make it teal. Instead, CAPTURE the
// LEFT eye's (correct) fog at the left composite draw and write it into the right
// eye's fog at the right composite draw. The right eye then shows the left eye's
// teal fog with a parallax offset (= the memory's working "teal + logo ghost"
// residual). Default ON when the shift is enabled; set UEVR_SN2_FOG_WITHIN_EYE=1 to
// force the old within-eye copy for comparison.
inline bool cross_eye_enabled() {
    const char* w = std::getenv("UEVR_SN2_FOG_WITHIN_EYE");
    if (w && w[0] && w[0] != '0') return false;   // explicit opt-out
    return true;                                   // default: cross-eye
}
// Destination-left for the shifted fog content, as a fraction of texture width.
// Default 0.5 = put the left-half content at [w/2, w] (full cover; content lands at
// far right). The right eye's resolved fog is LEFT-EYE-FRAMED (no per-eye parallax),
// so its content sits ~0.13*w to the RIGHT of where the right scene drew it -> sharp
// features (menu logo) ghost. Set < 0.5 (measured ~0.379 on the menu) to pull the
// content LEFT and align it with the right eye's parallax-shifted scene. We ALSO do a
// full-cover copy underneath so no black gap appears at the right edge.
inline double composite_shift_dst_frac() {
    const char* v = std::getenv("UEVR_SN2_FOG_SHIFT_DST_FRAC");
    if (!v || !*v) return 0.5;
    char* end = nullptr; const double f = std::strtod(v, &end);
    if (end == v || f <= 0.0 || f >= 1.0) return 0.5;
    return f;
}
// THE TRUE FIX (2026-05-24): depth-free reprojection compute pass. Instead of a
// CopyTextureRegion shift (which leaves a misaligned ghost sliver at the right
// edge because the left-eye-framed fog is narrower than the right eye), a compute
// pass reads the left-half fog and writes the right half at (rx + disparity) with
// an EDGE-CLAMPED source coord — so the far-right region smoothly extends the
// rightmost fog instead of ghosting. disparity = reproject_disp_frac * width
// (~0.127*width = the measured stereo disparity of the dominant content).
inline bool reproject_enabled() { static const bool e = env_truthy("UEVR_SN2_FOG_REPROJECT"); return e; }
// The compute reprojection pass (smooth clamp-extend) is correct but its mid-draw
// heap/PSO swap crashes UEVR's fragile D3D12 init race. Default OFF; the safe
// CopyTextureRegion-only clamp-extend (reproject_enabled) is used instead.
inline bool reproject_compute_enabled() { static const bool e = env_truthy("UEVR_SN2_FOG_REPROJECT_COMPUTE"); return e; }
// Seconds to wait (after the first eligible composite draw) before the compute
// reprojection kicks in — UEVR's D3D12 framework init "race" (~15-20s of
// "Failed to initialize Framework" retries) is fragile, and our heap/PSO swap
// crashes it. During the delay the safe copy-shift runs instead; once UEVR is
// stable + presenting, the reprojection takes over. Default 28s.
inline double reproject_delay_s() {
    const char* v = std::getenv("UEVR_SN2_FOG_REPROJECT_DELAY_S");
    if (!v || !*v) return 28.0;
    char* end = nullptr; const double d = std::strtod(v, &end);
    if (end == v || d < 0.0) return 28.0;
    return d;
}
inline double reproject_disp_frac() {
    const char* v = std::getenv("UEVR_SN2_FOG_REPROJECT_DISP");
    if (!v || !*v) return 0.127;
    char* end = nullptr; const double f = std::strtod(v, &end);
    if (end == v || f < 0.0 || f >= 0.5) return 0.127;
    return f;
}
inline uint32_t composite_ps_crc() { static const uint32_t c = env_crc("UEVR_SN2_FOG_RV_COMPOSITE_PS_CRC", 0x4E86DC09u); return c; }
inline uint32_t composite_cb_param() { static const uint32_t p = env_crc("UEVR_SN2_FOG_RV_COMPOSITE_PARAM", 5u); return p; }
inline bool enabled()        { return srv_redirect_enabled() || cb_redirect_enabled(); }
inline bool log_enabled()    { static const bool e = env_truthy("UEVR_SN2_FOG_RV_LOG"); return e; }
// Reconstruct root CBV params to redirect L->R: cb2=param5 (garbage), cb3=param6 (NaN+handle).
// cb0/cb1 (params 3/4) are correct per-eye and left untouched.
inline const std::array<uint32_t,2>& recon_cb_params() { static const auto s = env_slots("UEVR_SN2_FOG_RV_CB_PARAMS", 5, 6); return s; }
inline uint32_t main_crc()   { static const uint32_t c = env_crc("UEVR_SN2_FOG_RV_MAIN_CRC", 0x5af52812u); return c; }
inline uint32_t recon_crc()  { static const uint32_t c = env_crc("UEVR_SN2_FOG_RV_RECON_CRC", 0xf996b96bu); return c; }
inline uint32_t resolve_crc(){ static const uint32_t c = env_crc("UEVR_SN2_FOG_RV_RESOLVE_CRC", 0x0930dd4eu); return c; }
inline const std::array<uint32_t,2>& main_slots()    { static const auto s = env_slots("UEVR_SN2_FOG_RV_MAIN_SLOTS", 11, 12); return s; }
inline const std::array<uint32_t,2>& recon_slots()   { static const auto s = env_slots("UEVR_SN2_FOG_RV_RECON_SLOTS", 3, 4); return s; }
inline const std::array<uint32_t,2>& resolve_slots() { static const auto s = env_slots("UEVR_SN2_FOG_RV_RESOLVE_SLOTS", 0, 1); return s; }

inline bool is_consumer_crc(uint32_t crc) { return crc == recon_crc() || crc == resolve_crc(); }
inline bool is_relevant_crc(uint32_t crc) { return crc == main_crc() || is_consumer_crc(crc); }

// --- persistent state ---------------------------------------------------------
struct State {
    std::mutex mtx;
    bool heaps_ready = false;
    UINT desc_size = 0;
    // 2-descriptor non-shader-visible cache holding the captured right-vol SRV pair.
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> cache;       // CPU-only
    // shader-visible ring used to build patched 16-slot tables for the dispatch.
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> scratch;     // shader-visible
    std::atomic<uint32_t> ring_idx{0};
    std::atomic<bool> cached{false};                          // right vol captured this run
    std::atomic<uint64_t> capture_seq{0};
    std::atomic<uint64_t> redirect_seq{0};
    // Per-frame occurrence counters for the consumer passes. cmdlist_view_id is
    // unreliable for these compute-only dispatches (no viewport), so we identify
    // the right-eye run by order: left runs first, so the 2nd+ run per frame is
    // the right eye. Reset on each right-vol capture (MainCS view1, which always
    // precedes the reconstruct/resolve in the frame).
    std::atomic<int> recon_count{0};
    std::atomic<int> resolve_count{0};
    // Left reconstruct run's cb2/cb3 root-CBV GPU VAs (captured at occurrence-1),
    // used to redirect the right run's garbage cb2/cb3 (occurrence-2). Reset per frame.
    std::atomic<uint64_t> left_recon_cb2_va{0};
    std::atomic<uint64_t> left_recon_cb3_va{0};
    std::atomic<uint64_t> cb_redirect_seq{0};
    // Left composite (ExponentialPixelMain) cb2 VA, captured at the left draw, used to
    // fix the right draw's NaN cb2. Overwritten each frame at the left draw.
    std::atomic<uint64_t> left_composite_cb2_va{0};
    std::atomic<uint64_t> composite_cb_seq{0};
};

inline State& state() { static State s; return s; }

static constexpr UINT kWindow = 16;            // slots copied per patched table
static constexpr UINT kScratchSlots = 4096;    // ring capacity (256 dispatches * 16)

inline bool ensure_heaps(ID3D12Device* device) {
    auto& s = state();
    if (s.heaps_ready) return true;
    std::scoped_lock lk{s.mtx};
    if (s.heaps_ready) return true;
    if (device == nullptr) return false;
    s.desc_size = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    D3D12_DESCRIPTOR_HEAP_DESC cd{};
    cd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    cd.NumDescriptors = 2;
    cd.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;            // CPU-only (copy src/dst)
    if (FAILED(device->CreateDescriptorHeap(&cd, IID_PPV_ARGS(&s.cache)))) return false;

    D3D12_DESCRIPTOR_HEAP_DESC sd{};
    sd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    sd.NumDescriptors = kScratchSlots;
    sd.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;  // bound for the dispatch
    if (FAILED(device->CreateDescriptorHeap(&sd, IID_PPV_ARGS(&s.scratch)))) return false;

    s.heaps_ready = true;
    if (log_enabled()) {
        SPDLOG_WARN("[SN2-FogRVRedirect] heaps ready desc_size={} (cache=2, scratch={})", s.desc_size, kScratchSlots);
    }
    return true;
}

// Reset the per-frame consumer occurrence counters + cached left CBVs. Called on
// MainCS view1 (the frame boundary that precedes the reconstruct/resolve) when the
// SRV capture path is off (CB-redirect-only mode).
inline void reset_frame_counters_only() {
    auto& s = state();
    s.recon_count.store(0, std::memory_order_relaxed);
    s.resolve_count.store(0, std::memory_order_relaxed);
    s.left_recon_cb2_va.store(0, std::memory_order_relaxed);
    s.left_recon_cb3_va.store(0, std::memory_order_relaxed);
}

// Capture the right-volume SRV pair (game table slots main_slots) into the cache.
// table_cpu_base = CPU handle .ptr of the dispatch's root-0 SRV table.
inline void capture_right_vol(ID3D12Device* device, SIZE_T table_cpu_base, UINT stride) {
    auto& s = state();
    if (!ensure_heaps(device) || table_cpu_base == 0 || stride == 0) return;
    const auto cache_cpu = s.cache->GetCPUDescriptorHandleForHeapStart();
    const auto& sl = main_slots();
    // copy each of the two source slots into cache[0], cache[1]
    for (UINT i = 0; i < 2; ++i) {
        D3D12_CPU_DESCRIPTOR_HANDLE src{}; src.ptr = table_cpu_base + static_cast<SIZE_T>(sl[i]) * stride;
        D3D12_CPU_DESCRIPTOR_HANDLE dst{}; dst.ptr = cache_cpu.ptr + static_cast<SIZE_T>(i) * s.desc_size;
        device->CopyDescriptorsSimple(1, dst, src, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    }
    s.cached.store(true, std::memory_order_release);
    // New frame's consumer ordering starts here (MainCS view1 precedes recon/resolve).
    s.recon_count.store(0, std::memory_order_relaxed);
    s.resolve_count.store(0, std::memory_order_relaxed);
    s.left_recon_cb2_va.store(0, std::memory_order_relaxed);
    s.left_recon_cb3_va.store(0, std::memory_order_relaxed);
    const auto n = s.capture_seq.fetch_add(1, std::memory_order_relaxed) + 1;
    if (log_enabled() && (n <= 64 || (n % 600) == 0)) {
        SPDLOG_WARN("[SN2-FogRVRedirect] captured right vol seq={} from slots {},{}", n, sl[0], sl[1]);
    }
}

// Per-frame occurrence index for a consumer CRC (1=left, 2=right, ...). Left runs
// first; cmdlist_view_id is unreliable for these compute-only dispatches.
inline int consumer_occurrence(uint32_t cs_crc) {
    auto& s = state();
    if (cs_crc == recon_crc())        return s.recon_count.fetch_add(1, std::memory_order_relaxed) + 1;
    if (cs_crc == resolve_crc())      return s.resolve_count.fetch_add(1, std::memory_order_relaxed) + 1;
    return 0;
}
inline bool should_redirect_consumer(uint32_t cs_crc) { return consumer_occurrence(cs_crc) >= 2; }

// Cache the LEFT reconstruct run's cb2/cb3 root-CBV GPU VAs (occurrence-1).
inline void cache_left_recon_cbvs(uint64_t cb2_va, uint64_t cb3_va) {
    auto& s = state();
    s.left_recon_cb2_va.store(cb2_va, std::memory_order_release);
    s.left_recon_cb3_va.store(cb3_va, std::memory_order_release);
}
inline uint64_t left_recon_cb2_va() { return state().left_recon_cb2_va.load(std::memory_order_acquire); }
inline uint64_t left_recon_cb3_va() { return state().left_recon_cb3_va.load(std::memory_order_acquire); }
inline uint64_t next_cb_redirect_seq() { return state().cb_redirect_seq.fetch_add(1, std::memory_order_relaxed) + 1; }

// Composite (ExponentialPixelMain) cb2 redirect helpers.
inline void cache_left_composite_cb2(uint64_t va) { state().left_composite_cb2_va.store(va, std::memory_order_release); }
inline uint64_t left_composite_cb2_va() { return state().left_composite_cb2_va.load(std::memory_order_acquire); }
inline uint64_t next_composite_cb_seq() { return state().composite_cb_seq.fetch_add(1, std::memory_order_relaxed) + 1; }

// Build a patched copy of the dispatch's root-0 table (right-vol SRVs swapped into
// the fog slots) and bind it. Returns true if applied; on success the caller must
// restore the original table (orig_gpu) after Dispatch.
inline bool redirect_consumer(ID3D12Device* device,
                              ID3D12GraphicsCommandList* cmdlist,
                              uint32_t cs_crc,
                              SIZE_T table_cpu_base,
                              UINT64 table_gpu_base,
                              UINT stride) {
    auto& s = state();
    if (!s.cached.load(std::memory_order_acquire)) return false;       // no right vol yet this frame
    if (!ensure_heaps(device) || cmdlist == nullptr || table_cpu_base == 0 || stride == 0) return false;

    const std::array<uint32_t,2>& fog = (cs_crc == recon_crc()) ? recon_slots() : resolve_slots();

    // allocate a kWindow run from the scratch ring
    const UINT base = s.ring_idx.fetch_add(kWindow, std::memory_order_relaxed) % kScratchSlots;
    if (base + kWindow > kScratchSlots) {
        // avoid wrap split; restart at 0 (benign: rare, GPU is at most a few frames behind)
        s.ring_idx.store(kWindow, std::memory_order_relaxed);
        return redirect_consumer(device, cmdlist, cs_crc, table_cpu_base, table_gpu_base, stride);
    }
    const auto scratch_cpu0 = s.scratch->GetCPUDescriptorHandleForHeapStart();
    const auto scratch_gpu0 = s.scratch->GetGPUDescriptorHandleForHeapStart();
    D3D12_CPU_DESCRIPTOR_HANDLE scratch_cpu{}; scratch_cpu.ptr = scratch_cpu0.ptr + static_cast<SIZE_T>(base) * s.desc_size;
    D3D12_GPU_DESCRIPTOR_HANDLE scratch_gpu{}; scratch_gpu.ptr = scratch_gpu0.ptr + static_cast<UINT64>(base) * s.desc_size;

    // copy the whole 16-slot window from the game's table into scratch
    D3D12_CPU_DESCRIPTOR_HANDLE src_base{}; src_base.ptr = table_cpu_base;
    device->CopyDescriptorsSimple(kWindow, scratch_cpu, src_base, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    // patch the two fog slots with the cached right-vol SRV pair
    const auto cache_cpu = s.cache->GetCPUDescriptorHandleForHeapStart();
    for (UINT i = 0; i < 2; ++i) {
        if (fog[i] >= kWindow) continue;
        D3D12_CPU_DESCRIPTOR_HANDLE dst{}; dst.ptr = scratch_cpu.ptr + static_cast<SIZE_T>(fog[i]) * s.desc_size;
        D3D12_CPU_DESCRIPTOR_HANDLE src{}; src.ptr = cache_cpu.ptr + static_cast<SIZE_T>(i) * s.desc_size;
        device->CopyDescriptorsSimple(1, dst, src, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    }

    cmdlist->SetComputeRootDescriptorTable(0, scratch_gpu);
    const auto n = s.redirect_seq.fetch_add(1, std::memory_order_relaxed) + 1;
    if (log_enabled() && (n <= 64 || (n % 600) == 0)) {
        SPDLOG_WARN("[SN2-FogRVRedirect] redirected cs_crc=0x{:08x} seq={} fog_slots={},{} orig_gpu=0x{:x} scratch_gpu=0x{:x}",
                    cs_crc, n, fog[0], fog[1], table_gpu_base, scratch_gpu.ptr);
    }
    return true;
}

}  // namespace sn2_fog_right_vol_redirect
