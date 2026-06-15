#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <spdlog/spdlog.h>

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
    // Bind count within the current present window. The presented frame's
    // scene depth accumulates a full frame of binds (prepass, basepass,
    // decals, translucency, ...) while a pipelined next-frame buffer (RDG
    // ping-pongs SceneDepthZ between two pooled textures every frame) or an
    // auxiliary capture depth only collects a handful - picking by raw
    // "latest bind" alternated onto the next-frame buffer, whose GPU contents
    // are one frame STALE, doubling/twitching the warp during motion.
    uint64_t bind_window{};
    uint32_t bind_count{};
    // Bind order of the FIRST bind in the current window. The presented
    // frame's buffer starts binding right after the previous present; a
    // pipelined next-frame buffer starts mid-window. When two candidates have
    // comparable counts (deep CPU-ahead recording), the earliest starter is
    // the presented frame's depth.
    uint64_t window_first_bind{};
};

std::mutex g_mtx{};
std::vector<Candidate> g_candidates{};
// DSV descriptor handle -> resource, so bind-time hooks (which only see the
// descriptor) can resolve the candidate. Rebuilt as DSVs are (re)created.
std::unordered_map<SIZE_T, ID3D12Resource*> g_dsv_to_resource{};
// Liveness recording is armed by the first consumer call (select_scene_depth
// or the AFW setters). Until then record_dsv_bind is a single relaxed atomic
// load — the bind hook is installed in EVERY D3D12 game, and at defaults
// (DIBR off) nothing reads the liveness data, so it must not take g_mtx on
// the hot OMSetRenderTargets/BeginRenderPass path. The candidate set itself
// still populates at DSV creation, so arming mid-session works; the first
// armed select falls back to stale candidates for one frame by design.
std::atomic<bool> g_liveness_armed{false};
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

// === Bind census (translucency forensics) ===
// Creation-time view shapes so the census never calls GetDesc on a
// possibly-dead resource. Resource pointers are used as opaque keys only.
struct ViewInfo {
    void* resource{};
    uint32_t width{};
    uint32_t height{};
    DXGI_FORMAT format{};
    uint32_t dsv_flags{}; // D3D12_DSV_FLAGS (0 for RTVs / default views)
};

struct CensusEntry {
    SIZE_T rtv0{};
    uint32_t rtv_count{};
    SIZE_T dsv{};
};

constexpr size_t kMaxViewMappings = 4096;
constexpr size_t kMaxCensusEntries = 1024;

std::unordered_map<SIZE_T, ViewInfo> g_rtv_views{};
std::unordered_map<SIZE_T, ViewInfo> g_dsv_views{};
std::vector<CensusEntry> g_census{};
std::atomic<bool> g_census_armed{false};

// Bind-chain health: counts where the signature chain dies in a session
// (resolve = descriptor missing from the creation-observed view maps - e.g.
// the target was created before the device hooks installed on a warm boot;
// qualify = shape/flags mismatch). Read by bind_health_report().
std::atomic<uint64_t> g_health_vel_calls{0};
std::atomic<uint64_t> g_health_vel_resolve_miss{0};
std::atomic<uint64_t> g_health_vel_qualify_fail{0};
std::atomic<uint64_t> g_health_afw_calls{0};
std::atomic<uint64_t> g_health_afw_resolve_miss{0};
std::atomic<uint64_t> g_health_afw_qualify_fail{0};
std::atomic<uint64_t> g_health_afw_seq_push{0};
std::atomic<uint64_t> g_view_map_evictions{0};

// Evict instead of clearing wholesale: a full clear() silently destroyed the
// long-lived SceneColor/velocity entries (created once at VR engagement,
// never recreated), killing every bind-signature consumer for the rest of
// the session once descriptor churn hit the cap.
template <typename Map> void evict_quarter(Map& map) {
    const size_t target = map.size() - map.size() / 4;
    for (auto it = map.begin(); it != map.end() && map.size() > target;) {
        it = map.erase(it);
    }
    g_view_map_evictions.fetch_add(1, std::memory_order_relaxed);
}
long long g_census_last_ms{0};

bool census_enabled() {
    static const bool enabled = []() {
        const char* v = std::getenv("UEVR_DIBR_BIND_CENSUS");
        return v != nullptr && v[0] == '1';
    }();
    return enabled;
}

bool probe_enabled() {
    static const bool enabled = []() {
        const char* v = std::getenv("UEVR_DIBR_PRETRANS_DUMP");
        return v != nullptr && v[0] == '1';
    }();
    return enabled;
}

// AFW depth snapshots also need the view maps (set dynamically by the
// synthesis pass each frame, and statically from the env so views created
// before the first present are captured when AFW is requested at launch).
std::atomic<bool> g_afw_depth_enabled{false};

// Armed from the PERSISTED CONFIG (not the env) so config/UI-driven AFW also
// records the view maps from the first qualifying view creation. Distinct from
// g_afw_depth_enabled so it does NOT enable the opt-in per-frame depth snapshot.
std::atomic<bool> g_view_tracking_forced{false};

bool afw_requested_via_env() {
    static const bool requested = []() {
        const char* v = std::getenv("UEVR_DIBR");
        return v != nullptr && (std::strcmp(v, "afw") == 0 || std::strcmp(v, "alternate") == 0);
    }();
    return requested;
}

// The view maps feed the census, the probe, and the AFW depth snapshots.
bool view_tracking_enabled() {
    return census_enabled() || probe_enabled() || afw_requested_via_env() ||
           g_afw_depth_enabled.load(std::memory_order_relaxed) ||
           g_view_tracking_forced.load(std::memory_order_relaxed);
}

long long now_ms() {
    return static_cast<long long>(GetTickCount64());
}

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

