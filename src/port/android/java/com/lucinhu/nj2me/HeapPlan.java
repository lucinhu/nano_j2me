package com.lucinhu.nj2me;

/**
 * How much Java heap a game gets when the player has not chosen a size.
 *
 * <p>The runtime's heap is one arena it allocates and clears when the MIDlet
 * starts, so "automatic" has to be a fraction of the phone it is running on
 * rather than a number picked for a 2010 handset.  This class is the policy
 * alone - no Android types - so the arithmetic can be checked off the device.
 *
 * <p>{@link #AUTO} (zero) is the value stored in a game's settings file; the
 * port resolves it at launch, which means moving the same games folder to a
 * bigger phone raises the heap without editing every sidecar.
 */
final class HeapPlan {

    /** Stored heap value meaning "decide from the phone's RAM at launch". */
    static final int AUTO = 0;

    /** Floor: below this a 3D title dies inside its own loader. */
    static final int MIN_MB = 32;

    /** Ceiling: one MIDlet holding more than this is memory the phone wants. */
    static final int MAX_MB = 128;

    /**
     * The old fixed default.  Settings files written before automatic heaps
     * spelled 8 MB out, so reading one has to tell "the old default" from "the
     * player deliberately picked 8" - the latter now carries a marker.
     */
    static final int LEGACY_DEFAULT_MB = 8;

    private HeapPlan() {
    }

    /**
     * A heap that suits the phone: one sixteenth of its RAM, at least
     * {@link #MIN_MB}, at most {@link #MAX_MB}, rounded down to 8 MB.
     *
     * <p>One sixteenth is two steps more conservative than the runtime's own
     * default: a 1 GB handset gets 64 MB (what Minecraft PE needed here) and a
     * 2 GB one gets the 128 MB ceiling.  An unknown size ({@code totalBytes} 0,
     * as on a device where the framework would not answer) gets the floor,
     * which is still enough for a small title rather than a crash.
     */
    static int autoHeapMBFor(long totalBytes) {
        long totalMB = totalBytes > 0 ? totalBytes / (1024L * 1024L) : 0;
        long heapMB = totalMB / 16L;
        if (heapMB < MIN_MB) {
            heapMB = MIN_MB;
        }
        if (heapMB > MAX_MB) {
            heapMB = MAX_MB;
        }
        return (int) (heapMB & ~7L);
    }

    /**
     * The heap a sidecar asks for: its number, unless that number is the old
     * default and nobody marked it as a choice.
     */
    static int fromSidecar(int savedHeapMB, boolean explicitChoice) {
        if (!explicitChoice && savedHeapMB == LEGACY_DEFAULT_MB) {
            return AUTO;
        }
        return savedHeapMB;
    }
}
