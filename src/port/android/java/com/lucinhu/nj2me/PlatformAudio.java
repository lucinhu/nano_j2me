/*
 * nano_j2me - PlatformAudio.java
 *
 * The platform's audio decoders, behind the port's media hooks.
 *
 * WHY MediaCodec, AND NOT MediaExtractor
 * --------------------------------------
 * The runtime reads the sample formats it always read (WAV, AU, raw PCM) and
 * renders MIDI itself, but a J2ME game also ships MP3 and AMR, and those are
 * codecs, not file parsers.  Android has them, in hardware on most devices,
 * behind MediaCodec - and behind MediaCodec alone is where this file stands.
 *
 * It used to hand the file to MediaExtractor first, and that is exactly what
 * does not work on the test device.  The extractor is not a library: it is a
 * separate system service, "media.extractor", and on an Android 16 emulator
 * that service never comes up inside an app process:
 *
 *     ServiceManagerCppClient: Waiting for service 'media.extractor' ...
 *     ServiceManagerCppClient: Service media.extractor didn't start.
 *     MediaExtractorFactory: extractor service not running
 *     NuMediaExtractor: initMediaExtractor: failed to create MediaExtractor
 *     nj2me: audio: the platform cannot decode these 2525 bytes
 *            (java.io.IOException: Failed to instantiate extractor.)
 *
 * so every setDataSource() threw and every compressed asset was refused.
 * MediaCodec is a different service ("media.codec"), it is running, and a probe
 * drove c2.android.amrnb.decoder straight on that same emulator and got all 51
 * AMR frames / 16320 PCM bytes back.  So the codec stays and the container
 * parsing moves in here, which for J2ME's two compressed formats is small:
 *
 *   AMR   "#!AMR\n" (narrowband) or "#!AMR-WB\n" (wideband), then one ToC byte
 *         plus its payload per 20 ms frame.  That walk IS the file format
 *         (RFC 4867), and it is the same one src/midp/nj_amr.c makes for the
 *         runtime's own decoder, frame for frame.
 *   MP3   an ID3v2 tag and/or a run of MPEG audio frames.  Their lengths do not
 *         have to be parsed: a frame header gives the sample rate and the
 *         channel mode the decoder is configured with, and the decoder finds
 *         the frames in a byte stream itself - that is what a streaming decoder
 *         is for.
 *
 * Anything else is refused with null, which is a real answer rather than a
 * failure: a J2ME sound is usually WAV, MIDI or AMR, and the runtime has its
 * own readers for all three.  The containers this file no longer opens (MP4,
 * Ogg) are ones the extractor never opened on this device either.
 *
 * WHY AMR IS COUNTED AND MP3 IS DECODED
 * -------------------------------------
 * The runtime calls info() for the exact PCM length, allocates that much and
 * then calls decode(); the two must not disagree.  AMR cannot disagree: every
 * frame is 20 ms of mono 16-bit PCM (160 samples narrowband, 320 wideband), so
 * walking the frame headers answers info() exactly without opening a codec, and
 * decode() feeds the very frames that were counted.
 *
 * MP3 cannot be answered that way.  The first frame may be an Info/Xing header
 * rather than audio, the encoder pads the last frame and its gapless fields ask
 * the decoder to trim it back, and the decoder's own word on the sample rate
 * only arrives in the middle of the stream.  So info() decodes once and caches
 * the result; decode(), which the runtime always calls next on the same bytes,
 * is answered from the cache and costs nothing.  (The frame header is still
 * parsed: its sample rate and channel mode configure the decoder, and its
 * bitrate is what the feed timestamps are measured with.)
 *
 * WHAT THE C SIDE SEES
 * --------------------
 * Two statics, called on the VM thread: info() answers {sampleRate,
 * channelCount, frameCount, pcmBytes} and decode() hands back exactly
 * pcmBytes bytes of PCM - signed 16-bit little-endian, interleaved by frame,
 * which is the only encoding the runtime takes.  pcmBytes is the byte count,
 * not the width of a frame: the C side sizes the buffer it decodes into from
 * info()[3] and refuses the decode if decode() is not exactly that long (both
 * refusals are silent, so a width there costs a whole Player for no log
 * line).  null
 * means "the platform will not decode this" (nothing recognised it, no decoder
 * for it, not 16-bit, longer than the cap) and is not an error.
 *
 * The bytes go to MediaCodec as they are, so nothing is written to the app's
 * cache any more, and there is no temp file to delete on the way out.
 */
package com.lucinhu.nj2me;

import android.content.Context;
import android.media.AudioFormat;
import android.media.MediaCodec;
import android.media.MediaFormat;
import android.util.Log;
import android.util.LruCache;

