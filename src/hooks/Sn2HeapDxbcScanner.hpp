// Sn2HeapDxbcScanner.hpp
//
// Mid-run DXBC bytecode dumper. Scans the LIVE process's readable memory
// regions for the "DXBC" 4-byte magic, validates the container header,
// computes CRC32 of the full container, matches against a configurable
// target list, and dumps every matching blob to disk.
//
// SOLVES: PSOs created BEFORE UEVR injected (~+45s into SN2 startup) can't
// be caught via the CreateGraphicsPipelineState hook. But the game still
// holds references to the original DXBC bytecode blobs in its heap — UE5
// keeps FShaderCode bytes alive for shader hot-reload and debug paths.
// We scan for "DXBC" magic, validate, CRC, match.
//
// CONFIG
//   UEVR_SN2_HEAP_SCAN_DXBC=1                 enable + on first Present, lazy
//                                              init the trigger watcher
//   UEVR_SN2_HEAP_SCAN_TRIGGER_FILE=path      file watched in Present; create
//                                              it to fire one scan (default
//                                              C:\tmp\scan_dxbc.txt)
//   UEVR_SN2_HEAP_SCAN_OUT_DIR=path           output dir for ps_0x<crc>.dxbc
//                                              files (default C:\tmp\dxbc_scan)
//   UEVR_SN2_HEAP_SCAN_CRCS=0x37558de4,...    comma list of target CRCs.
//                                              If empty, dump EVERY unique
//                                              DXBC blob found in the heap.
//
// USAGE
//   while game running:
//     echo > C:\tmp\scan_dxbc.txt
//   → UEVR scans process memory next frame
//   → matching DXBC blobs land in C:\tmp\dxbc_scan\ps_0x<crc>.dxbc
//
// HOW IT WORKS
//   1. VirtualQuery iterates committed memory regions in our address space.
//   2. For each readable region, slide a 4-byte window looking for "DXBC".
//   3. At each hit, read 32 bytes: validate magic + version + size_in_range.
//   4. Read the full container by its embedded size field.
//   5. crc32_ieee(bytes) → match against env list or dump-all mode.
//   6. Write to disk, dedupe by CRC.
//
// FALSE POSITIVES
//   DXBC ASCII can appear in code/data unrelated to shaders. We mitigate
//   by checking: bytes 20-23 == 0x00000001 (version), 32 ≤ size ≤ 256 KB,
//   chunk_count > 0 and < 32, all chunk offsets in-range. Real DXBCs pass;
//   random matches almost never do.

#pragma once

#include <cstdint>
#include <string>

namespace sn2_heap_dxbc_scanner {

bool env_enabled();

// Called from Present hook every frame. Polls trigger file; on detect, runs
// one full process-wide DXBC scan + dump pass. Safe to call before init —
// no-ops if env_enabled() returns false.
void on_present(uint64_t frame_count);

// Programmatic trigger (no file). Runs one scan synchronously on the calling
// thread. Returns the number of unique DXBC blobs written.
size_t scan_now();

}  // namespace sn2_heap_dxbc_scanner
