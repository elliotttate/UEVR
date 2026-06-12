// Sn2PsoBytecodeDumper.cpp
//
// Two responsibilities:
//
//   A. DXBC/DXIL bytecode dump (original) — when UEVR_SN2_PSO_BYTECODE_DIR is
//      set, write each shader's bytecode to disk.
//
//   B. PSO CRC registry (new, 2026-05-29) — maintain a compact
//      PSO* → {ps_crc, vs_crc, cs_crc, has_ps, create_rva} table that is
//      independent of ShaderOverrideRegistry.  Populated from every
//      create_graphics_pso / create_compute_pso / create_pso_stream call when
//      UEVR_SN2_RDOC_TAGS=1 (or UEVR_SN2_PSO_BYTECODE_DIR is set).  Exposes
//      ps_crc_for_pso() / vs_crc_for_pso() / cs_crc_for_pso() for the per-draw
//      marker code to fall back to when ShaderOverrideRegistry returns 0 (which
//      happens for the ~9 858 stream-form PSOs created via CreatePipelineState
//      unless the ShaderHunter is also active).
//
//      The sentinel NOPS_CRC (0xFFFFFFFFu) means "this PSO has no pixel shader"
//      so callers can distinguish "hash not found" (0) from "genuinely depth-
//      only/compute" (0xFFFFFFFF).
//
//   C. PSO→CRC sidecar JSON (new, 2026-05-29) — when the gate is on, write a
//      JSON file next to the UEVR log (or the bytecode dump dir if set) that
//      maps every PSO pointer seen to {ps_crc, vs_crc, cs_crc, has_ps,
//      rva_hex}.  Filename: sn2_pso_crc_map.json.  Written on the first new-PSO
//      event after the sidecar is overdue (lazy, ~5 s interval), and again at
//      DLL unload via on_unload().  Downstream tools can label any RenderDoc
//      draw even if the live marker missed it.

#include "Sn2PsoBytecodeDumper.hpp"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>

#include <intrin.h>
#include <windows.h>
#include <ShlObj.h>

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

// --------------------------------------------------------------------------
// Internal helpers
// --------------------------------------------------------------------------

