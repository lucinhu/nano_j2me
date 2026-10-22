/*
 * Mascot Capsule Micro3D v3 (com.mascotcapsule.micro3d.v3).
 *
 * WHAT THIS IS
 * ------------
 * Micro3D is the other 3D API that shipped on Japanese and Chinese feature
 * phones, older than JSR 184 and still used by two titles in the collection
 * (鬼泣3D中文版_3376, 生化危机3D(CAPCOM经典)_3671).  Both died in <clinit>
 * before this existed.
 *
 * It is FIXED POINT: a Vector3D is three ints, an AffineTrans is a 4x3 matrix
 * of ints, and 4096 is 1.0.  That is not an implementation detail - the games
 * build their own matrices, hand them over, and read individual entries back
 * (m00..m23 are public fields here precisely because both titles touch them
 * hundreds of times).  A float transform stack would disagree with the game's
 * own arithmetic, so everything below is integer.
 *
 * The member list is MEASURED, not guessed: javap over every class of both
 * titles gives 42 distinct member references under this package name.  The
 * constructors are the only thing added beyond that list, because a game has
 * to allocate the objects it names.
 *
 * WHAT IT DOES, AND WHAT IT DOES NOT DO
 * ------------------------------------
 * The scene arithmetic is real: integer rotations, matrix concatenation,
 * look-at, normalisation and the integer square root, all tested against a
 * libm oracle.  FigureLayout exposes the two projection modes the games use -
 * parallel-scale and perspective-FOV - and keeps the reference's defaults
 * (implicit identity affine, parallel 512/512 scale, centre 0,0); the other
 * two reference modes (parallel-size, perspective-WH) are not exposed because
 * no title in the corpus calls them.  renderPrimitives() draws solid points,
 * lines, triangles and quads through the public eleven-argument ABI, with the
 * reference's transform rounding, centre offsets, near/far clipping and
 * command colours; tests/test_mascot.c binds a real Image and checks the
 * pixels with Image.getRGB().
 *
 * Everything that needs data this runtime does not parse yet is refused
 * rather than approximated: textured and point-sprite primitives are
 * validated and then draw nothing (no colour is invented), Figure(String)/
 * renderFigure and ActionTable data are not read (MBAC/MTRA formats), and
 * ADD/SUB blending draws nothing because nj_gfx has no such operation.  A
 * wrong picture is harder to diagnose than no picture.
 *
 * TRIMMING
 * --------
 * NJ_ENABLE_MASCOT=0 removes every class here, and tests/test_mascot.c prints
 * a loud SKIP rather than passing silently.
 */

#include "nj_internal.h"
#include "nj_midp.h"

#include <math.h>

#if NJ_ENABLE_MASCOT

#define NJFN(name) NJ_NATIVE_FN(name)

/* Keep the per-triangle filler out of line: inlining it into the clipping
 * helper three times inflated that helper's C stack frame (measured with
 * -fstack-usage; the extra call per emitted triangle is noise). */
#if defined(__GNUC__)
#  define M3_NOINLINE __attribute__((noinline))
#else
#  define M3_NOINLINE
#endif

#define NJ_C_V3  "com/mascotcapsule/micro3d/v3/Vector3D"
#define NJ_C_XF  "com/mascotcapsule/micro3d/v3/AffineTrans"
#define NJ_C_G3D "com/mascotcapsule/micro3d/v3/Graphics3D"
#define NJ_C_FIG "com/mascotcapsule/micro3d/v3/Figure"
#define NJ_C_FL  "com/mascotcapsule/micro3d/v3/FigureLayout"
#define NJ_C_TEX "com/mascotcapsule/micro3d/v3/Texture"
#define NJ_C_LIG "com/mascotcapsule/micro3d/v3/Light"
#define NJ_C_FX  "com/mascotcapsule/micro3d/v3/Effect3D"
#define NJ_C_AT  "com/mascotcapsule/micro3d/v3/ActionTable"
#define NJ_C_U3D "com/mascotcapsule/micro3d/v3/Util3D"

/* 1.0 in Micro3D's fixed point. */
#define M3_ONE 4096

/* FigureLayout projection modes: the values are the public command
 * constants, so a layout built by another implementation with the same
 * values is understood here too.  The mode lives in the high byte of the
 * stored word and the FOV angle shares the low 11 bits (valid 1..2047), so
 * angle support does not cost FigureLayout another persistent field. */
#define M3_PROJ_MODE_MASK 0xFF000000u
#define M3_PROJ_PARALLEL  0x90000000u
#define M3_PROJ_FOV       0x92000000u
#define M3_ANGLE_MASK     0x7FFu

/* Reference default parallel scale (FigureLayout() uses 512,512). */
#define M3_DEFAULT_SCALE 512
#define M3_IMG "Ljavax/microedition/lcdui/Image;"

#define M(name, desc, flags) { name, desc, flags }
#define NF(a) ((int)(sizeof(a) / sizeof((a)[0])))

/* ------------------------------------------------------------------ */
/* Fixed point arithmetic                                             */
/* ------------------------------------------------------------------ */

/*
 * Integer square root, floor.
 *
 * Used by Util3D.sqrt() and by the normalisations, and it is a PLAIN integer
 * root: a game passes the SQUARED length (x*x+y*y+z*z, already scaled by
 * 4096*4096) and compares the result against its own coordinates, so returning
 * a fixed-point number here would be wrong by another factor of 4096.
 */
static nj_s4 m3_isqrt(nj_s8 v)
{
    nj_s8 x, y;

    if (v <= 0) {
        return 0;
    }
    x = v;
    y = (x + 1) / 2;
    while (y < x) {
        x = y;
        y = (x + v / x) / 2;
    }
    return (nj_s4)x;
}

/* (a * b) / 4096 with a 64-bit product: 4096*4096 already overflows 32 bits. */
static nj_s4 m3_mul(nj_s4 a, nj_s4 b)
{
    return (nj_s4)(((nj_s8)a * (nj_s8)b) / M3_ONE);
}

/* Micro3D uses 4096 phases per turn and rounds sine to the nearest 1/4096.
 * Reflect a quarter-wave ROM table: no degree quantisation, integer divide,
 * libm dependency or per-VM allocation. Unsigned phase reduction also makes
 * negative angles and INT_MIN/INT_MAX well-defined (including cosine's shift).
 */
#include "nj_mascot_sin.inc"

static nj_s4 m3_sin(nj_s4 turn)
{
    nj_u4 phase = (nj_u4)turn & 4095u;
    nj_u4 index = phase & 1023u;
    nj_s4 value;

    if (phase & 1024u) {
        index = 1024u - index;
    }
    value = m3_sin_quarter[index];
    return (phase & 2048u) ? -value : value;
}

static nj_s4 m3_cos(nj_s4 turn)
{
    return m3_sin((nj_s4)(((nj_u4)turn + 1024u) & 4095u));
}

/* ------------------------------------------------------------------ */
/* Field access through the receiver's own class                      */
/* ------------------------------------------------------------------ */

/*
 * Two lookups, one semantics.
 *
 * The slow one (NJ_MASCOT_FAST_FIELDS=0) takes the class NAME from the object
 * and resolves it again for every component.  That is what would let these
 * natives serve another spelling of the same API (a flat
 * com/mascotcapsule/, or Capcom's com/capcom/mascotcapsule/) without an
 * edit, and it is the pre-optimisation behaviour kept for A/B measurement.
 *
 * The fast one (default) uses the linked class the live receiver already
 * owns: no name hashing, no resolve cache probe, no string compare per matrix
 * component.  Both branches resolve the same class and the same fields for
 * every object this module can see; only the path differs.  The class names
 * above are still always correct - the compiled tables are untouched.
 *
 * The measured cost of the fast path is code size plus a 16-byte larger
 * native frame (compiler-dependent), not persistent RAM, which is why it is
 * a build switch.  nj_config.h owns the default.
 */
static nj_field *m3_field(nj_vm *vm, nj_ref o, const char *name, const char *desc)
{
#if NJ_MASCOT_FAST_FIELDS
    (void)vm;
    return o && o->cls ? nj_field_lookup_in(o->cls, name, desc, 0) : NULL;
#else
    const char *cls = nj_ref_class_name(o);

    if (!cls) {
        return NULL;
    }
    return nj_field_lookup(vm, cls, name, desc, 0);
#endif
}

/* Defined with the renderer helpers further down; the FigureLayout getter
 * above them needs the same field-by-receiver-class lookup. */
static nj_ref m3_ref(nj_vm *vm, nj_ref o, const char *name, const char *desc);

static nj_s4 m3_get(nj_vm *vm, nj_ref o, const char *name)
{
    nj_field *f = m3_field(vm, o, name, "I");

    return f ? nj_iget(vm, o, f) : 0;
}

static void m3_set(nj_vm *vm, nj_ref o, const char *name, nj_s4 v)
{
    nj_field *f = m3_field(vm, o, name, "I");

    if (f) {
        nj_iput(vm, o, f, v);
    }
}

static void m3_set_ref(nj_vm *vm, nj_ref o, const char *name, const char *desc, nj_ref v)
{
    nj_field *f = m3_field(vm, o, name, desc);

    if (f) {
        nj_rput(vm, o, f, v);
    }
}

/* ------------------------------------------------------------------ */
/* Vector3D                                                           */
/* ------------------------------------------------------------------ */

static const nj_builtin_field v3_fields[] = {
    { "x", "I", 0 },
    { "y", "I", 0 },
    { "z", "I", 0 }
};

static const nj_builtin_method v3_methods[] = {
    M("<init>", "()V", 0),
    M("<init>", "(III)V", 0),
    M("<init>", "(L" NJ_C_V3 ";)V", 0),
    M("set", "(III)V", 0),
    M("unit", "()V", 0),
    M("innerProduct", "(L" NJ_C_V3 ";)I", NJ_ACC_PUBLIC),
    M("outerProduct", "(L" NJ_C_V3 ";L" NJ_C_V3 ";)V", NJ_ACC_PUBLIC)
};

