/*
 * Micro3D model/animation bindings and deferred scene submission.
 * Included by nj_lib_mascot.c: the Java objects own all buffers through ordinary
 * reference fields, so GC, VM destruction and save-state relocation see them.
 * No native pointer or unregistered host allocation is retained in a Java blob.
 * Local pointers into rooted arrays may span buffer growth: this relies on the
 * VM's non-moving GC. A moving collector must reacquire those pointers after
 * allocation (notably texture-reference growth during model submission).
 *
 * MBAC/MTRA and rendering semantics follow the MIT-licensed FreeJ2ME-Plus
 * implementation, Copyright (c) 2026 Yury Kharchenko and Roman Lahin.
 */

static nj_ref m3_data(nj_vm *vm, nj_ref self)
{
    return m3_ref(vm, self, "_data", "[B");
}

static const nj_mascot_model *m3_model(nj_vm *vm, nj_ref self)
{
    nj_ref data = m3_data(vm, self);
    const nj_mascot_model *model;
    if (!data || nj_array_length(data) < sizeof(nj_mascot_model)) return NULL;
    model = (const nj_mascot_model *)nj_array_data(data);
    if (model->total_bytes != nj_array_length(data)) return NULL;
    return model;
}

/* kind: 0 = model, 1 = animation, 2 = indexed texture. Decoding first validates
 * the complete source. Publish the new buffer only after successful filling;
 * a failed replacement leaves the receiver's previous state intact. */
static nj_status m3_decode(nj_vm *vm, nj_ref self, const void *src, size_t len,
                           int kind)
{
    size_t size = 0;
    int ok;
    nj_ref data;
    if (kind == 0)
        ok = nj_mascot_model_decode(src, len, NULL, 0, &size) == NJ_MASCOT_MODEL_OK;
    else if (kind == 1)
        ok = nj_mascot_anim_query(src, len, &size) == NJ_MASCOT_ANIM_OK;
    else {
        nj_s4 texture_size = 0;
        ok = len <= 0x7fffffffu && nj_mascot_texture_query(src, (nj_s4)len,
                    kind == 2, &texture_size) == NJ_MR_OK;
        size = ok ? (size_t)texture_size : 0u;
    }
    if (!ok || size > 0x7fffffffu)
        return nj_throw(vm, "java/lang/RuntimeException", "invalid Micro3D resource");
    data = nj_byte_array_new(vm, (nj_u4)size);
    if (!data) return nj_throw_oom(vm);
    if (kind == 0)
        ok = nj_mascot_model_decode(src, len, nj_array_data(data), size, &size) == NJ_MASCOT_MODEL_OK;
    else if (kind == 1)
        ok = nj_mascot_anim_fill(src, len, nj_array_data(data), size) == NJ_MASCOT_ANIM_OK;
    else
        ok = nj_mascot_texture_fill(src, (nj_s4)len, kind == 2,
                    nj_array_data(data), (nj_s4)size) == NJ_MR_OK;
    if (!ok) return nj_throw(vm, "java/lang/RuntimeException", "invalid Micro3D resource");
    m3_set_ref(vm, self, "_data", "[B", data);
    if (kind == 0) m3_set(vm, self, "_selected", -1);
    return NJ_OK;
}

static nj_status m3_decode_array(nj_vm *vm, const nj_value *args, int kind, int slice)
{
    nj_ref a = NJ_ARG_REF(args, 0);
    nj_s4 offset = slice ? NJ_ARG_INT(args, 1) : 0;
    nj_s4 count = slice ? NJ_ARG_INT(args, 2) : (nj_s4)nj_array_length(a);
    if (!a) return nj_throw(vm, "java/lang/NullPointerException", "Micro3D data");
    if (a->kind != NJ_OBJ_ARRAY || a->aflags != NJ_ARRAY_T_BYTE ||
        offset < 0 || count < 0 || (nj_u4)offset > a->length ||
        (nj_u4)count > a->length - (nj_u4)offset)
        return nj_throw(vm, "java/lang/ArrayIndexOutOfBoundsException", "Micro3D data range");
    return m3_decode(vm, NJ_THIS(args), (const nj_u1 *)nj_array_data(a) + offset,
                     (size_t)count, kind);
}

static nj_status m3_decode_resource(nj_vm *vm, const nj_value *args, int kind)
{
    nj_ref name = NJ_ARG_REF(args, 0);
    char path[512];
    nj_u4 len = 0;
    void *data;
    nj_status st;
    nj_app *app = (nj_app *)vm->loader_ud;
    if (!name) return nj_throw(vm, "java/lang/NullPointerException", "Micro3D resource name");
    if (nj_string_length(name) >= sizeof(path) / 3u ||
        nj_string_to_utf8(name, path, (int)sizeof(path)) <= 0)
        return nj_throw(vm, "java/io/IOException", "Micro3D resource name too long");
    data = app ? nj_app_resource(vm, app, path, &len) : NULL;
    if (!data) return nj_throw(vm, "java/io/IOException", "Micro3D resource not found");
    st = m3_decode(vm, NJ_THIS(args), data, len, kind);
    nj_host_free(vm, data);
    return st;
}

