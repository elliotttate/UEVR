#pragma once
// =====================================================================
// Sn2RenderDocTags — make compiled-game GPU captures legible.
// =====================================================================
// A stripped UE5.6 binary tells the capture tool nothing: RenderDoc/PIX/
// Nsight show numeric resource IDs and opaque draw/dispatch events. This
// module injects, at runtime, the two things those tools faithfully
// surface but the game never sets:
//
//   1. Human-readable resource NAMES at creation (ID3D12Resource::SetName).
//      Visible in the RenderDoc Resource Inspector, PIX, and Nsight. Turns
//      "Resource 30076" into "SN2|FOGVOL|54x30x48|R11G11B10F|UAV|v1|...".
//
//   2. Per-event MARKERS on every draw/dispatch carrying the EYE, the bound
//      shader CRC, and the engine call-site RVA. The Event Browser then
//      reads e.g. "EYE_R | cs=0x5af52812 | Subnautica2.exe+0x2fbed20",
//      grouping the dump by eye and linking each GPU event straight back to
//      the function in IDA that emitted it.
//
// Pure observer: never mutates draw/dispatch arguments or resource desc.
// Everything is gated by UEVR_SN2_RDOC_TAGS=1 and OFF by default, so there
// is zero cost on a normal run — flip it on only when you intend to capture.
//
// Sub-toggles (all default to the master flag's value):
//   UEVR_SN2_RDOC_TAGS_NAMES=0   disable resource naming
//   UEVR_SN2_RDOC_TAGS_MARKERS=0 disable per-event markers
//   UEVR_SN2_RDOC_TAGS_ANSI=1    emit legacy ANSI markers (RenderDoc-only)
//                                instead of the universal PIX3 blob.
//
// Marker encoding: by default a PIX3 event blob (decoded by RenderDoc, PIX,
// and Nsight). The ANSI fallback is the simplest format RenderDoc reads and
// is handy if a tool ever chokes on the blob.
// =====================================================================

#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>

#include <windows.h>
#include <intrin.h>
#include <d3d12.h>
#include <wrl/client.h>

namespace sn2_rdoc_tags {

// ---------------------------------------------------------------------
// Env gating
// ---------------------------------------------------------------------
namespace detail {
    inline bool env_on(const char* name) {
        char v[16]{};
        const DWORD len = GetEnvironmentVariableA(name, v, sizeof(v));
        if (len == 0 || len >= sizeof(v)) return false;
        return std::strcmp(v, "0") != 0 && std::strcmp(v, "false") != 0 &&
               std::strcmp(v, "FALSE") != 0 && std::strcmp(v, "off") != 0 &&
               std::strcmp(v, "OFF") != 0;
    }
    // A sub-toggle defaults to the master flag unless explicitly set to "0".
    inline bool sub_on(const char* name, bool master) {
        char v[16]{};
        const DWORD len = GetEnvironmentVariableA(name, v, sizeof(v));
        if (len == 0 || len >= sizeof(v)) return master; // unset -> follow master
        return std::strcmp(v, "0") != 0 && std::strcmp(v, "false") != 0 &&
               std::strcmp(v, "FALSE") != 0 && std::strcmp(v, "off") != 0 &&
               std::strcmp(v, "OFF") != 0;
    }
}

inline bool enabled() {
    static const bool v = detail::env_on("UEVR_SN2_RDOC_TAGS");
    return v;
}
inline bool name_resources_enabled() {
    static const bool v = enabled() && detail::sub_on("UEVR_SN2_RDOC_TAGS_NAMES", enabled());
    return v;
}
inline bool markers_enabled() {
    static const bool v = enabled() && detail::sub_on("UEVR_SN2_RDOC_TAGS_MARKERS", enabled());
    return v;
}
inline bool ansi_markers() {
    static const bool v = detail::env_on("UEVR_SN2_RDOC_TAGS_ANSI");
    return v;
}
inline bool target_only_markers() {
    static const bool v = detail::env_on("UEVR_SN2_RDOC_TAGS_TARGET_ONLY");
    return v;
}

inline const std::unordered_set<uint32_t>& target_ps_crcs() {
    static const std::unordered_set<uint32_t> v = []() {
        std::unordered_set<uint32_t> out;
        char buf[1024]{};
        const DWORD len = GetEnvironmentVariableA("UEVR_SN2_RDOC_TAGS_TARGET_PS_CRCS", buf, sizeof(buf));
        if (len == 0 || len >= sizeof(buf)) {
            out.insert(0x13B00F0Cu);
            return out;
        }
        std::string s{buf, len};
        size_t pos = 0;
        while (pos < s.size()) {
            const size_t comma = s.find(',', pos);
            const std::string tok = s.substr(pos, comma == std::string::npos ? std::string::npos : comma - pos);
            char* end = nullptr;
            const unsigned long parsed = std::strtoul(tok.c_str(), &end, 0);
            if (end != tok.c_str()) {
                out.insert(static_cast<uint32_t>(parsed));
            }
            if (comma == std::string::npos) break;
            pos = comma + 1;
        }
        if (out.empty()) out.insert(0x13B00F0Cu);
        return out;
    }();
    return v;
}

inline const std::unordered_set<uint32_t>& target_cs_crcs() {
    static const std::unordered_set<uint32_t> v = []() {
        std::unordered_set<uint32_t> out;
        char buf[1024]{};
        const DWORD len = GetEnvironmentVariableA("UEVR_SN2_RDOC_TAGS_TARGET_CS_CRCS", buf, sizeof(buf));
        if (len == 0 || len >= sizeof(buf)) {
            return out;
        }
        std::string s{buf, len};
        size_t pos = 0;
        while (pos < s.size()) {
            const size_t comma = s.find(',', pos);
            const std::string tok = s.substr(pos, comma == std::string::npos ? std::string::npos : comma - pos);
            char* end = nullptr;
            const unsigned long parsed = std::strtoul(tok.c_str(), &end, 0);
            if (end != tok.c_str()) {
                out.insert(static_cast<uint32_t>(parsed));
            }
            if (comma == std::string::npos) break;
            pos = comma + 1;
        }
        return out;
    }();
    return v;
}

inline bool should_mark_draw(uint32_t ps_crc) {
    if (!target_only_markers()) return true;
    const auto& crcs = target_ps_crcs();
    return crcs.find(ps_crc) != crcs.end();
}

inline bool should_mark_dispatch(uint32_t cs_crc) {
    if (!target_only_markers()) return true;
    const auto& crcs = target_cs_crcs();
    return crcs.find(cs_crc) != crcs.end();
}

// ---------------------------------------------------------------------
// Address -> "module+0xRVA" so each event/resource links to IDA. Resolves
// the module that actually contains the return address (the render code may
// live in the main image or a DLL), and offsets within it — matching the
// base IDA used to disassemble that module. Cached per module handle.
// ---------------------------------------------------------------------
inline std::string format_rva(uintptr_t addr) {
    if (addr == 0) return "?";
    static std::mutex mu;
    static std::unordered_map<uintptr_t, std::string> name_by_base; // module base -> short name

    HMODULE hmod = nullptr;
    if (!GetModuleHandleExW(
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCWSTR>(addr), &hmod) || hmod == nullptr) {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "0x%llx", static_cast<unsigned long long>(addr));
        return buf;
    }

