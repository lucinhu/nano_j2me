/*
 * nano_j2me - nj_cheat.c
 *
 * Arena-offset cheat codes.
 *
 * The runtime has no flat address space: every Java object lives in one
 * non-moving arena that the libretro port already exposes to the frontend as
 * SYSTEM_RAM (see nj_vm_memory_region in nj_heap.c).  A libretro cheat code
 * is normally an (address, value) pair over a flat map, so this core uses a
 * dedicated form that names the same window:
 *
 *     <arena-offset>:<hex bytes>      e.g.  @1A2B0:05  or  7ff000:0000FFFF
 *
 * The value is re-applied at the start of every frame, so a game that writes
 * its own state back cannot cancel the patch.  This is a debugging tool and
 * not a save channel: the write bypasses the Java memory model and a wrong
 * offset can corrupt the running program.
 *
 * The table lives in the VM, and the same engine serves the headless runner
 * (--cheat/--cheat-file), the libretro port (retro_cheat_set) and any
 * frontend that can hand the runtime a cheat file.
 */
#include "nj_internal.h"

#include <stdio.h>
#include <string.h>

static int nj_cheat_hex_digit(int c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/*
 * Parse one code.  Returns 1 and fills *out on success, 0 on a malformed
 * code.  Offsets and byte values are hex; whitespace may surround the
 * separator and the bytes.
 */
static int nj_cheat_parse(const char *code, nj_cheat_patch *out)
{
    const char *p = code;
    nj_u4 offset = 0;
    unsigned len = 0;
    int nibble;

    if (!code || !out) return 0;
    memset(out, 0, sizeof(*out));

    while (*p == ' ' || *p == '\t') p++;
    if (*p == '@') p++;
    if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) p += 2;

    nibble = nj_cheat_hex_digit((unsigned char)*p);
    if (nibble < 0) return 0;
    while ((nibble = nj_cheat_hex_digit((unsigned char)*p)) >= 0) {
        if (offset > (0xFFFFFFFFu / 16u)) return 0;   /* cannot address it */
        offset = offset * 16u + (nj_u4)nibble;
        p++;
    }
    while (*p == ' ' || *p == '\t') p++;
    if (*p != ':' && *p != '=') return 0;
    p++;

    while (*p) {
        int hi, lo;
        while (*p == ' ' || *p == '\t') p++;
        if (!*p) break;
        hi = nj_cheat_hex_digit((unsigned char)p[0]);
        lo = hi < 0 ? -1 : nj_cheat_hex_digit((unsigned char)p[1]);
        if (lo < 0) return 0;
        if (len >= NJ_CHEAT_MAX_BYTES) return 0;
        out->value[len++] = (unsigned char)(hi * 16 + lo);
        p += 2;
    }
    if (len == 0) return 0;
    out->offset = offset;
    out->len = len;
    out->enabled = 1;
    return 1;
}

int nj_vm_cheat_add(nj_vm *vm, const char *code)
{
    nj_cheat_patch patch;
    int slot;

    if (!vm || !code) return NJ_ERR;
    if (!nj_cheat_parse(code, &patch)) return NJ_ERR_FORMAT;

    /* Reuse a disabled slot before growing the list. */
    for (slot = 0; slot < vm->cheats.count; slot++) {
        if (!vm->cheats.patch[slot].enabled) break;
    }
    if (slot >= NJ_CHEAT_MAX_CODES) return NJ_ERR_STATE;
    vm->cheats.patch[slot] = patch;
    if (slot == vm->cheats.count) vm->cheats.count++;
    return slot;
}

int nj_vm_cheat_disable(nj_vm *vm, int slot)
{
    if (!vm || slot < 0 || slot >= vm->cheats.count) return NJ_ERR;
    vm->cheats.patch[slot].enabled = 0;
    vm->cheats.patch[slot].len = 0;
    return NJ_OK;
}

void nj_vm_cheat_clear(nj_vm *vm)
{
    if (!vm) return;
    memset(&vm->cheats, 0, sizeof(vm->cheats));
}

/*
 * Read one code per line from a text file.  Blank lines and lines whose first
 * non-space character is '#' are ignored; a malformed code line is reported
 * at level 1 and skipped.  Returns the number of codes added, or a negative
 * nj_status when the file cannot be opened.
 */