import java.io.ByteArrayOutputStream;
import java.nio.ByteBuffer;
import java.util.Arrays;
import java.util.HashSet;
import java.util.Set;
import java.util.zip.CRC32;

final class PlatformAudio {

    private static final String TAG = "nj2me";

    /*
     * The runtime holds a whole clip in RAM, so this is a whole-clip decoder
     * and the cap is a promise to the device: 8 MiB of 44.1 kHz stereo 16-bit
     * is about 47 seconds, longer than any J2ME sound effect and most music
     * loops, and small enough to survive a low-end heap.  A file past it is
     * refused with a log line rather than decoded and then thrown away.
     */
    private static final int PCM_CAP = 8 * 1024 * 1024;

    /*
     * Decoded clips, keyed by a CRC32 of the bytes they came from.  Sized in
     * bytes, not entries: four of the 8 MiB results this decoder can produce
     * would be 32 MiB of retained PCM, while the cache exists only so that
     * info() and decode() - which the runtime calls back to back - do not
     * decode twice.  Two caps' worth means an accepted clip can never be
     * larger than the cache (LruCache drops anything past maxSize the moment
     * it is put, which would quietly defeat the hit).
     *
     * A CRC32 is not a cryptographic hash: two different sounds crafted to
     * collide would answer for each other.  The clip carries its source length
     * as a second check, and the only thing a collision can do is play the
     * wrong sound out of the JAR that supplied both - nothing here is a
     * security boundary.
     */
    private static final int CACHE_BYTES = 2 * PCM_CAP;

    private static final LruCache<Long, Clip> CLIPS =
            new LruCache<Long, Clip>(CACHE_BYTES) {
                @Override
                protected int sizeOf(Long key, Clip value) {
                    return value.pcm.length;
                }
            };

    /*
     * Every synchronous MediaCodec call takes a timeout in microseconds and
     * answers INFO_TRY_AGAIN_LATER when there is nothing to hand over.  Ten
     * milliseconds is short because this loop also feeds the input side, and a
     * run of empty polls without the decoder taking a buffer or producing one
     * means a decoder that is not going to move: the loop gives up rather than
     * spin the VM thread forever.
     */
    private static final long DEQUEUE_TIMEOUT_US = 10000L;
    private static final int STALL_POLLS = 200;

    /*
     * A stall is not the only way to be stuck - a decoder can keep accepting
     * input and produce nothing for as long as the file lasts, and the game's
     * frame thread is inside this call.  This is the hard bound on the whole
     * decode, generous enough for a slow device and small enough that a
     * malformed file cannot look like a hang.
     */
    private static final long WALL_CLOCK_NANOS = 20000000000L;

    /* Per output buffer, so no buffer's size decides the scratch allocation. */
    private static final int COPY_CHUNK = 16 * 1024;

    /*
     * How far into a file the MP3 frame scan will look.  A frame header comes
     * after an ID3v2 tag and never far into one; fifty album covers of scanning
     * to find nothing would be slower than the refusal it leads to.
     */
    private static final int MPEG_SCAN_LIMIT = 64 * 1024;

    /* See logOnce(). */
    private static final int LOG_ONCE_MAX = 8;
    private static final Set<String> LOGGED = new HashSet<String>();

    /* The three decoders this file knows how to reach. */
    private static final String MIME_AMR_NB = "audio/3gpp";
    private static final String MIME_AMR_WB = "audio/amrwb";
    private static final String MIME_MPEG = "audio/mpeg";

    /* "#!AMR\n", "#!AMR-WB\n", "ID3". */
    private static final byte[] MAGIC_AMR_NB = {
            0x23, 0x21, 0x41, 0x4D, 0x52, 0x0A};
    private static final byte[] MAGIC_AMR_WB = {
            0x23, 0x21, 0x41, 0x4D, 0x52, 0x2D, 0x57, 0x42, 0x0A};
    private static final byte[] MAGIC_ID3 = {0x49, 0x44, 0x33};

    /*
     * What one AMR frame's payload costs, indexed by the ToC's frame type
     * (RFC 4867 Table 1a for narrowband, Table 1b for wideband).  Type 8 is the
     * comfort-noise SID frame, types 9..14 are reserved, and 15 says the frame
     * is missing - a byte with no payload at all, which is why its entry is 0
     * and not "invalid".
     */
    private static final int[] AMR_NB_FRAME_BYTES = {
            12, 13, 15, 17, 19, 20, 26, 31, 32, 5, 0, 0, 0, 0, 0, 0};
    private static final int[] AMR_WB_FRAME_BYTES = {
            17, 23, 32, 36, 40, 46, 50, 58, 60, 5, 0, 0, 0, 0, 0, 0};
    private static final int AMR_SID = 8;
    private static final int AMR_NO_DATA = 15;

