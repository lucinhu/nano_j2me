/*
 * Reads Holder's statics through fieldrefs.
 *
 * Compiled against the *plain* Holder (see Holder.plain.java), so javac does
 * not inline the values: every read below is a getstatic of Holder.a / .s /
 * .b / .l / .f.  Holder.class in the built jar is the *final* one, whose
 * ConstantValue attributes are the only place a / s / l / f have a value.
 */
public class CVRead {
    public static String line() {
        return "CV a=" + Holder.a
             + " s=" + Holder.s
             + " b=" + Holder.b
             + " l=" + Holder.l
             + " f=" + Holder.f;
    }
}