void record_dsv(ID3D12Resource* resource, D3D12_CPU_DESCRIPTOR_HANDLE descriptor,
    const D3D12_DEPTH_STENCIL_VIEW_DESC* view_desc) {
    if (resource == nullptr) {
        return;
    }

    const auto desc = resource->GetDesc();

    // Census view info records EVERY DSV (shadow atlases included) - the
    // census is a frame-structure x-ray, not a scene-depth selector.
    if (view_tracking_enabled()) {
        std::scoped_lock _{g_mtx};
        if (g_dsv_views.size() >= kMaxViewMappings) {
            evict_quarter(g_dsv_views);
        }
        auto& vi = g_dsv_views[descriptor.ptr];
        vi.resource = resource;
        vi.width = static_cast<uint32_t>(desc.Width);
        vi.height = desc.Height;
        vi.format = desc.Format;
        vi.dsv_flags = (view_desc != nullptr) ? static_cast<uint32_t>(view_desc->Flags) : 0u;
    }

    // vrmod depth_select rejects: multi-sampled, EXACTLY square targets, and
    // tiny surfaces. Shadow atlases and cube faces are exactly square; the
    // previous +/-5% aspect BAND also swallowed Quest 3's near-square
    // 1680x1760 single-view eye depth -> zero candidates -> the warp fell
    // back to a frame-stale depth copy, which under AFW alternation is the
    // OTHER eye's depth = whole-image two-position oscillation.
    if (desc.SampleDesc.Count > 1 || desc.Width < 256 || desc.Height < 256) {
        return;
    }

    if (desc.Width == desc.Height) {
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

void record_rtv(ID3D12Resource* resource, D3D12_CPU_DESCRIPTOR_HANDLE descriptor) {
    if (!view_tracking_enabled() || resource == nullptr || descriptor.ptr == 0) {
        return;
    }

    const auto desc = resource->GetDesc();

    // Motion-vector discovery probe: UE renders object motion into a velocity
    // GBuffer - FVelocityRendering::GetFormat: PF_A16B16G16R16
    // (R16G16B16A16_UNORM, velocity-depth in zw) on Lumen/ray-tracing
    // platforms like SN2, PF_G16R16 (R16G16_UNORM) otherwise. Encoding
    // (Common.ush EncodeVelocityToTexture): xy = V.xy * 0.2495 + 32767/65535,
    // and pixels with NO object motion keep the CLEAR value (0,0) - so a
    // binary "animated?" gate needs no decode. Log each velocity-shaped
    // render target ONCE so the AFW temporal gates can be wired to the real
    // buffer identity (PureDark gates his temporal blend on |MV| ~0.005;
    // ours currently has no MV input at all).
    if ((desc.Format == DXGI_FORMAT_R16G16B16A16_UNORM || desc.Format == DXGI_FORMAT_R16G16_UNORM ||
            desc.Format == DXGI_FORMAT_R16G16_FLOAT || desc.Format == DXGI_FORMAT_R32G32_FLOAT) &&
        desc.Width >= 256 && desc.Height >= 256 && desc.SampleDesc.Count == 1) {
        static std::mutex s_vel_mtx{};
        static std::unordered_set<ID3D12Resource*> s_vel_seen{};
        std::scoped_lock _{s_vel_mtx};
        if (s_vel_seen.size() < 64 && s_vel_seen.insert(resource).second) {
            spdlog::info("[DIBR] velocity-shaped RTV observed: {:p} {}x{} fmt {}",
                static_cast<void*>(resource), desc.Width, desc.Height, static_cast<int>(desc.Format));
        }
    }

    std::scoped_lock _{g_mtx};
    if (g_rtv_views.size() >= kMaxViewMappings) {
        evict_quarter(g_rtv_views);
    }
    auto& vi = g_rtv_views[descriptor.ptr];
    vi.resource = resource;
    vi.width = static_cast<uint32_t>(desc.Width);
    vi.height = desc.Height;
    vi.format = desc.Format;
    vi.dsv_flags = 0;
}

void record_census_bind(const SIZE_T* rtvs, uint32_t rtv_count, SIZE_T dsv) {
    if (!g_census_armed.load(std::memory_order_relaxed)) {
        return;
    }

    std::scoped_lock _{g_mtx};
    if (g_census.size() >= kMaxCensusEntries) {
        return;
    }
    CensusEntry e{};
    e.rtv0 = (rtvs != nullptr && rtv_count > 0) ? rtvs[0] : 0;
    e.rtv_count = rtv_count;
    e.dsv = dsv;
    g_census.emplace_back(e);
}

std::string take_census_report() {
    if (!census_enabled()) {
        return {};
    }

    std::scoped_lock _{g_mtx};

    if (g_census_armed.load(std::memory_order_relaxed)) {
        // A full present window has been collected - format and disarm.
        g_census_armed.store(false, std::memory_order_relaxed);
        g_census_last_ms = now_ms();

        std::string out;
        out.reserve(8192);
        char line[256];
        std::snprintf(line, sizeof(line), "bind census: %zu binds\n", g_census.size());
        out += line;

        const auto describe = [&](SIZE_T key, const std::unordered_map<SIZE_T, ViewInfo>& views, const char* tag) {
            if (key == 0) {
                std::snprintf(line, sizeof(line), " %s=none", tag);
                out += line;
                return;
            }
            const auto it = views.find(key);
            if (it == views.end()) {
                std::snprintf(line, sizeof(line), " %s=?", tag);
                out += line;
                return;
            }
            const auto& vi = it->second;
            std::snprintf(line, sizeof(line), " %s=%p %ux%u f%d", tag, vi.resource, vi.width, vi.height,
                static_cast<int>(vi.format));
            out += line;
            if (vi.dsv_flags != 0) {
                std::snprintf(line, sizeof(line), " ro=%u", vi.dsv_flags);
                out += line;
            }
        };

        size_t i = 0;
        while (i < g_census.size()) {
            // Run-length collapse of consecutive identical binds.
            size_t run = 1;
            while (i + run < g_census.size() &&
                   g_census[i + run].rtv0 == g_census[i].rtv0 &&
                   g_census[i + run].dsv == g_census[i].dsv &&
                   g_census[i + run].rtv_count == g_census[i].rtv_count) {
                ++run;
            }

            std::snprintf(line, sizeof(line), "[%03zu]", i);
            out += line;
            describe(g_census[i].rtv0, g_rtv_views, "rtv0");
            if (g_census[i].rtv_count > 1) {
                std::snprintf(line, sizeof(line), " n=%u", g_census[i].rtv_count);
                out += line;
            }
            describe(g_census[i].dsv, g_dsv_views, "dsv");
            if (run > 1) {
                std::snprintf(line, sizeof(line), " x%zu", run);
                out += line;
            }
            out += '\n';
            i += run;
        }

        g_census.clear();
        return out;
    }

    // Re-arm every 10 s so the census tracks scene changes without spamming.
    if (now_ms() - g_census_last_ms > 10000) {
        g_census.clear();
        g_census_armed.store(true, std::memory_order_relaxed);
    }
    return {};
}

void record_dsv_bind(D3D12_CPU_DESCRIPTOR_HANDLE descriptor) {
    if (!g_liveness_armed.load(std::memory_order_relaxed) || descriptor.ptr == 0) {
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
            const auto order = g_bind_order.fetch_add(1, std::memory_order_relaxed);
            if (c.bind_window != present) {
                c.bind_window = present;
                c.bind_count = 0;
                c.window_first_bind = order;
            }
            ++c.bind_count;
            c.last_bind_present = present;
            c.last_bind_order = order;
            return;
        }
    }
}

Microsoft::WRL::ComPtr<ID3D12Resource> select_scene_depth(uint32_t full_width, uint32_t eye_width, uint32_t height) {
    if (full_width == 0 || eye_width == 0 || height == 0) {
        return nullptr;
    }

    // A consumer exists — start recording bind liveness from here on.
    g_liveness_armed.store(true, std::memory_order_relaxed);

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
    const auto dim_delta = [](uint32_t a, uint32_t b) {
        return a > b ? a - b : b - a;
    };
    const auto dim_slop = [](uint32_t v, uint32_t floor) {
        return std::max<uint32_t>(floor, v / 100u);
    };
    const uint32_t full_width_slop = dim_slop(full_width, 8u);
    const uint32_t eye_width_slop = dim_slop(eye_width, 4u);
    const uint32_t height_slop = dim_slop(height, 4u);

    // A candidate qualifies in either shape family: double-wide (both views
    // packed) or single-eye (single-view rendering allocates SceneDepthZ at
    // the lone view's extent). Dynamic res shrinks within a family. MetaXR's
    // D3D12-on-Vulkan path can report the DXGI/UI extent a few pixels smaller
    // than the actual UE scene/depth targets, so permit a tiny overshoot.
    const auto shape_ok = [&](const Candidate& c) {
        const float aspect = static_cast<float>(c.width) / static_cast<float>(c.height);
        const bool full_shape = std::fabs(aspect - full_aspect) <= full_aspect * 0.02f &&
                                c.width <= full_width + full_width_slop &&
                                c.height <= height + height_slop &&
                                c.width * 2 >= full_width;
        const bool eye_shape = std::fabs(aspect - eye_aspect) <= eye_aspect * 0.02f &&
                               c.width <= eye_width + eye_width_slop &&
                               c.height <= height + height_slop &&
                               c.width * 2 >= eye_width;
        return full_shape || eye_shape;
    };

    const Candidate* best = nullptr;
    bool best_exact = false;
    bool best_live = false;

    for (const auto& c : g_candidates) {
        const bool exact =
            ((dim_delta(c.width, full_width) <= full_width_slop) ||
             (dim_delta(c.width, eye_width) <= eye_width_slop)) &&
            dim_delta(c.height, height) <= height_slop;
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

        // Among live candidates, prefer the one the PRESENTED frame actually
        // rendered with: it accumulated a full frame of depth binds in the
        // window that just ended, while a pipelined next-frame buffer or an
        // auxiliary capture depth only has a handful. Tie-break by latest
        // bind order (scene captures render before the main view).
        if (live) {
            const uint32_t c_count = (c.bind_window == present) ? c.bind_count : 0u;
            const uint32_t b_count = (best->bind_window == present) ? best->bind_count : 0u;
            // Comparable counts (deep CPU-ahead: the next frame's recording may
            // be nearly complete at present time): the EARLIEST window starter
            // is the presented frame's depth. Lopsided counts: more binds wins
            // (full frame of depth passes vs a capture depth or a just-started
            // next-frame buffer).
            const bool comparable = c_count > 0 && b_count > 0 &&
                                    c_count < b_count * 2 && b_count < c_count * 2;
            if (comparable) {
                if (c.window_first_bind != best->window_first_bind) {
                    if (c.window_first_bind < best->window_first_bind) {
                        best = &c;
                        best_exact = exact;
                    }
                    continue;
                }
            } else if (c_count != b_count) {
                if (c_count > b_count) {
                    best = &c;
                    best_exact = exact;
                }
                continue;
            }
            if (c.last_bind_order != best->last_bind_order) {
                if (c.last_bind_order > best->last_bind_order) {
                    best = &c;
                    best_exact = exact;
                }
                continue;
            }
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

std::string bind_health_report() {
    size_t rtv_n = 0;
    size_t dsv_n = 0;
    {
        std::scoped_lock _{g_mtx};
        rtv_n = g_rtv_views.size();
        dsv_n = g_dsv_views.size();
    }
    char buf[320]{};
    std::snprintf(buf, sizeof(buf),
        "views rtv=%zu dsv=%zu evict=%llu | afw_bind calls=%llu resolve_miss=%llu qualify_fail=%llu seq_push=%llu | vel_bind calls=%llu resolve_miss=%llu qualify_fail=%llu",
        rtv_n, dsv_n,
        (unsigned long long)g_view_map_evictions.load(std::memory_order_relaxed),
        (unsigned long long)g_health_afw_calls.load(std::memory_order_relaxed),
        (unsigned long long)g_health_afw_resolve_miss.load(std::memory_order_relaxed),
        (unsigned long long)g_health_afw_qualify_fail.load(std::memory_order_relaxed),
        (unsigned long long)g_health_afw_seq_push.load(std::memory_order_relaxed),
        (unsigned long long)g_health_vel_calls.load(std::memory_order_relaxed),
        (unsigned long long)g_health_vel_resolve_miss.load(std::memory_order_relaxed),
        (unsigned long long)g_health_vel_qualify_fail.load(std::memory_order_relaxed));
    return buf;
}

std::string describe_candidates() {
    std::scoped_lock _{g_mtx};
    const uint64_t present = g_present_seq.load(std::memory_order_relaxed);
    std::string out;
    for (const auto& c : g_candidates) {
        if (c.last_bind_present + 4 <= present) {
            continue; // long stale; not interesting
        }
        char buf[160];
        std::snprintf(buf, sizeof(buf), "[0x%llx %ux%u f%u cnt=%u first=%llu last=%llu win=%llu] ",
            static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(c.resource.Get())),
            c.width, c.height, static_cast<unsigned>(c.format), c.bind_count,
            static_cast<unsigned long long>(c.window_first_bind),
            static_cast<unsigned long long>(c.last_bind_order),
            static_cast<unsigned long long>(c.bind_window));
        out += buf;
    }
    return out;
}
// === Translucency probe ===
// Forensics tool AND the future capture mechanism: copies the SceneColor
// resource at each qualifying bind of one armed frame. The copy is recorded
// into the game's own command list at bind time, so it executes exactly
// before that pass's draws regardless of which thread recorded it.
namespace {
struct ProbeSlot {
    ComPtr<ID3D12Resource> texture{};
    void* source{};
    uint32_t width{};
    uint32_t height{};
    DXGI_FORMAT format{};
    uint32_t rtv_count{};
    uint32_t dsv_flags{};
    bool filled{};
};

constexpr size_t kProbeSlots = 8;
ProbeSlot g_probe_slots[kProbeSlots]{};
std::atomic<bool> g_probe_armed{false};
std::atomic<uint32_t> g_probe_next{0};
long long g_probe_last_ms{0};
int g_probe_flush_delay{0};
uint32_t g_probe_round{0};

// Half-float -> 8-bit sRGB-ish for the PPM dump (gamma 1/2.2, no tonemap).
uint8_t half_to_u8(uint16_t h) {
    const uint32_t sign = (h >> 15) & 1u;
    const uint32_t exp = (h >> 10) & 0x1Fu;
    const uint32_t man = h & 0x3FFu;
    float f = 0.0f;
    if (exp == 0) {
        f = static_cast<float>(man) * (1.0f / 16777216.0f);
    } else if (exp < 31) {
        f = std::ldexp(1.0f + static_cast<float>(man) / 1024.0f, static_cast<int>(exp) - 15);
    } else {
        f = 1.0f;
    }
    if (sign) {
        f = 0.0f;
    }
    f = std::pow(std::fmin(f, 1.0f), 1.0f / 2.2f);
    return static_cast<uint8_t>(f * 255.0f + 0.5f);
}
} // namespace

void record_probe_bind(ID3D12GraphicsCommandList* cmd_list, SIZE_T rtv0, uint32_t rtv_count, SIZE_T dsv) {
    if (!g_probe_armed.load(std::memory_order_relaxed) || cmd_list == nullptr || rtv0 == 0 || dsv == 0) {
        return;
    }

    // Signature: single eye-sized RGBA16F RTV (SceneColor) + read-only DSV.
    ViewInfo rtv_info{};
    ViewInfo dsv_info{};
    {
        std::scoped_lock _{g_mtx};
        const auto rit = g_rtv_views.find(rtv0);
        const auto dit = g_dsv_views.find(dsv);
        if (rit == g_rtv_views.end() || dit == g_dsv_views.end()) {
            return;
        }
        rtv_info = rit->second;
        dsv_info = dit->second;
    }

    if (rtv_count != 1 || rtv_info.format != DXGI_FORMAT_R16G16B16A16_FLOAT ||
        rtv_info.width < 1024 || dsv_info.dsv_flags == 0) {
        return;
    }

    const auto slot_index = g_probe_next.fetch_add(1, std::memory_order_relaxed);
    if (slot_index >= kProbeSlots) {
        return;
    }
    auto& slot = g_probe_slots[slot_index];

    ComPtr<ID3D12Device> device{};
    if (FAILED(cmd_list->GetDevice(IID_PPV_ARGS(&device))) || device == nullptr) {
        return;
    }

    // (Re)create the slot texture to match the source shape.
    if (slot.texture == nullptr || slot.width != rtv_info.width || slot.height != rtv_info.height ||
        slot.format != rtv_info.format) {
        D3D12_HEAP_PROPERTIES heap{};
        heap.Type = D3D12_HEAP_TYPE_DEFAULT;
        D3D12_RESOURCE_DESC desc{};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        desc.Width = rtv_info.width;
        desc.Height = rtv_info.height;
        desc.DepthOrArraySize = 1;
        desc.MipLevels = 1;
        desc.Format = rtv_info.format;
        desc.SampleDesc.Count = 1;
        desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
        if (FAILED(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
                D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(slot.texture.ReleaseAndGetAddressOf())))) {
            return;
        }
        slot.width = rtv_info.width;
        slot.height = rtv_info.height;
        slot.format = rtv_info.format;
    }

    // The resource is bound as an RTV right after this call, so RENDER_TARGET
    // is its current state.
    auto* src = static_cast<ID3D12Resource*>(rtv_info.resource);
    D3D12_RESOURCE_BARRIER to_copy{};
    to_copy.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    to_copy.Transition.pResource = src;
    to_copy.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    to_copy.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    to_copy.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
    cmd_list->ResourceBarrier(1, &to_copy);

    cmd_list->CopyResource(slot.texture.Get(), src);

    std::swap(to_copy.Transition.StateBefore, to_copy.Transition.StateAfter);
    cmd_list->ResourceBarrier(1, &to_copy);

    slot.source = rtv_info.resource;
    slot.rtv_count = rtv_count;
    slot.dsv_flags = dsv_info.dsv_flags;
    slot.filled = true;
}

// === AFW per-frame depth snapshots (see header) ===
namespace {
struct AfwDepthSlot {
    ComPtr<ID3D12Resource> texture{};
    uint32_t frame{0xFFFFFFFFu};
    uint64_t width{};
    uint32_t height{};
    DXGI_FORMAT format{};
};
constexpr size_t kAfwDepthSlots = 4;
AfwDepthSlot g_afw_depth_slots[kAfwDepthSlots]{};
std::mutex g_afw_depth_mtx{};
// g_afw_depth_enabled lives near view_tracking_enabled (it feeds the gate).
std::atomic<uint32_t> g_recording_frame{0xFFFFFFFFu};
std::atomic<uint32_t> g_afw_depth_done_frame{0xFFFFFFFFu};

// Sequence-paired depth identity (see header): recording frames delimited by
// the bound depth RESOURCE changing between qualifying binds (RDG ping-pongs
// SceneDepthZ every frame; all qualifying binds within one frame share one
// depth). Window sized well past any realistic CPU-ahead depth.
std::atomic<bool> g_afw_seq_enabled{false};
constexpr size_t kAfwSeqWindow = 16;
AfwDepthSeqEntry g_afw_seq_ring[kAfwSeqWindow]{};
uint64_t g_afw_seq_pushed{0};
ID3D12Resource* g_afw_seq_last{nullptr};
std::mutex g_afw_seq_mtx{};

// AFW authoritative frame naming (see header): per-command-list frame tags set
// at record time, drained in submission order at ECL. The map stays tiny (one
// qualifying depth-bind list per frame, submitted promptly); a safety cap
// guards against a tagged-but-never-submitted list leaking entries.
std::atomic<bool> g_afw_naming_enabled{false};
std::atomic<uint32_t> g_afw_submitted_frame{0xFFFFFFFFu};
std::mutex g_afw_cl_tag_mtx{};
std::unordered_map<ID3D12CommandList*, uint32_t> g_afw_cl_tags{};
constexpr size_t kAfwClTagCap = 64;
} // namespace

void set_afw_depth_snapshot_enabled(bool enabled) {
    if (enabled) {
        g_liveness_armed.store(true, std::memory_order_relaxed);
    }
    g_afw_depth_enabled.store(enabled, std::memory_order_relaxed);
}

void set_view_tracking_forced(bool enabled) {
    if (enabled) {
        g_liveness_armed.store(true, std::memory_order_relaxed);
    }
    g_view_tracking_forced.store(enabled, std::memory_order_relaxed);
}

void set_recording_frame(uint32_t engine_frame) {
    g_recording_frame.store(engine_frame, std::memory_order_release);
}

namespace {
std::mutex g_vel_mtx{};
Microsoft::WRL::ComPtr<ID3D12Resource> g_vel_snapshot{};
uint64_t g_vel_width{0};
uint32_t g_vel_height{0};
uint64_t g_vel_best_area{0};
uint32_t g_vel_done_frame{0xFFFFFFFFu};
bool g_vel_snapshot_readable{false}; // false = COPY_DEST, true = NON_PIXEL_SHADER_RESOURCE
// Deferred-release graveyard: a recreated snapshot's predecessor may still be
// referenced by recorded-but-unexecuted command lists (the copy + barriers);
// releasing it live is a use-after-free the GPU sees - keep retirees alive
// for a generous frame budget instead.
struct VelRetired {
    Microsoft::WRL::ComPtr<ID3D12Resource> texture{};
    uint32_t retire_frame{};
};
std::vector<VelRetired> g_vel_graveyard{};
std::atomic<void*> g_vel_source{nullptr};
// Live-target memo: record_velocity_bind runs at the velocity RTV bind,
// BEFORE the frame's velocity draws are recorded - a copy there captures the
// PREVIOUS frame's content. The bind only remembers the qualifying resource;
// copy_velocity_snapshot() records the actual copy later in the frame, at the
// post-opaque depth-signature bind, yielding same-frame velocity (which is
// what the consumer's reprojection matrices describe).
void* g_vel_live{nullptr};
uint32_t g_vel_live_frame{0xFFFFFFFFu};

// Opt-in (UEVR_DIBR_VELOCITY=1): the snapshot copy mutates the game's command
// stream every frame; keep the default path untouched until the consumers
// (temporal MV gates) ship.
bool velocity_snapshot_enabled() {
    static const bool value = []() {
        const char* env = std::getenv("UEVR_DIBR_VELOCITY");
        return env != nullptr && env[0] != '\0' && env[0] != '0';
    }();
    return value;
}
} // namespace

void record_velocity_bind(ID3D12GraphicsCommandList* cmd_list, SIZE_T rtv0, uint32_t rtv_count) {
    if (!velocity_snapshot_enabled() || !view_tracking_enabled() || cmd_list == nullptr || rtv0 == 0 || rtv_count == 0) {
        return;
    }

    g_health_vel_calls.fetch_add(1, std::memory_order_relaxed);
    ViewInfo info{};
    bool extent_matches_depth = false;
    {
        std::scoped_lock _{g_mtx};
        const auto it = g_rtv_views.find(rtv0);
        if (it == g_rtv_views.end()) {
            g_health_vel_resolve_miss.fetch_add(1, std::memory_order_relaxed);
            return;
        }
        info = it->second;
        // Version-invariant identity (verified 4.26 -> 5.6): the velocity
        // target's extent exactly equals a scene depth extent. Names and
        // allocation paths churned across engine versions; this did not.
        for (const auto& c : g_candidates) {
            if (c.width == info.width && c.height == info.height) {
                extent_matches_depth = true;
                break;
            }
        }
    }

    // SceneVelocity invariants (UE 4.26 -> 5.6): exactly one of two formats
    // (R16G16B16A16_UNORM on RT/Lumen platforms, R16G16_UNORM otherwise),
    // scene-scale, non-square, non-MSAA, extent == SceneDepthZ's.
    if ((info.format != DXGI_FORMAT_R16G16B16A16_UNORM && info.format != DXGI_FORMAT_R16G16_UNORM) ||
        info.resource == nullptr || !extent_matches_depth ||
        info.width < 1024 || info.height < 256 || info.width == info.height) {
        g_health_vel_qualify_fail.fetch_add(1, std::memory_order_relaxed);
        return;
    }

    // Remember-only: the copy itself is recorded at the depth-signature bind
    // (copy_velocity_snapshot), after the velocity pass's draws.
    std::scoped_lock _{g_vel_mtx};
    const uint32_t frame = g_recording_frame.load(std::memory_order_acquire);
    const uint64_t area = static_cast<uint64_t>(info.width) * info.height;
    if (frame == g_vel_live_frame && area < g_vel_best_area) {
        return; // a larger qualifying target already won this frame
    }
    g_vel_best_area = area;
    g_vel_live = info.resource;
    g_vel_live_frame = frame;
}

namespace {
// Records the in-list snapshot copy of the velocity target remembered by
// record_velocity_bind. Invoked from record_afw_depth_bind's qualifying
// SceneColor + read-only-DSV bind: recording order places that bind after
// the velocity pass, so the copy executes on THIS frame's completed velocity.
void copy_velocity_snapshot(ID3D12GraphicsCommandList* cmd_list, uint32_t frame) {
    if (!velocity_snapshot_enabled()) {
        return;
    }

    std::scoped_lock _{g_vel_mtx};
    // Only a same-frame memo qualifies; one copy per recording frame.
    if (g_vel_live == nullptr || g_vel_live_frame != frame || g_vel_done_frame == frame) {
        return;
    }
    g_vel_done_frame = frame;

    // Retirees outlive any command list recorded around their release.
    g_vel_graveyard.erase(
        std::remove_if(g_vel_graveyard.begin(), g_vel_graveyard.end(),
            [frame](const VelRetired& r) { return frame - r.retire_frame > 64; }),
        g_vel_graveyard.end());

    Microsoft::WRL::ComPtr<ID3D12Device> device{};
    if (FAILED(cmd_list->GetDevice(IID_PPV_ARGS(&device))) || device == nullptr) {
        return;
    }

    auto* src = static_cast<ID3D12Resource*>(g_vel_live);
    const auto sdesc = src->GetDesc();

    if (g_vel_snapshot == nullptr || g_vel_width != sdesc.Width || g_vel_height != sdesc.Height) {
        // Retire (never live-release) the previous snapshot: recorded command
        // lists from in-flight frames may still hold copies/barriers on it.
        if (g_vel_snapshot != nullptr) {
            g_vel_graveyard.push_back(VelRetired{std::move(g_vel_snapshot), frame});
            g_vel_snapshot = nullptr;
        }
        D3D12_HEAP_PROPERTIES heap{};
        heap.Type = D3D12_HEAP_TYPE_DEFAULT;
        D3D12_RESOURCE_DESC desc{};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        desc.Width = sdesc.Width;
        desc.Height = sdesc.Height;
        desc.DepthOrArraySize = 1;
        desc.MipLevels = 1;
        desc.Format = sdesc.Format;
        desc.SampleDesc.Count = 1;
        if (FAILED(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
                D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(g_vel_snapshot.ReleaseAndGetAddressOf())))) {
            g_vel_snapshot.Reset();
            return;
        }
        g_vel_snapshot->SetName(L"DIBR Velocity Snapshot");
        g_vel_width = sdesc.Width;
        g_vel_height = sdesc.Height;
        g_vel_snapshot_readable = false;
        spdlog::info("[DIBR] velocity snapshot active: {}x{} fmt {} (SceneVelocity {:p})",
            sdesc.Width, sdesc.Height, static_cast<int>(sdesc.Format), static_cast<void*>(src));
    }

    // Between the velocity pass's last draw and post-processing's first read
    // RDG leaves the target in RENDER_TARGET (it batches the SRV transition
    // immediately before the consuming pass, which is after this bind);
    // round-trip it through COPY_SOURCE around an in-list copy (same pattern
    // as the AFW depth snapshot).
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = src;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
    cmd_list->ResourceBarrier(1, &barrier);

    if (g_vel_snapshot_readable) {
        D3D12_RESOURCE_BARRIER snap{};
        snap.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        snap.Transition.pResource = g_vel_snapshot.Get();
        snap.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        snap.Transition.StateBefore = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
        snap.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
        cmd_list->ResourceBarrier(1, &snap);
    }

    cmd_list->CopyResource(g_vel_snapshot.Get(), src);
    g_vel_source.store(src, std::memory_order_relaxed);

    D3D12_RESOURCE_BARRIER snap_read{};
    snap_read.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    snap_read.Transition.pResource = g_vel_snapshot.Get();
    snap_read.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    snap_read.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
    snap_read.Transition.StateAfter = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    cmd_list->ResourceBarrier(1, &snap_read);
    g_vel_snapshot_readable = true;

    std::swap(barrier.Transition.StateBefore, barrier.Transition.StateAfter);
    cmd_list->ResourceBarrier(1, &barrier);
}
} // namespace

Microsoft::WRL::ComPtr<ID3D12Resource> get_velocity_snapshot() {
    std::scoped_lock _{g_vel_mtx};
    return g_vel_snapshot_readable ? g_vel_snapshot : nullptr;
}

void* get_velocity_source() {
    return g_vel_source.load(std::memory_order_relaxed);
}

void record_afw_depth_bind(ID3D12GraphicsCommandList* cmd_list, SIZE_T rtv0, uint32_t rtv_count, SIZE_T dsv) {
    const bool snap_enabled = g_afw_depth_enabled.load(std::memory_order_relaxed);
    const bool seq_enabled = g_afw_seq_enabled.load(std::memory_order_relaxed);
    // velocity_snapshot_enabled keeps the signature path alive outside AFW:
    // the same-frame velocity copy is recorded at this bind too.
    const bool naming_enabled = g_afw_naming_enabled.load(std::memory_order_relaxed);
    if ((!snap_enabled && !seq_enabled && !velocity_snapshot_enabled() && !naming_enabled) ||
        cmd_list == nullptr || rtv0 == 0 || dsv == 0) {
        return;
    }

    // Same signature as the pretrans probe: a single scene-sized SceneColor
    // RTV + READ-ONLY DSV. At this bind the opaque depth is complete -
    // exactly the content the scatter warp wants. SceneColor's format is
    // scalability-dependent: RGBA16F at high post-process quality,
    // R11G11B10F at low (sg.PostProcessQuality drops it) - SN2 flips between
    // BOOTS as auto-scalability re-benchmarks, and a single-format check
    // silently killed the seq pairing + velocity snapshot on the low tier.
    g_health_afw_calls.fetch_add(1, std::memory_order_relaxed);
    ViewInfo rtv_info{};
    ViewInfo dsv_info{};
    {
        std::scoped_lock _{g_mtx};
        const auto rit = g_rtv_views.find(rtv0);
        const auto dit = g_dsv_views.find(dsv);
        if (rit == g_rtv_views.end() || dit == g_dsv_views.end()) {
            g_health_afw_resolve_miss.fetch_add(1, std::memory_order_relaxed);
            return;
        }
        rtv_info = rit->second;
        dsv_info = dit->second;
    }

    if (rtv_count != 1 ||
        (rtv_info.format != DXGI_FORMAT_R16G16B16A16_FLOAT && rtv_info.format != DXGI_FORMAT_R11G11B10_FLOAT) ||
        rtv_info.width < 256 || rtv_info.height < 256 || dsv_info.dsv_flags == 0 || dsv_info.resource == nullptr) {
        g_health_afw_qualify_fail.fetch_add(1, std::memory_order_relaxed);
        return;
    }

    // Sequence-paired identity: one push per recording frame, delimited by
    // the depth resource changing between qualifying binds. The frame tag is
    // a tie-breaker for the consumer's calibration only - it races recording
    // by +1, which is exactly why it cannot be the pairing key.
    if (seq_enabled) {
        auto* dres = static_cast<ID3D12Resource*>(dsv_info.resource);
        std::scoped_lock _{g_afw_seq_mtx};
        if (dres != g_afw_seq_last) {
            g_afw_seq_last = dres;
            auto& e = g_afw_seq_ring[g_afw_seq_pushed % kAfwSeqWindow];
            e.seq = g_afw_seq_pushed++;
            e.frame_tag = g_recording_frame.load(std::memory_order_acquire);
            e.resource = dres;
            g_health_afw_seq_push.fetch_add(1, std::memory_order_relaxed);
        }
    }

    const uint32_t frame = g_recording_frame.load(std::memory_order_acquire);
    if (frame == 0xFFFFFFFFu) {
        return;
    }

    // Authoritative frame naming: tag THIS command list with the frame it is
    // recording. The ECL hook publishes it in submission order. Tag here (the
    // qualifying scene-depth bind) so exactly one list per frame carries the
    // name. cmd_list is the same pointer the queue submits (single-inheritance
    // ID3D12GraphicsCommandList -> ID3D12CommandList).
    if (naming_enabled) {
        std::scoped_lock _{g_afw_cl_tag_mtx};
        if (g_afw_cl_tags.size() > kAfwClTagCap) {
            g_afw_cl_tags.clear(); // tagged-but-never-submitted leak guard
        }
        g_afw_cl_tags[static_cast<ID3D12CommandList*>(cmd_list)] = frame;
    }

    // The velocity pass was recorded before this bind: snapshot this frame's
    // completed velocity now (no-op unless UEVR_DIBR_VELOCITY armed it).
    copy_velocity_snapshot(cmd_list, frame);

    if (!snap_enabled) {
        return;
    }

    // First qualifying bind per recording frame only.
    if (g_afw_depth_done_frame.exchange(frame, std::memory_order_relaxed) == frame) {
        return;
    }

    ComPtr<ID3D12Device> device{};
    if (FAILED(cmd_list->GetDevice(IID_PPV_ARGS(&device))) || device == nullptr) {
        return;
    }

    auto* src = static_cast<ID3D12Resource*>(dsv_info.resource);
    const auto sdesc = src->GetDesc();

    std::scoped_lock _{g_afw_depth_mtx};
    auto& slot = g_afw_depth_slots[frame % kAfwDepthSlots];
    if (slot.texture == nullptr || slot.width != sdesc.Width || slot.height != sdesc.Height ||
        slot.format != sdesc.Format) {
        D3D12_HEAP_PROPERTIES heap{};
        heap.Type = D3D12_HEAP_TYPE_DEFAULT;
        D3D12_RESOURCE_DESC desc{};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        desc.Width = sdesc.Width;
        desc.Height = sdesc.Height;
        desc.DepthOrArraySize = 1;
        desc.MipLevels = 1;
        desc.Format = sdesc.Format;
        desc.SampleDesc.Count = 1;
        desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
        if (FAILED(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
                D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(slot.texture.ReleaseAndGetAddressOf())))) {
            slot.frame = 0xFFFFFFFFu;
            return;
        }
        slot.texture->SetName(L"DIBR AFW Depth Snapshot");
        slot.width = sdesc.Width;
        slot.height = sdesc.Height;
        slot.format = sdesc.Format;
    }

    // The DSV is bound READ-ONLY right after this call; UE keeps read-only
    // depth in DEPTH_READ combined with the SRV states (it samples scene
    // depth in the same passes).
    constexpr D3D12_RESOURCE_STATES read_only_depth =
        D3D12_RESOURCE_STATE_DEPTH_READ |
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE |
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;

    D3D12_RESOURCE_BARRIER to_copy{};
    to_copy.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    to_copy.Transition.pResource = src;
    to_copy.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    to_copy.Transition.StateBefore = read_only_depth;
    to_copy.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
    cmd_list->ResourceBarrier(1, &to_copy);

    cmd_list->CopyResource(slot.texture.Get(), src);

    std::swap(to_copy.Transition.StateBefore, to_copy.Transition.StateAfter);
    cmd_list->ResourceBarrier(1, &to_copy);

    slot.frame = frame;
}

