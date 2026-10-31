/*
 * nano_j2me - src/client/nj_library.c
 *
 * The on-disk games library of the desktop client: probe a .jar for the
 * MIDlet metadata a launcher wants, keep a list of games, and scan a
 * directory progressively so the window can paint while the disk is busy.
 *
 * A jar is only mapped for the moment a manifest or an icon is read: probing
 * must not slurp a 40 MiB game into memory just to show its name, and icons
 * are decoded on demand the first time the caller asks for one.  Everything
 * here is hosted-only (POSIX/C99); the MIDP runtime itself never calls it.
 */
#include "nj2me/nj_library.h"
#include "nj2me/nj_png.h"
#include "nj2me/nj_zip.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Map the jar when the host has mmap(), fall back to fread() otherwise. */
#if (defined(__unix__) || defined(__APPLE__)) && !defined(_WIN32)
#  define NJ_LIB_MMAP 1
#  include <fcntl.h>
#  include <sys/mman.h>
#  include <sys/stat.h>
#  include <unistd.h>
#else
#  define NJ_LIB_MMAP 0
#endif

/* Directory enumeration, same libc guard as src/midp/nj_lib_rms.c.  Windows
 * counts as a libc-hosting desktop here too, but enumerates with
 * FindFirstFileW: mingw's dirent.h is ANSI-only and a jar named 1942空战_10403
 * would show up as question marks. */
#if NJ_USE_LIBC
#  if defined(_WIN32)
#    include "nj_posix_win32.h"
#    define NJ_LIB_DIRENT 1
#  else
#    include <dirent.h>
#    include <sys/stat.h>
#    include <unistd.h>
#    define NJ_LIB_DIRENT 1
#  endif
#else
#  define NJ_LIB_DIRENT 0
#endif

#define NJ_LIB_MANIFEST_MAX  (64u * 1024u)       /* refuse larger manifests */
#define NJ_LIB_MANIFEST_VAL  512u                /* one attribute value */
#define NJ_LIB_ICON_MAX      (4u * 1024u * 1024u) /* refuse larger icons */
#define NJ_LIB_INIT_CAP      16
#define NJ_LIB_CLEAN_TMP     1024

/* ------------------------------------------------------------------ */
/* small string helpers                                               */
/* ------------------------------------------------------------------ */

static void nj_lib_trim(char *s);
static void nj_lib_copy_utf8(char *dst, size_t cap, const char *src);
static void nj_lib_clean_name(const char *in, char *out, size_t cap);
static const char *nj_lib_basename(const char *path);
static int  nj_lib_lower(int c);

/*
 * Copy a UTF-8 string, truncating to cap-1 bytes on a character boundary.
 * The walk-back looks at the first dropped source byte: when it is a
 * continuation byte the last copied byte is a lead byte of a split
 * character, so it is dropped too.
 */
static void nj_lib_copy_utf8(char *dst, size_t cap, const char *src)
{
    size_t i = 0;

    if (dst == NULL || cap == 0) {
        return;
    }
    dst[0] = '\0';
    if (src == NULL) {
        return;
    }
    while (src[i] != '\0' && i + 1u < cap) {
        dst[i] = src[i];
        i++;
    }
    while (i > 0 && ((unsigned char)src[i] & 0xC0u) == 0x80u) {
        i--;
    }
    dst[i] = '\0';
}

static int nj_lib_lower(int c)
{
    return (c >= 'A' && c <= 'Z') ? (c - 'A' + 'a') : c;
}

static const char *nj_lib_basename(const char *path)
{
    const char *base;

    if (path == NULL) {
        return "";
    }
    base = path;
    while (*path != '\0') {
        if (*path == '/'
#if defined(_WIN32)
            || *path == '\\'
#endif
           ) {
            base = path + 1;
        }
        path++;
    }
    return base;
}

static void nj_lib_trim(char *s)
{
    size_t i;
    size_t n;

    if (s == NULL) {
        return;
    }
    n = strlen(s);
    while (n > 0) {
        char c = s[n - 1u];
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n' ||
            c == '\v' || c == '\f') {
            s[--n] = '\0';
        } else {
            break;
        }
    }
    i = 0;
    while (s[i] == ' ' || s[i] == '\t' || s[i] == '\r' || s[i] == '\n' ||
           s[i] == '\v' || s[i] == '\f') {
        i++;
    }
    if (i > 0) {
        memmove(s, s + i, n - i + 1u);
    }
}

/*
 * Turn a raw manifest/filename string into something a window can draw:
 * control bytes become spaces, space runs collapse, the ends are trimmed and
 * the result is capped without cutting a UTF-8 sequence.  The intermediate
 * buffer is bounded, which is fine because every input here is short (file
 * name, manifest display name).
 */
static void nj_lib_clean_name(const char *in, char *out, size_t cap)
{
    char tmp[NJ_LIB_CLEAN_TMP];
    size_t i = 0;
    size_t n = 0;

    if (out == NULL || cap == 0) {
        return;
    }
    out[0] = '\0';
    if (in == NULL) {
        return;
    }
    while (in[i] != '\0') {
        unsigned char c = (unsigned char)in[i];
        if (c < 0x20u) {
            c = ' ';
        }
        if (c == ' ') {
            if (n == 0 || tmp[n - 1u] == ' ') {
                i++;
                continue;
            }
        }
        if (n + 1u >= sizeof tmp) {
            break;
        }
        tmp[n++] = (char)c;
        i++;
    }
    while (n > 0 && tmp[n - 1u] == ' ') {
        n--;
    }
    tmp[n] = '\0';
    nj_lib_copy_utf8(out, cap, tmp);
    n = strlen(out);
    while (n > 0 && out[n - 1u] == ' ') {
        n--;
    }
    out[n] = '\0';
}

