# DXIL/DXBC backend boundary

UEVR now has three shader-editing tiers:

1. **Container/text tier**: parse DXBC/DXIL containers, disassemble DXIL, apply JSON transform rules, reassemble, validate, and sign through DXC. This is the runtime-safe path used by shader override manifests.
2. **SM5 token tier**: `dxil-patch dxbc-tokens` dumps raw `SHEX`/`SHDR` token streams, and `dxil-patch dxbc-token-patch` can replace specific 32-bit tokens. This is intentionally low-level and should be used only for tightly scoped SM5 experiments after external validation.
3. **Semantic LLVM/DxilModule tier**: `uevr-dxil-semantic-pass` lives in the DirectXShaderCompiler tree under `tools/clang/tools/uevr-dxil-semantic-pass`. UEVR can call it from `dxil_semantic_transform`, `left_dxil_semantic_transform`, or `right_dxil_semantic_transform` manifests. It loads a full DXIL container, restores `DxilModule` side data, applies semantic LLVM IR rules, serializes a new container, validates/signs it with DXC, then feeds the output through the same PSO replacement path.

The practical first choice remains tier 1. It gives signed in-engine DXIL edits without carrying a full LLVM toolchain inside UEVR. Tier 3 is now available for shaders that need true IR edits, but the tool is intentionally external so UEVRBackend.dll does not carry LLVM/DXC internals at runtime.
