// AUTO-PATTERNED from dibr_yoro.hlsl's declarations - keep the cbuffer block
// byte-identical across every DIBR kernel (the runtime layout guard checks it).
//
// AFW history stash: copies this frame's raw source render (color + device
// depth, in the source's own overscanned screen space) into the history pair
// the fill kernel consumes next frame. Under AFW alternation the next frame
// synthesizes THIS eye, so its disocclusion holes fill with these REAL pixels
// (reprojected through reproj_target_to_prev and depth-validated against the
// stashed keys). Runs after the fill pass has read the previous stash.

// Thread-group edge (overridable via UEVR_DIBR_TG; the C++ dispatch math
// uses the same value).
#ifndef DIBR_TG
#define DIBR_TG 16
#endif

Texture2D<float4> g_colorTex : register(t0);
Texture2D<float>  g_depthTex : register(t1);
RWTexture2D<float4> g_sbsOut : register(u0);
RWTexture2D<uint> g_scatterKey : register(u1);
RWTexture2D<float4> g_scatterColor : register(u2);
// Written here, read by next frame's fill pass.
RWTexture2D<float4> g_historyColor : register(u3);
RWTexture2D<uint> g_historyKey : register(u4);
// Persistent background layer: read half = last frame's update (other eye's
// space, one frame ago; null until the first update), write half refreshed
// here. Remembers the FARTHEST surface seen per pixel across frames so
// reveals can fill with real background even while an occluder has covered
// it in every recent render.
Texture2D<float4> g_bgColorPrev : register(t5);
Texture2D<uint>   g_bgKeyPrev   : register(t6);
RWTexture2D<float4> g_bgColor : register(u6);
RWTexture2D<uint>   g_bgKey   : register(u7);
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

float SampleRawDeviceDepth(float2 uv)
{
    float mode = floor(depth_sample_mode + 0.5f);
    float2 duv = TransformDepthUv(saturate(uv));
    return (mode >= 1.0f)
        ? g_depthTex.SampleLevel(g_pointSampler, duv, 0)
        : g_depthTex.SampleLevel(g_linearSampler, duv, 0);
}

// Source-eye pixel + raw device depth -> synthesized target eye uv through
// the exact clip->clip matrix (see DIBRSynthesis.hpp).
float2 ReprojectSourceUv(float2 srcUv, float rawDepth, float eyeSign)
{
    float2 ndc = float2(srcUv.x * 2.0f - 1.0f, 1.0f - srcUv.y * 2.0f);
    float4 clip = float4(ndc, rawDepth, 1.0f);
    float4 t = (eyeSign > 0.0f) ? mul(reproj_source_to_left, clip) : mul(reproj_source_to_right, clip);
    float w = (abs(t.w) > 1e-6f) ? t.w : 1e-6f;
    float2 tNdc = t.xy / w;
    return float2(tNdc.x * 0.5f + 0.5f, 0.5f - tNdc.y * 0.5f);
}

float SynthEyeSign()
{
    // mode_param0: 0 = left reference (synthesize RIGHT, eyeSign -1).
    return (mode_param0 < 0.5f) ? -1.0f : 1.0f;
}

