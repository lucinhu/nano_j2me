/*
 * Decode a movie through the runtime's Java-level media Player and dump the
 * RGB565 frames it produces.
 *
 * This is the Player path, not the provider path: Manager.createPlayer() is
 * called exactly as a MIDlet would call it, the Player keeps the source array
 * and its RGB565 frame buffer, and nj_media_frame() is the pump the runtime
 * calls once per emulated display frame.  Each frame is written as
 * <prefix>-NNN.ppm and its FNV-1a printed as "rgb565 frame N fnv 0x...", so a
 * reference conversion (tools/m4v_dump.c --convert-yuv) can be compared with
 * diff and the PPMs with tools/compare_m4v.py-style byte comparison.
 *
 * Built and run by tools/m4v_player_probe.sh, which takes the compile flags
 * from the runtime build directory: this file touches the VM structs, so it
 * must be compiled with the same nj_config switches as the library.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "nj2me/nj.h"
#include "nj_internal.h"
#include "nj_midp.h"

#define MEDIA_MGR "javax/microedition/media/Manager"
#define BAIS      "java/io/ByteArrayInputStream"

static unsigned long fnv1a(const unsigned char *p, unsigned long n)
{
    unsigned long h = 2166136261ul;

    while (n-- > 0u) {
        h ^= *p++;
        h *= 16777619ul;
        h &= 0xFFFFFFFFul;
    }
    return h;
}

static nj_ref make_player(nj_vm *vm, const nj_u1 *data, nj_u4 len)
{
    nj_ref    bar, in, player;
    nj_value  a[2], ret;

    bar = nj_byte_array_new(vm, len);
    if (bar == NULL) {
        return NULL;
    }
    memcpy(bar->u.bytes, data, len);
    in = nj_object_new(vm, BAIS);
    if (in == NULL) {
        return NULL;
    }
    a[0].r = bar;
    if (nj_call_special(vm, in, BAIS, "<init>", "([B)V", a, NULL) != NJ_OK) {
        return NULL;
    }
    a[0].r = in;
    a[1].r = nj_string_new(vm, "video/3gpp");
    ret.bits = 0u;
    if (nj_call_static(vm, MEDIA_MGR, "createPlayer",
                       "(Ljava/io/InputStream;Ljava/lang/String;)L"
                       "javax/microedition/media/Player;", a, &ret) != NJ_OK) {
        return NULL;
    }
    return ret.r;
}

static nj_s4 field_int(nj_vm *vm, nj_ref obj, const char *name)
{
    nj_field *f = nj_field_lookup(vm, "javax/microedition/media/Player",
                                  name, "I", 0);
    return f != NULL ? nj_field_read_int(vm, obj, f) : -1;
}

static void write_ppm(const char *path, const nj_u1 *rgb, nj_u4 w, nj_u4 h)
{
    FILE *o = fopen(path, "wb");
    nj_u4 x, y;

    if (o == NULL) {
        return;
    }
    fprintf(o, "P6\n%u %u\n255\n", (unsigned)w, (unsigned)h);
    for (y = 0; y < h; y++) {
        for (x = 0; x < w; x++) {
            nj_u2 px = (nj_u2)(rgb[(y * w + x) * 2u] |
                               ((nj_u4)rgb[(y * w + x) * 2u + 1u] << 8));
            unsigned r = (px >> 11) & 31u, g = (px >> 5) & 63u, b = px & 31u;

            fputc((int)((r << 3) | (r >> 2)), o);
            fputc((int)((g << 2) | (g >> 4)), o);
            fputc((int)((b << 3) | (b >> 2)), o);
        }
    }
    fclose(o);
}

int main(int argc, char **argv)
{
    nj_port       port;
    nj_vm        *vm;
    FILE         *f;
    long          sz;
    unsigned char *data;
    nj_ref        player, vbuf;
    nj_s4         w, h, frames, i;
    int           want;
    int           bad = 0;

    if (argc < 5) {
        fprintf(stderr, "usage: %s clip.3gp outprefix frames heap_mb\n", argv[0]);
        return 2;
    }
    want = atoi(argv[3]);
    if (want <= 0) {
        want = 5;
    }
    f = fopen(argv[1], "rb");
    if (f == NULL) {
        fprintf(stderr, "cannot open %s\n", argv[1]);
        return 1;
    }
    fseek(f, 0, SEEK_END);
    sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    data = (unsigned char *)malloc((size_t)sz);
    if (data == NULL || fread(data, 1, (size_t)sz, f) != (size_t)sz) {
        fprintf(stderr, "cannot read %s\n", argv[1]);
        return 1;
    }
    fclose(f);

    memset(&port, 0, sizeof(port));
    if (nj_port_headless(&port) != NJ_OK) {
        fprintf(stderr, "headless port failed\n");
        return 1;
    }
    vm = nj_vm_create(&port, (nj_size)atoi(argv[4]) * 1024u * 1024u);
    if (vm == NULL) {
        fprintf(stderr, "VM failed\n");
        nj_port_shutdown(&port);
        return 1;
    }
    player = nj_root(vm, make_player(vm, data, (nj_u4)sz));
    if (player == NULL) {
        fprintf(stderr, "createPlayer failed\n");
        nj_clear_exception(vm);
        nj_vm_destroy(vm);
        nj_port_shutdown(&port);
        return 1;
    }
    (void)nj_call(vm, player, "javax/microedition/media/Player", "realize",
                  "()V", NULL, NULL);
    (void)nj_call(vm, player, "javax/microedition/media/Player", "prefetch",
                  "()V", NULL, NULL);
    (void)nj_call(vm, player, "javax/microedition/media/Player", "start",
                  "()V", NULL, NULL);
    w = field_int(vm, player, "pVw");
    h = field_int(vm, player, "pVh");
    frames = field_int(vm, player, "pFrames");
    printf("player %dx%d frames=%d\n", (int)w, (int)h, (int)frames);
    if (w <= 0 || h <= 0 || want > frames) {
        want = frames > 0 && frames < want ? (int)frames : want;
    }
    for (i = 0; i < want; i++) {
        char  path[1024];
        nj_s4 pushed;
        nj_ref v;
        nj_field *fld;

        nj_media_frame(vm);
        fld = nj_field_lookup(vm, "javax/microedition/media/Player",
                              "pVbuf", "[B", 0);
        v = fld != NULL ? nj_field_read_ref(vm, player, fld) : NULL;
        pushed = field_int(vm, player, "pPushed");
        if (v == NULL || pushed != i + 1) {
            fprintf(stderr, "frame %d not decoded (pushed=%d)\n", (int)i,
                    (int)pushed);
            bad++;
            break;
        }
        snprintf(path, sizeof(path), "%s-%03d.ppm", argv[2], (int)i);
        write_ppm(path, v->u.bytes, (nj_u4)w, (nj_u4)h);
        printf("rgb565 frame %d fnv 0x%08lx\n", (int)i,
               fnv1a(v->u.bytes, v->length));
    }
    (void)vbuf;
    nj_vm_destroy(vm);
    nj_port_shutdown(&port);
    free(data);
    return bad == 0 ? 0 : 1;
}