namespace sn2_pso_bytecode_dumper {

// ---- CRC helper (same polynomial as ShaderOverrideRegistry::crc32_ieee) ---
namespace {
uint32_t pso_crc32(const void* data, size_t size) {
    // Build the table once on first call.
    static uint32_t table[256]{};
    static std::atomic<bool> ready{false};
    if (!ready.load(std::memory_order_acquire)) {
        for (uint32_t i = 0; i < 256; ++i) {
            uint32_t c = i;
            for (int j = 0; j < 8; ++j)
                c = (c & 1u) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
            table[i] = c;
        }
        ready.store(true, std::memory_order_release);
    }
    uint32_t crc = 0xFFFFFFFFu;
    const auto* p = static_cast<const uint8_t*>(data);
    for (size_t i = 0; i < size; ++i)
        crc = table[(crc ^ p[i]) & 0xFFu] ^ (crc >> 8);
    return crc ^ 0xFFFFFFFFu;
}

// ---- Gate: either the bytecode-dump dir or the rdoc-tags flag enables us. --
// VALUE-aware: launchers scrub inherited toggles by setting them to "0"; a
// presence check would keep populating the stream-PSO CRC map + sidecar path
// on every clean run.
bool gate_active() {
    static const bool v = []() {
        wchar_t dir[8]{};
        const auto dir_len = GetEnvironmentVariableW(L"UEVR_SN2_PSO_BYTECODE_DIR", dir, (DWORD)std::size(dir));
        const bool dir_set = dir_len > 0; // a path (any non-empty value, may exceed buf)
        wchar_t tags[32]{};
        const auto tags_len = GetEnvironmentVariableW(L"UEVR_SN2_RDOC_TAGS", tags, (DWORD)std::size(tags));
        const std::wstring_view tv{tags, std::min<DWORD>(tags_len, (DWORD)std::size(tags) - 1)};
        const bool tags_on = tags_len > 0 && !tv.empty() && tv != L"0" && tv != L"false" && tv != L"off";
        return dir_set || tags_on;
    }();
    return v;
}

// ---- Sidecar output directory -------------------------------------------
//  Prefer UEVR_SN2_PSO_BYTECODE_DIR; fall back to the UEVR persistent log
//  directory (%APPDATA%\Roaming\UnrealVRMod\<exe-stem>).
std::string sidecar_dir() {
    static const std::string s = []() -> std::string {
        // First choice: bytecode dump dir.
        char buf[2048]{};
        DWORD len = GetEnvironmentVariableA("UEVR_SN2_PSO_BYTECODE_DIR", buf, sizeof(buf));
        if (len > 0 && len < sizeof(buf)) return std::string{buf, len};

        // Second choice: UEVR log dir (%APPDATA%\Roaming\UnrealVRMod\<exe>).
        wchar_t appdata[MAX_PATH]{};
        SHGetSpecialFolderPathW(nullptr, appdata, CSIDL_APPDATA, FALSE);
        if (appdata[0] == L'\0') return std::string{};

        wchar_t exe[MAX_PATH]{};
        GetModuleFileNameW(nullptr, exe, MAX_PATH);
        // stem
        const wchar_t* slash = wcsrchr(exe, L'\\');
        const wchar_t* dot   = wcsrchr(exe, L'.');
        std::wstring stem;
        if (slash != nullptr) {
            const wchar_t* start = slash + 1;
            stem = (dot != nullptr && dot > slash) ? std::wstring(start, dot) : std::wstring(start);
        }
        if (stem.empty()) return std::string{};

        std::wstring dir_w = std::wstring(appdata) + L"\\UnrealVRMod\\" + stem;
        // narrow
        char narrow[MAX_PATH * 2]{};
        int k = WideCharToMultiByte(CP_UTF8, 0, dir_w.c_str(), -1, narrow, sizeof(narrow), nullptr, nullptr);
        if (k <= 0) return std::string{};
        return std::string{narrow};
    }();
    return s;
}

// ---- PSO CRC entry -------------------------------------------------------
struct PsoEntry {
    uint32_t ps_crc{0};   // 0 if not yet computed / unknown
    uint32_t vs_crc{0};
    uint32_t cs_crc{0};
    bool has_ps{false};   // false for depth-only / compute PSOs
    uintptr_t rva{0};     // create-time call-site (0 if not captured)
};

// ---- Registry state -------------------------------------------------------
struct RegState {
    std::mutex                              mu;
    std::unordered_map<uintptr_t, PsoEntry> map;   // PSO* → entry
    // Sidecar flush bookkeeping.
    std::chrono::steady_clock::time_point   last_flush{};
    bool                                    dirty{false};
    bool                                    dir_ensured{false};
    // DXBC dump dedup.
    std::unordered_set<uint32_t>            dxbc_seen;
    bool                                    dxbc_dir_created{false};
};

RegState& reg() {
    static RegState s;
    return s;
}

// ---- Ensure sidecar directory exists (once) --------------------------------
void ensure_sidecar_dir(RegState& r) {
    if (r.dir_ensured) return;
    const auto d = sidecar_dir();
    if (!d.empty()) {
        std::error_code ec;
        std::filesystem::create_directories(d, ec);
    }
    r.dir_ensured = true;
}

// ---- Write the sidecar JSON ------------------------------------------------
void flush_sidecar_locked(RegState& r) {
    // Must be called with r.mu held.
    const auto dir = sidecar_dir();
    if (dir.empty()) return;

    nlohmann::json doc = nlohmann::json::array();
    for (const auto& [ptr, e] : r.map) {
        char pso_hex[20], ps_hex[12], vs_hex[12], cs_hex[12], rva_hex[24];
        std::snprintf(pso_hex, sizeof(pso_hex), "0x%llx",
                      static_cast<unsigned long long>(ptr));
        if (e.has_ps)
            std::snprintf(ps_hex, sizeof(ps_hex), "0x%08x", e.ps_crc);
        else
            std::strcpy(ps_hex, "NO-PS");
        std::snprintf(vs_hex, sizeof(vs_hex), "0x%08x", e.vs_crc);
        std::snprintf(cs_hex, sizeof(cs_hex), "0x%08x", e.cs_crc);
        if (e.rva != 0) {
            // Resolve module name + offset exactly like sn2_rdoc_tags::format_rva.
            HMODULE hmod = nullptr;
            if (GetModuleHandleExW(
                    GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                    GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                    reinterpret_cast<LPCWSTR>(e.rva), &hmod) && hmod != nullptr) {
                wchar_t mod_path[MAX_PATH]{};
                GetModuleFileNameW(hmod, mod_path, MAX_PATH);
                const wchar_t* sl = wcsrchr(mod_path, L'\\');
                char mod_name[MAX_PATH]{};
                WideCharToMultiByte(CP_UTF8, 0,
                    sl ? sl + 1 : mod_path, -1,
                    mod_name, sizeof(mod_name), nullptr, nullptr);
                const uintptr_t base = reinterpret_cast<uintptr_t>(hmod);
                std::snprintf(rva_hex, sizeof(rva_hex), "%s+0x%llx",
                              mod_name,
                              static_cast<unsigned long long>(e.rva - base));
            } else {
                std::snprintf(rva_hex, sizeof(rva_hex), "0x%llx",
                              static_cast<unsigned long long>(e.rva));
            }
        } else {
            std::strcpy(rva_hex, "?");
        }
        doc.push_back({
            {"pso",    pso_hex},
            {"ps_crc", ps_hex},
            {"vs_crc", vs_hex},
            {"cs_crc", cs_hex},
            {"has_ps", e.has_ps},
            {"rva",    rva_hex},
        });
    }

    char path[2100];
    std::snprintf(path, sizeof(path), "%s\\sn2_pso_crc_map.json", dir.c_str());
    try {
        std::ofstream f{path, std::ios::binary | std::ios::trunc};
        if (f.good()) {
            const auto text = doc.dump(2);
            f.write(text.data(), static_cast<std::streamsize>(text.size()));
        }
    } catch (...) {}

    r.last_flush = std::chrono::steady_clock::now();
    r.dirty      = false;
}

// ---- Maybe flush (called after a new entry is added; throttled 5 s) --------
void maybe_flush(RegState& r) {
    // r.mu already held by caller.
    if (!r.dirty) return;
    const auto now = std::chrono::steady_clock::now();
    const auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
        now - r.last_flush).count();
    if (elapsed < 5) return;
    flush_sidecar_locked(r);
}

// ---- Record a PSO in the registry -----------------------------------------
void record_pso(uintptr_t pso_ptr, uint32_t ps_crc, bool has_ps,
                uint32_t vs_crc, uint32_t cs_crc, uintptr_t create_rva) {
    if (!gate_active() || pso_ptr == 0) return;
    auto& r = reg();
    std::scoped_lock _{r.mu};
    ensure_sidecar_dir(r);
    auto& e  = r.map[pso_ptr];
    e.ps_crc = has_ps ? ps_crc : 0;
    e.vs_crc = vs_crc;
    e.cs_crc = cs_crc;
    e.has_ps = has_ps;
    e.rva    = create_rva;
    r.dirty  = true;
    maybe_flush(r);
}

// ---- Original DXBC dump helpers -------------------------------------------
std::string& output_dir_dxbc() {
    static std::string s = []() -> std::string {
        char buf[2048]{};
        const auto len = GetEnvironmentVariableA("UEVR_SN2_PSO_BYTECODE_DIR", buf, sizeof(buf));
        if (len == 0 || len >= sizeof(buf)) return std::string{};
        return std::string{buf, len};
    }();
    return s;
}

void ensure_dxbc_dir(RegState& r) {
    if (r.dxbc_dir_created || output_dir_dxbc().empty()) return;
    std::error_code ec;
    std::filesystem::create_directories(output_dir_dxbc(), ec);
    r.dxbc_dir_created = true;
}

void dump_one(const void* bytecode, size_t size, uint32_t crc, const char* suffix) {
    if (bytecode == nullptr || size == 0 || crc == 0) return;
    if (output_dir_dxbc().empty()) return;
    auto& r = reg();
    const uint32_t key = crc ^ static_cast<uint32_t>(std::hash<std::string>{}(suffix));
    {
        std::scoped_lock _{r.mu};
        if (!r.dxbc_seen.insert(key).second) return;
        ensure_dxbc_dir(r);
    }
    char path[1024];
    std::snprintf(path, sizeof(path), "%s\\%s_0x%08x.dxbc",
                  output_dir_dxbc().c_str(), suffix, crc);
    try {
        std::ofstream f{path, std::ios::binary};
        if (f.good()) {
            f.write(reinterpret_cast<const char*>(bytecode), size);
            SPDLOG_INFO("[SN2-PsoDump] {} 0x{:08x} ({} bytes) -> {}",
                        suffix, crc, size, path);
        }
    } catch (...) {}
    // Reflection JSON sidecar (header-inline, self-gated).
    dump_reflection_json(bytecode, size, crc, suffix);
}

// ---- Stream walker helper --------------------------------------------------
// Walks a D3D12_PIPELINE_STATE_STREAM_DESC subobject stream and calls back
// with (type, bytecode_ptr, bytecode_size) for each shader-bytecode subobject.
// Stops when an unknown or zero-size subobject type is encountered (matches
// the same strategy used in D3D12Hook.cpp's stream walkers for UE5.6).
template <typename Fn>
bool walk_stream(const D3D12_PIPELINE_STATE_STREAM_DESC* desc, Fn&& cb) {
    if (desc == nullptr || desc->pPipelineStateSubobjectStream == nullptr ||
        desc->SizeInBytes == 0) return false;
    const auto* base = static_cast<const uint8_t*>(desc->pPipelineStateSubobjectStream);
    const size_t total = desc->SizeInBytes;
    constexpr size_t kSubAlign = alignof(void*);  // each subobject struct is alignas(void*)
    auto round_up = [](size_t v, size_t a) { return (v + a - 1) & ~(a - 1); };
    size_t pos = 0;
    while (true) {
        // Each subobject struct is alignas(void*) -> starts on an 8-byte boundary.
        pos = round_up(pos, kSubAlign);
        if (pos + sizeof(D3D12_PIPELINE_STATE_SUBOBJECT_TYPE) > total) break;
        const auto type = *reinterpret_cast<const D3D12_PIPELINE_STATE_SUBOBJECT_TYPE*>(base + pos);

        // Map each type to its sizeof(value_struct).
        size_t value_size = 0;
        switch (type) {
            case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_ROOT_SIGNATURE:        value_size = sizeof(ID3D12RootSignature*);               break;
            case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_VS:
            case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_PS:
            case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DS:
            case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_HS:
            case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_GS:
            case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_CS:
            case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_AS:
            case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_MS:                    value_size = sizeof(D3D12_SHADER_BYTECODE);               break;
            case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_STREAM_OUTPUT:         value_size = sizeof(D3D12_STREAM_OUTPUT_DESC);            break;
            case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_BLEND:                 value_size = sizeof(D3D12_BLEND_DESC);                    break;
            case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_SAMPLE_MASK:           value_size = sizeof(UINT);                                break;
            case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_RASTERIZER:            value_size = sizeof(D3D12_RASTERIZER_DESC);               break;
            case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DEPTH_STENCIL:         value_size = sizeof(D3D12_DEPTH_STENCIL_DESC);            break;
            case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DEPTH_STENCIL1:        value_size = sizeof(D3D12_DEPTH_STENCIL_DESC1);           break;
            case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_INPUT_LAYOUT:          value_size = sizeof(D3D12_INPUT_LAYOUT_DESC);             break;
            case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_IB_STRIP_CUT_VALUE:    value_size = sizeof(D3D12_INDEX_BUFFER_STRIP_CUT_VALUE);  break;
            case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_PRIMITIVE_TOPOLOGY:    value_size = sizeof(D3D12_PRIMITIVE_TOPOLOGY_TYPE);       break;
            case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_RENDER_TARGET_FORMATS: value_size = sizeof(D3D12_RT_FORMAT_ARRAY);               break;
            case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DEPTH_STENCIL_FORMAT:  value_size = sizeof(DXGI_FORMAT);                        break;
            case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_SAMPLE_DESC:           value_size = sizeof(DXGI_SAMPLE_DESC);                   break;
            case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_NODE_MASK:             value_size = sizeof(UINT);                               break;
            case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_CACHED_PSO:            value_size = sizeof(D3D12_CACHED_PIPELINE_STATE);        break;
            case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_FLAGS:                 value_size = sizeof(D3D12_PIPELINE_STATE_FLAGS);         break;
            case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_VIEW_INSTANCING:       value_size = sizeof(D3D12_VIEW_INSTANCING_DESC);         break;
            default: value_size = 0; break; // unknown or future subobject type
        }
        if (value_size == 0) break; // unknown/future subobject -> cannot safely skip

        // The inner value's offset WITHIN the subobject = round_up(sizeof(type_enum),
        // alignof(inner)). Pointer-containing inners (shader bytecode, root sig, input
        // layout, cached pso, stream output, view instancing) are 8-aligned; everything
        // else (FLAGS/NODE_MASK/BLEND/RASTERIZER/DEPTH_STENCIL/SAMPLE_MASK/...) is
        // 4-aligned. The old code aligned EVERY inner to 8, so a 4-aligned subobject
        // appearing before the PS derailed the walk -> every stream PSO became NO-PS.
        size_t inner_align = 4;
        switch (type) {
            case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_ROOT_SIGNATURE:
            case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_VS:
            case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_PS:
            case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DS:
            case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_HS:
            case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_GS:
            case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_CS:
            case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_AS:
            case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_MS:
            case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_STREAM_OUTPUT:
            case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_INPUT_LAYOUT:
            case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_CACHED_PSO:
            case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_VIEW_INSTANCING:
                inner_align = kSubAlign; break;
            default: inner_align = 4; break;
        }
        const size_t inner_off = pos + round_up(sizeof(D3D12_PIPELINE_STATE_SUBOBJECT_TYPE), inner_align);
        if (inner_off + value_size > total) break;

        // Fire the callback for shader-bytecode subobjects only.
        switch (type) {
            case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_VS:
            case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_PS:
            case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DS:
            case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_HS:
            case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_GS:
            case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_CS:
            case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_AS:
            case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_MS: {
                const auto& bc = *reinterpret_cast<const D3D12_SHADER_BYTECODE*>(base + inner_off);
                cb(type, bc.pShaderBytecode, bc.BytecodeLength);
            } break;
            default: break;
        }

        pos = inner_off + value_size; // loop top re-aligns to the next subobject
    }
    return true;
}

} // anonymous namespace

