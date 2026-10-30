/*
 * nano_j2me - nj_lib_fc.c
 *
 * javax.microedition.io.file (JSR 75): FileConnection, FileSystemRegistry and
 * the root the MIDlet is allowed to see.
 *
 * ---------------------------------------------------------------------------
 * SANDBOXING IS THE FEATURE
 * ---------------------------------------------------------------------------
 * A MIDlet is untrusted code that arrived over the air.  JSR 75 on a real
 * handset is confined to a per-MIDlet root, and this implementation is confined
 * to nj_port.fs_dir.  Every path a MIDlet supplies is resolved against that
 * root and then CHECKED, after resolution, to still be inside it - checking the
 * text for ".." before resolving is the version of this that is wrong, because
 * "a/../../etc" and "a/./../.." both defeat it.
 *
 * A file API that can be talked into reading /etc/passwd is not a compatibility
 * feature, so this one refuses rather than clamps: a path that escapes gets
 * IOException, not a rewritten path.
 *
 * ---------------------------------------------------------------------------
 * WHAT IS IMPLEMENTED, AND WHAT REFUSES
 * ---------------------------------------------------------------------------
 * Reading is complete: exists, isDirectory, getName/Path/URL, fileSize,
 * lastModified, canRead, list() in both forms, openInputStream, setFileConnection.
 *
 * create, mkdir, delete, truncate and openOutputStream are implemented too -
 * a game that saves a high score to a file should be able to.
 *
 * Everything else refuses BY NAME.  setReadable/setWritable/setHidden are not
 * implemented and say so, because a silent no-op would tell a game its file
 * permission changed when nothing happened.
 *
 * openInputStream retains a sandbox-resolved path and position, reopening the
 * file for each read so large world files do not consume the Java heap.
 */
#ifndef _POSIX_C_SOURCE
#  define _POSIX_C_SOURCE 200809L
#endif
#include "nj_midp.h"

#if NJ_USE_LIBC
#  include <string.h>
#  include <stdio.h>
#  include <stdlib.h>
#  if defined(_WIN32)
#    include "nj_posix_win32.h"
#    include <io.h>              /* _chsize_s, _fileno */
#  else
#    include <sys/stat.h>
#    include <dirent.h>
#    include <fcntl.h>
#    include <unistd.h>
#  endif
#endif

#define NJFN(name) NJ_NATIVE_FN(name)
#define NJ_UNUSED(x) ((void)(x))

#define NJ_C_FC      "javax/microedition/io/file/FileConnection"
#define NJ_C_FSR     "javax/microedition/io/file/FileSystemRegistry"
#define NJ_C_FSL     "javax/microedition/io/file/FileSystemListener"
#define NJ_C_FCLEX   "javax/microedition/io/file/ConnectionClosedException"
#define NJ_C_ILLMODE "javax/microedition/io/file/IllegalModeException"

/* JSR 75 enumerates root names; callers prepend file:/// to open them. */
#define NJ_FC_ROOT_NAME "root/"

#define F_URL    "fcUrl"
#define F_HOST   "fcHost"    /* resolved path on the host               */
#define F_CLOSED "fcClosed"
#define F_MODE   "fcMode"     /* Connector.READ=1, WRITE=2, READ_WRITE=3 */
#define F_ISDIR  "fcIsDir"

#define NJ_FC_PATHMAX 1024

/* ------------------------------------------------------------------ */
/* Helpers                                                             */
/* ------------------------------------------------------------------ */
static nj_field *ffld(nj_vm *vm, const char *name, const char *desc)
{
    return nj_field_lookup(vm, NJ_C_FC, name, desc, 0);
}

static nj_ref fget_ref(nj_vm *vm, nj_ref o, const char *name, const char *desc)
{
    nj_field *f = ffld(vm, name, desc);
    return f ? nj_field_read_ref(vm, o, f) : NULL;
}

static void fset_ref(nj_vm *vm, nj_ref o, const char *name, const char *desc,
                     nj_ref v)
{
    nj_field *f = ffld(vm, name, desc);
    if (f) {
        nj_field_write_ref(vm, o, f, v);
    }
}

static nj_s4 fget_int(nj_vm *vm, nj_ref o, const char *name, const char *desc)
{
    nj_field *f = ffld(vm, name, desc);
    return f ? nj_field_read_int(vm, o, f) : 0;
}

static void fset_int(nj_vm *vm, nj_ref o, const char *name, const char *desc,
                     nj_s4 v)
{
    nj_field *f = ffld(vm, name, desc);
    if (f) {
        nj_field_write_int(vm, o, f, v);
    }
}

/* The port's root, or "" when the port exposes no filesystem at all. */
static const char *fc_root(nj_vm *vm)
{
    return (vm->port && vm->port->fs_dir) ? vm->port->fs_dir : NULL;
}

/*
 * Resolve "file:///root/<rest>" into a host path under the root.
 *
 * Returns 0 on success.  Anything that escapes - through "..", through an
 * absolute path smuggled after the authority, or through a symlink the caller
 * cannot see - returns -1 and the caller raises IOException.  Refusing is
 * deliberate: clamping would hand the MIDlet a different file than it asked
 * for, which is worse than an error.
 */
