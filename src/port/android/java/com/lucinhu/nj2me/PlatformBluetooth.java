/*
 * nano_j2me - PlatformBluetooth.java
 *
 * The platform's Bluetooth radio, behind the port's javax.bluetooth (JSR 82)
 * hooks.  It answers only the questions text can carry: who this adapter is,
 * which devices an inquiry has heard, and which services SDP has found on one
 * of them.  The shape is PlatformSensor's - static methods, a handle-free
 * text protocol, and a value for every answer - because the C side stores
 * these strings, not Java objects, and would otherwise have to hold references
 * to a heap it does not manage.
 *
 * WHY A BROADCAST RECEIVER AND NOT A CALL
 * ---------------------------------------
 * Android has no "tell me what has been found": discovery is a broadcast, and
 * ACTION_FOUND arrives whenever the controller hears an inquiry response.  So
 * init() registers one receiver, the callbacks fill a map keyed by address,
 * and inquiryDevices() is a snapshot of that map.  A device is put in a
 * LinkedHashMap so an inquiry's results come out in the order the radio heard
 * them and a device the radio heard twice is still listed once - the runtime
 * walks the list, and a list that reorders itself between two calls would
 * report a device twice.
 *
 * WHAT THIS FILE DELIBERATELY DOES NOT DO
 * ---------------------------------------
 * It opens no sockets: no BluetoothSocket, no BluetoothServerSocket, no
 * L2CAP channel.  The runtime's connection half is a separate set of port
 * hooks, and a bridge that cannot keep a peer on the other end of a JNI
 * handle has no business handing one over.  The urls services() produces are
 * complete and valid, but this class never dials them.  Nor does it wait for
 * anything: setDiscoverable() reports the mode it asked for, not the mode the
 * device ended up in - on Android that answer needs the user to accept a
 * system dialog, and JSR 82's setDiscoverable() has no completion event to
 * hang that on.
 *
 * WHY IT NEVER THROWS
 * -------------------
 * SecurityException is the ordinary case here, not the rare one: on API 31+
 * a missing BLUETOOTH_CONNECT makes nearly every call below throw.  A
 * throwable crossing JNI is an abort, so every entry point catches Throwable
 * and returns the neutral value (0, -1, "", "000000000000", an empty array),
 * which the runtime reads as "no such device" - exactly what a phone that
 * cannot use its radio should say.
 *
 * The application context is kept, never the Activity: this class holds it in
 * a static field, and a static field holding an Activity pins that Activity,
 * and its whole view tree, for the life of the process.
 *
 * Threading: the receiver runs on the main thread while a MIDlet reads from
 * the VM thread, so the maps are guarded by one lock.  No Bluetooth call is
 * made while holding it - each entry point copies what it needs out of the
 * map, or does its binder work outside the lock - so a slow radio cannot
 * stall the broadcast callback that would fill it.
 */
package com.lucinhu.nj2me;

import android.bluetooth.BluetoothAdapter;
import android.bluetooth.BluetoothClass;
import android.bluetooth.BluetoothDevice;
import android.bluetooth.BluetoothManager;
import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.content.pm.PackageManager;
import android.os.Build;
import android.os.ParcelUuid;
import android.util.Log;

import java.util.ArrayList;
import java.util.Collections;
import java.util.HashMap;
import java.util.HashSet;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Locale;
import java.util.Map;
import java.util.Set;
import java.util.UUID;

final class PlatformBluetooth {

    private static final String TAG = "nj2me";

    /*
     * The two JSR 82 inquiry access codes this bridge understands.  GIAC is
     * "any device may answer" - what LocalDevice.setDiscoverable() passes -
     * and the LIAC base is "do not answer", which is how a midlet turns
     * discoverability back off.
     */
    private static final int IAC_GIAC = 0x9E8B33;
    private static final int IAC_NOT_DISCOVERABLE = 0x9E8B00;

    /*
     * BluetoothAdapter.SCAN_MODE_* spelled out.  The constants are public -
     * getScanMode() and its values have been in the SDK since API 5 - but the
     * only setter, setScanMode(), is marked @hide, so the numbers live here
     * for the reflective call that has to use them.
     */
    private static final int SCAN_MODE_NONE = 20;
    private static final int SCAN_MODE_CONNECTABLE = 21;
    private static final int SCAN_MODE_CONNECTABLE_DISCOVERABLE = 23;

    /* The SDP base UUID every 16-bit service UUID is an alias of, as a full
     * string: a MIDlet's 16-bit UUID and the 128-bit one Android prints for
     * the same service have to compare equal, and that comparison happens on
     * this spelling. */
    private static final String BASE_UUID_STR =
            "00001000-0000-1000-8000-00805f9b34fb";