Microsoft::WRL::ComPtr<ID3D12Resource> get_afw_depth_snapshot(uint32_t engine_frame) {
    std::scoped_lock _{g_afw_depth_mtx};
    const auto& slot = g_afw_depth_slots[engine_frame % kAfwDepthSlots];
    if (slot.frame == engine_frame && slot.texture != nullptr) {
        return slot.texture;
    }
    return nullptr;
}

void set_afw_depth_sequence_enabled(bool enabled) {
    if (enabled) {
        g_liveness_armed.store(true, std::memory_order_relaxed);
    }
    const bool was = g_afw_seq_enabled.exchange(enabled, std::memory_order_relaxed);
    if (was && !enabled) {
        // Drop the held resource references when AFW turns off.
        std::scoped_lock _{g_afw_seq_mtx};
        for (auto& e : g_afw_seq_ring) {
            e = {};
        }
        g_afw_seq_pushed = 0;
        g_afw_seq_last = nullptr;
    }
}

std::vector<AfwDepthSeqEntry> get_afw_depth_sequence(uint64_t& total_pushed) {
    std::vector<AfwDepthSeqEntry> out;
    std::scoped_lock _{g_afw_seq_mtx};
    total_pushed = g_afw_seq_pushed;
    const uint64_t n = std::min<uint64_t>(g_afw_seq_pushed, kAfwSeqWindow);
    out.reserve(static_cast<size_t>(n));
    for (uint64_t i = g_afw_seq_pushed - n; i < g_afw_seq_pushed; ++i) {
        out.push_back(g_afw_seq_ring[i % kAfwSeqWindow]);
    }
    return out;
}

