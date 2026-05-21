# DXIL transform templates

These JSON files are starting points for UEVR's practical DXIL text transform path and the optional DXC-internals semantic pass:

1. Capture/inspect a shader with `uevr_render_shader_bytecode` or `dxil-patch disasm`.
2. Use the reflection/root-binding output to fill the template's handle names, register indices, or descriptor-table ranges.
3. Apply with `dxil-patch transform <input.dxbc> <template.json> -o <output.dxbc> --report <report.json>` or reference the JSON from a shader override manifest as `dxil_transform`, `left_transform`, or `right_transform`.
4. For true LLVM/DxilModule edits, reference the same rule shape as `dxil_semantic_transform`, `left_dxil_semantic_transform`, or `right_dxil_semantic_transform` and set `UEVR_DXIL_SEMANTIC_TOOL` if `uevr-dxil-semantic-pass.exe` is not beside `UEVRBackend.dll`.

The runtime text tier remains the lowest-friction path. The semantic tier exists for the same UE/SN2 stereo patterns when a real IR rewrite is safer than disassembly text replacement: camera/matrix cbuffer reads, LUT SRV redirects, screen-space UV constants, and view/array-slice plumbing.
