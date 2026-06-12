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

[numthreads(DIBR_TG, DIBR_TG, 1)]
void CSMain(uint3 dtid : SV_DispatchThreadID)
{
    // The fill pass runs entirely in SYNTHESIS (target eye) space (== out at scale 1.0).
    if (dtid.x >= synth_width || dtid.y >= synth_height) return;
    if (g_scatterColor[dtid.xy].a > 0.5f) return; // already covered

    // Disocclusion hole: extend the BACKGROUND side (the deeper of the two
    // nearest valid scanline neighbors), never the occluder - YORO-paper
    // doctrine. Fine 1-px steps cover the common narrow reveals; coarse 4-px
    // strides extend the reach to very-near-object holes (a stride can skip a
    // thin valid run and land slightly farther out - fine for background
    // extension). Anything wider falls back to the source color at this
    // position (flat mono fill).
    const int kFineSearch = 8;
    const int kCoarseStep = 4;
    const int kMaxSearch = 96;
    int lx = -1; uint lkey = 0u;
    int rx = -1; uint rkey = 0u;

    // Keys with the MSB marker were committed by this fill pass itself (other
    // threads, this dispatch) - skip them so hole pixels never adopt other
    // hole pixels' fill as real geometry (that ordering race would shimmer).
    [loop]
    for (int i = 1; i <= kMaxSearch; i += (i < kFineSearch) ? 1 : kCoarseStep) {
        int x = (int)dtid.x - i;
        if (x < 0) break;
        uint k = g_scatterKey[uint2(x, dtid.y)];
        if (k != 0u && (k & 0x80000000u) == 0u && g_scatterColor[uint2(x, dtid.y)].a > 0.5f) { lx = x; lkey = k; break; }
    }
    [loop]
    for (int i = 1; i <= kMaxSearch; i += (i < kFineSearch) ? 1 : kCoarseStep) {
        int x = (int)dtid.x + i;
        if (x >= (int)synth_width) break;
        uint k = g_scatterKey[uint2(x, dtid.y)];
        if (k != 0u && (k & 0x80000000u) == 0u && g_scatterColor[uint2(x, dtid.y)].a > 0.5f) { rx = x; rkey = k; break; }
    }

    // Scanline fill candidate. Smaller key bits = farther (reversed-Z) = the
    // background side of the reveal. Instead of copying the single hole-edge
    // pixel (whose colour is the anti-aliased occluder boundary), average a
    // short run a few pixels into the background; and when BOTH sides are at a
    // similar background depth, inverse-distance blend them - that removes the
    // hard per-row left/right pick that flips between scanlines and shreds the
    // band. When the two sides differ in depth (one IS the occluder) the blend
    // is skipped and we hard-pick the farther/background side, as before.
    float4 c;
    uint fillKey = 0u;
    if (lx >= 0 && rx >= 0) {
        float ld = asfloat(lkey);
        float rd = asfloat(rkey);
        bool useLeft = (lkey <= rkey);
        if (max(ld, rd) <= min(ld, rd) * 1.10f) {
            // Both sides genuinely background: blend the two runs by proximity
            // so the seam where the pick would flip doesn't shred row-to-row.
            float3 lc = SampleBackgroundRun(lx, (int)dtid.y, -1, lkey);
            float3 rc = SampleBackgroundRun(rx, (int)dtid.y, +1, rkey);
            float dl = (float)((int)dtid.x - lx);
            float dr = (float)(rx - (int)dtid.x);
            float wl = dr / max(dl + dr, 1.0f); // nearer neighbour weighs more
            c = float4(lerp(rc, lc, saturate(wl)), 1.0f);
        } else {
            // One side IS the occluder (large depth gap): take only the
            // farther/background run - skips the wasted second background run.
            c = useLeft
                ? float4(SampleBackgroundRun(lx, (int)dtid.y, -1, lkey), 1.0f)
                : float4(SampleBackgroundRun(rx, (int)dtid.y, +1, rkey), 1.0f);
        }
        fillKey = useLeft ? lkey : rkey;
    } else if (lx >= 0) {
        c = float4(SampleBackgroundRun(lx, (int)dtid.y, -1, lkey), 1.0f);
        fillKey = lkey;
    } else if (rx >= 0) {
        c = float4(SampleBackgroundRun(rx, (int)dtid.y, +1, rkey), 1.0f);
        fillKey = rkey;
    } else {
        float2 uv = float2((dtid.x + 0.5f) / (float)synth_width, (dtid.y + 0.5f) / (float)synth_height);
        if (overscan_x > 1.0f) {
            uv.x = 0.5f + (uv.x - 0.5f) / overscan_x; // crop overscanned source to true FOV
        }
        c = float4(g_colorTex.SampleLevel(g_linearSampler, uv, 0).rgb, 1.0f);
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
    if (temporal_enabled > 0.5f && fillKey != 0u) {
        float estDepth = asfloat(fillKey);
        float2 uv = float2((dtid.x + 0.5f) / (float)synth_width, (dtid.y + 0.5f) / (float)synth_height);
        float2 ndc = float2(uv.x * 2.0f - 1.0f, 1.0f - uv.y * 2.0f);
        float4 prev = mul(reproj_target_to_prev, float4(ndc, estDepth, 1.0f));
        float w = (abs(prev.w) > 1e-6f) ? prev.w : 1e-6f;
        float2 pn = prev.xy / w;
        int px = (int)((pn.x * 0.5f + 0.5f) * (float)synth_width);
        int py = (int)((0.5f - pn.y * 0.5f) * (float)synth_height);
        if (px >= 0 && px < (int)synth_width && py >= 0 && py < (int)synth_height) {
            const float tol = max(0.15f * estDepth, 2e-4f);
            float4 h = g_historyColor[uint2(px, py)];
            uint hk = g_historyKey[uint2(px, py)] & 0x7FFFFFFFu; // strip fill marker
            bool validHistory = (h.a > 0.5f && hk != 0u && abs(asfloat(hk) - estDepth) <= tol);
            if (!validHistory) {
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
                    if (nh.a > 0.5f && nk != 0u && abs(asfloat(nk) - estDepth) <= tol) {
                        h = nh;
                        validHistory = true;
                        break;
                    }
                }
            }
            if (validHistory) {
                // temporal_blend is pre-scaled by the caller against the
                // per-frame pose delta: fast motion favors fresh fill.
                c.rgb = lerp(c.rgb, h.rgb, saturate(temporal_blend));
            }
        }
    }

    g_scatterColor[dtid.xy] = float4(c.rgb, 1.0f);
    if (fillKey != 0u) {
        // Commit the adopted background key so next frame's temporal gate can
        // validate this band. Device depths are positive floats, so the MSB is
        // free to mark "filled, not scattered" for the search masks above.
        g_scatterKey[dtid.xy] = fillKey | 0x80000000u;
    }
}