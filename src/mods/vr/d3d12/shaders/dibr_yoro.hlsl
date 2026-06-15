// dibr_yoro.hlsl — YORO / Meta-style asymmetric inverse-warp DIBR
//
// One eye is a pristine passthrough copy of the captured frame; the other
// eye carries the FULL inter-eye disparity (2× the per-eye offset used by
// symmetric DIBR). Same total stereo separation as dibr_inverse.hlsl, but
// artifact distribution favours a sharp reference eye (marginally cheaper too).
//
// reference_eye: mode_param0, where 0.0 = left reference and 1.0 = right reference.
//
// SCATTER_COMPOSE=1 (a second PSO compiled from this same source) specializes
// the kernel for the scatter pipeline: the gather search machinery is compiled
// out entirely, the synthesized eye comes from g_scatterColor.

#ifndef SCATTER_COMPOSE
#define SCATTER_COMPOSE 0
#endif

// DIBR_LEAN=1 (additional PSOs from this same source) compiles out the
// optional output features; the CPU picks it whenever every gated parameter
// is at its pass-through default (see DIBRSynthesis::params_allow_lean).
#ifndef DIBR_LEAN
#define DIBR_LEAN 0
#endif

// Thread-group edge (overridable via UEVR_DIBR_TG; the C++ dispatch math
// uses the same value).
#ifndef DIBR_TG
#define DIBR_TG 16
#endif

Texture2D<float4> g_colorTex : register(t0);
Texture2D<float>  g_depthTex : register(t1);
// Conditioned search depth (dibr_depth_prep.hlsl PREP_MODE 2): the old
// YoroSearchDepth chain evaluated once per source pixel. Gather path only.
Texture2D<float2> g_prepDepth : register(t2);
// SRV alias of the history color (same resource as u3, transitioned to a
// shader-readable state for the passes that only READ it) so the AFW blend
// can use one filtered sample instead of a manual 4-tap bilinear.
Texture2D<float4> g_historyColorSrv : register(t3);
RWTexture2D<float4> g_sbsOut : register(u0);
// Scatter pipeline intermediates (see dibr_scatter_*.hlsl); only read when
// scatter_compose > 0.5.
RWTexture2D<uint> g_scatterKey : register(u1);
RWTexture2D<float4> g_scatterColor : register(u2);
// Temporal history keys (AFW: last frame's REAL render of the eye being
// synthesized, device-depth keys); only read when temporal_enabled > 1.5.
RWTexture2D<uint> g_historyKey : register(u4);
// SceneVelocity snapshot (UE PF_A16B16G16R16: xy = gamma-encoded screen-space
// object motion, zero texel = static / not written, zw = packed previous
// device depth). Null descriptor when no snapshot exists; currently consumed
// by debug view 8 (the select/bind/sample wiring proof).
Texture2D<float4> g_velocityTex : register(t4);
Texture2D<float4> g_hybridTargetColorTex : register(t7);
SamplerState g_linearSampler : register(s0);
SamplerState g_pointSampler : register(s1);

cbuffer StereoParams : register(b0) {
    float divergence;
    float convergence;
    uint  srcWidth;
    uint  srcHeight;
    float edge_compression;
    float reverse_depth;
    float depth_floor;
    float depth_ceiling;
    float depth_gain;
    float depth_curve;
    float depth_range_boost_strength;
    float depth_range_boost_center;
    float depth_range_boost_width;
    float depth_range_boost_scale;
    float perspective_shift;
    float zpd_balance;
    float popout_limit;
    float edge_fill;
    float edge_fill_mode;
    float range_smoothing;
    float foreground_protect;
    float raymarch_steps;
    float mode_param0;
    float near_field_strength;
    float near_field_start;
    float near_field_end;
    float near_field_target;
    float auto_depth_strength;
    float auto_depth_radius;
    float auto_depth_min_range;
    float auto_depth_contrast;
    float disocclusion_strength;
    float disocclusion_threshold;
    float disocclusion_feather;
    float disocclusion_depth_weight;
    float edge_guard_strength;
    float edge_guard_width;
    float edge_guard_shape;
    float edge_guard_near_depth;
    float depth_uv_scale_x;
    float depth_uv_scale_y;
    float depth_uv_offset_x;
    float depth_uv_offset_y;
    float depth_uv_anchor;
    float depth_uv_flip_x;
    float depth_uv_flip_y;
    float depth_value_flip;
    float depth_linearize_strength;
    float depth_linearize_near;
    float depth_linearize_far;
    float depth_linearize_mode;
    float convergence_boundary_strength;
    float convergence_boundary_threshold;
    float convergence_boundary_feather;
    float convergence_boundary_scale;
    float depth_artifact_guard_strength;
    float depth_artifact_guard_threshold;
    float depth_artifact_guard_feather;
    float depth_artifact_guard_scale;
    float depth_edge_mask_strength;
    float depth_edge_mask_radius;
    float depth_edge_mask_threshold;
    float depth_edge_mask_feather;
    float depth_expand_strength;
    float depth_expand_radius;
    float depth_expand_edge_threshold;
    float depth_expand_near_bias;
    float letterbox_mask_strength;
    float letterbox_mask_x;
    float letterbox_mask_y;
    float letterbox_mask_feather;
    float letterbox_auto_strength;
    float letterbox_auto_mode;
    float letterbox_auto_max_x;
    float letterbox_auto_max_y;
    float letterbox_auto_threshold;
    float letterbox_auto_feather;
    float depth_reconstruct_strength;
    float depth_reconstruct_radius;
    float depth_reconstruct_edge_threshold;
    float depth_reconstruct_near_bias;
    float region_mask_strength;
    float region_mask_left;
    float region_mask_top;
    float region_mask_right;
    float region_mask_bottom;
    float region_mask_target_depth;
    float region_mask_feather;
    float region_mask_depth_gate;
    float weapon_mask_strength;
    float weapon_mask_left;
    float weapon_mask_top;
    float weapon_mask_right;
    float weapon_mask_bottom;
    float weapon_mask_target_depth;
    float weapon_mask_feather;
    float weapon_mask_depth_gate;
    float weapon_auto_mask_strength;
    float weapon_auto_mask_y_start;
    float weapon_auto_mask_near;
    float weapon_auto_mask_far;
    float weapon_auto_mask_target_depth;
    float weapon_auto_mask_feather;
    float weapon_boundary_strength;
    float weapon_boundary_y_start;
    float weapon_boundary_near;
    float weapon_boundary_far;
    float weapon_boundary_scale;
    float weapon_boundary_feather;
    float focus_reduction_strength;
    float focus_reduction_mode;
    float focus_reduction_world_scale;
    float focus_reduction_weapon_scale;
    float focus_reduction_eye_selection;
    float output_matte_strength;
    float output_matte_left;
    float output_matte_top;
    float output_matte_right;
    float output_matte_bottom;
    float output_matte_feather;
    float output_matte_mode;
    float output_matte_gray;
    float cursor_overlay_strength;
    float cursor_overlay_type;
    float cursor_overlay_x;
    float cursor_overlay_y;
    float cursor_overlay_size;
    float cursor_overlay_thickness;
    float cursor_overlay_feather;
    float cursor_overlay_depth;
    float cursor_overlay_color_mode;
    float cursor_overlay_lock_to_center;
    float depth_sample_mode;
    float depth_dither_strength;
    float depth_dither_bits;
    float raymarch_foveation_strength;
    float raymarch_foveation_radius;
    float raymarch_foveation_min_steps;
    float raymarch_foveation_curve;
    float debug_view_mode;
    float debug_view_scale;
    float debug_view_near;
    float debug_view_far;
    float ui_alpha_mask_strength;
    float ui_alpha_mask_threshold;
    float ui_alpha_mask_feather;
    float ui_alpha_mask_target_depth;
    float ui_auto_mask_strength;
    float ui_auto_mask_mode;
    float ui_auto_mask_threshold;
    float ui_auto_mask_feather;
    float ui_auto_mask_target_depth;
    float shape_mask_strength;
    float shape_mask_mode;
    float shape_mask_left;
    float shape_mask_top;
    float shape_mask_right;
    float shape_mask_bottom;
    float shape_mask_target_depth;
    float shape_mask_feather;
    float shape_mask_depth_gate;
    float shape_mask_invert;
    float shape_mask_edge_width;
    float comfort_nose_strength;
    float comfort_nose_width;
    float comfort_nose_height;
    float comfort_nose_y;
    float comfort_nose_feather;
    float comfort_nose_curve;
    float comfort_nose_mode;
    float comfort_nose_color_r;
    float comfort_nose_color_g;
    float comfort_nose_color_b;
    float image_filter_sharpen_strength;
    float image_filter_radius;
    float image_filter_sharpen_limit;
    float image_filter_aa_strength;
    float image_filter_aa_threshold;
    float image_filter_aa_feather;
    float image_filter_alpha_passthrough;
    float image_filter_deband_strength;
    float image_filter_deband_radius;
    float image_filter_deband_threshold;
    float image_filter_deband_grain;
    float output_eye_swap;
    float output_saturation;
    float output_vignette_strength;
    float output_vignette_radius;
    float output_vignette_feather;
    float output_hmd_vignette;
    float output_geometry_barrel;
    float output_geometry_radial_k2;
    float output_geometry_radial_k3;
    float output_geometry_poly_strength;
    float output_geometry_poly_k1_r;
    float output_geometry_poly_k1_g;
    float output_geometry_poly_k1_b;
    float output_geometry_poly_k2_r;
    float output_geometry_poly_k2_g;
    float output_geometry_poly_k2_b;
    float output_geometry_zoom;
    float output_geometry_fov;
    float output_geometry_scale_x;
    float output_geometry_scale_y;
    float output_geometry_offset_x;
    float output_geometry_offset_y;
    float output_geometry_left_offset_x;
    float output_geometry_left_offset_y;
    float output_geometry_right_offset_x;
    float output_geometry_right_offset_y;
    float output_geometry_left_rotation_deg;
    float output_geometry_right_rotation_deg;
    float output_geometry_keystone_tilt;
    float output_geometry_tie_right_alignment;
    float output_geometry_ipd_offset;
    float output_geometry_lens_dependent_ipd;
    float output_geometry_axis_swap;
    float output_headset_profile;
    float output_composition_mode;
    float output_layout_mode;
    float output_anaglyph_saturation;
    float output_anaglyph_contrast;
    float output_anaglyph_mode;
    float output_anaglyph_left_contrast;
    float output_anaglyph_right_contrast;
    float filter_emulator_focus;
    float filter_emulator_max_depth;
    float filter_emulator_near_reduction;
    float filter_emulator_auto_focus;
    float filter_emulator_reduce_r;
    float filter_emulator_reduce_g;
    float filter_emulator_reduce_b;
    float output_interlace_swap;
    float output_interlace_blend;
    float output_interlace_scale_mode;
    float output_interlace_sample_offset;
    float output_distortion_grid;
    float output_frame_marker_mode;
    float output_frame_marker_thickness;
    float output_alignment_marker_mode;
    float output_alignment_marker_thickness;
    float stereo_axis_mode;
    uint  frame_index;
    float reproj_enabled;
    float4x4 reproj_source_to_left;
    float4x4 reproj_source_to_right;
    float scatter_compose;
    float overscan_x;
    float temporal_enabled;
    float temporal_blend;
    float4x4 reproj_target_to_prev;
    // Output (submit) eye size - differs from srcWidth when the overscan-grown
    // render target makes the source wider than the true-FOV output.
    uint  out_width;
    uint  out_height;
    uint  synth_width;
    uint  synth_height;
    // CPU-resolved constants (stamped by DIBRSynthesis::synthesize each
    // frame): dispatch-uniform values hoisted out of the per-pixel code.
    float pre_effective_convergence;
    float pre_inv_src_width;
    float pre_inv_src_height;
    float pre_edge_comp_inv;
    float hybrid_target_rect_min_x;
    float hybrid_target_rect_min_y;
    float hybrid_target_rect_max_x;
    float hybrid_target_rect_max_y;
};

float EffectiveConvergence()
{
    return pre_effective_convergence;
}

float StereoDepthDelta(float depth)
{
    return depth - EffectiveConvergence();
}

float FilterEmulatorMask()
{
    float compositionMode = floor(output_composition_mode + 0.5f);
    float anaglyphMode = floor(output_anaglyph_mode + 0.5f);
    return (anaglyphMode >= 4.5f && compositionMode >= 2.5f && compositionMode < 5.5f) ? 1.0f : 0.0f;
}

float FilterEmulatorFocusScale(float depth)
{
    if (FilterEmulatorMask() <= 0.0f) {
        return 1.0f;
    }

    float focusValue = clamp(filter_emulator_focus, 0.0f, 1.5f);
    float scale = max(0.0f, lerp(0.25f, 0.75f, 1.0f - focusValue));
    if (filter_emulator_auto_focus > 0.5f) {
        float focusDistance = abs(depth - EffectiveConvergence());
        scale *= lerp(0.75f, 1.0f, saturate(smoothstep(0.0f, 0.13f, focusDistance)));
    }
    return scale;
}

float ApplyFilterEmulatorDepthControls(float depth)
{
    if (FilterEmulatorMask() <= 0.0f) {
        return depth;
    }

    float adjusted = depth;
    float farMask = saturate(adjusted * 0.5f);
    adjusted = lerp(adjusted, min(adjusted, saturate(filter_emulator_max_depth)), farMask);

    float focus = EffectiveConvergence();
    float nearMask = saturate((focus - adjusted) / max(focus, 0.0001f));
    adjusted = lerp(adjusted, lerp(adjusted, focus, 0.25f), nearMask * saturate(filter_emulator_near_reduction));
    return saturate(adjusted);
}

float2 TransformDepthUv(float2 uv)
{
    float2 scale = max(float2(depth_uv_scale_x, depth_uv_scale_y), float2(0.0001f, 0.0001f));
    float2 offset = float2(depth_uv_offset_x, depth_uv_offset_y);
    float anchor = floor(depth_uv_anchor + 0.5f);
    float2 mapped = (anchor < 0.5f)
        ? (uv - 0.5f) / scale + 0.5f
        : ((anchor < 1.5f) ? uv / scale : 1.0f - ((1.0f - uv) / scale));
    mapped += offset;
    if (depth_uv_flip_x > 0.5f) {
        mapped.x = 1.0f - mapped.x;
    }
    if (depth_uv_flip_y > 0.5f) {
        mapped.y = 1.0f - mapped.y;
    }
    return saturate(mapped);
}

float SampleDepthTexture(float2 uv)
{
    float mode = floor(depth_sample_mode + 0.5f);
    return (mode >= 1.0f)
        ? g_depthTex.SampleLevel(g_pointSampler, uv, 0)
        : g_depthTex.SampleLevel(g_linearSampler, uv, 0);
}

float LetterboxAutoMask(float2 uv)
{
    float strength = saturate(letterbox_auto_strength);
    if (strength <= 0.0f) {
        return 0.0f;
    }

    float mode = floor(letterbox_auto_mode + 0.5f);
    float feather = max(letterbox_auto_feather, 0.0001f);
    float xExtent = saturate(letterbox_auto_max_x);
    float yExtent = saturate(letterbox_auto_max_y);
    float xMask = (mode < 0.5f || mode >= 1.5f)
        ? 1.0f - smoothstep(max(xExtent - feather, 0.0f), xExtent, min(uv.x, 1.0f - uv.x))
        : 0.0f;
    float yMask = (mode < 1.5f)
        ? 1.0f - smoothstep(max(yExtent - feather, 0.0f), yExtent, min(uv.y, 1.0f - uv.y))
        : 0.0f;
    float3 color = g_colorTex.SampleLevel(g_linearSampler, saturate(uv), 0).rgb;
    float luma = dot(color, float3(0.2126f, 0.7152f, 0.0722f));
    float threshold = saturate(letterbox_auto_threshold);
    float darkMask = 1.0f - smoothstep(threshold, min(threshold + feather, 1.0f), luma);
    return saturate(max(xMask, yMask) * darkMask * strength);
}

float LetterboxMask(float2 uv)
{
    float feather = max(letterbox_mask_feather, 0.0001f);
    float xMask = (letterbox_mask_x > 0.0f)
        ? 1.0f - smoothstep(max(letterbox_mask_x - feather, 0.0f), letterbox_mask_x, min(uv.x, 1.0f - uv.x))
        : 0.0f;
    float yMask = (letterbox_mask_y > 0.0f)
        ? 1.0f - smoothstep(max(letterbox_mask_y - feather, 0.0f), letterbox_mask_y, min(uv.y, 1.0f - uv.y))
        : 0.0f;
    float manualMask = max(xMask, yMask) * saturate(letterbox_mask_strength);
    return saturate(max(manualMask, LetterboxAutoMask(uv)));
}

float ApplyLetterboxDepthMask(float2 uv, float depth)
{
    return lerp(depth, EffectiveConvergence(), LetterboxMask(uv));
}

