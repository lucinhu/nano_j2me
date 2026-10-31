/*
 * nano_j2me - nj_client_ui.c
 *
 * Portable UI model and software drawing for the desktop launcher.
 *
 * The window, the event loop, the SDL surface and every blocking file
 * operation belong to the embedder (src/client/main_sdl2.c); this module is
 * deliberately self-contained: it owns a little layout/raster state, paints
 * through nj_gfx onto a caller supplied ARGB8888 surface, and reports user
 * intent as a single pending action.  Keeping the model free of SDL makes the
 * launcher testable headless and lets a future frontend reuse it.
 *
 * Four screens are modelled here:
 *
 *   LIBRARY   the game list (title, icon, size, runnability), empty state and
 *             the footer shortcuts / buttons.
 *   BROWSER   a directory lister for picking a .jar or the games directory.
 *   SETTINGS  a modal panel over the dimmed library: games directory, scaling,
 *             filter, fullscreen, accel, on-screen keypad, fps cap, the key
 *             bindings page, heap size, mute, network and the reset row.
 *             Changes are written through to the borrowed nj_client_settings
 *             immediately.
 *   BINDS     the twelve game-action key slots, opened from SETTINGS.  Enter
 *             starts a one-key capture: the embedder feeds the raw SDL key
 *             name to nj_client_ui_capture_key() instead of navigating, and
 *             the model decides whether the key is allowed.
 *
 * All state lives in struct nj_client_ui (heap); there are no mutable file
 * scope statics, no threads and no sleeps - time arrives through
 * nj_client_ui_tick().
 */

#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif

#include "nj2me/nj.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if !defined(_WIN32)
#include <sys/stat.h>
#include <sys/types.h>
#endif

/* ------------------------------------------------------------------ */
/* Limits and layout constants                                         */
/* ------------------------------------------------------------------ */
#define UI_PATH_MAX   NJ_CLIENT_PATH_MAX     /* 1024, borrowed from the API */
#define UI_TEXT_MAX   512                    /* njs of raw decoded text     */
#define UI_DRAW_MAX   (UI_TEXT_MAX + 4)      /* room for the three dots     */
#define UI_TOAST_MAX  1024
#define UI_TOAST_MS   3500u

#define UI_HEADER_H   56
#define UI_FOOTER_H   40
#define UI_ROW_H      48
#define UI_BROW_ROW_H 40
#define UI_ICON       32
#define UI_SEARCH_H   32      /* search bar under the library header */
#define UI_FILTER_MAX 120     /* bytes of UTF-8 search text, no split chars */
#define UI_SECTION_H  28      /* "最近玩过" / "全部游戏" header rows */
#define UI_STAR_W     18      /* favourite star column before the name */
#define UI_HELP_W     620     /* F1 shortcut sheet, clamped to the window */
#define UI_HELP_H     400
#define UI_HELP_MARGIN 24

#define UI_BTN_W      76
#define UI_BTN_H      26
#define UI_BTN_GAP    8
#define UI_PICK_BTN_W 112

/* ------------------------------------------------------------------ */
/* Palette (ARGB8888, matching the spec in the launcher brief)         */
/* ------------------------------------------------------------------ */
#define UI_COL_BG         0xFF191C22u
#define UI_COL_CHROME     0xFF232733u
#define UI_COL_LINE       0xFF39404Fu
#define UI_COL_TITLE      0xFFF2F5FAu
#define UI_COL_DIM_TEXT   0xFF8A94A6u
#define UI_COL_SEL        0xFF2E6EA6u
#define UI_COL_HOVER      0xFF262C38u
#define UI_COL_ZEBRA_A    0xFF1E222Bu
#define UI_COL_ZEBRA_B    0xFF20242Du
#define UI_COL_ICON_BG    0xFF2B3342u
#define UI_COL_ROW_TITLE  0xFFEEF2F8u
#define UI_COL_ROW_SUB    0xFF93A0B2u
#define UI_COL_INVALID    0xFF7A8698u
#define UI_COL_BADGE      0xFFD08A6Au
#define UI_COL_THUMB      0xFF3E4A5Cu
#define UI_COL_BTN        0xFF2B3342u
#define UI_COL_BTN_HOVER  0xFF354054u
#define UI_COL_BTN_TEXT   0xFFE7ECF4u
#define UI_COL_PATH_BAR   0xFF20242Du
#define UI_COL_PATH_TEXT  0xFFC7D0DCu
#define UI_COL_FOLDER     0xFF4A6FA5u
#define UI_COL_PANEL      0xFF252B36u
#define UI_COL_PANEL_EDGE 0xFF3C4555u
#define UI_COL_MUTED      0xFF6F7A8Au
#define UI_COL_DIM        0xB0000000u
#define UI_COL_STAR       0xFFFFD166u /* the favourite star's accent */

/* ------------------------------------------------------------------ */
/* Model state                                                         */
/* ------------------------------------------------------------------ */
struct nj_client_ui {
    /* borrowed, not owned */
    nj_client_settings *settings;
    nj_library *lib;

    int width;
    int height;
    nj_u4 now_ms;
    int mode;

    int library_sel;
    int library_sel_recent;          /* selection sits in the recent section */
    int library_top;                 /* first visible row, headers excluded  */

    /* LIBRARY search: filter holds the typed UTF-8 bytes and filter_len the
     * exact byte count (the buffer is NUL terminated as well).  help_active
     * is the F1 shortcut sheet; both only mean anything in the library. */
    char filter[UI_FILTER_MAX + 1];
    int filter_len;
    int help_active;

    /* Last applied layout; recomputed by layout_apply() so a resize is free. */
    int header_h;
    int footer_h;
    int footer_y;
    int list_y;
    int list_h;
    int row_h;
    int visible_rows;

    int pointer_x;                    /* -1 until the pointer is known */
    int pointer_y;
    int button_rect[3][4];            /* 打开… / 设置 / 退出, last drawn */

    char browser_dir[UI_PATH_MAX];
    nj_lib_dir_entry *browser_entries;
    int browser_count;
    int browser_sel;
    int browser_top;
    int browser_visible;
    int browser_up;                   /* parent directory exists */
    int browser_pick_dir;
    int browser_return_mode;
    int pick_btn_rect[4];

    int settings_row;
    int settings_top;
    int settings_visible;
    int settings_panel[4];
    int settings_row_y;
    int settings_row_h;

    int bind_row;
    int bind_top;
    int bind_visible;
    int bind_panel[4];
    int bind_row_y;
    int bind_row_h;
    int capture_active;               /* waiting for the next key press */
    int capture_bind;                 /* slot that press will be stored in */

    char toast[UI_TOAST_MAX];
    nj_u4 toast_start;
    int toast_active;

    int action;
    char action_jar[UI_PATH_MAX];
    int quit;
};

/* Forward declarations: the input, browser and settings layers refer to
 * helpers defined in later sections, so announce them here. */
static void ui_settings_clamp(nj_client_ui *ui);
static void ui_binds_open(nj_client_ui *ui);
static void ui_lib_activate(nj_client_ui *ui);

