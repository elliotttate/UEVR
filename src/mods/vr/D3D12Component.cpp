#include <d3dcompiler.h>

#include <openvr.h>
#include <utility/Module.hpp>
#include <utility/String.hpp>
#include <utility/ScopeGuard.hpp>
#include <utility/Logging.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <DirectXMath.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <sstream>
#include <string_view>
#include <unordered_set>
#include <vector>

#include "Framework.hpp"
#include "render/D3D12Diagnostics.hpp"
#include "render/ShaderCompiler.hpp"
#include "../GameSpecific.hpp"
#include "../VR.hpp"

#include <sdk/Utility.hpp>

#include <../../directxtk12-src/Inc/ResourceUploadBatch.h>
#include <../../directxtk12-src/Inc/RenderTargetState.h>

#include "shaders/Compiled/alpha_luminance_sprite_ps_SpritePixelShader.inc"
#include "shaders/Compiled/alpha_luminance_sprite_ps_SpriteVertexShader.inc"

#include "d3d12/DirectXTK.hpp"

#include "../../hooks/Sn2DebugResources.hpp"
#include "../../hooks/DIBRDepthTracker.hpp"

#include "D3D12Component.hpp"

//#define AFR_DEPTH_TEMP_DISABLED

constexpr auto ENGINE_SRC_DEPTH = D3D12_RESOURCE_STATE_DEPTH_READ | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
constexpr auto ENGINE_SRC_COLOR = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;

namespace sn2_openxr_array_diag {
    static bool env_on(const char* name) {
        char b[16]{};
        const auto n = GetEnvironmentVariableA(name, b, sizeof(b));
        return n != 0 && n < sizeof(b) && b[0] && b[0] != '0';
    }

    static UINT env_u32(const char* name, UINT fallback) {
        char b[32]{};
        const auto n = GetEnvironmentVariableA(name, b, sizeof(b));
        if (n == 0 || n >= sizeof(b)) {
            return fallback;
        }
        char* end = nullptr;
        const auto v = std::strtoul(b, &end, 0);
        return end != b ? static_cast<UINT>(v) : fallback;
    }

    struct RtvRing {
        Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> heap{};
        UINT stride{0};
        UINT next{0};
        UINT capacity{64};
        ID3D12Device* device{nullptr};
        std::mutex mutex{};
    };

    static RtvRing& rtv_ring() {
        static RtvRing s{};
        return s;
    }

    static bool ensure_rtv_ring(ID3D12Device* device) {
        auto& r = rtv_ring();
        std::scoped_lock lock{r.mutex};
        if (r.heap != nullptr && r.device == device) {
            return true;
        }

        r.heap.Reset();
        r.device = nullptr;
        r.next = 0;
        r.stride = 0;

        if (device == nullptr) {
            return false;
        }

        D3D12_DESCRIPTOR_HEAP_DESC hd{};
        hd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        hd.NumDescriptors = r.capacity;
        hd.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        if (FAILED(device->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&r.heap))) || r.heap == nullptr) {
            SPDLOG_WARN("[SN2-OpenXRArrayDiag] CreateDescriptorHeap(RTV ring) failed");
            return false;
        }
        r.device = device;
        r.stride = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
        SPDLOG_WARN("[SN2-OpenXRArrayDiag] RTV ring ready capacity={} stride={}", r.capacity, r.stride);
        return true;
    }

    static bool clear_slice_if_enabled(d3d12::CommandContext& commands, ID3D12Resource* dst) {
        if (!env_on("UEVR_SN2_OPENXR_ARRAY_CLEAR")) {
            return false;
        }
        if (dst == nullptr || commands.cmd_list == nullptr || g_framework == nullptr || g_framework->get_d3d12_hook() == nullptr) {
            return false;
        }

        auto* device = g_framework->get_d3d12_hook()->get_device();
        if (!ensure_rtv_ring(device)) {
            return false;
        }

        const UINT slice = env_u32("UEVR_SN2_OPENXR_ARRAY_CLEAR_EYE", 1);
        const auto desc = dst->GetDesc();
        if (desc.Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE2D || slice >= desc.DepthOrArraySize) {
            SPDLOG_WARN_ONCE("[SN2-OpenXRArrayDiag] clear skipped: dim={} array={} requested_slice={}",
                static_cast<unsigned>(desc.Dimension),
                static_cast<unsigned>(desc.DepthOrArraySize),
                slice);
            return false;
        }
        if ((desc.Flags & D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET) == 0) {
            SPDLOG_WARN_ONCE("[SN2-OpenXRArrayDiag] clear skipped: OpenXR array image is not RT-capable flags=0x{:x}",
                static_cast<unsigned>(desc.Flags));
            return false;
        }

        D3D12_CPU_DESCRIPTOR_HANDLE handle{};
        {
            auto& r = rtv_ring();
            std::scoped_lock lock{r.mutex};
            const UINT idx = r.next++ % r.capacity;
            handle = r.heap->GetCPUDescriptorHandleForHeapStart();
            handle.ptr += static_cast<SIZE_T>(idx) * static_cast<SIZE_T>(r.stride);

            D3D12_RENDER_TARGET_VIEW_DESC rtv{};
            rtv.Format = desc.Format;
            rtv.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2DARRAY;
            rtv.Texture2DArray.MipSlice = 0;
            rtv.Texture2DArray.FirstArraySlice = slice;
            rtv.Texture2DArray.ArraySize = 1;
            rtv.Texture2DArray.PlaneSlice = 0;
            device->CreateRenderTargetView(dst, &rtv, handle);
        }

        const float magenta[4]{1.0f, 0.0f, 1.0f, 1.0f};
        commands.cmd_list->ClearRenderTargetView(handle, magenta, 0, nullptr);
        commands.has_commands = true;

        static uint64_t s_count = 0;
        ++s_count;
        if (s_count <= 16 || (s_count % 600) == 0) {
            SPDLOG_WARN("[SN2-OpenXRArrayDiag] cleared OpenXR native array slice={} dst=0x{:x} {}x{} array={} fmt={} n={}",
                slice,
                reinterpret_cast<uintptr_t>(dst),
                static_cast<unsigned>(desc.Width),
                static_cast<unsigned>(desc.Height),
                static_cast<unsigned>(desc.DepthOrArraySize),
                static_cast<unsigned>(desc.Format),
                s_count);
        }
        return true;
    }

    static bool clear_source_box_if_enabled(
        d3d12::CommandContext& commands,
        ID3D12Resource* src,
        const D3D12_BOX& box,
        D3D12_RESOURCE_STATES src_state)
    {
        if (!env_on("UEVR_SN2_OPENXR_SOURCE_CLEAR")) {
            return false;
        }
        // This mutates the game's source backbuffer from UEVR's OpenXR copy
        // command context. It device-removed once in SN2 (DXGI_ERROR_INVALID_CALL),
        // so require a second explicit opt-in and keep it diagnostic-only.
        if (!env_on("UEVR_SN2_OPENXR_SOURCE_CLEAR_UNSAFE")) {
            SPDLOG_WARN_ONCE("[SN2-OpenXRArrayDiag] UEVR_SN2_OPENXR_SOURCE_CLEAR requested but ignored; set UEVR_SN2_OPENXR_SOURCE_CLEAR_UNSAFE=1 to run the known-risk source mutation probe");
            return false;
        }
        if (src == nullptr || commands.cmd_list == nullptr || g_framework == nullptr || g_framework->get_d3d12_hook() == nullptr) {
            return false;
        }

        auto* device = g_framework->get_d3d12_hook()->get_device();
        if (!ensure_rtv_ring(device)) {
            return false;
        }

        const auto desc = src->GetDesc();
        if (desc.Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE2D) {
            SPDLOG_WARN_ONCE("[SN2-OpenXRArrayDiag] source clear skipped: dim={}",
                static_cast<unsigned>(desc.Dimension));
            return false;
        }
        if ((desc.Flags & D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET) == 0) {
            SPDLOG_WARN_ONCE("[SN2-OpenXRArrayDiag] source clear skipped: source is not RT-capable flags=0x{:x}",
                static_cast<unsigned>(desc.Flags));
            return false;
        }

        D3D12_CPU_DESCRIPTOR_HANDLE handle{};
        {
            auto& r = rtv_ring();
            std::scoped_lock lock{r.mutex};
            const UINT idx = r.next++ % r.capacity;
            handle = r.heap->GetCPUDescriptorHandleForHeapStart();
            handle.ptr += static_cast<SIZE_T>(idx) * static_cast<SIZE_T>(r.stride);

            D3D12_RENDER_TARGET_VIEW_DESC rtv{};
            rtv.Format = desc.Format;
            if (desc.DepthOrArraySize > 1) {
                rtv.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2DARRAY;
                rtv.Texture2DArray.MipSlice = 0;
                rtv.Texture2DArray.FirstArraySlice = 0;
                rtv.Texture2DArray.ArraySize = 1;
                rtv.Texture2DArray.PlaneSlice = 0;
            } else {
                rtv.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
                rtv.Texture2D.MipSlice = 0;
                rtv.Texture2D.PlaneSlice = 0;
            }
            device->CreateRenderTargetView(src, &rtv, handle);
        }

        D3D12_RESOURCE_BARRIER barrier{};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource = src;
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        barrier.Transition.StateBefore = src_state;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
        if (src_state != D3D12_RESOURCE_STATE_RENDER_TARGET) {
            commands.cmd_list->ResourceBarrier(1, &barrier);
        }

        const D3D12_RECT rect{
            static_cast<LONG>(box.left),
            static_cast<LONG>(box.top),
            static_cast<LONG>(box.right),
            static_cast<LONG>(box.bottom)
        };
        const float magenta[4]{1.0f, 0.0f, 1.0f, 1.0f};
        commands.cmd_list->ClearRenderTargetView(handle, magenta, 1, &rect);

        if (src_state != D3D12_RESOURCE_STATE_RENDER_TARGET) {
            barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
            barrier.Transition.StateAfter = src_state;
            commands.cmd_list->ResourceBarrier(1, &barrier);
        }
        commands.has_commands = true;

        static uint64_t s_count = 0;
        ++s_count;
        if (s_count <= 16 || (s_count % 600) == 0) {
            SPDLOG_WARN("[SN2-OpenXRArrayDiag] cleared OpenXR source box src=0x{:x} {}x{} fmt={} state={} rect=({}, {}, {}, {}) n={}",
                reinterpret_cast<uintptr_t>(src),
                static_cast<unsigned>(desc.Width),
                static_cast<unsigned>(desc.Height),
                static_cast<unsigned>(desc.Format),
                static_cast<unsigned>(src_state),
                rect.left, rect.top, rect.right, rect.bottom,
                s_count);
        }
        return true;
    }
}

// 2026-05-24 SN2 RIGHT-EYE COLOR TRANSFER (present-time cosmetic fix).
// The right eye renders the scene above-water (warm) while the left renders it underwater (teal);
// the per-view divergence is in the basepass lighting and not reachable from a runtime hook. This
// compute pass, run on the final SBS image before the per-eye OpenXR copy, gives the RIGHT half the
// LEFT half's color (chroma) while keeping the right's own luminance — so the right eye gains the
// underwater tint but keeps its geometry/parallax. Gated by UEVR_SN2_RIGHT_EYE_COLOR_TRANSFER.
namespace sn2_color_transfer {
    template <typename T> using ComPtr = Microsoft::WRL::ComPtr<T>;
    static bool env_on() {
        static const bool v = []() {
            char b[8]{}; const auto n = GetEnvironmentVariableA("UEVR_SN2_RIGHT_EYE_COLOR_TRANSFER", b, sizeof(b));
            return n != 0 && n < sizeof(b) && b[0] && b[0] != '0';
        }();
        return v;
    }
    static float strength() {
        static const float s = []() {
            char b[16]{}; const auto n = GetEnvironmentVariableA("UEVR_SN2_COLOR_TRANSFER_STRENGTH", b, sizeof(b));
            if (n == 0 || n >= sizeof(b)) return 1.0f;
            const float f = (float)atof(b); return (f < 0.0f) ? 0.0f : (f > 1.0f ? 1.0f : f);
        }();
        return s;
    }

    static bool g_attempted = false;
    static bool g_ok = false;
    static ComPtr<ID3D12RootSignature> g_rs{};
    static ComPtr<ID3D12PipelineState> g_pso{};
    static ComPtr<ID3D12DescriptorHeap> g_heap{};
    static ComPtr<ID3D12Resource> g_cb{};
    static uint8_t* g_cb_ptr = nullptr;
    static ComPtr<ID3D12Resource> g_temp{};
    static UINT g_tw = 0, g_th = 0;
    static DXGI_FORMAT g_tfmt = DXGI_FORMAT_UNKNOWN;
    static d3d12::CommandContext g_cmd{};

    static bool ensure_init(ID3D12Device* device) {
        if (g_attempted) return g_ok;
        g_attempted = true;
        // Compile the transfer shader (runtime HLSL->DXIL via dxc).
        render::ShaderCompileRequest req{};
        char path[512]{};
        const auto n = GetEnvironmentVariableA("UEVR_SN2_COLOR_TRANSFER_SHADER", path, sizeof(path));
        req.source_path = (n != 0 && n < sizeof(path))
            ? std::filesystem::path(path)
            : std::filesystem::path(L"E:/Github/Subnautica 2/moddingkit/shaders/sn2_right_eye_color_transfer.hlsl");
        req.entry_point = "main";
        req.profile = "cs_6_0";
        req.warnings_as_errors = false;
        const auto res = render::compile_shader_file(req);
        if (!res.succeeded || res.bytecode.empty()) {
            SPDLOG_ERROR("[SN2-ColorTransfer] shader compile FAILED: {}", res.error);
            return false;
        }
        D3D12_DESCRIPTOR_RANGE uav_range{};
        uav_range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
        uav_range.NumDescriptors = 1; uav_range.BaseShaderRegister = 0; uav_range.RegisterSpace = 0;
        uav_range.OffsetInDescriptorsFromTableStart = 0;
        D3D12_ROOT_PARAMETER params[2]{};
        params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
        params[0].Descriptor.ShaderRegister = 0; params[0].Descriptor.RegisterSpace = 0;
        params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        params[1].DescriptorTable.NumDescriptorRanges = 1;
        params[1].DescriptorTable.pDescriptorRanges = &uav_range;
        params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        D3D12_ROOT_SIGNATURE_DESC rsd{};
        rsd.NumParameters = 2; rsd.pParameters = params; rsd.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;
        ComPtr<ID3DBlob> sig{}, err{};
        if (FAILED(D3D12SerializeRootSignature(&rsd, D3D_ROOT_SIGNATURE_VERSION_1, &sig, &err))) {
            SPDLOG_ERROR("[SN2-ColorTransfer] root sig serialize failed"); return false;
        }
        if (FAILED(device->CreateRootSignature(0, sig->GetBufferPointer(), sig->GetBufferSize(), IID_PPV_ARGS(&g_rs)))) {
            SPDLOG_ERROR("[SN2-ColorTransfer] CreateRootSignature failed"); return false;
        }
        D3D12_COMPUTE_PIPELINE_STATE_DESC pd{};
        pd.pRootSignature = g_rs.Get();
        pd.CS.pShaderBytecode = res.bytecode.data(); pd.CS.BytecodeLength = res.bytecode.size();
        if (FAILED(device->CreateComputePipelineState(&pd, IID_PPV_ARGS(&g_pso)))) {
            SPDLOG_ERROR("[SN2-ColorTransfer] CreateComputePipelineState failed"); return false;
        }
        D3D12_DESCRIPTOR_HEAP_DESC hd{};
        hd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV; hd.NumDescriptors = 1;
        hd.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        if (FAILED(device->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&g_heap)))) {
            SPDLOG_ERROR("[SN2-ColorTransfer] CreateDescriptorHeap failed"); return false;
        }
        D3D12_HEAP_PROPERTIES up{}; up.Type = D3D12_HEAP_TYPE_UPLOAD;
        D3D12_RESOURCE_DESC cbd{};
        cbd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER; cbd.Width = 256; cbd.Height = 1;
        cbd.DepthOrArraySize = 1; cbd.MipLevels = 1; cbd.Format = DXGI_FORMAT_UNKNOWN;
        cbd.SampleDesc.Count = 1; cbd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        if (FAILED(device->CreateCommittedResource(&up, D3D12_HEAP_FLAG_NONE, &cbd,
                D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&g_cb)))) {
            SPDLOG_ERROR("[SN2-ColorTransfer] CB create failed"); return false;
        }
        D3D12_RANGE rr{0, 0};
        if (FAILED(g_cb->Map(0, &rr, reinterpret_cast<void**>(&g_cb_ptr)))) {
            SPDLOG_ERROR("[SN2-ColorTransfer] CB map failed"); return false;
        }
        if (!g_cmd.setup(L"SN2 color transfer")) {
            SPDLOG_ERROR("[SN2-ColorTransfer] command context setup failed"); return false;
        }
        g_ok = true;
        SPDLOG_WARN("[SN2-ColorTransfer] initialized OK (strength={})", strength());
        return true;
    }

    static DXGI_FORMAT uav_typed_format(DXGI_FORMAT f) {
        switch (f) {
        case DXGI_FORMAT_B8G8R8A8_TYPELESS:
        case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:    return DXGI_FORMAT_B8G8R8A8_UNORM;
        case DXGI_FORMAT_R8G8B8A8_TYPELESS:
        case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:    return DXGI_FORMAT_R8G8B8A8_UNORM;
        case DXGI_FORMAT_R10G10B10A2_TYPELESS:   return DXGI_FORMAT_R10G10B10A2_UNORM;
        case DXGI_FORMAT_R16G16B16A16_TYPELESS:  return DXGI_FORMAT_R16G16B16A16_FLOAT;
        default:                                  return f; // already a typed UAV-capable format
        }
    }

    static bool ensure_temp(ID3D12Device* device, UINT w, UINT h, DXGI_FORMAT fmt) {
        if (g_temp && g_tw == w && g_th == h && g_tfmt == fmt) return true;
        D3D12_HEAP_PROPERTIES dp{}; dp.Type = D3D12_HEAP_TYPE_DEFAULT;
        D3D12_RESOURCE_DESC td{};
        td.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D; td.Width = w; td.Height = h;
        td.DepthOrArraySize = 1; td.MipLevels = 1; td.Format = fmt; td.SampleDesc.Count = 1;
        td.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
        ComPtr<ID3D12Resource> t{};
        if (FAILED(device->CreateCommittedResource(&dp, D3D12_HEAP_FLAG_NONE, &td,
                D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&t)))) {
            SPDLOG_ERROR("[SN2-ColorTransfer] temp tex create failed (fmt={})", (int)fmt);
            return false;
        }
        g_temp = t; g_tw = w; g_th = h; g_tfmt = fmt;
        // The resource keeps the (possibly typeless) backbuffer format for CopyResource compatibility,
        // but the UAV must be a fully-typed format the compute can load/store.
        D3D12_UNORDERED_ACCESS_VIEW_DESC ud{};
        ud.Format = uav_typed_format(fmt); ud.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
        device->CreateUnorderedAccessView(g_temp.Get(), nullptr, &ud, g_heap->GetCPUDescriptorHandleForHeapStart());
        SPDLOG_WARN("[SN2-ColorTransfer] temp created {}x{} resFmt={} uavFmt={}", w, h, (int)fmt, (int)ud.Format);
        return true;
    }

    static void barrier(ID3D12GraphicsCommandList* cl, ID3D12Resource* r,
                        D3D12_RESOURCE_STATES a, D3D12_RESOURCE_STATES b) {
        D3D12_RESOURCE_BARRIER br{}; br.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        br.Transition.pResource = r; br.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        br.Transition.StateBefore = a; br.Transition.StateAfter = b;
        cl->ResourceBarrier(1, &br);
    }

    static void run(ID3D12Resource* sbs, D3D12_RESOURCE_STATES sbs_state, UINT w, UINT h) {
        if (!env_on() || sbs == nullptr || w < 2 || h == 0) return;
        auto* device = g_framework->get_d3d12_hook()->get_device();
        if (device == nullptr) return;
        if (!ensure_init(device)) return;
        const auto desc = sbs->GetDesc();
        w = static_cast<UINT>(desc.Width); h = desc.Height; // authoritative dims from the resource
        if (w < 2 || h == 0) return;
        if (!ensure_temp(device, w, h, desc.Format)) return;

        struct Params { uint32_t W, H, Half; float Strength; } p{ w, h, w / 2u, strength() };
        memcpy(g_cb_ptr, &p, sizeof(p));

        g_cmd.wait(INFINITE); // wait for previous + reset list ready to record
        auto* cl = g_cmd.cmd_list.Get();
        if (cl == nullptr) return;

        barrier(cl, sbs, sbs_state, D3D12_RESOURCE_STATE_COPY_SOURCE);
        barrier(cl, g_temp.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_DEST);
        cl->CopyResource(g_temp.Get(), sbs);
        barrier(cl, g_temp.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        ID3D12DescriptorHeap* heaps[] = { g_heap.Get() };
        cl->SetDescriptorHeaps(1, heaps);
        cl->SetComputeRootSignature(g_rs.Get());
        cl->SetPipelineState(g_pso.Get());
        cl->SetComputeRootConstantBufferView(0, g_cb->GetGPUVirtualAddress());
        cl->SetComputeRootDescriptorTable(1, g_heap->GetGPUDescriptorHandleForHeapStart());
        cl->Dispatch((w + 7) / 8, (h + 7) / 8, 1);
        barrier(cl, g_temp.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
        barrier(cl, sbs, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_COPY_DEST);
        cl->CopyResource(sbs, g_temp.Get());
        barrier(cl, sbs, D3D12_RESOURCE_STATE_COPY_DEST, sbs_state);
        barrier(cl, g_temp.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_COMMON);

        g_cmd.has_commands = true;
        g_cmd.execute();
        g_cmd.wait(INFINITE); // block until processed, before the eye copies read the backbuffer

        static std::atomic<uint64_t> rn{0};
        const auto cnt = rn.fetch_add(1, std::memory_order_relaxed);
        if (cnt < 8 || (cnt % 600) == 0) {
            SPDLOG_WARN("[SN2-ColorTransfer] ran #{} sbs={}x{} fmt={} half={}", cnt + 1, w, h, (int)desc.Format, w / 2);
        }
    }
} // namespace sn2_color_transfer

// 2026-05-28 SN2: depth-aware right-eye fog blend.
// Reads slice 0 (left, contains teal fog) and slice 1 (right, missing fog) of an
// already-populated OpenXR native-stereo-array texture, plus UE's SceneDepthZ (an
// SBS depth with left[0..w/2] and right[w/2..w] halves). For each pixel in slice 1,
// if the right pixel's depth is at the far plane (= sky / volumetric-fog region),
// replace with slice 0's color. Otherwise keep slice 1 (preserves right-eye
// foreground parallax). This is the parallax-correct version of MIRROR_FULL.
//
// Env switches:
//   UEVR_SN2_RIGHT_EYE_DEPTH_BLEND       = 1  enable
//   UEVR_SN2_RIGHT_EYE_DEPTH_THRESHOLD  = 0.9999  (reversed-Z) far-plane cutoff
//   UEVR_SN2_RIGHT_EYE_DEPTH_SOFTNESS   = 0.0005  smoothstep half-width
//   UEVR_SN2_RIGHT_EYE_DEPTH_REVERSED_Z = 1   (UE5 default is reversed-Z)
namespace sn2_depth_blend {
    template <typename T> using ComPtr = Microsoft::WRL::ComPtr<T>;
    static bool env_on() {
        static const bool v = []() {
            char b[8]{}; const auto n = GetEnvironmentVariableA("UEVR_SN2_RIGHT_EYE_DEPTH_BLEND", b, sizeof(b));
            return n != 0 && n < sizeof(b) && b[0] && b[0] != '0';
        }();
        return v;
    }
    static float depth_threshold() {
        static const float v = []() {
            char b[32]{}; const auto n = GetEnvironmentVariableA("UEVR_SN2_RIGHT_EYE_DEPTH_THRESHOLD", b, sizeof(b));
            return (n != 0 && n < sizeof(b)) ? (float)atof(b) : 0.0005f; // reversed-Z far ≈ 0
        }();
        return v;
    }
    static float depth_softness() {
        static const float v = []() {
            char b[32]{}; const auto n = GetEnvironmentVariableA("UEVR_SN2_RIGHT_EYE_DEPTH_SOFTNESS", b, sizeof(b));
            return (n != 0 && n < sizeof(b)) ? (float)atof(b) : 0.0001f;
        }();
        return v;
    }
    static uint32_t reversed_z() {
        static const uint32_t v = []() {
            char b[8]{}; const auto n = GetEnvironmentVariableA("UEVR_SN2_RIGHT_EYE_DEPTH_REVERSED_Z", b, sizeof(b));
            return (n != 0 && n < sizeof(b) && b[0] != '0') ? 1u : 1u; // UE5 default = reversed
        }();
        return v;
    }

    static bool g_attempted = false;
    static bool g_ok = false;
    static ComPtr<ID3D12RootSignature> g_rs{};
    static ComPtr<ID3D12PipelineState> g_pso{};
    static ComPtr<ID3D12DescriptorHeap> g_heap{};
    static ComPtr<ID3D12Resource> g_cb{};
    static uint8_t* g_cb_ptr = nullptr;
    static UINT g_inc = 0;
    // Intermediate one-eye UNORM UAV texture. The OpenXR native-stereo-array swapchain
    // is _SRGB with no UNORDERED_ACCESS usage, so we cannot UAV-write it directly (the
    // old code did -> device-remove on submit). The CS writes here, then we copy this
    // into array slice 1 (copy is valid on the swapchain).
    static ComPtr<ID3D12Resource> g_intermediate{};
    static UINT g_inter_w = 0;
    static UINT g_inter_h = 0;

    static bool ensure_init(ID3D12Device* device) {
        if (g_attempted) return g_ok;
        g_attempted = true;
        render::ShaderCompileRequest req{};
        char path[512]{};
        const auto n = GetEnvironmentVariableA("UEVR_SN2_DEPTH_BLEND_SHADER", path, sizeof(path));
        req.source_path = (n != 0 && n < sizeof(path))
            ? std::filesystem::path(path)
            : std::filesystem::path(L"E:/Github/Subnautica 2/moddingkit/shaders/sn2_right_eye_depth_blend.hlsl");
        req.entry_point = "main";
        req.profile = "cs_6_0";
        req.warnings_as_errors = false;
        const auto res = render::compile_shader_file(req);
        if (!res.succeeded || res.bytecode.empty()) {
            SPDLOG_ERROR("[SN2-DepthBlend] shader compile FAILED: {}", res.error);
            return false;
        }
        D3D12_DESCRIPTOR_RANGE ranges[2]{};
        ranges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV; // t0,t1
        ranges[0].NumDescriptors = 2; ranges[0].BaseShaderRegister = 0; ranges[0].RegisterSpace = 0;
        ranges[0].OffsetInDescriptorsFromTableStart = 0;
        ranges[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV; // u0
        ranges[1].NumDescriptors = 1; ranges[1].BaseShaderRegister = 0; ranges[1].RegisterSpace = 0;
        ranges[1].OffsetInDescriptorsFromTableStart = 2;
        D3D12_ROOT_PARAMETER params[2]{};
        params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
        params[0].Descriptor.ShaderRegister = 0; params[0].Descriptor.RegisterSpace = 0;
        params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        params[1].DescriptorTable.NumDescriptorRanges = 2;
        params[1].DescriptorTable.pDescriptorRanges = ranges;
        params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        D3D12_ROOT_SIGNATURE_DESC rsd{};
        rsd.NumParameters = 2; rsd.pParameters = params; rsd.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;
        ComPtr<ID3DBlob> sig{}, err{};
        if (FAILED(D3D12SerializeRootSignature(&rsd, D3D_ROOT_SIGNATURE_VERSION_1, &sig, &err))) {
            SPDLOG_ERROR("[SN2-DepthBlend] root sig serialize failed"); return false;
        }
        if (FAILED(device->CreateRootSignature(0, sig->GetBufferPointer(), sig->GetBufferSize(), IID_PPV_ARGS(&g_rs)))) {
            SPDLOG_ERROR("[SN2-DepthBlend] CreateRootSignature failed"); return false;
        }
        D3D12_COMPUTE_PIPELINE_STATE_DESC pd{};
        pd.pRootSignature = g_rs.Get();
        pd.CS.pShaderBytecode = res.bytecode.data(); pd.CS.BytecodeLength = res.bytecode.size();
        if (FAILED(device->CreateComputePipelineState(&pd, IID_PPV_ARGS(&g_pso)))) {
            SPDLOG_ERROR("[SN2-DepthBlend] CreateComputePipelineState failed"); return false;
        }
        // Round-robin descriptor slots across frames to avoid GPU-in-flight races.
        // 30 slots = 10 frames × 3 descriptors per dispatch.
        D3D12_DESCRIPTOR_HEAP_DESC hd{};
        hd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV; hd.NumDescriptors = 30;
        hd.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        if (FAILED(device->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&g_heap)))) {
            SPDLOG_ERROR("[SN2-DepthBlend] CreateDescriptorHeap failed"); return false;
        }
        g_inc = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        D3D12_HEAP_PROPERTIES up{}; up.Type = D3D12_HEAP_TYPE_UPLOAD;
        D3D12_RESOURCE_DESC cbd{};
        cbd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER; cbd.Width = 256; cbd.Height = 1;
        cbd.DepthOrArraySize = 1; cbd.MipLevels = 1; cbd.Format = DXGI_FORMAT_UNKNOWN;
        cbd.SampleDesc.Count = 1; cbd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        if (FAILED(device->CreateCommittedResource(&up, D3D12_HEAP_FLAG_NONE, &cbd,
                D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&g_cb)))) {
            SPDLOG_ERROR("[SN2-DepthBlend] CB create failed"); return false;
        }
        D3D12_RANGE rr{0, 0};
        if (FAILED(g_cb->Map(0, &rr, reinterpret_cast<void**>(&g_cb_ptr)))) {
            SPDLOG_ERROR("[SN2-DepthBlend] CB map failed"); return false;
        }
        g_ok = true;
        SPDLOG_WARN("[SN2-DepthBlend] initialized OK (thresh={} soft={} revZ={})",
            depth_threshold(), depth_softness(), reversed_z());
        return true;
    }

    // Run the depth blend pass. Caller passes a recording command list that already
    // has both slices populated. We add SRV-for-array-color, SRV-for-depth, and
    // UAV-for-array-color(slice 1 only) descriptors into our heap, set the compute
    // PSO, and dispatch.
    static void run(ID3D12GraphicsCommandList* cl,
                    ID3D12Resource* stereo_array_color, D3D12_RESOURCE_STATES color_state_in,
                    ID3D12Resource* sbs_depth,           D3D12_RESOURCE_STATES depth_state_in,
                    UINT slice_w, UINT slice_h, UINT backbuffer_w)
    {
        if (!env_on() || cl == nullptr || stereo_array_color == nullptr) return;
        // Depth is OPTIONAL — luma-only heuristic works without it.
        auto* device = g_framework->get_d3d12_hook()->get_device();
        if (device == nullptr) return;
        if (!ensure_init(device)) return;
        if (slice_w == 0 || slice_h == 0) return;

        // Pack CB.
        struct CB {
            uint32_t dim_x, dim_y;
            float    depth_far_threshold;
            float    blend_softness;
            uint32_t backbuffer_w;
            uint32_t reversed_z;
            uint32_t luma_only;
            uint32_t _pad0;
        } cb{ slice_w, slice_h, depth_threshold(), depth_softness(), backbuffer_w, reversed_z(),
              (sbs_depth == nullptr) ? 1u : 0u, 0 };
        memcpy(g_cb_ptr, &cb, sizeof(cb));

        // Descriptor writes. Heap layout per group: [color SRV][depth SRV][color UAV].
        // Round-robin across frames to avoid GPU-in-flight races on descriptor reuse.
        static std::atomic<UINT> s_group_idx{0};
        const UINT group = s_group_idx.fetch_add(1, std::memory_order_relaxed) % 10u;
        const auto color_desc = stereo_array_color->GetDesc();
        const D3D12_RESOURCE_DESC depth_desc = (sbs_depth != nullptr) ? sbs_depth->GetDesc() : D3D12_RESOURCE_DESC{};

        // UNORM (non-sRGB) format of the swapchain's family — UAV-compatible and
        // copy-compatible with the _SRGB swapchain slice.
        const DXGI_FORMAT unorm_fmt =
            (color_desc.Format == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB ||
             color_desc.Format == DXGI_FORMAT_R8G8B8A8_UNORM ||
             color_desc.Format == DXGI_FORMAT_R8G8B8A8_TYPELESS)
                ? DXGI_FORMAT_R8G8B8A8_UNORM
                : DXGI_FORMAT_B8G8R8A8_UNORM;

        // Lazy-create / resize the intermediate one-eye UNORM UAV texture.
        if (g_intermediate == nullptr || g_inter_w != slice_w || g_inter_h != slice_h) {
            g_intermediate.Reset();
            D3D12_HEAP_PROPERTIES dp{}; dp.Type = D3D12_HEAP_TYPE_DEFAULT;
            D3D12_RESOURCE_DESC td{};
            td.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
            td.Width = slice_w; td.Height = slice_h;
            td.DepthOrArraySize = 1; td.MipLevels = 1;
            td.Format = unorm_fmt;
            td.SampleDesc.Count = 1;
            td.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
            td.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
            if (FAILED(device->CreateCommittedResource(&dp, D3D12_HEAP_FLAG_NONE, &td,
                    D3D12_RESOURCE_STATE_COPY_SOURCE, nullptr, IID_PPV_ARGS(&g_intermediate)))) {
                SPDLOG_ERROR("[SN2-DepthBlend] intermediate create FAILED {}x{} fmt={}",
                    slice_w, slice_h, (int)unorm_fmt);
                g_intermediate.Reset();
                return;
            }
            g_inter_w = slice_w; g_inter_h = slice_h;
            SPDLOG_WARN("[SN2-DepthBlend] intermediate UAV texture created {}x{} fmt={}",
                slice_w, slice_h, (int)unorm_fmt);
        }
        D3D12_CPU_DESCRIPTOR_HANDLE cpu = g_heap->GetCPUDescriptorHandleForHeapStart();
        cpu.ptr += static_cast<SIZE_T>(group) * 3u * g_inc;
        D3D12_GPU_DESCRIPTOR_HANDLE gpu = g_heap->GetGPUDescriptorHandleForHeapStart();
        gpu.ptr += static_cast<UINT64>(group) * 3u * g_inc;

        // SRV t0: Texture2DArray<float4> (entire array, both slices readable).
        {
            D3D12_SHADER_RESOURCE_VIEW_DESC sd{};
            sd.Format = color_desc.Format == DXGI_FORMAT_R8G8B8A8_TYPELESS ? DXGI_FORMAT_R8G8B8A8_UNORM
                       : color_desc.Format == DXGI_FORMAT_B8G8R8A8_TYPELESS ? DXGI_FORMAT_B8G8R8A8_UNORM
                       : color_desc.Format;
            sd.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
            sd.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            sd.Texture2DArray.MostDetailedMip = 0;
            sd.Texture2DArray.MipLevels = 1;
            sd.Texture2DArray.FirstArraySlice = 0;
            sd.Texture2DArray.ArraySize = 2;
            sd.Texture2DArray.PlaneSlice = 0;
            sd.Texture2DArray.ResourceMinLODClamp = 0.0f;
            device->CreateShaderResourceView(stereo_array_color, &sd, cpu);
            cpu.ptr += g_inc;
        }
        // SRV t1: Texture2D<float> depth (or null if not available — shader ignores).
        {
            D3D12_SHADER_RESOURCE_VIEW_DESC sd{};
            sd.Format = DXGI_FORMAT_R32_FLOAT;
            sd.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
            sd.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            sd.Texture2D.MostDetailedMip = 0;
            sd.Texture2D.MipLevels = 1;
            sd.Texture2D.PlaneSlice = 0;
            sd.Texture2D.ResourceMinLODClamp = 0.0f;
            if (sbs_depth != nullptr) {
                // UE depth is typically D32_FLOAT; SRV must be R32_FLOAT.
                sd.Format = (depth_desc.Format == DXGI_FORMAT_R32_TYPELESS ||
                             depth_desc.Format == DXGI_FORMAT_D32_FLOAT ||
                             depth_desc.Format == DXGI_FORMAT_D32_FLOAT_S8X24_UINT)
                            ? DXGI_FORMAT_R32_FLOAT : depth_desc.Format;
            }
            device->CreateShaderResourceView(sbs_depth, &sd, cpu);
            cpu.ptr += g_inc;
        }
        // UAV u0: the INTERMEDIATE Texture2D (NOT the swapchain array — see HLSL note).
        {
            D3D12_UNORDERED_ACCESS_VIEW_DESC ud{};
            ud.Format = unorm_fmt;
            ud.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
            ud.Texture2D.MipSlice = 0;
            ud.Texture2D.PlaneSlice = 0;
            device->CreateUnorderedAccessView(g_intermediate.Get(), nullptr, &ud, cpu);
        }

        // Pre-dispatch barriers: array (both slices) -> NON_PIXEL_SHADER_RESOURCE (SRV read),
        // depth -> NPS, intermediate COPY_SOURCE -> UAV.
        {
            D3D12_RESOURCE_BARRIER pre[3]{};
            UINT pre_n = 0;
            if (color_state_in != D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE) {
                auto& b = pre[pre_n++]; b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
                b.Transition.pResource = stereo_array_color;
                b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
                b.Transition.StateBefore = color_state_in;
                b.Transition.StateAfter = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
            }
            if (sbs_depth != nullptr && depth_state_in != D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE) {
                auto& b = pre[pre_n++]; b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
                b.Transition.pResource = sbs_depth;
                b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
                b.Transition.StateBefore = depth_state_in;
                b.Transition.StateAfter = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
            }
            {
                auto& b = pre[pre_n++]; b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
                b.Transition.pResource = g_intermediate.Get();
                b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
                b.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE;
                b.Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
            }
            cl->ResourceBarrier(pre_n, pre);
        }

        ID3D12DescriptorHeap* heaps[] = { g_heap.Get() };
        cl->SetDescriptorHeaps(1, heaps);
        cl->SetComputeRootSignature(g_rs.Get());
        cl->SetPipelineState(g_pso.Get());
        cl->SetComputeRootConstantBufferView(0, g_cb->GetGPUVirtualAddress());
        cl->SetComputeRootDescriptorTable(1, gpu);
        cl->Dispatch((slice_w + 7) / 8, (slice_h + 7) / 8, 1);

        // Post-dispatch: intermediate UAV -> COPY_SOURCE; array slice 1 (subres 1) NPS -> COPY_DEST.
        {
            D3D12_RESOURCE_BARRIER b2[2]{};
            b2[0].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            b2[0].Transition.pResource = g_intermediate.Get();
            b2[0].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            b2[0].Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
            b2[0].Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
            b2[1].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            b2[1].Transition.pResource = stereo_array_color;
            b2[1].Transition.Subresource = 1; // array slice 1, mip 0
            b2[1].Transition.StateBefore = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
            b2[1].Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
            cl->ResourceBarrier(2, b2);
        }

        // Copy intermediate (the blended right eye) -> array slice 1.
        {
            D3D12_TEXTURE_COPY_LOCATION dstL{};
            dstL.pResource = stereo_array_color;
            dstL.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
            dstL.SubresourceIndex = 1; // array slice 1
            D3D12_TEXTURE_COPY_LOCATION srcL{};
            srcL.pResource = g_intermediate.Get();
            srcL.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
            srcL.SubresourceIndex = 0;
            cl->CopyTextureRegion(&dstL, 0, 0, 0, &srcL, nullptr);
        }

        // Restore: array slice 1 COPY_DEST -> color_state_in; array slice 0 NPS -> color_state_in;
        // depth NPS -> depth_state_in.
        {
            D3D12_RESOURCE_BARRIER post[3]{};
            UINT post_n = 0;
            {
                auto& b = post[post_n++]; b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
                b.Transition.pResource = stereo_array_color;
                b.Transition.Subresource = 1;
                b.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
                b.Transition.StateAfter = color_state_in;
            }
            if (color_state_in != D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE) {
                auto& b = post[post_n++]; b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
                b.Transition.pResource = stereo_array_color;
                b.Transition.Subresource = 0; // slice 0 was moved to NPS; restore it
                b.Transition.StateBefore = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
                b.Transition.StateAfter = color_state_in;
            }
            if (sbs_depth != nullptr && depth_state_in != D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE) {
                auto& b = post[post_n++]; b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
                b.Transition.pResource = sbs_depth;
                b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
                b.Transition.StateBefore = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
                b.Transition.StateAfter = depth_state_in;
            }
            cl->ResourceBarrier(post_n, post);
        }

        static std::atomic<uint64_t> rn{0};
        const auto cnt = rn.fetch_add(1, std::memory_order_relaxed);
        if (cnt < 8 || (cnt % 600) == 0) {
            const HRESULT dr = device->GetDeviceRemovedReason();
            SPDLOG_WARN("[SN2-DepthBlend] dispatched #{} slice={}x{} bb_w={} depth={} luma_only={} array_fmt={} device_removed=0x{:x}",
                cnt + 1, slice_w, slice_h, backbuffer_w,
                (sbs_depth != nullptr) ? 1 : 0, cb.luma_only, (int)color_desc.Format, (uint32_t)dr);
        }
    }
} // namespace sn2_depth_blend

