/*
 * Fixed-work RMS lookup benchmark (POSIX host). Link against the same runtime
 * before/after; see docs/notes/2026-11-10-rms-lookup.md for reproducible commands.
 * Creates a private /tmp directory of version-1 stores; never opens user data.
 * File creation, parsing and VM initialization are outside the timed region.
 * Every returned size/byte is checked against the independently written data.
 * The private directory is printed on stderr and retained for inspection.
 */
#define _POSIX_C_SOURCE 200809L
#include "nj2me/nj.h"
#include "nj_internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define RS "javax/microedition/rms/RecordStore"

typedef struct {
    const char *name;
    unsigned count, queries;
    int sparse, reordered, first, copy;
} workload;

static const workload cases[] = {
    {"size-8",          8, 8192, 0, 0, 0, 0},
    {"size-128",      128, 8192, 0, 0, 0, 0},
    {"size-1024",    1024, 8192, 0, 0, 0, 0},
    {"size-4096",    4096, 4096, 0, 0, 0, 0},
    {"first-4096",   4096, 8192, 0, 0, 1, 0},
    {"copy-4096",    4096, 4096, 0, 0, 0, 1},
    {"sparse-1024",  1024, 8192, 1, 0, 0, 0},
    {"reorder-1024", 1024, 8192, 0, 1, 0, 0}
};

static void require(int ok, nj_vm *vm, const char *where)
{
    if (!ok || (vm && nj_has_exception(vm))) {
        fprintf(stderr, "benchmark failed at %s: %s\n", where,
                vm && nj_has_exception(vm) ? nj_exception_describe(vm) : "mismatch/I/O");
        exit(2);
    }
}

static unsigned record_id(const workload *w, unsigned index)
{
    if (w->sparse) return 2u * index + 1u;
    if (w->reordered) return (index * 733u) % w->count + 1u;
    return index + 1u;
}

static unsigned char payload(unsigned index, unsigned byte)
{
    return (unsigned char)(index * 37u + byte * 11u);
}

static void u16(FILE *f, unsigned value)
{
    require(fputc((int)((value >> 8) & 255u), f) != EOF &&
            fputc((int)(value & 255u), f) != EOF, NULL, "write u16");
}

static void write_store(const char *directory, const workload *w)
{
    char path[256];
    FILE *f;
    unsigned i, j;
    require(snprintf(path, sizeof(path), "%s/%s.rms", directory, w->name) > 0,
            NULL, "store path");
    f = fopen(path, "wb");
    require(f != NULL, NULL, "create private store");
    require(fwrite("NJRS", 1, 4, f) == 4, NULL, "store magic");
    u16(f, 1); u16(f, 0);
    u16(f, w->sparse ? 2u * w->count + 1u : w->count + 1u);
    u16(f, w->count);
    for (i = 0; i < w->count; ++i) {
        u16(f, record_id(w, i));
        u16(f, i % 32u);
        for (j = 0; j < i % 32u; ++j)
            require(fputc(payload(i, j), f) != EOF, NULL, "record bytes");
    }
    require(fclose(f) == 0, NULL, "close fixture");
}

static double seconds(clockid_t clock)
{
    struct timespec t;
    require(clock_gettime(clock, &t) == 0, NULL, "clock_gettime");
    return (double)t.tv_sec + (double)t.tv_nsec / 1e9;
}

static unsigned long long hash_word(unsigned long long h, unsigned word)
{
    unsigned i;
    for (i = 0; i < 4; ++i) {
        h = (h ^ (word & 255u)) * 1099511628211ULL;
        word >>= 8;
    }
    return h;
}

static void run_case(const char *directory, const workload *w)
{
    nj_port port;
    nj_vm *vm;
    nj_ref store, buffer;
    nj_value a[3], result;
    nj_vm_stats before, after;
    nj_u4 roots, rng = 123456789u;
    unsigned q;
    double cpu, wall;
    unsigned long long hash = 14695981039346656037ULL;

    write_store(directory, w);
    require(nj_port_headless(&port) == NJ_OK, NULL, "headless port");
    port.rms_dir = directory;
    vm = nj_vm_create(&port, 16u * 1024u * 1024u);
    require(vm != NULL, NULL, "create VM");
    a[0].r = nj_string_new(vm, w->name); a[1].i = 0;
    require(a[0].r != NULL, vm, "store name");
    require(nj_call_static(vm, RS, "openRecordStore", "(Ljava/lang/String;Z)L" RS ";",
                           a, &result) == NJ_OK, vm, "open store");
    store = result.r;
    roots = nj_root_mark(vm);
    nj_root(vm, store);
    require(store && nj_root_mark(vm) == roots + 1u, vm, "root store");
    buffer = nj_byte_array_new(vm, 32);
    require(buffer != NULL, vm, "copy buffer");
    nj_root(vm, buffer);
    require(nj_root_mark(vm) == roots + 2u, vm, "root buffer");
    /* Warm the public-call and field caches with the same deterministic probe. */
    a[0].i = (nj_s4)record_id(w, 0);
    require(nj_call(vm, store, RS, "getRecordSize", "(I)I", a, &result) == NJ_OK,
            vm, "warm lookup");
    nj_vm_get_stats(vm, &before);
    cpu = seconds(CLOCK_PROCESS_CPUTIME_ID);
    wall = seconds(CLOCK_MONOTONIC);
    for (q = 0; q < w->queries; ++q) {
        unsigned index, j;
        rng = rng * 1664525u + 1013904223u;
        index = w->first ? 0u : ((rng >> 8) % w->count);
        a[0].i = (nj_s4)record_id(w, index);
        if (w->copy) {
            a[1].r = buffer; a[2].i = 0;
            require(nj_call(vm, store, RS, "getRecord", "(I[BI)I", a, &result) == NJ_OK,
                    vm, "getRecord");
        } else {
            require(nj_call(vm, store, RS, "getRecordSize", "(I)I", a, &result) == NJ_OK,
                    vm, "getRecordSize");
        }
        require(result.i == (nj_s4)(index % 32u), vm, "record length");
        hash = hash_word(hash, (unsigned)a[0].i);
        hash = hash_word(hash, (unsigned)result.i);
        if (w->copy) for (j = 0; j < index % 32u; ++j) {
            require(buffer->u.bytes[j] == payload(index, j), vm, "record payload");
            hash = hash_word(hash, buffer->u.bytes[j]);
        }
    }
    wall = seconds(CLOCK_MONOTONIC) - wall;
    cpu = seconds(CLOCK_PROCESS_CPUTIME_ID) - cpu;
    nj_vm_get_stats(vm, &after);
    printf("%s,%u,%u,%.6f,%.6f,%016llx,%llu,%u\n", w->name, w->count,
           w->queries, cpu * 1000.0, wall * 1000.0, hash,
           (unsigned long long)(after.native_calls - before.native_calls),
           (unsigned)(after.gc_count - before.gc_count));
    nj_root_release(vm, roots);
    nj_vm_destroy(vm);
    if (port.shutdown) port.shutdown(port.ud);
}

int main(void)
{
    char directory[] = "/tmp/nj-rms-lookup-XXXXXX";
    unsigned i;
    require(mkdtemp(directory) != NULL, NULL, "private directory");
    fprintf(stderr, "Private RMS fixtures retained at %s\n", directory);
    puts("case,records,queries,cpu_ms,wall_ms,checksum,native_calls,gc");
    for (i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) run_case(directory, &cases[i]);
    return 0;
}
