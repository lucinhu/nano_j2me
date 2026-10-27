/*
 * Snapshot probe: save a VM that is playing a live MPEG-4 movie, then load the
 * blob into a fresh VM and drive the restored Player.  Run it from the
 * repository root; it writes /tmp/m4v-live-snapshot.njs and reads
 * tests/fixtures/audiodemo.jar to give the destination VM a suite to resolve
 * its class table through.
 *
 *   cc -O1 -g -std=c99 -fsanitize=address,undefined -fno-omit-frame-pointer \
 *      -I src/core -I src/midp -I src/gfx -I include \
 *      tools/snapshot_probe.c /tmp/nj-media-san/libnj2me.a \
 *      -lz -lm -lcurl -o /tmp/snapshot_probe
 *
 * The live Player is made reachable from a Java static (System.out, abused as
 * an object root) so that the restored copy can be found again after load.
 */
#include "nj2me/nj.h"
#include "nj2me/nj_app.h"
#include "nj2me/nj_vm.h"
#include "nj_internal.h"
#include "nj_midp.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BAIS "java/io/ByteArrayInputStream"
#define MEDIA_MGR "javax/microedition/media/Manager"
#define PLAYER "javax/microedition/media/Player"

static int checks, failures;

#define CHECK(cond, ...)                                                     \
    do {                                                                     \
        checks++;                                                            \
        if (!(cond)) {                                                       \
            failures++;                                                      \
            printf("  FAIL %s:%d  ", __FILE__, __LINE__);                    \
            printf(__VA_ARGS__);                                             \
            printf("\n");                                                    \
        }                                                                    \
    } while (0)

static nj_ref g_sysout_field(nj_vm *vm)
{
    return (nj_ref)(uintptr_t)nj_field_lookup(vm, "java/lang/System", "out",
                                              "Ljava/io/PrintStream;", 1);
}

static nj_ref load_file_array(nj_vm *vm, const char *path)
{
    FILE *f = fopen(path, "rb");
    long n;
    nj_ref a;

    if (!f) {
        return NULL;
    }
    if (fseek(f, 0L, SEEK_END) != 0 || (n = ftell(f)) <= 0 ||
        fseek(f, 0L, SEEK_SET) != 0) {
        fclose(f);
        return NULL;
    }
    a = nj_byte_array_new(vm, (nj_u4)n);
    if (a != NULL && fread(a->u.bytes, 1, (size_t)n, f) != (size_t)n) {
        a = NULL;
    }
    fclose(f);
    return a;
}

static nj_ref make_player(nj_vm *vm, const char *path)
{
    nj_ref bytes = load_file_array(vm, path);
    nj_ref in, type;
    nj_value a[2], ret;
    int rc;

    if (bytes == NULL) {
        return NULL;
    }
    in = nj_object_new(vm, BAIS);
    if (in == NULL) {
        return NULL;
    }
    memset(a, 0, sizeof(a));
    a[0].r = bytes;
    if (nj_call_special(vm, in, BAIS, "<init>", "([B)V", a, NULL) != NJ_OK) {
        return NULL;
    }
    type = nj_string_new(vm, "video/3gpp");
    memset(a, 0, sizeof(a));
    a[0].r = in;
    a[1].r = type;
    ret.bits = 0u;
    rc = nj_call_static(vm, MEDIA_MGR, "createPlayer",
                        "(Ljava/io/InputStream;Ljava/lang/String;)L"
                        "javax/microedition/media/Player;", a, &ret);
    return (rc == NJ_OK) ? ret.r : NULL;
}

static void call_void(nj_vm *vm, nj_ref p, const char *name)
{
    int rc = nj_call(vm, p, PLAYER, name, "()V", NULL, NULL);

    if (rc != NJ_OK || nj_has_exception(vm)) {
        printf("  threw in %s: %s\n", name,
               nj_exception_describe(vm) ? nj_exception_describe(vm) : "?");
        nj_clear_exception(vm);
    }
}

static int read_int(nj_vm *vm, nj_ref p, const char *name)
{
    nj_field *f = nj_field_lookup(vm, PLAYER, name, "I", 0);

    return f ? nj_field_read_int(vm, p, f) : -1;
}

static nj_ref read_ref(nj_vm *vm, nj_ref p, const char *name, const char *desc)
{
    nj_field *f = nj_field_lookup(vm, PLAYER, name, desc, 0);

    return f ? nj_field_read_ref(vm, p, f) : NULL;
}

