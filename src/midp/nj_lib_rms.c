/*
 * nano_j2me - nj_lib_rms.c
 *
 * javax.microedition.rms.RecordStore.
 *
 * This is the only part of the class library that persists anything, so it is
 * also the only part that has to be careful about what "restart" means.  A
 * high-score table that survives a process restart is the whole point of RMS,
 * and a game that silently loses the player's progress is worse than one that
 * refuses to save.
 *
 * ---------------------------------------------------------------------------
 * REPRESENTATION
 * ---------------------------------------------------------------------------
 * A store is an ordinary Java object holding two parallel Vectors - the record
 * payloads and their ids - plus the next id to hand out.  Record ids are
 * allowed to have gaps (deleteRecord leaves one), which is why the ids live in
 * their own Vector rather than being the array index.  All of it belongs to the
 * garbage collector, so nothing here can outlive nj_vm_destroy().
 *
 * ---------------------------------------------------------------------------
 * ON-DISK FORMAT
 * ---------------------------------------------------------------------------
 * One file per store, "<rms_dir>/<name>.rms", big-endian to match the
 * DataOutputStream convention every J2ME developer already knows:
 *
 *      "NJRS"          4 bytes   magic
 *      version         u2        currently 1
 *      reserved        u2        written as 0, ignored on read
 *      nextId          u2
 *      count           u2
 *      per record:     u2 id, u2 length, length bytes
 *
 * followed, optionally, by a trailer that older readers never look for:
 *
 *      "NJMT"          4 bytes   trailer tag
 *      trailerVersion  u32        currently 1
 *      lastModified    u64        epoch milliseconds of the last mutation
 *
 * A file whose magic or version does not match is treated as absent rather
 * than as an error: losing one high-score table is better than refusing to
 * start the game, and a half-written file from a power cut is the realistic
 * way this happens on a phone.
 *
 * The trailer exists because RecordStore.getLastModified() has to answer with
 * the time of the last *mutation*, not with the current time, and it has to
 * survive a restart.  The records stay exactly where version 1 put them, so a
 * build from before this trailer existed still reads (and rewrites) the store
 * unchanged; if such a build rewrites it, the trailer is dropped and the
 * timestamp falls back to "unknown" - the data, which is the part that must
 * not be lost, is never touched.  A bare version 1 file is still read: the
 * timestamp reported for it is the host file's modification time when the
 * platform can stat the file, or 0 for "unknown" - never the current time,
 * which would be a lie that games comparing it against the wall clock act on.
 * The next mutation appends the trailer and the value becomes exact.
 *
 * A record longer than 65535 bytes cannot carry its length in the u2 record
 * header, so the header stores length 0 and the payload moves into an
 * overflow section between the records and the trailer:
 *
 *      "NJBG"          4 bytes   overflow tag
 *      bigVersion      u32       currently 1
 *      bigCount        u32
 *      per big record: u2 id, u2 reserved (0), u32 length, length bytes
 *
 * An older build keeps reading the record area - that one record comes back
 * empty, which is the same silent truncation it did before this section
 * existed - and never looks at the overflow, so every other record is
 * exactly where it was and parses correctly.  A build from before the trailer
 * ignores both sections the same way.  The section is omitted entirely when
 * no record is long, so ordinary stores stay byte-for-byte what they were.
 *
 * ---------------------------------------------------------------------------
 * I/O
 * ---------------------------------------------------------------------------
 * Through the port's file_* hooks when the port supplies them (an ESP32 writing
 * to a SPIFFS partition) and through the C library otherwise (a desktop).  The
 * path comes from nj_port.rms_dir, or the working directory.
 */
#include "nj_midp.h"

#if NJ_USE_LIBC
#  include <string.h>
#  include <stdio.h>
#  if !defined(_WIN32)
#    include <dirent.h>
#    include <sys/stat.h>
#    include <errno.h>
#  endif
#endif

#define NJFN(name) NJ_NATIVE_FN(name)
#define NJ_CALL(name, vm, th, args, ret) name((vm), (th), (args), (ret))
#define NJ_UNUSED(x) ((void)(x))

#define NJ_RS_MAGIC0 'N'
#define NJ_RS_MAGIC1 'J'
#define NJ_RS_MAGIC2 'R'
#define NJ_RS_MAGIC3 'S'
#define NJ_RS_VERSION 1u
/* Optional end-of-file trailer.  Version 1 records stay byte-for-byte what
 * they were, so an older build can still read the file; it simply does not
 * know the trailer is there. */
#define NJ_RS_TRAILER0 'N'
#define NJ_RS_TRAILER1 'J'
#define NJ_RS_TRAILER2 'M'
#define NJ_RS_TRAILER3 'T'
#define NJ_RS_TRAILER_VERSION 1u
#define NJ_RS_TRAILER_SIZE 16u   /* tag 4 + version 4 + timestamp 8 */
/* Optional record-overflow section: payloads that do not fit the u2 length in
 * the record area.  Same append-only trick as the trailer. */
#define NJ_RS_BIG0 'N'
#define NJ_RS_BIG1 'J'
#define NJ_RS_BIG2 'B'
#define NJ_RS_BIG3 'G'
#define NJ_RS_BIG_VERSION 1u
#define NJ_RS_BIG_MAX 4096u      /* implausible counts are treated as corrupt */
#define NJ_RS_RECORD_MAX (1024u * 1024u)  /* sanity bound on one record */
/* The timestamp field.  It replaces the old rsModified boolean, which was
 * written by every save and read by nobody. */
#define NJ_F_RS_LASTMOD "rsLastModified"   /* J */

/* ------------------------------------------------------------------ */
/* Small helpers                                                       */
/* ------------------------------------------------------------------ */
/* nj_vm.h has no static-call-returning-ref helper; this keeps it local. */
static nj_ref nj_call_static_ref_local(nj_vm *vm, const char *cls,
                                       const char *name, const char *desc,
                                       nj_value *args)
{
    nj_value r;
    r.bits = 0u;
    if (nj_call_static(vm, cls, name, desc, args, &r) != NJ_OK) {
        return NULL;
    }
    return r.r;
}

static nj_field *rsf(nj_vm *vm, const char *name, const char *desc)
{
    return nj_field_lookup(vm, NJ_C_RECORDSTORE, name, desc, 0);
}

static nj_ref rs_get(nj_vm *vm, nj_ref rs, const char *name, const char *desc)
{
    nj_field *f = rsf(vm, name, desc);
    return f ? nj_field_read_ref(vm, rs, f) : NULL;
}

static nj_s4 rs_geti(nj_vm *vm, nj_ref rs, const char *name)
{
    nj_field *f = rsf(vm, name, "I");
    if (!f) {
        f = rsf(vm, name, "Z");
    }
    return f ? nj_field_read_int(vm, rs, f) : 0;
}

static void rs_seti(nj_vm *vm, nj_ref rs, const char *name, nj_s4 v)
{
    /* Fields come in two shapes here and the descriptor is part of the key:
     * looking up "rsOpen" with desc "I" finds nothing and silently writes
     * nowhere, which is exactly how openRecordStore came back usable-looking
     * but not open.  Try the int form, then the boolean form. */
    nj_field *f = rsf(vm, name, "I");
    if (!f) {
        f = rsf(vm, name, "Z");
    }
    if (f) {
        nj_field_write_int(vm, rs, f, v);
    }
}

static nj_s8 rs_getl(nj_vm *vm, nj_ref rs, const char *name)
{
    nj_field *f = rsf(vm, name, "J");
    return f ? nj_field_read_long(vm, rs, f) : 0;
}

static void rs_setl(nj_vm *vm, nj_ref rs, const char *name, nj_s8 v)
{
    nj_field *f = rsf(vm, name, "J");
    if (f) {
        nj_field_write_long(vm, rs, f, v);
    }
}

/* The enumeration classes.  Defined here, above their first use, and
 * NOT next to the tables at the bottom where they were first written -
 * a macro that is used before it is defined is not a macro yet. */
#define NJ_C_RENUM "javax/microedition/rms/RecordEnumeration"
#define NJ_C_RFILTER "javax/microedition/rms/RecordFilter"
#define NJ_C_RCOMP "javax/microedition/rms/RecordComparator"
/* Implementation state lives in Java objects, without changing the public
 * builtin layouts (including their snapshot ABI). Neither class has statics. */
#define NJ_C_RMETA "nj2me/rms/Records"
#define NJ_C_RENUM_IMPL "nj2me/rms/Enumeration"

/*
 * The rsf/rs_get/rs_geti family looks fields up on RecordStore and nowhere
 * else, so the enumeration's OWN fields were invisible to them - the same shape
 * of bug as the field helpers that hardcoded the Player class and the "I"
 * descriptor.  These take the class explicitly.
 */
static nj_field *renumf(nj_vm *vm, const char *name, const char *desc)
{
    return nj_field_lookup(vm, NJ_C_RENUM, name, desc, 0);
}

static nj_ref renum_get(nj_vm *vm, nj_ref o, const char *name, const char *desc)
{
    nj_field *f = renumf(vm, name, desc);
    return f ? nj_field_read_ref(vm, o, f) : NULL;
}

static nj_s4 renum_geti(nj_vm *vm, nj_ref o, const char *name)
{
    nj_field *f = renumf(vm, name, "I");
    if (!f) {
        f = renumf(vm, name, "Z");
    }
    return f ? nj_field_read_int(vm, o, f) : 0;
}

static void renum_seti(nj_vm *vm, nj_ref o, const char *name, nj_s4 v)
{
    nj_field *f = renumf(vm, name, "I");
    if (!f) {
        f = renumf(vm, name, "Z");
    }
    if (f) {
        nj_field_write_int(vm, o, f, v);
    }
}

static void renum_setr(nj_vm *vm, nj_ref o, const char *name,
                       const char *desc, nj_ref v)
{
    nj_field *f = renumf(vm, name, desc);
    if (f) {
        nj_field_write_ref(vm, o, f, v);
    }
}

/* java.util.Vector, called through the usual virtual dispatch. */
static nj_s4 vec_size(nj_vm *vm, nj_ref v)
{
    return nj_call_int(vm, v, "java/util/Vector", "size", "()I", NULL);
}

static nj_ref vec_at(nj_vm *vm, nj_ref v, nj_s4 i)
{
    nj_value a1[2];
    a1[0].i = i;
    return nj_call_ref(vm, v, "java/util/Vector", "elementAt",
                       "(I)Ljava/lang/Object;", a1);
}

static int vec_add(nj_vm *vm, nj_ref v, nj_ref o)
{
    nj_value a1[2];
    a1[0].r = o;
    return nj_call(vm, v, "java/util/Vector", "addElement",
                   "(Ljava/lang/Object;)V", a1, NULL) == NJ_OK;
}

static int vec_set(nj_vm *vm, nj_ref v, nj_s4 i, nj_ref o)
{
    nj_value a2[3];
    a2[0].r = o;
    a2[1].i = i;
    return nj_call(vm, v, "java/util/Vector", "setElementAt",
                   "(Ljava/lang/Object;I)V", a2, NULL) == NJ_OK;
}

static int vec_del(nj_vm *vm, nj_ref v, nj_s4 i)
{
    nj_value a1[2];
    a1[0].i = i;
    return nj_call(vm, v, "java/util/Vector", "removeElementAt", "(I)V",
                   a1, NULL) == NJ_OK;
}

/* Index of the first matching id, including in old files with unordered or
 * repeated ids. The store owns a builtin Vector of builtin Integers: reading
 * their fields avoids two Java/native bridges per comparison. There are no
 * allocations or callbacks while the backing array is in use. Unusual object
 * types retain the original virtual calls, without retaining array pointers
 * across them. No sortedness assumption or persistent lookup cache is needed. */