static void v3_put3(nj_vm *vm, nj_ref o, nj_s4 x, nj_s4 y, nj_s4 z)
{
    m3_set(vm, o, "x", x);
    m3_set(vm, o, "y", y);
    m3_set(vm, o, "z", z);
}

NJFN(mascot_v3_init)
{
    v3_put3(vm, NJ_THIS(args), 0, 0, 0);
    return NJ_OK;
}

NJFN(mascot_v3_init3)
{
    v3_put3(vm, NJ_THIS(args), NJ_ARG_INT(args, 0), NJ_ARG_INT(args, 1), NJ_ARG_INT(args, 2));
    return NJ_OK;
}

NJFN(mascot_v3_init_copy)
{
    nj_ref self = NJ_THIS(args);
    nj_ref o = NJ_ARG_REF(args, 0);

    if (!o) {
        return nj_throw(vm, "java/lang/NullPointerException", "v is null");
    }
    v3_put3(vm, self, m3_get(vm, o, "x"), m3_get(vm, o, "y"), m3_get(vm, o, "z"));
    return NJ_OK;
}

NJFN(mascot_v3_set)
{
    v3_put3(vm, NJ_THIS(args), NJ_ARG_INT(args, 0), NJ_ARG_INT(args, 1), NJ_ARG_INT(args, 2));
    return NJ_OK;
}

/*
 * unit(): scale to length 1.0, i.e. 4096.
 *
 * The zero vector is left alone rather than divided by zero - the result is
 * undefined in the API, and doing nothing is the only choice that cannot
 * corrupt a later transform.
 */
NJFN(mascot_v3_unit)
{
    nj_ref self = NJ_THIS(args);
    nj_s4 x = m3_get(vm, self, "x");
    nj_s4 y = m3_get(vm, self, "y");
    nj_s4 z = m3_get(vm, self, "z");
    nj_s4 len = m3_isqrt((nj_s8)x * x + (nj_s8)y * y + (nj_s8)z * z);

    if (len == 0) {
        return NJ_OK;
    }
    v3_put3(vm, self,
            (nj_s4)(((nj_s8)x * M3_ONE) / len),
            (nj_s4)(((nj_s8)y * M3_ONE) / len),
            (nj_s4)(((nj_s8)z * M3_ONE) / len));
    return NJ_OK;
}

/*
 * innerProduct(v): the receiver is one of the two vectors, v is the other.
 *
 * This is an INSTANCE method -- the API is "a.innerProduct(b)" -- so the
 * argument list holds only b and the receiver comes from NJ_THIS().  Reading
 * two arguments here is what made a test crash: the second slot does not exist
 * and held whatever the caller had left there.
 */
NJFN(mascot_v3_inner)
{
    nj_ref self = NJ_THIS(args);
    nj_ref b = NJ_ARG_REF(args, 0);

    if (!self || !b) {
        return nj_throw(vm, "java/lang/NullPointerException", "null vector");
    }
    ret->i = m3_mul(m3_get(vm, self, "x"), m3_get(vm, b, "x")) +
             m3_mul(m3_get(vm, self, "y"), m3_get(vm, b, "y")) +
             m3_mul(m3_get(vm, self, "z"), m3_get(vm, b, "z"));
    return NJ_OK;
}

/* outerProduct(a, b): the result goes into the receiver. */
NJFN(mascot_v3_outer)
{
    nj_ref self = NJ_THIS(args);
    nj_ref a = NJ_ARG_REF(args, 0);
    nj_ref b = NJ_ARG_REF(args, 1);
    nj_s4 ax, ay, az, bx, by, bz;

    if (!a || !b) {
        return nj_throw(vm, "java/lang/NullPointerException", "null vector");
    }
    ax = m3_get(vm, a, "x"); ay = m3_get(vm, a, "y"); az = m3_get(vm, a, "z");
    bx = m3_get(vm, b, "x"); by = m3_get(vm, b, "y"); bz = m3_get(vm, b, "z");
    v3_put3(vm, self,
            m3_mul(ay, bz) - m3_mul(az, by),
            m3_mul(az, bx) - m3_mul(ax, bz),
            m3_mul(ax, by) - m3_mul(ay, bx));
    return NJ_OK;
}

/* ------------------------------------------------------------------ */
/* AffineTrans: a 4x3 matrix of ints, translation in column 3         */
/* ------------------------------------------------------------------ */

#define M3_NXF 12
#define XF(n) { "m" #n, "I", 0 }

static const nj_builtin_field xf_fields[] = {
    XF(00), XF(01), XF(02), XF(03),
    XF(10), XF(11), XF(12), XF(13),
    XF(20), XF(21), XF(22), XF(23)
};

static const char *const xf_name[M3_NXF] = {
    "m00", "m01", "m02", "m03",
    "m10", "m11", "m12", "m13",
    "m20", "m21", "m22", "m23"
};

static const nj_builtin_method xf_methods[] = {
    M("<init>", "()V", 0),
    M("setIdentity", "()V", 0),
    M("rotationX", "(I)V", 0),
    M("rotationY", "(I)V", 0),
    M("rotationZ", "(I)V", 0),
    M("multiply", "(L" NJ_C_XF ";L" NJ_C_XF ";)V", 0),
    M("multiply", "(L" NJ_C_XF ";)V", 0),
    /* The short spelling of the same operation; 浴火银河 uses this one. */
    M("mul", "(L" NJ_C_XF ";)V", 0),
    M("setViewTrans", "(L" NJ_C_V3 ";L" NJ_C_V3 ";L" NJ_C_V3 ";)V", 0),
    M("transform", "(L" NJ_C_V3 ";)V", 0),
    M("translation", "(L" NJ_C_V3 ";)V", 0)
};

static void xf_read(nj_vm *vm, nj_ref o, nj_s4 *m)
{
    int i;

    for (i = 0; i < M3_NXF; i++) {
        m[i] = m3_get(vm, o, xf_name[i]);
    }
}

static void xf_write(nj_vm *vm, nj_ref o, const nj_s4 *m)
{
    int i;

    for (i = 0; i < M3_NXF; i++) {
        m3_set(vm, o, xf_name[i], m[i]);
    }
}

static void xf_identity(nj_s4 *m)
{
    int i;

    for (i = 0; i < M3_NXF; i++) {
        m[i] = 0;
    }
    m[0] = M3_ONE;
    m[5] = M3_ONE;
    m[10] = M3_ONE;
}

/*
 * out = a * b.
 *
 * Only three columns exist: this is 4x3, not 4x4.  The translation column
 * (m03/m13/m23) is a full 3x3 product plus the incoming translation, not a
 * per-row dot product - getting that wrong is the classic concatenation bug
 * and it shows up as a camera that drifts every frame.
 */
static void xf_concat(const nj_s4 *a, const nj_s4 *b, nj_s4 *out)
{
    int r, c;

    for (r = 0; r < 3; r++) {
        for (c = 0; c < 3; c++) {
            out[r * 4 + c] = (nj_s4)(m3_mul(a[r * 4 + 0], b[0 * 4 + c]) +
                                     m3_mul(a[r * 4 + 1], b[1 * 4 + c]) +
                                     m3_mul(a[r * 4 + 2], b[2 * 4 + c]));
        }
        out[r * 4 + 3] = (nj_s4)(m3_mul(a[r * 4 + 0], b[3]) +
                                 m3_mul(a[r * 4 + 1], b[7]) +
                                 m3_mul(a[r * 4 + 2], b[11]) +
                                 a[r * 4 + 3]);
    }
}

NJFN(mascot_xf_init)
{
    nj_s4 m[M3_NXF];

    xf_identity(m);
    xf_write(vm, NJ_THIS(args), m);
    return NJ_OK;
}

NJFN(mascot_xf_multiply)
{
    nj_ref self = NJ_THIS(args);
    nj_ref a = NJ_ARG_REF(args, 0);
    nj_ref b = NJ_ARG_REF(args, 1);
    nj_s4 ma[M3_NXF], mb[M3_NXF], r[M3_NXF];

    if (!a || !b) {
        return nj_throw(vm, "java/lang/NullPointerException", "null operand");
    }
    xf_read(vm, a, ma);
    xf_read(vm, b, mb);
    xf_concat(ma, mb, r);
    xf_write(vm, self, r);
    return NJ_OK;
}

/* multiply(t): this = this * t */
NJFN(mascot_xf_multiply_in)
{
    nj_ref self = NJ_THIS(args);
    nj_ref t = NJ_ARG_REF(args, 0);
    nj_s4 ma[M3_NXF], mb[M3_NXF], r[M3_NXF];

    if (!t) {
        return nj_throw(vm, "java/lang/NullPointerException", "t is null");
    }
    xf_read(vm, self, ma);
    xf_read(vm, t, mb);
    xf_concat(ma, mb, r);
    xf_write(vm, self, r);
    return NJ_OK;
}

/* rotationX/Y/Z REPLACE the matrix with a pure rotation, as the API says. */
NJFN(mascot_xf_rot_x)
{
    nj_s4 a = NJ_ARG_INT(args, 0);
    nj_s4 s = m3_sin(a), c = m3_cos(a);
    nj_s4 m[M3_NXF];

    xf_identity(m);
    m[5] = c;  m[6] = -s;
    m[9] = s;  m[10] = c;
    xf_write(vm, NJ_THIS(args), m);
    return NJ_OK;
}

NJFN(mascot_xf_rot_y)
{
    nj_s4 a = NJ_ARG_INT(args, 0);
    nj_s4 s = m3_sin(a), c = m3_cos(a);
    nj_s4 m[M3_NXF];

    xf_identity(m);
    m[0] = c;  m[2] = s;
    m[8] = -s; m[10] = c;
    xf_write(vm, NJ_THIS(args), m);
    return NJ_OK;
}

NJFN(mascot_xf_rot_z)
{
    nj_s4 a = NJ_ARG_INT(args, 0);
    nj_s4 s = m3_sin(a), c = m3_cos(a);
    nj_s4 m[M3_NXF];

    xf_identity(m);
    m[0] = c;  m[1] = -s;
    m[4] = s;  m[5] = c;
    xf_write(vm, NJ_THIS(args), m);
    return NJ_OK;
}

