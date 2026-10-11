/*
 * nano_j2me - tools/nj_inspect.c
 *
 * A class-loader exerciser: open a JAR, load and link EVERY class in it, and
 * report what the parser, the linker and the constant-pool resolver made of it.
 *
 * This is the compatibility gate for the front half of the runtime.  Real
 * 2004-era MIDlets were produced by javac 1.4 targeting CLDC 1.0, use the
 * legacy jsr/ret for finally, and carry obfuscated names; if the loader
 * survives a whole archive of them without a parse error or a bad resolve,
 * the failure modes left are API gaps, not parser bugs.
 *
 * It deliberately loads every class rather than only the MIDlet: an archive
 * whose entry points work but whose menu screens are broken is still a broken
 * game, and only loading everything catches that.
 *
 * Usage: nj_inspect <game.jar> [--verbose]
 */
#include "nj_internal.h"
#include "nj2me/nj.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned char *read_file(const char *path, nj_u4 *out_len)
{
    FILE *f = fopen(path, "rb");
    long n;
    unsigned char *buf;

    if (!f) {
        return NULL;
    }
    if (fseek(f, 0, SEEK_END) != 0 || (n = ftell(f)) <= 0) {
        fclose(f);
        return NULL;
    }
    rewind(f);
    buf = (unsigned char *)malloc((size_t)n);
    if (!buf) {
        fclose(f);
        return NULL;
    }
    if (fread(buf, 1u, (size_t)n, f) != (size_t)n) {
        free(buf);
        fclose(f);
        return NULL;
    }
    fclose(f);
    *out_len = (nj_u4)n;
    return buf;
}

/* A ClassSource that walks the archive's entry list rather than being asked. */
typedef struct walk {
    nj_app *app;
    int     verbose;
    int     loaded;
    int     failed;
    int     methods;
    int     fields;
    int     natives;
} walk;

static int g_parse_only;

static void walk_class(nj_vm *vm, walk *w, const char *entry_name)
{
    char cls_name[256];
    nj_u4 n = 0u;
    const char *p = entry_name;
    nj_class *c;

    /* "net/frog_parrot/jump/Jump.class" -> "net/frog_parrot/jump/Jump" */
    while (*p && n + 1u < (nj_u4)sizeof(cls_name)) {
        if (strncmp(p, ".class", 6u) == 0 && p[6] == '\0') {
            break;
        }
        cls_name[n++] = *p++;
    }
    cls_name[n] = '\0';

    c = nj_class_resolve(vm, cls_name);
    if (!c) {
        w->failed++;
        printf("  FAIL load   %s\n", cls_name);
        nj_clear_exception(vm);
        return;
    }
    if (!g_parse_only && nj_class_link(vm, c) != NJ_OK) {
        w->failed++;
        printf("  FAIL link   %s\n", cls_name);
        nj_clear_exception(vm);
        return;
    }
    w->loaded++;
    w->methods += (int)c->method_count;
    w->fields += (int)c->field_count;
    if (w->verbose) {
        printf("  ok  %-52s super=%-28s m=%-3u f=%-3u size=%u\n",
               cls_name,
               c->super ? c->super->name : "(none)",
               (unsigned)c->method_count, (unsigned)c->field_count,
               (unsigned)c->instance_size);
    }
}

int main(int argc, char **argv)
{
    unsigned char *jar;
    nj_u4 jar_len = 0u;
    nj_port port;
    nj_vm *vm;
    nj_app *app;
    walk w;
    int verbose = 0;
    int i;
    int entries;
    nj_u4 released;

    if (argc < 2) {
        fprintf(stderr, "usage: %s <game.jar> [--verbose]\n", argv[0]);
        return 2;
    }
    if (argc >= 3 && strcmp(argv[2], "--verbose") == 0) {
        verbose = 1;
    }
    /* --parse-only isolates the class-file parser from missing APIs: every
     * class is parsed but not linked, so a failure here is a parser bug while
     * a failure only in the linked run is an unimplemented class. */
    for (i = 2; i < argc; i++) {
        if (strcmp(argv[i], "--parse-only") == 0) {
            g_parse_only = 1;
        }
    }

    jar = read_file(argv[1], &jar_len);
    if (!jar) {
        fprintf(stderr, "cannot read %s\n", argv[1]);
        return 2;
    }

    if (nj_port_headless_ex(&port, 240, 320, 0, NULL) != NJ_OK) {
        fprintf(stderr, "port\n");
        free(jar);
        return 2;
    }

    vm = nj_vm_create(&port, 8u * 1024u * 1024u);
    if (vm) {
        vm->verbose = verbose ? 3u : 2u;
    }
    if (!vm) {
        fprintf(stderr, "vm\n");
        free(jar);
        return 2;
    }
    app = nj_app_open(vm, jar, jar_len);
    if (!app) {
        fprintf(stderr, "not a usable MIDlet suite\n");
        nj_vm_destroy(vm);
        free(jar);
        return 1;
    }

    printf("== %s ==\n", argv[1]);
    printf("archive : %u bytes\n", (unsigned)jar_len);
    printf("name    : %s\n", app->jar_name);
    printf("vendor  : %s\n", app->jar_vendor);
    printf("profile : %s / %s\n", app->configuration, app->profile);
    printf("midlets : %d\n", app->midlet_count);
    for (i = 0; i < app->midlet_count && i < 5; i++) {
        const char *cn = nj_app_midlet_class(app, i);
        printf("  [%d] %s\n", i, cn ? cn : "?");
    }
    printf("classes in archive:\n");

    memset(&w, 0, sizeof(w));
    w.app = app;
    w.verbose = verbose;

    entries = nj_zip_count((const nj_zip *)app->zip);
    for (i = 0; i < entries; i++) {
        const nj_zip_entry *e =
            nj_zip_entry_at((const nj_zip *)app->zip, i);
        size_t len;
        if (!e) {
            continue;
        }
        len = strlen(e->name);
        if (len > 6u && strcmp(e->name + len - 6u, ".class") == 0) {
            walk_class(vm, &w, e->name);
        }
    }

    printf("\nloaded  : %d class(es), %d methods, %d fields\n",
           w.loaded, w.methods, w.fields);
    printf("failed  : %d\n", w.failed);

    /* Resolve the MIDlet itself and its <init>, the way the runner would. */
    if (w.failed == 0 && app->midlet_class[0]) {
        nj_ref exc = NULL;
        nj_ref m = nj_vm_new_midlet(vm, app->midlet_class, &exc);
        if (m) {
            printf("midlet  : %s instantiated\n", app->midlet_class);
        } else {
            printf("midlet  : %s FAILED to instantiate (%s)\n",
                   app->midlet_class, nj_ref_class_name(exc));
            nj_clear_exception(vm);
            w.failed++;
        }
    }

    nj_app_close(vm, app);
    released = nj_vm_destroy(vm);
    printf("teardown: %lu host blocks released\n", (unsigned long)released);
    nj_port_shutdown(&port);
    free(jar);

    if (w.failed != 0) {
        printf("\nFAIL: %d class(es) could not be loaded\n", w.failed);
        return 1;
    }
    printf("\nPASS: every class parsed, linked and (for the MIDlet) "
           "instantiated\n");
    return 0;
}
