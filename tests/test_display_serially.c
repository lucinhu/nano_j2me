/*
 * Event-loop regression for Display.callSerially (no JAR or wall-clock waits).
 * MIDP 2 Display: pending Canvas paint and buffer switch precede Runnable.run;
 * calls are FIFO, once per enqueue, and independent of subsequent repaints.
 * Snapshot/64-per-frame fairness and exception isolation are runtime policies.
 *
 * Builtin native fixtures enter the real Display/Canvas API and nj_midp_frame.
 * No test calls the new dispatch helper or manufactures queue/cadence state.
 */
#include "nj2me/nj.h"
#include "nj_internal.h"
#include "nj_midp.h"

#include <stdio.h>
#include <string.h>

#define C_RUN "test/SerialRunnable"
#define C_CANVAS "test/SerialCanvas"
#define D_DISPLAYABLE "Ljavax/microedition/lcdui/Displayable;"
#define D_GRAPHICS "Ljavax/microedition/lcdui/Graphics;"
#define GREEN 0xFF00FF00u
#define BLUE  0xFF0000FFu

enum { PLAIN, SELF, NEST, COLLECT, THROW, SWITCH_FORM };
enum { EV_PAINT = -1, EV_PRESENT = -2 };

static int checks, failures;
static const char *case_name;
#define CHECK(c) do {                                                    \
    checks++;                                                            \
    if (!(c)) {                                                          \
        failures++;                                                      \
        fprintf(stderr, "FAIL %s:%d [%s]: %s\n",                         \
                __FILE__, __LINE__, case_name, #c);                       \
    }                                                                    \
} while (0)

typedef struct fixture {
    nj_port port;
    nj_vm *vm;
    nj_ref display, form, from_paint, graphics;
    int queue_in_paint;
    nj_field *id, *mode, *payload;
    void (*present)(void *, int, int, int, int);
    int runs[8], depth, max_depth;
    int events[256], nevents, paints, presents;
    int nesting;
    nj_u4 paint_color, presented_pixel;
} fixture;

/* Tests own this observer; the runtime has no mutable globals. */
static fixture *active;

static void event(int code)
{
    if (active->nevents < (int)(sizeof(active->events) / sizeof(active->events[0]))) {
        active->events[active->nevents++] = code;
    }
}

static void observe_present(void *ud, int x, int y, int w, int h)
{
    fixture *f = active;
    f->presents++;
    f->presented_pixel = nj_surface_get(nj_midp_screen(f->vm), 8, 8);
    event(EV_PRESENT);
    if (f->present) { f->present(ud, x, y, w, h); }
}

static int call(nj_ref obj, const char *cls, const char *name,
                const char *desc, nj_value *args, nj_value *ret)
{
    nj_status st = nj_call(active->vm, obj, cls, name, desc, args, ret);
    CHECK(st == NJ_OK);
    if (st != NJ_OK) {
        fprintf(stderr, "  %s: %s\n", name, nj_exception_describe(active->vm));
        nj_clear_exception(active->vm);
        return 0;
    }
    return 1;
}

static nj_ref root(nj_ref r)
{
    nj_u4 n = active->vm->nroots;
    CHECK(r != NULL);
    if (r) {
        (void)nj_root(active->vm, r);
        CHECK(active->vm->nroots == n + 1u);
    }
    return r;
}

static int enqueue(nj_ref r)
{
    nj_value a[1];
    a[0].r = r;
    return call(active->display, NJ_C_DISPLAY, "callSerially",
                "(Ljava/lang/Runnable;)V", a, NULL);
}

static void set_current(nj_ref r)
{
    nj_value a[1];
    a[0].r = r;
    (void)call(active->display, NJ_C_DISPLAY, "setCurrent",
               "(" D_DISPLAYABLE ")V", a, NULL);
}

static void frame(void)
{
    CHECK(nj_midp_frame(active->vm) == NJ_OK);
    CHECK(!nj_has_exception(active->vm));
}

static void flush(nj_ref canvas)
{
    (void)call(canvas, NJ_C_GAMECANVAS, "flushGraphics", "()V", NULL, NULL);
}

