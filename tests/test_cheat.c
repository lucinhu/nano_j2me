/*
 * Arena-offset cheat codes (nj_cheat.c).
 *
 * A cheat code is "<arena offset>:<hex bytes>" over the same non-moving arena
 * window a libretro frontend sees as SYSTEM_RAM.  These checks cover the
 * parser, the slot bookkeeping, the per-frame apply semantics and the
 * out-of-range guard.  The apply case writes into the tail of a real 64 KiB
 * arena, which is free space after VM creation; the VM is not run, so the
 * patched bytes are only inspected by this test.
 */
#include "nj_internal.h"
#include "nj2me/nj.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define HEAP_BYTES (64u * 1024u)

static unsigned checks, failures;

#define CHECK(condition, message) do { \
    ++checks; \
    if (!(condition)) { \
        ++failures; \
        fprintf(stderr, "FAIL cheat line %d: %s\n", __LINE__, (message)); \
    } \
} while (0)

static nj_vm *fresh_vm(void)
{
    nj_vm *vm = nj_vm_create(NULL, HEAP_BYTES);
    if (!vm) {
        fprintf(stderr, "cheat fixture: cannot create VM\n");
        exit(2);
    }
    return vm;
}

static int read_slot(const char *code)
{
    nj_vm *vm = fresh_vm();
    int slot = nj_vm_cheat_add(vm, code);
    nj_vm_destroy(vm);
    return slot;
}

static void test_parser(void)
{
    /* Valid forms: plain, '@' prefix, 0x prefix, '=' separator, spaced bytes. */
    CHECK(read_slot("0:01") == 0, "plain code accepted");
    CHECK(read_slot("@1A2B:DEADBEEF") == 0, "'@' prefix accepted");
    CHECK(read_slot("0x100:00 11 22") == 0, "0x prefix and spaced bytes");
    CHECK(read_slot("20=FF00") == 0, "'=' separator accepted");
    CHECK(read_slot("  30 : 0a 0b ") == 0, "surrounding spaces accepted");

    /* Malformed forms are refused with NJ_ERR_FORMAT. */
    CHECK(read_slot("zz:05") == NJ_ERR_FORMAT, "non-hex offset refused");
    CHECK(read_slot("10:") == NJ_ERR_FORMAT, "empty value refused");
    CHECK(read_slot(":05") == NJ_ERR_FORMAT, "empty offset refused");
    CHECK(read_slot("1005") == NJ_ERR_FORMAT, "missing separator refused");
    CHECK(read_slot("10:0") == NJ_ERR_FORMAT, "odd nibble refused");
    CHECK(read_slot("10:00112233445566778899AABBCCDDEEFF00") == NJ_ERR_FORMAT,
          "too many bytes refused");
    CHECK(read_slot(NULL) == NJ_ERR, "NULL code refused");
}

static void test_slots(void)
{
    nj_vm *vm = fresh_vm();
    int a, b;

    CHECK(nj_vm_cheat_add(vm, "0:01") == 0, "first slot is 0");
    CHECK(nj_vm_cheat_add(vm, "1:02") == 1, "second slot is 1");
    a = nj_vm_cheat_add(vm, "2:03");
    CHECK(a == 2, "third slot is 2");
    CHECK(vm->cheats.count == 3, "three codes stored");
    CHECK(nj_vm_cheat_disable(vm, 1) == NJ_OK, "disable slot 1");
    b = nj_vm_cheat_add(vm, "3:04");
    CHECK(b == 1, "disabled slot is reused");
    CHECK(nj_vm_cheat_disable(vm, 2) == NJ_OK, "disable slot 2");
    CHECK(nj_vm_cheat_disable(vm, 99) == NJ_ERR, "out-of-range disable refused");
    nj_vm_cheat_clear(vm);
    CHECK(vm->cheats.count == 0, "clear empties the table");
    nj_vm_destroy(vm);
}