static nj_s4 rs_find_ids(nj_vm *vm, nj_ref ids, nj_s4 id)
{
    nj_field *data, *count, *value;
    nj_s4 n, i;

    if (!ids) return -1;
    data = nj_field_lookup(vm, "java/util/Vector", "elementData",
                           "[Ljava/lang/Object;", 0);
    count = nj_field_lookup(vm, "java/util/Vector", "elementCount", "I", 0);
    value = nj_field_lookup(vm, "java/lang/Integer", "value", "I", 0);
    if (data && count && value && ids->kind == NJ_OBJ_PLAIN &&
        ids->cls == data->owner) {
        nj_ref array = nj_field_read_ref_fast(vm, ids, data);
        n = nj_field_read_int_fast(vm, ids, count);
        if (n == 0) return -1;
        if (n > 0 && array && array->kind == NJ_OBJ_ARRAY &&
            array->aflags == NJ_ARRAY_T_REF && array->u.refs &&
            (nj_u4)n <= array->length) {
            for (i = 0; i < n; i++) {
                nj_ref o = array->u.refs[i];
                if (!o) continue;
                if (o->kind != NJ_OBJ_PLAIN || o->cls != value->owner)
                    break;
                if (nj_field_read_int_fast(vm, o, value) == id) return i;
            }
            if (i == n) return -1;
        }
    }
    n = vec_size(vm, ids);
    for (i = 0; i < n; i++) {
        nj_ref o = vec_at(vm, ids, i);
        if (o && nj_call_int(vm, o, "java/lang/Integer", "intValue", "()I",
                             NULL) == id) {
            return i;
        }
    }
    return -1;
}

static nj_s4 rs_find(nj_vm *vm, nj_ref rs, nj_s4 id)
{
    return rs_find_ids(vm, rs_get(vm, rs, NJ_F_RS_IDS, "Ljava/util/Vector;"), id);
}

static nj_ref rs_int_obj(nj_vm *vm, nj_s4 v)
{
    nj_value a1[2];
    a1[0].i = v;
    return nj_call_static_ref_local(vm, "java/lang/Integer", "valueOf",
                                    "(I)Ljava/lang/Integer;", a1);
}

static int rs_is_open(nj_vm *vm, nj_ref rs)
{
    return (int)rs_geti(vm, rs, NJ_F_RS_OPEN);
}

static nj_status rs_require_open(nj_vm *vm, nj_ref rs)
{
    return rs_is_open(vm, rs) ? NJ_OK :
        nj_throw(vm, "javax/microedition/rms/RecordStoreNotOpenException", NULL);
}

/* nj_root returns its input even if the host root table cannot grow. */
static int rs_root(nj_vm *vm, nj_ref ref)
{
    nj_u4 roots;
    if (!ref) return 1;  /* empty records are valid comparator operands */
    roots = nj_root_mark(vm);
    nj_root(vm, ref);
    return nj_root_mark(vm) == roots + 1u;
}

/* Only use an implementation field after checking the actual object's class.
 * In particular, snapshots can still contain the original base classes. */
static int rms_is(nj_ref o, const char *name)
{
    nj_class *c = o ? o->cls : NULL;
    for (; c; c = c->super) {
        if (strcmp(c->name, name) == 0) return 1;
    }
    return 0;
}

static nj_ref rms_getref(nj_vm *vm, nj_ref o, const char *cls,
                         const char *name, const char *desc)
{
    nj_field *f = nj_field_lookup(vm, cls, name, desc, 0);
    return f ? nj_field_read_ref(vm, o, f) : NULL;
}

static void rms_setref(nj_vm *vm, nj_ref o, const char *cls,
                       const char *name, const char *desc, nj_ref value)
{
    nj_field *f = nj_field_lookup(vm, cls, name, desc, 0);
    if (f) nj_field_write_ref(vm, o, f, value);
}

static nj_s4 rms_getint(nj_vm *vm, nj_ref o, const char *cls, const char *name)
{
    nj_field *f = nj_field_lookup(vm, cls, name, "I", 0);
    return f ? nj_field_read_int(vm, o, f) : 0;
}

static void rms_setint(nj_vm *vm, nj_ref o, const char *cls,
                       const char *name, nj_s4 value)
{
    nj_field *f = nj_field_lookup(vm, cls, name, "I", 0);
    if (f) nj_field_write_int(vm, o, f, value);
}

/* Upgrade the record Vector, not the RecordStore object. This is also valid
 * for an old restored store: Records really is a Vector subclass, and the
 * existing record arrays remain the same objects. Install only after success. */
static nj_ref rs_metadata(nj_vm *vm, nj_ref rs, int create)
{
    nj_ref old = rs_get(vm, rs, NJ_F_RS_RECORDS, "Ljava/util/Vector;");
    nj_ref state = NULL;
    nj_u4 roots;
    nj_s4 i, n;
    if (rms_is(old, NJ_C_RMETA)) return old;
    if (!create) return NULL;
    roots = nj_root_mark(vm);
    if (!rs_root(vm, rs) || !rs_root(vm, old)) goto fail;
    state = nj_object_new(vm, NJ_C_RMETA);
    if (!state || !rs_root(vm, state)) goto fail;
    n = old ? vec_size(vm, old) : 0;
    if (nj_has_exception(vm) || n < 0) goto fail;
    for (i = 0; i < n; i++) {
        nj_ref record = vec_at(vm, old, i);
        if (nj_has_exception(vm) || !vec_add(vm, state, record)) goto fail;
    }
    rms_setint(vm, state, NJ_C_RMETA, "openCount", rs_is_open(vm, rs) ? 1 : 0);
    rms_setref(vm, rs, NJ_C_RECORDSTORE, NJ_F_RS_RECORDS,
               "Ljava/util/Vector;", state);
    nj_root_release(vm, roots);
    return state;
fail:
    nj_root_release(vm, roots);
    if (!nj_has_exception(vm)) (void)nj_throw_oom(vm);
    return NULL;
}

/* Open is infrequent. Query the arena without allocating or running Java so
 * existing objects can be reused without a C side table or new static slots. */
static nj_ref rs_find_open(nj_vm *vm, nj_ref name)
{
    nj_size at = 0u;
    nj_u4 len = nj_string_len(name);
    nj_field *nf = rsf(vm, NJ_F_RS_NAME, "Ljava/lang/String;");
    nj_field *of = rsf(vm, NJ_F_RS_OPEN, "Z");
    if (!nf || !of) return NULL;
    while (at + NJ_BLOCK_HDR <= vm->heap.bump) {
        nj_block *b = (nj_block *)(void *)(vm->heap.base + at);
        nj_ref o;
        if (b->size < NJ_BLOCK_HDR || b->size > vm->heap.bump - at) break;
        o = (nj_ref)(void *)(vm->heap.base + at + NJ_BLOCK_HDR);
        if (b->size >= NJ_BLOCK_HDR + sizeof(nj_object) &&
            b->magic == NJ_BLOCK_MAGIC && o->kind == NJ_OBJ_PLAIN &&
            rms_is(o, NJ_C_RECORDSTORE) && nj_field_read_int(vm, o, of)) {
            nj_ref other = nj_field_read_ref(vm, o, nf);
            if (other && nj_string_len(other) == len &&
                memcmp(nj_string_chars(other), nj_string_chars(name),
                       (size_t)len * sizeof(nj_u2)) == 0) return o;
        }
        at += b->size;
    }
    return NULL;
}

static nj_status re_refresh(nj_vm *vm, nj_ref self);
static nj_status rs_notify_enumerations(nj_vm *vm, nj_ref rs);

/* ------------------------------------------------------------------ */
/* Persistence                                                         */
/* ------------------------------------------------------------------ */
typedef struct rs_writer {
    nj_vm   *vm;
    nj_file  f;
    int      use_port;
#if NJ_USE_LIBC
    FILE    *fp;        /* hosted fallback; the port's hooks win when present */
#endif
    nj_u4    written;
    int      failed;    /* a short write: the file is incomplete */
} rs_writer;

typedef struct rs_reader {
    nj_vm   *vm;
    nj_file  f;
    int      use_port;
#if NJ_USE_LIBC
    FILE    *fp;        /* hosted fallback; the port's hooks win when present */
#endif
    nj_u4    read;
} rs_reader;

/*
 * A store name becomes one file name, not a path.
 *
 * JSR-118 says a store name has to be usable as a file name, but plenty of
 * shipping titles ignore that: 阿尔卑斯牧场物语 opens "/A/AConfig", and the
 * old code turned that into "<rms>/A/AConfig.rms" - a path whose parent
 * directory does not exist, so every save failed with
 * RecordStoreException "cannot open the record store file for writing" and the
 * title then threw NullPointerException on the store it never got.
 *
 * Flattening keeps such a title saving and keeps the RMS directory a security
 * boundary: a name like "../../etc/passwd" becomes ".._.._etc_passwd".  Names
 * that already work - plain ASCII, or the Chinese names several titles use -
 * are not rewritten, so no reader of these files sees a different name than
 * before.
 */
static void rs_path(nj_vm *vm, const char *name, char *out, nj_u4 cap)
{
    const char *dir = (vm->port && vm->port->rms_dir) ? vm->port->rms_dir : ".";
    nj_u4 n = 0u;
    int first = 1;

    while (*dir && n + 1u < cap) {
        out[n++] = *dir++;
    }
    if (n > 0u && out[n - 1u] != '/' && n + 1u < cap) {
        out[n++] = '/';
    }
    while (*name && n + 1u < cap) {
        char c = *name++;
        if (c == '/' || c == '\\') {
            /* A leading separator is part of the game's spelling of the name
             * and carries no meaning for us; a separator in the middle would
             * turn the store into a subdirectory, so it becomes a plain
             * character. */
            if (first) {
                continue;
            }
            c = '_';
        }
        first = 0;
        out[n++] = c;
    }
    if (n + 4u < cap) {
        out[n++] = '.';
        out[n++] = 'r';
        out[n++] = 'm';
        out[n++] = 's';
    }
    out[n] = '\0';
}

/*
 * Modification time of a store file that predates the trailer, in epoch
 * milliseconds.  This is a fallback, not a source of truth: it is only used
 * when the file carries no trailer, and only for a host-backed file (rs_load
 * returns 0 rather than calling this when the port supplies file hooks, since
 * a port filesystem's names are not host paths).  A platform without stat()
 * also gets 0, which means "unknown" - reporting the current time instead
 * would make every old store look modified a moment ago.
 */
static nj_s8 rs_file_mtime_ms(const char *path)
{
#if NJ_USE_LIBC && !defined(_WIN32)
    struct stat st;
    if (stat(path, &st) == 0) {
        return (nj_s8)st.st_mtime * 1000;
    }
#else
    (void)path;
#endif
    return 0;
}

/* ---- writer ---- */
static int w_open(nj_vm *vm, rs_writer *w, const char *path)
{
    memset(w, 0, sizeof(*w));
    w->vm = vm;
    if (vm->port && vm->port->file_open) {
        if (vm->port->file_open(vm->port->ud, path, "wb", &w->f) != NJ_OK) {
            return 0;
        }
        w->use_port = 1;
        return 1;
    }
#if NJ_USE_LIBC
    w->fp = fopen(path, "wb");
    return w->fp != NULL;
#else
    return 0;
#endif
}

/*
 * Append bytes.  A port hook or the C library may accept only part of what it
 * was given (or fail outright); that is recorded instead of being rounded up
 * to success, so rs_save() can report the failure and keep the old timestamp.
 */
static void w_bytes(rs_writer *w, const void *p, nj_u4 n)
{
    if (w->failed || n == 0u) {
        return;
    }
    if (w->use_port) {
        nj_s4 put = w->vm->port->file_write(w->vm->port->ud, &w->f, p, n);
        if (put < 0 || (nj_u4)put != n) {
            w->failed = 1;
            return;
        }
        w->written += n;
    } else {
#if NJ_USE_LIBC
        if (fwrite(p, 1u, n, w->fp) != n) {
            w->failed = 1;
            return;
        }
        w->written += n;
#endif
    }
}

static void w_u2(rs_writer *w, nj_u4 v)
{
    nj_u1 b[2];
    b[0] = (nj_u1)((v >> 8) & 0xFFu);
    b[1] = (nj_u1)(v & 0xFFu);
    w_bytes(w, b, 2u);
}

static void w_u4(rs_writer *w, nj_u4 v)
{
    nj_u1 b[4];
    b[0] = (nj_u1)((v >> 24) & 0xFFu);
    b[1] = (nj_u1)((v >> 16) & 0xFFu);
    b[2] = (nj_u1)((v >> 8) & 0xFFu);
    b[3] = (nj_u1)(v & 0xFFu);
    w_bytes(w, b, 4u);
}

static void w_u8(rs_writer *w, nj_s8 v)
{
    nj_u8 u = (nj_u8)v;
    nj_u1 b[8];
    int i;

    for (i = 7; i >= 0; i--) {
        b[i] = (nj_u1)(u & 0xFFu);
        u >>= 8;
    }
    w_bytes(w, b, 8u);
}