void set_afw_frame_naming_enabled(bool enabled) {
    if (enabled) {
        g_liveness_armed.store(true, std::memory_order_relaxed);
    }
    const bool was = g_afw_naming_enabled.exchange(enabled, std::memory_order_relaxed);
    if (was && !enabled) {
        std::scoped_lock _{g_afw_cl_tag_mtx};
        g_afw_cl_tags.clear();
        g_afw_submitted_frame.store(0xFFFFFFFFu, std::memory_order_release);
    }
}

void on_command_lists_submitted(ID3D12CommandList* const* lists, uint32_t count) {
    if (!g_afw_naming_enabled.load(std::memory_order_relaxed) || lists == nullptr || count == 0) {
        return;
    }
    std::scoped_lock _{g_afw_cl_tag_mtx};
    if (g_afw_cl_tags.empty()) {
        return;
    }
    for (uint32_t i = 0; i < count; ++i) {
        const auto it = g_afw_cl_tags.find(lists[i]);
        if (it == g_afw_cl_tags.end()) {
            continue;
        }
        const uint32_t frame = it->second;
        g_afw_cl_tags.erase(it);
        // A frame becomes the backbuffer content only once its rendering is
        // submitted; frames advance monotonically. A large backwards jump is a
        // counter reset (level reload) and is adopted.
        const uint32_t prev = g_afw_submitted_frame.load(std::memory_order_relaxed);
        if (prev == 0xFFFFFFFFu || frame > prev || (prev - frame) > 0x40000000u) {
            g_afw_submitted_frame.store(frame, std::memory_order_release);
        }
    }
}