static void test_apply(void)
{
    nj_vm *vm = fresh_vm();
    void *base = NULL;
    nj_size size = 0;
    unsigned char *tail;
    unsigned char *far_tail;
    nj_u4 off, far_off;

    CHECK(nj_vm_memory_region(vm, &base, &size) == NJ_OK, "arena exposed");
    CHECK(base != NULL && size == HEAP_BYTES, "arena is the created heap");
    tail = (unsigned char *)base + size - 8;
    far_off = (nj_u4)(size - 2u);
    far_tail = (unsigned char *)base + far_off;

    memset(tail, 0, 4);
    *far_tail = 0x5A;
    *(far_tail + 1) = 0xA5;

    /* Offset needs to be formatted; build the code in place. */
    {
        char code[64];
        snprintf(code, sizeof(code), "%lx:DEADBEEF", (unsigned long)(size - 8u));
        CHECK(nj_vm_cheat_add(vm, code) == 0, "tail patch added");
        nj_cheat_apply(vm);
        CHECK(memcmp(tail, "\xDE\xAD\xBE\xEF", 4) == 0, "bytes patched");
        /* The game writing the old value back must not cancel the cheat. */
        memset(tail, 0, 4);
        nj_cheat_apply(vm);
        CHECK(memcmp(tail, "\xDE\xAD\xBE\xEF", 4) == 0,
              "patch re-applied after an overwrite");
    }

    /* A patch that runs past the end of the arena is skipped, not clamped. */
    {
        char code[64];
        snprintf(code, sizeof(code), "%lx:11223344", (unsigned long)far_off);
        CHECK(nj_vm_cheat_add(vm, code) >= 0, "out-of-range patch stored");
        nj_cheat_apply(vm);
        CHECK(*far_tail == 0x5A && *(far_tail + 1) == 0xA5,
              "out-of-range patch leaves memory alone");
    }

    /* Disabled patches are not applied. */
    CHECK(nj_vm_cheat_disable(vm, 0) == NJ_OK, "disable tail patch");
    memset(tail, 0, 4);
    nj_cheat_apply(vm);
    CHECK(memcmp(tail, "\0\0\0\0", 4) == 0, "disabled patch not applied");

    nj_vm_destroy(vm);
}

static void test_load_file(void)
{
    const char *path = "nj_cheat_unit_test.txt";
    nj_vm *vm = fresh_vm();
    FILE *f = fopen(path, "w");
    int n;

    if (!f) {
        fprintf(stderr, "cheat fixture: cannot write %s\n", path);
        exit(2);
    }
    fputs("# fixture cheat file\n\n", f);
    fputs("0x40:01\n", f);
    fputs("  0x41=0203  \n", f);
    fputs("this is not a cheat\n", f);
    fputs("0x42:FF\n", f);
    fclose(f);

    n = nj_vm_cheat_load(vm, path);
    CHECK(n == 3, "three codes loaded, malformed line skipped");
    CHECK(vm->cheats.count == 3, "table holds the loaded codes");
    remove(path);
    CHECK(nj_vm_cheat_load(vm, "nj_cheat_unit_test_missing.txt") == NJ_ERR_FORMAT,
          "missing file refused");
    nj_vm_destroy(vm);
}

/*
 * Cheat search: find/filter/read over the arena.  The fixture pokes bytes into
 * the free tail of a real arena; the VM is never run, so these bytes are only
 * inspected by this test.
 */
