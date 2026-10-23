/*
 * Mascot Capsule Micro3D v3: Graphics3D.drawCommandList() interpreter.
 *
 * This file is included by src/midp/nj_lib_mascot.c after
 * "nj_mascot_scene.inc", inside the same translation unit, so it can use the
 * scene layer (m3_scene_setup, m3_queue, m3_transform_vertex, clip helpers)
 * without exporting anything and without adding state to nj_vm or to any
 * Java object.  It adds no writable globals and allocates no buffers: a
 * command list is read directly out of the caller's int[] and the primitives
 * are queued through the existing Graphics3D scene queue, then rendered by
 * the existing Graphics3D.flush().
 *
 * Reference: FreeJ2ME-Plus MIT source, Graphics3D.drawCommandList()
 * (com/mascotcapsule/micro3d/v3/Graphics3D.java in the local checkout).
 *
 * COMMAND LIST FORMAT (all words are Java ints)
 * --------------------------------------------
 *   word 0        COMMAND_LIST_VERSION_1_0 (0xFE000001)
 *   then commands until the end of the array:
 *     0x80000000  END         stop interpreting; queued primitives stay
 *                             queued until the app calls flush()
 *     0x81000000  NOP         skip (cmd & 0xFFFFFF) words
 *     0x82000000  FLUSH       render everything queued so far
 *     ... other command words (see below)
 *     0x01..0x05  primitive data: bit 24..31 type, bits 16..23 number of
 *                             primitives, then as many payload words as the
 *                             flags say.
 *
 * The primitive payload starts at the word right AFTER the command word.
 * The reference worker reads `int vtxOffset = idx++;`: the post-increment
 * makes vtxOffset the index of the first vertex word and there is NO stored
 * offset word in the array (the ++ merely steps the cursor before the
 * derived offsets are computed).  Fragment offsets are derived from the
 * primitive count and the PDATA_* flag bits, exactly like the reference.
 * The layout for lines (the form Galaxy on Fire emits) is:
 *   vertices                    numPrims * 2 * 3 ints (first word is x0)
 *   normals                     PDATA_NORMAL_PER_FACE: numPrims*3
 *                               PDATA_NORMAL_PER_VERTEX: numVtx*3
 *   textureCoords (if 0x3000)   numVtx * 2 ints
 *   colors                      PDATA_COLOR_PER_COMMAND: 1
 *                               PDATA_COLOR_PER_FACE: numPrims
 *
 * SCOPE OF THIS FIRST VERSION
 * ---------------------------
 * Implemented: version check, END, NOP, FLUSH, and LINES with per-command or
 * per-face colours (the only command list form Galaxy on Fire submits: one
 * LINES batch and END).  Everything else is REJECTED loudly with
 * UnsupportedOperationException instead of drawing a wrong picture: POINTS,
 * TRIANGLES, QUADS, POINT_SPRITES, CENTER, TEXTURE_INDEX, AFFINE_INDEX,
 * PARALLEL_SCALE/SIZE, PERSPECTIVE_FOV/WH, ATTRIBUTE, CLIP, AMBIENT_LIGHT,
 * DIRECTION_LIGHT and THRESHOLD.  Extending this file is additive: each
 * command gets its own decode branch and the payload offsets are already
 * computed as the reference computes them.
 *
 * Validation order follows the reference where it is observable:
 * null layout/effect/commandList -> NullPointerException;
 * no bound Graphics -> IllegalStateException;
 * bad version -> IllegalArgumentException;
 * out-of-range payload/offset -> ArrayIndexOutOfBoundsException;
 * unknown command word -> IllegalArgumentException.
 */

/* Command words, from Graphics3D.java (only the ones used here are listed;
 * the rest are recognised so the error can say "not implemented" rather
 * than "unknown command"). */
#define M3_CMD_VERSION_1_0    0xFE000001u
#define M3_CMD_END            0x80000000u
#define M3_CMD_NOP            0x81000000u
#define M3_CMD_FLUSH          0x82000000u
#define M3_CMD_ATTRIBUTE      0x83000000u
#define M3_CMD_CLIP           0x84000000u
#define M3_CMD_CENTER         0x85000000u
#define M3_CMD_TEXTURE_INDEX  0x86000000u
#define M3_CMD_AFFINE_INDEX   0x87000000u
#define M3_CMD_PARALLEL_SCALE 0x90000000u
#define M3_CMD_PARALLEL_SIZE  0x91000000u
#define M3_CMD_PERSPECTIVE_FOV 0x92000000u
#define M3_CMD_PERSPECTIVE_WH 0x93000000u
#define M3_CMD_AMBIENT_LIGHT  0xA0000000u
#define M3_CMD_DIRECTION_LIGHT 0xA1000000u
#define M3_CMD_THRESHOLD      0xAF000000u

/* Read the next word of a command list, throwing AIOOBE the way Java array
 * indexing would.  `pos` is signed 64-bit only so a runaway NOP skip cannot
 * wrap back into the array on this side of the VM. */