/* Returns 1 when every byte reached the file and the handle closed cleanly. */
static int w_close(rs_writer *w)
{
    int ok = !w->failed;

    if (w->use_port) {
        w->vm->port->file_close(w->vm->port->ud, &w->f);
    }
#if NJ_USE_LIBC
    else if (w->fp) {
        if (fclose(w->fp) != 0) {
            ok = 0;
        }
        w->fp = NULL;
    }
#endif
    return ok;
}

/* ---- reader ---- */
static int r_open(nj_vm *vm, rs_reader *r, const char *path)
{
    memset(r, 0, sizeof(*r));
    r->vm = vm;
    if (vm->port && vm->port->file_open) {
        if (vm->port->file_open(vm->port->ud, path, "rb", &r->f) != NJ_OK) {
            return 0;
        }
        r->use_port = 1;
        return 1;
    }
#if NJ_USE_LIBC
    r->fp = fopen(path, "rb");
    return r->fp != NULL;
#else
    return 0;
#endif
}

static nj_u4 r_bytes(rs_reader *r, void *p, nj_u4 n)
{
    if (r->use_port) {
        nj_s4 got = r->vm->port->file_read(r->vm->port->ud, &r->f, p, n);
        if (got > 0) {
            r->read += (nj_u4)got;
            return (nj_u4)got;
        }
        return 0u;
    }
#if NJ_USE_LIBC
    {
        size_t got = fread(p, 1u, n, r->fp);
        r->read += (nj_u4)got;
        return (nj_u4)got;
    }
#else
    return 0u;
#endif
}

static nj_u4 r_u2(rs_reader *r)
{
    nj_u1 b[2];
    if (r_bytes(r, b, 2u) != 2u) {
        return 0xFFFFu;
    }
    return ((nj_u4)b[0] << 8) | (nj_u4)b[1];
}

static int r_u4(rs_reader *r, nj_u4 *out)
{
    nj_u1 b[4];
    if (r_bytes(r, b, 4u) != 4u) {
        return 0;
    }
    *out = ((nj_u4)b[0] << 24) | ((nj_u4)b[1] << 16) |
           ((nj_u4)b[2] << 8) | (nj_u4)b[3];
    return 1;
}

static int r_u8(rs_reader *r, nj_s8 *out)
{
    nj_u1 b[8];
    nj_u8 u = 0u;
    int i;

    if (r_bytes(r, b, 8u) != 8u) {
        return 0;
    }
    for (i = 0; i < 8; i++) {
        u = (u << 8) | (nj_u8)b[i];
    }
    *out = (nj_s8)u;
    return 1;
}

/*
 * Read the optional trailer at the end of the records.
 *
 * Accepted only when the remaining bytes are exactly one 16-byte version 1
 * trailer.  An unknown tag or version, a short tail, or any bytes after the
 * trailer each mean "no trailer here", and the caller falls back to the file's
 * modification time.  That strictness is what makes it safe for a future
 * trailer version to reuse the tag: an old reader will ignore it rather than
 * misinterpret it.
 */
static int r_trailer(rs_reader *r, const nj_u1 tag[4], nj_s8 *ts)
{
    nj_u4 version;
    nj_s8 value;
    nj_u1 extra;

    if (tag[0] != NJ_RS_TRAILER0 || tag[1] != NJ_RS_TRAILER1 ||
        tag[2] != NJ_RS_TRAILER2 || tag[3] != NJ_RS_TRAILER3) {
        return 0;
    }
    if (!r_u4(r, &version) || version != NJ_RS_TRAILER_VERSION) {
        return 0;
    }
    if (!r_u8(r, &value)) {
        return 0;
    }
    if (r_bytes(r, &extra, 1u) != 0u) {
        return 0;   /* trailing bytes: not a trailer this reader knows */
    }
    *ts = value;
    return 1;
}

/* Put an overflow payload where its record was read.  The record area holds a
 * zero-length placeholder for it, matched by id. */
static int rs_replace_record(nj_vm *vm, nj_ref recs, nj_ref ids, nj_s4 id,
                             nj_ref rec)
{
    nj_u4 roots = nj_root_mark(vm);
    nj_s4 i;
    int result = 0;
    if (!rs_root(vm, rec)) return 0;
    i = rs_find_ids(vm, ids, id);
    if (i >= 0) result = vec_set(vm, recs, i, rec);
    nj_root_release(vm, roots);
    return result;
}

/* The record-overflow body, after its 4-byte tag has been read. */
static int r_big(rs_reader *r, nj_vm *vm, nj_ref recs, nj_ref ids)
{
    nj_u4 version, count, i;

    if (!r_u4(r, &version) || version != NJ_RS_BIG_VERSION) {
        return 0;
    }
    if (!r_u4(r, &count) || count > NJ_RS_BIG_MAX) {
        return 0;
    }
    for (i = 0u; i < count; i++) {
        nj_u4 id = r_u2(r);
        nj_u4 len;
        nj_ref rec;

        (void)r_u2(r);   /* reserved, written as 0 */
        if (!r_u4(r, &len)) {
            return 0;
        }
        if (len <= 0xFFFFu || len > NJ_RS_RECORD_MAX) {
            return 0;    /* only lengths the record header cannot hold */
        }
        rec = nj_byte_array_new(vm, len);
        if (!rec) {
            return 0;
        }
        if (r_bytes(r, rec->u.bytes, len) != len) {
            return 0;
        }
        if (!rs_replace_record(vm, recs, ids, (nj_s4)id, rec)) {
            return 0;
        }
    }
    return 1;
}

/*
 * The optional tail sections, in the order this build writes them: any number
 * of record-overflow sections, then the trailer, which must be the last bytes
 * in the file.  An unrecognised tag ends the search, exactly like r_trailer()'s
 * strictness: the records already loaded stay, only the timestamp falls back.
 */
static int r_sections(rs_reader *r, nj_vm *vm, nj_ref recs, nj_ref ids,
                      nj_s8 *ts)
{
    nj_u1 tag[4];
    int have_ts = 0;

    while (r_bytes(r, tag, 4u) == 4u) {
        if (tag[0] == NJ_RS_BIG0 && tag[1] == NJ_RS_BIG1 &&
            tag[2] == NJ_RS_BIG2 && tag[3] == NJ_RS_BIG3) {
            if (!r_big(r, vm, recs, ids)) {
                return have_ts;
            }
            continue;
        }
        if (r_trailer(r, tag, ts)) {
            have_ts = 1;
        }
        return have_ts;
    }
    return have_ts;
}

static void r_close(rs_reader *r)
{
    if (r->use_port) {
        r->vm->port->file_close(r->vm->port->ud, &r->f);
    }
#if NJ_USE_LIBC
    else if (r->fp) {
        fclose(r->fp);
        r->fp = NULL;
    }
#endif
}

/*
 * Write the store out.
 *
 * The whole file is rewritten on every modification.  A high-score table is a
 * few hundred bytes and a title saves on level completion, not per frame, so
 * the simplicity is worth far more than an append-only scheme would be.  This
 * is NOT an atomic rewrite: w_open() truncates the existing file before the
 * new bytes are written, so a crash or a full disk can leave a partial file
 * (the loader treats an unreadable file as an empty store rather than refusing
 * to start).  What this function does guarantee is narrower and was the bug
 * the trailer started as: the in-memory timestamp is only published after the
 * write actually succeeded, so a failed mutation cannot make a store look
 * modified.
 *
 * `mutated` says whether this save follows a change (create, add, set, delete)
 * or is just closeRecordStore() flushing.  Only a change advances the
 * lastModified timestamp: closing a store must not make it look modified.  The
 * trailer is omitted when the timestamp is unknown (0), so an untouched
 * version 1 store stays a bare version 1 file until something actually
 * changes.
 */
static nj_status rs_save(nj_vm *vm, nj_ref rs, int mutated)
{
    nj_ref name = rs_get(vm, rs, NJ_F_RS_NAME, "Ljava/lang/String;");
    nj_ref recs = rs_get(vm, rs, NJ_F_RS_RECORDS, "Ljava/util/Vector;");
    nj_ref ids = rs_get(vm, rs, NJ_F_RS_IDS, "Ljava/util/Vector;");
    char nbuf[128];
    char path[320];
    rs_writer w;
    nj_s4 n, i;
    nj_u4 big;
    nj_s8 ts;

    if (!name || !recs || !ids) {
        return NJ_ERR;
    }
    if (nj_string_to_utf8(name, nbuf, (int)sizeof(nbuf)) <= 0) {
        return NJ_ERR;
    }
    rs_path(vm, nbuf, path, (nj_u4)sizeof(path));

    /* Read the old value, or the mutation time, but do not publish the new
     * time yet: if the write below fails, the store was not modified and its
     * lastModified must stay what it was. */
    ts = mutated ? nj_now_ms(vm) : rs_getl(vm, rs, NJ_F_RS_LASTMOD);

    if (!w_open(vm, &w, path)) {
        return nj_throw(vm, "javax/microedition/rms/RecordStoreException",
                        "cannot open the record store file for writing");
    }

    {
        nj_u1 magic[4];
        magic[0] = NJ_RS_MAGIC0; magic[1] = NJ_RS_MAGIC1;
        magic[2] = NJ_RS_MAGIC2; magic[3] = NJ_RS_MAGIC3;
        w_bytes(&w, magic, 4u);
    }
    w_u2(&w, NJ_RS_VERSION);
    w_u2(&w, 0u);
    w_u2(&w, (nj_u4)rs_geti(vm, rs, NJ_F_RS_NEXTID));

    n = vec_size(vm, recs);
    w_u2(&w, (nj_u4)n);

    /* Records whose length does not fit the u2 header go into the overflow
     * section written after the record area; count them first. */
    big = 0u;
    for (i = 0; i < n; i++) {
        nj_ref rec = vec_at(vm, recs, i);
        if (rec && rec->length > 0xFFFFu) {
            big++;
        }
    }

    for (i = 0; i < n; i++) {
        nj_ref rec = vec_at(vm, recs, i);
        nj_ref ido = vec_at(vm, ids, i);
        nj_s4 id = ido ? nj_call_int(vm, ido, "java/lang/Integer", "intValue",
                                     "()I", NULL) : 0;
        nj_u4 len = rec ? rec->length : 0u;

        w_u2(&w, (nj_u4)id);
        w_u2(&w, len > 0xFFFFu ? 0u : len);
        if (rec && len > 0u && len <= 0xFFFFu) {
            w_bytes(&w, rec->u.bytes, len);
        }
    }

    if (big > 0u) {
        nj_u1 tag[4];
        tag[0] = NJ_RS_BIG0; tag[1] = NJ_RS_BIG1;
        tag[2] = NJ_RS_BIG2; tag[3] = NJ_RS_BIG3;
        w_bytes(&w, tag, 4u);
        w_u4(&w, NJ_RS_BIG_VERSION);
        w_u4(&w, big);
        for (i = 0; i < n; i++) {
            nj_ref rec = vec_at(vm, recs, i);
            nj_ref ido = vec_at(vm, ids, i);
            nj_s4 id;
            nj_u4 len;

            if (!rec || rec->length <= 0xFFFFu) {
                continue;
            }
            id = ido ? nj_call_int(vm, ido, "java/lang/Integer", "intValue",
                                   "()I", NULL) : 0;
            len = rec->length;
            w_u2(&w, (nj_u4)id);
            w_u2(&w, 0u);
            w_u4(&w, len);
            w_bytes(&w, rec->u.bytes, len);
        }
    }

    if (ts != 0) {
        nj_u1 tag[4];
        tag[0] = NJ_RS_TRAILER0; tag[1] = NJ_RS_TRAILER1;
        tag[2] = NJ_RS_TRAILER2; tag[3] = NJ_RS_TRAILER3;
        w_bytes(&w, tag, 4u);
        w_u4(&w, NJ_RS_TRAILER_VERSION);
        w_u8(&w, ts);
    }
    if (!w_close(&w)) {
        return nj_throw(vm, "javax/microedition/rms/RecordStoreException",
                        "cannot write the record store file");
    }
    if (mutated) {
        rs_setl(vm, rs, NJ_F_RS_LASTMOD, ts);
    }
    return NJ_OK;
}

/* Load a store's records into its Vectors.  A missing or foreign file leaves
 * the store empty, which is what a first run looks like. */