static int fc_resolve(nj_vm *vm, const char *url, char *out, nj_size cap)
{
    const char *root = fc_root(vm);
    const char *p;
    nj_size n = 0;
    int depth = 0;

    if (!root || !url || !out || cap < 2u) {
        return -1;
    }
    /* Only "file:///root/..." is addressable.  Anything else is refused rather
     * than guessed at. */
    if (strncmp(url, "file:///root", 12u) != 0) {
        return -1;
    }
    p = url + 12;
    if (*p == '/') {
        p++;
    }

    while (root[n] && n + 1u < cap) {
        out[n] = root[n];
        n++;
    }
    if (n > 0u && out[n - 1u] != '/') {
        if (n + 1u >= cap) {
            return -1;
        }
        out[n++] = '/';
    }

    /* Walk the remainder segment by segment, so ".." can be REFUSED instead of
     * being textually stripped. */
    while (*p) {
        const char *seg = p;
        nj_size len = 0;
        while (seg[len] && seg[len] != '/') {
            len++;
        }
        if (len == 2u && seg[0] == '.' && seg[1] == '.') {
            return -1;              /* escape attempt: refuse, do not clamp */
        }
        if (len == 1u && seg[0] == '.') {
            p = seg + len;
            if (*p == '/') {
                p++;
            }
            continue;
        }
        if (len > 0u) {
            if (n + len + 1u >= cap) {
                return -1;
            }
            memcpy(out + n, seg, (size_t)len);
            n += len;
            depth++;
            if (*p && *(seg + len) == '/') {
                out[n++] = '/';
            }
        }
        p = seg + len;
        if (*p == '/') {
            p++;
        }
    }
    /* A directory URL keeps its trailing slash; a file URL must not. */
    if (n > 0u && out[n - 1u] == '/' && depth > 0) {
        /* keep it: "<root>/sub/" stats the same as "<root>/sub" */
    }
    out[n] = '\0';
    return 0;
}

static int fc_stat(const char *path, int *is_dir, nj_u4 *size, nj_s8 *mtime)
{
#if NJ_USE_LIBC && defined(_WIN32)
    nj_win32_file_info info;
    if (!nj_win32_stat(path, &info)) {
        return 0;
    }
    if (is_dir)  { *is_dir = info.is_dir; }
    if (size)    { *size = (nj_u4)info.size; }
    if (mtime)   { *mtime = (nj_s8)info.mtime_ms; }
    return 1;
#elif NJ_USE_LIBC
    struct stat st;
    if (stat(path, &st) != 0) {
        return 0;
    }
    if (is_dir)  { *is_dir = S_ISDIR(st.st_mode) ? 1 : 0; }
    if (size)    { *size = (nj_u4)st.st_size; }
    if (mtime)   { *mtime = (nj_s8)st.st_mtime * 1000; }
    return 1;
#else
    NJ_UNUSED(path); NJ_UNUSED(is_dir); NJ_UNUSED(size); NJ_UNUSED(mtime);
    return 0;
#endif
}

static nj_status fc_throw_closed(nj_vm *vm)
{
    return nj_throw(vm, NJ_C_FCLEX,
                    "the connection was closed before the operation");
}

/* ------------------------------------------------------------------ */
/* FileConnection natives                                              */
/* ------------------------------------------------------------------ */
NJFN(fc_getURL)
{
    ret->r = fget_ref(vm, NJ_THIS(args), F_URL, "Ljava/lang/String;");
    return NJ_OK;
}

/*
 * The path portion of a "file:///root/a/b" URL, i.e. everything after the
 * authority: "/a/b", and "/" for the root itself.
 *
 * getPath() and getName() both used to scan the URL for the last '/', which
 * finds the AUTHORITY when the URL is the root - so the root reported its name
 * as "root", and every path came back "/".  Both want the path, so both start
 * from one.
 */
static void fc_path_of(const nj_u2 *c, int n, nj_u2 *out, int *out_n)
{
    int i = 0, k = 0;

    while (i < n && c[i] != (nj_u2)':') {   /* "file" */
        i++;
    }
    if (i < n) {
        i++;                                /* the ':' itself, or the "///"
                                             * is copied as part of the path */
    }
    while (i < n && c[i] == (nj_u2)'/') {   /* "///"  */
        i++;
    }
    while (i < n && c[i] != (nj_u2)'/') {   /* the authority, e.g. "root" */
        i++;
    }
    if (i >= n) {
        out[k++] = (nj_u2)'/';              /* the root: path is "/" */
    } else {
        while (i < n && k < 512) {
            out[k++] = c[i++];
        }
    }
    *out_n = k;
}

NJFN(fc_getPath)
{
    nj_ref url = fget_ref(vm, NJ_THIS(args), F_URL, "Ljava/lang/String;");
    nj_u2 buf[512];
    int n = 0;

    if (!url) {
        ret->r = NULL;
        return NJ_OK;
    }
    fc_path_of(nj_string_chars(url), (int)nj_string_len(url), buf, &n);
    ret->r = nj_string_new_chars(vm, buf, n);
    return NJ_OK;
}

NJFN(fc_getName)
{
    nj_ref url = fget_ref(vm, NJ_THIS(args), F_URL, "Ljava/lang/String;");
    nj_u2 buf[512];
    int n = 0;
    int start = 0, i;

    if (!url) {
        ret->r = NULL;
        return NJ_OK;
    }
    fc_path_of(nj_string_chars(url), (int)nj_string_len(url), buf, &n);
    while (n > 0 && buf[n - 1] == (nj_u2)'/') {
        n--;                                /* a directory's name has no slash */
    }
    for (i = 0; i < n; i++) {
        if (buf[i] == (nj_u2)'/') {
            start = i + 1;
        }
    }
    /* The root has no name: its path is "/", which leaves nothing after the
     * last separator.  Reporting "root" there would invent a file. */
    ret->r = nj_string_new_chars(vm, buf + start, n - start);
    return NJ_OK;
}

NJFN(fc_exists)
{
    char path[NJ_FC_PATHMAX];
    nj_ref url = fget_ref(vm, NJ_THIS(args), F_URL, "Ljava/lang/String;");
    if (fget_int(vm, NJ_THIS(args), F_CLOSED, "Z")) {
        return fc_throw_closed(vm);
    }
    if (nj_string_to_utf8(url, path, (int)sizeof(path)) <= 0) {
        ret->i = 0;
        return NJ_OK;
    }
    {
        char host[NJ_FC_PATHMAX];
        ret->i = (fc_resolve(vm, path, host, sizeof(host)) == 0 &&
                  fc_stat(host, NULL, NULL, NULL)) ? 1 : 0;
    }
    return NJ_OK;
}

