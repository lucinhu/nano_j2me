# Optional synchronous parallel raster work

`nj_port.parallel_for` is an optional fork/join callback, appended to the port
vtable. Rebuild ports with the updated header. A NULL callback retains serial
rendering; a callback may also execute every partition serially. It must finish
all partitions before returning and must not run VM operations from workers.

Large visible M3G triangles can split pixel work into two horizontal bands.
The VM still owns geometry, object fields, allocation and garbage collection.
Both bands seed and increment their row accumulators in the original serial
order. Skipping directly to the second band's first row would change floating
rounding, texture boundaries and depth ties. Submission order is preserved:
all bands finish before the next triangle, including blended and coplanar draws.
Feedback textures that overlap their target remain serial. Small triangles
remain serial to avoid a handoff that costs more than the pixel work.

The concurrent regression reuses public-API target fixtures and compares all
color and depth bytes with the serial run. It covers RGB565/RGB888/ARGB8888,
padded strides, translated/empty clips, viewport clipping, alpha tests, blend
modes and depth ties. A feedback fixture checks that overlapping texture storage
does not dispatch concurrent work. ASan/UBSan passed: 234 actual pthread forks,
117,504,000 identical color/depth bytes.

The Korvo adapter uses a persistent priority-3 worker on Core 1, below audio and
MIDI priorities, while Core 0 also renders its own band. A 300-round hardware
fork/join test checked odd PSRAM boundaries, canaries and completion: zero
mismatches, caller Core 0 and worker Core 1. Four fixed rendering workloads
matched their Java checksums and final native/LCD CRCs; two on/off repetitions
returned all PSRAM and measured roughly 1.46x-1.51x throughput on large triangles.
Full JBenchmark/game and firmware records live in the parent Korvo repository.

Opaque Graphics fills may also use `nj_port.gfx_fill_rect`. The generic gfx
initializer clears this hook; the MIDP Graphics loader borrows it from the port.
Only translated, clipped, opaque, non-XOR rectangles are offered. Colors are
already packed for the target format. A callback must finish synchronously or
return zero without writing to retain the software fallback. It must not call
back into the VM or retain the surface. Rebuild consumers of `nj_gfx` and
`nj_port` with the updated headers.

The new standalone fill regression compares complete guarded buffers across
RGB565/RGB888/ARGB8888, clip/translation, declined acceleration, alpha and XOR.
The Korvo port splits large fills by rows and uses 128-bit PIE stores only on
Core 1, with scalar alignment prefixes/tails and a scalar Core 0 band. A real
128-round PSRAM test with odd strides, unaligned starts and canaries completed
with zero mismatches (110,720 SIMD blocks). Measured fill performance and the
board's independent SIMD/parallel settings live in the parent report.
