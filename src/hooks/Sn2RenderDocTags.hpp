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
#include <cstring>
#include <cstdio>
#include <mutex>
#include <string>
#include <unordered_map>

#include <windows.h>
#include <intrin.h>
#include <d3d12.h>

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

} // namespace sn2_rdoc_tags
