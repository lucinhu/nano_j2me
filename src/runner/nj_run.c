/*
 * nano_j2me - nj_run.c
 *
 * The runner: everything between "here is a port and a JAR" and "the MIDlet is
 * running".  A platform's main() is normally three lines that call this.
 *
 * The sequence matters and is documented here because it is also the teardown
 * order in reverse:
 *
 *   read the archive          (the bytes must outlive the VM; nj_zip reads in
 *                              place and never copies)
 *   create the VM             (Java heap, class library, natives)
 *   open the app              (installs itself as the VM's class source)
 *   instantiate the MIDlet    (runs <init>, which every game overrides)
 *   startApp()                (the game builds its UI and usually starts a
 *                              thread, then returns)
 *   run the frame loop        (input, background threads, paint, present)
 *   destroyApp(false)         (the game saves its RecordStore here)
 *   close the app, destroy the VM, shut the port down
 *
 * Every step is unwound on every error path, so a failed launch leaves the
 * process exactly as it found it - which is what makes a "back to the menu,
 * pick another game" UI possible on a device that never reboots.
 */
#include "nj2me/nj.h"

/*
 * How long to wait for a MIDlet to install its Displayable after startApp()
 * returns, in frames.  See the comment at the check in nj_run().
 */
#define NJ_NO_DISPLAY_GRACE 60

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <sys/stat.h>

/* ------------------------------------------------------------------ */
/* Reading the archive                                                 */
/* ------------------------------------------------------------------ */
/*
 * A helper object so that whichever allocation strategy the port uses is
 * unwound by the same code path.
 */
typedef struct jar_blob {
    void       *data;
    nj_u4       size;
    const nj_port *port;
} jar_blob;

static void blob_free(jar_blob *b)
{
    if (!b || !b->data) {
        return;
    }
    if (b->port && b->port->free) {
        b->port->free(b->port->ud, b->data);
    } else {
        free(b->data);
    }
    b->data = NULL;
    b->size = 0u;
}

static void *blob_alloc(const nj_port *port, nj_size n)
{
    if (port && port->alloc) {
        return port->alloc(port->ud, n);
    }
    return malloc((size_t)n);
}

/* Read a whole file through the port's hooks when it has them. */
static nj_status blob_read(jar_blob *b, const char *path)
{
    const nj_port *port = b->port;
    nj_u4 size = 0u;

    if (port && port->file_open) {
        nj_file f;
        nj_u4 cap = 64u * 1024u;
        nj_u1 *buf;
        nj_u4 used = 0u;

        memset(&f, 0, sizeof(f));
        if (port->file_open(port->ud, path, "rb", &f) != NJ_OK) {
            return NJ_ERR;
        }
        buf = (nj_u1 *)blob_alloc(port, cap);
        if (!buf) {
            port->file_close(port->ud, &f);
            return NJ_ERR_NOMEM;
        }
        for (;;) {
            nj_s4 got;
            if (used == cap) {
                nj_u1 *nb;
                cap *= 2u;
                nb = (nj_u1 *)blob_alloc(port, cap);
                if (!nb) {
                    blob_free(&(jar_blob){ buf, used, port });
                    port->file_close(port->ud, &f);
                    return NJ_ERR_NOMEM;
                }
                memcpy(nb, buf, used);
                blob_free(&(jar_blob){ buf, used, port });
                buf = nb;
            }
            got = port->file_read(port->ud, &f, buf + used,
                                  (nj_u4)(cap - used));
            if (got < 0) {
                blob_free(&(jar_blob){ buf, used, port });
                port->file_close(port->ud, &f);
                return NJ_ERR;
            }
            if (got == 0) {
                break;
            }
            used += (nj_u4)got;
        }
        port->file_close(port->ud, &f);
        b->data = buf;
        b->size = used;
        return used ? NJ_OK : NJ_ERR;
    }

    {
        FILE *fp = fopen(path, "rb");
        long n;
        nj_u1 *buf;

        if (!fp) {
            return NJ_ERR;
        }
        if (fseek(fp, 0, SEEK_END) != 0 || (n = ftell(fp)) <= 0) {
            fclose(fp);
            return NJ_ERR;
        }
        rewind(fp);
        buf = (nj_u1 *)blob_alloc(port, (nj_size)n);
        if (!buf) {
            fclose(fp);
            return NJ_ERR_NOMEM;
        }
        if (fread(buf, 1u, (size_t)n, fp) != (size_t)n) {
            fclose(fp);
            blob_free(&(jar_blob){ buf, (nj_u4)n, port });
            return NJ_ERR;
        }
        fclose(fp);
        b->data = buf;
        b->size = (nj_u4)n;
        size = (nj_u4)n;
        (void)size;
        return NJ_OK;
    }
}