    const uintptr_t base = reinterpret_cast<uintptr_t>(hmod);
    std::string short_name;
    {
        std::scoped_lock _{mu};
        auto it = name_by_base.find(base);
        if (it != name_by_base.end()) {
            short_name = it->second;
        } else {
            wchar_t path[MAX_PATH]{};
            const DWORD n = GetModuleFileNameW(hmod, path, MAX_PATH);
            std::string nm = "mod";
            if (n > 0) {
                // basename
                const wchar_t* slash = wcsrchr(path, L'\\');
                const wchar_t* base_w = slash ? slash + 1 : path;
                char tmp[MAX_PATH]{};
                const int k = WideCharToMultiByte(CP_UTF8, 0, base_w, -1, tmp, sizeof(tmp), nullptr, nullptr);
                if (k > 0) nm = tmp;
            }
            name_by_base.emplace(base, nm);
            short_name = nm;
        }
    }

    char buf[MAX_PATH + 32];
    std::snprintf(buf, sizeof(buf), "%s+0x%llx", short_name.c_str(),
                  static_cast<unsigned long long>(addr - base));
    return buf;
}

// ---------------------------------------------------------------------
// DXGI format short names (the ones that actually show up in this game).
// ---------------------------------------------------------------------
inline const char* fmt_name(DXGI_FORMAT f) {
    switch (f) {
        case DXGI_FORMAT_R11G11B10_FLOAT:        return "R11G11B10F";
        case DXGI_FORMAT_R16G16B16A16_FLOAT:     return "RGBA16F";
        case DXGI_FORMAT_R32G32B32A32_FLOAT:     return "RGBA32F";
        case DXGI_FORMAT_R10G10B10A2_UNORM:      return "R10G10B10A2";
        case DXGI_FORMAT_R8G8B8A8_UNORM:         return "RGBA8";
        case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:    return "RGBA8srgb";
        case DXGI_FORMAT_B8G8R8A8_UNORM:         return "BGRA8";
        case DXGI_FORMAT_R16G16_FLOAT:           return "RG16F";
        case DXGI_FORMAT_R32_FLOAT:              return "R32F";
        case DXGI_FORMAT_R16_FLOAT:              return "R16F";
        case DXGI_FORMAT_R8_UNORM:               return "R8";
        case DXGI_FORMAT_R32_UINT:               return "R32U";
        case DXGI_FORMAT_R16_UINT:               return "R16U";
        case DXGI_FORMAT_D32_FLOAT:              return "D32F";
        case DXGI_FORMAT_D24_UNORM_S8_UINT:      return "D24S8";
        case DXGI_FORMAT_R24G8_TYPELESS:         return "R24G8tl";
        case DXGI_FORMAT_R32_TYPELESS:           return "R32tl";
        case DXGI_FORMAT_UNKNOWN:                return "UNK";
        default:                                 return nullptr; // caller prints fmt<N>
    }
}

