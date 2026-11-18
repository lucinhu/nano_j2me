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

int main(void)
{
    test_parser();
    test_slots();
    test_apply();
    test_load_file();
    printf("test_cheat: %u checks, %u failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