/* ------------------------------------------------------------------ */
/* The run                                                             */
/* ------------------------------------------------------------------ */
/*
 * A Throwable's message, for a log line.
 *
 * The class name alone is not enough and this is the case that proves it:
 * "cannot instantiate Bubblet: java/lang/NoClassDefFoundError" says nothing,
 * while the MESSAGE of that exception names the class that was missing.  The
 * runtime has always put it there; the runner was throwing it away.
 *
 * Returns "" rather than NULL so the caller can print unconditionally.
 */
static const char *exc_message(nj_vm *vm, nj_ref exc, char *buf, nj_size cap)
{
    nj_field *f;
    nj_ref msg;

    buf[0] = '\0';
    if (!vm || !exc) {
        return buf;
    }
    /*
     * Looked up on Throwable rather than on the exception's own class: the
     * field is DECLARED there, so the lookup walks nothing and the offset is
     * valid for every subclass instance.  The runner only has the public
     * headers, which do not expose a class's name from a ref.
     */
    f = nj_field_lookup(vm, "java/lang/Throwable", "detailMessage",
                        "Ljava/lang/String;", 0);
    if (!f) {
        return buf;
    }
    msg = nj_field_read_ref(vm, exc, f);
    if (!msg) {
        return buf;
    }
    (void)nj_string_to_utf8(msg, buf, (int)cap);
    return buf;
}

/* ------------------------------------------------------------------ */
/* Scripted keys                                                       */
/* ------------------------------------------------------------------ */
/*
 * "frame:key,frame:key" with key names rather than numeric codes, because a
 * script is meant to be read: "20:fire,40:fire" says what it does and "20:37"
 * does not.
 */
/*
 * Big enough for a full navigation: title -> menu -> start -> play -> back.
 * The first version held 32 and the corpus only ever sent two or three presses,
 * which is why "the game started" was the strongest thing it could claim.
 */
#define NJ_KEY_SCRIPT_MAX 512

typedef struct key_script {
    nj_u8 frame[NJ_KEY_SCRIPT_MAX];
    nj_u1 key[NJ_KEY_SCRIPT_MAX];
    /*
     * Pointer entries live in the same list as key entries so their ORDER and
     * their frame numbers are the same thing a key's are.  ptr_kind is 0 for a
     * key entry and one of NJ_POINTER_* for a pointer entry, which is what
     * tells the two apart; px/py are only read when it is set.
     */
    nj_u1 ptr_kind[NJ_KEY_SCRIPT_MAX];
    nj_s2 px[NJ_KEY_SCRIPT_MAX];
    nj_s2 py[NJ_KEY_SCRIPT_MAX];
    int   n;
    int   next_down;      /* index of the next press to send   */
    int   next_up;        /* index of the next release to send */
} key_script;

