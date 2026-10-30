/*
 * nano_j2me - src/port/posix/nj_posix_win32.h
 *
 * The handful of Windows-hosting shims the POSIX-shaped desktop sources share.
 *
 * The headless and SDL2 ports, the games library and the settings file are all
 * written against the POSIX C library: fopen/stat/mkdir/opendir, UTF-8 path
 * strings.  mingw-w64 supplies those names, but they go through the ANSI code
 * page, so a jar called 1942空战_10403.jar cannot be opened from a UTF-8 C
 * string.  This header keeps every Windows-specific call in one place:
 *
 *   - paths are UTF-8, converted to UTF-16 for the wide file APIs, and '/'
 *     becomes '\' (the Win32 file APIs are happiest with backslashes);
 *   - file_open/file_exists map to _wfopen/GetFileAttributesExW;
 *   - directory listing maps to FindFirstFileW (dirent.h exists in mingw but
 *     is ANSI-only and cannot represent such a name);
 *   - the executable-memory helpers have no shim here: nj_port_headless.c
 *     uses VirtualAlloc/VirtualProtect/VirtualFree directly.
 *
 * Everything is static inline and no-ops on POSIX, so callers may include it
 * unconditionally and the Linux build is byte-for-byte the code it was.
 */
#ifndef NJ_POSIX_WIN32_H
#define NJ_POSIX_WIN32_H

#if defined(_WIN32)

#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#  define NOMINMAX
#endif

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

/* Long enough for NJ_CLIENT_PATH_MAX/NJ_LIB_PATH_MAX UTF-8 bytes plus the
 * directory-wildcard suffix, in UTF-16 units. */
#define NJ_WIN32_WIDE_MAX 2048

/* ---------------------------------------------------------------- */
/* UTF-8 <-> UTF-16                                                  */
/* ---------------------------------------------------------------- */

static inline int nj_win32_utf8_to_wide(const char *in, wchar_t *out, int cap)
{
    int n;

    if (!in || !out || cap <= 0) return 0;
    n = MultiByteToWideChar(CP_UTF8, 0, in, -1, out, cap);
    if (n <= 0) {
        out[0] = 0;
        return 0;
    }
    return n - 1;                       /* characters, NUL excluded */
}

static inline int nj_win32_wide_to_utf8(const wchar_t *in, char *out, int cap)
{
    int n;

    if (!in || !out || cap <= 0) return 0;
    n = WideCharToMultiByte(CP_UTF8, 0, in, -1, out, cap, NULL, NULL);
    if (n <= 0) {
        out[0] = 0;
        return 0;
    }
    return n - 1;                       /* bytes, NUL excluded */
}

/* Path form of the conversion: Win32 file APIs get backslashes. */
static inline int nj_win32_path_to_wide(const char *path, wchar_t *out, int cap)
{
    int i, n;

    n = nj_win32_utf8_to_wide(path, out, cap);
    if (n <= 0) return 0;
    for (i = 0; i < n; i++) {
        if (out[i] == L'/') out[i] = L'\\';
    }
    return n;
}

/* ---------------------------------------------------------------- */
/* Files                                                            */
/* ---------------------------------------------------------------- */

static inline FILE *nj_win32_fopen(const char *path, const char *mode)
{
    wchar_t wpath[NJ_WIN32_WIDE_MAX];
    wchar_t wmode[16];

    if (!path || !nj_win32_path_to_wide(path, wpath, NJ_WIN32_WIDE_MAX)) {
        return NULL;
    }
    if (!mode || mode[0] == '\0') mode = "rb";
    if (!nj_win32_utf8_to_wide(mode, wmode, (int)(sizeof wmode / sizeof wmode[0]))) {
        return NULL;
    }
    return _wfopen(wpath, wmode);
}

typedef struct nj_win32_file_info {
    unsigned long long size;
    long long          mtime_ms;    /* Unix ms, 0 when unavailable */
    int                is_dir;
    int                is_regular;
} nj_win32_file_info;