static void rs_load(nj_vm *vm, nj_ref rs)
{
    nj_ref name = rs_get(vm, rs, NJ_F_RS_NAME, "Ljava/lang/String;");
    nj_ref recs = rs_get(vm, rs, NJ_F_RS_RECORDS, "Ljava/util/Vector;");
    nj_ref ids = rs_get(vm, rs, NJ_F_RS_IDS, "Ljava/util/Vector;");
    char nbuf[128];
    char path[320];
    rs_reader r;
    nj_u1 magic[4];
    nj_u4 version, next_id, count, i;
    nj_s8 last_modified = 0;
    nj_u4 roots = nj_root_mark(vm);
    int parsed_all, has_trailer = 0;

    if (!name || !recs || !ids) {
        return;
    }
    if (nj_string_to_utf8(name, nbuf, (int)sizeof(nbuf)) <= 0) {
        return;
    }
    rs_path(vm, nbuf, path, (nj_u4)sizeof(path));

    if (!r_open(vm, &r, path)) {
        return;
    }
    if (r_bytes(&r, magic, 4u) != 4u ||
        magic[0] != NJ_RS_MAGIC0 || magic[1] != NJ_RS_MAGIC1 ||
        magic[2] != NJ_RS_MAGIC2 || magic[3] != NJ_RS_MAGIC3) {
        r_close(&r);
        return;   /* not ours: treat as empty rather than refusing to start */
    }
    version = r_u2(&r);
    (void)r_u2(&r);
    if (version != NJ_RS_VERSION) {
        r_close(&r);
        return;
    }
    next_id = r_u2(&r);
    count = r_u2(&r);
    if (count > 4096u) {
        count = 0u;   /* implausible header: treat as empty */
    }

    for (i = 0u; i < count; i++) {
        nj_u4 id = r_u2(&r);
        nj_u4 len = r_u2(&r);
        nj_ref rec;
        nj_ref ido;

        if (len > NJ_RS_RECORD_MAX) {
            break;
        }
        rec = nj_byte_array_new(vm, len);
        if (!rec) {
            break;
        }
        if (len > 0u && r_bytes(&r, rec->u.bytes, len) != len) {
            break;
        }
        if (!rs_root(vm, rec)) break;
        ido = rs_int_obj(vm, (nj_s4)id);
        if (!ido || !rs_root(vm, ido)) {
            break;
        }
        if (!vec_add(vm, recs, rec) || !vec_add(vm, ids, ido)) {
            break;
        }
        nj_root_release(vm, roots);
    }
    nj_root_release(vm, roots);
    parsed_all = (i == count);
    if (parsed_all && r_sections(&r, vm, recs, ids, &last_modified)) {
        has_trailer = 1;
    }
    r_close(&r);

    if (!has_trailer) {
        /* A version 1 file from before the trailer existed.  Its records are
         * still good; only the timestamp is unknown.  The file's mtime is the
         * closest honest answer a hosted platform has.  With a port filesystem
         * (a SPIFFS partition, a virtual filesystem with its own name space)
         * there is no metadata hook, so report 0 = "unknown" instead of
         * guessing from a host path that may not mean the same file. */
        last_modified = (vm->port && vm->port->file_open)
                      ? 0 : rs_file_mtime_ms(path);
    }
    rs_setl(vm, rs, NJ_F_RS_LASTMOD, last_modified);

    if (next_id == 0u || next_id == 0xFFFFu) {
        next_id = 1u;
    }
    rs_seti(vm, rs, NJ_F_RS_NEXTID, (nj_s4)next_id);
}

static void rs_delete_file(nj_vm *vm, const char *name)
{
    char path[320];
    rs_path(vm, name, path, (nj_u4)sizeof(path));
    if (vm->port && vm->port->file_remove) {
        (void)vm->port->file_remove(vm->port->ud, path);
        return;
    }
#if NJ_USE_LIBC
    remove(path);
#endif
}

/* ------------------------------------------------------------------ */
/* Lifecycle                                                           */
/* ------------------------------------------------------------------ */
static nj_ref rs_open_common_body(nj_vm *vm, nj_ref name, int create_if_missing)
{
    nj_ref rs;
    nj_ref recs;
    nj_ref ids;
    nj_field *f;
    char nbuf[128];
    char path[320];
    int exists;

    if (!name) {
        return NULL;
    }
    if (nj_string_to_utf8(name, nbuf, (int)sizeof(nbuf)) <= 0) {
        return NULL;
    }
    rs = rs_find_open(vm, name);
    if (rs) {
        nj_ref state;
        nj_s4 count;
        if (!rs_root(vm, rs)) { (void)nj_throw_oom(vm); return NULL; }
        state = rs_metadata(vm, rs, 1);
        if (!state) return NULL;
        count = rms_getint(vm, state, NJ_C_RMETA, "openCount");
        if (count == 2147483647) {
            (void)nj_throw(vm, "javax/microedition/rms/RecordStoreException",
                           "too many opens");
            return NULL;
        }
        rms_setint(vm, state, NJ_C_RMETA, "openCount", count + 1);
        return rs;
    }
    rs_path(vm, nbuf, path, (nj_u4)sizeof(path));

    if (vm->port && vm->port->file_exists) {
        exists = vm->port->file_exists(vm->port->ud, path);
    } else {
#if NJ_USE_LIBC
        FILE *t = fopen(path, "rb");
        exists = t != NULL;
        if (t) {
            fclose(t);
        }
#else
        exists = 0;
#endif
    }
    if (!exists && !create_if_missing) {
        return NULL;
    }

    rs = nj_object_new(vm, NJ_C_RECORDSTORE);
    if (!rs) {
        return NULL;
    }
    if (!rs_root(vm, rs)) {
        (void)nj_throw_oom(vm);
        return NULL;
    }
    recs = nj_object_new(vm, "java/util/Vector");
    if (!recs || !rs_root(vm, recs)) {
        (void)nj_throw_oom(vm);
        return NULL;
    }
    ids = nj_object_new(vm, "java/util/Vector");
    if (!ids || !rs_root(vm, ids)) {
        (void)nj_throw_oom(vm);
        return NULL;
    }
    f = rsf(vm, NJ_F_RS_NAME, "Ljava/lang/String;");
    if (f) {
        nj_field_write_ref(vm, rs, f, name);
    }
    f = rsf(vm, NJ_F_RS_RECORDS, "Ljava/util/Vector;");
    if (f) {
        nj_field_write_ref(vm, rs, f, recs);
    }
    f = rsf(vm, NJ_F_RS_IDS, "Ljava/util/Vector;");
    if (f) {
        nj_field_write_ref(vm, rs, f, ids);
    }
    rs_seti(vm, rs, NJ_F_RS_NEXTID, 1);
    rs_seti(vm, rs, NJ_F_RS_OPEN, 1);
    rs_seti(vm, rs, "rsMode", 0);          /* AUTHMODE_PRIVATE */

    if (exists) {
        rs_load(vm, rs);
    } else {
        (void)rs_save(vm, rs, 1); /* create the file eagerly, so it persists */
        nj_clear_exception(vm);
    }
    return rs;
}

static nj_ref rs_open_common(nj_vm *vm, nj_ref name, int create_if_missing)
{
    nj_u4 roots = nj_root_mark(vm);
    nj_ref rs = rs_open_common_body(vm, name, create_if_missing);
    nj_root_release(vm, roots);
    return rs;
}

NJFN(rs_open)
{
    nj_ref name = NJ_ARG_REF(args, 0);
    nj_s4 create = NJ_ARG_INT(args, 1);
    nj_ref rs = rs_open_common(vm, name, create ? 1 : 0);
    if (!rs) {
        if (vm->exception) {
            return NJ_ERR;
        }
        return nj_throw(vm, "javax/microedition/rms/RecordStoreNotFoundException",
                        NULL);
    }
    ret->r = rs;
    return NJ_OK;
}

NJFN(rs_open_three)
{
    /* (name, createIfNecessary, blockOnLock) - the third argument is about
     * cross-process locking, which cannot happen in this runtime. */
    return NJ_CALL(rs_open, vm, th, args, ret);
}

NJFN(rs_open_auth)
{
    /* (name, createIfNecessary, authmode, writable) - the MIDP 2.0 overload,
     * and the one the corpus calls: eight titles ask for a store this way and
     * got a NoSuchMethodError before the first byte of game state was read.
     * authmode is about OTHER suites in OTHER processes, so AUTHMODE_ANY and
     * AUTHMODE_PRIVATE both mean "this one process" here; `writable` decides
     * whether an existing store may be written, which is enforced per record
     * write by the store itself. */
    nj_status st = NJ_CALL(rs_open, vm, th, args, ret);

    if (st == NJ_OK && ret->r) {
        /* Remember which authmode the store was opened with, so getMode() can
         * answer it.  Only the two documented constants are meaningful here. */
        nj_s4 mode = NJ_ARG_INT(args, 2);
        if (mode == 0 || mode == 1) {
            rs_seti(vm, ret->r, "rsMode", mode);
        }
    }
    return st;
}

/*
 * setMode(int authmode, boolean writable) - the MIDP 1.0 access-mode setter.
 * authmode is about other suites in other processes, which cannot exist in this
 * runtime (see rs_open_auth), so this validates the constant and remembers it;
 * `writable` is enforced per record write by the store itself.
 */
NJFN(rs_setMode)
{
    if (rs_require_open(vm, NJ_THIS(args)) != NJ_OK) return NJ_ERR;
    nj_s4 mode = NJ_ARG_INT(args, 0);

    if (mode != 0 && mode != 1) {      /* AUTHMODE_PRIVATE / AUTHMODE_ANY */
        return nj_throw(vm, "java/lang/IllegalArgumentException",
                        "invalid authmode");
    }
    rs_seti(vm, NJ_THIS(args), "rsMode", mode);
    return NJ_OK;
}

/* getMode(): the MIDP 1.0 reader for what setMode() stored.  MIDP 2.0 dropped
 * it in favour of the four-argument openRecordStore, but a title built against
 * the older API still calls it. */
NJFN(rs_getMode)
{
    if (rs_require_open(vm, NJ_THIS(args)) != NJ_OK) return NJ_ERR;
    ret->i = rs_geti(vm, NJ_THIS(args), "rsMode");
    return NJ_OK;
}

NJFN(rs_open_named)
{
    /* (name, vendorName, suiteName) - MIDP 1.0's form, which creates the
     * store when it is missing.  Vendor and suite only choose a directory on
     * a device that hosts several suites; every store here lives in the one
     * RMS directory, so the store's own name is what identifies it. */
    nj_ref name = NJ_ARG_REF(args, 0);
    nj_ref rs;

    if (!name || !NJ_ARG_REF(args, 1) || !NJ_ARG_REF(args, 2)) {
        return nj_throw(vm, "java/lang/NullPointerException", NULL);
    }
    rs = rs_open_common(vm, name, 1);
    if (!rs) {
        if (vm->exception) {
            return NJ_ERR;
        }
        return nj_throw(vm,
                        "javax/microedition/rms/RecordStoreNotFoundException",
                        NULL);
    }
    ret->r = rs;
    return NJ_OK;
}

NJFN(rs_delete)
{
    nj_ref name = NJ_ARG_REF(args, 0);
    char nbuf[128];

    if (!name) {
        return nj_throw(vm, "java/lang/NullPointerException", NULL);
    }
    if (nj_string_to_utf8(name, nbuf, (int)sizeof(nbuf)) <= 0) {
        return nj_throw(vm, "javax/microedition/rms/RecordStoreException", NULL);
    }
    if (rs_find_open(vm, name)) {
        return nj_throw(vm, "javax/microedition/rms/RecordStoreException",
                        "record store is open");
    }
    rs_delete_file(vm, nbuf);
    return NJ_OK;
}

NJFN(rs_close)
{
    nj_ref rs = NJ_THIS(args);
    if (!rs_is_open(vm, rs)) {
        return nj_throw(vm, "javax/microedition/rms/RecordStoreNotOpenException",
                        NULL);
    }
    {
        nj_ref state = rs_metadata(vm, rs, 0);
        nj_s4 count = state ? rms_getint(vm, state, NJ_C_RMETA, "openCount") : 1;
        if (count > 1) {
            rms_setint(vm, state, NJ_C_RMETA, "openCount", count - 1);
            return NJ_OK;
        }
        if (rs_save(vm, rs, 0) != NJ_OK) return NJ_ERR;
        if (state) {
            rms_setint(vm, state, NJ_C_RMETA, "openCount", 0);
            rms_setref(vm, state, NJ_C_RMETA, "enumerations",
                       "Ljava/util/Vector;", NULL);
        }
    }
    rs_seti(vm, rs, NJ_F_RS_OPEN, 0);
    return NJ_OK;
}

