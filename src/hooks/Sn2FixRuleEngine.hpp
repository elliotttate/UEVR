// Sn2FixRuleEngine.hpp
//
// Declarative fix rules. Loads a JSON file describing per-PSO fixes; the dup
// hook consults the rule for each draw and applies the action.
//
// File format (loaded from UEVR_SN2_FIX_RULES_FILE, live-reloaded every 60 frames):
//
// {
//   "rules": [
//     {
//       "when": { "ps_crc": "0x9d14fcf0", "eye": "right" },
//       "then": {
//         "swap_cb": [
//           { "root": 3, "from": "auto", "to": "synth(donor=0x4d44ce74)" }
//         ],
//         "redirect_rtv": "mirror",
//         "no_viewport_shift": true
//       }
//     },
//     {
//       "when": { "ps_crc": "0x37558de4", "eye": "right" },
//       "then": { "redirect_srv": [{ "slot": 5, "to": "mirror_srv" }] }
//     }
//   ]
// }
//
// Today's per-PSO logic lives across multiple files (Sn2DupConfigFile,
// Sn2RightCbSynth, Sn2UweFogMirrorHook, ...). The rule engine consolidates
// that into one place per fix and makes new fixes a config edit, not a code
// change.
//
// Implementation note: this is a scaffold — actual rule application requires
// wiring into the dup function. For now it stores parsed rules and exposes a
// lookup API.

#pragma once

#include <atomic>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace sn2_fix_rules {

enum class CbOp : uint8_t { None, SwapToSynth, SwapToLeft, SwapToDelta };
enum class RtvOp : uint8_t { None, RedirectToMirror };
enum class SrvOp : uint8_t { None, RedirectToMirror, RedirectToLeft };

struct CbSwap {
    uint32_t root;
    CbOp op;
    uint32_t donor_crc;  // for SwapToSynth
    int64_t delta;       // for SwapToDelta
};

struct Rule {
    uint32_t ps_crc;
    int eye_bucket;      // -1 = both, 1 = left, 2 = right
    std::vector<CbSwap> cb_swaps;
    RtvOp rtv_op = RtvOp::None;
    SrvOp srv_op = SrvOp::None;
    uint32_t srv_slot = UINT32_MAX;  // for SrvOp
    bool no_viewport_shift = false;
};

// Load/reload rules from disk if file mtime changed.
void refresh_rules();

// Look up rule for (ps_crc, eye_bucket). Returns nullptr if none.
const Rule* find_rule(uint32_t ps_crc, int eye_bucket);

// Number of currently-loaded rules.
size_t rule_count();

// Where rules are loaded from.
const std::string& rules_file_path();

}  // namespace sn2_fix_rules
