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
static void constraint_cls(nj_vm *vm, nj_ref ref, const char *cls, int value)
{
    nj_value arg;
    arg.i = value;
    CHECK(nj_call(vm, ref, cls, "setConstraints", "(I)V", &arg, NULL) == NJ_OK);
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
    /* A focused TextField owns the editing keys, like the reference runtime:
     * DOWN widens the character selection instead of leaving the item, so the
     * native session survives the press. */
    CHECK(nj_midp_key(vm, NJ_KEY_DOWN, 1));
    nj_midp_text_input_sync(vm);
    CHECK(snapshot.active && snapshot.session == session);
    expect(vm, field, NJ_C_TEXTFIELD, chinese, 1);
    /* Making the focused field UNEDITABLE hands DOWN back to the Form's item
     * traversal: focus moves to the second row and the old session retires. */
    constraint_cls(vm, field, NJ_C_TEXTFIELD, 0x20000);
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
static void sdl_key_repeat_test(nj_port *port)
{
    static const struct {
        SDL_Keycode symbol;
        SDL_Scancode scan;
        int key;
    } cases[] = {
        {SDLK_ESCAPE, SDL_SCANCODE_ESCAPE, NJ_KEY_SOFT_RIGHT},
        {SDLK_F2, SDL_SCANCODE_F2, NJ_KEY_SOFT_RIGHT},
        {SDLK_UP, SDL_SCANCODE_UP, NJ_KEY_UP},
        {SDLK_RETURN, SDL_SCANCODE_RETURN, NJ_KEY_FIRE}
    };
    nj_event input;
    SDL_Event event;
    int i, press, repeat, count;
    static const nj_u1 expected[] = {
        NJ_EV_KEY_DOWN, NJ_EV_KEY_REPEAT, NJ_EV_KEY_REPEAT,
        NJ_EV_KEY_REPEAT, NJ_EV_KEY_UP
    };
    for (i = 0; i < (int)(sizeof(cases) / sizeof(cases[0])); i++) {
        /* Preserve the distinction and order through the port queue. MIDP
         * applies different repeat policies to Canvas and built-in screens. */
        for (press = 0; press < 2; press++) {
            memset(&event, 0, sizeof(event));
            event.type = SDL_KEYDOWN;
            event.key.keysym.sym = cases[i].symbol;
            event.key.keysym.scancode = cases[i].scan;
            push_sdl(&event);
            event.key.repeat = 1;
            for (repeat = 0; repeat < 3; repeat++) { push_sdl(&event); }
            event.type = SDL_KEYUP;
            event.key.repeat = 0;
            push_sdl(&event);
            port->display_present(port->ud, 0, 0, 240, 320);
            count = 0;
            while (port->input_poll(port->ud, &input)) {
                CHECK(input.key == cases[i].key);
                CHECK(count < (int)sizeof(expected) && input.type == expected[count]);
                count++;
            }
            CHECK(count == (int)sizeof(expected));
        }
    }
}
/*
 * A Canvas that draws its own editor.
 *
 * It has no MIDP TextField, so the runtime never saw a character at all: the
 * host's letters were dropped and the title's editor stayed empty.  The
 * characters now travel as key presses that carry them, and
 * Displayable.getKeyChar() hands them to the MIDlet.
 */
static void sdl_canvas_char_test(nj_port *port, int compat)
{
    nj_event input;
    SDL_Event event;
    nj_u4 chars[8];
    int n = 0, i;

    /* A letter the host keyboard sends: it has no key of its own, so the key
     * press is the only place the character can come from. */
    memset(&event, 0, sizeof(event));
    event.type = SDL_KEYDOWN;
    event.key.keysym.sym = SDLK_q;
    event.key.keysym.scancode = SDL_SCANCODE_Q;
    push_sdl(&event);
    event.type = SDL_KEYUP;
    push_sdl(&event);
    port->display_present(port->ud, 0, 0, 240, 320);
    while (port->input_poll(port->ud, &input)) {
        if (input.type == NJ_EV_KEY_DOWN) {
            CHECK(input.key_char == (nj_u4)'q');
            if (n < 8) { chars[n++] = input.key_char; }
        }
    }
    CHECK(n == 1 && chars[0] == (nj_u4)'q');

    if (compat) {
        /* SDL2-compat's event translator crashes on a synthetic text event. */
        return;
    }
    /* A commit from the host's IME: one key press per character. */
    memset(&event, 0, sizeof(event));
    event.type = SDL_TEXTINPUT;
    memcpy(event.text.text, "\xe4\xb8\xad", 3);          /* U+4E2D */
    memcpy(event.text.text + 3, "\xe6\x96\x87", 3);      /* U+6587 */
    push_sdl(&event);
    port->display_present(port->ud, 0, 0, 240, 320);
    n = 0;
    while (port->input_poll(port->ud, &input)) {
        if (input.type == NJ_EV_KEY_DOWN) {
            CHECK(input.key_char == (n == 0 ? 0x4E2Du : 0x6587u));
            if (n < 8) { chars[n++] = input.key_char; }
        }
    }
    CHECK(n == 2 && chars[0] == 0x4E2Du && chars[1] == 0x6587u);
    for (i = 0; i < n; i++) { (void)chars[i]; }
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
    sdl_key_repeat_test(&port);
    memset(&state, 0, sizeof(state)); state.active = 1; state.session = 7;
    port.text_input(port.ud, &state);
    CHECK(SDL_IsTextInputActive());
    /* Native editing keeps desktop autorepeat: holding Backspace must delete
     * more than one character, without also sending game key presses. */
    memset(&event, 0, sizeof(event)); event.type = SDL_KEYDOWN;
    event.key.keysym.sym = SDLK_BACKSPACE;
    event.key.keysym.scancode = SDL_SCANCODE_BACKSPACE;
    push_sdl(&event);
    event.key.repeat = 1; push_sdl(&event); push_sdl(&event);
    event.type = SDL_KEYUP; event.key.repeat = 0; push_sdl(&event);
    port.display_present(port.ud, 0, 0, 240, 320);
    while (port.input_poll(port.ud, &input)) {
        CHECK(input.type == NJ_EV_TEXT_EDIT && input.key == NJ_TEXT_BACKSPACE &&
              input.text_session == 7);
        edits++;
    }
    CHECK(edits == 3);
    edits = 0;
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
    sdl_canvas_char_test(&port, compat);
    nj_port_shutdown(&port);
}
#endif
static void item_command_test(void)
{
    nj_port port;
    nj_vm *vm;
    nj_ref field, item, form, command, other, commands;
    nj_field *slot;
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
    /* A Canvas opens an editor Form after clearing optional (possibly null)
     * commands. Null removal must not abort construction or erase commands. */
    args[0].r = string(vm, "editor");
    form = make(vm, NJ_C_FORM, "(Ljava/lang/String;)V", args);
    args[0].r = NULL;
    CHECK(nj_call(vm, form, NJ_C_DISPLAYABLE, "removeCommand", "(Ljavax/microedition/lcdui/Command;)V", args, NULL) == NJ_OK);
    args[0].r = string(vm, "OK"); args[1].i = 4; args[2].i = 0;
    command = make(vm, NJ_C_COMMAND, "(Ljava/lang/String;II)V", args);
    other = make(vm, NJ_C_COMMAND, "(Ljava/lang/String;II)V", args);
    args[0].r = command;
    CHECK(nj_call(vm, form, NJ_C_DISPLAYABLE, "addCommand", "(Ljavax/microedition/lcdui/Command;)V", args, NULL) == NJ_OK);
    CHECK(nj_call(vm, item, NJ_C_STRINGITEM, "addCommand", "(Ljavax/microedition/lcdui/Command;)V", args, NULL) == NJ_OK);
    for (i = 0; i < 2; i++) {
        args[0].r = i ? other : NULL;
        CHECK(nj_call(vm, form, NJ_C_DISPLAYABLE, "removeCommand", "(Ljavax/microedition/lcdui/Command;)V", args, NULL) == NJ_OK);
        CHECK(nj_call(vm, field, NJ_C_ITEM, "removeCommand", "(Ljavax/microedition/lcdui/Command;)V", args, NULL) == NJ_OK);
        CHECK(nj_call(vm, item, NJ_C_STRINGITEM, "removeCommand", "(Ljavax/microedition/lcdui/Command;)V", args, NULL) == NJ_OK);
        slot = nj_field_lookup(vm, NJ_C_DISPLAYABLE, NJ_F_D_COMMANDS, "Ljava/util/Vector;", 0);
        commands = nj_field_read_ref(vm, form, slot);
        CHECK(commands != NULL);
        CHECK(nj_call(vm, commands, "java/util/Vector", "size", "()I", NULL, &ret) == NJ_OK && ret.i == 1);
        slot = nj_field_lookup(vm, NJ_C_STRINGITEM, "siCmd", "Ljavax/microedition/lcdui/Command;", 0);
        CHECK(nj_field_read_ref(vm, item, slot) == command);
    }
    args[0].r = command;
    CHECK(nj_call(vm, item, NJ_C_STRINGITEM, "removeCommand", "(Ljavax/microedition/lcdui/Command;)V", args, NULL) == NJ_OK);
    CHECK(nj_field_read_ref(vm, item, slot) == NULL);
    CHECK(nj_call(vm, form, NJ_C_DISPLAYABLE, "removeCommand", "(Ljavax/microedition/lcdui/Command;)V", args, NULL) == NJ_OK);
    CHECK(nj_call(vm, commands, "java/util/Vector", "size", "()I", NULL, &ret) == NJ_OK && ret.i == 0);
    args[0].r = NULL;
    CHECK(nj_call(vm, form, NJ_C_DISPLAYABLE, "addCommand", "(Ljavax/microedition/lcdui/Command;)V", args, NULL) != NJ_OK);
    CHECK(vm->exception && !strcmp(vm->exception->cls->name, "java/lang/NullPointerException"));
    vm->exception = NULL;
    nj_vm_destroy(vm); nj_port_shutdown(&port);
}
static nj_ref dispatched_command, dispatched_screen;
static int command_calls;
static nj_status capture_command(nj_vm *vm, nj_thread *th,
                                 nj_value *args, nj_value *ret)
{
    (void)vm; (void)th; (void)ret;
    dispatched_command = args[0].r;
    dispatched_screen = args[1].r;
    command_calls++;
    return NJ_OK;
}
static nj_ref new_command(nj_vm *vm, const char *label, int type, int priority)
{
    nj_value args[3];
    args[0].r = string(vm, label); args[1].i = type; args[2].i = priority;
    return make(vm, NJ_C_COMMAND, "(Ljava/lang/String;II)V", args);
}
static void check_command_key(nj_vm *vm, int key, nj_ref screen, nj_ref command)
{
    int before = command_calls;
    CHECK(nj_midp_key(vm, key, 1));
    CHECK(command_calls == before + 1 && dispatched_command == command &&
          dispatched_screen == screen);
    (void)nj_midp_key(vm, key, 0);
    CHECK(command_calls == before + 1 && !vm->exception);
}
static void editor_command_test(void)
{
    static const nj_builtin_method methods[] = {
        {"commandAction", "(Ljavax/microedition/lcdui/Command;Ljavax/microedition/lcdui/Displayable;)V", NJ_ACC_PUBLIC}
    };
    static const nj_builtin_class fixture = {
        "test/EditorCommands", "java/lang/Object", NJ_ACC_PUBLIC,
        NULL, 0u, methods, 1u
    };
    nj_port port;
    nj_vm *vm;
    nj_class *cls;
    nj_ref listener, confirm, cancel, field, screen;
    nj_value arg;
    int i, before;
    const nj_u2 chinese[] = {0x4e2d, 0x6587};
    const nj_u2 typed[] = {0x4e2d, 0x6587, 'a'};
    CHECK(nj_port_headless(&port) == NJ_OK);
    port.text_input = capture;
    vm = nj_vm_create(&port, 4u * 1024u * 1024u);
    CHECK(vm != NULL); if (!vm) { nj_port_shutdown(&port); return; }
    cls = nj_class_create_builtin(vm, &fixture);
    CHECK(cls != NULL); if (!cls) { nj_vm_destroy(vm); nj_port_shutdown(&port); return; }
    cls->methods[0].native = capture_command;
    listener = nj_root(vm, nj_object_new(vm, fixture.name));
    CHECK(listener != NULL);
    confirm = new_command(vm, "OK", 4, 1);
    cancel = new_command(vm, "Back", 3, 1);
    for (i = 0; i < 2; i++) {
        nj_vm_set_device(vm, i ? nj_device_find("motorola-e398") : NULL);
        field = widget(vm, i ? NJ_C_TEXTBOX : NJ_C_TEXTFIELD, 32, 0, "");
        screen = field;
        if (!i) {
            arg.r = string(vm, "Account");
            screen = make(vm, NJ_C_FORM, "(Ljava/lang/String;)V", &arg);
            arg.r = field;
            CHECK(nj_call(vm, screen, NJ_C_FORM, "append", "(Ljavax/microedition/lcdui/Item;)I", &arg, NULL) == NJ_OK);
        }
        /* Xiayi uses CANCEL and OK at the same priority. Check both
         * insertion orders and ensure the right key never fires OK. */
        arg.r = i ? confirm : cancel;
        CHECK(nj_call(vm, screen, NJ_C_DISPLAYABLE, "addCommand", "(Ljavax/microedition/lcdui/Command;)V", &arg, NULL) == NJ_OK);
        arg.r = i ? cancel : confirm;
        CHECK(nj_call(vm, screen, NJ_C_DISPLAYABLE, "addCommand", "(Ljavax/microedition/lcdui/Command;)V", &arg, NULL) == NJ_OK);
        arg.r = listener;
        CHECK(nj_call(vm, screen, NJ_C_DISPLAYABLE, "setCommandListener", "(Ljavax/microedition/lcdui/CommandListener;)V", &arg, NULL) == NJ_OK);
        current(vm, screen);
        CHECK(commit(vm, "\xe4\xb8\xad\xe6\x96\x87", snapshot.session));
        check_command_key(vm, NJ_KEY_SOFT_LEFT, screen, confirm);
        expect(vm, field, i ? NJ_C_TEXTBOX : NJ_C_TEXTFIELD, chinese, 2);
        check_command_key(vm, NJ_KEY_SOFT_RIGHT, screen, cancel);
        /* An editable screen gives FIRE to the editor: the selected character
         * is inserted at the caret and no command runs (reference practice -
         * TextBox/TextField keyPressed consumes it; commands stay on the soft
         * keys). */
        before = command_calls;
        CHECK(nj_midp_key(vm, NJ_KEY_FIRE, 1));
        CHECK(command_calls == before && !vm->exception);
        expect(vm, field, i ? NJ_C_TEXTBOX : NJ_C_TEXTFIELD, typed, 3);
        /* Once the editor is UNEDITABLE, FIRE is the default command again,
         * exactly as it was before the key editor landed. */
        constraint_cls(vm, field, i ? NJ_C_TEXTBOX : NJ_C_TEXTFIELD, 0x20000);
        check_command_key(vm, NJ_KEY_FIRE, screen, confirm);
        constraint_cls(vm, field, i ? NJ_C_TEXTBOX : NJ_C_TEXTFIELD, 0);
        /* Removing one command must not leave the remaining command
         * unreachable. Then cover two positive commands with priorities. */
        arg.r = cancel;
        CHECK(nj_call(vm, screen, NJ_C_DISPLAYABLE, "removeCommand", "(Ljavax/microedition/lcdui/Command;)V", &arg, NULL) == NJ_OK);
        check_command_key(vm, NJ_KEY_SOFT_LEFT, screen, confirm);
        check_command_key(vm, NJ_KEY_SOFT_RIGHT, screen, confirm);
        arg.r = new_command(vm, "Help", 5, 0);
        CHECK(nj_call(vm, screen, NJ_C_DISPLAYABLE, "addCommand", "(Ljavax/microedition/lcdui/Command;)V", &arg, NULL) == NJ_OK);
        check_command_key(vm, NJ_KEY_SOFT_LEFT, screen, arg.r);
        check_command_key(vm, NJ_KEY_SOFT_RIGHT, screen, confirm);
    }
    nj_vm_destroy(vm); nj_port_shutdown(&port);
}
static void max_size_test(void)
{
    nj_port port;
    nj_vm *vm;
    const char *classes[] = {NJ_C_TEXTFIELD, NJ_C_TEXTBOX};
    const nj_u2 text[] = {'o', 'l', 'd', 0x4e2d};
    int i;
    CHECK(nj_port_headless(&port) == NJ_OK);
    port.text_input = capture;
    vm = nj_vm_create(&port, 4u * 1024u * 1024u);
    CHECK(vm != NULL); if (!vm) { nj_port_shutdown(&port); return; }
    for (i = 0; i < 2; i++) {
        nj_ref editor = widget(vm, classes[i], 4, 0, "old");
        nj_ref displayable = editor;
        nj_value arg, ret;
        nj_u4 session;
        if (i == 0) {
            arg.r = string(vm, "editor");
            displayable = make(vm, NJ_C_FORM, "(Ljava/lang/String;)V", &arg);
            arg.r = editor;
            CHECK(nj_call(vm, displayable, NJ_C_FORM, "append", "(Ljavax/microedition/lcdui/Item;)I", &arg, NULL) == NJ_OK);
        }
        current(vm, displayable);
        session = snapshot.session;
        arg.i = 12;
        /* Real games link to int setMaxSize(int), then use the editor. */
        CHECK(nj_call(vm, editor, classes[i], "setMaxSize", "(I)I", &arg, &ret) == NJ_OK && ret.i == 12);
        CHECK(nj_call(vm, editor, classes[i], "getMaxSize", "()I", NULL, &ret) == NJ_OK && ret.i == 12);
        nj_midp_text_input_sync(vm);
        CHECK(snapshot.active && snapshot.max_length == 12 && snapshot.session != session);
        CHECK(!commit(vm, "X", session));
        CHECK(commit(vm, "\xe4\xb8\xad", snapshot.session));
        expect(vm, editor, classes[i], text, 4);
        arg.i = 0;
        CHECK(nj_call(vm, editor, classes[i], "setMaxSize", "(I)I", &arg, &ret) != NJ_OK);
        CHECK(vm->exception && !strcmp(vm->exception->cls->name, "java/lang/IllegalArgumentException"));
        vm->exception = NULL;
        CHECK(nj_call(vm, editor, classes[i], "getMaxSize", "()I", NULL, &ret) == NJ_OK && ret.i == 12);
        expect(vm, editor, classes[i], text, 4);
    }
    nj_vm_destroy(vm); nj_port_shutdown(&port);
}
static int key_presses, key_releases, pressed_code, released_code;
static nj_status capture_press(nj_vm *vm, nj_thread *th, nj_value *args, nj_value *ret)
{
    (void)vm; (void)th; (void)ret;
    pressed_code = args[0].i;
    key_presses++;
    return NJ_OK;
}
static nj_status capture_release(nj_vm *vm, nj_thread *th, nj_value *args, nj_value *ret)
{
    (void)vm; (void)th; (void)ret;
    released_code = args[0].i;
    key_releases++;
    return NJ_OK;
}
static void key_frame(nj_vm *vm, nj_u1 type, int key)
{
    /* The VM keeps its port read-only; the test pushes events into it, which
     * is what the port API is for, so drop the const here. */
    nj_port *port = (nj_port *)vm->port;
    nj_event event;
    memset(&event, 0, sizeof(event));
    event.type = type; event.key = (nj_u1)key;
    nj_port_push_event(port, &event);
    /* Stop after input dispatch so this frame need not implement paint. Keep
     * the VM running while Java callbacks and widget handlers execute. */
    event.type = NJ_EV_QUIT;
    nj_port_push_event(port, &event);
    CHECK(nj_midp_frame(vm) == NJ_OK && !vm->exception);
    vm->running = 1u;
}
static void check_key_name(nj_vm *vm, nj_ref canvas, int code, const char *name)
{
    nj_value arg, ret = {0};
    const nj_u2 *chars;
    size_t i, len = strlen(name);
    arg.i = code;
    CHECK(nj_call_static(vm, NJ_C_CANVAS, "getKeyName", "(I)Ljava/lang/String;",
                         &arg, &ret) == NJ_OK);
    CHECK(ret.r != NULL && nj_string_len(ret.r) == len);
    if (vm->exception || !ret.r || nj_string_len(ret.r) != len) { return; }
    chars = nj_string_chars(ret.r);
    for (i = 0; i < len; i++) { CHECK(chars[i] == (nj_u2)(unsigned char)name[i]); }
}
static void motorola_softkey_test(void)
{
    static const nj_builtin_method methods[] = {
        {"keyPressed", "(I)V", NJ_ACC_PUBLIC},
        {"keyReleased", "(I)V", NJ_ACC_PUBLIC},
        {"commandAction", "(Ljavax/microedition/lcdui/Command;Ljavax/microedition/lcdui/Displayable;)V", NJ_ACC_PUBLIC}
    };
    static const nj_builtin_class fixture = {
        "test/HandsetCanvas", NJ_C_CANVAS, NJ_ACC_PUBLIC,
        NULL, 0u, methods, 3u
    };
    static const nj_device_prop override[] = {{"microedition.platform", "custom"}};
    static const int keys[] = {NJ_KEY_SOFT_LEFT, NJ_KEY_SOFT_RIGHT, NJ_KEY_UP, NJ_KEY_FIRE, NJ_KEY_5};
    static const int normal[] = {-6, -7, -1, -5, 53};
    nj_port port;
    nj_vm *vm;
    nj_class *cls;
    nj_ref canvas, confirm, cancel;
    nj_value arg;
    nj_u4 p;
    int i, expected;
    CHECK(nj_port_headless(&port) == NJ_OK);
    vm = nj_vm_create(&port, 4u * 1024u * 1024u);
    CHECK(vm != NULL); if (!vm) { nj_port_shutdown(&port); return; }
    cls = nj_class_create_builtin(vm, &fixture);
    CHECK(cls != NULL); if (!cls) { nj_vm_destroy(vm); nj_port_shutdown(&port); return; }
    cls->methods[0].native = capture_press;
    cls->methods[1].native = capture_release;
    cls->methods[2].native = capture_command;
    canvas = nj_root(vm, nj_object_new(vm, fixture.name));
    CHECK(canvas != NULL);
    CHECK(nj_call_special(vm, canvas, NJ_C_CANVAS, "<init>", "()V", NULL, NULL) == NJ_OK);
    current(vm, canvas);
    for (p = 0; p < nj_device_count(); p++) {
        const nj_device *device = nj_device_at(p);
        int moto = strcmp(device->name, "motorola-e398") == 0;
        nj_vm_set_device(vm, device);
        /* CLI --prop installs a merged table after selecting a handset. */
        nj_vm_set_system_properties(vm, override, 1u);
        for (i = 0; i < (int)(sizeof(keys) / sizeof(keys[0])); i++) {
            expected = moto && i < 2 ? -21 - i : normal[i];
            key_presses = key_releases = 0;
            key_frame(vm, NJ_EV_KEY_DOWN, keys[i]);
            CHECK(key_presses == 1 && pressed_code == expected && key_releases == 0);
            key_frame(vm, NJ_EV_KEY_REPEAT, keys[i]);
            CHECK(key_presses == 1 && key_releases == 0);
            key_frame(vm, NJ_EV_KEY_UP, keys[i]);
            CHECK(key_releases == 1 && released_code == expected);
        }
        check_key_name(vm, canvas, moto ? -21 : -6, "SOFTKEY1");
        check_key_name(vm, canvas, moto ? -22 : -7, "SOFTKEY2");
        if (!moto) {
            check_key_name(vm, canvas, -21, "SOFTKEY3");
            /* A code no profile defines answers the placeholder string. */
            arg.i = -22;
            CHECK(nj_call_static(vm, NJ_C_CANVAS, "getKeyName",
                                 "(I)Ljava/lang/String;", &arg, NULL) == NJ_OK);
            CHECK(vm->exception == NULL);
        }
    }
    nj_vm_set_device(vm, nj_device_find("motorola-e398"));
    /* Canvas commands still receive logical left/right keys once per press.
     * Canvas chooses the default by priority, unlike Form's command groups. */
    confirm = new_command(vm, "OK", 4, 0);
    cancel = new_command(vm, "Back", 3, 1);
    arg.r = confirm;
    CHECK(nj_call(vm, canvas, NJ_C_DISPLAYABLE, "addCommand", "(Ljavax/microedition/lcdui/Command;)V", &arg, NULL) == NJ_OK);
    arg.r = cancel;
    CHECK(nj_call(vm, canvas, NJ_C_DISPLAYABLE, "addCommand", "(Ljavax/microedition/lcdui/Command;)V", &arg, NULL) == NJ_OK);
    arg.r = canvas;
    CHECK(nj_call(vm, canvas, NJ_C_DISPLAYABLE, "setCommandListener", "(Ljavax/microedition/lcdui/CommandListener;)V", &arg, NULL) == NJ_OK);
    check_command_key(vm, NJ_KEY_SOFT_LEFT, canvas, confirm);
    check_command_key(vm, NJ_KEY_SOFT_RIGHT, canvas, cancel);
    nj_vm_set_device(vm, NULL);
    check_key_name(vm, canvas, -21, "SOFTKEY3");
    nj_vm_destroy(vm); nj_port_shutdown(&port);
}
static void repeat_policy_test(void)
{
    static const nj_builtin_method methods[] = {
        {"keyPressed", "(I)V", NJ_ACC_PUBLIC},
        {"keyReleased", "(I)V", NJ_ACC_PUBLIC}
    };
    static const nj_builtin_class fixture = {
        "test/RepeatCanvas", NJ_C_CANVAS, NJ_ACC_PUBLIC,
        NULL, 0u, methods, 2u
    };
    static const int keys[] = {NJ_KEY_SOFT_RIGHT, NJ_KEY_UP, NJ_KEY_FIRE};
    nj_port port;
    nj_vm *vm;
    nj_class *cls;
    nj_ref canvas, list, form, gauge, game;
    nj_value args[4], ret;
    int i, j;
    CHECK(nj_port_headless(&port) == NJ_OK);
    vm = nj_vm_create(&port, 4u * 1024u * 1024u);
    CHECK(vm != NULL); if (!vm) { nj_port_shutdown(&port); return; }
    cls = nj_class_create_builtin(vm, &fixture);
    CHECK(cls != NULL); if (!cls) { nj_vm_destroy(vm); nj_port_shutdown(&port); return; }
    cls->methods[0].native = capture_press;
    cls->methods[1].native = capture_release;
    canvas = nj_root(vm, nj_object_new(vm, fixture.name));
    CHECK(canvas != NULL);
    CHECK(nj_call_special(vm, canvas, NJ_C_CANVAS, "<init>", "()V", NULL, NULL) == NJ_OK);
    current(vm, canvas);
    CHECK(nj_call(vm, canvas, NJ_C_CANVAS, "hasRepeatEvents", "()Z", NULL, &ret) == NJ_OK && !ret.i);
    for (i = 0; i < (int)(sizeof(keys) / sizeof(keys[0])); i++) {
        key_presses = key_releases = 0;
        key_frame(vm, NJ_EV_KEY_DOWN, keys[i]);
        for (j = 0; j < 3; j++) { key_frame(vm, NJ_EV_KEY_REPEAT, keys[i]); }
        CHECK(key_presses == 1 && key_releases == 0);
        key_frame(vm, NJ_EV_KEY_UP, keys[i]);
        CHECK(key_presses == 1 && key_releases == 1);
        key_frame(vm, NJ_EV_KEY_DOWN, keys[i]);
        key_frame(vm, NJ_EV_KEY_UP, keys[i]);
        CHECK(key_presses == 2 && key_releases == 2);
    }
    args[0].i = 1;
    game = make(vm, NJ_C_GAMECANVAS, "(Z)V", args);
    current(vm, game);
    key_frame(vm, NJ_EV_KEY_DOWN, NJ_KEY_UP);
    key_frame(vm, NJ_EV_KEY_REPEAT, NJ_KEY_UP);
    CHECK(nj_call(vm, game, NJ_C_GAMECANVAS, "getKeyStates", "()I", NULL, &ret) == NJ_OK && ret.i == 2);
    CHECK(nj_call(vm, game, NJ_C_GAMECANVAS, "getKeyStates", "()I", NULL, &ret) == NJ_OK && ret.i == 2);
    key_frame(vm, NJ_EV_KEY_UP, NJ_KEY_UP);
    key_frame(vm, NJ_EV_KEY_REPEAT, NJ_KEY_UP);
    CHECK(nj_call(vm, game, NJ_C_GAMECANVAS, "getKeyStates", "()I", NULL, &ret) == NJ_OK && !ret.i);
    args[0].r = string(vm, "Repeat"); args[1].i = 3;
    list = make(vm, NJ_C_LIST, "(Ljava/lang/String;I)V", args);
    for (i = 0; i < 6; i++) {
        args[0].r = string(vm, "Row");
        CHECK(nj_call(vm, list, NJ_C_LIST, "append", "(Ljava/lang/String;)I", args, NULL) == NJ_OK);
    }
    current(vm, list);
    key_frame(vm, NJ_EV_KEY_DOWN, NJ_KEY_DOWN);
    for (i = 0; i < 3; i++) { key_frame(vm, NJ_EV_KEY_REPEAT, NJ_KEY_DOWN); }
    key_frame(vm, NJ_EV_KEY_UP, NJ_KEY_DOWN);
    CHECK(nj_call(vm, list, NJ_C_LIST, "getSelectedIndex", "()I", NULL, &ret) == NJ_OK && ret.i == 4);
    args[0].r = string(vm, "Form");
    form = make(vm, NJ_C_FORM, "(Ljava/lang/String;)V", args);
    args[0].r = string(vm, "Level"); args[1].i = 1; args[2].i = 100; args[3].i = 0;
    gauge = make(vm, NJ_C_GAUGE, "(Ljava/lang/String;ZII)V", args);
    args[0].r = gauge;
    CHECK(nj_call(vm, form, NJ_C_FORM, "append", "(Ljavax/microedition/lcdui/Item;)I", args, NULL) == NJ_OK);
    current(vm, form);
    key_frame(vm, NJ_EV_KEY_DOWN, NJ_KEY_DOWN);
    CHECK(nj_call(vm, gauge, NJ_C_GAUGE, "getValue", "()I", NULL, &ret) == NJ_OK && ret.i == 5);
    for (i = 0; i < 3; i++) { key_frame(vm, NJ_EV_KEY_REPEAT, NJ_KEY_DOWN); }
    key_frame(vm, NJ_EV_KEY_UP, NJ_KEY_DOWN);
    CHECK(nj_call(vm, gauge, NJ_C_GAUGE, "getValue", "()I", NULL, &ret) == NJ_OK && ret.i == 20);
    nj_vm_destroy(vm); nj_port_shutdown(&port);
}
int main(void)
{
    runtime_test();
    repeat_policy_test();
    motorola_softkey_test();
    item_command_test();
    editor_command_test();
    max_size_test();
#ifdef NJ_HAVE_SDL2
    sdl_test();
#endif
    printf("test_text_input: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
