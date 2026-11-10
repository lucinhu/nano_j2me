/* Real Form graph roundtrip across the Screen.fmScroll layout addition.
 * Default: no JAR/filesystem fixture. Optional: --old-2a JAR STATE (read-only
 * compatibility refusal plus fresh MIDlet startup, never a resume claim). */
#include "nj2me/nj.h"
#include "nj2me/nj_app.h"
#include "nj2me/nj_gfx.h"
#include "nj_internal.h"
#include "nj_midp.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FORM "javax/microedition/lcdui/Form"
#define ITEM "javax/microedition/lcdui/Item"
#define IMAGE "javax/microedition/lcdui/Image"
#define GRAPHICS "javax/microedition/lcdui/Graphics"
#define DISPLAYABLE "javax/microedition/lcdui/Displayable"
#define LISTENER "test/FormSnapshotListener"
#define DI "Ljavax/microedition/lcdui/Image;"
#define DITEM "Ljavax/microedition/lcdui/Item;"
#define DISL "Ljavax/microedition/lcdui/ItemStateListener;"
#define TITLE "Form snapshot: scroll and references"
#define WIDTH 240
#define HEIGHT 320
#define ROWS 40
static int checks;
#define CHECK(c) do { ++checks; if (!(c)) { \
    fprintf(stderr, "FAIL Form snapshot line %d: %s\n", __LINE__, #c); exit(1); \
} } while (0)

typedef struct fixture { nj_port port; nj_vm *vm; nj_class_source source; } fixture;
/* Java 45.3 implements ItemStateListener. Public instance fields calls/last;
 * callback: ++calls; last = item. No SDK stubs, test natives or static roots. */