/*
 * setViewTrans(position, lookAt, up): a right-handed look-at matrix.
 *
 * The basis vectors are normalised with the same integer square root the games
 * use for their own vectors, and the translation is -basis * position.  A
 * degenerate view (position == lookAt, or up parallel to the direction) leaves
 * the matrix untouched instead of dividing by zero.
 */
NJFN(mascot_xf_set_view)
{
    nj_ref self = NJ_THIS(args);
    nj_ref pos = NJ_ARG_REF(args, 0);
    nj_ref at = NJ_ARG_REF(args, 1);
    nj_ref up = NJ_ARG_REF(args, 2);
    nj_s4 p[3], d[3], u[3], f[3], r[3], v[3], m[M3_NXF];
    nj_s4 len;
    int i;

    if (!pos || !at || !up) {
        return nj_throw(vm, "java/lang/NullPointerException", "null vector");
    }
    p[0] = m3_get(vm, pos, "x"); p[1] = m3_get(vm, pos, "y"); p[2] = m3_get(vm, pos, "z");
    d[0] = m3_get(vm, at, "x") - p[0];
    d[1] = m3_get(vm, at, "y") - p[1];
    d[2] = m3_get(vm, at, "z") - p[2];
    u[0] = m3_get(vm, up, "x"); u[1] = m3_get(vm, up, "y"); u[2] = m3_get(vm, up, "z");

    len = m3_isqrt((nj_s8)d[0] * d[0] + (nj_s8)d[1] * d[1] + (nj_s8)d[2] * d[2]);
    if (len == 0) {
        return NJ_OK;
    }
    for (i = 0; i < 3; i++) {
        f[i] = (nj_s4)(((nj_s8)d[i] * M3_ONE) / len);
    }

    r[0] = m3_mul(f[1], u[2]) - m3_mul(f[2], u[1]);
    r[1] = m3_mul(f[2], u[0]) - m3_mul(f[0], u[2]);
    r[2] = m3_mul(f[0], u[1]) - m3_mul(f[1], u[0]);
    len = m3_isqrt((nj_s8)r[0] * r[0] + (nj_s8)r[1] * r[1] + (nj_s8)r[2] * r[2]);
    if (len == 0) {
        return NJ_OK;
    }
    for (i = 0; i < 3; i++) {
        r[i] = (nj_s4)(((nj_s8)r[i] * M3_ONE) / len);
    }

    v[0] = m3_mul(r[1], f[2]) - m3_mul(r[2], f[1]);
    v[1] = m3_mul(r[2], f[0]) - m3_mul(r[0], f[2]);
    v[2] = m3_mul(r[0], f[1]) - m3_mul(r[1], f[0]);

    xf_identity(m);
    m[0] = r[0]; m[1] = r[1]; m[2] = r[2];
    m[4] = v[0]; m[5] = v[1]; m[6] = v[2];
    m[8] = f[0]; m[9] = f[1]; m[10] = f[2];
    m[3]  = (nj_s4)(-((nj_s8)r[0] * p[0] + (nj_s8)r[1] * p[1] + (nj_s8)r[2] * p[2]));
    m[7]  = (nj_s4)(-((nj_s8)v[0] * p[0] + (nj_s8)v[1] * p[1] + (nj_s8)v[2] * p[2]));
    m[11] = (nj_s4)(-((nj_s8)f[0] * p[0] + (nj_s8)f[1] * p[1] + (nj_s8)f[2] * p[2]));
    xf_write(vm, self, m);
    return NJ_OK;
}

NJFN(mascot_xf_transform)
{
    nj_ref self = NJ_THIS(args);
    nj_ref o = NJ_ARG_REF(args, 0);
    nj_s4 m[M3_NXF], x, y, z;

    if (!o) {
        return nj_throw(vm, "java/lang/NullPointerException", "v is null");
    }
    xf_read(vm, self, m);
    x = m3_get(vm, o, "x"); y = m3_get(vm, o, "y"); z = m3_get(vm, o, "z");
    v3_put3(vm, o,
            (nj_s4)(m3_mul(m[0], x) + m3_mul(m[1], y) + m3_mul(m[2], z) + m[3]),
            (nj_s4)(m3_mul(m[4], x) + m3_mul(m[5], y) + m3_mul(m[6], z) + m[7]),
            (nj_s4)(m3_mul(m[8], x) + m3_mul(m[9], y) + m3_mul(m[10], z) + m[11]));
    return NJ_OK;
}

NJFN(mascot_xf_translation)
{
    nj_ref self = NJ_THIS(args);
    nj_ref o = NJ_ARG_REF(args, 0);
    nj_s4 m[M3_NXF];

    if (!o) {
        return nj_throw(vm, "java/lang/NullPointerException", "v is null");
    }
    xf_read(vm, self, m);
    v3_put3(vm, o, m[3], m[7], m[11]);
    return NJ_OK;
}

/* ------------------------------------------------------------------ */
/* Util3D                                                             */
/* ------------------------------------------------------------------ */

static const nj_builtin_method u3d_methods[] = {
    M("<init>", "()V", 0),
    M("sqrt", "(I)I", NJ_ACC_PUBLIC | NJ_ACC_STATIC),
    M("sin", "(I)I", NJ_ACC_PUBLIC | NJ_ACC_STATIC),
    M("cos", "(I)I", NJ_ACC_PUBLIC | NJ_ACC_STATIC)
};

NJFN(mascot_u3d_sin)
{
    ret->i = m3_sin(NJ_ARG_INT(args, 0));
    return NJ_OK;
}

NJFN(mascot_u3d_cos)
{
    ret->i = m3_cos(NJ_ARG_INT(args, 0));
    return NJ_OK;
}

NJFN(mascot_u3d_sqrt)
{
    ret->i = m3_isqrt(NJ_ARG_INT(args, 0));
    return NJ_OK;
}

/* ------------------------------------------------------------------ */
/* The remaining classes: real stored state, no pixels                */
/* ------------------------------------------------------------------ */

static const nj_builtin_method fig_methods[] = {
    M("<init>", "()V", 0),
    /*
     * Figure(byte[] data): the model itself.  浴火银河 builds its figures this
     * way and cannot proceed without it.
     */
    M("<init>", "([B)V", 0),
    M("setTexture", "(L" NJ_C_TEX ";)V", 0),
    M("setTexture", "([L" NJ_C_TEX ";)V", 0),
    M("setPosture", "(L" NJ_C_AT ";II)V", 0),
    M("setLayout", "(L" NJ_C_FL ";)V", 0)
};

static const nj_builtin_field fig_fields[] = {
    { "_data", "[B", 0 },
    { "_tex", "L" NJ_C_TEX ";", 0 },
    { "_texs", "[L" NJ_C_TEX ";", 0 },
    { "_tbl", "L" NJ_C_AT ";", 0 },
    { "_lay", "L" NJ_C_FL ";", 0 },
    { "_a", "I", 0 },
    { "_b", "I", 0 }
};

static const nj_builtin_method fl_methods[] = {
    M("<init>", "()V", 0),
    M("setAffineTrans", "(L" NJ_C_XF ";)V", 0),
    M("getAffineTrans", "()L" NJ_C_XF ";", 0),
    M("setPerspective", "(III)V", 0),
    M("setCenter", "(II)V", 0),
    M("setScale", "(II)V", 0)
};

static const nj_builtin_field fl_fields[] = {
    { "_xf", "L" NJ_C_XF ";", 0 },
    { "_p", "I", 0 }, { "_near", "I", 0 }, { "_far", "I", 0 },
    { "_cx", "I", 0 }, { "_cy", "I", 0 },
    { "_sx", "I", 0 }, { "_sy", "I", 0 }
};

static const nj_builtin_method tex_methods[] = {
    M("<init>", "(II)V", 0),
    M("<init>", "(" M3_IMG ")V", 0),
    M("<init>", "(" M3_IMG "II)V", 0),
    /*
     * Texture(byte[] data, boolean alphaBlend): the data is a raw texture
     * bitmap whose layout the games know and this runtime does not.
     */
    M("<init>", "([BZ)V", 0)
};

static const nj_builtin_field tex_fields[] = {
    { "_img", M3_IMG, 0 }, { "_w", "I", 0 }, { "_h", "I", 0 },
    { "_data", "[B", 0 }, { "_blend", "I", 0 }
};

static const nj_builtin_method lig_methods[] = {
    M("<init>", "()V", 0),
    M("<init>", "(L" NJ_C_V3 ";II)V", 0),
    M("setDirection", "(L" NJ_C_V3 ";)V", 0),
    M("setAmbIntensity", "(I)V", 0),
    M("setDirIntensity", "(I)V", 0)
};

static const nj_builtin_field lig_fields[] = {
    { "_dir", "L" NJ_C_V3 ";", 0 },
    { "_amb", "I", 0 }, { "_dif", "I", 0 }
};

static const nj_builtin_method fx_methods[] = {
    M("<init>", "()V", 0),
    /*
     * Effect3D(Light, int shading, boolean semiTransparent, Texture
     * sphereMap): 浴火银河 builds its effects this way, and it is the
     * only form in the corpus that takes a light up front.
     */
    M("<init>", "(L" NJ_C_LIG ";IZL" NJ_C_TEX ";)V", 0),
    M("setLight", "(L" NJ_C_LIG ";)V", 0),
    M("setSphereMap", "(L" NJ_C_TEX ";)V", 0),
    M("setShading", "(I)V", 0),
    M("setSemiTransparentEnabled", "(Z)V", 0)
};

static const nj_builtin_field fx_fields[] = {
    { "_lig", "L" NJ_C_LIG ";", 0 },
    { "_sph", "L" NJ_C_TEX ";", 0 },
    { "_sh", "I", 0 }, { "_semi", "I", 0 }
};

static const nj_builtin_method at_methods[] = {
    M("<init>", "([BII)V", 0),
    /* The same model data, handed over whole rather than as a slice. */
    M("<init>", "([B)V", 0),
    M("getNumFrame", "(I)I", 0),
    M("getFrameValue", "(II)I", 0)
};

static const nj_builtin_field at_fields[] = {
    { "_data", "[B", 0 },
    { "_n", "I", 0 }, { "_pat", "I", 0 }
};