/* ------------------------------------------------------------------ */
/* Queries                                                             */
/* ------------------------------------------------------------------ */
NJFN(rs_getName)
{
    if (rs_require_open(vm, NJ_THIS(args)) != NJ_OK) return NJ_ERR;
    ret->r = rs_get(vm, NJ_THIS(args), NJ_F_RS_NAME, "Ljava/lang/String;");
    return NJ_OK;
}

NJFN(rs_getNumRecords)
{
    if (rs_require_open(vm, NJ_THIS(args)) != NJ_OK) return NJ_ERR;
    nj_ref recs = rs_get(vm, NJ_THIS(args), NJ_F_RS_RECORDS,
                         "Ljava/util/Vector;");
    ret->i = recs ? vec_size(vm, recs) : 0;
    return NJ_OK;
}

NJFN(rs_getNextRecordID)
{
    if (rs_require_open(vm, NJ_THIS(args)) != NJ_OK) return NJ_ERR;
    ret->i = rs_geti(vm, NJ_THIS(args), NJ_F_RS_NEXTID);
    return NJ_OK;
}

NJFN(rs_getSize)
{
    if (rs_require_open(vm, NJ_THIS(args)) != NJ_OK) return NJ_ERR;
    nj_ref rs = NJ_THIS(args);
    nj_ref recs = rs_get(vm, rs, NJ_F_RS_RECORDS, "Ljava/util/Vector;");
    nj_s4 n, i, total = 0;

    if (!recs) {
        ret->i = 0;
        return NJ_OK;
    }
    n = vec_size(vm, recs);
    for (i = 0; i < n; i++) {
        nj_ref rec = vec_at(vm, recs, i);
        if (rec) {
            total += (nj_s4)rec->length + 8;
        }
    }
    ret->i = total;
    return NJ_OK;
}

NJFN(rs_getSizeAvailable)
{
    if (rs_require_open(vm, NJ_THIS(args)) != NJ_OK) return NJ_ERR;
    /* Not a filesystem query: report a generous fixed budget so that games
     * which check before writing behave sensibly. */
    ret->i = 1024 * 1024;
    return NJ_OK;
}

NJFN(rs_getLastModified)
{
    nj_ref rs = NJ_THIS(args);

    if (!rs_is_open(vm, rs)) {
        return nj_throw(vm, "javax/microedition/rms/RecordStoreNotOpenException",
                        NULL);
    }
    ret->j = rs_getl(vm, rs, NJ_F_RS_LASTMOD);
    return NJ_OK;
}

NJFN(rs_getVersion)
{
    if (rs_require_open(vm, NJ_THIS(args)) != NJ_OK) return NJ_ERR;
    ret->i = 1;
    return NJ_OK;
}

/* ------------------------------------------------------------------ */
/* Records                                                             */
/* ------------------------------------------------------------------ */
static nj_s4 rs_do_add(nj_vm *vm, nj_ref rs, nj_ref data, nj_s4 off, nj_s4 len)
{
    nj_ref recs = rs_get(vm, rs, NJ_F_RS_RECORDS, "Ljava/util/Vector;");
    nj_ref ids = rs_get(vm, rs, NJ_F_RS_IDS, "Ljava/util/Vector;");
    nj_ref copy;
    nj_ref ido;
    nj_s4 id;

    if (!rs_is_open(vm, rs)) {
        return nj_throw(vm, "javax/microedition/rms/RecordStoreNotOpenException",
                        NULL);
    }
    /* MIDP allows null data for an empty record. */
    if (!data && len == 0) {
        off = 0;
        len = 0;
    } else if (!data) {
        return nj_throw(vm, "java/lang/NullPointerException", NULL);
    }
    if (off < 0 || len < 0 || (data &&
        ((nj_u4)off > data->length || (nj_u4)len > data->length - (nj_u4)off))) {
        return nj_throw(vm, "java/lang/ArrayIndexOutOfBoundsException", NULL);
    }
    copy = nj_byte_array_new(vm, (nj_u4)len);
    if (!copy) {
        return nj_throw_oom(vm);
    }
    if (data && len > 0) {
        memcpy(copy->u.bytes, data->u.bytes + off, (size_t)len);
    }
    if (!rs_root(vm, copy)) return nj_throw_oom(vm);
    id = rs_geti(vm, rs, NJ_F_RS_NEXTID);
    rs_seti(vm, rs, NJ_F_RS_NEXTID, id + 1);
    ido = rs_int_obj(vm, id);
    if (!ido || !rs_root(vm, ido) || !vec_add(vm, recs, copy) || !vec_add(vm, ids, ido)) {
        return nj_throw_oom(vm);
    }
    if (rs_save(vm, rs, 1) != NJ_OK) {
        return NJ_ERR;
    }
    return id;
}

NJFN(rs_addRecord)
{
    nj_u4 roots = nj_root_mark(vm);
    nj_s4 id = rs_do_add(vm, NJ_THIS(args), NJ_ARG_REF(args, 0),
                         NJ_ARG_INT(args, 1), NJ_ARG_INT(args, 2));
    if (id >= 0 && rs_notify_enumerations(vm, NJ_THIS(args)) != NJ_OK) id = NJ_ERR;
    nj_root_release(vm, roots);
    if (id < 0) {
        return NJ_ERR;
    }
    ret->i = id;
    return NJ_OK;
}

NJFN(rs_getRecord)
{
    nj_ref rs = NJ_THIS(args);
    nj_ref recs;
    nj_s4 id = NJ_ARG_INT(args, 0);
    nj_s4 idx;

    if (!rs_is_open(vm, rs)) {
        return nj_throw(vm, "javax/microedition/rms/RecordStoreNotOpenException",
                        NULL);
    }
    recs = rs_get(vm, rs, NJ_F_RS_RECORDS, "Ljava/util/Vector;");
    idx = rs_find(vm, rs, id);
    if (idx < 0) {
        return nj_throw(vm, "javax/microedition/rms/InvalidRecordIDException",
                        NULL);
    }
    {
        nj_ref rec = vec_at(vm, recs, idx);
        nj_ref copy;
        /* MIDP getRecord(int) returns null for an existing empty record. */
        if (!rec || rec->length == 0u) {
            ret->r = NULL;
            return NJ_OK;
        }
        copy = nj_byte_array_new(vm, rec->length);
        if (!copy) {
            return nj_throw_oom(vm);
        }
        if (rec && rec->length > 0u) {
            memcpy(copy->u.bytes, rec->u.bytes, rec->length);
        }
        ret->r = copy;
    }
    return NJ_OK;
}

NJFN(rs_getRecordInto)
{
    if (rs_require_open(vm, NJ_THIS(args)) != NJ_OK) return NJ_ERR;
    nj_ref rs = NJ_THIS(args);
    nj_ref buf = NJ_ARG_REF(args, 1);
    nj_s4 off = NJ_ARG_INT(args, 2);
    nj_s4 idx = rs_find(vm, rs, NJ_ARG_INT(args, 0));
    nj_ref recs = rs_get(vm, rs, NJ_F_RS_RECORDS, "Ljava/util/Vector;");
    nj_ref rec;

    if (idx < 0 || !recs) {
        return nj_throw(vm, "javax/microedition/rms/InvalidRecordIDException",
                        NULL);
    }
    if (!buf) {
        return nj_throw(vm, "java/lang/NullPointerException", NULL);
    }
    rec = vec_at(vm, recs, idx);
    if (!rec) {
        return nj_throw(vm, "javax/microedition/rms/InvalidRecordIDException",
                        NULL);
    }
    if (off < 0 || (nj_u4)off > buf->length ||
        rec->length > buf->length - (nj_u4)off) {
        return nj_throw(vm, "java/lang/ArrayIndexOutOfBoundsException", NULL);
    }
    if (rec->length > 0u) {
        memcpy(buf->u.bytes + off, rec->u.bytes, rec->length);
    }
    ret->i = (nj_s4)rec->length;
    return NJ_OK;
}

NJFN(rs_getRecordSize)
{
    if (rs_require_open(vm, NJ_THIS(args)) != NJ_OK) return NJ_ERR;
    nj_ref rs = NJ_THIS(args);
    nj_ref recs = rs_get(vm, rs, NJ_F_RS_RECORDS, "Ljava/util/Vector;");
    nj_s4 idx = rs_find(vm, rs, NJ_ARG_INT(args, 0));
    nj_ref rec;

    if (idx < 0 || !recs) {
        return nj_throw(vm, "javax/microedition/rms/InvalidRecordIDException",
                        NULL);
    }
    rec = vec_at(vm, recs, idx);
    ret->i = rec ? (nj_s4)rec->length : 0;
    return NJ_OK;
}

NJFN(rs_setRecord)
{
    nj_ref rs = NJ_THIS(args);
    nj_ref data = NJ_ARG_REF(args, 1);
    nj_s4 off = NJ_ARG_INT(args, 2);
    nj_s4 len = NJ_ARG_INT(args, 3);
    nj_ref recs;
    nj_s4 idx;

    if (!rs_is_open(vm, rs)) {
        return nj_throw(vm, "javax/microedition/rms/RecordStoreNotOpenException",
                        NULL);
    }
    /* MIDP allows null data for an empty record. */
    if (!data && len == 0) {
        off = 0;
        len = 0;
    } else if (!data) {
        return nj_throw(vm, "java/lang/NullPointerException", NULL);
    }
    if (off < 0 || len < 0 || (data &&
        ((nj_u4)off > data->length || (nj_u4)len > data->length - (nj_u4)off))) {
        return nj_throw(vm, "java/lang/ArrayIndexOutOfBoundsException", NULL);
    }
    recs = rs_get(vm, rs, NJ_F_RS_RECORDS, "Ljava/util/Vector;");
    idx = rs_find(vm, rs, NJ_ARG_INT(args, 0));
    if (idx < 0) {
        return nj_throw(vm, "javax/microedition/rms/InvalidRecordIDException",
                        NULL);
    }
    {
        nj_ref copy = nj_byte_array_new(vm, (nj_u4)len);
        if (!copy) {
            return nj_throw_oom(vm);
        }
        if (len > 0) {
            memcpy(copy->u.bytes, data->u.bytes + off, (nj_size)len);
        }
        if (!vec_set(vm, recs, idx, copy)) {
            return NJ_ERR;
        }
    }
    if (rs_save(vm, rs, 1) != NJ_OK) return NJ_ERR;
    return rs_notify_enumerations(vm, rs);
}

NJFN(rs_deleteRecord)
{
    nj_ref rs = NJ_THIS(args);
    nj_ref recs = rs_get(vm, rs, NJ_F_RS_RECORDS, "Ljava/util/Vector;");
    nj_ref ids = rs_get(vm, rs, NJ_F_RS_IDS, "Ljava/util/Vector;");
    nj_s4 idx;

    if (!rs_is_open(vm, rs)) {
        return nj_throw(vm, "javax/microedition/rms/RecordStoreNotOpenException",
                        NULL);
    }
    idx = rs_find(vm, rs, NJ_ARG_INT(args, 0));
    if (idx < 0) {
        return nj_throw(vm, "javax/microedition/rms/InvalidRecordIDException",
                        NULL);
    }
    if (!vec_del(vm, recs, idx) || !vec_del(vm, ids, idx)) {
        return NJ_ERR;
    }
    if (rs_save(vm, rs, 1) != NJ_OK) return NJ_ERR;
    return rs_notify_enumerations(vm, rs);
}