float RegionDepthMask(float2 uv, float depth)
{
    float strength = saturate(region_mask_strength);
    if (strength <= 0.0f) {
        return 0.0f;
    }

    float left = saturate(region_mask_left);
    float top = saturate(region_mask_top);
    float right = saturate(region_mask_right);
    float bottom = saturate(region_mask_bottom);
    if (right <= left || bottom <= top) {
        return 0.0f;
    }

    float feather = max(region_mask_feather, 0.0001f);
    float xMask = smoothstep(left, min(left + feather, right), uv.x)
        * (1.0f - smoothstep(max(right - feather, left), right, uv.x));
    float yMask = smoothstep(top, min(top + feather, bottom), uv.y)
        * (1.0f - smoothstep(max(bottom - feather, top), bottom, uv.y));
    float gate = saturate(region_mask_depth_gate);
    float depthMask = (gate < 1.0f)
        ? 1.0f - smoothstep(gate, min(gate + feather, 1.0f), depth)
        : 1.0f;
    return saturate(xMask * yMask * depthMask * strength);
}

float ApplyRegionDepthMask(float2 uv, float depth)
{
    return lerp(depth, saturate(region_mask_target_depth), RegionDepthMask(uv, depth));
}

float WeaponDepthMask(float2 uv, float depth)
{
    float strength = saturate(weapon_mask_strength);
    if (strength <= 0.0f) {
        return 0.0f;
    }

    float left = saturate(weapon_mask_left);
    float top = saturate(weapon_mask_top);
    float right = saturate(weapon_mask_right);
    float bottom = saturate(weapon_mask_bottom);
    if (right <= left || bottom <= top) {
        return 0.0f;
    }

    float feather = max(weapon_mask_feather, 0.0001f);
    float xMask = smoothstep(left, min(left + feather, right), uv.x)
        * (1.0f - smoothstep(max(right - feather, left), right, uv.x));
    float yMask = smoothstep(top, min(top + feather, bottom), uv.y)
        * (1.0f - smoothstep(max(bottom - feather, top), bottom, uv.y));
    float gate = saturate(weapon_mask_depth_gate);
    float depthMask = (gate < 1.0f)
        ? 1.0f - smoothstep(gate, min(gate + feather, 1.0f), depth)
        : 1.0f;
    return saturate(xMask * yMask * depthMask * strength);
}

float AutoWeaponDepthMask(float2 uv, float depth)
{
    float strength = saturate(weapon_auto_mask_strength);
    if (strength <= 0.0f) {
        return 0.0f;
    }

    float feather = max(weapon_auto_mask_feather, 0.0001f);
    float yStart = min(saturate(weapon_auto_mask_y_start), 0.9999f);
    float yMask = smoothstep(yStart, min(yStart + feather, 1.0f), uv.y);
    float nearDepth = min(saturate(weapon_auto_mask_near), 0.9999f);
    float farDepth = min(max(saturate(weapon_auto_mask_far), nearDepth + 0.0001f), 1.0f);
    float depthMask = 1.0f - smoothstep(nearDepth, farDepth, depth);
    return saturate(yMask * depthMask * strength);
}

float WeaponBoundaryScale(float2 uv, float depth)
{
    float strength = saturate(weapon_boundary_strength);
    if (strength <= 0.0f) {
        return 1.0f;
    }

    float feather = max(weapon_boundary_feather, 0.0001f);
    float yStart = min(saturate(weapon_boundary_y_start), 0.9999f);
    float yMask = smoothstep(yStart, min(yStart + feather, 1.0f), uv.y);
    float nearDepth = min(saturate(weapon_boundary_near), 0.9999f);
    float farDepth = min(max(saturate(weapon_boundary_far), nearDepth + 0.0001f), 1.0f);
    float depthMask = 1.0f - smoothstep(nearDepth, farDepth, depth);
    float mask = saturate(yMask * depthMask * strength);
    return lerp(1.0f, saturate(weapon_boundary_scale), mask);
}

float AutoWeaponFocusMask(float2 uv, float depth)
{
    float feather = max(weapon_auto_mask_feather, 0.0001f);
    float yStart = min(saturate(weapon_auto_mask_y_start), 0.9999f);
    float yMask = smoothstep(yStart, min(yStart + feather, 1.0f), uv.y);
    float nearDepth = min(saturate(weapon_auto_mask_near), 0.9999f);
    float farDepth = min(max(saturate(weapon_auto_mask_far), nearDepth + 0.0001f), 1.0f);
    float depthMask = 1.0f - smoothstep(nearDepth, farDepth, depth);
    return saturate(yMask * depthMask);
}

float FocusReductionWeaponMask(float2 uv, float depth)
{
    return saturate(max(WeaponDepthMask(uv, depth), AutoWeaponFocusMask(uv, depth)));
}

float FocusReductionBaseScale(float2 uv, float depth)
{
    float strength = saturate(focus_reduction_strength);
    if (strength <= 0.0f) {
        return 1.0f;
    }

    float mode = floor(focus_reduction_mode + 0.5f);
    float weaponMask = FocusReductionWeaponMask(uv, depth);
    float worldScale = saturate(focus_reduction_world_scale);
    float weaponScale = saturate(focus_reduction_weapon_scale);
    float targetScale = lerp(worldScale, weaponScale, weaponMask);
    if (mode < 0.5f) {
        targetScale = lerp(worldScale, 1.0f, weaponMask);
    } else if (mode < 1.5f) {
        targetScale = lerp(1.0f, weaponScale, weaponMask);
    }
    return lerp(1.0f, targetScale, strength);
}

float FocusReductionEyeGate(float eyeSign)
{
    float selection = floor(focus_reduction_eye_selection + 0.5f);
    if (selection < 0.5f) {
        return 1.0f;
    }
    if (selection < 1.5f) {
        return (eyeSign < 0.0f) ? 1.0f : 0.0f;
    }
    return (eyeSign > 0.0f) ? 1.0f : 0.0f;
}

float FocusReductionScale(float2 uv, float depth, float eyeSign)
{
    return lerp(1.0f, FocusReductionBaseScale(uv, depth), FocusReductionEyeGate(eyeSign));
}

float ApplyWeaponDepthMask(float2 uv, float depth)
{
    float manualMask = WeaponDepthMask(uv, depth);
    float autoMask = AutoWeaponDepthMask(uv, depth);
    float maskedDepth = lerp(depth, saturate(weapon_mask_target_depth), manualMask);
    return lerp(maskedDepth, saturate(weapon_auto_mask_target_depth), autoMask);
}

float RectShapeMask(float2 uv, float left, float top, float right, float bottom, float feather)
{
    float xMask = smoothstep(left, min(left + feather, right), uv.x)
        * (1.0f - smoothstep(max(right - feather, left), right, uv.x));
    float yMask = smoothstep(top, min(top + feather, bottom), uv.y)
        * (1.0f - smoothstep(max(bottom - feather, top), bottom, uv.y));
    return saturate(xMask * yMask);
}

float ShapeDepthMask(float2 uv, float depth)
{
    float strength = saturate(shape_mask_strength);
    float mode = floor(shape_mask_mode + 0.5f);
    if (strength <= 0.0f || mode < 0.5f) {
        return 0.0f;
    }

    float left = saturate(shape_mask_left);
    float top = saturate(shape_mask_top);
    float right = saturate(shape_mask_right);
    float bottom = saturate(shape_mask_bottom);
    if (right <= left || bottom <= top) {
        return 0.0f;
    }

    float feather = max(shape_mask_feather, 0.0001f);
    float2 center = float2((left + right) * 0.5f, (top + bottom) * 0.5f);
    float2 halfSize = max(float2((right - left) * 0.5f, (bottom - top) * 0.5f), float2(0.0001f, 0.0001f));
    float rectMask = RectShapeMask(uv, left, top, right, bottom, feather);
    float2 ellipseD = (uv - center) / halfSize;
    float ellipseRadius = length(ellipseD);
    float featherNorm = feather / max(min(halfSize.x, halfSize.y), 0.0001f);
    float ellipseMask = 1.0f - smoothstep(max(1.0f - featherNorm, 0.0f), 1.0f, ellipseRadius);
    float edgeWidth = max(shape_mask_edge_width, 0.0f);
    float rectDistance = min(min(uv.x - left, right - uv.x), min(uv.y - top, bottom - uv.y));
    float rectEdge = rectMask * (1.0f - smoothstep(edgeWidth, edgeWidth + feather, rectDistance));
    float ellipseDistance = max((1.0f - ellipseRadius) * min(halfSize.x, halfSize.y), 0.0f);
    float ellipseEdge = saturate(ellipseMask) * (1.0f - smoothstep(edgeWidth, edgeWidth + feather, ellipseDistance));

    float shape = (mode < 1.5f) ? rectMask
        : ((mode < 2.5f) ? ellipseMask
        : ((mode < 3.5f) ? rectEdge : ellipseEdge));
    shape = lerp(shape, 1.0f - shape, step(0.5f, shape_mask_invert));

    float gate = saturate(shape_mask_depth_gate);
    float depthMask = (gate < 1.0f)
        ? 1.0f - smoothstep(gate, min(gate + feather, 1.0f), depth)
        : 1.0f;
    return saturate(shape * depthMask * strength);
}

float ApplyShapeDepthMask(float2 uv, float depth)
{
    return lerp(depth, saturate(shape_mask_target_depth), ShapeDepthMask(uv, depth));
}

float OutputMatteMask(float2 uv)
{
    float strength = saturate(output_matte_strength);
    if (strength <= 0.0f) {
        return 0.0f;
    }

    float left = saturate(output_matte_left);
    float top = saturate(output_matte_top);
    float right = saturate(output_matte_right);
    float bottom = saturate(output_matte_bottom);
    if (right <= left || bottom <= top) {
        return 0.0f;
    }

    float feather = max(output_matte_feather, 0.0001f);
    float xMask = smoothstep(left, min(left + feather, right), uv.x)
        * (1.0f - smoothstep(max(right - feather, left), right, uv.x));
    float yMask = smoothstep(top, min(top + feather, bottom), uv.y)
        * (1.0f - smoothstep(max(bottom - feather, top), bottom, uv.y));
    return saturate(xMask * yMask * strength);
}

float4 ApplyOutputMatte(float2 uv, float4 stereoColor, float4 centerColor)
{
#if DIBR_LEAN
    return stereoColor;
#endif
    float mask = OutputMatteMask(uv);
    if (mask <= 0.0f) {
        return stereoColor;
    }

    float mode = floor(output_matte_mode + 0.5f);
    float gray = saturate(output_matte_gray);
    float4 target = centerColor;
    if (mode >= 0.5f && mode < 1.5f) {
        target = float4(0.0f, 0.0f, 0.0f, stereoColor.a);
    } else if (mode >= 1.5f && mode < 2.5f) {
        target = float4(1.0f, 1.0f, 1.0f, stereoColor.a);
    } else if (mode >= 2.5f) {
        target = float4(gray, gray, gray, stereoColor.a);
    }
    return lerp(stereoColor, target, mask);
}

float3 CursorOverlayColor()
{
    float mode = floor(cursor_overlay_color_mode + 0.5f);
    if (mode < 0.5f) return float3(1.0f, 1.0f, 1.0f);
    if (mode < 1.5f) return float3(0.0f, 0.0f, 0.0f);
    if (mode < 2.5f) return float3(1.0f, 0.0f, 0.0f);
    if (mode < 3.5f) return float3(0.0f, 1.0f, 0.0f);
    if (mode < 4.5f) return float3(0.0f, 0.25f, 1.0f);
    if (mode < 5.5f) return float3(0.0f, 1.0f, 1.0f);
    if (mode < 6.5f) return float3(1.0f, 0.0f, 1.0f);
    return float3(1.0f, 1.0f, 0.0f);
}

float CursorSegmentMask(float2 p, float2 a, float2 b, float thickness, float feather)
{
    float2 pa = p - a;
    float2 ba = b - a;
    float h = saturate(dot(pa, ba) / max(dot(ba, ba), 0.000001f));
    float dist = length(pa - ba * h);
    return 1.0f - smoothstep(thickness, thickness + feather, dist);
}

float CursorOverlayMask(float2 uv, float eyeSign)
{
    float strength = saturate(cursor_overlay_strength);
    float type = floor(cursor_overlay_type + 0.5f);
    if (strength <= 0.0f || type < 0.5f) {
        return 0.0f;
    }

    float centerX = lerp(saturate(cursor_overlay_x), 0.5f, step(0.5f, cursor_overlay_lock_to_center));
    float2 center = float2(centerX, saturate(cursor_overlay_y));
    float cursorShift = divergence * StereoDepthDelta(saturate(cursor_overlay_depth)) / (float)srcWidth;
    center.x -= eyeSign * cursorShift;

    float2 d = uv - center;
    d.x *= (float)srcWidth / max((float)srcHeight, 1.0f);
    float size = max(cursor_overlay_size, 0.0001f);
    float thickness = max(cursor_overlay_thickness, 0.0001f);
    float feather = max(cursor_overlay_feather, 0.00001f);
    float lenD = length(d);
    float crossX = (1.0f - smoothstep(thickness, thickness + feather, abs(d.y)))
        * (1.0f - smoothstep(size, size + feather, abs(d.x)));
    float crossY = (1.0f - smoothstep(thickness, thickness + feather, abs(d.x)))
        * (1.0f - smoothstep(size, size + feather, abs(d.y)));
    float cross = saturate(max(crossX, crossY));
    float ring = 1.0f - smoothstep(thickness, thickness + feather, abs(lenD - size * 0.65f));
    float diamond = 1.0f - smoothstep(size, size + feather, abs(d.x) + abs(d.y));
    float dot = 1.0f - smoothstep(size * 0.45f, size * 0.45f + feather, lenD);
    float pointer = max(
        CursorSegmentMask(d, float2(-size * 0.55f, -size * 0.55f), float2(size * 0.38f, size * 0.02f), thickness, feather),
        CursorSegmentMask(d, float2(-size * 0.55f, -size * 0.55f), float2(-size * 0.05f, size * 0.48f), thickness, feather));
    pointer = max(pointer, CursorSegmentMask(d, float2(size * 0.02f, size * 0.20f), float2(size * 0.32f, size * 0.52f), thickness, feather));

    float mask = (type < 1.5f) ? max(ring, cross * 0.75f)
        : ((type < 2.5f) ? diamond
        : ((type < 3.5f) ? dot
        : ((type < 4.5f) ? cross : pointer)));
    return saturate(mask * strength);
}

float4 ApplyCursorOverlay(float2 uv, float eyeSign, float4 color)
{
#if DIBR_LEAN
    return color;
#endif
    float mask = CursorOverlayMask(uv, eyeSign);
    if (mask <= 0.0f) {
        return color;
    }
    return float4(lerp(color.rgb, CursorOverlayColor(), mask), color.a);
}

float ComfortNoseMask(float2 uv, float eyeSign)
{
    float strength = saturate(comfort_nose_strength);
    if (strength <= 0.0f) {
        return 0.0f;
    }

    float side = (eyeSign > 0.0f) ? (1.0f - uv.x) : uv.x;
    float width = max(comfort_nose_width, 0.0001f);
    float height = max(comfort_nose_height, 0.0001f);
    float yCenter = saturate(comfort_nose_y);
    float feather = max(comfort_nose_feather, 0.0001f);
    float curve = max(comfort_nose_curve, 0.1f);
    float x = side / width;
    float y = abs(uv.y - yCenter) / height;
    float shape = pow(max(x, 0.0f), curve) + y * y;
    return saturate((1.0f - smoothstep(max(1.0f - feather, 0.0f), 1.0f, shape)) * strength);
}

float4 ApplyComfortNose(float2 uv, float eyeSign, float4 color)
{
#if DIBR_LEAN
    return color;
#endif
    float mask = ComfortNoseMask(uv, eyeSign);
    if (mask <= 0.0f) {
        return color;
    }
    float mode = floor(comfort_nose_mode + 0.5f);
    float3 target = float3(0.0f, 0.0f, 0.0f);
    if (mode >= 0.5f) {
        float3 noseColor = saturate(float3(comfort_nose_color_r, comfort_nose_color_g, comfort_nose_color_b));
        target = noseColor;
        if (mode >= 1.5f) {
            float luma = dot(color.rgb, float3(0.2126f, 0.7152f, 0.0722f));
            target = (mode < 2.5f) ? (noseColor * luma) : (noseColor * color.rgb);
        }
    }
    return float4(lerp(color.rgb, target, mask), color.a);
}

float ImageFilterLuma(float3 color)
{
    return dot(color, float3(0.2126f, 0.7152f, 0.0722f));
}

float ImageFilterNoise(float2 uv)
{
    float2 p = uv * float2((float)srcWidth, (float)srcHeight);
    return frac(52.9829189f * frac(dot(p, float2(0.06711056f, 0.00583715f))));
}

