package ioapi;

import java.io.ByteArrayInputStream;
import java.io.ByteArrayOutputStream;
import java.io.DataInput;
import java.io.DataInputStream;
import java.io.DataOutput;
import java.io.DataOutputStream;
import java.io.EOFException;
import java.io.IOException;

/*
 * Java-level regression fixture for the java.io contract that CLDC/MIDP
 * games depend on: DataInput/DataOutput reached through an INTERFACE
 * reference (invokeinterface, not a C-level call), and DataOutputStream's
 * float/double/char encoding.
 *
 * The point of the interface reference is the constant pool: a call through
 * java/io/DataInput.readFloat()F is resolved against the interface's declared
 * method set before dispatch.  A C nj_call() bypasses that resolution and
 * therefore cannot catch the bug this fixture exists for.
 *
 * Compiled with `javac --release 8`; tests/ioapi/gen_c_header.py embeds these
 * classes into tests/test_ioapi.c.  Every method returns an int so the C side
 * can compare it with the desktop JVM's answer (IoApiDesktop prints the same
 * tags).  Do not print from here: the C test drives the values.
 */
public final class IoApiProbe {
    private IoApiProbe() {
    }

    private static byte[] bytes(int... v) {
        byte[] b = new byte[v.length];
        for (int i = 0; i < v.length; i++) {
            b[i] = (byte) v[i];
        }
        return b;
    }

    private static DataInput din(int... v) {
        return new DataInputStream(new ByteArrayInputStream(bytes(v)));
    }

    private static int digest(byte[] b) {
        int h = 17;
        for (int i = 0; i < b.length; i++) {
            h = h * 31 + (b[i] & 0xFF);
        }
        return h;
    }

    /* ---------------- DataInput.readFloat through an interface ------- */

    public static int readFloatBits() throws IOException {
        DataInput in = din(0x3F, 0xC0, 0x00, 0x00);     /* 1.5f */
        return Float.floatToIntBits(in.readFloat());
    }

    public static int readFloatNegZeroBits() throws IOException {
        DataInput in = din(0x80, 0x00, 0x00, 0x00);     /* -0.0f */
        return Float.floatToIntBits(in.readFloat());
    }

    public static int readFloatPosInfBits() throws IOException {
        DataInput in = din(0x7F, 0x80, 0x00, 0x00);     /* +Infinity */
        return Float.floatToIntBits(in.readFloat());
    }

    public static int readFloatNanBits() throws IOException {
        DataInput in = din(0x7F, 0xC0, 0x00, 0x01);     /* NaN payload 1 */
        return Float.floatToIntBits(in.readFloat());
    }

    public static int readFloatEof() throws IOException {
        DataInput in = new DataInputStream(new ByteArrayInputStream(new byte[0]));
        try {
            in.readFloat();
            return 30;
        } catch (EOFException e) {
            return 31;
        } catch (IOException e) {
            return 32;
        }
    }

    /* One byte short of a full float: illegal EOF, not a Short read. */
    public static int readFloatTruncatedEof() throws IOException {
        DataInput in = din(0x3F, 0xC0, 0x00);
        try {
            in.readFloat();
            return 33;
        } catch (EOFException e) {
            return 34;
        } catch (IOException e) {
            return 35;
        }
    }

    /* ---------------- DataInput.readDouble through an interface ------ */

    public static int readDoublePiBits() throws IOException {
        DataInput in = din(0x40, 0x09, 0x21, 0xFB, 0x54, 0x44, 0x2D, 0x18);
        long b = Double.doubleToLongBits(in.readDouble());
        return (int) (b ^ (b >>> 32));
    }

    public static int readDoubleNegZeroBits() throws IOException {
        DataInput in = din(0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00);
        long b = Double.doubleToLongBits(in.readDouble());
        return (int) (b ^ (b >>> 32));
    }

    public static int readDoubleNegInfBits() throws IOException {
        DataInput in = din(0xFF, 0xF0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00);
        long b = Double.doubleToLongBits(in.readDouble());
        return (int) (b ^ (b >>> 32));
    }

    public static int readDoubleEof() throws IOException {
        DataInput in = new DataInputStream(new ByteArrayInputStream(new byte[0]));
        try {
            in.readDouble();
            return 36;
        } catch (EOFException e) {
            return 37;
        } catch (IOException e) {
            return 38;
        }
    }

    /* ---------------- DataOutput encoding through an interface ------- */

    public static int writeFloatBytes() throws IOException {
        ByteArrayOutputStream bos = new ByteArrayOutputStream();
        DataOutput out = new DataOutputStream(bos);
        out.writeFloat(1.5f);
        out.writeFloat(-0.0f);
        out.writeFloat(Float.POSITIVE_INFINITY);
        return digest(bos.toByteArray());
    }

    public static int writeFloatLength() throws IOException {
        ByteArrayOutputStream bos = new ByteArrayOutputStream();
        DataOutput out = new DataOutputStream(bos);
        out.writeFloat(1.5f);
        out.writeFloat(-0.0f);
        out.writeFloat(Float.POSITIVE_INFINITY);
        return bos.toByteArray().length;
    }

    public static int writeDoubleBytes() throws IOException {
        ByteArrayOutputStream bos = new ByteArrayOutputStream();
        DataOutput out = new DataOutputStream(bos);
        out.writeDouble(1.5);
        out.writeDouble(-0.0);
        out.writeDouble(Double.NEGATIVE_INFINITY);
        return digest(bos.toByteArray());
    }

    public static int writeCharsBytes() throws IOException {
        ByteArrayOutputStream bos = new ByteArrayOutputStream();
        DataOutput out = new DataOutputStream(bos);
        out.writeChars("Az");
        return digest(bos.toByteArray());
    }

    /* ---------------- round trip ------------------------------------- */

    public static int roundTripBits() throws IOException {
        ByteArrayOutputStream bos = new ByteArrayOutputStream();
        DataOutputStream out = new DataOutputStream(bos);
        out.writeFloat(1.5f);
        out.writeFloat(-2.25f);
        out.writeDouble(1.0 / 3.0);
        DataInput in = new DataInputStream(
                new ByteArrayInputStream(bos.toByteArray()));
        int a = Float.floatToIntBits(in.readFloat());
        int b = Float.floatToIntBits(in.readFloat());
        long c = Double.doubleToLongBits(in.readDouble());
        return a ^ b ^ (int) (c ^ (c >>> 32));
    }
}