/* 1 when the path exists (a directory counts), 0 otherwise. */
static inline int nj_win32_stat(const char *path, nj_win32_file_info *out)
{
    wchar_t wpath[NJ_WIN32_WIDE_MAX];
    WIN32_FILE_ATTRIBUTE_DATA data;

    if (out) {
        out->size = 0u;
        out->mtime_ms = 0;
        out->is_dir = 0;
        out->is_regular = 0;
    }
    if (!path || path[0] == '\0' ||
        !nj_win32_path_to_wide(path, wpath, NJ_WIN32_WIDE_MAX)) {
        return 0;
    }
    if (!GetFileAttributesExW(wpath, GetFileExInfoStandard, &data)) {
        return 0;
    }
    if (out) {
        ULARGE_INTEGER ft;

        out->size = ((unsigned long long)data.nFileSizeHigh << 32)
                  | (unsigned long long)data.nFileSizeLow;
        /* FILETIME: 100 ns ticks since 1601-01-01; Unix ms is the same value
         * divided by 10000, minus the 1601 -> 1970 offset. */
        ft.LowPart = data.ftLastWriteTime.dwLowDateTime;
        ft.HighPart = data.ftLastWriteTime.dwHighDateTime;
        out->mtime_ms = (long long)(ft.QuadPart / 10000u)
                      - 11644473600000LL;
        out->is_dir = (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ? 1 : 0;
        out->is_regular = out->is_dir ? 0 : 1;
    }
    return 1;
}

static inline int nj_win32_remove(const char *path)
{
    wchar_t wpath[NJ_WIN32_WIDE_MAX];

    if (!path || !nj_win32_path_to_wide(path, wpath, NJ_WIN32_WIDE_MAX)) return -1;
    return _wremove(wpath) == 0 ? 0 : -1;
}

static inline int nj_win32_rename(const char *from, const char *to)
{
    wchar_t wfrom[NJ_WIN32_WIDE_MAX];
    wchar_t wto[NJ_WIN32_WIDE_MAX];

    if (!from || !to ||
        !nj_win32_path_to_wide(from, wfrom, NJ_WIN32_WIDE_MAX) ||
        !nj_win32_path_to_wide(to, wto, NJ_WIN32_WIDE_MAX)) {
        return -1;
    }
    return _wrename(wfrom, wto) == 0 ? 0 : -1;
}

/* Rename and replace: CRT rename()/_wrename() fail when the destination
 * exists (MSVC semantics), while an atomic settings write needs the POSIX
 * replace behaviour.  MoveFileExW gives that. */
static inline int nj_win32_replace(const char *from, const char *to)
{
    wchar_t wfrom[NJ_WIN32_WIDE_MAX];
    wchar_t wto[NJ_WIN32_WIDE_MAX];

    if (!from || !to ||
        !nj_win32_path_to_wide(from, wfrom, NJ_WIN32_WIDE_MAX) ||
        !nj_win32_path_to_wide(to, wto, NJ_WIN32_WIDE_MAX)) {
        return -1;
    }
    return MoveFileExW(wfrom, wto, MOVEFILE_REPLACE_EXISTING) ? 0 : -1;
}

static inline int nj_win32_is_dir(const char *path)
{
    nj_win32_file_info info;

    return nj_win32_stat(path, &info) && info.is_dir;
}

/* Create one level from a wide path; existing directories are success. */
static inline int nj_win32_wmkdir(const wchar_t *wpath)
{
    DWORD attrs;

    if (!wpath || wpath[0] == 0) return -1;
    if (_wmkdir(wpath) == 0) return 0;
    if (GetLastError() == ERROR_ALREADY_EXISTS) return 0;
    attrs = GetFileAttributesW(wpath);
    if (attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY)) {
        return 0;
    }
    return -1;
}

/* Create one level; existing directories are success. */
static inline int nj_win32_mkdir(const char *path)
{
    wchar_t wpath[NJ_WIN32_WIDE_MAX];

    if (!path || !nj_win32_path_to_wide(path, wpath, NJ_WIN32_WIDE_MAX)) return -1;
    return nj_win32_wmkdir(wpath);
}

/* mkdir -p for absolute paths ("C:\a\b", "Z:\a\b"); no drive creation. */
static inline int nj_win32_mkdir_p(const char *path)
{
    wchar_t wpath[NJ_WIN32_WIDE_MAX];
    wchar_t part[NJ_WIN32_WIDE_MAX];
    int i, n;

    if (!path || !nj_win32_path_to_wide(path, wpath, NJ_WIN32_WIDE_MAX)) return 0;

    n = (int)wcslen(wpath);
    while (n > 0 && wpath[n - 1] == L'\\') {
        if (n == 3 && wpath[1] == L':') break;   /* keep "C:\" */
        wpath[--n] = 0;
    }
    for (i = 0; i <= n; i++) {
        if (i < n && wpath[i] != L'\\') continue;
        if (i == 0) continue;
        /* "C:" alone is a drive, not a directory to create. */
        if (i == 2 && wpath[1] == L':') continue;
        /* UNC "\\server\share": creating the server component is wrong. */
        if (i < n + 1 && i > 0 && wpath[0] == L'\\' && wpath[1] == L'\\' &&
            i <= 2) {
            continue;
        }
        memcpy(part, wpath, (size_t)i * sizeof *part);
        part[i] = 0;
        if (nj_win32_wmkdir(part) != 0) return 0;
    }
    return 1;
}