/* Join dir and name with one '/' when needed; 0 when it does not fit. */
static int nj_lib_join(char *out, size_t cap, const char *dir, const char *name)
{
    size_t d;
    size_t n;

    if (out == NULL || cap == 0) {
        return 0;
    }
    out[0] = '\0';
    if (dir == NULL || name == NULL) {
        return 0;
    }
    d = strlen(dir);
    n = strlen(name);
    if (d + 1u + n + 1u > cap) {
        return 0;
    }
    memcpy(out, dir, d);
    if (d > 0 && dir[d - 1u] != '/') {
        out[d++] = '/';
    }
    memcpy(out + d, name, n + 1u);
    return 1;
}

/* Make path absolute by prefixing the cwd for relative input. */
static int nj_lib_abs_path(const char *in, char *out, size_t cap)
{
    size_t n;

    if (in == NULL || out == NULL || cap == 0) {
        return 0;
    }
    if (in[0] == '/') {
        n = strlen(in);
        if (n >= cap) {
            return 0;
        }
        memcpy(out, in, n + 1u);
        return 1;
    }
#if defined(_WIN32)
    /* A Windows path is also absolute with a drive ("C:/x") or a backslash.
     * Everything else is relative and gets the current directory, which the
     * wide API can return through an ANSI-hostile working directory too. */
    if (in[0] == '\\' ||
        (((in[0] >= 'A' && in[0] <= 'Z') || (in[0] >= 'a' && in[0] <= 'z')) &&
         in[1] == ':')) {
        n = strlen(in);
        if (n >= cap) {
            return 0;
        }
        memcpy(out, in, n + 1u);
        return 1;
    }
    {
        char cwd[NJ_LIB_PATH_MAX];

        if (!nj_win32_getcwd(cwd, (int)sizeof cwd)) {
            return 0;
        }
        n = strlen(cwd);
        if (n + 1u + strlen(in) + 1u > cap) {
            return 0;
        }
        memcpy(out, cwd, n);
        out[n++] = '/';
        memcpy(out + n, in, strlen(in) + 1u);
        return 1;
    }
#elif NJ_LIB_DIRENT
    {
        char cwd[NJ_LIB_PATH_MAX];

        if (getcwd(cwd, sizeof cwd) == NULL) {
            return 0;
        }
        n = strlen(cwd);
        if (n + 1u + strlen(in) + 1u > cap) {
            return 0;
        }
        memcpy(out, cwd, n);
        out[n++] = '/';
        memcpy(out + n, in, strlen(in) + 1u);
        return 1;
    }
#else
    n = strlen(in);
    if (n >= cap) {
        return 0;
    }
    memcpy(out, in, n + 1u);
    return 1;
#endif
}

#if NJ_LIB_DIRENT
/* st_size -> the u32 the public structs carry. */
static nj_u4 nj_lib_size_u4(nj_u8 value)
{
    return (value > (nj_u8)0xFFFFFFFFu) ? (nj_u4)0xFFFFFFFFu : (nj_u4)value;
}
#endif

/* ------------------------------------------------------------------ */
/* opening a jar: one helper shared by probe and icon extraction      */
/* ------------------------------------------------------------------ */

typedef struct nj_lib_jar {
    const nj_u1 *data;
    nj_u4        size;
    nj_zip      *zip;
    void        *map;
    nj_u1       *heap;
} nj_lib_jar;

#define NJ_LIB_JAR_OPENED 1
#define NJ_LIB_JAR_NOT_ZIP 0
#define NJ_LIB_JAR_NO_FILE (-1)

#if defined(_WIN32)
/*
 * The wide API: fopen() goes through the ANSI code page and cannot open a jar
 * whose name is Chinese.  The UTF-8 -> UTF-16 conversion lives in
 * nj_posix_win32.h (included from the top under _WIN32), shared with the
 * ports, so this is a one-line call.
 */
static FILE *nj_lib_fopen_read(const char *path)
{
    return (path != NULL) ? nj_win32_fopen(path, "rb") : NULL;
}
#else
static FILE *nj_lib_fopen_read(const char *path)
{
    return (path != NULL) ? fopen(path, "rb") : NULL;
}
#endif

static void nj_lib_jar_close(nj_lib_jar *jar)
{
    if (jar == NULL) {
        return;
    }
    if (jar->zip != NULL) {
        nj_zip_close(jar->zip);
        jar->zip = NULL;
    }
#if NJ_LIB_MMAP
    if (jar->map != NULL) {
        munmap(jar->map, (size_t)jar->size);
        jar->map = NULL;
    }
#endif
    if (jar->heap != NULL) {
        free(jar->heap);
        jar->heap = NULL;
    }
    jar->data = NULL;
    jar->size = 0;
}

