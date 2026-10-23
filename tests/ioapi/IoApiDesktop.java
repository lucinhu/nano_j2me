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
            p("io.enc.default_read", StreamEncoding.defaultLatin1Read());
            p("io.enc.default_write", StreamEncoding.defaultLatin1Write());
            p("io.enc.latin1_alias_read", StreamEncoding.latin1AliasesRead());
            p("io.enc.latin1_alias_write", StreamEncoding.latin1AliasesWrite());
            p("io.enc.unknown_reader", StreamEncoding.unknownReader());
            p("io.enc.unknown_writer", StreamEncoding.unknownWriter());
            p("io.enc.null_reader", StreamEncoding.nullReader());
            p("io.enc.null_writer", StreamEncoding.nullWriter());
            p("io.enc.closed_reader", StreamEncoding.closedReader());
            p("io.enc.closed_writer", StreamEncoding.closedWriter());
            p("io.enc.utf8_alias", StreamEncoding.utf8Aliases());
            p("io.enc.utf8_read", StreamEncoding.utf8Read());
            p("io.enc.split_supplementary", StreamEncoding.splitSupplementary());
            p("io.enc.ready_pending_low", StreamEncoding.readyPendingLow());
            p("io.enc.skip_pending_low", StreamEncoding.skipPendingLow());
            p("io.enc.utf8_write", StreamEncoding.utf8WriteOverloads());
            p("io.enc.writer_cross_calls", StreamEncoding.writerCrossCalls());
            p("io.enc.close_dangling_high", StreamEncoding.closeDanglingHigh());
            p("io.enc.closed_empty_string", StreamEncoding.closedEmptyStringWrite());
            p("io.enc.failed_close_writer", StreamEncoding.failedCloseWriter());
            p("io.enc.write_bad_surrogates", StreamEncoding.malformedSurrogatesWrite());
            p("io.enc.ascii_alias", StreamEncoding.asciiAliases());
            p("io.enc.ascii_read", StreamEncoding.asciiRead());
            p("io.enc.ascii_write", StreamEncoding.asciiWrite());
            p("io.enc.invalid_lead", StreamEncoding.invalidLead());
            p("io.enc.overlong_two", StreamEncoding.overlongTwo());
            p("io.enc.overlong_three", StreamEncoding.overlongThree());
            p("io.enc.overlong_four", StreamEncoding.overlongFour());
            p("io.enc.truncated_two", StreamEncoding.truncatedTwo());
            p("io.enc.truncated_three", StreamEncoding.truncatedThree());
            p("io.enc.truncated_four", StreamEncoding.truncatedFour());
            p("io.enc.bad_cont_two", StreamEncoding.badContinuationTwo());
            p("io.enc.bad_cont_three", StreamEncoding.badContinuationThree());
            p("io.enc.bad_cont_four", StreamEncoding.badContinuationFour());
            p("io.enc.encoded_surrogates", StreamEncoding.encodedSurrogates());
            p("io.enc.above_limit", StreamEncoding.aboveUnicodeLimit());
            p("io.enc.modified_nul", StreamEncoding.modifiedNulRejected());
        } catch (Throwable t) {
            System.out.println("io.FATAL " + t);
        }
    }
}