NJFN(mascot_scene_fig_bytes) { return m3_decode_array(vm, args, 0, 0); }
NJFN(mascot_scene_fig_resource) { return m3_decode_resource(vm, args, 0); }
NJFN(mascot_scene_at_bytes) { return m3_decode_array(vm, args, 1, 0); }
NJFN(mascot_scene_at_slice) { return m3_decode_array(vm, args, 1, 1); }
NJFN(mascot_scene_at_resource) { return m3_decode_resource(vm, args, 1); }
NJFN(mascot_scene_tex_bytes)
{
    nj_status st = m3_decode_array(vm, args, NJ_ARG_INT(args, 1) ? 2 : 3, 0);
    if (st == NJ_OK) m3_set(vm, NJ_THIS(args), "_blend", !!NJ_ARG_INT(args, 1));
    return st;
}
NJFN(mascot_scene_tex_resource)
{
    nj_status st = m3_decode_resource(vm, args, NJ_ARG_INT(args, 1) ? 2 : 3);
    if (st == NJ_OK) m3_set(vm, NJ_THIS(args), "_blend", !!NJ_ARG_INT(args, 1));
    return st;
}
NJFN(mascot_scene_dispose_data)
{
    m3_set_ref(vm, NJ_THIS(args), "_data", "[B", NULL);
    return NJ_OK;
}
NJFN(mascot_scene_fig_dispose)
{
    nj_ref self = NJ_THIS(args);
    m3_set_ref(vm, self, "_data", "[B", NULL);
    m3_set_ref(vm, self, "_tbl", "L" NJ_C_AT ";", NULL);
    m3_set_ref(vm, self, "_tex", "L" NJ_C_TEX ";", NULL);
    m3_set_ref(vm, self, "_texs", "[L" NJ_C_TEX ";", NULL);
    return NJ_OK;
}
NJFN(mascot_scene_at_count)
{
    nj_ref data = m3_data(vm, NJ_THIS(args));
    if (!data) return nj_throw(vm, "java/lang/IllegalStateException", "disposed ActionTable");
    ret->i = nj_mascot_anim_action_count(nj_array_data(data), nj_array_length(data));
    return NJ_OK;
}
NJFN(mascot_scene_at_frames)
{
    nj_ref data = m3_data(vm, NJ_THIS(args));
    int32_t frames;
    if (!data) return nj_throw(vm, "java/lang/IllegalStateException", "disposed ActionTable");
    if (nj_mascot_anim_frames(nj_array_data(data), nj_array_length(data),
                              NJ_ARG_INT(args, 0), &frames) != 0)
        return nj_throw(vm, "java/lang/IllegalArgumentException", "ActionTable action");
    ret->i = frames;
    return NJ_OK;
}
NJFN(mascot_scene_fig_posture)
{
    nj_ref self = NJ_THIS(args), table = NJ_ARG_REF(args, 0), data;
    nj_s4 action = NJ_ARG_INT(args, 1), frame = NJ_ARG_INT(args, 2);
    int32_t pattern = m3_get(vm, self, "_pattern"), frames;
    if (!table) return nj_throw(vm, "java/lang/NullPointerException", "ActionTable");
    data = m3_data(vm, table);
    if (!data) return nj_throw(vm, "java/lang/IllegalStateException", "disposed ActionTable");
    if (nj_mascot_anim_frames(nj_array_data(data), nj_array_length(data), action, &frames) != 0)
        return nj_throw(vm, "java/lang/IllegalArgumentException", "ActionTable action");
    if (frame < 0) frame = 0;
    nj_mascot_anim_pattern(nj_array_data(data), nj_array_length(data), action,
                           frame, pattern, &pattern);
    m3_set_ref(vm, self, "_tbl", "L" NJ_C_AT ";", table);
    m3_set(vm, self, "_a", action);
    m3_set(vm, self, "_b", frame);
    m3_set(vm, self, "_pattern", pattern);
    return NJ_OK;
}
NJFN(mascot_scene_fig_patterns)
{
    const nj_mascot_model *model = m3_model(vm, NJ_THIS(args));
    if (!model) return nj_throw(vm, "java/lang/IllegalStateException", "disposed Figure");
    ret->i = model->num_patterns;
    return NJ_OK;
}
NJFN(mascot_scene_fig_pattern)
{
    m3_set(vm, NJ_THIS(args), "_pattern", NJ_ARG_INT(args, 0));
    return NJ_OK;
}
static int m3_texture_count(nj_vm *vm, nj_ref self)
{
    nj_ref one = m3_ref(vm, self, "_tex", "L" NJ_C_TEX ";");
    nj_ref many = m3_ref(vm, self, "_texs", "[L" NJ_C_TEX ";");
    return one ? 1 : (int)nj_array_length(many);
}
NJFN(mascot_scene_fig_tex_count)
{
    ret->i = m3_texture_count(vm, NJ_THIS(args));
    return NJ_OK;
}
NJFN(mascot_scene_fig_tex_select)
{
    nj_s4 index = NJ_ARG_INT(args, 0);
    if (index < 0 || index >= m3_texture_count(vm, NJ_THIS(args)))
        return nj_throw(vm, "java/lang/IllegalArgumentException", "Figure texture index");
    m3_set(vm, NJ_THIS(args), "_selected", index);
    return NJ_OK;
}
static nj_ref m3_figure_texture(nj_vm *vm, nj_ref self, int slot)
{
    nj_ref one = m3_ref(vm, self, "_tex", "L" NJ_C_TEX ";");
    nj_ref many = m3_ref(vm, self, "_texs", "[L" NJ_C_TEX ";");
    return one ? (slot == 0 ? one : NULL) : nj_array_get_ref(many, (nj_u4)slot);
}
NJFN(mascot_scene_fig_tex_get)
{
    int selected = m3_get(vm, NJ_THIS(args), "_selected");
    ret->r = selected >= 0 ? m3_figure_texture(vm, NJ_THIS(args), selected) : NULL;
    return NJ_OK;
}
NJFN(mascot_scene_fig_tex)
{
    nj_ref self = NJ_THIS(args), tex = NJ_ARG_REF(args, 0);
    if (!tex) return nj_throw(vm, "java/lang/NullPointerException", "Figure texture");
    if (!m3_get(vm, tex, "_blend"))
        return nj_throw(vm, "java/lang/IllegalArgumentException", "Figure needs a model texture");
    m3_set_ref(vm, self, "_tex", "L" NJ_C_TEX ";", tex);
    m3_set_ref(vm, self, "_texs", "[L" NJ_C_TEX ";", NULL);
    m3_set(vm, self, "_selected", 0);
    return NJ_OK;
}
NJFN(mascot_scene_fig_texs)
{
    nj_ref self = NJ_THIS(args), texs = NJ_ARG_REF(args, 0);
    nj_u4 i;
    if (!texs) return nj_throw(vm, "java/lang/NullPointerException", "Figure textures");
    if (!nj_array_length(texs))
        return nj_throw(vm, "java/lang/IllegalArgumentException", "empty Figure textures");
    for (i = 0; i < nj_array_length(texs); ++i) {
        nj_ref tex = nj_array_get_ref(texs, i);
        if (!tex) return nj_throw(vm, "java/lang/NullPointerException", "Figure texture");
        if (!m3_get(vm, tex, "_blend"))
            return nj_throw(vm, "java/lang/IllegalArgumentException", "Figure needs model textures");
    }
    m3_set_ref(vm, self, "_texs", "[L" NJ_C_TEX ";", texs);
    m3_set_ref(vm, self, "_tex", "L" NJ_C_TEX ";", NULL);
    m3_set(vm, self, "_selected", -1);
    return NJ_OK;
}
NJFN(mascot_scene_fx_default)
{
    m3_set(vm, NJ_THIS(args), "_semi", 1);
    return NJ_OK;
}

/* Grow-on-demand buffers are shared by every Figure submitted to one Graphics3D.
 * Applications with a tighter heap can cap the queue at compile time. Reaching
 * the cap reports OOM instead of dropping geometry or retaining a partial frame. */
typedef struct m3_qv {
    nj_s2 x, y;
    nj_u2 u, v;
    nj_s2 light, eu, ev;
} m3_qv;
typedef struct m3_qprim {
    m3_qv v[3];
    nj_s4 z;
    nj_u4 color, toon;
    nj_u2 material, texture, environment;
    nj_u1 kind, pad;
} m3_qprim;
typedef struct m3_sv { nj_s4 x, y, z, u, v, light, eu, ev; } m3_sv;
typedef struct m3_scene {
    nj_vm *vm;
    nj_ref self;
    nj_s4 matrix[12];
    nj_s4 cx, cy, kx, ky, near_z, far_z;
    nj_s4 width, height;
    nj_s4 light[3], ambient, diffuse;
    int perspective, lit, semi;
    nj_u4 toon;
    nj_u2 environment;
} m3_scene;

