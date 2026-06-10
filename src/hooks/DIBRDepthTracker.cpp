#include <cmath>
#include <cstdint>
#include <mutex>
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
};

std::mutex g_mtx{};
std::vector<Candidate> g_candidates{};
uint64_t g_sequence{0};

// Bounded: depth targets are created rarely (a handful per resolution), so a
// small cap with oldest-first eviction covers resizes without growing.
constexpr size_t kMaxCandidates = 32;

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

Microsoft::WRL::ComPtr<ID3D12Resource> select_scene_depth(uint32_t width, uint32_t height) {
    if (width == 0 || height == 0) {
        return nullptr;
    }

    std::scoped_lock _{g_mtx};

    // Exact extent match preferred; otherwise accept same-aspect candidates at
    // or below the target (dynamic resolution / TSR renders the scene depth at
    // internal res). Larger area wins, then depth format tier, then recency.
    const float target_aspect = static_cast<float>(width) / static_cast<float>(height);
    const Candidate* best = nullptr;
    bool best_exact = false;

    for (const auto& c : g_candidates) {
        const bool exact = c.width == width && c.height == height;
        if (!exact) {
            const float aspect = static_cast<float>(c.width) / static_cast<float>(c.height);
            if (std::fabs(aspect - target_aspect) > target_aspect * 0.02f) {
                continue;
            }
            if (c.width > width || c.width * 2 < width) {
                continue; // larger than output or below half-res: not the scene depth
            }
        }

        if (best == nullptr) {
            best = &c;
            best_exact = exact;
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