// ==========================================================================
// Public API
// ==========================================================================

// --------------------------------------------------------------------------
// on_create_graphics_pso — classic CreateGraphicsPipelineState path.
// --------------------------------------------------------------------------
void on_create_graphics_pso(const D3D12_GRAPHICS_PIPELINE_STATE_DESC* desc,
                            ID3D12PipelineState* pso,
                            uintptr_t create_rva) {
    if (desc == nullptr) return;

    const bool has_ps = desc->PS.pShaderBytecode != nullptr && desc->PS.BytecodeLength > 0;
    const uint32_t ps_crc = has_ps
        ? pso_crc32(desc->PS.pShaderBytecode, desc->PS.BytecodeLength) : 0;
    const bool has_vs = desc->VS.pShaderBytecode != nullptr && desc->VS.BytecodeLength > 0;
    const uint32_t vs_crc = has_vs
        ? pso_crc32(desc->VS.pShaderBytecode, desc->VS.BytecodeLength) : 0;

    if (pso != nullptr)
        record_pso(reinterpret_cast<uintptr_t>(pso),
                   ps_crc, has_ps, vs_crc, /*cs*/ 0, create_rva);

    // DXBC dump (original behaviour, gated on UEVR_SN2_PSO_BYTECODE_DIR).
    if (env_enabled()) {
        dump_one(desc->PS.pShaderBytecode, desc->PS.BytecodeLength, ps_crc, "ps");
        dump_one(desc->VS.pShaderBytecode, desc->VS.BytecodeLength, vs_crc, "vs");
        if (desc->GS.BytecodeLength > 0) {
            const uint32_t gs_crc = pso_crc32(desc->GS.pShaderBytecode, desc->GS.BytecodeLength);
            dump_one(desc->GS.pShaderBytecode, desc->GS.BytecodeLength, gs_crc, "gs");
        }
    }
}

