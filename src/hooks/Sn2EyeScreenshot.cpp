// Sn2EyeScreenshot.cpp
//
// Implementation: see header for protocol description.
//
// Strategy:
//   - Reuse sn2_rt_snapshot's readback buffer + fence + PPM saver.
//   - We avoid the existing "intent queue" because it dequeues only one
//     intent per Present (insufficient for L+R+BB in one frame). Instead, we
//     manage our own persistent command list and call
//     sn2_rt_snapshot::schedule_capture directly with unique tags carrying
//     our per-request sequence number.
//   - Frame timeline of one request:
//       Frame N    : trigger file detected. Schedule L+R+BB copies on our CL,
//                    execute it on the device queue. Track expected output
//                    filename prefixes ("EYESHOT-L_s%llu", etc.).
//       Frame N+k  : sn2_rt_snapshot::drain_to_disk has saved the PPMs into
//                    the Temp dir (after fence completion). We scan the dir,
//                    find each match by sequence-tag prefix, and rename
//                    (rename = atomic move) it into OUTPUT_DIR with a stable
//                    name. Once all three are renamed, write done.txt and
//                    delete the trigger file. Request complete.

#include "Sn2EyeScreenshot.hpp"

#include <array>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <mutex>
#include <string>

#include <Windows.h>
#include <wrl/client.h>

#include <spdlog/spdlog.h>

// Phase AA: sidecar emission for UEVR↔RD bridge
namespace sn2_capture_sidecar {
void emit(uint64_t seq);
bool env_enabled();
}

// Forward decls from D3D12Hook.cpp's sn2_rt_snapshot namespace.
namespace sn2_rt_snapshot {
bool init(ID3D12Device* device);
bool initialized();
uint64_t schedule_capture(ID3D12GraphicsCommandList* cl,
                          ID3D12Resource* src_tex,
                          D3D12_RESOURCE_STATES src_current_state,
                          const char* tag);
void signal_after_execute(ID3D12CommandQueue* queue);
void drain_to_disk();
}  // namespace sn2_rt_snapshot

// Defined in D3D12Hook.cpp. Returns the game's primary direct command queue
// (the same one used to call Present), or nullptr if not yet captured.
extern "C" ID3D12CommandQueue* sn2_eye_screenshot_get_command_queue();

namespace sn2_eye_screenshot {

namespace {

std::string env_str(const char* name) {
    char buf[2048]{};
    const auto len = GetEnvironmentVariableA(name, buf, sizeof(buf));
    if (len == 0 || len >= sizeof(buf)) return {};
    return std::string{buf, len};
}

const std::string& trigger_path() {
    static const std::string s = env_str("UEVR_SN2_EYE_SCREENSHOT_TRIGGER_FILE");
    return s;
}

const std::string& output_dir() {
    static const std::string s = env_str("UEVR_SN2_EYE_SCREENSHOT_OUTPUT_DIR");
    return s;
}

// Temp directory where sn2_rt_snapshot::save_ppm writes its output. Hardcoded
// there to "C:\\Users\\ellio\\AppData\\Local\\Temp" — mirror that here.
const std::string& temp_dir() {
    static const std::string s = []() {
        char buf[2048]{};
        const auto len = GetEnvironmentVariableA("LOCALAPPDATA", buf, sizeof(buf));
        if (len == 0 || len >= sizeof(buf)) {
            // Fall back to the hardcoded path used by sn2_rt_snapshot.
            return std::string{"C:\\Users\\ellio\\AppData\\Local\\Temp"};
        }
        return std::string{buf, len} + "\\Temp";
    }();
    return s;
}

struct State {
    std::mutex mu;
    ID3D12Resource* latest_left{nullptr};
    ID3D12Resource* latest_right{nullptr};

    // Per-request tracking.
    std::atomic<bool> request_in_flight{false};
    std::atomic<uint64_t> seq{0};
    std::string expected_left_prefix;   // e.g. "sn2_rt_EYESHOT-L_s5_f"
    std::string expected_right_prefix;
    std::string expected_bb_prefix;
    bool got_left{false};
    bool got_right{false};
    bool got_bb{false};
    int poll_frames_remaining{0};  // request times out after N polled frames

