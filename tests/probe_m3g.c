/*
 * nano_j2me - probe_m3g.c
 *
 * A one-file command line wrapper around the two .m3g probes, so that
 * tools/check_m3g_reals.sh can point it at every .m3g file the collected games
 * actually ship.  That script is the check tests/test_m3g_file.c cannot be:
 * the unit test encodes the format, so if the format is written down wrongly
 * the test agrees with the code and both are wrong together.  Real files do
 * not agree with anything.
 *
 * This has already earned its place.  The first version of the object walk
 * treated an object's length field as including its own five byte header, and
 * the unit test agreed; running the walk over the real files turned 36859
 * objects into a stream of nonsense types, which is how the mistake was found.
 * It was found here and not in the test, because the test was built from the
 * same misunderstanding.
 *
 * zlib lives here and not in the runtime on purpose: the runtime is meant to
 * build without libc, so decompression belongs behind a port hook, and this
 * tool is a host program that may link whatever the host has.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>
#include "nj2me/nj.h"

nj_status nj_m3g_container_probe(const nj_u1 *p, nj_u4 n,
                                 nj_u1 *ids, nj_u4 id_count,
                                 char *err, nj_u4 errcap);
nj_status nj_m3g_objects_probe(const nj_u1 *p, nj_u4 n, nj_u4 *count,
                               nj_u1 *types, nj_u4 type_count,
                               char *err, nj_u4 errcap);

#define MAX_SECTIONS 64u
#define MAX_TYPES 24u

static const char *type_name(unsigned t)
{
    switch (t) {
    case 0u:   return "Header";
    case 1u:   return "AnimationController";
    case 2u:   return "AnimationTrack";
    case 3u:   return "Appearance";
    case 4u:   return "Background";
    case 5u:   return "Camera";
    case 6u:   return "CompositingMode";
    case 7u:   return "Fog";
    case 8u:   return "PolygonMode";
    case 9u:   return "Group";
    case 10u:  return "Image2D";
    case 11u:  return "TriangleStripArray";
    case 12u:  return "Light";
    case 13u:  return "Material";
    case 14u:  return "Mesh";
    case 15u:  return "MorphingMesh";
    case 16u:  return "SkinnedMesh";
    case 17u:  return "Texture2D";
    case 18u:  return "Sprite";
    case 19u:  return "KeyframeSequence";
    case 20u:  return "VertexArray";
    case 21u:  return "VertexBuffer";
    case 22u:  return "World";
    case 255u: return "ExternalReference";
    default:   return "?";
    }
}

static nj_u4 le32(const nj_u1 *p)
{
    return (nj_u4)p[0] | ((nj_u4)p[1] << 8) |
           ((nj_u4)p[2] << 16) | ((nj_u4)p[3] << 24);
}

int main(int argc, char **argv)
{
    FILE *f;
    long len;
    nj_u1 *buf;
    nj_u1 ids[MAX_SECTIONS];
    char err[256];
    nj_status st;
    nj_u4 i, off, nsections = 0, nobjects = 0;
    nj_u4 tally[256];
    unsigned shown = 0;

    if (argc != 2) { fprintf(stderr, "usage: probe_m3g FILE\n"); return 2; }
    f = fopen(argv[1], "rb");
    if (!f) { perror(argv[1]); return 2; }
    if (fseek(f, 0, SEEK_END) != 0 || (len = ftell(f)) < 0 ||
        fseek(f, 0, SEEK_SET) != 0) { fclose(f); return 2; }
    buf = (nj_u1 *)malloc((size_t)len ? (size_t)len : 1u);
    if (!buf) { fclose(f); return 2; }
    if (fread(buf, 1u, (size_t)len, f) != (size_t)len) { free(buf); fclose(f); return 2; }
    fclose(f);

    memset(tally, 0, sizeof(tally));
    st = nj_m3g_container_probe(buf, (nj_u4)len, ids, MAX_SECTIONS, err, sizeof(err));
    if (st != NJ_OK) {
        printf("REJECTED %d: %s\n", (int)st, err);
        free(buf);
        return 1;
    }

    /* Walk the sections again here, because the probe reports ids and not
       offsets, and this tool needs the offsets to reach the objects. */
    for (off = 12u; off < (nj_u4)len; ) {
        unsigned scheme = buf[off];
        nj_u4 clen = le32(buf + off + 1u);
        nj_u4 ulen = le32(buf + off + 5u);
        nj_u4 count = 0;
        nj_u1 types[MAX_TYPES];
        nj_u1 *body = NULL;

        if (clen < 13u || off + clen > (nj_u4)len) {
            printf("REJECTED section at %lu: length %lu\n",
                   (unsigned long)off, (unsigned long)clen);
            free(buf);
            return 1;
        }
        if (ulen != 0u) {
            const nj_u1 *objs = buf + off + 9u;
            nj_u4 objs_len = clen - 13u;
            if (scheme == 1u) {
                uLongf out = (uLongf)ulen;
                body = (nj_u1 *)malloc(ulen ? ulen : 1u);
                if (!body || uncompress(body, &out, objs, (uLong)objs_len) != Z_OK ||
                    (nj_u4)out != ulen) {
                    printf("REJECTED section at %lu: %lu byte(s) of zlib did not inflate to %lu\n",
                           (unsigned long)off, (unsigned long)objs_len, (unsigned long)ulen);
                    free(body); free(buf);
                    return 1;
                }
            } else if (objs_len != ulen) {
                printf("REJECTED section at %lu: stored, but %lu byte(s) of objects against %lu declared\n",
                       (unsigned long)off, (unsigned long)objs_len, (unsigned long)ulen);
                free(buf);
                return 1;
            }
            st = nj_m3g_objects_probe(body ? body : objs, ulen, &count, types,
                                      MAX_TYPES, err, sizeof(err));
            if (st != NJ_OK) {
                printf("REJECTED section at %lu: %s\n", (unsigned long)off, err);
                free(body); free(buf);
                return 1;
            }
            {
                nj_u4 k;
                for (k = 0; k < count && k < MAX_TYPES; k++) { tally[types[k]]++; }
            }
            nobjects += count;
            free(body);
        }
        nsections++;
        off += clen;
    }

    printf("OK %lu bytes, %lu section(s), %lu object(s)",
           (unsigned long)len, (unsigned long)nsections, (unsigned long)nobjects);
    for (i = 0; i < 256u; i++) {
        if (tally[i] != 0u && shown < 5u) {
            printf("%s%s=%lu", shown ? " " : " [", type_name((unsigned)i),
                   (unsigned long)tally[i]);
            shown++;
        }
    }
    if (shown) { printf("]"); }
    printf("\n");
    free(buf);
    return 0;
}
