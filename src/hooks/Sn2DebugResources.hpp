// Sn2DebugResources.hpp
//
// RenderDoc / OpenXR capture-readability debug resources for the Subnautica 2
// right-eye-fog investigation. Owned by agent OWNEDRES. Implementation lives in
// src/mods/vr/D3D12Component.cpp (no new .cpp — this is a header-only interface).
//
// These are the ONLY features in the capture-readability batch that carry a
// device-removal risk, so EVERYTHING here is:
//   * default-OFF (gated by env vars, see below),
//   * backed by UEVR-OWNED COMMITTED resources in their OWN heaps only
//     (never an engine RDG/transient/async resource), and
//   * copied owned->staging only on UEVR's OWN command queue after a fence.
//
// The actual shader-side wiring (the producer store redirect and the t5 SRV
// redirect to the sentinel) is delivered via STAGED dxil_text_patch manifests in
//   E:\Github\Subnautica 2\moddingkit\shader_overrides_staging\
// which must be hand-deployed into the live profile shader_overrides directory
// for a validation run. They are intentionally NOT auto-enabled.
//
// FEATURE MAP
//   #4  Producer fill-coordinate watermark  (UEVR_SN2_FOG_FILL_WATERMARK)
//       Owned RWTexture2D<uint> (~107x30 R32_UINT) + own readback buffer.
//       A staged dxil_text_patch on a producer CS (0x0930dd4e / 0xd1f85c42 /
//       0x3402487c) REDIRECTS an existing store operand to write
//       (eyeID<<24)|(DTid.x & 0xFFFF) into a space99 UAV slot bound to this
//       owned texture, where eyeID is the UEVR eye bucket (1=left, 2=right,
//       0=unknown). Goal: a literal CPU-readable map of which froxel cells
//       [0..53] one eye filled vs [53..107] empty.
//       STATUS: code-complete, default-OFF, PENDING LIVE VALIDATION.
//
//   #11 Sentinel test froxel  (UEVR_SN2_SENTINEL_FROXEL)
//       Owned Texture3D (R11G11B10F ~107x30x48) filled ONCE with an X-gradient
//       on UEVR's own queue while idle. The staged consumer dxil_text_patch
//       manifests redirect the t5 froxel SRV to this sentinel. A/B oracle:
//       smooth gradient across the right half => consumer UV correct (pure
//       producer-fill bug); seam at x~0.5 => consumer-UV bug.
//       STATUS: code-complete, default-OFF, PENDING LIVE VALIDATION.
//
//   #12 eye-id corner watermark  (PURE manifest, no owned resource)
//       Staged dxil_text_patch on the composite PS 0x4e86dc09 that writes a
//       green corner for the left eye / red corner for the right under an
//       SV_Position corner predicate. In-shader color write into the RT the PS
//       already owns => SAFE / COMPLETE.
//
//   #10 OpenXR native-stereo-array SetName  (implemented in D3D12Component.cpp
//       at the native_stereo_array submit). SAFE / COMPLETE.
//
//   #15 Cave-region last-writer recorder  (UEVR_SN2_RDOC_TAGS + UEVR_SN2_CAVE_REGION)
//       PURE OBSERVER — no GPU resources, no engine mutations, zero cost when off.
//       At every draw whose scissor/viewport intersects a configured screen rect
//       (the "cave-opening region"), records the last covering draw per eye into
//       a two-slot in-memory ring.  At Present, flushes the per-eye last-writer
//       record to the UEVR log as:
//         [SN2-CaveWriter] frame=<n> eye=L|R ps=0x<crc> rtv=0x<handle>
//                          scissor=[x0,y0,x1,y1] draw_seq=<n>
//       and emits a PIX SetMarker on the identified draw command list so the
//       event shows up inline in RenderDoc: "CAVE_REGION_WRITER eye=L ps=0x..."
//
//       Eye-half split: SN2 SBS target is 2560 wide.
//         Left eye  occupies [0, 1280).
//         Right eye occupies [1280, 2560).
//       The configured cave-region rect is in per-eye-half coordinates:
//         e.g. UEVR_SN2_CAVE_REGION="400,0,880,360" means the inner rectangle
//         [400,0,880,360] within each eye's 1280×720 half.
//       On the full SBS surface the probe windows become:
//         Left  half: [400,  0, 880,  360]
//         Right half: [1680, 0, 2160, 360]   (= per-eye rect + 1280)
//
//       Default rect (omit env or set to "default"):
//         Per-eye [320, 100, 960, 460] — upper-center region within a 1280×720
//         half, chosen to bracket the cave opening which appears near the top of
//         the underwater vista in the main menu scene (confirmed from captures).
//
//       Env vars:
//         UEVR_SN2_RDOC_TAGS=1          master gate (shared with feature #10..#14)
//         UEVR_SN2_CAVE_REGION=x0,y0,x1,y1  override per-eye rect (pixels)
//                                            "default" or absent => built-in rect
//
//       Safety: on_draw_observed() takes only values the caller already has
//       (eye_bucket, scissor0, last_rtv0_handle, ps_crc, draw_seq) — no extra
//       D3D12 queries. The PIX SetMarker is the one mutation: it calls
//       ID3D12GraphicsCommandList::SetMarker on the ENGINE command list but that
//       is the same call the existing Sn2RenderDocTags markers already make and
//       is accepted as safe (it is capture-only metadata, not geometry).
//
// The functions below are called by the D3D12 dispatch/draw hooks
// (D3D12Hook.cpp already #includes this header). They are no-ops unless the
// corresponding env gate is set, and they never touch an engine resource.

