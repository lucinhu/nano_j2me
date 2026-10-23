/* Fixed Java work in three engine modes. Uses public APIs only. */
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#include "nj2me/nj.h"
#include <errno.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#if !NJ_ENABLE_JIT
#error "test_jit_perf requires NJ_ENABLE_JIT=1"
#endif
#ifndef NJ_TEST_JIT_PERF_JAR
#define NJ_TEST_JIT_PERF_JAR "tests/fixtures/jitperf.jar"
#endif

enum { MODE_INTERPRETER, MODE_PROBE, MODE_EXECUTE, MODE_COUNT, KERNEL_COUNT = 8 };
static const char *const mode_names[] = {"interpreter", "probe-only", "execute"};
static const char *const kernels[] = {"locals", "constants", "fields", "arrays", "mixed",
                                      "branches", "bytes", "matrix"};
static const char *current_kernel = "setup", *current_mode = "setup";
#define CHECK(condition) do { if (!(condition)) { \
    fprintf(stderr, "FAIL %s:%d kernel=%s mode=%s: %s\n", __FILE__, __LINE__, \
            current_kernel, current_mode, #condition); exit(1); } } while (0)

typedef struct code_meter {
    void *(*allocate)(void *, nj_size);
    int (*commit)(void *, void *, nj_size);
    void (*release)(void *, void *, nj_size);
    nj_u8 attempts, allocations, commits, frees;
    nj_u8 requested_bytes;
} code_meter;
/* Sequential test instrumentation; the original port ud is preserved. */
static code_meter *active_meter;
static void *meter_alloc(void *ud, nj_size n)
{
    void *p;
    active_meter->attempts++;
    p = active_meter->allocate(ud, n);
    if (p) { active_meter->allocations++; active_meter->requested_bytes += n; }
    return p;
}
static int meter_commit(void *ud, void *p, nj_size n)
{
    active_meter->commits++;
    return active_meter->commit(ud, p, n);
}
static void meter_free(void *ud, void *p, nj_size n)
{
    active_meter->frees++;
    active_meter->release(ud, p, n);
}

typedef struct measurement {
    nj_u4 checksum;
    nj_u8 java_insns;
    double wall_ms;
    nj_jit_stats jit;
    code_meter code;
    int available;
} measurement;

/* Independent C oracle uses only unsigned arithmetic for Java's wraparound. */
static nj_u4 expected_checksum(unsigned kernel, nj_u4 iterations)
{
    nj_u4 i, a, b, sum, state, values[64];
    switch (kernel) {
    case 0:
        a = 0x1234; b = 0x5678;
        for (i = 0; i < iterations; ++i) { a = (a + b) ^ (i << 1); b = (b ^ a) + 3u; }
        return a ^ b;
    case 1:
        state = 0x12345678u;
        for (i = 0; i < iterations; ++i) state = state * 1664525u + 1013904223u;
        return state;
    case 2:
        state = 7;
        for (i = 0; i < iterations; ++i) { state += i; state ^= i << 3; }
        return state;
    case 3:
        for (i = 0; i < 64; ++i) values[i] = i * 17u + 3u;
        sum = 0;
        for (i = 0; i < iterations; ++i) { values[i & 63u] += i; sum ^= values[i & 63u]; }
        return sum ^ values[iterations & 63u];
    case 4: {
        nj_u4 box = 0, index;
        for (i = 0; i < 32; ++i) values[i] = i * 7u + 1u;
        sum = 0;
        for (i = 0; i < iterations; ++i) {
            index = i & 31u;
            box += i;
            values[index] += box;
            sum ^= values[index] + box;
            box ^= i << 2;
        }
        return sum ^ box ^ values[iterations & 31u];
    }
    case 5:
        state = 0x13579bdfu; sum = 0;
        for (i = 0; i < iterations; ++i) {
            state = state * 1103515245u + 12345u;
            if ((state & 7u) < 3u) sum += state;
            else if (!(state & 1u)) sum ^= state >> 5;
            else sum -= state & 1023u;
        }
        return sum ^ state;
    case 6: {
        /* JitPerf.bytes: signed-byte table plus a static int updated by
           putstatic.  Byte semantics are explicit: truncate to eight bits,
           then sign-extend back to int. */
        nj_s1 tiles[1024];
        nj_s4 tile_sum = 0;
        for (i = 0; i < 1024u; ++i) tiles[i] = (nj_s1)(nj_u1)(i * 31u + 7u);
        sum = 0;
        for (i = 0; i < iterations; ++i) {
            nj_u4 index = (i * 7u + sum) & 1023u;
            nj_s4 v = (nj_s4)tiles[index];
            v = (nj_s4)(nj_s1)(nj_u1)((nj_u4)v + (i & 31u));
            tiles[index] = (nj_s1)(nj_u1)v;
            sum = (sum + (nj_u4)v) ^ i;
            tile_sum = (nj_s4)((nj_u4)tile_sum + (nj_u4)sum);
        }
        return sum ^ (nj_u4)tile_sum;
    }
    case 7: {
        /* JitPerf.matrix: flattened 32x32 int grid; each iteration aaloads a
           row and then does iaload/iastore through it. */
        nj_u4 cells[32][32], j, v, *line;
        unsigned row, column;
        for (row = 0; row < 32u; ++row)
            for (column = 0; column < 32u; ++column)
                cells[row][column] = row * 37u + column * 11u + 5u;
        sum = 0;
        for (i = 0; i < iterations; ++i) {
            line = cells[(i * 5u + sum) & 31u];
            j = (i * 11u + sum) & 31u;
            v = line[j];
            v = (v + (i & 0xffffu)) ^ (sum >> 3);
            line[j] = v;
            sum += v;
        }
        return sum;
    }
    default:
        return 0;
    }
}

static double wall_ms(const nj_port *port)
{
#if defined(CLOCK_MONOTONIC)
    struct timespec now;
    CHECK(clock_gettime(CLOCK_MONOTONIC, &now) == 0);
    (void)port;
    return (double)now.tv_sec * 1000.0 + (double)now.tv_nsec / 1000000.0;
#else
    CHECK(port->millis != NULL);
    return (double)port->millis(port->ud);
#endif
}
static void quiet_log(void *ud, int level, const char *message)
{
    (void)ud; (void)level; (void)message;
}
static void check_call(nj_vm *vm, const char *kernel, nj_u4 iterations, nj_value *result)
{
    nj_value argument;
    nj_status status;
    memset(&argument, 0, sizeof(argument)); argument.i = (nj_s4)iterations;
    status = nj_call_static(vm, "jitperf/JitPerf", kernel, "(I)I", &argument, result);
    if (status != NJ_OK || nj_has_exception(vm)) {
        const char *exception = nj_exception_describe(vm);
        fprintf(stderr, "Java call failed kernel=%s status=%d exception=%s\n",
                kernel, (int)status, exception ? exception : "none");
        CHECK(status == NJ_OK && !nj_has_exception(vm));
    }
}
static int run_case(const nj_u1 *jar, nj_u4 jar_len, unsigned kernel,
                    unsigned mode, nj_u4 iterations, measurement *result)
{
    nj_port port;
    nj_vm *vm;
    nj_app *app;
    nj_value value;
    nj_vm_stats before, after;
    double start, finish;
    int status = 0;
    current_kernel = kernels[kernel]; current_mode = mode_names[mode];
    memset(result, 0, sizeof(*result));
    CHECK(nj_port_headless(&port) == NJ_OK);
    port.log = quiet_log;
    result->code.allocate = port.code_alloc;
    result->code.commit = port.code_commit;
    result->code.release = port.code_free;
    active_meter = &result->code;
    if (port.code_alloc) port.code_alloc = meter_alloc;
    if (port.code_commit) port.code_commit = meter_commit;
    if (port.code_free) port.code_free = meter_free;
    vm = nj_vm_create(&port, 2u * 1024u * 1024u);
    CHECK(vm != NULL);
    nj_jit_disable(vm);
    app = nj_app_open(vm, jar, jar_len);
    CHECK(app != NULL);
    /* Resolve classes and initialize fixture state outside the timed region.
     * This zero-iteration call always uses the interpreter, so each measured
     * mode starts with an empty JIT and identical class initialization. */
    check_call(vm, kernels[kernel], 0, &value);
    CHECK(value.bits == expected_checksum(kernel, 0));
    /* Force the class initialiser (which fills the static tables) to run
     * before the timed region; the kernel argument is ignored. */
    check_call(vm, "warm", 0, &value);
    CHECK(result->code.attempts == 0 && result->code.commits == 0);
    if (mode != MODE_INTERPRETER) {
        nj_status enabled = nj_jit_enable(vm);
        if (enabled == NJ_ERR_UNSUPPORTED) {
            printf("SKIP kernel=%s mode=%s reason=unsupported-cpu-or-executable-port\n",
                   kernels[kernel], mode_names[mode]);
            status = 77; goto cleanup;
        }
        CHECK(enabled == NJ_OK);
        CHECK(nj_jit_set_mode(vm, mode == MODE_PROBE ? NJ_JIT_MODE_PROBE_ONLY : NJ_JIT_MODE_EXECUTE) == NJ_OK);
    }
    nj_vm_get_stats(vm, &before);
    start = wall_ms(&port);
    check_call(vm, kernels[kernel], iterations, &value);
    finish = wall_ms(&port);
    nj_vm_get_stats(vm, &after);
    result->checksum = value.bits;
    CHECK(result->checksum == expected_checksum(kernel, iterations));
    result->java_insns = after.insns - before.insns;
    result->wall_ms = finish - start;
    CHECK(result->wall_ms >= 0.0 && result->java_insns > 0);
    result->available = 1;
    if (mode == MODE_INTERPRETER) CHECK(!nj_jit_get_stats(vm, &result->jit));
    else CHECK(nj_jit_get_stats(vm, &result->jit));
    if (mode != MODE_EXECUTE) {
        CHECK(result->jit.compiled_blocks == 0 && result->jit.executed_blocks == 0);
        CHECK(result->jit.bytecodes == 0 && result->jit.code_bytes == 0);
        CHECK(result->jit.peak_code_bytes == 0);
        CHECK(result->code.attempts == 0 && result->code.allocations == 0 && result->code.commits == 0);
    }
    if (result->jit.detailed_stats_enabled) {
        nj_u8 lengths = 0;
        unsigned bin;
        for (bin = 0; bin < 5; ++bin) lengths += result->jit.block_lengths[bin];
        CHECK(lengths == result->jit.executed_blocks);
    }
cleanup:
    nj_app_close(vm, app);
    (void)nj_vm_destroy(vm);
    CHECK(result->code.allocations == result->code.frees);
    active_meter = NULL;
    nj_port_shutdown(&port);
    return status;
}

static void print_fallback(unsigned kernel, unsigned mode, const measurement *r)
{
    nj_u8 native = (mode == MODE_EXECUTE && r->jit.stats_enabled)
                 ? r->jit.bytecodes : 0u;
    nj_u8 fallback = r->java_insns > native ? r->java_insns - native : 0u;
    double ratio = r->java_insns ? (double)fallback / (double)r->java_insns : 1.0;
    printf("FALLBACK kernel=%s mode=%s native_bytecodes=%" PRIu64
           " java_insns=%" PRIu64 " interpreter_bytecodes=%" PRIu64
           " fallback_ratio=%.6f metadata_bytes=%lu code_bytes=%lu "
           "peak_code_bytes=%lu total_bytes=%lu\n",
           kernels[kernel], mode_names[mode], (uint64_t)native,
           (uint64_t)r->java_insns, (uint64_t)fallback, ratio,
           (unsigned long)r->jit.metadata_bytes,
           (unsigned long)r->jit.code_bytes,
           (unsigned long)r->jit.peak_code_bytes,
           (unsigned long)(r->jit.metadata_bytes + r->jit.peak_code_bytes));
}

static void print_measurement(unsigned kernel, unsigned mode, nj_u4 iterations,
                             const measurement *r)
{
    const nj_jit_stats *s = &r->jit;
    printf("RESULT kernel=%s mode=%s iterations=%" PRIu32 " checksum=%08" PRIx32
           " wall_ms=%.3f java_insns=%" PRIu64 " stats=%d detailed=%d ",
           kernels[kernel], mode_names[mode], (uint32_t)iterations, (uint32_t)r->checksum,
           r->wall_ms, (uint64_t)r->java_insns, s->stats_enabled, s->detailed_stats_enabled);
    if (s->stats_enabled || mode != MODE_EXECUTE)
        printf("native_blocks=%" PRIu64 " native_bytecodes=%" PRIu64 " ",
               (uint64_t)s->executed_blocks, (uint64_t)s->bytecodes);
    else printf("native_blocks=unavailable native_bytecodes=unavailable ");
#define COUNTER(field) printf(#field "=%" PRIu64 " ", (uint64_t)s->field)
    COUNTER(compiled_blocks); COUNTER(declined_blocks);
    COUNTER(code_evictions); COUNTER(method_map_evictions); COUNTER(recompiled_blocks);
    COUNTER(unsupported_blocks); COUNTER(short_blocks);
    COUNTER(memory_failures); COUNTER(commit_failures);
    if (s->detailed_stats_enabled || mode == MODE_INTERPRETER) {
        COUNTER(queries); COUNTER(code_hits); COUNTER(cold_skips); COUNTER(negative_hits);
        COUNTER(probe_only_skips); COUNTER(budget_skips); COUNTER(stack_skips);
        printf("length_hist=%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%" PRIu64 " ",
               (uint64_t)s->block_lengths[0], (uint64_t)s->block_lengths[1],
               (uint64_t)s->block_lengths[2], (uint64_t)s->block_lengths[3], (uint64_t)s->block_lengths[4]);
    } else printf("queries=unavailable code_hits=unavailable cold_skips=unavailable "
                  "negative_hits=unavailable probe_only_skips=unavailable budget_skips=unavailable "
                  "stack_skips=unavailable length_hist=unavailable ");
    /* Faults are counted in every statistics configuration; only the detailed
       probe/cold counters above are compiled out. */
    COUNTER(faults);
#undef COUNTER
    printf("metadata_bytes=%lu code_bytes=%lu peak_code_bytes=%lu "
           "code_alloc_attempts=%" PRIu64 " code_allocations=%" PRIu64
           " code_commits=%" PRIu64 " code_frees=%" PRIu64 " code_requested_bytes=%" PRIu64 "\n",
           (unsigned long)s->metadata_bytes, (unsigned long)s->code_bytes, (unsigned long)s->peak_code_bytes,
           (uint64_t)r->code.attempts, (uint64_t)r->code.allocations, (uint64_t)r->code.commits,
           (uint64_t)r->code.frees, (uint64_t)r->code.requested_bytes);
    print_fallback(kernel, mode, r);
}
static nj_u1 *read_jar(const char *path, nj_u4 *length)
{
    FILE *file = fopen(path, "rb");
    long bytes;
    nj_u1 *data;
    if (!file) { fprintf(stderr, "cannot open JAR %s: %s\n", path, strerror(errno)); return NULL; }
    CHECK(fseek(file, 0, SEEK_END) == 0);
    bytes = ftell(file);
    CHECK(bytes > 0 && (uint64_t)bytes <= UINT32_MAX);
    rewind(file);
    data = malloc((size_t)bytes); CHECK(data != NULL);
    CHECK(fread(data, 1, (size_t)bytes, file) == (size_t)bytes);
    CHECK(fclose(file) == 0); *length = (nj_u4)bytes;
    return data;
}
static void usage(const char *name)
{
    fprintf(stderr, "usage: %s [--jar PATH] [--iterations 1..10000000] "
            "[--mode all|interpreter|probe-only|execute] [--require-execution]\n", name);
}
int main(int argc, char **argv)
{
    const char *jar_path = NJ_TEST_JIT_PERF_JAR;
    nj_u4 iterations = 20000, jar_len;
    nj_u1 *jar;
    measurement results[KERNEL_COUNT][MODE_COUNT];
    unsigned kernel, mode;
    int selected = -1, require_execution = 0, skipped = 0, execution_observed = 0, i;
    memset(results, 0, sizeof(results));
    for (i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--require-execution") == 0) require_execution = 1;
        else if (strcmp(argv[i], "--jar") == 0 && i + 1 < argc) jar_path = argv[++i];
        else if (strcmp(argv[i], "--iterations") == 0 && i + 1 < argc) {
            char *end;
            unsigned long n;
            errno = 0; n = strtoul(argv[++i], &end, 10);
            if (errno || *end || end == argv[i] || n < 1 || n > 10000000) { usage(argv[0]); return 2; }
            iterations = (nj_u4)n;
        } else if (strcmp(argv[i], "--mode") == 0 && i + 1 < argc) {
            const char *name = argv[++i];
            if (strcmp(name, "all") == 0) selected = -1;
            else {
                for (mode = 0; mode < MODE_COUNT; ++mode) if (strcmp(name, mode_names[mode]) == 0) break;
                if (mode == MODE_COUNT) { usage(argv[0]); return 2; }
                selected = (int)mode;
            }
        } else if (strcmp(argv[i], "--help") == 0) { usage(argv[0]); return 0; }
        else { usage(argv[0]); return 2; }
    }
    if (require_execution && selected >= 0 && selected != MODE_EXECUTE) { usage(argv[0]); return 2; }
    jar = read_jar(jar_path, &jar_len);
    if (!jar) return 1;
    printf("JIT_PERF iterations=%" PRIu32 " kernels=8 stats_build=%d detailed_build=%d "
           "length_bins=1,2-3,4-7,8-15,16+\n", (uint32_t)iterations, NJ_JIT_STATS, NJ_JIT_DETAILED_STATS);
    printf("JIT_CONFIG rv32=%d arm64=%d rv32_reg_cache=%d hot_threshold=%u "
           "min_block=%u max_block=%u code_entries=%u code_ways=%u max_code_bytes=%u "
           "method_entries=%u method_arena_bytes=%u max_faults=%u max_exits=%u\n",
           NJ_JIT_HOST_RV32, NJ_JIT_HOST_ARM64, NJ_JIT_RV32_REG_CACHE,
           (unsigned)NJ_JIT_HOT_THRESHOLD, (unsigned)NJ_JIT_MIN_BLOCK_INSNS,
           (unsigned)NJ_JIT_MAX_BLOCK_INSNS, (unsigned)NJ_JIT_CACHE_ENTRIES,
           (unsigned)NJ_JIT_CACHE_WAYS, (unsigned)NJ_JIT_MAX_CODE_BYTES,
           (unsigned)NJ_JIT_METHOD_ENTRIES, (unsigned)NJ_JIT_METHOD_ARENA_BYTES,
           (unsigned)NJ_JIT_MAX_FAULTS, (unsigned)NJ_JIT_MAX_EXITS);
    puts("TIMING first fixed-workload call after interpreted zero-iteration class warmup; "
         "includes JIT heat/compilation, excludes VM/JAR setup. QEMU time is not board performance; "
         "display/audio are outside this compute benchmark.");
    for (kernel = 0; kernel < KERNEL_COUNT; ++kernel) {
        for (mode = 0; mode < MODE_COUNT; ++mode) {
            measurement *r = &results[kernel][mode];
            int status;
            if (selected >= 0 && mode != (unsigned)selected) continue;
            status = run_case(jar, jar_len, kernel, mode, iterations, r);
            if (status == 77) { skipped = 1; continue; }
            CHECK(status == 0);
            print_measurement(kernel, mode, iterations, r);
            if (mode == MODE_EXECUTE && r->jit.compiled_blocks && r->code.commits &&
                (!r->jit.stats_enabled || r->jit.executed_blocks)) execution_observed = 1;
            if (mode > 0 && results[kernel][MODE_INTERPRETER].available)
                CHECK(r->checksum == results[kernel][MODE_INTERPRETER].checksum);
        }
        if (selected < 0 && results[kernel][0].available && results[kernel][1].available &&
            results[kernel][2].available && results[kernel][0].wall_ms > 0.0) {
            printf("RATIO kernel=%s probe_over_interpreter=%.6f execute_over_interpreter=%.6f\n",
                   kernels[kernel], results[kernel][1].wall_ms / results[kernel][0].wall_ms,
                   results[kernel][2].wall_ms / results[kernel][0].wall_ms);
        }
    }
    free(jar);
    if (require_execution && (skipped || !execution_observed)) {
        fputs("FAIL: --require-execution requires a supported JIT and committed/executed blocks; "
              "increase iterations if no site became hot.\n", stderr);
        return 1;
    }
    if (skipped) { puts("SKIP: checksum checks passed for available modes; JIT modes unavailable."); return 77; }
    puts("PASS: selected kernels/modes match independent unsigned C checksums; code allocations released.");
    return 0;
}
