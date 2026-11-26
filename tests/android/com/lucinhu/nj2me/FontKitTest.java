package com.lucinhu.nj2me;

/**
 * The font settings' arithmetic, off the device.
 *
 * <p>What is pinned here is what the settings rows promise: how big a cached
 * glyph is (the subset reports real bytes), which files are fonts, that the
 * copy the app opens is named by this code and not by whatever the picker
 * handed back, and that the two style switches really do key different cache
 * entries - a switch that silently kept serving the old glyphs would look like
 * a broken toggle on the device.
 */
public final class FontKitTest {

    private FontKitTest() {
    }

    public static void main(String[] args) {
        Check.section("two uint16 dimensions followed by one coverage byte per pixel");
        Check.eq("12x12", FontKit.glyphBytes(12, 12), 148);
        Check.eq("16x16", FontKit.glyphBytes(16, 16), 260);
        byte[] header = new byte[4];
        FontKit.putDimensions(header, 256, 512);
        Check.eq("width low", header[0], 0);
        Check.eq("width high", header[1], 1);
        Check.eq("height low", header[2], 0);
        Check.eq("height high", header[3], 2);
        Check.eq("nothing is nothing", FontKit.glyphBytes(0, 12), 0);

        Check.section("only a file a font loader would open");
        Check.isTrue("ttf", FontKit.looksLikeFont("MyFont.ttf"));
        Check.isTrue("otf in any case", FontKit.looksLikeFont("font.OTF"));
        Check.isTrue("ttc", FontKit.looksLikeFont("collection.ttc"));
        Check.isTrue("a picture is not a font", !FontKit.looksLikeFont("holiday.jpg"));
        Check.isTrue("no name at all is not a font", !FontKit.looksLikeFont(null));

        Check.section("the copy is named by this code, not by the picker");
        Check.eq("keeps the extension", FontKit.storeName("My Font.ttf"), "custom.ttf");
        Check.eq("otf stays otf", FontKit.storeName("a.otf"), "custom.otf");
        Check.eq("a path is not a name", FontKit.storeName("../../etc/passwd"), "custom.ttf");
        Check.eq("no extension gets .ttf", FontKit.storeName("font"), "custom.ttf");

        Check.section("one cache key carries the character and both switches");
        int plain = FontKit.cacheKey('A', 12, 1, false, true);
        Check.eq("the code point comes back", FontKit.cacheCodePoint(plain), (int) 'A');
        Check.isTrue("bold is different ink",
                FontKit.cacheKey('A', 12, 1, true, true) != plain);
        Check.isTrue("hard edges are different ink",
                FontKit.cacheKey('A', 12, 1, false, false) != plain);
        Check.isTrue("a size is different ink",
                FontKit.cacheKey('A', 14, 1, false, true) != plain);
        Check.eq("a hanzi survives the packing",
                FontKit.cacheCodePoint(FontKit.cacheKey(0x4e2d, 16, 1, true, false)), 0x4e2d);

        java.util.HashSet<Integer> keys = new java.util.HashSet<Integer>();
        for (int cell = 1; cell <= 32; ++cell) {
            for (int scale = 1; scale <= FontKit.MAX_SAMPLE_SCALE; ++scale) {
                int key = FontKit.cacheKey(0x10ffff, cell, scale, true, true);
                Check.eq("trim decodes Unicode at every scale", FontKit.cacheCodePoint(key), 0x10ffff);
                Check.isTrue("cell and sample scale select distinct ink", keys.add(key));
            }
        }
        Check.section("sizes a player can read");
        Check.eq("bytes", FontKit.size(512), "512 B");
        Check.eq("kilobytes", FontKit.size(40 * 1024), "40.0 KB");
        Check.eq("megabytes", FontKit.size(3 * 1024 * 1024), "3.0 MB");

        Check.done();
    }
}