static nj_ref m3_buffer(nj_vm *vm, nj_ref self, const char *name,
                        const char *desc, const char *element, nj_u4 need)
{
    nj_ref old = m3_ref(vm, self, name, desc), out;
    nj_u4 cap = nj_array_length(old);
    size_t unit = element[0] == 'I' ? sizeof(nj_s4) :
                  element[0] == 'B' ? 1u : sizeof(nj_ref);
    if (cap >= need) return old;
    if (cap < 64u) cap = 64u;
    while (cap < need) {
        if (cap > 0x3fffffffu) { nj_throw_oom(vm); return NULL; }
        cap *= 2u;
    }
    out = nj_array_new(vm, element, cap);
    if (!out) { nj_throw_oom(vm); return NULL; }
    if (old) memcpy(nj_array_data(out), nj_array_data(old), nj_array_length(old) * unit);
    m3_set_ref(vm, self, name, desc, out);
    return out;
}

/* Store immutable byte blobs, not the mutable/disposable Texture wrappers. */
static int m3_bind_texture(m3_scene *sc, nj_ref texture)
{
    nj_vm *vm = sc->vm;
    nj_ref data, refs;
    nj_u4 i, n = (nj_u4)m3_get(vm, sc->self, "_nt");
    if (!texture) return 0;
    data = m3_data(vm, texture);
    if (!data) { nj_throw(vm, "java/lang/IllegalStateException", "disposed Texture"); return -1; }
    refs = m3_ref(vm, sc->self, "_textures", "[Ljava/lang/Object;");
    for (i = 0; i < n; ++i) if (nj_array_get_ref(refs, i) == data) return (int)i + 1;
    if (n >= 4095u) { nj_throw(vm, "java/lang/IndexOutOfBoundsException", "too many Micro3D textures"); return -1; }
    refs = m3_buffer(vm, sc->self, "_textures", "[Ljava/lang/Object;",
                     "java/lang/Object", n + 1u);
    if (!refs) return -1;
    nj_array_set_ref(refs, n, data);
    m3_set(vm, sc->self, "_nt", (nj_s4)n + 1);
    return (int)n + 1;
}

static void m3_scene_reset(nj_vm *vm, nj_ref self)
{
    nj_ref refs = m3_ref(vm, self, "_textures", "[Ljava/lang/Object;");
    nj_s4 n = m3_get(vm, self, "_nt"), i;
    for (i = 0; i < n; ++i) nj_array_set_ref(refs, (nj_u4)i, NULL);
    m3_set(vm, self, "_count", 0);
    m3_set(vm, self, "_nt", 0);
}

static int m3_scene_setup(m3_scene *sc, nj_vm *vm, nj_ref self,
                          nj_ref layout, nj_ref effect, nj_s4 x, nj_s4 y)
{
    nj_ref g, xf, light, direction, sphere;
    nj_gfx gfx;
    nj_surface surface;
    nj_u4 proj;
    memset(sc, 0, sizeof(*sc));
    sc->vm = vm; sc->self = self;
    g = m3_ref(vm, self, "_g", "Ljavax/microedition/lcdui/Graphics;");
    if (!g) { nj_throw(vm, "java/lang/IllegalStateException", "no Graphics bound"); return 0; }
    if (!layout || !effect) { nj_throw(vm, "java/lang/NullPointerException", "layout/effect"); return 0; }
    if (!nj_midp_gfx_load(vm, g, &gfx, &surface) || !gfx.surf) return 0;
    sc->width = gfx.surf->width; sc->height = gfx.surf->height;
    xf = m3_ref(vm, layout, "_xf", "L" NJ_C_XF ";");
    if (xf) xf_read(vm, xf, sc->matrix); else xf_identity(sc->matrix);
    sc->cx = (nj_s4)((nj_u4)m3_get(vm, layout, "_cx") + (nj_u4)x);
    sc->cy = (nj_s4)((nj_u4)m3_get(vm, layout, "_cy") + (nj_u4)y);
    proj = (nj_u4)m3_get(vm, layout, "_p");
    sc->perspective = (proj & M3_PROJ_MODE_MASK) == M3_PROJ_FOV ||
                      (proj & M3_PROJ_MODE_MASK) == M3_PROJ_WH;
    sc->near_z = m3_get(vm, layout, "_near"); sc->far_z = m3_get(vm, layout, "_far");
    sc->kx = m3_get(vm, layout, "_sx"); sc->ky = m3_get(vm, layout, "_sy");
    if ((proj & M3_PROJ_MODE_MASK) == M3_PROJ_FOV) {
        nj_s4 angle = (nj_s4)(proj & M3_ANGLE_MASK);
        float scale;
        if (angle < 1 || angle > 2047 || sc->near_z < 1 || sc->far_z <= sc->near_z) return 0;
        scale=0.5f/(float)tan(angle*(3.14159265358979323846/4096.0));
        sc->kx=(nj_s4)(sc->width*scale);
        sc->ky=sc->kx;
    } else if ((proj & M3_PROJ_MODE_MASK) == M3_PROJ_PARALLEL_SIZE ||
               (proj & M3_PROJ_MODE_MASK) == M3_PROJ_WH) {
        nj_u4 near_scale=sc->perspective?(nj_u4)sc->near_z:1u;
        if(sc->kx<=0 || sc->ky<=0 || (sc->perspective &&
           (sc->near_z<1 || sc->far_z<=sc->near_z)))return 0;
        sc->kx=m3_i32(((nj_u4)sc->width<<12)*near_scale)/sc->kx;
        sc->ky=m3_i32(((nj_u4)sc->height<<12)*near_scale)/sc->ky;
    }
    sc->semi = m3_get(vm, effect, "_semi") != 0;
    sc->toon = (nj_u4)m3_get(vm, effect, "_sh");
    light = m3_ref(vm, effect, "_lig", "L" NJ_C_LIG ";");
    direction = m3_ref(vm, light, "_dir", "L" NJ_C_V3 ";");
    if (light && direction) {
        sc->lit = 1;
        sc->light[0] = m3_get(vm, direction, "x");
        sc->light[1] = m3_get(vm, direction, "y");
        sc->light[2] = m3_get(vm, direction, "z");
        sc->ambient = m3_get(vm, light, "_amb");
        sc->diffuse = m3_get(vm, light, "_dif");
    }
    sphere = m3_ref(vm, effect, "_sph", "L" NJ_C_TEX ";");
    if (sc->lit && sphere) {
        int id = m3_bind_texture(sc, sphere);
        if (id < 0) return 0;
        sc->environment = (nj_u2)id;
    }
    return 1;
}

static void m3_matmul(const nj_s4 a[12], const nj_s4 b[12], nj_s4 out[12])
{
    nj_s4 tmp[12];
    int r, c, k;
    for (r = 0; r < 3; ++r) for (c = 0; c < 4; ++c) {
        nj_u4 sum = 2048u;
        for (k = 0; k < 3; ++k) sum += (nj_u4)a[r * 4 + k] * (nj_u4)b[k * 4 + c];
        tmp[r * 4 + c] = (nj_s4)m3_shr12((nj_s4)sum);
        if (c == 3) tmp[r * 4 + c] = (nj_s4)((nj_u4)tmp[r * 4 + c] + (nj_u4)a[r * 4 + 3]);
    }
    memcpy(out, tmp, sizeof(tmp));
}

