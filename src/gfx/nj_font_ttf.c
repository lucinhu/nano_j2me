/*
 * nano_j2me - nj_font_ttf.c
 *
 * Render MIDP text through a TrueType face instead of the generated bitmap
 * tables.  Part of nj_gfx.h's font registry; the rasteriser itself is
 * src/gfx/nj_ttf.c.
 *
 * Design notes
 * ------------
 *  - The bitmap face is never removed, it is demoted.  Every slot keeps its
 *    builtin face as `fallback`, so a character the supplied font has no glyph
 *    for (a hanzi outside the font's coverage, or a dingbat the face simply
 *    lacks) still renders through the CJK table rather than as a blank cell.
 *    A font that renders nothing reads as a blank screen, not as a missing
 *    font, and that distinction is worth more than the bytes it costs.
 *
 *  - The metrics come from the bitmap face, not from the TrueType font.  This
 *    is the one decision in here that is not obvious, and it is deliberate:
 *    MIDlet layout is written against Font.getHeight().  A game that puts a
 *    status bar at y = height - 16, or wraps an Alert into 16-pixel lines,
 *    silently re-flows every screen if the face reports 19.  The user asked
 *    for a different typeface, not for a different layout, so the face is
 *    fitted into the box the bitmap face already occupies - see em_for_box().
 *
 *  - All state is in caller-supplied memory.  There is no static here and none
 *    in the rasteriser; the emulator hands over its host allocator, whose
 *    blocks are released by nj_vm_destroy(), so this module needs no teardown
 *    and two emulators in one process cannot see each other's faces.
 *
 *  - A font that fails to parse leaves the font set exactly as it was.  Half
 *    the sizes upgraded and half not is a state no caller can reason about,
 *    and it would be reached by a corrupt file rather than by anything the
 *    user did.
 */
#include "nj2me/nj_gfx.h"
#include "nj2me/nj_vm.h"

#if NJ_ENABLE_TTF

#include "nj_internal.h"
#include "nj_ttf.h"

/*
 * Rasterised glyphs kept per face.
 *
 * A face is built per (size, style) slot, so this multiplies by six.  Most of
 * an entry's bytes are its coverage rows - a 16-pixel em costs about 16x20 -
 * so 128 entries is roughly 50 KB per face and 300 KB for the whole set; the
 * measured figure is nj_ttf_sizeof(128) == 51544 bytes (16496 with no glyph
 * cache at all, and clamped at 4096 entries).  That is desktop RAM, which is
 * the reason the TrueType path is opt-in at runtime and absent from the build
 * entirely when NJ_ENABLE_TTF is 0.
 */
#define TTF_CACHE_GLYPHS 128

/* How many times em_for_box() may re-aim before settling for the closest. */
#define TTF_EM_PASSES 4

/* The MIDP size constant behind each fontset size index.  Index order is
 * nj_font.c's: 0 = SMALL, 1 = MEDIUM, 2 = LARGE. */
static const int s_size_of_index[3] = {
    NJ_FONT_SIZE_SMALL,
    NJ_FONT_SIZE_MEDIUM,
    NJ_FONT_SIZE_LARGE
};

/*
 * The em size that makes the face report `box->height` as its line height.
 *
 * A TrueType line height is proportional to the em size, so measuring once at
 * any em gives the ratio and one more open at the corrected value lands on it;
 * the loop is there because rounding to whole pixels can leave a pixel of
 * error that a second step removes.  Only header tables are touched, so a pass
 * costs a parse and no rasterisation.
 *
 * Returns 0 if the face cannot be opened or reports a degenerate line height,
 * which the caller treats as "this font is unusable" rather than as a value.
 */
static int em_for_box(void *face_mem, nj_size face_bytes,
                      const nj_u1 *data, nj_u4 len,
                      const nj_font *box, int style, int size_const)
{
    int em = (box->height > 0) ? box->height : 16;
    int pass;

    for (pass = 0; pass < TTF_EM_PASSES; pass++) {
        nj_font probe;
        int natural;
        long want;

        if (!nj_ttf_open(face_mem, face_bytes, data, len, em)) {
            return 0;
        }
        if (!nj_ttf_font((nj_ttf *)face_mem, &probe, style, size_const)) {
            return 0;
        }
        natural = probe.ascent + probe.descent;
        if (natural <= 0) {
            return 0;
        }
        if (natural == box->height) {
            return em;
        }
        /* Aim at the em that would scale `natural` onto the box. */
        want = ((long)em * (long)box->height + (long)(natural / 2)) / natural;
        if (want < 1) {
            want = 1;
        }
        if ((int)want == em) {
            break;   /* whole-pixel rounding has nothing left to give */
        }
        em = (int)want;
    }
    return em;
}

