/*
 * nano_j2me - nj_library.h
 *
 * The games library behind the desktop client: a directory of .jar files,
 * each opened once to learn the name its manifest gives it, the MIDlet class
 * to start, and the icon its MIDlet-1 points at.
 *
 * This module does the I/O - dirent, fopen, the ZIP reader, the PNG decoder -
 * and draws nothing.  The launcher UI (nj_client.h) and the tests both use it,
 * so it knows nothing about windows either.
 *
 * Two things it deliberately does NOT do:
 *
 *   - it does not read a whole jar to find a name.  The archive is mmap'd for
 *     the duration of one probe and unmapped again, so scanning a folder of
 *     four hundred games touches a few hundred KB of the corpus, not 300 MB;
 *   - it does not decide that a broken file is an error.  A jar that cannot be
 *     opened stays in the list with `valid == 0` and the reason in `note`, so
 *     the launcher can say "this one is broken" instead of pretending the user
 *     never had the file.
 *
 * Hosted builds only: a bare-metal target has no library to scan.
 */
#ifndef NJ_LIBRARY_H
#define NJ_LIBRARY_H

#include "nj_types.h"
#include "nj_gfx.h"

#ifdef __cplusplus
extern "C" {
#endif

#define NJ_LIB_PATH_MAX 1024
#define NJ_LIB_FILE_MAX 128
#define NJ_LIB_NAME_MAX 192

/* One .jar the client knows about. */
typedef struct nj_lib_entry {
    char   path[NJ_LIB_PATH_MAX];   /* absolute when the library scanned it */
    char   file[NJ_LIB_FILE_MAX];   /* basename, shown as the secondary line */
    char   name[NJ_LIB_NAME_MAX];   /* UTF-8 display name, never empty */
    char   midlet[NJ_LIB_FILE_MAX]; /* MIDlet-1 class, "" when the jar has none */
    char   icon[NJ_LIB_FILE_MAX];   /* ZIP entry holding the icon, "" for none */
    nj_u4  size;                    /* file size in bytes, 0 when unknown */
    int    valid;                   /* 1 = runs; 0 = shown, not runnable */
    char   note[64];                /* why an invalid jar cannot run, "" if ok */
} nj_lib_entry;

/* A lazily decoded icon, one per entry.  (The typedef is named
 * nj_lib_icon_entry, not nj_lib_icon, because that is the function name.) */
typedef struct nj_lib_icon_entry {
    nj_image *image;   /* NULL until nj_lib_icon() tried and after it failed */
    int       attempted;
} nj_lib_icon_entry;

/*
 * The library.  `dir` is the directory the current scan belongs to; entries
 * survive it.  A progressive scan is a pending name list (see
 * nj_lib_scan_begin/step) so a window can paint "scanning 37/400" instead of
 * freezing on a slow disk.
 */
typedef struct nj_library {
    nj_lib_entry *entries;
    nj_lib_icon_entry *icons;
    int           count;
    int           cap;
    int           valid_count;
    char          dir[NJ_LIB_PATH_MAX];
    char        **pending;        /* absolute paths waiting to be probed */
    int           pending_count;
    int           pending_index;
    int           scan_active;
    int           done;           /* a scan has finished since the last clear */
} nj_library;

/* One row of a directory listing: a subdirectory or a .jar file. */
typedef struct nj_lib_dir_entry {
    char  name[NJ_LIB_FILE_MAX];   /* UTF-8 basename, no trailing slash */
    int   is_dir;
    nj_u4 size;
} nj_lib_dir_entry;

/* Ownership: nj_library_new returns NULL on OOM; free(NULL) is fine. */
nj_library *nj_library_new(void);
void        nj_library_free(nj_library *lib);

/* Drop every entry and every icon and forget the scan.  Leaves the object
 * allocated: `dir` is kept so a rescan knows where it was. */
void nj_library_clear(nj_library *lib);

/*
 * Open one .jar and fill `out` - but never touch any library.  Returns 1 when
 * the file is a ZIP that names a MIDlet (MIDlet-1 or MIDlet-Name), 0 when it
 * is not runnable.  `out` is filled either way (a name derived from the file
 * name, `valid` and `note` explaining the verdict), so the caller can list a
 * broken jar too.  `path` must be NUL-terminated UTF-8; on Windows a UTF-8
 * path is opened through the wide API.
 */
int nj_lib_probe(const char *path, nj_lib_entry *out);

/*
 * Append one jar (probe + duplicate check) and return its index, or -1 when
 * the entry cannot be added (OOM; a duplicate path is a no-op returning the
 * existing index).  The icon is not decoded here.
 */
int nj_lib_add(nj_library *lib, const char *path);

/*
 * Scan `dir` for *.jar files, one entry at a time.
 *
 *   nj_lib_scan_begin  reads the directory, sorts the names and returns 1 (0
 *                      on OOM or when `dir` cannot be opened);
 *   nj_lib_scan_step   probes up to `budget` pending files and returns the
 *                      number of entries appended (0 when the scan finished);
 *   nj_lib_scan_busy   1 while a begun scan has pending files;
 *   nj_lib_scan        begin + step until done, i.e. the blocking one-shot
 *                      version used by tests and by --print-library.
 *
 * The scan is a full rebuild: begin clears the library first.  Names ending
 * in .jar are case-insensitive; subdirectories are not followed.
 */
int nj_lib_scan_begin(nj_library *lib, const char *dir);
int nj_lib_scan_step(nj_library *lib, int budget);
int nj_lib_scan_busy(const nj_library *lib);
int nj_lib_scan(nj_library *lib, const char *dir);

/* Rescan the directory of the last scan (blocking).  Returns the entry count. */
int nj_lib_rescan(nj_library *lib);

/*
 * Decode entry `index`'s icon on first use and cache it.  Returns NULL when
 * the jar names no icon, the entry is out of range, or decoding failed; that
 * answer is cached too, so a broken icon costs one attempt.
 *
 * The returned image is owned by the library and stays valid until
 * nj_library_clear()/nj_library_free()/a rescan.
 */
const nj_image *nj_lib_icon(nj_library *lib, int index);

/* The entry with this jar path, or NULL.  Paths are compared as strings,
 * which is what a settings file can reproduce; the library's own entries are
 * the absolute paths the scan produced. */
const nj_lib_entry *nj_library_find_path(const nj_library *lib, const char *path);

/* Case-insensitive "*.jar" basename test (a path works too). */
int nj_lib_is_jar_name(const char *name);

/*
 * List one directory: subdirectories first (sorted by name), then *.jar files
 * (sorted by name).  Returns the number of rows and fills `*out` with a
 * malloc'd array (call nj_lib_free_list) or 0 and *out = NULL.  A NULL
 * directory, an unreadable one, or a non-POSIX build returns 0.
 */
int  nj_lib_list_dir(const char *dir, nj_lib_dir_entry **out, int *count);
void nj_lib_free_list(nj_lib_dir_entry *list);

#ifdef __cplusplus
}
#endif

#endif /* NJ_LIBRARY_H */