    /*
     * deviceClass(decimal) is an unsigned 32-bit word, while Java's int is
     * signed: a class the platform reports with its top bit set has to be
     * widened here or the C side reads it as negative.
     */
    private static final long CLASS_MASK = 0xFFFFFFFFL;

    /*
     * The well-known service UUIDs JSR 82 names, keyed by the canonical
     * string UUID.toString() prints.  The 16-bit UUIDs sit in the SDP base
     * UUID that bluetooth.jsr82's own tables use.
     */
    private static final Map<String, String> NAMES = new HashMap<String, String>();
    static {
        NAMES.put("00001101-0000-1000-8000-00805f9b34fb", "Serial Port");
        NAMES.put("00001105-0000-1000-8000-00805f9b34fb", "OBEX Object Push");
        NAMES.put("00001116-0000-1000-8000-00805f9b34fb", "OBEX File Transfer");
        NAMES.put("00000003-0000-1000-8000-00805f9b34fb", "RFCOMM");
        NAMES.put("00000100-0000-1000-8000-00805f9b34fb", "L2CAP");
        NAMES.put("00001108-0000-1000-8000-00805f9b34fb", "Headset");
        NAMES.put("0000110a-0000-1000-8000-00805f9b34fb", "Audio Source");
        NAMES.put("00001112-0000-1000-8000-00805f9b34fb", "Advanced Audio");
        NAMES.put("0000110c-0000-1000-8000-00805f9b34fb", "A/V Remote Control");
    }

    /* The one address that means "unknown", in the twelve-digit spelling the
     * C side parses. */
    private static final String UNKNOWN_ADDRESS = "000000000000";

    /* See logOnce(). */
    private static final int LOG_ONCE_MAX = 8;
    private static final Set<String> LOGGED = new HashSet<String>();

    /*
     * Handle-free state.  DEVICES is ordered because inquiryDevices() reports
     * discovery order, and it is the only map: a device's service records
     * live in its own Entry, so the address that keys the device is also the
     * key to its UUIDs - there is no second table that could disagree with
     * the first about which device a service belongs to.
     */
    private static final Object LOCK = new Object();
    private static final Map<String, Entry> DEVICES = new LinkedHashMap<String, Entry>();

    /*
     * Application context only.  A static field holding an Activity would keep
     * the Activity alive for the life of the process; the adapter is a binder
     * client and is fetched once, because it never goes stale.
     */
    private static Context appContext;
    private static BluetoothAdapter adapter;

    /*
     * Whether ACTION_DISCOVERY_FINISHED is still pending.  inquiryStart()
     * leaves it true, inquiryStop() clears it, and the callback clears it -
     * the flag exists so that a second inquiry is not refused after one that
     * ran out on its own.
     */
    private static boolean discovering;

    /* Registered once.  A second init() must not register a second receiver,
     * which would double every discovered device in the log and in DEVICES. */
    private static boolean receiverRegistered;

    private PlatformBluetooth() {
    }

    /**
     * Called once from the Activity, before the VM can ask for anything.
     * Keeps the application context, finds the adapter, and registers the one
     * receiver discovery depends on; all three are then good for the life of
     * the process.
     */
    static void init(Context ctx) {
        if (ctx == null) {
            return;
        }
        try {
            Context app = ctx.getApplicationContext();
            appContext = app != null ? app : ctx;
            BluetoothManager manager = (BluetoothManager) appContext
                    .getSystemService(Context.BLUETOOTH_SERVICE);
            adapter = manager != null ? manager.getAdapter() : null;
            if (adapter == null) {
                logOnce("init-none", "this device has no Bluetooth adapter");
            }
            registerReceiver();
            /*
             * Paired devices are known to the agent without an inquiry: they
             * are in the adapter's own list.  Seeding them means a midlet can
             * start a service search on a device it has used before without
             * spending twelve seconds of inquiry first, and they still take
             * part in the one-entry-per-address rule.
             */
            seedBonded();
        } catch (Throwable t) {
            adapter = null;
            logOnce("init:" + t.getClass().getName(),
                    "the platform has no Bluetooth adapter (" + t + ")");
        }
    }

    /**
     * 1 when there is an adapter and this app may use it, else 0.
     *
     * <p>On API 31+ the radio is behind runtime permissions: without
     * BLUETOOTH_CONNECT nearly every call below throws SecurityException, and
     * without BLUETOOTH_SCAN an inquiry finds nothing.  A SecurityException
     * crossing JNI is a crash, so this is where the runtime is told to leave
     * the whole package alone instead.
     */
    static int available() {
        try {
            if (adapter == null) {
                return 0;
            }
            if (Build.VERSION.SDK_INT >= 31
                    && !granted("android.permission.BLUETOOTH_CONNECT")) {
                logOnce("perm-connect", "BLUETOOTH_CONNECT is not granted, so"
                        + " this app may not use the Bluetooth adapter");
                return 0;
            }
            if (!adapter.isEnabled()) {
                logOnce("disabled", "the Bluetooth adapter is switched off");
                return 0;
            }
            return 1;
        } catch (Throwable t) {
            logOnce("available:" + t.getClass().getName(),
                    "the Bluetooth adapter could not be read (" + t + ")");
            return 0;
        }
    }