/* ------------------------------------------------------------------ */
/* Small helpers                                                       */
/* ------------------------------------------------------------------ */
static int ui_min(int a, int b) { return a < b ? a : b; }
static int ui_max(int a, int b) { return a > b ? a : b; }
static int ui_clamp_int(int v, int lo, int hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static int ui_copy(char *out, int cap, const char *src)
{
    size_t n;
    if (!out || cap <= 0) return 0;
    if (!src) src = "";
    n = strlen(src);
    if (n >= (size_t)cap) return 0;
    memcpy(out, src, n + 1u);
    return 1;
}

/* File name part of a path, for header/toast text. */
static const char *ui_basename(const char *path)
{
    const char *cut = NULL;
    const char *p;
    if (!path) return "";
    for (p = path; *p; p++) {
        if (*p == '/' || *p == '\\') cut = p;
    }
    return cut ? cut + 1 : path;
}

/* Join a directory and a child name with a single separator. */
static int ui_join_path(char *out, int cap, const char *dir, const char *name)
{
    int dl, nl, slash, need;
    if (!out || cap <= 0 || !name) return 0;
    if (!dir || !dir[0]) dir = ".";
    dl = (int)strlen(dir);
    nl = (int)strlen(name);
    slash = (dl > 0 && dir[dl - 1] == '/') ? 0 : 1;
    need = dl + slash + nl + 1;
    if (need > cap) return 0;
    memcpy(out, dir, (size_t)dl);
    if (slash) out[dl++] = '/';
    memcpy(out + dl, name, (size_t)nl + 1u);
    return 1;
}

/* Parent directory of `dir` ("/" is its own parent).  Returns 0 when there is
 * no parent to show, e.g. a relative single-component path. */
static int ui_parent_path(const char *dir, char *out, int cap)
{
    int n;
    if (!dir || !out || cap <= 0) return 0;
    n = (int)strlen(dir);
    while (n > 1 && dir[n - 1] == '/') n--;
    while (n > 0 && dir[n - 1] != '/') n--;
    while (n > 1 && dir[n - 1] == '/') n--;
    if (n <= 0 || n >= cap) return 0;
    memcpy(out, dir, (size_t)n);
    out[n] = '\0';
    return 1;
}

static int ui_has_parent(const char *dir)
{
    char parent[UI_PATH_MAX];
    if (!ui_parent_path(dir, parent, (int)sizeof(parent))) return 0;
    return strcmp(parent, dir) != 0;
}

/* Directory test; non-POSIX builds only treat plain files as jars anyway. */
static int ui_is_dir(const char *path)
{
#if defined(_WIN32)
    (void)path;
    return 0;
#else
    struct stat st;
    if (!path || !path[0]) return 0;
    return stat(path, &st) == 0 && S_ISDIR(st.st_mode) ? 1 : 0;
#endif
}

/* A path that is worth handing to the browser.  On POSIX it has to exist and
 * be a directory; elsewhere any non-empty string is accepted. */
static int ui_path_usable(const char *path)
{
#if defined(_WIN32)
    return path && path[0] != '\0';
#else
    return ui_is_dir(path);
#endif
}

static const char *ui_games_dir(const nj_client_ui *ui)
{
    if (ui->settings && ui->settings->games_dir[0]) return ui->settings->games_dir;
    if (ui->lib && ui->lib->dir[0]) return ui->lib->dir;
    return "";
}

/* ------------------------------------------------------------------ */
/* UTF-8 decoding and fitted (ellipsised) text drawing                 */
/* ------------------------------------------------------------------ */
/*
 * The runtime's nj_gfx text calls take UTF-16, and the private helper in
 * src/core/nj_internal.h is not part of the client build, so the launcher
 * carries its own decoder.  Invalid sequences become U+FFFD and always
 * consume at least one byte, which keeps malformed settings or file names
 * from looping forever.
 */
static int ui_utf8_one(const unsigned char *p, nj_u4 *cp)
{
    unsigned c;
    int need, i;
    nj_u4 v;

    c = (unsigned)p[0];
    if (c < 0x80u) {
        *cp = (nj_u4)c;
        return 1;
    }
    if ((c & 0xE0u) == 0xC0u) {
        need = 1;
        v = (nj_u4)(c & 0x1Fu);
    } else if ((c & 0xF0u) == 0xE0u) {
        need = 2;
        v = (nj_u4)(c & 0x0Fu);
    } else if ((c & 0xF8u) == 0xF0u) {
        need = 3;
        v = (nj_u4)(c & 0x07u);
    } else {
        *cp = 0xFFFDu;
        return 1;
    }
    for (i = 1; i <= need; i++) {
        unsigned t = (unsigned)p[i];
        if ((t & 0xC0u) != 0x80u) {
            *cp = 0xFFFDu;
            return i;               /* resynchronise at the bad byte */
        }
        v = (v << 6) | (nj_u4)(t & 0x3Fu);
    }
    if ((need == 1 && v < 0x80u) || (need == 2 && v < 0x800u) ||
        (need == 3 && v < 0x10000u) || (v >= 0xD800u && v <= 0xDFFFu) ||
        v > 0x10FFFFu) {
        *cp = 0xFFFDu;
        return need + 1;
    }
    *cp = v;
    return need + 1;
}

static int ui_utf8_decode(const char *s, nj_u2 *out, int cap)
{
    const unsigned char *p = (const unsigned char *)s;
    int n = 0;
    if (!p || !out || cap <= 0) return 0;
    while (*p != 0u && n < cap) {
        nj_u4 cp;
        p += ui_utf8_one(p, &cp);
        if (cp >= 0x10000u) {
            if (n + 2 > cap) break;
            cp -= 0x10000u;
            out[n++] = (nj_u2)(0xD800u + (cp >> 10));
            out[n++] = (nj_u2)(0xDC00u + (cp & 0x3FFu));
        } else {
            out[n++] = (nj_u2)cp;
        }
    }
    return n;
}

/*
 * Fit UTF-16 text into `avail` pixels, replacing the dropped prefix/suffix
 * with "...".  right elision keeps the head (file names), left elision keeps
 * the tail (directory paths, where the last components are the useful part).
 * The renderer's advances are additive (no kerning), so cumulative widths are
 * exact and this stays linear.
 */
static int ui_fit_text(const nj_font *font, const nj_u2 *s, int n, int avail,
                       int left_elide, nj_u2 *out, int out_cap)
{
    int cum[UI_TEXT_MAX + 1];
    int i, total, ell;

    if (!font || !s || !out || n <= 0 || out_cap <= 0 || avail <= 0) return 0;
    if (n > UI_TEXT_MAX) n = UI_TEXT_MAX;
    cum[0] = 0;
    for (i = 0; i < n; i++) cum[i + 1] = cum[i] + nj_font_char_width(font, s[i]);
    total = cum[n];
    if (total <= avail) {
        if (n > out_cap) n = out_cap;
        for (i = 0; i < n; i++) out[i] = s[i];
        return n;
    }
    ell = 3 * nj_font_char_width(font, (nj_u2)'.');
    if (ell >= avail) return 0;
    if (left_elide) {
        int start = 0;
        while (start < n && total - cum[start] + ell > avail) start++;
        while (start < n && s[start] >= 0xDC00u && s[start] <= 0xDFFFu) start++;
        if (start >= n || (n - start) + 3 > out_cap) return 0;
        out[0] = (nj_u2)'.';
        out[1] = (nj_u2)'.';
        out[2] = (nj_u2)'.';
        for (i = 0; i < n - start; i++) out[3 + i] = s[start + i];
        return 3 + (n - start);
    }
    {
        int k = n;
        while (k > 0) {
            if (s[k - 1] >= 0xD800u && s[k - 1] <= 0xDBFFu) {
                k--;                /* never split a surrogate pair */
                continue;
            }
            if (cum[k] + ell <= avail) break;
            k--;
        }
        if (k <= 0 || k + 3 > out_cap) return 0;
        for (i = 0; i < k; i++) out[i] = s[i];
        out[k] = (nj_u2)'.';
        out[k + 1] = (nj_u2)'.';
        out[k + 2] = (nj_u2)'.';
        return k + 3;
    }
}

static int ui_text_width(const nj_font *font, const char *utf8)
{
    nj_u2 raw[UI_TEXT_MAX];
    int n;
    if (!font || !utf8) return 0;
    n = ui_utf8_decode(utf8, raw, (int)(sizeof(raw) / sizeof(raw[0])));
    return n > 0 ? nj_font_string_width(font, raw, n) : 0;
}

/*
 * Draw one UTF-8 string.  avail > 0 limits the width and ellipsises the
 * remainder; avail <= 0 draws the whole string.  The clip rectangle is the
 * caller's responsibility, as everywhere else in this file.
 */
static void ui_draw_text(nj_gfx *g, const char *utf8, const nj_font *font,
                         nj_u4 color, int x, int y, int anchor,
                         int avail, int left_elide)
{
    nj_u2 raw[UI_TEXT_MAX];
    nj_u2 fitted[UI_DRAW_MAX];
    int n, m;

    if (!g || !font || !utf8) return;
    n = ui_utf8_decode(utf8, raw, (int)(sizeof(raw) / sizeof(raw[0])));
    if (n <= 0) return;
    if (avail > 0) {
        m = ui_fit_text(font, raw, n, avail, left_elide, fitted,
                        (int)(sizeof(fitted) / sizeof(fitted[0])));
        if (m <= 0) return;
    } else {
        memcpy(fitted, raw, (size_t)n * sizeof(fitted[0]));
        m = n;
    }
    nj_gfx_set_font(g, font);
    nj_gfx_set_color(g, color);
    nj_gfx_draw_text(g, fitted, m, x, y, anchor);
}

static const nj_font *ui_font_large(void)
{
    return nj_font_builtin(NJ_FONT_SIZE_LARGE, NJ_FONT_STYLE_PLAIN);
}

static const nj_font *ui_font_medium(void)
{
    return nj_font_builtin(NJ_FONT_SIZE_MEDIUM, NJ_FONT_STYLE_PLAIN);
}

static const nj_font *ui_font_small(void)
{
    return nj_font_builtin(NJ_FONT_SIZE_SMALL, NJ_FONT_STYLE_PLAIN);
}

/* ------------------------------------------------------------------ */
/* Layout                                                              */
/* ------------------------------------------------------------------ */
static void ui_clip_full(const nj_client_ui *ui, nj_gfx *g)
{
    nj_gfx_set_clip(g, 0, 0, ui->width, ui->height);
}

static void ui_layout_apply(nj_client_ui *ui, int width, int height)
{
    int header, footer, list_h;

    if (!ui) return;
    if (width < 1) width = 1;
    if (height < 1) height = 1;
    if (height >= 480) {
        header = UI_HEADER_H;
        footer = UI_FOOTER_H;
    } else if (height >= 360) {
        header = 48;
        footer = 36;
    } else {
        header = 42;
        footer = 30;
    }
    /* On very short windows the chrome must not eat the whole list. */
    if (header + footer + UI_SEARCH_H > height - 24) {
        header = ui_max(24, height / 8);
        footer = ui_max(20, height / 10);
    }
    list_h = height - header - footer - UI_SEARCH_H;
    if (list_h < 0) list_h = 0;

    ui->width = width;
    ui->height = height;
    ui->header_h = header;
    ui->footer_h = footer;
    ui->footer_y = height - footer;
    ui->list_y = header + UI_SEARCH_H;
    ui->list_h = list_h;
    ui->row_h = UI_ROW_H;
    ui->visible_rows = list_h > 0 ? list_h / UI_ROW_H : 0;
}

/* ------------------------------------------------------------------ */
/* Library view state                                                  */
/* ------------------------------------------------------------------ */
static int ui_lib_count(const nj_client_ui *ui)
{
    int n;
    if (!ui || !ui->lib || !ui->lib->entries) return 0;
    n = ui->lib->count;
    if (n <= 0) return 0;
    if (ui->lib->cap > 0 && n > ui->lib->cap) n = ui->lib->cap;
    return n;
}

static int ui_scan_busy(const nj_client_ui *ui)
{
    if (!ui || !ui->lib) return 0;
    return nj_lib_scan_busy(ui->lib) ? 1 : 0;
}

/* ---- LIBRARY search -------------------------------------------------
 * The filter is a UTF-8 byte string.  Matching folds ASCII case and compares
 * every other byte exactly, which is correct UTF-8 substring matching without
 * a decoder: a needle never starts mid-character, so a byte match is a
 * character match.  Only the library list is filtered; browser, settings and
 * binds rows never look at this state. */
static int ui_filter_active(const nj_client_ui *ui)
{
    return (ui && ui->filter_len > 0) ? 1 : 0;
}

static int ui_filter_byte_eq(unsigned char a, unsigned char b)
{
    if (a >= (unsigned char)'A' && a <= (unsigned char)'Z') {
        a = (unsigned char)(a - (unsigned char)'A' + (unsigned char)'a');
    }
    if (b >= (unsigned char)'A' && b <= (unsigned char)'Z') {
        b = (unsigned char)(b - (unsigned char)'A' + (unsigned char)'a');
    }
    return a == b ? 1 : 0;
}

static int ui_filter_contains(const char *hay, const char *needle, int nlen)
{
    int hlen, i, j;

    if (!hay || !needle || nlen <= 0) return 0;
    hlen = (int)strlen(hay);
    if (nlen > hlen) return 0;
    for (i = 0; i + nlen <= hlen; i++) {
        for (j = 0; j < nlen; j++) {
            if (!ui_filter_byte_eq((unsigned char)hay[i + j],
                                   (unsigned char)needle[j])) {
                break;
            }
        }
        if (j == nlen) return 1;
    }
    return 0;
}

static int ui_entry_matches(const nj_client_ui *ui, int index)
{
    const nj_lib_entry *e;

    if (!ui || !ui->lib || !ui->lib->entries) return 0;
    if (index < 0 || index >= ui_lib_count(ui)) return 0;
    if (!ui_filter_active(ui)) return 1;
    e = &ui->lib->entries[index];
    return ui_filter_contains(e->name, ui->filter, ui->filter_len) ||
           ui_filter_contains(e->file, ui->filter, ui->filter_len) ||
           ui_filter_contains(e->midlet, ui->filter, ui->filter_len);
}

static int ui_filter_count(const nj_client_ui *ui)
{
    int i, n, count = 0;

    n = ui_lib_count(ui);
    for (i = 0; i < n; i++) {
        if (ui_entry_matches(ui, i)) count++;
    }
    return count;
}

/* First matching entry index, or -1 when nothing matches. */
static int ui_filter_first(const nj_client_ui *ui)
{
    int i, n = ui_lib_count(ui);

    for (i = 0; i < n; i++) {
        if (ui_entry_matches(ui, i)) return i;
    }
    return -1;
}

/* Position of an entry in the filtered view (-1 when it is filtered out). */
static int ui_filter_pos_of(const nj_client_ui *ui, int index)
{
    int i, n, pos = 0;

    if (!ui_filter_active(ui)) return index;
    n = ui_lib_count(ui);
    for (i = 0; i < n; i++) {
        if (!ui_entry_matches(ui, i)) continue;
        if (i == index) return pos;
        pos++;
    }
    return -1;
}

/* Entry at a filtered position, or -1. */
static int ui_filter_entry_at(const nj_client_ui *ui, int pos)
{
    int i, n, p = 0;

    if (!ui_filter_active(ui)) {
        return (pos >= 0 && pos < ui_lib_count(ui)) ? pos : -1;
    }
    if (pos < 0) return -1;
    n = ui_lib_count(ui);
    for (i = 0; i < n; i++) {
        if (!ui_entry_matches(ui, i)) continue;
        if (p == pos) return i;
        p++;
    }
    return -1;
}

/* ---- LIBRARY sections: 最近玩过 --------------------------------------
 * With an empty search and at least one recent jar that still resolves
 * through the library, the list shows two non-selectable headers: the
 * resolved recent entries (newest first, each path once) and then every
 * entry.  Headers take UI_SECTION_H pixels of list space and scroll with the
 * content.  library_top and the selection are indices over SELECTABLE rows
 * (headers never count), so row_rect(), clicks and keys stay one row per
 * game. */
static void ui_lib_clamp(nj_client_ui *ui);

/* Resolve settings->recent through the library, newest first, deduplicated;
 * writes entry indices and returns how many resolve. */
static int ui_recent_resolved(const nj_client_ui *ui,
                              int out[NJ_CLIENT_RECENT_MAX])
{
    int i, n = 0, j;

    if (!ui || !ui->lib || !ui->lib->entries || !ui->settings) return 0;
    for (i = 0; i < ui->settings->recent_count && i < NJ_CLIENT_RECENT_MAX;
         i++) {
        const nj_lib_entry *e;
        int index;
        if (!ui->settings->recent[i][0]) continue;
        e = nj_library_find_path(ui->lib, ui->settings->recent[i]);
        if (!e) continue;
        index = (int)(e - ui->lib->entries);
        if (index < 0 || index >= ui_lib_count(ui)) continue;
        for (j = 0; j < n; j++) {
            if (out[j] == index) break;
        }
        if (j < n) continue;          /* the same jar listed twice */
        out[n++] = index;
    }
    return n;
}

/* Recent rows while the section is up; 0 while searching, with an empty
 * library, or when no recent path resolves any more. */
static int ui_recent_row_count(const nj_client_ui *ui)
{
    int tmp[NJ_CLIENT_RECENT_MAX];

    if (!ui || ui_filter_active(ui) || ui_lib_count(ui) <= 0) return 0;
    return ui_recent_resolved(ui, tmp);
}

/* Selectable rows: the flat/filtered list, or recent rows + every entry. */
static int ui_view_rows(const nj_client_ui *ui)
{
    if (!ui) return 0;
    if (ui_filter_active(ui)) return ui_filter_count(ui);
    return ui_recent_row_count(ui) + ui_lib_count(ui);
}

/* Entry index for a selectable row, or -1. */
static int ui_view_entry_at(const nj_client_ui *ui, int row)
{
    int n;

    if (!ui || row < 0) return -1;
    if (ui_filter_active(ui)) return ui_filter_entry_at(ui, row);
    n = ui_recent_row_count(ui);
    if (n > 0) {
        if (row < n) {
            int tmp[NJ_CLIENT_RECENT_MAX];
            ui_recent_resolved(ui, tmp);
            return tmp[row];
        }
        row -= n;
    }
    return row < ui_lib_count(ui) ? row : -1;
}

/* Row y offset from list_y, section headers included (they scroll). */
static int ui_view_y(const nj_client_ui *ui, int row)
{
    int n = ui_recent_row_count(ui);

    if (!ui || n <= 0) return row * UI_ROW_H;
    if (row < n) return UI_SECTION_H + row * ui->row_h;
    return UI_SECTION_H + n * ui->row_h + UI_SECTION_H +
           (row - n) * ui->row_h;
}

/* Content offset for a scroll position: the 最近玩过 header shows only at the
 * very top of the list; scrolling past it moves the headers away with the
 * rows (they are not sticky). */
static int ui_view_base(const nj_client_ui *ui, int top)
{
    if (!ui) return 0;
    if (top <= 0 && ui_recent_row_count(ui) > 0) return 0;
    return ui_view_y(ui, top);
}

/* Whole selectable rows visible when `top` is the first one. */
static int ui_view_visible_rows(const nj_client_ui *ui, int top)
{
    int rows, r, base, count = 0;

    if (!ui) return 0;
    rows = ui_view_rows(ui);
    if (rows <= 0) return 0;
    base = ui_view_base(ui, top);
    for (r = top; r < rows && count <= ui->visible_rows + 2; r++) {
        if (ui_view_y(ui, r) - base + ui->row_h > ui->list_h) break;
        count++;
    }
    return count > 0 ? count : 1;
}

/* Largest top that still keeps the last row inside the viewport. */
static int ui_view_max_top(const nj_client_ui *ui)
{
    int rows, top, last_y;

    if (!ui) return 0;
    rows = ui_view_rows(ui);
    if (rows <= 0 || ui->row_h <= 0 || ui->list_h <= 0) return 0;
    last_y = ui_view_y(ui, rows - 1);
    top = rows - 1;
    while (top > 0 && last_y - ui_view_base(ui, top) + ui->row_h > ui->list_h) {
        top--;
    }
    return top;
}

/* Row of the current selection, or -1 when it is filtered out/gone. */
static int ui_view_row_of_sel(const nj_client_ui *ui)
{
    int n;

    if (!ui || ui->library_sel < 0 || ui->library_sel >= ui_lib_count(ui)) {
        return -1;
    }
    if (ui_filter_active(ui)) return ui_filter_pos_of(ui, ui->library_sel);
    n = ui_recent_row_count(ui);
    if (n > 0) {
        if (ui->library_sel_recent) {
            int tmp[NJ_CLIENT_RECENT_MAX], i, found = -1;
            ui_recent_resolved(ui, tmp);
            for (i = 0; i < n; i++) {
                if (tmp[i] == ui->library_sel) {
                    found = i;
                    break;
                }
            }
            if (found >= 0) return found;
            /* The recent entry drifted out of the section: the same jar's
             * full-list row keeps the selection. */
        }
        return ui->library_sel + n;
    }
    return ui->library_sel;
}

/* Put the selection on a selectable row (headers are skipped by definition). */
static void ui_lib_select_row(nj_client_ui *ui, int row)
{
    int rows, entry, n;

    if (!ui) return;
    rows = ui_view_rows(ui);
    if (rows <= 0) {
        ui->library_sel = -1;
        ui->library_sel_recent = 0;
        ui_lib_clamp(ui);
        return;
    }
    row = ui_clamp_int(row, 0, rows - 1);
    entry = ui_view_entry_at(ui, row);
    if (entry < 0) return;
    n = ui_recent_row_count(ui);
    ui->library_sel = entry;
    ui->library_sel_recent = (n > 0 && row < n) ? 1 : 0;
    ui_lib_clamp(ui);
}

static void ui_lib_clamp(nj_client_ui *ui)
{
    int rows, selrow, top, max_top;

    if (!ui) return;
    rows = ui_view_rows(ui);
    if (rows <= 0) {
        ui->library_sel = -1;
        ui->library_sel_recent = 0;
        ui->library_top = 0;
        return;
    }
    selrow = ui_view_row_of_sel(ui);
    if (selrow < 0) {
        int n = ui_recent_row_count(ui);
        selrow = 0;
        ui->library_sel = ui_view_entry_at(ui, 0);
        ui->library_sel_recent = n > 0 ? 1 : 0;
        if (ui->library_sel < 0) {
            ui->library_sel = 0;
            ui->library_sel_recent = 0;
        }
    }
    max_top = ui_view_max_top(ui);
    top = ui_clamp_int(ui->library_top, 0, max_top);
    if (selrow < top) top = selrow;
    while (top < selrow &&
           ui_view_y(ui, selrow) - ui_view_base(ui, top) + ui->row_h >
               ui->list_h) {
        top++;
    }
    ui->library_top = top;
}

static void ui_lib_move(nj_client_ui *ui, int delta)
{
    int rows, row;

    if (!ui || delta == 0) return;
    rows = ui_view_rows(ui);
    if (rows <= 0) {
        ui->library_sel = -1;
        ui->library_sel_recent = 0;
        return;
    }
    row = ui_view_row_of_sel(ui);
    if (row < 0) row = 0;
    row = ui_clamp_int(row + ui_clamp_int(delta, -1000, 1000), 0, rows - 1);
    ui_lib_select_row(ui, row);
}

/* After a filter edit: the first match is the new selection (or -1) and the
 * view starts at the top of the result list. */
static void ui_filter_sync(nj_client_ui *ui)
{
    if (!ui) return;
    ui->library_sel = ui_filter_first(ui);
    ui->library_sel_recent = 0;
    ui->library_top = 0;
    ui_lib_clamp(ui);
}

/* Append one Unicode code point as UTF-8.  A code point that would not fit
 * whole is dropped rather than half-written. */
static void ui_filter_append(nj_client_ui *ui, nj_u2 ch)
{
    unsigned char enc[3];
    int n, i;

    if (!ui || ch < 0x20u || ch == 0x7Fu) return;
    if (ch < 0x80u) {
        enc[0] = (unsigned char)ch;
        n = 1;
    } else if (ch < 0x800u) {
        enc[0] = (unsigned char)(0xC0u | (nj_u2)(ch >> 6));
        enc[1] = (unsigned char)(0x80u | (nj_u2)(ch & 0x3Fu));
        n = 2;
    } else {
        enc[0] = (unsigned char)(0xE0u | (nj_u2)(ch >> 12));
        enc[1] = (unsigned char)(0x80u | (nj_u2)((ch >> 6) & 0x3Fu));
        enc[2] = (unsigned char)(0x80u | (nj_u2)(ch & 0x3Fu));
        n = 3;
    }
    if (ui->filter_len + n > UI_FILTER_MAX) return;
    for (i = 0; i < n; i++) ui->filter[ui->filter_len + i] = (char)enc[i];
    ui->filter_len += n;
    ui->filter[ui->filter_len] = '\0';
    ui_filter_sync(ui);
}

/* Remove the last code point; returns 1 when something was removed. */
static int ui_filter_backspace(nj_client_ui *ui)
{
    int i;

    if (!ui || ui->filter_len <= 0) return 0;
    i = ui->filter_len - 1;
    while (i > 0 && ((unsigned char)ui->filter[i] & 0xC0u) == 0x80u) i--;
    ui->filter_len = i;
    ui->filter[ui->filter_len] = '\0';
    ui_filter_sync(ui);
    return 1;
}

static void ui_filter_clear(nj_client_ui *ui)
{
    if (!ui) return;
    ui->filter_len = 0;
    ui->filter[0] = '\0';
    ui_lib_clamp(ui);
}


/* ------------------------------------------------------------------ */
/* Toast, action and mode helpers                                      */
/* ------------------------------------------------------------------ */
static void ui_note_set(nj_client_ui *ui, const char *message)
{
    int n;
    if (!ui || !message) return;
    n = (int)strlen(message);
    if (n > (int)sizeof(ui->toast) - 1) {
        n = (int)sizeof(ui->toast) - 1;
        /* Do not leave half a multi-byte sequence at the cut. */
        while (n > 0 && ((unsigned char)ui->toast[n] & 0xC0u) == 0x80u) n--;
        memcpy(ui->toast, message, (size_t)n);
        ui->toast[n] = '\0';
    } else {
        memcpy(ui->toast, message, (size_t)n + 1u);
    }
    ui->toast_start = ui->now_ms;
    ui->toast_active = 1;
}

static void ui_save_settings(nj_client_ui *ui)
{
    if (!ui || !ui->settings) return;
    (void)nj_client_settings_save(ui->settings, NULL);
}

static void ui_emit_run(nj_client_ui *ui, const char *path)
{
    if (!ui || !path || !path[0]) return;
    if (ui->quit) return;
    if (!ui_copy(ui->action_jar, (int)sizeof(ui->action_jar), path)) {
        ui_note_set(ui, "路径太长，无法运行");
        return;
    }
    ui->action = NJ_CLIENT_ACTION_RUN;
}

static void ui_request_quit(nj_client_ui *ui)
{
    if (!ui) return;
    ui->quit = 1;
    ui->action = NJ_CLIENT_ACTION_QUIT;
}

/* ------------------------------------------------------------------ */
/* Browser view state                                                  */
/* ------------------------------------------------------------------ */
static int ui_browser_path_h(const nj_client_ui *ui)
{
    return ui->height >= 320 ? 40 : 30;
}

static void ui_browser_geom(const nj_client_ui *ui, int *rows_y, int *rows_h,
                            int *vis)
{
    int path_h, ry, rh;
    path_h = ui_browser_path_h(ui);
    ry = ui->header_h + path_h;
    rh = ui->height - ry - ui->footer_h;
    if (rh < 0) rh = 0;
    if (rows_y) *rows_y = ry;
    if (rows_h) *rows_h = rh;
    if (vis) *vis = rh > 0 ? rh / UI_BROW_ROW_H : 0;
}

static int ui_browser_row_count(const nj_client_ui *ui)
{
    int n;
    if (!ui) return 0;
    n = ui->browser_entries ? ui->browser_count : 0;
    if (n < 0) n = 0;
    return (ui->browser_up ? 1 : 0) + n;
}

/* Row 0 is ".." when a parent exists, then the entries in display order. */
typedef struct ui_browser_row {
    int is_up;
    int is_dir;
    const char *name;
    nj_u4 size;
} ui_browser_row;

static int ui_browser_row_at(const nj_client_ui *ui, int index,
                             ui_browser_row *row)
{
    int i;
    if (!ui || !row) return 0;
    if (index < 0 || index >= ui_browser_row_count(ui)) return 0;
    row->is_up = 0;
    row->is_dir = 0;
    row->name = "";
    row->size = 0;
    if (ui->browser_up) {
        if (index == 0) {
            row->is_up = 1;
            row->is_dir = 1;
            row->name = "..";
            return 1;
        }
        index--;
    }
    i = index;
    if (i < 0 || i >= ui->browser_count || !ui->browser_entries) return 0;
    row->is_dir = ui->browser_entries[i].is_dir ? 1 : 0;
    row->name = ui->browser_entries[i].name;
    row->size = ui->browser_entries[i].size;
    return 1;
}

/* Directories before files, then alphabetical; the listing order of the
 * directory backend is not part of its contract. */
static int ui_browser_compare(const nj_lib_dir_entry *a,
                              const nj_lib_dir_entry *b)
{
    if (a->is_dir != b->is_dir) return a->is_dir ? -1 : 1;
    return strcmp(a->name, b->name);
}

static void ui_browser_sort(nj_lib_dir_entry *list, int count)
{
    int i, j;
    if (!list || count < 2) return;
    for (i = 1; i < count; i++) {
        nj_lib_dir_entry tmp = list[i];
        j = i - 1;
        while (j >= 0 && ui_browser_compare(&list[j], &tmp) > 0) {
            list[j + 1] = list[j];
            j--;
        }
        list[j + 1] = tmp;
    }
}

static void ui_browser_free(nj_client_ui *ui)
{
    if (!ui) return;
    if (ui->browser_entries) {
        nj_lib_free_list(ui->browser_entries);
        ui->browser_entries = NULL;
    }
    ui->browser_count = 0;
}

static void ui_browser_clamp(nj_client_ui *ui)
{
    int n, vis, max_top, rows_y, rows_h;

    if (!ui) return;
    ui_browser_geom(ui, &rows_y, &rows_h, &vis);
    (void)rows_y;
    (void)rows_h;
    n = ui_browser_row_count(ui);
    ui->browser_visible = vis;
    if (n <= 0) {
        ui->browser_sel = -1;
        ui->browser_top = 0;
        return;
    }
    ui->browser_sel = ui_clamp_int(ui->browser_sel, 0, n - 1);
    if (vis <= 0) {
        ui->browser_top = 0;
        return;
    }
    max_top = n > vis ? n - vis : 0;
    ui->browser_top = ui_clamp_int(ui->browser_top, 0, max_top);
    if (ui->browser_sel < ui->browser_top) ui->browser_top = ui->browser_sel;
    if (ui->browser_sel >= ui->browser_top + vis) {
        ui->browser_top = ui->browser_sel - vis + 1;
    }
    if (ui->browser_top < 0) ui->browser_top = 0;
}

static void ui_browser_set(nj_client_ui *ui, int index)
{
    if (!ui || ui_browser_row_count(ui) <= 0) return;
    ui->browser_sel = index;
    ui_browser_clamp(ui);
}

static void ui_browser_move(nj_client_ui *ui, int delta)
{
    int n, sel;
    if (!ui || delta == 0) return;
    n = ui_browser_row_count(ui);
    if (n <= 0) return;
    sel = ui->browser_sel < 0 ? 0 : ui->browser_sel;
    delta = ui_clamp_int(delta, -1000, 1000);
    ui->browser_sel = ui_clamp_int(sel + delta, 0, n - 1);
    ui_browser_clamp(ui);
}

static void ui_browser_reload(nj_client_ui *ui)
{
    nj_lib_dir_entry *list = NULL;
    int count = 0;

    if (!ui) return;
    ui_browser_free(ui);
    if (!ui->browser_dir[0]) {
        if (!ui_copy(ui->browser_dir, (int)sizeof(ui->browser_dir), ".")) {
            ui->browser_dir[0] = '\0';
        }
    }
    if (ui->browser_dir[0]) {
        (void)nj_lib_list_dir(ui->browser_dir, &list, &count);
    }
    if (!list || count <= 0) {
        if (list) nj_lib_free_list(list);
        list = NULL;
        count = 0;
    }
    ui_browser_sort(list, count);
    ui->browser_entries = list;
    ui->browser_count = count;
    ui->browser_up = ui_has_parent(ui->browser_dir) ? 1 : 0;
    ui->browser_sel = 0;
    ui->browser_top = 0;
    ui_browser_clamp(ui);
}

static void ui_browser_open(nj_client_ui *ui, int pick_dir, int return_mode)
{
    char dir[UI_PATH_MAX];

    if (!ui) return;
    dir[0] = '\0';
    if (ui->settings) {
        if (ui->settings->last_dir[0] &&
            ui_path_usable(ui->settings->last_dir)) {
            (void)ui_copy(dir, (int)sizeof(dir), ui->settings->last_dir);
        } else if (ui->settings->games_dir[0] &&
                   ui_path_usable(ui->settings->games_dir)) {
            (void)ui_copy(dir, (int)sizeof(dir), ui->settings->games_dir);
        }
    }
    if (!dir[0] && ui->lib && ui->lib->dir[0] &&
        ui_path_usable(ui->lib->dir)) {
        (void)ui_copy(dir, (int)sizeof(dir), ui->lib->dir);
    }
    if (!dir[0]) (void)ui_copy(dir, (int)sizeof(dir), ".");

    ui->browser_pick_dir = pick_dir ? 1 : 0;
    ui->browser_return_mode = return_mode;
    (void)ui_copy(ui->browser_dir, (int)sizeof(ui->browser_dir), dir);
    ui_browser_reload(ui);
    ui->mode = NJ_CLIENT_MODE_BROWSER;
}

/* Leave the browser, remembering the directory for the next visit. */
static void ui_browser_leave(nj_client_ui *ui, int return_mode)
{
    if (!ui) return;
    if (return_mode != NJ_CLIENT_MODE_LIBRARY &&
        return_mode != NJ_CLIENT_MODE_SETTINGS) {
        return_mode = NJ_CLIENT_MODE_LIBRARY;
    }
    if (ui->settings && ui->browser_dir[0]) {
        (void)ui_copy(ui->settings->last_dir,
                      (int)sizeof(ui->settings->last_dir), ui->browser_dir);
    }
    ui_browser_free(ui);
    ui->mode = return_mode;
    if (ui->mode == NJ_CLIENT_MODE_LIBRARY) ui_lib_clamp(ui);
    else ui_settings_clamp(ui);
}

static void ui_browser_go_up(nj_client_ui *ui)
{
    char parent[UI_PATH_MAX];
    if (!ui || !ui->browser_up) return;
    if (!ui_parent_path(ui->browser_dir, parent, (int)sizeof(parent))) return;
    if (strcmp(parent, ui->browser_dir) == 0) return;
    if (!ui_copy(ui->browser_dir, (int)sizeof(ui->browser_dir), parent)) return;
    ui_browser_reload(ui);
}

static void ui_browser_descend(nj_client_ui *ui, const char *name)
{
    char next[UI_PATH_MAX];
    if (!ui || !name || !name[0]) return;
    if (!ui_join_path(next, (int)sizeof(next), ui->browser_dir, name)) return;
    if (!ui_copy(ui->browser_dir, (int)sizeof(ui->browser_dir), next)) return;
    ui_browser_reload(ui);
}

static void ui_browser_activate(nj_client_ui *ui)
{
    ui_browser_row row;
    char path[UI_PATH_MAX];

    if (!ui || !ui_browser_row_at(ui, ui->browser_sel, &row)) return;
    if (row.is_up) {
        ui_browser_go_up(ui);
        return;
    }
    if (row.is_dir) {
        ui_browser_descend(ui, row.name);
        return;
    }
    if (ui->browser_pick_dir) {
        ui_note_set(ui, "请先进入目录，再按 P 使用当前目录");
        return;
    }
    if (!ui_join_path(path, (int)sizeof(path), ui->browser_dir, row.name)) {
        ui_note_set(ui, "路径太长，无法运行");
        return;
    }
    if (ui->lib) {
        nj_lib_entry entry;
        memset(&entry, 0, sizeof(entry));
        if (!nj_lib_probe(path, &entry) || !entry.valid) {
            ui_note_set(ui, "不是可运行的 .jar");
            return;
        }
        (void)nj_lib_add(ui->lib, path);
    }
    if (ui->settings && ui->browser_dir[0]) {
        (void)ui_copy(ui->settings->last_dir,
                      (int)sizeof(ui->settings->last_dir), ui->browser_dir);
    }
    ui_emit_run(ui, path);
    ui_browser_free(ui);
    ui->mode = NJ_CLIENT_MODE_LIBRARY;
    ui_lib_clamp(ui);
}

/* Adopt the current browser directory as the games directory. */
static void ui_browser_pick(nj_client_ui *ui)
{
    char dir[UI_PATH_MAX];
    char msg[UI_TOAST_MAX];
    nj_client_settings *s;

    if (!ui || !ui->browser_pick_dir) return;
    s = ui->settings;
    if (!ui_copy(dir, (int)sizeof(dir), ui->browser_dir)) return;
    if (!s) {
        ui_browser_leave(ui, NJ_CLIENT_MODE_LIBRARY);
        return;
    }
    if (!ui_copy(s->games_dir, (int)sizeof(s->games_dir), dir)) {
        ui_note_set(ui, "目录路径太长，无法使用");
        return;
    }
    (void)ui_copy(s->last_dir, (int)sizeof(s->last_dir), dir);
    ui_save_settings(ui);
    if (ui->lib) (void)nj_lib_scan_begin(ui->lib, dir);
    ui->library_sel = 0;
    ui->library_top = 0;
    if (snprintf(msg, sizeof(msg), "游戏目录：%s", dir) > 0) {
        ui_note_set(ui, msg);
    }
    ui_browser_leave(ui, NJ_CLIENT_MODE_LIBRARY);
}

/* ------------------------------------------------------------------ */
/* Settings view state                                                 */
/* ------------------------------------------------------------------ */
/*
 * The rows, in drawing order.  Left/Right cycles every value except the games
 * directory (Enter opens the picker), the key-map row (Enter opens the bind
 * page) and the reset row (Enter restores everything).  The order is also
 * what the input handlers and the tests rely on.
 */
enum {
    UI_SET_ROW_GAMES = 0,
    UI_SET_ROW_SCALE,
    UI_SET_ROW_FILTER,
    UI_SET_ROW_FULLSCREEN,
    UI_SET_ROW_ACCEL,
    UI_SET_ROW_GAMEPAD,
    UI_SET_ROW_KEYS,
    UI_SET_ROW_FPS,
    UI_SET_ROW_BINDS,
    UI_SET_ROW_HEAP,
    UI_SET_ROW_MUTE,
    UI_SET_ROW_NETWORK,
    UI_SET_ROW_RESET
};

static const char *ui_setting_label(int row)
{
    switch (row) {
    case UI_SET_ROW_GAMES: return "游戏目录";
    case UI_SET_ROW_SCALE: return "画面缩放";
    case UI_SET_ROW_FILTER: return "缩放滤镜";
    case UI_SET_ROW_FULLSCREEN: return "全屏";
    case UI_SET_ROW_ACCEL: return "渲染加速";
    case UI_SET_ROW_GAMEPAD: return "手柄";
    case UI_SET_ROW_KEYS: return "屏幕键盘";
    case UI_SET_ROW_FPS: return "帧率上限";
    case UI_SET_ROW_BINDS: return "按键映射";
    case UI_SET_ROW_HEAP: return "堆大小";
    case UI_SET_ROW_MUTE: return "静音";
    case UI_SET_ROW_NETWORK: return "网络";
    default: return "恢复默认";
    }
}

static int ui_setting_count(void)
{
    return UI_SET_ROW_RESET + 1;
}

/* The heap row is a fixed ladder; a saved value between rungs snaps down so
 * the panel never shows something Left/Right cannot return to. */
static int ui_heap_index(int heap_mb)
{
    if (heap_mb <= 0) return 0;
    if (heap_mb <= 8) return 1;
    if (heap_mb <= 16) return 2;
    if (heap_mb <= 32) return 3;
    return 4;
}

static void ui_settings_value(const nj_client_ui *ui, int row, char *out,
                              int cap)
{
    const nj_client_settings *s;
    if (!out || cap <= 0) return;
    out[0] = '\0';
    if (!ui || !(s = ui->settings)) return;
    switch (row) {
    case UI_SET_ROW_GAMES:
        (void)ui_copy(out, cap, s->games_dir);
        break;
    case UI_SET_ROW_SCALE:
        if (s->game_scale >= 1 && s->game_scale <= 4) {
            snprintf(out, (size_t)cap, "%dx", s->game_scale);
        } else {
            snprintf(out, (size_t)cap, "自动");
        }
        break;
    case UI_SET_ROW_FILTER:
        snprintf(out, (size_t)cap, "%s", s->filter_linear ? "线性" : "最近邻");
        break;
    case UI_SET_ROW_FULLSCREEN:
        snprintf(out, (size_t)cap, "%s", s->fullscreen ? "开" : "关");
        break;
    case UI_SET_ROW_ACCEL:
        snprintf(out, (size_t)cap, "%s", s->accel ? "加速" : "软件");
        break;
    case UI_SET_ROW_GAMEPAD:
        snprintf(out, (size_t)cap, "%s", s->gamepad ? "开" : "关");
        break;
    case UI_SET_ROW_KEYS:
        snprintf(out, (size_t)cap, "%s", s->keys_visible ? "开" : "关");
        break;
    case UI_SET_ROW_FPS:
        if (s->fps_cap > 0) snprintf(out, (size_t)cap, "%d", s->fps_cap);
        else snprintf(out, (size_t)cap, "不限");
        break;
    case UI_SET_ROW_BINDS:
        snprintf(out, (size_t)cap, "%d 项", NJ_PORT_BIND_COUNT);
        break;
    case UI_SET_ROW_HEAP:
        if (s->heap_mb > 0) snprintf(out, (size_t)cap, "%d MB", s->heap_mb);
        else snprintf(out, (size_t)cap, "自动");
        break;
    case UI_SET_ROW_MUTE:
        snprintf(out, (size_t)cap, "%s", s->mute ? "静音" : "关");
        break;
    case UI_SET_ROW_NETWORK:
        snprintf(out, (size_t)cap, "%s", s->network ? "开" : "关");
        break;
    default:
        break;
    }
}

static void ui_settings_clamp(nj_client_ui *ui)
{
    int count, vis, max_top;
    if (!ui) return;
    count = ui_setting_count();
    vis = ui->settings_visible > 0 ? ui->settings_visible : count;
    if (vis > count) vis = count;
    ui->settings_row = ui_clamp_int(ui->settings_row, 0, count - 1);
    if (ui->settings_top < 0) ui->settings_top = 0;
    max_top = count > vis ? count - vis : 0;
    ui->settings_top = ui_clamp_int(ui->settings_top, 0, max_top);
    if (ui->settings_row < ui->settings_top) {
        ui->settings_top = ui->settings_row;
    }
    if (ui->settings_row >= ui->settings_top + vis) {
        ui->settings_top = ui->settings_row - vis + 1;
    }
    if (ui->settings_top < 0) ui->settings_top = 0;
}

static void ui_settings_set(nj_client_ui *ui, int row)
{
    if (!ui) return;
    ui->settings_row = row;
    ui_settings_clamp(ui);
}

static void ui_settings_move(nj_client_ui *ui, int delta)
{
    if (!ui || delta == 0) return;
    ui_settings_set(ui,
                    ui_clamp_int(ui->settings_row + delta, 0,
                                 ui_setting_count() - 1));
}

static void ui_settings_cycle(nj_client_ui *ui, int dir)
{
    nj_client_settings *s;
    if (!ui || !(s = ui->settings)) return;
    dir = dir > 0 ? 1 : -1;
    switch (ui->settings_row) {
    case UI_SET_ROW_SCALE:
        s->game_scale += dir;
        if (s->game_scale < 0) s->game_scale = 4;
        if (s->game_scale > 4) s->game_scale = 0;
        break;
    case UI_SET_ROW_FILTER:
        s->filter_linear = s->filter_linear ? 0 : 1;
        break;
    case UI_SET_ROW_FULLSCREEN:
        s->fullscreen = s->fullscreen ? 0 : 1;
        break;
    case UI_SET_ROW_ACCEL:
        s->accel = s->accel ? 0 : 1;
        break;
    case UI_SET_ROW_GAMEPAD:
        s->gamepad = s->gamepad ? 0 : 1;
        break;
    case UI_SET_ROW_KEYS:
        s->keys_visible = s->keys_visible ? 0 : 1;
        break;
    case UI_SET_ROW_FPS: {
        int idx;
        if (s->fps_cap == 0) idx = 0;
        else if (s->fps_cap == 30) idx = 1;
        else if (s->fps_cap == 60) idx = 2;
        else idx = -1;
        if (idx < 0) idx = dir > 0 ? 0 : 2;
        else idx = (idx + dir + 3) % 3;
        s->fps_cap = idx == 0 ? 0 : (idx == 1 ? 30 : 60);
        break;
    }
    case UI_SET_ROW_HEAP: {
        int idx = (ui_heap_index(s->heap_mb) + dir + 5) % 5;
        s->heap_mb = idx == 0 ? 0 : (8 << (idx - 1));
        break;
    }
    case UI_SET_ROW_MUTE:
        s->mute = s->mute ? 0 : 1;
        break;
    case UI_SET_ROW_NETWORK:
        s->network = s->network ? 0 : 1;
        break;
    default:
        return;                          /* games dir, binds and reset rows */
    }
    ui_save_settings(ui);
}

static void ui_settings_reset(nj_client_ui *ui)
{
    /* Recent/favourite jars are user state, not preferences: the reset must
     * put the settings back to defaults without forgetting what was played
     * or starred. */
    char recent[NJ_CLIENT_RECENT_MAX][NJ_CLIENT_PATH_MAX];
    char favorites[NJ_CLIENT_FAVORITE_MAX][NJ_CLIENT_PATH_MAX];
    char msg[UI_TOAST_MAX];
    int recent_count, favorite_count;

    if (!ui || !ui->settings) return;
    recent_count = ui->settings->recent_count;
    if (recent_count < 0) recent_count = 0;
    if (recent_count > NJ_CLIENT_RECENT_MAX) recent_count = NJ_CLIENT_RECENT_MAX;
    favorite_count = ui->settings->favorite_count;
    if (favorite_count < 0) favorite_count = 0;
    if (favorite_count > NJ_CLIENT_FAVORITE_MAX) {
        favorite_count = NJ_CLIENT_FAVORITE_MAX;
    }
    memcpy(recent, ui->settings->recent, sizeof(recent));
    memcpy(favorites, ui->settings->favorites, sizeof(favorites));
    nj_client_settings_default(ui->settings);
    memcpy(ui->settings->recent, recent, sizeof(recent));
    ui->settings->recent_count = recent_count;
    memcpy(ui->settings->favorites, favorites, sizeof(favorites));
    ui->settings->favorite_count = favorite_count;
    ui_save_settings(ui);
    if (ui->lib && ui->settings->games_dir[0]) {
        (void)nj_lib_scan_begin(ui->lib, ui->settings->games_dir);
    }
    ui->library_sel = 0;
    ui->library_top = 0;
    if (snprintf(msg, sizeof(msg), "已恢复默认设置") > 0) {
        ui_note_set(ui, msg);
    }
}

static void ui_settings_activate(nj_client_ui *ui)
{
    if (!ui) return;
    switch (ui->settings_row) {
    case UI_SET_ROW_GAMES:
        if (!ui->settings) return;
        ui_browser_open(ui, 1, NJ_CLIENT_MODE_SETTINGS);
        break;
    case UI_SET_ROW_BINDS:
        ui_binds_open(ui);
        break;
    case UI_SET_ROW_RESET:
        ui_settings_reset(ui);
        break;
    default:
        break;
    }
}

static void ui_open_settings(nj_client_ui *ui)
{
    if (!ui) return;
    ui->mode = NJ_CLIENT_MODE_SETTINGS;
    ui->settings_row = 0;
    ui->settings_top = 0;
    ui_settings_clamp(ui);
}

/* ------------------------------------------------------------------ */
/* Key-mapping view state                                              */
/* ------------------------------------------------------------------ */
static int ui_bind_count(void)
{
    return NJ_PORT_BIND_COUNT;
}

/* The name a slot actually uses: the player's override, else the default. */
static const char *ui_bind_effective(const nj_client_ui *ui, int bind)
{
    const char *def;
    if (bind < 0 || bind >= NJ_PORT_BIND_COUNT) return NULL;
    def = nj_client_bind_default_key(bind);
    if (!ui || !ui->settings) return def;
    if (ui->settings->binds[bind][0]) return ui->settings->binds[bind];
    return def;
}

/* Case-insensitive comparison; SDL reports "w" while a default may be "W". */
static int ui_name_eq(const char *a, const char *b)
{
    if (!a || !b) return 0;
    while (*a && *b) {
        int ca = (unsigned char)*a++;
        int cb = (unsigned char)*b++;
        if (ca >= 'a' && ca <= 'z') ca -= 'a' - 'A';
        if (cb >= 'a' && cb <= 'z') cb -= 'a' - 'A';
        if (ca != cb) return 0;
    }
    return *a == '\0' && *b == '\0';
}

/* Keys the launcher itself owns (menu, refresh, fullscreen, ...). */
static int ui_name_reserved(const char *name)
{
    return ui_name_eq(name, "F3") || ui_name_eq(name, "F5") ||
           ui_name_eq(name, "F10") || ui_name_eq(name, "F11") ||
           ui_name_eq(name, "F12") || ui_name_eq(name, "Tab");
}

/* A lone modifier cannot stand in for a game action. */
static int ui_name_modifier(const char *name)
{
    return ui_name_eq(name, "Left Shift") || ui_name_eq(name, "Right Shift") ||
           ui_name_eq(name, "Left Ctrl") || ui_name_eq(name, "Right Ctrl") ||
           ui_name_eq(name, "Left Alt") || ui_name_eq(name, "Right Alt") ||
           ui_name_eq(name, "Left GUI") || ui_name_eq(name, "Right GUI");
}

/* Printable ASCII, no '=' (the settings file's separator) and short enough to
 * fit the stored name. */
static int ui_name_storable(const char *name)
{
    size_t n;
    if (!name || !name[0]) return 0;
    n = strlen(name);
    if (n >= (size_t)NJ_CLIENT_BIND_NAME_MAX) return 0;
    for (; *name; name++) {
        unsigned char c = (unsigned char)*name;
        if (c < 0x20u || c > 0x7Eu || c == '=') return 0;
    }
    return 1;
}

static void ui_binds_clamp(nj_client_ui *ui)
{
    int count, vis, max_top;
    if (!ui) return;
    count = ui_bind_count();
    vis = ui->bind_visible > 0 ? ui->bind_visible : count;
    if (vis > count) vis = count;
    ui->bind_row = ui_clamp_int(ui->bind_row, 0, count - 1);
    if (ui->bind_top < 0) ui->bind_top = 0;
    max_top = count > vis ? count - vis : 0;
    ui->bind_top = ui_clamp_int(ui->bind_top, 0, max_top);
    if (ui->bind_row < ui->bind_top) {
        ui->bind_top = ui->bind_row;
    }
    if (ui->bind_row >= ui->bind_top + vis) {
        ui->bind_top = ui->bind_row - vis + 1;
    }
    if (ui->bind_top < 0) ui->bind_top = 0;
}

static void ui_binds_set(nj_client_ui *ui, int row)
{
    if (!ui) return;
    ui->bind_row = row;
    ui_binds_clamp(ui);
}

static void ui_binds_move(nj_client_ui *ui, int delta)
{
    if (!ui || delta == 0) return;
    ui_binds_set(ui, ui_clamp_int(ui->bind_row + delta, 0,
                                  ui_bind_count() - 1));
}

static void ui_binds_open(nj_client_ui *ui)
{
    if (!ui) return;
    ui->mode = NJ_CLIENT_MODE_BINDS;
    ui->bind_row = 0;
    ui->bind_top = 0;
    ui->capture_active = 0;
    ui->capture_bind = -1;
    ui_binds_clamp(ui);
}

/* Back to the settings row that opened the page. */
static void ui_binds_close(nj_client_ui *ui)
{
    if (!ui) return;
    ui->capture_active = 0;
    ui->capture_bind = -1;
    ui->mode = NJ_CLIENT_MODE_SETTINGS;
    ui->settings_row = UI_SET_ROW_BINDS;
    ui_settings_clamp(ui);
}

static void ui_bind_begin_capture(nj_client_ui *ui)
{
    if (!ui || !ui->settings) return;
    ui->capture_active = 1;
    ui->capture_bind = ui->bind_row;
}

/* Backspace: drop the override so the port default applies again. */
static void ui_bind_clear(nj_client_ui *ui)
{
    if (!ui || !ui->settings) return;
    ui->settings->binds[ui->bind_row][0] = '\0';
    ui_save_settings(ui);
    ui_note_set(ui, "已恢复默认");
}

static void ui_rescan(nj_client_ui *ui)
{
    const char *dir;
    if (!ui) return;
    dir = ui_games_dir(ui);
    if (!dir[0]) {
        ui_note_set(ui, "没有设置游戏目录");
        return;
    }
    if (!ui->lib) {
        ui_note_set(ui, "游戏库不可用");
        return;
    }
    if (!nj_lib_scan_begin(ui->lib, dir)) {
        ui_note_set(ui, "无法扫描游戏目录");
    }
}

/* ------------------------------------------------------------------ */
/* Drawing primitives                                                  */
/* ------------------------------------------------------------------ */
static int ui_hovering(const nj_client_ui *ui, int x, int y, int w, int h)
{
    if (w <= 0 || h <= 0) return 0;
    return ui->pointer_x >= x && ui->pointer_x < x + w &&
           ui->pointer_y >= y && ui->pointer_y < y + h;
}

static void ui_fill_round(nj_gfx *g, int x, int y, int w, int h, int radius,
                          nj_u4 color)
{
    if (w <= 0 || h <= 0) return;
    nj_gfx_set_color(g, color);
    nj_gfx_fill_round_rect(g, x, y, w, h, radius, radius);
}

static void ui_draw_button(nj_client_ui *ui, nj_gfx *g, int x, int y, int w,
                           const char *label)
{
    ui_fill_round(g, x, y, w, UI_BTN_H, 6,
                  ui_hovering(ui, x, y, w, UI_BTN_H) ? UI_COL_BTN_HOVER
                                                     : UI_COL_BTN);
    ui_draw_text(g, label, ui_font_medium(), UI_COL_BTN_TEXT, x + w / 2,
                 y + UI_BTN_H / 2, NJ_ANCHOR_HCENTER | NJ_ANCHOR_VCENTER,
                 w - 8, 0);
}

static void ui_draw_scrollbar(nj_gfx *g, int x, int y, int h, int total,
                              int top, int visible)
{
    int track_h, thumb_h, thumb_y, max_top;

    if (total <= visible || visible <= 0 || h <= 8) return;
    track_h = h - 8;
    if (track_h <= 0) return;
    nj_gfx_set_color(g, UI_COL_CHROME);
    nj_gfx_fill_rect(g, x, y + 4, 4, track_h);
    thumb_h = (int)(((long)track_h * (long)visible) / (long)total);
    if (thumb_h < 24) thumb_h = 24;
    if (thumb_h > track_h) thumb_h = track_h;
    max_top = total - visible;
    thumb_y = y + 4;
    if (max_top > 0) {
        thumb_y += (int)(((long)(track_h - thumb_h) * (long)top) / (long)max_top);
    }
    if (thumb_y + thumb_h > y + 4 + track_h) {
        thumb_y = y + 4 + track_h - thumb_h;
    }
    nj_gfx_set_color(g, UI_COL_THUMB);
    nj_gfx_fill_rect(g, x, thumb_y, 4, thumb_h);
}

/* Read one ARGB8888 source pixel; the caller checked the format. */
static nj_u4 ui_icon_pixel(const nj_image *img, int sx, int sy)
{
    const nj_u4 *pixels = (const nj_u4 *)img->pixels;
    return pixels[(size_t)sy * (size_t)img->width + (size_t)sx];
}

/* Area-average the source rectangle [x0,x1) x [y0,y1) into one pixel.
 * Colours are alpha weighted so transparent edges do not darken the result. */
static nj_u4 ui_icon_area(const nj_image *img, int x0, int y0, int x1, int y1)
{
    unsigned long count = 0, sum_a = 0, sum_r = 0, sum_g = 0, sum_b = 0;
    unsigned long a, r, g, b, n;
    int x, y;

    if (x1 <= x0) x1 = x0 + 1;
    if (y1 <= y0) y1 = y0 + 1;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > img->width) x1 = img->width;
    if (y1 > img->height) y1 = img->height;
    for (y = y0; y < y1; y++) {
        for (x = x0; x < x1; x++) {
            nj_u4 p = ui_icon_pixel(img, x, y);
            a = (p >> 24) & 0xFFu;
            sum_a += a;
            sum_r += ((p >> 16) & 0xFFu) * a;
            sum_g += ((p >> 8) & 0xFFu) * a;
            sum_b += (p & 0xFFu) * a;
            count++;
        }
    }
    if (count == 0ul || sum_a == 0ul) return 0u;
    n = (sum_a + count / 2ul) / count;             /* rounded mean alpha */
    if (n > 255ul) n = 255ul;
    r = (sum_r + sum_a / 2ul) / sum_a;
    g = (sum_g + sum_a / 2ul) / sum_a;
    b = (sum_b + sum_a / 2ul) / sum_a;
    if (r > 255ul) r = 255ul;
    if (g > 255ul) g = 255ul;
    if (b > 255ul) b = 255ul;
    return ((nj_u4)n << 24) | ((nj_u4)r << 16) | ((nj_u4)g << 8) | (nj_u4)b;
}

