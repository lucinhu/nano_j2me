/*
 * nano_j2me - nj_builtins.c
 *
 * Materialises every C-implemented class (the "class library") and runs the
 * late static initialisation.
 *
 * The aggregation table below is `const`, so it is placed in .rodata/flash and
 * costs no RAM.  All mutable state produced here hangs off the nj_vm instance.
 */
#include "nj_internal.h"

typedef struct nj_builtin_source {
    const nj_builtin_class *classes;
    const int              *count;
} nj_builtin_source;

static const nj_builtin_source nj_sources[] = {
    { nj_lib_object_classes, &nj_lib_object_class_count },
    { nj_lib_string_classes, &nj_lib_string_class_count },
    { nj_lib_system_classes, &nj_lib_system_class_count },
    { nj_lib_math_classes,   &nj_lib_math_class_count   },
    { nj_lib_thread_classes, &nj_lib_thread_class_count },
    { nj_lib_boxing_classes, &nj_lib_boxing_class_count },
    { nj_lib_util_classes,   &nj_lib_util_class_count   },
    { nj_lib_io_classes,     &nj_lib_io_class_count     },
    { nj_lib_lcdui_classes,  &nj_lib_lcdui_class_count  },
    { nj_lib_game_classes,   &nj_lib_game_class_count   },
    { nj_lib_rms_classes,    &nj_lib_rms_class_count    },
    { nj_lib_midlet_classes, &nj_lib_midlet_class_count }
};
#define NJ_NSOURCES ((int)(sizeof(nj_sources) / sizeof(nj_sources[0])))

void nj_builtins_register_all(nj_vm *vm)
{
    int s;
    int pass;

    if (!vm) {
        return;
    }

    /* Pass 1: create every class record so that cross-references resolve. */
    for (s = 0; s < NJ_NSOURCES; s++) {
        const nj_builtin_class *cs = nj_sources[s].classes;
        int n = *nj_sources[s].count;
        int i;
        for (i = 0; i < n; i++) {
            nj_class *c = nj_class_create_builtin(vm, &cs[i]);
            if (!c) {
                nj_logf(vm, 0, "cannot create builtin class %s", cs[i].name);
                continue;
            }
            if (vm->nbuiltins >= vm->builtin_cap) {
                nj_u2 cap = vm->builtin_cap ? (nj_u2)(vm->builtin_cap * 2u) : 64u;
                nj_class **nb = (nj_class **)nj_host_alloc(
                    vm, (nj_size)cap * sizeof(nj_class *));
                if (!nb) {
                    return;
                }
                if (vm->builtins) {
                    nj_u4 k;
                    for (k = 0u; k < vm->nbuiltins; k++) {
                        nb[k] = vm->builtins[k];
                    }
                    nj_host_free(vm, vm->builtins);
                }
                vm->builtins = nb;
                vm->builtin_cap = cap;
            }
            vm->builtins[vm->nbuiltins++] = c;
        }
    }

    /*
     * Pass 2: link repeatedly until the graph settles.  Linking is idempotent
     * and returns early once a class is linked, so the extra passes simply
     * pick up classes whose superclass had not been created yet in pass 1.
     */
    for (pass = 0; pass < 4; pass++) {
        nj_u4 i;
        int progress = 0;
        for (i = 0u; i < vm->nbuiltins; i++) {
            nj_class *c = vm->builtins[i];
            if (c && c->state < NJ_CLASS_LINKED) {
                if (nj_class_link(vm, c) == NJ_OK) {
                    progress = 1;
                }
            }
        }
        if (!progress) {
            break;
        }
    }

    /* Object must always exist: the interpreter and GC assume it. */
    if (!vm->cls_object) {
        nj_logf(vm, 0, "builtin class library is missing java/lang/Object");
    }
}
