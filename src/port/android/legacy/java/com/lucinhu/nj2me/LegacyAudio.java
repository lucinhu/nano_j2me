package com.lucinhu.nj2me;

import android.media.AudioFormat;
import android.media.AudioManager;
import android.media.AudioTrack;
import android.util.Log;

/** API 3 streaming audio. Its thread must end before nativeDestroy frees the mixer. */
final class LegacyAudio implements Runnable {
    private static final int RATE = 44100;
    private static final int FRAMES = 1024;
    private final long handle;
    private final Object wake = new Object();
    // Never hold this across write(): stop() must be able to unblock a full stream.
    private final Object control = new Object();
    private boolean released;
    private boolean stopped;
    private final AudioTrack track;
    private final Thread thread;
    private volatile boolean running = true;
    private volatile boolean active;

    LegacyAudio(long owner) {
        handle = owner;
        int minimum = AudioTrack.getMinBufferSize(RATE,
                AudioFormat.CHANNEL_CONFIGURATION_STEREO, AudioFormat.ENCODING_PCM_16BIT);
        if (minimum <= 0) { throw new IllegalStateException("44100Hz stereo is unsupported"); }
        track = new AudioTrack(AudioManager.STREAM_MUSIC, RATE,
                AudioFormat.CHANNEL_CONFIGURATION_STEREO, AudioFormat.ENCODING_PCM_16BIT,
                Math.max(minimum, FRAMES * 4 * 2), AudioTrack.MODE_STREAM);
        if (track.getState() != AudioTrack.STATE_INITIALIZED) {
            track.release();
            throw new IllegalStateException("AudioTrack initialization failed");
        }
        thread = new Thread(this, "nj2me-audio");
        thread.start();
    }

    void setActive(boolean enabled) {
        synchronized (control) {
            if (!running || released) { return; }
            active = enabled;
            try {
                if (enabled) { track.play(); }
                else { track.pause(); track.flush(); }
            } catch (RuntimeException e) {
                Log.w("nj2me", "AudioTrack pause/play failed", e);
            }
        }
        synchronized (wake) { wake.notifyAll(); }
    }

    /** Safe from the UI thread: never joins or waits for the VM. */
    void requestStop() {
        synchronized (control) {
            if (!running || released) { return; }
            running = false;
            active = false;
            try { track.pause(); track.flush(); track.stop(); }
            catch (RuntimeException ignored) { }
            stopped = true;
        }
        synchronized (wake) { wake.notifyAll(); }
    }

    void awaitStop() {
        boolean interrupted = false;
        for (;;) {
            try { thread.join(); break; }
            catch (InterruptedException e) { interrupted = true; }
        }
        if (interrupted) { Thread.currentThread().interrupt(); }
    }

    public void run() {
        short[] samples = new short[FRAMES * 2];
        try {
            while (running) {
                synchronized (wake) {
                    while (running && !active) { wake.wait(); }
                }
                if (!running) { break; }
                int frames = GameActivity.nativeRenderAudio(handle, samples, FRAMES);
                if (frames <= 0 || frames > FRAMES) { throw new IllegalStateException("Invalid PCM frame count"); }
                int offset = 0;
                int count = frames * 2;
                while (running && active && offset < count) {
                    int written = track.write(samples, offset, count - offset);
                    if (written <= 0) {
                        if (!running || !active) { break; }
                        throw new IllegalStateException("AudioTrack.write: " + written);
                    }
                    offset += written;
                }
            }
        } catch (InterruptedException ignored) {
            Thread.currentThread().interrupt();
        } catch (RuntimeException e) {
            Log.w("nj2me", "PCM output stopped", e);
        } catch (LinkageError e) {
            Log.w("nj2me", "Legacy PCM JNI is unavailable", e);
        } finally {
            synchronized (control) {
                running = false;
                active = false;
                if (!released) {
                    if (!stopped) { try { track.stop(); } catch (RuntimeException ignored) { } }
                    released = true;
                    track.release();
                }
            }
        }
    }
}
