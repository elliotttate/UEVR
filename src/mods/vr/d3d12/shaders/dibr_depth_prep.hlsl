// dibr_depth_prep.hlsl - per-pixel depth conditioning prepass
//
// Runs the depth conditioning chain ONCE per source pixel and stores the
// result in g_prepOut (x = conditioned depth, y = source gradient where the
// consumer wants it). The gather kernels' search loops then read conditioned
// depth back as a single tap per probe instead of re-running the multi-tap
// chain per step - the chain used to be re-evaluated up to ~100x per output
// pixel inside the raymarch/YORO searches.
//
// Compiled three ways (one PSO each, same source - see DIBRSynthesis):
//   PREP_MODE 0 - inverse kernel:  base canonical depth + depth edge mask
//                 (the old SamplePreparedDepth; neighborhood passes stay in
//                 the kernel as single taps of this output).
//   PREP_MODE 1 - raymarch kernel: the full SamplePreparedDepthWithGradient
//                 chain (shaping, auto balance, edge mask, expand,
//                 smooth/protect, reconstruct, region/weapon/shape/UI masks).
//   PREP_MODE 2 - YORO gather:     the old YoroSearchDepth conditioning
//                 (5-tap smooth/protect + masks + range boost + filter
//                 emulator).
//
// The StereoParams cbuffer below must stay byte-identical with every other
// DIBR kernel (the runtime layout guard checks it).

#ifndef PREP_MODE
#define PREP_MODE 0
#endif

// Thread-group edge (overridable via UEVR_DIBR_TG; the C++ dispatch math
// uses the same value).
#ifndef DIBR_TG
#define DIBR_TG 16
#endif

Texture2D<float4> g_colorTex : register(t0);
Texture2D<float>  g_depthTex : register(t1);
RWTexture2D<float4> g_prepOut : register(u5);
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
};

float EffectiveConvergence()
{
    return pre_effective_convergence;
}

