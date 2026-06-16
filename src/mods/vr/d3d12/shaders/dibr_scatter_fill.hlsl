// AUTO-PATTERNED from dibr_yoro.hlsl's declarations - keep the cbuffer block
// byte-identical across every DIBR kernel (the runtime layout guard checks it).

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
// Last frame's FILLED synthesized eye + its depth keys (ping-ponged by
// DIBRSynthesis) - the R3 temporal hole-fill source. The color is bound as
// an SRV (the fill only READS it; DIBRSynthesis transitions the resource to
// a shader-readable state around the read-only passes).
Texture2D<float4> g_historyColor : register(t3);
RWTexture2D<uint> g_historyKey : register(u4);
// Persistent background layer (last frame's update, this eye's space one
// frame back - same reprojection convention as the stash). Holds the
// FARTHEST surface remembered per pixel across frames: the reveal source of
// last resort when the one-frame stash still had the occluder covering the
// hole. Null until the first update has run.
Texture2D<float4> g_bgColorPrev : register(t5);
Texture2D<uint>   g_bgKeyPrev   : register(t6);
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

// A neighborhood-probe hit must be SOLID: backed by a same-depth covered
// texel one step further from the hole. The silhouette-stretch zone scatters
// ISOLATED occluder samples into reveal bands; trusting a stray as a "side"
// misclassified reveals as interior/same-surface gaps and smeared occluder
// color across them - starving the history paths that hold the real
// content. Strays fail this test and the probe walks on.
bool SolidCoverage(int2 p, int2 away, out uint key)
{
    key = 0u;
    uint k = g_scatterKey[uint2(p)];
    if (k == 0u || (k & 0x80000000u) != 0u || g_scatterColor[uint2(p)].a <= 0.5f) {
        return false;
    }
    int2 p2 = p + away;
    if (p2.x < 0 || p2.x >= (int)synth_width || p2.y < 0 || p2.y >= (int)synth_height) {
        key = k; // image edge backs the sample
        return true;
    }
    uint k2 = g_scatterKey[uint2(p2)];
    if (k2 == 0u || (k2 & 0x80000000u) != 0u || g_scatterColor[uint2(p2)].a <= 0.5f) {
        return false;
    }
    float d = asfloat(k & 0x7FFFFFFFu);
    if (abs(asfloat(k2 & 0x7FFFFFFFu) - d) > max(0.10f * d, 1e-3f)) {
        return false;
    }
    key = k;
    return true;
}

bool RealCovered(int x, int y, out uint key)
{
    key = 0u;
    if (x < 0 || x >= (int)synth_width || y < 0 || y >= (int)synth_height) {
        return false;
    }
    uint k = g_scatterKey[uint2(x, y)];
    if (k == 0u || (k & 0x80000000u) != 0u || g_scatterColor[uint2(x, y)].a <= 0.5f) {
        return false;
    }
    key = k;
    return true;
}

bool BackgroundDepthOk(uint key, uint refKey)
{
    float refDepth = asfloat(refKey);
    float depthTol = max(0.05f * refDepth, 0.01f);
    // Reversed-Z: larger device depth is closer to the eye. For a reveal
    // source we can walk to equal/farther surfaces, but we should not cross
    // back onto the foreground silhouette.
    return asfloat(key) <= refDepth + depthTol;
}

// Average a short run of covered BACKGROUND pixels starting at a scanline
// neighbour and stepping `dir` further AWAY from the hole (always into
// already-covered territory). This dilutes the anti-aliased occluder-edge
// pixel - the colour that otherwise smears across the reveal - and lowers the
// per-row colour variance that reads as a "shredded" band. The run stops the
// moment it hits an uncovered pixel, a fill-marked pixel, or a depth jump
// toward the foreground, so the occluder is never averaged back in.
float3 SampleBackgroundRun(int startX, int y, int dir, uint refKey)
{
    const int kRunTaps = 4;
    float refDepth = asfloat(refKey);
    float depthTol = max(0.05f * refDepth, 0.01f);
    float3 acc = float3(0.0f, 0.0f, 0.0f);
    float wsum = 0.0f;
    [loop]
    for (int t = 0; t < kRunTaps; ++t) {
        int x = startX + dir * t;
        if (x < 0 || x >= (int)synth_width) break;
        uint k = g_scatterKey[uint2(x, y)];
        if (k == 0u || (k & 0x80000000u) != 0u) break;        // uncovered / fill-marked
        if (g_scatterColor[uint2(x, y)].a <= 0.5f) break;     // not covered
        if (asfloat(k) > refDepth + depthTol) break;          // reversed-Z: jumped toward foreground
        float w = 1.0f / (1.0f + (float)t);
        acc += g_scatterColor[uint2(x, y)].rgb * w;
        wsum += w;
    }
    return (wsum > 0.0f) ? (acc / wsum) : g_scatterColor[uint2(startX, y)].rgb;
}

