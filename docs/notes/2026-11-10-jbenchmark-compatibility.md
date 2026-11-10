# JBenchmark compatibility fixes on Korvo

JBenchmark 2.1.1 stopped in its UI test after updating a non-interactive Gauge.
MIDP allows application updates regardless of user interaction mode. Gauge now
preserves the initial value, clamps definite values, accepts indefinite states
0 through 3, and resets the value when switching range kinds. Programmatic
changes request repaint without sending an ItemStateListener user event.
Authority: [MIDP Gauge](https://docs.oracle.com/javame/config/cldc/ref-impl/midp2.0/jsr118/javax/microedition/lcdui/Gauge.html).

JBenchmark3D 3.1.0 failed in d.c after loading maplow.m3g: Loader returned an
extra VertexBuffer and skeleton Group. Root discovery already tracked Mesh
edges, but omitted SkinnedMesh's vertex buffer, skeleton and bone edges. Mark
these references as well. A synthetic container checks that unrelated Groups
remain roots and referenced skin resources do not. The older skin renderer
fixture also skipped result index zero; use the normal zero-based Java array.

JBenchmark HD 4.0.3 unboxed Graphics3D.getProperties().get("numTextureUnits")
and received null. Advertise the renderer's existing single texture unit as
an Integer. No new rendering capability is claimed.
Authority: [JSR 184](https://jcp.org/aboutJava/communityprocess/mrel/jsr184/index.html).

Validation uses ASan/UBSan test_lcdui, test_m3g, test_m3g_file and
 test_m3g_load_memory. Original benchmark JARs are unchanged and not included.
Device scores are recorded in the parent Korvo repository after flashing.
