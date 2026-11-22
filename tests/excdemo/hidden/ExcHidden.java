/*
 * Package-private and in another package: Class.newInstance() is specified to
 * refuse it with IllegalAccessException, and the message must name the class.
 *
 * It lives in its own package (not the demo's default package) because a
 * package-private class IS reachable from code in the same package - handsets
 * rely on that, and 3D_Tetris does exactly this through Class.newInstance().
 */
package hidden;

class ExcHidden {
    public ExcHidden() { }
}
