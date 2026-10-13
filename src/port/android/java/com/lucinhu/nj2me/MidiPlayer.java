/*
 * nano_j2me - MidiPlayer.java
 *
 * The platform's General MIDI synthesizer, behind the port's nj_port.midi_*
 * hooks.
 *
 * WHY THIS IS NOT A SYNTHESIZER IN THIS FILE
 * ------------------------------------------
 * Android has shipped Sonivox since API 21: libsonivox.so, a General MIDI
 * engine with the standard instrument bank, reachable through MediaPlayer
 * (the media framework registers a MIDI extractor for mid/midi/xmf/imy/rtttl
 * and hands the file to that same engine).  It is the identical synthesizer
 * the Android J2ME loaders vendor as a native library - so the sound is the
 * one a J2ME player expects, and it costs this project no bytes, no samples
 * and no third-party code.
 *
 * The runtime renders MIDI itself (src/midp/nj_midi.c) and is good at it, but
 * four waveforms are not a piano.  When this class is present the port hands
 * every .mid over and the renderer becomes the fallback for whatever the
 * platform refuses.
 *
 * WHAT THE C SIDE SEES
 * --------------------
 * Eight statics, called on the VM thread: open/length/start/stop/close/seek/
 * time/volume/finished.  A handle is a long id, never a Java object, because
 * the runtime stores it in a byte array on the Java heap and C must not keep
 * a reference to anything it cannot release.  open() returning 0 means "the
 * platform will not play this", which is not an error: the runtime then
 * renders the file the way it does on the desktop.
 *
 * The file has to reach MediaPlayer as a path or a descriptor, so it is
 * written to the app cache once per playback and deleted with the handle.  A
 * .mid is a few kilobytes, so this is a file the OS never has to flush.
 */
package com.lucinhu.nj2me;

import android.content.Context;
import android.media.MediaPlayer;
import android.util.Log;

import java.io.File;
import java.io.FileOutputStream;
import java.util.HashMap;
import java.util.Map;

final class MidiPlayer {

    private static final String TAG = "nj2me";

    /*
     * Handles are ids into this map rather than raw pointers, and the map is
     * the only place a MediaPlayer lives.  It is small on purpose: a game
     * holds one or two MIDI Players at a time, and a leak here would be a
     * leak of a hardware audio track.
     */
    private static final Map<Long, MediaPlayer> PLAYERS = new HashMap<Long, MediaPlayer>();
    private static final Map<Long, File> FILES = new HashMap<Long, File>();

    /* Ids this shell paused itself, so resume only restarts its own work. */
    private static final java.util.Set<Long> PAUSED = new java.util.HashSet<Long>();

    private static Context appContext;
    private static long nextId = 1L;

    private MidiPlayer() {
    }

    /* Called once from the Activity: a MediaPlayer needs somewhere to write. */
    static void init(Context context) {
        if (context != null) {
            appContext = context.getApplicationContext();
        }
    }

    /*
     * Prepare a Standard MIDI File for playback.  Returns 0 when the platform
     * will not take it - an unsupported variant, no synthesizer, a broken
     * file - and the runtime falls back to its own renderer.
     */
    static synchronized long open(byte[] data) {
        if (appContext == null || data == null || data.length == 0) {
            return 0L;
        }
        long id = nextId++;
        File file = new File(appContext.getCacheDir(), "midi-" + id + ".mid");
        MediaPlayer mp = null;
        try {
            FileOutputStream out = new FileOutputStream(file);
            try {
                out.write(data);
            } finally {
                out.close();
            }
            mp = new MediaPlayer();
            mp.setDataSource(file.getAbsolutePath());
            mp.prepare();
        } catch (Throwable t) {
            /*
             * Every failure here is ordinary: a device without the MIDI
             * extractor, a file the parser rejects, a codec that cannot be
             * opened.  Logged at debug, not warned, because the fallback is
             * the documented answer rather than a defect.
             */
            Log.d(TAG, "midi: the platform declined this file (" + t + ")");
            if (mp != null) {
                try {
                    mp.release();
                } catch (Throwable ignored) {
                    /* releasing twice is not worth a crash */
                }
            }
            //noinspection ResultOfMethodCallIgnored
            file.delete();
            return 0L;
        }
        PLAYERS.put(id, mp);
        FILES.put(id, file);
        return id;
    }

    /* Duration in milliseconds, or 0 when the platform does not know. */
    static synchronized int length(long id) {
        MediaPlayer mp = PLAYERS.get(id);
        if (mp == null) {
            return 0;
        }
        try {
            return mp.getDuration();
        } catch (Throwable t) {
            return 0;
        }
    }