    /*
     * Every AMR frame is 20 ms - that is the codec, not the file - at 8 kHz
     * mono narrowband or 16 kHz mono wideband, and the runtime's PCM is 16-bit.
     */
    private static final int AMR_NB_RATE = 8000;
    private static final int AMR_WB_RATE = 16000;
    private static final int AMR_NB_SAMPLES = 160;
    private static final int AMR_WB_SAMPLES = 320;
    private static final long AMR_FRAME_US = 20000L;

    /* The MPEG audio tables the frame header indexes into (Layer III). */
    private static final int[] MPEG_RATES_V1 = {44100, 48000, 32000};
    private static final int[] MPEG_RATES_V2 = {22050, 24000, 16000};
    private static final int[] MPEG_RATES_V25 = {11025, 12000, 8000};
    private static final int[] MPEG_BITRATES_V1_L3 = {
            0, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320};
    private static final int[] MPEG_BITRATES_V2_L3 = {
            0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160};

    /*
     * The sniff's answers.  A "kind" rather than a mime because two of them
     * (MP3 files with and without an ID3 tag) share a mime and a decode path.
     */
    private static final int KIND_UNKNOWN = 0;
    private static final int KIND_AMR_NB = 1;
    private static final int KIND_AMR_WB = 2;
    private static final int KIND_MPEG = 3;

    /*
     * MediaFormat's key for the PCM encoding.  Spelled out rather than taken
     * from MediaFormat.KEY_PCM_ENCODING because that constant is API 24 and
     * this port's floor is 21.
     */
    private static final String KEY_PCM_ENCODING = "pcm-encoding";

    private PlatformAudio() {
    }

    /*
     * Called once from the Activity, before the VM can ask for audio.  The
     * context has nothing to do any more - the decoder takes the bytes
     * directly and never writes a temp file - but the hook stays: the Activity
     * calls it, and an app that binds this port calls it whether or not this
     * class still has a use for it.
     */
    static void init(Context context) {
        /* nothing to bind */
    }

    /**
     * One clip's shape: {@code {sampleRate, channelCount, frameCount,
     * pcmBytes}}, or null when the platform has no decoder for this file
     * or it is longer than the cap.
     *
     * <p>{@code pcmBytes} is exactly what {@link #decode} will return - the
     * C side allocates that many bytes and then checks the decode against it.
     */
    static synchronized int[] info(byte[] data) {
        if (data == null || data.length == 0) {
            return null;
        }
        int kind = sniff(data);
        if (kind == KIND_AMR_NB || kind == KIND_AMR_WB) {
            /* Counted, not decoded: see the header. */
            return amrShape(data, kind);
        }
        /* MP3 (and anything else with a decoder): the decode is the answer,
         * and the cache makes the decode() that follows free. */
        Clip clip = clip(data);
        if (clip == null) {
            return null;
        }
        return new int[]{clip.sampleRate, clip.channelCount, clip.frameCount,
                clip.pcm.length};
    }

    /**
     * The whole clip as signed 16-bit little-endian PCM, interleaved by frame,
     * or null.  The array is the cached one, which nothing here writes to
     * after the cache put; the caller copies what it needs across JNI.
     */
    static synchronized byte[] decode(byte[] data) {
        Clip clip = clip(data);
        return clip == null ? null : clip.pcm;
    }

    /*
     * Decode, or answer from the cache.  This is the single place a clip is
     * produced, so info() and decode() on the same bytes always agree: the
     * second call reads what the first one stored.
     */
    private static Clip clip(byte[] data) {
        if (data == null || data.length == 0) {
            return null;
        }
        long key = crc32(data);
        Clip hit = CLIPS.get(Long.valueOf(key));
        if (hit != null && hit.sourceLength == data.length) {
            return hit;
        }
        int kind = sniff(data);
        if (kind == KIND_UNKNOWN) {
            logOnce("unknown", "no platform decoder for these " + data.length
                    + " bytes: the port reads AMR and MP3 streams, and the"
                    + " runtime reads its own sample formats");
            return null;
        }
        try {
            Clip made = decodeAll(data, kind);
            if (made != null) {
                CLIPS.put(Long.valueOf(key), made);
            }
            return made;
        } catch (Throwable t) {
            /*
             * Every ordinary refusal has already returned null with its own
             * explanation; this is the platform surprising us - no decoder for
             * the mime, a codec that throws from configure(), a service that is
             * not there.  The key includes the exception class so the first few
             * distinct failures are visible and a game that retries every frame
             * is not.
             */
            logOnce("decode:" + t.getClass().getName(),
                    "the platform cannot decode these " + data.length + " bytes ("
                            + t + ")");
            return null;
        }
    }