static void fill(nj_ref g, nj_u4 color)
{
    nj_value a[4];
    nj_surface *s = nj_midp_screen(active->vm);
    a[0].i = (nj_s4)(color & 0xFFFFFFu);
    (void)call(g, NJ_C_GRAPHICS, "setColor", "(I)V", a, NULL);
    a[0].i = 0; a[1].i = 0; a[2].i = s->width; a[3].i = s->height;
    (void)call(g, NJ_C_GRAPHICS, "fillRect", "(IIII)V", a, NULL);
}

static nj_u4 screen_color(nj_u4 color)
{
    nj_u4 pixel = 0u;
    nj_surface s = *nj_midp_screen(active->vm);
    s.pixels = &pixel; s.width = 1; s.height = 1; s.stride = (int)sizeof(pixel);
    nj_surface_set(&s, 0, 0, color);
    return nj_surface_get(&s, 0, 0);
}

NJ_NATIVE_FN(paint_fixture)
{
    active->paints++;
    event(EV_PAINT);
    active->graphics = NJ_ARG_REF(args, 0);
    fill(active->graphics, active->paint_color);
    if (active->queue_in_paint) {
        active->queue_in_paint = 0;
        (void)call(NJ_THIS(args), NJ_C_CANVAS, "repaint", "()V", NULL, NULL);
        (void)enqueue(active->from_paint);
    }
    return NJ_OK;
}

NJ_NATIVE_FN(run_fixture)
{
    fixture *f = active;
    nj_ref self = NJ_THIS(args);
    int id = nj_field_read_int(vm, self, f->id);
    int mode = nj_field_read_int(vm, self, f->mode);
    CHECK(id > 0 && id < (int)(sizeof(f->runs) / sizeof(f->runs[0])));
    if (id <= 0 || id >= (int)(sizeof(f->runs) / sizeof(f->runs[0]))) {
        return NJ_ERR;
    }
    f->depth++;
    if (f->depth > f->max_depth) { f->max_depth = f->depth; }
    f->runs[id]++;
    event(id);

    if (mode == SELF && f->runs[id] < 3) {
        (void)enqueue(self);
    } else if (mode == NEST && !f->nesting) {
        int before = f->runs[2];
        f->nesting = 1;
        /* Blocking callbacks can pump a full nested frame. Neither that
         * boundary nor a synchronous paint may invoke the next Runnable. */
        CHECK(nj_midp_frame(vm) == NJ_OK);
        CHECK(f->runs[2] == before);
        CHECK(nj_midp_paint(vm) == NJ_OK);
        CHECK(f->runs[2] == before);
        f->nesting = 0;
    } else if (mode == COLLECT) {
        nj_vm_stats before, after;
        nj_ref garbage = nj_array_new(vm, "B", 4096u);
        CHECK(garbage != NULL);
        nj_vm_get_stats(vm, &before);
        (void)nj_gc_collect(vm);
        nj_vm_get_stats(vm, &after);
        CHECK(after.gc_count == before.gc_count + 1u);
        CHECK(!nj_heap_owns_object_fast(vm, garbage));
        /* The queue has removed self. C locals and this fixture's global
         * observer are NOT Java GC roots; no test root protects this receiver. */
        CHECK(nj_heap_owns_object_fast(vm, self));
        if (nj_heap_owns_object_fast(vm, self)) {
            nj_ref p = nj_field_read_ref(vm, self, f->payload);
            CHECK(nj_heap_owns_object_fast(vm, p));
            if (nj_heap_owns_object_fast(vm, p)) {
                CHECK(nj_array_length(p) == 1u);
                CHECK(nj_array_get_int(p, 0u) == 0x12345678);
            }
            CHECK(nj_field_read_int(vm, self, f->id) == id);
        }
    } else if (mode == SWITCH_FORM) {
        set_current(f->form);
    }
    f->depth--;
    if (mode == THROW) {
        return nj_throw(vm, "java/lang/IllegalStateException", "serial fixture");
    }
    return vm->exception ? NJ_ERR : NJ_OK;
}

