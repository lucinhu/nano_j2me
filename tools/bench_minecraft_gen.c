/*
 * Fixed-work MinecraftJ2ME PE 0.6.1 GEN benchmark. The caller supplies the JAR;
 * no game bytes or save files are included or changed by this tool.
 *
 * Usage: bench_minecraft_gen <original.jar> <signed-64-bit-seed>
 * Uses a fresh 64 MiB headless VM, survival gameType=0, and calls the original
 * Level()/Gen(Level,seed)/Gen.generate(NULL) methods. Only generate() is timed:
 * this excludes the UI, Player creation, save/load, rendering and checksums.
 * It is consequently not the full MC.createWorld GEN timer or a game FPS test.
 *
 * Link against an existing configured build (do not rebuild it for this tool):
 *   cc -std=c99 -O2 -Wall -Wextra -Werror -Iinclude -Isrc/core \
 *      tools/bench_minecraft_gen.c build/libnj2me.a -lz -lm \
 *      $(pkg-config --libs libcurl sdl2) -o /tmp/bench_minecraft_gen
 * Optional library/port dependencies must match that build's configuration.
 * Pin the process to the same CPU and alternate binaries for comparisons.
 *
 * Hashes are FNV-1a over Pal.exportChunk's 32768 block bytes / 16384 packed
 * data bytes. The aggregate hashes consume each chunk hash in little-endian
 * byte order, matching the game's WorldSum helper without depending on it.
 * Dispatches count fused interpreter handlers, not original JVM instructions.
 */
#define _POSIX_C_SOURCE 200809L
#include "nj2me/nj.h"
#include "nj_internal.h" /* temporary GC roots; no sampling or VM layout access */

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

