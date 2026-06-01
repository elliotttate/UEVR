// Sn2PsoBytecodeDumper.hpp
//
// Dump every PSO's shader bytecode to disk at CreateGraphicsPipelineState /
// CreateComputePipelineState time. The dumped .dxbc files can be reflected
// with dxc to get authoritative root-signature parameter → semantic mapping
// (which root holds View, Material, Pass, etc.) instead of inferring from
// observed binding patterns.
//
// CONFIG
//   UEVR_SN2_PSO_BYTECODE_DIR=C:\tmp\dxbc   — output directory; missing = disabled
//
// FILES
//   {dir}\{crc8hex}.dxbc           — pixel shader bytecode (if present)
//   {dir}\{crc8hex}.vs.dxbc        — vertex shader (if present)
//   {dir}\{crc8hex}.cs.dxbc        — compute shader (if present)
//   {dir}\{crc8hex}.meta.json      — root-sig hash + RTV/DSV formats
//   {dir}\{suffix}_0x{crc}.reflect.json
//                                  — reflection: bound-resource table (t#/u#/b#/s#
//                                    with type/dimension/return-type), input/output
//                                    signature, and DXC disassembly (#7, first-seen
//                                    CRC only, CPU-only off the render thread)
//
// Skips duplicate (same CRC) shaders.

#pragma once

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <mutex>
#include <sstream>
#include <string>
#include <unordered_set>
#include <vector>

#include <d3d12.h>

#include <nlohmann/json.hpp>

#include "render/ShaderCompiler.hpp"