static int key_from_name(const char *p, int len, nj_u1 *out)
{
    static const struct { const char *name; nj_u1 k; } names[] = {
        { "up", NJ_KEY_UP }, { "down", NJ_KEY_DOWN },
        { "left", NJ_KEY_LEFT }, { "right", NJ_KEY_RIGHT },
        { "fire", NJ_KEY_FIRE }, { "soft1", NJ_KEY_SOFT_LEFT },
        { "soft2", NJ_KEY_SOFT_RIGHT }, { "soft3", NJ_KEY_SOFT_RIGHT },
        { "clear", NJ_KEY_CLEAR },
        { "a", NJ_KEY_GAME_A }, { "b", NJ_KEY_GAME_B },
        { "c", NJ_KEY_GAME_C }, { "d", NJ_KEY_GAME_D },
        { "star", NJ_KEY_STAR }, { "pound", NJ_KEY_POUND },
        { "*", NJ_KEY_STAR }, { "#", NJ_KEY_POUND },
        { NULL, 0 }
    };
    int i;

    if (len == 1 && p[0] >= '0' && p[0] <= '9') {
        *out = (nj_u1)(NJ_KEY_0 + (p[0] - '0'));
        return 1;
    }
    for (i = 0; names[i].name; i++) {
        int j = 0;
        while (names[i].name[j] && j < len && names[i].name[j] == p[j]) {
            j++;
        }
        if (j == len && names[i].name[j] == '\0') {
            *out = names[i].k;
            return 1;
        }
    }
    return 0;
}

/*
 * Load a key script from a file, or return the spec unchanged.
 *
 * A script that drives a game through its menus is a real artefact - it has to
 * be read, edited when the game's menu moves, and kept next to the title it
 * describes - so "--keys @tests/keys/maze.txt" is the form that gets used.  The
 * caller frees the returned pointer when it differs from `spec`.
 */
static char *key_script_load(const char *spec, nj_size *out_len)
{
    const char *path;
    FILE *f;
    long n;
    char *buf;

    *out_len = 0u;
    if (!spec || spec[0] != '@') {
        return (char *)(uintptr_t)spec;      /* borrowed: not ours to free */
    }
    path = spec + 1;
    f = fopen(path, "rb");
    if (!f) {
        return NULL;
    }
    if (fseek(f, 0L, SEEK_END) != 0 || (n = ftell(f)) < 0L) {
        fclose(f);
        return NULL;
    }
    rewind(f);
    buf = (char *)malloc((size_t)n + 1u);
    if (!buf) {
        fclose(f);
        return NULL;
    }
    if (n > 0L && fread(buf, 1u, (size_t)n, f) != (size_t)n) {
        free(buf);
        fclose(f);
        return NULL;
    }
    buf[n] = '\0';
    fclose(f);
    *out_len = (nj_size)n;
    return buf;
}

/*
 * `pressed@X,Y`, `dragged@X,Y`, `released@X,Y`.  Coordinates are DEVICE pixels,
 * the same thing the port contract takes, so a script stays valid if the
 * window scale changes.
 *
 * The '@' is what keeps this out of the key namespace: `down` is already the
 * down-arrow key, so `down@40,60` would have been ambiguous.
 */
static int ptr_kind_from_name(const char *p, int len)
{
    if (len == 7 && memcmp(p, "pressed", 7) == 0)  { return NJ_EV_POINTER_DOWN; }
    if (len == 8 && memcmp(p, "released", 8) == 0) { return NJ_EV_POINTER_UP; }
    if (len == 7 && memcmp(p, "dragged", 7) == 0)  { return NJ_EV_POINTER_MOVE; }
    return -1;
}