/* Get the file bytes: mmap on POSIX, malloc+fread everywhere else. */
static int nj_lib_jar_bytes(const char *path, nj_lib_jar *jar)
{
    FILE *f;
    long end;
    size_t got;

    memset(jar, 0, sizeof *jar);
    if (path == NULL || path[0] == '\0') {
        return 0;
    }
#if NJ_LIB_MMAP
    {
        struct stat st;
        int fd = open(path, O_RDONLY);

        if (fd >= 0) {
            if (fstat(fd, &st) == 0 && S_ISREG(st.st_mode) && st.st_size >= 0 &&
                (nj_u8)st.st_size <= (nj_u8)0xFFFFFFFFu) {
                jar->size = (nj_u4)st.st_size;
                if (jar->size == 0) {
                    close(fd);
                    return 1;
                }
                jar->map = mmap(NULL, (size_t)jar->size, PROT_READ, MAP_PRIVATE, fd, 0);
                if (jar->map != MAP_FAILED) {
                    close(fd);
                    jar->data = (const nj_u1 *)jar->map;
                    return 1;
                }
                jar->map = NULL;
            }
            close(fd);
        }
        /* Fall through: the path may be a pipe or another non-regular file. */
    }
#endif
    f = nj_lib_fopen_read(path);
    if (f == NULL) {
        return 0;
    }
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return 0;
    }
    end = ftell(f);
    if (end < 0 || (nj_u8)end > (nj_u8)0xFFFFFFFFu) {
        fclose(f);
        return 0;
    }
    if (fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        return 0;
    }
    jar->size = (nj_u4)end;
    if (jar->size == 0) {
        fclose(f);
        return 1;
    }
    jar->heap = (nj_u1 *)malloc((size_t)jar->size);
    if (jar->heap == NULL) {
        fclose(f);
        return 0;
    }
    got = fread(jar->heap, 1, (size_t)jar->size, f);
    fclose(f);
    if (got != (size_t)jar->size) {
        free(jar->heap);
        jar->heap = NULL;
        jar->size = 0;
        return 0;
    }
    jar->data = jar->heap;
    return 1;
}

/* Read the bytes and parse the central directory.  Always leaves *jar safe
 * for nj_lib_jar_close(). */
static int nj_lib_jar_open(const char *path, nj_lib_jar *jar)
{
    if (!nj_lib_jar_bytes(path, jar)) {
        return NJ_LIB_JAR_NO_FILE;
    }
    if (jar->size < 22u) {                    /* smallest possible ZIP tail */
        return NJ_LIB_JAR_NOT_ZIP;
    }
    jar->zip = nj_zip_open(jar->data, jar->size, NULL);
    return (jar->zip != NULL) ? NJ_LIB_JAR_OPENED : NJ_LIB_JAR_NOT_ZIP;
}

/* ------------------------------------------------------------------ */
/* manifest parsing                                                   */
/* ------------------------------------------------------------------ */

/*
 * Extract MANIFEST.MF into a NUL-terminated buffer.  Returns NULL when the
 * manifest is bigger than the caller is willing to tolerate or extraction
 * fails; an empty (but present) manifest is a valid empty string.
 */
static char *nj_lib_manifest_text(const nj_lib_jar *jar, const nj_zip_entry *entry,
                                  nj_u4 *len_out)
{
    nj_u1 *buf;
    nj_s4 got;

    *len_out = 0;
    if (jar == NULL || entry == NULL) {
        return NULL;
    }
    if (entry->uncomp_size > NJ_LIB_MANIFEST_MAX) {
        return NULL;
    }
    buf = (nj_u1 *)malloc((size_t)entry->uncomp_size + 1u);
    if (buf == NULL) {
        return NULL;
    }
    if (entry->uncomp_size == 0u) {
        buf[0] = 0;
        return (char *)buf;
    }
    got = nj_zip_extract(jar->zip, entry, buf, entry->uncomp_size);
    if (got < 0) {
        free(buf);
        return NULL;
    }
    buf[got] = 0;
    *len_out = (nj_u4)got;
    return (char *)buf;
}

/*
 * MIDlet-1 is "Display Name , /icon.png , com.foo.Bar" (src/core/nj_app.c).
 * Any field may be empty and a trailing comma may be missing.
 */
static void nj_lib_parse_midlet1(const char *value, char *name, size_t name_cap,
                                 char *icon, size_t icon_cap,
                                 char *cls, size_t cls_cap)
{
    char buf[NJ_LIB_MANIFEST_VAL];
    char *first;
    char *second = NULL;

    if (name != NULL && name_cap > 0) {
        name[0] = '\0';
    }
    if (icon != NULL && icon_cap > 0) {
        icon[0] = '\0';
    }
    if (cls != NULL && cls_cap > 0) {
        cls[0] = '\0';
    }
    if (value == NULL) {
        return;
    }
    nj_lib_copy_utf8(buf, sizeof buf, value);
    nj_lib_trim(buf);
    first = strchr(buf, ',');
    if (first != NULL) {
        *first++ = '\0';
        second = strchr(first, ',');
        if (second != NULL) {
            *second++ = '\0';
        }
    }
    nj_lib_trim(buf);
    nj_lib_clean_name(buf, name, name_cap);
    if (first != NULL) {
        nj_lib_trim(first);
        nj_lib_copy_utf8(icon, icon_cap, first);
    }
    if (second != NULL) {
        nj_lib_trim(second);
        nj_lib_copy_utf8(cls, cls_cap, second);
    }
}

/* Display name fallback: the file name without its .jar suffix, cleaned. */
static void nj_lib_fallback_name(const char *path, char *out, size_t cap)
{
    char tmp[NJ_LIB_NAME_MAX * 2u];
    size_t n;

    nj_lib_copy_utf8(tmp, sizeof tmp, nj_lib_basename(path));
    n = strlen(tmp);
    if (nj_lib_is_jar_name(tmp)) {
        tmp[n - 4u] = '\0';
    }
    nj_lib_clean_name(tmp, out, cap);
    if (out[0] == '\0') {
        nj_lib_copy_utf8(out, cap, "\xE6\x9C\xAA\xE5\x91\xBD\xE5\x90\x8D"); /* 未命名 */
    }
}

