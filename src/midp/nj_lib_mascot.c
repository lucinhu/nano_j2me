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
 * WHAT IT DOES NOT DO
 * -------------------
 * There is no rasterizer.  Graphics3D.bind/flush/renderPrimitives/renderFigure
 * accept their arguments and draw nothing.  That is deliberate and it is the
 * same rule the m3g module follows: a wrong picture is harder to diagnose than
 * no picture, so this module never invents pixels.  The scene arithmetic
 * (integer rotations, matrix concatenation, look-at, normalisation, integer
 * square root) is real and tested in tests/test_mascot.c, because that is the
 * part a game can observe through its own fields.
 *
 * TRIMMING
 * --------
 * NJ_ENABLE_MASCOT=0 removes every class here, and tests/test_mascot.c prints
 * a loud SKIP rather than passing silently.
 */

#include "nj_internal.h"
#include "nj_midp.h"

#if NJ_ENABLE_MASCOT

#define NJFN(name) NJ_NATIVE_FN(name)

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

/*
 * sin and cos for rotationX/Y/Z, without libm.
 *
 * The angle is in 4096ths of a full turn, which is what these calls take.
 * Bhaskara's approximation is used instead of a table: it is exact at 0, 90,
 * 180 and 270 degrees (where games put their right angles), within about 0.2%
 * in between, needs no data and no init step, and keeps this file building
 * when NJ_ENABLE_FLOAT=0 compiles the transcendental natives out.
 */
static nj_s4 m3_sin_deg(nj_s4 deg)
{
    nj_s8 t, b;

    deg %= 360;
    if (deg < 0) {
        deg += 360;
    }
    if (deg > 180) {
        deg -= 360;   /* sin(-x) = -sin(x) */
    }
    t = (nj_s8)4 * deg * (180 - deg);
    b = 40500 - (nj_s8)deg * (180 - deg);
    return (nj_s4)((t * M3_ONE) / b);
}

static nj_s4 m3_sin(nj_s4 turn)
{
    return m3_sin_deg((nj_s4)(((nj_s8)turn * 360) / M3_ONE));
}

static nj_s4 m3_cos(nj_s4 turn)
{
    return m3_sin((nj_s4)(turn + M3_ONE / 4));
}

/* ------------------------------------------------------------------ */
/* Field access through the receiver's own class                      */
/* ------------------------------------------------------------------ */

/*
 * Every accessor below takes the class NAME from the object rather than from a
 * constant.  That is what would let these natives serve another spelling of
 * the same API (a flat com/mascotcapsule/, or Capcom's
 * com/capcom/mascotcapsule/) if a title needs it - the descriptors are
 * identical, only the package differs.
 */
static nj_field *m3_field(nj_vm *vm, nj_ref o, const char *name, const char *desc)
{
    const char *cls = nj_ref_class_name(o);

    if (!cls) {
        return NULL;
    }
    return nj_field_lookup(vm, cls, name, desc, 0);
}

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
    M("sqrt", "(I)I", NJ_ACC_PUBLIC | NJ_ACC_STATIC)
};

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
NJFN(mascot_fl_xf)
{
    m3_set_ref(vm, NJ_THIS(args), "_xf", "L" NJ_C_XF ";", NJ_ARG_REF(args, 0));
    return NJ_OK;
}

NJFN(mascot_fl_perspective)
{
    nj_ref self = NJ_THIS(args);

    m3_set(vm, self, "_p", NJ_ARG_INT(args, 0));
    m3_set(vm, self, "_near", NJ_ARG_INT(args, 1));
    m3_set(vm, self, "_far", NJ_ARG_INT(args, 2));
    return NJ_OK;
}

NJFN(mascot_fl_center)
{
    nj_ref self = NJ_THIS(args);

    m3_set(vm, self, "_cx", NJ_ARG_INT(args, 0));
    m3_set(vm, self, "_cy", NJ_ARG_INT(args, 1));
    return NJ_OK;
}

