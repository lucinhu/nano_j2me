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

static nj_datetime rtc;
static int rtc_calls, rtc_ok;

static int fake_rtc(void *ud, nj_datetime *out)
{
    (void)ud;
    rtc_calls++;
    *out = rtc;
    return rtc_ok;
}

static void check_current_calendars(nj_vm *vm, nj_s8 expected)
{
    nj_value result, arg;
    nj_ref zone;
    int variant;

    arg.r = nj_root(vm, nj_string_new(vm, "GMT"));
    eq("GMT lookup succeeds", nj_call(vm, NULL, "java/util/TimeZone",
       "getTimeZone", "(Ljava/lang/String;)Ljava/util/TimeZone;", &arg,
       &result), NJ_OK);
    zone = nj_root(vm, result.r);
    for (variant = 0; variant < 2; variant++) {
        nj_ref calendar, date;
        arg.r = zone;
        eq("Calendar.getInstance succeeds", nj_call(vm, NULL,
           "java/util/Calendar", "getInstance", variant
           ? "(Ljava/util/TimeZone;)Ljava/util/Calendar;"
           : "()Ljava/util/Calendar;", variant ? &arg : NULL, &result), NJ_OK);
        calendar = nj_root(vm, result.r);
        eq("Calendar.getTime succeeds", nj_call(vm, calendar,
           "java/util/Calendar", "getTime", "()Ljava/util/Date;", NULL,
           &result), NJ_OK);
        date = nj_root(vm, result.r);
        eq("Calendar Date.getTime succeeds", nj_call(vm, date,
           "java/util/Date", "getTime", "()J", NULL, &result), NJ_OK);
        eq("a new Calendar captures the current instant", result.j, expected);
    }
}

static void test_epoch_clock(nj_port *port)
{
    static const struct {
        nj_datetime date;
        nj_s8 expected;
    } cases[] = {
        {{1969, 11, 31, 23, 59, 59, 999, 3, 0}, -1},
        {{1970, 0, 1, 0, 0, 0, 0, 4, 0}, 0},
        {{2000, 1, 29, 12, 34, 56, 789, 2, 330}, 951807896789LL},
        {{2026, 9, 20, 11, 21, 13, 456, 2, 480}, 1792466473456LL},
        {{2038, 0, 19, 3, 14, 8, 1, 2, -210}, 2147496248001LL},
        {{2100, 2, 1, 0, 0, 0, 0, 1, 0}, 4107542400000LL}
    };
    unsigned i;
    port->now = fake_rtc;
    rtc_ok = 1;
    for (i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        nj_vm *vm;
        nj_value result;
        nj_ref date;
        int calls_after_calendar;
        rtc = cases[i].date;
        rtc_calls = 0;
        fake_now = 12345;
        vm = nj_vm_create(port, 1024u * 1024u);
        eq("RTC VM exists", vm != NULL, 1);
        if (!vm) { continue; }
        eq("local RTC converts to UTC epoch", nj_now_ms(vm), cases[i].expected);
        eq("RTC sampled once at creation", rtc_calls, 1);
        vm->running = 1;
        eq("System.currentTimeMillis succeeds", nj_call(vm, NULL,
           "java/lang/System", "currentTimeMillis", "()J", NULL, &result), NJ_OK);
        eq("Java reads the epoch", result.j, cases[i].expected);
        date = nj_root(vm, nj_object_new(vm, "java/util/Date"));
        eq("Date exists", date != NULL, 1);
        eq("Date() succeeds", nj_call_special(vm, date, "java/util/Date",
           "<init>", "()V", NULL, NULL), NJ_OK);
        eq("Date.getTime succeeds", nj_call(vm, date, "java/util/Date",
           "getTime", "()J", NULL, &result), NJ_OK);
        eq("Date() captures the same current instant", result.j, cases[i].expected);
        check_current_calendars(vm, cases[i].expected);
        calls_after_calendar = rtc_calls;
        rtc.year = 1980; /* A later host clock adjustment cannot move deadlines. */
        fake_now += 25;
        eq("RTC changes do not jump virtual time", nj_now_ms(vm), cases[i].expected + 25);
        nj_vm_set_speed(vm, 200);
        eq("changing epoch clock speed does not jump", nj_now_ms(vm), cases[i].expected + 25);
        fake_now += 50;
        eq("epoch clock still scales elapsed time", nj_now_ms(vm), cases[i].expected + 125);
        eq("later clock reads do not resample RTC", rtc_calls, calls_after_calendar);
        nj_vm_destroy(vm);
    }
    /* Reject an unavailable RTC and malformed calendar values without UB or
     * silently accepting a normalized invalid date. */
    for (i = 0; i < 10; i++) {
        nj_vm *vm;
        rtc = cases[3].date;
        rtc_ok = i != 0;
        switch (i) {
        case 1: rtc.month = -1; break;
        case 2: rtc.month = 12; break;
        case 3: rtc.year = 0; break;
        case 4: rtc.year = 2100; rtc.month = 1; rtc.day = 29; break;
        case 5: rtc.hour = 24; break;
        case 6: rtc.minute = 60; break;
        case 7: rtc.second = 60; break;
        case 8: rtc.millis = 1000; break;
        case 9: rtc.tz_offset = 1440; break;
        }
        fake_now = 700;
        vm = nj_vm_create(port, 1024u * 1024u);
        eq("fallback VM exists", vm != NULL, 1);
        if (vm) {
            eq("unavailable or invalid RTC retains raw clock", nj_now_ms(vm), 700);
            nj_vm_destroy(vm);
        }
    }
    port->now = NULL;
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
    port.now = NULL; /* Explicitly exercise the no-RTC embedded fallback. */

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
    test_epoch_clock(&port);
    nj_port_shutdown(&port);

    printf("%d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
