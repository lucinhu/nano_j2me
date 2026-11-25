package com.lucinhu.nj2me;

import android.content.Context;

/** Shared player preferences; the game also applies live settings to its VM. */
final class DisplayControls {
    static final String[] OFF_ON = {"关闭", "开启"};
    enum Option { FONT, PICTURE, FPS, GLES }

    static Md3.Setting control(final Context ctx, final Option option, final Runnable changed) {
        String title;
        String[] labels = OFF_ON;
        int index;
        final int[] caps = {-1, 60, 30, 0};
        switch (option) {
        case FONT: title = "字体反锯齿"; index = Settings.fontAntiAlias(ctx) ? 1 : 0; break;
        case PICTURE: title = "画面反锯齿"; index = Settings.pictureAntiAlias(ctx) ? 1 : 0; break;
        case GLES: title = "硬件加速（GLES2，下次启动）"; index = Settings.gles2Experimental(ctx) ? 1 : 0; break;
        default:
            title = "帧率上限";
            int current = Settings.frameCap(ctx);
            labels = new String[caps.length];
            index = 0;
            for (int i = 0; i < caps.length; i++) {
                labels[i] = Settings.frameCapText(caps[i]);
                if (current == caps[i]) index = i;
            }
        }
        Md3.Setting row = new Md3.Setting(ctx, title, labels, index, new Md3.Pick() {
            @Override public void set(int i) {
                switch (option) {
                case FONT: Settings.setFontAntiAlias(ctx, i == 1); break;
                case PICTURE: Settings.setPictureAntiAlias(ctx, i == 1); break;
                case FPS: Settings.setFrameCap(ctx, caps[i]); break;
                case GLES: Settings.setGles2Experimental(ctx, i == 1); break;
                }
                if (changed != null) changed.run();
            }
        });
        if (option == Option.GLES && !GameActivity.graphicsSupported()) {
            row.setText("硬件加速：此安装包未含 GLES2");
            row.setEnabled(false);
        }
        return row;
    }
}