namespace sn2_pso_bytecode_dumper {

inline bool env_enabled() {
    static const bool e = []() {
        return GetEnvironmentVariableW(L"UEVR_SN2_PSO_BYTECODE_DIR", nullptr, 0) > 0;
    }();
    return e;
}

// #7: reflection-JSON sidecar emission.
//
// Self-contained so it can live entirely in this header: own first-seen-CRC
// dedup set, own gating, and it reuses render::inspect_shader_bytecode (the
// existing reflection + disassembly entry in ShaderCompiler.cpp) rather than
// reimplementing reflection. Off the render thread / CPU-only is the caller's
// responsibility — the work here is pure CPU (no GPU, no D3D calls).
//
// Output directory: UEVR_SN2_PSO_BYTECODE_DIR (same as the DXBC dump); if unset,
// falls back to UEVR_SN2_RDOC_TAGS-controlled capture dir via the same env var.
inline std::string reflect_output_dir() {
    static const std::string s = []() -> std::string {
        char buf[2048]{};
        auto len = GetEnvironmentVariableA("UEVR_SN2_PSO_BYTECODE_DIR", buf, sizeof(buf));
        if (len > 0 && len < sizeof(buf)) {
            return std::string{buf, len};
        }
        return std::string{};
    }();
    return s;
}

// Gated by the DXBC-dump env if present; otherwise by UEVR_SN2_RDOC_TAGS.
inline bool reflect_enabled() {
    static const bool e = []() {
        if (GetEnvironmentVariableW(L"UEVR_SN2_PSO_BYTECODE_DIR", nullptr, 0) > 0) {
            return true;
        }
        return GetEnvironmentVariableW(L"UEVR_SN2_RDOC_TAGS", nullptr, 0) > 0;
    }();
    return e;
}

// Register class prefix for the bound-resource table: cbv->b, sampler->s,
// uav->u, everything else (srv/tbuffer/byteaddress/structured)->t.
inline char reflect_register_prefix(const std::string& type) {
    if (type == "cbv") return 'b';
    if (type == "sampler") return 's';
    if (type == "uav") return 'u';
    return 't';
}

// Emit {dir}\{suffix}_0x{crc}.reflect.json for the FIRST-SEEN crc/suffix pair.
// Returns silently on: disabled, empty bytecode, crc==0, or already-seen.
inline void dump_reflection_json(const void* bytecode, size_t size,
                                 uint32_t crc, const char* suffix) {
    if (!reflect_enabled() || bytecode == nullptr || size == 0 || crc == 0) {
        return;
    }
    if (reflect_output_dir().empty()) {
        return;
    }

    // First-seen-CRC dedup (keyed like the DXBC dump: crc ^ hash(suffix)).
    static std::mutex mu;
    static std::unordered_set<uint32_t> seen;
    const uint32_t key = crc ^ static_cast<uint32_t>(std::hash<std::string>{}(suffix));
    {
        std::scoped_lock _{mu};
        if (!seen.insert(key).second) {
            return;
        }
    }

    // Reuse the existing reflection + disassembly entry — do not reimplement.
    const auto inspection = render::inspect_shader_bytecode(bytecode, size, /*disassemble=*/true);

    nlohmann::json doc;
    doc["crc"] = crc;
    {
        char hexbuf[16];
        std::snprintf(hexbuf, sizeof(hexbuf), "0x%08x", crc);
        doc["crc_hex"] = hexbuf;
    }
    doc["stage"] = suffix;
    doc["bytecode_size"] = inspection.bytecode_size;
    doc["container_kind"] = inspection.container_kind;
    doc["container_hash"] = inspection.container_hash;
    doc["compiler"] = inspection.compiler;
    if (!inspection.error.empty()) {
        doc["error"] = inspection.error;
    }

    const auto& refl = inspection.reflection;
    doc["reflection_ok"] = refl.ok;
    if (!refl.error.empty()) {
        doc["reflection_error"] = refl.error;
    }
    doc["creator"] = refl.creator;
    doc["instruction_count"] = refl.instruction_count;

    // Bound-resource register table (t#/u#/b#/s#).
    auto& resources = doc["bound_resources"] = nlohmann::json::array();
    for (const auto& r : refl.bound_resources) {
        const char prefix = reflect_register_prefix(r.type);
        std::ostringstream reg;
        reg << prefix << r.bind_point;
        if (r.bind_count > 1) {
            reg << '[' << r.bind_count << ']';
        }
        resources.push_back({
            {"name", r.name},
            {"register", reg.str()},
            {"register_class", std::string(1, prefix)},
            {"type", r.type},
            {"return_type", r.return_type},
            {"dimension", r.dimension},
            {"bind_point", r.bind_point},
            {"bind_count", r.bind_count},
            {"space", r.space},
            {"flags", r.flags},
        });
    }

    auto emit_signature = [](const std::vector<render::ShaderReflectionSignatureParamInfo>& params) {
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& p : params) {
            arr.push_back({
                {"semantic_name", p.semantic_name},
                {"semantic_index", p.semantic_index},
                {"register", p.register_index},
                {"system_value", p.system_value},
                {"component_type", p.component_type},
                {"mask", p.mask},
                {"read_write_mask", p.read_write_mask},
                {"stream", p.stream},
            });
        }
        return arr;
    };
    doc["input_signature"] = emit_signature(refl.input_parameters);
    doc["output_signature"] = emit_signature(refl.output_parameters);

    // Constant-buffer layouts are useful for naming b# bindings.
    auto& cbuffers = doc["constant_buffers"] = nlohmann::json::array();
    for (const auto& cb : refl.constant_buffers) {
        nlohmann::json vars = nlohmann::json::array();
        for (const auto& v : cb.variables) {
            vars.push_back({
                {"name", v.name},
                {"offset", v.start_offset},
                {"size", v.size},
                {"type_name", v.type_name},
                {"type_class", v.type_class},
                {"type_kind", v.type_kind},
                {"rows", v.rows},
                {"columns", v.columns},
                {"elements", v.elements},
            });
        }
        cbuffers.push_back({
            {"name", cb.name},
            {"type", cb.type},
            {"size", cb.size},
            {"variables", std::move(vars)},
        });
    }

    doc["disassembly"] = inspection.disassembly;

    char path[1024];
    std::snprintf(path, sizeof(path), "%s\\%s_0x%08x.reflect.json",
                  reflect_output_dir().c_str(), suffix, crc);
    try {
        std::ofstream f{path, std::ios::binary | std::ios::trunc};
        if (f.good()) {
            const auto text = doc.dump(2);
            f.write(text.data(), static_cast<std::streamsize>(text.size()));
        }
    } catch (...) {}
}