static void nj_lib_note(nj_lib_entry *entry, const char *note)
{
    if (entry == NULL) {
        return;
    }
    snprintf(entry->note, sizeof entry->note, "%s", (note != NULL) ? note : "");
}

/*
 * Midlet-1's icon field is a zip path that may start with '/'.  Store the
 * actual entry name (what the zip really holds) so a later icon load can
 * find it exactly; an empty/absent entry keeps the field empty.
 */
static void nj_lib_resolve_icon(const nj_zip *zip, const char *want,
                                char *out, size_t out_cap)
{
    const nj_zip_entry *entry;
    char path[NJ_LIB_FILE_MAX];
    size_t i = 0;

    out[0] = '\0';
    if (zip == NULL || want == NULL || want[0] == '\0') {
        return;
    }
    nj_lib_copy_utf8(path, sizeof path, want);
    while (path[i] == '/') {
        i++;
    }
    if (i > 0) {
        memmove(path, path + i, strlen(path + i) + 1u);
    }
    if (path[0] == '\0') {
        return;
    }
    entry = nj_zip_find(zip, path);
    if (entry == NULL) {
        entry = nj_zip_find_ci(zip, path);
    }
    if (entry == NULL || entry->uncomp_size == 0u) {
        return;
    }
    nj_lib_copy_utf8(out, out_cap, entry->name);
}

/* ------------------------------------------------------------------ */
/* public: probe                                                      */
/* ------------------------------------------------------------------ */

int nj_lib_probe(const char *path, nj_lib_entry *out)
{
    nj_lib_entry scratch;
    nj_lib_entry *entry = (out != NULL) ? out : &scratch;
    nj_lib_jar jar;
    int view;

    memset(entry, 0, sizeof *entry);
    if (path == NULL) {
        nj_lib_note(entry, "\xE6\x96\x87\xE4\xBB\xB6\xE6\x89\x93\xE4\xB8\x8D\xE5\xBC\x80"); /* 文件打不开 */
        nj_lib_fallback_name(NULL, entry->name, sizeof entry->name);
        return 0;
    }

    /* Every field is filled even when the jar turns out to be unusable. */
    if (!nj_lib_abs_path(path, entry->path, sizeof entry->path)) {
        nj_lib_copy_utf8(entry->path, sizeof entry->path, path);
    }
    nj_lib_copy_utf8(entry->file, sizeof entry->file, nj_lib_basename(path));
    nj_lib_fallback_name(path, entry->name, sizeof entry->name);

    view = nj_lib_jar_open(path, &jar);
    if (view == NJ_LIB_JAR_NO_FILE) {
        nj_lib_note(entry, "\xE6\x96\x87\xE4\xBB\xB6\xE6\x89\x93\xE4\xB8\x8D\xE5\xBC\x80"); /* 文件打不开 */
        return 0;
    }
    entry->size = jar.size;
    if (view == NJ_LIB_JAR_NOT_ZIP) {
        nj_lib_note(entry, "\xE4\xB8\x8D\xE6\x98\xAF ZIP \xE6\x96\x87\xE4\xBB\xB6"); /* 不是 ZIP 文件 */
        nj_lib_jar_close(&jar);
        return 0;
    }

    {
        const nj_zip_entry *manifest;
        char *text;
        nj_u4 text_len = 0;

        manifest = nj_zip_find(jar.zip, "META-INF/MANIFEST.MF");
        if (manifest == NULL) {
            manifest = nj_zip_find_ci(jar.zip, "META-INF/MANIFEST.MF");
        }
        if (manifest == NULL) {
            nj_lib_note(entry, "\xE7\xBC\xBA\xE5\xB0\x91\xE6\xB8\x85\xE5\x8D\x95"); /* 缺少清单 */
            nj_lib_jar_close(&jar);
            return 0;
        }
        text = nj_lib_manifest_text(&jar, manifest, &text_len);
        if (text == NULL) {
            nj_lib_note(entry, "\xE6\xB8\x85\xE5\x8D\x95\xE8\xAF\xBB\xE5\x8F\x96\xE5\xA4\xB1\xE8\xB4\xA5"); /* 清单读取失败 */
            nj_lib_jar_close(&jar);
            return 0;
        }

        {
            char midlet1[NJ_LIB_MANIFEST_VAL];
            char midlet_name[NJ_LIB_MANIFEST_VAL];
            char midlet_icon[NJ_LIB_MANIFEST_VAL];
            char display[NJ_LIB_NAME_MAX];
            char icon_path[NJ_LIB_FILE_MAX];
            char cls[NJ_LIB_FILE_MAX];
            int have_midlet1;
            int have_name;
            int have_icon;

            midlet1[0] = '\0';
            midlet_name[0] = '\0';
            midlet_icon[0] = '\0';
            display[0] = '\0';
            icon_path[0] = '\0';
            cls[0] = '\0';

            have_midlet1 = (nj_manifest_get(text, text_len, "MIDlet-1",
                                            midlet1, sizeof midlet1) == NJ_OK);
            have_name = (nj_manifest_get(text, text_len, "MIDlet-Name",
                                         midlet_name, sizeof midlet_name) == NJ_OK);
            have_icon = (nj_manifest_get(text, text_len, "MIDlet-Icon",
                                         midlet_icon, sizeof midlet_icon) == NJ_OK);

            if (have_midlet1) {
                nj_lib_parse_midlet1(midlet1, display, sizeof display,
                                     icon_path, sizeof icon_path,
                                     cls, sizeof cls);
            }
            if (have_name) {
                nj_lib_trim(midlet_name);
            } else {
                midlet_name[0] = '\0';
            }
            if (have_icon) {
                nj_lib_trim(midlet_icon);
            } else {
                midlet_icon[0] = '\0';
            }

            /* Display name: MIDlet-1 display, then MIDlet-Name, then file. */
            if (display[0] != '\0') {
                nj_lib_clean_name(display, entry->name, sizeof entry->name);
            }
            if (entry->name[0] == '\0' && midlet_name[0] != '\0') {
                nj_lib_clean_name(midlet_name, entry->name, sizeof entry->name);
            }
            if (entry->name[0] == '\0') {
                nj_lib_fallback_name(path, entry->name, sizeof entry->name);
            }

            /* Icon: MIDlet-1 field, then MIDlet-Icon. */
            if (icon_path[0] == '\0' && midlet_icon[0] != '\0') {
                nj_lib_copy_utf8(icon_path, sizeof icon_path, midlet_icon);
            }
            nj_lib_resolve_icon(jar.zip, icon_path, entry->icon, sizeof entry->icon);

            nj_lib_copy_utf8(entry->midlet, sizeof entry->midlet, cls);

            if (cls[0] != '\0') {
                entry->valid = 1;
                entry->note[0] = '\0';
            } else {
                entry->valid = 0;
                nj_lib_note(entry, "\xE6\xB8\x85\xE5\x8D\x95\xE9\x87\x8C\xE6\xB2\xA1\xE6\x9C\x89 MIDlet"); /* 清单里没有 MIDlet */
            }
        }
        free(text);
    }

    nj_lib_jar_close(&jar);
    return entry->valid;
}

