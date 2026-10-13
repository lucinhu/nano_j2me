/*
 * Pass 2 of the fixture: the same fields as `static final` constants, so javac
 * writes ConstantValue attributes and leaves a / s / l / f out of <clinit>.
 * `b` is deliberately left non-final: it is assigned by a <clinit> the runtime
 * really runs, which keeps the two paths in one class.
 */
public class Holder {
    public static final int a = 42;
    public static final String s = "cv-string";
    public static int b = 7;
    public static final long l = 1311768467463790320L;
    public static final float f = 2.5f;
}