static void test_search(void)
{
    static const nj_u1 pat[4] = { 0xDE, 0xAD, 0xBE, 0xEF };
    static const nj_u1 pat2[4] = { 0x11, 0x22, 0x33, 0x44 };
    static const nj_u1 pat_tail[4] = { 0xCA, 0xFE, 0xBA, 0xBE };
    nj_vm *vm = fresh_vm();
    void *base = NULL;
    nj_size size = 0;
    unsigned char *arena;
    nj_u4 off_a, off_b, off_u, off_tail;
    nj_u4 hits[16];
    nj_u1 buf[8];
    nj_u4 n;

    CHECK(nj_vm_memory_region(vm, &base, &size) == NJ_OK, "search: arena exposed");
    arena = (unsigned char *)base;
    off_a = (nj_u4)(size - 64u);
    off_b = off_a + 8u;
    off_u = off_a + 17u;                 /* deliberately not 4-byte aligned */
    off_tail = (nj_u4)(size - 4u);

    memcpy(arena + off_a, pat, 4);
    memcpy(arena + off_b, pat, 4);
    memcpy(arena + off_u, pat, 4);
    memcpy(arena + off_tail, pat_tail, 4);

    /* Any offsets: all three copies, in ascending order. */
    n = nj_vm_cheat_find(vm, pat, 4u, 0u, hits, 16u);
    CHECK(n == 3u, "find: three copies found");
    CHECK(hits[0] == off_a && hits[1] == off_b && hits[2] == off_u,
          "find: offsets are ascending and exact");

    /* 4-byte alignment drops the unaligned copy. */
    n = nj_vm_cheat_find(vm, pat, 4u, 4u, hits, 16u);
    CHECK(n == 2u && hits[0] == off_a && hits[1] == off_b,
          "find: alignment filters unaligned hit");

    /* A full result list stops the scan instead of overrunning. */
    n = nj_vm_cheat_find(vm, pat, 4u, 0u, hits, 1u);
    CHECK(n == 1u && hits[0] == off_a, "find: max caps the result list");

    /* The pattern that ends exactly at the last byte is still found. */
    n = nj_vm_cheat_find(vm, pat_tail, 4u, 0u, hits, 16u);
    CHECK(n == 1u && hits[0] == off_tail, "find: hit ending at arena end");

    /* Narrow a candidate list: only the copy rewritten with pat2 survives. */
    memcpy(arena + off_b, pat2, 4);
    {
        nj_u4 cands[3];
        cands[0] = off_a;
        cands[1] = off_b;
        cands[2] = off_u;
        n = nj_vm_cheat_filter(vm, cands, 3u, pat2, 4u, cands);
        CHECK(n == 1u && cands[0] == off_b, "filter: keeps the changed copy");
    }

    /* Read back a candidate and the same range out of bounds. */
    CHECK(nj_vm_cheat_read(vm, off_b, buf, 4u) == 1, "read: candidate readable");
    CHECK(memcmp(buf, pat2, 4) == 0, "read: bytes match the arena");
    CHECK(nj_vm_cheat_read(vm, (nj_u4)(size - 2u), buf, 4u) == 0,
          "read: range past the arena end refused");
    CHECK(nj_vm_cheat_read(vm, (nj_u4)size, buf, 1u) == 0,
          "read: offset at arena end refused");

    /* Malformed search arguments are refused, not crashed on. */
    CHECK(nj_vm_cheat_find(vm, NULL, 4u, 0u, hits, 16u) == 0u,
          "find: NULL pattern refused");
    CHECK(nj_vm_cheat_find(vm, pat, 0u, 0u, hits, 16u) == 0u,
          "find: empty pattern refused");
    CHECK(nj_vm_cheat_find(vm, pat, 17u, 0u, hits, 16u) == 0u,
          "find: over-long pattern refused");
    CHECK(nj_vm_cheat_find(vm, pat, 4u, 0u, NULL, 16u) == 0u,
          "find: NULL output refused");
    CHECK(nj_vm_cheat_find(vm, pat, 4u, 0u, hits, 0u) == 0u,
          "find: zero max refused");

    nj_vm_destroy(vm);
}

