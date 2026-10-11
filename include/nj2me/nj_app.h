/*
 * nano_j2me - nj_app.h
 *
 * A loaded application: one JAR (or an in-memory equivalent) plus its
 * manifest.  The app also acts as the VM's class source, so the core never
 * knows whether classes come from a JAR, a flash partition or a test fixture.
 */
#ifndef NJ_APP_H
#define NJ_APP_H

#include "nj_types.h"
#include "nj_vm.h"

#ifdef __cplusplus
extern "C" {
#endif

#define NJ_APP_NAME_MAX   64
#define NJ_APP_CLASS_MAX 160
#define NJ_APP_ICON_MAX  160

typedef struct nj_app {
    const void *data;
    nj_u4       size;

    void       *zip;          /* opaque nj_zip* */

    char midlet_name[NJ_APP_NAME_MAX];
    char midlet_class[NJ_APP_CLASS_MAX];
    char midlet_icon[NJ_APP_ICON_MAX];
    char midlet_extra[4][NJ_APP_CLASS_MAX];  /* MIDlet-2..MIDlet-5 */

    char jar_name[NJ_APP_NAME_MAX];
    char jar_vendor[NJ_APP_NAME_MAX];
    char jar_version[32];
    char profile[64];
    char configuration[64];

    int  midlet_count;
} nj_app;

/*
 * Open an application.  `data` must stay valid for the app's lifetime; the
 * reader never copies the archive.  Returns NULL and raises a Java exception
 * when the JAR is unreadable or has no usable MIDlet-1 entry.
 */
nj_app *nj_app_open(nj_vm *vm, const void *data, nj_u4 size);

/* Release the app.  Also clears vm->source if it points at this app. */
void nj_app_close(nj_vm *vm, nj_app *app);

/* Extract a named resource ("/res/logo.png" or "res/logo.png").
 * Returns a host allocation the caller frees with nj_host_free(). */
void *nj_app_resource(nj_vm *vm, nj_app *app, const char *name, nj_u4 *out_len);

/* Does the archive contain this entry? */
int nj_app_has_resource(nj_app *app, const char *name);

#ifdef __cplusplus
}
#endif

#endif /* NJ_APP_H */