/* ------------------------------------------------------------------ */
/* public: library object                                             */
/* ------------------------------------------------------------------ */

nj_library *nj_library_new(void)
{
    return (nj_library *)calloc(1, sizeof(nj_library));
}

static void nj_lib_free_pending(nj_library *lib)
{
    int i;

    if (lib == NULL) {
        return;
    }
    if (lib->pending != NULL) {
        for (i = 0; i < lib->pending_count; i++) {
            free(lib->pending[i]);
        }
        free(lib->pending);
    }
    lib->pending = NULL;
    lib->pending_count = 0;
    lib->pending_index = 0;
}

void nj_library_clear(nj_library *lib)
{
    int i;

    if (lib == NULL) {
        return;
    }
    if (lib->icons != NULL) {
        for (i = 0; i < lib->cap; i++) {
            if (lib->icons[i].image != NULL) {
                nj_image_free(lib->icons[i].image, NULL, NULL);
            }
        }
        free(lib->icons);
        lib->icons = NULL;
    }
    free(lib->entries);
    lib->entries = NULL;
    lib->count = 0;
    lib->cap = 0;
    lib->valid_count = 0;
    nj_lib_free_pending(lib);
    lib->scan_active = 0;
    lib->done = 0;
    /* `dir` is kept: a rescan still knows where the last scan looked. */
}

void nj_library_free(nj_library *lib)
{
    if (lib == NULL) {
        return;
    }
    nj_library_clear(lib);
    free(lib);
}

int nj_lib_add(nj_library *lib, const char *path)
{
    nj_lib_entry probe;
    int i;

    if (lib == NULL || path == NULL || path[0] == '\0') {
        return -1;
    }
    nj_lib_probe(path, &probe);

    for (i = 0; i < lib->count; i++) {
        if (strcmp(lib->entries[i].path, probe.path) == 0) {
            return i;                       /* already listed */
        }
    }

    if (lib->count == lib->cap) {
        int cap = (lib->cap > 0) ? lib->cap * 2 : NJ_LIB_INIT_CAP;
        nj_lib_entry *entries;
        nj_lib_icon_entry *icons;

        entries = (nj_lib_entry *)realloc(lib->entries, (size_t)cap * sizeof *entries);
        if (entries == NULL) {
            return -1;
        }
        lib->entries = entries;
        icons = (nj_lib_icon_entry *)realloc(lib->icons, (size_t)cap * sizeof *icons);
        if (icons == NULL) {
            return -1;
        }
        lib->icons = icons;
        memset(lib->icons + lib->cap, 0,
               (size_t)(cap - lib->cap) * sizeof *lib->icons);
        lib->cap = cap;
    }

    lib->entries[lib->count] = probe;
    lib->icons[lib->count].image = NULL;
    lib->icons[lib->count].attempted = 0;
    lib->count++;
    if (probe.valid) {
        lib->valid_count++;
    }
    return lib->count - 1;
}

/* ------------------------------------------------------------------ */
/* public: directory scanning                                         */
/* ------------------------------------------------------------------ */

static int nj_lib_name_cmp(const void *pa, const void *pb)
{
    const char *const *a = (const char *const *)pa;
    const char *const *b = (const char *const *)pb;

    return strcmp(*a, *b);
}

static int nj_lib_entry_cmp(const void *pa, const void *pb)
{
    const nj_lib_entry *a = (const nj_lib_entry *)pa;
    const nj_lib_entry *b = (const nj_lib_entry *)pb;

    if (a->valid != b->valid) {
        return (a->valid != 0) ? -1 : 1;    /* runnable games first */
    }
    return strcmp(a->name, b->name);
}

/*
 * Collect the .jar basenames of one directory (regular files or symlinks to
 * regular files; subdirectories are deliberately not followed).  The caller
 * owns the strings and frees them.
 */