// ---------------------------------------------------------------------
// Resource naming.
//   category : optional hint (e.g. "FOGVOL"); nullptr -> derived from desc.
//   view_at_create : sn2_get_current_fog_view() value (-1 unknown).
//   callsite_rva : engine return address of the create call.
// ---------------------------------------------------------------------
inline void name_resource(ID3D12Resource* res, const D3D12_RESOURCE_DESC* desc,
                          bool is_placed, UINT64 heap_offset, int view_at_create,
                          uintptr_t callsite_rva, const char* category) {
    if (!name_resources_enabled() || res == nullptr || desc == nullptr) return;

    static std::atomic<uint64_t> s_counter{0};
    const uint64_t idx = s_counter.fetch_add(1, std::memory_order_relaxed);

    char kind[16];
    switch (desc->Dimension) {
        case D3D12_RESOURCE_DIMENSION_BUFFER:    std::strcpy(kind, "buf");   break;
        case D3D12_RESOURCE_DIMENSION_TEXTURE1D: std::strcpy(kind, "tex1d"); break;
        case D3D12_RESOURCE_DIMENSION_TEXTURE2D: std::strcpy(kind, "tex2d"); break;
        case D3D12_RESOURCE_DIMENSION_TEXTURE3D: std::strcpy(kind, "tex3d"); break;
        default:                                 std::strcpy(kind, "res");   break;
    }

    // flags
    char flags[16] = {0};
    {
        size_t k = 0;
        auto add = [&](const char* s) {
            if (k != 0 && k < sizeof(flags) - 1) flags[k++] = '+';
            for (const char* p = s; *p && k < sizeof(flags) - 1; ++p) flags[k++] = *p;
            flags[k] = 0;
        };
        if (desc->Flags & D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET)        add("RT");
        if (desc->Flags & D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL)        add("DS");
        if (desc->Flags & D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS)     add("UAV");
        if (k == 0) std::strcpy(flags, "SR");
    }

    // dims
    char dims[48];
    if (desc->Dimension == D3D12_RESOURCE_DIMENSION_BUFFER) {
        std::snprintf(dims, sizeof(dims), "0x%llx", static_cast<unsigned long long>(desc->Width));
    } else {
        std::snprintf(dims, sizeof(dims), "%llux%ux%u",
                      static_cast<unsigned long long>(desc->Width),
                      static_cast<unsigned>(desc->Height),
                      static_cast<unsigned>(desc->DepthOrArraySize));
    }

    // format
    char fmtbuf[16];
    const char* fmt = fmt_name(desc->Format);
    if (fmt == nullptr) {
        std::snprintf(fmtbuf, sizeof(fmtbuf), "fmt%u", static_cast<unsigned>(desc->Format));
        fmt = fmtbuf;
    }

    char viewbuf[8];
    if (view_at_create == 0)      std::strcpy(viewbuf, "v0");
    else if (view_at_create == 1) std::strcpy(viewbuf, "v1");
    else                          std::strcpy(viewbuf, "v?");

    const std::string rva = format_rva(callsite_rva);

    char narrow[256];
    if (desc->Dimension == D3D12_RESOURCE_DIMENSION_BUFFER) {
        std::snprintf(narrow, sizeof(narrow), "SN2|%s|%s|%s|%s|%s|@%s|#%llu",
                      category ? category : kind, kind, dims, flags, viewbuf,
                      rva.c_str(), static_cast<unsigned long long>(idx));
    } else {
        std::snprintf(narrow, sizeof(narrow), "SN2|%s|%s|%s|%s|%s|%s|@%s|#%llu",
                      category ? category : kind, kind, dims, fmt, flags, viewbuf,
                      rva.c_str(), static_cast<unsigned long long>(idx));
    }
    if (is_placed) {
        char tail[32];
        std::snprintf(tail, sizeof(tail), "|ho=0x%llx", static_cast<unsigned long long>(heap_offset));
        const size_t cur = std::strlen(narrow);
        if (cur + std::strlen(tail) < sizeof(narrow)) std::strcat(narrow, tail);
    }

    wchar_t wide[256];
    const int n = MultiByteToWideChar(CP_UTF8, 0, narrow, -1, wide, 256);
    if (n > 0) {
        res->SetName(wide);
    }
}

// ---------------------------------------------------------------------
// PIX3 event-blob encoding (constants from WinPixEventRuntime
// PIXEventsCommon.h, MIT). Decoded by RenderDoc, PIX and Nsight.
// ---------------------------------------------------------------------
namespace pix3 {
    enum EventType : uint64_t {
        kEndEvent          = 0x000,
        kBeginEvent_NoArgs = 0x002,
        kSetMarker_NoArgs  = 0x008,
    };
    constexpr uint64_t kTypeReadMask         = 0x00000000000003FFull;
    constexpr uint64_t kTypeBitShift         = 10;
    constexpr uint64_t kTimestampReadMask    = 0x00000FFFFFFFFFFFull;
    constexpr uint64_t kTimestampBitShift    = 20;
    constexpr uint64_t kStrAlignReadMask     = 0xFull;
    constexpr uint64_t kStrAlignBitShift     = 60;
    constexpr uint64_t kStrChunkSzReadMask   = 0x1Full;
    constexpr uint64_t kStrChunkSzBitShift   = 55;
    constexpr uint64_t kStrIsANSIBitShift    = 54;
    constexpr uint64_t kStrIsShortcutBitShift= 53;

    inline uint64_t encode_event_info(uint64_t timestamp, uint64_t type) {
        return ((timestamp & kTimestampReadMask) << kTimestampBitShift) |
               ((type & kTypeReadMask) << kTypeBitShift);
    }
    inline uint64_t encode_string_info(uint64_t alignment, uint64_t chunk_size,
                                       bool is_ansi, bool is_shortcut) {
        return ((alignment & kStrAlignReadMask) << kStrAlignBitShift) |
               ((chunk_size & kStrChunkSzReadMask) << kStrChunkSzBitShift) |
               ((is_ansi ? 1ull : 0ull) << kStrIsANSIBitShift) |
               ((is_shortcut ? 1ull : 0ull) << kStrIsShortcutBitShift);
    }