/* Java Vector3D.unit rescales before its rounded integer square root. */
static nj_s4 m3_i32(nj_u4 v)
{
    return v <= 2147483647u ? (nj_s4)v : -1 - (nj_s4)~v;
}
static nj_s4 m3_asr32(nj_s4 v, unsigned n)
{
    if (!n) return v;
    return m3_i32(((nj_u4)v >> n) | (v < 0 ? ~(nj_u4)0 << (32 - n) : 0));
}
static void m3_unit(nj_s4 v[3])
{
    nj_u4 mask=0, length, rem, bit, root=0;
    nj_s4 xyz[3];
    unsigned leading=32;
    int k, shift;
    for(k=0;k<3;++k) mask |= v[k]<0 ? 0u-(nj_u4)v[k] : (nj_u4)v[k];
    while(mask) {mask>>=1;--leading;}
    shift=(int)leading-17;
    for(k=0;k<3;++k)
        xyz[k]=shift>0?m3_i32((nj_u4)v[k]<<(unsigned)shift):m3_asr32(v[k],(unsigned)-shift);
    length=(nj_u4)xyz[0]*(nj_u4)xyz[0]+(nj_u4)xyz[1]*(nj_u4)xyz[1]+(nj_u4)xyz[2]*(nj_u4)xyz[2];
    rem=length; bit=(nj_u4)1<<30;
    while(bit>rem)bit>>=2;
    while(bit) {
        if(rem>=root+bit){rem-=root+bit;root=(root>>1)+bit;}
        else root>>=1;
        bit>>=2;
    }
    if(root<65535u&&length-root*root>root)++root;
    if(root)for(k=0;k<3;++k)v[k]=m3_i32((nj_u4)xyz[k]<<12)/(nj_s4)root;
    else {v[0]=v[1]=0;v[2]=4096;}
}

typedef struct m3_light_frame { nj_s4 dir[3], u[3], v[3]; } m3_light_frame;
static void m3_light_prepare(const m3_scene *sc, const nj_s4 m[12], m3_light_frame *lf)
{
    int r;
    for (r = 0; r < 3; ++r) {
        /* AffineTrans.rotate is the transpose rotation used by the reference. */
        nj_u4 sum = 2048u + (nj_u4)m[r] * (nj_u4)sc->light[0] +
                    (nj_u4)m[4+r] * (nj_u4)sc->light[1] +
                    (nj_u4)m[8+r] * (nj_u4)sc->light[2];
        lf->dir[r] = (nj_s4)m3_shr12((nj_s4)sum);
        lf->u[r] = m[r]; lf->v[r] = m[4+r];
    }
    m3_unit(lf->dir); m3_unit(lf->u); m3_unit(lf->v);
}
static nj_s4 m3_dot_raw(const nj_s4 a[3], const nj_s4 b[3])
{
    return (nj_s4)((nj_u4)a[0]*(nj_u4)b[0] + (nj_u4)a[1]*(nj_u4)b[1] + (nj_u4)a[2]*(nj_u4)b[2]);
}
static void m3_light_vertex(const m3_scene *sc, const m3_light_frame *lf,
                             const nj_s4 normal[3], m3_sv *v)
{
    nj_s4 dot = (nj_s4)(0u - (nj_u4)m3_dot_raw(normal, lf->dir));
    nj_s4 intensity = sc->ambient;
    int k;
    if (dot > ((sc->toon & 1u) ? (-2147483647 - 1) : 0)) {
        nj_s4 a = (nj_s4)m3_shr12(dot);
        intensity = (nj_s4)((nj_u4)intensity + (nj_u4)m3_shr12((nj_s4)((nj_u4)a * (nj_u4)sc->diffuse)));
    }
    v->light = (nj_s2)(intensity > 4095 ? 4095 : intensity);
    for (k = 0; k < 2; ++k) {
        nj_s4 d = (nj_s4)m3_shr12(m3_dot_raw(normal, k ? lf->v : lf->u));
        d = (d + 4096) / 2;
        if (d < 0) d = 0;
        if(d > 4095) d = 4095;
        if (k) v->ev = d * 65 / 256; else v->eu = d * 65 / 256;
    }
}

static void m3_transform_vertex(const nj_s4 m[12], const nj_s4 xyz[3], m3_sv *v)
{
    m3_vtx out;
    m3_xform(m, xyz, &out);
    memset(v, 0, sizeof(*v));
    v->x = (nj_s4)out.x; v->y = (nj_s4)out.y; v->z = (nj_s4)out.z;
    v->light = 4095;
}
static void m3_project_vertex(const m3_scene *sc, const m3_sv *v, m3_qv *out)
{
    nj_s8 x, y;
    if (sc->perspective) {
        x = (nj_s8)v->x * sc->kx / (v->z ? v->z : 1);
        y = (nj_s8)v->y * sc->ky / (v->z ? v->z : 1);
    } else {
        x = m3_shr12((nj_s8)v->x * sc->kx);
        y = m3_shr12((nj_s8)v->y * sc->ky);
    }
    x += sc->cx; y += sc->cy;
    if (x < -32768) x = -32768;
    if(x > 32767) x = 32767;
    if (y < -32768) y = -32768;
    if(y > 32767) y = 32767;
    out->x = (nj_s2)x; out->y = (nj_s2)y;
    out->u = (nj_u2)v->u; out->v = (nj_u2)v->v;
    out->light = (nj_s2)v->light; out->eu = (nj_s2)v->eu; out->ev = (nj_s2)v->ev;
}

static int m3_queue(m3_scene *sc, const m3_sv *a, const m3_sv *b, const m3_sv *c,
                    int kind, nj_u4 material, nj_u4 color, int texture, nj_s4 sort_z)
{
    nj_vm *vm = sc->vm;
    m3_qprim q, *queue;
    nj_ref data;
    nj_u4 count = (nj_u4)m3_get(vm, sc->self, "_count");
    memset(&q, 0, sizeof(q));
    m3_project_vertex(sc, a, &q.v[0]);
    if (kind >= 2) m3_project_vertex(sc, b, &q.v[1]);
    if (kind >= 3) m3_project_vertex(sc, c, &q.v[2]);
    if (kind == 3) {
        int x0=q.v[0].x,x1=q.v[1].x,x2=q.v[2].x;
        int y0=q.v[0].y,y1=q.v[1].y,y2=q.v[2].y;
        nj_s8 cross = (nj_s8)(x1-x0)*(y2-y0)-(nj_s8)(y1-y0)*(x2-x0);
        if ((!cross) || (!(material & NJ_MASCOT_MAT_DOUBLE_FACE) && cross < 0)) return 1;
        if ((x0<0&&x1<0&&x2<0)||(y0<0&&y1<0&&y2<0)||
            (x0>=sc->width&&x1>=sc->width&&x2>=sc->width)||
            (y0>=sc->height&&y1>=sc->height&&y2>=sc->height)) return 1;
    }
    if (count >= NJ_MASCOT_MAX_QUEUED_TRIANGLES ||
        count >= 0x7fffffffu / (nj_u4)sizeof(q)) {
        nj_throw_oom(vm); return 0;
    }
    data = m3_buffer(vm, sc->self, "_queue", "[B", "B", (count+1u)*(nj_u4)sizeof(q));
    if (!data) return 0;
    q.z=sort_z; q.kind=(nj_u1)kind; q.material=(nj_u2)material; q.color=color; q.toon=sc->toon;
    q.texture=(nj_u2)texture; q.environment=sc->environment;
    queue=(m3_qprim *)nj_array_data(data); queue[count]=q;
    m3_set(vm, sc->self, "_count", (nj_s4)count+1);
    return 1;
}