    /*
     * The decode proper, one branch per kind.  Both return a Clip whose
     * pcm.length is exactly frameCount*bytesPerFrame - the promise the C side
     * sizes its buffer from - or null.
     */
    private static Clip decodeAll(byte[] data, int kind) throws Exception {
        if (kind == KIND_AMR_NB || kind == KIND_AMR_WB) {
            return decodeAmr(data, kind);
        }
        return decodeMpeg(data);
    }

    /*
     * AMR, decided by the frame walk alone.  The walk says how many frames
     * there are and the C side has already allocated 20 ms of PCM for each, so
     * the decoder's byte count is trimmed or zero-padded to that length: the
     * audio is the decoder's, the length is the file's, and the log says when
     * the two disagreed.
     */
    private static Clip decodeAmr(byte[] data, int kind) throws Exception {
        boolean wide = kind == KIND_AMR_WB;
        int frames = amrFrames(data, kind);
        if (frames == 0) {
            logOnce("amr-frames", "the AMR magic is there but not one whole"
                    + " frame follows it");
            return null;
        }
        int rate = wide ? AMR_WB_RATE : AMR_NB_RATE;
        int bytesPerFrame = (wide ? AMR_WB_SAMPLES : AMR_NB_SAMPLES) * 2;
        long want = (long) frames * bytesPerFrame;
        if (want > PCM_CAP) {
            logOnce("length", "refusing " + frames + " AMR frames: the port"
                    + " caps decoded audio at " + PCM_CAP + " bytes");
            return null;
        }
        String mime = wide ? MIME_AMR_WB : MIME_AMR_NB;
        Input source = new AmrInput(data, wide ? MAGIC_AMR_WB.length
                : MAGIC_AMR_NB.length, wide ? AMR_WB_FRAME_BYTES
                : AMR_NB_FRAME_BYTES, frames);
        Decoded got = decodeLoop(mime, rate, 1, source);
        if (got == null) {
            return null;
        }
        if (got.sampleRate > 0 && got.sampleRate != rate) {
            /* The PCM is at the decoder's rate while info() promised the
             * file's, and a Player that plays it at the wrong pitch is worse
             * than a refusal - the runtime still has its own AMR decoder. */
            logOnce("amr-rate", "the " + mime + " decoder reports "
                    + got.sampleRate + " Hz where the file's format says "
                    + rate);
            return null;
        }
        byte[] pcm = got.pcm;
        if (pcm.length != (int) want) {
            logOnce("amr-length", "the AMR decoder returned " + pcm.length
                    + " bytes for " + frames + " frames, not " + want
                    + "; the clip keeps the frame walk's length");
            pcm = Arrays.copyOf(pcm, (int) want);
        }
        return new Clip(data.length, rate, 1, frames, bytesPerFrame, pcm);
    }

    /*
     * MP3: strip the ID3v2 tag, read the first frame header for the shape the
     * decoder is configured with, and let the decoder find the frames in the
     * stream.  The clip's shape is the decoder's own report - falling back to
     * the header's word only if it never reports one - because that is the only
     * answer that cannot be wrong about a VBR file.
     */
    private static Clip decodeMpeg(byte[] data) throws Exception {
        int start = skipId3(data);
        MpegHeader head = findMpegHeader(data, start);
        int rate = head != null ? head.sampleRate : 44100;
        int channels = head != null ? head.channels : 2;
        int bitrate = head != null && head.bitrate > 0 ? head.bitrate : 128000;
        Decoded got = decodeLoop(MIME_MPEG, rate, channels,
                new MpegInput(data, start, bitrate));
        if (got == null) {
            return null;
        }
        if (got.sampleRate <= 0 || got.channelCount <= 0) {
            got.sampleRate = rate;
            got.channelCount = channels;
        }
        int bytesPerFrame = got.channelCount * 2;
        int frames = got.pcm.length / bytesPerFrame;
        if (frames == 0) {
            logOnce("mp3-empty", "the MP3 decoder produced no PCM for these "
                    + data.length + " bytes");
            return null;
        }
        /* Drop a trailing partial frame: decode() must be exactly
         * frameCount*bytesPerFrame bytes, because that is the contract the
         * runtime sizes its buffer from. */
        byte[] pcm = Arrays.copyOf(got.pcm, frames * bytesPerFrame);
        return new Clip(data.length, got.sampleRate, got.channelCount, frames,
                bytesPerFrame, pcm);
    }