/* Scale one icon into the 32x32 row slot.  Smaller sources are expanded with
 * nearest-neighbour; sources at least as large as the slot are box filtered
 * (area sampling) so an 80x59 PNG does not alias.  The result is composited
 * through a 32x32 ARGB8888 image and nj_gfx clips it to that rectangle, so
 * pixels outside the slot are never touched. */
static void ui_draw_icon_scaled(nj_gfx *g, const nj_image *img, int x, int y)
{
    nj_u4 out[UI_ICON * UI_ICON];
    nj_image scaled;
    int dx, dy;

    if (img->width <= UI_ICON && img->height <= UI_ICON) {
        for (dy = 0; dy < UI_ICON; dy++) {
            int sy = dy * img->height / UI_ICON;
            for (dx = 0; dx < UI_ICON; dx++) {
                int sx = dx * img->width / UI_ICON;
                out[dy * UI_ICON + dx] = ui_icon_pixel(img, sx, sy);
            }
        }
    } else {
        for (dy = 0; dy < UI_ICON; dy++) {
            int y0 = dy * img->height / UI_ICON;
            int y1 = (dy + 1) * img->height / UI_ICON;
            for (dx = 0; dx < UI_ICON; dx++) {
                int x0 = dx * img->width / UI_ICON;
                int x1 = (dx + 1) * img->width / UI_ICON;
                out[dy * UI_ICON + dx] = ui_icon_area(img, x0, y0, x1, y1);
            }
        }
    }
    scaled.width = UI_ICON;
    scaled.height = UI_ICON;
    scaled.format = NJ_PIXFMT_ARGB8888;
    scaled.pixels = out;
    scaled.has_alpha = 1;
    nj_gfx_draw_image(g, &scaled, x, y, 0, 0, UI_ICON, UI_ICON);
}