float4 ApplyImageFilter(float2 sampleUv, float4 color)
{
#if DIBR_LEAN
    return color;
#endif
    float sharpenStrength = max(image_filter_sharpen_strength, 0.0f);
    float aaStrength = saturate(image_filter_aa_strength);
    float debandStrength = saturate(image_filter_deband_strength);
    float debandGrain = max(image_filter_deband_grain, 0.0f);
    if (sharpenStrength <= 0.0f && aaStrength <= 0.0f && debandStrength <= 0.0f && debandGrain <= 0.0f) {
        return color;
    }

    float2 uv = saturate(sampleUv);
    float4 filtered = color;

    if (debandStrength > 0.0f || debandGrain > 0.0f) {
        float debandRadius = max(image_filter_deband_radius, 0.0f);
        float2 debandTexel = float2(1.0f / max((float)srcWidth, 1.0f), 1.0f / max((float)srcHeight, 1.0f)) * debandRadius;
        float4 dl = g_colorTex.SampleLevel(g_linearSampler, saturate(uv - float2(debandTexel.x, 0.0f)), 0);
        float4 dr = g_colorTex.SampleLevel(g_linearSampler, saturate(uv + float2(debandTexel.x, 0.0f)), 0);
        float4 du = g_colorTex.SampleLevel(g_linearSampler, saturate(uv - float2(0.0f, debandTexel.y)), 0);
        float4 dd = g_colorTex.SampleLevel(g_linearSampler, saturate(uv + float2(0.0f, debandTexel.y)), 0);
        float4 debandAverage = (dl + dr + du + dd + filtered * 4.0f) / 8.0f;
        float debandGradient = max(abs(ImageFilterLuma(dr.rgb) - ImageFilterLuma(dl.rgb)),
                                   abs(ImageFilterLuma(dd.rgb) - ImageFilterLuma(du.rgb)));
        float threshold = max(image_filter_deband_threshold, 0.0f);
        float flatMask = 1.0f - smoothstep(threshold, threshold * 2.0f + 0.0001f, debandGradient);
        filtered.rgb = saturate(lerp(filtered.rgb, debandAverage.rgb, debandStrength * flatMask));
        if (debandGrain > 0.0f) {
            float grain = (ImageFilterNoise(uv) - 0.5f) * (debandGrain / 255.0f) * flatMask;
            filtered.rgb = saturate(filtered.rgb + grain);
        }
    }

    float radius = max(image_filter_radius, 0.0f);
    float2 texel = float2(1.0f / max((float)srcWidth, 1.0f), 1.0f / max((float)srcHeight, 1.0f)) * radius;
    float4 left = g_colorTex.SampleLevel(g_linearSampler, saturate(uv - float2(texel.x, 0.0f)), 0);
    float4 right = g_colorTex.SampleLevel(g_linearSampler, saturate(uv + float2(texel.x, 0.0f)), 0);
    float4 up = g_colorTex.SampleLevel(g_linearSampler, saturate(uv - float2(0.0f, texel.y)), 0);
    float4 down = g_colorTex.SampleLevel(g_linearSampler, saturate(uv + float2(0.0f, texel.y)), 0);
    float4 average = (left + right + up + down + filtered * 2.0f) / 6.0f;

    float gradient = max(abs(ImageFilterLuma(right.rgb) - ImageFilterLuma(left.rgb)),
                         abs(ImageFilterLuma(down.rgb) - ImageFilterLuma(up.rgb)));
    float threshold = max(image_filter_aa_threshold, 0.0f);
    float feather = max(image_filter_aa_feather, 0.0001f);
    float edgeMask = smoothstep(threshold, threshold + feather, gradient);
    filtered = lerp(filtered, average, aaStrength * edgeMask);

    if (sharpenStrength > 0.0f) {
        float4 blur = (left + right + up + down + filtered * 4.0f) / 8.0f;
        float limit = max(image_filter_sharpen_limit, 0.0f);
        float3 detail = clamp(filtered.rgb - blur.rgb, float3(-limit, -limit, -limit), float3(limit, limit, limit));
        filtered.rgb = saturate(filtered.rgb + detail * sharpenStrength);
    }

    filtered.a = lerp(filtered.a, color.a, saturate(image_filter_alpha_passthrough));
    return filtered;
}

float2 OutputPolynomialUv(float2 uv, float k1, float k2)
{
    float2 p = uv * 2.0f - 1.0f;
    float r2 = dot(p, p);
    float r4 = r2 * r2;
    p *= 1.0f + k1 * r2 + k2 * r4;
    return saturate(p * 0.5f + 0.5f);
}

float4 OutputDistortionGridColor(float2 uv)
{
    float2 grid = abs(frac((uv - 0.5f) * 25.0f));
    float gridLine = (grid.x > 0.9f || grid.y > 0.9f) ? 1.0f : 0.0f;
    return float4(gridLine, gridLine, gridLine, 1.0f);
}

float4 SampleOutputSource(float2 uv)
{
#if !DIBR_LEAN
    if (output_distortion_grid > 0.5f) {
        return OutputDistortionGridColor(uv);
    }
#endif
    return g_colorTex.SampleLevel(g_linearSampler, uv, 0);
}

float OutputHeadsetProfileMode()
{
    return floor(clamp(output_headset_profile, 0.0f, 3.0f) + 0.5f);
}

bool OutputHeadsetProfileEnabled()
{
    return OutputHeadsetProfileMode() > 0.5f;
}

float OutputHeadsetIpdOffset()
{
    float profile = OutputHeadsetProfileMode();
    if (profile < 0.5f) {
        return output_geometry_ipd_offset;
    }
    if (profile < 1.5f) {
        return 0.0f;
    }
    if (profile < 2.5f) {
        return 25.0f;
    }
    return 27.25f;
}

float2 OutputHeadsetScale()
{
    float profile = OutputHeadsetProfileMode();
    if (profile < 0.5f) {
        return max(float2(output_geometry_scale_x, output_geometry_scale_y), float2(0.0001f, 0.0001f));
    }
    float yScale = (profile >= 1.5f && profile < 2.5f) ? 0.925f : 1.0f;
    return float2(1.0f, yScale);
}

float OutputHeadsetZoom()
{
    return OutputHeadsetProfileEnabled() ? 1.0f : max(output_geometry_zoom, 0.0001f);
}

float OutputHeadsetFov()
{
    return OutputHeadsetProfileEnabled() ? 0.0f : output_geometry_fov;
}

float2 OutputHeadsetPixelOffset(float eyeSign)
{
    if (OutputHeadsetProfileEnabled()) {
        return float2(0.0f, 0.0f);
    }
    bool useLeftAlignment = eyeSign > 0.0f || output_geometry_tie_right_alignment > 0.5f;
    return useLeftAlignment
        ? float2(output_geometry_left_offset_x, output_geometry_left_offset_y)
        : float2(output_geometry_right_offset_x, output_geometry_right_offset_y);
}

float OutputHeadsetRotation(float eyeSign)
{
    if (OutputHeadsetProfileEnabled()) {
        return 0.0f;
    }
    bool useLeftAlignment = eyeSign > 0.0f || output_geometry_tie_right_alignment > 0.5f;
    return useLeftAlignment ? output_geometry_left_rotation_deg : output_geometry_right_rotation_deg;
}

float3 OutputHeadsetPolyK1()
{
    if (OutputHeadsetProfileEnabled()) {
        return float3(0.22f, 0.22f, 0.22f);
    }
    return float3(output_geometry_poly_k1_r, output_geometry_poly_k1_g, output_geometry_poly_k1_b);
}

float3 OutputHeadsetPolyK2()
{
    if (OutputHeadsetProfileEnabled()) {
        return float3(0.24f, 0.24f, 0.24f);
    }
    return float3(output_geometry_poly_k2_r, output_geometry_poly_k2_g, output_geometry_poly_k2_b);
}

float4 SampleOutputColor(float2 sampleUv)
{
    float2 uv = saturate(sampleUv);
    float4 baseColor = SampleOutputSource(uv);
#if DIBR_LEAN
    return baseColor;
#endif
    float strength = saturate(output_geometry_poly_strength);
    if (strength <= 0.0f) {
        return baseColor;
    }

    float3 k1 = clamp(OutputHeadsetPolyK1(), -2.0f, 2.0f);
    float3 k2 = clamp(OutputHeadsetPolyK2(), -2.0f, 2.0f);
    float2 uvR = lerp(uv, OutputPolynomialUv(uv, k1.r, k2.r), strength);
    float2 uvG = lerp(uv, OutputPolynomialUv(uv, k1.g, k2.g), strength);
    float2 uvB = lerp(uv, OutputPolynomialUv(uv, k1.b, k2.b), strength);
    float4 red = SampleOutputSource(uvR);
    float4 green = SampleOutputSource(uvG);
    float4 blue = SampleOutputSource(uvB);
    return float4(red.r, green.g, blue.b, green.a);
}

float4 SampleFilteredOutputColor(float2 sampleUv)
{
    return ApplyImageFilter(sampleUv, SampleOutputColor(sampleUv));
}

float2 MirrorEdgeUv(float2 uv)
{
    return 1.0f - abs(frac(uv * 0.5f) * 2.0f - 1.0f);
}

float4 SampleStereoColor(float2 sampleUv, float2 centerUv, float4 centerColor)
{
    float outside = (sampleUv.x < 0.0f || sampleUv.x > 1.0f ||
                     sampleUv.y < 0.0f || sampleUv.y > 1.0f) ? 1.0f : 0.0f;
    float mode = floor(edge_fill_mode + 0.5f);
    float2 edgeUv = (mode < 0.5f) ? MirrorEdgeUv(sampleUv) : saturate(sampleUv);
    float4 edgeColor = SampleFilteredOutputColor(edgeUv);
    if (outside >= 0.5f) {
        float4 blackColor = float4(0.0f, 0.0f, 0.0f, centerColor.a);
        float4 fillColor = (mode >= 0.5f && mode < 1.5f) ? blackColor : edgeColor;
        return lerp(centerColor, fillColor, saturate(edge_fill));
    }
    return edgeColor;
}

float3 PrepareCompositionColor(float3 color, float eyeContrast)
{
    float sat = max(output_anaglyph_saturation, 0.0f);
    float luma = ImageFilterLuma(color);
    color = lerp(float3(luma, luma, luma), color, sat);
    float contrast = max(output_anaglyph_contrast * eyeContrast, 0.0f);
    return saturate((color - 0.5f) * contrast + 0.5f);
}

float3 ComposeSimpleAnaglyphColor(float3 left, float3 right, float pair)
{
    if (pair < 0.5f) {
        return float3(left.r, right.g, right.b);
    }
    if (pair < 1.5f) {
        return float3(right.r, left.g, right.b);
    }
    return float3(right.r, right.g, left.b);
}

float3 ComposeOptimizedAnaglyphColor(float3 left, float3 right, float pair)
{
    if (pair < 0.5f) {
        float3 dubois;
        dubois.r = dot(left, float3(0.437f, 0.449f, 0.164f)) + dot(right, float3(-0.062f, -0.062f, -0.024f));
        dubois.g = dot(left, float3(-0.011f, -0.032f, -0.007f)) + dot(right, float3(0.377f, 0.761f, -0.009f));
        dubois.b = dot(left, float3(-0.015f, -0.034f, -0.006f)) + dot(right, float3(-0.026f, -0.093f, 1.234f));
        return saturate(dubois);
    }

    if (pair < 1.5f) {
        float3 tuned;
        tuned.r = dot(left, float3(-0.062f, -0.158f, -0.039f)) + dot(right, float3(0.529f, 0.705f, 0.024f));
        tuned.g = dot(left, float3(0.284f, 0.668f, 0.143f)) + dot(right, float3(-0.016f, -0.015f, 0.065f));
        tuned.b = dot(left, float3(-0.015f, -0.027f, 0.021f)) + dot(right, float3(0.009f, 0.075f, 0.937f));
        return saturate(tuned);
    }

    float3 amber;
    amber.r = dot(left, float3(0.72f, 0.21f, 0.07f)) + dot(right, float3(0.04f, 0.04f, -0.02f));
    amber.g = dot(left, float3(0.18f, 0.72f, 0.10f)) + dot(right, float3(0.03f, 0.03f, -0.01f));
    amber.b = dot(right, float3(0.07f, 0.18f, 0.75f)) + dot(left, float3(-0.03f, -0.03f, 0.06f));
    return saturate(amber);
}

float3 ComposeDeghostAnaglyphColor(float3 left, float3 right, float pair)
{
    float3 simple = ComposeSimpleAnaglyphColor(left, right, pair);
    float l = ImageFilterLuma(left);
    float r = ImageFilterLuma(right);
    float delta = l - r;
    float3 correction;
    if (pair < 0.5f) {
        correction = float3(delta * 0.08f, -delta * 0.04f, -delta * 0.04f);
    } else if (pair < 1.5f) {
        correction = float3(-delta * 0.04f, delta * 0.08f, -delta * 0.04f);
    } else {
        correction = float3(-delta * 0.04f, -delta * 0.04f, delta * 0.08f);
    }
    return saturate(simple + correction);
}

float3 ComposeWarmCoolAnaglyphColor(float3 left, float3 right, float pair)
{
    pair = floor(pair + 0.5f);
    if (pair < 0.5f) {
        float warm = dot(left, float3(0.72f, 0.22f, 0.06f));
        float coolG = dot(right, float3(0.08f, 0.78f, 0.14f));
        float coolB = dot(right, float3(0.04f, 0.24f, 0.72f));
        return saturate(float3(warm, coolG, coolB));
    }
    if (pair < 1.5f) {
        float magR = dot(right, float3(0.72f, 0.22f, 0.06f));
        float green = dot(left, float3(0.12f, 0.76f, 0.12f));
        float magB = dot(right, float3(0.06f, 0.22f, 0.72f));
        return saturate(float3(magR, green, magB));
    }

    float amberR = dot(right, float3(0.78f, 0.18f, 0.04f));
    float amberG = dot(right, float3(0.22f, 0.72f, 0.06f));
    float blue = dot(left, float3(0.06f, 0.20f, 0.74f));
    return saturate(float3(amberR, amberG, blue));
}

float3 ApplyFilterEmulatorRgbReduction(float3 color)
{
    float3 balance = float3(
        dot(color, float3(1.0f, -1.0f, -1.0f)),
        dot(color, float3(-1.0f, 1.0f, -1.0f)),
        dot(color, float3(-1.0f, -1.0f, 1.0f)));
    color.r *= lerp(1.0f, lerp(1.0f, 0.5f, smoothstep(-0.250f, 0.0f, balance.r)), saturate(filter_emulator_reduce_r));
    color.g *= lerp(1.0f, lerp(1.0f, 0.5f, smoothstep(-0.375f, 0.0f, balance.g)), saturate(filter_emulator_reduce_g));
    color.b *= lerp(1.0f, lerp(1.0f, 0.5f, smoothstep(-0.500f, 0.0f, balance.b)), saturate(filter_emulator_reduce_b));
    return saturate(color);
}

float3 ComposeFilterEmulatorAnaglyphColor(float3 left, float3 right, float pair)
{
    pair = floor(pair + 0.5f);
    float3 color;
    if (pair < 0.5f) {
        color = left * float3(1.0f, 0.0f, 1.0f) + right * float3(0.0f, 1.0f, 0.0f);
        return ApplyFilterEmulatorRgbReduction(color);
    }

    if (pair < 1.5f) {
        color = float3(left.r, ImageFilterLuma(right), left.b);
        return ApplyFilterEmulatorRgbReduction(color);
    }

    float leftLuma = ImageFilterLuma(left);
    float rightLuma = ImageFilterLuma(right);
    color = float3(left.r + 0.35f * left.b, right.g + 0.25f * right.b, 0.5f * (leftLuma + rightLuma));
    return ApplyFilterEmulatorRgbReduction(color);
}

float3 ComposeRedBlueOptimizedAnaglyphColor(float3 left, float3 right)
{
    return saturate(float3(ImageFilterLuma(left), 0.0f, ImageFilterLuma(right)));
}

float3 ComposeRedGreenAnaglyphColor(float3 left, float3 right)
{
    return saturate(float3(left.r, right.g, 0.0f));
}

float3 ComposeMagentaCyanAnaglyphColor(float3 left, float3 right)
{
    float leftLuma = ImageFilterLuma(left);
    float rightLuma = ImageFilterLuma(right);
    return saturate(float3(left.r + left.b, right.g + right.b, 0.5f * (leftLuma + rightLuma)));
}

float3 ComposeLcdOptimizedRedCyanAnaglyphColor(float3 left, float3 right)
{
    float3 color;
    color.r = dot(left, float3(0.4561f, 0.500484f, 0.176381f)) + dot(right, float3(-0.0434706f, -0.0879388f, -0.00155529f));
    color.g = dot(left, float3(-0.400822f, -0.0378246f, -0.0157589f)) + dot(right, float3(0.378476f, 0.73364f, -0.0184503f));
    color.b = dot(left, float3(-0.0152161f, -0.0205971f, -0.00546856f)) + dot(right, float3(-0.0721527f, -0.112961f, 1.2264f));
    return pow(saturate(color), float3(0.625f, 1.25f, 1.0f));
}