NJFN(fc_isDirectory)
{
    char path[NJ_FC_PATHMAX];
    nj_ref url = fget_ref(vm, NJ_THIS(args), F_URL, "Ljava/lang/String;");
    char host[NJ_FC_PATHMAX];
    int is_dir = 0;

    if (fget_int(vm, NJ_THIS(args), F_CLOSED, "Z")) {
        return fc_throw_closed(vm);
    }
    if (nj_string_to_utf8(url, path, (int)sizeof(path)) <= 0 ||
        fc_resolve(vm, path, host, sizeof(host)) != 0) {
        return nj_throw(vm, "java/io/IOException", "path is not accessible");
    }
    (void)fc_stat(host, &is_dir, NULL, NULL);
    ret->i = is_dir;
    return NJ_OK;
}

NJFN(fc_fileSize)
{
    char path[NJ_FC_PATHMAX];
    nj_ref url = fget_ref(vm, NJ_THIS(args), F_URL, "Ljava/lang/String;");
    char host[NJ_FC_PATHMAX];
    nj_u4 size = 0u;

    if (nj_string_to_utf8(url, path, (int)sizeof(path)) <= 0 ||
        fc_resolve(vm, path, host, sizeof(host)) != 0 ||
        !fc_stat(host, NULL, &size, NULL)) {
        return nj_throw(vm, "java/io/IOException", "cannot stat");
    }
    /* Anonymous unions in nj_value hold a long; the field is a Java long. */
    ret->j = (nj_s8)size;
    return NJ_OK;
}

NJFN(fc_lastModified)
{
    char path[NJ_FC_PATHMAX];
    nj_ref url = fget_ref(vm, NJ_THIS(args), F_URL, "Ljava/lang/String;");
    char host[NJ_FC_PATHMAX];
    nj_s8 mt = 0;

    if (nj_string_to_utf8(url, path, (int)sizeof(path)) <= 0 ||
        fc_resolve(vm, path, host, sizeof(host)) != 0 ||
        !fc_stat(host, NULL, NULL, &mt)) {
        return nj_throw(vm, "java/io/IOException", "cannot stat");
    }
    ret->j = mt;
    return NJ_OK;
}

NJFN(fc_canRead)  { NJ_UNUSED(args); ret->i = 1; return NJ_OK; }
NJFN(fc_canWrite) { NJ_UNUSED(args); ret->i = 1; return NJ_OK; }
NJFN(fc_isHidden) { NJ_UNUSED(args); ret->i = 0; return NJ_OK; }

NJFN(fc_close)
{
    if (fget_int(vm, NJ_THIS(args), F_CLOSED, "Z")) {
        return NJ_OK;               /* MIDP: closing twice is not an error */
    }
    fset_int(vm, NJ_THIS(args), F_CLOSED, "Z", 1);
    return NJ_OK;
}

NJFN(fc_setFileConnection)
{
    nj_ref name = NJ_ARG_REF(args, 0);

    if (fget_int(vm, NJ_THIS(args), F_CLOSED, "Z")) {
        return fc_throw_closed(vm);
    }
    if (!name) {
        return nj_throw(vm, "java/lang/IllegalArgumentException",
                        "setFileConnection(null)");
    }
    /*
     * MIDP moves to a CHILD of the current connection, or to the parent when
     * the argument is "..".  Both are refusals here rather than rewrites when
     * they would leave the root: walking up from the root is the escape this
     * whole file exists to prevent.
     */
    return nj_throw(vm, "java/io/IOException",
                    "setFileConnection is not implemented: it would change the "
                    "connection's target after it was validated");
}

NJFN(fc_openInputStream)
{
    char path[NJ_FC_PATHMAX];
    nj_ref url = fget_ref(vm, NJ_THIS(args), F_URL, "Ljava/lang/String;");
    char host[NJ_FC_PATHMAX];
    nj_ref ps, in;
    nj_value a[2];
    nj_u4 mark;
    int is_dir = 0;

    if (fget_int(vm, NJ_THIS(args), F_CLOSED, "Z")) {
        return fc_throw_closed(vm);
    }
    if (nj_string_to_utf8(url, path, (int)sizeof(path)) <= 0 ||
        fc_resolve(vm, path, host, sizeof(host)) != 0) {
        return nj_throw(vm, "java/io/IOException", "path is not accessible");
    }
    if (fc_stat(host, &is_dir, NULL, NULL) && is_dir) {
        return nj_throw(vm, "java/io/IOException", "cannot read a directory");
    }
    /* FileConnection owns sandbox resolution; the stream only retains this
     * validated host path and its current position, never the whole file. */
    mark = nj_root_mark(vm);
    ps = nj_root(vm, nj_string_new(vm, host));
    if (!ps) {
        nj_root_release(vm, mark);
        return nj_throw_oom(vm);
    }
    in = nj_root(vm, nj_object_new(vm, "java/io/FileInputStream"));
    if (!in) {
        nj_root_release(vm, mark);
        return NJ_ERR;
    }
    a[0].r = ps;
    if (nj_call_special(vm, in, "java/io/FileInputStream", "<init>",
                        "(Ljava/lang/String;)V", a, NULL) != NJ_OK) {
        nj_root_release(vm, mark);
        return NJ_ERR;
    }
    ret->r = in;
    nj_root_release(vm, mark);
    return NJ_OK;
}

/*
 * openOutputStream() hands back a FileOutputStream over the resolved host
 * path.  The path is resolved HERE, by the module that owns the sandbox, so a
 * MIDlet cannot step outside the root by opening a stream instead of a
 * connection - the stream only ever sees an already-validated path.
 */