/* Generic 32x32 icon slot used by the library rows. */
static void ui_draw_row_icon(nj_client_ui *ui, nj_gfx *g, int index, int x,
                             int y)
{
    const nj_image *img;
    const nj_lib_entry *entry;

    ui_fill_round(g, x, y, UI_ICON, UI_ICON, 6, UI_COL_ICON_BG);
    if (!ui->lib || !ui->lib->entries) return;
    if (index < 0 || index >= ui_lib_count(ui)) return;
    entry = &ui->lib->entries[index];
    if (!entry->valid) return;
    img = nj_lib_icon(ui->lib, index);
    if (!img || img->width <= 0 || img->height <= 0 || !img->pixels) return;
    /* nj_lib_icon() only returns decoded ARGB8888 images; guard anyway so a
     * future decoder cannot make this code read the wrong pixel layout. */
    if (img->format != NJ_PIXFMT_ARGB8888) return;
    ui_draw_icon_scaled(g, img, x, y);
}

/* ---- Favourite stars -------------------------------------------------- */
static int ui_path_is_favorite(const nj_client_ui *ui, const char *path)
{
    int i, n;

    if (!ui || !ui->settings || !path || !path[0]) return 0;
    n = ui->settings->favorite_count;
    if (n < 0) n = 0;
    if (n > NJ_CLIENT_FAVORITE_MAX) n = NJ_CLIENT_FAVORITE_MAX;
    for (i = 0; i < n; i++) {
        if (strcmp(ui->settings->favorites[i], path) == 0) return 1;
    }
    return 0;
}

