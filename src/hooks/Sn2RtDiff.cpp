// Sn2RtDiff.cpp — implementation
//
// Uses existing sn2_rt_snapshot infrastructure for readback. We just need to
// pick out one LEFT and one RIGHT scene-color RT per frame, schedule both via
// the snapshot intent queue, then in a post-processor compute per-pixel diff.
//
// Note: the actual readback + PPM write happens in sn2_rt_snapshot::drain.
// We tag our snapshot intents with "RTDIFF-L" / "RTDIFF-R" prefix so the
// Python tool diff_rt_dumps.py can pair them by sequence number.

#include "Sn2RtDiff.hpp"

#include <atomic>
#include <filesystem>
#include <mutex>
#include <string>

#include <spdlog/spdlog.h>

namespace sn2_rt_snapshot {
void queue_intent_named(ID3D12Resource* res, const char* tag_prefix,
                        char eye_char, uint64_t seq);
}

namespace sn2_rt_diff {

namespace {

struct State {
    std::atomic<uint64_t> frame_count{0};
    std::atomic<uint64_t> seq{0};
    std::atomic<bool> have_left{false};
    std::atomic<bool> have_right{false};
    std::mutex mu;
    ID3D12Resource* left_rt{nullptr};
    ID3D12Resource* right_rt{nullptr};
};

State& state() {
    static State s;
    return s;
}

std::string& output_dir() {
    static std::string s = []() -> std::string {
        char buf[2048]{};
        const auto len = GetEnvironmentVariableA("UEVR_SN2_RT_DIFF_DIR", buf, sizeof(buf));
        if (len == 0 || len >= sizeof(buf)) return std::string{};
        return std::string{buf, len};
    }();
    return s;
}

}  // namespace

void note_scene_color_write(ID3D12GraphicsCommandList* /*cl*/,
                            ID3D12Resource* scene_color,
                            int eye_bucket) {
    if (!env_enabled()) return;
    if (scene_color == nullptr) return;
    // UEVR StereoTraceBucket: Left=1, Right=2.
    if (eye_bucket != 1 && eye_bucket != 2) return;
    auto& s = state();
    std::scoped_lock _{s.mu};
    if (eye_bucket == 1 && s.left_rt == nullptr) {
        s.left_rt = scene_color;
        s.have_left.store(true, std::memory_order_release);
    } else if (eye_bucket == 2 && s.right_rt == nullptr) {
        s.right_rt = scene_color;
        s.have_right.store(true, std::memory_order_release);
    }
}

void on_present() {
    if (!env_enabled()) return;
    auto& s = state();
    const auto f = s.frame_count.fetch_add(1, std::memory_order_relaxed) + 1;
    if ((f % every_n_frames()) != 0) {
        // Reset per-frame state so next capture frame is clean.
        std::scoped_lock _{s.mu};
        s.left_rt = nullptr;
        s.right_rt = nullptr;
        s.have_left.store(false, std::memory_order_release);
        s.have_right.store(false, std::memory_order_release);
        return;
    }
    if (!s.have_left.load(std::memory_order_acquire) ||
        !s.have_right.load(std::memory_order_acquire)) {
        // Missing one eye for this capture frame; clear and skip.
        std::scoped_lock _{s.mu};
        s.left_rt = nullptr;
        s.right_rt = nullptr;
        s.have_left.store(false, std::memory_order_release);
        s.have_right.store(false, std::memory_order_release);
        return;
    }

    const auto seq = s.seq.fetch_add(1, std::memory_order_relaxed) + 1;
    ID3D12Resource* l = nullptr;
    ID3D12Resource* r = nullptr;
    {
        std::scoped_lock _{s.mu};
        l = s.left_rt;
        r = s.right_rt;
        s.left_rt = nullptr;
        s.right_rt = nullptr;
        s.have_left.store(false, std::memory_order_release);
        s.have_right.store(false, std::memory_order_release);
    }
    if (l == nullptr || r == nullptr) return;

    // Schedule both via sn2_rt_snapshot. The diff (CPU-side comparison +
    // delta PPM) is done by an offline Python tool.
    sn2_rt_snapshot::queue_intent_named(l, "RTDIFF", 'L', seq);
    sn2_rt_snapshot::queue_intent_named(r, "RTDIFF", 'R', seq);

    SPDLOG_INFO("[SN2-RtDiff] queued L+R scene-color for seq={} (L=0x{:x} R=0x{:x})",
                seq, reinterpret_cast<uintptr_t>(l), reinterpret_cast<uintptr_t>(r));
}

}  // namespace sn2_rt_diff