// -------------------------------------------------------------------------
// Sentinel returned by ps_crc_for_pso() when the PSO is in the registry
// but has NO pixel shader (depth-only / compute). Distinguishes
// "hash failed / PSO not seen" (0) from "genuinely no PS" (NOPS_CRC).
// -------------------------------------------------------------------------
static constexpr uint32_t NOPS_CRC = 0xFFFFFFFFu;

// -------------------------------------------------------------------------
// PSO-create observers.  Each overload is called from the matching
// D3D12Hook hook function immediately after the real D3D12 call succeeds.
//
// The two-arg "hint" overloads are legacy call-sites that already computed
// the CRC via ShaderOverrideRegistry; they are kept for backward compat
// and only do the DXBC dump, not the registry insertion (no pso pointer).
// The three-arg overloads with ID3D12PipelineState* populate the CRC
// registry AND do the DXBC dump.
// -------------------------------------------------------------------------

// CreateGraphicsPipelineState path.
void on_create_graphics_pso(const D3D12_GRAPHICS_PIPELINE_STATE_DESC* desc,
                            ID3D12PipelineState* pso,
                            uintptr_t create_rva = 0);
// Legacy overload: DXBC dump only (no registry update).
void on_create_graphics_pso(const D3D12_GRAPHICS_PIPELINE_STATE_DESC* desc,
                            uint32_t ps_crc_hint, uint32_t vs_crc_hint);

// CreateComputePipelineState path.
void on_create_compute_pso(const D3D12_COMPUTE_PIPELINE_STATE_DESC* desc,
                           ID3D12PipelineState* pso,
                           uintptr_t create_rva = 0);
// Legacy overload: DXBC dump only.
void on_create_compute_pso(const D3D12_COMPUTE_PIPELINE_STATE_DESC* desc,
                           uint32_t cs_crc_hint);

// ID3D12Device2::CreatePipelineState (stream form) — primary UE5 path.
// This is the call that was previously unregistered; it now walks the
// subobject stream itself so ShaderHunter does not need to be active.
void on_create_pso_stream(const D3D12_PIPELINE_STATE_STREAM_DESC* desc,
                          ID3D12PipelineState* pso,
                          uintptr_t create_rva = 0);
// Legacy overload kept for forward compat (DXBC dump only, no registry).
void on_create_pso_stream(const void* ps_bytecode, size_t ps_size, uint32_t ps_crc,
                          const void* vs_bytecode, size_t vs_size, uint32_t vs_crc,
                          const void* cs_bytecode, size_t cs_size, uint32_t cs_crc);

// -------------------------------------------------------------------------
// PSO CRC lookups — O(1), mutex-protected, safe to call from the hot path.
//
//   Return 0       : PSO not in this registry (not yet seen, or gate off).
//   Return NOPS_CRC: PSO IS in registry but has no pixel shader.
//   Other value    : the actual CRC32 of that stage's DXIL/DXBC bytecode.
//
// Usage in the per-draw marker (D3D12Hook.cpp):
//
//   uint32_t ps_crc = reg.d3d12_pso_pixel_crc32(pso_ptr);
//   if (ps_crc == 0)
//       ps_crc = sn2_pso_bytecode_dumper::ps_crc_for_pso(
//                    static_cast<ID3D12PipelineState*>(s.current_pso));
//   // ps_crc == NOPS_CRC -> label "no-PS", not "0x00000000"
// -------------------------------------------------------------------------
uint32_t ps_crc_for_pso(ID3D12PipelineState* pso);
uint32_t vs_crc_for_pso(ID3D12PipelineState* pso);
uint32_t cs_crc_for_pso(ID3D12PipelineState* pso);

// -------------------------------------------------------------------------
// on_unload — call once at DLL teardown to flush the final sidecar JSON.
// -------------------------------------------------------------------------
void on_unload();

}  // namespace sn2_pso_bytecode_dumper