static int key_script_parse(const char *spec, key_script *ks)
{
    const char *p = spec;

    ks->n = 0;
    ks->next_down = 0;
    ks->next_up = 0;
    if (!spec) {
        return 0;
    }
    /* Whitespace and newlines separate entries, and '#' starts a comment that
     * runs to the end of the line.  A script that is meant to be READ - and to
     * be edited when a game's menu moves - cannot be one long line. */
    while (*p && ks->n < NJ_KEY_SCRIPT_MAX) {
        nj_u8 frame = 0u;
        int len = 0;
        nj_u1 key = 0;

        while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r' ||
               *p == ',') {
            p++;
        }
        if (*p == '#') {
            while (*p && *p != '\n') { p++; }
            continue;
        }
        if (!*p) {
            break;
        }
        while (*p >= '0' && *p <= '9') {
            frame = frame * 10u + (nj_u8)(*p - '0');
            p++;
        }
        if (*p != ':') {
            return -1;
        }
        p++;
        /* Measure the key name BEFORE looking it up.  The scan was moved below
         * the call while adding comments, which made len always 0 and rejected
         * every script - including the well-formed ones.
         *
         * The scan runs to whitespace now rather than stopping at a comma,
         * because a pointer entry's two coordinates are comma-separated and
         * stopping at the first one would truncate it to `pressed@40`. */
        while (p[len] && p[len] != ' ' && p[len] != '\t' &&
               p[len] != '\n' && p[len] != '\r' && p[len] != '#') {
            len++;
        }
        {
            int at = -1;
            int i;
            for (i = 0; i < len; i++) {
                if (p[i] == '@') { at = i; break; }
            }
            if (at > 0) {
                int kind = ptr_kind_from_name(p, at);
                int x = 0, y = 0, j = at + 1;
                if (kind < 0) {
                    return -1;
                }
                while (j < len && p[j] >= '0' && p[j] <= '9') {
                    x = x * 10 + (p[j] - '0');
                    j++;
                }
                if (j >= len || p[j] != ',') {
                    return -1;
                }
                j++;
                while (j < len && p[j] >= '0' && p[j] <= '9') {
                    y = y * 10 + (p[j] - '0');
                    j++;
                }
                if (j != len) {
                    return -1;
                }
                ks->ptr_kind[ks->n] = (nj_u1)kind;
                ks->px[ks->n] = (nj_s2)x;
                ks->py[ks->n] = (nj_s2)y;
                ks->key[ks->n] = 0;
            } else {
                /*
                 * A comma separates entries only for a KEY name.  The scan
                 * above runs to whitespace - it has to, or a pointer entry's
                 * coordinates would be cut short - so "5:fire,20:up" arrives
                 * here as one long name.  Cut it at the first comma rather than
                 * relying on the scan to have stopped there.
                 *
                 * Getting this wrong sent NO keys at all, silently: the parse
                 * returned -1 and the runner carried on with no input.
                 * tests/test_poke caught it.
                 */
                {
                    int c;
                    for (c = 0; c < len; c++) {
                        if (p[c] == ',') {
                            len = c;
                            break;
                        }
                    }
                }
                if (!key_from_name(p, len, &key)) {
                    return -1;
                }
                ks->ptr_kind[ks->n] = 0;
                ks->key[ks->n] = key;
            }
            ks->frame[ks->n] = frame;
            ks->n++;
            p += len;
            /*
             * A pointer entry stopped on its own comma, so anything up to the
             * next separator is the rest of its coordinates and has to be
             * stepped over here.  A key entry must NOT do this: with
             * "5:fire,20:up" there is no whitespace between the entries, and
             * skipping to the next space would swallow the second one.
             */
            if (at > 0) {
                while (*p && *p != ' ' && *p != '\t' && *p != '\n' &&
                       *p != '\r' && *p != '#') {
                    p++;
                }
            }
        }
    }
    return ks->n;
}

/*
 * Send whatever this frame is due.  A press is a key-down now and a key-up one
 * frame later: a key that goes down and never comes up leaves many games stuck
 * on it, and a press and release in the same frame is a key most of them never
 * see at all.
 */
static void key_script_tick(const nj_port *port, key_script *ks, nj_u8 frame)
{
    nj_event ev;

    if (!port->input_push || ks->n == 0) {
        return;
    }
    while (ks->next_down < ks->n && ks->frame[ks->next_down] <= frame) {
        int i = ks->next_down;
        memset(&ev, 0, sizeof(ev));
        if (ks->ptr_kind[i] != 0) {
            /*
             * A pointer entry says exactly what happens and nothing is
             * inferred: `pressed` sends a down and `released` sends an up, so a
             * tap is two lines.  Auto-releasing one frame later the way keys do
             * would be convenient and wrong - it would make a long press or a
             * drag impossible to express, and a script that also wrote
             * `released` would send two ups.
             */
            ev.type = ks->ptr_kind[i];
            ev.x = ks->px[i];
            ev.y = ks->py[i];
            ks->next_down++;
            ks->next_up++;      /* nothing to release later */
        } else {
            ev.type = (nj_u1)NJ_EV_KEY_DOWN;
            ev.key = ks->key[i];
            ks->next_down++;
        }
        (void)port->input_push(port->ud, &ev);
    }
    while (ks->next_up < ks->next_down) {
        if (ks->ptr_kind[ks->next_up] != 0) {
            ks->next_up++;
            continue;
        }
        /* Release one frame after the matching press. */
        if (ks->frame[ks->next_up] + 1u > frame) {
            break;
        }
        memset(&ev, 0, sizeof(ev));
        ev.type = (nj_u1)NJ_EV_KEY_UP;
        ev.key = ks->key[ks->next_up];
        (void)port->input_push(port->ud, &ev);
        ks->next_up++;
    }
}