// ===========================================================================
// sn2_debug — RenderDoc / OpenXR capture-readability debug resources.
//
// Owned by agent OWNEDRES. Declared in src/hooks/Sn2DebugResources.hpp; called
// from the D3D12 dispatch/draw hooks. EVERYTHING here is default-OFF and uses
// ONLY UEVR-owned committed resources in their own heaps. We NEVER call
// CopyTextureRegion / ResourceBarrier / SetDescriptorHeaps against an engine
// resource or the engine's command list. The owned->staging readback runs on
// our OWN CommandContext queue after a fence.
//
// DEVICE-REMOVAL NOTE: features #4 (watermark) and #11 (sentinel) are the only
// device-removal-risk surfaces in the capture-readability batch. They are
// PENDING LIVE VALIDATION. The risky part is NOT the C++ here (which only
// touches owned resources) but the STAGED dxil_text_patch manifests that bind
// the owned texture into a space99 UAV slot / redirect the t5 SRV. Those
// manifests are hand-deployed for a validation run only.
// ===========================================================================
namespace sn2_debug {
    template <typename T> using DComPtr = Microsoft::WRL::ComPtr<T>;

    static bool env_truthy_local(const char* name) {
        char b[16]{};
        const auto n = GetEnvironmentVariableA(name, b, (DWORD)sizeof(b));
        return n != 0 && n < sizeof(b) && b[0] && b[0] != '0';
    }

    // ---- feature gates ----------------------------------------------------
    bool fog_fill_watermark_enabled() {
        static const bool v = env_truthy_local("UEVR_SN2_FOG_FILL_WATERMARK");
        return v;
    }
    bool sentinel_froxel_enabled() {
        static const bool v = env_truthy_local("UEVR_SN2_SENTINEL_FROXEL");
        return v;
    }

    static bool verbose_log() {
        static const bool v = env_truthy_local("UEVR_SN2_DEBUG_RES_LOG");
        return v;
    }

    // Producer CS CRCs we watermark (UWEFogResolveCS + LightScattering family).
    static bool is_producer_crc(uint32_t cs_crc) {
        return cs_crc == 0x0930dd4eu  // UWEFogResolveCS (store-x redirect target)
            || cs_crc == 0xd1f85c42u  // LightScatteringCS variant
            || cs_crc == 0x3402487cu; // FinalIntegration / froxel-fill variant
    }

    // Froxel grid X-by-Y the watermark maps. SBS family froxel is ~107 wide
    // (each eye fills ~53). 30 rows is enough to make the L/R-empty split legible.
    static constexpr UINT kWmWidth  = 107;
    static constexpr UINT kWmHeight = 30;
    // Sentinel test froxel (matches the family froxel dimensions ~107x30x48).
    static constexpr UINT kSentinelW = 107;
    static constexpr UINT kSentinelH = 30;
    static constexpr UINT kSentinelD = 48;

    struct State {
        std::mutex mutex{};
        bool attempted_watermark = false;
        bool attempted_sentinel  = false;

        // #4 watermark: owned RWTexture2D<uint> (R32_UINT) + own readback buffer.
        DComPtr<ID3D12Resource> wm_tex{};       // DEFAULT heap, UAV-capable.
        DComPtr<ID3D12Resource> wm_readback{};  // READBACK heap, CPU-mappable.
        UINT64 wm_readback_total = 0;
        D3D12_PLACED_SUBRESOURCE_FOOTPRINT wm_footprint{};
        UINT wm_rows = 0;
        UINT64 wm_row_size = 0;

        // #11 sentinel: owned Texture3D (R11G11B10F) filled once with X-gradient.
        DComPtr<ID3D12Resource> sentinel_tex{};
        bool sentinel_filled = false;

        // Our OWN command context (own queue + fence) for owned->own copies.
        // NEVER used against an engine resource or the engine command list.
        d3d12::CommandContext cmds{};
        bool cmds_ready = false;

        std::atomic<uint64_t> wm_seq{0};
        std::atomic<uint64_t> consumer_seq{0};
    };

    static State& state() {
        static State s{};
        return s;
    }

    static ID3D12Device* resolve_device(ID3D12Device* dev) {
        if (dev != nullptr) {
            return dev;
        }
        if (g_framework != nullptr && g_framework->get_d3d12_hook() != nullptr) {
            return g_framework->get_d3d12_hook()->get_device();
        }
        return nullptr;
    }

    static bool ensure_cmds(State& s) {
        if (s.cmds_ready) {
            return true;
        }
        if (!s.cmds.setup(L"SN2 Debug Resources (owned copy queue)")) {
            SPDLOG_WARN("[SN2-DebugRes] failed to set up owned command context");
            return false;
        }
        s.cmds_ready = true;
        return true;
    }

    // ---- #4 watermark resource creation -----------------------------------
    static void ensure_watermark(ID3D12Device* device, State& s) {
        if (s.attempted_watermark) {
            return;
        }
        s.attempted_watermark = true;

        // Owned RWTexture2D<uint> in our OWN default heap (UAV-capable).
        D3D12_HEAP_PROPERTIES dp{}; dp.Type = D3D12_HEAP_TYPE_DEFAULT;
        D3D12_RESOURCE_DESC td{};
        td.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        td.Width = kWmWidth; td.Height = kWmHeight;
        td.DepthOrArraySize = 1; td.MipLevels = 1;
        td.Format = DXGI_FORMAT_R32_UINT;
        td.SampleDesc.Count = 1;
        td.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
        td.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
        if (FAILED(device->CreateCommittedResource(&dp, D3D12_HEAP_FLAG_NONE, &td,
                D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr, IID_PPV_ARGS(&s.wm_tex)))) {
            SPDLOG_WARN("[SN2-DebugRes] watermark tex create FAILED {}x{}", kWmWidth, kWmHeight);
            s.wm_tex.Reset();
            return;
        }
        s.wm_tex->SetName(L"SN2_Debug_FogFillWatermark_R32UINT");

        // Own readback buffer sized from the texture footprint.
        device->GetCopyableFootprints(&td, 0, 1, 0, &s.wm_footprint, &s.wm_rows,
            &s.wm_row_size, &s.wm_readback_total);

        D3D12_HEAP_PROPERTIES rp{}; rp.Type = D3D12_HEAP_TYPE_READBACK;
        D3D12_RESOURCE_DESC bd{};
        bd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        bd.Width = s.wm_readback_total; bd.Height = 1;
        bd.DepthOrArraySize = 1; bd.MipLevels = 1;
        bd.Format = DXGI_FORMAT_UNKNOWN;
        bd.SampleDesc.Count = 1;
        bd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        if (FAILED(device->CreateCommittedResource(&rp, D3D12_HEAP_FLAG_NONE, &bd,
                D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&s.wm_readback)))) {
            SPDLOG_WARN("[SN2-DebugRes] watermark readback create FAILED size={}", s.wm_readback_total);
            s.wm_readback.Reset();
            return;
        }
        s.wm_readback->SetName(L"SN2_Debug_FogFillWatermark_Readback");

        SPDLOG_WARN("[SN2-DebugRes] #4 watermark resources created {}x{} R32_UINT readback={}B "
                    "(bind owned tex into space99 UAV via staged manifest wm_producer_store_0930dd4e.json)",
            kWmWidth, kWmHeight, s.wm_readback_total);
    }

    // ---- #11 sentinel resource creation + one-time gradient fill ----------
    static void ensure_sentinel(ID3D12Device* device, State& s) {
        if (s.attempted_sentinel) {
            return;
        }
        s.attempted_sentinel = true;

        // Owned Texture3D in our OWN default heap. We fill it via an UPLOAD
        // staging buffer + CopyTextureRegion on our OWN queue while idle —
        // both src and dst are UEVR-owned, so this never touches engine state.
        D3D12_HEAP_PROPERTIES dp{}; dp.Type = D3D12_HEAP_TYPE_DEFAULT;
        D3D12_RESOURCE_DESC td{};
        td.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE3D;
        td.Width = kSentinelW; td.Height = kSentinelH; td.DepthOrArraySize = kSentinelD;
        td.MipLevels = 1;
        td.Format = DXGI_FORMAT_R11G11B10_FLOAT;
        td.SampleDesc.Count = 1;
        td.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
        td.Flags = D3D12_RESOURCE_FLAG_NONE;
        if (FAILED(device->CreateCommittedResource(&dp, D3D12_HEAP_FLAG_NONE, &td,
                D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&s.sentinel_tex)))) {
            SPDLOG_WARN("[SN2-DebugRes] sentinel tex create FAILED {}x{}x{}",
                kSentinelW, kSentinelH, kSentinelD);
            s.sentinel_tex.Reset();
            return;
        }
        s.sentinel_tex->SetName(L"SN2_Debug_SentinelFroxel_R11G11B10F");

        // Build an X-gradient in R11G11B10F packed format for every slice.
        // Pack: R(11) | G(11)<<11 | B(10)<<22. Floats are positive [0,1] so we
        // use the simple unsigned-normalized-ish encode that RenderDoc decodes
        // back to a visible gradient (exact float bits not load-bearing — we
        // just need a smooth ramp the consumer A/B oracle can read).
        const auto pack_r11g11b10 = [](float r, float g, float b) -> uint32_t {
            auto to_u = [](float v, int bits, int mant) -> uint32_t {
                // crude float->packed: clamp [0,1], scale to mantissa range,
                // place at exponent 15 (1.x). Good enough for a visible ramp.
                v = v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
                const uint32_t maxm = (1u << mant) - 1u;
                const uint32_t m = (uint32_t)(v * (float)maxm + 0.5f);
                (void)bits;
                return m; // store in mantissa; exponent left 0 => denorm ramp
            };
            const uint32_t rr = to_u(r, 11, 6) & 0x7ffu;
            const uint32_t gg = to_u(g, 11, 6) & 0x7ffu;
            const uint32_t bb = to_u(b, 10, 5) & 0x3ffu;
            return rr | (gg << 11) | (bb << 22);
        };

        D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp{};
        UINT rows = 0; UINT64 row_size = 0; UINT64 total = 0;
        device->GetCopyableFootprints(&td, 0, 1, 0, &fp, &rows, &row_size, &total);

        D3D12_HEAP_PROPERTIES up{}; up.Type = D3D12_HEAP_TYPE_UPLOAD;
        D3D12_RESOURCE_DESC ub{};
        ub.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        ub.Width = total; ub.Height = 1; ub.DepthOrArraySize = 1; ub.MipLevels = 1;
        ub.Format = DXGI_FORMAT_UNKNOWN; ub.SampleDesc.Count = 1;
        ub.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        DComPtr<ID3D12Resource> upload{};
        if (FAILED(device->CreateCommittedResource(&up, D3D12_HEAP_FLAG_NONE, &ub,
                D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&upload)))) {
            SPDLOG_WARN("[SN2-DebugRes] sentinel upload buffer create FAILED size={}", total);
            return;
        }
        upload->SetName(L"SN2_Debug_SentinelFroxel_Upload");

        uint8_t* mapped = nullptr;
        D3D12_RANGE rr{0, 0};
        if (FAILED(upload->Map(0, &rr, reinterpret_cast<void**>(&mapped))) || mapped == nullptr) {
            SPDLOG_WARN("[SN2-DebugRes] sentinel upload map FAILED");
            return;
        }
        const UINT row_pitch = fp.Footprint.RowPitch;
        const UINT slice_pitch = row_pitch * kSentinelH;
        for (UINT z = 0; z < kSentinelD; ++z) {
            for (UINT y = 0; y < kSentinelH; ++y) {
                auto* dst = reinterpret_cast<uint32_t*>(mapped + fp.Offset
                    + (size_t)z * slice_pitch + (size_t)y * row_pitch);
                for (UINT x = 0; x < kSentinelW; ++x) {
                    const float gx = (float)x / (float)(kSentinelW - 1); // 0..1 across X
                    // Green dominant ramp = clearly directional + teal-ish to
                    // resemble the fog so it reads naturally in the consumer.
                    dst[x] = pack_r11g11b10(gx * 0.25f, gx, gx * 0.6f);
                }
            }
        }
        D3D12_RANGE wr{0, total};
        upload->Unmap(0, &wr);

        if (!ensure_cmds(s)) {
            return;
        }
        s.cmds.wait(INFINITE);
        auto* cl = s.cmds.cmd_list.Get();
        if (cl == nullptr) {
            return;
        }

        // owned upload -> owned sentinel, both UEVR-owned, our queue.
        D3D12_TEXTURE_COPY_LOCATION src{};
        src.pResource = upload.Get();
        src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        src.PlacedFootprint = fp;
        D3D12_TEXTURE_COPY_LOCATION dst{};
        dst.pResource = s.sentinel_tex.Get();
        dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        dst.SubresourceIndex = 0;
        cl->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);

        // Transition owned sentinel to a shader-readable state for the consumer
        // SRV redirect. This barrier is on OUR owned resource, OUR command list.
        D3D12_RESOURCE_BARRIER b{};
        b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        b.Transition.pResource = s.sentinel_tex.Get();
        b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        b.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
        b.Transition.StateAfter = ENGINE_SRC_COLOR;
        cl->ResourceBarrier(1, &b);

        s.cmds.has_commands = true;
        s.cmds.execute();
        s.cmds.wait(INFINITE);
        s.sentinel_filled = true;

        SPDLOG_WARN("[SN2-DebugRes] #11 sentinel froxel created+filled {}x{}x{} R11G11B10F "
                    "(redirect t5 SRV to it via staged sentinel_t5_<crc>.json)",
            kSentinelW, kSentinelH, kSentinelD);
    }

    void ensure_resources(ID3D12Device* dev) {
        if (!fog_fill_watermark_enabled() && !sentinel_froxel_enabled()) {
            return;
        }
        auto* device = resolve_device(dev);
        if (device == nullptr) {
            return;
        }
        auto& s = state();
        std::scoped_lock lock{s.mutex};
        if (fog_fill_watermark_enabled()) {
            ensure_watermark(device, s);
        }
        if (sentinel_froxel_enabled()) {
            ensure_sentinel(device, s);
        }
    }

    // ---- #4 readback after the producer dispatch --------------------------
    // We DO NOT touch the engine command list `cl`. The staged dxil_text_patch
    // redirected the producer's existing store into the space99 UAV bound to our
    // owned wm_tex, so by the time the engine submits, wm_tex holds the fill
    // coords. We copy owned wm_tex -> owned readback on OUR queue, after a fence,
    // and (occasionally) dump the CPU map. This never serializes the engine.
    static void copy_watermark_readback(State& s, ID3D12Device* device) {
        if (s.wm_tex == nullptr || s.wm_readback == nullptr) {
            return;
        }
        if (!ensure_cmds(s)) {
            return;
        }
        s.cmds.wait(INFINITE);
        auto* cl = s.cmds.cmd_list.Get();
        if (cl == nullptr) {
            return;
        }

        // owned wm_tex UAV -> COPY_SOURCE (our resource, our list).
        D3D12_RESOURCE_BARRIER toSrc{};
        toSrc.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        toSrc.Transition.pResource = s.wm_tex.Get();
        toSrc.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        toSrc.Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        toSrc.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
        cl->ResourceBarrier(1, &toSrc);

        D3D12_TEXTURE_COPY_LOCATION dst{};
        dst.pResource = s.wm_readback.Get();
        dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        dst.PlacedFootprint = s.wm_footprint;
        D3D12_TEXTURE_COPY_LOCATION src{};
        src.pResource = s.wm_tex.Get();
        src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        src.SubresourceIndex = 0;
        cl->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);

        // restore owned wm_tex to UAV for the next frame's producer store.
        D3D12_RESOURCE_BARRIER back = toSrc;
        back.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE;
        back.Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        cl->ResourceBarrier(1, &back);

        s.cmds.has_commands = true;
        s.cmds.execute();
        s.cmds.wait(INFINITE);

        // Decode + log the literal fill map (low 16 bits = DTid.x, top 8 = eye).
        uint8_t* mapped = nullptr;
        D3D12_RANGE rr{0, (SIZE_T)s.wm_readback_total};
        if (FAILED(s.wm_readback->Map(0, &rr, reinterpret_cast<void**>(&mapped))) || mapped == nullptr) {
            return;
        }
        // Sample one representative row (middle) to summarize L/R coverage.
        // High byte of each cell = eyeID stamped by the shader, using the UEVR
        // eye-bucket convention 1=LEFT, 2=RIGHT, 0=unknown/unset. A cell value of
        // 0 means that froxel column was never filled (empty), so left/right and
        // empty are all distinguishable.
        const UINT y = kWmHeight / 2;
        const auto* row = reinterpret_cast<const uint32_t*>(mapped + s.wm_footprint.Offset
            + (size_t)y * s.wm_footprint.Footprint.RowPitch);
        UINT left_filled = 0, right_filled = 0;
        UINT stamped_left = 0, stamped_right = 0, stamped_unknown = 0;
        const UINT half = kWmWidth / 2;
        for (UINT x = 0; x < kWmWidth; ++x) {
            const uint32_t v = row[x];
            if (v != 0) {
                const uint32_t eye_id = (v >> 24) & 0xFFu; // 1=L,2=R,0=unknown
                if (eye_id == 1u) ++stamped_left;
                else if (eye_id == 2u) ++stamped_right;
                else ++stamped_unknown;
                if (x < half) ++left_filled; else ++right_filled;
            }
        }
        D3D12_RANGE wr{0, 0};
        s.wm_readback->Unmap(0, &wr);

        SPDLOG_WARN("[SN2-DebugRes] #4 watermark map row{} cells_left[0..{}]filled={} cells_right[{}..{}]filled={} "
                    "stamped(eye1=L={} eye2=R={} unknown={}) "
                    "(expect one half filled, other empty => producer fill split)",
            y, half, left_filled, half, kWmWidth, right_filled,
            stamped_left, stamped_right, stamped_unknown);
        (void)device;
    }

    void on_producer_dispatch(ID3D12GraphicsCommandList* cl, uint32_t cs_crc, int eye) {
        if (!fog_fill_watermark_enabled() || !is_producer_crc(cs_crc)) {
            return;
        }
        auto* device = resolve_device(nullptr);
        if (device == nullptr) {
            return;
        }
        auto& s = state();
        std::scoped_lock lock{s.mutex};
        ensure_watermark(device, s);
        if (s.wm_tex == nullptr) {
            return;
        }
        // Throttle the owned->own readback (do not do it every dispatch; it has
        // its own fence wait). Once per ~120 producer dispatches is plenty for a
        // capture-time diagnostic.
        const auto seq = s.wm_seq.fetch_add(1, std::memory_order_relaxed);
        if ((seq % 120) == 0) {
            copy_watermark_readback(s, device);
        }
        if (verbose_log() && seq < 8) {
            SPDLOG_WARN("[SN2-DebugRes] #4 producer dispatch crc=0x{:08x} eye={} seq={} "
                        "(staged manifest must redirect store into space99 UAV=owned wm_tex)",
                cs_crc, eye, seq);
        }
        (void)cl;
    }

    void on_consumer_draw(ID3D12GraphicsCommandList* cl, uint32_t ps_crc, int eye) {
        if (!sentinel_froxel_enabled()) {
            return;
        }
        // Lightweight marker only — the actual t5 SRV redirect to the owned
        // sentinel lives in the staged consumer manifests. We never touch `cl`
        // or any engine resource here.
        const auto seq = state().consumer_seq.fetch_add(1, std::memory_order_relaxed);
        if (verbose_log() && seq < 16) {
            SPDLOG_WARN("[SN2-DebugRes] #11 consumer draw crc=0x{:08x} eye={} seq={} "
                        "(t5 SRV should be redirected to owned sentinel by staged manifest)",
                ps_crc, eye, seq);
        }
        (void)cl;
    }

    // =========================================================================
    // Feature #15 — Cave-region last-writer recorder
    // =========================================================================
    // Pure CPU observer. No GPU resources, no engine mutations.
    // Gated by UEVR_SN2_RDOC_TAGS (same master flag as features #10–#14).
    //
    // Cave-region rect is specified in per-eye-half pixel coordinates
    // (each eye half is 1280 wide in SN2's 2560-wide SBS target).
    // Env var UEVR_SN2_CAVE_REGION="x0,y0,x1,y1" overrides the default.
    // The default [320,100,960,460] brackets the upper-center region of a
    // 1280×720 half-frame, covering the cave opening in the main-menu vista
    // as confirmed from the sn2_rd_uevr_20260525_141356_frame652.rdc capture.
    // =========================================================================

    bool cave_writer_enabled() {
        static const bool v = env_truthy_local("UEVR_SN2_RDOC_TAGS");
        return v;
    }

    // Cave rect in per-eye-half coordinates (pixels within one 1280-wide half).
    struct CaveRect { long x0, y0, x1, y1; };

    static CaveRect cave_rect_per_eye() {
        static const CaveRect v = []() -> CaveRect {
            char buf[64]{};
            const DWORD n = GetEnvironmentVariableA("UEVR_SN2_CAVE_REGION", buf, sizeof(buf));
            if (n == 0 || n >= sizeof(buf)) {
                return {320, 100, 960, 460}; // default: upper-centre of a 1280x720 half
            }
            // Parse "x0,y0,x1,y1" — accept spaces around commas.
            long vals[4] = {320, 100, 960, 460};
            char* p = buf;
            for (int i = 0; i < 4 && p != nullptr && *p != '\0'; ++i) {
                while (*p == ' ') ++p;
                char* end = nullptr;
                vals[i] = std::strtol(p, &end, 10);
                if (end == p) break; // parse error — keep defaults
                p = end;
                while (*p == ',' || *p == ' ') ++p;
            }
            return {vals[0], vals[1], vals[2], vals[3]};
        }();
        return v;
    }

    // One record per DISTINCT (by ps_crc) draw covering a cave region. We now record
    // ALL covering draws for the LEFT region [0,1280] and the RIGHT region [1280,2560]
    // independently, with membership decided purely by WHERE the scissor draws (not by
    // the eye_bucket label) — so a full-screen [0,2560] pass lands in both lists while a
    // per-eye scene draw lands in only its own. The set difference (L-only vs R-only
    // CRCs) is the teal-vs-sky divergence we're hunting.
    struct CaveDrawRec {
        uint32_t ps_crc     = 0;
        int      eye_bucket = 0;
        uint64_t rtv_handle = 0;
        long     sx0 = 0, sy0 = 0, sx1 = 0, sy1 = 0; // full-SBS scissor coords
        uint64_t draw_seq   = 0;
    };

    static constexpr size_t kCaveMaxRecs = 48;

    struct CaveWriterState {
        std::mutex mutex{};
        std::vector<CaveDrawRec> left{};   // distinct draws covering the LEFT cave rect
        std::vector<CaveDrawRec> right{};  // distinct draws covering the RIGHT cave rect
        std::atomic<uint64_t> frame_seq{0};
    };

    static CaveWriterState& cave_state() {
        static CaveWriterState s{};
        return s;
    }

    // Returns true if two LTRB rectangles overlap (strictly).
    static inline bool rects_intersect(long ax0, long ay0, long ax1, long ay1,
                                       long bx0, long by0, long bx1, long by1) {
        return ax0 < bx1 && ax1 > bx0 && ay0 < by1 && ay1 > by0;
    }

    // Append a distinct (by ps_crc) covering draw into a region list (capped).
    static void cave_push(std::vector<CaveDrawRec>& list, int eye_bucket, uint64_t rtv,
                          const Sn2Rect& sc, uint32_t ps_crc, uint64_t seq) {
        for (const auto& r : list) { if (r.ps_crc == ps_crc) return; } // distinct CRCs only
        if (list.size() >= kCaveMaxRecs) return;
        CaveDrawRec rec;
        rec.ps_crc = ps_crc; rec.eye_bucket = eye_bucket; rec.rtv_handle = rtv;
        rec.sx0 = sc.left; rec.sy0 = sc.top; rec.sx1 = sc.right; rec.sy1 = sc.bottom;
        rec.draw_seq = seq;
        list.push_back(rec);
    }

    void on_draw_observed(
        ID3D12GraphicsCommandList* cl,
        int       eye_bucket,
        bool      has_scissor,
        Sn2Rect   scissor,
        uint64_t  rtv_handle,
        uint32_t  ps_crc,
        uint64_t  draw_seq)
    {
        if (!cave_writer_enabled() || !has_scissor) {
            return;
        }
        // LEFT cave rect lives in [0,1280); RIGHT is the same rect shifted +1280.
        // Membership is by WHERE the scissor draws (NOT eye_bucket), so a full-screen
        // [0,2560] pass lands in both lists and per-eye scene draws land in only one.
        const CaveRect per_eye = cave_rect_per_eye();
        const bool covers_left = rects_intersect(
            scissor.left, scissor.top, scissor.right, scissor.bottom,
            per_eye.x0,          per_eye.y0, per_eye.x1,          per_eye.y1);
        const bool covers_right = rects_intersect(
            scissor.left, scissor.top, scissor.right, scissor.bottom,
            per_eye.x0 + 1280L,  per_eye.y0, per_eye.x1 + 1280L,  per_eye.y1);
        if (!covers_left && !covers_right) {
            return;
        }
        {
            auto& cs = cave_state();
            std::scoped_lock lock{cs.mutex};
            if (covers_left)  cave_push(cs.left,  eye_bucket, rtv_handle, scissor, ps_crc, draw_seq);
            if (covers_right) cave_push(cs.right, eye_bucket, rtv_handle, scissor, ps_crc, draw_seq);
        }

        // Inline PIX SetMarker so the covering draw is greppable in the RenderDoc
        // capture too (metadata-only; same safety profile as sn2_rdoc_tags::mark()).
        if (cl != nullptr) {
            char text[96];
            std::snprintf(text, sizeof(text),
                "CAVE_REGION_WRITER region=%s%s eb=%d ps=0x%08x rtv=0x%llx seq=%llu",
                covers_left ? "L" : "", covers_right ? "R" : "",
                eye_bucket, ps_crc,
                static_cast<unsigned long long>(rtv_handle),
                static_cast<unsigned long long>(draw_seq));
            constexpr UINT PIX_EVENT_ANSI_VERSION = 1;
            UINT text_bytes = 1; // at minimum null terminator
            for (const char* p = text; *p; ++p) ++text_bytes;
            cl->SetMarker(PIX_EVENT_ANSI_VERSION, text, text_bytes);
        }
    }

    void flush_cave_writers() {
        if (!cave_writer_enabled()) {
            return;
        }
        auto& cs = cave_state();
        const uint64_t frame = cs.frame_seq.fetch_add(1, std::memory_order_relaxed);
        std::vector<CaveDrawRec> left_snap, right_snap;
        {
            std::scoped_lock lock{cs.mutex};
            left_snap  = cs.left;
            right_snap = cs.right;
            cs.left.clear();
            cs.right.clear();
        }

        // The menu is static, so throttle the full dump to keep the log lean:
        // every frame for the first few, then once every ~180 frames.
        const bool do_log = (frame < 3) || ((frame % 180) == 0);
        if (!do_log) {
            return;
        }

        const auto log_list = [&](const std::vector<CaveDrawRec>& recs, const char* tag) {
            if (recs.empty()) {
                SPDLOG_WARN("[SN2-CaveWriter] frame={} region={} NONE", frame, tag);
                return;
            }
            for (size_t i = 0; i < recs.size(); ++i) {
                const auto& r = recs[i];
                SPDLOG_WARN("[SN2-CaveWriter] frame={} region={} idx={} ps=0x{:08x} eb={} "
                            "rtv=0x{:016x} scissor=[{},{},{},{}] seq={}",
                            frame, tag, i, r.ps_crc, r.eye_bucket, r.rtv_handle,
                            r.sx0, r.sy0, r.sx1, r.sy1, r.draw_seq);
            }
        };
        log_list(left_snap,  "L");
        log_list(right_snap, "R");

        // Set difference: CRCs covering one region but not the other = the divergence
        // (e.g. the teal water/fog draw on the left vs the sky draw on the right).
        const auto in_list = [](const std::vector<CaveDrawRec>& v, uint32_t crc) {
            for (const auto& r : v) { if (r.ps_crc == crc) return true; }
            return false;
        };
        std::string lonly, ronly;
        char tmp[16];
        for (const auto& r : left_snap) {
            if (!in_list(right_snap, r.ps_crc)) { std::snprintf(tmp, sizeof(tmp), "0x%08x ", r.ps_crc); lonly += tmp; }
        }
        for (const auto& r : right_snap) {
            if (!in_list(left_snap, r.ps_crc)) { std::snprintf(tmp, sizeof(tmp), "0x%08x ", r.ps_crc); ronly += tmp; }
        }
        SPDLOG_WARN("[SN2-CaveWriter] frame={} DIVERGENCE L_only=[{}] R_only=[{}]",
                    frame, lonly, ronly);
    }

} // namespace sn2_debug

