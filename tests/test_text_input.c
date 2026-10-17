/* Native IME contract regression: UTF-8 commits, UTF-16 replacement,
 * constraints, expired focus, caret/Unicode and SDL key suppression. */
#include "nj2me/nj.h"
#include "../src/midp/nj_midp.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef NJ_HAVE_SDL2
#include <SDL.h>
#endif

static int failures, checks;
#define CHECK(x) do { ++checks; if (!(x)) { ++failures; fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x); } } while (0)
static nj_text_input_state snapshot;
static nj_u2 snapshot_text[512];
static void capture(void *ud, const nj_text_input_state *state)
{
    (void)ud;
    snapshot = *state;
    if (state->length <= 512 && state->length) {
        memcpy(snapshot_text, state->text, (size_t)state->length * sizeof(nj_u2));
    }
    snapshot.text = snapshot_text;
}
static nj_ref make(nj_vm *vm, const char *cls, const char *desc, nj_value *args)
{
    nj_ref ref = nj_object_new(vm, cls);
    CHECK(ref != NULL);
    if (!ref) { return NULL; }
    nj_root(vm, ref);
    CHECK(nj_call_special(vm, ref, cls, "<init>", desc, args, NULL) == NJ_OK);
    return ref;
}
static nj_ref string(nj_vm *vm, const char *text)
{
    nj_ref ref = nj_string_new(vm, text);
    nj_root(vm, ref);
    return ref;
}
static nj_ref widget(nj_vm *vm, const char *cls, int max, int constraints, const char *text)
{
    nj_value args[4];
    args[0].r = string(vm, "IME"); args[1].r = string(vm, text);
    args[2].i = max; args[3].i = constraints;
    return make(vm, cls, "(Ljava/lang/String;Ljava/lang/String;II)V", args);
}
static void current(nj_vm *vm, nj_ref ref)
{
    CHECK(nj_midp_set_current(vm, ref) == NJ_OK);
    nj_midp_text_input_sync(vm);
}
static void expect(nj_vm *vm, nj_ref ref, const char *cls, const nj_u2 *text, int len)
{
    nj_value ret;
    CHECK(nj_call(vm, ref, cls, "getString", "()Ljava/lang/String;", NULL, &ret) == NJ_OK);
    CHECK(ret.r && (int)nj_string_len(ret.r) == len);
    CHECK(ret.r && (!len || !memcmp(nj_string_chars(ret.r), text, (size_t)len * sizeof(nj_u2))));
    CHECK(vm->exception == NULL);
}
static int commit(nj_vm *vm, const char *text, nj_u4 session)
{
    nj_event event;
    memset(&event, 0, sizeof(event));
    event.type = NJ_EV_TEXT_INPUT;
    event.text_session = session;
    CHECK(strlen(text) < sizeof(event.text));
    memcpy(event.text, text, strlen(text) + 1);
    return nj_midp_text_event(vm, &event);
}
static int replace(nj_vm *vm, const nj_u2 *text, int len, int cursor, nj_u4 seq, nj_u4 session)
{
    nj_event event;
    memset(&event, 0, sizeof(event));
    event.type = NJ_EV_TEXT_REPLACE;
    event.text_session = session; event.text_sequence = seq;
    event.text_utf16 = text; event.text_length = len; event.text_cursor = cursor;
    return nj_midp_text_event(vm, &event);
}
static void constraint(nj_vm *vm, nj_ref ref, int value)
{
    nj_value arg;
    arg.i = value;
    CHECK(nj_call(vm, ref, NJ_C_TEXTBOX, "setConstraints", "(I)V", &arg, NULL) == NJ_OK);
    nj_midp_text_input_sync(vm);
}
static void runtime_test(void)
{
    nj_port port;
    nj_vm *vm;
    nj_ref box, other, form, field, field2;
    nj_u4 session;
    nj_value args[2];
    const nj_u2 chinese[] = {0x4e2d, 0x6587, 0xd83d, 0xde00};
    const nj_u2 middle[] = {0x4e2d, 0x6587, 'A', 0xd83d, 0xde00};
    const nj_u2 modified[] = {0x65b0, 0x6587};
    nj_u2 long_text[42];
    int i;
    CHECK(nj_port_headless(&port) == NJ_OK);
    port.text_input = capture;
    vm = nj_vm_create(&port, 4u * 1024u * 1024u);
    CHECK(vm != NULL); if (!vm) { return; }
    box = widget(vm, NJ_C_TEXTBOX, 128, 0, "");
    other = widget(vm, NJ_C_TEXTBOX, 4, 0, "old");
    current(vm, box);
    CHECK(snapshot.active && snapshot.multiline && snapshot.max_length == 128);
    session = snapshot.session;
    CHECK(session != 0);
    CHECK(commit(vm, "\xe4\xb8\xad\xe6\x96\x87\xf0\x9f\x98\x80", session));
    expect(vm, box, NJ_C_TEXTBOX, chinese, 4);
    CHECK(nj_midp_text_edit(vm, NJ_TEXT_LEFT));
    CHECK(commit(vm, "A", session));
    expect(vm, box, NJ_C_TEXTBOX, middle, 5);
    CHECK(nj_midp_text_edit(vm, NJ_TEXT_DELETE));
    expect(vm, box, NJ_C_TEXTBOX, middle, 3);
    CHECK(!commit(vm, "\xe4\xb8", session)); /* truncated CJK */
    CHECK(!commit(vm, "\xc0\xaf", session)); /* overlong */
    CHECK(!commit(vm, "\xed\xa0\x80", session)); /* UTF8 surrogate */
    CHECK(!commit(vm, "\xf4\x90\x80\x80", session)); /* > U+10ffff */
    expect(vm, box, NJ_C_TEXTBOX, middle, 3);

    /* Android complete snapshots have no 31-byte limit. */
    for (i = 0; i < 40; i++) { long_text[i] = (nj_u2)(0x4e00 + i); }
    long_text[40] = 0xd83d; long_text[41] = 0xde00;
    CHECK(replace(vm, long_text, 42, 42, 1, session));
    expect(vm, box, NJ_C_TEXTBOX, long_text, 42);
    CHECK(!replace(vm, chinese, 4, 4, 1, session)); /* duplicate sequence */
    CHECK(replace(vm, modified, 2, 1, 2, session)); /* replace existing and caret */
    expect(vm, box, NJ_C_TEXTBOX, modified, 2);
    nj_midp_text_input_sync(vm);
    CHECK(snapshot.cursor == 1 && snapshot.sequence == 2);
    CHECK(replace(vm, NULL, 0, 0, 3, session)); /* delete all */
    expect(vm, box, NJ_C_TEXTBOX, NULL, 0);
    CHECK(!replace(vm, chinese, 4, 3, 4, session)); /* split surrogate caret */
    nj_midp_text_input_sync(vm);
    CHECK(snapshot.sequence == 4 && snapshot.length == 0);
    CHECK(!replace(vm, chinese + 3, 1, 1, 5, session)); /* orphan low surrogate */

    current(vm, other);
    CHECK(snapshot.session != session && snapshot.length == 3);
    CHECK(!commit(vm, "X", session));
    CHECK(!replace(vm, modified, 2, 2, 6, session));
    CHECK(nj_midp_text_edit(vm, NJ_TEXT_HOME));
    CHECK(!commit(vm, "\xf0\x9f\x98\x80", snapshot.session)); /* 3+2 > max4 */
    CHECK(commit(vm, "\xe4\xb8\xad", snapshot.session));
    CHECK(!commit(vm, "x", snapshot.session));
    CHECK(nj_midp_text_edit(vm, NJ_TEXT_DELETE));
    CHECK(nj_midp_text_edit(vm, NJ_TEXT_BACKSPACE));

    current(vm, box);
    constraint(vm, box, 0x10000); /* PASSWORD retains actual Unicode value */
    CHECK(snapshot.active && (snapshot.constraints & 0x10000));
    CHECK(commit(vm, "\xe4\xb8\xad\xe6\x96\x87\xf0\x9f\x98\x80", snapshot.session));
    expect(vm, box, NJ_C_TEXTBOX, chinese, 4);
    CHECK(replace(vm, NULL, 0, 0, 1, snapshot.session));
    session = snapshot.session;
    constraint(vm, box, 0x20000); /* UNEDITABLE retires native editor */
    CHECK(!snapshot.active && snapshot.session != session);
    CHECK(!commit(vm, "x", session));
    constraint(vm, box, 2);
    CHECK(commit(vm, "123", snapshot.session));
    CHECK(!commit(vm, "\xe4\xb8\xad", snapshot.session));
    CHECK(!commit(vm, "a", snapshot.session));
    CHECK(replace(vm, NULL, 0, 0, 1, snapshot.session));
    constraint(vm, box, 5);
    CHECK(commit(vm, "-12.5", snapshot.session));
    CHECK(!commit(vm, ".", snapshot.session));
    CHECK(!commit(vm, "-", snapshot.session));
    CHECK(replace(vm, NULL, 0, 0, 1, snapshot.session));
    constraint(vm, box, 0);
    CHECK(commit(vm, "a\nb", snapshot.session));

    args[0].r = string(vm, "Form");
    form = make(vm, NJ_C_FORM, "(Ljava/lang/String;)V", args);
    field = widget(vm, NJ_C_TEXTFIELD, 64, 0, "");
    field2 = widget(vm, NJ_C_TEXTFIELD, 64, 0, "");
    args[0].r = field;
    CHECK(nj_call(vm, form, NJ_C_FORM, "append", "(Ljavax/microedition/lcdui/Item;)I", args, NULL) == NJ_OK);
    args[0].r = field2;
    CHECK(nj_call(vm, form, NJ_C_FORM, "append", "(Ljavax/microedition/lcdui/Item;)I", args, NULL) == NJ_OK);
    current(vm, form);
    CHECK(snapshot.active && !snapshot.multiline);
    session = snapshot.session;
    CHECK(commit(vm, "\xe4\xb8\xad", session));
    CHECK(nj_midp_key(vm, NJ_KEY_DOWN, 1));
    nj_midp_text_input_sync(vm);
    CHECK(snapshot.session != session);
    CHECK(!commit(vm, "X", session));
    expect(vm, field, NJ_C_TEXTFIELD, chinese, 1);
    CHECK(replace(vm, modified, 2, 1, 1, snapshot.session));
    expect(vm, field2, NJ_C_TEXTFIELD, modified, 2);
    nj_gc_collect(vm);
    nj_midp_text_input_sync(vm);
    CHECK(snapshot.length == 2 && snapshot.text[0] == 0x65b0);
    CHECK(vm->exception == NULL);
    /* Registration forms often begin with instructions, not an editor. A
     * tap must select the actual row, and tapping it again must reopen an
     * IME dismissed with Back rather than trigger the Form's default command. */
    args[0].r = string(vm, "Register");
    form = make(vm, NJ_C_FORM, "(Ljava/lang/String;)V", args);
    args[0].r = string(vm, ""); args[1].r = string(vm, "Instructions");
    other = make(vm, NJ_C_STRINGITEM, "(Ljava/lang/String;Ljava/lang/String;)V", args);
    field = widget(vm, NJ_C_TEXTFIELD, 64, 0, "user");
    field2 = widget(vm, NJ_C_TEXTFIELD, 64, 0x10000, "secret");
    args[0].r = other;
    CHECK(nj_call(vm, form, NJ_C_FORM, "append", "(Ljavax/microedition/lcdui/Item;)I", args, NULL) == NJ_OK);
    args[0].r = field;
    CHECK(nj_call(vm, form, NJ_C_FORM, "append", "(Ljavax/microedition/lcdui/Item;)I", args, NULL) == NJ_OK);
    args[0].r = field2;
    CHECK(nj_call(vm, form, NJ_C_FORM, "append", "(Ljavax/microedition/lcdui/Item;)I", args, NULL) == NJ_OK);
    current(vm, form);
    CHECK(!snapshot.active);
    CHECK(nj_midp_paint(vm) == NJ_OK);
    {
        int y, user_y = -1, pass_y = -1;
        /* Locate drawn rows without coupling the test to font metrics. */
        for (y = 21; y < 200; y++) {
            (void)nj_midp_pointer(vm, 12, y, NJ_POINTER_DOWN);
            if (snapshot.active && snapshot.length == 4 && user_y < 0) { user_y = y; }
            if (snapshot.active && snapshot.constraints == 0x10000u) { pass_y = y; break; }
        }
        CHECK(user_y >= 0 && pass_y > user_y);
        session = snapshot.session;
        CHECK(nj_midp_pointer(vm, 12, pass_y, NJ_POINTER_DOWN));
        CHECK(snapshot.active && snapshot.constraints == 0x10000u && snapshot.session != session);
        CHECK(!commit(vm, "X", session));
        session = snapshot.session;
        CHECK(!nj_midp_pointer(vm, 12, user_y, NJ_POINTER_MOVE));
        CHECK(!nj_midp_pointer(vm, 12, user_y, NJ_POINTER_UP));
        CHECK(snapshot.session == session);
        CHECK(nj_midp_pointer(vm, 12, user_y, NJ_POINTER_DOWN));
        CHECK(snapshot.active && snapshot.constraints == 0 && snapshot.length == 4);
        CHECK(commit(vm, "\xe4\xb8\xad", snapshot.session));
        CHECK(!nj_midp_pointer(vm, 12, 0, NJ_POINTER_DOWN));
        CHECK(!nj_midp_pointer(vm, -1, user_y, NJ_POINTER_DOWN));
        CHECK(nj_midp_current(vm) == form);
    }
    CHECK(vm->exception == NULL);
    nj_vm_destroy(vm);
    nj_port_shutdown(&port);
}
#ifdef NJ_HAVE_SDL2
static void push_sdl(SDL_Event *event)
{
    /* SDL2-compat requires a real window for text events (ID 0 crashes in
     * SDL3's event translator). The test creates exactly one SDL window. */
    SDL_Window *window = SDL_GetKeyboardFocus();
    unsigned id;
    for (id = 1; !window && id < 65536; id++) { window = SDL_GetWindowFromID(id); }
    CHECK(window != NULL);
    if (!window) { return; }
    event->window.windowID = SDL_GetWindowID(window);
    CHECK(SDL_PeepEvents(event, 1, SDL_ADDEVENT, 0, 0) == 1);
}
static void sdl_test(void)
{
    nj_port port;
    nj_text_input_state state;
    nj_event input;
    SDL_Event event;
    SDL_version version;
    int compat, commits = 0, keys = 0, edits = 0, units = 0, seen = 0, i;
    SDL_GetVersion(&version);
    compat = version.major == 2 && version.minor == 32 && version.patch >= 50;
    SDL_setenv("SDL_VIDEODRIVER", "dummy", 1);
    SDL_setenv("SDL_AUDIODRIVER", "dummy", 1);
    CHECK(nj_port_sdl2(&port, "IME regression") == NJ_OK);
    if (!port.text_input) { CHECK(0); return; }
    {
        int width = 240, height = 320, stride = 0;
        void *pixels = NULL;
        CHECK(port.display_init(port.ud, &width, &height, &stride, &pixels) >= 0);
        CHECK(pixels != NULL && width > 0 && height > 0);
    }
    memset(&state, 0, sizeof(state)); state.active = 1; state.session = 7;
    port.text_input(port.ud, &state);
    CHECK(SDL_IsTextInputActive());
    if (compat) {
        fprintf(stderr, "SDL2-compat %u.%u.%u: synthetic text event replay skipped (SDL3 translator crash); activation/deactivation tested\n", version.major, version.minor, version.patch);
        state.active = 0; port.text_input(port.ud, &state);
        CHECK(!SDL_IsTextInputActive());
        nj_port_shutdown(&port);
        return;
    }
    memset(&event, 0, sizeof(event)); event.type = SDL_TEXTEDITING;
    strcpy(event.edit.text, "zhong"); push_sdl(&event);
    event.type = SDL_KEYDOWN; event.key.keysym.sym = SDLK_BACKSPACE;
    event.key.keysym.scancode = SDL_SCANCODE_BACKSPACE; push_sdl(&event);
    event.type = SDL_KEYUP; push_sdl(&event);
    event.type = SDL_KEYDOWN; event.key.keysym.sym = SDLK_a;
    event.key.keysym.scancode = SDL_SCANCODE_A; push_sdl(&event);
    event.type = SDL_KEYUP; push_sdl(&event);
    /* SDL backend splits longer IME commits into bounded character-aligned
     * events. Replay a 40-byte commit containing CJK and a supplementary pair. */
    memset(&event, 0, sizeof(event)); event.type = SDL_TEXTINPUT;
    for (i = 0; i < 10; i++) { memcpy(event.text.text + i * 3, "\xe4\xb8\xad", 3); }
    push_sdl(&event);
    memset(&event, 0, sizeof(event)); event.type = SDL_TEXTINPUT;
    strcpy(event.text.text, "\xe6\x96\x87\xe6\x96\x87\xf0\x9f\x98\x80"); push_sdl(&event);
    event.type = SDL_KEYDOWN; event.key.keysym.sym = SDLK_BACKSPACE;
    event.key.keysym.scancode = SDL_SCANCODE_BACKSPACE; push_sdl(&event);
    event.type = SDL_KEYUP; push_sdl(&event);
    port.display_present(port.ud, 0, 0, 240, 320);
    while (port.input_poll(port.ud, &input)) {
        seen++;
        if (input.type == NJ_EV_TEXT_INPUT) {
            commits++; CHECK(input.text_session == 7);
            if (commits == 1) { CHECK(strlen(input.text) == 30); units += 10; }
            else { CHECK(strlen(input.text) == 10); units += 4; }
        } else if (input.type == NJ_EV_TEXT_EDIT) { edits++; }
        else if (input.type == NJ_EV_KEY_DOWN || input.type == NJ_EV_KEY_UP) { keys++; }
    }
    CHECK(seen == 3 && commits == 2 && units == 14 && edits == 1 && keys == 0);
    event.type = SDL_TEXTEDITING; strcpy(event.edit.text, "old"); push_sdl(&event);
    state.session = 8; port.text_input(port.ud, &state); /* pending old SDL event flushed */
    port.display_present(port.ud, 0, 0, 240, 320);
    CHECK(!port.input_poll(port.ud, &input));
    state.active = 0; port.text_input(port.ud, &state);
    CHECK(!SDL_IsTextInputActive());
    nj_port_shutdown(&port);
}
#endif
static void item_command_test(void)
{
    nj_port port;
    nj_vm *vm;
    nj_ref field, item;
    nj_value args[3], ret;
    int types[] = {1, 2, 8}, i;
    CHECK(nj_port_headless(&port) == NJ_OK);
    vm = nj_vm_create(&port, 4u * 1024u * 1024u);
    CHECK(vm != NULL); if (!vm) { return; }
    field = widget(vm, NJ_C_TEXTFIELD, 64, 0, "");
    args[0].r = string(vm, "label"); args[1].r = string(vm, "text");
    item = make(vm, NJ_C_STRINGITEM, "(Ljava/lang/String;Ljava/lang/String;)V", args);
    for (i = 0; i < 3; i++) {
        nj_ref command;
        args[0].r = string(vm, "command"); args[1].i = types[i]; args[2].i = 4;
        command = make(vm, NJ_C_COMMAND, "(Ljava/lang/String;II)V", args);
        args[0].r = command;
        CHECK(nj_call(vm, field, NJ_C_ITEM, "addCommand", "(Ljavax/microedition/lcdui/Command;)V", args, NULL) == NJ_OK);
        CHECK(nj_call(vm, item, NJ_C_STRINGITEM, "addCommand", "(Ljavax/microedition/lcdui/Command;)V", args, NULL) == NJ_OK);
        CHECK(nj_call(vm, field, NJ_C_ITEM, "setDefaultCommand", "(Ljavax/microedition/lcdui/Command;)V", args, NULL) == NJ_OK);
        CHECK(nj_call(vm, field, NJ_C_ITEM, "getDefaultCommand", "()Ljavax/microedition/lcdui/Command;", NULL, &ret) == NJ_OK && ret.r == command);
    }
    args[0].r = NULL;
    CHECK(nj_call(vm, field, NJ_C_ITEM, "setDefaultCommand", "(Ljavax/microedition/lcdui/Command;)V", args, NULL) == NJ_OK);
    CHECK(nj_call(vm, field, NJ_C_ITEM, "addCommand", "(Ljavax/microedition/lcdui/Command;)V", args, NULL) != NJ_OK);
    CHECK(vm->exception && !strcmp(vm->exception->cls->name, "java/lang/NullPointerException"));
    vm->exception = NULL;
    CHECK(nj_call(vm, item, NJ_C_STRINGITEM, "addCommand", "(Ljavax/microedition/lcdui/Command;)V", args, NULL) != NJ_OK);
    CHECK(vm->exception && !strcmp(vm->exception->cls->name, "java/lang/NullPointerException"));
    vm->exception = NULL;
    nj_vm_destroy(vm); nj_port_shutdown(&port);
}
int main(void)
{
    runtime_test();
    item_command_test();
#ifdef NJ_HAVE_SDL2
    sdl_test();
#endif
    printf("test_text_input: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