// Legacy overload without pso/rva — keeps old call-sites compiling.
void on_create_graphics_pso(const D3D12_GRAPHICS_PIPELINE_STATE_DESC* desc,
                            uint32_t ps_crc_hint, uint32_t /*vs_crc_hint*/) {
    if (!env_enabled() || desc == nullptr) return;
    // ps_crc_hint may be 0 if ShaderRegistry wasn't recording yet; recompute.
    const bool has_ps = desc->PS.pShaderBytecode != nullptr && desc->PS.BytecodeLength > 0;
    const uint32_t ps_crc = (ps_crc_hint != 0) ? ps_crc_hint
        : (has_ps ? pso_crc32(desc->PS.pShaderBytecode, desc->PS.BytecodeLength) : 0);
    const bool has_vs = desc->VS.pShaderBytecode != nullptr && desc->VS.BytecodeLength > 0;
    const uint32_t vs_crc = has_vs
        ? pso_crc32(desc->VS.pShaderBytecode, desc->VS.BytecodeLength) : 0;
    dump_one(desc->PS.pShaderBytecode, desc->PS.BytecodeLength, ps_crc, "ps");
    dump_one(desc->VS.pShaderBytecode, desc->VS.BytecodeLength, vs_crc, "vs");
    if (desc->GS.BytecodeLength > 0) {
        const uint32_t gs_crc = pso_crc32(desc->GS.pShaderBytecode, desc->GS.BytecodeLength);
        dump_one(desc->GS.pShaderBytecode, desc->GS.BytecodeLength, gs_crc, "gs");
    }
}