static int m3_clip_attributes(const m3_sv *in, int n, nj_s4 plane, int greater,
                              m3_sv *out)
{
    int i, num=0;
    for (i=0;i<n;++i) {
        const m3_sv *a=&in[i],*b=&in[(i+1)%n];
        int ai=greater?a->z>=plane:a->z<=plane, bi=greater?b->z>=plane:b->z<=plane;
        if (ai) out[num++]=*a;
        if (ai!=bi) {
            nj_s8 f=((nj_s8)plane-a->z)*4096/((nj_s8)b->z-a->z);
            m3_sv *d=&out[num++];
#define M3_LERP(F) d->F=(nj_s4)((nj_s8)a->F+m3_shr12(((nj_s8)b->F-a->F)*f))
            M3_LERP(x); M3_LERP(y); M3_LERP(u); M3_LERP(v);
            M3_LERP(light); M3_LERP(eu); M3_LERP(ev);
#undef M3_LERP
            d->z=plane;
        }
    }
    return num;
}
static int m3_submit_triangle(m3_scene *sc, const m3_sv *a, const m3_sv *b,
                              const m3_sv *c, nj_u4 mat, nj_u4 color, int tex,
                              nj_s4 sort_z)
{
    m3_sv initial[3], nearv[4], farv[5];
    const m3_sv *p=initial;
    int n=3,i;
    initial[0]=*a; initial[1]=*b; initial[2]=*c;
    if (!sc->semi) mat &= ~6u;
    if (!sc->lit) mat &= ~(NJ_MASCOT_MAT_LIGHTING|NJ_MASCOT_MAT_SPECULAR);
    if (!sc->environment) mat &= ~NJ_MASCOT_MAT_SPECULAR;
    if (sc->perspective && (a->z<sc->near_z||b->z<sc->near_z||c->z<sc->near_z||
                            a->z>sc->far_z||b->z>sc->far_z||c->z>sc->far_z)) {
        n=m3_clip_attributes(initial,3,sc->near_z,1,nearv);
        if (n<3) return 1;
        n=m3_clip_attributes(nearv,n,sc->far_z,0,farv);
        p=farv;
    }
    for(i=1;i+1<n;++i)
        if (!m3_queue(sc,&p[0],&p[i],&p[i+1],3,mat,color,tex,sort_z)) return 0;
    return 1;
}

static nj_s4 m3_sort_depth(const m3_sv *v, int n, nj_u4 mat)
{
    int i;
    nj_u4 sum=0;
    nj_s4 value=v[0].z;
    for(i=0;i<n;++i) {
        sum+=(nj_u4)v[i].z;
        if ((mat & NJ_MASCOT_MAT_ZSORT_NEAR) && v[i].z<value) value=v[i].z;
        if (!(mat & NJ_MASCOT_MAT_ZSORT_NEAR) && v[i].z>value) value=v[i].z;
    }
    if (mat & (NJ_MASCOT_MAT_ZSORT_NEAR|NJ_MASCOT_MAT_ZSORT_FAR)) return value;
    return n==4 ? (nj_s4)((nj_s4)sum>=0?(nj_s4)sum/4:-1-(-((nj_s4)sum+1)/4)) : (nj_s4)sum/n;
}

static int m3_model_polygons(m3_scene *sc, const nj_mascot_model *model,
                             const m3_sv *vertices, nj_u4 offset, int stride,
                             nj_u4 start, nj_u4 count, int n, int texture)
{
    const nj_u1 *blob=(const nj_u1 *)model;
    const nj_u2 *poly=(const nj_u2 *)(blob+offset)+start*(nj_u4)stride;
    const nj_u4 *colors=(const nj_u4 *)(blob+model->colors_offset);
    nj_u4 i;
    for(i=0;i<count;++i,poly+=stride) {
        m3_sv v[4];
        nj_u4 mat=poly[0], color=texture?0xffffffffu:colors[poly[1]];
        int base=texture?1:2, j;
        nj_s4 z;
        for(j=0;j<n;++j) {
            v[j]=vertices[poly[base+j]];
            if (texture) {
                nj_u2 uv=poly[base+n+j];
                v[j].u=uv&0xff00u; v[j].v=(uv&255u)<<8;
            }
            if (mat & NJ_MASCOT_MAT_FLAT_NORMAL) {
                v[j].light=v[0].light; v[j].eu=v[0].eu; v[j].ev=v[0].ev;
            }
        }
        z=m3_sort_depth(v,n,mat);
        if (!m3_submit_triangle(sc,&v[0],&v[1],&v[2],mat,color,texture,z)) return 0;
        if(n==4 && !m3_submit_triangle(sc,&v[2],&v[1],&v[3],mat,color,texture,z)) return 0;
    }
    return 1;
}