    static synchronized boolean start(long id, boolean loop) {
        MediaPlayer mp = PLAYERS.get(id);
        if (mp == null) {
            return false;
        }
        try {
            mp.setLooping(loop);
            mp.start();
            return true;
        } catch (Throwable t) {
            Log.d(TAG, "midi: start failed (" + t + ")");
            return false;
        }
    }

    /*
     * JSR 135 stop() keeps the media time: the Player can be started again and
     * resumes where it was.  pause() is exactly that; seekTo(0) would be a
     * rewind a title did not ask for.
     */
    static synchronized void stop(long id) {
        MediaPlayer mp = PLAYERS.get(id);
        if (mp == null) {
            return;
        }
        try {
            mp.pause();
        } catch (Throwable t) {
            Log.d(TAG, "midi: stop failed (" + t + ")");
        }
    }

    /*
     * The game is over, but a MIDlet does not always get that far: it can be
     * torn down with its Player still started, and a MediaPlayer outlives the
     * VM, the port and the Activity.  That is the "the music keeps playing
     * after I leave the game" complaint, so the shell closes the books itself
     * instead of trusting deallocate() to have run.
     */
    static synchronized void closeAll() {
        for (Long id : new java.util.ArrayList<Long>(PLAYERS.keySet())) {
            close(id);
        }
        PLAYERS.clear();
        FILES.clear();
        PAUSED.clear();
    }

    /*
     * Leaving the screen is not the end of the game: pause what is playing and
     * remember it, so the same players come back when the game does.  A player
     * the MIDlet itself stopped is not playing, so it stays stopped - the shell
     * only undoes what it did.
     */
    static synchronized void pauseAll() {
        PAUSED.clear();
        for (Map.Entry<Long, MediaPlayer> e : PLAYERS.entrySet()) {
            MediaPlayer mp = e.getValue();
            try {
                if (mp.isPlaying()) {
                    mp.pause();
                    PAUSED.add(e.getKey());
                }
            } catch (Throwable ignored) {
                /* a player the platform already let go of */
            }
        }
    }

    static synchronized void resumeAll() {
        for (Long id : PAUSED) {
            MediaPlayer mp = PLAYERS.get(id);
            if (mp == null) {
                continue;
            }
            try {
                mp.start();
            } catch (Throwable t) {
                Log.d(TAG, "midi: resume failed (" + t + ")");
            }
        }
        PAUSED.clear();
    }

    static synchronized void close(long id) {
        MediaPlayer mp = PLAYERS.remove(id);
        File file = FILES.remove(id);
        if (mp != null) {
            try {
                mp.stop();
            } catch (Throwable ignored) {
                /* already stopped or never started */
            }
            try {
                mp.release();
            } catch (Throwable ignored) {
                /* nothing left to release */
            }
        }
        if (file != null) {
            //noinspection ResultOfMethodCallIgnored
            file.delete();
        }
    }

    static synchronized void seek(long id, int ms) {
        MediaPlayer mp = PLAYERS.get(id);
        if (mp == null) {
            return;
        }
        try {
            mp.seekTo(ms < 0 ? 0 : ms);
        } catch (Throwable t) {
            Log.d(TAG, "midi: seek failed (" + t + ")");
        }
    }

    static synchronized int time(long id) {
        MediaPlayer mp = PLAYERS.get(id);
        if (mp == null) {
            return 0;
        }
        try {
            return mp.getCurrentPosition();
        } catch (Throwable t) {
            return 0;
        }
    }

    /* JSR 135 levels are 0..100; MediaPlayer takes 0.0..1.0 per channel. */
    static synchronized void volume(long id, int level) {
        MediaPlayer mp = PLAYERS.get(id);
        if (mp == null) {
            return;
        }
        float v = level <= 0 ? 0.0f : (level >= 100 ? 1.0f : level / 100.0f);
        try {
            mp.setVolume(v, v);
        } catch (Throwable t) {
            Log.d(TAG, "midi: volume failed (" + t + ")");
        }
    }

    /*
     * "Has it played out?" - which is what END_OF_MEDIA is fired from.  The
     * runtime only asks while the Player is started, so a stopped player
     * (paused by stop(), which unregisters it) is never misread as finished.
     * A player that failed to start is also "not playing", and that is the
     * honest answer: there is no music coming.
     */
    static synchronized boolean finished(long id) {
        MediaPlayer mp = PLAYERS.get(id);
        if (mp == null) {
            return true;
        }
        try {
            return !mp.isPlaying();
        } catch (Throwable t) {
            return true;
        }
    }
}