// --------------------------------------------------------------------------
// on_create_compute_pso — classic CreateComputePipelineState path.
// --------------------------------------------------------------------------
void on_create_compute_pso(const D3D12_COMPUTE_PIPELINE_STATE_DESC* desc,
                           ID3D12PipelineState* pso,
                           uintptr_t create_rva) {
    if (desc == nullptr) return;
    const bool has_cs = desc->CS.pShaderBytecode != nullptr && desc->CS.BytecodeLength > 0;
    const uint32_t cs_crc = has_cs
        ? pso_crc32(desc->CS.pShaderBytecode, desc->CS.BytecodeLength) : 0;

    if (pso != nullptr)
        record_pso(reinterpret_cast<uintptr_t>(pso),
                   /*ps*/ 0, /*has_ps*/ false, /*vs*/ 0, cs_crc, create_rva);

    if (env_enabled())
        dump_one(desc->CS.pShaderBytecode, desc->CS.BytecodeLength, cs_crc, "cs");
}

// Legacy overload.
void on_create_compute_pso(const D3D12_COMPUTE_PIPELINE_STATE_DESC* desc,
                           uint32_t cs_crc_hint) {
    if (!env_enabled() || desc == nullptr) return;
    const bool has_cs = desc->CS.pShaderBytecode != nullptr && desc->CS.BytecodeLength > 0;
    const uint32_t cs_crc = (cs_crc_hint != 0) ? cs_crc_hint
        : (has_cs ? pso_crc32(desc->CS.pShaderBytecode, desc->CS.BytecodeLength) : 0);
    dump_one(desc->CS.pShaderBytecode, desc->CS.BytecodeLength, cs_crc, "cs");
}