NJFN(mascot_scene_figure)
{
    nj_ref self=NJ_THIS(args), figure=NJ_ARG_REF(args,0), layout=NJ_ARG_REF(args,3);
    nj_ref effect=NJ_ARG_REF(args,4), scratch, action_table, animation;
    const nj_mascot_model *model;
    const nj_u1 *blob;
    const nj_s2 *xyz,*normal;
    const nj_mascot_bone *bones;
    const nj_mascot_pattern_range *patterns;
    nj_s4 *matrices, selected, pattern, action, frame;
    m3_sv *verts;
    m3_scene sc;
    size_t need;
    int b,p,t;
    if (!figure) return nj_throw(vm,"java/lang/NullPointerException","Figure");
    model=m3_model(vm,figure);
    if (!model) return nj_throw(vm,"java/lang/IllegalStateException","disposed Figure");
    if (!m3_scene_setup(&sc,vm,self,layout,effect,NJ_ARG_INT(args,1),NJ_ARG_INT(args,2)))
        return vm->exception?NJ_ERR:NJ_OK;
    need=(size_t)model->num_vertices*sizeof(m3_sv)+(size_t)model->num_bones*12u*sizeof(nj_s4);
    if(need>0x7fffffffu) return nj_throw_oom(vm);
    scratch=m3_buffer(vm,self,"_scratch","[B","B",(nj_u4)need);
    if(!scratch && need) return NJ_ERR;
    verts=(m3_sv *)nj_array_data(scratch);
    matrices=(nj_s4 *)(verts+model->num_vertices);
    blob=(const nj_u1 *)model;
    xyz=(const nj_s2 *)(blob+model->vertices_offset);
    normal=model->normals_offset?(const nj_s2 *)(blob+model->normals_offset):NULL;
    bones=(const nj_mascot_bone *)(blob+model->bones_offset);
    patterns=(const nj_mascot_pattern_range *)(blob+model->patterns_offset);
    action_table=m3_ref(vm,figure,"_tbl","L" NJ_C_AT ";");
    animation=m3_data(vm,action_table);
    if (action_table && !animation) return nj_throw(vm,"java/lang/IllegalStateException","disposed ActionTable");
    action=m3_get(vm,figure,"_a"); frame=m3_get(vm,figure,"_b");
    if(!normal) sc.lit=0;
    /* A model without bones uses the layout directly. The decoder guarantees
     * that otherwise the ordered bone runs cover the model's vertices. */
    for(b=0;b<(model->num_bones?model->num_bones:1);++b) {
        nj_s4 local[12], transform[12];
        const nj_s4 *base=sc.matrix;
        m3_light_frame lf;
        nj_u4 first=0, count=model->num_vertices, i;
        if(model->num_bones) {
            const nj_mascot_bone *bone=&bones[b];
            memcpy(local,bone->matrix,sizeof(local));
            first=bone->start_vertex; count=bone->num_vertices;
            if(animation && b<nj_mascot_anim_bone_count(nj_array_data(animation),nj_array_length(animation))) {
                if(nj_mascot_anim_evaluate(nj_array_data(animation),nj_array_length(animation),
                    action,b,frame,bone->matrix,local)!=NJ_MASCOT_ANIM_OK)
                    return nj_throw(vm,"java/lang/IllegalArgumentException","invalid Figure action");
            }
            if(bone->parent>=0) base=matrices+bone->parent*12;
            m3_matmul(base,local,transform);
            memcpy(matrices+b*12,transform,sizeof(transform));
        } else memcpy(transform,sc.matrix,sizeof(transform));
        if(sc.lit) m3_light_prepare(&sc,transform,&lf);
        for(i=first;i<first+count;++i) {
            nj_s4 v[3]={xyz[i*3],xyz[i*3+1],xyz[i*3+2]};
            m3_transform_vertex(transform,v,&verts[i]);
            if(sc.lit) {
                nj_s4 n[3]={normal[i*3],normal[i*3+1],normal[i*3+2]};
                m3_light_vertex(&sc,&lf,n,&verts[i]);
            }
        }
    }
    selected=m3_get(vm,figure,"_selected"); pattern=m3_get(vm,figure,"_pattern");
    for(p=0;p<model->num_patterns;++p) {
        nj_u4 bit=p?1u<<(p&31):0u;
        if(((nj_u4)pattern&bit)!=bit) continue;
        for(t=0;t<=model->num_textures;++t) {
            const nj_mascot_pattern_range *range=&patterns[p*(model->num_textures+1)+t];
            int tex=0;
            if(t) {
                nj_ref texture;
                int slot=t-1;
                if(selected>=0) { if(slot) continue; slot=selected; }
                texture=m3_figure_texture(vm,figure,slot);
                if(!texture) continue;
                tex=m3_bind_texture(&sc,texture);
                if(tex<0) return NJ_ERR;
            }
            if(!m3_model_polygons(&sc,model,verts,
                t?model->poly_t3_offset:model->poly_c3_offset,t?7:5,
                range->first_tri,range->num_tri,3,tex) ||
               !m3_model_polygons(&sc,model,verts,
                t?model->poly_t4_offset:model->poly_c4_offset,t?9:6,
                range->first_quad,range->num_quad,4,tex)) return NJ_ERR;
        }
    }
    return NJ_OK;
}