NJFN(rs_listNames)
{
    const nj_port *port = vm->port;
    const char *dir = (port && port->rms_dir) ? port->rms_dir : ".";
    void *handle = NULL;
    nj_ref names, result;
    nj_u4 mark;
    nj_s4 count, i;
    int next, is_dir;
    char name[256];
    nj_status st = NJ_OK;
    int use_port = port && port->dir_open && port->dir_next && port->dir_close;
#if NJ_USE_LIBC && !defined(_WIN32)
    DIR *host_dir = NULL;
#endif

    /* MIDP returns null when no stores exist.  Walk the same directory used
     * by rs_path(), including closed stores from earlier VM sessions. */
    ret->r = NULL;
    if (use_port) {
        if (port->dir_open(port->ud, dir, &handle) != 0) {
            return NJ_OK;
        }
    } else {
#if NJ_USE_LIBC && !defined(_WIN32)
        /* SDL2 uses libc for RMS files and does not supply directory hooks. */
        if (port && port->file_open) {
            return NJ_OK; /* A custom file backend must supply its own listing. */
        }
        host_dir = opendir(dir);
        if (!host_dir) {
            return NJ_OK;
        }
#else
        return NJ_OK;
#endif
    }
    mark = nj_root_mark(vm);
    names = nj_object_new(vm, "java/util/Vector");
    if (!nj_root(vm, names)) {
        st = nj_throw_oom(vm);
        goto done;
    }
    if (nj_call_special(vm, names, "java/util/Vector", "<init>", "()V",
                        NULL, NULL) != NJ_OK) {
        st = NJ_ERR;
        goto done;
    }
    for (;;) {
        nj_size len;
        nj_ref s;
        if (use_port) {
            next = port->dir_next(port->ud, handle, name, sizeof(name), &is_dir);
        } else {
#if NJ_USE_LIBC && !defined(_WIN32)
            struct dirent *entry;
            struct stat info;
            char path[768];
            errno = 0;
            entry = readdir(host_dir);
            next = entry ? 1 : (errno ? -1 : 0);
            if (entry) {
                if (strlen(entry->d_name) >= sizeof(name)) {
                    continue;
                }
                strcpy(name, entry->d_name);
                if (snprintf(path, sizeof(path), "%s/%s", dir, name) >=
                        (int)sizeof(path) || stat(path, &info) != 0 ||
                        !S_ISREG(info.st_mode)) {
                    continue;
                }
                is_dir = 0;
            }
#else
            next = 0;
#endif
        }
        if (next != 1) {
            break;
        }
        len = strlen(name);
        if (is_dir || len <= 4u || strcmp(name + len - 4u, ".rms") != 0) {
            continue;
        }
        name[len - 4u] = '\0';
        s = nj_string_new(vm, name);
        if (!s || !vec_add(vm, names, s)) {
            st = vm->exception ? NJ_ERR : nj_throw_oom(vm);
            goto done;
        }
    }
    if (next < 0) {
        st = nj_throw(vm, "javax/microedition/rms/RecordStoreException",
                      "cannot list record stores");
        goto done;
    }
    count = vec_size(vm, names);
    if (vm->exception) {
        st = NJ_ERR;
        goto done;
    }
    if (count == 0) {
        goto done;
    }
    result = nj_object_array_new(vm, "java/lang/String", (nj_u4)count);
    if (!nj_root(vm, result)) {
        st = nj_throw_oom(vm);
        goto done;
    }
    for (i = 0; i < count; i++) {
        result->u.refs[i] = vec_at(vm, names, i);
        if (vm->exception) {
            st = NJ_ERR;
            goto done;
        }
    }
    ret->r = result;
done:
    if (use_port) {
        port->dir_close(port->ud, handle);
    }
#if NJ_USE_LIBC && !defined(_WIN32)
    else {
        closedir(host_dir);
    }
#endif
    nj_root_release(vm, mark);
    return st;
}

NJFN(rs_listener_noop)
{
    return NJ_OK;
}

/* ------------------------------------------------------------------ */
/* RecordEnumeration                                                   */
/* ------------------------------------------------------------------ */
/*
 * MIDP RMS enumeration: filter, order and walk a store's records.
 *
 * A game that keeps a high-score table sorts it through a RecordComparator and
 * reads it back through one of these, and the first real title to try
 * (五子棋) died on the missing enumerateRecords() before it could load its save
 * file.
 *
 * Ordinary enumerations snapshot the matching IDs. Kept enumerations subscribe
 * to their store and rebuild after each successful mutation. Each rebuild uses
 * a bounded ID snapshot before calling game code; its implementation object
 * holds the filter/comparator as GC-visible, snapshot-visible references.
 */

/* The store retains only kept enumerations, just as a RecordListener would.
 * Removing one releases that retention even while the store stays open. */
static nj_status re_subscribe(nj_vm *vm, nj_ref self, int keep)
{
    nj_u4 roots = nj_root_mark(vm);
    nj_ref rs, state, list;
    nj_s4 i, n;
    nj_status st = NJ_OK;
    if (!rs_root(vm, self)) return nj_throw_oom(vm);
    rs = renum_get(vm, self, "reStore", "L" NJ_C_RECORDSTORE ";");
    state = rs_metadata(vm, rs, keep);
    if (!state) {
        if (nj_has_exception(vm)) st = NJ_ERR;
        else renum_seti(vm, self, "reKeep", 0);
        goto done;
    }
    list = rms_getref(vm, state, NJ_C_RMETA, "enumerations", "Ljava/util/Vector;");
    if (!list && keep) {
        list = nj_object_new(vm, "java/util/Vector");
        if (!list || !rs_root(vm, list)) { st = nj_throw_oom(vm); goto done; }
        rms_setref(vm, state, NJ_C_RMETA, "enumerations", "Ljava/util/Vector;", list);
    }
    n = list ? vec_size(vm, list) : 0;
    if (nj_has_exception(vm)) { st = NJ_ERR; goto done; }
    for (i = 0; i < n; i++) {
        nj_ref en = vec_at(vm, list, i);
        if (nj_has_exception(vm)) { st = NJ_ERR; goto done; }
        if (en == self) {
            if (!keep && !vec_del(vm, list, i)) { st = NJ_ERR; goto done; }
            renum_seti(vm, self, "reKeep", keep);
            goto done;
        }
    }
    if (keep && !vec_add(vm, list, self)) { st = NJ_ERR; goto done; }
    renum_seti(vm, self, "reKeep", keep);
done:
    nj_root_release(vm, roots);
    return st;
}

static nj_status re_require_live(nj_vm *vm, nj_ref self)
{
    return renum_geti(vm, self, "reDestroyed") ?
        nj_throw(vm, "java/lang/IllegalStateException", "the enumeration was destroyed") : NJ_OK;
}

NJFN(re_numRecords)
{
    nj_ref ids;
    if (re_require_live(vm, NJ_THIS(args)) != NJ_OK) return NJ_ERR;
    ids = renum_get(vm, NJ_THIS(args), "reIds", "[I");
    ret->i = ids ? (nj_s4)ids->length : 0;
    return NJ_OK;
}

NJFN(re_hasNext)
{
    nj_ref self = NJ_THIS(args), ids;
    if (re_require_live(vm, self) != NJ_OK) return NJ_ERR;
    ids = renum_get(vm, self, "reIds", "[I");
    {
        nj_s4 pos = renum_geti(vm, self, "rePos");
        nj_s4 n = ids ? (nj_s4)ids->length : 0;
        int available = rms_is(self, NJ_C_RENUM_IMPL) ?
            (n > 0 && (pos == -1 || pos < n - 1)) : pos < n;
        ret->i = rs_is_open(vm, renum_get(vm, self, "reStore", "L" NJ_C_RECORDSTORE ";")) && available;
    }
    return NJ_OK;
}
NJFN(re_hasPrev)
{
    nj_ref self = NJ_THIS(args);
    if (re_require_live(vm, self) != NJ_OK) return NJ_ERR;
    {
        nj_ref ids = renum_get(vm, self, "reIds", "[I");
        nj_s4 pos = renum_geti(vm, self, "rePos");
        int available = pos > 0 || (rms_is(self, NJ_C_RENUM_IMPL) &&
                        pos == -1 && ids && ids->length > 0u);
        ret->i = rs_is_open(vm, renum_get(vm, self, "reStore", "L" NJ_C_RECORDSTORE ";")) && available;
    }
    return NJ_OK;
}
NJFN(re_reset)
{
    if (re_require_live(vm, NJ_THIS(args)) != NJ_OK) return NJ_ERR;
    renum_seti(vm, NJ_THIS(args), "rePos",
               rms_is(NJ_THIS(args), NJ_C_RENUM_IMPL) ? -1 : 0);
    return NJ_OK;
}
NJFN(re_destroy)
{
    nj_ref self = NJ_THIS(args);
    if (re_require_live(vm, self) != NJ_OK) return NJ_ERR;
    if (rms_is(self, NJ_C_RENUM_IMPL)) {
        if (re_subscribe(vm, self, 0) != NJ_OK) return NJ_ERR;
        rms_setref(vm, self, NJ_C_RENUM_IMPL, "filter", "L" NJ_C_RFILTER ";", NULL);
        rms_setref(vm, self, NJ_C_RENUM_IMPL, "comparator", "L" NJ_C_RCOMP ";", NULL);
    }
    renum_seti(vm, self, "reDestroyed", 1);
    renum_setr(vm, self, "reIds", "[I", NULL);
    renum_setr(vm, self, "reStore", "L" NJ_C_RECORDSTORE ";", NULL);
    renum_seti(vm, self, "reKeep", 0);
    return NJ_OK;
}
NJFN(re_isKept)
{
    if (re_require_live(vm, NJ_THIS(args)) != NJ_OK) return NJ_ERR;
    ret->i = renum_geti(vm, NJ_THIS(args), "reKeep");
    return NJ_OK;
}
NJFN(re_keepUpdated)
{
    nj_ref self = NJ_THIS(args);
    int keep = NJ_ARG_INT(args, 0) != 0;
    if (re_require_live(vm, self) != NJ_OK) return NJ_ERR;
    if (!rms_is(self, NJ_C_RENUM_IMPL)) {
        renum_seti(vm, self, "reKeep", keep); /* legacy snapshot */
        return NJ_OK;
    }
    if (keep) {
        if (re_refresh(vm, self) != NJ_OK) return NJ_ERR;
        if (re_require_live(vm, self) != NJ_OK) return NJ_ERR;
        if (rs_require_open(vm, renum_get(vm, self, "reStore",
                                        "L" NJ_C_RECORDSTORE ";")) != NJ_OK) return NJ_ERR;
    }
    return re_subscribe(vm, self, keep);
}

NJFN(re_rebuild)
{
    if (re_require_live(vm, NJ_THIS(args)) != NJ_OK) return NJ_ERR;
    return re_refresh(vm, NJ_THIS(args));
}

/* New enumerations store the last returned index; -1 means untouched, so
 * either direction can be chosen initially. An exhausted backwards traversal
 * stays at zero and cannot accidentally wrap to the last record again. */
static nj_status re_take_id(nj_vm *vm, nj_ref self, int forward, nj_s4 *id)
{
    nj_ref ids;
    nj_s4 pos, n, index;
    int impl = rms_is(self, NJ_C_RENUM_IMPL);
    if (re_require_live(vm, self) != NJ_OK) return NJ_ERR;
    if (rs_require_open(vm, renum_get(vm, self, "reStore",
                                    "L" NJ_C_RECORDSTORE ";")) != NJ_OK) return NJ_ERR;
    ids = renum_get(vm, self, "reIds", "[I");
    pos = renum_geti(vm, self, "rePos");
    n = ids ? (nj_s4)ids->length : 0;
    if (impl) index = pos == -1 ? (forward ? 0 : n - 1) : pos + (forward ? 1 : -1);
    else index = forward ? pos : pos - 1; /* legacy snapshot cursor */
    if (index < 0 || index >= n) {
        return nj_throw(vm, "javax/microedition/rms/InvalidRecordIDException",
                        forward ? "no next record" : "no previous record");
    }
    *id = ((nj_s4 *)ids->u.bytes)[index];
    renum_seti(vm, self, "rePos", impl ? index : index + (forward ? 1 : 0));
    return NJ_OK;
}

static nj_status re_step(nj_vm *vm, nj_value *args, nj_value *ret, int forward)
{
    nj_ref self = NJ_THIS(args);
    nj_s4 id;
    nj_value a[1];
    if (re_take_id(vm, self, forward, &id) != NJ_OK) return NJ_ERR;
    a[0].i = id;
    return nj_call(vm, renum_get(vm, self, "reStore", "L" NJ_C_RECORDSTORE ";"),
                   NJ_C_RECORDSTORE, "getRecord", "(I)[B", a, ret);
}

NJFN(re_nextRecord)     { return re_step(vm, args, ret, 1); }
NJFN(re_previousRecord) { return re_step(vm, args, ret, 0); }

static nj_status re_peek_id(nj_vm *vm, nj_value *args, nj_value *ret, int forward)
{
    nj_s4 id;
    if (re_take_id(vm, NJ_THIS(args), forward, &id) != NJ_OK) return NJ_ERR;
    ret->i = id;
    return NJ_OK;
}