// --------------------------------------------------------------------------
// on_create_pso_stream — ID3D12Device2::CreatePipelineState stream form.
//
// This is the PRIMARY path for all UE5 PSOs (~9 858 in SN2).
// ShaderOverrideRegistry.register_d3d12_pipeline_state_stream_creation is
// only called when the ShaderHunter is active, so d3d12_pso_pixel_crc32
// returns 0 for every stream PSO unless the Hunter runs.  This function
// records CRCs directly without depending on the registry, so the lookup
// works under UEVR_SN2_RDOC_TAGS alone.
// --------------------------------------------------------------------------
void on_create_pso_stream(const D3D12_PIPELINE_STATE_STREAM_DESC* desc,
                          ID3D12PipelineState* pso,
                          uintptr_t create_rva) {
    if (!gate_active() || desc == nullptr) return;

    uint32_t ps_crc{0}, vs_crc{0}, cs_crc{0};
    bool has_ps{false};
    // bc/sz are only used for DXBC dump; held here so we avoid re-walking.
    struct ShaderSlot { const void* bc{nullptr}; size_t sz{0}; };
    ShaderSlot ps_slot, vs_slot, cs_slot;

    walk_stream(desc, [&](D3D12_PIPELINE_STATE_SUBOBJECT_TYPE type,
                          const void* bc, size_t sz) {
        if (bc == nullptr || sz == 0) return;
        switch (type) {
            case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_PS:
                ps_slot = {bc, sz};
                ps_crc  = pso_crc32(bc, sz); has_ps = true;
                break;
            case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_VS:
                vs_slot = {bc, sz};
                vs_crc  = pso_crc32(bc, sz);
                break;
            case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_CS:
                cs_slot = {bc, sz};
                cs_crc  = pso_crc32(bc, sz);
                break;
            default: break;
        }
    });

    if (pso != nullptr)
        record_pso(reinterpret_cast<uintptr_t>(pso),
                   ps_crc, has_ps, vs_crc, cs_crc, create_rva);

    // DXBC dump (gated on UEVR_SN2_PSO_BYTECODE_DIR as before).
    if (env_enabled()) {
        dump_one(ps_slot.bc, ps_slot.sz, ps_crc, "ps");
        dump_one(vs_slot.bc, vs_slot.sz, vs_crc, "vs");
        dump_one(cs_slot.bc, cs_slot.sz, cs_crc, "cs");
    }
}