    /*
     * The synchronous MediaCodec loop, shared by every decoder here: create the
     * codec, feed it from `source`, drain what it produces, stop at
     * end-of-stream.  null is a refusal - no decoder, a stall, the cap, the
     * wall clock, a decoder that contradicts itself mid-file - and each one
     * leaves a log line behind.
     *
     * Nothing container-ish goes in: no csd, no duration, no codec config.  AMR
     * and MP3 are raw frame streams and neither decoder needs any.  The
     * timestamps are only there because MediaCodec wants a stamp per buffer.
     */
    private static Decoded decodeLoop(String mime, int sampleRate, int channels,
                                      Input source) throws Exception {
        MediaCodec codec = MediaCodec.createDecoderByType(mime);
        try {
            MediaFormat format =
                    MediaFormat.createAudioFormat(mime, sampleRate, channels);
            codec.configure(format, null, null, 0);
            codec.start();

            MediaCodec.BufferInfo buffer = new MediaCodec.BufferInfo();
            ByteArrayOutputStream pcm = new ByteArrayOutputStream(64 * 1024);
            byte[] chunk = new byte[COPY_CHUNK];
            boolean inputDone = false;
            boolean outputDone = false;
            boolean shapeSeen = false;
            int outRate = 0;
            int outChannels = 0;
            int stalled = 0;
            long deadline = System.nanoTime() + WALL_CLOCK_NANOS;

            while (!outputDone) {
                if (!inputDone) {
                    int index = codec.dequeueInputBuffer(DEQUEUE_TIMEOUT_US);
                    if (index >= 0) {
                        stalled = 0;
                        ByteBuffer in = codec.getInputBuffer(index);
                        if (in == null) {
                            logOnce("in-buffer", "the decoder returned no input"
                                    + " buffer for index " + index);
                            return null;
                        }
                        in.clear();
                        int n = source.next(in);
                        if (n < 0) {
                            codec.queueInputBuffer(index, 0, 0,
                                    source.timestampUs(),
                                    MediaCodec.BUFFER_FLAG_END_OF_STREAM);
                            inputDone = true;
                        } else {
                            codec.queueInputBuffer(index, 0, n,
                                    source.timestampUs(), 0);
                        }
                    }
                }

                int index = codec.dequeueOutputBuffer(buffer, DEQUEUE_TIMEOUT_US);
                if (index == MediaCodec.INFO_OUTPUT_FORMAT_CHANGED) {
                    stalled = 0;
                    MediaFormat got = codec.getOutputFormat();
                    if (!isPcm16(got)) {
                        logOnce("encoding", "the decoder emits PCM encoding "
                                + intOr(got, KEY_PCM_ENCODING, -1)
                                + ", not 16-bit");
                        return null;
                    }
                    int rate = intOr(got, MediaFormat.KEY_SAMPLE_RATE, 0);
                    int ch = intOr(got, MediaFormat.KEY_CHANNEL_COUNT, 0);
                    if (rate <= 0 || ch <= 0) {
                        logOnce("format", "the decoder reported " + rate
                                + " Hz and " + ch + " channels");
                        return null;
                    }
                    if (shapeSeen && (rate != outRate || ch != outChannels)) {
                        /*
                         * A decoder that changes shape mid-file leaves PCM of
                         * two different shapes on either side of the change;
                         * there is no single array that answers info().
                         */
                        logOnce("reconfig", "the decoder changed shape mid-file: "
                                + outRate + " Hz/" + outChannels + " ch -> "
                                + rate + " Hz/" + ch + " ch");
                        return null;
                    }
                    outRate = rate;
                    outChannels = ch;
                    shapeSeen = true;
                } else if (index >= 0) {
                    stalled = 0;
                    try {
                        if (buffer.size > 0) {
                            if (pcm.size() + buffer.size > PCM_CAP) {
                                logOnce("cap", "refusing more than " + PCM_CAP
                                        + " bytes of PCM: the runtime holds a"
                                        + " whole clip in memory");
                                return null;
                            }
                            ByteBuffer decoded = codec.getOutputBuffer(index);
                            if (decoded == null) {
                                logOnce("out-buffer", "the decoder returned no"
                                        + " output buffer for index " + index);
                                return null;
                            }
                            /* BufferInfo's window, not the whole buffer: the
                             * decoder is free to leave other samples in it. */
                            decoded.position(buffer.offset);
                            decoded.limit(buffer.offset + buffer.size);
                            while (decoded.hasRemaining()) {
                                int n = Math.min(decoded.remaining(), chunk.length);
                                decoded.get(chunk, 0, n);
                                pcm.write(chunk, 0, n);
                            }
                        }
                        if ((buffer.flags & MediaCodec.BUFFER_FLAG_END_OF_STREAM)
                                != 0) {
                            outputDone = true;
                        }
                    } finally {
                        /* Every path releases the buffer it was handed, so a
                         * refusal cannot starve the codec of output slots. */
                        codec.releaseOutputBuffer(index, false);
                    }
                } else if (index != MediaCodec.INFO_OUTPUT_BUFFERS_CHANGED) {
                    /*
                     * INFO_TRY_AGAIN_LATER, or any other negative code - which
                     * for a client that fetches each buffer by index is
                     * "nothing to do" anyway.  INFO_OUTPUT_BUFFERS_CHANGED is
                     * the deprecated buffer-array notice and is skipped
                     * deliberately: it is not progress and not a stall.
                     */
                    if (++stalled >= STALL_POLLS) {
                        logOnce("stall", "the decoder stopped making progress after "
                                + pcm.size() + " bytes");
                        return null;
                    }
                }

                if (!outputDone && System.nanoTime() - deadline >= 0L) {
                    logOnce("slow", "the decoder was still working after "
                            + (WALL_CLOCK_NANOS / 1000000000L) + " s ("
                            + pcm.size() + " bytes so far)");
                    return null;
                }
            }

            Decoded out = new Decoded();
            out.sampleRate = shapeSeen ? outRate : 0;
            out.channelCount = shapeSeen ? outChannels : 0;
            out.pcm = pcm.toByteArray();
            return out;
        } finally {
            /*
             * Release in the order the platform wants them, and never let a
             * teardown failure replace the result or the exception that is
             * already on its way out.
             */
            try {
                codec.stop();
            } catch (Throwable ignored) {
                /* already stopped, or never started */
            }
            try {
                codec.release();
            } catch (Throwable ignored) {
                /* nothing left to release */
            }
        }
    }

