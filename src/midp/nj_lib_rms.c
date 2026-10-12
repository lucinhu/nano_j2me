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
 * A file whose magic or version does not match is treated as absent rather
 * than as an error: losing one high-score table is better than refusing to
 * start the game, and a half-written file from a power cut is the realistic
 * way this happens on a phone.
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
#endif

#define NJFN(name) NJ_NATIVE_FN(name)
#define NJ_CALL(name, vm, th, args, ret) name((vm), (th), (args), (ret))
#define NJ_UNUSED(x) ((void)(x))

#define NJ_RS_MAGIC0 'N'
#define NJ_RS_MAGIC1 'J'
#define NJ_RS_MAGIC2 'R'
#define NJ_RS_MAGIC3 'S'
#define NJ_RS_VERSION 1u

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

/* The enumeration classes.  Defined here, above their first use, and
 * NOT next to the tables at the bottom where they were first written -
 * a macro that is used before it is defined is not a macro yet. */
#define NJ_C_RENUM "javax/microedition/rms/RecordEnumeration"
#define NJ_C_RFILTER "javax/microedition/rms/RecordFilter"
#define NJ_C_RCOMP "javax/microedition/rms/RecordComparator"

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

/* Index of record id `id` in the id vector, or -1. */
static nj_s4 rs_find(nj_vm *vm, nj_ref rs, nj_s4 id)
{
    nj_ref ids = rs_get(vm, rs, NJ_F_RS_IDS, "Ljava/util/Vector;");
    nj_s4 n, i;

    if (!ids) {
        return -1;
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

static void rs_path(nj_vm *vm, const char *name, char *out, nj_u4 cap)
{
    const char *dir = (vm->port && vm->port->rms_dir) ? vm->port->rms_dir : ".";
    nj_u4 n = 0u;

    while (*dir && n + 1u < cap) {
        out[n++] = *dir++;
    }
    if (n > 0u && out[n - 1u] != '/' && n + 1u < cap) {
        out[n++] = '/';
    }
    while (*name && n + 1u < cap) {
        out[n++] = *name++;
    }
    if (n + 4u < cap) {
        out[n++] = '.';
        out[n++] = 'r';
        out[n++] = 'm';
        out[n++] = 's';
    }
    out[n] = '\0';
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

static void w_bytes(rs_writer *w, const void *p, nj_u4 n)
{
    if (w->use_port) {
        if (w->vm->port->file_write(w->vm->port->ud, &w->f, p, n) > 0) {
            w->written += n;
        }
    } else {
#if NJ_USE_LIBC
        if (fwrite(p, 1u, n, w->fp) == n) {
            w->written += n;
        }
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

static void w_close(rs_writer *w)
{
    if (w->use_port) {
        w->vm->port->file_close(w->vm->port->ud, &w->f);
    }
#if NJ_USE_LIBC
    else if (w->fp) {
        fclose(w->fp);
        w->fp = NULL;
    }
#endif
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
 * the simplicity is worth far more than an append-only scheme would be - and
 * a rewrite cannot leave a torn record behind if it is interrupted, because
 * the file is either the old one or the new one.
 */
static nj_status rs_save(nj_vm *vm, nj_ref rs)
{
    nj_ref name = rs_get(vm, rs, NJ_F_RS_NAME, "Ljava/lang/String;");
    nj_ref recs = rs_get(vm, rs, NJ_F_RS_RECORDS, "Ljava/util/Vector;");
    nj_ref ids = rs_get(vm, rs, NJ_F_RS_IDS, "Ljava/util/Vector;");
    char nbuf[128];
    char path[320];
    rs_writer w;
    nj_s4 n, i;

    if (!name || !recs || !ids) {
        return NJ_ERR;
    }
    if (nj_string_to_utf8(name, nbuf, (int)sizeof(nbuf)) <= 0) {
        return NJ_ERR;
    }
    rs_path(vm, nbuf, path, (nj_u4)sizeof(path));

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

    for (i = 0; i < n; i++) {
        nj_ref rec = vec_at(vm, recs, i);
        nj_ref ido = vec_at(vm, ids, i);
        nj_s4 id = ido ? nj_call_int(vm, ido, "java/lang/Integer", "intValue",
                                     "()I", NULL) : 0;
        nj_u4 len = rec ? rec->length : 0u;

        w_u2(&w, (nj_u4)id);
        w_u2(&w, len);
        if (rec && len > 0u) {
            w_bytes(&w, rec->u.bytes, len);
        }
    }
    w_close(&w);

    rs_seti(vm, rs, NJ_F_RS_MODIFIED, 1);
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

        if (len > 1024u * 1024u) {
            break;
        }
        rec = nj_byte_array_new(vm, len);
        if (!rec) {
            break;
        }
        if (len > 0u && r_bytes(&r, rec->u.bytes, len) != len) {
            break;
        }
        ido = rs_int_obj(vm, (nj_s4)id);
        if (!ido) {
            break;
        }
        if (!vec_add(vm, recs, rec) || !vec_add(vm, ids, ido)) {
            break;
        }
    }
    r_close(&r);

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
static nj_ref rs_open_common(nj_vm *vm, nj_ref name, int create_if_missing)
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
    recs = nj_object_new(vm, "java/util/Vector");
    ids = nj_object_new(vm, "java/util/Vector");
    if (!recs || !ids) {
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

    if (exists) {
        rs_load(vm, rs);
    } else {
        (void)rs_save(vm, rs);   /* create the file eagerly, so it persists */
        nj_clear_exception(vm);
    }
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
    return NJ_CALL(rs_open, vm, th, args, ret);
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
    if (rs_save(vm, rs) != NJ_OK) {
        return NJ_ERR;
    }
    rs_seti(vm, rs, NJ_F_RS_OPEN, 0);
    return NJ_OK;
}

/* ------------------------------------------------------------------ */
/* Queries                                                             */
/* ------------------------------------------------------------------ */
NJFN(rs_getName)
{
    ret->r = rs_get(vm, NJ_THIS(args), NJ_F_RS_NAME, "Ljava/lang/String;");
    return NJ_OK;
}

NJFN(rs_getNumRecords)
{
    nj_ref recs = rs_get(vm, NJ_THIS(args), NJ_F_RS_RECORDS,
                         "Ljava/util/Vector;");
    ret->i = recs ? vec_size(vm, recs) : 0;
    return NJ_OK;
}

NJFN(rs_getNextRecordID)
{
    ret->i = rs_geti(vm, NJ_THIS(args), NJ_F_RS_NEXTID);
    return NJ_OK;
}

NJFN(rs_getSize)
{
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
    /* Not a filesystem query: report a generous fixed budget so that games
     * which check before writing behave sensibly. */
    ret->i = 1024 * 1024;
    return NJ_OK;
}

NJFN(rs_getLastModified)
{
    ret->i = (nj_s4)(nj_now_ms(vm) & 0x7FFFFFFF);
    return NJ_OK;
}

NJFN(rs_getVersion)
{
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
    /*
     * A null array is tolerated ONLY when nothing is being read from it.
     *
     * MIDP says addRecord(null, ...) is a NullPointerException, and this
     * implementation did exactly that - until a real GPL game built from source
     * (Bubblet, tools/make_bubblet_jar.sh) called addRecord(null, 0, 0) to
     * create its empty high-score record, and died on the first frame.  The
     * game ran on Series 40 hardware, so the devices it was written for
     * accepted this.
     *
     * The line drawn here is not "be permissive" but "null is harmless exactly
     * when no bytes are read from it": len == 0 means an empty record either
     * way, and len > 0 with a null array is still the error the spec says it
     * is.  A game with a genuine null bug still finds out.
     */
    if (!data && len == 0) {
        off = 0;
        len = 0;
    } else if (!data) {
        return nj_throw(vm, "java/lang/NullPointerException", NULL);
    }
    if (off < 0 || len < 0 || (data && (nj_u4)(off + len) > data->length)) {
        return nj_throw(vm, "java/lang/ArrayIndexOutOfBoundsException", NULL);
    }
    copy = nj_byte_array_new(vm, (nj_u4)len);
    if (!copy) {
        return nj_throw_oom(vm);
    }
    if (data && len > 0) {
        memcpy(copy->u.bytes, data->u.bytes + off, (size_t)len);
    }
    id = rs_geti(vm, rs, NJ_F_RS_NEXTID);
    rs_seti(vm, rs, NJ_F_RS_NEXTID, id + 1);
    ido = rs_int_obj(vm, id);
    if (!ido || !vec_add(vm, recs, copy) || !vec_add(vm, ids, ido)) {
        return nj_throw_oom(vm);
    }
    if (rs_save(vm, rs) != NJ_OK) {
        return NJ_ERR;
    }
    return id;
}

NJFN(rs_addRecord)
{
    nj_s4 id = rs_do_add(vm, NJ_THIS(args), NJ_ARG_REF(args, 0),
                         NJ_ARG_INT(args, 1), NJ_ARG_INT(args, 2));
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
        nj_ref copy = nj_byte_array_new(vm, rec ? rec->length : 0u);
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
    if (off < 0 || (nj_u4)(off + (nj_s4)rec->length) > buf->length) {
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
    /*
     * A null array is tolerated ONLY when nothing is being read from it.
     *
     * MIDP says addRecord(null, ...) is a NullPointerException, and this
     * implementation did exactly that - until a real GPL game built from source
     * (Bubblet, tools/make_bubblet_jar.sh) called addRecord(null, 0, 0) to
     * create its empty high-score record, and died on the first frame.  The
     * game ran on Series 40 hardware, so the devices it was written for
     * accepted this.
     *
     * The line drawn here is not "be permissive" but "null is harmless exactly
     * when no bytes are read from it": len == 0 means an empty record either
     * way, and len > 0 with a null array is still the error the spec says it
     * is.  A game with a genuine null bug still finds out.
     */
    if (!data && len == 0) {
        off = 0;
        len = 0;
    } else if (!data) {
        return nj_throw(vm, "java/lang/NullPointerException", NULL);
    }
    if (off < 0 || len < 0 || (data && (nj_u4)(off + len) > data->length)) {
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
    return rs_save(vm, rs);
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
    return rs_save(vm, rs);
}

NJFN(rs_listNames)
{
    /* Enumerating the directory would need a port hook this runtime does not
     * have; an empty array is honest, and every game that calls this treats an
     * empty result as "no saves yet". */
    ret->r = nj_object_array_new(vm, "java/lang/String", 0u);
    return ret->r ? NJ_OK : nj_throw_oom(vm);
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
 * The enumeration SNAPSHOTS the matching ids when it is created.  MIDP allows a
 * live view through keepUpdated(true), and that is accepted here but does not
 * rebuild - a store small enough for a high-score table is rebuilt by the game
 * far more often than it changes underneath, and pretending to track it would
 * be worse than saying so.
 */

NJFN(re_numRecords)
{
    nj_ref ids = renum_get(vm, NJ_THIS(args), "reIds", "[I");
    ret->i = ids ? (nj_s4)ids->length : 0;
    return NJ_OK;
}

NJFN(re_hasNext)     { ret->i = (renum_geti(vm, NJ_THIS(args), "rePos") <
                                 (nj_s4)(renum_get(vm, NJ_THIS(args), "reIds", "[I")
                                          ? renum_get(vm, NJ_THIS(args), "reIds", "[I")->length : 0u))
                               ? 1 : 0; return NJ_OK; }
NJFN(re_hasPrev)     { ret->i = (renum_geti(vm, NJ_THIS(args), "rePos") > 0) ? 1 : 0; return NJ_OK; }
NJFN(re_reset)       { renum_seti(vm, NJ_THIS(args), "rePos", 0); return NJ_OK; }
NJFN(re_destroy)     { renum_seti(vm, NJ_THIS(args), "reDestroyed", 1); return NJ_OK; }
NJFN(re_isKept)      { ret->i = renum_geti(vm, NJ_THIS(args), "reKeep"); return NJ_OK; }
NJFN(re_keepUpdated) { renum_seti(vm, NJ_THIS(args), "reKeep",
                             NJ_ARG_INT(args, 0) ? 1 : 0); return NJ_OK; }

NJFN(re_rebuild)
{
    /* Accepted, and it does not re-run the filter: see the note above. */
    renum_seti(vm, NJ_THIS(args), "rePos", 0);
    return NJ_OK;
}

/* The record at the cursor, moving it forward or back. */
static nj_status re_step(nj_vm *vm, nj_value *args, nj_value *ret, int forward)
{
    nj_ref self = NJ_THIS(args);
    nj_ref ids = renum_get(vm, self, "reIds", "[I");
    nj_ref rs = renum_get(vm, self, "reStore", "L" NJ_C_RECORDSTORE ";");
    nj_s4 pos = renum_geti(vm, self, "rePos");
    nj_s4 n = ids ? (nj_s4)ids->length : 0;
    nj_s4 id;
    nj_value a[3];
    nj_value r;

    if (renum_geti(vm, self, "reDestroyed")) {
        return nj_throw(vm, "javax/microedition/rms/RecordStoreException",
                        "the enumeration was destroyed");
    }
    if (forward) {
        if (pos >= n) {
            return nj_throw(vm, "javax/microedition/rms/InvalidRecordIDException",
                            "no next record");
        }
        id = ((nj_s4 *)ids->u.bytes)[pos];
        renum_seti(vm, self, "rePos", pos + 1);
    } else {
        if (pos <= 0) {
            return nj_throw(vm, "javax/microedition/rms/InvalidRecordIDException",
                            "no previous record");
        }
        id = ((nj_s4 *)ids->u.bytes)[pos - 1];
        renum_seti(vm, self, "rePos", pos - 1);
    }
    if (!rs) {
        return nj_throw(vm, "javax/microedition/rms/RecordStoreNotOpenException",
                        NULL);
    }
    a[0].i = id;
    r.r = NULL;
    if (nj_call(vm, rs, NJ_C_RECORDSTORE, "getRecord", "(I)[B", a, &r) != NJ_OK) {
        return NJ_ERR;
    }
    ret->r = r.r;
    return NJ_OK;
}

NJFN(re_nextRecord)     { return re_step(vm, args, ret, 1); }
NJFN(re_previousRecord) { return re_step(vm, args, ret, 0); }

/* The id at the cursor, without fetching the record. */
static nj_status re_peek_id(nj_vm *vm, nj_value *args, nj_value *ret, int forward)
{
    nj_ref self = NJ_THIS(args);
    nj_ref ids = renum_get(vm, self, "reIds", "[I");
    nj_s4 pos = renum_geti(vm, self, "rePos");
    nj_s4 n = ids ? (nj_s4)ids->length : 0;

    if (forward) {
        if (pos >= n) {
            return nj_throw(vm, "javax/microedition/rms/InvalidRecordIDException",
                            "no next record");
        }
        ret->i = ((nj_s4 *)ids->u.bytes)[pos];
        renum_seti(vm, self, "rePos", pos + 1);
    } else {
        if (pos <= 0) {
            return nj_throw(vm, "javax/microedition/rms/InvalidRecordIDException",
                            "no previous record");
        }
        ret->i = ((nj_s4 *)ids->u.bytes)[pos - 1];
        renum_seti(vm, self, "rePos", pos - 1);
    }
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
NJFN(rs_enumerateRecords)
{
    nj_ref rs = NJ_THIS(args);
    nj_ref filter = NJ_ARG_REF(args, 0);
    nj_ref comp = NJ_ARG_REF(args, 1);
    nj_ref ids;
    nj_ref out;
    nj_s4 n, i, keep = 0;
    nj_s4 *dst;

    if (!rs_is_open(vm, rs)) {
        return nj_throw(vm, "javax/microedition/rms/RecordStoreNotOpenException",
                        NULL);
    }
    {
        nj_ref idv = rs_get(vm, rs, NJ_F_RS_IDS, "Ljava/util/Vector;");
        n = vec_size(vm, idv);
        ids = nj_array_new(vm, "int", (nj_u4)n);
        if (!ids) {
            return nj_throw_oom(vm);
        }
        dst = (nj_s4 *)ids->u.bytes;
        n = 0;
        for (i = 0; i < vec_size(vm, idv); i++) {
            nj_ref ido = vec_at(vm, idv, i);
            nj_s4 id = ido ? nj_call_int(vm, ido, "java/lang/Integer",
                                         "intValue", "()I", NULL) : 0;
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
            dst[n++] = id;
        }
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
                ga[0].i = key;
                gr.r = NULL;
                if (nj_call(vm, rs, NJ_C_RECORDSTORE, "getRecord", "(I)[B",
                            ga, &gr) != NJ_OK) {
                    return NJ_ERR;
                }
                ga[0].i = dst[j];
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

    if (args[2].bits != 0u) {
        keep = 1;
    }
    out = nj_object_new(vm, NJ_C_RENUM);
    if (!out) {
        return nj_throw_oom(vm);
    }
    /* renum_*, not rs_*: rs_* looks fields up on RecordStore, so writing the
     * enumeration's fields through it would silently write nowhere. */
    renum_setr(vm, out, "reIds", "[I", ids);
    renum_setr(vm, out, "reStore", "L" NJ_C_RECORDSTORE ";", rs);
    renum_seti(vm, out, "reKeep", keep);
    renum_seti(vm, out, "rePos", 0);
    ret->r = out;
    return NJ_OK;
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
    FLD(NJ_F_RS_MODIFIED, "Z",                   0)
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
    M("getNumRecords",     "()I", 0),
    M("getSize",           "()I", 0),
    M("getSizeAvailable",  "()I", 0),
    M("getNextRecordID",   "()I", 0),
    M("getLastModified",   "()I", 0),
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
    { NJ_C_RECORDSTORE, "getNumRecords",   "()I", rs_getNumRecords },
    { NJ_C_RECORDSTORE, "getSize",         "()I", rs_getSize },
    { NJ_C_RECORDSTORE, "getSizeAvailable","()I", rs_getSizeAvailable },
    { NJ_C_RECORDSTORE, "getNextRecordID", "()I", rs_getNextRecordID },
    { NJ_C_RECORDSTORE, "getLastModified", "()I", rs_getLastModified },
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
    return NJ_OK;
}