/* See mascot_fig_data: the pose table's bytes are kept, not understood. */
NJFN(mascot_at_data)
{
    m3_set_ref(vm, NJ_THIS(args), "_data", "[B", NJ_ARG_REF(args, 0));
    return NJ_OK;
}

static const nj_builtin_method g3d_methods[] = {
    M("<init>", "()V", 0),
    M("bind", "(Ljavax/microedition/lcdui/Graphics;)V", 0),
    M("release", "(Ljavax/microedition/lcdui/Graphics;)V", 0),
    M("flush", "()V", 0),
    M("renderPrimitives", "(L" NJ_C_TEX ";IIL" NJ_C_FL ";L" NJ_C_FX ";II[I[I[I[I)V", 0),
    M("renderFigure", "(L" NJ_C_FIG ";IIL" NJ_C_FL ";L" NJ_C_FX ";)V", 0)
};

static const nj_builtin_field g3d_fields[] = {
    { "_g", "Ljavax/microedition/lcdui/Graphics;", 0 }
};

/* Empty constructors: the classes above have no state of their own to set up. */
NJFN(mascot_noop)
{
    (void)vm;
    (void)args;
    (void)ret;
    return NJ_OK;
}

/* Figure */
/*
 * The model bytes are a Micro3D model file in a layout this runtime has no
 * documentation for, and there is no reference render of any corpus title to
 * check a parser against.  Keeping the bytes instead of throwing means a title
 * gets past the constructor and keeps running -- it does NOT mean the model is
 * understood, and renderFigure still draws nothing.
 *
 * This is the same choice as Sound.play and DeviceControl: accept the call,
 * keep what was given, and say plainly what is missing rather than inventing a
 * model that would make the picture wrong in a way nobody can debug.
 */
NJFN(mascot_fig_data)
{
    m3_set_ref(vm, NJ_THIS(args), "_data", "[B", NJ_ARG_REF(args, 0));
    return NJ_OK;
}

NJFN(mascot_fig_tex)
{
    m3_set_ref(vm, NJ_THIS(args), "_tex", "L" NJ_C_TEX ";", NJ_ARG_REF(args, 0));
    return NJ_OK;
}

NJFN(mascot_fig_texs)
{
    m3_set_ref(vm, NJ_THIS(args), "_texs", "[L" NJ_C_TEX ";", NJ_ARG_REF(args, 0));
    return NJ_OK;
}

NJFN(mascot_fig_posture)
{
    nj_ref self = NJ_THIS(args);

    m3_set_ref(vm, self, "_tbl", "L" NJ_C_AT ";", NJ_ARG_REF(args, 0));
    m3_set(vm, self, "_a", NJ_ARG_INT(args, 1));
    m3_set(vm, self, "_b", NJ_ARG_INT(args, 2));
    return NJ_OK;
}

NJFN(mascot_fig_layout)
{
    m3_set_ref(vm, NJ_THIS(args), "_lay", "L" NJ_C_FL ";", NJ_ARG_REF(args, 0));
    return NJ_OK;
}

/* FigureLayout */
/*
 * FigureLayout(): parallel projection at the reference's default 512/512
 * scale, centre (0,0), and an IMPLICIT identity affine.  The old constructor
 * was a no-op, so a layout that never called setScale() projected with zero
 * magnification and every primitive collapsed onto the centre pixel.
 *
 * The identity AffineTrans is deliberately not allocated: `_xf == NULL` means
 * "identity" to renderPrimitives, exactly like a zeroed transform in the
 * reference's native path would.  getAffineTrans() materialises the object
 * lazily and stores it, so Java code that does use the getter keeps the
 * reference's stable, mutable identity instance; a layout that never asks
 * (the common case) costs no extra heap.
 */
NJFN(mascot_fl_init)
{
    nj_ref self = NJ_THIS(args);

    m3_set(vm, self, "_p", (nj_s4)M3_PROJ_PARALLEL);
    m3_set(vm, self, "_sx", M3_DEFAULT_SCALE);
    m3_set(vm, self, "_sy", M3_DEFAULT_SCALE);
    return NJ_OK;
}

/*
 * setAffineTrans(m): store the caller's matrix.  A null argument resets the
 * layout to its implicit identity, the observable result of the reference's
 * setAffineTrans(null) (FigureLayout.java:69), without keeping an object
 * alive just to say "no transform".
 */
NJFN(mascot_fl_xf)
{
    m3_set_ref(vm, NJ_THIS(args), "_xf", "L" NJ_C_XF ";", NJ_ARG_REF(args, 0));
    return NJ_OK;
}

/*
 * getAffineTrans(): allocate the identity matrix on first use, store it and
 * hand out that same object afterwards.  The reference stores an
 * AffineTrans[] and returns affineArray[0], so callers can mutate the matrix
 * and have the renderer see the change; returning a fresh object every time
 * would break that contract.  On allocation failure nj_object_new() has
 * already raised OutOfMemoryError and we return NJ_ERR.
 */
NJFN(mascot_fl_get_xf)
{
    nj_ref self = NJ_THIS(args);
    nj_ref xf = m3_ref(vm, self, "_xf", "L" NJ_C_XF ";");

    if (!xf) {
        xf = nj_object_new(vm, NJ_C_XF);
        if (!xf) {
            return NJ_ERR;      /* nj_object_new() raised OutOfMemoryError */
        }
        m3_set(vm, xf, "m00", M3_ONE);
        m3_set(vm, xf, "m11", M3_ONE);
        m3_set(vm, xf, "m22", M3_ONE);
        m3_set_ref(vm, self, "_xf", "L" NJ_C_XF ";", xf);
    }
    ret->r = xf;
    return NJ_OK;
}

/*
 * setPerspective(zNear, zFar, angle), in that order.
 *
 * The old code stored arg0 as the projection scale, arg1 as near and arg2 as
 * far, so the near plane was the game's near value shifted by one - exactly
 * the kind of "looks almost right" bug a wrong picture hides.  The reference
 * validates the triple and raises IllegalArgumentException; a bad plane would
 * otherwise discard every primitive and look like an empty scene.
 */
NJFN(mascot_fl_perspective)
{
    nj_ref self = NJ_THIS(args);
    nj_s4 near_z = NJ_ARG_INT(args, 0);
    nj_s4 far_z = NJ_ARG_INT(args, 1);
    nj_s4 angle = NJ_ARG_INT(args, 2);

    if (near_z >= far_z || near_z < 1 || far_z > 32767 ||
        angle < 1 || angle > 2047) {
        return nj_throw(vm, "java/lang/IllegalArgumentException",
                        "setPerspective: invalid near/far/angle");
    }
    m3_set(vm, self, "_near", near_z);
    m3_set(vm, self, "_far", far_z);
    /* Mode in the high byte, angle in the low 11 bits: the validator above
     * keeps angle inside 1..2047, so the two never collide. */
    m3_set(vm, self, "_p",
           (nj_s4)(M3_PROJ_FOV | ((nj_u4)angle & M3_ANGLE_MASK)));
    return NJ_OK;
}

NJFN(mascot_fl_center)
{
    nj_ref self = NJ_THIS(args);

    m3_set(vm, self, "_cx", NJ_ARG_INT(args, 0));
    m3_set(vm, self, "_cy", NJ_ARG_INT(args, 1));
    return NJ_OK;
}

/* setScale(x,y) also switches the layout back to parallel projection. */
NJFN(mascot_fl_scale)
{
    nj_ref self = NJ_THIS(args);

    m3_set(vm, self, "_sx", NJ_ARG_INT(args, 0));
    m3_set(vm, self, "_sy", NJ_ARG_INT(args, 1));
    m3_set(vm, self, "_p", (nj_s4)M3_PROJ_PARALLEL);
    return NJ_OK;
}

/* Texture: remember the image and its size, which is what a caller can see. */
NJFN(mascot_tex_from_image)
{
    nj_ref self = NJ_THIS(args);
    nj_ref img = NJ_ARG_REF(args, 0);
    nj_s4 w = 0, h = 0;

    m3_set_ref(vm, self, "_img", M3_IMG, img);
    if (img) {
        nj_value r;
        if (nj_call(vm, img, "javax/microedition/lcdui/Image", "getWidth", "()I", NULL, &r) == NJ_OK) {
            w = r.i;
        }
        if (nj_call(vm, img, "javax/microedition/lcdui/Image", "getHeight", "()I", NULL, &r) == NJ_OK) {
            h = r.i;
        }
    }
    m3_set(vm, self, "_w", w);
    m3_set(vm, self, "_h", h);
    return NJ_OK;
}

/* Texture(int w, int h) makes an empty texture of a given size. */
NJFN(mascot_tex_from_size)
{
    nj_ref self = NJ_THIS(args);

    m3_set(vm, self, "_w", NJ_ARG_INT(args, 0));
    m3_set(vm, self, "_h", NJ_ARG_INT(args, 1));
    m3_set_ref(vm, self, "_img", M3_IMG, 0);
    return NJ_OK;
}

/* Light */
/*
 * Light(Vector3D direction, int dirIntensity, int ambIntensity).
 *
 * Two newly fetched titles (浴火银河) construct a light this way; the two
 * titles that were measured first only used the no-argument constructor, so
 * this form was missing until the wider corpus asked for it.
 */
/*
 * Texture(byte[], boolean) -- 浴火银河 builds its textures this way.
 *
 * The bytes are kept as they were handed over.  Nothing decodes them yet, so
 * _w/_h stay 0: inventing a size from the array length would be a guess, and a
 * wrong texture size is harder to notice than an obviously empty one.
 */
NJFN(mascot_tex_init_bytes)
{
    nj_ref self = NJ_THIS(args);

    m3_set_ref(vm, self, "_data", "[B", NJ_ARG_REF(args, 0));
    m3_set(vm, self, "_blend", NJ_ARG_INT(args, 1) ? 1 : 0);
    return NJ_OK;
}