    // Private CL+allocator for our own snapshot work.
    Microsoft::WRL::ComPtr<ID3D12CommandAllocator> alloc;
    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> cl;
    bool init_logged{false};
};

State& state() {
    static State s;
    return s;
}

bool ensure_cl(ID3D12Device* device) {
    auto& s = state();
    if (s.cl != nullptr && s.alloc != nullptr) return true;
    if (device == nullptr) return false;
    HRESULT hr = device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                                IID_PPV_ARGS(&s.alloc));
    if (FAILED(hr)) return false;
    hr = device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
                                   s.alloc.Get(), nullptr, IID_PPV_ARGS(&s.cl));
    if (FAILED(hr)) { s.alloc.Reset(); return false; }
    s.cl->Close();
    return true;
}

bool trigger_file_present() {
    const auto& p = trigger_path();
    if (p.empty()) return false;
    const DWORD attrs = GetFileAttributesA(p.c_str());
    return attrs != INVALID_FILE_ATTRIBUTES &&
           !(attrs & FILE_ATTRIBUTE_DIRECTORY);
}

// Scan temp_dir for a file beginning with `prefix`. Return absolute path or
// empty string.
std::string find_first_match(const std::string& prefix) {
    namespace fs = std::filesystem;
    std::error_code ec;
    fs::directory_iterator it(temp_dir(), ec);
    if (ec) return {};
    for (const auto& entry : it) {
        if (ec) break;
        if (!entry.is_regular_file(ec)) continue;
        const auto name = entry.path().filename().string();
        if (name.rfind(prefix, 0) == 0) {
            return entry.path().string();
        }
    }
    return {};
}

void ensure_output_dir() {
    namespace fs = std::filesystem;
    const auto& d = output_dir();
    if (d.empty()) return;
    std::error_code ec;
    fs::create_directories(d, ec);
}

bool move_file(const std::string& src, const std::string& dst) {
    namespace fs = std::filesystem;
    std::error_code ec;
    // Remove dst first if present so rename is unambiguous.
    fs::remove(dst, ec);
    fs::rename(src, dst, ec);
    if (!ec) return true;
    // Cross-device fallback: copy + remove.
    ec.clear();
    fs::copy_file(src, dst, fs::copy_options::overwrite_existing, ec);
    if (ec) return false;
    fs::remove(src, ec);
    return true;
}

void write_text_file(const std::string& path, const std::string& contents) {
    FILE* f = nullptr;
    fopen_s(&f, path.c_str(), "wb");
    if (f == nullptr) return;
    std::fwrite(contents.data(), 1, contents.size(), f);
    std::fclose(f);
}