NJFN(mascot_fl_scale)
{
    nj_ref self = NJ_THIS(args);

    m3_set(vm, self, "_sx", NJ_ARG_INT(args, 0));
    m3_set(vm, self, "_sy", NJ_ARG_INT(args, 1));
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
/* Graphics3D: the rasterizer                                         */
/* ------------------------------------------------------------------ */
/*
 * Mascot Capsule hands the vertices over directly: renderPrimitives receives
 * the coordinate arrays and a FigureLayout that carries both the affine
 * transform and the projection.  There is no scene graph to walk and no .m3g
 * container to parse, which is why the Micro3D rasterizer is written before
 * the JSR 184 one.
 *
 * It transforms each vertex by the layout's affine matrix, projects it with
 * the layout's perspective and fills the resulting triangles through the
 * Graphics the game bound.  Everything is 4096 fixed point, which is what the
 * API uses and what xf_read already returns.
 *
 * What it does not do, said plainly rather than hidden:
 *   - no texture sampling.  The games' Texture data is a raw bitmap whose
 *     layout is not documented and is not in a format this runtime reads, so
 *     every triangle is flat shaded from the effect's light intensities.
 *   - no depth buffer.  Triangles are painted in submission order, so a scene
 *     that relies on z ordering will look wrong where it overlaps itself.
 *   - renderFigure is still a no-op: a Figure carries posture tables and
 *     textures but this runtime has no model geometry for it.
 * With those limits it is still a real projection of the real vertices, so a
 * game's 3D content appears where before there was nothing at all.
 */
#define M3_ONE 4096

static nj_ref m3_get_ref(nj_vm *vm, nj_ref o, const char *cls,
                         const char *name, const char *desc)
{
    const nj_field *f;

    if (!o) {
        return 0;
    }
    f = nj_field_lookup(vm, cls, name, desc, 0);
    return f ? nj_field_read_ref(vm, o, f) : 0;
}

/* Light intensities are 0..4096; a light nobody configured shows mid grey
 * rather than black, because black is indistinguishable from "the projection
 * put it off screen". */
static nj_u4 m3_shade(nj_vm *vm, nj_ref fx)
{
    nj_ref lig;
    nj_s4 v;

    lig = m3_get_ref(vm, fx, NJ_C_FX, "_lig", "L" NJ_C_LIG ";");
    if (!lig) {
        return 0xC0C0C0u;
    }
    v = m3_get(vm, lig, "_amb") + m3_get(vm, lig, "_dif");
    if (v <= 0) {
        return 0xC0C0C0u;
    }
    if (v > M3_ONE) {
        v = M3_ONE;
    }
    v = v * 255 / M3_ONE;
    return ((nj_u4)v << 16) | ((nj_u4)v << 8) | (nj_u4)v;
}

static void m3_project(const nj_s4 *m, nj_s4 kx, nj_s4 ky, nj_s4 cx, nj_s4 cy,
                       nj_s4 x, nj_s4 y, nj_s4 z, nj_s4 *ox, nj_s4 *oy)
{
    nj_s8 vx = ((nj_s8)m[0] * x + (nj_s8)m[1] * y + (nj_s8)m[2] * z) / M3_ONE + m[3];
    nj_s8 vy = ((nj_s8)m[4] * x + (nj_s8)m[5] * y + (nj_s8)m[6] * z) / M3_ONE + m[7];
    nj_s8 vz = ((nj_s8)m[8] * x + (nj_s8)m[9] * y + (nj_s8)m[10] * z) / M3_ONE + m[11];

    if (vz < 1) {
        vz = 1;                     /* at or behind the eye: clamp, never divide by zero */
    }
    *ox = (nj_s4)(cx + (nj_s8)kx * vx / vz);
    *oy = (nj_s4)(cy + (nj_s8)ky * vy / vz);
}

NJFN(mascot_g3d_renderPrimitives)
{
    nj_ref self = NJ_THIS(args);
    nj_ref g, layout, fx, xf, xa, ya, za;
    nj_s4 m[12];
    nj_s4 p, sx, sy, cx, cy, kx, ky, count, i;
    nj_u4 colour;
    nj_value r;

    g = m3_get_ref(vm, self, NJ_C_G3D, "_g", "Ljavax/microedition/lcdui/Graphics;");
    if (!g) {
        return NJ_OK;               /* nothing bound: draw nothing, quietly */
    }
    layout = NJ_ARG_REF(args, 3);
    fx = NJ_ARG_REF(args, 4);
    count = NJ_ARG_INT(args, 2);
    xa = NJ_ARG_REF(args, 7);
    ya = NJ_ARG_REF(args, 8);
    za = NJ_ARG_REF(args, 9);
    if (!layout || !xa || !ya || !za || count < 3) {
        return NJ_OK;
    }

    for (i = 0; i < 12; i++) {
        m[i] = (i % 4 == 3) ? 0 : ((i / 4 == i % 4) ? M3_ONE : 0);
    }
    xf = m3_get_ref(vm, layout, NJ_C_FL, "_xf", "L" NJ_C_XF ";");
    if (xf) {
        xf_read(vm, xf, m);
    }

    p = m3_get(vm, layout, "_p");
    sx = m3_get(vm, layout, "_sx");
    sy = m3_get(vm, layout, "_sy");
    cx = m3_get(vm, layout, "_cx");
    cy = m3_get(vm, layout, "_cy");
    if (p == 0) {
        p = M3_ONE;
    }
    if (sx == 0) {
        sx = M3_ONE;
    }
    if (sy == 0) {
        sy = M3_ONE;
    }
    if (cx == 0 && cy == 0) {
        /* A title that never called setCenter still means the middle of the
         * screen.  Without this every model lands in the top-left corner,
         * which looks like a projection bug rather than a missing default. */
        if (nj_call(vm, g, "javax/microedition/lcdui/Graphics", "getWidth", "()I", NULL, &r) == NJ_OK) {
            cx = r.i / 2;
        }
        if (nj_call(vm, g, "javax/microedition/lcdui/Graphics", "getHeight", "()I", NULL, &r) == NJ_OK) {
            cy = r.i / 2;
        }
    }
    kx = (nj_s4)(((nj_s8)p * sx) / M3_ONE);
    ky = (nj_s4)(((nj_s8)p * sy) / M3_ONE);
    colour = m3_shade(vm, fx);

    for (i = 0; i + 2 < count; i++) {
        nj_s4 x1, y1, x2, y2, x3, y3;

        if (i + 2 >= xa->length || i + 2 >= ya->length || i + 2 >= za->length) {
            break;                  /* the arrays are shorter than the count says */
        }
        m3_project(m, kx, ky, cx, cy, ((nj_s4 *)xa->u.bytes)[i],
                   ((nj_s4 *)ya->u.bytes)[i], ((nj_s4 *)za->u.bytes)[i], &x1, &y1);
        m3_project(m, kx, ky, cx, cy, ((nj_s4 *)xa->u.bytes)[i + 1],
                   ((nj_s4 *)ya->u.bytes)[i + 1], ((nj_s4 *)za->u.bytes)[i + 1], &x2, &y2);
        m3_project(m, kx, ky, cx, cy, ((nj_s4 *)xa->u.bytes)[i + 2],
                   ((nj_s4 *)ya->u.bytes)[i + 2], ((nj_s4 *)za->u.bytes)[i + 2], &x3, &y3);
        if (nj_midp_gfx_triangle(vm, g, x1, y1, x2, y2, x3, y3, colour)) {
            vm->tris++;
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

    { NJ_C_FIG, "<init>", "()V", mascot_noop },
    { NJ_C_FIG, "<init>", "([B)V", mascot_fig_data },
    { NJ_C_FIG, "setTexture", "(L" NJ_C_TEX ";)V", mascot_fig_tex },
    { NJ_C_FIG, "setTexture", "([L" NJ_C_TEX ";)V", mascot_fig_texs },
    { NJ_C_FIG, "setPosture", "(L" NJ_C_AT ";II)V", mascot_fig_posture },
    { NJ_C_FIG, "setLayout", "(L" NJ_C_FL ";)V", mascot_fig_layout },

    { NJ_C_FL, "<init>", "()V", mascot_noop },
    { NJ_C_FL, "setAffineTrans", "(L" NJ_C_XF ";)V", mascot_fl_xf },
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