NJFN(mascot_lig_init)
{
    nj_ref self = NJ_THIS(args);

    if (NJ_ARG_REF(args, 0)) {
        m3_set_ref(vm, self, "_dir", "L" NJ_C_V3 ";", NJ_ARG_REF(args, 0));
    }
    m3_set(vm, self, "_dif", NJ_ARG_INT(args, 1));
    m3_set(vm, self, "_amb", NJ_ARG_INT(args, 2));
    return NJ_OK;
}

NJFN(mascot_lig_dir)
{
    m3_set_ref(vm, NJ_THIS(args), "_dir", "L" NJ_C_V3 ";", NJ_ARG_REF(args, 0));
    return NJ_OK;
}

NJFN(mascot_lig_amb)
{
    m3_set(vm, NJ_THIS(args), "_amb", NJ_ARG_INT(args, 0));
    return NJ_OK;
}

NJFN(mascot_lig_dif)
{
    m3_set(vm, NJ_THIS(args), "_dif", NJ_ARG_INT(args, 0));
    return NJ_OK;
}

/* Effect3D */
NJFN(mascot_fx_init)
{
    nj_ref self = NJ_THIS(args);

    m3_set_ref(vm, self, "_lig", "L" NJ_C_LIG ";", NJ_ARG_REF(args, 0));
    m3_set(vm, self, "_sh", NJ_ARG_INT(args, 1));
    m3_set(vm, self, "_semi", NJ_ARG_INT(args, 2) ? 1 : 0);
    m3_set_ref(vm, self, "_sph", "L" NJ_C_TEX ";", NJ_ARG_REF(args, 3));
    return NJ_OK;
}

NJFN(mascot_fx_light)
{
    m3_set_ref(vm, NJ_THIS(args), "_lig", "L" NJ_C_LIG ";", NJ_ARG_REF(args, 0));
    return NJ_OK;
}

NJFN(mascot_fx_sphere)
{
    m3_set_ref(vm, NJ_THIS(args), "_sph", "L" NJ_C_TEX ";", NJ_ARG_REF(args, 0));
    return NJ_OK;
}

NJFN(mascot_fx_shading)
{
    m3_set(vm, NJ_THIS(args), "_sh", NJ_ARG_INT(args, 0));
    return NJ_OK;
}

NJFN(mascot_fx_semi)
{
    m3_set(vm, NJ_THIS(args), "_semi", NJ_ARG_INT(args, 0) ? 1 : 0);
    return NJ_OK;
}

/* ActionTable */
NJFN(mascot_at_init)
{
    nj_ref self = NJ_THIS(args);

    m3_set(vm, self, "_n", 0);
    m3_set(vm, self, "_pat", 0);
    return NJ_OK;
}

/*
 * getNumFrame / getFrameValue report 0.
 *
 * Honest about what it is: there is no action-table parser here, so this says
 * "no frames" rather than inventing a pose.  A game asking for a frame gets
 * the rest pose, which is the pose it already drew.
 */
NJFN(mascot_at_num_frame)
{
    ret->i = 0;
    return NJ_OK;
}

NJFN(mascot_at_frame_value)
{
    ret->i = 0;
    return NJ_OK;
}

/* Graphics3D */
NJFN(mascot_g3d_bind)
{
    m3_set_ref(vm, NJ_THIS(args), "_g", "Ljavax/microedition/lcdui/Graphics;",
               NJ_ARG_REF(args, 0));
    return NJ_OK;
}

NJFN(mascot_g3d_release)
{
    m3_set_ref(vm, NJ_THIS(args), "_g", "Ljavax/microedition/lcdui/Graphics;", 0);
    return NJ_OK;
}

/* ------------------------------------------------------------------ */
/* Graphics3D: the primitive rasterizer                               */
/* ------------------------------------------------------------------ */
/*
 * Graphics3D.renderPrimitives is the whole draw path of this API in one call.
 * The real argument list is measured from the corpus and matches javap:
 *
 *   arg 0 texture             arg 6 numPrimitives
 *   arg 1 x                   arg 7 vertexCoords[]: interleaved x,y,z ints
 *   arg 2 y                   arg 8 normals[]       (may be null)
 *   arg 3 FigureLayout        arg 9 textureCoords[] (may be null)
 *   arg 4 Effect3D            arg10 colors[]        (may be null)
 *   arg 5 command
 *
 * `count` is the number of PRIMITIVES, not the number of vertices: a quad
 * consumes four vertices and a triangle three.  The old code read y as the
 * count, three separate coordinate arrays as vertices, and treated the
 * result as a triangle strip - none of which is this ABI.
 *
 * The command word carries the primitive type in bits 24..31, the
 * per-primitive data flags in bits 8..13 (texture coordinates, colour type,
 * normal type) and the material blend bits in 5..6.
 *
 * Everything below is integer like the rest of this API.  Each vertex is
 * transformed by the layout's affine matrix (12.12 fixed point) and then
 * projected into the bound Graphics' framebuffer:
 *
 *   parallel (setScale):      px = ((x * scaleX) >> 12) + centreX
 *   perspective (setPerspective): px = (x * projScaleX) / z + centreX
 *
 * where centreX/Y = the layout's centre plus the x/y arguments of this call.
 * The layout default centre is (0,0); nothing here invents a screen centre.
 * For the FOV form projScaleX = fbWidth * 0.5 / tan(angle * PI / 4096), and
 * primitives behind the eye are clipped against the near/far planes instead
 * of being divided by a clamped z (that clamp turned a vertex behind the
 * camera into a huge wrong triangle).
 *
 * What the picture is and what it is not, said plainly:
 *   - solid triangles, quads, lines and points are implemented and clipped
 *     against the near and far planes;
 *   - colours come from the command flags, never invented: an untextured
 *     primitive with no colour data draws nothing (the reference returns
 *     too), an invalid colour or normal type raises IllegalArgumentException;
 *   - textured and point-sprite primitives are validated, then draw nothing:
 *     the runtime has no Micro3D texture sampler (the Texture bytes are a raw
 *     bitmap format this project cannot read yet), and painting them with an
 *     invented colour would be a wrong picture, not a partial one;
 *   - lighting is not implemented.  When a command asks for it (normal data
 *     present, PATTR_LIGHTING set and the effect carries a Light) the call
 *     draws nothing, because painting the flat command colour would be a
 *     wrong picture; without that flag normals are accepted and ignored, as
 *     the reference does for unlit materials;
 *   - HALF blend is approximated with 128/256 source-over alpha (the
 *     reference rounds exactly half of each channel; 128/255 is close but
 *     not bit-identical, so tests assert the approximation explicitly);
 *     ADD and SUB have no equivalent in nj_gfx, so submissions that ask for
 *     them under an effect with semi-transparency enabled draw nothing -
 *     drawing them opaque would be a wrong picture reported as supported;
 *   - primitives are painted in framebuffer coordinates: the bound
 *     Graphics' translate and clip are ignored (the reference renders into
 *     its own framebuffer, not into Java Graphics user space), and there is
 *     no depth buffer - primitives are painted in submission order, which
 *     matches the reference's order for the single-pass calls the corpus
 *     makes.
 * renderFigure stays a no-op: Figure carries model bytes whose format this
 * runtime does not parse, so there is no geometry to draw for it.
 */

/* Primitive type, from the command word's top byte. */
#define M3_PRIM_MASK     0xFF000000u
#define M3_PRIM_POINTS   0x01000000u
#define M3_PRIM_LINES    0x02000000u
#define M3_PRIM_TRIS     0x03000000u
#define M3_PRIM_QUADS    0x04000000u
#define M3_PRIM_SPRITES  0x05000000u

/* Per-primitive data flags. */
#define M3_PDATA_UV          0x3000
#define M3_PDATA_COLOR_MASK  0x0C00
#define M3_PDATA_COLOR_CMD   0x0400
#define M3_PDATA_COLOR_FACE  0x0800
#define M3_PDATA_COLOR_BAD   0x0C00
#define M3_PDATA_NORMAL_MASK 0x0300
#define M3_PDATA_NORMAL_BAD  0x0100
#define M3_PATTR_LIGHTING    0x0001
#define M3_PATTR_BLEND_MASK  0x0060
#define M3_PATTR_BLEND_HALF  0x0020
#define M3_PATTR_BLEND_ADD   0x0040
#define M3_PATTR_BLEND_SUB   0x0060

/* A triangle clipped by the near plane has at most 4 vertices, and clipping
 * that result by the far plane adds at most one more, so 5 slots suffice. */
#define M3_MAX_CLIP_VERTS 5

typedef struct m3_vtx {
    nj_s8 x;
    nj_s8 y;
    nj_s8 z;
} m3_vtx;

/* Read a reference field through the receiver's own class (see m3_field). */
static nj_ref m3_ref(nj_vm *vm, nj_ref o, const char *name, const char *desc)
{
    nj_field *f = m3_field(vm, o, name, desc);

    return f ? nj_field_read_ref(vm, o, f) : 0;
}

/*
 * Java's >> on a signed value is an arithmetic shift (floor division by
 * 4096).  The C standard leaves right shift of a negative value
 * implementation defined, so spell out the floor; the form below never
 * negates a value that could be INT64_MIN, so it is defined for every input
 * (the accumulator reaches that range with extreme int matrices).
 */
static nj_s8 m3_shr12(nj_s8 v)
{
    return v >= 0 ? (v / M3_ONE) : -1 - (-(v + 1) / M3_ONE);
}

/* m[0]*v[0] + m[1]*v[1] + m[2]*v[2] + 2048 for the 12.12 transform row.
 * The reference evaluates this in Java int: every product and the sum wrap
 * modulo 2^32.  Accumulating in nj_u4 reproduces exactly those bits (defined
 * in C) and keeps the transform output inside int32, so the later clip and
 * projection products cannot overflow int64.  Do not widen this to 64 bits:
 * that would disagree with the reference for extreme legal inputs. */
static nj_s4 m3_dot12(const nj_s4 *m, const nj_s4 *v)
{
    nj_u4 acc = (nj_u4)m[0] * (nj_u4)v[0];

    acc += (nj_u4)m[1] * (nj_u4)v[1];
    acc += (nj_u4)m[2] * (nj_u4)v[2];
    acc += 2048u;
    return (nj_s4)acc;
}