/* A section header band; it scrolls with the rows and is never selectable. */
static void ui_draw_section_header(nj_client_ui *ui, nj_gfx *g, int y,
                                   const char *text)
{
    ui_draw_text(g, text, ui_font_small(), UI_COL_MUTED, 24,
                 y + UI_SECTION_H / 2, NJ_ANCHOR_LEFT | NJ_ANCHOR_VCENTER,
                 ui->width - 48, 0);
}

static void ui_draw_library_row(nj_client_ui *ui, nj_gfx *g, int row,
                                int hovered)
{
    const nj_lib_entry *e;
    const nj_font *large = ui_font_large();
    const nj_font *medium = ui_font_medium();
    int index, row_y, x, w, title_avail, sub_avail;
    int selected, badge_x, badge_w, title_x;
    char sub[256];
    const char *title;
    const char *sub_name;
    nj_u4 title_color;

    if (!ui->lib || !ui->lib->entries) return;
    index = ui_view_entry_at(ui, row);
    if (index < 0) return;
    e = &ui->lib->entries[index];
    row_y = ui->list_y + ui_view_y(ui, row) - ui_view_base(ui, ui->library_top);
    x = 8;
    w = ui->width - 16;
    selected = row == ui_view_row_of_sel(ui);

    nj_gfx_set_color(g, (row & 1) ? UI_COL_ZEBRA_B : UI_COL_ZEBRA_A);
    nj_gfx_fill_rect(g, x, row_y, w, ui->row_h);
    if (selected || hovered) {
        ui_fill_round(g, x, row_y + 2, w, ui->row_h - 4, 6,
                      selected ? UI_COL_SEL : UI_COL_HOVER);
    }
    ui_draw_row_icon(ui, g, index, 24, row_y + 8);

    title = e->name[0] ? e->name : e->file;
    title_color = e->valid ? UI_COL_ROW_TITLE : UI_COL_INVALID;
    badge_w = 0;
    badge_x = w - 12;
    if (!e->valid) {
        badge_w = ui_text_width(medium, "无法运行");
        ui_draw_text(g, "无法运行", medium, UI_COL_BADGE, badge_x,
                     row_y + 8, NJ_ANCHOR_RIGHT | NJ_ANCHOR_TOP, 0, 0);
    }
    /* The star column is reserved on every row so the names line up. */
    title_x = 68 + UI_STAR_W;
    title_avail = badge_x - badge_w - (e->valid ? 0 : 12) - title_x;
    if (title_avail < 8) title_avail = 8;

    sub_name = e->file[0] ? e->file : title;
    if (snprintf(sub, sizeof(sub), "%s · %lu KB", sub_name,
                 (unsigned long)(e->size / 1024u)) <= 0) {
        sub[0] = '\0';
    }
    sub_avail = w - 12 - title_x;
    if (sub_avail < 8) sub_avail = 8;

    if (ui_path_is_favorite(ui, e->path)) {
        ui_draw_text(g, "★", large, UI_COL_STAR, 68, row_y + 6,
                     NJ_ANCHOR_LEFT | NJ_ANCHOR_TOP, UI_STAR_W, 0);
    }
    ui_draw_text(g, title, large, title_color, title_x, row_y + 6,
                 NJ_ANCHOR_LEFT | NJ_ANCHOR_TOP, title_avail, 0);
    if (sub[0]) {
        ui_draw_text(g, sub, medium, UI_COL_ROW_SUB, title_x, row_y + 26,
                     NJ_ANCHOR_LEFT | NJ_ANCHOR_TOP, sub_avail, 0);
    }
}

static void ui_draw_empty(nj_client_ui *ui, nj_gfx *g)
{
    const nj_font *large = ui_font_large();
    const nj_font *medium = ui_font_medium();
    const nj_font *small = ui_font_small();
    int w = ui->width;
    int y0 = ui->list_y;
    int h = ui->list_h;
    int cx = w / 2;
    int y, total;
    const char *dir = ui_games_dir(ui);

    if (h <= 0) return;
    if (ui_scan_busy(ui)) {
        ui_draw_text(g, "正在扫描游戏库…", large, UI_COL_ROW_TITLE, cx,
                     y0 + h / 2, NJ_ANCHOR_HCENTER | NJ_ANCHOR_VCENTER,
                     w - 32, 0);
        return;
    }
    if (ui_filter_active(ui) && ui_lib_count(ui) > 0) {
        /* A non-empty library with no matching row is not "empty": say the
         * search failed and how to get back to the full list. */
        total = large->height + 12 + medium->height + 6 + small->height;
        y = y0 + (h - total) / 2;
        if (y < y0 + 8) y = y0 + 8;
        ui_draw_text(g, "没有匹配的游戏", large, UI_COL_ROW_TITLE, cx, y,
                     NJ_ANCHOR_HCENTER | NJ_ANCHOR_TOP, w - 32, 0);
        y += large->height + 12;
        ui_draw_text(g, "按 Backspace 修改，或按 Esc 清除搜索", medium,
                     UI_COL_ROW_SUB, cx, y, NJ_ANCHOR_HCENTER | NJ_ANCHOR_TOP,
                     w - 32, 0);
        y += medium->height + 6;
        ui_draw_text(g, ui->filter, small, UI_COL_MUTED, cx, y,
                     NJ_ANCHOR_HCENTER | NJ_ANCHOR_TOP, w - 48, 1);
        return;
    }
    total = large->height + 12 + medium->height + 6 + medium->height + 16 +
            small->height;
    y = y0 + (h - total) / 2;
    if (y < y0 + 8) y = y0 + 8;

    ui_draw_text(g, "游戏库是空的", large, UI_COL_ROW_TITLE, cx, y,
                 NJ_ANCHOR_HCENTER | NJ_ANCHOR_TOP, w - 32, 0);
    y += large->height + 12;
    ui_draw_text(g, "把 .jar 文件拖到窗口即可加入并运行", medium,
                 UI_COL_ROW_SUB, cx, y,
                 NJ_ANCHOR_HCENTER | NJ_ANCHOR_TOP, w - 32, 0);
    y += medium->height + 6;
    ui_draw_text(g, "或者按 O 打开文件选择器", medium, UI_COL_ROW_SUB, cx, y,
                 NJ_ANCHOR_HCENTER | NJ_ANCHOR_TOP, w - 32, 0);
    y += medium->height + 16;
    if (dir[0]) {
        ui_draw_text(g, dir, small, UI_COL_MUTED, cx, y,
                     NJ_ANCHOR_HCENTER | NJ_ANCHOR_TOP, w - 48, 1);
    }
}

/* Selectable row under the pointer, or -1 (outside, or a header band). */
static int ui_lib_row_at(const nj_client_ui *ui, int px, int py)
{
    int rows, base, row;

    if (!ui || ui->mode != NJ_CLIENT_MODE_LIBRARY) return -1;
    if (px < 8 || px >= ui->width - 8) return -1;
    if (py < ui->list_y || py >= ui->list_y + ui->list_h) return -1;
    if (ui->row_h <= 0) return -1;
    rows = ui_view_rows(ui);
    if (rows <= 0) return -1;
    base = ui_view_base(ui, ui->library_top);
    for (row = ui->library_top; row < rows; row++) {
        int y = ui->list_y + ui_view_y(ui, row) - base;
        if (y >= ui->list_y + ui->list_h) break;
        if (py < y) break;               /* a header band: dead space */
        if (py < y + ui->row_h) return row;
    }
    return -1;
}

static int ui_row_hover(const nj_client_ui *ui)
{
    if (!ui || ui->mode != NJ_CLIENT_MODE_LIBRARY) return -1;
    if (ui->pointer_x < 0 || ui->pointer_y < 0) return -1;
    return ui_lib_row_at(ui, ui->pointer_x, ui->pointer_y);
}

static void ui_draw_library_list(nj_client_ui *ui, nj_gfx *g)
{
    int rows, recent, top, base, row, hover, total, first;

    ui_lib_clamp(ui);
    rows = ui_view_rows(ui);
    recent = ui_recent_row_count(ui);
    top = ui->library_top;
    base = ui_view_base(ui, top);
    nj_gfx_set_clip(g, 0, ui->list_y, ui->width, ui->list_h);
    if (rows <= 0) {
        ui_draw_empty(ui, g);
        ui_clip_full(ui, g);
        return;
    }
    hover = ui_row_hover(ui);
    if (recent > 0) {
        int hy = ui->list_y - base;
        if (hy + UI_SECTION_H > ui->list_y &&
            hy < ui->list_y + ui->list_h) {
            ui_draw_section_header(ui, g, hy, "最近玩过");
        }
        hy = ui->list_y + UI_SECTION_H + recent * ui->row_h - base;
        if (hy + UI_SECTION_H > ui->list_y &&
            hy < ui->list_y + ui->list_h) {
            ui_draw_section_header(ui, g, hy, "全部游戏");
        }
    }
    for (row = top; row < rows; row++) {
        int y = ui->list_y + ui_view_y(ui, row) - base;
        if (y >= ui->list_y + ui->list_h) break;
        ui_draw_library_row(ui, g, row, row == hover);
    }
    total = rows + (recent > 0 ? 2 : 0);
    first = top + (recent > 0 ? (top < recent ? 1 : 2) : 0);
    ui_draw_scrollbar(g, ui->width - 6, ui->list_y, ui->list_h, total, first,
                      ui_view_visible_rows(ui, top));
    ui_clip_full(ui, g);
}

static void ui_draw_header(nj_client_ui *ui, nj_gfx *g, int browser)
{
    const nj_font *large = ui_font_large();
    const nj_font *medium = ui_font_medium();
    int w = ui->width;
    int h = ui->header_h;
    int mid = h / 2;
    int x;

    nj_gfx_set_color(g, UI_COL_CHROME);
    nj_gfx_fill_rect(g, 0, 0, w, h);
    nj_gfx_set_color(g, UI_COL_LINE);
    if (h > 1) nj_gfx_fill_rect(g, 0, h - 1, w, 1);
    nj_gfx_set_clip(g, 0, 0, w, h);

    if (browser) {
        ui_draw_text(g, ui->browser_pick_dir ? "选择游戏目录" : "打开游戏",
                     large, UI_COL_TITLE, 16, mid,
                     NJ_ANCHOR_LEFT | NJ_ANCHOR_VCENTER, w - 32, 0);
        ui_clip_full(ui, g);
        return;
    }

    ui_draw_text(g, "nano_j2me", large, UI_COL_TITLE, 16, mid,
                 NJ_ANCHOR_LEFT | NJ_ANCHOR_VCENTER, w - 32, 0);
    x = 16 + ui_text_width(large, "nano_j2me") + 8;
    if (x + 48 < w) {
        char version[32];
        snprintf(version, sizeof(version), "v%s", NJ_VERSION_STRING);
        ui_draw_text(g, version, medium, UI_COL_DIM_TEXT, x, mid,
                     NJ_ANCHOR_LEFT | NJ_ANCHOR_VCENTER, w - x - 16, 0);
        x += ui_text_width(medium, version);
    }
    x += 16;
    if (x < w - 40) {
        if (ui_scan_busy(ui)) {
            char busy[64];
            int total = ui->lib ? ui->lib->pending_count : 0;
            int done = ui->lib ? ui->lib->pending_index : 0;
            if (total < 0) total = 0;
            if (done < 0) done = 0;
            if (done > total) done = total;
            if (total > 0) {
                snprintf(busy, sizeof(busy), "正在扫描 %d/%d", done, total);
            } else {
                snprintf(busy, sizeof(busy), "正在扫描…");
            }
            ui_draw_text(g, busy, medium, UI_COL_DIM_TEXT, w - 16, mid,
                         NJ_ANCHOR_RIGHT | NJ_ANCHOR_VCENTER, w - 16 - x, 0);
        } else {
            const char *dir = ui_games_dir(ui);
            if (dir[0]) {
                ui_draw_text(g, dir, medium, UI_COL_DIM_TEXT, w - 16, mid,
                             NJ_ANCHOR_RIGHT | NJ_ANCHOR_VCENTER,
                             w - 16 - x, 1);
            }
        }
    }
    ui_clip_full(ui, g);
}

static void ui_draw_footer_hint(nj_client_ui *ui, nj_gfx *g, const char *text,
                                int limit)
{
    int avail;

    if (limit <= 16) return;
    /* The buttons matter more than a hint clipped to a couple of glyphs:
     * on a narrow window (320 px wide puts them at x=68) show no hint at
     * all instead of "up down space" fragments. */
    avail = limit - 24;
    if (avail < 140) return;
    nj_gfx_set_clip(g, 0, ui->footer_y, limit, ui->footer_h);
    ui_draw_text(g, text, ui_font_medium(), UI_COL_ROW_SUB, 16,
                 ui->footer_y + ui->footer_h / 2,
                 NJ_ANCHOR_LEFT | NJ_ANCHOR_VCENTER, avail, 0);
    ui_clip_full(ui, g);
}

static void ui_draw_footer(nj_client_ui *ui, nj_gfx *g, int browser)
{
    int w = ui->width;
    int y = ui->footer_y;
    int h = ui->footer_h;
    int by, i;
    int bx;

    nj_gfx_set_color(g, UI_COL_CHROME);
    nj_gfx_fill_rect(g, 0, y, w, h);
    nj_gfx_set_color(g, UI_COL_LINE);
    if (h > 1) nj_gfx_fill_rect(g, 0, y, w, 1);
    by = y + (h - UI_BTN_H) / 2;
    if (by < y + 2) by = y + 2;

    if (browser) {
        bx = w - 16;
        if (ui->browser_pick_dir) {
            ui->pick_btn_rect[0] = bx - UI_PICK_BTN_W;
            ui->pick_btn_rect[1] = by;
            ui->pick_btn_rect[2] = UI_PICK_BTN_W;
            ui->pick_btn_rect[3] = UI_BTN_H;
            ui_draw_button(ui, g, ui->pick_btn_rect[0], by, UI_PICK_BTN_W,
                           "使用此目录");
            bx -= UI_PICK_BTN_W + UI_BTN_GAP;
        } else {
            memset(ui->pick_btn_rect, 0, sizeof(ui->pick_btn_rect));
        }
        ui_draw_footer_hint(ui, g, "Enter 打开 · Backspace 上级 · Esc 取消",
                            bx - 8);
    } else {
        bx = w - 16 - (3 * UI_BTN_W + 2 * UI_BTN_GAP);
        for (i = 0; i < 3; i++) {
            ui->button_rect[i][0] = bx + i * (UI_BTN_W + UI_BTN_GAP);
            ui->button_rect[i][1] = by;
            ui->button_rect[i][2] = UI_BTN_W;
            ui->button_rect[i][3] = UI_BTN_H;
        }
        ui_draw_button(ui, g, ui->button_rect[0][0], by, UI_BTN_W, "打开…");
        ui_draw_button(ui, g, ui->button_rect[1][0], by, UI_BTN_W, "设置");
        ui_draw_button(ui, g, ui->button_rect[2][0], by, UI_BTN_W, "退出");
        /* The library has more shortcuts than one medium line fits next to
         * the three buttons, and search must stay discoverable: two small
         * lines list everything including "输入 搜索". */
        if (bx - 8 - 24 >= 180) {
            nj_gfx_set_clip(g, 0, y, bx - 8, h);
            ui_draw_text(g, "↑↓ 选择 · Enter 运行 · O 打开 · R 刷新 · F2 收藏",
                         ui_font_small(), UI_COL_ROW_SUB, 16,
                         y + h / 2 - 8,
                         NJ_ANCHOR_LEFT | NJ_ANCHOR_VCENTER, bx - 8 - 24, 0);
            ui_draw_text(g, "S 设置 · 输入 搜索 · Esc 退出",
                         ui_font_small(), UI_COL_ROW_SUB, 16,
                         y + h / 2 + 8,
                         NJ_ANCHOR_LEFT | NJ_ANCHOR_VCENTER, bx - 8 - 24, 0);
            ui_clip_full(ui, g);
        }
    }
    ui_clip_full(ui, g);
}

/* LIBRARY search bar: 32 px directly under the header.  The left side shows
 * the typed filter (or a hint while it is empty), the right side the matched
 * and total row counts. */