/*
 * --where: hand the core's thread/frame description to the port's log.
 *
 * The core builds the lines, because it is the only place that can see the
 * frames; the runner only decides where they go.
 */
static void where_sink(void *ud, const char *line)
{
    const nj_port *p = (const nj_port *)ud;

    if (p && p->log) {
        p->log(p->user, 2, line);
    }
}

/*
 * mkdir -p, for the RecordStore directory.  Failure is not fatal: if the
 * directory cannot be made, RMS falls back to failing per store, which is the
 * same thing that happens on a read-only filesystem and is already handled.
 */
static void mkdir_p(const char *path)
{
    char tmp[512];
    size_t i;
    size_t len = strlen(path);

    if (len == 0u || len >= sizeof(tmp)) {
        return;
    }
    memcpy(tmp, path, len + 1u);
    for (i = 1u; i < len; i++) {
        if (tmp[i] == '/') {
            tmp[i] = '\0';
            (void)mkdir(tmp, 0777);
            tmp[i] = '/';
        }
    }
    (void)mkdir(tmp, 0777);
}

int nj_run(const nj_port *port_in, const nj_run_opts *opts)
{
    nj_port port;
    nj_vm *vm = NULL;
    nj_app *app = NULL;
    jar_blob blob;
    nj_ref midlet = NULL;
    nj_ref exc = NULL;
    const char *class_name = NULL;
    int rc = 1;
    int stats = 0;
    char rms_path[512];
    rms_path[0] = '\0';
    nj_s8 nj_start_ms = 0;
    nj_s8 nj_end_ms = 0;

    if (!port_in || !opts) {
        return 2;
    }
    port = *port_in;
    stats = opts->stats;

    /*
     * Put RecordStore somewhere that is not the working directory.
     *
     * The RMS module falls back to "." when the port does not say, so running a
     * game dropped its save files wherever you happened to be standing - which
     * is how a repository ended up with 45 .rms files in its root, named after
     * whatever each third-party title called its store ("bubblet", "shushan",
     * "__sfSmsInfo").  They are gitignored, so nothing was ever committed, but
     * a runtime that writes to the CWD by default is a runtime that litters.
     *
     * Worse, "." is also a COLLISION: two games that both call their store
     * "save" share one file, and the second one to run reads the first one's
     * high scores.
     *
     * So: one directory per game, under the user's data directory when there is
     * one.  --rms-dir still overrides it, and a port that sets rms_dir itself
     * is left alone.
     */
    if (!port.rms_dir && opts->jar_path && opts->jar_path[0]) {
        const char *xdg = getenv("XDG_DATA_HOME");
        const char *home = getenv("HOME");
        char name[96];
        int n = 0;
        const char *q = opts->jar_path + strlen(opts->jar_path);

        /* The JAR's basename, without directories and without the .jar. */
        while (q > opts->jar_path && q[-1] != '/' && q[-1] != '\\') { q--; }
        while (*q && *q != '.' && n < (int)sizeof(name) - 1) { name[n++] = *q++; }
        if (n == 0) { memcpy(name, "game", 5); n = 4; }
        name[n] = '\0';

        if (xdg && xdg[0]) {
            snprintf(rms_path, sizeof(rms_path), "%s/nano_j2me/%s", xdg, name);
        } else if (home && home[0]) {
            snprintf(rms_path, sizeof(rms_path), "%s/.local/share/nano_j2me/%s",
                     home, name);
        } else {
            /* No home either: keep it out of the way but make it obvious. */
            snprintf(rms_path, sizeof(rms_path), "%s.rms", name);
        }
        mkdir_p(rms_path);
        port.rms_dir = rms_path;
    }
    nj_start_ms = (nj_s8)(port.millis ? port.millis(port.ud) : 0u);
    port.user = port_in->user;

    memset(&blob, 0, sizeof(blob));
    blob.port = &port;

    /* ---- the archive ---- */
    if (opts->jar_data && opts->jar_size > 0u) {
        blob.data = (void *)(uintptr_t)opts->jar_data;
        blob.size = opts->jar_size;
        blob.port = NULL;            /* borrowed: never freed below */
    } else if (port.app_blob) {
        nj_u4 n = 0u;
        blob.data = (void *)(uintptr_t)port.app_blob(port.ud, &n);
        blob.size = n;
        blob.port = NULL;            /* owned by the port */
    } else if (opts->jar_path) {
        if (blob_read(&blob, opts->jar_path) != NJ_OK) {
            if (port.log) {
                port.log(port.user, 0, "cannot read the JAR");
            }
            return 2;
        }
    } else {
        if (port.log) {
            port.log(port.user, 0, "no JAR given");
        }
        return 2;
    }

    /* ---- the VM ---- */
    vm = nj_vm_create(&port, opts->heap_bytes);
    if (!vm) {
        if (port.log) {
            port.log(port.user, 0, "cannot create the VM (out of memory?)");
        }
        goto done;
    }

    /* ---- the application ---- */
    app = nj_app_open(vm, blob.data, blob.size);
    if (!app) {
        if (port.log) {
            port.log(port.user, 0, "not a usable MIDlet suite "
                                   "(bad JAR or no MIDlet-1 in the manifest)");
        }
        goto done;
    }

    class_name = opts->midlet_class;
    if (!class_name || !*class_name) {
        class_name = nj_app_midlet_class(app, 0);
    }
    if (!class_name || !*class_name) {
        if (port.log) {
            port.log(port.user, 0, "the manifest names no MIDlet class");
        }
        goto done;
    }

    if (port.log) {
        char buf[256];
        snprintf(buf, sizeof(buf), "running %s (%s %s)", class_name,
                 app->jar_name[0] ? app->jar_name : "unnamed",
                 app->jar_version[0] ? app->jar_version : "?");
        port.log(port.user, 2, buf);
    }

    /* ---- the MIDlet ---- */
    midlet = nj_vm_new_midlet(vm, class_name, &exc);
    if (!midlet) {
        if (port.log) {
            char buf[512];
            char why[256];
            const char *n = nj_ref_class_name(exc);
            (void)exc_message(vm, exc, why, sizeof(why));
            snprintf(buf, sizeof(buf), "cannot instantiate %s: %s%s%s",
                     class_name, n ? n : "?",
                     why[0] ? ": " : "", why);
            port.log(port.user, 0, buf);
        }
        goto done;
    }

    /*
     * startApp() may legitimately return immediately (most games start a
     * thread and return), or it may do all its work inline.  Either way the
     * frame loop below takes over afterwards.
     */
    if (nj_vm_start_app(vm, midlet) != NJ_OK && nj_has_exception(vm)) {
        if (port.log) {
            char buf[320];
            /* nj_exception_describe includes the class, the message and the
             * method+pc it was raised at, which is what turns "it threw" into
             * a one-line diagnosis. */
            snprintf(buf, sizeof(buf), "startApp() threw %s",
                     nj_exception_describe(vm));
            port.log(port.user, 0, buf);
        }
        nj_clear_exception(vm);
    }

    /*
     * A MIDlet that threw out of startApp() has not installed a Displayable,
     * so the frame loop would sit there drawing nothing until the user
     * intervened.  Report and leave instead: an emulator that appears to hang
     * is far worse to debug than one that exits and says why.
     */
    /*
     * ...but do not give up the instant startApp() returns.
     *
     * A MIDlet is allowed to install its Displayable LATER - from a background
     * thread it started, or after a timer - and doing so is ordinary, not
     * broken.  Treating that as fatal turned "slow" into "broken":
     * shi-qian-bu-luo (Prehistoric Tribes) installs nothing in startApp() and
     * was reported as having nothing to show, after executing eighteen
     * bytecodes.
     *
     * So wait - but not forever.  Without a bound, a MIDlet that really does
     * install nothing would sit here looking like a hang, and a stuck emulator
     * is exactly the failure this check exists to prevent.  Sixty frames is
     * one second at the default rate, long enough for a thread to get going
     * and short enough to stay a diagnostic.
     */
    {
        int waited = 0;
        while (!nj_midp_current(vm) && waited < NJ_NO_DISPLAY_GRACE) {
            nj_midp_frame(vm);
            waited++;
        }
        if (waited > 0 && nj_midp_current(vm) && port.log) {
            /* Worth saying, because it explains a blank first second. */
            char late[96];
            snprintf(late, sizeof(late),
                     "note: the Displayable arrived %d frame(s) after startApp()",
                     waited);
            port.log(port.user, 0, late);
        }
    }

    if (!nj_midp_current(vm)) {
        if (port.log) {
            port.log(port.user, 0,
                     "the MIDlet installed no Displayable; nothing to show");
        }
        goto done;
    }

    /*
     * The key script is loaded HERE, not inside the frame loop, so its lifetime
     * is the whole run and the loop below can just tick it.
     */
    key_script keys;
    {
        nj_size klen = 0u;
        char *kspec = key_script_load(opts->keys, &klen);
        if (opts->keys && !kspec) {
            if (port.log) {
                port.log(port.user, 0, "--keys: cannot read the script file");
            }
            key_script_parse(NULL, &keys);
        } else {
            if (key_script_parse(kspec, &keys) < 0 && port.log) {
                port.log(port.user, 0,
                         "--keys: expected frame:key pairs such as "
                         "\"20:fire,60:left\", or @file for a script");
            }
            if (kspec && kspec != opts->keys) {
                free(kspec);
            }
        }
    }

    /* ---- the frame loop ---- */
    {
        nj_status st;
    /*
     * The explicit frame loop is used when a frame limit was asked for, and
     * ALWAYS when keys were scripted.
     *
     * A scripted press is delivered between two frames, and nj_midp_run() is a
     * single call that does not return until the MIDlet exits, so there is
     * nowhere to put it on that path.  Without this the keys were silently
     * ignored unless --run-frames happened to be given as well - which is
     * exactly the combination a corpus run does not use, so a title that needs
     * a key to get past its splash screen appeared to have no gameplay.
     */
    if (opts->run_frames > 0u || (opts->keys && opts->keys[0])) {
            /*
             * Run a fixed number of frames and stop.
             *
             * Most MIDlets never exit on their own, so without this a
             * benchmark has to be killed from outside - which loses the
             * counters (they are printed before teardown) and makes the frame
             * count depend on how loaded the machine was.  A fixed frame count
             * gives the same work every time, which is what a performance
             * comparison needs.
             */
            for (;;) {
                nj_vm_stats s;
                if (!nj_vm_running(vm)) {
                    break;
                }
                nj_vm_get_stats(vm, &s);
                if (opts->run_frames > 0u &&
                    s.frames >= (nj_u8)opts->run_frames) {
                    break;
                }
                key_script_tick(&port, &keys, s.frames);
                st = nj_midp_frame(vm);
                if (st != NJ_OK) {
                    break;
                }
            }
            st = NJ_OK;
        } else {
            st = nj_midp_run(vm);
        }
        if (st != NJ_OK) {
            if (port.log) {
                port.log(port.user, 0, "the display loop failed");
            }
            goto done;
        }
    }

    rc = (int)nj_vm_exit_code(vm);

    /*
     * Always: a run that presented nothing and a run the MIDlet asked to end
     * look identical in a frame dump.  Ports that cannot log still get the
     * answer through nj_vm_stop_why().
     */
    if (port.log && vm) {
        const char *why = nj_vm_stop_why(vm);
        char whybuf[160];
        snprintf(whybuf, sizeof(whybuf), "run ended: %s (exit code %d)",
                 why ? why : "the frame budget ran out", rc);
        port.log(port.user, 1, whybuf);
    }

done:
    nj_end_ms = (nj_s8)(port.millis ? port.millis(port.ud) : 0u);
    /*
     * --stats: the numbers behind docs/PERFORMANCE.md.
     *
     * Printed before teardown so they describe the run that just happened, not
     * a torn-down VM.  The definitions are the ones in nj_vm.h, so an embedder
     * measuring its own target gets comparable figures.
     */
    if (opts->where && vm) {
        nj_vm_describe_threads(vm, where_sink, (void *)&port);
    }

    if (stats && vm) {
        nj_vm_stats s;
        double secs = (nj_end_ms > nj_start_ms)
                          ? (double)(nj_end_ms - nj_start_ms) / 1000.0
                          : 0.0;
        nj_vm_get_stats(vm, &s);
        if (port.log) {
            char buf[256];
            snprintf(buf, sizeof(buf),
                     "stats: frames=%lu time=%.3fs fps=%.1f "
                     "bytecodes=%lu (%.2f M/s) natives=%lu "
                     "heap=%lu/%lu bytes",
                     (unsigned long)s.frames, secs,
                     secs > 0.0 ? (double)s.frames / secs : 0.0,
                     (unsigned long)s.insns,
                     secs > 0.0 ? (double)s.insns / secs / 1.0e6 : 0.0,
                     (unsigned long)s.native_calls,
                     (unsigned long)s.heap_used, (unsigned long)s.heap_peak);
            port.log(port.user, 2, buf);

            /*
             * What the PORT observed, as opposed to what the VM counted.
             *
             * Pixels cannot tell "saved the score" from "silently did
             * nothing": both are the same sequence of frames.  The headless
             * port records audio and file activity for exactly that reason,
             * and this is the line that lets a script assert on it.
             *
             * -1 means "this port does not report that at all", which is a
             * different answer from 0 ("it reports, and nothing happened").
             * A test that cannot tell those apart passes on a port that was
             * never wired up.
             */
            if (nj_port_headless_file_opens(&port) >= 0 ||
                nj_port_headless_tone_calls(&port) >= 0) {
                snprintf(buf, sizeof(buf),
                         "observed: threads=%u gc=%lu "
                         "tones=%ld pcm_frames=%ld "
                         "file_opens=%ld file_writes=%ld file_reads=%ld "
                         "bytes_written=%ld",
                         (unsigned)s.threads, (unsigned long)s.gc_count,
                         (long)nj_port_headless_tone_calls(&port),
                         (long)nj_port_headless_pcm_frames(&port),
                         (long)nj_port_headless_file_opens(&port),
                         (long)nj_port_headless_file_writes(&port),
                         (long)nj_port_headless_file_reads(&port),
                         (long)nj_port_headless_bytes_written(&port));
                port.log(port.user, 2, buf);
            }
        }
    }
    if (vm) {
        (void)nj_midp_destroy_app(vm, 0);
        if (app) {
            nj_app_close(vm, app);
            app = NULL;
        }
        {
            nj_u4 released = nj_vm_destroy(vm);
            if (port.log && released) {
                char buf[96];
                snprintf(buf, sizeof(buf),
                         "released %lu host blocks at teardown",
                         (unsigned long)released);
                port.log(port.user, 2, buf);
            }
        }
        vm = NULL;
    }
    blob_free(&blob);
    /*
     * The port is NOT shut down here.
     *
     * nj_run() copies the caller's nj_port and does not own it, so tearing it
     * down means the caller's state is freed the moment the run ends - which
     * makes it impossible to ask a port what it observed during the run, and
     * shows up as a use-after-free for anyone who tries.  The caller created
     * the port; the caller shuts it down.
     */
    return rc;
}