int nj_vm_cheat_load(nj_vm *vm, const char *path)
{
    FILE *f;
    char line[256];
    int added = 0;
    int lineno = 0;

    if (!vm || !path) return NJ_ERR;
    f = fopen(path, "r");
    if (!f) return NJ_ERR_FORMAT;
    while (fgets(line, sizeof(line), f)) {
        char *s = line;
        size_t len;
        int slot;

        lineno++;
        /* A line too long for the buffer is truncated by fgets; still parse. */
        len = strlen(s);
        while (len > 0 && (s[len - 1] == '\n' || s[len - 1] == '\r')) s[--len] = '\0';
        while (*s == ' ' || *s == '\t') s++;
        if (*s == '\0' || *s == '#') continue;
        slot = nj_vm_cheat_add(vm, s);
        if (slot < 0) {
            nj_logf(vm, 1, "cheat file %s:%d: ignoring code '%s'\n",
                    path, lineno, s);
            continue;
        }
        added++;
        nj_logf(vm, 2, "cheat file %s:%d: slot %d, %s\n", path, lineno, slot, s);
    }
    fclose(f);
    return added;
}

/* Re-apply every enabled patch over the arena.  Called once per frame. */
void nj_cheat_apply(nj_vm *vm)
{
    void *data = NULL;
    nj_size size = 0;
    int i;

    if (!vm || vm->cheats.count <= 0) return;
    if (nj_vm_memory_region(vm, &data, &size) != NJ_OK) return;
    if (!data || size == 0) return;
    for (i = 0; i < vm->cheats.count; i++) {
        const nj_cheat_patch *c = &vm->cheats.patch[i];
        if (!c->enabled || c->len == 0 || c->len > NJ_CHEAT_MAX_BYTES) continue;
        if (c->offset > size || (nj_size)c->len > size - c->offset) continue;
        memcpy((unsigned char *)data + c->offset, c->value, c->len);
    }
}

void nj_vm_apply_cheats(nj_vm *vm)
{
    if (vm == NULL || vm->cheats.count <= 0) return;
    nj_cheat_apply(vm);
}

/* ------------------------------------------------------------------ */
/* Cheat search                                                        */
/* ------------------------------------------------------------------ */
/*
 * A frontend cheat search needs two operations over the arena: find every
 * occurrence of a value ("first search"), then keep only the ones whose bytes
 * changed to the new value ("next search").  Both are plain reads: no object
 * is written, so a mistaken candidate only wastes a slot in the frontend's
 * list.  Offsets are the same arena offsets a cheat code names, which is what
 * lets a search result be turned into a code without translation.
 */

static const nj_u1 *cheat_arena(nj_vm *vm, nj_u4 *out_size)
{
    void *data = NULL;
    nj_size size = 0;

    if (out_size) *out_size = 0u;
    if (!vm) return NULL;
    if (nj_vm_memory_region(vm, &data, &size) != NJ_OK) return NULL;
    if (!data || size == 0) return NULL;
    if (size > (nj_size)0xFFFFFFFFu) size = (nj_size)0xFFFFFFFFu;
    if (out_size) *out_size = (nj_u4)size;
    return (const nj_u1 *)data;
}

nj_u4 nj_vm_cheat_find(nj_vm *vm, const nj_u1 *pattern, nj_u2 plen,
                       nj_u2 align, nj_u4 *offsets, nj_u4 max)
{
    const nj_u1 *arena;
    nj_u4 size = 0u;
    nj_u4 stored = 0u;
    nj_size i;

    if (!pattern || plen == 0u || plen > NJ_CHEAT_MAX_BYTES) return 0u;
    if (!offsets || max == 0u) return 0u;
    if (align == 0u) align = 1u;
    arena = cheat_arena(vm, &size);
    if (!arena || size < plen) return 0u;

    i = 0u;
    while (i + plen <= (nj_size)size) {
        const nj_u1 *hit;

        hit = (const nj_u1 *)memchr(arena + i, (int)pattern[0],
                                    (size_t)((nj_size)size - i - plen + 1u));
        if (!hit) break;
        i = (nj_size)(hit - arena);
        if ((align == 1u || ((nj_u4)i % align) == 0u) &&
            memcmp(hit, pattern, plen) == 0) {
            offsets[stored++] = (nj_u4)i;
            if (stored >= max) break;   /* full: more hits may exist */
        }
        i++;
    }
    return stored;
}

nj_u4 nj_vm_cheat_filter(nj_vm *vm, const nj_u4 *in_offsets, nj_u4 count,
                         const nj_u1 *pattern, nj_u2 plen,
                         nj_u4 *out_offsets)
{
    const nj_u1 *arena;
    nj_u4 size = 0u;
    nj_u4 kept = 0u;
    nj_u4 k;

    if (!in_offsets || !out_offsets || count == 0u) return 0u;
    if (!pattern || plen == 0u || plen > NJ_CHEAT_MAX_BYTES) return 0u;
    arena = cheat_arena(vm, &size);
    if (!arena) return 0u;

    for (k = 0u; k < count; k++) {
        nj_u4 off = in_offsets[k];
        if (off > size || plen > size - off) continue;
        if (memcmp(arena + off, pattern, plen) == 0) {
            out_offsets[kept++] = off;
        }
    }
    return kept;
}

