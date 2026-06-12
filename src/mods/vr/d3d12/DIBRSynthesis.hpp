#pragma once

#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <d3d12.h>
#include <dxgi.h>
#include <wrl.h>

namespace vrmod {
// 1:1 mirror of the `StereoParams` cbuffer shared by shaders/dibr_inverse.hlsl,
// dibr_yoro.hlsl, dibr_raymarch.hlsl and the scatter/prep kernels (4-byte scalars,
// field order is load-bearing). Every field is 4 bytes, so HLSL cbuffer packing (which only
// inserts padding when a field would straddle a 16-byte register boundary)
// degenerates to a flat, tightly packed layout identical to this struct; it is
// memcpy'd into the upload constant buffer verbatim.
//
// Defaults are ported from vrmod-private's `impl Default for StereoParams`
// (the tuned values shipped with the original implementation; every optional
// feature defaults to off).
//
// Field of note: `mode_param0` carries the YORO reference eye
// (0.0 = left reference / synthesize right, 1.0 = right reference).
struct DIBRStereoParams {
    float divergence{30.0f};
    float convergence{0.5f};
    uint32_t source_width{1920};
    uint32_t source_height{1080};
    float edge_compression{0.0f};
    float reverse_depth{0.0f};
    float depth_floor{0.0f};
    float depth_ceiling{1.0f};
    float depth_gain{1.0f};
    float depth_curve{1.0f};
    float depth_range_boost_strength{0.0f};
    float depth_range_boost_center{0.5f};
    float depth_range_boost_width{0.5f};
    float depth_range_boost_scale{2.5f};
    float perspective_shift{0.0f};
    float zpd_balance{0.0f};
    float popout_limit{1.0f};
    float edge_fill{1.0f};
    float edge_fill_mode{2.0f};
    float range_smoothing{0.35f};
    float foreground_protect{0.5f};
    float raymarch_steps{32.0f};
    float mode_param0{0.0f};
    float near_field_strength{0.0f};
    float near_field_start{0.0f};
    float near_field_end{0.15f};
    float near_field_target{0.5f};
    float auto_depth_strength{0.0f};
    float auto_depth_radius{0.05f};
    float auto_depth_min_range{0.05f};
    float auto_depth_contrast{1.0f};
    float disocclusion_strength{0.6f};
    float disocclusion_threshold{0.06f};
    float disocclusion_feather{0.08f};
    float disocclusion_depth_weight{1.0f};
    float edge_guard_strength{0.0f};
    float edge_guard_width{0.08f};
    float edge_guard_shape{1.0f};
    float edge_guard_near_depth{0.25f};
    float depth_uv_scale_x{1.0f};
    float depth_uv_scale_y{1.0f};
    float depth_uv_offset_x{0.0f};
    float depth_uv_offset_y{0.0f};
    float depth_uv_anchor{0.0f};
    float depth_uv_flip_x{0.0f};
    float depth_uv_flip_y{0.0f};
    float depth_value_flip{0.0f};
    float depth_linearize_strength{0.0f};
    float depth_linearize_near{0.1f};
    float depth_linearize_far{1000.0f};
    float depth_linearize_mode{0.0f};
    float convergence_boundary_strength{0.0f};
    float convergence_boundary_threshold{0.08f};
    float convergence_boundary_feather{0.08f};
    float convergence_boundary_scale{0.5f};
    float depth_artifact_guard_strength{0.0f};
    float depth_artifact_guard_threshold{0.18f};
    float depth_artifact_guard_feather{0.12f};
    float depth_artifact_guard_scale{0.25f};
    float depth_edge_mask_strength{0.0f};
    float depth_edge_mask_radius{1.0f};
    float depth_edge_mask_threshold{0.04f};
    float depth_edge_mask_feather{0.08f};
    float depth_expand_strength{0.0f};
    float depth_expand_radius{1.0f};
    float depth_expand_edge_threshold{0.04f};
    float depth_expand_near_bias{1.0f};
    float letterbox_mask_strength{0.0f};
    float letterbox_mask_x{0.0f};
    float letterbox_mask_y{0.0f};
    float letterbox_mask_feather{0.02f};
    float letterbox_auto_strength{0.0f};
    float letterbox_auto_mode{0.0f};
    float letterbox_auto_max_x{0.25f};
    float letterbox_auto_max_y{0.25f};
    float letterbox_auto_threshold{0.06f};
    float letterbox_auto_feather{0.02f};
    float depth_reconstruct_strength{0.0f};
    float depth_reconstruct_radius{1.0f};
    float depth_reconstruct_edge_threshold{0.08f};
    float depth_reconstruct_near_bias{0.75f};
    float region_mask_strength{0.0f};
    float region_mask_left{0.0f};
    float region_mask_top{0.55f};
    float region_mask_right{1.0f};
    float region_mask_bottom{1.0f};
    float region_mask_target_depth{0.5f};
    float region_mask_feather{0.04f};
    float region_mask_depth_gate{1.0f};
    float weapon_mask_strength{0.0f};
    float weapon_mask_left{0.0f};
    float weapon_mask_top{0.55f};
    float weapon_mask_right{1.0f};
    float weapon_mask_bottom{1.0f};
    float weapon_mask_target_depth{0.5f};
    float weapon_mask_feather{0.04f};
    float weapon_mask_depth_gate{1.0f};
    float weapon_auto_mask_strength{0.0f};
    float weapon_auto_mask_y_start{0.5f};
    float weapon_auto_mask_near{0.08f};
    float weapon_auto_mask_far{0.28f};
    float weapon_auto_mask_target_depth{0.5f};
    float weapon_auto_mask_feather{0.08f};
    float weapon_boundary_strength{0.0f};
    float weapon_boundary_y_start{0.85f};
    float weapon_boundary_near{0.08f};
    float weapon_boundary_far{0.32f};
    float weapon_boundary_scale{0.5f};
    float weapon_boundary_feather{0.08f};
    float focus_reduction_strength{0.0f};
    float focus_reduction_mode{2.0f};
    float focus_reduction_world_scale{0.5f};
    float focus_reduction_weapon_scale{0.25f};
    float focus_reduction_eye_selection{0.0f};
    float output_matte_strength{0.0f};
    float output_matte_left{0.0f};
    float output_matte_top{0.0f};
    float output_matte_right{1.0f};
    float output_matte_bottom{1.0f};
    float output_matte_feather{0.04f};
    float output_matte_mode{0.0f};
    float output_matte_gray{0.0f};
    float cursor_overlay_strength{0.0f};
    float cursor_overlay_type{0.0f};
    float cursor_overlay_x{0.5f};
    float cursor_overlay_y{0.5f};
    float cursor_overlay_size{0.02f};
    float cursor_overlay_thickness{0.002f};
    float cursor_overlay_feather{0.0015f};
    float cursor_overlay_depth{0.5f};
    float cursor_overlay_color_mode{0.0f};
    float cursor_overlay_lock_to_center{1.0f};
    float depth_sample_mode{0.0f};
    float depth_dither_strength{0.0f};
    float depth_dither_bits{6.0f};
    float raymarch_foveation_strength{0.0f};
    float raymarch_foveation_radius{0.35f};
    float raymarch_foveation_min_steps{12.0f};
    float raymarch_foveation_curve{1.5f};
    float debug_view_mode{0.0f};
    float debug_view_scale{1.0f};
    float debug_view_near{0.0f};
    float debug_view_far{1.0f};
    float ui_alpha_mask_strength{0.0f};
    float ui_alpha_mask_threshold{0.5f};
    float ui_alpha_mask_feather{0.1f};
    float ui_alpha_mask_target_depth{0.5f};
    float ui_auto_mask_strength{0.0f};
    float ui_auto_mask_mode{0.0f};
    float ui_auto_mask_threshold{0.85f};
    float ui_auto_mask_feather{0.08f};
    float ui_auto_mask_target_depth{0.5f};
    float shape_mask_strength{0.0f};
    float shape_mask_mode{0.0f};
    float shape_mask_left{0.0f};
    float shape_mask_top{0.0f};
    float shape_mask_right{1.0f};
    float shape_mask_bottom{1.0f};
    float shape_mask_target_depth{0.5f};
    float shape_mask_feather{0.04f};
    float shape_mask_depth_gate{1.0f};
    float shape_mask_invert{0.0f};
    float shape_mask_edge_width{0.04f};
    float comfort_nose_strength{0.0f};
    float comfort_nose_width{0.16f};
    float comfort_nose_height{0.46f};
    float comfort_nose_y{0.58f};
    float comfort_nose_feather{0.08f};
    float comfort_nose_curve{1.35f};
    float comfort_nose_mode{0.0f};
    float comfort_nose_color_r{0.74f};
    float comfort_nose_color_g{0.49f};
    float comfort_nose_color_b{0.36f};
    float image_filter_sharpen_strength{0.0f};
    float image_filter_radius{1.0f};
    float image_filter_sharpen_limit{0.25f};
    float image_filter_aa_strength{0.0f};
    float image_filter_aa_threshold{0.08f};
    float image_filter_aa_feather{0.12f};
    float image_filter_alpha_passthrough{1.0f};
    float image_filter_deband_strength{0.0f};
    float image_filter_deband_radius{2.0f};
    float image_filter_deband_threshold{0.015f};
    float image_filter_deband_grain{0.0f};
    float output_eye_swap{0.0f};
    float output_saturation{1.0f};
    float output_vignette_strength{0.0f};
    float output_vignette_radius{0.75f};
    float output_vignette_feather{0.25f};
    float output_hmd_vignette{0.0f};
    float output_geometry_barrel{0.0f};
    float output_geometry_radial_k2{0.0f};
    float output_geometry_radial_k3{0.0f};
    float output_geometry_poly_strength{0.0f};
    float output_geometry_poly_k1_r{0.22f};
    float output_geometry_poly_k1_g{0.22f};
    float output_geometry_poly_k1_b{0.22f};
    float output_geometry_poly_k2_r{0.24f};
    float output_geometry_poly_k2_g{0.24f};
    float output_geometry_poly_k2_b{0.24f};
    float output_geometry_zoom{1.0f};
    float output_geometry_fov{0.0f};
    float output_geometry_scale_x{1.0f};
    float output_geometry_scale_y{1.0f};
    float output_geometry_offset_x{0.0f};
    float output_geometry_offset_y{0.0f};
    float output_geometry_left_offset_x{0.0f};
    float output_geometry_left_offset_y{0.0f};
    float output_geometry_right_offset_x{0.0f};
    float output_geometry_right_offset_y{0.0f};
    float output_geometry_left_rotation_deg{0.0f};
    float output_geometry_right_rotation_deg{0.0f};
    float output_geometry_keystone_tilt{0.0f};
    float output_geometry_tie_right_alignment{1.0f};
    float output_geometry_ipd_offset{0.0f};
    float output_geometry_lens_dependent_ipd{0.0f};
    float output_geometry_axis_swap{0.0f};
    float output_headset_profile{0.0f};
    float output_composition_mode{0.0f};
    float output_layout_mode{0.0f};
    float output_anaglyph_saturation{1.0f};
    float output_anaglyph_contrast{1.0f};
    float output_anaglyph_mode{0.0f};
    float output_anaglyph_left_contrast{1.0f};
    float output_anaglyph_right_contrast{1.0f};
    float filter_emulator_focus{0.5f};
    float filter_emulator_max_depth{1.0f};
    float filter_emulator_near_reduction{1.0f};
    float filter_emulator_auto_focus{0.0f};
    float filter_emulator_reduce_r{0.5f};
    float filter_emulator_reduce_g{0.5f};
    float filter_emulator_reduce_b{0.5f};
    float output_interlace_swap{0.0f};
    float output_interlace_blend{0.5f};
    float output_interlace_scale_mode{0.0f};
    float output_interlace_sample_offset{0.5f};
    float output_distortion_grid{0.0f};
    float output_frame_marker_mode{0.0f};
    float output_frame_marker_thickness{0.001f};
    float output_alignment_marker_mode{0.0f};
    float output_alignment_marker_thickness{0.002f};
    float stereo_axis_mode{0.0f};
    uint32_t frame_index{0};