static const nj_u1 listener_class[] = {
    0xca, 0xfe, 0xba, 0xbe, 0x00, 0x03, 0x00, 0x2d, 0x00, 0x16, 0x01, 0x00,
    0x19, 0x74, 0x65, 0x73, 0x74, 0x2f, 0x46, 0x6f, 0x72, 0x6d, 0x53, 0x6e,
    0x61, 0x70, 0x73, 0x68, 0x6f, 0x74, 0x4c, 0x69, 0x73, 0x74, 0x65, 0x6e,
    0x65, 0x72, 0x07, 0x00, 0x01, 0x01, 0x00, 0x10, 0x6a, 0x61, 0x76, 0x61,
    0x2f, 0x6c, 0x61, 0x6e, 0x67, 0x2f, 0x4f, 0x62, 0x6a, 0x65, 0x63, 0x74,
    0x07, 0x00, 0x03, 0x01, 0x00, 0x2a, 0x6a, 0x61, 0x76, 0x61, 0x78, 0x2f,
    0x6d, 0x69, 0x63, 0x72, 0x6f, 0x65, 0x64, 0x69, 0x74, 0x69, 0x6f, 0x6e,
    0x2f, 0x6c, 0x63, 0x64, 0x75, 0x69, 0x2f, 0x49, 0x74, 0x65, 0x6d, 0x53,
    0x74, 0x61, 0x74, 0x65, 0x4c, 0x69, 0x73, 0x74, 0x65, 0x6e, 0x65, 0x72,
    0x07, 0x00, 0x05, 0x01, 0x00, 0x06, 0x3c, 0x69, 0x6e, 0x69, 0x74, 0x3e,
    0x01, 0x00, 0x03, 0x28, 0x29, 0x56, 0x0c, 0x00, 0x07, 0x00, 0x08, 0x0a,
    0x00, 0x04, 0x00, 0x09, 0x01, 0x00, 0x04, 0x43, 0x6f, 0x64, 0x65, 0x01,
    0x00, 0x05, 0x63, 0x61, 0x6c, 0x6c, 0x73, 0x01, 0x00, 0x01, 0x49, 0x0c,
    0x00, 0x0c, 0x00, 0x0d, 0x09, 0x00, 0x02, 0x00, 0x0e, 0x01, 0x00, 0x04,
    0x6c, 0x61, 0x73, 0x74, 0x01, 0x00, 0x1f, 0x4c, 0x6a, 0x61, 0x76, 0x61,
    0x78, 0x2f, 0x6d, 0x69, 0x63, 0x72, 0x6f, 0x65, 0x64, 0x69, 0x74, 0x69,
    0x6f, 0x6e, 0x2f, 0x6c, 0x63, 0x64, 0x75, 0x69, 0x2f, 0x49, 0x74, 0x65,
    0x6d, 0x3b, 0x0c, 0x00, 0x10, 0x00, 0x11, 0x09, 0x00, 0x02, 0x00, 0x12,
    0x01, 0x00, 0x10, 0x69, 0x74, 0x65, 0x6d, 0x53, 0x74, 0x61, 0x74, 0x65,
    0x43, 0x68, 0x61, 0x6e, 0x67, 0x65, 0x64, 0x01, 0x00, 0x22, 0x28, 0x4c,
    0x6a, 0x61, 0x76, 0x61, 0x78, 0x2f, 0x6d, 0x69, 0x63, 0x72, 0x6f, 0x65,
    0x64, 0x69, 0x74, 0x69, 0x6f, 0x6e, 0x2f, 0x6c, 0x63, 0x64, 0x75, 0x69,
    0x2f, 0x49, 0x74, 0x65, 0x6d, 0x3b, 0x29, 0x56, 0x00, 0x21, 0x00, 0x02,
    0x00, 0x04, 0x00, 0x01, 0x00, 0x06, 0x00, 0x02, 0x00, 0x01, 0x00, 0x0c,
    0x00, 0x0d, 0x00, 0x00, 0x00, 0x01, 0x00, 0x10, 0x00, 0x11, 0x00, 0x00,
    0x00, 0x02, 0x00, 0x01, 0x00, 0x07, 0x00, 0x08, 0x00, 0x01, 0x00, 0x0b,
    0x00, 0x00, 0x00, 0x11, 0x00, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00, 0x05,
    0x2a, 0xb7, 0x00, 0x0a, 0xb1, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00,
    0x14, 0x00, 0x15, 0x00, 0x01, 0x00, 0x0b, 0x00, 0x00, 0x00, 0x1c, 0x00,
    0x03, 0x00, 0x02, 0x00, 0x00, 0x00, 0x10, 0x2a, 0x59, 0xb4, 0x00, 0x0f,
    0x04, 0x60, 0xb5, 0x00, 0x0f, 0x2a, 0x2b, 0xb5, 0x00, 0x13, 0xb1, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00,
};

/* The snapshot loader requires an attached class source even when classes
 * are already loaded. Both VMs serve the exact same Java listener bytes. */
