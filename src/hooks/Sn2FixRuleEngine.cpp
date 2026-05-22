// Sn2FixRuleEngine.cpp — implementation
//
// Loads fix rules from JSON. Polls file mtime every 60 frames for live reload.
// Exposes find_rule(ps_crc, eye_bucket) for the dup function to consume.

#include "Sn2FixRuleEngine.hpp"

#include <atomic>
#include <fstream>
#include <mutex>
#include <unordered_map>

#include <Windows.h>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

namespace sn2_fix_rules {

namespace {

std::string env_str(const char* name) {
    char buf[2048]{};
    const auto len = GetEnvironmentVariableA(name, buf, sizeof(buf));
    if (len == 0 || len >= sizeof(buf)) return {};
    return std::string{buf, len};
}

uint32_t parse_u32(const std::string& s) {
    if (s.empty()) return 0;
    char* tail = nullptr;
    const auto v = std::strtoul(s.c_str(), &tail, 0);
    return (tail != s.c_str()) ? static_cast<uint32_t>(v) : 0;
}

struct Storage {
    std::mutex mu;
    // Keyed by (ps_crc, eye_bucket)
    std::unordered_map<uint64_t, Rule> rules;
    int64_t last_mtime = 0;
    std::atomic<uint64_t> poll_counter{0};
};

Storage& storage() { static Storage s; return s; }

uint64_t pack_key(uint32_t ps_crc, int eye_bucket) {
    return (static_cast<uint64_t>(eye_bucket) << 32) |
           static_cast<uint64_t>(ps_crc);
}

CbOp parse_cb_op(const std::string& to) {
    // "synth(donor=0x...)" → SwapToSynth
    if (to.rfind("synth(", 0) == 0) return CbOp::SwapToSynth;
    if (to == "left") return CbOp::SwapToLeft;
    if (to.rfind("delta=", 0) == 0) return CbOp::SwapToDelta;
    return CbOp::None;
}

uint32_t parse_donor_from_to(const std::string& to) {
    auto pos = to.find("donor=");
    if (pos == std::string::npos) return 0;
    pos += 6;
    auto end = to.find_first_of(",)", pos);
    return parse_u32(to.substr(pos, (end == std::string::npos) ? std::string::npos : end - pos));
}

int64_t parse_delta_from_to(const std::string& to) {
    auto pos = to.find("delta=");
    if (pos == std::string::npos) return 0;
    pos += 6;
    char* tail = nullptr;
    return std::strtoll(to.c_str() + pos, &tail, 0);
}

void parse_rules_doc(const nlohmann::json& doc, std::unordered_map<uint64_t, Rule>& out) {
    out.clear();
    if (!doc.contains("rules") || !doc["rules"].is_array()) return;
    for (const auto& r : doc["rules"]) {
        if (!r.contains("when") || !r.contains("then")) continue;
        Rule rule{};
        const auto& w = r["when"];
        // ps_crc
        if (w.contains("ps_crc")) {
            if (w["ps_crc"].is_string()) {
                rule.ps_crc = parse_u32(w["ps_crc"].get<std::string>());
            } else if (w["ps_crc"].is_number()) {
                rule.ps_crc = w["ps_crc"].get<uint32_t>();
            }
        }
        if (rule.ps_crc == 0) continue;
        // eye
        rule.eye_bucket = -1;
        if (w.contains("eye")) {
            if (w["eye"].is_string()) {
                const auto e = w["eye"].get<std::string>();
                if (e == "left") rule.eye_bucket = 1;
                else if (e == "right") rule.eye_bucket = 2;
            } else if (w["eye"].is_number()) {
                rule.eye_bucket = w["eye"].get<int>();
            }
        }
        // then
        const auto& t = r["then"];
        if (t.contains("swap_cb") && t["swap_cb"].is_array()) {
            for (const auto& s : t["swap_cb"]) {
                CbSwap sw{};
                sw.root = s.contains("root") ? s["root"].get<uint32_t>() : 0;
                const auto to = s.contains("to") ? s["to"].get<std::string>() : std::string{};
                sw.op = parse_cb_op(to);
                sw.donor_crc = parse_donor_from_to(to);
                sw.delta = parse_delta_from_to(to);
                rule.cb_swaps.push_back(sw);
            }
        }
        if (t.contains("redirect_rtv")) {
            const auto v = t["redirect_rtv"];
            if (v.is_string() && v.get<std::string>() == "mirror") {
                rule.rtv_op = RtvOp::RedirectToMirror;
            }
        }
        if (t.contains("redirect_srv") && t["redirect_srv"].is_array()) {
            for (const auto& s : t["redirect_srv"]) {
                if (s.contains("to") && s["to"].is_string()) {
                    const auto to = s["to"].get<std::string>();
                    if (to == "mirror_srv" || to == "mirror") {
                        rule.srv_op = SrvOp::RedirectToMirror;
                        if (s.contains("slot")) rule.srv_slot = s["slot"].get<uint32_t>();
                    } else if (to == "left") {
                        rule.srv_op = SrvOp::RedirectToLeft;
                        if (s.contains("slot")) rule.srv_slot = s["slot"].get<uint32_t>();
                    }
                }
            }
        }
        if (t.contains("no_viewport_shift")) {
            rule.no_viewport_shift = t["no_viewport_shift"].get<bool>();
        }
        out[pack_key(rule.ps_crc, rule.eye_bucket)] = rule;
    }
}

}  // namespace

const std::string& rules_file_path() {
    static const std::string p = env_str("UEVR_SN2_FIX_RULES_FILE");
    return p;
}

void refresh_rules() {
    const auto& path = rules_file_path();
    if (path.empty()) return;
    auto& s = storage();
    const auto n = s.poll_counter.fetch_add(1, std::memory_order_relaxed);
    if ((n % 60) != 0) return;

    WIN32_FILE_ATTRIBUTE_DATA fad{};
    if (!GetFileAttributesExA(path.c_str(), GetFileExInfoStandard, &fad)) return;
    const int64_t mtime = (static_cast<int64_t>(fad.ftLastWriteTime.dwHighDateTime) << 32)
                          | fad.ftLastWriteTime.dwLowDateTime;
    if (mtime == s.last_mtime) return;

    std::ifstream f(path);
    if (!f.good()) return;
    nlohmann::json doc;
    try {
        f >> doc;
    } catch (const std::exception& e) {
        SPDLOG_WARN("[SN2-FixRules] parse error: {}", e.what());
        return;
    }
    std::unordered_map<uint64_t, Rule> parsed;
    parse_rules_doc(doc, parsed);
    {
        std::scoped_lock _{s.mu};
        s.rules = std::move(parsed);
        s.last_mtime = mtime;
    }
    SPDLOG_WARN("[SN2-FixRules] reloaded {} rules from {}", s.rules.size(), path);
}

const Rule* find_rule(uint32_t ps_crc, int eye_bucket) {
    auto& s = storage();
    std::scoped_lock _{s.mu};
    // Try exact eye match first
    auto it = s.rules.find(pack_key(ps_crc, eye_bucket));
    if (it != s.rules.end()) return &it->second;
    // Fall back to "both eyes" rule (eye_bucket=-1)
    auto it2 = s.rules.find(pack_key(ps_crc, -1));
    if (it2 != s.rules.end()) return &it2->second;
    return nullptr;
}

size_t rule_count() {
    auto& s = storage();
    std::scoped_lock _{s.mu};
    return s.rules.size();
}

}  // namespace sn2_fix_rules