[numthreads(DIBR_TG, DIBR_TG, 1)]
void CSMain(uint3 dtid : SV_DispatchThreadID)
{
    // History lives in the source render's own pixel space (== output dims;
    // the overscanned projection is accounted for by the C++-built
    // reproj_target_to_prev matrix, not by any UV remap here).
    if (dtid.x >= out_width || dtid.y >= out_height) return;
    float2 uv = float2((dtid.x + 0.5f) / (float)out_width,
                       (dtid.y + 0.5f) / (float)out_height);
    // Raw device depth (reversed-Z) as the validation key, clamped strictly
    // above zero EXACTLY like the scatter keys (dibr_scatter_depth): with
    // reversed-Z, open water / sky sits AT device 0, and keying it 0 made
    // every history consumer reject the previous frame's real render as "no
    // data" precisely over the background that disocclusion reveals expose -
    // the bands fell back to per-row scanline fill (the laddered artifact)
    // even though the stash held the true content.
    //
    // Silhouette ring: the COLOR at an anti-aliased object edge is a
    // fore/background blend, but the DEPTH buffer doesn't anti-alias - edge
    // texels whose depth says "background" carry occluder-contaminated
    // color. Validated at face value they paint a 1px ghost CONTOUR of last
    // frame's silhouette into this frame's reveals (drifting into open
    // water when the object sways). Key the whole 3x3 ring of any strong
    // depth edge at the NEAREST depth so the consumers' depth gates reject
    // the contaminated texels and fall back to the (clean) interpolated
    // fill there.
    float d = SampleRawDeviceDepth(uv);
    float dmin = d;
    float dmax = d;
    const float2 texel = float2(1.0f / (float)out_width, 1.0f / (float)out_height);
    [unroll]
    for (int ny = -1; ny <= 1; ++ny) {
        [unroll]
        for (int nx = -1; nx <= 1; ++nx) {
            if (nx == 0 && ny == 0) continue;
            float nd = SampleRawDeviceDepth(uv + float2(nx, ny) * texel);
            dmin = min(dmin, nd);
            dmax = max(dmax, nd);
        }
    }
    const bool edge = (dmax - dmin) > max(0.10f * dmax, 1e-3f);
    const float keyD = max(edge ? dmax : d, 1e-7f);
    const float3 realCol = g_colorTex.SampleLevel(g_pointSampler, uv, 0).rgb;
    g_historyKey[dtid.xy] = asuint(keyD);
    g_historyColor[dtid.xy] = float4(realCol, 1.0f);

    // Persistent background layer update. Default: refresh with this frame's
    // real render (we SEE this pixel's content). Carry the remembered
    // background forward instead when the current content is significantly
    // NEARER than what this pixel remembered - an occluder has moved in
    // front, and overwriting would forget the background a future reveal
    // needs. The previous layer lives in LAST frame's real eye's space (the
    // other eye, one frame back): same-frame source->target hop composed
    // with target->prev, the velocity-advection matrix. The carry lookup
    // iterates once at the REMEMBERED depth: background parallax, not the
    // occluder's.
    float3 bgCol = realCol;
    float bgD = keyD;
    uint bw, bh;
    g_bgKeyPrev.GetDimensions(bw, bh);
    if (bw != 0u) {
        float2 ndc = float2(uv.x * 2.0f - 1.0f, 1.0f - uv.y * 2.0f);
        float lookupD = keyD;
        int2 pc = int2(-1, -1);
        uint pk = 0u;
        [unroll]
        for (int it = 0; it < 2; ++it) {
            float4 sclip = float4(ndc, lookupD, 1.0f);
            float4 t = (mode_param0 < 0.5f) ? mul(reproj_source_to_right, sclip)
                                            : mul(reproj_source_to_left, sclip);
            float4 pp = mul(reproj_target_to_prev, t);
            float w = (abs(pp.w) > 1e-6f) ? pp.w : 1e-6f;
            float2 pn = pp.xy / w;
            pc = int2((int)((pn.x * 0.5f + 0.5f) * (float)out_width),
                      (int)((0.5f - pn.y * 0.5f) * (float)out_height));
            if (pc.x < 0 || pc.x >= (int)out_width || pc.y < 0 || pc.y >= (int)out_height) {
                pk = 0u;
                break;
            }
            pk = g_bgKeyPrev.Load(int3(pc, 0));
            if (pk == 0u || abs(asfloat(pk) - lookupD) <= max(0.05f * lookupD, 5e-4f)) {
                break; // converged (or nothing remembered)
            }
            lookupD = asfloat(pk);
        }
        if (pk != 0u && keyD > asfloat(pk) + max(0.10f * keyD, 1e-3f)) {
            bgCol = g_bgColorPrev.Load(int3(pc, 0)).rgb;
            bgD = asfloat(pk);
        }
    }
    g_bgColor[dtid.xy] = float4(bgCol, 1.0f);
    g_bgKey[dtid.xy] = asuint(bgD);
}
