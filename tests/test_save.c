/*
 * test_save.c - save state / restore state.
 *
 * What this exercises:
 *
 *   1. a real MIDlet (tests/fixtures/fielddemo.jar) is started in one VM and
 *      driven a few frames, so there is live MIDlet state to save: the static
 *      java.util.Vector FieldChild.b with its two Strings;
 *   2. that VM is saved to a file and loaded into a SECOND, freshly created
 *      VM in the same process with the same JAR attached - the MIDlet's
 *      observable state (the static field, the Vector, its elements, the
 *      Displayable) is still there afterwards;
 *   3. the restored VM can allocate and run more frames (and its display
 *      comes up through the port the way it did the first time);
 *   4. a blob with a damaged CRC is refused, and so is a blob whose classes
 *      do not match the attached JAR;
 *   5. reference identity survives: the String inside the restored Vector is
 *      still the *same object* as the interned literal, which is only true if
 *      every slot pointing at it was rebased.
 *
 * It is white-box on purpose - it reads the static slot and the Vector's
 * elementData directly, so a failure points at the slot that went wrong
 * instead of at a MIDlet that printed the wrong number.
 */
#include "nj2me/nj.h"
#include "nj_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int checks;
static int failures;

/*
 * The thread/frame VM A was in when the snapshot was taken, so the checks
 * after the load can compare the restored one with it (stack-image test).
 */
static nj_u2 saved_thread_idx;
static int   saved_nframes = -1;
static nj_u4 saved_sp_off;
static nj_u4 fake_now;

#define CHECK(cond, what)                                                   \
    do {                                                                    \
        checks++;                                                           \
        if (!(cond)) {                                                      \
            failures++;                                                     \
            printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, (what));       \
        }                                                                   \
    } while (0)

#define HEAP_BYTES  (2u * 1024u * 1024u)
#define JAR_PATH    "tests/fixtures/fielddemo.jar"
#define OTHER_JAR   "tests/fixtures/hello.jar"
#define STATE_PATH  "/tmp/nj2me_test_state.bin"
#define BAD_PATH    "/tmp/nj2me_test_state_bad_crc.bin"
#define GEN2_PATH   "/tmp/nj2me_test_state_gen2.bin"
#define UNLINKED_PATH "/tmp/nj2me_test_state_unlinked.bin"
#define DISPLAY_GRACE 90

static nj_u4 fake_millis(void *ud)
{
    (void)ud;
    return fake_now;
}

/* ------------------------------------------------------------------ */
/* Host helpers                                                        */
/* ------------------------------------------------------------------ */
static nj_u1 *slurp(const char *path, nj_u4 *out_len)
{
    FILE *f = fopen(path, "rb");
    long n;
    nj_u1 *p;

    if (!f) {
        return NULL;
    }
    if (fseek(f, 0L, SEEK_END) != 0 || (n = ftell(f)) <= 0 ||
        fseek(f, 0L, SEEK_SET) != 0) {
        fclose(f);
        return NULL;
    }
    p = (nj_u1 *)malloc((size_t)n);
    if (!p || fread(p, 1, (size_t)n, f) != (size_t)n) {
        free(p);
        fclose(f);
        return NULL;
    }
    fclose(f);
    *out_len = (nj_u4)n;
    return p;
}

/* Copy `src` to `dst`, flipping one byte well inside the body. */
static int corrupt_one_byte(const char *src, const char *dst)
{
    FILE *in = fopen(src, "rb");
    FILE *out;
    long n;
    nj_u1 *p;
    long at;

    if (!in) {
        return 0;
    }
    if (fseek(in, 0L, SEEK_END) != 0 || (n = ftell(in)) <= 0 ||
        fseek(in, 0L, SEEK_SET) != 0) {
        fclose(in);
        return 0;
    }
    p = (nj_u1 *)malloc((size_t)n);
    if (!p || fread(p, 1, (size_t)n, in) != (size_t)n) {
        free(p);
        fclose(in);
        return 0;
    }
    fclose(in);
    at = n / 2;                 /* past the header, inside the payload */
    p[at] ^= 0x5Au;
    out = fopen(dst, "wb");
    if (!out) {
        free(p);
        return 0;
    }
    if (fwrite(p, 1, (size_t)n, out) != (size_t)n) {
        fclose(out);
        free(p);
        return 0;
    }
    fclose(out);
    free(p);
    return 1;
}

/* ------------------------------------------------------------------ */
/* Java state readers (white-box)                                      */
/* ------------------------------------------------------------------ */
static nj_field *find_field(nj_vm *vm, const char *cls, const char *name,
                            const char *desc, int is_static)
{
    nj_class *c = nj_class_resolve(vm, cls);

    if (!c) {
        return NULL;
    }
    return nj_field_lookup_in(c, name, desc, is_static);
}

static nj_ref static_ref(nj_vm *vm, const char *cls, const char *name,
                         const char *desc)
{
    nj_field *f = find_field(vm, cls, name, desc, 1);

    if (!f) {
        return NULL;
    }
    return nj_field_read_ref_fast(vm, NULL, f);
}

static nj_ref obj_ref_field(nj_vm *vm, nj_ref obj, const char *cls,
                            const char *name, const char *desc)
{
    nj_field *f = find_field(vm, cls, name, desc, 0);

    if (!f || !obj) {
        return NULL;
    }
    return nj_field_read_ref_fast(vm, obj, f);
}

