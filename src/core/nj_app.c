/*
 * nano_j2me - nj_app.c
 *
 * JAR-backed application: manifest parsing, class lookup and resource access.
 *
 * The app doubles as the VM's nj_class_source, so once it is installed the
 * class loader can pull any class file straight out of the archive without the
 * VM knowing anything about ZIP.
 */
#include "nj_internal.h"
#include "nj2me/nj_app.h"
#include "nj2me/nj_zip.h"

#if NJ_USE_LIBC
#  include <string.h>
#endif

/* ------------------------------------------------------------------ */
/* Helpers                                                             */
/* ------------------------------------------------------------------ */
static void copy_str(char *dst, nj_u4 cap, const char *src)
{
    nj_u4 i = 0u;

    if (!cap) {
        return;
    }
    if (!src) {
        dst[0] = '\0';
        return;
    }
    while (src[i] && i + 1u < cap) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

/* Trim leading and trailing spaces and control characters in place. */
static void trim_in_place(char *s)
{
    char *p = s;
    nj_u4 n;

    while (*p == ' ' || *p == '\t') {
        p++;
    }
    if (p != s) {
        nj_u4 i = 0u;
        while (p[i]) {
            s[i] = p[i];
            i++;
        }
        s[i] = '\0';
    }
    n = (nj_u4)strlen(s);
    while (n > 0u && (s[n - 1u] == ' ' || s[n - 1u] == '\t' ||
                      s[n - 1u] == '\r' || s[n - 1u] == '\n')) {
        s[--n] = '\0';
    }
}

/*
 * MIDlet-1 has the form "  Display Name , /icon.png , com.foo.Bar  ".
 * Any of the three fields may be empty.
 */
static void parse_midlet_attr(nj_app *app, const char *value, int index)
{
    char buf[NJ_APP_CLASS_MAX * 2];
    char *c1, *c2;

    copy_str(buf, (nj_u4)sizeof(buf), value);
    trim_in_place(buf);

    c1 = strchr(buf, ',');
    if (c1) {
        *c1++ = '\0';
        c2 = strchr(c1, ',');
        if (c2) {
            *c2++ = '\0';
        }
    } else {
        c2 = NULL;
    }

    {
        char *name = buf;
        char *icon = c1 ? c1 : (char *)"";
        char *cls = c2 ? c2 : (char *)"";
        trim_in_place(name);
        trim_in_place(icon);
        trim_in_place(cls);

        if (cls[0] == '/') {
            cls++;
        }
        {
            /* Manifest class names use dots; the VM wants slashes only for
             * lookup, and accepts either, so store the dotted form. */
            nj_u4 i;
            for (i = 0u; cls[i]; i++) {
                if (cls[i] == '/') {
                    cls[i] = '.';
                }
            }
        }
        if (index == 0) {
            copy_str(app->midlet_name, sizeof(app->midlet_name), name);
            copy_str(app->midlet_icon, sizeof(app->midlet_icon), icon);
            copy_str(app->midlet_class, sizeof(app->midlet_class), cls);
        } else if (index < 5) {
            copy_str(app->midlet_extra[index - 1], NJ_APP_CLASS_MAX, cls);
        }
        if (cls[0]) {
            app->midlet_count++;
        }
    }
}

/* ------------------------------------------------------------------ */
/* Class source                                                        */
/* ------------------------------------------------------------------ */
static nj_status app_load_class(nj_vm *vm, void *ud, const char *name,
                                nj_u1 **out_bytes, nj_u4 *out_len)
{
    nj_app *app = (nj_app *)ud;
    char path[NJ_APP_CLASS_MAX + 8];
    const nj_zip_entry *e;
    nj_u4 n = 0u;
    nj_u1 *buf;
    nj_s4 got;

    if (!app || !app->zip || !name) {
        return NJ_ERR;
    }
    while (name[n] && n + 8u < (nj_u4)sizeof(path)) {
        path[n] = name[n];
        n++;
    }
    path[n] = '\0';
    {
        const char *suffix = ".class";
        nj_u4 k = 0u;
        while (suffix[k] && n + 1u < (nj_u4)sizeof(path)) {
            path[n++] = suffix[k++];
        }
        path[n] = '\0';
    }

    e = nj_zip_find(app->zip, path);
    if (!e) {
        e = nj_zip_find_ci(app->zip, path);
    }
    if (!e) {
        return NJ_ERR;   /* class genuinely absent from the archive */
    }
    got = nj_zip_extract(app->zip, e, NULL, 0u);
    if (got <= 0) {
        return NJ_ERR_FORMAT;
    }
    buf = (nj_u1 *)nj_host_alloc(vm, (nj_size)got);
    if (!buf) {
        return NJ_ERR_NOMEM;
    }
    got = nj_zip_extract(app->zip, e, buf, (nj_u4)got);
    if (got < 0) {
        nj_host_free(vm, buf);
        return NJ_ERR_FORMAT;
    }
    *out_bytes = buf;
    *out_len = (nj_u4)got;
    return NJ_OK;
}

/* ------------------------------------------------------------------ */
/* Open / close                                                        */
/* ------------------------------------------------------------------ */
nj_app *nj_app_open(nj_vm *vm, const void *data, nj_u4 size)
{
    nj_app *app;

    if (!vm || !data || size < 22u) {
        return NULL;
    }
    app = (nj_app *)nj_host_alloc(vm, sizeof(nj_app));
    if (!app) {
        return NULL;
    }
    app->data = data;
    app->size = size;

    /* NULL selects malloc/free inside nj_zip; every host allocation the VM
     * makes is tracked anyway, so nj_vm_destroy() releases it on every path. */
    app->zip = nj_zip_open(data, size, NULL);
    if (!app->zip) {
        nj_host_free(vm, app);
        return NULL;
    }

    /* ---- manifest ---- */
    {
        const nj_zip_entry *e = nj_zip_find(app->zip, "META-INF/MANIFEST.MF");
        if (!e) {
            e = nj_zip_find_ci(app->zip, "META-INF/MANIFEST.MF");
        }
        if (e && e->uncomp_size > 0u && e->uncomp_size < 65536u) {
            char *mf = (char *)nj_host_alloc(vm, (nj_size)e->uncomp_size + 1u);
            if (mf) {
                nj_s4 got = nj_zip_extract(app->zip, e, mf, e->uncomp_size);
                if (got >= 0) {
                    char v[NJ_APP_CLASS_MAX * 2];
                    mf[got] = '\0';
                    if (nj_manifest_get(mf, (nj_u4)got, "MIDlet-1", v,
                                        (nj_u4)sizeof(v)) == NJ_OK) {
                        parse_midlet_attr(app, v, 0);
                    }
                    if (nj_manifest_get(mf, (nj_u4)got, "MIDlet-Name", v,
                                        (nj_u4)sizeof(v)) == NJ_OK) {
                        trim_in_place(v);
                        copy_str(app->jar_name, sizeof(app->jar_name), v);
                    }
                    if (nj_manifest_get(mf, (nj_u4)got, "MIDlet-Vendor", v,
                                        (nj_u4)sizeof(v)) == NJ_OK) {
                        trim_in_place(v);
                        copy_str(app->jar_vendor, sizeof(app->jar_vendor), v);
                    }
                    if (nj_manifest_get(mf, (nj_u4)got, "MIDlet-Version", v,
                                        (nj_u4)sizeof(v)) == NJ_OK) {
                        trim_in_place(v);
                        copy_str(app->jar_version, sizeof(app->jar_version), v);
                    }
                    if (nj_manifest_get(mf, (nj_u4)got, "MicroEdition-Profile", v,
                                        (nj_u4)sizeof(v)) == NJ_OK) {
                        trim_in_place(v);
                        copy_str(app->profile, sizeof(app->profile), v);
                    }
                    if (nj_manifest_get(mf, (nj_u4)got, "MicroEdition-Configuration", v,
                                        (nj_u4)sizeof(v)) == NJ_OK) {
                        trim_in_place(v);
                        copy_str(app->configuration, sizeof(app->configuration), v);
                    }
                }
                nj_host_free(vm, mf);
            }
        }
    }

    /* ---- install as the class source ---- */
    {
        nj_class_source *src = (nj_class_source *)nj_host_alloc(
            vm, sizeof(nj_class_source));
        if (!src) {
            nj_app_close(vm, app);
            return NULL;
        }
        src->load = app_load_class;
        src->ud = app;
        vm->source = src;
    }
    vm->loader_ud = app;
    return app;
}

void nj_app_close(nj_vm *vm, nj_app *app)
{
    if (!vm || !app) {
        return;
    }
    if (vm->source && vm->source->ud == app) {
        nj_host_free(vm, vm->source);
        vm->source = NULL;
    }
    if (vm->loader_ud == app) {
        vm->loader_ud = NULL;
    }
    if (app->zip) {
        nj_zip_close(app->zip);
        app->zip = NULL;
    }
    nj_host_free(vm, app);
}

/* ------------------------------------------------------------------ */
/* Resources                                                           */
/* ------------------------------------------------------------------ */
int nj_app_has_resource(nj_app *app, const char *name)
{
    const char *n = name;

    if (!app || !app->zip || !name) {
        return 0;
    }
    if (*n == '/') {
        n++;
    }
    if (nj_zip_find(app->zip, n)) {
        return 1;
    }
    return nj_zip_find_ci(app->zip, n) != NULL;
}

void *nj_app_resource(nj_vm *vm, nj_app *app, const char *name, nj_u4 *out_len)
{
    const nj_zip_entry *e;
    const char *n = name;
    nj_u4 want;
    nj_u1 *buf;

    if (out_len) {
        *out_len = 0u;
    }
    if (!vm || !app || !app->zip || !name) {
        return NULL;
    }
    if (*n == '/') {
        n++;
    }
    e = nj_zip_find(app->zip, n);
    if (!e) {
        e = nj_zip_find_ci(app->zip, n);
    }
    if (!e || e->uncomp_size == 0u) {
        return NULL;
    }
    want = e->uncomp_size;
    buf = (nj_u1 *)nj_host_alloc(vm, (nj_size)want + 1u);
    if (!buf) {
        return NULL;
    }
    if (nj_zip_extract(app->zip, e, buf, want) < 0) {
        nj_host_free(vm, buf);
        return NULL;
    }
    buf[want] = 0u;
    if (out_len) {
        *out_len = want;
    }
    return buf;
}