/* Narrow to the nj_gfx coordinate type without wrapping a malformed vertex;
 * nj_gfx clamps again internally before any 64-bit product. */
static nj_s4 m3_s4_of(nj_s8 v)
{
    if (v > 2147483647LL) {
        return 2147483647;
    }
    if (v < -2147483647LL - 1) {
        return -2147483647 - 1;
    }
    return (nj_s4)v;
}

/* Apply the 4x3 affine matrix to one vertex, with the reference's +2048/>>12
 * rounding and then the translation column. */
/* The Java reference adds the translation column in int arithmetic, so that
 * addition wraps modulo 2^32 too; the result still fits int32 and is stored
 * widened in m3_vtx to keep the clip/project helpers untouched. */
static void m3_xform(const nj_s4 *m, const nj_s4 *v, m3_vtx *out)
{
    out->x = (nj_s4)((nj_u4)m3_shr12(m3_dot12(m, v)) + (nj_u4)m[3]);
    out->y = (nj_s4)((nj_u4)m3_shr12(m3_dot12(m + 4, v)) + (nj_u4)m[7]);
    out->z = (nj_s4)((nj_u4)m3_shr12(m3_dot12(m + 8, v)) + (nj_u4)m[11]);
}

/* Crossing of a->b with z == plane, interpolated with the same 12-bit fixed
 * point fraction the reference line clipper uses. */
static void m3_clip_cross(const m3_vtx *a, const m3_vtx *b, nj_s8 plane,
                          m3_vtx *out)
{
    nj_s8 dz = b->z - a->z;
    nj_s8 frac = (dz != 0) ? (((plane - a->z) * M3_ONE) / dz) : 0;

    out->x = a->x + m3_shr12((b->x - a->x) * frac);
    out->y = a->y + m3_shr12((b->y - a->y) * frac);
    out->z = plane;
}

/* Sutherland-Hodgman against one z plane.  keep_ge keeps z >= plane,
 * otherwise z <= plane.  Returns the number of output vertices; the caller
 * passes a buffer big enough for n + 1 entries. */
static int m3_clip_plane(const m3_vtx *in, int n, nj_s8 plane, int keep_ge,
                         m3_vtx *out)
{
    int i, on = 0;

    for (i = 0; i < n; i++) {
        const m3_vtx *a = &in[i];
        const m3_vtx *b = &in[(i + 1) % n];
        int a_in = keep_ge ? (a->z >= plane) : (a->z <= plane);
        int b_in = keep_ge ? (b->z >= plane) : (b->z <= plane);

        if (a_in) {
            out[on++] = *a;
        }
        if (a_in != b_in) {
            m3_clip_cross(a, b, plane, &out[on++]);
        }
    }
    return on;
}

/* Clip one eye-space triangle against the near then the far plane, taking
 * the vertices by pointer (no input copy).  A triangle clipped by one plane
 * has at most 4 vertices, so 5 output slots cover both planes.  Returns 0
 * when nothing is left, otherwise the polygon vertex count (3..5). */
static int m3_clip3(const m3_vtx *a, const m3_vtx *b, const m3_vtx *c,
                    nj_s8 plane, int keep_ge, m3_vtx out[4])
{
    const m3_vtx *in[3];
    int i, on = 0;

    in[0] = a;
    in[1] = b;
    in[2] = c;
    for (i = 0; i < 3; i++) {
        const m3_vtx *p = in[i];
        const m3_vtx *q = in[(i + 1) % 3];
        int p_in = keep_ge ? (p->z >= plane) : (p->z <= plane);
        int q_in = keep_ge ? (q->z >= plane) : (q->z <= plane);

        if (p_in) {
            out[on++] = *p;
        }
        if (p_in != q_in) {
            m3_clip_cross(p, q, plane, &out[on++]);
        }
    }
    return on;
}

static int m3_clip_tri(const m3_vtx *a, const m3_vtx *b, const m3_vtx *c,
                       nj_s8 near_z, nj_s8 far_z,
                       m3_vtx out[M3_MAX_CLIP_VERTS])
{
    m3_vtx mid[4];
    int n = m3_clip3(a, b, c, near_z, 1, mid);

    if (n < 3) {
        return 0;
    }
    return m3_clip_plane(mid, n, far_z, 0, out);
}

/* Clip one line segment against the near and far planes in eye space.
 * Returns 0 when the whole segment is outside. */
static int m3_clip_line(m3_vtx *a, m3_vtx *b, nj_s8 near_z, nj_s8 far_z)
{
    m3_vtx t;

    if (a->z < near_z && b->z < near_z) {
        return 0;
    }
    if (a->z > far_z && b->z > far_z) {
        return 0;
    }
    if (a->z < near_z) {
        m3_clip_cross(a, b, near_z, &t);
        *a = t;
    } else if (b->z < near_z) {
        m3_clip_cross(b, a, near_z, &t);
        *b = t;
    }
    if (a->z > far_z) {
        m3_clip_cross(a, b, far_z, &t);
        *a = t;
    } else if (b->z > far_z) {
        m3_clip_cross(b, a, far_z, &t);
        *b = t;
    }
    return 1;
}

/* Project one transformed vertex to framebuffer coordinates. */
static void m3_project(const m3_vtx *v, int perspective, nj_s4 kx, nj_s4 ky,
                       nj_s4 cx, nj_s4 cy, nj_s4 *ox, nj_s4 *oy)
{
    nj_s8 px, py;

    if (perspective) {
        px = (v->x * (nj_s8)kx) / v->z + cx;
        py = (v->y * (nj_s8)ky) / v->z + cy;
    } else {
        px = m3_shr12(v->x * (nj_s8)kx) + cx;
        py = m3_shr12(v->y * (nj_s8)ky) + cy;
    }
    *ox = m3_s4_of(px);
    *oy = m3_s4_of(py);
}

/* Project and fill one already-clipped triangle. */
static M3_NOINLINE void m3_fill_tri(nj_gfx *g, const m3_vtx *a,
                        const m3_vtx *b, const m3_vtx *c, int perspective,
                        nj_s4 kx, nj_s4 ky, nj_s4 cx, nj_s4 cy)
{
    nj_s4 x1, y1, x2, y2, x3, y3;

    m3_project(a, perspective, kx, ky, cx, cy, &x1, &y1);
    m3_project(b, perspective, kx, ky, cx, cy, &x2, &y2);
    m3_project(c, perspective, kx, ky, cx, cy, &x3, &y3);
    nj_gfx_triangle(g, x1, y1, x2, y2, x3, y3, 1);
}

/* Draw one transformed triangle in perspective mode, clipping it first.
 * Each triangle is clipped on its own so a quad keeps the reference's
 * (0,1,3)/(1,2,3) split even when part of it crosses a clip plane; clipping
 * the quad as one polygon and fanning the result would silently move the
 * diagonal (and show with HALF blend).  Returns the number of triangles
 * emitted (0 when the triangle is entirely outside the view volume). */
static int m3_draw_clipped_tri(nj_gfx *g, const m3_vtx *a, const m3_vtx *b,
                               const m3_vtx *c, nj_s8 near_z, nj_s8 far_z,
                               nj_s4 kx, nj_s4 ky, nj_s4 cx, nj_s4 cy)
{
    m3_vtx cut[M3_MAX_CLIP_VERTS];
    int n = m3_clip_tri(a, b, c, near_z, far_z, cut);
    int j;

    for (j = 0; j + 2 < n; j++) {
        m3_fill_tri(g, &cut[0], &cut[j + 1], &cut[j + 2], 1, kx, ky, cx, cy);
    }
    return n >= 3 ? n - 2 : 0;
}

/* Validate that `a` is an int[] with at least `need` elements. */
static int m3_int_array_ok(nj_ref a, nj_s4 need)
{
    return a && a->kind == NJ_OBJ_ARRAY && a->aflags == NJ_ARRAY_T_INT &&
           (nj_s4)a->length >= need;
}