    // --- True-matrix reprojection (appended; offsets above are load-bearing) ---
    // When > 0.5 the synthesized-eye search maps source->target through the
    // exact clip-to-clip matrices below (built from the runtime's real
    // per-eye projections + IPD) instead of the screen-space divergence
    // model. The matrices are glm::mat4 memcpy'd column-major; the HLSL side
    // declares default (column_major) float4x4, so mul(M, v) computes M*v.
    // 243 scalars end at byte 972; the flag brings it to 976 (16-aligned), so
    // the float4x4 fields start exactly where HLSL packs them - no padding.
    float reproj_enabled{0.0f};
    float reproj_source_to_left[16]{};
    float reproj_source_to_right[16]{};
    // When > 0.5 the YORO kernel composes the synthesized eye from the
    // scatter pipeline's filled buffer (u2) instead of running its gather
    // search - see the dibr_scatter_* kernels.
    float scatter_compose{0.0f};
    // Horizontal overscan factor of the rendered source view (1 = none). The
    // engine renders the lone view this much wider; the compose remaps the
    // reference eye back to its true FOV (pure NDC x scale - the projection
    // widening scales M[0][0] and M[2][0] together, so ndc divides evenly).
    float overscan_x{1.0f};
    // R3: temporal hole fill. When > 0.5 the scatter fill kernel first tries
    // last frame's synthesized eye (reprojected through the camera-delta
    // matrix below and validated against the stored depth key) before the
    // scanline fill. Set to 0 by DIBRSynthesis while no valid history exists.
    float temporal_enabled{0.0f};
    // EMA weight for validated history in the fill kernel (also keeps the
    // matrix below 16-byte aligned). 0.85 = ~7x boil damping; the caller
    // scales it down with per-frame pose delta so fast motion favors fresh
    // fill over latched history.
    float temporal_blend{0.85f};
    // Current target-eye clip -> previous frame's target-eye clip.
    float reproj_target_to_prev[16]{};
    // Output (submit) eye size - differs from source_width when the
    // overscan-grown render target makes the source wider than the true-FOV
    // output. synthesize() defaults these to the source dims when zero.
    uint32_t out_width{0};
    uint32_t out_height{0};
    // Synthesis (scatter-buffer) resolution. Defaults to the out dims, where
    // synth_scale 1.0 reproduces the historical full-res path bit-for-bit. The
    // half-res toggle (UEVR_DIBR_SYNTH_SCALE) drops these below the out dims:
    // the scatter clear/depth/color/fill passes work in this space and the
    // compose pass bilinearly upscales the scatter colour when reading it.
    // These two uints fill the .z/.w padding of out_width/out_height's cbuffer
    // row, so the struct stays 16-byte aligned with no new register.
    uint32_t synth_width{0};
    uint32_t synth_height{0};
    // CPU-resolved constants, stamped by synthesize() right before upload:
    // dispatch-uniform values hoisted out of the per-pixel shader code.
    // pre_effective_convergence = lerp(convergence, 0.5, saturate(zpd_balance));
    // pre_inv_src_* = 1 / source dims; pre_edge_comp_inv = the 1/atan / 1/tan
    // normalization of the edge-compression projection (1 when off).
    float pre_effective_convergence{0.5f};
    float pre_inv_src_width{1.0f / 1920.0f};
    float pre_inv_src_height{1.0f / 1080.0f};
    float pre_edge_comp_inv{1.0f};
};

static_assert(sizeof(DIBRStereoParams) == 256 * 4 + 3 * 64, "DIBRStereoParams must mirror the HLSL StereoParams cbuffer (243 scalars + 5 flags/pads + three float4x4 + out/synth dims + 4 CPU-resolved constants)");
static_assert(offsetof(DIBRStereoParams, reproj_target_to_prev) % 16 == 0, "temporal reprojection matrix must be 16-byte aligned");
static_assert(offsetof(DIBRStereoParams, reproj_source_to_left) % 16 == 0, "reprojection matrices must be 16-byte aligned to match HLSL cbuffer packing");

// DIBR stereo synthesis: a single compute dispatch that turns one rendered
// color frame + scene depth into a packed stereo pair. Ported from
// vrmod-private's vrmod-stereo crate. Three interchangeable kernels share one
// root signature and parameter buffer:
//   InverseWarp - symmetric inverse warp, both eyes synthesized.
//   Yoro        - "You Only Render Once": reference eye is an untouched
//                 passthrough, the other eye carries the full disparity.
//   Raymarch    - steep-parallax search over the depth field before sampling
//                 color; best edge behavior, highest cost (foveation-capable).
//
// Usage: call ensure() once a device is available (compile-once; failure is
// sticky and logged), then synthesize() each frame. synthesize() only records
// into the provided command list - the caller owns submission and lifetime of
// the list, and must keep the returned texture alive until the GPU work
// completes (it is owned by this class and reused every frame).
class DIBRSynthesis {
public:
    enum class Mode : int {
        InverseWarp = 0,
        Yoro = 1,
        Raymarch = 2,
        // R2 redesign: forward scatter (occlusion correct by construction)
        // + explicit hole fill, composed by the yoro kernel. Requires the
        // true-matrix reprojection inputs (params.reproj_*).
        YoroScatter = 3,
    };