NJFN(fc_openOutputStream)
{
    char path[NJ_FC_PATHMAX];
    char host[NJ_FC_PATHMAX];
    nj_ref url = fget_ref(vm, NJ_THIS(args), F_URL, "Ljava/lang/String;");
    nj_ref ps, os;
    nj_value a[2];
    nj_u4 mark;

    if (fget_int(vm, NJ_THIS(args), F_CLOSED, "Z")) {
        return fc_throw_closed(vm);
    }
    if (!(fget_int(vm, NJ_THIS(args), F_MODE, "I") & 2)) {
        return nj_throw(vm, NJ_C_ILLMODE, "connection is read-only");
    }
    if (nj_string_to_utf8(url, path, (int)sizeof(path)) <= 0 ||
        fc_resolve(vm, path, host, sizeof(host)) != 0) {
        return nj_throw(vm, "java/io/IOException", "path is not accessible");
    }
    mark = nj_root_mark(vm);
    ps = nj_root(vm, nj_string_new(vm, host));
    if (!ps) {
        nj_root_release(vm, mark);
        return nj_throw_oom(vm);
    }
    os = nj_root(vm, nj_object_new(vm, "java/io/FileOutputStream"));
    if (!os) {
        nj_root_release(vm, mark);
        return NJ_ERR;
    }
    a[0].r = ps;
    if (nj_call_special(vm, os, "java/io/FileOutputStream", "<init>",
                        "(Ljava/lang/String;)V", a, NULL) != NJ_OK) {
        nj_root_release(vm, mark);
        return NJ_ERR;
    }
    ret->r = os;
    nj_root_release(vm, mark);
    return NJ_OK;
}

/* Offset writes replace bytes in place. An offset past EOF is clamped to EOF,
 * rather than extending the file or appending every write as an append stream. */
NJFN(fc_openOutputStream_offset)
{
    nj_s8 offset = NJ_ARG_LONG(args, 0);
    nj_ref url = fget_ref(vm, NJ_THIS(args), F_URL, "Ljava/lang/String;");
    char path[NJ_FC_PATHMAX], host[NJ_FC_PATHMAX];
    nj_u4 size;
    int is_dir = 0;
    nj_field *position;

    if (offset < 0) return nj_throw(vm, "java/lang/IllegalArgumentException", "negative offset");
    if (fget_int(vm, NJ_THIS(args), F_CLOSED, "Z")) return fc_throw_closed(vm);
    if (nj_string_to_utf8(url, path, (int)sizeof(path)) <= 0 ||
        fc_resolve(vm, path, host, sizeof(host)) != 0 ||
        !fc_stat(host, &is_dir, &size, NULL) || is_dir) {
        return nj_throw(vm, "java/io/IOException", "cannot open the file");
    }
    if (offset > size) offset = size;
    if (offset > 0x7fffffff) {
        return nj_throw(vm, "java/io/IOException", "file offset exceeds stream capacity");
    }
    if (fc_openOutputStream(vm, th, args, ret) != NJ_OK) return NJ_ERR;
    position = nj_field_lookup(vm, "java/io/ByteArrayOutputStream", "count", "I", 0);
    nj_field_write_int(vm, ret->r, position, (nj_s4)offset);
    return NJ_OK;
}

/*
 * The two data-stream forms differ from the plain ones only by a wrapper, so
 * each builds the plain form and wraps the result.  Wrapping rather than
 * reimplementing is what keeps a single answer to "where does this file live".
 */
NJFN(fc_openDataOutputStream)
{
    nj_ref inner, out;
    nj_value a[2];

    if (fc_openOutputStream(vm, th, args, ret) != NJ_OK) {
        return NJ_ERR;
    }
    inner = ret->r;
    out = nj_object_new(vm, "java/io/DataOutputStream");
    if (!out) {
        return NJ_ERR;
    }
    a[0].r = inner;
    if (nj_call_special(vm, out, "java/io/DataOutputStream", "<init>",
                        "(Ljava/io/OutputStream;)V", a, NULL) != NJ_OK) {
        return NJ_ERR;
    }
    ret->r = out;
    return NJ_OK;
}

NJFN(fc_openDataInputStream)
{
    nj_ref inner, in;
    nj_value a[2];
    nj_u4 mark;

    if (fc_openInputStream(vm, th, args, ret) != NJ_OK) {
        return NJ_ERR;
    }
    mark = nj_root_mark(vm);
    inner = nj_root(vm, ret->r);
    in = nj_root(vm, nj_object_new(vm, "java/io/DataInputStream"));
    if (!in) {
        nj_root_release(vm, mark);
        return NJ_ERR;
    }
    a[0].r = inner;
    if (nj_call_special(vm, in, "java/io/DataInputStream", "<init>",
                        "(Ljava/io/InputStream;)V", a, NULL) != NJ_OK) {
        nj_root_release(vm, mark);
        return NJ_ERR;
    }
    ret->r = in;
    nj_root_release(vm, mark);
    return NJ_OK;
}

