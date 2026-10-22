/*
 * nano_j2me - tests/test_rms.c
 *
 * RecordStore.getLastModified()J, and the on-disk timestamp the corpus needs.
 *
 *   1. The descriptor.  MIDP declares `public long getLastModified()`.
 *      HighSpeed 3D's GlomoReg.h calls it with ()J and its exception table
 *      catches only java.lang.Exception - our old ()I native raised
 *      NoSuchMethodError, an Error, which escaped startApp.  So a regression
 *      here is not a wrong number, it is a title that dies.  The first case
 *      therefore calls through a ()J descriptor and checks the 64-bit value
 *      really arrives in the long slot (an implementation writing ret->i
 *      leaves the upper half zero, and a 2040 timestamp cannot be confused
 *      with a 32-bit one).
 *   2. The value.  "The time the record store was last modified", not "now".
 *      Reading it twice, enumerating it, or closing/reopening the store must
 *      not move it; adding, setting and deleting a record must.  A failed
 *      mutation must not either - it changed nothing.
 *   3. The persistence.  A timestamp that only survives one process is the
 *      old behaviour with extra steps.  The tests destroy the VM (the C API's
 *      reboot) and read the value back through a new VM on the same directory,
 *      with the fake clock moved far forward, so "now" is not an acceptable
 *      answer.
 *   4. The format.  The trailer is append-only so builds from before it still
 *      read the records: an old decoder is implemented here byte for byte and
 *      run over the new file, then a bare version 1 file (what an old build
 *      writes) is fed to the new reader.  Missing, truncated, unknown-version
 *      and extra-byte trailers each fall back to the file's mtime; none of
 *      them loses a record.
 *
 * The test owns its clock: nj_port.millis/now are replaced before each VM is
 * created, so the expected values are exact constants and not "whatever the
 * machine said".  It is still an epoch-based clock (the fixed date is
 * 2040-06-15T12:00:00Z), because the timestamp has to be comparable with
 * System.currentTimeMillis() in the game's own check.
 *
 * Usage:  test_rms
 * The base directory comes from NANO_J2ME_RMS (default tests/out/rms), the
 * same variable tests/test_lib.c honours.
 * Exit code 0 on PASS, non-zero otherwise.
 */
#include "nj2me/nj.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <utime.h>

/* ------------------------------------------------------------------ */
/* Assertion machinery (same shape as the other white-box tests)       */
/* ------------------------------------------------------------------ */
static int g_pass;
static int g_fail;

#define OK(cond)                                                              \
    do {                                                                      \
        if (cond) {                                                           \
            g_pass++;                                                         \
        } else {                                                              \
            g_fail++;                                                         \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);   \
        }                                                                     \
    } while (0)