    // Build a SetMarker blob with one ANSI string. Returns byte size (0 on fail).
    inline UINT build_marker(uint64_t* out, size_t out_cap, uint32_t color, const char* str) {
        if (out_cap < 5) return 0;
        size_t i = 0;
        out[i++] = encode_event_info(0, kSetMarker_NoArgs);
        out[i++] = static_cast<uint64_t>(color);
        out[i++] = encode_string_info(0, 8, /*is_ansi*/ true, /*is_shortcut*/ false);

        const char* p = str;
        bool done = false;
        while (!done && i < out_cap - 1) {
            uint64_t chunk = 0;
            char* cb = reinterpret_cast<char*>(&chunk);
            for (int b = 0; b < 8; ++b) {
                const char c = *p;
                cb[b] = c;
                if (c == '\0') { done = true; break; }
                ++p;
            }
            out[i++] = chunk; // little-endian: byte0 is first char
        }
        out[i++] = 0; // PIXEventsBlockEndMarker
        return static_cast<UINT>(i * sizeof(uint64_t));
    }

    // Build a BeginEvent blob with one ANSI string. Returns byte size (0 on fail).
    // RenderDoc surfaces BeginEvent/EndEvent as collapsible regions in the Event Browser.
    inline UINT build_begin_event(uint64_t* out, size_t out_cap, uint32_t color, const char* str) {
        if (out_cap < 5) return 0;
        size_t i = 0;
        out[i++] = encode_event_info(0, kBeginEvent_NoArgs);
        out[i++] = static_cast<uint64_t>(color);
        out[i++] = encode_string_info(0, 8, /*is_ansi*/ true, /*is_shortcut*/ false);

        const char* p = str;
        bool done = false;
        while (!done && i < out_cap - 1) {
            uint64_t chunk = 0;
            char* cb = reinterpret_cast<char*>(&chunk);
            for (int b = 0; b < 8; ++b) {
                const char c = *p;
                cb[b] = c;
                if (c == '\0') { done = true; break; }
                ++p;
            }
            out[i++] = chunk;
        }
        out[i++] = 0; // PIXEventsBlockEndMarker
        return static_cast<UINT>(i * sizeof(uint64_t));
    }

}

inline const char* eye_tag(int eye_bucket) {
    if (eye_bucket == 1) return "EYE_L";
    if (eye_bucket == 2) return "EYE_R";
    return "EYE_?";
}
inline uint32_t eye_color(int eye_bucket) {
    if (eye_bucket == 1) return 0xFFE8902Au; // warm (left)
    if (eye_bucket == 2) return 0xFF2AA8E8u; // cool (right)
    return 0xFF8A8A8Au;                      // gray (unknown)
}

// Emit a point marker on the command list: "EYE_x | <detail>".
inline void mark(ID3D12GraphicsCommandList* cl, int eye_bucket, const char* detail) {
    if (cl == nullptr || !markers_enabled()) return;

    char text[200];
    std::snprintf(text, sizeof(text), "%s | %s", eye_tag(eye_bucket), detail ? detail : "");

    if (ansi_markers()) {
        // Legacy PIX ANSI marker (RenderDoc reads this directly).
        constexpr UINT PIX_EVENT_ANSI_VERSION = 1;
        cl->SetMarker(PIX_EVENT_ANSI_VERSION, text, static_cast<UINT>(std::strlen(text) + 1));
        return;
    }

    uint64_t blob[48];
    const UINT bytes = pix3::build_marker(blob, 48, eye_color(eye_bucket), text);
    if (bytes == 0) return;
    constexpr UINT PIX_EVENT_PIX3BLOB_VERSION = 2;
    cl->SetMarker(PIX_EVENT_PIX3BLOB_VERSION, blob, bytes);
}

// ---------------------------------------------------------------------------
// begin_region / end_region — collapsible event regions (BeginEvent/EndEvent).
// RenderDoc renders these as foldable groups in the Event Browser and gives them
// a colour strip, making the fog pipeline instantly identifiable in a capture.
//
// Usage (must be balanced per-command-list):
//   begin_region(cl, eye_bucket, "SN2_FogPipeline");
//   ... dispatches ...
//   end_region(cl);
// ---------------------------------------------------------------------------
inline void begin_region(ID3D12GraphicsCommandList* cl, int eye_bucket, const char* label) {
    if (cl == nullptr || !markers_enabled()) return;

    char text[200];
    std::snprintf(text, sizeof(text), "%s | %s", eye_tag(eye_bucket), label ? label : "");

    if (ansi_markers()) {
        constexpr UINT PIX_EVENT_ANSI_VERSION = 1;
        cl->BeginEvent(PIX_EVENT_ANSI_VERSION, text, static_cast<UINT>(std::strlen(text) + 1));
        return;
    }

    uint64_t blob[64];
    const UINT bytes = pix3::build_begin_event(blob, 64, eye_color(eye_bucket), text);
    if (bytes == 0) return;
    constexpr UINT PIX_EVENT_PIX3BLOB_VERSION = 2;
    cl->BeginEvent(PIX_EVENT_PIX3BLOB_VERSION, blob, bytes);
}