NJFN(re_nextId)     { return re_peek_id(vm, args, ret, 1); }
NJFN(re_previousId) { return re_peek_id(vm, args, ret, 0); }

/*
 * RecordStore.enumerateRecords(filter, comparator, keepUpdated).
 *
 * Filter first, then sort: the comparator is only ever asked about records the
 * game said it wanted, and a comparator that behaves differently depending on
 * order would otherwise see a different set.
 */
static nj_status re_build_ids(nj_vm *vm, nj_ref rs, nj_ref filter,
                              nj_ref comp, nj_ref *result)
{
    nj_ref ids;
    nj_s4 n, i, keep = 0;
    nj_s4 *dst;

    if (!rs_is_open(vm, rs)) {
        return nj_throw(vm, "javax/microedition/rms/RecordStoreNotOpenException",
                        NULL);
    }
    {
        nj_ref idv = rs_get(vm, rs, NJ_F_RS_IDS, "Ljava/util/Vector;");
        n = vec_size(vm, idv);
        ids = nj_array_new(vm, "I", (nj_u4)n);
        if (!ids) {
            return nj_throw_oom(vm);
        }
        if (!rs_root(vm, ids)) return nj_throw_oom(vm);
        dst = (nj_s4 *)ids->u.bytes;
        /* Snapshot before calling user code: filters can mutate the store.
         * Iterating the live Vector can outgrow this fixed-size array. */
        for (i = 0; i < n; i++) {
            nj_ref ido = vec_at(vm, idv, i);
            dst[i] = ido ? nj_call_int(vm, ido, "java/lang/Integer",
                                      "intValue", "()I", NULL) : 0;
            if (nj_has_exception(vm)) return NJ_ERR;
        }
        for (i = 0; i < n; i++) {
            nj_s4 id = dst[i];
            if (filter) {
                nj_value a[3];
                nj_value r;
                nj_value ga[2];
                nj_value gr;
                ga[0].i = id;
                gr.r = NULL;
                if (nj_call(vm, rs, NJ_C_RECORDSTORE, "getRecord", "(I)[B",
                            ga, &gr) != NJ_OK) {
                    return NJ_ERR;
                }
                a[0].r = gr.r;
                r.i = 0;
                if (nj_call(vm, filter, "javax/microedition/rms/RecordFilter",
                            "matches", "([B)Z", a, &r) != NJ_OK) {
                    return NJ_ERR;
                }
                if (!r.i) {
                    continue;   /* filtered out */
                }
            }
            dst[keep++] = id;
        }
        n = keep;
        keep = 0;
        ids->length = (nj_u4)n;
    }

    /*
     * Insertion sort through the Java comparator.  A high-score table has tens
     * of records, not thousands, and calling into Java per comparison makes any
     * cleverer sort's bookkeeping the expensive part.
     */
    if (comp) {
        for (i = 1; i < n; i++) {
            nj_s4 key = dst[i];
            nj_s4 j = i - 1;
            while (j >= 0) {
                nj_value a[3];
                nj_value r;
                nj_value ga[2];
                nj_value gr;
                int c;
                nj_u4 roots = nj_root_mark(vm);
                ga[0].i = key;
                gr.r = NULL;
                if (nj_call(vm, rs, NJ_C_RECORDSTORE, "getRecord", "(I)[B",
                            ga, &gr) != NJ_OK) {
                    return NJ_ERR;
                }
                if (!rs_root(vm, gr.r)) return nj_throw_oom(vm);
                a[0].r = NULL;
                {
                    nj_value gb[2];
                    nj_value gbr;
                    gb[0].i = dst[j];
                    gbr.r = NULL;
                    if (nj_call(vm, rs, NJ_C_RECORDSTORE, "getRecord", "(I)[B",
                                gb, &gbr) != NJ_OK) {
                        return NJ_ERR;
                    }
                    a[0].r = gbr.r;
                }
                a[1].r = gr.r;
                r.i = 0;
                if (nj_call(vm, comp, "javax/microedition/rms/RecordComparator",
                            "compare", "([B[B)I", a, &r) != NJ_OK) {
                    return NJ_ERR;
                }
                nj_root_release(vm, roots);
                c = r.i;
                if (c <= 0) {
                    break;
                }
                dst[j + 1] = dst[j];
                j--;
            }
            dst[j + 1] = key;
        }
    }

    *result = ids;
    return NJ_OK;
}

/* A bounded pass over an ID snapshot. User callbacks may reenter RMS, so a
 * refresh in progress must not recursively refresh itself. Publish atomically. */
static nj_status re_refresh(nj_vm *vm, nj_ref self)
{
    nj_u4 roots = nj_root_mark(vm);
    nj_ref ids = NULL, rs, filter, comp;
    nj_status st;
    if (!rms_is(self, NJ_C_RENUM_IMPL)) {
        /* Legacy snapshots never saved the callbacks. Preserve their frozen
         * enumeration rather than silently replacing its filter with NULL. */
        renum_seti(vm, self, "rePos", 0);
        return NJ_OK;
    }
    if (rms_getint(vm, self, NJ_C_RENUM_IMPL, "busy")) return NJ_OK;
    if (!rs_root(vm, self)) return nj_throw_oom(vm);
    rs = renum_get(vm, self, "reStore", "L" NJ_C_RECORDSTORE ";");
    filter = rms_getref(vm, self, NJ_C_RENUM_IMPL, "filter", "L" NJ_C_RFILTER ";");
    comp = rms_getref(vm, self, NJ_C_RENUM_IMPL, "comparator", "L" NJ_C_RCOMP ";");
    if (!rs_root(vm, rs) || !rs_root(vm, filter) || !rs_root(vm, comp)) {
        nj_root_release(vm, roots);
        return nj_throw_oom(vm);
    }
    rms_setint(vm, self, NJ_C_RENUM_IMPL, "busy", 1);
    st = re_build_ids(vm, rs, filter, comp, &ids);
    rms_setint(vm, self, NJ_C_RENUM_IMPL, "busy", 0);
    if (st == NJ_OK && !renum_geti(vm, self, "reDestroyed")) {
        renum_setr(vm, self, "reIds", "[I", ids);
        renum_seti(vm, self, "rePos", -1);
    }
    nj_root_release(vm, roots);
    return st;
}

static nj_status rs_notify_enumerations(nj_vm *vm, nj_ref rs)
{
    nj_ref state = rs_metadata(vm, rs, 0);
    nj_ref list, snapshot;
    nj_s4 i, n;
    nj_status st = NJ_OK;
    nj_u4 roots;
    if (!state) return NJ_OK;
    list = rms_getref(vm, state, NJ_C_RMETA, "enumerations", "Ljava/util/Vector;");
    if (!list) return NJ_OK;
    roots = nj_root_mark(vm);
    if (!rs_root(vm, rs) || !rs_root(vm, list)) goto oom;
    n = vec_size(vm, list);
    if (nj_has_exception(vm)) { st = NJ_ERR; goto done; }
    snapshot = nj_array_new(vm, "Ljava/lang/Object;", (nj_u4)n);
    if (!snapshot || !rs_root(vm, snapshot)) goto oom;
    for (i = 0; i < n; i++) {
        snapshot->u.refs[i] = vec_at(vm, list, i);
        if (nj_has_exception(vm)) { st = NJ_ERR; goto done; }
    }
    /* The registry can change during a callback; the snapshot stays rooted. */
    for (i = 0; i < n; i++) {
        nj_ref en = snapshot->u.refs[i];
        if (en && !renum_geti(vm, en, "reDestroyed") &&
            renum_geti(vm, en, "reKeep") && rs_is_open(vm, rs) &&
            renum_get(vm, en, "reStore", "L" NJ_C_RECORDSTORE ";") == rs) {
            st = re_refresh(vm, en);
            if (st != NJ_OK) goto done;
        }
    }
    goto done;
oom:
    st = nj_throw_oom(vm);
done:
    nj_root_release(vm, roots);
    return st;
}

static nj_status rs_enumerate_records(nj_vm *vm, nj_value *args, nj_value *ret)
{
    nj_ref rs = NJ_THIS(args), out;
    if (rs_require_open(vm, rs) != NJ_OK) return NJ_ERR;
    out = nj_object_new(vm, NJ_C_RENUM_IMPL);
    if (!out || !rs_root(vm, out)) return nj_throw_oom(vm);
    renum_setr(vm, out, "reStore", "L" NJ_C_RECORDSTORE ";", rs);
    rms_setref(vm, out, NJ_C_RENUM_IMPL, "filter", "L" NJ_C_RFILTER ";", NJ_ARG_REF(args, 0));
    rms_setref(vm, out, NJ_C_RENUM_IMPL, "comparator", "L" NJ_C_RCOMP ";", NJ_ARG_REF(args, 1));
    if (re_refresh(vm, out) != NJ_OK) return NJ_ERR;
    /* Initial callbacks may mutate the store. Only subscribe after they finish. */
    if (NJ_ARG_INT(args, 2) && re_subscribe(vm, out, 1) != NJ_OK) return NJ_ERR;
    ret->r = out;
    return NJ_OK;
}

NJFN(rs_enumerateRecords)
{
    nj_u4 roots = nj_root_mark(vm);
    nj_status st = rs_enumerate_records(vm, args, ret);
    /* Also release comparison operands on every callback failure path. */
    nj_root_release(vm, roots);
    return st;
}

/* ------------------------------------------------------------------ */
/* Tables                                                             */
/* ------------------------------------------------------------------ */
#define M(name, desc, flags) { name, desc, flags }
#define FLD(name, desc, flags) { name, desc, flags }

static const nj_builtin_field recordstore_fields[] = {
    FLD(NJ_F_RS_NAME,     "Ljava/lang/String;",  0),
    FLD(NJ_F_RS_RECORDS,  "Ljava/util/Vector;",  0),
    FLD(NJ_F_RS_IDS,      "Ljava/util/Vector;",  0),
    FLD(NJ_F_RS_NEXTID,   "I",                   0),
    FLD(NJ_F_RS_OPEN,     "Z",                   0),
    /* Neither access mode ever leaves 0/1 (openRecordStore's authmode and
     * setMode() are both validated against the two constants), and the class
     * library reads and writes them through rs_geti/rs_seti, which try "I"
     * then "Z".  Keeping the two one-byte fields together puts the long on
     * the same 8-byte boundary it needs and the int-sized hole that used to
     * follow rsOpen is reused: the instance stays at its pre-timestamp size
     * instead of growing by a whole 16-byte-aligned slot for one value. */
    FLD("rsMode",         "Z",                   0),
    FLD(NJ_F_RS_LASTMOD,  "J",                   0),
    /* The two access modes setMode() accepts. */
    FLD("AUTHMODE_PRIVATE", "I", NJ_ACC_STATIC | NJ_ACC_FINAL),
    FLD("AUTHMODE_ANY",     "I", NJ_ACC_STATIC | NJ_ACC_FINAL)
};

static const nj_builtin_method recordstore_methods[] = {
    M("openRecordStore", "(Ljava/lang/String;Z)L" NJ_C_RECORDSTORE ";",
      NJ_ACC_STATIC),
    M("openRecordStore", "(Ljava/lang/String;ZZ)L" NJ_C_RECORDSTORE ";",
      NJ_ACC_STATIC),
    M("openRecordStore", "(Ljava/lang/String;ZIZ)L" NJ_C_RECORDSTORE ";",
      NJ_ACC_STATIC),
    M("openRecordStore",
      "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;)L"
      NJ_C_RECORDSTORE ";",
      NJ_ACC_STATIC),
    M("deleteRecordStore", "(Ljava/lang/String;)V", NJ_ACC_STATIC),
    M("listRecordStores", "()[Ljava/lang/String;", NJ_ACC_STATIC),
    M("getName",           "()Ljava/lang/String;", 0),
    M("setMode",           "(IZ)V", 0),
    M("getMode",           "()I", 0),
    M("getNumRecords",     "()I", 0),
    M("getSize",           "()I", 0),
    M("getSizeAvailable",  "()I", 0),
    M("getNextRecordID",   "()I", 0),
    M("getLastModified",   "()J", 0),
    M("getVersion",        "()I", 0),
    M("getRecordSize",     "(I)I", 0),
    M("addRecord",         "([BII)I", 0),
    M("getRecord",         "(I)[B", 0),
    M("getRecord",         "(I[BI)I", 0),
    M("setRecord",         "(I[BII)V", 0),
    M("deleteRecord",      "(I)V", 0),
    M("closeRecordStore",  "()V", 0),
    M("addRecordListener", "(Ljavax/microedition/rms/RecordListener;)V", 0),
    M("removeRecordListener", "(Ljavax/microedition/rms/RecordListener;)V", 0),
    M("enumerateRecords",
      "(Ljavax/microedition/rms/RecordFilter;"
      "Ljavax/microedition/rms/RecordComparator;Z)"
      "Ljavax/microedition/rms/RecordEnumeration;", 0)
};