static int obj_int_field(nj_vm *vm, nj_ref obj, const char *cls,
                         const char *name, const char *desc)
{
    nj_field *f = find_field(vm, cls, name, desc, 0);

    if (!f || !obj) {
        return -1;
    }
    return (int)nj_field_read_int_fast(vm, obj, f);
}

static nj_ref ref_array_at(nj_ref arr, nj_u4 i)
{
    if (!arr || arr->kind != NJ_OBJ_ARRAY || arr->aflags != NJ_ARRAY_T_REF ||
        !arr->u.refs || i >= arr->length) {
        return NULL;
    }
    return arr->u.refs[i];
}

static int str_eq(nj_ref s, const char *want)
{
    nj_u2 *ch;
    nj_u4 n, i, wl;

    if (!s || s->kind != NJ_OBJ_STRING) {
        return 0;
    }
    ch = nj_string_chars(s);
    n = nj_string_len(s);
    wl = (nj_u4)strlen(want);
    if (!ch || n != wl) {
        return 0;
    }
    for (i = 0u; i < n; i++) {
        if (ch[i] != (nj_u2)(unsigned char)want[i]) {
            return 0;
        }
    }
    return 1;
}

/* The MIDlet's state: FieldChild.b, a static Vector built by startApp(). */
static nj_ref field_child_b(nj_vm *vm)
{
    return static_ref(vm, "FieldChild", "b", "Ljava/util/Vector;");
}

static int vector_size(nj_vm *vm, nj_ref v)
{
    return obj_int_field(vm, v, "java/util/Vector", "elementCount", "I");
}

static nj_ref vector_at(nj_vm *vm, nj_ref v, nj_u4 i)
{
    nj_ref data = obj_ref_field(vm, v, "java/util/Vector", "elementData",
                                "[Ljava/lang/Object;");

    return ref_array_at(data, i);
}

