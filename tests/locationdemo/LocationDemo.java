/*
 * JSR 179 fixture: exercises javax.microedition.location through real
 * bytecode so the C class library is proven with the interpreter, not only
 * by unit calls.  Prints stable markers that tools/check_location.sh asserts.
 */
import javax.microedition.lcdui.Display;
import javax.microedition.lcdui.Form;
import javax.microedition.location.AddressInfo;
import javax.microedition.location.Coordinates;
import javax.microedition.location.Criteria;
import javax.microedition.location.Location;
import javax.microedition.location.LocationException;
import javax.microedition.location.LocationListener;
import javax.microedition.location.LocationProvider;
import javax.microedition.location.Orientation;
import javax.microedition.location.QualifiedCoordinates;
import javax.microedition.midlet.MIDlet;

public class LocationDemo extends MIDlet implements LocationListener {
    static int updates;

    public void startApp() {
        Form f = new Form("Location");
        Display.getDisplay(this).setCurrent(f);
        try {
            Criteria c = new Criteria();
            c.setHorizontalAccuracy(50);
            c.setPreferredPowerConsumption(Criteria.POWER_USAGE_LOW);
            c.setSpeedAndCourseRequired(true);
            System.out.println("LOCATION criteria=" + c.getHorizontalAccuracy()
                    + "/" + c.getPreferredPowerConsumption() + "/"
                    + c.isSpeedAndCourseRequired());

            LocationProvider p = LocationProvider.getInstance(c);
            System.out.println("LOCATION provider=" + (p != null) + " state="
                    + p.getState() + " available="
                    + LocationProvider.AVAILABLE);

            Location last = p.getLastKnownLocation();
            check(last, "last");
            Location fix = p.getLocation(1000);
            check(fix, "fix");

            QualifiedCoordinates a = new QualifiedCoordinates(60.1699, 24.9384,
                    10.0f, 30.0f, 10.0f);
            QualifiedCoordinates b = new QualifiedCoordinates(60.1709, 24.9384,
                    10.0f, 30.0f, 10.0f);
            System.out.println("LOCATION distance=" + Math.round(a.distance(b) * 100)
                    + " azimuth=" + Math.round(a.azimuthTo(b)));
            System.out.println("LOCATION accuracy=" + Math.round(a.getHorizontalAccuracy())
                    + "/" + Math.round(a.getVerticalAccuracy()));

            Coordinates deg = new Coordinates(Coordinates.convert("60.1699"),
                    24.9384, 5.0f);
            System.out.println("LOCATION convert=" + Math.round(deg.getLatitude() * 1000)
                    + " formatted=" + Coordinates.convert(24.9384, Coordinates.DD_MM));

            Orientation o = Orientation.getOrientation();
            System.out.println("LOCATION orientation=" + Math.round(o.getCompassAzimuth())
                    + "/" + o.isOrientationMagnetic() + "/"
                    + Math.round(o.getPitch()) + "/" + Math.round(o.getRoll()));

            AddressInfo addr = new AddressInfo("Main Street 1", "Helsinki",
                    "+358 0 000 000", "demo@example.org");
            addr.setField(AddressInfo.CITY, "Helsinki");
            System.out.println("LOCATION address=" + addr.getLine1() + "/"
                    + addr.getField(AddressInfo.CITY) + "/" + addr.getPhoneNumber()
                    + "/" + addr.getEmailAddress());

            try {
                new Coordinates(91.0, 0.0, 0.0f);
                System.out.println("LOCATION invalid=missed");
            } catch (IllegalArgumentException e) {
                System.out.println("LOCATION invalid=caught");
            }

            try {
                LocationProvider.addProximityListener(null, a, 10.0f);
                System.out.println("LOCATION proximity=missed");
            } catch (LocationException e) {
                System.out.println("LOCATION proximity=caught");
            }

            updates = 0;
            p.setLocationListener(this, -1, -1, -1);
            System.out.println("LOCATION listener=" + updates);
            p.reset();
        } catch (Exception e) {
            System.out.println("LOCATION failed=" + e);
        }
        notifyDestroyed();
    }

    static void check(Location loc, String tag) {
        QualifiedCoordinates q = loc.getQualifiedCoordinates();
        System.out.println("LOCATION " + tag + " valid=" + loc.isValid()
                + " lat=" + Math.round(q.getLatitude() * 1000)
                + " lon=" + Math.round(q.getLongitude() * 1000)
                + " alt=" + Math.round(q.getAltitude())
                + " speed=" + Math.round(loc.getSpeed())
                + " course=" + Math.round(loc.getCourse())
                + " ts=" + (loc.getTimestamp() > 0));
    }

    public void pauseApp() {
    }

    public void destroyApp(boolean unconditional) {
    }

    public void locationUpdated(LocationProvider p, Location loc) {
        updates++;
        System.out.println("LOCATION updated=" + updates + " valid=" + loc.isValid());
    }

    public void providerStateChanged(LocationProvider p, int state) {
        System.out.println("LOCATION providerState=" + state);
    }
}