// Legacy overload (old three-pair signature kept for forward compatibility).
void on_create_pso_stream(const void* ps, size_t ps_size, uint32_t ps_crc,
                          const void* vs, size_t vs_size, uint32_t vs_crc,
                          const void* cs, size_t cs_size, uint32_t cs_crc) {
    if (!env_enabled()) return;
    dump_one(ps, ps_size, ps_crc, "ps");
    dump_one(vs, vs_size, vs_crc, "vs");
    dump_one(cs, cs_size, cs_crc, "cs");
}

// --------------------------------------------------------------------------
// PSO CRC lookups (non-blocking, call from any thread).
// Returns 0 when the PSO is unknown to this registry.
// Returns NOPS_CRC (0xFFFFFFFF) when the PSO IS known and has no PS.
// --------------------------------------------------------------------------
uint32_t ps_crc_for_pso(ID3D12PipelineState* pso) {
    if (pso == nullptr) return 0;
    auto& r = reg();
    std::scoped_lock _{r.mu};
    const auto it = r.map.find(reinterpret_cast<uintptr_t>(pso));
    if (it == r.map.end()) return 0;
    if (!it->second.has_ps) return NOPS_CRC;
    return it->second.ps_crc;
}

uint32_t vs_crc_for_pso(ID3D12PipelineState* pso) {
    if (pso == nullptr) return 0;
    auto& r = reg();
    std::scoped_lock _{r.mu};
    const auto it = r.map.find(reinterpret_cast<uintptr_t>(pso));
    return it != r.map.end() ? it->second.vs_crc : 0;
}

uint32_t cs_crc_for_pso(ID3D12PipelineState* pso) {
    if (pso == nullptr) return 0;
    auto& r = reg();
    std::scoped_lock _{r.mu};
    const auto it = r.map.find(reinterpret_cast<uintptr_t>(pso));
    return it != r.map.end() ? it->second.cs_crc : 0;
}

// --------------------------------------------------------------------------
// on_unload — flush the sidecar one last time at DLL teardown.
// --------------------------------------------------------------------------
void on_unload() {
    if (!gate_active()) return;
    auto& r = reg();
    std::scoped_lock _{r.mu};
    r.dirty = true;
    flush_sidecar_locked(r);
}

} // namespace sn2_pso_bytecode_dumper
