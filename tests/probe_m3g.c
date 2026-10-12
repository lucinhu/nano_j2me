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
#include "nj2me/nj_vm.h"


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
    nj_port port;
    nj_vm *vm;
    static nj_m3g_object objs[65536];
    nj_u4 count = 0, nsections = 0, i;
    nj_u4 tally[256];
    unsigned shown = 0;
    char err[256];
    nj_status st;

    if (argc != 2) { fprintf(stderr, "usage: probe_m3g FILE\n"); return 2; }
    f = fopen(argv[1], "rb");
    if (!f) { perror(argv[1]); return 2; }
    if (fseek(f, 0, SEEK_END) != 0 || (len = ftell(f)) < 0 ||
        fseek(f, 0, SEEK_SET) != 0) { fclose(f); return 2; }
    buf = (nj_u1 *)malloc((size_t)len ? (size_t)len : 1u);
    if (!buf) { fclose(f); return 2; }
    if (fread(buf, 1u, (size_t)len, f) != (size_t)len) { free(buf); fclose(f); return 2; }
    fclose(f);

    /*
     * The runtime's scanner, not a copy of it.  The sections are inflated
     * through the port's inflate hook - the same hook a device would fill in -
     * so what this checks is the code the emulator will actually run, and a
     * mistake in the runtime cannot be cancelled out by the same mistake
     * written twice.
     */
    if (nj_port_headless(&port) != NJ_OK) { free(buf); return 2; }
    vm = nj_vm_create(&port, 1u << 20);
    if (!vm) { free(buf); return 2; }

    st = nj_m3g_scan_objects(vm, buf, (nj_u4)len, objs, 65536u, &count,
                             &nsections, err, sizeof(err));
    if (st != NJ_OK) {
        printf("REJECTED %d: %s\n", (int)st, err);
        nj_vm_destroy(vm);
        free(buf);
        return 1;
    }

    memset(tally, 0, sizeof(tally));
    for (i = 0; i < count && i < 65536u; i++) { tally[objs[i].type]++; }

    printf("OK %lu bytes, %lu section(s), %lu object(s)",
           (unsigned long)len, (unsigned long)nsections, (unsigned long)count);
    for (i = 0; i < 256u; i++) {
        if (tally[i] != 0u && shown < 5u) {
            printf("%s%s=%lu", shown ? " " : " [", type_name((unsigned)i),
                   (unsigned long)tally[i]);
            shown++;
        }
    }
    if (shown) { printf("]"); }
    printf("\n");

    nj_vm_destroy(vm);
    free(buf);
    return 0;
}
