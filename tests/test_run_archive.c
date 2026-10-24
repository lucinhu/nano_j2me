/* Exercise archive loading through the public runner and a bounded port.
 * Refuse the VM allocation after file_close: the archive's bytes, lifetime
 * and peak allocation can then be checked independently of Java execution. */
#include "nj2me/nj.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    nj_u4 size, hint, chunk, pos, error_at;
    nj_size limit, live, peak;
    unsigned opens, closes, allocations, verified, vm_attempts;
    int open;
} fixture;

typedef union { nj_size size; long double alignment; void *pointer; } header;
static unsigned failures;
#define CHECK(c) do { if (!(c)) { \
    fprintf(stderr, "line %d: %s\n", __LINE__, #c); failures++; \
} } while (0)

static nj_u1 value(nj_u4 pos) { return (nj_u1)(pos * 17u + (pos >> 8)); }

static void *allocate(void *ud, nj_size size)
{
    fixture *s = ud;
    header *h;
    if (!s->open) { s->vm_attempts++; return NULL; }
    if (size > s->limit - s->live) return NULL;
    h = malloc(sizeof(*h) + size);
    if (!h) return NULL;
    h->size = size;
    s->live += size;
    if (s->live > s->peak) s->peak = s->live;
    s->allocations++;
    return h + 1;
}

static void release(void *ud, void *ptr)
{
    fixture *s = ud;
    header *h;
    if (!ptr) return;
    h = (header *)ptr - 1;
    if (!s->open && s->vm_attempts) {
        nj_u4 i;
        CHECK(h->size >= s->size);
        for (i = 0; i < s->size; i++) {
            if (((nj_u1 *)ptr)[i] != value(i)) { CHECK(0); break; }
        }
        s->verified++;
    }
    s->live -= h->size;
    free(h);
}

static nj_status open_file(void *ud, const char *path, const char *mode, nj_file *f)
{
    fixture *s = ud;
    (void)path;
    CHECK(strcmp(mode, "rb") == 0);
    s->opens++; s->open = 1;
    *f = (nj_file){ .handle = s, .size = s->hint };
    return NJ_OK;
}

static nj_s4 read_file(void *ud, nj_file *f, void *dst, nj_u4 count)
{
    fixture *s = ud;
    nj_u4 i;
    CHECK(count != 0);
    if (s->error_at && s->pos >= s->error_at) return -1;
    if (count > s->size - s->pos) count = s->size - s->pos;
    if (s->chunk && count > s->chunk) count = s->chunk;
    for (i = 0; i < count; i++) ((nj_u1 *)dst)[i] = value(s->pos + i);
    s->pos += count; f->pos = s->pos;
    return (nj_s4)count;
}

static void close_file(void *ud, nj_file *f)
{
    fixture *s = ud;
    s->closes++; s->open = 0; f->handle = NULL;
}

static fixture run(nj_u4 size, nj_u4 hint, nj_u4 chunk, nj_size limit,
                   nj_u4 error_at, int complete)
{
    fixture s = { .size = size, .hint = hint, .chunk = chunk,
                  .limit = limit, .error_at = error_at };
    nj_port port = { .ud = &s, .user = &s, .rms_dir = "",
        .alloc = allocate, .free = release, .file_open = open_file,
        .file_read = read_file, .file_close = close_file };
    nj_run_opts opts = { .jar_path = "memory.jar", .heap_bytes = 65536 };
    CHECK(nj_run(&port, &opts) == (complete ? 1 : 2));
    CHECK(s.opens == 1 && s.closes == 1);
    CHECK(s.live == 0 && s.peak <= limit);
    CHECK(s.vm_attempts == (unsigned)complete);
    CHECK(s.verified == (unsigned)complete);
    if (complete) CHECK(s.pos == size);
    return s;
}

int main(void)
{
    fixture s;
    /* A known 1 MiB + 17-byte archive must fit its exact byte budget. */
    s = run(1048593, 1048593, 997, 1048593, 0, 1);
    CHECK(s.allocations == 1 && s.peak == 1048593);
    s = run(65536, 0, 0, 65536, 0, 1); /* EOF at growth boundary */
    CHECK(s.allocations == 1);
    run(1, 1, 1, 1, 0, 1);
    run(3, 1, 1, 10, 0, 1);         /* stale size grows, keeps probe byte */
    run(70001, 0, 113, 196608, 0, 1); /* unknown length and short reads */
    run(11, 100, 3, 100, 0, 1);     /* early EOF: size is only a hint */
    run(0, 0, 0, 65536, 0, 0);
    run(65537, 0, 0, 65536, 0, 0); /* failed growth frees old buffer */
    run(100, 100, 0, 99, 0, 0);    /* failed initial allocation closes */
    run(100, 100, 10, 100, 20, 0); /* read error unwinds */
    run(65536, 0, 0, 65536, 65536, 0); /* error during EOF probe */
    printf("archive loading: %u failures\n", failures);
    return failures ? 1 : 0;
}
