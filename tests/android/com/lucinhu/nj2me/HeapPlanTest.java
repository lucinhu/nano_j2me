package com.lucinhu.nj2me;

/**
 * Automatic Java heap, off the device.
 *
 * <p>Requirement: a game that has not chosen a size must not be pinned to one
 * of the old fixed steps (8/16/32 MB).  The port derives the heap from the
 * phone's RAM instead, and a settings file from before the change - which wrote
 * heap=8 as a default - must move to automatic, while a size the player really
 * picked stays put.  Both decisions live in {@link HeapPlan}.
 */
public final class HeapPlanTest {

    private HeapPlanTest() {
    }

    private static long mb(long megabytes) {
        return megabytes * 1024L * 1024L;
    }

    public static void main(String[] args) {
        Check.section("heap from phone RAM");
        Check.eq("unknown RAM still starts a 3D title",
                 HeapPlan.autoHeapMBFor(0L), 32);
        Check.eq("256 MB phone", HeapPlan.autoHeapMBFor(mb(256)), 32);
        Check.eq("512 MB phone", HeapPlan.autoHeapMBFor(mb(512)), 32);
        Check.eq("768 MB phone", HeapPlan.autoHeapMBFor(mb(768)), 48);
        Check.eq("1 GB phone", HeapPlan.autoHeapMBFor(mb(1024)), 64);
        Check.eq("1.5 GB phone", HeapPlan.autoHeapMBFor(mb(1536)), 96);
        Check.eq("2 GB phone", HeapPlan.autoHeapMBFor(mb(2048)), 128);
        Check.eq("4 GB phone is capped", HeapPlan.autoHeapMBFor(mb(4096)), 128);
        Check.eq("a reported 1.8 GB phone",
                 HeapPlan.autoHeapMBFor(1932735283L), 112);
        Check.isTrue("values stay on the 8 MB grid",
                HeapPlan.autoHeapMBFor(1932735283L) % 8 == 0);
        Check.isTrue("never below the floor",
                HeapPlan.autoHeapMBFor(1L) >= HeapPlan.MIN_MB);
        Check.isTrue("never above the ceiling",
                HeapPlan.autoHeapMBFor(Long.MAX_VALUE) <= HeapPlan.MAX_MB);

        Check.section("settings files");
        Check.eq("the old default moves to automatic",
                 HeapPlan.fromSidecar(8, false), HeapPlan.AUTO);
        Check.eq("a size the player picked stays",
                 HeapPlan.fromSidecar(16, false), 16);
        Check.eq("a deliberate 8 MB stays 8 MB",
                 HeapPlan.fromSidecar(8, true), 8);
        Check.eq("a file with no heap line is automatic",
                 HeapPlan.fromSidecar(HeapPlan.AUTO, false), HeapPlan.AUTO);

        Check.done();
    }
}