    /*
     * Where the decoder's input comes from.  This is the one difference between
     * the formats that the loop has to know about: AMR hands over one whole
     * frame per buffer, which is what its decoder expects, and MP3 a run of
     * bytes, because its decoder parses the frames itself.
     */
    private interface Input {

        /* The next piece, written into `into`; -1 when the stream is done. */
        int next(ByteBuffer into);

        /* The timestamp of the piece next() just answered with, microseconds. */
        long timestampUs();
    }

    /*
     * AMR frames, in order, starting after the magic.  It feeds exactly the
     * frames the walk counted - not one more and not one less - so the shape
     * info() promised is the shape the decoder is asked for.
     */
    private static final class AmrInput implements Input {

        private final byte[] data;
        private final int[] sizes;
        private final int frames;
        private int off;
        private int fed;
        private long nextUs;
        private long lastUs;

        AmrInput(byte[] data, int off, int[] sizes, int frames) {
            this.data = data;
            this.off = off;
            this.sizes = sizes;
            this.frames = frames;
        }

        public int next(ByteBuffer into) {
            if (fed >= frames) {
                lastUs = nextUs;
                return -1;
            }
            int span = 1 + sizes[(data[off] >> 3) & 0x0F];
            if (span > into.remaining()) {
                /*
                 * A decoder's input buffer is kilobytes and the largest AMR
                 * frame is 61 bytes, so this cannot happen; the guard is here
                 * so that a decoder handing over a smaller one ends the stream
                 * instead of copying past the end of its buffer.
                 */
                logOnce("amr-buffer", "the decoder's input buffer is smaller"
                        + " than one AMR frame");
                lastUs = nextUs;
                return -1;
            }
            into.put(data, off, span);
            off += span;
            fed++;
            lastUs = nextUs;
            nextUs += AMR_FRAME_US;
            return span;
        }

        public long timestampUs() {
            return lastUs;
        }
    }

    /*
     * MP3 bytes, from after the ID3v2 tag to the end of the file, in whatever
     * pieces the decoder's input buffers take.  Frame boundaries are not
     * respected on purpose: the decoder is a stream parser, and splitting on
     * boundaries it does not need would only add a second parser to get wrong.
     */
    private static final class MpegInput implements Input {

        private final byte[] data;
        private final int bitrate;
        private int off;
        private long nextUs;
        private long lastUs;

        MpegInput(byte[] data, int off, int bitrate) {
            this.data = data;
            this.off = off;
            this.bitrate = bitrate > 0 ? bitrate : 128000;
        }

        public int next(ByteBuffer into) {
            lastUs = nextUs;
            if (off >= data.length) {
                return -1;
            }
            int n = Math.min(into.remaining(), data.length - off);
            if (n <= 0) {
                return -1;
            }
            into.put(data, off, n);
            off += n;
            /*
             * The stamp is the bytes' own duration at the first frame's
             * bitrate.  Nothing reads it back out; it exists because MediaCodec
             * wants a stamp per buffer, and a plausible one costs less than a
             * warning per buffer.  A VBR file drifts, and that is fine - what
             * comes out is PCM, not timing.
             */
            nextUs += (long) n * 8000000L / (long) bitrate;
            return n;
        }

        public long timestampUs() {
            return lastUs;
        }
    }