    /**
     * This adapter's address, twelve lower-case hex digits with no colons, or
     * "000000000000" when the platform will not say.
     *
     * <p>Android 6 and later hand a constant fake address to an ordinary app;
     * that is an unknown address, and it is reported as one rather than as a
     * number the runtime could mistake for a real device.
     */
    static synchronized String address() {
        BluetoothAdapter a = adapter;
        if (a == null) {
            return UNKNOWN_ADDRESS;
        }
        try {
            return normalizeAddress(a.getAddress());
        } catch (Throwable t) {
            logOnce("address:" + t.getClass().getName(),
                    "the local Bluetooth address could not be read (" + t + ")");
            return UNKNOWN_ADDRESS;
        }
    }

    /**
     * The adapter's friendly name, or "" when the platform will not give one
     * (Android 12 and later restrict getAddress() and getName() similarly).
     */
    static synchronized String name() {
        BluetoothAdapter a = adapter;
        if (a == null) {
            return "";
        }
        try {
            String value = a.getName();
            return value == null ? "" : field(value);
        } catch (Throwable t) {
            logOnce("name:" + t.getClass().getName(),
                    "the local Bluetooth name could not be read (" + t + ")");
            return "";
        }
    }

    /**
     * The adapter's class-of-device word as an unsigned decimal int, or 0
     * when the platform has none.
     */
    static synchronized int deviceClass() {
        BluetoothAdapter a = adapter;
        if (a == null) {
            return 0;
        }
        try {
            /*
             * BluetoothAdapter.getBluetoothClass() is not in the public SDK
             * any more (it is a system API on modern releases), so it is asked
             * for by reflection.  A platform that will not answer leaves the
             * class at 0, which every caller already treats as "unknown".
             */
            java.lang.reflect.Method m =
                BluetoothAdapter.class.getMethod("getBluetoothClass");
            Object cls = m.invoke(a);
            if (cls == null) {
                return 0;
            }
            java.lang.reflect.Method g =
                cls.getClass().getMethod("getDeviceClass");
            Object v = g.invoke(cls);
            int word = v instanceof Integer ? (Integer) v : 0;
            return (int) (((long) word) & CLASS_MASK);
        } catch (Throwable t) {
            logOnce("class:" + t.getClass().getName(),
                    "the local Bluetooth class could not be read (" + t + ")");
            return 0;
        }
    }

    /**
     * Ask the platform to become discoverable, or to stop being discoverable.
     *
     * <p>{@code mode} is a JSR 82 inquiry access code: 0x9E8B33 (GIAC) means
     * "answer inquiries", 0x9E8B00 means "do not".  The answer is the mode
     * that was requested, or -1 when the request could not even be made.
     *
     * <p>It does not wait: on Android 13 and later the request raises a
     * system dialog, so the adapter is not discoverable yet when this
     * returns.  JSR 82 has no completion callback for discoverability, and
     * blocking the VM thread inside a user prompt would freeze the midlet, so
     * the runtime is told the request was accepted and nothing more.
     */
    static synchronized int setDiscoverable(int mode) {
        BluetoothAdapter a = adapter;
        int scanMode;
        if (mode == IAC_GIAC) {
            scanMode = SCAN_MODE_CONNECTABLE_DISCOVERABLE;
        } else if (mode == IAC_NOT_DISCOVERABLE) {
            scanMode = SCAN_MODE_CONNECTABLE;
        } else if (mode == 0 && a != null) {
            /* JSR 82's own "not discoverable" spelling. */
            scanMode = SCAN_MODE_NONE;
        } else {
            logOnce("discoverable-mode:" + mode,
                    "this is not an inquiry access code the port knows");
            return -1;
        }
        if (a == null) {
            return -1;
        }
        try {
            /*
             * setScanMode() is a system API on modern releases, so it is asked
             * for by reflection first.  When the platform will not hand it
             * over the public way to become discoverable is the system's own
             * dialog, which is exactly what a Bluetooth settings screen uses.
             *
             * Either way the answer is the mode that was REQUESTED: the dialog
             * is asynchronous, the controller applies a scan mode in its own
             * time, and JSR 82 has no callback for either, so waiting for a
             * settled state would mean blocking the VM thread inside a user
             * prompt.  -1 is kept for the two cases that are not a request at
             * all: no adapter, and an inquiry access code this port does not
             * understand.
             */
            try {
                java.lang.reflect.Method m =
                    BluetoothAdapter.class.getMethod("setScanMode", int.class);
                m.invoke(a, Integer.valueOf(scanMode));
                return mode;
            } catch (Throwable reflection) {
                /*
                 * No hidden method, or a hidden-API policy that refuses the
                 * call: fall back to the dialog.  It needs BLUETOOTH_ADVERTISE
                 * from Android 12, and asking without it throws
                 * SecurityException, so the grant is checked rather than
                 * caught - a refused request that reported success would be
                 * worse than a plain -1.
                 */
                if (appContext == null || !granted("android.permission.BLUETOOTH_ADVERTISE")) {
                    logOnce("discoverable:" + reflection.getClass().getName(),
                            "this platform will not change the scan mode ("
                            + reflection + ")");
                    return -1;
                }
                android.content.Intent i = new android.content.Intent(
                        BluetoothAdapter.ACTION_REQUEST_DISCOVERABLE);
                i.putExtra(BluetoothAdapter.EXTRA_DISCOVERABLE_DURATION, 300);
                i.addFlags(android.content.Intent.FLAG_ACTIVITY_NEW_TASK);
                appContext.startActivity(i);
                return mode;
            }
        } catch (Throwable t) {
            logOnce("discoverable:" + t.getClass().getName(),
                    "discoverability could not be changed (" + t + ")");
            return -1;
        }
    }