int main(int argc, char **argv)
{
    const char *path = (argc > 1) ? argv[1] : "/tmp/m4v-samples/1942.3gp";
    const char *snap = "/tmp/m4v-live-snapshot.njs";
    nj_port port;
    nj_vm *vm = NULL, *vm2 = NULL;
    nj_app *vm2_app = NULL;
    nj_ref player = NULL, restored = NULL, f;
    nj_field *sysout;
    FILE *fp;
    int rc, i;

    memset(&port, 0, sizeof(port));
    if (nj_port_headless(&port) != 0) {
        printf("no headless port\n");
        return 1;
    }
    port.rms_dir = "/tmp";

    printf("== snapshot probe: %s ==\n", path);

    /* --- source VM: a movie Player in the middle of playback --- */
    vm = nj_vm_create(&port, 8 * 1024 * 1024);
    CHECK(vm != NULL, "source VM");
    if (vm == NULL) {
        nj_port_shutdown(&port);
        return 1;
    }
    player = make_player(vm, path);
    CHECK(player != NULL, "a Player for the movie");
    if (player == NULL) {
        nj_vm_destroy(vm);
        nj_port_shutdown(&port);
        return 1;
    }
    call_void(vm, player, "realize");
    call_void(vm, player, "prefetch");
    call_void(vm, player, "start");
    for (i = 0; i < 3; i++) {
        nj_media_frame(vm);
    }
    CHECK(read_int(vm, player, "pPushed") == 3,
          "three samples pumped before the save (%d)",
          read_int(vm, player, "pPushed"));
    CHECK(read_int(vm, player, "pState") == 400, "the Player is STARTED");
    CHECK(vm->media_players != NULL, "the live Player is on the pump registry");

    /* Reachable from a static so the restored copy can be located again. */
    sysout = nj_field_lookup(vm, "java/lang/System", "out",
                             "Ljava/io/PrintStream;", 1);
    CHECK(sysout != NULL, "java/lang/System.out exists");
    if (sysout != NULL) {
        nj_field_write_ref(vm, NULL, sysout, player);
    }

    fp = fopen(snap, "wb");
    CHECK(fp != NULL, "snapshot file");
    if (fp == NULL) {
        nj_vm_destroy(vm);
        nj_port_shutdown(&port);
        return 1;
    }
    rc = nj_vm_save(vm, fp);
    fclose(fp);
    CHECK(rc == NJ_OK, "nj_vm_save with a live movie decoder (%d)", rc);
    CHECK(vm->media_players != NULL && read_int(vm, player, "pState") == 400,
          "save left the source Player playing");

    /* The source keeps decoding after the save: the save must not have
     * closed or borrowed the source decoder. */
    for (i = 0; i < 2; i++) {
        nj_media_frame(vm);
    }
    CHECK(read_int(vm, player, "pPushed") == 5,
          "the source keeps decoding after the save (%d)",
          read_int(vm, player, "pPushed"));

    /* --- destination VM: load the blob and drive the restored Player --- */
    vm2 = nj_vm_create(&port, 8 * 1024 * 1024);
    CHECK(vm2 != NULL, "destination VM");
    if (vm2 != NULL) {
        /*
         * nj_vm_load() resolves the saved class table through the suite the
         * caller attached, so the destination needs any open JAR first (the
         * restored world carries its own app pointer from the blob).
         */
        nj_ref jar = load_file_array(vm2, "tests/fixtures/audiodemo.jar");

        if (jar != NULL) {
            vm2_app = nj_app_open(vm2, jar->u.bytes, jar->length);
        }
        CHECK(vm2_app != NULL, "a suite is attached to the destination VM");
        fp = fopen(snap, "rb");
        CHECK(fp != NULL, "snapshot file readable");
        if (fp != NULL) {
            rc = nj_vm_load(vm2, fp);
            fclose(fp);
            CHECK(rc == NJ_OK, "nj_vm_load of the movie snapshot (%d)", rc);
        }
        CHECK(vm2->media_players == NULL,
              "the restored pump registry is empty (no native handles)");
        sysout = nj_field_lookup(vm2, "java/lang/System", "out",
                                 "Ljava/io/PrintStream;", 1);
        CHECK(sysout != NULL, "the destination knows System.out");
        restored = (sysout != NULL) ? nj_field_read_ref(vm2, NULL, sysout)
                                    : NULL;
        if (restored != NULL) {
            CHECK(read_int(vm2, restored, "pKind") == 1,
                  "a restored movie Player drops to a silent audio Player (%d)",
                  read_int(vm2, restored, "pKind"));
            CHECK(read_int(vm2, restored, "pState") == 300,
                  "a restored STARTED Player is back in PREFETCHED");
            CHECK(read_ref(vm2, restored, "pData", "[B") == NULL,
                  "a restored movie Player forgets the decoder holder");
            /* The holdover frame buffer is plain arena bytes, so it survives
             * and getSnapshot() keeps working - no native pointer involved. */
            f = read_ref(vm2, restored, "pVbuf", "[B");
            CHECK(f != NULL, "the restored Player keeps its decoded frame");
            /* MIDP lifecycle on the restored object: realize/start/stop must
             * not touch a decoder that no longer exists. */
            call_void(vm2, restored, "realize");
            call_void(vm2, restored, "prefetch");
            call_void(vm2, restored, "start");
            CHECK(!nj_has_exception(vm2),
                  "a restored movie Player can start silently");
            nj_media_frame(vm2);
            call_void(vm2, restored, "stop");
            call_void(vm2, restored, "deallocate");
            call_void(vm2, restored, "close");
            CHECK(!nj_has_exception(vm2),
                  "a restored movie Player closes without stale handles");
        }
        CHECK(nj_gc_run(vm2) == NJ_OK, "GC over the restored movie snapshot");
    }

    /* Source teardown releases the live decoder (F2 path). */
    call_void(vm, player, "stop");
    call_void(vm, player, "deallocate");
    call_void(vm, player, "close");
    if (vm2_app != NULL) {
        nj_app_close(vm2, vm2_app);
    }
    nj_vm_destroy(vm2);
    nj_vm_destroy(vm);
    nj_port_shutdown(&port);

    printf("== %d checks, %d failure(s) ==\n", checks, failures);
    return failures ? 1 : 0;
}