inline void end_region(ID3D12GraphicsCommandList* cl) {
    if (cl == nullptr || !markers_enabled()) return;
    // ID3D12GraphicsCommandList::EndEvent takes no arguments regardless of
    // whether we used PIX3 blob or ANSI for the matching BeginEvent.
    cl->EndEvent();
}

// ---------------------------------------------------------------------------
// classify_resource_wellknown — return a semantic SN2 resource name for
// resources whose dimensions+format unambiguously identify them, or nullptr
// if the resource is not one of the known fog-pipeline resources.
//
// The returned pointer is a string literal (static storage). Callers may
// use it directly as the `category` argument to name_resource(), or call
// name_resource_wellknown() below which does the whole SetName.
//
// Heuristics (all gated at call site by name_resources_enabled()):
//
//   SN2_IntegratedLightScattering_froxel
//     Tex3D R11G11B10F, W∈[40,192], H∈[20,128], D∈[32,80], UAV
//     — the ILS / UWE voxel grid read by LightScatteringCS and SLW consumers.
//
//   SN2_LightScattering_froxel
//     Tex3D R16G16B16A16F, same size ranges
//     — the LightScattering accumulation volume seen in some quality configs.
//
//   SN2_FogHistory
//     Tex3D R11G11B10F, W∈[40,192], H∈[20,128], D∈[32,80], NO UAV flag
//     (read-only at the time of naming — temporal history input).
//
//   SN2_ResolvedFog2D
//     Tex2D R11G11B10F, W∈[256,2048], H∈[128,1024], UAV
//     — the 2D resolved/composited fog output.
//
//   SN2_SceneColorSBS
//     Tex2D R11G11B10F, W∈[1920,3840], H∈[360,1080], RT (no UAV)
//     — the full-width SBS scene-color buffer (≈2560×720 in 1280×720 native).
// ---------------------------------------------------------------------------
inline const char* classify_resource_wellknown(const D3D12_RESOURCE_DESC& d) {
    if (d.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE3D) {
        // ILS froxel + LightScattering froxel share the same size window.
        if (d.Width >= 40 && d.Width <= 192 &&
            d.Height >= 20 && d.Height <= 128 &&
            d.DepthOrArraySize >= 32 && d.DepthOrArraySize <= 80) {
            if (d.Format == DXGI_FORMAT_R11G11B10_FLOAT) {
                // Distinguish history (no UAV) from live froxel (UAV).
                if (d.Flags & D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS)
                    return "SN2_IntegratedLightScattering_froxel";
                else
                    return "SN2_FogHistory";
            }
            if (d.Format == DXGI_FORMAT_R16G16B16A16_FLOAT &&
                (d.Flags & D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS))
                return "SN2_LightScattering_froxel";
        }
    }
    else if (d.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE2D) {
        if (d.Format == DXGI_FORMAT_R11G11B10_FLOAT) {
            // SBS scene color: much wider than tall (≥2× aspect ratio), no UAV
            if (d.Width >= 1920 && d.Width <= 3840 &&
                d.Height >= 360 && d.Height <= 1080 &&
                (d.Flags & D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET) &&
                !(d.Flags & D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS) &&
                static_cast<UINT64>(d.Width) >= static_cast<UINT64>(d.Height) * 2)
                return "SN2_SceneColorSBS";

            // Resolved fog 2D: modest size, UAV-writable
            if (d.Width >= 256 && d.Width <= 2048 &&
                d.Height >= 128 && d.Height <= 1024 &&
                (d.Flags & D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS))
                return "SN2_ResolvedFog2D";
        }
    }
    return nullptr;
}

// name_resource_wellknown — like name_resource() but applies the semantic
// well-known name when classify_resource_wellknown() fires, falling back to
// the generic name_resource() with category=nullptr otherwise.
// Called from the CreateCommittedResource / CreatePlacedResource hooks in
// D3D12Hook.cpp (replaces the existing FOGVOL-only path when enabled).
inline void name_resource_wellknown(ID3D12Resource* res, const D3D12_RESOURCE_DESC* desc,
                                    bool is_placed, UINT64 heap_offset,
                                    int view_at_create, uintptr_t callsite_rva) {
    if (!name_resources_enabled() || res == nullptr || desc == nullptr) return;
    const char* wk = classify_resource_wellknown(*desc);
    // Well-known resources get a short, clean name so RenderDoc shows them directly.
    // Generic resources fall through to the full name_resource() encoding.
    if (wk != nullptr) {
        // Append the view index so per-eye duplicates are distinguishable.
        char vbuf[8];
        if (view_at_create == 0)      std::strcpy(vbuf, "_v0");
        else if (view_at_create == 1) std::strcpy(vbuf, "_v1");
        else                          std::strcpy(vbuf, "_v?");

        char narrow[128];
        std::snprintf(narrow, sizeof(narrow), "%s%s", wk, vbuf);
        wchar_t wide[128];
        const int n = MultiByteToWideChar(CP_UTF8, 0, narrow, -1, wide, 128);
        if (n > 0) {
            res->SetName(wide);
        }
    } else {
        name_resource(res, desc, is_placed, heap_offset, view_at_create, callsite_rva, nullptr);
    }
}