    /**
     * Start a discovery.  The answer is 1 when the radio accepted it, 0 when
     * it did not - an inquiry already running is cancelled first, because
     * "start" has to mean a fresh set of results and Android's own
     * startDiscovery() would otherwise be a no-op the midlet never hears
     * about.
     *
     * <p>{@code durationMs} is ignored: Android's inquiry is the controller's
     * own and runs for as long as it runs.  It is accepted because the port
     * hook carries it, and the runtime's inquiry_ms field is what actually
     * shortens a wait.
     */
    static int inquiryStart(int durationMs) {
        if (available() == 0 || !grantedScan()) {
            return 0;
        }
        BluetoothAdapter a = adapter;
        if (a == null) {
            return 0;
        }
        try {
            if (a.isDiscovering()) {
                a.cancelDiscovery();
            }
            if (!a.startDiscovery()) {
                logOnce("inquiry-start", "the platform refused to start"
                        + " discovery");
                return 0;
            }
            synchronized (LOCK) {
                discovering = true;
            }
            return 1;
        } catch (Throwable t) {
            logOnce("inquiry-start:" + t.getClass().getName(),
                    "an inquiry could not be started (" + t + ")");
            return 0;
        }
    }

    /**
     * Devices found so far, in discovery order, one TAB-separated line each:
     *
     *     address, name, deviceClass
     *
     * <p>A device appears once no matter how many inquiry responses it sent,
     * because the map it comes from is keyed by address.  Never null; an
     * empty array means the inquiry has not heard anything yet.
     */
    static String[] inquiryDevices() {
        try {
            List<Entry> devices;
            synchronized (LOCK) {
                devices = new ArrayList<Entry>(DEVICES.values());
            }
            List<String> lines = new ArrayList<String>(devices.size());
            for (Entry entry : devices) {
                try {
                    lines.add(entry.address + "\t" + entry.name + "\t"
                            + (entry.deviceClass & CLASS_MASK));
                } catch (Throwable t) {
                    /* One device this file cannot describe must not hide the
                     * others: the runtime loses a device, not the list. */
                    logOnce("describe:" + t.getClass().getName(),
                            "a discovered device could not be described ("
                                    + t + ")");
                }
            }
            return lines.toArray(new String[lines.size()]);
        } catch (Throwable t) {
            logOnce("devices:" + t.getClass().getName(),
                    "the discovered devices could not be listed (" + t + ")");
            return new String[0];
        }
    }

    /**
     * Cancel a running discovery.  Stopping a discovery that is not running
     * is stopping one that is already stopped: nothing happens.
     */
    static void inquiryStop() {
        try {
            synchronized (LOCK) {
                discovering = false;
            }
            BluetoothAdapter a = adapter;
            if (a != null && a.isDiscovering()) {
                a.cancelDiscovery();
            }
        } catch (Throwable t) {
            logOnce("inquiry-stop:" + t.getClass().getName(),
                    "an inquiry could not be cancelled (" + t + ")");
        }
    }