/* The current directory, UTF-8, native separators; 1 on success. */
static inline int nj_win32_getcwd(char *out, int cap)
{
    wchar_t wd[NJ_WIN32_WIDE_MAX];
    DWORD n;

    if (!out || cap <= 0) return 0;
    n = GetCurrentDirectoryW((DWORD)(sizeof wd / sizeof wd[0]), wd);
    if (n == 0 || n >= (DWORD)(sizeof wd / sizeof wd[0])) return 0;
    if (!nj_win32_wide_to_utf8(wd, out, cap)) return 0;
    return 1;
}

/* ---------------------------------------------------------------- */
/* Directory enumeration                                            */
/* ---------------------------------------------------------------- */
/*
 * FindFirstFileW, not mingw's dirent.h: that one is built on the ANSI API, so
 * it returns "????.jar" for a Chinese name instead of the name.
 */
typedef struct nj_win32_dir {
    HANDLE          handle;
    WIN32_FIND_DATAW data;
    int             first;
    int             done;
} nj_win32_dir;

static inline void nj_win32_dir_close(nj_win32_dir *d)
{
    if (!d) return;
    if (d->handle != INVALID_HANDLE_VALUE) {
        FindClose(d->handle);
        d->handle = INVALID_HANDLE_VALUE;
    }
    d->done = 1;
}

static inline int nj_win32_dir_open(nj_win32_dir *d, const char *dir)
{
    char pattern[NJ_WIN32_WIDE_MAX];
    wchar_t wpattern[NJ_WIN32_WIDE_MAX];
    size_t n;

    if (!d || !dir) return 0;
    d->handle = INVALID_HANDLE_VALUE;
    d->first = 1;
    d->done = 0;

    n = strlen(dir);
    if (n + 3u > sizeof pattern) return 0;
    memcpy(pattern, dir, n);
    if (n == 0u || (pattern[n - 1u] != '/' && pattern[n - 1u] != '\\')) {
        pattern[n++] = '\\';
    }
    pattern[n++] = '*';
    pattern[n] = '\0';

    if (!nj_win32_path_to_wide(pattern, wpattern, NJ_WIN32_WIDE_MAX)) return 0;
    d->handle = FindFirstFileW(wpattern, &d->data);
    if (d->handle == INVALID_HANDLE_VALUE) {
        d->done = 1;
        return 0;
    }
    return 1;
}

/* 1 = entry, 0 = end, -1 = conversion error.  "." and ".." are skipped. */
static inline int nj_win32_dir_next(nj_win32_dir *d, char *name, int cap,
                                    int *is_dir, unsigned long long *size)
{
    for (;;) {
        if (!d || d->done || d->handle == INVALID_HANDLE_VALUE) return 0;
        if (!d->first) {
            if (!FindNextFileW(d->handle, &d->data)) {
                nj_win32_dir_close(d);
                return 0;
            }
        }
        d->first = 0;
        if (d->data.cFileName[0] == L'.' &&
            (d->data.cFileName[1] == 0 ||
             (d->data.cFileName[1] == L'.' && d->data.cFileName[2] == 0))) {
            continue;
        }
        if (!name || cap <= 0 ||
            !nj_win32_wide_to_utf8(d->data.cFileName, name, cap)) {
            return -1;
        }
        if (is_dir) {
            *is_dir = (d->data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ? 1 : 0;
        }
        if (size) {
            *size = ((unsigned long long)d->data.nFileSizeHigh << 32)
                  | (unsigned long long)d->data.nFileSizeLow;
        }
        return 1;
    }
}

/* ---------------------------------------------------------------- */
/* Environment and sleeps                                           */
/* ---------------------------------------------------------------- */

static inline void nj_win32_sleep_ms(unsigned long ms)
{
    if (ms > 0u) Sleep((DWORD)ms);
}

/* getenv() through the wide API, so a non-ANSI %APPDATA% also works. */
static inline int nj_win32_getenv_utf8(const char *name, char *out, int cap)
{
    wchar_t wname[64];
    wchar_t *wval;
    DWORD n;
    int got;

    if (out && cap > 0) out[0] = '\0';
    if (!name || !out || cap <= 0) return 0;
    if (!nj_win32_utf8_to_wide(name, wname,
                               (int)(sizeof wname / sizeof wname[0]))) {
        return 0;
    }
    n = GetEnvironmentVariableW(wname, NULL, 0);
    if (n == 0) return 0;
    wval = (wchar_t *)malloc((size_t)n * sizeof *wval);
    if (!wval) return 0;
    n = GetEnvironmentVariableW(wname, wval, n);
    if (n == 0) {
        free(wval);
        return 0;
    }
    got = nj_win32_wide_to_utf8(wval, out, cap);
    free(wval);
    return got > 0;
}

#endif /* _WIN32 */
#endif /* NJ_POSIX_WIN32_H */