// ---------------------------------------------------------------------------
// fog_role_for_crc — map a shader CRC32 to a stable, human-readable role used
// when SetName()-ing the PSO. The string is a literal (static storage); the
// caller appends "|<crc8hex>|cs|ps". Returns nullptr for unknown CRCs so the
// PSO-naming path skips them (avoids spamming thousands of irrelevant PSOs).
//
// Roles seeded from the SN2 fog-pipeline RE (producers/consumers/composite).
// ---------------------------------------------------------------------------
inline const char* fog_role_for_crc(uint32_t crc) {
    switch (crc) {
        // PRODUCERS (compute)
        case 0xd1d94ed1u: return "FogProducer:MaterialSetupCS";
        case 0xd1f85c42u: return "FogProducer:LightScatteringCS";
        case 0x3402487cu: return "FogProducer:FinalIntegrationCS";
        case 0x0930dd4eu: return "FogResolve:UWEFogResolveCS";
        case 0xf996b96bu: return "FogReconstruct:UWEFogReconstructCS";
        case 0x9d14fcf0u: return "FogProducer:VoxelizePS";
        // CONSUMERS (pixel)
        case 0xb9be2499u: return "FogConsumer:SLW_Main";
        case 0xde7c3822u: return "FogConsumer:SLW_Main2";
        case 0x4a4eb78cu: return "FogConsumer:SLW_VolumeOverlay(pathB-froxel)";
        case 0x8733f2e0u: return "FogConsumer:SLW_NearFog";
        case 0xfedc00f9u: return "FogConsumer:SLW";
        case 0x8568e000u: return "FogConsumer:SLW";
        case 0x32040a0du: return "SLW_NoFog";
        case 0x13b00f0cu: return "FogConsumer:UnderwaterTealDraw";
        // OTHER
        case 0x4e86dc09u: return "FogComposite:ExponentialPixelMain(pathA)";
        case 0x166dba88u: return "Lumen_WaterSurface";
        default:          return nullptr;
    }
}

// name_pso — SetName a created engine PSO as "SN2_PSO|<role>|<crc8hex>|<cs|ps>".
// Only well-known fog-pipeline CRCs are named (fog_role_for_crc != nullptr) so
// the capture's pipeline list stays uncluttered. Gated by name_resources_enabled().
inline void name_pso(ID3D12PipelineState* pso, uint32_t crc, bool is_compute) {
    if (!name_resources_enabled() || pso == nullptr || crc == 0) return;
    const char* role = fog_role_for_crc(crc);
    if (role == nullptr) return; // unknown -> skip naming

    char narrow[160];
    std::snprintf(narrow, sizeof(narrow), "SN2_PSO|%s|0x%08x|%s",
                  role, crc, is_compute ? "cs" : "ps");
    wchar_t wide[160];
    const int n = MultiByteToWideChar(CP_UTF8, 0, narrow, -1, wide, 160);
    if (n > 0) {
        pso->SetName(wide);
    }
}

// ---------------------------------------------------------------------------
// name_descriptor_heap — SetName a heap as
//   "SN2_Heap|<TYPE>|<sv|nsv>|N=<NumDescriptors>"
// so RenderDoc's Resource Inspector shows the heap's purpose at a glance.
// Gated by name_resources_enabled().
// ---------------------------------------------------------------------------
inline const char* heap_type_name(D3D12_DESCRIPTOR_HEAP_TYPE t) {
    switch (t) {
        case D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV: return "CBV_SRV_UAV";
        case D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER:     return "SAMPLER";
        case D3D12_DESCRIPTOR_HEAP_TYPE_RTV:         return "RTV";
        case D3D12_DESCRIPTOR_HEAP_TYPE_DSV:         return "DSV";
        default:                                     return "HEAP";
    }
}
inline void name_descriptor_heap(ID3D12DescriptorHeap* heap, const D3D12_DESCRIPTOR_HEAP_DESC* desc) {
    if (!name_resources_enabled() || heap == nullptr || desc == nullptr) return;
    const bool shader_visible = (desc->Flags & D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE) != 0;
    char narrow[96];
    std::snprintf(narrow, sizeof(narrow), "SN2_Heap|%s|%s|N=%u",
                  heap_type_name(desc->Type),
                  shader_visible ? "sv" : "nsv",
                  static_cast<unsigned>(desc->NumDescriptors));
    wchar_t wide[96];
    const int n = MultiByteToWideChar(CP_UTF8, 0, narrow, -1, wide, 96);
    if (n > 0) {
        heap->SetName(wide);
    }
}

// ---------------------------------------------------------------------------
// name_root_signature — SetName a root signature as
//   "SN2_RootSig|params=<N>|<short blob hash>"
// param-count and blob hash are cheap, derived from the serialized blob the
// engine passed to CreateRootSignature (offset 8 of the v1.0/1.1 binary layout
// holds NumParameters). The hash is an FNV-1a over the blob so two distinct
// signatures get distinct tags even with the same param count.
// Gated by name_resources_enabled().
// ---------------------------------------------------------------------------
inline uint32_t blob_fnv1a(const void* data, size_t len) {
    const auto* p = static_cast<const uint8_t*>(data);
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < len; ++i) {
        h ^= p[i];
        h *= 16777619u;
    }
    return h;
}
inline void name_root_signature(ID3D12RootSignature* rs, const void* blob, size_t blob_len) {
    if (!name_resources_enabled() || rs == nullptr) return;
    // Serialized RS binary: [u32 version][u32 NumParameters][u32 ParametersOffset]...
    uint32_t num_params = 0;
    if (blob != nullptr && blob_len >= 8) {
        std::memcpy(&num_params, static_cast<const uint8_t*>(blob) + 4, sizeof(uint32_t));
        if (num_params > 256) num_params = 0; // sanity: not the layout we expect
    }
    const uint32_t hash = (blob != nullptr && blob_len > 0) ? blob_fnv1a(blob, blob_len) : 0;
    char narrow[64];
    std::snprintf(narrow, sizeof(narrow), "SN2_RootSig|params=%u|0x%08x",
                  static_cast<unsigned>(num_params), hash);
    wchar_t wide[64];
    const int n = MultiByteToWideChar(CP_UTF8, 0, narrow, -1, wide, 64);
    if (n > 0) {
        rs->SetName(wide);
    }
}

