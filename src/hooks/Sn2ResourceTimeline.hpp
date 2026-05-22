// Sn2ResourceTimeline.hpp
//
// Per-resource access timeline. For each tracked resource, records:
//   - write events (PSO, eye, frame, event-in-frame)
//   - read events (PSO, eye, frame, event-in-frame)
//
// Lets us answer "what wrote IntegratedLightScattering on frame N event 7245?"
// without digging through 100MB of log text.
//
// USAGE
//   UEVR_SN2_RES_TIMELINE_DIR=C:\tmp\res_timeline
//   UEVR_SN2_RES_TIMELINE_TRACKED_RESOURCES=0x208e8c97ce0,0x1f9625b7990
//
// Periodically (every 600 frames) flushes per-resource JSON files to the dir.

#pragma once

#include <atomic>
#include <cstdint>
#include <d3d12.h>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace sn2_resource_timeline {

struct Event {
    uint32_t ps_crc;
    uint32_t cs_crc;
    int      eye_bucket;
    uint64_t frame;
    uint64_t event_in_frame;
    enum class Kind : uint8_t { Write, Read } kind;
};

bool env_enabled();
const std::unordered_set<ID3D12Resource*>& tracked_resources_ptrs();
const std::string& output_dir();

// Hook entry: called from draw/dispatch where bindings are known.
void note_access(ID3D12Resource* res,
                 uint32_t ps_crc,
                 uint32_t cs_crc,
                 int eye_bucket,
                 bool is_write);

// Called from Present to flush to disk.
void on_present();

// Register a resource pointer to track at runtime.
void register_track(ID3D12Resource* res);

}  // namespace sn2_resource_timeline