/* Is `r` also reachable from the interned string table? */
static int interned_holds(nj_vm *vm, nj_ref r)
{
    nj_u2 i;

    for (i = 0u; i < vm->ninterned; i++) {
        if (vm->interned[i] == r) {
            return 1;
        }
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* MIDlet harness                                                      */
/* ------------------------------------------------------------------ */
/* Start the fixture MIDlet and drive frames until it has a Displayable. */
static int start_fixture(nj_vm *vm, const nj_u1 *jar, nj_u4 jar_len,
                         nj_app **out_app)
{
    nj_ref exc = NULL;
    nj_ref midlet;
    int waited = 0;

    *out_app = nj_app_open(vm, jar, jar_len);
    if (!*out_app) {
        return 0;
    }
    midlet = nj_vm_new_midlet(vm, "FieldDemo", &exc);
    if (!midlet) {
        printf("  note: cannot instantiate FieldDemo: %s\n",
               nj_exception_describe(vm));
        return 0;
    }
    (void)nj_vm_start_app(vm, midlet);
    while (!nj_midp_current(vm) && waited < DISPLAY_GRACE) {
        (void)nj_midp_frame(vm);
        waited++;
    }
    return nj_midp_current(vm) != NULL;
}

/* A desktop caller can select a heap larger than the embedded default.
 * Its snapshot must round-trip, without expanding a smaller caller's budget. */
static void test_large_heap(const nj_port *port, const nj_u1 *jar, nj_u4 jar_len)
{
    const nj_size capacity = (nj_size)NJ_MAX_HEAP + 48u * 1024u * 1024u;
    nj_vm *source = nj_vm_create(port, capacity);
    nj_vm *target = NULL;
    nj_app *source_app = NULL, *target_app = NULL;
    FILE *state = tmpfile();
    CHECK(source != NULL && state != NULL, "large-heap snapshot setup");
    if (!source || !state) { goto done; }
    CHECK(start_fixture(source, jar, jar_len, &source_app),
          "large-heap fixture starts");
    CHECK(nj_vm_save(source, state) == NJ_OK,
          "save an explicitly configured large heap");
    target = nj_vm_create(port, HEAP_BYTES);
    CHECK(target != NULL, "smaller restore VM exists");
    if (target) {
        target_app = nj_app_open(target, jar, jar_len);
        CHECK(target_app != NULL, "smaller restore VM attaches fixture");
        rewind(state);
        CHECK(nj_vm_load(target, state) == NJ_ERR_FORMAT,
              "a snapshot cannot enlarge the caller's heap budget");
        CHECK(target->heap.size == HEAP_BYTES,
              "refused large restore preserves the smaller heap");
        if (target_app) { nj_app_close(target, target_app); }
        nj_vm_destroy(target);
        target = NULL;
        target_app = NULL;
    }
    target = nj_vm_create(port, capacity);
    CHECK(target != NULL, "large restore VM exists");
    if (target) {
        target_app = nj_app_open(target, jar, jar_len);
        CHECK(target_app != NULL, "large restore VM attaches fixture");
        rewind(state);
        CHECK(nj_vm_load(target, state) == NJ_OK,
              "restore an explicitly configured large heap");
        CHECK(target->heap.size == capacity,
              "large restored heap retains its capacity");
        CHECK(str_eq(vector_at(target, field_child_b(target), 0u), "first"),
              "large snapshot restores observable Java state");
    }
done:
    if (target_app) { nj_app_close(target, target_app); }
    nj_vm_destroy(target);
    if (source_app) { nj_app_close(source, source_app); }
    nj_vm_destroy(source);
    if (state) { fclose(state); }
}

/* Native resources cannot cross an arena snapshot, even for a Player which
 * has not started and is absent from media_players.  Deliberately invalid
 * pointer bytes make an accidental close deterministic instead of depending
 * on a previous process's allocator layout. */
#define TEST_PLAYER "javax/microedition/media/Player"
static int restored_media_host_calls;
static void restored_media_handle(void *ud, void *handle)
{
    (void)ud;
    (void)handle;
    restored_media_host_calls++;
}
static void restored_media_pcm(void *ud)
{
    (void)ud;
    restored_media_host_calls++;
}
static void media_test_int(nj_vm *vm, nj_ref p, const char *name,
                           const char *desc, int value)
{
    nj_field *f = find_field(vm, TEST_PLAYER, name, desc, 0);
    CHECK(f != NULL, "Player test field exists");
    if (f) { nj_field_write_int_fast(vm, p, f, value); }
}
static void media_test_ref(nj_vm *vm, nj_ref p, const char *name,
                           const char *desc, nj_ref value)
{
    nj_field *f = find_field(vm, TEST_PLAYER, name, desc, 0);
    CHECK(f != NULL, "Player reference test field exists");
    if (f) { nj_field_write_ref(vm, p, f, value); }
}
static void test_restored_media(nj_port *base, const nj_u1 *jar, nj_u4 jar_len)
{
    nj_port port = *base;
    nj_vm *source = NULL, *target = NULL;
    nj_app *source_app = NULL, *target_app = NULL;
    nj_ref players[4] = {NULL, NULL, NULL, NULL};
    nj_ref handles[4] = {NULL, NULL, NULL, NULL};
    nj_ref roots = NULL, vector, tones, sequence;
    FILE *state = tmpfile();
    int i;
    void *poison = (void *)(uintptr_t)1u;

    port.midi_stop = restored_media_handle;
    port.midi_close = restored_media_handle;
    port.pcm_close = restored_media_pcm;
    /* No new device is available in this test; any close targets the stale
     * pre-snapshot ownership, not a legitimately reopened output device. */
    port.pcm_open = NULL;
    source = nj_vm_create(&port, HEAP_BYTES);
    CHECK(source && state, "media snapshot setup");
    if (!source || !state) { goto done; }
    CHECK(start_fixture(source, jar, jar_len, &source_app),
          "media snapshot fixture starts");
    source->gc_disabled = 1u;
    vector = field_child_b(source);
    roots = nj_array_new(source, "Ljava/lang/Object;", 4u);
    CHECK(vector && roots, "media snapshot root vector exists");
    if (!vector || !roots) { goto done; }
    nj_field_write_ref(source, vector,
        find_field(source, "java/util/Vector", "elementData",
                   "[Ljava/lang/Object;", 0), roots);
    nj_field_write_int_fast(source, vector,
        find_field(source, "java/util/Vector", "elementCount", "I", 0), 4);
    for (i = 0; i < 4; i++) {
        players[i] = nj_object_new(source, TEST_PLAYER);
        handles[i] = nj_array_new(source, "B", i == 0 ? 16384u + sizeof(void *) : 16u);
        CHECK(players[i] && handles[i], "media snapshot Player allocated");
        if (!players[i] || !handles[i]) { goto done; }
        roots->u.refs[i] = players[i];
        memcpy(handles[i]->u.bytes, &poison, sizeof(poison));
        media_test_int(source, players[i], "pState", "I", i == 0 ? 300 : 400);
        media_test_int(source, players[i], "pKind", "I", i == 3 ? 0 : 1);
        media_test_int(source, players[i], "pFrames", "I", 8);
        media_test_int(source, players[i], "pOpen", "Z", 1);
        media_test_int(source, players[i], "pLoopCount", "I", 1);
        if (i < 3) {
            media_test_ref(source, players[i], "pData", "[B", handles[i]);
        }
    }
    media_test_int(source, players[0], "pStream", "Z", 1);
    media_test_int(source, players[1], "pPlat", "Z", 1);
    media_test_ref(source, players[1], "pMidi", "[B", handles[1]);
    tones = nj_array_new(source, "I", 2u);
    sequence = nj_array_new(source, "B", 2u);
    CHECK(tones && sequence, "heap-only tone resources exist");
    media_test_ref(source, players[3], "pSeq", "[B", sequence);
    media_test_ref(source, players[3], "pTones", "[I", tones);
    media_test_int(source, players[3], "pNTones", "I", 1);
    /* No Player is registered, including the unstarted MIDI stream. */
    CHECK(source->media_players == NULL, "Players are outside pump registry");
    source->gc_disabled = 0u;
    restored_media_host_calls = 0;
    CHECK(nj_vm_save(source, state) == NJ_OK, "save Player native handle snapshot");
    CHECK(restored_media_host_calls == 0, "save does not stop source audio");
    CHECK(obj_ref_field(source, players[0], TEST_PLAYER, "pData", "[B") == handles[0]
          && obj_int_field(source, players[0], TEST_PLAYER, "pStream", "Z") == 1,
          "save preserves source stream ownership");
    CHECK(obj_ref_field(source, players[1], TEST_PLAYER, "pMidi", "[B") == handles[1]
          && obj_int_field(source, players[1], TEST_PLAYER, "pPlat", "Z") == 1
          && obj_int_field(source, players[2], TEST_PLAYER, "pOpen", "Z") == 1,
          "save preserves source platform and PCM ownership");
    target = nj_vm_create(&port, HEAP_BYTES);
    CHECK(target != NULL, "media restore VM exists");
    if (!target) { goto done; }
    target_app = nj_app_open(target, jar, jar_len);
    CHECK(target_app != NULL, "media restore attaches fixture");
    rewind(state);
    CHECK(nj_vm_load(target, state) == NJ_OK, "load Player native handle snapshot");
    CHECK(restored_media_host_calls == 0, "load never closes saved host pointers");
    CHECK(target->media_players == NULL, "restored audio pump starts empty");
    for (i = 0; i < 4; i++) {
        nj_ref p = vector_at(target, field_child_b(target), (nj_u4)i);
        CHECK(p && obj_int_field(target, p, TEST_PLAYER, "pOpen", "Z") == 0
              && obj_int_field(target, p, TEST_PLAYER, "pStream", "Z") == 0
              && obj_int_field(target, p, TEST_PLAYER, "pPlat", "Z") == 0,
              "all restored Players forget external ownership");
        if (!p) { continue; }
        if (i < 2) {
            CHECK(obj_ref_field(target, p, TEST_PLAYER, "pData", "[B") == NULL
                  && obj_ref_field(target, p, TEST_PLAYER, "pMidi", "[B") == NULL,
                  "restored native songs forget pointer containers");
            CHECK(nj_call(target, p, TEST_PLAYER, "start", "()V", NULL, NULL) == NJ_OK,
                  "restored native song can start silently");
            CHECK(nj_call(target, p, TEST_PLAYER, "stop", "()V", NULL, NULL) == NJ_OK,
                  "restored native song can stop safely");
        } else if (i == 2) {
            nj_ref pcm = obj_ref_field(target, p, TEST_PLAYER, "pData", "[B");
            CHECK(pcm && pcm->length == 16u
                  && memcmp(pcm->u.bytes, &poison, sizeof(poison)) == 0,
                  "restore preserves ordinary PCM bytes");
        } else {
            CHECK(obj_ref_field(target, p, TEST_PLAYER, "pSeq", "[B") != NULL
                  && obj_ref_field(target, p, TEST_PLAYER, "pTones", "[I") != NULL
                  && obj_int_field(target, p, TEST_PLAYER, "pNTones", "I") == 1,
                  "restore preserves heap-only tone sequence");
        }
        CHECK(nj_call(target, p, TEST_PLAYER, "deallocate", "()V", NULL, NULL) == NJ_OK,
              "restored Player deallocates without stale handle");
        CHECK(nj_call(target, p, TEST_PLAYER, "close", "()V", NULL, NULL) == NJ_OK,
              "restored Player closes without stale handle");
    }
    CHECK(restored_media_host_calls == 0, "restored lifecycle never uses old host resources");
    CHECK(nj_gc_run(target) == NJ_OK, "GC after restored Player teardown succeeds");
 done:
    /* These source fixtures intentionally contain invalid pointers.  Forget
     * them only AFTER checking save left the source unchanged. */
    if (source) {
        for (i = 0; i < 4; i++) {
            if (players[i]) { nj_media_restore_object(source, players[i]); }
        }
        source->media_players = NULL;
    }
    if (target_app) { nj_app_close(target, target_app); }
    nj_vm_destroy(target);
    if (source_app) { nj_app_close(source, source_app); }
    nj_vm_destroy(source);
    if (state) { fclose(state); }
}

static int restore_rtc_year;
static int restore_rtc(void *ud, nj_datetime *out)
{
    (void)ud;
    memset(out, 0, sizeof(*out));
    out->year = restore_rtc_year;
    out->month = 9;
    out->day = 20;
    out->hour = 11;
    out->tz_offset = 480;
    return 1;
}

/* A restored MIDlet keeps its virtual epoch even when the new port uses
 * another raw-clock origin, RTC instant, or no RTC at all. */
static void test_clock_restore(nj_port *port, const nj_u1 *jar, nj_u4 jar_len)
{
    static const nj_u4 origins[] = { 0u, 500u, 50000000u };
    static const int speeds[] = { 100, 200 };
    unsigned oi, si, mode;
    int (*previous_now)(void *, nj_datetime *) = port->now;
    for (mode = 0; mode < 4; mode++) {
        for (oi = 0; oi < sizeof(origins) / sizeof(origins[0]); oi++) {
            for (si = 0; si < 2u; si++) {
                nj_vm *source = NULL, *target = NULL;
                nj_app *source_app = NULL, *target_app = NULL;
                nj_thread *sleeper, *timed, *untimed, *expired;
                nj_u2 sleep_idx, timed_idx, untimed_idx, expired_idx, i;
                nj_s8 saved, source_virtual, source_real;
                nj_value result;
                FILE *state = tmpfile();
                fake_now = 1000000u;
                restore_rtc_year = 2026;
                port->now = (mode & 1u) ? restore_rtc : NULL;
                source = nj_vm_create(port, HEAP_BYTES);
                CHECK(state && source, "clock restore source and stream exist");
                if (!state || !source) { goto done; }
                CHECK(start_fixture(source, jar, jar_len, &source_app),
                      "clock source fixture starts");
                nj_vm_set_speed(source, speeds[si]);
                fake_now += 50u;
                saved = nj_now_ms(source);
                sleep_idx = source->nthreads;
                sleeper = nj_thread_create(source, NULL, 0);
                timed_idx = source->nthreads;
                timed = nj_thread_create(source, NULL, 0);
                untimed_idx = source->nthreads;
                untimed = nj_thread_create(source, NULL, 0);
                expired_idx = source->nthreads;
                expired = nj_thread_create(source, NULL, 0);
                CHECK(sleeper && timed && untimed && expired, "clock test threads exist");
                if (!sleeper || !timed || !untimed || !expired) { goto done; }
                CHECK(nj_thread_sleep(source, sleeper, 200) == NJ_ERR_TIMEOUT,
                      "clock source parks sleeper");
                timed->state = expired->state = untimed->state = NJ_THREAD_WAITING;
                timed->wait_deadline = saved + 200;
                expired->wait_deadline = saved - 1;
                untimed->wait_deadline = 0;
                source->halt_at_ms = saved + 5000;
                CHECK(nj_vm_save(source, state) == NJ_OK, "clock snapshot saves");
                CHECK(nj_now_ms(source) == saved && sleeper->wake_at_ms == saved + 200
                      && timed->wait_deadline == saved + 200,
                      "saving leaves source time and deadlines unchanged");

                source_virtual = source->speed_virtual;
                source_real = source->speed_real;
                fake_now = origins[oi];
                restore_rtc_year = 2030;
                port->now = (mode & 2u) ? restore_rtc : NULL;
                target = nj_vm_create(port, HEAP_BYTES);
                CHECK(target != NULL, "clock restore target exists");
                if (!target) { goto done; }
                target_app = nj_app_open(target, jar, jar_len);
                CHECK(target_app != NULL, "clock target attaches fixture");
                rewind(state);
                {
                    nj_status loaded = nj_vm_load(target, state);
                    CHECK(loaded == NJ_OK, "clock snapshot loads");
                    if (loaded != NJ_OK) { goto done; }
                }
                CHECK(nj_now_ms(target) == saved, "restore preserves MIDlet virtual time");
                CHECK(nj_call(target, NULL, "java/lang/System", "currentTimeMillis", "()J",
                              NULL, &result) == NJ_OK && result.j == saved,
                      "Java currentTimeMillis continues at saved virtual time");
                CHECK(target->speed_percent == speeds[si]
                      && target->halt_at_ms == saved + 5000,
                      "restore preserves speed and virtual halt deadline");
                sleeper = target->threads[sleep_idx];
                timed = target->threads[timed_idx];
                untimed = target->threads[untimed_idx];
                expired = target->threads[expired_idx];
                CHECK(sleeper->wake_at_ms - nj_now_ms(target) == 200
                      && timed->wait_deadline - nj_now_ms(target) == 200,
                      "sleep and timed wait retain remaining delay across epochs");
                CHECK(expired->wait_deadline == saved - 1 && untimed->wait_deadline == 0,
                      "expired timed wait stays distinct from untimed sentinel");
                /* Freeze bytecode execution, but exercise real scheduler wakeups. */
                for (i = 0; i < target->nthreads; i++) { target->threads[i]->in_c_call = 1; }
                (void)nj_scheduler_step(target);
                CHECK(expired->state == NJ_THREAD_RUNNING && expired->wait_deadline == 0,
                      "already expired timed wait wakes immediately");
                CHECK(sleeper->state == NJ_THREAD_SLEEPING && timed->state == NJ_THREAD_WAITING
                      && untimed->state == NJ_THREAD_WAITING,
                      "future and untimed waits remain parked on first scheduler step");
                fake_now += (nj_u4)(10000 / speeds[si]);
                CHECK(nj_now_ms(target) == saved + 100,
                      "restored speed scales elapsed raw time");
                (void)nj_scheduler_step(target);
                CHECK(sleeper->state == NJ_THREAD_SLEEPING && timed->state == NJ_THREAD_WAITING,
                      "sleep and timed wait remain parked before their deadline");
                fake_now += (nj_u4)(10000 / speeds[si]);
                (void)nj_scheduler_step(target);
                CHECK(sleeper->state == NJ_THREAD_RUNNING && timed->state == NJ_THREAD_RUNNING,
                      "sleep and timed wait wake at preserved deadline");
                CHECK(untimed->state == NJ_THREAD_WAITING && untimed->wait_deadline == 0,
                      "untimed wait remains indefinite after clock migration");
                CHECK(source->threads[sleep_idx]->wake_at_ms == saved + 200
                      && source->threads[sleep_idx]->state == NJ_THREAD_SLEEPING
                      && source->speed_virtual == source_virtual && source->speed_real == source_real
                      && source->speed_percent == speeds[si],
                      "restoring another VM leaves source state unchanged");
            done:
                if (target_app) { nj_app_close(target, target_app); }
                nj_vm_destroy(target);
                if (source_app) { nj_app_close(source, source_app); }
                nj_vm_destroy(source);
                if (state) { fclose(state); }
            }
        }
    }
    port->now = previous_now;
}

/* ------------------------------------------------------------------ */
/* main                                                                */
/* ------------------------------------------------------------------ */
int main(void)
{
    nj_port port;
    nj_vm *vm_a = NULL, *vm_b = NULL, *vm_c = NULL, *vm_d = NULL, *vm_e = NULL;
    nj_app *app_a = NULL, *app_b = NULL;
    nj_u1 *jar = NULL, *other = NULL;
    nj_u4 jar_len = 0u, other_len = 0u;
    nj_ref b_a = NULL, b_b = NULL, b_e = NULL;
    nj_ref s_a0, s_b0;
    nj_u4 frames_before;
    void *blk;
    nj_ref fresh;
    int i;

    printf("== nano_j2me save state / restore state ==\n");

    if (nj_port_headless(&port) != NJ_OK) {
        printf("FAIL headless port did not come up\n");
        return 1;
    }
    port.millis = fake_millis;
    fake_now = 1000u;

    jar = slurp(JAR_PATH, &jar_len);
    other = slurp(OTHER_JAR, &other_len);
    if (!jar || !other) {
        printf("FAIL cannot read fixtures (%s / %s)\n", JAR_PATH, OTHER_JAR);
        return 1;
    }

    /* ---- VM A: run the MIDlet mid-flight ---------------------------- */
    vm_a = nj_vm_create(&port, HEAP_BYTES);
    CHECK(vm_a != NULL, "nj_vm_create A");
    if (!vm_a) {
        return 1;
    }
    CHECK(start_fixture(vm_a, jar, jar_len, &app_a),
          "the fixture MIDlet started and installed a Displayable");
    fake_now += 250u;
    for (i = 0; i < 5; i++) {
        (void)nj_midp_frame(vm_a);
    }
    CHECK(!nj_has_exception(vm_a), "no pending exception after the frames");
    CHECK(nj_midp_current(vm_a) != NULL, "VM A is still showing a Displayable");

    /* The MIDlet's observable state, written by FieldDemo.startApp(). */
    b_a = field_child_b(vm_a);
    CHECK(b_a != NULL, "FieldChild.b is set after startApp()");
    CHECK(vector_size(vm_a, b_a) == 2, "FieldChild.b holds 2 elements");
    s_a0 = vector_at(vm_a, b_a, 0u);
    CHECK(str_eq(s_a0, "first"), "FieldChild.b[0] is \"first\"");
    CHECK(str_eq(vector_at(vm_a, b_a, 1u), "second"),
          "FieldChild.b[1] is \"second\"");
    CHECK(interned_holds(vm_a, s_a0),
          "the String in the Vector is the interned literal (identity)");

    /* ---- refusals --------------------------------------------------- */
    CHECK(nj_vm_can_save(NULL) == 0, "nj_vm_can_save(NULL) refuses");
    CHECK(nj_vm_can_save(vm_a) == 1, "nj_vm_can_save() accepts a live VM");
    vm_a->native_depth = 1u;
    CHECK(nj_vm_can_save(vm_a) == 0, "refuses while native_depth != 0");
    vm_a->native_depth = 0u;
    vm_a->in_paint = 1;
    CHECK(nj_vm_can_save(vm_a) == 0, "refuses while painting");
    vm_a->in_paint = 0;
    if (vm_a->nthreads > 0u) {
        vm_a->threads[0]->in_c_call = 1;
        CHECK(nj_vm_can_save(vm_a) == 0, "refuses while a thread is in a C call");
        vm_a->threads[0]->in_c_call = 0;
    }
    CHECK(nj_vm_can_save(vm_a) == 1, "accepts again once the flags are clear");

    /*
     * Regression: th->top is only the frame-allocation frontier, not a
     * high-water mark.  nj_frame_pop() leaves it at the locals of the frame
     * that just returned and the caller then pushes operands ABOVE it, so a
     * live operand stack can sit past th->top.  A snapshot that imaged only
     * th->top slots dropped those operands, and the next save of the restored
     * VM refused to load at all because a frame's sp pointed past its image
     * (a real game hit exactly that).  Leave this VM in that state.
     */
    {
        nj_u2 ti;
        for (ti = 0u; ti < vm_a->nthreads; ti++) {
            nj_thread *t = vm_a->threads[ti];

            if (t && t->nframes > 0) {
                nj_frame *fr = &t->frames[t->nframes - 1];

                saved_thread_idx = ti;
                saved_nframes = t->nframes;
                saved_sp_off = (nj_u4)(fr->sp - t->stack_mem);
                if (fr->locals > t->stack_mem && fr->locals < fr->sp) {
                    t->top = fr->locals;   /* what a returned callee leaves */
                }
                CHECK(t->top <= fr->sp, "the thread is in the stale-top state");
                break;
            }
        }
    }

    /* ---- save ------------------------------------------------------- */
    CHECK(nj_vm_save_file(vm_a, STATE_PATH) == NJ_OK, "nj_vm_save_file()");
    CHECK(nj_vm_save_file(vm_a, "/nonexistent-dir/nope.bin") != NJ_OK,
          "saving into a missing directory fails cleanly");

    /* ---- VM B: a fresh VM, same JAR, load --------------------------- */
    vm_b = nj_vm_create(&port, HEAP_BYTES);
    CHECK(vm_b != NULL, "nj_vm_create B");
    app_b = nj_app_open(vm_b, jar, jar_len);
    CHECK(app_b != NULL, "the same JAR attaches to the fresh VM");
    {
        int loaded = (nj_vm_load_file(vm_b, STATE_PATH) == NJ_OK);

        CHECK(loaded, "nj_vm_load_file()");
        if (!loaded) {
            /*
             * A refused load leaves the VM unusable on purpose (nj_vm_load()
             * installs everything or nothing), so every check below would be
             * reading a half-built state - or a NULL arena.  Report and move on
             * to the checks that do not need a restored VM.
             */
            goto after_vm_b;
        }
    }

    /* (1) the MIDlet's observable state continued */
    CHECK(nj_midp_current(vm_b) != NULL,
          "the restored VM still has its current Displayable");
    CHECK(vm_b->app != NULL, "the restored VM still has its MIDlet");
    b_b = field_child_b(vm_b);
    CHECK(b_b != NULL, "FieldChild.b survived the restore");
    CHECK(vector_size(vm_b, b_b) == 2, "the restored Vector still holds 2");
    s_b0 = vector_at(vm_b, b_b, 0u);
    CHECK(str_eq(s_b0, "first"), "FieldChild.b[0] is still \"first\"");
    CHECK(str_eq(vector_at(vm_b, b_b, 1u), "second"),
          "FieldChild.b[1] is still \"second\"");
    /* (5) identity: elementData[0] and the interned literal are one object */
    CHECK(interned_holds(vm_b, s_b0),
          "the restored String is the same object as the interned literal");
    CHECK(b_b != b_a, "the arena really moved (a new object address)");
    CHECK(nj_string_len(s_b0) == 5u, "nj_string_len() works on restored data");

    /* the stale-top thread came back with its operand stack pointer intact */
    if (saved_nframes > 0 && saved_thread_idx < vm_b->nthreads) {
        nj_thread *t = vm_b->threads[saved_thread_idx];

        CHECK(t != NULL && t->nframes == saved_nframes,
              "the restored thread kept its frame chain");
        if (t && t->nframes == saved_nframes) {
            nj_frame *fr = &t->frames[saved_nframes - 1];

            CHECK((nj_u4)(fr->sp - t->stack_mem) == saved_sp_off,
                  "the restored frame's operand stack pointer is intact");
        }
    }

    /* (2) it can allocate, and run more frames */
    /* nj_alloc() is the heap allocator: the free lists must have been rebuilt
     * by the load, or this returns NULL and the restored MIDlet cannot run. */
    CHECK(nj_heap_free_bytes(vm_b) > 0u, "the restored heap has free blocks");
    blk = nj_alloc(vm_b, 256u);
    CHECK(blk != NULL, "nj_alloc() works on the restored heap");
    if (blk) {
        memset(blk, 0xA5, 256u);
        *((nj_u4 *)blk) = 0x12345678u;
        CHECK(*((nj_u4 *)blk) == 0x12345678u, "the freshly allocated block is usable");
        CHECK((nj_u1 *)blk < vm_b->heap.base + vm_b->heap.bump,
              "the block came from a free list, not the bump pointer");
        /*
         * nj_alloc() hands back raw bytes, but every caller in the core turns
         * them into an object; the save walk reads a block header and expects
         * a live block to hold one.  Leave a valid, unreachable object header
         * behind so this block does not look like corruption to the save that
         * follows (a real embedder has no reason to keep raw arena bytes).
         */
        {
            nj_object *o = (nj_object *)blk;
            o->cls = NULL;
            o->u.raw = NULL;
            o->length = 0u;
            o->size = 256u;
            o->kind = NJ_OBJ_PLAIN;
            o->mark = 0u;
            o->aflags = 0u;
        }
    }
    fresh = nj_new_string_utf8(vm_b, "after-load", -1);
    CHECK(fresh != NULL, "the restored VM can create a Java String");
    CHECK(str_eq(fresh, "after-load"), "the new String reads back");

    frames_before = vm_b->frames_presented;
    for (i = 0; i < 5; i++) {
        (void)nj_midp_frame(vm_b);
    }
    CHECK(vm_b->frames_presented > frames_before,
          "the restored VM presented more frames");
    CHECK(!nj_has_exception(vm_b), "no exception while running after the load");
    CHECK(field_child_b(vm_b) == b_b, "the static slot still points at it");
    CHECK(nj_vm_save(vm_b, NULL) != NJ_OK, "nj_vm_save(NULL file) is refused");

    /* Saving the restored VM again must produce a blob a third VM can use. */
    CHECK(nj_vm_save_file(vm_b, GEN2_PATH) == NJ_OK,
          "the restored VM can be saved again");
    vm_e = nj_vm_create(&port, HEAP_BYTES);
    {
        nj_app *app_e = nj_app_open(vm_e, jar, jar_len);
        CHECK(app_e != NULL, "JAR attaches to the third VM");
        CHECK(nj_vm_load_file(vm_e, GEN2_PATH) == NJ_OK,
              "second-generation blob loads");
        b_e = field_child_b(vm_e);
        CHECK(b_e != NULL, "second generation keeps the static field");
        CHECK(vector_size(vm_e, b_e) == 2, "second generation keeps the Vector");
        CHECK(str_eq(vector_at(vm_e, b_e, 0u), "first"),
              "second generation keeps \"first\"");
        if (app_e) {
            nj_app_close(vm_e, app_e);
        }
    }

after_vm_b:
    /*
     * Regression: a class that was merely LOADED when the snapshot was taken.
     * A real game carries one - 仙剑奇侠传 has pal/e, a class the verifier
     * resolved and the program never used - and its entry has instance_size 0
     * and no static slots at all.  Linking it again on load handed out static
     * slots the saved VM had given to other classes, and comparing its saved
     * instance_size (0) against the freshly linked one refused the whole blob,
     * so the restored game never started.  Forge that entry and check that the
     * load succeeds and leaves the class exactly as it was.
     */
    {
        nj_class *victim = NULL;
        nj_u2 vi;

        for (vi = 0u; vi < vm_a->nclasses; vi++) {
            nj_class *c = vm_a->classes[vi];

            if (c && !c->is_array && c->state >= NJ_CLASS_LINKED &&
                strcmp(c->name, "FieldParent") == 0) {
                victim = c;
                break;
            }
        }
        CHECK(victim != NULL, "found a class to present as never-linked");
        if (victim) {
            nj_u1 keep_state = victim->state;
            nj_u2 keep_size = victim->instance_size;
            nj_vm *vm_g = NULL;

            victim->state = NJ_CLASS_LOADED;
            victim->instance_size = 0u;
            CHECK(nj_vm_save_file(vm_a, UNLINKED_PATH) == NJ_OK,
                  "saved a blob holding a never-linked class");
            victim->state = keep_state;
            victim->instance_size = keep_size;

            vm_g = nj_vm_create(&port, HEAP_BYTES);
            CHECK(vm_g != NULL, "VM for the never-linked class test");
            if (vm_g) {
                nj_app *app_g = nj_app_open(vm_g, jar, jar_len);
                int ok = (app_g != NULL &&
                          nj_vm_load_file(vm_g, UNLINKED_PATH) == NJ_OK);

                CHECK(ok, "a blob with a never-linked class loads");
                if (ok) {
                    /*
                     * FieldParent is the superclass of a class the MIDlet does
                     * use, so linking can still reach it - what matters is that
                     * the load did not refuse the blob over a class the saved
                     * VM had never linked, and that the restored VM runs.
                     */
                    nj_u4 before = vm_g->frames_presented;

                    CHECK(nj_heap_free_bytes(vm_g) > 0u,
                          "the restored VM has a usable heap");
                    (void)nj_midp_frame(vm_g);
                    CHECK(vm_g->frames_presented >= before,
                          "the restored VM still runs a frame");
                }
                if (app_g) {
                    nj_app_close(vm_g, app_g);
                }
                nj_vm_destroy(vm_g);
            }
        }
    }
    /* (3) a damaged CRC is refused */
    CHECK(corrupt_one_byte(STATE_PATH, BAD_PATH), "produced a corrupted copy");
    vm_c = nj_vm_create(&port, HEAP_BYTES);
    {
        nj_app *app_c = nj_app_open(vm_c, jar, jar_len);
        CHECK(app_c != NULL, "JAR attaches to the CRC-test VM");
        CHECK(nj_vm_load_file(vm_c, BAD_PATH) == NJ_ERR_FORMAT,
              "a damaged CRC is refused with NJ_ERR_FORMAT");
        if (app_c) {
            nj_app_close(vm_c, app_c);
        }
    }

    /* (4) a blob whose class table does not match the attached JAR */
    vm_d = nj_vm_create(&port, HEAP_BYTES);
    {
        nj_app *app_d = nj_app_open(vm_d, other, other_len);
        CHECK(app_d != NULL, "the other JAR attaches");
        CHECK(nj_vm_load_file(vm_d, STATE_PATH) == NJ_ERR_FORMAT,
              "a blob from another JAR is refused with NJ_ERR_FORMAT");
        if (app_d) {
            nj_app_close(vm_d, app_d);
        }
    }
    /* ...and with no JAR at all there is nothing to resolve classes from. */
    {
        nj_vm *vm_f = nj_vm_create(&port, HEAP_BYTES);
        CHECK(vm_f != NULL, "VM for the no-JAR test");
        CHECK(nj_vm_load_file(vm_f, STATE_PATH) == NJ_ERR_STATE,
              "a load with no class source is refused");
        nj_vm_destroy(vm_f);
    }
    /* A missing / truncated file is refused too. */
    CHECK(nj_vm_load_file(vm_a, "/nonexistent-dir/nope.bin") != NJ_OK,
          "a missing blob is refused");
    {
        FILE *f = fopen(BAD_PATH, "wb");
        if (f) {
            fwrite("NJSS", 1, 4u, f);
            fclose(f);
        }
        CHECK(nj_vm_load_file(vm_a, BAD_PATH) != NJ_OK,
              "a truncated blob is refused");
    }

    test_large_heap(&port, jar, jar_len);
    test_restored_media(&port, jar, jar_len);
    test_clock_restore(&port, jar, jar_len);

    /* ---- teardown ---------------------------------------------------- */
    if (app_a) {
        nj_app_close(vm_a, app_a);
    }
    if (app_b) {
        nj_app_close(vm_b, app_b);
    }
    nj_vm_destroy(vm_a);
    nj_vm_destroy(vm_b);
    nj_vm_destroy(vm_c);
    nj_vm_destroy(vm_d);
    nj_vm_destroy(vm_e);
    nj_port_shutdown(&port);
    remove(STATE_PATH);
    remove(GEN2_PATH);
    remove(UNLINKED_PATH);
    remove(BAD_PATH);
    free(jar);
    free(other);

    printf("  %d checks, %d failures\n", checks, failures);
    if (failures) {
        printf("\nFAIL: save/restore\n");
        return 1;
    }
    printf("PASS: save state / restore state\n");
    return 0;
}