    /**
     * The services SDP knows for one device, one TAB-separated line each:
     *
     *     url, name, uuidHex
     *
     * <p>{@code address} is the twelve-digit form inquiryDevices() produced;
     * {@code uuidHex} is the service the midlet asked for - a 4-digit 16-bit
     * UUID or a 32-digit 128-bit one - and null searches for every service on
     * the device, which is what a JSR 82 search with a null UUID means.  It is
     * not used to drop records here: Android's SDP client fetches the device's
     * whole record set, and a JSR 82 listener is allowed to see records it did
     * not ask for.  Filtering them away would hide a service that a later
     * search on the same device is entitled to hear about.
     *
     * <p>The first call for an address starts an SDP query and answers with
     * what is already cached (often nothing); the results land in the
     * receiver and the next call has them.  Waiting here would block the VM
     * thread on a remote device, which is exactly the stall the runtime's
     * frame pump exists to avoid.  Never null, and an empty array is a
     * device with no services - a real answer, not an error.
     */
    static String[] services(String address, String uuidHex) {
        try {
            String key = normalizeAddress(address);
            if (!isAddress(key)) {
                logOnce("services-address", "this is not a Bluetooth address");
                return new String[0];
            }
            Entry entry = entryFor(key);
            if (entry == null) {
                return new String[0];
            }
            if (entry.uuids == null) {
                discover(entry);
            }
            List<String> lines = entry.records;
            if (lines == null) {
                return new String[0];
            }
            return lines.toArray(new String[lines.size()]);
        } catch (Throwable t) {
            logOnce("services:" + t.getClass().getName(),
                    "the services of a device could not be listed (" + t + ")");
            return new String[0];
        }
    }

    /*
     * One discovered device.  The address is final and already normalized,
     * because it is the map's key and a second call to the platform for it
     * could disagree with the first; the other three are written by the
     * receiver and read by a midlet, so they are volatile.  records is
     * rebuilt from uuids rather than appended to, so a UUID that arrives
     * twice (cached first, then from the SDP broadcast) is still one service.
     */
    private static final class Entry {

        final String address;
        volatile String name = "";
        volatile int deviceClass;
        volatile List<UUID> uuids;
        volatile List<String> records;

        Entry(String address) {
            this.address = address;
        }
    }

    /*
     * The receiver registered by init().  Three actions matter:
     *
     *   ACTION_FOUND         a device answered the inquiry; add it
     *   ACTION_UUID          SDP came back with a device's services
     *   ACTION_DISCOVERY_FINISHED  the radio is done; allow the next inquiry
     *
     * The filter is built with the actions rather than with the adapter's
     * convenience constants, and registration goes through one helper so that
     * the API 33 "not exported" flag is applied on the releases that require
     * it and the two-argument form is used on the ones that do not know it.
     */
    private static void registerReceiver() {
        synchronized (LOCK) {
            if (receiverRegistered || appContext == null) {
                return;
            }
        }
        IntentFilter filter = new IntentFilter();
        filter.addAction(BluetoothDevice.ACTION_FOUND);
        filter.addAction(BluetoothDevice.ACTION_UUID);
        filter.addAction(BluetoothAdapter.ACTION_DISCOVERY_FINISHED);
        Receiver receiver = new Receiver();
        try {
            if (Build.VERSION.SDK_INT >= 33) {
                appContext.registerReceiver(receiver, filter,
                        Context.RECEIVER_NOT_EXPORTED);
            } else {
                appContext.registerReceiver(receiver, filter);
            }
            synchronized (LOCK) {
                receiverRegistered = true;
            }
        } catch (Throwable t) {
            logOnce("register:" + t.getClass().getName(),
                    "discovery broadcasts cannot be received (" + t + ")");
        }
    }

    private static final class Receiver extends BroadcastReceiver {

        public void onReceive(Context context, Intent intent) {
            try {
                String action = intent == null ? null : intent.getAction();
                if (BluetoothDevice.ACTION_FOUND.equals(action)) {
                    onFound(intent);
                } else if (BluetoothDevice.ACTION_UUID.equals(action)) {
                    onUuid(intent);
                } else if (BluetoothAdapter.ACTION_DISCOVERY_FINISHED
                        .equals(action)) {
                    onFinished();
                }
            } catch (Throwable t) {
                logOnce("receiver:" + t.getClass().getName(),
                        "a Bluetooth broadcast could not be read (" + t + ")");
            }
        }

        private void onFound(Intent intent) {
            BluetoothDevice device = (BluetoothDevice) intent
                    .getParcelableExtra(BluetoothDevice.EXTRA_DEVICE);
            if (device == null) {
                return;
            }
            String address = addressOf(device);
            if (!isAddress(address)) {
                logOnce("found-address", "a discovered device has no usable"
                        + " Bluetooth address");
                return;
            }
            /*
             * Entry construction stays outside the lock because it asks the
             * platform for the name and the class; the lock is taken only to
             * put the result in the map.
             */
            Entry fresh = new Entry(address);
            fresh.name = nameOf(device);
            fresh.deviceClass = classOf(device);
            boolean added = false;
            synchronized (LOCK) {
                if (!DEVICES.containsKey(address)) {
                    DEVICES.put(address, fresh);
                    added = true;
                }
            }
            logOnce("found:" + address, "a device answered the inquiry");
            if (added) {
                findUuids(device, address);
            }
        }