NJFN(fc_truncate)
{
    nj_s8 length = NJ_ARG_LONG(args, 0);
    char path[NJ_FC_PATHMAX];
    char host[NJ_FC_PATHMAX];
    nj_ref self = NJ_THIS(args);
    nj_ref url = fget_ref(vm, self, F_URL, "Ljava/lang/String;");

    if (length < 0) {
        return nj_throw(vm, "java/lang/IllegalArgumentException",
                        "negative file length");
    }
    if (fget_int(vm, self, F_CLOSED, "Z")) {
        return fc_throw_closed(vm);
    }
    if (!(fget_int(vm, self, F_MODE, "I") & 2)) {
        return nj_throw(vm, NJ_C_ILLMODE, "connection is read-only");
    }
    if (nj_string_to_utf8(url, path, (int)sizeof(path)) <= 0 ||
        fc_resolve(vm, path, host, sizeof(host)) != 0) {
        return nj_throw(vm, "java/io/IOException", "path is not accessible");
    }
#if NJ_USE_LIBC && defined(_WIN32)
    {
        /* Same rule as the POSIX path: no create, no extend, directories
         * cannot be truncated.  "r+b" fails on both a missing file and a
         * directory; _chsize_s is the CRT spelling of ftruncate. */
        FILE *fp = nj_win32_fopen(host, "r+b");
        if (!fp) {
            return nj_throw(vm, "java/io/IOException", "cannot open for truncation");
        }
        if (_chsize_s(_fileno(fp), (long long)length) != 0) {
            fclose(fp);
            return nj_throw(vm, "java/io/IOException", "cannot truncate the file");
        }
        if (fclose(fp) != 0) {
            return nj_throw(vm, "java/io/IOException", "cannot truncate the file");
        }
        return NJ_OK;
    }
#elif NJ_USE_LIBC
    {
        struct stat st;
        int fd = open(host, O_WRONLY);
        int failed;
        if (fd < 0) {
            return nj_throw(vm, "java/io/IOException", "cannot open for truncation");
        }
        /* Open without O_CREAT or O_TRUNC: a missing file must stay missing,
         * and JSR 75 never extends a file, even for a Java long > 32 bits.
         * Stat and truncate the same descriptor so the size is not narrowed
         * through fc_stat's 32-bit result or looked up on a different file. */
        failed = fstat(fd, &st) != 0;
        if (!failed) {
            failed = S_ISDIR(st.st_mode) ||
                (length < (nj_s8)st.st_size &&
                 ftruncate(fd, (off_t)length) != 0);
        }
        if (close(fd) != 0) {
            failed = 1;
        }
        if (failed) {
            return nj_throw(vm, "java/io/IOException", "cannot truncate the file");
        }
        return NJ_OK;
    }
#else
    return nj_throw(vm, "java/io/IOException", "needs a hosted C library");
#endif
}

/* Operations that are deliberately not implemented, each saying so. */
NJFN(fc_not_impl)
{
    return nj_throw(vm, "java/io/IOException",
                    "not implemented by this runtime");
}

/* ------------------------------------------------------------------ */
/* The "does it match" filter used by list()                           */
/* ------------------------------------------------------------------ */
/*
 * JSR 75 filters are "*.nes" style: '*' matches any run, '?' one character.
 * Implemented here rather than pulled from the C library because a freestanding
 * build has no fnmatch().
 */
static int fc_match(const char *pat, const char *name)
{
    if (!pat || !*pat) {
        return 1;
    }
    if (*pat == '*') {
        /* Try every split, so "*.nes" matches "a.b.nes". */
        while (pat[1] == '*') {
            pat++;
        }
        if (!pat[1]) {
            return 1;
        }
        for (; *name; name++) {
            if (fc_match(pat + 1, name)) {
                return 1;
            }
        }
        return fc_match(pat + 1, name);
    }
    if (*pat == '?') {
        return (*name != '\0') && fc_match(pat + 1, name + 1);
    }
    if (*pat != *name) {
        return 0;
    }
    if (!*pat) {
        return 1;
    }
    return fc_match(pat + 1, name + 1);
}

/* ------------------------------------------------------------------ */
/* list()                                                              */
/* ------------------------------------------------------------------ */
static nj_status fc_list_impl(nj_vm *vm, nj_value *args, nj_value *ret,
                              nj_ref filter);

NJFN(fc_list)
{
    /* list(): every entry.  A function of its own rather than a guess at how
     * many arguments arrived - reading args[0] on the no-argument path reads
     * whatever is below the call, and the array filter that "appeared" that way
     * made the empty list look correct. */
    return fc_list_impl(vm, args, ret, NULL);
}

NJFN(fc_list_filtered)
{
    return fc_list_impl(vm, args, ret, NJ_ARG_REF(args, 0));
}

static nj_status fc_list_impl(nj_vm *vm, nj_value *args, nj_value *ret,
                              nj_ref filter)
{
    char path[NJ_FC_PATHMAX];
    char host[NJ_FC_PATHMAX];
    nj_ref url = fget_ref(vm, NJ_THIS(args), F_URL, "Ljava/lang/String;");
    const char *pat = NULL;
    char patbuf[128];
    nj_ref vec = NULL;
    int is_dir = 0;

    if (fget_int(vm, NJ_THIS(args), F_CLOSED, "Z")) {
        return fc_throw_closed(vm);
    }
    if (filter && nj_string_to_utf8(filter, patbuf, (int)sizeof(patbuf)) > 0) {
        pat = patbuf;
    }
    if (nj_string_to_utf8(url, path, (int)sizeof(path)) <= 0 ||
        fc_resolve(vm, path, host, sizeof(host)) != 0 ||
        !fc_stat(host, &is_dir, NULL, NULL)) {
        return nj_throw(vm, "java/io/IOException", "cannot list");
    }
    if (!is_dir) {
        return nj_throw(vm, "java/io/IOException", "not a directory");
    }

    vec = nj_object_new(vm, "java/util/Vector");
    if (!vec) {
        return NJ_ERR;
    }

    if (vm->port && vm->port->dir_open) {
        void *h = NULL;
        char name[256];
        int isd = 0;
        if (vm->port->dir_open(vm->port->ud, host, &h) != 0) {
            return nj_throw(vm, "java/io/IOException",
                            "the port cannot read this directory");
        }
        while (vm->port->dir_next(vm->port->ud, h, name, sizeof(name), &isd) == 1) {
            char entry[300];
            nj_ref s;
            if (!fc_match(pat, name)) {
                continue;
            }
            /* Directories are reported WITH a trailing slash - that is how a
             * MIDlet tells them apart, since the filter matches both. */
            if (isd) {
                nj_size k = 0;
                while (name[k] && k + 2u < sizeof(entry)) {
                    entry[k] = name[k];
                    k++;
                }
                entry[k++] = '/';
                entry[k] = '\0';
            } else {
                nj_size k = 0;
                while (name[k] && k + 1u < sizeof(entry)) {
                    entry[k] = name[k];
                    k++;
                }
                entry[k] = '\0';
            }
            s = nj_string_new(vm, entry);
            if (s) {
                nj_value a1[2];
                a1[0].r = s;
                (void)nj_call(vm, vec, "java/util/Vector", "addElement",
                              "(Ljava/lang/Object;)V", a1, NULL);
            }
        }
        vm->port->dir_close(vm->port->ud, h);
    } else {
        return nj_throw(vm, "java/io/IOException",
                        "this port cannot list directories");
    }

    {
        nj_value r;
        r.r = NULL;
        if (nj_call(vm, vec, "java/util/Vector", "elements",
                    "()Ljava/util/Enumeration;", NULL, &r) != NJ_OK) {
            return NJ_ERR;
        }
        ret->r = r.r;
    }
    return NJ_OK;
}