static nj_status m3_scene_primitives(nj_vm *vm, const nj_value *args)
{
    nj_ref self=NJ_THIS(args), texture=NJ_ARG_REF(args,0), layout=NJ_ARG_REF(args,3);
    nj_ref effect=NJ_ARG_REF(args,4), vertices=NJ_ARG_REF(args,7);
    nj_ref normals=NJ_ARG_REF(args,8), uvs=NJ_ARG_REF(args,9), colors=NJ_ARG_REF(args,10);
    nj_s4 command=NJ_ARG_INT(args,5), count=NJ_ARG_INT(args,6);
    int prim=command&M3_PRIM_MASK,n,uv,ct,nt,tex=0,i;
    nj_u4 mat;
    const nj_s4 *vp,*up=NULL,*cp=NULL,*np=NULL;
    m3_scene sc;
    m3_light_frame lf;
    if(!layout||!effect||!vertices) return nj_throw(vm,"java/lang/NullPointerException","Micro3D primitive data");
    if(count<1||count>255||(command&0x00ff0000)) return nj_throw(vm,"java/lang/IllegalArgumentException","Micro3D primitive command");
    if(!m3_ref(vm,self,"_g","Ljavax/microedition/lcdui/Graphics;"))
        return nj_throw(vm,"java/lang/IllegalStateException","no Graphics bound");
    switch(prim) {
    case M3_PRIM_POINTS: n=1; break;
    case M3_PRIM_LINES: n=2; break;
    case M3_PRIM_TRIS: n=3; break;
    case M3_PRIM_QUADS: n=4; break;
    case M3_PRIM_SPRITES: n=1; break;
    default:return nj_throw(vm,"java/lang/IllegalArgumentException","unknown Micro3D primitive");
    }
    uv=(command&M3_PDATA_UV)!=0||prim==M3_PRIM_SPRITES;
    ct=uv?0:command&M3_PDATA_COLOR_MASK;
    nt=command&M3_PDATA_NORMAL_MASK;
    if((uv&&(!texture||!uvs))||(nt&&!normals)||(ct==M3_PDATA_COLOR_FACE&&!colors))
        return nj_throw(vm,"java/lang/NullPointerException","Micro3D primitive arrays");
    if(!m3_int_array_ok(vertices,count*n*3))
        return nj_throw(vm,"java/lang/ArrayIndexOutOfBoundsException","Micro3D coordinates");
    if(ct==M3_PDATA_COLOR_BAD || (n>=3&&nt==M3_PDATA_NORMAL_BAD))
        return nj_throw(vm,"java/lang/IllegalArgumentException","Micro3D attributes");
    if(uv) {
        int size=prim==M3_PRIM_SPRITES?(((command&0x3000)==0x1000?1:count)*8):count*n*2;
        if(!m3_int_array_ok(uvs,size)) return nj_throw(vm,"java/lang/ArrayIndexOutOfBoundsException","Micro3D UVs");
        up=(const nj_s4 *)nj_array_data(uvs);
    } else {
        if(!ct) return NJ_OK;
        if(!m3_int_array_ok(colors,ct==M3_PDATA_COLOR_CMD?1:count))
            return nj_throw(vm,"java/lang/ArrayIndexOutOfBoundsException","Micro3D colors");
        cp=(const nj_s4 *)nj_array_data(colors);
    }
    if(!m3_scene_setup(&sc,vm,self,layout,effect,NJ_ARG_INT(args,1),NJ_ARG_INT(args,2)))
        return vm->exception?NJ_ERR:NJ_OK;
    if(uv) {tex=m3_bind_texture(&sc,texture);
    if(tex<0)return NJ_ERR;}
    mat=NJ_MASCOT_MAT_DOUBLE_FACE | ((nj_u4)(command&0x60)>>4) | ((nj_u4)(command&0x10)>>4);
    if(!sc.semi) mat&=~6u;
    if(n>=3&&sc.lit&&nt&&(command&M3_PATTR_LIGHTING)) {
        int need=(nt==0x200?count:count*n)*3;
        if(!m3_int_array_ok(normals,need))return nj_throw(vm,"java/lang/ArrayIndexOutOfBoundsException","Micro3D normals");
        np=(const nj_s4 *)nj_array_data(normals);
        mat|=NJ_MASCOT_MAT_LIGHTING;
        if(sc.environment&&(command&2)) mat|=NJ_MASCOT_MAT_SPECULAR;
        m3_light_prepare(&sc,sc.matrix,&lf);
    }
    vp=(const nj_s4 *)nj_array_data(vertices);
    for(i=0;i<count;++i) {
        m3_sv v[4];
        nj_u4 color=cp?0xff000000u|((nj_u4)cp[ct==M3_PDATA_COLOR_CMD?0:i]&0xffffffu):0xffffffffu;
        nj_s4 z;
        int j;
        for(j=0;j<n;++j) {
            m3_transform_vertex(sc.matrix,vp+(i*n+j)*3,&v[j]);
            if(uv&&prim!=M3_PRIM_SPRITES) {
                v[j].u=(nj_s4)((nj_u4)up[(i*n+j)*2]<<8)&0xffff;
                v[j].v=(nj_s4)((nj_u4)up[(i*n+j)*2+1]<<8)&0xffff;
            }
            if(np)m3_light_vertex(&sc,&lf,np+(nt==0x200?i:i*n+j)*3,&v[j]);
        }
        z=m3_sort_depth(v,n,0);
        if(prim==M3_PRIM_SPRITES) {
            const nj_s4 *p=up+(((command&0x3000)==0x1000)?0:i*8);
            m3_qv center;
            m3_scene screen=sc;
            nj_s8 w=p[0],h=p[1];
            nj_s4 sn=m3_sin(p[2]),cs=m3_cos(p[2]);
            int uvx[4]={0,1,0,1},uvy[4]={0,0,1,1};
            if((sc.perspective&&(v[0].z<sc.near_z||v[0].z>sc.far_z))||p[5]<=p[3]||p[6]<=p[4])continue;
            m3_project_vertex(&sc,&v[0],&center);
            if(!(p[7]&1)) {
                if(sc.perspective&&!(p[7]&2)) {w=w*sc.kx/v[0].z;h=h*sc.ky/v[0].z;}
                else {w=m3_shr12(w*sc.kx);h=m3_shr12(h*sc.ky);}
            }
            screen.perspective=0;screen.kx=screen.ky=4096;screen.cx=screen.cy=0;
            for(j=0;j<4;++j) {
                nj_s8 xx=(uvx[j]?w:-w)/2,yy=(uvy[j]?h:-h)/2;
                v[j].x=m3_s4_of(center.x+m3_shr12(xx*cs-yy*sn));
                v[j].y=m3_s4_of(center.y+m3_shr12(xx*sn+yy*cs));
                v[j].z=z;v[j].light=4095;v[j].eu=v[j].ev=0;
                v[j].u=(nj_s4)((nj_u4)p[uvx[j]?5:3]<<8)&0xffff;
                v[j].v=(nj_s4)((nj_u4)p[uvy[j]?6:4]<<8)&0xffff;
            }
            if(!m3_submit_triangle(&screen,&v[0],&v[1],&v[2],mat,color,tex,z)||
               !m3_submit_triangle(&screen,&v[2],&v[1],&v[3],mat,color,tex,z))return NJ_ERR;
        } else if(n>=3) {
            if(!m3_submit_triangle(&sc,&v[0],&v[1],&v[n==4?3:2],mat,color,tex,z))return NJ_ERR;
            if(n==4&&!m3_submit_triangle(&sc,&v[1],&v[2],&v[3],mat,color,tex,z))return NJ_ERR;
        } else if(n==1) {
            if(sc.perspective&&(v[0].z<sc.near_z||v[0].z>sc.far_z))continue;
            if(!m3_queue(&sc,&v[0],NULL,NULL,1,mat,color,0,z))return NJ_ERR;
        } else {
            if(sc.perspective) {
                m3_vtx a={v[0].x,v[0].y,v[0].z}, b={v[1].x,v[1].y,v[1].z};
                if(!m3_clip_line(&a,&b,sc.near_z,sc.far_z))continue;
                v[0].x=(nj_s4)a.x;v[0].y=(nj_s4)a.y;v[0].z=(nj_s4)a.z;
                v[1].x=(nj_s4)b.x;v[1].y=(nj_s4)b.y;v[1].z=(nj_s4)b.z;
            }
            /* Lines are sorted by their clipped endpoint depths. */
            z=m3_sort_depth(v,2,0);
            if(!m3_queue(&sc,&v[0],&v[1],NULL,2,mat,color,0,z))return NJ_ERR;
        }
    }
    return NJ_OK;
}

/* Heap-sort an index array using an explicit context. No comparator global and
 * no recursion; submission index breaks equal-depth ties deterministically. */
static int m3_order_less(const m3_qprim *q, nj_u4 a, nj_u4 b)
{
    return q[a].z>q[b].z || (q[a].z==q[b].z && a<b);
}
static void m3_order_down(nj_u4 *order, nj_u4 n, nj_u4 at, const m3_qprim *q)
{
    while(at<n/2u) {
        nj_u4 child=at*2u+1u, value;
        if(child+1u<n&&m3_order_less(q,order[child],order[child+1u]))++child;
        if(!m3_order_less(q,order[at],order[child]))break;
        value=order[at];order[at]=order[child];order[child]=value;at=child;
    }
}
static void m3_order_sort(nj_u4 *order, nj_u4 n, const m3_qprim *q)
{
    nj_u4 i;
    for(i=0;i<n;++i)order[i]=i;
    for(i=n/2u;i>0;--i)m3_order_down(order,n,i-1u,q);
    for(i=n;i>1;--i) {
        nj_u4 value=order[0];order[0]=order[i-1u];order[i-1u]=value;
        m3_order_down(order,i-1u,0,q);
    }
}

