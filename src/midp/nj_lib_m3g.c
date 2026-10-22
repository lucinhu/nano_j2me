/*
 * nano_j2me - nj_lib_m3g.c
 *
 * JSR 184 (Mobile 3D Graphics), step one: the classes, the scene graph and
 * the transform maths.
 *
 * Why this exists
 * ---------------
 * Three collected titles - 都市摩天楼_6192, 极品飞车14之热力追踪中文版_4160 and
 * 极品飞车16：亡命狂飙_1519 - died on `NoClassDefFoundError` for m3g and were
 * the last class-level gap in the corpus.  Declaring the classes and letting
 * the calls do nothing was rejected: that moves the failure from "no such
 * class" to a wrong picture, and a wrong picture looks like it works.
 *
 * So this file does real work:
 *
 *   - `Transform` is a real 4x4 matrix with a real inverse (Gauss-Jordan on
 *     the augmented matrix, then a normalise of the rotation basis).
 *   - `Transformable` keeps its transform, translation, scale and orientation
 *     in that matrix, so `setScale`/`setTranslation`/`setOrientation` compose
 *     exactly as the specification says they do.
 *   - `Node`/`Group`/`World` are a real scene graph: parents are recorded,
 *     children are a java.util.Vector, and addChild refuses a second parent
 *     the way the JSR requires.
 *   - `Graphics3D` records the target, viewport and camera, and `render()`
 *     walks the graph and rasterises nothing yet - that is step two, and it
 *     is deliberately visible as an empty 3D view rather than as a lie.
 *   - `Loader.load()` throws `IOException`, because there is no .m3g parser
 *     yet and a MIDlet can handle an IOException; it cannot handle silence.
 *
 * Every module of state lives in a field of the object it belongs to, so
 * there is no writable static anywhere and two VMs cannot see each other.
 *
 * Compiled out with -DNJ_ENABLE_M3G=0: the tables below are then empty and
 * every class is simply absent, which is the honest answer for a build that
 * left the module out.
 */
#include "nj_internal.h"
#include "nj_midp.h"
#include "nj2me/nj_inflate.h"

#include <stdio.h>
#include <stdlib.h>

#include <math.h>
#include <string.h>


/*
 * ---------------------------------------------------------------------------
 * The .m3g container.
 * ---------------------------------------------------------------------------
 *
 * JSR 184 files start with a twelve byte identifier and are then a chain of
 * sections that must tile the file exactly:
 *
 *     0..11   the identifier: AB 4A "JSR184" BB 0D 0A 1A 0A
 *
 *     then, repeatedly:  id (1 byte), compressed length (u32 little endian,
 *                        the nine byte header included), uncompressed length
 *                        (u32 little endian), payload
 *
 * Everything above is checked here and nothing else.  That is deliberate: this
 * function exists to turn "the file is not a .m3g" and "the file is a .m3g
 * whose section 3 is truncated" into two different answers, because the second
 * one is our bug and the first one is the game's.  Interpreting the payloads is
 * the next step and is not pretended here.
 */
#define NJ_M3G_HEADER 12u
#define NJ_M3G_FILE_SECTION 30u

static const nj_u1 m3g_ident[12] = {
    0xabu, 0x4au, 0x53u, 0x52u, 0x31u, 0x38u,
    0x34u, 0xbbu, 0x0du, 0x0au, 0x1au, 0x0au
};

static nj_u4 le32(const nj_u1 *p)
{
    return (nj_u4)p[0] | ((nj_u4)p[1] << 8) |
           ((nj_u4)p[2] << 16) | ((nj_u4)p[3] << 24);
}

/*
 * The colours of JSR 184: a ColorRGB is three bytes and a ColorRGBA four, in
 * the order Red, Green, Blue (and Alpha).  The corpus settles the order of the
 * fourth byte - every one of the 594 Materials it holds has 0xFF there, and 54
 * of its 56 Backgrounds have it in the last byte - so it is an alpha and it
 * comes last.  This runtime stores a colour as 0xAARRGGBB.
 */
static nj_u4 m3g_rgb(const nj_u1 *p)
{
    return 0xFF000000u | ((nj_u4)p[0] << 16) | ((nj_u4)p[1] << 8) | (nj_u4)p[2];
}

static nj_u4 m3g_rgba(const nj_u1 *p)
{
    return ((nj_u4)p[3] << 24) | ((nj_u4)p[0] << 16) | ((nj_u4)p[1] << 8) |
           (nj_u4)p[2];
}

/*
 * Where a Node's own data starts.
 *
 * Every Node in the corpus - Mesh, Camera, Light, Sprite, Texture2D - is
 * laid out as the twelve byte Object3D header, a byte saying whether the
 * forty byte component transform follows, three flag bytes, the four byte
 * sentinel 0xFFFFFFFF and two more bytes; the type's own fields start after
 * that, at offset 22 without a transform and 62 with one.  The readings below
 * were each checked against the corpus by requiring them to consume the
 * object exactly, which is how the ten byte prefix was found: reading the
 * type's fields at 12 left ten bytes unaccounted for in all 152 Cameras and
 * all 110 Lights.
 */
/*
 * Where a Transformable's own data starts: the same twelve byte header and the
 * same optional forty byte component transform, but no Node prefix - a
 * Texture2D and a VertexBuffer are not Nodes and carry no flags or sentinel.
 */
static nj_u4 m3g_xform_data(const nj_u1 *data, nj_u4 len)
{
    nj_u4 base = 12u;

    if (len > 12u && (data[12] & 0x01u)) {
        base += 40u;
    }
    return base;
}

static nj_u4 m3g_node_data(const nj_u1 *data, nj_u4 len)
{
    nj_u4 base = 12u + 10u;             /* the Object3D header, then the prefix */

    if (len > 12u && (data[12] & 0x01u)) {
        base += 40u;
    }
    return base;
}

#define M3G_FAIL(...)                                                     \
    do {                                                                  \
        if (err && errcap) {                                              \
            snprintf(err, errcap, __VA_ARGS__);                           \
        }                                                                 \
        return NJ_ERR_FORMAT;                                            \
    } while (0)

/*
 * Walk a .m3g container.
 *
 * The layout below was read off 975 real files in the collected corpus, not
 * off a specification: the first attempt at this function used a version byte
 * at offset 0 and big-endian section lengths, and every real file was then
 * rejected as "not JSR 184" - which is how a whole format came to be written
 * down wrongly in these comments.  What the files actually say:
 *
 *   0..11   the identifier, AB 4A "JSR184" BB 0D 0A 1A 0A
 *
 *   then, repeatedly, a section:
 *     Byte     compression scheme   (0 stored, 1 zlib; 2..255 are reserved)
 *     UInt32   total section length (little endian, this header included)
 *     UInt32   uncompressed length  (little endian, the Objects field after
 *                                    decompression; 0 means "ignore me")
 *     Byte[]   Objects              (total length - 13 bytes)
 *     UInt32   Adler32 checksum     (little endian, over every byte above)
 *
 * The chain tiles every one of the 975 real files exactly, 811 of the sections
 * are zlib and 1344 are stored, and every one of the 2155 inflates or reads
 * back to precisely its declared uncompressed length.
 *
 * The four bytes after the Objects field are that checksum.  They were a
 * mystery for one round because this function used to treat the whole
 * post-header payload as objects: the object walk then ran into the checksum
 * and reported nonsense types.  Checking the checksum is not done here - a
 * loader is allowed to skip it - but the field has to be accounted for, or
 * every object chain is four bytes too long.
 */
#define NJ_M3G_TYPE_EXTERNAL_REF 255u
/*
 * An Object3D header has at least twelve bytes: userID, animation-track
 * count, and user-parameter count. Nonempty lists extend it; type decoders
 * use offsets relative to this minimum after m3g_object_header_size skips
 * those lists. Header objects and external references have their own layout.
 */
#define NJ_M3G_OBJECT_HEADER     12u
#define NJ_M3G_T_ANIMATION_TRACK  2u
#define NJ_M3G_T_GROUP        9u
#define NJ_M3G_T_POLYGON_MODE     8u
#define NJ_M3G_T_IMAGE2D         10u
#define NJ_M3G_T_MESH            14u
#define NJ_M3G_T_VERTEX_ARRAY    20u
#define NJ_M3G_T_VERTEX_BUFFER   21u
#define NJ_M3G_T_TRIANGLE_STRIP_ARRAY 11u
#define NJ_M3G_T_WORLD           22u
#define NJ_M3G_T_APPEARANCE        3u
#define NJ_M3G_T_BACKGROUND        4u
#define NJ_M3G_T_CAMERA            5u
#define NJ_M3G_T_COMPOSITING_MODE  6u
#define NJ_M3G_T_FOG               7u
#define NJ_M3G_T_LIGHT            12u
#define NJ_M3G_T_MATERIAL         13u
#define NJ_M3G_T_TEXTURE2D        17u
#define NJ_M3G_T_SPRITE           18u

/*
 * The object types that are Nodes, and therefore start with the Node header:
 * an enable byte whose bit 0 says a transform follows.  Everything else in the
 * format begins with its own fields at byte 12, and reading a transform out of
 * one of those would be reading its data.
 */
static int m3g_type_is_node(nj_u1 type)
{
    return type == 5u  || type == 9u  || type == 12u || type == 14u ||
           type == 15u || type == 16u || type == 18u || type == 22u;
}

/* The standalone container decoder also reads floats with M3G disabled. */
static float bits2f(nj_s4 i)
{
    union { float f; nj_s4 i; } u;
    u.i = i;
    return u.f;
}

#if NJ_ENABLE_M3G
static void m3g_image_dirty(nj_vm *vm, nj_ref img);
static nj_ref m3g_image_mips(nj_vm *vm, nj_ref img);
#endif

#if NJ_ENABLE_M3G
/*
 * A Node's transform, as the file stores it: translation (3 floats), scale
 * (3 floats) and orientation (angle in radians followed by the axis), which
 * compose as T * R * S.  The runtime keeps a Java matrix, so the result is
 * row-major with the translation in the last column - the layout
 * setTranslation() writes and jr_setup() multiplies.
 */
static void m3g_node_matrix(const nj_u1 *d, float *m)
{
    float tx, ty, tz, sx, sy, sz, ang, ax, ay, az;
    float len, sn, cs, om, r[9];
    int k;

    tx = bits2f((nj_s4)le32(d));      ty = bits2f((nj_s4)le32(d + 4u));
    tz = bits2f((nj_s4)le32(d + 8u)); sx = bits2f((nj_s4)le32(d + 12u));
    sy = bits2f((nj_s4)le32(d + 16u)); sz = bits2f((nj_s4)le32(d + 20u));
    ang = bits2f((nj_s4)le32(d + 24u));
    ax = bits2f((nj_s4)le32(d + 28u)); ay = bits2f((nj_s4)le32(d + 32u));
    az = bits2f((nj_s4)le32(d + 36u));
    len = sqrtf(ax * ax + ay * ay + az * az);
    if (!(len > 0.0f)) { ax = 0.0f; ay = 0.0f; az = 1.0f; len = 1.0f; }
    ax /= len; ay /= len; az /= len;
    /*
     * The file's angle is in degrees, the same unit postRotate() takes: the
     * meshes of the corpus carry 0, 90.00 and 90.39 there, and 90.39 radians
     * would be a 5178 degree turn.
     */
    ang *= 3.14159265358979f / 180.0f;
    sn = sinf(ang); cs = cosf(ang); om = 1.0f - cs;
    r[0] = om * ax * ax + cs;      r[1] = om * ax * ay - sn * az;
    r[2] = om * ax * az + sn * ay; r[3] = om * ax * ay + sn * az;
    r[4] = om * ay * ay + cs;      r[5] = om * ay * az - sn * ax;
    r[6] = om * ax * az - sn * ay; r[7] = om * ay * az + sn * ax;
    r[8] = om * az * az + cs;
    for (k = 0; k < 4; k++) { m[k] = 0.0f; m[4 + k] = 0.0f; m[8 + k] = 0.0f; }
    m[0] = r[0] * sx; m[1] = r[1] * sy; m[2] = r[2] * sz;
    m[4] = r[3] * sx; m[5] = r[4] * sy; m[6] = r[5] * sz;
    m[8] = r[6] * sx; m[9] = r[7] * sy; m[10] = r[8] * sz;
    m[3] = tx; m[7] = ty; m[11] = tz;
    m[12] = m[13] = m[14] = 0.0f; m[15] = 1.0f;
}
#endif

#define NJ_M3G_SECTION 9u
#define NJ_M3G_SECTION_MIN 13u   /* nine byte header + four byte Adler32 */

nj_status nj_m3g_container_probe(const nj_u1 *p, nj_u4 n,
                                 nj_u1 *ids, nj_u4 id_count,
                                 char *err, nj_u4 errcap)
{
    nj_u4 off, nids = 0;

    if (err && errcap) { err[0] = '\0'; }
    if (!p) { M3G_FAIL("no bytes"); }
    if (n < NJ_M3G_FILE_SECTION) {
        M3G_FAIL("only %lu byte(s): the file header alone is %u",
                 (unsigned long)n, (unsigned)NJ_M3G_FILE_SECTION);
    }
    if (memcmp(p, m3g_ident, sizeof(m3g_ident)) != 0) {
        M3G_FAIL("this is not a JSR 184 file: it starts %02x %02x %02x %02x",
                 (unsigned)p[0], (unsigned)p[1], (unsigned)p[2], (unsigned)p[3]);
    }
    off = NJ_M3G_HEADER;
    while (off < n) {
        nj_u4 clen, ulen;
        if (n - off < NJ_M3G_SECTION) {
            M3G_FAIL("a section header at %lu is cut short: %lu byte(s) left",
                     (unsigned long)off, (unsigned long)(n - off));
        }
        clen = le32(p + off + 1u);
        ulen = le32(p + off + 5u);
        if (p[off] > 1u) {
            M3G_FAIL("the section at %lu uses compression scheme %u; only 0 (stored) and 1 (zlib) exist",
                     (unsigned long)off, (unsigned)p[off]);
        }
        if (clen < NJ_M3G_SECTION_MIN) {
            M3G_FAIL("the section at %lu says it is %lu byte(s); %u is the smallest possible (nine byte header plus a four byte checksum)",
                     (unsigned long)off, (unsigned long)clen,
                     (unsigned)NJ_M3G_SECTION_MIN);
        }
        if (clen > n - off) {
            M3G_FAIL("the section at %lu runs %lu byte(s) past the end of a %lu byte file",
                     (unsigned long)off, (unsigned long)(clen - (n - off)),
                     (unsigned long)n);
        }
        (void)ulen;
        if (ids && nids < id_count) { ids[nids] = p[off]; }
        nids++;
        off += clen;
    }
    if (off != n) {
        M3G_FAIL("the sections end at %lu, not at the end of the file (%lu)",
                 (unsigned long)off, (unsigned long)n);
    }
    if (ids) {
        nj_u4 i;
        for (i = nids; i < id_count; i++) { ids[i] = 0xffu; }
    }
    return NJ_OK;
}


/* ---- what is inside an object ----------------------------------------- */

/*
 * The fields of an object, decoded from the bytes that follow its header.
 *
 * Every offset here is measured, not assumed.  The measurement that makes it
 * trustworthy is that the values land in closed sets: across the 975 real .m3g
 * files, every PolygonMode holds culling 162 (CULL_NONE), shading 165
 * (SHADE_SMOOTH) and winding 168 (WINDING_CCW) - three bytes out of 1513
 * objects, all three inside the enumerations JSR 184 defines.  An offset that
 * was one byte out would not do that.
 *
 * Only the types whose layout has been pinned this way are decoded.  Anything
 * else comes back with its userID and nothing else, which is honest: the
 * caller can tell "not decoded yet" from "decoded to zero".
 */
/*
 * How many bytes one texel takes in the file, by the JSR's own format
 * numbers: 96 ALPHA, 97 LUMINANCE, 98 LUMINANCE_ALPHA, 99 RGB, 100 RGBA.
 */
static nj_u4 m3g_image_bpp(nj_u4 fmt)
{
    switch (fmt) {
    case 96u:
    case 97u:
        return 1u;
    case 98u:
        return 2u;
    case 99u:
        return 3u;
    default:
        return 4u;
    }
}

#if NJ_ENABLE_M3G
/*
 * One texel of the file, as the four bytes the rasteriser samples: red,
 * green, blue and alpha, in that order.
 */
static void m3g_image_texel(nj_u4 fmt, const nj_u1 *s, nj_u1 *out)
{
    switch (fmt) {
    case 96u:                       /* alpha only: white, and the alpha */
        out[0] = 0xFFu;
        out[1] = 0xFFu;
        out[2] = 0xFFu;
        out[3] = s[0];
        break;
    case 97u:                       /* luminance: grey */
        out[0] = s[0];
        out[1] = s[0];
        out[2] = s[0];
        out[3] = 0xFFu;
        break;
    case 98u:                       /* luminance and alpha */
        out[0] = s[0];
        out[1] = s[0];
        out[2] = s[0];
        out[3] = s[1];
        break;
    case 99u:                       /* rgb */
        out[0] = s[0];
        out[1] = s[1];
        out[2] = s[2];
        out[3] = 0xFFu;
        break;
    default:                        /* rgba */
        out[0] = s[0];
        out[1] = s[1];
        out[2] = s[2];
        out[3] = s[3];
        break;
    }
}
#endif

/* Object3D: userID, AnimationTrack indices, then length-prefixed user
 * parameters. Subtraction bounds each count before multiplying or advancing. */
static nj_u4 m3g_object_header_size(const nj_u1 *data, nj_u4 len)
{
    nj_u4 at, count, i;
    if (len < 12u) { return 0u; }
    count = le32(data + 4u);
    if (count > (len - 12u) / 4u) { return 0u; }
    at = 8u + 4u * count;
    count = le32(data + at);
    at += 4u;
    if (count > (len - at) / 8u) { return 0u; }
    for (i = 0u; i < count; i++) {
        nj_u4 size;
        if (len - at < 8u) { return 0u; }
        size = le32(data + at + 4u);
        at += 8u;
        if (size > len - at) { return 0u; }
        at += size;
    }
    return at;
}

nj_status nj_m3g_decode_object(const nj_u1 *data, nj_u4 len, nj_u1 type,
                               nj_m3g_fields *out, char *err, nj_u4 errcap)
{
    if (err && errcap) { err[0] = '\0'; }
    if (!data || !out) { M3G_FAIL("no object bytes"); }
    memset(out, 0, sizeof(*out));
    if (type == NJ_M3G_TYPE_EXTERNAL_REF) {
        if (len == 0u || data[len - 1u] != 0u) {
            M3G_FAIL("an external reference has no terminating zero");
        }
        out->data = data;
        out->data_len = len;
        out->decoded = 1u;
        return NJ_OK;
    }
    if (len < NJ_M3G_OBJECT_HEADER) {
        M3G_FAIL("an object of type %u is %lu byte(s), too short to hold a header",
                 (unsigned)type, (unsigned long)len);
    }
    out->user_id = le32(data);
    {
        nj_u4 header = m3g_object_header_size(data, len);
        if (!header) { M3G_FAIL("truncated Object3D header"); }
        /* The type decoders address their payload relative to the minimal
         * twelve-byte header. Keep those offsets while skipping variable
         * animation/user data; no payload copy is needed. */
        data += header - NJ_M3G_OBJECT_HEADER;
        len -= header - NJ_M3G_OBJECT_HEADER;
    }
    /*
     * A Node's transform sits at byte 16 when bit 0 of the enable byte says so
     * (byte 12 is the enable byte, 13-15 the alpha/picking/rendering flags,
     * then the transform).  It is remembered as a pointer into the section
     * buffer - the same shape a VertexArray's bytes are kept in - and the
     * object builder turns it into the Java matrix.
     *
     * Measured: the transform begins at byte 13, right after the enable byte.
     * Every Mesh of fo.m3g/ny.m3g/Att.m3g that carries one reads a coherent
     * translation, unit scale and a 90 degree axis rotation there, and reads
     * 1e11-sized float noise three bytes later - so byte 13 is where the file
     * puts it and the flags are not in between.
     */
    if ((m3g_type_is_node(type) || type == NJ_M3G_T_TEXTURE2D) &&
        len >= NJ_M3G_OBJECT_HEADER + 44u &&
        (data[12] & 0x01u)) {
        out->data = data + NJ_M3G_OBJECT_HEADER + 1u;
        out->data_len = 40u;
    }

    switch (type) {
    case NJ_M3G_T_ANIMATION_TRACK: /* sequence, controller, property */
        if (len < 24u) { M3G_FAIL("truncated AnimationTrack"); }
        out->refs[0] = le32(data + 12u);
        out->refs[1] = le32(data + 16u);
        out->nrefs = 2u;
        break;
    case NJ_M3G_T_POLYGON_MODE:      /* three enumeration bytes */
        if (len < NJ_M3G_OBJECT_HEADER + 3u) {
            M3G_FAIL("a PolygonMode is %lu byte(s)", (unsigned long)len);
        }
        out->a = data[12]; out->b = data[13]; out->c = data[14];
        /* zero means "not set in the file"; anything else is one of the three
           enumerations the format defines, and a file that says otherwise is
           one we would be guessing at */
        if (!((out->a == 0u) || (out->a >= 160u && out->a <= 162u)) ||
            !((out->b == 0u) || (out->b >= 164u && out->b <= 165u)) ||
            !((out->c == 0u) || (out->c >= 168u && out->c <= 169u))) {
            M3G_FAIL("a PolygonMode holds culling=%lu shading=%lu winding=%lu",
                     (unsigned long)out->a, (unsigned long)out->b,
                     (unsigned long)out->c);
        }
        break;
    case NJ_M3G_T_GROUP:
        /*
         * A Group's children are the LAST thing in its data.  Node's own
         * fields sit in front of them and their length depends on which
         * transform representation the file chose, so instead of walking that
         * layout from the front - which is where the previous three wrong
         * guesses about this format came from - the tail is located by
         * requiring the count and the indices to consume the data exactly.
         *
         * Not finding one is not an error: a Group whose children cannot be
         * located draws nothing, which is better than refusing to load a file
         * whose other 51 objects are fine.
         */
        {
            nj_u4 o, k;
            for (o = NJ_M3G_OBJECT_HEADER; o + 4u <= len; o++) {
                nj_u4 n = le32(data + o);
                if (n > 4096u) { continue; }
                if (o + 4u + 4u * n != len) { continue; }
                out->ref_off = o + 4u;
                out->nrefs = (n < (nj_u4)NJ_M3G_MAX_REFS) ? n : (nj_u4)NJ_M3G_MAX_REFS;
                out->nrefs_total = n;
                for (k = 0u; k < out->nrefs; k++) {
                    out->refs[k] = le32(data + out->ref_off + 4u * k);
                }
                break;
            }
            out->a = out->nrefs;
        }
        break;
    case NJ_M3G_T_IMAGE2D: {
        /*
         * Measured over every Image2D the corpus ships (35 in the stored
         * sections, and the shape holds for each one exactly): the format is
         * one byte at 12, isMutable at 13, the width a u32 at 14, the height a
         * u32 at 18 and the size of the palette a u32 at 22.  The palette
         * starts at 26, followed by the pixel byte count and then the pixels.
         * Pixels use one
         * byte per pixel when the image is paletted, and otherwise as many
         * bytes per pixel as the format says.  The reading is accepted only
         * when it consumes the object exactly; anything else is left
         * undecoded, so an untextured surface still draws.
         */
        nj_u4 fmt, bpp, pal;

        if (len < 30u) {
            out->decoded = 0u;
            return NJ_OK;
        }
        fmt = data[12];
        bpp = m3g_image_bpp(fmt);
        out->a = fmt;
        out->b = le32(data + 14u);
        out->c = le32(data + 18u);
        pal = le32(data + 22u);
        if (out->b == 0u || out->b > 4096u || out->c == 0u || out->c > 4096u) {
            out->decoded = 0u;
            return NJ_OK;
        }
        if (pal == 0u) {
            if (len != 30u + out->b * out->c * bpp) {
                out->decoded = 0u;
                return NJ_OK;
            }
        } else if (pal % bpp != 0u || pal > 4u * 1024u ||
                   len != 30u + pal + out->b * out->c) {
            out->decoded = 0u;
            return NJ_OK;
        }
        out->d = pal;
        /* The pixel byte count follows the palette, not its size field. */
        out->data = data + (pal ? 26u : 30u);
        out->data_len = len - (pal ? 26u : 30u);
        break;
    }
    case NJ_M3G_T_WORLD:
        /*
         * A World is a Group, but its children do not sit at the end of its
         * data: Node's fields are in front of them and the two references the
         * World adds (its active camera and its background) are behind them.
         *
         * Unlike a Group - where the tail of the data is the anchor - the
         * layout here was measured across the corpus: of the 93 World objects
         * in the collected titles, every single one puts the child count at
         * offset 22 and has exactly eight bytes after the children, so a
         * layout is accepted only when the count, the indices and those eight
         * bytes consume the data exactly.  Without this a World came back with
         * no children, Object3D.find() could not reach the model inside it,
         * and a title that looks its mesh up by userID called addChild(null) -
         * which is 毁灭战士3_10598 failing to start.
         */
        if (len >= 26u) {
            nj_u4 n = le32(data + 22u);
            if ((n > 0u) && (n <= 4096u) &&
                (22u + 4u + 4u * n + 8u == len)) {
                nj_u4 k;
                out->ref_off = 26u;
                out->nrefs = (n < (nj_u4)NJ_M3G_MAX_REFS) ? n
                                                          : (nj_u4)NJ_M3G_MAX_REFS;
                out->nrefs_total = n;
                for (k = 0u; k < out->nrefs; k++) {
                    out->refs[k] = le32(data + out->ref_off + 4u * k);
                }
                out->a = out->nrefs;
                /* The two references the World adds behind its children: the
                   active camera and the background.  Nothing reads them yet;
                   they are kept so that getActiveCamera() and getBackground()
                   can answer instead of the title finding the camera by
                   scanning the children itself. */
                out->b = le32(data + 26u + 4u * n);
                out->c = le32(data + 30u + 4u * n);
            }
        }
        break;
    case NJ_M3G_T_MESH:
        /*
         * A Mesh is a Node followed by the vertex buffer it draws from and its
         * submeshes: for each one an IndexBuffer reference and an Appearance
         * reference.
         *
         * The Node's length is not fixed, which is what made this layout look
         * like two different ones at first.  Byte 12 is the Node's enable bits
         * and bit 0 says whether the 40 bytes of transform (translation, scale,
         * orientation) are in the file; 1,725 of the 2,906 Meshes in the 975
         * corpus files carry them and 988 do not.  Past that a Mesh holds four
         * bytes that are 0xFFFFFFFF in every file (the same eight-byte pair
         * every object's header ends with), then the vertex buffer, then the
         * submesh count and the 8 bytes per submesh.
         *
         * The layout is accepted only when it consumes the object exactly, the
         * same rule the World above is held to: a wrong reading here does not
         * draw nothing, it draws some other object's submesh list.  Measured
         * over the corpus, that rule accepts 2,608 of the 2,906 Meshes; the
         * rest keep their vertex buffer and no submeshes, which is where they
         * are today.
         */
        if (len >= 0x20u) {
            nj_u4 base = 0x10u;
            nj_u4 vb, nsub;

            if ((data[12] & 0x01u) != 0u) {
                base += 40u;
            }
            if (base + 14u <= len && le32(data + base) == 0xFFFFFFFFu) {
                vb = le32(data + base + 6u);
                nsub = le32(data + base + 10u);
                if (nsub <= (nj_u4)(NJ_M3G_MAX_REFS / 2u) &&
                    len == base + 14u + 8u * nsub) {
                    nj_u4 k;
                    int usable = 1;

                    for (k = 0u; k < nsub && usable; k++) {
                        nj_u4 ib = le32(data + base + 14u + 8u * k);
                        nj_u4 app = le32(data + base + 18u + 8u * k);

                        if (ib == 0u || app == 0u) {
                            usable = 0;
                        } else {
                            out->refs[2u * k] = ib;
                            out->refs[2u * k + 1u] = app;
                        }
                    }
                    if (usable) {
                        out->a = vb;
                        out->b = nsub;
                        out->nrefs = 2u * nsub;
                        out->decoded = 1u;
                    }
                }
            }
        }
        break;
    case NJ_M3G_T_VERTEX_BUFFER: {
        /*
         * JSR 184 file format 11.29: defaultColor, the positions reference,
         * positionBias[3], positionScale, the normals and colors references,
         * then a texture-unit count and that many (texCoords, texCoordBias[3],
         * texCoordScale) triples.
         *
         * Measured over the corpus: 1,852 of the 1,901 VertexBuffers are 68
         * bytes (one texture unit) and 49 are 48 (none), and every one of
         * them lands all four references on a VertexArray under the value-2
         * rule the Mesh layout forced.  Nothing is accepted unless the layout
         * consumes the object exactly.
         *
         * The four floats are kept as bit patterns in the reference slots
         * from NJ_M3G_MAX_REFS - 4 upwards: the slots below nrefs are what
         * the roots pass walks, so a float stored there would be mistaken for
         * a reference to some object and would quietly unroot the World.
         */
        if (len >= 48u) {
            nj_u4 texn = le32(data + 44u);
            if (texn <= 8u && len == 48u + 20u * texn) {
                out->a = le32(data + 16u);          /* positions */
                out->b = le32(data + 36u);          /* normals */
                out->c = le32(data + 40u);          /* colors */
                out->refs[0] = out->a;
                out->refs[1] = out->b;
                out->refs[2] = out->c;
                out->nrefs = 3u;
                if (texn > 0u) {
                    out->refs[3] = le32(data + 48u);
                    out->nrefs = 4u;
                    out->refs[8]  = le32(data + 52u);   /* texCoordBias bits */
                    out->refs[9]  = le32(data + 56u);
                    out->refs[10] = le32(data + 60u);
                    out->refs[11] = le32(data + 64u);   /* texCoordScale bits */
                }
                out->refs[NJ_M3G_MAX_REFS - 4u] = le32(data + 20u);  /* bias */
                out->refs[NJ_M3G_MAX_REFS - 3u] = le32(data + 24u);
                out->refs[NJ_M3G_MAX_REFS - 2u] = le32(data + 28u);
                out->refs[NJ_M3G_MAX_REFS - 1u] = le32(data + 32u);  /* scale */
                out->decoded = 1u;
            }
        }
        break; }
    case NJ_M3G_T_TRIANGLE_STRIP_ARRAY: {
        /*
         * JSR 184 file format 11.27: an encoding byte (128 = 32-bit indices,
         * 129 = a byte each, 130 = 16-bit each), the index count, that many
         * indices, then a strip count and that many strip lengths which add
         * up to the index count.
         *
         * Measured: all 2,397 TriangleStripArrays in the 975 corpus files
         * consume their object exactly under this reading.  The indices stay
         * in the section buffer and are copied out when the scene graph is
         * built, the way a VertexArray's bytes already are.
         */
        {
            nj_u4 enc = data[12];
            nj_u4 isz = (enc == 128u) ? 4u : (enc == 130u) ? 2u
                     : (enc == 129u) ? 1u : 0u;
            if (isz != 0u && len >= 21u) {
                nj_u4 n = le32(data + 13u);
                nj_u4 at = 17u + isz * n;
                if (at + 4u <= len) {
                    nj_u4 nstrip = le32(data + at);
                    /*
                     * The exact-consumption test is the real validator: a
                     * strip count is bounded by what is left of the object.
                     * The rally title's terrain carries 242, 262 and 320
                     * strips in one array, so an earlier ceiling of 64 threw
                     * away exactly the meshes that make up the ground.
                     */
                    if (nstrip >= 1u && at + 4u + 4u * nstrip == len) {
                        out->a = enc;
                        out->b = n;
                        out->c = nstrip;
                        out->data = data + 17u;
                        out->data_len = len - 17u;
                        out->ref_off = at;   /* where the strip lengths sit */
                        out->decoded = 1u;
                    }
                }
            }
        }
        break; }
    case NJ_M3G_T_VERTEX_ARRAY:      /* componentSize, componentCount, componentType */
        if (len < NJ_M3G_OBJECT_HEADER + 3u) {
            M3G_FAIL("a VertexArray is %lu byte(s)", (unsigned long)len);
        }
        out->a = data[12]; out->b = data[13]; out->c = data[14];
        /*
         * JSR 184 file format 11.28: componentSize, componentCount, the
         * encoding, then a UInt16 vertex count, and only then the components.
         * The two bytes of the count are why the data starts at 17: a reading
         * that started at 15 put every value half a component out of step,
         * and a shape that is out of step is not a shape that is missing.
         * Measured across the corpus, all 5,210 VertexArrays consume their
         * object exactly under this reading (17 + vertexCount * size * count).
         */
        out->data = data + NJ_M3G_OBJECT_HEADER + 5u;
        out->data_len = len - (NJ_M3G_OBJECT_HEADER + 5u);
        if (!(out->a == 0u || out->a == 1u || out->a == 2u || out->a == 4u) ||
            out->b > 4u || out->c > 2u) {
            M3G_FAIL("a VertexArray holds size=%lu count=%lu type=%lu",
                     (unsigned long)out->a, (unsigned long)out->b,
                     (unsigned long)out->c);
        }
        break;
    /*
     * The types below are what turns a white silhouette into the picture the
     * title drew: an Appearance names the material, the polygon mode, the
     * compositing mode and the textures; a Material holds the colours; a
     * Background holds the colour the viewport is cleared to.  Each reading is
     * accepted only when it consumes the object exactly - the same rule the
     * World and the Mesh were read under - because a layout that merely fits
     * is a layout that draws the wrong picture rather than none.
     */
    case NJ_M3G_T_APPEARANCE:
        /*
         * layer, then compositingMode, fog, polygonMode and material, then a
         * COUNT and that many texture references - which is what
         * m3gLoadAppearance() in the reference loader reads, field for field.
         * The count is a number, not a reference: reading it as one put the
         * first texture into the second unit's slot and left unit 0 pointing
         * at whatever object the number happened to name, so a textured track
         * came out untextured (or worse, textured with the wrong image).
         */
        if (len < 33u || (len - 33u) % 4u != 0u || (len - 33u) / 4u > 2u) {
            out->decoded = 0u;
            return NJ_OK;
        }
        out->a = data[12];
        out->refs[0] = le32(data + 13u);      /* compositingMode */
        out->refs[1] = le32(data + 17u);      /* fog */
        out->refs[2] = le32(data + 21u);      /* polygonMode */
        out->refs[3] = le32(data + 25u);      /* material */
        {
            nj_u4 ntex = le32(data + 29u);

            if (ntex != (len - 33u) / 4u) {
                out->decoded = 0u;
                return NJ_OK;
            }
            out->refs[4] = (ntex > 0u) ? le32(data + 33u) : 0u;
            out->refs[5] = (ntex > 1u) ? le32(data + 37u) : 0u;
            out->nrefs = 4u + (ntex > 2u ? 2u : ntex);
        }
        break;
    case NJ_M3G_T_BACKGROUND:
        /* 11.5: the colour, the image, the two image modes, the four crop
           numbers and the two clear flags.  Two corpus Backgrounds carry four
           bytes more, so the rule is "at least", and the fields read are the
           ones this runtime has somewhere to put. */
        if (len < 40u) {
            out->decoded = 0u;
            return NJ_OK;
        }
        out->a = m3g_rgba(data + 12);
        out->refs[0] = le32(data + 16);          /* backgroundImage */
        out->nrefs = 1u;
        out->b = (nj_u4)data[20] | ((nj_u4)data[21] << 8) |
                 ((nj_u4)data[38] << 16) | ((nj_u4)data[39] << 17);
        break;
    case NJ_M3G_T_CAMERA: {
        nj_u4 base = m3g_node_data(data, len);

        /*
         * 11.6: a Byte projectionType and then either a 4x4 matrix (GENERIC)
         * or fovy, aspectRatio, near, far.  The reading is chosen by length,
         * not by the type byte: the corpus writes 49 and 50 there, neither of
         * which is the 0 the specification names, while all 152 of its Cameras
         * consume their object exactly under one of these two shapes.
         */
        if (len == base + 17u) {
            out->a = 1u;                         /* perspective */
            out->f[0] = bits2f((nj_s4)le32(data + base + 1u));   /* fovy */
            out->f[1] = bits2f((nj_s4)le32(data + base + 5u));   /* aspect */
            out->f[2] = bits2f((nj_s4)le32(data + base + 9u));   /* near */
            out->f[3] = bits2f((nj_s4)le32(data + base + 13u));  /* far */
        } else if (len == base + 65u) {
            out->a = 0u;                         /* a handed-over matrix */
            out->data = data + base + 1u;
            out->data_len = 64u;
        } else {
            out->decoded = 0u;      /* a shape this runtime does not read */
            return NJ_OK;
        }
        break;
    }
    case NJ_M3G_T_COMPOSITING_MODE:
        if (len != 26u) {
            out->decoded = 0u;
            return NJ_OK;
        }
        out->a = (nj_u4)(data[12] ? 1u : 0u) | (nj_u4)(data[13] ? 2u : 0u) |
                 (nj_u4)(data[14] ? 4u : 0u) | (nj_u4)(data[15] ? 8u : 0u) |
                 ((nj_u4)data[16] << 8) | ((nj_u4)data[17] << 16);
        out->f[0] = bits2f((nj_s4)le32(data + 18));   /* depthOffsetFactor */
        out->f[1] = bits2f((nj_s4)le32(data + 22));   /* depthOffsetUnits */
        break;
    case NJ_M3G_T_FOG:
        if (len != 20u && len != 24u) {
            out->decoded = 0u;
            return NJ_OK;
        }
        out->a = m3g_rgb(data + 12);
        out->b = data[15];                       /* mode */
        out->f[0] = bits2f((nj_s4)le32(data + 16));
        out->f[1] = (len == 24u) ? bits2f((nj_s4)le32(data + 20)) : 0.0f;
        break;
    case NJ_M3G_T_LIGHT: {
        nj_u4 base = m3g_node_data(data, len);

        if (len != base + 28u) {
            out->decoded = 0u;
            return NJ_OK;
        }
        out->f[0] = bits2f((nj_s4)le32(data + base));        /* attenuation */
        out->f[1] = bits2f((nj_s4)le32(data + base + 4u));
        out->f[2] = bits2f((nj_s4)le32(data + base + 8u));
        out->a = m3g_rgb(data + base + 12u);                 /* color */
        out->b = data[base + 15u];                           /* mode */
        out->f[3] = bits2f((nj_s4)le32(data + base + 16u));  /* intensity */
        out->f[4] = bits2f((nj_s4)le32(data + base + 20u));  /* spotAngle */
        out->f[5] = bits2f((nj_s4)le32(data + base + 24u));  /* spotExponent */
        break;
    }
    case NJ_M3G_T_MATERIAL:
        if (len != 30u) {
            out->decoded = 0u;
            return NJ_OK;
        }
        out->a = m3g_rgb(data + 12);             /* ambient */
        out->b = m3g_rgba(data + 15);            /* diffuse */
        out->c = m3g_rgb(data + 19);             /* emissive */
        out->d = m3g_rgb(data + 22);             /* specular */
        out->f[0] = bits2f((nj_s4)le32(data + 25));   /* shininess */
        out->f[1] = (data[29] != 0u) ? 1.0f : 0.0f;   /* vertexColorTracking */
        break;
    case NJ_M3G_T_TEXTURE2D: {
        nj_u4 base = m3g_xform_data(data, len) + 2u;

        /* Component-transform flag + optional 40 bytes + general-transform
           flag precede the image reference.  Both flags occupy a byte even
           when false; the sampler payload itself is twelve bytes. */
        if (len != base + 12u) {
            out->decoded = 0u;
            return NJ_OK;
        }
        out->refs[0] = le32(data + base);        /* image */
        out->nrefs = 1u;
        out->a = m3g_rgb(data + base + 4u);      /* blendColor */
        out->b = (nj_u4)data[base + 7u] | ((nj_u4)data[base + 8u] << 8) |
                 ((nj_u4)data[base + 9u] << 16) | ((nj_u4)data[base + 10u] << 24);
        out->c = (nj_u4)data[base + 11u];
        break;
    }
    case NJ_M3G_T_SPRITE: {
        nj_u4 base = m3g_node_data(data, len);

        if (len != base + 25u) {
            out->decoded = 0u;
            return NJ_OK;
        }
        out->refs[0] = le32(data + base);        /* image */
        out->refs[1] = le32(data + base + 4u);   /* appearance */
        out->nrefs = 2u;
        out->a = data[base + 8u];                /* isScaled */
        break;
    }
    default:
        out->decoded = 0u;
        return NJ_OK;
    }
    out->decoded = 1u;
    return NJ_OK;
}

/* ---- the object layer ------------------------------------------------- */

/*
 * Walk the objects inside every section of a .m3g file.
 *
 * The container layer only proves that the sections tile the file.  This is
 * the layer beneath it: a section's payload is a run of objects, each a one
 * byte type, a four byte little endian data length, and that many bytes of
 * data.  A compressed section is inflated through the port's inflate hook,
 * because the runtime has no decompressor of its own and is meant to build
 * without one - a port that leaves the hook null gets "unsupported" here
 * rather than a wrong answer.
 *
 * `out` may be NULL with `out_cap` zero: the counts are reported either way,
 * so a caller can ask "how many" and "which ones" separately.
 */
#define M3G_SCAN_FAIL(...) do { nj_host_free(vm, scratch); M3G_FAIL(__VA_ARGS__); } while (0)

nj_u4 nj_m3g_ref_at(const nj_m3g_fields *f, nj_u4 k)
{
    if (k < (nj_u4)NJ_M3G_MAX_REFS) {
        return f->refs[k];
    }
    return (f->extra != NULL) ? f->extra[k - (nj_u4)NJ_M3G_MAX_REFS] : 0u;
}

#if NJ_ENABLE_M3G
/* Release the per-object overflow lists of a scan result. */
static void m3g_free_refs(nj_vm *vm, nj_m3g_object *objs, nj_u4 nobj)
{
    nj_u4 i;

    for (i = 0u; i < nobj; i++) {
        nj_host_free(vm, objs[i].fields.owned_data);
        objs[i].fields.owned_data = NULL;
        nj_host_free(vm, objs[i].fields.animation_refs);
        objs[i].fields.animation_refs = NULL;
        objs[i].fields.nanimation_refs = 0u;
        if (objs[i].fields.extra != NULL) {
            nj_host_free(vm, objs[i].fields.extra);
            objs[i].fields.extra = NULL;
        }
    }
}
#endif

nj_status nj_m3g_scan_objects(nj_vm *vm, const nj_u1 *p, nj_u4 n,
                              nj_m3g_object *out, nj_u4 out_cap,
                              nj_u4 *count, nj_u4 *sections,
                              char *err, nj_u4 errcap)
{
    nj_u4 off, nsec = 0, nobj = 0, scratch_cap = 0;
    nj_u1 *scratch = NULL;
    nj_status st;

    if (count)    { *count = 0; }
    if (sections) { *sections = 0; }
    st = nj_m3g_container_probe(p, n, NULL, 0, err, errcap);
    if (st != NJ_OK) { return st; }

    off = NJ_M3G_HEADER;
    while (off < n) {
        nj_u4 clen = le32(p + off + 1u);
        nj_u4 ulen = le32(p + off + 5u);
        nj_u4 body_len = clen - NJ_M3G_SECTION_MIN;   /* less header and Adler32 */
        const nj_u1 *body;
        nj_u4 at;

        nsec++;
        /*
         * The first section is the file header, and its payload is a header -
         * version, flags, a few counts - not a run of objects.  Walking it as
         * objects reads its fields as lengths, which is exactly the nonsense
         * this check exists to catch.
         */
        if (off == NJ_M3G_HEADER || ulen == 0u) { off += clen; continue; }
        if (p[off] == 1u) {
            if (ulen > scratch_cap) {
                nj_host_free(vm, scratch);
                scratch = (nj_u1 *)nj_host_alloc(vm, (nj_size)ulen);
                scratch_cap = scratch ? ulen : 0u;
                if (!scratch) {
                    M3G_SCAN_FAIL("out of host memory for a %lu byte section",
                                  (unsigned long)ulen);
                }
            }
            if (vm && vm->port && vm->port->inflate) {
                st = vm->port->inflate(vm->port->ud, p + off + NJ_M3G_SECTION,
                                       body_len, scratch, ulen);
            } else {
                nj_u4 decoded_len = ulen;
                st = nj_inflate_zlib(p + off + NJ_M3G_SECTION, body_len,
                                     scratch, &decoded_len);
                if (st == NJ_OK && decoded_len != ulen) {
                    st = NJ_ERR_FORMAT;
                }
            }
            if (st != NJ_OK) {
                M3G_SCAN_FAIL("the section at %lu did not inflate to the %lu byte(s) it declares",
                              (unsigned long)off, (unsigned long)ulen);
            }
            body = scratch;
            body_len = ulen;
        } else {
            body = p + off + NJ_M3G_SECTION;
        }

        at = 0;
        while (at + 5u <= body_len) {
            nj_u1 type = body[at];
            nj_u4 olen = le32(body + at + 1u);
            if (olen > body_len - at - 5u) {
                M3G_SCAN_FAIL("an object at %lu in the section at %lu says it is %lu byte(s) but only %lu remain",
                              (unsigned long)at, (unsigned long)off,
                              (unsigned long)olen, (unsigned long)(body_len - at - 5u));
            }
            if (out && nobj < out_cap) {
                out[nobj].type = type;
                out[nobj].off  = off + NJ_M3G_SECTION + at;
                out[nobj].decoded = (nj_m3g_decode_object(body + at + 5u, olen, type,
                                                          &out[nobj].fields,
                                                          NULL, 0u) == NJ_OK);
                out[nobj].len  = olen;
                /* an external reference is a bare string, everything else
                   starts with its userID */
                out[nobj].user_id = (type == NJ_M3G_TYPE_EXTERNAL_REF || olen < 4u)
                                  ? 0u : le32(body + at + 5u);
                {
                    nj_m3g_fields *f = &out[nobj].fields;
                    nj_u4 header = type == NJ_M3G_TYPE_EXTERNAL_REF ? 0u
                        : m3g_object_header_size(body + at + 5u, olen);
                    /* Report initialized entries even on a later allocation or
                     * framing failure, so callers can release every owned list. */
                    if (count) { *count = nobj + 1u; }
                    if (header) {
                        nj_u4 k, tracks = le32(body + at + 9u);
                        if (tracks) {
                            f->animation_refs = (nj_u4 *)nj_host_alloc(
                                vm, (nj_size)tracks * sizeof(nj_u4));
                            if (!f->animation_refs) {
                                M3G_SCAN_FAIL("out of host memory for animation references");
                            }
                            f->nanimation_refs = tracks;
                            for (k = 0u; k < tracks; k++) {
                                f->animation_refs[k] = le32(body + at + 13u + 4u * k);
                            }
                        }
                        /* ref_off is relative to the decoder's adjusted data;
                         * overflow children are copied from the original bytes. */
                        if (f->nrefs_total) { f->ref_off += header - 12u; }
                    }
                    if (f->data && f->data_len) {
                        f->owned_data = (nj_u1 *)nj_host_alloc(vm, f->data_len);
                        if (!f->owned_data) { M3G_SCAN_FAIL("out of host memory for object data"); }
                        memcpy(f->owned_data, f->data, f->data_len);
                        f->data = f->owned_data;
                    }
                    if (f->nrefs_total > (nj_u4)NJ_M3G_MAX_REFS) {
                        nj_u4 want = f->nrefs_total - (nj_u4)NJ_M3G_MAX_REFS;
                        nj_u4 *more = (nj_u4 *)nj_host_alloc(
                            vm, (nj_size)want * sizeof(nj_u4));
                        if (more == NULL) {
                            /* keep what fits rather than reading past it */
                            f->nrefs_total = (nj_u4)NJ_M3G_MAX_REFS;
                        } else {
                            nj_u4 k;
                            for (k = 0u; k < want; k++) {
                                more[k] = le32(body + at + 5u + f->ref_off +
                                               4u * ((nj_u4)NJ_M3G_MAX_REFS + k));
                            }
                            f->extra = more;
                        }
                    }
                }
            }
            nobj++;
            at += 5u + olen;
        }
        if (at != body_len) {
            M3G_SCAN_FAIL("the objects in the section at %lu end at %lu, not at %lu",
                          (unsigned long)off, (unsigned long)at, (unsigned long)body_len);
        }
        off += clen;
    }

    nj_host_free(vm, scratch);
    if (count)    { *count = nobj; }
    if (sections) { *sections = nsec; }
    return NJ_OK;
}


/*
 * Walk the object stream inside one inflated section.
 *
 * An object is type(1), then the length of its data (u32 little endian, NOT
 * counting the five byte header), then the data.  Getting that one field wrong
 * by five bytes per object is what turned 36859 real objects into nonsense
 * types: the walk drifts one byte further out of step with every object.  The
 * chain must tile the section exactly, for the same
 * reason the sections must tile the file: a reader that tolerates a short
 * object will read the next object's type byte out of this one's payload and
 * report a nonsense type several objects later, far from the damage.
 *
 * Nothing here interprets a payload.  That is the point: this layer answers
 * "is this stream a well formed sequence of objects", and the object decoder
 * answers "is this Mesh well formed".  Merging the two makes a truncated file
 * indistinguishable from an object type we do not implement yet.
 */
nj_status nj_m3g_objects_probe(const nj_u1 *p, nj_u4 n, nj_u4 *count,
                               nj_u1 *types, nj_u4 type_count,
                               char *err, nj_u4 errcap)
{
    nj_u4 off = 0, seen = 0;

    if (err && errcap) { err[0] = '\0'; }
    if (count) { *count = 0u; }
    if (!p && n != 0u) { M3G_FAIL("no bytes"); }
    while (off < n) {
        nj_u4 len;
        if (n - off < 5u) {
            M3G_FAIL("an object header at %lu is cut short: %lu byte(s) left",
                     (unsigned long)off, (unsigned long)(n - off));
        }
        len = le32(p + off + 1u);
        if (p[off] > 22u && p[off] != 255u) {
            M3G_FAIL("the object at %lu has type %u, and 23..254 are reserved",
                     (unsigned long)off, (unsigned)p[off]);
        }
        if (len > n - off - 5u) {
            M3G_FAIL("the object at %lu says its data is %lu byte(s), which runs past the end of a %lu byte section",
                     (unsigned long)off, (unsigned long)len, (unsigned long)n);
        }
        if (types && seen < type_count) { types[seen] = p[off]; }
        seen++;
        off += 5u + len;
    }
    if (count) { *count = seen; }
    return NJ_OK;
}


#define NJFN(name) NJ_NATIVE_FN(name)

/*
 * This module needs the libm transcendentals, and so it is only useful in a
 * build that has them: -DNJ_ENABLE_M3G=1 together with -DNJ_ENABLE_FLOAT=0
 * leaves the rotation helpers without sqrtf/cosf/sinf.  Say so at compile
 * time instead of failing to link with no explanation.
 */
#ifdef NJ_ENABLE_FLOAT
#if NJ_ENABLE_M3G && !NJ_ENABLE_FLOAT
#error "JSR 184 needs libm: build with NJ_ENABLE_FLOAT=1 or NJ_ENABLE_M3G=0"
#endif
#endif

#define NJ_C_O3D     "javax/microedition/m3g/Object3D"
#define NJ_C_XFORM   "javax/microedition/m3g/Transform"
#define NJ_C_XABLE   "javax/microedition/m3g/Transformable"
#define NJ_C_NODE    "javax/microedition/m3g/Node"
#define NJ_C_GROUP   "javax/microedition/m3g/Group"
#define NJ_C_WORLD   "javax/microedition/m3g/World"
#define NJ_C_CAMERA  "javax/microedition/m3g/Camera"
#define NJ_C_LIGHT   "javax/microedition/m3g/Light"
#define NJ_C_BG      "javax/microedition/m3g/Background"
#define NJ_C_G3D     "javax/microedition/m3g/Graphics3D"
#define NJ_C_LOADER  "javax/microedition/m3g/Loader"
#define NJ_C_IMAGE2D "javax/microedition/m3g/Image2D"
#define NJ_C_ATRACK  "javax/microedition/m3g/AnimationTrack"
#define NJ_C_APPEAR  "javax/microedition/m3g/Appearance"
#define NJ_C_MESH    "javax/microedition/m3g/Mesh"
#define NJ_C_MORPH   "javax/microedition/m3g/MorphingMesh"
#define NJ_C_SKIN    "javax/microedition/m3g/SkinnedMesh"
#define NJ_C_POLY    "javax/microedition/m3g/PolygonMode"
#define NJ_C_COMP    "javax/microedition/m3g/CompositingMode"
#define NJ_C_TEX2D   "javax/microedition/m3g/Texture2D"
#define NJ_C_MAT     "javax/microedition/m3g/Material"
#define NJ_C_VBUF    "javax/microedition/m3g/VertexBuffer"
#define NJ_C_IBUF    "javax/microedition/m3g/IndexBuffer"
#define NJ_C_SPRITE3D "javax/microedition/m3g/Sprite3D"
/* Distinct from nj_midp.h's NJ_C_SPRITE3D, which is the GAME package's
   javax/microedition/lcdui/game/Sprite: the two names clashed and whichever
   header was read last won (-Wmacro-redefined). */
#define NJ_C_M3G_SPRITE "javax/microedition/m3g/Sprite"
#define NJ_C_FOG      "javax/microedition/m3g/Fog"
#define NJ_C_RAY      "javax/microedition/m3g/RayIntersection"
#define NJ_C_ACTL     "javax/microedition/m3g/AnimationController"
#define NJ_C_KFS      "javax/microedition/m3g/KeyframeSequence"
#define NJ_C_VARR     "javax/microedition/m3g/VertexArray"
#define NJ_C_TSA      "javax/microedition/m3g/TriangleStripArray"
#define NJ_C_VECTOR  "java/util/Vector"
#define NJ_C_IMAGE   "javax/microedition/lcdui/Image"

#if NJ_ENABLE_M3G

/* ------------------------------------------------------------------ */
/* Matrices                                                            */
/* ------------------------------------------------------------------ */
/*
 * A 4x4 matrix stored as a java float[16] in row-major order, acting on
 * column vectors: v' = M * v.  The array lives in a field of the object that
 * owns it - there is no C-side matrix cache and nothing is global.
 */
#define NJ_MF 16

/*
 * Look a field up, or throw.  Declared here and defined with the rest of the
 * helpers far below, because the natives that use it are spread over the whole
 * file: the batch that added the second half of the JSR 184 surface put one
 * native above the definition, and gcc refuses to guess a signature.
 */
static nj_field *m3g_fof(nj_vm *vm, const char *cls, const char *name,
                         const char *desc);

/* Keep mutability in the existing format word, not another per-image field.
 * Pixel uploads must not change it, and every format consumer masks it out. */
#define M3G_IMAGE_MUTABLE 0x100u
#define M3G_IMAGE_FORMAT_MASK 0xffu
static nj_s4 m3g_image_format(nj_vm *vm, nj_ref image)
{
    return (nj_s4)((nj_u4)nj_iget(vm, image,
                   m3g_fof(vm, NJ_C_IMAGE2D, "_nj_fmt", "I")) &
                   M3G_IMAGE_FORMAT_MASK);
}

/*
 * Floats live in int fields as their IEEE-754 bit patterns.  The public field
 * accessors are nj_field_read_int / nj_field_write_int, the storage is four
 * bytes either way, and nothing outside this file reads these fields.
 */
static nj_s4 f2bits(float f)
{
    union { float f; nj_s4 i; } u;
    u.f = f;
    return u.i;
}

static nj_ref mat_new(nj_vm *vm);

/*
 * The matrix a scene object carries, allocated the first time anybody asks.
 *
 * A node decoded out of an .m3g file has its matrix filled in by the loader,
 * but a node built with `new Group()` has never stored one - the field is
 * null, and every Transformable method used to quietly do nothing with it.
 * That is why 毁灭战士3_10598 kept drawing nothing even after postRotate
 * existed: the call was answered, and had nowhere to write.  The identity
 * matrix is the value the JSR gives a fresh Transformable, so installing it
 * on first access is the behaviour a title already expects.
 */
static float *mat_of(nj_vm *vm, nj_ref obj, const char *cls, const char *name)
{
    nj_field *f;
    nj_ref a;

    if (!obj) {
        return NULL;
    }
    f = nj_field_lookup(vm, cls, name, "[F", 0);
    if (!f) {
        return NULL;
    }
    a = nj_rget(vm, obj, f);
    if (!a) {
        a = mat_new(vm);
        if (!a) {
            return NULL;
        }
        nj_rput(vm, obj, f, a);
    }
    return (float *)a->u.bytes;
}

static nj_ref mat_new(nj_vm *vm)
{
    nj_ref a = nj_array_new(vm, "F", NJ_MF);
    float *m;

    if (!a) {
        return 0;
    }
    m = (float *)a->u.bytes;
    memset(m, 0, sizeof(float) * NJ_MF);
    m[0] = m[5] = m[10] = m[15] = 1.0f;
    return a;
}

static void mat_mul(const float *a, const float *b, float *out)
{
    float r[NJ_MF];
    int i, j, k;

    for (i = 0; i < 4; i++) {
        for (j = 0; j < 4; j++) {
            float s = 0.0f;
            for (k = 0; k < 4; k++) {
                s += a[i * 4 + k] * b[k * 4 + j];
            }
            r[i * 4 + j] = s;
        }
    }
    memcpy(out, r, sizeof(r));
}

static void mat_identity(float *m)
{
    memset(m, 0, sizeof(float) * NJ_MF);
    m[0] = m[5] = m[10] = m[15] = 1.0f;
}

static void mat_translate(float *m, float x, float y, float z)
{
    float t[NJ_MF];

    mat_identity(t);
    t[3] = x;
    t[7] = y;
    t[11] = z;
    mat_mul(m, t, m);
}

static void mat_scale(float *m, float x, float y, float z)
{
    float t[NJ_MF];

    mat_identity(t);
    t[0] = x;
    t[5] = y;
    t[10] = z;
    mat_mul(m, t, m);
}

/*
 * postRotate(angle, ax, ay, az): right-hand rule about the (not necessarily
 * normalised) axis, in degrees, which is what the JSR specifies.
 */
static void mat_rotate(float *m, float deg, float ax, float ay, float az)
{
    float t[NJ_MF];
    float len = sqrtf(ax * ax + ay * ay + az * az);
    float rad, c, s, ic;

    if (len <= 0.0f) {
        return;                     /* a zero axis rotates nothing */
    }
    ax /= len; ay /= len; az /= len;
    rad = deg * 3.14159265358979f / 180.0f;
    c = cosf(rad); s = sinf(rad); ic = 1.0f - c;

    mat_identity(t);
    t[0] = ax * ax * ic + c;
    t[1] = ax * ay * ic - az * s;
    t[2] = ax * az * ic + ay * s;
    t[4] = ay * ax * ic + az * s;
    t[5] = ay * ay * ic + c;
    t[6] = ay * az * ic - ax * s;
    t[8] = az * ax * ic - ay * s;
    t[9] = az * ay * ic + ax * s;
    t[10] = az * az * ic + c;
    mat_mul(m, t, m);
}

/*
 * Invert in place.  The affine case is handled directly (transpose the
 * rotation basis and negate the translation); anything with a non-affine
 * bottom row falls back to Gauss-Jordan, and a singular matrix becomes the
 * identity, which is what the JSR says to do rather than throwing.
 */
static void mat_invert(float *m)
{
    float inv[NJ_MF], tmp[NJ_MF];
    int i, j, k;
    int affine = (m[12] == 0.0f && m[13] == 0.0f && m[14] == 0.0f &&
                  m[15] == 1.0f);

    if (affine) {
        /*
         * The upper-left 3x3 block inverse, then minus that times the
         * translation - which lives in column 3 (elements 3, 7, 11) because
         * this is a row-major matrix acting on column vectors, not in the
         * fourth row.  Transposing the block and negating the translation -
         * the shortcut for a pure rotation - is wrong the moment there is a
         * scale in the matrix, and a title that inverts its own transform
         * after scaling a node would silently get the scale twice.
         */
        float a = m[0], b = m[1], c = m[2];
        float d = m[4], e = m[5], f = m[6];
        float g = m[8], h = m[9], i2 = m[10];
        float det = a * (e * i2 - f * h) - b * (d * i2 - f * g) +
                    c * (d * h - e * g);

        if (fabsf(det) <= 1e-12f) {
            mat_identity(m);        /* singular: the JSR says identity */
            return;
        }
        inv[0] = (e * i2 - f * h) / det;
        inv[1] = (c * h - b * i2) / det;
        inv[2] = (b * f - c * e) / det;
        inv[3] = -(inv[0] * m[3] + inv[1] * m[7] + inv[2] * m[11]);
        inv[4] = (f * g - d * i2) / det;
        inv[5] = (a * i2 - c * g) / det;
        inv[6] = (c * d - a * f) / det;
        inv[7] = -(inv[4] * m[3] + inv[5] * m[7] + inv[6] * m[11]);
        inv[8] = (d * h - e * g) / det;
        inv[9] = (b * g - a * h) / det;
        inv[10] = (a * e - b * d) / det;
        inv[11] = -(inv[8] * m[3] + inv[9] * m[7] + inv[10] * m[11]);
        inv[12] = 0.0f;
        inv[13] = 0.0f;
        inv[14] = 0.0f;
        inv[15] = 1.0f;
        memcpy(m, inv, sizeof(inv));
        return;
    }

    mat_identity(tmp);
    for (i = 0; i < 4; i++) {
        int piv = i;
        float best = fabsf(m[i * 4 + i]);

        for (k = i + 1; k < 4; k++) {
            float v = fabsf(m[k * 4 + i]);
            if (v > best) { best = v; piv = k; }
        }
        if (best <= 0.0f) {
            mat_identity(m);        /* singular: the JSR says identity */
            return;
        }
        if (piv != i) {
            for (j = 0; j < 4; j++) {
                float t;
                t = m[i * 4 + j]; m[i * 4 + j] = m[piv * 4 + j]; m[piv * 4 + j] = t;
                t = tmp[i * 4 + j]; tmp[i * 4 + j] = tmp[piv * 4 + j]; tmp[piv * 4 + j] = t;
            }
        }
        {
            float d = m[i * 4 + i];
            for (j = 0; j < 4; j++) {
                m[i * 4 + j] /= d;
                tmp[i * 4 + j] /= d;
            }
        }
        for (k = 0; k < 4; k++) {
            float f;
            if (k == i) {
                continue;
            }
            f = m[k * 4 + i];
            if (f == 0.0f) {
                continue;
            }
            for (j = 0; j < 4; j++) {
                m[k * 4 + j] -= f * m[i * 4 + j];
                tmp[k * 4 + j] -= f * tmp[i * 4 + j];
            }
        }
    }
    memcpy(m, tmp, sizeof(tmp));
}

/* ------------------------------------------------------------------ */
/* Transform                                                           */
/* ------------------------------------------------------------------ */
NJFN(m3g_xf_init)
{
    nj_rput(vm, NJ_THIS(args), nj_field_lookup(vm, NJ_C_XFORM, "_nj_m", "[F", 0),
            mat_new(vm));
    return NJ_OK;
}

NJFN(m3g_xf_setIdentity)
{
    float *m = mat_of(vm, NJ_THIS(args), NJ_C_XFORM, "_nj_m");
    if (m) {
        mat_identity(m);
    }
    return NJ_OK;
}

NJFN(m3g_xf_set_xf)
{
    float *d = mat_of(vm, NJ_THIS(args), NJ_C_XFORM, "_nj_m");
    float *s = mat_of(vm, NJ_ARG_REF(args, 0), NJ_C_XFORM, "_nj_m");
    if (d && s) {
        memcpy(d, s, sizeof(float) * NJ_MF);
    }
    return NJ_OK;
}

NJFN(m3g_xf_set_arr)
{
    nj_ref arr = NJ_ARG_REF(args, 0);
    float *d = mat_of(vm, NJ_THIS(args), NJ_C_XFORM, "_nj_m");

    if (!arr || arr->length < NJ_MF) {
        NJ_THROW(vm, "java/lang/IllegalArgumentException",
                 "a Transform is set from a float[16]");
    }
    if (d) {
        memcpy(d, arr->u.bytes, sizeof(float) * NJ_MF);
    }
    return NJ_OK;
}

NJFN(m3g_xf_postMultiply)
{
    float *d = mat_of(vm, NJ_THIS(args), NJ_C_XFORM, "_nj_m");
    float *o = mat_of(vm, NJ_ARG_REF(args, 0), NJ_C_XFORM, "_nj_m");
    if (d && o) {
        mat_mul(d, o, d);
    }
    return NJ_OK;
}

NJFN(m3g_xf_postTranslate)
{
    float *d = mat_of(vm, NJ_THIS(args), NJ_C_XFORM, "_nj_m");
    if (d) {
        mat_translate(d, NJ_ARG_FLOAT(args, 0), NJ_ARG_FLOAT(args, 1),
                      NJ_ARG_FLOAT(args, 2));
    }
    return NJ_OK;
}

NJFN(m3g_xf_postScale)
{
    float *d = mat_of(vm, NJ_THIS(args), NJ_C_XFORM, "_nj_m");
    if (d) {
        mat_scale(d, NJ_ARG_FLOAT(args, 0), NJ_ARG_FLOAT(args, 1),
                  NJ_ARG_FLOAT(args, 2));
    }
    return NJ_OK;
}

NJFN(m3g_xf_postRotate)
{
    float *d = mat_of(vm, NJ_THIS(args), NJ_C_XFORM, "_nj_m");
    if (d) {
        mat_rotate(d, NJ_ARG_FLOAT(args, 0), NJ_ARG_FLOAT(args, 1),
                   NJ_ARG_FLOAT(args, 2), NJ_ARG_FLOAT(args, 3));
    }
    return NJ_OK;
}

/*
 * get(float[]) / set(float[]): the matrix in and out as a 16-element array.
 * Neither is in the first pass of this module and 生化危机3D walks straight
 * into get(), so it is here; set() is its mirror and costs nothing.
 */
NJFN(m3g_xf_get)
{
    nj_ref self = NJ_THIS(args);
    nj_ref out = NJ_ARG_REF(args, 0);
    nj_ref m = nj_rget(vm, self, nj_field_lookup(vm, NJ_C_XFORM, "_nj_m", "[F", 0));

    if (!out || out->length < 16) {
        return nj_throw(vm, "java/lang/IllegalArgumentException",
                        "the array must hold at least sixteen elements");
    }
    if (m) {
        memcpy(nj_array_data(out), nj_array_data(m), 16u * sizeof(float));
    }
    return NJ_OK;
}

/* Transform.set(float[]) is m3g_xf_set_arr above.  A second function with the
   same job lived here and was never registered, so it compiled to nothing and
   warned (-Wunused-function); the registered one is the one with the check. */

NJFN(m3g_xf_invert)
{
    float *d = mat_of(vm, NJ_THIS(args), NJ_C_XFORM, "_nj_m");
    if (d) {
        mat_invert(d);
    }
    return NJ_OK;
}

/*
 * transform(float[]): v' = M v, the same column-vector convention as
 * Transformable, postTranslate and mat_mul use.  The JSR's wording for this
 * one method reads both ways - some implementations multiply the vector from
 * the left, which is the transpose - and with no reference device here the
 * choice is made for consistency: a title that places a point with
 * transform() gets the answer its own setTranslation/postTranslate imply.  If
 * a title ever needs the transpose it will show up as a mirrored rotation,
 * which is visible, rather than as a silent wrong number.
 */
NJFN(m3g_xf_transform)
{
    nj_ref v = NJ_ARG_REF(args, 0);
    float *m = mat_of(vm, NJ_THIS(args), NJ_C_XFORM, "_nj_m");
    float in[4], out[4];
    int i, j;

    if (!v || v->length < 4) {
        NJ_THROW(vm, "java/lang/IllegalArgumentException",
                 "transform() fills a float[4]");
    }
    if (!m) {
        return NJ_OK;
    }
    in[0] = ((float *)v->u.bytes)[0];
    in[1] = ((float *)v->u.bytes)[1];
    in[2] = ((float *)v->u.bytes)[2];
    in[3] = ((float *)v->u.bytes)[3];
    for (i = 0; i < 4; i++) {
        float sum = 0.0f;
        for (j = 0; j < 4; j++) {
            sum += m[i * 4 + j] * in[j];
        }
        out[i] = sum;
    }
    for (i = 0; i < 4; i++) {
        ((float *)v->u.bytes)[i] = out[i];
    }
    return NJ_OK;
}

/* ------------------------------------------------------------------ */
/* The scene graph                                                     */
/* ------------------------------------------------------------------ */
NJFN(m3g_userID_get)
{
    ret->i = nj_iget(vm, NJ_THIS(args),
                     nj_field_lookup(vm, NJ_C_O3D, "_nj_userID", "I", 0));
    return NJ_OK;
}

NJFN(m3g_userID_set)
{
    nj_iput(vm, NJ_THIS(args),
            nj_field_lookup(vm, NJ_C_O3D, "_nj_userID", "I", 0),
            NJ_ARG_INT(args, 0));
    return NJ_OK;
}

/*
 * Object3D.find(userID) searches this object and its children, depth first,
 * which is what graphs of titles rely on to fetch a node they made earlier.
 */
static nj_ref find_by_id(nj_vm *vm, nj_ref obj, nj_s4 id, int depth)
{
    nj_field *kids_f;
    nj_ref kids;

    if (!obj || !nj_heap_owns(vm, obj) || depth > 64) {
        return 0;
    }
    if (nj_iget(vm, obj, nj_field_lookup(vm, NJ_C_O3D, "_nj_userID", "I", 0)) == id) {
        return obj;
    }
    if (!nj_class_is_subclass_of(obj->cls, nj_class_lookup(vm, NJ_C_GROUP))) {
        return 0;
    }
    kids_f = nj_field_lookup(vm, NJ_C_GROUP, "_nj_kids", "L" NJ_C_VECTOR ";", 0);
    kids = nj_rget(vm, obj, kids_f);
    if (kids) {
        nj_value a[5];
        nj_value n;
        nj_s4 count, i;

        if (nj_call(vm, kids, NJ_C_VECTOR, "size", "()I", NULL, &n) != NJ_OK) {
            return 0;
        }
        count = n.i;
        for (i = 0; i < count; i++) {
            nj_ref child;
            nj_ref got = 0;

            a[0].i = i;
            if (nj_call(vm, kids, NJ_C_VECTOR, "elementAt",
                        "(I)Ljava/lang/Object;", a, &n) != NJ_OK) {
                return 0;
            }
            child = n.r;
            got = find_by_id(vm, child, id, depth + 1);
            if (got) {
                return got;
            }
        }
    }
    return 0;
}

/*
 * The userID lookup the title actually asked for.
 *
 * Object3D.find() is defined over an object and its descendants, and that is
 * what find_by_id() does; a World, though, stands for a whole file, and the
 * title's idea of what is in that file comes from the file itself.  When our
 * reading of the references did not link the object the title is looking for,
 * the answer that keeps the title running is the file's own table.
 */
static nj_ref find_in_table(nj_vm *vm, nj_ref world, nj_s4 id)
{
    nj_field *uf = nj_field_lookup(vm, NJ_C_O3D, "_nj_userID", "I", 0);
    nj_ref all = nj_rget(vm, world, nj_field_lookup(vm, NJ_C_WORLD, "_nj_all",
                                                    "[L" NJ_C_O3D ";", 0));
    nj_u4 n, i;

    if (!all || !uf) {
        return 0;
    }
    n = nj_array_length(all);
    for (i = 0u; i < n; i++) {
        nj_ref o = nj_array_get_ref(all, i);
        if (o && nj_iget(vm, o, uf) == id) {
            return o;
        }
    }
    return 0;
}

NJFN(m3g_find)
{
    nj_ref got = find_by_id(vm, NJ_THIS(args), NJ_ARG_INT(args, 0), 0);
    if (!got) {
        got = find_in_table(vm, NJ_THIS(args), NJ_ARG_INT(args, 0));
    }
    ret->r = got;
    return NJ_OK;
}

/* Duplicate instance state, retaining referenced M3G resources while giving
 * internal arrays and slot vectors their own storage.  Node copies are detached
 * from the source parent; a Group's descendants are duplicated below. */
static nj_ref m3g_copy_object(nj_vm *vm, nj_ref self, unsigned depth)
{
    nj_ref copy;
    nj_class *cls;
    if (!self || !self->cls || depth > 64u) { return 0; }
    copy = nj_root(vm, nj_object_new(vm, self->cls->name));
    if (!copy) { return 0; }
    memcpy((nj_u1 *)copy + sizeof(nj_object),
           (const nj_u1 *)self + sizeof(nj_object),
           self->cls->instance_size - sizeof(nj_object));
    for (cls = self->cls; cls; cls = cls->super) {
        nj_u2 i;
        for (i = 0; i < cls->field_count; i++) {
            nj_field *f = &cls->fields[i];
            nj_ref src, dst;
            if (f->flags & NJ_ACC_STATIC) { continue; }
            if (strcmp(f->name, "_nj_parent") == 0) {
                nj_rput(vm, copy, f, 0);
            } else if (f->desc[0] == '[') {
                src = nj_rget(vm, self, f);
                if (!src) { continue; }
                dst = nj_array_new(vm, f->desc + 1, nj_array_length(src));
                if (!dst) { return 0; }
                memcpy(nj_array_data(dst), nj_array_data(src),
                       src->size - (nj_size)((nj_u1 *)src->u.bytes - (nj_u1 *)src));
                nj_rput(vm, copy, f, dst);
            } else if (strcmp(f->desc, "L" NJ_C_VECTOR ";") == 0) {
                nj_value count, a[2], item;
                nj_s4 k;
                src = nj_rget(vm, self, f);
                if (!src) { continue; }
                dst = nj_root(vm, nj_object_new(vm, NJ_C_VECTOR));
                if (!dst || nj_call(vm, dst, NJ_C_VECTOR, "<init>", "()V", NULL, NULL) != NJ_OK) { return 0; }
                nj_rput(vm, copy, f, dst);
                if (nj_call(vm, src, NJ_C_VECTOR, "size", "()I", NULL, &count) != NJ_OK) { return 0; }
                for (k = 0; k < count.i; k++) {
                    a[0].i = k;
                    if (nj_call(vm, src, NJ_C_VECTOR, "elementAt", "(I)Ljava/lang/Object;", a, &item) != NJ_OK) { return 0; }
                    if (strcmp(f->name, "_nj_kids") == 0 && item.r) {
                        nj_field *parent;
                        item.r = m3g_copy_object(vm, item.r, depth + 1u);
                        if (!item.r) { return 0; }
                        parent = nj_field_lookup(vm, NJ_C_NODE, "_nj_parent", "L" NJ_C_NODE ";", 0);
                        nj_rput(vm, item.r, parent, copy);
                    }
                    a[0].r = item.r;
                    if (nj_call(vm, dst, NJ_C_VECTOR, "addElement", "(Ljava/lang/Object;)V", a, NULL) != NJ_OK) { return 0; }
                }
            }
        }
    }
    return copy;
}

NJFN(m3g_duplicate)
{
    nj_u4 mark = nj_root_mark(vm);
    ret->r = m3g_copy_object(vm, NJ_THIS(args), 0u);
    nj_root_release(vm, mark);
    return ret->r ? NJ_OK : nj_throw_oom(vm);
}

/*
 * resetLights() and the Graphics3D constructor have nothing to do: this
 * module keeps no light state and no per-instance state beyond its fields.
 * They are bound to a named native rather than to NULL so that the method
 * table and the native table agree - test_natives checks exactly that, and a
 * NULL entry would be reported as a missing implementation.
 */
NJFN(m3g_noop_init)
{
    (void)args;
    return NJ_OK;
}

/*
 * Animation tracks are stored but not interpolated: animate() reports "no
 * change" rather than a wrong pose, and the three accessors agree with each
 * other because they share one Vector field.
 */
static nj_ref track_vec(nj_vm *vm, nj_ref self, int create)
{
    nj_field *f = nj_field_lookup(vm, NJ_C_O3D, "_nj_tracks",
                                  "L" NJ_C_VECTOR ";", 0);
    nj_ref v = nj_rget(vm, self, f);

    if (!v && create) {
        v = nj_object_new(vm, NJ_C_VECTOR);
        if (!v) {
            return 0;
        }
        nj_call(vm, v, NJ_C_VECTOR, "<init>", "()V", NULL, NULL);
        nj_rput(vm, self, f, v);
    }
    return v;
}

NJFN(m3g_track_add)
{
    nj_ref v = track_vec(vm, NJ_THIS(args), 1);
    nj_value a[5], n;

    if (!v) {
        return nj_throw_oom(vm);
    }
    a[0].r = NJ_ARG_REF(args, 0);
    if (nj_call(vm, v, NJ_C_VECTOR, "addElement", "(Ljava/lang/Object;)V",
                a, &n) != NJ_OK) {
        return NJ_ERR;
    }
    return NJ_OK;
}

NJFN(m3g_track_count)
{
    nj_ref v = track_vec(vm, NJ_THIS(args), 0);
    nj_value n;

    if (!v) {
        ret->i = 0;
        return NJ_OK;
    }
    if (nj_call(vm, v, NJ_C_VECTOR, "size", "()I", NULL, &n) != NJ_OK) {
        return NJ_ERR;
    }
    ret->i = n.i;
    return NJ_OK;
}

NJFN(m3g_track_get)
{
    nj_ref v = track_vec(vm, NJ_THIS(args), 0);
    nj_value a[5], n;

    if (!v) {
        NJ_THROW(vm, "java/lang/IndexOutOfBoundsException", "no animation tracks");
    }
    a[0].i = NJ_ARG_INT(args, 0);
    if (nj_call(vm, v, NJ_C_VECTOR, "elementAt", "(I)Ljava/lang/Object;",
                a, &n) != NJ_OK) {
        return NJ_ERR;
    }
    ret->r = n.r;
    return NJ_OK;
}

/* ---- orientation --------------------------------------------------- */
/*
 * setOrientation(angle, ax, ay, az) sets the rotation part and keeps the
 * other two, which is the specification's behaviour and the reason the scale
 * is read back out first.  The scale is then restored around the new basis.
 */
NJFN(m3g_xable_setOrientation)
{
    float *m = mat_of(vm, NJ_THIS(args), NJ_C_XABLE, "_nj_m");
    float sx, sy, sz;

    if (!m) {
        return NJ_OK;
    }
    sx = sqrtf(m[0] * m[0] + m[4] * m[4] + m[8] * m[8]);
    sy = sqrtf(m[1] * m[1] + m[5] * m[5] + m[9] * m[9]);
    sz = sqrtf(m[2] * m[2] + m[6] * m[6] + m[10] * m[10]);
    {
        float r[NJ_MF], keep[NJ_MF];
        memcpy(r, m, sizeof(r));
        r[0] = r[5] = r[10] = 1.0f;
        r[1] = r[2] = r[4] = r[6] = r[8] = r[9] = 0.0f;
        mat_rotate(r, NJ_ARG_FLOAT(args, 0), NJ_ARG_FLOAT(args, 1),
                   NJ_ARG_FLOAT(args, 2), NJ_ARG_FLOAT(args, 3));
        memcpy(keep, m, sizeof(keep));
        m[0] = r[0] * sx; m[1] = r[1] * sy; m[2] = r[2] * sz;
        m[4] = r[4] * sx; m[5] = r[5] * sy; m[6] = r[6] * sz;
        m[8] = r[8] * sx; m[9] = r[9] * sy; m[10] = r[10] * sz;
        m[3] = keep[3]; m[7] = keep[7]; m[11] = keep[11];
    }
    return NJ_OK;
}

NJFN(m3g_xable_getOrientation)
{
    nj_ref out = NJ_ARG_REF(args, 0);
    float *m = mat_of(vm, NJ_THIS(args), NJ_C_XABLE, "_nj_m");
    float qw, qx, qy, qz, tr;

    if (!out || out->length < 4) {
        NJ_THROW(vm, "java/lang/IllegalArgumentException",
                 "getOrientation fills a float[4]");
    }
    if (!m) {
        ((float *)out->u.bytes)[0] = 0.0f;
        ((float *)out->u.bytes)[1] = 0.0f;
        ((float *)out->u.bytes)[2] = 0.0f;
        ((float *)out->u.bytes)[3] = 1.0f;
        return NJ_OK;
    }
    {
        float sx = sqrtf(m[0] * m[0] + m[4] * m[4] + m[8] * m[8]);
        float sy = sqrtf(m[1] * m[1] + m[5] * m[5] + m[9] * m[9]);
        float sz = sqrtf(m[2] * m[2] + m[6] * m[6] + m[10] * m[10]);
        float b00 = sx > 0.0f ? m[0] / sx : 0.0f;
        float b01 = sy > 0.0f ? m[1] / sy : 0.0f;
        float b02 = sz > 0.0f ? m[2] / sz : 0.0f;
        float b10 = sx > 0.0f ? m[4] / sx : 0.0f;
        float b11 = sy > 0.0f ? m[5] / sy : 0.0f;
        float b12 = sz > 0.0f ? m[6] / sz : 0.0f;
        float b20 = sx > 0.0f ? m[8] / sx : 0.0f;
        float b21 = sy > 0.0f ? m[9] / sy : 0.0f;
        float b22 = sz > 0.0f ? m[10] / sz : 0.0f;

        tr = b00 + b11 + b22;
        if (tr > 0.0f) {
            float s2 = sqrtf(tr + 1.0f) * 2.0f;
            qw = 0.25f * s2;
            qx = (b21 - b12) / s2;
            qy = (b02 - b20) / s2;
            qz = (b10 - b01) / s2;
        } else if (b00 > b11 && b00 > b22) {
            float s2 = sqrtf(1.0f + b00 - b11 - b22) * 2.0f;
            qw = (b21 - b12) / s2;
            qx = 0.25f * s2;
            qy = (b01 + b10) / s2;
            qz = (b02 + b20) / s2;
        } else if (b11 > b22) {
            float s2 = sqrtf(1.0f + b11 - b00 - b22) * 2.0f;
            qw = (b02 - b20) / s2;
            qx = (b01 + b10) / s2;
            qy = 0.25f * s2;
            qz = (b12 + b21) / s2;
        } else {
            float s2 = sqrtf(1.0f + b22 - b00 - b11) * 2.0f;
            qw = (b10 - b01) / s2;
            qx = (b02 + b20) / s2;
            qy = (b12 + b21) / s2;
            qz = 0.25f * s2;
        }
    }
    /* The JSR writes angle first, then the axis - the opposite of the
     * constructor argument order, and a classic source of silent mirrors. */
    if (qw < 0.0f) { qw = -qw; qx = -qx; qy = -qy; qz = -qz; }
    {
        float len = sqrtf(qx * qx + qy * qy + qz * qz);
        float ang = 2.0f * atan2f(len, qw) * 180.0f / 3.14159265358979f;
        ((float *)out->u.bytes)[0] = ang;
        if (len > 0.0f) {
            ((float *)out->u.bytes)[1] = qx / len;
            ((float *)out->u.bytes)[2] = qy / len;
            ((float *)out->u.bytes)[3] = qz / len;
        } else {
            ((float *)out->u.bytes)[1] = 0.0f;
            ((float *)out->u.bytes)[2] = 1.0f;
            ((float *)out->u.bytes)[3] = 0.0f;
        }
    }
    return NJ_OK;
}

NJFN(m3g_animate)
{
    /*
     * Animation tracks are stored (addAnimationTrack/getAnimationTrack), but
     * nothing interpolates them yet: a title that asks for an animation gets
     * "no change" rather than a wrong pose.  Returns the JSR's "invalid
     * time" for the same reason.
     */
    (void)args;
    ret->i = 0;
    return NJ_OK;
}

/* ---- Transformable ------------------------------------------------- */
NJFN(m3g_xable_setTransform)
{
    float *d = mat_of(vm, NJ_THIS(args), NJ_C_XABLE, "_nj_m");
    nj_ref xf = NJ_ARG_REF(args, 0);
    float *s = mat_of(vm, xf, NJ_C_XFORM, "_nj_m");

    if (!d) {
        return NJ_OK;
    }
    if (!xf) {
        /*
         * setTransform(null) resets the node to the identity, which is what
         * the JSR says and what a renderer uses to undo a temporary
         * placement: The Run resets its track nodes this way before every
         * World render.  Ignoring the null left the previous placement in
         * place, so the race world stayed where a previous frame had moved
         * it - far outside the camera - and the screen showed only the
         * background clear.
         */
        mat_identity(d);
    } else if (s) {
        memcpy(d, s, sizeof(float) * NJ_MF);
    }
    return NJ_OK;
}

NJFN(m3g_xable_getTransform)
{
    float *s = mat_of(vm, NJ_THIS(args), NJ_C_XABLE, "_nj_m");
    float *d = mat_of(vm, NJ_ARG_REF(args, 0), NJ_C_XFORM, "_nj_m");
    if (s && d) {
        memcpy(d, s, sizeof(float) * NJ_MF);
    }
    return NJ_OK;
}

NJFN(m3g_xable_setScale)
{
    float *m = mat_of(vm, NJ_THIS(args), NJ_C_XABLE, "_nj_m");
    if (!m) {
        return NJ_OK;
    }
    /*
     * setScale replaces the scale, keeping translation and rotation.  The
     * basis vectors are the first three columns; normalise each to the
     * requested length.  A zero scale collapses the axis, as it should.
     */
    {
        float sx = NJ_ARG_FLOAT(args, 0);
        float sy = NJ_ARG_FLOAT(args, 1);
        float sz = NJ_ARG_FLOAT(args, 2);
        float cx = sqrtf(m[0] * m[0] + m[4] * m[4] + m[8] * m[8]);
        float cy = sqrtf(m[1] * m[1] + m[5] * m[5] + m[9] * m[9]);
        float cz = sqrtf(m[2] * m[2] + m[6] * m[6] + m[10] * m[10]);
        int i;

        for (i = 0; i < 3; i++) {
            m[i * 4 + 0] = (cx > 0.0f) ? m[i * 4 + 0] / cx * sx : 0.0f;
            m[i * 4 + 1] = (cy > 0.0f) ? m[i * 4 + 1] / cy * sy : 0.0f;
            m[i * 4 + 2] = (cz > 0.0f) ? m[i * 4 + 2] / cz * sz : 0.0f;
        }
    }
    return NJ_OK;
}

NJFN(m3g_xable_getScale)
{
    float *m = mat_of(vm, NJ_THIS(args), NJ_C_XABLE, "_nj_m");
    nj_ref out = NJ_ARG_REF(args, 0);

    if (!out || out->length < 3) {
        NJ_THROW(vm, "java/lang/IllegalArgumentException", "getScale fills a float[3]");
    }
    ((float *)out->u.bytes)[0] = m ? sqrtf(m[0] * m[0] + m[4] * m[4] + m[8] * m[8]) : 0.0f;
    ((float *)out->u.bytes)[1] = m ? sqrtf(m[1] * m[1] + m[5] * m[5] + m[9] * m[9]) : 0.0f;
    ((float *)out->u.bytes)[2] = m ? sqrtf(m[2] * m[2] + m[6] * m[6] + m[10] * m[10]) : 0.0f;
    return NJ_OK;
}

NJFN(m3g_xable_setTranslation)
{
    float *m = mat_of(vm, NJ_THIS(args), NJ_C_XABLE, "_nj_m");
    if (m) {
        m[3] = NJ_ARG_FLOAT(args, 0);
        m[7] = NJ_ARG_FLOAT(args, 1);
        m[11] = NJ_ARG_FLOAT(args, 2);
    }
    return NJ_OK;
}

NJFN(m3g_xable_getTranslation)
{
    float *m = mat_of(vm, NJ_THIS(args), NJ_C_XABLE, "_nj_m");
    nj_ref out = NJ_ARG_REF(args, 0);

    if (!out || out->length < 3) {
        NJ_THROW(vm, "java/lang/IllegalArgumentException",
                 "getTranslation fills a float[3]");
    }
    ((float *)out->u.bytes)[0] = m ? m[3] : 0.0f;
    ((float *)out->u.bytes)[1] = m ? m[7] : 0.0f;
    ((float *)out->u.bytes)[2] = m ? m[11] : 0.0f;
    return NJ_OK;
}

/*
 * The three moves that follow placement.
 *
 * translate, scale and postRotate are Transform's moves applied to the
 * object's own matrix, and they are how a title positions a node after
 * creating it - 毁灭战士3_10598 rotates each part about its own origin and then
 * moves it along its own axes, and it has no other way to say that.
 */
NJFN(m3g_xable_translate)
{
    float *m = mat_of(vm, NJ_THIS(args), NJ_C_XABLE, "_nj_m");
    if (m) {
        mat_translate(m, NJ_ARG_FLOAT(args, 0), NJ_ARG_FLOAT(args, 1),
                      NJ_ARG_FLOAT(args, 2));
    }
    return NJ_OK;
}

NJFN(m3g_xable_scale)
{
    float *m = mat_of(vm, NJ_THIS(args), NJ_C_XABLE, "_nj_m");
    if (m) {
        mat_scale(m, NJ_ARG_FLOAT(args, 0), NJ_ARG_FLOAT(args, 1),
                  NJ_ARG_FLOAT(args, 2));
    }
    return NJ_OK;
}

NJFN(m3g_xable_postRotate)
{
    float *m = mat_of(vm, NJ_THIS(args), NJ_C_XABLE, "_nj_m");
    if (m) {
        mat_rotate(m, NJ_ARG_FLOAT(args, 0), NJ_ARG_FLOAT(args, 1),
                   NJ_ARG_FLOAT(args, 2), NJ_ARG_FLOAT(args, 3));
    }
    return NJ_OK;
}

/*
 * getCompositeTransform: this node's matrix with every ancestor's applied, so
 * a part can find out where it ended up.  The parent chain is walked upwards
 * and each parent multiplies from the left; the depth cap is there because a
 * parent chain that somehow became a cycle would otherwise never end, and a
 * transform that is wrong is better than a runtime that hangs.
 */
#define NJ_M3G_MAX_DEPTH 64

NJFN(m3g_xable_getCompositeTransform)
{
    float acc[NJ_MF], tmp[NJ_MF];
    nj_ref out = NJ_ARG_REF(args, 0);
    float *dst = mat_of(vm, out, NJ_C_XFORM, "_nj_m");
    nj_ref node = NJ_THIS(args);
    float *own;
    int depth = 0;

    if (!dst) {
        return NJ_OK;
    }
    own = mat_of(vm, node, NJ_C_XABLE, "_nj_m");
    if (own) {
        memcpy(acc, own, sizeof(acc));
    } else {
        mat_identity(acc);
    }
    node = nj_rget(vm, node, nj_field_lookup(vm, NJ_C_NODE, "_nj_parent",
                                             "L" NJ_C_NODE ";", 0));
    while (node && depth < NJ_M3G_MAX_DEPTH) {
        float *pm = mat_of(vm, node, NJ_C_XABLE, "_nj_m");
        if (pm) {
            mat_mul(pm, acc, tmp);
            memcpy(acc, tmp, sizeof(acc));
        }
        node = nj_rget(vm, node, nj_field_lookup(vm, NJ_C_NODE, "_nj_parent",
                                                 "L" NJ_C_NODE ";", 0));
        depth++;
    }
    memcpy(dst, acc, sizeof(float) * NJ_MF);
    return NJ_OK;
}

NJFN(m3g_node_scope_set)
{
    nj_iput(vm, NJ_THIS(args),
            nj_field_lookup(vm, NJ_C_NODE, "_nj_scope", "I", 0),
            NJ_ARG_INT(args, 0));
    return NJ_OK;
}

NJFN(m3g_node_scope_get)
{
    ret->i = nj_iget(vm, NJ_THIS(args),
                     nj_field_lookup(vm, NJ_C_NODE, "_nj_scope", "I", 0));
    return NJ_OK;
}

NJFN(m3g_node_pick_set)
{
    nj_iput(vm, NJ_THIS(args),
            nj_field_lookup(vm, NJ_C_NODE, "_nj_pick", "Z", 0),
            NJ_ARG_INT(args, 0) ? 1 : 0);
    return NJ_OK;
}

NJFN(m3g_node_pick_get)
{
    ret->i = nj_iget(vm, NJ_THIS(args),
                     nj_field_lookup(vm, NJ_C_NODE, "_nj_pick", "Z", 0));
    return NJ_OK;
}

/*
 * setAlignment(zRef, zTarget, yRef, yTarget): "point my Z axis at zRef and my
 * Y axis at yRef".  The references and the axes they stand for are kept.
 */
NJFN(m3g_node_align_set)
{
    nj_ref self = NJ_THIS(args);
    nj_iput(vm, self, nj_field_lookup(vm, NJ_C_NODE, "_nj_alignZA", "I", 0),
            NJ_ARG_INT(args, 1));
    nj_iput(vm, self, nj_field_lookup(vm, NJ_C_NODE, "_nj_alignYA", "I", 0),
            NJ_ARG_INT(args, 3));
    nj_rput(vm, self,
            nj_field_lookup(vm, NJ_C_NODE, "_nj_alignZ", "L" NJ_C_NODE ";", 0),
            NJ_ARG_REF(args, 0));
    nj_rput(vm, self,
            nj_field_lookup(vm, NJ_C_NODE, "_nj_alignY", "L" NJ_C_NODE ";", 0),
            NJ_ARG_REF(args, 2));
    return NJ_OK;
}

NJFN(m3g_xf_transpose)
{
    float *m = mat_of(vm, NJ_THIS(args), NJ_C_XFORM, "_nj_m");
    float t[NJ_MF];
    int r, c;

    if (!m) {
        return NJ_OK;
    }
    for (r = 0; r < 4; r++) {
        for (c = 0; c < 4; c++) {
            t[c * 4 + r] = m[r * 4 + c];
        }
    }
    memcpy(m, t, sizeof(t));
    return NJ_OK;
}

/* ---- Node ---------------------------------------------------------- */
NJFN(m3g_render_set)
{
    int on = NJ_ARG_INT(args, 0) ? 1 : 0;

    nj_iput(vm, NJ_THIS(args),
            nj_field_lookup(vm, NJ_C_NODE, "_nj_render", "Z", 0), on);
    /* The scene graph walk reads this one, so that a node nobody has spoken
       about keeps rendering: see node_fields. */
    nj_iput(vm, NJ_THIS(args),
            nj_field_lookup(vm, NJ_C_NODE, "_nj_hidden", "Z", 0), on ? 0 : 1);
    return NJ_OK;
}

NJFN(m3g_render_get)
{
    nj_field *h = nj_field_lookup(vm, NJ_C_NODE, "_nj_hidden", "Z", 0);

    ret->i = (h && nj_iget(vm, NJ_THIS(args), h)) ? 0 : 1;
    return NJ_OK;
}

NJFN(m3g_alpha_set)
{
    /* "F", the way node_fields declares it: a lookup that asks for the wrong
     * descriptor answers NULL, and the setter then sets nothing. */
    nj_iput(vm, NJ_THIS(args),
            nj_field_lookup(vm, NJ_C_NODE, "_nj_alpha", "F", 0),
            f2bits(NJ_ARG_FLOAT(args, 0)));
    nj_iput(vm, NJ_THIS(args), m3g_fof(vm, NJ_C_NODE, "_nj_alphaSet", "Z"), 1);
    return NJ_OK;
}

NJFN(m3g_parent_get)
{
    ret->r = nj_rget(vm, NJ_THIS(args),
                     nj_field_lookup(vm, NJ_C_NODE, "_nj_parent", "L" NJ_C_NODE ";", 0));
    return NJ_OK;
}

/*
 * getTransformTo(other, out): the transform from this node's space into the
 * other's.  Walking both chains to the root and combining is the definition;
 * a node that is not an ancestor of the other returns false, which is what
 * the JSR says and what titles test.
 */
NJFN(m3g_getTransformTo)
{
    nj_ref target = NJ_ARG_REF(args, 0);
    float *out = mat_of(vm, NJ_ARG_REF(args, 1), NJ_C_XFORM, "_nj_m");
    nj_field *pf;
    float wa[NJ_MF], wb[NJ_MF];
    nj_ref a = NJ_THIS(args), b = target, ra = 0, rb = 0;
    int depth;

    if (!out) {
        return NJ_OK;
    }
    if (!target) {
        ret->i = 0;              /* a title that passes null gets a false */
        return NJ_OK;
    }
    pf = nj_field_lookup(vm, NJ_C_NODE, "_nj_parent", "L" NJ_C_NODE ";", 0);
    a = NJ_THIS(args);
    b = target;
    mat_identity(wa);
    for (depth = 0; a && depth < 64; depth++) {
        float *m = mat_of(vm, a, NJ_C_XABLE, "_nj_m");
        if (m) {
            float next[NJ_MF];
            /* A parent's matrix multiplies on the left: world =
               parent * ... * own, as M3G composes node transforms. */
            mat_mul(m, wa, next);
            memcpy(wa, next, sizeof(wa));
        }
        ra = a;
        a = nj_rget(vm, a, pf);
    }
    mat_identity(wb);
    for (depth = 0; b && depth < 64; depth++) {
        float *m = mat_of(vm, b, NJ_C_XABLE, "_nj_m");
        if (m) {
            float next[NJ_MF];
            mat_mul(m, wb, next);
            memcpy(wb, next, sizeof(wb));
        }
        rb = b;
        b = nj_rget(vm, b, pf);
    }
    if (!ra || ra != rb) {
        ret->i = 0;              /* separate scene graphs: no answer */
        return NJ_OK;
    }
    /*
     * This node's world matrix and the other node's world matrix give the
     * answer outright: a point goes from this node's space to the world with
     * wa, and from the world into the other node's space with the inverse of
     * wb.  Composing the two chains up to a common ancestor and inverting
     * only the target side is the same thing, and this form also covers
     * siblings and cousins, which the ancestor-only case missed - the racing
     * title asks for the transform between a wheel and the car body, and the
     * old code returned a matrix missing the body's part of the chain.
     */
    mat_invert(wb);
    mat_mul(wb, wa, out);
    ret->i = 1;
    return NJ_OK;
}

/* ---- Group --------------------------------------------------------- */
NJFN(m3g_addChild)
{
    nj_ref self = NJ_THIS(args);
    nj_ref child = NJ_ARG_REF(args, 0);
    nj_ref kids = nj_rget(vm, self, nj_field_lookup(vm, NJ_C_GROUP, "_nj_kids",
                                                    "L" NJ_C_VECTOR ";", 0));
    nj_field *pf = nj_field_lookup(vm, NJ_C_NODE, "_nj_parent", "L" NJ_C_NODE ";", 0);
    nj_value a[5];
    nj_value n;

    if (!child) {
        NJ_THROW(vm, "java/lang/NullPointerException", "addChild(null)");
    }
    if (nj_rget(vm, child, pf)) {
        NJ_THROW(vm, "java/lang/IllegalStateException",
                 "the node already has a parent");
    }
    /*
     * A child the FILE listed is already in this vector - m3g_load put it
     * there and deliberately left its parent unset - so the game calling
     * addChild() for it must adopt it and succeed, not fail and not append a
     * second copy.  A node with some other parent has already been refused
     * above, which is why this check sits after that one.
     */
    if (kids) {
        nj_value ia[5], ir;
        ia[0].r = child;
        if (nj_call(vm, kids, NJ_C_VECTOR, "indexOf", "(Ljava/lang/Object;)I",
                    ia, &ir) == NJ_OK && ir.i >= 0) {
            nj_rput(vm, child, pf, self);
            return NJ_OK;
        }
    }
    if (!kids) {
        kids = nj_object_new(vm, NJ_C_VECTOR);
        if (!kids) {
            return nj_throw_oom(vm);
        }
        nj_call(vm, kids, NJ_C_VECTOR, "<init>", "()V", NULL, NULL);
        nj_rput(vm, self, nj_field_lookup(vm, NJ_C_GROUP, "_nj_kids",
                                          "L" NJ_C_VECTOR ";", 0), kids);
    }
    a[0].r = child;
    if (nj_call(vm, kids, NJ_C_VECTOR, "addElement",
                "(Ljava/lang/Object;)V", a, &n) != NJ_OK) {
        return NJ_ERR;
    }
    nj_rput(vm, child, pf, self);
    return NJ_OK;
}

NJFN(m3g_removeChild)
{
    nj_ref self = NJ_THIS(args);
    nj_ref child = NJ_ARG_REF(args, 0);
    nj_ref kids = nj_rget(vm, self, nj_field_lookup(vm, NJ_C_GROUP, "_nj_kids",
                                                    "L" NJ_C_VECTOR ";", 0));
    nj_value a[5], n;

    if (!child || !kids) {
        return NJ_OK;
    }
    a[0].r = child;
    if (nj_call(vm, kids, NJ_C_VECTOR, "removeElement",
                "(Ljava/lang/Object;)Z", a, &n) != NJ_OK) {
        return NJ_ERR;
    }
    nj_rput(vm, child, nj_field_lookup(vm, NJ_C_NODE, "_nj_parent",
                                       "L" NJ_C_NODE ";", 0), 0);
    return NJ_OK;
}

NJFN(m3g_getChildCount)
{
    nj_ref kids = nj_rget(vm, NJ_THIS(args), nj_field_lookup(vm, NJ_C_GROUP,
                                                             "_nj_kids",
                                                             "L" NJ_C_VECTOR ";", 0));
    nj_value n;

    if (!kids) {
        ret->i = 0;
        return NJ_OK;
    }
    if (nj_call(vm, kids, NJ_C_VECTOR, "size", "()I", NULL, &n) != NJ_OK) {
        return NJ_ERR;
    }
    ret->i = n.i;
    return NJ_OK;
}

NJFN(m3g_getChild)
{
    nj_ref kids = nj_rget(vm, NJ_THIS(args), nj_field_lookup(vm, NJ_C_GROUP,
                                                             "_nj_kids",
                                                             "L" NJ_C_VECTOR ";", 0));
    nj_value a[5], n;

    if (!kids) {
        NJ_THROW(vm, "java/lang/IndexOutOfBoundsException", "no children");
    }
    a[0].i = NJ_ARG_INT(args, 0);
    if (nj_call(vm, kids, NJ_C_VECTOR, "elementAt",
                "(I)Ljava/lang/Object;", a, &n) != NJ_OK) {
        return NJ_ERR;
    }
    ret->r = n.r;
    return NJ_OK;
}

/* ---- World --------------------------------------------------------- */
NJFN(m3g_setActiveCamera)
{
    nj_rput(vm, NJ_THIS(args),
            nj_field_lookup(vm, NJ_C_WORLD, "_nj_cam", "L" NJ_C_CAMERA ";", 0),
            NJ_ARG_REF(args, 0));
    return NJ_OK;
}

NJFN(m3g_setBackground)
{
    nj_rput(vm, NJ_THIS(args),
            nj_field_lookup(vm, NJ_C_WORLD, "_nj_bg", "L" NJ_C_BG ";", 0),
            NJ_ARG_REF(args, 0));
    return NJ_OK;
}

/*
 * The getters were missing, and the corpus calls them: three references to
 * World.getActiveCamera and two to World.getBackground across the collected
 * titles, all of which died with NoSuchMethodError before this - 毁灭战士3_10598
 * got as far as loading its model and then asked the World which camera it
 * should render with.
 */
NJFN(m3g_getActiveCamera)
{
    ret->r = nj_rget(vm, NJ_THIS(args),
                     nj_field_lookup(vm, NJ_C_WORLD, "_nj_cam",
                                     "L" NJ_C_CAMERA ";", 0));
    return NJ_OK;
}

NJFN(m3g_getBackground)
{
    ret->r = nj_rget(vm, NJ_THIS(args),
                     nj_field_lookup(vm, NJ_C_WORLD, "_nj_bg",
                                     "L" NJ_C_BG ";", 0));
    return NJ_OK;
}

/* ---- Background ---------------------------------------------------- */
NJFN(m3g_bg_setColor)
{
    nj_iput(vm, NJ_THIS(args),
            nj_field_lookup(vm, NJ_C_BG, "_nj_color", "I", 0),
            NJ_ARG_INT(args, 0));
    return NJ_OK;
}

NJFN(m3g_bg_setColorClear)
{
    nj_iput(vm, NJ_THIS(args),
            nj_field_lookup(vm, NJ_C_BG, "_nj_cclear", "Z", 0),
            NJ_ARG_INT(args, 0) ? 1 : 0);
    return NJ_OK;
}

NJFN(m3g_bg_setDepthClear)
{
    nj_iput(vm, NJ_THIS(args),
            nj_field_lookup(vm, NJ_C_BG, "_nj_dclear", "Z", 0),
            NJ_ARG_INT(args, 0) ? 1 : 0);
    return NJ_OK;
}

NJFN(m3g_bg_isColorClear)
{
    ret->i = nj_iget(vm, NJ_THIS(args),
                     nj_field_lookup(vm, NJ_C_BG, "_nj_cclear", "Z", 0)) ? 1 : 0;
    return NJ_OK;
}

NJFN(m3g_bg_isDepthClear)
{
    ret->i = nj_iget(vm, NJ_THIS(args),
                     nj_field_lookup(vm, NJ_C_BG, "_nj_dclear", "Z", 0)) ? 1 : 0;
    return NJ_OK;
}

/*
 * The background is what a World draws behind the scene: a colour, and - when
 * a title wants a bitmap behind it - an image with a crop window and an image
 * mode.  毁灭战士3_10598 sets one, and the image it hands over is the sky.
 */
NJFN(m3g_bg_setImage)
{
    nj_rput(vm, NJ_THIS(args),
            nj_field_lookup(vm, NJ_C_BG, "_nj_img", "L" NJ_C_IMAGE2D ";", 0),
            NJ_ARG_REF(args, 0));
    return NJ_OK;
}

NJFN(m3g_bg_getImage)
{
    ret->r = nj_rget(vm, NJ_THIS(args),
                     nj_field_lookup(vm, NJ_C_BG, "_nj_img",
                                     "L" NJ_C_IMAGE2D ";", 0));
    return NJ_OK;
}

NJFN(m3g_bg_setImageMode)
{
    nj_ref self = NJ_THIS(args);

    nj_iput(vm, self, m3g_fof(vm, NJ_C_BG, "_nj_immode", "I"),
            NJ_ARG_INT(args, 0));
    nj_iput(vm, self, m3g_fof(vm, NJ_C_BG, "_nj_immodeY", "I"),
            NJ_ARG_INT(args, 1));
    return NJ_OK;
}

NJFN(m3g_bg_getImageMode)
{
    ret->i = nj_iget(vm, NJ_THIS(args),
                     nj_field_lookup(vm, NJ_C_BG, "_nj_immode", "I", 0));
    return NJ_OK;
}

NJFN(m3g_bg_setCrop)
{
    nj_ref self = NJ_THIS(args);
    nj_iput(vm, self, nj_field_lookup(vm, NJ_C_BG, "_nj_cropX", "I", 0),
            NJ_ARG_INT(args, 0));
    nj_iput(vm, self, nj_field_lookup(vm, NJ_C_BG, "_nj_cropY", "I", 0),
            NJ_ARG_INT(args, 1));
    nj_iput(vm, self, nj_field_lookup(vm, NJ_C_BG, "_nj_cropW", "I", 0),
            NJ_ARG_INT(args, 2));
    nj_iput(vm, self, nj_field_lookup(vm, NJ_C_BG, "_nj_cropH", "I", 0),
            NJ_ARG_INT(args, 3));
    return NJ_OK;
}

NJFN(m3g_bg_getCropX)
{
    ret->i = nj_iget(vm, NJ_THIS(args),
                     nj_field_lookup(vm, NJ_C_BG, "_nj_cropX", "I", 0));
    return NJ_OK;
}

NJFN(m3g_bg_getCropY)
{
    ret->i = nj_iget(vm, NJ_THIS(args),
                     nj_field_lookup(vm, NJ_C_BG, "_nj_cropY", "I", 0));
    return NJ_OK;
}

NJFN(m3g_bg_getCropW)
{
    ret->i = nj_iget(vm, NJ_THIS(args),
                     nj_field_lookup(vm, NJ_C_BG, "_nj_cropW", "I", 0));
    return NJ_OK;
}

NJFN(m3g_bg_getCropH)
{
    ret->i = nj_iget(vm, NJ_THIS(args),
                     nj_field_lookup(vm, NJ_C_BG, "_nj_cropH", "I", 0));
    return NJ_OK;
}

/* ---- Camera -------------------------------------------------------- */
NJFN(m3g_setPerspective)
{
    nj_ref self = NJ_THIS(args);
    nj_iput(vm, self, nj_field_lookup(vm, NJ_C_CAMERA, "_nj_is_persp", "Z", 0), 1);
    nj_iput(vm, self, nj_field_lookup(vm, NJ_C_CAMERA, "_nj_is_parallel", "Z", 0), 0);
    nj_rput(vm, self, nj_field_lookup(vm, NJ_C_CAMERA, "_nj_generic", "[F", 0), 0);
    {
        static const char *names[4] = { "_nj_fovy", "_nj_aspect", "_nj_near", "_nj_far" };
        int i;
        for (i = 0; i < 4; i++) {
            nj_iput(vm, self, nj_field_lookup(vm, NJ_C_CAMERA, names[i], "I", 0),
                    f2bits(NJ_ARG_FLOAT(args, i)));
        }
    }
    return NJ_OK;
}

NJFN(m3g_setParallel)
{
    nj_ref self = NJ_THIS(args);
    nj_iput(vm, self, nj_field_lookup(vm, NJ_C_CAMERA, "_nj_is_parallel", "Z", 0), 1);
    nj_iput(vm, self, nj_field_lookup(vm, NJ_C_CAMERA, "_nj_is_persp", "Z", 0), 0);
    nj_rput(vm, self, nj_field_lookup(vm, NJ_C_CAMERA, "_nj_generic", "[F", 0), 0);
    {
        static const char *names[4] = { "_nj_height", "_nj_aspect", "_nj_near", "_nj_far" };
        int i;
        for (i = 0; i < 4; i++) {
            nj_iput(vm, self, nj_field_lookup(vm, NJ_C_CAMERA, names[i], "I", 0),
                    f2bits(NJ_ARG_FLOAT(args, i)));
        }
    }
    return NJ_OK;
}

/*
 * getProjection(out) writes the projection matrix into `out` and returns 1,
 * or leaves it alone and returns 0 when the camera has neither projection -
 * the two return values the JSR defines.
 */
/*
 * setGeneric: the title hands over a projection matrix of its own.  It is kept
 * as given and used instead of the perspective or parallel formula; a camera
 * with a generic projection is neither of the other two.
 */
NJFN(m3g_cam_setGeneric)
{
    nj_ref self = NJ_THIS(args);
    float *src = mat_of(vm, NJ_ARG_REF(args, 0), NJ_C_XFORM, "_nj_m");
    nj_ref a = mat_new(vm);

    if (!src) {
        return NJ_OK;
    }
    if (!a) {
        NJ_THROW(vm, "java/lang/OutOfMemoryError", "Camera.setGeneric");
    }
    memcpy((float *)a->u.bytes, src, sizeof(float) * NJ_MF);
    nj_rput(vm, self, nj_field_lookup(vm, NJ_C_CAMERA, "_nj_generic", "[F", 0), a);
    nj_iput(vm, self, nj_field_lookup(vm, NJ_C_CAMERA, "_nj_is_persp", "Z", 0), 0);
    nj_iput(vm, self, nj_field_lookup(vm, NJ_C_CAMERA, "_nj_is_parallel", "Z", 0), 0);
    return NJ_OK;
}

static int cam_proj_fill(nj_vm *vm, nj_ref self, float *out)
{
    float f, aspect, near, far, fov;
    int perspective = nj_iget(vm, self,
        nj_field_lookup(vm, NJ_C_CAMERA, "_nj_is_persp", "Z", 0));

    {
        nj_ref g = nj_rget(vm, self, nj_field_lookup(vm, NJ_C_CAMERA,
                                                     "_nj_generic", "[F", 0));
        if (g && g->length >= (nj_s4)NJ_MF) {
            memcpy(out, (const float *)g->u.bytes, sizeof(float) * NJ_MF);
            return 1;
        }
    }
    if (!perspective &&
        !nj_iget(vm, self, nj_field_lookup(vm, NJ_C_CAMERA, "_nj_is_parallel",
                                           "Z", 0))) {
        return 0;
    }
    f = bits2f(nj_iget(vm, self, nj_field_lookup(vm, NJ_C_CAMERA, "_nj_fovy",
                                                 "I", 0)));
    if (f == 0.0f) {
        f = 45.0f;
    }
    aspect = bits2f(nj_iget(vm, self, nj_field_lookup(vm, NJ_C_CAMERA,
                                                      "_nj_aspect", "I", 0)));
    if (aspect == 0.0f) {
        aspect = 1.0f;
    }
    near = bits2f(nj_iget(vm, self, nj_field_lookup(vm, NJ_C_CAMERA, "_nj_near",
                                                    "I", 0)));
    /* Only the implicit perspective camera uses zero as a default sentinel.
     * Parallel cameras permit zero and negative clipping distances. */
    if (perspective && near == 0.0f) {
        near = 1.0f;
    }
    far = bits2f(nj_iget(vm, self, nj_field_lookup(vm, NJ_C_CAMERA, "_nj_far",
                                                   "I", 0)));
    if (perspective && far == 0.0f) {
        far = 1000.0f;
    }
    mat_identity(out);
    if (nj_iget(vm, self, nj_field_lookup(vm, NJ_C_CAMERA, "_nj_is_persp",
                                          "Z", 0))) {
        fov = f * 3.14159265358979f / 180.0f;
        out[0] = 1.0f / tanf(fov * 0.5f) / aspect;
        out[5] = 1.0f / tanf(fov * 0.5f);
        out[10] = -(far + near) / (far - near);
        out[11] = -(2.0f * far * near) / (far - near);
        out[14] = -1.0f;
        out[15] = 0.0f;
    } else {
        float height = bits2f(nj_iget(vm, self,
            nj_field_lookup(vm, NJ_C_CAMERA, "_nj_height", "I", 0)));

        if (height == 0.0f) { height = 2.0f; }
        out[0] = 2.0f / (height * aspect);
        out[5] = 2.0f / height;
        out[10] = -2.0f / (far - near);
        out[11] = -(far + near) / (far - near);
    }
    return 1;
}

NJFN(m3g_getProjection)
{
    float *out = mat_of(vm, NJ_ARG_REF(args, 0), NJ_C_XFORM, "_nj_m");

    if (!out) {
        ret->i = 0;
        return NJ_OK;
    }
    ret->i = cam_proj_fill(vm, NJ_THIS(args), out);
    return NJ_OK;
}

/* ---- Graphics3D ---------------------------------------------------- */
/*
 * getInstance() returns one object per VM, kept in a static field of the
 * class itself.  Static fields live in the VM's slot array and are GC roots,
 * so this is per-VM state with nothing global behind it.
 */
NJFN(m3g_getInstance)
{
    nj_field *f = nj_field_lookup(vm, NJ_C_G3D, "_nj_instance", "L" NJ_C_G3D ";", 1);
    nj_ref inst = nj_field_read_ref(vm, NULL, f);

    if (!inst) {
        inst = nj_object_new(vm, NJ_C_G3D);
        if (!inst) {
            return nj_throw_oom(vm);
        }
        nj_call_special(vm, inst, NJ_C_G3D, "<init>", "()V", NULL, NULL);
        nj_field_write_ref(vm, NULL, f, inst);
    }
    ret->r = inst;
    return NJ_OK;
}

/*
 * The Graphics a bound target draws into, with its size.  A target may be a
 * Graphics or a mutable Image2D (which is asked for its Graphics), and a
 * title that binds one and never calls setViewport still draws over the whole
 * of it - 浴火银河2完整版_7689 calls render(World) once a frame after
 * binding and never sets a viewport, and with the viewport left at its
 * initial 0x0 the whole scene was skipped (513 renders, no triangles).
 */
static nj_ref m3g_target_graphics(nj_vm *vm, nj_ref target, int *w, int *h)
{
    const char *cn;
    nj_value got;
    nj_ref g = target;

    *w = 0;
    *h = 0;
    if (!target || !nj_heap_owns(vm, target)) {
        return 0;
    }
    cn = nj_ref_class_name(g);
    if (!cn) {
        return 0;
    }
    if (strcmp(cn, "javax/microedition/lcdui/Image") == 0) {
        got.r = 0;
        if (nj_call(vm, g, "javax/microedition/lcdui/Image", "getGraphics",
                    "()Ljavax/microedition/lcdui/Graphics;", NULL, &got) != NJ_OK
            || !got.r) {
            return 0;
        }
        g = got.r;
    } else if (strcmp(cn, "javax/microedition/lcdui/Graphics") != 0) {
        return 0;                 /* neither of the two target kinds */
    }
    /*
     * getClipWidth()/getClipHeight(), not getWidth()/getHeight(): Graphics
     * has no width or height of its own (calling getWidth on it is a
     * NoSuchMethodError, which is how this was found - the title's thread died
     * on it).  A Graphics nobody has clipped is clipped to its whole target
     * when it is made, so the clip is the surface size here.
     */
    got.i = 0;
    if (nj_call(vm, g, "javax/microedition/lcdui/Graphics", "getClipWidth",
                "()I", NULL, &got) == NJ_OK) {
        *w = (int)got.i;
    }
    got.i = 0;
    if (nj_call(vm, g, "javax/microedition/lcdui/Graphics", "getClipHeight",
                "()I", NULL, &got) == NJ_OK) {
        *h = (int)got.i;
    }
    return g;
}

NJFN(m3g_bindTarget)
{
    nj_ref self = NJ_THIS(args);
    nj_ref target = NJ_ARG_REF(args, 0);
    int w = 0, h = 0;

    nj_rput(vm, self,
            nj_field_lookup(vm, NJ_C_G3D, "_nj_target", "Ljava/lang/Object;", 0),
            target);
    /*
     * "the state of the Graphics3D instance is ... viewport : undefined
     * (reset at bindTarget)" - the reset is to the whole target, so a title
     * that never calls setViewport draws into all of it.  The fields stay 0
     * for a target this runtime cannot draw into, and render() then does
     * nothing.
     */
    if (m3g_target_graphics(vm, target, &w, &h) != 0 && w > 0 && h > 0) {
        nj_iput(vm, self, nj_field_lookup(vm, NJ_C_G3D, "_nj_vx", "I", 0), 0);
        nj_iput(vm, self, nj_field_lookup(vm, NJ_C_G3D, "_nj_vy", "I", 0), 0);
        nj_iput(vm, self, nj_field_lookup(vm, NJ_C_G3D, "_nj_vw", "I", 0), w);
        nj_iput(vm, self, nj_field_lookup(vm, NJ_C_G3D, "_nj_vh", "I", 0), h);
    }
    /* The depth buffer belongs to the target as much as the viewport does:
       binding a new one starts from a clean buffer.  The next render clears
       it (an impossible frame number is what "needs clearing" looks like). */
    nj_iput(vm, self, m3g_fof(vm, NJ_C_G3D, "_nj_zframe", "I"), -1);
    return NJ_OK;
}

NJFN(m3g_releaseTarget)
{
    nj_rput(vm, NJ_THIS(args),
            nj_field_lookup(vm, NJ_C_G3D, "_nj_target", "Ljava/lang/Object;", 0), 0);
    return NJ_OK;
}

NJFN(m3g_setViewport)
{
    nj_ref self = NJ_THIS(args);
    static const char *names[4] = { "_nj_vx", "_nj_vy", "_nj_vw", "_nj_vh" };
    int i;

    for (i = 0; i < 4; i++) {
        nj_iput(vm, self, nj_field_lookup(vm, NJ_C_G3D, names[i], "I", 0),
                NJ_ARG_INT(args, i));
    }
    return NJ_OK;
}

NJFN(m3g_setCamera)
{
    nj_ref self = NJ_THIS(args);
    float *m = mat_of(vm, NJ_ARG_REF(args, 1), NJ_C_XFORM, "_nj_m");

    nj_rput(vm, self, nj_field_lookup(vm, NJ_C_G3D, "_nj_camera", "L" NJ_C_CAMERA ";", 0),
            NJ_ARG_REF(args, 0));
    if (m) {
        nj_field *af = nj_field_lookup(vm, NJ_C_G3D, "_nj_cam_m", "[F", 0);
        nj_ref arr = nj_rget(vm, self, af);
        if (!arr) {
            arr = mat_new(vm);
            nj_rput(vm, self, af, arr);
        }
        if (arr) {
            memcpy(arr->u.bytes, m, sizeof(float) * NJ_MF);
        }
    }
    return NJ_OK;
}

/* The Background image covers the viewport through its crop rectangle.
   BORDER uses the clear colour outside the image; REPEAT wraps each axis. */
static void m3g_background_image(nj_vm *vm, nj_ref bg, nj_ref target,
                                  int vx, int vy, int vw, int vh)
{
    nj_ref image = nj_rget(vm, bg, m3g_fof(vm, NJ_C_BG, "_nj_img", "L" NJ_C_IMAGE2D ";"));
    nj_ref pixels, dst_image = NULL;
    nj_surface scratch, *surface;
    const char *cls = nj_ref_class_name(target);
    int iw, ih, cw, ch, cx, cy, mx, my, fmt, bpp, x, y;
    const nj_u1 *src;

    if (!image || !target || !cls) { return; }
    pixels = nj_rget(vm, image, m3g_fof(vm, NJ_C_IMAGE2D, "_nj_pixels", "[B"));
    iw = nj_iget(vm, image, m3g_fof(vm, NJ_C_IMAGE2D, "_nj_w", "I"));
    ih = nj_iget(vm, image, m3g_fof(vm, NJ_C_IMAGE2D, "_nj_h", "I"));
    fmt = m3g_image_format(vm, image);
    bpp = fmt == 100 ? 4 : fmt == 99 ? 3 : 0;
    if (!pixels || !bpp || iw <= 0 || ih <= 0 ||
        (nj_size)iw * ih * bpp > nj_array_length(pixels)) { return; }
    if (strcmp(cls, "javax/microedition/lcdui/Image") == 0) {
        dst_image = target;
    } else if (strcmp(cls, "javax/microedition/lcdui/Graphics") == 0) {
        dst_image = nj_rget(vm, target, m3g_fof(vm, cls, "gTarget", "Ljavax/microedition/lcdui/Image;"));
    } else { return; }
    surface = nj_midp_surface(vm, dst_image, &scratch);
    if (!surface || surface->format != NJ_PIXFMT_ARGB8888) { return; }
    if (vw <= 0 || vh <= 0) { vx = vy = 0; vw = surface->width; vh = surface->height; }
    cx = nj_iget(vm, bg, m3g_fof(vm, NJ_C_BG, "_nj_cropX", "I"));
    cy = nj_iget(vm, bg, m3g_fof(vm, NJ_C_BG, "_nj_cropY", "I"));
    cw = nj_iget(vm, bg, m3g_fof(vm, NJ_C_BG, "_nj_cropW", "I"));
    ch = nj_iget(vm, bg, m3g_fof(vm, NJ_C_BG, "_nj_cropH", "I"));
    mx = nj_iget(vm, bg, m3g_fof(vm, NJ_C_BG, "_nj_immode", "I"));
    my = nj_iget(vm, bg, m3g_fof(vm, NJ_C_BG, "_nj_immodeY", "I"));
    if (cw <= 0 || ch <= 0) { return; }
    src = (const nj_u1 *)nj_array_data(pixels);
    for (y = 0; y < vh; y++) {
        int sy = cy + (int)(((double)y + 0.5) * ch / vh);
        nj_u4 *row;
        if (vy + y < 0 || vy + y >= surface->height) { continue; }
        if (my == 33) { sy = ((sy % ih) + ih) % ih; }
        if (sy < 0 || sy >= ih) { continue; }
        row = (nj_u4 *)((nj_u1 *)surface->pixels + (nj_size)(vy + y) * surface->stride);
        for (x = 0; x < vw; x++) {
            int sx = cx + (int)(((double)x + 0.5) * cw / vw);
            const nj_u1 *p;
            if (vx + x < 0 || vx + x >= surface->width) { continue; }
            if (mx == 33) { sx = ((sx % iw) + iw) % iw; }
            if (sx < 0 || sx >= iw) { continue; }
            p = src + ((nj_size)sy * iw + sx) * bpp;
            row[vx + x] = 0xFF000000u | ((nj_u4)p[0] << 16) |
                          ((nj_u4)p[1] << 8) | p[2];
        }
    }
}

NJFN(m3g_clear)
{
    nj_ref bg = NJ_ARG_REF(args, 0);
    nj_ref self = NJ_THIS(args);
    nj_ref target;
    nj_s4 argb;

    if (!bg) {
        return NJ_OK;
    }
    if (nj_iget(vm, bg, nj_field_lookup(vm, NJ_C_BG, "_nj_dclear", "Z", 0))) {
        nj_iput(vm, self, m3g_fof(vm, NJ_C_G3D, "_nj_zframe", "I"), -1);
    }
    if (!nj_iget(vm, bg, nj_field_lookup(vm, NJ_C_BG, "_nj_cclear", "Z", 0))) {
        return NJ_OK;               /* the title asked for no colour clear */
    }
    argb = nj_iget(vm, bg, nj_field_lookup(vm, NJ_C_BG, "_nj_color", "I", 0));
    target = nj_rget(vm, self, nj_field_lookup(vm, NJ_C_G3D, "_nj_target",
                                               "Ljava/lang/Object;", 0));
    /*
     * bindTarget(Object) is handed the target surface directly.  A title
     * binds either a Graphics or an Image; both answer to setColor/fillRect,
     * and filling the whole viewport is all a clear has to do.
     */
    if (target && nj_heap_owns(vm, target)) {
        nj_value a[5], n;
        nj_s4 vx = nj_iget(vm, self, nj_field_lookup(vm, NJ_C_G3D, "_nj_vx", "I", 0));
        nj_s4 vy = nj_iget(vm, self, nj_field_lookup(vm, NJ_C_G3D, "_nj_vy", "I", 0));
        nj_s4 vw = nj_iget(vm, self, nj_field_lookup(vm, NJ_C_G3D, "_nj_vw", "I", 0));
        nj_s4 vh = nj_iget(vm, self, nj_field_lookup(vm, NJ_C_G3D, "_nj_vh", "I", 0));

        a[0].i = argb | (nj_s4)0xFF000000u;
        if (nj_call(vm, target, "javax/microedition/lcdui/Graphics", "setColor",
                    "(I)V", a, &n) != NJ_OK) {
            return NJ_OK;           /* not a surface: nothing to clear */
        }
        a[0].i = vx; a[1].i = vy; a[2].i = vw; a[3].i = vh;
        (void)nj_call(vm, target, "javax/microedition/lcdui/Graphics", "fillRect",
                      "(IIII)V", a, &n);
        m3g_background_image(vm, bg, target, vx, vy, vw, vh);
    }
    return NJ_OK;
}


void nj_m3g_project(const float m[16], const float v[3], int w, int h,
                    float out[2])
{
    float x = m[0] * v[0] + m[4] * v[1] + m[8]  * v[2] + m[12];
    float y = m[1] * v[0] + m[5] * v[1] + m[9]  * v[2] + m[13];
    float ww = m[3] * v[0] + m[7] * v[1] + m[11] * v[2] + m[15];

    if (ww == 0.0f) {
        /* A vertex at infinity has no pixel.  It still has to leave `out`
           DETERMINED: the caller builds a triangle from three of these, and
           returning here without writing handed the rasteriser two stack words
           GCC is right to call uninitialised (-Wmaybe-uninitialized, xy[0..5]).
           Zero puts the vertex at the viewport's own origin, so such a
           triangle is degenerate and draws nothing - which is what a vertex at
           infinity deserves. */
        out[0] = 0.0f;
        out[1] = 0.0f;
        return;
    }
    out[0] = (x / ww + 1.0f) * 0.5f * (float)w;
    out[1] = (1.0f - y / ww) * 0.5f * (float)h;
}

nj_u4 nj_m3g_raster_tri(struct nj_gfx *g, const float xy[6], nj_u4 argb)
{
    if (!g || !xy) {
        return 0u;
    }
    nj_gfx_set_color(g, argb | 0xFF000000u);
    nj_gfx_triangle(g, (int)xy[0], (int)xy[1], (int)xy[2], (int)xy[3],
                    (int)xy[4], (int)xy[5], 1);
    return 1u;
}

nj_status nj_m3g_load_geometry(nj_vm *vm, const nj_m3g_object *objs, nj_u4 nobj,
                               nj_ref arr)
{
    nj_u4 i;

    if (!vm || !objs || !arr) {
        return NJ_ERR_STATE;
    }
    for (i = 0u; i < nobj; i++) {
        nj_ref o, values;
        nj_s2 *shorts;
        nj_u4 per, nverts, n, k;
        const nj_u1 *p;

        if (objs[i].type != NJ_M3G_T_VERTEX_ARRAY || !objs[i].fields.data) {
            continue;
        }
        o = nj_array_get_ref(arr, i + 1u);
        if (!o) { continue; }
        per = (nj_u4)objs[i].fields.a * (nj_u4)objs[i].fields.b;
        if (per == 0u) { continue; }
        nverts = objs[i].fields.data_len / per;
        n = nverts * (nj_u4)objs[i].fields.b;
        if (n == 0u) { continue; }

        values = nj_array_new(vm, "S", n);
        if (!values) { continue; }
        values = nj_root(vm, values);
        p = objs[i].fields.data;
        shorts = (nj_s2 *)nj_array_data(values);
        if (!shorts) { continue; }
        for (k = 0u; k < n; k++) {
            /* componentSize 1 is one byte per component, 2 is a
               little-endian pair - the same order the container uses for
               everything else. */
            shorts[k] = (objs[i].fields.a == 2u)
                      ? (nj_s2)((nj_u2)p[k * 2u] | ((nj_u2)p[k * 2u + 1u] << 8))
                      : (nj_s2)(nj_s1)p[k];
            if (objs[i].fields.c == 1u) {
                nj_s4 prev = k < objs[i].fields.b ? 0 : shorts[k - objs[i].fields.b];
                nj_s4 sum = prev + shorts[k];
                shorts[k] = objs[i].fields.a == 1u
                          ? (nj_s2)(nj_s1)(nj_u1)sum : (nj_s2)(nj_u2)sum;
            }
        }
        nj_iput(vm, o, nj_field_lookup(vm, NJ_C_VARR, "_nj_numVerts", "I", 0),
                (nj_s4)nverts);
        nj_iput(vm, o, nj_field_lookup(vm, NJ_C_VARR, "_nj_numComp", "I", 0),
                (nj_s4)objs[i].fields.b);
        nj_iput(vm, o, nj_field_lookup(vm, NJ_C_VARR, "_nj_compSize", "I", 0),
                (nj_s4)objs[i].fields.a);
        nj_rput(vm, o, nj_field_lookup(vm, NJ_C_VARR, "_nj_data", "[S", 0),
                values);
    }
    return NJ_OK;
}


static void collect_walk(nj_vm *vm, nj_ref o, nj_ref *out, float *mats,
                         const float *parent, nj_u4 cap, nj_u4 *n, int depth,
                         int apply_own)
{
    const char *cn;
    nj_ref kids, m;
    nj_value v;
    nj_u4 len, i;
    float model[16];

    if (!o || depth > 32) {          /* a graph a file made cannot be a loop */
        return;
    }
    /*
     * A Node the title switched off takes its children with it, which is what
     * setRenderingEnable() means.  Nothing else in this runtime consults the
     * flag, so without this a mesh a title hid on purpose is still drawn -
     * the rally title hides meshes that way and its 3D view showed a patch of
     * flat tyre texture over the car because of it.
     */
    {
        nj_field *hf = nj_field_lookup(vm, NJ_C_NODE, "_nj_hidden", "Z", 0);

        if (hf && nj_iget(vm, o, hf)) {
            return;
        }
    }
    cn = nj_ref_class_name(o);
    /*
     * Every object this walk visits is a Transformable (a Mesh is a Node, a
     * Group and a World are Groups), so its own matrix may be read - and it
     * must be read only for those classes: nj_field_lookup() finds a field by
     * name in any class, so asking a Camera for "_nj_m" would read whatever
     * sits at that offset in it.
     */
    memcpy(model, parent, sizeof(model));
    m = nj_rget(vm, o, nj_field_lookup(vm, NJ_C_XABLE, "_nj_m", "[F", 0));
    if (apply_own && m && nj_array_length(m) >= 16) {
        mat_mul(parent, (const float *)nj_array_data(m), model);
    }
    /*
     * Which class the object is decides what may be read out of it.  A
     * built-in class has one super, so MorphingMesh and SkinnedMesh are
     * meshes and World is a Group, and asking by name is how this walk knows.
     *
     * nj_field_lookup() is NOT that test: it finds a field by name in any
     * class and nj_rget() then reads that offset out of the object in front
     * of it.  Reading "_nj_kids" that way out of a Camera - which has no such
     * field, and whose own fields sit at those offsets - produced 0x1 as a
     * "Vector", and the renderer then walked into it and crashed.
     */
    if (cn != NULL && (strcmp(cn, NJ_C_MESH) == 0 ||
                       strcmp(cn, NJ_C_MORPH) == 0 ||
                       strcmp(cn, NJ_C_SKIN) == 0 ||
                       strcmp(cn, NJ_C_SPRITE3D) == 0 ||
                       strcmp(cn, NJ_C_M3G_SPRITE) == 0)) {
        if (*n < cap) {
            out[*n] = o;
            if (mats) { memcpy(mats + (*n) * 16u, model, sizeof(model)); }
        }
        (*n)++;
        return;
    }
    if (cn == NULL || (strcmp(cn, NJ_C_GROUP) != 0 &&
                       strcmp(cn, NJ_C_WORLD) != 0)) {
        return;                  /* a Camera or a Light: no children */
    }
    kids = nj_rget(vm, o, nj_field_lookup(vm, NJ_C_GROUP, "_nj_kids",
                                          "L" NJ_C_VECTOR ";", 0));
    if (!kids) { return; }
    v.i = 0;
    if (nj_call(vm, kids, NJ_C_VECTOR, "size", "()I", NULL, &v) != NJ_OK) {
        return;
    }
    len = (nj_u4)v.i;
    for (i = 0u; i < len; i++) {
        nj_value a, r;
        a.i = (nj_s4)i;
        r.r = 0;
        if (nj_call(vm, kids, NJ_C_VECTOR, "elementAt",
                    "(I)Ljava/lang/Object;", &a, &r) != NJ_OK) {
            continue;
        }
        collect_walk(vm, r.r, out, mats, model, cap, n, depth + 1, 1);
    }
}

nj_u4 nj_m3g_collect_meshes_m(nj_vm *vm, nj_ref root, nj_ref *out, float *mats,
                              nj_u4 cap);

nj_u4 nj_m3g_collect_meshes(nj_vm *vm, nj_ref root, nj_ref *out, nj_u4 cap)
{
    return nj_m3g_collect_meshes_m(vm, root, out, NULL, cap);
}

/*
 * The camera's matrix RELATIVE TO THE WORLD: the product of the chain from
 * the World down to the camera node.  A chase camera is a child of the group
 * that carries the car, so its own matrix is only local, and using it puts
 * the eye where that group sits - which drew the rally title's world from the
 * wrong side of the car.  The reference implementation does the same thing
 * (m3gGetTransformTo(world, camera)), which is why it insists the camera be a
 * child of the World being rendered.
 *
 * The composition order is mat_mul()'s: the child's own matrix applied first,
 * then its ancestors', which is "clip = P * V * M * v" read from the leaf up.
 */
static int cam_walk(nj_vm *vm, nj_ref o, nj_ref cam, float *out, int depth)
{
    const char *cn;
    nj_ref kids, m;
    nj_value v;
    nj_u4 len, i;

    if (!o || depth > 32) {
        return 0;
    }
    if (o == cam) {
        memset(out, 0, sizeof(float) * 16u);
        out[0] = out[5] = out[10] = out[15] = 1.0f;
        return 1;
    }
    cn = nj_ref_class_name(o);
    if (cn == NULL || (strcmp(cn, NJ_C_GROUP) != 0 &&
                       strcmp(cn, NJ_C_WORLD) != 0)) {
        return 0;                /* a Camera, a Light, a Sprite: no children */
    }
    kids = nj_rget(vm, o, nj_field_lookup(vm, NJ_C_GROUP, "_nj_kids",
                                          "L" NJ_C_VECTOR ";", 0));
    if (!kids) {
        return 0;
    }
    v.i = 0;
    if (nj_call(vm, kids, NJ_C_VECTOR, "size", "()I", NULL, &v) != NJ_OK) {
        return 0;
    }
    len = (nj_u4)v.i;
    for (i = 0u; i < len; i++) {
        nj_value a, r;
        nj_ref child;
        float sub[16], own[16];

        a.i = (nj_s4)i;
        r.r = 0;
        if (nj_call(vm, kids, NJ_C_VECTOR, "elementAt",
                    "(I)Ljava/lang/Object;", &a, &r) != NJ_OK || !r.r) {
            continue;
        }
        child = r.r;
        if (!cam_walk(vm, child, cam, sub, depth + 1)) {
            continue;
        }
        memset(own, 0, sizeof(own));
        own[0] = own[5] = own[10] = own[15] = 1.0f;
        m = nj_rget(vm, child, nj_field_lookup(vm, NJ_C_XABLE, "_nj_m", "[F", 0));
        if (m && nj_array_length(m) >= 16) {
            memcpy(own, nj_array_data(m), sizeof(own));
        }
        mat_mul(own, sub, out);
        return 1;
    }
    return 0;
}

/*
 * The same walk, and for each mesh the matrix that takes its vertices to
 * world space: the product of every Group transform above it.  A renderer
 * needs that matrix, and recomputing it per mesh would mean walking the
 * graph once per mesh.
 */
nj_u4 nj_m3g_collect_meshes_m(nj_vm *vm, nj_ref root, nj_ref *out, float *mats,
                              nj_u4 cap)
{
    nj_u4 n = 0u;
    float id[16];

    if (!vm || !root) {
        return 0u;
    }
    memset(id, 0, sizeof(id));
    id[0] = id[5] = id[10] = id[15] = 1.0f;
    collect_walk(vm, root, out, mats, id, cap, &n, 0, 1);
    return n;
}

/*
 * Where the filled triangles go.  A target is either the runtime's own
 * surface or a Java Graphics, and the walk should not care which - so the
 * mesh walk hands each projected triangle to a callback.
 */
typedef nj_u4 (*nj_m3g_emit_fn)(void *ud, const float xy[6], nj_u4 argb);

typedef struct {
    nj_vm *vm;
    nj_ref g;
    int ox, oy;     /* the viewport's own origin: Graphics3D.setViewport(x,y,..) */
} m3g_java_target;

static nj_u4 emit_to_java(void *ud, const float xy[6], nj_u4 argb)
{
    m3g_java_target *t = (m3g_java_target *)ud;
    /* Its own name: check_arg_slots.py maps array names file-wide, and this
       file has plenty of five-entry `a`s that this is not. */
    nj_value emit_args[7], r;
    float ox = (float)t->ox, oy = (float)t->oy;

    emit_args[0].i = (nj_s4)(argb | 0xFF000000u);
    if (nj_call(t->vm, t->g, "javax/microedition/lcdui/Graphics", "setColor",
                "(I)V", emit_args, &r) != NJ_OK) {
        return 0u;
    }
    emit_args[0].i = (nj_s4)(xy[0] + ox); emit_args[1].i = (nj_s4)(xy[1] + oy);
    emit_args[2].i = (nj_s4)(xy[2] + ox); emit_args[3].i = (nj_s4)(xy[3] + oy);
    emit_args[4].i = (nj_s4)(xy[4] + ox); emit_args[5].i = (nj_s4)(xy[5] + oy);
    if (nj_call(t->vm, t->g, "javax/microedition/lcdui/Graphics", "fillTriangle",
                "(IIIIII)V", emit_args, &r) != NJ_OK) {
        return 0u;
    }
    return 1u;
}

static nj_u4 emit_to_gfx(void *ud, const float xy[6], nj_u4 argb)
{
    return nj_m3g_raster_tri((struct nj_gfx *)ud, xy, argb);
}

/*
 * One triangle of a Mesh, from three vertex indices.
 *
 * A vertex component is a quantised integer: the file gives a scale and a bias
 * per VertexBuffer and the vertex is component * scale + bias (JSR 184 file
 * format 11.29).  Drawing the raw components instead puts the model at the
 * origin at a size that has nothing to do with the model.
 */
static nj_u4 emit_vertex_tri(nj_m3g_emit_fn emit, void *ud, const nj_s2 *s,
                             nj_u4 comp, nj_u4 a, nj_u4 b, nj_u4 c,
                             float scale, const float *bias,
                             const float m[16], int w, int h, nj_u4 argb)
{
    const nj_u4 ii[3] = { a, b, c };
    float xy[6];
    nj_u4 k, e;

    for (k = 0u; k < 3u; k++) {
        const nj_s2 *p = s + (nj_u4)ii[k] * comp;
        float v[3];

        for (e = 0u; e < 3u; e++) {
            float raw = (e < comp) ? (float)p[e] : 0.0f;
            float off = (bias != NULL) ? bias[e] : 0.0f;

            v[e] = raw * scale + off;
        }
        nj_m3g_project(m, v, w, h, xy + k * 2u);
    }
    return emit(ud, xy, argb);
}

static nj_u4 raster_mesh_with(nj_vm *vm, nj_ref mesh, const float m[16],
                              int w, int h, nj_u4 argb,
                              nj_m3g_emit_fn emit, void *ud)
{
    nj_ref vb, va, data, ib;
    const nj_s2 *s;
    const nj_s4 *idx = NULL;
    const nj_s4 *strips = NULL;
    const float *bias = NULL;
    float scale = 1.0f;
    nj_u4 nverts, comp, nidx = 0u, nstrip = 0u, drawn = 0u, i;

    if (!vm || !mesh || !emit) {
        return 0u;
    }
    vb = nj_rget(vm, mesh, nj_field_lookup(vm, NJ_C_MESH, "_nj_vb", "L" NJ_C_VBUF ";", 0));
    if (!vb) { return 0u; }
    va = nj_rget(vm, vb, nj_field_lookup(vm, NJ_C_VBUF, "_nj_positions",
                                         "L" NJ_C_VARR ";", 0));
    data = nj_rget(vm, va, nj_field_lookup(vm, NJ_C_VARR, "_nj_data", "[S", 0));
    nverts = (nj_u4)nj_iget(vm, va, nj_field_lookup(vm, NJ_C_VARR, "_nj_numVerts", "I", 0));
    comp = (nj_u4)nj_iget(vm, va, nj_field_lookup(vm, NJ_C_VARR, "_nj_numComp", "I", 0));
    s = (const nj_s2 *)nj_array_data(data);
    if (!s || comp < 2u || nverts < 3u) {
        return 0u;
    }
    scale = bits2f(nj_iget(vm, vb, nj_field_lookup(vm, NJ_C_VBUF, "_nj_bias",
                                                   "F", 0)));
    if (!(scale > 0.0f) && !(scale < 0.0f)) {
        scale = 1.0f;      /* setPositions was never called: the JSR's default */
    }
    {
        nj_ref b = nj_rget(vm, vb, nj_field_lookup(vm, NJ_C_VBUF, "_nj_scale",
                                                   "[F", 0));
        if (b && b->length >= 3) {
            bias = (const float *)nj_array_data(b);
        }
    }
    /*
     * The index buffer, when the file gave one.  A TriangleStripArray is a
     * strip and not a list of triangles: triangle j uses indices j, j+1 and
     * j+2, and every odd triangle swaps its last two so the winding stays
     * consistent (JSR 184, TriangleStripArray).  A Mesh without one still
     * draws its vertices as consecutive triples, which is what a Mesh built
     * from a plain array of positions means.
     */
    ib = nj_rget(vm, mesh, nj_field_lookup(vm, NJ_C_MESH, "_nj_ib",
                                           "L" NJ_C_IBUF ";", 0));
    if (ib) {
        nj_ref d = nj_rget(vm, ib, nj_field_lookup(vm, NJ_C_IBUF, "_nj_ib_data",
                                                   "[I", 0));
        nj_ref l = nj_rget(vm, ib, nj_field_lookup(vm, NJ_C_TSA, "_nj_lengths",
                                                   "[I", 0));
        nj_s4 ns = nj_iget(vm, ib, nj_field_lookup(vm, NJ_C_TSA, "_nj_strips",
                                                   "I", 0));

        nidx = (nj_u4)nj_iget(vm, ib, nj_field_lookup(vm, NJ_C_IBUF,
                                                      "_nj_ib_count", "I", 0));
        if (d && nidx > 0u) {
            idx = (const nj_s4 *)nj_array_data(d);
        }
        if (l && ns > 0) {
            strips = (const nj_s4 *)nj_array_data(l);
            nstrip = (nj_u4)ns;
        }
    }
    if (idx != NULL && strips != NULL && nstrip > 0u) {
        nj_u4 at = 0u, st;

        for (st = 0u; st < nstrip && at < nidx; st++) {
            nj_u4 slen = (nj_u4)strips[st];
            nj_u4 t;

            for (t = 0u; t + 3u <= slen && at + t + 2u < nidx; t++) {
                nj_u4 a0 = (nj_u4)idx[at + t];
                nj_u4 a1 = (nj_u4)idx[at + t + 1u];
                nj_u4 a2 = (nj_u4)idx[at + t + 2u];

                if ((t & 1u) != 0u) {
                    nj_u4 sw = a1; a1 = a2; a2 = sw;
                }
                if (a0 < nverts && a1 < nverts && a2 < nverts) {
                    if (emit_vertex_tri(emit, ud, s, comp, a0, a1, a2, scale,
                                        bias, m, w, h, argb) != 0u) {
                        drawn++;
                    }
                }
            }
            at += slen;
        }
    } else {
        for (i = 0u; i + 3u <= nverts; i += 3u) {
            if (emit_vertex_tri(emit, ud, s, comp, i, i + 1u, i + 2u, scale,
                                bias, m, w, h, argb) != 0u) {
                drawn++;
            }
        }
    }
    vm->tris += (nj_u8)drawn;
    return drawn;
}
nj_u4 nj_m3g_raster_mesh(nj_vm *vm, struct nj_gfx *g, nj_ref mesh,
                         const float m[16], int w, int h, nj_u4 argb)
{
    return raster_mesh_with(vm, mesh, m, w, h, argb, emit_to_gfx, g);
}


/*
 * The World path draws through the same software pipeline the immediate
 * render(VertexBuffer, IndexBuffer, Appearance, Transform, int) uses, so it
 * lives with that pipeline further down this file and is declared here.
 */
static nj_u4 m3g_world_draw(nj_vm *vm, nj_ref self, nj_ref root,
                            nj_ref *meshes, float *mats, nj_u4 n,
                            const nj_ref *slights, const float *slmats,
                            nj_s4 snlights);
static void collect_lights_walk(nj_vm *vm, nj_ref o, nj_ref *out, float *mats,
                                const float *parent, nj_u4 cap, nj_u4 *n,
                                int depth);


static nj_status m3g_render_scene(nj_vm *vm, nj_thread *th NJ_MAYBE_UNUSED,
                                  nj_value *args, nj_value *ret NJ_MAYBE_UNUSED,
                                  int node_render)
{
    nj_ref self = NJ_THIS(args);
    nj_ref root = NJ_ARG_REF(args, 0);
    nj_ref target, meshes[64];
    nj_ref slights[16];
    float slmats[16 * 16];
    const nj_ref *slightp = NULL;
    const float *smatp = NULL;
    nj_s4 snlight = 0;
    nj_u4 n;
    float mats[64 * 16];
    nj_s4 vw, vh;

    vm->m3g_renders++;

    target = nj_rget(vm, self, nj_field_lookup(vm, NJ_C_G3D, "_nj_target",
                                               "Ljava/lang/Object;", 0));
    if (!target || !nj_heap_owns(vm, target)) {
        return NJ_OK;               /* nothing was bound to draw into */
    }
    /*
     * The viewport is checked here rather than used: a render with no target
     * to draw into, or one whose target cannot be reached, draws nothing at
     * all, and that answer has to come before the walk.  The camera, the
     * projection and the matrices live in jr_setup(), which every mesh goes
     * through - including the World's active camera, which is the way the
     * corpus names its camera (World.setActiveCamera(), not setCamera()).
     */
    vw = nj_iget(vm, self, nj_field_lookup(vm, NJ_C_G3D, "_nj_vw", "I", 0));
    vh = nj_iget(vm, self, nj_field_lookup(vm, NJ_C_G3D, "_nj_vh", "I", 0));
    if (vw <= 0 || vh <= 0) {
        int tw = 0, th = 0;

        /* No setViewport: the whole target, which is what bindTarget resets
           the viewport to. */
        if (m3g_target_graphics(vm, target, &tw, &th) == 0 || tw <= 0 || th <= 0) {
            return NJ_OK;           /* no usable target, no pixels */
        }
    }
    /*
     * A World may carry its own Background, and a title that draws a World
     * without calling Graphics3D.clear() expects the sky to be cleared to it.
     * The clear is the same native the title would have called, so there is
     * one implementation of "fill the viewport with the background colour"
     * and the World path is not a second one.
     */
    if (nj_ref_class_name(root) &&
        strcmp(nj_ref_class_name(root), NJ_C_WORLD) == 0) {
        nj_ref bg = nj_rget(vm, root, m3g_fof(vm, NJ_C_WORLD, "_nj_bg",
                                              "L" NJ_C_BG ";"));

        if (bg) {
            nj_value ca[1], cn;

            ca[0].r = bg;
            (void)nj_call(vm, self, NJ_C_G3D, "clear", "(L" NJ_C_BG ";)V", ca, &cn);
        }
    }

    /*
     * Each mesh draws through the same pipeline the immediate
     * render(VertexBuffer, IndexBuffer, Appearance, Transform, int) uses:
     * the model matrix from the walk, the mesh's vertex buffer, its index
     * buffer and its first submesh appearance.  That is what gives a World
     * its materials, textures, lighting, fog, culling and depth - before
     * this, a World was drawn as flat white triangles with no transform.
     */
    /*
     * render(World) is lit by the Light nodes that are part of that World.
     * It is a per-World set: two Worlds in one title do not share lights, a
     * Light the title created and added to this World counts, and a light
     * that was right for another World must not leak in.  Collect on every
     * World render - the walk is cheap next to the raster - and keep the
     * Graphics3D's own addLight list out of it.
     */
    if (nj_ref_class_name(root) &&
        strcmp(nj_ref_class_name(root), NJ_C_WORLD) == 0) {
        float ident[16];
        nj_u4 ln = 0u;

        memset(ident, 0, sizeof(ident));
        ident[0] = ident[5] = ident[10] = ident[15] = 1.0f;
        collect_lights_walk(vm, root, slights, slmats, ident, 16u, &ln, 0);
        slightp = slights;
        smatp = slmats;
        snlight = (nj_s4)ln;
    }
    /*
     * A scene whose lights are nodes in it lights itself: fill the
     * Graphics3D's light list from the graph when the title never called
     * addLight.  Not done for a World: its own set was collected above.
     */
    if (!slightp &&
        nj_iget(vm, self, m3g_fof(vm, NJ_C_G3D, "_nj_nlights", "I")) == 0) {
        nj_ref lrefs[16];
        float lmat[16 * 16];
        float ident[16];
        nj_u4 ln = 0u;

        memset(ident, 0, sizeof(ident));
        ident[0] = ident[5] = ident[10] = ident[15] = 1.0f;
        collect_lights_walk(vm, root, lrefs, lmat, ident, 16u, &ln, 0);
        if (ln > 0u) {
            nj_ref arr = nj_object_array_new(vm, NJ_C_LIGHT, ln);
            nj_ref fm = nj_array_new(vm, "F", ln * 16u);
            nj_u4 li;
            /*
             * The two arrays are temporary: they only have to survive the
             * field writes below.  Rooting them without a matching release
             * pins them - and everything they reference - for the life of the
             * VM, and nj_vm_can_save() refuses to snapshot while any root is
             * held, so a title taking this path would silently lose
             * --save-state.  Same failure mode as the Image2D constructor.
             */
            nj_u4 mark = nj_root_mark(vm);

            for (li = 0u; li < ln; li++) {
                nj_array_set_ref(arr, li, lrefs[li]);
                memcpy((float *)nj_array_data(fm) + (nj_size)li * 16u,
                       lmat + (nj_size)li * 16u, sizeof(float) * 16u);
            }
            nj_root(vm, arr);
            nj_root(vm, fm);
            nj_rput(vm, self, m3g_fof(vm, NJ_C_G3D, "_nj_lights",
                                      "[L" NJ_C_LIGHT ";"), arr);
            nj_rput(vm, self, m3g_fof(vm, NJ_C_G3D, "_nj_lmats", "[F"), fm);
            nj_iput(vm, self, m3g_fof(vm, NJ_C_G3D, "_nj_nlights", "I"),
                    (nj_s4)ln);
            nj_root_release(vm, mark);
        }
    }
    if (node_render) {
        float identity[16];
        float *given = mat_of(vm, NJ_ARG_REF(args, 1), NJ_C_XFORM, "_nj_m");

        mat_identity(identity);
        n = 0u;
        /* render(Node, Transform) replaces the root's local transform;
           descendants retain their transforms relative to that root. */
        collect_walk(vm, root, meshes, mats, given ? given : identity,
                     64u, &n, 0, 0);
    } else {
        n = nj_m3g_collect_meshes_m(vm, root, meshes, mats, 64u);
    }
    (void)m3g_world_draw(vm, self, root, meshes, mats, n,
                         slightp, smatp, snlight);
    return NJ_OK;
}

NJFN(m3g_render)
{
    return m3g_render_scene(vm, th, args, ret, 0);
}

NJFN(m3g_render_node)
{
    return m3g_render_scene(vm, th, args, ret, 1);
}

/*
 * Mesh keeps its submesh appearances in one Vector and its count in an int
 * field.  getSubmeshCount() answers from the field and setAppearance() keeps
 * it in step with the Vector, so the two can never disagree.
 */
static void set_submesh_count(nj_vm *vm, nj_ref mesh, nj_s4 n)
{
    nj_iput(vm, mesh, nj_field_lookup(vm, NJ_C_MESH, "_nj_nsub", "I", 0), n);
}

static nj_s4 get_submesh_count(nj_vm *vm, nj_ref mesh)
{
    return nj_iget(vm, mesh, nj_field_lookup(vm, NJ_C_MESH, "_nj_nsub", "I", 0));
}

/* ------------------------------------------------------------------ */
/* Appearance and the three look-up objects                            */
/* ------------------------------------------------------------------ */
/*
 * These are pure state holders: an Appearance remembers which Material,
 * PolygonMode, CompositingMode and per-layer Texture2D it was given, and the
 * three setters remember the values a title sets on them.  Nothing is
 * rendered from them yet (that is the rasteriser, step two), but nothing is
 * forgotten either - a title that sets a blending mode and reads it back gets
 * the value it set, which is the difference between a stub and a holder.
 */
#define NJ_M3G_REF_SET(fn, cls, cap, fieldname)                        \
    NJFN(fn)                                                           \
    {                                                                  \
        nj_rput(vm, NJ_THIS(args),                                     \
                nj_field_lookup(vm, cls, fieldname, cap, 0),           \
                NJ_ARG_REF(args, 0));                                  \
        return NJ_OK;                                                  \
    }

NJFN(m3g_app_setMaterial)
{
    nj_rput(vm, NJ_THIS(args),
            nj_field_lookup(vm, NJ_C_APPEAR, "_nj_mat", "L" NJ_C_MAT ";", 0),
            NJ_ARG_REF(args, 0));
    return NJ_OK;
}

NJFN(m3g_app_setPolygonMode)
{
    nj_rput(vm, NJ_THIS(args),
            nj_field_lookup(vm, NJ_C_APPEAR, "_nj_pm", "L" NJ_C_POLY ";", 0),
            NJ_ARG_REF(args, 0));
    return NJ_OK;
}

NJFN(m3g_app_setCompositingMode)
{
    nj_rput(vm, NJ_THIS(args),
            nj_field_lookup(vm, NJ_C_APPEAR, "_nj_cm", "L" NJ_C_COMP ";", 0),
            NJ_ARG_REF(args, 0));
    return NJ_OK;
}

/*
 * setTexture(unit, tex): unit 0 is the base texture, 1 the second.  m3g has
 * exactly two units, so anything else is the IllegalArgumentException the
 * specification names.
 */
NJFN(m3g_app_setTexture)
{
    nj_s4 unit = NJ_ARG_INT(args, 0);

    if (unit < 0 || unit > 1) {
        NJ_THROW(vm, "java/lang/IllegalArgumentException",
                 "a texture unit is 0 or 1");
    }
    nj_rput(vm, NJ_THIS(args),
            nj_field_lookup(vm, NJ_C_APPEAR,
                            unit == 0 ? "_nj_tex0" : "_nj_tex1",
                            "L" NJ_C_TEX2D ";", 0),
            NJ_ARG_REF(args, 1));
    return NJ_OK;
}

NJFN(m3g_app_getTexture)
{
    nj_s4 unit = NJ_ARG_INT(args, 0);

    if (unit < 0 || unit > 1) {
        NJ_THROW(vm, "java/lang/IllegalArgumentException",
                 "a texture unit is 0 or 1");
    }
    ret->r = nj_rget(vm, NJ_THIS(args),
                     nj_field_lookup(vm, NJ_C_APPEAR,
                                     unit == 0 ? "_nj_tex0" : "_nj_tex1",
                                     "L" NJ_C_TEX2D ";", 0));
    return NJ_OK;
}

#define NJ_M3G_GET(fn, cls, cap, fieldname)                            \
    NJFN(fn)                                                           \
    {                                                                  \
        ret->r = nj_rget(vm, NJ_THIS(args),                            \
                         nj_field_lookup(vm, cls, fieldname, cap, 0)); \
        return NJ_OK;                                                  \
    }

NJ_M3G_GET(m3g_app_getCompositingMode, NJ_C_APPEAR, "L" NJ_C_COMP ";", "_nj_cm")
NJ_M3G_GET(m3g_app_getPolygonMode,     NJ_C_APPEAR, "L" NJ_C_POLY ";", "_nj_pm")

NJFN(m3g_app_setLayer)
{
    nj_iput(vm, NJ_THIS(args),
            nj_field_lookup(vm, NJ_C_APPEAR, "_nj_layer", "I", 0),
            NJ_ARG_INT(args, 0));
    return NJ_OK;
}

/* ---- PolygonMode / CompositingMode / Texture2D --------------------- */
NJFN(m3g_poly_setWinding)
{
    nj_s4 w = NJ_ARG_INT(args, 0);

    /*
     * 168 and 169, as the JSR 184 class definition says.  This check used to
     * demand 16896 and 16897, which is what I wrote down from memory; the
     * corpus title that calls setWinding passes the correct value and was
     * rejected for it, so the wrong numbers were ours, not the game's.
     */
    if (w != 168 && w != 169) {
        NJ_THROW(vm, "java/lang/IllegalArgumentException",
                 "a winding is WINDING_CCW (168) or WINDING_CW (169)");
    }
    nj_iput(vm, NJ_THIS(args),
            nj_field_lookup(vm, NJ_C_POLY, "_nj_winding", "I", 0), w);
    return NJ_OK;
}

/*
 * Culling.  JSR 184 fixes the three values (CULL_BACK 160, CULL_FRONT 161,
 * CULL_NONE 162 - the same numbers m3g_core.h in the reference implementation
 * defines) and PolygonMode.setCulling(I)V is called by a corpus title that
 * would otherwise not start at all.  The class constants used to say NONE 160
 * and BACK 161, which is the pair swapped: a MIDlet asking for no culling got
 * back-face culling instead, and every file-loaded PolygonMode kept the
 * constructor's default because nothing wrote the file's value.
 */
NJFN(m3g_poly_setCulling)
{
    nj_s4 c = NJ_ARG_INT(args, 0);

    if (c != 160 && c != 161 && c != 162) {
        NJ_THROW(vm, "java/lang/IllegalArgumentException",
                 "culling is CULL_BACK (160), CULL_FRONT (161) or CULL_NONE (162)");
    }
    nj_iput(vm, NJ_THIS(args),
            nj_field_lookup(vm, NJ_C_POLY, "_nj_culling", "I", 0), c);
    return NJ_OK;
}

NJFN(m3g_poly_getCulling)
{
    ret->i = nj_iget(vm, NJ_THIS(args),
                     nj_field_lookup(vm, NJ_C_POLY, "_nj_culling", "I", 0));
    return NJ_OK;
}

/* Both entry points store the light and a flat 16-float world transform. */
NJFN(m3g_g3d_setLight);
NJFN(m3g_g3d_addLight)
{
    nj_ref self = NJ_THIS(args);
    nj_s4 n = nj_iget(vm, self, m3g_fof(vm, NJ_C_G3D, "_nj_nlights", "I"));
    nj_value set_args[4], ignored;
    nj_status st;

    memset(set_args, 0, sizeof(set_args));
    set_args[0].r = self;
    set_args[1].i = n;
    set_args[2].r = NJ_ARG_REF(args, 0);
    set_args[3].r = NJ_ARG_REF(args, 1);
    st = m3g_g3d_setLight(vm, th, set_args + 1, &ignored);
    if (st != NJ_OK) { return st; }
    ret->i = n;
    return NJ_OK;
}

NJFN(m3g_g3d_resetLights)
{
    nj_iput(vm, NJ_THIS(args),
            nj_field_lookup(vm, NJ_C_G3D, "_nj_nlights", "I", 0), 0);
    return NJ_OK;
}

NJFN(m3g_poly_setPersp)
{
    nj_iput(vm, NJ_THIS(args),
            nj_field_lookup(vm, NJ_C_POLY, "_nj_persp", "Z", 0),
            NJ_ARG_INT(args, 0) ? 1 : 0);
    return NJ_OK;
}

#define NJ_M3G_INT_SET(fn, cls, fieldname)                             \
    NJFN(fn)                                                           \
    {                                                                  \
        nj_iput(vm, NJ_THIS(args),                                     \
                nj_field_lookup(vm, cls, fieldname, "I", 0),           \
                NJ_ARG_INT(args, 0));                                  \
        return NJ_OK;                                                  \
    }

NJ_M3G_INT_SET(m3g_comp_setBlending, NJ_C_COMP, "_nj_blend")
NJ_M3G_INT_SET(m3g_comp_setDepthTest, NJ_C_COMP, "_nj_dtest")
NJ_M3G_INT_SET(m3g_comp_setDepthWrite, NJ_C_COMP, "_nj_dwrite")
NJ_M3G_INT_SET(m3g_tex_setBlending, NJ_C_TEX2D, "_nj_blend")

NJFN(m3g_comp_getBlending)
{
    ret->i = nj_iget(vm, NJ_THIS(args),
                     nj_field_lookup(vm, NJ_C_COMP, "_nj_blend", "I", 0));
    return NJ_OK;
}

NJFN(m3g_comp_setAlphaThreshold)
{
    nj_iput(vm, NJ_THIS(args),
            nj_field_lookup(vm, NJ_C_COMP, "_nj_athresh", "I", 0),
            f2bits(NJ_ARG_FLOAT(args, 0)));
    return NJ_OK;
}

/*
 * VertexArray, from 急速公路狂飙－新年版_84: the corpus asked for
 * VertexArray(int,int,int) and nothing else, so that is what is here, plus the
 * three accessors that read back what the constructor was told.  Guessing at
 * the rest of the class would be inventing an API instead of implementing one.
 */
NJFN(m3g_varr_init)
{
    nj_ref self = NJ_THIS(args);

    nj_iput(vm, self, nj_field_lookup(vm, NJ_C_VARR, "_nj_numVerts", "I", 0),
            NJ_ARG_INT(args, 0));
    nj_iput(vm, self, nj_field_lookup(vm, NJ_C_VARR, "_nj_numComp", "I", 0),
            NJ_ARG_INT(args, 1));
    /*
     * The third argument is the component SIZE in bytes, not the type: the
     * reference implementation's signature is VertexArray(int numVertices, int
     * numComponents, int componentSize), and getComponentType() answers the
     * type that size implies - BYTE (0), SHORT (1) or FIXED (2).  Every
     * collected title passes 2, which is what a 16-bit vertex component is.
     */
    {
        nj_s4 size = NJ_ARG_INT(args, 2);
        nj_s4 type = (size <= 1) ? 0 : (size == 2) ? 1 : 2;

        nj_iput(vm, self, nj_field_lookup(vm, NJ_C_VARR, "_nj_compSize", "I", 0),
                size);
        nj_iput(vm, self, nj_field_lookup(vm, NJ_C_VARR, "_nj_ctype", "I", 0),
                type);
    }
    return NJ_OK;
}

#define NJ_M3G_VARR_GET(fn, field)                                            \
    NJFN(fn)                                                                  \
    {                                                                         \
        ret->i = nj_field_read_int(vm, NJ_THIS(args),                         \
                                   nj_field_lookup(vm, NJ_C_VARR, field,      \
                                                   "I", 0));                  \
        return NJ_OK;                                                         \
    }

/* Vertex data belongs to the VertexArray; callers may reuse their buffers. */
static nj_status m3g_varr_set_values(nj_vm *vm, nj_ref self, nj_ref src,
                                    nj_s4 first, nj_s4 count, int bytes)
{
    nj_s4 vertices = nj_iget(vm, self,
        nj_field_lookup(vm, NJ_C_VARR, "_nj_numVerts", "I", 0));
    nj_s4 comp = nj_iget(vm, self,
        nj_field_lookup(vm, NJ_C_VARR, "_nj_numComp", "I", 0));
    nj_field *field = nj_field_lookup(vm, NJ_C_VARR, "_nj_data", "[S", 0);
    nj_ref dst;
    nj_s4 i, total;

    if (!src) {
        NJ_THROW(vm, "java/lang/NullPointerException", "null vertex values");
    }
    if (comp < 1 || comp > 4 || first < 0 || count < 0 ||
        first > vertices || count > vertices - first ||
        (nj_u8)count * (nj_u8)comp > src->length) {
        NJ_THROW(vm, "java/lang/IllegalArgumentException", "invalid vertex range");
    }
    dst = nj_rget(vm, self, field);
    if (!dst) {
        dst = nj_short_array_new(vm, (nj_u4)vertices * (nj_u4)comp);
        if (!dst) { return nj_throw_oom(vm); }
        nj_rput(vm, self, field, dst);
    }
    total = count * comp;
    for (i = 0; i < total; i++) {
        ((nj_s2 *)nj_array_data(dst))[first * comp + i] = bytes
            ? (nj_s2)((const nj_s1 *)nj_array_data(src))[i]
            : ((const nj_s2 *)nj_array_data(src))[i];
    }
    nj_iput(vm, self, nj_field_lookup(vm, NJ_C_VARR, "_nj_first", "I", 0), 0);
    nj_iput(vm, self, nj_field_lookup(vm, NJ_C_VARR, "_nj_count", "I", 0), vertices);
    return NJ_OK;
}
NJFN(m3g_varr_set_short)
{
    return m3g_varr_set_values(vm, NJ_THIS(args), NJ_ARG_REF(args, 2),
                              NJ_ARG_INT(args, 0), NJ_ARG_INT(args, 1), 0);
}

NJ_M3G_VARR_GET(m3g_varr_getVertexCount,    "_nj_numVerts")
NJ_M3G_VARR_GET(m3g_varr_getComponentCount, "_nj_numComp")
NJ_M3G_VARR_GET(m3g_varr_getComponentSize,  "_nj_compSize")

/*
 * Shading.  JSR 184 fixes the two values - SHADE_FLAT 164, SHADE_SMOOTH 165 -
 * and 剑灵－古剑魂梦 calls setShading() during start-up, so without this the
 * title never gets to its first frame.  getShading() comes along because a
 * value that can be set but not read is a value the caller cannot check.
 */
NJFN(m3g_poly_setShading)
{
    nj_s4 sh = NJ_ARG_INT(args, 0);

    if (sh != 164 && sh != 165) {
        NJ_THROW(vm, "java/lang/IllegalArgumentException",
                 "shading is SHADE_FLAT (164) or SHADE_SMOOTH (165)");
    }
    nj_iput(vm, NJ_THIS(args),
            nj_field_lookup(vm, NJ_C_POLY, "_nj_shading", "I", 0), sh);
    return NJ_OK;
}

NJFN(m3g_poly_getShading)
{
    ret->i = nj_field_read_int(vm, NJ_THIS(args),
                               nj_field_lookup(vm, NJ_C_POLY, "_nj_shading",
                                               "I", 0));
    return NJ_OK;
}

/* Two-sided lighting, from 急速公路狂飙－新年版_84.  A boolean with no
 * validation to get wrong: store it and read it back. */
NJFN(m3g_poly_setTwoSided)
{
    nj_iput(vm, NJ_THIS(args),
            nj_field_lookup(vm, NJ_C_POLY, "_nj_twosided", "Z", 0),
            NJ_ARG_INT(args, 0) ? 1 : 0);
    return NJ_OK;
}

NJFN(m3g_poly_getTwoSided)
{
    ret->i = nj_field_read_int(vm, NJ_THIS(args),
                               nj_field_lookup(vm, NJ_C_POLY, "_nj_twosided",
                                               "Z", 0)) ? 1 : 0;
    return NJ_OK;
}

NJFN(m3g_tex_setImage)
{
    nj_rput(vm, NJ_THIS(args),
            nj_field_lookup(vm, NJ_C_TEX2D, "_nj_img",
                            "L" NJ_C_IMAGE2D ";", 0),
            NJ_ARG_REF(args, 0));
    return NJ_OK;
}

/* Texture2D(Image2D) is the whole of the constructor: it stores the image,
 * which is exactly what setImage() does, so share the one implementation
 * rather than writing the same field store twice. */
NJFN(m3g_tex2d_init)
{
    return m3g_tex_setImage(vm, th, args, ret);
}

/*
 * Filtering, from 急速公路狂飙－新年版_84.  Both arguments are stored as given:
 * this module does not rasterise, so nothing consumes them yet, and rejecting a
 * value here would turn "we do not use it" into "your title does not start".
 * The constants belong to a step that renders, and guessing them now would be
 * a guess with a failure mode of its own.
 */
NJFN(m3g_tex_setFiltering)
{
    nj_ref self = NJ_THIS(args);

    nj_iput(vm, self, nj_field_lookup(vm, NJ_C_TEX2D, "_nj_filtLevel", "I", 0),
            NJ_ARG_INT(args, 0));
    nj_iput(vm, self, nj_field_lookup(vm, NJ_C_TEX2D, "_nj_filtImage", "I", 0),
            NJ_ARG_INT(args, 1));
    return NJ_OK;
}

NJFN(m3g_tex_getImage)
{
    ret->r = nj_rget(vm, NJ_THIS(args),
                     nj_field_lookup(vm, NJ_C_TEX2D, "_nj_img",
                                     "L" NJ_C_IMAGE2D ";", 0));
    return NJ_OK;
}

NJFN(m3g_tex_getFiltering)
{
    ret->i = nj_field_read_int(vm, NJ_THIS(args),
                               nj_field_lookup(vm, NJ_C_TEX2D, "_nj_filtLevel",
                                               "I", 0));
    return NJ_OK;
}

NJFN(m3g_tex_setWrapping)
{
    nj_ref self = NJ_THIS(args);

    nj_iput(vm, self, nj_field_lookup(vm, NJ_C_TEX2D, "_nj_wrapS", "I", 0),
            NJ_ARG_INT(args, 0));
    nj_iput(vm, self, nj_field_lookup(vm, NJ_C_TEX2D, "_nj_wrapT", "I", 0),
            NJ_ARG_INT(args, 1));
    return NJ_OK;
}

/* ---- Sprite3D ------------------------------------------------------ */
/*
 * A Sprite3D is an image with an appearance, anchored at its node's origin.
 * A new image resets the crop to its full extent.  The raster path uses the
 * same fields for API-created Sprite3D nodes and the loader's Sprite alias.
 */
NJFN(m3g_sprite_init)
{
    nj_ref self = NJ_THIS(args);
    nj_ref image = NJ_ARG_REF(args, 1);

    if (!image) {
        NJ_THROW(vm, "java/lang/NullPointerException", "Sprite3D image");
    }
    nj_iput(vm, self, m3g_fof(vm, NJ_C_SPRITE3D, "_nj_cropX", "I"), 0);
    nj_iput(vm, self, m3g_fof(vm, NJ_C_SPRITE3D, "_nj_cropY", "I"), 0);
    nj_iput(vm, self, m3g_fof(vm, NJ_C_SPRITE3D, "_nj_cropW", "I"),
            nj_iget(vm, image, m3g_fof(vm, NJ_C_IMAGE2D, "_nj_w", "I")));
    nj_iput(vm, self, m3g_fof(vm, NJ_C_SPRITE3D, "_nj_cropH", "I"),
            nj_iget(vm, image, m3g_fof(vm, NJ_C_IMAGE2D, "_nj_h", "I")));
    nj_iput(vm, self, m3g_fof(vm, NJ_C_SPRITE3D, "_nj_scaled", "Z"),
            NJ_ARG_INT(args, 0) ? 1 : 0);
    nj_rput(vm, self, m3g_fof(vm, NJ_C_SPRITE3D, "_nj_img",
                              "L" NJ_C_IMAGE2D ";"), NJ_ARG_REF(args, 1));
    nj_rput(vm, self, m3g_fof(vm, NJ_C_SPRITE3D, "_nj_app",
                              "L" NJ_C_APPEAR ";"), NJ_ARG_REF(args, 2));
    return NJ_OK;
}

NJFN(m3g_sprite_setImage)
{
    nj_ref self = NJ_THIS(args), image = NJ_ARG_REF(args, 0);

    if (!image) {
        NJ_THROW(vm, "java/lang/NullPointerException", "Sprite3D image");
    }
    nj_rput(vm, self, m3g_fof(vm, NJ_C_SPRITE3D, "_nj_img",
                             "L" NJ_C_IMAGE2D ";"), image);
    nj_iput(vm, self, m3g_fof(vm, NJ_C_SPRITE3D, "_nj_cropX", "I"), 0);
    nj_iput(vm, self, m3g_fof(vm, NJ_C_SPRITE3D, "_nj_cropY", "I"), 0);
    nj_iput(vm, self, m3g_fof(vm, NJ_C_SPRITE3D, "_nj_cropW", "I"),
            nj_iget(vm, image, m3g_fof(vm, NJ_C_IMAGE2D, "_nj_w", "I")));
    nj_iput(vm, self, m3g_fof(vm, NJ_C_SPRITE3D, "_nj_cropH", "I"),
            nj_iget(vm, image, m3g_fof(vm, NJ_C_IMAGE2D, "_nj_h", "I")));
    return NJ_OK;
}

NJFN(m3g_sprite_setAppearance)
{
    nj_rput(vm, NJ_THIS(args),
            nj_field_lookup(vm, NJ_C_SPRITE3D, "_nj_app",
                            "L" NJ_C_APPEAR ";", 0),
            NJ_ARG_REF(args, 0));
    return NJ_OK;
}

/* ---- Mesh ---------------------------------------------------------- */
NJFN(m3g_mesh_setAppearance)
{
    nj_ref self = NJ_THIS(args);
    nj_s4 index = NJ_ARG_INT(args, 0);
    nj_ref vec = nj_rget(vm, self, nj_field_lookup(vm, NJ_C_MESH, "_nj_subs",
                                                   "L" NJ_C_VECTOR ";", 0));
    nj_value a[5];
    nj_ref ap = NJ_ARG_REF(args, 1);

    if (index < 0) {
        NJ_THROW(vm, "java/lang/IndexOutOfBoundsException", "negative submesh");
    }
    if (!vec) {
        nj_s4 i;
        vec = nj_object_new(vm, NJ_C_VECTOR);
        if (!vec) {
            return nj_throw_oom(vm);
        }
        nj_call(vm, vec, NJ_C_VECTOR, "<init>", "()V", NULL, NULL);
        nj_rput(vm, self, nj_field_lookup(vm, NJ_C_MESH, "_nj_subs",
                                          "L" NJ_C_VECTOR ";", 0), vec);
        set_submesh_count(vm, self, 0);
        for (i = 0; i <= index; i++) {
            a[0].r = 0;
            if (nj_call(vm, vec, NJ_C_VECTOR, "addElement",
                        "(Ljava/lang/Object;)V", a, NULL) != NJ_OK) {
                return NJ_ERR;
            }
        }
    }
    {
        nj_value n;
        if (nj_call(vm, vec, NJ_C_VECTOR, "size", "()I", NULL, &n) != NJ_OK) {
            return NJ_ERR;
        }
        while (n.i <= index) {
            a[0].r = 0;
            if (nj_call(vm, vec, NJ_C_VECTOR, "addElement",
                        "(Ljava/lang/Object;)V", a, NULL) != NJ_OK) {
                return NJ_ERR;
            }
            n.i++;
        }
    }
    /*
     * The object is argument 0 and the index argument 1, and the two must be
     * written into the slot their type belongs to: a slot is one union, so
     * writing the index into a[0].i makes the callee read a null object (or a
     * pointer made of an integer) and the appearance into a[1].r makes it read
     * an index out of the pointer's low half - which is how setAppearance()
     * came to throw ArrayIndexOutOfBoundsException on a one-element vector.
     */
    a[0].r = ap;
    a[1].i = index;
    if (nj_call(vm, vec, NJ_C_VECTOR, "setElementAt",
                "(Ljava/lang/Object;I)V", a, NULL) != NJ_OK) {
        return NJ_ERR;
    }
    {
        nj_value n;
        if (nj_call(vm, vec, NJ_C_VECTOR, "size", "()I", NULL, &n) == NJ_OK) {
            set_submesh_count(vm, self, n.i);
        }
    }
    return NJ_OK;
}

NJFN(m3g_mesh_getAppearance)
{
    nj_ref vec = nj_rget(vm, NJ_THIS(args), nj_field_lookup(vm, NJ_C_MESH,
                                                            "_nj_subs",
                                                            "L" NJ_C_VECTOR ";", 0));
    nj_value a[5], n;

    if (!vec) {
        NJ_THROW(vm, "java/lang/IndexOutOfBoundsException", "no submeshes");
    }
    a[0].i = NJ_ARG_INT(args, 0);
    if (nj_call(vm, vec, NJ_C_VECTOR, "elementAt", "(I)Ljava/lang/Object;",
                a, &n) != NJ_OK) {
        return NJ_ERR;
    }
    ret->r = n.r;
    return NJ_OK;
}

NJFN(m3g_mesh_getSubmeshCount)
{
    ret->i = get_submesh_count(vm, NJ_THIS(args));
    return NJ_OK;
}

NJFN(m3g_mesh_getVertexBuffer)
{
    ret->r = nj_rget(vm, NJ_THIS(args), nj_field_lookup(vm, NJ_C_MESH, "_nj_vb",
                                                        "L" NJ_C_VBUF ";", 0));
    return NJ_OK;
}

NJFN(m3g_mesh_getIndexBuffer)
{
    nj_ref self = NJ_THIS(args);
    nj_s4 index = NJ_ARG_INT(args, 0);
    nj_ref ibs = nj_rget(vm, self, nj_field_lookup(vm, NJ_C_MESH, "_nj_ibs",
                                                "[L" NJ_C_IBUF ";", 0));
    if (index < 0 || index >= get_submesh_count(vm, self)) {
        NJ_THROW(vm, "java/lang/IndexOutOfBoundsException", "invalid submesh");
    }
    ret->r = ibs ? nj_array_get_ref(ibs, (nj_u4)index) :
        nj_rget(vm, self, nj_field_lookup(vm, NJ_C_MESH, "_nj_ib",
                                       "L" NJ_C_IBUF ";", 0));
    return NJ_OK;
}

NJFN(m3g_mesh_setVertexBuffer)
{
    nj_rput(vm, NJ_THIS(args), nj_field_lookup(vm, NJ_C_MESH, "_nj_vb",
                                               "L" NJ_C_VBUF ";", 0),
            NJ_ARG_REF(args, 0));
    return NJ_OK;
}

/* ---- Loader -------------------------------------------------------- */
/*
 * Loader.load(String name).
 *
 * There is no .m3g object parser yet, so this cannot hand back a scene graph.
 * What it CAN do - and did not do before - is find the resource and say what
 * it found.  "no such resource" and "found 41280 bytes and cannot parse them"
 * are different answers, and only the first one is about the MIDlet's own
 * packaging; reporting both as the same IOException sends anyone reading the
 * log looking for a missing file that is right there.
 *
 * Returning an empty array instead was rejected: the JSR's own answer for
 * "could not load" is IOException, every title already has to handle it, and
 * an empty array puts a null World in the scene graph that fails somewhere far
 * from the cause.
 */
/*
 * The M3G file identifier, all twelve bytes of it.
 *
 * From the JSR 184 reference loader, which spells it out as the byte array
 * {-85, 74, 83, 82, 49, 56, 52, -69, 13, 10, 26, 10} - that is
 * AB 4A 'J' 'S' 'R' '1' '8' '4' BB 0D 0A 1A 0A, and it is exactly what
 * 毁灭战士3_10598 ships in mars/mar.m3g:
 *
 *     $ unzip -p 毁灭战士3_10598.jar mars/mar.m3g | head -c 16 | xxd
 *     00000000: ab4a 5352 3138 34bb 0d0a 1a0a 005f 0000  .JSR184......_..
 *
 * An earlier revision checked only the first two bytes; that was a guess made
 * before this evidence existed.  Twelve is the number, and the reference
 * implementation parses the object section in native code, so the layout
 * beyond this identifier is not recoverable from its Java source either.
 */
static const unsigned char NJ_M3G_FILE_ID[12] = {
    0xABu, 0x4Au, 0x53u, 0x52u, 0x31u, 0x38u, 0x34u, 0xBBu,
    0x0Du, 0x0Au, 0x1Au, 0x0Au
};

static int m3g_has_file_id(const unsigned char *b, nj_u4 len)
{
    int i;

    if (!b || len < (nj_u4)sizeof(NJ_M3G_FILE_ID)) {
        return 0;
    }
    for (i = 0; i < (int)sizeof(NJ_M3G_FILE_ID); i++) {
        if (b[i] != NJ_M3G_FILE_ID[i]) {
            return 0;
        }
    }
    return 1;
}

/* the class an object type maps to; defined with the other tables below */
static const char *m3g_class_of(nj_u1 type);

/*
 * Give every Group the children the file names, and set their parent.
 *
 * This is a function of its own rather than a loop inside the loader so that a
 * test can reach it: the loader needs a real .m3g inside a real MIDlet suite,
 * which a unit test cannot build, while this needs only an object table and
 * the array the loader is filling.
 *
 * The parent IS set, and that is what lets Object3D.find() walk to these
 * children - without the link a game looking a node up by userID gets null and
 * then calls addChild(null).  It is only safe because m3g_addChild()
 * recognises a node that is already in the vector, so the game's own
 * addChild() for the same node adopts it and returns.
 */
/*
 * Resolving a child reference.
 *
 * A reference in a Group or a World can be read two ways, and both are in the
 * corpus: as an index into the object table, or as the userID the file gave
 * the object.  The index reading is the one the format defines and the one
 * that fits 92% of the 4326 references in the collected titles, so it is tried
 * first - but a list that resolves to the group ITSELF is not being read the
 * way its file meant it (a node cannot be its own child), and for those the
 * file's own userIDs are the reading that makes sense.  毁灭战士3_10598 is one:
 * its World names 13, 14 and 15, which are the userIDs of its camera, its
 * light and its mesh, and reading them as indices would name the light, the
 * mesh and the World itself.
 */
/*
 * How one file numbers its references.
 *
 * All three are in the corpus.  Most files index the object table the way the
 * format defines it, where the first object is 1 (0 is null).  Some index it
 * from 0 instead, and some name objects by the userID the file gave them -
 * 毁灭战士3_10598 does: its World says 13, 14, 15, which are the userIDs of its
 * camera, light and mesh, while reading them as indices names the light, the
 * mesh and the World itself.  A file that says a group is its own child is not
 * being read the way it meant, so a reading is only used when every reference
 * in the list lands on a node and never on the group itself.
 */
#define M3G_REF_ONE   0
#define M3G_REF_ZERO  1
#define M3G_REF_UID   2
/*
 * The reading every file in the corpus actually uses.
 *
 * A Group or a Mesh names another object by a number that is TWO ahead of the
 * object's place in the file: the VertexBuffer, TriangleStripArray and
 * Appearance a Mesh names are at value - 2, and a World's children are its
 * group, its light and its mesh the same way.  Checked over the 2,606 Meshes
 * in the 975 .m3g files the corpus ships: reading them as value - 2 puts all
 * 2,606 vertex buffers on a VertexBuffer, all 2,606 index buffers on a
 * TriangleStripArray and all 2,606 appearances on an Appearance, while the
 * format's own one-based reading fits 629 and a zero-based one 528.  The same
 * offset is what makes a World's child list name nodes.
 */
#define M3G_REF_TWO   3

static int m3g_is_a(nj_vm *vm, nj_ref o, const char *cls)
{
    nj_class *want = nj_class_lookup(vm, cls);
    return (o != 0 && want != NULL && nj_class_is_subclass_of(o->cls, want));
}

static int m3g_is_node(nj_vm *vm, nj_ref o)
{
    return m3g_is_a(vm, o, NJ_C_NODE);
}

static nj_ref m3g_by_user_id(nj_vm *vm, nj_ref arr, nj_u4 nobj, nj_u4 id)
{
    nj_field *uf = nj_field_lookup(vm, NJ_C_O3D, "_nj_userID", "I", 0);
    nj_u4 k;

    if (!uf) {
        return 0;
    }
    for (k = 1u; k <= nobj; k++) {
        nj_ref o = nj_array_get_ref(arr, k);
        if (o && nj_iget(vm, o, uf) == (nj_s4)id) {
            return o;
        }
    }
    return 0;
}

static nj_ref m3g_resolve_ref(nj_vm *vm, nj_ref arr, nj_u4 nobj, nj_ref self,
                              nj_u4 r, int mode)
{
    nj_ref ch;

    if (r == 0u) {
        return 0;
    }
    if (mode == M3G_REF_UID) {
        ch = m3g_by_user_id(vm, arr, nobj, r);
    } else if (mode == M3G_REF_ZERO) {
        ch = (r + 1u <= nobj) ? nj_array_get_ref(arr, r + 1u) : 0;
    } else if (mode == M3G_REF_TWO) {
        /* object r - 2 of the file, which is table slot r - 1 (slot 0 is
           unused, slot i + 1 holds object i) */
        ch = (r >= 1u && r - 1u <= nobj) ? nj_array_get_ref(arr, r - 1u) : 0;
    } else {
        ch = (r <= nobj) ? nj_array_get_ref(arr, r) : 0;
    }
    if (!ch || ch == self) {
        return 0;
    }
    return ch;
}

static int m3g_list_resolves(nj_vm *vm, nj_ref arr, nj_u4 nobj, nj_ref self,
                             const nj_m3g_object *ob, int mode)
{
    nj_u4 k;

    for (k = 0u; k < ob->fields.nrefs; k++) {
        nj_u4 r = ob->fields.refs[k];
        nj_ref ch;

        if (r == 0u) {
            continue;
        }
        ch = m3g_resolve_ref(vm, arr, nobj, self, r, mode);
        if (!m3g_is_node(vm, ch)) {
            return 0;
        }
    }
    return 1;
}

/*
 * Which way this file numbers its object references.
 *
 * One file numbers its references one way, so the reading is chosen for the
 * file and not for each list: a list of one child that happens to land on a
 * node under the wrong reading is still the wrong child, and a scene graph
 * built from the wrong children draws the wrong thing instead of nothing.  A
 * reading is used only when every list in the file resolves under it.
 *
 * The loader asks this twice: once to link the children, and once to work out
 * which objects the file's references name at all (the roots).  It was a block
 * inside nj_m3g_link_groups until the roots needed the same answer, and two
 * copies of a rule about an undocumented byte layout is one copy too many.
 */
static int m3g_ref_mode_of(nj_vm *vm, const nj_m3g_object *objs, nj_u4 nobj,
                           nj_ref arr)
{
    int m;

    for (m = M3G_REF_TWO; m >= M3G_REF_ONE; m--) {
        nj_u4 j;
        int all = 1;
        for (j = 0u; j < nobj && all; j++) {
            nj_ref gj;
            if (objs[j].type != NJ_M3G_T_GROUP &&
                objs[j].type != NJ_M3G_T_WORLD) {
                continue;
            }
            if (objs[j].fields.nrefs_total == 0u) {
                continue;
            }
            gj = nj_array_get_ref(arr, j + 1u);
            if (gj && !m3g_list_resolves(vm, arr, nobj, gj, &objs[j], m)) {
                all = 0;
            }
        }
        if (all) {
            return m;
        }
    }
    return -1;
}

/*
 * Remember that some object's reference names the object a reference value
 * stands for, under the reading this file uses.
 *
 * The mark is by the file's own object number, which is what the table handed
 * to the loader is indexed by (slot i + 1 holds object i).  A value that names
 * nothing - the format's 0, or a number past the last object - marks nothing,
 * which is the honest answer for a reference the file itself cannot resolve.
 *
 * Two values are deliberately not marks:
 *
 *  - A reference from an object to itself.  Reachability is what decides
 *    whether an object is a root, and a self-loop would make any object with
 *    one unrooted.  Two corpus files do exactly this: their World's trailing
 *    pair reads back as camera 0 and background = the World's own number,
 *    which is not a background any renderer could draw.
 *  - A reference to a World.  A World is the top of a scene graph: nothing in
 *    the format can hold it, Loader.load() is how a title gets hold of it, and
 *    a title that finds it missing can only fail.  Reading a file's World out
 *    of its own speculative trailing fields is a risk with no upside.
 */
static void m3g_mark_ref(nj_u1 *is_ref, const nj_m3g_object *objs, nj_u4 nobj,
                         int mode, nj_u4 r, nj_u4 self)
{
    nj_u4 index;

    if (r == 0u) {
        return;
    }
    if (mode == M3G_REF_UID) {
        nj_u4 j;
        for (j = 0u; j < nobj; j++) {
            if (objs[j].fields.user_id == r && j + 1u != self &&
                objs[j].type != NJ_M3G_T_WORLD) {
                is_ref[j + 1u] = 1u;
            }
        }
        return;
    }
    if (mode == M3G_REF_TWO) {
        if (r < 1u) {
            return;
        }
        index = r - 1u;
    } else {
        index = (mode == M3G_REF_ZERO) ? r + 1u : r;
    }
    if (index >= 1u && index <= nobj && index != self &&
        objs[index - 1u].type != NJ_M3G_T_WORLD) {
        is_ref[index] = 1u;
    }
}

nj_status nj_m3g_link_groups(nj_vm *vm, const nj_m3g_object *objs, nj_u4 nobj,
                             nj_ref arr)
{
    nj_u4 i;
    int file_mode;

    if (!vm || !objs || !arr) {
        return NJ_ERR_STATE;
    }
    file_mode = m3g_ref_mode_of(vm, objs, nobj, arr);
    for (i = 0u; i < nobj; i++) {
        nj_ref g, kids;
        nj_u4 k, total;
        int mode;

        if (objs[i].type != NJ_M3G_T_GROUP &&
            objs[i].type != NJ_M3G_T_WORLD) {
            continue;
        }
        if (objs[i].type == NJ_M3G_T_GROUP && objs[i].fields.nrefs == 0u) {
            continue;
        }
        g = nj_array_get_ref(arr, i + 1u);
        if (!g) { continue; }
        kids = nj_rget(vm, g, nj_field_lookup(vm, NJ_C_GROUP, "_nj_kids",
                                              "L" NJ_C_VECTOR ";", 0));
        if (!kids) {
            nj_value vn;
            kids = nj_object_new(vm, NJ_C_VECTOR);
            if (!kids) { continue; }
            kids = nj_root(vm, kids);
            nj_call(vm, kids, NJ_C_VECTOR, "<init>", "()V", NULL, &vn);
            nj_rput(vm, g, nj_field_lookup(vm, NJ_C_GROUP, "_nj_kids",
                                           "L" NJ_C_VECTOR ";", 0), kids);
        }
        /*
         * The file's own reading, or - for a file whose lists disagree with
         * each other - the reading that describes THIS list, preferring the
         * format's own index reading.
         */
        mode = (file_mode >= 0) ? file_mode : M3G_REF_ONE;
        if (!m3g_list_resolves(vm, arr, nobj, g, &objs[i], mode)) {
            if (m3g_list_resolves(vm, arr, nobj, g, &objs[i], M3G_REF_ZERO)) {
                mode = M3G_REF_ZERO;
            } else if (m3g_list_resolves(vm, arr, nobj, g, &objs[i],
                                         M3G_REF_UID)) {
                mode = M3G_REF_UID;
            }
        }
        /*
         * nrefs_total is how many the file named and nrefs is how many fitted;
         * a caller that predates the overflow array sets only nrefs, and a list
         * that is silently not walked is a group with no children rather than
         * an error, which is the worse of the two failures.
         */
        total = (objs[i].fields.nrefs_total != 0u) ? objs[i].fields.nrefs_total
                                                   : objs[i].fields.nrefs;
        for (k = 0u; k < total; k++) {
            nj_u4 idx = nj_m3g_ref_at(&objs[i].fields, k);
            nj_value va[5], vr;
            nj_ref ch = m3g_resolve_ref(vm, arr, nobj, g, idx, mode);

            if (idx == 0u || !ch || !m3g_is_node(vm, ch)) { continue; }
            nj_rput(vm, ch, nj_field_lookup(vm, NJ_C_NODE, "_nj_parent",
                                            "L" NJ_C_NODE ";", 0), g);
            va[0].r = ch;
            (void)nj_call(vm, kids, NJ_C_VECTOR, "addElement",
                          "(Ljava/lang/Object;)V", va, &vr);
        }
        if (objs[i].type == NJ_M3G_T_WORLD) {
            /*
             * The World's own two references - its active camera and its
             * background - are read with the same reading the children were,
             * and only stored when they really are a Camera and a Background:
             * a title that asks for a camera and is handed a mesh draws
             * nothing and cannot tell why.
             */
            nj_ref cam = m3g_resolve_ref(vm, arr, nobj, g, objs[i].fields.b,
                                         mode);
            nj_ref bg = m3g_resolve_ref(vm, arr, nobj, g, objs[i].fields.c,
                                        mode);

            if (m3g_is_a(vm, cam, NJ_C_CAMERA)) {
                nj_rput(vm, g, nj_field_lookup(vm, NJ_C_WORLD, "_nj_cam",
                                               "L" NJ_C_CAMERA ";", 0), cam);
            }
            if (m3g_is_a(vm, bg, NJ_C_BG)) {
                nj_rput(vm, g, nj_field_lookup(vm, NJ_C_WORLD, "_nj_bg",
                                               "L" NJ_C_BG ";", 0), bg);
            }
        }
    }
    return NJ_OK;
}

/*
 * Give a Mesh the vertex buffer, index buffers and appearances the file names
 * for it.
 *
 * A Mesh in the file is a Node followed by the same three things the Java
 * constructor takes - a VertexBuffer, one IndexBuffer per submesh and one
 * Appearance per submesh - so this is the file's route to what mesh_set_base()
 * builds from code.  Without it a title that loads a model gets a Mesh with a
 * vertex buffer and no submeshes, and the first getAppearance(0) throws
 * IndexOutOfBoundsException: "no submeshes".  3D拉力赛-进化汉化版_6366 stops
 * exactly there, on its way into a race.
 *
 * The references are file indices like every other reference here, so they are
 * resolved with the reading this file uses; a reference that names nothing
 * leaves the submesh out rather than inventing one.
 */
/*
 * This is the mesh half of the linking above, exported for the same reason:
 * a unit test can build an object table and check what a Mesh ends up holding,
 * while the loader it normally runs under needs a real .m3g inside a MIDlet.
 */
nj_status nj_m3g_link_meshes(nj_vm *vm, const nj_m3g_object *objs,
                             nj_u4 nobj, nj_ref arr)
{
    int mode = m3g_ref_mode_of(vm, objs, nobj, arr);
    nj_u4 i;

    if (mode < 0) {
        mode = M3G_REF_TWO;
    }
    for (i = 0u; i < nobj; i++) {
        nj_ref mesh, subs, ibs;
        nj_u4 k, nsub;

        if (objs[i].type != NJ_M3G_T_MESH || objs[i].fields.decoded == 0u) {
            continue;
        }
        nsub = objs[i].fields.b;
        mesh = nj_array_get_ref(arr, i + 1u);
        if (!mesh) {
            continue;
        }
        nj_rput(vm, mesh, m3g_fof(vm, NJ_C_MESH, "_nj_vb",
                                  "L" NJ_C_VBUF ";"),
                m3g_resolve_ref(vm, arr, nobj, mesh, objs[i].fields.a, mode));
        ibs = nj_object_array_new(vm, NJ_C_IBUF, nsub);
        if (!ibs) {
            return nj_throw_oom(vm);
        }
        nj_rput(vm, mesh, m3g_fof(vm, NJ_C_MESH, "_nj_ibs",
                                 "[L" NJ_C_IBUF ";"), ibs);
        subs = nj_object_new(vm, NJ_C_VECTOR);
        if (!subs) {
            continue;
        }
        nj_root(vm, subs);
        nj_call(vm, subs, NJ_C_VECTOR, "<init>", "()V", NULL, NULL);
        for (k = 0u; k < nsub; k++) {
            nj_ref ib = m3g_resolve_ref(vm, arr, nobj, mesh,
                                        objs[i].fields.refs[2u * k], mode);
            nj_ref app = m3g_resolve_ref(vm, arr, nobj, mesh,
                                         objs[i].fields.refs[2u * k + 1u], mode);
            nj_value av[1];

            if (k == 0u) {
                nj_rput(vm, mesh, m3g_fof(vm, NJ_C_MESH, "_nj_ib",
                                          "L" NJ_C_IBUF ";"), ib);
            }
            nj_array_set_ref(ibs, k, ib);
            av[0].r = app;
            nj_call(vm, subs, NJ_C_VECTOR, "addElement",
                    "(Ljava/lang/Object;)V", av, NULL);
        }
        nj_rput(vm, mesh, m3g_fof(vm, NJ_C_MESH, "_nj_subs",
                                  "L" NJ_C_VECTOR ";"), subs);
        nj_iput(vm, mesh, m3g_fof(vm, NJ_C_MESH, "_nj_nsub", "I"),
                (nj_s4)nsub);
    }
    return NJ_OK;
}


/*
 * The geometry half of the linking: what a Mesh's vertex buffer points at, and
 * what a strip array holds.
 *
 * A VertexBuffer names its VertexArrays (positions, normals, colors, texture
 * coordinates) and carries the scale and bias the components were quantised
 * with; a TriangleStripArray holds indices and strip lengths.  Without this a
 * Mesh has a vertex buffer with nothing in it, and a rasteriser asked to draw
 * it draws nothing at all - which is exactly what the 3D area of a title did
 * before this existed.
 *
 * The indices are copied out of the section buffer here, for the same reason
 * the VertexArray components are: by the time anything draws, those bytes are
 * gone.  The floats travel as bit patterns in the reference slots from
 * NJ_M3G_MAX_REFS - 4 up, which the roots pass never looks at.
 */
/*
 * Move the scalars and the references a decoded object carries into the Java
 * object that stands for it.
 *
 * The loader decodes a whole file before any Java object exists, so the two
 * halves meet here.  Without this step an Appearance is an empty box: the
 * rasteriser finds no material (and paints everything white), no polygon mode
 * (and culls by its own default) and no texture.  References go through
 * m3g_resolve_ref() so they follow the file's own numbering.
 */
nj_status nj_m3g_link_misc(nj_vm *vm, const nj_m3g_object *objs, nj_u4 nobj,
                           nj_ref arr)
{
    int mode = m3g_ref_mode_of(vm, objs, nobj, arr);
    nj_u4 i;

    if (mode < 0) {
        mode = M3G_REF_TWO;
    }
    for (i = 0u; i < nobj; i++) {
        const nj_m3g_fields *f = &objs[i].fields;
        nj_ref o = nj_array_get_ref(arr, i + 1u);

        if (f->decoded == 0u || !o) {
            continue;
        }
        switch (objs[i].type) {
        case NJ_M3G_T_APPEARANCE: {
            static const char *names[6] = { "_nj_cm", "_nj_fog", "_nj_pm",
                                            "_nj_mat", "_nj_tex0", "_nj_tex1" };
            static const char *descs[6] = {
                "L" NJ_C_COMP ";", "L" NJ_C_FOG ";", "L" NJ_C_POLY ";",
                "L" NJ_C_MAT ";", "L" NJ_C_TEX2D ";", "L" NJ_C_TEX2D ";"
            };
            nj_u4 k;

            nj_iput(vm, o, m3g_fof(vm, NJ_C_APPEAR, "_nj_layer", "I"),
                    (nj_s4)f->a);
            for (k = 0u; k < f->nrefs && k < 6u; k++) {
                nj_ref r = m3g_resolve_ref(vm, arr, nobj, o, f->refs[k],
                                           mode);

                if (r) {
                    nj_rput(vm, o, m3g_fof(vm, NJ_C_APPEAR, names[k],
                                           descs[k]), r);
                }
            }
            break;
        }
        case NJ_M3G_T_IMAGE2D: {
            /*
             * The pixels are handed to the Java object as RGBA, whatever the
             * file's own format was, because that is the shape the texture
             * sampler reads.  A paletted image is expanded through its
             * palette on the way.
             */
            nj_u4 w = f->b, h = f->c, pal = f->d;
            nj_u4 bpp = m3g_image_bpp(f->a);
            nj_u4 n = w * h;
            nj_u1 *dst;
            nj_ref px;
            nj_u4 k;

            if (n == 0u || n > 4096u * 4096u || f->data == NULL ||
                f->data_len < pal) {
                break;
            }
            px = nj_array_new(vm, "B", n * 4u);
            if (!px) {
                break;
            }
            nj_root(vm, px);
            dst = (nj_u1 *)nj_array_data(px);
            if (pal > 0u) {
                nj_u4 entries = pal / bpp;
                const nj_u1 *pt = f->data;
                const nj_u1 *ix = f->data + pal + 4u;

                for (k = 0u; k < n; k++) {
                    nj_u4 e = ix[k];

                    if (e < entries) {
                        m3g_image_texel(f->a, pt + (nj_size)e * bpp, dst + k * 4u);
                    } else {
                        dst[k * 4u] = 0u;
                        dst[k * 4u + 1u] = 0u;
                        dst[k * 4u + 2u] = 0u;
                        dst[k * 4u + 3u] = 0xFFu;
                    }
                }
            } else {
                for (k = 0u; k < n; k++) {
                    m3g_image_texel(f->a, f->data + (nj_size)k * bpp, dst + k * 4u);
                }
            }
            nj_iput(vm, o, m3g_fof(vm, NJ_C_IMAGE2D, "_nj_fmt", "I"), 100);
            nj_iput(vm, o, m3g_fof(vm, NJ_C_IMAGE2D, "_nj_w", "I"), (nj_s4)w);
            nj_iput(vm, o, m3g_fof(vm, NJ_C_IMAGE2D, "_nj_h", "I"), (nj_s4)h);
            nj_rput(vm, o, m3g_fof(vm, NJ_C_IMAGE2D, "_nj_pixels", "[B"), px);
            break;
        }
        case NJ_M3G_T_MATERIAL:
            nj_iput(vm, o, m3g_fof(vm, NJ_C_MAT, "_nj_c_amb", "I"), (nj_s4)f->a);
            nj_iput(vm, o, m3g_fof(vm, NJ_C_MAT, "_nj_c_dif", "I"), (nj_s4)f->b);
            nj_iput(vm, o, m3g_fof(vm, NJ_C_MAT, "_nj_c_emi", "I"), (nj_s4)f->c);
            nj_iput(vm, o, m3g_fof(vm, NJ_C_MAT, "_nj_c_spec", "I"), (nj_s4)f->d);
            nj_iput(vm, o, m3g_fof(vm, NJ_C_MAT, "_nj_shininess", "F"),
                    (nj_s4)f2bits(f->f[0]));
            nj_iput(vm, o, m3g_fof(vm, NJ_C_MAT, "_nj_vcolor", "Z"),
                    (f->f[1] != 0.0f) ? 1 : 0);
            break;
        case NJ_M3G_T_POLYGON_MODE:
            /*
             * The three enumerations the file carries.  Nothing used to write
             * them, so every loaded PolygonMode kept the constructor's default
             * - back-face culling - even when the file asked for CULL_NONE,
             * which is what the rally title's twelve PolygonModes all ask for
             * (culling 162, shading 165, winding 168).  A byte of zero means
             * the file did not set that field, and the default stands.
             */
            if (f->a != 0u) {
                nj_iput(vm, o, m3g_fof(vm, NJ_C_POLY, "_nj_culling", "I"),
                        (nj_s4)f->a);
            }
            if (f->b != 0u) {
                nj_iput(vm, o, m3g_fof(vm, NJ_C_POLY, "_nj_shading", "I"),
                        (nj_s4)f->b);
            }
            if (f->c != 0u) {
                nj_iput(vm, o, m3g_fof(vm, NJ_C_POLY, "_nj_winding", "I"),
                        (nj_s4)f->c);
            }
            break;
        case NJ_M3G_T_COMPOSITING_MODE:
            nj_iput(vm, o, m3g_fof(vm, NJ_C_COMP, "_nj_blend", "I"),
                    (nj_s4)((f->a >> 8) & 0xFFu));
            /*
             * The four flags are declared "I" on the class, not "Z": the
             * field lookup matches the descriptor exactly and raises an
             * InternalError when the name is there with another type, which is
             * how 都市摩天楼_6192 stopped loading the moment this link step
             * was added.  Booleans are stored as ints throughout this module.
             */
            nj_iput(vm, o, m3g_fof(vm, NJ_C_COMP, "_nj_dtest", "I"),
                    (nj_s4)(f->a & 1u));
            nj_iput(vm, o, m3g_fof(vm, NJ_C_COMP, "_nj_dwrite", "I"),
                    (nj_s4)((f->a >> 1) & 1u));
            nj_iput(vm, o, m3g_fof(vm, NJ_C_COMP, "_nj_cwrite", "I"),
                    (nj_s4)((f->a >> 2) & 1u));
            nj_iput(vm, o, m3g_fof(vm, NJ_C_COMP, "_nj_awrite", "I"),
                    (nj_s4)((f->a >> 3) & 1u));
            nj_iput(vm, o, m3g_fof(vm, NJ_C_COMP, "_nj_athresh", "I"),
                    (nj_s4)((f->a >> 16) & 0xFFu));
            nj_iput(vm, o, m3g_fof(vm, NJ_C_COMP, "_nj_dofact", "F"),
                    (nj_s4)f2bits(f->f[0]));
            nj_iput(vm, o, m3g_fof(vm, NJ_C_COMP, "_nj_dounits", "F"),
                    (nj_s4)f2bits(f->f[1]));
            break;
        case NJ_M3G_T_FOG:
            nj_iput(vm, o, m3g_fof(vm, NJ_C_FOG, "_nj_mode", "I"), (nj_s4)f->b);
            nj_iput(vm, o, m3g_fof(vm, NJ_C_FOG, "_nj_color", "I"), (nj_s4)f->a);
            nj_iput(vm, o, m3g_fof(vm, NJ_C_FOG, "_nj_density", "F"),
                    (nj_s4)f2bits(f->f[0]));
            nj_iput(vm, o, m3g_fof(vm, NJ_C_FOG, "_nj_near", "F"),
                    (nj_s4)f2bits(f->f[0]));
            nj_iput(vm, o, m3g_fof(vm, NJ_C_FOG, "_nj_far", "F"),
                    (nj_s4)f2bits(f->f[1]));
            break;
        case NJ_M3G_T_CAMERA:
            nj_iput(vm, o, m3g_fof(vm, NJ_C_CAMERA, "_nj_is_persp", "Z"),
                    (f->a == 1u) ? 1 : 0);
            if (f->a == 0u) {
                /* A generic projection: the file's own 4x4 matrix, sixteen
                   floats, handed over as it stands. */
                nj_ref g = nj_rget(vm, o, m3g_fof(vm, NJ_C_CAMERA, "_nj_generic",
                                                  "[F"));

                if (g && nj_array_length(g) >= 16u && f->data_len >= 64u) {
                    const float *src = (const float *)(const void *)f->data;
                    float *dst = (float *)nj_array_data(g);
                    nj_u4 k;

                    for (k = 0u; k < 16u; k++) {
                        dst[k] = bits2f((nj_s4)le32(f->data + 4u * k));
                    }
                    (void)src;
                }
            } else {
                nj_iput(vm, o, m3g_fof(vm, NJ_C_CAMERA, "_nj_fovy", "I"),
                        (nj_s4)f2bits(f->f[0]));
                nj_iput(vm, o, m3g_fof(vm, NJ_C_CAMERA, "_nj_aspect", "I"),
                        (nj_s4)f2bits(f->f[1]));
                nj_iput(vm, o, m3g_fof(vm, NJ_C_CAMERA, "_nj_near", "I"),
                        (nj_s4)f2bits(f->f[2]));
                nj_iput(vm, o, m3g_fof(vm, NJ_C_CAMERA, "_nj_far", "I"),
                        (nj_s4)f2bits(f->f[3]));
            }
            break;
        case NJ_M3G_T_LIGHT:
            nj_iput(vm, o, m3g_fof(vm, NJ_C_LIGHT, "_nj_mode", "I"), (nj_s4)f->b);
            nj_iput(vm, o, m3g_fof(vm, NJ_C_LIGHT, "_nj_color", "I"), (nj_s4)f->a);
            nj_iput(vm, o, m3g_fof(vm, NJ_C_LIGHT, "_nj_intensity", "F"),
                    (nj_s4)f2bits(f->f[3]));
            break;
        case NJ_M3G_T_BACKGROUND: {
            nj_ref img = m3g_resolve_ref(vm, arr, nobj, o, f->refs[0], mode);


            nj_iput(vm, o, m3g_fof(vm, NJ_C_BG, "_nj_color", "I"), (nj_s4)f->a);
            nj_iput(vm, o, m3g_fof(vm, NJ_C_BG, "_nj_cclear", "Z"),
                    (nj_s4)((f->b >> 17) & 1u));
            nj_iput(vm, o, m3g_fof(vm, NJ_C_BG, "_nj_dclear", "Z"),
                    (nj_s4)((f->b >> 16) & 1u));
            nj_iput(vm, o, m3g_fof(vm, NJ_C_BG, "_nj_immode", "I"),
                    (nj_s4)(f->b & 0xFFu));
            if (img) {
                nj_rput(vm, o, m3g_fof(vm, NJ_C_BG, "_nj_img",
                                       "L" NJ_C_IMAGE2D ";"), img);
            }
            break;
        }
        case NJ_M3G_T_TEXTURE2D: {
            nj_ref img = m3g_resolve_ref(vm, arr, nobj, o, f->refs[0], mode);

            if (img) {
                nj_rput(vm, o, m3g_fof(vm, NJ_C_TEX2D, "_nj_img",
                                       "L" NJ_C_IMAGE2D ";"), img);
            }
            /*
             * The sampling settings travel with the texture, not with the
             * image: a title that authors FILTER_LINEAR in its file gets the
             * sampler asked for it, and one that leaves them out keeps the
             * JSR defaults the constructor already wrote (NEAREST, REPEAT).
             */
            nj_iput(vm, o, m3g_fof(vm, NJ_C_TEX2D, "_nj_blend", "I"),
                    (nj_s4)(f->b & 0xFFu));
            nj_iput(vm, o, m3g_fof(vm, NJ_C_TEX2D, "_nj_wrapS", "I"),
                    (nj_s4)((f->b >> 8) & 0xFFu));
            nj_iput(vm, o, m3g_fof(vm, NJ_C_TEX2D, "_nj_wrapT", "I"),
                    (nj_s4)((f->b >> 16) & 0xFFu));
            nj_iput(vm, o, m3g_fof(vm, NJ_C_TEX2D, "_nj_filtLevel", "I"),
                    (nj_s4)((f->b >> 24) & 0xFFu));
            nj_iput(vm, o, m3g_fof(vm, NJ_C_TEX2D, "_nj_filtImage", "I"),
                    (nj_s4)f->c);
            nj_iput(vm, o, m3g_fof(vm, NJ_C_TEX2D, "_nj_blendcolor", "I"),
                    (nj_s4)f->a);
            break;
        }
        case NJ_M3G_T_SPRITE: {
            nj_ref img = m3g_resolve_ref(vm, arr, nobj, o, f->refs[0], mode);
            nj_ref app = m3g_resolve_ref(vm, arr, nobj, o, f->refs[1], mode);

            if (img) {
                nj_rput(vm, o, m3g_fof(vm, NJ_C_SPRITE3D, "_nj_img",
                                       "L" NJ_C_IMAGE2D ";"), img);
            }
            if (app) {
                nj_rput(vm, o, m3g_fof(vm, NJ_C_SPRITE3D, "_nj_app",
                                       "L" NJ_C_APPEAR ";"), app);
            }
            break;
        }
        default:
            break;
        }
    }
    return NJ_OK;
}

static nj_status m3g_link_geometry(nj_vm *vm, const nj_m3g_object *objs,
                                   nj_u4 nobj, nj_ref arr)
{
    int mode = m3g_ref_mode_of(vm, objs, nobj, arr);
    nj_u4 i;

    if (mode < 0) {
        mode = M3G_REF_TWO;
    }
    for (i = 0u; i < nobj; i++) {
        const nj_m3g_object *ob = &objs[i];
        nj_ref o;

        if (ob->fields.decoded == 0u) {
            continue;
        }
        o = nj_array_get_ref(arr, i + 1u);
        if (!o) {
            continue;
        }
        if (ob->type == NJ_M3G_T_VERTEX_BUFFER) {
            static const char *names[3] = { "_nj_positions", "_nj_normals",
                                            "_nj_colors" };
            nj_ref bias3;
            float *bp;
            nj_u4 k;

            for (k = 0u; k < 3u; k++) {
                nj_ref a = (ob->fields.refs[k] != 0u)
                         ? m3g_resolve_ref(vm, arr, nobj, o, ob->fields.refs[k], mode)
                         : 0;
                if (a) {
                    nj_rput(vm, o, m3g_fof(vm, NJ_C_VBUF, names[k],
                                           "L" NJ_C_VARR ";"), a);
                }
            }
            if (ob->fields.nrefs > 3u) {
                nj_ref t = m3g_resolve_ref(vm, arr, nobj, o, ob->fields.refs[3], mode);
                if (t) {
                    nj_rput(vm, o, m3g_fof(vm, NJ_C_VBUF, "_nj_tex0",
                                           "L" NJ_C_VARR ";"), t);
                }
            }
            /* The API's setPositions(positions, scale, bias[]) keeps the
               scalar in _nj_bias and the three floats in _nj_scale; the file
               calls the same two things positionScale and positionBias. */
            nj_iput(vm, o, m3g_fof(vm, NJ_C_VBUF, "_nj_bias", "F"),
                    (nj_s4)ob->fields.refs[NJ_M3G_MAX_REFS - 1u]);
            bias3 = nj_array_new(vm, "F", 3u);
            bp = bias3 ? (float *)nj_array_data(bias3) : NULL;
            if (bp) {
                bp[0] = bits2f((nj_s4)ob->fields.refs[NJ_M3G_MAX_REFS - 4u]);
                bp[1] = bits2f((nj_s4)ob->fields.refs[NJ_M3G_MAX_REFS - 3u]);
                bp[2] = bits2f((nj_s4)ob->fields.refs[NJ_M3G_MAX_REFS - 2u]);
                nj_rput(vm, o, m3g_fof(vm, NJ_C_VBUF, "_nj_scale", "[F"),
                        nj_root(vm, bias3));
            }
            /* The same pair for the texture unit: the file stores a scale and
               a three-float bias per unit, and a sampler that ignores them
               draws a correctly shaped car with the wrong part of its skin.
               This runtime keeps unit 0 only, which is the unit the titles in
               the corpus use. */
            nj_iput(vm, o, m3g_fof(vm, NJ_C_VBUF, "_nj_texScale", "F"),
                    (nj_s4)ob->fields.refs[11]);
            bias3 = nj_array_new(vm, "F", 3u);
            bp = bias3 ? (float *)nj_array_data(bias3) : NULL;
            if (bp) {
                bp[0] = bits2f((nj_s4)ob->fields.refs[8]);
                bp[1] = bits2f((nj_s4)ob->fields.refs[9]);
                bp[2] = bits2f((nj_s4)ob->fields.refs[10]);
                nj_rput(vm, o, m3g_fof(vm, NJ_C_VBUF, "_nj_texBias", "[F"),
                        nj_root(vm, bias3));
            }
        } else if (ob->type == NJ_M3G_T_TRIANGLE_STRIP_ARRAY && ob->fields.data) {
            nj_u4 isz = (ob->fields.a == 128u) ? 4u : (ob->fields.a == 130u) ? 2u : 1u;
            nj_u4 n = ob->fields.b, k;
            nj_ref ia = nj_array_new(vm, "I", n);
            nj_s4 *ip = ia ? (nj_s4 *)nj_array_data(ia) : NULL;

            if (ip) {
                for (k = 0u; k < n; k++) {
                    const nj_u1 *q = ob->fields.data + k * isz;
                    ip[k] = (isz == 4u) ? (nj_s4)le32(q)
                          : (isz == 2u)
                          ? (nj_s4)((nj_u2)q[0] | ((nj_u2)q[1] << 8))
                          : (nj_s4)(nj_u1)q[0];
                }
                nj_rput(vm, o, m3g_fof(vm, NJ_C_IBUF, "_nj_ib_data", "[I"),
                        nj_root(vm, ia));
                nj_iput(vm, o, m3g_fof(vm, NJ_C_IBUF, "_nj_ib_count", "I"),
                        (nj_s4)n);
            }
            {
                nj_u4 ns = ob->fields.c;
                nj_ref la = nj_array_new(vm, "I", ns);
                nj_s4 *lp = la ? (nj_s4 *)nj_array_data(la) : NULL;

                if (lp) {
                    for (k = 0u; k < ns; k++) {
                        lp[k] = (nj_s4)le32(ob->fields.data + (ob->fields.ref_off - 17u)
                                            + 4u + 4u * k);
                    }
                    nj_rput(vm, o, m3g_fof(vm, NJ_C_TSA, "_nj_lengths", "[I"),
                            nj_root(vm, la));
                    nj_iput(vm, o, m3g_fof(vm, NJ_C_TSA, "_nj_strips", "I"),
                            (nj_s4)ns);
                }
            }
        }
    }
    return NJ_OK;
}

/*
 * Build the Java objects a container holds, from its bytes.  Both
 * Loader.load() overloads end up here: they differ only in where the bytes
 * come from - a resource name, or a byte[] the title read itself, which is
 * how 浴火银河2 and the other 3D titles hand over a .m3g they unpacked.  The
 * byte[] overload used to stop at the file identifier with "this runtime has
 * no object parser yet" while the String overload parsed the file, so a title
 * that loaded from its own stream got an IOException and an empty scene.
 *
 * The buffer stays owned by the caller; nothing here frees it.
 */
/*
 * Build the object table for a container and hand back the Object3D array.
 * `data` is BORROWED: one caller owns a host buffer it frees itself, the other
 * points straight into a Java byte[], so this function must never free it (it
 * used to, which meant a file load freed its buffer twice and a byte[] load
 * handed an interior pointer of a Java array to the host allocator - harmless
 * only because nj_host_free checks its own header magic and gives up).
 */
static nj_status m3g_external_image(nj_vm *vm, const char *base,
                                    const char *name, nj_ref *result)
{
    char path[512];
    const char *slash = base ? strrchr(base, '/') : NULL;
    nj_u4 mark = nj_root_mark(vm);
    nj_value a[2], got;
    nj_ref image, image2d, str;
    nj_status st;
    size_t prefix = (name[0] != '/' && slash) ? (size_t)(slash - base + 1) : 0u;

    if (prefix + strlen(name) >= sizeof(path)) {
        return nj_throw(vm, "java/io/IOException", "external image path too long");
    }
    if (prefix) { memcpy(path, base, prefix); }
    strcpy(path + prefix, name);
    str = nj_root(vm, nj_string_new(vm, path));
    a[0].r = str;
    /* An external URI may name another M3G container, not just a PNG.
     * Stranded 2 keeps its shared texture objects in all_3d_texture.m3g. */
    {
        nj_app *app = (nj_app *)vm->loader_ud;
        nj_u4 size = 0u;
        void *bytes = app ? nj_app_resource(vm, app, path, &size) : NULL;
        int container = bytes && size >= 12u &&
            memcmp(bytes, "\xabJSR184\xbb\r\n\x1a\n", 12u) == 0;
        if (bytes) { nj_host_free(vm, bytes); }
        if (container) {
            nj_ref loaded = nj_call_ref(vm, NULL, NJ_C_LOADER, "load",
                "(Ljava/lang/String;)[L" NJ_C_O3D ";", a);
            if (!loaded || nj_array_length(loaded) == 0u) {
                nj_root_release(vm, mark);
                return vm->exception ? NJ_ERR :
                    nj_throw(vm, "java/io/IOException", "empty external M3G");
            }
            *result = nj_array_get_ref(loaded, 0u);
            nj_root_release(vm, mark);
            return NJ_OK;
        }
    }
    image = nj_call_ref(vm, NULL, "javax/microedition/lcdui/Image", "createImage",
                       "(Ljava/lang/String;)Ljavax/microedition/lcdui/Image;", a);
    if (!image) {
        nj_root_release(vm, mark);
        return NJ_ERR;
    }
    nj_root(vm, image);
    image2d = nj_root(vm, nj_object_new(vm, NJ_C_IMAGE2D));
    if (!image2d) { nj_root_release(vm, mark); return nj_throw_oom(vm); }
    a[0].i = 100; a[1].r = image;
    st = nj_call_special(vm, image2d, NJ_C_IMAGE2D, "<init>",
                         "(ILjava/lang/Object;)V", a, &got);
    *result = image2d;
    nj_root_release(vm, mark);
    return st;
}

static nj_status m3g_build_objects(nj_vm *vm, const nj_u1 *data, nj_u4 len,
                                   const char *base, nj_value *ret)
{
    /*
     * It is a container, so build the objects it holds.  One Java object
     * per file object, of the class the file's type byte names, carrying
     * the userID the file gave it - which is what Object3D.find() looks
     * for.  The type numbers are the ones in the format specification's
     * ObjectType table, and this mapping was checked against it rather
     * than remembered: an earlier version of the table in the corpus tool
     * had 11 as IndexBuffer when the specification says TriangleStripArray.
     *
     * Keep every object in the one-based reference table, including external
     * images, then link scene members in a second pass. Only unreferenced
     * roots are returned to the caller.
     */
    nj_m3g_object *objs;
    nj_u4 nobj = 0u, nsec = 0u, i;
    nj_u4 cap = 4096u;
    char err[192];
    nj_ref arr;      /* the file's own table: index 0 unused, object i at i + 1 */
    nj_ref roots;    /* what the caller gets: the table's root objects only */
    nj_u1 *is_ref = NULL;
    nj_u4 mark;

    objs = (nj_m3g_object *)nj_host_alloc(vm, (nj_size)(sizeof(nj_m3g_object) * cap));
    if (!objs) {
        return nj_throw(vm, "java/lang/OutOfMemoryError", "the object table");
    }
    if (nj_m3g_scan_objects(vm, (const nj_u1 *)data, len, objs, cap,
                            &nobj, &nsec, err, sizeof(err)) != NJ_OK) {
        m3g_free_refs(vm, objs, nobj);
        nj_host_free(vm, objs);
        return nj_throw(vm, "java/io/IOException", err);
    }
    if (nobj > cap) {
        m3g_free_refs(vm, objs, cap);
        nj_host_free(vm, objs);
        return nj_throw(vm, "java/io/IOException", "too many M3G objects");
    }
    if (nobj == 0u) {
        m3g_free_refs(vm, objs, nobj);
        nj_host_free(vm, objs);
        return nj_throw(vm, "java/io/IOException",
                        "the container holds no objects");
    }
    arr = nj_object_array_new(vm, "javax/microedition/m3g/Object3D", nobj + 1u);
    if (!arr) {
        m3g_free_refs(vm, objs, nobj);
        nj_host_free(vm, objs);
        return NJ_ERR_NOMEM;
    }
    mark = nj_root_mark(vm);
    arr = nj_root(vm, arr);
    for (i = 0u; i < nobj; i++) {
        const char *cls = m3g_class_of(objs[i].type);
        nj_u4 inner;
        nj_ref o = 0;

        if (objs[i].type == NJ_M3G_TYPE_EXTERNAL_REF && objs[i].fields.data) {
            nj_status st = m3g_external_image(vm, base,
                (const char *)objs[i].fields.data, &o);
            if (st != NJ_OK) {
                nj_root_release(vm, mark);
                m3g_free_refs(vm, objs, nobj);
                nj_host_free(vm, objs);
                return st;
            }
            nj_array_set_ref(arr, i + 1u, o);
            continue;
        }
        if (!cls) { continue; }   /* a reserved type: the slot stays empty */
        inner = nj_root_mark(vm);
        o = nj_object_new(vm, cls);
        if (o) {
            o = nj_root(vm, o);
            nj_iput(vm, o, nj_field_lookup(vm, NJ_C_O3D, "_nj_userID", "I", 0),
                    (nj_s4)objs[i].fields.user_id);
            if (objs[i].fields.data_len == 40u && objs[i].fields.data) {
                /*
                 * The transform the file gives this Node.  Without it every
                 * object of a loaded scene sits at the origin, which is what a
                 * World drawn with no model matrices looks like - the camera
                 * and the track end up in the same place.
                 *
                 * The matrix array is allocated here because no Java
                 * constructor runs for an object that came out of a file: the
                 * field is declared on Transformable and its <init> is what
                 * allocates it for a title-built node, so without this the
                 * transform of every loaded Mesh, Group, Camera and Light was
                 * read and then dropped.
                 */
                nj_field *mf = nj_field_lookup(vm, NJ_C_XABLE, "_nj_m", "[F", 0);
                nj_ref m = mf ? nj_rget(vm, o, mf) : 0;

                if (!m) {
                    m = mat_new(vm);
                    if (m && mf) {
                        nj_rput(vm, o, mf, m);
                    }
                }
                if (m && nj_array_length(m) >= 16) {
                    float xf[16];

                    m3g_node_matrix(objs[i].fields.data, xf);
                    memcpy(nj_array_data(m), xf, sizeof(xf));
                }
            }
            if (objs[i].type == NJ_M3G_T_WORLD) {
                nj_rput(vm, o, nj_field_lookup(vm, NJ_C_WORLD, "_nj_all",
                                               "[L" NJ_C_O3D ";", 0), arr);
            }
            nj_array_set_ref(arr, i + 1u, o);
        }
        nj_root_release(vm, inner);
    }
    if (nj_m3g_load_geometry(vm, objs, nobj, arr) != NJ_OK) {
        m3g_free_refs(vm, objs, nobj);
        nj_host_free(vm, objs);
        return NJ_ERR;
    }
    if (m3g_link_geometry(vm, objs, nobj, arr) != NJ_OK) {
        m3g_free_refs(vm, objs, nobj);
        nj_host_free(vm, objs);
        return NJ_ERR;
    }
    if (nj_m3g_link_misc(vm, objs, nobj, arr) != NJ_OK) {
        m3g_free_refs(vm, (nj_m3g_object *)objs, nobj);
        nj_host_free(vm, (void *)objs);
        return NJ_ERR_NOMEM;
    }
    if (nj_m3g_link_groups(vm, objs, nobj, arr) != NJ_OK) {
        m3g_free_refs(vm, objs, nobj);
        nj_host_free(vm, objs);
        return NJ_ERR_STATE;
    }
    /*
     * A Mesh's submeshes are the next thing a title touches after the World -
     * getAppearance(), getIndexBuffer(), getSubmeshCount() - and they are
     * references in the same table, so they are linked here, beside the
     * children rather than inside the object builder.
     */
    if (nj_m3g_link_meshes(vm, objs, nobj, arr) != NJ_OK) {
        m3g_free_refs(vm, objs, nobj);
        nj_host_free(vm, objs);
        return NJ_ERR;
    }
    /*
     * What is still not linked is the rest of the graph, and that is a
     * measured decision rather than an unfinished one.
     *
     * The first version called Group.addChild() for every child the file
     * names.  Need for Speed Most Wanted had been reaching 20 frames with
     * 69 drawing calls; with the linking in place it dies on
     * "the node already has a parent", because the game adds the same
     * nodes itself and JSR 184 gives a Node exactly one parent.  The
     * file's graph and the game's graph are the same graph, and building
     * half of it here means the game cannot build the other half.
     *
     * What the game actually needs is for getChildCount()/getChild() to
     * answer from the file's list, which means storing it on the object
     * rather than mutating the graph behind the game's back.  The indices
     * are decoded and carried (see nj_m3g_fields.refs) so that is a
     * change to the Group class, not to the file parser.
     */
    /*
     * Hand back the file's ROOT objects, not the file's whole table.
     *
     * JSR 184 requires this of the method: load() "returns an array of
     * Object3Ds.  These are the root level objects in the file; in other
     * words, those objects that are not referenced by any other objects.
     * The array is guaranteed not to contain any null objects, but the order
     * of the objects in the array is undefined."
     *
     * The table above is indexed the way the file numbers its objects - from
     * one, with slot 0 unused - so handing it back meant two things the API
     * forbids: null slots, and non-root objects mixed in with the roots.  A
     * title doing the ordinary thing,
     *
     *     World w = (World) Loader.load("/res/arc.m3g")[0];
     *
     * read a null there and died of a NullPointerException inside its own
     * constructor, far from the cause.  3D拉力赛-进化汉化版 is one of them:
     * the race canvas loads its car that way, the constructor aborts, and
     * the half-built canvas then throws on every paint.  The array the API
     * describes has no nulls to trip over.
     *
     * Roots are computed from the references the parser already decoded: an
     * object is a root when no other object names it.  Both index conventions
     * are marked (the value and the value plus one) because which one a file
     * uses is auto-detected later by the linking pass; over-marking drops a
     * root, which costs an object in the answer, while under-marking would
     * hand back something the API says is not there.
     *
     * The World goes first when the file has one.  The order is undefined in
     * the API and the titles in the corpus read [0], so the undefined order
     * is spent on the object they are all looking for.
     */
    is_ref = (nj_u1 *)nj_host_alloc(vm, (nj_size)(nobj + 1u));
    if (!is_ref) {
        m3g_free_refs(vm, objs, nobj);
        nj_host_free(vm, objs);
        return NJ_ERR_NOMEM;
    }
    for (i = 0u; i <= nobj; i++) {
        is_ref[i] = 0u;
    }
    {
        int mode = m3g_ref_mode_of(vm, objs, nobj, arr);
        if (mode < 0) {
            mode = M3G_REF_TWO;
        }
        for (i = 0u; i < nobj; i++) {
            nj_u4 k;
            const nj_m3g_fields *f = &objs[i].fields;
            nj_u4 total = f->nrefs_total ? f->nrefs_total : f->nrefs;
            for (k = 0u; k < total; k++) {
                m3g_mark_ref(is_ref, objs, nobj, mode, nj_m3g_ref_at(f, k), i + 1u);
            }
            for (k = 0u; k < f->nanimation_refs; k++) {
                m3g_mark_ref(is_ref, objs, nobj, mode, f->animation_refs[k], i + 1u);
            }
            if (objs[i].type == NJ_M3G_T_MESH) {
                m3g_mark_ref(is_ref, objs, nobj, mode, f->a, i + 1u);
            }
            if (objs[i].type == NJ_M3G_T_WORLD) {
                /* the World's own two references are read the same way */
                m3g_mark_ref(is_ref, objs, nobj, mode, objs[i].fields.b, i + 1u);
                m3g_mark_ref(is_ref, objs, nobj, mode, objs[i].fields.c, i + 1u);
            }
        }
    }
    {
        nj_u4 count = 0u, world = 0u, at = 0u;
        nj_u4 rootmark;

        for (i = 0u; i < nobj; i++) {
            nj_ref o = nj_array_get_ref(arr, i + 1u);
            if (!o || is_ref[i + 1u]) {
                continue;
            }
            count++;
            if (objs[i].type == NJ_M3G_T_WORLD && !world) {
                world = i + 1u;
            }
        }
        if (count == 0u) {
            /* every object is named by another one: answer with what exists,
             * or the title gets an empty scene for a file full of geometry */
            for (i = 0u; i < nobj; i++) {
                if (nj_array_get_ref(arr, i + 1u)) {
                    count++;
                    if (objs[i].type == NJ_M3G_T_WORLD && !world) {
                        world = i + 1u;
                    }
                }
            }
        }
        rootmark = nj_root_mark(vm);
        roots = nj_object_array_new(vm, "javax/microedition/m3g/Object3D", count);
        if (!roots) {
            nj_root_release(vm, rootmark);
            nj_host_free(vm, is_ref);
            m3g_free_refs(vm, objs, nobj);
            nj_host_free(vm, objs);
            return NJ_ERR_NOMEM;
        }
        roots = nj_root(vm, roots);
        if (world) {
            nj_array_set_ref(roots, at++, nj_array_get_ref(arr, world));
        }
        for (i = 0u; i < nobj && at < count; i++) {
            nj_ref o = nj_array_get_ref(arr, i + 1u);
            if (!o || i + 1u == world || is_ref[i + 1u]) {
                continue;
            }
            nj_array_set_ref(roots, at++, o);
        }
        nj_root_release(vm, rootmark);
    }
    nj_host_free(vm, is_ref);
    nj_root_release(vm, mark);
    m3g_free_refs(vm, objs, nobj);
    nj_host_free(vm, objs);
    ret->r = roots;
    return NJ_OK;
}

NJFN(m3g_load)
{
    nj_ref name = NJ_ARG_REF(args, 0);
    nj_app *app = (nj_app *)vm->loader_ud;
    char cname[256];
    void *data;
    nj_u4 len = 0u;

    if (!name) {
        return nj_throw(vm, "java/lang/NullPointerException", "name is null");
    }
    if (!app) {
        return nj_throw(vm, "java/io/IOException", "no application is loaded");
    }
    if (nj_string_to_utf8(name, cname, (int)sizeof(cname)) <= 0) {
        return nj_throw(vm, "java/io/IOException", "bad resource name");
    }
    data = nj_app_resource(vm, app, cname, &len);
    if (!data || len == 0u) {
        if (data) {
            nj_host_free(vm, data);
        }
        return nj_throw(vm, "java/io/IOException", cname);
    }
    /*
     * The container signature.  I first wrote the four-byte 0xAB4A4A4A that I
     * remembered from the specification, and a real title then reported
     * "wrong magic" for a file that is one:
     *
     *     $ unzip -p 毁灭战士3_10598.jar mars/mar.m3g | head -c 8 | xxd
     *     00000000: ab4a 5352 3138 34bb 0d0a 1a0a 005f 0000  .JSR184......_..
     *
     * So the fixed part is the two bytes 0xAB 0x4A, followed by the ASCII tag
     * "JSR184".  Only the two bytes are checked here; anything more specific
     * would be a guess, and a wrong guess rejects valid files - which is the
     * same defect this comment is about.
     */
    {
        nj_status st = m3g_build_objects(vm, (const nj_u1 *)data, len, cname, ret);

        nj_host_free(vm, data);
        return st;
    }
}

/*
 * The class a file object of this type becomes, from the ObjectType table in
 * the format specification.  Zero means the type is reserved (23 to 254), and
 * the caller leaves that slot of the array empty rather than inventing one.
 */
static const char *m3g_class_of(nj_u1 type)
{
    switch (type) {
    case 1u:   return "javax/microedition/m3g/AnimationController";
    case 2u:   return "javax/microedition/m3g/AnimationTrack";
    case 3u:   return "javax/microedition/m3g/Appearance";
    case 4u:   return "javax/microedition/m3g/Background";
    case 5u:   return "javax/microedition/m3g/Camera";
    case 6u:   return "javax/microedition/m3g/CompositingMode";
    case 7u:   return "javax/microedition/m3g/Fog";
    case 8u:   return "javax/microedition/m3g/PolygonMode";
    case 9u:   return "javax/microedition/m3g/Group";
    case 10u:  return "javax/microedition/m3g/Image2D";
    case 11u:  return "javax/microedition/m3g/TriangleStripArray";
    case 12u:  return "javax/microedition/m3g/Light";
    case 13u:  return "javax/microedition/m3g/Material";
    case 14u:  return "javax/microedition/m3g/Mesh";
    case 15u:  return "javax/microedition/m3g/MorphingMesh";
    case 16u:  return "javax/microedition/m3g/SkinnedMesh";
    case 17u:  return "javax/microedition/m3g/Texture2D";
    case 18u:  return "javax/microedition/m3g/Sprite";
    case 19u:  return "javax/microedition/m3g/KeyframeSequence";
    case 20u:  return "javax/microedition/m3g/VertexArray";
    case 21u:  return "javax/microedition/m3g/VertexBuffer";
    case 22u:  return "javax/microedition/m3g/World";
    default:   return NULL;   /* 0 is the header, 255 an external reference */
    }
}

/* ---- Image2D ------------------------------------------------------- */
/* The standard three-int constructor creates a mutable image. Its storage
 * is allocated lazily on the first upload; subsequent uploads reuse it. */
NJFN(m3g_img2d_init3)
{
    nj_ref self = NJ_THIS(args);
    nj_s4 fmt = NJ_ARG_INT(args, 0);
    nj_s4 w = NJ_ARG_INT(args, 1), h = NJ_ARG_INT(args, 2);

    if (fmt < 96 || fmt > 100 || w <= 0 || h <= 0) {
        return nj_throw(vm, "java/lang/IllegalArgumentException", "image format or size");
    }
    nj_iput(vm, self, m3g_fof(vm, NJ_C_IMAGE2D, "_nj_fmt", "I"),
            (nj_s4)((nj_u4)fmt | M3G_IMAGE_MUTABLE));
    nj_iput(vm, self, m3g_fof(vm, NJ_C_IMAGE2D, "_nj_w", "I"), w);
    nj_iput(vm, self, m3g_fof(vm, NJ_C_IMAGE2D, "_nj_h", "I"), h);
    return NJ_OK;
}

NJFN(m3g_img2d_init_bytes)
{
    nj_ref self = NJ_THIS(args);

    if (NJ_ARG_INT(args, 0) < 96 || NJ_ARG_INT(args, 0) > 100) {
        return nj_throw(vm, "java/lang/IllegalArgumentException", "image format");
    }
    nj_iput(vm, self, nj_field_lookup(vm, NJ_C_IMAGE2D, "_nj_fmt", "I", 0),
            NJ_ARG_INT(args, 0));
    nj_iput(vm, self, nj_field_lookup(vm, NJ_C_IMAGE2D, "_nj_w", "I", 0),
            NJ_ARG_INT(args, 1));
    nj_rput(vm, self, nj_field_lookup(vm, NJ_C_IMAGE2D, "_nj_pixels", "[B", 0),
            NJ_ARG_REF(args, 2));
    m3g_image_dirty(vm, self);
    return NJ_OK;
}
/*
 * getFormat/getWidth/getHeight each read the field their constructor wrote.
 * Three natives rather than one shared by name: nj_native_entry carries no
 * per-entry cookie and the VM exposes no "which method am I" call, so the
 * cheap answer is the obvious one.
 */
/*
 * Image2D(int format, Object image).
 *
 * Which of the two shapes the object has decides where it is kept: an Image
 * goes into _nj_image with its size copied out, a byte[] into _nj_pixels.
 * Anything else is ignored rather than rejected -- a title that passes
 * something this runtime does not know how to hold is better off running with
 * an empty texture than dying in a constructor.
 */
NJFN(m3g_img2d_init_obj)
{
    nj_ref self = NJ_THIS(args);
    nj_ref o = NJ_ARG_REF(args, 1);
    const nj_field *f;

    if (NJ_ARG_INT(args, 0) < 96 || NJ_ARG_INT(args, 0) > 100) {
        return nj_throw(vm, "java/lang/IllegalArgumentException", "image format");
    }
    f = nj_field_lookup(vm, NJ_C_IMAGE2D, "_nj_fmt", "I", 0);
    if (f) {
        nj_field_write_int(vm, self, f, NJ_ARG_INT(args, 0));
    }
    if (!o) {
        return NJ_OK;
    }
    f = nj_field_lookup(vm, NJ_C_IMAGE2D, "_nj_image",
                        "Ljavax/microedition/lcdui/Image;", 0);
    if (f && nj_class_is_subclass_of(o->cls, nj_class_lookup(vm, "javax/microedition/lcdui/Image"))) {
        nj_value r;
        nj_s4 w = 0, h = 0;

        nj_field_write_ref(vm, self, f, o);
        if (nj_call(vm, o, "javax/microedition/lcdui/Image", "getWidth", "()I", NULL, &r) == NJ_OK) {
            w = r.i;
        }
        if (nj_call(vm, o, "javax/microedition/lcdui/Image", "getHeight", "()I", NULL, &r) == NJ_OK) {
            h = r.i;
        }
        f = nj_field_lookup(vm, NJ_C_IMAGE2D, "_nj_w", "I", 0);
        if (f) {
            nj_field_write_int(vm, self, f, w);
        }
        f = nj_field_lookup(vm, NJ_C_IMAGE2D, "_nj_h", "I", 0);
        if (f) {
            nj_field_write_int(vm, self, f, h);
        }
        /*
         * The LCDUI image keeps its pixels as ARGB ints in "imgPixels", and
         * the sampler reads bytes in RGBA order: convert once, here, rather
         * than at every sample.  This is the path a title takes when it wraps
         * a PNG it loaded itself - the rally title builds its textures this
         * way, which is why decoding the file's own Image2D payload was not
         * enough to put a picture on the track.
         */
        if (w > 0 && h > 0 && (nj_size)w * (nj_size)h <= 4096u * 4096u) {
            nj_field *pxf = nj_field_lookup(vm, "javax/microedition/lcdui/Image",
                                            "imgPixels", "[I", 0);
            nj_ref src = pxf ? nj_field_read_ref(vm, o, pxf) : 0;
            nj_u4 n = (nj_u4)w * (nj_u4)h;

            if (src && nj_array_length(src) >= n) {
                const nj_s4 *sp = (const nj_s4 *)nj_array_data(src);
                nj_ref dst = nj_array_new(vm, "B", n * 4u);

                if (dst) {
                    nj_u4 mark = nj_root_mark(vm);
                    nj_u1 *dp;
                    nj_u4 i;

                    nj_root(vm, dst);
                    dp = (nj_u1 *)nj_array_data(dst);
                    for (i = 0u; i < n; i++) {
                        nj_u4 c = (nj_u4)sp[i];

                        dp[i * 4u] = (nj_u1)((c >> 16) & 0xFFu);
                        dp[i * 4u + 1u] = (nj_u1)((c >> 8) & 0xFFu);
                        dp[i * 4u + 2u] = (nj_u1)(c & 0xFFu);
                        dp[i * 4u + 3u] = (nj_u1)((c >> 24) & 0xFFu);
                    }
                    f = nj_field_lookup(vm, NJ_C_IMAGE2D, "_nj_fmt", "I", 0);
                    if (f) {
                        nj_field_write_int(vm, self, f, 100);
                    }
                    f = nj_field_lookup(vm, NJ_C_IMAGE2D, "_nj_pixels", "[B", 0);
                    if (f) {
                        nj_field_write_ref(vm, self, f, dst);
                    }
                    nj_root_release(vm, mark);
                }
            }
        }
        return NJ_OK;
    }
    f = nj_field_lookup(vm, NJ_C_IMAGE2D, "_nj_pixels", "[B", 0);
    if (f) {
        nj_field_write_ref(vm, self, f, o);
    }
    return NJ_OK;
}

NJFN(m3g_img2d_getFormat)
{
    ret->i = m3g_image_format(vm, NJ_THIS(args));
    return NJ_OK;
}

NJFN(m3g_img2d_getWidth)
{
    ret->i = nj_iget(vm, NJ_THIS(args),
                     nj_field_lookup(vm, NJ_C_IMAGE2D, "_nj_w", "I", 0));
    return NJ_OK;
}

NJFN(m3g_img2d_getHeight)
{
    ret->i = nj_iget(vm, NJ_THIS(args),
                     nj_field_lookup(vm, NJ_C_IMAGE2D, "_nj_h", "I", 0));
    return NJ_OK;
}

/* Image2D.set(x, y, width, height, pixels): copy a tightly packed rectangle,
 * preserving format, dimensions, mutability and pixels outside the region. */
NJFN(m3g_img2d_set)
{
    nj_ref self = NJ_THIS(args), src = NJ_ARG_REF(args, 4), px;
    nj_s4 x = NJ_ARG_INT(args, 0), y = NJ_ARG_INT(args, 1);
    nj_s4 w = NJ_ARG_INT(args, 2), h = NJ_ARG_INT(args, 3), iw, ih;
    nj_u4 flags, fmt, bpp, mark, row;
    nj_u8 required, total;
    nj_size row_bytes;
    nj_u1 *dst;
    const nj_u1 *sp;
    nj_field *pixels_field;

    if (!src) {
        return nj_throw(vm, "java/lang/NullPointerException", "image pixels");
    }
    flags = (nj_u4)nj_iget(vm, self, m3g_fof(vm, NJ_C_IMAGE2D, "_nj_fmt", "I"));
    if (!(flags & M3G_IMAGE_MUTABLE)) {
        return nj_throw(vm, "java/lang/IllegalStateException", "immutable Image2D");
    }
    fmt = flags & M3G_IMAGE_FORMAT_MASK;
    iw = nj_iget(vm, self, m3g_fof(vm, NJ_C_IMAGE2D, "_nj_w", "I"));
    ih = nj_iget(vm, self, m3g_fof(vm, NJ_C_IMAGE2D, "_nj_h", "I"));
    /* Subtraction after bounds checks avoids overflowing x+w and y+h. */
    if (fmt < 96u || fmt > 100u || x < 0 || y < 0 || x > iw || y > ih ||
        w <= 0 || h <= 0 || w > iw - x || h > ih - y) {
        return nj_throw(vm, "java/lang/IllegalArgumentException", "image region");
    }
    bpp = m3g_image_bpp(fmt);
    required = (nj_u8)(nj_u4)w * (nj_u4)h * bpp;
    if (required > nj_array_length(src)) {
        return nj_throw(vm, "java/lang/IllegalArgumentException", "image pixels too short");
    }
    total = (nj_u8)(nj_u4)iw * (nj_u4)ih * bpp;
    if (total > 0x7fffffffu) {
        return nj_throw_oom(vm);
    }
    pixels_field = m3g_fof(vm, NJ_C_IMAGE2D, "_nj_pixels", "[B");
    px = nj_rget(vm, self, pixels_field);
    if (px && nj_array_length(px) < total) {
        return nj_throw(vm, "java/lang/IllegalStateException", "invalid image storage");
    }
    /* Native callers can hold their arguments only in C locals. Root both
     * before allocating; acquire byte pointers after that allocation. */
    mark = nj_root_mark(vm);
    self = nj_root(vm, self);
    src = nj_root(vm, src);
    if (!px) {
        px = nj_array_new(vm, "B", (nj_u4)total);
        if (!px) {
            nj_root_release(vm, mark);
            return nj_throw_oom(vm);
        }
        nj_rput(vm, self, pixels_field, px);
    }
    dst = (nj_u1 *)nj_array_data(px);
    sp = (const nj_u1 *)nj_array_data(src);
    row_bytes = (nj_size)w * bpp;
    for (row = 0u; row < (nj_u4)h; row++) {
        nj_size offset = (((nj_size)y + row) * (nj_size)iw + (nj_size)x) * bpp;
        memmove(dst + offset, sp + (nj_size)row * row_bytes, row_bytes);
    }
    m3g_image_dirty(vm, self);
    nj_root_release(vm, mark);
    return NJ_OK;
}

/* ------------------------------------------------------------------ */
/* Tables                                                              */
/* ------------------------------------------------------------------ */
/*
 * Loader.load(byte[] data, int offset).
 *
 * The same verdict as load(String), from a buffer the caller owns - which is
 * also the only one of the two a unit test can reach, because load(String)
 * needs an application loaded to have a JAR to read from.  `offset` is where
 * the container starts, per the reference loader.
 */
NJFN(m3g_load_bytes)
{
    nj_ref data = NJ_ARG_REF(args, 0);
    nj_s4 off = NJ_ARG_INT(args, 1);
    const unsigned char *b;
    nj_u4 n;

    if (!data) {
        return nj_throw(vm, "java/lang/NullPointerException", "data is null");
    }
    if (off < 0 || (nj_u4)off >= data->length) {
        return nj_throw(vm, "java/lang/IndexOutOfBoundsException", "offset");
    }
    b = (const unsigned char *)data->u.bytes + off;
    n = data->length - (nj_u4)off;
    if (!m3g_has_file_id(b, n)) {
        return nj_throw(vm, "java/io/IOException",
                        "the data is not a .m3g container "
                        "(wrong file identifier)");
    }
    return m3g_build_objects(vm, (const nj_u1 *)b, n, NULL, ret);
}

#define M(name, desc, flags) { name, desc, flags }

/* ------------------------------------------------------------------ */
/* Light                                                               */
/* ------------------------------------------------------------------ */
/*
 * A Light is a Node with a mode and a colour.  It is here because
 * 生化危机3D(CAPCOM经典) asks for the class and stops without it; the values
 * it is given are remembered and handed back, and nothing is lit, because
 * there is no rasteriser yet to light anything with.
 */
static const nj_builtin_field light_fields[] = {
    { "_nj_mode",       "I",  0 },
    { "_nj_color",      "I",  0 },
    { "_nj_intensity",  "F",  0 },
    { "_nj_spotAngle",  "F",  0 },
    { "_nj_spotExp",    "F",  0 },
    { "_nj_atten",      "[F", 0 },
    /* The mode constants, static and final: the values a title inlined when
     * it was compiled against the real API, so they have to match. */
    { "AMBIENT",        "I",  NJ_ACC_PUBLIC | NJ_ACC_STATIC | NJ_ACC_FINAL },
    { "DIRECTIONAL",    "I",  NJ_ACC_PUBLIC | NJ_ACC_STATIC | NJ_ACC_FINAL },
    { "OMNI",           "I",  NJ_ACC_PUBLIC | NJ_ACC_STATIC | NJ_ACC_FINAL },
    { "SPOT",           "I",  NJ_ACC_PUBLIC | NJ_ACC_STATIC | NJ_ACC_FINAL }
};

static const nj_builtin_method light_methods[] = {
    M("<init>",           "()V",    NJ_ACC_PUBLIC),
    M("setMode",          "(I)V",   NJ_ACC_PUBLIC),
    M("getMode",          "()I",    NJ_ACC_PUBLIC),
    M("setColor",         "(I)V",   NJ_ACC_PUBLIC),
    M("getColor",         "()I",    NJ_ACC_PUBLIC),
    M("setIntensity",     "(F)V",   NJ_ACC_PUBLIC),
    M("getIntensity",     "()F",    NJ_ACC_PUBLIC),
    M("setSpotAngle",     "(F)V",   NJ_ACC_PUBLIC),
    M("getSpotAngle",     "()F",    NJ_ACC_PUBLIC),
    M("setSpotExponent",  "(F)V",   NJ_ACC_PUBLIC),
    M("getSpotExponent",  "()F",    NJ_ACC_PUBLIC),
    M("setAttenuation",   "(FFF)V", NJ_ACC_PUBLIC),
    M("getAttenuation",   "([F)V",  NJ_ACC_PUBLIC),
    M("getConstantAttenuation", "()F", NJ_ACC_PUBLIC),
    M("getLinearAttenuation", "()F", NJ_ACC_PUBLIC),
    M("getQuadraticAttenuation", "()F", NJ_ACC_PUBLIC),
};

/*
 * A Light whose storage cannot be found is a broken build, not a broken
 * MIDlet, so say so and throw rather than dereference NULL: a missing field
 * here would otherwise show up as a segfault inside a native, which tells the
 * next person nothing.
 */
static nj_field *light_f(nj_vm *vm, const char *name, const char *desc)
{
    nj_field *f = nj_field_lookup(vm, NJ_C_LIGHT, name, desc, 0);

    if (!f) {
        /* Name the field: "a field this native needs" is true and useless, and
         * it is what made 浴火银河2's InternalError take a rebuild to place. */
        char msg[128];

        snprintf(msg, sizeof(msg),
                 "Light has no field %s of type %s", name, desc);
        nj_throw(vm, "java/lang/InternalError", msg);
    }
    return f;
}

NJFN(m3g_light_init)
{
    nj_ref self = NJ_THIS(args);
    nj_u4 mark = nj_root_mark(vm);
    nj_ref atten;

    /*
     * The receiver is rooted as well as the array.  A native's arguments are a
     * plain C array, so the collector cannot see the object it was called on;
     * nj_array_new() below can collect, and the field writes that follow then
     * land in reused memory.  (The array is rooted for the same reason: it is
     * held only by a C local while the lookups run.)
     */
    self = nj_root(vm, self);
    atten = nj_array_new(vm, "F", 3);
    if (!atten) {
        nj_root_release(vm, mark);
        return nj_throw_oom(vm);
    }
    atten = nj_root(vm, atten);
    nj_iput(vm, self, light_f(vm, "_nj_mode", "I"), 129);      /* DIRECTIONAL */
    nj_iput(vm, self, light_f(vm, "_nj_color", "I"), 0x00FFFFFF);
    nj_iput(vm, self, light_f(vm, "_nj_intensity", "F"), f2bits(1.0f));
    nj_iput(vm, self, light_f(vm, "_nj_spotAngle", "F"), f2bits(45.0f));
    nj_iput(vm, self, light_f(vm, "_nj_spotExp", "F"), f2bits(0.0f));
    nj_rput(vm, self, light_f(vm, "_nj_atten", "[F"), atten);
    nj_root_release(vm, mark);
    return NJ_OK;
}

NJFN(m3g_light_setMode)
{
    nj_iput(vm, NJ_THIS(args), light_f(vm, "_nj_mode", "I"), NJ_ARG_INT(args, 0));
    return NJ_OK;
}

NJFN(m3g_light_getMode)
{
    ret->i = nj_iget(vm, NJ_THIS(args), light_f(vm, "_nj_mode", "I"));
    return NJ_OK;
}

NJFN(m3g_light_setColor)
{
    nj_iput(vm, NJ_THIS(args), light_f(vm, "_nj_color", "I"), NJ_ARG_INT(args, 0));
    return NJ_OK;
}

NJFN(m3g_light_getColor)
{
    ret->i = nj_iget(vm, NJ_THIS(args), light_f(vm, "_nj_color", "I"));
    return NJ_OK;
}

NJFN(m3g_light_setIntensity)
{
    nj_iput(vm, NJ_THIS(args), light_f(vm, "_nj_intensity", "F"),
            f2bits(NJ_ARG_FLOAT(args, 0)));
    return NJ_OK;
}

NJFN(m3g_light_getIntensity)
{
    ret->f = bits2f(nj_iget(vm, NJ_THIS(args), light_f(vm, "_nj_intensity", "F")));
    return NJ_OK;
}

NJFN(m3g_light_setSpotAngle)
{
    nj_iput(vm, NJ_THIS(args), light_f(vm, "_nj_spotAngle", "F"),
            f2bits(NJ_ARG_FLOAT(args, 0)));
    return NJ_OK;
}

NJFN(m3g_light_getSpotAngle)
{
    ret->f = bits2f(nj_iget(vm, NJ_THIS(args), light_f(vm, "_nj_spotAngle", "F")));
    return NJ_OK;
}

NJFN(m3g_light_setSpotExponent)
{
    nj_iput(vm, NJ_THIS(args), light_f(vm, "_nj_spotExp", "F"),
            f2bits(NJ_ARG_FLOAT(args, 0)));
    return NJ_OK;
}

NJFN(m3g_light_getSpotExponent)
{
    ret->f = bits2f(nj_iget(vm, NJ_THIS(args), light_f(vm, "_nj_spotExp", "F")));
    return NJ_OK;
}

NJFN(m3g_light_setAttenuation)
{
    nj_ref a = nj_rget(vm, NJ_THIS(args), light_f(vm, "_nj_atten", "[F"));
    float *d = a ? (float *)nj_array_data(a) : NULL;

    if (!d || a->length < 3) {
        return nj_throw_oom(vm);
    }
    d[0] = NJ_ARG_FLOAT(args, 0);
    d[1] = NJ_ARG_FLOAT(args, 1);
    d[2] = NJ_ARG_FLOAT(args, 2);
    return NJ_OK;
}

NJFN(m3g_light_getAttenuation)
{
    nj_ref a = nj_rget(vm, NJ_THIS(args), light_f(vm, "_nj_atten", "[F"));
    nj_ref out = NJ_ARG_REF(args, 0);
    float *d = a ? (float *)nj_array_data(a) : NULL;
    float *o = out ? (float *)nj_array_data(out) : NULL;

    if (!o || out->length < 3) {
        return nj_throw(vm, "java/lang/IllegalArgumentException",
                        "the array must hold at least three elements");
    }
    if (d) {
        o[0] = d[0];
        o[1] = d[1];
        o[2] = d[2];
    }
    return NJ_OK;
}

/*
 * The mode constants are seeded at run time because a MIDlet compiled against
 * the real API has them inlined at its call sites: a Light built with
 * Light.DIRECTIONAL carries 129 whatever this runtime believes.  Leaving the
 * static fields at zero would make getMode() disagree with the value the
 * title passed to setMode() while both sides looked self-consistent - the
 * same trap List.IMPLICIT and RecordComparator.FOLLOWS fell into.
 */
nj_status nj_m3g_late_init(nj_vm *vm)
{
    /*
     * JSR 184 fixes every one of these numbers, and a MIDlet carries the
     * values javac inlined from whatever it compiled against - so a constant
     * this runtime does not seed reads 0 and the game hands 0 back to us.
     * The values are from the JSR 184 class definitions, not from a guess.
     */
    static const struct { const char *cls; const char *name; nj_s4 value; } k[] = {
        { NJ_C_LIGHT, "AMBIENT", 128 }, { NJ_C_LIGHT, "DIRECTIONAL", 129 },
        { NJ_C_LIGHT, "OMNI", 130 },    { NJ_C_LIGHT, "SPOT", 131 },
        { NJ_C_POLY, "CULL_BACK", 160 },  { NJ_C_POLY, "CULL_FRONT", 161 },
        { NJ_C_POLY, "CULL_NONE", 162 },
        { NJ_C_POLY, "SHADE_FLAT", 164 }, { NJ_C_POLY, "SHADE_SMOOTH", 165 },
        { NJ_C_POLY, "WINDING_CCW", 168 }, { NJ_C_POLY, "WINDING_CW", 169 },
        { NJ_C_COMP, "ALPHA", 64 },      { NJ_C_COMP, "ALPHA_ADD", 65 },
        { NJ_C_COMP, "MODULATE", 66 },   { NJ_C_COMP, "MODULATE_X2", 67 },
        { NJ_C_COMP, "REPLACE", 68 },
        { NJ_C_TEX2D, "FILTER_BASE_LEVEL", 208 },
        { NJ_C_TEX2D, "FILTER_LINEAR", 209 },
        { NJ_C_TEX2D, "FILTER_NEAREST", 210 },
        { NJ_C_TEX2D, "FUNC_ADD", 224 }, { NJ_C_TEX2D, "FUNC_BLEND", 225 },
        { NJ_C_TEX2D, "FUNC_DECAL", 226 }, { NJ_C_TEX2D, "FUNC_MODULATE", 227 },
        { NJ_C_TEX2D, "FUNC_REPLACE", 228 },
        { NJ_C_TEX2D, "WRAP_CLAMP", 240 }, { NJ_C_TEX2D, "WRAP_REPEAT", 241 }
    };
    size_t i;

    for (i = 0; i < sizeof(k) / sizeof(k[0]); i++) {
        nj_field *f = nj_field_lookup(vm, k[i].cls, k[i].name, "I", 1);
        if (f) {
            nj_field_write_int(vm, NULL, f, k[i].value);
        }
    }
    return NJ_OK;
}

static const nj_builtin_field o3d_fields[] = {
    { "_nj_userID", "I", NJ_ACC_PUBLIC },
    { "_nj_tracks", "L" NJ_C_VECTOR ";", NJ_ACC_PUBLIC },
    { "_nj_user", "Ljava/lang/Object;", NJ_ACC_PUBLIC },
};
static const nj_builtin_field xable_fields[] = {
    { "_nj_m", "[F", NJ_ACC_PUBLIC }
};
static const nj_builtin_field xform_fields[] = {
    { "_nj_m", "[F", NJ_ACC_PUBLIC }
};
static const nj_builtin_field node_fields[] = {
    { "_nj_render", "Z", NJ_ACC_PUBLIC },
    /*
     * The rendering enable flag with the default the spec gives it.  A Java
     * boolean field starts out false, and JSR 184 says a Node renders until
     * setRenderingEnable(false) says otherwise - so the flag the walk asks
     * about is this one, which is set only when a title speaks.  _nj_render
     * stays for isRenderingEnabled() and for anything that reads the field.
     */
    { "_nj_hidden", "Z", NJ_ACC_PUBLIC },
    { "_nj_alpha",  "F", NJ_ACC_PUBLIC },
    { "_nj_alphaSet", "Z", NJ_ACC_PUBLIC },
    { "_nj_parent", "L" NJ_C_NODE ";", NJ_ACC_PUBLIC },
    { "_nj_scope",  "I", NJ_ACC_PUBLIC },
    { "_nj_pick",   "Z", NJ_ACC_PUBLIC },
    { "_nj_alignZ",  "L" NJ_C_NODE ";", NJ_ACC_PUBLIC },
    { "_nj_alignZA", "I", NJ_ACC_PUBLIC },
    { "_nj_alignY",  "L" NJ_C_NODE ";", NJ_ACC_PUBLIC },
    { "_nj_alignYA", "I", NJ_ACC_PUBLIC },
};
static const nj_builtin_field group_fields[] = {
    { "_nj_kids", "L" NJ_C_VECTOR ";", NJ_ACC_PUBLIC }
};
static const nj_builtin_field world_fields[] = {
    { "_nj_cam", "L" NJ_C_CAMERA ";", NJ_ACC_PUBLIC },
    { "_nj_bg",  "L" NJ_C_BG ";", NJ_ACC_PUBLIC },
    /*
     * Every object the World's file declared, in table order.
     *
     * A file numbers its references one of three ways and a title that reads
     * its model out of the world by userID is entitled to get it; when the
     * reading chosen for the file is not the one its author used, the object is
     * in the file but not in the linked tree, the lookup answers null, and the
     * title calls addChild(null) - 毁灭战士3_10598 does exactly that.  Keeping
     * the table lets find() answer what the title asked for instead of what our
     * reading of the file produced.
     */
    { "_nj_all", "[L" NJ_C_O3D ";", NJ_ACC_PUBLIC }
};
static const nj_builtin_field camera_fields[] = {
    { "_nj_is_persp",    "Z", NJ_ACC_PUBLIC },
    { "_nj_is_parallel", "Z", NJ_ACC_PUBLIC },
    { "_nj_fovy",   "I", NJ_ACC_PUBLIC },
    { "_nj_height", "I", NJ_ACC_PUBLIC },
    { "_nj_aspect", "I", NJ_ACC_PUBLIC },
    { "_nj_near",   "I", NJ_ACC_PUBLIC },
    { "_nj_far",    "I", NJ_ACC_PUBLIC },
    /* A projection handed over as a matrix, used when set. */
    { "_nj_generic", "[F", NJ_ACC_PUBLIC },
};
static const nj_builtin_field bg_fields[] = {
    { "_nj_color",  "I", NJ_ACC_PUBLIC },
    { "_nj_cclear", "Z", NJ_ACC_PUBLIC },
    { "_nj_dclear", "Z", NJ_ACC_PUBLIC },
    /* An image behind the scene, with the window it is drawn through. */
    { "_nj_img",    "L" NJ_C_IMAGE2D ";", NJ_ACC_PUBLIC },
    { "_nj_immode", "I", NJ_ACC_PUBLIC },
    { "_nj_cropX",  "I", NJ_ACC_PUBLIC },
    { "_nj_cropY",  "I", NJ_ACC_PUBLIC },
    { "_nj_cropW",  "I", NJ_ACC_PUBLIC },
    { "_nj_cropH",  "I", NJ_ACC_PUBLIC },
    { "_nj_immodeY", "I", NJ_ACC_PUBLIC },
};
static const nj_builtin_field g3d_fields[] = {
    { "_nj_nlights", "I", NJ_ACC_PUBLIC },
    { "_nj_target", "Ljava/lang/Object;", NJ_ACC_PUBLIC },
    { "_nj_camera", "L" NJ_C_CAMERA ";", NJ_ACC_PUBLIC },
    { "_nj_cam_m",  "[F", NJ_ACC_PUBLIC },
    { "_nj_vx", "I", NJ_ACC_PUBLIC },
    { "_nj_vy", "I", NJ_ACC_PUBLIC },
    { "_nj_vw", "I", NJ_ACC_PUBLIC },
    { "_nj_vh", "I", NJ_ACC_PUBLIC },
    { "_nj_instance", "L" NJ_C_G3D ";", NJ_ACC_PUBLIC | NJ_ACC_STATIC },
    { "_nj_hints", "I", NJ_ACC_PUBLIC },
    { "_nj_depth", "Z", NJ_ACC_PUBLIC },
    { "_nj_drnear", "F", NJ_ACC_PUBLIC },
    { "_nj_drfar", "F", NJ_ACC_PUBLIC },
    { "_nj_lights", "[L" NJ_C_LIGHT ";", NJ_ACC_PUBLIC },
    { "_nj_lmats", "[F", NJ_ACC_PUBLIC },
    /* The software rasteriser's own state: 1/w per pixel (larger is
       nearer), the viewport it belongs to, and the frame it was last
       cleared in - a title that never calls clear() still renders. */
    { "_nj_zbuf", "[I", NJ_ACC_PUBLIC },
    { "_nj_zw", "I", NJ_ACC_PUBLIC },
    { "_nj_zh", "I", NJ_ACC_PUBLIC },
    { "_nj_zframe", "I", NJ_ACC_PUBLIC },
};
static const nj_builtin_field appear_fields[] = {
    { "_nj_mat",  "L" NJ_C_MAT ";",   NJ_ACC_PUBLIC },
    { "_nj_pm",   "L" NJ_C_POLY ";",  NJ_ACC_PUBLIC },
    { "_nj_cm",   "L" NJ_C_COMP ";",  NJ_ACC_PUBLIC },
    { "_nj_tex0", "L" NJ_C_TEX2D ";", NJ_ACC_PUBLIC },
    { "_nj_tex1", "L" NJ_C_TEX2D ";", NJ_ACC_PUBLIC },
    { "_nj_layer", "I",               NJ_ACC_PUBLIC },
    { "_nj_fog", "L" NJ_C_FOG ";", NJ_ACC_PUBLIC },
};
static const nj_builtin_field varr_fields[] = {
    { "_nj_numVerts", "I", NJ_ACC_PUBLIC },
    { "_nj_numComp",  "I", NJ_ACC_PUBLIC },
    { "_nj_compSize", "I", NJ_ACC_PUBLIC },
    /* Where set() put its data.  Nothing reads these yet - this module does
     * not rasterise - but storing the reference is what lets step two find it
     * instead of the caller's data having been dropped on the floor. */
    { "_nj_data",     "[S", NJ_ACC_PUBLIC },
    { "_nj_first",    "I",  NJ_ACC_PUBLIC },
    { "_nj_count",    "I",  NJ_ACC_PUBLIC },
    { "_nj_ctype", "I", NJ_ACC_PUBLIC },
};
static const nj_builtin_field poly_fields[] = {
    { "_nj_winding", "I", NJ_ACC_PUBLIC },
    { "_nj_persp",   "Z", NJ_ACC_PUBLIC },
    { "_nj_culling", "I", NJ_ACC_PUBLIC },
    { "_nj_shading", "I", NJ_ACC_PUBLIC },
    { "_nj_twosided", "Z", NJ_ACC_PUBLIC },
    { "_nj_localcam", "Z", NJ_ACC_PUBLIC },
};
static const nj_builtin_field comp_fields[] = {
    { "_nj_blend",    "I", NJ_ACC_PUBLIC },
    { "_nj_dtest",    "I", NJ_ACC_PUBLIC },
    { "_nj_dwrite",   "I", NJ_ACC_PUBLIC },
    { "_nj_athresh",  "I", NJ_ACC_PUBLIC },
    { "_nj_awrite", "I", NJ_ACC_PUBLIC },
    { "_nj_cwrite", "I", NJ_ACC_PUBLIC },
    { "_nj_dofact", "F", NJ_ACC_PUBLIC },
    { "_nj_dounits", "F", NJ_ACC_PUBLIC },
};
static const nj_builtin_field tex2d_fields[] = {
    { "_nj_img",   "L" NJ_C_IMAGE2D ";", NJ_ACC_PUBLIC },
    { "_nj_blend", "I",                  NJ_ACC_PUBLIC },
    { "_nj_wrapS", "I",                  NJ_ACC_PUBLIC },
    { "_nj_wrapT", "I",                  NJ_ACC_PUBLIC },
    { "_nj_filtLevel", "I",              NJ_ACC_PUBLIC },
    { "_nj_filtImage", "I",              NJ_ACC_PUBLIC },
    { "_nj_blendcolor", "I", NJ_ACC_PUBLIC },
};
static const nj_builtin_field sprite_fields[] = {
    { "_nj_img", "L" NJ_C_IMAGE2D ";", NJ_ACC_PUBLIC },
    { "_nj_app", "L" NJ_C_APPEAR ";",  NJ_ACC_PUBLIC },
    { "_nj_scaled", "Z", NJ_ACC_PUBLIC },
    { "_nj_cropX", "I", NJ_ACC_PUBLIC },
    { "_nj_cropY", "I", NJ_ACC_PUBLIC },
    { "_nj_cropW", "I", NJ_ACC_PUBLIC },
    { "_nj_cropH", "I", NJ_ACC_PUBLIC },
};
static const nj_builtin_field mesh_fields[] = {
    { "_nj_vb",   "L" NJ_C_VBUF ";", NJ_ACC_PUBLIC },
    { "_nj_ib",   "L" NJ_C_IBUF ";", NJ_ACC_PUBLIC },
    { "_nj_ibs",  "[L" NJ_C_IBUF ";", NJ_ACC_PUBLIC },
    { "_nj_subs", "L" NJ_C_VECTOR ";", NJ_ACC_PUBLIC },
    { "_nj_nsub", "I",                NJ_ACC_PUBLIC }
};
/*
 * javax.microedition.m3g.TriangleStripArray
 *
 * Geometry a title assembles by hand.  Measured over the whole collected
 * corpus, the only members any title actually uses are the two constructors
 * (TriangleStripArray([I[I) and TriangleStripArray(I[I)); the getters below
 * are here because they read back what those constructors stored, and a test
 * cannot check storage without them.
 *
 * The strip lengths are KEPT.  The rasteriser does not exist yet, and
 * accepting a strip and forgetting it is how a "supported" class becomes a
 * wrong picture later.
 */
static const nj_builtin_field tsa_fields[] = {
    { "_nj_lengths", "[I", 0 },
    { "_nj_strips",  "I",  0 },
    { "_nj_first",   "I",  0 }
};

NJFN(m3g_tsa_init_lengths)
{
    nj_ref self = NJ_THIS(args);
    nj_ref idx = NJ_ARG_REF(args, 0);
    nj_ref lens = NJ_ARG_REF(args, 1);

    /*
     * TriangleStripArray(int[] indices, int[] stripLengths): the first array
     * is the indices, the second the lengths.  Storing the first as the
     * lengths - which is what this did - made every vertex index a strip
     * length, so a strip drew the wrong triangles or none at all.
     */
    nj_rput(vm, self, nj_field_lookup(vm, NJ_C_IBUF, "_nj_ib_data", "[I", 0),
            idx);
    nj_iput(vm, self, nj_field_lookup(vm, NJ_C_IBUF, "_nj_ib_count", "I", 0),
            (nj_s4)(idx ? idx->length : 0));
    nj_rput(vm, self, nj_field_lookup(vm, NJ_C_TSA, "_nj_lengths", "[I", 0),
            lens);
    nj_iput(vm, self, nj_field_lookup(vm, NJ_C_TSA, "_nj_strips", "I", 0),
            (nj_s4)(lens ? lens->length : 0));
    nj_iput(vm, self, nj_field_lookup(vm, NJ_C_TSA, "_nj_first", "I", 0), 0);
    return NJ_OK;
}

NJFN(m3g_tsa_init_first)
{
    nj_ref self = NJ_THIS(args);
    nj_ref lens = NJ_ARG_REF(args, 1);

    nj_rput(vm, self, nj_field_lookup(vm, NJ_C_TSA, "_nj_lengths", "[I", 0),
            lens);
    nj_iput(vm, self, nj_field_lookup(vm, NJ_C_TSA, "_nj_strips", "I", 0),
            (nj_s4)(lens ? lens->length : 0));
    nj_iput(vm, self, nj_field_lookup(vm, NJ_C_TSA, "_nj_first", "I", 0),
            NJ_ARG_INT(args, 0));
    return NJ_OK;
}

NJFN(m3g_tsa_getStripCount)
{
    ret->i = nj_field_read_int(vm, NJ_THIS(args),
                               nj_field_lookup(vm, NJ_C_TSA, "_nj_strips",
                                               "I", 0));
    return NJ_OK;
}

NJFN(m3g_tsa_getIndexCount)
{
    nj_ref lens = nj_rget(vm, NJ_THIS(args),
                          nj_field_lookup(vm, NJ_C_TSA, "_nj_lengths", "[I",
                                          0));
    nj_s4 i, total = 0;

    if (lens) {
        for (i = 0; i < (nj_s4)lens->length; i++) {
            total += ((nj_s4 *)lens->u.bytes)[i];
        }
    }
    ret->i = total;
    return NJ_OK;
}

NJFN(m3g_tsa_getStripLengths)
{
    nj_ref self = NJ_THIS(args);
    nj_ref lens = nj_rget(vm, self,
                          nj_field_lookup(vm, NJ_C_TSA, "_nj_lengths", "[I",
                                          0));
    nj_ref dst = NJ_ARG_REF(args, 0);
    nj_s4 i;

    if (!dst || !lens) {
        return NJ_OK;
    }
    if ((nj_s4)lens->length > (nj_s4)dst->length) {
        NJ_THROW(vm, "java/lang/IllegalArgumentException",
                 "the array is too short for the strip lengths");
        return NJ_OK;
    }
    for (i = 0; i < (nj_s4)lens->length; i++) {
        ((nj_s4 *)dst->u.bytes)[i] = ((nj_s4 *)lens->u.bytes)[i];
    }
    return NJ_OK;
}

static const nj_builtin_method tsa_methods[] = {
    M("<init>",          "([I[I)V", NJ_ACC_PUBLIC),
    M("<init>",          "(I[I)V",  NJ_ACC_PUBLIC),
    M("getStripCount",   "()I",     NJ_ACC_PUBLIC),
    M("getIndexCount",   "()I",     NJ_ACC_PUBLIC),
    M("getStripLengths", "([I)V",   NJ_ACC_PUBLIC)
};

static const nj_builtin_field img2d_fields[] = {
    { "_nj_fmt",    "I",   NJ_ACC_PUBLIC },
    { "_nj_w",      "I",   NJ_ACC_PUBLIC },
    { "_nj_h",      "I",   NJ_ACC_PUBLIC },
    { "_nj_pixels", "[B",  NJ_ACC_PUBLIC },
    { "_nj_image",  "Ljavax/microedition/lcdui/Image;", NJ_ACC_PUBLIC },
    { "_nj_palette", "[B", NJ_ACC_PUBLIC },
    { "_nj_mips",   "[B",  NJ_ACC_PUBLIC },
};

static const nj_builtin_method o3d_methods[] = {
    M("setUserID",             "(I)V",                     NJ_ACC_PUBLIC),
    M("getUserID",             "()I",                      NJ_ACC_PUBLIC),
    M("addAnimationTrack",     "(L" NJ_C_ATRACK ";)V",     NJ_ACC_PUBLIC),
    M("animate",               "(I)I",                     NJ_ACC_PUBLIC),
    M("duplicate",             "()L" NJ_C_O3D ";",         NJ_ACC_PUBLIC),
    M("find",                  "(I)L" NJ_C_O3D ";",        NJ_ACC_PUBLIC),
    M("getAnimationTrack",     "(I)L" NJ_C_ATRACK ";",     NJ_ACC_PUBLIC),
    M("getAnimationTrackCount","()I",                      NJ_ACC_PUBLIC),
    M("getReferences", "([Ljavax/microedition/m3g/Object3D;)I", NJ_ACC_PUBLIC),
    M("setUserObject", "(Ljava/lang/Object;)V", NJ_ACC_PUBLIC),
    M("getUserObject", "()Ljava/lang/Object;", NJ_ACC_PUBLIC),
    M("removeAnimationTrack", "(Ljavax/microedition/m3g/AnimationTrack;)V", NJ_ACC_PUBLIC),
};
static const nj_builtin_method transform_methods[] = {
    M("<init>",        "()V",                       NJ_ACC_PUBLIC),
    M("setIdentity",   "()V",                       NJ_ACC_PUBLIC),
    M("get",           "([F)V",                     NJ_ACC_PUBLIC),
    M("set",           "([F)V",                     NJ_ACC_PUBLIC),
    M("set",           "(L" NJ_C_XFORM ";)V",       NJ_ACC_PUBLIC),
    M("postMultiply",  "(L" NJ_C_XFORM ";)V",       NJ_ACC_PUBLIC),
    M("postTranslate", "(FFF)V",                    NJ_ACC_PUBLIC),
    M("postScale",     "(FFF)V",                    NJ_ACC_PUBLIC),
    M("postRotate",    "(FFFF)V",                   NJ_ACC_PUBLIC),
    M("invert",        "()V",                       NJ_ACC_PUBLIC),
    M("transform",     "([F)V",                     NJ_ACC_PUBLIC),
    M("transpose",     "()V",                      NJ_ACC_PUBLIC),
    M("<init>", "(Ljavax/microedition/m3g/Transform;)V", NJ_ACC_PUBLIC),
    M("postRotateQuat", "(FFFF)V", NJ_ACC_PUBLIC),
    M("transform", "(Ljavax/microedition/m3g/VertexArray;[FZ)V", NJ_ACC_PUBLIC),
};
static const nj_builtin_method xable_methods[] = {
    M("setTransform",     "(L" NJ_C_XFORM ";)V", NJ_ACC_PUBLIC),
    M("getTransform",     "(L" NJ_C_XFORM ";)V", NJ_ACC_PUBLIC),
    M("setScale",         "(FFF)V",              NJ_ACC_PUBLIC),
    M("getScale",         "([F)V",               NJ_ACC_PUBLIC),
    M("setOrientation",   "(FFFF)V",             NJ_ACC_PUBLIC),
    M("getOrientation",   "([F)V",               NJ_ACC_PUBLIC),
    M("setTranslation",   "(FFF)V",              NJ_ACC_PUBLIC),
    M("getTranslation",   "([F)V",               NJ_ACC_PUBLIC),
    M("translate",            "(FFF)V",              NJ_ACC_PUBLIC),
    M("scale",                "(FFF)V",              NJ_ACC_PUBLIC),
    M("postRotate",           "(FFFF)V",             NJ_ACC_PUBLIC),
    M("getCompositeTransform","(L" NJ_C_XFORM ";)V", NJ_ACC_PUBLIC),
    M("preRotate", "(FFFF)V", NJ_ACC_PUBLIC),
};
static const nj_builtin_method node_methods[] = {
    M("setRenderingEnable", "(Z)V",                    NJ_ACC_PUBLIC),
    M("isRenderingEnabled", "()Z",                     NJ_ACC_PUBLIC),
    M("setAlphaFactor",     "(F)V",                    NJ_ACC_PUBLIC),
    M("getParent",          "()L" NJ_C_NODE ";",       NJ_ACC_PUBLIC),
    M("getTransformTo",     "(L" NJ_C_NODE ";L" NJ_C_XFORM ";)Z", NJ_ACC_PUBLIC),
    M("setScope",           "(I)V",                    NJ_ACC_PUBLIC),
    M("getScope",           "()I",                     NJ_ACC_PUBLIC),
    M("setPickingEnable",   "(Z)V",                    NJ_ACC_PUBLIC),
    M("isPickingEnabled",   "()Z",                     NJ_ACC_PUBLIC),
    M("setAlignment", "(L" NJ_C_NODE ";IL" NJ_C_NODE ";I)V", NJ_ACC_PUBLIC),
    M("getAlphaFactor", "()F", NJ_ACC_PUBLIC),
    M("align", "(Ljavax/microedition/m3g/Node;)V", NJ_ACC_PUBLIC),
    M("getAlignmentTarget", "(I)I", NJ_ACC_PUBLIC),
    M("getAlignmentReference", "(I)Ljavax/microedition/m3g/Node;", NJ_ACC_PUBLIC),
};
static const nj_builtin_method group_methods[] = {
    M("addChild",       "(L" NJ_C_NODE ";)V",          NJ_ACC_PUBLIC),
    M("removeChild",    "(L" NJ_C_NODE ";)V",          NJ_ACC_PUBLIC),
    M("getChild",       "(I)L" NJ_C_NODE ";",          NJ_ACC_PUBLIC),
    M("getChildCount",  "()I",                         NJ_ACC_PUBLIC),
    M("<init>", "()V", NJ_ACC_PUBLIC),
    M("pick", "(IFFFFFFLjavax/microedition/m3g/RayIntersection;)Z", NJ_ACC_PUBLIC),
    M("pick", "(IFFLjavax/microedition/m3g/Camera;Ljavax/microedition/m3g/RayIntersection;)Z", NJ_ACC_PUBLIC),
};
static const nj_builtin_method world_methods[] = {
    M("setActiveCamera", "(L" NJ_C_CAMERA ";)V",   NJ_ACC_PUBLIC),
    M("getActiveCamera", "()L" NJ_C_CAMERA ";",    NJ_ACC_PUBLIC),
    M("setBackground",   "(L" NJ_C_BG ";)V",       NJ_ACC_PUBLIC),
    M("getBackground",   "()L" NJ_C_BG ";",        NJ_ACC_PUBLIC),
    M("<init>", "()V", NJ_ACC_PUBLIC),
};
static const nj_builtin_method camera_methods[] = {
    M("setPerspective", "(FFFF)V",                NJ_ACC_PUBLIC),
    M("setParallel",    "(FFFF)V",                NJ_ACC_PUBLIC),
    M("getProjection",  "(L" NJ_C_XFORM ";)I",    NJ_ACC_PUBLIC),
    M("setGeneric",     "(L" NJ_C_XFORM ";)V",  NJ_ACC_PUBLIC),
    M("<init>", "()V", NJ_ACC_PUBLIC),
    M("getProjection", "([F)I", NJ_ACC_PUBLIC),
};
static const nj_builtin_method bg_methods[] = {
    M("setColor",            "(I)V", NJ_ACC_PUBLIC),
    M("setColorClearEnable", "(Z)V", NJ_ACC_PUBLIC),
    M("setDepthClearEnable", "(Z)V", NJ_ACC_PUBLIC),
    M("isColorClearEnabled", "()Z", NJ_ACC_PUBLIC),
    M("isDepthClearEnabled", "()Z", NJ_ACC_PUBLIC),
    M("setImage",            "(L" NJ_C_IMAGE2D ";)V", NJ_ACC_PUBLIC),
    M("getImage",            "()L" NJ_C_IMAGE2D ";", NJ_ACC_PUBLIC),
    M("setImageMode",        "(II)V", NJ_ACC_PUBLIC),
    M("getImageMode",        "()I",  NJ_ACC_PUBLIC),
    M("setCrop",             "(IIII)V", NJ_ACC_PUBLIC),
    M("getCropX",            "()I", NJ_ACC_PUBLIC),
    M("getCropY",            "()I", NJ_ACC_PUBLIC),
    M("getCropWidth",        "()I", NJ_ACC_PUBLIC),
    M("getCropHeight",       "()I", NJ_ACC_PUBLIC),
    M("<init>", "()V", NJ_ACC_PUBLIC),
    M("getColor", "()I", NJ_ACC_PUBLIC),
    M("getImageModeX", "()I", NJ_ACC_PUBLIC),
    M("getImageModeY", "()I", NJ_ACC_PUBLIC),
};
static const nj_builtin_method g3d_methods[] = {
    M("<init>",        "()V",                        NJ_ACC_PUBLIC),
    M("getInstance",   "()L" NJ_C_G3D ";",           NJ_ACC_PUBLIC | NJ_ACC_STATIC),
    M("bindTarget",    "(Ljava/lang/Object;)V",      NJ_ACC_PUBLIC),
    M("bindTarget",    "(Ljava/lang/Object;ZI)V",    NJ_ACC_PUBLIC),
    M("releaseTarget", "()V",                        NJ_ACC_PUBLIC),
    M("setViewport",   "(IIII)V",                    NJ_ACC_PUBLIC),
    M("setCamera",     "(L" NJ_C_CAMERA ";L" NJ_C_XFORM ";)V", NJ_ACC_PUBLIC),
    M("clear",         "(L" NJ_C_BG ";)V",           NJ_ACC_PUBLIC),
    M("render",        "(L" NJ_C_WORLD ";)V",        NJ_ACC_PUBLIC),
    M("render",        "(L" NJ_C_NODE ";L" NJ_C_XFORM ";)V", NJ_ACC_PUBLIC),
    M("resetLights",   "()V",                        NJ_ACC_PUBLIC),
    M("addLight",      "(L" NJ_C_LIGHT ";L" NJ_C_XFORM ";)I", NJ_ACC_PUBLIC),
    M("getTarget", "()Ljava/lang/Object;", NJ_ACC_PUBLIC),
    M("getHints", "()I", NJ_ACC_PUBLIC),
    M("isDepthBufferEnabled", "()Z", NJ_ACC_PUBLIC),
    M("getViewportX", "()I", NJ_ACC_PUBLIC),
    M("getViewportY", "()I", NJ_ACC_PUBLIC),
    M("getViewportWidth", "()I", NJ_ACC_PUBLIC),
    M("getViewportHeight", "()I", NJ_ACC_PUBLIC),
    M("setDepthRange", "(FF)V", NJ_ACC_PUBLIC),
    M("getDepthRangeNear", "()F", NJ_ACC_PUBLIC),
    M("getDepthRangeFar", "()F", NJ_ACC_PUBLIC),
    M("render", "(Ljavax/microedition/m3g/VertexBuffer;Ljavax/microedition/m3g/IndexBuffer;Ljavax/microedition/m3g/Appearance;Ljavax/microedition/m3g/Transform;I)V", NJ_ACC_PUBLIC),
    M("render", "(Ljavax/microedition/m3g/VertexBuffer;Ljavax/microedition/m3g/IndexBuffer;Ljavax/microedition/m3g/Appearance;Ljavax/microedition/m3g/Transform;)V", NJ_ACC_PUBLIC),
    M("getCamera", "(Ljavax/microedition/m3g/Transform;)Ljavax/microedition/m3g/Camera;", NJ_ACC_PUBLIC),
    M("setLight", "(ILjavax/microedition/m3g/Light;Ljavax/microedition/m3g/Transform;)V", NJ_ACC_PUBLIC),
    M("getLightCount", "()I", NJ_ACC_PUBLIC),
    M("getLight", "(ILjavax/microedition/m3g/Transform;)Ljavax/microedition/m3g/Light;", NJ_ACC_PUBLIC),
    /* Static: it describes the implementation, not an instance. */
    M("getProperties", "()Ljava/util/Hashtable;", NJ_ACC_PUBLIC | NJ_ACC_STATIC),
};
static const nj_builtin_method loader_methods[] = {
    M("load", "(Ljava/lang/String;)[L" NJ_C_O3D ";", NJ_ACC_PUBLIC | NJ_ACC_STATIC),
    M("load", "([BI)[L" NJ_C_O3D ";", NJ_ACC_PUBLIC | NJ_ACC_STATIC)
};
static const nj_builtin_method appear_methods[] = {
    M("setMaterial",        "(L" NJ_C_MAT ";)" "V",     NJ_ACC_PUBLIC),
    M("setPolygonMode",     "(L" NJ_C_POLY ";)V",       NJ_ACC_PUBLIC),
    M("getPolygonMode",     "()L" NJ_C_POLY ";",        NJ_ACC_PUBLIC),
    M("setCompositingMode", "(L" NJ_C_COMP ";)V",       NJ_ACC_PUBLIC),
    M("getCompositingMode", "()L" NJ_C_COMP ";",        NJ_ACC_PUBLIC),
    M("setTexture",         "(IL" NJ_C_TEX2D ";)V",     NJ_ACC_PUBLIC),
    M("getTexture",         "(I)L" NJ_C_TEX2D ";",      NJ_ACC_PUBLIC),
    M("setLayer",           "(I)V",                     NJ_ACC_PUBLIC),
    M("<init>", "()V", NJ_ACC_PUBLIC),
    M("getLayer", "()I", NJ_ACC_PUBLIC),
    M("setFog", "(Ljavax/microedition/m3g/Fog;)V", NJ_ACC_PUBLIC),
    M("getFog", "()Ljavax/microedition/m3g/Fog;", NJ_ACC_PUBLIC),
    M("getMaterial", "()Ljavax/microedition/m3g/Material;", NJ_ACC_PUBLIC),
};
/*
 * VertexBuffer's default colour, from 急速公路狂飙－新年版_84.  VertexBuffer had
 * no fields and no methods of its own, so both are new; a colour with no getter
 * is a colour the caller cannot check, and both directions are here.
 */
NJFN(m3g_vbuf_setDefaultColor)
{
    nj_iput(vm, NJ_THIS(args),
            nj_field_lookup(vm, NJ_C_VBUF, "_nj_defColor", "I", 0),
            NJ_ARG_INT(args, 0));
    return NJ_OK;
}

NJFN(m3g_vbuf_getDefaultColor)
{
    ret->i = nj_field_read_int(vm, NJ_THIS(args),
                               nj_field_lookup(vm, NJ_C_VBUF, "_nj_defColor",
                                               "I", 0));
    return NJ_OK;
}

NJFN(m3g_vbuf_setPositions)
{
    nj_ref self = NJ_THIS(args);

    nj_rput(vm, self, nj_field_lookup(vm, NJ_C_VBUF, "_nj_positions",
                                      "L" NJ_C_VARR ";", 0), NJ_ARG_REF(args, 0));
    nj_iput(vm, self, nj_field_lookup(vm, NJ_C_VBUF, "_nj_bias", "F", 0),
            f2bits(NJ_ARG_FLOAT(args, 1)));
    nj_rput(vm, self, nj_field_lookup(vm, NJ_C_VBUF, "_nj_scale", "[F", 0),
            NJ_ARG_REF(args, 2));
    return NJ_OK;
}

NJFN(m3g_vbuf_setColors)
{
    nj_rput(vm, NJ_THIS(args),
            nj_field_lookup(vm, NJ_C_VBUF, "_nj_colors",
                            "L" NJ_C_VARR ";", 0), NJ_ARG_REF(args, 0));
    return NJ_OK;
}

/* The float[]+float[] form of the same call, which is the one
 * 急速公路狂飙－新年版_84 actually uses.  Same three things are kept. */
NJFN(m3g_vbuf_setTexCoords_ff)
{
    nj_ref self = NJ_THIS(args);

    nj_iput(vm, self, nj_field_lookup(vm, NJ_C_VBUF, "_nj_texUnit", "I", 0),
            NJ_ARG_INT(args, 0));
    nj_rput(vm, self, nj_field_lookup(vm, NJ_C_VBUF, "_nj_tex0",
                                      "L" NJ_C_VARR ";", 0), NJ_ARG_REF(args, 1));
    nj_iput(vm, self, nj_field_lookup(vm, NJ_C_VBUF, "_nj_texScale", "F", 0),
            f2bits(NJ_ARG_FLOAT(args, 2)));
    nj_rput(vm, self, nj_field_lookup(vm, NJ_C_VBUF, "_nj_texBias", "[F", 0),
            NJ_ARG_REF(args, 3));
    return NJ_OK;
}

NJFN(m3g_vbuf_setTexCoords)
{
    nj_ref self = NJ_THIS(args);

    nj_rput(vm, self, nj_field_lookup(vm, NJ_C_VBUF, "_nj_tex0",
                                      "L" NJ_C_VARR ";", 0), NJ_ARG_REF(args, 1));
    nj_iput(vm, self, nj_field_lookup(vm, NJ_C_VBUF, "_nj_texOff", "I", 0),
            NJ_ARG_INT(args, 2));
    return NJ_OK;
}

static const nj_builtin_field vbuf_fields[] = {
    { "_nj_defColor",  "I",              NJ_ACC_PUBLIC },
    /* The four arrays a Mesh is built from, kept together with the offsets
     * they were given.  Stored, not consumed: this module still does not
     * rasterise, and dropping them would mean step two has nothing to read. */
    { "_nj_positions", "L" NJ_C_VARR ";", NJ_ACC_PUBLIC },
    { "_nj_bias",      "F",              NJ_ACC_PUBLIC },
    { "_nj_scale",     "[F",             NJ_ACC_PUBLIC },
    { "_nj_colors",    "L" NJ_C_VARR ";", NJ_ACC_PUBLIC },
    { "_nj_tex0",      "L" NJ_C_VARR ";", NJ_ACC_PUBLIC },
    { "_nj_texBias",   "[F",             NJ_ACC_PUBLIC },
    { "_nj_texScale",  "F",              NJ_ACC_PUBLIC },
    { "_nj_texOff",    "I",              NJ_ACC_PUBLIC },
    { "_nj_texUnit",   "I",              NJ_ACC_PUBLIC },
    { "_nj_normals", "L" NJ_C_VARR ";", NJ_ACC_PUBLIC },
};
static const nj_builtin_method vbuf_methods[] = {
    M("setDefaultColor", "(I)V", NJ_ACC_PUBLIC),
    M("getDefaultColor", "()I",  NJ_ACC_PUBLIC),
    M("setPositions",    "(L" NJ_C_VARR ";F[F)V", NJ_ACC_PUBLIC),
    M("setColors",       "(L" NJ_C_VARR ";)V",    NJ_ACC_PUBLIC),
    M("setTexCoords",    "(IL" NJ_C_VARR ";I)V",  NJ_ACC_PUBLIC),
    M("setTexCoords",    "(IL" NJ_C_VARR ";F[F)V", NJ_ACC_PUBLIC),
    M("<init>", "()V", NJ_ACC_PUBLIC),
    M("getVertexCount", "()I", NJ_ACC_PUBLIC),
    M("setNormals", "(Ljavax/microedition/m3g/VertexArray;)V", NJ_ACC_PUBLIC),
    M("getPositions", "([F)Ljavax/microedition/m3g/VertexArray;", NJ_ACC_PUBLIC),
    M("getTexCoords", "(I[F)Ljavax/microedition/m3g/VertexArray;", NJ_ACC_PUBLIC),
    M("getNormals", "()Ljavax/microedition/m3g/VertexArray;", NJ_ACC_PUBLIC),
    M("getColors", "()Ljavax/microedition/m3g/VertexArray;", NJ_ACC_PUBLIC),
};

static const nj_builtin_method varr_methods[] = {
    M("<init>",             "(III)V", NJ_ACC_PUBLIC),
    M("getVertexCount",     "()I",    NJ_ACC_PUBLIC),
    M("getComponentCount",  "()I",    NJ_ACC_PUBLIC),
    M("getComponentSize",   "()I",    NJ_ACC_PUBLIC),
    M("set",                "(II[S)V", NJ_ACC_PUBLIC),
    M("set", "(II[B)V", NJ_ACC_PUBLIC),
    M("getComponentType", "()I", NJ_ACC_PUBLIC),
    M("get", "(II[S)V", NJ_ACC_PUBLIC),
    M("get", "(II[B)V", NJ_ACC_PUBLIC),
};
static const nj_builtin_method poly_methods[] = {
    M("setWinding",                   "(I)V", NJ_ACC_PUBLIC),
    M("setPerspectiveCorrectionEnable", "(Z)V", NJ_ACC_PUBLIC),
    M("setCulling",                   "(I)V", NJ_ACC_PUBLIC),
    M("getCulling",                   "()I",  NJ_ACC_PUBLIC),
    M("setShading",                   "(I)V", NJ_ACC_PUBLIC),
    M("getShading",                   "()I",  NJ_ACC_PUBLIC),
    M("setTwoSidedLightingEnable",    "(Z)V", NJ_ACC_PUBLIC),
    M("isTwoSidedLightingEnabled",    "()Z",  NJ_ACC_PUBLIC),
    M("<init>", "()V", NJ_ACC_PUBLIC),
    M("getWinding", "()I", NJ_ACC_PUBLIC),
    M("setLocalCameraLightingEnable", "(Z)V", NJ_ACC_PUBLIC),
    M("isLocalCameraLightingEnabled", "()Z", NJ_ACC_PUBLIC),
    M("isPerspectiveCorrectionEnabled", "()Z", NJ_ACC_PUBLIC),
};
static const nj_builtin_method comp_methods[] = {
    M("setBlending",        "(I)V", NJ_ACC_PUBLIC),
    M("getBlending",        "()I",  NJ_ACC_PUBLIC),
    M("setAlphaThreshold",  "(F)V", NJ_ACC_PUBLIC),
    M("setDepthTestEnable", "(Z)V", NJ_ACC_PUBLIC),
    M("setDepthWriteEnable","(Z)V", NJ_ACC_PUBLIC),
    M("<init>", "()V", NJ_ACC_PUBLIC),
    M("getAlphaThreshold", "()F", NJ_ACC_PUBLIC),
    M("setAlphaWriteEnable", "(Z)V", NJ_ACC_PUBLIC),
    M("isAlphaWriteEnabled", "()Z", NJ_ACC_PUBLIC),
    M("setColorWriteEnable", "(Z)V", NJ_ACC_PUBLIC),
    M("isColorWriteEnabled", "()Z", NJ_ACC_PUBLIC),
    M("isDepthWriteEnabled", "()Z", NJ_ACC_PUBLIC),
    M("isDepthTestEnabled", "()Z", NJ_ACC_PUBLIC),
    M("setDepthOffset", "(FF)V", NJ_ACC_PUBLIC),
    M("getDepthOffsetFactor", "()F", NJ_ACC_PUBLIC),
    M("getDepthOffsetUnits", "()F", NJ_ACC_PUBLIC),
};
static const nj_builtin_method tex2d_methods[] = {
    M("<init>",      "(L" NJ_C_IMAGE2D ";)V", NJ_ACC_PUBLIC),
    M("setImage",    "(L" NJ_C_IMAGE2D ";)V", NJ_ACC_PUBLIC),
    M("setBlending", "(I)V",                  NJ_ACC_PUBLIC),
    M("setWrapping", "(II)V",                 NJ_ACC_PUBLIC),
    M("setFiltering", "(II)V",                NJ_ACC_PUBLIC),
    M("getFiltering", "()I",                  NJ_ACC_PUBLIC),
    M("getImage",     "()L" NJ_C_IMAGE2D ";",  NJ_ACC_PUBLIC),
    M("getLevelFilter", "()I", NJ_ACC_PUBLIC),
    M("getImageFilter", "()I", NJ_ACC_PUBLIC),
    M("getWrappingS", "()I", NJ_ACC_PUBLIC),
    M("getWrappingT", "()I", NJ_ACC_PUBLIC),
    M("getBlending", "()I", NJ_ACC_PUBLIC),
    M("setBlendColor", "(I)V", NJ_ACC_PUBLIC),
    M("getBlendColor", "()I", NJ_ACC_PUBLIC),
};
static const nj_builtin_method sprite_methods[] = {
    M("<init>",        "(ZL" NJ_C_IMAGE2D ";L" NJ_C_APPEAR ";)V", NJ_ACC_PUBLIC),
    M("setImage",      "(L" NJ_C_IMAGE2D ";)V", NJ_ACC_PUBLIC),
    M("setAppearance", "(L" NJ_C_APPEAR ";)V",  NJ_ACC_PUBLIC),
    M("isScaled", "()Z", NJ_ACC_PUBLIC),
    M("getAppearance", "()Ljavax/microedition/m3g/Appearance;", NJ_ACC_PUBLIC),
    M("getImage", "()Ljavax/microedition/m3g/Image2D;", NJ_ACC_PUBLIC),
    M("setCrop", "(IIII)V", NJ_ACC_PUBLIC),
    M("getCropX", "()I", NJ_ACC_PUBLIC),
    M("getCropY", "()I", NJ_ACC_PUBLIC),
    M("getCropWidth", "()I", NJ_ACC_PUBLIC),
    M("getCropHeight", "()I", NJ_ACC_PUBLIC),
};
static const nj_builtin_method mesh_methods[] = {
    M("<init>",           "(L" NJ_C_VBUF ";L" NJ_C_IBUF ";L" NJ_C_APPEAR ";)V",
                          NJ_ACC_PUBLIC),
    M("<init>",           "(L" NJ_C_VBUF ";[L" NJ_C_IBUF ";[L" NJ_C_APPEAR ";)V",
                          NJ_ACC_PUBLIC),
    M("getVertexBuffer",  "()L" NJ_C_VBUF ";",        NJ_ACC_PUBLIC),
    M("setVertexBuffer",  "(L" NJ_C_VBUF ";)V",       NJ_ACC_PUBLIC),
    M("getIndexBuffer",   "(I)L" NJ_C_IBUF ";",       NJ_ACC_PUBLIC),
    M("getSubmeshCount",  "()I",                      NJ_ACC_PUBLIC),
    M("setAppearance",    "(IL" NJ_C_APPEAR ";)V",    NJ_ACC_PUBLIC),
    M("getAppearance",    "(I)L" NJ_C_APPEAR ";",     NJ_ACC_PUBLIC)
};
static const nj_builtin_method img2d_methods[] = {
    M("<init>", "(III)V",     NJ_ACC_PUBLIC),
    M("<init>", "(II[B)V",    NJ_ACC_PUBLIC),
    /*
     * JSR 184's other form: the image is an object that may be either a
     * javax.microedition.lcdui.Image or a byte[].  3D激爆拳击 builds its
     * textures this way and could not start without it.
     */
    M("<init>", "(ILjava/lang/Object;)V", NJ_ACC_PUBLIC),
    M("set",    "(IIII[B)V",  NJ_ACC_PUBLIC),
    M("getFormat",   "()I",   NJ_ACC_PUBLIC),
    M("getWidth",    "()I",   NJ_ACC_PUBLIC),
    M("getHeight",   "()I",   NJ_ACC_PUBLIC),
    M("<init>", "(III[B)V", NJ_ACC_PUBLIC),
    M("<init>", "(III[B[B)V", NJ_ACC_PUBLIC),
    M("isMutable", "()Z", NJ_ACC_PUBLIC),
};

#define M3G_N(arr) ((nj_u2)(sizeof(arr) / sizeof((arr)[0])))

/*
 * ------------------------------------------------------------------
 * The rest of the package: the holders a title builds by hand
 * ------------------------------------------------------------------
 *
 * A note above the class table used to say that none of the collected titles
 * calls a method on Material, Fog, KeyframeSequence and the rest, which was
 * true when it was written and stopped being true as soon as a title built
 * one by hand: a MIDlet that writes `new Material()` got NoSuchMethodError
 * out of its constructor.  The classes below are the ones a title can
 * construct or animate with; each stores what it is given and hands it back.
 *
 * The storage rule is the file's own: a float lives in a field typed "F" and
 * moves through nj_iget/nj_iput, which carry its four bytes; a reference
 * lives in a field of its own type.  Nothing here rasterises.
 */
static nj_field *m3g_fof(nj_vm *vm, const char *cls, const char *name,
                         const char *desc)
{
    nj_field *f = nj_field_lookup(vm, cls, name, desc, 0);

    if (!f) {
        /* Name it: "a field this native needs" is true and useless, and it
         * is what made 浴火银河2's InternalError take a rebuild to place. */
        char msg[176];

        snprintf(msg, sizeof msg, "m3g: %s has no field %s of type %s",
                 cls, name, desc);
        nj_throw(vm, "java/lang/InternalError", msg);
    }
    return f;
}

#define M3G_I_GET(fn, cls, fld)                                          \
    NJFN(fn)                                                             \
    {                                                                    \
        nj_field *f = m3g_fof(vm, cls, fld, "I");                        \
        ret->i = f ? nj_iget(vm, NJ_THIS(args), f) : 0;                  \
        return NJ_OK;                                                    \
    }
#define M3G_I_SET(fn, cls, fld)                                          \
    NJFN(fn)                                                             \
    {                                                                    \
        nj_field *f = m3g_fof(vm, cls, fld, "I");                        \
        if (f) {                                                         \
            nj_iput(vm, NJ_THIS(args), f, NJ_ARG_INT(args, 0));          \
        }                                                                \
        return NJ_OK;                                                    \
    }
#define M3G_Z_GET(fn, cls, fld)                                          \
    NJFN(fn)                                                             \
    {                                                                    \
        nj_field *f = m3g_fof(vm, cls, fld, "Z");                        \
        ret->i = f ? (nj_iget(vm, NJ_THIS(args), f) ? 1 : 0) : 0;        \
        return NJ_OK;                                                    \
    }
#define M3G_Z_SET(fn, cls, fld)                                          \
    NJFN(fn)                                                             \
    {                                                                    \
        nj_field *f = m3g_fof(vm, cls, fld, "Z");                        \
        if (f) {                                                         \
            nj_iput(vm, NJ_THIS(args), f, NJ_ARG_INT(args, 0) ? 1 : 0);  \
        }                                                                \
        return NJ_OK;                                                    \
    }
#define M3G_F_GET(fn, cls, fld)                                          \
    NJFN(fn)                                                             \
    {                                                                    \
        nj_field *f = m3g_fof(vm, cls, fld, "F");                        \
        ret->f = f ? bits2f(nj_iget(vm, NJ_THIS(args), f)) : 0.0f;       \
        return NJ_OK;                                                    \
    }
#define M3G_F_SET(fn, cls, fld)                                          \
    NJFN(fn)                                                             \
    {                                                                    \
        nj_field *f = m3g_fof(vm, cls, fld, "F");                        \
        if (f) {                                                         \
            nj_iput(vm, NJ_THIS(args), f, f2bits(NJ_ARG_FLOAT(args, 0)));\
        }                                                                \
        return NJ_OK;                                                    \
    }
#define M3G_R_GET(fn, cls, fld, desc)                                    \
    NJFN(fn)                                                             \
    {                                                                    \
        nj_field *f = m3g_fof(vm, cls, fld, desc);                       \
        ret->r = f ? nj_rget(vm, NJ_THIS(args), f) : 0;                  \
        return NJ_OK;                                                    \
    }
#define M3G_R_SET(fn, cls, fld, desc)                                    \
    NJFN(fn)                                                             \
    {                                                                    \
        nj_field *f = m3g_fof(vm, cls, fld, desc);                       \
        if (f) {                                                         \
            nj_rput(vm, NJ_THIS(args), f, NJ_ARG_REF(args, 0));          \
        }                                                                \
        return NJ_OK;                                                    \
    }

/* Copy a java float[] into another, as short as the shorter one. */
static nj_u4 m3g_copy_floats(nj_ref dst, nj_ref src)
{
    float *d, *s;
    nj_u4 n, i;

    if (!dst || !src) {
        return 0u;
    }
    n = (nj_u4)nj_array_length(src);
    if (n > (nj_u4)nj_array_length(dst)) {
        n = (nj_u4)nj_array_length(dst);
    }
    d = (float *)nj_array_data(dst);
    s = (float *)nj_array_data(src);
    if (!d || !s) {
        return 0u;
    }
    for (i = 0u; i < n; i++) {
        d[i] = s[i];
    }
    return n;
}

/* ------------------------------------------------------------------ */
/* Material                                                            */
/* ------------------------------------------------------------------ */
static const nj_builtin_field mat_fields[] = {
    { "_nj_c_amb",     "I", 0 },
    { "_nj_c_dif",     "I", 0 },
    { "_nj_c_emi",     "I", 0 },
    { "_nj_c_spec",    "I", 0 },
    { "_nj_shininess", "F", 0 },
    { "_nj_vcolor",    "Z", 0 },
    { "AMBIENT",  "I", NJ_ACC_PUBLIC | NJ_ACC_STATIC | NJ_ACC_FINAL },
    { "DIFFUSE",  "I", NJ_ACC_PUBLIC | NJ_ACC_STATIC | NJ_ACC_FINAL },
    { "EMISSIVE", "I", NJ_ACC_PUBLIC | NJ_ACC_STATIC | NJ_ACC_FINAL },
    { "SPECULAR", "I", NJ_ACC_PUBLIC | NJ_ACC_STATIC | NJ_ACC_FINAL }
};

static const nj_builtin_method mat_methods[] = {
    M("<init>",                       "()V", NJ_ACC_PUBLIC),
    M("setColor",                     "(II)V", NJ_ACC_PUBLIC),
    M("getColor",                     "(I)I", NJ_ACC_PUBLIC),
    M("setShininess",                 "(F)V", NJ_ACC_PUBLIC),
    M("getShininess",                 "()F", NJ_ACC_PUBLIC),
    M("setVertexColorTrackingEnable", "(Z)V", NJ_ACC_PUBLIC),
    M("isVertexColorTrackingEnabled", "()Z", NJ_ACC_PUBLIC)
};

/*
 * The four colour targets are the JSR's own numbers: 1024 AMBIENT, 2048
 * DIFFUSE, 4096 EMISSIVE, 8192 SPECULAR.  A title that passes anything else
 * gets an exception naming them, not a silent write into the wrong slot.
 */
static const char *mat_colour_field(nj_vm *vm, nj_s4 target)
{
    switch (target) {
    case 1024: return "_nj_c_amb";
    case 2048: return "_nj_c_dif";
    case 4096: return "_nj_c_emi";
    case 8192: return "_nj_c_spec";
    default:
        /* nj_throw, not NJ_THROW: this helper answers a field name, so the
           macro's `return` would hand back an nj_status as a pointer. */
        (void)nj_throw(vm, "java/lang/IllegalArgumentException",
                       "colour target is AMBIENT (1024), DIFFUSE (2048), "
                       "EMISSIVE (4096) or SPECULAR (8192)");
        return NULL;
    }
}

NJFN(m3g_mat_init)
{
    nj_ref self = NJ_THIS(args);

    nj_iput(vm, self, m3g_fof(vm, NJ_C_MAT, "_nj_c_amb", "I"), (nj_s4)0x3F800000);
    nj_iput(vm, self, m3g_fof(vm, NJ_C_MAT, "_nj_c_dif", "I"), (nj_s4)0xCCCCCCCC);
    nj_iput(vm, self, m3g_fof(vm, NJ_C_MAT, "_nj_c_emi", "I"), 0);
    nj_iput(vm, self, m3g_fof(vm, NJ_C_MAT, "_nj_c_spec", "I"), 0);
    nj_iput(vm, self, m3g_fof(vm, NJ_C_MAT, "_nj_shininess", "F"), 0);
    nj_iput(vm, self, m3g_fof(vm, NJ_C_MAT, "_nj_vcolor", "Z"), 0);
    return NJ_OK;
}

NJFN(m3g_mat_setColor)
{
    const char *fld = mat_colour_field(vm, NJ_ARG_INT(args, 0));

    if (!fld) {
        return NJ_ERR;
    }
    nj_iput(vm, NJ_THIS(args), m3g_fof(vm, NJ_C_MAT, fld, "I"),
            NJ_ARG_INT(args, 1));
    return NJ_OK;
}

NJFN(m3g_mat_getColor)
{
    const char *fld = mat_colour_field(vm, NJ_ARG_INT(args, 0));

    if (!fld) {
        return NJ_ERR;
    }
    ret->i = nj_iget(vm, NJ_THIS(args), m3g_fof(vm, NJ_C_MAT, fld, "I"));
    return NJ_OK;
}

M3G_F_SET(m3g_mat_setShininess, NJ_C_MAT, "_nj_shininess")
M3G_F_GET(m3g_mat_getShininess, NJ_C_MAT, "_nj_shininess")
M3G_Z_SET(m3g_mat_setTrackColor, NJ_C_MAT, "_nj_vcolor")
M3G_Z_GET(m3g_mat_isTrackColor, NJ_C_MAT, "_nj_vcolor")

/* ------------------------------------------------------------------ */
/* Fog                                                                 */
/* ------------------------------------------------------------------ */
static const nj_builtin_field fog_fields[] = {
    { "_nj_mode",    "I", 0 },
    { "_nj_color",   "I", 0 },
    { "_nj_density", "F", 0 },
    { "_nj_near",    "F", 0 },
    { "_nj_far",     "F", 0 },
    { "EXPONENTIAL", "I", NJ_ACC_PUBLIC | NJ_ACC_STATIC | NJ_ACC_FINAL },
    { "LINEAR",      "I", NJ_ACC_PUBLIC | NJ_ACC_STATIC | NJ_ACC_FINAL }
};

static const nj_builtin_method fog_methods[] = {
    M("<init>",          "()V",  NJ_ACC_PUBLIC),
    M("setMode",         "(I)V", NJ_ACC_PUBLIC),
    M("getMode",         "()I",  NJ_ACC_PUBLIC),
    M("setColor",        "(I)V", NJ_ACC_PUBLIC),
    M("getColor",        "()I",  NJ_ACC_PUBLIC),
    M("setDensity",      "(F)V", NJ_ACC_PUBLIC),
    M("getDensity",      "()F",  NJ_ACC_PUBLIC),
    M("setLinear",       "(FF)V", NJ_ACC_PUBLIC),
    M("getNearDistance", "()F",  NJ_ACC_PUBLIC),
    M("getFarDistance",  "()F",  NJ_ACC_PUBLIC)
};

NJFN(m3g_fog_init)
{
    nj_ref self = NJ_THIS(args);

    nj_iput(vm, self, m3g_fof(vm, NJ_C_FOG, "_nj_mode", "I"), 80);
    nj_iput(vm, self, m3g_fof(vm, NJ_C_FOG, "_nj_color", "I"), 0);
    nj_iput(vm, self, m3g_fof(vm, NJ_C_FOG, "_nj_density", "F"), f2bits(1.0f));
    nj_iput(vm, self, m3g_fof(vm, NJ_C_FOG, "_nj_near", "F"), f2bits(0.0f));
    nj_iput(vm, self, m3g_fof(vm, NJ_C_FOG, "_nj_far", "F"), f2bits(1.0f));
    return NJ_OK;
}

NJFN(m3g_fog_setMode)
{
    nj_s4 mode = NJ_ARG_INT(args, 0);

    if (mode != 80 && mode != 81) {
        NJ_THROW(vm, "java/lang/IllegalArgumentException",
                 "fog mode is EXPONENTIAL (80) or LINEAR (81)");
        return NJ_ERR;
    }
    nj_iput(vm, NJ_THIS(args), m3g_fof(vm, NJ_C_FOG, "_nj_mode", "I"), mode);
    return NJ_OK;
}

NJFN(m3g_fog_setLinear)
{
    nj_ref self = NJ_THIS(args);

    nj_iput(vm, self, m3g_fof(vm, NJ_C_FOG, "_nj_near", "F"),
            f2bits(NJ_ARG_FLOAT(args, 0)));
    nj_iput(vm, self, m3g_fof(vm, NJ_C_FOG, "_nj_far", "F"),
            f2bits(NJ_ARG_FLOAT(args, 1)));
    return NJ_OK;
}

M3G_I_GET(m3g_fog_getMode,    NJ_C_FOG, "_nj_mode")
M3G_I_SET(m3g_fog_setColor,   NJ_C_FOG, "_nj_color")
M3G_I_GET(m3g_fog_getColor,   NJ_C_FOG, "_nj_color")
M3G_F_SET(m3g_fog_setDensity, NJ_C_FOG, "_nj_density")
M3G_F_GET(m3g_fog_getDensity, NJ_C_FOG, "_nj_density")
M3G_F_GET(m3g_fog_getNear,    NJ_C_FOG, "_nj_near")
M3G_F_GET(m3g_fog_getFar,     NJ_C_FOG, "_nj_far")

/* ------------------------------------------------------------------ */
/* IndexBuffer                                                         */
/* ------------------------------------------------------------------ */
/*
 * An IndexBuffer is abstract in the API: the class that is really built is
 * TriangleStripArray, which keeps its strip lengths and fills these two
 * fields.  They live here because getIndexCount() and getIndices() are
 * declared here and a subclass inherits both the method and the storage.
 */
static const nj_builtin_field ibuf_fields[] = {
    { "_nj_ib_count", "I",  0 },
    { "_nj_ib_data",  "[I", 0 }
};

static const nj_builtin_method ibuf_methods[] = {
    M("getIndexCount", "()I",  NJ_ACC_PUBLIC),
    M("getIndices",    "([I)V", NJ_ACC_PUBLIC)
};

NJFN(m3g_ibuf_getIndexCount)
{
    ret->i = nj_iget(vm, NJ_THIS(args),
                     m3g_fof(vm, NJ_C_IBUF, "_nj_ib_count", "I"));
    return NJ_OK;
}

NJFN(m3g_ibuf_getIndices)
{
    nj_ref dst = NJ_ARG_REF(args, 0);
    nj_ref data = nj_rget(vm, NJ_THIS(args),
                          m3g_fof(vm, NJ_C_IBUF, "_nj_ib_data", "[I"));
    nj_u4 n;

    if (!dst || !data) {
        return NJ_OK;
    }
    n = (nj_u4)nj_array_length(data);
    if (n > (nj_u4)nj_array_length(dst)) {
        n = (nj_u4)nj_array_length(dst);
    }
    {
        nj_u4 i;

        for (i = 0u; i < n; i++) {
            nj_array_set_int(dst, i, nj_array_get_int(data, i));
        }
    }
    return NJ_OK;
}

/* ------------------------------------------------------------------ */
/* KeyframeSequence                                                    */
/* ------------------------------------------------------------------ */
static const nj_builtin_field kfs_fields[] = {
    { "_nj_ncomp",  "I",  0 },
    { "_nj_nkeys",  "I",  0 },
    { "_nj_interp", "I",  0 },
    { "_nj_dur",    "I",  0 },
    { "_nj_repeat", "I",  0 },
    { "_nj_vfirst", "I",  0 },
    { "_nj_vlast",  "I",  0 },
    { "_nj_times",  "[I", 0 },
    { "_nj_keys",   "[F", 0 },
    { "STEP",     "I", NJ_ACC_PUBLIC | NJ_ACC_STATIC | NJ_ACC_FINAL },
    { "LINEAR",   "I", NJ_ACC_PUBLIC | NJ_ACC_STATIC | NJ_ACC_FINAL },
    { "SPLINE",   "I", NJ_ACC_PUBLIC | NJ_ACC_STATIC | NJ_ACC_FINAL },
    { "SQUAD",    "I", NJ_ACC_PUBLIC | NJ_ACC_STATIC | NJ_ACC_FINAL },
    { "SLERP",    "I", NJ_ACC_PUBLIC | NJ_ACC_STATIC | NJ_ACC_FINAL },
    { "CONSTANT", "I", NJ_ACC_PUBLIC | NJ_ACC_STATIC | NJ_ACC_FINAL },
    { "LOOP",     "I", NJ_ACC_PUBLIC | NJ_ACC_STATIC | NJ_ACC_FINAL }
};

static const nj_builtin_method kfs_methods[] = {
    M("<init>",               "(III)V",  NJ_ACC_PUBLIC),
    M("getComponentCount",    "()I",     NJ_ACC_PUBLIC),
    M("getKeyframeCount",     "()I",     NJ_ACC_PUBLIC),
    M("getInterpolationType", "()I",     NJ_ACC_PUBLIC),
    M("setKeyframe",          "(II[F)V", NJ_ACC_PUBLIC),
    M("getKeyframe",          "(I[F)I",  NJ_ACC_PUBLIC),
    M("setValidRange",        "(II)V",   NJ_ACC_PUBLIC),
    M("getValidRangeFirst",   "()I",     NJ_ACC_PUBLIC),
    M("getValidRangeLast",    "()I",     NJ_ACC_PUBLIC),
    M("setDuration",          "(I)V",    NJ_ACC_PUBLIC),
    M("getDuration",          "()I",     NJ_ACC_PUBLIC),
    M("setRepeatMode",        "(I)V",    NJ_ACC_PUBLIC),
    M("getRepeatMode",        "()I",     NJ_ACC_PUBLIC)
};

NJFN(m3g_kfs_init)
{
    nj_ref self = NJ_THIS(args);
    nj_s4 ncomp = NJ_ARG_INT(args, 0);
    nj_s4 nkeys = NJ_ARG_INT(args, 1);
    nj_s4 interp = NJ_ARG_INT(args, 2);
    nj_ref times, keys;

    if (ncomp < 1 || ncomp > 4 || nkeys < 1 ||
        (interp != 176 && interp != 177 && interp != 178 &&
         interp != 179 && interp != 180)) {
        NJ_THROW(vm, "java/lang/IllegalArgumentException",
                 "a sequence is 1..4 components, 1.. keyframes, and one of "
                 "STEP (180), LINEAR (176), SPLINE (178), SQUAD (179), "
                 "SLERP (177)");
        return NJ_ERR;
    }
    times = nj_int_array_new(vm, (nj_u4)nkeys);
    keys = nj_array_new(vm, "F", (nj_u4)(ncomp * nkeys));
    if (!times || !keys) {
        return nj_throw_oom(vm);
    }
    nj_rput(vm, self, m3g_fof(vm, NJ_C_KFS, "_nj_times", "[I"), times);
    nj_rput(vm, self, m3g_fof(vm, NJ_C_KFS, "_nj_keys", "[F"), keys);
    nj_iput(vm, self, m3g_fof(vm, NJ_C_KFS, "_nj_ncomp", "I"), ncomp);
    nj_iput(vm, self, m3g_fof(vm, NJ_C_KFS, "_nj_nkeys", "I"), nkeys);
    nj_iput(vm, self, m3g_fof(vm, NJ_C_KFS, "_nj_interp", "I"), interp);
    nj_iput(vm, self, m3g_fof(vm, NJ_C_KFS, "_nj_repeat", "I"), 192);
    nj_iput(vm, self, m3g_fof(vm, NJ_C_KFS, "_nj_vlast", "I"), nkeys - 1);
    return NJ_OK;
}

NJFN(m3g_kfs_setKeyframe)
{
    nj_ref self = NJ_THIS(args);
    nj_s4 idx = NJ_ARG_INT(args, 0);
    nj_s4 time = NJ_ARG_INT(args, 1);
    nj_ref src = NJ_ARG_REF(args, 2);
    nj_ref keys, times;
    nj_s4 nkeys, ncomp;
    float *k, *s;
    nj_u4 n, i;

    nkeys = nj_iget(vm, self, m3g_fof(vm, NJ_C_KFS, "_nj_nkeys", "I"));
    ncomp = nj_iget(vm, self, m3g_fof(vm, NJ_C_KFS, "_nj_ncomp", "I"));
    if (idx < 0 || idx >= nkeys) {
        NJ_THROW(vm, "java/lang/IndexOutOfBoundsException",
                 "no such keyframe");
        return NJ_ERR;
    }
    if (time < 0) {
        NJ_THROW(vm, "java/lang/IllegalArgumentException",
                 "keyframe time must not be negative");
        return NJ_ERR;
    }
    keys = nj_rget(vm, self, m3g_fof(vm, NJ_C_KFS, "_nj_keys", "[F"));
    times = nj_rget(vm, self, m3g_fof(vm, NJ_C_KFS, "_nj_times", "[I"));
    if (!keys || !times) {
        return NJ_OK;
    }
    nj_array_set_int(times, (nj_u4)idx, time);
    if (!src) {
        return NJ_OK;
    }
    k = (float *)nj_array_data(keys);
    s = (float *)nj_array_data(src);
    if (!k || !s) {
        return NJ_OK;
    }
    n = (nj_u4)ncomp;
    if (n > (nj_u4)nj_array_length(src)) {
        n = (nj_u4)nj_array_length(src);
    }
    for (i = 0u; i < n; i++) {
        k[(nj_u4)idx * (nj_u4)ncomp + i] = s[i];
    }
    return NJ_OK;
}

NJFN(m3g_kfs_getKeyframe)
{
    nj_ref self = NJ_THIS(args);
    nj_s4 idx = NJ_ARG_INT(args, 0);
    nj_ref dst = NJ_ARG_REF(args, 1);
    nj_ref keys, times;
    nj_s4 nkeys, ncomp;
    float *k, *d;
    nj_u4 n, i;

    nkeys = nj_iget(vm, self, m3g_fof(vm, NJ_C_KFS, "_nj_nkeys", "I"));
    ncomp = nj_iget(vm, self, m3g_fof(vm, NJ_C_KFS, "_nj_ncomp", "I"));
    if (idx < 0 || idx >= nkeys) {
        NJ_THROW(vm, "java/lang/IndexOutOfBoundsException",
                 "no such keyframe");
        return NJ_ERR;
    }
    keys = nj_rget(vm, self, m3g_fof(vm, NJ_C_KFS, "_nj_keys", "[F"));
    times = nj_rget(vm, self, m3g_fof(vm, NJ_C_KFS, "_nj_times", "[I"));
    ret->i = times ? nj_array_get_int(times, (nj_u4)idx) : 0;
    if (!keys || !dst) {
        return NJ_OK;
    }
    k = (float *)nj_array_data(keys);
    d = (float *)nj_array_data(dst);
    if (!k || !d) {
        return NJ_OK;
    }
    n = (nj_u4)ncomp;
    if (n > (nj_u4)nj_array_length(dst)) {
        n = (nj_u4)nj_array_length(dst);
    }
    for (i = 0u; i < n; i++) {
        d[i] = k[(nj_u4)idx * (nj_u4)ncomp + i];
    }
    return NJ_OK;
}

NJFN(m3g_kfs_setValidRange)
{
    nj_ref self = NJ_THIS(args);
    nj_s4 first = NJ_ARG_INT(args, 0);
    nj_s4 last = NJ_ARG_INT(args, 1);
    nj_s4 nkeys = nj_iget(vm, self, m3g_fof(vm, NJ_C_KFS, "_nj_nkeys", "I"));

    if (first < 0 || last >= nkeys || first > last) {
        NJ_THROW(vm, "java/lang/IllegalArgumentException",
                 "the valid range must be inside the keyframes, first <= last");
        return NJ_ERR;
    }
    nj_iput(vm, self, m3g_fof(vm, NJ_C_KFS, "_nj_vfirst", "I"), first);
    nj_iput(vm, self, m3g_fof(vm, NJ_C_KFS, "_nj_vlast", "I"), last);
    return NJ_OK;
}

NJFN(m3g_kfs_setDuration)
{
    nj_s4 d = NJ_ARG_INT(args, 0);

    if (d <= 0) {
        NJ_THROW(vm, "java/lang/IllegalArgumentException",
                 "the duration must be positive");
        return NJ_ERR;
    }
    nj_iput(vm, NJ_THIS(args), m3g_fof(vm, NJ_C_KFS, "_nj_dur", "I"), d);
    return NJ_OK;
}

NJFN(m3g_kfs_setRepeatMode)
{
    nj_s4 mode = NJ_ARG_INT(args, 0);

    if (mode != 192 && mode != 193) {
        NJ_THROW(vm, "java/lang/IllegalArgumentException",
                 "repeat mode is CONSTANT (192) or LOOP (193)");
        return NJ_ERR;
    }
    nj_iput(vm, NJ_THIS(args), m3g_fof(vm, NJ_C_KFS, "_nj_repeat", "I"), mode);
    return NJ_OK;
}

M3G_I_GET(m3g_kfs_getComponentCount,    NJ_C_KFS, "_nj_ncomp")
M3G_I_GET(m3g_kfs_getKeyframeCount,     NJ_C_KFS, "_nj_nkeys")
M3G_I_GET(m3g_kfs_getInterpolationType, NJ_C_KFS, "_nj_interp")
M3G_I_GET(m3g_kfs_getValidRangeFirst,   NJ_C_KFS, "_nj_vfirst")
M3G_I_GET(m3g_kfs_getValidRangeLast,    NJ_C_KFS, "_nj_vlast")
M3G_I_GET(m3g_kfs_getDuration,          NJ_C_KFS, "_nj_dur")
M3G_I_GET(m3g_kfs_getRepeatMode,        NJ_C_KFS, "_nj_repeat")

/* ------------------------------------------------------------------ */
/* AnimationController                                                 */
/* ------------------------------------------------------------------ */
static const nj_builtin_field actl_fields[] = {
    { "_nj_ai0",    "I", 0 },
    { "_nj_ai1",    "I", 0 },
    { "_nj_speed",  "F", 0 },
    { "_nj_pos",    "F", 0 },
    { "_nj_weight", "F", 0 }
};

static const nj_builtin_method actl_methods[] = {
    M("<init>",                "()V",  NJ_ACC_PUBLIC),
    M("setActiveInterval",     "(II)V", NJ_ACC_PUBLIC),
    M("getActiveIntervalStart", "()I", NJ_ACC_PUBLIC),
    M("getActiveIntervalEnd",  "()I",  NJ_ACC_PUBLIC),
    M("setSpeed",              "(FI)V", NJ_ACC_PUBLIC),
    M("getSpeed",              "()F",  NJ_ACC_PUBLIC),
    M("setPosition",           "(FI)V", NJ_ACC_PUBLIC),
    M("getPosition",           "(I)F", NJ_ACC_PUBLIC),
    M("getRefWorldTime",       "()I",  NJ_ACC_PUBLIC),
    M("setWeight",             "(F)V", NJ_ACC_PUBLIC),
    M("getWeight",             "()F",  NJ_ACC_PUBLIC)
};

NJFN(m3g_actl_init)
{
    nj_ref self = NJ_THIS(args);

    nj_iput(vm, self, m3g_fof(vm, NJ_C_ACTL, "_nj_ai0", "I"), 0);
    nj_iput(vm, self, m3g_fof(vm, NJ_C_ACTL, "_nj_ai1", "I"), 0);
    nj_iput(vm, self, m3g_fof(vm, NJ_C_ACTL, "_nj_speed", "F"), f2bits(1.0f));
    nj_iput(vm, self, m3g_fof(vm, NJ_C_ACTL, "_nj_pos", "F"), f2bits(0.0f));
    nj_iput(vm, self, m3g_fof(vm, NJ_C_ACTL, "_nj_weight", "F"), f2bits(1.0f));
    return NJ_OK;
}

NJFN(m3g_actl_setActiveInterval)
{
    nj_ref self = NJ_THIS(args);
    nj_s4 first = NJ_ARG_INT(args, 0);
    nj_s4 last = NJ_ARG_INT(args, 1);
    nj_s4 t;

    /* The JSR says the two are swapped when they arrive the wrong way
       round, rather than refused. */
    if (first > last) {
        t = first;
        first = last;
        last = t;
    }
    nj_iput(vm, self, m3g_fof(vm, NJ_C_ACTL, "_nj_ai0", "I"), first);
    nj_iput(vm, self, m3g_fof(vm, NJ_C_ACTL, "_nj_ai1", "I"), last);
    return NJ_OK;
}

NJFN(m3g_actl_getRefWorldTime)
{
    ret->i = (nj_s4)nj_now_ms(vm);
    return NJ_OK;
}

M3G_I_GET(m3g_actl_getActiveIntervalStart, NJ_C_ACTL, "_nj_ai0")
M3G_I_GET(m3g_actl_getActiveIntervalEnd,   NJ_C_ACTL, "_nj_ai1")
M3G_F_SET(m3g_actl_setSpeed,    NJ_C_ACTL, "_nj_speed")
M3G_F_GET(m3g_actl_getSpeed,    NJ_C_ACTL, "_nj_speed")
M3G_F_SET(m3g_actl_setPosition, NJ_C_ACTL, "_nj_pos")
M3G_F_GET(m3g_actl_getPosition, NJ_C_ACTL, "_nj_pos")
M3G_F_SET(m3g_actl_setWeight,   NJ_C_ACTL, "_nj_weight")
M3G_F_GET(m3g_actl_getWeight,   NJ_C_ACTL, "_nj_weight")

/* ------------------------------------------------------------------ */
/* AnimationTrack                                                      */
/* ------------------------------------------------------------------ */
#define M3G_PROP(name, value) { name, "I", NJ_ACC_PUBLIC | NJ_ACC_STATIC | NJ_ACC_FINAL }

static const nj_builtin_field atrack_fields[] = {
    { "_nj_kfs",  "L" NJ_C_KFS ";",  0 },
    { "_nj_ctl",  "L" NJ_C_ACTL ";", 0 },
    { "_nj_prop", "I", 0 },
    M3G_PROP("ALPHA",          256),
    M3G_PROP("AMBIENT_COLOR",  257),
    M3G_PROP("COLOR",          258),
    M3G_PROP("CROP",           259),
    M3G_PROP("DENSITY",        260),
    M3G_PROP("DIFFUSE_COLOR",  261),
    M3G_PROP("EMISSIVE_COLOR", 262),
    M3G_PROP("FAR_DISTANCE",   263),
    M3G_PROP("FIELD_OF_VIEW",  264),
    M3G_PROP("INTENSITY",      265),
    M3G_PROP("MORPH_WEIGHTS",  266),
    M3G_PROP("NEAR_DISTANCE",  267),
    M3G_PROP("ORIENTATION",    268),
    M3G_PROP("PICKABILITY",    269),
    M3G_PROP("SCALE",          270),
    M3G_PROP("SHININESS",      271),
    M3G_PROP("SPECULAR_COLOR", 272),
    M3G_PROP("SPOT_ANGLE",     273),
    M3G_PROP("SPOT_EXPONENT",  274),
    M3G_PROP("TRANSLATION",    275),
    M3G_PROP("VISIBILITY",     276)
};

static const nj_builtin_method atrack_methods[] = {
    M("<init>",             "(L" NJ_C_KFS ";I)V", NJ_ACC_PUBLIC),
    M("setController",      "(L" NJ_C_ACTL ";)V", NJ_ACC_PUBLIC),
    M("getController",      "()L" NJ_C_ACTL ";",  NJ_ACC_PUBLIC),
    M("getKeyframeSequence", "()L" NJ_C_KFS ";",  NJ_ACC_PUBLIC),
    M("getTargetProperty",  "()I",                NJ_ACC_PUBLIC)
};

NJFN(m3g_atrack_init)
{
    nj_ref self = NJ_THIS(args);
    nj_ref seq = NJ_ARG_REF(args, 0);
    nj_s4 prop = NJ_ARG_INT(args, 1);

    if (!seq) {
        NJ_THROW(vm, "java/lang/NullPointerException",
                 "an AnimationTrack needs a KeyframeSequence");
        return NJ_ERR;
    }
    if (prop < 256 || prop > 276) {
        NJ_THROW(vm, "java/lang/IllegalArgumentException",
                 "the target property is one of 256..276");
        return NJ_ERR;
    }
    nj_rput(vm, self, m3g_fof(vm, NJ_C_ATRACK, "_nj_kfs", "L" NJ_C_KFS ";"), seq);
    nj_iput(vm, self, m3g_fof(vm, NJ_C_ATRACK, "_nj_prop", "I"), prop);
    return NJ_OK;
}

M3G_R_SET(m3g_atrack_setController, NJ_C_ATRACK, "_nj_ctl",
          "L" NJ_C_ACTL ";")
M3G_R_GET(m3g_atrack_getController, NJ_C_ATRACK, "_nj_ctl",
          "L" NJ_C_ACTL ";")
M3G_R_GET(m3g_atrack_getSequence, NJ_C_ATRACK, "_nj_kfs",
          "L" NJ_C_KFS ";")
M3G_I_GET(m3g_atrack_getProperty, NJ_C_ATRACK, "_nj_prop")

/* ------------------------------------------------------------------ */
/* RayIntersection                                                     */
/* ------------------------------------------------------------------ */
static const nj_builtin_field ray_fields[] = {
    { "_nj_xnode",   "L" NJ_C_NODE ";", 0 },
    { "_nj_ray",     "[F", 0 },
    { "_nj_dist",    "F",  0 },
    { "_nj_submesh", "I",  0 },
    { "_nj_tex",     "[F", 0 },
    { "_nj_norm",    "[F", 0 }
};

static const nj_builtin_method ray_methods[] = {
    M("<init>",         "()V",                NJ_ACC_PUBLIC),
    M("getIntersected", "()L" NJ_C_NODE ";",  NJ_ACC_PUBLIC),
    M("getRay",         "([F)V",              NJ_ACC_PUBLIC),
    M("getDistance",    "()F",                NJ_ACC_PUBLIC),
    M("getSubmeshIndex", "()I",               NJ_ACC_PUBLIC),
    M("getTextureS",    "(I)F",               NJ_ACC_PUBLIC),
    M("getTextureT",    "(I)F",               NJ_ACC_PUBLIC),
    M("getNormalX",     "()F",                NJ_ACC_PUBLIC),
    M("getNormalY",     "()F",                NJ_ACC_PUBLIC),
    M("getNormalZ",     "()F",                NJ_ACC_PUBLIC)
};

NJFN(m3g_ray_init)
{
    nj_ref self = NJ_THIS(args);
    nj_ref ray = nj_array_new(vm, "F", 6u);
    nj_ref tex = nj_array_new(vm, "F", 4u);
    nj_ref nrm = nj_array_new(vm, "F", 3u);

    if (!ray || !tex || !nrm) {
        return nj_throw_oom(vm);
    }
    nj_rput(vm, self, m3g_fof(vm, NJ_C_RAY, "_nj_ray", "[F"), ray);
    nj_rput(vm, self, m3g_fof(vm, NJ_C_RAY, "_nj_tex", "[F"), tex);
    nj_rput(vm, self, m3g_fof(vm, NJ_C_RAY, "_nj_norm", "[F"), nrm);
    return NJ_OK;
}

NJFN(m3g_ray_getRay)
{
    nj_ref dst = NJ_ARG_REF(args, 0);
    nj_ref ray = nj_rget(vm, NJ_THIS(args),
                         m3g_fof(vm, NJ_C_RAY, "_nj_ray", "[F"));

    (void)m3g_copy_floats(dst, ray);
    return NJ_OK;
}

/* One of the two texture coordinates of texture unit `index`, or of the
   normal, read out of the small float arrays the intersection carries. */
static nj_s4 m3g_ray_component(nj_vm *vm, nj_value *args, const char *fld, int k)
{
    nj_ref a = nj_rget(vm, NJ_THIS(args), m3g_fof(vm, NJ_C_RAY, fld, "[F"));
    float *p;

    if (!a || k >= (nj_s4)nj_array_length(a)) {
        return 0;
    }
    p = (float *)nj_array_data(a);
    return p ? f2bits(p[k]) : 0;
}

NJFN(m3g_ray_getTextureS)
{
    nj_s4 idx = NJ_ARG_INT(args, 0);

    ret->i = m3g_ray_component(vm, args, "_nj_tex", (int)(idx * 2));
    if ((idx * 2) < 0 || (idx * 2) >= 4) {
        NJ_THROW(vm, "java/lang/IndexOutOfBoundsException",
                 "texture unit 0 or 1");
        return NJ_ERR;
    }
    return NJ_OK;
}

NJFN(m3g_ray_getTextureT)
{
    nj_s4 idx = NJ_ARG_INT(args, 0);

    ret->i = m3g_ray_component(vm, args, "_nj_tex", (int)(idx * 2 + 1));
    if ((idx * 2 + 1) < 0 || (idx * 2 + 1) >= 4) {
        NJ_THROW(vm, "java/lang/IndexOutOfBoundsException",
                 "texture unit 0 or 1");
        return NJ_ERR;
    }
    return NJ_OK;
}

NJFN(m3g_ray_getNormalX)
{
    ret->i = m3g_ray_component(vm, args, "_nj_norm", 0);
    return NJ_OK;
}

NJFN(m3g_ray_getNormalY)
{
    ret->i = m3g_ray_component(vm, args, "_nj_norm", 1);
    return NJ_OK;
}

NJFN(m3g_ray_getNormalZ)
{
    ret->i = m3g_ray_component(vm, args, "_nj_norm", 2);
    return NJ_OK;
}

M3G_R_GET(m3g_ray_getIntersected, NJ_C_RAY, "_nj_xnode", "L" NJ_C_NODE ";")
M3G_F_GET(m3g_ray_getDistance,    NJ_C_RAY, "_nj_dist")
M3G_I_GET(m3g_ray_getSubmeshIndex, NJ_C_RAY, "_nj_submesh")

/* ------------------------------------------------------------------ */
/* The mesh family: the base a Mesh ctor stores                        */
/* ------------------------------------------------------------------ */
/*
 * Vertex buffer, index buffer and one appearance per submesh.  The loader
 * fills the same three things from the file, and getSubmeshCount() answers
 * from the int field, so both routes end up with the two in step.
 */
static void mesh_set_base(nj_vm *vm, nj_ref self, nj_ref vb, nj_ref ib,
                          nj_ref app)
{
    nj_ref subs;

    nj_rput(vm, self, m3g_fof(vm, NJ_C_MESH, "_nj_ibs", "[L" NJ_C_IBUF ";"), 0);
    subs = nj_object_new(vm, NJ_C_VECTOR);
    nj_rput(vm, self, m3g_fof(vm, NJ_C_MESH, "_nj_vb", "L" NJ_C_VBUF ";"), vb);
    nj_rput(vm, self, m3g_fof(vm, NJ_C_MESH, "_nj_ib", "L" NJ_C_IBUF ";"), ib);
    if (!subs) {
        return;
    }
    nj_call(vm, subs, NJ_C_VECTOR, "<init>", "()V", NULL, NULL);
    nj_rput(vm, self, m3g_fof(vm, NJ_C_MESH, "_nj_subs", "L" NJ_C_VECTOR ";"),
            subs);
    if (ib) {
        nj_value a[1];

        a[0].r = app;
        nj_call(vm, subs, NJ_C_VECTOR, "addElement", "(Ljava/lang/Object;)V",
                a, NULL);
    }
    nj_iput(vm, self, m3g_fof(vm, NJ_C_MESH, "_nj_nsub", "I"), ib ? 1 : 0);
}

NJFN(m3g_mesh_init)
{
    mesh_set_base(vm, NJ_THIS(args), NJ_ARG_REF(args, 0), NJ_ARG_REF(args, 1),
                  NJ_ARG_REF(args, 2));
    return NJ_OK;
}

/* Copy each submesh slot; null appearances retain their positions. */
static nj_status mesh_set_arrays(nj_vm *vm, nj_ref self, nj_ref vb,
                                  nj_ref ibs, nj_ref apps)
{
    nj_u4 k, nsub = ibs ? nj_array_length(ibs) : 0u;
    nj_ref copy, subs;
    mesh_set_base(vm, self, vb, nsub ? nj_array_get_ref(ibs, 0u) : 0,
                  apps && nj_array_length(apps) ? nj_array_get_ref(apps, 0u) : 0);
    copy = nj_object_array_new(vm, NJ_C_IBUF, nsub);
    if (!copy) return nj_throw_oom(vm);
    nj_rput(vm, self, m3g_fof(vm, NJ_C_MESH, "_nj_ibs", "[L" NJ_C_IBUF ";"), copy);
    subs = nj_rget(vm, self, m3g_fof(vm, NJ_C_MESH, "_nj_subs", "L" NJ_C_VECTOR ";"));
    if (!subs) return nj_throw_oom(vm);
    if (nj_call(vm, subs, NJ_C_VECTOR, "removeAllElements", "()V", NULL, NULL) != NJ_OK)
        return NJ_ERR;
    for (k = 0u; k < nsub; k++) {
        nj_value a;
        nj_array_set_ref(copy, k, nj_array_get_ref(ibs, k));
        a.r = apps && k < nj_array_length(apps) ? nj_array_get_ref(apps, k) : 0;
        if (nj_call(vm, subs, NJ_C_VECTOR, "addElement", "(Ljava/lang/Object;)V", &a, NULL) != NJ_OK)
            return NJ_ERR;
    }
    set_submesh_count(vm, self, (nj_s4)nsub);
    return NJ_OK;
}

NJFN(m3g_mesh_init_arrays)
{
    return mesh_set_arrays(vm, NJ_THIS(args), NJ_ARG_REF(args, 0),
                           NJ_ARG_REF(args, 1), NJ_ARG_REF(args, 2));
}

/* ------------------------------------------------------------------ */
/* MorphingMesh                                                        */
/* ------------------------------------------------------------------ */
static const nj_builtin_field morph_fields[] = {
    { "_nj_morphs",  "[L" NJ_C_VBUF ";", 0 },
    { "_nj_nmorph",  "I", 0 },
    { "_nj_weights", "[F", 0 }
};

static const nj_builtin_method morph_methods[] = {
    M("<init>", "(L" NJ_C_VBUF ";[L" NJ_C_VBUF ";L" NJ_C_IBUF ";"
                "L" NJ_C_APPEAR ";)V", NJ_ACC_PUBLIC),
    M("<init>", "(L" NJ_C_VBUF ";[L" NJ_C_VBUF ";[L" NJ_C_IBUF ";"
                "[L" NJ_C_APPEAR ";)V", NJ_ACC_PUBLIC),
    M("getMorphTarget",      "(I)L" NJ_C_VBUF ";", NJ_ACC_PUBLIC),
    M("getMorphTargetCount", "()I",                NJ_ACC_PUBLIC),
    M("setWeights",          "([F)V",              NJ_ACC_PUBLIC),
    M("getWeights",          "([F)V",              NJ_ACC_PUBLIC)
};

/* Store the morph targets and give every one of them a weight: the first is
   the whole shape until a title says otherwise, which is the default the JSR
   describes. */
static void morph_set_targets(nj_vm *vm, nj_ref self, nj_ref vb, nj_ref targets,
                              nj_ref ib, nj_ref app)
{
    nj_u4 n = targets ? (nj_u4)nj_array_length(targets) : 0u;
    nj_ref copy = 0, weights = 0;
    nj_u4 i;

    mesh_set_base(vm, self, vb, ib, app);
    if (n > 0u) {
        copy = nj_object_array_new(vm, "javax/microedition/m3g/VertexBuffer", n);
        weights = nj_array_new(vm, "F", n);
        if (!copy || !weights) {
            return;
        }
        for (i = 0u; i < n; i++) {
            nj_array_set_ref(copy, i, nj_array_get_ref(targets, i));
        }
        {
            float *w = (float *)nj_array_data(weights);

            if (w) {
                for (i = 0u; i < n; i++) {
                    w[i] = (i == 0u) ? 1.0f : 0.0f;
                }
            }
        }
    }
    nj_rput(vm, self, m3g_fof(vm, NJ_C_MORPH, "_nj_morphs",
                              "[L" NJ_C_VBUF ";"), copy);
    nj_rput(vm, self, m3g_fof(vm, NJ_C_MORPH, "_nj_weights", "[F"), weights);
    nj_iput(vm, self, m3g_fof(vm, NJ_C_MORPH, "_nj_nmorph", "I"), (nj_s4)n);
}

NJFN(m3g_morph_init)
{
    morph_set_targets(vm, NJ_THIS(args), NJ_ARG_REF(args, 0), NJ_ARG_REF(args, 1),
                      NJ_ARG_REF(args, 2), NJ_ARG_REF(args, 3));
    return NJ_OK;
}

NJFN(m3g_morph_init_arrays)
{
    nj_ref self = NJ_THIS(args);
    nj_ref ibs = NJ_ARG_REF(args, 2);
    nj_ref apps = NJ_ARG_REF(args, 3);
    nj_ref ib = 0, app = 0;

    if (ibs && nj_array_length(ibs) > 0) {
        ib = nj_array_get_ref(ibs, 0u);
    }
    if (apps && nj_array_length(apps) > 0) {
        app = nj_array_get_ref(apps, 0u);
    }
    morph_set_targets(vm, self, NJ_ARG_REF(args, 0), NJ_ARG_REF(args, 1),
                      ib, app);
    return mesh_set_arrays(vm, self, NJ_ARG_REF(args, 0), ibs, apps);
}

NJFN(m3g_morph_getTarget)
{
    nj_ref targets = nj_rget(vm, NJ_THIS(args),
                             m3g_fof(vm, NJ_C_MORPH, "_nj_morphs",
                                     "[L" NJ_C_VBUF ";"));
    nj_s4 idx = NJ_ARG_INT(args, 0);

    if (!targets || idx < 0 || idx >= (nj_s4)nj_array_length(targets)) {
        NJ_THROW(vm, "java/lang/IndexOutOfBoundsException",
                 "no such morph target");
        return NJ_ERR;
    }
    ret->r = nj_array_get_ref(targets, (nj_u4)idx);
    return NJ_OK;
}

NJFN(m3g_morph_setWeights)
{
    nj_ref w = nj_rget(vm, NJ_THIS(args),
                       m3g_fof(vm, NJ_C_MORPH, "_nj_weights", "[F"));

    (void)m3g_copy_floats(w, NJ_ARG_REF(args, 0));
    return NJ_OK;
}

NJFN(m3g_morph_getWeights)
{
    nj_ref w = nj_rget(vm, NJ_THIS(args),
                       m3g_fof(vm, NJ_C_MORPH, "_nj_weights", "[F"));

    (void)m3g_copy_floats(NJ_ARG_REF(args, 0), w);
    return NJ_OK;
}

M3G_I_GET(m3g_morph_getTargetCount, NJ_C_MORPH, "_nj_nmorph")

/* ------------------------------------------------------------------ */
/* SkinnedMesh                                                         */
/* ------------------------------------------------------------------ */
#define NJ_M3G_BONES 64

static const nj_builtin_field skin_fields[] = {
    { "_nj_skeleton", "L" NJ_C_GROUP ";",  0 },
    { "_nj_bones",    "[L" NJ_C_NODE ";",  0 },
    { "_nj_nbones",   "I",                 0 }
};

static const nj_builtin_method skin_methods[] = {
    M("<init>", "(L" NJ_C_VBUF ";L" NJ_C_IBUF ";L" NJ_C_APPEAR ";"
                "L" NJ_C_GROUP ";)V", NJ_ACC_PUBLIC),
    M("<init>", "(L" NJ_C_VBUF ";[L" NJ_C_IBUF ";[L" NJ_C_APPEAR ";"
                "L" NJ_C_GROUP ";)V", NJ_ACC_PUBLIC),
    M("getSkeleton",     "()L" NJ_C_GROUP ";", NJ_ACC_PUBLIC),
    M("addTransform",    "(L" NJ_C_NODE ";III)V", NJ_ACC_PUBLIC),
    M("getBoneTransform", "(L" NJ_C_NODE ";L" NJ_C_XFORM ";)V", NJ_ACC_PUBLIC),
    M("getBoneVertices", "(L" NJ_C_NODE ";[I[F)I", NJ_ACC_PUBLIC)
};

NJFN(m3g_skin_init)
{
    nj_ref self = NJ_THIS(args);

    mesh_set_base(vm, self, NJ_ARG_REF(args, 0), NJ_ARG_REF(args, 1),
                  NJ_ARG_REF(args, 2));
    nj_rput(vm, self, m3g_fof(vm, NJ_C_SKIN, "_nj_skeleton",
                              "L" NJ_C_GROUP ";"), NJ_ARG_REF(args, 3));
    return NJ_OK;
}

NJFN(m3g_skin_init_arrays)
{
    nj_ref self = NJ_THIS(args);
    nj_ref ibs = NJ_ARG_REF(args, 1);
    nj_ref apps = NJ_ARG_REF(args, 2);

    if (mesh_set_arrays(vm, self, NJ_ARG_REF(args, 0), ibs, apps) != NJ_OK)
        return NJ_ERR;
    nj_rput(vm, self, m3g_fof(vm, NJ_C_SKIN, "_nj_skeleton",
                              "L" NJ_C_GROUP ";"), NJ_ARG_REF(args, 3));
    return NJ_OK;
}

/*
 * addTransform records which node drives which bone.  Nothing skins a vertex
 * yet, so the table is what the getters answer from: getBoneTransform hands
 * back the node's own transform and getBoneVertices answers zero, which says
 * "this runtime does not skin" instead of inventing vertices.  The table is
 * a fixed 64 entries because a bone count is bounded by the file format
 * (255) and a title that exceeds 64 is better refused than silently cut.
 */
NJFN(m3g_skin_addTransform)
{
    nj_ref self = NJ_THIS(args);
    nj_ref node = NJ_ARG_REF(args, 0);
    nj_ref bones = nj_rget(vm, self, m3g_fof(vm, NJ_C_SKIN, "_nj_bones",
                                             "[L" NJ_C_NODE ";"));
    nj_s4 n = nj_iget(vm, self, m3g_fof(vm, NJ_C_SKIN, "_nj_nbones", "I"));

    if (!node) {
        NJ_THROW(vm, "java/lang/NullPointerException", "addTransform(null)");
        return NJ_ERR;
    }
    if (!bones) {
        bones = nj_object_array_new(vm, "javax/microedition/m3g/Node",
                                    (nj_u4)NJ_M3G_BONES);
        if (!bones) {
            return nj_throw_oom(vm);
        }
        nj_rput(vm, self, m3g_fof(vm, NJ_C_SKIN, "_nj_bones",
                                  "[L" NJ_C_NODE ";"), bones);
    }
    if (n >= (nj_s4)NJ_M3G_BONES) {
        NJ_THROW(vm, "java/lang/IndexOutOfBoundsException",
                 "this runtime keeps 64 bone links");
        return NJ_ERR;
    }
    nj_array_set_ref(bones, (nj_u4)n, node);
    nj_iput(vm, self, m3g_fof(vm, NJ_C_SKIN, "_nj_nbones", "I"), n + 1);
    return NJ_OK;
}

NJFN(m3g_skin_getBoneTransform)
{
    nj_ref node = NJ_ARG_REF(args, 0);
    nj_ref dst = NJ_ARG_REF(args, 1);

    if (dst) {
        float *m = mat_of(vm, dst, NJ_C_XFORM, "_nj_m");

        if (m && node) {
            float *src = mat_of(vm, node, NJ_C_NODE, "_nj_m");

            if (src) {
                memcpy(m, src, sizeof(float) * NJ_MF);
            }
        }
    }
    return NJ_OK;
}

NJFN(m3g_skin_getBoneVertices)
{
    /* Nothing is skinned: zero vertices, and the index array is left alone
       rather than filled with values that would be wrong. */
    ret->i = 0;
    return NJ_OK;
}

M3G_R_GET(m3g_skin_getSkeleton, NJ_C_SKIN, "_nj_skeleton",
          "L" NJ_C_GROUP ";")


/* ==================================================================== *
 * Tranche two: the rest of the member surface.  Same rule as tranche one
 * - store what a title hands over, answer what it asks for - and the same
 * limit, stated where it bites: this module does not rasterise, so
 * Graphics3D.render() returns the number of triangles it did NOT draw.
 *
 * The audit that produced this list is tools/m3g_api_audit.py: it reads
 * this file's tables the way the compiler would and compares them with
 * the API the collected titles were compiled against.
 * ==================================================================== */

#define M3G_REF_GET(fn, cls, fld, desc)                                   \
    NJFN(fn)                                                              \
    {                                                                     \
        nj_field *f = m3g_fof(vm, cls, fld, desc);                        \
        ret->r = f ? nj_rget(vm, NJ_THIS(args), f) : NULL;                \
        return NJ_OK;                                                     \
    }
#define M3G_REF_SET(fn, cls, fld, desc)                                   \
    NJFN(fn)                                                              \
    {                                                                     \
        nj_field *f = m3g_fof(vm, cls, fld, desc);                        \
        if (f) {                                                          \
            /* nj_rput, not nj_iput: a reference does not fit an nj_s4. */\
            nj_rput(vm, NJ_THIS(args), f, NJ_ARG_REF(args, 0));           \
        }                                                                 \
        return NJ_OK;                                                     \
    }
/* A boolean that is stored in an "I" field, because that is how this file
 * has kept them since PolygonMode.setTwoSided. */
#define M3G_BOOL_GET(fn, cls, fld)                                        \
    NJFN(fn)                                                              \
    {                                                                     \
        nj_field *f = m3g_fof(vm, cls, fld, "I");                         \
        ret->i = f ? (nj_iget(vm, NJ_THIS(args), f) ? 1 : 0) : 0;         \
        return NJ_OK;                                                     \
    }
#define M3G_BOOL_SET(fn, cls, fld)                                        \
    NJFN(fn)                                                              \
    {                                                                     \
        nj_field *f = m3g_fof(vm, cls, fld, "I");                         \
        if (f) {                                                          \
            nj_iput(vm, NJ_THIS(args), f, NJ_ARG_INT(args, 0) ? 1 : 0);   \
        }                                                                 \
        return NJ_OK;                                                     \
    }

/* ---- Appearance ---------------------------------------------------- */
NJFN(m3g_appear_init)
{
    nj_ref self = NJ_THIS(args);

    nj_iput(vm, self, m3g_fof(vm, NJ_C_APPEAR, "_nj_layer", "I"), 0);
    return NJ_OK;
}
M3G_I_GET(m3g_appear_getLayer, NJ_C_APPEAR, "_nj_layer")
M3G_REF_SET(m3g_appear_setFog, NJ_C_APPEAR, "_nj_fog", "L" NJ_C_FOG ";")
M3G_REF_GET(m3g_appear_getFog, NJ_C_APPEAR, "_nj_fog", "L" NJ_C_FOG ";")
M3G_REF_GET(m3g_appear_getMaterial, NJ_C_APPEAR, "_nj_mat", "L" NJ_C_MAT ";")

/* ---- Background ---------------------------------------------------- */
NJFN(m3g_bg_init)
{
    nj_ref self = NJ_THIS(args);

    nj_iput(vm, self, m3g_fof(vm, NJ_C_BG, "_nj_color", "I"), 0);
    nj_iput(vm, self, m3g_fof(vm, NJ_C_BG, "_nj_cclear", "Z"), 1);
    nj_iput(vm, self, m3g_fof(vm, NJ_C_BG, "_nj_dclear", "Z"), 1);
    return NJ_OK;
}
M3G_I_GET(m3g_bg_getColor, NJ_C_BG, "_nj_color")
M3G_I_GET(m3g_bg_getImageModeX, NJ_C_BG, "_nj_immode")
M3G_I_GET(m3g_bg_getImageModeY, NJ_C_BG, "_nj_immodeY")

/* ---- Camera -------------------------------------------------------- */
/*
 * JSR 184's no-argument Camera has a perspective projection, 45 degrees of
 * vertical field of view, aspect 1, near 1 and far 1000.  The field values
 * are left at zero on purpose: cam_proj_fill() applies exactly those
 * defaults when it finds a zero, so the object answers the same matrix
 * without a second copy of the numbers.
 */
NJFN(m3g_cam_init)
{
    nj_iput(vm, NJ_THIS(args),
            m3g_fof(vm, NJ_C_CAMERA, "_nj_is_persp", "Z"), 1);
    return NJ_OK;
}

NJFN(m3g_cam_getProjection_arr)
{
    nj_ref arr = NJ_ARG_REF(args, 0);
    float m[NJ_MF];

    if (!arr || arr->length < (nj_s4)NJ_MF) {
        ret->i = 0;
        return NJ_OK;
    }
    if (!cam_proj_fill(vm, NJ_THIS(args), m)) {
        ret->i = 0;
        return NJ_OK;
    }
    memcpy((float *)arr->u.bytes, m, sizeof m);
    ret->i = 1;
    return NJ_OK;
}

/* ---- CompositingMode ----------------------------------------------- */
NJFN(m3g_comp_init)
{
    nj_ref self = NJ_THIS(args);

    nj_iput(vm, self, m3g_fof(vm, NJ_C_COMP, "_nj_blend", "I"), 64);  /* ALPHA */
    nj_iput(vm, self, m3g_fof(vm, NJ_C_COMP, "_nj_dtest", "I"), 1);
    nj_iput(vm, self, m3g_fof(vm, NJ_C_COMP, "_nj_dwrite", "I"), 1);
    nj_iput(vm, self, m3g_fof(vm, NJ_C_COMP, "_nj_awrite", "I"), 1);
    nj_iput(vm, self, m3g_fof(vm, NJ_C_COMP, "_nj_cwrite", "I"), 1);
    nj_iput(vm, self, m3g_fof(vm, NJ_C_COMP, "_nj_athresh", "I"), 0);
    return NJ_OK;
}
NJFN(m3g_comp_getAlphaThreshold)
{
    nj_field *f = m3g_fof(vm, NJ_C_COMP, "_nj_athresh", "I");

    /* The setter stores the float's bits, so the getter has to read bits. */
    ret->f = f ? bits2f(nj_iget(vm, NJ_THIS(args), f)) : 0.0f;
    return NJ_OK;
}
M3G_BOOL_SET(m3g_comp_setAlphaWrite, NJ_C_COMP, "_nj_awrite")
M3G_BOOL_GET(m3g_comp_isAlphaWrite, NJ_C_COMP, "_nj_awrite")
M3G_BOOL_SET(m3g_comp_setColorWrite, NJ_C_COMP, "_nj_cwrite")
M3G_BOOL_GET(m3g_comp_isColorWrite, NJ_C_COMP, "_nj_cwrite")
M3G_BOOL_GET(m3g_comp_isDepthWrite, NJ_C_COMP, "_nj_dwrite")
M3G_BOOL_GET(m3g_comp_isDepthTest, NJ_C_COMP, "_nj_dtest")
NJFN(m3g_comp_setDepthOffset)
{
    nj_ref self = NJ_THIS(args);

    nj_iput(vm, self, m3g_fof(vm, NJ_C_COMP, "_nj_dofact", "F"),
            f2bits(NJ_ARG_FLOAT(args, 0)));
    nj_iput(vm, self, m3g_fof(vm, NJ_C_COMP, "_nj_dounits", "F"),
            f2bits(NJ_ARG_FLOAT(args, 1)));
    return NJ_OK;
}
M3G_F_GET(m3g_comp_getDepthOffsetFactor, NJ_C_COMP, "_nj_dofact")
M3G_F_GET(m3g_comp_getDepthOffsetUnits, NJ_C_COMP, "_nj_dounits")

/* ---- Light attenuation --------------------------------------------- */
/*
 * The three coefficients live in one float[3].  A Light that never had
 * setAttenuation() has no array at all, and the JSR's default is the
 * constant 1 with no distance terms, which is what the zero index answers.
 */
static float m3g_atten_of(nj_vm *vm, nj_ref self, int index)
{
    nj_ref a = nj_rget(vm, self, m3g_fof(vm, NJ_C_LIGHT, "_nj_atten", "[F"));

    if (!a || (nj_s4)a->length <= (nj_s4)index) {
        return index == 0 ? 1.0f : 0.0f;
    }
    return ((const float *)a->u.bytes)[index];
}
NJFN(m3g_light_getConstantAtten)
{
    ret->f = m3g_atten_of(vm, NJ_THIS(args), 0);
    return NJ_OK;
}
NJFN(m3g_light_getLinearAtten)
{
    ret->f = m3g_atten_of(vm, NJ_THIS(args), 1);
    return NJ_OK;
}
NJFN(m3g_light_getQuadraticAtten)
{
    ret->f = m3g_atten_of(vm, NJ_THIS(args), 2);
    return NJ_OK;
}

/* ---- Node ---------------------------------------------------------- */
M3G_F_GET(m3g_node_getAlphaFactor, NJ_C_NODE, "_nj_alpha")
NJFN(m3g_node_align)
{
    nj_ref self = NJ_THIS(args);

    nj_rput(vm, self, m3g_fof(vm, NJ_C_NODE, "_nj_alignZ",
                              "L" NJ_C_NODE ";"), NJ_ARG_REF(args, 0));
    nj_iput(vm, self, m3g_fof(vm, NJ_C_NODE, "_nj_alignZA", "I"), 148);
    return NJ_OK;
}
NJFN(m3g_node_getAlignmentTarget)
{
    nj_ref self = NJ_THIS(args);
    nj_s4 axis = NJ_ARG_INT(args, 0);
    nj_s4 got;

    /*
     * align(reference) aligns the Z axis, and NONE asks "whichever axis you
     * are aligned on" - so a node aligned on Z has to answer Z for NONE as
     * well, and answer NONE only when it is aligned on nothing.
     */
    if (axis == 148 || axis == 144) {
        got = nj_iget(vm, self, m3g_fof(vm, NJ_C_NODE, "_nj_alignZA", "I"));
        if (axis == 148) {
            ret->i = got;
            return NJ_OK;
        }
        ret->i = (got != 0) ? 148 : 144;
        return NJ_OK;
    }
    if (axis == 147) {
        ret->i = nj_iget(vm, self, m3g_fof(vm, NJ_C_NODE, "_nj_alignYA", "I"));
        return NJ_OK;
    }
    ret->i = 144;
    return NJ_OK;
}
NJFN(m3g_node_getAlignmentReference)
{
    nj_ref self = NJ_THIS(args);
    nj_s4 axis = NJ_ARG_INT(args, 0);
    nj_ref ref;

    if (axis == 147) {
        ret->r = nj_rget(vm, self, m3g_fof(vm, NJ_C_NODE, "_nj_alignY",
                                           "L" NJ_C_NODE ";"));
        return NJ_OK;
    }
    /* Z, and NONE which means "the axis you are aligned on". */
    ref = nj_rget(vm, self, m3g_fof(vm, NJ_C_NODE, "_nj_alignZ",
                                    "L" NJ_C_NODE ";"));
    if (!ref && axis == 144) {
        ref = nj_rget(vm, self, m3g_fof(vm, NJ_C_NODE, "_nj_alignY",
                                        "L" NJ_C_NODE ";"));
    }
    ret->r = ref;
    return NJ_OK;
}

/* ---- Object3D ------------------------------------------------------ */
/*
 * getReferences() answers the AnimationTracks that animate this object,
 * which is the Vector addAnimationTrack() filled.  Inside the Vector they
 * are Object3Ds as far as the array is concerned, which is what the JSR
 * declares the array to hold.
 */
NJFN(m3g_o3d_getReferences)
{
    nj_ref self = NJ_THIS(args);
    nj_ref arr = NJ_ARG_REF(args, 0);
    nj_ref got[64];
    nj_s4 n = 0;
    nj_s4 i;
    nj_ref vec;
    nj_class *grp = nj_class_lookup(vm, NJ_C_GROUP);
    static const struct {
        const char *cls;
        const char *fld;
        const char *desc;
    } table[] = {
        { NJ_C_WORLD,    "_nj_cam",       "L" NJ_C_CAMERA ";" },
        { NJ_C_WORLD,    "_nj_bg",        "L" NJ_C_BG ";" },
        { NJ_C_SPRITE3D, "_nj_img",       "L" NJ_C_IMAGE2D ";" },
        { NJ_C_SPRITE3D, "_nj_app",       "L" NJ_C_APPEAR ";" },
        { NJ_C_MESH,     "_nj_vb",        "L" NJ_C_VBUF ";" },
        { NJ_C_MESH,     "_nj_ib",        "L" NJ_C_IBUF ";" },
        { NJ_C_APPEAR,   "_nj_mat",       "L" NJ_C_MAT ";" },
        { NJ_C_APPEAR,   "_nj_pm",        "L" NJ_C_POLY ";" },
        { NJ_C_APPEAR,   "_nj_cm",        "L" NJ_C_COMP ";" },
        { NJ_C_APPEAR,   "_nj_tex0",      "L" NJ_C_TEX2D ";" },
        { NJ_C_APPEAR,   "_nj_tex1",      "L" NJ_C_TEX2D ";" },
        { NJ_C_TEX2D,    "_nj_img",       "L" NJ_C_IMAGE2D ";" },
        { NJ_C_BG,       "_nj_img",       "L" NJ_C_IMAGE2D ";" },
        { NJ_C_ATRACK,   "_nj_kfs",       "L" NJ_C_KFS ";" },
        { NJ_C_ATRACK,   "_nj_ctl",       "L" NJ_C_ACTL ";" },
        { NJ_C_VBUF,     "_nj_positions", "L" NJ_C_VARR ";" },
        { NJ_C_VBUF,     "_nj_normals",   "L" NJ_C_VARR ";" },
        { NJ_C_VBUF,     "_nj_colors",    "L" NJ_C_VARR ";" },
        { NJ_C_VBUF,     "_nj_tex0",      "L" NJ_C_VARR ";" }
    };

    /*
     * The eight members above the call are not objects: they are counted, so
     * that a caller that asks with a null array still gets the right number,
     * and only the first 64 are remembered for the copy below.
     */
#define M3G_REF_PUSH(r)                                                     \
    do {                                                                    \
        if ((r) != 0) {                                                     \
            if (n < (nj_s4)(sizeof(got) / sizeof(got[0]))) {                \
                got[n] = (r);                                               \
            }                                                               \
            n++;                                                            \
        }                                                                   \
    } while (0)

    i = n;
    vec = nj_rget(vm, self, m3g_fof(vm, NJ_C_O3D, "_nj_tracks",
                                    "L" NJ_C_VECTOR ";"));
    for (i = 0; i < 2; i++) {
        nj_ref v = i == 0 ? vec : NULL;

        if (i == 1 && grp && nj_class_is_subclass_of(self->cls, grp)) {
            v = nj_rget(vm, self, m3g_fof(vm, NJ_C_GROUP, "_nj_kids",
                                          "L" NJ_C_VECTOR ";"));
        }
        if (v) {
            nj_value sz;

            if (nj_call(vm, v, NJ_C_VECTOR, "size", "()I", NULL, &sz) == NJ_OK) {
                nj_s4 k;

                for (k = 0; k < sz.i; k++) {
                    nj_value a[1];
                    nj_value e;

                    a[0].i = k;
                    if (nj_call(vm, v, NJ_C_VECTOR, "elementAt",
                                "(I)Ljava/lang/Object;", a, &e) == NJ_OK) {
                        M3G_REF_PUSH(e.r);
                    }
                }
            }
        }
    }
    for (i = 0; i < (nj_s4)(sizeof(table) / sizeof(table[0])); i++) {
        nj_class *c = nj_class_lookup(vm, table[i].cls);
        nj_field *f;

        if (!c || !nj_class_is_subclass_of(self->cls, c)) {
            continue;
        }
        f = nj_field_lookup(vm, table[i].cls, table[i].fld, table[i].desc, 0);
        if (f) {
            M3G_REF_PUSH(nj_rget(vm, self, f));
        }
    }
#undef M3G_REF_PUSH

    if (arr) {
        if ((nj_s4)arr->length < n) {
            NJ_THROW(vm, "java/lang/IllegalArgumentException",
                     "references array is too small");
        }
        for (i = 0; i < n && i < (nj_s4)arr->length; i++) {
            nj_array_set_ref(arr, (nj_u4)i, got[i]);
        }
    }
    ret->i = n;
    return NJ_OK;
}
M3G_REF_SET(m3g_o3d_setUserObject, NJ_C_O3D, "_nj_user", "Ljava/lang/Object;")
M3G_REF_GET(m3g_o3d_getUserObject, NJ_C_O3D, "_nj_user", "Ljava/lang/Object;")
NJFN(m3g_o3d_removeAnimationTrack)
{
    nj_ref self = NJ_THIS(args);
    nj_ref tracks = nj_rget(vm, self,
                            m3g_fof(vm, NJ_C_O3D, "_nj_tracks",
                                    "L" NJ_C_VECTOR ";"));
    nj_value a[1];

    if (tracks) {
        a[0].r = NJ_ARG_REF(args, 0);
        (void)nj_call(vm, tracks, NJ_C_VECTOR, "removeElement",
                      "(Ljava/lang/Object;)Z", a, NULL);
    }
    return NJ_OK;
}

/* ---- PolygonMode --------------------------------------------------- */
NJFN(m3g_poly_init)
{
    nj_ref self = NJ_THIS(args);

    nj_iput(vm, self, m3g_fof(vm, NJ_C_POLY, "_nj_winding", "I"), 168); /* CCW */
    nj_iput(vm, self, m3g_fof(vm, NJ_C_POLY, "_nj_culling", "I"), 160); /* BACK */
    nj_iput(vm, self, m3g_fof(vm, NJ_C_POLY, "_nj_shading", "I"), 164); /* FLAT */
    nj_iput(vm, self, m3g_fof(vm, NJ_C_POLY, "_nj_persp", "Z"), 1);
    nj_iput(vm, self, m3g_fof(vm, NJ_C_POLY, "_nj_twosided", "Z"), 0);
    nj_iput(vm, self, m3g_fof(vm, NJ_C_POLY, "_nj_localcam", "Z"), 0);
    return NJ_OK;
}
M3G_I_GET(m3g_poly_getWinding, NJ_C_POLY, "_nj_winding")
M3G_Z_SET(m3g_poly_setLocalCam, NJ_C_POLY, "_nj_localcam")
M3G_Z_GET(m3g_poly_isLocalCam, NJ_C_POLY, "_nj_localcam")
M3G_Z_GET(m3g_poly_isPersp, NJ_C_POLY, "_nj_persp")

/* ---- Sprite3D ------------------------------------------------------ */
M3G_Z_GET(m3g_sprite_isScaled, NJ_C_SPRITE3D, "_nj_scaled")
M3G_REF_GET(m3g_sprite_getAppearance, NJ_C_SPRITE3D, "_nj_app",
            "L" NJ_C_APPEAR ";")
M3G_REF_GET(m3g_sprite_getImage, NJ_C_SPRITE3D, "_nj_img",
            "L" NJ_C_IMAGE2D ";")
NJFN(m3g_sprite_setCrop)
{
    nj_ref self = NJ_THIS(args);
    static const char *names[4] = { "_nj_cropX", "_nj_cropY",
                                    "_nj_cropW", "_nj_cropH" };
    int i;

    for (i = 0; i < 4; i++) {
        nj_iput(vm, self, m3g_fof(vm, NJ_C_SPRITE3D, names[i], "I"),
                NJ_ARG_INT(args, i));
    }
    return NJ_OK;
}
M3G_I_GET(m3g_sprite_getCropX, NJ_C_SPRITE3D, "_nj_cropX")
M3G_I_GET(m3g_sprite_getCropY, NJ_C_SPRITE3D, "_nj_cropY")
M3G_I_GET(m3g_sprite_getCropW, NJ_C_SPRITE3D, "_nj_cropW")
M3G_I_GET(m3g_sprite_getCropH, NJ_C_SPRITE3D, "_nj_cropH")

/* ---- Texture2D ----------------------------------------------------- */
M3G_I_GET(m3g_tex_getLevelFilter, NJ_C_TEX2D, "_nj_filtLevel")
M3G_I_GET(m3g_tex_getImageFilter, NJ_C_TEX2D, "_nj_filtImage")
M3G_I_GET(m3g_tex_getWrappingS, NJ_C_TEX2D, "_nj_wrapS")
M3G_I_GET(m3g_tex_getWrappingT, NJ_C_TEX2D, "_nj_wrapT")
M3G_I_GET(m3g_tex_getBlending, NJ_C_TEX2D, "_nj_blend")
M3G_I_SET(m3g_tex_setBlendColor, NJ_C_TEX2D, "_nj_blendcolor")
M3G_I_GET(m3g_tex_getBlendColor, NJ_C_TEX2D, "_nj_blendcolor")

/* ---- Transformable.preRotate --------------------------------------- */
NJFN(m3g_xable_preRotate)
{
    float *m = mat_of(vm, NJ_THIS(args), NJ_C_XABLE, "_nj_m");
    float r[NJ_MF];
    float cur[NJ_MF];

    if (!m) {
        return NJ_OK;
    }
    mat_identity(r);
    mat_rotate(r, NJ_ARG_FLOAT(args, 0), NJ_ARG_FLOAT(args, 1),
               NJ_ARG_FLOAT(args, 2), NJ_ARG_FLOAT(args, 3));
    memcpy(cur, m, sizeof cur);
    mat_mul(r, cur, m);
    return NJ_OK;
}

/* ---- Transform ----------------------------------------------------- */
NJFN(m3g_xf_init_copy)
{
    float *src = mat_of(vm, NJ_ARG_REF(args, 0), NJ_C_XFORM, "_nj_m");
    float *dst = mat_of(vm, NJ_THIS(args), NJ_C_XFORM, "_nj_m");

    if (src && dst) {
        memcpy(dst, src, sizeof(float) * NJ_MF);
    }
    return NJ_OK;
}
/*
 * postRotateQuat(x, y, z, w): a unit quaternion, converted with the same
 * row-major layout mat_rotate() writes, then post-multiplied.
 */
NJFN(m3g_xf_postRotateQuat)
{
    float x = NJ_ARG_FLOAT(args, 0), y = NJ_ARG_FLOAT(args, 1);
    float z = NJ_ARG_FLOAT(args, 2), w = NJ_ARG_FLOAT(args, 3);
    float n = sqrtf(x * x + y * y + z * z + w * w);
    float *m = mat_of(vm, NJ_THIS(args), NJ_C_XFORM, "_nj_m");
    float r[NJ_MF];
    float cur[NJ_MF];

    if (!m || n <= 0.0f) {
        return NJ_OK;                    /* a zero quaternion rotates nothing */
    }
    x /= n; y /= n; z /= n; w /= n;
    mat_identity(r);
    r[0] = 1.0f - 2.0f * (y * y + z * z);
    r[1] = 2.0f * (x * y - z * w);
    r[2] = 2.0f * (x * z + y * w);
    r[4] = 2.0f * (x * y + z * w);
    r[5] = 1.0f - 2.0f * (x * x + z * z);
    r[6] = 2.0f * (y * z - x * w);
    r[8] = 2.0f * (x * z - y * w);
    r[9] = 2.0f * (y * z + x * w);
    r[10] = 1.0f - 2.0f * (x * x + y * y);
    memcpy(cur, m, sizeof cur);
    mat_mul(cur, r, m);
    return NJ_OK;
}

/* ---- VertexArray --------------------------------------------------- */
/*
 * The file keeps its data as shorts, which is what the container format
 * uses; the byte form widens on the way in and narrows on the way out.
 */
static nj_ref m3g_varr_data(nj_vm *vm, nj_ref self)
{
    return nj_rget(vm, self, m3g_fof(vm, NJ_C_VARR, "_nj_data", "[S"));
}
NJFN(m3g_varr_set_bytes)
{
    return m3g_varr_set_values(vm, NJ_THIS(args), NJ_ARG_REF(args, 2),
                              NJ_ARG_INT(args, 0), NJ_ARG_INT(args, 1), 1);
}
M3G_I_GET(m3g_varr_getComponentType, NJ_C_VARR, "_nj_ctype")
NJFN(m3g_varr_get_short)
{
    nj_ref self = NJ_THIS(args);
    nj_ref src = m3g_varr_data(vm, self);
    nj_ref out = NJ_ARG_REF(args, 2);
    nj_s4 first = NJ_ARG_INT(args, 0);
    nj_s4 count = NJ_ARG_INT(args, 1);
    nj_s4 comp = nj_iget(vm, self, m3g_fof(vm, NJ_C_VARR, "_nj_numComp", "I"));
    nj_s4 i, total = count * (comp > 0 ? comp : 1);

    if (!src || !out) {
        return NJ_OK;
    }
    for (i = 0; i < total
                && first * (comp > 0 ? comp : 1) + i < (nj_s4)src->length
                && i < (nj_s4)out->length; i++) {
        ((nj_s2 *)out->u.bytes)[i] =
            ((const nj_s2 *)src->u.bytes)[first * (comp > 0 ? comp : 1) + i];
    }
    return NJ_OK;
}
NJFN(m3g_varr_get_bytes)
{
    nj_ref self = NJ_THIS(args);
    nj_ref src = m3g_varr_data(vm, self);
    nj_ref out = NJ_ARG_REF(args, 2);
    nj_s4 first = NJ_ARG_INT(args, 0);
    nj_s4 count = NJ_ARG_INT(args, 1);
    nj_s4 comp = nj_iget(vm, self, m3g_fof(vm, NJ_C_VARR, "_nj_numComp", "I"));
    nj_s4 i, total = count * (comp > 0 ? comp : 1);

    if (!src || !out) {
        return NJ_OK;
    }
    for (i = 0; i < total
                && first * (comp > 0 ? comp : 1) + i < (nj_s4)src->length
                && i < (nj_s4)out->length; i++) {
        nj_s2 v = ((const nj_s2 *)src->u.bytes)[first * (comp > 0 ? comp : 1) + i];

        out->u.bytes[i] = (nj_u1)(nj_s1)(v > 127 ? 127 : (v < -128 ? -128 : v));
    }
    return NJ_OK;
}

/* ---- VertexBuffer -------------------------------------------------- */
NJFN(m3g_vbuf_init)
{
    nj_ref self = NJ_THIS(args);

    nj_iput(vm, self, m3g_fof(vm, NJ_C_VBUF, "_nj_defColor", "I"),
            (nj_s4)0xFFFFFFFF);
    nj_iput(vm, self, m3g_fof(vm, NJ_C_VBUF, "_nj_bias", "F"), 0);
    return NJ_OK;
}
NJFN(m3g_vbuf_getVertexCount)
{
    nj_ref pos = nj_rget(vm, NJ_THIS(args),
                         m3g_fof(vm, NJ_C_VBUF, "_nj_positions",
                                 "L" NJ_C_VARR ";"));

    ret->i = pos ? nj_iget(vm, pos,
                           m3g_fof(vm, NJ_C_VARR, "_nj_numVerts", "I")) : 0;
    return NJ_OK;
}
M3G_REF_SET(m3g_vbuf_setNormals, NJ_C_VBUF, "_nj_normals", "L" NJ_C_VARR ";")
M3G_REF_GET(m3g_vbuf_getNormals, NJ_C_VBUF, "_nj_normals", "L" NJ_C_VARR ";")
M3G_REF_GET(m3g_vbuf_getColors, NJ_C_VBUF, "_nj_colors", "L" NJ_C_VARR ";")

/*
 * The float[] getters answer the values a title handed in, with the bias
 * and scale the JSR says to apply.  A vertex with no scale array is scaled
 * by 1, which is what a VertexBuffer that never called setScale() means.
 */
static float m3g_vbuf_scale(nj_vm *vm, nj_ref self, nj_s4 comp)
{
    nj_ref s = nj_rget(vm, self, m3g_fof(vm, NJ_C_VBUF, "_nj_scale", "[F"));

    if (!s || (nj_s4)s->length <= comp) {
        return 1.0f;
    }
    return ((const float *)s->u.bytes)[comp];
}
static void m3g_vbuf_read(nj_vm *vm, nj_ref va, nj_ref out, nj_s4 per_vertex,
                          nj_ref self)
{
    nj_ref data;
    nj_s4 nverts, comp, i, c;
    float bias;

    if (!va || !out) {
        return;
    }
    data = nj_rget(vm, va, m3g_fof(vm, NJ_C_VARR, "_nj_data", "[S"));
    nverts = nj_iget(vm, va, m3g_fof(vm, NJ_C_VARR, "_nj_numVerts", "I"));
    /* The stride of the stored array is the component COUNT; the component
       size is only how wide one component is. */
    comp = nj_iget(vm, va, m3g_fof(vm, NJ_C_VARR, "_nj_numComp", "I"));
    bias = bits2f(nj_iget(vm, self, m3g_fof(vm, NJ_C_VBUF, "_nj_bias", "F")));
    for (i = 0; i < nverts; i++) {
        for (c = 0; c < per_vertex; c++) {
            float v = 0.0f;

            if (data && c < comp && i * comp + c < (nj_s4)data->length) {
                v = (float)((const nj_s2 *)data->u.bytes)[i * comp + c];
            } else if (c == 3) {
                v = 1.0f;               /* the W a position implies */
            }
            if (i * per_vertex + c < (nj_s4)out->length) {
                ((float *)out->u.bytes)[i * per_vertex + c] =
                    v * m3g_vbuf_scale(vm, self, c) + bias;
            }
        }
    }
}
NJFN(m3g_vbuf_getPositions)
{
    nj_ref self = NJ_THIS(args);
    nj_ref pos = nj_rget(vm, self,
                         m3g_fof(vm, NJ_C_VBUF, "_nj_positions",
                                 "L" NJ_C_VARR ";"));

    m3g_vbuf_read(vm, pos, NJ_ARG_REF(args, 0), 4, self);
    ret->r = pos;
    return NJ_OK;
}
NJFN(m3g_vbuf_getTexCoords)
{
    nj_ref self = NJ_THIS(args);
    nj_ref tex = nj_rget(vm, self,
                         m3g_fof(vm, NJ_C_VBUF, "_nj_tex0",
                                 "L" NJ_C_VARR ";"));

    m3g_vbuf_read(vm, tex, NJ_ARG_REF(args, 1), 2, self);
    ret->r = tex;
    return NJ_OK;
}

/* ---- World --------------------------------------------------------- */
NJFN(m3g_world_init)
{
    return NJ_OK;          /* a fresh World has no camera and no background */
}

/* ---- Graphics3D ---------------------------------------------------- */
M3G_REF_GET(m3g_g3d_getTarget, NJ_C_G3D, "_nj_target", "Ljava/lang/Object;")
M3G_I_GET(m3g_g3d_getHints, NJ_C_G3D, "_nj_hints")
M3G_Z_GET(m3g_g3d_isDepthBuffer, NJ_C_G3D, "_nj_depth")
M3G_I_GET(m3g_g3d_getViewportX, NJ_C_G3D, "_nj_vx")
M3G_I_GET(m3g_g3d_getViewportY, NJ_C_G3D, "_nj_vy")
M3G_I_GET(m3g_g3d_getViewportW, NJ_C_G3D, "_nj_vw")
M3G_I_GET(m3g_g3d_getViewportH, NJ_C_G3D, "_nj_vh")
NJFN(m3g_g3d_setDepthRange)
{
    nj_ref self = NJ_THIS(args);

    nj_iput(vm, self, m3g_fof(vm, NJ_C_G3D, "_nj_drnear", "F"),
            f2bits(NJ_ARG_FLOAT(args, 0)));
    nj_iput(vm, self, m3g_fof(vm, NJ_C_G3D, "_nj_drfar", "F"),
            f2bits(NJ_ARG_FLOAT(args, 1)));
    return NJ_OK;
}
M3G_F_GET(m3g_g3d_getDepthRangeNear, NJ_C_G3D, "_nj_drnear")
M3G_F_GET(m3g_g3d_getDepthRangeFar, NJ_C_G3D, "_nj_drfar")

/* ---- software rasteriser: depth, texture, culling -------------------
 *
 * The Java call path (Graphics.fillTriangle) can carry a flat colour and
 * nothing else: no depth, no texture, and one interpreter round trip per
 * triangle.  A title that renders a world into an Image or into a bound
 * Graphics wants a depth buffer, and a textured planet wants texture
 * coordinates - so the pixels are written here, straight into the target
 * surface, and the Java call remains the fallback for a target this cannot
 * reach.
 *
 * The depth buffer lives in a Java int[] on the Graphics3D (a C buffer
 * would have to be owned by someone, and the only lifetime the collector
 * understands is a Java one).  Entries hold 1/w scaled by 65536: larger is
 * nearer, and a freshly allocated array is all zero = "nothing drawn",
 * which is exactly the state a new buffer must be in.  It is cleared once
 * per frame so a title that never calls clear() still renders.
 */
typedef struct {
    nj_surface *surf;
    nj_s4 *z;
    int vx, vy, vw, vh;
    const nj_u1 *tex;
    int tex_w, tex_h, tex_bpp;
    int wrap_s, wrap_t;
    int cull;                  /* 160 CULL_BACK, 161 CULL_FRONT, 162 none */
    int winding;               /* 168 CCW, 169 CW */
    int texfunc;               /* 224 ADD .. 228 REPLACE; 227 MODULATE */
    int filt_image;            /* 128 NEAREST (the default) / 129 LINEAR */
    const nj_u1 *mips;         /* the level chain, RGBA, level 0 first */
    int mip_w, mip_h, mip_n;   /* its base size and how many levels it has */
    int blend;                 /* CompositingMode: 64 ALPHA .. 68 REPLACE */
    int athresh;               /* the alpha test, 0..255 */
    int awrite;                /* alphaWriteEnable */
    int dtest, dwrite;          /* independent CompositingMode depth controls */
    int sprite;                /* sprite-only post-texture alpha and fog */
    int sprite_alpha;
    int sprite_l, sprite_t, sprite_r, sprite_b;
    float sprite_fog;
    nj_u4 sprite_fog_colour;
} m3g_sw;

#define M3G_SW_BACK  160
#define M3G_SW_FRONT 161
#define M3G_SW_CCW   168

static int sw_depth(nj_vm *vm, nj_ref self, m3g_sw *sw)
{
    nj_field *fz = nj_field_lookup(vm, NJ_C_G3D, "_nj_zbuf", "[I", 0);
    nj_field *fw = nj_field_lookup(vm, NJ_C_G3D, "_nj_zw", "I", 0);
    nj_field *fh = nj_field_lookup(vm, NJ_C_G3D, "_nj_zh", "I", 0);
    nj_field *fr = nj_field_lookup(vm, NJ_C_G3D, "_nj_zframe", "I", 0);
    nj_size n = (nj_size)sw->vw * (nj_size)sw->vh;
    nj_ref a;
    nj_u4 mark;
    nj_s4 zw, zh, zf;
    nj_s4 *p;

    if (!fz || !fw || !fh || !fr || n == 0u) {
        return 0;
    }
    zw = nj_iget(vm, self, fw);
    zh = nj_iget(vm, self, fh);
    zf = nj_iget(vm, self, fr);
    a = nj_rget(vm, self, fz);
    if (!a || zw != sw->vw || zh != sw->vh || a->length < (nj_u4)n) {
        nj_ref na = nj_array_new(vm, "I", n);

        if (!na) {
            return 0;
        }
        mark = nj_root_mark(vm);
        na = nj_root(vm, na);
        nj_rput(vm, self, fz, na);
        nj_iput(vm, self, fw, sw->vw);
        nj_iput(vm, self, fh, sw->vh);
        a = na;
        nj_root_release(vm, mark);
        zf = -1;                       /* a fresh array is already "far" */
    }
    if (zf != (nj_s4)vm->frames) {     /* one clear per frame */
        p = (nj_s4 *)nj_array_data(a);
        if (p) {
            memset(p, 0, (size_t)n * sizeof(nj_s4));
        }
        nj_iput(vm, self, fr, (nj_s4)vm->frames);
    }
    sw->z = (nj_s4 *)nj_array_data(a);
    return sw->z != NULL;
}

/* The destination blend of CompositingMode.  Only reached when the source
   really is translucent (or when the mode multiplies regardless), so an
   opaque sprite still costs one store per pixel. */
static nj_u4 sw_blend(nj_u4 src, nj_u4 dst, int mode, int awrite)
{
    nj_u4 a = (src >> 24) & 0xFFu;
    nj_u4 inv = 255u - a;
    nj_u4 sr = (src >> 16) & 0xFFu, sg = (src >> 8) & 0xFFu, sb = src & 0xFFu;
    nj_u4 dr = (dst >> 16) & 0xFFu, dg = (dst >> 8) & 0xFFu, db = dst & 0xFFu;
    nj_u4 r, g, b, oa;

    if (mode == 65) {                     /* ALPHA_ADD: src*a + dst */
        r = (sr * a) / 255u + dr;
        g = (sg * a) / 255u + dg;
        b = (sb * a) / 255u + db;
    } else if (mode == 66 || mode == 67) {  /* MODULATE, MODULATE_X2 */
        nj_u4 m = (mode == 67) ? 2u : 1u;

        r = (sr * dr * m) / 255u;
        g = (sg * dg * m) / 255u;
        b = (sb * db * m) / 255u;
    } else {                              /* 64 ALPHA: src over dst */
        r = (sr * a + dr * inv) / 255u;
        g = (sg * a + dg * inv) / 255u;
        b = (sb * a + db * inv) / 255u;
    }
    if (r > 255u) { r = 255u; }
    if (g > 255u) { g = 255u; }
    if (b > 255u) { b = 255u; }
    if (!awrite) {
        oa = (dst >> 24) & 0xFFu;
    } else if (mode == 64) {
        oa = a + (((dst >> 24) & 0xFFu) * inv) / 255u;
        if (oa > 255u) { oa = 255u; }
    } else {
        oa = 255u;
    }
    return (oa << 24) | (r << 16) | (g << 8) | b;
}

/*
 * The level chain a texture is sampled through.
 *
 * The M3G file format gives a Texture2D one Image2D and no second level, so a
 * hardware sampler's level filter has nothing to choose from; a software
 * rasteriser can build the chain itself, which is the only way a receding
 * textured plane stops aliasing.  The chain is box-filtered from the image's
 * own RGBA pixels, kept as one byte[] on the Image2D (so it is built once and
 * freed with the object), and invalidated whenever the pixels change.
 */
static int m3g_mip_count(int w, int h)
{
    int n = 1;

    while (w > 1 || h > 1) {
        w = w > 1 ? w / 2 : 1;
        h = h > 1 ? h / 2 : 1;
        n++;
    }
    return n;
}

static void m3g_image_dirty(nj_vm *vm, nj_ref img)
{
    nj_rput(vm, img, m3g_fof(vm, NJ_C_IMAGE2D, "_nj_mips", "[B"), 0);
}

static nj_ref m3g_image_mips(nj_vm *vm, nj_ref img)
{
    nj_ref px, mips = 0;
    const nj_u1 *src;
    nj_u1 *dst;
    nj_s4 w, h, lw, lh, n, total, i;
    nj_u4 mark;

    if (!img) {
        return 0;
    }
    mips = nj_rget(vm, img, m3g_fof(vm, NJ_C_IMAGE2D, "_nj_mips", "[B"));
    if (mips) {
        return mips;               /* built once, freed with the image */
    }
    if (m3g_image_format(vm, img) != 100) {
        return 0;                  /* only RGBA pixels have a known layout */
    }
    w = nj_iget(vm, img, m3g_fof(vm, NJ_C_IMAGE2D, "_nj_w", "I"));
    h = nj_iget(vm, img, m3g_fof(vm, NJ_C_IMAGE2D, "_nj_h", "I"));
    px = nj_rget(vm, img, m3g_fof(vm, NJ_C_IMAGE2D, "_nj_pixels", "[B"));
    if (!px || w <= 0 || h <= 0) {
        return 0;
    }
    src = (const nj_u1 *)nj_array_data(px);
    if (!src || nj_array_length(px) < (nj_u4)(w * h * 4)) {
        return 0;
    }
    total = 0;
    for (lw = w, lh = h; ; lw = lw > 1 ? lw / 2 : 1, lh = lh > 1 ? lh / 2 : 1) {
        total += lw * lh * 4;
        if (lw <= 1 && lh <= 1) {
            break;
        }
    }
    mips = nj_array_new(vm, "B", (nj_u4)total);
    if (!mips) {
        return 0;
    }
    mark = nj_root_mark(vm);
    mips = nj_root(vm, mips);
    dst = (nj_u1 *)nj_array_data(mips);
    if (!dst) {
        nj_root_release(vm, mark);
        return 0;
    }
    memcpy(dst, src, (size_t)(w * h * 4));
    lw = w;
    lh = h;
    for (n = 0; (lw > 1 || lh > 1) && n < 16; n++) {
        nj_s4 nw = lw > 1 ? lw / 2 : 1, nh = lh > 1 ? lh / 2 : 1;
        const nj_u1 *up = dst;
        nj_u1 *to = dst + lw * lh * 4;
        nj_s4 x, y, c;

        for (y = 0; y < nh; y++) {
            for (x = 0; x < nw; x++) {
                for (c = 0; c < 4; c++) {
                    nj_s4 sx0 = x * 2, sy0 = y * 2;
                    nj_s4 sx1 = (sx0 + 1 < lw) ? sx0 + 1 : sx0;
                    nj_s4 sy1 = (sy0 + 1 < lh) ? sy0 + 1 : sy0;
                    nj_u4 v = up[((nj_size)sy0 * lw + sx0) * 4 + c]
                            + up[((nj_size)sy0 * lw + sx1) * 4 + c]
                            + up[((nj_size)sy1 * lw + sx0) * 4 + c]
                            + up[((nj_size)sy1 * lw + sx1) * 4 + c];

                    to[((nj_size)y * nw + x) * 4 + c] = (nj_u1)((v + 2u) / 4u);
                }
            }
        }
        dst = to;
        lw = nw;
        lh = nh;
    }
    /* One more pass to keep the loop's pointer arithmetic honest: the caller
       only needs the array, and the levels are found by walking from its start. */
    nj_rput(vm, img, m3g_fof(vm, NJ_C_IMAGE2D, "_nj_mips", "[B"), mips);
    nj_root_release(vm, mark);
    (void)i;
    return mips;
}

/* The level `lv` inside the chain, with its own size. */
static const nj_u1 *m3g_mip_level(const nj_u1 *mips, int w, int h, int lv,
                                  int *lw, int *lh)
{
    const nj_u1 *p = mips;
    int k;

    for (k = 0; k < lv; k++) {
        p += (nj_size)w * (nj_size)h * 4;
        w = w > 1 ? w / 2 : 1;
        h = h > 1 ? h / 2 : 1;
    }
    *lw = w;
    *lh = h;
    return p;
}

#if !NJ_M3G_FAST_PATHS
/* One texel as 0xAARRGGBB, for the filter below. */
static nj_u4 sw_texel_pack(const m3g_sw *sw, int u, int v)
{
    const nj_u1 *p = sw->tex + ((nj_size)v * (nj_size)sw->tex_w + (nj_size)u) *
                                (nj_size)sw->tex_bpp;
    nj_u4 a = (sw->tex_bpp == 4) ? (nj_u4)p[3] : 255u;

    return (a << 24) | ((nj_u4)p[0] << 16) | ((nj_u4)p[1] << 8) | (nj_u4)p[2];
}
#endif

static nj_u4 sw_texel(const m3g_sw *sw, float s, float t, nj_u4 argb)
{
    float fs, ft;
    int u, v;
    const nj_u1 *p;
    nj_u4 tr, tg, tb;
    nj_u4 ta = 255u;

    if (!sw->tex || sw->tex_w <= 0 || sw->tex_h <= 0) {
        return argb;
    }
    if (sw->wrap_s == 240) {            /* WRAP_CLAMP */
        fs = s < 0.0f ? 0.0f : (s > 1.0f ? 1.0f : s);
    } else {                           /* WRAP_REPEAT (the default) */
        fs = s - (float)(nj_s4)s;
        if (fs < 0.0f) { fs += 1.0f; }
        if (fs >= 1.0f) { fs = 0.999f; }
    }
    if (sw->wrap_t == 240) {
        ft = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
    } else {
        ft = t - (float)(nj_s4)t;
        if (ft < 0.0f) { ft += 1.0f; }
        if (ft >= 1.0f) { ft = 0.999f; }
    }
    if ((sw->filt_image == 209 || sw->filt_image == 129) &&
        sw->tex_w > 1 && sw->tex_h > 1) {
        /*
         * FILTER_LINEAR: the four texels around the sample point, weighted by
         * the distance to each.  The weights are 8-bit and sum to 65536, so
         * the blend is integer and the result is exact at the texel centres -
         * which is why a magnified texture stops showing the blocky steps the
         * nearest filter leaves, and why a minified one stops sparkling.
         */
        float x = fs * (float)(sw->tex_w - 1);
        float y = ft * (float)(sw->tex_h - 1);
        int u0 = (int)x, v0 = (int)y, u1, v1, fu, fv;
        nj_u4 w00, w10, w01, w11;
#if NJ_M3G_FAST_PATHS
        const nj_u1 *p00, *p10, *p01, *p11;
#else
        nj_u4 c00, c10, c01, c11;
#endif

        if (u0 < 0) { u0 = 0; }
        if (u0 >= sw->tex_w) { u0 = sw->tex_w - 1; }
        if (v0 < 0) { v0 = 0; }
        if (v0 >= sw->tex_h) { v0 = sw->tex_h - 1; }
        fu = (int)((x - (float)u0) * 256.0f + 0.5f);
        fv = (int)((y - (float)v0) * 256.0f + 0.5f);
        if (fu < 0) { fu = 0; }
        if (fu > 256) { fu = 256; }
        if (fv < 0) { fv = 0; }
        if (fv > 256) { fv = 256; }
        u1 = (u0 + 1 < sw->tex_w) ? u0 + 1 : u0;
        v1 = (v0 + 1 < sw->tex_h) ? v0 + 1 : v0;
        w00 = (nj_u4)(256 - fu) * (nj_u4)(256 - fv);
        w10 = (nj_u4)fu * (nj_u4)(256 - fv);
        w01 = (nj_u4)(256 - fu) * (nj_u4)fv;
        w11 = (nj_u4)fu * (nj_u4)fv;
#if NJ_M3G_FAST_PATHS
        /* Filter the stored channels directly; packing four ARGB words only
         * to unpack them again adds work to every linear sample. Preserve
         * the original weights and final shift, including RGB's alpha=255. */
        p00 = sw->tex + ((nj_size)v0 * (nj_size)sw->tex_w + (nj_size)u0) *
                        (nj_size)sw->tex_bpp;
        p10 = p00 + (nj_size)(u1 - u0) * (nj_size)sw->tex_bpp;
        p01 = p00 + (nj_size)(v1 - v0) * (nj_size)sw->tex_w * (nj_size)sw->tex_bpp;
        p11 = p01 + (nj_size)(u1 - u0) * (nj_size)sw->tex_bpp;
        tr = (p00[0] * w00 + p10[0] * w10 + p01[0] * w01 + p11[0] * w11) >> 16;
        tg = (p00[1] * w00 + p10[1] * w10 + p01[1] * w01 + p11[1] * w11) >> 16;
        tb = (p00[2] * w00 + p10[2] * w10 + p01[2] * w01 + p11[2] * w11) >> 16;
        if (sw->tex_bpp == 4) {
            ta = (p00[3] * w00 + p10[3] * w10 + p01[3] * w01 + p11[3] * w11) >> 16;
        }
#else
        c00 = sw_texel_pack(sw, u0, v0);
        c10 = sw_texel_pack(sw, u1, v0);
        c01 = sw_texel_pack(sw, u0, v1);
        c11 = sw_texel_pack(sw, u1, v1);
        tr = (nj_u1)(((((c00 >> 16) & 0xFFu) * w00 + ((c10 >> 16) & 0xFFu) * w10 +
                       ((c01 >> 16) & 0xFFu) * w01 + ((c11 >> 16) & 0xFFu) * w11) >> 16) & 0xFFu);
        tg = (nj_u1)(((((c00 >> 8) & 0xFFu) * w00 + ((c10 >> 8) & 0xFFu) * w10 +
                       ((c01 >> 8) & 0xFFu) * w01 + ((c11 >> 8) & 0xFFu) * w11) >> 16) & 0xFFu);
        tb = (nj_u1)((((c00 & 0xFFu) * w00 + (c10 & 0xFFu) * w10 +
                       (c01 & 0xFFu) * w01 + (c11 & 0xFFu) * w11) >> 16) & 0xFFu);
        ta = (nj_u1)(((((c00 >> 24) & 0xFFu) * w00 + ((c10 >> 24) & 0xFFu) * w10 +
                       ((c01 >> 24) & 0xFFu) * w01 + ((c11 >> 24) & 0xFFu) * w11) >> 16) & 0xFFu);
#endif
    } else {
        if (sw->sprite) {
            u = (int)(fs * (float)sw->tex_w);
            v = (int)(ft * (float)sw->tex_h);
            if (u < sw->sprite_l) { u = sw->sprite_l; }
            if (u >= sw->sprite_r) { u = sw->sprite_r - 1; }
            if (v < sw->sprite_t) { v = sw->sprite_t; }
            if (v >= sw->sprite_b) { v = sw->sprite_b - 1; }
        } else {
            u = (int)(fs * (float)(sw->tex_w - 1) + 0.5f);
            v = (int)(ft * (float)(sw->tex_h - 1) + 0.5f);
        }
        if (u < 0) { u = 0; }
        if (u >= sw->tex_w) { u = sw->tex_w - 1; }
        if (v < 0) { v = 0; }
        if (v >= sw->tex_h) { v = sw->tex_h - 1; }
        p = sw->tex + ((nj_size)v * (nj_size)sw->tex_w + (nj_size)u) *
                      (nj_size)sw->tex_bpp;
        tr = p[0]; tg = p[1]; tb = p[2];
        if (sw->tex_bpp == 4) {
            ta = p[3];           /* 100 rgba: four bytes per texel */
        }
    }
    /* FUNC_REPLACE and FUNC_DECAL take the texture's own colour; FUNC_MODULATE
       (the JSR default) tints it with the material colour, which is what the
       light or the vertex colour produced.  FUNC_ADD and FUNC_BLEND need a
       second texture unit, which this rasteriser does not have; MODULATE is
       the stand-in and docs/COMPATIBILITY.md says so. */
    if (sw->texfunc == 228 || sw->texfunc == 226) {
        return (ta << 24) | (tr << 16) | (tg << 8) | tb;
    }
    return (ta << 24)
         | (((tr * ((argb >> 16) & 0xFFu)) / 255u) << 16)
         | (((tg * ((argb >> 8) & 0xFFu)) / 255u) << 8)
         | ((tb * (argb & 0xFFu)) / 255u);
}

static nj_u4 sw_tri(m3g_sw *sw, const float xy[6], const float inv[3],
                    const float st[6], int has_tex, const nj_u4 rgb[3])
{
    float x0 = xy[0], y0 = xy[1], x1 = xy[2], y1 = xy[3], x2 = xy[4], y2 = xy[5];
    float area = (x1 - x0) * (y2 - y0) - (x2 - x0) * (y1 - y0);
    float ia;
    int cull = sw->cull ? sw->cull : M3G_SW_BACK;
    int wind = sw->winding ? sw->winding : M3G_SW_CCW;
    int back, minx, maxx, miny, maxy, px, py;
    float e0, e1, e2, g, h, k;
    float dxe0, dye0, dxe1, dye1, dxe2, dye2, dxg, dyg, dxh, dyh, dxk, dyk;
    nj_u4 *dst;
    nj_u4 drawn = 0u;
    /* Gouraud: the three vertex colours, interpolated like 1/w.  A flat
       triangle (the same colour three times, which is every unlit one) keeps
       the single-colour loop. */
    int gouraud = (rgb[0] != rgb[1] || rgb[1] != rgb[2]);
    nj_u4 flat = 0xFF000000u | (rgb[0] & 0x00FFFFFFu);
    float cr = 0.0f, cg = 0.0f, cb = 0.0f;
    float dxr = 0.0f, dyr = 0.0f, dxgc = 0.0f, dygc = 0.0f, dxb = 0.0f, dyb = 0.0f;
    float r0f = 0.0f, g0f = 0.0f, b0f = 0.0f;
    float r1f = 0.0f, g1f = 0.0f, b1f = 0.0f;
    float r2f = 0.0f, g2f = 0.0f, b2f = 0.0f;
#if NJ_M3G_FAST_PATHS
    int flat_opaque = !has_tex && !gouraud && !sw->sprite && sw->awrite &&
                      sw->athresh <= 255 && (sw->blend == 64 || sw->blend == 68);
#endif

    if (!sw->surf || !sw->z || !sw->surf->pixels) {
        return 0u;
    }
    if (sw->surf->format != NJ_PIXFMT_ARGB8888) {
        return 0u;
    }
    if (area == 0.0f) {
        return 0u;
    }
    /* In NDC (y up) the JSR's front face is counter-clockwise; the
       projection flips y for the screen, so a front face has a NEGATIVE
       signed area here.  tests/m3gdemo pins both directions: the triangle it
       renders is front-facing and must appear, and the same triangle with
       two of its vertices swapped is back-facing and must not. */
    back = (wind == M3G_SW_CCW) ? (area > 0.0f) : (area < 0.0f);
    if ((cull == M3G_SW_BACK && back) || (cull == M3G_SW_FRONT && !back)) {
        return 0u;
    }
    ia = 1.0f / area;
    minx = (int)floorf(x0 < x1 ? (x0 < x2 ? x0 : x2) : (x1 < x2 ? x1 : x2));
    maxx = (int)ceilf(x0 > x1 ? (x0 > x2 ? x0 : x2) : (x1 > x2 ? x1 : x2));
    miny = (int)floorf(y0 < y1 ? (y0 < y2 ? y0 : y2) : (y1 < y2 ? y1 : y2));
    maxy = (int)ceilf(y0 > y1 ? (y0 > y2 ? y0 : y2) : (y1 > y2 ? y1 : y2));
    if (minx < sw->vx) { minx = sw->vx; }
    if (miny < sw->vy) { miny = sw->vy; }
    if (maxx > sw->vx + sw->vw - 1) { maxx = sw->vx + sw->vw - 1; }
    if (maxy > sw->vy + sw->vh - 1) { maxy = sw->vy + sw->vh - 1; }
    if (minx > maxx || miny > maxy) {
        return 0u;
    }
    dst = (nj_u4 *)sw->surf->pixels;
    /* The edge functions are evaluated once per scanline and then stepped
       by a constant, which is what makes this affordable: a triangle that
       covers a third of a 240x320 screen used to cost three multiplies per
       pixel per edge.  The same trick covers the interpolation - s/w, t/w
       and 1/w are all linear in screen space, so each is one accumulator
       and one add per pixel. */
    dxe0 = -(y1 - y0); dye0 = (x1 - x0);
    dxe1 = -(y2 - y1); dye1 = (x2 - x1);
    dxe2 = -(y0 - y2); dye2 = (x0 - x2);
    e0 = (x1 - x0) * ((float)miny + 0.5f - y0) - (y1 - y0) * ((float)minx + 0.5f - x0);
    e1 = (x2 - x1) * ((float)miny + 0.5f - y1) - (y2 - y1) * ((float)minx + 0.5f - x1);
    e2 = (x0 - x2) * ((float)miny + 0.5f - y2) - (y0 - y2) * ((float)minx + 0.5f - x2);
    /* 1/w and the two texture coordinates, in the same stepping scheme. */
    g = e1 * inv[0] + e2 * inv[1] + e0 * inv[2];
    h = e1 * st[0] * inv[0] + e2 * st[2] * inv[1] + e0 * st[4] * inv[2];
    k = e1 * st[1] * inv[0] + e2 * st[3] * inv[1] + e0 * st[5] * inv[2];
    dxg = dxe1 * inv[0] + dxe2 * inv[1] + dxe0 * inv[2];
    dyg = dye1 * inv[0] + dye2 * inv[1] + dye0 * inv[2];
    dxh = dxe1 * st[0] * inv[0] + dxe2 * st[2] * inv[1] + dxe0 * st[4] * inv[2];
    dyh = dye1 * st[0] * inv[0] + dye2 * st[2] * inv[1] + dye0 * st[4] * inv[2];
    dxk = dxe1 * st[1] * inv[0] + dxe2 * st[3] * inv[1] + dxe0 * st[5] * inv[2];
    dyk = dye1 * st[1] * inv[0] + dye2 * st[3] * inv[1] + dye0 * st[5] * inv[2];
    if (gouraud) {
        /* e1 carries vertex 0, e2 vertex 1 and e0 vertex 2 - the pairing the
           texture coordinates and 1/w already use.  The colours are
           interpolated affinely, which is what Gouraud shading means. */
        r0f = (float)((rgb[0] >> 16) & 0xFFu);
        g0f = (float)((rgb[0] >> 8) & 0xFFu);
        b0f = (float)(rgb[0] & 0xFFu);
        r1f = (float)((rgb[1] >> 16) & 0xFFu);
        g1f = (float)((rgb[1] >> 8) & 0xFFu);
        b1f = (float)(rgb[1] & 0xFFu);
        r2f = (float)((rgb[2] >> 16) & 0xFFu);
        g2f = (float)((rgb[2] >> 8) & 0xFFu);
        b2f = (float)(rgb[2] & 0xFFu);
        cr = e1 * r0f + e2 * r1f + e0 * r2f;
        cg = e1 * g0f + e2 * g1f + e0 * g2f;
        cb = e1 * b0f + e2 * b1f + e0 * b2f;
        dxr = dxe1 * r0f + dxe2 * r1f + dxe0 * r2f;
        dyr = dye1 * r0f + dye2 * r1f + dye0 * r2f;
        dxgc = dxe1 * g0f + dxe2 * g1f + dxe0 * g2f;
        dygc = dye1 * g0f + dye2 * g1f + dye0 * g2f;
        dxb = dxe1 * b0f + dxe2 * b1f + dxe0 * b2f;
        dyb = dye1 * b0f + dye2 * b1f + dye0 * b2f;
    }
    for (py = miny; py <= maxy; py++) {
        float r0 = e0, r1 = e1, r2 = e2, rg = g, rh = h, rk = k;
        float rr = cr, rgc = cg, rbc = cb;
        float sgn = (area > 0.0f) ? 1.0f : -1.0f;
        float lo = (float)minx, hi = (float)maxx;
        int xl, xr, i;
        nj_size o;
        nj_u4 *drow;
        int empty = 0;

        /* The span this scanline's three edges allow, solved directly
           instead of testing every pixel of the bounding box: each edge is
           linear in x, so "on the inside" is one bound per edge. */
        for (i = 0; i < 3; i++) {
            float w = sgn * (i == 0 ? r0 : (i == 1 ? r1 : r2));
            float d = sgn * (i == 0 ? dxe0 : (i == 1 ? dxe1 : dxe2));

            if (d > 0.0f) {
                float x = (float)minx + (-w) / d;
                if (x > lo) { lo = x; }
            } else if (d < 0.0f) {
                float x = (float)minx + w / (-d);
                if (x < hi) { hi = x; }
            } else if (w < 0.0f) {
                empty = 1;
            }
        }
        if (!empty) {
            xl = (int)ceilf(lo - 0.5f);
            xr = (int)floorf(hi - 0.5f);
            if (xl < minx) { xl = minx; }
            if (xr > maxx) { xr = maxx; }
            if (xl > xr) { empty = 1; }
        }
        if (!empty) {
            float skip = (float)(xl - minx);
            float pg = rg + dxg * skip;
            float ph = rh + dxh * skip, pk = rk + dxk * skip;

            rr += dxr * skip;
            rgc += dxgc * skip;
            rbc += dxb * skip;
            o = (nj_size)(py - sw->vy) * (nj_size)sw->vw + (nj_size)(xl - sw->vx);
            drow = dst + (nj_size)py * (nj_size)sw->surf->width + (nj_size)xl;
#if NJ_M3G_FAST_PATHS
            /* The opaque, flat case needs only one interpolant. Keep its
             * expression and stepping order identical to the general loop:
             * reassociating the depth arithmetic can change a tie. */
            if (flat_opaque) {
                for (px = xl; px <= xr; px++, o++, drow++, pg += dxg) {
                    nj_s4 key = (nj_s4)(pg * (ia * 65536.0f));

                    if (key > 0 && (!sw->dtest || key > sw->z[o])) {
                        if (sw->dwrite) { sw->z[o] = key; }
                        *drow = flat;
                        drawn = 1u;
                    }
                }
            } else
#endif
            for (px = xl; px <= xr; px++, o++, drow++, pg += dxg, ph += dxh,
                     pk += dxk, rr += dxr, rgc += dxgc, rbc += dxb) {
                nj_s4 key = (nj_s4)(pg * (ia * 65536.0f));
                nj_u4 c, ca;

                if (sw->sprite) {
                    const float xx[3] = { x0, x1, x2 };
                    const float yy[3] = { y0, y1, y2 };
                    int edge, outside = 0;
                    for (edge = 0; edge < 3; edge++) {
                        int next = (edge + 1) % 3;
                        float dx = xx[next] - xx[edge], dy = yy[next] - yy[edge];
                        float e = dx * ((float)py + 0.5f - yy[edge]) -
                                  dy * ((float)px + 0.5f - xx[edge]);
                        if (area < 0.0f) { e = -e; dx = -dx; dy = -dy; }
                        if (e < 0.0f || (e == 0.0f &&
                            !(dy < 0.0f || (dy == 0.0f && dx > 0.0f)))) {
                            outside = 1; break;
                        }
                    }
                    if (outside) { continue; }
                }
                if (key <= 0) {
                    continue;
                }
                if (sw->dtest && key <= sw->z[o]) {
                    continue;
                }
                if (gouraud) {
                    c = 0xFF000000u |
                        ((nj_u4)(rr * ia + 0.5f) << 16) |
                        ((nj_u4)(rgc * ia + 0.5f) << 8) |
                        (nj_u4)(rbc * ia + 0.5f);
                } else {
                    c = flat;
                }
                if (has_tex) {
                    c = sw_texel(sw, ph / pg, pk / pg, c);
                }
                if (sw->sprite) {
                    nj_u4 a = ((c >> 24) * (nj_u4)sw->sprite_alpha + 127u) / 255u;
                    float f = sw->sprite_fog;
                    int ch;
                    nj_u4 fogged = a << 24;
                    for (ch = 0; ch < 3; ch++) {
                        int shift = 16 - 8 * ch;
                        float value = (float)((c >> shift) & 255u) * f +
                            (float)((sw->sprite_fog_colour >> shift) & 255u) * (1.0f - f);
                        fogged |= (nj_u4)(value + 0.5f) << shift;
                    }
                    c = fogged;
                }
                ca = (c >> 24) & 0xFFu;
                /* The alpha test is a test on the fragment, so a discarded
                   one leaves no depth behind - which is what lets a cut-out
                   sprite pass in front of the thing behind it. */
                if (sw->athresh > 0 && (nj_s4)ca < sw->athresh) {
                    continue;
                }
                if (sw->dwrite) { sw->z[o] = key; }
                if (sw->blend == 68) {              /* REPLACE */
                    *drow = sw->awrite ? c
                                       : ((c & 0x00FFFFFFu) | (*drow & 0xFF000000u));
                } else if (ca < 255u || sw->blend == 65 || sw->blend == 66 ||
                           sw->blend == 67) {
                    *drow = sw_blend(c, *drow, sw->blend, sw->awrite);
                } else {
                    *drow = sw->awrite ? c
                                       : ((c & 0x00FFFFFFu) | (*drow & 0xFF000000u));
                }
                drawn = 1u;
            }
        }
        e0 += dye0; e1 += dye1; e2 += dye2;
        g += dyg; h += dyh; k += dyk;
        cr += dyr; cg += dygc; cb += dyb;
    }
    return drawn;
}

/*
 * The JSR's render(VertexBuffer, IndexBuffer, Appearance, Transform, int) is
 * the path a modern title uses: the camera, the viewport and the target come
 * from the Graphics3D, the vertices from the buffer, and the index buffer
 * decides which three of them form a triangle.  Until this existed the call
 * answered 0 without drawing anything, which is the honest answer of a
 * runtime with no rasteriser - and also indistinguishable from a scene that
 * is off screen, which is why it needed fixing rather than documenting.
 *
 * What it does now: the full pipeline (model transform, camera view,
 * projection, viewport), a triangle list or a triangle strip, the material's
 * diffuse colour or the buffer's per-vertex colours, and the pixels through
 * the same Java call the World path uses (Graphics.fillTriangle).  What it
 * does NOT do yet, and says so here rather than in a failure: no depth buffer
 * (triangles land in submission order), no back-face culling, no texture, no
 * lighting.  render() answers the number of triangles it drew.
 */

/*
 * The JSR 184 caps a Graphics3D at eight lights; leave room for a title that
 * sets a few more slots than that and clamp whatever is beyond.  This is the
 * fixed size of the per-draw light table below.
 */
#define NJ_M3G_LIGHTS 16

typedef struct {
    nj_vm *vm;
    const nj_s2 *pos;
    const nj_s2 *col;
    nj_s4 pcomp, pfirst, ptype, pcount, ccomp, cfirst, ccount;
    float pfac;                   /* VertexBuffer's scale (stored as _nj_bias) */
    float pbias[4];               /* ... and its bias (stored as _nj_scale) */
    float mvp[16];
    float proj[16];               /* clip = proj * (camera space) */
    int vx, vy, vw, vh;
    m3g_java_target jt;
    nj_u4 dif;
    nj_u4 default_colour;
    nj_u4 drawn;
    const nj_s4 *idx;             /* the index buffer, when the mesh has one */
    nj_s4 icount;
    const nj_s4 *lens;            /* a strip array's strip lengths */
    nj_s4 nstrip;
    nj_s4 ifirst;
    m3g_sw sw;
    nj_surface scratch;        /* the target view sw.surf points into */                    /* the target surface, when reachable */
    const nj_s2 *tpos;            /* texture coordinates, unit 0 */
    nj_s4 tcomp, tfirst, tcount, ttype;
    float tscale;                 /* VertexBuffer's texCoordScale */
    const float *tmat;            /* Texture2D component/general transform */
    float tbias[4];               /* ... and its texCoordBias[3] */
    /*
     * Lighting.  JSR 184 lights a Material with the Light objects of the
     * Graphics3D: emission always, ambient from the ambient lights, and one
     * diffuse and specular term per remaining light, all evaluated per vertex
     * and interpolated across the triangle (Gouraud).  Without a Material or
     * without normals the buffer's colours are used as they are, which is
     * what the specification says an unlit appearance means.
     */
    const nj_s2 *nrm;             /* vertex normals, when the buffer has them */
    nj_s4 ncomp, nfirst, ncount, ntype;
    float mv[16];                 /* view * model: model space -> eye space */
    float view[16];               /* world -> eye, for the lights */
    int fog;                      /* a Fog is attached to the appearance */
    int fog_mode;                 /* 80 EXPONENTIAL, 81 LINEAR */
    nj_u4 fog_colour;
    float fog_density, fog_near, fog_far;
    nj_ref lights;                /* Graphics3D._nj_lights */
    nj_ref lmats;                 /* ... and one float[16] per light */
    const nj_ref *slights;        /* the World's own Light nodes, when given */
    const float *slmats;
    nj_s4 nlights;
    /*
     * The lights, read out of their Java objects once per draw.  jr_lit() runs
     * per vertex and per light, and a profile of Doom 3 showed its six
     * nj_field_lookup() calls and field reads there as the largest single cost
     * in the 3D path (29.6 M lookups for 3.3 M vertices).  Mode 0 means "no
     * light object at this index", which jr_lit() skips.
     */
    nj_u4 lmode[NJ_M3G_LIGHTS];
    nj_u4 lcol[NJ_M3G_LIGHTS];
    float lint[NJ_M3G_LIGHTS];
    float latt[NJ_M3G_LIGHTS][3];
    float lspot_angle[NJ_M3G_LIGHTS];
    float lspot_exp[NJ_M3G_LIGHTS];
    int lit;
    nj_u4 emi, amb, spc;          /* the material's other colours */
    float shin;
    int vcolor;
} m3g_jr;

/*
 * A matrix from Java, projected.  The matrices this path works with come
 * from the Java API - Camera.getProjection, setCamera's Transform, the
 * Transform handed to render() - and the runtime stores those ROW-MAJOR with
 * the translation in the last COLUMN (m[3], m[7], m[11]), which is what
 * m3g_xf_transform and mat_translate implement.  nj_m3g_project() reads the
 * transpose of that (the layout the M3G file loader produces), so this path
 * needs its own reader: the plain column-vector product, w from the fourth
 * row.  Mixing the two conventions is what made every model and view
 * transform silently do nothing.
 */
static void jr_project(const float m[16], const float v[3], int w, int h,
                       float out[2], float *inv)
{
    float x = m[0] * v[0] + m[1] * v[1] + m[2]  * v[2] + m[3];
    float y = m[4] * v[0] + m[5] * v[1] + m[6]  * v[2] + m[7];
    float ww = m[12] * v[0] + m[13] * v[1] + m[14] * v[2] + m[15];

    inv[0] = 0.0f;
    if (ww == 0.0f) {
        out[0] = 0.0f;               /* a vertex at infinity has no pixel */
        out[1] = 0.0f;
        return;
    }
    out[0] = (x / ww + 1.0f) * 0.5f * (float)w;
    out[1] = (1.0f - y / ww) * 0.5f * (float)h;
    if (ww > 1.0e-6f) { inv[0] = 1.0f / ww; }
}

static int jr_vertex(m3g_jr *r, nj_u4 v, float xy[2], float *inv, float st[2],
                     float pe[3], float ne[3])
{
    float p[4];
    nj_u4 k;
    const nj_s2 *s;

    inv[0] = 0.0f;
    st[0] = st[1] = 0.0f;
    p[0] = p[1] = p[2] = 0.0f;
    p[3] = 1.0f;
    pe[0] = pe[1] = pe[2] = 0.0f;
    ne[0] = ne[1] = ne[2] = 0.0f;
    if (!r->pos || v >= (nj_u4)r->pcount) {
        xy[0] = xy[1] = 0.0f;
        return 0;
    }
    s = r->pos + ((nj_size)r->pfirst + (nj_size)v) * (nj_size)r->pcomp;
    for (k = 0u; k < 4u && k < (nj_u4)r->pcomp; k++) {
        float raw = (float)s[k];

        /* A 4-byte component is 16.16 fixed point; the short store keeps the
           integer half, and that is what a render can honestly use. */
        if (r->ptype == 2) { raw = (float)(raw / 65536.0f); }
        p[k] = raw * r->pfac + r->pbias[k];
    }
    /* The clip-space w: the depth buffer keeps its reciprocal, because 1/w
       is what interpolates linearly across a projected triangle. */
    jr_project(r->mvp, p, r->vw, r->vh, xy, inv);
    /*
     * The eye-space position, which the lights need.  mv is view * model in
     * the Java layout jr_project reads (translation in m[3], m[7], m[11]).
     */
    /*
     * The camera-space position, which the lights and the fog need - and the
     * near-plane clip, which is what keeps a ground plane that passes behind
     * the camera from projecting to hundreds of thousands of pixels.  It is
     * computed for every vertex, lit or not, because the clip needs it.
     */
    pe[0] = r->mv[0] * p[0] + r->mv[1] * p[1] + r->mv[2]  * p[2] + r->mv[3];
    pe[1] = r->mv[4] * p[0] + r->mv[5] * p[1] + r->mv[6]  * p[2] + r->mv[7];
    pe[2] = r->mv[8] * p[0] + r->mv[9] * p[1] + r->mv[10] * p[2] + r->mv[11];
    if (r->lit && r->nrm && v < (nj_u4)r->ncount && r->ncomp >= 3) {
        const nj_s2 *q = r->nrm +
            ((nj_size)r->nfirst + (nj_size)v) * (nj_size)r->ncomp;
        float nx = (float)q[0], ny = (float)q[1], nz = (float)q[2];
        float len;

        if (r->ntype == 2) {
            nx /= 65536.0f; ny /= 65536.0f; nz /= 65536.0f;
        }
        /* Rotating a normal means the upper 3x3 of the model-view matrix.
           A title that only translates, rotates or scales uniformly gets the
           right answer; a non-uniform scale would need the inverse
           transpose, and is not attempted here. */
        ne[0] = r->mv[0] * nx + r->mv[1] * ny + r->mv[2]  * nz;
        ne[1] = r->mv[4] * nx + r->mv[5] * ny + r->mv[6]  * nz;
        ne[2] = r->mv[8] * nx + r->mv[9] * ny + r->mv[10] * nz;
        len = ne[0] * ne[0] + ne[1] * ne[1] + ne[2] * ne[2];
        if (len > 1.0e-12f) {
            len = 1.0f / (float)sqrt((double)len);
            ne[0] *= len; ne[1] *= len; ne[2] *= len;
        } else {
            ne[0] = ne[1] = ne[2] = 0.0f;
        }
    }
    /*
     * A buffer with no texture coordinates samples the first texel, and the
     * caller's array is not initialised: writing the zero here is what makes
     * that true instead of leaving whatever the stack held, which drew the
     * rally title's track as a moire of unrelated texels.
     */
    st[0] = 0.0f;
    st[1] = 0.0f;
    if (r->tpos && v < (nj_u4)r->tcount && r->tcomp >= 2) {
        const nj_s2 *q = r->tpos +
            ((nj_size)r->tfirst + (nj_size)v) * (nj_size)r->tcomp;
        float su = (float)q[0];
        float tv = (float)q[1];

        if (r->ttype == 2) { su /= 65536.0f; tv /= 65536.0f; }
        /* The buffer's scale and bias come last: they are the file's way of
           placing one texture across many meshes. */
        st[0] = su * r->tscale + r->tbias[0];
        st[1] = tv * r->tscale + r->tbias[1];
        if (r->tmat) {
            float u = st[0], vtex = st[1];
            st[0] = r->tmat[0] * u + r->tmat[1] * vtex + r->tmat[3];
            st[1] = r->tmat[4] * u + r->tmat[5] * vtex + r->tmat[7];
        }
    }
    return 1;
}

static nj_u4 jr_colour(m3g_jr *r, nj_u4 a, nj_u4 b, nj_u4 c)
{
    nj_u4 i;

    if (r->dif != 0xFFFFFFFFu) {
        return r->dif | 0xFF000000u;   /* a material colour is opaque */
    }
    if (!r->col || r->ccomp < 3) {
        return r->default_colour;
    }
    /* VertexArray colours are RGB or RGBA, with opaque alpha for RGB. */
    {
        nj_u4 sum[4] = { 0u, 0u, 0u, 0u };
        nj_u4 v[3];
        v[0] = a; v[1] = b; v[2] = c;
        for (i = 0u; i < 3u; i++) {
            const nj_s2 *s = r->col +
                ((nj_size)r->cfirst + (nj_size)v[i]) * (nj_size)r->ccomp;
            nj_u4 k;
            if (v[i] >= (nj_u4)r->ccount) { return r->default_colour; }
            for (k = 0u; k < (nj_u4)r->ccomp && k < 4u; k++) {
                sum[k] += (nj_u4)(nj_u1)(s[k] & 0xFF);
            }
            if (r->ccomp == 3) { sum[3] += 255u; }
        }
        return ((sum[3] / 3u) << 24) | ((sum[0] / 3u) << 16) |
               ((sum[1] / 3u) << 8) | (sum[2] / 3u);
    }
}

/*
 * The lighting equation, one vertex at a time.  JSR 184's model is: the
 * emission colour is always there; the ambient lights give material.ambient
 * times their colour; every other light gives material.diffuse times
 * max(0, N.L) and, when the material has a shininess, material.specular times
 * (N.H)^shininess - each scaled by the light's intensity, by its distance
 * attenuation and, for a spot, by its cone.  The viewer sits at the origin of
 * eye space, which is why the caller passes eye-space positions.
 *
 * What is not attempted: two-sided lighting (a back face is lit by the
 * material as it is), local vs infinite viewer (the specular term uses the
 * vertex-to-eye direction), and the per-light "vertex colour tracking"
 * nuances beyond multiplying ambient and diffuse by the vertex colour.
 */
static void jr_lit(m3g_jr *r, const float pe[3], const float ne[3],
                   nj_u4 vcol, float out[3])
{
    float acc[3];
    float mod[3];
    float mamb[3], mdif[3], mspc[3];
    float amb[3] = { 0.0f, 0.0f, 0.0f };
    float spec_scale = r->shin;
    nj_s4 i;

    if (spec_scale < 0.0f) { spec_scale = 0.0f; }
    if (spec_scale > 128.0f) { spec_scale = 128.0f; }

    mod[0] = mod[1] = mod[2] = 1.0f;
    if (r->vcolor) {
        float vr = (float)((vcol >> 16) & 0xFFu) / 255.0f;
        float vg = (float)((vcol >> 8) & 0xFFu) / 255.0f;
        float vb = (float)(vcol & 0xFFu) / 255.0f;

        if (vr > 0.0f || vg > 0.0f || vb > 0.0f) {
            mod[0] = vr; mod[1] = vg; mod[2] = vb;
        }
    }
    mamb[0] = (float)((r->amb >> 16) & 0xFFu) / 255.0f * mod[0];
    mamb[1] = (float)((r->amb >> 8) & 0xFFu) / 255.0f * mod[1];
    mamb[2] = (float)(r->amb & 0xFFu) / 255.0f * mod[2];
    mdif[0] = (float)((r->dif >> 16) & 0xFFu) / 255.0f * mod[0];
    mdif[1] = (float)((r->dif >> 8) & 0xFFu) / 255.0f * mod[1];
    mdif[2] = (float)(r->dif & 0xFFu) / 255.0f * mod[2];
    mspc[0] = (float)((r->spc >> 16) & 0xFFu) / 255.0f;
    mspc[1] = (float)((r->spc >> 8) & 0xFFu) / 255.0f;
    mspc[2] = (float)(r->spc & 0xFFu) / 255.0f;

    acc[0] = (float)((r->emi >> 16) & 0xFFu);
    acc[1] = (float)((r->emi >> 8) & 0xFFu);
    acc[2] = (float)(r->emi & 0xFFu);

    /* First pass: the ambient lights, whose colour is the ambient light. */
    for (i = 0; i < r->nlights; i++) {
        nj_u4 col;
        float inten;

        if (r->lmode[i] != 128u) { continue; }   /* not an AMBIENT light */
        col = r->lcol[i];
        inten = r->lint[i];
        amb[0] += (float)((col >> 16) & 0xFFu) / 255.0f * inten;
        amb[1] += (float)((col >> 8) & 0xFFu) / 255.0f * inten;
        amb[2] += (float)(col & 0xFFu) / 255.0f * inten;
    }
    acc[0] += mamb[0] * amb[0] * 255.0f;
    acc[1] += mamb[1] * amb[1] * 255.0f;
    acc[2] += mamb[2] * amb[2] * 255.0f;

    /* Second pass: everything else, brightest first is not needed - the
       terms add up. */
    for (i = 0; i < r->nlights; i++) {
        const float *xf = r->slmats
            ? r->slmats + (nj_size)i * 16u
            : ((r->lmats &&
               nj_array_length(r->lmats) >= (nj_u4)(i + 1) * 16u)
               ? (const float *)nj_array_data(r->lmats) + (nj_size)i * 16u
               : NULL);
        const float *att;
        nj_u4 col;
        float inten, spot = 1.0f;
        float lx, ly, lz, dist = 0.0f, ndl;
        float lr, lg, lb;
        int mode = (int)r->lmode[i];

        if (mode == 0) { continue; }             /* no light object at this index */
        if (mode == 128) { continue; }           /* ambient, done above */
        col   = r->lcol[i];
        inten = r->lint[i];
        att   = r->latt[i];
        /*
         * The light's transform is in world space.  A directional light
         * shines along the negative z axis of that transform; a positional
         * light sits at its origin.  Both are brought into eye space with
         * the view matrix, the same convention the camera's own matrix uses.
         */
        {
            float lw[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
            float le[4];
            float dx, dy, dz;

            if (xf) {
                const float *m = xf;
                /* The transform's origin and z axis, read the way
                   nj_m3g_project() reads a matrix (the same convention the
                   camera's own matrix is applied with, so a light and the
                   geometry agree about where the world is). */
                /* Origin m[3], m[7], m[11] and z axis m[2], m[6], m[10]:
                   the Java layout jr_project reads, not the transposed one
                   nj_m3g_project uses. */
                lw[0] = m[3]; lw[1] = m[7]; lw[2] = m[11];
                le[0] = r->view[0] * lw[0] + r->view[1] * lw[1] +
                        r->view[2] * lw[2] + r->view[3];
                le[1] = r->view[4] * lw[0] + r->view[5] * lw[1] +
                        r->view[6] * lw[2] + r->view[7];
                le[2] = r->view[8] * lw[0] + r->view[9] * lw[1] +
                        r->view[10] * lw[2] + r->view[11];
                /* The light's z axis, rotated into eye space. */
                dx = r->view[0] * m[2] + r->view[1] * m[6] + r->view[2]  * m[10];
                dy = r->view[4] * m[2] + r->view[5] * m[6] + r->view[6]  * m[10];
                dz = r->view[8] * m[2] + r->view[9] * m[6] + r->view[10] * m[10];
            } else {
                /* No transform: the identity, whose z axis is +z. */
                le[0] = 0.0f; le[1] = 0.0f; le[2] = 0.0f;
                dx = 0.0f; dy = 0.0f; dz = 1.0f;
            }
            if (mode == 129) {                            /* DIRECTIONAL */
                /* A light shines along the NEGATIVE z axis of its own
                   transform, so the direction towards it is +z. */
                lx = dx; ly = dy; lz = dz;
            } else {                                      /* OMNI and SPOT */
                float dl = (float)sqrt((double)(dx * dx + dy * dy + dz * dz));

                lx = le[0] - pe[0];
                ly = le[1] - pe[1];
                lz = le[2] - pe[2];
                dist = (float)sqrt((double)(lx * lx + ly * ly + lz * lz));
                if (dl > 0.0f) { dx /= dl; dy /= dl; dz /= dl; }
                if (dist > 0.0f) {
                    float cosang;

                    if (mode == 131) {                    /* SPOT */
                        float half_angle = r->lspot_angle[i] * 0.5f;
                        float limit = (float)cos((double)(half_angle * 0.017453292f));
                        float ex = r->lspot_exp[i];

                        cosang = (lx * dx + ly * dy + lz * dz) / dist;
                        if (cosang <= limit) {
                            continue;                     /* outside the cone */
                        }
                        if (cosang < 1.0f && ex > 0.0f) {
                            spot = (float)pow((double)cosang, (double)ex);
                        }
                    }
                }
            }
            {
                float ll = (float)sqrt((double)(lx * lx + ly * ly + lz * lz));

                if (ll <= 1.0e-9f) { continue; }
                lx /= ll; ly /= ll; lz /= ll;
            }
            ndl = ne[0] * lx + ne[1] * ly + ne[2] * lz;
            if (ndl <= 0.0f) { continue; }                /* facing away */
            lr = (float)((col >> 16) & 0xFFu) / 255.0f * inten;
            lg = (float)((col >> 8) & 0xFFu) / 255.0f * inten;
            lb = (float)(col & 0xFFu) / 255.0f * inten;
            if (mode != 129) {
                float den = att[0] + att[1] * dist + att[2] * dist * dist;

                if (den > 0.0f) { lr /= den; lg /= den; lb /= den; }
            }
            lr *= spot; lg *= spot; lb *= spot;
            acc[0] += mdif[0] * lr * ndl * 255.0f;
            acc[1] += mdif[1] * lg * ndl * 255.0f;
            acc[2] += mdif[2] * lb * ndl * 255.0f;
            if (spec_scale > 0.0f) {
                float vx = -pe[0], vy = -pe[1], vz = -pe[2];
                float vl = (float)sqrt((double)(vx * vx + vy * vy + vz * vz));
                float hx, hy, hz, hl, ndh, sp;

                if (vl > 1.0e-9f) {
                    vx /= vl; vy /= vl; vz /= vl;
                    hx = lx + vx; hy = ly + vy; hz = lz + vz;
                    hl = (float)sqrt((double)(hx * hx + hy * hy + hz * hz));
                    if (hl > 1.0e-9f) {
                        hx /= hl; hy /= hl; hz /= hl;
                        ndh = ne[0] * hx + ne[1] * hy + ne[2] * hz;
                        if (ndh > 0.0f) {
                            sp = (float)pow((double)ndh, (double)spec_scale);
                            acc[0] += mspc[0] * lr * sp * 255.0f;
                            acc[1] += mspc[1] * lg * sp * 255.0f;
                            acc[2] += mspc[2] * lb * sp * 255.0f;
                        }
                    }
                }
            }
        }
    }
    out[0] = acc[0] < 0.0f ? 0.0f : (acc[0] > 255.0f ? 255.0f : acc[0]);
    out[1] = acc[1] < 0.0f ? 0.0f : (acc[1] > 255.0f ? 255.0f : acc[1]);
    out[2] = acc[2] < 0.0f ? 0.0f : (acc[2] > 255.0f ? 255.0f : acc[2]);
}

/* Fog is applied to the colour the vertex already has, per vertex: GL ES 1.x
   computes the fog factor per vertex and interpolates it, and a title that
   wants the horizon to fade is served by that.  f == 1 is "no fog". */
static void jr_fog(m3g_jr *r, const float pe[3], float col[3])
{
    float d, f, span;
    int i;

    if (!r->fog) {
        return;
    }
    /* The depth along the view axis, which is what GL ES's default fog
       coordinate (FRAGMENT_DEPTH) uses: a vertex 5 units in front of the
       camera is 5 units into the fog, wherever it is on the screen. */
    d = fabsf(pe[2]);
    if (r->fog_mode == 81) {              /* LINEAR */
        span = r->fog_far - r->fog_near;
        f = (span > 0.0f) ? ((r->fog_far - d) / span) : 1.0f;
    } else {                              /* EXPONENTIAL */
        f = expf(-r->fog_density * d);
    }
    if (f < 0.0f) { f = 0.0f; }
    if (f > 1.0f) { f = 1.0f; }
    for (i = 0; i < 3; i++) {
        float fc = (float)((r->fog_colour >> (16 - i * 8)) & 0xFFu);

        col[i] = col[i] * f + fc * (1.0f - f);
    }
}

/*
 * One vertex, ready to rasterise: its screen position, its texture
 * coordinates, its camera-space position and normal (what the lighting and
 * the near-plane clip need), and the colour it carries.
 */
typedef struct {
    float xy[2];
    float inv;                     /* 1 / w */
    float st[2];
    float pe[3];                   /* camera space: z is negative in front */
    float ne[3];
    nj_u4 rgb;
} jr_vtx;

static int jr_fetch(m3g_jr *r, nj_u4 v, jr_vtx *o)
{
    float col[3];
    nj_u4 argb;

    o->xy[0] = o->xy[1] = 0.0f;
    o->inv = 0.0f;
    o->st[0] = o->st[1] = 0.0f;
    o->pe[0] = o->pe[1] = o->pe[2] = 0.0f;
    o->ne[0] = o->ne[1] = o->ne[2] = 0.0f;
    o->rgb = 0u;
    if (!jr_vertex(r, v, o->xy, &o->inv, o->st, o->pe, o->ne)) {
        return 0;
    }
    if (r->lit) {
        nj_u4 vc = r->default_colour;

        if (r->col && r->ccomp >= 3 && v < (nj_u4)r->ccount) {
            const nj_s2 *s = r->col +
                ((nj_size)r->cfirst + (nj_size)v) * (nj_size)r->ccomp;

            vc = ((nj_u4)(nj_u1)(s[0] & 0xFF) << 16) |
                 ((nj_u4)(nj_u1)(s[1] & 0xFF) << 8) |
                 (nj_u4)(nj_u1)(s[2] & 0xFF);
        }
        jr_lit(r, o->pe, o->ne, vc, col);
        jr_fog(r, o->pe, col);
    } else {
        argb = jr_colour(r, v, v, v);
        col[0] = (float)((argb >> 16) & 0xFFu);
        col[1] = (float)((argb >> 8) & 0xFFu);
        col[2] = (float)(argb & 0xFFu);
        jr_fog(r, o->pe, col);
    }
    o->rgb = ((nj_u4)(col[0] + 0.5f) << 16) |
             ((nj_u4)(col[1] + 0.5f) << 8) |
             (nj_u4)(col[2] + 0.5f);
    return 1;
}

/*
 * A point on a clip plane.  Every attribute is linear in camera space, so
 * the cut vertex is a weighted sum of the two ends - and its screen position
 * comes from projecting the interpolated camera-space point, not from
 * interpolating two projected ones (which is not linear).
 */
static void jr_blend(m3g_jr *r, const jr_vtx *a, const jr_vtx *b, float t,
                     jr_vtx *o)
{
    float c0[3], c1[3];
    int i;

    for (i = 0; i < 2; i++) {
        o->st[i] = a->st[i] + (b->st[i] - a->st[i]) * t;
    }
    for (i = 0; i < 3; i++) {
        o->pe[i] = a->pe[i] + (b->pe[i] - a->pe[i]) * t;
        o->ne[i] = a->ne[i] + (b->ne[i] - a->ne[i]) * t;
    }
    c0[0] = (float)((a->rgb >> 16) & 0xFFu);
    c0[1] = (float)((a->rgb >> 8) & 0xFFu);
    c0[2] = (float)(a->rgb & 0xFFu);
    c1[0] = (float)((b->rgb >> 16) & 0xFFu);
    c1[1] = (float)((b->rgb >> 8) & 0xFFu);
    c1[2] = (float)(b->rgb & 0xFFu);
    o->rgb = ((nj_u4)(c0[0] + (c1[0] - c0[0]) * t + 0.5f) << 16) |
             ((nj_u4)(c0[1] + (c1[1] - c0[1]) * t + 0.5f) << 8) |
             (nj_u4)(c0[2] + (c1[2] - c0[2]) * t + 0.5f);
    jr_project(r->proj, o->pe, r->vw, r->vh, o->xy, &o->inv);
}

static void jr_emit(m3g_jr *r, const jr_vtx *v0, const jr_vtx *v1,
                    const jr_vtx *v2)
{
    float xy[6], st[6], inv[3];
    nj_u4 rgb[3];

    xy[0] = v0->xy[0]; xy[1] = v0->xy[1];
    xy[2] = v1->xy[0]; xy[3] = v1->xy[1];
    xy[4] = v2->xy[0]; xy[5] = v2->xy[1];
    st[0] = v0->st[0]; st[1] = v0->st[1];
    st[2] = v1->st[0]; st[3] = v1->st[1];
    st[4] = v2->st[0]; st[5] = v2->st[1];
    inv[0] = v0->inv; inv[1] = v1->inv; inv[2] = v2->inv;
    rgb[0] = v0->rgb; rgb[1] = v1->rgb; rgb[2] = v2->rgb;
    if (r->sw.surf) {
        /* How many texels does one pixel cover?  The triangle's screen area
           and its area in texture space answer that, and the answer picks the
           level: 0 for a magnified texture, higher for a receding one, which
           is what stops the far field from sparkling.  Per triangle, not per
           fragment: a triangle small enough to alias is a triangle whose
           pixels all want the same level, and this rasteriser has no room for
           a derivative per pixel. */
        if (r->sw.mips && r->tpos != NULL && r->sw.mip_n > 1) {
            float sa = (xy[2] - xy[0]) * (xy[5] - xy[1]) -
                       (xy[4] - xy[0]) * (xy[3] - xy[1]);
            float ta = (st[2] - st[0]) * (st[5] - st[1]) -
                       (st[4] - st[0]) * (st[3] - st[1]);
            float ratio;
            int lv, lw, lh;
            const nj_u1 *p;

            if (sa < 0.0f) { sa = -sa; }
            if (ta < 0.0f) { ta = -ta; }
            sa *= 0.5f;
            ta *= 0.5f * (float)(r->sw.mip_w * r->sw.mip_h);
            ratio = ta / (sa > 0.25f ? sa : 0.25f);
            lv = 0;
            while (ratio > 4.0f && lv < r->sw.mip_n - 1) {
                ratio *= 0.25f;    /* one level is half the size each way */
                lv++;
            }
            p = m3g_mip_level(r->sw.mips, r->sw.mip_w, r->sw.mip_h, lv, &lw, &lh);
            if (p) {
                r->sw.tex = p;
                r->sw.tex_w = lw;
                r->sw.tex_h = lh;
                r->sw.tex_bpp = 4;   /* the chain is RGBA at every level */
            }
        }
        /* The rasteriser writes the pixels: depth, texture, culling. */
        if (sw_tri(&r->sw, xy, inv, st, r->sw.tex != NULL && (r->tpos != NULL || r->sw.sprite),
                   rgb) != 0u) {
            r->drawn++;
        }
        return;
    }
    if (emit_to_java(&r->jt, xy, 0xFF000000u | v0->rgb) != 0u) {
        r->drawn++;
    }
}

/* The projection's depth half-spaces, in camera coordinates. Using the
 * actual z and w rows also handles parallel and caller-supplied projections. */
static float jr_clip_distance(const float m[16], const jr_vtx *v, int plane)
{
    const float *p = v->pe;
    float z = m[8] * p[0] + m[9] * p[1] + m[10] * p[2] + m[11];
    float w = m[12] * p[0] + m[13] * p[1] + m[14] * p[2] + m[15];

    return plane == 0 ? w + z : w - z;
}

/* Clip before rasterisation: jr_project only divides x/y by w. Clipping a
 * convex polygon against one plane adds at most one vertex, so a triangle
 * needs at most four vertices after near and five after far. Both workspaces
 * are bounded stack arrays; jr_blend interpolates attributes and reprojects
 * every new camera-space intersection. */
static void jr_triangle_v(m3g_jr *r, const jr_vtx *v0, const jr_vtx *v1,
                          const jr_vtx *v2)
{
    jr_vtx poly[2][5];
    int n = 3, plane, k;

#if NJ_M3G_FAST_PATHS
    /* Most submitted triangles are already inside both depth planes. The
     * clipping passes would only copy these vertices, in this same order.
     * Keep the original comparisons (including points exactly on a plane)
     * and leave every crossing triangle to the general clipper below. */
    if (jr_clip_distance(r->proj, v0, 0) >= 0.0f &&
        jr_clip_distance(r->proj, v1, 0) >= 0.0f &&
        jr_clip_distance(r->proj, v2, 0) >= 0.0f &&
        jr_clip_distance(r->proj, v0, 1) >= 0.0f &&
        jr_clip_distance(r->proj, v1, 1) >= 0.0f &&
        jr_clip_distance(r->proj, v2, 1) >= 0.0f) {
        jr_emit(r, v0, v1, v2);
        return;
    }
#endif
    poly[0][0] = *v0; poly[0][1] = *v1; poly[0][2] = *v2;
    for (plane = 0; plane < 2; plane++) {
        const jr_vtx *src = poly[plane];
        jr_vtx *dst = poly[1 - plane];
        int count = 0;

        for (k = 0; k < n; k++) {
            int j = (k + 1) % n;
            float d0 = jr_clip_distance(r->proj, &src[k], plane);
            float d1 = jr_clip_distance(r->proj, &src[j], plane);
            int in0 = d0 >= 0.0f, in1 = d1 >= 0.0f;

            if (in0) {
                dst[count++] = src[k];
            }
            if (in0 != in1) {
                float t = d0 / (d0 - d1);

                if (t < 0.0f) { t = 0.0f; }
                if (t > 1.0f) { t = 1.0f; }
                jr_blend(r, &src[k], &src[j], t, &dst[count++]);
            }
        }
        n = count;
        if (n < 3) {
            return;
        }
    }
    for (k = 2; k < n; k++) {
        jr_emit(r, &poly[0][0], &poly[0][k - 1], &poly[0][k]);
    }
}

static void jr_triangle(m3g_jr *r, nj_u4 a, nj_u4 b, nj_u4 c)
{
    jr_vtx v[3];

    if (a == b || b == c || a == c) {
        return;                    /* a degenerate triangle draws nothing */
    }
    if (!jr_fetch(r, a, &v[0]) || !jr_fetch(r, b, &v[1]) ||
        !jr_fetch(r, c, &v[2])) {
        return;
    }
    jr_triangle_v(r, &v[0], &v[1], &v[2]);
}

static void jr_strip(m3g_jr *r, const nj_s4 *idx, nj_s4 count, nj_s4 first)
{
    nj_s4 i;

    for (i = 0; i + 3 <= count; i++) {
        nj_s4 v0 = idx ? idx[first + i] : (first + i);
        nj_s4 v1 = idx ? idx[first + i + 1] : (first + i + 1);
        nj_s4 v2 = idx ? idx[first + i + 2] : (first + i + 2);

        /* A strip walks its vertices in order, so consecutive triangles come
           out with OPPOSITE winding unless the odd ones swap their first two
           vertices - which is exactly what a strip rasteriser does. */
        if (i & 1) {
            jr_triangle(r, (nj_u4)v1, (nj_u4)v0, (nj_u4)v2);
        } else {
            jr_triangle(r, (nj_u4)v0, (nj_u4)v1, (nj_u4)v2);
        }
    }
}

static void jr_list(m3g_jr *r, const nj_s4 *idx, nj_s4 count, nj_s4 first)
{
    nj_s4 i;

    for (i = 0; i + 3 <= count; i += 3) {
        nj_s4 v0 = idx ? idx[first + i] : (first + i);
        nj_s4 v1 = idx ? idx[first + i + 1] : (first + i + 1);
        nj_s4 v2 = idx ? idx[first + i + 2] : (first + i + 2);

        jr_triangle(r, (nj_u4)v0, (nj_u4)v1, (nj_u4)v2);
    }
}

/*
 * Everything the software pipeline needs, gathered from the Graphics3D (the
 * target, the viewport, the camera and the lights), the VertexBuffer, the
 * index buffer, the Appearance and the model matrix.  The World path and the
 * immediate render(VertexBuffer, IndexBuffer, Appearance, Transform, int)
 * path differ only in where those come from, so both build this same state
 * and then hand the triangles to jr_draw().
 *
 * A 1 means the state is complete and the caller may draw.  A 0 means the
 * scene cannot be drawn at all - no target, no camera, no positions - which
 * the JSR answers with a render that draws nothing rather than an exception.
 */
static int jr_setup(nj_vm *vm, nj_ref self, nj_ref vb, nj_ref ib, nj_ref app,
                    nj_ref world, const nj_ref *slights, const float *slmats,
                    nj_s4 snlights, const float *model_in, m3g_jr *out)
{
    nj_ref target, g, cam, arr, d, mat = 0;
    int world_cam = 0;
    const char *cn;
    nj_s4 nverts, icount = 0, ifirst = 0;
    const nj_s4 *idx = NULL, *lens = NULL;
    nj_s4 nstrip = 0;
    float view[16], proj[16], model[16];
    float id[16];
    m3g_jr r;

    memset(&r, 0, sizeof(r));
    target = nj_rget(vm, self, m3g_fof(vm, NJ_C_G3D, "_nj_target",
                                       "Ljava/lang/Object;"));
    if (!target || !nj_heap_owns(vm, target)) {
        return 0;                            /* nothing bound, nothing drawn */
    }
    g = target;
    cn = nj_ref_class_name(g);
    if (!cn || strcmp(cn, "javax/microedition/lcdui/Graphics") != 0) {
        nj_value got;
        if (!cn || strcmp(cn, "javax/microedition/lcdui/Image") != 0) {
            return 0;                        /* neither a Graphics nor an Image */
        }
        if (vb) {
            got.r = 0;
            if (nj_call(vm, g, "javax/microedition/lcdui/Image", "getGraphics",
                        "()Ljavax/microedition/lcdui/Graphics;", NULL, &got) != NJ_OK
                || !got.r) {
                return 0;
            }
            g = got.r;
        } /* Sprites draw to the native surface without a Java Graphics. */
    }

    r.vm = vm;
    r.vx = (int)nj_iget(vm, self, m3g_fof(vm, NJ_C_G3D, "_nj_vx", "I"));
    r.vy = (int)nj_iget(vm, self, m3g_fof(vm, NJ_C_G3D, "_nj_vy", "I"));
    r.vw = (int)nj_iget(vm, self, m3g_fof(vm, NJ_C_G3D, "_nj_vw", "I"));
    r.vh = (int)nj_iget(vm, self, m3g_fof(vm, NJ_C_G3D, "_nj_vh", "I"));
    if (r.vw <= 0 || r.vh <= 0) {
        int tw = 0, th = 0;           /* no setViewport: the whole target */
        if (!vb && cn && strcmp(cn, "javax/microedition/lcdui/Image") == 0) {
            nj_surface scratch;
            nj_surface *surface = nj_midp_surface(vm, target, &scratch);
            if (!surface) { return 0; }
            tw = surface->width; th = surface->height;
        } else if (m3g_target_graphics(vm, g, &tw, &th) == 0) {
            return 0;
        }
        if (tw <= 0 || th <= 0) { return 0; }
        r.vx = r.vy = 0;
        r.vw = tw;
        r.vh = th;
    }

    /* The surface the pixels go into.  nj_midp_surface() answers the whole
       image, or the screen for a null target; the Java call stays the
       fallback for a target this cannot reach. */
    memset(&r.sw, 0, sizeof(r.sw));
    {
        const char *tcn = nj_ref_class_name(target);
        nj_ref img = 0;
        nj_surface *s;

        if (tcn && strcmp(tcn, "javax/microedition/lcdui/Image") == 0) {
            img = target;
        } else if (tcn && strcmp(tcn, "javax/microedition/lcdui/Graphics") == 0) {
            nj_field *gf = nj_field_lookup(vm, "javax/microedition/lcdui/Graphics",
                                           "gTarget",
                                           "Ljavax/microedition/lcdui/Image;", 0);
            if (gf) { img = nj_rget(vm, target, gf); }
        }
        /*
         * The view must outlive this call: jr_draw runs after jr_setup has
         * returned, and a scratch living on jr_setup's own frame turns into
         * whatever the next call puts there.  It lives in the draw state.
         */
        s = nj_midp_surface(vm, img, &r.scratch);
        /*
         * The software rasteriser writes 32-bit pixels, so a 16-bit target
         * cannot go through it - and a renderer that quietly dropped every
         * triangle on such a target would look exactly like a scene that is
         * off screen.  sw.surf stays NULL there, which sends the triangles to
         * the Java emitter instead: no depth buffer and no texture, but the
         * geometry appears, and that is the honest trade for a 565 screen.
         */
        if (s && s->format == NJ_PIXFMT_ARGB8888 &&
            r.vx >= 0 && r.vy >= 0 && r.vx + r.vw <= s->width &&
            r.vy + r.vh <= s->height) {
            r.sw.surf = s;
            r.sw.vx = r.vx;
            r.sw.vy = r.vy;
            r.sw.vw = r.vw;
            r.sw.vh = r.vh;
            if (!sw_depth(vm, self, &r.sw)) {
                r.sw.surf = NULL;      /* no depth buffer, no depth test */
            }
        }
    }

    memset(view, 0, sizeof(view));
    memset(model, 0, sizeof(model));
    view[0] = view[5] = view[10] = view[15] = 1.0f;
    model[0] = model[5] = model[10] = model[15] = 1.0f;
    id[0] = id[5] = id[10] = id[15] = 1.0f;
    cam = nj_rget(vm, self, m3g_fof(vm, NJ_C_G3D, "_nj_camera",
                                    "L" NJ_C_CAMERA ";"));
    if (world && nj_ref_class_name(world) &&
        strcmp(nj_ref_class_name(world), NJ_C_WORLD) == 0) {
        /*
         * World.setActiveCamera() names the camera for render(World), and it
         * wins over one assigned with setCamera(): a title that has both (The
         * Run does) sets the camera node's transform every frame and renders
         * the World, and reading setCamera's older Transform instead left the
         * camera where the last menu put it.  The World's camera is placed by
         * its node chain, so setCamera's matrix must not be used for it.
         */
        nj_ref wc = nj_rget(vm, world, m3g_fof(vm, NJ_C_WORLD, "_nj_cam",
                                               "L" NJ_C_CAMERA ";"));
        if (wc) {
            cam = wc;
            world_cam = 1;
        }
    }

    arr = world_cam ? NULL
                    : nj_rget(vm, self, m3g_fof(vm, NJ_C_G3D, "_nj_cam_m", "[F"));
    if ((!arr || nj_array_length(arr) < 16) && cam) {
        float cw[16];
        int gotw = 0;

        /*
         * No setCamera() Transform: the camera is a node in the scene, and
         * what places it is its matrix relative to the World - the chain from
         * the World down to it, because a chase camera is a child of the
         * group that carries the car.  The node's own matrix is the fallback
         * for a scene where the camera is not in the World's graph at all.
         */
        gotw = (world && cam_walk(vm, world, cam, cw, 0)) ? 1 : 0;
        if (gotw) {
            memcpy(view, cw, sizeof(view));
            mat_invert(view);
            arr = NULL;
        } else {
            arr = nj_rget(vm, cam, m3g_fof(vm, NJ_C_XABLE, "_nj_m", "[F"));
        }
    }
    if (arr && nj_array_length(arr) >= 16) {
        memcpy(view, nj_array_data(arr), sizeof(float) * 16u);
        /*
         * setCamera's Transform PLACES the camera in world space, so the
         * view matrix is its inverse: the stored matrix stays exactly what
         * the title set (getCamera hands it back), and the inverse is taken
         * here.  Using the transform itself as the view put the world in
         * front of a camera that was looking the other way.
         */
        mat_invert(view);
    }
    /* cam_proj_fill, not m3g_cam_projection: a Camera that never had its
       angles set is the JSR's default perspective (45 degrees, aspect 1,
       near 1, far 1000), and the viewport origin is added by the emitter
       rather than baked into the matrix. */
    if (!cam_proj_fill(vm, cam, proj)) {
        return 0;                            /* a camera this runtime cannot use */
    }
    if (model_in) {
        memcpy(model, model_in, sizeof(float) * 16u);
    }
    /* All three matrices are Java ones, so they multiply with mat_mul() (the
       row-major product) and not with m3g_mat_mul(), whose convention is the
       file loader's: clip = P * V * M * v, and mv = V * M for the lights. */
    memcpy(r.proj, proj, sizeof(r.proj));
    mat_mul(proj, view, r.mvp);
    mat_mul(r.mvp, model, r.mvp);
    memcpy(r.view, view, sizeof(r.view));
    mat_mul(view, model, r.mv);

    /* The vertex buffer's positions.  setPositions(vb, scale, bias) stores the
       scalar in _nj_bias and the per-component bias in _nj_scale. */
    if (vb) {
        d = nj_rget(vm, vb, m3g_fof(vm, NJ_C_VBUF, "_nj_positions",
                                    "L" NJ_C_VARR ";"));
        if (!d) {
            return 0;
        }
        arr = nj_rget(vm, d, m3g_fof(vm, NJ_C_VARR, "_nj_data", "[S"));
        if (!arr) {
            return 0;
        }
        r.pos    = (const nj_s2 *)nj_array_data(arr);
        r.pcomp  = nj_iget(vm, d, m3g_fof(vm, NJ_C_VARR, "_nj_numComp", "I"));
        r.pfirst = nj_iget(vm, d, m3g_fof(vm, NJ_C_VARR, "_nj_first", "I"));
        r.ptype  = nj_iget(vm, d, m3g_fof(vm, NJ_C_VARR, "_nj_ctype", "I"));
        r.pcount = nj_iget(vm, d, m3g_fof(vm, NJ_C_VARR, "_nj_count", "I"));
        nverts   = nj_iget(vm, d, m3g_fof(vm, NJ_C_VARR, "_nj_numVerts", "I"));
        if (!r.pos || r.pcomp < 2 || nverts <= 0) {
            return 0;
        }
        if (r.pcount <= 0 || r.pcount > nverts) { r.pcount = nverts; }
        r.pfac = bits2f(nj_iget(vm, vb, m3g_fof(vm, NJ_C_VBUF, "_nj_bias", "F")));
        if (!(r.pfac > 0.0f) && !(r.pfac < 0.0f)) { r.pfac = 1.0f; }
        {
            nj_ref b = nj_rget(vm, vb, m3g_fof(vm, NJ_C_VBUF, "_nj_scale", "[F"));
            nj_u4 k;
            for (k = 0u; k < 4u; k++) { r.pbias[k] = 0.0f; }
            if (b && nj_array_length(b) >= 3) {
                memcpy(r.pbias, nj_array_data(b), sizeof(float) * 3u);
            }
        }


        /* Colours, and the material that overrides them. */
        r.col = NULL;
        r.ccomp = r.cfirst = r.ccount = 0;
        d = nj_rget(vm, vb, m3g_fof(vm, NJ_C_VBUF, "_nj_colors", "L" NJ_C_VARR ";"));
        if (d) {
            nj_ref cd = nj_rget(vm, d, m3g_fof(vm, NJ_C_VARR, "_nj_data", "[S"));
            if (cd) {
                r.col    = (const nj_s2 *)nj_array_data(cd);
                r.ccomp  = nj_iget(vm, d, m3g_fof(vm, NJ_C_VARR, "_nj_numComp", "I"));
                r.cfirst = nj_iget(vm, d, m3g_fof(vm, NJ_C_VARR, "_nj_first", "I"));
                r.ccount = nj_iget(vm, d, m3g_fof(vm, NJ_C_VARR, "_nj_numVerts", "I"));
            }
        }
        r.default_colour = (nj_u4)nj_iget(vm, vb,
                                  m3g_fof(vm, NJ_C_VBUF, "_nj_defColor", "I"));
        r.dif = 0xFFFFFFFFu;
        mat = app ? nj_rget(vm, app, m3g_fof(vm, NJ_C_APPEAR, "_nj_mat",
                                             "L" NJ_C_MAT ";")) : 0;
        if (mat) {
            r.dif = (nj_u4)nj_iget(vm, mat, m3g_fof(vm, NJ_C_MAT, "_nj_c_dif", "I"));
        }
        /*
         * The rest of the material and the lights.  A surface is lit when the
         * appearance carries a Material and the buffer carries normals - that is
         * the JSR's own condition, and it is why an unlit buffer keeps its own
         * colours.  Nothing here reads the light list unless lighting is on.
         */
        r.emi = r.amb = r.spc = 0u;
        r.shin = 0.0f;
        r.vcolor = 0;
        if (mat) {
            r.emi = (nj_u4)nj_iget(vm, mat, m3g_fof(vm, NJ_C_MAT, "_nj_c_emi", "I"));
            r.amb = (nj_u4)nj_iget(vm, mat, m3g_fof(vm, NJ_C_MAT, "_nj_c_amb", "I"));
            r.spc = (nj_u4)nj_iget(vm, mat, m3g_fof(vm, NJ_C_MAT, "_nj_c_spec", "I"));
            r.shin = bits2f(nj_iget(vm, mat, m3g_fof(vm, NJ_C_MAT, "_nj_shininess", "F")));
            r.vcolor = (int)nj_iget(vm, mat, m3g_fof(vm, NJ_C_MAT, "_nj_vcolor", "Z"));
        }
        r.nrm = NULL;
        r.ncomp = r.nfirst = r.ncount = r.ntype = 0;
        d = nj_rget(vm, vb, m3g_fof(vm, NJ_C_VBUF, "_nj_normals", "L" NJ_C_VARR ";"));
        if (d) {
            nj_ref nd = nj_rget(vm, d, m3g_fof(vm, NJ_C_VARR, "_nj_data", "[S"));

            if (nd) {
                r.nrm    = (const nj_s2 *)nj_array_data(nd);
                r.ncomp  = nj_iget(vm, d, m3g_fof(vm, NJ_C_VARR, "_nj_numComp", "I"));
                r.nfirst = nj_iget(vm, d, m3g_fof(vm, NJ_C_VARR, "_nj_first", "I"));
                r.ncount = nj_iget(vm, d, m3g_fof(vm, NJ_C_VARR, "_nj_numVerts", "I"));
                r.ntype  = nj_iget(vm, d, m3g_fof(vm, NJ_C_VARR, "_nj_ctype", "I"));
            }
        }
    } /* Mesh vertex and material state; sprites supply their own vertices. */
    r.lights = nj_rget(vm, self, m3g_fof(vm, NJ_C_G3D, "_nj_lights",
                                         "[L" NJ_C_LIGHT ";"));
    r.lmats = nj_rget(vm, self, m3g_fof(vm, NJ_C_G3D, "_nj_lmats", "[F"));
    r.nlights = nj_iget(vm, self, m3g_fof(vm, NJ_C_G3D, "_nj_nlights", "I"));
    if (r.nlights < 0) { r.nlights = 0; }
    if (r.lights && r.nlights > (nj_s4)r.lights->length) {
        r.nlights = (nj_s4)r.lights->length;
    }
    if (!r.lights) { r.nlights = 0; }
    /*
     * render(World) is lit by the Light nodes that belong to that World, not
     * by whatever addLight() left on the Graphics3D and not by the lights of
     * the World drawn before it.  The walk in m3g_render_scene() hands that
     * set down here; the immediate-mode paths pass no override.
     */
    if (slights) {
        r.slights = slights;
        r.slmats = slmats;
        r.nlights = snlights;
    }
    /*
     * Read each light out of its Java object here, once, instead of inside
     * jr_lit(): that function runs per vertex per light, and the field lookups
     * and field reads dominated a profile of Doom 3.  Nothing between this
     * point and the last triangle can change a Light, because no Java code runs
     * while a native render does.
     */
    if (r.nlights > (nj_s4)NJ_M3G_LIGHTS) { r.nlights = (nj_s4)NJ_M3G_LIGHTS; }
    {
        nj_field *fm = nj_field_lookup(vm, NJ_C_LIGHT, "_nj_mode", "I", 0);
        nj_field *fi = nj_field_lookup(vm, NJ_C_LIGHT, "_nj_intensity", "F", 0);
        nj_field *fc = nj_field_lookup(vm, NJ_C_LIGHT, "_nj_color", "I", 0);
        nj_field *fa = nj_field_lookup(vm, NJ_C_LIGHT, "_nj_atten", "[F", 0);
        nj_field *fsa = nj_field_lookup(vm, NJ_C_LIGHT, "_nj_spotAngle", "F", 0);
        nj_field *fse = nj_field_lookup(vm, NJ_C_LIGHT, "_nj_spotExp", "F", 0);
        nj_s4 i;

        for (i = 0; i < r.nlights; i++) {
            nj_ref lt = r.slights ? r.slights[i]
                                  : nj_array_get_ref(r.lights, (nj_u4)i);

            r.lmode[i] = 0u;
            r.lcol[i] = 0xFFFFFFFFu;
            r.lint[i] = 1.0f;
            r.latt[i][0] = 1.0f; r.latt[i][1] = 0.0f; r.latt[i][2] = 0.0f;
            r.lspot_angle[i] = 45.0f;
            r.lspot_exp[i] = 0.0f;
            if (!lt || !fm || !fi || !fc) { continue; }
            r.lmode[i] = (nj_u4)nj_iget(vm, lt, fm);
            r.lcol[i] = (nj_u4)nj_iget(vm, lt, fc);
            r.lint[i] = bits2f(nj_iget(vm, lt, fi));
            if (fa) {
                nj_ref a = nj_rget(vm, lt, fa);

                if (a && nj_array_length(a) >= 3) {
                    const float *pp = (const float *)nj_array_data(a);

                    r.latt[i][0] = pp[0];
                    r.latt[i][1] = pp[1];
                    r.latt[i][2] = pp[2];
                }
            }
            if (!(r.latt[i][0] > 0.0f) && r.latt[i][1] == 0.0f &&
                r.latt[i][2] == 0.0f) {
                r.latt[i][0] = 1.0f;      /* the JSR default attenuation */
            }
            if (fsa) { r.lspot_angle[i] = bits2f(nj_iget(vm, lt, fsa)); }
            if (fse) { r.lspot_exp[i] = bits2f(nj_iget(vm, lt, fse)); }
        }
    }
    /*
     * Lighting uses the Material, normals and collected lights. Without
     * normals or lights this renderer keeps the surface's unlit colours.
     * World rendering collects Light nodes separately from addLight calls.
     */
    r.lit = (mat != 0 && r.nrm != NULL && r.ncomp >= 3 && r.nlights > 0);
    r.fog = 0;
    if (app) {
        nj_ref fog = nj_rget(vm, app, m3g_fof(vm, NJ_C_APPEAR, "_nj_fog",
                                              "L" NJ_C_FOG ";"));

        if (fog) {
            r.fog = 1;
            r.fog_mode = (int)nj_iget(vm, fog, m3g_fof(vm, NJ_C_FOG, "_nj_mode", "I"));
            r.fog_colour = (nj_u4)nj_iget(vm, fog,
                            m3g_fof(vm, NJ_C_FOG, "_nj_color", "I")) & 0x00FFFFFFu;
            r.fog_density = bits2f(nj_iget(vm, fog,
                             m3g_fof(vm, NJ_C_FOG, "_nj_density", "F")));
            r.fog_near = bits2f(nj_iget(vm, fog,
                          m3g_fof(vm, NJ_C_FOG, "_nj_near", "F")));
            r.fog_far = bits2f(nj_iget(vm, fog,
                         m3g_fof(vm, NJ_C_FOG, "_nj_far", "F")));
        }
    }

    /* Texture unit 0 and the polygon mode: a textured title says so in the
       Appearance, and the PolygonMode is where culling and winding live
       (the JSR defaults are CULL_BACK and CCW). */
    r.sw.cull = 0;
    r.sw.winding = 0;
    /* The JSR defaults when the appearance carries no CompositingMode:
       ALPHA blending with no alpha test, and the alpha channel written. */
    r.sw.texfunc = 227;
    r.sw.blend = 64;
    r.sw.athresh = 0;
    r.sw.awrite = 1;
    r.sw.dtest = 1;
    r.sw.dwrite = 1;
    if (app) {
        nj_ref pm = nj_rget(vm, app, m3g_fof(vm, NJ_C_APPEAR, "_nj_pm",
                                             "L" NJ_C_POLY ";"));

        if (vb && pm) {
            r.sw.cull = (int)nj_iget(vm, pm,
                          m3g_fof(vm, NJ_C_POLY, "_nj_culling", "I"));
            r.sw.winding = (int)nj_iget(vm, pm,
                             m3g_fof(vm, NJ_C_POLY, "_nj_winding", "I"));
        }
        {
            nj_ref cm = nj_rget(vm, app, m3g_fof(vm, NJ_C_APPEAR, "_nj_cm",
                                                 "L" NJ_C_COMP ";"));

            if (cm) {
                r.sw.dtest = (int)nj_iget(vm, cm,
                              m3g_fof(vm, NJ_C_COMP, "_nj_dtest", "I"));
                r.sw.dwrite = (int)nj_iget(vm, cm,
                               m3g_fof(vm, NJ_C_COMP, "_nj_dwrite", "I"));
                r.sw.blend = (int)nj_iget(vm, cm,
                              m3g_fof(vm, NJ_C_COMP, "_nj_blend", "I"));
                r.sw.awrite = (int)nj_iget(vm, cm,
                               m3g_fof(vm, NJ_C_COMP, "_nj_awrite", "I"));
                r.sw.athresh = (int)(bits2f(nj_iget(vm, cm,
                               m3g_fof(vm, NJ_C_COMP, "_nj_athresh", "I")))
                                     * 255.0f + 0.5f);
                if (r.sw.athresh < 0) { r.sw.athresh = 0; }
                if (r.sw.athresh > 255) { r.sw.athresh = 255; }
            }
        }
        {
            nj_ref tex = nj_rget(vm, app, m3g_fof(vm, NJ_C_APPEAR, "_nj_tex0",
                                                  "L" NJ_C_TEX2D ";"));
            if (vb && tex) {
                nj_ref im2 = nj_rget(vm, tex, m3g_fof(vm, NJ_C_TEX2D, "_nj_img",
                                                      "L" NJ_C_IMAGE2D ";"));
                r.tmat = mat_of(vm, tex, NJ_C_XABLE, "_nj_m");
                r.sw.wrap_s = (int)nj_iget(vm, tex,
                               m3g_fof(vm, NJ_C_TEX2D, "_nj_wrapS", "I"));
                r.sw.wrap_t = (int)nj_iget(vm, tex,
                               m3g_fof(vm, NJ_C_TEX2D, "_nj_wrapT", "I"));
                r.sw.texfunc = (int)nj_iget(vm, tex,
                                m3g_fof(vm, NJ_C_TEX2D, "_nj_blend", "I"));
                r.sw.filt_image = (int)nj_iget(vm, tex,
                                  m3g_fof(vm, NJ_C_TEX2D, "_nj_filtImage", "I"));
                if (im2) {
                    nj_ref px = nj_rget(vm, im2, m3g_fof(vm, NJ_C_IMAGE2D,
                                                         "_nj_pixels", "[B"));
                    nj_ref mipref;
                    nj_s4 fmt = m3g_image_format(vm, im2);

                    r.sw.tex_w = (int)nj_iget(vm, im2, m3g_fof(vm, NJ_C_IMAGE2D,
                                                               "_nj_w", "I"));
                    r.sw.tex_h = (int)nj_iget(vm, im2, m3g_fof(vm, NJ_C_IMAGE2D,
                                                               "_nj_h", "I"));
                    if (px && r.sw.tex_w > 0 && r.sw.tex_h > 0 &&
                        (fmt == 99 || fmt == 100)) {
                        r.sw.tex = (const nj_u1 *)nj_array_data(px);
                        r.sw.tex_bpp = (fmt == 100) ? 4 : 3;
                        /* The level chain, when the image has one to build:
                           level 0 of the chain is the RGBA copy of these very
                           pixels, and the levels below it are the same picture
                           at half the size each. */
                        mipref = m3g_image_mips(vm, im2);
                        if (mipref) {
                            r.sw.mips = (const nj_u1 *)nj_array_data(mipref);
                            r.sw.mip_w = r.sw.tex_w;
                            r.sw.mip_h = r.sw.tex_h;
                            r.sw.mip_n = m3g_mip_count(r.sw.tex_w, r.sw.tex_h);
                        }
                    }
                }
            }
        }
    }

    /* Texture coordinates, unit 0.  The JSR's per-unit scale and bias are
       not applied yet; the arrays titles ship already carry the mapping. */
    r.tpos = NULL;
    r.tcomp = r.tfirst = r.tcount = r.ttype = 0;
    r.tscale = 1.0f;
    r.tbias[0] = r.tbias[1] = r.tbias[2] = 0.0f;
    if (vb) {
        d = nj_rget(vm, vb, m3g_fof(vm, NJ_C_VBUF, "_nj_tex0", "L" NJ_C_VARR ";"));
        if (d) {
            nj_ref td = nj_rget(vm, d, m3g_fof(vm, NJ_C_VARR, "_nj_data", "[S"));

            if (td) {
                r.tpos   = (const nj_s2 *)nj_array_data(td);
                r.tcomp  = nj_iget(vm, d, m3g_fof(vm, NJ_C_VARR, "_nj_numComp", "I"));
                r.tfirst = nj_iget(vm, d, m3g_fof(vm, NJ_C_VARR, "_nj_first", "I"));
                r.tcount = nj_iget(vm, d, m3g_fof(vm, NJ_C_VARR, "_nj_numVerts", "I"));
                r.ttype  = nj_iget(vm, d, m3g_fof(vm, NJ_C_VARR, "_nj_ctype", "I"));
            }
        }
        /*
         * The buffer's own texture-coordinate transform.  A loaded VertexBuffer
         * carries a scale and a three-float bias per texture unit, and they are
         * not optional decoration: a rally car's skin is placed by them, so a
         * sampler that reads the raw coordinates draws the right shape with the
         * wrong piece of the texture on it.  Defaults (1, 0) leave a buffer built
         * through the API, which has none, exactly as it was.
         */
        {
            nj_ref tb = nj_rget(vm, vb, m3g_fof(vm, NJ_C_VBUF, "_nj_texBias", "[F"));

            r.tscale = bits2f(nj_iget(vm, vb, m3g_fof(vm, NJ_C_VBUF,
                                                      "_nj_texScale", "F")));
            if (!(r.tscale > 0.0f) && !(r.tscale < 0.0f)) {
                r.tscale = 1.0f;        /* nobody set one: the raw coordinates */
            }
            if (tb != 0) {
                const float *tp = (const float *)nj_array_data(tb);

                if (tp != NULL && nj_array_length(tb) >= 2) {
                    r.tbias[0] = tp[0];
                    r.tbias[1] = tp[1];
                    if (nj_array_length(tb) >= 3) {
                        r.tbias[2] = tp[2];
                    }
                }
            }
        }

    }

    /* The index buffer.  A TriangleStripArray carries strip lengths; a plain
       IndexBuffer is a list.  Neither carries a vertex count, so a strip with
       lengths walks its own count and a list walks the vertex range. */
    if (ib) {
        nj_field *f;

        /* The object's own class decides which fields exist: a strip has
           lengths, a plain IndexBuffer does not. */
        cn = nj_ref_class_name(ib);
        if (!cn) { cn = NJ_C_IBUF; }
        f = nj_field_lookup(vm, cn, "_nj_ib_data", "[I", 0);
        if (f) {
            nj_ref a = nj_rget(vm, ib, f);
            if (a) { idx = (const nj_s4 *)nj_array_data(a); }
        }
        f = nj_field_lookup(vm, cn, "_nj_ib_count", "I", 0);
        if (f) { icount = nj_iget(vm, ib, f); }
        f = nj_field_lookup(vm, cn, "_nj_lengths", "[I", 0);
        if (f) {
            nj_ref a = nj_rget(vm, ib, f);
            if (a) {
                lens = (const nj_s4 *)nj_array_data(a);
                nstrip = (nj_s4)a->length;
            }
        }
        f = nj_field_lookup(vm, cn, "_nj_first", "I", 0);
        if (f) { ifirst = nj_iget(vm, ib, f); }
    }
    r.idx = idx;
    r.icount = icount;
    r.lens = lens;
    r.nstrip = nstrip;
    r.ifirst = ifirst;

    r.jt.vm = vm;
    r.jt.g = g;
    r.jt.ox = r.vx;
    r.jt.oy = r.vy;
    r.drawn = 0u;
    *out = r;
    /*
     * sw.surf is the one field that points into the draw state itself: the
     * scratch above, filled by nj_midp_surface().  Copying the struct leaves
     * it aimed at this frame, which is dead the moment jr_setup returns - the
     * rasteriser then writes the picture into whatever the next call puts
     * there.  It has to follow the copy.
     */
    if (r.sw.surf == &r.scratch) {
        out->sw.surf = &out->scratch;
    }
    return 1;
}

/*
 * Draw what jr_setup() described: a strip array walks its own lengths (the
 * winding flips on every second triangle, which jr_strip handles), a plain
 * index buffer walks its indices, and a mesh with neither is the JSR's
 * implicit list over the vertex range.
 */
static nj_u4 jr_draw(m3g_jr *r)
{
    if (r->nstrip > 0 && r->lens) {
        nj_s4 i, base = r->ifirst;

        for (i = 0; i < r->nstrip; i++) {
            jr_strip(r, r->idx, r->lens[i], base);
            base += r->lens[i];
        }
    } else if (r->idx && r->icount > 0) {
        jr_list(r, r->idx, r->icount, r->ifirst);
    } else {
        /*
         * A submesh is allowed to leave its index buffer out.  What such a
         * mesh draws is its whole vertex buffer as ONE triangle strip - the
         * reference implementation's index buffer is always a strip, and the
         * file format writes null when the strip is simply "all of them".
         * Drawing it as a triangle list instead connects every third vertex,
         * which is what smeared the rally car's texture across the screen.
         */
        jr_strip(r, NULL, r->pcount, r->ifirst);
    }
    r->vm->tris += (nj_u8)r->drawn;
    r->vm->m3g_renders++;
    return r->drawn;
}

static nj_u4 m3g_render_jr(nj_vm *vm, nj_ref self, nj_ref vb, nj_ref ib,
                           nj_ref app, nj_ref xf)
{
    m3g_jr r;
    float model[16];
    nj_ref d;

    if (!vm || !self || !vb) {
        return 0u;
    }
    memset(model, 0, sizeof(model));
    model[0] = model[5] = model[10] = model[15] = 1.0f;
    if (xf) {
        d = nj_rget(vm, xf, m3g_fof(vm, NJ_C_XFORM, "_nj_m", "[F"));
        if (d && nj_array_length(d) >= 16) {
            memcpy(model, nj_array_data(d), sizeof(float) * 16u);
        }
    }
    if (!jr_setup(vm, self, vb, ib, app, 0, NULL, NULL, 0, model, &r)) {
        return 0u;
    }
    return jr_draw(&r);
}

/*
 * The lights that live in the scene graph.  A file-authored scene carries its
 * Light as a child of a Group or of the World, and the reference
 * implementation lights a World from exactly those nodes; the JSR's own list
 * (Graphics3D.addLight) is the other way a title can do it.  Without this the
 * rally title - whose every .m3g file holds one Light - was drawn with no
 * light at all, which left its track the material's emissive colour.
 */
static void collect_lights_walk(nj_vm *vm, nj_ref o, nj_ref *out, float *mats,
                                const float *parent, nj_u4 cap, nj_u4 *n,
                                int depth)
{
    const char *cn;
    nj_ref kids, m;
    nj_value v;
    nj_u4 len, i;
    float model[16];

    if (!o || depth > 32) {
        return;
    }
    {
        nj_field *hf = nj_field_lookup(vm, NJ_C_NODE, "_nj_hidden", "Z", 0);

        if (hf && nj_iget(vm, o, hf)) {
            return;              /* a light under a hidden node lights nothing */
        }
    }
    cn = nj_ref_class_name(o);
    memcpy(model, parent, sizeof(model));
    m = nj_rget(vm, o, nj_field_lookup(vm, NJ_C_XABLE, "_nj_m", "[F", 0));
    if (m && nj_array_length(m) >= 16) {
        mat_mul(parent, (const float *)nj_array_data(m), model);
    }
    if (cn != NULL && strcmp(cn, NJ_C_LIGHT) == 0) {
        if (*n < cap) {
            out[*n] = o;
            if (mats) { memcpy(mats + (*n) * 16u, model, sizeof(model)); }
        }
        (*n)++;
        return;
    }
    if (cn == NULL || (strcmp(cn, NJ_C_GROUP) != 0 &&
                       strcmp(cn, NJ_C_WORLD) != 0)) {
        return;                  /* a Camera, a Mesh, a Sprite: no children */
    }
    kids = nj_rget(vm, o, nj_field_lookup(vm, NJ_C_GROUP, "_nj_kids",
                                          "L" NJ_C_VECTOR ";", 0));
    if (!kids) { return; }
    v.i = 0;
    if (nj_call(vm, kids, NJ_C_VECTOR, "size", "()I", NULL, &v) != NJ_OK) {
        return;
    }
    len = (nj_u4)v.i;
    for (i = 0u; i < len; i++) {
        nj_value a, r;
        a.i = (nj_s4)i;
        r.r = 0;
        if (nj_call(vm, kids, NJ_C_VECTOR, "elementAt",
                    "(I)Ljava/lang/Object;", &a, &r) == NJ_OK) {
            collect_lights_walk(vm, r.r, out, mats, model, cap, n, depth + 1);
        }
    }
}


/*
 * Draw every mesh the walk collected, each with its own matrix, vertex buffer,
 * index buffer and first submesh appearance.  That is what gives a World its
 * materials, textures, lighting, fog, culling and depth: before this, a World
 * was drawn as flat white triangles with no transform at all.
 */
/* Sprite geometry is made on the C stack.  NULL vb selects only the common
 * camera, surface, compositing and fog state, never mesh appearance textures. */
static nj_u4 jr_sprite(nj_vm *vm, nj_ref self, nj_ref sprite, nj_ref world,
                        const float *model)
{
    nj_ref image = nj_rget(vm, sprite, m3g_fof(vm, NJ_C_SPRITE3D, "_nj_img",
                                              "L" NJ_C_IMAGE2D ";"));
    nj_ref app = nj_rget(vm, sprite, m3g_fof(vm, NJ_C_SPRITE3D, "_nj_app",
                                            "L" NJ_C_APPEAR ";"));
    nj_ref pixels, ancestor;
    nj_s4 iw, ih, fmt, cx, cy, cw, ch;
    nj_s8 aw, ah, left, top, right, bottom;
    m3g_jr r;
    jr_vtx v[4];
    float origin[3], center[2], inv, alpha = 1.0f;
    double halfw, halfh, x0, x1, y0, y1, sx0, sx1, sy0, sy1;
    double u0, u1, t0, t1;
    int k, scaled, flipx, flipy;

    if (!image || !app) { return 0u; }
    iw = nj_iget(vm, image, m3g_fof(vm, NJ_C_IMAGE2D, "_nj_w", "I"));
    ih = nj_iget(vm, image, m3g_fof(vm, NJ_C_IMAGE2D, "_nj_h", "I"));
    fmt = m3g_image_format(vm, image);
    pixels = nj_rget(vm, image, m3g_fof(vm, NJ_C_IMAGE2D, "_nj_pixels", "[B"));
    if (iw <= 0 || ih <= 0 || (fmt != 99 && fmt != 100) || !pixels) { return 0u; }
    /* Divide before multiplying: dimensions and array bounds are untrusted. */
    if ((nj_size)iw > (nj_size)nj_array_length(pixels) / (fmt == 100 ? 4u : 3u) /
                      (nj_size)ih) { return 0u; }
    cx = nj_iget(vm, sprite, m3g_fof(vm, NJ_C_SPRITE3D, "_nj_cropX", "I"));
    cy = nj_iget(vm, sprite, m3g_fof(vm, NJ_C_SPRITE3D, "_nj_cropY", "I"));
    cw = nj_iget(vm, sprite, m3g_fof(vm, NJ_C_SPRITE3D, "_nj_cropW", "I"));
    ch = nj_iget(vm, sprite, m3g_fof(vm, NJ_C_SPRITE3D, "_nj_cropH", "I"));
    flipx = cw < 0; flipy = ch < 0;
    aw = flipx ? -(nj_s8)cw : (nj_s8)cw;
    ah = flipy ? -(nj_s8)ch : (nj_s8)ch;
    if (!aw || !ah) { return 0u; }
    left = cx > 0 ? cx : 0; top = cy > 0 ? cy : 0;
    right = (nj_s8)cx + aw; bottom = (nj_s8)cy + ah;
    if (right > iw) { right = iw; }
    if (bottom > ih) { bottom = ih; }
    if (right <= left || bottom <= top) { return 0u; }
    if (!jr_setup(vm, self, 0, 0, app, world, NULL, NULL, 0, model, &r) ||
        !r.sw.surf) {
        return 0u;
    }
    origin[0] = r.mv[3]; origin[1] = r.mv[7]; origin[2] = r.mv[11];
    jr_project(r.proj, origin, r.vw, r.vh, center, &inv);
    if (!isfinite(inv) || inv <= 0.0f || !isfinite(center[0]) ||
        !isfinite(center[1]) || !isfinite(origin[2])) { return 0u; }
    scaled = nj_iget(vm, sprite, m3g_fof(vm, NJ_C_SPRITE3D, "_nj_scaled", "Z"));
    if (scaled) {
        float extent[3], projected[2], wi;
        double hx = 0.5 * sqrt((double)r.mv[0]*r.mv[0] +
                              (double)r.mv[4]*r.mv[4] + (double)r.mv[8]*r.mv[8]);
        double hy = 0.5 * sqrt((double)r.mv[1]*r.mv[1] +
                              (double)r.mv[5]*r.mv[5] + (double)r.mv[9]*r.mv[9]);
        memcpy(extent, origin, sizeof(extent));
        extent[0] += (float)hx;
        jr_project(r.proj, extent, r.vw, r.vh, projected, &wi);
        if (!(wi > 0.0f)) { return 0u; }
        halfw = fabs((double)projected[0] - center[0]);
        memcpy(extent, origin, sizeof(extent));
        extent[1] += (float)hy;
        jr_project(r.proj, extent, r.vw, r.vh, projected, &wi);
        if (!(wi > 0.0f)) { return 0u; }
        halfh = fabs((double)projected[1] - center[1]);
    } else {
        halfw = 0.5 * (double)aw;
        halfh = 0.5 * (double)ah;
    }
    /* The full crop, including portions outside the image, defines size.
     * Flips reverse sampling; the crop intersection retains its placement. */
    x0 = center[0] + halfw * (2.0 * ((double)left - cx) / aw - 1.0);
    x1 = center[0] + halfw * (2.0 * ((double)right - cx) / aw - 1.0);
    y0 = center[1] + halfh * (2.0 * ((double)top - cy) / ah - 1.0);
    y1 = center[1] + halfh * (2.0 * ((double)bottom - cy) / ah - 1.0);
    if (!isfinite(x0) || !isfinite(x1) || !isfinite(y0) || !isfinite(y1) ||
        x1 <= x0 || y1 <= y0) { return 0u; }
    /* Clip the rectangle before float-to-int raster bounds conversions, and
     * interpolate its UVs so huge off-screen crops cannot overflow them. */
    sx0 = fmax(x0, fmax(0.0, -(double)r.vx));
    sx1 = fmin(x1, fmin((double)r.vw, (double)r.sw.surf->width - r.vx));
    sy0 = fmax(y0, fmax(0.0, -(double)r.vy));
    sy1 = fmin(y1, fmin((double)r.vh, (double)r.sw.surf->height - r.vy));
    if (sx1 <= sx0 || sy1 <= sy0) { return 0u; }
    u0 = flipx ? (double)right / iw : (double)left / iw;
    u1 = flipx ? (double)left / iw : (double)right / iw;
    t0 = flipy ? (double)bottom / ih : (double)top / ih;
    t1 = flipy ? (double)top / ih : (double)bottom / ih;
    r.sw.sprite = 1;
    r.sw.tex = (const nj_u1 *)nj_array_data(pixels);
    r.sw.tex_w = iw; r.sw.tex_h = ih; r.sw.tex_bpp = fmt == 100 ? 4 : 3;
    r.sw.sprite_l = (int)left; r.sw.sprite_t = (int)top;
    r.sw.sprite_r = (int)right; r.sw.sprite_b = (int)bottom;
    r.sw.wrap_s = r.sw.wrap_t = 240; r.sw.filt_image = 128;
    r.sw.texfunc = 228; r.sw.cull = 162; r.sw.sprite_fog = 1.0f;
    r.sw.sprite_fog_colour = r.fog_colour;
    if (r.fog) {
        float depth = fabsf(origin[2]);
        float span = r.fog_far - r.fog_near;
        float f = r.fog_mode == 81 ? (span > 0.0f ? (r.fog_far-depth)/span : 1.0f)
                                   : expf(-r.fog_density * depth);
        r.sw.sprite_fog = isfinite(f) ? fminf(1.0f, fmaxf(0.0f, f)) : 1.0f;
    }
    ancestor = sprite;
    for (k = 0; ancestor && k <= 32; k++) {
        if (nj_iget(vm, ancestor, m3g_fof(vm, NJ_C_NODE, "_nj_alphaSet", "Z"))) {
            float a = bits2f(nj_iget(vm, ancestor, m3g_fof(vm, NJ_C_NODE, "_nj_alpha", "F")));
            alpha *= isfinite(a) ? fminf(1.0f, fmaxf(0.0f, a)) : 1.0f;
        }
        if (ancestor == world) { break; }
        ancestor = nj_rget(vm, ancestor, m3g_fof(vm, NJ_C_NODE, "_nj_parent", "L" NJ_C_NODE ";"));
    }
    r.sw.sprite_alpha = (int)(alpha * 255.0f + 0.5f);
    memset(v, 0, sizeof(v));
    for (k = 0; k < 4; k++) {
        double x = (k == 1 || k == 2) ? sx1 : sx0;
        double y = k >= 2 ? sy1 : sy0;
        v[k].xy[0] = (float)(x + r.vx); v[k].xy[1] = (float)(y + r.vy);
        v[k].inv = inv;
        v[k].st[0] = (float)(u0 + (u1-u0) * ((x-x0)/(x1-x0)));
        v[k].st[1] = (float)(t0 + (t1-t0) * ((y-y0)/(y1-y0)));
        memcpy(v[k].pe, origin, sizeof(origin));
        v[k].rgb = 0xFFFFFFu;
    }
    jr_triangle_v(&r, &v[0], &v[1], &v[2]);
    jr_triangle_v(&r, &v[0], &v[2], &v[3]);
    return r.drawn;
}

static nj_u4 m3g_world_draw(nj_vm *vm, nj_ref self, nj_ref root,
                            nj_ref *meshes, float *mats, nj_u4 n,
                            const nj_ref *slights, const float *slmats,
                            nj_s4 snlights)
{
    nj_u4 i, drawn = 0u;

    for (i = 0u; i < n && i < 64u; i++) {
        nj_ref vb, ib = 0, app = 0, subs;
        m3g_jr r;

        const char *cn = nj_ref_class_name(meshes[i]);
        if (cn && (strcmp(cn, NJ_C_SPRITE3D) == 0 || strcmp(cn, NJ_C_M3G_SPRITE) == 0)) {
            drawn += jr_sprite(vm, self, meshes[i], root, mats + (nj_size)i * 16u);
            continue;
        }
        vb = nj_rget(vm, meshes[i], nj_field_lookup(vm, NJ_C_MESH, "_nj_vb",
                                                    "L" NJ_C_VBUF ";", 0));
        if (!vb) {
            continue;                   /* a mesh with no vertices draws nothing */
        }
        ib = nj_rget(vm, meshes[i], nj_field_lookup(vm, NJ_C_MESH, "_nj_ib",
                                                    "L" NJ_C_IBUF ";", 0));
        subs = nj_rget(vm, meshes[i], nj_field_lookup(vm, NJ_C_MESH, "_nj_subs",
                                                      "L" NJ_C_VECTOR ";", 0));
        {
            nj_ref ibs = nj_rget(vm, meshes[i], m3g_fof(vm, NJ_C_MESH,
                                     "_nj_ibs", "[L" NJ_C_IBUF ";"));
            nj_s4 k, count = get_submesh_count(vm, meshes[i]);
            for (k = 0; k < count; k++) {
                nj_value a0, rr;
                app = 0;
                if (ibs) ib = nj_array_get_ref(ibs, (nj_u4)k);
                if (subs) {
                    a0.i = k;
                    rr.r = 0;
                    if (nj_call(vm, subs, NJ_C_VECTOR, "elementAt",
                                "(I)Ljava/lang/Object;", &a0, &rr) != NJ_OK)
                        continue;
                    app = rr.r;
                }
                if (!app || !ib) continue;
                if (jr_setup(vm, self, vb, ib, app, root, slights, slmats,
                             snlights, mats + (nj_size)i * 16u, &r))
                    drawn += jr_draw(&r);
            }
        }
    }
    return drawn;
}


NJFN(m3g_g3d_render5)
{
    nj_u4 drew = m3g_render_jr(vm, NJ_THIS(args), NJ_ARG_REF(args, 0),
                               NJ_ARG_REF(args, 1), NJ_ARG_REF(args, 2),
                               NJ_ARG_REF(args, 3));
    ret->i = (nj_s4)drew;
    return NJ_OK;
}
NJFN(m3g_g3d_getCamera)
{
    nj_ref cam = nj_rget(vm, NJ_THIS(args),
                         m3g_fof(vm, NJ_C_G3D, "_nj_camera",
                                 "L" NJ_C_CAMERA ";"));
    nj_ref m = nj_rget(vm, NJ_THIS(args),
                       m3g_fof(vm, NJ_C_G3D, "_nj_cam_m", "[F"));
    float *out = mat_of(vm, NJ_ARG_REF(args, 0), NJ_C_XFORM, "_nj_m");

    if (out && m && m->length >= (nj_s4)NJ_MF) {
        memcpy(out, (const float *)m->u.bytes, sizeof(float) * NJ_MF);
    }
    ret->r = cam;
    return NJ_OK;
}
/*
 * The light list: an object array of Light plus one float[] per slot for
 * the transform each was given.  Index 0 is LIGHT_0, so a title that sets
 * slot 3 alone still reports a count of 4, which is what the JSR means by
 * "the number of lights in the array".
 */
static nj_ref m3g_g3d_slot(nj_vm *vm, nj_ref self, const char *fld,
                           const char *elem, const char *arrtype, nj_s4 index)
{
    /*
     * The field is an array of `elem` (so its descriptor is `arrtype`, which
     * for a light array is "[Ljavax/microedition/m3g/Light;" and not the
     * element descriptor).  Looking the field up with the element descriptor
     * is what made setLight() throw the moment a title called it.
     */
    nj_ref arr = nj_rget(vm, self, m3g_fof(vm, NJ_C_G3D, fld, arrtype));

    if (!arr || (nj_s4)arr->length <= index) {
        nj_ref bigger = nj_object_array_new(vm, elem, (nj_u4)index + 8u);
        nj_s4 i;

        if (!bigger) {
            return NULL;
        }
        for (i = 0; arr && i < (nj_s4)arr->length; i++) {
            nj_array_set_ref(bigger, (nj_u4)i, nj_array_get_ref(arr, (nj_u4)i));
        }
        nj_rput(vm, self, m3g_fof(vm, NJ_C_G3D, fld, arrtype), bigger);
        arr = bigger;
    }
    return arr;
}
NJFN(m3g_g3d_setLight)
{
    nj_ref self = NJ_THIS(args);
    nj_s4 index = NJ_ARG_INT(args, 0);
    nj_ref lights, mats;
    float *src;

    if (index < 0) {
        NJ_THROW(vm, "java/lang/IndexOutOfBoundsException", "negative light");
    }
    lights = m3g_g3d_slot(vm, self, "_nj_lights", "L" NJ_C_LIGHT ";",
                              "[L" NJ_C_LIGHT ";", index);
    {
        nj_ref old = nj_rget(vm, self, m3g_fof(vm, NJ_C_G3D, "_nj_lmats", "[F"));
        nj_u4 count = (nj_u4)(index + 1) * NJ_MF;

        mats = old;
        if (!mats || nj_array_length(mats) < count) {
            mats = nj_array_new(vm, "F", count);
            if (!mats) { return nj_throw_oom(vm); }
            if (old) {
                memcpy(nj_array_data(mats), nj_array_data(old),
                       (nj_size)nj_array_length(old) * sizeof(float));
            }
            nj_rput(vm, self, m3g_fof(vm, NJ_C_G3D, "_nj_lmats", "[F"), mats);
        }
    }
    if (!lights) {
        return nj_throw_oom(vm);
    }
    nj_array_set_ref(lights, (nj_u4)index, NJ_ARG_REF(args, 1));
    {
        float *dst = (float *)nj_array_data(mats) + (nj_size)index * NJ_MF;

        src = mat_of(vm, NJ_ARG_REF(args, 2), NJ_C_XFORM, "_nj_m");
        if (src) {
            memcpy(dst, src, sizeof(float) * NJ_MF);
        } else {
            memset(dst, 0, sizeof(float) * NJ_MF);
            dst[0] = dst[5] = dst[10] = dst[15] = 1.0f;
        }
    }
    if (nj_iget(vm, self, m3g_fof(vm, NJ_C_G3D, "_nj_nlights", "I")) < index + 1) {
        nj_iput(vm, self, m3g_fof(vm, NJ_C_G3D, "_nj_nlights", "I"), index + 1);
    }
    return NJ_OK;
}
NJFN(m3g_g3d_getLightCount)
{
    ret->i = nj_iget(vm, NJ_THIS(args),
                     m3g_fof(vm, NJ_C_G3D, "_nj_nlights", "I"));
    return NJ_OK;
}
NJFN(m3g_g3d_getLight)
{
    nj_ref self = NJ_THIS(args);
    nj_s4 index = NJ_ARG_INT(args, 0);
    nj_ref lights = nj_rget(vm, self,
                            m3g_fof(vm, NJ_C_G3D, "_nj_lights",
                                    "[L" NJ_C_LIGHT ";"));
    nj_ref mats = nj_rget(vm, self,
                          m3g_fof(vm, NJ_C_G3D, "_nj_lmats", "[F"));
    float *out = mat_of(vm, NJ_ARG_REF(args, 1), NJ_C_XFORM, "_nj_m");

    if (lights && index >= 0 && index < (nj_s4)lights->length) {
        ret->r = nj_array_get_ref(lights, (nj_u4)index);
    } else {
        NJ_THROW(vm, "java/lang/IndexOutOfBoundsException", "light index");
    }
    if (out && mats && (nj_s4)mats->length >= NJ_MF * (index + 1)) {
        const float *all = (const float *)nj_array_data(mats);

        memcpy(out, all + (nj_u4)index * NJ_MF, sizeof(float) * NJ_MF);
    }
    return NJ_OK;
}

/* A boxed value for getProperties().  The classes are CLDC's own. */
static nj_ref m3g_box(nj_vm *vm, const char *cls, nj_s4 v, int is_bool)
{
    nj_ref o;
    nj_value a[1];
    nj_value r;

    o = nj_object_new(vm, cls);
    if (!o) {
        return NULL;
    }
    a[0].i = v;
    if (nj_call_special(vm, o, cls, "<init>", is_bool ? "(Z)V" : "(I)V",
                        a, &r) != NJ_OK) {
        return NULL;
    }
    return o;
}
/*
 * getProperties() answers what this implementation supports.  The values
 * are the ones the rest of this file implies: no antialiasing, no dither,
 * no mipmapping, no local camera lighting, and a rasteriser that does not
 * exist yet - so the support flags that would promise a picture are false
 * rather than optimistic.
 */
NJFN(m3g_g3d_getProperties)
{
    static const struct { const char *key; nj_s4 v; int is_bool; } props[] = {
        { "supportAntialiasing",           0, 1 },
        { "supportTrueColor",              0, 1 },
        { "supportDithering",              0, 1 },
        { "supportMipmapping",             0, 1 },
        { "supportPerspectiveCorrection",  1, 1 },
        { "supportLocalCameraLighting",    0, 1 },
        { "maxLights",                     8, 0 },
        { "maxViewportWidth",           4096, 0 },
        { "maxViewportHeight",          4096, 0 },
        { "maxTextureDimension",        1024, 0 },
        { "maxSpriteCropDimension",     4096, 0 },
        { "maxTransformsPerVertex",        3, 0 }
    };
    nj_ref ht;
    nj_u4 i;

    ht = nj_object_new(vm, "java/util/Hashtable");
    if (!ht) {
        return nj_throw_oom(vm);
    }
    if (nj_call_special(vm, ht, "java/util/Hashtable", "<init>", "()V",
                        NULL, NULL) != NJ_OK) {
        nj_clear_exception(vm);
        ret->r = NULL;
        return NJ_OK;
    }
    for (i = 0; i < (nj_u4)(sizeof(props) / sizeof(props[0])); i++) {
        nj_ref key = nj_string_new(vm, props[i].key);
        nj_ref val = m3g_box(vm, props[i].is_bool ? "java/lang/Boolean"
                                                  : "java/lang/Integer",
                             props[i].v, props[i].is_bool);
        nj_value a[2];
        nj_value r;

        if (!key || !val) {
            nj_clear_exception(vm);
            continue;
        }
        a[0].r = key;
        a[1].r = val;
        if (nj_call(vm, ht, "java/util/Hashtable", "put",
                    "(Ljava/lang/Object;Ljava/lang/Object;)"
                    "Ljava/lang/Object;", a, &r) != NJ_OK) {
            nj_clear_exception(vm);
        }
    }
    ret->r = ht;
    return NJ_OK;
}

/* ---- Group, Image2D, Transform.transform --------------------------- */

/*
 * A Group is born empty: the child Vector is created by whichever call
 * needs it (addChild, or the loader when a file names children), so the
 * constructor has nothing to store.
 */
NJFN(m3g_group_init)
{
    return NJ_OK;
}

/* Image2D(int format, int width, int height, byte[] image). */
NJFN(m3g_img2d_init_pixels)
{
    nj_ref self = NJ_THIS(args);

    if (NJ_ARG_INT(args, 0) < 96 || NJ_ARG_INT(args, 0) > 100) {
        return nj_throw(vm, "java/lang/IllegalArgumentException", "image format");
    }
    nj_iput(vm, self, m3g_fof(vm, NJ_C_IMAGE2D, "_nj_fmt", "I"),
            NJ_ARG_INT(args, 0));
    nj_iput(vm, self, m3g_fof(vm, NJ_C_IMAGE2D, "_nj_w", "I"),
            NJ_ARG_INT(args, 1));
    nj_iput(vm, self, m3g_fof(vm, NJ_C_IMAGE2D, "_nj_h", "I"),
            NJ_ARG_INT(args, 2));
    nj_rput(vm, self, m3g_fof(vm, NJ_C_IMAGE2D, "_nj_pixels", "[B"),
            NJ_ARG_REF(args, 3));
    m3g_image_dirty(vm, self);
    return NJ_OK;
}
/*
 * Image2D(int format, int width, int height, byte[] image, byte[] palette).
 * The palette form is a real JSR 184 shape - the file format carries one -
 * and this runtime keeps it beside the pixels rather than in a second class.
 */
NJFN(m3g_img2d_init_paletted)
{
    nj_ref self = NJ_THIS(args);

    if (NJ_ARG_INT(args, 0) < 96 || NJ_ARG_INT(args, 0) > 100) {
        return nj_throw(vm, "java/lang/IllegalArgumentException", "image format");
    }
    nj_iput(vm, self, m3g_fof(vm, NJ_C_IMAGE2D, "_nj_fmt", "I"),
            NJ_ARG_INT(args, 0));
    nj_iput(vm, self, m3g_fof(vm, NJ_C_IMAGE2D, "_nj_w", "I"),
            NJ_ARG_INT(args, 1));
    nj_iput(vm, self, m3g_fof(vm, NJ_C_IMAGE2D, "_nj_h", "I"),
            NJ_ARG_INT(args, 2));
    nj_rput(vm, self, m3g_fof(vm, NJ_C_IMAGE2D, "_nj_pixels", "[B"),
            NJ_ARG_REF(args, 3));
    m3g_image_dirty(vm, self);
    nj_rput(vm, self, m3g_fof(vm, NJ_C_IMAGE2D, "_nj_palette", "[B"),
            NJ_ARG_REF(args, 4));
    return NJ_OK;
}
/* Mutability is a constructor property, not whether an upload has occurred. */
NJFN(m3g_img2d_isMutable)
{
    nj_u4 flags = (nj_u4)nj_iget(vm, NJ_THIS(args),
                                 m3g_fof(vm, NJ_C_IMAGE2D, "_nj_fmt", "I"));
    ret->i = (flags & M3G_IMAGE_MUTABLE) != 0u;
    return NJ_OK;
}

/*
 * Transform.transform(VertexArray src, float[] dst, boolean w): each
 * vertex of the array through this matrix.  Three floats per vertex, or
 * four when the caller asks for the W it was computed with.
 */
NJFN(m3g_xf_transform_array)
{
    float *m = mat_of(vm, NJ_THIS(args), NJ_C_XFORM, "_nj_m");
    nj_ref va = NJ_ARG_REF(args, 0);
    nj_ref out = NJ_ARG_REF(args, 1);
    nj_s4 with_w = NJ_ARG_INT(args, 2);
    nj_ref data;
    nj_s4 nverts, comp, per, i;

    if (!m || !va || !out) {
        return NJ_OK;
    }
    data = nj_rget(vm, va, m3g_fof(vm, NJ_C_VARR, "_nj_data", "[S"));
    nverts = nj_iget(vm, va, m3g_fof(vm, NJ_C_VARR, "_nj_numVerts", "I"));
    /*
     * Components PER VERTEX, not the component size: this read _nj_compSize
     * (which is 2 for every collected title's vertex arrays, the size of a
     * short component) and so transformed two components per vertex and
     * stopped, leaving the rest of the output at zero.
     */
    comp = nj_iget(vm, va, m3g_fof(vm, NJ_C_VARR, "_nj_numComp", "I"));
    per = with_w ? 4 : 3;
    for (i = 0; i < nverts; i++) {
        float v[4];
        nj_s4 c;

        v[0] = v[1] = v[2] = 0.0f;
        v[3] = 1.0f;
        for (c = 0; c < 4 && c < comp; c++) {
            if (data && i * comp + c < (nj_s4)data->length) {
                v[c] = (float)((const nj_s2 *)data->u.bytes)[i * comp + c];
            }
        }
        for (c = 0; c < per; c++) {
            nj_s4 o = i * per + c;

            if (o >= (nj_s4)out->length) {
                break;
            }
            if (c == 3) {
                ((float *)out->u.bytes)[o] = m[12] * v[0] + m[13] * v[1]
                                           + m[14] * v[2] + m[15] * v[3];
            } else {
                ((float *)out->u.bytes)[o] = m[c * 4 + 0] * v[0]
                                           + m[c * 4 + 1] * v[1]
                                           + m[c * 4 + 2] * v[2]
                                           + m[c * 4 + 3] * v[3];
            }
        }
    }
    return NJ_OK;
}

/* ---- Group, Image2D and Transform natives listed above ------------- */
/* (registration rows live in the table below)                          */

/*
 * Group.pick(): "the ray hit nothing" is 0, and nothing here can hit
 * anything yet.  The two overloads differ in the far plane they take - a
 * distance or a Camera - and both answer the same way.
 */
NJFN(m3g_group_pick_vec)
{
    ret->i = 0;
    return NJ_OK;
}
NJFN(m3g_group_pick_cam)
{
    ret->i = 0;
    return NJ_OK;
}

const nj_builtin_class nj_lib_m3g_classes[] = {
    { NJ_C_O3D, "java/lang/Object", NJ_ACC_PUBLIC | NJ_ACC_ABSTRACT,
      o3d_fields, M3G_N(o3d_fields), o3d_methods, M3G_N(o3d_methods) },
    { NJ_C_XFORM, "java/lang/Object", NJ_ACC_PUBLIC,
      xform_fields, M3G_N(xform_fields), transform_methods, M3G_N(transform_methods) },
    { NJ_C_XABLE, NJ_C_O3D, NJ_ACC_PUBLIC | NJ_ACC_ABSTRACT,
      xable_fields, M3G_N(xable_fields), xable_methods, M3G_N(xable_methods) },
    { NJ_C_NODE, NJ_C_XABLE, NJ_ACC_PUBLIC | NJ_ACC_ABSTRACT,
      node_fields, M3G_N(node_fields), node_methods, M3G_N(node_methods) },
    { NJ_C_GROUP, NJ_C_NODE, NJ_ACC_PUBLIC,
      group_fields, M3G_N(group_fields), group_methods, M3G_N(group_methods) },
    { NJ_C_WORLD, NJ_C_GROUP, NJ_ACC_PUBLIC,
      world_fields, M3G_N(world_fields), world_methods, M3G_N(world_methods) },
    { NJ_C_CAMERA, NJ_C_NODE, NJ_ACC_PUBLIC,
      camera_fields, M3G_N(camera_fields), camera_methods, M3G_N(camera_methods) },
    { NJ_C_BG, NJ_C_O3D, NJ_ACC_PUBLIC,
      bg_fields, M3G_N(bg_fields), bg_methods, M3G_N(bg_methods) },
    { NJ_C_G3D, "java/lang/Object", NJ_ACC_PUBLIC,
      g3d_fields, M3G_N(g3d_fields), g3d_methods, M3G_N(g3d_methods) },
    { NJ_C_LOADER, "java/lang/Object", NJ_ACC_PUBLIC,
      NULL, 0u, loader_methods, M3G_N(loader_methods) },
    { NJ_C_MAT, NJ_C_O3D, NJ_ACC_PUBLIC,
      mat_fields, M3G_N(mat_fields), mat_methods, M3G_N(mat_methods) },
    { NJ_C_LIGHT, NJ_C_NODE, NJ_ACC_PUBLIC,
      light_fields, M3G_N(light_fields), light_methods, M3G_N(light_methods) },
    { NJ_C_VBUF, NJ_C_O3D, NJ_ACC_PUBLIC,
      vbuf_fields, M3G_N(vbuf_fields), vbuf_methods, M3G_N(vbuf_methods) },
    { NJ_C_IBUF, NJ_C_O3D, NJ_ACC_PUBLIC,
      ibuf_fields, M3G_N(ibuf_fields), ibuf_methods, M3G_N(ibuf_methods) },
    { NJ_C_APPEAR, NJ_C_O3D, NJ_ACC_PUBLIC,
      appear_fields, M3G_N(appear_fields), appear_methods, M3G_N(appear_methods) },
    { NJ_C_POLY, NJ_C_O3D, NJ_ACC_PUBLIC,
      poly_fields, M3G_N(poly_fields), poly_methods, M3G_N(poly_methods) },
    { NJ_C_COMP, NJ_C_O3D, NJ_ACC_PUBLIC,
      comp_fields, M3G_N(comp_fields), comp_methods, M3G_N(comp_methods) },
    { NJ_C_TEX2D, NJ_C_XABLE, NJ_ACC_PUBLIC,
      tex2d_fields, M3G_N(tex2d_fields), tex2d_methods, M3G_N(tex2d_methods) },
    { NJ_C_MESH, NJ_C_NODE, NJ_ACC_PUBLIC,
      mesh_fields, M3G_N(mesh_fields), mesh_methods, M3G_N(mesh_methods) },
    /* Morphing and skinned meshes are Meshes; the file format gives them
       their own types, and the loader maps those types to these names. */
    { NJ_C_MORPH, NJ_C_MESH, NJ_ACC_PUBLIC,
      morph_fields, M3G_N(morph_fields), morph_methods, M3G_N(morph_methods) },
    { NJ_C_SKIN, NJ_C_MESH, NJ_ACC_PUBLIC,
      skin_fields, M3G_N(skin_fields), skin_methods, M3G_N(skin_methods) },
    { NJ_C_ATRACK, "java/lang/Object", NJ_ACC_PUBLIC,
      atrack_fields, M3G_N(atrack_fields), atrack_methods, M3G_N(atrack_methods) },

    /*
     * The rest of the package exists so that a title which only names these
     * classes - as a field type, a cast, or an instanceof - loads.  None of
     * the collected titles calls a method on any of them, so none is
     * declared here: a method that is not declared is a NoSuchMethodError
     * the moment a title does call it, which is a clearer signal than an
     * empty implementation that silently returns nothing.
     */
    { NJ_C_SPRITE3D, NJ_C_NODE,     NJ_ACC_PUBLIC,
      sprite_fields, M3G_N(sprite_fields), sprite_methods, M3G_N(sprite_methods) },
    /* The file format calls type 18 "Sprite"; the titles call the class
       Sprite3D.  Both names exist so neither the loader nor a MIDlet
       trips over the other one. */
    { NJ_C_M3G_SPRITE, NJ_C_NODE,     NJ_ACC_PUBLIC,
      sprite_fields, M3G_N(sprite_fields), sprite_methods, M3G_N(sprite_methods) },
    { NJ_C_FOG, NJ_C_O3D, NJ_ACC_PUBLIC,
      fog_fields, M3G_N(fog_fields), fog_methods, M3G_N(fog_methods) },
    { NJ_C_RAY, NJ_C_O3D, NJ_ACC_PUBLIC,
      ray_fields, M3G_N(ray_fields), ray_methods, M3G_N(ray_methods) },
    { NJ_C_ACTL, NJ_C_O3D, NJ_ACC_PUBLIC,
      actl_fields, M3G_N(actl_fields), actl_methods, M3G_N(actl_methods) },
    { NJ_C_KFS, NJ_C_O3D, NJ_ACC_PUBLIC,
      kfs_fields, M3G_N(kfs_fields), kfs_methods, M3G_N(kfs_methods) },
    { NJ_C_VARR,     NJ_C_O3D,      NJ_ACC_PUBLIC,
      varr_fields, M3G_N(varr_fields), varr_methods, M3G_N(varr_methods) },
    { NJ_C_TSA,      NJ_C_IBUF,     NJ_ACC_PUBLIC,
      tsa_fields, M3G_N(tsa_fields), tsa_methods, M3G_N(tsa_methods) },
    { NJ_C_IMAGE2D, NJ_C_O3D, NJ_ACC_PUBLIC,
      img2d_fields, M3G_N(img2d_fields), img2d_methods, M3G_N(img2d_methods) },
    { NULL, NULL, 0u, NULL, 0u, NULL, 0u }
};
const int nj_lib_m3g_class_count =
    (int)(sizeof(nj_lib_m3g_classes) / sizeof(nj_lib_m3g_classes[0])) - 1;

const nj_native_entry nj_lib_m3g_table[] = {
    { NJ_C_O3D, "setUserID", "(" "I)V", m3g_userID_set },
    { NJ_C_TSA, "<init>",          "(" "[I[I)V", m3g_tsa_init_lengths },
    { NJ_C_TSA, "<init>",          "(" "I[I)V",  m3g_tsa_init_first },
    { NJ_C_TSA, "getStripCount",   "()I",        m3g_tsa_getStripCount },
    { NJ_C_TSA, "getIndexCount",   "()I",        m3g_tsa_getIndexCount },
    { NJ_C_TSA, "getStripLengths", "(" "[I)V",   m3g_tsa_getStripLengths },
    { NJ_C_O3D, "getUserID", "()I",     m3g_userID_get },
    { NJ_C_O3D, "animate",   "(I)I",    m3g_animate },
    { NJ_C_O3D, "addAnimationTrack",
      "(L" NJ_C_ATRACK ";)V", m3g_track_add },
    { NJ_C_O3D, "getAnimationTrackCount", "()I", m3g_track_count },
    { NJ_C_O3D, "getAnimationTrack",
      "(I)L" NJ_C_ATRACK ";", m3g_track_get },
    { NJ_C_O3D, "duplicate", "()L" NJ_C_O3D ";", m3g_duplicate },
    { NJ_C_O3D, "find",      "(I)L" NJ_C_O3D ";", m3g_find },
    { NJ_C_XFORM, "<init>",        "()V",                 m3g_xf_init },
    { NJ_C_XFORM, "setIdentity",   "()V",                 m3g_xf_setIdentity },
    { NJ_C_XFORM, "set",           "(L" NJ_C_XFORM ";)V", m3g_xf_set_xf },
    { NJ_C_XFORM, "set",           "([F)V",               m3g_xf_set_arr },
    { NJ_C_XFORM, "postMultiply",  "(L" NJ_C_XFORM ";)V", m3g_xf_postMultiply },
    { NJ_C_XFORM, "postTranslate", "(FFF)V",              m3g_xf_postTranslate },
    { NJ_C_XFORM, "postScale",     "(FFF)V",              m3g_xf_postScale },
    { NJ_C_XFORM, "postRotate",    "(FFFF)V",             m3g_xf_postRotate },
    { NJ_C_XFORM, "invert",        "()V",                 m3g_xf_invert },
    { NJ_C_XFORM, "get",           "([F)V",               m3g_xf_get },
    { NJ_C_XFORM, "transform",     "([F)V",               m3g_xf_transform },
    { NJ_C_XABLE, "setTransform",   "(L" NJ_C_XFORM ";)V", m3g_xable_setTransform },
    { NJ_C_XABLE, "getTransform",   "(L" NJ_C_XFORM ";)V", m3g_xable_getTransform },
    { NJ_C_XABLE, "setScale",       "(FFF)V",              m3g_xable_setScale },
    { NJ_C_XABLE, "getScale",       "([F)V",               m3g_xable_getScale },
    { NJ_C_XABLE, "setOrientation", "(FFFF)V",             m3g_xable_setOrientation },
    { NJ_C_XABLE, "getOrientation", "([F)V",               m3g_xable_getOrientation },
    { NJ_C_XABLE, "setTranslation", "(FFF)V",              m3g_xable_setTranslation },
    { NJ_C_XABLE, "getTranslation", "([F)V",               m3g_xable_getTranslation },
    { NJ_C_XABLE, "translate",      "(FFF)V",              m3g_xable_translate },
    { NJ_C_XABLE, "scale",          "(FFF)V",              m3g_xable_scale },
    { NJ_C_XABLE, "postRotate",     "(FFFF)V",             m3g_xable_postRotate },
    { NJ_C_XABLE, "getCompositeTransform",
      "(L" NJ_C_XFORM ";)V", m3g_xable_getCompositeTransform },
    { NJ_C_XFORM, "transpose",      "()V",                 m3g_xf_transpose },
    { NJ_C_NODE, "setScope",        "(I)V",                m3g_node_scope_set },
    { NJ_C_NODE, "getScope",        "()I",                 m3g_node_scope_get },
    { NJ_C_NODE, "setPickingEnable","(Z)V",                m3g_node_pick_set },
    { NJ_C_NODE, "isPickingEnabled","()Z",                 m3g_node_pick_get },
    { NJ_C_NODE, "setAlignment",
      "(L" NJ_C_NODE ";IL" NJ_C_NODE ";I)V", m3g_node_align_set },
    { NJ_C_NODE, "setRenderingEnable", "(Z)V",              m3g_render_set },
    { NJ_C_NODE, "isRenderingEnabled", "()Z",               m3g_render_get },
    { NJ_C_NODE, "setAlphaFactor",     "(F)V",              m3g_alpha_set },
    { NJ_C_NODE, "getParent",          "()L" NJ_C_NODE ";", m3g_parent_get },
    { NJ_C_NODE, "getTransformTo",
      "(L" NJ_C_NODE ";L" NJ_C_XFORM ";)Z", m3g_getTransformTo },
    { NJ_C_GROUP, "addChild",      "(L" NJ_C_NODE ";)V",   m3g_addChild },
    { NJ_C_GROUP, "removeChild",   "(L" NJ_C_NODE ";)V",   m3g_removeChild },
    { NJ_C_GROUP, "getChild",      "(I)L" NJ_C_NODE ";",   m3g_getChild },
    { NJ_C_GROUP, "getChildCount", "()I",                  m3g_getChildCount },
    { NJ_C_WORLD, "setActiveCamera", "(L" NJ_C_CAMERA ";)V", m3g_setActiveCamera },
    { NJ_C_WORLD, "setBackground",   "(L" NJ_C_BG ";)V",     m3g_setBackground },
    { NJ_C_BG, "setImage",           "(L" NJ_C_IMAGE2D ";)V", m3g_bg_setImage },
    { NJ_C_BG, "getImage",           "()L" NJ_C_IMAGE2D ";",  m3g_bg_getImage },
    { NJ_C_BG, "setImageMode",       "(II)V",                 m3g_bg_setImageMode },
    { NJ_C_BG, "getImageMode",       "()I",                   m3g_bg_getImageMode },
    { NJ_C_BG, "setCrop",            "(IIII)V",               m3g_bg_setCrop },
    { NJ_C_BG, "getCropX",           "()I",                   m3g_bg_getCropX },
    { NJ_C_BG, "getCropY",           "()I",                   m3g_bg_getCropY },
    { NJ_C_BG, "getCropWidth",       "()I",                   m3g_bg_getCropW },
    { NJ_C_BG, "getCropHeight",      "()I",                   m3g_bg_getCropH },
    { NJ_C_WORLD, "getActiveCamera", "()L" NJ_C_CAMERA ";",  m3g_getActiveCamera },
    { NJ_C_WORLD, "getBackground",   "()L" NJ_C_BG ";",      m3g_getBackground },
    { NJ_C_CAMERA, "setPerspective", "(FFFF)V",              m3g_setPerspective },
    { NJ_C_CAMERA, "setParallel",    "(FFFF)V",              m3g_setParallel },
    { NJ_C_CAMERA, "getProjection",  "(L" NJ_C_XFORM ";)I",  m3g_getProjection },
    { NJ_C_CAMERA, "setGeneric",     "(L" NJ_C_XFORM ";)V",  m3g_cam_setGeneric },
    { NJ_C_BG, "setColor",            "(I)V", m3g_bg_setColor },
    { NJ_C_BG, "setColorClearEnable", "(Z)V", m3g_bg_setColorClear },
    { NJ_C_BG, "isColorClearEnabled", "()Z",  m3g_bg_isColorClear },
    { NJ_C_BG, "setDepthClearEnable", "(Z)V", m3g_bg_setDepthClear },
    { NJ_C_BG, "isDepthClearEnabled", "()Z",  m3g_bg_isDepthClear },
    { NJ_C_G3D, "<init>",        "()V",                    m3g_noop_init },
    { NJ_C_G3D, "getInstance",   "()L" NJ_C_G3D ";",       m3g_getInstance },
    { NJ_C_G3D, "bindTarget",    "(Ljava/lang/Object;)V",  m3g_bindTarget },
    { NJ_C_G3D, "bindTarget",    "(Ljava/lang/Object;ZI)V", m3g_bindTarget },
    { NJ_C_G3D, "releaseTarget", "()V",                    m3g_releaseTarget },
    { NJ_C_G3D, "setViewport",   "(IIII)V",                m3g_setViewport },
    { NJ_C_G3D, "setCamera",
      "(L" NJ_C_CAMERA ";L" NJ_C_XFORM ";)V", m3g_setCamera },
    { NJ_C_G3D, "clear",         "(L" NJ_C_BG ";)V",       m3g_clear },
    { NJ_C_G3D, "render",        "(L" NJ_C_WORLD ";)V",    m3g_render },
    { NJ_C_G3D, "render",
      "(L" NJ_C_NODE ";L" NJ_C_XFORM ";)V", m3g_render_node },
    { NJ_C_G3D, "resetLights",   "()V",                    m3g_g3d_resetLights },
    { NJ_C_G3D, "addLight",      "(L" NJ_C_LIGHT ";L" NJ_C_XFORM ";)I", m3g_g3d_addLight },
    { NJ_C_LOADER, "load", "(Ljava/lang/String;)[L" NJ_C_O3D ";", m3g_load },
    { NJ_C_LOADER, "load", "([BI)[L" NJ_C_O3D ";", m3g_load_bytes },
    { NJ_C_APPEAR, "setMaterial",        "(L" NJ_C_MAT ";)" "V", m3g_app_setMaterial },
    { NJ_C_APPEAR, "setPolygonMode",     "(L" NJ_C_POLY ";)V",   m3g_app_setPolygonMode },
    { NJ_C_APPEAR, "getPolygonMode",     "()L" NJ_C_POLY ";",    m3g_app_getPolygonMode },
    { NJ_C_APPEAR, "setCompositingMode", "(L" NJ_C_COMP ";)V",   m3g_app_setCompositingMode },
    { NJ_C_APPEAR, "getCompositingMode", "()L" NJ_C_COMP ";",    m3g_app_getCompositingMode },
    { NJ_C_APPEAR, "setTexture",         "(IL" NJ_C_TEX2D ";)V", m3g_app_setTexture },
    { NJ_C_APPEAR, "getTexture",         "(I)L" NJ_C_TEX2D ";",  m3g_app_getTexture },
    { NJ_C_APPEAR, "setLayer",           "(I)V",                 m3g_app_setLayer },
    { NJ_C_POLY, "setCulling",                    "(I)V", m3g_poly_setCulling },
    { NJ_C_POLY, "setShading",                    "(I)V", m3g_poly_setShading },
    { NJ_C_POLY, "getShading",                    "()I",  m3g_poly_getShading },
    { NJ_C_POLY, "setTwoSidedLightingEnable",     "(Z)V", m3g_poly_setTwoSided },
    { NJ_C_POLY, "isTwoSidedLightingEnabled",     "()Z",  m3g_poly_getTwoSided },
    { NJ_C_POLY, "getCulling",                    "()I",  m3g_poly_getCulling },
    { NJ_C_POLY, "setWinding",                    "(I)V", m3g_poly_setWinding },
    { NJ_C_POLY, "setPerspectiveCorrectionEnable", "(Z)V", m3g_poly_setPersp },
    { NJ_C_COMP, "setBlending",         "(I)V", m3g_comp_setBlending },
    { NJ_C_COMP, "getBlending",         "()I",  m3g_comp_getBlending },
    { NJ_C_COMP, "setAlphaThreshold",   "(F)V", m3g_comp_setAlphaThreshold },
    { NJ_C_COMP, "setDepthTestEnable",  "(Z)V", m3g_comp_setDepthTest },
    { NJ_C_COMP, "setDepthWriteEnable", "(Z)V", m3g_comp_setDepthWrite },
    { NJ_C_TEX2D, "<init>",      "(L" NJ_C_IMAGE2D ";)V", m3g_tex2d_init },
    { NJ_C_VBUF, "setDefaultColor",   "(I)V", m3g_vbuf_setDefaultColor },
    { NJ_C_VBUF, "getDefaultColor",   "()I",  m3g_vbuf_getDefaultColor },
    { NJ_C_VBUF, "setPositions",      "(L" NJ_C_VARR ";F[F)V", m3g_vbuf_setPositions },
    { NJ_C_VBUF, "setColors",         "(L" NJ_C_VARR ";)V",    m3g_vbuf_setColors },
    { NJ_C_VBUF, "setTexCoords",      "(IL" NJ_C_VARR ";I)V",  m3g_vbuf_setTexCoords },
    { NJ_C_VBUF, "setTexCoords",      "(IL" NJ_C_VARR ";F[F)V", m3g_vbuf_setTexCoords_ff },
    { NJ_C_VARR, "<init>",            "(III)V", m3g_varr_init },
    { NJ_C_VARR, "set",               "(II[S)V", m3g_varr_set_short },
    { NJ_C_VARR, "getVertexCount",    "()I",    m3g_varr_getVertexCount },
    { NJ_C_VARR, "getComponentCount", "()I",    m3g_varr_getComponentCount },
    { NJ_C_VARR, "getComponentSize",  "()I",    m3g_varr_getComponentSize },
    { NJ_C_TEX2D, "setImage",    "(L" NJ_C_IMAGE2D ";)V", m3g_tex_setImage },
    { NJ_C_TEX2D, "setFiltering", "(II)V", m3g_tex_setFiltering },
    { NJ_C_TEX2D, "getFiltering", "()I",   m3g_tex_getFiltering },
    { NJ_C_TEX2D, "getImage",     "()L" NJ_C_IMAGE2D ";", m3g_tex_getImage },
    { NJ_C_TEX2D, "setBlending", "(I)V",  m3g_tex_setBlending },
    { NJ_C_TEX2D, "setWrapping", "(II)V", m3g_tex_setWrapping },
    { NJ_C_SPRITE3D, "<init>", "(ZL" NJ_C_IMAGE2D ";L" NJ_C_APPEAR ";)V",
      m3g_sprite_init },
    { NJ_C_SPRITE3D, "setImage",      "(L" NJ_C_IMAGE2D ";)V", m3g_sprite_setImage },
    { NJ_C_SPRITE3D, "setAppearance", "(L" NJ_C_APPEAR ";)V",  m3g_sprite_setAppearance },
    { NJ_C_MESH, "getVertexBuffer", "()L" NJ_C_VBUF ";",  m3g_mesh_getVertexBuffer },
    { NJ_C_MESH, "setVertexBuffer", "(L" NJ_C_VBUF ";)V", m3g_mesh_setVertexBuffer },
    { NJ_C_MESH, "getIndexBuffer",  "(I)L" NJ_C_IBUF ";", m3g_mesh_getIndexBuffer },
    { NJ_C_MESH, "getSubmeshCount", "()I",                m3g_mesh_getSubmeshCount },
    { NJ_C_MESH, "setAppearance",   "(IL" NJ_C_APPEAR ";)V", m3g_mesh_setAppearance },
    { NJ_C_MESH, "getAppearance",   "(I)L" NJ_C_APPEAR ";",  m3g_mesh_getAppearance },
    { NJ_C_IMAGE2D, "<init>", "(III)V",  m3g_img2d_init3 },
    { NJ_C_IMAGE2D, "<init>", "(II[B)V", m3g_img2d_init_bytes },
    { NJ_C_IMAGE2D, "<init>", "(ILjava/lang/Object;)V", m3g_img2d_init_obj },
    { NJ_C_IMAGE2D, "set", "(IIII[B)V", m3g_img2d_set },
    { NJ_C_IMAGE2D, "getFormat", "()I", m3g_img2d_getFormat },
    { NJ_C_IMAGE2D, "getWidth",  "()I", m3g_img2d_getWidth },
    { NJ_C_IMAGE2D, "getHeight", "()I", m3g_img2d_getHeight },
    { NJ_C_LIGHT, "<init>",           "()V",    m3g_light_init },
    { NJ_C_LIGHT, "setMode",          "(I)V",   m3g_light_setMode },
    { NJ_C_LIGHT, "getMode",          "()I",    m3g_light_getMode },
    { NJ_C_LIGHT, "setColor",         "(I)V",   m3g_light_setColor },
    { NJ_C_LIGHT, "getColor",         "()I",    m3g_light_getColor },
    { NJ_C_LIGHT, "setIntensity",     "(F)V",   m3g_light_setIntensity },
    { NJ_C_LIGHT, "getIntensity",     "()F",    m3g_light_getIntensity },
    { NJ_C_LIGHT, "setSpotAngle",     "(F)V",   m3g_light_setSpotAngle },
    { NJ_C_LIGHT, "getSpotAngle",     "()F",    m3g_light_getSpotAngle },
    { NJ_C_LIGHT, "setSpotExponent",  "(F)V",   m3g_light_setSpotExponent },
    { NJ_C_LIGHT, "getSpotExponent",  "()F",    m3g_light_getSpotExponent },
    { NJ_C_LIGHT, "setAttenuation",   "(FFF)V", m3g_light_setAttenuation },
    { NJ_C_LIGHT, "getAttenuation",   "([F)V",  m3g_light_getAttenuation },
    { NJ_C_MAT, "<init>", "()V", m3g_mat_init },
    { NJ_C_MAT, "setColor", "(II)V", m3g_mat_setColor },
    { NJ_C_MAT, "getColor", "(I)I", m3g_mat_getColor },
    { NJ_C_MAT, "setShininess", "(F)V", m3g_mat_setShininess },
    { NJ_C_MAT, "getShininess", "()F", m3g_mat_getShininess },
    { NJ_C_MAT, "setVertexColorTrackingEnable", "(Z)V", m3g_mat_setTrackColor },
    { NJ_C_MAT, "isVertexColorTrackingEnabled", "()Z", m3g_mat_isTrackColor },
    { NJ_C_FOG, "<init>", "()V", m3g_fog_init },
    { NJ_C_FOG, "setMode", "(I)V", m3g_fog_setMode },
    { NJ_C_FOG, "getMode", "()I", m3g_fog_getMode },
    { NJ_C_FOG, "setColor", "(I)V", m3g_fog_setColor },
    { NJ_C_FOG, "getColor", "()I", m3g_fog_getColor },
    { NJ_C_FOG, "setDensity", "(F)V", m3g_fog_setDensity },
    { NJ_C_FOG, "getDensity", "()F", m3g_fog_getDensity },
    { NJ_C_FOG, "setLinear", "(FF)V", m3g_fog_setLinear },
    { NJ_C_FOG, "getNearDistance", "()F", m3g_fog_getNear },
    { NJ_C_FOG, "getFarDistance", "()F", m3g_fog_getFar },
    { NJ_C_IBUF, "getIndexCount", "()I", m3g_ibuf_getIndexCount },
    { NJ_C_IBUF, "getIndices", "([I)V", m3g_ibuf_getIndices },
    { NJ_C_KFS, "<init>", "(III)V", m3g_kfs_init },
    { NJ_C_KFS, "getComponentCount", "()I", m3g_kfs_getComponentCount },
    { NJ_C_KFS, "getKeyframeCount", "()I", m3g_kfs_getKeyframeCount },
    { NJ_C_KFS, "getInterpolationType", "()I", m3g_kfs_getInterpolationType },
    { NJ_C_KFS, "setKeyframe", "(II[F)V", m3g_kfs_setKeyframe },
    { NJ_C_KFS, "getKeyframe", "(I[F)I", m3g_kfs_getKeyframe },
    { NJ_C_KFS, "setValidRange", "(II)V", m3g_kfs_setValidRange },
    { NJ_C_KFS, "getValidRangeFirst", "()I", m3g_kfs_getValidRangeFirst },
    { NJ_C_KFS, "getValidRangeLast", "()I", m3g_kfs_getValidRangeLast },
    { NJ_C_KFS, "setDuration", "(I)V", m3g_kfs_setDuration },
    { NJ_C_KFS, "getDuration", "()I", m3g_kfs_getDuration },
    { NJ_C_KFS, "setRepeatMode", "(I)V", m3g_kfs_setRepeatMode },
    { NJ_C_KFS, "getRepeatMode", "()I", m3g_kfs_getRepeatMode },
    { NJ_C_ACTL, "<init>", "()V", m3g_actl_init },
    { NJ_C_ACTL, "setActiveInterval", "(II)V", m3g_actl_setActiveInterval },
    { NJ_C_ACTL, "getActiveIntervalStart", "()I", m3g_actl_getActiveIntervalStart },
    { NJ_C_ACTL, "getActiveIntervalEnd", "()I", m3g_actl_getActiveIntervalEnd },
    { NJ_C_ACTL, "setSpeed", "(FI)V", m3g_actl_setSpeed },
    { NJ_C_ACTL, "getSpeed", "()F", m3g_actl_getSpeed },
    { NJ_C_ACTL, "setPosition", "(FI)V", m3g_actl_setPosition },
    { NJ_C_ACTL, "getPosition", "(I)F", m3g_actl_getPosition },
    { NJ_C_ACTL, "getRefWorldTime", "()I", m3g_actl_getRefWorldTime },
    { NJ_C_ACTL, "setWeight", "(F)V", m3g_actl_setWeight },
    { NJ_C_ACTL, "getWeight", "()F", m3g_actl_getWeight },
    { NJ_C_ATRACK, "<init>", "(L" NJ_C_KFS ";I)V", m3g_atrack_init },
    { NJ_C_ATRACK, "setController", "(L" NJ_C_ACTL ";)V", m3g_atrack_setController },
    { NJ_C_ATRACK, "getController", "()L" NJ_C_ACTL ";", m3g_atrack_getController },
    { NJ_C_ATRACK, "getKeyframeSequence", "()L" NJ_C_KFS ";", m3g_atrack_getSequence },
    { NJ_C_ATRACK, "getTargetProperty", "()I", m3g_atrack_getProperty },
    { NJ_C_RAY, "<init>", "()V", m3g_ray_init },
    { NJ_C_RAY, "getIntersected", "()L" NJ_C_NODE ";", m3g_ray_getIntersected },
    { NJ_C_RAY, "getRay", "([F)V", m3g_ray_getRay },
    { NJ_C_RAY, "getDistance", "()F", m3g_ray_getDistance },
    { NJ_C_RAY, "getSubmeshIndex", "()I", m3g_ray_getSubmeshIndex },
    { NJ_C_RAY, "getTextureS", "(I)F", m3g_ray_getTextureS },
    { NJ_C_RAY, "getTextureT", "(I)F", m3g_ray_getTextureT },
    { NJ_C_RAY, "getNormalX", "()F", m3g_ray_getNormalX },
    { NJ_C_RAY, "getNormalY", "()F", m3g_ray_getNormalY },
    { NJ_C_RAY, "getNormalZ", "()F", m3g_ray_getNormalZ },
    { NJ_C_MESH, "<init>", "(L" NJ_C_VBUF ";L" NJ_C_IBUF ";L" NJ_C_APPEAR ";)V", m3g_mesh_init },
    { NJ_C_MESH, "<init>", "(L" NJ_C_VBUF ";[L" NJ_C_IBUF ";[L" NJ_C_APPEAR ";)V", m3g_mesh_init_arrays },
    { NJ_C_MORPH, "<init>", "(L" NJ_C_VBUF ";[L" NJ_C_VBUF ";L" NJ_C_IBUF ";L" NJ_C_APPEAR ";)V", m3g_morph_init },
    { NJ_C_MORPH, "<init>", "(L" NJ_C_VBUF ";[L" NJ_C_VBUF ";[L" NJ_C_IBUF ";[L" NJ_C_APPEAR ";)V", m3g_morph_init_arrays },
    { NJ_C_MORPH, "getMorphTarget", "(I)L" NJ_C_VBUF ";", m3g_morph_getTarget },
    { NJ_C_MORPH, "getMorphTargetCount", "()I", m3g_morph_getTargetCount },
    { NJ_C_MORPH, "setWeights", "([F)V", m3g_morph_setWeights },
    { NJ_C_MORPH, "getWeights", "([F)V", m3g_morph_getWeights },
    { NJ_C_SKIN, "<init>", "(L" NJ_C_VBUF ";L" NJ_C_IBUF ";L" NJ_C_APPEAR ";L" NJ_C_GROUP ";)V", m3g_skin_init },
    { NJ_C_SKIN, "<init>", "(L" NJ_C_VBUF ";[L" NJ_C_IBUF ";[L" NJ_C_APPEAR ";L" NJ_C_GROUP ";)V", m3g_skin_init_arrays },
    { NJ_C_SKIN, "getSkeleton", "()L" NJ_C_GROUP ";", m3g_skin_getSkeleton },
    { NJ_C_SKIN, "addTransform", "(L" NJ_C_NODE ";III)V", m3g_skin_addTransform },
    { NJ_C_SKIN, "getBoneTransform", "(L" NJ_C_NODE ";L" NJ_C_XFORM ";)V", m3g_skin_getBoneTransform },
    { NJ_C_SKIN, "getBoneVertices", "(L" NJ_C_NODE ";[I[F)I", m3g_skin_getBoneVertices },
    { NJ_C_APPEAR, "<init>", "()V", m3g_appear_init },
    { NJ_C_APPEAR, "getLayer", "()I", m3g_appear_getLayer },
    { NJ_C_APPEAR, "setFog", "(Ljavax/microedition/m3g/Fog;)V", m3g_appear_setFog },
    { NJ_C_APPEAR, "getFog", "()Ljavax/microedition/m3g/Fog;", m3g_appear_getFog },
    { NJ_C_APPEAR, "getMaterial", "()Ljavax/microedition/m3g/Material;", m3g_appear_getMaterial },
    { NJ_C_BG, "<init>", "()V", m3g_bg_init },
    { NJ_C_BG, "getColor", "()I", m3g_bg_getColor },
    { NJ_C_BG, "getImageModeX", "()I", m3g_bg_getImageModeX },
    { NJ_C_BG, "getImageModeY", "()I", m3g_bg_getImageModeY },
    { NJ_C_CAMERA, "<init>", "()V", m3g_cam_init },
    { NJ_C_CAMERA, "getProjection", "([F)I", m3g_cam_getProjection_arr },
    { NJ_C_COMP, "<init>", "()V", m3g_comp_init },
    { NJ_C_COMP, "getAlphaThreshold", "()F", m3g_comp_getAlphaThreshold },
    { NJ_C_COMP, "setAlphaWriteEnable", "(Z)V", m3g_comp_setAlphaWrite },
    { NJ_C_COMP, "isAlphaWriteEnabled", "()Z", m3g_comp_isAlphaWrite },
    { NJ_C_COMP, "setColorWriteEnable", "(Z)V", m3g_comp_setColorWrite },
    { NJ_C_COMP, "isColorWriteEnabled", "()Z", m3g_comp_isColorWrite },
    { NJ_C_COMP, "isDepthWriteEnabled", "()Z", m3g_comp_isDepthWrite },
    { NJ_C_COMP, "isDepthTestEnabled", "()Z", m3g_comp_isDepthTest },
    { NJ_C_COMP, "setDepthOffset", "(FF)V", m3g_comp_setDepthOffset },
    { NJ_C_COMP, "getDepthOffsetFactor", "()F", m3g_comp_getDepthOffsetFactor },
    { NJ_C_COMP, "getDepthOffsetUnits", "()F", m3g_comp_getDepthOffsetUnits },
    { NJ_C_G3D, "getTarget", "()Ljava/lang/Object;", m3g_g3d_getTarget },
    { NJ_C_G3D, "getHints", "()I", m3g_g3d_getHints },
    { NJ_C_G3D, "isDepthBufferEnabled", "()Z", m3g_g3d_isDepthBuffer },
    { NJ_C_G3D, "getViewportX", "()I", m3g_g3d_getViewportX },
    { NJ_C_G3D, "getViewportY", "()I", m3g_g3d_getViewportY },
    { NJ_C_G3D, "getViewportWidth", "()I", m3g_g3d_getViewportW },
    { NJ_C_G3D, "getViewportHeight", "()I", m3g_g3d_getViewportH },
    { NJ_C_G3D, "setDepthRange", "(FF)V", m3g_g3d_setDepthRange },
    { NJ_C_G3D, "getDepthRangeNear", "()F", m3g_g3d_getDepthRangeNear },
    { NJ_C_G3D, "getDepthRangeFar", "()F", m3g_g3d_getDepthRangeFar },
    { NJ_C_G3D, "render", "(Ljavax/microedition/m3g/VertexBuffer;Ljavax/microedition/m3g/IndexBuffer;Ljavax/microedition/m3g/Appearance;Ljavax/microedition/m3g/Transform;I)V", m3g_g3d_render5 },
    { NJ_C_G3D, "render", "(Ljavax/microedition/m3g/VertexBuffer;Ljavax/microedition/m3g/IndexBuffer;Ljavax/microedition/m3g/Appearance;Ljavax/microedition/m3g/Transform;)V", m3g_g3d_render5 },
    { NJ_C_G3D, "getCamera", "(Ljavax/microedition/m3g/Transform;)Ljavax/microedition/m3g/Camera;", m3g_g3d_getCamera },
    { NJ_C_G3D, "setLight", "(ILjavax/microedition/m3g/Light;Ljavax/microedition/m3g/Transform;)V", m3g_g3d_setLight },
    { NJ_C_G3D, "getLightCount", "()I", m3g_g3d_getLightCount },
    { NJ_C_G3D, "getLight", "(ILjavax/microedition/m3g/Transform;)Ljavax/microedition/m3g/Light;", m3g_g3d_getLight },
    { NJ_C_G3D, "getProperties", "()Ljava/util/Hashtable;", m3g_g3d_getProperties },
    { NJ_C_GROUP, "<init>", "()V", m3g_group_init },
    { NJ_C_GROUP, "pick", "(IFFFFFFLjavax/microedition/m3g/RayIntersection;)Z", m3g_group_pick_vec },
    { NJ_C_GROUP, "pick", "(IFFLjavax/microedition/m3g/Camera;Ljavax/microedition/m3g/RayIntersection;)Z", m3g_group_pick_cam },
    { NJ_C_IMAGE2D, "<init>", "(III[B)V", m3g_img2d_init_pixels },
    { NJ_C_IMAGE2D, "<init>", "(III[B[B)V", m3g_img2d_init_paletted },
    { NJ_C_IMAGE2D, "isMutable", "()Z", m3g_img2d_isMutable },
    { NJ_C_LIGHT, "getConstantAttenuation", "()F", m3g_light_getConstantAtten },
    { NJ_C_LIGHT, "getLinearAttenuation", "()F", m3g_light_getLinearAtten },
    { NJ_C_LIGHT, "getQuadraticAttenuation", "()F", m3g_light_getQuadraticAtten },
    { NJ_C_NODE, "getAlphaFactor", "()F", m3g_node_getAlphaFactor },
    { NJ_C_NODE, "align", "(Ljavax/microedition/m3g/Node;)V", m3g_node_align },
    { NJ_C_NODE, "getAlignmentTarget", "(I)I", m3g_node_getAlignmentTarget },
    { NJ_C_NODE, "getAlignmentReference", "(I)Ljavax/microedition/m3g/Node;", m3g_node_getAlignmentReference },
    { NJ_C_O3D, "getReferences", "([Ljavax/microedition/m3g/Object3D;)I", m3g_o3d_getReferences },
    { NJ_C_O3D, "setUserObject", "(Ljava/lang/Object;)V", m3g_o3d_setUserObject },
    { NJ_C_O3D, "getUserObject", "()Ljava/lang/Object;", m3g_o3d_getUserObject },
    { NJ_C_O3D, "removeAnimationTrack", "(Ljavax/microedition/m3g/AnimationTrack;)V", m3g_o3d_removeAnimationTrack },
    { NJ_C_POLY, "<init>", "()V", m3g_poly_init },
    { NJ_C_POLY, "getWinding", "()I", m3g_poly_getWinding },
    { NJ_C_POLY, "setLocalCameraLightingEnable", "(Z)V", m3g_poly_setLocalCam },
    { NJ_C_POLY, "isLocalCameraLightingEnabled", "()Z", m3g_poly_isLocalCam },
    { NJ_C_POLY, "isPerspectiveCorrectionEnabled", "()Z", m3g_poly_isPersp },
    { NJ_C_SPRITE3D, "isScaled", "()Z", m3g_sprite_isScaled },
    { NJ_C_SPRITE3D, "getAppearance", "()Ljavax/microedition/m3g/Appearance;", m3g_sprite_getAppearance },
    { NJ_C_SPRITE3D, "getImage", "()Ljavax/microedition/m3g/Image2D;", m3g_sprite_getImage },
    { NJ_C_SPRITE3D, "setCrop", "(IIII)V", m3g_sprite_setCrop },
    { NJ_C_SPRITE3D, "getCropX", "()I", m3g_sprite_getCropX },
    { NJ_C_SPRITE3D, "getCropY", "()I", m3g_sprite_getCropY },
    { NJ_C_SPRITE3D, "getCropWidth", "()I", m3g_sprite_getCropW },
    { NJ_C_SPRITE3D, "getCropHeight", "()I", m3g_sprite_getCropH },
    /* The loader maps file type 18 to "Sprite" and a title names "Sprite3D";
       both class names are in the table, so both need the methods bound.
       A method declared by a class with no binding is a link error the
       moment a title calls it - test_natives is what caught this. */
    { NJ_C_M3G_SPRITE, "<init>", "(ZL" NJ_C_IMAGE2D ";L" NJ_C_APPEAR ";)V", m3g_sprite_init },
    { NJ_C_M3G_SPRITE, "setImage", "(L" NJ_C_IMAGE2D ";)V", m3g_sprite_setImage },
    { NJ_C_M3G_SPRITE, "setAppearance", "(L" NJ_C_APPEAR ";)V", m3g_sprite_setAppearance },
    { NJ_C_M3G_SPRITE, "isScaled", "()Z", m3g_sprite_isScaled },
    { NJ_C_M3G_SPRITE, "getAppearance", "()Ljavax/microedition/m3g/Appearance;", m3g_sprite_getAppearance },
    { NJ_C_M3G_SPRITE, "getImage", "()Ljavax/microedition/m3g/Image2D;", m3g_sprite_getImage },
    { NJ_C_M3G_SPRITE, "setCrop", "(IIII)V", m3g_sprite_setCrop },
    { NJ_C_M3G_SPRITE, "getCropX", "()I", m3g_sprite_getCropX },
    { NJ_C_M3G_SPRITE, "getCropY", "()I", m3g_sprite_getCropY },
    { NJ_C_M3G_SPRITE, "getCropWidth", "()I", m3g_sprite_getCropW },
    { NJ_C_M3G_SPRITE, "getCropHeight", "()I", m3g_sprite_getCropH },
    { NJ_C_TEX2D, "getLevelFilter", "()I", m3g_tex_getLevelFilter },
    { NJ_C_TEX2D, "getImageFilter", "()I", m3g_tex_getImageFilter },
    { NJ_C_TEX2D, "getWrappingS", "()I", m3g_tex_getWrappingS },
    { NJ_C_TEX2D, "getWrappingT", "()I", m3g_tex_getWrappingT },
    { NJ_C_TEX2D, "getBlending", "()I", m3g_tex_getBlending },
    { NJ_C_TEX2D, "setBlendColor", "(I)V", m3g_tex_setBlendColor },
    { NJ_C_TEX2D, "getBlendColor", "()I", m3g_tex_getBlendColor },
    { NJ_C_XFORM, "<init>", "(Ljavax/microedition/m3g/Transform;)V", m3g_xf_init_copy },
    { NJ_C_XFORM, "postRotateQuat", "(FFFF)V", m3g_xf_postRotateQuat },
    { NJ_C_XFORM, "transform", "(Ljavax/microedition/m3g/VertexArray;[FZ)V", m3g_xf_transform_array },
    { NJ_C_XABLE, "preRotate", "(FFFF)V", m3g_xable_preRotate },
    { NJ_C_VARR, "set", "(II[B)V", m3g_varr_set_bytes },
    { NJ_C_VARR, "getComponentType", "()I", m3g_varr_getComponentType },
    { NJ_C_VARR, "get", "(II[S)V", m3g_varr_get_short },
    { NJ_C_VARR, "get", "(II[B)V", m3g_varr_get_bytes },
    { NJ_C_VBUF, "<init>", "()V", m3g_vbuf_init },
    { NJ_C_VBUF, "getVertexCount", "()I", m3g_vbuf_getVertexCount },
    { NJ_C_VBUF, "setNormals", "(Ljavax/microedition/m3g/VertexArray;)V", m3g_vbuf_setNormals },
    { NJ_C_VBUF, "getPositions", "([F)Ljavax/microedition/m3g/VertexArray;", m3g_vbuf_getPositions },
    { NJ_C_VBUF, "getTexCoords", "(I[F)Ljavax/microedition/m3g/VertexArray;", m3g_vbuf_getTexCoords },
    { NJ_C_VBUF, "getNormals", "()Ljavax/microedition/m3g/VertexArray;", m3g_vbuf_getNormals },
    { NJ_C_VBUF, "getColors", "()Ljavax/microedition/m3g/VertexArray;", m3g_vbuf_getColors },
    { NJ_C_WORLD, "<init>", "()V", m3g_world_init },
    { NULL, NULL, NULL, NULL }
};
const int nj_lib_m3g_count =
    (int)(sizeof(nj_lib_m3g_table) / sizeof(nj_lib_m3g_table[0])) - 1;

#else  /* !NJ_ENABLE_M3G */

const nj_builtin_class nj_lib_m3g_classes[] = {
    { NULL, NULL, 0u, NULL, 0u, NULL, 0u }
};
const int nj_lib_m3g_class_count = 0;

const nj_native_entry nj_lib_m3g_table[] = {
    { NULL, NULL, NULL, NULL }
};
const int nj_lib_m3g_count = 0;

nj_status nj_m3g_late_init(nj_vm *vm)
{
    (void)vm;   /* nothing to seed: there are no m3g classes in this build */
    return NJ_OK;
}

#endif /* NJ_ENABLE_M3G */
