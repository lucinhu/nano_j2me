/*
 * com.nokia.mid.sound.Sound.convertFreqToNote()
 *
 * The Nokia UI helper maps a tone frequency onto a MIDI note number,
 * n = round(12 * log2(f / 8.176)) clamped at zero.  Real titles use it to turn
 * an entered frequency into the note the tone player plays, and the reference
 * implementation computes it with Math.log/Math.round, which the runtime has
 * to reproduce without a floating-point math library of its own.
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

    /* A4 is 440 Hz and MIDI 69; note 0 is 8.176 Hz, so 8.176 kHz is 120. */
    OK(note_of(vm, 440) == 69, "440 Hz is MIDI note 69");
    OK(note_of(vm, 880) == 81, "880 Hz is one octave up (81)");
    OK(note_of(vm, 8176) == 120, "8.176 kHz is MIDI note 120");
    OK(note_of(vm, 0) == 0, "zero frequency clamps to note 0");
    OK(note_of(vm, -5) == 0, "a negative frequency clamps to note 0");

    nj_vm_destroy(vm);
    nj_port_shutdown(&port);

    printf("%d passed, %d failed\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
