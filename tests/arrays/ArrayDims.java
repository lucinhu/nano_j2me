/* Multidimensional arrays, exercised through real bytecode.
 *
 * The chess title in the paced corpus indexes a[d][6] in a table it builds
 * itself, so the runtime's side of that shape - multianewarray, nested
 * lengths, ragged rows, and the out-of-range exception - deserves a test that
 * runs the instructions rather than a C helper.  class-file version 52
 * (--release 8) because that is what the tree's other fixtures use.
 */
public class ArrayDims {
    public static int check() {
        int[][] g = new int[3][7];
        int[][][] t = new int[2][4][5];
        int[][] ragged = new int[3][];

        ragged[0] = new int[2];
        ragged[1] = new int[5];
        ragged[2] = new int[1];

        boolean ok = g.length == 3 && g[0].length == 7 && g[2].length == 7;
        ok = ok && t.length == 2 && t[0].length == 4 && t[0][0].length == 5;
        ok = ok && t[1][3].length == 5;
        ok = ok && ragged[0].length == 2 && ragged[1].length == 5
                && ragged[2].length == 1;

        g[2][6] = 1000;
        ok = ok && g[2][6] == 1000;
        t[1][3][4] = 7;
        ok = ok && t[1][3][4] == 7;

        int caught = 0;
        try {
            g[0][7] = 1;
        } catch (ArrayIndexOutOfBoundsException e) {
            caught = 1;
        }
        ok = ok && caught == 1;

        return ok ? 1 : 0;
    }
}
