/*
 * Nokia Sound frequency conversion and SoundListener callback arguments.
 * The listener must receive the Sound that changed and its new state; games
 * use both to select the channel that finished or stopped playing.
 */
#include <stdio.h>
#include <string.h>

#include "nj2me/nj.h"

#include "nj_internal.h"

static int g_pass;
static int g_fail;

#define OK(cond, msg)                                                          \
    do {                                                                       \
        if (cond) {                                                            \
            g_pass++;                                                          \
        } else {                                                               \
            g_fail++;                                                          \
            printf("FAIL %d: %s\n", __LINE__, (msg));                          \
        }                                                                      \
    } while (0)

static nj_s4 const_of(nj_vm *vm, const char *name)
{
    nj_field *f = nj_field_lookup(vm, "com/nokia/mid/sound/Sound", name, "I", 1);

    return f ? nj_field_read_int(vm, NULL, f) : -9999;
}

static nj_s4 note_of(nj_vm *vm, nj_s4 freq)
{
    nj_value arg;
    nj_value ret;

    arg.i = freq;
    ret.i = -1;
    if (nj_call_static(vm, "com/nokia/mid/sound/Sound", "convertFreqToNote",
                       "(I)I", &arg, &ret) != NJ_OK) {
        return -1;
    }
    return ret.i;
}

#define SOUND "com/nokia/mid/sound/Sound"
#define LISTENER "com/nokia/mid/sound/SoundListener"

static nj_ref g_expected_sound;
static nj_s4 g_expected_event;
static int g_notifications;

NJ_NATIVE_FN(sound_changed)
{
    nj_ref sound = NJ_ARG_REF(args, 0);
    nj_value state;
    (void)th;
    (void)ret;
    g_notifications++;
    OK(sound == g_expected_sound, "callback receives the Sound whose state changed");
    OK(NJ_ARG_INT(args, 1) == g_expected_event, "callback receives the new state");
    /* Only dereference a valid Sound, so the old broken callback produces an
     * assertion failure rather than crashing this regression fixture. */
    if (sound == g_expected_sound) {
        nj_gc_collect(vm);
        OK(nj_call(vm, sound, SOUND, "getState", "()I", NULL, &state) == NJ_OK,
           "the callback Sound survives collection");
        OK(state.i == g_expected_event, "getState agrees with the callback event");
    }
    return NJ_OK;
}

static void test_sound_listener(nj_vm *vm)
{
    static const nj_builtin_method methods[] = {
        {"soundStateChanged", "(L" SOUND ";I)V", NJ_ACC_PUBLIC}
    };
    static const nj_builtin_class def = {
        "test/NokiaSoundListener", "java/lang/Object", NJ_ACC_PUBLIC,
        NULL, 0, methods, 1
    };
    nj_class *cls = nj_class_create_builtin(vm, &def);
    nj_u4 mark = nj_root_mark(vm);
    nj_ref sound, listener;
    nj_value a[3];

    OK(cls != NULL, "create the native listener fixture");
    if (!cls) return;
    cls->methods[0].native = sound_changed;
    sound = nj_root(vm, nj_object_new(vm, SOUND));
    listener = nj_root(vm, nj_object_new(vm, def.name));
    OK(sound != NULL && listener != NULL, "create Sound and listener");
    if (!sound || !listener) goto done;
    a[0].bits = a[1].bits = a[2].bits = 0;
    a[0].i = 440; a[1].j = 100;
    OK(nj_call(vm, sound, SOUND, "<init>", "(IJ)V", a, NULL) == NJ_OK,
       "initialize a stopped Sound");
    a[0].r = listener;
    OK(nj_call(vm, sound, SOUND, "setSoundListener", "(L" LISTENER ";)V", a, NULL) == NJ_OK,
       "register the listener");
    g_expected_sound = sound;
    g_notifications = 0;
    g_expected_event = 0; /* SOUND_PLAYING, Nokia's actual value */
    a[0].i = 1;
    OK(nj_call(vm, sound, SOUND, "play", "(I)V", a, NULL) == NJ_OK,
       "play invokes the listener");
    OK(g_notifications == 1, "play sends one notification");
    g_expected_event = 1; /* SOUND_STOPPED */
    OK(nj_call(vm, sound, SOUND, "stop", "()V", NULL, NULL) == NJ_OK,
       "stop invokes the listener");
    OK(g_notifications == 2, "stop sends one notification");
    g_expected_event = 0;
    OK(nj_call(vm, sound, SOUND, "resume", "()V", NULL, NULL) == NJ_OK,
       "resume invokes the listener");
    OK(g_notifications == 3, "resume sends one notification");
    OK(!nj_has_exception(vm), "notifications leave no exception pending");
    g_expected_sound = NULL;
done:
    nj_root_release(vm, mark);
}

int main(void)
{
    nj_port port;
    nj_vm *vm;
    nj_class *cls;

    printf("== nano_j2me Nokia Sound.convertFreqToNote test ==\n");

    memset(&port, 0, sizeof(port));
    if (nj_port_headless(&port) != NJ_OK) {
        fprintf(stderr, "cannot start the headless port\n");
        return 2;
    }
    vm = nj_vm_create(&port, 2u * 1024u * 1024u);
    if (!vm) {
        fprintf(stderr, "cannot create the VM\n");
        nj_port_shutdown(&port);
        return 2;
    }

    cls = nj_class_resolve(vm, "com/nokia/mid/sound/Sound");
    OK(cls != NULL, "the Sound class comes from the Nokia extension");

    /* The state constants come from Nokia's own class library: a MIDlet that
     * compares getState() with SOUND_PLAYING must see the pair a handset gave. */
    OK(const_of(vm, "SOUND_PLAYING") == 0, "SOUND_PLAYING is 0");
    OK(const_of(vm, "SOUND_STOPPED") == 1, "SOUND_STOPPED is 1");
    OK(const_of(vm, "SOUND_UNINITIALIZED") == 3, "SOUND_UNINITIALIZED is 3");

    /* A4 is 440 Hz and MIDI 69; note 0 is 8.176 Hz, so 8.176 kHz is 120. */
    OK(note_of(vm, 440) == 69, "440 Hz is MIDI note 69");
    OK(note_of(vm, 880) == 81, "880 Hz is one octave up (81)");
    OK(note_of(vm, 8176) == 120, "8.176 kHz is MIDI note 120");
    OK(note_of(vm, 0) == 0, "zero frequency clamps to note 0");
    OK(note_of(vm, -5) == 0, "a negative frequency clamps to note 0");

    test_sound_listener(vm);

    nj_vm_destroy(vm);
    nj_port_shutdown(&port);

    printf("%d passed, %d failed\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