    DIBRSynthesis() = default;
    ~DIBRSynthesis();
    DIBRSynthesis(const DIBRSynthesis&) = delete;
    DIBRSynthesis& operator=(const DIBRSynthesis&) = delete;

    // Non-blocking init: the first call kicks off a worker thread that
    // compiles the three kernels (DXC cs_6_0 preferred, FXC cs_5_0 fallback;
    // compiled bytecode is disk-cached so this is only slow once per source
    // revision) and builds all device objects. Returns true once everything
    // is ready; until then (and permanently after a failure, which logs once)
    // it returns false and synthesize() is a no-op.
    bool ensure(ID3D12Device* device);
    bool ready() const { return m_state.load(std::memory_order_acquire) == State::Ready; }
    bool failed() const { return m_state.load(std::memory_order_acquire) == State::Failed; }

    // Human-readable pipeline state for status UI.
    const char* state_name() const {
        switch (m_state.load(std::memory_order_acquire)) {
        case State::NotStarted:
            return "idle";
        case State::Building:
            return "compiling kernels...";
        case State::Ready:
            return "ready";
        case State::Failed:
            return "FAILED (see log)";
        }
        return "unknown";
    }

    // Records barriers + the DIBR dispatch into cmd_list and returns the
    // packed output texture (layout per params.output_layout_mode: 0 = SBS
    // double-wide), or nullptr on failure. color/depth are transitioned to
    // shader-readable states and restored to the given states afterwards.
    // params is taken by value: source_width/source_height are stamped from
    // the color texture desc and frame_index from the internal counter.
    ID3D12Resource* synthesize(
        ID3D12Device* device,
        ID3D12GraphicsCommandList* cmd_list,
        Mode mode,
        ID3D12Resource* color, D3D12_RESOURCE_STATES color_state,
        ID3D12Resource* depth, D3D12_RESOURCE_STATES depth_state,
        DIBRStereoParams params);