void start_request(IDXGISwapChain3* swap_chain) {
    auto& s = state();
    if (swap_chain == nullptr) return;

    Microsoft::WRL::ComPtr<ID3D12Device> device;
    if (FAILED(swap_chain->GetDevice(IID_PPV_ARGS(&device))) || device == nullptr) {
        SPDLOG_WARN("[SN2-EyeShot] swap_chain->GetDevice failed");
        return;
    }
    // Lazy init rt_snapshot infra (needed for readback buffer + fence).
    sn2_rt_snapshot::init(device.Get());
    if (!sn2_rt_snapshot::initialized()) {
        SPDLOG_WARN("[SN2-EyeShot] rt_snapshot init failed; cannot capture");
        return;
    }
    if (!ensure_cl(device.Get())) {
        SPDLOG_WARN("[SN2-EyeShot] could not create private command list");
        return;
    }

    ID3D12Resource* left = nullptr;
    ID3D12Resource* right = nullptr;
    {
        std::scoped_lock _{s.mu};
        left = s.latest_left;
        right = s.latest_right;
    }

    // Grab backbuffer.
    Microsoft::WRL::ComPtr<ID3D12Resource> backbuffer;
    const UINT idx = swap_chain->GetCurrentBackBufferIndex();
    swap_chain->GetBuffer(idx, IID_PPV_ARGS(&backbuffer));

    // Bail out if we have nothing useful to capture. (Backbuffer alone is
    // worth keeping — gives at least the side-by-side composited output.)
    if (left == nullptr && right == nullptr && backbuffer == nullptr) {
        SPDLOG_WARN("[SN2-EyeShot] no resources to capture (no eyes tagged, no backbuffer)");
        return;
    }

    // Obtain the device's primary command queue from D3D12Hook (it owns the
    // queue handle internally).
    ID3D12CommandQueue* queue = sn2_eye_screenshot_get_command_queue();
    if (queue == nullptr) {
        SPDLOG_WARN("[SN2-EyeShot] could not obtain command queue; aborting request");
        return;
    }

    // Build the request.
    const uint64_t seq = s.seq.fetch_add(1, std::memory_order_relaxed) + 1;
    char left_tag[64], right_tag[64], bb_tag[64];
    std::snprintf(left_tag, sizeof(left_tag), "EYESHOT-L_s%llu", (unsigned long long)seq);
    std::snprintf(right_tag, sizeof(right_tag), "EYESHOT-R_s%llu", (unsigned long long)seq);
    std::snprintf(bb_tag, sizeof(bb_tag), "EYESHOT-BB_s%llu", (unsigned long long)seq);

    s.alloc->Reset();
    s.cl->Reset(s.alloc.Get(), nullptr);

    bool any_scheduled = false;
    bool left_scheduled = false;
    bool right_scheduled = false;
    bool bb_scheduled = false;

    if (left != nullptr) {
        if (sn2_rt_snapshot::schedule_capture(s.cl.Get(), left,
                D3D12_RESOURCE_STATE_COMMON, left_tag) != UINT64_MAX) {
            any_scheduled = true;
            left_scheduled = true;
        } else {
            SPDLOG_WARN("[SN2-EyeShot] schedule_capture LEFT failed (unsupported format?)");
        }
    }
    if (right != nullptr) {
        if (sn2_rt_snapshot::schedule_capture(s.cl.Get(), right,
                D3D12_RESOURCE_STATE_COMMON, right_tag) != UINT64_MAX) {
            any_scheduled = true;
            right_scheduled = true;
        } else {
            SPDLOG_WARN("[SN2-EyeShot] schedule_capture RIGHT failed (unsupported format?)");
        }
    }
    if (backbuffer != nullptr) {
        if (sn2_rt_snapshot::schedule_capture(s.cl.Get(), backbuffer.Get(),
                D3D12_RESOURCE_STATE_COMMON, bb_tag) != UINT64_MAX) {
            any_scheduled = true;
            bb_scheduled = true;
        } else {
            SPDLOG_WARN("[SN2-EyeShot] schedule_capture BB failed (unsupported format?)");
        }
    }

    s.cl->Close();
    if (any_scheduled) {
        ID3D12CommandList* lists[] = {s.cl.Get()};
        queue->ExecuteCommandLists(1, lists);
        sn2_rt_snapshot::signal_after_execute(queue);
    } else {
        SPDLOG_WARN("[SN2-EyeShot] schedule_capture returned UINT64_MAX for all sources (unsupported formats?)");
        return;
    }

    // Record expected filename prefixes so we can poll for them. Only set
    // prefixes for captures that actually scheduled (not just had a non-null
    // pointer). Otherwise poll_request_completion would loop forever waiting
    // for a file that will never appear.
    s.expected_left_prefix  = left_scheduled  ? (std::string("sn2_rt_") + left_tag  + "_f") : std::string{};
    s.expected_right_prefix = right_scheduled ? (std::string("sn2_rt_") + right_tag + "_f") : std::string{};
    s.expected_bb_prefix    = bb_scheduled    ? (std::string("sn2_rt_") + bb_tag   + "_f") : std::string{};
    s.got_left  = !left_scheduled;
    s.got_right = !right_scheduled;
    s.got_bb    = !bb_scheduled;
    // After ~120 frames (~2s at 60fps) we give up on missing pieces and
    // finalize whatever we got, so a stuck request can never block future
    // triggers.
    s.poll_frames_remaining = 120;
    s.request_in_flight.store(true, std::memory_order_release);

    SPDLOG_WARN("[SN2-EyeShot] request seq={} scheduled: L={} R={} BB={}",
                seq, (left_scheduled ? "yes" : "skip"),
                (right_scheduled ? "yes" : "skip"),
                (bb_scheduled ? "yes" : "skip"));
}