static nj_status load_listener(nj_vm *vm, void *ud, const char *name,
                               nj_u1 **out_bytes, nj_u4 *out_len)
{
    (void)ud; *out_bytes = NULL; *out_len = 0u;
    if (strcmp(name, LISTENER) != 0) return NJ_ERR;
    *out_bytes = (nj_u1 *)nj_host_alloc(vm, (nj_size)sizeof(listener_class));
    if (!*out_bytes) return NJ_ERR_NOMEM;
    memcpy(*out_bytes, listener_class, sizeof(listener_class));
    *out_len = (nj_u4)sizeof(listener_class); return NJ_OK;
}
static void create(fixture *f, nj_size heap)
{
    nj_class *c;
    CHECK(nj_port_headless(&f->port) == NJ_OK);
    f->port.width = WIDTH; f->port.height = HEIGHT;
    f->vm = nj_vm_create(&f->port, heap); CHECK(f->vm != NULL);
    f->source.load = load_listener; f->source.ud = NULL;
    f->vm->source = &f->source;
    c = nj_class_load(f->vm, LISTENER, listener_class, (nj_u4)sizeof(listener_class));
    CHECK(c != NULL && nj_class_link(f->vm, c) == NJ_OK);
    CHECK(nj_class_is_assignable(c, nj_class_lookup(f->vm,
        "javax/microedition/lcdui/ItemStateListener")));
}
static nj_ref root(nj_vm *vm, nj_ref r)
{
    nj_u4 before = nj_root_mark(vm);
    CHECK(r != NULL); (void)nj_root(vm, r);
    CHECK(nj_root_mark(vm) == before + 1u); return r;
}
static nj_value call(nj_vm *vm, nj_ref obj, const char *cls,
                     const char *name, const char *desc, nj_value *a)
{
    nj_value r; memset(&r, 0, sizeof(r));
    CHECK(nj_call(vm, obj, cls, name, desc, a, &r) == NJ_OK);
    CHECK(!nj_has_exception(vm)); return r;
}
static nj_value field(nj_vm *vm, nj_ref obj, const char *cls,
                      const char *name, const char *desc)
{
    nj_value r; nj_field *f = nj_field_lookup(vm, cls, name, desc, 0);
    CHECK(obj && f); memset(&r, 0, sizeof(r));
    if (desc[0] == 'I') r.i = nj_field_read_int(vm, obj, f);
    else r.r = nj_field_read_ref(vm, obj, f);
    return r;
}
static nj_s4 integer(nj_vm *vm, nj_ref form, const char *name)
{ return field(vm, form, FORM, name, "I").i; }
static nj_ref image(nj_vm *vm, int w, int h, int rgb)
{
    nj_value a[4], r; nj_ref im, g; nj_u4 mark;
    memset(a, 0, sizeof(a)); a[0].i = w; a[1].i = h;
    CHECK(nj_call_static(vm, IMAGE, "createImage", "(II)" DI, a, &r) == NJ_OK);
    im = root(vm, r.r); mark = nj_root_mark(vm);
    g = root(vm, call(vm, im, IMAGE, "getGraphics",
                     "()Ljavax/microedition/lcdui/Graphics;", NULL).r);
    a[0].i = rgb; (void)call(vm, g, GRAPHICS, "setColor", "(I)V", a);
    a[0].i = 0; a[1].i = 0; a[2].i = w; a[3].i = h;
    (void)call(vm, g, GRAPHICS, "fillRect", "(IIII)V", a);
    nj_root_release(vm, mark); return im;
}
static nj_ref get_item(nj_vm *vm, nj_ref form, int index)
{
    nj_value a; a.i = index;
    return call(vm, form, FORM, "get", "(I)" DITEM, &a).r;
}
static void key(nj_vm *vm, int code)
{
    CHECK(nj_midp_key(vm, code, 1));
    (void)nj_midp_key(vm, code, 0);
    CHECK(nj_midp_paint(vm) == NJ_OK && !nj_has_exception(vm));
}
static nj_surface *paint(nj_vm *vm)
{
    nj_surface *s;
    nj_midp_repaint(vm); CHECK(nj_midp_paint(vm) == NJ_OK);
    s = nj_midp_screen(vm); CHECK(s && s->width == WIDTH && s->height == HEIGHT);
    CHECK(!nj_has_exception(vm)); return s;
}
static nj_u4 pixels(nj_surface *s)
{
    nj_u4 h = 2166136261u; int x, y;
    for (y = 0; y < s->height; ++y)
        for (x = 0; x < s->width; ++x) h = (h ^ nj_surface_get(s, x, y)) * 16777619u;
    return h;
}
/* Invoke the real Java listener synchronously to test the saved object graph.
 * Item.notifyStateChanged may defer delivery and requires a user value edit;
 * selecting an ImageItem is not such an edit. */