static int m3_cmd_word(nj_vm *vm, const nj_s4 *list, nj_s8 len, nj_s8 *pos,
                       nj_s4 *out)
{
    if (*pos < 0 || *pos >= len) {
        (void)nj_throw(vm, "java/lang/ArrayIndexOutOfBoundsException",
                       "Micro3D command list");
        return 0;
    }
    *out = list[(*pos)++];
    return 1;
}

static int m3_cmd_payload_oob(nj_vm *vm)
{
    (void)nj_throw(vm, "java/lang/ArrayIndexOutOfBoundsException",
                   "Micro3D command list payload");
    return 0;
}

/* True for command words the reference defines, whether or not this version
 * implements them; used only to choose an honest error message. */
static int m3_cmd_known(nj_u4 high)
{
    switch (high) {
    case M3_CMD_END:
    case M3_CMD_NOP:
    case M3_CMD_FLUSH:
    case M3_CMD_ATTRIBUTE:
    case M3_CMD_CLIP:
    case M3_CMD_CENTER:
    case M3_CMD_TEXTURE_INDEX:
    case M3_CMD_AFFINE_INDEX:
    case M3_CMD_PARALLEL_SCALE:
    case M3_CMD_PARALLEL_SIZE:
    case M3_CMD_PERSPECTIVE_FOV:
    case M3_CMD_PERSPECTIVE_WH:
    case M3_CMD_AMBIENT_LIGHT:
    case M3_CMD_DIRECTION_LIGHT:
    case M3_CMD_THRESHOLD:
    case M3_PRIM_POINTS:
    case M3_PRIM_LINES:
    case M3_PRIM_TRIS:
    case M3_PRIM_QUADS:
    case M3_PRIM_SPRITES:
        return 1;
    default:
        return 0;
    }
}

/* Draw one LINES command whose payload starts at *pos, i.e. at the first
 * vertex x word.  The exact reference offset arithmetic is used even when a
 * fragment (normals/UV) is ignored by the line path, so multi-command lists
 * keep decoding in step.  Returns NJ_OK after queueing, NJ_ERR after an
 * exception.
 */
static nj_status m3_cmd_lines(nj_vm *vm, const nj_s4 *list, nj_s8 len,
                              nj_s8 *pos, m3_scene *sc, nj_s4 command)
{
    nj_s4 prime = (command >> 16) & 0xff;      /* number of lines          */
    nj_s8 num_vtx = (nj_s8)prime * 2;
    nj_s8 vtx_offset, vtx_size, norm_size, uv_size, color_offset, color_size;
    nj_s8 end, i;
    nj_s4 normal_type = command & M3_PDATA_NORMAL_MASK;
    nj_s4 color_type = command & M3_PDATA_COLOR_MASK;
    nj_u4 mat;

    /* Reference Graphics3D.java: "int vtxOffset = idx++;".  There is no
     * stored offset word: the first payload word is already x0 of the first
     * vertex.  The post-increment only advances the cursor past it before
     * the derived fragment offsets are computed. */
    vtx_offset = *pos;
    ++*pos;
    vtx_size = num_vtx * 3;
    norm_size = normal_type == 0
                    ? 0
                    : (normal_type == 0x0200
                           ? (nj_s8)prime * 3
                           : (normal_type == 0x0300
                                  ? num_vtx * 3
                                  : 0));
    uv_size = (command & M3_PDATA_UV) ? num_vtx * 2 : 0;
    color_size = color_type == M3_PDATA_COLOR_CMD
                     ? 1
                     : (color_type == M3_PDATA_COLOR_FACE ? (nj_s8)prime : 0);
    color_offset = vtx_offset + vtx_size + norm_size + uv_size;
    end = color_offset + color_size;
    *pos = end;

    /* submitLines() only looks at the colour type; invalid colour is an
     * IllegalArgumentException, "no colour data" draws nothing. */
    if (color_type == M3_PDATA_COLOR_BAD) {
        return nj_throw(vm, "java/lang/IllegalArgumentException",
                        "Invalid Micro3D command-list colour type");
    }
    if (color_type == 0) {
        return NJ_OK;
    }
    /* submitLines() reads exactly the vertex words and the colour words; the
     * normal/UV words may lie past the end of the array without an exception
     * (they are never read), and a cursor beyond len just ends the loop like
     * the reference's "while (idx < length)".  When a fragment is actually
     * read, Java array indexing would throw AIOOBE. */
    if (vtx_offset < 0 || (prime > 0 && vtx_offset + vtx_size > len) ||
        color_offset < 0 || color_offset + color_size > len) {
        return m3_cmd_payload_oob(vm) ? NJ_OK : NJ_ERR;
    }

    mat = NJ_MASCOT_MAT_DOUBLE_FACE | ((nj_u4)(command & 0x60) >> 4) |
          ((nj_u4)(command & 0x10) >> 4);
    if (!sc->semi) {
        mat &= ~6u;
    }

    for (i = 0; i < prime; i++) {
        const nj_s4 *p = list + vtx_offset + i * 6;
        nj_u4 color = 0xff000000u |
                      ((nj_u4)list[color_offset +
                                   (color_type == M3_PDATA_COLOR_CMD ? 0 : i)] &
                       0xffffffu);
        m3_sv v[2]={{0}};
        nj_s4 z;

        m3_transform_vertex(sc->matrix, p, &v[0].x);
        m3_transform_vertex(sc->matrix, p + 3, &v[1].x);
        if (sc->perspective) {
            m3_vtx a, b;

            a.x = v[0].x;
            a.y = v[0].y;
            a.z = v[0].z;
            b.x = v[1].x;
            b.y = v[1].y;
            b.z = v[1].z;
            if (!m3_clip_line(&a, &b, sc->near_z, sc->far_z)) {
                continue;
            }
            v[0].x = (nj_s4)a.x;
            v[0].y = (nj_s4)a.y;
            v[0].z = (nj_s4)a.z;
            v[1].x = (nj_s4)b.x;
            v[1].y = (nj_s4)b.y;
            v[1].z = (nj_s4)b.z;
        }
        z = m3_sort_depth(v, 2, 0);
        if (!m3_queue(sc, &v[0], &v[1], NULL, 2, mat, color, 0, z)) {
            return NJ_ERR;
        }
    }
    return NJ_OK;
}