void poll_request_completion() {
    auto& s = state();
    if (!s.request_in_flight.load(std::memory_order_acquire)) return;

    ensure_output_dir();
    const auto& out = output_dir();

    if (!s.got_left && !s.expected_left_prefix.empty()) {
        auto src = find_first_match(s.expected_left_prefix);
        if (!src.empty()) {
            const auto dst = out + "\\left.ppm";
            if (move_file(src, dst)) {
                s.got_left = true;
                SPDLOG_WARN("[SN2-EyeShot] saved {}", dst);
            }
        }
    }
    if (!s.got_right && !s.expected_right_prefix.empty()) {
        auto src = find_first_match(s.expected_right_prefix);
        if (!src.empty()) {
            const auto dst = out + "\\right.ppm";
            if (move_file(src, dst)) {
                s.got_right = true;
                SPDLOG_WARN("[SN2-EyeShot] saved {}", dst);
            }
        }
    }
    if (!s.got_bb && !s.expected_bb_prefix.empty()) {
        auto src = find_first_match(s.expected_bb_prefix);
        if (!src.empty()) {
            const auto dst = out + "\\backbuffer.ppm";
            if (move_file(src, dst)) {
                s.got_bb = true;
                SPDLOG_WARN("[SN2-EyeShot] saved {}", dst);
            }
        }
    }

    // Bump down poll counter; if it reaches zero before all pieces arrived,
    // finalize with whatever we have and write a status note.
    if (s.poll_frames_remaining > 0) s.poll_frames_remaining--;
    const bool timed_out = (s.poll_frames_remaining <= 0)
                           && !(s.got_left && s.got_right && s.got_bb);

    if ((s.got_left && s.got_right && s.got_bb) || timed_out) {
        // Build a status line so the caller can tell what landed.
        std::string status = "ok";
        if (timed_out) {
            status = std::string("partial:")
                + " L=" + (s.got_left ? "ok" : "missing")
                + " R=" + (s.got_right ? "ok" : "missing")
                + " BB=" + (s.got_bb ? "ok" : "missing");
        }
        // Phase AA: emit UEVR↔RD bridge sidecar alongside the PPM dumps.
        // Reuses our per-request sequence number so sidecar pairs with PPMs.
        if (sn2_capture_sidecar::env_enabled()) {
            sn2_capture_sidecar::emit(s.seq.load(std::memory_order_relaxed));
        }
        write_text_file(out + "\\done.txt", status + "\n");
        // Delete trigger file (consume the request).
        DeleteFileA(trigger_path().c_str());
        // Reset state.
        s.request_in_flight.store(false, std::memory_order_release);
        s.expected_left_prefix.clear();
        s.expected_right_prefix.clear();
        s.expected_bb_prefix.clear();
        s.got_left = s.got_right = s.got_bb = false;
        s.poll_frames_remaining = 0;
        // Clear cached per-eye RTs so a fresh frame's tagging populates them.
        std::scoped_lock _{s.mu};
        s.latest_left = nullptr;
        s.latest_right = nullptr;
        SPDLOG_WARN("[SN2-EyeShot] request complete; status='{}'", status);
    }
}

}  // namespace

bool env_enabled() {
    static const bool e = []() {
        return !trigger_path().empty() && !output_dir().empty();
    }();
    return e;
}

void note_scene_color_write(ID3D12Resource* scene_color, int eye_bucket) {
    if (!env_enabled()) return;
    if (scene_color == nullptr) return;
    // UEVR's StereoTraceBucket: Unknown=0, Left=1, Right=2, Full=3, Multi=4
    if (eye_bucket != 1 && eye_bucket != 2) return;
    auto& s = state();
    std::scoped_lock _{s.mu};
    if (eye_bucket == 1) {       // Left
        s.latest_left = scene_color;
    } else {                      // Right (== 2)
        s.latest_right = scene_color;
    }
}

void on_present(IDXGISwapChain3* swap_chain) {
    if (!env_enabled()) return;
    auto& s = state();

    if (!s.init_logged) {
        s.init_logged = true;
        SPDLOG_WARN("[SN2-EyeShot] enabled. trigger='{}' output='{}'",
                    trigger_path(), output_dir());
    }

    // 1. Drain any completed captures (PPMs to disk). We always drive this so
    //    that even when UEVR_SN2_RT_SNAPSHOT isn't set, our own snapshots get
    //    written out by sn2_rt_snapshot::drain_to_disk.
    if (sn2_rt_snapshot::initialized()) {
        sn2_rt_snapshot::drain_to_disk();
    }

    // 2. Finish in-flight request if pending.
    poll_request_completion();

    // 3. Start a new request if a trigger file appeared AND none is in flight.
    if (!s.request_in_flight.load(std::memory_order_acquire) && trigger_file_present()) {
        start_request(swap_chain);
    }
}

}  // namespace sn2_eye_screenshot