static int begin(fixture *f, const char *name)
{
    static const nj_builtin_field fields[] = {
        { "id", "I", 0 }, { "mode", "I", 0 }, { "payload", "[I", 0 }
    };
    static const nj_builtin_method run[] = { { "run", "()V", NJ_ACC_PUBLIC } };
    static const nj_builtin_method paint[] = {
        { "paint", "(" D_GRAPHICS ")V", NJ_ACC_PUBLIC }
    };
    static const nj_builtin_class runnable = {
        C_RUN, "java/lang/Object", NJ_ACC_PUBLIC, fields, 3u, run, 1u
    };
    static const nj_builtin_class canvas = {
        C_CANVAS, NJ_C_CANVAS, NJ_ACC_PUBLIC, NULL, 0u, paint, 1u
    };
    nj_class *c;
    memset(f, 0, sizeof(*f));
    active = f; case_name = name; f->paint_color = GREEN;
    CHECK(nj_port_headless(&f->port) == NJ_OK);
    if (!f->port.ud) { return 0; }
    f->port.fps_cap = 0;
    f->present = f->port.display_present;
    f->port.display_present = observe_present;
    f->vm = nj_vm_create(&f->port, 4u * 1024u * 1024u);
    CHECK(f->vm != NULL);
    if (!f->vm) { nj_port_shutdown(&f->port); return 0; }
    f->display = nj_midp_display(f->vm);
    CHECK(f->display != NULL);
    c = nj_class_create_builtin(f->vm, &runnable);
    CHECK(c != NULL);
    if (c) { c->methods[0].native = run_fixture; }
    c = nj_class_create_builtin(f->vm, &canvas);
    CHECK(c != NULL);
    if (c) { c->methods[0].native = paint_fixture; }
    f->id = nj_field_lookup(f->vm, C_RUN, "id", "I", 0);
    f->mode = nj_field_lookup(f->vm, C_RUN, "mode", "I", 0);
    f->payload = nj_field_lookup(f->vm, C_RUN, "payload", "[I", 0);
    CHECK(f->id && f->mode && f->payload);
    if (!(f->display && c && f->id && f->mode && f->payload)) {
        (void)nj_vm_destroy(f->vm);
        nj_port_shutdown(&f->port);
        active = NULL;
        return 0;
    }
    return 1;
}

static void end(fixture *f)
{
    CHECK(!nj_has_exception(f->vm));
    CHECK(nj_vm_destroy(f->vm) != 0u);
    nj_port_shutdown(&f->port);
    active = NULL;
}

static nj_ref runnable(int id, int mode)
{
    nj_ref r = root(nj_object_new(active->vm, C_RUN));
    if (r) {
        nj_field_write_int(active->vm, r, active->id, id);
        nj_field_write_int(active->vm, r, active->mode, mode);
    }
    return r;
}

static nj_ref canvas(int game)
{
    nj_ref c = root(nj_object_new(active->vm, game ? NJ_C_GAMECANVAS : C_CANVAS));
    nj_value a[1];
    a[0].i = 0;
    if (c) {
        CHECK(nj_call_special(active->vm, c, game ? NJ_C_GAMECANVAS : NJ_C_CANVAS,
                             "<init>", game ? "(Z)V" : "()V",
                             game ? a : NULL, NULL) == NJ_OK);
        set_current(c);
    }
    return c;
}

static void green_flush(nj_ref c)
{
    nj_value ret;
    ret.r = NULL;
    if (call(c, NJ_C_GAMECANVAS, "getGraphics", "()" D_GRAPHICS, NULL, &ret)) {
        fill(ret.r, GREEN);
        flush(c);
    }
}

static void reset_events(void)
{
    active->nevents = 0;
}

static int count_events(int code)
{
    int i, n = 0;
    for (i = 0; i < active->nevents; i++) {
        if (active->events[i] == code) { n++; }
    }
    return n;
}

static void test_fifo_and_duplicates(void)
{
    fixture f;
    nj_ref a, b, c;
    int i, seen = 0;
    static const int want[] = { 1, 2, 1, 3 };
    if (!begin(&f, "FIFO and duplicate enqueue")) { return; }
    a = runnable(1, PLAIN); b = runnable(2, PLAIN); c = runnable(3, PLAIN);
    (void)enqueue(a); (void)enqueue(b); (void)enqueue(a); (void)enqueue(c);
    CHECK(f.runs[1] == 0 && f.runs[2] == 0 && f.runs[3] == 0);
    frame();
    for (i = 0; i < f.nevents; i++) {
        if (f.events[i] > 0) {
            CHECK(seen < 4 && f.events[i] == want[seen]);
            seen++;
        }
    }
    CHECK(seen == 4);
    CHECK(f.runs[1] == 2 && f.runs[2] == 1 && f.runs[3] == 1);
    frame();
    CHECK(f.runs[1] == 2 && f.runs[2] == 1 && f.runs[3] == 1);
    end(&f);
}