NJFN(mascot_g3d_renderPrimitives)
{
    nj_ref self = NJ_THIS(args);
    nj_ref g, layout, fx, texture, verts, uvs, colors, xf;
    nj_s4 m[12] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
    nj_s4 command, count, prim, color_type, vtx_per, i;
    nj_s4 px, py, cx, cy, sx, sy, near_z, far_z, angle;
    nj_u4 proj;
    nj_s4 kx = 0, ky = 0;
    int perspective = 0;
    int need_uv, blend = 0, semi, bm;
    nj_u4 color = 0u;
    const nj_s4 *vp;
    const nj_s4 *cp = NULL;
    nj_gfx gfx;
    nj_surface scratch;
    nj_s4 fbw, fbh;

    g = m3_ref(vm, self, "_g", "Ljavax/microedition/lcdui/Graphics;");
    texture = NJ_ARG_REF(args, 0);
    px = NJ_ARG_INT(args, 1);
    py = NJ_ARG_INT(args, 2);
    layout = NJ_ARG_REF(args, 3);
    fx = NJ_ARG_REF(args, 4);
    command = NJ_ARG_INT(args, 5);
    count = NJ_ARG_INT(args, 6);
    verts = NJ_ARG_REF(args, 7);
    uvs = NJ_ARG_REF(args, 9);
    colors = NJ_ARG_REF(args, 10);

    /* Reference order: required objects, count, reserved command bits, then
     * "is anything bound"; the per-flag array checks come after that. */
    if (!layout || !fx || !verts) {
        return nj_throw(vm, "java/lang/NullPointerException",
                        "renderPrimitives: layout, effect and vertexCoords are required");
    }
    if (count <= 0 || count > 255) {
        return nj_throw(vm, "java/lang/IllegalArgumentException",
                        "renderPrimitives: numPrimitives is out of range");
    }
    if ((command & 0x00FF0000) != 0) {
        return nj_throw(vm, "java/lang/IllegalArgumentException",
                        "renderPrimitives: reserved command bits are set");
    }
    if (!g) {
        return nj_throw(vm, "java/lang/IllegalStateException",
                        "renderPrimitives: no Graphics is bound");
    }

    prim = command & M3_PRIM_MASK;
    need_uv = ((command & M3_PDATA_UV) != 0) || (prim == M3_PRIM_SPRITES);

    if (need_uv && (!texture || !uvs)) {
        return nj_throw(vm, "java/lang/NullPointerException",
                        "renderPrimitives: texture data is required for this command");
    }
    if (((command & M3_PDATA_NORMAL_MASK) != 0) && !NJ_ARG_REF(args, 8)) {
        return nj_throw(vm, "java/lang/NullPointerException",
                        "renderPrimitives: normals are required for this command");
    }
    if (((command & M3_PDATA_COLOR_MASK) == M3_PDATA_COLOR_FACE) && !colors) {
        return nj_throw(vm, "java/lang/NullPointerException",
                        "renderPrimitives: per-face colours are required for this command");
    }

    switch (prim) {
    case M3_PRIM_POINTS:  vtx_per = 1; break;
    case M3_PRIM_LINES:   vtx_per = 2; break;
    case M3_PRIM_TRIS:    vtx_per = 3; break;
    case M3_PRIM_QUADS:   vtx_per = 4; break;
    case M3_PRIM_SPRITES: vtx_per = 1; break;
    default:
        return nj_throw(vm, "java/lang/IllegalArgumentException",
                        "renderPrimitives: unknown primitive type");
    }

    /* Interleaved x,y,z per vertex, count primitives.  A caller that hands
     * over a short array gets the same exception the reference's int[]
     * indexing would have raised. */
    if (!m3_int_array_ok(verts, count * vtx_per * 3)) {
        return nj_throw(vm, "java/lang/ArrayIndexOutOfBoundsException",
                        "renderPrimitives: vertexCoords is too short");
    }

    /* Texture coordinates win over colours, exactly as in the reference. */
    color_type = need_uv ? 0 : (command & M3_PDATA_COLOR_MASK);

    if (need_uv) {
        /*
         * The reference samples the Texture here.  This runtime cannot: the
         * texture data is a raw Micro3D bitmap whose layout is not read by
         * this project.  Validate the buffers a renderer would index (so a
         * malformed call still fails the same way), then draw nothing rather
         * than painting an invented colour.
         */
        if (!m3_int_array_ok(uvs, count * vtx_per * 2)) {
            return nj_throw(vm, "java/lang/ArrayIndexOutOfBoundsException",
                            "renderPrimitives: textureCoords is too short");
        }
        (void)texture;
        return NJ_OK;
    }

    if (color_type == M3_PDATA_COLOR_BAD) {
        return nj_throw(vm, "java/lang/IllegalArgumentException",
                        "renderPrimitives: invalid colour data type");
    }
    if ((prim == M3_PRIM_TRIS || prim == M3_PRIM_QUADS) &&
        (command & M3_PDATA_NORMAL_MASK) == M3_PDATA_NORMAL_BAD) {
        return nj_throw(vm, "java/lang/IllegalArgumentException",
                        "renderPrimitives: invalid normal data type");
    }
    if (color_type == 0) {
        return NJ_OK;               /* nothing to colour: reference draws nothing */
    }
    if (color_type == M3_PDATA_COLOR_CMD) {
        if (!m3_int_array_ok(colors, 1)) {
            return nj_throw(vm, "java/lang/ArrayIndexOutOfBoundsException",
                            "renderPrimitives: colors is too short");
        }
    } else if (color_type != M3_PDATA_COLOR_FACE) {
        return nj_throw(vm, "java/lang/IllegalArgumentException",
                        "renderPrimitives: invalid colour data type");
    } else if (!m3_int_array_ok(colors, count)) {
        return nj_throw(vm, "java/lang/ArrayIndexOutOfBoundsException",
                        "renderPrimitives: per-face colors is too short");
    }
    /* Lighting is not implemented.  The reference only applies it when the
     * command carries normal data, PATTR_LIGHTING is set and the effect has
     * a light; in that case the honest result is no picture, not the flat
     * command colour pretending to be a lit surface.  Commands without that
     * combination behave exactly like unlit materials in the reference. */
    if ((command & M3_PATTR_LIGHTING) != 0 &&
        (command & M3_PDATA_NORMAL_MASK) != 0 &&
        m3_ref(vm, fx, "_lig", "L" NJ_C_LIG ";") != 0) {
        return NJ_OK;
    }

    cp = (const nj_s4 *)nj_array_data(colors);
    vp = (const nj_s4 *)nj_array_data(verts);

    /* ---- figure layout: affine transform, projection, centre ---- */
    xf = m3_ref(vm, layout, "_xf", "L" NJ_C_XF ";");
    if (xf) {
        xf_read(vm, xf, m);
    } else {
        /* No matrix stored means identity: the layout allocates nothing on
         * construction, and setAffineTrans(null) is a reset to this. */
        xf_identity(m);
    }
    sx = m3_get(vm, layout, "_sx");
    sy = m3_get(vm, layout, "_sy");
    near_z = m3_get(vm, layout, "_near");
    far_z = m3_get(vm, layout, "_far");
    proj = (nj_u4)m3_get(vm, layout, "_p");
    angle = (nj_s4)(proj & M3_ANGLE_MASK);
    /* Java int addition wraps; doing this in signed C would be undefined for
     * a caller that sets an extreme centre.  The projection below then culls
     * the result out of the framebuffer like any other off-screen point. */
    cx = (nj_s4)((nj_u4)m3_get(vm, layout, "_cx") + (nj_u4)px);
    cy = (nj_s4)((nj_u4)m3_get(vm, layout, "_cy") + (nj_u4)py);

    /* ---- one Graphics load for the whole batch: no per-triangle field
     *      traffic, and nothing is stored back, so the caller's Graphics
     *      colour/position state does not change. ---- */
    if (!nj_midp_gfx_load(vm, g, &gfx, &scratch)) {
        return NJ_OK;               /* not a usable Graphics */
    }
    fbw = gfx.surf ? (nj_s4)gfx.surf->width : 0;
    fbh = gfx.surf ? (nj_s4)gfx.surf->height : 0;
    if (fbw <= 0 || fbh <= 0) {
        return NJ_OK;
    }
    /* The reference paints framebuffer coordinates, not Graphics user space:
     * the loaded translate is already folded into the clip, so drop it from
     * this local copy (it is never stored back). */
    gfx.translate_x = 0;
    gfx.translate_y = 0;
    /* Micro3D renders into its own framebuffer: Java Graphics setClip() must
     * not crop the 3D picture, so the batch draws against the whole surface.
     * (The load above read the clip; it is replaced here in the local copy
     * only and never stored back.) */
    nj_gfx_set_clip(&gfx, 0, 0, (int)fbw, (int)fbh);

    if ((proj & M3_PROJ_MODE_MASK) == (M3_PROJ_FOV & M3_PROJ_MODE_MASK)) {
        double scale;

        if (near_z >= far_z || near_z < 1 || far_z > 32767 ||
            angle < 1 || angle > 2047) {
            return NJ_OK;           /* no valid perspective to project with */
        }
        scale = 0.5 / tan(((double)angle) / 4096.0 * 3.14159265358979323846);
        kx = (nj_s4)(((double)fbw) * scale);
        ky = kx;
        perspective = 1;
    } else {
        /* Parallel projection: the layout scale IS the 12.12 multiplier. */
        kx = sx;
        ky = sy;
    }

    /* Effect transparency only matters for the material blend bits. */
    semi = m3_get(vm, fx, "_semi") != 0;
    bm = semi ? (command & M3_PATTR_BLEND_MASK) : 0;
    if (bm == M3_PATTR_BLEND_ADD || bm == M3_PATTR_BLEND_SUB) {
        /* nj_gfx has no additive/subtractive blend.  The reference would
         * draw these primitives, so the honest result is no picture, not an
         * opaque approximation that claims support it does not have. */
        return NJ_OK;
    }
    if (bm == M3_PATTR_BLEND_HALF) {
        blend = 1;
        nj_gfx_set_blend(&gfx, NJ_BLEND_SRC_OVER);
    } else {
        nj_gfx_set_blend(&gfx, NJ_BLEND_NONE);
    }

    for (i = 0; i < count; i++) {
        const nj_s4 *base = vp + i * vtx_per * 3;
        m3_vtx v[4];
        int j;

        if (color_type == M3_PDATA_COLOR_CMD) {
            color = 0xFF000000u | (((nj_u4)cp[0]) & 0x00FFFFFFu);
        } else {
            color = 0xFF000000u | (((nj_u4)cp[i]) & 0x00FFFFFFu);
        }
        if (blend) {
            color = (color & 0x00FFFFFFu) | 0x80000000u;
        }
        nj_gfx_set_color(&gfx, color);

        if (prim == M3_PRIM_POINTS) {
            nj_s4 x, y;

            m3_xform(m, base, &v[0]);
            if (perspective &&
                (v[0].z < near_z || v[0].z > far_z)) {
                continue;
            }
            m3_project(&v[0], perspective, kx, ky, cx, cy, &x, &y);
            if (x < 0 || x >= fbw || y < 0 || y >= fbh) {
                continue;
            }
            nj_gfx_pixel(&gfx, x, y);
        } else if (prim == M3_PRIM_LINES) {
            nj_s4 x1, y1, x2, y2;

            m3_xform(m, base, &v[0]);
            m3_xform(m, base + 3, &v[1]);
            if (perspective && !m3_clip_line(&v[0], &v[1], near_z, far_z)) {
                continue;
            }
            m3_project(&v[0], perspective, kx, ky, cx, cy, &x1, &y1);
            m3_project(&v[1], perspective, kx, ky, cx, cy, &x2, &y2);
            /* Reference culls a segment only when BOTH endpoints are outside
             * the same framebuffer bound; nj_gfx clips the rest. */
            if ((x1 < 0 && x2 < 0) || (x1 >= fbw && x2 >= fbw) ||
                (y1 < 0 && y2 < 0) || (y1 >= fbh && y2 >= fbh)) {
                continue;
            }
            nj_gfx_line(&gfx, x1, y1, x2, y2);
        } else {
            /* Triangles and quads.  A quad is two triangles with the
             * reference's (0,1,3)/(1,2,3) split, not a triangle strip. */
            for (j = 0; j < (int)vtx_per; j++) {
                m3_xform(m, base + j * 3, &v[j]);
            }
            /* Quad split first, clip second: each of the reference's two
             * triangles is clipped independently, never as one polygon. */
            {
                int nt = (vtx_per == 4) ? 2 : 1;
                int t;

                for (t = 0; t < nt; t++) {
                    const m3_vtx *ta = &v[t == 0 ? 0 : 1];
                    const m3_vtx *tb = &v[t == 0 ? 1 : 2];
                    const m3_vtx *tc = &v[vtx_per == 4 ? 3 : 2];

                    if (perspective) {
                        vm->tris += (nj_u8)m3_draw_clipped_tri(
                            &gfx, ta, tb, tc, near_z, far_z, kx, ky, cx, cy);
                    } else {
                        m3_fill_tri(&gfx, ta, tb, tc, 0, kx, ky, cx, cy);
                        vm->tris++;
                    }
                }
            }
        }
    }
    return NJ_OK;
}

