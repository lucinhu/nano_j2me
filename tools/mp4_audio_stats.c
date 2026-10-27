/*
 * Print the video and audio track statistics of an MP4/3GP with the runtime's
 * own container parser, so the three corpus movies can be checked against
 * ffprobe without decoding anything.
 *
 * Build and run (the parser is plain C and needs no library beyond the files
 * themselves):
 *
 *   cc -O2 -std=c99 -I include -I src/midp -o /tmp/mp4_audio_stats \
 *      tools/mp4_audio_stats.c src/midp/nj_mp4.c
 *   /tmp/mp4_audio_stats clip.3gp
 *
 * Output, one line per requested track plus a per-sample verification:
 *   audio: rc codec timescale duration samples channels sample_rate
 *          first=[offset size dts duration key] total_bytes
 *   video: rc codec WxH samples fps first=[offset size]
 * The tool also walks every audio sample and reports whether offsets are
 * contiguous and every sample lies inside the file, which is what the
 * "hand out offset/size/dts reliably" contract means in practice.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "nj2me/nj.h"
#include "nj_mp4.h"

static unsigned char *read_file(const char *path, long *size)
{
    FILE *f = fopen(path, "rb");
    long  sz;
    unsigned char *p;

    if (f == NULL) {
        return NULL;
    }
    fseek(f, 0, SEEK_END);
    sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    p = (unsigned char *)malloc((size_t)sz);
    if (p == NULL || fread(p, 1, (size_t)sz, f) != (size_t)sz) {
        free(p);
        p = NULL;
    }
    fclose(f);
    *size = sz;
    return p;
}

int main(int argc, char **argv)
{
    unsigned char *data;
    long           size = 0;
    nj_mp4         m;
    nj_mp4_sample  s;
    int            rc, n;
    unsigned long long total = 0;

    if (argc < 2) {
        fprintf(stderr, "usage: %s clip.3gp [clip.3gp ...]\n", argv[0]);
        return 2;
    }
    for (int f = 1; f < argc; f++) {
        data = read_file(argv[f], &size);
        if (data == NULL) {
            fprintf(stderr, "%s: cannot read\n", argv[f]);
            continue;
        }
        printf("file: %s (%ld bytes)\n", argv[f], size);

        rc = nj_mp4_open_audio(&m, data, (nj_u4)size);
        printf("audio: rc=%d", rc);
        if (rc == NJ_MP4_OK) {
            int bounded = 1, monotonic = 1;
            nj_u4 prev_end = 0;

            printf(" codec=0x%08x timescale=%u duration=%u samples=%u "
                   "channels=%u sample_rate=%u",
                   (unsigned)m.codec, (unsigned)m.timescale,
                   (unsigned)m.duration, (unsigned)m.sample_count,
                   (unsigned)m.channels, (unsigned)m.sample_rate);
            n = 0;
            total = 0;
            while ((rc = nj_mp4_next(&m, &s)) == 1) {
                if ((unsigned)size < s.size ||
                    s.offset > (nj_u4)size - s.size) {
                    bounded = 0;
                }
                /* Interleaved tracks jump forward between chunks; a sample
                 * that starts before the previous one ended is a bad offset. */
                if (n > 0 && s.offset < prev_end) {
                    monotonic = 0;
                }
                if (n == 0) {
                    printf(" first=[%u %u %u %u %u]",
                           (unsigned)s.offset, (unsigned)s.size,
                           (unsigned)s.dts, (unsigned)s.duration, s.key);
                }
                prev_end = s.offset + s.size;
                total += s.size;
                n++;
            }
            printf(" walked=%d next_rc=%d total_bytes=%llu bounded=%s "
                   "monotonic=%s\n", n, rc, total,
                   bounded ? "yes" : "no", monotonic ? "yes" : "no");
        } else {
            printf("\n");
        }

        rc = nj_mp4_open(&m, data, (nj_u4)size);
        printf("video: rc=%d", rc);
        if (rc == NJ_MP4_OK) {
            printf(" codec=0x%08x %ux%u samples=%u fps=%u",
                   (unsigned)m.codec, (unsigned)m.width, (unsigned)m.height,
                   (unsigned)m.sample_count, (unsigned)m.fps);
            if (nj_mp4_next(&m, &s) == 1) {
                printf(" first=[%u %u]", (unsigned)s.offset,
                       (unsigned)s.size);
            }
        }
        printf("\n");
        free(data);
    }
    return 0;
}