uint32_t get_afw_submitted_frame() {
    return g_afw_submitted_frame.load(std::memory_order_acquire);
}

bool is_afw_frame_naming_enabled() {
    return g_afw_naming_enabled.load(std::memory_order_acquire);
}

std::string probe_flush() {
    if (!probe_enabled()) {
        return {};
    }

    if (g_probe_armed.load(std::memory_order_relaxed)) {
        // The armed frame just presented; give the GPU two more presents
        // before reading the slots back.
        g_probe_armed.store(false, std::memory_order_relaxed);
        g_probe_flush_delay = 2;
        return {};
    }

    if (g_probe_flush_delay > 0 && --g_probe_flush_delay == 0) {
        // Read back and save every filled slot. Blocking - forensics only.
        ComPtr<ID3D12Device> device{};
        for (auto& slot : g_probe_slots) {
            if (slot.filled && slot.texture != nullptr) {
                slot.texture->GetDevice(IID_PPV_ARGS(&device));
                break;
            }
        }
        if (device == nullptr) {
            g_probe_last_ms = now_ms();
            return {};
        }

        ComPtr<ID3D12CommandQueue> queue{};
        ComPtr<ID3D12CommandAllocator> alloc{};
        ComPtr<ID3D12GraphicsCommandList> list{};
        ComPtr<ID3D12Fence> fence{};
        D3D12_COMMAND_QUEUE_DESC qdesc{};
        qdesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
        if (FAILED(device->CreateCommandQueue(&qdesc, IID_PPV_ARGS(&queue))) ||
            FAILED(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&alloc))) ||
            FAILED(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, alloc.Get(), nullptr, IID_PPV_ARGS(&list))) ||
            FAILED(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)))) {
            g_probe_last_ms = now_ms();
            return {};
        }

        struct Pending {
            ComPtr<ID3D12Resource> readback{};
            size_t slot_index{};
            uint32_t row_pitch{};
        };
        std::vector<Pending> pending{};

        for (size_t i = 0; i < kProbeSlots; ++i) {
            auto& slot = g_probe_slots[i];
            if (!slot.filled || slot.texture == nullptr) {
                continue;
            }

            const uint32_t row_pitch = (slot.width * 8u + 255u) & ~255u; // RGBA16F
            D3D12_HEAP_PROPERTIES heap{};
            heap.Type = D3D12_HEAP_TYPE_READBACK;
            D3D12_RESOURCE_DESC desc{};
            desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
            desc.Width = static_cast<uint64_t>(row_pitch) * slot.height;
            desc.Height = 1;
            desc.DepthOrArraySize = 1;
            desc.MipLevels = 1;
            desc.SampleDesc.Count = 1;
            desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

            Pending p{};
            p.slot_index = i;
            p.row_pitch = row_pitch;
            if (FAILED(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
                    D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&p.readback)))) {
                continue;
            }

            D3D12_TEXTURE_COPY_LOCATION src{};
            src.pResource = slot.texture.Get();
            src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
            src.SubresourceIndex = 0;
            D3D12_TEXTURE_COPY_LOCATION dst{};
            dst.pResource = p.readback.Get();
            dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
            dst.PlacedFootprint.Footprint.Format = slot.format;
            dst.PlacedFootprint.Footprint.Width = slot.width;
            dst.PlacedFootprint.Footprint.Height = slot.height;
            dst.PlacedFootprint.Footprint.Depth = 1;
            dst.PlacedFootprint.Footprint.RowPitch = row_pitch;

            D3D12_RESOURCE_BARRIER b{};
            b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            b.Transition.pResource = slot.texture.Get();
            b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            b.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
            b.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
            list->ResourceBarrier(1, &b);
            list->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
            std::swap(b.Transition.StateBefore, b.Transition.StateAfter);
            list->ResourceBarrier(1, &b);

            pending.emplace_back(std::move(p));
        }

        list->Close();
        ID3D12CommandList* lists[] = {list.Get()};
        queue->ExecuteCommandLists(1, lists);
        queue->Signal(fence.Get(), 1);
        if (fence->GetCompletedValue() < 1) {
            const HANDLE evt = CreateEventW(nullptr, FALSE, FALSE, nullptr);
            if (evt != nullptr) {
                fence->SetEventOnCompletion(1, evt);
                WaitForSingleObject(evt, 5000);
                CloseHandle(evt);
            }
        }

        char temp_dir[MAX_PATH]{};
        GetTempPathA(MAX_PATH, temp_dir);

        std::string report;
        report.reserve(1024);
        char line[512]{};

        for (const auto& p : pending) {
            const auto& slot = g_probe_slots[p.slot_index];
            void* mapped = nullptr;
            const D3D12_RANGE read_all{0, static_cast<SIZE_T>(p.row_pitch) * slot.height};
            if (FAILED(p.readback->Map(0, &read_all, &mapped)) || mapped == nullptr) {
                continue;
            }

            char path[MAX_PATH]{};
            std::snprintf(path, sizeof(path), "%suevr_dibr_pretrans_r%u_s%zu.ppm", temp_dir, g_probe_round, p.slot_index);
            std::ofstream f{path, std::ios::binary | std::ios::trunc};
            if (f) {
                char header[64]{};
                const int hlen = std::snprintf(header, sizeof(header), "P6\n%u %u\n255\n", slot.width, slot.height);
                f.write(header, hlen);
                std::vector<uint8_t> row(static_cast<size_t>(slot.width) * 3u);
                for (uint32_t y = 0; y < slot.height; ++y) {
                    const auto* px = reinterpret_cast<const uint16_t*>(static_cast<const uint8_t*>(mapped) + static_cast<size_t>(y) * p.row_pitch);
                    for (uint32_t x = 0; x < slot.width; ++x) {
                        row[x * 3 + 0] = half_to_u8(px[x * 4 + 0]);
                        row[x * 3 + 1] = half_to_u8(px[x * 4 + 1]);
                        row[x * 3 + 2] = half_to_u8(px[x * 4 + 2]);
                    }
                    f.write(reinterpret_cast<const char*>(row.data()), static_cast<std::streamsize>(row.size()));
                }
            }

            const D3D12_RANGE no_write{0, 0};
            p.readback->Unmap(0, &no_write);

            std::snprintf(line, sizeof(line), "slot %zu -> %s (src=%p ro=%u)\n", p.slot_index, path, slot.source, slot.dsv_flags);
            report += line;
        }

        for (auto& slot : g_probe_slots) {
            slot.filled = false;
        }
        g_probe_round++;
        g_probe_last_ms = now_ms();
        return report;
    }

    if (g_probe_flush_delay == 0 && now_ms() - g_probe_last_ms > 8000) {
        for (auto& slot : g_probe_slots) {
            slot.filled = false;
        }
        g_probe_next.store(0, std::memory_order_relaxed);
        g_probe_armed.store(true, std::memory_order_relaxed);
    }
    return {};
}

bool bind_capture_armed() {
    // Superset of every record_*_bind gate, so the always-installed bind hooks
    // can skip the whole census/probe/AFW/velocity block with one call at
    // defaults: census window arms only under census_enabled(), the probe
    // window only under probe_enabled(), and the AFW/velocity paths check the
    // same atomics/statics read here.
    return census_enabled() || probe_enabled() ||
           g_afw_depth_enabled.load(std::memory_order_relaxed) ||
           g_afw_seq_enabled.load(std::memory_order_relaxed) ||
           velocity_snapshot_enabled();
}

} // namespace dibr_depth_tracker