    // AFW (alternate frame warping): when enabled, each synthesize() call
    // stashes the raw source render (color + device-depth keys) into a
    // dedicated history pair AFTER the fill pass consumed the previous stash,
    // and the fill's temporal history binds to that pair instead of the
    // scatter ping-pong. Under per-frame eye alternation the stash is the
    // REAL render of the eye being synthesized next frame.
    void set_alternate_history(bool enabled) { m_afw_mode = enabled; }

    // Per-pass GPU timing (env UEVR_ENABLE_D3D12_GPU_TIMESTAMPS): the caller
    // provides the queue the recorded command list executes on (needed for
    // GetTimestampFrequency); synthesize() then brackets each DIBR pass with
    // timestamp queries, reads them back kRing frames later and logs per-pass
    // averages every few seconds ([DIBR][gpu]). No-op when the env is unset.
    void set_gpu_timing_queue(ID3D12CommandQueue* queue) { m_ts_queue = queue; }

    // SceneVelocity snapshot (dibr_depth_tracker::get_velocity_snapshot),
    // refreshed by the caller each frame; bound as t4 in NON_PIXEL_SHADER_
    // RESOURCE state (the tracker leaves it there). Null descriptor when
    // absent. Consumed by the velocity debug view; temporal gates next.
    void set_velocity_texture(ID3D12Resource* texture) { m_velocity_tex = texture; }