int nj_vm_cheat_read(nj_vm *vm, nj_u4 offset, nj_u1 *buf, nj_u2 len)
{
    const nj_u1 *arena;
    nj_u4 size = 0u;

    if (!buf || len == 0u) return 0;
    arena = cheat_arena(vm, &size);
    if (!arena || offset > size || len > size - offset) return 0;
    memcpy(buf, arena + offset, len);
    return 1;
}

/* Little-endian value of `width` bytes, widened to 64 bits. */
static nj_u8 cheat_read_le(const nj_u1 *p, nj_u2 width)
{
    nj_u8 v = 0u;
    nj_u2 i;

    for (i = 0u; i < width; i++) {
        v |= (nj_u8)p[i] << (8u * i);
    }
    return v;
}

/* Does `now` satisfy the requested relation to `prev`? */
static int cheat_change_matches(const nj_u1 *now, const nj_u1 *prev,
                                nj_u2 width, int mode)
{
    if (mode == NJ_CHEAT_CHANGED) return memcmp(now, prev, width) != 0;
    if (mode == NJ_CHEAT_UNCHANGED) return memcmp(now, prev, width) == 0;
    if (mode == NJ_CHEAT_INCREASED) {
        return cheat_read_le(now, width) > cheat_read_le(prev, width);
    }
    if (mode == NJ_CHEAT_DECREASED) {
        return cheat_read_le(now, width) < cheat_read_le(prev, width);
    }
    return 0;
}

nj_u4 nj_vm_cheat_snapshot(nj_vm *vm, nj_u1 *buf, nj_u4 cap)
{
    const nj_u1 *arena;
    nj_u4 size = 0u;
    nj_u4 n;

    arena = cheat_arena(vm, &size);
    if (!arena) return 0u;
    if (!buf) return size;              /* caller asks for the allocation size */
    n = size < cap ? size : cap;
    memcpy(buf, arena, n);
    return n;
}

nj_u4 nj_vm_cheat_scan_changed(nj_vm *vm, const nj_u1 *prev, nj_u4 prev_len,
                               nj_u2 width, int mode, nj_u4 *offsets,
                               nj_u4 max)
{
    const nj_u1 *arena;
    nj_u4 size = 0u;
    nj_u4 stored = 0u;
    nj_u4 off;

    if (!prev || !offsets || max == 0u) return 0u;
    if (width < 1u || width > 8u) return 0u;
    if (mode < NJ_CHEAT_CHANGED || mode > NJ_CHEAT_DECREASED) return 0u;
    arena = cheat_arena(vm, &size);
    if (!arena || prev_len < size || size < width) return 0u;

    for (off = 0u; off + width <= size; off += width) {
        if (!cheat_change_matches(arena + off, prev + off, width, mode)) {
            continue;
        }
        offsets[stored++] = off;
        if (stored >= max) break;       /* full: more hits may exist */
    }
    return stored;
}

nj_u4 nj_vm_cheat_filter_changed(nj_vm *vm, const nj_u4 *in_offsets,
                                 nj_u4 count, const nj_u1 *prev,
                                 nj_u4 prev_len, nj_u2 width, int mode,
                                 nj_u4 *out_offsets, nj_u4 max)
{
    const nj_u1 *arena;
    nj_u4 size = 0u;
    nj_u4 kept = 0u;
    nj_u4 k;

    if (!in_offsets || !out_offsets || count == 0u || max == 0u) return 0u;
    if (!prev || width < 1u || width > 8u) return 0u;
    if (mode < NJ_CHEAT_CHANGED || mode > NJ_CHEAT_DECREASED) return 0u;
    arena = cheat_arena(vm, &size);
    if (!arena || prev_len < size) return 0u;

    for (k = 0u; k < count && kept < max; k++) {
        nj_u4 off = in_offsets[k];
        if (off > size || width > size - off) continue;
        if (cheat_change_matches(arena + off, prev + off, width, mode)) {
            out_offsets[kept++] = off;
        }
    }
    return kept;
}

int nj_vm_cheat_write(nj_vm *vm, nj_u4 offset, const nj_u1 *bytes, nj_u2 len)
{
    void *base = NULL;
    nj_size size = 0;

    if (!vm || !bytes || len == 0u) return 0;
    if (nj_vm_memory_region(vm, &base, &size) != NJ_OK || !base) return 0;
    if ((nj_size)len > size || (nj_size)offset > size - (nj_size)len) return 0;
    memcpy((nj_u1 *)base + offset, bytes, len);
    return 1;
}
