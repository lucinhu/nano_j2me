package ioapi;

/*
 * Desktop-JVM oracle for the embedded fixture.  Prints one "tag value" line
 * per case; tests/test_ioapi.c hard-codes the same values.  Not embedded in
 * the C test (it only runs on a desktop JVM).
 */
public final class IoApiDesktop {
    private IoApiDesktop() {
    }

    private static void p(String tag, int v) {
        System.out.println(tag + " " + v);
    }

    public static void main(String[] args) {
        try {
            p("io.f_read_known", IoApiProbe.readFloatBits());
            p("io.f_read_negzero", IoApiProbe.readFloatNegZeroBits());
            p("io.f_read_posinf", IoApiProbe.readFloatPosInfBits());
            p("io.f_read_nan", IoApiProbe.readFloatNanBits());
            p("io.f_read_eof", IoApiProbe.readFloatEof());
            p("io.f_read_trunc", IoApiProbe.readFloatTruncatedEof());
            p("io.d_read_pi", IoApiProbe.readDoublePiBits());
            p("io.d_read_negzero", IoApiProbe.readDoubleNegZeroBits());
            p("io.d_read_neginf", IoApiProbe.readDoubleNegInfBits());
            p("io.d_read_eof", IoApiProbe.readDoubleEof());
            p("io.o_write_floats", IoApiProbe.writeFloatBytes());
            p("io.o_write_float_len", IoApiProbe.writeFloatLength());
            p("io.o_write_doubles", IoApiProbe.writeDoubleBytes());
            p("io.o_write_chars", IoApiProbe.writeCharsBytes());
            p("io.roundtrip", IoApiProbe.roundTripBits());
            p("io.r_lock_self", LockReader.ctorNoArgSelf());
            p("io.r_lock_explicit", LockReader.ctorExplicitObject());
            p("io.r_lock_null", LockReader.ctorNullRejected());
            p("io.r_sync_read", LockReader.syncRead());
            p("io.r_read_range", LockReader.readRange());
            p("io.r_inherited_read", RangeOnlyReader.inheritedRead());
            p("io.r_inherited_read2", RangeOnlyReader.inheritedReadTwice());
            p("io.r_inherited_skip", RangeOnlyReader.inheritedSkip());
            p("io.r_inherited_ready", RangeOnlyReader.inheritedReady());
        } catch (Throwable t) {
            System.out.println("io.FATAL " + t);
        }
    }
}