// ---------------------------------------------------------------------------
// name_swapchain_backbuffers — loop a DXGI swapchain's buffers and SetName each
// "SN2_Backbuffer_<i>". Runs at most once per swapchain pointer (idempotent).
// Pass the buffer count (DXGI_SWAP_CHAIN_DESC::BufferCount). Read-only on the
// engine resources (only SetName). Gated by name_resources_enabled().
// ---------------------------------------------------------------------------
template <typename TSwapChain>
inline void name_swapchain_backbuffers(TSwapChain* swap_chain, UINT buffer_count) {
    if (!name_resources_enabled() || swap_chain == nullptr || buffer_count == 0) return;
    static std::mutex mu;
    static std::unordered_set<const void*> done;
    {
        std::scoped_lock _{mu};
        if (!done.insert(static_cast<const void*>(swap_chain)).second) return; // already named
    }
    if (buffer_count > 8) buffer_count = 8; // bound the loop
    for (UINT i = 0; i < buffer_count; ++i) {
        Microsoft::WRL::ComPtr<ID3D12Resource> bb;
        if (SUCCEEDED(swap_chain->GetBuffer(i, IID_PPV_ARGS(&bb))) && bb != nullptr) {
            char narrow[32];
            std::snprintf(narrow, sizeof(narrow), "SN2_Backbuffer_%u", static_cast<unsigned>(i));
            wchar_t wide[32];
            const int n = MultiByteToWideChar(CP_UTF8, 0, narrow, -1, wide, 32);
            if (n > 0) bb->SetName(wide);
        }
    }
}

// ---------------------------------------------------------------------------
// color_for — phase/eye region colour. The eye dominates the hue (warm=L,
// cool=R) so the Event Browser strip groups by eye; the phase nudges the
// brightness so producer/consumer/composite regions read apart within an eye.
//   phase: 0=producer(dispatch) 1=consumer(draw) 2=composite/other
// ---------------------------------------------------------------------------
inline uint32_t color_for(int phase, int eye_bucket) {
    // Start from the per-eye base colour, then bias channels per phase.
    uint32_t c = eye_color(eye_bucket);
    switch (phase) {
        case 0: c = (eye_bucket == 2) ? 0xFF1C7FB4u : 0xFFB46E1Cu; break; // producer (darker)
        case 1: c = eye_color(eye_bucket);                          break; // consumer (base)
        case 2: c = (eye_bucket == 2) ? 0xFF55C2FFu : 0xFFFFB155u; break; // composite (brighter)
        default: break;
    }
    return c;
}

// ---------------------------------------------------------------------------
// build_dispatch_detail / build_draw_detail — rich per-event marker strings.
// Decoupled from CommandListCorrelationState (defined later in D3D12Hook.cpp):
// the caller extracts the primitive values and passes them in. Everything is
// read-only inspection of already-tracked root/scissor state.
//
// eye_bucket     : 1=LEFT 2=RIGHT 0=unknown (from cmdlist_eye_bucket(state)).
//                  Passed in so we can emit a definitive eye= tag regardless of
//                  whether the fog-hook view-index is populated.
// froxel_uav_va  : compute root-table/UAV GPU VA backing the froxel store
//                  (producer store target). 0 if unbound/unknown.
// store_base_x   : derived froxel store base-X if determinable, else -1.
//
// VIEW INDEX RESOLUTION (for the view= field):
//   Priority 1: fog-hook view_index (0 or 1) — authoritative when set.
//   Priority 2: eye_bucket (1→view 0, 2→view 1) — reliable for draw calls
//               whose PSO scissor has already been classified by the stereo
//               trace layer (most per-eye render-thread draws).
//   Priority 3: scissor X origin — for draws: left < SN2_SBS_HALF_WIDTH(1200)
//               → view 0/LEFT, else → view 1/RIGHT.  For dispatches: not
//               available (no scissor); falls through to eye_bucket only.
//   If all three are unavailable the field emits view=? and eye=?.
//
// WHAT IS NOT POPULATED (requires a new engine hook):
//   fview=0x...      — the FViewInfo* pointer for this draw/dispatch. Would
//                      require hooking into UE5's render-thread view iteration
//                      (FSceneRenderer::Render loop) to record each FViewInfo*
//                      alongside its eye index before D3D12 commands are
//                      recorded. The existing sn2_get_current_fog_view() hook
//                      (FFakeStereoRenderingHook.cpp) fires only for the fog
//                      compute path and does not expose the CPU pointer.
//   stereoPass=<n>   — FViewInfo::StereoPass enum (eSSP_FULL/LEFT/RIGHT).
//                      Same gap: needs a hook at FSceneView construction
//                      (FSceneView::FSceneView in SceneView.cpp) or at the
//                      view-array iteration in FSceneRenderer to bind the enum
//                      to the thread-local fog-view index. Once that hook
//                      records (thread_id, stereo_pass) at render-view setup,
//                      the value can be passed down here as a new parameter.
// ---------------------------------------------------------------------------

