/*
 * tests/tmp_game_stubs.c
 *
 * ============================================================================
 * THROWAWAY PLACEHOLDERS -- NOT PART OF THE RUNTIME.  DELETE WHEN UNUSED.
 * ============================================================================
 *
 * src/midp/nj_lib_io.c, nj_lib_rms.c and nj_lib_util.c belong to other modules
 * and were being written while the game package was verified.  The game package
 * uses none of them, so this file contributes their (empty) class and native
 * tables to the link, exactly as their own placeholders did before they were
 * implemented.  It exists only so that a verification run of
 * src/midp/nj_lib_game.c cannot fail because of an unrelated module's
 * edit-in-progress state.
 *
 * The authoritative run links the real files instead.
 */
#include "nj_internal.h"

#define EMPTY_CLASSES(name)                                                   \
    const nj_builtin_class nj_lib_##name##_classes[1] = {                     \
        { NULL, NULL, 0u, NULL, 0u, NULL, 0u }                                \
    };                                                                        \
    const int nj_lib_##name##_class_count = 0;                                \
    const nj_native_entry nj_lib_##name##_table[1] = {                        \
        { NULL, NULL, NULL, NULL }                                            \
    };                                                                        \
    const int nj_lib_##name##_count = 0

EMPTY_CLASSES(io);
EMPTY_CLASSES(rms);
EMPTY_CLASSES(util);