        private void onUuid(Intent intent) {
            BluetoothDevice device = (BluetoothDevice) intent
                    .getParcelableExtra(BluetoothDevice.EXTRA_DEVICE);
            ParcelUuid[] uuids = (ParcelUuid[]) intent
                    .getParcelableArrayExtra(BluetoothDevice.EXTRA_UUID);
            if (device == null || uuids == null) {
                return;
            }
            String address = addressOf(device);
            if (!isAddress(address)) {
                return;
            }
            Entry entry;
            synchronized (LOCK) {
                entry = DEVICES.get(address);
            }
            if (entry == null) {
                /* A device the inquiry never reported: not part of the list
                 * the runtime was given, so its services are not either. */
                return;
            }
            synchronized (entry) {
                List<UUID> found = entry.uuids;
                if (found == null) {
                    found = new ArrayList<UUID>(uuids.length);
                }
                for (int i = 0; i < uuids.length; i++) {
                    UUID uuid = uuids[i] == null ? null : uuids[i].getUuid();
                    if (uuid != null && !found.contains(uuid)) {
                        found.add(uuid);
                    }
                }
                entry.uuids = found;
                entry.records = buildRecords(address, found);
            }
        }

        private void onFinished() {
            synchronized (LOCK) {
                discovering = false;
            }
        }
    }

    /*
     * Ask for a device's services.  Called outside the lock, because
     * fetchUuidsWithSdp() is a binder call into the Bluetooth stack and the
     * receiver thread must not wait behind it.
     *
     * BluetoothDevice.getUuids() is the cached answer: on API 15 and later it
     * returns what SDP already learned and does not itself fetch, so it is
     * read first and fetchUuidsWithSdp() covers the device nothing has asked
     * about yet.  When the cache was empty the SDP answer arrives later as
     * ACTION_UUID, which is why this returns nothing.
     */
    private static void findUuids(BluetoothDevice device, String address) {
        try {
            ParcelUuid[] cached = device.getUuids();
            Entry entry;
            synchronized (LOCK) {
                entry = DEVICES.get(address);
            }
            if (entry == null) {
                return;
            }
            if (cached != null && cached.length > 0) {
                synchronized (entry) {
                    List<UUID> found = new ArrayList<UUID>(cached.length);
                    for (int i = 0; i < cached.length; i++) {
                        UUID uuid = cached[i] == null ? null : cached[i].getUuid();
                        if (uuid != null && !found.contains(uuid)) {
                            found.add(uuid);
                        }
                    }
                    entry.uuids = found;
                    entry.records = buildRecords(address, found);
                }
            }
            if (!device.fetchUuidsWithSdp()) {
                logOnce("sdp-refused", "the platform refused an SDP query for"
                        + " a device");
            }
        } catch (Throwable t) {
            logOnce("sdp:" + t.getClass().getName(),
                    "a device's services could not be queried (" + t + ")");
        }
    }

    /*
     * The device's entry, or null.  services() is handed an address the
     * runtime got from inquiryDevices(), so the usual case is a hit; asking
     * about an address that was never discovered is a miss, not a reason to
     * add it to a list a midlet is walking.
     */
    private static Entry entryFor(String address) {
        synchronized (LOCK) {
            return DEVICES.get(address);
        }
    }

    /*
     * Start an SDP query for one device without holding the lock.  Called
     * when a midlet asks for services before anything has been fetched; the
     * answer arrives through ACTION_UUID, so this call only starts it.
     */
    private static void discover(Entry entry) {
        try {
            BluetoothAdapter a = adapter;
            if (a == null) {
                return;
            }
            BluetoothDevice device = a.getRemoteDevice(entry.address);
            if (device == null) {
                return;
            }
            findUuids(device, entry.address);
        } catch (Throwable t) {
            logOnce("discover:" + t.getClass().getName(),
                    "a device's services could not be queried (" + t + ")");
        }
    }