float3 SampleBackgroundRunRows(int startX, int startY, int dir, uint refKey)
{
    float3 acc = float3(0.0f, 0.0f, 0.0f);
    float wsum = 0.0f;

    [unroll]
    for (int oi = 0; oi < 5; ++oi) {
        int oy = (oi == 0) ? 0 : ((oi == 1) ? -1 : ((oi == 2) ? 1 : ((oi == 3) ? -2 : 2)));
        int y = startY + oy;
        uint k;
        if (!RealCovered(startX, y, k) || !BackgroundDepthOk(k, refKey)) {
            continue;
        }

        // Adjacent rows keep thin foliage reveals from becoming horizontal
        // ladders, but the center row still owns the decision.
        float rowWeight = (oy == 0) ? 4.0f : ((abs(oy) == 1) ? 1.5f : 0.75f);
        acc += SampleBackgroundRun(startX, y, dir, refKey) * rowWeight;
        wsum += rowWeight;
    }

    return (wsum > 0.0f) ? (acc / wsum) : SampleBackgroundRun(startX, startY, dir, refKey);
}

bool FindBackgroundOnRow(int x0, int y, int dir, out int bx, out uint bkey)
{
    bx = -1;
    bkey = 0u;

    if (y < 0 || y >= (int)synth_height) {
        return false;
    }

    const int kFineSearch = 8;
    const int kCoarseStep = 4;
    const int kMaxSearch = 96;

    int i = 1;
    int hits = 0;
    [loop]
    while (i <= kMaxSearch) {
        int x = x0 + dir * i;
        if (x < 0 || x >= (int)synth_width) break;

        uint k;
        if (SolidCoverage(int2(x, y), int2(dir, 0), k)) {
            if (bx < 0 || asfloat(k) < asfloat(bkey)) {
                bx = x;
                bkey = k;
            }
            if (++hits >= 4) break;
            i += 6; // hop past this surface before the next census tap
        } else {
            i += (i < kFineSearch) ? 1 : kCoarseStep;
        }
    }

    return bx >= 0;
}

bool FindBackgroundNeighborhood(int2 hole, int dir, out int bx, out int by, out uint bkey)
{
    bx = -1;
    by = -1;
    bkey = 0u;

    [unroll]
    for (int oi = 0; oi < 5; ++oi) {
        int oy = (oi == 0) ? 0 : ((oi == 1) ? -1 : ((oi == 2) ? 1 : ((oi == 3) ? -2 : 2)));
        int rowX = -1;
        uint rowKey = 0u;
        if (FindBackgroundOnRow(hole.x, hole.y + oy, dir, rowX, rowKey)) {
            bx = rowX;
            by = hole.y + oy;
            bkey = rowKey;
            return true;
        }
    }

    return false;
}