/* ------------------------------------------------------------------ */
/* Tables                                                             */
/* ------------------------------------------------------------------ */

const nj_builtin_class nj_lib_mascot_classes[] = {
    { NJ_C_V3, "java/lang/Object", NJ_ACC_PUBLIC,
      v3_fields, NF(v3_fields), v3_methods, NF(v3_methods) },
    { NJ_C_XF, "java/lang/Object", NJ_ACC_PUBLIC,
      xf_fields, NF(xf_fields), xf_methods, NF(xf_methods) },
    { NJ_C_U3D, "java/lang/Object", NJ_ACC_PUBLIC,
      NULL, 0u, u3d_methods, NF(u3d_methods) },
    { NJ_C_FIG, "java/lang/Object", NJ_ACC_PUBLIC,
      fig_fields, NF(fig_fields), fig_methods, NF(fig_methods) },
    { NJ_C_FL, "java/lang/Object", NJ_ACC_PUBLIC,
      fl_fields, NF(fl_fields), fl_methods, NF(fl_methods) },
    { NJ_C_TEX, "java/lang/Object", NJ_ACC_PUBLIC,
      tex_fields, NF(tex_fields), tex_methods, NF(tex_methods) },
    { NJ_C_LIG, "java/lang/Object", NJ_ACC_PUBLIC,
      lig_fields, NF(lig_fields), lig_methods, NF(lig_methods) },
    { NJ_C_FX, "java/lang/Object", NJ_ACC_PUBLIC,
      fx_fields, NF(fx_fields), fx_methods, NF(fx_methods) },
    { NJ_C_AT, "java/lang/Object", NJ_ACC_PUBLIC,
      at_fields, NF(at_fields), at_methods, NF(at_methods) },
    { NJ_C_G3D, "java/lang/Object", NJ_ACC_PUBLIC,
      g3d_fields, NF(g3d_fields), g3d_methods, NF(g3d_methods) },
    { NULL, NULL, 0u, NULL, 0u, NULL, 0u }
};

const int nj_lib_mascot_class_count =
    (int)(sizeof(nj_lib_mascot_classes) / sizeof(nj_lib_mascot_classes[0])) - 1;

const nj_native_entry nj_lib_mascot_table[] = {
    { NJ_C_V3, "<init>", "()V", mascot_v3_init },
    { NJ_C_V3, "<init>", "(III)V", mascot_v3_init3 },
    { NJ_C_V3, "<init>", "(L" NJ_C_V3 ";)V", mascot_v3_init_copy },
    { NJ_C_V3, "set", "(III)V", mascot_v3_set },
    { NJ_C_V3, "unit", "()V", mascot_v3_unit },
    { NJ_C_V3, "innerProduct", "(L" NJ_C_V3 ";)I", mascot_v3_inner },
    { NJ_C_V3, "outerProduct", "(L" NJ_C_V3 ";L" NJ_C_V3 ";)V", mascot_v3_outer },

    { NJ_C_XF, "<init>", "()V", mascot_xf_init },
    { NJ_C_XF, "setIdentity", "()V", mascot_xf_init },
    { NJ_C_XF, "rotationX", "(I)V", mascot_xf_rot_x },
    { NJ_C_XF, "rotationY", "(I)V", mascot_xf_rot_y },
    { NJ_C_XF, "rotationZ", "(I)V", mascot_xf_rot_z },
    { NJ_C_XF, "multiply", "(L" NJ_C_XF ";L" NJ_C_XF ";)V", mascot_xf_multiply },
    { NJ_C_XF, "multiply", "(L" NJ_C_XF ";)V", mascot_xf_multiply_in },
    { NJ_C_XF, "mul", "(L" NJ_C_XF ";)V", mascot_xf_multiply_in },
    { NJ_C_XF, "setViewTrans", "(L" NJ_C_V3 ";L" NJ_C_V3 ";L" NJ_C_V3 ";)V", mascot_xf_set_view },
    { NJ_C_XF, "transform", "(L" NJ_C_V3 ";)V", mascot_xf_transform },
    { NJ_C_XF, "translation", "(L" NJ_C_V3 ";)V", mascot_xf_translation },

    { NJ_C_U3D, "<init>", "()V", mascot_noop },
    { NJ_C_U3D, "sqrt", "(I)I", mascot_u3d_sqrt },
    { NJ_C_U3D, "sin", "(I)I", mascot_u3d_sin },
    { NJ_C_U3D, "cos", "(I)I", mascot_u3d_cos },

    { NJ_C_FIG, "<init>", "()V", mascot_noop },
    { NJ_C_FIG, "<init>", "([B)V", mascot_fig_data },
    { NJ_C_FIG, "setTexture", "(L" NJ_C_TEX ";)V", mascot_fig_tex },
    { NJ_C_FIG, "setTexture", "([L" NJ_C_TEX ";)V", mascot_fig_texs },
    { NJ_C_FIG, "setPosture", "(L" NJ_C_AT ";II)V", mascot_fig_posture },
    { NJ_C_FIG, "setLayout", "(L" NJ_C_FL ";)V", mascot_fig_layout },

    { NJ_C_FL, "<init>", "()V", mascot_fl_init },
    { NJ_C_FL, "setAffineTrans", "(L" NJ_C_XF ";)V", mascot_fl_xf },
    { NJ_C_FL, "getAffineTrans", "()L" NJ_C_XF ";", mascot_fl_get_xf },
    { NJ_C_FL, "setPerspective", "(III)V", mascot_fl_perspective },
    { NJ_C_FL, "setCenter", "(II)V", mascot_fl_center },
    { NJ_C_FL, "setScale", "(II)V", mascot_fl_scale },

    { NJ_C_TEX, "<init>", "(II)V", mascot_tex_from_size },
    { NJ_C_TEX, "<init>", "(" M3_IMG ")V", mascot_tex_from_image },
    { NJ_C_TEX, "<init>", "(" M3_IMG "II)V", mascot_tex_from_image },

    { NJ_C_TEX, "<init>", "([BZ)V", mascot_tex_init_bytes },
    { NJ_C_FX, "<init>", "(L" NJ_C_LIG ";IZL" NJ_C_TEX ";)V", mascot_fx_init },
    { NJ_C_LIG, "<init>", "()V", mascot_noop },
    { NJ_C_LIG, "<init>", "(L" NJ_C_V3 ";II)V", mascot_lig_init },
    { NJ_C_LIG, "setDirection", "(L" NJ_C_V3 ";)V", mascot_lig_dir },
    { NJ_C_LIG, "setAmbIntensity", "(I)V", mascot_lig_amb },
    { NJ_C_LIG, "setDirIntensity", "(I)V", mascot_lig_dif },

    { NJ_C_FX, "<init>", "()V", mascot_noop },
    { NJ_C_FX, "setLight", "(L" NJ_C_LIG ";)V", mascot_fx_light },
    { NJ_C_FX, "setSphereMap", "(L" NJ_C_TEX ";)V", mascot_fx_sphere },
    { NJ_C_FX, "setShading", "(I)V", mascot_fx_shading },
    { NJ_C_FX, "setSemiTransparentEnabled", "(Z)V", mascot_fx_semi },

    { NJ_C_AT, "<init>", "([BII)V", mascot_at_init },
    { NJ_C_AT, "<init>", "([B)V", mascot_at_data },
    { NJ_C_AT, "getNumFrame", "(I)I", mascot_at_num_frame },
    { NJ_C_AT, "getFrameValue", "(II)I", mascot_at_frame_value },

    { NJ_C_G3D, "<init>", "()V", mascot_noop },
    { NJ_C_G3D, "bind", "(Ljavax/microedition/lcdui/Graphics;)V", mascot_g3d_bind },
    { NJ_C_G3D, "release", "(Ljavax/microedition/lcdui/Graphics;)V", mascot_g3d_release },
    { NJ_C_G3D, "flush", "()V", mascot_noop },
    { NJ_C_G3D, "renderPrimitives",
      "(L" NJ_C_TEX ";IIL" NJ_C_FL ";L" NJ_C_FX ";II[I[I[I[I)V", mascot_g3d_renderPrimitives },
    { NJ_C_G3D, "renderFigure", "(L" NJ_C_FIG ";IIL" NJ_C_FL ";L" NJ_C_FX ";)V", mascot_noop }
};

const int nj_lib_mascot_count =
    (int)(sizeof(nj_lib_mascot_table) / sizeof(nj_lib_mascot_table[0]));

#else /* !NJ_ENABLE_MASCOT */

/*
 * The module is trimmed out, but these four symbols still exist and are empty:
 * the registration in nj_builtins.c/nj_native.c is unconditional, exactly as
 * the m3g module does it, so that -DNJ_ENABLE_MASCOT=0 removes the classes
 * without every caller needing its own #if.
 */
const nj_builtin_class nj_lib_mascot_classes[] = {
    { NULL, NULL, 0u, NULL, 0u, NULL, 0u }
};
const int nj_lib_mascot_class_count = 0;
const nj_native_entry nj_lib_mascot_table[1];
const int nj_lib_mascot_count = 0;


#endif /* NJ_ENABLE_MASCOT */