NJFN(fc_create)
{
    char path[NJ_FC_PATHMAX];
    nj_ref url = fget_ref(vm, NJ_THIS(args), F_URL, "Ljava/lang/String;");
    char host[NJ_FC_PATHMAX];

    if (nj_string_to_utf8(url, path, (int)sizeof(path)) <= 0 ||
        fc_resolve(vm, path, host, sizeof(host)) != 0) {
        return nj_throw(vm, "java/io/IOException", "path is not accessible");
    }
    if (fc_stat(host, NULL, NULL, NULL)) {
        return nj_throw(vm, "java/io/IOException", "already exists");
    }
#if NJ_USE_LIBC && defined(_WIN32)
    {
        FILE *fp = nj_win32_fopen(host, "wb");
        if (!fp) {
            return nj_throw(vm, "java/io/IOException", "cannot create");
        }
        fclose(fp);
        return NJ_OK;
    }
#elif NJ_USE_LIBC
    {
        FILE *fp = fopen(host, "wb");
        if (!fp) {
            return nj_throw(vm, "java/io/IOException", "cannot create");
        }
        fclose(fp);
        return NJ_OK;
    }
#else
    return nj_throw(vm, "java/io/IOException", "needs a hosted C library");
#endif
}

NJFN(fc_mkdir)
{
    char path[NJ_FC_PATHMAX];
    nj_ref url = fget_ref(vm, NJ_THIS(args), F_URL, "Ljava/lang/String;");
    char host[NJ_FC_PATHMAX];

    if (nj_string_to_utf8(url, path, (int)sizeof(path)) <= 0 ||
        fc_resolve(vm, path, host, sizeof(host)) != 0) {
        return nj_throw(vm, "java/io/IOException", "path is not accessible");
    }
#if NJ_USE_LIBC && defined(_WIN32)
    if (nj_win32_mkdir(host) != 0) {
        return nj_throw(vm, "java/io/IOException", "cannot create directory");
    }
    return NJ_OK;
#elif NJ_USE_LIBC
    if (mkdir(host, 0777) != 0) {
        return nj_throw(vm, "java/io/IOException", "cannot create directory");
    }
    return NJ_OK;
#else
    return nj_throw(vm, "java/io/IOException", "needs a hosted C library");
#endif
}

NJFN(fc_rename)
{
    nj_ref self = NJ_THIS(args), name = NJ_ARG_REF(args, 0), updated;
    nj_ref url = fget_ref(vm, self, F_URL, "Ljava/lang/String;");
    char old_url[NJ_FC_PATHMAX], new_url[NJ_FC_PATHMAX], leaf[NJ_FC_PATHMAX];
    char old_host[NJ_FC_PATHMAX], new_host[NJ_FC_PATHMAX];
    nj_size end, prefix, n, i;
    int is_dir = 0;

    if (!name) return nj_throw(vm, "java/lang/NullPointerException", NULL);
    if (fget_int(vm, self, F_CLOSED, "Z")) return fc_throw_closed(vm);
    if (!(fget_int(vm, self, F_MODE, "I") & 2)) {
        return nj_throw(vm, NJ_C_ILLMODE, "connection is read-only");
    }
    if (nj_string_to_utf8(name, leaf, (int)sizeof(leaf)) <= 0 ||
        strcmp(leaf, ".") == 0 || strcmp(leaf, "..") == 0) {
        return nj_throw(vm, "java/lang/IllegalArgumentException", "invalid file name");
    }
    n = strlen(leaf);
    for (i = 0u; i < n; ++i) {
        if (leaf[i] == '/' || leaf[i] == '\\' || leaf[i] == ':') {
            return nj_throw(vm, "java/lang/IllegalArgumentException", "name contains a path");
        }
    }
    if (nj_string_to_utf8(url, old_url, (int)sizeof(old_url)) <= 0 ||
        fc_resolve(vm, old_url, old_host, sizeof(old_host)) != 0 ||
        !fc_stat(old_host, &is_dir, NULL, NULL)) {
        return nj_throw(vm, "java/io/IOException", "cannot rename the file");
    }
    end = strlen(old_url);
    while (end > 0u && old_url[end - 1u] == '/') --end;
    if (end <= 12u) return nj_throw(vm, "java/io/IOException", "cannot rename a root");
    prefix = end;
    while (prefix > 0u && old_url[prefix - 1u] != '/') --prefix;
    if (prefix + n + (is_dir ? 1u : 0u) >= sizeof(new_url)) {
        return nj_throw(vm, "java/io/IOException", "file name is too long");
    }
    memcpy(new_url, old_url, prefix);
    memcpy(new_url + prefix, leaf, n);
    if (is_dir) new_url[prefix + n++] = '/';
    new_url[prefix + n] = '\0';
    if (fc_resolve(vm, new_url, new_host, sizeof(new_host)) != 0 ||
        fc_stat(new_host, NULL, NULL, NULL)) {
        return nj_throw(vm, "java/io/IOException", "rename target is not available");
    }
    updated = nj_string_new(vm, new_url);
    if (!updated) return nj_throw_oom(vm);
#if NJ_USE_LIBC && defined(_WIN32)
    if (nj_win32_rename(old_host, new_host) != 0) {
        return nj_throw(vm, "java/io/IOException", "cannot rename the file");
    }
    fset_ref(vm, self, F_URL, "Ljava/lang/String;", updated);
    return NJ_OK;
#elif NJ_USE_LIBC
    if (rename(old_host, new_host) != 0) {
        return nj_throw(vm, "java/io/IOException", "cannot rename the file");
    }
    fset_ref(vm, self, F_URL, "Ljava/lang/String;", updated);
    return NJ_OK;
#else
    return nj_throw(vm, "java/io/IOException", "needs a hosted C library");
#endif
}