static const nj_builtin_field renum_fields[] = {
    FLD("reIds",       "[I",                     0),
    FLD("reStore",     "L" NJ_C_RECORDSTORE ";", 0),
    FLD("rePos",       "I",                      0),
    FLD("reKeep",      "Z",                      0),
    FLD("reDestroyed", "Z",                      0)
};

static const nj_builtin_field rmeta_fields[] = {
    FLD("enumerations", "Ljava/util/Vector;", 0),
    FLD("openCount", "I", 0)
};

static const nj_builtin_field renum_impl_fields[] = {
    FLD("filter", "L" NJ_C_RFILTER ";", 0),
    FLD("comparator", "L" NJ_C_RCOMP ";", 0),
    FLD("busy", "I", 0)
};

static const nj_builtin_method renum_methods[] = {
    M("numRecords",       "()I",   0),
    M("nextRecord",       "()[B",  0),
    M("previousRecord",   "()[B",  0),
    M("nextRecordId",     "()I",   0),
    M("previousRecordId", "()I",   0),
    M("hasNextElement",   "()Z",   0),
    M("hasPreviousElement", "()Z", 0),
    M("reset",            "()V",   0),
    M("rebuild",          "()V",   0),
    M("destroy",          "()V",   0),
    M("isKeptUpdated",    "()Z",   0),
    M("keepUpdated",      "(Z)V",  0)
};

static const nj_builtin_method rfilter_methods[] = {
    M("matches", "([B)Z", NJ_ACC_PUBLIC | NJ_ACC_ABSTRACT)
};

static const nj_builtin_field rcomp_fields[] = {
    FLD("EQUIVALENT", "I", NJ_ACC_PUBLIC | NJ_ACC_STATIC | NJ_ACC_FINAL),
    FLD("PRECEDES",   "I", NJ_ACC_PUBLIC | NJ_ACC_STATIC | NJ_ACC_FINAL),
    FLD("FOLLOWS",    "I", NJ_ACC_PUBLIC | NJ_ACC_STATIC | NJ_ACC_FINAL)
};

static const nj_builtin_method rcomp_methods[] = {
    M("compare", "([B[B)I", NJ_ACC_PUBLIC | NJ_ACC_ABSTRACT)
};

/* The exception hierarchy: no members of their own, inherited constructors. */
#define NJ_RMS_EXC(name, super)                                               \
    { name, super, NJ_ACC_PUBLIC, NULL, 0u, NULL, 0u },

static const nj_builtin_method listener_methods[] = {
    M("recordAdded",   "(L" NJ_C_RECORDSTORE ";I)V", NJ_ACC_ABSTRACT),
    M("recordChanged", "(L" NJ_C_RECORDSTORE ";I)V", NJ_ACC_ABSTRACT),
    M("recordDeleted", "(L" NJ_C_RECORDSTORE ";I)V", NJ_ACC_ABSTRACT)
};

const nj_builtin_class nj_lib_rms_classes[] = {
    { NJ_C_RECORDSTORE, "java/lang/Object", NJ_ACC_PUBLIC,
      recordstore_fields,
      (nj_u2)(sizeof(recordstore_fields) / sizeof(recordstore_fields[0])),
      recordstore_methods,
      (nj_u2)(sizeof(recordstore_methods) / sizeof(recordstore_methods[0])) },
    { "javax/microedition/rms/RecordListener",
      NULL, NJ_ACC_PUBLIC | NJ_ACC_INTERFACE | NJ_ACC_ABSTRACT,
      NULL, 0u, listener_methods,
      (nj_u2)(sizeof(listener_methods) / sizeof(listener_methods[0])) },
    { NJ_C_RENUM, "java/lang/Object", NJ_ACC_PUBLIC,
      renum_fields,
      (nj_u2)(sizeof(renum_fields) / sizeof(renum_fields[0])),
      renum_methods,
      (nj_u2)(sizeof(renum_methods) / sizeof(renum_methods[0])) },
    { NJ_C_RFILTER, NULL, NJ_ACC_PUBLIC | NJ_ACC_INTERFACE | NJ_ACC_ABSTRACT,
      NULL, 0u, rfilter_methods,
      (nj_u2)(sizeof(rfilter_methods) / sizeof(rfilter_methods[0])) },
    { NJ_C_RCOMP, NULL, NJ_ACC_PUBLIC | NJ_ACC_INTERFACE | NJ_ACC_ABSTRACT,
      rcomp_fields,
      (nj_u2)(sizeof(rcomp_fields) / sizeof(rcomp_fields[0])),
      rcomp_methods,
      (nj_u2)(sizeof(rcomp_methods) / sizeof(rcomp_methods[0])) },
    NJ_RMS_EXC("javax/microedition/rms/RecordStoreException",
               "java/lang/Exception")
    NJ_RMS_EXC("javax/microedition/rms/RecordStoreNotFoundException",
               "javax/microedition/rms/RecordStoreException")
    NJ_RMS_EXC("javax/microedition/rms/RecordStoreNotOpenException",
               "javax/microedition/rms/RecordStoreException")
    NJ_RMS_EXC("javax/microedition/rms/RecordStoreFullException",
               "javax/microedition/rms/RecordStoreException")
    NJ_RMS_EXC("javax/microedition/rms/InvalidRecordIDException",
               "javax/microedition/rms/RecordStoreException")
    { NJ_C_RMETA, "java/util/Vector", 0,
      rmeta_fields, (nj_u2)(sizeof(rmeta_fields) / sizeof(rmeta_fields[0])),
      NULL, 0u },
    { NJ_C_RENUM_IMPL, NJ_C_RENUM, 0,
      renum_impl_fields,
      (nj_u2)(sizeof(renum_impl_fields) / sizeof(renum_impl_fields[0])),
      NULL, 0u }
};
const int nj_lib_rms_class_count =
    (int)(sizeof(nj_lib_rms_classes) / sizeof(nj_lib_rms_classes[0]));

const nj_native_entry nj_lib_rms_table[] = {
    { NJ_C_RECORDSTORE, "openRecordStore", "(Ljava/lang/String;Z)L" NJ_C_RECORDSTORE ";", rs_open },
    { NJ_C_RECORDSTORE, "openRecordStore", "(Ljava/lang/String;ZZ)L" NJ_C_RECORDSTORE ";", rs_open_three },
    { NJ_C_RECORDSTORE, "openRecordStore", "(Ljava/lang/String;ZIZ)L" NJ_C_RECORDSTORE ";", rs_open_auth },
    { NJ_C_RECORDSTORE, "openRecordStore",
      "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;)L" NJ_C_RECORDSTORE ";",
      rs_open_named },
    { NJ_C_RECORDSTORE, "deleteRecordStore", "(Ljava/lang/String;)V", rs_delete },
    { NJ_C_RECORDSTORE, "listRecordStores", "()[Ljava/lang/String;", rs_listNames },
    { NJ_C_RECORDSTORE, "getName",         "()Ljava/lang/String;", rs_getName },
    { NJ_C_RECORDSTORE, "setMode",         "(IZ)V", rs_setMode },
    { NJ_C_RECORDSTORE, "getMode",         "()I", rs_getMode },
    { NJ_C_RECORDSTORE, "getNumRecords",   "()I", rs_getNumRecords },
    { NJ_C_RECORDSTORE, "getSize",         "()I", rs_getSize },
    { NJ_C_RECORDSTORE, "getSizeAvailable","()I", rs_getSizeAvailable },
    { NJ_C_RECORDSTORE, "getNextRecordID", "()I", rs_getNextRecordID },
    { NJ_C_RECORDSTORE, "getLastModified", "()J", rs_getLastModified },
    { NJ_C_RECORDSTORE, "getVersion",      "()I", rs_getVersion },
    { NJ_C_RECORDSTORE, "getRecordSize",   "(I)I", rs_getRecordSize },
    { NJ_C_RECORDSTORE, "addRecord",       "([BII)I", rs_addRecord },
    { NJ_C_RECORDSTORE, "getRecord",       "(I)[B", rs_getRecord },
    { NJ_C_RECORDSTORE, "getRecord",       "(I[BI)I", rs_getRecordInto },
    { NJ_C_RECORDSTORE, "setRecord",       "(I[BII)V", rs_setRecord },
    { NJ_C_RECORDSTORE, "deleteRecord",    "(I)V", rs_deleteRecord },
    { NJ_C_RECORDSTORE, "closeRecordStore","()V", rs_close },
    { NJ_C_RECORDSTORE, "addRecordListener",    "(Ljavax/microedition/rms/RecordListener;)V", rs_listener_noop },
    { NJ_C_RECORDSTORE, "removeRecordListener", "(Ljavax/microedition/rms/RecordListener;)V", rs_listener_noop },
    { NJ_C_RECORDSTORE, "enumerateRecords",
      "(Ljavax/microedition/rms/RecordFilter;"
      "Ljavax/microedition/rms/RecordComparator;Z)"
      "Ljavax/microedition/rms/RecordEnumeration;", rs_enumerateRecords },
    { NJ_C_RENUM, "numRecords",         "()I",  re_numRecords },
    { NJ_C_RENUM, "nextRecord",         "()[B", re_nextRecord },
    { NJ_C_RENUM, "previousRecord",     "()[B", re_previousRecord },
    { NJ_C_RENUM, "nextRecordId",       "()I",  re_nextId },
    { NJ_C_RENUM, "previousRecordId",   "()I",  re_previousId },
    { NJ_C_RENUM, "hasNextElement",     "()Z",  re_hasNext },
    { NJ_C_RENUM, "hasPreviousElement", "()Z",  re_hasPrev },
    { NJ_C_RENUM, "reset",              "()V",  re_reset },
    { NJ_C_RENUM, "rebuild",            "()V",  re_rebuild },
    { NJ_C_RENUM, "destroy",            "()V",  re_destroy },
    { NJ_C_RENUM, "isKeptUpdated",      "()Z",  re_isKept },
    { NJ_C_RENUM, "keepUpdated",        "(Z)V", re_keepUpdated }
};
const int nj_lib_rms_count =
    (int)(sizeof(nj_lib_rms_table) / sizeof(nj_lib_rms_table[0]));

/*
 * A builtin class carries no ConstantValue attribute, so the comparator's
 * ordering constants have to be planted at run time.
 *
 * They are load-bearing rather than decorative: javac inlines the STUB's value
 * into every MIDlet that compares against RecordComparator.PRECEDES or
 * FOLLOWS, so if the runtime holds a different number the sorter silently
 * orders backwards while both halves look self-consistent.  tests/test_consts
 * compares the two sides for exactly this reason.
 */
static const struct { const char *name; nj_s4 value; } rcomp_consts[] = {
    { "PRECEDES",   -1 },
    { "FOLLOWS",     1 },
    { "EQUIVALENT",  0 }
};

/* RecordStore's MIDP 1.0 access modes, written by setMode()/getMode(). */
static const struct { const char *name; nj_s4 value; } rs_consts[] = {
    { "AUTHMODE_PRIVATE", 0 },
    { "AUTHMODE_ANY",     1 }
};

nj_status nj_rms_late_init(nj_vm *vm)
{
    size_t i;

    for (i = 0; i < sizeof(rcomp_consts) / sizeof(rcomp_consts[0]); i++) {
        nj_field *f = nj_field_lookup(vm, NJ_C_RCOMP, rcomp_consts[i].name,
                                      "I", 1);
        if (f) {
            nj_field_write_int(vm, NULL, f, rcomp_consts[i].value);
        }
    }
    for (i = 0; i < sizeof(rs_consts) / sizeof(rs_consts[0]); i++) {
        nj_field *f = nj_field_lookup(vm, NJ_C_RECORDSTORE, rs_consts[i].name,
                                      "I", 1);
        if (f) {
            nj_field_write_int(vm, NULL, f, rs_consts[i].value);
        }
    }
    return NJ_OK;
}