    // Output texture format. Default R8G8B8A8_UNORM (guaranteed typed UAV
    // store support). Changing it forces the output texture to be recreated;
    // the caller is responsible for picking a UAV-capable format.
    void set_output_format(DXGI_FORMAT format);
    DXGI_FORMAT output_format() const { return m_output_format; }

    ID3D12Resource* output() const { return m_output.Get(); }

    // Releases every device object (device loss / teardown). The next
    // ensure() call rebuilds from scratch (the disk bytecode cache makes that
    // cheap). NON-BLOCKING even mid-compile: an in-flight build is abandoned
    // via a generation bump and its objects are discarded when the worker
    // finishes; only the destructor ever joins the worker.
    void reset();

private:
    enum class State : int {
        NotStarted = 0,
        Building = 1,
        Ready = 2,
        Failed = 3,
    };

    // Everything the build worker produces, so an abandoned build can be
    // discarded wholesale without touching the live members.
    struct DeviceObjects {
        Microsoft::WRL::ComPtr<ID3D12RootSignature> root_sig{};
        Microsoft::WRL::ComPtr<ID3D12PipelineState> pso_inverse{};
        Microsoft::WRL::ComPtr<ID3D12PipelineState> pso_inverse_lean{};
        Microsoft::WRL::ComPtr<ID3D12PipelineState> pso_yoro{};
        Microsoft::WRL::ComPtr<ID3D12PipelineState> pso_yoro_lean{};
        Microsoft::WRL::ComPtr<ID3D12PipelineState> pso_yoro_scatter{};
        Microsoft::WRL::ComPtr<ID3D12PipelineState> pso_yoro_scatter_lean{};
        Microsoft::WRL::ComPtr<ID3D12PipelineState> pso_raymarch{};
        Microsoft::WRL::ComPtr<ID3D12PipelineState> pso_raymarch_lean{};
        Microsoft::WRL::ComPtr<ID3D12PipelineState> pso_scatter_depth{};
        Microsoft::WRL::ComPtr<ID3D12PipelineState> pso_scatter_color{};
        Microsoft::WRL::ComPtr<ID3D12PipelineState> pso_scatter_fill{};
        Microsoft::WRL::ComPtr<ID3D12PipelineState> pso_afw_stash{};
        // Depth conditioning prepasses (dibr_depth_prep.hlsl, one PREP_MODE each).
        Microsoft::WRL::ComPtr<ID3D12PipelineState> pso_prep_inverse{};
        Microsoft::WRL::ComPtr<ID3D12PipelineState> pso_prep_raymarch{};
        Microsoft::WRL::ComPtr<ID3D12PipelineState> pso_prep_yoro{};
        Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> heap{};
        // Non-shader-visible UAV descriptors for ClearUnorderedAccessView*
        // (the API needs a CPU handle from a non-shader-visible heap).
        Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> clear_heap{};
        uint32_t descriptor_stride{0};
        Microsoft::WRL::ComPtr<ID3D12Resource> cbuffer{};
        uint8_t* cbuffer_ptr{nullptr};
    };