static void test_flush_and_self_requeue(void)
{
    fixture f;
    nj_ref c, r;
    int i;
    nj_vm_stats before, after;
    if (!begin(&f, "flush without repaint and self-requeue")) { return; }
    c = canvas(1);
    frame();                  /* consume setCurrent's explicit repaint first */
    /* The inherited sizeChanged() requests another repaint during that first
     * paint. Service it before measuring flush-only work; do not clear flags. */
    frame();
    CHECK(!f.vm->repaint_pending);
    green_flush(c);
    r = runnable(1, SELF);
    (void)enqueue(r);
    CHECK(f.runs[1] == 0);
    nj_vm_get_stats(f.vm, &before);
    for (i = 1; i <= 3; i++) {
        flush(c);             /* no repaint(), no direct paint() */
        frame();
        CHECK(f.runs[1] == i);
        CHECK(f.presented_pixel == screen_color(GREEN));
    }
    nj_vm_get_stats(f.vm, &after);
    CHECK(after.paints == before.paints);
    CHECK(f.max_depth == 1);
    frame();
    CHECK(f.runs[1] == 3);
    end(&f);
}

static void test_paint_and_present_order(void)
{
    fixture f;
    nj_ref c, r;
    int i, p = -1, s = -1, run = -1;
    if (!begin(&f, "paint alone does not dispatch; paint and present precede run")) { return; }
    c = canvas(0);
    frame();
    r = runnable(1, PLAIN);
    (void)enqueue(r);
    CHECK(nj_midp_paint(f.vm) == NJ_OK);
    CHECK(f.runs[1] == 0);   /* drawing alone is not an event iteration */
    frame();
    CHECK(f.runs[1] == 1);

    f.paint_color = BLUE;
    (void)call(c, NJ_C_CANVAS, "repaint", "()V", NULL, NULL);
    (void)enqueue(r);
    reset_events();
    frame();
    for (i = 0; i < f.nevents; i++) {
        if (f.events[i] == EV_PAINT && p < 0) { p = i; }
        if (f.events[i] == EV_PRESENT && s < 0) { s = i; }
        if (f.events[i] == 1 && run < 0) { run = i; }
    }
    CHECK(p >= 0 && s > p && run > s);
    CHECK(count_events(1) == 1);
    CHECK(f.presented_pixel == screen_color(BLUE));
    CHECK(f.runs[1] == 2);
    end(&f);
}

static void test_enqueue_from_paint(void)
{
    fixture f;
    nj_ref c;
    int paints, i, p = -1, s = -1, run = -1;
    if (!begin(&f, "paint requeues repaint and a new serial callback")) { return; }
    c = canvas(0);
    frame();
    /* Establish a real app-painted cadence: without a new explicit repaint,
     * the two following event frames would skip automatic painting. */
    (void)call(c, NJ_C_CANVAS, "repaint", "()V", NULL, NULL);
    (void)call(c, NJ_C_CANVAS, "serviceRepaints", "()V", NULL, NULL);
    frame();
    CHECK(f.vm->app_painted != 0u && f.vm->app_paint_period >= 1u);
    /* Paint synchronously, then issue another request before any frame.
     * That second explicit request must override painted_in_frame. */
    (void)call(c, NJ_C_CANVAS, "repaint", "()V", NULL, NULL);
    (void)call(c, NJ_C_CANVAS, "serviceRepaints", "()V", NULL, NULL);
    f.from_paint = runnable(1, PLAIN);
    f.queue_in_paint = 1;
    paints = f.paints;
    (void)call(c, NJ_C_CANVAS, "repaint", "()V", NULL, NULL);
    frame();
    CHECK(f.paints == paints + 1);
    CHECK(f.runs[1] == 0);  /* enqueued inside paint: outside this frame's batch */
    reset_events();
    frame();
    CHECK(f.paints == paints + 2); /* repaint requested inside paint survived */
    CHECK(f.runs[1] == 1);
    for (i = 0; i < f.nevents; i++) {
        if (f.events[i] == EV_PAINT && p < 0) { p = i; }
        if (f.events[i] == EV_PRESENT && s < 0) { s = i; }
        if (f.events[i] == 1 && run < 0) { run = i; }
    }
    CHECK(p >= 0 && s > p && run > s);
    frame();
    CHECK(f.runs[1] == 1);
    end(&f);
}

