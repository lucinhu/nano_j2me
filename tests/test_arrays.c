/*
 * Multidimensional arrays through real bytecode.
 *
 * tests/arrays/ArrayDims.java builds int[3][7], int[2][4][5] and a ragged
 * int[3][] table, reads every length back, stores into the last element of
 * each row and checks that one past the end throws
 * ArrayIndexOutOfBoundsException.  It returns 1 only when all of that holds,
 * so the C side only has to run its static method and read the result.
 *
 * Why bytecode: a C helper would test nj_new_array(), while the shape the
 * paced corpus hits - a[d][6] inside game code - depends on multianewarray,
 * the nested length bookkeeping and the array store bounds check.
 */
#include <stdio.h>
#include <string.h>

#include "nj2me/nj.h"

#include "nj_internal.h"

#include "arrays/arrays_fixture.h"

static int g_pass;
static int g_fail;

#define OK(cond, msg)                                                          \
    do {                                                                       \
        if (cond) {                                                            \
            g_pass++;                                                          \
        } else {                                                               \
            g_fail++;                                                          \
            printf("FAIL %d: %s\n", __LINE__, (msg));                          \
        }                                                                      \
    } while (0)

int main(void)
{
    nj_port port;
    nj_vm *vm;
    nj_value ret;
    nj_class *cls;

    printf("== nano_j2me multidimensional array test ==\n");

    memset(&port, 0, sizeof(port));
    if (nj_port_headless(&port) != NJ_OK) {
        fprintf(stderr, "cannot start the headless port\n");
        return 2;
    }
    vm = nj_vm_create(&port, 2u * 1024u * 1024u);
    if (!vm) {
        fprintf(stderr, "cannot create the VM\n");
        nj_port_shutdown(&port);
        return 2;
    }

    cls = nj_class_load(vm, "ArrayDims", arraydims_ArrayDims_class,
                        (nj_u4)sizeof(arraydims_ArrayDims_class));
    OK(cls != NULL, "ArrayDims loads");
    if (cls) {
        ret.i = 0;
        OK(nj_call_static(vm, "ArrayDims", "check", "()I", NULL, &ret) == NJ_OK,
           "ArrayDims.check() runs");
        OK(ret.i == 1,
           "nested and ragged array lengths, stores and the bounds check hold");
    }

    nj_vm_destroy(vm);
    nj_port_shutdown(&port);

    printf("%d passed, %d failed\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