float3 ComposeGreenMagentaTriochromeAnaglyphColor(float3 left, float3 right)
{
    float lOne = 0.45f;
    float rOne = 0.8f;
    float deghost = 0.275f;
    float3 image;
    float3 accum = saturate(right * float3(rOne, 1.0f - rOne, 0.0f));
    image.r = pow(dot(accum, float3(1.0f, 1.0f, 1.0f)), 1.15f);
    accum = saturate(left * float3((1.0f - lOne) * 0.5f, lOne, (1.0f - lOne) * 0.5f));
    image.g = pow(dot(accum, float3(1.0f, 1.0f, 1.0f)), 1.05f);
    accum = saturate(right * float3(0.0f, 1.0f - rOne, rOne));
    image.b = pow(dot(accum, float3(1.0f, 1.0f, 1.0f)), 1.15f);

    float3 base = image;
    image.r = base.r + (base.r * (deghost * 0.5f)) + (base.g * (deghost * -0.25f)) + (base.b * (deghost * -0.25f));
    image.g = base.g + (base.r * (deghost * -0.5f)) + (base.g * (deghost * 0.25f)) + (base.b * (deghost * -0.5f));
    image.b = base.b + (base.r * (deghost * -0.25f)) + (base.g * (deghost * -0.25f)) + (base.b * (deghost * 0.5f));
    return saturate(image);
}

float3 ComposeBlueAmberColorCodeAnaglyphColor(float3 left, float3 right)
{
    float lOne = 0.45f;
    float rOne = 1.0f;
    float deghost = 0.275f;
    float3 image;
    float3 accum = saturate(left * float3(rOne, 0.0f, 1.0f - rOne));
    image.r = pow(dot(accum, float3(1.0f, 1.0f, 1.0f)), 1.05f);
    accum = saturate(left * float3(0.0f, rOne, 1.0f - rOne));
    image.g = pow(dot(accum, float3(1.0f, 1.0f, 1.0f)), 1.10f);
    accum = saturate(right * float3((1.0f - lOne) * 0.5f, (1.0f - lOne) * 0.5f, lOne));
    image.b = pow(dot(accum, float3(1.0f, 1.0f, 1.0f)), 1.0f);
    image.b = lerp(pow(image.b, (deghost * 0.15f) + 1.0f), 1.0f - pow(abs(1.0f - image.b), (deghost * 0.15f) + 1.0f), image.b);

    float3 base = image;
    image.r = base.r + (base.r * (deghost * 1.5f)) + (base.g * (deghost * -0.75f)) + (base.b * (deghost * -0.75f));
    image.g = base.g + (base.r * (deghost * -0.75f)) + (base.g * (deghost * 1.5f)) + (base.b * (deghost * -0.75f));
    image.b = base.b + (base.r * (deghost * -1.5f)) + (base.g * (deghost * -1.5f)) + (base.b * (deghost * 3.0f));
    return saturate(image);
}

float3 ComposeAnaglyphColor(float3 left, float3 right, float channelMode)
{
    float mode = floor(output_anaglyph_mode + 0.5f);
    float pair = floor(channelMode + 0.5f);
    if (mode >= 10.5f) {
        return ComposeRedGreenAnaglyphColor(left, right);
    }
    if (mode >= 9.5f) {
        return ComposeLcdOptimizedRedCyanAnaglyphColor(left, right);
    }
    if (mode >= 8.5f) {
        return ComposeBlueAmberColorCodeAnaglyphColor(left, right);
    }
    if (mode >= 7.5f) {
        return ComposeGreenMagentaTriochromeAnaglyphColor(left, right);
    }
    if (mode >= 6.5f) {
        return ComposeMagentaCyanAnaglyphColor(left, right);
    }
    if (mode >= 5.5f) {
        return ComposeRedBlueOptimizedAnaglyphColor(left, right);
    }
    if (mode >= 4.5f) {
        return ComposeFilterEmulatorAnaglyphColor(left, right, pair);
    }
    if (mode >= 3.5f) {
        return ComposeWarmCoolAnaglyphColor(left, right, pair);
    }
    if (mode >= 2.5f) {
        return ComposeDeghostAnaglyphColor(left, right, pair);
    }
    if (mode >= 1.5f) {
        return ComposeOptimizedAnaglyphColor(left, right, pair);
    }

    if (mode >= 0.5f) {
        left = ImageFilterLuma(left).xxx;
        right = ImageFilterLuma(right).xxx;
    }
    return ComposeSimpleAnaglyphColor(left, right, pair);
}

uint FrameMarkerParity()
{
    uint parity = frame_index & 1u;
    if (output_interlace_swap > 0.5f) {
        parity = 1u - parity;
    }
    return parity;
}

float3 FrameMarkerLineColor(uint parity, float layoutMode, bool frameAlternate)
{
    if (frameAlternate) {
        return ((frame_index & 2u) == 0u)
            ? float3(1.0f, 0.0f, 1.0f)
            : float3(0.0f, 1.0f, 0.0f);
    }
    if ((layoutMode >= 0.5f && layoutMode < 1.5f) || layoutMode >= 3.0f) {
        return (parity == 0u) ? float3(0.0f, 0.0f, 1.0f) : float3(1.0f, 1.0f, 0.0f);
    }
    return (parity == 0u) ? float3(1.0f, 0.0f, 0.0f) : float3(0.0f, 1.0f, 1.0f);
}

float4 ApplyFrameMarker(uint outX, uint outY, uint outWidth, uint outHeight, float4 color, uint parity, float layoutMode)
{
#if DIBR_LEAN
    return color;
#endif
    float mode = floor(output_frame_marker_mode + 0.5f);
    if (mode < 0.5f) {
        return color;
    }

    uint rows = max(1u, (uint)round(max(output_frame_marker_thickness, 0.0001f) * (float)outHeight));
    rows = min(rows, outHeight);
    uint cols = max(1u, (uint)round(max(output_frame_marker_thickness, 0.0001f) * (float)outWidth));
    cols = min(cols, outWidth);

    if (mode >= 2.5f) {
        bool inCorner = outX < cols && outY < rows;
        if (!inCorner) {
            return color;
        }
        color.rgb = (parity == 0u) ? float3(0.0f, 0.0f, 0.0f) : float3(1.0f, 1.0f, 1.0f);
        color.a = max(color.a, 1.0f);
        return color;
    }

    bool bottomRows = outY >= outHeight - rows;
    bool topBottomMiddleRows = false;
    if (layoutMode >= 0.5f && layoutMode < 1.5f) {
        topBottomMiddleRows = outY >= srcHeight - rows && outY < srcHeight;
    }
    if (!bottomRows && !topBottomMiddleRows) {
        return color;
    }

    if (mode < 1.5f) {
        bool frameAlternate = floor(output_composition_mode + 0.5f) >= 8.5f;
        color.rgb = FrameMarkerLineColor(parity, layoutMode, frameAlternate);
    } else {
        float xNorm = ((float)outX + 0.5f) / max((float)outWidth, 1.0f);
        bool activeSegment = (parity == 0u) ? (xNorm <= 0.25f) : (xNorm <= 0.75f);
        color.rgb = activeSegment ? float3(0.0f, 0.0f, 1.0f) : float3(0.0f, 0.0f, 0.0f);
    }

    color.a = max(color.a, 1.0f);
    return color;
}

float AlignmentCrossMask(float2 markerUv)
{
    float thickness = max(output_alignment_marker_thickness, 0.0001f);
    float feather = max(1.0f / max(min((float)srcWidth, (float)srcHeight), 1.0f), thickness * 0.5f);
    float2 centered = abs(markerUv - float2(0.5f, 0.5f));
    float lineDistance = min(centered.x, centered.y);
    float inside = step(0.0f, markerUv.x) * step(markerUv.x, 1.0f) * step(0.0f, markerUv.y) * step(markerUv.y, 1.0f);
    return (1.0f - smoothstep(thickness, thickness + feather, lineDistance)) * inside;
}

float4 ApplyAlignmentMarker(float2 outputUv, float2 sampleUv, float4 color)
{
#if DIBR_LEAN
    return color;
#endif
    float mode = floor(output_alignment_marker_mode + 0.5f);
    if (mode < 0.5f)
    {
        return color;
    }

    if (mode == 1.0f || mode >= 2.5f)
    {
        float imageMask = AlignmentCrossMask(sampleUv);
        color.rgb = lerp(color.rgb, float3(1.0f, 1.0f, 0.0f), imageMask);
        color.a = max(color.a, imageMask);
    }

    if (mode >= 1.5f)
    {
        float lensMask = AlignmentCrossMask(outputUv);
        color.rgb = lerp(color.rgb, float3(0.0f, 1.0f, 0.0f), lensMask);
        color.a = max(color.a, lensMask);
    }
    return color;
}

float2 InterlaceGridCoord(uint x, uint y)
{
    float2 nativeSize = max(float2((float)srcWidth, (float)srcHeight), float2(1.0f, 1.0f));
    float2 uv = (float2((float)x, (float)y) + float2(0.5f, 0.5f)) / nativeSize;
    float mode = floor(output_interlace_scale_mode + 0.5f);
    if (mode < 0.5f) {
        return floor(float2((float)x, (float)y));
    }
    if (mode < 1.5f) {
        return floor(uv * float2(3840.0f, 2160.0f));
    }
    if (mode < 2.5f) {
        return floor(uv * float2(1920.0f * 0.5f, 1080.0f * 0.5f));
    }
    if (mode < 3.5f) {
        return floor(uv * float2(1921.0f * 0.5f, 1081.0f * 0.5f));
    }
    if (mode < 4.5f) {
        return floor(uv * float2(1680.0f * 0.5f, 1050.0f * 0.5f));
    }
    if (mode < 5.5f) {
        return floor(uv * float2(1681.0f * 0.5f, 1051.0f * 0.5f));
    }
    if (mode < 6.5f) {
        return floor(uv * float2(1280.0f * 0.5f, 720.0f * 0.5f));
    }
    return floor(uv * float2(1281.0f * 0.5f, 721.0f * 0.5f));
}

float2 InterlaceSampleOffset(float eyeSign)
{
#if DIBR_LEAN
    return float2(0.0f, 0.0f);
#endif
    float mode = floor(output_composition_mode + 0.5f);
    float offset = max(output_interlace_sample_offset, 0.0f);
    if (mode >= 5.5f && mode < 6.5f) {
        return float2(0.0f, eyeSign * offset / max((float)srcHeight, 1.0f));
    }
    if (mode >= 6.5f && mode < 7.5f) {
        return float2(eyeSign * offset / max((float)srcWidth, 1.0f), 0.0f);
    }
    return float2(0.0f, 0.0f);
}

float2 RotateOutputKeystonePoint(float2 value, float2 center, float angle)
{
    float s;
    float c;
    sincos(angle, s, c);
    float2 d = value - center;
    return float2(d.x * c - d.y * s, d.x * s + d.y * c) + center;
}

float SafeOutputKeystoneDivide(float numerator, float denominator)
{
    float safeDenominator = denominator;
    if (abs(safeDenominator) < 0.00001f)
    {
        safeDenominator = safeDenominator < 0.0f ? -0.00001f : 0.00001f;
    }
    return numerator / safeDenominator;
}

float2 OutputKeystonePlane(float2 tc, float2 center, float tiltX, float tiltY)
{
    float2 direction = float2(tc.x, 0.0f);
    float2 alpha0 = float2(0.0f, -1.0f);
    float2 beta0 = direction - alpha0;
    float2 gamma0 = RotateOutputKeystonePoint(float2(-1.0f, 0.0f), float2(center.x, 0.0f), tiltY);
    float2 delta0 = RotateOutputKeystonePoint(float2(1.0f, 0.0f), float2(center.x, 0.0f), tiltY) - gamma0;
    float ip = SafeOutputKeystoneDivide(
        ((gamma0.y + 1.0f) * delta0.x) - (gamma0.x * delta0.y),
        (delta0.x * beta0.y) - (delta0.y * beta0.x));
    float xPlane = ip * beta0.x;
    float yPlane = ip * tc.y;

    direction = float2(yPlane, 0.0f);
    float2 beta1 = direction - alpha0;
    float2 gamma1 = RotateOutputKeystonePoint(float2(-1.0f, 0.0f), float2(center.y, 0.0f), tiltX);
    float2 delta1 = RotateOutputKeystonePoint(float2(1.0f, 0.0f), float2(center.y, 0.0f), tiltX) - gamma1;
    float v = SafeOutputKeystoneDivide(
        ((gamma1.y + 1.0f) * delta1.x) - (gamma1.x * delta1.y),
        (delta1.x * beta1.y) - (delta1.y * beta1.x));
    return float2(v * xPlane, v * beta1.x);
}

float2 ApplyOutputEyeKeystone(float2 uv, float eyeSign)
{
    float tilt = clamp(output_geometry_keystone_tilt, -0.5f, 0.5f);
    if (abs(tilt) < 0.00001f)
    {
        return uv;
    }
    float anchorX = tilt > 0.0f ? -eyeSign : eyeSign;
    float signedTiltY = eyeSign > 0.0f ? tilt : -tilt;
    return (OutputKeystonePlane((uv * 2.0f) - 1.0f, float2(anchorX, 0.0f), 0.0f, signedTiltY) * 0.5f) + 0.5f;
}

float2 OutputEyeAlignmentOffset(float eyeSign)
{
    float2 pixelOffset = OutputHeadsetPixelOffset(eyeSign);
    if (output_geometry_lens_dependent_ipd <= 0.5f) {
        pixelOffset.x += -eyeSign * OutputHeadsetIpdOffset();
    }
    return pixelOffset / max(float2((float)srcWidth, (float)srcHeight), float2(1.0f, 1.0f));
}

float2 OutputLensDependentIpdOffset(float eyeSign)
{
    if (output_geometry_lens_dependent_ipd <= 0.5f) {
        return float2(0.0f, 0.0f);
    }
    return float2(-eyeSign * OutputHeadsetIpdOffset(), 0.0f) / max(float2((float)srcWidth, (float)srcHeight), float2(1.0f, 1.0f));
}

float2 RotateOutputEyeUv(float2 uv, float degreesValue)
{
    float angle = radians(degreesValue);
    float s;
    float c;
    sincos(angle, s, c);
    float2 d = uv - float2(0.5f, 0.5f);
    return float2(d.x * c - d.y * s, d.x * s + d.y * c) + float2(0.5f, 0.5f);
}

float2 ApplyOutputEyeAlignment(float2 uv, float eyeSign)
{
#if DIBR_LEAN
    return uv;
#endif
    float degreesValue = OutputHeadsetRotation(eyeSign);
    float2 alignedUv = ApplyOutputEyeKeystone(uv + OutputLensDependentIpdOffset(eyeSign), eyeSign);
    return RotateOutputEyeUv(alignedUv + OutputEyeAlignmentOffset(eyeSign), degreesValue);
}

void ApplyStereoComposition(uint x, uint y, inout float4 leftColor, inout float4 rightColor)
{
#if DIBR_LEAN
    return;
#endif
    float mode = floor(output_composition_mode + 0.5f);
    if (mode < 0.5f) {
        return;
    }

    float4 composed = leftColor;
    if (mode < 1.5f) {
        composed = leftColor;
    } else if (mode < 2.5f) {
        composed = rightColor;
    } else if (mode < 5.5f) {
        float3 left = PrepareCompositionColor(leftColor.rgb, max(output_anaglyph_left_contrast, 0.0f));
        float3 right = PrepareCompositionColor(rightColor.rgb, max(output_anaglyph_right_contrast, 0.0f));
        composed = float4(ComposeAnaglyphColor(left, right, mode - 3.0f), max(leftColor.a, rightColor.a));
    } else if (mode < 8.5f) {
        float2 grid = InterlaceGridCoord(x, y);
        uint gridX = (uint)grid.x;
        uint gridY = (uint)grid.y;
        uint parity = (mode < 6.5f) ? (gridY & 1u) : ((mode < 7.5f) ? (gridX & 1u) : ((gridX + gridY) & 1u));
        if (output_interlace_swap > 0.5f) {
            parity = 1u - parity;
        }
        float4 primary = (parity == 0u) ? leftColor : rightColor;
        float4 secondary = (parity == 0u) ? rightColor : leftColor;
        composed = lerp(primary, (primary + secondary) * 0.5f, saturate(output_interlace_blend));
    } else {
        uint parity = frame_index & 1u;
        if (output_interlace_swap > 0.5f) {
            parity = 1u - parity;
        }
        composed = (parity == 0u) ? leftColor : rightColor;
    }

    leftColor = composed;
    rightColor = composed;
}

