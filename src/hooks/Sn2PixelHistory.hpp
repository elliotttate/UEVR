// Sn2PixelHistory.hpp
//
// Pixel history: for any (x, y) pixel in a captured frame, list every draw
// event that covered that pixel.
//
// Approach: at each draw, record (ps_crc, viewport, scissor, rtv_resources)
// into an in-memory ring. Offline / on-demand query maps (x,y) → list of
// matching draws by checking viewport+scissor overlap.
//
// Limitations vs RenderDoc:
//   - We don't do per-pixel stencil tracking (would require shader injection)
//   - "Covered" = pixel is inside viewport rect AND scissor rect
//   - Doesn't account for depth occlusion or alpha blending
//   - Same RT writes from multiple draws are all listed (RD would show only
//     the winner via depth/blend ops)
//
// USAGE
//   UEVR_SN2_PIXEL_HISTORY=1
//   UEVR_SN2_PIXEL_HISTORY_DUMP=C:\tmp\pixel_history.json
//   UEVR_SN2_PIXEL_HISTORY_TRACK_PSOS=0x9d14fcf0,0x37558de4  (optional; default = all)
//   UEVR_SN2_PIXEL_HISTORY_MAX_DRAWS=10000
//
// Use trigger file C:\tmp\pixel_history_query.txt containing "<x>,<y>" to
// query — UEVR writes results to C:\tmp\pixel_history_query_out.json.

#pragma once

#include <atomic>
#include <cstdint>
#include <d3d12.h>
#include <mutex>
#include <string>
#include <unordered_set>
#include <vector>

namespace sn2_pixel_history {

struct DrawRecord {
    uint64_t frame;
    uint64_t event_in_frame;
    uint32_t ps_crc;
    int eye_bucket;
    float vp_x, vp_y, vp_w, vp_h;     // viewport
    LONG sc_left, sc_top, sc_right, sc_bottom;  // scissor
    ID3D12Resource* rtv0;             // primary RT
    uint32_t rtv_count;
};

bool env_enabled();
const std::string& output_path();
uint64_t max_draws();
const std::unordered_set<uint32_t>& track_psos();   // empty = track all

// Called from draw hook.
void note_draw(uint32_t ps_crc, int eye_bucket,
               const D3D12_VIEWPORT* vp,
               const D3D12_RECT* scissor,
               ID3D12Resource* const* rtvs, uint32_t rtv_count);

// On present: check for query trigger file, run lookup, emit JSON.
void on_present();

// Query: returns all draws covering pixel (x, y).
std::vector<DrawRecord> query_pixel(float x, float y);

}  // namespace sn2_pixel_history