NJFN(mascot_scene_bind)
{
    nj_ref self=NJ_THIS(args), g=NJ_ARG_REF(args,0);
    nj_gfx gfx;
    nj_surface surface;
    nj_u8 clip=0;
    if(!g)return nj_throw(vm,"java/lang/NullPointerException","Graphics");
    if(m3_ref(vm,self,"_g","Ljavax/microedition/lcdui/Graphics;"))
        return nj_throw(vm,"java/lang/IllegalStateException","Graphics3D already bound");
    if(nj_midp_gfx_load(vm,g,&gfx,&surface)&&gfx.surf) {
        nj_s8 x=gfx.clip_x-gfx.translate_x,y=gfx.clip_y-gfx.translate_y;
        nj_s8 x2=x+gfx.clip_w,y2=y+gfx.clip_h;
        if(x<0)x=0;
        if(y<0)y=0;
        if(x>gfx.surf->width)x=gfx.surf->width;
        if(y>gfx.surf->height)y=gfx.surf->height;
        if(x2<x)x2=x;
        if(y2<y)y2=y;
        if(x2>gfx.surf->width)x2=gfx.surf->width;
        if(y2>gfx.surf->height)y2=gfx.surf->height;
        clip=(nj_u8)x|((nj_u8)y<<16)|((nj_u8)x2<<32)|((nj_u8)y2<<48);
    }
    nj_field_write_bits64(vm,self,m3_field(vm,self,"_clip","J"),clip);
    m3_set_ref(vm,self,"_g","Ljavax/microedition/lcdui/Graphics;",g);
    return NJ_OK;
}
NJFN(mascot_scene_release)
{
    nj_ref self=NJ_THIS(args),g=NJ_ARG_REF(args,0);
    if(!g)return nj_throw(vm,"java/lang/NullPointerException","Graphics");
    if(g!=m3_ref(vm,self,"_g","Ljavax/microedition/lcdui/Graphics;"))
        return nj_throw(vm,"java/lang/IllegalArgumentException","Graphics binding mismatch");
    m3_scene_reset(vm,self);
    m3_set_ref(vm,self,"_g","Ljavax/microedition/lcdui/Graphics;",NULL);
    return NJ_OK;
}
NJFN(mascot_scene_flush)
{
    nj_ref self=NJ_THIS(args),g=m3_ref(vm,self,"_g","Ljavax/microedition/lcdui/Graphics;");
    nj_ref qref,refs,iref;
    nj_u4 n=(nj_u4)m3_get(vm,self,"_count"),i,*order;
    const m3_qprim *queue;
    nj_gfx gfx;
    nj_surface surface;
    nj_u8 clip;
    nj_s4 x1,y1,x2,y2;
    if(!g)return nj_throw(vm,"java/lang/IllegalStateException","no Graphics bound");
    if(!n){m3_scene_reset(vm,self);return NJ_OK;}
    if(!nj_midp_gfx_load(vm,g,&gfx,&surface)||!gfx.surf){m3_scene_reset(vm,self);return NJ_OK;}
    clip=nj_field_read_bits64(vm,self,m3_field(vm,self,"_clip","J"));
    x1=(nj_s4)(clip&65535u);y1=(nj_s4)((clip>>16)&65535u);
    x2=(nj_s4)((clip>>32)&65535u);y2=(nj_s4)(clip>>48);
    iref=m3_buffer(vm,self,"_order","[I","I",n);
    if(!iref)return NJ_ERR;
    qref=m3_ref(vm,self,"_queue","[B");refs=m3_ref(vm,self,"_textures","[Ljava/lang/Object;");
    queue=(const m3_qprim *)nj_array_data(qref);order=(nj_u4 *)nj_array_data(iref);
    m3_order_sort(order,n,queue);
    for(i=0;i<n;++i) {
        const m3_qprim *q=&queue[order[i]];
        nj_s4 blend=(q->material>>1)&3;
        if(q->kind==1) {
            if(q->v[0].x>=x1&&q->v[0].x<x2&&q->v[0].y>=y1&&q->v[0].y<y2)
                nj_mascot_raster_pixel(gfx.surf,q->v[0].x,q->v[0].y,q->color,blend);
        } else if(q->kind==2) {
            nj_mascot_raster_line(gfx.surf,x1,y1,x2,y2,q->v[0].x,q->v[0].y,q->v[1].x,q->v[1].y,q->color,blend);
        } else {
            nj_mascot_material mat;
            nj_mascot_vertex v[3];
            nj_ref tex=q->texture?nj_array_get_ref(refs,q->texture-1u):NULL;
            nj_ref env=q->environment?nj_array_get_ref(refs,q->environment-1u):NULL;
            int k;
            memset(&mat,0,sizeof(mat));mat.color=q->color;mat.blend=blend;mat.toon=q->toon;
            if(tex) {mat.flags|=NJ_MR_TEXTURED;mat.texture=nj_array_data(tex);mat.texture_len=(nj_s4)nj_array_length(tex);}
            if(q->material&NJ_MASCOT_MAT_COLORKEY)mat.flags|=NJ_MR_COLORKEY;
            if(q->material&NJ_MASCOT_MAT_LIGHTING) {
                mat.flags|=NJ_MR_SHADED;
                if(!(q->material&NJ_MASCOT_MAT_FLAT_NORMAL))mat.flags|=NJ_MR_LIGHTING;
                if(q->toon&1u)mat.flags|=NJ_MR_TOON;
            }
            if(env&&(q->material&NJ_MASCOT_MAT_SPECULAR)) {
                mat.flags|=NJ_MR_ENVMAP;mat.env=nj_array_data(env);mat.env_len=(nj_s4)nj_array_length(env);
            }
            for(k=0;k<3;++k) {
                v[k].x=q->v[k].x;v[k].y=q->v[k].y;v[k].u=q->v[k].u;v[k].v=q->v[k].v;
                v[k].light=q->v[k].light;v[k].eu=q->v[k].eu;v[k].ev=q->v[k].ev;
            }
            if(nj_mascot_raster_draw(gfx.surf,x1,y1,x2,y2,v,&mat)!=NJ_MR_OK) {
                m3_scene_reset(vm,self);
                return nj_throw(vm,"java/lang/IllegalStateException","invalid Micro3D texture storage");
            }
            vm->tris++;
        }
    }
    m3_scene_reset(vm,self);
    return NJ_OK;
}
NJFN(mascot_scene_draw_figure)
{
    nj_status st=mascot_scene_figure(vm,th,args,ret);
    return st==NJ_OK?mascot_scene_flush(vm,th,args,ret):st;
}
NJFN(mascot_scene_at_pattern)
{
    nj_ref data=m3_data(vm,NJ_THIS(args));
    int32_t pattern=0;
    if(!data)return nj_throw(vm,"java/lang/IllegalStateException","disposed ActionTable");
    if(nj_mascot_anim_pattern(nj_array_data(data),nj_array_length(data),NJ_ARG_INT(args,0),NJ_ARG_INT(args,1),0,&pattern)!=NJ_MASCOT_ANIM_OK)
        return nj_throw(vm,"java/lang/IllegalArgumentException","ActionTable action");
    ret->i=pattern;return NJ_OK;
}
NJFN(mascot_scene_fx_shading)
{
    nj_s4 shading=NJ_ARG_INT(args,0);
    nj_u4 old=(nj_u4)m3_get(vm,NJ_THIS(args),"_sh");
    if(shading<0||shading>1)return nj_throw(vm,"java/lang/IllegalArgumentException","Micro3D shading");
    m3_set(vm,NJ_THIS(args),"_sh",(nj_s4)((old&~1u)|(nj_u4)shading));return NJ_OK;
}
NJFN(mascot_scene_fx_toon)
{
    nj_s4 threshold=NJ_ARG_INT(args,0),high=NJ_ARG_INT(args,1),low=NJ_ARG_INT(args,2);
    nj_u4 old=(nj_u4)m3_get(vm,NJ_THIS(args),"_sh");
    if((nj_u4)threshold>255u||(nj_u4)high>255u||(nj_u4)low>255u)
        return nj_throw(vm,"java/lang/IllegalArgumentException","Micro3D toon thresholds");
    m3_set(vm,NJ_THIS(args),"_sh",(nj_s4)NJ_MR_TOON_PACK(old&1u,threshold,high,low));return NJ_OK;
}