void WriteStereoViews(uint x, uint y, float4 leftFullColor, float4 leftReducedColor, float4 rightReducedColor, float4 rightFullColor)
{
    float4 leftColor = leftFullColor;
    float4 rightColor = rightFullColor;
    ApplyStereoComposition(x, y, leftColor, rightColor);
    float compositionMode = floor(output_composition_mode + 0.5f);
    if (compositionMode >= 0.5f) {
        leftFullColor = leftColor;
        leftReducedColor = leftColor;
        rightReducedColor = rightColor;
        rightFullColor = rightColor;
    }

    float4 firstColor = (output_eye_swap > 0.5f) ? rightFullColor : leftFullColor;
    float4 firstReducedColor = (output_eye_swap > 0.5f) ? rightReducedColor : leftReducedColor;
    float4 secondReducedColor = (output_eye_swap > 0.5f) ? leftReducedColor : rightReducedColor;
    float4 secondColor = (output_eye_swap > 0.5f) ? leftFullColor : rightFullColor;
    float layoutMode = floor(output_layout_mode + 0.5f);
    uint markerParity = FrameMarkerParity();
    // Output addressing uses out_width/out_height (== srcWidth/srcHeight
    // unless the overscan-grown render target widened the source).
    if (layoutMode >= 4.0f) {
        g_sbsOut[uint2(x, y)] = ApplyFrameMarker(x, y, out_width, out_height, firstColor, markerParity, layoutMode);
        return;
    }
    if (layoutMode >= 3.0f) {
        uint gapRows = max(1u, (uint)round((float)out_height * 0.08510638f));
        uint outWidth = out_width;
        uint outHeight = out_height * 2u + gapRows;
        g_sbsOut[uint2(x, y)] = ApplyFrameMarker(x, y, outWidth, outHeight, firstColor, markerParity, layoutMode);
        g_sbsOut[uint2(x, y + out_height + gapRows)] = ApplyFrameMarker(x, y + out_height + gapRows, outWidth, outHeight, secondColor, markerParity, layoutMode);
        if (y < gapRows) {
            g_sbsOut[uint2(x, y + out_height)] = float4(0.0f, 0.0f, 0.0f, 1.0f);
        }
        return;
    }
    if (layoutMode >= 2.0f) {
        uint outWidth = out_width * 2u;
        uint outHeight = out_height * 2u;
        g_sbsOut[uint2(x, y)] = ApplyFrameMarker(x, y, outWidth, outHeight, firstColor, markerParity, layoutMode);
        g_sbsOut[uint2(x + out_width, y)] = ApplyFrameMarker(x + out_width, y, outWidth, outHeight, firstReducedColor, markerParity, layoutMode);
        g_sbsOut[uint2(x, y + out_height)] = ApplyFrameMarker(x, y + out_height, outWidth, outHeight, secondReducedColor, markerParity, layoutMode);
        g_sbsOut[uint2(x + out_width, y + out_height)] = ApplyFrameMarker(x + out_width, y + out_height, outWidth, outHeight, secondColor, markerParity, layoutMode);
        return;
    }
    if (layoutMode >= 1.0f) {
        uint outWidth = out_width;
        uint outHeight = out_height * 2u;
        g_sbsOut[uint2(x, y)] = ApplyFrameMarker(x, y, outWidth, outHeight, firstColor, markerParity, layoutMode);
        g_sbsOut[uint2(x, y + out_height)] = ApplyFrameMarker(x, y + out_height, outWidth, outHeight, secondColor, markerParity, layoutMode);
        return;
    }
    uint outWidth = out_width * 2u;
    uint outHeight = out_height;
    g_sbsOut[uint2(x, y)] = ApplyFrameMarker(x, y, outWidth, outHeight, firstColor, markerParity, layoutMode);
    g_sbsOut[uint2(x + out_width, y)] = ApplyFrameMarker(x + out_width, y, outWidth, outHeight, secondColor, markerParity, layoutMode);
}

void WriteStereoPair(uint x, uint y, float4 leftColor, float4 rightColor)
{
    WriteStereoViews(x, y, leftColor, lerp(leftColor, rightColor, 0.33333334f), lerp(leftColor, rightColor, 0.66666669f), rightColor);
}

float4 ApplyPresentationColor(float2 uv, float4 color)
{
#if DIBR_LEAN
    return color;
#endif
    float sat = max(output_saturation, 0.0f);
    float luma = ImageFilterLuma(color.rgb);
    color.rgb = saturate(lerp(float3(luma, luma, luma), color.rgb, sat));

    float strength = saturate(output_vignette_strength);
    if (strength > 0.0f) {
        float2 d = uv - 0.5f;
        d.x *= (float)srcWidth / max((float)srcHeight, 1.0f);
        float dist = length(d);
        float radius = max(output_vignette_radius, 0.0f);
        float feather = max(output_vignette_feather, 0.0001f);
        float mask = smoothstep(radius, radius + feather, dist) * strength;
        color.rgb = lerp(color.rgb, float3(0.0f, 0.0f, 0.0f), mask);
    }
    float hmdVignette = clamp(output_hmd_vignette, 0.0f, 10.0f);
    if (hmdVignette > 0.0f) {
        float2 shapedUv = saturate(-uv * uv + uv);
        float hmdMask = saturate(shapedUv.x * shapedUv.y * pow(max(12.5f - hmdVignette, 0.0f), 3.0f));
        color.rgb *= hmdMask;
    }
    return color;
}

float2 ApplyOutputGeometry(float2 uv)
{
#if DIBR_LEAN
    return saturate(uv);
#endif
    float2 d = uv - 0.5f;
    if (output_geometry_axis_swap > 0.5f) {
        d = d.yx;
    }

    float2 scale = OutputHeadsetScale();
    float zoom = OutputHeadsetZoom();
    d /= scale * zoom;
    d.x *= max(1.0f - clamp(OutputHeadsetFov(), 0.0f, 0.25f), 0.0001f);

    float aspect = (float)srcWidth / max((float)srcHeight, 1.0f);
    float2 aspectD = float2(d.x * aspect, d.y);
    float r2 = dot(aspectD, aspectD);
    float r4 = r2 * r2;
    float r6 = r4 * r2;
    float barrel = output_geometry_barrel;
    float radialK2 = output_geometry_radial_k2;
    float radialK3 = output_geometry_radial_k3;
    if (abs(barrel) > 0.00001f || abs(radialK2) > 0.00001f || abs(radialK3) > 0.00001f) {
        d *= max(0.0f, 1.0f + barrel * r2 + radialK2 * r4 + radialK3 * r6);
    }

    return saturate(float2(0.5f + output_geometry_offset_x, 0.5f + output_geometry_offset_y) + d);
}

float2 StereoShift(float shift)
{
    return (stereo_axis_mode > 0.5f) ? float2(0.0f, shift) : float2(shift, 0.0f);
}

float UiAlphaMaskFromColor(float4 sampleColor)
{
    float strength = saturate(ui_alpha_mask_strength);
    if (strength <= 0.0f) {
        return 0.0f;
    }

    float threshold = saturate(ui_alpha_mask_threshold);
    float feather = max(ui_alpha_mask_feather, 0.0001f);
    return smoothstep(threshold, min(threshold + feather, 1.0f), sampleColor.a) * strength;
}

float UiAutoMaskFromColor(float4 sampleColor)
{
    float strength = saturate(ui_auto_mask_strength);
    if (strength <= 0.0f) {
        return 0.0f;
    }

    float luma = dot(sampleColor.rgb, float3(0.2126f, 0.7152f, 0.0722f));
    float threshold = saturate(ui_auto_mask_threshold);
    float feather = max(ui_auto_mask_feather, 0.0001f);
    float brightMask = smoothstep(threshold, min(threshold + feather, 1.0f), luma);
    float darkMask = 1.0f - smoothstep(max(threshold - feather, 0.0f), threshold, luma);
    float mode = floor(ui_auto_mask_mode + 0.5f);
    float mask = (mode < 0.5f) ? brightMask : ((mode < 1.5f) ? darkMask : max(brightMask, darkMask));
    return saturate(mask * strength);
}

float UiAlphaMask(float2 uv)
{
    float4 sampleColor = g_colorTex.SampleLevel(g_linearSampler, saturate(uv), 0);
    return saturate(max(UiAlphaMaskFromColor(sampleColor), UiAutoMaskFromColor(sampleColor)));
}

float ApplyUiAlphaDepthMask(float2 uv, float depth)
{
    float4 sampleColor = g_colorTex.SampleLevel(g_linearSampler, saturate(uv), 0);
    float alphaMask = UiAlphaMaskFromColor(sampleColor);
    float autoMask = UiAutoMaskFromColor(sampleColor);
    depth = lerp(depth, saturate(ui_alpha_mask_target_depth), alphaMask);
    return lerp(depth, saturate(ui_auto_mask_target_depth), autoMask);
}

float DebugDepthValue(float depth)
{
    float nearValue = saturate(debug_view_near);
    float farValue = max(saturate(debug_view_far), nearValue + 0.0001f);
    return saturate(((depth - nearValue) / (farValue - nearValue)) * max(debug_view_scale, 0.0f));
}

float3 DebugHeat(float v)
{
    return saturate(float3(v * 2.0f - 0.5f, 1.0f - abs(v * 2.0f - 1.0f), 1.5f - v * 2.0f));
}

float3 PackDepthValue24(float depth)
{
    float depthValue = saturate(depth) * (256.0f * 256.0f * 256.0f - 1.0f) / (256.0f * 256.0f * 256.0f);
    float3 encode = frac(depthValue * float3(1.0f, 256.0f, 256.0f * 256.0f));
    encode.xy -= encode.yz / 256.0f;
    return encode;
}

float4 DibrDebugColor(float2 uv, float depth, float shift)
{
    float mode = floor(debug_view_mode + 0.5f);
    float depthValue = DebugDepthValue(depth);
    if (mode < 1.5f) {
        return float4(depthValue, depthValue, depthValue, 1.0f);
    }
    if (mode < 2.5f) {
        float disparity = saturate(abs(shift) * (float)srcWidth / max(abs(divergence), 1.0f) * max(debug_view_scale, 0.0f));
        return float4(DebugHeat(disparity), 1.0f);
    }

    float mask = saturate(LetterboxMask(uv) + RegionDepthMask(uv, depth) + WeaponDepthMask(uv, depth) + AutoWeaponDepthMask(uv, depth) + ShapeDepthMask(uv, depth) + OutputMatteMask(uv) + UiAlphaMask(uv));
    return float4(mask, depthValue * (1.0f - mask), 1.0f - mask, 1.0f);
}

float4 DibrAlignmentGridColor(float2 uv, float depth, float mode)
{
    float4 baseColor = SampleFilteredOutputColor(uv);
    float2 gridUv = frac(uv * 16.0f);
    float gridDist = min(min(gridUv.x, 1.0f - gridUv.x), min(gridUv.y, 1.0f - gridUv.y));
    float gridLine = 1.0f - smoothstep(0.0f, 0.012f, gridDist);
    float centerX = 1.0f - smoothstep(0.0f, 2.0f / max((float)srcWidth, 1.0f), abs(uv.x - 0.5f));
    float centerY = 1.0f - smoothstep(0.0f, 2.0f / max((float)srcHeight, 1.0f), abs(uv.y - 0.5f));
    float border = step(uv.x, 0.002f) + step(uv.y, 0.002f) + step(0.998f, uv.x) + step(0.998f, uv.y);

    float3 gridColor = (mode < 6.5f) ? float3(0.0f, 1.0f, 0.0f) : DebugHeat(DebugDepthValue(depth));
    float3 color = lerp(baseColor.rgb, gridColor, saturate(gridLine * 0.55f + border));
    color = lerp(color, float3(1.0f, 0.0f, 0.0f), centerX);
    color = lerp(color, float3(0.0f, 0.35f, 1.0f), centerY);
    return float4(saturate(color), baseColor.a);
}

float LinearizeProjectionDepth(float depth)
{
    float strength = saturate(depth_linearize_strength);
    if (strength <= 0.0f) {
        return depth;
    }

    float nearZ = max(depth_linearize_near, 0.0001f);
    float farZ = max(depth_linearize_far, nearZ + 0.0001f);
    float d = saturate(depth);
    float reversedMode = step(0.5f, floor(depth_linearize_mode + 0.5f));
    float standardDenom = farZ - d * (farZ - nearZ);
    float reversedDenom = nearZ + d * (farZ - nearZ);
    float denom = max(lerp(standardDenom, reversedDenom, reversedMode), 0.0001f);
    float eyeZ = (nearZ * farZ) / denom;
    float linearDepth = saturate((eyeZ - nearZ) / (farZ - nearZ));
    return lerp(depth, linearDepth, strength);
}

float DepthDitherNoise(float2 uv)
{
    float2 pixel = uv * float2((float)srcWidth, (float)srcHeight);
    return frac(sin(dot(pixel, float2(12.9898f, 78.233f))) * 43758.5453f);
}

float ApplyDepthDither(float2 uv, float depth)
{
    float strength = saturate(depth_dither_strength);
    if (strength <= 0.0f) {
        return depth;
    }
    float bits = clamp(depth_dither_bits, 1.0f, 15.0f);
    float stepSize = 1.0f / max(pow(2.0f, bits) - 1.0f, 1.0f);
    float noise = DepthDitherNoise(uv) - 0.5f;
    return saturate(depth + noise * stepSize * strength);
}

float ApplyDepthRangeBoost(float depth)
{
    float strength = saturate(depth_range_boost_strength);
    if (strength <= 0.0f) {
        return depth;
    }

    float center = saturate(depth_range_boost_center);
    float width = max(depth_range_boost_width, 0.0001f);
    float band = 1.0f - smoothstep(width, width * 2.0f, abs(saturate(depth) - center));
    float scale = lerp(1.0f, max(depth_range_boost_scale, 1.0f), band * strength);
    float focus = EffectiveConvergence();
    return saturate(focus + (depth - focus) * scale);
}

float SamplePreparedDepthBase(float2 uv)
{
    float depth = SampleDepthTexture(TransformDepthUv(uv));
    if (reverse_depth > 0.5f) {
        depth = 1.0f - depth;
    }
    if (depth_value_flip > 0.5f) {
        depth = 1.0f - depth;
    }
    depth = LinearizeProjectionDepth(depth);
    depth = ApplyDepthDither(uv, depth);
    return saturate(ApplyLetterboxDepthMask(uv, depth));
}

float ApplyDepthEdgeMask(float2 uv, float depth)
{
    float strength = clamp(depth_edge_mask_strength, -1.0f, 1.0f);
    if (abs(strength) <= 0.0f) {
        return depth;
    }

    float radius = max(depth_edge_mask_radius, 0.0f);
    float2 texel = float2(1.0f / max((float)srcWidth, 1.0f), 1.0f / max((float)srcHeight, 1.0f)) * radius;
    float dl = SamplePreparedDepthBase(uv - float2(texel.x, 0.0f));
    float dr = SamplePreparedDepthBase(uv + float2(texel.x, 0.0f));
    float du = SamplePreparedDepthBase(uv - float2(0.0f, texel.y));
    float dd = SamplePreparedDepthBase(uv + float2(0.0f, texel.y));
    float neighborAvg = (dl + dr + du + dd) * 0.25f;
    float gradient = max(max(abs(depth - dl), abs(depth - dr)), max(abs(depth - du), abs(depth - dd)));
    gradient = max(gradient, max(abs(dr - dl), abs(dd - du)));
    float edgeMask = smoothstep(max(depth_edge_mask_threshold, 0.0f), max(depth_edge_mask_threshold, 0.0f) + max(depth_edge_mask_feather, 0.0001f), gradient);
    float target = (strength >= 0.0f) ? neighborAvg : 1.0f;
    return saturate(lerp(depth, target, edgeMask * abs(strength)));
}

float SamplePreparedDepth(float2 uv)
{
    return ApplyDepthEdgeMask(uv, SamplePreparedDepthBase(uv));
}

float ExpandDepth(float2 uv, float depth)
{
    float strength = saturate(depth_expand_strength);
    if (strength <= 0.0f) {
        return depth;
    }

    float radius = max(depth_expand_radius, 0.0f);
    float2 texel = float2(1.0f / max((float)srcWidth, 1.0f), 1.0f / max((float)srcHeight, 1.0f)) * radius;
    float dl = SamplePreparedDepth(uv - float2(texel.x, 0.0f));
    float dr = SamplePreparedDepth(uv + float2(texel.x, 0.0f));
    float du = SamplePreparedDepth(uv - float2(0.0f, texel.y));
    float dd = SamplePreparedDepth(uv + float2(0.0f, texel.y));
    float dlu = SamplePreparedDepth(uv - texel);
    float dru = SamplePreparedDepth(uv + float2(texel.x, -texel.y));
    float dld = SamplePreparedDepth(uv + float2(-texel.x, texel.y));
    float drd = SamplePreparedDepth(uv + texel);

    float localMin = min(depth, min(min(dl, dr), min(min(du, dd), min(min(dlu, dru), min(dld, drd)))));
    float avg = (dl + dr + du + dd + dlu + dru + dld + drd) * 0.125f;
    float gradient = max(abs(dr - dl), abs(dd - du));
    float threshold = max(depth_expand_edge_threshold, 0.0001f);
    float edgeMask = smoothstep(threshold, threshold * 2.0f, gradient);
    float target = lerp(avg, localMin, saturate(depth_expand_near_bias));
    return saturate(lerp(depth, target, strength * edgeMask));
}