static void ui_draw_search_bar(nj_client_ui *ui, nj_gfx *g)
{
    const nj_font *medium = ui_font_medium();
    int y = ui->header_h;
    int cy = y + UI_SEARCH_H / 2;
    int x = 16;
    int right, avail, matched, total, tw;
    char count[64];

    nj_gfx_set_color(g, UI_COL_PATH_BAR);
    nj_gfx_fill_rect(g, 0, y, ui->width, UI_SEARCH_H);
    nj_gfx_set_color(g, UI_COL_LINE);
    if (UI_SEARCH_H > 1) {
        nj_gfx_fill_rect(g, 0, y + UI_SEARCH_H - 1, ui->width, 1);
    }
    nj_gfx_set_clip(g, 0, y, ui->width, UI_SEARCH_H);

    matched = ui_filter_active(ui) ? ui_filter_count(ui) : ui_lib_count(ui);
    total = ui_lib_count(ui);
    snprintf(count, sizeof(count), "%d / %d 项", matched, total);
    right = ui->width - 16 - ui_text_width(medium, count);
    if (right < x + 40) right = x + 40;

    ui_draw_text(g, "搜索：", medium, UI_COL_DIM_TEXT, x, cy,
                 NJ_ANCHOR_LEFT | NJ_ANCHOR_VCENTER, 0, 0);
    x += ui_text_width(medium, "搜索：") + 4;
    avail = right - 8 - x;
    if (avail < 0) avail = 0;
    if (ui->filter_len > 0) {
        tw = ui_text_width(medium, ui->filter);
        ui_draw_text(g, ui->filter, medium, UI_COL_TITLE, x, cy,
                     NJ_ANCHOR_LEFT | NJ_ANCHOR_VCENTER, avail, 1);
        if (tw > 0 && tw < avail) {
            ui_draw_text(g, "|", medium, UI_COL_TITLE, x + tw, cy,
                         NJ_ANCHOR_LEFT | NJ_ANCHOR_VCENTER, 0, 0);
        }
    } else {
        ui_draw_text(g, "输入名称或文件名搜索", medium, UI_COL_MUTED, x, cy,
                     NJ_ANCHOR_LEFT | NJ_ANCHOR_VCENTER, avail, 0);
    }
    ui_draw_text(g, count, medium, UI_COL_ROW_SUB, ui->width - 16, cy,
                 NJ_ANCHOR_RIGHT | NJ_ANCHOR_VCENTER, 0, 0);
    ui_clip_full(ui, g);
}

/* F1 shortcut sheet: a dimmed, centred card listing every client key.  The
 * sheet is modal in the model (see ui_key_library/nj_client_ui_key), so this
 * only has to paint over the library frame. */
typedef struct ui_help_entry {
    const char *heading;
    const char *text;
} ui_help_entry;

static const ui_help_entry ui_help_entries[] = {
    { "游戏库：", NULL },
    { NULL, "↑↓ 选择 · Enter 运行 · O 打开 · R 刷新 · S 设置" },
    { NULL, "输入 搜索 · F1 帮助 · Esc 退出" },
    { NULL, "F2 收藏/取消收藏 · 最近玩过 显示最近运行的游戏" },
    { "游戏窗口：", NULL },
    { NULL, "方向键 移动 · Enter/空格 开火 · F1 左软键 · F2 右软键" },
    { NULL, "Backspace 清除 · 数字键 · F3 速度 · F5 存档" },
    { NULL, "F10 回到库 · F11 全屏 · 拖放 .jar 切换" },
    { "设置：", NULL },
    { NULL, "↑↓ 选择 · ←→ 修改 · Enter 确认 · Esc 返回" },
    { NULL, "" },
    { NULL, "$XDG_CONFIG_HOME/nano_j2me/client.conf" },
    { NULL, "窗口大小会自动记住" }
};

static void ui_draw_help(nj_client_ui *ui, nj_gfx *g)
{
    const nj_font *large = ui_font_large();
    const nj_font *medium = ui_font_medium();
    const nj_font *small = ui_font_small();
    int w = ui->width, h = ui->height;
    int pw = ui_min(UI_HELP_W, w - UI_HELP_MARGIN);
    int ph = ui_min(UI_HELP_H, h - UI_HELP_MARGIN);
    int px, py, y, i;
    size_t n;

    nj_gfx_set_blend(g, NJ_BLEND_SRC_OVER);
    nj_gfx_set_color(g, UI_COL_DIM);
    nj_gfx_fill_rect(g, 0, 0, w, h);
    nj_gfx_set_blend(g, NJ_BLEND_NONE);
    if (pw < 200 || ph < 140) {
        /* Too small for a readable card: dimming alone still signals the
         * modal state, and no text is clipped into nonsense. */
        nj_gfx_set_blend(g, NJ_BLEND_SRC_OVER);
        return;
    }
    px = (w - pw) / 2;
    py = (h - ph) / 2;
    nj_gfx_set_clip(g, px, py, pw, ph);
    ui_fill_round(g, px, py, pw, ph, 10, UI_COL_PANEL);
    nj_gfx_set_color(g, UI_COL_PANEL_EDGE);
    nj_gfx_round_rect(g, px, py, pw - 1, ph - 1, 10, 10);
    ui_draw_text(g, "快捷键", large, UI_COL_TITLE, px + 24, py + 14,
                 NJ_ANCHOR_LEFT | NJ_ANCHOR_TOP, pw - 48, 0);

    y = py + 14 + large->height + 8;
    n = sizeof(ui_help_entries) / sizeof(ui_help_entries[0]);
    for (i = 0; i < (int)n; i++) {
        const ui_help_entry *e = &ui_help_entries[i];
        if (e->heading) {
            ui_draw_text(g, e->heading, medium, UI_COL_ROW_TITLE, px + 24, y,
                         NJ_ANCHOR_LEFT | NJ_ANCHOR_TOP, pw - 48, 0);
            y += medium->height + 2;
        } else if (e->text && e->text[0]) {
            int muted = (i >= (int)n - 2);
            ui_draw_text(g, e->text, small,
                         muted ? UI_COL_MUTED : UI_COL_ROW_SUB, px + 32, y,
                         NJ_ANCHOR_LEFT | NJ_ANCHOR_TOP, pw - 56, 1);
            y += small->height + 4;
        } else {
            y += 8;
        }
    }
    ui_draw_text(g, "F1 或 Esc 关闭", medium, UI_COL_DIM_TEXT, px + pw / 2,
                 py + ph - 20, NJ_ANCHOR_HCENTER | NJ_ANCHOR_VCENTER, pw - 48,
                 0);
    ui_clip_full(ui, g);
    nj_gfx_set_blend(g, NJ_BLEND_SRC_OVER);
}

static void ui_draw_library(nj_client_ui *ui, nj_gfx *g)
{
    ui_draw_header(ui, g, 0);
    ui_draw_search_bar(ui, g);
    ui_draw_library_list(ui, g);
    ui_draw_footer(ui, g, 0);
    if (ui->help_active) ui_draw_help(ui, g);
}

/* ------------------------------------------------------------------ */
/* Browser drawing                                                     */
/* ------------------------------------------------------------------ */
static void ui_draw_folder_icon(nj_gfx *g, int x, int y)
{
    nj_gfx_set_color(g, UI_COL_FOLDER);
    nj_gfx_fill_round_rect(g, x + 1, y + 1, 12, 8, 2, 2);
    nj_gfx_fill_round_rect(g, x, y + 6, 24, 16, 3, 3);
}

static void ui_draw_jar_icon(nj_gfx *g, int x, int y)
{
    ui_fill_round(g, x, y, 24, 24, 4, UI_COL_ICON_BG);
    ui_draw_text(g, "J", ui_font_medium(), UI_COL_ROW_SUB, x + 12, y + 12,
                 NJ_ANCHOR_HCENTER | NJ_ANCHOR_VCENTER, 0, 0);
}

static int ui_browser_hover(const nj_client_ui *ui)
{
    int rows_y, rows_h, vis, row;
    if (!ui || ui->mode != NJ_CLIENT_MODE_BROWSER) return -1;
    if (ui->pointer_x < 0 || ui->pointer_y < 0) return -1;
    ui_browser_geom(ui, &rows_y, &rows_h, &vis);
    (void)vis;
    if (ui->pointer_y < rows_y || ui->pointer_y >= rows_y + rows_h) return -1;
    if (ui->pointer_x < 0 || ui->pointer_x >= ui->width) return -1;
    row = ui->browser_top + (ui->pointer_y - rows_y) / UI_BROW_ROW_H;
    if (row < 0 || row >= ui_browser_row_count(ui)) return -1;
    return row;
}

static void ui_draw_browser_row(nj_client_ui *ui, nj_gfx *g, int index,
                                int row_y, int hovered)
{
    ui_browser_row row;
    const nj_font *medium = ui_font_medium();
    int selected = index == ui->browser_sel;
    int x = 8;
    int w = ui->width - 16;
    int name_x = 56;
    char name[192];

    if (!ui_browser_row_at(ui, index, &row)) return;
    nj_gfx_set_color(g, (index & 1) ? UI_COL_ZEBRA_B : UI_COL_ZEBRA_A);
    nj_gfx_fill_rect(g, x, row_y, w, UI_BROW_ROW_H);
    if (selected || hovered) {
        ui_fill_round(g, x, row_y + 2, w, UI_BROW_ROW_H - 4, 6,
                      selected ? UI_COL_SEL : UI_COL_HOVER);
    }
    if (row.is_dir) {
        ui_draw_folder_icon(g, 24, row_y + (UI_BROW_ROW_H - 24) / 2);
    } else {
        ui_draw_jar_icon(g, 24, row_y + (UI_BROW_ROW_H - 24) / 2);
    }
    if (row.is_up) {
        snprintf(name, sizeof(name), "..");
    } else if (row.is_dir) {
        snprintf(name, sizeof(name), "%s/", row.name);
    } else {
        snprintf(name, sizeof(name), "%s", row.name);
    }
    ui_draw_text(g, name, medium,
                 selected ? UI_COL_ROW_TITLE : UI_COL_PATH_TEXT, name_x,
                 row_y + UI_BROW_ROW_H / 2,
                 NJ_ANCHOR_LEFT | NJ_ANCHOR_VCENTER,
                 ui->width - 24 - name_x, 0);
}

static void ui_draw_browser(nj_client_ui *ui, nj_gfx *g)
{
    const nj_font *medium = ui_font_medium();
    int w = ui->width;
    int rows_y, rows_h, vis, path_h, n, i, hover;

    ui_draw_header(ui, g, 1);

    path_h = ui_browser_path_h(ui);
    nj_gfx_set_color(g, UI_COL_PATH_BAR);
    nj_gfx_fill_rect(g, 0, ui->header_h, w, path_h);
    nj_gfx_set_color(g, UI_COL_LINE);
    nj_gfx_fill_rect(g, 0, ui->header_h, w, 1);
    ui_draw_text(g, ui->browser_dir[0] ? ui->browser_dir : ".", medium,
                 UI_COL_PATH_TEXT, 16, ui->header_h + path_h / 2,
                 NJ_ANCHOR_LEFT | NJ_ANCHOR_VCENTER, w - 32, 1);

    ui_browser_geom(ui, &rows_y, &rows_h, &vis);
    ui_browser_clamp(ui);
    n = ui_browser_row_count(ui);
    hover = ui_browser_hover(ui);

    nj_gfx_set_clip(g, 0, rows_y, w, rows_h);
    if (n <= 0) {
        ui_draw_text(g, "此目录中没有游戏或文件夹", medium, UI_COL_ROW_SUB,
                     w / 2, rows_y + rows_h / 2,
                     NJ_ANCHOR_HCENTER | NJ_ANCHOR_VCENTER, w - 32, 0);
    } else {
        for (i = ui->browser_top; i < ui->browser_top + vis && i < n; i++) {
            ui_draw_browser_row(ui, g, i,
                                rows_y + (i - ui->browser_top) * UI_BROW_ROW_H,
                                i == hover);
        }
        ui_draw_scrollbar(g, w - 6, rows_y, rows_h, n, ui->browser_top, vis);
    }
    ui_clip_full(ui, g);
    ui_draw_footer(ui, g, 1);
}

/* ------------------------------------------------------------------ */
/* Settings drawing                                                    */
/* ------------------------------------------------------------------ */
/* Modal panel geometry shared by SETTINGS and BINDS.  Returns 0 when the
 * window is too small for a usable card. */
static int ui_panel_geom(const nj_client_ui *ui, int *px, int *py, int *pw,
                         int *ph)
{
    int w = ui->width;
    int h = ui->height;
    int cw = ui_min(540, w - 24);
    int ch = ui_min(320, h - 16);

    if (cw < 140 || ch < 120) return 0;
    if (px) *px = (w - cw) / 2;
    if (py) *py = (h - ch) / 2;
    if (pw) *pw = cw;
    if (ph) *ph = ch;
    return 1;
}

/* Row geometry inside a panel; also decides how many rows fit so the input
 * handlers clamp rows exactly the way the frame drew them. */
static void ui_panel_rows(int py, int ph, int count, int *rows_y, int *rh,
                          int *vis)
{
    int top_off = ui_clamp_int(ph * 64 / 320, 34, 64);
    *rh = ui_clamp_int(ph * 40 / 320, 26, 40);
    *vis = (ph - top_off - 26) / *rh;
    *vis = ui_clamp_int(*vis, 1, count);
    *rows_y = py + top_off;
}

/* Dim the library, then draw the page's card and title.  Returns 0 when the
 * window is too small, leaving just the dimmed library. */
static int ui_draw_panel(nj_client_ui *ui, nj_gfx *g, const char *title,
                         int *px, int *py, int *pw, int *ph)
{
    int x, y, w, h;

    ui_draw_library(ui, g);
    /* Dim the window; the overlay needs real alpha compositing. */
    nj_gfx_set_blend(g, NJ_BLEND_SRC_OVER);
    nj_gfx_set_color(g, UI_COL_DIM);
    nj_gfx_fill_rect(g, 0, 0, ui->width, ui->height);
    nj_gfx_set_blend(g, NJ_BLEND_NONE);

    if (!ui_panel_geom(ui, &x, &y, &w, &h)) {
        nj_gfx_set_blend(g, NJ_BLEND_SRC_OVER);
        return 0;
    }
    nj_gfx_set_clip(g, x, y, w, h);
    ui_fill_round(g, x, y, w, h, 10, UI_COL_PANEL);
    nj_gfx_set_color(g, UI_COL_PANEL_EDGE);
    nj_gfx_round_rect(g, x, y, w - 1, h - 1, 10, 10);
    ui_draw_text(g, title, ui_font_large(), UI_COL_TITLE, x + 24, y + 14,
                 NJ_ANCHOR_LEFT | NJ_ANCHOR_TOP, w - 48, 0);
    if (px) *px = x;
    if (py) *py = y;
    if (pw) *pw = w;
    if (ph) *ph = h;
    return 1;
}

static void ui_draw_panel_end(nj_client_ui *ui, nj_gfx *g)
{
    ui_clip_full(ui, g);
    nj_gfx_set_blend(g, NJ_BLEND_SRC_OVER);
}

static void ui_draw_settings(nj_client_ui *ui, nj_gfx *g)
{
    const nj_font *medium = ui_font_medium();
    int px, py, pw, ph, rows_y, rh, vis, i;
    int count = ui_setting_count();

    if (!ui_draw_panel(ui, g, "设置", &px, &py, &pw, &ph)) return;

    ui_panel_rows(py, ph, count, &rows_y, &rh, &vis);
    ui->settings_visible = vis;
    ui_settings_clamp(ui);

    ui->settings_panel[0] = px;
    ui->settings_panel[1] = py;
    ui->settings_panel[2] = pw;
    ui->settings_panel[3] = ph;
    ui->settings_row_y = rows_y;
    ui->settings_row_h = rh;

    for (i = ui->settings_top; i < ui->settings_top + vis && i < count; i++) {
        int ry = rows_y + (i - ui->settings_top) * rh;
        int selected = i == ui->settings_row;
        const char *label = ui_setting_label(i);
        int label_x = px + 24;
        int vx = px + pw - 24;
        int label_w;
        char value[UI_PATH_MAX];

        if (ry + rh > py + ph - 22) break;    /* keep the panel footer clear */
        if (selected) {
            ui_fill_round(g, px + 14, ry + 1, pw - 28, rh - 2, 6, UI_COL_SEL);
        }
        label_w = ui_text_width(medium, label);
        ui_draw_text(g, label, medium,
                     selected ? UI_COL_ROW_TITLE : UI_COL_PATH_TEXT, label_x,
                     ry + rh / 2, NJ_ANCHOR_LEFT | NJ_ANCHOR_VCENTER,
                     ui_max(20, vx - label_x - 16), 0);
        if (i == UI_SET_ROW_RESET) continue;      /* 恢复默认 has no value */
        ui_settings_value(ui, i, value, (int)sizeof(value));
        if (!value[0]) continue;
        ui_draw_text(g, value, medium,
                     selected ? UI_COL_ROW_TITLE : UI_COL_ROW_SUB, vx,
                     ry + rh / 2, NJ_ANCHOR_RIGHT | NJ_ANCHOR_VCENTER,
                     ui_max(0, vx - (label_x + label_w + 16)),
                     i == UI_SET_ROW_GAMES ? 1 : 0);
    }
    ui_draw_scrollbar(g, px + pw - 10, rows_y, vis * rh, count,
                      ui->settings_top, vis);
    ui_draw_text(g, "↑↓ 选择 · ←→ 修改 · Enter 打开 · Esc 关闭", medium,
                 UI_COL_ROW_SUB, px + 24, py + ph - 18,
                 NJ_ANCHOR_LEFT | NJ_ANCHOR_VCENTER, pw - 48, 0);
    ui_draw_panel_end(ui, g);
}