    /*
     * The AMR answer without a codec: walk the frame headers and count them.
     * 20 ms per frame, mono, 16-bit, so the PCM length is exact.
     */
    private static int[] amrShape(byte[] data, int kind) {
        int frames = amrFrames(data, kind);
        if (frames == 0) {
            logOnce("amr-frames", "the AMR magic is there but not one whole"
                    + " frame follows it");
            return null;
        }
        boolean wide = kind == KIND_AMR_WB;
        long bytes = (long) frames * (wide ? AMR_WB_SAMPLES : AMR_NB_SAMPLES) * 2L;
        if (bytes > PCM_CAP) {
            logOnce("length", "refusing " + frames + " AMR frames: the port"
                    + " caps decoded audio at " + PCM_CAP + " bytes");
            return null;
        }
        return new int[]{wide ? AMR_WB_RATE : AMR_NB_RATE, 1, frames, (int) bytes};
    }

    /*
     * The AMR storage format's frame walk (RFC 4867 4.3): after the magic, one
     * ToC byte and its payload per frame, and the ToC's frame type says how long
     * that payload is.  Frame types 9..14 are reserved - the stream is not a
     * stream any more - while 15 is legal and means "this frame is missing" at
     * one byte.  A truncated frame ends the walk rather than being counted: half
     * a frame decodes to noise, and the count here is what the PCM length is
     * computed from.  src/midp/nj_amr.c walks the file the same way.
     */
    private static int amrFrames(byte[] data, int kind) {
        boolean wide = kind == KIND_AMR_WB;
        int[] sizes = wide ? AMR_WB_FRAME_BYTES : AMR_NB_FRAME_BYTES;
        int off = wide ? MAGIC_AMR_WB.length : MAGIC_AMR_NB.length;
        int frames = 0;
        while (off < data.length) {
            int type = (data[off] >> 3) & 0x0F;
            if (type > AMR_SID && type != AMR_NO_DATA) {
                break;
            }
            int span = 1 + sizes[type];
            if (span > data.length - off) {
                break;
            }
            off += span;
            frames++;
        }
        return frames;
    }

    /*
     * What is this file?  Three magics and a frame header - nothing here is a
     * container parser, because the only files this class claims are the ones
     * whose frames it can find, and a file it does not claim is refused for the
     * runtime to deal with.
     */
    private static int sniff(byte[] data) {
        if (startsWith(data, MAGIC_AMR_WB)) {
            return KIND_AMR_WB;
        }
        if (startsWith(data, MAGIC_AMR_NB)) {
            return KIND_AMR_NB;
        }
        if (startsWith(data, MAGIC_ID3) || findMpegHeader(data, 0) != null) {
            return KIND_MPEG;
        }
        return KIND_UNKNOWN;
    }

    /*
     * An ID3v2 tag is ten bytes of header and a syncsafe length, plus a footer
     * when the flags say so.  It is not audio and the decoder has no reason to
     * see it, so feeding starts after it.  A tag whose length does not make
     * sense is left in place: the decoder resynchronises on the first frame,
     * which is what it does for a living.
     */
    private static int skipId3(byte[] data) {
        if (data.length < 10 || !startsWith(data, MAGIC_ID3)) {
            return 0;
        }
        long size = ((long) (data[6] & 0x7F) << 21)
                | ((long) (data[7] & 0x7F) << 14)
                | ((long) (data[8] & 0x7F) << 7)
                | ((long) (data[9] & 0x7F));
        long end = 10L + size + (((data[5] & 0xFF) & 0x10) != 0 ? 10L : 0L);
        if (end <= 10L || end > data.length) {
            return 0;
        }
        return (int) end;
    }

    /*
     * The first valid MPEG audio frame header at or after `from`.  A bare 0xFF
     * with its top bits set turns up inside tags and album art all the time, so
     * the header is only believed when every field in it parses - see
     * MpegHeader.parse().
     */
    private static MpegHeader findMpegHeader(byte[] data, int from) {
        int begin = Math.max(0, from);
        int end = Math.min(data.length - 3, begin + MPEG_SCAN_LIMIT);
        for (int i = begin; i < end; i++) {
            if ((data[i] & 0xFF) != 0xFF || (data[i + 1] & 0xE0) != 0xE0) {
                continue;
            }
            MpegHeader head = MpegHeader.parse(data, i);
            if (head != null) {
                return head;
            }
        }
        return null;
    }

    /*
     * One MPEG audio frame header (ISO/IEC 11172-3 and 13818-3): eleven sync
     * bits, the version and layer, then the bitrate, sample rate and channel
     * mode.  Only the Layer III bitrate tables live here - MP3 is what
     * "audio/mpeg" decodes - and a Layer I/II header keeps a zero bitrate,
     * which costs nothing but a rougher timestamp.
     */
    private static final class MpegHeader {