int nj_fontset_use_ttf(nj_fontset *fs, const nj_u1 *data, nj_u4 len,
                       nj_font_alloc_fn alloc, void *ud)
{
    nj_ttf *faces[NJ_FONTSET_SLOTS];
    int size_const[NJ_FONTSET_SLOTS];
    int style_const[NJ_FONTSET_SLOTS];
    nj_size face_bytes;
    int slot;
    int si;
    int st;
    int built = 0;

    if (fs == NULL || data == NULL || len == 0u || alloc == NULL) {
        return 0;
    }
    if (!fs->initialized) {
        nj_fontset_init(fs);
    }

    face_bytes = nj_ttf_sizeof(TTF_CACHE_GLYPHS);
    if (face_bytes == 0u) {
        return 0;
    }

    /*
     * Build every face before touching the font set, so a failure part way
     * through cannot leave some sizes upgraded and others not.  The faces are
     * allocated as we go and kept on failure too: they are tracked host blocks
     * that nj_vm_destroy() releases, so abandoning them costs nothing beyond
     * holding a few hundred KB until the emulator exits.
     */
    for (si = 0; si < 3; si++) {
        for (st = 0; st < NJ_FONT_SLOT_STYLES; st++) {
            const nj_font *box;
            void *mem;
            int em;

            slot = si * NJ_FONT_SLOT_STYLES + st;
            if (slot >= NJ_FONTSET_SLOTS) {
                break;
            }
            size_const[slot] = s_size_of_index[si];
            style_const[slot] = st ? NJ_FONT_STYLE_BOLD : NJ_FONT_STYLE_PLAIN;
            box = nj_font_builtin(size_const[slot], style_const[slot]);
            if (box == NULL) {
                return 0;
            }

            mem = alloc(ud, face_bytes);
            if (mem == NULL) {
                return 0;
            }
            em = em_for_box(mem, face_bytes, data, len, box,
                            style_const[slot], size_const[slot]);
            if (em <= 0) {
                return 0;
            }
            faces[slot] = nj_ttf_open(mem, face_bytes, data, len, em);
            if (faces[slot] == NULL) {
                return 0;
            }
            built++;
        }
    }
    if (built != NJ_FONTSET_SLOTS) {
        return 0;
    }

    /* Everything parsed: commit, slot by slot. */
    for (slot = 0; slot < NJ_FONTSET_SLOTS; slot++) {
        const nj_font *box = nj_font_builtin(size_const[slot],
                                             style_const[slot]);
        nj_font *dst = &fs->fonts[slot];

        if (!nj_ttf_font(faces[slot], dst, style_const[slot],
                         size_const[slot])) {
            /* Cannot happen for a face that opened, but a half-written slot
             * is worse than a bitmap one, so fall back rather than trust it. */
            nj_fontset_init(fs);
            return 0;
        }
        /*
         * Height and leading come from the bitmap box, so Font.getHeight() -
         * and every layout the MIDlet already committed to - is unchanged.
         *
         * Ascent and descent deliberately do NOT.  blit_glyph() places a
         * glyph's first row at `baseline - face->ascent`, and the rasteriser
         * laid the bitmap out from its own ascent line; overwriting that with
         * the box's value moves every glyph by (box->ascent - face->ascent)
         * pixels and can push the last row out of the box.  em_for_box()
         * already aims at a face whose ascent+descent matches the box height,
         * so leaving the face's own split in place keeps the total the same
         * while putting the ink where the rasteriser meant it to go.
         */
        dst->height  = box->height;
        dst->leading = box->leading;
        /* The bitmap face, CJK provider and all, catches what TTF cannot. */
        dst->fallback = box;
    }
    return NJ_FONTSET_SLOTS;
}

/* ------------------------------------------------------------------ */
/* Reading a face through the port                                     */
/* ------------------------------------------------------------------ */