float FilterEmulatorMask()
{
    float compositionMode = floor(output_composition_mode + 0.5f);
    float anaglyphMode = floor(output_anaglyph_mode + 0.5f);
    return (anaglyphMode >= 4.5f && compositionMode >= 2.5f && compositionMode < 5.5f) ? 1.0f : 0.0f;
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
    float stepSize = 1.0f / max(exp2(bits) - 1.0f, 1.0f);
    float noise = DepthDitherNoise(uv) - 0.5f;
    return saturate(depth + noise * stepSize * strength);
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
#if PREP_MODE == 1
    // The raymarch chain's letterbox heuristic is purely positional.
    return saturate(max(xMask, yMask) * strength);
#else
    float3 color = g_colorTex.SampleLevel(g_linearSampler, saturate(uv), 0).rgb;
    float luma = dot(color, float3(0.2126f, 0.7152f, 0.0722f));
    float threshold = saturate(letterbox_auto_threshold);
    float darkMask = 1.0f - smoothstep(threshold, min(threshold + feather, 1.0f), luma);
    return saturate(max(xMask, yMask) * darkMask * strength);
#endif
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

// Canonical depth: raw sample -> reverse/flip -> linearize -> dither ->
// letterbox flatten. Identical to the old SamplePreparedDepthBase (inverse /
// YORO) and SampleCanonicalDepth (raymarch).
float BaseDepth(float2 uv)
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

#if PREP_MODE == 1 || PREP_MODE == 2

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

float ApplyUiAlphaDepthMask(float2 uv, float depth)
{
    float4 sampleColor = g_colorTex.SampleLevel(g_linearSampler, saturate(uv), 0);
    float alphaMask = UiAlphaMaskFromColor(sampleColor);
    float autoMask = UiAutoMaskFromColor(sampleColor);
    depth = lerp(depth, saturate(ui_alpha_mask_target_depth), alphaMask);
    return lerp(depth, saturate(ui_auto_mask_target_depth), autoMask);
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

#endif // PREP_MODE == 1 || PREP_MODE == 2

#if PREP_MODE == 0

// Inverse kernel base conditioning: the old SamplePreparedDepth (canonical
// depth + the optional depth edge mask).
float ApplyDepthEdgeMask(float2 uv, float depth)
{
    float strength = clamp(depth_edge_mask_strength, -1.0f, 1.0f);
    if (abs(strength) <= 0.0f) {
        return depth;
    }

    float radius = max(depth_edge_mask_radius, 0.0f);
    float2 texel = float2(pre_inv_src_width, pre_inv_src_height) * radius;
    float dl = BaseDepth(uv - float2(texel.x, 0.0f));
    float dr = BaseDepth(uv + float2(texel.x, 0.0f));
    float du = BaseDepth(uv - float2(0.0f, texel.y));
    float dd = BaseDepth(uv + float2(0.0f, texel.y));
    float neighborAvg = (dl + dr + du + dd) * 0.25f;
    float gradient = max(max(abs(depth - dl), abs(depth - dr)), max(abs(depth - du), abs(depth - dd)));
    gradient = max(gradient, max(abs(dr - dl), abs(dd - du)));
    float edgeMask = smoothstep(max(depth_edge_mask_threshold, 0.0f), max(depth_edge_mask_threshold, 0.0f) + max(depth_edge_mask_feather, 0.0001f), gradient);
    float target = (strength >= 0.0f) ? neighborAvg : 1.0f;
    return saturate(lerp(depth, target, edgeMask * abs(strength)));
}

#elif PREP_MODE == 1

// Raymarch chain: shaping -> auto balance -> edge mask -> expand ->
// smooth/protect -> reconstruct -> masks, with the same tap structure as the
// old in-kernel SamplePreparedDepthWithGradient.
float ApplyNearFieldFlattening(float depth)
{
    float strength = saturate(near_field_strength);
    if (strength <= 0.0f) {
        return depth;
    }

    float startDepth = saturate(near_field_start);
    float endDepth = max(saturate(near_field_end), startDepth + 0.0001f);
    float mask = 1.0f - smoothstep(startDepth, endDepth, depth);
    float target = saturate(near_field_target);
    return lerp(depth, target, mask * strength);
}

float ShapeDepthBase(float depth)
{
    float shaped = saturate(depth);
    float floorValue = saturate(depth_floor);
    float ceilingValue = max(saturate(depth_ceiling), floorValue + 0.0001f);
    shaped = saturate((shaped - floorValue) / (ceilingValue - floorValue));
    shaped = pow(shaped, max(depth_curve, 0.01f));
    float focus = EffectiveConvergence();
    shaped = focus + (shaped - focus) * max(depth_gain, 0.0f);
    shaped = ApplyDepthRangeBoost(shaped);

    float nearLimit = max(popout_limit, 0.0f);
    if (nearLimit < 1.0f) {
        shaped = max(shaped, focus - nearLimit);
    }

    shaped = ApplyNearFieldFlattening(shaped);
    return saturate(ApplyFilterEmulatorDepthControls(shaped));
}

float SampleShapedDepthBase(float2 uv)
{
    return ShapeDepthBase(BaseDepth(uv));
}

float AutoBalanceDepth(float depth, float2 uv)
{
    float strength = saturate(auto_depth_strength);
    if (strength <= 0.0f) {
        return depth;
    }

    float aspect = max((float)srcHeight, 1.0f) / max((float)srcWidth, 1.0f);
    float radius = max(auto_depth_radius, 0.0f);
    float2 rx = float2(radius * aspect, 0.0f);
    float2 ry = float2(0.0f, radius);

    float d0 = depth;
    float d1 = SampleShapedDepthBase(uv + rx);
    float d2 = SampleShapedDepthBase(uv - rx);
    float d3 = SampleShapedDepthBase(uv + ry);
    float d4 = SampleShapedDepthBase(uv - ry);
    float d5 = SampleShapedDepthBase(uv + rx + ry);
    float d6 = SampleShapedDepthBase(uv + rx - ry);
    float d7 = SampleShapedDepthBase(uv - rx + ry);
    float d8 = SampleShapedDepthBase(uv - rx - ry);

    float localMin = min(d0, min(min(d1, d2), min(min(d3, d4), min(min(d5, d6), min(d7, d8)))));
    float localMax = max(d0, max(max(d1, d2), max(max(d3, d4), max(max(d5, d6), max(d7, d8)))));
    float range = max(localMax - localMin, max(auto_depth_min_range, 0.0001f));
    float localNorm = saturate((depth - localMin) / range);
    float balanced = saturate(EffectiveConvergence() + (localNorm - 0.5f) * max(auto_depth_contrast, 0.0f));
    return saturate(lerp(depth, balanced, strength));
}

float ShapeDepth(float depth, float2 uv)
{
    return AutoBalanceDepth(ShapeDepthBase(depth), uv);
}

float ReconstructDepth(float2 uv, float depth)
{
    float strength = saturate(depth_reconstruct_strength);
    if (strength <= 0.0f) {
        return depth;
    }

    float radius = max(depth_reconstruct_radius, 0.0f);
    float2 texel = float2(pre_inv_src_width, pre_inv_src_height) * radius;
    float dl = SampleShapedDepthBase(uv - float2(texel.x, 0.0f));
    float dr = SampleShapedDepthBase(uv + float2(texel.x, 0.0f));
    float du = SampleShapedDepthBase(uv - float2(0.0f, texel.y));
    float dd = SampleShapedDepthBase(uv + float2(0.0f, texel.y));
    float dlu = SampleShapedDepthBase(uv - texel);
    float dru = SampleShapedDepthBase(uv + float2(texel.x, -texel.y));
    float dld = SampleShapedDepthBase(uv + float2(-texel.x, texel.y));
    float drd = SampleShapedDepthBase(uv + texel);

    float localMin = min(depth, min(min(dl, dr), min(min(du, dd), min(min(dlu, dru), min(dld, drd)))));
    float avg = (dl + dr + du + dd + dlu + dru + dld + drd) * 0.125f;
    float gradient = max(abs(dr - dl), abs(dd - du));
    float threshold = max(depth_reconstruct_edge_threshold, 0.0001f);
    float edgeMask = smoothstep(threshold, threshold * 2.0f, gradient);
    float target = lerp(avg, localMin, saturate(depth_reconstruct_near_bias) * edgeMask);
    return saturate(lerp(depth, target, strength));
}

float ApplyDepthEdgeMaskFromGradient(float depth, float neighborAvg, float gradient)
{
    float strength = clamp(depth_edge_mask_strength, -1.0f, 1.0f);
    if (abs(strength) <= 0.0f) {
        return depth;
    }

    float threshold = max(depth_edge_mask_threshold, 0.0f);
    float feather = max(depth_edge_mask_feather, 0.0001f);
    float edgeMask = smoothstep(threshold, threshold + feather, gradient);
    float target = (strength >= 0.0f) ? neighborAvg : 1.0f;
    return saturate(lerp(depth, target, edgeMask * abs(strength)));
}

float PreparedDepthWithGradient(float2 uv, out float sourceGradient)
{
    uv = saturate(uv);

    float expandStrength = saturate(depth_expand_strength);
    float sampleRadius = (expandStrength > 0.0f) ? max(depth_expand_radius, 0.0f) : 1.0f;
    if (abs(clamp(depth_edge_mask_strength, -1.0f, 1.0f)) > 0.0f) {
        sampleRadius = max(sampleRadius, max(depth_edge_mask_radius, 0.0f));
    }
    float2 texel = float2(pre_inv_src_width, pre_inv_src_height) * sampleRadius;

    float2 uvL = uv - float2(texel.x, 0.0f);
    float2 uvR = uv + float2(texel.x, 0.0f);
    float2 uvU = uv - float2(0.0f, texel.y);
    float2 uvD = uv + float2(0.0f, texel.y);

    float d = ShapeDepth(BaseDepth(uv), uv);
    float dl = ShapeDepth(BaseDepth(uvL), uvL);
    float dr = ShapeDepth(BaseDepth(uvR), uvR);
    float du = ShapeDepth(BaseDepth(uvU), uvU);
    float dd = ShapeDepth(BaseDepth(uvD), uvD);

    float minDepth = min(d, min(min(dl, dr), min(du, dd)));
    float neighborAvg = (dl + dr + du + dd) * 0.25f;
    float gradient = max(abs(dr - dl), abs(dd - du));
    sourceGradient = gradient;

    d = ApplyDepthEdgeMaskFromGradient(d, neighborAvg, gradient);
    minDepth = min(d, min(min(dl, dr), min(du, dd)));

    if (expandStrength > 0.0f) {
        float expandThreshold = max(depth_expand_edge_threshold, 0.0001f);
        float expandMask = smoothstep(expandThreshold, expandThreshold * 2.0f, gradient) * expandStrength;
        float expandTarget = lerp(neighborAvg, minDepth, saturate(depth_expand_near_bias));
        d = saturate(lerp(d, expandTarget, expandMask));
        minDepth = min(d, min(min(dl, dr), min(du, dd)));
    }

    float edgeWeight = saturate(gradient * 24.0f);
    float smoothWeight = saturate(range_smoothing) * (1.0f - edgeWeight);
    float protectedWeight = edgeWeight * saturate(foreground_protect);

    float smoothedDepth = lerp(d, neighborAvg, smoothWeight);

    // Bias toward the nearest local depth on sharp transitions. This reduces
    // background bleeding around foreground silhouettes during the ray search.
    float prepared = saturate(lerp(smoothedDepth, minDepth, protectedWeight));
    return ApplyUiAlphaDepthMask(uv, ApplyShapeDepthMask(uv, ApplyWeaponDepthMask(uv, ApplyRegionDepthMask(uv, ReconstructDepth(uv, prepared)))));
}

#else // PREP_MODE == 2

// YORO gather search depth: 5-tap smooth/protect conditioning (Depth3D's
// always-on min-dilation) + masks + range boost + filter emulator.
float YoroSearchDepthChain(float2 uv)
{
    uv = saturate(uv);
    float2 texel = float2(pre_inv_src_width, pre_inv_src_height);

    float d  = BaseDepth(uv);
    float dl = BaseDepth(uv - float2(texel.x, 0.0f));
    float dr = BaseDepth(uv + float2(texel.x, 0.0f));
    float du = BaseDepth(uv - float2(0.0f, texel.y));
    float dd = BaseDepth(uv + float2(0.0f, texel.y));

    float minDepth = min(d, min(min(dl, dr), min(du, dd)));
    float neighborAvg = (dl + dr + du + dd) * 0.25f;
    float gradient = max(abs(dr - dl), abs(dd - du));

    // Smooth flat areas, and on sharp edges pull depth toward the nearest
    // local neighbor so thin foreground features (floating text, plant
    // fronds) warp as one coherent block instead of shredding per pixel.
    float edgeWeight = saturate(gradient * 24.0f);
    float smoothWeight = saturate(range_smoothing) * (1.0f - edgeWeight);
    float protectedWeight = edgeWeight * saturate(foreground_protect);
    float depth = lerp(d, neighborAvg, smoothWeight);
    depth = saturate(lerp(depth, minDepth, protectedWeight));

    depth = ApplyUiAlphaDepthMask(uv, ApplyShapeDepthMask(uv, ApplyWeaponDepthMask(uv, ApplyRegionDepthMask(uv, depth))));
    depth = ApplyDepthRangeBoost(depth);
    return ApplyFilterEmulatorDepthControls(depth);
}

#endif // PREP_MODE

[numthreads(DIBR_TG, DIBR_TG, 1)]
void CSMain(uint3 dtid : SV_DispatchThreadID)
{
    if (dtid.x >= srcWidth || dtid.y >= srcHeight) return;
    float2 uv = float2((dtid.x + 0.5f) * pre_inv_src_width,
                       (dtid.y + 0.5f) * pre_inv_src_height);

    float gradient = 0.0f;
#if PREP_MODE == 0
    float depth = ApplyDepthEdgeMask(uv, BaseDepth(uv));
#elif PREP_MODE == 1
    float depth = PreparedDepthWithGradient(uv, gradient);
#else
    float depth = YoroSearchDepthChain(uv);
#endif
    g_prepOut[dtid.xy] = float4(depth, gradient, 0.0f, 0.0f);
}
