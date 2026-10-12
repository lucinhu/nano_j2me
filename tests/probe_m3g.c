/*
 * nano_j2me - probe_m3g.c
 *
 * A one-file command line wrapper around the container probe, so that
 * tools/check_m3g_reals.sh can point it at every .m3g file the collected games
 * actually ship.  That script is the check tests/test_m3g_file.c cannot be:
 * the unit test encodes the format, so if the format is written down wrongly
 * the test agrees with the code and both are wrong together.  Real files do
 * not agree with anything.
 */
#include <stdio.h>
#include <stdlib.h>
#include "nj2me/nj.h"

nj_status nj_m3g_container_probe(const nj_u1 *p, nj_u4 n,
                                 nj_u1 *ids, nj_u4 id_count,
                                 char *err, nj_u4 errcap);

int main(int argc, char **argv)
{
    FILE *f;
    long len;
    nj_u1 *buf;
    nj_u1 ids[16];
    char err[256];
    nj_status st;
    nj_u4 i, nids = 0;

    if (argc != 2) { fprintf(stderr, "usage: probe_m3g FILE\n"); return 2; }
    f = fopen(argv[1], "rb");
    if (!f) { perror(argv[1]); return 2; }
    if (fseek(f, 0, SEEK_END) != 0 || (len = ftell(f)) < 0 ||
        fseek(f, 0, SEEK_SET) != 0) { fclose(f); return 2; }
    buf = (nj_u1 *)malloc((size_t)len ? (size_t)len : 1u);
    if (!buf) { fclose(f); return 2; }
    if (fread(buf, 1u, (size_t)len, f) != (size_t)len) { free(buf); fclose(f); return 2; }
    fclose(f);

    st = nj_m3g_container_probe(buf, (nj_u4)len, ids, 16u, err, sizeof(err));
    for (i = 0; i < 16u && ids[i] != 0xffu; i++) { nids++; }
    if (st == NJ_OK) {
        printf("OK %lu bytes, %lu section(s)\n", (unsigned long)len,
               (unsigned long)nids);
    } else {
        printf("REJECTED %d: %s\n", (int)st, err);
    }
    free(buf);
    return st == NJ_OK ? 0 : 1;
}
