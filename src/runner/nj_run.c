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

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

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

    if (!port_in || !opts) {
        return 2;
    }
    port = *port_in;
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
            char buf[256];
            const char *n = nj_ref_class_name(exc);
            snprintf(buf, sizeof(buf), "cannot instantiate %s: %s",
                     class_name, n);
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
        nj_ref e = nj_take_exception(vm);
        if (port.log) {
            char buf[256];
            snprintf(buf, sizeof(buf), "startApp() threw %s",
                     nj_ref_class_name(e));
            port.log(port.user, 0, buf);
        }
    }

    /* ---- the frame loop ---- */
    {
        nj_status st = nj_midp_run(vm);
        if (st != NJ_OK) {
            if (port.log) {
                port.log(port.user, 0, "the display loop failed");
            }
            goto done;
        }
    }

    rc = (int)nj_vm_exit_code(vm);

done:
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
    nj_port_shutdown(&port);
    return rc;
}
