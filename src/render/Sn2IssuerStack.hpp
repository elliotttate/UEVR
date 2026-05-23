// Sn2IssuerStack.hpp
//
// Capture the CPU return-address callstack at a StereoForensics record site and
// attach it (as raw VAs + the module base) to the event JSON. The offline
// symbolizer (tools/sn2_symbolizer.py) resolves those VAs against the binfold
// symbol dump, turning "PS missing on right" into the engine function that
// issued (or failed to issue) the work.
//
// Self-contained, header-only. Gated by env UEVR_STEREO_FORENSICS_STACKS=1; only
// reached on captured frames (callers already early-out otherwise), so the cost
// is bounded. attach() never throws (the record_* callers are also wrapped in
// STEREO_FORENSICS_TRY).

#pragma once

#include <cstdint>

#include <Windows.h>
#include <nlohmann/json.hpp>

namespace sn2_issuer_stack {

inline bool enabled() {
    static const bool e = []() {
        char buf[8]{};
        const DWORD n = GetEnvironmentVariableA("UEVR_STEREO_FORENSICS_STACKS", buf, sizeof(buf));
        return n > 0 && buf[0] != '\0' && buf[0] != '0';
    }();
    return e;
}

// Base of the main module (the game EXE). RVAs in the SN2 symbol dump are
// relative to this; the symbolizer subtracts module_base before lookup so ASLR
// is irrelevant.
inline uint64_t module_base() {
    static const uint64_t b = reinterpret_cast<uint64_t>(GetModuleHandleW(nullptr));
    return b;
}

// frames_to_skip: skip attach() + the immediate record_* frame so the first
// captured frame is the real D3D12 call site / engine code.
inline void attach(nlohmann::json& event, USHORT frames_to_skip = 2, USHORT frames_to_capture = 28) {
    if (!enabled()) {
        return;
    }
    void* frames[64]{};
    if (frames_to_capture > 64) {
        frames_to_capture = 64;
    }
    const USHORT n = RtlCaptureStackBackTrace(frames_to_skip, frames_to_capture, frames, nullptr);
    auto arr = nlohmann::json::array();
    for (USHORT i = 0; i < n; ++i) {
        arr.push_back(reinterpret_cast<uint64_t>(frames[i]));
    }
    event["issuer_stack"] = std::move(arr);
    event["module_base"] = module_base();
}

}  // namespace sn2_issuer_stack
