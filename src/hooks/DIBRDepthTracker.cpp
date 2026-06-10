#include <atomic>
#include <cmath>
#include <cstdint>
#include <mutex>
#include <unordered_map>
#include <vector>

#include "DIBRDepthTracker.hpp"

namespace dibr_depth_tracker {
namespace {
using Microsoft::WRL::ComPtr;

struct Candidate {
    ComPtr<ID3D12Resource> resource{};
    uint32_t width{};
    uint32_t height{};
    DXGI_FORMAT format{};
    uint64_t sequence{};
    // Present-window of the last DSV bind (OMSetRenderTargets/BeginRenderPass).
    uint64_t last_bind_present{};
    // Global monotonic order of the last bind. UE renders scene captures /
    // auxiliary views BEFORE the main view, so among live candidates the one
    // bound LATEST in the frame is the main view's scene depth - this is what
    // separates it from a fixed-camera capture depth that is also bound every
    // frame (which made the warp sample a depth that never tracked the HMD).
    uint64_t last_bind_order{};
};

std::mutex g_mtx{};
std::vector<Candidate> g_candidates{};
// DSV descriptor handle -> resource, so bind-time hooks (which only see the
// descriptor) can resolve the candidate. Rebuilt as DSVs are (re)created.
std::unordered_map<SIZE_T, ID3D12Resource*> g_dsv_to_resource{};
uint64_t g_sequence{0};
// Incremented once per presented frame (in select_scene_depth); binds recorded
// during a frame are tagged with the current value.
std::atomic<uint64_t> g_present_seq{1};
// Monotonic counter across every DSV bind (frame-order discriminator).
std::atomic<uint64_t> g_bind_order{1};

// Bounded: depth targets are created rarely (a handful per resolution), so a
// small cap with oldest-first eviction covers resizes without growing.
constexpr size_t kMaxCandidates = 32;
constexpr size_t kMaxDsvMappings = 256;

// vrmod depth_select format tiers (lower = better).
int format_tier(DXGI_FORMAT f) {
    switch (f) {
    case DXGI_FORMAT_R32G8X24_TYPELESS:
    case DXGI_FORMAT_D32_FLOAT_S8X24_UINT:
        return 0;
    case DXGI_FORMAT_R32_TYPELESS:
    case DXGI_FORMAT_D32_FLOAT:
        return 1;
    case DXGI_FORMAT_R24G8_TYPELESS:
    case DXGI_FORMAT_D24_UNORM_S8_UINT:
        return 2;
    case DXGI_FORMAT_R16_TYPELESS:
    case DXGI_FORMAT_D16_UNORM:
        return 3;
    default:
        return 4;
    }
}
} // namespace

void record_dsv(ID3D12Resource* resource, D3D12_CPU_DESCRIPTOR_HANDLE descriptor) {
    (void)descriptor;
    if (resource == nullptr) {
        return;
    }

    // vrmod depth_select rejects: multi-sampled, square aspect (shadow atlas /
    // cube face), and tiny surfaces.
    const auto desc = resource->GetDesc();
    if (desc.SampleDesc.Count > 1 || desc.Width < 256 || desc.Height < 256) {
        return;
    }

    const float aspect = static_cast<float>(desc.Width) / static_cast<float>(desc.Height);
    if (std::fabs(aspect - 1.0f) < 0.05f) {
        return;
    }

    std::scoped_lock _{g_mtx};

    // Keep the descriptor->resource map bounded: descriptor handles get
    // recycled by the game's DSV heaps, so stale entries are overwritten
    // naturally; only wholesale growth needs trimming.
    if (g_dsv_to_resource.size() >= kMaxDsvMappings) {
        g_dsv_to_resource.clear();
    }
    g_dsv_to_resource[descriptor.ptr] = resource;

    for (auto& c : g_candidates) {
        if (c.resource.Get() == resource) {
            c.sequence = ++g_sequence; // refresh recency on DSV recreation
            return;
        }
    }

    if (g_candidates.size() >= kMaxCandidates) {
        size_t oldest = 0;
        for (size_t i = 1; i < g_candidates.size(); ++i) {
            if (g_candidates[i].sequence < g_candidates[oldest].sequence) {
                oldest = i;
            }
        }
        g_candidates.erase(g_candidates.begin() + oldest);
    }

    Candidate c{};
    c.resource = resource;
    c.width = static_cast<uint32_t>(desc.Width);
    c.height = desc.Height;
    c.format = desc.Format;
    c.sequence = ++g_sequence;
    g_candidates.emplace_back(std::move(c));
}

void record_dsv_bind(D3D12_CPU_DESCRIPTOR_HANDLE descriptor) {
    if (descriptor.ptr == 0) {
        return;
    }

    const auto present = g_present_seq.load(std::memory_order_relaxed);

    std::scoped_lock _{g_mtx};

    const auto it = g_dsv_to_resource.find(descriptor.ptr);
    if (it == g_dsv_to_resource.end()) {
        return; // descriptor of a filtered-out / unknown depth target
    }

    for (auto& c : g_candidates) {
        if (c.resource.Get() == it->second) {
            c.last_bind_present = present;
            c.last_bind_order = g_bind_order.fetch_add(1, std::memory_order_relaxed);
            return;
        }
    }
}

Microsoft::WRL::ComPtr<ID3D12Resource> select_scene_depth(uint32_t full_width, uint32_t eye_width, uint32_t height) {
    if (full_width == 0 || eye_width == 0 || height == 0) {
        return nullptr;
    }

    std::scoped_lock _{g_mtx};

    // Advance the per-present liveness window. Binds recorded during the frame
    // just presented carry the pre-increment value; a candidate counts as live
    // when bound within the last two presents (covers recording pipelining).
    const uint64_t present = g_present_seq.fetch_add(1, std::memory_order_relaxed);

    // LIVE candidates first - a stale-but-perfectly-shaped candidate (e.g. a
    // frozen loading-screen depth) must always lose to one the game actually
    // bound this frame. Then exact extent, area, depth format tier, recency.
    const float full_aspect = static_cast<float>(full_width) / static_cast<float>(height);
    const float eye_aspect = static_cast<float>(eye_width) / static_cast<float>(height);

    // A candidate qualifies in either shape family: double-wide (both views
    // packed) or single-eye (single-view rendering allocates SceneDepthZ at
    // the lone view's extent). Dynamic res shrinks within a family.
    const auto shape_ok = [&](const Candidate& c) {
        const float aspect = static_cast<float>(c.width) / static_cast<float>(c.height);
        const bool full_shape = std::fabs(aspect - full_aspect) <= full_aspect * 0.02f &&
                                c.width <= full_width && c.width * 2 >= full_width;
        const bool eye_shape = std::fabs(aspect - eye_aspect) <= eye_aspect * 0.02f &&
                               c.width <= eye_width && c.width * 2 >= eye_width;
        return full_shape || eye_shape;
    };

    const Candidate* best = nullptr;
    bool best_exact = false;
    bool best_live = false;

    for (const auto& c : g_candidates) {
        const bool exact = (c.width == full_width || c.width == eye_width) && c.height == height;
        if (!exact && !shape_ok(c)) {
            continue;
        }

        const bool live = c.last_bind_present + 2 > present;

        if (best == nullptr) {
            best = &c;
            best_exact = exact;
            best_live = live;
            continue;
        }

        if (live != best_live) {
            if (live) {
                best = &c;
                best_exact = exact;
                best_live = true;
            }
            continue;
        }

        // Among live candidates, the LATEST-bound one is the main view's
        // scene depth: UE renders scene captures / auxiliary views first and
        // the main view (with its late translucency/post passes) last. A
        // fixed-camera capture depth is live too but always binds earlier.
        if (live && c.last_bind_order != best->last_bind_order) {
            if (c.last_bind_order > best->last_bind_order) {
                best = &c;
                best_exact = exact;
            }
            continue;
        }

        if (exact != best_exact) {
            if (exact) {
                best = &c;
                best_exact = true;
            }
            continue;
        }

        const uint64_t area = static_cast<uint64_t>(c.width) * c.height;
        const uint64_t best_area = static_cast<uint64_t>(best->width) * best->height;
        if (area > best_area ||
            (area == best_area && (format_tier(c.format) < format_tier(best->format) ||
                (format_tier(c.format) == format_tier(best->format) && c.sequence > best->sequence)))) {
            best = &c;
        }
    }

    return best != nullptr ? best->resource : nullptr;
}
} // namespace dibr_depth_tracker