float ReconstructDepth(float2 uv, float depth)
{
    float strength = saturate(depth_reconstruct_strength);
    if (strength <= 0.0f) {
        return depth;
    }

    float radius = max(depth_reconstruct_radius, 0.0f);
    float2 texel = float2(1.0f / max((float)srcWidth, 1.0f), 1.0f / max((float)srcHeight, 1.0f)) * radius;
    float dl = SamplePreparedDepth(uv - float2(texel.x, 0.0f));
    float dr = SamplePreparedDepth(uv + float2(texel.x, 0.0f));
    float du = SamplePreparedDepth(uv - float2(0.0f, texel.y));
    float dd = SamplePreparedDepth(uv + float2(0.0f, texel.y));
    float dlu = SamplePreparedDepth(uv - texel);
    float dru = SamplePreparedDepth(uv + float2(texel.x, -texel.y));
    float dld = SamplePreparedDepth(uv + float2(-texel.x, texel.y));
    float drd = SamplePreparedDepth(uv + texel);

    float localMin = min(depth, min(min(dl, dr), min(min(du, dd), min(min(dlu, dru), min(dld, drd)))));
    float avg = (dl + dr + du + dd + dlu + dru + dld + drd) * 0.125f;
    float gradient = max(abs(dr - dl), abs(dd - du));
    float threshold = max(depth_reconstruct_edge_threshold, 0.0001f);
    float edgeMask = smoothstep(threshold, threshold * 2.0f, gradient);
    float target = lerp(avg, localMin, saturate(depth_reconstruct_near_bias) * edgeMask);
    return saturate(lerp(depth, target, strength));
}

float ScreenEdgeGuard(float2 uv, float depth)
{
    float strength = saturate(edge_guard_strength);
    if (strength <= 0.0f) {
        return 1.0f;
    }

    float axisCoord = (stereo_axis_mode > 0.5f) ? uv.y : uv.x;
    float edgeDist = min(saturate(axisCoord), 1.0f - saturate(axisCoord));
    float edgeMask = saturate(1.0f - edgeDist / max(edge_guard_width, 0.0001f));
    edgeMask = pow(edgeMask, max(edge_guard_shape, 0.01f));

    float nearRange = edge_guard_near_depth;
    float depthMask = (nearRange > 0.0f)
        ? saturate((EffectiveConvergence() - depth) / max(nearRange, 0.0001f))
        : 1.0f;
    return 1.0f - edgeMask * depthMask * strength;
}

float ConvergenceBoundaryScale(float2 uv, float depth)
{
    float strength = saturate(convergence_boundary_strength);
    if (strength <= 0.0f) {
        return 1.0f;
    }

    float2 texel = float2(1.0f / max((float)srcWidth, 1.0f), 1.0f / max((float)srcHeight, 1.0f));
    float dl = SamplePreparedDepth(uv - float2(texel.x, 0.0f));
    float dr = SamplePreparedDepth(uv + float2(texel.x, 0.0f));
    float du = SamplePreparedDepth(uv - float2(0.0f, texel.y));
    float dd = SamplePreparedDepth(uv + float2(0.0f, texel.y));
    float gradient = max(abs(dr - dl), abs(dd - du));
    float threshold = max(convergence_boundary_threshold, 0.0f);
    float feather = max(convergence_boundary_feather, 0.0001f);
    float mask = smoothstep(threshold, threshold + feather, gradient) * strength;
    return lerp(1.0f, saturate(convergence_boundary_scale), mask);
}

float DepthArtifactGuardScale(float2 uv, float depth)
{
    float strength = saturate(depth_artifact_guard_strength);
    if (strength <= 0.0f) {
        return 1.0f;
    }

    float2 texel = float2(1.0f / max((float)srcWidth, 1.0f), 1.0f / max((float)srcHeight, 1.0f));
    float dl = SamplePreparedDepth(uv - float2(texel.x, 0.0f));
    float dr = SamplePreparedDepth(uv + float2(texel.x, 0.0f));
    float du = SamplePreparedDepth(uv - float2(0.0f, texel.y));
    float dd = SamplePreparedDepth(uv + float2(0.0f, texel.y));
    float gradient = max(abs(dr - dl), abs(dd - du));
    float threshold = max(depth_artifact_guard_threshold, 0.0f);
    float feather = max(depth_artifact_guard_feather, 0.0001f);
    float mask = smoothstep(threshold, threshold + feather, gradient) * strength;
    return lerp(1.0f, saturate(depth_artifact_guard_scale), mask);
}

// ---- Synthesized-eye parallax search (ported from dibr_raymarch.hlsl) ----
// The legacy single-tap warp (sample at uv - shift(depth_at_destination))
// tears thin features and depth-less translucents apart at depth
// discontinuities: the offset jumps mid-feature wherever the background
// depth changes. Searching the depth field along the disparity ray for the
// surface that actually lands on this output pixel resolves the occlusion
// the same way the raymarch kernel does, while the reference eye stays a
// pristine passthrough.

#define YORO_MAX_SEARCH_STEPS 64

// The conditioned search depth (5-tap smooth/protect + masks + range boost +
// filter emulator) is built once per source pixel by dibr_depth_prep.hlsl
// (PREP_MODE 2); every probe of the search reads it back as a single tap.
float YoroSearchDepth(float2 uv)
{
    return g_prepDepth.SampleLevel(g_linearSampler, saturate(uv), 0).x;
}

// Signed UV shift of the synthesized eye (full disparity - the reference eye
// is untouched, so the synthesized eye carries the whole baseline). Matches
// the legacy fullLeft/RightOffset math. The two depth-gradient guards
// (convergence boundary / artifact guard) are factored out into a
// boundaryScale computed once at the output pixel, so the search loop does
// not resample the depth gradient per probe - and the remaining mask-stack
// guards below are likewise hoisted to the output pixel (they vary slowly
// along the ray and are all 1.0 at default settings).
float YoroSynthGuard(float2 uv, float depth, float eyeSign)
{
    return FilterEmulatorFocusScale(depth) * ScreenEdgeGuard(uv, depth)
        * WeaponBoundaryScale(uv, depth) * FocusReductionScale(uv, depth, eyeSign);
}

float YoroSynthShiftGuarded(float depth, float guard, float eyeSign)
{
    float delta = StereoDepthDelta(depth);
    // Same near-disparity clamp as the raymarch kernel's DepthToUvShiftBase:
    // makes the Pop-out Limit slider effective in YORO and tames the warp on
    // very close geometry.
    float nearLimit = max(popout_limit, 0.0f);
    if (nearLimit < 1.0f && delta < 0.0f) {
        delta = max(delta, -nearLimit);
    }
    return eyeSign * 2.0f * (divergence * delta * guard + perspective_shift) * pre_inv_src_width;
}

int YoroSearchSteps(float2 uv, float requestedSteps)
{
    float steps = clamp(requestedSteps, 8.0f, (float)YORO_MAX_SEARCH_STEPS);
    float strength = saturate(raymarch_foveation_strength);
    if (strength <= 0.0f) {
        return (int)steps;
    }

    float radius = saturate(raymarch_foveation_radius);
    float edgeDistance = max(abs(uv.x - 0.5f), abs(uv.y - 0.5f)) * 2.0f;
    float mask = saturate((edgeDistance - radius) / max(1.0f - radius, 0.0001f));
    mask = pow(mask, max(raymarch_foveation_curve, 0.1f));

    float minSteps = min(clamp(raymarch_foveation_min_steps, 4.0f, (float)YORO_MAX_SEARCH_STEPS), steps);
    steps = lerp(steps, minSteps, mask * strength);
    return (int)clamp(round(steps), 4.0f, (float)YORO_MAX_SEARCH_STEPS);
}

// ---- True-matrix reprojection (R1 redesign) ----

// Maps a TARGET/reference-FOV uv into the (possibly overscanned) SOURCE
// image. The overscan widening scales the projection's M[0][0] and M[2][0]
// together, so ndc divides evenly: a pure x scale about ndc 0.
float2 SourceRemapUv(float2 uv)
{
    if (overscan_x > 1.0f) {
        uv.x = 0.5f + (uv.x - 0.5f) / overscan_x;
    }
    return uv;
}

float SampleRawDeviceDepth(float2 uv)
{
    return SampleDepthTexture(TransformDepthUv(saturate(uv)));
}

#if SCATTER_COMPOSE
float HybridLinearDepthFromRaw(float rawDepth)
{
    float depth = rawDepth;
    if (reverse_depth > 0.5f) {
        depth = 1.0f - depth;
    }
    if (depth_value_flip > 0.5f) {
        depth = 1.0f - depth;
    }
    return LinearizeProjectionDepth(saturate(depth));
}

float HybridTargetSelector()
{
    return round(near_field_target);
}

float4 HybridTargetRect()
{
    float selector = abs(HybridTargetSelector());
    float4 fallbackRect = selector < 1.5f
        ? float4(0.0f, 0.0f, 0.5f, 1.0f)
        : float4(0.5f, 0.0f, 1.0f, 1.0f);
    float4 rect = float4(
        hybrid_target_rect_min_x,
        hybrid_target_rect_min_y,
        hybrid_target_rect_max_x,
        hybrid_target_rect_max_y);
    bool rectValid =
        rect.z > rect.x + 0.0001f &&
        rect.w > rect.y + 0.0001f;
    return rectValid ? rect : fallbackRect;
}

float2 HybridTargetColorUv(float2 uv)
{
    float4 rect = HybridTargetRect();
    return lerp(rect.xy, rect.zw, saturate(uv));
}

float2 HybridTargetDepthUv(float2 uv)
{
    float selector = HybridTargetSelector();
    if (selector < 0.0f) {
        return TransformDepthUv(uv);
    }

    float4 rect = HybridTargetRect();
    return lerp(rect.xy, rect.zw, saturate(uv));
}

float HybridTargetHalfEdgeWeight(float2 uv)
{
    // Only fade the target pass at the output eye edges. The real target rect
    // may be narrower than the nominal half; fading in target-rect UV space
    // would erase useful native stereo pixels inside that valid rect.
    float edgeDistance = min(saturate(uv.x), 1.0f - saturate(uv.x));
    float overscanFringe = saturate(1.0f - 1.0f / max(overscan_x, 1.0f));
    float fadeWidth = max(overscanFringe * 0.5f, 0.006f);
    return smoothstep(0.0f, fadeWidth, edgeDistance);
}

float HybridTargetLinearDepth(float2 uv)
{
    float mode = floor(depth_sample_mode + 0.5f);
    float2 duv = saturate(HybridTargetDepthUv(saturate(uv)));
    float rawDepth = (mode >= 1.0f)
        ? g_depthTex.SampleLevel(g_pointSampler, duv, 0)
        : g_depthTex.SampleLevel(g_linearSampler, duv, 0);
    return HybridLinearDepthFromRaw(rawDepth);
}

float HybridTargetSignalWeight(float4 color)
{
    // A few SN2 passes leave real target-eye pixels as a literal black clear
    // where the depth still looks close. Do not let those clear pixels punch a
    // hole through the DIBR fallback; a real dark surface still has enough
    // nonzero signal/noise to pass this very low threshold.
    float3 c = abs(color.rgb);
    float peak = max(c.r, max(c.g, c.b));
    float luma = dot(c, float3(0.2126f, 0.7152f, 0.0722f));
    return smoothstep(0.0015f, 0.018f, max(peak, luma));
}

float HybridTargetNearWeight(float targetDepth, float sourceDepth)
{
    float split = saturate(near_field_start);
    float feather = max(near_field_end, 0.00001f);

    // The native target eye is the only proof that a near-field pixel really
    // survived the far clip. Source depth can say "a similar surface exists in
    // the reference eye", but it cannot distinguish a real target pixel from
    // the clipped clear layer that produced the vertical bands.
    return 1.0f - smoothstep(split, split + feather, targetDepth);
}

float HybridTargetCoverageWeight(float2 uv, float targetDepth, float sourceDepth, float4 targetColor)
{
    // In hybrid mode the target eye is deliberately far-clipped. Depth alone
    // has lied in this path, and color alone preserves clipped shadow/clear
    // pixels, so require both: the target pass must look close AND must have
    // actually drawn a non-clear pixel.
    return HybridTargetNearWeight(targetDepth, sourceDepth) *
        HybridTargetSignalWeight(targetColor) *
        HybridTargetHalfEdgeWeight(uv);
}

float4 ApplyHybridNearStereo(float2 uv, float sourceDepth, float4 synthColor)
{
    if (near_field_strength < 0.5f) {
        return synthColor;
    }

    float targetDepth = HybridTargetLinearDepth(uv);
    float2 targetUv = saturate(HybridTargetColorUv(uv));
    float4 realNear = g_hybridTargetColorTex.SampleLevel(g_linearSampler, targetUv, 0);
    float realNearWeight = HybridTargetCoverageWeight(uv, targetDepth, sourceDepth, realNear);
    float4 outColor = lerp(synthColor, realNear, realNearWeight);
    outColor.a = 1.0f;
    return outColor;
}
#endif

// Map a SOURCE-eye pixel + raw device depth (reversed-Z, as stored) to the
// synthesized TARGET eye's uv through the exact clip->clip matrix. The
// homogeneous unproject trick makes (ndc, deviceZ, 1) valid input for the
// combined P_dst * T * P_src^-1 matrix regardless of w.
float2 ReprojectSourceUv(float2 srcUv, float rawDepth, float eyeSign)
{
    float2 ndc = float2(srcUv.x * 2.0f - 1.0f, 1.0f - srcUv.y * 2.0f);
    float4 clip = float4(ndc, rawDepth, 1.0f);
    float4 t = (eyeSign > 0.0f) ? mul(reproj_source_to_left, clip) : mul(reproj_source_to_right, clip);
    float w = (abs(t.w) > 1e-6f) ? t.w : 1e-6f;
    float2 tNdc = t.xy / w;
    return float2(tNdc.x * 0.5f + 0.5f, 0.5f - tNdc.y * 0.5f);
}

// First-crossing search in exact-reprojection space: find the source pixel
// whose projection lands on this output pixel. Disparity is monotonic in
// depth, so candidate source offsets span the shifts implied by the nearest
// (device 1, reversed-Z) and farthest (device 0) depths; marching from the
// near-implied end keeps nearest-surface-wins occlusion.
// x component of ReprojectSourceUv with the scanline-invariant matrix terms
// hoisted: every probe of a horizontal search shares ndc.y, so the 4x4
// multiply collapses to two FMA pairs in ndc.x and depth (only t.x and t.w
// matter for the horizontal hit test).
float YoroReprojX(float srcUvX, float rawDepth, float3 coefX, float3 coefW)
{
    float ndcX = srcUvX * 2.0f - 1.0f;
    float tx = coefX.x * ndcX + coefX.y * rawDepth + coefX.z;
    float tw = coefW.x * ndcX + coefW.y * rawDepth + coefW.z;
    float w = (abs(tw) > 1e-6f) ? tw : 1e-6f;
    return (tx / w) * 0.5f + 0.5f;
}

float2 YoroMatrixSearchUv(float2 uv, float eyeSign)
{
    float4x4 M = (eyeSign > 0.0f) ? reproj_source_to_left : reproj_source_to_right;
    float ndcY = 1.0f - uv.y * 2.0f;
    float3 coefX = float3(M._m00, M._m02, M._m01 * ndcY + M._m03);
    float3 coefW = float3(M._m30, M._m32, M._m31 * ndcY + M._m33);

    float fwdNear = YoroReprojX(uv.x, 1.0f, coefX, coefW) - uv.x;
    float fwdFar = YoroReprojX(uv.x, 0.0f, coefX, coefW) - uv.x;

    // A source pixel at uv.x + s with forward shift fwd(depth) lands at
    // uv.x + s + fwd; landing on this pixel needs s = -fwd(depth).
    float sNearEnd = -fwdNear;
    float sFarEnd = -fwdFar;

    float spanPx = abs(sNearEnd - sFarEnd) * (float)srcWidth;
    if (spanPx <= 0.25f) {
        return float2(uv.x + sFarEnd, uv.y);
    }

    float requestedSteps = (raymarch_steps > 0.0f) ? raymarch_steps : 32.0f;
    int steps = YoroSearchSteps(uv, min(requestedSteps, max(spanPx, 8.0f)));

    float prevS = 0.0f;
    float prevF = 0.0f;
    bool havePrev = false;
    float hitS = sFarEnd;

    [loop]
    for (int i = 0; i <= steps; ++i) {
        float t = (float)i / (float)steps;
        float s = lerp(sNearEnd, sFarEnd, t);
        float2 probeUv = float2(uv.x + s, uv.y);
        float f = YoroReprojX(probeUv.x, SampleRawDeviceDepth(probeUv), coefX, coefW) - uv.x;

        if (havePrev && (f <= 0.0f) != (prevF <= 0.0f)) {
            // Bisect the bracket for sub-step precision.
            float lo = prevS;
            float hi = s;
            float fLo = prevF;
            [loop]
            for (int r = 0; r < 4; ++r) {
                float mid = 0.5f * (lo + hi);
                float2 midUv = float2(uv.x + mid, uv.y);
                float fMid = YoroReprojX(midUv.x, SampleRawDeviceDepth(midUv), coefX, coefW) - uv.x;
                if ((fMid <= 0.0f) == (fLo <= 0.0f)) {
                    lo = mid;
                    fLo = fMid;
                } else {
                    hi = mid;
                }
            }
            hitS = 0.5f * (lo + hi);
            break;
        }

        havePrev = true;
        prevS = s;
        prevF = f;
    }

    float2 srcUv = float2(uv.x + hitS, uv.y);

    // One vertical correction pass: asymmetric frusta produce a small y
    // disparity; line the found sample up on y as well.
    float dHit = SampleRawDeviceDepth(srcUv);
    float2 proj = ReprojectSourceUv(srcUv, dHit, eyeSign);
    srcUv.y += (uv.y - proj.y);
    return srcUv;
}

