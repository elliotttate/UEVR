#pragma once

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <string_view>

namespace render {

// Canonical runtime eye bucket shared by D3D12 hooks, StereoForensics, and
// offline tools. Keep this distinct from UE view indices, where 0=left/1=right.
enum class StereoTraceBucket : uint8_t {
    Unknown = 0,
    Left = 1,
    Right = 2,
    Full = 3,
    Multi = 4,
};

inline constexpr int kStereoEyeAny = -1;

inline int stereo_eye_bucket_value(StereoTraceBucket bucket) {
    return static_cast<int>(bucket);
}

inline bool is_canonical_stereo_eye_bucket(int value) {
    return value >= static_cast<int>(StereoTraceBucket::Unknown) &&
           value <= static_cast<int>(StereoTraceBucket::Multi);
}

inline bool is_stereo_side_bucket(int value) {
    return value == static_cast<int>(StereoTraceBucket::Left) ||
           value == static_cast<int>(StereoTraceBucket::Right);
}

inline const char* stereo_eye_bucket_name(int value) {
    switch (value) {
    case static_cast<int>(StereoTraceBucket::Unknown): return "unknown";
    case static_cast<int>(StereoTraceBucket::Left): return "left";
    case static_cast<int>(StereoTraceBucket::Right): return "right";
    case static_cast<int>(StereoTraceBucket::Full): return "full";
    case static_cast<int>(StereoTraceBucket::Multi): return "multi";
    case kStereoEyeAny: return "any";
    default: return "invalid";
    }
}

inline int canonicalize_stereo_eye_bucket(int value, int fallback = static_cast<int>(StereoTraceBucket::Unknown)) {
    return is_canonical_stereo_eye_bucket(value) ? value : fallback;
}

inline int view_index_to_stereo_eye_bucket(int view_index, int fallback = static_cast<int>(StereoTraceBucket::Unknown)) {
    if (view_index == 0) {
        return static_cast<int>(StereoTraceBucket::Left);
    }
    if (view_index == 1) {
        return static_cast<int>(StereoTraceBucket::Right);
    }
    return fallback;
}

inline int parse_stereo_eye_bucket(std::string_view value, int fallback = kStereoEyeAny) {
    std::string s{value};
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    if (s.empty()) {
        return fallback;
    }
    if (s == "left" || s == "l") {
        return static_cast<int>(StereoTraceBucket::Left);
    }
    if (s == "right" || s == "r") {
        return static_cast<int>(StereoTraceBucket::Right);
    }
    if (s == "unknown" || s == "u") {
        return static_cast<int>(StereoTraceBucket::Unknown);
    }
    if (s == "full" || s == "f") {
        return static_cast<int>(StereoTraceBucket::Full);
    }
    if (s == "multi" || s == "m") {
        return static_cast<int>(StereoTraceBucket::Multi);
    }
    if (s == "both" || s == "any" || s == "all" || s == "*") {
        return kStereoEyeAny;
    }
    char* end = nullptr;
    const long parsed = std::strtol(s.c_str(), &end, 0);
    if (end != s.c_str() && *end == '\0') {
        return canonicalize_stereo_eye_bucket(static_cast<int>(parsed), fallback);
    }
    return fallback;
}

} // namespace render
