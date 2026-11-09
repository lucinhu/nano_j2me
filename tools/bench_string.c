/* Fixed-work StringBuffer/StringBuilder.getChars benchmark.
 * Link with libnj2me.a and its platform libraries; see the optimization report.
 * Columns: case, seconds, ns/call, Java objects allocated, destination checksum.
 * Compare alternating runs with identical flags, work and checksums.
 */
#define _POSIX_C_SOURCE 200809L
#include "nj_internal.h"
#include "nj2me/nj.h"
#include "nj2me/nj_port.h"
#include "nj2me/nj_vm.h"
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

static double now(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (double)t.tv_sec + (double)t.tv_nsec * 1e-9;
}

static void run(const char *name, const char *cls, int length, int copy)
{
    const int calls = 100000;
    nj_port port;
    nj_vm *vm;
    nj_ref sb, src, dst;
    nj_value a[4];
    nj_vm_stats before, after;
    char desc[64];
    unsigned hash = 2166136261u;
    int i;
    double start, elapsed;
    nj_port_headless(&port);
    vm = nj_vm_create(&port, 1024u * 1024u);
    if (!vm) exit(1);
    sb = nj_root(vm, nj_object_new(vm, cls));
    src = nj_root(vm, nj_array_new(vm, "C", (nj_u4)length));
    dst = nj_root(vm, nj_array_new(vm, "C", (nj_u4)copy + 2u));
    if (!sb || !src || !dst) exit(1);
    for (i = 0; i < length; i++)
        ((nj_u2 *)nj_array_data(src))[i] = (nj_u2)(0x4e00 + i % 1000);
    if (nj_call(vm, sb, cls, "<init>", "()V", NULL, NULL) != NJ_OK) exit(1);
    snprintf(desc, sizeof(desc), "([C)L%s;", cls);
    a[0].r = src;
    if (nj_call(vm, sb, cls, "append", desc, a, NULL) != NJ_OK) exit(1);
    a[0].i = (length - copy) / 2; a[1].i = a[0].i + copy;
    a[2].r = dst; a[3].i = 1;
    for (i = 0; i < 256; i++)
        if (nj_call(vm, sb, cls, "getChars", "(II[CI)V", a, NULL) != NJ_OK) exit(1);
    nj_gc_run(vm);
    nj_vm_get_stats(vm, &before);
    start = now();
    for (i = 0; i < calls; i++)
        if (nj_call(vm, sb, cls, "getChars", "(II[CI)V", a, NULL) != NJ_OK) exit(1);
    elapsed = now() - start;
    nj_vm_get_stats(vm, &after);
    for (i = 0; i < copy + 2; i++)
        hash = (hash ^ ((nj_u2 *)nj_array_data(dst))[i]) * 16777619u;
    printf("%s,%.9f,%.3f,%u,%08x\n", name, elapsed,
           elapsed * 1e9 / calls, (unsigned)(after.total_objects - before.total_objects), hash);
    nj_vm_destroy(vm);
    nj_port_shutdown(&port);
}

int main(void)
{
    run("buffer32-copy16", "java/lang/StringBuffer", 32, 16);
    run("buffer4096-copy16", "java/lang/StringBuffer", 4096, 16);
    run("buffer4096-copy4096", "java/lang/StringBuffer", 4096, 4096);
    run("buffer4096-copy0", "java/lang/StringBuffer", 4096, 0);
    run("builder32-copy16", "java/lang/StringBuilder", 32, 16);
    return 0;
}