    void build_async(Microsoft::WRL::ComPtr<ID3D12Device> device, uint64_t generation);
    static bool create_root_signature(ID3D12Device* device, DeviceObjects& objs);
    static bool create_psos(ID3D12Device* device, DeviceObjects& objs);
    static bool create_rings(ID3D12Device* device, DeviceObjects& objs);
    bool ensure_output(ID3D12Device* device, uint32_t width, uint32_t height);
    bool ensure_scatter(ID3D12Device* device, uint32_t width, uint32_t height);
    bool ensure_afw_history(ID3D12Device* device, uint32_t width, uint32_t height);
    bool ensure_prep(ID3D12Device* device, uint32_t width, uint32_t height);
    void join_worker();

    // True when every parameter gated behind DIBR_LEAN is at its pass-through
    // default, so the lean PSO (optional output features compiled out) is
    // exactly equivalent to the full kernel.
    static bool params_allow_lean(const DIBRStereoParams& p);

    static DXGI_FORMAT color_srv_format(DXGI_FORMAT f);
    static DXGI_FORMAT depth_srv_format(DXGI_FORMAT f);
    static uint32_t output_layout_mode(const DIBRStereoParams& p);
    static uint32_t frame_pack_gap_height(const DIBRStereoParams& p);
    static void packed_output_dimensions(const DIBRStereoParams& p, uint32_t& w, uint32_t& h);