NJFN(fc_delete)
{
    char path[NJ_FC_PATHMAX];
    nj_ref url = fget_ref(vm, NJ_THIS(args), F_URL, "Ljava/lang/String;");
    char host[NJ_FC_PATHMAX];

    if (nj_string_to_utf8(url, path, (int)sizeof(path)) <= 0 ||
        fc_resolve(vm, path, host, sizeof(host)) != 0) {
        return nj_throw(vm, "java/io/IOException", "path is not accessible");
    }
#if NJ_USE_LIBC && defined(_WIN32)
    if (nj_win32_remove(host) != 0) {
        return nj_throw(vm, "java/io/IOException", "cannot delete");
    }
    return NJ_OK;
#elif NJ_USE_LIBC
    if (remove(host) != 0) {
        return nj_throw(vm, "java/io/IOException", "cannot delete");
    }
    return NJ_OK;
#else
    return nj_throw(vm, "java/io/IOException", "needs a hosted C library");
#endif
}

/* ------------------------------------------------------------------ */
/* FileSystemRegistry                                                  */
/* ------------------------------------------------------------------ */
NJFN(fsr_add)
{
    /*
     * Accepted and remembered nowhere.  Reporting filesystem changes would mean
     * watching the host's directories, and nothing in this runtime does; a
     * listener that is never called is honest, whereas one that fires spuriously
     * makes a file picker reload at random.
     */
    ret->i = 1;
    return NJ_OK;
}

NJFN(fsr_remove) { ret->i = 1; return NJ_OK; }

NJFN(fsr_listRoots)
{
    nj_ref vec = nj_object_new(vm, "java/util/Vector");
    nj_ref s;
    nj_value a1[2];

    if (!vec) {
        return NJ_ERR;
    }
    /* Without a filesystem, return an empty enumeration. Callers may always
     * iterate the result, even when there are no roots to open. */
    s = fc_root(vm) ? nj_string_new(vm, NJ_FC_ROOT_NAME) : NULL;
    if (s) {
        a1[0].r = s;
        (void)nj_call(vm, vec, "java/util/Vector", "addElement",
                      "(Ljava/lang/Object;)V", a1, NULL);
    }
    {
        nj_value r;
        r.r = NULL;
        if (nj_call(vm, vec, "java/util/Vector", "elements",
                    "()Ljava/util/Enumeration;", NULL, &r) != NJ_OK) {
            return NJ_ERR;
        }
        ret->r = r.r;
    }
    return NJ_OK;
}

/* ------------------------------------------------------------------ */
/* Construction, used by Connector.open                                */
/* ------------------------------------------------------------------ */
/*
 * Build a FileConnection for a file:// URL, or NULL with an exception pending.
 * Exposed to nj_lib_io.c, which owns Connector.
 */
nj_ref nj_fc_new(nj_vm *vm, const char *url);

nj_ref nj_fc_new(nj_vm *vm, const char *url)
{
    char host[NJ_FC_PATHMAX];
    nj_ref o;
    int is_dir = 0;

    if (!url || fc_resolve(vm, url, host, sizeof(host)) != 0) {
        (void)nj_throw(vm, "javax/microedition/io/ConnectionNotFoundException",
                       url ? url : "(null)");
        return NULL;
    }
    o = nj_object_new(vm, NJ_C_FC);
    if (!o) {
        return NULL;
    }
    fset_ref(vm, o, F_URL, "Ljava/lang/String;", nj_string_new(vm, url));
    fset_int(vm, o, F_CLOSED, "Z", 0);
    fset_int(vm, o, F_MODE, "I", 3);
    (void)fc_stat(host, &is_dir, NULL, NULL);
    fset_int(vm, o, F_ISDIR, "Z", is_dir);
    return o;
}

/* ------------------------------------------------------------------ */
/* Tables                                                              */
/* ------------------------------------------------------------------ */
#define M(name, desc, flags) { name, desc, flags }
#define FLD(name, desc, flags) { name, desc, flags }

static const nj_builtin_field fc_fields[] = {
    FLD(F_URL,    "Ljava/lang/String;", 0),
    FLD(F_HOST,   "Ljava/lang/String;", 0),
    FLD(F_CLOSED, "Z", 0),
    FLD(F_MODE,   "I", 0),
    FLD(F_ISDIR,  "Z", 0)
};

static const nj_builtin_method fc_methods[] = {
    M("close",              "()V", 0),
    M("exists",             "()Z", 0),
    M("isDirectory",        "()Z", 0),
    M("canRead",            "()Z", 0),
    M("canWrite",           "()Z", 0),
    M("isHidden",           "()Z", 0),
    M("getName",            "()Ljava/lang/String;", 0),
    M("getPath",            "()Ljava/lang/String;", 0),
    M("getURL",             "()Ljava/lang/String;", 0),
    M("fileSize",           "()J", 0),
    M("lastModified",       "()J", 0),
    M("list",               "()Ljava/util/Enumeration;", 0),
    M("list",               "(Ljava/lang/String;Z)Ljava/util/Enumeration;", 0),
    M("openInputStream",    "()Ljava/io/InputStream;", 0),
    M("create",             "()V", 0),
    M("mkdir",              "()V", 0),
    M("delete",             "()V", 0),
    M("setFileConnection",  "(Ljava/lang/String;)V", 0),
    M("availableSize",      "()J", 0),
    M("openOutputStream",   "()Ljava/io/OutputStream;", 0),
    M("openDataInputStream",  "()Ljava/io/DataInputStream;", 0),
    M("openDataOutputStream", "()Ljava/io/DataOutputStream;", 0),
    M("setReadable",        "(Z)V", 0),
    M("setWritable",        "(Z)V", 0),
    M("setHidden",          "(Z)V", 0),
    M("truncate",           "(J)V", 0),
    M("rename",             "(Ljava/lang/String;)V", 0),
    M("openOutputStream",   "(J)Ljava/io/OutputStream;", 0)
};

