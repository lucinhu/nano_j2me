/* Static initialiser whose side effect is observable from the gate. */
public final class InitProbe {
    public static final int VALUE;

    static {
        System.out.println("CLASSINIT probe-init");
        VALUE = 7;
    }
}