static int nj_lib_collect_jars(const char *dir, char ***names_out, int *count_out)
{
    *names_out = NULL;
    *count_out = 0;
#if NJ_LIB_DIRENT && defined(_WIN32)
    {
        char **names = NULL;
        int count = 0;
        int cap = 0;
        int failed = 0;
        nj_win32_dir d;

        if (!nj_win32_dir_open(&d, dir)) {
            return 0;
        }
        for (;;) {
            char name[1024];
            char path[NJ_LIB_PATH_MAX + NJ_LIB_FILE_MAX + 2];
            int is_dir = 0;
            unsigned long long size = 0;
            char *copy;
            size_t len;
            int rc = nj_win32_dir_next(&d, name, (int)sizeof name, &is_dir,
                                       &size);

            if (rc < 0) {
                failed = 1;
                break;
            }
            if (rc == 0) {
                break;
            }
            if (is_dir || !nj_lib_is_jar_name(name)) {
                continue;
            }
            len = strlen(name);
            if (len >= (size_t)NJ_LIB_FILE_MAX) {
                continue;                   /* cannot be stored in the entry */
            }
            if (!nj_lib_join(path, sizeof path, dir, name) ||
                !nj_win32_stat(path, NULL)) {
                continue;                   /* gone between listing and open */
            }
            if (count == cap) {
                int ncap = (cap > 0) ? cap * 2 : NJ_LIB_INIT_CAP;
                char **grown = (char **)realloc(names, (size_t)ncap * sizeof *grown);
                if (grown == NULL) {
                    failed = 1;
                    break;
                }
                names = grown;
                cap = ncap;
            }
            copy = (char *)malloc(len + 1u);
            if (copy == NULL) {
                failed = 1;
                break;
            }
            memcpy(copy, name, len + 1u);
            names[count++] = copy;
        }
        nj_win32_dir_close(&d);
        if (failed) {
            int i;
            for (i = 0; i < count; i++) {
                free(names[i]);
            }
            free(names);
            return 0;
        }
        *names_out = names;
        *count_out = count;
        return 1;
    }
#elif NJ_LIB_DIRENT
    {
        char **names = NULL;
        int count = 0;
        int cap = 0;
        DIR *d = opendir(dir);
        struct dirent *de;
        int failed = 0;

        if (d == NULL) {
            return 0;
        }
        while ((de = readdir(d)) != NULL) {
            char path[NJ_LIB_PATH_MAX + NJ_LIB_FILE_MAX + 2];
            struct stat st;
            char *copy;
            size_t len;

            if (de->d_name[0] == '.' &&
                (de->d_name[1] == '\0' ||
                 (de->d_name[1] == '.' && de->d_name[2] == '\0'))) {
                continue;
            }
            if (!nj_lib_is_jar_name(de->d_name)) {
                continue;
            }
            len = strlen(de->d_name);
            if (len >= (size_t)NJ_LIB_FILE_MAX) {
                continue;                   /* cannot be stored in the entry */
            }
            if (!nj_lib_join(path, sizeof path, dir, de->d_name)) {
                continue;
            }
            if (stat(path, &st) != 0 || !S_ISREG(st.st_mode)) {
                continue;                   /* directories, broken links */
            }
            if (count == cap) {
                int ncap = (cap > 0) ? cap * 2 : NJ_LIB_INIT_CAP;
                char **grown = (char **)realloc(names, (size_t)ncap * sizeof *grown);
                if (grown == NULL) {
                    failed = 1;
                    break;
                }
                names = grown;
                cap = ncap;
            }
            copy = (char *)malloc(len + 1u);
            if (copy == NULL) {
                failed = 1;
                break;
            }
            memcpy(copy, de->d_name, len + 1u);
            names[count++] = copy;
        }
        closedir(d);
        if (failed) {
            int i;
            for (i = 0; i < count; i++) {
                free(names[i]);
            }
            free(names);
            return 0;
        }
        *names_out = names;
        *count_out = count;
        return 1;
    }
#else
    (void)dir;
    return 0;
#endif
}

static void nj_lib_scan_finish(nj_library *lib)
{
    nj_lib_free_pending(lib);
    lib->scan_active = 0;
    lib->done = 1;
    /* Icons are all still un-attempted (a scan clears the library first),
     * so sorting the entries cannot desynchronise the icons array. */
    if (lib->count > 1) {
        qsort(lib->entries, (size_t)lib->count, sizeof *lib->entries,
              nj_lib_entry_cmp);
    }
}

int nj_lib_scan_begin(nj_library *lib, const char *dir)
{
    char abs[NJ_LIB_PATH_MAX];
    char **names = NULL;
    int name_count = 0;
    int i;

    if (lib == NULL) {
        return 0;
    }
    nj_library_clear(lib);
    lib->done = 1;                          /* until the scan really starts */

    if (dir == NULL || dir[0] == '\0' || !nj_lib_abs_path(dir, abs, sizeof abs)) {
        lib->dir[0] = '\0';
        return 0;
    }
    nj_lib_copy_utf8(lib->dir, sizeof lib->dir, abs);

    if (!nj_lib_collect_jars(lib->dir, &names, &name_count)) {
        return 0;                           /* empty, consistent library */
    }
    if (name_count > 1) {
        qsort(names, (size_t)name_count, sizeof *names, nj_lib_name_cmp);
    }

    if (name_count > 0) {
        lib->pending = (char **)malloc((size_t)name_count * sizeof *lib->pending);
        if (lib->pending == NULL) {
            for (i = 0; i < name_count; i++) {
                free(names[i]);
            }
            free(names);
            return 0;
        }
        for (i = 0; i < name_count; i++) {
            char path[NJ_LIB_PATH_MAX + NJ_LIB_FILE_MAX + 2];
            char *copy;
            size_t len;

            if (!nj_lib_join(path, sizeof path, lib->dir, names[i])) {
                nj_lib_free_pending(lib);
                for (i = 0; i < name_count; i++) {
                    free(names[i]);
                }
                free(names);
                return 0;
            }
            len = strlen(path);
            copy = (char *)malloc(len + 1u);
            if (copy == NULL) {
                nj_lib_free_pending(lib);
                for (i = 0; i < name_count; i++) {
                    free(names[i]);
                }
                free(names);
                return 0;
            }
            memcpy(copy, path, len + 1u);
            lib->pending[i] = copy;
            lib->pending_count = i + 1;    /* partial list stays consistent */
        }
    }
    for (i = 0; i < name_count; i++) {
        free(names[i]);
    }
    free(names);

    lib->pending_index = 0;
    lib->scan_active = 1;
    lib->done = 0;
    return 1;
}