// Derive a view index (0=L, 1=R, -1=unknown) from eye_bucket when the
// fog-hook view_index is unavailable. eye_bucket: 1=LEFT, 2=RIGHT, else unknown.
inline int view_from_eye_bucket(int eye_bucket) {
    if (eye_bucket == 1) return 0;
    if (eye_bucket == 2) return 1;
    return -1;
}

// Derive a view index from scissor left-X for draw calls.
// SN2 native SBS target is 2560 wide; the right eye starts at x>=1200 (matching
// the classify_rects() threshold already used by the stereo trace layer).
inline int view_from_scissor_x(bool has_scissor, long sx_left) {
    if (!has_scissor) return -1;
    return (sx_left >= 1200) ? 1 : 0;
}

// Resolve the best available view index from the three priority sources.
// For dispatches, pass has_scissor=false (compute shaders have no scissor).
inline int resolve_view_index(int fog_hook_view, int eye_bucket,
                              bool has_scissor, long sx_left) {
    if (fog_hook_view == 0 || fog_hook_view == 1) return fog_hook_view;
    const int from_bucket = view_from_eye_bucket(eye_bucket);
    if (from_bucket >= 0) return from_bucket;
    return view_from_scissor_x(has_scissor, sx_left); // -1 if none available
}

inline void build_dispatch_detail(char* out, size_t cap,
                                  uint32_t cs_crc,
                                  unsigned gx, unsigned gy, unsigned gz,
                                  int view_index,
                                  int eye_bucket,
                                  uintptr_t froxel_uav_va,
                                  int store_base_x,
                                  const char* rdg,
                                  const char* src) {
    if (out == nullptr || cap == 0) return;
    char rolebuf[24];
    const char* role = fog_role_for_crc(cs_crc);
    if (role == nullptr) { std::strcpy(rolebuf, "?"); role = rolebuf; }
    char xbuf[16];
    if (store_base_x >= 0) std::snprintf(xbuf, sizeof(xbuf), "%d", store_base_x);
    else                   std::strcpy(xbuf, "?");

    // Resolve the definitive view index: fog-hook > eye_bucket > scissor.
    // Dispatches have no scissor so the last fallback is eye_bucket only.
    const int resolved_view = resolve_view_index(view_index, eye_bucket,
                                                 /*has_scissor*/ false, 0L);
    const char* eye_str = (resolved_view == 0) ? "L"
                        : (resolved_view == 1) ? "R"
                        : "?";
    char viewbuf[16];
    if (resolved_view >= 0) std::snprintf(viewbuf, sizeof(viewbuf), "%d", resolved_view);
    else                    std::strcpy(viewbuf, "?");

    std::snprintf(out, cap,
                  "DISP cs=0x%08x [%s] grp=%ux%ux%u view=%s eye=%s froxelUAV=0x%llx storeX=%s rdg=\"%s\" src=%s",
                  cs_crc, role, gx, gy, gz, viewbuf, eye_str,
                  static_cast<unsigned long long>(froxel_uav_va), xbuf,
                  rdg ? rdg : "", src ? src : "");
}

inline void build_draw_detail(char* out, size_t cap,
                              uint32_t ps_crc,
                              unsigned count_a, unsigned inst_count, bool indexed,
                              int view_index,
                              int eye_bucket,
                              uintptr_t froxel_srv_t5_va,
                              bool has_scissor, long sx_left, long sx_right,
                              const char* rdg,
                              const char* src) {
    if (out == nullptr || cap == 0) return;
    char rolebuf[24];
    const char* role = fog_role_for_crc(ps_crc);
    if (role == nullptr) { std::strcpy(rolebuf, "?"); role = rolebuf; }

    // Resolve the definitive view index: fog-hook > eye_bucket > scissor X.
    const int resolved_view = resolve_view_index(view_index, eye_bucket,
                                                 has_scissor, sx_left);
    const char* eye_str = (resolved_view == 0) ? "L"
                        : (resolved_view == 1) ? "R"
                        : "?";
    char viewbuf[16];
    if (resolved_view >= 0) std::snprintf(viewbuf, sizeof(viewbuf), "%d", resolved_view);
    else                    std::strcpy(viewbuf, "?");

    // vrect=[x0,x1] — the scissor X span this draw writes to.
    char vrectbuf[32];
    if (has_scissor) std::snprintf(vrectbuf, sizeof(vrectbuf), "[%ld,%ld]", sx_left, sx_right);
    else             std::strcpy(vrectbuf, "none");

    std::snprintf(out, cap,
                  "%s ps=0x%08x [%s] %s=%u i=%u view=%s eye=%s vrect=%s t5froxel=0x%llx rdg=\"%s\" src=%s",
                  indexed ? "DRAWIDX" : "DRAW", ps_crc, role,
                  indexed ? "idx" : "v", count_a, inst_count, viewbuf, eye_str, vrectbuf,
                  static_cast<unsigned long long>(froxel_srv_t5_va),
                  rdg ? rdg : "", src ? src : "");
}

} // namespace sn2_rdoc_tags