#pragma once

#include <cstdint>

struct ID3D12Device;
struct ID3D12GraphicsCommandList;

// Minimal D3D12_RECT equivalent so the header compiles without d3d12.h.
// The caller passes s.scissor0 (already a D3D12_RECT) cast to this struct —
// the layout is identical (both are {LONG left,top,right,bottom}).
struct Sn2Rect { long left, top, right, bottom; };

namespace sn2_debug {
    // env UEVR_SN2_FOG_FILL_WATERMARK (default off) — feature #4.
    bool fog_fill_watermark_enabled();

    // env UEVR_SN2_SENTINEL_FROXEL (default off) — feature #11.
    bool sentinel_froxel_enabled();

    // -------------------------------------------------------------------------
    // Feature #15: cave-region last-writer recorder.
    //
    // cave_writer_enabled() — returns true when UEVR_SN2_RDOC_TAGS is set.
    //   Shared gate with the existing RenderDoc-tags features so a single env
    //   var enables the whole capture-readability bundle.
    // -------------------------------------------------------------------------
    bool cave_writer_enabled();

    // -------------------------------------------------------------------------
    // on_draw_observed()
    //
    // Call from BOTH the DrawInstanced and DrawIndexedInstanced hooks, at the
    // same site where on_consumer_draw() is already called.  The function is
    // a pure CPU-side observer: it only reads the arguments and updates an
    // in-memory per-eye last-writer record.
    //
    // Parameters (all available in both draw hooks before the guard block):
    //   cl         — the engine command list (used only for the optional PIX
    //                SetMarker; may be nullptr — function is a no-op then).
    //   eye_bucket — UEVR eye convention: 1=LEFT, 2=RIGHT, 0=unknown.
    //   has_scissor— whether s.has_scissor is true (scissor rect is valid).
    //   scissor    — s.scissor0 cast to Sn2Rect (left/top/right/bottom pixels
    //                on the full 2560-wide SBS surface).
    //   rtv_handle — s.last_rtv0_handle (CPU descriptor handle of RTV slot 0).
    //   ps_crc     — forensics_ps_crc (0 for non-graphics / pre-PSO draws).
    //   draw_seq   — monotonic draw counter for this frame (pass
    //                s_cave_draw_seq.fetch_add(1) defined locally in the
    //                draw hook, or a shared frame-draw counter — any value
    //                that distinguishes draws within the same frame).
    //
    // The function:
    //   1. Reads (once, cached) the configured cave rect from env.
    //   2. Derives the SBS-surface left/right probe windows from the per-eye rect.
    //   3. Checks whether the scissor intersects the probe window for the eye.
    //   4. If it does, atomically overwrites the last-writer slot for that eye.
    //   5. Optionally emits a PIX SetMarker "CAVE_REGION_WRITER eye=L|R ps=0x..."
    //      on `cl` (only when sn2_rdoc_tags::markers_enabled() is also true —
    //      same condition as the existing rdoc_tags marker calls in the hook).
    //
    // Thread-safety: the per-eye slots use a mutex (same pattern as State in
    // feature #4/#11). Draw hooks may fire on parallel render threads.
    // -------------------------------------------------------------------------
    void on_draw_observed(
        ID3D12GraphicsCommandList* cl,
        int            eye_bucket,
        bool           has_scissor,
        Sn2Rect        scissor,
        uint64_t       rtv_handle,
        uint32_t       ps_crc,
        uint64_t       draw_seq);