float2 YoroSearchUv(float2 uv, float eyeSign, float centerDepth, float boundaryScale, float shiftScale)
{
    if (reproj_enabled > 0.5f) {
        float2 srcUv = YoroMatrixSearchUv(uv, eyeSign);
        // Screen-edge guard, gradient guards and the reduced-disparity layout
        // compress the found offset toward the far-plane alignment.
        float guard = ScreenEdgeGuard(uv, centerDepth) * boundaryScale * shiftScale;
        float sFar = -(ReprojectSourceUv(uv, 0.0f, eyeSign).x - uv.x);
        srcUv.x = uv.x + sFar + (srcUv.x - uv.x - sFar) * guard;
        return srcUv;
    }

    // The mask-stack guard product is hoisted to the output pixel (it varies
    // slowly along the ray and every factor is 1.0 at default settings); each
    // probe is then one prep-texture tap plus a few ALU ops.
    float guard = YoroSynthGuard(uv, centerDepth, eyeSign);
    float outerScale = boundaryScale * shiftScale;
    float targetShift = YoroSynthShiftGuarded(centerDepth, guard, eyeSign) * outerScale;
    float absTarget = abs(targetShift);
    float2 directUv = uv + StereoShift(targetShift);
    if (absTarget <= (0.25f * pre_inv_src_width)) {
        return directUv;
    }

    float defaultSteps = clamp(abs(divergence) * 0.75f, 8.0f, (float)YORO_MAX_SEARCH_STEPS);
    float requestedSteps = (raymarch_steps > 0.0f) ? raymarch_steps : defaultSteps;
    int steps = YoroSearchSteps(uv, requestedSteps);
    float direction = (targetShift >= 0.0f) ? 1.0f : -1.0f;

    // March until the ray CROSSES the depth surface. f(t) = the travel the
    // probe's own depth asks for, minus the travel needed to land on this
    // output pixel; f starts positive and its FIRST sign change is the
    // nearest surface that projects onto this pixel. (A global best-error
    // scan instead picks later local minima and stamps repeated copies of
    // high-contrast edges across disocclusion bands - the "3 edges" look.)
    float prevT = 0.0f;
    float hitT = -1.0f;

    [loop]
    for (int i = 1; i <= steps; ++i) {
        float t = (float)i / (float)steps;
        float2 probeUv = saturate(uv + StereoShift(direction * absTarget * t));
        float probeDepth = YoroSearchDepth(probeUv);
        float f = abs(YoroSynthShiftGuarded(probeDepth, guard, eyeSign) * outerScale) - absTarget * t;

        if (f <= 0.0f) {
            // Bisect the bracket for sub-step precision (Depth3D-style).
            float lo = prevT;
            float hi = t;
            [loop]
            for (int r = 0; r < 4; ++r) {
                float mid = 0.5f * (lo + hi);
                float2 midUv = saturate(uv + StereoShift(direction * absTarget * mid));
                float fMid = abs(YoroSynthShiftGuarded(YoroSearchDepth(midUv), guard, eyeSign) * outerScale) - absTarget * mid;
                if (fMid <= 0.0f) {
                    hi = mid;
                } else {
                    lo = mid;
                }
            }
            hitT = hi;
            break;
        }

        prevT = t;
    }

    if (hitT < 0.0f) {
        // No surface crossed within range: the destination-depth estimate
        // stands (deterministic and temporally stable).
        return directUv;
    }

    return uv + StereoShift(direction * absTarget * hitT);
}

// Edge-fill behavior of SampleStereoColor plus the raymarch kernel's
// disocclusion guard: where the searched sample's depth disagrees with the
// output pixel's depth (a revealed region with no true source data), blend
// back toward the unwarped center color instead of smearing the occluder.
float4 SampleSynthStereoColor(float2 sampleUv, float2 centerUv, float4 centerColor, float centerDepth)
{
    float4 base = SampleStereoColor(sampleUv, centerUv, centerColor);
    float outside = (sampleUv.x < 0.0f || sampleUv.x > 1.0f ||
                     sampleUv.y < 0.0f || sampleUv.y > 1.0f) ? 1.0f : 0.0f;
    if (outside >= 0.5f) {
        return base;
    }

    float sampleDepth = YoroSearchDepth(sampleUv);
    // Two-sided, asymmetric test. Near side (sample NEARER than the output
    // pixel = foreground smeared into a revealed region) keeps the tight
    // threshold. Far side (sample much FARTHER = a large reveal being filled
    // by stretching whatever the ray hits, e.g. an animated wave crest
    // smeared into a rope-like band at strong silhouettes) needs a much
    // higher threshold: small far-deltas are normal on slanted surfaces, and
    // guarding them is exactly what used to blur the whole eye.
    float w = max(disocclusion_depth_weight, 0.0f);
    float threshold = max(disocclusion_threshold, 0.0f);
    float feather = max(disocclusion_feather, 0.0001f);
    float nearMask = smoothstep(threshold, threshold + feather, (centerDepth - sampleDepth) * w);
    float farMask = smoothstep(threshold * 4.0f, threshold * 4.0f + feather * 2.0f, (sampleDepth - centerDepth) * w);
    float guardMask = max(nearMask, farMask) * saturate(disocclusion_strength);
    return lerp(base, centerColor, guardMask);
}

#if SCATTER_COMPOSE
// Read the synthesized eye from the scatter colour buffer, which lives in
// SYNTHESIS space (synth_width x synth_height). When that's below the output
// res (the half-res synth toggle) this bilinearly upscales; at scale 1.0
// synth == out and it collapses to the exact 1:1 fetch (bit-identical).
float3 SampleScatterUpscaled(uint ox, uint oy)
{
    if (synth_width == out_width && synth_height == out_height) {
        return g_scatterColor[uint2(ox, oy)].rgb;
    }
    float fx = ((float)ox + 0.5f) * (float)synth_width / (float)out_width - 0.5f;
    float fy = ((float)oy + 0.5f) * (float)synth_height / (float)out_height - 0.5f;
    fx = clamp(fx, 0.0f, (float)synth_width - 1.0f);
    fy = clamp(fy, 0.0f, (float)synth_height - 1.0f);
    int x0 = (int)floor(fx);
    int y0 = (int)floor(fy);
    int x1 = min(x0 + 1, (int)synth_width - 1);
    int y1 = min(y0 + 1, (int)synth_height - 1);
    float wx = fx - (float)x0;
    float wy = fy - (float)y0;
    float3 c00 = g_scatterColor[uint2(x0, y0)].rgb;
    float3 c10 = g_scatterColor[uint2(x1, y0)].rgb;
    float3 c01 = g_scatterColor[uint2(x0, y1)].rgb;
    float3 c11 = g_scatterColor[uint2(x1, y1)].rgb;
    return lerp(lerp(c00, c10, wx), lerp(c01, c11, wx), wy);
}

float ScatterKeyTrust(uint rawKey)
{
    if (near_field_strength < 0.5f) {
        return 1.0f;
    }

    if (rawKey == 0u) {
        return 0.0f;
    }

    // Nonzero keys mean the scatter/fill pass found a real depth to stand on.
    // The outer-strip bug is handled earlier by refusing keyless hybrid history;
    // rejecting all stash/background provenance here creates black halos around
    // close native stereo geometry.
    return 1.0f;
}

float SampleScatterTrustUpscaled(uint ox, uint oy)
{
    if (near_field_strength < 0.5f) {
        return 1.0f;
    }

    if (synth_width == out_width && synth_height == out_height) {
        return ScatterKeyTrust(g_scatterKey[uint2(min(ox, synth_width - 1u), min(oy, synth_height - 1u))]);
    }

    float fx = ((float)ox + 0.5f) * (float)synth_width / (float)out_width - 0.5f;
    float fy = ((float)oy + 0.5f) * (float)synth_height / (float)out_height - 0.5f;
    fx = clamp(fx, 0.0f, (float)synth_width - 1.0f);
    fy = clamp(fy, 0.0f, (float)synth_height - 1.0f);
    int x0 = (int)floor(fx);
    int y0 = (int)floor(fy);
    int x1 = min(x0 + 1, (int)synth_width - 1);
    int y1 = min(y0 + 1, (int)synth_height - 1);
    float wx = fx - (float)x0;
    float wy = fy - (float)y0;
    float c00 = ScatterKeyTrust(g_scatterKey[uint2(x0, y0)]);
    float c10 = ScatterKeyTrust(g_scatterKey[uint2(x1, y0)]);
    float c01 = ScatterKeyTrust(g_scatterKey[uint2(x0, y1)]);
    float c11 = ScatterKeyTrust(g_scatterKey[uint2(x1, y1)]);
    return lerp(lerp(c00, c10, wx), lerp(c01, c11, wx), wy);
}

float3 HybridScatterFallbackColor(float2 uv, float sourceDepth, float3 centerRgb)
{
    if (near_field_strength < 0.5f) {
        return centerRgb;
    }

    float targetDepth = HybridTargetLinearDepth(uv);
    float2 targetUv = saturate(HybridTargetColorUv(uv));
    float4 targetColor = g_hybridTargetColorTex.SampleLevel(g_linearSampler, targetUv, 0);
    float targetWeight = HybridTargetCoverageWeight(uv, targetDepth, sourceDepth, targetColor);

    return lerp(float3(0.0f, 0.0f, 0.0f), targetColor.rgb, targetWeight);
}

// AFW CombinedWarping: pull the synthesized eye toward last frame's REAL
// render of the same eye (reprojected through the exact camera delta and
// depth-validated). The warp output and a native render of the same view
// differ subtly in resampling character and disocclusion treatment; under
// per-frame eye alternation that difference flickers at half rate. Blending
// validated real history into the WHOLE synthesized eye (not just holes)
// collapses the difference. Gated on temporal_enabled > 1.5 (the AFW mode;
// plain scatter history is the previous synthesized output, which would
// smear here).
float3 ApplyAfwHistoryBlend(uint2 px, float3 c)
{
    if (temporal_enabled < 1.5f) {
        return c;
    }
    uint skRaw = g_scatterKey[px];
    uint sk = skRaw & 0x7FFFFFFFu; // strip the fill's marker bit
    // Marker bit set = this pixel is a fill band: its warp-side color is
    // synthetic, so the color-agreement gate below must not protect it.
    const bool wasFilled = (skRaw & 0x80000000u) != 0u;
    float estDepth = asfloat(sk);
    float2 uvRaw = float2((px.x + 0.5f) / (float)out_width,
                          (px.y + 0.5f) / (float)out_height);
    float2 ndc = float2(uvRaw.x * 2.0f - 1.0f, 1.0f - uvRaw.y * 2.0f);
    if (sk == 0u) {
        // No warp data AT ALL: the outer no-source band (beyond the source
        // eye's frustum on this eye's outward side) and unscattered sky. The
        // caller's fallback is centerColor - the OTHER eye's content at the
        // wrong parallax - which alternates against this eye's REAL render
        // at half rate: the leftmost-of-left/rightmost-of-right edge
        // flicker. But this eye rendered the band itself one frame ago:
        // fetch the stash, iterating once at the FOUND depth so near
        // content lands at its own parallax instead of the far plane's.
        float lookupD = 0.0f;
        float2 bandUv = float2(0.0f, 0.0f);
        uint bandKey = 0u;
        [unroll]
        for (int it = 0; it < 2; ++it) {
            float4 bp = mul(reproj_target_to_prev, float4(ndc, lookupD, 1.0f));
            float bw = (abs(bp.w) > 1e-6f) ? bp.w : 1e-6f;
            float2 bn = bp.xy / bw;
            bandUv = float2(bn.x * 0.5f + 0.5f, 0.5f - bn.y * 0.5f);
            int2 bpx = int2((int)(bandUv.x * (float)out_width), (int)(bandUv.y * (float)out_height));
            if (bpx.x < 0 || bpx.x >= (int)out_width || bpx.y < 0 || bpx.y >= (int)out_height) {
                bandKey = 0u;
                break;
            }
            bandKey = g_historyKey[uint2(bpx)] & 0x7FFFFFFFu;
            if (bandKey == 0u || abs(asfloat(bandKey) - lookupD) <= max(0.05f * lookupD, 5e-4f)) {
                break;
            }
            lookupD = asfloat(bandKey);
        }
        if (bandKey != 0u) {
            // The band's only alternative is wrong-parallax content: take
            // the real render outright.
            return g_historyColorSrv.SampleLevel(g_linearSampler, bandUv, 0).rgb;
        }
        return c; // never seen (fresh rotation into unviewed area)
    }
    float4 prev = mul(reproj_target_to_prev, float4(ndc, estDepth, 1.0f));
    float w = (abs(prev.w) > 1e-6f) ? prev.w : 1e-6f;
    float2 pn = prev.xy / w;
    // Bilinear history fetch. The reprojection lands at fractional pixel
    // positions almost everywhere (the overscan mapping is a ~1/1.12 scale),
    // so a nearest-neighbor read snaps each sample by up to half a pixel in
    // a sawtooth pattern across the screen - blended at high weight that
    // reads as whole-screen micro-shake alternating with the real frames.
    // The filtering itself is one sampler op against the SRV alias of the
    // history (it lives in a shader-readable state for this pass).
    float2 histUv = float2(pn.x * 0.5f + 0.5f, 0.5f - pn.y * 0.5f);
    // MV advection: the reprojection above is camera-only, so object-space
    // animation (swaying plants, fish) lands the fetch on the object's OLD
    // position - the color gate then rejects it and the pixel shimmers at
    // half rate. The velocity buffer lives in the rendered (source) eye's
    // screen space, and under AFW its prev-frame camera IS the target eye:
    // subtracting the in-shader static prediction from the decoded velocity
    // isolates the object-motion residual, which advects the history fetch
    // onto the object's true previous position. Misses (wrong texel via the
    // disparity-blind lookup) still fail the depth/color gates below - the
    // failure mode is the status quo, not a new artifact.
    {
        uint vw, vh;
        g_velocityTex.GetDimensions(vw, vh);
        if (vw != 0u) {
            float2 srcUv = SourceRemapUv(uvRaw);
            float2 vUv = srcUv;
            if (vw >= srcWidth * 2u) {
                vUv.x *= 0.5f; // double-wide family target; lone view in the left half
            }
            float4 enc = g_velocityTex.SampleLevel(g_pointSampler, vUv, 0);
            if (any(enc.xy != 0.0f)) { // zero texel = unwritten sentinel (static)
                const float invDiv = 1.0f / (0.499f * 0.5f);
                float2 linV = enc.xy * invDiv - (32767.0f / 65535.0f) * invDiv;
                float2 v = (linV * abs(linV)) * 0.5f; // VELOCITY_ENCODE_GAMMA (SM5+)
                // 4-channel velocity packs prev device depth in zw; the
                // 2-channel format reads zero there - keep the scatter key.
                float vdepth = estDepth;
                if (enc.z != 0.0f || enc.w != 0.0f) {
                    uint hi = (uint)round(enc.z * 65535.0f) << 16;
                    uint lo = (uint)round(enc.w * 65535.0f) & 0xFFFEu;
                    vdepth = asfloat(hi | lo);
                }
                // The velocity texel must belong to THIS pixel's surface. At
                // a disocclusion reveal the disparity-blind lookup lands on
                // the OCCLUDER that opened the hole (near) while the band's
                // committed content is background (far) - advecting by the
                // occluder's motion drags unrelated background in, and
                // wasFilled bypasses the color gate, so it shipped a woven
                // band of displaced logo/water. Depth agreement keys the
                // advection to same-surface motion only - which keeps it
                // ACTIVE for interior stretch gaps on the moving object
                // itself (their committed key IS the object's depth).
                if (abs(vdepth - estDepth) <= max(0.2f * estDepth, 1e-3f)) {
                    // Static prediction for the source texel: source ->
                    // target eye (same frame), then target -> previous
                    // frame. Clip vectors compose without intermediate
                    // w-divides (homogeneous scale cancels in the divide).
                    float2 srcNdc = float2(srcUv.x * 2.0f - 1.0f, 1.0f - srcUv.y * 2.0f);
                    float4 sclip = float4(srcNdc, vdepth, 1.0f);
                    float4 t = (mode_param0 < 0.5f) ? mul(reproj_source_to_right, sclip)
                                                    : mul(reproj_source_to_left, sclip);
                    float4 ps = mul(reproj_target_to_prev, t);
                    float pw = (abs(ps.w) > 1e-6f) ? ps.w : 1e-6f;
                    float2 camV = srcNdc - ps.xy / pw;
                    float2 objV = v - camV;
                    histUv -= objV * float2(0.5f, -0.5f);
                }
            }
        }
    }
    float fx = histUv.x * (float)out_width - 0.5f;
    float fy = histUv.y * (float)out_height - 0.5f;
    if (fx < 0.0f || fy < 0.0f || fx > (float)(out_width - 1u) || fy > (float)(out_height - 1u)) {
        return c;
    }
    // Depth-validate at the nearest tap (keys don't interpolate). Fill bands
    // get a looser ABSOLUTE floor: their committed key can be far-field
    // (open water at device ~1e-4, reversed-Z), where a relative tolerance
    // collapses below the rock-vs-water separation and rejects the real
    // reveal content the blend exists to deliver.
    //
    // SOFT confidence, not a binary accept: under 6DOF motion the
    // reprojection error grows smoothly, and a hard threshold makes pixels
    // TOGGLE between history and warp frame-to-frame - half-rate shimmer
    // that only exists while moving. Full weight inside half the tolerance,
    // fading to zero at twice it; at rest (error ~0) identical to before.
    uint hk = g_historyKey[uint2((uint)round(max(fx, 0.0f)), (uint)round(max(fy, 0.0f)))] & 0x7FFFFFFFu;
    const float tol = max(0.15f * estDepth, wasFilled ? 2e-3f : 2e-4f);
    const float depthErr = abs(asfloat(hk) - estDepth);
    const float depthConf = saturate((2.0f * tol - depthErr) / (1.5f * tol));
    if (hk != 0u && depthConf > 0.0f) {
        float3 h = g_historyColorSrv.SampleLevel(g_linearSampler, histUv, 0).rgb;
        // Color-agreement gate: the blend exists to cancel the SUBTLE
        // real-vs-warp resampling difference, where history and warp agree
        // closely. Object-space animation that the MV advection above missed
        // (no velocity texel, disparity-blind lookup) still arrives displaced
        // while its depth validates - blending that paints a double image.
        // Large color deltas therefore reject history instead (the warp
        // result stands alone). Correctly-advected fetches agree in color and
        // pass naturally.
        // EXCEPT in fill bands: there the warp side is synthetic fill and the
        // depth-validated history is the actual render of the reveal, so a
        // disagreement is precisely the case where history must win.
        float lumDiff = dot(abs(h - c), float3(0.299f, 0.587f, 0.114f));
        float gate = wasFilled ? 1.0f : saturate(1.0f - lumDiff * 8.0f);
        c = lerp(c, h, saturate(temporal_blend) * gate * depthConf);
    }
    return c;
}
#endif