    // In-flight protection without fences: per-call constant-buffer slots and
    // descriptor triplets rotate through a ring sized well past UEVR's frame
    // queue depth.
    static constexpr uint32_t kRing = 8;
    // t0 color, t1 depth, t2 prepared depth (SRV), t3 history color (SRV),
    // u0 output, u1/u2 scatter key+color, u3/u4 history color+key, u5 prep
    // UAV, t4 velocity snapshot (SRV, table offset 10 via its own range).
    static constexpr uint32_t kDescriptorsPerSlot = 11;
    // Derived, not hardcoded: a fixed value overran the upload buffer when the
    // struct grew (the reprojection matrices pushed it past the old 1024).
    static constexpr uint32_t kCbSlotSize = (sizeof(DIBRStereoParams) + 255u) & ~255u;

    std::atomic<State> m_state{State::NotStarted};
    std::thread m_worker{};
    std::atomic<bool> m_worker_finished{false};
    std::atomic<uint64_t> m_generation{0};
    // Serializes the worker's commit against reset()'s abandon.
    std::mutex m_commit_mtx{};

    DeviceObjects m_objs{};

    Microsoft::WRL::ComPtr<ID3D12Resource> m_output{};
    uint32_t m_output_width{0};
    uint32_t m_output_height{0};
    DXGI_FORMAT m_output_format{DXGI_FORMAT_R8G8B8A8_UNORM};

    // Scatter pipeline intermediates (kept in UNORDERED_ACCESS; eye-sized).
    // Ping-ponged: the previous frame's FILLED pair serves as the temporal
    // hole-fill history for the current frame.
    Microsoft::WRL::ComPtr<ID3D12Resource> m_scatter_key[2]{};
    Microsoft::WRL::ComPtr<ID3D12Resource> m_scatter_color[2]{};
    uint32_t m_scatter_width{0};
    uint32_t m_scatter_height{0};
    uint32_t m_scatter_index{0};
    bool m_scatter_history_valid{false};

    // AFW real-render history (see set_alternate_history). Single pair: the
    // fill reads it before the stash pass overwrites it (UAV barriers order
    // the read-then-write within the frame's command list).
    Microsoft::WRL::ComPtr<ID3D12Resource> m_afw_history_key{};
    Microsoft::WRL::ComPtr<ID3D12Resource> m_afw_history_color{};
    bool m_afw_history_valid{false};
    bool m_afw_mode{false};

    // Conditioned-depth prepass target (source-sized; written by the
    // depth-prep PSO each frame, then read by the gather kernels as t2).
    // m_prep_is_srv tracks which state the texture was left in.
    Microsoft::WRL::ComPtr<ID3D12Resource> m_prep{};
    uint32_t m_prep_width{0};
    uint32_t m_prep_height{0};
    bool m_prep_is_srv{false};