[numthreads(DIBR_TG, DIBR_TG, 1)]
void CSMain(uint3 dtid : SV_DispatchThreadID)
{
    // The fill pass runs entirely in SYNTHESIS (target eye) space (== out at scale 1.0).
    if (dtid.x >= synth_width || dtid.y >= synth_height) return;

    // Interleave repair, BEFORE every other path: a magnifying silhouette
    // flank scatters the object as isolated columns, and background (far)
    // samples win the texels between them - "see-through" striping that no
    // HOLE machinery can reach because those texels are COVERED. A texel
    // whose own coverage is far-field but which is flanked by NEAR-depth
    // coverage within 2px on BOTH sides sits INSIDE the object's span: it
    // is the object's missing column. Same for holes in that zone (their
    // sparse columns also fail the solidity gate below by construction).
    {
        const bool selfCovered = g_scatterColor[dtid.xy].a > 0.5f;
        const uint selfRaw = g_scatterKey[dtid.xy];
        const float selfD = selfCovered ? asfloat(selfRaw & 0x7FFFFFFFu) : 0.0f;
        uint nearL = 0u;
        uint nearR = 0u;
        int nearLx = 0;
        int nearRx = 0;
        [unroll]
        for (int o = 1; o <= 2; ++o) {
            int xn = (int)dtid.x - o;
            int xp = (int)dtid.x + o;
            if (nearL == 0u && xn >= 0) {
                uint k = g_scatterKey[uint2(xn, dtid.y)];
                if (k != 0u && (k & 0x80000000u) == 0u && g_scatterColor[uint2(xn, dtid.y)].a > 0.5f &&
                    asfloat(k) > selfD + max(0.10f * asfloat(k), 1e-3f)) { nearL = k; nearLx = xn; }
            }
            if (nearR == 0u && xp < (int)synth_width) {
                uint k = g_scatterKey[uint2(xp, dtid.y)];
                if (k != 0u && (k & 0x80000000u) == 0u && g_scatterColor[uint2(xp, dtid.y)].a > 0.5f &&
                    asfloat(k) > selfD + max(0.10f * asfloat(k), 1e-3f)) { nearR = k; nearRx = xp; }
            }
        }
        if (nearL != 0u && nearR != 0u &&
            abs(asfloat(nearL) - asfloat(nearR)) <= max(0.10f * max(asfloat(nearL), asfloat(nearR)), 1e-3f)) {
            // Same near surface on both flanks: adopt the nearer column.
            const bool takeL = asfloat(nearL) >= asfloat(nearR);
            const int sx = takeL ? nearLx : nearRx;
            g_scatterColor[dtid.xy] = float4(g_scatterColor[uint2(sx, dtid.y)].rgb, 1.0f);
            g_scatterKey[dtid.xy] = (((takeL ? nearL : nearR) & ~0x3u) | 0x1u) | 0x80000000u;
            return;
        }
    }

    if (g_scatterColor[dtid.xy].a > 0.5f) return; // already covered

    // Interior stretch gap vs true reveal: a near surface that MAGNIFIES in
    // the synthesized eye scatters its samples apart, leaving 1-8px gaps
    // INSIDE the object with same-depth coverage on BOTH sides. Those gaps
    // must be repaired from their own surface - routing them through the
    // directional background machinery paints water/logo stripes inside
    // foreground objects, and per-row one-sided picks shred into ladder
    // bands. Only a genuine depth EDGE between the sides falls through to
    // the reveal path below.
    int xl = -1, xr = -1;
    uint kl = 0u, kr = 0u;
    {
        [loop]
        for (int s = 1; s <= 12 && (xl < 0 || xr < 0); ++s) {
            if (xl < 0) {
                int x = (int)dtid.x - s;
                uint k;
                if (x >= 0 && SolidCoverage(int2(x, dtid.y), int2(-1, 0), k)) { xl = x; kl = k; }
            }
            if (xr < 0) {
                int x = (int)dtid.x + s;
                uint k;
                if (x < (int)synth_width && SolidCoverage(int2(x, dtid.y), int2(1, 0), k)) { xr = x; kr = k; }
            }
        }
    }
    // Fill provenance for debug view 9 (low 2 key bits): 0 = scanline /
    // source fallback, 1 = two-sided interpolation, 2 = stash history,
    // 3 = background layer.
    uint prov = 0u;
    float4 c = float4(0.0f, 0.0f, 0.0f, 1.0f);
    uint fillKey = 0u;
    int bx = -1;
    int by = -1;
    uint bkey = 0u;

    if (xl >= 0 && xr >= 0) {
        float dl = asfloat(kl);
        float dr = asfloat(kr);
        float wl = (float)(xr - (int)dtid.x);
        float wr = (float)((int)dtid.x - xl);
        float3 col = (g_scatterColor[uint2(xl, dtid.y)].rgb * wl +
                      g_scatterColor[uint2(xr, dtid.y)].rgb * wr) / max(wl + wr, 1.0f);
        if (abs(dl - dr) <= max(0.10f * max(dl, dr), 1e-3f)) {
            // Same surface on both sides (magnification gap): repair from its
            // own surface and stop - no disocclusion happened here.
            g_scatterColor[dtid.xy] = float4(col, 1.0f);
            g_scatterKey[dtid.xy] = ((((dl < dr) ? kl : kr) & ~0x3u) | 0x1u) | 0x80000000u;
            return;
        }
        // Which side is the FAR surface on? A true disocclusion's background
        // lies along the warp direction (the reveal opens opposite the
        // occluder's shift); finding it on the WRONG side means the gap is
        // interior to a THIN occluder crossed horizontally (a frond) - its
        // content is the OBJECT. Routing those through the reveal machinery
        // painted the water/logo behind the frond INTO it: the teal specks
        // that bloom while turning the head (rotation thins the scatter
        // coverage of thin geometry, multiplying these gaps).
        {
            const int revealDir = (SynthEyeSign() < 0.0f) ? 1 : -1;
            const bool farIsRight = (dr < dl); // reversed-Z: smaller = farther
            const bool farAlongDir = (revealDir > 0) ? farIsRight : !farIsRight;
            if (!farAlongDir) {
                const bool leftNear = (dl > dr);
                float3 nearCol = leftNear ? g_scatterColor[uint2(xl, dtid.y)].rgb
                                          : g_scatterColor[uint2(xr, dtid.y)].rgb;
                g_scatterColor[dtid.xy] = float4(nearCol, 1.0f);
                g_scatterKey[dtid.xy] = (((leftNear ? kl : kr) & ~0x3u) | 0x1u) | 0x80000000u;
                return;
            }
        }
        // Mixed-depth edge gap: a REVEAL band. A two-sided gradient looks
        // smooth in one row but boils against the real frame when the head
        // moves. Seed the reveal from the known far side, then let the
        // directional/background-history path do the actual fill.
        const bool leftIsFar = (dl < dr); // reversed-Z: smaller = farther
        bx = leftIsFar ? xl : xr;
        by = (int)dtid.y;
        bkey = leftIsFar ? kl : kr;
        fillKey = bkey;
    }

    if (xl < 0 || xr < 0) {
        // Thin VERTICAL geometry (fronds, coral stalks) drops sparse texels
        // under head rotation: horizontally the gap sees one side or none,
        // and the reveal machinery would paint the background from BEHIND
        // the object into it - the teal specks that bloom while turning.
        // The structure is vertical, so probe for same-surface coverage
        // above and below and repair from the object itself. True reveal
        // bands extend vertically too, so their vertical probes land on
        // holes and fail harmlessly through to the reveal path.
        int yu = -1, yd = -1;
        uint ku = 0u, kd = 0u;
        [loop]
        for (int s = 1; s <= 8 && (yu < 0 || yd < 0); ++s) {
            if (yu < 0) {
                int yy = (int)dtid.y - s;
                uint k;
                if (yy >= 0 && SolidCoverage(int2(dtid.x, yy), int2(0, -1), k)) { yu = yy; ku = k; }
            }
            if (yd < 0) {
                int yy = (int)dtid.y + s;
                uint k;
                if (yy < (int)synth_height && SolidCoverage(int2(dtid.x, yy), int2(0, 1), k)) { yd = yy; kd = k; }
            }
        }
        if (yu >= 0 && yd >= 0) {
            float du = asfloat(ku);
            float dd = asfloat(kd);
            if (abs(du - dd) <= max(0.10f * max(du, dd), 1e-3f)) {
                float wu = (float)(yd - (int)dtid.y);
                float wd = (float)((int)dtid.y - yu);
                float3 vcol = (g_scatterColor[uint2(dtid.x, yu)].rgb * wu +
                               g_scatterColor[uint2(dtid.x, yd)].rgb * wd) / max(wu + wd, 1.0f);
                g_scatterColor[dtid.xy] = float4(vcol, 1.0f);
                g_scatterKey[dtid.xy] = ((((du < dd) ? ku : kd) & ~0x3u) | 0x1u) | 0x80000000u;
                return;
            }
        }
    }

    // Disocclusion hole: a reveal opens on the side of a foreground object
    // OPPOSITE its warp direction, so the background that belongs in it lies
    // on ONE known side - along the eye baseline, +x when synthesizing the
    // RIGHT eye and -x for the LEFT. Searching only that side can never adopt
    // the occluder's color; the old bidirectional search depth-picked (and on
    // near-equal keys BLENDED) both sides, and whenever the occluder side won
    // it painted an object-colored ghost band that swapped sides with the AFW
    // eye alternation. PureDark's fill is likewise directional. Fine 1-px
    // steps cover the common narrow reveals; coarse 4-px strides extend the
    // reach to very-near-object holes (a stride can skip a thin valid run and
    // land slightly farther out - fine for background extension). No hit
    // (image edge, peripheral gate) falls back to the source color at this
    // position (flat mono fill, real content where the temporal gate passes).
    const int dir = (SynthEyeSign() < 0.0f) ? 1 : -1; // toward the background side

    // Peripheral gate (PD doctrine): reveals far from the view center sit in
    // the lens periphery where the compose's edge guard already compresses
    // the result; the flat source fill is indistinguishable there. Saves the
    // walk and avoids stretching background across the synthesized eye's
    // no-source outer band.
    float2 holeUv = float2((dtid.x + 0.5f) / (float)synth_width, (dtid.y + 0.5f) / (float)synth_height);
    const bool central = length(holeUv - 0.5f) <= 0.65f;

    // Keys with the MSB marker were committed by this fill pass itself (other
    // threads, this dispatch) - skip them so hole pixels never adopt other
    // hole pixels' fill as real geometry (that ordering race would shimmer).
    if (central && bx < 0) {
        // Depth-aware: at thin-object reveals (plant fronds, railings) the
        // FIRST covered texel along the walk is often the NEXT occluder
        // strand - foreground, not the background the reveal exposes -
        // and first-hit fill paints the band with occluder color ("no
        // filling" look). Census up to three DISTINCT surfaces (hopping a
        // few px past each hit so a strand's run counts once) and keep the
        // FARTHEST (smallest reversed-Z key): reveals expose background by
        // definition.
        FindBackgroundNeighborhood(int2((int)dtid.x, (int)dtid.y), dir, bx, by, bkey);
    }

    if (bx >= 0) {
        // Average a short run a few pixels INTO the background (stepping
        // away from the hole) instead of copying the single hole-edge
        // pixel, whose color is the anti-aliased boundary.
        c = float4(SampleBackgroundRunRows(bx, (by >= 0) ? by : (int)dtid.y, dir, bkey), 1.0f);
        fillKey = bkey;
    } else {
        float2 srcUv = holeUv;
        if (overscan_x > 1.0f) {
            srcUv.x = 0.5f + (srcUv.x - 0.5f) / overscan_x; // crop overscanned source to true FOV
        }
        c = float4(g_colorTex.SampleLevel(g_linearSampler, srcUv, 0).rgb, 1.0f);
    }

    // R3 temporal reuse: reproject this hole into LAST frame's synthesized eye
    // (camera-delta matrix) and EMA-blend its content in when the stored depth
    // agrees. The fill pass commits its keys (marker bit, below), so a
    // persistent disocclusion band carries a valid history key and the blend
    // chain latches - without that committed key the gate could never pass in
    // the very bands it exists to stabilize. Depth validation rejects history
    // the camera-delta matrix can't explain (engine-side locomotion, animated
    // content); the 0.85 blend damps per-frame scanline boil ~7x while still
    // converging in a few frames so animated content doesn't freeze stale.
    if (temporal_enabled > 0.5f) {
        // Reproject at the adopted background depth, or at the far plane
        // (device 0, reversed-Z) when the directional search missed - for a
        // one-frame camera delta the reprojection offset barely depends on
        // depth, and a reveal's true content is background by definition.
        const bool haveKey = (fillKey != 0u);
        float estDepth = haveKey ? asfloat(fillKey) : 0.0f;
        float2 uv = float2((dtid.x + 0.5f) / (float)synth_width, (dtid.y + 0.5f) / (float)synth_height);
        float2 ndc = float2(uv.x * 2.0f - 1.0f, 1.0f - uv.y * 2.0f);
        // Keyless holes (outer no-source band, unscattered sky) iterate the
        // fetch once at the FOUND stash depth: a single far-plane reproject
        // misses near content by its full parallax, landing the fetch on
        // unrelated background and painting it over the band (the eaten
        // outer-edge geometry that alternates against the real frames).
        float lookupD = estDepth;
        int px = -1;
        int py = -1;
        [unroll]
        for (int it = 0; it < 2; ++it) {
            float4 prev = mul(reproj_target_to_prev, float4(ndc, lookupD, 1.0f));
            float w = (abs(prev.w) > 1e-6f) ? prev.w : 1e-6f;
            float2 pn = prev.xy / w;
            px = (int)((pn.x * 0.5f + 0.5f) * (float)synth_width);
            py = (int)((0.5f - pn.y * 0.5f) * (float)synth_height);
            if (haveKey || px < 0 || px >= (int)synth_width || py < 0 || py >= (int)synth_height) {
                break; // keyed fetches stay single-tap (estDepth is trusted)
            }
            uint ik = g_historyKey[uint2(px, py)] & 0x7FFFFFFFu;
            if (ik == 0u || abs(asfloat(ik) - lookupD) <= max(0.05f * lookupD, 5e-4f)) {
                break;
            }
            lookupD = asfloat(ik);
        }
        // Hybrid DIBR has a live target-eye render for close geometry. Letting
        // keyless outer bands adopt old synthesized history paints stale
        // wrong-parallax content over that layer, which is the blue strip in
        // debug view 9. Keep keyed reveals stabilized, but leave never-covered
        // pixels honest so compose can use the target-eye fallback.
        const bool allowKeylessHistory = near_field_strength < 0.5f || haveKey;
        if (allowKeylessHistory && px >= 0 && px < (int)synth_width && py >= 0 && py < (int)synth_height) {
            const float tol = max(0.15f * estDepth, 2e-4f);
            float4 h = g_historyColor[uint2(px, py)];
            uint hk = g_historyKey[uint2(px, py)] & 0x7FFFFFFFu; // strip fill marker
            // AFW: the stash is a REAL render of this very eye, so the only
            // wrong content it can offer at a reveal is the OCCLUDER at its
            // old position - accept anything background-side of the
            // occluder/background midpoint (reversed-Z: smaller = farther).
            // The symmetric |diff|<=tol gate kept rejecting real reveal
            // content: with a far-field adopted key the relative tolerance
            // collapses (open water at device ~1e-4 vs the rock arch at
            // ~5e-3), so history lost to per-row scanline fill - the
            // shredded ladder band.
            float occluderDepth = max((xl >= 0) ? asfloat(kl) : 0.0f,
                                      (xr >= 0) ? asfloat(kr) : 0.0f);
            float acceptCeil = 0.5f * (occluderDepth + estDepth);
            bool depthOk = (temporal_enabled > 1.5f)
                ? (occluderDepth <= 0.0f || asfloat(hk) <= acceptCeil)
                : (!haveKey || abs(asfloat(hk) - estDepth) <= tol);
            bool validHistory = (h.a > 0.5f && hk != 0u && depthOk);
            if (!validHistory && haveKey) {
                // Rotation rounding often lands one texel off a valid history
                // pixel; probe the 3x3 ring before giving up on history.
                const int2 kRing[8] = {
                    int2(-1, 0), int2(1, 0), int2(0, -1), int2(0, 1),
                    int2(-1, -1), int2(1, -1), int2(-1, 1), int2(1, 1)
                };
                [loop]
                for (int n = 0; n < 8; ++n) {
                    int nx = px + kRing[n].x;
                    int ny = py + kRing[n].y;
                    if (nx < 0 || nx >= (int)synth_width || ny < 0 || ny >= (int)synth_height) continue;
                    float4 nh = g_historyColor[uint2(nx, ny)];
                    uint nk = g_historyKey[uint2(nx, ny)] & 0x7FFFFFFFu;
                    bool nOk = (temporal_enabled > 1.5f)
                        ? (occluderDepth <= 0.0f || asfloat(nk) <= acceptCeil)
                        : (abs(asfloat(nk) - estDepth) <= tol);
                    if (nh.a > 0.5f && nk != 0u && nOk) {
                        h = nh;
                        hk = nk;
                        validHistory = true;
                        break;
                    }
                }
            }
            // Background-layer fallback: when the one-frame stash has nothing
            // valid (the occluder covered this reveal in the LAST frame too -
            // exactly what happens mid-sway), consult the persistent
            // background memory. It accumulates the farthest-seen surface per
            // pixel across many frames, so the reveal's true background is
            // usually remembered even when no recent render saw it. The
            // lookup iterates once at the remembered depth (background
            // parallax, not the adopted key's).
            if (!validHistory && temporal_enabled > 1.5f) {
                uint bw, bh;
                g_bgKeyPrev.GetDimensions(bw, bh);
                if (bw != 0u) {
                    float lookupD = estDepth;
                    int2 pc = int2(-1, -1);
                    uint bk = 0u;
                    [unroll]
                    for (int it = 0; it < 2; ++it) {
                        float4 bprev = mul(reproj_target_to_prev, float4(ndc, lookupD, 1.0f));
                        float bw2 = (abs(bprev.w) > 1e-6f) ? bprev.w : 1e-6f;
                        float2 bpn = bprev.xy / bw2;
                        pc = int2((int)((bpn.x * 0.5f + 0.5f) * (float)synth_width),
                                  (int)((0.5f - bpn.y * 0.5f) * (float)synth_height));
                        if (pc.x < 0 || pc.x >= (int)synth_width || pc.y < 0 || pc.y >= (int)synth_height) {
                            bk = 0u;
                            break;
                        }
                        bk = g_bgKeyPrev.Load(int3(pc, 0));
                        if (bk == 0u || abs(asfloat(bk) - lookupD) <= max(0.05f * lookupD, 5e-4f)) {
                            break;
                        }
                        lookupD = asfloat(bk);
                    }
                    if (bk != 0u && (occluderDepth <= 0.0f || asfloat(bk) <= acceptCeil)) {
                        h = float4(g_bgColorPrev.Load(int3(pc, 0)).rgb, 1.0f);
                        hk = bk;
                        validHistory = true;
                        prov = 3u;
                    }
                }
            }
            if (validHistory) {
                if (prov == 0u) {
                    prov = 2u; // stash (direct or ring-probe) accepted
                }
                if (temporal_enabled > 1.5f) {
                    // AFW: the history is last frame's REAL render of THIS
                    // eye - the reveal was actually rendered there one frame
                    // ago, through an exact full-camera-delta matrix. Real
                    // content REPLACES the synthetic scanline fill outright
                    // (PD CombinedWarping doctrine); there is no feedback
                    // risk because the stash never contains fill output.
                    c.rgb = h.rgb;
                } else {
                    // Plain scatter: history is the previous fill output -
                    // EMA blend (caller pre-scales temporal_blend against the
                    // pose delta so fast motion favors fresh fill).
                    c.rgb = lerp(c.rgb, h.rgb, saturate(temporal_blend));
                }
                if (!haveKey) {
                    // Adopt the history's real depth as this band's key so
                    // next frame's gates can validate it.
                    fillKey = hk;
                }
            }
        }
    }

    g_scatterColor[dtid.xy] = float4(c.rgb, 1.0f);
    if (fillKey != 0u) {
        // Commit the adopted background key so next frame's temporal gate can
        // validate this band. Device depths are positive floats, so the MSB is
        // free to mark "filled, not scattered" for the search masks above;
        // the low 2 bits carry the provenance for debug view 9.
        g_scatterKey[dtid.xy] = ((fillKey & ~0x3u) | (prov & 0x3u)) | 0x80000000u;
    }
}