[numthreads(DIBR_TG, DIBR_TG, 1)]
void CSMain(uint3 dtid : SV_DispatchThreadID)
{
    uint x = dtid.x;
    uint y = dtid.y;
    // One thread per OUTPUT pixel (out == src unless the overscan-grown render
    // target makes the source wider than the true-FOV output).
    if (x >= out_width || y >= out_height) return;

    float2 uv = float2((x + 0.5f) / (float)out_width,
                        (y + 0.5f) / (float)out_height);

#if !DIBR_LEAN
    if (edge_compression > 0.0f) {
        float2 s = (uv - 0.5f) * 2.0f;
        float c = edge_compression * 3.0f;
        float2 s_warp = float2(atan(s.x * c), atan(s.y * c)) * pre_edge_comp_inv;
        uv = s_warp * 0.5f + 0.5f;
    }
    else if (edge_compression < 0.0f) {
        float2 s = (uv - 0.5f) * 2.0f;
        float c = -edge_compression * 1.2f;
        float2 s_warp = float2(tan(s.x * c), tan(s.y * c)) * pre_edge_comp_inv;
        uv = s_warp * 0.5f + 0.5f;
    }
#endif
    uv = ApplyOutputGeometry(uv);

#if !DIBR_LEAN
    float debugMode = floor(debug_view_mode + 0.5f);
#if SCATTER_COMPOSE
    if (debugMode >= 10.5f && near_field_strength >= 0.5f) {
        float sourceDepth = SamplePreparedDepthBase(SourceRemapUv(uv));
        float targetDepth = HybridTargetLinearDepth(uv);
        float2 targetUv = saturate(HybridTargetColorUv(uv));
        float4 targetColor = g_hybridTargetColorTex.SampleLevel(g_linearSampler, targetUv, 0);
        float signal = HybridTargetSignalWeight(targetColor);
        float mask = HybridTargetCoverageWeight(uv, targetDepth, sourceDepth, targetColor);
        if (debugMode < 11.5f) {
            WriteStereoPair(x, y, targetColor, targetColor);
        } else {
            WriteStereoPair(x, y, float4(mask, targetDepth, signal, 1.0f), float4(mask, targetDepth, signal, 1.0f));
        }
        return;
    }
#endif
    if (debugMode >= 8.5f) {
        // Debug view 9: fill provenance. Which source produced each pixel of
        // the synthesized eye - the question every reveal-artifact hunt
        // starts with. Gray = scattered geometry (luminance), red =
        // scanline/source fallback, green = two-sided interpolation, blue =
        // stash history accepted, cyan = persistent background layer.
        uint skRawD = g_scatterKey[uint2(min(x, synth_width - 1u), min(y, synth_height - 1u))];
        float3 col;
        if (skRawD == 0u) {
            col = float3(0.0f, 0.0f, 0.0f); // never covered, never filled
        } else if ((skRawD & 0x80000000u) == 0u) {
            float lum = dot(g_scatterColor[uint2(min(x, synth_width - 1u), min(y, synth_height - 1u))].rgb,
                float3(0.299f, 0.587f, 0.114f));
            col = float3(lum, lum, lum);
        } else {
            uint provD = skRawD & 0x3u;
            col = (provD == 1u) ? float3(0.1f, 0.9f, 0.1f)
                : (provD == 2u) ? float3(0.15f, 0.25f, 1.0f)
                : (provD == 3u) ? float3(0.1f, 0.9f, 0.9f)
                                : float3(1.0f, 0.15f, 0.1f);
        }
        float4 dbg = float4(col, 1.0f);
        WriteStereoPair(x, y, dbg, dbg);
        return;
    }
    if (debugMode >= 7.5f) {
        // Debug view 8: SceneVelocity wiring proof (select/bind/sample). The
        // snapshot is this frame's completed velocity GBuffer (copied at the
        // post-opaque bind); UE only writes OBJECT motion by default, so
        // static world keeps the zero clear texel. Dark green = static, heat = decoded |V| (Common.ush:
        // linear decode then the SM5+ gamma square), black = no data.
        uint vw, vh;
        g_velocityTex.GetDimensions(vw, vh);
        float3 col = float3(0.0f, 0.0f, 0.0f);
        if (vw != 0u) {
            float2 vUv = uv;
            if (vw >= srcWidth * 2u) {
                vUv.x *= 0.5f; // double-wide family target; the lone view fills the left half
            }
            float4 enc = g_velocityTex.SampleLevel(g_pointSampler, vUv, 0);
            if (any(enc.xy != 0.0f)) {
                const float invDiv = 1.0f / (0.499f * 0.5f);
                float2 lin = enc.xy * invDiv - (32767.0f / 65535.0f) * invDiv;
                float2 v = (lin * abs(lin)) * 0.5f; // VELOCITY_ENCODE_GAMMA (SM5+)
                col = DebugHeat(saturate(length(v) * max(debug_view_scale, 0.0f) * 30.0f));
            } else {
                col = float3(0.0f, 0.07f, 0.0f);
            }
        }
        float4 dbg = float4(col, 1.0f);
        WriteStereoPair(x, y, dbg, dbg);
        return;
    }
    if (debugMode >= 1.0f) {
        // Debug views are the only consumer of the fully conditioned depth and
        // the guarded disparity (the gather path searches on YoroSearchDepth,
        // the scatter path on the raw chain) - keeping this work inside the
        // branch takes it off the hot path for every mode.
        float depth = SamplePreparedDepth(uv);
        depth = ExpandDepth(uv, depth);
        depth = ReconstructDepth(uv, depth);
        depth = ApplyUiAlphaDepthMask(uv, ApplyShapeDepthMask(uv, ApplyWeaponDepthMask(uv, ApplyRegionDepthMask(uv, depth))));
        depth = ApplyDepthRangeBoost(depth);
        depth = ApplyFilterEmulatorDepthControls(depth);

        float guardedDisparity = divergence * StereoDepthDelta(depth) * FilterEmulatorFocusScale(depth);
        guardedDisparity *= ScreenEdgeGuard(uv, depth) * ConvergenceBoundaryScale(uv, depth) * DepthArtifactGuardScale(uv, depth) * WeaponBoundaryScale(uv, depth);
        float leftScale = FocusReductionScale(uv, depth, 1.0f);
        float rightScale = FocusReductionScale(uv, depth, -1.0f);
        float offset = (guardedDisparity * 0.5f * (leftScale + rightScale) + perspective_shift) / (float)srcWidth;

        float depthValue = DebugDepthValue(depth);
        if (debugMode >= 5.5f) {
            float4 debugColor = DibrAlignmentGridColor(uv, depth, debugMode);
            WriteStereoPair(x, y, debugColor, debugColor);
            return;
        }
        if (debugMode >= 4.0f) {
            WriteStereoPair(x, y,
                g_colorTex.SampleLevel(g_linearSampler, uv, 0),
                (debugMode < 4.5f)
                    ? float4(depthValue, depthValue, depthValue, 1.0f)
                    : float4(PackDepthValue24(depthValue), 1.0f));
            return;
        }

        float4 debugColor = DibrDebugColor(uv, depth, offset);
        WriteStereoPair(x, y, debugColor, debugColor);
        return;
    }
#endif

    float refEye = mode_param0;
    // SourceRemapUv crops the overscanned source render back to the true FOV
    // (identity when overscan is off).
    float4 centerColor = SampleFilteredOutputColor(SourceRemapUv(uv));
    float2 leftInterlaceOffset = InterlaceSampleOffset(1.0f);
    float2 rightInterlaceOffset = InterlaceSampleOffset(-1.0f);

#if SCATTER_COMPOSE
    // The scatter chain already built the synthesized eye; the only depth
    // consumer left is ScreenEdgeGuard's near-field gate - one cheap tap.
    float searchDepth = SamplePreparedDepthBase(SourceRemapUv(uv));
#else
    // Occlusion-aware source search for the synthesized eye. The two
    // depth-gradient guards are evaluated once here and folded into the
    // search's shift scale (see YoroSynthShiftBase).
    float searchDepth = YoroSearchDepth(SourceRemapUv(uv));
    float boundaryScale = ConvergenceBoundaryScale(uv, searchDepth) * DepthArtifactGuardScale(uv, searchDepth);
#endif

    if (refEye < 0.5f) {
        // Left reference: pristine left, synthesize right at full disparity.
        float2 leftRefUV = ApplyOutputEyeAlignment(uv + leftInterlaceOffset, 1.0f);
        float4 leftRefColor = SampleFilteredOutputColor(SourceRemapUv(leftRefUV));
        float4 outLeft = ApplyCursorOverlay(uv, 1.0f, ApplyPresentationColor(uv, ApplyComfortNose(uv, 1.0f, ApplyOutputMatte(uv, leftRefColor, centerColor))));
        outLeft = ApplyAlignmentMarker(uv, leftRefUV, outLeft);

        float2 rightUV = uv;
        float4 rightColor;
#if SCATTER_COMPOSE
        // R2: the scatter pipeline already produced an occlusion-correct,
        // hole-filled synthesized eye; fetch it, blending toward the
        // unwarped center near the screen edges (the synthesized eye's
        // outer band has no source data - same role as the gather path's
        // ScreenEdgeGuard disparity squeeze).
        // AFW: the outer band IS sourced now (the fill recovers the eye's
        // own previous real render there), so the edge guard's centerColor
        // fade would REPLACE valid content with the other eye's image at the
        // wrong parallax - the residual 15% the history blend can't recover
        // is the half-rate ghost outline at the outward screen edge. Keep
        // the guard only for the non-AFW scatter path.
        float edgeKeep = (temporal_enabled > 1.5f) ? 1.0f : ScreenEdgeGuard(uv, searchDepth);
        edgeKeep *= SampleScatterTrustUpscaled(x, y);
        rightColor = float4(lerp(HybridScatterFallbackColor(uv, searchDepth, centerColor.rgb), SampleScatterUpscaled(x, y), edgeKeep), centerColor.a);
        rightColor.rgb = ApplyAfwHistoryBlend(uint2(x, y), rightColor.rgb);
        rightColor = ApplyHybridNearStereo(uv, searchDepth, rightColor);
#else
        float2 rightSearchUV = YoroSearchUv(uv, -1.0f, searchDepth, boundaryScale, 1.0f);
        rightUV = ApplyOutputEyeAlignment(rightSearchUV + rightInterlaceOffset, -1.0f);
        rightColor = SampleSynthStereoColor(rightUV, uv, centerColor, searchDepth);
#endif
        float4 outRight = ApplyCursorOverlay(uv, -1.0f, ApplyPresentationColor(uv, ApplyComfortNose(uv, -1.0f, ApplyOutputMatte(uv, rightColor, centerColor))));
        outRight = ApplyAlignmentMarker(uv, rightUV, outRight);
        if (floor(output_layout_mode + 0.5f) == 2.0f) {
            float4 outLeftReduced = outLeft;
#if SCATTER_COMPOSE
            // The scatter chain has no reduced-disparity variant; reuse the
            // full-disparity synthesized eye for the reduced view.
            float2 rightReducedUV = rightUV;
            float4 outRightReduced = outRight;
#else
            float2 rightReducedSearchUV = YoroSearchUv(uv, -1.0f, searchDepth, boundaryScale, 0.33333334f);
            float2 rightReducedUV = ApplyOutputEyeAlignment(rightReducedSearchUV + rightInterlaceOffset, -1.0f);
            float4 rightReducedColor = SampleSynthStereoColor(rightReducedUV, uv, centerColor, searchDepth);
            float4 outRightReduced = ApplyCursorOverlay(uv, -1.0f, ApplyPresentationColor(uv, ApplyComfortNose(uv, -1.0f, ApplyOutputMatte(uv, rightReducedColor, centerColor))));
            outRightReduced = ApplyAlignmentMarker(uv, rightReducedUV, outRightReduced);
#endif
            WriteStereoViews(x, y, outLeft, outLeftReduced, outRightReduced, outRight);
        } else {
            WriteStereoPair(x, y, outLeft, outRight);
        }
    } else {
        // Right reference: synthesize left at full disparity, pristine right.
        float2 leftUV = uv;
        float4 leftColor;
#if SCATTER_COMPOSE
        // See the right-eye branch: under AFW the edge guard must not fade
        // the recovered band back to wrong-parallax centerColor.
        float edgeKeep = (temporal_enabled > 1.5f) ? 1.0f : ScreenEdgeGuard(uv, searchDepth);
        edgeKeep *= SampleScatterTrustUpscaled(x, y);
        leftColor = float4(lerp(HybridScatterFallbackColor(uv, searchDepth, centerColor.rgb), SampleScatterUpscaled(x, y), edgeKeep), centerColor.a);
        leftColor.rgb = ApplyAfwHistoryBlend(uint2(x, y), leftColor.rgb);
        leftColor = ApplyHybridNearStereo(uv, searchDepth, leftColor);
#else
        float2 leftSearchUV = YoroSearchUv(uv, 1.0f, searchDepth, boundaryScale, 1.0f);
        leftUV = ApplyOutputEyeAlignment(leftSearchUV + leftInterlaceOffset, 1.0f);
        leftColor = SampleSynthStereoColor(leftUV, uv, centerColor, searchDepth);
#endif
        float4 outLeft = ApplyCursorOverlay(uv, 1.0f, ApplyPresentationColor(uv, ApplyComfortNose(uv, 1.0f, ApplyOutputMatte(uv, leftColor, centerColor))));
        outLeft = ApplyAlignmentMarker(uv, leftUV, outLeft);

        float2 rightRefUV = ApplyOutputEyeAlignment(uv + rightInterlaceOffset, -1.0f);
        float4 rightRefColor = SampleFilteredOutputColor(SourceRemapUv(rightRefUV));
        float4 outRight = ApplyCursorOverlay(uv, -1.0f, ApplyPresentationColor(uv, ApplyComfortNose(uv, -1.0f, ApplyOutputMatte(uv, rightRefColor, centerColor))));
        outRight = ApplyAlignmentMarker(uv, rightRefUV, outRight);
        if (floor(output_layout_mode + 0.5f) == 2.0f) {
#if SCATTER_COMPOSE
            float4 outLeftReduced = outLeft;
#else
            float2 leftReducedSearchUV = YoroSearchUv(uv, 1.0f, searchDepth, boundaryScale, 0.33333334f);
            float2 leftReducedUV = ApplyOutputEyeAlignment(leftReducedSearchUV + leftInterlaceOffset, 1.0f);
            float4 leftReducedColor = SampleSynthStereoColor(leftReducedUV, uv, centerColor, searchDepth);
            float4 outLeftReduced = ApplyCursorOverlay(uv, 1.0f, ApplyPresentationColor(uv, ApplyComfortNose(uv, 1.0f, ApplyOutputMatte(uv, leftReducedColor, centerColor))));
            outLeftReduced = ApplyAlignmentMarker(uv, leftReducedUV, outLeftReduced);
#endif
            float4 outRightReduced = outRight;
            WriteStereoViews(x, y, outLeft, outLeftReduced, outRightReduced, outRight);
        } else {
            WriteStereoPair(x, y, outLeft, outRight);
        }
    }
}