/* ------------------------------------------------------------------ */
/* Key-mapping drawing                                                 */
/* ------------------------------------------------------------------ */
/* Small "press a key" card over the bind page, centered in the panel. */
static void ui_draw_capture(nj_client_ui *ui, nj_gfx *g, int px, int py,
                            int pw, int ph)
{
    const nj_font *large = ui_font_large();
    const nj_font *medium = ui_font_medium();
    int box_w, box_h, bx, by, avail, text_w;
    char action[UI_TEXT_MAX];

    if (!ui->capture_active) return;
    snprintf(action, sizeof(action), "动作：%s",
             nj_client_bind_label(ui->capture_bind));
    text_w = ui_max(ui_text_width(large, "按下新按键…"),
                    ui_max(ui_text_width(medium, action),
                           ui_text_width(medium, "Esc 取消")));
    box_w = ui_min(pw - 40, text_w + 48);
    if (box_w < 180) box_w = ui_min(pw - 40, 180);
    if (box_w < 80) box_w = pw - 8;
    box_h = 14 + large->height + 8 + medium->height + 8 + medium->height + 14;
    bx = px + (pw - box_w) / 2;
    by = py + (ph - box_h) / 2;
    if (by < py + 40) by = py + 40;
    if (by + box_h > py + ph - 8) by = py + ph - 8 - box_h;

    ui_fill_round(g, bx, by, box_w, box_h, 8, UI_COL_BTN);
    nj_gfx_set_color(g, UI_COL_PANEL_EDGE);
    nj_gfx_round_rect(g, bx, by, box_w - 1, box_h - 1, 8, 8);
    avail = ui_max(8, box_w - 24);
    ui_draw_text(g, "按下新按键…", large, UI_COL_TITLE, bx + box_w / 2,
                 by + 14, NJ_ANCHOR_HCENTER | NJ_ANCHOR_TOP, avail, 0);
    ui_draw_text(g, action, medium, UI_COL_ROW_TITLE, bx + box_w / 2,
                 by + 14 + large->height + 8,
                 NJ_ANCHOR_HCENTER | NJ_ANCHOR_TOP, avail, 0);
    ui_draw_text(g, "Esc 取消", medium, UI_COL_DIM_TEXT, bx + box_w / 2,
                 by + box_h - 14, NJ_ANCHOR_HCENTER | NJ_ANCHOR_VCENTER,
                 avail, 0);
}

static void ui_draw_binds(nj_client_ui *ui, nj_gfx *g)
{
    const nj_font *medium = ui_font_medium();
    int px, py, pw, ph, rows_y, rh, vis, i;
    int count = ui_bind_count();

    if (!ui_draw_panel(ui, g, "按键映射", &px, &py, &pw, &ph)) return;

    ui_panel_rows(py, ph, count, &rows_y, &rh, &vis);
    ui->bind_visible = vis;
    ui_binds_clamp(ui);

    ui->bind_panel[0] = px;
    ui->bind_panel[1] = py;
    ui->bind_panel[2] = pw;
    ui->bind_panel[3] = ph;
    ui->bind_row_y = rows_y;
    ui->bind_row_h = rh;

    for (i = ui->bind_top; i < ui->bind_top + vis && i < count; i++) {
        int ry = rows_y + (i - ui->bind_top) * rh;
        int selected = i == ui->bind_row;
        int custom = ui->settings && ui->settings->binds[i][0] != '\0';
        const char *label = nj_client_bind_label(i);
        const char *name = ui_bind_effective(ui, i);
        int label_x = px + 24;
        int vx = px + pw - 24;
        int label_w;

        if (ry + rh > py + ph - 22) break;    /* keep the panel footer clear */
        if (selected) {
            ui_fill_round(g, px + 14, ry + 1, pw - 28, rh - 2, 6, UI_COL_SEL);
        }
        if (!label) label = "";
        if (!name) name = "";
        label_w = ui_text_width(medium, label);
        ui_draw_text(g, label, medium,
                     selected ? UI_COL_ROW_TITLE : UI_COL_PATH_TEXT, label_x,
                     ry + rh / 2, NJ_ANCHOR_LEFT | NJ_ANCHOR_VCENTER,
                     ui_max(20, vx - label_x - 16), 0);
        /* Defaults are dimmed; a player-chosen key stands out. */
        ui_draw_text(g, name, medium,
                     selected ? UI_COL_ROW_TITLE
                              : (custom ? UI_COL_PATH_TEXT : UI_COL_ROW_SUB),
                     vx, ry + rh / 2,
                     NJ_ANCHOR_RIGHT | NJ_ANCHOR_VCENTER,
                     ui_max(0, vx - (label_x + label_w + 16)), 0);
    }
    ui_draw_scrollbar(g, px + pw - 10, rows_y, vis * rh, count, ui->bind_top,
                      vis);
    ui_draw_text(g, "Enter 修改 · Backspace 恢复默认 · Esc 返回", medium,
                 UI_COL_ROW_SUB, px + 24, py + ph - 18,
                 NJ_ANCHOR_LEFT | NJ_ANCHOR_VCENTER, pw - 48, 0);
    ui_draw_capture(ui, g, px, py, pw, ph);
    ui_draw_panel_end(ui, g);
}

/* ------------------------------------------------------------------ */
/* Toast drawing                                                       */
/* ------------------------------------------------------------------ */
static void ui_draw_toast(nj_client_ui *ui, nj_gfx *g)
{
    const nj_font *medium = ui_font_medium();
    int w = ui->width;
    int text_w, box_w, box_h, x, y;

    if (!ui->toast_active) return;
    if ((nj_u4)(ui->now_ms - ui->toast_start) >= UI_TOAST_MS) {
        ui->toast_active = 0;
        return;
    }
    text_w = ui_text_width(medium, ui->toast);
    box_w = ui_min(w - 24, text_w + 32);
    if (box_w < 60) box_w = 60;
    box_h = medium->height + 12;
    x = (w - box_w) / 2;
    y = ui->footer_y - box_h - 12;
    if (y < ui->header_h + 4) y = ui->header_h + 4;
    if (y + box_h > ui->height) y = ui->height - box_h;

    ui_fill_round(g, x, y, box_w, box_h, 6, UI_COL_BTN);
    nj_gfx_set_color(g, UI_COL_PANEL_EDGE);
    nj_gfx_round_rect(g, x, y, box_w - 1, box_h - 1, 6, 6);
    ui_draw_text(g, ui->toast, medium, UI_COL_BTN_TEXT, w / 2,
                 y + box_h / 2, NJ_ANCHOR_HCENTER | NJ_ANCHOR_VCENTER,
                 box_w - 16, 0);
}

/* ------------------------------------------------------------------ */
/* Input: keyboard                                                     */
/* ------------------------------------------------------------------ */
/* F2 stars or unstars the selected row.  Favourites are user state: they
 * live in the settings, are saved like a settings edit, and survive the
 * 恢复默认 reset. */
static void ui_favorite_toggle(nj_client_ui *ui)
{
    nj_client_settings *s;
    const nj_lib_entry *e;
    char path[NJ_CLIENT_PATH_MAX];
    char msg[UI_TOAST_MAX];
    const char *name;
    int i, n, j;

    if (!ui || !ui->settings || !ui->lib || !ui->lib->entries) return;
    if (ui->library_sel < 0 || ui->library_sel >= ui_lib_count(ui)) return;
    e = &ui->lib->entries[ui->library_sel];
    if (!e->path[0]) return;
    if (!ui_copy(path, (int)sizeof(path), e->path)) return;
    s = ui->settings;
    n = s->favorite_count;
    if (n < 0) n = 0;
    if (n > NJ_CLIENT_FAVORITE_MAX) n = NJ_CLIENT_FAVORITE_MAX;
    for (i = 0; i < n; i++) {
        if (strcmp(s->favorites[i], path) == 0) break;
    }
    name = e->name[0] ? e->name : (e->file[0] ? e->file : path);
    if (i < n) {
        for (j = i; j + 1 < n; j++) {
            memcpy(s->favorites[j], s->favorites[j + 1],
                   (size_t)NJ_CLIENT_PATH_MAX);
        }
        s->favorites[n - 1][0] = '\0';
        s->favorite_count = n - 1;
        if (snprintf(msg, sizeof(msg), "已取消收藏「%s」", name) <= 0) {
            msg[0] = '\0';
        }
    } else {
        if (n >= NJ_CLIENT_FAVORITE_MAX) {
            /* Full: the oldest star makes room, like the recent list. */
            memmove(s->favorites[0], s->favorites[1],
                    (size_t)(NJ_CLIENT_FAVORITE_MAX - 1) *
                        (size_t)NJ_CLIENT_PATH_MAX);
            n = NJ_CLIENT_FAVORITE_MAX - 1;
        }
        if (!ui_copy(s->favorites[n], (int)sizeof(s->favorites[n]), path)) {
            return;
        }
        s->favorite_count = n + 1;
        if (snprintf(msg, sizeof(msg), "已收藏「%s」", name) <= 0) {
            msg[0] = '\0';
        }
    }
    ui_save_settings(ui);
    if (msg[0]) ui_note_set(ui, msg);
}

static void ui_key_library(nj_client_ui *ui, int key)
{
    int vis = ui->visible_rows > 0 ? ui->visible_rows : 1;

    if (ui->help_active) {
        /* The F1 sheet is modal: only its three closing keys do anything. */
        if (key == NJ_CLIENT_KEY_HELP || key == NJ_CLIENT_KEY_ESCAPE ||
            key == NJ_CLIENT_KEY_ENTER) {
            ui->help_active = 0;
        }
        return;
    }
    switch (key) {
    case NJ_CLIENT_KEY_UP:
        ui_lib_move(ui, -1);
        break;
    case NJ_CLIENT_KEY_DOWN:
        ui_lib_move(ui, 1);
        break;
    case NJ_CLIENT_KEY_PAGE_UP:
        ui_lib_move(ui, -vis);
        break;
    case NJ_CLIENT_KEY_PAGE_DOWN:
        ui_lib_move(ui, vis);
        break;
    case NJ_CLIENT_KEY_HOME:
        ui_lib_select_row(ui, 0);
        break;
    case NJ_CLIENT_KEY_END:
        ui_lib_select_row(ui, ui_view_rows(ui) - 1);
        break;
    case NJ_CLIENT_KEY_FAVORITE:
        ui_favorite_toggle(ui);
        break;
    case NJ_CLIENT_KEY_ENTER:
        ui_lib_activate(ui);
        break;
    case NJ_CLIENT_KEY_BACKSPACE:
        (void)ui_filter_backspace(ui);
        break;
    case NJ_CLIENT_KEY_ESCAPE:
        if (ui_filter_active(ui)) {
            ui_filter_clear(ui);      /* first Escape clears, second quits */
        } else {
            ui_request_quit(ui);
        }
        break;
    case NJ_CLIENT_KEY_HELP:
        ui->help_active = 1;
        break;
    default:
        break;
    }
}

static void ui_key_browser(nj_client_ui *ui, int key)
{
    int vis = ui->browser_visible > 0 ? ui->browser_visible : 1;
    switch (key) {
    case NJ_CLIENT_KEY_UP:
        ui_browser_move(ui, -1);
        break;
    case NJ_CLIENT_KEY_DOWN:
        ui_browser_move(ui, 1);
        break;
    case NJ_CLIENT_KEY_PAGE_UP:
        ui_browser_move(ui, -vis);
        break;
    case NJ_CLIENT_KEY_PAGE_DOWN:
        ui_browser_move(ui, vis);
        break;
    case NJ_CLIENT_KEY_HOME:
        ui_browser_set(ui, 0);
        break;
    case NJ_CLIENT_KEY_END:
        ui_browser_set(ui, ui_browser_row_count(ui) - 1);
        break;
    case NJ_CLIENT_KEY_ENTER:
        ui_browser_activate(ui);
        break;
    case NJ_CLIENT_KEY_ESCAPE:
        ui_browser_leave(ui, ui->browser_return_mode);
        break;
    case NJ_CLIENT_KEY_BACKSPACE:
        ui_browser_go_up(ui);
        break;
    default:
        break;
    }
}

static void ui_key_settings(nj_client_ui *ui, int key)
{
    int vis = ui->settings_visible > 0 ? ui->settings_visible
                                      : ui_setting_count();
    switch (key) {
    case NJ_CLIENT_KEY_UP:
        ui_settings_move(ui, -1);
        break;
    case NJ_CLIENT_KEY_DOWN:
        ui_settings_move(ui, 1);
        break;
    case NJ_CLIENT_KEY_PAGE_UP:
        ui_settings_move(ui, -vis);
        break;
    case NJ_CLIENT_KEY_PAGE_DOWN:
        ui_settings_move(ui, vis);
        break;
    case NJ_CLIENT_KEY_HOME:
        ui_settings_set(ui, 0);
        break;
    case NJ_CLIENT_KEY_END:
        ui_settings_set(ui, ui_setting_count() - 1);
        break;
    case NJ_CLIENT_KEY_LEFT:
        ui_settings_cycle(ui, -1);
        break;
    case NJ_CLIENT_KEY_RIGHT:
        ui_settings_cycle(ui, 1);
        break;
    case NJ_CLIENT_KEY_ENTER:
        ui_settings_activate(ui);
        break;
    case NJ_CLIENT_KEY_ESCAPE:
        ui->mode = NJ_CLIENT_MODE_LIBRARY;
        ui_lib_clamp(ui);
        break;
    default:
        break;
    }
}

static void ui_key_binds(nj_client_ui *ui, int key)
{
    int vis = ui->bind_visible > 0 ? ui->bind_visible : ui_bind_count();
    switch (key) {
    case NJ_CLIENT_KEY_UP:
        ui_binds_move(ui, -1);
        break;
    case NJ_CLIENT_KEY_DOWN:
        ui_binds_move(ui, 1);
        break;
    case NJ_CLIENT_KEY_PAGE_UP:
        ui_binds_move(ui, -vis);
        break;
    case NJ_CLIENT_KEY_PAGE_DOWN:
        ui_binds_move(ui, vis);
        break;
    case NJ_CLIENT_KEY_HOME:
        ui_binds_set(ui, 0);
        break;
    case NJ_CLIENT_KEY_END:
        ui_binds_set(ui, ui_bind_count() - 1);
        break;
    case NJ_CLIENT_KEY_ENTER:
        ui_bind_begin_capture(ui);
        break;
    case NJ_CLIENT_KEY_BACKSPACE:
        ui_bind_clear(ui);
        break;
    case NJ_CLIENT_KEY_ESCAPE:
        ui_binds_close(ui);
        break;
    default:
        break;
    }
}

/* ------------------------------------------------------------------ */
/* Input: library activation                                           */
/* ------------------------------------------------------------------ */
static void ui_lib_activate(nj_client_ui *ui)
{
    int n = ui_lib_count(ui);
    const nj_lib_entry *e;

    if (!ui || n <= 0) return;
    if (ui->library_sel < 0 || ui->library_sel >= n) return;
    e = &ui->lib->entries[ui->library_sel];
    if (!e->valid) {
        ui_note_set(ui, "无法运行这个 .jar");
        return;
    }
    if (!e->path[0]) {
        ui_note_set(ui, "无法运行：路径为空");
        return;
    }
    ui_emit_run(ui, e->path);
}

/* ------------------------------------------------------------------ */
/* Input: pointer                                                      */
/* ------------------------------------------------------------------ */
static void ui_pointer_library(nj_client_ui *ui, int x, int y)
{
    int i, index;

    for (i = 0; i < 3; i++) {
        if (ui_hovering(ui, ui->button_rect[i][0], ui->button_rect[i][1],
                        ui->button_rect[i][2], ui->button_rect[i][3])) {
            switch (i) {
            case 0:
                ui_browser_open(ui, 0, NJ_CLIENT_MODE_LIBRARY);
                break;
            case 1:
                ui_open_settings(ui);
                break;
            default:
                ui_request_quit(ui);
                break;
            }
            return;
        }
    }
    index = ui_lib_row_at(ui, x, y);
    if (index < 0) return;        /* outside, or a non-selectable header */
    if (index == ui_view_row_of_sel(ui)) {
        ui_lib_activate(ui);      /* click on the selection = double click */
    } else {
        ui_lib_select_row(ui, index);
    }
}

static void ui_pointer_browser(nj_client_ui *ui, int x, int y)
{
    int rows_y, rows_h, vis, index;

    if (ui->browser_pick_dir &&
        ui_hovering(ui, ui->pick_btn_rect[0], ui->pick_btn_rect[1],
                    ui->pick_btn_rect[2], ui->pick_btn_rect[3])) {
        ui_browser_pick(ui);
        return;
    }
    ui_browser_geom(ui, &rows_y, &rows_h, &vis);
    (void)vis;
    if (y < rows_y || y >= rows_y + rows_h) return;
    if (x < 0 || x >= ui->width) return;
    index = ui->browser_top + (y - rows_y) / UI_BROW_ROW_H;
    if (index < 0 || index >= ui_browser_row_count(ui)) return;
    if (index == ui->browser_sel) {
        ui_browser_activate(ui);
    } else {
        ui->browser_sel = index;
        ui_browser_clamp(ui);
    }
}

static void ui_pointer_settings(nj_client_ui *ui, int x, int y)
{
    int index;
    (void)x;
    if (ui->settings_panel[2] <= 0 || ui->settings_panel[3] <= 0) return;
    if (!ui_hovering(ui, ui->settings_panel[0], ui->settings_panel[1],
                     ui->settings_panel[2], ui->settings_panel[3])) {
        return;
    }
    if (ui->settings_row_h <= 0 || y < ui->settings_row_y) return;
    /* Ignore taps in the footer gap: only visible rows are clickable. */
    if (y >= ui->settings_row_y + ui->settings_visible * ui->settings_row_h) {
        return;
    }
    index = ui->settings_top + (y - ui->settings_row_y) / ui->settings_row_h;
    if (index < 0 || index >= ui_setting_count()) return;
    if (index == ui->settings_row) {
        ui_settings_activate(ui);
    } else {
        ui_settings_set(ui, index);
    }
}

