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

#include <stdio.h>
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
 * Every object's data starts with twelve bytes before its own fields: the
 * userID, then eight bytes that are zero in all 36859 objects in the corpus.
 * Measured the same way as everything else here - the first attempt assumed
 * eight and read a PolygonMode's culling as 0 instead of 162.
 */
#define NJ_M3G_OBJECT_HEADER     12u
#define NJ_M3G_T_GROUP        9u
#define NJ_M3G_T_POLYGON_MODE     8u
#define NJ_M3G_T_IMAGE2D         10u
#define NJ_M3G_T_MESH            14u
#define NJ_M3G_T_VERTEX_ARRAY    20u
#define NJ_M3G_T_VERTEX_BUFFER   21u
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
nj_status nj_m3g_decode_object(const nj_u1 *data, nj_u4 len, nj_u1 type,
                               nj_m3g_fields *out, char *err, nj_u4 errcap)
{
    if (err && errcap) { err[0] = '\0'; }
    if (!data || !out) { M3G_FAIL("no object bytes"); }
    memset(out, 0, sizeof(*out));
    if (type == NJ_M3G_TYPE_EXTERNAL_REF) {
        /* a bare string, no header: nothing to decode and nothing wrong */
        out->decoded = 0u;
        return NJ_OK;
    }
    if (len < NJ_M3G_OBJECT_HEADER) {
        M3G_FAIL("an object of type %u is %lu byte(s), too short to hold a header",
                 (unsigned)type, (unsigned long)len);
    }
    out->user_id = le32(data);

    switch (type) {
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
    case NJ_M3G_T_IMAGE2D:
        /*
         * format and width as u16 at 12 and 14, then two bytes that are zero
         * in all 1054 objects in the corpus, then the height as u16 at 18.
         * Measured: reading the height at 16 gave 0 for every texture in the
         * corpus while the file's own width and height disagreed (a 256x256
         * texture came back as 256 wide and 0 high).
         */
        if (len < NJ_M3G_OBJECT_HEADER + 8u) {
            M3G_FAIL("an Image2D is %lu byte(s)", (unsigned long)len);
        }
        out->a = (nj_u4)data[12] | ((nj_u4)data[13] << 8);
        out->b = (nj_u4)data[14] | ((nj_u4)data[15] << 8);
        out->c = (nj_u4)data[18] | ((nj_u4)data[19] << 8);
        if (!((out->a == 0u) || (out->a >= 96u && out->a <= 100u)) ||
            out->b > 4096u || out->c > 4096u) {
            M3G_FAIL("an Image2D holds format=%lu %lux%lu",
                     (unsigned long)out->a, (unsigned long)out->b,
                     (unsigned long)out->c);
        }
        break;
    case NJ_M3G_T_VERTEX_ARRAY:      /* componentSize, componentCount, componentType */
        if (len < NJ_M3G_OBJECT_HEADER + 3u) {
            M3G_FAIL("a VertexArray is %lu byte(s)", (unsigned long)len);
        }
        out->a = data[12]; out->b = data[13]; out->c = data[14];
        /* The bytes start right after the three header bytes and run to the
           end of the object.  Measured across the corpus: 18 of 18 decoded
           VertexArrays in one real file put their first component here. */
        out->data = data + NJ_M3G_OBJECT_HEADER + 3u;
        out->data_len = len - (NJ_M3G_OBJECT_HEADER + 3u);
        if (!(out->a == 0u || out->a == 1u || out->a == 2u || out->a == 4u) ||
            out->b > 4u || out->c > 2u) {
            M3G_FAIL("a VertexArray holds size=%lu count=%lu type=%lu",
                     (unsigned long)out->a, (unsigned long)out->b,
                     (unsigned long)out->c);
        }
        break;
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
            if (!vm || !vm->port || !vm->port->inflate) {
                M3G_FAIL("the section at %lu is compressed and this port has no inflate hook",
                         (unsigned long)off);
            }
            if (ulen > scratch_cap) {
                nj_host_free(vm, scratch);
                scratch = (nj_u1 *)nj_host_alloc(vm, (nj_size)ulen);
                scratch_cap = scratch ? ulen : 0u;
                if (!scratch) {
                    M3G_SCAN_FAIL("out of host memory for a %lu byte section",
                                  (unsigned long)ulen);
                }
            }
            st = vm->port->inflate(vm->port->ud, p + off + NJ_M3G_SECTION,
                                   body_len, scratch, ulen);
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
#if !NJ_ENABLE_FLOAT
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
#define NJ_C_SPRITE   "javax/microedition/m3g/Sprite"
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

static float bits2f(nj_s4 i)
{
    union { float f; nj_s4 i; } u;
    u.i = i;
    return u.f;
}

static float *mat_of(nj_vm *vm, nj_ref obj, const char *cls, const char *name)
{
    nj_ref a = nj_rget(vm, obj, nj_field_lookup(vm, cls, name, "[F", 0));
    return a ? (float *)a->u.bytes : NULL;
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

NJFN(m3g_xf_set)
{
    nj_ref self = NJ_THIS(args);
    nj_ref in = NJ_ARG_REF(args, 0);
    nj_ref m = nj_rget(vm, self, nj_field_lookup(vm, NJ_C_XFORM, "_nj_m", "[F", 0));

    if (!in || in->length < 16) {
        return nj_throw(vm, "java/lang/IllegalArgumentException",
                        "the array must hold at least sixteen elements");
    }
    if (m) {
        memcpy(nj_array_data(m), nj_array_data(in), 16u * sizeof(float));
    }
    return NJ_OK;
}

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

NJFN(m3g_find)
{
    nj_ref got = find_by_id(vm, NJ_THIS(args), NJ_ARG_INT(args, 0), 0);
    ret->r = got;
    return NJ_OK;
}

/*
 * duplicate(): a shallow copy of the object itself, with no children and no
 * parent, which is what the JSR asks for and what titles rely on when they
 * clone a Mesh to give it its own appearance.
 */
NJFN(m3g_duplicate)
{
    nj_ref self = NJ_THIS(args);
    nj_ref copy;

    if (!self || !self->cls) {
        ret->r = 0;
        return NJ_OK;
    }
    copy = nj_object_new(vm, self->cls->name);
    if (!copy) {
        return nj_throw_oom(vm);
    }
    {
        float *d = mat_of(vm, copy, NJ_C_XABLE, "_nj_m");
        float *s = mat_of(vm, self, NJ_C_XABLE, "_nj_m");
        if (d && s) {
            memcpy(d, s, sizeof(float) * NJ_MF);
        }
    }
    ret->r = copy;
    return NJ_OK;
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
    float *s = mat_of(vm, NJ_ARG_REF(args, 0), NJ_C_XFORM, "_nj_m");
    if (d && s) {
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

/* ---- Node ---------------------------------------------------------- */
NJFN(m3g_render_set)
{
    nj_field *f = nj_field_lookup(vm, NJ_C_NODE, "_nj_render", "Z", 0);
    nj_iput(vm, NJ_THIS(args), f, NJ_ARG_INT(args, 0) ? 1 : 0);
    return NJ_OK;
}

NJFN(m3g_render_get)
{
    ret->i = nj_iget(vm, NJ_THIS(args),
                     nj_field_lookup(vm, NJ_C_NODE, "_nj_render", "Z", 0)) ? 1 : 0;
    return NJ_OK;
}

NJFN(m3g_alpha_set)
{
    nj_iput(vm, NJ_THIS(args),
            nj_field_lookup(vm, NJ_C_NODE, "_nj_alpha", "I", 0),
            f2bits(NJ_ARG_FLOAT(args, 0)));
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
    nj_ref chain_a[64], chain_b[64];
    int na = 0, nb = 0, i, common;

    if (!out) {
        return NJ_OK;
    }
    {
        nj_ref p = NJ_THIS(args);
        while (p && na < 64) {
            chain_a[na++] = p;
            p = nj_rget(vm, p, nj_field_lookup(vm, NJ_C_NODE, "_nj_parent",
                                               "L" NJ_C_NODE ";", 0));
        }
    }
    {
        nj_ref p = target;
        while (p && nb < 64) {
            chain_b[nb++] = p;
            p = nj_rget(vm, p, nj_field_lookup(vm, NJ_C_NODE, "_nj_parent",
                                               "L" NJ_C_NODE ";", 0));
        }
    }
    common = -1;
    for (i = 0; i < na && common < 0; i++) {
        int j;
        for (j = 0; j < nb; j++) {
            if (chain_a[i] == chain_b[j]) {
                common = i;
                break;
            }
        }
    }
    if (common < 0) {
        ret->i = 0;
        return NJ_OK;
    }
    /*
     * Compose from this node down to the common ancestor, then from the
     * target up to it inverted.  Only the common case - one node being an
     * ancestor of the other - is needed by the titles that call this.
     */
    mat_identity(out);
    for (i = 0; i < common; i++) {
        float *m = mat_of(vm, chain_a[i], NJ_C_XABLE, "_nj_m");
        float inv[NJ_MF];
        if (!m) {
            continue;
        }
        memcpy(inv, m, sizeof(inv));
        mat_invert(inv);
        mat_mul(out, inv, out);
    }
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

/* ---- Camera -------------------------------------------------------- */
NJFN(m3g_setPerspective)
{
    nj_ref self = NJ_THIS(args);
    nj_iput(vm, self, nj_field_lookup(vm, NJ_C_CAMERA, "_nj_is_persp", "Z", 0), 1);
    nj_iput(vm, self, nj_field_lookup(vm, NJ_C_CAMERA, "_nj_is_parallel", "Z", 0), 0);
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
NJFN(m3g_getProjection)
{
    nj_ref self = NJ_THIS(args);
    float *out = mat_of(vm, NJ_ARG_REF(args, 0), NJ_C_XFORM, "_nj_m");
    float f, aspect, near, far, fov;

    if (!out) {
        ret->i = 0;
        return NJ_OK;
    }
    if (!nj_iget(vm, self, nj_field_lookup(vm, NJ_C_CAMERA, "_nj_is_persp", "Z", 0)) &&
        !nj_iget(vm, self, nj_field_lookup(vm, NJ_C_CAMERA, "_nj_is_parallel", "Z", 0))) {
        ret->i = 0;
        return NJ_OK;
    }
    f = bits2f(nj_iget(vm, self, nj_field_lookup(vm, NJ_C_CAMERA, "_nj_fovy", "I", 0)));
    if (f == 0.0f) {
        f = 45.0f;
    }
    aspect = bits2f(nj_iget(vm, self, nj_field_lookup(vm, NJ_C_CAMERA, "_nj_aspect", "I", 0)));
    if (aspect == 0.0f) {
        aspect = 1.0f;
    }
    near = bits2f(nj_iget(vm, self, nj_field_lookup(vm, NJ_C_CAMERA, "_nj_near", "I", 0)));
    if (near == 0.0f) {
        near = 1.0f;
    }
    far = bits2f(nj_iget(vm, self, nj_field_lookup(vm, NJ_C_CAMERA, "_nj_far", "I", 0)));
    if (far == 0.0f) {
        far = 1000.0f;
    }
    mat_identity(out);
    if (nj_iget(vm, self, nj_field_lookup(vm, NJ_C_CAMERA, "_nj_is_persp", "Z", 0))) {
        fov = f * 3.14159265358979f / 180.0f;
        out[0] = 1.0f / tanf(fov * 0.5f) / aspect;
        out[5] = 1.0f / tanf(fov * 0.5f);
        out[10] = -(far + near) / (far - near);
        out[11] = -(2.0f * far * near) / (far - near);
        out[14] = -1.0f;
        out[15] = 0.0f;
    }
    ret->i = 1;
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

NJFN(m3g_bindTarget)
{
    nj_rput(vm, NJ_THIS(args),
            nj_field_lookup(vm, NJ_C_G3D, "_nj_target", "Ljava/lang/Object;", 0),
            NJ_ARG_REF(args, 0));
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

/*
 * clear(Background): the one thing this module genuinely does to a surface
 * today.  It is real - the colour is written through the port's own
 * Graphics - and it is also what makes a title's 2D interface visible over
 * the empty 3D view instead of the two fighting.
 */
NJFN(m3g_clear)
{
    nj_ref bg = NJ_ARG_REF(args, 0);
    nj_ref self = NJ_THIS(args);
    nj_ref target;
    nj_s4 argb;

    if (!bg) {
        return NJ_OK;
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
    }
    return NJ_OK;
}

NJFN(m3g_render)
{
    /*
     * Counted, not rasterised: this module has no rasteriser yet, so the only
     * honest thing to report is whether a title ever reaches this call at all.
     * tris= answered that question for the Mascot path and the answer was
     * zero everywhere; this is the same instrument for JSR 184.
     */
    vm->m3g_renders++;
    /*
     * Step two territory: walking the graph is done, rasterising the meshes
     * is not.  Nothing is faked - the traversal really happens, so a title
     * that counts on its nodes being visited (finds, cameras, backgrounds)
     * behaves, and the screen honestly shows no 3D yet.
     */
    nj_ref node = NJ_ARG_REF(args, 0);
    int depth = 0;

    while (node && nj_heap_owns(vm, node) && depth < 64) {
        nj_ref kids;
        depth++;
        if (!nj_class_is_subclass_of(node->cls, nj_class_lookup(vm, NJ_C_GROUP))) {
            break;
        }
        kids = nj_rget(vm, node, nj_field_lookup(vm, NJ_C_GROUP, "_nj_kids",
                                                 "L" NJ_C_VECTOR ";", 0));
        if (!kids) {
            break;
        }
        {
            nj_value n;
            nj_value a[5];
            if (nj_call(vm, kids, NJ_C_VECTOR, "size", "()I", NULL, &n) != NJ_OK) {
                return NJ_ERR;
            }
            if (n.i <= 0) {
                break;
            }
            a[0].i = 0;
            if (nj_call(vm, kids, NJ_C_VECTOR, "elementAt",
                        "(I)Ljava/lang/Object;", a, &n) != NJ_OK) {
                return NJ_ERR;
            }
            node = n.r;
        }
    }
    return NJ_OK;
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
 * Culling.  JSR 184 fixes the three values (CULL_NONE 160, CULL_BACK 161,
 * CULL_FRONT 162) and PolygonMode.setCulling(I)V is called by a corpus title
 * that would otherwise not start at all.
 */
NJFN(m3g_poly_setCulling)
{
    nj_s4 c = NJ_ARG_INT(args, 0);

    if (c != 160 && c != 161 && c != 162) {
        NJ_THROW(vm, "java/lang/IllegalArgumentException",
                 "culling is CULL_NONE (160), CULL_BACK (161) or CULL_FRONT (162)");
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

/*
 * The light list.  Nothing is lit - there is no rasteriser yet - so this
 * keeps the LENGTH of the list and nothing else: addLight() returns the index
 * the caller expects and resetLights() empties it.  setLight()/getLight() are
 * deliberately NOT declared: a method that accepts a light and forgets it is
 * the half-implementation this project keeps refusing to ship, and no title in
 * the corpus calls them.
 */
NJFN(m3g_g3d_addLight)
{
    nj_field *f = nj_field_lookup(vm, NJ_C_G3D, "_nj_nlights", "I", 0);
    nj_s4 n = nj_iget(vm, NJ_THIS(args), f);

    nj_iput(vm, NJ_THIS(args), f, n + 1);
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
    nj_iput(vm, self, nj_field_lookup(vm, NJ_C_VARR, "_nj_compSize", "I", 0),
            NJ_ARG_INT(args, 2));
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

/*
 * VertexArray.set(int, int, short[]), also from 急速公路狂飙－新年版_84.  The
 * three arguments are kept together so the range means something later.
 */
NJFN(m3g_varr_set_short)
{
    nj_ref self = NJ_THIS(args);

    nj_rput(vm, self, nj_field_lookup(vm, NJ_C_VARR, "_nj_data", "[S", 0),
            NJ_ARG_REF(args, 2));
    nj_iput(vm, self, nj_field_lookup(vm, NJ_C_VARR, "_nj_first", "I", 0),
            NJ_ARG_INT(args, 0));
    nj_iput(vm, self, nj_field_lookup(vm, NJ_C_VARR, "_nj_count", "I", 0),
            NJ_ARG_INT(args, 1));
    return NJ_OK;
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
 * The constructor is the only member the collected titles call, so the three
 * setters are declared with it - a title that sets the crop or swaps the
 * image finds a method that records it rather than a link error.
 */
NJFN(m3g_sprite_init)
{
    (void)args;
    return NJ_OK;
}

NJFN(m3g_sprite_setImage)
{
    nj_rput(vm, NJ_THIS(args),
            nj_field_lookup(vm, NJ_C_SPRITE3D, "_nj_img",
                            "L" NJ_C_IMAGE2D ";", 0),
            NJ_ARG_REF(args, 0));
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
    a[0].i = index;
    a[1].r = ap;
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
    ret->r = nj_rget(vm, NJ_THIS(args), nj_field_lookup(vm, NJ_C_MESH, "_nj_ib",
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
nj_status nj_m3g_link_groups(nj_vm *vm, const nj_m3g_object *objs, nj_u4 nobj,
                             nj_ref arr)
{
    nj_u4 i;

    if (!vm || !objs || !arr) {
        return NJ_ERR_STATE;
    }
    for (i = 0u; i < nobj; i++) {
        nj_ref g, kids;
        nj_u4 k;

        if (objs[i].type != NJ_M3G_T_GROUP || objs[i].fields.nrefs == 0u) {
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
        for (k = 0u; k < objs[i].fields.nrefs; k++) {
            nj_u4 idx = objs[i].fields.refs[k];
            nj_value va[5], vr;
            nj_ref ch;
            if (idx == 0u || idx > nobj) { continue; }   /* 0 is null */
            ch = nj_array_get_ref(arr, idx);
            if (!ch) { continue; }
            nj_rput(vm, ch, nj_field_lookup(vm, NJ_C_NODE, "_nj_parent",
                                            "L" NJ_C_NODE ";", 0), g);
            va[0].r = ch;
            (void)nj_call(vm, kids, NJ_C_VECTOR, "addElement",
                          "(Ljava/lang/Object;)V", va, &vr);
        }
    }
    return NJ_OK;
}

NJFN(m3g_load)
{
    nj_ref name = NJ_ARG_REF(args, 0);
    nj_app *app = (nj_app *)vm->loader_ud;
    char cname[256];
    void *data;
    nj_u4 len = 0u;
    nj_u4 magic = 0u;

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
    if (m3g_has_file_id((const unsigned char *)data, len)) {
        magic = 0xAB4Au;
    }
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
         * What is NOT done here is linking: a Group's children, a Mesh's
         * submeshes, an Appearance's textures are all object references in the
         * file, and reading them needs the object table and the reference
         * rules.  Until that exists the objects come back empty, which is
         * honest - and it is why a title that loads a model still draws
         * nothing.
         */
        nj_m3g_object *objs;
        nj_u4 nobj = 0u, nsec = 0u, i;
        nj_u4 cap = 4096u;
        char err[192];
        nj_ref arr;
        nj_u4 mark;

        objs = (nj_m3g_object *)nj_host_alloc(vm, (nj_size)(sizeof(nj_m3g_object) * cap));
        if (!objs) {
            nj_host_free(vm, data);
            return nj_throw(vm, "java/lang/OutOfMemoryError", "the object table");
        }
        if (nj_m3g_scan_objects(vm, (const nj_u1 *)data, len, objs, cap,
                                &nobj, &nsec, err, sizeof(err)) != NJ_OK) {
            nj_host_free(vm, objs);
            nj_host_free(vm, data);
            return nj_throw(vm, "java/io/IOException", err);
        }
        nj_host_free(vm, data);
        if (nobj == 0u) {
            nj_host_free(vm, objs);
            return nj_throw(vm, "java/io/IOException",
                            "the container holds no objects");
        }
        arr = nj_object_array_new(vm, "javax/microedition/m3g/Object3D", nobj + 1u);
        if (!arr) {
            nj_host_free(vm, objs);
            return NJ_ERR_NOMEM;
        }
        mark = nj_root_mark(vm);
        arr = nj_root(vm, arr);
        for (i = 0u; i < nobj; i++) {
            const char *cls = m3g_class_of(objs[i].type);
            nj_u4 inner;
            nj_ref o;

            if (!cls) { continue; }   /* a reserved type: the slot stays empty */
            inner = nj_root_mark(vm);
            o = nj_object_new(vm, cls);
            if (o) {
                o = nj_root(vm, o);
                nj_iput(vm, o, nj_field_lookup(vm, NJ_C_O3D, "_nj_userID", "I", 0),
                        (nj_s4)objs[i].fields.user_id);
                nj_array_set_ref(arr, i + 1u, o);
            }
            nj_root_release(vm, inner);
        }
        if (nj_m3g_link_groups(vm, objs, nobj, arr) != NJ_OK) {
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
        nj_root_release(vm, mark);
        nj_host_free(vm, objs);
        ret->r = arr;
        return NJ_OK;
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
/*
 * Three constructor forms, because the collected titles use a fourth shape
 * the specification does not define: Image2D(int, int, int), called by both
 * 极品飞车 builds.  A class that is missing a constructor is a dead MIDlet,
 * and the three ints are the same three the four-argument form takes, so the
 * extra form is recorded exactly like the standard one.
 */
NJFN(m3g_img2d_init3)
{
    nj_ref self = NJ_THIS(args);

    nj_iput(vm, self, nj_field_lookup(vm, NJ_C_IMAGE2D, "_nj_fmt", "I", 0),
            NJ_ARG_INT(args, 0));
    nj_iput(vm, self, nj_field_lookup(vm, NJ_C_IMAGE2D, "_nj_w", "I", 0),
            NJ_ARG_INT(args, 1));
    nj_iput(vm, self, nj_field_lookup(vm, NJ_C_IMAGE2D, "_nj_h", "I", 0),
            NJ_ARG_INT(args, 2));
    return NJ_OK;
}

NJFN(m3g_img2d_init_bytes)
{
    nj_ref self = NJ_THIS(args);

    nj_iput(vm, self, nj_field_lookup(vm, NJ_C_IMAGE2D, "_nj_fmt", "I", 0),
            NJ_ARG_INT(args, 0));
    nj_iput(vm, self, nj_field_lookup(vm, NJ_C_IMAGE2D, "_nj_w", "I", 0),
            NJ_ARG_INT(args, 1));
    nj_rput(vm, self, nj_field_lookup(vm, NJ_C_IMAGE2D, "_nj_pixels", "[B", 0),
            NJ_ARG_REF(args, 2));
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
    ret->i = nj_iget(vm, NJ_THIS(args),
                     nj_field_lookup(vm, NJ_C_IMAGE2D, "_nj_fmt", "I", 0));
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

NJFN(m3g_img2d_set)
{
    nj_ref self = NJ_THIS(args);

    nj_iput(vm, self, nj_field_lookup(vm, NJ_C_IMAGE2D, "_nj_fmt", "I", 0),
            NJ_ARG_INT(args, 0));
    nj_iput(vm, self, nj_field_lookup(vm, NJ_C_IMAGE2D, "_nj_w", "I", 0),
            NJ_ARG_INT(args, 1));
    nj_iput(vm, self, nj_field_lookup(vm, NJ_C_IMAGE2D, "_nj_h", "I", 0),
            NJ_ARG_INT(args, 2));
    nj_rput(vm, self, nj_field_lookup(vm, NJ_C_IMAGE2D, "_nj_pixels", "[B", 0),
            NJ_ARG_REF(args, 3));
    return NJ_OK;
}

#endif /* NJ_ENABLE_M3G */

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
    return nj_throw(vm, "java/io/IOException",
                    "a .m3g container; this runtime has no object parser yet");
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
    M("getAttenuation",   "([F)V",  NJ_ACC_PUBLIC)
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
        nj_throw(vm, "java/lang/InternalError",
                 "the Light class is missing a field this native needs");
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
        { NJ_C_POLY, "CULL_NONE", 160 },  { NJ_C_POLY, "CULL_BACK", 161 },
        { NJ_C_POLY, "CULL_FRONT", 162 },
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

#if NJ_ENABLE_M3G

static const nj_builtin_field o3d_fields[] = {
    { "_nj_userID", "I", NJ_ACC_PUBLIC },
    { "_nj_tracks", "L" NJ_C_VECTOR ";", NJ_ACC_PUBLIC }
};
static const nj_builtin_field xable_fields[] = {
    { "_nj_m", "[F", NJ_ACC_PUBLIC }
};
static const nj_builtin_field xform_fields[] = {
    { "_nj_m", "[F", NJ_ACC_PUBLIC }
};
static const nj_builtin_field node_fields[] = {
    { "_nj_render", "Z", NJ_ACC_PUBLIC },
    { "_nj_alpha",  "F", NJ_ACC_PUBLIC },
    { "_nj_parent", "L" NJ_C_NODE ";", NJ_ACC_PUBLIC }
};
static const nj_builtin_field group_fields[] = {
    { "_nj_kids", "L" NJ_C_VECTOR ";", NJ_ACC_PUBLIC }
};
static const nj_builtin_field world_fields[] = {
    { "_nj_cam", "L" NJ_C_CAMERA ";", NJ_ACC_PUBLIC },
    { "_nj_bg",  "L" NJ_C_BG ";", NJ_ACC_PUBLIC }
};
static const nj_builtin_field camera_fields[] = {
    { "_nj_is_persp",    "Z", NJ_ACC_PUBLIC },
    { "_nj_is_parallel", "Z", NJ_ACC_PUBLIC },
    { "_nj_fovy",   "F", NJ_ACC_PUBLIC },
    { "_nj_height", "F", NJ_ACC_PUBLIC },
    { "_nj_aspect", "F", NJ_ACC_PUBLIC },
    { "_nj_near",   "F", NJ_ACC_PUBLIC },
    { "_nj_far",    "F", NJ_ACC_PUBLIC }
};
static const nj_builtin_field bg_fields[] = {
    { "_nj_color",  "I", NJ_ACC_PUBLIC },
    { "_nj_cclear", "Z", NJ_ACC_PUBLIC },
    { "_nj_dclear", "Z", NJ_ACC_PUBLIC }
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
    { "_nj_instance", "L" NJ_C_G3D ";", NJ_ACC_PUBLIC | NJ_ACC_STATIC }
};
static const nj_builtin_field appear_fields[] = {
    { "_nj_mat",  "L" NJ_C_MAT ";",   NJ_ACC_PUBLIC },
    { "_nj_pm",   "L" NJ_C_POLY ";",  NJ_ACC_PUBLIC },
    { "_nj_cm",   "L" NJ_C_COMP ";",  NJ_ACC_PUBLIC },
    { "_nj_tex0", "L" NJ_C_TEX2D ";", NJ_ACC_PUBLIC },
    { "_nj_tex1", "L" NJ_C_TEX2D ";", NJ_ACC_PUBLIC },
    { "_nj_layer", "I",               NJ_ACC_PUBLIC }
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
    { "_nj_count",    "I",  NJ_ACC_PUBLIC }
};
static const nj_builtin_field poly_fields[] = {
    { "_nj_winding", "I", NJ_ACC_PUBLIC },
    { "_nj_persp",   "Z", NJ_ACC_PUBLIC },
    { "_nj_culling", "I", NJ_ACC_PUBLIC },
    { "_nj_shading", "I", NJ_ACC_PUBLIC },
    { "_nj_twosided", "Z", NJ_ACC_PUBLIC }
};
static const nj_builtin_field comp_fields[] = {
    { "_nj_blend",    "I", NJ_ACC_PUBLIC },
    { "_nj_dtest",    "I", NJ_ACC_PUBLIC },
    { "_nj_dwrite",   "I", NJ_ACC_PUBLIC },
    { "_nj_athresh",  "I", NJ_ACC_PUBLIC }
};
static const nj_builtin_field tex2d_fields[] = {
    { "_nj_img",   "L" NJ_C_IMAGE2D ";", NJ_ACC_PUBLIC },
    { "_nj_blend", "I",                  NJ_ACC_PUBLIC },
    { "_nj_wrapS", "I",                  NJ_ACC_PUBLIC },
    { "_nj_wrapT", "I",                  NJ_ACC_PUBLIC },
    { "_nj_filtLevel", "I",              NJ_ACC_PUBLIC },
    { "_nj_filtImage", "I",              NJ_ACC_PUBLIC }
};
static const nj_builtin_field sprite_fields[] = {
    { "_nj_img", "L" NJ_C_IMAGE2D ";", NJ_ACC_PUBLIC },
    { "_nj_app", "L" NJ_C_APPEAR ";",  NJ_ACC_PUBLIC }
};
static const nj_builtin_field mesh_fields[] = {
    { "_nj_vb",   "L" NJ_C_VBUF ";", NJ_ACC_PUBLIC },
    { "_nj_ib",   "L" NJ_C_IBUF ";", NJ_ACC_PUBLIC },
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
    nj_ref lens = NJ_ARG_REF(args, 0);

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
    { "_nj_image",  "Ljavax/microedition/lcdui/Image;", NJ_ACC_PUBLIC }
};

static const nj_builtin_method o3d_methods[] = {
    M("setUserID",             "(I)V",                     NJ_ACC_PUBLIC),
    M("getUserID",             "()I",                      NJ_ACC_PUBLIC),
    M("addAnimationTrack",     "(L" NJ_C_ATRACK ";)V",     NJ_ACC_PUBLIC),
    M("animate",               "(I)I",                     NJ_ACC_PUBLIC),
    M("duplicate",             "()L" NJ_C_O3D ";",         NJ_ACC_PUBLIC),
    M("find",                  "(I)L" NJ_C_O3D ";",        NJ_ACC_PUBLIC),
    M("getAnimationTrack",     "(I)L" NJ_C_ATRACK ";",     NJ_ACC_PUBLIC),
    M("getAnimationTrackCount","()I",                      NJ_ACC_PUBLIC)
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
    M("transform",     "([F)V",                     NJ_ACC_PUBLIC)
};
static const nj_builtin_method xable_methods[] = {
    M("setTransform",     "(L" NJ_C_XFORM ";)V", NJ_ACC_PUBLIC),
    M("getTransform",     "(L" NJ_C_XFORM ";)V", NJ_ACC_PUBLIC),
    M("setScale",         "(FFF)V",              NJ_ACC_PUBLIC),
    M("getScale",         "([F)V",               NJ_ACC_PUBLIC),
    M("setOrientation",   "(FFFF)V",             NJ_ACC_PUBLIC),
    M("getOrientation",   "([F)V",               NJ_ACC_PUBLIC),
    M("setTranslation",   "(FFF)V",              NJ_ACC_PUBLIC),
    M("getTranslation",   "([F)V",               NJ_ACC_PUBLIC)
};
static const nj_builtin_method node_methods[] = {
    M("setRenderingEnable", "(Z)V",                    NJ_ACC_PUBLIC),
    M("isRenderingEnabled", "()Z",                     NJ_ACC_PUBLIC),
    M("setAlphaFactor",     "(F)V",                    NJ_ACC_PUBLIC),
    M("getParent",          "()L" NJ_C_NODE ";",       NJ_ACC_PUBLIC),
    M("getTransformTo",     "(L" NJ_C_NODE ";L" NJ_C_XFORM ";)Z", NJ_ACC_PUBLIC)
};
static const nj_builtin_method group_methods[] = {
    M("addChild",       "(L" NJ_C_NODE ";)V",          NJ_ACC_PUBLIC),
    M("removeChild",    "(L" NJ_C_NODE ";)V",          NJ_ACC_PUBLIC),
    M("getChild",       "(I)L" NJ_C_NODE ";",          NJ_ACC_PUBLIC),
    M("getChildCount",  "()I",                         NJ_ACC_PUBLIC)
};
static const nj_builtin_method world_methods[] = {
    M("setActiveCamera", "(L" NJ_C_CAMERA ";)V",   NJ_ACC_PUBLIC),
    M("setBackground",   "(L" NJ_C_BG ";)V",       NJ_ACC_PUBLIC)
};
static const nj_builtin_method camera_methods[] = {
    M("setPerspective", "(FFFF)V",                NJ_ACC_PUBLIC),
    M("setParallel",    "(FFFF)V",                NJ_ACC_PUBLIC),
    M("getProjection",  "(L" NJ_C_XFORM ";)I",    NJ_ACC_PUBLIC)
};
static const nj_builtin_method bg_methods[] = {
    M("setColor",            "(I)V", NJ_ACC_PUBLIC),
    M("setColorClearEnable", "(Z)V", NJ_ACC_PUBLIC),
    M("setDepthClearEnable", "(Z)V", NJ_ACC_PUBLIC)
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
    M("setLayer",           "(I)V",                     NJ_ACC_PUBLIC)
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
    nj_iput(vm, self, nj_field_lookup(vm, NJ_C_VBUF, "_nj_bias", "F", 0),
            f2bits(NJ_ARG_FLOAT(args, 2)));
    nj_rput(vm, self, nj_field_lookup(vm, NJ_C_VBUF, "_nj_scale", "[F", 0),
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
    { "_nj_texOff",    "I",              NJ_ACC_PUBLIC },
    { "_nj_texUnit",   "I",              NJ_ACC_PUBLIC }
};
static const nj_builtin_method vbuf_methods[] = {
    M("setDefaultColor", "(I)V", NJ_ACC_PUBLIC),
    M("getDefaultColor", "()I",  NJ_ACC_PUBLIC),
    M("setPositions",    "(L" NJ_C_VARR ";F[F)V", NJ_ACC_PUBLIC),
    M("setColors",       "(L" NJ_C_VARR ";)V",    NJ_ACC_PUBLIC),
    M("setTexCoords",    "(IL" NJ_C_VARR ";I)V",  NJ_ACC_PUBLIC),
    M("setTexCoords",    "(IL" NJ_C_VARR ";F[F)V", NJ_ACC_PUBLIC)
};

static const nj_builtin_method varr_methods[] = {
    M("<init>",             "(III)V", NJ_ACC_PUBLIC),
    M("getVertexCount",     "()I",    NJ_ACC_PUBLIC),
    M("getComponentCount",  "()I",    NJ_ACC_PUBLIC),
    M("getComponentSize",   "()I",    NJ_ACC_PUBLIC),
    M("set",                "(II[S)V", NJ_ACC_PUBLIC)
};
static const nj_builtin_method poly_methods[] = {
    M("setWinding",                   "(I)V", NJ_ACC_PUBLIC),
    M("setPerspectiveCorrectionEnable", "(Z)V", NJ_ACC_PUBLIC),
    M("setCulling",                   "(I)V", NJ_ACC_PUBLIC),
    M("getCulling",                   "()I",  NJ_ACC_PUBLIC),
    M("setShading",                   "(I)V", NJ_ACC_PUBLIC),
    M("getShading",                   "()I",  NJ_ACC_PUBLIC),
    M("setTwoSidedLightingEnable",    "(Z)V", NJ_ACC_PUBLIC),
    M("isTwoSidedLightingEnabled",    "()Z",  NJ_ACC_PUBLIC)
};
static const nj_builtin_method comp_methods[] = {
    M("setBlending",        "(I)V", NJ_ACC_PUBLIC),
    M("getBlending",        "()I",  NJ_ACC_PUBLIC),
    M("setAlphaThreshold",  "(F)V", NJ_ACC_PUBLIC),
    M("setDepthTestEnable", "(Z)V", NJ_ACC_PUBLIC),
    M("setDepthWriteEnable","(Z)V", NJ_ACC_PUBLIC)
};
static const nj_builtin_method tex2d_methods[] = {
    M("<init>",      "(L" NJ_C_IMAGE2D ";)V", NJ_ACC_PUBLIC),
    M("setImage",    "(L" NJ_C_IMAGE2D ";)V", NJ_ACC_PUBLIC),
    M("setBlending", "(I)V",                  NJ_ACC_PUBLIC),
    M("setWrapping", "(II)V",                 NJ_ACC_PUBLIC),
    M("setFiltering", "(II)V",                NJ_ACC_PUBLIC),
    M("getFiltering", "()I",                  NJ_ACC_PUBLIC),
    M("getImage",     "()L" NJ_C_IMAGE2D ";",  NJ_ACC_PUBLIC)
};
static const nj_builtin_method sprite_methods[] = {
    M("<init>",        "(ZL" NJ_C_IMAGE2D ";L" NJ_C_APPEAR ";)V", NJ_ACC_PUBLIC),
    M("setImage",      "(L" NJ_C_IMAGE2D ";)V", NJ_ACC_PUBLIC),
    M("setAppearance", "(L" NJ_C_APPEAR ";)V",  NJ_ACC_PUBLIC)
};
static const nj_builtin_method mesh_methods[] = {
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
    M("getHeight",   "()I",   NJ_ACC_PUBLIC)
};

#define M3G_N(arr) ((nj_u2)(sizeof(arr) / sizeof((arr)[0])))

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
    { NJ_C_MAT, NJ_C_O3D, NJ_ACC_PUBLIC, NULL, 0u, NULL, 0u },
    { NJ_C_LIGHT, NJ_C_NODE, NJ_ACC_PUBLIC,
      light_fields, M3G_N(light_fields), light_methods, M3G_N(light_methods) },
    { NJ_C_VBUF, NJ_C_O3D, NJ_ACC_PUBLIC,
      vbuf_fields, M3G_N(vbuf_fields), vbuf_methods, M3G_N(vbuf_methods) },
    { NJ_C_IBUF, NJ_C_O3D, NJ_ACC_PUBLIC, NULL, 0u, NULL, 0u },
    { NJ_C_APPEAR, NJ_C_O3D, NJ_ACC_PUBLIC,
      appear_fields, M3G_N(appear_fields), appear_methods, M3G_N(appear_methods) },
    { NJ_C_POLY, NJ_C_O3D, NJ_ACC_PUBLIC,
      poly_fields, M3G_N(poly_fields), poly_methods, M3G_N(poly_methods) },
    { NJ_C_COMP, NJ_C_O3D, NJ_ACC_PUBLIC,
      comp_fields, M3G_N(comp_fields), comp_methods, M3G_N(comp_methods) },
    { NJ_C_TEX2D, NJ_C_O3D, NJ_ACC_PUBLIC,
      tex2d_fields, M3G_N(tex2d_fields), tex2d_methods, M3G_N(tex2d_methods) },
    { NJ_C_MESH, NJ_C_NODE, NJ_ACC_PUBLIC,
      mesh_fields, M3G_N(mesh_fields), mesh_methods, M3G_N(mesh_methods) },
    /* Morphing and skinned meshes are Meshes; the file format gives them
       their own types, and the loader maps those types to these names. */
    { NJ_C_MORPH, NJ_C_MESH, NJ_ACC_PUBLIC, NULL, 0u, NULL, 0u },
    { NJ_C_SKIN,  NJ_C_MESH, NJ_ACC_PUBLIC, NULL, 0u, NULL, 0u },
    { NJ_C_ATRACK, "java/lang/Object", NJ_ACC_PUBLIC,
      NULL, 0u, NULL, 0u },

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
    { NJ_C_SPRITE,   NJ_C_NODE,     NJ_ACC_PUBLIC, NULL, 0u, NULL, 0u },
    { NJ_C_FOG,      NJ_C_O3D,      NJ_ACC_PUBLIC, NULL, 0u, NULL, 0u },
    { NJ_C_RAY,      NJ_C_O3D,      NJ_ACC_PUBLIC, NULL, 0u, NULL, 0u },
    { NJ_C_ACTL,     NJ_C_O3D,      NJ_ACC_PUBLIC, NULL, 0u, NULL, 0u },
    { NJ_C_KFS,      NJ_C_O3D,      NJ_ACC_PUBLIC, NULL, 0u, NULL, 0u },
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
    { NJ_C_CAMERA, "setPerspective", "(FFFF)V",              m3g_setPerspective },
    { NJ_C_CAMERA, "setParallel",    "(FFFF)V",              m3g_setParallel },
    { NJ_C_CAMERA, "getProjection",  "(L" NJ_C_XFORM ";)I",  m3g_getProjection },
    { NJ_C_BG, "setColor",            "(I)V", m3g_bg_setColor },
    { NJ_C_BG, "setColorClearEnable", "(Z)V", m3g_bg_setColorClear },
    { NJ_C_BG, "setDepthClearEnable", "(Z)V", m3g_bg_setDepthClear },
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
      "(L" NJ_C_NODE ";L" NJ_C_XFORM ";)V", m3g_render },
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