int nj_lib_scan_step(nj_library *lib, int budget)
{
    int added = 0;

    if (lib == NULL) {
        return 0;
    }
    if (!lib->scan_active) {
        if (lib->pending != NULL) {
            nj_lib_free_pending(lib);
        }
        return 0;
    }
    while (budget > 0 && lib->pending_index < lib->pending_count) {
        int index = lib->pending_index;
        char *path = lib->pending[index];

        lib->pending_index++;
        if (path != NULL) {
            if (nj_lib_add(lib, path) >= 0) {
                added++;
            }
            free(path);
            lib->pending[index] = NULL;
        }
        budget--;
    }
    if (lib->pending_index >= lib->pending_count) {
        nj_lib_scan_finish(lib);
    }
    return added;
}

int nj_lib_scan_busy(const nj_library *lib)
{
    return (lib != NULL && lib->scan_active) ? 1 : 0;
}

int nj_lib_scan(nj_library *lib, const char *dir)
{
    if (lib == NULL) {
        return 0;
    }
    if (!nj_lib_scan_begin(lib, dir)) {
        return 0;
    }
    while (lib->scan_active) {
        nj_lib_scan_step(lib, 64);
    }
    return lib->count;
}

int nj_lib_rescan(nj_library *lib)
{
    char dir[NJ_LIB_PATH_MAX];

    if (lib == NULL || lib->dir[0] == '\0') {
        return 0;
    }
    nj_lib_copy_utf8(dir, sizeof dir, lib->dir);
    return nj_lib_scan(lib, dir);
}

/*
 * Resolve a saved path string (client.conf remembers recent/starred jars) to
 * the library row it names.  The comparison is a plain strcmp, matching what
 * nj_lib_add uses to reject duplicates: a path from a settings file is the
 * absolute path the scan stored, and a jar that moved or was deleted simply
 * does not match anything.
 */
const nj_lib_entry *nj_library_find_path(const nj_library *lib, const char *path)
{
    int i;

    if (lib == NULL || lib->entries == NULL || path == NULL || path[0] == '\0') {
        return NULL;
    }
    for (i = 0; i < lib->count; i++) {
        if (strcmp(lib->entries[i].path, path) == 0) {
            return &lib->entries[i];
        }
    }
    return NULL;
}

/* ------------------------------------------------------------------ */
/* public: jars and icons                                             */
/* ------------------------------------------------------------------ */

int nj_lib_is_jar_name(const char *name)
{
    const char *base;
    size_t n;

    if (name == NULL) {
        return 0;
    }
    base = nj_lib_basename(name);
    n = strlen(base);
    if (n < 4u || base[n - 4u] != '.') {
        return 0;
    }
    return (nj_lib_lower((unsigned char)base[n - 3u]) == 'j' &&
            nj_lib_lower((unsigned char)base[n - 2u]) == 'a' &&
            nj_lib_lower((unsigned char)base[n - 1u]) == 'r') ? 1 : 0;
}

const nj_image *nj_lib_icon(nj_library *lib, int index)
{
    nj_lib_entry *entry;
    nj_lib_icon_entry *slot;
    nj_lib_jar jar;
    const nj_zip_entry *manifest_icon;
    nj_u1 *buf;
    nj_s4 got;
    nj_image *image;

    if (lib == NULL || lib->icons == NULL || lib->entries == NULL) {
        return NULL;
    }
    if (index < 0 || index >= lib->count) {
        return NULL;
    }
    slot = &lib->icons[index];
    if (slot->attempted) {
        return slot->image;                 /* success and failure both cached */
    }
    slot->attempted = 1;

    entry = &lib->entries[index];
    if (entry->icon[0] == '\0' || entry->path[0] == '\0') {
        return NULL;
    }
    if (nj_lib_jar_open(entry->path, &jar) != NJ_LIB_JAR_OPENED) {
        nj_lib_jar_close(&jar);             /* drops a map of a non-ZIP file */
        return NULL;
    }
    manifest_icon = nj_zip_find(jar.zip, entry->icon);
    if (manifest_icon == NULL) {
        manifest_icon = nj_zip_find_ci(jar.zip, entry->icon);
    }
    if (manifest_icon == NULL || manifest_icon->uncomp_size == 0u ||
        manifest_icon->uncomp_size > NJ_LIB_ICON_MAX) {
        nj_lib_jar_close(&jar);
        return NULL;
    }
    buf = (nj_u1 *)malloc((size_t)manifest_icon->uncomp_size);
    if (buf == NULL) {
        nj_lib_jar_close(&jar);
        return NULL;
    }
    got = nj_zip_extract(jar.zip, manifest_icon, buf, manifest_icon->uncomp_size);
    nj_lib_jar_close(&jar);                 /* map is not needed while decoding */
    if (got <= 0) {
        free(buf);
        return NULL;
    }
    image = nj_image_load(buf, (nj_u4)got, NULL, NULL, NULL, NULL);
    free(buf);
    slot->image = image;
    return image;
}