namespace vrmod {
namespace {
constexpr auto FRAME_TIMING_LOG_INTERVAL = std::chrono::seconds(5);
constexpr bool SHF_AUTO_MONO_CINEMATIC = true;
constexpr bool SHF_AUTO_2D_SCREEN_FROM_MONO_CINEMATIC = true;

bool sn2_env_truthy(const char* name) {
    char value[32]{};
    const auto len = GetEnvironmentVariableA(name, value, static_cast<DWORD>(sizeof(value)));
    if (len == 0 || len >= sizeof(value)) {
        return false;
    }

    std::string_view raw{value, std::min<DWORD>(len, static_cast<DWORD>(sizeof(value) - 1))};
    return raw != "0" && raw != "false" && raw != "FALSE" && raw != "off" && raw != "OFF";
}

bool frame_profiler_log_enabled() {
    static const bool enabled = sn2_env_truthy("UEVR_D3D12_FRAME_PROFILER_LOG");
    return enabled;
}

// Feature #10 (agent OWNEDRES): give the OpenXR native-stereo-array texture and
// the native (SBS) source resource descriptive RenderDoc-legible names so a
// capture can tell slice0=left / slice1=right apart and identify the source.
// SetName is SAFE (no device removal). Idempotent: each distinct resource is
// named once. We only name UEVR/OpenXR-side resources here; DXGI swapchain
// back-buffers belong to another agent and are intentionally skipped.
inline void sn2_name_native_stereo_resources(ID3D12Resource* array_tex, ID3D12Resource* native_source) {
    static std::unordered_set<ID3D12Resource*> s_named{};
    static std::mutex s_mutex{};
    std::scoped_lock lock{s_mutex};

    if (array_tex != nullptr && s_named.insert(array_tex).second) {
        // The array texture carries both eye slices: slice0=left, slice1=right.
        // D3D12 SetName is per-resource (not per-subresource), so encode both.
        array_tex->SetName(L"SN2_OpenXR_Array_Slice0_LeftEye__Slice1_RightEye");
    }

    if (native_source != nullptr && s_named.insert(native_source).second) {
        const auto d = native_source->GetDesc();
        wchar_t name[128]{};
        swprintf_s(name, L"SN2_NativeStereoSource_%llux%u_fmt%u",
            (unsigned long long)d.Width, (unsigned)d.Height, (unsigned)d.Format);
        native_source->SetName(name);
    }
}

enum SwapchainRecreateReason : uint32_t {
    SWAPCHAIN_RECREATE_NONE = 0,
    SWAPCHAIN_RECREATE_HMD_RESOLUTION = 1 << 0,
    SWAPCHAIN_RECREATE_EMPTY = 1 << 1,
    SWAPCHAIN_RECREATE_UI_EXTENT = 1 << 2,
    SWAPCHAIN_RECREATE_AFR_STATE = 1 << 3,
    SWAPCHAIN_RECREATE_DEPTH_EXTENT = 1 << 4,
    SWAPCHAIN_RECREATE_DEPTH_NULL_DEFAULTS = 1 << 5,
    SWAPCHAIN_RECREATE_SCENE_EYE_EXTENT = 1 << 6,
};

std::string format_swapchain_recreate_reasons(uint32_t reasons) {
    if (reasons == SWAPCHAIN_RECREATE_NONE) {
        return "none";
    }

    std::string out{};
    const auto append = [&](uint32_t flag, const char* name) {
        if ((reasons & flag) == 0) {
            return;
        }

        if (!out.empty()) {
            out += "|";
        }

        out += name;
    };

    append(SWAPCHAIN_RECREATE_HMD_RESOLUTION, "hmd_resolution");
    append(SWAPCHAIN_RECREATE_EMPTY, "empty_swapchains");
    append(SWAPCHAIN_RECREATE_UI_EXTENT, "ui_extent");
    append(SWAPCHAIN_RECREATE_AFR_STATE, "afr_state");
    append(SWAPCHAIN_RECREATE_DEPTH_EXTENT, "depth_extent");
    append(SWAPCHAIN_RECREATE_DEPTH_NULL_DEFAULTS, "depth_null_defaults");
    return out;
}

void prepare_openxr_swapchain_recreate(VR* vr, uint32_t reasons) {
    const auto cadence_sensitive_recreate =
        (reasons & (SWAPCHAIN_RECREATE_AFR_STATE | SWAPCHAIN_RECREATE_DEPTH_EXTENT | SWAPCHAIN_RECREATE_DEPTH_NULL_DEFAULTS)) != 0;

    if (!cadence_sensitive_recreate) {
        return;
    }

    if (vr == nullptr || vr->get_runtime() == nullptr || !vr->get_runtime()->is_openxr()) {
        return;
    }

    const auto openxr = vr->get_openxr_runtime();

    if (openxr == nullptr) {
        return;
    }

    const auto reason_text = "d3d12_swapchain_recreate:" + format_swapchain_recreate_reasons(reasons);
    openxr->prepare_resolution_scale_reconfigure(reason_text.c_str());
}

std::pair<uint32_t, uint32_t> get_ui_extent() {
    const auto fallback = std::pair<uint32_t, uint32_t>{
        (uint32_t)g_framework->get_d3d12_rt_size().x,
        (uint32_t)g_framework->get_d3d12_rt_size().y
    };

    const auto vr = VR::get();

    if (vr == nullptr) {
        return fallback;
    }

    const auto& fake_stereo_hook = vr->get_fake_stereo_hook();

    if (fake_stereo_hook == nullptr) {
        return fallback;
    }

    const auto rtm = fake_stereo_hook->get_render_target_manager();

    if (rtm == nullptr) {
        return fallback;
    }

    if (const auto requested_width = rtm->get_dedicated_ui_width();
        requested_width > 0 && rtm->get_dedicated_ui_height() > 0)
    {
        return {requested_width, rtm->get_dedicated_ui_height()};
    }

    const auto ui_target = rtm->get_ui_target();

    if (ui_target == nullptr || !g_framework->is_dx12()) {
        return fallback;
    }

    const auto native = (ID3D12Resource*)ui_target->get_native_resource();

    if (native == nullptr) {
        return fallback;
    }

    const auto desc = native->GetDesc();

    if (desc.Width == 0 || desc.Height == 0) {
        return fallback;
    }

    return {(uint32_t)desc.Width, (uint32_t)desc.Height};
}

bool is_shf_current_game() {
    static const bool result = []() {
        const auto exe_path = utility::get_module_pathw(utility::get_executable());
        return exe_path && exe_path->find(L"SHf-Win64-Shipping") != std::wstring::npos;
    }();

    return result;
}

bool is_stalker2_current_game() {
    static const bool result = []() {
        const auto exe_path = utility::get_module_pathw(utility::get_executable());
        return exe_path && exe_path->find(L"Stalker2-Win64-Shipping") != std::wstring::npos;
    }();

    return result;
}

bool is_avowed_current_game() {
    static const bool result = []() {
        const auto exe_path = utility::get_module_pathw(utility::get_executable());
        return exe_path && uevr::games::is_avowed_executable_path(*exe_path);
    }();

    return result;
}

bool is_ue_5_1_dx12_backend() {
    if (g_framework == nullptr || !g_framework->is_dx12()) {
        return false;
    }

    static const bool result = []() {
        const auto found_version = sdk::search_for_version(utility::get_executable());

        if (found_version) {
            const auto version = utility::narrow(*found_version);
            return version == "5.1" || version.starts_with("5.1.");
        }

        const auto disk_version = sdk::get_file_version_info();
        return disk_version.dwFileVersionMS == 0x00050001;
    }();

    return result;
}

bool texture_context_has_views(const d3d12::TextureContext& context) {
    return context.texture.Get() != nullptr &&
        context.rtv_heap != nullptr &&
        context.rtv_heap->Heap() != nullptr &&
        context.srv_heap != nullptr &&
        context.srv_heap->Heap() != nullptr;
}

void log_shf_texture_reference_rebuild(
    ID3D12Resource* backbuffer,
    ID3D12Resource* real_backbuffer,
    ID3D12Resource* current_game_texture,
    uint64_t frame_count)
{
    if (!is_shf_current_game() || backbuffer == nullptr) {
        return;
    }

    const auto backbuffer_desc = backbuffer->GetDesc();
    const auto real_desc = real_backbuffer != nullptr ? std::optional<D3D12_RESOURCE_DESC>{real_backbuffer->GetDesc()} : std::nullopt;
    static std::mutex log_mutex{};
    static std::unordered_set<uintptr_t> logged_backbuffers{};
    static uint64_t rebuild_count{};
    static uint64_t duplicate_suppressed{};

    bool log_unique = false;
    uint64_t seen = 0;
    uint64_t unique = 0;
    uint64_t suppressed = 0;

    {
        std::scoped_lock _{log_mutex};
        ++rebuild_count;
        seen = rebuild_count;

        const auto key = (uintptr_t)backbuffer;

        if (!logged_backbuffers.contains(key)) {
            logged_backbuffers.insert(key);
            log_unique = logged_backbuffers.size() <= 64;
        } else {
            ++duplicate_suppressed;
        }

        unique = logged_backbuffers.size();
        suppressed = duplicate_suppressed;
    }

    if (log_unique && real_desc) {
        SPDLOG_WARN("[SHf][D3D12] Game Texture reference rebuild #{} frame={} unique_backbuffers={} backbuffer={:x} real_backbuffer={:x} current_game_texture={:x} bb=[{}x{} fmt={} flags=0x{:x}] real=[{}x{} fmt={} flags=0x{:x}]",
            seen, frame_count, unique, (uintptr_t)backbuffer, (uintptr_t)real_backbuffer, (uintptr_t)current_game_texture,
            backbuffer_desc.Width, backbuffer_desc.Height, (uint32_t)backbuffer_desc.Format, (uint32_t)backbuffer_desc.Flags,
            real_desc->Width, real_desc->Height, (uint32_t)real_desc->Format, (uint32_t)real_desc->Flags);
    } else if (log_unique) {
        SPDLOG_WARN("[SHf][D3D12] Game Texture reference rebuild #{} frame={} unique_backbuffers={} backbuffer={:x} real_backbuffer=<null> current_game_texture={:x} bb=[{}x{} fmt={} flags=0x{:x}]",
            seen, frame_count, unique, (uintptr_t)backbuffer, (uintptr_t)current_game_texture,
            backbuffer_desc.Width, backbuffer_desc.Height, (uint32_t)backbuffer_desc.Format, (uint32_t)backbuffer_desc.Flags);
    } else if (real_desc) {
        SPDLOG_INFO_EVERY_N_SEC(2,
            "[SHf][D3D12] Game Texture reference rebuild summary seen={} unique_backbuffers={} duplicate_suppressed={} frame={} backbuffer={:x} real_backbuffer={:x} current_game_texture={:x} bb=[{}x{} fmt={} flags=0x{:x}] real=[{}x{} fmt={} flags=0x{:x}]",
            seen, unique, suppressed, frame_count, (uintptr_t)backbuffer, (uintptr_t)real_backbuffer, (uintptr_t)current_game_texture,
            backbuffer_desc.Width, backbuffer_desc.Height, (uint32_t)backbuffer_desc.Format, (uint32_t)backbuffer_desc.Flags,
            real_desc->Width, real_desc->Height, (uint32_t)real_desc->Format, (uint32_t)real_desc->Flags);
    } else {
        SPDLOG_INFO_EVERY_N_SEC(2,
            "[SHf][D3D12] Game Texture reference rebuild summary seen={} unique_backbuffers={} duplicate_suppressed={} frame={} backbuffer={:x} real_backbuffer=<null> current_game_texture={:x} bb=[{}x{} fmt={} flags=0x{:x}]",
            seen, unique, suppressed, frame_count, (uintptr_t)backbuffer, (uintptr_t)current_game_texture,
            backbuffer_desc.Width, backbuffer_desc.Height, (uint32_t)backbuffer_desc.Format, (uint32_t)backbuffer_desc.Flags);
    }
}

bool shf_texture_desc_matches(const D3D12_RESOURCE_DESC& a, const D3D12_RESOURCE_DESC& b) {
    return a.Dimension == b.Dimension &&
           a.Alignment == b.Alignment &&
           a.Width == b.Width &&
           a.Height == b.Height &&
           a.DepthOrArraySize == b.DepthOrArraySize &&
           a.MipLevels == b.MipLevels &&
           a.Format == b.Format &&
           a.SampleDesc.Count == b.SampleDesc.Count &&
           a.SampleDesc.Quality == b.SampleDesc.Quality;
}

float native_stereo_half_to_float(uint16_t value) {
    const auto sign = (value & 0x8000) != 0 ? -1.0f : 1.0f;
    const auto exponent = (value >> 10) & 0x1f;
    const auto mantissa = value & 0x03ff;

    if (exponent == 0) {
        return sign * std::ldexp((float)mantissa, -24);
    }

    if (exponent == 31) {
        return mantissa == 0 ? sign * INFINITY : NAN;
    }

    return sign * std::ldexp((float)(mantissa + 1024), (int)exponent - 25);
}

uint8_t native_stereo_float_to_byte(float value) {
    if (!std::isfinite(value)) {
        value = 0.0f;
    }

    value = std::clamp(value, 0.0f, 1.0f);
    value = std::pow(value, 1.0f / 2.2f);
    return (uint8_t)std::clamp((int)std::lround(value * 255.0f), 0, 255);
}

uint32_t native_stereo_bytes_per_pixel(DXGI_FORMAT format) {
    switch (format) {
    case DXGI_FORMAT_R8G8B8A8_UNORM:
    case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
    case DXGI_FORMAT_B8G8R8X8_UNORM:
    case DXGI_FORMAT_B8G8R8X8_UNORM_SRGB:
    case DXGI_FORMAT_B8G8R8A8_TYPELESS:
    case DXGI_FORMAT_B8G8R8A8_UNORM:
    case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
    case DXGI_FORMAT_R10G10B10A2_UNORM:
        return 4;
    case DXGI_FORMAT_R16G16B16A16_FLOAT:
    case DXGI_FORMAT_R16G16B16A16_UNORM:
        return 8;
    case DXGI_FORMAT_R32G32B32A32_FLOAT:
        return 16;
    default:
        return 0;
    }
}

bool native_stereo_decode_pixel(DXGI_FORMAT format, const uint8_t* pixel, uint8_t& r, uint8_t& g, uint8_t& b) {
    switch (format) {
    case DXGI_FORMAT_R8G8B8A8_UNORM:
    case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
        r = pixel[0];
        g = pixel[1];
        b = pixel[2];
        return true;

    case DXGI_FORMAT_B8G8R8A8_UNORM:
    case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
    case DXGI_FORMAT_B8G8R8X8_UNORM:
    case DXGI_FORMAT_B8G8R8X8_UNORM_SRGB:
    case DXGI_FORMAT_B8G8R8A8_TYPELESS:
        b = pixel[0];
        g = pixel[1];
        r = pixel[2];
        return true;

    case DXGI_FORMAT_R10G10B10A2_UNORM: {
        const auto packed = *(const uint32_t*)pixel;
        r = native_stereo_float_to_byte((float)(packed & 0x3ff) / 1023.0f);
        g = native_stereo_float_to_byte((float)((packed >> 10) & 0x3ff) / 1023.0f);
        b = native_stereo_float_to_byte((float)((packed >> 20) & 0x3ff) / 1023.0f);
        return true;
    }

    case DXGI_FORMAT_R16G16B16A16_FLOAT: {
        const auto* halfs = (const uint16_t*)pixel;
        r = native_stereo_float_to_byte(native_stereo_half_to_float(halfs[0]));
        g = native_stereo_float_to_byte(native_stereo_half_to_float(halfs[1]));
        b = native_stereo_float_to_byte(native_stereo_half_to_float(halfs[2]));
        return true;
    }

    case DXGI_FORMAT_R16G16B16A16_UNORM: {
        const auto* values = (const uint16_t*)pixel;
        r = native_stereo_float_to_byte((float)values[0] / 65535.0f);
        g = native_stereo_float_to_byte((float)values[1] / 65535.0f);
        b = native_stereo_float_to_byte((float)values[2] / 65535.0f);
        return true;
    }

    case DXGI_FORMAT_R32G32B32A32_FLOAT: {
        const auto* values = (const float*)pixel;
        r = native_stereo_float_to_byte(values[0]);
        g = native_stereo_float_to_byte(values[1]);
        b = native_stereo_float_to_byte(values[2]);
        return true;
    }

    default:
        return false;
    }
}

}

bool D3D12Component::dump_texture_region_to_bmp(
    ID3D12Resource* texture,
    const D3D12_BOX& src_box,
    D3D12_RESOURCE_STATES source_state,
    const std::filesystem::path& path)
{
    if (texture == nullptr) {
        return false;
    }

    const auto desc = texture->GetDesc();
    const auto width = src_box.right - src_box.left;
    const auto height = src_box.bottom - src_box.top;
    const auto bytes_per_pixel = native_stereo_bytes_per_pixel(desc.Format);

    if (width == 0 || height == 0 || bytes_per_pixel == 0) {
        SPDLOG_WARN("[NativeStereoDebug] Cannot dump region {}x{} format={}", width, height, (int)desc.Format);
        return false;
    }

    auto& hook = g_framework->get_d3d12_hook();
    auto device = hook->get_device();

    auto region_desc = desc;
    region_desc.Width = width;
    region_desc.Height = height;
    region_desc.DepthOrArraySize = 1;
    region_desc.MipLevels = 1;
    region_desc.SampleDesc.Count = 1;
    region_desc.SampleDesc.Quality = 0;
    region_desc.Flags = D3D12_RESOURCE_FLAG_NONE;

    D3D12_PLACED_SUBRESOURCE_FOOTPRINT layout{};
    UINT num_rows{};
    UINT64 row_size_bytes{};
    UINT64 total_bytes{};
    device->GetCopyableFootprints(&region_desc, 0, 1, 0, &layout, &num_rows, &row_size_bytes, &total_bytes);

    D3D12_HEAP_PROPERTIES heap_props{};
    heap_props.Type = D3D12_HEAP_TYPE_READBACK;
    heap_props.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    heap_props.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;

    D3D12_RESOURCE_DESC buffer_desc{};
    buffer_desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    buffer_desc.Width = total_bytes;
    buffer_desc.Height = 1;
    buffer_desc.DepthOrArraySize = 1;
    buffer_desc.MipLevels = 1;
    buffer_desc.Format = DXGI_FORMAT_UNKNOWN;
    buffer_desc.SampleDesc.Count = 1;
    buffer_desc.SampleDesc.Quality = 0;
    buffer_desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    buffer_desc.Flags = D3D12_RESOURCE_FLAG_NONE;

    ComPtr<ID3D12Resource> readback{};
    if (FAILED(device->CreateCommittedResource(
            &heap_props,
            D3D12_HEAP_FLAG_NONE,
            &buffer_desc,
            D3D12_RESOURCE_STATE_COPY_DEST,
            nullptr,
            IID_PPV_ARGS(&readback)))) {
        SPDLOG_WARN("[NativeStereoDebug] Failed to create readback buffer for {}", path.string());
        return false;
    }

    if (!m_native_debug_dump_commands.ready()) {
        if (!m_native_debug_dump_commands.setup(L"Native stereo debug dump commands")) {
            return false;
        }
    }

    m_native_debug_dump_commands.wait(INFINITE);

    auto* cmd_list = m_native_debug_dump_commands.cmd_list.Get();
    if (cmd_list == nullptr) {
        return false;
    }

    D3D12_RESOURCE_BARRIER src_barrier{};
    src_barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    src_barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    src_barrier.Transition.pResource = texture;
    src_barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    src_barrier.Transition.StateBefore = source_state;
    src_barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
    cmd_list->ResourceBarrier(1, &src_barrier);

    D3D12_TEXTURE_COPY_LOCATION dst_loc{};
    dst_loc.pResource = readback.Get();
    dst_loc.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    dst_loc.PlacedFootprint = layout;

    D3D12_TEXTURE_COPY_LOCATION src_loc{};
    src_loc.pResource = texture;
    src_loc.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    src_loc.SubresourceIndex = 0;

    cmd_list->CopyTextureRegion(&dst_loc, 0, 0, 0, &src_loc, &src_box);

    src_barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE;
    src_barrier.Transition.StateAfter = source_state;
    cmd_list->ResourceBarrier(1, &src_barrier);

    m_native_debug_dump_commands.has_commands = true;
    m_native_debug_dump_commands.execute();
    m_native_debug_dump_commands.wait(INFINITE);

    uint8_t* mapped = nullptr;
    D3D12_RANGE read_range{0, total_bytes};
    if (FAILED(readback->Map(0, &read_range, (void**)&mapped)) || mapped == nullptr) {
        SPDLOG_WARN("[NativeStereoDebug] Failed to map readback buffer for {}", path.string());
        return false;
    }

    std::filesystem::create_directories(path.parent_path());

    const uint32_t bmp_stride = ((width * 3) + 3) & ~3u;
    const uint32_t image_size = bmp_stride * height;
    const uint32_t file_size = 14 + 40 + image_size;

    std::ofstream out{path, std::ios::binary};
    if (!out) {
        D3D12_RANGE write_range{0, 0};
        readback->Unmap(0, &write_range);
        SPDLOG_WARN("[NativeStereoDebug] Failed to open dump path {}", path.string());
        return false;
    }

    auto write_u16 = [&](uint16_t value) {
        out.put((char)(value & 0xff));
        out.put((char)((value >> 8) & 0xff));
    };

    auto write_u32 = [&](uint32_t value) {
        out.put((char)(value & 0xff));
        out.put((char)((value >> 8) & 0xff));
        out.put((char)((value >> 16) & 0xff));
        out.put((char)((value >> 24) & 0xff));
    };

    auto write_i32 = [&](int32_t value) {
        write_u32((uint32_t)value);
    };

    write_u16(0x4d42);
    write_u32(file_size);
    write_u16(0);
    write_u16(0);
    write_u32(54);

    write_u32(40);
    write_i32((int32_t)width);
    write_i32(-(int32_t)height);
    write_u16(1);
    write_u16(24);
    write_u32(0);
    write_u32(image_size);
    write_i32(2835);
    write_i32(2835);
    write_u32(0);
    write_u32(0);

    std::vector<uint8_t> row(bmp_stride);
    double luma_sum = 0.0;
    uint64_t nonblack_count = 0;
    const uint64_t pixel_count = (uint64_t)width * (uint64_t)height;

    for (uint32_t y = 0; y < height; ++y) {
        std::fill(row.begin(), row.end(), 0);
        const auto* src_row = mapped + layout.Offset + ((size_t)y * layout.Footprint.RowPitch);

        for (uint32_t x = 0; x < width; ++x) {
            uint8_t r{};
            uint8_t g{};
            uint8_t b{};
            native_stereo_decode_pixel(desc.Format, src_row + ((size_t)x * bytes_per_pixel), r, g, b);

            const auto dst = x * 3;
            row[dst + 0] = b;
            row[dst + 1] = g;
            row[dst + 2] = r;

            const auto luma = (0.2126 * (double)r) + (0.7152 * (double)g) + (0.0722 * (double)b);
            luma_sum += luma;

            if (luma > 15.0) {
                ++nonblack_count;
            }
        }

        out.write((const char*)row.data(), row.size());
    }

    D3D12_RANGE write_range{0, 0};
    readback->Unmap(0, &write_range);

    SPDLOG_INFO("[NativeStereoDebug] Dumped {} format={} region={} {} {} {} mean_luma={:.2f} nonblack={:.2f}%",
        path.string(), (int)desc.Format,
        src_box.left, src_box.top, src_box.right, src_box.bottom,
        pixel_count != 0 ? luma_sum / (double)pixel_count : 0.0,
        pixel_count != 0 ? (100.0 * (double)nonblack_count / (double)pixel_count) : 0.0);

    return true;
}

void D3D12Component::dump_native_stereo_backbuffer_once(
    ID3D12Resource* backbuffer,
    const D3D12_BOX& left_box,
    const D3D12_BOX& right_box,
    D3D12_RESOURCE_STATES source_state)
{
    if (m_native_debug_dumped_backbuffer || backbuffer == nullptr) {
        return;
    }

    auto vr = VR::get();
    if (vr == nullptr || !vr->is_native_stereo_fix_enabled() || vr->is_native_stereo_fix_same_pass_enabled()) {
        return;
    }

    if (!sn2_env_truthy("UEVR_SN2_NATIVE_STEREO_DUMP_BACKBUFFER")) {
        return;
    }

    if (++m_native_debug_submit_count < 30) {
        return;
    }

    m_native_debug_dumped_backbuffer = true;

    const auto dump_dir = Framework::get_persistent_dir() / "native_stereo_debug";
    dump_texture_region_to_bmp(backbuffer, left_box, source_state, dump_dir / "submit_source_left.bmp");
    dump_texture_region_to_bmp(backbuffer, right_box, source_state, dump_dir / "submit_source_right.bmp");
}

const char* D3D12Component::shf_scene_mode_name(ShfSceneMode mode) {
    switch (mode) {
    case ShfSceneMode::Stereo3D:
        return "Stereo3D";
    case ShfSceneMode::Mono2D:
        return "Mono2D";
    default:
        return "Unknown";
    }
}

bool D3D12Component::ensure_2d_screen_textures(ID3D12Device* device, const D3D12_RESOURCE_DESC& base_desc) {
    if (device == nullptr) {
        return false;
    }

    auto screen_desc = base_desc;
    screen_desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    screen_desc.Alignment = 0;
    screen_desc.Width = (uint32_t)g_framework->get_d3d12_rt_size().x;
    screen_desc.Height = (uint32_t)g_framework->get_d3d12_rt_size().y;
    screen_desc.DepthOrArraySize = 1;
    screen_desc.MipLevels = 1;
    screen_desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    screen_desc.SampleDesc.Count = 1;
    screen_desc.SampleDesc.Quality = 0;
    screen_desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    screen_desc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    screen_desc.Flags &= ~D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE;

    if (screen_desc.Width == 0 || screen_desc.Height == 0) {
        SPDLOG_ERROR_EVERY_N_SEC(1, "[VR] Refusing to create zero-sized 2D screen textures (D3D12).");
        return false;
    }

    D3D12_HEAP_PROPERTIES heap_props{};
    heap_props.Type = D3D12_HEAP_TYPE_DEFAULT;
    heap_props.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    heap_props.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;

    bool all_ready = true;

    for (auto& context : m_2d_screen_tex) {
        bool needs_create = context.texture.Get() == nullptr;

        if (!needs_create) {
            const auto existing_desc = context.texture->GetDesc();
            needs_create =
                existing_desc.Width != screen_desc.Width ||
                existing_desc.Height != screen_desc.Height ||
                existing_desc.Format != screen_desc.Format ||
                existing_desc.SampleDesc.Count != screen_desc.SampleDesc.Count ||
                existing_desc.SampleDesc.Quality != screen_desc.SampleDesc.Quality;
        }

        if (!needs_create) {
            continue;
        }

        context.reset();

        ComPtr<ID3D12Resource> screen_tex{};
        if (FAILED(device->CreateCommittedResource(
                &heap_props,
                D3D12_HEAP_FLAG_NONE,
                &screen_desc,
                ENGINE_SRC_COLOR,
                nullptr,
                IID_PPV_ARGS(&screen_tex)))) {
            spdlog::error("[VR] Failed to create 2D screen texture.");
            all_ready = false;
            continue;
        }

        screen_tex->SetName(L"2D Screen Texture");

        if (!context.setup(device, screen_tex.Get(), DXGI_FORMAT_B8G8R8A8_UNORM, DXGI_FORMAT_B8G8R8A8_UNORM, L"2D Screen")) {
            spdlog::error("[VR] Failed to setup 2D screen context.");
            context.reset();
            all_ready = false;
            continue;
        }

        SPDLOG_INFO("[VR] Created D3D12 2D screen texture [{}x{} fmt={}]", screen_desc.Width, screen_desc.Height, (uint32_t)screen_desc.Format);
    }

    return all_ready;
}

D3D12Component::ShfSceneMode D3D12Component::classify_shf_scene_mode(
    const D3D12_RESOURCE_DESC& source_desc,
    const D3D12_RESOURCE_DESC& real_desc) const
{
    const auto source_width = (uint64_t)source_desc.Width;
    const auto source_height = (uint32_t)source_desc.Height;
    const auto real_width = (uint64_t)real_desc.Width;
    const auto real_height = (uint32_t)real_desc.Height;

    if (real_width > 0 && real_height > 0 && source_width == real_width * 2 && source_height == real_height) {
        return ShfSceneMode::Mono2D;
    }

    if (m_backbuffer_size[0] != 0 && m_backbuffer_size[1] != 0 &&
        source_width == m_backbuffer_size[0] && source_height == m_backbuffer_size[1]) {
        return ShfSceneMode::Stereo3D;
    }

    if (source_width > real_width * 2 || source_height > real_height) {
        return ShfSceneMode::Stereo3D;
    }

    return ShfSceneMode::Unknown;
}

void D3D12Component::log_shf_scene_mode_if_needed(
    ShfSceneMode mode,
    const D3D12_RESOURCE_DESC& source_desc,
    const D3D12_RESOURCE_DESC& real_desc,
    uint64_t frame_count,
    bool using_mono_expansion)
{
    if (!is_shf_current_game()) {
        return;
    }

    if (m_shf_scene_mode != mode) {
        SPDLOG_WARN(
            "[SHf][D3D12] Scene mode changed {} -> {} frame={} src=[{}x{} fmt={} flags=0x{:x}] real=[{}x{} fmt={} flags=0x{:x}] normal_dw={}x{} mono_expanded={}",
            shf_scene_mode_name(m_shf_scene_mode),
            shf_scene_mode_name(mode),
            frame_count,
            source_desc.Width,
            source_desc.Height,
            (uint32_t)source_desc.Format,
            (uint32_t)source_desc.Flags,
            real_desc.Width,
            real_desc.Height,
            (uint32_t)real_desc.Format,
            (uint32_t)real_desc.Flags,
            m_backbuffer_size[0],
            m_backbuffer_size[1],
            using_mono_expansion);
        m_shf_scene_mode = mode;
        return;
    }

    SPDLOG_INFO_EVERY_N_SEC(
        5,
        "[SHf][D3D12] Scene mode summary mode={} frame={} src=[{}x{} fmt={} flags=0x{:x}] real=[{}x{} fmt={} flags=0x{:x}] normal_dw={}x{} mono_expanded={}",
        shf_scene_mode_name(mode),
        frame_count,
        source_desc.Width,
        source_desc.Height,
        (uint32_t)source_desc.Format,
        (uint32_t)source_desc.Flags,
        real_desc.Width,
        real_desc.Height,
        (uint32_t)real_desc.Format,
        (uint32_t)real_desc.Flags,
        m_backbuffer_size[0],
        m_backbuffer_size[1],
        using_mono_expansion);
}

bool D3D12Component::ensure_shf_mono_scene_texture(ID3D12Device* device, const D3D12_RESOURCE_DESC& source_desc) {
    if (device == nullptr || m_backbuffer_size[0] == 0 || m_backbuffer_size[1] == 0) {
        return false;
    }

    auto mono_desc = source_desc;
    mono_desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    mono_desc.Alignment = 0;
    mono_desc.Width = m_backbuffer_size[0];
    mono_desc.Height = m_backbuffer_size[1];
    mono_desc.DepthOrArraySize = 1;
    mono_desc.MipLevels = 1;
    mono_desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    mono_desc.SampleDesc.Count = 1;
    mono_desc.SampleDesc.Quality = 0;
    mono_desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    mono_desc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    mono_desc.Flags &= ~D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE;

    const auto needs_create =
        m_shf_mono_scene_tex.texture.Get() == nullptr ||
        m_shf_mono_scene_width != mono_desc.Width ||
        m_shf_mono_scene_height != mono_desc.Height ||
        m_shf_mono_scene_format != mono_desc.Format;

    if (!needs_create) {
        return m_shf_mono_scene_tex.srv_heap != nullptr && m_shf_mono_scene_tex.rtv_heap != nullptr;
    }

    D3D12_HEAP_PROPERTIES heap_props{};
    heap_props.Type = D3D12_HEAP_TYPE_DEFAULT;
    heap_props.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    heap_props.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;

    m_shf_mono_scene_tex.reset();

    ComPtr<ID3D12Resource> mono_tex{};
    if (FAILED(device->CreateCommittedResource(
            &heap_props,
            D3D12_HEAP_FLAG_NONE,
            &mono_desc,
            ENGINE_SRC_COLOR,
            nullptr,
            IID_PPV_ARGS(&mono_tex)))) {
        SPDLOG_ERROR_EVERY_N_SEC(
            1,
            "[SHf][D3D12] Failed to create mono cutscene expansion texture [{}x{} fmt={} flags=0x{:x}]",
            mono_desc.Width,
            mono_desc.Height,
            (uint32_t)mono_desc.Format,
            (uint32_t)mono_desc.Flags);
        return false;
    }

    mono_tex->SetName(L"SHf Mono Cutscene Expansion");

    if (!m_shf_mono_scene_tex.setup(device, mono_tex.Get(), DXGI_FORMAT_B8G8R8A8_UNORM, DXGI_FORMAT_B8G8R8A8_UNORM, L"SHf Mono Cutscene Expansion")) {
        spdlog::error("[SHf][D3D12] Failed to setup mono cutscene expansion texture.");
        m_shf_mono_scene_tex.reset();
        m_shf_mono_scene_width = 0;
        m_shf_mono_scene_height = 0;
        m_shf_mono_scene_format = DXGI_FORMAT_UNKNOWN;
        return false;
    }

    m_shf_mono_scene_width = mono_desc.Width;
    m_shf_mono_scene_height = mono_desc.Height;
    m_shf_mono_scene_format = mono_desc.Format;

    if (!m_shf_mono_scene_commands.ready()) {
        m_shf_mono_scene_commands.setup(L"SHf Mono Cutscene Expansion Commands");
    }

    SPDLOG_WARN(
        "[SHf][D3D12] Created mono cutscene expansion texture [{}x{}] from source [{}x{}]",
        mono_desc.Width,
        mono_desc.Height,
        source_desc.Width,
        source_desc.Height);

    return true;
}

d3d12::TextureContext* D3D12Component::render_shf_mono_scene_texture(ID3D12Device* device) {
    if (!SHF_AUTO_MONO_CINEMATIC ||
        m_game_batch == nullptr ||
        m_game_tex.texture.Get() == nullptr ||
        m_game_tex.srv_heap == nullptr ||
        m_game_tex.srv_heap->Heap() == nullptr) {
        return nullptr;
    }

    const auto source_desc = m_game_tex.texture->GetDesc();

    if (!ensure_shf_mono_scene_texture(device, source_desc) ||
        m_shf_mono_scene_tex.texture.Get() == nullptr ||
        m_shf_mono_scene_tex.rtv_heap == nullptr) {
        return nullptr;
    }

    auto& command_ctx = m_shf_mono_scene_commands;

    if (!command_ctx.ready()) {
        command_ctx.setup(L"SHf Mono Cutscene Expansion Commands");
    }

    if (!command_ctx.ready()) {
        return nullptr;
    }

    command_ctx.wait(INFINITE);

    const float clear_color[] = {0.0f, 0.0f, 0.0f, 0.0f};
    command_ctx.clear_rtv(m_shf_mono_scene_tex, clear_color, ENGINE_SRC_COLOR);

    const auto half_width = (LONG)(m_backbuffer_size[0] / 2);
    const auto full_width = (LONG)m_backbuffer_size[0];
    const auto full_height = (LONG)m_backbuffer_size[1];
    const auto source_half_width = (LONG)(source_desc.Width / 2);
    const auto source_height = (LONG)source_desc.Height;

    const RECT left_src{0, 0, source_half_width, source_height};
    const RECT right_src{source_half_width, 0, (LONG)source_desc.Width, source_height};

    auto fit_eye_rect = [&](LONG eye_left, LONG eye_right) {
        RECT dest{eye_left, 0, eye_right, full_height};
        const auto eye_width = (float)(eye_right - eye_left);
        const auto eye_height = (float)full_height;
        const auto source_aspect = source_half_width > 0 && source_height > 0 ? (float)source_half_width / (float)source_height : 1.0f;
        const auto eye_aspect = eye_height > 0.0f ? eye_width / eye_height : source_aspect;

        if (source_aspect > eye_aspect) {
            const auto fitted_height = (LONG)(eye_width / source_aspect);
            const auto y = (full_height - fitted_height) / 2;
            dest.top = y;
            dest.bottom = y + fitted_height;
        } else {
            const auto fitted_width = (LONG)(eye_height * source_aspect);
            const auto x = eye_left + ((LONG)eye_width - fitted_width) / 2;
            dest.left = x;
            dest.right = x + fitted_width;
        }

        return dest;
    };

    const auto left_dest = fit_eye_rect(0, half_width);
    const auto right_dest = fit_eye_rect(half_width, full_width);

    d3d12::render_srv_to_rtv(
        m_game_batch.get(),
        command_ctx.cmd_list.Get(),
        m_game_tex,
        m_shf_mono_scene_tex,
        left_src,
        left_dest,
        ENGINE_SRC_COLOR,
        ENGINE_SRC_COLOR);

    d3d12::render_srv_to_rtv(
        m_game_batch.get(),
        command_ctx.cmd_list.Get(),
        m_game_tex,
        m_shf_mono_scene_tex,
        right_src,
        right_dest,
        ENGINE_SRC_COLOR,
        ENGINE_SRC_COLOR);

    command_ctx.execute();

    SPDLOG_INFO_EVERY_N_SEC(
        2,
        "[SHf][D3D12] Expanded low-res cutscene source [{}x{}] into stereo-safe double-wide [{}x{}]",
        source_desc.Width,
        source_desc.Height,
        m_backbuffer_size[0],
        m_backbuffer_size[1]);

    return &m_shf_mono_scene_tex;
}

vr::EVRCompositorError D3D12Component::on_frame(VR* vr) {
    const auto on_frame_start = std::chrono::steady_clock::now();
    utility::ScopeGuard frame_timing_guard{[&]() {
        m_perf_on_frame.add(std::chrono::steady_clock::now() - on_frame_start);
        log_frame_timing_stats_if_needed(vr);
    }};

    m_last_on_frame = std::chrono::steady_clock::now();
    bool defer_stalker2_transition_openxr = false;

    auto close_openxr_setup_failure_frame = [&]() {
        if (vr->m_openxr == nullptr || !vr->get_runtime()->is_openxr()) {
            return;
        }

        if (vr->m_openxr->close_synced_frame_without_layers("d3d12_setup_failed")) {
            SPDLOG_WARNING_EVERY_N_SEC(
                1,
                "[D3D12 VR] Closed pending OpenXR frame after D3D12 setup failure so the runtime can keep advancing");
        }
    };

    if (m_force_reset || m_last_afr_state != vr->is_using_afr()) {
        if (!setup()) {
            SPDLOG_ERROR_EVERY_N_SEC(1, "[D3D12 VR] Could not set up, trying again next frame");
            close_openxr_setup_failure_frame();
            m_force_reset = true;
            return vr::VRCompositorError_None;
        }

        m_last_afr_state = vr->is_using_afr();
    }

    auto& hook = g_framework->get_d3d12_hook();

    hook->set_next_present_interval(0); // disable vsync for vr
    
    // get device
    auto device = hook->get_device();

    // get command queue
    auto command_queue = hook->get_command_queue();

    // get swapchain
    auto swapchain = hook->get_swap_chain();

    // get back buffer
    ComPtr<ID3D12Resource> backbuffer{};
    ComPtr<ID3D12Resource> real_backbuffer{};
    auto ue4_texture = VR::get()->m_fake_stereo_hook->get_render_target_manager()->get_render_target();

    if (ue4_texture != nullptr) {
        backbuffer = (ID3D12Resource*)ue4_texture->get_native_resource();
    }

    if (FAILED(swapchain->GetBuffer(swapchain->GetCurrentBackBufferIndex(), IID_PPV_ARGS(&real_backbuffer)))) {
        spdlog::error("[VR] Failed to get real back buffer.");
        return vr::VRCompositorError_None;
    }

    if (vr->is_extreme_compatibility_mode_enabled()) {
        backbuffer = real_backbuffer;
    }

    if (backbuffer == nullptr) {
        SPDLOG_ERROR_EVERY_N_SEC(1, "[VR] Failed to get back buffer.");
        return vr::VRCompositorError_None;
    }

    const auto is_shf_external_backbuffer =
        is_shf_current_game() &&
        g_framework->is_dx12() &&
        backbuffer.Get() != nullptr &&
        real_backbuffer.Get() != nullptr &&
        backbuffer.Get() != real_backbuffer.Get();
    const auto is_stalker2_ue51_external_backbuffer =
        is_stalker2_current_game() &&
        is_ue_5_1_dx12_backend() &&
        backbuffer.Get() != nullptr &&
        real_backbuffer.Get() != nullptr &&
        backbuffer.Get() != real_backbuffer.Get();
    const auto use_stable_external_backbuffer_copy =
        is_shf_external_backbuffer || is_stalker2_ue51_external_backbuffer;
    const auto volatile_external_source_state =
        is_shf_external_backbuffer ? ENGINE_SRC_COLOR : D3D12_RESOURCE_STATE_RENDER_TARGET;
    const char* stable_external_copy_label =
        is_stalker2_ue51_external_backbuffer ? "Stalker2 UE5.1" : "SHf";
    const wchar_t* stable_external_copy_name =
        is_stalker2_ue51_external_backbuffer ? L"Stalker2 UE5.1 Stable Scene Copy" : L"SHf Stable Scene Copy";
    const auto skip_in_place_ui_invert = false;
    m_skip_spectator_view_for_volatile_external_rt = is_shf_external_backbuffer;
    auto scene_source_state = use_stable_external_backbuffer_copy ? ENGINE_SRC_COLOR : D3D12_RESOURCE_STATE_RENDER_TARGET;

    if (is_stalker2_ue51_external_backbuffer) {
        static auto s_stalker2_last_d3d12_frame = std::chrono::steady_clock::time_point{};
        const auto now = std::chrono::steady_clock::now();

        if (s_stalker2_last_d3d12_frame.time_since_epoch().count() != 0 &&
            now - s_stalker2_last_d3d12_frame > std::chrono::milliseconds{100})
        {
            vr->note_stalker2_transition_stress("d3d12_frame_gap");
        }

        s_stalker2_last_d3d12_frame = now;
    }

    const auto ui_invert_alpha = vr->get_overlay_component().get_ui_invert_alpha();

    // Update the UI overlay.
    auto runtime = vr->get_runtime();
    const auto openxr_runtime = runtime->is_openxr() ? vr->m_openxr.get() : nullptr;
    const auto debug_submit_empty_frame = openxr_runtime != nullptr && openxr_runtime->debug_submit_empty_frame->value();
    const auto debug_skip_scene_copy = openxr_runtime != nullptr && openxr_runtime->debug_skip_scene_copy->value();
    const auto debug_skip_ui_copy = openxr_runtime != nullptr && openxr_runtime->debug_skip_ui_copy->value();
    const auto debug_disable_depth_submit = openxr_runtime != nullptr && openxr_runtime->debug_disable_depth_submit->value();
    const auto suppress_scene_copy = debug_submit_empty_frame || debug_skip_scene_copy;
    const auto suppress_ui_copy = debug_submit_empty_frame || debug_skip_ui_copy;

    const auto is_same_frame = m_last_rendered_frame > 0 && m_last_rendered_frame == vr->m_render_frame_count;
    m_last_rendered_frame = vr->m_render_frame_count;

    const auto is_actually_afr = vr->is_using_afr();
    const auto is_afr = !is_same_frame && vr->is_using_afr();
    const auto is_left_eye_frame = is_afr && vr->m_render_frame_count % 2 == vr->m_left_eye_interval;
    const auto is_right_eye_frame = !is_afr || vr->m_render_frame_count % 2 == vr->m_right_eye_interval;

    // Sometimes this can happen if pipeline execution does not go exactly as planned
    // so we need to resynchronized or begin the frame again.
    if (runtime->ready()) {
        if (runtime->is_openxr()) {
            // Keep xrWaitFrame ownership where it already is, but do not let the D3D12
            // path begin the frame here. We open it at the first OpenXR copy/acquire.
            defer_stalker2_transition_openxr =
                vr->should_defer_stalker2_openxr_frame_for_transition("d3d12_pre_wait");

            if (!defer_stalker2_transition_openxr) {
                runtime->synchronize_frame(std::nullopt, VRRuntime::SyncFrameCallsite::RuntimeFixFrame);
            }
        } else {
            runtime->fix_frame();
        }
    }

    const auto& ffsr = VR::get()->m_fake_stereo_hook;
    const auto ui_target = ffsr->get_render_target_manager()->get_ui_target();

    const auto frame_count = vr->m_render_frame_count;

    if (m_game_tex.texture.Get() == nullptr && backbuffer.Get() == real_backbuffer.Get()) {
        spdlog::info("[VR] Setting up game texture as copy of backbuffer");
        
        ComPtr<ID3D12Resource> backbuffer_copy{};
        D3D12_HEAP_PROPERTIES heap_props{};
        heap_props.Type = D3D12_HEAP_TYPE_DEFAULT;
        heap_props.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
        heap_props.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;

        auto desc = backbuffer->GetDesc();
        desc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
        desc.Flags &= ~D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE;

        m_backbuffer_copy.reset();

        ComPtr<ID3D12Resource> backbuffer_copy2{};

        if (FAILED(device->CreateCommittedResource(&heap_props, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_RENDER_TARGET, nullptr, IID_PPV_ARGS(&backbuffer_copy2)))) {
            spdlog::error("[VR] Failed to create backbuffer copy.");
            return vr::VRCompositorError_None;
        }

        if (!m_backbuffer_copy.setup(device, backbuffer_copy2.Get(), std::nullopt, std::nullopt, L"Backbuffer Copy")) {
            spdlog::error("[VR] Failed to fully setup backbuffer copy.");
            m_backbuffer_copy.reset();
        }

        desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM; // UE backbuffer is not VR compatible, so we need to copy it to a new texture with this one.

        if (FAILED(device->CreateCommittedResource(&heap_props, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_RENDER_TARGET, nullptr, IID_PPV_ARGS(&backbuffer_copy)))) {
            spdlog::error("[VR] Failed to create backbuffer copy.");
            return vr::VRCompositorError_None;
        }

        if (!m_game_tex.setup(device, backbuffer_copy.Get(), DXGI_FORMAT_B8G8R8A8_UNORM, DXGI_FORMAT_B8G8R8A8_UNORM, L"Game Texture")) {
            spdlog::error("[VR] Failed to fully setup game texture.");
            m_game_tex.reset();
        } else {
            for (auto& commands : m_game_tex_commands) {
                commands.setup(L"Game Texture Commands");
            }
        }
    } else if (backbuffer.Get() != real_backbuffer.Get() && (use_stable_external_backbuffer_copy || m_game_tex.texture.Get() != backbuffer.Get() || !texture_context_has_views(m_game_tex))) {
        log_shf_texture_reference_rebuild(backbuffer.Get(), real_backbuffer.Get(), m_game_tex.texture.Get(), frame_count);

        if (use_stable_external_backbuffer_copy) {
            const auto source_desc = backbuffer->GetDesc();
            const auto needs_copy_texture =
                m_game_tex.texture.Get() == nullptr ||
                !shf_texture_desc_matches(m_game_tex.texture->GetDesc(), source_desc);

            if (needs_copy_texture) {
                SPDLOG_WARN("[{}][D3D12] Creating owned stable scene copy for volatile external RT [{}x{} fmt={} flags=0x{:x}]",
                    stable_external_copy_label, source_desc.Width, source_desc.Height, (uint32_t)source_desc.Format, (uint32_t)source_desc.Flags);

                D3D12_HEAP_PROPERTIES heap_props{};
                heap_props.Type = D3D12_HEAP_TYPE_DEFAULT;
                heap_props.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
                heap_props.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;

                auto copy_desc = source_desc;
                copy_desc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
                copy_desc.Flags &= ~D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE;

                ComPtr<ID3D12Resource> stable_copy{};

                if (FAILED(device->CreateCommittedResource(&heap_props, D3D12_HEAP_FLAG_NONE, &copy_desc, ENGINE_SRC_COLOR, nullptr, IID_PPV_ARGS(&stable_copy)))) {
                    SPDLOG_ERROR_EVERY_N_SEC(1,
                        "[{}][D3D12] Failed to create owned stable scene copy [{}x{} fmt={} flags=0x{:x}]; falling back to volatile RT path",
                        stable_external_copy_label, copy_desc.Width, copy_desc.Height, (uint32_t)copy_desc.Format, (uint32_t)copy_desc.Flags);
                    m_game_tex.reset();
                } else if (!m_game_tex.setup(device, stable_copy.Get(), DXGI_FORMAT_B8G8R8A8_UNORM, DXGI_FORMAT_B8G8R8A8_UNORM, stable_external_copy_name)) {
                    spdlog::error("[{}][D3D12] Failed to setup owned stable scene copy.", stable_external_copy_label);
                    m_game_tex.reset();
                } else {
                    for (auto& commands : m_game_tex_commands) {
                        if (!commands.ready()) {
                            commands.setup(L"SHf Stable Scene Copy Commands");
                        }
                    }
                }
            }

            if (m_game_tex.texture.Get() != nullptr) {
                const auto idx = swapchain->GetCurrentBackBufferIndex() % m_game_tex_commands.size();
                auto& command_ctx = m_game_tex_commands[idx];

                if (!command_ctx.ready()) {
                    command_ctx.setup(L"SHf Stable Scene Copy Commands");
                }

                if (command_ctx.ready()) {
                    command_ctx.wait(INFINITE);
                    command_ctx.copy(backbuffer.Get(), m_game_tex.texture.Get(), volatile_external_source_state, ENGINE_SRC_COLOR);
                    command_ctx.execute();

                    SPDLOG_INFO_EVERY_N_SEC(2,
                        "[{}][D3D12] Copied volatile external RT into owned stable scene texture for HMD/mirror/2D",
                        stable_external_copy_label);

                    m_skip_spectator_view_for_volatile_external_rt = false;
                    backbuffer = m_game_tex.texture;
                    scene_source_state = ENGINE_SRC_COLOR;
                }
            }

            if (m_game_tex.texture.Get() == nullptr) {
                SPDLOG_WARNING_EVERY_N_SEC(
                    1,
                    "[{}][D3D12] Stable scene copy unavailable; falling back to volatile external RT reference",
                    stable_external_copy_label);
                scene_source_state = volatile_external_source_state;

                if (!m_game_tex.setup(device, backbuffer.Get(), DXGI_FORMAT_B8G8R8A8_UNORM, DXGI_FORMAT_B8G8R8A8_UNORM, L"Game Texture")) {
                    spdlog::error("[VR] Failed to fully setup fallback game texture reference.");
                    m_game_tex.reset();
                }
            }
        } else {
            spdlog::info("[VR] Setting up game texture as reference to original");

            if (!m_game_tex.setup(device, backbuffer.Get(), DXGI_FORMAT_B8G8R8A8_UNORM, DXGI_FORMAT_B8G8R8A8_UNORM, L"Game Texture")) {
                spdlog::error("[VR] Failed to fully setup game texture.");
                m_game_tex.reset();
            }
        }
    }

    if (vr->is_native_stereo_fix_enabled()) {
        const auto scene_capture = ffsr->get_render_target_manager()->get_scene_capture_render_target();
        const auto scene_capture_rt = scene_capture != nullptr ? (ID3D12Resource*)scene_capture->get_native_resource() : nullptr;

        if (is_avowed_current_game()) {
            SPDLOG_INFO_EVERY_N_SEC(
                2,
                "[Avowed][D3D12][NativeStereoFix] Scene capture texture state: rhi={} native={} cached={} game_tex={}",
                (uintptr_t)scene_capture,
                (uintptr_t)scene_capture_rt,
                (uintptr_t)m_scene_capture_tex.texture.Get(),
                (uintptr_t)m_game_tex.texture.Get());
        }

        if (scene_capture_rt != nullptr && m_scene_capture_tex.texture.Get() != scene_capture_rt) {
            spdlog::info("[VR] Setting up scene capture texture as reference to original");

            if (!m_scene_capture_tex.setup(device, scene_capture_rt, DXGI_FORMAT_B8G8R8A8_UNORM, DXGI_FORMAT_B8G8R8A8_UNORM, L"Scene Capture Texture")) {
                spdlog::error("[VR] Failed to fully setup scene capture texture.");
                m_scene_capture_tex.reset();
            }
        }

        if (scene_capture_rt == nullptr && m_scene_capture_tex.texture.Get() != nullptr) {
            spdlog::info("[VR] Resetting scene capture texture");

            m_scene_capture_tex.reset();
        }
    } else {
        m_scene_capture_tex.reset();
    }

    // We need to render the scene capture texture to the right side of the double wide texture
    auto pre_render = [&](d3d12::CommandContext& commands, ID3D12Resource* render_target) {
        if (render_target == nullptr) {
            return;
        }

        // Also the same for right, even though it's not a double wide texture
        D3D12_BOX left_src_box{
            .left = 0,
            .top = 0,
            .front = 0,
            .right = m_backbuffer_size[0] / 2,
            .bottom = m_backbuffer_size[1],
            .back = 1
        };

        commands.copy_region_stereo(
            m_game_tex.texture.Get(), m_scene_capture_tex.texture.Get(), render_target,
            &left_src_box, &left_src_box,
            0, 0, 0, m_backbuffer_size[0] / 2, 0, 0,
            D3D12_RESOURCE_STATE_RENDER_TARGET,
            D3D12_RESOURCE_STATE_RENDER_TARGET
        );
    };

    // For copying the real backbuffer if we need to
    if (m_game_tex.texture.Get() != nullptr && backbuffer == real_backbuffer) {
        const auto idx = swapchain->GetCurrentBackBufferIndex() % m_game_tex_commands.size();
        auto& command_ctx = m_game_tex_commands[idx];
        if (command_ctx.cmd_list != nullptr) {
            command_ctx.wait(INFINITE);
            float clear_color[] = { 0.0f, 0.0f, 0.0f, 0.0f };
            command_ctx.clear_rtv(m_game_tex, (float*)&clear_color, D3D12_RESOURCE_STATE_RENDER_TARGET);
            command_ctx.copy(real_backbuffer.Get(), m_backbuffer_copy.texture.Get(), D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
            //m_game_tex_commands[idx].copy(backbuffer.Get(), m_game_tex.texture.Get(), D3D12_RESOURCE_STATE_PRESENT, ENGINE_SRC_COLOR);
            d3d12::render_srv_to_rtv(
                m_game_batch.get(),
                command_ctx.cmd_list.Get(),
                m_backbuffer_copy,
                m_game_tex,
                D3D12_RESOURCE_STATE_RENDER_TARGET,
                D3D12_RESOURCE_STATE_RENDER_TARGET
            );
            command_ctx.execute();
        }

        backbuffer = m_game_tex.texture;
    }

    auto* effective_game_tex = &m_game_tex;
    bool shf_using_mono_expansion = false;
    auto shf_scene_mode = ShfSceneMode::Unknown;

    if (is_shf_external_backbuffer && m_game_tex.texture.Get() != nullptr && real_backbuffer.Get() != nullptr) {
        const auto source_desc = m_game_tex.texture->GetDesc();
        const auto real_desc = real_backbuffer->GetDesc();
        shf_scene_mode = classify_shf_scene_mode(source_desc, real_desc);

        if (SHF_AUTO_MONO_CINEMATIC && shf_scene_mode == ShfSceneMode::Mono2D) {
            if (auto* mono_scene = render_shf_mono_scene_texture(device); mono_scene != nullptr && mono_scene->texture.Get() != nullptr) {
                effective_game_tex = mono_scene;
                backbuffer = mono_scene->texture;
                scene_source_state = ENGINE_SRC_COLOR;
                shf_using_mono_expansion = true;
            } else {
                SPDLOG_ERROR_EVERY_N_SEC(
                    1,
                    "[SHf][D3D12] Mono scene source detected but expansion texture was unavailable; leaving existing stereo copy path active");
            }
        }

        log_shf_scene_mode_if_needed(shf_scene_mode, source_desc, real_desc, frame_count, shf_using_mono_expansion);
    }

    if (ui_target != nullptr) {
        if (m_game_ui_tex.texture.Get() != ui_target->get_native_resource()) {
            if (!m_game_ui_tex.setup(device, 
                (ID3D12Resource*)ui_target->get_native_resource(), 
                DXGI_FORMAT_B8G8R8A8_UNORM, DXGI_FORMAT_B8G8R8A8_UNORM,
                L"Game UI Texture"))
            {
                spdlog::error("[VR] Failed to fully setup game UI texture.");
                m_game_ui_tex.reset();
            }
        }

        // Recreate UI texture if needed
        if (!vr->is_extreme_compatibility_mode_enabled()) {
            const auto native = (ID3D12Resource*)ui_target->get_native_resource();
            const auto is_same_native = native == m_last_checked_native;
            m_last_checked_native = native;

            if (native != nullptr && !is_same_native) {
                const auto desc = native->GetDesc();

                if (runtime->is_openxr()) {
                    if (auto it = vr->m_openxr->swapchains.find((uint32_t)runtimes::OpenXR::SwapchainIndex::UI);
                        it != vr->m_openxr->swapchains.end()) 
                    {
                        const auto& uisc = it->second;
                        if (desc.Width != uisc.width ||
                            desc.Height != uisc.height)
                        {
                            SPDLOG_INFO_EVERY_N_SEC(1, "[OpenXR] UI size changed, recreating [{}x{}]->[{}x{}]", desc.Width, desc.Height, uisc.width, uisc.height);
                            ffsr->set_should_recreate_textures(true);
                        }
                    }
                } else if (m_game_ui_tex.texture != nullptr) {
                    const auto ui_desc = m_game_ui_tex.texture->GetDesc();

                    if (desc.Width != ui_desc.Width || desc.Height != ui_desc.Height) {
                        SPDLOG_INFO_EVERY_N_SEC(1, "[OpenVR] UI size changed, recreating texture [{}x{}]->[{}x{}]", desc.Width, desc.Height, ui_desc.Width, ui_desc.Height);
                        ffsr->set_should_recreate_textures(true);
                    }
                }
            } else if (native == nullptr) {
                spdlog::error("[VR] Recreating UI texture because native resource is null");
                ffsr->set_should_recreate_textures(true);
            }
        }
    } else {
        const bool keep_pending_ue57_ui =
            ffsr->get_render_target_manager()->get_dedicated_ui_width() != 0 &&
            ffsr->get_render_target_manager()->get_dedicated_ui_height() != 0 &&
            ffsr->get_render_target_manager()->is_dedicated_ui_target_pending();

        if (!keep_pending_ue57_ui) {
            m_game_ui_tex.reset(); // Probably fixes non-resident errors.
        }
    }

    const float clear_color[] = { 0.0f, 0.0f, 0.0f, 0.0f };
    const auto is_2d_screen = vr->is_using_2d_screen();
    const auto shf_auto_2d_screen =
        SHF_AUTO_2D_SCREEN_FROM_MONO_CINEMATIC &&
        is_shf_external_backbuffer &&
        shf_scene_mode == ShfSceneMode::Mono2D &&
        m_game_tex.texture.Get() != nullptr &&
        m_game_tex.srv_heap != nullptr;
    const auto use_2d_screen = is_2d_screen || shf_auto_2d_screen;

    if (shf_auto_2d_screen) {
        SPDLOG_INFO_EVERY_N_SEC(
            2,
            "[SHf][D3D12] Auto 2D screen active for detected Mono2D cinematic segment");
    }

    if (use_2d_screen && effective_game_tex != nullptr && effective_game_tex->texture.Get() != nullptr) {
        ensure_2d_screen_textures(device, effective_game_tex->texture->GetDesc());
    }

    auto draw_2d_view = [&](d3d12::CommandContext& commands, ID3D12Resource* render_target) {
        auto& view_game_tex = effective_game_tex != nullptr ? *effective_game_tex : m_game_tex;
        const auto view_game_tex_clear_state =
            (is_shf_external_backbuffer || shf_using_mono_expansion) ? ENGINE_SRC_COLOR : D3D12_RESOURCE_STATE_RENDER_TARGET;

        if (ui_invert_alpha > 0.0f && !skip_in_place_ui_invert && m_game_ui_tex.texture.Get() != nullptr && m_game_ui_tex.srv_heap != nullptr) {
            const std::array<float, 4> blend_factor{ 1.0f, 1.0f, 1.0f, ui_invert_alpha };
            const DirectX::XMFLOAT4 invert_alpha_tint{ 1.0f, 1.0f, 1.0f, ui_invert_alpha };
            d3d12::render_srv_to_rtv(
                m_ui_batch_alpha_invert.get(),
                commands.cmd_list.Get(),
                m_game_ui_tex,
                m_game_ui_tex,
                ENGINE_SRC_COLOR,
                ENGINE_SRC_COLOR,
                blend_factor,
                invert_alpha_tint);
        }

        draw_spectator_view(commands.cmd_list.Get(), is_right_eye_frame, &view_game_tex);

        const auto has_2d_screen_textures =
            m_2d_screen_tex[0].texture.Get() != nullptr &&
            m_2d_screen_tex[1].texture.Get() != nullptr &&
            m_2d_screen_tex[0].rtv_heap != nullptr &&
            m_2d_screen_tex[1].rtv_heap != nullptr;

        if (use_2d_screen && has_2d_screen_textures && view_game_tex.texture.Get() != nullptr && view_game_tex.srv_heap != nullptr) {
            // Clear previous frame
            for (auto& screen : m_2d_screen_tex) {
                commands.clear_rtv(screen, clear_color, ENGINE_SRC_COLOR);
            }

            const auto use_shf_flat_screen_source = is_shf_current_game();
            auto* screen_source_tex = &view_game_tex;

            if (use_shf_flat_screen_source &&
                shf_scene_mode == ShfSceneMode::Mono2D &&
                m_game_tex.texture.Get() != nullptr &&
                m_game_tex.srv_heap != nullptr) {
                screen_source_tex = &m_game_tex;
            }

            const auto view_desc = screen_source_tex->texture->GetDesc();
            RECT left_source_rect{0, 0, (LONG)((float)m_backbuffer_size[0] / 2.0f), (LONG)m_backbuffer_size[1]};
            RECT right_source_rect{(LONG)((float)m_backbuffer_size[0] / 2.0f), 0, (LONG)((float)m_backbuffer_size[0]), (LONG)m_backbuffer_size[1]};
            std::optional<RECT> screen_dest_rect = std::nullopt;

            if (use_shf_flat_screen_source) {
                const auto source_width = (LONG)view_desc.Width;
                const auto source_height = (LONG)view_desc.Height;
                left_source_rect = RECT{0, 0, source_width, source_height};

                // Mono2D uses the original wide cinematic source. Other manual 2D cases use a matched single eye.
                if (shf_scene_mode != ShfSceneMode::Mono2D &&
                    view_desc.Width >= (uint64_t)view_desc.Height * 2 &&
                    view_desc.Width >= 2) {
                    left_source_rect.right = (LONG)(view_desc.Width / 2);
                }

                right_source_rect = left_source_rect;
                const auto screen_desc = m_2d_screen_tex[0].texture->GetDesc();
                const auto source_rect_width = (float)(left_source_rect.right - left_source_rect.left);
                const auto source_rect_height = (float)(left_source_rect.bottom - left_source_rect.top);
                const auto screen_width = (float)screen_desc.Width;
                const auto screen_height = (float)screen_desc.Height;
                RECT dest_rect{0, 0, (LONG)screen_desc.Width, (LONG)screen_desc.Height};

                if (source_rect_width > 0.0f && source_rect_height > 0.0f && screen_width > 0.0f && screen_height > 0.0f) {
                    const auto source_aspect = source_rect_width / source_rect_height;
                    const auto screen_aspect = screen_width / screen_height;

                    if (source_aspect > screen_aspect) {
                        const auto fitted_height = (LONG)(screen_width / source_aspect);
                        const auto y = ((LONG)screen_desc.Height - fitted_height) / 2;
                        dest_rect.top = y;
                        dest_rect.bottom = y + fitted_height;
                    } else {
                        const auto fitted_width = (LONG)(screen_height * source_aspect);
                        const auto x = ((LONG)screen_desc.Width - fitted_width) / 2;
                        dest_rect.left = x;
                        dest_rect.right = x + fitted_width;
                    }

                    screen_dest_rect = dest_rect;
                }

                SPDLOG_INFO_EVERY_N_SEC(
                    2,
                    "[SHf][D3D12] 2D screen using matched mono source mode={} auto={} tex=[{}x{} fmt={}] src=[{},{} -> {},{}] dst=[{},{} -> {},{}]",
                    shf_scene_mode_name(m_shf_scene_mode),
                    shf_auto_2d_screen,
                    view_desc.Width,
                    view_desc.Height,
                    (uint32_t)view_desc.Format,
                    left_source_rect.left,
                    left_source_rect.top,
                    left_source_rect.right,
                    left_source_rect.bottom,
                    screen_dest_rect ? screen_dest_rect->left : 0,
                    screen_dest_rect ? screen_dest_rect->top : 0,
                    screen_dest_rect ? screen_dest_rect->right : (LONG)m_2d_screen_tex[0].texture->GetDesc().Width,
                    screen_dest_rect ? screen_dest_rect->bottom : (LONG)m_2d_screen_tex[0].texture->GetDesc().Height);
            }

            d3d12::render_srv_to_rtv(
                m_game_batch.get(),
                commands.cmd_list.Get(),
                *screen_source_tex,
                m_2d_screen_tex[0],
                left_source_rect,
                screen_dest_rect,
                ENGINE_SRC_COLOR,
                ENGINE_SRC_COLOR
            );

            if (m_game_ui_tex.texture.Get() != nullptr && m_game_ui_tex.srv_heap != nullptr) {
                d3d12::render_srv_to_rtv(
                    m_game_batch.get(),
                    commands.cmd_list.Get(),
                    m_game_ui_tex,
                    m_2d_screen_tex[0],
                    ENGINE_SRC_COLOR,
                    ENGINE_SRC_COLOR
                );
            }

            if (!is_afr) {
                if (!use_shf_flat_screen_source && m_scene_capture_tex.texture.Get() != nullptr) {
                    d3d12::render_srv_to_rtv(
                        m_game_batch.get(),
                        commands.cmd_list.Get(),
                        m_scene_capture_tex,
                        m_2d_screen_tex[1],
                        ENGINE_SRC_COLOR,
                        ENGINE_SRC_COLOR
                    );
                } else {
                    d3d12::render_srv_to_rtv(
                        m_game_batch.get(),
                        commands.cmd_list.Get(),
                        *screen_source_tex,
                        m_2d_screen_tex[1],
                        right_source_rect,
                        screen_dest_rect,
                        ENGINE_SRC_COLOR,
                        ENGINE_SRC_COLOR
                    );
                }

                if (m_game_ui_tex.texture.Get() != nullptr && m_game_ui_tex.srv_heap != nullptr) {
                    d3d12::render_srv_to_rtv(
                        m_game_batch.get(),
                        commands.cmd_list.Get(),
                        m_game_ui_tex,
                        m_2d_screen_tex[1],
                        ENGINE_SRC_COLOR,
                        ENGINE_SRC_COLOR
                    );
                }
            }

            // Clear the RT so the entire background is black when submitting to the compositor
            commands.clear_rtv(view_game_tex, (float*)&clear_color, view_game_tex_clear_state);

            if (m_scene_capture_tex.texture.Get() != nullptr) {
                commands.clear_rtv(m_scene_capture_tex, (float*)&clear_color, D3D12_RESOURCE_STATE_RENDER_TARGET);
            }
        }
    };

    // Draws the spectator view
    auto clear_rt = [&](d3d12::CommandContext& commands) {
		if (m_game_ui_tex.texture.Get() == nullptr) {
            return;
        }
		
        const float ui_clear_color[] = { 0.0f, 0.0f, 0.0f, ui_invert_alpha };
        commands.clear_rtv(m_game_ui_tex, (float*)&ui_clear_color, ENGINE_SRC_COLOR);
    };

    auto ensure_openxr_frame_began = [&](const char* caller) -> bool {
        if (!runtime->is_openxr() || !vr->m_openxr->can_run_frame_loop()) {
            return false;
        }

        if (vr->m_openxr->frame_began) {
            return true;
        }

        if (defer_stalker2_transition_openxr && !vr->m_openxr->frame_synced) {
            return false;
        }

        const auto begin_result = vr->m_openxr->begin_frame(caller);

        if (!vr->m_openxr->frame_began) {
            SPDLOG_INFO_EVERY_N_SEC(
                1,
                "[OpenXR] Skipping D3D12 OpenXR copy because begin_frame did not leave a frame open: {}",
                vr->m_openxr->get_result_string(begin_result)
            );
            return false;
        }

        return true;
    };

    if (runtime->is_openvr() && m_openvr.ui_tex.texture.Get() != nullptr) {
        const auto ui_copy_start = std::chrono::steady_clock::now();
        utility::ScopeGuard ui_copy_timing_guard{[&]() {
            m_perf_ui_copy.add(std::chrono::steady_clock::now() - ui_copy_start);
        }};

        m_openvr.ui_tex.commands.wait(INFINITE);

        draw_2d_view(m_openvr.ui_tex.commands, nullptr);

        if (is_right_eye_frame) {
            if (use_2d_screen) {
                m_openvr.ui_tex.commands.copy(m_2d_screen_tex[0].texture.Get(), m_openvr.ui_tex.texture.Get(), ENGINE_SRC_COLOR);
            } else if (ui_target != nullptr) {
                m_openvr.ui_tex.commands.copy((ID3D12Resource*)ui_target->get_native_resource(), m_openvr.ui_tex.texture.Get(), ENGINE_SRC_COLOR);
            }
        } else if (use_2d_screen) {
            m_openvr.ui_tex.commands.copy(m_2d_screen_tex[0].texture.Get(), m_openvr.ui_tex.texture.Get(), ENGINE_SRC_COLOR);
        }

        clear_rt(m_openvr.ui_tex.commands);
        m_openvr.ui_tex.commands.execute();
    } else if (runtime->is_openxr() && vr->m_openxr->can_run_frame_loop() && ensure_openxr_frame_began("d3d12_first_copy")) {
        const auto ui_copy_start = std::chrono::steady_clock::now();
        utility::ScopeGuard ui_copy_timing_guard{[&]() {
            m_perf_ui_copy.add(std::chrono::steady_clock::now() - ui_copy_start);
        }};

        if (suppress_ui_copy) {
            SPDLOG_INFO_EVERY_N_SEC(2, "[OpenXR][debug] Skipping UI copy for perf isolation");
        } else {
            if (is_right_eye_frame) {
                if (use_2d_screen) {
                    if (is_afr) {
                        m_openxr.copy((uint32_t)runtimes::OpenXR::SwapchainIndex::UI_RIGHT, m_2d_screen_tex[0].texture.Get(), draw_2d_view, clear_rt, ENGINE_SRC_COLOR);
                    } else {
                        m_openxr.copy((uint32_t)runtimes::OpenXR::SwapchainIndex::UI, m_2d_screen_tex[0].texture.Get(), draw_2d_view, std::nullopt, ENGINE_SRC_COLOR);
                        m_openxr.copy((uint32_t)runtimes::OpenXR::SwapchainIndex::UI_RIGHT, m_2d_screen_tex[1].texture.Get(), std::nullopt, clear_rt, ENGINE_SRC_COLOR);
                    }
                } else if (ui_target != nullptr) {
                    m_openxr.copy((uint32_t)runtimes::OpenXR::SwapchainIndex::UI, (ID3D12Resource*)ui_target->get_native_resource(), draw_2d_view, clear_rt, ENGINE_SRC_COLOR);
                }

                auto fw_rt = g_framework->get_rendertarget_d3d12();

                if (fw_rt && g_framework->is_drawing_anything()) {
                    m_openxr.copy((uint32_t)runtimes::OpenXR::SwapchainIndex::FRAMEWORK_UI, g_framework->get_rendertarget_d3d12().Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
                }
            } else if (use_2d_screen) {
                m_openxr.copy((uint32_t)runtimes::OpenXR::SwapchainIndex::UI, m_2d_screen_tex[0].texture.Get(), draw_2d_view, clear_rt, ENGINE_SRC_COLOR);
            } else if (m_game_ui_tex.commands.ready()) {
                m_game_ui_tex.commands.wait(INFINITE);
                draw_2d_view(m_game_ui_tex.commands, nullptr);
                clear_rt(m_game_ui_tex.commands);
                m_game_ui_tex.commands.execute();
            }
        }
    }

    /*else if (m_game_tex.texture.Get() != nullptr) {
        m_game_tex.commands.wait(INFINITE);
        draw_spectator_view(m_game_tex.commands.cmd_list.Get(), is_right_eye_frame);
        m_game_tex.commands.execute();
    }*/

    ComPtr<ID3D12Resource> scene_depth_tex{};

    if (vr->is_depth_enabled() && runtime->is_depth_allowed()) {
        auto& rt_pool = vr->get_render_target_pool_hook();
        scene_depth_tex = rt_pool->get_texture<ID3D12Resource>(L"SceneDepthZ");

        if (scene_depth_tex != nullptr) {
            const auto desc = scene_depth_tex->GetDesc();

            if (runtime->is_openxr()) {
                if (vr->m_openxr->needs_depth_resize(desc.Width, desc.Height) || m_openxr.made_depth_with_null_defaults) {
                    uint32_t reasons = SWAPCHAIN_RECREATE_DEPTH_EXTENT;
                    if (m_openxr.made_depth_with_null_defaults) {
                        reasons |= SWAPCHAIN_RECREATE_DEPTH_NULL_DEFAULTS;
                    }
                    log_openxr_swapchain_recreate(vr, reasons, (uint32_t)desc.Width, (uint32_t)desc.Height);
                    prepare_openxr_swapchain_recreate(vr, reasons);
                    m_openxr.create_swapchains(); // recreate swapchains to match the new depth size
                }
            }
        }

    #ifdef AFR_DEPTH_TEMP_DISABLED
        if (is_actually_afr) {
            scene_depth_tex.Reset();
        }
    #endif
    }

    if (shf_using_mono_expansion && scene_depth_tex != nullptr) {
        SPDLOG_INFO_EVERY_N_SEC(2, "[SHf][D3D12] Suppressing depth submit while mono cutscene expansion is active");
        scene_depth_tex.Reset();
    }

    if ((debug_disable_depth_submit || debug_submit_empty_frame || debug_skip_scene_copy) && scene_depth_tex != nullptr) {
        SPDLOG_INFO_EVERY_N_SEC(2, "[OpenXR][debug] Suppressing depth submit for perf isolation");
        scene_depth_tex.Reset();
    }

    // Single-view rendering (DIBR or Mono) only writes the reference view's
    // half of SceneDepthZ - don't hand the stale other half to the runtime as
    // a composition depth layer. (run_dibr_synthesis re-resolves depth for
    // the synthesis itself and only samples the rendered half.)
    if (scene_depth_tex != nullptr && vr->is_single_view_rendering_active()) {
        SPDLOG_INFO_EVERY_N_SEC(5, "[DIBR] Suppressing depth-layer submit while single-view rendering is active");
        scene_depth_tex.Reset();
    }

    // 2026-05-24 SN2 RIGHT-EYE COLOR TRANSFER: process the SBS backbuffer's right half (give it the
    // left half's underwater color while keeping its own luminance) before the per-eye copies, so the
    // right eye displays underwater like the left. Gated by UEVR_SN2_RIGHT_EYE_COLOR_TRANSFER; no-op
    // otherwise. Runs synchronously (its own command context + fence wait) so the eye copies that read
    // `backbuffer` below see the processed image.
    ::sn2_color_transfer::run(backbuffer.Get(), scene_source_state, m_backbuffer_size[0], m_backbuffer_size[1]);

    // DIBR synthetic stereo (see DIBR_PORT_PLAN.md): rewrite the SBS backbuffer in place
    // with depth-synthesized stereo before the per-eye copies consume it. Env-gated via
    // UEVR_DIBR; no-op otherwise. scene_depth_tex may have been suppressed above (mono
    // expansion / debug toggles) - run_dibr_synthesis re-resolves SceneDepthZ itself.
    run_dibr_synthesis(vr, backbuffer.Get(), scene_source_state, scene_depth_tex.Get());

    // If m_frame_count is even, we're rendering the left eye.
    if (is_left_eye_frame) {
        m_submitted_left_eye = true;

        // OpenXR texture
        if (runtime->is_openxr() && vr->m_openxr->can_run_frame_loop()) {
            const auto swapchain_copy_start = std::chrono::steady_clock::now();
            utility::ScopeGuard swapchain_copy_timing_guard{[&]() {
                m_perf_swapchain_copy.add(std::chrono::steady_clock::now() - swapchain_copy_start);
            }};

            D3D12_BOX src_box{};
            src_box.left = 0;
            src_box.top = 0;
            src_box.bottom = m_backbuffer_size[1];
            src_box.front = 0;
            src_box.back = 1;

            if (vr->is_extreme_compatibility_mode_enabled()) {
                src_box.right = m_backbuffer_size[0];
            } else {
                src_box.right = m_backbuffer_size[0] / 2;
            }

            if (suppress_scene_copy) {
                SPDLOG_INFO_EVERY_N_SEC(2, "[OpenXR][debug] Skipping left-eye scene copy for perf isolation");
                if (!debug_submit_empty_frame) {
                    m_openxr.copy((uint32_t)runtimes::OpenXR::SwapchainIndex::AFR_LEFT_EYE, nullptr, scene_source_state, nullptr);
                }
            } else {
                m_openxr.copy((uint32_t)runtimes::OpenXR::SwapchainIndex::AFR_LEFT_EYE, backbuffer.Get(), scene_source_state, &src_box);

                if (scene_depth_tex != nullptr) {
                    m_openxr.copy((uint32_t)runtimes::OpenXR::SwapchainIndex::AFR_DEPTH_LEFT_EYE, scene_depth_tex.Get(), ENGINE_SRC_DEPTH, nullptr);
                }
            }
        }

        // OpenVR texture
        // Copy the back buffer to the left eye texture
        if (runtime->is_openvr()) {
            m_openvr.copy_left(backbuffer.Get(), scene_source_state);

            auto openvr = vr->get_runtime<runtimes::OpenVR>();
            const auto submit_pose = openvr->get_pose_for_submit();

            vr::D3D12TextureData_t left {
                m_openvr.get_left().texture.Get(),
                command_queue,
                0
            };
            
            vr::VRTextureWithPose_t left_eye{
                (void*)&left, vr::TextureType_DirectX12, vr::ColorSpace_Auto,
                submit_pose
            };
            const auto left_bounds = vr::VRTextureBounds_t{runtime->view_bounds[0][0], runtime->view_bounds[0][2],
                                                           runtime->view_bounds[0][1], runtime->view_bounds[0][3]};
            auto e = vr::VRCompositor()->Submit(vr::Eye_Left, &left_eye, &left_bounds, vr::EVRSubmitFlags::Submit_TextureWithPose);

            if (e != vr::VRCompositorError_None) {
                spdlog::error("[VR] VRCompositor failed to submit left eye: {}", (int)e);
                return e;
            }
        }
    } else {
        utility::ScopeGuard __{[&]() {
            m_submitted_left_eye = false;
        }};

        // OpenXR texture
        if (runtime->is_openxr() && vr->m_openxr->can_run_frame_loop()) {
            const auto swapchain_copy_start = std::chrono::steady_clock::now();
            utility::ScopeGuard swapchain_copy_timing_guard{[&]() {
                m_perf_swapchain_copy.add(std::chrono::steady_clock::now() - swapchain_copy_start);
            }};

            if (is_actually_afr && !is_afr && !m_submitted_left_eye) {
                D3D12_BOX src_box{};
                src_box.left = 0;
                src_box.top = 0;
                src_box.bottom = m_backbuffer_size[1];
                src_box.front = 0;
                src_box.back = 1;

                if (vr->is_extreme_compatibility_mode_enabled()) {
                    src_box.right = m_backbuffer_size[0];
                } else {
                    src_box.right = m_backbuffer_size[0] / 2;
                }

                if (suppress_scene_copy) {
                    SPDLOG_INFO_EVERY_N_SEC(2, "[OpenXR][debug] Skipping staged left-eye scene copy for perf isolation");
                    if (!debug_submit_empty_frame) {
                        m_openxr.copy((uint32_t)runtimes::OpenXR::SwapchainIndex::AFR_LEFT_EYE, nullptr, scene_source_state, nullptr);
                    }
                } else {
                    m_openxr.copy((uint32_t)runtimes::OpenXR::SwapchainIndex::AFR_LEFT_EYE, backbuffer.Get(), scene_source_state, &src_box);

                    if (scene_depth_tex != nullptr) {
                        m_openxr.copy((uint32_t)runtimes::OpenXR::SwapchainIndex::AFR_DEPTH_LEFT_EYE, scene_depth_tex.Get(), ENGINE_SRC_DEPTH, nullptr);
                    }
                }
            }

            if (is_actually_afr) {
                D3D12_BOX src_box{};

                if (!vr->is_extreme_compatibility_mode_enabled()) {
                    if (!is_afr) {
                        src_box.left = m_backbuffer_size[0] / 2;
                        src_box.right = m_backbuffer_size[0];
                        src_box.top = 0;
                        src_box.bottom = m_backbuffer_size[1];
                        src_box.front = 0;
                        src_box.back = 1;
                    } else { // Copy the left eye on AFR
                        src_box.left = 0;
                        src_box.right = m_backbuffer_size[0] / 2;
                        src_box.top = 0;
                        src_box.bottom = m_backbuffer_size[1];
                        src_box.front = 0;
                        src_box.back = 1;
                    }   
                } else {
                    src_box.left = 0;
                    src_box.right = m_backbuffer_size[0];
                    src_box.top = 0;
                    src_box.bottom = m_backbuffer_size[1];
                    src_box.front = 0;
                    src_box.back = 1;
                }

                if (suppress_scene_copy) {
                    SPDLOG_INFO_EVERY_N_SEC(2, "[OpenXR][debug] Skipping right-eye scene copy for perf isolation");
                    if (!debug_submit_empty_frame) {
                        m_openxr.copy((uint32_t)runtimes::OpenXR::SwapchainIndex::AFR_RIGHT_EYE, nullptr, scene_source_state, nullptr);
                    }
                } else {
                    m_openxr.copy((uint32_t)runtimes::OpenXR::SwapchainIndex::AFR_RIGHT_EYE, backbuffer.Get(), scene_source_state, &src_box);

                    if (scene_depth_tex != nullptr) {
                        m_openxr.copy((uint32_t)runtimes::OpenXR::SwapchainIndex::AFR_DEPTH_RIGHT_EYE, scene_depth_tex.Get(), ENGINE_SRC_DEPTH, nullptr);
                    }
                }
            } else {
                // Copy over the entire double wide, or split native stereo into per-eye OpenXR swapchains.
                if (suppress_scene_copy) {
                    SPDLOG_INFO_EVERY_N_SEC(2, "[OpenXR][debug] Skipping double-wide scene copy for perf isolation");
                    if (!debug_submit_empty_frame) {
                        m_openxr.copy((uint32_t)runtimes::OpenXR::SwapchainIndex::DOUBLE_WIDE, nullptr, scene_source_state, nullptr);
                    }
                } else {
                    const auto native_left_swapchain = (uint32_t)runtimes::OpenXR::SwapchainIndex::AFR_LEFT_EYE;
                    const auto native_right_swapchain = (uint32_t)runtimes::OpenXR::SwapchainIndex::AFR_RIGHT_EYE;
                    const auto native_stereo_array_swapchain = (uint32_t)runtimes::OpenXR::SwapchainIndex::NATIVE_STEREO_ARRAY;
                    const auto use_native_split_submit =
                        vr->is_native_stereo_fix_enabled() &&
                        !vr->is_native_stereo_fix_same_pass_enabled() &&
                        vr->m_openxr->swapchains.contains(native_left_swapchain) &&
                        vr->m_openxr->swapchains.contains(native_right_swapchain);
                    const auto use_native_array_submit =
                        !use_native_split_submit &&
                        vr->is_native_stereo_fix_enabled() &&
                        !vr->is_native_stereo_fix_same_pass_enabled() &&
                        vr->m_openxr->swapchains.contains(native_stereo_array_swapchain);

                    if (use_native_split_submit || use_native_array_submit) {
                        // DIBR overscan growth resizes the engine render target
                        // mid-session; the scene swapchains must follow or the
                        // eye-sized box copies below become invalid (black eyes).
                        if (use_native_array_submit) {
                            const auto expected_eye_w = vr->get_dibr_render_eye_width();
                            const auto& arr_sc = vr->m_openxr->swapchains[native_stereo_array_swapchain];
                            if (arr_sc.width != static_cast<int32_t>(expected_eye_w)) {
                                spdlog::info("[VR] Scene swapchain eye width {} != expected {}; recreating swapchains",
                                    arr_sc.width, expected_eye_w);
                                prepare_openxr_swapchain_recreate(vr, SWAPCHAIN_RECREATE_SCENE_EYE_EXTENT);
                                m_openxr.create_swapchains();
                            }
                        }

                        const auto backbuffer_desc = backbuffer->GetDesc();
                        SPDLOG_INFO_ONCE("[NativeStereoDebug] Split submit source backbuffer={}x{} configured={}x{} state={} mode={}",
                            backbuffer_desc.Width,
                            backbuffer_desc.Height,
                            m_backbuffer_size[0],
                            m_backbuffer_size[1],
                            (uint32_t)scene_source_state,
                            use_native_split_submit ? "per-eye" : "array");
                        {
                            static std::atomic<uint64_t> native_submit_seq{0};
                            const auto nsn = native_submit_seq.fetch_add(1, std::memory_order_relaxed) + 1;
                            if (nsn <= 32 || (nsn % 600) == 0) {
                                SPDLOG_INFO("[NativeStereoDebug] Split submit#{} source=0x{:x} size={}x{} fmt={} flags=0x{:x} configured={}x{} state={} mode={}",
                                    nsn,
                                    reinterpret_cast<uintptr_t>(backbuffer.Get()),
                                    backbuffer_desc.Width,
                                    backbuffer_desc.Height,
                                    static_cast<unsigned>(backbuffer_desc.Format),
                                    static_cast<unsigned>(backbuffer_desc.Flags),
                                    m_backbuffer_size[0],
                                    m_backbuffer_size[1],
                                    static_cast<unsigned>(scene_source_state),
                                    use_native_split_submit ? "per-eye" : "array");
                            }
                        }

                        D3D12_BOX left_src_box{};
                        left_src_box.left = 0;
                        left_src_box.top = 0;
                        left_src_box.right = m_backbuffer_size[0] / 2;
                        left_src_box.bottom = m_backbuffer_size[1];
                        left_src_box.front = 0;
                        left_src_box.back = 1;

                        D3D12_BOX right_src_box{};
                        right_src_box.left = m_backbuffer_size[0] / 2;
                        right_src_box.top = 0;
                        right_src_box.right = m_backbuffer_size[0];
                        right_src_box.bottom = m_backbuffer_size[1];
                        right_src_box.front = 0;
                        right_src_box.back = 1;

                        SPDLOG_INFO_ONCE("[NativeStereoDebug] Split submit left box={} {} {} {}, right box={} {} {} {}",
                            left_src_box.left, left_src_box.top, left_src_box.right, left_src_box.bottom,
                            right_src_box.left, right_src_box.top, right_src_box.right, right_src_box.bottom);

                        dump_native_stereo_backbuffer_once(backbuffer.Get(), left_src_box, right_src_box, scene_source_state);

                        // #10: name the native SBS source for both split + array paths
                        // (the array texture itself is named inside the array-submit lambda).
                        sn2_name_native_stereo_resources(nullptr, backbuffer.Get());

                        if (use_native_split_submit) {
                            SPDLOG_INFO_ONCE("[NativeStereoDebug] Split submit using native per-eye OpenXR swapchains");
                            // SN2 fog hack: check env var to mirror left half to right eye.
                            static const bool mirror_left_to_right = []() {
                                wchar_t value[16]{};
                                const auto len = GetEnvironmentVariableW(
                                    L"UEVR_SUBNAUTICA2_MIRROR_LEFT_TO_RIGHT_EYE",
                                    value, (DWORD)std::size(value));
                                return len > 0 && value[0] != L'\0' && value[0] != L'0';
                            }();
                            m_openxr.copy(native_left_swapchain, backbuffer.Get(), scene_source_state, &left_src_box);
                            if (mirror_left_to_right) {
                                SPDLOG_INFO_ONCE("[NativeStereoDebug] MIRROR ENABLED: right eye sampling left half of backbuffer");
                                m_openxr.copy(native_right_swapchain, backbuffer.Get(), scene_source_state, &left_src_box);
                            } else {
                                m_openxr.copy(native_right_swapchain, backbuffer.Get(), scene_source_state, &right_src_box);
                            }
                        } else {
                            SPDLOG_INFO_ONCE("[NativeStereoDebug] Array submit using native stereo swapchain slices 0/1");
                            // SN2 fog hack: mirror ONLY THE TOP HALF (sky region with teal fog)
                            // of left eye to right eye. Right eye keeps its own foreground
                            // render with parallax for the bottom half.
                            static const bool mirror_top_half = []() {
                                wchar_t value[16]{};
                                const auto len = GetEnvironmentVariableW(
                                    L"UEVR_SUBNAUTICA2_MIRROR_LEFT_TO_RIGHT_EYE",
                                    value, (DWORD)std::size(value));
                                return len > 0 && value[0] != L'\0' && value[0] != L'0';
                            }();
                            // 2026-05-27 FULL mirror variant: copy ENTIRE left half to slice 1
                            // (right eye) instead of just the top sky region. Eliminates the
                            // mid-screen seam at the cost of zero right-eye parallax. Proof-of-
                            // mechanism for the SN2 right-eye-fog work — slice 1 then matches
                            // slice 0 fully so the OpenXR submit shows teal everywhere on right.
                            static const bool mirror_full_left = []() {
                                wchar_t value[16]{};
                                const auto len = GetEnvironmentVariableW(
                                    L"UEVR_SUBNAUTICA2_MIRROR_LEFT_TO_RIGHT_EYE_FULL",
                                    value, (DWORD)std::size(value));
                                return len > 0 && value[0] != L'\0' && value[0] != L'0';
                            }();
                            // Compute top-half source box from left half of backbuffer.
                            D3D12_BOX left_top_src_box = left_src_box;
                            left_top_src_box.bottom = left_src_box.top + (left_src_box.bottom - left_src_box.top) / 2;
                            SPDLOG_INFO_ONCE("[NativeStereoDebug] Mirror flag (sky-only top half): {} full: {}, left_top box={}-{} {}-{}",
                                mirror_top_half, mirror_full_left,
                                left_top_src_box.left, left_top_src_box.right,
                                left_top_src_box.top, left_top_src_box.bottom);
                            // Capture scene depth into the lambda so the depth-blend compute can sample it.
                            ComPtr<ID3D12Resource> lambda_scene_depth = scene_depth_tex;
                            // The depth-blend needs depth even when UEVR depth-submit is OFF (scene_depth_tex
                            // is only populated when is_depth_enabled()). Fetch SceneDepthZ directly for the
                            // blend (read-only) so the DEPTH heuristic (far-plane = distant fog) works — the
                            // luma-only fallback cannot catch the right eye's DARK distant region.
                            if (lambda_scene_depth == nullptr && sn2_depth_blend::env_on()) {
                                auto& rt_pool_db = vr->get_render_target_pool_hook();
                                if (rt_pool_db != nullptr) {
                                    lambda_scene_depth = rt_pool_db->get_texture<ID3D12Resource>(L"SceneDepthZ");
                                }
                            }
                            const UINT lambda_slice_w = static_cast<UINT>(left_src_box.right - left_src_box.left);
                            const UINT lambda_slice_h = static_cast<UINT>(left_src_box.bottom - left_src_box.top);
                            const UINT lambda_bb_w   = static_cast<UINT>(m_backbuffer_size[0]);
                            m_openxr.copy(
                                native_stereo_array_swapchain,
                                nullptr,
                                [backbuffer, left_src_box, right_src_box, left_top_src_box, scene_source_state,
                                 mirror = mirror_top_half, mirror_full = mirror_full_left,
                                 scene_depth = lambda_scene_depth,
                                 slice_w = lambda_slice_w, slice_h = lambda_slice_h, bb_w = lambda_bb_w]
                                (d3d12::CommandContext& commands, ID3D12Resource* dst) mutable {
                                    // #10: name the OpenXR array texture (slice0=left/slice1=right)
                                    // and the native SBS source for RenderDoc/OpenXR legibility.
                                    sn2_name_native_stereo_resources(dst, backbuffer.Get());
                                    sn2_openxr_array_diag::clear_source_box_if_enabled(
                                        commands,
                                        backbuffer.Get(),
                                        right_src_box,
                                        scene_source_state);
                                    // Slice 0 (left eye): full left half
                                    commands.copy_region_to_subresource(
                                        backbuffer.Get(),
                                        dst,
                                        &left_src_box,
                                        0,
                                        scene_source_state,
                                        D3D12_RESOURCE_STATE_RENDER_TARGET);
                                    // Slice 1 (right eye): default is right half of SBS (right-eye parallax).
                                    // With UEVR_SUBNAUTICA2_MIRROR_LEFT_TO_RIGHT_EYE_FULL=1: skip right_src_box
                                    // entirely and copy LEFT half — both eyes show identical left render.
                                    if (mirror_full) {
                                        commands.copy_region_to_subresource(
                                            backbuffer.Get(),
                                            dst,
                                            &left_src_box,
                                            1,
                                            scene_source_state,
                                            D3D12_RESOURCE_STATE_RENDER_TARGET);
                                    } else {
                                        commands.copy_region_to_subresource(
                                            backbuffer.Get(),
                                            dst,
                                            &right_src_box,
                                            1,
                                            scene_source_state,
                                            D3D12_RESOURCE_STATE_RENDER_TARGET);
                                        // Then overlay top half from left eye (the sky region with teal fog)
                                        // ONLY if (top-only) mirror flag is set.
                                        if (mirror) {
                                            commands.copy_region_to_subresource(
                                                backbuffer.Get(),
                                                dst,
                                                &left_top_src_box,
                                                1,
                                                scene_source_state,
                                                D3D12_RESOURCE_STATE_RENDER_TARGET);
                                        }
                                    }
                                    // 2026-05-28 SN2 depth-aware right-eye fog blend (parallax-correct
                                    // version of MIRROR_FULL). Gated by UEVR_SN2_RIGHT_EYE_DEPTH_BLEND.
                                    // Depth is optional (luma-only heuristic kicks in if null).
                                    sn2_depth_blend::run(
                                        commands.cmd_list.Get(),
                                        dst,                 D3D12_RESOURCE_STATE_RENDER_TARGET,
                                        scene_depth.Get(),   ENGINE_SRC_DEPTH,
                                        slice_w, slice_h, bb_w);
                                    sn2_openxr_array_diag::clear_slice_if_enabled(commands, dst);
                                },
                                std::nullopt,
                                D3D12_RESOURCE_STATE_RENDER_TARGET,
                                nullptr);
                        }
                    } else if (m_scene_capture_tex.texture.Get() == nullptr || shf_using_mono_expansion) {
                        m_openxr.copy((uint32_t)runtimes::OpenXR::SwapchainIndex::DOUBLE_WIDE, backbuffer.Get(), scene_source_state, nullptr);
                    } else {
                        m_openxr.copy((uint32_t)runtimes::OpenXR::SwapchainIndex::DOUBLE_WIDE, nullptr, pre_render, std::nullopt, D3D12_RESOURCE_STATE_RENDER_TARGET, nullptr);
                    }

                    if (scene_depth_tex != nullptr) {
                        m_openxr.copy((uint32_t)runtimes::OpenXR::SwapchainIndex::DEPTH, scene_depth_tex.Get(), ENGINE_SRC_DEPTH, nullptr);
                    }
                }
            }
        }

        // OpenVR texture
        // Copy the back buffer to the left and right eye textures.
        if (runtime->is_openvr()) {
            auto openvr = vr->get_runtime<runtimes::OpenVR>();
            const auto submit_pose = openvr->get_pose_for_submit();

            if (!is_afr) {
                m_openvr.copy_left(backbuffer.Get(), scene_source_state);

                vr::D3D12TextureData_t left {
                    m_openvr.get_left().texture.Get(),
                    command_queue,
                    0
                };

                vr::VRTextureWithPose_t left_eye{
                    (void*)&left, vr::TextureType_DirectX12, vr::ColorSpace_Auto,
                    submit_pose
                };
                const auto left_bounds = vr::VRTextureBounds_t{runtime->view_bounds[0][0], runtime->view_bounds[0][2],
                                                               runtime->view_bounds[0][1], runtime->view_bounds[0][3]};
                auto e = vr::VRCompositor()->Submit(vr::Eye_Left, &left_eye, &left_bounds, vr::EVRSubmitFlags::Submit_TextureWithPose);

                if (e != vr::VRCompositorError_None) {
                    spdlog::error("[VR] VRCompositor failed to submit left eye: {}", (int)e);
                    //return e; // dont return because it will just completely stop us from even getting to the right eye which could be catastrophic
                }
            }

            if (!is_afr) {
                if (m_scene_capture_tex.texture.Get() == nullptr) {
                    m_openvr.copy_right(backbuffer.Get(), scene_source_state);
                } else {
                    m_openvr.copy_left_to_right(m_scene_capture_tex.texture.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET);
                }
            } else {
                m_openvr.copy_left_to_right(backbuffer.Get(), scene_source_state);
            }

            vr::D3D12TextureData_t right {
                m_openvr.get_right().texture.Get(),
                command_queue,
                0
            };

            vr::VRTextureWithPose_t right_eye{
                (void*)&right, vr::TextureType_DirectX12, vr::ColorSpace_Auto,
                submit_pose
            };
            const auto right_bounds = vr::VRTextureBounds_t{runtime->view_bounds[1][0], runtime->view_bounds[1][2],
                                                            runtime->view_bounds[1][1], runtime->view_bounds[1][3]};
            auto e = vr::VRCompositor()->Submit(vr::Eye_Right, &right_eye, &right_bounds, vr::EVRSubmitFlags::Submit_TextureWithPose);
            runtime->frame_synced = false;

            if (e != vr::VRCompositorError_None) {
                spdlog::error("[VR] VRCompositor failed to submit right eye: {}", (int)e);
                return e;
            } else {
                vr->m_submitted = true;
            }

            ++m_openvr.texture_counter;
        }
    }

    if (is_right_eye_frame) {
        if ((runtime->ready() && vr->get_synchronize_stage() == VR::SynchronizeStage::VERY_LATE) || !runtime->got_first_sync) {
            //vr->update_hmd_state();
        }
    }

    vr::EVRCompositorError e = vr::EVRCompositorError::VRCompositorError_None;

    if (is_right_eye_frame) {
        ////////////////////////////////////////////////////////////////////////////////
        // OpenXR start ////////////////////////////////////////////////////////////////
        ////////////////////////////////////////////////////////////////////////////////
        if (runtime->is_openxr() && vr->m_openxr->can_run_frame_loop()) {
            const auto openxr_submit_start = std::chrono::steady_clock::now();
            utility::ScopeGuard openxr_submit_timing_guard{[&]() {
                m_perf_openxr_submit.add(std::chrono::steady_clock::now() - openxr_submit_start);
            }};

            if (defer_stalker2_transition_openxr && !vr->m_openxr->frame_synced && !vr->m_openxr->frame_began) {
                SPDLOG_INFO_EVERY_N_SEC(
                    1,
                    "[Stalker2][OpenXR] Skipping D3D12 OpenXR submit for transition guard because no frame was synchronized");
                return e;
            }

            if (!vr->m_openxr->frame_began) {
                const auto begin_result = vr->m_openxr->begin_frame("d3d12_submit");

                if (!vr->m_openxr->frame_began) {
                    SPDLOG_INFO_EVERY_N_SEC(
                        1,
                        "[OpenXR] Skipping D3D12 submit because begin_frame did not leave a frame open: {}",
                        vr->m_openxr->get_result_string(begin_result)
                    );
                    return e;
                }
            }

            vr->m_openxr->refresh_stale_pose_before_submit(frame_count, "d3d12_submit");

            std::vector<XrCompositionLayerBaseHeader*> quad_layers{};

            auto& openxr_overlay = vr->get_overlay_component().get_openxr();
            const auto ui_pose_diagnostics_enabled = vr->is_ui_layer_pose_telemetry_enabled() || vr->is_ui_layer_pose_stabilizer_enabled();
            const auto ui_pose_basis = ui_pose_diagnostics_enabled ? vr->build_ui_layer_pose_basis(frame_count) : vrmod::UILayerPoseBasis{};
            const auto* ui_pose_basis_ptr = ui_pose_diagnostics_enabled ? &ui_pose_basis : nullptr;

            if (!suppress_ui_copy && use_2d_screen) {
                if (shf_auto_2d_screen) {
                    SPDLOG_INFO_EVERY_N_SEC(
                        2,
                        "[SHf][D3D12] Submitting auto 2D screen as eye-specific OpenXR slate layers");
                }

                const auto left_layer = openxr_overlay.generate_slate_layer(runtimes::OpenXR::SwapchainIndex::UI, XrEyeVisibility::XR_EYE_VISIBILITY_LEFT, ui_pose_basis_ptr);
                const auto right_layer = openxr_overlay.generate_slate_layer(runtimes::OpenXR::SwapchainIndex::UI_RIGHT, XrEyeVisibility::XR_EYE_VISIBILITY_RIGHT, ui_pose_basis_ptr);

                if (left_layer && m_openxr.ever_acquired((uint32_t)runtimes::OpenXR::SwapchainIndex::UI)) {
                    quad_layers.push_back((XrCompositionLayerBaseHeader*)&left_layer->get());
                }

                if (right_layer && m_openxr.ever_acquired((uint32_t)runtimes::OpenXR::SwapchainIndex::UI_RIGHT)) {
                    quad_layers.push_back((XrCompositionLayerBaseHeader*)&right_layer->get());
                }
            } else if (!suppress_ui_copy && m_openxr.ever_acquired((uint32_t)runtimes::OpenXR::SwapchainIndex::UI)) {
                const auto slate_layer = openxr_overlay.generate_slate_layer(runtimes::OpenXR::SwapchainIndex::UI, XrEyeVisibility::XR_EYE_VISIBILITY_BOTH, ui_pose_basis_ptr);

                if (slate_layer) {
                    quad_layers.push_back(&slate_layer->get());
                }   
            }
            
            if (!suppress_ui_copy && m_openxr.ever_acquired((uint32_t)runtimes::OpenXR::SwapchainIndex::FRAMEWORK_UI)) {
                const auto framework_quad = openxr_overlay.generate_framework_ui_quad();
                if (framework_quad) {
                    quad_layers.push_back((XrCompositionLayerBaseHeader*)&framework_quad->get());
                }
            }

            auto result = vr->m_openxr->end_frame(quad_layers, scene_depth_tex.Get() != nullptr);

            if (result == XR_ERROR_LAYER_INVALID) {
                spdlog::info("[VR] Attempting to correct invalid layer");

                m_openxr.wait_for_all_copies();

                spdlog::info("[VR] Calling xrEndFrame again");
                result = vr->m_openxr->end_frame(quad_layers);
            }

            vr->m_openxr->needs_pose_update = true;
            vr->m_submitted = result == XR_SUCCESS;
        }

        ////////////////////////////////////////////////////////////////////////////////
        // OpenVR start ////////////////////////////////////////////////////////////////
        ////////////////////////////////////////////////////////////////////////////////
        if (runtime->is_openvr()) {
            if (runtime->needs_pose_update) {
                vr->m_submitted = false;
                spdlog::info("[VR] Runtime needed pose update inside present (frame {})", vr->m_frame_count);
                return vr::VRCompositorError_None;
            }

            //++m_openvr.texture_counter;
        }

        // Allows the desktop window to be recorded.
        /*if (vr->m_desktop_fix->value()) {
            if (runtime->ready() && m_prev_backbuffer != backbuffer && m_prev_backbuffer != nullptr) {
                m_generic_commands[frame_count % 3].wait(INFINITE);
                m_generic_commands[frame_count % 3].copy(m_prev_backbuffer.Get(), backbuffer.Get(), D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_PRESENT);
                m_generic_commands[frame_count % 3].execute();
            }
        }*/
    }

    m_prev_backbuffer = backbuffer;

    return e;
}

void D3D12Component::log_frame_timing_stats_if_needed(VR* vr) {
    if (!frame_profiler_log_enabled()) {
        return;
    }

    const auto now = std::chrono::steady_clock::now();

    if (m_last_frame_timing_log.time_since_epoch().count() == 0) {
        m_last_frame_timing_log = now;
        return;
    }

    if (now - m_last_frame_timing_log < FRAME_TIMING_LOG_INTERVAL) {
        return;
    }

    if (m_perf_on_frame.count == 0 &&
        m_perf_ui_copy.count == 0 &&
        m_perf_swapchain_copy.count == 0 &&
        m_perf_openxr_submit.count == 0 &&
        m_perf_spectator_mirror.count == 0 &&
        m_perf_post_present.count == 0)
    {
        m_last_frame_timing_log = now;
        return;
    }

    bool has_ui_target = false;
    bool ui_target_pending = false;
    uint32_t dedicated_ui_width = 0;
    uint32_t dedicated_ui_height = 0;

    if (vr != nullptr && vr->m_fake_stereo_hook != nullptr) {
        const auto rtm = vr->m_fake_stereo_hook->get_render_target_manager();

        if (rtm != nullptr) {
            has_ui_target = rtm->get_ui_target() != nullptr;
            ui_target_pending = rtm->is_dedicated_ui_target_pending();
            dedicated_ui_width = rtm->get_dedicated_ui_width();
            dedicated_ui_height = rtm->get_dedicated_ui_height();
        }
    }

    const auto mirror_mode = vr != nullptr ? (int)vr->get_desktop_mirror_mode() : -1;
    const auto desktop_fix = vr != nullptr && vr->m_desktop_fix->value();
    const auto hmd_active = vr != nullptr && vr->is_hmd_active();
    const auto afr = vr != nullptr && vr->is_using_afr();
    const auto native_stereo = vr != nullptr && vr->is_native_stereo_fix_enabled();
    const auto has_ui_tex = m_game_ui_tex.texture.Get() != nullptr;
    const auto has_game_tex = m_game_tex.texture.Get() != nullptr;

    spdlog::info(
        "[D3D12][frame-profiler] on_frame avg={:.2f}ms max={:.2f}ms n={} ui_copy avg={:.2f}ms max={:.2f}ms n={} swapchain_copy avg={:.2f}ms max={:.2f}ms n={} openxr_submit avg={:.2f}ms max={:.2f}ms n={} spectator_mirror avg={:.2f}ms max={:.2f}ms n={} post_present avg={:.2f}ms max={:.2f}ms n={} mirror_mode={} desktop_fix={} hmd={} afr={} native_stereo={} has_game_tex={} has_ui_tex={} has_ui_target={} ui_pending={} ui_extent={}x{} submitted={} dbg_empty={} dbg_skip_scene={} dbg_skip_ui={} dbg_no_depth={}",
        m_perf_on_frame.avg(),
        m_perf_on_frame.max_ms,
        m_perf_on_frame.count,
        m_perf_ui_copy.avg(),
        m_perf_ui_copy.max_ms,
        m_perf_ui_copy.count,
        m_perf_swapchain_copy.avg(),
        m_perf_swapchain_copy.max_ms,
        m_perf_swapchain_copy.count,
        m_perf_openxr_submit.avg(),
        m_perf_openxr_submit.max_ms,
        m_perf_openxr_submit.count,
        m_perf_spectator_mirror.avg(),
        m_perf_spectator_mirror.max_ms,
        m_perf_spectator_mirror.count,
        m_perf_post_present.avg(),
        m_perf_post_present.max_ms,
        m_perf_post_present.count,
        mirror_mode,
        desktop_fix,
        hmd_active,
        afr,
        native_stereo,
        has_game_tex,
        has_ui_tex,
        has_ui_target,
        ui_target_pending,
        dedicated_ui_width,
        dedicated_ui_height,
        vr != nullptr && vr->m_submitted,
        vr != nullptr && vr->m_openxr != nullptr && vr->m_openxr->debug_submit_empty_frame->value(),
        vr != nullptr && vr->m_openxr != nullptr && vr->m_openxr->debug_skip_scene_copy->value(),
        vr != nullptr && vr->m_openxr != nullptr && vr->m_openxr->debug_skip_ui_copy->value(),
        vr != nullptr && vr->m_openxr != nullptr && vr->m_openxr->debug_disable_depth_submit->value()
    );

    m_last_frame_timing_log = now;
    m_perf_on_frame.reset();
    m_perf_ui_copy.reset();
    m_perf_swapchain_copy.reset();
    m_perf_openxr_submit.reset();
    m_perf_spectator_mirror.reset();
    m_perf_post_present.reset();
}

bool D3D12Component::has_game_and_ui_textures() const {
    return m_game_tex.texture.Get() != nullptr &&
        m_game_ui_tex.texture.Get() != nullptr;
}

bool D3D12Component::is_initialized() const {
    if (m_openvr.left_eye_tex[0].texture != nullptr) {
        return true;
    }

    std::scoped_lock _{const_cast<std::recursive_mutex&>(m_openxr.mtx)};
    for (const auto& [_, ctx] : m_openxr.contexts) {
        if (!ctx.textures.empty() && ctx.textures[0].texture != nullptr) {
            return true;
        }
    }

    return false;
}

const char* D3D12Component::get_shf_scene_mode_str() const {
    return shf_scene_mode_name(m_shf_scene_mode);
}

D3D12Component::EyeTarget D3D12Component::get_current_eye_target(int side) const {
    using SwapIdx = ::runtimes::OpenXR::SwapchainIndex;
    using XRContext = D3D12Component::OpenXR::SwapchainContext;
    EyeTarget out{};
    const bool right = (side == 1);

    // 1) OpenVR mirror eye textures — populated only when running OpenVR.
    if (m_openvr.left_eye_tex[0].texture != nullptr) {
        const auto idx = m_openvr.texture_counter % m_openvr.left_eye_tex.size();
        out.texture = right ? m_openvr.right_eye_tex[idx].texture : m_openvr.left_eye_tex[idx].texture;
        if (out.texture != nullptr) {
            const auto desc = out.texture->GetDesc();
            out.region_w = static_cast<UINT>(desc.Width);
            out.region_h = desc.Height;
            out.path = "OpenVR";
            return out;
        }
    }

    std::scoped_lock _{const_cast<std::recursive_mutex&>(m_openxr.mtx)};

    auto get_xr_ctx = [&](SwapIdx which) -> const XRContext* {
        auto it = m_openxr.contexts.find(static_cast<uint32_t>(which));
        if (it == m_openxr.contexts.end()) return nullptr;
        if (it->second.textures.empty()) return nullptr;
        return &it->second;
    };
    auto current_tex = [](const XRContext& ctx) -> ID3D12Resource* {
        if (ctx.textures.empty()) return nullptr;
        const auto i = (std::min)((size_t)ctx.last_acquired_texture, ctx.textures.size() - 1);
        return ctx.textures[i].texture;
    };

    // 2) OpenXR AFR — separate swapchains per eye.
    if (const auto* afr = get_xr_ctx(right ? SwapIdx::AFR_RIGHT_EYE : SwapIdx::AFR_LEFT_EYE);
        afr != nullptr && current_tex(*afr) != nullptr) {
        out.texture = current_tex(*afr);
        const auto desc = out.texture->GetDesc();
        out.region_w = static_cast<UINT>(desc.Width);
        out.region_h = desc.Height;
        out.path = right ? "OpenXR/AFR_RIGHT_EYE" : "OpenXR/AFR_LEFT_EYE";
        return out;
    }

    // 3) OpenXR NATIVE_STEREO_ARRAY — single texture, two array slices.
    if (const auto* arr = get_xr_ctx(SwapIdx::NATIVE_STEREO_ARRAY);
        arr != nullptr && current_tex(*arr) != nullptr) {
        out.texture = current_tex(*arr);
        const auto desc = out.texture->GetDesc();
        out.region_w = static_cast<UINT>(desc.Width);
        out.region_h = desc.Height;
        out.array_slice = right ? 1u : 0u;
        out.path = "OpenXR/NATIVE_STEREO_ARRAY";
        return out;
    }

    // 4) OpenXR DOUBLE_WIDE — single texture, eye = half-region.
    if (const auto* dw = get_xr_ctx(SwapIdx::DOUBLE_WIDE);
        dw != nullptr && current_tex(*dw) != nullptr) {
        out.texture = current_tex(*dw);
        const auto desc = out.texture->GetDesc();
        const UINT w = static_cast<UINT>(desc.Width);
        const UINT h = desc.Height;
        const UINT half = w / 2;
        out.region_x = right ? half : 0;
        out.region_y = 0;
        out.region_w = half;
        out.region_h = h;
        out.path = "OpenXR/DOUBLE_WIDE";
        return out;
    }

    // 5) Last-ditch fallback: the game's actual swapchain backbuffer, sampled as
    //    a half-region. This is what's actually presented in native stereo when
    //    UEVR isn't bouncing through its own mirror, e.g. some Mono2D paths.
    if (g_framework != nullptr) {
        if (auto& hook = g_framework->get_d3d12_hook(); hook != nullptr) {
            if (auto* swap = hook->get_swap_chain(); swap != nullptr) {
                Microsoft::WRL::ComPtr<ID3D12Resource> bb{};
                UINT idx = 0;
                if (auto* swap3 = static_cast<IDXGISwapChain3*>(swap); swap3 != nullptr) {
                    idx = swap3->GetCurrentBackBufferIndex();
                }
                if (SUCCEEDED(swap->GetBuffer(idx, IID_PPV_ARGS(&bb))) && bb != nullptr) {
                    const auto desc = bb->GetDesc();
                    const UINT w = static_cast<UINT>(desc.Width);
                    const UINT h = desc.Height;
                    const UINT half = (w >= 2) ? w / 2 : w;
                    out.texture = bb;
                    out.region_x = right ? half : 0;
                    out.region_y = 0;
                    out.region_w = (w >= 2) ? half : w;
                    out.region_h = h;
                    out.path = "Framework/Swapchain";
                    out.note = "Fallback: full backbuffer sampled as half-region. May not actually be a stereo backbuffer.";
                    return out;
                }
            }
        }
    }

    out.path = "none";
    return out;
}

namespace {
D3D12Component::FfiTiming to_ffi(const auto& s) {
    return {s.count, s.avg(), s.max_ms};
}
}

D3D12Component::FfiTiming D3D12Component::get_timing_on_frame()         const { return to_ffi(m_perf_on_frame); }
D3D12Component::FfiTiming D3D12Component::get_timing_ui_copy()          const { return to_ffi(m_perf_ui_copy); }
D3D12Component::FfiTiming D3D12Component::get_timing_swapchain_copy()   const { return to_ffi(m_perf_swapchain_copy); }
D3D12Component::FfiTiming D3D12Component::get_timing_openxr_submit()    const { return to_ffi(m_perf_openxr_submit); }
D3D12Component::FfiTiming D3D12Component::get_timing_spectator_mirror() const { return to_ffi(m_perf_spectator_mirror); }
D3D12Component::FfiTiming D3D12Component::get_timing_post_present()     const { return to_ffi(m_perf_post_present); }

D3D12Component::HitchFrameSnapshot D3D12Component::get_hitch_frame_snapshot(VR* vr) const {
    HitchFrameSnapshot snapshot{};
    snapshot.initialized = is_initialized();
    snapshot.force_reset = m_force_reset;
    snapshot.last_afr_state = m_last_afr_state;
    snapshot.has_prev_backbuffer = m_prev_backbuffer.Get() != nullptr;
    snapshot.has_game_tex = m_game_tex.texture.Get() != nullptr;
    snapshot.has_ui_tex = m_game_ui_tex.texture.Get() != nullptr;
    snapshot.has_scene_capture_tex = m_scene_capture_tex.texture.Get() != nullptr;
    snapshot.backbuffer_width = m_backbuffer_size[0];
    snapshot.backbuffer_height = m_backbuffer_size[1];
    const auto [ui_width, ui_height] = get_ui_extent();
    snapshot.ui_extent_width = ui_width;
    snapshot.ui_extent_height = ui_height;
    snapshot.hmd_width = vr != nullptr ? vr->get_hmd_width() : 0;
    snapshot.hmd_height = vr != nullptr ? vr->get_hmd_height() : 0;
    snapshot.swapchain_recreate_count = m_swapchain_recreate_count;
    snapshot.last_swapchain_recreate_reasons = m_last_swapchain_recreate_reasons;
    snapshot.perf_on_frame_count = m_perf_on_frame.count;
    snapshot.perf_on_frame_avg_ms = m_perf_on_frame.avg();
    snapshot.perf_on_frame_max_ms = m_perf_on_frame.max_ms;
    snapshot.perf_ui_copy_count = m_perf_ui_copy.count;
    snapshot.perf_ui_copy_avg_ms = m_perf_ui_copy.avg();
    snapshot.perf_ui_copy_max_ms = m_perf_ui_copy.max_ms;
    snapshot.perf_swapchain_copy_count = m_perf_swapchain_copy.count;
    snapshot.perf_swapchain_copy_avg_ms = m_perf_swapchain_copy.avg();
    snapshot.perf_swapchain_copy_max_ms = m_perf_swapchain_copy.max_ms;
    snapshot.perf_openxr_submit_count = m_perf_openxr_submit.count;
    snapshot.perf_openxr_submit_avg_ms = m_perf_openxr_submit.avg();
    snapshot.perf_openxr_submit_max_ms = m_perf_openxr_submit.max_ms;

    if (vr != nullptr && vr->m_openxr != nullptr) {
        const auto cached = vr->m_openxr->get_cached_swapchain_dimensions();
        snapshot.openxr_swapchain_count = cached.count;
        snapshot.ui_swapchain_width = cached.ui_width;
        snapshot.ui_swapchain_height = cached.ui_height;
        snapshot.eye_swapchain_width = cached.eye_width;
        snapshot.eye_swapchain_height = cached.eye_height;
        snapshot.depth_swapchain_width = cached.depth_width;
        snapshot.depth_swapchain_height = cached.depth_height;
    }

    return snapshot;
}

void D3D12Component::log_openxr_swapchain_recreate(VR* vr, uint32_t reasons, uint32_t new_depth_width, uint32_t new_depth_height) {
    if (reasons == SWAPCHAIN_RECREATE_NONE || vr == nullptr || vr->m_openxr == nullptr) {
        return;
    }

    uint32_t old_ui_width = 0;
    uint32_t old_ui_height = 0;
    uint32_t old_depth_width = 0;
    uint32_t old_depth_height = 0;
    uint32_t old_eye_width = 0;
    uint32_t old_eye_height = 0;
    size_t swapchain_count = 0;

    {
        std::scoped_lock _{vr->m_openxr->swapchain_mtx};
        swapchain_count = vr->m_openxr->swapchains.size();

        const auto read_swapchain = [&](runtimes::OpenXR::SwapchainIndex index, uint32_t& width, uint32_t& height) {
            const auto it = vr->m_openxr->swapchains.find((uint32_t)index);

            if (it != vr->m_openxr->swapchains.end()) {
                width = (uint32_t)std::max(0, it->second.width);
                height = (uint32_t)std::max(0, it->second.height);
            }
        };

        read_swapchain(runtimes::OpenXR::SwapchainIndex::DOUBLE_WIDE, old_eye_width, old_eye_height);
        if (old_eye_width == 0 || old_eye_height == 0) {
            read_swapchain(runtimes::OpenXR::SwapchainIndex::AFR_LEFT_EYE, old_eye_width, old_eye_height);
        }
        read_swapchain(runtimes::OpenXR::SwapchainIndex::UI, old_ui_width, old_ui_height);
        read_swapchain(runtimes::OpenXR::SwapchainIndex::DEPTH, old_depth_width, old_depth_height);
        if (old_depth_width == 0 || old_depth_height == 0) {
            read_swapchain(runtimes::OpenXR::SwapchainIndex::AFR_DEPTH_LEFT_EYE, old_depth_width, old_depth_height);
        }
    }

    const auto [new_ui_width, new_ui_height] = get_ui_extent();
    ++m_swapchain_recreate_count;
    m_last_swapchain_recreate_reasons = reasons;

    SPDLOG_INFO(
        "[OpenXR][swapchain-recreate] reasons={} old_hmd={}x{} new_hmd={}x{} old_eye={}x{} old_ui={}x{} new_ui={}x{} old_depth={}x{} new_depth={}x{} old_afr={} new_afr={} swapchains={}",
        format_swapchain_recreate_reasons(reasons),
        m_openxr.last_resolution[0],
        m_openxr.last_resolution[1],
        vr->get_hmd_width(),
        vr->get_hmd_height(),
        old_eye_width,
        old_eye_height,
        old_ui_width,
        old_ui_height,
        new_ui_width,
        new_ui_height,
        old_depth_width,
        old_depth_height,
        new_depth_width,
        new_depth_height,
        m_last_afr_state,
        vr->is_using_afr(),
        swapchain_count);
}

std::unique_ptr<DirectX::DX12::SpriteBatch> D3D12Component::setup_sprite_batch_pso(
    DXGI_FORMAT output_format, 
    std::span<const uint8_t> ps, 
    std::span<const uint8_t> vs, 
    std::optional<DirectX::SpriteBatchPipelineStateDescription> pd) 
{
    spdlog::info("[D3D12] Setting up sprite batch PSO");

    auto& hook = g_framework->get_d3d12_hook();

    auto device = hook->get_device();
    auto command_queue = hook->get_command_queue();
    auto swapchain = hook->get_swap_chain();

    DirectX::ResourceUploadBatch upload{ device };
    upload.Begin();

    if (!pd) {
        pd = DirectX::SpriteBatchPipelineStateDescription{DirectX::RenderTargetState{output_format, DXGI_FORMAT_UNKNOWN}};
    }

    if (ps.size() > 0) {
        pd->customPixelShader = D3D12_SHADER_BYTECODE{ps.data(), ps.size()};
    }

    if (vs.size() > 0) {
        pd->customVertexShader = D3D12_SHADER_BYTECODE{vs.data(), vs.size()};
    }

    auto batch = std::make_unique<DirectX::DX12::SpriteBatch>(device, upload, *pd);

    auto result = upload.End(command_queue);
    result.wait();

    spdlog::info("[D3D12] Sprite batch PSO setup complete");

    return batch;
}

void D3D12Component::draw_spectator_view(ID3D12GraphicsCommandList* command_list, bool is_right_eye_frame, d3d12::TextureContext* game_tex_override) {
    if (command_list == nullptr) {
        SPDLOG_INFO_EVERY_N_SEC(5, "[D3D12][spectator] disabled: command list is null");
        return;
    }

    if (m_skip_spectator_view_for_volatile_external_rt) {
        SPDLOG_INFO_EVERY_N_SEC(2, "[SHf][D3D12] Skipping desktop mirror for volatile external RT");
        return;
    }

    const auto& vr = VR::get();
    const auto mirror_mode = vr->get_desktop_mirror_mode();
    const auto has_ui_tex = m_game_ui_tex.texture != nullptr && m_game_ui_tex.srv_heap != nullptr && m_game_ui_tex.srv_heap->Heap() != nullptr;

    if (!vr->is_hmd_active()) {
        SPDLOG_INFO_EVERY_N_SEC(
            5,
            "[D3D12][spectator] disabled: HMD inactive mirror_mode={} desktop_fix={} has_ui_tex={}",
            (int)mirror_mode,
            vr->m_desktop_fix->value(),
            has_ui_tex);
        return;
    }

    if (!vr->m_desktop_fix->value()) {
        SPDLOG_INFO_EVERY_N_SEC(
            5,
            "[D3D12][spectator] disabled: Desktop Spectator View is off mirror_mode={} has_ui_tex={} right_eye_frame={}",
            (int)mirror_mode,
            has_ui_tex,
            is_right_eye_frame);
        return;
    }

    auto& game_tex = game_tex_override != nullptr ? *game_tex_override : m_game_tex;
    const auto has_game_tex = game_tex.texture != nullptr && game_tex.srv_heap != nullptr && game_tex.srv_heap->Heap() != nullptr;

    if (!has_game_tex) {
        SPDLOG_INFO_EVERY_N_SEC(
            5,
            "[D3D12][spectator] disabled: game texture context unavailable tex={} srv_heap={} srv={} mirror_mode={} desktop_fix={}",
            game_tex.texture.Get() != nullptr,
            game_tex.srv_heap != nullptr,
            game_tex.srv_heap != nullptr && game_tex.srv_heap->Heap() != nullptr,
            (int)mirror_mode,
            vr->m_desktop_fix->value());
        return;
    }

    const auto spectator_mirror_start = std::chrono::steady_clock::now();
    utility::ScopeGuard spectator_mirror_timing_guard{[&]() {
        m_perf_spectator_mirror.add(std::chrono::steady_clock::now() - spectator_mirror_start);
    }};

    auto& hook = g_framework->get_d3d12_hook();

    auto device = hook->get_device();
    auto command_queue = hook->get_command_queue();
    auto swapchain = hook->get_swap_chain();

    ComPtr<ID3D12Resource> backbuffer{};
    const auto index = swapchain->GetCurrentBackBufferIndex();

    if (FAILED(swapchain->GetBuffer(index, IID_PPV_ARGS(&backbuffer)))) {
        return;
    }

    if (index >= m_backbuffer_textures.size()) {
        m_backbuffer_textures.resize(index + 1);
        spdlog::info("[VR] Resized backbuffer textures to {}", index + 1);

        for (auto& tex : m_backbuffer_textures) {
            if (tex == nullptr) {
                tex = std::make_unique<d3d12::TextureContext>();
            }
        }
    }

    auto& backbuffer_ctx_ptr = m_backbuffer_textures[index];
    
    if (backbuffer_ctx_ptr == nullptr) {
        // if this has happened, assume the rest of the textures are also null
        for (auto& tex : m_backbuffer_textures) {
            if (tex == nullptr) {
                tex = std::make_unique<d3d12::TextureContext>();
            }
        }
    }

    auto& backbuffer_ctx = *backbuffer_ctx_ptr;

    const auto desc = backbuffer->GetDesc();

    if (backbuffer_ctx.texture.Get() != backbuffer.Get()) {
        if (!backbuffer_ctx.setup(device, backbuffer.Get(), std::nullopt, std::nullopt, L"Backbuffer")) {
            spdlog::error("[VR] Failed to setup backbuffer RTV (D3D12)");
            return;
        }

        spdlog::info("[VR] Created backbuffer RTV (D3D12)");
    }

    if (backbuffer_ctx.rtv_heap == nullptr || backbuffer_ctx.rtv_heap->Heap() == nullptr) {
        spdlog::error("[VR] Backbuffer RTV heap is null (D3D12)");
        return;
    }

    // Copy the previous right eye frame to the left eye frame
    const auto prev_index = (index + m_backbuffer_textures.size() - 1) % m_backbuffer_textures.size();
    if (vr->is_using_afr() && !is_right_eye_frame && m_backbuffer_textures[prev_index]->texture != nullptr) {
        const auto& last_right_eye_buffer = m_backbuffer_textures[prev_index]->texture;

        if (backbuffer.Get() != last_right_eye_buffer.Get()) {
            m_generic_commands[index % 3].wait(INFINITE);
            m_generic_commands[index % 3].copy(last_right_eye_buffer.Get(), backbuffer.Get(), D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_PRESENT);
            m_generic_commands[index % 3].execute();

            return;
        }
    }

    auto& batch = m_backbuffer_batch;

    D3D12_VIEWPORT viewport{};
    viewport.Width = (float)desc.Width;
    viewport.Height = (float)desc.Height;
    viewport.MaxDepth = 1.0f;
    
    batch->SetViewport(viewport);

    D3D12_RECT scissor_rect{};
    scissor_rect.left = 0;
    scissor_rect.top = 0;
    scissor_rect.right = (LONG)desc.Width;
    scissor_rect.bottom = (LONG)desc.Height;

    // Transition backbuffer to D3D12_RESOURCE_STATE_RENDER_TARGET
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = backbuffer.Get();
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    render::D3D12Diagnostics::get().record_resource_barriers("VR::D3D12Component::draw_spectator_view/BackbufferToRT", 1, &barrier);
    command_list->ResourceBarrier(1, &barrier);

    // Set RTV to backbuffer
    D3D12_CPU_DESCRIPTOR_HANDLE rtv_heaps[] = { backbuffer_ctx.get_rtv() };
    render::D3D12Diagnostics::get().record_rtv_bind("VR::D3D12Component::draw_spectator_view/BackbufferRT", 1, rtv_heaps, nullptr);
    command_list->OMSetRenderTargets(1, rtv_heaps, FALSE, nullptr);

    // Clear backbuffer
    const float bb_clear_color[] = { 0.0f, 0.0f, 0.0f, 0.0f };
    command_list->ClearRenderTargetView(backbuffer_ctx.get_rtv(), bb_clear_color, 0, nullptr);

    // Setup viewport and scissor rects
    command_list->RSSetViewports(1, &viewport);
    command_list->RSSetScissorRects(1, &scissor_rect);

    batch->Begin(command_list, DirectX::DX12::SpriteSortMode::SpriteSortMode_Immediate);

    RECT dest_rect{ 0, 0, (LONG)desc.Width, (LONG)desc.Height };

    const auto aspect_ratio = (float)desc.Width / (float)desc.Height;

    const auto eye_width = ((float)m_backbuffer_size[0] / 2.0f);
    const auto eye_height = (float)m_backbuffer_size[1];
    const auto eye_aspect_ratio = eye_width / eye_height;

    const auto original_centerw = (float)eye_width / 2.0f;
    const auto original_centerh = (float)eye_height / 2.0f;

    ///////////////
    // Eye (game) texture
    ///////////////
    // only show one half of the double wide texture (right side)
    RECT source_rect{};

    const bool force_sbs_desktop_mirror =
        vr->is_native_stereo_fix_enabled() &&
        sn2_openxr_array_diag::env_on("UEVR_SN2_DESKTOP_MIRROR_SBS");

    // Show the full SBS source for SN2 diagnostics when requested. Normally
    // UEVR mirrors a single eye to the desktop once OpenXR/native-stereo is
    // active, which hides desktop-only fixes from the visible game window.
    if (force_sbs_desktop_mirror) {
        source_rect.left = 0;
        source_rect.top = 0;
        source_rect.right = m_backbuffer_size[0];
        source_rect.bottom = m_backbuffer_size[1];
        SPDLOG_INFO_ONCE(
            "[SN2-DesktopMirror] UEVR_SN2_DESKTOP_MIRROR_SBS=1: desktop spectator shows full SBS source while OpenXR native stereo is active");
    }
    // Show left side when using AFR or native stereo fix
    else if (vr->is_using_afr() || vr->is_native_stereo_fix_enabled()) {
        source_rect.left = 0;
        source_rect.top = 0;
        source_rect.right = m_backbuffer_size[0] / 2;
        source_rect.bottom = m_backbuffer_size[1];
    } else {
        source_rect.left = (LONG)m_backbuffer_size[0] / 2;
        source_rect.top = 0;
        source_rect.right = m_backbuffer_size[0];
        source_rect.bottom = m_backbuffer_size[1];
    }

    // Correct left/top/right/bottom to match the aspect ratio of the game.
    // The SBS diagnostic mirror intentionally uses the whole source texture and
    // should not be cropped as if it were a single eye.
    if (!force_sbs_desktop_mirror) {
        if (eye_aspect_ratio > aspect_ratio) {
            const auto new_width = eye_height * aspect_ratio;
            const auto new_centerw = new_width / 2.0f;
            source_rect.left = (LONG)(original_centerw - new_centerw);
            source_rect.right = (LONG)(original_centerw + new_centerw);
        } else {
            const auto new_height = eye_width / aspect_ratio;
            const auto new_centerh = new_height / 2.0f;
            source_rect.top = (LONG)(original_centerh - new_centerh);
            source_rect.bottom = (LONG)(original_centerh + new_centerh);
        }
    }

    // Set descriptor heaps
    ID3D12DescriptorHeap* game_heaps[] = { game_tex.srv_heap->Heap() };
    render::D3D12Diagnostics::get().record_descriptor_heaps_set("VR::D3D12Component::draw_spectator_view/GameSRV", 1, game_heaps);
    command_list->SetDescriptorHeaps(1, game_heaps);

    batch->Draw(game_tex.get_srv_gpu(),
        DirectX::XMUINT2{ (uint32_t)m_backbuffer_size[0], (uint32_t)m_backbuffer_size[1] },
        dest_rect,
        &source_rect, 
        DirectX::Colors::White);

    if (mirror_mode == VR::DESKTOP_MIRROR_FULL && has_ui_tex) {
        const auto ui_desc = m_game_ui_tex.texture->GetDesc();
        ID3D12DescriptorHeap* ui_heaps[] = { m_game_ui_tex.srv_heap->Heap() };
        render::D3D12Diagnostics::get().record_descriptor_heaps_set("VR::D3D12Component::draw_spectator_view/UISRV", 1, ui_heaps);
        command_list->SetDescriptorHeaps(1, ui_heaps);

        batch->Draw(m_game_ui_tex.get_srv_gpu(), 
            DirectX::XMUINT2{ (uint32_t)ui_desc.Width, (uint32_t)ui_desc.Height },
            dest_rect, 
            DirectX::Colors::White);
    }

    batch->End();

    // Transition backbuffer to D3D12_RESOURCE_STATE_PRESENT
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
    render::D3D12Diagnostics::get().record_resource_barriers("VR::D3D12Component::draw_spectator_view/BackbufferToPresent", 1, &barrier);
    command_list->ResourceBarrier(1, &barrier);
}

void D3D12Component::clear_backbuffer() {
    auto& hook = g_framework->get_d3d12_hook();
    auto device = hook->get_device();
    auto swapchain = hook->get_swap_chain();

    if (device == nullptr || swapchain == nullptr) {
        return;
    }

    ComPtr<ID3D12Resource> backbuffer{};
    const auto index = swapchain->GetCurrentBackBufferIndex();

    if (FAILED(swapchain->GetBuffer(index, IID_PPV_ARGS(&backbuffer)))) {
        return;
    }

    if (backbuffer == nullptr) {
        return;
    }

    if (index >= m_backbuffer_textures.size()) {
        m_backbuffer_textures.resize(index + 1);
        spdlog::info("[VR] Resized backbuffer textures to {}", index + 1);

        for (auto& tex : m_backbuffer_textures) {
            if (tex == nullptr) {
                tex = std::make_unique<d3d12::TextureContext>();
            }
        }
    }

    auto& backbuffer_ctx_ptr = m_backbuffer_textures[index];
    
    if (backbuffer_ctx_ptr == nullptr) {
        // if this has happened, assume the rest of the textures are also null
        for (auto& tex : m_backbuffer_textures) {
            if (tex == nullptr) {
                tex = std::make_unique<d3d12::TextureContext>();
            }
        }
    }

    auto& backbuffer_ctx = *backbuffer_ctx_ptr;

    if (backbuffer_ctx.texture.Get() != backbuffer.Get()) {
        if (!backbuffer_ctx.setup(device, backbuffer.Get(), std::nullopt, std::nullopt, L"Backbuffer")) {
            spdlog::error("[VR] Failed to setup backbuffer RTV (D3D12)");
            return;
        }

        spdlog::info("[VR] Created backbuffer RTV (D3D12)");
    }

    // oh well
    if (backbuffer_ctx.rtv_heap == nullptr || backbuffer_ctx.rtv_heap->Heap() == nullptr) {
        return;
    }

    // Clear the backbuffer
    backbuffer_ctx.commands.wait(0);
    const float clear_color[] = { 0.0f, 0.0f, 0.0f, 0.0f };
    backbuffer_ctx.commands.clear_rtv(backbuffer_ctx.texture.Get(), backbuffer_ctx.get_rtv(), clear_color, D3D12_RESOURCE_STATE_PRESENT);
    backbuffer_ctx.commands.execute();
}

void D3D12Component::on_post_present(VR* vr) {
    const auto post_present_start = std::chrono::steady_clock::now();
    utility::ScopeGuard post_present_timing_guard{[&]() {
        m_perf_post_present.add(std::chrono::steady_clock::now() - post_present_start);
    }};

    if (m_graphics_memory != nullptr) {
        auto& hook = g_framework->get_d3d12_hook();

        auto device = hook->get_device();
        auto command_queue = hook->get_command_queue();

        m_graphics_memory->Commit(command_queue);
    }

    // Clear the (real) backbuffer if VR is enabled. Otherwise it will flicker and all sorts of nasty things.
    if (vr->is_hmd_active()) {
        clear_backbuffer();
    }
}

namespace dibr_config {
// Phase 2 control surface: env vars only (the ImGui panel is Phase 3).
//   UEVR_DIBR              off|0 (default) | 1|yoro|synth_right | yoro_left|synth_left | inverse | raymarch
//   UEVR_DIBR_DIVERGENCE   float, pixel-space eye separation (default 30)
//   UEVR_DIBR_CONVERGENCE  float 0..1, zero-parallax depth (default 0.5)
//   UEVR_DIBR_REVERSE_DEPTH float, default 1 (UE renders reversed-Z)
//   UEVR_DIBR_DEBUG_VIEW   float, kernel diagnostic view (1=depth heatmap, 2=disparity, ...)
//   UEVR_DIBR_RAYMARCH_STEPS float, raymarch kernel step count (default 32)
//   UEVR_DIBR_DEPTH_UV_AUTO 0 disables the automatic double-wide depth alignment
struct Config {
    bool enabled{false};
    DIBRSynthesis::Mode mode{DIBRSynthesis::Mode::Yoro};
    float yoro_reference_eye{0.0f}; // 0 = left reference (synthesize right)
    bool afw{false}; // alternate the reference eye every engine frame
    bool depth_uv_auto{true};
    std::optional<float> divergence{};
    std::optional<float> convergence{};
    std::optional<float> reverse_depth{};
    std::optional<float> debug_view{};
    std::optional<float> raymarch_steps{};
    std::optional<float> linearize{};
    std::optional<float> linearize_mode{};
    std::optional<float> linearize_near{};
    std::optional<float> linearize_far{};
};

std::optional<float> env_float(const char* name) {
    const char* v = std::getenv(name);
    if (v == nullptr || v[0] == '\0') {
        return std::nullopt;
    }
    char* end = nullptr;
    const float parsed = std::strtof(v, &end);
    if (end == v) {
        return std::nullopt;
    }
    return parsed;
}

const Config& get() {
    static const Config cfg = []() {
        Config c{};

        // Value overrides apply regardless of who enables DIBR (UI or env),
        // so scripted runs can pin single parameters while the UI drives.
        c.divergence = env_float("UEVR_DIBR_DIVERGENCE");
        c.convergence = env_float("UEVR_DIBR_CONVERGENCE");
        c.reverse_depth = env_float("UEVR_DIBR_REVERSE_DEPTH");
        c.debug_view = env_float("UEVR_DIBR_DEBUG_VIEW");
        c.raymarch_steps = env_float("UEVR_DIBR_RAYMARCH_STEPS");
        c.linearize = env_float("UEVR_DIBR_LINEARIZE");
        c.linearize_mode = env_float("UEVR_DIBR_LINEARIZE_MODE");
        c.linearize_near = env_float("UEVR_DIBR_LINEARIZE_NEAR");
        c.linearize_far = env_float("UEVR_DIBR_LINEARIZE_FAR");
        c.depth_uv_auto = env_float("UEVR_DIBR_DEPTH_UV_AUTO").value_or(1.0f) != 0.0f;

        const char* mode_env = std::getenv("UEVR_DIBR");
        const std::string mode = mode_env != nullptr ? mode_env : "";

        if (mode.empty() || mode == "0" || mode == "off") {
            return c;
        }

        c.enabled = true;
        if (mode == "1" || mode == "yoro" || mode == "synth_right") {
            c.mode = DIBRSynthesis::Mode::Yoro;
            c.yoro_reference_eye = 0.0f;
        } else if (mode == "yoro_left" || mode == "synth_left") {
            c.mode = DIBRSynthesis::Mode::Yoro;
            c.yoro_reference_eye = 1.0f;
        } else if (mode == "inverse") {
            c.mode = DIBRSynthesis::Mode::InverseWarp;
        } else if (mode == "raymarch") {
            c.mode = DIBRSynthesis::Mode::Raymarch;
        } else if (mode == "scatter" || mode == "yoro_scatter") {
            c.mode = DIBRSynthesis::Mode::YoroScatter;
            c.yoro_reference_eye = 0.0f;
        } else if (mode == "afw" || mode == "alternate") {
            // AFW: scatter pipeline with the reference eye flipping every
            // engine frame (PureDark-style Alternate Frame Warping cadence).
            c.mode = DIBRSynthesis::Mode::YoroScatter;
            c.afw = true;
        } else {
            SPDLOG_WARN("[DIBR] unrecognized UEVR_DIBR value '{}', defaulting to yoro (synthesize right eye)", mode);
        }

        SPDLOG_INFO("[DIBR] enabled via env: mode={} yoro_ref={} divergence={} convergence={} reverse_depth={}",
            mode, c.yoro_reference_eye, c.divergence.value_or(-1.0f), c.convergence.value_or(-1.0f),
            c.reverse_depth.value_or(1.0f));
        return c;
    }();
    return cfg;
}

// The kernel writes through a typed UAV, so the output must be a UAV-store
// capable format in the same copy family as the backbuffer (the result is
// copied straight back over it).
DXGI_FORMAT uav_store_format_for(ID3D12Device* device, DXGI_FORMAT backbuffer_format) {
    DXGI_FORMAT candidate = DXGI_FORMAT_UNKNOWN;
    switch (backbuffer_format) {
    case DXGI_FORMAT_R8G8B8A8_TYPELESS:
    case DXGI_FORMAT_R8G8B8A8_UNORM:
    case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
        candidate = DXGI_FORMAT_R8G8B8A8_UNORM;
        break;
    case DXGI_FORMAT_B8G8R8A8_TYPELESS:
    case DXGI_FORMAT_B8G8R8A8_UNORM:
    case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
        candidate = DXGI_FORMAT_B8G8R8A8_UNORM;
        break;
    case DXGI_FORMAT_R10G10B10A2_TYPELESS:
    case DXGI_FORMAT_R10G10B10A2_UNORM:
        candidate = DXGI_FORMAT_R10G10B10A2_UNORM;
        break;
    case DXGI_FORMAT_R16G16B16A16_TYPELESS:
    case DXGI_FORMAT_R16G16B16A16_FLOAT:
        candidate = DXGI_FORMAT_R16G16B16A16_FLOAT;
        break;
    default:
        return DXGI_FORMAT_UNKNOWN;
    }

    D3D12_FEATURE_DATA_FORMAT_SUPPORT support{candidate};
    if (FAILED(device->CheckFeatureSupport(D3D12_FEATURE_FORMAT_SUPPORT, &support, sizeof(support)))) {
        return DXGI_FORMAT_UNKNOWN;
    }
    if ((support.Support2 & D3D12_FORMAT_SUPPORT2_UAV_TYPED_STORE) == 0) {
        return DXGI_FORMAT_UNKNOWN;
    }
    return candidate;
}
} // namespace dibr_config

void D3D12Component::run_dibr_synthesis(VR* vr, ID3D12Resource* backbuffer, D3D12_RESOURCE_STATES scene_source_state, ID3D12Resource* scene_depth) {
    if (backbuffer == nullptr) {
        return;
    }

    auto* device = g_framework->get_d3d12_hook()->get_device();
    if (device == nullptr) {
        return;
    }

    const auto bb_desc = backbuffer->GetDesc();
    const auto eye_width = static_cast<uint32_t>(bb_desc.Width / 2);
    const auto eye_height = static_cast<uint32_t>(bb_desc.Height);
    if (eye_width == 0 || eye_height == 0) {
        return;
    }

    // Engine-side single-view state (DIBR synthesis or Mono): the stereo hook
    // renders ONLY the reference view (into the left half of the double-wide
    // backbuffer) while this is true, so every exit from this function must
    // leave the right half filled - the engine never wrote it. The cooldown
    // keeps the fill alive for the frames-in-flight window right after the
    // policy flips off (mode switched, pipeline failure, device reset), when
    // arriving backbuffers were still rendered with a single view.
    const bool single_view = vr->is_single_view_rendering_active();
    if (single_view) {
        m_dibr_single_view_cooldown = 3;
    } else if (m_dibr_single_view_cooldown > 0) {
        --m_dibr_single_view_cooldown;
    }
    const bool right_half_needs_fill = single_view || m_dibr_single_view_cooldown > 0;

    const auto barrier = [](ID3D12GraphicsCommandList* cmd_list, ID3D12Resource* res, D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after) {
        if (before == after) {
            return;
        }
        D3D12_RESOURCE_BARRIER b{};
        b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        b.Transition.pResource = res;
        b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        b.Transition.StateBefore = before;
        b.Transition.StateAfter = after;
        cmd_list->ResourceBarrier(1, &b);
    };

    // Intermediate single-eye source: the kernels treat the whole color SRV
    // as one source image, so one half of the SBS backbuffer is staged out.
    // Also reused as the bounce buffer for the mono right-half fill, since
    // D3D12 forbids same-subresource CopyTextureRegion.
    const auto ensure_source_tex = [&]() -> bool {
        if (m_dibr_source != nullptr && m_dibr_source_width == eye_width && m_dibr_source_height == eye_height &&
            m_dibr_source_format == bb_desc.Format) {
            return true;
        }
        m_dibr_source.Reset();

        D3D12_HEAP_PROPERTIES heap_props{};
        heap_props.Type = D3D12_HEAP_TYPE_DEFAULT;

        D3D12_RESOURCE_DESC desc{};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        desc.Width = eye_width;
        desc.Height = eye_height;
        desc.DepthOrArraySize = 1;
        desc.MipLevels = 1;
        desc.Format = bb_desc.Format;
        desc.SampleDesc.Count = 1;

        if (FAILED(device->CreateCommittedResource(&heap_props, D3D12_HEAP_FLAG_NONE, &desc,
                D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, nullptr, IID_PPV_ARGS(&m_dibr_source)))) {
            SPDLOG_ERROR_ONCE("[DIBR] failed to create {}x{} source staging texture", eye_width, eye_height);
            return false;
        }

        m_dibr_source->SetName(L"DIBR Source (single eye)");
        m_dibr_source_width = eye_width;
        m_dibr_source_height = eye_height;
        m_dibr_source_format = bb_desc.Format;
        SPDLOG_INFO("[DIBR] source staging texture {}x{} (fmt {})", eye_width, eye_height, static_cast<int>(bb_desc.Format));
        return true;
    };

    // Stage one half of the backbuffer (src_x = 0 or eye_width) into m_dibr_source.
    const auto stage_source = [&](ID3D12GraphicsCommandList* cmd_list, uint32_t src_x) {
        barrier(cmd_list, backbuffer, scene_source_state, D3D12_RESOURCE_STATE_COPY_SOURCE);
        barrier(cmd_list, m_dibr_source.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_DEST);

        D3D12_BOX src_box{};
        src_box.left = src_x;
        src_box.right = src_x + eye_width;
        src_box.bottom = eye_height;
        src_box.back = 1;

        D3D12_TEXTURE_COPY_LOCATION src_loc{};
        src_loc.pResource = backbuffer;
        src_loc.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        src_loc.SubresourceIndex = 0;
        D3D12_TEXTURE_COPY_LOCATION dst_loc{};
        dst_loc.pResource = m_dibr_source.Get();
        dst_loc.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        dst_loc.SubresourceIndex = 0;
        cmd_list->CopyTextureRegion(&dst_loc, 0, 0, 0, &src_loc, &src_box);

        barrier(cmd_list, m_dibr_source.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        barrier(cmd_list, backbuffer, D3D12_RESOURCE_STATE_COPY_SOURCE, scene_source_state);
    };

    // Copy the staged source over the backbuffer's right half (flat mono fill
    // for the eye the engine did not render).
    const auto copy_staged_to_right_half = [&](ID3D12GraphicsCommandList* cmd_list) {
        barrier(cmd_list, m_dibr_source.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_SOURCE);
        barrier(cmd_list, backbuffer, scene_source_state, D3D12_RESOURCE_STATE_COPY_DEST);

        D3D12_BOX src_box{};
        src_box.right = eye_width;
        src_box.bottom = eye_height;
        src_box.back = 1;

        D3D12_TEXTURE_COPY_LOCATION src_loc{};
        src_loc.pResource = m_dibr_source.Get();
        src_loc.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        src_loc.SubresourceIndex = 0;
        D3D12_TEXTURE_COPY_LOCATION dst_loc{};
        dst_loc.pResource = backbuffer;
        dst_loc.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        dst_loc.SubresourceIndex = 0;
        cmd_list->CopyTextureRegion(&dst_loc, eye_width, 0, 0, &src_loc, &src_box);

        barrier(cmd_list, backbuffer, D3D12_RESOURCE_STATE_COPY_DEST, scene_source_state);
        barrier(cmd_list, m_dibr_source.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    };

    // Mirror the rendered eye to the right half while the engine is (or may
    // still be) rendering a single view. This is the INTENDED steady state of
    // the Mono rendering method (expected=true) and the bail-out safety net
    // for DIBR when synthesis can't run this frame (expected=false).
    const auto fill_right_half_mono = [&](bool expected) {
        if (!right_half_needs_fill) {
            return;
        }
        if (!m_dibr_commands.ready() && !m_dibr_commands.setup(L"DIBR Synthesis")) {
            return;
        }
        std::scoped_lock _{m_dibr_commands.mtx};
        m_dibr_commands.wait(INFINITE);
        if (!ensure_source_tex()) {
            return;
        }
        auto* cmd_list = m_dibr_commands.cmd_list.Get();
        stage_source(cmd_list, 0);
        copy_staged_to_right_half(cmd_list);
        m_dibr_commands.has_commands = true;
        m_dibr_commands.execute();
        if (!expected) {
            SPDLOG_WARNING_EVERY_N_SEC(5, "[DIBR] single-view active but synthesis unavailable; mirrored the rendered eye");
        }
    };

    // Mono rendering method: gearmono-style baseline. The engine rendered one
    // centered union-frustum view into the left half; mirror it to the right
    // half and skip synthesis entirely.
    if (vr->is_mono_rendering_active()) {
        SPDLOG_INFO_ONCE("[DIBR] Mono rendering method active: mirroring the centered view to both eyes");
        fill_right_half_mono(true);
        return;
    }

    // Effective mode: the UEVR_DIBR env override (scripted testing) wins over
    // the persisted UI combo; otherwise the UI drives.
    const auto& env = dibr_config::get();
    auto mode = DIBRSynthesis::Mode::Yoro;
    float yoro_reference_eye = 0.0f;
    bool afw = false;

    if (env.enabled) {
        mode = env.mode;
        yoro_reference_eye = env.yoro_reference_eye;
        afw = env.afw;
    } else {
        // get_dibr_requested_mode also maps the "Synthetic Stereo (DIBR)"
        // rendering method with the panel combo on Off to YORO synth-right.
        switch (vr->get_dibr_requested_mode()) {
        case 1: // YORO, synthesize right
            mode = DIBRSynthesis::Mode::Yoro;
            yoro_reference_eye = 0.0f;
            break;
        case 2: // YORO, synthesize left
            mode = DIBRSynthesis::Mode::Yoro;
            yoro_reference_eye = 1.0f;
            break;
        case 3:
            mode = DIBRSynthesis::Mode::InverseWarp;
            break;
        case 4:
            mode = DIBRSynthesis::Mode::Raymarch;
            break;
        case 5: // YORO scatter (R2 redesign)
            mode = DIBRSynthesis::Mode::YoroScatter;
            yoro_reference_eye = 0.0f;
            break;
        case 6: // AFW: alternate frame warping (scatter + per-frame eye flip)
            mode = DIBRSynthesis::Mode::YoroScatter;
            afw = true;
            break;
        default:
            fill_right_half_mono(false); // engine may still be mid-transition out of single-view
            return; // Off
        }
    }

    // AFW: resolve the rendered (reference) eye of the frame being presented.
    // The authoritative source is the stereo hook's per-engine-frame view
    // ring (captured at view-calc time, looked up by m_render_frame_count -
    // the same counter UEVR's AFR submit path keys its eye off). Frame
    // parity is only the fallback for ring misses (engagement transitions).
    // UEVR_DIBR_AFW_PARITY=1 flips the fallback if a title's counter skews.
    uint32_t afw_presented_frame = 0;
    int32_t afw_eye_now = -1;
    glm::quat afw_rot_now{};
    glm::vec3 afw_loc_now{};
    glm::vec3 afw_other_loc_now{};
    if (afw) {
        static const uint32_t parity_flip = []() {
            const char* v = std::getenv("UEVR_DIBR_AFW_PARITY");
            return (v != nullptr && v[0] == '1') ? 1u : 0u;
        }();
        afw_presented_frame = (uint32_t)vr->m_render_frame_count;
        if (vr->m_fake_stereo_hook != nullptr) {
            afw_presented_frame -= (uint32_t)vr->m_fake_stereo_hook->get_frame_delay_compensation();
        }
        if (vr->get_afw_view(afw_presented_frame, afw_eye_now, afw_rot_now, afw_loc_now, afw_other_loc_now)) {
            // UEVR_DIBR_AFW_PARITY=1 also flips ring hits: if a title's
            // pipeline is one frame deeper than the counter chain assumes,
            // the ring hit is stale-but-valid and the eye association is
            // systematically inverted - this is the diagnostic lever for it.
            yoro_reference_eye = (float)(((uint32_t)afw_eye_now ^ parity_flip) & 1u);
        } else {
            afw_eye_now = -1;
            yoro_reference_eye = (float)((afw_presented_frame ^ parity_flip) & 1u);
        }
        SPDLOG_INFO_ONCE("[DIBR] AFW active: reference eye alternates per frame (this frame: {})",
            yoro_reference_eye > 0.5f ? "right" : "left");

        // Parity forensics partner of the stereo hook's offset trace.
        if (single_view) {
            static std::atomic<int> s_afw_trace{0};
            if (s_afw_trace.fetch_add(1, std::memory_order_relaxed) < 240) {
                SPDLOG_INFO("[DIBR][AFWTRACE] synth rframe={} mframe={} iframe={} ref={} ring={}",
                    (uint32_t)vr->m_render_frame_count, (uint32_t)vr->m_frame_count,
                    (uint32_t)vr->get_runtime()->internal_frame_count, yoro_reference_eye,
                    afw_eye_now >= 0 ? "hit" : "MISS");
            }
        }
    }

    if (m_dibr.failed()) {
        fill_right_half_mono(false);
        return;
    }

    // True AFR submits only the rendered eye's half each frame; the
    // synthesized half would never be consumed, so don't burn GPU on it.
    if (vr->m_rendering_method->value() == VR::RenderingMethod::ALTERNATING) {
        SPDLOG_WARN_ONCE("[DIBR] Alternating (AFR) rendering ignores the synthesized eye; use Native Stereo or Synchronized Sequential");
        fill_right_half_mono(false);
        return;
    }

    // 2D screen mode shows the game on a flat virtual screen; synthesized
    // parallax would never be visible.
    if (vr->m_2d_screen_mode->value()) {
        fill_right_half_mono(false);
        return;
    }

    if (vr->is_extreme_compatibility_mode_enabled()) {
        SPDLOG_WARN_ONCE("[DIBR] extreme compatibility mode submits the full backbuffer per eye; DIBR disabled");
        fill_right_half_mono(false);
        return;
    }

    // Kicks off the async kernel build on first use; no-op frames until ready.
    if (!m_dibr.ensure(device)) {
        SPDLOG_INFO_EVERY_N_SEC(5, "[DIBR] kernels still compiling; passing frame through untouched");
        fill_right_half_mono(false);
        return;
    }

    // DIBR needs the engine depth even when depth-layer submission is turned
    // off, so fall back to a direct pool lookup. The pool hook only
    // self-activates when depth submission is enabled - request activation
    // ourselves (idempotent; it installs on the next engine tick).
    Microsoft::WRL::ComPtr<ID3D12Resource> depth{scene_depth};
    if (depth == nullptr) {
        if (auto& rt_pool = vr->get_render_target_pool_hook(); rt_pool != nullptr) {
            rt_pool->activate();
            depth = rt_pool->get_texture<ID3D12Resource>(L"SceneDepthZ");
        }
    }

    // Final fallback: device-level DSV discovery (the pool hook cannot install
    // in every title - SN2's UE5 build defeats its signature scan). UE's scene
    // depth always matches the swapchain extent, double-wide included.
    if (depth == nullptr) {
        depth = dibr_depth_tracker::select_scene_depth(static_cast<uint32_t>(bb_desc.Width), eye_width, eye_height);
        if (depth != nullptr) {
            SPDLOG_INFO_ONCE("[DIBR] using DSV-discovered scene depth (render-target pool unavailable)");
        }
    }

    // Translucency forensics (UEVR_DIBR_BIND_CENSUS=1): per-present flush of
    // the ordered RTV/DSV bind window the command-list hooks collected.
    if (const auto census = dibr_depth_tracker::take_census_report(); !census.empty()) {
        SPDLOG_INFO("[DIBR] {}", census);
    }

    // Pre-translucency probe (UEVR_DIBR_PRETRANS_DUMP=1): scene-color copies
    // at every qualifying bind of an armed frame, dumped to %TEMP% as .ppm.
    if (const auto probe = dibr_depth_tracker::probe_flush(); !probe.empty()) {
        SPDLOG_INFO("[DIBR] pretrans probe:\n{}", probe);
    }

    if (depth == nullptr) {
        SPDLOG_WARNING_EVERY_N_SEC(5, "[DIBR] no scene depth available; skipping synthesis this frame (wanted full={} eye={}x{})",
            static_cast<uint32_t>(bb_desc.Width), eye_width, eye_height);
        SPDLOG_INFO_EVERY_N_SEC(10, "[DIBR] depth candidates on selection failure: {}", dibr_depth_tracker::describe_candidates());
        fill_right_half_mono(false);
        return;
    }

    // Selection diagnostics: a depth-target SHAPE switch is the prime suspect
    // for sudden depth-shaped distortion. Pointer-only changes are normal
    // (RDG ping-pongs SceneDepthZ between pooled textures every frame), so
    // those are only summarized periodically.
    {
        static uint64_t s_last_extent{0};
        static uint32_t s_pointer_switches{0};
        const auto dd = depth->GetDesc();
        const auto extent_key = (static_cast<uint64_t>(dd.Width) << 32) ^ (static_cast<uint64_t>(dd.Height) << 8) ^ static_cast<uint64_t>(dd.Format);
        if (extent_key != s_last_extent) {
            s_last_extent = extent_key;
            SPDLOG_INFO("[DIBR] depth target shape -> {}x{} fmt {}", dd.Width, dd.Height, static_cast<int>(dd.Format));
        }
        static ID3D12Resource* s_last_depth{nullptr};
        if (depth.Get() != s_last_depth) {
            s_last_depth = depth.Get();
            ++s_pointer_switches;
            SPDLOG_INFO_EVERY_N_SEC(30, "[DIBR] depth pointer switches so far: {} (per-frame pooled ping-pong is normal)", s_pointer_switches);
        }
        SPDLOG_INFO_EVERY_N_SEC(10, "[DIBR] depth candidates: {}", dibr_depth_tracker::describe_candidates());
    }

    const auto output_format = dibr_config::uav_store_format_for(device, bb_desc.Format);
    if (output_format == DXGI_FORMAT_UNKNOWN) {
        SPDLOG_ERROR_ONCE("[DIBR] backbuffer format {} has no UAV-store-capable equivalent on this device; DIBR disabled",
            static_cast<int>(bb_desc.Format));
        fill_right_half_mono(false);
        return;
    }

    if (!m_dibr_commands.ready() && !m_dibr_commands.setup(L"DIBR Synthesis")) {
        return;
    }

    // The fence wait must precede any resource recreation below: dropping the
    // staging/output textures while the previous frame's DIBR commands are
    // still in flight would free GPU-referenced memory.
    std::scoped_lock _{m_dibr_commands.mtx};
    m_dibr_commands.wait(INFINITE);
    auto* cmd_list = m_dibr_commands.cmd_list.Get();

    m_dibr.set_output_format(output_format);

    if (!ensure_source_tex()) {
        return;
    }

    // 1) Stage the reference eye's half of the backbuffer as the synthesis
    // source. While the engine renders (or may still be rendering) a single
    // view it always lands in the LEFT half regardless of which eye it
    // represents; in two-view mode the YORO reference eye owns its own half
    // (the right half when synthesizing the left eye).
    const bool reference_is_right = (mode == DIBRSynthesis::Mode::Yoro || mode == DIBRSynthesis::Mode::YoroScatter) &&
                                    yoro_reference_eye > 0.5f;
    const uint32_t source_x = (!right_half_needs_fill && reference_is_right) ? eye_width : 0;
    stage_source(cmd_list, source_x);

    // 2) Parameters: vrmod defaults -> persisted UI settings -> env overrides.
    // The UI divergence is tuned in pixels at a 1920-wide eye (the Depth3D /
    // vrmod convention); scale by the actual eye width so the ANGULAR
    // disparity stays constant across render resolutions. The env override
    // stays absolute for scripted reproducibility.
    const float divergence_scale = static_cast<float>(eye_width) / 1920.0f;
    DIBRStereoParams params{};
    params.mode_param0 = yoro_reference_eye;
    params.divergence = env.divergence.value_or(vr->m_dibr_divergence->value() * divergence_scale);
    params.convergence = env.convergence.value_or(vr->m_dibr_convergence->value());
    params.zpd_balance = vr->m_dibr_zpd_balance->value();
    params.reverse_depth = env.reverse_depth.value_or(vr->m_dibr_reverse_depth->value() ? 1.0f : 0.0f);
    params.depth_linearize_strength = env.linearize.value_or(vr->m_dibr_depth_linearize->value());
    params.depth_linearize_mode = env.linearize_mode.value_or(static_cast<float>(vr->m_dibr_depth_linearize_mode->value()));
    params.depth_linearize_near = env.linearize_near.value_or(vr->m_dibr_depth_linearize_near->value());
    params.depth_linearize_far = env.linearize_far.value_or(vr->m_dibr_depth_linearize_far->value());
    params.depth_gain = vr->m_dibr_depth_gain->value();
    params.depth_curve = vr->m_dibr_depth_curve->value();
    params.popout_limit = vr->m_dibr_popout_limit->value();
    params.edge_fill_mode = static_cast<float>(vr->m_dibr_edge_fill_mode->value());
    params.disocclusion_strength = vr->m_dibr_disocclusion_strength->value();
    params.edge_guard_strength = vr->m_dibr_edge_guard_strength->value();
    params.foreground_protect = vr->m_dibr_foreground_protect->value();
    params.range_smoothing = vr->m_dibr_range_smoothing->value();
    params.raymarch_steps = env.raymarch_steps.value_or(static_cast<float>(vr->m_dibr_raymarch_steps->value()));
    params.raymarch_foveation_strength = vr->m_dibr_foveation_strength->value();
    params.debug_view_mode = env.debug_view.value_or(static_cast<float>(vr->m_dibr_debug_view->value()));

    // --- True-matrix reprojection (R1 redesign) ---
    // Exact clip(source eye) -> clip(target eye) built from the runtime's
    // real per-eye projections and the IPD expressed in UE units, replacing
    // the screen-space divergence model in the YORO kernel. The divergence
    // slider becomes stereo strength: 30 = 100% of the true IPD.
    // UEVR_DIBR_REPROJ=0 falls back to the legacy model; UEVR_DIBR_REPROJ_SIGN
    // flips the eye-separation sign if a title's view convention differs.
    static const bool reproj_disabled = []() {
        const char* v = std::getenv("UEVR_DIBR_REPROJ");
        return v != nullptr && v[0] == '0';
    }();
    static const float reproj_sign = []() {
        const char* v = std::getenv("UEVR_DIBR_REPROJ_SIGN");
        return (v != nullptr && v[0] == '-') ? -1.0f : 1.0f;
    }();

    if ((mode == DIBRSynthesis::Mode::Yoro || mode == DIBRSynthesis::Mode::YoroScatter) && !reproj_disabled) {
        const auto proj_l = vr->get_projection_matrix(VRRuntime::Eye::LEFT);
        const auto proj_r = vr->get_projection_matrix(VRRuntime::Eye::RIGHT);
        const auto off_l = glm::vec3{vr->get_eye_offset(VRRuntime::Eye::LEFT)};
        const auto off_r = glm::vec3{vr->get_eye_offset(VRRuntime::Eye::RIGHT)};

        float strength = env.divergence.has_value()
            ? (*env.divergence / 30.0f)
            : (vr->m_dibr_divergence->value() / 30.0f);
        float baseline_sign = reproj_sign;
        if (afw) {
            // AFW: the warp must reproduce the engine's EXACT eye baseline -
            // the synthesized eye alternates with a REAL render of the same
            // eye every frame, so any baseline scale or sign deviation
            // becomes a two-position oscillation at half refresh. Divergence
            // and UEVR_DIBR_REPROJ_SIGN are taste knobs for the
            // always-synthesized modes only.
            strength = 1.0f;
            baseline_sign = 1.0f;
            SPDLOG_INFO_ONCE("[DIBR] AFW: warp baseline pinned to the true IPD (Divergence/REPROJ_SIGN ignored)");
        }
        // get_world_scale() matches the stereo hook's eye displacement
        // (world_to_meters * world_scale); omitting it desynchronizes the
        // warp baseline from the engine baseline whenever world scale != 1.
        const float ipd_ue = glm::length(off_r - off_l) * vr->get_world_to_meters() * vr->get_world_scale() * strength * baseline_sign;

        if (ipd_ue != 0.0f) {
            const bool ref_left = yoro_reference_eye < 0.5f;
            glm::mat4 proj_src = ref_left ? proj_l : proj_r;
            const auto& proj_dst = ref_left ? proj_r : proj_l;

            // Overscan: the engine rendered the lone view this much wider (the
            // stereo hook scales the projection's M[0][0]/M[2][0]); widen our
            // copy of the source projection identically so the reprojection
            // stays exact, and tell the kernels so the reference eye is
            // cropped back to its true FOV.
            const float overscan = vr->get_dibr_overscan_factor();
            if (overscan > 1.0f) {
                proj_src[0][0] /= overscan; // UE M[0][0]
                proj_src[2][0] /= overscan; // UE M[2][0]
            }
            params.overscan_x = overscan;

            // The target eye sits at +/-IPD along view-space X relative to the
            // source eye; world points shift the opposite way in its view.
            const float dx = ref_left ? -ipd_ue : ipd_ue;
            const glm::mat4 t = glm::translate(glm::mat4{1.0f}, glm::vec3{dx, 0.0f, 0.0f});
            const glm::mat4 m = proj_dst * t * glm::inverse(proj_src);
            const glm::mat4 ident{1.0f};

            std::memcpy(params.reproj_source_to_right, ref_left ? &m[0][0] : &ident[0][0], sizeof(params.reproj_source_to_right));
            std::memcpy(params.reproj_source_to_left, ref_left ? &ident[0][0] : &m[0][0], sizeof(params.reproj_source_to_left));
            params.reproj_enabled = 1.0f;

            // R3: camera-delta reprojection matrix for the temporal hole
            // fill - current target-eye clip -> previous frame's target-eye
            // clip, derived from the HMD pose delta expressed in UE view
            // axes (OpenXR z-back -> UE z-forward via the z-flip conjugate).
            // Engine-side motion (locomotion, animated cameras) is invisible
            // to this matrix; the fill kernel's depth validation rejects
            // history it can't explain, so it degrades to the scanline fill.
            if (vr->is_dibr_temporal_enabled() && mode == DIBRSynthesis::Mode::YoroScatter && afw) {
                // AFW: history is last frame's REAL render of the eye being
                // synthesized now. Build the reprojection from the hook's
                // captured per-frame eye cameras - the FULL camera delta
                // (engine-side camera motion + head motion + eye offset), so
                // history stays registered while the game camera drifts
                // (a pure HMD-pose delta can't see engine motion and the
                // depth validation rejects history whenever the camera
                // moves). No motion fade either: the delta is exact for
                // static world content; the fill's depth validation handles
                // animated content.
                int32_t prev_eye = -1;
                glm::quat prev_rot{};
                glm::vec3 prev_loc{};
                glm::vec3 prev_other_loc{};
                const int target_eye = ref_left ? 1 : 0;
                if (afw_eye_now >= 0 &&
                    vr->get_afw_view(afw_presented_frame - 1, prev_eye, prev_rot, prev_loc, prev_other_loc) &&
                    prev_eye == target_eye) {
                    // pose_now = the TARGET eye's camera this frame (the
                    // hook computed it through its own transform math - the
                    // record's other_location); pose_prev = the previous
                    // frame's RENDERED camera, which IS the same physical
                    // eye. Translations are remapped into the hook's quat
                    // family with the hook's own (improper) quat_converter
                    // quat - do not substitute the matrix, glm's mat->quat
                    // conversion of it is a different map. fz conjugation as
                    // in the proven mode-5 path.
                    const glm::quat qc{Matrix4x4f{
                        0, 0, -1, 0,
                        1, 0, 0, 0,
                        0, 1, 0, 0,
                        0, 0, 0, 1}};
                    const glm::quat qc_inv = glm::inverse(qc);

                    glm::mat4 pose_now{glm::normalize(afw_rot_now)};
                    pose_now[3] = glm::vec4{qc_inv * afw_other_loc_now, 1.0f};
                    glm::mat4 pose_prev{glm::normalize(prev_rot)};
                    pose_prev[3] = glm::vec4{qc_inv * prev_loc, 1.0f};

                    const glm::mat4 d_raw = glm::inverse(pose_prev) * pose_now;
                    const glm::mat4 fz = glm::scale(glm::mat4{1.0f}, glm::vec3{1.0f, 1.0f, -1.0f});
                    const glm::mat4 d_ue = fz * d_raw * fz;

                    // Forensics: the same-eye frame delta should be small and
                    // smooth (camera drift + head sway), never IPD-sized.
                    {
                        static std::atomic<int> s_afw_hist_trace{0};
                        if (s_afw_hist_trace.fetch_add(1, std::memory_order_relaxed) < 120) {
                            const float dtrans = glm::length(glm::vec3{d_raw[3]});
                            const float cos_half = std::clamp((d_raw[0][0] + d_raw[1][1] + d_raw[2][2] - 1.0f) * 0.5f, -1.0f, 1.0f);
                            SPDLOG_INFO("[DIBR][AFWTRACE] hist dtrans={:.4f} drot={:.3f}deg", dtrans, glm::degrees(std::acos(cos_half)));
                        }
                        // Motion trigger for the consecutive-frame dumper: the
                        // static-pose captures are provably stable; the
                        // reported flicker correlates with head motion, so
                        // capture exactly then.
                        const float dtrans = glm::length(glm::vec3{d_raw[3]});
                        const float cos_half = std::clamp((d_raw[0][0] + d_raw[1][1] + d_raw[2][2] - 1.0f) * 0.5f, -1.0f, 1.0f);
                        const float drot_deg = glm::degrees(std::acos(cos_half));
                        if (dtrans > 0.4f || drot_deg > 0.15f) {
                            m_afw_dump_motion_trigger.store(true, std::memory_order_relaxed);
                        }
                    }

                    // The history is the raw render, which carries the
                    // overscan-widened projection.
                    glm::mat4 prev_proj = proj_dst;
                    if (overscan > 1.0f) {
                        prev_proj[0][0] /= overscan;
                        prev_proj[2][0] /= overscan;
                    }
                    const glm::mat4 hist = prev_proj * d_ue * glm::inverse(proj_dst);
                    std::memcpy(params.reproj_target_to_prev, &hist[0][0], sizeof(params.reproj_target_to_prev));
                    // 2.0 = AFW level: the fill uses real-render history for
                    // holes AND the compose blends it into the whole
                    // synthesized eye (CombinedWarping-style consistency).
                    params.temporal_enabled = 2.0f;
                    params.temporal_blend = vr->get_dibr_temporal_blend();
                }
            } else if (vr->is_dibr_temporal_enabled() && mode == DIBRSynthesis::Mode::YoroScatter) {
                static glm::mat4 s_prev_pose{1.0f};
                static bool s_prev_valid{false};

                glm::mat4 pose{glm::normalize(glm::quat{vr->get_rotation(0)})};
                pose[3] = glm::vec4{glm::vec3{vr->get_position(0)} * vr->get_world_to_meters(), 1.0f};

                if (s_prev_valid) {
                    const glm::mat4 d_raw = glm::inverse(s_prev_pose) * pose;
                    const glm::mat4 fz = glm::scale(glm::mat4{1.0f}, glm::vec3{1.0f, 1.0f, -1.0f});
                    const glm::mat4 d_ue = fz * d_raw * fz;
                    const glm::mat4 hist = proj_dst * d_ue * glm::inverse(proj_dst);
                    std::memcpy(params.reproj_target_to_prev, &hist[0][0], sizeof(params.reproj_target_to_prev));
                    params.temporal_enabled = 1.0f;

                    // Adaptive EMA: full smoothing while the head is steady,
                    // fading to fresh scanline fill under fast motion (boil is
                    // motion-masked there; latched history would smear). Knee:
                    // ~1 cm or ~1 deg of pose change per frame zeroes the blend.
                    const float trans_ue = glm::length(glm::vec3{d_raw[3]}); // UE units (cm at wtm=100)
                    const float trans_cm = trans_ue * (100.0f / std::max(vr->get_world_to_meters(), 1.0f));
                    const float cos_half = std::clamp((d_raw[0][0] + d_raw[1][1] + d_raw[2][2] - 1.0f) * 0.5f, -1.0f, 1.0f);
                    const float rot_deg = glm::degrees(std::acos(cos_half));
                    const float motion = trans_cm + rot_deg;
                    params.temporal_blend = vr->get_dibr_temporal_blend() * std::clamp(1.0f - motion, 0.0f, 1.0f);
                }

                s_prev_pose = pose;
                s_prev_valid = true;
            }

            SPDLOG_INFO_ONCE("[DIBR] true-matrix reprojection active (ipd_ue={:.3f}, strength={:.2f}, sign={}, overscan={:.2f})",
                ipd_ue, strength, reproj_sign, overscan);
        }
    }

    // SceneDepthZ can cover the full double-wide render while the source is a
    // single eye; map output UVs onto the half of the depth texture that the
    // staged color half was rendered with.
    if (env.depth_uv_auto) {
        const auto depth_desc = depth->GetDesc();
        const float width_ratio = static_cast<float>(depth_desc.Width) / static_cast<float>(eye_width);
        if (width_ratio > 1.5f) {
            params.depth_uv_scale_x = 2.0f; // TransformDepthUv divides: uv.x / 2
            // anchor 1 = top-left half, 2 = bottom-right half (scale_y stays 1, so y is unaffected)
            params.depth_uv_anchor = source_x > 0 ? 2.0f : 1.0f;
        }
    }

    // Output stays at the source eye size: the submit path slices the
    // double-wide backbuffer at its half and copies eye-sized boxes into the
    // (equally grown) scene swapchains, so the packed pair must span the full
    // backbuffer. With overscan growth the compose maps each output pixel
    // through the true-FOV crop, so the grown eye carries the full native
    // detail of the crop region.
    params.out_width = eye_width;
    params.out_height = eye_height;

    // AFW: the synthesis pass stashes each frame's source render (color +
    // device depth) as the next frame's temporal-fill history - under
    // alternation that history IS the real render of the eye being
    // synthesized, so disocclusion holes fill with one-frame-old real pixels.
    m_dibr.set_alternate_history(afw);

    // 3) Synthesize the packed SBS pair (records into the same command list).
    auto* output = m_dibr.synthesize(device, cmd_list, mode,
        m_dibr_source.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
        depth.Get(), ENGINE_SRC_DEPTH,
        params);

    // 4) Copy the synthesized pair back over the backbuffer so every
    // downstream consumer (OpenXR eye copies, OpenVR submits, mirror) sees it.
    if (output != nullptr) {
        barrier(cmd_list, output, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_SOURCE);
        barrier(cmd_list, backbuffer, scene_source_state, D3D12_RESOURCE_STATE_COPY_DEST);

        D3D12_BOX full_out{};
        full_out.right = eye_width * 2;
        full_out.bottom = eye_height;
        full_out.back = 1;

        D3D12_TEXTURE_COPY_LOCATION out_loc{};
        out_loc.pResource = output;
        out_loc.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        out_loc.SubresourceIndex = 0;
        D3D12_TEXTURE_COPY_LOCATION bb_loc{};
        bb_loc.pResource = backbuffer;
        bb_loc.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        bb_loc.SubresourceIndex = 0;
        cmd_list->CopyTextureRegion(&bb_loc, 0, 0, 0, &out_loc, &full_out);

        barrier(cmd_list, backbuffer, D3D12_RESOURCE_STATE_COPY_DEST, scene_source_state);
        barrier(cmd_list, output, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_COMMON);

        // Arms single-view mode: from here on the stereo hook may drop the
        // engine's second view, knowing this pass can fill the other eye.
        vr->notify_dibr_synthesis_succeeded();

        SPDLOG_INFO_ONCE("[DIBR] first synthesized frame submitted (mode={}, {}x{} per eye)",
            static_cast<int>(mode), eye_width, eye_height);

        // AFW forensics (UEVR_DIBR_AFW_DUMP=1): grab 4 CONSECUTIVE presented
        // frames (both eyes, post-synthesis) so per-parity geometry can be
        // measured without slow camera sway confounding seconds-apart
        // samples. Saved to %TEMP%\uevr_afw_dump_<i>_ref<eye>_rf<frame>.ppm.
        static const bool afw_dump_enabled = []() {
            const char* v = std::getenv("UEVR_DIBR_AFW_DUMP");
            return v != nullptr && v[0] == '1';
        }();
        if (afw_dump_enabled && afw && single_view) {
            constexpr uint32_t kDumpCount = 4;
            static uint32_t s_captured = 0;
            static Microsoft::WRL::ComPtr<ID3D12Resource> s_readback[kDumpCount];
            static uint64_t s_row_pitch = 0;
            static float s_ref[kDumpCount];
            static uint32_t s_rframe[kDumpCount];
            static bool s_saved = false;
            // Armed by sustained head/camera motion (see the temporal block):
            // static-pose frames measure stable; the flicker reproduces under
            // motion, so capture exactly that condition.
            const bool armed = m_afw_dump_motion_trigger.load(std::memory_order_relaxed);

            if (armed && s_captured < kDumpCount) {
                const auto out_desc = output->GetDesc();
                D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp{};
                UINT64 total{};
                device->GetCopyableFootprints(&out_desc, 0, 1, 0, &fp, nullptr, nullptr, &total);
                s_row_pitch = fp.Footprint.RowPitch;

                auto& rb = s_readback[s_captured];
                if (rb == nullptr) {
                    D3D12_HEAP_PROPERTIES hp{};
                    hp.Type = D3D12_HEAP_TYPE_READBACK;
                    D3D12_RESOURCE_DESC bd{};
                    bd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
                    bd.Width = total;
                    bd.Height = 1;
                    bd.DepthOrArraySize = 1;
                    bd.MipLevels = 1;
                    bd.SampleDesc.Count = 1;
                    bd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
                    device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &bd,
                        D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&rb));
                }
                if (rb != nullptr) {
                    barrier(cmd_list, output, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_SOURCE);
                    D3D12_TEXTURE_COPY_LOCATION src_loc{};
                    src_loc.pResource = output;
                    src_loc.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
                    src_loc.SubresourceIndex = 0;
                    D3D12_TEXTURE_COPY_LOCATION dst_loc{};
                    dst_loc.pResource = rb.Get();
                    dst_loc.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
                    dst_loc.PlacedFootprint = fp;
                    cmd_list->CopyTextureRegion(&dst_loc, 0, 0, 0, &src_loc, nullptr);
                    barrier(cmd_list, output, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_COMMON);
                    s_ref[s_captured] = yoro_reference_eye;
                    s_rframe[s_captured] = afw_presented_frame;
                    ++s_captured;
                }
            } else if (s_captured == kDumpCount && !s_saved) {
                // The per-frame fence wait at the top of this function
                // guarantees the last capture's GPU copy completed.
                s_saved = true;
                const auto out_desc = output->GetDesc();
                const uint32_t w = (uint32_t)out_desc.Width;
                const uint32_t h = out_desc.Height;
                char temp_path[MAX_PATH]{};
                GetTempPathA(MAX_PATH, temp_path);
                for (uint32_t i = 0; i < kDumpCount; ++i) {
                    if (s_readback[i] == nullptr) {
                        continue;
                    }
                    uint8_t* data = nullptr;
                    if (FAILED(s_readback[i]->Map(0, nullptr, (void**)&data)) || data == nullptr) {
                        continue;
                    }
                    const auto path = fmt::format("{}uevr_afw_dump_{}_ref{}_rf{}.ppm",
                        temp_path, i, (int)(s_ref[i] + 0.5f), s_rframe[i]);
                    if (FILE* f = fopen(path.c_str(), "wb"); f != nullptr) {
                        fprintf(f, "P6\n%u %u\n255\n", w, h);
                        std::vector<uint8_t> row(w * 3);
                        for (uint32_t y = 0; y < h; ++y) {
                            const uint8_t* src_row = data + y * s_row_pitch;
                            for (uint32_t x = 0; x < w; ++x) {
                                // BGRA8 -> RGB (fmt 87 family)
                                row[x * 3 + 0] = src_row[x * 4 + 2];
                                row[x * 3 + 1] = src_row[x * 4 + 1];
                                row[x * 3 + 2] = src_row[x * 4 + 0];
                            }
                            fwrite(row.data(), 1, row.size(), f);
                        }
                        fclose(f);
                        SPDLOG_INFO("[DIBR][AFWDUMP] saved {}", path);
                    }
                    s_readback[i]->Unmap(0, nullptr);
                }
            }
        }
    } else {
        SPDLOG_WARNING_EVERY_N_SEC(5, "[DIBR] synthesize() returned null; frame passed through");
        if (right_half_needs_fill) {
            // The staged source is the rendered eye - mirror it flat so the
            // never-rendered half is not stale garbage.
            copy_staged_to_right_half(cmd_list);
        }
    }

    m_dibr_commands.has_commands = true;
    m_dibr_commands.execute();
}

void D3D12Component::on_reset(VR* vr) {
    m_force_reset = true;
    m_last_frame_timing_log = {};
    m_perf_on_frame.reset();
    m_perf_ui_copy.reset();
    m_perf_swapchain_copy.reset();
    m_perf_openxr_submit.reset();
    m_perf_spectator_mirror.reset();
    m_perf_post_present.reset();

    auto runtime = vr->get_runtime();

    for (auto& ctx : m_openvr.left_eye_tex) {
        ctx.reset();
    }

    for (auto& ctx : m_openvr.right_eye_tex) {
        ctx.reset();
    }

    for (auto& commands : m_generic_commands) {
        commands.reset();
    }

    for (auto& commands : m_game_tex_commands) {
        commands.reset();
    }

    for (auto& backbuffer : m_backbuffer_textures) {
        backbuffer.reset();
    }

    for (auto & screen : m_2d_screen_tex) {
        screen.reset();
    }

    m_openvr.ui_tex.reset();
    m_game_ui_tex.reset();
    m_game_tex.reset();
    m_scene_capture_tex.reset();
    m_shf_mono_scene_tex.reset();
    m_shf_mono_scene_commands.reset();

    // Order matters: the command context reset waits for in-flight DIBR GPU
    // work before the textures it references are released below.
    m_dibr_commands.reset();
    m_dibr.reset();
    m_dibr_source.Reset();
    m_dibr_source_width = 0;
    m_dibr_source_height = 0;
    m_dibr_source_format = DXGI_FORMAT_UNKNOWN;
    m_native_debug_dump_commands.reset();
    m_native_debug_dumped_backbuffer = false;
    m_native_debug_submit_count = 0;
    m_shf_mono_scene_width = 0;
    m_shf_mono_scene_height = 0;
    m_shf_mono_scene_format = DXGI_FORMAT_UNKNOWN;
    m_skip_spectator_view_for_volatile_external_rt = false;
    m_shf_scene_mode = ShfSceneMode::Unknown;
    m_backbuffer_batch.reset();
    m_game_batch.reset();
    m_ui_batch_alpha_invert.reset();
    m_graphics_memory.reset();

    if (runtime->is_openxr() && runtime->loaded) {
        m_openxr.wait_for_all_copies();

        auto& rt_pool = vr->get_render_target_pool_hook();
        ComPtr<ID3D12Resource> scene_depth_tex{rt_pool->get_texture<ID3D12Resource>(L"SceneDepthZ")};

        bool needs_depth_resize = false;

        if (scene_depth_tex != nullptr) {
            const auto desc = scene_depth_tex->GetDesc();
            needs_depth_resize = vr->m_openxr->needs_depth_resize(desc.Width, desc.Height);

            if (needs_depth_resize) {
                spdlog::info("[VR] SceneDepthZ needs resize ({}x{})", desc.Width, desc.Height);
            }
        }


        const auto [ui_width, ui_height] = get_ui_extent();
        uint32_t reasons = SWAPCHAIN_RECREATE_NONE;
        int32_t old_ui_width = 0;
        int32_t old_ui_height = 0;
        bool swapchains_empty = false;

        {
            std::scoped_lock _{vr->m_openxr->swapchain_mtx};
            swapchains_empty = vr->m_openxr->swapchains.empty();
            const auto ui_it = vr->m_openxr->swapchains.find((uint32_t)runtimes::OpenXR::SwapchainIndex::UI);

            if (ui_it != vr->m_openxr->swapchains.end()) {
                old_ui_width = ui_it->second.width;
                old_ui_height = ui_it->second.height;
            }
        }

        if (m_openxr.last_resolution[0] != vr->get_hmd_width() || m_openxr.last_resolution[1] != vr->get_hmd_height()) {
            reasons |= SWAPCHAIN_RECREATE_HMD_RESOLUTION;
        }

        if (swapchains_empty) {
            reasons |= SWAPCHAIN_RECREATE_EMPTY;
        } else if ((uint32_t)old_ui_width != ui_width || (uint32_t)old_ui_height != ui_height) {
            reasons |= SWAPCHAIN_RECREATE_UI_EXTENT;
        }

        if (m_last_afr_state != vr->is_using_afr()) {
            reasons |= SWAPCHAIN_RECREATE_AFR_STATE;
        }

        if (needs_depth_resize) {
            reasons |= SWAPCHAIN_RECREATE_DEPTH_EXTENT;
        }

        if (reasons != SWAPCHAIN_RECREATE_NONE) {
            uint32_t new_depth_width = 0;
            uint32_t new_depth_height = 0;

            if (scene_depth_tex != nullptr) {
                const auto desc = scene_depth_tex->GetDesc();
                new_depth_width = (uint32_t)desc.Width;
                new_depth_height = (uint32_t)desc.Height;
            }

            log_openxr_swapchain_recreate(vr, reasons, new_depth_width, new_depth_height);
            prepare_openxr_swapchain_recreate(vr, reasons);
            m_openxr.create_swapchains();
            m_last_afr_state = vr->is_using_afr();
        }

        // end the frame before something terrible happens
        //vr->m_openxr.synchronize_frame();
        //vr->m_openxr.begin_frame();
        //vr->m_openxr.end_frame();
    }

    m_prev_backbuffer.Reset();
    m_openvr.texture_counter = 0;
}

bool D3D12Component::setup() {
    SPDLOG_INFO_EVERY_N_SEC(1, "[VR] Setting up d3d12 textures...");

    auto vr = VR::get();
    on_reset(vr.get());
    
    m_prev_backbuffer.Reset();

    auto& hook = g_framework->get_d3d12_hook();

    auto device = hook->get_device();
    auto swapchain = hook->get_swap_chain();

    ComPtr<ID3D12Resource> backbuffer{};

    auto ue4_texture = vr->m_fake_stereo_hook->get_render_target_manager()->get_render_target();

    if (ue4_texture != nullptr) {
        backbuffer = (ID3D12Resource*)ue4_texture->get_native_resource();
    }

    ComPtr<ID3D12Resource> real_backbuffer{};
    if (FAILED(swapchain->GetBuffer(swapchain->GetCurrentBackBufferIndex(), IID_PPV_ARGS(&real_backbuffer)))) {
        spdlog::error("[VR] Failed to get real back buffer (D3D12).");
        return false;
    }

    if (vr->is_extreme_compatibility_mode_enabled()) {
        backbuffer = real_backbuffer;
    }

    if (backbuffer == nullptr) {
        SPDLOG_ERROR_EVERY_N_SEC(1, "[VR] Failed to get back buffer (D3D12).");
        return false;
    }

    if (m_graphics_memory == nullptr) {
        m_graphics_memory = std::make_unique<DirectX::DX12::GraphicsMemory>(device);
    }

    const auto real_backbuffer_desc = real_backbuffer->GetDesc();

    auto backbuffer_desc = backbuffer->GetDesc();

    spdlog::info("[VR] D3D12 Real backbuffer width: {}, height: {}, format: {}", real_backbuffer_desc.Width, real_backbuffer_desc.Height, (uint32_t)real_backbuffer_desc.Format);

    backbuffer_desc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    backbuffer_desc.Flags &= ~D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE;
    backbuffer_desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;

    if (!vr->is_extreme_compatibility_mode_enabled()) {
        backbuffer_desc.Width /= 2; // The texture we get from UE is both eyes combined. we will copy the regions later.
    }

    spdlog::info("[VR] D3D12 RT width: {}, height: {}, format: {}", backbuffer_desc.Width, backbuffer_desc.Height, (uint32_t)backbuffer_desc.Format);

    D3D12_HEAP_PROPERTIES heap_props{};
    heap_props.Type = D3D12_HEAP_TYPE_DEFAULT;
    heap_props.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    heap_props.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;

    if (vr->is_using_2d_screen()) {
        ensure_2d_screen_textures(device, backbuffer_desc);
    }

    if (vr->get_runtime()->is_openvr()) {
        for (auto& ctx : m_openvr.left_eye_tex) {
            if (FAILED(device->CreateCommittedResource(&heap_props, D3D12_HEAP_FLAG_NONE, &backbuffer_desc, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, nullptr,
                    IID_PPV_ARGS(&ctx.texture)))) {
                spdlog::error("[VR] Failed to create left eye texture.");
                return false;
            }

            ctx.texture->SetName(L"OpenVR Left Eye Texture");
            if (!ctx.commands.setup(L"OpenVR Left Eye")) {
                spdlog::error("[VR] Failed to setup left eye context.");
                return false;
            }
        }

        for (auto& ctx : m_openvr.right_eye_tex) {
            if (FAILED(device->CreateCommittedResource(&heap_props, D3D12_HEAP_FLAG_NONE, &backbuffer_desc, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, nullptr,
                    IID_PPV_ARGS(&ctx.texture)))) {
                spdlog::error("[VR] Failed to create right eye texture.");
                return false;
            }

            ctx.texture->SetName(L"OpenVR Right Eye Texture");
            if (!ctx.commands.setup(L"OpenVR Right Eye")) {
                spdlog::error("[VR] Failed to setup right eye context.");
                return false;
            }
        }

        // Set up the UI texture to match the engine-provided UI extent when available.
        auto ui_desc = backbuffer_desc;
        const auto [ui_width, ui_height] = get_ui_extent();
        ui_desc.Width = ui_width;
        ui_desc.Height = ui_height;

        ComPtr<ID3D12Resource> ui_tex{};
        if (FAILED(device->CreateCommittedResource(&heap_props, D3D12_HEAP_FLAG_NONE, &ui_desc, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, nullptr,
                IID_PPV_ARGS(&ui_tex)))) {
            spdlog::error("[VR] Failed to create UI texture.");
            return false;
        }

        ui_tex->SetName(L"OpenVR UI Texture");

        if (!m_openvr.ui_tex.setup(device, ui_tex.Get(), DXGI_FORMAT_B8G8R8A8_UNORM, DXGI_FORMAT_B8G8R8A8_UNORM, L"OpenVR UI")) {
            spdlog::error("[VR] Failed to setup OpenVR UI context.");
            return false;
        }
    }

    for (auto& commands : m_generic_commands) {
        if (!commands.setup(L"Generic commands")) {
            return false;
        }
    }

    if (!m_native_debug_dump_commands.setup(L"Native stereo debug dump commands")) {
        return false;
    }

    if (!vr->is_extreme_compatibility_mode_enabled()) {
        m_backbuffer_size[0] = backbuffer_desc.Width * 2;
    } else {
        m_backbuffer_size[0] = backbuffer_desc.Width;
    }

    m_backbuffer_size[1] = backbuffer_desc.Height;

    m_backbuffer_batch = setup_sprite_batch_pso(real_backbuffer_desc.Format);
    m_game_batch = setup_sprite_batch_pso(backbuffer_desc.Format);

    // Custom blend state to flip the alpha in-place of the UI texture without an intermediate render target
    {
        DirectX::SpriteBatchPipelineStateDescription invert_alpha_in_place_pd{DirectX::RenderTargetState{backbuffer_desc.Format, DXGI_FORMAT_UNKNOWN}};

        auto& bd = invert_alpha_in_place_pd.blendDesc;
        auto& bdrt = bd.RenderTarget[0];
        bdrt.BlendEnable = TRUE;

        bdrt.SrcBlend = D3D12_BLEND_ONE;
        bdrt.DestBlend = D3D12_BLEND_ZERO;
        bdrt.BlendOp = D3D12_BLEND_OP_ADD;

        bdrt.SrcBlendAlpha = D3D12_BLEND_BLEND_FACTOR;
        bdrt.DestBlendAlpha = D3D12_BLEND_INV_BLEND_FACTOR;
        bdrt.BlendOpAlpha = D3D12_BLEND_OP_ADD;
        bdrt.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

        m_ui_batch_alpha_invert = setup_sprite_batch_pso(
            backbuffer_desc.Format, 
            alpha_luminance_sprite_ps_SpritePixelShader, 
            alpha_luminance_sprite_ps_SpriteVertexShader, 
            invert_alpha_in_place_pd
        );
    }

    spdlog::info("[VR] d3d12 textures have been setup");
    m_force_reset = false;

    return true;
}

void D3D12Component::OpenXR::initialize(XrSessionCreateInfo& session_info) {
    std::scoped_lock _{this->mtx};

	auto& hook = g_framework->get_d3d12_hook();

    auto device = hook->get_device();
    auto command_queue = hook->get_command_queue();

    this->binding.device = device;
    this->binding.queue = command_queue;

    spdlog::info("[VR] Searching for xrGetD3D12GraphicsRequirementsKHR...");
    PFN_xrGetD3D12GraphicsRequirementsKHR fn = nullptr;
    xrGetInstanceProcAddr(VR::get()->m_openxr->instance, "xrGetD3D12GraphicsRequirementsKHR", (PFN_xrVoidFunction*)(&fn));

    XrGraphicsRequirementsD3D12KHR gr{XR_TYPE_GRAPHICS_REQUIREMENTS_D3D12_KHR};
    gr.adapterLuid = device->GetAdapterLuid();
    gr.minFeatureLevel = D3D_FEATURE_LEVEL_11_0;

    spdlog::info("[VR] Calling xrGetD3D12GraphicsRequirementsKHR");
    fn(VR::get()->m_openxr->instance, VR::get()->m_openxr->system, &gr);

    session_info.next = &this->binding;
}

std::optional<std::string> D3D12Component::OpenXR::create_swapchains() {
    std::scoped_lock _{this->mtx};

    spdlog::info("[VR] Creating OpenXR swapchains for D3D12");

    this->destroy_swapchains();
    
    auto& hook = g_framework->get_d3d12_hook();
    auto device = hook->get_device();
    auto swapchain = hook->get_swap_chain();

    ComPtr<ID3D12Resource> backbuffer{};

    auto vr = VR::get();
    bool has_actual_vr_backbuffer = false;

    if (vr != nullptr && vr->m_fake_stereo_hook != nullptr) {
        auto ue4_texture = vr->m_fake_stereo_hook->get_render_target_manager()->get_render_target();

        if (ue4_texture != nullptr) {
            backbuffer = (ID3D12Resource*)ue4_texture->get_native_resource();
            has_actual_vr_backbuffer = backbuffer != nullptr;
        }
    }
    
    // Get the existing backbuffer
    // so we can get the format and stuff.
    if (backbuffer == nullptr && FAILED(swapchain->GetBuffer(swapchain->GetCurrentBackBufferIndex(), IID_PPV_ARGS(&backbuffer)))) {
        spdlog::error("[VR] Failed to get back buffer.");
        return "Failed to get back buffer.";
    }

    D3D12_HEAP_PROPERTIES heap_props{};
    heap_props.Type = D3D12_HEAP_TYPE_DEFAULT;
    heap_props.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    heap_props.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;

    auto backbuffer_desc = backbuffer->GetDesc();
    auto& openxr = vr->m_openxr;

    this->contexts.clear();

    auto create_swapchain = [&](uint32_t i, const XrSwapchainCreateInfo& swapchain_create_info, const D3D12_RESOURCE_DESC& desc) -> std::optional<std::string> {
        // Create the swapchain.
        runtimes::OpenXR::Swapchain swapchain{};
        swapchain.width = swapchain_create_info.width;
        swapchain.height = swapchain_create_info.height;

        if (xrCreateSwapchain(openxr->session, &swapchain_create_info, &swapchain.handle) != XR_SUCCESS) {
            spdlog::error("[VR] D3D12: Failed to create swapchain.");
            return "Failed to create swapchain.";
        }

        vr->m_openxr->swapchains[i] = swapchain;
        vr->m_openxr->cache_swapchain_dimensions(i, swapchain.width, swapchain.height);

        uint32_t image_count{};
        auto result = xrEnumerateSwapchainImages(swapchain.handle, 0, &image_count, nullptr);

        if (result != XR_SUCCESS) {
            spdlog::error("[VR] Failed to enumerate swapchain images.");
            return "Failed to enumerate swapchain images.";
        }

        SPDLOG_INFO("[VR] Runtime wants {} images for swapchain {}", image_count, i);

        auto& ctx = this->contexts[i];

        ctx.textures.clear();
        ctx.textures.resize(image_count);
        ctx.texture_contexts.clear();
        ctx.texture_contexts.resize(image_count);

        for (uint32_t j = 0; j < image_count; ++j) {
            ctx.textures[j] = {XR_TYPE_SWAPCHAIN_IMAGE_D3D12_KHR};
            ctx.texture_contexts[j] = std::make_unique<d3d12::TextureContext>();
            ctx.texture_contexts[j]->commands.setup((std::wstring{L"OpenXR commands "} + std::to_wstring(i) + L" " + std::to_wstring(j)).c_str());
        }

        result = xrEnumerateSwapchainImages(swapchain.handle, image_count, &image_count, (XrSwapchainImageBaseHeader*)&ctx.textures[0]);
        
        if (result != XR_SUCCESS) {
            spdlog::error("[VR] Failed to enumerate swapchain images after texture creation.");
            return "Failed to enumerate swapchain images after texture creation.";
        }

        for (uint32_t j = 0; j < image_count; ++j) {
            ctx.textures[j].texture->AddRef();
            const auto ref_count = ctx.textures[j].texture->Release();

            spdlog::info("[VR] AFTER Swapchain texture {} {} ref count: {}", i, j, ref_count);
        }

        if (swapchain_create_info.createFlags & XR_SWAPCHAIN_CREATE_STATIC_IMAGE_BIT) {
            for (uint32_t j = 0; j < image_count; ++j) {
                XrSwapchainImageAcquireInfo acquire_info{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
                XrSwapchainImageWaitInfo wait_info{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
                wait_info.timeout = XR_INFINITE_DURATION;
                XrSwapchainImageReleaseInfo release_info{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};

                uint32_t index{};
                xrAcquireSwapchainImage(swapchain.handle, &acquire_info, &index);
                xrWaitSwapchainImage(swapchain.handle, &wait_info);

                auto& texture_ctx = ctx.texture_contexts[index];
                texture_ctx->texture = ctx.textures[index].texture;

                // Depth stencil textures don't need an RTV.
                if ((desc.Flags & D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL) == 0) {
                    if (ctx.texture_contexts[index]->create_rtv(device, (DXGI_FORMAT)swapchain_create_info.format)) {
                        const float clear_color[4] = {0.0f, 0.0f, 0.0f, 0.0f};
                        texture_ctx->commands.clear_rtv(ctx.textures[index].texture, texture_ctx->get_rtv(), clear_color, D3D12_RESOURCE_STATE_RENDER_TARGET);
                        texture_ctx->commands.execute();
                        texture_ctx->commands.wait(100);
                    } else {
                        spdlog::error("[VR] Failed to create RTV for swapchain image {}.", index);
                    }
                }

                texture_ctx->texture.Reset();
                texture_ctx->rtv_heap.reset();

                xrReleaseSwapchainImage(swapchain.handle, &release_info);
            }
        }

        return std::nullopt;
    };

    const auto double_wide_multiple = vr->is_using_afr() ? 1 : 2;
    // DIBR overscan growth: the scene swapchains must match the (possibly
    // grown) render target eye size, because the submit path copies eye-sized
    // boxes sliced from the double-wide backbuffer.
    const auto scene_eye_width = vr->get_dibr_render_eye_width();

    XrSwapchainCreateInfo standard_swapchain_create_info{XR_TYPE_SWAPCHAIN_CREATE_INFO};
    standard_swapchain_create_info.arraySize = 1;
    standard_swapchain_create_info.format = DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
    standard_swapchain_create_info.width = scene_eye_width * double_wide_multiple;
    standard_swapchain_create_info.height = vr->get_hmd_height();
    standard_swapchain_create_info.mipCount = 1;
    standard_swapchain_create_info.faceCount = 1;
    standard_swapchain_create_info.sampleCount = backbuffer_desc.SampleDesc.Count;
    standard_swapchain_create_info.usageFlags = XR_SWAPCHAIN_USAGE_SAMPLED_BIT | XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT | XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT;

    auto hmd_desc = backbuffer_desc;
    hmd_desc.Width = scene_eye_width * double_wide_multiple;
    hmd_desc.Height = vr->get_hmd_height();
    hmd_desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;

    hmd_desc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    hmd_desc.Flags &= ~D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE;

    // Above is outdated, we will just use a double wide texture
    if (!vr->is_using_afr()) {
        spdlog::info("[VR] Creating double wide swapchain for eyes");
        spdlog::info("[VR] Width: {}", vr->get_hmd_width() * 2);
        spdlog::info("[VR] Height: {}", vr->get_hmd_height());

        if (auto err = create_swapchain((uint32_t)runtimes::OpenXR::SwapchainIndex::DOUBLE_WIDE, standard_swapchain_create_info, hmd_desc)) {
            return err;
        }

        if (vr->is_native_stereo_fix_enabled() && !vr->is_native_stereo_fix_same_pass_enabled()) {
            auto native_stereo_array_create_info = standard_swapchain_create_info;
            auto native_stereo_array_desc = hmd_desc;

            native_stereo_array_create_info.width = scene_eye_width;
            native_stereo_array_create_info.arraySize = 2;
            native_stereo_array_desc.Width = scene_eye_width;
            native_stereo_array_desc.DepthOrArraySize = 2;

            spdlog::info("[VR] Creating native stereo texture array swapchain");
            spdlog::info("[VR] Width: {}", scene_eye_width);
            spdlog::info("[VR] Height: {}", vr->get_hmd_height());
            spdlog::info("[VR] Array size: 2");
            if (auto err = create_swapchain((uint32_t)runtimes::OpenXR::SwapchainIndex::NATIVE_STEREO_ARRAY, native_stereo_array_create_info, native_stereo_array_desc)) {
                return err;
            }
        }
    } else {
        spdlog::info("[VR] Creating AFR swapchain for eyes");
        spdlog::info("[VR] Width: {}", vr->get_hmd_width());
        spdlog::info("[VR] Height: {}", vr->get_hmd_height());

        spdlog::info("[VR] Creating AFR left eye swapchain");
        if (auto err = create_swapchain((uint32_t)runtimes::OpenXR::SwapchainIndex::AFR_LEFT_EYE, standard_swapchain_create_info, hmd_desc)) {
            return err;
        }

        spdlog::info("[VR] Creating AFR right eye swapchain");
        if (auto err = create_swapchain((uint32_t)runtimes::OpenXR::SwapchainIndex::AFR_RIGHT_EYE, standard_swapchain_create_info, hmd_desc)) {
            return err;
        }
    }

    auto virtual_desktop_dummy_desc = backbuffer_desc;
    auto virtual_desktop_dummy_swapchain_create_info = standard_swapchain_create_info;

    virtual_desktop_dummy_desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
    virtual_desktop_dummy_desc.Width = 4;
    virtual_desktop_dummy_desc.Height = 4;
    virtual_desktop_dummy_swapchain_create_info.width = 4;
    virtual_desktop_dummy_swapchain_create_info.height = 4;
    virtual_desktop_dummy_swapchain_create_info.createFlags = XR_SWAPCHAIN_CREATE_STATIC_IMAGE_BIT; // so we dont need to acquire/release/wait

    // The virtual desktop dummy texture
    if (auto err = create_swapchain((uint32_t)runtimes::OpenXR::SwapchainIndex::DUMMY_VIRTUAL_DESKTOP, virtual_desktop_dummy_swapchain_create_info, virtual_desktop_dummy_desc)) {
        return err;
    }

    const auto [ui_width, ui_height] = get_ui_extent();
    spdlog::info("[VR] OpenXR UI extent: {}x{}", ui_width, ui_height);

    auto desktop_rt_swapchain_create_info = standard_swapchain_create_info;
    desktop_rt_swapchain_create_info.format = DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
    desktop_rt_swapchain_create_info.width = ui_width;
    desktop_rt_swapchain_create_info.height = ui_height;

    auto desktop_rt_desc = backbuffer_desc;
    desktop_rt_desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
    desktop_rt_desc.Width = ui_width;
    desktop_rt_desc.Height = ui_height;

    desktop_rt_desc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    desktop_rt_desc.Flags &= ~D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE;

    // The UI texture
    if (auto err = create_swapchain((uint32_t)runtimes::OpenXR::SwapchainIndex::UI, desktop_rt_swapchain_create_info, desktop_rt_desc)) {
        return err;
    }

    if (auto err = create_swapchain((uint32_t)runtimes::OpenXR::SwapchainIndex::UI_RIGHT, desktop_rt_swapchain_create_info, desktop_rt_desc)) {
        return err;
    }

    if (auto err = create_swapchain((uint32_t)runtimes::OpenXR::SwapchainIndex::FRAMEWORK_UI, desktop_rt_swapchain_create_info, desktop_rt_desc)) {
        return err;
    }

    // Depth textures
    if (vr->get_openxr_runtime()->is_depth_allowed()) {
        // Even when using AFR, the depth tex is always the size of a double wide.
        // That's kind of unfortunate in terms of how many copies we have to do but whatever.
        auto depth_swapchain_create_info = standard_swapchain_create_info;
        depth_swapchain_create_info.format = DXGI_FORMAT_D32_FLOAT_S8X24_UINT;
        depth_swapchain_create_info.createFlags = 0;
        depth_swapchain_create_info.usageFlags = XR_SWAPCHAIN_USAGE_SAMPLED_BIT | XR_SWAPCHAIN_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | XR_SWAPCHAIN_USAGE_MUTABLE_FORMAT_BIT;
        depth_swapchain_create_info.width = vr->get_hmd_width() * 2;
        depth_swapchain_create_info.height = vr->get_hmd_height();

        auto depth_desc = backbuffer_desc;
        depth_desc.Format = DXGI_FORMAT_R32G8X24_TYPELESS;
        //depth_desc.BindFlags = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE;
        depth_desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
        depth_desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        depth_desc.DepthOrArraySize = 1;

        depth_desc.Flags &= ~D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE;

        depth_desc.Width = vr->get_hmd_width() * 2;
        depth_desc.Height = vr->get_hmd_height();

        auto& rt_pool = vr->get_render_target_pool_hook();
        auto depth_tex = rt_pool->get_texture<ID3D12Resource>(L"SceneDepthZ");

        if (depth_tex != nullptr) {
            this->made_depth_with_null_defaults = false;
            depth_desc = depth_tex->GetDesc();

            if (depth_desc.Format == DXGI_FORMAT_R24G8_TYPELESS) {
                depth_swapchain_create_info.format = DXGI_FORMAT_D24_UNORM_S8_UINT;
            }

            spdlog::info("[VR] Depth texture size: {}x{}", depth_desc.Width, depth_desc.Height);
            spdlog::info("[VR] Depth texture format: {}", (uint32_t)depth_desc.Format);
            spdlog::info("[VR] Depth texture flags: {}", (uint32_t)depth_desc.Flags);

            if (depth_desc.Width > hmd_desc.Width || depth_desc.Height > hmd_desc.Height) {
                spdlog::info("[VR] Depth texture is larger than the HMD");
                //depth_desc.Width = hmd_desc.Width;
                //depth_desc.Height = hmd_desc.Height;
            }

            depth_desc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
            depth_desc.Flags &= ~D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE;

            depth_swapchain_create_info.width = depth_desc.Width;
            depth_swapchain_create_info.height = depth_desc.Height;
        } else {
            this->made_depth_with_null_defaults = true;
            spdlog::error("[VR] Depth texture is null! Using default values");
            depth_desc.Width = vr->get_hmd_width() * 2;
            depth_desc.Height = vr->get_hmd_height();
        }

        if (!vr->is_using_afr()) {
            spdlog::info("[VR] Creating double wide depth swapchain");
            if (auto err = create_swapchain((uint32_t)runtimes::OpenXR::SwapchainIndex::DEPTH, depth_swapchain_create_info, depth_desc)) {
                return err;
            }
        } else {
            spdlog::info("[VR] Creating AFR depth swapchain");
            spdlog::info("[VR] Creating AFR left eye depth swapchain");
            if (auto err = create_swapchain((uint32_t)runtimes::OpenXR::SwapchainIndex::AFR_DEPTH_LEFT_EYE, depth_swapchain_create_info, depth_desc)) {
                return err;
            }

            spdlog::info("[VR] Creating AFR right eye depth swapchain");
            if (auto err = create_swapchain((uint32_t)runtimes::OpenXR::SwapchainIndex::AFR_DEPTH_RIGHT_EYE, depth_swapchain_create_info, depth_desc)) {
                return err;
            }
        }
    }

    this->last_resolution = {vr->get_hmd_width(), vr->get_hmd_height()};

    return std::nullopt;
}

void D3D12Component::OpenXR::destroy_swapchains() {
    std::scoped_lock _{this->mtx};
    auto vr = VR::get();

    if (vr != nullptr && vr->m_openxr != nullptr) {
        vr->m_openxr->clear_cached_swapchain_dimensions();
    }

    if (this->contexts.empty()) {
        return;
    }

    if (vr == nullptr || vr->m_openxr == nullptr) {
        return;
    }
    
    std::scoped_lock __{vr->m_openxr->swapchain_mtx};

    spdlog::info("[VR] Destroying swapchains.");

    this->wait_for_all_copies();

    for (auto& it : this->contexts) {
        auto& ctx = it.second;
        const auto i = it.first;

        //ctx.texture_contexts.clear();
        for (auto& texture_context : ctx.texture_contexts) {
            if (texture_context != nullptr) {
                texture_context->reset();
            }
        }

        ctx.texture_contexts.clear();

        std::vector<ID3D12Resource*> needs_release{};

        for (auto& tex : ctx.textures) {
            if (tex.texture != nullptr) {
                tex.texture->AddRef();
                needs_release.push_back(tex.texture);
            }
        }

        if (vr->m_openxr->swapchains.contains(i)) {
            const auto result = xrDestroySwapchain(vr->m_openxr->swapchains[i].handle);

            if (result != XR_SUCCESS) {
                spdlog::error("[VR] Failed to destroy swapchain {}.", i);
            } else {
                spdlog::info("[VR] Destroyed swapchain {}.", i);
            }
        } else {
            spdlog::error("[VR] Swapchain {} does not exist.", i);
        }

        for (auto& tex : needs_release) {
            if (const auto ref_count = tex->Release(); ref_count != 0) {
                spdlog::info("[VR] Memory leak detected in swapchain texture {} ({} refs)", i, ref_count);
            } else {
                spdlog::info("[VR] Swapchain texture {} released.", i);
            }
        }
        
        ctx.textures.clear();
    }

    this->contexts.clear();
    vr->m_openxr->swapchains.clear();
}

void D3D12Component::OpenXR::copy(
    uint32_t swapchain_idx, 
    ID3D12Resource* resource, 
    std::optional<std::function<void(d3d12::CommandContext&, ID3D12Resource*)>> pre_commands, 
    std::optional<std::function<void(d3d12::CommandContext&)>> additional_commands, 
    D3D12_RESOURCE_STATES src_state, 
    D3D12_BOX* src_box,
    uint32_t dst_subresource) 
{
    std::scoped_lock _{this->mtx};

    auto vr = VR::get();

    if (vr->m_openxr->frame_state.shouldRender != XR_TRUE) {
        return;
    }

    if (!vr->m_openxr->frame_began) {
        if (vr->get_synchronize_stage() != VR::SynchronizeStage::VERY_LATE) {
            spdlog::error("[VR] OpenXR: Frame not begun when trying to copy.");
            return;
        }
    }

    if (!this->contexts.contains(swapchain_idx)) {
        spdlog::error("[VR] OpenXR: Trying to copy to swapchain {} but it doesn't exist.", swapchain_idx);
        return;
    }

    if (!vr->m_openxr->swapchains.contains(swapchain_idx)) {
        spdlog::error("[VR] OpenXR: Trying to copy to swapchain {} but it doesn't exist.", swapchain_idx);
        return;
    }

    if (this->contexts[swapchain_idx].num_textures_acquired > 0) {
        spdlog::info("[VR] Already acquired textures for swapchain {}?", swapchain_idx);
    }

    const auto& swapchain = vr->m_openxr->swapchains[swapchain_idx];
    auto& ctx = this->contexts[swapchain_idx];

    XrSwapchainImageAcquireInfo acquire_info{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};

    uint32_t texture_index{};
    auto result = xrAcquireSwapchainImage(swapchain.handle, &acquire_info, &texture_index);

    if (result == XR_ERROR_RUNTIME_FAILURE) {
        spdlog::error("[VR] xrAcquireSwapchainImage failed: {}", vr->m_openxr->get_result_string(result));
        spdlog::info("[VR] Attempting to correct...");

        for (auto& texture_ctx : ctx.texture_contexts) {
            texture_ctx->commands.reset();
        }

        texture_index = 0;
        result = xrAcquireSwapchainImage(swapchain.handle, &acquire_info, &texture_index);
    }


    if (result != XR_SUCCESS) {
        spdlog::error("[VR] xrAcquireSwapchainImage failed: {}", vr->m_openxr->get_result_string(result));
    } else {
        ctx.num_textures_acquired++;
        ctx.last_acquired_texture = texture_index;

        XrSwapchainImageWaitInfo wait_info{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
        //wait_info.timeout = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::seconds(1)).count();
        wait_info.timeout = XR_INFINITE_DURATION;
        result = xrWaitSwapchainImage(swapchain.handle, &wait_info);

        if (result != XR_SUCCESS) {
            spdlog::error("[VR] xrWaitSwapchainImage failed: {}", vr->m_openxr->get_result_string(result));
        } else {
            auto& texture_ctx = ctx.texture_contexts[texture_index];
            texture_ctx->commands.wait(INFINITE);

            if (pre_commands) {
                (*pre_commands)(texture_ctx->commands, ctx.textures[texture_index].texture);
            }

            // We may simply just want to render to the render target directly
            // hence, a null resource is allowed.
            if (resource != nullptr) {
                if (src_box == nullptr) {
                    const auto is_depth = swapchain_idx == (uint32_t)runtimes::OpenXR::SwapchainIndex::DEPTH || 
                                        swapchain_idx == (uint32_t)runtimes::OpenXR::SwapchainIndex::AFR_DEPTH_LEFT_EYE || 
                                        swapchain_idx == (uint32_t)runtimes::OpenXR::SwapchainIndex::AFR_DEPTH_RIGHT_EYE;
                    const auto dst_state = is_depth ? D3D12_RESOURCE_STATE_DEPTH_WRITE : D3D12_RESOURCE_STATE_RENDER_TARGET;

                    texture_ctx->commands.copy(
                        resource, 
                        ctx.textures[texture_index].texture, 
                        src_state, 
                        dst_state);
                } else {
                    texture_ctx->commands.copy_region_to_subresource(
                        resource, 
                        ctx.textures[texture_index].texture, src_box,
                        dst_subresource,
                        src_state, 
                        D3D12_RESOURCE_STATE_RENDER_TARGET);
                }
            }

            if (additional_commands) {
                (*additional_commands)(texture_ctx->commands);
            }

            texture_ctx->commands.execute();

            XrSwapchainImageReleaseInfo release_info{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
            auto result = xrReleaseSwapchainImage(swapchain.handle, &release_info);

            // SteamVR shenanigans.
            if (result == XR_ERROR_RUNTIME_FAILURE) {
                spdlog::error("[VR] xrReleaseSwapchainImage failed: {}", vr->m_openxr->get_result_string(result));
                spdlog::info("[VR] Attempting to correct...");

                result = xrWaitSwapchainImage(swapchain.handle, &wait_info);

                if (result != XR_SUCCESS) {
                    spdlog::error("[VR] xrWaitSwapchainImage failed: {}", vr->m_openxr->get_result_string(result));
                }

                for (auto& texture_ctx : ctx.texture_contexts) {
                    texture_ctx->commands.wait(INFINITE);
                }

                result = xrReleaseSwapchainImage(swapchain.handle, &release_info);
            }

            if (result != XR_SUCCESS) {
                spdlog::error("[VR] xrReleaseSwapchainImage failed: {}", vr->m_openxr->get_result_string(result));
                return;
            }

            ctx.num_textures_acquired--;
            ctx.last_acquired_frame = vr->get_frame_count();
            ctx.ever_acquired = true;
        }
    }
}
} // namespace vrmod