/* Shared body of the Texture[] and single-Texture overloads.  `array_form`
 * selects which Java signature produced `args`; both put the command list in
 * slot 5 and x/y in slots 1/2, exactly like the reference overloads. */
static nj_status m3_cmd_draw(nj_vm *vm, nj_thread *th, nj_value *args,
                             nj_value *ret, int array_form)
{
    nj_ref self = NJ_THIS(args);
    nj_ref layout = NJ_ARG_REF(args, 3);
    nj_ref effect = NJ_ARG_REF(args, 4);
    nj_ref list = NJ_ARG_REF(args, 5);
    nj_s4 x = NJ_ARG_INT(args, 1);
    nj_s4 y = NJ_ARG_INT(args, 2);
    m3_scene sc;
    const nj_s4 *words;
    nj_s8 len, pos = 0;
    nj_s4 command, version;

    (void)array_form; /* both overloads share the same interpretation; the
                       * texture argument only matters to textured commands,
                       * which this version rejects. */

    if (!layout || !effect || !list) {
        return nj_throw(vm, "java/lang/NullPointerException",
                        "Micro3D command list");
    }
    if (!m3_ref(vm, self, "_g", "Ljavax/microedition/lcdui/Graphics;")) {
        return nj_throw(vm, "java/lang/IllegalStateException",
                        "no Graphics bound");
    }
    if (list->kind != NJ_OBJ_ARRAY || list->aflags != NJ_ARRAY_T_INT) {
        return nj_throw(vm, "java/lang/IllegalArgumentException",
                        "Micro3D command list is not an int[]");
    }
    len = (nj_s8)list->length;
    words = (const nj_s4 *)nj_array_data(list);

    if (!m3_cmd_word(vm, words, len, &pos, &version)) {
        return NJ_ERR;
    }
    if ((nj_u4)version != M3_CMD_VERSION_1_0) {
        return nj_throw(vm, "java/lang/IllegalArgumentException",
                        "Unsupported Micro3D command list version");
    }

    /* Projection/centre/effect state for the whole list.  A layout with an
     * invalid projection is accepted by the reference setter (it keeps the
     * old mode); this version reports nothing and queues nothing rather than
     * inventing magnification for it. */
    if (!m3_scene_setup(&sc, vm, self, layout, effect, x, y)) {
        return vm->exception ? NJ_ERR : NJ_OK;
    }

    while (pos < len) {
        nj_u4 high;

        if (!m3_cmd_word(vm, words, len, &pos, &command)) {
            return NJ_ERR;
        }
        high = (nj_u4)command & 0xFF000000u;
        switch (high) {
        case M3_CMD_END:
            /* Not flushed here: the reference leaves the queue for the next
             * explicit Graphics3D.flush(), which is how the corpus uses it. */
            return NJ_OK;
        case M3_CMD_NOP: {
            nj_s8 skip = (nj_s8)(command & 0xFFFFFF);

            pos += skip;
            break;
        }
        case M3_CMD_FLUSH: {
            nj_status st = mascot_scene_flush(vm, th, args, ret);

            if (st != NJ_OK) {
                return st;
            }
            /* Reference flush() unbinds textures and clears the environment
             * map source; the projection/centre/light remain in effect. */
            sc.environment = 0;
            break;
        }
        case M3_PRIM_LINES: {
            nj_status st = m3_cmd_lines(vm, words, len, &pos, &sc, command);

            if (st != NJ_OK) {
                return st;
            }
            break;
        }
        default:
            if (m3_cmd_known(high)) {
                return nj_throw(vm,
                                "java/lang/UnsupportedOperationException",
                                "Micro3D command list: command not implemented");
            }
            return nj_throw(vm, "java/lang/IllegalArgumentException",
                            "Micro3D command list: unknown command");
        }
    }
    return NJ_OK;
}

NJFN(mascot_cmd_draw_list)
{
    return m3_cmd_draw(vm, th, args, ret, 0);
}

NJFN(mascot_cmd_draw_list_array)
{
    return m3_cmd_draw(vm, th, args, ret, 1);
}