static void test_change_search(void)
{
    nj_vm *vm = fresh_vm();
    void *base = NULL;
    nj_size size = 0;
    unsigned char *arena;
    nj_u4 off[3];
    nj_u4 hits[16];
    nj_u1 *snap;
    nj_u4 snap_len;
    static const nj_u1 v10[2] = { 0x10, 0x00 };
    static const nj_u1 v20[2] = { 0x20, 0x00 };
    static const nj_u1 v05[2] = { 0x05, 0x00 };
    static const nj_u1 v30[2] = { 0x30, 0x00 };
    nj_u1 readback[2];

    CHECK(nj_vm_memory_region(vm, &base, &size) == NJ_OK, "change: arena exposed");
    arena = (unsigned char *)base;
    off[0] = (nj_u4)(size - 4096u);
    off[1] = off[0] + 64u;
    off[2] = off[0] + 128u;

    /* Give the three addresses a known baseline first. */
    CHECK(nj_vm_cheat_write(vm, off[0], v10, 2u) == 1, "write: baseline 0x10 written");
    CHECK(nj_vm_cheat_write(vm, off[1], v20, 2u) == 1, "write: baseline 0x20 written");
    CHECK(nj_vm_cheat_write(vm, off[2], v05, 2u) == 1, "write: baseline 0x05 written");
    snap_len = nj_vm_cheat_snapshot(vm, NULL, 0u);
    CHECK(snap_len > 0u && snap_len == (nj_u4)size, "change: snapshot size is the arena size");
    snap = (nj_u1 *)calloc(1u, snap_len);
    CHECK(snap != NULL, "change: snapshot buffer allocated");
    CHECK(nj_vm_cheat_snapshot(vm, snap, snap_len) == snap_len,
          "change: snapshot copied the arena");
    CHECK(nj_vm_cheat_scan_changed(vm, snap, snap_len, 2u, NJ_CHEAT_CHANGED,
                                   hits, 16u) == 0u,
          "change: no hits before the game writes anything");

    /* 0x10 -> 0x30 (up), 0x20 -> 0x10 (down), 0x05 stays; 0x10 == old 0x10? no. */
    CHECK(nj_vm_cheat_write(vm, off[0], v30, 2u) == 1, "write: 0x30 written");
    CHECK(nj_vm_cheat_write(vm, off[1], v10, 2u) == 1, "write: 0x10 written");
    nj_vm_cheat_write(vm, off[2], v05, 2u);

    CHECK(nj_vm_cheat_scan_changed(vm, snap, snap_len, 2u, NJ_CHEAT_CHANGED,
                                   hits, 16u) == 2u,
          "change: two changed addresses found");
    CHECK(hits[0] == off[0] && hits[1] == off[1],
          "change: changed addresses are exact and ascending");
    CHECK(nj_vm_cheat_scan_changed(vm, snap, snap_len, 2u, NJ_CHEAT_INCREASED,
                                   hits, 16u) == 1u && hits[0] == off[0],
          "change: increased scan keeps the grown value");
    CHECK(nj_vm_cheat_scan_changed(vm, snap, snap_len, 2u, NJ_CHEAT_DECREASED,
                                   hits, 16u) == 1u && hits[0] == off[1],
          "change: decreased scan keeps the shrunk value");
    CHECK(nj_vm_cheat_filter_changed(vm, off, 3u, snap, snap_len, 2u,
                                     NJ_CHEAT_UNCHANGED, hits, 16u) == 1u &&
          hits[0] == off[2],
          "change: filter keeps only the unchanged candidate");
    {
        nj_u4 in[2] = { off[0], off[1] };
        nj_u4 out[2];

        CHECK(nj_vm_cheat_filter_changed(vm, in, 2u, snap, snap_len, 2u,
                                         NJ_CHEAT_CHANGED, out, 1u) == 1u,
              "change: filter honours the output cap");
        CHECK(nj_vm_cheat_filter_changed(vm, in, 2u, snap, snap_len, 2u,
                                         NJ_CHEAT_INCREASED, out, 2u) == 1u &&
              out[0] == off[0],
              "change: filter increased keeps the grown candidate");
    }
    CHECK(nj_vm_cheat_read(vm, off[0], readback, 2u) == 1 &&
          readback[0] == 0x30 && readback[1] == 0x00,
          "write: bytes are visible through the read helper");
    CHECK(nj_vm_cheat_write(vm, (nj_u4)size - 1u, v10, 2u) == 0,
          "write: range past the arena end refused");
    CHECK(nj_vm_cheat_write(vm, 0u, v10, 0u) == 0, "write: empty write refused");
    CHECK(nj_vm_cheat_scan_changed(vm, snap, snap_len - 1u, 2u,
                                   NJ_CHEAT_CHANGED, hits, 16u) == 0u,
          "change: short baseline refused");
    free(snap);
    nj_vm_destroy(vm);
}

int main(void)
{
    test_parser();
    test_slots();
    test_apply();
    test_load_file();
    test_search();
    test_change_search();
    printf("test_cheat: %u checks, %u failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
