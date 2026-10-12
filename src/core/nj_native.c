/*
 * nano_j2me - nj_native.c
 *
 * The native method registry.
 *
 * Each class-library translation unit exposes a `const nj_native_entry[]`
 * table; this file aggregates them (the aggregation table is itself `const`,
 * so it lives in .rodata and costs no RAM) and builds a per-VM open-addressed
 * hash.  The hash belongs to the nj_vm instance and is released by
 * nj_natives_shutdown(), which is what keeps the "no mutable global state"
 * rule intact.
 */
#include "nj_internal.h"

#if NJ_USE_LIBC
#  include <string.h>
#endif

/* ------------------------------------------------------------------ */
/* Aggregation of every class-library binding table                    */
/* ------------------------------------------------------------------ */
typedef struct nj_native_table {
    const nj_native_entry *entries;
    const int             *count;
} nj_native_table;

/*
 * Tables are listed unconditionally; a module that is compiled out simply
 * provides an empty table (its count is 0), so this list stays const and
 * feature-independent.
 */
static const nj_native_table nj_tables[] = {
    { nj_lib_object_table,  &nj_lib_object_count  },
    { nj_lib_string_table,  &nj_lib_string_count  },
    { nj_lib_system_table,  &nj_lib_system_count  },
    { nj_lib_math_table,    &nj_lib_math_count    },
    { nj_lib_thread_table,  &nj_lib_thread_count  },
    { nj_lib_boxing_table,  &nj_lib_boxing_count  },
    { nj_lib_util_table,    &nj_lib_util_count    },
    { nj_lib_io_table,      &nj_lib_io_count      },
    { nj_lib_lcdui_table,   &nj_lib_lcdui_count   },
    { nj_lib_game_table,    &nj_lib_game_count    },
    { nj_lib_rms_table,     &nj_lib_rms_count     },
    { nj_lib_midlet_table,  &nj_lib_midlet_count  },
    { nj_lib_media_table,   &nj_lib_media_count   },
    { nj_lib_content_table, &nj_lib_content_count },
    { nj_lib_nokia_table,   &nj_lib_nokia_count   },
    { nj_lib_wireless_table, &nj_lib_wireless_count },
    { nj_lib_m3g_table, &nj_lib_m3g_count },
    { nj_lib_mascot_table, &nj_lib_mascot_count },
    { nj_lib_fc_table,      &nj_lib_fc_count      }
};
#define NJ_NTABLES ((int)(sizeof(nj_tables) / sizeof(nj_tables[0])))

/* ------------------------------------------------------------------ */
/* Hash                                                                */
/* ------------------------------------------------------------------ */
static nj_u4 native_hash(const char *cls, const char *name, const char *desc)
{
    nj_u4 h = 2166136261u;
    const char *parts[3];
    int i;

    parts[0] = cls;
    parts[1] = name;
    parts[2] = desc;
    for (i = 0; i < 3; i++) {
        const char *p = parts[i];
        while (p && *p) {
            h ^= (nj_u1)*p++;
            h *= 16777619u;
        }
        h ^= 0x1Fu;
        h *= 16777619u;
    }
    return h;
}

static nj_u2 next_pow2(nj_u2 v)
{
    nj_u2 r = 8u;
    while (r < v && r < 16384u) {
        r = (nj_u2)(r * 2u);
    }
    return r;
}

static nj_native_binding *native_slot(nj_vm *vm, const char *cls,
                                      const char *name, const char *desc)
{
    nj_u4 mask = (nj_u4)vm->native_cap - 1u;
    nj_u4 h = native_hash(cls, name, desc) & mask;
    nj_u4 probes = 0u;

    while (probes++ <= mask) {
        nj_native_binding *b = &vm->natives[h];
        if (!b->cls) {
            return b;
        }
        if (strcmp(b->cls, cls) == 0 && strcmp(b->name, name) == 0 &&
            strcmp(b->desc, desc) == 0) {
            return b;
        }
        h = (h + 1u) & mask;
    }
    return NULL;
}

static nj_status natives_insert(nj_vm *vm, const char *cls, const char *name,
                                const char *desc, nj_native_fn fn)
{
    nj_native_binding *b = native_slot(vm, cls, name, desc);
    if (!b) {
        return NJ_ERR_NOMEM;
    }
    if (b->cls) {
        return NJ_OK;   /* already bound: first definition wins */
    }
    b->cls = cls;
    b->name = name;
    b->desc = desc;
    b->fn = fn;
    vm->nnatives++;
    return NJ_OK;
}

nj_status nj_natives_init(nj_vm *vm)
{
    nj_u2 total = 0u;
    int t;

    for (t = 0; t < NJ_NTABLES; t++) {
        int n = *nj_tables[t].count;
        if (n > 0) {
            total = (nj_u2)(total + (nj_u2)n);
        }
    }
    vm->native_cap = next_pow2((nj_u2)(total * 2u + 16u));
    vm->natives = (nj_native_binding *)nj_host_alloc(
        vm, (nj_size)vm->native_cap * sizeof(nj_native_binding));
    if (!vm->natives) {
        return NJ_ERR_NOMEM;
    }
    vm->nnatives = 0u;

    for (t = 0; t < NJ_NTABLES; t++) {
        const nj_native_entry *e = nj_tables[t].entries;
        int n = *nj_tables[t].count;
        int i;
        for (i = 0; i < n; i++) {
            if (!e[i].cls || !e[i].fn) {
                continue;
            }
            (void)natives_insert(vm, e[i].cls, e[i].name, e[i].desc, e[i].fn);
        }
    }
    return NJ_OK;
}

void nj_natives_shutdown(nj_vm *vm)
{
    if (vm->natives) {
        nj_host_free(vm, vm->natives);
        vm->natives = NULL;
    }
    vm->nnatives = 0u;
    vm->native_cap = 0u;
}

nj_native_fn nj_native_find(nj_vm *vm, const char *cls, const char *name,
                            const char *desc)
{
    nj_native_binding *b;

    if (!vm || !vm->natives || !cls || !name || !desc) {
        return NULL;
    }
    b = native_slot(vm, cls, name, desc);
    if (b && b->cls) {
        vm->native_hits++;
        return b->fn;
    }
    vm->native_misses++;
    return NULL;
}