static void request_and_service_twice(nj_ref c)
{
    (void)call(c, NJ_C_CANVAS, "repaint", "()V", NULL, NULL);
    (void)call(c, NJ_C_CANVAS, "serviceRepaints", "()V", NULL, NULL);
    (void)call(c, NJ_C_CANVAS, "repaint", "()V", NULL, NULL);
    (void)call(c, NJ_C_CANVAS, "serviceRepaints", "()V", NULL, NULL);
}

static void test_coalesced_service(void)
{
    fixture f;
    nj_ref c, g, r;
    int paints, i, p = -1, s = -1, run = -1;
    if (!begin(&f, "coalesced service preserves later drawing and callback order")) {
        return;
    }
    c = canvas(0);
    frame();
    frame();                  /* consume the initial sizeChanged repaint too */
    request_and_service_twice(c);
    /* Capture the real paint Graphics and root it before any further API call.
     * The observer's C pointer itself never serves as a Java root. */
    g = root(f.graphics);
    paints = f.paints;
    fill(g, BLUE);
    frame();
    CHECK(f.paints == paints);
    CHECK(f.presented_pixel == screen_color(BLUE));
    CHECK(f.vm->repaint_pending != 0u); /* the coalesced request was not lost */
    frame();
    CHECK(f.paints == paints + 1);
    CHECK(f.presented_pixel == screen_color(GREEN));
    CHECK(f.vm->repaint_pending == 0u);

    request_and_service_twice(c);
    paints = f.paints;
    fill(g, BLUE);
    r = runnable(1, PLAIN);
    (void)enqueue(r);
    reset_events();
    frame();
    CHECK(f.paints == paints + 1);
    CHECK(f.presented_pixel == screen_color(GREEN));
    CHECK(f.runs[1] == 1);
    for (i = 0; i < f.nevents; i++) {
        if (f.events[i] == EV_PAINT && p < 0) { p = i; }
        if (f.events[i] == EV_PRESENT && s < 0) { s = i; }
        if (f.events[i] == 1 && run < 0) { run = i; }
    }
    CHECK(p >= 0 && s > p && run > s);

    request_and_service_twice(c);
    paints = f.paints;
    f.paint_color = BLUE;
    (void)call(c, NJ_C_CANVAS, "repaint", "()V", NULL, NULL);
    frame();                  /* a fresh request must override coalescing */
    CHECK(f.paints == paints + 1);
    CHECK(f.presented_pixel == screen_color(BLUE));
    CHECK(f.vm->repaint_pending == 0u);
    CHECK(f.runs[1] == 1);
    end(&f);
}

static void test_nested_frame(void)
{
    fixture f;
    nj_ref a, b;
    if (!begin(&f, "nested frame and synchronous paint inside run")) { return; }
    (void)canvas(0);
    frame();
    a = runnable(1, NEST); b = runnable(2, PLAIN);
    (void)enqueue(a); (void)enqueue(b);
    frame();
    CHECK(f.runs[1] == 1 && f.runs[2] == 1);
    CHECK(f.max_depth == 1);
    frame();
    CHECK(f.runs[1] == 1 && f.runs[2] == 1);
    end(&f);
}