static void ui_pointer_binds(nj_client_ui *ui, int x, int y)
{
    int index;
    (void)x;
    if (ui->capture_active) return;       /* the overlay swallows clicks */
    if (ui->bind_panel[2] <= 0 || ui->bind_panel[3] <= 0) return;
    if (!ui_hovering(ui, ui->bind_panel[0], ui->bind_panel[1],
                     ui->bind_panel[2], ui->bind_panel[3])) {
        return;
    }
    if (ui->bind_row_h <= 0 || y < ui->bind_row_y) return;
    if (y >= ui->bind_row_y + ui->bind_visible * ui->bind_row_h) return;
    index = ui->bind_top + (y - ui->bind_row_y) / ui->bind_row_h;
    if (index < 0 || index >= ui_bind_count()) return;
    if (index == ui->bind_row) {
        ui_bind_begin_capture(ui);
    } else {
        ui_binds_set(ui, index);
    }
}

/* ------------------------------------------------------------------ */
/* Public API                                                          */
/* ------------------------------------------------------------------ */
nj_client_ui *nj_client_ui_new(nj_client_settings *settings, nj_library *lib)
{
    nj_client_ui *ui = (nj_client_ui *)calloc(1, sizeof(*ui));
    if (!ui) return NULL;
    ui->settings = settings;
    ui->lib = lib;
    ui->mode = NJ_CLIENT_MODE_LIBRARY;
    ui->library_sel = -1;
    ui->browser_sel = -1;
    ui->pointer_x = -1;
    ui->pointer_y = -1;
    ui->settings_visible = ui_setting_count();
    ui->bind_visible = ui_bind_count();
    ui->capture_bind = -1;
    ui->browser_return_mode = NJ_CLIENT_MODE_LIBRARY;
    ui->action = NJ_CLIENT_ACTION_NONE;
    ui_layout_apply(ui, 720, 540);
    return ui;
}

void nj_client_ui_free(nj_client_ui *ui)
{
    if (!ui) return;
    ui_browser_free(ui);
    free(ui);
}

void nj_client_ui_resize(nj_client_ui *ui, int width, int height)
{
    if (!ui) return;
    ui_layout_apply(ui, width, height);
    ui_lib_clamp(ui);
    ui_browser_clamp(ui);
    ui_settings_clamp(ui);
    ui_binds_clamp(ui);
}

void nj_client_ui_tick(nj_client_ui *ui, nj_u4 now_ms)
{
    if (!ui) return;
    ui->now_ms = now_ms;
}

void nj_client_ui_draw(nj_client_ui *ui, nj_surface *surface)
{
    nj_gfx g;

    if (!ui || !surface || !surface->pixels) return;
    if (surface->width <= 0 || surface->height <= 0) return;

    ui_layout_apply(ui, surface->width, surface->height);
    nj_gfx_init(&g, surface);
    nj_gfx_clear(&g, UI_COL_BG);

    switch (ui->mode) {
    case NJ_CLIENT_MODE_BROWSER:
        ui_browser_clamp(ui);
        ui_draw_browser(ui, &g);
        break;
    case NJ_CLIENT_MODE_SETTINGS:
        ui_lib_clamp(ui);
        ui_draw_settings(ui, &g);
        break;
    case NJ_CLIENT_MODE_BINDS:
        ui_lib_clamp(ui);
        ui_draw_binds(ui, &g);
        break;
    default:
        ui_lib_clamp(ui);
        ui_draw_library(ui, &g);
        break;
    }
    ui_draw_toast(ui, &g);
    ui_clip_full(ui, &g);
}

void nj_client_ui_key(nj_client_ui *ui, int key, int down, int repeat)
{
    (void)repeat;
    if (!ui || !down) return;
    switch (ui->mode) {
    case NJ_CLIENT_MODE_BROWSER:
        ui_key_browser(ui, key);
        break;
    case NJ_CLIENT_MODE_SETTINGS:
        ui_key_settings(ui, key);
        break;
    case NJ_CLIENT_MODE_BINDS:
        /* While waiting for a key the embedder feeds capture_key() instead;
         * a navigation key that still gets here must not move or leave. */
        if (!ui->capture_active) ui_key_binds(ui, key);
        break;
    default:
        ui_key_library(ui, key);
        break;
    }
}

void nj_client_ui_char(nj_client_ui *ui, nj_u2 ch)
{
    if (!ui) return;
    switch (ui->mode) {
    case NJ_CLIENT_MODE_LIBRARY:
        if (ui->help_active) break;       /* the sheet swallows typing too */
        if (ui_filter_active(ui)) {
            /* While searching, letters and digits are search text.  R stays
             * the rescan key and keeps the filter, as the footer promises. */
            if (ch == 'r' || ch == 'R') ui_rescan(ui);
            else ui_filter_append(ui, ch);
            break;
        }
        if (ch == 'o' || ch == 'O') {
            ui_browser_open(ui, 0, NJ_CLIENT_MODE_LIBRARY);
        } else if (ch == 'r' || ch == 'R') {
            ui_rescan(ui);
        } else if (ch == 's' || ch == 'S') {
            ui_open_settings(ui);
        } else if (ch == 'q' || ch == 'Q') {
            ui_request_quit(ui);
        } else {
            ui_filter_append(ui, ch);
        }
        break;
    case NJ_CLIENT_MODE_BROWSER:
        if ((ch == 'p' || ch == 'P') && ui->browser_pick_dir) {
            ui_browser_pick(ui);
        }
        break;
    case NJ_CLIENT_MODE_SETTINGS:
        if (ch == 's' || ch == 'S') {
            ui->mode = NJ_CLIENT_MODE_LIBRARY;
            ui_lib_clamp(ui);
        }
        break;
    default:
        break;
    }
}

void nj_client_ui_pointer(nj_client_ui *ui, int x, int y, int down)
{
    if (!ui) return;
    ui->pointer_x = x;
    ui->pointer_y = y;
    if (!down) return;
    if (ui->help_active) return;          /* the sheet is modal for clicks */
    switch (ui->mode) {
    case NJ_CLIENT_MODE_BROWSER:
        ui_pointer_browser(ui, x, y);
        break;
    case NJ_CLIENT_MODE_SETTINGS:
        ui_pointer_settings(ui, x, y);
        break;
    case NJ_CLIENT_MODE_BINDS:
        ui_pointer_binds(ui, x, y);
        break;
    default:
        ui_pointer_library(ui, x, y);
        break;
    }
}

void nj_client_ui_pointer_move(nj_client_ui *ui, int x, int y)
{
    if (!ui) return;
    ui->pointer_x = x;
    ui->pointer_y = y;
}

void nj_client_ui_wheel(nj_client_ui *ui, int dy)
{
    if (!ui || dy == 0) return;
    if (ui->help_active) return;          /* the sheet is modal for the wheel */
    dy = ui_clamp_int(dy, -1000, 1000);
    switch (ui->mode) {
    case NJ_CLIENT_MODE_BROWSER:
        ui_browser_move(ui, -dy);
        break;
    case NJ_CLIENT_MODE_SETTINGS:
        ui_settings_move(ui, -dy);
        break;
    case NJ_CLIENT_MODE_BINDS:
        if (!ui->capture_active) ui_binds_move(ui, -dy);
        break;
    default:
        ui_lib_move(ui, -dy);
        break;
    }
}

void nj_client_ui_drop(nj_client_ui *ui, const char *path)
{
    char msg[UI_TOAST_MAX];

    if (!ui || !path || !path[0]) return;
    if (ui->help_active) return;          /* the sheet is modal for drops */
    if (ui_is_dir(path)) {
        if (!ui->settings) {
            ui_note_set(ui, "没有可用的设置，无法保存游戏目录");
            return;
        }
        if (!ui_copy(ui->settings->games_dir,
                     (int)sizeof(ui->settings->games_dir), path)) {
            ui_note_set(ui, "目录路径太长，无法使用");
            return;
        }
        (void)ui_copy(ui->settings->last_dir,
                      (int)sizeof(ui->settings->last_dir), path);
        ui_save_settings(ui);
        if (ui->lib) (void)nj_lib_scan_begin(ui->lib, path);
        ui_browser_free(ui);
        ui->mode = NJ_CLIENT_MODE_LIBRARY;
        ui->library_sel = 0;
        ui->library_top = 0;
        if (snprintf(msg, sizeof(msg), "游戏目录：%s", path) > 0) {
            ui_note_set(ui, msg);
        }
        return;
    }
    if (!nj_lib_is_jar_name(path)) {
        ui_note_set(ui, "不是可运行的 .jar");
        return;
    }
    if (ui->lib) {
        nj_lib_entry entry;
        memset(&entry, 0, sizeof(entry));
        if (!nj_lib_probe(path, &entry) || !entry.valid) {
            ui_note_set(ui, "不是可运行的 .jar");
            return;
        }
        (void)nj_lib_add(ui->lib, path);
    }
    ui_browser_free(ui);
    ui->mode = NJ_CLIENT_MODE_LIBRARY;
    ui_emit_run(ui, path);
}

void nj_client_ui_note(nj_client_ui *ui, const char *message)
{
    if (!ui || !message) return;
    ui_note_set(ui, message);
}

void nj_client_ui_note_game_exit(nj_client_ui *ui, const char *jar, int status)
{
    char msg[UI_TOAST_MAX];
    const char *base;

    if (!ui) return;
    base = ui_basename(jar);
    if (!base[0]) {
        if (status != 0) {
            snprintf(msg, sizeof(msg), "游戏已退出（状态 %d）", status);
        } else {
            snprintf(msg, sizeof(msg), "游戏已退出");
        }
    } else if (status != 0) {
        snprintf(msg, sizeof(msg), "《%s》已退出（状态 %d）", base, status);
    } else {
        snprintf(msg, sizeof(msg), "《%s》已退出", base);
    }
    ui_note_set(ui, msg);
}

void nj_client_ui_record_run(nj_client_ui *ui, const char *path)
{
    nj_client_settings *s;
    char copy[NJ_CLIENT_PATH_MAX];
    int i, n, j;

    if (!ui || !path || !path[0]) return;
    s = ui->settings;
    if (!s) return;
    if (!ui_copy(copy, (int)sizeof(copy), path)) return;
    n = s->recent_count;
    if (n < 0) n = 0;
    if (n > NJ_CLIENT_RECENT_MAX) n = NJ_CLIENT_RECENT_MAX;
    for (i = 0; i < n; i++) {
        if (strcmp(s->recent[i], copy) == 0) break;
    }
    if (i >= n) {
        if (n < NJ_CLIENT_RECENT_MAX) n++;
        i = n - 1;              /* the slot the oldest entry falls off */
    }
    for (j = i; j > 0; j--) {
        memcpy(s->recent[j], s->recent[j - 1], (size_t)NJ_CLIENT_PATH_MAX);
    }
    memcpy(s->recent[0], copy, strlen(copy) + 1u);
    s->recent_count = n;
    ui_save_settings(ui);
}

int nj_client_ui_take(nj_client_ui *ui, nj_client_action *action, char *jar,
                      int jar_cap)
{
    int taken;
    if (!ui || ui->action == NJ_CLIENT_ACTION_NONE) return 0;
    taken = ui->action;
    ui->action = NJ_CLIENT_ACTION_NONE;
    if (action) *action = (nj_client_action)taken;
    if (taken == NJ_CLIENT_ACTION_RUN) {
        if (jar && jar_cap > 0) {
            snprintf(jar, (size_t)jar_cap, "%s", ui->action_jar);
        }
        nj_client_ui_record_run(ui, ui->action_jar);
        ui->action_jar[0] = '\0';
    }
    return 1;
}

int nj_client_ui_quit(const nj_client_ui *ui)
{
    return ui ? ui->quit : 0;
}

int nj_client_ui_row_rect(const nj_client_ui *ui, int index, int *x, int *y,
                          int *w, int *h)
{
    int rows, top, row, row_y;
    if (!ui || ui->mode != NJ_CLIENT_MODE_LIBRARY) return 0;
    rows = ui_view_rows(ui);
    /* A filter flattens the list: callers keep passing the ENTRY index (the
     * historical contract), so translate it to the filtered position.  With
     * no filter the index is the selectable row, which equals the entry index
     * when the recent section is absent. */
    row = index;
    if (ui_filter_active(ui)) {
        row = ui_filter_pos_of(ui, index);
    }
    if (row < 0 || row >= rows) return 0;
    if (ui->visible_rows <= 0 || ui->row_h <= 0 || ui->width < 16) return 0;
    top = ui->library_top;
    row_y = ui->list_y + ui_view_y(ui, row) - ui_view_base(ui, top);
    if (row_y < ui->list_y || row_y + ui->row_h > ui->list_y + ui->list_h) {
        return 0;
    }
    if (x) *x = 8;
    if (y) *y = row_y;
    if (w) *w = ui->width - 16;
    if (h) *h = ui->row_h;
    return 1;
}

int nj_client_ui_selected(const nj_client_ui *ui)
{
    return ui ? ui->library_sel : -1;
}

int nj_client_ui_selected_favorite(const nj_client_ui *ui)
{
    if (!ui || !ui->lib || !ui->lib->entries) return 0;
    if (ui->library_sel < 0 || ui->library_sel >= ui_lib_count(ui)) return 0;
    return ui_path_is_favorite(ui, ui->lib->entries[ui->library_sel].path);
}

int nj_client_ui_mode(const nj_client_ui *ui)
{
    return ui ? ui->mode : NJ_CLIENT_MODE_LIBRARY;
}

const char *nj_client_ui_filter(const nj_client_ui *ui)
{
    return ui ? ui->filter : "";
}

int nj_client_ui_help_visible(const nj_client_ui *ui)
{
    return (ui && ui->help_active) ? 1 : 0;
}

int nj_client_ui_browser_selected(const nj_client_ui *ui)
{
    return ui ? ui->browser_sel : -1;
}

int nj_client_ui_selected_bind(const nj_client_ui *ui)
{
    if (!ui || ui->mode != NJ_CLIENT_MODE_BINDS) return -1;
    return ui->bind_row;
}

int nj_client_ui_capturing(const nj_client_ui *ui)
{
    return (ui && ui->capture_active) ? 1 : 0;
}

void nj_client_ui_capture_cancel(nj_client_ui *ui)
{
    if (!ui) return;
    ui->capture_active = 0;
    ui->capture_bind = -1;
}

int nj_client_ui_capture_key(nj_client_ui *ui, const char *sdl_key_name)
{
    nj_client_settings *s;
    const char *label;
    char msg[UI_TOAST_MAX];
    int slot, i, len;

    if (!ui || !ui->capture_active) return 0;
    s = ui->settings;
    slot = ui->capture_bind;
    if (!s || slot < 0 || slot >= NJ_PORT_BIND_COUNT) {
        ui->capture_active = 0;
        ui->capture_bind = -1;
        return 0;
    }
    label = nj_client_bind_label(slot);

    /* Esc closes the overlay; it never binds a slot. */
    if (ui_name_eq(sdl_key_name, "Escape")) {
        ui->capture_active = 0;
        ui->capture_bind = -1;
        ui_note_set(ui, "已取消");
        return 0;
    }
    /* Pressing the slot's own default drops the override instead of pinning
     * a key that already matches it. */
    if (ui_name_eq(sdl_key_name, nj_client_bind_default_key(slot))) {
        s->binds[slot][0] = '\0';
        ui_save_settings(ui);
        ui->capture_active = 0;
        ui->capture_bind = -1;
        ui_note_set(ui, "已恢复默认");
        return 1;
    }
    if (ui_name_reserved(sdl_key_name)) {
        ui_note_set(ui, "该键是客户端快捷键");
        return 0;
    }
    if (ui_name_modifier(sdl_key_name)) {
        ui_note_set(ui, "修饰键不能单独绑定");
        return 0;
    }
    if (!ui_name_storable(sdl_key_name)) {
        len = sdl_key_name ? (int)strlen(sdl_key_name) : 0;
        ui_note_set(ui, len >= NJ_CLIENT_BIND_NAME_MAX ? "按键名太长"
                                                       : "无法识别的按键");
        return 0;
    }
    /* One key, one action: compare with every other slot's effective name. */
    for (i = 0; i < NJ_PORT_BIND_COUNT; i++) {
        const char *other;
        if (i == slot) continue;
        other = ui_bind_effective(ui, i);
        if (!other) continue;
        if (!ui_name_eq(sdl_key_name, other)) continue;
        snprintf(msg, sizeof(msg), "该键已用于「%s」",
                 nj_client_bind_label(i) ? nj_client_bind_label(i) : "");
        ui_note_set(ui, msg);
        return 0;
    }

    len = (int)strlen(sdl_key_name);
    memcpy(s->binds[slot], sdl_key_name, (size_t)len + 1u);
    ui_save_settings(ui);
    ui->capture_active = 0;
    ui->capture_bind = -1;
    snprintf(msg, sizeof(msg), "%s：%s", label ? label : "", sdl_key_name);
    ui_note_set(ui, msg);
    return 1;
}