    /*
     * Every paired device, in the order the adapter lists them.  Called from
     * init() under the lock it takes itself; a failure here means the map
     * stays empty and the inquiry fills it later.
     */
    private static void seedBonded() {
        try {
            BluetoothAdapter a = adapter;
            if (a == null) {
                return;
            }
            Set<BluetoothDevice> bonded = a.getBondedDevices();
            if (bonded == null) {
                return;
            }
            List<Entry> seeded = new ArrayList<Entry>(bonded.size());
            for (BluetoothDevice device : bonded) {
                if (device == null) {
                    continue;
                }
                String address = addressOf(device);
                if (!isAddress(address)) {
                    continue;
                }
                Entry entry = new Entry(address);
                entry.name = nameOf(device);
                entry.deviceClass = classOf(device);
                seeded.add(entry);
            }
            synchronized (LOCK) {
                for (int i = 0; i < seeded.size(); i++) {
                    Entry entry = seeded.get(i);
                    if (!DEVICES.containsKey(entry.address)) {
                        DEVICES.put(entry.address, entry);
                    }
                }
            }
        } catch (Throwable t) {
            logOnce("bonded:" + t.getClass().getName(),
                    "the paired devices could not be listed (" + t + ")");
        }
    }

    /*
     * The service lines for one device: each UUID becomes
     *
     *     url, name, uuid
     *
     * with the url built from the device address and the UUID's channel.
     */
    private static List<String> buildRecords(String address, List<UUID> uuids) {
        List<String> lines = new ArrayList<String>(uuids.size());
        for (int i = 0; i < uuids.size(); i++) {
            try {
                UUID uuid = uuids.get(i);
                String hex = hex(uuid);
                lines.add(urlOf(address, uuid) + "\t" + nameOf(uuid) + "\t"
                        + hex);
            } catch (Throwable t) {
                logOnce("record:" + t.getClass().getName(),
                        "a service record could not be rendered (" + t + ")");
            }
        }
        return lines;
    }

    /*
     * The connection url for one service.  RFCOMM unless the service is the
     * protocol itself - the 16-bit 0x0100 UUID means the device speaks
     * L2CAP and its PSM is not the channel byte below - in which case the
     * url is btl2cap:// and port 1.
     */
    private static String urlOf(String address, UUID uuid) {
        if (uuid == null) {
            return "btspp://" + address + ":1";
        }
        String shortUuid = shortHex(uuid);
        if ("00000100".equals(shortUuid)) {
            return "btl2cap://" + address + ":1";
        }
        return "btspp://" + address + ":" + channelOf(uuid);
    }

    /*
     * The channel byte of a UUID.  A 16-bit RFCOMM UUID is 0000xxxx, where
     * the low byte of xxxx is the server channel the service registered on:
     * UUID 1101 on channel 3 is spelled 00001103.  Anything else - a
     * protocol UUID like 0003, or a 128-bit UUID whose last 32-bit group is
     * a random protocol id - carries no channel, and RFCOMM's own default of
     * 1 is the honest guess.
     */
    private static int channelOf(UUID uuid) {
        String shortUuid = shortHex(uuid);
        if (shortUuid == null) {
            return 1;
        }
        /*
         * The channel is the low byte of the 16-bit UUID: UUID 1101 on
         * channel 3 is spelled 00001103, so the digits move aside by two and
         * the last two are the channel.
         */
        int channel = (hexDigit(shortUuid.charAt(6)) << 4)
                | hexDigit(shortUuid.charAt(7));
        if (channel > 0 && channel < 31) {
            return channel;
        }
        return 1;
    }

    /*
     * The 16-bit alias of a UUID, in the same 8-digit spelling NAMES uses, or
     * null when the UUID is not an alias of the SDP base.
     */
    private static String shortHex(UUID uuid) {
        try {
            String text = uuid.toString().toLowerCase(Locale.ROOT);
            if (text.length() == 36
                    && "0000".equals(text.substring(0, 4))
                    && text.substring(8).equals("-0000-1000-8000-00805f9b34fb")) {
                return "0000" + text.substring(4, 8);
            }
            return null;
        } catch (Throwable t) {
            return null;
        }
    }

    /*
     * The name JSR 82 gives a UUID in its own tables, or "service <uuid>"
     * for anything else - a MIDlet's own 128-bit UUID is a name no table can
     * hold, and inventing a friendly one would be worse than the number.  The
     * table is keyed by the full canonical string rather than by its first
     * eight digits so that a 128-bit UUID that merely starts like a
     * well-known one is not mistaken for it.
     */
    private static String nameOf(UUID uuid) {
        try {
            String text = uuid.toString().toLowerCase(Locale.ROOT);
            String known = NAMES.get(text);
            if (known != null) {
                return known;
            }
            return "service " + text;
        } catch (Throwable t) {
            return "service unknown";
        }
    }

    /*
     * The 32 hex digits of a UUID with the dashes removed, which is the form
     * the C side parses into the port's 16 byte uuid field.
     */
    private static String hex(UUID uuid) {
        if (uuid == null) {
            return "";
        }
        String text = uuid.toString().toLowerCase(Locale.ROOT);
        StringBuilder out = new StringBuilder(32);
        for (int i = 0; i < text.length(); i++) {
            char c = text.charAt(i);
            if (c != '-') {
                out.append(c);
            }
        }
        return out.toString();
    }