static void test_gc_receiver(void)
{
    fixture f;
    nj_u4 mark;
    nj_ref r, payload, second;
    if (!begin(&f, "GC after queue removes receiver")) { return; }
    second = runnable(2, PLAIN);
    mark = nj_root_mark(f.vm);
    r = runnable(1, COLLECT);
    payload = root(nj_array_new(f.vm, "I", 1u));
    if (r && payload) {
        nj_array_set_int(payload, 0u, 0x12345678);
        nj_field_write_ref(f.vm, r, f.payload, payload);
        (void)enqueue(r); (void)enqueue(second);
        nj_root_release(f.vm, mark); /* receiver and its payload ONLY in queue */
        frame();
        CHECK(f.runs[1] == 1 && f.runs[2] == 1);
        CHECK(f.vm->nroots == mark); /* dispatch roots must be released */
    }
    end(&f);
}

static void test_exception_tail(void)
{
    fixture f;
    nj_ref a, b;
    if (!begin(&f, "exception defers surviving tail to next frame")) { return; }
    a = runnable(1, THROW); b = runnable(2, PLAIN);
    (void)enqueue(a); (void)enqueue(b);
    frame();
    CHECK(f.runs[1] == 1 && f.runs[2] == 0);
    frame();
    CHECK(f.runs[1] == 1 && f.runs[2] == 1);
    end(&f);
}

static void test_bounded_queue(void)
{
    fixture f;
    nj_ref r;
    int i;
    if (!begin(&f, "65 enqueue requests split across frame budget")) { return; }
    r = runnable(1, PLAIN);
    for (i = 0; i < 65; i++) { (void)enqueue(r); }
    frame();
    CHECK(f.runs[1] == 64);
    frame();
    CHECK(f.runs[1] == 65);
    frame();
    CHECK(f.runs[1] == 65);
    end(&f);
}

static nj_ref result_form(void)
{
    nj_vm *vm = active->vm;
    nj_ref form = root(nj_object_new(vm, NJ_C_FORM));
    nj_value a[1], ret;
    a[0].r = root(nj_string_new(vm, "Serial result"));
    if (form) {
        CHECK(nj_call_special(vm, form, NJ_C_FORM, "<init>",
                             "(Ljava/lang/String;)V", a, NULL) == NJ_OK);
        a[0].r = root(nj_string_new(vm, "complete: 123"));
        (void)call(form, NJ_C_FORM, "append", "(Ljava/lang/String;)I", a, &ret);
    }
    return form;
}

static void test_period255_and_form(void)
{
    fixture f;
    nj_ref c, r;
    nj_value ret;
    int i;
    if (!begin(&f, "natural period255 boundary and result Form visibility")) { return; }
    c = canvas(1);
    frame();                  /* settle the initial setCurrent repaint */
    f.form = result_form();
    green_flush(c);
    frame();
    /* Quiet frames saturate idle. A subsequent app flush records that gap as
     * its cadence. All transitions use the same normal API as an application. */
    for (i = 0; i < 255; i++) { frame(); }
    CHECK(f.vm->app_paint_idle == 255u);
    flush(c);
    frame();
    CHECK(f.vm->app_paint_period == 255u);
    CHECK(f.vm->app_paint_idle == 0u);
    CHECK(f.presented_pixel == screen_color(GREEN));
    r = runnable(1, SWITCH_FORM);
    (void)enqueue(r);
    frame();
    CHECK(f.runs[1] == 1);
    /* setCurrent may take effect between event deliveries; the next frame
     * must show the Form rather than retaining the GameCanvas cadence. */
    frame();
    ret.r = NULL;
    (void)call(f.display, NJ_C_DISPLAY, "getCurrent", "()" D_DISPLAYABLE, NULL, &ret);
    CHECK(ret.r == f.form);
    ret.i = 0;
    (void)call(f.form, NJ_C_DISPLAYABLE, "isShown", "()Z", NULL, &ret);
    CHECK(ret.i != 0);
    CHECK(f.presented_pixel != screen_color(GREEN));
    CHECK(f.presented_pixel == nj_surface_get(nj_midp_screen(f.vm), 8, 8));
    CHECK(f.runs[1] == 1);
    end(&f);
}

int main(void)
{
    test_fifo_and_duplicates();
    test_flush_and_self_requeue();
    test_paint_and_present_order();
    test_enqueue_from_paint();
    test_coalesced_service();
    test_nested_frame();
    test_gc_receiver();
    test_exception_tail();
    test_bounded_queue();
    test_period255_and_form();
    printf("display_serially: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