    // -------------------------------------------------------------------------
    // flush_cave_writers()
    //
    // Call from BOTH D3D12Hook::present() and D3D12Hook::present1(), gated by
    // cave_writer_enabled(), at the same site as the other sn2_*::on_present()
    // calls.  Reads the last per-eye covering-draw record and writes one
    // [SN2-CaveWriter] line per eye to the UEVR spdlog (WARN level so it
    // appears in the default log without -v).  The slot is then cleared so the
    // next frame starts fresh.
    //
    // Output format (one line per eye that had at least one covering draw):
    //   [SN2-CaveWriter] frame=<n> eye=L ps=0x<8hex> rtv=0x<16hex>
    //                    scissor=[<x0>,<y0>,<x1>,<y1>] draw_seq=<n>
    //   [SN2-CaveWriter] frame=<n> eye=R ps=0x<8hex> rtv=0x<16hex>
    //                    scissor=[<x0>,<y0>,<x1>,<y1>] draw_seq=<n>
    //   [SN2-CaveWriter] frame=<n> NO_WRITER eye=L  (if no covering draw this frame)
    //   [SN2-CaveWriter] frame=<n> NO_WRITER eye=R
    // -------------------------------------------------------------------------
    void flush_cave_writers();

    // Idempotent. Creates the UEVR-owned committed debug resources (watermark
    // RWTexture2D + readback, sentinel Texture3D) once, only for whichever
    // feature gates are enabled. Safe to call every frame; cheap after the
    // first successful init. No-op if dev == nullptr or no gate is enabled.
    void ensure_resources(ID3D12Device* dev);

    // Called from the compute-dispatch hook. When the watermark gate is enabled
    // and cs_crc is a known producer (0x0930dd4e / 0xd1f85c42 / 0x3402487c),
    // ensures the owned watermark resource exists and schedules an owned->own
    // readback (on UEVR's OWN queue, after a fence). Never mutates the engine
    // command list state beyond what the caller already owns; if it cannot do so
    // safely it does nothing.
    //
    // EYE CONVENTION (UEVR eye-bucket, set by the caller in D3D12Hook.cpp):
    //   1 = LEFT eye, 2 = RIGHT eye, 0 = unknown/unset.
    // The #4 watermark stamp encodes this in the high byte of the packed value
    // ((eyeID & 0xFF) << 24), so a CPU readback of 0 means "unset", 1 = left,
    // 2 = right — left and right are always distinguishable from each other and
    // from the empty (0) cells.
    void on_producer_dispatch(ID3D12GraphicsCommandList* cl, uint32_t cs_crc, int eye);

    // Called from the draw hook. Lightweight marker for sentinel-froxel
    // consumers (the actual t5 SRV redirect lives in the staged manifest); used
    // only for logging/sequencing so a capture can be correlated. Never mutates
    // engine resources. Eye convention: 1 = LEFT, 2 = RIGHT, 0 = unknown.
    void on_consumer_draw(ID3D12GraphicsCommandList* cl, uint32_t ps_crc, int eye);
}