    /* The device's address, normalized, or null when the platform will not
     * give one. */
    private static String addressOf(BluetoothDevice device) {
        try {
            return normalizeAddress(device.getAddress());
        } catch (Throwable t) {
            return null;
        }
    }

    private static String nameOf(BluetoothDevice device) {
        try {
            String value = device.getName();
            return value == null ? "" : field(value);
        } catch (Throwable t) {
            return "";
        }
    }

    /* The class-of-device word as an unsigned int, 0 when the platform has
     * none: a device with no class is a device the runtime shows as
     * "unknown", which is what it is. */
    private static int classOf(BluetoothDevice device) {
        try {
            BluetoothClass cls = device.getBluetoothClass();
            return cls == null ? 0 : (int) (cls.getDeviceClass() & CLASS_MASK);
        } catch (Throwable t) {
            return 0;
        }
    }

    /*
     * Every Bluetooth string this file reads is pulled through here before it
     * reaches one of the TAB-separated lines: a device name is whatever the
     * peer chose to call itself, and a tab, newline or carriage return in one
     * would otherwise turn one field into two or hide the rest of the line.
     */
    private static String field(String text) {
        StringBuilder out = new StringBuilder(text.length());
        for (int i = 0; i < text.length(); i++) {
            char c = text.charAt(i);
            out.append(c < ' ' ? ' ' : c);
        }
        return out.toString();
    }

    private static boolean isAddress(String address) {
        if (address == null || address.length() != 12) {
            return false;
        }
        boolean any = false;
        for (int i = 0; i < 12; i++) {
            char c = address.charAt(i);
            if (c != '0') {
                any = true;
            }
            if (hexDigit(c) < 0) {
                return false;
            }
        }
        return any;
    }

    /*
     * A Bluetooth address as J2ME spells it: twelve lower-case hex digits and
     * no colons ("001122334455").  Android prints them upper case with colons,
     * and an address of all zeros is the platform's way of saying "unknown",
     * which is the same string an unknowable address gets.
     */
    private static String normalizeAddress(String address) {
        String text = address == null ? "" : address.toLowerCase(Locale.ROOT);
        StringBuilder out = new StringBuilder(12);
        for (int i = 0; i < text.length() && out.length() < 12; i++) {
            char c = text.charAt(i);
            if (hexDigit(c) >= 0) {
                out.append(c);
            }
        }
        while (out.length() < 12) {
            out.insert(0, '0');
        }
        return out.toString();
    }

    private static int hexDigit(char c) {
        if (c >= '0' && c <= '9') {
            return c - '0';
        }
        if (c >= 'a' && c <= 'f') {
            return c - 'a' + 10;
        }
        if (c >= 'A' && c <= 'F') {
            return c - 'A' + 10;
        }
        return -1;
    }

    /*
     * Whether a permission is granted.  checkSelfPermission() is API 23, and
     * the Bluetooth runtime permissions only exist from API 31, so this is
     * only ever asked on a release that has the method - the one call site
     * checks the version first.
     */
    private static boolean granted(String permission) {
        try {
            Context ctx = appContext;
            if (ctx == null) {
                return false;
            }
            if (Build.VERSION.SDK_INT >= 23) {
                return ctx.checkSelfPermission(permission)
                        == PackageManager.PERMISSION_GRANTED;
            }
            return true;
        } catch (Throwable t) {
            return false;
        }
    }

    /*
     * Whether discovery may run.  BLUETOOTH_SCAN is API 31 and later only;
     * before that the install-time BLUETOOTH permission covers it, so the
     * answer is yes on those releases.
     */
    private static boolean grantedScan() {
        if (Build.VERSION.SDK_INT < 31) {
            return true;
        }
        if (granted("android.permission.BLUETOOTH_SCAN")) {
            return true;
        }
        logOnce("perm-scan", "BLUETOOTH_SCAN is not granted, so an inquiry"
                + " would find nothing");
        return false;
    }

    /*
     * A device can answer the same inquiry several times, and a game asks for
     * its devices every time a midlet starts, so a device that cannot be used
     * would otherwise write a line per attempt.  One line per kind of failure,
     * and the set is capped because nothing may grow a static field one
     * distinct failure at a time.
     */
    private static void logOnce(String kind, String detail) {
        synchronized (LOGGED) {
            if (LOGGED.size() >= LOG_ONCE_MAX || !LOGGED.add(kind)) {
                return;
            }
        }
        Log.w(TAG, "bluetooth: " + detail);
    }
}
