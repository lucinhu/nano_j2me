/*
 * nano_j2me - tests/test_speed.c
 *
 * The game-speed multiplier is one clock, and this pins its arithmetic.
 *
 * nj_now_ms() is the only clock a MIDlet can read: Thread.sleep, Object.wait,
 * java.util.Timer and the frame pacer all come through it.  That is what makes
 * `nj_vm_set_speed(vm, 200)` a game speed rather than a frame skip - and also
 * what makes it easy to get wrong, because a clock that jumps when the
 * multiplier changes would make every parked deadline fire at once.
 *
 * The test drives a port whose millis() the test owns, so "one real second"
 * is exactly what it says here.
 */
#include "nj2me/nj.h"
#include "nj_internal.h"

#include <stdio.h>
#include <string.h>

static nj_u4 fake_now;
static int checks;
static int fails;

static nj_u4 fake_millis(void *ud)
{
    (void)ud;
    return fake_now;
}

static void eq(const char *what, nj_s8 got, nj_s8 want)
{
    checks++;
    if (got != want) {
        fails++;
        printf("FAIL %s: got %lld, want %lld\n", what, (long long)got,
               (long long)want);
    }
}

int main(void)
{
    nj_port port;
    nj_vm *vm;
    nj_s8 base;
    int i;

    if (nj_port_headless(&port) != NJ_OK) {
        printf("FAIL headless port did not come up\n");
        return 1;
    }
    /* The whole point: this test owns the clock. */
    port.millis = fake_millis;

    fake_now = 0u;
    vm = nj_vm_create(&port, 1024u * 1024u);
    if (!vm) {
        printf("FAIL vm create\n");
        nj_port_shutdown(&port);
        return 1;
    }

    eq("a fresh VM runs at real time", nj_vm_speed(vm), 100);

    /* Real time: one real second is one MIDlet second. */
    fake_now = 1000u;
    base = nj_now_ms(vm);
    fake_now = 2000u;
    eq("1x: a real second is a MIDlet second", nj_now_ms(vm) - base, 1000);
    eq("1x: the absolute clock is the port clock", base, 1000);

    /* Twice as fast. */
    nj_vm_set_speed(vm, 200);
    eq("2x: reported", nj_vm_speed(vm), 200);
    base = nj_now_ms(vm);
    eq("2x: changing the speed does not move the clock", base, 2000);
    fake_now = 3000u;
    eq("2x: a real second is two MIDlet seconds", nj_now_ms(vm) - base, 2000);

    /* Half speed, and the instant it was set is preserved. */
    nj_vm_set_speed(vm, 50);
    base = nj_now_ms(vm);
    eq("0.5x: starts where 2x left off", base, 4000);
    fake_now = 4000u;
    eq("0.5x: a real second is half a MIDlet second", nj_now_ms(vm) - base, 500);

    /* A quarter of the way through, turning it up must not jump. */
    fake_now = 4500u;
    nj_vm_set_speed(vm, 300);
    base = nj_now_ms(vm);
    eq("3x: no jump on the way up", base, 4750);
    fake_now = 5500u;
    eq("3x: a real second is three MIDlet seconds", nj_now_ms(vm) - base, 3000);

    /* Back to real time: the clock keeps running from where it was. */
    base = nj_now_ms(vm);
    nj_vm_set_speed(vm, 100);
    eq("back to 1x: no jump", nj_now_ms(vm), base);
    fake_now = 6500u;
    eq("back to 1x: real again", nj_now_ms(vm) - base, 1000);

    /* Clamps: a speed of zero is a frozen game and a speed of 500% is a
     * different game, so both ends are bounded. */
    nj_vm_set_speed(vm, 0);
    eq("a speed below the floor is clamped", nj_vm_speed(vm), 10);
    nj_vm_set_speed(vm, 5000);
    eq("a speed above the ceiling is clamped", nj_vm_speed(vm), 800);

    /* A scheduler pass is not a millisecond.
     *
     * nj_yield() and the end of scheduler_step() each used to do
     * `vm->scheduler_ticks++`, and nj_now_ms() folds the port's millisecond
     * clock into that same counter - so every scheduler pass moved the
     * MIDlet's clock forward ON TOP of real time.  A busy title gained about
     * 30 ms of MIDlet time per 5 ms frame, the runner's frame pacer measured
     * every frame as over budget and never slept, and `--fps 30` ran
     * 合金弹头2013BT版_678 at 154 fps with every countdown and animation in it
     * scaled by the same factor.  The clock is the port's clock, and nothing
     * else may advance it.
     */
    nj_vm_set_speed(vm, 100);
    fake_now = 20000u;
    base = nj_now_ms(vm);
    for (i = 0; i < 5000; i++) {
        nj_yield(vm);
    }
    eq("a scheduler pass does not move the clock", nj_now_ms(vm) - base, 0);

    /* The fold: the port's 32-bit clock wraps, and the MIDlet must not see
     * time go backwards when it does. */
    nj_vm_set_speed(vm, 100);
    fake_now = 0xFFFFFF00u;
    base = nj_now_ms(vm);
    fake_now = 0x00000100u;      /* wrapped past zero */
    eq("the clock survives a port wrap", nj_now_ms(vm) - base, 512);

    /* Every speed in the range is monotonic: a clock that steps backwards
     * would make the pacer sleep for hours (see frame_pace). */
    for (i = 0; i < 3; i++) {
        nj_s8 prev = nj_now_ms(vm);
        nj_s8 next;
        nj_vm_set_speed(vm, 10 + i * 300);
        fake_now += 7u;
        next = nj_now_ms(vm);
        checks++;
        if (next < prev) {
            fails++;
            printf("FAIL the clock went backwards at %d%%\n",
                   10 + i * 300);
        }
    }

    nj_vm_destroy(vm);
    nj_port_shutdown(&port);

    printf("%d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
