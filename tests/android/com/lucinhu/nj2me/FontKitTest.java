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
        Check.section("a glyph costs two header bytes and one per pixel");
        Check.eq("12x12", FontKit.glyphBytes(12, 12), 146);
        Check.eq("16x16", FontKit.glyphBytes(16, 16), 258);
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
        int plain = FontKit.cacheKey('A', 12, false, true);
        Check.eq("the code point comes back", FontKit.cacheCodePoint(plain), (int) 'A');
        Check.isTrue("bold is different ink",
                FontKit.cacheKey('A', 12, true, true) != plain);
        Check.isTrue("hard edges are different ink",
                FontKit.cacheKey('A', 12, false, false) != plain);
        Check.isTrue("a size is different ink",
                FontKit.cacheKey('A', 14, false, true) != plain);
        Check.eq("a hanzi survives the packing",
                FontKit.cacheCodePoint(FontKit.cacheKey(0x4e2d, 16, true, false)), 0x4e2d);

        Check.section("sizes a player can read");
        Check.eq("bytes", FontKit.size(512), "512 B");
        Check.eq("kilobytes", FontKit.size(40 * 1024), "40.0 KB");
        Check.eq("megabytes", FontKit.size(3 * 1024 * 1024), "3.0 MB");

        Check.done();
    }
}
