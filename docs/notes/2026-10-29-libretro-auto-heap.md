# libretro: automatic Java heap

The core option `nano_j2me_heap` offered 16/32/64/128 MB and defaulted to 64,
one fixed answer for a handheld and a desktop.  It now offers **auto** as its
first value and default: the heap is a sixteenth of the host's RAM, floored at
32 MB (below that a 3D title dies in its own loader) and capped at 128 MB, on
the same 8 MB grid the fixed values use.  A host that cannot report its RAM
gets the floor.  The fixed sizes stay selectable, and an explicit value behaves
as before (clamped to 8–512 MB).

The size is resolved once per content load, next to the other core options, and
logged:

```
[core][info] [nano_j2me] heap: automatic, 128 MB for this host
```

so a user's "it ran out of memory" report carries the number the VM got.

## Implementation

* `lr_host_ram_mb()` in [nj_port_libretro.c](../../src/port/libretro/nj_port_libretro.c)
  reads `sysconf(_SC_PHYS_PAGES) * sysconf(_SC_PAGESIZE)` on POSIX and
  `GlobalMemoryStatusEx().ullTotalPhys` on Windows; zero means unknown.
* `lr_auto_heap_mb()` applies the clamp/grid, and `lr_read_config()` resolves
  `auto` (or an empty option value) once, before `nj_vm_create()`.
* The option's registered default is now `auto`; RetroArch's core options show
  "Auto (by RAM)" first.

## Verification

`tools/check_libretro_frontend.sh` grows a heap probe that runs the mock
frontend against the real core:

1. a zero-frame load reads the registered option — the default must be `auto`
   and `auto` must be among the values (exact list is printed in the log);
2. a two-frame load must log the resolved size, so an option that advertises
   `auto` but never resolves it cannot pass.

Result on this machine: `11 passed, 0 failed, 0 skipped`, with the probe
reporting `PASS heap option defaults to auto`,
`PASS heap option offers auto (values auto,16,32,64,128)` and
`PASS automatic heap resolves at load and is logged`; the core built from a
clean worktree at the commit and logged
`heap: automatic, 128 MB for this host` (this host's RAM is above the cap).
The run also exercises the fixed-size path indirectly: `--set
nano_j2me_heap=16` continues to be accepted by the same parser.

## Limits

* Automatic is a host-RAM heuristic, not a measurement of the running MIDlet:
  a very large title on a small host can still run out, and the fixed values
  (up to 128 MB) remain the answer.
* The value is resolved at load; changing the option takes effect on the next
  content load, like every other core option.