/* ------------------------------------------------------------------ */
/* public: directory listing                                          */
/* ------------------------------------------------------------------ */

#if NJ_LIB_DIRENT
static int nj_lib_list_push(nj_lib_dir_entry **list, int *count, int *cap,
                            const nj_lib_dir_entry *row)
{
    if (*count == *cap) {
        int ncap = (*cap > 0) ? *cap * 2 : NJ_LIB_INIT_CAP;
        nj_lib_dir_entry *grown;

        grown = (nj_lib_dir_entry *)realloc(*list, (size_t)ncap * sizeof *grown);
        if (grown == NULL) {
            return 0;
        }
        *list = grown;
        *cap = ncap;
    }
    (*list)[(*count)++] = *row;
    return 1;
}

static int nj_lib_dir_cmp(const void *pa, const void *pb)
{
    const nj_lib_dir_entry *a = (const nj_lib_dir_entry *)pa;
    const nj_lib_dir_entry *b = (const nj_lib_dir_entry *)pb;

    if (a->is_dir != b->is_dir) {
        return (a->is_dir != 0) ? -1 : 1;   /* directories first */
    }
    return strcmp(a->name, b->name);
}
#endif

int nj_lib_list_dir(const char *dir, nj_lib_dir_entry **out, int *count)
{
    char abs[NJ_LIB_PATH_MAX];

    if (out != NULL) {
        *out = NULL;
    }
    if (count != NULL) {
        *count = 0;
    }
    if (dir == NULL || dir[0] == '\0') {
        return 0;
    }
    if (!nj_lib_abs_path(dir, abs, sizeof abs)) {
        return 0;
    }
#if NJ_LIB_DIRENT && defined(_WIN32)
    {
        nj_lib_dir_entry *list = NULL;
        int n = 0;
        int cap = 0;
        nj_win32_dir d;

        if (!nj_win32_dir_open(&d, abs)) {
            return 0;
        }
        for (;;) {
            char name[1024];
            int is_dir = 0;
            unsigned long long size = 0;
            nj_lib_dir_entry row;
            size_t len;
            int rc = nj_win32_dir_next(&d, name, (int)sizeof name, &is_dir,
                                       &size);

            if (rc < 0) {
                free(list);
                nj_win32_dir_close(&d);
                return 0;
            }
            if (rc == 0) {
                break;
            }
            len = strlen(name);
            if (len >= (size_t)NJ_LIB_FILE_MAX) {
                continue;
            }
            memset(&row, 0, sizeof row);
            nj_lib_copy_utf8(row.name, sizeof row.name, name);
            if (is_dir) {
                row.is_dir = 1;
            } else if (nj_lib_is_jar_name(name)) {
                row.is_dir = 0;
                row.size = nj_lib_size_u4((nj_u8)size);
            } else {
                continue;
            }
            if (!nj_lib_list_push(&list, &n, &cap, &row)) {
                free(list);
                nj_win32_dir_close(&d);
                return 0;
            }
        }
        nj_win32_dir_close(&d);
        if (n > 1) {
            qsort(list, (size_t)n, sizeof *list, nj_lib_dir_cmp);
        }
        if (out != NULL) {
            *out = list;
        } else {
            free(list);
        }
        if (count != NULL) {
            *count = n;
        }
        return n;
    }
#elif NJ_LIB_DIRENT
    {
        nj_lib_dir_entry *list = NULL;
        int n = 0;
        int cap = 0;
        DIR *d = opendir(abs);
        struct dirent *de;

        if (d == NULL) {
            return 0;
        }
        while ((de = readdir(d)) != NULL) {
            char path[NJ_LIB_PATH_MAX + NJ_LIB_FILE_MAX + 2];
            struct stat st;
            nj_lib_dir_entry row;
            size_t len;

            if (de->d_name[0] == '.' &&
                (de->d_name[1] == '\0' ||
                 (de->d_name[1] == '.' && de->d_name[2] == '\0'))) {
                continue;
            }
            len = strlen(de->d_name);
            if (len >= (size_t)NJ_LIB_FILE_MAX) {
                continue;
            }
            if (!nj_lib_join(path, sizeof path, abs, de->d_name)) {
                continue;
            }
            if (stat(path, &st) != 0) {     /* follows symlinks */
                continue;
            }
            memset(&row, 0, sizeof row);
            nj_lib_copy_utf8(row.name, sizeof row.name, de->d_name);
            if (S_ISDIR(st.st_mode)) {
                row.is_dir = 1;
            } else if (S_ISREG(st.st_mode) && nj_lib_is_jar_name(de->d_name)) {
                row.is_dir = 0;
                row.size = nj_lib_size_u4((nj_u8)st.st_size);
            } else {
                continue;
            }
            if (!nj_lib_list_push(&list, &n, &cap, &row)) {
                free(list);
                return 0;
            }
        }
        closedir(d);
        if (n > 1) {
            qsort(list, (size_t)n, sizeof *list, nj_lib_dir_cmp);
        }
        if (out != NULL) {
            *out = list;
        } else {
            free(list);
        }
        if (count != NULL) {
            *count = n;
        }
        return n;
    }
#else
    (void)abs;
    return 0;
#endif
}

void nj_lib_free_list(nj_lib_dir_entry *list)
{
    free(list);
}