        final int sampleRate;
        final int channels;
        final int bitrate;

        private MpegHeader(int sampleRate, int channels, int bitrate) {
            this.sampleRate = sampleRate;
            this.channels = channels;
            this.bitrate = bitrate;
        }

        static MpegHeader parse(byte[] data, int off) {
            int b1 = data[off + 1] & 0xFF;
            int b2 = data[off + 2] & 0xFF;
            int b3 = data[off + 3] & 0xFF;
            /* 0 = MPEG-2.5, 2 = MPEG-2, 3 = MPEG-1, 1 = reserved. */
            int version = (b1 >> 3) & 0x03;
            /* 1 = Layer III, 2 = Layer II, 3 = Layer I, 0 = reserved. */
            int layer = (b1 >> 1) & 0x03;
            int bitrateIndex = (b2 >> 4) & 0x0F;
            int rateIndex = (b2 >> 2) & 0x03;
            if (version == 1 || layer == 0 || bitrateIndex == 0
                    || bitrateIndex == 15 || rateIndex == 3) {
                return null;
            }
            int[] rates = version == 3 ? MPEG_RATES_V1
                    : version == 2 ? MPEG_RATES_V2 : MPEG_RATES_V25;
            int bitrate = 0;
            if (layer == 1) {
                bitrate = (version == 3 ? MPEG_BITRATES_V1_L3
                        : MPEG_BITRATES_V2_L3)[bitrateIndex] * 1000;
            }
            /* Channel mode: 3 is mono, everything else is two channels. */
            int channels = ((b3 >> 6) & 0x03) == 3 ? 1 : 2;
            return new MpegHeader(rates[rateIndex], channels, bitrate);
        }
    }

    /*
     * The one PCM encoding the runtime takes.  A format that does not name one
     * is assumed to be 16-bit: every decoder this port has met emits that, and
     * the key is documented to be present only when it is something else.
     */
    private static boolean isPcm16(MediaFormat format) {
        return intOr(format, KEY_PCM_ENCODING,
                AudioFormat.ENCODING_PCM_16BIT) == AudioFormat.ENCODING_PCM_16BIT;
    }

    /*
     * MediaFormat throws for a key that is not there and for a value of an
     * unexpected class, and a missing key is ordinary: not every decoder
     * reports a channel count, and a raw stream has no container to report
     * anything.
     */
    private static int intOr(MediaFormat format, String key, int fallback) {
        try {
            return format.containsKey(key) ? format.getInteger(key) : fallback;
        } catch (Throwable t) {
            return fallback;
        }
    }

    private static boolean startsWith(byte[] data, byte[] magic) {
        if (data.length < magic.length) {
            return false;
        }
        for (int i = 0; i < magic.length; i++) {
            if (data[i] != magic[i]) {
                return false;
            }
        }
        return true;
    }

    private static long crc32(byte[] data) {
        CRC32 crc = new CRC32();
        crc.update(data, 0, data.length);
        return crc.getValue();
    }

    /*
     * A game that asks for a file the platform cannot read asks again every
     * time a sound plays, so a failure is logged once per kind, never once per
     * call.  The set is capped because a hostile JAR must not be able to grow
     * a static field one distinct failure at a time.
     */
    private static void logOnce(String kind, String detail) {
        synchronized (LOGGED) {
            if (LOGGED.size() >= LOG_ONCE_MAX || !LOGGED.add(kind)) {
                return;
            }
        }
        Log.w(TAG, "audio: " + detail);
    }

    /*
     * One decoded clip.  Immutable, and handed to C as the same array: the
     * runtime copies what it needs, and nothing here writes to it after the
     * cache put.  pcm.length is exactly frameCount*bytesPerFrame, always.
     */
    private static final class Clip {

        final int sourceLength;
        final int sampleRate;
        final int channelCount;
        final int frameCount;
        final int bytesPerFrame;
        final byte[] pcm;

        Clip(int sourceLength, int sampleRate, int channelCount, int frameCount,
             int bytesPerFrame, byte[] pcm) {
            this.sourceLength = sourceLength;
            this.sampleRate = sampleRate;
            this.channelCount = channelCount;
            this.frameCount = frameCount;
            this.bytesPerFrame = bytesPerFrame;
            this.pcm = pcm;
        }
    }

    /*
     * What the loop got out of a decoder: the PCM, and the shape the decoder
     * reported - zero when it never said so, which only an MP3 decoder may do,
     * and then the frame header's word is the one that stands.
     */
    private static final class Decoded {

        int sampleRate;
        int channelCount;
        byte[] pcm = new byte[0];
    }
}