    // --- Per-pass GPU timing (see set_gpu_timing_queue) ---
    enum class TsPass : uint8_t { Prep = 0, Clear, ScatterDepth, ScatterColor, Fill, Compose, Stash, Count };
    static constexpr uint32_t kTsMaxPasses = 8; // >= (uint)TsPass::Count
    static constexpr uint32_t kTsQueriesPerSlot = kTsMaxPasses * 2;
    bool ensure_gpu_timing(ID3D12Device* device);
    void drain_gpu_timing_slot(uint32_t slot);
    void log_gpu_timing();

    // Weak SceneVelocity snapshot pointer (see set_velocity_texture).
    ID3D12Resource* m_velocity_tex{nullptr};

    // --- Velocity behavioral calibration (discovery Layer 3) ---
    // Reads back a few velocity texels per frame, decodes them under BOTH
    // encode flavors (linear = UE4.26/4.27, gamma = UE5.5+; the transition
    // point is unbracketed and licensee forks deviate), scores each against
    // the displacement the AFW frame-pair matrix predicts from the velocity's
    // own packed depth, and picks the flavor empirically. Engine-version
    // knowledge only orders the default, never skips the trial.
    void record_velocity_calibration(ID3D12GraphicsCommandList* cmd_list, uint32_t slot, const DIBRStereoParams& params);
    void drain_velocity_calibration(uint32_t slot);
    void velocity_calibration_verdict();

    struct VelCalibSlot {
        bool valid{false};
        float matrix[16]{};   // reproj for the frame pair the sampled velocity spans
        uint32_t vel_width{0};
        uint32_t vel_height{0};
        uint32_t view_w{0};   // the lone view's width within the velocity target
        uint32_t strip_x{0};
        uint32_t strip_y[2]{};
    };
    static constexpr uint32_t kVelStripTexels = 16;
    static constexpr uint32_t kVelStrips = 2;
    static constexpr uint32_t kVelRowBytes = 256; // 16 texels x 8B padded to placed-footprint alignment
    static constexpr uint32_t kVelSlotBytes = kVelStrips * kVelRowBytes;
    Microsoft::WRL::ComPtr<ID3D12Resource> m_vel_calib_readback{};
    uint8_t* m_vel_calib_mapped{nullptr};
    std::array<VelCalibSlot, kRing> m_vel_calib_slots{};
    std::vector<float> m_vel_err_gamma{};
    std::vector<float> m_vel_err_linear{};
    uint64_t m_vel_zero_texels{0};
    uint64_t m_vel_total_texels{0};
    bool m_vel_calibrated{false};
    std::chrono::steady_clock::time_point m_vel_calib_last_log{};

    Microsoft::WRL::ComPtr<ID3D12QueryHeap> m_ts_heap{};
    Microsoft::WRL::ComPtr<ID3D12Resource> m_ts_readback{};
    uint64_t* m_ts_mapped{nullptr};
    ID3D12CommandQueue* m_ts_queue{nullptr}; // weak, refreshed by the caller each frame
    uint64_t m_ts_frequency{0};
    // Per ring slot: which passes were bracketed, in recording order. The
    // slot is drained (kRing frames later, safely past the frame queue depth
    // - same in-flight assumption the cbuffer ring makes) before reuse.
    std::array<std::array<uint8_t, kTsMaxPasses>, kRing> m_ts_slot_seq{};
    std::array<uint8_t, kRing> m_ts_slot_count{};
    // Aggregates per pass since the last [DIBR][gpu] log line.
    std::array<double, kTsMaxPasses> m_ts_sum_ms{};
    std::array<uint32_t, kTsMaxPasses> m_ts_count{};
    std::chrono::steady_clock::time_point m_ts_last_log{};

    // Descriptor-churn guard: per ring slot, a hash of every input that feeds
    // the slot's descriptor writes; when unchanged the 10+ Create*View calls
    // are skipped. m_resource_generation invalidates on any internal
    // recreate (so a new texture at a recycled pointer can't alias a hash).
    std::array<uint64_t, kRing> m_slot_desc_hash{};
    uint32_t m_resource_generation{0};

    uint32_t m_ring_index{0};
    uint32_t m_frame_index{0};
};
} // namespace vrmod