static const nj_builtin_method fsr_methods[] = {
    M("addFileSystemListener",
      "(L" NJ_C_FSL ";)Z", NJ_ACC_STATIC),
    M("removeFileSystemListener",
      "(L" NJ_C_FSL ";)Z", NJ_ACC_STATIC),
    M("listRoots", "()Ljava/util/Enumeration;", NJ_ACC_STATIC)
};

static const nj_builtin_method fsl_methods[] = {
    M("rootChanged", "(ILjava/lang/String;)V", NJ_ACC_ABSTRACT)
};

const nj_builtin_class nj_lib_fc_classes[] = {
    { NJ_C_FC, "java/lang/Object", NJ_ACC_PUBLIC,
      fc_fields,
      (nj_u2)(sizeof(fc_fields) / sizeof(fc_fields[0])),
      fc_methods,
      (nj_u2)(sizeof(fc_methods) / sizeof(fc_methods[0])) },
    { NJ_C_FSR, "java/lang/Object", NJ_ACC_PUBLIC,
      NULL, 0u, fsr_methods,
      (nj_u2)(sizeof(fsr_methods) / sizeof(fsr_methods[0])) },
    { NJ_C_FSL, NULL, NJ_ACC_PUBLIC | NJ_ACC_INTERFACE | NJ_ACC_ABSTRACT,
      NULL, 0u, fsl_methods,
      (nj_u2)(sizeof(fsl_methods) / sizeof(fsl_methods[0])) },
    { NJ_C_FCLEX, "java/io/IOException", NJ_ACC_PUBLIC, NULL, 0u, NULL, 0u },
    { NJ_C_ILLMODE, "java/lang/RuntimeException", NJ_ACC_PUBLIC,
      NULL, 0u, NULL, 0u },
    { NULL, NULL, 0u, NULL, 0u, NULL, 0u }
};
const int nj_lib_fc_class_count =
    (int)(sizeof(nj_lib_fc_classes) / sizeof(nj_lib_fc_classes[0])) - 1;

const nj_native_entry nj_lib_fc_table[] = {
    { NJ_C_FC, "close",       "()V", fc_close },
    { NJ_C_FC, "exists",      "()Z", fc_exists },
    { NJ_C_FC, "isDirectory", "()Z", fc_isDirectory },
    { NJ_C_FC, "canRead",     "()Z", fc_canRead },
    { NJ_C_FC, "canWrite",    "()Z", fc_canWrite },
    { NJ_C_FC, "isHidden",    "()Z", fc_isHidden },
    { NJ_C_FC, "getName",     "()Ljava/lang/String;", fc_getName },
    { NJ_C_FC, "getPath",     "()Ljava/lang/String;", fc_getPath },
    { NJ_C_FC, "getURL",      "()Ljava/lang/String;", fc_getURL },
    { NJ_C_FC, "fileSize",    "()J", fc_fileSize },
    { NJ_C_FC, "lastModified", "()J", fc_lastModified },
    { NJ_C_FC, "list", "()Ljava/util/Enumeration;", fc_list },
    { NJ_C_FC, "list", "(Ljava/lang/String;Z)Ljava/util/Enumeration;",
      fc_list_filtered },
    { NJ_C_FC, "openInputStream", "()Ljava/io/InputStream;", fc_openInputStream },
    { NJ_C_FC, "create",  "()V", fc_create },
    { NJ_C_FC, "mkdir",   "()V", fc_mkdir },
    { NJ_C_FC, "delete",  "()V", fc_delete },
    { NJ_C_FC, "setFileConnection", "(Ljava/lang/String;)V", fc_setFileConnection },
    { NJ_C_FC, "availableSize", "()J", fc_fileSize },
    { NJ_C_FC, "openOutputStream",   "()Ljava/io/OutputStream;", fc_openOutputStream },
    { NJ_C_FC, "openDataInputStream",  "()Ljava/io/DataInputStream;", fc_openDataInputStream },
    { NJ_C_FC, "openDataOutputStream", "()Ljava/io/DataOutputStream;", fc_openDataOutputStream },
    { NJ_C_FC, "setReadable", "(Z)V", fc_not_impl },
    { NJ_C_FC, "setWritable", "(Z)V", fc_not_impl },
    { NJ_C_FC, "setHidden",   "(Z)V", fc_not_impl },
    { NJ_C_FC, "truncate",    "(J)V", fc_truncate },
    { NJ_C_FC, "rename", "(Ljava/lang/String;)V", fc_rename },
    { NJ_C_FC, "openOutputStream", "(J)Ljava/io/OutputStream;", fc_openOutputStream_offset },
    { NJ_C_FSR, "addFileSystemListener",
      "(L" NJ_C_FSL ";)Z", fsr_add },
    { NJ_C_FSR, "removeFileSystemListener",
      "(L" NJ_C_FSL ";)Z", fsr_remove },
    { NJ_C_FSR, "listRoots", "()Ljava/util/Enumeration;", fsr_listRoots },
    { NULL, NULL, NULL, NULL }
};
const int nj_lib_fc_count =
    (int)(sizeof(nj_lib_fc_table) / sizeof(nj_lib_fc_table[0])) - 1;