/*
 * Read a whole font file, leaving it in one exactly-sized VM host block.
 *
 * Through the port's file hooks rather than stdio: a platform whose fonts live
 * in flash then uses the same call as one with a filesystem, and a freestanding
 * build has no stdio to use in the first place.
 *
 * The reading buffer comes from the port's allocator, which has a matching
 * free, so the doubling copies can be handed back as we go; only the final
 * block stays, and it stays in the VM's host registry because a face borrows
 * the bytes instead of copying them.  Growing directly in host blocks would
 * retain every intermediate size until nj_vm_destroy() - about a megabyte for
 * a 320 KB font - and a font set that outlived its own file would be reading
 * freed memory.
 */
static const nj_u1 *read_face(nj_vm *vm, const char *path, nj_u4 *len_out)
{
    const nj_port *port = vm->port;
    nj_file f;
    nj_u1 *tmp;
    nj_u1 *out;
    nj_u4 cap;
    nj_u4 used = 0u;

    *len_out = 0u;
    /*
     * Only the file hooks are required.  nj_port.h documents alloc/free as
     * optional ("when NULL the runtime uses malloc/free"), and a port that
     * leaves them NULL - the headless one does - is not broken.  Demanding
     * them here made --font fail on every such port with a message that named
     * neither the port nor the missing hook.  The scratch buffer comes from
     * the VM's own tracked allocator instead, which is always available and is
     * what the final copy below already used.
     */
    if (port == NULL || port->file_open == NULL || port->file_read == NULL
        || port->file_close == NULL) {
        return NULL;
    }

    memset(&f, 0, sizeof(f));
    if (port->file_open(port->ud, path, "rb", &f) != NJ_OK) {
        return NULL;
    }

    cap = 64u * 1024u;
    tmp = (nj_u1 *)nj_host_alloc(vm, cap);
    if (tmp == NULL) {
        port->file_close(port->ud, &f);
        return NULL;
    }

    for (;;) {
        nj_s4 got;

        if (used == cap) {
            nj_u1 *nb = (nj_u1 *)nj_host_alloc(vm, cap * 2u);

            if (nb == NULL) {
                nj_host_free(vm, tmp);
                port->file_close(port->ud, &f);
                return NULL;
            }
            memcpy(nb, tmp, used);
            nj_host_free(vm, tmp);
            tmp = nb;
            cap *= 2u;
        }
        got = port->file_read(port->ud, &f, tmp + used, (nj_u4)(cap - used));
        if (got < 0) {
            nj_host_free(vm, tmp);
            port->file_close(port->ud, &f);
            return NULL;
        }
        if (got == 0) {
            break;
        }
        used += (nj_u4)got;
    }
    port->file_close(port->ud, &f);

    if (used == 0u) {
        nj_host_free(vm, tmp);
        return NULL;
    }
    out = (nj_u1 *)nj_host_alloc(vm, used);
    if (out != NULL) {
        memcpy(out, tmp, used);
        *len_out = used;
    }
    nj_host_free(vm, tmp);
    return out;
}

static void *vm_font_alloc(void *ud, nj_size n)
{
    return nj_host_alloc((nj_vm *)ud, n);
}

int nj_vm_use_font_file(nj_vm *vm, const char *path)
{
    const nj_u1 *data;
    nj_u4 len = 0u;

    if (vm == NULL || path == NULL || path[0] == '\0') {
        return 0;
    }
    data = read_face(vm, path, &len);
    if (data == NULL) {
        return 0;
    }
    return nj_fontset_use_ttf(&vm->fonts, data, len, vm_font_alloc, vm)
               == NJ_FONTSET_SLOTS
           ? 1
           : 0;
}

#else  /* !NJ_ENABLE_TTF */

/*
 * Built without TrueType.  The symbol still exists so a caller needs no
 * #ifdef of its own, and it fails the one way that is true: this build cannot
 * use a font file.  Saying so is better than the caller silently running on
 * the built-in font and reporting success.
 */
int nj_fontset_use_ttf(nj_fontset *fs, const nj_u1 *data, nj_u4 len,
                       nj_font_alloc_fn alloc, void *ud)
{
    (void)fs;
    (void)data;
    (void)len;
    (void)alloc;
    (void)ud;
    return 0;
}

int nj_vm_use_font_file(nj_vm *vm, const char *path)
{
    (void)vm;
    (void)path;
    return 0;
}

#endif /* NJ_ENABLE_TTF */