static double seconds(clockid_t clock_id)
{
    struct timespec ts;
    if (clock_gettime(clock_id, &ts) != 0) {
        perror("clock_gettime");
        exit(2);
    }
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

static int call(nj_vm *vm, nj_ref receiver, const char *cls,
                const char *name, const char *desc, nj_value *args)
{
    const char *exception;
    if (nj_call(vm, receiver, cls, name, desc, args, NULL) == NJ_OK) {
        return 1;
    }
    exception = nj_exception_describe(vm);
    fprintf(stderr, "%s.%s%s failed: %s\n", cls, name, desc,
            exception ? exception : "no exception description");
    return 0;
}

static int root(nj_vm *vm, nj_ref object)
{
    nj_u4 mark = nj_root_mark(vm);
    nj_root(vm, object);
    if (nj_root_mark(vm) != mark + 1u) {
        fprintf(stderr, "cannot reserve temporary GC root\n");
        return 0;
    }
    return 1;
}

static nj_u4 hash_bytes(const nj_u1 *bytes, nj_u4 length)
{
    nj_u4 hash = 2166136261u, i;
    for (i = 0; i < length; i++) {
        hash = (hash ^ bytes[i]) * 16777619u;
    }
    return hash;
}

static nj_u4 hash_word(nj_u4 hash, nj_u4 word)
{
    unsigned i;
    for (i = 0; i < 4u; i++) {
        hash = (hash ^ (word & 255u)) * 16777619u;
        word >>= 8;
    }
    return hash;
}

static int world_hash(nj_vm *vm, nj_ref level,
                      const nj_field *blocks_field, const nj_field *data_field)
{
    nj_ref blocks = nj_field_read_ref(vm, level, blocks_field);
    nj_ref data = nj_field_read_ref(vm, level, data_field);
    nj_ref buffer;
    nj_value args[3];
    nj_u4 chunk, blocks_hash = 2166136261u, data_hash = 2166136261u;
    if (!blocks || !data || !(buffer = nj_array_new(vm, "B", 32768u))) {
        fprintf(stderr, "missing world palettes or cannot allocate hash buffer\n");
        return 0;
    }
    if (!root(vm, buffer)) return 0;
    for (chunk = 0; chunk < 256u; chunk++) {
        nj_u4 bh, dh;
        args[0].i = (nj_s4)chunk;
        args[1].r = buffer;
        args[2].i = 0;
        if (!call(vm, blocks, "Pal", "exportChunk", "(I[BZ)V", args)) {
            return 0;
        }
        bh = hash_bytes((const nj_u1 *)nj_array_data(buffer), 32768u);
        args[2].i = 1;
        if (!call(vm, data, "Pal", "exportChunk", "(I[BZ)V", args)) {
            return 0;
        }
        dh = hash_bytes((const nj_u1 *)nj_array_data(buffer), 16384u);
        blocks_hash = hash_word(blocks_hash, bh);
        data_hash = hash_word(data_hash, dh);
        printf("CHUNK %03u blocks=%08x data=%08x\n",
               (unsigned)chunk, (unsigned)bh, (unsigned)dh);
    }
    printf("WORLD chunks=256 blocks=%08x data=%08x\n",
           (unsigned)blocks_hash, (unsigned)data_hash);
    return 1;
}

int main(int argc, char **argv)
{
    FILE *file = NULL;
    long file_size;
    unsigned char *jar = NULL;
    char *end;
    long long seed;
    nj_port port;
    nj_vm *vm = NULL;
    nj_app *app = NULL;
    nj_ref level, gen;
    nj_field *type_field, *seed_field, *blocks_field, *data_field;
    nj_field *spawn_fields[3];
    const char *spawn_names[3] = {"spawnX", "spawnY", "spawnZ"};
    nj_value args[2];
    nj_vm_stats before, after;
    double wall, cpu;
    int status = 1, port_open = 0, i;

    if (argc != 3) {
        fprintf(stderr, "usage: %s <original.jar> <signed-64-bit-seed>\n", argv[0]);
        return 2;
    }
    errno = 0;
    seed = strtoll(argv[2], &end, 0);
    if (errno != 0 || end == argv[2] || *end != '\0') {
        fprintf(stderr, "invalid signed 64-bit seed: %s\n", argv[2]);
        return 2;
    }
    file = fopen(argv[1], "rb");
    if (!file) {
        perror(argv[1]);
        return 2;
    }
    if (fseek(file, 0, SEEK_END) != 0 || (file_size = ftell(file)) <= 0 ||
        (uintmax_t)file_size > UINT32_MAX ||
        (uintmax_t)file_size > SIZE_MAX || fseek(file, 0, SEEK_SET) != 0) {
        fprintf(stderr, "cannot read JAR size or JAR exceeds 32-bit limit\n");
        goto done;
    }
    jar = (unsigned char *)malloc((size_t)file_size);
    if (!jar || fread(jar, 1, (size_t)file_size, file) != (size_t)file_size) {
        fprintf(stderr, "cannot allocate or read JAR bytes\n");
        goto done;
    }
    fclose(file);
    file = NULL;
    if (nj_port_headless(&port) != NJ_OK) {
        fprintf(stderr, "cannot initialize headless port\n");
        goto done;
    }
    port_open = 1;
    port.rms_dir = "";
    vm = nj_vm_create(&port, 64u * 1024u * 1024u);
    if (!vm || nj_lib_late_init(vm) != NJ_OK ||
        !(app = nj_app_open(vm, jar, (nj_u4)file_size))) {
        fprintf(stderr, "cannot initialize VM or open JAR\n");
        goto done;
    }
    level = nj_object_new(vm, "Level");
    if (!level) {
        fprintf(stderr, "cannot allocate Level (unsupported JAR or out of memory)\n");
        goto done;
    }
    if (!root(vm, level)) goto done;
    if (!call(vm, level, "Level", "<init>", "()V", NULL)) {
        goto done;
    }
    type_field = nj_field_lookup(vm, "Level", "gameType", "I", 0);
    seed_field = nj_field_lookup(vm, "Level", "seed", "J", 0);
    blocks_field = nj_field_lookup(vm, "Level", "blocks", "LPal;", 0);
    data_field = nj_field_lookup(vm, "Level", "data", "LPal;", 0);
    for (i = 0; i < 3; i++) {
        spawn_fields[i] = nj_field_lookup(vm, "Level", spawn_names[i], "I", 0);
    }
    if (!type_field || !seed_field || !blocks_field || !data_field ||
        !spawn_fields[0] || !spawn_fields[1] || !spawn_fields[2]) {
        fprintf(stderr, "unsupported Level fields in this JAR\n");
        goto done;
    }
    nj_field_write_int(vm, level, type_field, 0);
    nj_field_write_long(vm, level, seed_field, (nj_s8)seed);
    gen = nj_object_new(vm, "Gen");
    if (!gen) {
        fprintf(stderr, "cannot allocate Gen\n");
        goto done;
    }
    if (!root(vm, gen)) goto done;
    args[0].r = level;
    args[1].j = (nj_s8)seed;
    if (!call(vm, gen, "Gen", "<init>", "(LLevel;J)V", args)) {
        goto done;
    }
    nj_vm_get_stats(vm, &before);
    cpu = seconds(CLOCK_PROCESS_CPUTIME_ID);
    wall = seconds(CLOCK_MONOTONIC);
    args[0].r = NULL;
    if (!call(vm, gen, "Gen", "generate", "(LGen$Progress;)V", args)) {
        goto done;
    }
    wall = seconds(CLOCK_MONOTONIC) - wall;
    cpu = seconds(CLOCK_PROCESS_CPUTIME_ID) - cpu;
    nj_vm_get_stats(vm, &after);
    printf("GEN seed=%lld type=0 wall_ms=%.3f cpu_ms=%.3f dispatch=%llu "
           "natives=%llu gc=%u heap=%lu peak=%lu\n",
           seed, wall * 1000.0, cpu * 1000.0,
           (unsigned long long)(after.insns - before.insns),
           (unsigned long long)(after.native_calls - before.native_calls),
           (unsigned)(after.gc_count - before.gc_count),
           (unsigned long)after.heap_used, (unsigned long)after.heap_peak);
    printf("SPAWN %d %d %d\n",
           (int)nj_field_read_int(vm, level, spawn_fields[0]),
           (int)nj_field_read_int(vm, level, spawn_fields[1]),
           (int)nj_field_read_int(vm, level, spawn_fields[2]));
    if (!world_hash(vm, level, blocks_field, data_field)) {
        goto done;
    }
    status = 0;

done:
    if (file) fclose(file);
    if (app) nj_app_close(vm, app);
    if (vm) nj_vm_destroy(vm);
    if (port_open) nj_port_shutdown(&port);
    free(jar);
    return status;
}