#define OK_MSG(cond, ...)                                                     \
    do {                                                                      \
        if (cond) {                                                           \
            g_pass++;                                                         \
        } else {                                                              \
            g_fail++;                                                         \
            fprintf(stderr, "FAIL %s:%d: %s: ", __FILE__, __LINE__, #cond);   \
            fprintf(stderr, __VA_ARGS__);                                     \
            fputc('\n', stderr);                                              \
        }                                                                     \
    } while (0)

#define RS "javax/microedition/rms/RecordStore"
#define RS_OPEN "(Ljava/lang/String;Z)L" RS ";"

/* 2040-06-15T12:00:00Z.  Larger than 2^32 milliseconds, so a truncating
 * implementation cannot pass by accident. */
#define EPOCH_2040_MS 2223374400000LL
#define MILLIS_ORIGIN 500000u

/* ------------------------------------------------------------------ */
/* The fake wall clock                                                 */
/* ------------------------------------------------------------------ */
static nj_u4 g_clock_ms = MILLIS_ORIGIN;
static nj_datetime g_clock_dt;

static int test_now(void *ud, nj_datetime *out)
{
    (void)ud;
    *out = g_clock_dt;
    return 1;
}

static nj_u4 test_millis(void *ud)
{
    (void)ud;
    return g_clock_ms;
}

/* Reset the counter and tell the VM what "now" is.  Called just before
 * nj_vm_create; the runtime anchors its virtual epoch at creation. */
static void clock_reset(void)
{
    g_clock_ms = MILLIS_ORIGIN;
    memset(&g_clock_dt, 0, sizeof(g_clock_dt));
    g_clock_dt.year = 2040;
    g_clock_dt.month = 5;   /* zero-based: June */
    g_clock_dt.day = 15;
    g_clock_dt.hour = 12;
    g_clock_dt.weekday = 5; /* Friday; not used by the conversion */
    g_clock_dt.tz_offset = 0;
}

static void clock_advance(nj_u4 ms)
{
    g_clock_ms += ms;
}

/* A port whose file_open() refuses every write, so a save can be made to fail
 * on demand.  Reads still go to the real headless port. */
static nj_port g_inject_base;

static int inject_open(void *ud, const char *name, const char *mode,
                       nj_file *out)
{
    (void)ud;
    if (mode && (mode[0] == 'w' || mode[0] == 'a')) {
        return NJ_ERR;
    }
    return g_inject_base.file_open(g_inject_base.ud, name, mode, out);
}

/* What the VM's clock reads right now, given it was created at the current
 * value of the counter.  Only valid immediately after creating a VM. */
static nj_s8 clock_vm_now(void)
{
    return EPOCH_2040_MS + (nj_s8)(g_clock_ms - MILLIS_ORIGIN);
}

/* ------------------------------------------------------------------ */
/* Small helpers                                                       */
/* ------------------------------------------------------------------ */
static nj_value vref(nj_ref r)
{
    nj_value v;
    v.bits = 0u;
    v.r = r;
    return v;
}

static nj_value vint(nj_s4 i)
{
    nj_value v;
    v.bits = 0u;
    v.i = i;
    return v;
}

static void clean(nj_vm *vm, const char *where)
{
    if (nj_has_exception(vm)) {
        const char *d = nj_exception_describe(vm);
        g_fail++;
        fprintf(stderr, "FAIL %s: unexpected exception: %s\n", where,
                d ? d : "(no description)");
        nj_clear_exception(vm);
    } else {
        g_pass++;
    }
}

/* mkdir -p, so the test also works when tests/out does not exist yet. */
static void mkdir_p(const char *path)
{
    char tmp[1024];
    size_t i, n;

    n = strlen(path);
    if (n >= sizeof(tmp)) {
        return;
    }
    memcpy(tmp, path, n + 1u);
    for (i = 1u; i < n; i++) {
        if (tmp[i] == '/') {
            tmp[i] = '\0';
            (void)mkdir(tmp, 0777);
            tmp[i] = '/';
        }
    }
    (void)mkdir(tmp, 0777);
}

static void store_path(char *out, size_t cap, const char *dir,
                       const char *name)
{
    snprintf(out, cap, "%s/%s.rms", dir, name);
}

static int file_exists(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0;
}

static long file_size(const char *path)
{
    struct stat st;
    if (stat(path, &st) != 0) {
        return -1;
    }
    return (long)st.st_size;
}

static void set_mtime(const char *path, long seconds)
{
    struct utimbuf tb;
    tb.actime = seconds;
    tb.modtime = seconds;
    (void)utime(path, &tb);
}

/* A port with the fake clock installed.  `libc_backend` drops every
 * filesystem hook so the runtime's own C-library fallback is exercised. */
static nj_port make_port(const nj_port *base, const char *dir,
                         int libc_backend)
{
    nj_port p = *base;

    p.rms_dir = dir;
    p.millis = test_millis;
    p.now = test_now;
    if (libc_backend) {
        p.file_open = NULL;
        p.file_read = NULL;
        p.file_write = NULL;
        p.file_seek = NULL;
        p.file_close = NULL;
        p.file_remove = NULL;
        p.file_exists = NULL;
        p.dir_open = NULL;
        p.dir_next = NULL;
        p.dir_close = NULL;
    }
    return p;
}

static nj_vm *vm_new(nj_port *port)
{
    nj_vm *vm = nj_vm_create(port, 2u * 1024u * 1024u);
    OK_MSG(vm != NULL, "create a 2 MiB VM");
    return vm;
}

/* ------------------------------------------------------------------ */
/* RecordStore calls                                                   */
/* ------------------------------------------------------------------ */
static nj_ref open_store(nj_vm *vm, const char *name, int create,
                         const char *where)
{
    nj_value a[2], ret;
    nj_status st;

    a[0] = vref(nj_string_new(vm, name));
    a[1] = vint(create ? 1 : 0);
    ret.bits = 0u;
    st = nj_call_static(vm, RS, "openRecordStore", RS_OPEN, a, &ret);
    OK_MSG(st == NJ_OK && !nj_has_exception(vm) && ret.r != NULL,
           "%s: openRecordStore(%s, %d) (exception: %s)", where, name, create,
           nj_has_exception(vm) ? nj_exception_describe(vm) : "none");
    if (nj_has_exception(vm)) {
        nj_clear_exception(vm);
        return NULL;
    }
    return ret.r;
}

static void close_store(nj_vm *vm, nj_ref store, const char *where)
{
    nj_status st = nj_call(vm, store, RS, "closeRecordStore", "()V", NULL,
                           NULL);
    OK_MSG(st == NJ_OK && !nj_has_exception(vm),
           "%s: closeRecordStore() (exception: %s)", where,
           nj_has_exception(vm) ? nj_exception_describe(vm) : "none");
    if (nj_has_exception(vm)) {
        nj_clear_exception(vm);
    }
}

/* The access-mode pair.  getMode() returns 0/1; setMode() validates. */
static int get_mode(nj_vm *vm, nj_ref store, nj_s4 *out, const char *where)
{
    nj_value ret;
    nj_status st;

    ret.bits = 0u;
    st = nj_call(vm, store, RS, "getMode", "()I", NULL, &ret);
    OK_MSG(st == NJ_OK && !nj_has_exception(vm),
           "%s: getMode() (exception: %s)", where,
           nj_has_exception(vm) ? nj_exception_describe(vm) : "none");
    if (nj_has_exception(vm)) {
        nj_clear_exception(vm);
        return 0;
    }
    *out = ret.i;
    return 1;
}

static nj_status set_mode(nj_vm *vm, nj_ref store, nj_s4 mode)
{
    nj_value a[2];

    a[0] = vint(mode);
    a[1] = vint(1);                 /* writable */
    return nj_call(vm, store, RS, "setMode", "(IZ)V", a, NULL);
}

static void delete_store(nj_vm *vm, const char *name, const char *where)
{
    nj_value a[1];
    nj_status st;

    a[0] = vref(nj_string_new(vm, name));
    st = nj_call_static(vm, RS, "deleteRecordStore", "(Ljava/lang/String;)V",
                        a, NULL);
    OK_MSG(st == NJ_OK && !nj_has_exception(vm),
           "%s: deleteRecordStore(%s)", where, name);
    if (nj_has_exception(vm)) {
        nj_clear_exception(vm);
    }
}

static nj_s4 num_records(nj_vm *vm, nj_ref store, const char *where)
{
    nj_value ret;
    nj_status st;

    ret.bits = 0u;
    st = nj_call(vm, store, RS, "getNumRecords", "()I", NULL, &ret);
    OK_MSG(st == NJ_OK && !nj_has_exception(vm),
           "%s: getNumRecords() (exception: %s)", where,
           nj_has_exception(vm) ? nj_exception_describe(vm) : "none");
    if (nj_has_exception(vm)) {
        nj_clear_exception(vm);
        return -1;
    }
    return ret.i;
}

static nj_s4 add_bytes(nj_vm *vm, nj_ref store, const void *p, nj_u4 n,
                       const char *where)
{
    nj_ref arr = nj_byte_array_new(vm, n);
    nj_value a[3], ret;
    nj_status st;

    if (!arr) {
        g_fail++;
        fprintf(stderr, "FAIL %s: cannot allocate record bytes\n", where);
        return -1;
    }
    if (n > 0u) {
        memcpy(arr->u.bytes, p, n);
    }
    a[0] = vref(arr);
    a[1] = vint(0);
    a[2] = vint((nj_s4)n);
    ret.bits = 0u;
    st = nj_call(vm, store, RS, "addRecord", "([BII)I", a, &ret);
    OK_MSG(st == NJ_OK && !nj_has_exception(vm),
           "%s: addRecord(%u bytes) (exception: %s)", where, n,
           nj_has_exception(vm) ? nj_exception_describe(vm) : "none");
    if (nj_has_exception(vm)) {
        nj_clear_exception(vm);
        return -1;
    }
    return ret.i;
}

static void set_bytes(nj_vm *vm, nj_ref store, nj_s4 id, const void *p,
                      nj_u4 n, const char *where)
{
    nj_ref arr = nj_byte_array_new(vm, n);
    nj_value a[4];
    nj_status st;

    if (!arr) {
        g_fail++;
        fprintf(stderr, "FAIL %s: cannot allocate record bytes\n", where);
        return;
    }
    if (n > 0u) {
        memcpy(arr->u.bytes, p, n);
    }
    a[0] = vint(id);
    a[1] = vref(arr);
    a[2] = vint(0);
    a[3] = vint((nj_s4)n);
    st = nj_call(vm, store, RS, "setRecord", "(I[BII)V", a, NULL);
    OK_MSG(st == NJ_OK && !nj_has_exception(vm),
           "%s: setRecord(%ld) (exception: %s)", where, (long)id,
           nj_has_exception(vm) ? nj_exception_describe(vm) : "none");
    if (nj_has_exception(vm)) {
        nj_clear_exception(vm);
    }
}

static void delete_record(nj_vm *vm, nj_ref store, nj_s4 id,
                          const char *where)
{
    nj_value a[1];
    nj_status st;

    a[0] = vint(id);
    st = nj_call(vm, store, RS, "deleteRecord", "(I)V", a, NULL);
    OK_MSG(st == NJ_OK && !nj_has_exception(vm),
           "%s: deleteRecord(%ld) (exception: %s)", where, (long)id,
           nj_has_exception(vm) ? nj_exception_describe(vm) : "none");
    if (nj_has_exception(vm)) {
        nj_clear_exception(vm);
    }
}

/* Call getLastModified through a ()J descriptor and check both the status and
 * the exact value.  The call is where a wrong native descriptor would show. */
static void expect_lm(nj_vm *vm, nj_ref store, nj_s8 want, const char *what)
{
    nj_value ret;
    nj_status st;

    ret.bits = 0u;
    ret.j = 0;
    st = nj_call(vm, store, RS, "getLastModified", "()J", NULL, &ret);
    OK_MSG(st == NJ_OK && !nj_has_exception(vm),
           "%s: getLastModified()J returns normally (exception: %s)", what,
           nj_has_exception(vm) ? nj_exception_describe(vm) : "none");
    if (nj_has_exception(vm)) {
        nj_clear_exception(vm);
        return;
    }
    OK_MSG(ret.j == want, "%s: getLastModified() == %lld, want %lld", what,
           (long long)ret.j, (long long)want);
}

/* Call getLastModified expecting an exception whose description contains
 * `want_class`. */
static void expect_lm_throws(nj_vm *vm, nj_ref store, const char *want_class,
                             const char *what)
{
    nj_value ret;
    nj_status st;
    const char *d;

    ret.bits = 0u;
    st = nj_call(vm, store, RS, "getLastModified", "()J", NULL, &ret);
    d = nj_has_exception(vm) ? nj_exception_describe(vm) : NULL;
    OK_MSG(st != NJ_OK && d != NULL && strstr(d, want_class) != NULL,
           "%s: expected %s, got status %d exception %s", what, want_class,
           (int)st, d ? d : "(none)");
    nj_clear_exception(vm);
}

/* ------------------------------------------------------------------ */
/* Byte-level v1 file helpers (the OLD format, and the old decoder)    */
/* ------------------------------------------------------------------ */
typedef struct old_view {
    int ok;
    int version;
    int reserved;
    int count;
    int has_trailer;
    nj_s8 trailer_ts;
    nj_u4 next_id;
    nj_u4 first_id;
    char first[64];
} old_view;

static nj_u2 rd_u2(FILE *fp, int *ok)
{
    unsigned char b[2];
    if (fread(b, 1u, 2u, fp) != 2u) {
        *ok = 0;
        return 0u;
    }
    return (nj_u2)(((nj_u2)b[0] << 8) | b[1]);
}

static nj_u4 rd_u4(FILE *fp, int *ok)
{
    unsigned char b[4];
    nj_u4 v = 0u;
    int i;
    if (fread(b, 1u, 4u, fp) != 4u) {
        *ok = 0;
        return 0u;
    }
    for (i = 0; i < 4; i++) {
        v = (v << 8) | b[i];
    }
    return v;
}

/* Decode with the pre-trailer rules only: header, records, stop.  This is the
 * compatibility proof that an old binary sees the records it expects. */
static void old_v1_decode(const char *path, old_view *out)
{
    FILE *fp = fopen(path, "rb");
    unsigned char magic[4];
    int ok = 1;
    int i;
    int extra;

    memset(out, 0, sizeof(*out));
    if (!fp) {
        return;
    }
    if (fread(magic, 1u, 4u, fp) != 4u || magic[0] != 'N' ||
        magic[1] != 'J' || magic[2] != 'R' || magic[3] != 'S') {
        fclose(fp);
        return;
    }
    out->version = (int)rd_u2(fp, &ok);
    out->reserved = (int)rd_u2(fp, &ok);
    out->next_id = rd_u2(fp, &ok);
    out->count = (int)rd_u2(fp, &ok);
    if (!ok || out->version != 1) {
        fclose(fp);
        return;
    }
    for (i = 0; i < out->count && ok; i++) {
        nj_u4 id = rd_u2(fp, &ok);
        nj_u4 len = rd_u2(fp, &ok);
        if (!ok) {
            break;
        }
        if (i == 0) {
            nj_u4 take = len < sizeof(out->first) - 1u
                       ? len : (nj_u4)(sizeof(out->first) - 1u);
            out->first_id = id;
            if (take > 0u && fread(out->first, 1u, take, fp) != take) {
                ok = 0;
                break;
            }
            out->first[take] = '\0';
            if (len > take && fseek(fp, (long)(len - take), SEEK_CUR) != 0) {
                ok = 0;
                break;
            }
        } else if (fseek(fp, (long)len, SEEK_CUR) != 0) {
            ok = 0;
            break;
        }
    }
    if (ok) {
        unsigned char tail[17];
        size_t got = fread(tail, 1u, sizeof(tail), fp);
        if (got == 16u && tail[0] == 'N' && tail[1] == 'J' &&
            tail[2] == 'M' && tail[3] == 'T' && tail[4] == 0x00 &&
            tail[5] == 0x00 && tail[6] == 0x00 && tail[7] == 0x01) {
            nj_u8 ts = 0u;
            int j;
            for (j = 0; j < 8; j++) {
                ts = (ts << 8) | tail[8 + j];
            }
            out->has_trailer = 1;
            out->trailer_ts = (nj_s8)ts;
        }
    }
    out->ok = ok;
    fclose(fp);
}

static void wr_u1(FILE *fp, unsigned v)
{
    unsigned char b = (unsigned char)v;
    (void)fwrite(&b, 1u, 1u, fp);
}

static void wr_u2(FILE *fp, unsigned v)
{
    unsigned char b[2];
    b[0] = (unsigned char)(v >> 8);
    b[1] = (unsigned char)(v & 0xffu);
    (void)fwrite(b, 1u, 2u, fp);
}

static void wr_u4(FILE *fp, nj_u4 v)
{
    unsigned char b[4];
    b[0] = (unsigned char)(v >> 24);
    b[1] = (unsigned char)(v >> 16);
    b[2] = (unsigned char)(v >> 8);
    b[3] = (unsigned char)v;
    (void)fwrite(b, 1u, 4u, fp);
}

static void wr_u8(FILE *fp, nj_u8 v)
{
    unsigned char b[8];
    int i;
    for (i = 7; i >= 0; i--) {
        b[i] = (unsigned char)(v & 0xffu);
        v >>= 8;
    }
    (void)fwrite(b, 1u, 8u, fp);
}

/* Write a bare version-1 store (no trailer), the way a build from before the
 * trailer writes it.  One record: id 7, bytes "abc". */
static void write_legacy_v1(const char *path)
{
    FILE *fp = fopen(path, "wb");

    if (!fp) {
        g_fail++;
        fprintf(stderr, "FAIL cannot create %s\n", path);
        return;
    }
    wr_u1(fp, 'N');
    wr_u1(fp, 'J');
    wr_u1(fp, 'R');
    wr_u1(fp, 'S');
    wr_u2(fp, 1u);      /* version */
    wr_u2(fp, 0u);      /* reserved */
    wr_u2(fp, 9u);      /* nextId */
    wr_u2(fp, 1u);      /* count */
    wr_u2(fp, 7u);      /* record id */
    wr_u2(fp, 3u);      /* length */
    (void)fwrite("abc", 1u, 3u, fp);
    fclose(fp);
}

/* Write a bare version-1 store with no records at all. */
static void write_empty_legacy_v1(const char *path)
{
    FILE *fp = fopen(path, "wb");

    if (!fp) {
        g_fail++;
        fprintf(stderr, "FAIL cannot create %s\n", path);
        return;
    }
    wr_u1(fp, 'N');
    wr_u1(fp, 'J');
    wr_u1(fp, 'R');
    wr_u1(fp, 'S');
    wr_u2(fp, 1u);
    wr_u2(fp, 0u);
    wr_u2(fp, 1u);
    wr_u2(fp, 0u);
    fclose(fp);
}

static void append_bytes(const char *path, const unsigned char *p, size_t n)
{
    FILE *fp = fopen(path, "ab");
    if (!fp) {
        g_fail++;
        fprintf(stderr, "FAIL cannot append to %s\n", path);
        return;
    }
    (void)fwrite(p, 1u, n, fp);
    fclose(fp);
}

static void write_trailer(const char *path, nj_u8 version, nj_s8 ts)
{
    FILE *fp = fopen(path, "ab");
    if (!fp) {
        g_fail++;
        fprintf(stderr, "FAIL cannot append trailer to %s\n", path);
        return;
    }
    wr_u1(fp, 'N');
    wr_u1(fp, 'J');
    wr_u1(fp, 'M');
    wr_u1(fp, 'T');
    wr_u4(fp, (nj_u4)version);
    wr_u8(fp, (nj_u8)ts);
    fclose(fp);
}

/* ------------------------------------------------------------------ */
/* Case 1: the descriptor and the creation timestamp                   */
/* ------------------------------------------------------------------ */
static void test_create_and_stable(const nj_port *base, const char *dir,
                                   int libc_backend)
{
    const char *name = "create.sav";
    char path[1200];
    nj_port port = make_port(base, dir, libc_backend);
    nj_vm *vm;
    nj_ref store;

    (void)libc_backend;
    store_path(path, sizeof(path), dir, name);
    (void)remove(path);

    clock_reset();
    vm = vm_new(&port);
    if (!vm) {
        return;
    }
    store = open_store(vm, name, 1, "create");
    if (store) {
        expect_lm(vm, store, clock_vm_now(), "right after creation");
        /* Reading it again, with the wall clock moved on, must not move it. */
        clock_advance(2500u);
        expect_lm(vm, store, EPOCH_2040_MS, "a second read, 2.5 s later");
        /* A read-only reopen is still stable. */
        close_store(vm, store, "create");
        store = open_store(vm, name, 0, "create-reopen");
        if (store) {
            clock_advance(2500u);
            expect_lm(vm, store, EPOCH_2040_MS, "reopened 2.5 s later");
            close_store(vm, store, "create-reopen");
        }
    }
    clean(vm, "create-and-stable");
    nj_vm_destroy(vm);
}

/* ------------------------------------------------------------------ */
/* Case 2: mutations move it, reads and failed mutations do not        */
/* ------------------------------------------------------------------ */
static void test_mutations(const nj_port *base, const char *dir)
{
    const char *name = "mutate.sav";
    char path[1200];
    nj_port port = make_port(base, dir, 1);
    nj_vm *vm;
    nj_ref store;
    nj_s4 id1, id2;

    store_path(path, sizeof(path), dir, name);
    (void)remove(path);

    clock_reset();
    vm = vm_new(&port);
    if (!vm) {
        return;
    }
    store = open_store(vm, name, 1, "mutate");
    if (store) {
        expect_lm(vm, store, EPOCH_2040_MS, "creation");

        clock_advance(1000u);
        id1 = add_bytes(vm, store, "a", 1u, "mutate/add-1");
        OK_MSG(id1 == 1, "first record id is 1, got %ld", (long)id1);
        expect_lm(vm, store, EPOCH_2040_MS + 1000, "after addRecord");

        clock_advance(500u);
        id2 = add_bytes(vm, store, "bb", 2u, "mutate/add-2");
        OK_MSG(id2 == 2, "second record id is 2, got %ld", (long)id2);
        expect_lm(vm, store, EPOCH_2040_MS + 1500, "after second addRecord");

        clock_advance(500u);
        set_bytes(vm, store, id1, "A", 1u, "mutate/set");
        expect_lm(vm, store, EPOCH_2040_MS + 2000, "after setRecord");

        clock_advance(500u);
        delete_record(vm, store, id2, "mutate/delete");
        expect_lm(vm, store, EPOCH_2040_MS + 2500, "after deleteRecord");

        /* Reads are not mutations. */
        clock_advance(500u);
        OK_MSG(num_records(vm, store, "mutate/read") == 1,
               "one record survives the delete");
        expect_lm(vm, store, EPOCH_2040_MS + 2500, "after reads");

        /* Failed mutations changed nothing, so they must not move it. */
        clock_advance(500u);
        {
            nj_value a[4];
            nj_ref arr = nj_byte_array_new(vm, 1u);
            nj_status st;
            if (arr) {
                arr->u.bytes[0] = 'Z';
            }
            a[0] = vint(9999);
            a[1] = vref(arr);
            a[2] = vint(0);
            a[3] = vint(1);
            st = nj_call(vm, store, RS, "setRecord", "(I[BII)V", a, NULL);
            OK_MSG(st != NJ_OK && nj_has_exception(vm),
                   "setRecord(9999) is rejected");
            nj_clear_exception(vm);
        }
        expect_lm(vm, store, EPOCH_2040_MS + 2500,
                  "after a rejected setRecord");
        {
            nj_value a[1];
            nj_status st;
            a[0] = vint(9999);
            st = nj_call(vm, store, RS, "deleteRecord", "(I)V", a, NULL);
            OK_MSG(st != NJ_OK && nj_has_exception(vm),
                   "deleteRecord(9999) is rejected");
            nj_clear_exception(vm);
        }
        clock_advance(500u);
        expect_lm(vm, store, EPOCH_2040_MS + 2500,
                  "after a rejected deleteRecord");

        /* Closing rewrites the file, but rewriting is not a mutation. */
        close_store(vm, store, "mutate/close");
        clock_advance(1000u);
        store = open_store(vm, name, 0, "mutate/reopen");
        if (store) {
            expect_lm(vm, store, EPOCH_2040_MS + 2500,
                      "reopened after close");
            close_store(vm, store, "mutate/reopen-close");
        }
    }
    clean(vm, "mutations");
    nj_vm_destroy(vm);
}

/* ------------------------------------------------------------------ */
/* Case 3: across a VM restart (the C API's reboot)                    */
/* ------------------------------------------------------------------ */
static void test_reboot(const nj_port *base, const char *dir)
{
    const char *name = "reboot.sav";
    char path[1200];
    nj_port port = make_port(base, dir, 1);
    nj_vm *vm;
    nj_ref store;
    nj_s8 saved;

    store_path(path, sizeof(path), dir, name);
    (void)remove(path);

    clock_reset();
    vm = vm_new(&port);
    if (!vm) {
        return;
    }
    store = open_store(vm, name, 1, "reboot");
    if (!store) {
        clean(vm, "reboot");
        nj_vm_destroy(vm);
        return;
    }
    clock_advance(4000u);
    (void)add_bytes(vm, store, "keep", 4u, "reboot/write");
    saved = EPOCH_2040_MS + 4000;
    expect_lm(vm, store, saved, "before reboot");
    close_store(vm, store, "reboot/close");
    clean(vm, "reboot/before-restart");
    nj_vm_destroy(vm);

    /* A restart much later in wall-clock time.  "Now" must not leak in. */
    clock_advance(500000u);
    vm = vm_new(&port);
    if (!vm) {
        return;
    }
    OK_MSG(saved > (nj_s8)2147483647LL,
           "the saved value does not fit in 32 bits: %lld", (long long)saved);
    store = open_store(vm, name, 0, "reboot/restart");
    if (store) {
        nj_s8 now = clock_vm_now();
        expect_lm(vm, store, saved, "after a restart");
        OK_MSG(now != saved,
               "the fresh VM's now (%lld) is not the lastModified",
               (long long)now);
        OK_MSG(num_records(vm, store, "reboot/restart") == 1,
               "the record survived the restart");
        close_store(vm, store, "reboot/restart-close");
    }
    clean(vm, "reboot");
    nj_vm_destroy(vm);
}

/* ------------------------------------------------------------------ */
/* Case 4: closed store throws, and deleteRecordStore removes the file */
/* ------------------------------------------------------------------ */
static void test_closed_and_delete(const nj_port *base, const char *dir)
{
    const char *name = "closed.sav";
    char path[1200];
    nj_port port = make_port(base, dir, 1);
    nj_vm *vm;
    nj_ref store;

    store_path(path, sizeof(path), dir, name);
    (void)remove(path);

    clock_reset();
    vm = vm_new(&port);
    if (!vm) {
        return;
    }
    store = open_store(vm, name, 1, "closed");
    if (store) {
        expect_lm(vm, store, EPOCH_2040_MS, "created");
        close_store(vm, store, "closed/close");
        expect_lm_throws(vm, store, "RecordStoreNotOpenException",
                         "getLastModified on a closed store");
        OK_MSG(file_exists(path), "the store file exists after close");
        delete_store(vm, name, "closed/delete");
        OK_MSG(!file_exists(path), "deleteRecordStore removed the file");
    }
    clean(vm, "closed-and-delete");
    nj_vm_destroy(vm);
}

/* ------------------------------------------------------------------ */
/* Case 4a: access modes survive the packing and reject invalid values  */
/* ------------------------------------------------------------------ */
static void test_access_mode(const nj_port *base, const char *dir)
{
    const char *name = "modes.sav";
    char path[1200];
    nj_port port = make_port(base, dir, 0);
    nj_vm *vm;
    nj_ref store;
    nj_s4 mode = -1;
    nj_status st;

    store_path(path, sizeof(path), dir, name);
    (void)remove(path);
    clock_reset();
    vm = vm_new(&port);
    if (!vm) {
        return;
    }
    store = open_store(vm, name, 1, "mode/create");
    if (!store) {
        clean(vm, "mode/create");
        nj_vm_destroy(vm);
        return;
    }
    OK_MSG(get_mode(vm, store, &mode, "mode/fresh") && mode == 0,
           "a new store opens in AUTHMODE_PRIVATE (0), got %ld", (long)mode);

    st = set_mode(vm, store, 1);
    OK_MSG(st == NJ_OK && !nj_has_exception(vm) && get_mode(vm, store, &mode,
           "mode/any") && mode == 1,
           "setMode(AUTHMODE_ANY) is reported by getMode() as 1, got %ld",
           (long)mode);

    /* An invalid authmode is rejected and must leave the stored value alone. */
    st = set_mode(vm, store, 7);
    OK_MSG(st != NJ_OK && nj_has_exception(vm),
           "setMode(7) fails");
    OK_MSG(nj_has_exception(vm) &&
           strstr(nj_exception_describe(vm), "IllegalArgumentException"),
           "setMode(7) is an IllegalArgumentException (%s)",
           nj_has_exception(vm) ? nj_exception_describe(vm) : "none");
    nj_clear_exception(vm);
    OK_MSG(get_mode(vm, store, &mode, "mode/after-invalid") && mode == 1,
           "a rejected setMode() preserves the current mode, got %ld",
           (long)mode);

    st = set_mode(vm, store, 0);
    OK_MSG(st == NJ_OK && !nj_has_exception(vm) && get_mode(vm, store, &mode,
           "mode/private") && mode == 0,
           "setMode(AUTHMODE_PRIVATE) is reported by getMode() as 0, got %ld",
           (long)mode);

    close_store(vm, store, "mode/close");
    clean(vm, "mode/close");
    nj_vm_destroy(vm);
}

/* ------------------------------------------------------------------ */
/* Case 4b: a failed save neither moves the timestamp nor lies          */
/* ------------------------------------------------------------------ */
static void test_write_failure_keeps_timestamp(const nj_port *base,
                                               const char *dir)
{
    const char *name = "fault.sav";
    char path[1200];
    nj_port port = make_port(base, dir, 0);
    nj_vm *vm;
    nj_ref store;
    nj_s8 saved;

    store_path(path, sizeof(path), dir, name);
    (void)remove(path);

    /* A healthy store with one record and a known timestamp. */
    clock_reset();
    vm = vm_new(&port);
    if (!vm) {
        return;
    }
    store = open_store(vm, name, 1, "fault/setup");
    if (!store) {
        clean(vm, "fault/setup");
        nj_vm_destroy(vm);
        return;
    }
    clock_advance(1000u);
    (void)add_bytes(vm, store, "keep", 4u, "fault/setup-add");
    saved = EPOCH_2040_MS + 1000;
    expect_lm(vm, store, saved, "fault/setup");
    close_store(vm, store, "fault/setup");
    clean(vm, "fault/setup");
    nj_vm_destroy(vm);

    /* Reopen through a port where opening for write fails.  The read clock has
     * moved well past the saved value, so "now" would be visibly different. */
    g_inject_base = *base;
    {
        nj_port bad = make_port(base, dir, 0);
        bad.file_open = inject_open;
        clock_advance(4000u);
        vm = vm_new(&bad);
        if (!vm) {
            return;
        }
        store = open_store(vm, name, 0, "fault/reopen");
        if (store) {
            expect_lm(vm, store, saved, "fault/reopen after clock moved on");
            clock_advance(2000u);
            {
                nj_ref arr = nj_byte_array_new(vm, 1u);
                nj_value a[3], ret;
                nj_status st;
                if (arr) {
                    arr->u.bytes[0] = 'X';
                }
                a[0] = vref(arr);
                a[1] = vint(0);
                a[2] = vint(1);
                ret.bits = 0u;
                st = nj_call(vm, store, RS, "addRecord", "([BII)I", a, &ret);
                OK_MSG(st != NJ_OK && nj_has_exception(vm),
                       "a mutation that cannot be written reports failure");
                OK_MSG(nj_has_exception(vm) &&
                       strstr(nj_exception_describe(vm), "RecordStoreException"),
                       "the failure is a RecordStoreException (%s)",
                       nj_has_exception(vm) ? nj_exception_describe(vm)
                                            : "none");
                nj_clear_exception(vm);
            }
            expect_lm(vm, store, saved,
                      "failed mutation must not move lastModified");
        }
        clean(vm, "fault/failed-write");
        /* No closeRecordStore(): closing also saves, which this port refuses;
         * dropping the VM is the crash the timestamp must survive. */
        nj_vm_destroy(vm);
    }

    /* A healthy VM must see the last SUCCESSFUL write, not the failed one. */
    clock_advance(3000u);
    {
        nj_port good = make_port(base, dir, 0);
        vm = vm_new(&good);
        if (!vm) {
            return;
        }
        store = open_store(vm, name, 0, "fault/recover");
        if (store) {
            expect_lm(vm, store, saved, "failed mutation not persisted");
            OK_MSG(num_records(vm, store, "fault/recover") == 1,
                   "the failed mutation was not persisted");
            close_store(vm, store, "fault/recover");
        }
        clean(vm, "fault/recover");
        nj_vm_destroy(vm);
    }
}

/* ------------------------------------------------------------------ */
/* Case 5: legacy files and trailer variants                           */
/* ------------------------------------------------------------------ */
static void test_legacy_and_variants(const nj_port *base, const char *dir)
{
    enum { VARIANT_NONE, VARIANT_TAG3, VARIANT_TAG4, VARIANT_VERSION_ONLY,
           VARIANT_VERSION_TRUNC, VARIANT_UNKNOWN_VERSION, VARIANT_EXTRA,
           VARIANT_VALID, VARIANT_COUNT };
    static const char *names[VARIANT_COUNT] = {
        "legacy.none", "legacy.tag3", "legacy.tag4", "legacy.veronly",
        "legacy.vertrunc", "legacy.unknownver", "legacy.extra", "legacy.valid"
    };
    static const long MTIME = 1600000000L;   /* 2020-09-13T12:26:40Z */
    static const nj_s8 MTIME_MS = 1600000000000LL;
    static const nj_s8 TRAILER_TS = 1234567890123LL;
    nj_port port = make_port(base, dir, 1);
    nj_vm *vm;
    int v;

    clock_reset();
    vm = vm_new(&port);
    if (!vm) {
        return;
    }

    for (v = 0; v < VARIANT_COUNT; v++) {
        char path[1200];
        unsigned char buf[8];
        nj_ref store;
        char where[64];

        store_path(path, sizeof(path), dir, names[v]);
        (void)remove(path);
        write_legacy_v1(path);

        switch (v) {
        case VARIANT_TAG3:
            buf[0] = 'N'; buf[1] = 'J'; buf[2] = 'M';
            append_bytes(path, buf, 3u);
            break;
        case VARIANT_TAG4:
            buf[0] = 'N'; buf[1] = 'J'; buf[2] = 'M'; buf[3] = 'T';
            append_bytes(path, buf, 4u);
            break;
        case VARIANT_VERSION_ONLY:
            {
                unsigned char vbe[8] = { 'N', 'J', 'M', 'T', 0, 0, 0, 1 };
                append_bytes(path, vbe, 8u);
            }
            break;
        case VARIANT_VERSION_TRUNC:
            buf[0] = 'N'; buf[1] = 'J'; buf[2] = 'M'; buf[3] = 'T';
            append_bytes(path, buf, 4u);
            {
                unsigned char vbe[4] = { 0, 0, 0, 1 };
                append_bytes(path, vbe, 4u);
            }
            {
                unsigned char half[4] = { 0xFF, 0xFF, 0xFF, 0xFF };
                append_bytes(path, half, 4u);   /* half a u64 */
            }
            break;
        case VARIANT_UNKNOWN_VERSION:
            write_trailer(path, 7u, TRAILER_TS);
            break;
        case VARIANT_EXTRA:
            write_trailer(path, 1u, TRAILER_TS);
            buf[0] = 0x55u;
            append_bytes(path, buf, 1u);
            break;
        case VARIANT_VALID:
            write_trailer(path, 1u, TRAILER_TS);
            break;
        default:
            break;
        }
        set_mtime(path, MTIME);

        snprintf(where, sizeof(where), "legacy variant %d", v);
        store = open_store(vm, names[v], 0, where);
        if (!store) {
            continue;
        }
        OK_MSG(num_records(vm, store, where) == 1,
               "%s: the legacy record survived", where);
        /* The ONLY case that can know a real timestamp is a well-formed
         * trailer.  Everything else is the mtime fallback, never now. */
        if (v == VARIANT_VALID) {
            expect_lm(vm, store, TRAILER_TS, where);
        } else {
            expect_lm(vm, store, MTIME_MS, where);
        }
        close_store(vm, store, where);
        (void)remove(path);
    }

    /* An empty bare-v1 store: records empty, timestamp from mtime. */
    {
        char path[1200];
        nj_ref store;
        store_path(path, sizeof(path), dir, "legacy.empty");
        (void)remove(path);
        write_empty_legacy_v1(path);
        set_mtime(path, MTIME);
        store = open_store(vm, "legacy.empty", 0, "legacy empty");
        if (store) {
            OK_MSG(num_records(vm, store, "legacy empty") == 0,
                   "an empty legacy store has no records");
            expect_lm(vm, store, MTIME_MS, "legacy empty store");
            close_store(vm, store, "legacy empty");
            /* The first save after the upgrade writes the trailer with the
             * fallback value, so the next read is exact without re-stat. */
            store = open_store(vm, "legacy.empty", 0, "legacy empty reopen");
            if (store) {
                expect_lm(vm, store, MTIME_MS, "legacy empty after rewrite");
                close_store(vm, store, "legacy empty reopen");
            }
        }
        (void)remove(path);
    }

    clean(vm, "legacy-and-variants");
    nj_vm_destroy(vm);
}

/* ------------------------------------------------------------------ */
/* Case 6: an old decoder still reads a file this build writes         */
/* ------------------------------------------------------------------ */
static void test_old_decoder_reads_new_file(const nj_port *base,
                                            const char *dir)
{
    const char *name = "olddec.sav";
    char path[1200];
    nj_port port = make_port(base, dir, 1);
    nj_vm *vm;
    nj_ref store;
    old_view view;
    nj_s8 saved;

    store_path(path, sizeof(path), dir, name);
    (void)remove(path);

    clock_reset();
    vm = vm_new(&port);
    if (!vm) {
        return;
    }
    store = open_store(vm, name, 1, "olddecoder");
    if (store) {
        (void)add_bytes(vm, store, "hello", 5u, "olddecoder/add-1");
        clock_advance(2000u);
        (void)add_bytes(vm, store, "world", 5u, "olddecoder/add-2");
        clock_advance(3000u);
        set_bytes(vm, store, 1, "HELLO", 5u, "olddecoder/set");
        saved = EPOCH_2040_MS + 5000;
        expect_lm(vm, store, saved, "olddecoder before close");
        close_store(vm, store, "olddecoder");

        old_v1_decode(path, &view);
        OK_MSG(view.ok, "the old v1 decoder accepts the new file");
        OK_MSG(view.version == 1, "the header still says version 1, got %d",
               view.version);
        OK_MSG(view.reserved == 0, "the reserved word stays zero");
        OK_MSG(view.count == 2, "the old decoder sees both records, got %d",
               view.count);
        OK_MSG(view.first_id == 1 && strcmp(view.first, "HELLO") == 0,
               "the old decoder sees the updated first record (%lu, %s)",
               (unsigned long)view.first_id, view.first);
        OK_MSG(view.has_trailer, "the trailer is present for the new reader");
        OK_MSG(view.trailer_ts == saved,
               "the trailer carries %lld, saved %lld",
               (long long)view.trailer_ts, (long long)saved);
        OK_MSG(file_size(path) == 12 + 2 * 9 + 16,
               "file is v1 records plus exactly one trailer (%ld bytes)",
               file_size(path));
    }
    clean(vm, "old-decoder");
    nj_vm_destroy(vm);
}

/* ------------------------------------------------------------------ */
/* Case 7: the port's file hooks (not the libc fallback)               */
/* ------------------------------------------------------------------ */
static void test_port_backend(const nj_port *base, const char *dir)
{
    const char *name = "porthooks.sav";
    char path[1200];
    nj_port port = make_port(base, dir, 0);
    nj_vm *vm;
    nj_ref store;
    old_view view;

    store_path(path, sizeof(path), dir, name);
    (void)remove(path);

    clock_reset();
    vm = vm_new(&port);
    if (!vm) {
        return;
    }
    store = open_store(vm, name, 1, "port-backend");
    if (store) {
        clock_advance(700u);
        (void)add_bytes(vm, store, "hook", 4u, "port-backend/add");
        expect_lm(vm, store, EPOCH_2040_MS + 700, "port backend write");
        close_store(vm, store, "port-backend");
        clock_advance(9000u);

        /* Reopen after a VM restart, still through the hooks. */
        nj_vm_destroy(vm);
        vm = vm_new(&port);
        if (!vm) {
            return;
        }
        store = open_store(vm, name, 0, "port-backend/restart");
        if (store) {
            expect_lm(vm, store, EPOCH_2040_MS + 700,
                      "port backend after restart");
            close_store(vm, store, "port-backend/restart");
        }
        old_v1_decode(path, &view);
        OK_MSG(view.ok && view.count == 1 && view.has_trailer &&
               view.trailer_ts == EPOCH_2040_MS + 700,
               "the hook backend wrote a real v1 file with the trailer");
    }
    clean(vm, "port-backend");
    nj_vm_destroy(vm);
}

/* ------------------------------------------------------------------ */
int main(void)
{
    static const char *subdirs[] = {
        "getlastmodified-libc", "getlastmodified-port"
    };
    const char *base_dir = getenv("NANO_J2ME_RMS");
    nj_port base_port;
    char dir[1024];
    unsigned i;

    if (!base_dir || !*base_dir) {
        base_dir = "tests/out/rms";
    }
    mkdir_p(base_dir);

    if (nj_port_headless(&base_port) != NJ_OK) {
        fprintf(stderr, "FAIL cannot create the headless port\n");
        return 1;
    }

    /* Both the libc fallback and the port hooks get the same coverage; only
     * the variant-file cases need host-visible paths, and the headless hooks
     * are host paths themselves. */
    for (i = 0; i < sizeof(subdirs) / sizeof(subdirs[0]); i++) {
        snprintf(dir, sizeof(dir), "%s/%s", base_dir, subdirs[i]);
        mkdir_p(dir);
        test_create_and_stable(&base_port, dir, (int)(i == 0u));
        test_mutations(&base_port, dir);
        test_access_mode(&base_port, dir);
        test_reboot(&base_port, dir);
        test_closed_and_delete(&base_port, dir);
        test_legacy_and_variants(&base_port, dir);
        test_old_decoder_reads_new_file(&base_port, dir);
    }
    snprintf(dir, sizeof(dir), "%s/getlastmodified-fault", base_dir);
    mkdir_p(dir);
    test_write_failure_keeps_timestamp(&base_port, dir);
    test_port_backend(&base_port, dir);

    nj_port_shutdown(&base_port);

    printf("test_rms: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