static void invoke_listener(nj_vm *vm, nj_ref form, int index, int count)
{
    nj_value a;
    nj_ref item = get_item(vm, form, index);
    nj_ref listener = field(vm, form, FORM, "fmItemListener", DISL).r;
    CHECK(listener && strcmp(listener->cls->name, LISTENER) == 0);
    a.r = item;
    (void)call(vm, listener, LISTENER, "itemStateChanged", "(" DITEM ")V", &a);
    CHECK(field(vm, listener, LISTENER, "calls", "I").i == count);
    CHECK(field(vm, listener, LISTENER, "last", DITEM).r == item);
}
static nj_ref build_form(nj_vm *vm)
{
    nj_u4 base = nj_root_mark(vm); nj_ref form, title, blue, red, buffer, listener;
    nj_value a[4]; int i;
    memset(a, 0, sizeof(a));
    form = root(vm, nj_object_new(vm, FORM));
    title = root(vm, nj_string_new(vm, TITLE)); a[0].r = title;
    (void)call(vm, form, FORM, "<init>", "(Ljava/lang/String;)V", a);
    blue = image(vm, 3, 3, 0x0000ff); red = image(vm, 3, 3, 0xff0000);
    for (i = 0; i < ROWS; ++i) {
        nj_u4 mark = nj_root_mark(vm);
        nj_ref item = root(vm, nj_object_new(vm, "javax/microedition/lcdui/ImageItem"));
        memset(a, 0, sizeof(a)); a[1].r = i == ROWS - 1 ? red : blue;
        /* Use explicit alternate text: null acceptance is a separate ImageItem
         * compatibility defect, not the snapshot behavior exercised here. */
        a[3].r = title;
        (void)call(vm, item, "javax/microedition/lcdui/ImageItem", "<init>",
                   "(Ljava/lang/String;" DI "ILjava/lang/String;)V", a);
        a[0].i = 234; a[1].i = 32;
        (void)call(vm, item, ITEM, "setPreferredSize", "(II)V", a);
        a[0].r = item; CHECK(call(vm, form, FORM, "append", "(" DITEM ")I", a).i == i);
        nj_root_release(vm, mark);
    }
    listener = root(vm, nj_object_new(vm, LISTENER));
    (void)call(vm, listener, LISTENER, "<init>", "()V", NULL);
    a[0].r = listener;
    (void)call(vm, form, FORM, "setItemStateListener", "(" DISL ")V", a);
    /* fmBuffer neighbours the added int. Attach only after append: legacy Form
     * append treats a non-null fmBuffer as a Graphics append target. */
    buffer = image(vm, 5, 7, 0x19a7e3);
    { nj_field *f = nj_field_lookup(vm, FORM, "fmBuffer", DI, 0);
      CHECK(f != NULL); nj_field_write_ref(vm, form, f, buffer); }
    CHECK(nj_midp_set_current(vm, form) == NJ_OK); (void)paint(vm);
    for (i = 0; i < ROWS + 5; ++i) key(vm, NJ_KEY_DOWN);
    (void)paint(vm); CHECK(integer(vm, form, "fmIndex") == ROWS - 1);
    CHECK(integer(vm, form, "fmScroll") > 0); invoke_listener(vm, form, ROWS - 1, 1);
    nj_root_release(vm, base); CHECK(nj_root_mark(vm) == base && base == 0u);
    /* Raw C locals are not roots: the Form, listener and image survive solely
     * through Display/current_displayable and their normal object fields. */
    (void)nj_gc_collect(vm); CHECK(nj_midp_current(vm) == form);
    return form;
}
static void graph(nj_vm *vm, nj_ref form, int index, int scroll)
{
    nj_value r; nj_ref listener, buffer, item, data;
    CHECK(form && strcmp(form->cls->name, FORM) == 0);
    CHECK(integer(vm, form, "fmIndex") == index && integer(vm, form, "fmScroll") == scroll);
    CHECK(integer(vm, form, "fmCount") == ROWS);
    r = call(vm, form, DISPLAYABLE, "getTitle", "()Ljava/lang/String;", NULL);
    CHECK(nj_string_equals_utf8(r.r, TITLE));
    listener = field(vm, form, FORM, "fmItemListener", DISL).r;
    buffer = field(vm, form, FORM, "fmBuffer", DI).r;
    CHECK(listener && strcmp(listener->cls->name, LISTENER) == 0);
    CHECK(buffer && strcmp(buffer->cls->name, IMAGE) == 0 && buffer != listener);
    CHECK(field(vm, listener, LISTENER, "calls", "I").i == 1);
    item = get_item(vm, form, ROWS - 1);
    CHECK(field(vm, listener, LISTENER, "last", DITEM).r == item);
    CHECK(field(vm, item, ITEM, "itOwner", "Ljavax/microedition/lcdui/Form;").r == form);
    CHECK(call(vm, buffer, IMAGE, "getWidth", "()I", NULL).i == 5);
    CHECK(call(vm, buffer, IMAGE, "getHeight", "()I", NULL).i == 7);
    /* Image's ordinary reference array is also visited by snapshot fixups/GC. */
    data = field(vm, buffer, IMAGE, "imgPixels", "[I").r;
    CHECK(data && nj_array_length(data) == 35u);
    CHECK((((nj_u4 *)nj_array_data(data))[0] & 0xffffffu) == 0x19a7e3u);
}
static void roundtrip(void)
{
    fixture source, target; nj_vm *a, *b; nj_ref form; FILE *f; long saved_bytes;
    nj_s4 scroll, index; nj_u4 before; nj_surface *s; int x, y, found = 0, i;
    create(&source, 2u * 1024u * 1024u); create(&target, 2u * 1024u * 1024u);
    a = source.vm; b = target.vm; form = build_form(a);
    scroll = integer(a, form, "fmScroll"); index = integer(a, form, "fmIndex");
    graph(a, form, index, scroll); before = pixels(paint(a));
    CHECK(nj_root_mark(a) == 0u && nj_vm_can_save(a));
    f = tmpfile(); CHECK(f != NULL); CHECK(nj_vm_save(a, f) == NJ_OK);
    /* The format stores heap.bump plus class/reference metadata; it has no
     * one-MiB file limit, even for this two-MiB VM. */
    saved_bytes = ftell(f);
    CHECK(saved_bytes > 0L && (nj_u8)saved_bytes >= (nj_u8)a->heap.bump);
    printf("Form snapshot serialized bytes: %ld\n", saved_bytes);
    CHECK(fseek(f, 0L, SEEK_SET) == 0); CHECK(nj_vm_load(b, f) == NJ_OK);
    CHECK(fclose(f) == 0); form = nj_midp_current(b);
    CHECK(nj_root_mark(b) == 0u); graph(b, form, index, scroll);
    (void)nj_gc_collect(b); CHECK(nj_midp_current(b) == form);
    graph(b, form, index, scroll); s = paint(b); CHECK(pixels(s) == before);
    /* Tap a visible blue row above the selected final red row. Same geometry
     * as test_form_scroll: title 21, row preferred height 32 + padding 2. */
    for (y = 22; y < HEIGHT - 20 && !found; ++y)
        for (x = 1; x < WIDTH - 8; ++x)
            if ((nj_surface_get(s, x, y) & 0xffffffu) == 0x0000ffu) {
                int expected = (y - 21 + scroll) / 34;
                CHECK(expected >= 0 && expected < ROWS - 1);
                CHECK(nj_midp_pointer(b, x, y, NJ_POINTER_DOWN));
                CHECK(integer(b, form, "fmIndex") == expected); found = 1; break;
            }
    CHECK(found); invoke_listener(b, form, integer(b, form, "fmIndex"), 2);
    for (i = 0; i < ROWS + 5; ++i) key(b, NJ_KEY_UP);
    CHECK(integer(b, form, "fmIndex") == 0 && integer(b, form, "fmScroll") == 0);
    (void)nj_gc_collect(b); CHECK(pixels(paint(b)) != before);
    CHECK(field(b, form, FORM, "fmItemListener", DISL).r != NULL);
    CHECK(field(b, form, FORM, "fmBuffer", DI).r != NULL);
    nj_vm_destroy(b); nj_vm_destroy(a);
    nj_port_shutdown(&target.port); nj_port_shutdown(&source.port);
}
static nj_u4 file_hash(FILE *f)
{
    unsigned char buf[4096]; size_t n, i; nj_u4 h = 2166136261u;
    CHECK(fseek(f, 0L, SEEK_SET) == 0);
    while ((n = fread(buf, 1u, sizeof(buf), f)) != 0u)
        for (i = 0u; i < n; ++i) h = (h ^ buf[i]) * 16777619u;
    CHECK(!ferror(f)); CHECK(fseek(f, 0L, SEEK_SET) == 0); return h;
}
static void old_refusal(const char *jar_path, const char *state_path)
{
    fixture target; nj_vm *vm; nj_app *app; nj_ref form, display, midlet;
    nj_thread *thread; nj_u1 *arena; nj_u4 roots, hash, jar_hash;
    nj_s4 scroll; FILE *jar, *state; unsigned char *bytes; long size; int rc;
    jar = fopen(jar_path, "rb"); state = fopen(state_path, "rb"); CHECK(jar && state);
    jar_hash = file_hash(jar); hash = file_hash(state);
    CHECK(fseek(jar, 0L, SEEK_END) == 0); size = ftell(jar);
    CHECK(size > 0L && (unsigned long)size <= 0xffffffffUL);
    CHECK(fseek(jar, 0L, SEEK_SET) == 0); bytes = (unsigned char *)malloc((size_t)size);
    CHECK(bytes != NULL && fread(bytes, 1u, (size_t)size, jar) == (size_t)size);
    create(&target, 16u * 1024u * 1024u); vm = target.vm;
    app = nj_app_open(vm, bytes, (nj_u4)size); CHECK(app != NULL);
    form = build_form(vm); scroll = integer(vm, form, "fmScroll");
    arena = vm->heap.base; display = vm->display; thread = vm->cur; roots = nj_root_mark(vm);
    rc = nj_vm_load(vm, state); CHECK(rc == NJ_ERR_FORMAT);
    /* Resolving classes may allocate metadata/strings before a refusal. Do not
     * demand unchanged heap.bump/whole-arena bytes; installation is forbidden. */
    CHECK(vm->heap.base == arena && vm->display == display && vm->cur == thread);
    CHECK(nj_midp_current(vm) == form && nj_root_mark(vm) == roots);
    graph(vm, form, ROWS - 1, scroll); (void)nj_gc_collect(vm); (void)paint(vm);
    CHECK(file_hash(state) == hash && file_hash(jar) == jar_hash);
    /* Only after asserting refusal/target integrity, prove a fresh MIDlet can
     * start on the same VM. This is deliberately not a CLI resume assertion. */
    CHECK(nj_app_midlet_class(app, 0) != NULL);
    midlet = root(vm, nj_vm_new_midlet(vm, nj_app_midlet_class(app, 0), NULL));
    CHECK(nj_vm_start_app(vm, midlet) == NJ_OK); CHECK(!nj_has_exception(vm));
    nj_root_release(vm, roots); nj_app_close(vm, app); nj_vm_destroy(vm);
    nj_port_shutdown(&target.port); free(bytes);
    CHECK(fclose(state) == 0 && fclose(jar) == 0);
    printf("old 2a snapshot refused before arena install; fresh MIDlet startup succeeded\n");
}
int main(int argc, char **argv)
{
    if (argc != 1 && !(argc == 4 && strcmp(argv[1], "--old-2a") == 0)) {
        fprintf(stderr, "usage: %s [--old-2a ORIGINAL.jar ORIGINAL.state]\n", argv[0]); return 2;
    }
    if (argc == 1) roundtrip(); else old_refusal(argv[2], argv[3]);
    printf("Form snapshot: %d checks passed (real listener, GC, paint, key, pointer)\n", checks);
    return 0;
}
