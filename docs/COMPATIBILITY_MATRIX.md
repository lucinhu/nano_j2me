# Compatibility matrix

One row per JAR from the compatibility corpus (2462 titles): fresh rows from the 2026-11-23 0.5.35 corpus sweeps; BT INFOx re-measured after Choice.POPUP support.

Rows that were re-measured keep the newer numbers; the remaining
historical rows keep the numbers their original sweep recorded, so
compare rows within one measurement batch, not across the whole table.

* port `headless`, screen `240x320`, `--fps 30`, `--run-frames 600`;
* heap `16 MB`, retried at `32 MB` only after an out-of-memory;
* one shared input script (fire, both soft keys, two screen taps, four arrows,
  digits, `*`), a fresh RMS directory per title;
* `运行正常` = exit 0, at least half the frames painted, nothing uncaught.
* `未呈现画面` = exit 0, ran to the end on its own, never painted a frame (the run loop had nothing to show - a quiet title, not a crash).

Corpus provenance and size are in `docs/CORPUS.md`; regenerate this table
with `tools/make_compat_matrix.py <rows dir>` after a new sweep.

Built from `HEAD`, 2026-11-23 (recorded by `tools/sweep_compat.py`).

| verdict | titles |
| --- | --- |
| 运行正常 | 2421 |
| 非可用包 | 15 |
| 标题自身异常 | 10 |
| 超时 | 6 |
| 失败 | 4 |
| 未呈现画面 | 3 |
| 标题自行退出 | 3 |

| 3D API used | titles |
| --- | --- |
| - | 1736 |
| nokia-ui | 604 |
| m3g | 68 |
| m3g,nokia-ui | 30 |
| mascot | 20 |
| mascot,nokia-ui | 2 |
| m3g,mascot,nokia-ui | 1 |
| m3g,mascot | 1 |

| title | size KB | MIDlet name | vendor | version | profile | 3D | classes | heap MB | frames | bytecodes | uncaught | exceptions | verdict | note |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| (新)三国之关云长战曹贼_2550.jar | 934 | (新)三国之关云长战曹贼 | - | 1.0.0 | MIDP-1.0 | - | 21 | 16 | 600 | 4305855 | 0 | - | 运行正常 |  |
| 1942空战_10403.jar | 582 | 1942AirCombat supplied by ne | - | 1.0.17 | MIDP-2.0 | - | 14 | 16 | 600 | 2454253 | 0 | java/lang/ArrayIndexOutOfBoundsE | 运行正常 |  |
| 1945空战英雄_6105.jar | 588 | ACH1945 Lite | - | 1.0.0 | MIDP-2.0 | nokia-ui | 32 | 16 | 600 | 4734587 | 0 | - | 运行正常 | needs --device nokia6230 |
| 3D台球之王_5014.jar | 314 | 3D台球之王(EA正版) | - | 1.0.0 | MIDP-2.0 | - | 23 | 16 | 600 | 122441 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| 3D坦克对战1944_7139.jar | 345 | Tank Ace | - | 1.03 | MIDP-2.0 | - | 32 | 16 | 600 | 51946261 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| 3D城市桌球_8299.jar | 262 | Biliardo 3D Urban Tour | - | 1.5.9 | MIDP-1.0 | m3g | 4 | 16 | 600 | 1347401 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| 3D孤胆枪手2010_4634.jar | 985 | Alien Shooter 3D | - | 2.0 | MIDP-2.0 | nokia-ui | 19 | 16 | 600 | 232693436 | 0 | java/lang/ArrayIndexOutOfBoundsE | 运行正常 |  |
| 3D拉力赛-进化汉化版_6366.jar | 347 | 3DRallyEvolution | - | 1.0 | MIDP-2.0 | m3g | 12 | 16 | 600 | 429138 | 0 | javax/microedition/rms/InvalidRe | 运行正常 |  |
| 3D摩托赛车_9287.jar | 335 | 3DMotoRacing by Racer_Team | - | 1.0 | MIDP-2.0 | m3g | 12 | 16 | 600 | 1187384 | 0 | java/lang/IllegalStateException, | 运行正常 |  |
| 3D武装直升机2_archive.jar | 80 | Attack Chopper 2 | - | 1.01 | MIDP-2.0 | - | 16 | 16 | 600 | 911 | 0 | java/io/IOException,java/lang/Nu | 运行正常 | painted 602 frames but made no draw calls (b |
| 3D滚球_11586.jar | 69 | 3D QBlast | - | 1.0.0 | MIDP-1.0 | nokia-ui | 19 | 16 | 600 | 1432233 | 0 | - | 运行正常 |  |
| 3D激爆拳击_8805.jar | 1024 | K.O. Legends | - | 1.0.8 | MIDP-2.0 | m3g,nokia-ui | 23 | 16 | 600 | 4082192911 | 0 | java/lang/NullPointerException | 运行正常 |  |
| 3D版田园物语_6744.jar | 969 | 3D版田园物语 | - | 1.0 | MIDP-2.0 | - | 65 | 16 | 600 | 37076792 | 0 | java/lang/NullPointerException | 运行正常 |  |
| 3D疯狂过山车S60v5版_3875.jar | 797 | Roller Rev 99 | - | 1.0.29 | MIDP-2.0 | - | 39 | 16 | 600 | 186009 | 0 | java/io/EOFException | 运行正常 |  |
| 3D科林麦克雷拉力赛之尘埃_6600.jar | 5601 | Colin McRae DiRT 3D_konno | - | 1.2.5 | MIDP-1.0 | m3g,nokia-ui | 39 | 16 | 600 | 19568568 | 0 | java/lang/NullPointerException | 运行正常 |  |
| 3D跳台滑雪2011_4507.jar | 964 | SkokiNarciarskie2011 | - | 1.1.0 | MIDP-2.0 | m3g | 91 | 16 | 600 | 6798576 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| Boun_en_ar_de_he-IL_ru-RU.jar | 40 | Bounce | - | 1.24 | MIDP-1.0 | nokia-ui | 8 | 16 | 600 | 5094 | 0 | - | 运行正常 | runs without input; the generic key script m |
| CCTV贺岁游戏-三国演义_6527.jar | 929 | 三国演义 | - | 1.0.0 | MIDP-1.0 | nokia-ui | 33 | 16 | 600 | 2731251893 | 0 | - | 运行正常 |  |
| CS反恐特警-狙击精英_6539.jar | 417 | (国际巨作)CS反恐特警-狙击精英 | - | 1.0.0 | MIDP-1.0 | - | 20 | 16 | 600 | 1850226 | 0 | java/lang/ArrayIndexOutOfBoundsE | 运行正常 |  |
| CS反恐特警真3D版2_6747.jar | 255 | CS反恐特警真3D版II | - | 1.0 | MIDP-1.0 | - | 34 | 16 | 600 | 308587871 | 0 | - | 运行正常 |  |
| CS反恐精英(蓝牙对战)_6858.jar | 335 | Micro Counter Strike v1.1 | - | 1.1.0 | MIDP-2.0 | m3g | 61 | 16 | 600 | 665 | 0 | - | 运行正常 | painted 600 frames but made no draw calls (b |
| FIFA09足球(EA中文版)_7962.jar | 695 | FIFA 09宝软网baoruan.com免费分享 | - | 1.0 | MIDP-2.0 | - | 36 | 16 | 600 | 753622 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| Flippy跳跃大冒险_archive.jar | 141 | Flippy's Flying Frenzy | - | 1.07 | MIDP-1.0 | nokia-ui | 13 | 16 | 600 | 1510815 | 0 | java/io/EOFException,javax/micro | 运行正常 |  |
| GT赛车2_36.jar | 1815 | GT Racing 2 | - | 1.0.9 | MIDP-2.0 | nokia-ui | 11 | 16 | 600 | 18620391 | 0 | - | 运行正常 | runs without input; the generic key script d |
| JBenchmark HD.jar | 343 | JBenchmark HD | - | 4.0.3 | MIDP-2.0 | m3g | 14 | 16 | 600 | 14599662 | 0 | - | 运行正常 | runs without input; the generic key script m |
| JBenchmark.jar | 25 | JBenchmark | - | 1.1.1 | MIDP-1.0 | - | 4 | 16 | 600 | 385924 | 0 | java/lang/ArithmeticException | 运行正常 |  |
| JBenchmark2.jar | 62 | JBenchmark2 | - | 2.1.1 | MIDP-2.0 | - | 6 | 16 | 600 | 9275566559 | 0 | java/lang/ArithmeticException,ja | 运行正常 |  |
| JBenchmark3D.jar | 80 | JBenchmark3D | - | 3.1.0 | MIDP-2.0 | m3g | 5 | 16 | 600 | 24600614 | 0 | - | 运行正常 |  |
| LOVExLOVE恋爱学院_4424.jar | 860 | LS | - | 01.30.00 | MIDP-2.0 | - | 75 | 16 | 600 | 559931 | 0 | - | 运行正常 |  |
| MinecraftJ2ME_PE_0.6.1_V2.2.jar | 1214 | MinecraftJ2ME_PE_0.6.1 | - | 0.6.1 | MIDP-2.0 | m3g,nokia-ui | 170 | 16 | 600 | 4662495823 | 0 | java/lang/StackOverflowError,jav | 运行正常 |  |
| NBA2010中文版_6247.jar | 849 | NBA 10（EA正版） | - | 1.0.0 | MIDP-2.0 | - | 35 | 16 | 779 | 7835414259 | 0 | javax/microedition/rms/InvalidRe | 运行正常 |  |
| NBA2010授权正版_5060.jar | 826 | NBA2010（授权正版） | - | 1.0.0 | MIDP-2.0 | - | 13 | 16 | 658 | 10404778 | 0 | - | 运行正常 |  |
| NBA篮球2007_10887.jar | 298 | NBA LIVE 07 | - | 4.2.23 | MIDP-2.0 | - | 86 | 16 | 1060 | 12843312 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| NBA职业篮球2010豪华版_7111.jar | 841 | NBA Pro Basketball 2010 | - | 1.0.8 | MIDP-2.0 | - | 9 | 16 | 600 | 4413361422 | 0 | java/lang/NullPointerException | 运行正常 |  |
| PC正版-使命召唤5_7199.jar | 407 | PC正版-使命召唤5 | - | 1.0.0 | MIDP-2.0 | - | 21 | 16 | 600 | 12736154 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| QQ飞车2010_5772.jar | 457 | QQ2010 | - | 1.0 | MIDP-2.0 | - | 30 | 16 | 600 | 3377751239 | 0 | java/io/EOFException | 运行正常 |  |
| Vime_en_ar_de_he-IL_ru-RU.jar | 62 | Virtual Me | - | 0.17 | MIDP-1.0 | nokia-ui | 9 | 16 | 600 | 838717 | 0 | javax/microedition/rms/InvalidRe | 运行正常 | runs without input; the generic key script m |
| book4.jar | 20 | J2ME Games Examples | - | 2.0 | MIDP-2.0 | - | 10 | 16 | 600 | 146 | 0 | - | 运行正常 | runs without input; the generic key script m |
| book5.jar | 29 | J2ME Games Examples | - | 2.0 | MIDP-2.0 | - | 14 | 16 | 600 | 150 | 0 | - | 运行正常 | runs without input; the generic key script m |
| book6.jar | 38 | J2ME Games Examples | - | 2.0 | MIDP-2.0 | - | 16 | 16 | 600 | 162 | 0 | - | 运行正常 | runs without input; the generic key script m |
| book7.jar | 39 | J2ME Games Examples | - | 2.0 | MIDP-2.0 | - | 17 | 16 | 600 | 162 | 0 | - | 运行正常 | runs without input; the generic key script m |
| bubblet.jar | 27 | Bubblet | - | 1.0.0 | - | nokia-ui | 14 | 16 | 600 | 930941 | 0 | - | 运行正常 |  |
| game15.jar | 3 | Game15MIDlet | - | 1.0.0 | - | - | 2 | 16 | 600 | 118407 | 0 | - | 运行正常 |  |
| hello.jar | 2 | Hello World | - | 2.0 | MIDP-2.0 | - | 2 | 16 | 600 | 9582 | 0 | - | 运行正常 |  |
| jump.jar | 15 | J2ME Games Examples | - | 2.0 | MIDP-2.0 | - | 7 | 16 | 600 | 138 | 0 | - | 运行正常 | runs without input; the generic key script m |
| maze.jar | 7 | J2ME Games Examples | - | 1.0 | MIDP-1.0 | - | 4 | 16 | 600 | 866629 | 0 | - | 运行正常 |  |
| slimevolley.jar | 8 | SlimeVolleyBallMidlet | - | 1.0.0 | - | - | 7 | 16 | 470 | 167128 | 0 | - | 运行正常 | exited itself (notifyDestroyed) |
| vault/(128x128) SolaRola 1.0.3 5716 v2.jar | 233 | SolaRola | - | 1.0.3 | MIDP-2.0 | - | 2 | 16 | 600 | 3766345 | 0 | javax/microedition/rms/InvalidRe | 运行正常 |  |
| vault/(128x128) SolaRola 1.0.3 5716.jar | 62 | SolaRola | - | 1.0.3 | MIDP-1.0 | nokia-ui | 2 | 16 | 600 | 788103 | 0 | javax/microedition/rms/InvalidRe | 运行正常 |  |
| vault/(128x128) SolaRola 1.0.3 5751.jar | 117 | SolaRola | - | 1.0.3 | MIDP-2.0 | nokia-ui | 2 | 16 | 600 | 1666076 | 0 | javax/microedition/rms/InvalidRe | 运行正常 |  |
| vault/(128x160) SolaRola 1.0.3 5716 v2.jar | 233 | SolaRola | - | 1.0.3 | MIDP-2.0 | - | 2 | 16 | 600 | 3773131 | 0 | javax/microedition/rms/InvalidRe | 运行正常 |  |
| vault/(128x160) SolaRola 1.0.3 5716.jar | 112 | SolaRola | - | 1.0.3 | MIDP-2.0 | nokia-ui | 2 | 16 | 600 | 2019906 | 0 | javax/microedition/rms/InvalidRe | 运行正常 |  |
| vault/(128x160) SolaRola 1.0.3 5745.jar | 98 | SolaRola | - | 1.0.3 | MIDP-2.0 | - | 2 | 16 | 600 | 1719912 | 0 | javax/microedition/rms/InvalidRe | 运行正常 |  |
| vault/(128x160) SolaRola 1.0.4 5856.jar | 232 | SolaRola | - | 1.0.4 | MIDP-2.0 | - | 2 | 16 | 600 | 2616067 | 0 | javax/microedition/rms/InvalidRe | 运行正常 |  |
| vault/(176x204) SolaRola 1.0.3 5733.jar | 169 | Sola Rola | - | 1.0.3 | MIDP-2.0 | - | 2 | 16 | 600 | 824268 | 0 | javax/microedition/rms/InvalidRe | 运行正常 |  |
| vault/(176x208) SolaRola 1.0.3 5716.jar | 417 | SolaRola | - | 1.0.3 | MIDP-2.0 | nokia-ui | 2 | 16 | 600 | 3900366 | 0 | javax/microedition/rms/InvalidRe | 运行正常 |  |
| vault/(176x208) SolaRola 1.0.3 5725 v2.jar | 238 | SolaRola | - | 1.0.3 | MIDP-2.0 | nokia-ui | 2 | 16 | 600 | 3618373 | 0 | javax/microedition/rms/InvalidRe | 运行正常 |  |
| vault/(176x208) SolaRola 1.0.3 5725.jar | 228 | SolaRola | - | 1.0.3 | MIDP-2.0 | nokia-ui | 2 | 16 | 600 | 2406192 | 0 | javax/microedition/rms/InvalidRe | 运行正常 |  |
| vault/(176x208) SolaRola 1.0.4 5831.jar | 201 | SolaRola | - | 1.0.4 | MIDP-1.0 | nokia-ui | 2 | 16 | 600 | 3522037 | 0 | javax/microedition/rms/InvalidRe | 运行正常 |  |
| vault/(176x220) SolaRola 1.0.3 5716 v2.jar | 417 | SolaRola | - | 1.0.3 | MIDP-2.0 | - | 2 | 16 | 600 | 4073383 | 0 | javax/microedition/rms/InvalidRe | 运行正常 |  |
| vault/(176x220) SolaRola 1.0.3 5716.jar | 237 | SolaRola | - | 1.0.3 | MIDP-2.0 | - | 2 | 16 | 600 | 3900859 | 0 | javax/microedition/rms/InvalidRe | 运行正常 |  |
| vault/(176x220) SolaRola 1.0.4 5733.jar | 237 | SolaRola | - | 1.0.3 | MIDP-2.0 | - | 2 | 16 | 600 | 2497978 | 0 | javax/microedition/rms/InvalidRe | 运行正常 |  |
| vault/(176x220) SolaRola 1.0.4 5814.jar | 163 | SolaRola | - | 1.0.4 | MIDP-2.0 | - | 2 | 16 | 600 | 2945521 | 0 | javax/microedition/rms/InvalidRe | 运行正常 |  |
| vault/(208x208) SolaRola 1.0.3 5716.jar | 231 | SolaRola | - | 1.0.3 | MIDP-2.0 | - | 2 | 16 | 600 | 3634853 | 0 | javax/microedition/rms/InvalidRe | 运行正常 |  |
| vault/(240x320) SolaRola 1.0.3 5716 v2.jar | 237 | SolaRola | - | 1.0.3 | MIDP-2.0 | - | 2 | 16 | 600 | 3549918 | 0 | javax/microedition/rms/InvalidRe | 运行正常 |  |
| vault/(240x320) SolaRola 1.0.3 5716.jar | 417 | SolaRola | - | 1.0.3 | MIDP-2.0 | - | 2 | 16 | 600 | 4247958 | 0 | javax/microedition/rms/InvalidRe | 运行正常 |  |
| vault/(240x320) SolaRola 1.0.3 5725 v2.jar | 231 | SolaRola | - | 1.0.3 | MIDP-2.0 | nokia-ui | 2 | 16 | 600 | 3946797 | 0 | javax/microedition/rms/InvalidRe | 运行正常 |  |
| vault/(240x320) SolaRola 1.0.3 5725 v3.jar | 418 | SolaRola | - | 1.0.3 | MIDP-2.0 | nokia-ui | 2 | 16 | 600 | 4088111 | 0 | javax/microedition/rms/InvalidRe | 运行正常 |  |
| vault/(240x320) SolaRola 1.0.3 5725.jar | 417 | SolaRola | - | 1.0.3 | MIDP-2.0 | nokia-ui | 2 | 16 | 600 | 4258954 | 0 | javax/microedition/rms/InvalidRe | 运行正常 |  |
| vault/(240x320) SolaRola 1.0.3 5764.jar | 234 | SolaRola | - | 1.0.3 | MIDP-2.0 | - | 2 | 16 | 600 | 2523113 | 0 | javax/microedition/rms/InvalidRe | 运行正常 |  |
| vault/(240x320) SolaRola 1.0.4 5888.jar | 417 | SolaRola | - | 1.0.4 | MIDP-2.0 | nokia-ui | 2 | 16 | 600 | 3638833 | 0 | javax/microedition/rms/InvalidRe | 运行正常 |  |
| vault/(320x240) SolaRola 1.0.3 5725.jar | 417 | SolaRola | - | 1.0.3 | MIDP-2.0 | nokia-ui | 2 | 16 | 600 | 3644728 | 0 | javax/microedition/rms/InvalidRe | 运行正常 |  |
| vault/(352x416) SolaRola 1.0.4 5831.jar | 417 | SolaRola | - | 1.0.4 | MIDP-2.0 | nokia-ui | 2 | 16 | 600 | 2932591 | 0 | javax/microedition/rms/InvalidRe | 运行正常 |  |
| vault/-ProbudPomeranc_K700.jar | 78 | Probud pomeranc | - | 1.0.0 | MIDP-1.0 | - | 12 | 16 | 600 | 1660707 | 0 | java/io/IOException,java/lang/Nu | 运行正常 |  |
| vault/000-Tomb Raider The Osiris Codex v1.0.9 (176x208) [English].jar | 134 | TombRaider1 | - | 1.0.9 | MIDP-1.0 | - | 5 | 16 | 600 | 1006636 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/001-Tomb Raider The Osiris Codex v1.1.3 (128x128) [German].jar | 63 | Tomb Raider The Osiris Codex | - | 1.1.3 | MIDP-1.0 | - | 6 | 16 | 600 | 3539767 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/002-Tomb Raider The Osiris Codex v1.1.3 (176x208) [German].jar | 135 | TombRaider1 | - | 1.1.3 | MIDP-1.0 | - | 5 | 16 | 600 | 466028 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/003-Tomb Raider The Osiris Codex v1.1.4 (176x208) [Italian].jar | 135 | TombRaider1 | - | 1.1.4 | MIDP-1.0 | - | 5 | 16 | 601 | 514540 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/004-Tomb Raider The Osiris Codex v1.1.8 (176x208) [Spanish].jar | 93 | TombRaider1 | - | 1.1.8 | MIDP-1.0 | - | 5 | 16 | 600 | 834858 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/005-Tomb Raider The Osiris Codex v1.2.5 (128x128) [Chinese A].jar | 58 | 地狱书上 | - | 1.2.5 | MIDP-1.0 | - | 6 | 16 | 600 | 2172757 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/006-Tomb Raider The Osiris Codex v1.2.5 (128x128) [Chinese B].jar | 58 | 地狱书下 | - | 1.2.5 | MIDP-1.0 | - | 6 | 16 | 600 | 2112289 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/007 - Quantum of Solace.jar | 584 | Quantum Of Solace | - | 1.0.6 | MIDP-1.0 | - | 7 | 16 | 600 | 62759612 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/007 Ice Racer.jar | 171 | IceRacer | - | 1.5 | MIDP-1.0 | nokia-ui | 16 | 16 | 600 | 389439 | 0 | java/lang/IllegalArgumentExcepti | 运行正常 |  |
| vault/007-Tomb Raider The Osiris Codex v1.5.6 (240x320) [English].jar | 118 | TombRaider1 | - | 1.5.6 | MIDP-1.0 | - | 6 | 16 | 601 | 208905 | 0 | - | 运行正常 |  |
| vault/007_Casino_Royale_Rus.jar | 213 | Casino Royale | - | 0.23.7 | MIDP-2.0 | - | 5 | 16 | 600 | 1178024 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/008-Tomb Raider Quest for Cinnabar v1.1.0 (176x208) [German].jar | 140 | TombRaider2 | - | 1.1.0 | MIDP-1.0 | - | 5 | 16 | 600 | 467069 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/009-Tomb Raider Quest for Cinnabar v1.1.1 (176x208) [English].jar | 137 | TombRaider2 | - | 1.1.1 | MIDP-1.0 | - | 5 | 16 | 600 | 446106 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/010-Tomb Raider Quest for Cinnabar v1.1.2 (176x208) [English].jar | 138 | TombRaider2 | - | 1.1.2 | MIDP-1.0 | - | 5 | 16 | 600 | 438786 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/011-Tomb Raider Quest for Cinnabar v1.1.3 (128x128) [Spanish].jar | 63 | TombRaider2 | - | 1.1.3 | MIDP-1.0 | - | 6 | 16 | 600 | 2609371 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/012-Tomb Raider Quest for Cinnabar v1.1.5 (128x128) [English].jar | 62 | TombRaider2 | - | 1.1.5 | MIDP-1.0 | - | 6 | 16 | 600 | 2598745 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/013-Tomb Raider Quest for Cinnabar v1.1.5 (128x128) [German].jar | 63 | Tomb Raider Quest For Cinnab | - | 1.1.5 | MIDP-1.0 | - | 6 | 16 | 600 | 2582486 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/014-Tomb Raider Quest for Cinnabar v1.1.5 (176x208) [Spanish].jar | 97 | TombRaider2 | - | 1.1.5 | MIDP-1.0 | - | 5 | 16 | 600 | 857231 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/015-Tomb Raider Elixir Of Life v1.0.3 (176x208) [English].jar | 131 | TombRaider3 | - | 1.0.3 | MIDP-1.0 | - | 5 | 16 | 601 | 450470 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/016-Tomb Raider Elixir Of Life v1.0.4 (128x128) [English].jar | 63 | TombRaider3 | - | 1.0.4 | MIDP-1.0 | - | 6 | 16 | 600 | 2670375 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/017-Tomb Raider Elixir Of Life v1.0.6 (176x208) [German].jar | 91 | TombRaider3 | - | 1.0.6 | MIDP-1.0 | - | 5 | 16 | 600 | 816040 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/018-Tomb Raider Elixir Of Life v1.0.8 (128x128) [German].jar | 63 | Tomb Raider Elixir Of Life | - | 1.0.8 | MIDP-1.0 | - | 6 | 16 | 600 | 2602095 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/019-Tomb Raider Elixir Of Life v1.1.4 (128x128) [Spanish].jar | 63 | TombRaider3 | - | 1.1.4 | MIDP-1.0 | - | 6 | 16 | 600 | 2620121 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/020-Tomb Raider Elixir Of Life v1.1.5 (176x208) [Italian].jar | 91 | TombRaider3 | - | 1.1.5 | MIDP-1.0 | - | 5 | 16 | 600 | 877802 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/021-Tomb Raider Elixir Of Life v1.1.5 (176x208) [Spanish].jar | 91 | TombRaider3 | - | 1.1.5 | MIDP-1.0 | - | 5 | 16 | 600 | 821299 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/022-Tomb Raider Elixir Of Life v1.1.9 (176x208) [Chinese].jar | 90 | TombRaider3 | - | 1.1.9 | MIDP-1.0 | - | 5 | 16 | 600 | 325827 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/023-Tomb Raider Elixir Of Life v1.2.1 (128x128) [Chinese A].jar | 62 | 掌星-古墓丽影3A | - | 1.2.1 | MIDP-1.0 | - | 8 | 16 | 600 | 2506441 | 0 | javax/microedition/rms/InvalidRe | 运行正常 |  |
| vault/024-Tomb Raider Elixir Of Life v1.2.1 (128x128) [Chinese B].jar | 62 | 掌星-古墓丽影3B | - | 1.2.1 | MIDP-1.0 | - | 8 | 16 | 600 | 2411053 | 0 | javax/microedition/rms/InvalidRe | 运行正常 |  |
| vault/025-Tomb Raider Slot v01.01.03 (CLDC-1.0, MIDP-2.0) (128x128).jar | 107 | SL Tomb Raider | - | 01.01.03 | MIDP-2.0 | - | 52 | 16 | 600 | 856659 | 0 | java/io/IOException | 运行正常 |  |
| vault/026-Tomb Raider Slot v01.01.03 (CLDC-1.0, MIDP-2.0) (176x208).jar | 155 | SL Tomb Raider | - | 01.01.03 | MIDP-2.0 | - | 52 | 16 | 600 | 861247 | 0 | java/io/IOException | 运行正常 |  |
| vault/027-Tomb Raider Slot v01.01.03 (CLDC-1.0, MIDP-2.0) (176x220).jar | 158 | SL Tomb Raider | - | 01.01.03 | MIDP-2.0 | - | 52 | 16 | 600 | 861289 | 0 | java/io/IOException | 运行正常 |  |
| vault/028-Tomb Raider Slot v01.01.03 (CLDC-1.0, MIDP-2.0) (240x240).jar | 181 | SL Tomb Raider | - | 01.01.03 | MIDP-2.0 | - | 52 | 16 | 600 | 863959 | 0 | java/io/IOException | 运行正常 |  |
| vault/029-Tomb Raider Slot v01.01.03 (CLDC-1.0, MIDP-2.0) (320x240).jar | 167 | SL Tomb Raider | - | 01.01.03 | MIDP-2.0 | - | 52 | 16 | 600 | 884453 | 0 | java/io/IOException | 运行正常 |  |
| vault/030-Tomb Raider Slot v01.01.03 (CLDC-1.0, MIDP-2.0) (360x640).jar | 241 | SL Tomb Raider | - | 01.01.03 | MIDP-2.0 | - | 52 | 16 | 600 | 730779 | 0 | java/io/IOException | 运行正常 |  |
| vault/031-Tomb Raider Slot v01.01.03 (Sony Ericsson) (240x320).jar | 188 | SL Tomb Raider | - | 01.01.03 | MIDP-2.0 | - | 52 | 16 | 600 | 864992 | 0 | java/io/IOException | 运行正常 |  |
| vault/032-Tomb Raider Slot v30.01 (CLDC-1.0, MIDP-2.0) (128x128).jar | 138 | Tomb Raider Slot | - | 30.01 | MIDP-2.0 | - | 46 | 16 | 600 | 66041 | 0 | - | 运行正常 |  |
| vault/033-Tomb Raider Slot v30.01 (CLDC-1.0, MIDP-2.0) (176x220).jar | 219 | Tomb Raider Slot | - | 30.01 | MIDP-2.0 | - | 44 | 16 | 600 | 66058 | 0 | - | 运行正常 |  |
| vault/034-Tomb Raider Slot v30.01 (CLDC-1.0, MIDP-2.0) (Nokia 128x160).jar | 144 | Tomb Raider Slot | - | 30.01 | MIDP-2.0 | - | 46 | 16 | 600 | 66257 | 0 | - | 运行正常 |  |
| vault/035-Tomb Raider Slot v30.01 (CLDC-1.0, MIDP-2.0) (Nokia 176x208).jar | 178 | Tomb Raider Slot | - | 30.01 | MIDP-2.0 | - | 46 | 16 | 600 | 66058 | 0 | - | 运行正常 |  |
| vault/036-Tomb Raider Slot v30.01 (CLDC-1.0, MIDP-2.0) (Samsung 240x320).jar | 225 | Tomb Raider Slot | - | 30.01 | MIDP-2.0 | - | 46 | 16 | 600 | 66128 | 0 | - | 运行正常 |  |
| vault/037-Tomb Raider Slot v30.02 (CLDC-1.0, MIDP-2.0) (Nokia s40v3a) (240x320).jar | 249 | Tomb Raider Slot | - | 30.02 | MIDP-2.0 | - | 45 | 16 | 600 | 64467 | 0 | - | 运行正常 |  |
| vault/038-Tomb Raider Slot v30.02 (CLDC-1.0, MIDP-2.0) (s40v2) (128x128).jar | 138 | Tomb Raider Slot | - | 30.02 | MIDP-2.0 | - | 45 | 16 | 600 | 63791 | 0 | - | 运行正常 |  |
| vault/039-Tomb Raider Slot v30.02 (CLDC-1.0, MIDP-2.0) (s40v2a) (128x160).jar | 144 | Tomb Raider Slot | - | 30.02 | MIDP-2.0 | - | 45 | 16 | 600 | 63592 | 0 | - | 运行正常 |  |
| vault/040-Tomb Raider Slot v30.02 (CLDC-1.0, MIDP-2.0) (s40v3) (208x208).jar | 192 | Tomb Raider Slot | - | 30.02 | MIDP-2.0 | - | 45 | 16 | 600 | 64236 | 0 | - | 运行正常 |  |
| vault/041-Tomb Raider Slot v30.02 (CLDC-1.1, MIDP-2.0) (s40v3a) (240x320).jar | 249 | Tomb Raider Slot | - | 30.02 | MIDP-2.0 | - | 45 | 16 | 600 | 64155 | 0 | - | 运行正常 |  |
| vault/042-Lara Crofts Poker Party (CLDC-1.0, MIDP-1.0) v1.0.1 (132x176).jar | 102 | Lara Crofts Poker Party | - | 1.0.1 | MIDP-1.0 | - | 15 | 16 | 600 | 593503 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/043-Tomb Raider Puzzle Paradox v0.4.1 (176x220).jar | 213 | Tomb Raider Puzzle Paradox | - | 0.4.1 | MIDP-2.0 | - | 2 | 16 | 600 | 7070677 | 0 | java/io/IOException,javax/microe | 运行正常 |  |
| vault/044-Tomb Raider Puzzle Paradox v0.4.1 (CLDC-1.1, MIDP-2.0) (Sony Ericsson) 176x220).jar | 213 | Tomb Raider Puzzle Paradox | - | 0.4.1 | MIDP-2.0 | - | 2 | 16 | 600 | 7070364 | 0 | java/io/IOException,javax/microe | 运行正常 |  |
| vault/045-Tomb Raider Puzzle Paradox v0.4.6 (CLDC-1.1, MIDP-2.0) (s40v3a) (240x320).jar | 315 | Tomb Raider Puzzle Paradox | - | 0.4.6 | MIDP-2.0 | - | 2 | 16 | 600 | 7711645 | 0 | java/io/IOException,javax/microe | 运行正常 |  |
| vault/046-Tomb Raider Puzzle Paradox v0.4.8 (CLDC-1.0, MIDP-2.0) (Nokia s60v2) (176x208).jar | 208 | Tomb Raider Puzzle Paradox | - | 0.4.8 | MIDP-2.0 | - | 2 | 16 | 600 | 6448812 | 0 | java/io/IOException,javax/microe | 运行正常 |  |
| vault/047-Tomb Raider Puzzle Paradox v0.6.7 (CLDC-1.0, MIDP-2.0) (Motorola) (176x220).jar | 139 | Tomb Raider Puzzle Paradox | - | 0.6.7 | MIDP-2.0 | - | 2 | 16 | 600 | 6714728 | 0 | java/io/IOException,javax/microe | 运行正常 |  |
| vault/048-Tomb Raider Puzzle Paradox v0.6.8 (CLDC-1.1, MIDP-2.0) (Nokia N73) (240x320).jar | 303 | Tomb Raider Puzzle Paradox | - | 0.6.8 | MIDP-2.0 | - | 2 | 16 | 600 | 8157532 | 0 | java/io/IOException,javax/microe | 运行正常 |  |
| vault/049-Tomb Raider Puzzle Paradox v0.7.0 (132x176).jar | 152 | Tomb Raider Puzzle Paradox | - | 0.7.0 | MIDP-2.0 | - | 2 | 16 | 600 | 5956948 | 0 | java/io/IOException,javax/microe | 运行正常 |  |
| vault/050-Tomb Raider Puzzle Paradox v0.7.3 (CLDC-1.1, MIDP-2.0) (Nokia s60v3) (352x416).jar | 330 | Tomb Raider Puzzle Paradox | - | 0.7.3 | MIDP-2.0 | - | 2 | 16 | 600 | 5229605 | 0 | java/io/IOException,java/lang/Nu | 运行正常 |  |
| vault/051-Tomb Raider Puzzle Paradox v0.7.6 (CLDC-1.0, MIDP-1.0) (N-Gage) (176x208).jar | 204 | Tomb Raider Puzzle Paradox | - | 0.7.6 | MIDP-1.0 | - | 2 | 16 | 600 | 6067763 | 0 | java/io/IOException,javax/microe | 运行正常 |  |
| vault/052-Tomb Raider Legend Tokyo v1.12.7 (CLDC-1.1, MIDP-2.0) (for JSR184 - 240x320 screens) [English].jar | 516 | Tomb Raider Legend | - | 1.12.7 | MIDP-2.0 | - | 21 | 16 | 600 | 1291012 | 0 | - | 运行正常 |  |
| vault/053-Tomb Raider Legend Tokyo v1.15.41 (CLDC-1.1, MIDP-2.0) (Nokia 6280-6131) (240x320) [English].jar | 515 | Tomb Raider Legend | - | 1.15.41 | MIDP-2.0 | - | 21 | 16 | 600 | 1252003 | 0 | - | 运行正常 |  |
| vault/054-Tomb Raider Legend Tokyo v1.16 (176x220) [English].jar | 401 | Tomb Raider Legend | - | 1.16 | MIDP-2.0 | - | 22 | 16 | 600 | 1310186 | 0 | - | 运行正常 |  |
| vault/055-Tomb Raider Legend Tokyo v1.17 (CLDC-1.1, MIDP-2.0) (Nokia N73) (240x320) [Italian].jar | 468 | Tomb Raider Legend | - | 1.17 | MIDP-2.0 | - | 22 | 16 | 600 | 1647294 | 0 | - | 运行正常 |  |
| vault/056-Tomb Raider Legend Tokyo v1.17.6 (CLDC-1.1, MIDP-2.0) (NOGUNSOUND - Nokia 6280) [Spanish].jar | 517 | Tomb Raider Legend | - | 1.17.6 | MIDP-2.0 | - | 21 | 16 | 600 | 1452921 | 0 | - | 运行正常 |  |
| vault/057-Tomb Raider Legend Tokyo v1.17.9 (CLDC-1.1, MIDP-2.0) (Nokia 6233) [Spanish].jar | 517 | Tomb Raider Legend | - | 1.17.9 | MIDP-2.0 | - | 21 | 16 | 600 | 1874801 | 0 | - | 运行正常 |  |
| vault/058-Tomb Raider Anniversary v0.2.1 (128x128).jar | 63 | TombRaiderAnniversary | - | 0.2.1 | MIDP-1.0 | - | 4 | 16 | 600 | 18306349 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/059-Tomb Raider Anniversary v0.2.1 (CLDC-1.0, MIDP-2.0) (Motorola) (176x220).jar | 129 | TombRaiderAnniversary | - | 0.2.1 | MIDP-2.0 | - | 4 | 16 | 600 | 12001959 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/060-Tomb Raider Anniversary v0.2.1 (CLDC-1.1, MIDP-2.0) (Nokia 6680) (176x208).jar | 177 | TombRaiderAnniversary | - | 0.2.1 | MIDP-2.0 | - | 4 | 16 | 600 | 60460 | 0 | java/lang/IllegalStateException, | 运行正常 |  |
| vault/0602supermario.jar | 51 | SuperMario_t610 | - | 1.0.1 | MIDP-1.0 | nokia-ui | 26 | 16 | 600 | 604585 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/0602tetris.jar | 83 | Tetris(R) | - | 5.01.26 | MIDP-1.0 | - | 9 | 16 | 600 | 6159515235 | 0 | java/lang/StringIndexOutOfBounds | 运行正常 |  |
| vault/061-Tomb Raider Anniversary v0.2.1 (CLDC-1.1, MIDP-2.0) (Nokia E61) (320x240).jar | 296 | TombRaiderAnniversary | - | 0.2.1 | MIDP-2.0 | - | 4 | 16 | 600 | 1318529 | 0 | java/lang/ArrayIndexOutOfBoundsE | 运行正常 |  |
| vault/062-Tomb Raider Anniversary v0.2.1 (CLDC-1.1, MIDP-2.0) (Nokia N80) (352x416).jar | 324 | TombRaiderAnniversary | - | 0.2.1 | MIDP-2.0 | - | 4 | 16 | 600 | 1195913 | 0 | java/lang/ArrayIndexOutOfBoundsE | 运行正常 |  |
| vault/063-Tomb Raider Anniversary v0.2.1 (CLDC-1.1, MIDP-2.0) (SE) (128x160).jar | 134 | TombRaiderAnniversary | - | 0.2.1 | MIDP-2.0 | - | 4 | 16 | 600 | 60251 | 0 | java/lang/IllegalStateException, | 运行正常 |  |
| vault/064-Tomb Raider Anniversary v0.2.2 (CLDC-1.1, MIDP-2.0) (SE) (128x128).jar | 138 | TombRaiderAnniversary | - | 0.2.2 | MIDP-2.0 | - | 4 | 16 | 600 | 60477 | 0 | java/lang/IllegalStateException, | 运行正常 |  |
| vault/065-Tomb Raider Anniversary v0.2.2 (CLDC-1.1, MIDP-2.0) (SE) (176x220).jar | 169 | TombRaiderAnniversary | - | 0.2.2 | MIDP-2.0 | - | 4 | 16 | 600 | 1083285 | 0 | java/lang/ArrayIndexOutOfBoundsE | 运行正常 |  |
| vault/066-Tomb Raider Anniversary v0.2.2 (CLDC-1.1, MIDP-2.0) (SE) (240x320).jar | 275 | TombRaiderAnniversary | - | 0.2.2 | MIDP-2.0 | - | 4 | 16 | 600 | 128611 | 0 | java/lang/IllegalStateException, | 运行正常 |  |
| vault/067-Tomb Raider Anniversary v0.2.3 (CLDC-1.0, MIDP-1.0) (Nokia s60v1 3650) (176x208).jar | 177 | TombRaiderAnniversary | - | 0.2.3 | MIDP-1.0 | - | 4 | 16 | 600 | 1196791 | 0 | java/lang/ArrayIndexOutOfBoundsE | 运行正常 |  |
| vault/068-Tomb Raider Anniversary v0.2.3 (CLDC-1.0, MIDP-1.0) (Nokia) (176x208).jar | 201 | Tomb Raider Anniversary | - | 0.2.3 | MIDP-1.0 | - | 4 | 16 | 600 | 1293160 | 0 | java/lang/ArrayIndexOutOfBoundsE | 运行正常 |  |
| vault/069-Tomb Raider Anniversary v0.2.3 (CLDC-1.0, MIDP-2.0) (Nokia 6600) (176x208).jar | 187 | TombRaiderAnniversary | - | 0.2.3 | MIDP-2.0 | - | 4 | 16 | 600 | 1175322 | 0 | java/lang/ArrayIndexOutOfBoundsE | 运行正常 |  |
| vault/070-Tomb Raider Anniversary v0.2.4 (CLDC-1.1, MIDP-2.0) (Motorola) (240x320).jar | 185 | TombRaiderAnniversary | - | 0.2.4 | MIDP-2.0 | - | 4 | 16 | 600 | 1036340 | 0 | java/lang/ArrayIndexOutOfBoundsE | 运行正常 |  |
| vault/071-Tomb Raider Anniversary v0.2.4 (CLDC-1.1, MIDP-2.0) (Nokia 6280-6131) (240x320).jar | 272 | TombRaiderAnniversary | - | 0.2.4 | MIDP-2.0 | - | 4 | 16 | 600 | 1389941 | 0 | java/lang/ArrayIndexOutOfBoundsE | 运行正常 |  |
| vault/072-Tomb Raider Anniversary v0.2.5 (CLDC-1.0, MIDP-2.0) (Motorola) (128x128).jar | 134 | TombRaiderAnniversary | - | 0.2.5 | MIDP-2.0 | - | 4 | 16 | 600 | 1006838 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/073-Tomb Raider Anniversary v0.2.5 (CLDC-1.0, MIDP-2.0) (Motorola) (128x160).jar | 130 | TombRaiderAnniversary | - | 0.2.5 | MIDP-2.0 | - | 4 | 16 | 600 | 12867824 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/074-Tomb Raider Anniversary v0.2.5 (CLDC-1.1, MIDP-2.0) (N73) (240x320).jar | 272 | TombRaiderAnniversary | - | 0.2.5 | MIDP-2.0 | - | 4 | 16 | 600 | 1377163 | 0 | java/lang/ArrayIndexOutOfBoundsE | 运行正常 |  |
| vault/075-Tomb Raider Anniversary v0.2.6 (CLDC-1.1, MIDP-2.0) (Nokia s40v3) (208x208).jar | 168 | TombRaiderAnniversary | - | 0.2.6 | MIDP-2.0 | - | 4 | 16 | 600 | 1263802 | 0 | java/lang/ArrayIndexOutOfBoundsE | 运行正常 |  |
| vault/076-Tomb Raider Anniversary v0.2.6 (s40v3) (208x208).jar | 168 | TombRaiderAnniversary | - | 0.2.6 | MIDP-2.0 | - | 4 | 16 | 600 | 1263260 | 0 | java/lang/ArrayIndexOutOfBoundsE | 运行正常 |  |
| vault/077-Tomb Raider Anniversary v0.3.1 (CLDC-1.1, MIDP-2.0) (Nokia N91) (176x208).jar | 169 | TombRaiderAnniversary | - | 0.3.1 | MIDP-2.0 | - | 4 | 16 | 600 | 60310 | 0 | java/lang/IllegalStateException, | 运行正常 |  |
| vault/078-Tomb Raider Anniversary v0.3.2 (CLDC-1.0, MIDP-2.0) (s40v2) (128x128).jar | 87 | TombRaiderAnniversary | - | 0.3.2 | MIDP-2.0 | - | 4 | 16 | 600 | 1747704 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/079-Tomb Raider Anniversary v1.0.0 (CLDC-1.0, MIDP-2.0) (s40v2a) (128x160).jar | 111 | TombRaiderAnniversary | - | 1.0.0 | MIDP-2.0 | - | 4 | 16 | 600 | 362696452 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/080-Tomb Raider Anniversary v1.0.0 (CLDC-1.1, MIDP-2.0) (Nokia 6280) (240x320).jar | 271 | TombRaiderAnniversary | - | 1.0.0 | MIDP-2.0 | - | 4 | 16 | 600 | 139278 | 0 | java/lang/IllegalStateException, | 运行正常 |  |
| vault/081-Tomb Raider Underworld 2D v1.3.8 (CLDC-1.0, MIDP-1.0) (s40v2) (128x128).jar | 126 | TR Underworld | - | 1.3.8 | MIDP-1.0 | - | 3 | 16 | 600 | 216992 | 0 | java/io/IOException,java/lang/Ar | 运行正常 |  |
| vault/082-Tomb Raider Underworld 2D v1.3.8 (CLDC-1.0, MIDP-1.0) (s60v2-N6600) (176x208).jar | 511 | TR Underworld | - | 1.3.8 | MIDP-1.0 | - | 6 | 16 | 600 | 370247 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/083-Tomb Raider Underworld 2D v1.4.0 (CLDC-1.0, MIDP-1.0) (Nokia 3250) (176x208).jar | 511 | TR Underworld | - | 1.4.0 | MIDP-1.0 | - | 6 | 16 | 600 | 371747 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/084-Tomb Raider Underworld 2D v1.4.0 (CLDC-1.0, MIDP-1.0) (Samsung SGH-E250i) (128x160).jar | 263 | TR Underworld | - | 1.4.0 | MIDP-1.0 | - | 4 | 16 | 600 | 348301 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/085-Tomb Raider Underworld 2D v1.4.0 (CLDC-1.0, MIDP-1.0) (Samsung SGH-F480) (240x320).jar | 571 | TR Underworld | - | 1.4.0 | MIDP-1.0 | - | 6 | 16 | 600 | 380040 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/086-Tomb Raider Underworld 2D v1.4.0 (CLDC-1.0, MIDP-1.0) (Sony Ericsson W810) (176x220).jar | 524 | TR Underworld | - | 1.4.0 | MIDP-1.0 | - | 6 | 16 | 600 | 378339 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/087-Tomb Raider Underworld 2D v1.4.0 (CLDC-1.0, MIDP-1.0) (s60v2-N6680) (176x208).jar | 488 | TR Underworld | - | 1.4.0 | MIDP-1.0 | - | 6 | 16 | 600 | 364910 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/088-Tomb Raider Underworld 2D v1.4.1 (Benqsie EL71) (240x320).jar | 544 | TR Underworld | - | 1.4.1 | MIDP-1.0 | nokia-ui | 15 | 16 | 600 | 371374 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/089-Tomb Raider Underworld 2D v1.4.1 (CLDC-1.0, MIDP-1.0) (Nokia N95) (240x320).jar | 539 | TR Underworld | - | 1.4.1 | MIDP-1.0 | - | 7 | 16 | 600 | 370674 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/09 Formula Extreme.jar | 275 | Formula Extreme 09 (Bluetoot | - | 1.0.1 | MIDP-2.0 | - | 6 | 16 | 600 | 1237121 | 0 | java/io/IOException,java/lang/Nu | 运行正常 |  |
| vault/090-Tomb Raider Underworld 2D v1.4.1 (CLDC-1.0, MIDP-1.0) (s40v3a) (240x320).jar | 539 | TR Underworld | - | 1.4.1 | MIDP-1.0 | - | 7 | 16 | 600 | 369946 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/091-Tomb Raider Underworld 2D v1.4.1 (CLDC-1.0, MIDP-1.0) (s40v5) (240x320).jar | 539 | TR Underworld | - | 1.4.1 | MIDP-1.0 | - | 7 | 16 | 600 | 370842 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/092-Tomb Raider Underworld 2D v1.4.2 (CLDC-1.0, MIDP-1.0) (s40v3) (208x208).jar | 293 | TR Underworld | - | 1.4.2 | MIDP-1.0 | - | 7 | 16 | 600 | 353766 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/093-Tomb Raider Underworld 2D v1.4.5 (CLDC-1.0, MIDP-1.0) (s40v2a-N5200) (128x160).jar | 238 | TR Underworld | - | 1.4.5 | MIDP-1.0 | - | 3 | 16 | 600 | 342660 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/094-Tomb Raider Underworld 2D v1.4.5 (CLDC-1.0, MIDP-1.0) (s40v2a-N6101) (128x160).jar | 124 | TR Underworld | - | 1.4.5 | MIDP-1.0 | - | 3 | 16 | 600 | 230495 | 0 | java/io/IOException,java/lang/Ar | 运行正常 |  |
| vault/095-Tomb Raider Underworld 2D v1.4.5 (CLDC-1.0, MIDP-1.0) (s40v2a-N6151) (128x160).jar | 238 | TR Underworld | - | 1.4.5 | MIDP-1.0 | - | 3 | 16 | 600 | 340980 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/096-Tomb Raider Underworld 2D v1.4.6 (CLDC-1.0, MIDP-1.0) (Sony Ericsson K510) (128x160).jar | 264 | TR Underworld | - | 1.4.6 | MIDP-1.0 | - | 4 | 16 | 600 | 343403 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/097-Tomb Raider Underworld 3D v1.7.0 (CLDC-1.0, MIDP-1.0) (Nokia N92) (240x320).jar | 1131 | TR Underworld | - | 1.7.0 | MIDP-1.0 | - | 7 | 16 | 600 | 13289772 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/098-Tomb Raider Underworld 3D v1.7.1 (CLDC-1.0, MIDP-1.0) (Nokia N73) (240x320).jar | 1081 | TR Underworld | - | 1.7.1 | MIDP-1.0 | - | 7 | 16 | 600 | 12859520 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/099-Tomb Raider Underworld 3D v1.7.1 (CLDC-1.0, MIDP-1.0) (Nokia N80) (352x416).jar | 1139 | TR Underworld | - | 1.7.1 | MIDP-1.0 | - | 7 | 16 | 600 | 13258027 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/09_Formula_Extreme_(Gameleons_2008)_n70 - Mastiwap.Com.jar | 208 | 09 Formula Extreme | - | 1.0.1 | MIDP-2.0 | nokia-ui | 5 | 16 | 600 | 7659604 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/1 On 1 Basket.jar | 173 | 1 On 1 Basket | - | 1.0.16 | MIDP-1.0 | - | 3 | 16 | 600 | 134324042 | 0 | - | 运行正常 |  |
| vault/100-Tomb Raider Underworld 3D v1.7.1 (CLDC-1.0, MIDP-1.0) (Nokia N96) (240x320).jar | 1081 | TR Underworld | - | 1.7.1 | MIDP-1.0 | - | 7 | 16 | 600 | 12858162 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/101-Tomb Raider Underworld 3D v1.7.1 (CLDC-1.0, MIDP-1.0) (s60v2-N70) (176x208).jar | 1007 | TR Underworld | - | 1.7.1 | MIDP-1.0 | - | 7 | 16 | 600 | 13217797 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/102-Tomb Raider Underworld 3D v1.7.2 (176x208).jar | 1069 | TR Underworld | - | 1.7.2 | MIDP-1.0 | - | 8 | 16 | 600 | 12786370 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/103-Tomb Raider Underworld 3D v1.7.2 (320x240).jar | 1073 | TR Underworld | - | 1.7.2 | MIDP-1.0 | - | 7 | 16 | 600 | 13499048 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/104-Tomb Raider Underworld 3D v1.7.3 (CLDC-1.0, MIDP-1.0) (Sony Ericsson K810) (240x320).jar | 1145 | TR Underworld | - | 1.7.3 | MIDP-1.0 | - | 7 | 16 | 600 | 12994739 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/105-Tomb Raider Underworld 3D v1.8.8 (CLDC-1.0, MIDP-1.0) (Nokia 5800) (360x640).jar | 1304 | TR Underworld | - | 1.8.8 | MIDP-1.0 | - | 7 | 16 | 600 | 13853244 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/1100 A.D. - Battle of Legends.jar | 390 | 1100 AD. Battle of Legends | - | 1.0.0 | MIDP-2.0 | - | 43 | 16 | 600 | 937055 | 0 | - | 运行正常 |  |
| vault/1177613020_taxi_tycoon.jar | 179 | TaxiTycoon by Dimonvideo.ru | - | 1.0.0 | MIDP-2.0 | nokia-ui | 6 | 16 | 600 | 187059 | 0 | java/lang/ClassCastException | 运行正常 | runs without input; the generic key script t |
| vault/1182423485_snowrallycanada.jar | 1015 | S.R. Canada | - | 1.1 | MIDP-2.0 | m3g | 88 | 16 | 600 | 5614518 | 0 | javax/microedition/rms/RecordSto | 运行正常 | runs without input; the generic key script m |
| vault/1182526113_incastory_nokia_60_gkataria.jar | 151 | IncaStory\gkataria | - | 1.0 | MIDP-2.0 | - | 4 | 16 | 600 | 3431453843 | 0 | - | 运行正常 |  |
| vault/1183650031_tonyhawksdownhilljam_sonyericssonk790.jar | 1332 | Tony Hawk's Downhill Jam | - | 1.0.2 | MIDP-2.0 | mascot | 109 | 16 | 600 | 31815892 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/160190_AH4_RU_SerK750_.jar | 253 | Age of Heroes 4 | - | 1.1.0 | MIDP-2.0 | - | 3 | 16 | 600 | 394888 | 0 | java/lang/ArrayIndexOutOfBoundsE | 运行正常 |  |
| vault/1916 Dogfight.jar | 376 | 1916 Dogfight | - | 1.0.0 | MIDP-2.0 | - | 39 | 16 | 600 | 47128616 | 0 | java/io/EOFException,java/lang/N | 运行正常 |  |
| vault/1942.jar | 400 | 1942 | - | 1.0.0 | MIDP-2.0 | - | 18 | 16 | 600 | 13764818 | 0 | - | 运行正常 |  |
| vault/1943 Sky War.jar | 138 | 1943 SkyWar | - | 1.0 | MIDP-1.0 | - | 16 | 16 | 600 | 3045231 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/1_temple_flee_temple_158578.jar | 997 | 神庙逃亡 | - | 1.0.0 | MIDP-2.0 | - | 32 | 16 | 600 | 2031307 | 0 | java/io/IOException | 运行正常 |  |
| vault/2004 Real Football.jar | 189 | 2004 Real Football | - | 1.0.0 | MIDP-2.0 | - | 9 | 16 | 600 | 214675124 | 0 | - | 运行正常 |  |
| vault/2005 Real Football.jar | 201 | 2005RealFootball | - | 1.1.5 | MIDP-1.0 | - | 6 | 16 | 600 | 1861217 | 0 | - | 运行正常 |  |
| vault/2006 Real Football 3D.jar | 753 | 2006RealFootball3D | - | 1.0.7 | MIDP-2.0 | - | 7 | 16 | 602 | 2983997402 | 0 | java/lang/NumberFormatException | 运行正常 |  |
| vault/2006 Real Football.jar | 308 | 2006 Real Football | - | 1.1.0 | MIDP-2.0 | - | 6 | 16 | 600 | 17201678 | 0 | - | 运行正常 |  |
| vault/2010FIFAWorldCup_4.4.11_320x240.jar | 777 | 2010 FIFA World Cup | - | 4.4.11 | MIDP-2.0 | - | 31 | 16 | 600 | 8584921 | 0 | - | 运行正常 |  |
| vault/2012 Apocalypse.jar | 449 | 2012 Apocalypse | - | 1.0.0 | MIDP-1.0 | - | 48 | 16 | 600 | 19279792 | 0 | - | 运行正常 |  |
| vault/22521-The-Simpsons---Minutes-to-Meltdown-CZ.jar | 278 | The Simpsons: Minutes to Mel | - | 4.1.60 | MIDP-2.0 | - | 10 | 16 | 600 | 2464953 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/24 - Jack Bauer.jar | 591 | 24 Jack Bauer | - | 2.0.5 | MIDP-2.0 | - | 20 | 16 | 600 | 11657663 | 0 | java/io/EOFException,javax/micro | 运行正常 |  |
| vault/24 - Special Ops.jar | 302 | 24 Special Ops | - | 1.0.28 | MIDP-2.0 | - | 13 | 16 | 600 | 14303970 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/24 Agent Down.jar | 214 | 24 - Agent Down | - | 0.4.6 | MIDP-2.0 | - | 23 | 16 | 600 | 1723073 | 0 | java/io/IOException,java/lang/Nu | 运行正常 |  |
| vault/24 unite speciale-176x220.jar | 232 | - | - | - | - | - | 0 | 16 | 600 | 20922891 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/24-Agent_Down-j2me-samsung-(240x320) - Mastiwap.Com.jar | 215 | 24 - Agent Down | - | 0.4.7 | MIDP-2.0 | - | 24 | 16 | 600 | 1775372 | 0 | java/io/IOException,java/lang/Nu | 运行正常 |  |
| vault/240x320-deliver-xpress.jar | 242 | DeliverXpress | - | 1.0 | MIDP-1.0 | nokia-ui | 18 | 16 | 600 | 9759684 | 0 | - | 运行正常 |  |
| vault/244913_OffRoadDirtMotocross_Nokia6280_EN_IGP_102_s-c.ru.jar | 529 | Off-Road Dirt Motocross by D | - | 1.0.2 | MIDP-2.0 | - | 11 | 16 | 600 | 5775632950 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/24_Special_Ops_S40_2ed_6230i_8800.jar | 211 | 24 Special Ops | - | 1.0.31 | MIDP-2.0 | nokia-ui | 12 | 16 | 600 | 17659834 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/24_tm-Agent_Down_240x320.jar | 214 | 24 - Agent Down | - | 0.4.6 | MIDP-2.0 | - | 23 | 16 | 600 | 1699314 | 0 | java/io/IOException,java/lang/Nu | 运行正常 |  |
| vault/24thegame_8o3452rp.jar | 177 | 24 by mtsDesign | - | 0.4.0 | MIDP-2.0 | - | 24 | 16 | 600 | 307376 | 0 | javax/microedition/rms/InvalidRe | 运行正常 |  |
| vault/260115_DiamondTwister_S40V3_240x320_s-c.ru.jar | 358 | Diamond Twister | - | 1.0.8 | MIDP-2.0 | - | 7 | 32 | 600 | 2190532925 | 0 | java/lang/NullPointerException,j | 运行正常 | runs without input; the generic key script t |
| vault/2890_Stalker_Last_Wi.jar | 1712 | STALKER 3D Last Wish | - | 1.0 | MIDP-2.0 | - | 54 | 16 | 600 | 120801205 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/2933_horror.jar | 661 | Hysteric by -Spirit- | - | 1.0 | MIDP-2.0 | - | 54 | 16 | 600 | 110625220 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/2972_Dead_Shock.jar | 544 | Dead Shock | - | 0.1.78 | MIDP-2.0 | - | 82 | 16 | 600 | 460894371 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/2fast2furious_sek700.wz.cz.jar | 67 | 2Fast2Furious | - | 1.0.0 | MIDP-1.0 | nokia-ui | 19 | 16 | 600 | 46379474 | 0 | - | 运行正常 |  |
| vault/2in1MahjongGames_1.0.0_320x240.jar | 446 | 2in1 Mahjong Games | - | 2.0 | MIDP-2.0 | - | 65 | 16 | 600 | 4519721331 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/2x1 Quest Pack 240x320.jar | 433 | 2-4-1 Quest Pack | - | 1.0.2 | MIDP-2.0 | - | 28 | 16 | 600 | 3952735 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/3 In 1 sheep.jar | 217 | iGame 3in1Sheep | - | 1.0 | MIDP-2.0 | - | 9 | 16 | 600 | 984203 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/3029_Death_Night.jar | 930 | Death Night | - | 1.0 | MIDP-2.0 | - | 5 | 16 | 600 | 558948791 | 0 | - | 运行正常 |  |
| vault/3050_Dead_Space.jar | 1260 | Dead Space Mobile 3D | - | 1.0 | MIDP-2.0 | - | 54 | 16 | 600 | 115378702 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/3067_Horrors_new.jar | 1709 | Horrors | - | 1.0 | MIDP-2.0 | - | 54 | 16 | 600 | 308383310 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/30seconds.jar | 55 | 30Seconds | - | 1.0 | MIDP-1.0 | - | 22 | 16 | 600 | 21574 | 0 | - | 运行正常 | runs without input; the generic key script m |
| vault/3161_Silent_Evil_201.jar | 2607 | Silent Evil BETA | - | 1.5 | MIDP-2.0 | - | 68 | 16 | 600 | 3141827938 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/3210_Meat2Eat_II.jar | 422 | Резня 3D.Часть 2 | - | 1.0 | MIDP-2.0 | - | 55 | 16 | 600 | 194251221 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/3220_Raccoon_City.jar | 1515 | RE:Racoon City | - | 1.0.3 | MIDP-2.0 | - | 14 | 16 | 601 | 4793638 | 0 | java/lang/ArrayIndexOutOfBoundsE | 运行正常 |  |
| vault/3235_Into_The_Dead.jar | 1141 | Into the Dead | - | 1.0.0 | MIDP-2.0 | - | 10 | 16 | 601 | 2849756 | 0 | java/lang/NumberFormatException | 运行正常 |  |
| vault/3259_Not_Alone.jar | 286 | Not alone | - | 1.0 | MIDP-2.0 | - | 13 | 16 | 600 | 6649763 | 0 | - | 运行正常 |  |
| vault/3267_Manhunt.jar | 714 | Manhunt: Prison Massacre | - | 1.0 | MIDP-1.0 | - | 5 | 16 | 600 | 183205 | 0 | java/lang/NullPointerException,j | 运行正常 | runs without input; the generic key script m |
| vault/365 Solitaire Gold 12in1-Lite.jar | 217 | 365 Solitaire Gold | - | 0.0.1 | MIDP-2.0 | - | 19 | 16 | 600 | 29769627 | 0 | java/lang/IllegalArgumentExcepti | 运行正常 |  |
| vault/365PuzzleClub.jar | 371 | 365 Puzzle Club | - | 1.5.0 | MIDP-2.0 | - | 9 | 16 | 600 | 16076220 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/365PuzzleClub2.jar | 456 | 365 Puzzle Club | - | 0.0.6 | MIDP-2.0 | - | 8 | 16 | 600 | 24086883 | 0 | - | 运行正常 |  |
| vault/3D Alpha Wing EX.jar | 207 | 沙罗蔓蛇EX | - | 1.0.9 | MIDP-1.0 | - | 16 | 16 | 600 | 1026920 | 0 | java/lang/IllegalArgumentExcepti | 运行正常 |  |
| vault/3D Archery.jar | 221 | 3D Archery | - | 1.0 | MIDP-2.0 | m3g | 12 | 16 | 470 | 365584 | 0 | javax/microedition/rms/RecordSto | 运行正常 | exited itself (notifyDestroyed) |
| vault/3D Beach Ping Pong.jar | 208 | Ping Pong | - | 1.0.8 | MIDP-2.0 | m3g | 18 | 16 | 600 | 1251057 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/3D Bomberman Atomic.jar | 995 | Bomberman 3D | - | 1.0.3 | MIDP-2.0 | - | 36 | 16 | 600 | 482359 | 0 | java/io/EOFException,javax/micro | 运行正常 |  |
| vault/3D Fast and Furious - The Movie.jar | 930 | 3D Fast and Furious The Movi | - | 1.0.7 | MIDP-2.0 | - | 57 | 16 | 600 | 2075400 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/3D Fomula Extreme.jar | 1206 | 3DFormulaExtreeme | - | 1.0.0 | MIDP-2.0 | - | 118 | 16 | 600 | 5649290 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/3D Marble Madness.jar | 545 | Marble Madness | - | 4.10.10 | MIDP-2.0 | - | 34 | 16 | 600 | 2855380 | 0 | javax/microedition/rms/InvalidRe | 运行正常 |  |
| vault/3D Need for Drifting.jar | 312 | 3DNeedForDrift | - | 1.0 | MIDP-2.0 | - | 12 | 16 | 600 | 766746 | 0 | javax/microedition/rms/InvalidRe | 运行正常 |  |
| vault/3D Planet Riders.jar | 1278 | PlanetRiders | - | 1.2.3 | MIDP-2.0 | - | 116 | 16 | 600 | 5525126 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/3D Pool.jar | 95 | 3D Pool | - | 1.2.0 | MIDP-1.0 | - | 3 | 16 | 600 | 179021 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/3D Rally Pro Contest.jar | 200 | RallyProContest | - | 1.1 | MIDP-1.0 | - | 20 | 16 | 600 | 3023713 | 0 | javax/microedition/rms/InvalidRe | 运行正常 |  |
| vault/3D Rollercoaster Rush.jar | 282 | 3D Coaster Rush | - | 1.1.9 | MIDP-2.0 | - | 19 | 16 | 600 | 1386400 | 0 | java/io/EOFException,java/lang/N | 运行正常 |  |
| vault/3D Solid Weapon 2.jar | 529 | 3D Solid Weapon 2 | - | 1.0.1 | MIDP-2.0 | - | 64 | 16 | 600 | 14424209 | 0 | - | 运行正常 |  |
| vault/3D Solid Weapon.jar | 335 | 3D Solid Weapon - Ballacker | - | 1.0 | MIDP-2.0 | - | 36 | 16 | 600 | 365361 | 0 | - | 运行正常 |  |
| vault/3D Street Rail Racing.jar | 589 | 3D Street Rail Racing | - | 1.0.22 | MIDP-2.0 | - | 89 | 16 | 600 | 4307255 | 0 | - | 运行正常 |  |
| vault/3D Super Ball.jar | 221 | 3D SuperBall | - | 1.1.4 | MIDP-2.0 | - | 34 | 16 | 600 | 4711664 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/3DBrickBreakerRevolution.jar | 403 | 3D Brick Breaker | - | 1.0.0 | MIDP-2.0 | m3g | 21 | 16 | 600 | 46986990 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/3DBrickBreakerRevolution2.jar | 578 | 3DBrickBreak2 by BerON | - | 1.0.3 | MIDP-2.0 | m3g | 29 | 16 | 600 | 55279940 | 0 | java/io/EOFException,javax/micro | 运行正常 |  |
| vault/3DBrickBreakerRevolution_1.0.0_320x240.jar | 403 | 3D Brick Breaker | - | 1.0.0 | MIDP-2.0 | m3g,nokia-ui | 21 | 16 | 600 | 49275219 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/3DKamasutra.jar | 207 | 3D Kamasutra Trivia | - | 1.1.3 | MIDP-2.0 | m3g | 13 | 16 | 600 | 2190038 | 0 | - | 运行正常 |  |
| vault/3DPingPong.jar | 210 | Ping Pong | - | 1.0.0 | MIDP-2.0 | m3g,nokia-ui | 27 | 16 | 600 | 1129487 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/3D_Autobahn_Raser_World_Challenge_S40_2ed_6230i_8800_208x208.jar | 215 | 3D Autobahn Raser WC | - | 1.1.0 | MIDP-1.0 | m3g,nokia-ui | 34 | 16 | 600 | 3167971 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/3D_Autobahn_Raser_World_Challenge_S60_5F3ed_N80_E60_352x416.jar | 225 | 3D Autobahn Raser WC | - | 1.1.0 | MIDP-1.0 | m3g,nokia-ui | 34 | 16 | 600 | 369112509 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/3D_Beach_Mini_Golf.jar | 215 | 3D Beach Mini Golf | - | 1.4.01 | MIDP-1.0 | m3g | 55 | 16 | 600 | 343026 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/3D_Superball_(IndiaGames).jar | 221 | 3D SuperBall | - | 1.1.4 | MIDP-2.0 | m3g | 34 | 16 | 600 | 4061639 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/3D_Tetris.jar | 64 | 3DTetris | - | 1.0 | MIDP-2.0 | - | 54 | 16 | 600 | 10206262 | 0 | - | 运行正常 |  |
| vault/3G.Entertainment.-.Manga.Babes.Trisha's.Seducing.Dance.SE.176x220 - Mastiwap.Com.jar | 211 | Trisha | - | 1.0.0 | MIDP-1.0 | nokia-ui | 6 | 16 | 470 | 335997 | 0 | - | 运行正常 | exited itself (notifyDestroyed) |
| vault/3StyleSnowboard_N70 - Mastiwap.Com.jar | 211 | 3Style Snowboarding | - | 1.0.1 | MIDP-2.0 | nokia-ui | 10 | 16 | 600 | 6884902 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/3d_brick_breaker_revolution_360x640_n97-296782.jar | 420 | 3D Brick Breaker | - | 1.0.0 | MIDP-2.0 | m3g,nokia-ui | 21 | 16 | 600 | 28339072 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/3d_extremeairsnowboarding.jar | 240 | ExtremeAirSnowboarding3D | - | 0.0.1 | MIDP-1.0 | mascot | 25 | 16 | 600 | 3635025 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/3d_tiger_woods_2006.jar | 229 | Tiger Woods PGA TOUR 06 | - | 1.2.9 | MIDP-1.0 | - | 53 | 16 | 601 | 9860559 | 0 | java/lang/RuntimeException,javax | 运行正常 |  |
| vault/3dbrickbre_lcdvclkl.jar | 566 | 3DBrickBreak2 by Stox | - | 1.0.4 | MIDP-2.0 | m3g | 29 | 16 | 601 | 87331812 | 0 | java/io/EOFException,javax/micro | 运行正常 |  |
| vault/3kingdoms_sek700.ic.cz.jar | 61 | Three Kingdoms EN | - | 1.2 | MIDP-1.0 | nokia-ui | 23 | 16 | 600 | 778759 | 0 | - | 运行正常 |  |
| vault/3style_240_320.jar | 277 | 3Style Snowboarding | - | 1.0.0 | MIDP-2.0 | - | 10 | 16 | 600 | 8987373 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/3zs6gamworms2008 240x320.jar | 567 | Worms 2008 | - | 1.3.11 | MIDP-2.0 | - | 20 | 16 | 600 | 225805000 | 0 | java/lang/ArrayIndexOutOfBoundsE | 运行正常 |  |
| vault/4 Wheel Xtreme 3D.jar | 312 | 4 Wheel Xtreme 3D | - | 0.5.29 | MIDP-2.0 | m3g | 57 | 16 | 600 | 62682034 | 0 | java/io/IOException,java/lang/Cl | 运行正常 |  |
| vault/4 Wheel Xtreme.jar | 237 | 4 Wheel Xtreme | - | 0.7.0 | MIDP-2.0 | - | 19 | 16 | 600 | 1441541 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/4in1 Memory.jar | 213 | 4 in 1 | - | 1.0 | MIDP-2.0 | - | 6 | 16 | 600 | 838526 | 1 | java/io/EOFException,java/lang/N | 标题自身异常 | game code threw java.lang.NullPointerExcepti |
| vault/4x4 Extreme Offroad-Lite.jar | 214 | 4x4 Extreme OffRoad | - | 1.0.0 | MIDP-2.0 | - | 80 | 16 | 600 | 2283546761 | 0 | java/io/IOException,java/lang/Nu | 运行正常 |  |
| vault/4x4 Extreme Rally - World Tour.jar | 378 | 4x4 Extreme Rally World Tour | - | 1.0.2 | MIDP-2.0 | - | 9 | 16 | 600 | 614280 | 0 | java/io/IOException,java/lang/Nu | 运行正常 |  |
| vault/4x4 Extreme Rally 2006.jar | 154 | 4x4 Extreme Rally 2006 | - | 1.0.3 | MIDP-2.0 | - | 10 | 16 | 600 | 10220652 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/4x4 Extreme Rally 3D.jar | 207 | 4x4 Extreme Rally 3D | - | 1.0.2 | MIDP-2.0 | - | 11 | 16 | 600 | 10052309 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/4x4ExtremeRally3D - Mastiwap.Com.jar | 216 | 4x4 Extreme Rally 3D | - | 1.0.2 | MIDP-2.0 | m3g,nokia-ui | 23 | 16 | 600 | 14369018 | 0 | java/io/IOException,java/lang/Nu | 运行正常 |  |
| vault/4x4_Extreme_Rally_3D_240x320 - Mastiwap.Com.jar | 207 | 4x4 Extreme Rally 3D | - | 1.0.2 | MIDP-2.0 | m3g,nokia-ui | 11 | 16 | 600 | 15207220 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/4x4_Extreme_Rally_3D_Samsung_240x320.jar | 207 | 4x4 Extreme Rally 3D | - | 1.0.2 | MIDP-2.0 | m3g,nokia-ui | 11 | 16 | 600 | 15227173 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/50cent_Get_Rich.jar | 221 | Get Rich or Die Tryin' | - | 1.0.6 | MIDP-1.0 | - | 8 | 16 | 600 | 962439 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/50sn100s_sek700.wz.cz.jar | 60 | 50sn100s | - | 1.0 | MIDP-1.0 | nokia-ui | 5 | 16 | 600 | 60534 | 0 | - | 运行正常 |  |
| vault/639_dead_space_3d.jar | 998 | Dead space 3D | - | 1.0.0 | MIDP-1.0 | - | 121 | 16 | 600 | 537428 | 0 | - | 运行正常 |  |
| vault/7 Wonders.jar | 309 | 7 Wonders | - | 1.0.9 | MIDP-2.0 | - | 4 | 16 | 600 | 769455347 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/7Zwerge.jar | 114 | 7Zwerge | - | 1.02 | MIDP-1.0 | nokia-ui | 10 | 16 | 600 | 12797473 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/80sPack80sPi N73.jar | 331 | 80's Bundle | - | 1.0.0 | MIDP-2.0 | m3g,nokia-ui | 8 | 16 | 600 | 471401371 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/A Dog's Adventure.jar | 264 | A Dog's Adventure | - | 1.0.4 | MIDP-2.0 | - | 9 | 16 | 600 | 30770 | 0 | - | 运行正常 |  |
| vault/A Good Day to Die Hard.jar | 1593 | A Good Day To Die Hard DEDOM | - | 1.1.1 | MIDP-2.0 | - | 9 | 16 | 600 | 165599089 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/A.M.A.-.IQ.Booster..SE.176x220 - Mastiwap.Com.jar | 204 | AMA IQ Booster | - | 1.0.0 | MIDP-2.0 | - | 15 | 16 | 600 | 11506421 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/A.T.V-Offroad_Fury.jar | 253 | ATV Offroad Fury | - | 1.0 | MIDP-1.0 | nokia-ui | 6 | 16 | 601 | 461747 | 0 | javax/microedition/rms/InvalidRe | 运行正常 |  |
| vault/AAFlak88.jar | 79 | AAFlak88 | - | 1.2 | MIDP-1.0 | nokia-ui | 13 | 16 | 600 | 2799613531 | 0 | java/lang/OutOfMemoryError,javax | 运行正常 |  |
| vault/AA_Flak_88.jar | 78 | AAFlak88 | - | 1.0 | MIDP-1.0 | nokia-ui | 14 | 16 | 600 | 2222683192 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/AD1066-WilliamTheConquerorCZ_s40v3 - Mastiwap.Com.jar | 225 | AD1066 | - | 1.0 | MIDP-2.0 | - | 4 | 16 | 600 | 50003983 | 0 | java/io/IOException,java/lang/Il | 运行正常 |  |
| vault/AD1066_1.0_320x240.jar | 324 | AD1066 | - | 1.0 | MIDP-2.0 | - | 42 | 16 | 600 | 24664541 | 0 | java/io/IOException,java/lang/Ar | 运行正常 |  |
| vault/AMA IQ BOOST.jar | 221 | AMA IQ Booster | - | 1.0.1 | MIDP-2.0 | - | 15 | 16 | 600 | 11369958 | 0 | - | 运行正常 |  |
| vault/AMA.IQ.Booster_s40v3 - Mastiwap.Com.jar | 218 | AMA IQ Booster | - | 1.0.6 | MIDP-1.0 | - | 21 | 16 | 600 | 7138128 | 0 | - | 运行正常 |  |
| vault/AMALoveTest_s40v3 - Mastiwap.Com.jar | 222 | AMA Love Test | - | 1.0.0 | MIDP-2.0 | - | 12 | 16 | 600 | 3537975 | 0 | - | 运行正常 |  |
| vault/AMALoveTriangleCZ_s40v3a - Mastiwap.Com.jar | 214 | AMA Milostny trojuhelnik | - | 2.6.1 | MIDP-2.0 | - | 21 | 16 | 600 | 1407892 | 0 | javax/microedition/rms/InvalidRe | 运行正常 |  |
| vault/AMAPerfectBodyDiet_s40v2a - Mastiwap.Com.jar | 210 | AMA Perfect Body Diet | - | 1.0.3 | MIDP-1.0 | nokia-ui | 20 | 16 | 600 | 3973466 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/AMA_IQ_Booster-j2me-samsung-(240x320) - Mastiwap.Com.jar | 222 | AMA IQ Booster | - | 1.0.2 | MIDP-2.0 | - | 15 | 16 | 600 | 11157621 | 0 | - | 运行正常 |  |
| vault/AMA_IQ_Booster_Samsung_240x320.jar | 220 | AMA IQ Booster | - | 1.0.0 | MIDP-2.0 | - | 15 | 16 | 600 | 11344892 | 0 | - | 运行正常 |  |
| vault/AMF_Xtreme_Bowling.jar | 221 | - | - | - | - | m3g | 44 | 16 | 0 | 0 | 0 | - | 非可用包 | not a usable MIDlet suite (bad JAR or no MID |
| vault/AND1 Streetball.jar | 298 | AND1 Street Basketball | - | 1.0.7 | MIDP-2.0 | - | 4 | 16 | 600 | 267309418 | 0 | - | 运行正常 |  |
| vault/AOEIIIAsianDynasties_1.0.0_320x240.jar | 610 | AOE III Asian Dynasties | - | 1.0.0 | MIDP-2.0 | - | 41 | 16 | 600 | 10713042 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/ASPHALT_K700_DE_EN_ES_FR_IT_101.jar | 199 | Asphalt Urban GT | - | 1.0.1 | MIDP-1.0 | nokia-ui | 5 | 16 | 600 | 1663039722 | 0 | javax/microedition/rms/InvalidRe | 运行正常 |  |
| vault/AaronsRockBlock2.jar | 78 | Rock Block | - | 1.02 | MIDP-1.0 | nokia-ui | 15 | 16 | 600 | 2732621 | 0 | - | 运行正常 |  |
| vault/Aberrance.jar | 92 | Aberrance | - | 1.0 | MIDP-1.0 | nokia-ui | 26 | 16 | 600 | 205106 | 0 | javax/microedition/rms/InvalidRe | 运行正常 |  |
| vault/Abouttimesanta_sek700.wz.cz.jar | 57 | AboutTimeSanta | - | 1.0 | MIDP-1.0 | nokia-ui | 5 | 16 | 600 | 2301976 | 0 | - | 运行正常 |  |
| vault/Abracadaball.jar | 581 | Abracadaball | - | 1.0.6 | MIDP-2.0 | - | 5 | 16 | 600 | 192824135 | 0 | java/lang/ArrayIndexOutOfBoundsE | 运行正常 |  |
| vault/Abracadaball_1.0.2_320x240.jar | 472 | Abracadaball | - | 1.0.2 | MIDP-2.0 | - | 5 | 16 | 600 | 25026471 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/Absolute Blockers Deluxe.jar | 99 | Blockers Deluxe | - | 1.1.15 | MIDP-2.0 | - | 19 | 16 | 600 | 483935 | 0 | java/lang/NumberFormatException, | 运行正常 |  |
| vault/AbsoluteSummerSports_s40v3 - Mastiwap.Com.jar | 208 | Absolute Summer Sports | - | 1.0.1 | MIDP-2.0 | - | 24 | 16 | 600 | 4734586 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/AbsoluteTrickshot_K700.jar | 62 | Absolute Trickshot [terjef] | - | 1.1.3 | MIDP-1.0 | nokia-ui | 5 | 16 | 600 | 1228480 | 0 | javax/microedition/media/MediaEx | 运行正常 |  |
| vault/Absolute_Puzzle-1.0.0.jar | 77 | Absolute Puzzle | - | 1.0.0 | MIDP-1.0 | - | 48 | 16 | 600 | 478975 | 0 | - | 运行正常 |  |
| vault/Absolute_Puzzle.jar | 99 | Absolute Puzzle | - | 1.0.0 | MIDP-1.0 | nokia-ui | 49 | 16 | 600 | 1005121 | 0 | - | 运行正常 |  |
| vault/Absolute_Summer_Sports_(176x208) - Mastiwap.Com.jar | 208 | Absolute Summer Sports | - | 1.0.1 | MIDP-2.0 | - | 24 | 16 | 600 | 4578234 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/Absolute_Summer_Sports_S60_5F3ed_5500.jar | 208 | Absolute Summer Sports | - | 1.0.1 | MIDP-2.0 | - | 24 | 16 | 600 | 4600500 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/Accorta Institute KeepIn Head.jar | 204 | Keepinhead | - | 1.6.5 | MIDP-1.0 | - | 18 | 16 | 600 | 2307965 | 0 | - | 运行正常 |  |
| vault/Ace Combat - Northern Wings.jar | 848 | Ace Combat | - | 1.0.0 | MIDP-2.0 | - | 8 | 16 | 600 | 11625921 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/AceOfSpades.jar | 60 | AceOfSpades | - | 1.1 | MIDP-2.0 | - | 24 | 16 | 600 | 57087 | 0 | java/lang/StringIndexOutOfBounds | 运行正常 |  |
| vault/Aces of the Luftwaffe 2.jar | 619 | Aces Of The Luftwaffe 2 | - | 1.0 | MIDP-2.0 | - | 17 | 16 | 600 | 2470024 | 0 | java/io/IOException,javax/microe | 运行正常 |  |
| vault/Aces of the Luftwaffe.jar | 444 | Aces Of The Luftwaffe | - | 1.0 | MIDP-2.0 | - | 6 | 16 | 600 | 1986863 | 0 | java/io/IOException,javax/microe | 运行正常 |  |
| vault/AcesOf1916_OrigK700.jar | 65 | AcesOf1916 | - | 1.0 | MIDP-1.0 | - | 8 | 16 | 600 | 255241 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/Activision Anthology.jar | 210 | ActivisionAnthology | - | 0.2.7 | MIDP-2.0 | nokia-ui | 11 | 16 | 600 | 18432331 | 0 | java/io/IOException,java/lang/Nu | 运行正常 |  |
| vault/ActivisionAnthology.jar | 173 | Activision Anthology  (by GD | - | 01.00.01 | MIDP-2.0 | - | 4 | 16 | 600 | 6669066 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/Advanced Brain Trainer 2__320x240.jar | 293 | Advanced Brain Trainer 2 | - | 00.00.29 | MIDP-2.0 | nokia-ui | 25 | 16 | 600 | 6344937 | 0 | java/lang/ArrayIndexOutOfBoundsE | 运行正常 |  |
| vault/Adventure Time - Heroes of OOO.jar | 320 | Adventure Time | - | 1.0.2 | MIDP-2.0 | - | 12 | 16 | 600 | 1624009 | 0 | - | 运行正常 |  |
| vault/Age of Ampire3 - Mastiwap.Com.jar | 208 | Age Of Empires III Mobile | - | 1.0.4 | MIDP-1.0 | - | 10 | 16 | 600 | 7441812 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/AgeOfEmpiresIIIENG_s40v3 - Mastiwap.Com.jar | 204 | Age Of Empires III Mobile | - | 1.0.1 | MIDP-1.0 | - | 13 | 16 | 600 | 71572425 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/AgeOfHeroes2 - Mastiwap.Com.jar | 224 | Age of Heroes 2 | - | 1.1.0 | MIDP-2.0 | - | 3 | 16 | 600 | 152707 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/AgeOfHeroes4SK_s40v2a - Mastiwap.Com.jar | 225 | Age of Heroes 4 SK | - | 1.1.1 | MIDP-2.0 | - | 3 | 16 | 600 | 373268 | 0 | java/lang/ArrayIndexOutOfBoundsE | 运行正常 |  |
| vault/AgeOfHeroesV-WarriorsWay_s40v2-N6230 - Mastiwap.Com.jar | 224 | AHV Warrior's Way | - | 1.1.0 | MIDP-1.0 | nokia-ui | 8 | 16 | 600 | 289420 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/Age_Of_Empires_3_S40_2ed_6230i_8800.jar | 204 | Age Of Empires III Mobile | - | 1.0.1 | MIDP-1.0 | - | 13 | 16 | 600 | 54972328 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/Age_of_Heroes_4 blood twilight.jar | 221 | Age of Heroes 4 | - | 1.1.0 | MIDP-1.0 | nokia-ui | 3 | 16 | 600 | 395357 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/Age_of_Heroes_4_blood twilight.jar | 224 | Age of Heroes 4 | - | 1.1.0 | MIDP-1.0 | nokia-ui | 3 | 16 | 600 | 409784 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/Agent To The Stars.jar | 221 | Mr10Percent | - | 1.0.0 | MIDP-1.0 | - | 12 | 16 | 600 | 3105980 | 0 | - | 运行正常 |  |
| vault/Air Race.jar | 224 | Air_race_US | - | 1.0 | MIDP-2.0 | - | 27 | 16 | 600 | 4018307 | 0 | java/io/IOException | 运行正常 |  |
| vault/Air Strike 1944 - Flight for Freedom.jar | 294 | Air Strike 1944 | - | 1.0.9 | MIDP-2.0 | - | 6 | 16 | 600 | 52088838 | 0 | - | 运行正常 |  |
| vault/AirBuster 3D.jar | 299 | AirBurster3d | - | 1.0.1 | MIDP-2.0 | - | 6 | 16 | 600 | 5564308 | 0 | java/io/IOException,java/lang/Ar | 运行正常 |  |
| vault/AirHockey_s40v3 - Mastiwap.Com.jar | 215 | OAAirHockey | - | 1.15.39 | MIDP-2.0 | nokia-ui | 75 | 16 | 600 | 4447728 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/AirHockey_s40v3a-N6280 - Mastiwap.Com.jar | 221 | OAAirHockey | - | 1.13.34 | MIDP-2.0 | - | 73 | 16 | 600 | 4604637 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/AirStrike1944FlightForFreedom_1.0.1_320x240.jar | 316 | Air Strike 1944 | - | 1.0.1 | MIDP-2.0 | - | 6 | 16 | 600 | 75265393 | 0 | - | 运行正常 |  |
| vault/Aircraft Gear.jar | 265 | Aircraft Gear | - | 1.0.0 | MIDP-2.0 | - | 12 | 16 | 600 | 3030575 | 0 | java/io/IOException,java/lang/Nu | 运行正常 |  |
| vault/Airraid_sek700.wz.cz.jar | 53 | AirRaid | - | 1.0 | MIDP-1.0 | nokia-ui | 5 | 16 | 600 | 86760 | 0 | - | 运行正常 |  |
| vault/Akiza Dart.jar | 206 | AKIZA Dart by BerON | - | 1.0.0 | MIDP-2.0 | - | 6 | 16 | 600 | 2330672 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/Aladdin 2 The New Adventure_320x240.jar | 643 | Aladdin 2 | - | 1.0 | MIDP-2.0 | - | 8 | 16 | 600 | 4644337 | 0 | - | 运行正常 |  |
| vault/Alco Bench.jar | 209 | AlcoBench | - | 09.8.11 | MIDP-2.0 | nokia-ui | 66 | 16 | 600 | 862258 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/Alex Rider Storm Breaker.jar | 225 | Stormbreaker by street_racer | - | 1.2.0 | MIDP-2.0 | nokia-ui | 9 | 16 | 600 | 4920150668 | 0 | java/lang/ArrayIndexOutOfBoundsE | 运行正常 |  |
| vault/Ali Baba And The Scary Dev.jar | 204 | Ali Baba | - | 1.1.1 | MIDP-2.0 | - | 2 | 16 | 600 | 1276829627 | 0 | java/lang/ArrayIndexOutOfBoundsE | 运行正常 |  |
| vault/AliBaba_and_Scary_Dev_240x320.jar | 204 | AliBaba | - | 1.1.0 | MIDP-2.0 | - | 2 | 16 | 600 | 8292924961 | 0 | java/lang/IllegalArgumentExcepti | 运行正常 |  |
| vault/Alien Hominid Redialed.jar | 209 | AH Redialed | - | 1.0.0 | MIDP-2.0 | - | 14 | 16 | 600 | 3181117 | 0 | java/lang/StringIndexOutOfBounds | 运行正常 |  |
| vault/Alien Hominid.jar | 209 | AH Redialed by Darden | - | 1.0.0 | MIDP-2.0 | - | 14 | 16 | 600 | 2984477 | 0 | java/lang/StringIndexOutOfBounds | 运行正常 |  |
| vault/Alien Quarantine.jar | 1918 | Alien Quarantine DEDOMIL.NET | - | 1.2.3 | MIDP-2.0 | - | 17 | 16 | 600 | 45504487 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/AlienVsPredator2-Requiem_s40v3 - Mastiwap.Com.jar | 220 | AVP Requiem | - | 1.0.0 | MIDP-2.0 | - | 32 | 16 | 600 | 15271776 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/Alien_Hominid_Redialed.jar | 205 | AH Redialed | - | 1.0.1 | MIDP-2.0 | - | 14 | 16 | 600 | 2519860 | 0 | java/lang/StringIndexOutOfBounds | 运行正常 |  |
| vault/Alien_Hominid_Redialed_S60_5F3ed_N80_E60.jar | 210 | AH Redialed | - | 1.0.1 | MIDP-2.0 | - | 14 | 16 | 600 | 2894764 | 0 | java/io/IOException,java/lang/St | 运行正常 |  |
| vault/Alpha Wing 2.jar | 553 | Alpha Wing 2 | - | 1.0.2 | MIDP-1.0 | - | 20 | 16 | 600 | 6476328 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/Alpha Wing EX.jar | 207 | Alpha Wing EX | - | 1.0.9 | MIDP-1.0 | - | 16 | 16 | 600 | 664923 | 0 | java/lang/IllegalArgumentExcepti | 运行正常 |  |
| vault/AlphaWing - Mastiwap.Com (7).jar | 207 | Alpha Wing | - | 1.1.1 | MIDP-1.0 | - | 16 | 16 | 600 | 620915 | 0 | java/lang/IllegalArgumentExcepti | 运行正常 |  |
| vault/AlphaWingEX (2).jar | 207 | Alpha Wing EX | - | 1.0.9 | MIDP-1.0 | - | 16 | 16 | 600 | 642826 | 0 | java/lang/IllegalArgumentExcepti | 运行正常 |  |
| vault/AlphaWingEX.jar | 207 | Alpha Wing EX **MASYAKA** | - | 1.0.9 | MIDP-1.0 | - | 16 | 16 | 600 | 1056140 | 0 | java/lang/IllegalArgumentExcepti | 运行正常 |  |
| vault/Alten8.-.Eternity's.Child.SE.176x220 - Mastiwap.Com.jar | 209 | Eternity Child | - | 1.0 | MIDP-2.0 | - | 12 | 16 | 600 | 3531544227 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/Alten8.-.Opal.Quest.SE.176x220 - Mastiwap.Com.jar | 225 | OpalQuest | - | 1.15 | MIDP-2.0 | - | 11 | 16 | 600 | 11290919 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/America's Army - Special Operations.jar | 329 | America's Army | - | 1.0.5 | MIDP-2.0 | - | 6 | 16 | 600 | 15498105 | 0 | - | 运行正常 |  |
| vault/American Gangster - The Mobile Game.jar | 543 | American Gangster | - | 1.0.9 | MIDP-2.0 | - | 6 | 16 | 600 | 11574333 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/AmericanGangster_java-gratis.jar | 395 | American Gangster | - | 1.0.0 | MIDP-2.0 | nokia-ui | 6 | 16 | 600 | 12909810 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/American_Gangster _-_The_Mobile_Game_N91 - Mastiwap.Com.jar | 209 | American Gangster | - | 1.0.1 | MIDP-2.0 | nokia-ui | 6 | 16 | 600 | 15698483 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/Americas Cup Racer__320x240.jar | 141 | ACRacer | - | 1.0.5 | MIDP-1.0 | - | 17 | 16 | 600 | 2447093 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/Amy Pumpkin's Nightmare.jar | 252 | AmyPumpkin | - | 1.0 | MIDP-2.0 | - | 6 | 16 | 600 | 707975 | 0 | java/io/EOFException,javax/micro | 运行正常 |  |
| vault/Anakonda.jar | 83 | Anakonda | - | 1.0.0 | MIDP-1.0 | - | 13 | 16 | 600 | 1021025 | 0 | java/io/IOException,java/lang/Nu | 运行正常 |  |
| vault/Anarchy2087_s60v1 - Mastiwap.Com.jar | 221 | Anarchy2087 | - | 1.0 | MIDP-1.0 | nokia-ui | 8 | 16 | 600 | 9745355 | 0 | java/io/IOException,javax/microe | 运行正常 |  |
| vault/Ancient Empires 2.jar | 264 | Ancient Empires II | - | 1.0.2 | MIDP-1.0 | - | 9 | 16 | 600 | 636956 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/Ancient Empires.jar | 125 | Ancient Empires | - | 1.0.3 | MIDP-1.0 | - | 9 | 16 | 600 | 122070 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/AncientEmpires.jar | 125 | Ancient Empires | - | 1.0.3 | MIDP-1.0 | nokia-ui | 9 | 16 | 600 | 163689 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/AncientEmpires2.jar | 264 | Ancient Empires II | - | 1.0.2 | MIDP-1.0 | - | 9 | 16 | 600 | 667227 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/AncientGames.jar | 218 | Ancient Games by Spid3r | - | 1.0.4 | MIDP-1.0 | nokia-ui | 48 | 16 | 600 | 7786309 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/AncientGames_S60V2_Retail_Etty - Mastiwap.Com.jar | 213 | AncientGames | - | 1.0.4 | MIDP-1.0 | nokia-ui | 48 | 16 | 600 | 7760120 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/AncientGames_s40v3a - Mastiwap.Com.jar | 218 | AncientGames | - | 1.0.4 | MIDP-1.0 | nokia-ui | 48 | 16 | 600 | 7719727 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/Ancient_Empires_by_BFLM.jar | 126 | Ancient Empires | - | 1.0.4 | MIDP-1.0 | nokia-ui | 9 | 16 | 600 | 489006 | 0 | java/lang/ArrayIndexOutOfBoundsE | 运行正常 |  |
| vault/Ancient_Taxi_x65.jar | 215 | WUP.Ru | - | 1.0.85 | MIDP-2.0 | - | 45 | 16 | 600 | 7404823 | 0 | java/io/IOException,javax/microe | 运行正常 |  |
| vault/AngelsAndDemons.jar | 570 | Angels and Demons | - | 1.0.0 | MIDP-1.0 | - | 12 | 16 | 600 | 10324022 | 0 | java/lang/NumberFormatException, | 运行正常 |  |
| vault/Angry Snake__320x240.jar | 319 | Angry Snake | - | 1.0.0 | MIDP-2.0 | - | 22 | 16 | 600 | 1514523 | 0 | - | 运行正常 |  |
| vault/Animal Tycoon 2_320x240.jar | 1026 | Animal Tycoon 2 | - | 1.0 | MIDP-2.0 | - | 73 | 16 | 600 | 2786277 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/Antar_Expedition - Mastiwap.Com.jar | 213 | Antar | - | 1.02 | MIDP-1.0 | nokia-ui | 33 | 16 | 600 | 53 | 0 | - | 运行正常 | runs without input; the generic key script m |
| vault/Anyplays.-.Sexy.Puzzmaniac.SE.176x220 - Mastiwap.Com.jar | 208 | Puzzmaniac | - | 1.0 | MIDP-2.0 | - | 15 | 16 | 433 | 1439233 | 0 | java/io/EOFException,java/lang/N | 运行正常 | exited itself (notifyDestroyed) |
| vault/Apocalypse3000.jar | 213 | Apocalypse 3000 | - | 1.26 | MIDP-1.0 | nokia-ui | 18 | 16 | 600 | 778085 | 0 | java/io/EOFException,javax/micro | 运行正常 |  |
| vault/Appu And Pappu.jar | 205 | Appu And Pappu | - | 1.0 | MIDP-2.0 | - | 5 | 16 | 600 | 3956145 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/Aqua Blast.jar | 211 | Aqua Blast | - | 1.0 | MIDP-2.0 | - | 5 | 16 | 600 | 5347298 | 0 | - | 运行正常 |  |
| vault/AquaDig.jar | 160 | AquaDig | - | 1.22 | MIDP-2.0 | - | 14 | 16 | 600 | 6248369 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/AquaLifeX_MIDP2_240X320 - Mastiwap.Com.jar | 225 | AquaLifeX_MIDP2_240X320 | - | 1.0 | MIDP-2.0 | - | 2 | 16 | 600 | 2016159 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/Aqua_Driller_(176x208) - Mastiwap.Com.jar | 217 | AquaDriller | - | 1.20 | MIDP-2.0 | nokia-ui | 24 | 16 | 600 | 232599 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/Aqua_Driller_v1.20 - Mastiwap.Com.jar | 216 | AquaDriller | - | 1.20 | MIDP-2.0 | nokia-ui | 24 | 16 | 600 | 232902 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/Aquarace.jar | 83 | AquaRace | - | 2.0.1 | MIDP-1.0 | nokia-ui | 15 | 16 | 600 | 1729603 | 0 | java/io/IOException,java/lang/Nu | 运行正常 |  |
| vault/Arcade Side Golf.jar | 194 | ArcadeSideGolf | - | 1.0.2 | MIDP-2.0 | - | 6 | 16 | 600 | 20245697 | 0 | java/io/IOException,java/lang/Nu | 运行正常 |  |
| vault/ArcadeClassicsCollection_1.0.0_320x240.jar | 521 | ArcadeClassics | - | 1.0.0 | MIDP-1.0 | - | 51 | 16 | 600 | 14345749 | 0 | java/io/EOFException,java/io/IOE | 运行正常 |  |
| vault/ArcadePark.jar | 243 | Arcade Park 2 | - | 1.0.0 | MIDP-2.0 | - | 13 | 16 | 600 | 1689456 | 0 | java/io/EOFException,java/io/IOE | 运行正常 |  |
| vault/ArcadePark2.jar | 371 | ArcadePark2 | - | 1.1.2 | MIDP-2.0 | - | 46 | 16 | 60 | 1895 | 0 | java/lang/NoClassDefFoundError,j | 标题自身异常 | title references a class it does not bundle  |
| vault/Archon.jar | 162 | Archon | - | 1.0.1 | MIDP-2.0 | - | 7 | 16 | 600 | 4790372 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/Arkanoid Evolution.jar | 597 | Arkanoid Evolution | - | 1.1.0 | MIDP-2.0 | - | 19 | 16 | 600 | 4018079 | 0 | java/lang/ArrayIndexOutOfBoundsE | 运行正常 |  |
| vault/Arkanoid.jar | 92 | Arkanoid | - | 1.8.6 | MIDP-1.0 | - | 12 | 16 | 600 | 382505 | 0 | - | 运行正常 |  |
| vault/Army Men - Mobile Ops.jar | 654 | ArmyMen | - | 1.0.0 | MIDP-2.0 | - | 89 | 16 | 600 | 23250280 | 0 | java/io/EOFException,java/lang/A | 运行正常 |  |
| vault/Army Men Mobile Ops_320x240.jar | 544 | ArmyMen | - | 1.0.0 | MIDP-2.0 | - | 89 | 16 | 600 | 34523816 | 0 | java/io/EOFException,java/lang/A | 运行正常 |  |
| vault/Asphalt - Urban GT 3D.jar | 339 | Asphalt Urban GT 3D | - | 1.0.5 | MIDP-2.0 | - | 27 | 16 | 600 | 12886671 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/Asphalt - Urban GT.jar | 250 | AsphaltUrbanGT | - | 1.0.1 | MIDP-2.0 | - | 7 | 16 | 600 | 221253009 | 0 | javax/microedition/rms/InvalidRe | 运行正常 |  |
| vault/Asphalt 2 - Urban GT.jar | 309 | Asphalt2 | - | 1.0.0 | MIDP-2.0 | - | 6 | 16 | 600 | 8822272 | 0 | - | 运行正常 |  |
| vault/Asphalt 3 - Street Rules 3D.jar | 1027 | Asphalt 3 Street Rules 3D | - | 1.0.0 | MIDP-2.0 | - | 19 | 16 | 600 | 141142083 | 0 | java/lang/ClassNotFoundException | 运行正常 |  |
| vault/Asphalt 3 - Street Rules.jar | 285 | Asphalt3 | - | 1.0.2 | MIDP-2.0 | - | 5 | 16 | 600 | 324602040 | 0 | javax/microedition/rms/InvalidRe | 运行正常 |  |
| vault/Asphalt 4 - Elite Racing.jar | 370 | Asphalt 4 : Elite Racing | - | 1.1.7 | MIDP-2.0 | - | 10 | 16 | 600 | 10091593 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/Asphalt 6 - Adrenaline.jar | 1500 | Asphalt6 | - | 1.0.0 | MIDP-2.0 | - | 22 | 16 | 600 | 168720803 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/Asphalt Nitro.jar | 2086 | Asphalt Nitro | - | 1.1.7 | MIDP-2.0 | - | 21 | 16 | 600 | 185969608 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/Asphalt3StreetRules_1.1.5_320x240.jar | 1110 | Asphalt 3 3D Street Rules | - | 1.1.5 | MIDP-2.0 | m3g,nokia-ui | 18 | 16 | 600 | 4014167949 | 0 | java/lang/ClassNotFoundException | 运行正常 |  |
| vault/Asphalt4EliteRacing_1.0.0_320x240.jar | 331 | Asphalt 4 Elite Racing | - | 1.0.0 | MIDP-2.0 | - | 11 | 16 | 600 | 3385298137 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/Asphalt5NitroRush_1.1.5_320x240.jar | 957 | Asphalt 5 Nitro Rush 3D | - | 1.1.5 | MIDP-2.0 | m3g,nokia-ui | 18 | 16 | 600 | 3871065686 | 0 | java/lang/ClassNotFoundException | 运行正常 |  |
| vault/Asphalt6Adrenaline_1.0.2_320x240.jar | 1033 | Asphalt 6 Adrenaline | - | 1.0.2 | MIDP-2.0 | - | 20 | 16 | 600 | 326734777 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/AsphaltUrbanGT3_176x220.jar | 296 | Asphalt Urban GT3 | - | 1.1.0 | MIDP-2.0 | - | 5 | 16 | 600 | 4023144913 | 0 | javax/microedition/rms/InvalidRe | 运行正常 |  |
| vault/Asphalt_Urban_3_3D_sew800i[1].webgarden.cz.jar.jar | 1106 | Asphalt 3 3D Street Rules_by | - | 1.1.5 | MIDP-2.0 | m3g,nokia-ui | 18 | 16 | 600 | 3965912595 | 0 | java/lang/ClassNotFoundException | 运行正常 |  |
| vault/Asphalt_Urban_GT3.jar | 250 | Asphalt3 | - | 1.0.1 | MIDP-2.0 | - | 5 | 16 | 600 | 21397934 | 0 | javax/microedition/rms/InvalidRe | 运行正常 |  |
| vault/Asphalt_Urban_GT_3D_sew800i[1].webgarden.cz.jar | 399 | Asphalt Urban GT 3D_by_pirat | - | 1.1.9 | MIDP-2.0 | mascot | 23 | 16 | 600 | 25543 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/Assassin's Creed - Brotherhood.jar | 1171 | Assassin's Creed Brotherhood | - | 1.1.8 | MIDP-2.0 | - | 11 | 16 | 600 | 183741858 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/Assassin's Creed - Revelations.jar | 1596 | Assassin's Creed Revelations | - | 1.0.6 | MIDP-2.0 | - | 14 | 16 | 600 | 40767011 | 0 | java/lang/IndexOutOfBoundsExcept | 运行正常 |  |
| vault/Assassin's Creed II.jar | 1025 | Assassin's Creed 2 | - | 1.2.3 | MIDP-2.0 | - | 9 | 16 | 600 | 180784094 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/Assassin's Creed III.jar | 1625 | Assassin's Creed III | - | 1.1.3 | MIDP-2.0 | - | 12 | 16 | 600 | 116503061 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/Assassin's Creed.jar | 612 | Assassin's Creed | - | 1.0.2 | MIDP-2.0 | - | 5 | 16 | 600 | 648882219 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/Assassin_sCreed_SonyEricsson_W810i_CZ_IGP_101.jar | 576 | Assassin_s creed | - | 1.0.1 | MIDP-2.0 | - | 6 | 16 | 600 | 3878765833 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/Assassins Creed-176x220.jar | 347 | Assassins Creed | - | 1.1.0 | MIDP-2.0 | - | 5 | 16 | 600 | 3483651350 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/Assassins Creed.jar | 575 | Assassins creed | - | 1.0.9 | MIDP-2.0 | - | 6 | 16 | 600 | 3800546955 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/Assassins-Creed-176x220-mobility.freeforfun.org.jar | 347 | Assassin_s Creed by piratesx | - | 1.1.0 | MIDP-2.0 | - | 5 | 16 | 600 | 3491640135 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/AssassinsCreed1_1.0.1_320x240.jar | 376 | Assassin_s Creed | - | 1.0.1 | MIDP-2.0 | - | 5 | 16 | 600 | 3634790237 | 0 | - | 运行正常 |  |
| vault/AssassinsCreed2_1.2.5_320x240.jar | 687 | Assassin's Creed 2 | - | 1.2.5 | MIDP-2.0 | - | 9 | 16 | 600 | 58025237 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/AssassinsCreedBrotherhood_1.2.7_320x240.jar | 1017 | Assassin's Creed Brotherhood | - | 1.2.7 | MIDP-2.0 | - | 10 | 16 | 600 | 117462485 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/Assault_Wings_1944.jar | 98 | Assault Wings 1944 | - | 1.0.2 | MIDP-2.0 | - | 13 | 16 | 600 | 1926384 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/Assessment_Trainer_s60v3_240x320.jar | 265 | AssessmentTrainer | - | 1.6 | MIDP-2.0 | - | 2 | 16 | 600 | 4471026 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/Asterix 2008 240x320.jar | 553 | Asterix 2008 | - | 00.01.06 | MIDP-2.0 | - | 17 | 16 | 600 | 2262264 | 0 | - | 运行正常 |  |
| vault/AsterixObelixEncounterCleopatra_1.0.3_320x240.jar | 666 | Asterix and Obelix encounter | - | 1.0.3 | MIDP-2.0 | - | 11 | 16 | 600 | 66355935 | 0 | - | 运行正常 |  |
| vault/Asteroids.jar | 368 | Asteroids | - | 1.0.3 | MIDP-2.0 | - | 6 | 16 | 600 | 4636618 | 0 | java/lang/NumberFormatException, | 运行正常 |  |
| vault/Astro Knight Galaxy.jar | 182 | Knight | - | 1.0 | MIDP-1.0 | - | 3 | 16 | 600 | 287345774 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/AtariLegendsPack.jar | 290 | Atari Legends pack | - | 0.0.15 | MIDP-2.0 | - | 47 | 16 | 600 | 2099826 | 0 | java/io/IOException,javax/microe | 运行正常 |  |
| vault/AtariLegendsVol1.jar | 83 | Atari Legends Vol1 [terjef] | - | 1.1.6 | MIDP-1.0 | - | 13 | 16 | 600 | 2703266 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/AtariLegendsVol2.jar | 78 | Atari Legends 2 [terjef] | - | 1.3.8 | MIDP-2.0 | - | 9 | 16 | 600 | 2705214 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/AtariLegendsVol3.jar | 100 | Atari Legends 3 [terjef] | - | 1.1.0 | MIDP-2.0 | - | 10 | 16 | 600 | 1372705 | 0 | java/lang/IllegalMonitorStateExc | 运行正常 |  |
| vault/Atlantis SkyPatrol.jar | 468 | Atlantis Sky Patrol | - | 1.0.0 | MIDP-2.0 | - | 12 | 16 | 600 | 750784 | 0 | - | 运行正常 |  |
| vault/Atlantis_2097_by_BFLM.jar | 201 | Atlantis2097 | - | 2.2 | MIDP-1.0 | nokia-ui | 7 | 16 | 600 | 5157 | 0 | - | 运行正常 |  |
| vault/Avalanche Snowboarding.jar | 836 | Avalanche Snowboarding | - | 1.2.0 | MIDP-2.0 | - | 17 | 16 | 600 | 18593864 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/AvalancheSnowboarding_1.2.1_320x240.jar | 714 | Avalanche Snowboarding | - | 1.2.1 | MIDP-2.0 | - | 18 | 16 | 600 | 61675040 | 0 | javax/microedition/rms/InvalidRe | 运行正常 |  |
| vault/Avatar_1.0.6_320x240.jar | 659 | Avatar | - | 1.0.6 | MIDP-2.0 | - | 8 | 16 | 600 | 31059463 | 0 | - | 运行正常 |  |
| vault/Avenging Angel.jar | 221 | Avenging Angel | - | 2.00.36 | MIDP-2.0 | - | 5 | 16 | 600 | 638148 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/AztecSnake.jar | 488 | Aztec Snake Retail Stox | - | 1.0.0 | MIDP-2.0 | - | 17 | 16 | 600 | 599707 | 0 | - | 运行正常 |  |
| vault/BMW 1 Series Challenge.jar | 118 | BMW 1 Series Challenge | - | 1.0.3 | MIDP-1.0 | nokia-ui | 6 | 16 | 600 | 433630 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/BT_Info.jar | 58 | 'BT INFOx | - | 1.04 | MIDP-2.0 | - | 13 | 16 | 600 | 6325 | 0 | java/lang/ArrayIndexOutOfBoundsE | 运行正常 | runs without input; the generic key script m |
| vault/Backgammon.jar | 147 | elkware's Backgammon | - | 1.1.2 | MIDP-2.0 | - | 3 | 16 | 600 | 4055283 | 0 | java/io/IOException,java/lang/Nu | 运行正常 |  |
| vault/BackstagePass.jar | 63 | BackstagePass | - | 1.0.1 | MIDP-1.0 | nokia-ui | 10 | 16 | 600 | 18774178 | 0 | - | 运行正常 |  |
| vault/Batman - Guardian of Gotham.jar | 638 | Batman(TM) - Guardian of Got | - | 2.0.0 | MIDP-2.0 | - | 82 | 16 | 600 | 5917024 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/Battle for The White House.jar | 576 | Battle For The White House | - | 1.0.4 | MIDP-2.0 | - | 4 | 16 | 600 | 13409832 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/Battle_Of_Empires_by_BFLM.jar | 95 | Battle Of Empires | - | 2.0 | MIDP-1.0 | nokia-ui | 6 | 16 | 600 | 1195145 | 0 | java/io/IOException | 运行正常 |  |
| vault/Battleships - Sea on Fire.jar | 222 | Battleships 2010 | - | 1.13.5 | MIDP-2.0 | - | 17 | 16 | 600 | 437581 | 0 | - | 运行正常 |  |
| vault/Battlestar Galactica.jar | 440 | Battlestar Galactica | - | 1.0.0 | MIDP-2.0 | - | 61 | 16 | 600 | 5716326 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/Beach Mini Golf 3D.jar | 238 | Minigolf 3D | - | 1.3.01 | MIDP-1.0 | - | 55 | 16 | 600 | 12688892 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/Bejeweled 3.jar | 550 | Bejeweled 3 | - | 4.0.78 | MIDP-2.0 | - | 42 | 16 | 600 | 2937296 | 0 | - | 运行正常 |  |
| vault/Bejeweled Twist.jar | 574 | BejeweledTwist | - | 01.00.04 | MIDP-2.0 | - | 5 | 16 | 600 | 31599371 | 0 | java/io/IOException,javax/microe | 运行正常 |  |
| vault/Bejeweled.jar | 350 | Bejeweled (mobilegamesarena. | - | 1.5.46 | MIDP-2.0 | nokia-ui | 14 | 16 | 600 | 21174186 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/Bejeweled1_1.5.46_320x240.jar | 371 | Bejeweled | - | 1.5.46 | MIDP-2.0 | nokia-ui | 14 | 16 | 600 | 20914106 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/Bejeweled3_4.7.74_320x240.jar | 656 | Bejeweled 3 | - | 4.7.74 | MIDP-2.0 | m3g | 42 | 16 | 600 | 24052657 | 0 | - | 运行正常 |  |
| vault/BejeweledTwist.jar | 659 | BejeweledTwist by Alexe | - | 01.00.37 | MIDP-2.0 | - | 4 | 16 | 601 | 30398040 | 0 | java/io/IOException,javax/microe | 运行正常 |  |
| vault/Bejeweled_320x240.jar | 350 | Bejeweled | - | 1.5.46 | MIDP-2.0 | nokia-ui | 14 | 16 | 600 | 21384798 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/Beowulf - The Mobile Game.jar | 576 | Beowulf | - | 1.1.1 | MIDP-2.0 | - | 7 | 16 | 600 | 11182462 | 0 | - | 运行正常 |  |
| vault/Beowulf_1.1.6_320x240.jar | 585 | Beowulf | - | 1.1.6 | MIDP-2.0 | nokia-ui | 7 | 16 | 600 | 16204491 | 0 | - | 运行正常 |  |
| vault/Ber_Nebo_Neber-by-TOM35.jar | 189 | Ber Nebo Neber by TOM35 | - | 1.0.6 | MIDP-2.0 | - | 4 | 16 | 601 | 315182127 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/BestBuster.jar | 81 | BestBuster | - | 1.0 | MIDP-1.0 | nokia-ui | 7 | 16 | 600 | 101031 | 0 | - | 运行正常 |  |
| vault/Biene_Maja.jar | 55 | Biene Maja | - | 1.0.1 | MIDP-1.0 | nokia-ui | 14 | 16 | 600 | 917021 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/Big Range Hunting 2.jar | 1048 | Big Range Hunting 2 | - | 1.0.0 | MIDP-2.0 | - | 13 | 16 | 600 | 14764809 | 0 | java/lang/ArrayIndexOutOfBoundsE | 运行正常 |  |
| vault/Big Range Hunting.jar | 319 | Big Range Hunting | - | 1.0.3 | MIDP-2.0 | nokia-ui | 6 | 16 | 600 | 4090188182 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/Big2Popstars.jar | 55 | Big 2 Popstars | - | 1.0.0 | MIDP-1.0 | nokia-ui | 13 | 16 | 600 | 1893883 | 0 | - | 运行正常 |  |
| vault/Big2_LeYuan.jar | 195 | 锄D乐园 | - | 1.2 | MIDP-1.0 | - | 5 | 16 | 600 | 91854 | 0 | - | 运行正常 |  |
| vault/BigBrotherBrix.jar | 97 | Brix BB | - | 2.0.0 | MIDP-1.0 | nokia-ui | 5 | 16 | 600 | 3857345170 | 0 | - | 运行正常 |  |
| vault/Big_2_Heroes_by_BFLM.jar | 55 | Big 2 Heroes | - | 1.0.1 | MIDP-1.0 | nokia-ui | 14 | 16 | 600 | 2862080 | 0 | - | 运行正常 |  |
| vault/Bigbrother_sek700.wz.cz.jar | 64 | Big Brother | - | 1.8 | MIDP-1.0 | nokia-ui | 32 | 16 | 600 | 602966 | 0 | - | 运行正常 |  |
| vault/Bijlee_sek700.wz.cz.jar | 56 | Bijlee | - | 1.0 | MIDP-1.0 | nokia-ui | 6 | 16 | 600 | 153207 | 0 | - | 运行正常 |  |
| vault/Biker_Fun.jar | 58 | BikerFun | - | 1.2 | MIDP-1.0 | nokia-ui | 18 | 16 | 600 | 119445 | 0 | - | 运行正常 |  |
| vault/Biker_Fun_by_BFLM.jar | 52 | BikerFun | - | 1.2 | MIDP-1.0 | nokia-ui | 11 | 16 | 600 | 119445 | 0 | - | 运行正常 |  |
| vault/Bikini Volleyball.jar | 621 | Bikini Volleyball | - | 1.0.7 | MIDP-2.0 | - | 8 | 16 | 600 | 4434518 | 0 | java/lang/ArrayIndexOutOfBoundsE | 运行正常 |  |
| vault/BikiniVolleyball_1.0.0_320x240.jar | 659 | Bikini Volleyball | - | 1.0.0 | MIDP-2.0 | - | 8 | 16 | 600 | 5598962349 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/Billy The Kid - Wanted.jar | 201 | Billy the Kid: Wanted **MASY | - | 1.2.8 | MIDP-2.0 | - | 23 | 16 | 600 | 316360 | 0 | javax/microedition/rms/InvalidRe | 运行正常 |  |
| vault/Bio Soldiers 3D.jar | 419 | 3D_BIO-SOLDIERS | - | 1.0 | MIDP-2.0 | - | 7 | 16 | 600 | 730885 | 0 | java/io/IOException,java/lang/Nu | 运行正常 |  |
| vault/BioShock.jar | 1050 | BioShock | - | 1.1.4 | MIDP-2.0 | - | 19 | 16 | 600 | 10209153 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/Bionicle Heroes.jar | 162 | BIONICLE HEROES | - | 1.0.2 | MIDP-1.0 | - | 11 | 16 | 600 | 240393951 | 0 | - | 运行正常 |  |
| vault/Biozone.jar | 541 | Biozone | - | 0.9.0 | MIDP-2.0 | - | 9 | 16 | 600 | 363231225 | 0 | java/lang/IllegalArgumentExcepti | 运行正常 |  |
| vault/Black Shark 2 - Siberia.jar | 262 | BlackShark 2 Siberia | - | 1.3.0 | MIDP-2.0 | - | 16 | 16 | 600 | 11661 | 0 | java/io/EOFException,java/lang/I | 运行正常 |  |
| vault/Blackbeard's Treasure.jar | 422 | BlackbeardTreasure | - | 1.0.2 | MIDP-1.0 | - | 27 | 16 | 600 | 130647858 | 0 | - | 运行正常 |  |
| vault/Blade Knight Tactics - Two Cities.jar | 202 | Blade Knight Tactics Two Cit | - | 1.1.0 | MIDP-2.0 | - | 14 | 16 | 600 | 172346 | 0 | - | 运行正常 |  |
| vault/Block Runner.jar | 474 | BlockRunner2D | - | 1.0.2 | MIDP-2.0 | - | 33 | 16 | 600 | 725813815 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/Block'd.jar | 153 | Block'd | - | 4.3.51 | MIDP-2.0 | - | 11 | 16 | 600 | 3622329 | 0 | - | 运行正常 |  |
| vault/BlockBreaker 2 Deluxe.jar | 583 | Block Breaker 2 Deluxe | - | 1.0.8 | MIDP-2.0 | - | 10 | 16 | 600 | 97384015 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/BlockBreaker 3 Unlimited.jar | 1089 | Block Breaker 3 Unlimited | - | 1.0.8 | MIDP-2.0 | - | 21 | 16 | 600 | 137867352 | 0 | - | 运行正常 |  |
| vault/BlockBreaker Deluxe.jar | 591 | BlockBreaker Deluxe | - | 1.0.9 | MIDP-2.0 | - | 7 | 16 | 600 | 342428814 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/BlockBreaker2Deluxe.jar | 574 | Block Breaker 2 Deluxe (mobi | - | 1.0.3 | MIDP-2.0 | nokia-ui | 10 | 16 | 600 | 2522110527 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/BlockBreaker3Unlimited_1.0.1_320x240.jar | 417 | Block Breaker 3 Unlimited | - | 1.0.1 | MIDP-2.0 | - | 12 | 16 | 600 | 39382769 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/BlockBreakerDeluxe.jar | 591 | Block Breaker Deluxe | - | 1.0.9 | MIDP-2.0 | m3g,nokia-ui | 7 | 16 | 600 | 5142482923 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/BlockBreakerDeluxe2_1.0.4_320x240.jar | 375 | Block Breaker 2 Deluxe | - | 1.0.4 | MIDP-2.0 | - | 10 | 16 | 600 | 23442294 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/Blokus_1.0.6_320x240.jar | 538 | Blokus | - | 1.0.6 | MIDP-2.0 | - | 16 | 16 | 600 | 28663065 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/Blood+.jar | 677 | Blood+ | - | 1.0.0 | MIDP-2.0 | - | 23 | 16 | 600 | 7544461 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/Blur Racing Mobile.jar | 624 | Blur | - | 1.0.0 | MIDP-2.0 | - | 22 | 16 | 600 | 259433233 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/BoBoElf.jar | 117 | Bobo Elf | - | 1.0 | MIDP-1.0 | nokia-ui | 14 | 16 | 600 | 3399171 | 0 | javax/microedition/rms/InvalidRe | 运行正常 |  |
| vault/Boardersball_sek700.wz.cz.jar | 182 | Boarder's Ball | - | 1.1 | MIDP-1.0 | nokia-ui | 63 | 16 | 600 | 2045463627 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/Bobble Blast Deluxe_320x240_Nokia_E71.jar | 372 | Bobble Blast Deluxe | - | 1.0.0 | MIDP-2.0 | - | 86 | 16 | 600 | 22724111 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/Bobby.Bearing_v102.jar | 58 | Bobby Bearing | - | 1.0.2 | MIDP-1.0 | nokia-ui | 19 | 16 | 600 | 17290 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/BobbyCarrot2_K700.jar | 74 | BC2 [terjef] | - | 1.0.0 | MIDP-1.0 | - | 4 | 16 | 600 | 214632 | 0 | - | 运行正常 |  |
| vault/BobbyCarrot_BFLM.jar | 76 | Bobby Carrot | - | 1.0 | MIDP-1.0 | nokia-ui | 4 | 16 | 600 | 109031 | 0 | - | 运行正常 |  |
| vault/Bobbys Blocks_320x240.jar | 229 | Bobby | - | 1.1.2 | MIDP-2.0 | - | 12 | 16 | 600 | 2923102458 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/Bobobox_sek700.wz.cz.jar | 80 | 波波糖果盒 | - | 1.1.2 | MIDP-2.0 | - | 9 | 16 | 600 | 7386831 | 0 | java/io/IOException,javax/microe | 运行正常 |  |
| vault/Bocce World Tour.jar | 288 | Bocce World Tour | - | 1.4.1 | MIDP-2.0 | nokia-ui | 12 | 16 | 600 | 26633616 | 0 | java/io/EOFException,java/lang/N | 运行正常 |  |
| vault/Bocconi_ICE_cz_by_Dzirvi.jar | 104 | BocconiICE_1 | - | 1.3 | MIDP-2.0 | - | 18 | 16 | 600 | 2134515 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/Bolt.jar | 432 | Bolt | - | 1.0.0 | MIDP-2.0 | - | 21 | 16 | 600 | 562021 | 0 | java/io/EOFException,java/io/IOE | 运行正常 |  |
| vault/Bomb Jack.jar | 90 | Bombjack | - | 1.0.9 | MIDP-1.0 | - | 9 | 16 | 600 | 817131 | 0 | javax/microedition/rms/InvalidRe | 运行正常 |  |
| vault/Bomber Pilot.jar | 68 | BomberPilot | - | 1.0 | MIDP-1.0 | nokia-ui | 4 | 16 | 600 | 1784174 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/Bomber.jar | 113 | Bomber | - | 1.1.4 | MIDP-1.0 | nokia-ui | 51 | 16 | 600 | 7068760 | 0 | javax/microedition/rms/InvalidRe | 运行正常 |  |
| vault/Bomberman 2007.jar | 291 | Bomberman | - | 1.0.32 | MIDP-1.0 | - | 12 | 16 | 600 | 23434154 | 0 | javax/microedition/rms/InvalidRe | 运行正常 |  |
| vault/Bomberman Deluxe.jar | 166 | Bomberman Deluxe | - | 1.28.2 | MIDP-1.0 | - | 5 | 16 | 600 | 26684634 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/Bomberman.jar | 45 | Bomberman | - | 1.0.3 | MIDP-1.0 | nokia-ui | 8 | 16 | 600 | 1391558 | 0 | javax/microedition/rms/InvalidRe | 运行正常 |  |
| vault/Bomberman2.jar | 40 | Bomberman | - | 1.0.3 | MIDP-1.0 | nokia-ui | 15 | 16 | 600 | 1695572 | 0 | javax/microedition/rms/InvalidRe | 运行正常 |  |
| vault/BombermanDeluxe.jar | 172 | Bomberman Deluxe | - | 1.28.2 | MIDP-1.0 | - | 5 | 16 | 600 | 27118081 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/BombermanPuzzle.jar | 52 | Bomberman Puzzle | - | 1.0.1 | MIDP-1.0 | - | 6 | 16 | 600 | 375423 | 0 | javax/microedition/rms/InvalidRe | 运行正常 |  |
| vault/BombermanSupreme.jar | 315 | Bomberman | - | 1.0.5 | MIDP-1.0 | - | 19 | 16 | 600 | 22658179 | 0 | javax/microedition/rms/InvalidRe | 运行正常 |  |
| vault/BoulderDash2.jar | 93 | Boulder Dash 2 | - | 1.2 | MIDP-2.0 | - | 9 | 16 | 600 | 808941 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/Bounce (128x128).jar | 54 | Bounce | - | 1.8 | MIDP-1.0 | nokia-ui | 9 | 16 | 600 | 518758 | 0 | java/lang/StackOverflowError | 运行正常 |  |
| vault/Bounce Tales (240x320).jar | 592 | Bounce Tales | - | 2.0.25 | MIDP-2.0 | nokia-ui | 20 | 16 | 600 | 71401224 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/Bounce Tales.jar | 592 | Bounce Tales | - | 2.0.25 | MIDP-2.0 | - | 20 | 16 | 600 | 11624377 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/Brain Genius (240x320).jar | 321 | Brain Genius | - | 0.2.0 | MIDP-1.0 | - | 8 | 16 | 600 | 7294570 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/Brain Genius 2 Deluxe__320x240.jar | 575 | Brain Genius 2 Deluxe | - | 1.1.3 | MIDP-1.0 | nokia-ui | 24 | 16 | 600 | 7396746 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/Brain Tester 24 Pack Vol 2_320x240.jar | 951 | Brain Tester2 | - | 1.0.5 | MIDP-2.0 | nokia-ui | 39 | 16 | 600 | 14170768 | 0 | java/io/EOFException,javax/micro | 运行正常 |  |
| vault/BrainChallenge1_1.0.0_320x240.jar | 241 | Brain Challenge | - | 1.0.0 | MIDP-2.0 | - | 8 | 16 | 600 | 10640135 | 0 | java/lang/ArrayIndexOutOfBoundsE | 运行正常 |  |
| vault/BrainChallenge2StressManagement_1.0.9_320x240.jar | 391 | BRAIN CHALLENGE VOL.2 | - | 1.0.9 | MIDP-2.0 | - | 8 | 16 | 600 | 28163202 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/BrainChallenge3ThinkAgain_1.1.0_320x240.jar | 730 | Brain Challenge 3 Think Agai | - | 1.1.0 | MIDP-2.0 | - | 20 | 16 | 600 | 2167163738 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/BrainChallenge4-BreakingLimits_1.0.5_320x240.jar | 870 | Brain Challenge 4: Breaking  | - | 1.0.5 | MIDP-2.0 | nokia-ui | 10 | 16 | 600 | 331449483 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/BrainTester_1.0.2_320x240.jar | 272 | Brain | - | 0.0.2 | MIDP-2.0 | - | 69 | 16 | 600 | 7643374 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/Brave - The Official Game.jar | 1373 | Brave | - | 1.1.1 | MIDP-2.0 | - | 29 | 16 | 600 | 17818806 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/Break Quest Mobile_320x240.jar | 492 | Break Quest Mobile | - | 1.0.1 | MIDP-2.0 | - | 69 | 16 | 600 | 1209998006 | 0 | java/io/IOException,javax/microe | 运行正常 |  |
| vault/Brick and Bubble Revolution__320x240.jar | 803 | BrickBubbleRev | - | 1.0.2 | MIDP-2.0 | nokia-ui | 70 | 16 | 600 | 36050383 | 0 | java/io/EOFException,java/lang/O | 运行正常 |  |
| vault/BrickBreakerRevolution.jar | 234 | Brick Breaker | - | 1.0.09 | MIDP-2.0 | nokia-ui | 13 | 16 | 600 | 1885648971 | 0 | java/io/EOFException,java/lang/N | 运行正常 |  |
| vault/BrickBreakerRevolution2.jar | 947 | BBR 2 | - | 1.0.4 | MIDP-2.0 | nokia-ui | 57 | 16 | 600 | 210942381 | 0 | java/io/EOFException,javax/micro | 运行正常 |  |
| vault/BrickCrusher_1.0.0_320x240.jar | 291 | Brick Crusher | - | 1.0.0 | MIDP-2.0 | - | 89 | 16 | 600 | 1737697697 | 0 | java/io/IOException,javax/microe | 运行正常 |  |
| vault/Brothers in Arms - Art of War.jar | 594 | Brothers in Arms Art of War | - | 1.3.8 | MIDP-2.0 | - | 6 | 16 | 600 | 63293982 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/Brothers in Arms - Earned in Blood.jar | 257 | Brothers In Arms | - | 1.0.1 | MIDP-2.0 | - | 9 | 16 | 600 | 1103978 | 0 | - | 运行正常 |  |
| vault/BrothersInArmsEarnedInBlood_1.0.1_320x240.jar | 232 | Brothers In Arms:Earned In B | - | 1.0.1 | MIDP-2.0 | - | 7 | 16 | 600 | 2719251446 | 0 | java/lang/ArrayIndexOutOfBoundsE | 运行正常 |  |
| vault/Brutal_Fight_by_BFLM.jar | 171 | BrutalFighting | - | 1.0 | MIDP-1.0 | nokia-ui | 5 | 16 | 600 | 608268 | 0 | javax/microedition/rms/InvalidRe | 运行正常 |  |
| vault/Bubble Bash 2.jar | 994 | Bubble Bash 2 | - | 1.1.3 | MIDP-2.0 | - | 24 | 16 | 600 | 63257984 | 0 | - | 运行正常 |  |
| vault/Bubble Bash 3.jar | 1224 | Bubble Bash 3 | - | 1.1.3 | MIDP-2.0 | - | 20 | 16 | 600 | 702965020 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/Bubble Bash Mania.jar | 955 | Bubble Bash Mania | - | 1.2.9 | MIDP-2.0 | - | 16 | 16 | 600 | 11805597068 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/Bubble Bash.jar | 320 | - | - | - | - | - | 0 | 16 | 0 | 0 | 0 | - | 非可用包 | not a zip file |
| vault/Bubble Bobble Evolution.jar | 545 | Bubble Bobble Evolution | - | 1.5.2 | MIDP-1.0 | - | 15 | 16 | 600 | 4252198 | 0 | java/lang/IllegalArgumentExcepti | 运行正常 |  |
| vault/Bubble Bobble ver1.0.2.jar | 66 | BubbleBobble | - | 1.0.2 | MIDP-1.0 | nokia-ui | 10 | 16 | 600 | 1680885 | 0 | - | 运行正常 |  |
| vault/Bubble Bobble.jar | 137 | Bubble Bobble | - | 1.4.9 | MIDP-2.0 | - | 12 | 16 | 600 | 423368 | 0 | - | 运行正常 |  |
| vault/Bubble Bobble_320x240.jar | 134 | Bubble Bobble | - | 1.4.9 | MIDP-2.0 | - | 12 | 16 | 600 | 2725442 | 0 | java/io/IOException | 运行正常 |  |
| vault/Bubble Boom Challenge 2__320x240.jar | 361 | BB_Challenge2 | - | 1.0.8 | MIDP-2.0 | - | 16 | 16 | 600 | 14205792 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/Bubble Pop_320x240.jar | 938 | BubblePop | - | 1.4.0 | MIDP-2.0 | nokia-ui | 37 | 16 | 600 | 6361366 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/Bubble Town 2_320x240.jar | 435 | Bubble Town 2 | - | 1.2.3 | MIDP-2.0 | - | 61 | 16 | 600 | 959325 | 0 | java/lang/ArrayIndexOutOfBoundsE | 运行正常 |  |
| vault/Bubble X Mania Deluxe_320x240.jar | 315 | Bublex Mania Deluxe | - | 1.0.0 | MIDP-2.0 | - | 20 | 16 | 600 | 3971478669 | 0 | java/io/IOException,javax/microe | 运行正常 |  |
| vault/BubbleBash.jar | 320 | Bubble Bash (mobilegamesaren | - | 1.0.1 | MIDP-2.0 | - | 9 | 16 | 600 | 4995842154 | 0 | - | 运行正常 |  |
| vault/BubbleBash1_4.0.0_320x240.jar | 432 | Bubble Bash | - | 1.0.0 | MIDP-2.0 | - | 8 | 16 | 600 | 4951093893 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/BubbleBash2_1.0.8_320x240.jar | 639 | Bubble Bash 2 | - | 1.0.9 | MIDP-2.0 | - | 20 | 16 | 600 | 30239986 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/BubbleGunV1.1_LaSgO.jar | 97 | BubbleGun | - | 1.0 | MIDP-1.0 | nokia-ui | 4 | 16 | 600 | 1747914190 | 0 | - | 运行正常 |  |
| vault/Bubble_Bash.jar | 275 | Bubble Bash | - | 1.0.3 | MIDP-2.0 | - | 8 | 16 | 600 | 3892550634 | 0 | - | 运行正常 |  |
| vault/Bubble_Tots_sek700.ic.cz.jar | 44 | Bubble Tots | - | 1.2.0 | MIDP-1.0 | nokia-ui | 21 | 16 | 600 | 4315300 | 0 | java/io/EOFException | 运行正常 |  |
| vault/Bubble_Trouble.jar | 46 | Bubble Trouble | - | 1.26 | MIDP-1.0 | nokia-ui | 2 | 16 | 600 | 1246541 | 0 | - | 运行正常 |  |
| vault/Bubbles.jar | 48 | Bubbles | - | 1.5.0 | MIDP-1.0 | nokia-ui | 12 | 16 | 600 | 26816 | 0 | java/io/IOException,java/lang/Nu | 运行正常 |  |
| vault/Bubblesoccer_sek700.wz.cz.jar | 80 | BubbleSoccer | - | 1.0 | MIDP-1.0 | - | 5 | 16 | 600 | 714911042 | 0 | - | 运行正常 |  |
| vault/BuffyS60.jar | 180 | Buffy, The Vampire Slayer | - | 1.1.1 | MIDP-1.0 | nokia-ui | 7 | 16 | 600 | 4245904 | 0 | - | 运行正常 |  |
| vault/BuggyFighter_K700F.jar | 102 | Buggy Fighter [terjef] | - | 1.0 | MIDP-2.0 | - | 23 | 16 | 600 | 8926964 | 0 | java/io/EOFException,java/io/IOE | 运行正常 | runs without input; the generic key script m |
| vault/Bugs Bunny - Rabbit Rescue.jar | 576 | Bugs Bunny | - | 1.0.9 | MIDP-2.0 | - | 38 | 16 | 600 | 3440861 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/Bugz.Buster.v1.1.0_XiMpDA.jar | 84 | BugzBuster | - | 1.1.0 | MIDP-1.0 | nokia-ui | 9 | 16 | 600 | 317433 | 0 | java/lang/ArrayIndexOutOfBoundsE | 运行正常 |  |
| vault/Bugz_Buster.jar | 90 | BugzBuster | - | 1.1.0 | MIDP-1.0 | nokia-ui | 16 | 16 | 600 | 299019 | 0 | java/lang/ArrayIndexOutOfBoundsE | 运行正常 |  |
| vault/Build-a-lot_320x240.jar | 337 | Build-a-lot serviak | - | 1.0.2 | MIDP-2.0 | - | 42 | 16 | 600 | 18218675 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/Bulldozer Inc.jar | 190 | BulldozerInc | - | 1.0 | MIDP-2.0 | - | 5 | 16 | 600 | 315201 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/Bumble_Beez_by_BFLM.jar | 46 | BumbleBeez | - | 1.0 | MIDP-1.0 | nokia-ui | 9 | 16 | 600 | 412062 | 0 | - | 运行正常 |  |
| vault/Bumper Car City.jar | 316 | Autoscooter | - | 1.0.58 | MIDP-2.0 | - | 14 | 16 | 600 | 18969582 | 0 | java/io/EOFException,java/lang/N | 运行正常 |  |
| vault/Burger Time Delight.jar | 265 | BurgerTime Delight | - | 1.0.0 | MIDP-2.0 | - | 38 | 16 | 600 | 6510060 | 0 | - | 运行正常 |  |
| vault/Burger Time Special.jar | 149 | Burger Time Special | - | 1.0.56 | MIDP-1.0 | - | 9 | 16 | 600 | 4751277 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/Burning Tires.jar | 412 | 3D Burning Tires | - | 1.3.6 | MIDP-2.0 | - | 115 | 16 | 600 | 3805776 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/Burnout 3D.jar | 371 | Burnout | - | 4.2.36 | MIDP-2.0 | - | 10 | 16 | 600 | 16499908 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/Burnout.jar | 354 | Burnout | - | 4.3.21 | MIDP-2.0 | - | 8 | 16 | 600 | 4671537 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/Burnout_2ri68qv5.jar | 412 | Burnout | - | 4.20.50 | MIDP-2.0 | - | 8 | 16 | 600 | 113574658 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/Burnout_4.3.34_320x240.jar | 400 | Burnout | - | 4.3.34 | MIDP-2.0 | - | 8 | 16 | 600 | 108680481 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/CSI Miami 176x208 CZ.jar | 299 | CSI MIAMI | - | 1.0.2 | MIDP-2.0 | nokia-ui | 6 | 16 | 600 | 182354076 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/CSI Miami.jar | 564 | CSI MIAMI by piratesxxl.110m | - | 1.0.4 | MIDP-2.0 | - | 6 | 16 | 600 | 215937229 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/CSI2Miami_1.0.5_320x240.jar | 693 | CSI: Miami 2 | - | 1.0.5 | MIDP-2.0 | - | 15 | 16 | 600 | 27324986 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/CSILasVegas_1.03_320x240.jar | 642 | CSI: Las Vegas | - | 1.0.3 | MIDP-2.0 | - | 8 | 16 | 600 | 10254853 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/CSIMiami2.jar | 945 | CSI: Miami 2 | - | 1.1.1 | MIDP-2.0 | nokia-ui | 15 | 16 | 600 | 27614549 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/CSIMiami_1.0.3_320x240.jar | 641 | CSI: Miami | - | 1.0.2 | MIDP-2.0 | - | 6 | 16 | 600 | 231688551 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/CSINewYork.jar | 952 | CSI NY The mobile game | - | 1.0.3 | MIDP-2.0 | - | 8 | 16 | 600 | 97518647 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/CSINewYork_1.1.2_320x240.jar | 375 | CSI: New York | - | 1.1.2 | MIDP-2.0 | - | 7 | 16 | 600 | 49200719 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/CSI_MIAMI_S60V3_N73_240x320.jar | 594 | CSI: MIAMI | - | 1.0.0 | MIDP-1.0 | - | 6 | 16 | 600 | 3028059785 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/CSI_MIAMI_sonyericsson_k700_CZ_IGP_EUROPE_103.jar | 300 | CSI MIAMI | - | 1.0.3 | MIDP-2.0 | - | 6 | 16 | 600 | 206478399 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/CSI_nokia_6270_6280_EN_IGP_EUROPE_103.jar | 618 | CSI The mobile game | - | 1.0.3 | MIDP-2.0 | - | 6 | 16 | 600 | 8888320 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/Caesar.jar | 267 | CAESAR | - | 0.4.6 | MIDP-1.0 | nokia-ui | 5 | 16 | 600 | 8944016 | 0 | - | 运行正常 |  |
| vault/CafeKakuro.jar | 294 | Cafe Kakuro | - | 1.1.60 | MIDP-2.0 | nokia-ui | 20 | 16 | 600 | 38613759 | 0 | java/io/EOFException,java/lang/N | 运行正常 |  |
| vault/Cake Mania - My Story.jar | 967 | Cake Mania | - | 1.0.1 | MIDP-2.0 | - | 43 | 16 | 600 | 2600879 | 0 | java/io/EOFException,javax/micro | 运行正常 |  |
| vault/California Gold Rush - Bonanza!.jar | 1227 | CGRBonanza | - | 1.0.8 | MIDP-2.0 | - | 71 | 16 | 600 | 837406 | 0 | java/io/EOFException,javax/micro | 运行正常 |  |
| vault/California Gold Rush Bonanza_1.0.7_320x240.jar | 841 | California Gold Rush | - | 1.0.4 | MIDP-2.0 | nokia-ui | 62 | 16 | 600 | 1762074 | 0 | java/io/EOFException,javax/micro | 运行正常 |  |
| vault/Cannon Rats.jar | 1050 | Cannon Rats | - | 1.1.5 | MIDP-2.0 | - | 49 | 16 | 600 | 197684627 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/CannonRats_1.1.6_320x240.jar | 819 | Cannon Rats | - | 1.1.6 | MIDP-2.0 | - | 49 | 16 | 600 | 228578 | 0 | - | 运行正常 | self-paced title: runs 600 frames unpaced (t |
| vault/Carcassonne_1.0.8_320x240.jar | 652 | Carcassonne | - | 1.0.8 | MIDP-2.0 | nokia-ui | 82 | 16 | 600 | 2654061161 | 0 | java/io/IOException,javax/microe | 运行正常 |  |
| vault/Castle of Magic.jar | 946 | Castle of Magic | - | 1.1.3 | MIDP-2.0 | - | 12 | 16 | 600 | 454394165 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/Castlevania - Aria of Sorrow.jar | 405 | Castlevania Aria Of Sorrow | - | 1.0.0 | MIDP-2.0 | - | 4 | 16 | 600 | 8945551 | 0 | javax/microedition/rms/InvalidRe | 运行正常 |  |
| vault/Castlevania - Dawn of Sorrow.jar | 610 | Castlevania Dawn of Sorrow | - | 1.0.0 | MIDP-2.0 | - | 19 | 16 | 600 | 4025754 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/Castlevania - Order of Shadows.jar | 521 | CastlevaniaOoS | - | 0.0.7 | MIDP-2.0 | - | 66 | 16 | 600 | 1037964 | 0 | javax/microedition/rms/InvalidRe | 运行正常 |  |
| vault/Castlevania Order Of Shadows_320x240.jar | 523 | Castlevania 2 | - | 0.0.10 | MIDP-2.0 | - | 66 | 16 | 601 | 1789336 | 0 | javax/microedition/rms/InvalidRe | 运行正常 |  |
| vault/Castlevania-AriaOfSorrow_N80.jar | 403 | Castlevania Aria Of Sorrow | - | 1.0.0 | MIDP-2.0 | - | 4 | 16 | 600 | 11739659 | 0 | javax/microedition/rms/InvalidRe | 运行正常 |  |
| vault/Catan The Seafarers_320x240_Nokia_E71.jar | 504 | Catan2 | - | 1.0.5 | MIDP-2.0 | nokia-ui | 110 | 16 | 600 | 3709018150 | 0 | java/io/IOException,javax/microe | 运行正常 |  |
| vault/CatanTheFirstIsland_1.0.2_320x240.jar | 418 | Catan | - | 1.0.2 | MIDP-2.0 | nokia-ui | 103 | 16 | 600 | 3530931127 | 0 | java/lang/NumberFormatException, | 运行正常 |  |
| vault/Chainz.jar | 101 | Chainz | - | 1.0.13 | MIDP-2.0 | nokia-ui | 12 | 16 | 600 | 671236129 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/ChampionshipStreetRacing_1.0.0_320x240.jar | 556 | ChampStreetRacing | - | 1.0.0 | MIDP-2.0 | - | 95 | 16 | 600 | 12432517 | 0 | java/io/IOException,java/lang/Nu | 运行正常 |  |
| vault/ChessMaster_1.0.2_320x240.jar | 189 | Chess Master | - | 1.0.2 | MIDP-2.0 | - | 6 | 16 | 600 | 2341667748 | 0 | - | 运行正常 |  |
| vault/Chessmaster (240x400).jar | 958 | Chessmaster | - | 1.0.5 | MIDP-2.0 | - | 9 | 16 | 600 | 884351884 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/Chessmaster.jar | 139 | Chessmaster | - | 1.0.4 | MIDP-1.0 | nokia-ui | 5 | 16 | 600 | 1892031811 | 0 | - | 运行正常 |  |
| vault/Chica_Interviu_Valencia_Sextron_by_BFLM.jar | 106 | Chica Interviu Valencia | - | 1.0.0 | MIDP-1.0 | nokia-ui | 3 | 16 | 600 | 127567 | 0 | - | 运行正常 |  |
| vault/Chickenrun_sek700.wz.cz.jar | 88 | ChickenRun | - | 1.0 | MIDP-1.0 | nokia-ui | 5 | 16 | 600 | 183728 | 0 | javax/microedition/rms/InvalidRe | 运行正常 |  |
| vault/ChickswithBricks.jar | 82 | Chicks-with-Bricks | - | 1.00 | MIDP-1.0 | - | 2 | 16 | 600 | 132155 | 0 | java/io/IOException,javax/microe | 运行正常 |  |
| vault/Chinese_Chess_sek700.ic.cz.jar | 85 | ChinaChess | - | 1.0 | MIDP-1.0 | nokia-ui | 11 | 16 | 600 | 527605080 | 0 | java/lang/IllegalArgumentExcepti | 运行正常 |  |
| vault/ChipsChallenge_K700.jar | 160 | Chips Chall | - | 1.0.3 | MIDP-1.0 | - | 25 | 16 | 600 | 861170495 | 0 | java/lang/NumberFormatException | 运行正常 |  |
| vault/Choc_o_bloc.jar | 77 | - | - | - | - | nokia-ui | 0 | 16 | 600 | 239708 | 0 | java/io/EOFException | 运行正常 |  |
| vault/Chopper_Rescue_by_BFLM.jar | 66 | Chopper | - | 1.0 | MIDP-1.0 | nokia-ui | 12 | 16 | 600 | 220298 | 0 | - | 运行正常 | runs without input; the generic key script m |
| vault/ChristmasBlockBreaker_K700_UK_100.jar | 147 | ChristmasBlockBreaker | - | 1.0.0 | MIDP-2.0 | - | 11 | 16 | 600 | 3416172657 | 0 | - | 运行正常 |  |
| vault/ChristmasMidnightPool_1.0.8_320x240.jar | 248 | Christmas Midnight Pool | - | 1.0.8 | MIDP-1.0 | - | 8 | 16 | 600 | 290361409 | 0 | - | 运行正常 |  |
| vault/Christmaskiss_sek700.wz.cz.jar | 61 | ChristmasKiss | - | 1.0 | MIDP-1.0 | nokia-ui | 7 | 16 | 600 | 97212 | 0 | - | 运行正常 |  |
| vault/Chuck Norris - Bring on the Pain.jar | 621 | Chuck Norris - Bring on the | - | 1.1.0 | MIDP-2.0 | - | 7 | 16 | 600 | 27381970 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/Chuck norris-176x220.jar | 584 | Chuck Norris - Bring on the  | - | 1.0.6 | MIDP-2.0 | nokia-ui | 7 | 16 | 600 | 8865436 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/ChuckNorrisBringOnThePain_1.0.2_320x240.jar | 600 | Chuck Norris - Bring on the  | - | 1.0.2 | MIDP-2.0 | - | 6 | 16 | 600 | 37658607 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/City Bloxx.jar | 290 | City Bloxx | - | 1.0.8 | MIDP-2.0 | m3g,nokia-ui | 19 | 16 | 0 | 715818 | 0 | java/lang/NullPointerException,j | 未呈现画面 | exited itself (notifyDestroyed) without pain |
| vault/City Cops.jar | 82 | CityCops | - | 1.1.0 | MIDP-1.0 | nokia-ui | 2 | 16 | 600 | 213041 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/CityAuto_K700F.jar | 63 | CityAuto [terjef] | - | 1.0 | MIDP-2.0 | - | 4 | 16 | 600 | 1125089 | 0 | javax/microedition/rms/InvalidRe | 运行正常 |  |
| vault/CityTycoon.jar | 216 | City Tycoon | - | 1.0 | MIDP-2.0 | - | 33 | 16 | 600 | 179786 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/Clash of the Titans - The Movie.jar | 713 | Clash of Titans Movie | - | 1.0.0 | MIDP-2.0 | - | 17 | 16 | 600 | 265234483 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/Climate Mission (640x360).jar | 6347 | Climate Mission | - | 1.3.11 | MIDP-2.0 | nokia-ui | 112 | 16 | 600 | 58790523 | 0 | javax/microedition/rms/InvalidRe | 运行正常 | runs without input; the generic key script m |
| vault/Colin McRae - DiRT 3D.jar | 867 | Colin McRae DiRT 3D srv | - | 1.0.5 | MIDP-1.0 | - | 39 | 16 | 600 | 14487631 | 0 | - | 运行正常 |  |
| vault/Colin McRae - DiRT.jar | 568 | Colin McRae DiRT | - | 1.2.5 | MIDP-1.0 | - | 21 | 16 | 600 | 5785988 | 0 | - | 运行正常 |  |
| vault/Command & Conquer 3 - Tiberium Wars.jar | 594 | Command & Conquer 3 - Tiberi | - | 4.1.64 | MIDP-2.0 | - | 29 | 16 | 600 | 2847419 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/Command & Conquer 4 - Tiberian Twilight.jar | 1097 | C&C4 Tiberian Twilight | - | 4.1.10 | MIDP-2.0 | - | 38 | 16 | 600 | 2466996 | 0 | - | 运行正常 |  |
| vault/Command And Conquer 3 Tiberium Wars_320x240_Nokia_6790.jar | 602 | Command & Conquer 3 - Tiberi | - | 4.3.81 | MIDP-2.0 | nokia-ui | 29 | 16 | 600 | 13997911 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/CommandAndConquer4TiberiumTwilight_4.1.8_320x240.jar | 1087 | C&C4 Tiberian Twilight [Mobi | - | 4.1.8 | MIDP-2.0 | - | 35 | 16 | 600 | 9958846 | 0 | - | 运行正常 |  |
| vault/CommandAndConquerRedAlertMobile_4.8.5_320x240.jar | 804 | C&C: Red Alert Mobile | - | 4.8.5 | MIDP-2.0 | - | 32 | 16 | 600 | 12456954 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/Commandos.jar | 609 | Commandos | - | 1.2.3 | MIDP-2.0 | - | 20 | 16 | 600 | 33509532 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/CommonKnowledge_1.0.5_320x240.jar | 336 | Common Knowledge Quiz | - | 1.0.5 | MIDP-2.0 | nokia-ui | 6 | 16 | 600 | 4219270386 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/CopsLAPolice_1.2.3_320x240.jar | 635 | COPS | - | 1.2.3 | MIDP-2.0 | - | 11 | 16 | 600 | 145969728 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/Cowboys & Aliens.jar | 1255 | Cowboys And Aliens | - | 1.1.8 | MIDP-2.0 | - | 16 | 16 | 600 | 192651415 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/Crash Arena 3D.jar | 379 | CrashArena 3D | - | 1.3.0 | MIDP-2.0 | - | 81 | 16 | 600 | 3692984 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/Crash Bamdicoot - Mutant Island.jar | 905 | Crash Mutant Island | - | 1.0.4 | MIDP-1.0 | - | 49 | 16 | 600 | 5360357 | 0 | java/io/IOException,java/lang/Nu | 运行正常 |  |
| vault/Crash Car Mania 3D.jar | 505 | Crash Car Mania by piguro | - | 1.5.1 | MIDP-2.0 | - | 36 | 16 | 600 | 3954829 | 0 | java/lang/ClassCastException,jav | 运行正常 |  |
| vault/Crash of the Titans.jar | 282 | Crash Titans | - | 1.2.1 | MIDP-2.0 | - | 11 | 16 | 600 | 332424 | 0 | - | 运行正常 |  |
| vault/CrashBandicootNitroKart2.jar | 396 | CBNK | - | 5.0.6 | MIDP-1.0 | - | 23 | 16 | 600 | 19154467 | 0 | java/io/IOException | 运行正常 |  |
| vault/Crazy Window Cleaners.jar | 473 | WindowCleaners | - | 1.0.52 | MIDP-2.0 | - | 15 | 16 | 600 | 3096060 | 0 | java/io/EOFException,java/lang/N | 运行正常 |  |
| vault/Cross Black.jar | 88 | Cross Black | - | 1.2 | MIDP-1.0 | - | 18 | 16 | 600 | 9879187 | 0 | javax/microedition/rms/InvalidRe | 运行正常 |  |
| vault/Crosspix.jar | 234 | Crosspix | - | 1.0.2 | MIDP-1.0 | - | 14 | 16 | 600 | 11662864 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/Cruizecontrol_sek700.wz.cz.jar | 54 | CruizeControl | - | 1.0 | MIDP-1.0 | nokia-ui | 7 | 16 | 600 | 477702 | 0 | - | 运行正常 |  |
| vault/Crunch Time Basketball__320x240.jar | 220 | CrunchTimeBasketball | - | 1.0.0 | MIDP-2.0 | - | 8 | 16 | 600 | 2770676 | 0 | - | 运行正常 |  |
| vault/Crystal Defenders.jar | 384 | Crystal Defenders | - | 1.0.0 | MIDP-2.0 | - | 17 | 16 | 600 | 7431381 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/Crystal Defenders_320x240.jar | 368 | Crystal Defenders TEGOS.ru | - | 1.0.0 | MIDP-2.0 | - | 18 | 16 | 600 | 14496586 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/Cthulhu.jar | 401 | Cthulhu_DW1 | - | 1.2.5 | MIDP-1.0 | nokia-ui | 28 | 16 | 600 | 2910473 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/Cubes.jar | 449 | Cubes | - | 1.0.13 | MIDP-2.0 | - | 23 | 16 | 601 | 557808423 | 0 | javax/microedition/media/MediaEx | 运行正常 |  |
| vault/Cubis2.jar | 246 | Cubis 2 | - | 1.2.3 | MIDP-2.0 | - | 12 | 16 | 600 | 3232224 | 0 | java/io/IOException | 运行正常 |  |
| vault/Cupid.jar | 77 | Cupid | - | 1.0 | MIDP-1.0 | nokia-ui | 2 | 16 | 600 | 786964 | 0 | java/io/IOException | 运行正常 |  |
| vault/Curling ver1.0.3.jar | 60 | Curling | - | 1.0.3 | MIDP-1.0 | nokia-ui | 16 | 16 | 600 | 204453 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/CyberPawVer2_sek700.ic.cz.jar | 70 | CyberPawVer2_NS60 | - | 2.17 | MIDP-1.0 | nokia-ui | 12 | 16 | 600 | 7831942028 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/Cyberpunk---The-Arasakas-Plot.jar | 392 | CyberPunk Arasakas Plot by K | - | 1.3.2 | MIDP-1.0 | nokia-ui | 40 | 16 | 600 | 1021054 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/Dakar 2008.jar | 409 | Dakar 2008 | - | 1.0.1 | MIDP-1.0 | - | 3 | 16 | 600 | 285362232 | 0 | - | 运行正常 |  |
| vault/Dakar 2009.jar | 849 | DAKAR Rally 2009 | - | 1.1.3 | MIDP-2.0 | - | 46 | 16 | 600 | 2974967 | 0 | java/io/IOException,java/lang/Nu | 运行正常 |  |
| vault/Dakar 2010.jar | 975 | DAKAR Rally 2010 | - | 0.1.8 | MIDP-2.0 | - | 47 | 16 | 600 | 10384560 | 0 | java/io/IOException,java/lang/Nu | 运行正常 |  |
| vault/Danger Dash.jar | 1282 | Danger Dash | - | 1.0.5 | MIDP-2.0 | - | 24 | 16 | 600 | 567152521 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/DarkWorld2.jar | 439 | Dark World 2_freemobilejavag | - | 1.0 | MIDP-2.0 | - | 26 | 16 | 600 | 250191 | 0 | - | 运行正常 |  |
| vault/DateOrDitch2_1.0.3_320x240.jar | 731 | Date Or Ditch 2 | - | 1.0.3 | MIDP-2.0 | - | 28 | 16 | 600 | 22931304 | 0 | java/lang/ClassNotFoundException | 运行正常 |  |
| vault/DateOrDitch_1.0.3_320x240.jar | 556 | Date or Ditch | - | 1.0.3 | MIDP-2.0 | - | 11 | 16 | 600 | 27059741 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/Death Race.jar | 609 | Death Race: The Mobile Game | - | 1.1.4 | MIDP-2.0 | - | 17 | 16 | 600 | 86994141 | 0 | javax/microedition/rms/InvalidRe | 运行正常 |  |
| vault/Death Trap 2 - The Unlocked Code.jar | 237 | Death Trap 2.TUC | - | 1.0.0 | MIDP-2.0 | - | 12 | 16 | 600 | 566189 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/Death Zone.jar | 306 | Death Zone serviak and Black | - | 1.0.0 | MIDP-2.0 | - | 24 | 16 | 600 | 638687205 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/DerekJeterProBaseball2009_1.2.0_320x240.jar | 601 | Derek Jeter Pro Barketball 2 | - | 1.2.0 | MIDP-2.0 | - | 10 | 16 | 617 | 16914902 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/DesperateHousewives_1.0.2_320x240.jar | 398 | Desperate Housewives | - | 1.0.2 | MIDP-2.0 | - | 8 | 16 | 600 | 3915115204 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/DetectiveRidleyAndTheMysteriousEnigmaa_1.06_320x240.jar | 916 | Detective Ridley And The Mys | - | 1.0.6 | MIDP-2.0 | - | 12 | 16 | 600 | 33734477 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/Dewy's Adventure.jar | 416 | Dewy's Adventure | - | 1.0.0 | MIDP-2.0 | - | 2 | 16 | 600 | 6765217759 | 0 | javax/microedition/rms/InvalidRe | 运行正常 |  |
| vault/Diamond Islands 2_320x240.jar | 1093 | Diamond Islands 2 | - | 1.0.18 | MIDP-2.0 | nokia-ui | 23 | 16 | 600 | 7405092 | 0 | java/io/EOFException,java/lang/C | 运行正常 |  |
| vault/Diamond Rush.jar | 296 | Diamond Rush | - | 1.2.0 | MIDP-2.0 | - | 10 | 16 | 600 | 1179769 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/Diamond Twister (1).jar | 594 | Diamond Twister | - | 1.0.8 | MIDP-2.0 | - | 6 | 16 | 600 | 9648119 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/Diamond Twister 2.jar | 1003 | Diamond Twister 2 | - | 1.1.3 | MIDP-2.0 | - | 8 | 16 | 601 | 7009717 | 0 | - | 运行正常 |  |
| vault/Diamond Twister 2_320x240.jar | 877 | Diamond Twister 2 | - | 1.0.5 | MIDP-2.0 | - | 8 | 16 | 601 | 8293436 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/DiamondIslands.jar | 385 | Diamond Islands | - | 1.0.11 | MIDP-2.0 | - | 11 | 16 | 601 | 8974031 | 0 | java/io/EOFException,javax/micro | 运行正常 |  |
| vault/DiamondIslands2.jar | 530 | Diamond Islands 2 | - | 1.0.21 | MIDP-2.0 | nokia-ui | 20 | 16 | 601 | 32762258 | 0 | java/io/EOFException,java/lang/A | 运行正常 |  |
| vault/DiamondQuest.jar | 440 | Paris Hilton's Diamond Quest | - | 1.0.0 | MIDP-2.0 | - | 7 | 16 | 600 | 1595238070 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/DiamondRush.jar | 408 | Diamond Rush by GDC | - | 1.0.1 | MIDP-2.0 | - | 9 | 16 | 600 | 3993320 | 0 | javax/microedition/rms/InvalidRe | 运行正常 |  |
| vault/DiamondRush_1.1.6_320x240.jar | 300 | Diamond Rush | - | 1.1.6 | MIDP-2.0 | - | 12 | 16 | 600 | 4173333 | 0 | javax/microedition/rms/InvalidRe | 运行正常 |  |
| vault/DiamondTwister.jar | 594 | Diamond Twister (mobilegames | - | 1.0.7 | MIDP-2.0 | - | 6 | 16 | 0 | 0 | 0 | - | 非可用包 | archive entry is corrupt (Error -3 while dec |
| vault/DiamondTwister2.jar | 807 | Diamond Twister 2 | - | 1.1.0 | MIDP-2.0 | - | 8 | 16 | 601 | 11057536 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/Dictator Defense.jar | 656 | DictatorDef | - | 1.0.18 | MIDP-2.0 | - | 32 | 16 | 600 | 2021228 | 0 | java/io/EOFException,javax/micro | 运行正常 |  |
| vault/Die Hard 4.0 - The Mobile Game.jar | 593 | Die Hard 4.0 | - | 1.0.6 | MIDP-2.0 | - | 5 | 16 | 600 | 591469450 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/Dig_Dug_240x400.jar | 192 | Dig Dug by Namco | - | 2.1.0 | MIDP-2.0 | - | 13 | 16 | 600 | 6028884767 | 0 | - | 运行正常 |  |
| vault/Diner Dash Flo On The Go_320x240.jar | 576 | Diner Dash Flo on the Go | - | 1.0.0 | MIDP-2.0 | - | 37 | 16 | 600 | 37725180 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/DiscoveringAngelsAndDemons.jar | 177 | AngelsAndDemons | - | 1.0.0 | MIDP-2.0 | - | 10 | 16 | 600 | 3907880 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/DisneyBoardGames.jar | 624 | Disney Board Games | - | 1.1.1 | MIDP-2.0 | - | 36 | 16 | 600 | 6935806 | 0 | java/io/EOFException,javax/micro | 运行正常 |  |
| vault/Dogz3D_1.1.5_320x240.jar | 932 | DogZ 3D | - | 1.1.5 | MIDP-2.0 | m3g | 20 | 16 | 600 | 57907163 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/DominoFever_1.0.3_320x240.jar | 400 | Domino Fever | - | 1.0.3 | MIDP-2.0 | - | 9 | 16 | 600 | 5603507 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/DonaldDuckQuestDeluxe.jar | 190 | Donald Duck's Quest Deluxe | - | 2.0.0 | MIDP-1.0 | - | 9 | 16 | 600 | 1913681 | 0 | java/io/EOFException,java/lang/N | 运行正常 |  |
| vault/Dont Pull_320x240.jar | 111 | Dont Pull | - | 1.0.0 | MIDP-2.0 | - | 6 | 16 | 600 | 430849286 | 0 | java/lang/ArrayIndexOutOfBoundsE | 运行正常 |  |
| vault/Doodle Jump.jar | 452 | Doodle Jump | - | 1.3.0 | MIDP-2.0 | - | 7 | 16 | 600 | 989325546 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/Doom II RPG.jar | 748 | Doom II RPG | - | 5.1.89 | MIDP-2.0 | - | 35 | 16 | 600 | 4648340 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/Doom RPG.jar | 295 | Doom RPG | - | 1.4.67 | MIDP-2.0 | - | 26 | 16 | 600 | 258335 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/Dragon How To Train Your Dragon_320x240_Nokia_E71.jar | 355 | Dragon Konon.Mobi | - | 1.0.0 | MIDP-2.0 | - | 10 | 16 | 600 | 7285652 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/Dragon Mania.jar | 1604 | Dragon Mania | - | 1.0.0 | MIDP-2.0 | - | 24 | 16 | 600 | 596170174 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/Driver - San Francisco.jar | 1265 | Driver San Francisco | - | 1.2.4 | MIDP-2.0 | - | 11 | 16 | 600 | 68282003 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/Driver L.A. Undercover.jar | 539 | Driver | - | 1.0.5 | MIDP-2.0 | - | 6 | 16 | 600 | 229352757 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/DriverLAUndercover_1.0.5_320x240.jar | 317 | Driver | - | 1.0.5 | MIDP-2.0 | nokia-ui | 8 | 16 | 600 | 9970789 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/DriverSanFrancisco_1.0.4_320x240.jar | 840 | Driver San Francisco | - | 1.0.4 | MIDP-2.0 | - | 12 | 16 | 600 | 169132757 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/Dungeon Hunter - Curse Of Heaven.jar | 1012 | Dungeon Hunter Curse of Heav | - | 1.0.0 | MIDP-2.0 | - | 16 | 16 | 600 | 50845586 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/Dungeon Hunter 3.jar | 1334 | DUNGEON HUNTER 3 | - | 1.3.0 | MIDP-2.0 | - | 13 | 16 | 600 | 240999944 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/DungeonHunter3_1.1.0_320x240.jar | 887 | Dungeon Hunter 3 | - | 1.1.0 | MIDP-2.0 | - | 14 | 16 | 600 | 5011599684 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/EA.Mobile.The.Sims.2.Pets.v4.4.44.jar | 390 | The Sims 2 Pets | - | 4.4.44 | MIDP-2.0 | - | 36 | 16 | 600 | 14181740 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/EAsnowboard.jar | 101 | EASnowboarding | - | 1.1.6 | MIDP-1.0 | nokia-ui | 16 | 16 | 600 | 114496 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/Earthworm Jim.jar | 1026 | Earthworm Jim | - | 1.1.7 | MIDP-2.0 | - | 17 | 16 | 600 | 112910616 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/EarthwormJim_1.1.7_320x240.jar | 675 | Earthworm Jim | - | 1.1.7 | MIDP-2.0 | nokia-ui | 17 | 16 | 600 | 121421284 | 0 | - | 运行正常 |  |
| vault/Easnow.jar | 96 | EASnowboarding | - | 1.1.6 | MIDP-1.0 | - | 9 | 16 | 600 | 114332 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/EdEddnEddy_Jawbrakers_by_BFLM.jar | 82 | Ed Edd n Eddy - Giant Jawbre | - | 1.0.1 | MIDP-1.0 | nokia-ui | 9 | 16 | 600 | 1170578 | 0 | - | 运行正常 |  |
| vault/Edens60_sek700.wz.cz.jar | 84 | Eden | - | 1.2.0 | MIDP-1.0 | nokia-ui | 5 | 16 | 600 | 4359903 | 0 | java/lang/IllegalArgumentExcepti | 运行正常 |  |
| vault/EdgeRally_K700.jar | 128 | EdgeRally | - | 02.00.02 | MIDP-2.0 | - | 10 | 16 | 600 | 16816600 | 0 | - | 运行正常 |  |
| vault/Edge_320x240_Nokia_E71.jar | 376 | Edge Konon.Mobi | - | 4.0.0 | MIDP-2.0 | - | 4 | 16 | 600 | 16484024 | 0 | javax/microedition/io/Connection | 运行正常 |  |
| vault/Egypt Quest II Dreamzone.jar | 140 | EQ2 Dreamzone | - | 1.11 | MIDP-1.0 | nokia-ui | 23 | 16 | 601 | 6633330 | 0 | java/lang/IllegalArgumentExcepti | 运行正常 |  |
| vault/Eidos_Hitman_Blood_Money_Vegas.jar | 137 | Hitman Ep1 Vegas | - | 1.2.4 | MIDP-1.0 | - | 9 | 16 | 600 | 737924 | 0 | - | 运行正常 |  |
| vault/El Che.jar | 386 | El Che | - | 1.0.4 | MIDP-2.0 | - | 26 | 16 | 600 | 2244141 | 0 | - | 运行正常 |  |
| vault/Election2004.jar | 86 | Election2004 | - | 1.1.2 | MIDP-1.0 | - | 11 | 16 | 600 | 710869 | 0 | java/io/IOException | 运行正常 |  |
| vault/Elektra.jar | 174 | Elektra | - | 2.2.5 | MIDP-1.0 | nokia-ui | 5 | 16 | 600 | 116135 | 0 | - | 运行正常 |  |
| vault/Elkware.Ancient.Towers.Of.Sarun.jar | 186 | Ancient Towers | - | 1.0.1 | MIDP-1.0 | nokia-ui | 6 | 16 | 600 | 5843316 | 0 | java/io/IOException,java/lang/Nu | 运行正常 |  |
| vault/ElkwareMarbleMiracle(by_wl).jar | 137 | Marble Miracle | - | 1.0.2 | MIDP-1.0 | nokia-ui | 4 | 16 | 600 | 6683 | 0 | java/lang/NullPointerException,j | 运行正常 | runs without input; the generic key script m |
| vault/Elope_sek700.wz.cz.jar | 74 | Elope | - | 1.0 | MIDP-1.0 | nokia-ui | 5 | 16 | 600 | 376775 | 0 | - | 运行正常 |  |
| vault/Elven Chronicles.jar | 233 | Elven Chronicles | - | 1.0.9 | MIDP-2.0 | - | 11 | 16 | 600 | 14479823 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/EverydayEnglishTrainer_1.0.2_320x240.jar | 605 | Everyday English Trainer | - | 1.0.2 | MIDP-2.0 | - | 4 | 16 | 600 | 6159886187 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/FIFA 2007.jar | 419 | FIFA 07 | - | 1.7.11 | MIDP-1.0 | - | 5 | 16 | 600 | 365342 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/FIFA_2003_by_BFLM.jar | 97 | FIFA 2003 | - | 0.6.2 | MIDP-1.0 | nokia-ui | 5 | 16 | 600 | 1018418 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/FIFA_2005_vXXX.jar | 118 | FIFA 2005 | - | 0.4.6 | MIDP-1.0 | - | 3 | 16 | 600 | 241161 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/FIFA_Football_2004_by_BFLM.jar | 113 | EA Sports FIFA Football 2004 | - | 0.5.1 | MIDP-1.0 | nokia-ui | 3 | 16 | 600 | 4019615 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/Family Guy - Uncensored.jar | 471 | Family Guy Uncensored | - | 1.0.4 | MIDP-2.0 | - | 10 | 16 | 600 | 7200005 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/Family Guy 2.jar | 808 | Family Guy 2 | - | 1.0.0 | MIDP-2.0 | - | 25 | 16 | 600 | 15171259 | 0 | java/lang/ArrayIndexOutOfBoundsE | 运行正常 |  |
| vault/FarCry 2.jar | 626 | Far Cry 2 | - | 1.0.8 | MIDP-2.0 | - | 17 | 16 | 600 | 126040750 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/FarCry2_1.0.8_320x240.jar | 634 | Far Cry 2 | - | 1.0.8 | MIDP-2.0 | - | 17 | 16 | 600 | 127766685 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/Farm Tycoon__320x240.jar | 739 | FarmTycoon | - | 1.0 | MIDP-2.0 | - | 19 | 16 | 600 | 5104164 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/Fast Five The Movie - Official Game.jar | 1226 | Fast Five | - | 1.2.2 | MIDP-2.0 | - | 12 | 16 | 600 | 21604947 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/Fast and Furious 6.jar | 1917 | Fast & Furious 6 | - | 1.1.2 | MIDP-2.0 | - | 19 | 16 | 600 | 54786277 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/FastAndFurious6_1.0.6_320x240.jar | 1287 | Fast & Furious 6 | - | 1.0.6 | MIDP-2.0 | - | 15 | 16 | 601 | 128302001 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/Ferrari GT - Evolution.jar | 739 | Ferrari GT | - | 1.0.0 | MIDP-2.0 | - | 14 | 16 | 600 | 2314691 | 0 | - | 运行正常 |  |
| vault/Ferrari GT 2 - Revolution.jar | 992 | Ferrari GT 2: Revolution | - | 1.1.2 | MIDP-2.0 | - | 19 | 16 | 601 | 202666626 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/Ferrari GT 3 - World Track.jar | 1562 | Ferrari GT 3: World Track | - | 1.3.3 | MIDP-2.0 | - | 36 | 16 | 600 | 71671324 | 0 | java/io/IOException,java/lang/Nu | 运行正常 |  |
| vault/Ferrari World Championship 2009.jar | 988 | Ferrari World Championship 2 | - | 1.0.6 | MIDP-2.0 | - | 15 | 16 | 600 | 26432938 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/Ferrari World Championship.jar | 595 | Ferrari World Championship | - | 1.0.8 | MIDP-2.0 | - | 13 | 16 | 600 | 31749912 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/FerrariGT2Revolution_1.0.9_320x240.jar | 689 | Ferrari GT 2 Revolution | - | 1.0.9 | MIDP-2.0 | nokia-ui | 19 | 16 | 600 | 178086278 | 0 | - | 运行正常 |  |
| vault/Fight Night Round 3 240x320.jar | 313 | Fight Night Round 3 | - | 4.6.0 | MIDP-2.0 | m3g | 25 | 16 | 600 | 11160844 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/File_Protect.jar | 29 | FileProtect | - | 1.0 | MIDP-2.0 | - | 29 | 16 | 600 | 121 | 0 | - | 运行正常 | runs without input; the generic key script m |
| vault/Final Fantasy II.jar | 1343 | Final Fantasy II | - | 1.0.0 | MIDP-2.0 | - | 13 | 16 | 600 | 5323413 | 0 | java/lang/ArrayIndexOutOfBoundsE | 运行正常 |  |
| vault/Final Fantasy.jar | 1148 | Final Fantasy | - | 1.0.0 | MIDP-2.0 | - | 12 | 16 | 600 | 3555013 | 0 | - | 运行正常 |  |
| vault/FinalFantasy1_1.0.0_320x240_Nokia_E71.jar | 1153 | Final Fantasy | - | 1.0.0 | MIDP-2.0 | - | 13 | 16 | 600 | 5839244 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/FinalFantasyII_1.0.0_320x240.jar | 998 | Final fantasy II | - | 1.0.0 | MIDP-2.0 | - | 14 | 16 | 600 | 15133788 | 0 | java/lang/ArrayIndexOutOfBoundsE | 运行正常 |  |
| vault/FinalFury_176x220.jar | 149 | FinalFury on MOBERs | - | 1.0 | MIDP-1.0 | - | 13 | 16 | 600 | 1876481 | 0 | - | 运行正常 |  |
| vault/Finding_Nemo.jar | 65 | Finding Nemo | - | 1.0 | MIDP-1.0 | - | 13 | 16 | 600 | 1113301 | 0 | - | 运行正常 |  |
| vault/Fire[1].Shark.jar | 144 | FireShark | - | 1.2 | MIDP-1.0 | nokia-ui | 11 | 16 | 600 | 787024 | 0 | - | 运行正常 |  |
| vault/Firefight_sek700.wz.cz.jar | 51 | FireFight | - | 1.0 | MIDP-1.0 | nokia-ui | 5 | 16 | 600 | 117579 | 0 | - | 运行正常 |  |
| vault/Fish Tycoon 240x320.jar | 294 | Fish Tycoon | - | 1.0.1 | MIDP-2.0 | - | 16 | 16 | 600 | 4841159 | 0 | - | 运行正常 |  |
| vault/FiveStones.jar | 90 | FiveStones | - | 1.1 | MIDP-2.0 | - | 22 | 16 | 600 | 68415 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/Flat Out Racing 3D_320x240_Nokia_E71.jar | 344 | FlatOut Racing3D | - | 0.5.4 | MIDP-2.0 | m3g | 54 | 16 | 626 | 91560339 | 0 | java/io/IOException,java/lang/Cl | 运行正常 |  |
| vault/FlatOut 3D.jar | 348 | FlatOut Racing3D | - | 0.4.9 | MIDP-2.0 | - | 54 | 16 | 600 | 68779800 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/FlatOut.jar | 402 | FlatOut Racing | - | 0.5.7 | MIDP-2.0 | - | 22 | 16 | 600 | 24841953 | 0 | java/lang/IllegalArgumentExcepti | 运行正常 |  |
| vault/FlaĹˇka 3.jar | 80 | Flaska 3 | - | 1.0 | MIDP-2.0 | - | 5 | 16 | 0 | 0 | 0 | - | 超时 |  |
| vault/Flexis.Extreme..jar | 163 | Flexis Extreme [Series60v3.C | - | 1.0.5 | MIDP-1.0 | nokia-ui | 22 | 16 | 600 | 7636648 | 0 | java/io/EOFException,java/lang/A | 运行正常 |  |
| vault/Fling_320x240.jar | 306 | Fling | - | 0.1.8 | MIDP-2.0 | nokia-ui | 30 | 16 | 600 | 758679 | 0 | java/io/IOException,java/lang/Nu | 运行正常 |  |
| vault/FortBoyard_1.0.2_320x240.jar | 577 | Fort Boyard | - | 1.0.2 | MIDP-2.0 | nokia-ui | 14 | 16 | 600 | 12260007 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/Frantic Factory_320x240.jar | 361 | Frantic Factory | - | 1.0.1 | MIDP-1.0 | - | 12 | 16 | 600 | 17074559 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/Frogger Beats n' Bounces.jar | 216 | Frogger Beats n Bounces | - | 1.0.7 | MIDP-2.0 | - | 2 | 16 | 601 | 880065 | 0 | - | 运行正常 |  |
| vault/Frogger Evolution.jar | 224 | Frogger Evolution | - | 1.0.0 | MIDP-2.0 | - | 2 | 16 | 600 | 4709889 | 0 | - | 运行正常 |  |
| vault/FrozenPenguin2_1.0_320x240.jar | 515 | Frozen Penguin 2 | - | 1.0 | MIDP-2.0 | - | 44 | 16 | 600 | 36612247 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/FruitZum_1.0_320x240.jar | 252 | Fruit Zum | - | 1.0 | MIDP-2.0 | - | 12 | 16 | 600 | 2219306 | 0 | java/io/IOException,java/lang/Nu | 运行正常 |  |
| vault/Fullmetal Fortress_320x240.jar | 114 | FULLMETAL FORTRESS | - | 1.1.0 | MIDP-1.0 | - | 7 | 16 | 600 | 82807 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/FurbyIsland.jar | 372 | Furby Island | - | 1.0.17 | MIDP-2.0 | - | 17 | 16 | 600 | 3050124 | 0 | java/lang/ArrayIndexOutOfBoundsE | 运行正常 |  |
| vault/G.I. Joe - The Rise of Cobra.jar | 678 | G.I. JOE | - | 7.2.50 | MIDP-2.0 | - | 13 | 16 | 600 | 1006582 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/GT Racing - Motor Academy.jar | 992 | GT Racing: Motor Academy | - | 1.2.2 | MIDP-2.0 | - | 12 | 16 | 600 | 2878434 | 0 | - | 运行正常 |  |
| vault/GT Racing 2 - The Real Car Experience.jar | 1812 | GT Racing 2 | - | 1.0.9 | MIDP-2.0 | - | 11 | 16 | 600 | 17605209 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/Galaga 3.jar | 141 | Galaga 3 | - | 1.0.2 | MIDP-2.0 | - | 13 | 16 | 600 | 12001759 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/Galaga X.jar | 505 | Galaga X | - | 1.0.5 | MIDP-2.0 | - | 33 | 16 | 600 | 40249450 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/Galaga3.jar | 141 | Galaga 3 | - | 1.0.2 | MIDP-2.0 | - | 13 | 16 | 600 | 4395414240 | 0 | java/util/NoSuchElementException | 运行正常 |  |
| vault/Galazer Deluxe.jar | 715 | Galazer Deluxe | - | 1.1.6 | MIDP-2.0 | - | 63 | 16 | 600 | 428041 | 0 | - | 运行正常 |  |
| vault/Gameloft Brother In Arms Earned In Bloods 1.2.4.jar | 222 | Brothers In Arms | - | 1.2.5 | MIDP-2.0 | nokia-ui | 7 | 16 | 600 | 2813058104 | 0 | java/io/IOException | 运行正常 |  |
| vault/Gameloft Christmas Block Breaker.jar | 148 | ChristmasBlockBreaker | - | 1.0.2 | MIDP-1.0 | nokia-ui | 8 | 16 | 600 | 2181523458 | 0 | - | 运行正常 |  |
| vault/Gameloft Midnight Bowling.jar | 217 | MidnightBowling [stuart2773] | - | 1.0.7 | MIDP-2.0 | - | 4 | 16 | 600 | 4087002156 | 0 | java/lang/IllegalArgumentExcepti | 运行正常 |  |
| vault/Gameloft.Mystery.Mansion.Pinball.1.2.1-Retail.JAVA.teamSMPDA.jar | 222 | MysteryMansionPinball | - | 1.2.1 | MIDP-1.0 | nokia-ui | 5 | 16 | 600 | 7449422396 | 0 | javax/microedition/rms/InvalidRe | 运行正常 |  |
| vault/Gangstar - Crime City.jar | 324 | Gangstar - Crime City | - | 1.0.3 | MIDP-2.0 | - | 5 | 16 | 600 | 471501210 | 0 | javax/microedition/rms/InvalidRe | 运行正常 |  |
| vault/Gangstar - Miami Vindication.jar | 1268 | GangStar: Miami Vindication | - | 1.1.8 | MIDP-2.0 | - | 10 | 16 | 600 | 424838050 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/Gangstar 2 - Kings of L.A..jar | 987 | Gangstar 2 | - | 1.0.7 | MIDP-2.0 | - | 11 | 16 | 600 | 360167875 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/Gangstar City.jar | 1621 | Gangstar City | - | 1.1.1 | MIDP-2.0 | - | 16 | 16 | 600 | 547991627 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/Gangstar Rio - City of Saints.jar | 1449 | Gangstar Rio: City of Saints | - | 1.0.9 | MIDP-2.0 | - | 13 | 16 | 600 | 274430988 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/Gangstar1CrimeCity_1.0.0_320x240.jar | 321 | Gangstar Crime City | - | 1.0.0 | MIDP-2.0 | - | 6 | 16 | 600 | 4850430890 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/Gangstar2KingsOfLA_1.1.2_320x240.jar | 692 | Gangstar 2: Kings of L.A | - | 1.1.2 | MIDP-2.0 | - | 11 | 16 | 600 | 1246282 | 0 | - | 运行正常 | self-paced title: runs 600 frames unpaced (t |
| vault/Gangstar3MiamiVindication_1.0.0_320x240.jar | 348 | Gangstar Miami | - | 1.0.0 | MIDP-2.0 | - | 5 | 16 | 600 | 5268994736 | 0 | javax/microedition/rms/InvalidRe | 运行正常 |  |
| vault/Gauntlet_320x240.jar | 969 | Gauntlet | - | 4.2.3 | MIDP-2.0 | - | 23 | 16 | 601 | 4224045 | 0 | - | 运行正常 |  |
| vault/GemDropDeluxe.jar | 1243 | Gem Drop Deluxe | - | 1.0.3 | MIDP-2.0 | nokia-ui | 2 | 16 | 600 | 4786712905 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/GhostRecon2AdvancedWarfighter_1.0.0_320x240.jar | 330 | G.R.A.W. 2 | - | 1.0.0 | MIDP-2.0 | - | 6 | 16 | 600 | 2366708204 | 0 | - | 运行正常 |  |
| vault/GianaSisters.jar | 103 | Super Mario | - | 1.0.2 | MIDP-2.0 | nokia-ui | 23 | 16 | 600 | 2336651 | 0 | java/io/IOException,java/lang/Nu | 运行正常 |  |
| vault/Go Go Rescue Squad_320x240.jar | 393 | Go! Go! Rescue Squad | - | 0.0.8 | MIDP-2.0 | - | 12 | 16 | 600 | 27495749 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/God of War - Betrayal.jar | 488 | God Of War | - | 1.4.46 | MIDP-2.0 | - | 6 | 16 | 600 | 142966717 | 0 | java/lang/NumberFormatException, | 运行正常 |  |
| vault/GoldDigger.jar | 924 | Gold Digger | - | 1.0 | MIDP-2.0 | nokia-ui | 23 | 16 | 600 | 420055861 | 0 | java/lang/IllegalArgumentExcepti | 运行正常 |  |
| vault/GoldenSolitaire.jar | 233 | Golden Solitaire | - | 1.0.3 | MIDP-2.0 | - | 17 | 16 | 600 | 47510827 | 0 | javax/microedition/rms/InvalidRe | 运行正常 |  |
| vault/Gorilla Rampage.jar | 957 | GorillaAngriff | - | 1.0.1 | MIDP-2.0 | - | 38 | 16 | 600 | 742024 | 0 | java/io/EOFException,javax/micro | 运行正常 |  |
| vault/Green Farm 3.jar | 1435 | Green Farm 3 | - | 1.0.7 | MIDP-2.0 | - | 22 | 16 | 600 | 63728788 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/Guitar Hero - Warriors of Rock Mobile More Music.jar | 615 | Guitar Hero Warriors of Rock | - | 3.1.0 | MIDP-2.0 | - | 25 | 16 | 600 | 20853841 | 0 | java/lang/ArrayIndexOutOfBoundsE | 运行正常 |  |
| vault/Guitar Hero - Warriors of Rock Mobile.jar | 1013 | Guitar Hero Warriors of Rock | - | 1.0.0 | MIDP-2.0 | - | 24 | 16 | 600 | 25984147 | 0 | javax/microedition/rms/InvalidRe | 运行正常 |  |
| vault/Guitar Hero World Your - Mobile.jar | 720 | Guitar Hero World Tour | - | 1.0.11 | MIDP-2.0 | - | 33 | 16 | 600 | 1002215 | 0 | java/io/IOException | 运行正常 |  |
| vault/Guitar Rock Tour.jar | 598 | Guitar Rock Tour | - | 1.0.8 | MIDP-2.0 | - | 6 | 16 | 600 | 321189571 | 0 | java/lang/NumberFormatException | 运行正常 |  |
| vault/Gumball Journey to the Moon-3-240x320.jar | 794 | Gumball | - | 1.1.0 | MIDP-2.0 | - | 15 | 16 | 600 | 1645428 | 0 | - | 运行正常 |  |
| vault/GunFever.jar | 167 | GunFever | - | 1.0.4 | MIDP-2.0 | - | 9 | 16 | 600 | 540289 | 0 | - | 运行正常 |  |
| vault/Guns, Wheels & Madheads 2.jar | 310 | Guns Wheels Madheads 2 | - | 1.1.0 | MIDP-2.0 | - | 27 | 16 | 600 | 708419 | 0 | - | 运行正常 |  |
| vault/Guns, Wheels & Madheads.jar | 329 | Guns Wheels Madheads | - | 1.1.0 | MIDP-2.0 | - | 28 | 16 | 600 | 80131483 | 0 | - | 运行正常 |  |
| vault/HarryPotterAndTheDeathlyHallowsPart2_1.2.4_320x240.jar | 956 | Harry Potter 7 P2 | - | 1.2.4 | MIDP-2.0 | - | 17 | 16 | 600 | 22895422 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/Hero of Sparta.jar | 993 | Hero Of Sparta | - | 1.1.9 | MIDP-2.0 | - | 9 | 16 | 600 | 636602976 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/HeroOfSparta_1.0.2_320x240.jar | 602 | Hero Of Sparta | - | 1.0.2 | MIDP-2.0 | - | 9 | 16 | 606 | 533867866 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/HeroesLore.jar | 827 | Heroes Lore by SunYu_911 | - | 2.0.7 | MIDP-2.0 | - | 90 | 16 | 601 | 4923614 | 0 | java/lang/ClassCastException,jav | 运行正常 |  |
| vault/Heroes_1.0.0_320x240.jar | 536 | Heroes | - | 1.0.0 | MIDP-2.0 | - | 5 | 16 | 600 | 132930165 | 0 | java/lang/ArrayIndexOutOfBoundsE | 运行正常 |  |
| vault/Hexic.jar | 198 | Hexic | - | 1.0.9 | MIDP-2.0 | - | 12 | 16 | 600 | 5894907 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/HugoFernando_K500 (mountin jacker).jar | 129 | Hugo & Fernando [mountin jac | - | 1.0.0 | MIDP-1.0 | - | 14 | 16 | 600 | 1246083 | 0 | java/io/EOFException,javax/micro | 运行正常 |  |
| vault/Hugo_Fishing_BFLM.jar | 100 | Hugo goes fishing | - | 1.27 | MIDP-1.0 | nokia-ui | 14 | 16 | 600 | 6034266 | 0 | java/io/EOFException,java/io/IOE | 运行正常 |  |
| vault/Hugo_Follow_Monkey_by_BFLM.jar | 100 | Hugo Follow the Monkey | - | 1.2.5 | MIDP-1.0 | nokia-ui | 13 | 16 | 600 | 4370104 | 0 | java/io/EOFException,java/io/IOE | 运行正常 |  |
| vault/Hugo_Snow_BFLM.jar | 98 | Hugo in the snow | - | 1.28 | MIDP-1.0 | nokia-ui | 10 | 16 | 600 | 793130 | 0 | java/io/EOFException,java/io/IOE | 运行正常 |  |
| vault/Hugo_Xmas_Snow_BFLM.jar | 98 | Hugo in the Xmas Snow | - | 1.2.8 | MIDP-1.0 | nokia-ui | 10 | 16 | 600 | 1361212 | 0 | java/io/EOFException,java/io/IOE | 运行正常 |  |
| vault/HungryWorming_BFLM.jar | 191 | HungryWorming | - | 1.1 | MIDP-1.0 | nokia-ui | 41 | 16 | 600 | 5969157202 | 0 | java/lang/ClassCastException | 运行正常 |  |
| vault/Hungry_Fish_by_merdoc.jar | 77 | - | - | - | - | - | 0 | 16 | 0 | 0 | 0 | - | 非可用包 | not a usable MIDlet suite (bad JAR or no MID |
| vault/IQ Booster Logic__320x240.jar | 508 | IQ Booster Logic | - | 1.0.0 | MIDP-2.0 | - | 101 | 16 | 600 | 44432341 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/Ice Age 2 Arctic Slide.jar | 368 | Ice Age 2 | - | 1.0.1 | MIDP-1.0 | - | 5 | 16 | 600 | 4654019 | 0 | java/io/EOFException,javax/micro | 运行正常 |  |
| vault/Ice Age 4 - Continental Drift.jar | 1527 | Ice Age 4: Continental Drift | - | 1.1.6 | MIDP-2.0 | - | 17 | 16 | 600 | 196936691 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/Ice Age Scrat-ventures.jar | 1016 | ICE AGE SCRAT-VENTURES | - | 1.0.1 | MIDP-2.0 | - | 24 | 16 | 600 | 84498029 | 0 | - | 运行正常 |  |
| vault/Ice Age Village.jar | 1791 | Ice Age Village | - | 1.0.9 | MIDP-2.0 | - | 19 | 16 | 600 | 196237724 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/IceAge2.Sony.Ericsson.W800i.K750i.D750i.W810i.jar | 212 | Ice Age 2 | - | 1.1.0 | MIDP-1.0 | - | 29 | 16 | 600 | 1221896 | 0 | java/io/EOFException,javax/micro | 运行正常 |  |
| vault/IceAge2ArcticSlide_176x220.jar | 192 | Ice Age 2 **MASYAKA** | - | 1.0.1 | MIDP-1.0 | - | 5 | 16 | 600 | 4643929 | 0 | java/io/EOFException,javax/micro | 运行正常 |  |
| vault/IceAgeSkater.jar | 130 | IceAgeSkater | - | 1.00 | MIDP-1.0 | nokia-ui | 14 | 16 | 600 | 1993909 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/IceBladePenguin_12.jar | 60 | IceBladePenguin | - | 1.2 | MIDP-1.0 | nokia-ui | 9 | 16 | 600 | 6314 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/Ice_Hockey_2003.jar | 101 | IceHockey2003 | - | 1.1.2 | MIDP-1.0 | nokia-ui | 10 | 16 | 600 | 192380 | 0 | - | 运行正常 |  |
| vault/IdolLines.jar | 295 | Idol Lines (www.mobilegamesa | - | 1.0 | MIDP-2.0 | - | 10 | 16 | 600 | 3042515 | 0 | java/io/IOException,java/lang/Ar | 运行正常 |  |
| vault/Immortals.jar | 1387 | Immortals | - | 1.0.7 | MIDP-2.0 | - | 13 | 16 | 600 | 130566992 | 0 | - | 运行正常 |  |
| vault/IncaQuest.jar | 301 | Inca Quest | - | 0.8.1 | MIDP-1.0 | - | 16 | 16 | 600 | 4391161 | 0 | java/io/IOException,javax/microe | 运行正常 |  |
| vault/IncaStory.jar | 152 | IncaStory | - | 1.0 | MIDP-2.0 | - | 4 | 16 | 600 | 3625692518 | 0 | - | 运行正常 |  |
| vault/Iron Man 2.jar | 1074 | Iron Man 2 | - | 1.1.4 | MIDP-2.0 | - | 9 | 16 | 600 | 133717447 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/Iron Man 3.jar | 1753 | Iron Man 3 | - | 1.1.2 | MIDP-2.0 | - | 12 | 16 | 600 | 166184845 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/IstanBUL.jar | 2793 | istanBUL | - | 1.0.0 | MIDP-2.0 | - | 21 | 16 | 600 | 516513 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/JMEBoy_with-games.jar | 698 | JMEBoy | - | 1.5.3 | MIDP-2.0 | nokia-ui | 94 | 16 | 600 | 2010203 | 0 | java/lang/NullPointerException,j | 运行正常 | painted 604 frames but made no draw calls (b |
| vault/James Cameron's Avatar.jar | 998 | Avatar | - | 1.0.6 | MIDP-2.0 | - | 9 | 16 | 600 | 21523502 | 0 | - | 运行正常 |  |
| vault/JetSetWilly.jar | 177 | Jet Set Willy | - | 1.0.1 | MIDP-1.0 | nokia-ui | 14 | 16 | 600 | 1090390 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/Jewel Conquest__320x240.jar | 270 | Jewel | - | 1.0 | MIDP-2.0 | - | 8 | 16 | 600 | 7198049 | 0 | - | 运行正常 |  |
| vault/Jewel Explosion_320x240.jar | 354 | Jewel Explosion | - | 1.0.0 | MIDP-2.0 | - | 35 | 16 | 600 | 2191813111 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/Jewel Quest III World Adventure_320x240_Nokia_E71.jar | 764 | Jewel Quest III World Advent | - | 4.5.57 | MIDP-2.0 | - | 25 | 16 | 600 | 10956380 | 0 | - | 运行正常 |  |
| vault/JewelPack2in1_2.0_320x240.jar | 394 | Jewel Pack 2in1 | - | 2.0 | MIDP-2.0 | - | 77 | 16 | 600 | 1298435686 | 0 | java/io/IOException,java/lang/Nu | 运行正常 |  |
| vault/JewelQuest.jar | 164 | Jewel Quest | - | 1.0.5 | MIDP-1.0 | - | 19 | 16 | 600 | 440266 | 0 | java/io/IOException | 运行正常 |  |
| vault/JewelQuest2.jar | 315 | JewelQuest2 | - | 0.3.2 | MIDP-1.0 | - | 14 | 16 | 600 | 1170914 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/JewelQuest3.jar | 585 | Jewel Quest III World Advent | - | 4.2.84 | MIDP-2.0 | - | 25 | 16 | 600 | 2680492 | 0 | - | 运行正常 |  |
| vault/JewelQuestDeluxe.jar | 743 | Jewel Quest Deluxe | - | 1.1.2 | MIDP-2.0 | - | 5 | 16 | 600 | 402167 | 1 | java/io/IOException,java/lang/Cl | 标题自身异常 | game code threw an exception in a thread whi |
| vault/Jurassic Park.jar | 1102 | Jurassic Park | - | 1.1.2 | MIDP-2.0 | - | 12 | 16 | 600 | 141527149 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/JurassicPark_1.2.2_320x240.jar | 911 | Jurassic Park | - | 1.2.2 | MIDP-2.0 | - | 12 | 16 | 600 | 267911928 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/K.O. Legends.jar | 1024 | K.O. Legends | - | 1.0.6 | MIDP-2.0 | - | 23 | 16 | 600 | 226749888 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/KO.Legends-176x220.jar | 282 | KO Legends by piratesxxl.110 | - | 1.0.1 | MIDP-2.0 | - | 9 | 16 | 600 | 30352016 | 0 | java/lang/ArrayIndexOutOfBoundsE | 运行正常 |  |
| vault/KOFighters_1.0.0_320x240.jar | 323 | KO Fighters | - | 1.0.0 | MIDP-2.0 | - | 12 | 16 | 600 | 24902961 | 0 | java/lang/ArrayIndexOutOfBoundsE | 运行正常 |  |
| vault/KO_Kickboxing_by_BFLM.jar | 62 | KO Kickboxing | - | 1.0.0 | MIDP-1.0 | nokia-ui | 13 | 16 | 600 | 4073 | 0 | - | 运行正常 |  |
| vault/Kartmania 3D.jar | 934 | Kartmania3D | - | 1.0.2 | MIDP-2.0 | - | 123 | 16 | 600 | 4141190 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/KasparovChess.jar | 189 | Kasparov Chess | - | 1.0.1 | MIDP-1.0 | - | 6 | 16 | 600 | 5061299 | 0 | javax/microedition/rms/InvalidRe | 运行正常 |  |
| vault/King Kong.jar | 297 | KingKong | - | 1.0.3 | MIDP-2.0 | - | 5 | 16 | 600 | 60703102 | 0 | - | 运行正常 |  |
| vault/KnockOut_Champs_RoS_S60.jar | 117 | KOChamps (RoS) | - | 1.0.1 | MIDP-1.0 | nokia-ui | 3 | 16 | 600 | 4341582333 | 0 | - | 运行正常 |  |
| vault/KobeBryantProBasketball2008.jar | 314 | PROBASKET 2008 by D@nilYcH | - | 1.0.1 | MIDP-2.0 | - | 8 | 16 | 600 | 3820380187 | 0 | java/lang/ArrayIndexOutOfBoundsE | 运行正常 |  |
| vault/Kobe_Bryant_Pro_Basketball_2008_176x220.jar | 535 | PRO BASKET 2008 | - | 1.0.2 | MIDP-2.0 | - | 8 | 16 | 600 | 13701918 | 0 | java/lang/ArrayIndexOutOfBoundsE | 运行正常 |  |
| vault/KoraDeluxe3D_1.0.4_320x240.jar | 947 | KORa Deluxe | - | 1.0.4 | MIDP-2.0 | m3g | 123 | 16 | 600 | 199578731 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/KungFu.jar | 63 | Kung Fu | - | 1.0.5 | MIDP-1.0 | nokia-ui | 6 | 16 | 600 | 1113839 | 0 | - | 运行正常 |  |
| vault/KungFuPanda_4.9.12_320x240.jar | 544 | Kung Fu Panda | - | 4.9.12 | MIDP-2.0 | nokia-ui | 26 | 16 | 600 | 13579922 | 0 | java/lang/IllegalMonitorStateExc | 运行正常 |  |
| vault/Kung_Fu_Panda_240x320.jar | 290 | Kung Fu Panda | - | 4.3.92 | MIDP-2.0 | - | 24 | 16 | 600 | 12104556 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/Kungfu.jar | 163 | Kung Fu | - | 1.0.5 | MIDP-1.0 | nokia-ui | 7 | 16 | 600 | 11094045 | 0 | - | 运行正常 |  |
| vault/LOST.jar | 308 | LOST | - | 1.0.2 | MIDP-2.0 | - | 6 | 16 | 600 | 346817663 | 0 | - | 运行正常 |  |
| vault/Lara Crofts Poker Party (CLDC-1.0, MIDP-1.0) v1.0.1 (132x176).jar | 102 | Lara Crofts Poker Party | - | 1.0.1 | MIDP-1.0 | - | 15 | 16 | 600 | 1941201 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/LasVegasNights_1.1.6_320x240.jar | 635 | Las Vegas Nights | - | 1.1.6 | MIDP-2.0 | - | 10 | 16 | 600 | 11919819 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/LegoBatman_1.3.0_320x240.jar | 1004 | Lego Batman | - | 1.3.0 | MIDP-2.0 | - | 18 | 16 | 600 | 197873416 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/Let's Go Bowling.jar | 961 | Let's go bowling! | - | 1.1.8 | MIDP-2.0 | - | 23 | 16 | 600 | 239404490 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/Let's Golf.jar | 1027 | Let's Golf! | - | 1.1.1 | MIDP-2.0 | - | 7 | 16 | 600 | 112541540 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/LetsGoBowling_1.0.4_320x240.jar | 698 | Let's go bowling! | - | 1.0.4 | MIDP-2.0 | m3g | 22 | 16 | 600 | 67609324 | 0 | - | 运行正常 |  |
| vault/LetsGolf_1.0.7_320x240.jar | 634 | Let's Golf! | - | 1.0.7 | MIDP-2.0 | - | 9 | 16 | 600 | 63883114 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/LiveFreeOrDieHard_1.0.0_320x240.jar | 326 | Live Free Or Die Hard | - | 1.0.0 | MIDP-2.0 | nokia-ui | 5 | 16 | 600 | 4764709313 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/Looney Tunes Monster Match.jar | 674 | LooneyTunes Monster Match | - | 1.0.0 | MIDP-2.0 | - | 44 | 16 | 600 | 5111906 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/LooneyTunesMonsterMatch.jar | 675 | LooneyTunes Monster Match (w | - | 1.0.0 | MIDP-2.0 | - | 44 | 16 | 600 | 22211751 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/LostPlanet2_1.0.7_320x240.jar | 625 | Lost Planet 2 | - | 1.0.7 | MIDP-2.0 | - | 16 | 16 | 600 | 134036578 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/Lost_1.0.0_320x240.jar | 301 | LOST | - | 1.0.0 | MIDP-2.0 | - | 6 | 16 | 600 | 6783352778 | 0 | - | 运行正常 |  |
| vault/Lotrreturofking_sek700.wz.cz.jar | 85 | Return Of The King | - | 1.54 | MIDP-1.0 | nokia-ui | 11 | 16 | 600 | 1944264 | 0 | java/lang/ArrayIndexOutOfBoundsE | 运行正常 |  |
| vault/Lotrtwotowers_sek700.wz.cz.jar | 127 | Lord of the Rings RPG | - | 1.86 | MIDP-1.0 | nokia-ui | 17 | 16 | 600 | 209484 | 0 | java/io/EOFException,javax/micro | 运行正常 |  |
| vault/Lotus_Challenge.jar | 96 | Lotus | - | 1.0.8 | MIDP-1.0 | - | 19 | 16 | 600 | 3409197 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/Love Triangle Dating.jar | 191 | Love Triangle stuart2773 | - | 1.0.9 | MIDP-1.0 | nokia-ui | 30 | 16 | 600 | 943819 | 0 | javax/microedition/rms/InvalidRe | 运行正常 |  |
| vault/Love_Diver_by_BFLM.jar | 72 | LoveDiver | - | 1.0 | MIDP-1.0 | nokia-ui | 7 | 16 | 600 | 89039 | 0 | - | 运行正常 |  |
| vault/Lovecka sezona.jar | 296 | Open Season | - | 1.1.3 | MIDP-2.0 | - | 6 | 16 | 600 | 3169518931 | 0 | javax/microedition/media/MediaEx | 运行正常 |  |
| vault/Lucha Libre - Desafio Total.jar | 517 | Lucha Libre Desafio Total | - | 1.1.7 | MIDP-2.0 | - | 6 | 16 | 601 | 354716059 | 0 | - | 运行正常 |  |
| vault/Lucky_Slot_Machine_v101.jar | 63 | Lucky Slot Machine | - | 1.0.1 | MIDP-1.0 | nokia-ui | 16 | 16 | 600 | 337506 | 0 | - | 运行正常 |  |
| vault/Lumines Mobile.jar | 285 | Lumines | - | 1.1.8 | MIDP-2.0 | - | 6 | 16 | 600 | 3154381401 | 0 | - | 运行正常 |  |
| vault/Lumines_1.0.0_320x240.jar | 428 | Lumines | - | 1.0.0 | MIDP-2.0 | - | 6 | 16 | 600 | 3042151199 | 0 | - | 运行正常 |  |
| vault/Luxor Quest_320x240.jar | 374 | Luxor Quest | - | 1.1.9 | MIDP-2.0 | - | 21 | 16 | 600 | 4990850 | 0 | java/io/IOException,java/lang/Nu | 运行正常 |  |
| vault/Luxor2.jar | 650 | LUXOR 2 | - | 1.0.11 | MIDP-2.0 | nokia-ui | 6 | 16 | 600 | 421373210 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/Luxor2_Nokia_ N95.jar | 802 | LUXOR 2 | - | 1.0.11 | MIDP-2.0 | - | 5 | 16 | 600 | 234392152 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/LuxorQuest.jar | 755 | Luxor Quest_freemobilejavaga | - | 1.1.1 | MIDP-2.0 | - | 22 | 16 | 600 | 3465321 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/M.A.C.H_Air_Combat.jar | 136 | MACH | - | 0.4.5 | MIDP-2.0 | - | 3 | 16 | 600 | 4077899651 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/MacrospaceDrakengard.jar | 172 | Drakengard | - | 1.0.2 | MIDP-1.0 | nokia-ui | 6 | 16 | 600 | 837688 | 0 | java/io/IOException,javax/microe | 运行正常 |  |
| vault/Madagascar 2 Escape to Africa (176x208 176x220 and 240x320).jar | 577 | Madagascar Escape 2 Africa | - | 1.1.1 | MIDP-2.0 | nokia-ui | 10 | 16 | 600 | 5402092837 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/Mafia.Wars.Yakuza.jar | 222 | Mafia Wars 3 | - | 1.2.09 | MIDP-2.0 | - | 8 | 16 | 600 | 1406059 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/Mafiawars.jar | 88 | MafiaWars [terjef] | - | 1.4.4 | MIDP-1.0 | - | 10 | 16 | 600 | 97602 | 0 | javax/microedition/rms/InvalidRe | 运行正常 |  |
| vault/Magic Wing II Ep.1.jar | 140 | MagicWingIIEp1\gkataria | - | 1.0 | MIDP-1.0 | - | 3 | 16 | 600 | 225925262 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/MagicGarden.jar | 237 | MagicGarden by drAdeLante | - | 1.0.1 | MIDP-2.0 | - | 13 | 16 | 601 | 400403 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/MahjongAdventure_1.0.2_320x240.jar | 269 | Mahjong Adventure | - | 1.0.2 | MIDP-2.0 | - | 33 | 16 | 600 | 5055287796 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/MahjongQuest.jar | 214 | Mah Jong Quest | - | 1.0.5 | MIDP-1.0 | - | 14 | 16 | 600 | 440842 | 0 | java/io/IOException,java/lang/Nu | 运行正常 |  |
| vault/MahjongStar_1.0.0_320x240.jar | 235 | Mahjong | - | 1.3.6 | MIDP-2.0 | - | 12 | 16 | 600 | 2968874 | 0 | java/io/IOException | 运行正常 |  |
| vault/Mahjong_1.3.6_320x240.jar | 264 | Mahjong Stars | - | 1.0.0 | MIDP-2.0 | - | 33 | 16 | 600 | 5039737452 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/Majesty Royal Simulator_320x240.jar | 1137 | Majesty | - | 1.13.34 | MIDP-2.0 | - | 77 | 16 | 600 | 35731940 | 0 | java/io/EOFException,java/io/IOE | 运行正常 |  |
| vault/Marble Madness.jar | 636 | Marble Madness | - | 4.5.84 | MIDP-2.0 | - | 32 | 16 | 600 | 5168146 | 0 | javax/microedition/rms/InvalidRe | 运行正常 |  |
| vault/March of Heroes.jar | 1496 | March Of Heroes | - | 1.1.2 | MIDP-2.0 | - | 17 | 16 | 209 | 38698074 | 0 | java/lang/NullPointerException | 标题自行退出 | exited itself (notifyDestroyed) after 209 fr |
| vault/MarchOfHeroes_1.0.9_320x240.jar | 970 | March Of Heroes | - | 1.0.9 | MIDP-2.0 | - | 19 | 16 | 600 | 126922153 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/Mech Strategy Wars.jar | 698 | MechStrategyWars | - | 1.0.6 | MIDP-2.0 | - | 13 | 16 | 600 | 4504832 | 0 | - | 运行正常 |  |
| vault/Medal Chase 2__320x240.jar | 179 | MedalChase2 | - | 1.0.0 | MIDP-2.0 | - | 9 | 16 | 600 | 3703798 | 0 | - | 运行正常 |  |
| vault/Medal Honor10_4.14.37_320x240.jar | 652 | Medal Honor 10 | - | 4.14.37 | MIDP-2.0 | - | 32 | 16 | 600 | 21868193 | 0 | - | 运行正常 |  |
| vault/Medal of Honor - Airborne - Elite.jar | 559 | Medal Of Honor Airborne Elit | - | 1.0.0 | MIDP-2.0 | - | 32 | 16 | 600 | 10557545 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/Medal of Honor - Airborne 3D.jar | 587 | Medal Of Honor Airborne | - | 7.1.33 | MIDP-2.0 | - | 40 | 16 | 600 | 8893457 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/Medal of Honor - Airborne.jar | 547 | Medal Of Honor Airborne | - | 4.5.25 | MIDP-2.0 | - | 32 | 16 | 600 | 10342281 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/Medal of Honor 2010.jar | 1118 | Medal of Honor 2010 | - | 4.4.47 | MIDP-2.0 | - | 33 | 16 | 600 | 1531108 | 0 | - | 运行正常 |  |
| vault/Medal of Honor.jar | 418 | Medal of Honor | - | 1.12.73 | MIDP-2.0 | - | 41 | 16 | 600 | 618759417 | 0 | - | 运行正常 |  |
| vault/Medieval Combat - Age of Glory.jar | 267 | Medieval Combat - Age of Glo | - | 1.0.4 | MIDP-2.0 | - | 6 | 16 | 600 | 162138922 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/Mega Tower Assault.jar | 848 | Mega Tower Assault | - | 1.2.6 | MIDP-2.0 | - | 16 | 16 | 600 | 5583007 | 0 | - | 运行正常 |  |
| vault/MegaMan 3_320x240.jar | 210 | Mega Man III | - | 1.0.0 | MIDP-2.0 | - | 14 | 16 | 600 | 2808839 | 0 | java/io/EOFException,java/lang/A | 运行正常 |  |
| vault/MegaTowerAssault_1.3.3_320x240.jar | 772 | Mega Tower Assault | - | 1.3.3 | MIDP-2.0 | nokia-ui | 16 | 16 | 600 | 5131428 | 0 | - | 运行正常 |  |
| vault/Men in Black 3.jar | 1618 | Men in Black 3 | - | 1.3.0 | MIDP-2.0 | - | 17 | 16 | 600 | 79140841 | 0 | - | 运行正常 |  |
| vault/Metal Gear Acid 2 3D.jar | 595 | Metal Gear Acid 2 3D | - | 1.0.0 | MIDP-2.0 | - | 37 | 16 | 600 | 9462724 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/Metal Gear Acid 2.jar | 426 | Metal Gear Acid 2 | - | 1.0.0 | MIDP-2.0 | - | 24 | 16 | 600 | 10986848 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/Metal Gear Acid 3D.jar | 561 | MGA Mobile | - | 1.0.0 | MIDP-1.0 | - | 6 | 16 | 600 | 11365114 | 0 | java/lang/IllegalArgumentExcepti | 运行正常 |  |
| vault/Metal Gear Acid.jar | 373 | MGA Mobile | - | 1.0.0 | MIDP-1.0 | - | 5 | 16 | 600 | 11343243 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/Metal Gear.jar | 301 | Metal Gear | - | 0.0.4 | MIDP-2.0 | - | 12 | 16 | 600 | 1571147 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/Metal Slug 4 Mobile.jar | 681 | Metal Slug M4 | - | 1.1.1 | MIDP-2.0 | - | 6 | 16 | 600 | 3272600 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/MetalGearAcid.jar | 410 | MGA Mobile | - | 1.0.0 | MIDP-1.0 | - | 6 | 16 | 600 | 7596757 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/MetalGearAcid2.jar | 604 | Metal Gear Acid 2 3D | - | 1.0.0 | MIDP-2.0 | m3g | 34 | 16 | 600 | 11848512 | 0 | javax/microedition/rms/RecordSto | 运行正常 | runs without input; the generic key script m |
| vault/MetalSlug3.jar | 302 | Metal Slug Mobile 3 | - | 0.4.8 | MIDP-2.0 | - | 3 | 16 | 600 | 5941164 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/MetalSlug4.jar | 768 | Metal Slug 4 | - | 1.0.1 | MIDP-2.0 | - | 6 | 16 | 600 | 8721668 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/MetalSlug4_1.1.5_320x240.jar | 748 | Metal Slug 4 | - | 1.1.5 | MIDP-2.0 | - | 4 | 16 | 600 | 5635584 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/Meteos - Astro Blocks.jar | 290 | Meteos | - | 1.0.5 | MIDP-2.0 | - | 9 | 16 | 600 | 254565967 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/MeteosAstroBlocks.jar | 296 | Meteos - Astro Blocks | - | 1.0.4 | MIDP-2.0 | - | 7 | 16 | 600 | 4251357272 | 0 | java/lang/ArrayIndexOutOfBoundsE | 运行正常 |  |
| vault/MiamiNights2_1.0.3_320x240.jar | 632 | Miami Nights 2 | - | 1.0.3 | MIDP-2.0 | - | 7 | 16 | 600 | 641587576 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/Midnight Bowling 2.jar | 588 | Midnight Bowling 2 | - | 1.1.1 | MIDP-2.0 | - | 5 | 16 | 600 | 185538582 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/Midnight Bowling 3.jar | 1430 | Midnight Bowling 3 | - | 1.1.3 | MIDP-2.0 | - | 30 | 16 | 600 | 38737350 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/Midnight Bowling.jar | 260 | MidnightBowling | - | 1.0.1 | MIDP-2.0 | - | 5 | 16 | 600 | 2984109 | 0 | javax/microedition/rms/InvalidRe | 运行正常 |  |
| vault/Midnight Pool 2.jar | 605 | Midnight Pool 2 | - | 1.0.9 | MIDP-2.0 | - | 13 | 16 | 600 | 396033219 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/Midnight Pool 3.jar | 1348 | Midnight Pool 3 | - | 1.2.2 | MIDP-2.0 | - | 29 | 16 | 600 | 51654719 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/Midnight Pool 4.jar | 656 | Midnight Pool | - | 1.0.2 | MIDP-2.0 | - | 26 | 16 | 600 | 43069956 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/Midnight Pool.jar | 158 | Midnight Pool [meinhold] | - | 1.0.1 | MIDP-1.0 | - | 5 | 16 | 600 | 7604281803 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/Midnight Pool2.jar | 180 | Midnight Pool | - | 1.0.0 | MIDP-2.0 | - | 5 | 16 | 600 | 6021024 | 0 | - | 运行正常 |  |
| vault/MidnightBowling2.jar | 552 | Midnight Bowling 2 | - | 1.1.1 | MIDP-2.0 | nokia-ui | 5 | 16 | 600 | 6110947373 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/MidnightBowling2_1.0.1_320x240.jar | 558 | Midnight Bowling 2 | - | 1.0.1 | MIDP-2.0 | nokia-ui | 5 | 16 | 600 | 274285449 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/MidnightDarts_1.0.3_320x240.jar | 563 | MIDNIGHT DARTS | - | 1.0.3 | MIDP-2.0 | - | 6 | 16 | 600 | 13410005 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/MidnightPool1_1.0.0_320x240.jar | 205 | Midnight Pool | - | 1.0.0 | MIDP-2.0 | - | 5 | 16 | 600 | 5926008033 | 0 | - | 运行正常 |  |
| vault/MidnightPool2_1.0.1_320x240.jar | 406 | Midnight Pool 2 | - | 1.0.1 | MIDP-2.0 | - | 14 | 16 | 600 | 5867187752 | 0 | java/lang/ClassNotFoundException | 运行正常 |  |
| vault/MidnightPool3_1.2.4_320x240.jar | 1265 | Midnight Pool 3 | - | 1.2.4 | MIDP-2.0 | - | 29 | 16 | 600 | 115045488 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/MidnightPool_K700_EN.jar | 147 | Midnight Pool [terjef] | - | 1.0.0 | MIDP-2.0 | - | 5 | 16 | 600 | 7948149952 | 0 | - | 运行正常 |  |
| vault/Midnight_Billard.jar | 186 | - | - | - | - | - | 0 | 16 | 0 | 0 | 0 | - | 非可用包 | not a zip file |
| vault/Midnight_poker.jar | 227 | Midnight Poker | - | 1.1.2 | MIDP-2.0 | - | 6 | 16 | 600 | 3463152708 | 0 | - | 运行正常 |  |
| vault/Midtown_Madness_3.jar | 80 | Midtown Madness 3 | - | 01.00.31 | MIDP-1.0 | - | 19 | 16 | 600 | 501903 | 0 | javax/microedition/media/MediaEx | 运行正常 |  |
| vault/Mig 2D Retro Shooter.jar | 1494 | Mig29 2D Retro Shooter | - | 1.0.0 | MIDP-2.0 | - | 75 | 16 | 600 | 36769345 | 0 | java/io/EOFException,java/io/IOE | 运行正常 |  |
| vault/Might and Magic -CZ-.jar | 183 | MightAndMagic | - | 1.0.0 | MIDP-2.0 | - | 5 | 16 | 600 | 3576109025 | 0 | - | 运行正常 |  |
| vault/Might and Magic II.jar | 602 | MightAndMagicII | - | 1.1.1 | MIDP-2.0 | - | 10 | 16 | 600 | 1001320649 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/Might and Magic.jar | 189 | MightAndMagic | - | 1.0.0 | MIDP-2.0 | - | 5 | 16 | 600 | 172832600 | 0 | - | 运行正常 |  |
| vault/MightAndMagic.jar | 188 | MightAndMagic (En) | - | 1.0.0 | MIDP-2.0 | - | 5 | 16 | 600 | 3631785336 | 0 | - | 运行正常 |  |
| vault/MightAndMagicII_1.0.2_320x240.jar | 609 | Might And Magic II | - | 1.0.2 | MIDP-2.0 | nokia-ui | 9 | 16 | 600 | 202247655 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/Might_And_Magic.jar | 187 | MightAndMagic | - | 1.0.0 | MIDP-2.0 | nokia-ui | 6 | 16 | 600 | 3144198692 | 0 | java/io/EOFException | 运行正常 |  |
| vault/Might_and_Magic.jar | 181 | MightAndMagic | - | 1.0.1 | MIDP-2.0 | nokia-ui | 5 | 16 | 600 | 2677073582 | 0 | java/io/EOFException | 运行正常 |  |
| vault/MightySword.jar | 166 | MightySword | - | 1.0 | MIDP-1.0 | nokia-ui | 16 | 16 | 600 | 2219012 | 0 | java/io/IOException | 运行正常 |  |
| vault/MikisWorld1.jar | 60 | Miki'sWorld1 | - | 1.1.7 | MIDP-1.0 | nokia-ui | 13 | 16 | 600 | 4548450049 | 0 | - | 运行正常 |  |
| vault/Mikis_World2_sek700.wz.cz.jar | 63 | Miki'sWorld2 | - | 1.2.2 | MIDP-1.0 | nokia-ui | 12 | 16 | 600 | 4685170 | 0 | - | 运行正常 |  |
| vault/Mikis_World_Part1_by_BFLM.jar | 65 | Miki'sWorld1 | - | 1.1.2 | MIDP-1.0 | nokia-ui | 13 | 16 | 600 | 4000503 | 0 | - | 运行正常 |  |
| vault/Million Dollar Poker.jar | 594 | Million Dollar Poker | - | 1.0.6 | MIDP-2.0 | - | 10 | 16 | 600 | 186553723 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/MillionDollarPokerFeatGusHansen_1.0.0_320x240.jar | 480 | Million Dollar Poker | - | 1.0.0 | MIDP-2.0 | nokia-ui | 10 | 16 | 600 | 3429716317 | 0 | - | 运行正常 |  |
| vault/Millionaire City.jar | 751 | MillCity | - | 1.0.25 | MIDP-2.0 | - | 64 | 16 | 600 | 1855441 | 0 | java/io/EOFException,javax/micro | 运行正常 |  |
| vault/MindGames.jar | 186 | Mind Games by Kowciu | - | 1.0.17 | MIDP-2.0 | - | 17 | 16 | 600 | 2834304 | 0 | javax/microedition/rms/InvalidRe | 运行正常 |  |
| vault/MindGames2.jar | 461 | Mind Games 2 | - | 1.0.0 | MIDP-2.0 | nokia-ui | 24 | 16 | 600 | 3001371 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/Mini Golf 99 Challenge 2010_320x240.jar | 611 | MGolf 99 Chall | - | 1.0.3 | MIDP-2.0 | nokia-ui | 37 | 16 | 600 | 18899262 | 0 | java/io/EOFException,java/lang/N | 运行正常 |  |
| vault/Mini Golf Magic 3D.jar | 334 | MiniGolfMagic by [wl] | - | 1.5.05 | MIDP-2.0 | - | 18 | 16 | 600 | 1657947 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/MiniGolf Theme Park - 99 Holes.jar | 469 | MGolf Park 99 | - | 1.0.11 | MIDP-2.0 | - | 36 | 16 | 600 | 12320689 | 0 | java/io/EOFException,javax/micro | 运行正常 |  |
| vault/MiniGolfThemePark.jar | 744 | MGolf Park 99_www.antofungky | - | 1.0.15 | MIDP-2.0 | nokia-ui | 39 | 16 | 600 | 12449400 | 0 | java/io/EOFException,java/lang/C | 运行正常 |  |
| vault/Minigolf Revolution - Pirate Park.jar | 581 | Minigolf Revolution Pirate P | - | 1.0.1 | MIDP-2.0 | - | 7 | 16 | 600 | 9194388 | 0 | - | 运行正常 |  |
| vault/Mission Impossible III.jar | 313 | Mission Impossible III | - | 1.0.0 | MIDP-2.0 | - | 4 | 16 | 600 | 430581033 | 0 | - | 运行正常 |  |
| vault/MissionImpossible3_1.0.0_320x240.jar | 293 | Mission Impossible 3 | - | 1.0.0 | MIDP-2.0 | nokia-ui | 4 | 16 | 600 | 3053459789 | 0 | - | 运行正常 |  |
| vault/Modern Combat 2 - Black Pegasus.jar | 1344 | Modern Combat 2: Black Pegas | - | 1.2.2 | MIDP-2.0 | - | 13 | 16 | 600 | 161802566 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/Modern Combat 4 - Zero Hour.jar | 1539 | Modern Combat4: Zero Hour | - | 1.1.2 | MIDP-2.0 | - | 12 | 16 | 600 | 270126237 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/MokisAdventure_1.10.jar | 51 | XiMPDA- Moki's Adventure | - | 1.10 | MIDP-1.0 | nokia-ui | 9 | 16 | 600 | 5812895705 | 0 | - | 运行正常 |  |
| vault/MonkeyMadness_sek700.ic.cz.jar | 41 | Monkey Madness | - | 0.4.2 | MIDP-1.0 | - | 4 | 16 | 600 | 300704 | 0 | - | 运行正常 |  |
| vault/Monkeyball_sek700.wz.cz.jar | 39 | MonkeyBall | - | 1.0 | MIDP-1.0 | nokia-ui | 5 | 16 | 600 | 121411 | 0 | - | 运行正常 |  |
| vault/Monopoly.jar | 109 | Monopoly | - | 1.0.37 | MIDP-2.0 | - | 7 | 16 | 600 | 11241283765 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/MonopolyTycoon_K700.jar | 187 | Mono Tycoon [terjef] | - | 0.0.48 | MIDP-2.0 | - | 11 | 16 | 600 | 366744 | 0 | - | 运行正常 |  |
| vault/MonopolyUBuild_1.9.2_320x240.jar | 518 | MONOPOLY U-Build www.wapx.ws | - | 1.9.2 | MIDP-2.0 | - | 28 | 16 | 600 | 23704349 | 0 | javax/microedition/rms/InvalidRe | 运行正常 |  |
| vault/MonsterLand10.jar | 60 | ACO_MonsterLand | - | 1.0 | MIDP-1.0 | - | 2 | 16 | 600 | 142141 | 0 | - | 运行正常 | runs without input; the generic key script m |
| vault/Monster_Pets.jar | 75 | Monster Pets | - | 1.0.3 | MIDP-1.0 | nokia-ui | 14 | 16 | 600 | 1102051 | 0 | java/lang/ClassNotFoundException | 运行正常 |  |
| vault/Monsters University.jar | 1355 | Monsters University | - | 1.1.2 | MIDP-2.0 | - | 13 | 16 | 600 | 21945927 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/Moorhuhn Invasion.jar | 99 | Moorhuhn Invasion | - | 1.1.0 | MIDP-2.0 | - | 5 | 16 | 600 | 3807238547 | 0 | - | 运行正常 |  |
| vault/Moorhuhn_Kart_Racer_by_BFLM.jar | 83 | Moorhuhn Kart Racer | - | 1.51 | MIDP-1.0 | nokia-ui | 19 | 16 | 0 | 0 | 0 | - | 非可用包 | archive entry logo.png is corrupt (inflate/C |
| vault/Moorhuhn_X_1.30.jar | 125 | Moorhuhn X | - | 1.30 | MIDP-1.0 | - | 6 | 16 | 600 | 896218422 | 0 | java/lang/ArrayIndexOutOfBoundsE | 运行正常 |  |
| vault/MoorhunPlaysuit_sek700.ic.cz.jar | 273 | Moorhen Playsuit | - | 1.0.9 | MIDP-1.0 | nokia-ui | 13 | 16 | 600 | 3462669 | 0 | - | 运行正常 |  |
| vault/MortalKombat3D_176x220.jar | 426 | Mortal Kombat Mobile 3D | - | 1.1.3 | MIDP-2.0 | m3g | 49 | 16 | 600 | 8589833 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/Moscowpoly1603.jar | 49 | Moscowpoly (Beta) | - | 0.95 | MIDP-1.0 | - | 9 | 16 | 600 | 14288 | 0 | - | 运行正常 | runs without input; the generic key script m |
| vault/Moto GP 07 240x320.jar | 434 | MotoGP07 | - | 0.0.1 | MIDP-2.0 | m3g,nokia-ui | 74 | 16 | 600 | 3886851406 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/Moto Racing  Fever 3D.jar | 312 | Moto Racing Fever | - | 1.2.0 | MIDP-2.0 | - | 27 | 16 | 600 | 12647329 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/MotocrossTrailExtreme_1.0.5_320x240.jar | 1049 | Motocross Trial Extreme | - | 1.0.5 | MIDP-2.0 | - | 11 | 16 | 600 | 177026060 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/MrSudoku.jar | 112 | MrSuDoku Tegos.ru | - | 1.0 | MIDP-2.0 | nokia-ui | 3 | 16 | 600 | 656496 | 0 | java/io/IOException,javax/microe | 运行正常 |  |
| vault/Ms Pac Man_320x240.jar | 237 | Ms. PAC-MAN | - | 1.4.21 | MIDP-2.0 | - | 47 | 16 | 600 | 3550177 | 0 | java/io/EOFException,javax/bluet | 运行正常 |  |
| vault/Ms. PAC-MAN (176x220) (1.0.0).jar | 59 | Ms. PAC-MAN | - | 1.0.0 | MIDP-2.0 | - | 10 | 16 | 600 | 295169253 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/Ms. PAC-MAN Demo (176x220) (1.00).jar | 73 | Ms. PAC-MAN DEMO | - | 1.00 | MIDP-2.0 | - | 2 | 16 | 600 | 3604484376 | 0 | - | 运行正常 |  |
| vault/Ms. PAC-MAN Demo by Namco (128x160) (1.5.1).jar | 59 | Ms. PAC-MAN Demo by Namco | - | 1.5.1 | MIDP-1.0 | - | 10 | 16 | 600 | 806845 | 0 | - | 运行正常 |  |
| vault/Ms. PAC-MAN Demo by Namco (176x220) (2.1.0).jar | 80 | Ms. PAC-MAN by Namco | - | 2.1.0 | MIDP-2.0 | - | 21 | 16 | 600 | 3530829 | 0 | javax/microedition/io/Connection | 运行正常 |  |
| vault/Ms. PAC-MAN Demo by Namco (240x400) (2.1.0).jar | 190 | Ms. PAC-MAN by Namco | - | 2.1.0 | MIDP-2.0 | - | 22 | 16 | 600 | 3138915 | 0 | java/io/IOException,java/lang/Nu | 运行正常 |  |
| vault/MsPacMan.jar | 248 | Ms. PAC-MAN | - | 1.4.5 | MIDP-2.0 | nokia-ui | 47 | 16 | 600 | 3844470 | 0 | java/io/EOFException,javax/bluet | 运行正常 |  |
| vault/MyLifeInNewYork_1.0.4_320x240.jar | 406 | My Life in New York | - | 1.0.4 | MIDP-2.0 | - | 6 | 16 | 600 | 5977333 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/MyMonsterPet.jar | 535 | MyMonsterPet-zetrO | - | 1.3.5 | MIDP-1.0 | - | 50 | 16 | 600 | 24759732 | 0 | - | 运行正常 |  |
| vault/My_mobile_pocket_people (mountin jacker).jar | 98 | MMPP [mountin jacker] | - | 1.0.0 | MIDP-2.0 | nokia-ui | 12 | 16 | 600 | 394709 | 0 | javax/microedition/rms/InvalidRe | 运行正常 |  |
| vault/N.O.V.A. 3.jar | 1536 | N.O.V.A. 3 | - | 1.1.9 | MIDP-2.0 | - | 9 | 16 | 600 | 13654416 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/N.O.V.A. Legacy.jar | 1226 | N.O.V.A. Legacy | - | 1.0.8 | MIDP-2.0 | - | 12 | 16 | 600 | 30280340 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/N.O.V.A..jar | 1195 | N.O.V.A. | - | 1.0.6 | MIDP-2.0 | - | 14 | 16 | 600 | 179286175 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/NAMCO RIDGE RACER 240x320.jar | 269 | RIDGE RACER | - | 1.0.5 | MIDP-2.0 | m3g | 36 | 16 | 600 | 6117291356 | 0 | javax/microedition/media/MediaEx | 运行正常 |  |
| vault/NBA ProBasketBall2010_1.0.1_320x240.jar | 552 | NBA Pro Basketball 2010 | - | 1.0.1 | MIDP-2.0 | - | 9 | 16 | 600 | 224164889 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/NBA Smash!.jar | 617 | NBA Smash | - | 1.0.9 | MIDP-2.0 | - | 9 | 16 | 600 | 137407896 | 0 | java/lang/ArrayIndexOutOfBoundsE | 运行正常 |  |
| vault/NFL2011_1.0.1_320x240 C3.jar | 819 | NFL 2011 | - | 1.0.1 | MIDP-2.1 | - | 22 | 16 | 600 | 685119011 | 0 | java/lang/ClassNotFoundException | 运行正常 |  |
| vault/NOVA Near Orbit Vanguard Alliance_320x240.jar | 866 | N.O.V.A. | - | 1.0.6 | MIDP-2.0 | - | 14 | 16 | 600 | 79251802 | 0 | - | 运行正常 |  |
| vault/NYNights_K700_EN.jar | 194 | New York Nights [terjef] | - | 1.0.2 | MIDP-2.0 | - | 6 | 16 | 600 | 2085858 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/Namco Arcade Golf_320x240.jar | 288 | NAMCO ARCADE GOLF | - | 1.3.6 | MIDP-1.0 | - | 5 | 16 | 600 | 9800207 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/Nano_Kid.jar | 63 | Nano Kid | - | 1.0.1 | MIDP-1.0 | nokia-ui | 5 | 16 | 600 | 3151888414 | 0 | - | 运行正常 |  |
| vault/Nascar 2007_by_java-xxl[1].wgz.cz.jar | 457 | NASCAR 07 | - | 4.1.40 | MIDP-2.0 | - | 119 | 16 | 600 | 4785896 | 0 | java/lang/NoSuchMethodError,java | 运行正常 | runs without input; the generic key script t |
| vault/Nate Adams Feestyle Motocross.jar | 143 | NateAdamsFMX | - | 1.5.0 | MIDP-2.0 | - | 52 | 16 | 600 | 1810293 | 0 | java/lang/ClassCastException,jav | 运行正常 |  |
| vault/Naval Battle - Mission Commander.jar | 299 | Naval Battle: Mission Comman | - | 1.0.5 | MIDP-2.0 | - | 6 | 16 | 600 | 16806045 | 0 | javax/bluetooth/BluetoothStateEx | 运行正常 |  |
| vault/Naval_Battle_Mission_Commander_BT.jar | 251 | Naval Battle Mission Command | - | 1.0.4 | MIDP-2.0 | - | 6 | 16 | 600 | 4961049260 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/Need for Speed - Carbon 3D.jar | 398 | NeedForSpeedCarbon | - | 1.1.98 | MIDP-2.0 | - | 33 | 16 | 600 | 4525112 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/Need for Speed - Hot Pursuit 3D Bonus Edition.jar | 1592 | Need for Speed Hot Pursuit B | - | 4.16.3 | MIDP-2.0 | - | 67 | 16 | 602 | 10816785 | 0 | java/lang/ClassNotFoundException | 运行正常 |  |
| vault/Need for Speed - Hot Pursuit 3D.jar | 1126 | Need for Speed Hot Pursuit | - | 4.1.88 | MIDP-2.0 | - | 60 | 16 | 602 | 9751471 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/Need for Speed - Hot Pursuit Bonus Edition.jar | 947 | Need for Speed Hot Pursuit B | - | 4.16.0 | MIDP-2.0 | - | 42 | 16 | 600 | 10868541 | 0 | java/lang/ClassNotFoundException | 运行正常 |  |
| vault/Need for Speed - Hot Pursuit.jar | 571 | Need for Speed Hot Pursuit | - | 4.3.80 | MIDP-2.0 | - | 35 | 16 | 600 | 10197042 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/Need for Speed - Most Wanted 3D.jar | 1496 | Need For Speed Most Wanted | - | 0.0.12 | MIDP-2.0 | - | 36 | 16 | 602 | 8067042 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/Need for Speed - Most Wanted.jar | 941 | Need For Speed Most Wanted | - | 0.0.12 | MIDP-2.0 | - | 35 | 16 | 601 | 4619888 | 0 | java/io/IOException,java/lang/Ar | 运行正常 |  |
| vault/Need for Speed - ProStreet 3D.jar | 653 | Need for Speed ProStreet | - | 7.0.89 | MIDP-2.0 | - | 37 | 16 | 600 | 146657 | 0 | java/io/IOException,java/lang/Nu | 运行正常 |  |
| vault/Need for Speed - Shift 3D.jar | 889 | Need for Speed Shift | - | 7.1.2 | MIDP-2.0 | - | 57 | 16 | 600 | 7792162 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/Need for Speed - Shift.jar | 472 | Need for Speed Shift | - | 4.3.30 | MIDP-2.0 | - | 32 | 16 | 600 | 8055150 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/Need for Speed - The Run 3D.jar | 1231 | Need for Speed The Run | - | 4.8.27 | MIDP-2.0 | - | 61 | 16 | 602 | 11398741 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/Need for Speed - The Run.jar | 518 | Need for Speed The Run | - | 4.9.7 | MIDP-2.0 | - | 38 | 16 | 600 | 11411748 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/Need for Speed - Undercover Velocity 3D.jar | 1125 | Need for Speed Undercover Ve | - | 1.0.0 | MIDP-2.0 | - | 46 | 16 | 600 | 4383476 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/Need for Speed - Undercover.jar | 485 | Need for Speed Undercover | - | 4.4.0 | MIDP-2.0 | - | 29 | 16 | 600 | 5010016 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/NeonRunner.jar | 330 | Neon Runner | - | 1.0.1 | MIDP-2.0 | nokia-ui | 21 | 16 | 600 | 1867012228 | 0 | - | 运行正常 |  |
| vault/New York Nights.v1.00.jar | 190 | NYNights | - | 0.0.75 | MIDP-1.0 | nokia-ui | 7 | 16 | 600 | 5078492075 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/NewYorkNights2FriendsForLife_1.0.5_320x240.jar | 664 | New York Nights 2 | - | 1.0.5 | MIDP-2.0 | - | 8 | 16 | 600 | 9553775 | 0 | java/lang/ClassNotFoundException | 运行正常 |  |
| vault/NightAtTheMuseum2_1.0.2_320x240.jar | 324 | Night at the Museum. Battle  | - | 1.0.2 | MIDP-2.0 | - | 13 | 16 | 600 | 8654787 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/Nightmare Creatures.jar | 160 | NightmareCreatures | - | 0.3.2 | MIDP-1.0 | nokia-ui | 4 | 16 | 600 | 1190510478 | 0 | - | 运行正常 |  |
| vault/Ninja Prophecy.jar | 1021 | Ninja Prophecy | - | 1.1.5 | MIDP-2.0 | - | 54 | 16 | 600 | 160199619 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/Ninja Prophecy_1.0.3_320x240.jar | 723 | Ninja Prophecy yMoBi.ru | - | 1.0.3 | MIDP-2.0 | nokia-ui | 54 | 16 | 601 | 67522365 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/NinjaProphecy_1.0.3_320x240.jar | 723 | Ninja Prophecy | - | 1.0.3 | MIDP-2.0 | nokia-ui | 54 | 16 | 600 | 68731301 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/Nitro Street Racing 2.jar | 976 | NitroStreet Racing2 | - | 1.2.0 | MIDP-2.0 | - | 16 | 16 | 600 | 342833686 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/Nitro Street Racing 3D.jar | 1151 | NitroStreet Racing | - | 1.1.2 | MIDP-2.0 | - | 25 | 16 | 600 | 6934450652 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/Nitro Street Racing.jar | 608 | NitroStreet Racing | - | 1.1.0 | MIDP-2.0 | - | 9 | 16 | 600 | 603300673 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/NitroStreetRacing2_1.1.3_320x240.jar | 628 | Nitro Street Racing 2 | - | 1.1.3 | MIDP-2.0 | - | 18 | 16 | 600 | 630199178 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/Nitro_Street_Racing_S60V3_240x320.jar | 593 | NitroRacing | - | 1.1.7 | MIDP-2.0 | - | 6 | 16 | 600 | 3818633712 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/NoWayOut_BFLM.jar | 106 | No Way Out | - | 1.0.0 | MIDP-1.0 | nokia-ui | 7 | 16 | 600 | 755770 | 0 | java/io/IOException | 运行正常 |  |
| vault/Numba.jar | 203 | Numba | - | 0.0.3 | MIDP-2.0 | - | 15 | 16 | 600 | 105013983 | 0 | java/io/IOException,javax/microe | 运行正常 |  |
| vault/Oban_Star_Racers_(EN).jar | 331 | ObanStarRacers | - | 1.0.0 | MIDP-2.0 | nokia-ui | 12 | 16 | 548 | 797489 | 0 | - | 运行正常 | exited itself (notifyDestroyed) |
| vault/Off-Road Dirt Motocross.jar | 579 | Xtreme Dirt Bike | - | 1.0.8 | MIDP-2.0 | - | 11 | 16 | 600 | 473424685 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/OffRoadDirtMotocross_1.0.0_320x240.jar | 358 | Off-Road Dirt Motocross | - | 1.0.0 | MIDP-2.0 | - | 12 | 16 | 600 | 3904489956 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/Oilwars.jar | 125 | OilWars | - | 1.1.2 | MIDP-1.0 | nokia-ui | 4 | 16 | 600 | 711784 | 0 | java/io/IOException,java/lang/Nu | 运行正常 |  |
| vault/Omegamissionv102-_sek700.wz.cz.jar | 84 | OmegaMission | - | 1.0.2 | MIDP-1.0 | nokia-ui | 7 | 16 | 600 | 22978 | 0 | - | 运行正常 | runs without input; the generic key script m |
| vault/Onimusha Puzzle Dark Castle_320x240.jar | 161 | Onimusha Puzzle Dark Castle | - | 1.0.0 | MIDP-2.0 | - | 4 | 16 | 600 | 2349372 | 0 | java/lang/ArrayIndexOutOfBoundsE | 运行正常 |  |
| vault/Open Season.jar | 310 | Open Season | - | 1.1.3 | MIDP-2.0 | - | 6 | 16 | 606 | 2341163542 | 0 | - | 运行正常 |  |
| vault/OrangeWar_sek700.ic.cz.jar | 73 | Orange War | - | 1.0.0 | MIDP-1.0 | nokia-ui | 11 | 16 | 600 | 157317 | 0 | - | 运行正常 |  |
| vault/OregonTrailAmericanSettlers_1.1.4_320x240.jar | 917 | Oregon Trail American Settle | - | 1.1.4 | MIDP-2.0 | nokia-ui | 23 | 16 | 600 | 29788049 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/OregonTrailGoldRush_1.4.3_320x240.jar | 654 | The Oregon Trail Gold Rush | - | 1.4.3 | MIDP-2.0 | - | 20 | 16 | 600 | 56999770 | 0 | - | 运行正常 |  |
| vault/Orlen Team - Dakar 2007.jar | 159 | Orlen Team - Dakar 2007 | - | 1.0.0 | MIDP-2.0 | - | 10 | 16 | 600 | 6934379 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/PAC-MAN (176x220) (1.0.0).jar | 86 | PAC-MAN | - | 1.0.0 | MIDP-1.0 | - | 8 | 16 | 600 | 40035 | 0 | java/lang/StringIndexOutOfBounds | 运行正常 |  |
| vault/PAC-MAN Ms. PAC-MAN Demo (176x220) (2.1.0).jar | 68 | PAC-MAN and Ms. PAC-MAN DEMO | - | 2.1.0 | MIDP-2.0 | - | 18 | 16 | 548 | 908566 | 0 | - | 运行正常 | exited itself (notifyDestroyed) |
| vault/PAC-MAN Ms. PAC-MAN Demo (240x320) (2.1.1).jar | 67 | PAC-MAN / Ms. PAC-MAN DEMO b | - | 2.1.1 | MIDP-2.0 | - | 18 | 16 | 600 | 1184626 | 0 | - | 运行正常 |  |
| vault/PAC-MAN Ms. PAC-MAN Demo (240x320) (2.6.0).jar | 181 | PAC-MAN \ Ms. PAC-MAN DEMO b | - | 2.6.0 | MIDP-2.0 | - | 29 | 16 | 600 | 202585700 | 0 | - | 运行正常 |  |
| vault/PAC-MAN Ms.PAC-MAN Demo (128x160) (2.6.0).jar | 111 | PAC-MAN \ Ms. PAC-MAN DEMO | - | 2.6.0 | MIDP-2.0 | - | 28 | 16 | 600 | 156791294 | 0 | - | 运行正常 |  |
| vault/PAC-MANIA 3D by Namco (176x220) (1.0.0).jar | 184 | PAC-MANIA (3D) by Namco | - | 1.0.0 | MIDP-2.0 | - | 19 | 16 | 600 | 781 | 0 | java/lang/StringIndexOutOfBounds | 运行正常 | runs without input; the generic key script m |
| vault/POPWW_K700_EN_108__ares_.jar | 190 | PrinceOfPersia-WarriorWithin | - | 1.0.8 | MIDP-1.0 | - | 5 | 16 | 600 | 2755379242 | 0 | - | 运行正常 |  |
| vault/POPersia_the_two_thrones.jar | 268 | Prince Of Persia 3 | - | 1.1.5 | MIDP-2.0 | - | 6 | 16 | 600 | 3125601250 | 0 | - | 运行正常 |  |
| vault/Pac Man Kart Rally 2D-3D_320x240.jar | 500 | PAC-MAN Kart Rally | - | 1.0.1 | MIDP-2.0 | - | 25 | 16 | 600 | 6603839 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/Pac-Man - Championship Edition.jar | 697 | PAC-MAN CHAMPIONSHIP EDITION | - | 1.1.0 | MIDP-2.0 | - | 22 | 16 | 600 | 27840332 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/Pac-Man Championship Edition_320x240.jar | 441 | PAC-MAN CHAMPIONSHIP EDITION | - | 1.1.0 | MIDP-2.0 | - | 22 | 16 | 600 | 22769767 | 0 | javax/microedition/rms/InvalidRe | 运行正常 |  |
| vault/Pac-Man Party.jar | 836 | PAC-MAN Party by Namco | - | 1.0.0 | MIDP-2.0 | - | 5 | 16 | 600 | 28549657 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/Pac-Man Pinball 2_320x240.jar | 421 | PAC-MAN Pinball 2 | - | 1.0.3 | MIDP-2.0 | nokia-ui | 24 | 16 | 600 | 8637731 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/Pac-Man.jar | 209 | PAC-MAN | - | 1.8.6 | MIDP-2.0 | - | 44 | 16 | 600 | 17688906 | 0 | javax/bluetooth/BluetoothStateEx | 运行正常 |  |
| vault/Pac-Mania.jar | 169 | PAC-MANIA | - | 1.1.8 | MIDP-2.0 | - | 4 | 16 | 600 | 12124954044 | 0 | - | 运行正常 |  |
| vault/PacManChampionship.jar | 558 | PAC-MAN CHAMPIONSHIP EDITION | - | 1.1.0 | MIDP-2.0 | - | 23 | 16 | 600 | 30917845 | 0 | javax/microedition/rms/InvalidRe | 运行正常 |  |
| vault/Pahtum_sek700.wz.cz.jar | 71 | PahTum | - | 1.0.5 | MIDP-1.0 | nokia-ui | 11 | 16 | 600 | 1428732 | 0 | - | 运行正常 |  |
| vault/Paperboy - Wheels on Fire.jar | 320 | Paperboy Wheels On Fire | - | 01.00.01 | MIDP-2.0 | - | 11 | 16 | 600 | 2897066 | 0 | - | 运行正常 |  |
| vault/ParcoGiochi.jar | 283 | Parco Giochi | - | 1.0.48 | MIDP-2.0 | nokia-ui | 14 | 16 | 600 | 18051893 | 0 | java/io/EOFException,java/lang/N | 运行正常 |  |
| vault/ParisHiltonsDiamondQuest_1.1.0_320x240.jar | 323 | Paris Hilton's Diamond Quest | - | 1.1.0 | MIDP-2.0 | - | 8 | 16 | 600 | 6905762806 | 0 | javax/microedition/rms/InvalidRe | 运行正常 |  |
| vault/ParisNights_1.0.0_320x240.jar | 320 | Paris Nights | - | 1.0.0 | MIDP-2.0 | - | 10 | 16 | 600 | 1953242 | 0 | java/lang/ArrayIndexOutOfBoundsE | 运行正常 |  |
| vault/PenaltyKick.jar | 37 | Penalty Kick 2002 | - | 1.0.1 | MIDP-1.0 | - | 3 | 16 | 600 | 32682 | 0 | - | 运行正常 | runs without input; the generic key script m |
| vault/Petz_1.0.2_320x240.jar | 643 | Petz | - | 1.0.2 | MIDP-2.0 | - | 11 | 16 | 600 | 22010316 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/Phantom Duck.jar | 465 | Phantom Duck | - | 1.0.2 | MIDP-1.0 | - | 11 | 16 | 600 | 1964582 | 0 | java/io/EOFException | 运行正常 |  |
| vault/Pictionary_320x240.jar | 208 | Pictionary | - | 1.13.24 | MIDP-2.0 | - | 13 | 16 | 600 | 687817357 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/Pirates of the Caribbean - At World's End.jar | 283 | Pirates 3 | - | 1.0.2 | MIDP-1.0 | - | 19 | 16 | 600 | 2748242 | 0 | java/io/EOFException,java/lang/N | 运行正常 |  |
| vault/Pirates of the Seven Seas.jar | 581 | Pirates of the Seven Seas | - | 1.1.6 | MIDP-2.0 | - | 7 | 16 | 600 | 1517883 | 0 | - | 运行正常 |  |
| vault/PiratesOfTheCaribbean.jar | 265 | Pirates 3 | - | 1.0.4 | MIDP-1.0 | nokia-ui | 19 | 16 | 600 | 5894411 | 0 | java/io/EOFException,java/lang/N | 运行正常 |  |
| vault/PiratesofTheSevenSeas_176x220.jar | 327 | Pirates of the Seven Seas by | - | 1.0.3 | MIDP-2.0 | nokia-ui | 7 | 16 | 600 | 4333673 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/Pixeline ZOO.jar | 53 | PixZoo | - | 1.0.1 | MIDP-1.0 | nokia-ui | 10 | 16 | 600 | 373920 | 0 | java/io/IOException | 运行正常 |  |
| vault/Plants vs Zombies (240x320 Touch, N8FanClub.com v4.2.74).jar | 1712 | Plants vs Zombies (N8FanClub | - | 4.2.74 | MIDP-2.0 | - | 99 | 16 | 601 | 6044041 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/Plants vs Zombies (240x320 Touch, dedomil.net v4.3.22).jar | 1510 | Plants vs Zombies | - | 4.3.22 | MIDP-2.0 | - | 124 | 16 | 601 | 5611858 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/Plants vs Zombies (240x320 Touch, v4.3.26).jar | 1746 | Plants vs Zombies | - | 4.3.26 | MIDP-2.0 | - | 99 | 16 | 600 | 6619525 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/Plants vs Zombies (240x320).jar | 1629 | Plants vs Zombies | - | 1.0.0 | MIDP-2.0 | - | 212 | 16 | 601 | 5033279 | 0 | java/lang/StringIndexOutOfBounds | 运行正常 |  |
| vault/Plants vs Zombies (240x320, RuGame.Mobi v4.1.38).jar | 1301 | Plants vs Zombies | - | 4.1.38 | MIDP-2.0 | - | 93 | 16 | 600 | 108859307 | 0 | java/lang/StringIndexOutOfBounds | 运行正常 |  |
| vault/Plants vs Zombies (240x320, RuGame.Mobi v4.2.66).jar | 1329 | Plants vs Zombies | - | 4.2.66 | MIDP-2.0 | - | 93 | 16 | 601 | 4995509 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/Plants vs Zombies (240x320, ayarde09 v4.2.61).jar | 1314 | Plant vs Zombies by ayarde09 | - | 4.2.61 | MIDP-2.0 | - | 93 | 16 | 0 | 0 | 0 | - | 失败 |  |
| vault/Plants vs Zombies (240x400 Touch, RuGame.Mobi v4.2.75).jar | 1341 | Plants vs Zombies | - | 4.2.75 | MIDP-2.0 | - | 99 | 16 | 601 | 5521955 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/Plants vs Zombies (240x400 Touch, v4.2.75).jar | 1341 | Plants vs Zombies | - | 4.2.75 | MIDP-2.0 | - | 99 | 16 | 601 | 5521955 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/Plants vs Zombies (320x240).jar | 1510 | Plants vs Zombies | - | 4.3.22 | MIDP-2.0 | - | 124 | 16 | 601 | 5611858 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/Plants vs Zombies (320x240, RuGame.Mobi v4.1.61).jar | 1313 | Plants vs Zombies | - | 4.1.61 | MIDP-2.0 | - | 93 | 16 | 601 | 4996850 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/Plants vs Zombies (320x240, v4.2.61).jar | 1314 | Plants vs Zombies | - | 4.2.61 | MIDP-2.0 | - | 93 | 16 | 601 | 5004338 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/Plants vs Zombies (320x480 Touch, dedomil.net v4.6.0).jar | 1592 | Plants vs Zombies | - | 4.6.0 | MIDP-2.0 | - | 99 | 16 | 601 | 448876195 | 0 | java/lang/StringIndexOutOfBounds | 运行正常 |  |
| vault/Plants vs Zombies (360x640 Touch, RuGame.Mobi v4.3.26).jar | 1746 | Plants vs Zombies | - | 4.3.26 | MIDP-2.0 | - | 99 | 16 | 600 | 6619525 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/Plants vs Zombies (360x640, v4.1.3).jar | 1710 | Plants vs Zombies | - | 4.1.3 | MIDP-2.0 | - | 99 | 16 | 600 | 209218 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/Plants vs Zombies (400x240).jar | 1341 | Plants vs Zombies | - | 4.2.75 | MIDP-2.0 | - | 99 | 16 | 601 | 5521955 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/Plants vs Zombies (640x360 Touch, Tencent).jar | 2417 | 植物大战僵尸 | - | 01.25.2 | MIDP-2.0 | - | 114 | 16 | 600 | 492240853 | 0 | java/lang/OutOfMemoryError,javax | 运行正常 |  |
| vault/Plants vs Zombies (640x360).jar | 1712 | Plants vs Zombies | - | 4.2.74 | MIDP-2.0 | - | 99 | 16 | 601 | 6044041 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/Plants vs Zombies (Samsung GT-S5230, RuGame.Mobi v4.2.75).jar | 1341 | Plants vs Zombies | - | 4.2.75 | MIDP-2.0 | - | 99 | 16 | 601 | 5521955 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/Plants vs. Zombies [240x320] (K800i) (andrew-lviv.net).jar | 1318 | Plants vs Zombies | - | 4.0.49 | MIDP-2.0 | - | 93 | 16 | 601 | 4999423 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/Plants vs. Zombies [240x320] (K850i W910i) (andrew-lviv.net).jar | 1329 | Plants vs Zombies | - | 4.2.66 | MIDP-2.0 | - | 93 | 16 | 601 | 4995509 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/Plants vs. Zombies [240x320] (N82) (andrew-lviv.net).jar | 1291 | Plants vs Zombies | - | 4.3.65 | MIDP-2.0 | - | 93 | 16 | 600 | 249470 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/Plants vs. Zombies [240x320] (N95 C5-00) (andrew-lviv.net).jar | 1336 | Plants vs Zombies | - | 4.1.78 | MIDP-2.0 | - | 93 | 16 | 601 | 14861594 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/Plants vs. Zombies [240x320] (SGH-F480) (andrew-lviv.net).jar | 1510 | Plants vs Zombies | - | 4.3.22 | MIDP-2.0 | - | 124 | 16 | 601 | 5611858 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/Plants vs. Zombies [240x400] (Nokia Asha 311) (andrew-lviv.net).jar | 1341 | Plants vs Zombies | - | 4.2.75 | MIDP-2.0 | - | 99 | 16 | 601 | 5521955 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/Plants vs. Zombies [240x400] (S5230) (andrew-lviv.net).jar | 1341 | Plants vs Zombies | - | 4.2.75 | MIDP-2.0 | - | 99 | 16 | 601 | 5521955 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/Plants vs. Zombies [320x240] (E71) (andrew-lviv.net).jar | 1313 | Plants vs Zombies | - | 4.1.61 | MIDP-2.0 | - | 93 | 16 | 601 | 4996850 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/Plants vs. Zombies [320x240] (E72) (andrew-lviv.net).jar | 1314 | Plants vs Zombies | - | 4.2.61 | MIDP-2.0 | - | 93 | 16 | 601 | 5004338 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/Plants vs. Zombies [320x480] (Samsung GT-S5292) (andrew-lviv.net).jar | 1592 | Plants vs Zombies | - | 4.6.0 | MIDP-2.0 | - | 99 | 16 | 601 | 448876195 | 0 | java/lang/StringIndexOutOfBounds | 运行正常 |  |
| vault/Plants vs. Zombies [360x640] (C6-00) (andrew-lviv.net).jar | 1746 | Plants vs Zombies | - | 4.3.26 | MIDP-2.0 | - | 99 | 16 | 600 | 6619525 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/Plants vs. Zombies [360x640] (N5800 N5228) (andrew-lviv.net).jar | 1700 | Plants vs Zombies | - | 4.0.45 | MIDP-2.0 | - | 99 | 16 | 600 | 209218 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/Plants vs. Zombies [360x640] (N8) (andrew-lviv.net).jar | 1712 | Plants vs Zombies | - | 4.2.74 | MIDP-2.0 | - | 99 | 16 | 601 | 6044041 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/Plants vs. Zombies [360x640] (SATIO U5i Vivaz) (andrew-lviv.net).jar | 1710 | Plants vs Zombies | - | 4.1.3 | MIDP-2.0 | - | 99 | 16 | 600 | 209218 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/Plants vs. Zombies [480x800] (S8000) (andrew-lviv.net).jar | 1341 | Plants vs Zombies | - | 4.2.75 | MIDP-2.0 | - | 99 | 16 | 601 | 5521955 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/PlantsVsZombies_4.1.61_320x240.jar | 1313 | Plants vs Zombies | - | 4.1.61 | MIDP-2.0 | - | 93 | 16 | 601 | 4996850 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/Plants_vs_Zombies_240x320-4.3.22-651742-mobiles24.jar | 1510 | Plants vs Zombies | - | 4.3.22 | MIDP-2.0 | - | 124 | 16 | 601 | 5611858 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/PlantsvsZombies_Asha311.jar | 1341 | Plants vs Zombies | - | 4.2.75 | MIDP-2.0 | - | 99 | 16 | 601 | 5521955 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/PlantsvsZombies_C6-00.jar | 1746 | Plants vs Zombies | - | 4.3.26 | MIDP-2.0 | - | 99 | 16 | 600 | 6619525 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/PlantsvsZombies_E71.jar | 1313 | Plants vs Zombies | - | 4.1.61 | MIDP-2.0 | - | 93 | 16 | 601 | 4996850 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/PlantsvsZombies_K800i.jar | 1318 | Plants vs Zombies | - | 4.0.49 | MIDP-2.0 | - | 93 | 16 | 601 | 4999423 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/PlantsvsZombies_K850i.jar | 1329 | Plants vs Zombies | - | 4.2.66 | MIDP-2.0 | - | 93 | 16 | 601 | 4995509 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/PlantsvsZombies_N5800.jar | 1700 | Plants vs Zombies | - | 4.0.45 | MIDP-2.0 | - | 99 | 16 | 600 | 209218 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/PlantsvsZombies_N8.jar | 1712 | Plants vs Zombies | - | 4.2.74 | MIDP-2.0 | - | 99 | 16 | 601 | 6044041 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/PlantsvsZombies_N82.jar | 1291 | Plants vs Zombies | - | 4.3.65 | MIDP-2.0 | - | 93 | 16 | 600 | 249470 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/PlantsvsZombies_N95.jar | 1336 | Plants vs Zombies | - | 4.1.78 | MIDP-2.0 | - | 93 | 16 | 601 | 14861594 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/Plasma_Inferno.jar | 110 | Plasma Inferno | - | 1.0.4 | MIDP-1.0 | nokia-ui | 7 | 16 | 600 | 2473827 | 0 | - | 运行正常 |  |
| vault/Platinum Solitaire 2.jar | 763 | Platinum Solitaire 2 | - | 1.1.7 | MIDP-2.0 | - | 8 | 16 | 600 | 13509400 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/Platinum Solitaire 3.jar | 1243 | Platinum Solitaire 3 | - | 1.1.5 | MIDP-2.0 | - | 6 | 16 | 600 | 66574060 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/Platinum Solitaire.jar | 595 | Platinum Solitaire | - | 1.0.12 | MIDP-2.0 | - | 6 | 16 | 600 | 305385259 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/Platinum Sudoku 2.jar | 964 | Platinum Sudoku 2 | - | 1.0.8 | MIDP-2.0 | - | 14 | 16 | 600 | 72399404 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/Platinum Sudoku 2_320x240.jar | 389 | Platinum Sudoku 2 | - | 1.2.0 | MIDP-2.0 | - | 13 | 16 | 600 | 6527292 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/Platinum Sudoku.jar | 262 | Platinum Sudoku | - | 1.2.2 | MIDP-2.0 | - | 8 | 16 | 600 | 291108007 | 0 | - | 运行正常 |  |
| vault/PlatinumMahjong.jar | 288 | Platinum Mahjong | - | 1.0.9 | MIDP-2.0 | - | 4 | 16 | 600 | 3729527310 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/PlatinumSolitaire2_1.0.4_320x240.jar | 529 | Platinum Solitaire 2 | - | 1.0.4 | MIDP-2.0 | - | 8 | 16 | 600 | 4055989635 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/PlatinumSolitaire3_1.0.7_320x240.jar | 1094 | Platinum Solitaire 3 | - | 1.0.7 | MIDP-2.0 | - | 6 | 16 | 600 | 4538435889 | 0 | - | 运行正常 |  |
| vault/PlatinumSudoku.jar | 269 | Platinum Sudoku | - | 1.0.1 | MIDP-2.0 | - | 9 | 16 | 600 | 5309706951 | 0 | - | 运行正常 |  |
| vault/PlatinumSudoku1_1.0.1_320x240.jar | 334 | Platinum Sudoku | - | 1.0.1 | MIDP-2.0 | - | 12 | 16 | 600 | 4696924254 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/PlatinumSudoku2.jar | 629 | Platinum Sudoku 2 | - | 1.1.3 | MIDP-2.0 | - | 14 | 16 | 600 | 84457419 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/Playman Beach Volleyball 3D CZ sew800i[1].webgarden.cz.jar.jar | 115 | Playman Beach Volley | - | 1.0.0 | MIDP-2.0 | m3g,nokia-ui | 6 | 16 | 600 | 560541331 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/Playman Summer Games 3_320x240.jar | 414 | Playman Summer Games 3 | - | 1.0.21 | MIDP-2.0 | - | 6 | 16 | 600 | 5919231542 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/PoP2_-_Warrior_Within.jar | 191 | PrinceOfPersia-WarriorWithin | - | 1.0.5 | MIDP-2.0 | - | 5 | 16 | 600 | 2900437320 | 0 | - | 运行正常 |  |
| vault/PoP3_-_The_Two_Thrones.jar | 279 | Prince Of Persia 3 | - | 1.1.5 | MIDP-1.0 | nokia-ui | 6 | 16 | 600 | 76556469 | 0 | - | 运行正常 |  |
| vault/PoP_-_Classic.jar | 516 | Prince of Persia Classic by  | - | 1.0.1 | MIDP-2.0 | - | 7 | 16 | 600 | 6150730022 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/PoP_-_Harem_Adventures.jar | 100 | Prince Of Persia Harem Adven | - | 1.2.6 | MIDP-1.0 | nokia-ui | 14 | 16 | 600 | 2998601673 | 0 | - | 运行正常 |  |
| vault/PoP_-_The_Sands_Of_Time.jar | 110 | PrinceSOT | - | 1.1.0 | MIDP-2.0 | - | 7 | 16 | 600 | 3953119058 | 0 | - | 运行正常 |  |
| vault/Pocket Chef.jar | 630 | Pocket Chef | - | 1.0.0 | MIDP-2.0 | - | 12 | 16 | 600 | 84522518 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/PocketChef_1.0.3_320x240.jar | 565 | Pocket Chef | - | 1.0.3 | MIDP-2.0 | - | 11 | 16 | 600 | 122180475 | 0 | - | 运行正常 |  |
| vault/Pocket_Glider.jar | 170 | PocketGlider | - | 1.0.0 | MIDP-1.0 | - | 12 | 16 | 600 | 13565705 | 0 | - | 运行正常 |  |
| vault/Pocket_War.jar | 89 | XiMPDA- Pocket War | - | 1.10 | MIDP-1.0 | nokia-ui | 5 | 16 | 600 | 2002409481 | 0 | - | 运行正常 |  |
| vault/Poisonalley_sek700.wz.cz.jar | 56 | PoisonAlley | - | 1.0 | MIDP-1.0 | nokia-ui | 5 | 16 | 600 | 100129 | 0 | - | 运行正常 |  |
| vault/Polda 5 Sexy PrĹˇĂ­.jar | 212 | Polda5 - Sexyprsi | - | 1.0.0 | MIDP-1.0 | - | 6 | 16 | 600 | 4260711911 | 0 | java/lang/ClassNotFoundException | 运行正常 |  |
| vault/PoliticallyIncorrect.jar | 88 | PoliticallyIncorrect_RoS_S60 | - | 1.0 | MIDP-1.0 | nokia-ui | 9 | 16 | 600 | 207069 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/Popstar_by_BFLM.jar | 46 | - | - | - | - | nokia-ui | 0 | 16 | 600 | 3122989 | 0 | - | 运行正常 |  |
| vault/PornManager_k500700.jar | 208 | PornManager [pappfer.hu] | - | 1.0 | MIDP-2.0 | - | 6 | 16 | 600 | 3886550 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/Postal Babes.jar | 337 | Postal Babes | - | 1.4.0 | MIDP-2.0 | - | 20 | 16 | 600 | 8236184 | 0 | java/io/EOFException,java/io/IOE | 运行正常 |  |
| vault/Predators.jar | 959 | Predators | - | 1.0.8 | MIDP-2.0 | - | 17 | 16 | 600 | 110755314 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/Predators_1.3.0_320x240.jar | 663 | Predators | - | 1.3.0 | MIDP-2.0 | - | 19 | 16 | 600 | 187447111 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/Prehistoric Fun Park_320x240_Nokia_E71.jar | 213 | Prehistoric Fun Park | - | 0.1.20 | MIDP-2.0 | - | 7 | 16 | 600 | 43808 | 0 | - | 运行正常 |  |
| vault/Prince Of Persia - 3 - Worrior Within.jar | 164 | PrinceofPersia:WarriorWithin | - | 1.0.2 | MIDP-2.0 | - | 6 | 16 | 600 | 3292541100 | 0 | - | 运行正常 |  |
| vault/Prince of Persia (2008).jar | 620 | Prince of Persia | - | 1.0.5 | MIDP-2.0 | - | 6 | 16 | 600 | 635351818 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/Prince of Persia - The Forgotten Sands.jar | 995 | Prince of Persia The Forgott | - | 1.0.7 | MIDP-2.0 | - | 11 | 16 | 600 | 31180620 | 0 | java/lang/ArrayIndexOutOfBoundsE | 运行正常 |  |
| vault/Prince of Persia - The Two Thrones.jar | 306 | Prince Of Persia 3 | - | 1.0.1 | MIDP-2.0 | - | 7 | 16 | 600 | 343346649 | 0 | - | 运行正常 |  |
| vault/Prince of Persia - Warrior Within.jar | 221 | Prince Of Persia - Warrior W | - | 1.0.0 | MIDP-2.0 | - | 7 | 16 | 600 | 429412142 | 0 | - | 运行正常 |  |
| vault/Prince of Persia Classic.jar | 617 | Prince of Persia Classic | - | 1.0.6 | MIDP-2.0 | - | 7 | 16 | 600 | 226197433 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/PrinceOfPersia.jar | 196 | PrinceOfPersiaTheSandsOfTime | - | 1.0.1 | MIDP-2.0 | - | 6 | 16 | 600 | 16531230 | 0 | - | 运行正常 |  |
| vault/PrinceOfPersia2.jar | 211 | PrinceOfPersia-WarriorWithin | - | 1.0.1 | MIDP-1.0 | nokia-ui | 5 | 16 | 600 | 2490141293 | 0 | - | 运行正常 |  |
| vault/PrinceOfPersia3.jar | 275 | Prince Of Persia 3 | - | 1.0 | MIDP-2.0 | - | 7 | 16 | 600 | 3096091548 | 0 | - | 运行正常 |  |
| vault/PrinceOfPersia3TheTwoThrones_1.0.3_320x240.jar | 326 | Prince Of Persia 3 | - | 1.0.3 | MIDP-2.0 | - | 8 | 16 | 891 | 1658318501 | 0 | - | 运行正常 |  |
| vault/PrinceOfPersia4.jar | 600 | Prince of Persia | - | 1.0.4 | MIDP-2.0 | - | 6 | 16 | 600 | 1102012332 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/PrinceOfPersiaClassic_1.0.1_320x240.jar | 614 | Prince of Persia Classic by  | - | 1.0.1 | MIDP-2.0 | - | 7 | 16 | 600 | 6482660644 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/PrinceOfPersiaThe ForgottenSands_1.0.4_320x240.jar | 737 | Prince of Persia The Forgott | - | 1.0.4 | MIDP-2.0 | - | 11 | 16 | 600 | 42737328 | 0 | - | 运行正常 |  |
| vault/PrinceOfPersia_1.0.0_320x240.jar | 591 | Prince of Persia | - | 1.0.0 | MIDP-2.0 | - | 6 | 16 | 600 | 102685575 | 0 | - | 运行正常 |  |
| vault/Pro Evolution Soccer 2008.jar | 346 | PES 2008 | - | 1.0.1 | MIDP-2.0 | - | 8 | 16 | 601 | 519765820 | 0 | java/lang/IndexOutOfBoundsExcept | 运行正常 |  |
| vault/Pro Evolution Soccer 2010.jar | 458 | PES 2010 | - | 1.0.0 | MIDP-2.0 | - | 9 | 16 | 600 | 6218142070 | 0 | java/lang/IndexOutOfBoundsExcept | 运行正常 |  |
| vault/Pro Evolution Soccer 2011.jar | 558 | PES 2011 | - | 1.0.0 | MIDP-2.0 | - | 9 | 16 | 600 | 5127301551 | 0 | java/lang/IndexOutOfBoundsExcept | 运行正常 |  |
| vault/ProBowling.jar | 155 | ProBowling [terjef] | - | 1.0.5 | MIDP-1.0 | - | 10 | 16 | 600 | 1367807737 | 0 | java/lang/IllegalMonitorStateExc | 运行正常 |  |
| vault/ProEvolutionSoccer2014_1.0.8_320x240.jar | 1045 | PES 2014 | - | 1.0.8 | MIDP-2.0 | - | 18 | 16 | 601 | 440430 | 0 | java/io/IOException,java/lang/Il | 运行正常 |  |
| vault/ProGolf2010_1.1.0_320x240.jar | 652 | Pro Golf 2010 | - | 1.1.0 | MIDP-2.0 | - | 6 | 16 | 600 | 2371446477 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/ProMotoRacing.jar | 598 | PRO MOTO RACING | - | 1.1.9 | MIDP-2.0 | - | 9 | 16 | 600 | 9929827 | 0 | java/lang/ArrayIndexOutOfBoundsE | 运行正常 |  |
| vault/Pro_Baseball_2007_176x220_sek750.wgz.cz.jar | 302 | Derek Jeter Pro Baseball 200 | - | 1.0.3 | MIDP-2.0 | nokia-ui | 4 | 16 | 600 | 11200950473 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/Pro_League_Soccer_sek700.ic.cz.jar | 71 | Pro League Soccer | - | 1.0.3 | MIDP-1.0 | nokia-ui | 26 | 16 | 600 | 5618582 | 0 | - | 运行正常 |  |
| vault/Pro_Racing_Rally S40v3.jar | 309 | Pro Rally Racing | - | 1.1.9 | MIDP-2.0 | nokia-ui | 10 | 16 | 600 | 4101498923 | 0 | java/lang/ArrayIndexOutOfBoundsE | 运行正常 |  |
| vault/Pro_Rally_Racing_176x220.jar | 293 | Pro Rally Racing | - | 1.1.8 | MIDP-2.0 | - | 9 | 16 | 600 | 3367085853 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/Project Gotham Racing.jar | 941 | PGR | - | 1.0.2 | MIDP-1.0 | - | 45 | 16 | 600 | 2520430 | 0 | - | 运行正常 |  |
| vault/ProjectSupermodel.jar | 66 | Project Supermodel(en) | - | 1.0 | MIDP-1.0 | nokia-ui | 11 | 16 | 600 | 200320 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/Prsi_176x220.jar | 35 | Prsi | - | 1.0 | MIDP-1.0 | - | 3 | 16 | 600 | 387349 | 0 | - | 运行正常 |  |
| vault/Pub Mania.jar | 566 | PubMania | - | 1.0.1 | MIDP-2.0 | - | 5 | 16 | 600 | 275058540 | 0 | javax/microedition/rms/InvalidRe | 运行正常 |  |
| vault/PubFunPaintball_by_mountinjacker.jar | 54 | Pub Fun Paintball [mountin j | - | 1.0.1 | MIDP-1.0 | nokia-ui | 16 | 16 | 600 | 307485 | 0 | java/io/IOException | 运行正常 |  |
| vault/PubPool.jar | 56 | PubPool | - | 1.0.5 | MIDP-1.0 | nokia-ui | 4 | 16 | 601 | 379387 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/Pub_Darts.jar | 80 | PubDarts | - | 0.0.2 | MIDP-1.0 | - | 3 | 16 | 600 | 164951 | 0 | java/io/EOFException,javax/micro | 运行正常 | runs without input; the generic key script m |
| vault/Pub_FootBall_K500_En (mountin jacker).jar | 114 | PubFootball [mountin jacker] | - | 1.0.51 | MIDP-1.0 | - | 19 | 16 | 600 | 506214 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/Pub_Mania_240.jar | 566 | PubMania | - | 1.0.1 | MIDP-2.0 | nokia-ui | 5 | 16 | 600 | 3860035239 | 0 | javax/microedition/rms/InvalidRe | 运行正常 |  |
| vault/Pursuit_Squad_by_BFLM.jar | 106 | Pursuit Squad | - | 1.1.0 | MIDP-1.0 | nokia-ui | 9 | 16 | 600 | 3468346176 | 0 | javax/microedition/media/MediaEx | 运行正常 |  |
| vault/Puzzle Bobble Evolution.jar | 531 | Puzzle Bobble Evolution | - | 0.8.0 | MIDP-1.0 | - | 17 | 16 | 600 | 4466891 | 0 | java/lang/IllegalArgumentExcepti | 运行正常 |  |
| vault/Puzzle Bobble.jar | 145 | Puzzle Bobble | - | 1.4.8 | MIDP-2.0 | - | 12 | 16 | 600 | 241572 | 0 | java/io/IOException | 运行正常 |  |
| vault/Puzzle Challenge Deluxe_320x240_Nokia_E71.jar | 259 | Puzzle Challenge Deluxe | - | 1.0.0 | MIDP-1.0 | - | 12 | 16 | 600 | 11693326 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/Puzzle Quest - Warlords.jar | 441 | Puzzle Quest - Warlords | - | 1.0.63 | MIDP-1.0 | - | 15 | 16 | 600 | 574263 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/Puzzle Tracks__320x240.jar | 348 | Puzzle Tracks | - | 1.2.0 | MIDP-2.0 | - | 12 | 16 | 600 | 2990507 | 0 | java/io/IOException | 运行正常 |  |
| vault/PuzzleQuest.jar | 327 | Puzzle Quest - Warlords | - | 1.0.47 | MIDP-1.0 | - | 11 | 16 | 600 | 18853232 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/Puzzle_Bobble_by_BFLM.jar | 39 | PuzzleBobble | - | 1.0.0 | MIDP-1.0 | nokia-ui | 3 | 16 | 600 | 633114 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/Puzzle_Bobble_v2_by_BFLM.jar | 62 | PuzzleBobble | - | 1.0.1 | MIDP-1.0 | nokia-ui | 3 | 16 | 600 | 391243 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/Puzzlebox_1.0.0_320x240.jar | 340 | Puzzle Box | - | 1.2.0 | MIDP-2.0 | - | 58 | 16 | 600 | 86377 | 0 | - | 运行正常 |  |
| vault/Pyramid Bloxx.jar | 303 | Pyramid Bloxx | - | 1.0.40 | MIDP-2.0 | nokia-ui | 12 | 16 | 600 | 34241092 | 0 | java/io/EOFException,java/lang/N | 运行正常 |  |
| vault/Pyramid.Bloxx.SE.176x220.jar | 266 | Pyramid Bloxx | - | 1.0.40 | MIDP-2.0 | - | 12 | 16 | 600 | 26560489 | 0 | java/io/EOFException,java/lang/N | 运行正常 |  |
| vault/QBlox_320x240.jar | 382 | QBlox | - | 1.0.8 | MIDP-2.0 | nokia-ui | 14 | 16 | 600 | 103284853 | 0 | java/io/IOException,java/lang/Ar | 运行正常 |  |
| vault/QuadraPop_for_SonyEricsson_K700i_K750i_D750i_W800i_W810i.jar | 114 | QuadraPop | - | 1.0 | MIDP-2.0 | - | 5 | 16 | 600 | 2842570 | 0 | - | 运行正常 |  |
| vault/R-Type.jar | 272 | R-Type | - | 0.0.5 | MIDP-2.0 | - | 7 | 16 | 600 | 606469 | 0 | java/io/IOException,java/lang/Nu | 运行正常 |  |
| vault/RF-manager-k750i.jar | 595 | RealFootballManager fixed by | - | 1.0.8 | MIDP-2.0 | nokia-ui | 9 | 16 | 600 | 136847045 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/ROLL_A_ROUND_K500_F500 (mountin jacker).jar | 63 | Roll A Round [mountin jacker | - | 1.0.1 | MIDP-1.0 | - | 5 | 16 | 600 | 26279 | 0 | java/io/IOException,java/lang/Nu | 运行正常 |  |
| vault/ROLL_A_ROUND_k700.jar | 68 | Roll A Round | - | 1.0.1 | MIDP-1.0 | - | 5 | 16 | 600 | 26230 | 0 | java/io/IOException,java/lang/Nu | 运行正常 |  |
| vault/RTLRacing2005.jar | 158 | RTL Racing 2005 | - | 1.00 | MIDP-1.0 | nokia-ui | 7 | 16 | 600 | 6331009 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/RTL_Soccer_sek700.ic.cz.jar | 140 | RTL Soccer | - | 1.1.6 | MIDP-1.0 | nokia-ui | 27 | 16 | 600 | 142080 | 0 | java/lang/ClassNotFoundException | 运行正常 |  |
| vault/Race Driver - Grid.jar | 970 | Race Driver Grid | - | 1.0.2 | MIDP-2.0 | - | 66 | 16 | 600 | 12176266 | 0 | - | 运行正常 |  |
| vault/Race Electrix_320x240.jar | 488 | Race Electrix | - | 1.1.5 | MIDP-1.0 | - | 3 | 16 | 600 | 1942288 | 0 | - | 运行正常 |  |
| vault/Racing Fever GT.jar | 253 | Racing Fever GT 3D | - | 1.0.6 | MIDP-2.0 | - | 28 | 16 | 600 | 12609122 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/RacingRescue.jar | 74 | Racing Rescue | - | 1.0 | MIDP-1.0 | nokia-ui | 27 | 16 | 600 | 159894 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/Racingfever2_sek700.wz.cz.jar | 102 | RacingFever2 | - | 1.0.5 | MIDP-1.0 | nokia-ui | 16 | 16 | 600 | 57286 | 0 | - | 运行正常 |  |
| vault/Rack_Em_Up__Billiard_Game_.jar | 77 | RackEmUp | - | 1.1.4 | MIDP-1.0 | nokia-ui | 39 | 16 | 600 | 480022 | 0 | - | 运行正常 |  |
| vault/RadioBee.jar | 140 | RadioBee | - | 1.4 | MIDP-2.0 | - | 54 | 16 | 0 | 47513 | 0 | java/lang/NullPointerException,j | 失败 |  |
| vault/Rainbow Invaders_320x240.jar | 112 | Rainbow Invaders | - | 1.0 | MIDP-2.0 | - | 13 | 16 | 600 | 26135922 | 0 | java/io/IOException,java/lang/Nu | 运行正常 |  |
| vault/Rainbow Six - Lockdown.jar | 63 | RSix_LockDown | - | 1.0.7 | MIDP-1.0 | nokia-ui | 6 | 16 | 600 | 368510 | 0 | - | 运行正常 |  |
| vault/Rainbow Six - Lockdown_2.jar | 199 | RainbowSixLockdown | - | 1.0.5 | MIDP-2.0 | - | 6 | 16 | 600 | 3199485 | 0 | - | 运行正常 |  |
| vault/Rally Master Pro.jar | 1155 | Rally Master Pro | - | 1.0 | MIDP-2.0 | - | 179 | 16 | 600 | 10834336 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/Rally Stars.jar | 512 | RallyStars-3D | - | 1.1.0 | MIDP-2.0 | - | 45 | 16 | 600 | 14994339 | 0 | java/io/IOException,java/lang/Nu | 运行正常 |  |
| vault/RallyProContest_3D.jar | 200 | RallyProContest | - | 1.1 | MIDP-1.0 | m3g | 20 | 16 | 600 | 3892417 | 0 | javax/microedition/rms/InvalidRe | 运行正常 |  |
| vault/Raptor.jar | 88 | Raptor | - | 1.0.7 | MIDP-2.0 | - | 3 | 16 | 600 | 208810 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/Ratatoulie-176x220.jar | 195 | Cheese Rush | - | 1.0.39 | MIDP-1.0 | - | 13 | 16 | 600 | 4054950390 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/Rayman 3.jar | 98 | Rayman3 2004 | - | 1.0.1 | MIDP-2.0 | - | 5 | 16 | 600 | 3950264106 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/Rayman 3_2.jar | 176 | Rayman 3 | - | 1.0.3 | MIDP-2.0 | - | 5 | 16 | 600 | 2011561690 | 0 | - | 运行正常 |  |
| vault/Rayman Kart (240x320).jar | 319 | Rayman Kart | - | 1.0.0 | MIDP-2.0 | - | 6 | 16 | 600 | 3531297633 | 0 | javax/microedition/rms/InvalidRe | 运行正常 |  |
| vault/Rayman Raving Rabbids.jar | 325 | Rayman Raving Rabbids -leo53 | - | 1.0.7 | MIDP-2.0 | - | 5 | 16 | 600 | 3338145222 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/Rayman3_by_BFLM.jar | 115 | Rayman3 | - | 0.3.80 | MIDP-1.0 | nokia-ui | 5 | 16 | 600 | 1855562085 | 0 | - | 运行正常 |  |
| vault/Rayman_Bowling_ v126-XiMpDA.jar | 157 | Rayman Bowling | - | 1.2.6 | MIDP-1.0 | nokia-ui | 16 | 16 | 600 | 3290626 | 0 | - | 运行正常 |  |
| vault/Rayman_Golf.jar | 123 | Rayman Golf | - | 1.0.7 | MIDP-1.0 | nokia-ui | 13 | 16 | 600 | 1296279878 | 0 | - | 运行正常 |  |
| vault/Rball_v515.jar | 120 | XiMPDA- R-Ball(TM) | - | 5.1.5 | MIDP-1.0 | nokia-ui | 28 | 16 | 600 | 571953 | 0 | - | 运行正常 |  |
| vault/Real Football 2007.jar | 341 | Real Football 2007 | - | 1.0.1 | MIDP-2.0 | - | 9 | 16 | 600 | 79129495 | 0 | java/lang/NumberFormatException | 运行正常 |  |
| vault/Real Football 2008 K700i 176x220 CZ.jar | 296 | Real Football 2008 | - | 1.0.1 | MIDP-2.0 | - | 7 | 16 | 600 | 36012106 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/Real Football 2008 K750i 176x220 CZ.jar | 316 | Real Football 2008 | - | 1.0.1 | MIDP-2.0 | - | 7 | 16 | 600 | 36119503 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/Real Football 2008.jar | 598 | Real Football 2008 | - | 1.0.1 | MIDP-2.0 | - | 8 | 16 | 600 | 210718288 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/Real Football 2009.jar | 702 | Real Football 2009 | - | 1.1.3 | MIDP-2.0 | - | 12 | 16 | 600 | 185312793 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/Real Football 2010.jar | 990 | Real Football 2010 | - | 1.1.2 | MIDP-2.0 | - | 17 | 16 | 600 | 37736592 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/Real Football 2011.jar | 1069 | Real Football 2011 | - | 1.1.6 | MIDP-2.0 | - | 16 | 16 | 600 | 31340158 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/Real Football 2012.jar | 1188 | Real Football 2012 | - | 1.1.4 | MIDP-2.0 | - | 15 | 16 | 600 | 41386423 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/Real Football 2013.jar | 1360 | Real Football 2013 | - | 1.1.9 | MIDP-2.0 | - | 19 | 16 | 600 | 53720579 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/Real Football 2014.jar | 1296 | Real Football 2014 | - | 1.0.1 | MIDP-2.0 | - | 13 | 16 | 600 | 65346529 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/Real Football 2015.jar | 1038 | Real Football 2015 | - | 1.0.0 | MIDP-2.0 | - | 14 | 16 | 600 | 138779995 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/Real Football 2016.jar | 1002 | Real Football 2016 | - | 1.0.1 | MIDP-2.0 | - | 16 | 16 | 600 | 113644953 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/Real Football 2017.jar | 1015 | Real Football 2017 | - | 1.0.3 | MIDP-2.0 | - | 16 | 16 | 600 | 129738652 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/Real Football 2018.jar | 1020 | Real Football 2018 | - | 1.0.1 | MIDP-2.0 | - | 16 | 16 | 600 | 127692104 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/RealFootball2007-3D_1.0.6_320x240.jar | 1003 | RealFootball2007 3D | - | 1.0.6 | MIDP-2.0 | m3g,nokia-ui | 7 | 16 | 600 | 6873345681 | 0 | java/io/IOException,java/lang/Nu | 运行正常 |  |
| vault/RealFootball2012_1.1.2_320x240.jar | 898 | Real Football 2012 | - | 1.1.2 | MIDP-2.0 | nokia-ui | 14 | 16 | 600 | 590406329 | 0 | - | 运行正常 | runs without input; the generic key script d |
| vault/RealFootballManagerEdition_1.0.0_320x240.jar | 599 | Real Football Manager Editio | - | 1.0.0 | MIDP-2.0 | - | 10 | 16 | 600 | 3401433610 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/RealRuby_1.0.5_320x240.jar | 484 | Real Rugby | - | 1.0.5 | MIDP-2.0 | - | 7 | 16 | 600 | 7296966305 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/Real_2_Football_2007.jar | 335 | RealFootball2007 | - | 1.0.7 | MIDP-2.0 | - | 7 | 16 | 600 | 3243374774 | 0 | - | 运行正常 |  |
| vault/Red Bull Motocross.jar | 834 | RedBullMotoCross2D | - | 0.3.9 | MIDP-2.0 | - | 52 | 16 | 600 | 6251660 | 0 | java/io/EOFException,javax/micro | 运行正常 |  |
| vault/Red Out Racer.jar | 244 | Red Out Racer | - | 1.0.2 | MIDP-2.0 | - | 23 | 16 | 600 | 1890019 | 0 | javax/microedition/media/MediaEx | 运行正常 |  |
| vault/Reggie Bush Pro Football 2007.jar | 344 | Reggie Bush Pro Football 200 | - | 1.0.3 | MIDP-2.0 | - | 6 | 16 | 600 | 3898765 | 0 | javax/microedition/rms/InvalidRe | 运行正常 |  |
| vault/Reggie_Bush_2007.jar | 290 | Reggie Bush Pro Football 200 | - | 1.1.3 | MIDP-2.0 | - | 6 | 16 | 600 | 4579802 | 0 | javax/microedition/rms/InvalidRe | 运行正常 |  |
| vault/Resident Evil - The Missions.jar | 974 | Resident Evil - The Missions | - | 1.0.0 | MIDP-2.0 | - | 5 | 16 | 601 | 1497848 | 0 | - | 运行正常 |  |
| vault/Resident-Evil---The-Missions.jar | 934 | Resident Evil - The Missions | - | 1.0.0 | MIDP-2.0 | mascot | 5 | 16 | 601 | 1261224 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/ResidentEvil-Degeneration_s40v3a.jar | 371 | Resident Evil Degeneration | - | 1.0.0 | MIDP-2.0 | - | 10 | 16 | 601 | 2905480 | 0 | java/lang/NumberFormatException | 运行正常 |  |
| vault/Resident_Evil_ATM.jar | 108 | Resident Evil - ATN | - | 1.2.0 | MIDP-1.0 | - | 13 | 16 | 600 | 546362 | 0 | javax/microedition/rms/InvalidRe | 运行正常 |  |
| vault/Ribbit_sek700.wz.cz.jar | 51 | Ribbit | - | 1.0 | MIDP-1.0 | nokia-ui | 5 | 16 | 600 | 207053 | 0 | - | 运行正常 |  |
| vault/Richmans60_sek700.wz.cz.jar | 61 | RichMan7650Eng | - | 1.0 | MIDP-1.0 | nokia-ui | 8 | 16 | 600 | 66572 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/Rick_Ranger_sek700.ic.cz.jar | 118 | Rick Ranger | - | 1.0.8 | MIDP-1.0 | nokia-ui | 24 | 16 | 601 | 2354330 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/Ridge Racer Drift.jar | 1268 | RidgeRacerD | - | 1.0.1 | MIDP-2.0 | - | 28 | 16 | 600 | 190565764 | 0 | javax/microedition/rms/InvalidRe | 运行正常 |  |
| vault/Ridge Racer.jar | 398 | Ridge Racer | - | 1.0.7 | MIDP-2.0 | - | 35 | 16 | 600 | 82292567 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/Ridge_Racer_3D.jar | 290 | RIDGE RACER | - | 1.0.3 | MIDP-2.0 | mascot | 33 | 16 | 600 | 13243878 | 0 | java/lang/IllegalStateException, | 运行正常 | runs without input; the generic key script t |
| vault/RiseOfLostEmpires_1.0.0_320x240.jar | 320 | Rise of Lost Empires | - | 1.0.0 | MIDP-2.0 | - | 10 | 16 | 600 | 4100795547 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/Risk_320x240.jar | 956 | RISK | - | 1.11.2 | MIDP-2.0 | nokia-ui | 12 | 16 | 600 | 11139333 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/Robin Hood_320x240_Nokia_E71.jar | 998 | Robin Hood | - | 4.8.0 | MIDP-2.0 | - | 15 | 16 | 600 | 14107431 | 0 | java/io/EOFException | 运行正常 |  |
| vault/Robobombo.jar | 338 | Robobombo | - | 1.0.7 | MIDP-2.0 | - | 61 | 16 | 600 | 2259389 | 0 | java/lang/ClassNotFoundException | 运行正常 |  |
| vault/Robot Battle Tactics.jar | 321 | Robot Battle Tactics | - | 1.0.0 | MIDP-2.0 | - | 14 | 16 | 600 | 4454379 | 0 | - | 运行正常 |  |
| vault/Robot Maker_320x240.jar | 357 | Robot Maker TEGOS.ru | - | 1.0.0 | MIDP-2.0 | - | 10 | 16 | 600 | 3897288 | 0 | - | 运行正常 |  |
| vault/RolandGarros2009_1.0.8_320x240.jar | 604 | Roland-Garros 2009 | - | 1.0.8 | MIDP-2.0 | - | 11 | 16 | 600 | 86309833 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/Roll that Ball 2.jar | 512 | Roll that Ball 2 | - | 1.0.1 | MIDP-2.0 | - | 18 | 16 | 600 | 369622431 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/Roll that Ball.jar | 568 | Roll that Ball | - | 1.0.0 | MIDP-2.0 | - | 18 | 16 | 600 | 330747742 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/RollerBlade.jar | 54 | Rollerblade | - | 1.0 | MIDP-1.0 | nokia-ui | 7 | 16 | 600 | 84910 | 0 | - | 运行正常 |  |
| vault/Roller_Coaster_Rush.jar | 170 | Coaster Rush | - | 1.2.05 | MIDP-1.0 | nokia-ui | 17 | 16 | 600 | 1152467 | 0 | java/io/EOFException,javax/micro | 运行正常 |  |
| vault/Rollerbot3D_K700F.jar | 124 | RollerBot [terjef] | - | 1.1.0 | MIDP-1.0 | mascot | 55 | 16 | 600 | 7260146956 | 0 | java/lang/IllegalStateException, | 运行正常 |  |
| vault/Rollercoaster Revolution - 99 Tracks.jar | 845 | Abahn Rev 99 | - | 1.0.10 | MIDP-2.0 | - | 39 | 16 | 600 | 3381751 | 0 | java/io/EOFException,javax/micro | 运行正常 |  |
| vault/Rollercoaster Revolution 99 Tracks_320x240.jar | 666 | Roller Rev 99 | - | 1.0.20 | MIDP-2.0 | nokia-ui | 39 | 16 | 600 | 63262878 | 0 | java/io/EOFException,java/lang/C | 运行正常 |  |
| vault/Rollercoaster Rush.jar | 199 | Coaster Rush [terjef] | - | 1.2.32 | MIDP-1.0 | - | 17 | 16 | 600 | 1610397 | 0 | java/io/EOFException,javax/micro | 运行正常 |  |
| vault/Rollercoaster.Rush.3D.SE.176x220.jar | 249 | 3D Coaster Rush | - | 1.1.9 | MIDP-2.0 | m3g,mascot,nokia-ui | 19 | 16 | 600 | 4407950 | 0 | java/io/EOFException,java/lang/N | 运行正常 |  |
| vault/Rollercoaster.Rush.SE.176x220.jar | 184 | Rollercoaster Rush | - | 1.2.28 | MIDP-1.0 | - | 16 | 16 | 600 | 1441021 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/Rome - Throne of Destiny.jar | 232 | Rome Throne Of Destiny | - | 1.0.0 | MIDP-2.0 | - | 12 | 16 | 600 | 1170921 | 0 | java/io/IOException,javax/microe | 运行正常 |  |
| vault/Romeo and Juliet.jar | 254 | R&J | - | 1.0.10 | MIDP-2.0 | - | 23 | 16 | 600 | 4001092912 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/Roomservice_sek700.wz.cz.jar | 54 | RoomService | - | 1.0 | MIDP-1.0 | nokia-ui | 5 | 16 | 600 | 194266 | 0 | - | 运行正常 |  |
| vault/Roque.jar | 94 | - | - | - | - | - | 0 | 16 | 0 | 0 | 0 | - | 非可用包 | not a usable MIDlet suite (bad JAR or no MID |
| vault/Roverreunion_v5110_XiMPDA.jar | 135 | XiMPDA- Rover Reunion(TM) | - | 5.1.10 | MIDP-1.0 | nokia-ui | 12 | 16 | 600 | 334179 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/Ruarc_BFLM.jar | 96 | Legends of Ruarc | - | 1.04 | MIDP-1.0 | nokia-ui | 8 | 16 | 600 | 1798572 | 0 | - | 运行正常 |  |
| vault/Rubik__s_Cube.jar | 190 | Rubik's Cube | - | 1.0.0 | MIDP-1.0 | nokia-ui | 22 | 16 | 600 | 252607 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/Rubiks.MathTris.240x320.jar | 278 | Rubiks MathTris | - | 1.1.0 | MIDP-1.0 | - | 7 | 16 | 600 | 694532 | 0 | java/io/EOFException,java/lang/A | 运行正常 |  |
| vault/SC-PT.jar | 179 | SC-PT | - | 1.0.5 | MIDP-1.0 | nokia-ui | 6 | 16 | 600 | 3324426856 | 0 | java/lang/NegativeArraySizeExcep | 运行正常 |  |
| vault/SCU240X320.jar | 320 | Special Crime Unit | - | 1.0.2 | MIDP-2.0 | - | 5 | 16 | 600 | 3887777586 | 0 | - | 运行正常 |  |
| vault/SC_PT_K700_ES_101a.jar | 178 | Splinter Cell PT | - | 1.0.1 | MIDP-2.0 | - | 2 | 16 | 600 | 3326913480 | 0 | - | 运行正常 | runs without input; the generic key script m |
| vault/SC_PT_K700_ES_101b.jar | 177 | Splinter Cell PT | - | 1.0.1 | MIDP-2.0 | mascot | 13 | 16 | 600 | 13143258 | 1 | java/lang/NullPointerException,j | 标题自身异常 | game code threw java.lang.NullPointerExcepti |
| vault/SC_PT_K700_IT_101a.jar | 178 | Splinter Cell PT | - | 1.0.1 | MIDP-2.0 | - | 2 | 16 | 600 | 3367875784 | 0 | - | 运行正常 | runs without input; the generic key script m |
| vault/SC_PT_K700_IT_101b.jar | 177 | Splinter Cell PT | - | 1.0.1 | MIDP-2.0 | mascot | 13 | 16 | 600 | 13314818 | 1 | java/lang/NullPointerException,j | 标题自身异常 | game code threw java.lang.NullPointerExcepti |
| vault/SEALTeam6.jar | 89 | SEAL Team 6 | - | 1.0.4 | MIDP-1.0 | nokia-ui | 8 | 16 | 600 | 1027227 | 0 | java/io/IOException,java/lang/Nu | 运行正常 |  |
| vault/SEAL_Team_6.jar | 89 | SEAL Team 6 | - | 1.0.4 | MIDP-1.0 | nokia-ui | 8 | 16 | 600 | 1027227 | 0 | java/io/IOException,java/lang/Nu | 运行正常 |  |
| vault/SEGA Rally 3D.jar | 958 | SEGA Rally 3D | - | 1.0.5 | MIDP-2.0 | - | 44 | 16 | 600 | 217522597 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/SU-30.jar | 434 | Su-30 | - | 1.8.0 | MIDP-2.0 | - | 55 | 16 | 600 | 4917863 | 0 | java/io/EOFException,java/io/IOE | 运行正常 |  |
| vault/SallysStudio_1.0.4_320x240.jar | 1004 | Sallys Studio | - | 1.0.4 | MIDP-2.0 | - | 14 | 16 | 600 | 64657845 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/SaufkollegeME.jar | 44 | SaufkollegeME | - | 0.4 | MIDP-1.0 | - | 42 | 16 | 600 | 174 | 0 | java/lang/IndexOutOfBoundsExcept | 运行正常 | runs without input; the generic key script t |
| vault/Schnik_Schnak_by BFLM.jar | 97 | Schnik Schnak Schnuk | - | 1.0 | MIDP-1.0 | nokia-ui | 14 | 16 | 600 | 71420 | 0 | - | 运行正常 |  |
| vault/Scientistcode.jar | 44 | ScientistCode | - | 1.2 | MIDP-1.0 | nokia-ui | 9 | 16 | 600 | 1839565 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/Scooby-Doo Castle Capers_by_mountinjacker.jar | 82 | Scooby-Doo Castle Capers | - | 1.0.3 | MIDP-1.0 | - | 9 | 16 | 600 | 687155 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/ScoobyDooCastleCaper_BFLM.jar | 81 | Scooby-Doo Castle Capers | - | 1.0.0 | MIDP-1.0 | nokia-ui | 9 | 16 | 600 | 740664 | 0 | - | 运行正常 |  |
| vault/ScoobyDoo_CastleCapers(by_wl).jar | 82 | Scooby-Doo Castle Capers | - | 1.0.3 | MIDP-1.0 | - | 9 | 16 | 600 | 689377 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/Scoobydoo2_DarkDungeon(by_wl).jar | 82 | Scooby-Doo 2 Dark Dungeons | - | 1.0.0 | MIDP-1.0 | nokia-ui | 9 | 16 | 600 | 631601 | 0 | - | 运行正常 |  |
| vault/Scooter_Lovers_v100_XiMPDA.jar | 92 | Scooter Lovers | - | 1.0.0 | MIDP-1.0 | nokia-ui | 11 | 16 | 600 | 75297491 | 0 | java/lang/ArrayIndexOutOfBoundsE | 运行正常 |  |
| vault/Scpandoratomorrow_sek700.wz.cz.jar | 193 | SC-PT | - | 1.5.7 | MIDP-2.0 | - | 6 | 16 | 600 | 1701339912 | 0 | javax/microedition/rms/InvalidRe | 运行正常 |  |
| vault/Scrabble Mobile_1.1.1_320x240-en.jar | 825 | Scrabble | - | 1.1.1 | MIDP-2.0 | - | 4 | 16 | 600 | 6391914271 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/SeaBounty_nokia_240x320.jar | 219 | SeaBounty by serviak | - | 1.0.19 | MIDP-2.0 | nokia-ui | 82 | 16 | 600 | 737146 | 0 | - | 运行正常 |  |
| vault/SeaHunter.jar | 48 | Sea Hunter | - | 1.0 | MIDP-1.0 | - | 20 | 16 | 600 | 1319426 | 0 | java/lang/ClassNotFoundException | 运行正常 |  |
| vault/Searescue.jar | 38 | Sea Rescue Color | - | 1.11.12 | MIDP-1.0 | nokia-ui | 10 | 16 | 600 | 143495 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/SegaRally3D_SK_by_mycyno.jar | 1091 | SEGA Rally 3D SK | - | 1.0.5 | MIDP-2.0 | mascot,nokia-ui | 80 | 16 | 600 | 3882883647 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/Sega_Home_Run_King2.jar | 133 | Home Run King 2 | - | 1.1 | MIDP-1.0 | nokia-ui | 19 | 16 | 600 | 151664 | 0 | - | 运行正常 |  |
| vault/ShadoWalker.jar | 518 | Shadowalker | - | 1.0.3 | MIDP-1.0 | - | 12 | 16 | 600 | 722344 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/Shadowalker.jar | 591 | Shadowalker | - | 1.0.6 | MIDP-1.0 | - | 14 | 16 | 600 | 20832210 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/Shaft_K700.jar | 123 | Shaft [terjef] | - | 3.4.4 | MIDP-2.0 | - | 16 | 16 | 600 | 19924 | 0 | - | 运行正常 |  |
| vault/Shamegamealysons60_sek700.wz.cz.jar | 121 | Shamegame Alyson | - | 1.2.1 | MIDP-1.0 | nokia-ui | 15 | 16 | 600 | 1922506 | 0 | - | 运行正常 |  |
| vault/Shark_Attack_by_BFLM.jar | 47 | Shark Attack | - | 1.1 | MIDP-1.0 | nokia-ui | 14 | 16 | 600 | 6073 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/Sharkshuntingv101-_sek700.wz.cz.jar | 66 | Sharks Hunting | - | 1.0.1 | MIDP-1.0 | nokia-ui | 13 | 16 | 600 | 2810 | 0 | - | 运行正常 | runs without input; the generic key script m |
| vault/Shaun-of-the-dead-mobility.freeforfun.org.jar | 79 | SOTD | - | 1.0.07 | MIDP-1.0 | - | 10 | 16 | 600 | 6858533 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/SheepMania.jar | 86 | SheepMania | - | 1.0 | MIDP-1.0 | nokia-ui | 12 | 16 | 600 | 1591706 | 0 | - | 运行正常 |  |
| vault/Sheet.jar | 39 | Sheet | - | 1.0.0 | MIDP-1.0 | - | 31 | 16 | 600 | 101394 | 0 | - | 运行正常 |  |
| vault/SherlockHolmes_1.0.9_320x240.jar | 640 | Sherlock Holmes: The Officia | - | 1.0.9 | MIDP-2.0 | - | 10 | 16 | 600 | 24794595 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/Shizuka_sek700.ic.cz.jar | 195 | Shizuka | - | 1.0.0 | MIDP-1.0 | nokia-ui | 9 | 16 | 600 | 16514 | 0 | java/lang/NullPointerException,j | 运行正常 | runs without input; the generic key script m |
| vault/Shizuka_vXXX.jar | 165 | Shizuka | - | 1.0.0 | MIDP-2.0 | - | 6 | 16 | 600 | 2905122 | 0 | java/io/IOException,java/lang/Nu | 运行正常 |  |
| vault/Shrek The Third.jar | 600 | Shrek the Third : The Offici | - | 1.0.0 | MIDP-2.0 | - | 6 | 16 | 600 | 2919919509 | 0 | - | 运行正常 |  |
| vault/Shrek2.jar | 62 | Shrek 2 Adventure | - | 3.2 | MIDP-1.0 | nokia-ui | 17 | 16 | 601 | 652140 | 0 | - | 运行正常 |  |
| vault/Shrek2_sek700.wz.cz.jar | 180 | Shrek2 | - | 2.5 | MIDP-1.0 | nokia-ui | 17 | 16 | 600 | 417969 | 0 | - | 运行正常 |  |
| vault/Shrek2trivia_sek700.wz.cz.jar | 80 | Shrek 2 Trivia | - | 1.3 | MIDP-1.0 | nokia-ui | 4 | 16 | 600 | 76953 | 0 | - | 运行正常 |  |
| vault/ShrekForeverAfter_1.0.3_320x240.jar | 437 | Shrek Forever After | - | 1.0.2 | MIDP-2.0 | - | 15 | 16 | 600 | 3048198 | 0 | java/lang/ClassNotFoundException | 运行正常 |  |
| vault/ShrekParty_1.0.1_320x240.jar | 678 | Shrek Party | - | 1.0.1 | MIDP-2.0 | - | 7 | 16 | 600 | 28735852 | 0 | java/lang/ArrayIndexOutOfBoundsE | 运行正常 |  |
| vault/ShrekTheThird CZ_176x220.jar | 186 | ShrekTheThird | - | 1.0.2 | MIDP-2.0 | - | 5 | 16 | 600 | 4234301184 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/ShrekTheThird-complete.jar | 594 | Shrek the Third : The Offici | - | 1.0.10 | MIDP-2.0 | - | 6 | 16 | 600 | 4210330716 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/ShrekTheThird_1.1.2_320x240.jar | 579 | Shrek the Third: The Officia | - | 1.1.2 | MIDP-2.0 | - | 7 | 16 | 600 | 2906820084 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/Shurikenstorm_sek700.wz.cz.jar | 52 | Shuriken Storm | - | 1.0 | MIDP-1.0 | nokia-ui | 9 | 16 | 600 | 2280266 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/Siberian Strike.jar | 207 | Siberian Strike | - | 1.0.0 | MIDP-2.0 | - | 6 | 16 | 600 | 1070354 | 0 | - | 运行正常 |  |
| vault/SiberianStrike_SonyEricssonK700_Z1010_UK_103.jar | 110 | Siberian Strike | - | 1.0.3 | MIDP-2.0 | - | 5 | 16 | 600 | 553916 | 0 | - | 运行正常 |  |
| vault/Siberian_Strike_ep1.jar | 140 | Siberian Ep I | - | 1.1.2 | MIDP-1.0 | nokia-ui | 5 | 16 | 600 | 953646 | 0 | java/io/IOException | 运行正常 |  |
| vault/Siberian_Strike_ep2.jar | 174 | Siberian Ep II | - | 1.1.2 | MIDP-1.0 | nokia-ui | 5 | 16 | 600 | 1158804 | 0 | java/io/IOException | 运行正常 |  |
| vault/Siberian_strike_Ep_I.jar | 140 | Siberian Ep I | - | 1.1.2 | MIDP-1.0 | nokia-ui | 5 | 16 | 600 | 869129 | 0 | java/io/IOException | 运行正常 |  |
| vault/Sid Meiers Civilization 4 Defender Of The Gates_320x240_Nokia_E71.jar | 363 | Civilization IV Defenders Of | - | 1.2.6 | MIDP-2.0 | - | 12 | 16 | 600 | 6060087 | 0 | java/io/IOException | 运行正常 |  |
| vault/SidMeiersCivilization5_1.0.6_320x240.jar | 635 | Sid Meier's Civilization V M | - | 1.0.6 | MIDP-2.0 | - | 11 | 16 | 600 | 11693639 | 0 | - | 运行正常 |  |
| vault/Siege.jar | 82 | Siege | - | 0.8.0 | MIDP-1.0 | nokia-ui | 10 | 16 | 0 | 0 | 0 | - | 超时 |  |
| vault/SifraMistraLeonarda.jar | 390 | SifraMistraLeonarda | - | 2.2.8 | MIDP-2.0 | nokia-ui | 4 | 16 | 600 | 1703166 | 0 | - | 运行正常 |  |
| vault/Silent Hill Mobile 2.jar | 475 | Silent Hill Mobile 2 | - | 1.0.0 | MIDP-2.0 | - | 12 | 16 | 600 | 8286718 | 0 | java/io/IOException,java/lang/Nu | 运行正常 |  |
| vault/Silent Hill Mobile 3.jar | 579 | Silent Hill 3 | - | 1.0.20 | MIDP-2.0 | - | 13 | 16 | 600 | 2680857 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/Silent Hill Mobile.jar | 476 | Silent Hill | - | 0.1.0 | MIDP-2.0 | - | 10 | 16 | 600 | 3291811 | 0 | java/io/IOException,javax/microe | 运行正常 |  |
| vault/Silent Hunter_320x240_Nokia_E71.jar | 730 | Silent Hunter | - | 1.0 | MIDP-2.0 | - | 45 | 16 | 600 | 3635065 | 0 | java/io/IOException,javax/microe | 运行正常 |  |
| vault/Silent-Hill-SK_.jar | 465 | Silent Hill SK | - | 0.1.0 | MIDP-2.0 | - | 10 | 16 | 600 | 3647286 | 0 | java/io/IOException,javax/microe | 运行正常 |  |
| vault/Sim Babe 2 k700i 176x220 CZ.jar | 248 | SimBabe2 | - | 1.4 | MIDP-2.0 | - | 2 | 16 | 600 | 2662819157 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/SimBabe_240x320.jar | 244 | Sim Babe | - | 1.07 | MIDP-2.0 | nokia-ui | 3 | 16 | 600 | 1164134 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/SimCity Deluxe.jar | 914 | SimCity Deluxe | - | 1.12.37 | MIDP-2.0 | - | 180 | 16 | 600 | 8488099 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/SimCity Metropolis_320x240.jar | 779 | SimCityM | - | 1.23.87 | MIDP-2.0 | - | 25 | 16 | 600 | 151757324 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/Sim_City.jar | 168 | SimCity | - | 4.8.86 | MIDP-1.0 | - | 9 | 16 | 600 | 6503505265 | 0 | java/lang/NullPointerException,j | 运行正常 | runs without input; the generic key script m |
| vault/Simcity_Societies.jar | 604 | Simcity Societies | - | 1.11.96 | MIDP-2.0 | nokia-ui | 12 | 16 | 600 | 153419028 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/Sk8terBoy.jar | 118 | Sk8terBoy | - | 1.3.5 | MIDP-1.0 | nokia-ui | 19 | 16 | 600 | 247718 | 0 | - | 运行正常 |  |
| vault/Skate K700 176x220.jar | 340 | skate. | - | 1.19.56 | MIDP-2.0 | - | 40 | 16 | 600 | 4808712 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/Skate by EA Mobile.jar | 442 | Skate by piratesxxl.110mb.co | - | 1.23.79 | MIDP-2.0 | - | 40 | 16 | 600 | 4704815 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/Skate&Slam_by_BFLM.jar | 97 | Skate&Slam | - | 1.0.7 | MIDP-1.0 | nokia-ui | 4 | 16 | 600 | 3526463 | 0 | - | 运行正常 |  |
| vault/Skate-176x220.jar | 447 | skate._by_http://x-mobile.ho | - | 1.23.79 | MIDP-2.0 | - | 40 | 16 | 600 | 4737538 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/Skateboardv100-_sek700.wz.cz.jar | 64 | Skateboard | - | 1.0.0 | MIDP-1.0 | nokia-ui | 28 | 16 | 600 | 2800377 | 0 | java/io/IOException | 运行正常 |  |
| vault/Skater_by_BFLM.jar | 81 | Skater | - | 1.1 | MIDP-1.0 | nokia-ui | 13 | 16 | 600 | 4799747762 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/Sketcher (eFusion).jar | 301 | Sketcher | - | 1.0.1 | MIDP-2.0 | - | 11 | 16 | 600 | 3246162096 | 0 | - | 运行正常 |  |
| vault/SkiJump_EN_v104.jar | 114 | SkiJump [terjef] | - | 1.0.4 | MIDP-1.0 | - | 17 | 16 | 600 | 244873 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/SkidCity_N60.jar | 70 | skidcity | - | 0.91 | MIDP-1.0 | nokia-ui | 8 | 16 | 600 | 33874 | 0 | - | 运行正常 |  |
| vault/Skull_Castle_Pinball_by_BFLM.jar | 57 | Skull Castle Pinball | - | 1.0.3 | MIDP-1.0 | nokia-ui | 6 | 16 | 600 | 381159 | 0 | - | 运行正常 |  |
| vault/Skydiver.jar | 48 | - | - | - | - | nokia-ui | 0 | 16 | 600 | 202931 | 0 | javax/microedition/rms/RecordSto | 运行正常 | runs without input; the generic key script m |
| vault/Skydiver_mountin jacker.jar | 48 | - | - | - | - | nokia-ui | 0 | 16 | 601 | 202771 | 0 | javax/microedition/rms/RecordSto | 运行正常 | runs without input; the generic key script m |
| vault/Sleigh_ride_BFLM.jar | 75 | SleighRide | - | 1.0 | MIDP-1.0 | nokia-ui | 9 | 16 | 600 | 1445242 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/Slide a Lama Deluxe-176x220.jar | 210 | Slide-A-Lama Deluxe by www.n | - | 1.0.0 | MIDP-2.0 | - | 41 | 16 | 600 | 4673637 | 0 | java/io/IOException,java/lang/Il | 运行正常 |  |
| vault/Slide a Lama.jar | 172 | Slide-a-lama | - | 1.0.0 | MIDP-1.0 | - | 28 | 16 | 600 | 2487979 | 1 | java/lang/NoSuchMethodError,java | 标题自身异常 | game code threw an exception in a thread whi |
| vault/Slide-a-lama.jar | 206 | Slide-A-Lama Deluxe by www.n | - | 1.0.0 | MIDP-2.0 | - | 41 | 16 | 600 | 4549450 | 0 | java/io/IOException,java/lang/Il | 运行正常 |  |
| vault/SlotMachine.jar | 175 | SlotMachine XXX | - | 1.0 | MIDP-1.0 | nokia-ui | 19 | 16 | 600 | 48575 | 0 | - | 运行正常 |  |
| vault/Slugfest_sek700.wz.cz.jar | 61 | SlugFest | - | 1.0 | MIDP-1.0 | nokia-ui | 5 | 16 | 600 | 71120 | 0 | - | 运行正常 |  |
| vault/Slyder.jar | 91 | Slyder | - | 1.0.6 | MIDP-2.0 | nokia-ui | 3 | 16 | 600 | 229420 | 0 | java/io/EOFException,java/lang/N | 运行正常 |  |
| vault/Smack_Head_Tony_Blair_by_BFLM.jar | 68 | Blair | - | 1.0.00 | MIDP-1.0 | nokia-ui | 8 | 16 | 600 | 738430 | 0 | javax/microedition/rms/InvalidRe | 运行正常 |  |
| vault/SmartMovie.jar | 937 | - | - | - | - | - | 0 | 16 | 0 | 0 | 0 | - | 非可用包 | not a zip file |
| vault/SmartMovie2.jar | 941 | - | - | - | - | - | 0 | 16 | 0 | 0 | 0 | - | 非可用包 | not a zip file |
| vault/Snake III.jar | 293 | Snake III | - | 3.07.2 | MIDP-2.0 | - | 49 | 16 | 600 | 131592 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/Snake Reloaded_320x240_Nokia_E71.jar | 619 | Snake Reloaded | - | 1.0.17 | MIDP-2.0 | nokia-ui | 47 | 16 | 600 | 69829247 | 0 | java/io/EOFException,javax/micro | 运行正常 |  |
| vault/Snowboard Hero.jar | 751 | SnowboardHero | - | 1.0.1 | MIDP-2.0 | - | 135 | 16 | 600 | 8889770 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/Sokoban.jar | 35 | Sokoban | - | 1.1 | MIDP-1.0 | - | 14 | 16 | 600 | 9578389 | 0 | java/io/EOFException | 运行正常 |  |
| vault/Solitaire 8 in 1 2011_320x240.jar | 366 | Solitaire8x1 | - | 2.0 | MIDP-2.0 | nokia-ui | 8 | 16 | 600 | 8507452 | 0 | - | 运行正常 |  |
| vault/Solitaire Deluxe 16 Pack_320x240.jar | 427 | Solitaire Dlx | - | 1.5.24 | MIDP-2.0 | - | 51 | 16 | 600 | 26870311 | 0 | java/io/IOException,javax/microe | 运行正常 |  |
| vault/Solitaire Pop_320x240.jar | 526 | Solitaire Pop | - | 1.0.0 | MIDP-2.0 | - | 7 | 16 | 600 | 10383637 | 0 | java/lang/ArrayIndexOutOfBoundsE | 运行正常 |  |
| vault/Sonic & SEGA All-Star Racing.jar | 1015 | Sonic Racing | - | 1.2.2 | MIDP-2.0 | - | 24 | 16 | 600 | 8347579 | 0 | - | 运行正常 |  |
| vault/Sonic Advance.jar | 1073 | Sonic Advance | - | 1.3.0 | MIDP-2.0 | - | 174 | 16 | 600 | 2391943 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/Sonic Jump.jar | 328 | Sonic Jump | - | 1.0.3 | MIDP-1.0 | - | 7 | 16 | 602 | 4337055 | 0 | java/io/IOException,javax/microe | 运行正常 |  |
| vault/Sonic Runners Adventures.jar | 966 | Sonic Runners Adventure | - | 1.0.8 | MIDP-2.0 | - | 14 | 16 | 600 | 27246883 | 0 | - | 运行正常 |  |
| vault/Sonic Spinball.jar | 637 | Sonic Spinball by konon | - | 4.0.20 | MIDP-2.0 | - | 14 | 16 | 600 | 28141272 | 0 | java/lang/ArrayIndexOutOfBoundsE | 运行正常 |  |
| vault/Sonic Spinball_320x240.jar | 560 | Sonic Spinball | - | 4.0.75 | MIDP-2.0 | - | 16 | 16 | 600 | 23697819 | 0 | java/lang/ArrayIndexOutOfBoundsE | 运行正常 |  |
| vault/Sonic Unleashed.jar | 848 | SonicUnleashed | - | 1.2.5 | MIDP-2.0 | - | 15 | 16 | 600 | 183726898 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/SonicAdvance_1.4.0_320x240.jar | 1057 | Sonic Advance | - | 1.4.0 | MIDP-2.0 | - | 174 | 16 | 600 | 2712930 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/SonicSegaAllStars_1.0.2_320x240.jar | 659 | Sonic Racing | - | 1.0.2 | MIDP-2.0 | - | 26 | 16 | 600 | 43179638 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/SonicTheHedgehogPartOne_6280.jar | 278 | Sonic The Hedgehog Part One | - | 1.0.46 | MIDP-2.0 | - | 5 | 16 | 600 | 4200118 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/SonicTheHedgehogPartTwo_6280.jar | 339 | Sonic The Hedgehog Part Two | - | 1.0.2 | MIDP-2.0 | nokia-ui | 5 | 16 | 600 | 1499157 | 0 | java/io/IOException,java/lang/Nu | 运行正常 |  |
| vault/SonicUnleashed_1.2.2_320x240.jar | 642 | SonicUnleashed | - | 1.2.2 | MIDP-2.0 | - | 11 | 16 | 600 | 287775136 | 0 | - | 运行正常 |  |
| vault/Soul Calibur Mobile.jar | 1334 | SOULCALIBUR Mobile by Namco | - | 1.0.0 | MIDP-2.0 | - | 14 | 16 | 600 | 6769462 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/Soul of Darkness.jar | 631 | Soul Of Darkness | - | 1.3.2 | MIDP-2.0 | - | 8 | 16 | 600 | 363502553 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/SoulOfDarkness_HTC_P3450_EN_IGPL_EU_101_S8000.jar | 617 | Soul Of Darkness | - | 1.0.1 | MIDP-2.0 | - | 6 | 16 | 0 | 0 | 0 | - | 超时 |  |
| vault/SouthParkSportsDay.jar | 104 | South Park Sports Day | - | 1.0.0 | MIDP-2.0 | - | 14 | 16 | 600 | 3274121759 | 0 | javax/microedition/rms/RecordSto | 运行正常 | runs without input; the generic key script m |
| vault/SouthParkSportsDay_K700.jar | 104 | South_Park_Sports_Day | - | 1.0.35 | MIDP-2.0 | - | 14 | 16 | 600 | 3076221509 | 0 | javax/microedition/rms/RecordSto | 运行正常 | runs without input; the generic key script m |
| vault/South_Park.jar | 116 | SouthPark | - | 1.9 | MIDP-1.0 | - | 8 | 16 | 600 | 121540 | 0 | - | 运行正常 |  |
| vault/SpaceBalls_by_Mellie75rox.jar | 64 | SpaceBalls | - | 1.0 | MIDP-1.0 | nokia-ui | 37 | 16 | 600 | 898980 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/SpaceCraft.jar | 72 | Space Craft | - | 1.0 | MIDP-1.0 | nokia-ui | 25 | 16 | 600 | 268760 | 0 | java/io/IOException | 运行正常 |  |
| vault/SpaceInvaderEvolution_0.5.6_320x240.jar | 350 | Space Invader Evolution | - | 0.5.6 | MIDP-2.0 | - | 42 | 16 | 600 | 4460478 | 0 | - | 运行正常 |  |
| vault/Space_Impact_III.jar | 62 | Space Impact III | - | 1.2 | MIDP-1.0 | nokia-ui | 11 | 16 | 600 | 1514020 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/Space_Taxi_Pinball.jar | 108 | Space Taxi | - | 1.0.6 | MIDP-1.0 | nokia-ui | 12 | 16 | 600 | 4265855 | 0 | java/io/EOFException,javax/micro | 运行正常 |  |
| vault/Spacewarrior_sek700.wz.cz.jar | 102 | Space Warrior | - | 1.0 | MIDP-1.0 | nokia-ui | 16 | 16 | 600 | 134369 | 0 | - | 运行正常 |  |
| vault/Speed Racer.jar | 886 | Speed Racer 3D | - | 1.2.9 | MIDP-2.0 | - | 26 | 16 | 600 | 842903 | 0 | - | 运行正常 |  |
| vault/Spider-Man - Toxic City.jar | 995 | Spider-Man: Toxic City | - | 1.1.6 | MIDP-2.0 | - | 15 | 16 | 600 | 95483971 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/Spider-Man - Ultimate Power.jar | 1542 | Spider-Man: Ultimate Power | - | 1.2.3 | MIDP-2.0 | - | 18 | 16 | 600 | 69901982 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/SpiderMan3_128x160.jar | 224 | Spider-Man 3 | - | 0.11.81 | MIDP-2.0 | - | 5 | 16 | 600 | 17969968 | 0 | javax/microedition/rms/RecordSto | 运行正常 | runs without input; the generic key script m |
| vault/Spiderman.2.pinball_sek700.wz.cz.jar | 71 | Spider-Man 2 Pinball | - | 0.1.8 | MIDP-1.0 | nokia-ui | 2 | 16 | 600 | 2375903 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/Splashdown.jar | 96 | Splashdown | - | 1.25.0 | MIDP-1.0 | - | 13 | 16 | 600 | 10457264 | 0 | javax/microedition/rms/InvalidRe | 运行正常 |  |
| vault/Splatterhouse.jar | 549 | Splatterhouse by Namco | - | 1.0.0 | MIDP-2.0 | - | 14 | 16 | 600 | 13180845 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/Splinter Cell - 3 - Pandora Tommorow.jar | 126 | SC-PT | - | 1.1.0 | MIDP-1.0 | - | 6 | 16 | 600 | 1676154199 | 0 | javax/microedition/rms/InvalidRe | 运行正常 |  |
| vault/Splinter Cell - 4 - Chaos Theory.jar | 99 | SplinterCell CT | - | 1.1.3 | MIDP-2.0 | - | 4 | 16 | 600 | 2789593505 | 0 | - | 运行正常 |  |
| vault/Splinter Cell 3D-sek750i.uw.hu.jar | 347 | Splinter Cell PT | - | 1.0.1 | MIDP-2.0 | mascot | 13 | 16 | 600 | 1908469137 | 0 | java/lang/NoSuchMethodError,java | 运行正常 | runs without input; the generic key script t |
| vault/SplinterCell4.jar | 63 | SC-CT | - | 1.1.3 | MIDP-1.0 | nokia-ui | 3 | 16 | 600 | 14124489 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/SplinterCellChao_K700F.jar | 197 | SC-CT [terjef] | - | 1.1.4 | MIDP-2.0 | nokia-ui | 5 | 16 | 600 | 3450825981 | 0 | - | 运行正常 |  |
| vault/SplinterCellConviction_1.1.5_320x240_bresilien.jar | 638 | Splinter Cell Conviction | - | 1.1.5 | MIDP-2.0 | - | 11 | 16 | 600 | 11764395 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/SplinterCellDoubleAgent_1.0.1_320x240.jar | 317 | Splinter Cell DA | - | 1.0.1 | MIDP-2.0 | nokia-ui | 6 | 16 | 601 | 67484393 | 0 | java/lang/ArrayIndexOutOfBoundsE | 运行正常 |  |
| vault/Splinter_Cell_3d_DEMO.jar | 183 | Splinter Cell PT Demo | - | 1.0.5 | MIDP-2.0 | mascot | 13 | 16 | 600 | 1778415841 | 0 | javax/microedition/rms/InvalidRe | 运行正常 | runs without input; the generic key script m |
| vault/Splinter_Cell_Chaos_Theory_Full_K700.jar | 203 | SC-CT | - | 1.1.5 | MIDP-1.0 | nokia-ui | 5 | 16 | 600 | 3614112962 | 0 | - | 运行正常 |  |
| vault/Splinter_Cell_Double_Agent.jar | 303 | Splinter Cell DA | - | 1.0.5 | MIDP-2.0 | - | 6 | 16 | 600 | 3554680477 | 0 | - | 运行正常 |  |
| vault/Splinter_Cell_Eng_by_BFLM.jar | 99 | Splinter Cell | - | 1.1.3 | MIDP-1.0 | nokia-ui | 7 | 16 | 600 | 3083378502 | 0 | - | 运行正常 |  |
| vault/Spongebob - Bikini Bottom Pursuit.jar | 390 | Bob Esponja | - | 1.1.0 | MIDP-2.0 | - | 21 | 16 | 600 | 496533 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/Spooks_1.0.0_320x240.jar | 411 | Spooks The Mobile Game | - | 1.0.0 | MIDP-2.0 | - | 7 | 16 | 600 | 17285123 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/Spore Creatures.jar | 758 | SPORE CREATURES | - | 4.0.52 | MIDP-2.0 | - | 42 | 16 | 600 | 63668253 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/SporeCreatures_4.9.20_320x240.jar | 663 | SPORE CREATURES | - | 4.9.20 | MIDP-2.0 | - | 42 | 16 | 600 | 5268430 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/SporeOrigins_4.9.0_320x240.jar | 805 | SPORE | - | 4.9.0 | MIDP-2.0 | - | 28 | 16 | 600 | 3181972721 | 0 | java/lang/IllegalArgumentExcepti | 运行正常 |  |
| vault/Spring_by_BFLM.jar | 66 | Spring | - | 2.0.0 | MIDP-1.0 | nokia-ui | 31 | 16 | 600 | 1246587 | 0 | - | 运行正常 |  |
| vault/Spyro Eternal Night_320x240.jar | 370 | SpyroEternal | - | 0.5.9 | MIDP-2.0 | nokia-ui | 6 | 16 | 600 | 7559268 | 0 | java/io/EOFException,javax/micro | 运行正常 |  |
| vault/Spyro The Dragon.jar | 145 | Spyro The Dragon K750 | - | 1.3.0 | MIDP-2.0 | - | 3 | 16 | 600 | 2668794604 | 0 | - | 运行正常 | runs without input; the generic key script d |
| vault/Spyro_K700B.jar | 95 | Spyro [terjef] | - | 1.6.1 | MIDP-1.0 | - | 3 | 16 | 600 | 272532 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/Spyro_Ripto_Quest.jar | 95 | Spyro | - | 1.6.0 | MIDP-1.0 | - | 3 | 16 | 600 | 271626 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/Stalker2.jar | 441 | Stalker_2 srv | - | 1.0 | MIDP-2.0 | nokia-ui | 9 | 16 | 600 | 4858887511 | 0 | java/io/EOFException | 运行正常 |  |
| vault/Star Defence 176x220.jar | 135 | Star Defence_konno | - | 1.0.0 | MIDP-2.0 | nokia-ui | 7 | 16 | 600 | 986649 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/Star Invasion_320x240.jar | 1003 | Star Invasion | - | 1.0.21 | MIDP-2.0 | nokia-ui | 17 | 16 | 600 | 53748847 | 0 | java/io/EOFException,javax/micro | 运行正常 |  |
| vault/Star Wars The Clone Wars_320x240.jar | 416 | Star Wars - The Clone Wars | - | 1.0.35 | MIDP-1.0 | nokia-ui | 7 | 16 | 600 | 9606027 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/Star.diver_sek700.ic.cz.jar | 81 | Star diver | - | 1.0 | MIDP-1.0 | nokia-ui | 7 | 16 | 600 | 128639 | 0 | - | 运行正常 | runs without input; the generic key script m |
| vault/StarCraft.jar | 1014 | Ghost | - | 1.0.0 | MIDP-1.0 | - | 9 | 16 | 600 | 2909176449 | 0 | - | 运行正常 |  |
| vault/Street Fighter Alpha - Warriors' Dreams.jar | 1041 | Street Fighter Alpha | - | 1.1.5 | MIDP-2.0 | - | 19 | 16 | 600 | 59446444 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/Strip_Blackjack_Adrian.jar | 124 | Strip Blackjack Adrian | - | 1.10 | MIDP-1.0 | nokia-ui | 11 | 16 | 600 | 163793 | 0 | java/io/IOException | 运行正常 |  |
| vault/Strip_Blackjack_Kath.jar | 117 | Strip Blackjack Kath | - | 1.10 | MIDP-1.0 | nokia-ui | 11 | 16 | 600 | 154299 | 0 | java/io/IOException | 运行正常 |  |
| vault/Strip_Tak_Toe.jar | 137 | Strip Tak Toe | - | 1.1.3 | MIDP-2.0 | nokia-ui | 14 | 16 | 600 | 5920015104 | 0 | - | 运行正常 |  |
| vault/Stuntman.jar | 89 | Stuntman1 | - | 1.2.5 | MIDP-1.0 | nokia-ui | 5 | 16 | 601 | 1262441 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/SubEscape_K700B.jar | 62 | Sub Escape [terjef] | - | 1.0 | MIDP-1.0 | nokia-ui | 18 | 16 | 600 | 255010 | 0 | javax/microedition/rms/InvalidRe | 运行正常 |  |
| vault/Subaru_Rally_Challenge_176x208(x-MoBiLA)_sek750[1].blog.cz.jar | 390 | Subaru Rally Challenge by x- | - | 1.0.2 | MIDP-2.0 | - | 11 | 16 | 600 | 15460144 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/SubmarineFB_sek700.ic.cz.jar | 69 | Submarine Final Battle | - | 1.0 | MIDP-1.0 | - | 9 | 16 | 600 | 764706 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/Submarine_Panic_by_BFLM.jar | 97 | subMarine Panic | - | 1.0 | MIDP-1.0 | nokia-ui | 16 | 16 | 600 | 184526 | 0 | - | 运行正常 |  |
| vault/Submarine_sek700.ic.cz.jar | 62 | Submarine | - | 1.0 | MIDP-1.0 | nokia-ui | 14 | 16 | 600 | 937511 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/Sudoku DChoc Cafe__320x240.jar | 290 | Sudoku | - | 1.1.25 | MIDP-2.0 | nokia-ui | 18 | 16 | 600 | 13492772 | 0 | java/io/EOFException,java/lang/N | 运行正常 |  |
| vault/Summer_Games_by_BFLM.jar | 59 | Summer Games 2004 | - | 1.3.1 | MIDP-1.0 | nokia-ui | 6 | 16 | 600 | 1684832 | 0 | java/lang/StackOverflowError,jav | 运行正常 |  |
| vault/Super Bomberman.jar | 150 | Super Bomberman | - | 1.1.1 | MIDP-1.0 | - | 4 | 16 | 600 | 4918907 | 0 | java/lang/ArrayIndexOutOfBoundsE | 运行正常 |  |
| vault/Super Breakout.jar | 222 | Super Breakout | - | 1.0.4 | MIDP-2.0 | - | 28 | 16 | 600 | 31839645 | 0 | java/lang/ClassCastException,jav | 运行正常 |  |
| vault/Super Monkey Ball - Tip'n Tilt 2.jar | 300 | SMB TNT2 | - | 1.2.8 | MIDP-2.0 | - | 28 | 16 | 600 | 532390 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/Super Monkey Ball - Tip'n Tilt.jar | 362 | Super Monkey Ball Tip n Tilt | - | 1.0.1 | MIDP-2.0 | - | 7 | 16 | 600 | 7179413 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/Super Puzzle Bobble.jar | 142 | Super Puzzle Bobble | - | 1.4.8 | MIDP-2.0 | - | 12 | 16 | 600 | 231472 | 0 | java/io/IOException | 运行正常 |  |
| vault/Super Water Bomber.jar | 318 | Super Water Bomber | - | 1.0.3 | MIDP-2.0 | - | 18 | 16 | 600 | 12200233 | 0 | java/io/EOFException,javax/micro | 运行正常 |  |
| vault/SuperMarioTheLostLevels_1.0_320x240.jar | 133 | 超级玛利兄弟2 | - | 1.0 | MIDP-2.0 | - | 17 | 16 | 600 | 3474055 | 0 | java/io/IOException | 运行正常 |  |
| vault/SuperYumYum.jar | 194 | Super Yum Yum | - | 1.1.0 | MIDP-1.0 | nokia-ui | 16 | 16 | 600 | 1481933 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/Super_Bomberman_s60_by_techmob.jar | 169 | Super Bomberman | - | 1.0.1 | MIDP-1.0 | nokia-ui | 5 | 16 | 600 | 1481189196 | 0 | java/lang/ArrayIndexOutOfBoundsE | 运行正常 |  |
| vault/Superman & Batman - Heroes United.jar | 457 | Superman Batman Heroes Unite | - | 1.0.5 | MIDP-2.0 | - | 6 | 16 | 600 | 2899754 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/Supreme Soccer.jar | 162 | XiMPDA- SupremeSoccer | - | 1.1.1 | MIDP-1.0 | nokia-ui | 10 | 16 | 600 | 1702871 | 0 | java/io/IOException,java/lang/Nu | 运行正常 |  |
| vault/Supreme_Soccer_v10_XiMPDA.jar | 162 | XiMPDA- SupremeSoccer | - | 1.1.1 | MIDP-1.0 | nokia-ui | 10 | 16 | 600 | 1702393 | 0 | java/io/IOException,java/lang/Nu | 运行正常 |  |
| vault/Supremesnowboarding_sek700.wz.cz.jar | 114 | Supreme Snowboarding | - | 1.1.8 | MIDP-1.0 | - | 3 | 16 | 600 | 1091286249 | 0 | java/lang/NoSuchMethodError,java | 运行正常 | runs without input; the generic key script t |
| vault/SurfIsUp_1.2.3_320x240.jar | 310 | Surf's Up | - | 1.3.3 | MIDP-2.0 | - | 6 | 16 | 600 | 4079568863 | 0 | - | 运行正常 |  |
| vault/Survivor.jar | 74 | Survivor | - | 1.1.0 | MIDP-1.0 | - | 15 | 16 | 600 | 127898 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/Survivor_K700F.jar | 161 | Survivor [terjef] | - | 1.0.3 | MIDP-1.0 | - | 16 | 16 | 600 | 233551 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/Swapper_320x240.jar | 266 | Swapper | - | 1.0.0 | MIDP-2.0 | - | 34 | 16 | 600 | 2704012081 | 0 | java/io/IOException,java/lang/Cl | 运行正常 |  |
| vault/Swat-force-mobility.freeforfun.org.jar | 150 | SWAT Force Etty | - | 1.5.4 | MIDP-2.0 | - | 3 | 16 | 600 | 331788 | 0 | - | 运行正常 | runs without input; the generic key script d |
| vault/TRANSFORMERS (176x208) 1.0.4 [Andrew-Lviv.net].jar | 566 | Transformers by street_racer | - | 1.0.4 | MIDP-2.0 | m3g | 8 | 16 | 600 | 31529821 | 0 | java/lang/IllegalArgumentExcepti | 运行正常 | runs without input; the generic key script t |
| vault/TRANSFORMERS (176x208) 1.0.4 [Dedomil.net].jar | 569 | Transformers by street_racer | - | 1.0.4 | MIDP-2.0 | m3g | 8 | 16 | 600 | 31530047 | 0 | java/lang/IllegalArgumentExcepti | 运行正常 | runs without input; the generic key script t |
| vault/TRANSFORMERS (240x320) 1.0.1 [Andrew-Lviv.net].jar | 520 | Transformers | - | 1.0.2 | MIDP-2.0 | m3g | 86 | 16 | 600 | 31518897 | 0 | java/lang/IndexOutOfBoundsExcept | 运行正常 | runs without input; the generic key script t |
| vault/TRANSFORMERS (240x320) 1.0.1 [Dedomil.net].jar | 614 | Transformers | - | 1.0.1 | MIDP-2.0 | m3g | 8 | 16 | 601 | 9202477 | 0 | javax/microedition/rms/InvalidRe | 运行正常 |  |
| vault/TRANSFORMERS (240x320) 1.0.2 [Dedomil.net].jar | 523 | Transformers | - | 1.0.2 | MIDP-2.0 | m3g | 86 | 16 | 600 | 31518920 | 0 | java/lang/IndexOutOfBoundsExcept | 运行正常 | runs without input; the generic key script t |
| vault/TRANSFORMERS (320x240) 1.0.1 [Andrew-Lviv.net].jar | 626 | Transformers | - | 1.0.1 | MIDP-2.0 | m3g | 8 | 16 | 600 | 31530038 | 0 | java/lang/IndexOutOfBoundsExcept | 运行正常 | runs without input; the generic key script t |
| vault/TRANSFORMERS (320x240) 1.0.1 [Dedomil.net].jar | 629 | Transformers | - | 1.0.1 | MIDP-2.0 | m3g | 8 | 16 | 600 | 31530053 | 0 | java/lang/IndexOutOfBoundsExcept | 运行正常 | runs without input; the generic key script t |
| vault/TRANSFORMERS G1_ AWAKENING (240x320) 1.0.4 [Dedomil.net].jar | 646 | Transformers G1 Awakening | - | 1.0.4 | MIDP-1.0 | m3g | 18 | 16 | 600 | 44504753 | 0 | java/io/EOFException | 运行正常 |  |
| vault/TRANSFORMERS G1_ AWAKENING (320x240) 1.0.0 [Dedomil.net].jar | 337 | Transformers G1 Awakening | - | 1.0.0 | MIDP-1.0 | - | 12 | 16 | 600 | 27702269 | 0 | java/io/EOFException | 运行正常 |  |
| vault/TRANSFORMERS_ DARK OF THE MOON (240x320) 4.4.95 [Andrew-Lviv.net].jar | 1019 | Transformers Dark of the Moo | - | 4.4.95 | MIDP-2.0 | - | 84 | 16 | 600 | 6947301 | 0 | - | 运行正常 |  |
| vault/TRANSFORMERS_ DARK OF THE MOON (240x320) 4.4.95 [Dedomil.net].jar | 1019 | Transformers Dark of the Moo | - | 4.4.95 | MIDP-2.0 | - | 84 | 16 | 600 | 6941324 | 0 | - | 运行正常 |  |
| vault/TRANSFORMERS_ DARK OF THE MOON (240x320) 4.5.15 [Dedomil.net].jar | 1020 | Transformers Dark of the Moo | - | 4.5.15 | MIDP-2.0 | - | 84 | 16 | 600 | 6947638 | 0 | - | 运行正常 |  |
| vault/TRANSFORMERS_ DARK OF THE MOON (240x320) 4.5.20 [Dedomil.net].jar | 1023 | Transformers Dark of the Moo | - | 4.5.20 | MIDP-2.0 | - | 84 | 16 | 600 | 7239190 | 0 | - | 运行正常 |  |
| vault/TRANSFORMERS_ DARK OF THE MOON (240x320) 4.7.22 [Andrew-Lviv.net].jar | 802 | Transformers Dark of the Moo | - | 4.7.22 | MIDP-2.0 | - | 82 | 16 | 600 | 4769154 | 0 | - | 运行正常 |  |
| vault/TRANSFORMERS_ DARK OF THE MOON (240x320) 4.7.22 [Dedomil.net].jar | 802 | Transformers Dark of the Moo | - | 4.7.22 | MIDP-2.0 | - | 82 | 16 | 600 | 4769166 | 0 | - | 运行正常 |  |
| vault/TRANSFORMERS_ DARK OF THE MOON (320x240) 4.6.74 [Andrew-Lviv.net].jar | 786 | Transformers Dark of the Moo | - | 4.9.49 | MIDP-2.0 | - | 80 | 16 | 600 | 12017121 | 0 | - | 运行正常 |  |
| vault/TRANSFORMERS_ DARK OF THE MOON (320x240) 4.6.74 [Dedomil.net].jar | 786 | Transformers Dark of the Moo | - | 4.6.74 | MIDP-2.0 | - | 80 | 16 | 600 | 12016828 | 0 | - | 运行正常 |  |
| vault/TRANSFORMERS_ DARK OF THE MOON (360x640) 4.6.51 [Dedomil.net].jar | 1012 | Transformers.3.:.Dark.of.the | - | 4.6.51 | MIDP-2.0 | - | 85 | 16 | 600 | 5461374 | 0 | - | 运行正常 |  |
| vault/TRANSFORMERS_ DARK OF THE MOON (360x640) 4.7.86 [Dedomil.net].jar | 1012 | Transformers.3.:.Dark.of.the | - | 4.7.86 | MIDP-2.0 | - | 85 | 16 | 600 | 5459491 | 0 | - | 运行正常 |  |
| vault/TRANSFORMERS_ DARK OF THE MOON (360x640) 4.8.25 [Andrew-Lviv.net].jar | 1012 | Transformers.3.:.Dark.of.the | - | 4.8.25 | MIDP-2.0 | - | 85 | 16 | 600 | 5080327 | 0 | - | 运行正常 |  |
| vault/TRANSFORMERS_ DARK OF THE MOON (360x640) 4.8.25 [Dedomil.net].jar | 1012 | Transformers.3.:.Dark.of.the | - | 4.8.25 | MIDP-2.0 | - | 85 | 16 | 600 | 5123012 | 0 | - | 运行正常 |  |
| vault/TRANSFORMERS_ REVENGE OF THE FALLEN (240x320) 1.0.0 Multi6 [Andrew-Lviv.net].jar | 721 | TRANSFORMERS: Revenge of the | - | 1.0.0 | MIDP-2.0 | - | 13 | 16 | 600 | 34452939 | 0 | java/lang/ArrayIndexOutOfBoundsE | 运行正常 |  |
| vault/TRANSFORMERS_ REVENGE OF THE FALLEN (240x320) 1.0.0 Multi6 [Dedomil.net].jar | 721 | TRANSFORMERS: Revenge of the | - | 1.0.0 | MIDP-2.0 | - | 13 | 16 | 600 | 33730885 | 0 | java/lang/ArrayIndexOutOfBoundsE | 运行正常 |  |
| vault/TRANSFORMERS_ REVENGE OF THE FALLEN (320x240) 1.0.0 Multi3 [PHONEKY].jar | 597 | TRANSFORMERS: ROTF | - | 1.0.0 | MIDP-2.0 | - | 31 | 16 | 600 | 52480272 | 0 | java/lang/ArrayIndexOutOfBoundsE | 运行正常 |  |
| vault/TRANSFORMERS_ REVENGE OF THE FALLEN (360x640) 1.0.0 [Glu Mobile Ltd].jar | 731 | TRANSFORMERS Revenge of the  | - | 1.0.0 | MIDP-2.0 | - | 36 | 16 | 600 | 32641129 | 0 | java/lang/ArrayIndexOutOfBoundsE | 运行正常 |  |
| vault/Taiko_no_Tatsujin_Japanese128x128-40143-mobiles24.jar | 214 | Taiko no Tatsujin | - | 1.0.0 | MIDP-2.0 | - | 10 | 16 | 600 | 92016717 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/Taxi_In_City.jar | 79 | XiMPDA- Taxi In City | - | 1.10 | MIDP-1.0 | nokia-ui | 2 | 16 | 600 | 1144588 | 0 | java/io/IOException | 运行正常 |  |
| vault/Team Monster Us vs Them_320x240.jar | 526 | Team Monster | - | 0.9.15 | MIDP-2.0 | - | 5 | 16 | 600 | 16822091 | 0 | java/io/IOException,javax/microe | 运行正常 |  |
| vault/Teamcsctourcycling_sek700.wz.cz.jar | 131 | Team CSC Tour Cycling | - | 1.0 | MIDP-1.0 | nokia-ui | 11 | 16 | 601 | 3051905 | 0 | - | 运行正常 |  |
| vault/Teenpatti_sek700.wz.cz.jar | 47 | TeenPatti | - | 1.0 | MIDP-1.0 | nokia-ui | 5 | 16 | 600 | 569014 | 0 | - | 运行正常 |  |
| vault/Tekken Mobile.jar | 759 | TEKKEN MOBILE | - | 1.0.7 | MIDP-2.0 | - | 32 | 16 | 600 | 5411541 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/Temple Rush 2.jar | 267 | Temple Rush 2 | - | 1.0.4 | MIDP-2.0 | - | 21 | 16 | 600 | 319098 | 0 | java/io/IOException,java/lang/St | 运行正常 |  |
| vault/Temple Rush.jar | 269 | Temple Rush | - | 1.0.3 | MIDP-2.0 | - | 21 | 16 | 600 | 319161 | 0 | java/io/IOException,java/lang/St | 运行正常 |  |
| vault/Tenchu.jar | 148 | Tenchu | - | 1.2.0 | MIDP-1.0 | nokia-ui | 7 | 16 | 600 | 1735551 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/Tennis-Open_-by-Picho_rk.jar | 187 | - | - | - | - | - | 0 | 16 | 0 | 0 | 0 | - | 非可用包 | not a zip file |
| vault/TennisChampion_sek700.ic.cz.jar | 75 | Tennis Champion | - | 1.10.14 | MIDP-1.0 | nokia-ui | 15 | 16 | 600 | 253916 | 0 | java/io/IOException,java/lang/Nu | 运行正常 |  |
| vault/Tennis_Pro_Neocroma_by_BFLM.jar | 84 | Tennis Pro | - | 1.0.1 | MIDP-1.0 | nokia-ui | 5 | 16 | 600 | 372391 | 0 | javax/microedition/rms/InvalidRe | 运行正常 |  |
| vault/Terminator Salvation.jar | 939 | Terminator Salvation | - | 1.1.0 | MIDP-2.0 | - | 8 | 16 | 600 | 243872880 | 0 | - | 运行正常 |  |
| vault/TerminatorRevenge_v[1].2.0_S60_by_piratesxxl.wz.cz.jar | 240 | Terminator Revenge by Street | - | 1.0.13 | MIDP-2.0 | nokia-ui | 12 | 16 | 600 | 8512571 | 0 | javax/microedition/media/MediaEx | 运行正常 |  |
| vault/TerminatorSalvation_1.0.5_320x240.jar | 471 | Terminator Salvation | - | 1.0.5 | MIDP-2.0 | - | 16 | 16 | 600 | 8159268 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/Tetris (2008).jar | 102 | Tetris | - | 1.21.29 | MIDP-1.0 | - | 57 | 16 | 600 | 1432780 | 0 | java/util/NoSuchElementException | 运行正常 |  |
| vault/Tetris Blockout_320x240.jar | 202 | Tetris Blockout | - | 4.6.17 | MIDP-2.0 | nokia-ui | 14 | 16 | 601 | 3791951 | 0 | java/io/IOException,java/lang/Ar | 运行正常 |  |
| vault/Tetris Mobile.jar | 361 | TETRIS® | - | 1.0.0 | MIDP-2.0 | - | 34 | 16 | 600 | 8616310 | 0 | - | 运行正常 |  |
| vault/Tetris_1.0.0_320x240.jar | 423 | TETRIS® | - | 1.0.0 | MIDP-2.0 | nokia-ui | 34 | 16 | 600 | 82001 | 0 | - | 运行正常 | self-paced title: runs 600 frames unpaced (t |
| vault/Tetris_320x240.jar | 636 | Tetris-New | - | 4.13.15 | MIDP-2.0 | m3g,nokia-ui | 114 | 16 | 600 | 31083902 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/Tetrisdlx.jar | 78 | Tetris Deluxe | - | 1.0 | MIDP-1.0 | nokia-ui | 48 | 16 | 0 | 281 | 0 | java/lang/NullPointerException | 失败 |  |
| vault/Tetron.jar | 89 | Tetron | - | 1.0.2 | MIDP-1.0 | nokia-ui | 16 | 16 | 600 | 23846 | 1 | java/lang/NullPointerException,j | 标题自身异常 | game code threw an exception in a thread whi |
| vault/TexasHoldemPoker1_1.1.0_320x240.jar | 413 | Texas HoldEm Poker | - | 1.1.0 | MIDP-2.0 | nokia-ui | 14 | 16 | 600 | 8692315938 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/TexasHoldemPoker2_1.3.3_320x240.jar | 1026 | Texas Hold'Em Poker 2 | - | 1.3.3 | MIDP-2.0 | - | 25 | 16 | 600 | 308291277 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/The Adventures of Tintin.jar | 1775 | The Adventures of Tintin | - | 1.1.4 | MIDP-2.0 | - | 12 | 16 | 600 | 23104573 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/The Amazing Spider-Man 2.jar | 2201 | The Amazing Spider Man 2 | - | 1.0.4 | MIDP-2.0 | - | 17 | 16 | 600 | 52173520 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/The Amazing Spider-Man.jar | 1538 | The Amazing Spider Man | - | 1.2.7 | MIDP-2.0 | - | 12 | 16 | 600 | 222640093 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/The Avengers.jar | 1386 | The Avengers | - | 1.3.0 | MIDP-2.0 | - | 12 | 16 | 600 | 204222580 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/The Bourne Conspiracy.jar | 528 | The Bourne Conspiracy | - | 0.3.4 | MIDP-1.0 | - | 6 | 16 | 600 | 10874775 | 0 | java/io/IOException | 运行正常 |  |
| vault/The Chronicles of Narnia - The Voyage of the Dawn Treader.jar | 1226 | Narnia 3 | - | 1.2.3 | MIDP-2.0 | - | 10 | 16 | 600 | 114923761 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/The Dark Knight Rises.jar | 1466 | The Dark Knight Rises | - | 1.2.5 | MIDP-2.0 | - | 15 | 16 | 600 | 65859847 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/The Fast And The Furious - Fugitive.jar | 710 | Fast and Furious Fugitive | - | 0.8.4 | MIDP-2.0 | - | 3 | 16 | 600 | 15989265 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/The Fast and The Furious - Pink Slip 3D.jar | 665 | Fast and Furious Streets | - | 0.5.5 | MIDP-2.0 | - | 30 | 16 | 600 | 3376204 | 0 | java/io/IOException,java/lang/Ar | 运行正常 |  |
| vault/The House of the Dead - Nightmare (1).jar | 967 | THE HOUSE OF THE DEAD-NIGHTM | - | 1.0.0 | MIDP-1.0 | - | 152 | 16 | 600 | 192847 | 0 | java/lang/ArrayIndexOutOfBoundsE | 运行正常 |  |
| vault/The Legend of Spyro - A New Beginning.jar | 307 | Spyro Legend | - | 0.6.6 | MIDP-1.0 | - | 5 | 16 | 600 | 849162 | 0 | java/io/EOFException,javax/micro | 运行正常 |  |
| vault/The Legend of Spyro - Dawn of the Dragon.jar | 466 | Spyro Dawn of the Dragon | - | 0.4.4 | MIDP-2.0 | - | 7 | 16 | 600 | 1806158 | 0 | java/io/IOException,javax/microe | 运行正常 |  |
| vault/The Legend of Spyro - The Eternal Night.jar | 187 | SEN | - | 0.6.0 | MIDP-2.0 | - | 5 | 16 | 600 | 7145579 | 0 | java/io/EOFException,javax/micro | 运行正常 |  |
| vault/The Lord of the Rings - Middle-Earth Defence.jar | 1105 | LOTR Middle-earth Defence | - | 1.0.0 | MIDP-2.0 | - | 27 | 16 | 600 | 6018442 | 0 | javax/microedition/rms/InvalidRe | 运行正常 |  |
| vault/The Mummy 3 - Tomb of the Dragon Emperor.jar | 611 | The Mummy: TOTDE | - | 1.1.6 | MIDP-2.0 | - | 6 | 16 | 600 | 48954963 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/The Mummy-176x220.jar | 571 | The Mummy: TOTDE | - | 1.1.2 | MIDP-2.0 | m3g,nokia-ui | 11 | 16 | 600 | 5964821167 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/The Oregon Trail - American Settler.jar | 1291 | Oregon Trail American Settle | - | 1.2.3 | MIDP-2.0 | - | 24 | 16 | 600 | 20864178 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/The Oregon Trail - Gold Rush.jar | 987 | The Oregon Trail: Gold Rush | - | 1.5.4 | MIDP-2.0 | - | 20 | 16 | 600 | 72665495 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/The Oregon Trail.jar | 594 | The Oregon Trail | - | 1.1.6 | MIDP-2.0 | - | 10 | 16 | 600 | 6470649 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/The Overtaker 3D.jar | 499 | The Overtaker 3D | - | 1.0.8 | MIDP-2.0 | - | 8 | 16 | 600 | 13279855 | 0 | java/io/IOException,java/lang/Nu | 运行正常 |  |
| vault/The Settlers.jar | 596 | The Settlers | - | 1.0.3 | MIDP-2.0 | - | 6 | 16 | 600 | 11058406 | 0 | - | 运行正常 |  |
| vault/The Simpsons - Itchy & Scratchy Land.jar | 767 | The Simpsons 2 | - | 7.1.97 | MIDP-2.0 | - | 8 | 16 | 600 | 3577384 | 0 | javax/microedition/rms/InvalidRe | 运行正常 |  |
| vault/The Simpsons - Minutes to Meltdown.jar | 395 | The Simpsons | - | 4.1.18 | MIDP-2.0 | - | 10 | 16 | 600 | 137426 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/The Sims 2 Mobile.jar | 252 | The Sims 2 Mobile | - | 1.0.5 | MIDP-1.0 | nokia-ui | 18 | 16 | 600 | 3474828685 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/The Sims 3 Ambitions_320x240.jar | 817 | The Sims 3 Ambitions | - | 4.2.28 | MIDP-2.0 | - | 26 | 16 | 600 | 12559604 | 0 | java/io/EOFException | 运行正常 |  |
| vault/The Sims DJ_320x240.jar | 636 | The Sims DJ | - | 7.2.47 | MIDP-2.0 | m3g | 34 | 16 | 600 | 45062782 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/TheAdventuresOfTintin_1.0.1_320x240.jar | 1068 | The Adventures of Tintin | - | 1.0.1 | MIDP-2.0 | - | 13 | 16 | 600 | 61736560 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/TheEscape.jar | 146 | TheEscape | - | 1.0 | MIDP-2.0 | m3g | 34 | 16 | 600 | 661343 | 0 | - | 运行正常 |  |
| vault/TheItalianJob.jar | 118 | The Italian Job | - | 1.0 | MIDP-1.0 | nokia-ui | 8 | 16 | 600 | 175562 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/TheMummyTheTombOfTheDragonEmperor_1.0.0_320x240.jar | 634 | The Mummy: TOTDE | - | 1.0.0 | MIDP-2.0 | - | 10 | 16 | 600 | 684389686 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/TheMummy_K700F.jar | 85 | The Mummy [terjef] | - | 1.1.9 | MIDP-1.0 | nokia-ui | 9 | 16 | 600 | 11752 | 0 | - | 运行正常 | self-paced title: runs 600 frames unpaced (t |
| vault/TheO[1].C.jar | 294 | The O.C. | - | 1.0.1 | MIDP-2.0 | nokia-ui | 4 | 16 | 600 | 4838305409 | 0 | javax/microedition/media/MediaEx | 运行正常 |  |
| vault/ThePaperMenace(by_wl).jar | 178 | ThePaperMenace | - | 1.0 | MIDP-2.0 | nokia-ui | 6 | 16 | 600 | 14772 | 0 | java/lang/NullPointerException,j | 运行正常 | runs without input; the generic key script m |
| vault/TheSettlers_1.1.1_320x240.jar | 538 | The settlers | - | 1.1.1 | MIDP-2.0 | - | 6 | 16 | 600 | 10482557 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/TheSettlers_en-176x220.jar | 277 | The Settlers | - | 1.1.1 | MIDP-2.0 | - | 6 | 16 | 600 | 3050243 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/TheSims3WinterEdition_0.2.12_320x240.jar | 719 | The Sims 3 Winter Edition | - | 0.2.12 | MIDP-2.0 | m3g | 37 | 16 | 600 | 9807689 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/TheSims3_7.6.60_320x240.jar | 523 | The Sims 3 | - | 7.6.60 | MIDP-2.0 | - | 27 | 16 | 600 | 17746438 | 0 | java/lang/ArrayIndexOutOfBoundsE | 运行正常 |  |
| vault/The_Black_Citadel.jar | 92 | BlackCitadel | - | 1.28 | MIDP-2.0 | - | 3 | 16 | 600 | 150900 | 0 | - | 运行正常 | runs without input; the generic key script d |
| vault/The_Chronicles_of_Riddick.jar | 160 | Riddick | - | 1.1.0 | MIDP-2.0 | - | 13 | 16 | 600 | 1642309 | 0 | - | 运行正常 |  |
| vault/The_Day_After_Tomorrow.jar | 150 | TDAT | - | 1.1.1 | MIDP-1.0 | - | 8 | 16 | 600 | 120659 | 0 | - | 运行正常 |  |
| vault/The_Infernal_Gate.jar | 192 | The Infernal Gate | - | 1.0.0 | MIDP-1.0 | nokia-ui | 16 | 16 | 600 | 3416013 | 1 | java/io/IOException,java/lang/Ar | 标题自身异常 | game code threw java.lang.ArrayIndexOutOfBou |
| vault/The_OC.jar | 285 | The OC | - | 1.0.5 | MIDP-2.0 | - | 4 | 16 | 600 | 3996714511 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/The_Pirate_by_BFLM.jar | 78 | ThePirate | - | 1.0 | MIDP-1.0 | - | 9 | 16 | 600 | 45286 | 0 | - | 运行正常 |  |
| vault/The_Simpsons_2.jar | 775 | The Simpsons 2 | - | 7.4.95 | MIDP-2.0 | - | 8 | 16 | 600 | 2489559085 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/The_Simpsons_Minutes_To_Meltdown_CZ.jar | 282 | The Simpsons: Minutes to Mel | - | 4.1.60 | MIDP-2.0 | - | 10 | 16 | 600 | 2463037 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/The_Sims_2_CZ.jar | 254 | The Sims 2 | - | 1.0.5 | MIDP-1.0 | nokia-ui | 18 | 16 | 600 | 3450321595 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/Thedayaftertomorrow_sek700.wz.cz.jar | 172 | TDAT | - | 1.1.4 | MIDP-1.0 | nokia-ui | 8 | 16 | 600 | 128802 | 0 | - | 运行正常 |  |
| vault/Thehulk.retail.drakhan_sek700.wz.cz.jar | 104 | TheHulk | - | 1.1.1 | MIDP-1.0 | nokia-ui | 16 | 16 | 600 | 935745 | 0 | - | 运行正常 |  |
| vault/Thor - The Dark World.jar | 1661 | Thor The Dark World | - | 1.0.1 | MIDP-2.0 | - | 12 | 16 | 600 | 460309655 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/Tom Clancy's Ghost Recon - Future Soldier.jar | 1493 | Ghost Recon Future Soldier | - | 1.3.1 | MIDP-2.0 | - | 18 | 16 | 600 | 53189946 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/Tom Clancy's Ghost Recon 2 - Advance Warfighter.jar | 375 | Ghost Recon 2 | - | 1.0.4 | MIDP-2.0 | - | 11 | 16 | 600 | 205698505 | 0 | java/lang/ClassNotFoundException | 运行正常 |  |
| vault/Tom Clancy's H.A.W.X..jar | 962 | H.A.W.X | - | 1.1.1 | MIDP-2.0 | - | 8 | 16 | 600 | 117520933 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/Tom Clancy's Rainbow Six - Lockdown.jar | 199 | RainbowSixLockdown | - | 1.1.1 | MIDP-2.0 | - | 6 | 16 | 600 | 3917070 | 0 | - | 运行正常 |  |
| vault/Tom Clancy's Rainbow Six 3.jar | 194 | Rainbow Six 3 | - | 1.0.1 | MIDP-2.0 | - | 8 | 16 | 600 | 2991624 | 0 | - | 运行正常 |  |
| vault/Tom Clancy's Splinter Cell - Chaos Theory.jar | 298 | SplinterCell CT | - | 1.0.1 | MIDP-2.0 | - | 7 | 16 | 600 | 168419511 | 0 | - | 运行正常 |  |
| vault/Tom Clancy's Splinter Cell - Conviction.jar | 958 | Splinter Cell Conviction | - | 1.1.4 | MIDP-2.0 | - | 11 | 16 | 600 | 42949037 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/Tom Clancy's Splinter Cell - Double Agent.jar | 330 | Splinter Cell DA | - | 1.0.2 | MIDP-2.0 | - | 6 | 16 | 600 | 273423531 | 0 | - | 运行正常 |  |
| vault/Tom Clancy's Splinter Cell - Pandora Tomorrow.jar | 223 | SC-PT | - | 1.0.2 | MIDP-2.0 | - | 6 | 16 | 600 | 1969170 | 0 | javax/microedition/rms/InvalidRe | 运行正常 |  |
| vault/Tom Clancys's Rainbow Six - Vegas (1).jar | 319 | Rainbow Six Vegas | - | 1.3.7 | MIDP-2.0 | - | 7 | 16 | 600 | 163176590 | 0 | - | 运行正常 |  |
| vault/TomClancysHAWX_1.0.2_320x240.jar | 319 | Tom Clancy's H.A.W.X | - | 1.0.2 | MIDP-2.0 | - | 8 | 16 | 600 | 5504149904 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/Tomb Raider - Anniversary.jar | 291 | TombRaiderAnniversary | - | 0.2.8 | MIDP-2.0 | - | 4 | 16 | 600 | 1154824 | 0 | java/lang/ArrayIndexOutOfBoundsE | 运行正常 |  |
| vault/Tomb Raider - Legend Tokyo.jar | 516 | Tomb Raider Legend | - | 1.15.41 | MIDP-2.0 | - | 21 | 16 | 600 | 1187578 | 0 | - | 运行正常 |  |
| vault/Tomb Raider - Underworld.jar | 1127 | TR Underworld | - | 1.7.0 | MIDP-1.0 | - | 7 | 16 | 600 | 12861635 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/Tomb Raider Anniversary v0.2.1 (128x128).jar | 63 | TombRaiderAnniversary | - | 0.2.1 | MIDP-1.0 | - | 4 | 16 | 600 | 2440886 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/Tomb Raider Anniversary v0.2.1 (CLDC-1.0, MIDP-2.0) (Motorola) (176x220).jar | 129 | TombRaiderAnniversary | - | 0.2.1 | MIDP-2.0 | - | 4 | 16 | 600 | 683415 | 0 | java/lang/NullPointerException | 运行正常 | painted 300 frames but made no draw calls (b |
| vault/Tomb Raider Anniversary v0.2.1 (CLDC-1.1, MIDP-2.0) (Nokia 6680) (176x208).jar | 177 | TombRaiderAnniversary | - | 0.2.1 | MIDP-2.0 | - | 4 | 16 | 600 | 2854819 | 0 | java/lang/ArrayIndexOutOfBoundsE | 运行正常 |  |
| vault/Tomb Raider Anniversary v0.2.1 (CLDC-1.1, MIDP-2.0) (Nokia E61) (320x240).jar | 296 | TombRaiderAnniversary | - | 0.2.1 | MIDP-2.0 | - | 4 | 16 | 600 | 1718001 | 0 | java/lang/ArrayIndexOutOfBoundsE | 运行正常 |  |
| vault/Tomb Raider Anniversary v0.2.1 (CLDC-1.1, MIDP-2.0) (Nokia N80) (352x416).jar | 324 | TombRaiderAnniversary | - | 0.2.1 | MIDP-2.0 | - | 4 | 16 | 600 | 3867297 | 0 | java/lang/ArrayIndexOutOfBoundsE | 运行正常 |  |
| vault/Tomb Raider Anniversary v0.2.1 (CLDC-1.1, MIDP-2.0) (SE) (128x160).jar | 134 | TombRaiderAnniversary | - | 0.2.1 | MIDP-2.0 | - | 4 | 16 | 600 | 1419112 | 0 | java/lang/ArrayIndexOutOfBoundsE | 运行正常 |  |
| vault/Tomb Raider Anniversary v0.2.2 (CLDC-1.1, MIDP-2.0) (SE) (128x128).jar | 138 | TombRaiderAnniversary | - | 0.2.2 | MIDP-2.0 | - | 4 | 16 | 600 | 1461465 | 0 | java/lang/ArrayIndexOutOfBoundsE | 运行正常 |  |
| vault/Tomb Raider Anniversary v0.2.2 (CLDC-1.1, MIDP-2.0) (SE) (176x220).jar | 169 | TombRaiderAnniversary | - | 0.2.2 | MIDP-2.0 | - | 4 | 16 | 600 | 1472088 | 0 | java/lang/ArrayIndexOutOfBoundsE | 运行正常 |  |
| vault/Tomb Raider Anniversary v0.2.2 (CLDC-1.1, MIDP-2.0) (SE) (240x320).jar | 275 | TombRaiderAnniversary | - | 0.2.2 | MIDP-2.0 | - | 4 | 16 | 600 | 1319421 | 0 | java/lang/ArrayIndexOutOfBoundsE | 运行正常 |  |
| vault/Tomb Raider Anniversary v0.2.3 (CLDC-1.0, MIDP-1.0) (Nokia s60v1 3650) (176x208).jar | 177 | TombRaiderAnniversary | - | 0.2.3 | MIDP-1.0 | - | 4 | 16 | 600 | 832006 | 0 | java/lang/ArrayIndexOutOfBoundsE | 运行正常 |  |
| vault/Tomb Raider Anniversary v0.2.3 (CLDC-1.0, MIDP-1.0) (Nokia) (176x208).jar | 201 | Tomb Raider Anniversary | - | 0.2.3 | MIDP-1.0 | - | 4 | 16 | 600 | 3703444 | 0 | java/lang/ArrayIndexOutOfBoundsE | 运行正常 |  |
| vault/Tomb Raider Anniversary v0.2.3 (CLDC-1.0, MIDP-2.0) (Nokia 6600) (176x208).jar | 187 | TombRaiderAnniversary | - | 0.2.3 | MIDP-2.0 | - | 4 | 16 | 600 | 2617626 | 0 | java/lang/ArrayIndexOutOfBoundsE | 运行正常 |  |
| vault/Tomb Raider Anniversary v0.2.4 (CLDC-1.1, MIDP-2.0) (Motorola) (240x320).jar | 185 | TombRaiderAnniversary | - | 0.2.4 | MIDP-2.0 | - | 4 | 16 | 600 | 2995262 | 0 | java/lang/ArrayIndexOutOfBoundsE | 运行正常 |  |
| vault/Tomb Raider Anniversary v0.2.4 (CLDC-1.1, MIDP-2.0) (Nokia 6280-6131) (240x320).jar | 272 | TombRaiderAnniversary | - | 0.2.4 | MIDP-2.0 | - | 4 | 16 | 600 | 1774641 | 0 | java/lang/ArrayIndexOutOfBoundsE | 运行正常 |  |
| vault/Tomb Raider Anniversary v0.2.5 (CLDC-1.0, MIDP-2.0) (Motorola) (128x128).jar | 134 | TombRaiderAnniversary | - | 0.2.5 | MIDP-2.0 | - | 4 | 16 | 600 | 1418345 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/Tomb Raider Anniversary v0.2.5 (CLDC-1.0, MIDP-2.0) (Motorola) (128x160).jar | 130 | TombRaiderAnniversary | - | 0.2.5 | MIDP-2.0 | - | 4 | 16 | 600 | 975559 | 0 | java/lang/IllegalArgumentExcepti | 运行正常 |  |
| vault/Tomb Raider Anniversary v0.2.5 (CLDC-1.1, MIDP-2.0) (N73) (240x320).jar | 272 | TombRaiderAnniversary | - | 0.2.5 | MIDP-2.0 | - | 4 | 16 | 600 | 1764319 | 0 | java/lang/ArrayIndexOutOfBoundsE | 运行正常 |  |
| vault/Tomb Raider Anniversary v0.2.6 (CLDC-1.1, MIDP-2.0) (Nokia s40v3) (208x208).jar | 168 | TombRaiderAnniversary | - | 0.2.6 | MIDP-2.0 | - | 4 | 16 | 600 | 1528364 | 0 | java/lang/ArrayIndexOutOfBoundsE | 运行正常 |  |
| vault/Tomb Raider Anniversary v0.2.6 (s40v3) (208x208).jar | 168 | TombRaiderAnniversary | - | 0.2.6 | MIDP-2.0 | - | 4 | 16 | 600 | 1527824 | 0 | java/lang/ArrayIndexOutOfBoundsE | 运行正常 |  |
| vault/Tomb Raider Anniversary v0.2.8 (CLDC-1.1, MIDP-2.0) (N95) (240x320).jar | 291 | TombRaiderAnniversary | - | 0.2.8 | MIDP-2.0 | - | 4 | 16 | 600 | 1722896 | 0 | java/lang/ArrayIndexOutOfBoundsE | 运行正常 |  |
| vault/Tomb Raider Anniversary v0.3.1 (CLDC-1.1, MIDP-2.0) (Nokia N91) (176x208).jar | 169 | TombRaiderAnniversary | - | 0.3.1 | MIDP-2.0 | - | 4 | 16 | 600 | 1427177 | 0 | java/lang/ArrayIndexOutOfBoundsE | 运行正常 |  |
| vault/Tomb Raider Anniversary v0.3.2 (CLDC-1.0, MIDP-2.0) (s40v2) (128x128).jar | 87 | TombRaiderAnniversary | - | 0.3.2 | MIDP-2.0 | - | 4 | 16 | 600 | 933859 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/Tomb Raider Anniversary v1.0.0 (CLDC-1.0, MIDP-2.0) (s40v2a) (128x160).jar | 111 | TombRaiderAnniversary | - | 1.0.0 | MIDP-2.0 | - | 4 | 16 | 600 | 2948538924 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/Tomb Raider Anniversary v1.0.0 (CLDC-1.1, MIDP-2.0) (Nokia 6280) (240x320).jar | 271 | TombRaiderAnniversary | - | 1.0.0 | MIDP-2.0 | - | 4 | 16 | 600 | 1316601 | 0 | java/lang/ArrayIndexOutOfBoundsE | 运行正常 |  |
| vault/Tomb Raider Elixir Of Life v1.0.3 (176x208) [English].jar | 131 | TombRaider3 | - | 1.0.3 | MIDP-1.0 | - | 5 | 16 | 601 | 1147440 | 0 | - | 运行正常 |  |
| vault/Tomb Raider Elixir Of Life v1.0.4 (128x128) [English].jar | 63 | TombRaider3 | - | 1.0.4 | MIDP-1.0 | - | 6 | 16 | 600 | 1807705 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/Tomb Raider Elixir Of Life v1.0.6 (176x208) [German].jar | 91 | TombRaider3 | - | 1.0.6 | MIDP-1.0 | - | 5 | 16 | 600 | 1444248 | 0 | - | 运行正常 |  |
| vault/Tomb Raider Elixir Of Life v1.0.8 (128x128) [German].jar | 63 | Tomb Raider Elixir Of Life | - | 1.0.8 | MIDP-1.0 | - | 6 | 16 | 600 | 1276812 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/Tomb Raider Elixir Of Life v1.1.4 (128x128) [Spanish].jar | 63 | TombRaider3 | - | 1.1.4 | MIDP-1.0 | - | 6 | 16 | 600 | 1570969 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/Tomb Raider Elixir Of Life v1.1.5 (176x208) [Italian].jar | 91 | TombRaider3 | - | 1.1.5 | MIDP-1.0 | - | 5 | 16 | 600 | 1454648 | 0 | - | 运行正常 |  |
| vault/Tomb Raider Elixir Of Life v1.1.5 (176x208) [Spanish].jar | 91 | TombRaider3 | - | 1.1.5 | MIDP-1.0 | - | 5 | 16 | 600 | 1506351 | 0 | - | 运行正常 |  |
| vault/Tomb Raider Elixir Of Life v1.1.9 (176x208) [Chinese].jar | 90 | TombRaider3 | - | 1.1.9 | MIDP-1.0 | - | 5 | 16 | 601 | 964066 | 0 | - | 运行正常 |  |
| vault/Tomb Raider Elixir Of Life v1.2.1 (128x128) [Chinese A].jar | 62 | 掌星-古墓丽影3A | - | 1.2.1 | MIDP-1.0 | - | 8 | 16 | 600 | 1171832 | 0 | javax/microedition/rms/InvalidRe | 运行正常 |  |
| vault/Tomb Raider Elixir Of Life v1.2.1 (128x128) [Chinese B].jar | 62 | 掌星-古墓丽影3B | - | 1.2.1 | MIDP-1.0 | - | 8 | 16 | 600 | 1625873 | 0 | javax/microedition/rms/InvalidRe | 运行正常 |  |
| vault/Tomb Raider Legend Tokyo v1.12.7 (CLDC-1.1, MIDP-2.0) (for JSR184 - 240x320 screens) [English].jar | 516 | Tomb Raider Legend | - | 1.12.7 | MIDP-2.0 | - | 21 | 16 | 600 | 10076828 | 0 | java/lang/IllegalArgumentExcepti | 运行正常 |  |
| vault/Tomb Raider Legend Tokyo v1.15.41 (CLDC-1.1, MIDP-2.0) (Nokia 6280-6131) (240x320) [English].jar | 515 | Tomb Raider Legend | - | 1.15.41 | MIDP-2.0 | - | 21 | 16 | 600 | 10163384 | 0 | - | 运行正常 |  |
| vault/Tomb Raider Legend Tokyo v1.16 (176x220) [English].jar | 401 | Tomb Raider Legend | - | 1.16 | MIDP-2.0 | - | 22 | 16 | 600 | 10428722 | 0 | java/lang/IllegalArgumentExcepti | 运行正常 |  |
| vault/Tomb Raider Legend Tokyo v1.17 (CLDC-1.1, MIDP-2.0) (Nokia N73) (240x320) [Italian].jar | 468 | Tomb Raider Legend | - | 1.17 | MIDP-2.0 | - | 22 | 16 | 600 | 11237451 | 0 | java/lang/IllegalArgumentExcepti | 运行正常 |  |
| vault/Tomb Raider Legend Tokyo v1.17.6 (CLDC-1.1, MIDP-2.0) (NOGUNSOUND - Nokia 6280) [Spanish].jar | 517 | Tomb Raider Legend | - | 1.17.6 | MIDP-2.0 | - | 21 | 16 | 600 | 11082767 | 0 | - | 运行正常 |  |
| vault/Tomb Raider Legend Tokyo v1.17.9 (CLDC-1.1, MIDP-2.0) (Nokia 6233) [Spanish].jar | 517 | Tomb Raider Legend | - | 1.17.9 | MIDP-2.0 | - | 21 | 16 | 600 | 11129276 | 0 | - | 运行正常 |  |
| vault/Tomb Raider Puzzle Paradox v0.4.1 (176x220).jar | 213 | Tomb Raider Puzzle Paradox | - | 0.4.1 | MIDP-2.0 | - | 2 | 16 | 600 | 6306436 | 0 | java/io/IOException,javax/microe | 运行正常 |  |
| vault/Tomb Raider Puzzle Paradox v0.4.1 (CLDC-1.1, MIDP-2.0) (Sony Ericsson) 176x220).jar | 213 | Tomb Raider Puzzle Paradox | - | 0.4.1 | MIDP-2.0 | - | 2 | 16 | 600 | 6279283 | 0 | java/io/IOException,javax/microe | 运行正常 |  |
| vault/Tomb Raider Puzzle Paradox v0.4.6 (CLDC-1.1, MIDP-2.0) (s40v3a) (240x320).jar | 315 | Tomb Raider Puzzle Paradox | - | 0.4.6 | MIDP-2.0 | - | 2 | 16 | 600 | 3783849 | 0 | java/io/IOException,javax/microe | 运行正常 |  |
| vault/Tomb Raider Puzzle Paradox v0.4.8 (CLDC-1.0, MIDP-2.0) (Nokia s60v2) (176x208).jar | 208 | Tomb Raider Puzzle Paradox | - | 0.4.8 | MIDP-2.0 | - | 2 | 16 | 600 | 5211128 | 0 | java/io/IOException,javax/microe | 运行正常 |  |
| vault/Tomb Raider Puzzle Paradox v0.6.7 (CLDC-1.0, MIDP-2.0) (Motorola) (176x220).jar | 139 | Tomb Raider Puzzle Paradox | - | 0.6.7 | MIDP-2.0 | - | 2 | 16 | 600 | 13171089 | 0 | java/io/IOException,javax/microe | 运行正常 |  |
| vault/Tomb Raider Puzzle Paradox v0.6.8 (CLDC-1.1, MIDP-2.0) (Nokia N73) (240x320).jar | 303 | Tomb Raider Puzzle Paradox | - | 0.6.8 | MIDP-2.0 | - | 2 | 16 | 600 | 8044277 | 0 | java/io/IOException,javax/microe | 运行正常 |  |
| vault/Tomb Raider Puzzle Paradox v0.7.0 (132x176).jar | 152 | Tomb Raider Puzzle Paradox | - | 0.7.0 | MIDP-2.0 | - | 2 | 16 | 600 | 7213240 | 0 | java/io/IOException,javax/microe | 运行正常 |  |
| vault/Tomb Raider Puzzle Paradox v0.7.3 (CLDC-1.1, MIDP-2.0) (Nokia s60v3) (352x416).jar | 330 | Tomb Raider Puzzle Paradox | - | 0.7.3 | MIDP-2.0 | - | 2 | 16 | 600 | 4131656 | 0 | java/io/IOException,java/lang/Nu | 运行正常 |  |
| vault/Tomb Raider Puzzle Paradox v0.7.6 (CLDC-1.0, MIDP-1.0) (N-Gage) (176x208).jar | 204 | Tomb Raider Puzzle Paradox | - | 0.7.6 | MIDP-1.0 | - | 2 | 16 | 600 | 2126508 | 0 | java/io/IOException,javax/microe | 运行正常 |  |
| vault/Tomb Raider Quest for Cinnabar v1.1.0 (176x208) [German].jar | 140 | TombRaider2 | - | 1.1.0 | MIDP-1.0 | - | 5 | 16 | 601 | 1291922 | 0 | - | 运行正常 |  |
| vault/Tomb Raider Quest for Cinnabar v1.1.1 (176x208) [English].jar | 137 | TombRaider2 | - | 1.1.1 | MIDP-1.0 | - | 5 | 16 | 601 | 1115450 | 0 | - | 运行正常 |  |
| vault/Tomb Raider Quest for Cinnabar v1.1.2 (176x208) [English].jar | 138 | TombRaider2 | - | 1.1.2 | MIDP-1.0 | - | 5 | 16 | 601 | 1130256 | 0 | - | 运行正常 |  |
| vault/Tomb Raider Quest for Cinnabar v1.1.3 (128x128) [Spanish].jar | 63 | TombRaider2 | - | 1.1.3 | MIDP-1.0 | - | 6 | 16 | 600 | 2037137 | 0 | - | 运行正常 |  |
| vault/Tomb Raider Quest for Cinnabar v1.1.5 (128x128) [English].jar | 62 | TombRaider2 | - | 1.1.5 | MIDP-1.0 | - | 6 | 16 | 600 | 1270883 | 0 | - | 运行正常 |  |
| vault/Tomb Raider Quest for Cinnabar v1.1.5 (128x128) [German].jar | 63 | Tomb Raider Quest For Cinnab | - | 1.1.5 | MIDP-1.0 | - | 6 | 16 | 600 | 1268488 | 0 | - | 运行正常 |  |
| vault/Tomb Raider Quest for Cinnabar v1.1.5 (176x208) [Spanish].jar | 97 | TombRaider2 | - | 1.1.5 | MIDP-1.0 | - | 5 | 16 | 600 | 1455041 | 0 | - | 运行正常 |  |
| vault/Tomb Raider Slot v01.01.03 (CLDC-1.0, MIDP-2.0) (128x128).jar | 107 | SL Tomb Raider | - | 01.01.03 | MIDP-2.0 | - | 52 | 16 | 601 | 1758846 | 0 | java/io/IOException | 运行正常 |  |
| vault/Tomb Raider Slot v01.01.03 (CLDC-1.0, MIDP-2.0) (176x208).jar | 155 | SL Tomb Raider | - | 01.01.03 | MIDP-2.0 | - | 52 | 16 | 601 | 1823087 | 0 | java/io/IOException | 运行正常 |  |
| vault/Tomb Raider Slot v01.01.03 (CLDC-1.0, MIDP-2.0) (176x220).jar | 158 | SL Tomb Raider | - | 01.01.03 | MIDP-2.0 | - | 52 | 16 | 601 | 1807751 | 0 | java/io/IOException | 运行正常 |  |
| vault/Tomb Raider Slot v01.01.03 (CLDC-1.0, MIDP-2.0) (240x240).jar | 181 | SL Tomb Raider | - | 01.01.03 | MIDP-2.0 | - | 52 | 16 | 601 | 1811012 | 0 | java/io/IOException | 运行正常 |  |
| vault/Tomb Raider Slot v01.01.03 (CLDC-1.0, MIDP-2.0) (320x240).jar | 167 | SL Tomb Raider | - | 01.01.03 | MIDP-2.0 | - | 52 | 16 | 601 | 1828485 | 0 | java/io/IOException | 运行正常 |  |
| vault/Tomb Raider Slot v01.01.03 (CLDC-1.0, MIDP-2.0) (360x640).jar | 241 | SL Tomb Raider | - | 01.01.03 | MIDP-2.0 | - | 52 | 16 | 601 | 1424435 | 0 | java/io/IOException | 运行正常 |  |
| vault/Tomb Raider Slot v01.01.03 (Sony Ericsson) (240x320).jar | 188 | SL Tomb Raider | - | 01.01.03 | MIDP-2.0 | - | 52 | 16 | 601 | 1833038 | 0 | java/io/IOException | 运行正常 |  |
| vault/Tomb Raider Slot v30.01 (CLDC-1.0, MIDP-2.0) (128x128).jar | 138 | Tomb Raider Slot | - | 30.01 | MIDP-2.0 | - | 46 | 16 | 600 | 866121 | 0 | java/io/IOException | 运行正常 |  |
| vault/Tomb Raider Slot v30.01 (CLDC-1.0, MIDP-2.0) (176x220).jar | 219 | Tomb Raider Slot | - | 30.01 | MIDP-2.0 | - | 44 | 16 | 600 | 892567 | 0 | java/io/IOException | 运行正常 |  |
| vault/Tomb Raider Slot v30.01 (CLDC-1.0, MIDP-2.0) (Nokia 128x160).jar | 144 | Tomb Raider Slot | - | 30.01 | MIDP-2.0 | - | 46 | 16 | 600 | 884521 | 0 | - | 运行正常 |  |
| vault/Tomb Raider Slot v30.01 (CLDC-1.0, MIDP-2.0) (Nokia 176x208).jar | 178 | Tomb Raider Slot | - | 30.01 | MIDP-2.0 | - | 46 | 16 | 600 | 878300 | 0 | - | 运行正常 |  |
| vault/Tomb Raider Slot v30.01 (CLDC-1.0, MIDP-2.0) (Samsung 240x320).jar | 225 | Tomb Raider Slot | - | 30.01 | MIDP-2.0 | - | 46 | 16 | 600 | 909261 | 0 | - | 运行正常 |  |
| vault/Tomb Raider Slot v30.02 (CLDC-1.0, MIDP-2.0) (Nokia s40v3a) (240x320).jar | 249 | Tomb Raider Slot | - | 30.02 | MIDP-2.0 | - | 45 | 16 | 600 | 911892 | 0 | - | 运行正常 |  |
| vault/Tomb Raider Slot v30.02 (CLDC-1.0, MIDP-2.0) (s40v2) (128x128).jar | 138 | Tomb Raider Slot | - | 30.02 | MIDP-2.0 | - | 45 | 16 | 600 | 872152 | 0 | java/io/IOException | 运行正常 |  |
| vault/Tomb Raider Slot v30.02 (CLDC-1.0, MIDP-2.0) (s40v2a) (128x160).jar | 144 | Tomb Raider Slot | - | 30.02 | MIDP-2.0 | - | 45 | 16 | 600 | 887300 | 0 | - | 运行正常 |  |
| vault/Tomb Raider Slot v30.02 (CLDC-1.0, MIDP-2.0) (s40v3) (208x208).jar | 192 | Tomb Raider Slot | - | 30.02 | MIDP-2.0 | - | 45 | 16 | 600 | 897651 | 0 | - | 运行正常 |  |
| vault/Tomb Raider Slot v30.02 (CLDC-1.1, MIDP-2.0) (s40v3a) (240x320).jar | 249 | Tomb Raider Slot | - | 30.02 | MIDP-2.0 | - | 45 | 16 | 600 | 912205 | 0 | - | 运行正常 |  |
| vault/Tomb Raider The Osiris Codex v1.0.9 (176x208) [English].jar | 134 | TombRaider1 | - | 1.0.9 | MIDP-1.0 | - | 5 | 16 | 600 | 1123917 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/Tomb Raider The Osiris Codex v1.1.3 (128x128) [German].jar | 63 | Tomb Raider The Osiris Codex | - | 1.1.3 | MIDP-1.0 | - | 6 | 16 | 600 | 1798693 | 0 | - | 运行正常 |  |
| vault/Tomb Raider The Osiris Codex v1.1.3 (176x208) [German].jar | 135 | TombRaider1 | - | 1.1.3 | MIDP-1.0 | - | 5 | 16 | 601 | 1354756 | 0 | - | 运行正常 |  |
| vault/Tomb Raider The Osiris Codex v1.1.4 (176x208) [Italian].jar | 135 | TombRaider1 | - | 1.1.4 | MIDP-1.0 | - | 5 | 16 | 600 | 1408890 | 0 | - | 运行正常 |  |
| vault/Tomb Raider The Osiris Codex v1.1.8 (176x208) [Spanish].jar | 93 | TombRaider1 | - | 1.1.8 | MIDP-1.0 | - | 5 | 16 | 600 | 1460597 | 0 | - | 运行正常 |  |
| vault/Tomb Raider The Osiris Codex v1.2.5 (128x128) [Chinese A].jar | 58 | 地狱书上 | - | 1.2.5 | MIDP-1.0 | - | 6 | 16 | 600 | 1112837 | 0 | - | 运行正常 |  |
| vault/Tomb Raider The Osiris Codex v1.2.5 (128x128) [Chinese B].jar | 58 | 地狱书下 | - | 1.2.5 | MIDP-1.0 | - | 6 | 16 | 600 | 1975975 | 0 | - | 运行正常 |  |
| vault/Tomb Raider The Osiris Codex v1.5.6 (240x320) [English].jar | 118 | TombRaider1 | - | 1.5.6 | MIDP-1.0 | - | 6 | 16 | 601 | 974722 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/Tomb Raider Underworld 2D v1.3.8 (CLDC-1.0, MIDP-1.0) (s40v2) (128x128).jar | 126 | TR Underworld | - | 1.3.8 | MIDP-1.0 | - | 3 | 16 | 600 | 830844 | 0 | java/io/IOException,java/lang/Ar | 运行正常 |  |
| vault/Tomb Raider Underworld 2D v1.3.8 (CLDC-1.0, MIDP-1.0) (s60v2-N6600) (176x208).jar | 511 | TR Underworld | - | 1.3.8 | MIDP-1.0 | - | 6 | 16 | 600 | 2033351 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/Tomb Raider Underworld 2D v1.4.0 (CLDC-1.0, MIDP-1.0) (Nokia 3250) (176x208).jar | 511 | TR Underworld | - | 1.4.0 | MIDP-1.0 | - | 6 | 16 | 600 | 3082400 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/Tomb Raider Underworld 2D v1.4.0 (CLDC-1.0, MIDP-1.0) (Samsung SGH-E250i) (128x160).jar | 263 | TR Underworld | - | 1.4.0 | MIDP-1.0 | - | 4 | 16 | 600 | 556412 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/Tomb Raider Underworld 2D v1.4.0 (CLDC-1.0, MIDP-1.0) (Samsung SGH-F480) (240x320).jar | 571 | TR Underworld | - | 1.4.0 | MIDP-1.0 | - | 6 | 16 | 600 | 2077999 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/Tomb Raider Underworld 2D v1.4.0 (CLDC-1.0, MIDP-1.0) (Sony Ericsson W810) (176x220).jar | 524 | TR Underworld | - | 1.4.0 | MIDP-1.0 | - | 6 | 16 | 600 | 3450304 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/Tomb Raider Underworld 2D v1.4.0 (CLDC-1.0, MIDP-1.0) (s60v2-N6680) (176x208).jar | 488 | TR Underworld | - | 1.4.0 | MIDP-1.0 | - | 6 | 16 | 600 | 2045313 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/Tomb Raider Underworld 2D v1.4.1 (Benqsie EL71) (240x320).jar | 544 | TR Underworld | - | 1.4.1 | MIDP-1.0 | nokia-ui | 15 | 16 | 600 | 378650 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/Tomb Raider Underworld 2D v1.4.1 (CLDC-1.0, MIDP-1.0) (Nokia N95) (240x320).jar | 539 | TR Underworld | - | 1.4.1 | MIDP-1.0 | - | 7 | 16 | 600 | 3357193 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/Tomb Raider Underworld 2D v1.4.1 (CLDC-1.0, MIDP-1.0) (s40v3a) (240x320).jar | 539 | TR Underworld | - | 1.4.1 | MIDP-1.0 | - | 7 | 16 | 600 | 2710677 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/Tomb Raider Underworld 2D v1.4.1 (CLDC-1.0, MIDP-1.0) (s40v5) (240x320).jar | 539 | TR Underworld | - | 1.4.1 | MIDP-1.0 | - | 7 | 16 | 600 | 3091851 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/Tomb Raider Underworld 2D v1.4.2 (CLDC-1.0, MIDP-1.0) (s40v3) (208x208).jar | 293 | TR Underworld | - | 1.4.2 | MIDP-1.0 | - | 7 | 16 | 600 | 963495 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/Tomb Raider Underworld 2D v1.4.5 (CLDC-1.0, MIDP-1.0) (s40v2a-N5200) (128x160).jar | 238 | TR Underworld | - | 1.4.5 | MIDP-1.0 | - | 3 | 16 | 600 | 929260 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/Tomb Raider Underworld 2D v1.4.5 (CLDC-1.0, MIDP-1.0) (s40v2a-N6101) (128x160).jar | 124 | TR Underworld | - | 1.4.5 | MIDP-1.0 | - | 3 | 16 | 600 | 402688 | 0 | java/io/IOException,java/lang/Ar | 运行正常 |  |
| vault/Tomb Raider Underworld 2D v1.4.5 (CLDC-1.0, MIDP-1.0) (s40v2a-N6151) (128x160).jar | 238 | TR Underworld | - | 1.4.5 | MIDP-1.0 | - | 3 | 16 | 600 | 865857 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/Tomb Raider Underworld 2D v1.4.6 (CLDC-1.0, MIDP-1.0) (Sony Ericsson K510) (128x160).jar | 264 | TR Underworld | - | 1.4.6 | MIDP-1.0 | - | 4 | 16 | 600 | 556959 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/Tomb Raider Underworld 3D v1.7.0 (CLDC-1.0, MIDP-1.0) (Nokia N92) (240x320).jar | 1131 | TR Underworld | - | 1.7.0 | MIDP-1.0 | - | 7 | 16 | 600 | 27283066 | 0 | java/lang/ArrayIndexOutOfBoundsE | 运行正常 |  |
| vault/Tomb Raider Underworld 3D v1.7.0 (CLDC-1.0, MIDP-1.0) (Nokia N95) (240x320).jar | 1127 | TR Underworld | - | 1.7.0 | MIDP-1.0 | - | 7 | 16 | 600 | 17102865 | 0 | java/lang/ArrayIndexOutOfBoundsE | 运行正常 |  |
| vault/Tomb Raider Underworld 3D v1.7.1 (CLDC-1.0, MIDP-1.0) (Nokia N73) (240x320).jar | 1081 | TR Underworld | - | 1.7.1 | MIDP-1.0 | - | 7 | 16 | 600 | 53835350 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/Tomb Raider Underworld 3D v1.7.1 (CLDC-1.0, MIDP-1.0) (Nokia N80) (352x416).jar | 1139 | TR Underworld | - | 1.7.1 | MIDP-1.0 | - | 7 | 16 | 600 | 17060611 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/Tomb Raider Underworld 3D v1.7.1 (CLDC-1.0, MIDP-1.0) (Nokia N96) (240x320).jar | 1081 | TR Underworld | - | 1.7.1 | MIDP-1.0 | - | 7 | 16 | 600 | 53835000 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/Tomb Raider Underworld 3D v1.7.1 (CLDC-1.0, MIDP-1.0) (s60v2-N70) (176x208).jar | 1007 | TR Underworld | - | 1.7.1 | MIDP-1.0 | - | 7 | 16 | 600 | 17227866 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/Tomb Raider Underworld 3D v1.7.2 (176x208).jar | 1069 | TR Underworld | - | 1.7.2 | MIDP-1.0 | - | 8 | 16 | 600 | 13690263 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/Tomb Raider Underworld 3D v1.7.2 (320x240).jar | 1073 | TR Underworld | - | 1.7.2 | MIDP-1.0 | - | 7 | 16 | 600 | 53983548 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/Tomb Raider Underworld 3D v1.7.3 (CLDC-1.0, MIDP-1.0) (Sony Ericsson K810) (240x320).jar | 1145 | TR Underworld | - | 1.7.3 | MIDP-1.0 | - | 7 | 16 | 600 | 13716980 | 0 | java/lang/ArrayIndexOutOfBoundsE | 运行正常 |  |
| vault/Tomb Raider Underworld 3D v1.8.8 (CLDC-1.0, MIDP-1.0) (Nokia 5800) (360x640).jar | 1304 | TR Underworld | - | 1.8.8 | MIDP-1.0 | - | 7 | 16 | 600 | 17813757 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/Tony Hawk - Vert.jar | 458 | Tony Hawk VERT | - | 1.0.0 | MIDP-2.0 | - | 49 | 16 | 600 | 5812065 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/TonyH.jar | 252 | Tony Hawk's American Wastela | - | 1.0.83 | MIDP-2.0 | nokia-ui | 22 | 16 | 600 | 8008933 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/Tony_Hawks-American_Wasteland.jar | 225 | Tony Hawk's American Wastela | - | 1.2.67 | MIDP-2.0 | - | 22 | 16 | 600 | 6657594 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/Tony_Hawks_Underground_IT_by_BFLM.jar | 135 | Tony Hawk Underground | - | 1.68.0 | MIDP-1.0 | nokia-ui | 18 | 16 | 600 | 1559639 | 0 | - | 运行正常 |  |
| vault/Top Gear - The Mobile Game.jar | 604 | Top Gear: the mobile game | - | 1.1.0 | MIDP-2.0 | - | 17 | 16 | 600 | 77632713 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/TopGun2_700.jar | 170 | TopGun 2 [terjef] | - | 1.0.3 | MIDP-2.0 | - | 22 | 16 | 600 | 2406845192 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/TopGuy_by_BFLM.jar | 120 | Top_Guy | - | 1.0 | MIDP-1.0 | nokia-ui | 17 | 16 | 600 | 4311310 | 0 | - | 运行正常 |  |
| vault/TopSpinTennis.jar | 98 | Top Spin | - | 1.0.8 | MIDP-1.0 | - | 10 | 16 | 600 | 4562799854 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/Tornado Mania 3D.jar | 455 | 3D Tornado Mania | - | 1.0.0 | MIDP-2.0 | - | 24 | 16 | 600 | 12093672 | 0 | java/io/EOFException,java/lang/N | 运行正常 |  |
| vault/Tornado Mania.jar | 339 | Tornado Mania! | - | 1.1.0 | MIDP-2.0 | - | 10 | 16 | 600 | 13843036 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/Toro_sek700.ic.cz.jar | 68 | Toro | - | 1.5.0 | MIDP-1.0 | nokia-ui | 5 | 16 | 600 | 381260068 | 0 | - | 运行正常 |  |
| vault/Torturechambers60_sek700.wz.cz.jar | 54 | TortureChamber | - | 1.0 | MIDP-1.0 | nokia-ui | 5 | 16 | 600 | 55883 | 0 | - | 运行正常 |  |
| vault/TotalConquest_1.0.1_320x240.jar | 919 | Total Conquest by Ankit | - | 1.0.1 | MIDP-2.0 | - | 25 | 16 | 600 | 93721309 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/Totally Spies.jar | 306 | TotallySpies! | - | 1.0.0 | MIDP-2.0 | - | 8 | 16 | 600 | 111300189 | 0 | - | 运行正常 |  |
| vault/TotallySpies S60.jar | 193 | TotallySpies! | - | 1.1.4 | MIDP-1.0 | nokia-ui | 5 | 16 | 600 | 2707698846 | 0 | java/io/EOFException,java/lang/A | 运行正常 |  |
| vault/Tour de France 2010_320x240.jar | 297 | Tour De France 2010 | - | 1.0 | MIDP-2.0 | nokia-ui | 13 | 16 | 600 | 3751661 | 0 | - | 运行正常 |  |
| vault/Tournament BlackJack_320x240.jar | 323 | Tournament BJ | - | 1.0.17 | MIDP-2.0 | - | 49 | 16 | 600 | 28264239 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/Tower Bloxx - My City.jar | 927 | TBloxx My City | - | 1.0.4 | MIDP-2.0 | - | 39 | 16 | 600 | 17704855 | 0 | java/io/EOFException,javax/micro | 运行正常 |  |
| vault/Tower Bloxx - New York.jar | 1088 | Tower Bloxx NY | - | 1.0.0 | MIDP-2.0 | - | 56 | 16 | 600 | 5546675 | 0 | java/io/EOFException,javax/micro | 运行正常 |  |
| vault/Tower Bloxx - Revolution.jar | 779 | TowerBloxxRev | - | 1.0.2 | MIDP-2.0 | - | 52 | 16 | 600 | 5325095 | 0 | java/io/EOFException,javax/micro | 运行正常 |  |
| vault/Tower Bloxx Delux-176x220.jar | 301 | Tower Bloxx Deluxe -  bought | - | 1.0.4 | MIDP-2.0 | m3g,mascot | 20 | 16 | 600 | 8914170 | 0 | java/io/EOFException,javax/micro | 运行正常 |  |
| vault/Tower Bloxx Deluxe.jar | 426 | Tower Bloxx Deluxe | - | 1.0.2 | MIDP-2.0 | - | 20 | 16 | 600 | 12265510 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/Tower Bloxx New York_320x240_Nokia_E71.jar | 995 | Tower Bloxx NY | - | 1.0.4 | MIDP-2.0 | nokia-ui | 56 | 16 | 600 | 23503443 | 0 | java/io/EOFException,javax/micro | 运行正常 |  |
| vault/Tower Bloxx.jar | 165 | Tower Bloxx(TM) | - | 1.2.6 | MIDP-1.0 | - | 16 | 16 | 600 | 12653534 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/Tower Mania.jar | 247 | Tower Mania | - | 1.0.1 | MIDP-2.0 | - | 22 | 16 | 600 | 13025299 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/TowerDefense_1.0_320x240.jar | 204 | Tower defense | - | 1.0 | MIDP-2.0 | - | 20 | 16 | 600 | 17093781 | 0 | - | 运行正常 |  |
| vault/Townsmen 3.jar | 207 | Townsmen3 **MASYAKA** | - | 1.0 | MIDP-2.0 | - | 5 | 16 | 600 | 480218 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/Townsmen 5 CZ ALL RESOLUTIONS.jar | 240 | Townsmen5 by piratesxxl.110m | - | 1.5 | MIDP-1.0 | nokia-ui | 7 | 16 | 600 | 2912803 | 0 | java/lang/ArrayIndexOutOfBoundsE | 运行正常 |  |
| vault/Townsmen 6 Revolution_320x240.jar | 630 | Townsmen 6 xsmart.ru | - | 1.0.0 | MIDP-2.0 | - | 24 | 16 | 601 | 3804324 | 0 | java/io/IOException,java/lang/Nu | 运行正常 |  |
| vault/Townsmen-cz-by-macamobil.jar | 240 | Townsmen5 | - | 1.5 | MIDP-1.0 | nokia-ui | 7 | 16 | 600 | 2930548 | 0 | java/lang/ArrayIndexOutOfBoundsE | 运行正常 |  |
| vault/Townsmen2.jar | 173 | Townsmen2Gold | - | 1.0 | MIDP-1.0 | nokia-ui | 5 | 16 | 600 | 587020 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/Townsmen_2_Gold_vXXX.jar | 86 | Townsmen2Gold | - | 1.0 | MIDP-1.0 | - | 6 | 16 | 600 | 342920 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/Townsmen_by_BFLM.jar | 86 | Townsmen | - | 1.11 | MIDP-1.0 | nokia-ui | 6 | 16 | 600 | 263954 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/Toxic Racer 3D.jar | 187 | ToxicRacer **MASYAKA** | - | 1.4.0 | MIDP-2.0 | m3g | 8 | 16 | 600 | 952125 | 0 | - | 运行正常 | runs without input; the generic key script d |
| vault/Track_And_Field.jar | 137 | Track and Field 2006 | - | 1.0.0 | MIDP-2.0 | - | 2 | 16 | 600 | 9889600925 | 0 | - | 运行正常 |  |
| vault/Trafficjamv10-_sek700.wz.cz.jar | 54 | Traffic Jam | - | 1.0 | MIDP-1.0 | nokia-ui | 6 | 16 | 600 | 788186 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/Train-Tycoon-2007-CZ[1].jar | 134 | Train Tycoon 2007 | - | 1.0 | MIDP-1.0 | - | 3 | 16 | 600 | 5671765 | 0 | - | 运行正常 |  |
| vault/Transformers_176x220.jar | 569 | Transformers | - | 1.0.4 | MIDP-2.0 | m3g | 8 | 16 | 600 | 31529805 | 0 | java/lang/IllegalArgumentExcepti | 运行正常 | runs without input; the generic key script t |
| vault/Treasure Grab.jar | 443 | Treasure Grab | - | 1.0.44 | MIDP-1.0 | - | 10 | 16 | 600 | 196792598 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/Trickotreatv100-_sek700.wz.cz.jar | 90 | TrT7650c | - | 1.0.0 | MIDP-1.0 | nokia-ui | 8 | 16 | 600 | 24717 | 0 | - | 运行正常 |  |
| vault/Triple Pop.jar | 36 | Triple Pop | - | 1.01 | MIDP-1.0 | nokia-ui | 8 | 16 | 600 | 1793445 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/TrivialPursuit_sek700.ic.cz.jar | 80 | Trivial Pursuit | - | 1.0.3 | MIDP-1.0 | nokia-ui | 3 | 16 | 600 | 652376654 | 0 | java/lang/ArrayIndexOutOfBoundsE | 运行正常 |  |
| vault/Troia_v.1.0.0.jar | 134 | Troia | - | 1.0 | MIDP-1.0 | nokia-ui | 11 | 16 | 600 | 3304047 | 0 | - | 运行正常 |  |
| vault/Tropical Madness.jar | 309 | Tropical Madness | - | 1.1.5 | MIDP-2.0 | - | 6 | 16 | 600 | 38446869 | 0 | - | 运行正常 |  |
| vault/Turtle Dash.jar | 825 | Turtle Dash | - | 1.0.0 | MIDP-2.0 | - | 34 | 16 | 600 | 3021039 | 0 | java/io/EOFException,javax/micro | 运行正常 |  |
| vault/Turtle Dash_320x240.jar | 729 | Turtle Dash | - | 1.0.0 | MIDP-2.0 | nokia-ui | 34 | 16 | 600 | 59444848 | 0 | java/io/EOFException,javax/micro | 运行正常 |  |
| vault/Ultimate Brick Breaker 2_320x240.jar | 341 | Ultimate Brick Breaker 2 | - | 1.1.1 | MIDP-2.0 | - | 27 | 16 | 600 | 74159198 | 0 | java/lang/ClassCastException,jav | 运行正常 |  |
| vault/Ultimate Street Football.jar | 462 | Street Football | - | 1.0.0 | MIDP-2.0 | - | 8 | 16 | 600 | 450672577 | 0 | - | 运行正常 |  |
| vault/Ultra Phoenix - Resurgence.jar | 221 | Ultra Phoenix - Resurgence | - | 1.1.3 | MIDP-2.0 | - | 30 | 16 | 600 | 7783282 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/UnoSpin_1.0.6_320x240.jar | 757 | UNO Spin | - | 1.0.6 | MIDP-2.0 | - | 30 | 16 | 600 | 49659629 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/Uno_1.0.1_320x240.jar | 547 | UNO | - | 1.0.1 | MIDP-2.0 | - | 15 | 16 | 600 | 99956223 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/Urban Attack 176x220.jar | 1109 | Urban Attack by piratesxxl.1 | - | 1.2.8 | MIDP-1.0 | - | 4 | 16 | 600 | 24534414 | 0 | - | 运行正常 |  |
| vault/Urban_Soccer.jar | 96 | Urban Soccer | - | 1.0.0 | MIDP-2.0 | - | 4 | 16 | 600 | 18738801 | 0 | java/io/IOException,java/lang/Il | 运行正常 |  |
| vault/V-Rally 3D CZ.jar | 582 | V-Rally3D | - | 1.2.5 | MIDP-2.0 | mascot | 114 | 16 | 600 | 14423947 | 0 | javax/microedition/rms/InvalidRe | 运行正常 |  |
| vault/V-Rally 3D.jar | 304 | V-Rally 3D | - | 1.5.2 | MIDP-2.0 | - | 99 | 16 | 600 | 293785 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/V-Rally3D.jar | 301 | V-Rally3D | - | 1.3.1 | MIDP-2.0 | m3g | 95 | 16 | 600 | 14057915 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/V-Rally_3D.jar | 565 | V-Rally3D | - | 1.2.5 | MIDP-2.0 | mascot | 114 | 16 | 600 | 14450919 | 0 | javax/microedition/rms/InvalidRe | 运行正常 |  |
| vault/VampireRomance_1.1.0_320x240.jar | 1053 | Vampire Romance | - | 1.1.0 | MIDP-2.0 | nokia-ui | 16 | 16 | 600 | 207585811 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/Vampire_Bloodline_BFLM.jar | 156 | Vampire Bloodline | - | 1.0.0 | MIDP-1.0 | nokia-ui | 6 | 16 | 600 | 1491205 | 0 | java/lang/ArrayIndexOutOfBoundsE | 运行正常 |  |
| vault/Van_Helsing.jar | 96 | Van Helsing | - | 1.1.7 | MIDP-1.0 | - | 11 | 16 | 600 | 343411 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/Vanguard_sek700.ic.cz.jar | 62 | Vanguard | - | 1.0.1 | MIDP-1.0 | nokia-ui | 24 | 16 | 600 | 2346523 | 0 | - | 运行正常 |  |
| vault/Vans_Skate_And_Slam.jar | 197 | VansSkateAndSlam | - | 1.2.0 | MIDP-1.0 | nokia-ui | 9 | 16 | 600 | 24774932 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/Vans_Skate_And_Slam_T610__T630__K700.jar | 99 | Vans Skate And Slam | - | 1.0.1 | MIDP-1.0 | - | 5 | 16 | 600 | 2648401731 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/Venus_Flytrap_BFLM.v1.0.jar | 84 | Venus Flytrap | - | 1.0 | MIDP-1.0 | nokia-ui | 7 | 16 | 600 | 17510 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/Vietcong.jar | 94 | Vietcong | - | 1.0 | MIDP-1.0 | nokia-ui | 19 | 16 | 600 | 1540241411 | 0 | - | 运行正常 | runs without input; the generic key script d |
| vault/Vijay_Singh_Pro_Golf_2005.jar | 180 | Vijay Singh Pro Golf 2005 | - | 1.8.2 | MIDP-1.0 | nokia-ui | 11 | 16 | 600 | 18595799 | 0 | - | 运行正常 |  |
| vault/VikingGames.jar | 142 | Viking Games [terjef] | - | 1.9 | MIDP-1.0 | - | 8 | 16 | 600 | 418227 | 0 | javax/microedition/rms/InvalidRe | 运行正常 |  |
| vault/Violence_Combat_by_BFLM.jar | 102 | Violence Combat | - | 1.0 | MIDP-1.0 | nokia-ui | 4 | 16 | 600 | 19295 | 0 | java/io/IOException,java/lang/Nu | 运行正常 | runs without input; the generic key script t |
| vault/VirtualRabbit.jar | 80 | VirtualRabbit | - | 1.0 | MIDP-1.0 | - | 2 | 16 | 600 | 170844 | 0 | - | 运行正常 |  |
| vault/Virtual_Pilot_by_BFLM.jar | 57 | Virtual Pilot | - | 1.0 | MIDP-1.0 | nokia-ui | 6 | 16 | 600 | 56832 | 0 | - | 运行正常 |  |
| vault/VitesseIllimitee.jar | 95 | VitesseIllimitee (RoS) | - | 1.0 | MIDP-1.0 | nokia-ui | 8 | 16 | 600 | 85692 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/Vodafone McLaren Mercedes Team Racing.jar | 202 | VMM Team Racing by NH | - | 1.0.10 | MIDP-1.0 | - | 14 | 16 | 600 | 613357 | 0 | java/io/EOFException,java/lang/N | 运行正常 |  |
| vault/WORLD-FIGHTING.jar | 200 | World Fighting | - | 1.0 | MIDP-1.0 | nokia-ui | 13 | 16 | 600 | 291353 | 0 | javax/microedition/rms/InvalidRe | 运行正常 |  |
| vault/WTPaparazziEdition.jar | 96 | WTPaparazziEdition_RoS_N60 | - | 1.0 | MIDP-1.0 | nokia-ui | 9 | 16 | 600 | 156342 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/WWE_Cage_by_BFLM.jar | 108 | WWE(TM) Cage | - | 1.1.21 | MIDP-1.0 | nokia-ui | 6 | 16 | 600 | 620323 | 0 | - | 运行正常 |  |
| vault/WWE_Hardcore_by_BFLM.jar | 152 | WWE(TM) Hardcore | - | 1.1.6 | MIDP-1.0 | nokia-ui | 7 | 16 | 600 | 140426 | 0 | - | 运行正常 |  |
| vault/War Hero 1944.jar | 278 | War Hero 1944 Shared by Mate | - | 1.0.62 | MIDP-1.0 | - | 15 | 16 | 600 | 12641036 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/Watchmen.jar | 607 | Watchmen | - | 1.3.3 | MIDP-2.0 | - | 125 | 16 | 600 | 3951695 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/WhereInTheWorldIsCarmenSandiego_1.0.1_320x240.jar | 663 | Where in The World is Carmen | - | 1.0.1 | MIDP-2.0 | - | 5 | 16 | 600 | 33073560 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/WheresWallyNow_1.0.1_320x240.jar | 988 | Where is Wally Now | - | 1.0.1 | MIDP-2.0 | - | 7 | 16 | 600 | 43797142 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/Wild West Guns.jar | 1472 | Wild West Guns | - | 1.2.6 | MIDP-2.0 | - | 17 | 16 | 600 | 189574104 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/Wimbledon2009_1.0.4_320x240.jar | 706 | wimbledon 2009 | - | 1.0.4 | MIDP-2.0 | - | 11 | 16 | 600 | 88934932 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/Wizard_Pinball.jar | 80 | - | - | - | - | nokia-ui | 0 | 16 | 600 | 592445 | 0 | java/io/EOFException | 运行正常 |  |
| vault/Wizardry.jar | 187 | Wizardry | - | 1.75 | MIDP-1.0 | - | 19 | 16 | 600 | 60989014 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/Wokwms60_sek700.wz.cz.jar | 104 | Wok WM | - | 1.0.0 | MIDP-1.0 | nokia-ui | 21 | 16 | 600 | 464912 | 0 | - | 运行正常 |  |
| vault/Wolf_Striker.jar | 114 | Wolf Striker | - | 1.1 | MIDP-2.0 | - | 31 | 16 | 600 | 105648 | 0 | - | 运行正常 |  |
| vault/Womens Murder Club_320x240_Nokia_E71.jar | 965 | Women's Murder Club Konon.Mo | - | 1.2.2 | MIDP-2.0 | - | 59 | 16 | 600 | 1333836 | 0 | java/lang/ArrayIndexOutOfBoundsE | 运行正常 |  |
| vault/Wonder Blocks.jar | 612 | Wonder Blocks | - | 1.1.0 | MIDP-2.0 | - | 10 | 16 | 600 | 22741070 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/Wonder Zoo.jar | 837 | Wonder Zoo | - | 1.2.9 | MIDP-2.0 | - | 14 | 16 | 600 | 17815357 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/Woody Woodpecker in Waterfools.jar | 615 | woody | - | 2.0.0 | MIDP-2.0 | - | 21 | 16 | 600 | 1044972 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/Woof_by_BFLM.jar | 55 | Woof | - | 1.0 | MIDP-1.0 | nokia-ui | 5 | 16 | 600 | 92438 | 0 | - | 运行正常 |  |
| vault/Word_Craze.jar | 67 | Word Craze | - | 1.0.9 | MIDP-1.0 | - | 11 | 16 | 600 | 294151 | 0 | - | 运行正常 |  |
| vault/World Rally Championship 3D.jar | 635 | WRC 3D | - | 0.8.4 | MIDP-2.0 | - | 4 | 16 | 600 | 5337759 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/World at Arms.jar | 1957 | World At Arms | - | 1.0.9 | MIDP-2.0 | - | 16 | 16 | 600 | 422495137 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/Worms 2.jar | 233 | Worms K750 | - | 1.3.3 | MIDP-2.0 | - | 28 | 16 | 600 | 1950066 | 0 | - | 运行正常 |  |
| vault/Worms 2007-176x220.jar | 210 | Worms_2007_SK | - | 0.7.4 | MIDP-2.0 | - | 31 | 16 | 600 | 3512866453 | 0 | java/lang/ArrayIndexOutOfBoundsE | 运行正常 |  |
| vault/Worms 2008.jar | 567 | Worms 2008 | - | 1.3.11 | MIDP-2.0 | - | 20 | 16 | 600 | 792340 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/Worms 2010.jar | 996 | Worms | - | 4.10.60 | MIDP-2.0 | - | 22 | 16 | 600 | 7856881 | 0 | - | 运行正常 |  |
| vault/Worms 2011 Armageddon.jar | 702 | Worms 2011 Armageddon | - | 4.1.18 | MIDP-2.0 | - | 34 | 16 | 601 | 3009588 | 0 | - | 运行正常 |  |
| vault/Worms Forts 3D CZ.jar | 257 | WormsForts3D | - | 36.0 | MIDP-2.0 | mascot,nokia-ui | 33 | 16 | 600 | 5647197 | 0 | - | 运行正常 |  |
| vault/Worms Forts 3D.jar | 198 | Worms Forts | - | 0.80 | MIDP-1.0 | nokia-ui | 8 | 16 | 600 | 11802481 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/Worms Forts-176x220.jar | 63 | Worms Forts | - | 1.0.6 | MIDP-1.0 | nokia-ui | 8 | 16 | 600 | 29480578 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/Worms Forts.jar | 204 | Worms Forts | - | 0.80 | MIDP-1.0 | - | 8 | 16 | 600 | 9523444 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/Worms Golf.jar | 66 | Worms Golf | - | 1.0.6 | MIDP-1.0 | nokia-ui | 5 | 16 | 600 | 332911 | 0 | - | 运行正常 |  |
| vault/Worms Reloaded.jar | 757 | Worms Reloaded | - | 4.4.53 | MIDP-2.0 | - | 35 | 16 | 600 | 12965135 | 0 | - | 运行正常 |  |
| vault/Worms.jar | 58 | - | - | - | - | nokia-ui | 0 | 16 | 600 | 1509289 | 0 | - | 运行正常 |  |
| vault/WormsForts_K700.jar | 204 | Worms Forts [terjef] | - | 0.80 | MIDP-1.0 | - | 8 | 16 | 600 | 9524300 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/WormsGolf_K700_EN.jar | 68 | WormsGolf [terjef] | - | 1.0 | MIDP-2.0 | - | 5 | 16 | 600 | 323810 | 0 | java/lang/IllegalArgumentExcepti | 运行正常 |  |
| vault/Worms_K700.jar | 96 | Worms [terjef] | - | 1.0.0 | MIDP-2.0 | - | 15 | 16 | 600 | 3377014 | 0 | - | 运行正常 |  |
| vault/Wrath of the Titans.jar | 1557 | Wrath of the Titans | - | 1.3.2 | MIDP-2.0 | - | 20 | 16 | 600 | 175367175 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/Wsciekly_Mikolaj.jar | 108 | Wsciekly Mikolaj | - | 1.0 | MIDP-1.0 | nokia-ui | 18 | 16 | 600 | 416470770 | 0 | - | 运行正常 |  |
| vault/XIII - Covert Identity.jar | 598 | XIII_2 | - | 1.0.0 | MIDP-2.0 | - | 5 | 16 | 600 | 530645441 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/XIII_by_BFLM.jar | 141 | XIII | - | 2.0.3 | MIDP-1.0 | nokia-ui | 4 | 16 | 600 | 3038676166 | 0 | - | 运行正常 |  |
| vault/XIMPDA___Dungeon_Capers_TM__v_1__1__1_.1.8.17.jar | 88 | XiMPDA- Dungeon Capers(TM) | - | 1.8.17 | MIDP-1.0 | nokia-ui | 5 | 16 | 600 | 752064 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/XIMPDA___Tank_Terror_TM__v_1__1__1_.1.7.14.jar | 89 | XiMPDA- Tank Terror(TM) | - | 1.7.14 | MIDP-1.0 | nokia-ui | 8 | 16 | 600 | 2531903 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/XiMPDA___Ground_Pounder_TM__v_1__1__1_.1.1.8.jar | 88 | XiMPDA- Ground Pounder(TM) | - | 1.1.8 | MIDP-1.0 | nokia-ui | 8 | 16 | 600 | 214205 | 0 | - | 运行正常 | runs without input; the generic key script m |
| vault/Xmas Jewel Explosion__320x240.jar | 180 | X-mas Jewel Explosion | - | 1.0.2 | MIDP-2.0 | - | 36 | 16 | 600 | 2118448917 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/XmasSkies.jar | 107 | Xmas Skies | - | 1.0 | MIDP-1.0 | nokia-ui | 37 | 16 | 600 | 10244611 | 0 | java/lang/ArrayIndexOutOfBoundsE | 运行正常 |  |
| vault/Xmen2_by_BFLM.jar | 219 | Xmen(tm)2 Battle | - | 0.36 | MIDP-1.0 | nokia-ui | 5 | 16 | 600 | 1292326 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/Xpider.jar | 62 | Xpider | - | 1.0 | MIDP-1.0 | nokia-ui | 19 | 16 | 600 | 89101 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/Xrjetski_sek700.wz.cz.jar | 61 | XR-JetSki | - | 1.0.0 | MIDP-1.0 | nokia-ui | 3 | 16 | 600 | 1269999 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/Xtreme Dirt Bike 176x208.jar | 260 | XtremeBike by piratesxxl.110 | - | 1.0.5 | MIDP-2.0 | nokia-ui | 11 | 16 | 600 | 3872716684 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/Xtreme Dirt Bike 176x220.jar | 295 | Off-Road Dirt Motocross | - | 1.0.5 | MIDP-2.0 | - | 11 | 16 | 600 | 3669774749 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/Xtreme_Volleyball_by_BFLM.jar | 176 | DoAX | - | 1.0.1 | MIDP-1.0 | nokia-ui | 5 | 16 | 600 | 3215553381 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/YETISPORTS_Part_3.jar | 73 | YETISPORTS Part 3 | - | 1.0 | MIDP-1.0 | - | 19 | 16 | 600 | 81041 | 0 | - | 运行正常 |  |
| vault/Yahtzee.jar | 62 | YatchGame | - | 1.01 | MIDP-1.0 | nokia-ui | 56 | 16 | 600 | 9934 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/Yamakasi Masters-176x220.jar | 381 | Yamakasi Masters by BlackFan | - | 1.0.0 | MIDP-2.0 | - | 14 | 16 | 600 | 6907337 | 0 | - | 运行正常 |  |
| vault/Yatzee_k700.jar | 98 | Yatzee [terjef] | - | 1.0.0 | MIDP-1.0 | - | 18 | 16 | 600 | 30286 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/Yeti Sports - Stage Dive.jar | 63 | Stage Dive | - | 1.0.0 | MIDP-1.0 | - | 29 | 16 | 601 | 4341815 | 0 | java/io/IOException | 运行正常 |  |
| vault/YetiSportsEpisode_5.jar | 63 | YETISPORTS 5 | - | 1.0.0 | MIDP-1.0 | - | 30 | 16 | 601 | 3492077 | 0 | - | 运行正常 |  |
| vault/YetiSports_7.jar | 62 | YETISPORTS 7 | - | 1.0.1 | MIDP-1.0 | - | 28 | 16 | 601 | 2552634 | 0 | - | 运行正常 |  |
| vault/Yeti_Sports_1_by_BFLM.jar | 53 | YETISPORTSP1 | - | 1.0 | MIDP-1.0 | - | 15 | 16 | 600 | 18796 | 0 | - | 运行正常 | runs without input; the generic key script m |
| vault/Yeti_Sports_2_by_BFLM.jar | 54 | YETISPORTS Part 2 | - | 1.0 | MIDP-1.0 | - | 17 | 16 | 600 | 360575 | 0 | java/lang/IllegalArgumentExcepti | 运行正常 |  |
| vault/Yetisports8.jar | 69 | YETISPORTS 8 | - | 1.0 | MIDP-1.0 | nokia-ui | 5 | 16 | 600 | 9059373629 | 0 | java/lang/NullPointerException,j | 运行正常 | runs without input; the generic key script m |
| vault/Yetisports9.jar | 112 | YETISPORTS 9-kryak- | - | 1.2 | MIDP-1.0 | - | 6 | 16 | 600 | 10168549002 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/Ylympicsr.jar | 81 | YLYMPICS | - | 1.0.0 | MIDP-1.0 | nokia-ui | 19 | 16 | 600 | 137502 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/Yogi_Bear_Pic_a_nic_by_BFLM.jar | 83 | Yogi Bear Pic-a-nic | - | 1.0.0 | MIDP-1.0 | nokia-ui | 20 | 16 | 600 | 244417 | 0 | - | 运行正常 |  |
| vault/YosamaBin.jar | 110 | YosamaBin_N60 | - | 1.0 | MIDP-1.0 | nokia-ui | 8 | 16 | 600 | 104170 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/Yoyo_Fighter_sek700.ic.cz.jar | 77 | Yoyo Fighter | - | 1.0.7 | MIDP-1.0 | nokia-ui | 11 | 16 | 600 | 1098515 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/Zombie Infection 2.jar | 1224 | Zombie Infection 2 | - | 1.1.9 | MIDP-2.0 | - | 13 | 16 | 600 | 202116318 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/Zombie Infection.jar | 1013 | Zombie Infection | - | 1.2.0 | MIDP-2.0 | - | 8 | 16 | 601 | 22099539 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/Zombie Mob Defense.jar | 772 | Zombie Mob | - | 1.0.0 | MIDP-2.0 | - | 39 | 16 | 600 | 4088760 | 0 | java/io/EOFException,javax/micro | 运行正常 |  |
| vault/ZombieInfection1_1.0.0_320x240.jar | 358 | Zombie Infection | - | 1.0.0 | MIDP-2.0 | - | 9 | 16 | 600 | 401632137 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/ZombieInfection2_1.0.3_320x240.jar | 430 | Zombie Infection 2 | - | 1.0.3 | MIDP-2.0 | - | 50 | 16 | 600 | 3501547934 | 0 | java/lang/ClassNotFoundException | 运行正常 |  |
| vault/ZombieInfection_SonyEricsson_X1_EN_IGPL_EU_TS_101.jar | 615 | Zombie Infection | - | 1.0.1 | MIDP-2.0 | - | 8 | 16 | 600 | 1632813305 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/Zombiewood.jar | 820 | Zombiewood | - | 1.2.9 | MIDP-2.0 | - | 42 | 16 | 600 | 93994054 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/Zoo Keeper.jar | 366 | ZOO KEEPER | - | 1.1.1 | MIDP-2.0 | - | 9 | 16 | 600 | 1274023133 | 0 | - | 运行正常 |  |
| vault/Zuma's Revenge!.jar | 1118 | Zumas Revenge | - | 00.01.47 | MIDP-2.0 | - | 8 | 16 | 601 | 120025308 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/absolute_puzzle_deluxe.jar | 112 | Absolute Puzzle Deluxe | - | 1.2.1 | MIDP-2.0 | - | 22 | 16 | 600 | 165237 | 0 | java/lang/NumberFormatException, | 运行正常 |  |
| vault/ac.jar | 56 | Air Command | - | 1.1 | MIDP-1.0 | nokia-ui | 10 | 16 | 0 | 1087 | 0 | java/lang/NoClassDefFoundError | 标题自身异常 | title references a class it does not bundle  |
| vault/adventure_time_hero_259515.jar | 232 | Adventure Time | - | 1.0.2 | MIDP-2.0 | - | 12 | 16 | 600 | 7355326 | 0 | java/lang/ArrayIndexOutOfBoundsE | 运行正常 |  |
| vault/adventuret_b1N4sR1I.jar | 276 | Adventure Time | - | 1.0.2 | MIDP-2.0 | - | 15 | 16 | 600 | 2397041 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/age_of_empires_iii_m_259219.jar | 296 | Age Of Empires III Mobile | - | 1.0.1 | MIDP-1.0 | - | 13 | 16 | 600 | 59381867 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/aloneinthe_x1i5e1cw.jar | 2939 | Alone in the dark | - | 1.0 | MIDP-2.0 | - | 54 | 16 | 600 | 93028074 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/alonso2005.jar | 133 | Alonso 2005 | - | 1.0.5 | MIDP-1.0 | nokia-ui | 16 | 16 | 600 | 367426928 | 0 | - | 运行正常 |  |
| vault/alpha_wing_bqYpzkwZ.jar | 261 | Alpha Wing | - | 1.0.0 | MIDP-1.0 | - | 16 | 16 | 600 | 874898 | 0 | java/lang/IllegalArgumentExcepti | 运行正常 |  |
| vault/ancientemp_wn2uiomk.jar | 125 | Ancient Empires | - | 1.0.3 | MIDP-1.0 | nokia-ui | 9 | 16 | 600 | 578985 | 0 | java/lang/ArrayIndexOutOfBoundsE | 运行正常 |  |
| vault/app.jar | 468 | Tomb Raider Legend | - | 1.17 | MIDP-2.0 | m3g | 22 | 16 | 600 | 10769187 | 0 | java/lang/IllegalArgumentExcepti | 运行正常 |  |
| vault/asphalt_3_street_rule_360x640_176330.jar | 1103 | Asphalt 3 3D Street Rules | - | 1.1.5 | MIDP-2.0 | m3g,nokia-ui | 18 | 16 | 600 | 4034352595 | 0 | java/lang/ClassNotFoundException | 运行正常 |  |
| vault/asphalt_6_adrenalin_258989.jar | 2684 | Asphalt6 Adrenaline | - | 1.0.3 | MIDP-2.0 | - | 20 | 32 | 600 | 778151346 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/assasin.jar | 1025 | Assassin's Creed 2 | - | 1.2.3 | MIDP-2.0 | - | 9 | 16 | 600 | 238539020 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/assassins_creed_260615.jar | 633 | Assassins Creed | - | 1.0.0 | MIDP-2.0 | - | 6 | 16 | 0 | 0 | 0 | - | 超时 |  |
| vault/assault.jar | 315 | AssaultTeam3D | - | 1.1 | MIDP-2.0 | - | 32 | 16 | 600 | 25473277 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/avenging-angel-352x416-s60-namobilu.com.jar | 291 | Avenging Angel | - | 2.00.36 | MIDP-2.0 | - | 5 | 16 | 600 | 5323001 | 0 | java/io/IOException,java/lang/Nu | 运行正常 |  |
| vault/beyblade_grevolution(2).jar | 163 | BeyBlade GRevolution | - | 1.2.0 | MIDP-1.0 | - | 9 | 16 | 600 | 1507532 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/biohazard-240x320-namobilu.com.jar | 640 | BioHazard | - | 1.0.0 | MIDP-1.0 | - | 61 | 16 | 0 | 0 | 0 | - | 失败 |  |
| vault/blades_and_magic_3d_multiscreen-27836.jar | 1142 | Blades and Magic | - | 1.0.9 | MIDP-2.0 | mascot | 86 | 16 | 600 | 7833823 | 0 | java/io/IOException,java/lang/Nu | 运行正常 |  |
| vault/bobby carrot 5 k700.jar | 192 | Bobby Carrot 5 | - | 1.0.5 | MIDP-2.0 | - | 2 | 16 | 600 | 748795 | 0 | - | 运行正常 |  |
| vault/bobby_carrot_2_wint_240201.jar | 109 | BC2 - Winterland | - | 1.0.6 | MIDP-1.0 | nokia-ui | 4 | 16 | 600 | 191916 | 0 | - | 运行正常 |  |
| vault/boggle_small-en.jar | 123 | Boggle | - | 1.4.4 | MIDP-2.0 | - | 3 | 16 | 600 | 2097317 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/bogorscastle.jar | 112 | BogorsCastle | - | 1.0 | MIDP-1.0 | nokia-ui | 11 | 16 | 600 | 132269 | 0 | - | 运行正常 |  |
| vault/boink reloaded.jar | 103 | Boink Reloaded (ChipedUK) | - | 2.0 | MIDP-1.0 | nokia-ui | 4 | 16 | 600 | 1693459 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/bounce_Nokia.jar | 39 | Bounce | - | 1.24 | MIDP-1.0 | nokia-ui | 8 | 16 | 600 | 5071 | 0 | - | 运行正常 | runs without input; the generic key script m |
| vault/bounce_tale_qy5sf1si.jar | 426 | - | - | - | - | - | 0 | 16 | 0 | 0 | 0 | - | 非可用包 | not a zip file |
| vault/bounce_tales_travel_240x320_191806.jar | 521 | Bounce Tales | - | 2.0.3 | MIDP-2.0 | nokia-ui | 19 | 16 | 600 | 70985382 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/bounce_water_dash_240x320_191284.jar | 93 | Bounce Water Dash v0.3 TEGOS | - | 1.0.0 | MIDP-2.0 | - | 25 | 16 | 600 | 878022 | 0 | java/lang/ClassNotFoundException | 运行正常 |  |
| vault/bounceback_64b1jou2.jar | 96 | Bounce Back | - | 1.13 | MIDP-1.0 | nokia-ui | 10 | 16 | 600 | 101688 | 0 | - | 运行正常 |  |
| vault/bouncedash_shItN4ur.jar | 153 | Bounce Dash | - | 1.0.0 | MIDP-2.0 | - | 26 | 16 | 600 | 3343268212 | 0 | java/lang/ClassNotFoundException | 运行正常 |  |
| vault/bouncetale_9s1otkmz.jar | 368 | Bounce Tales | - | 2.0.3 | MIDP-2.0 | nokia-ui | 19 | 16 | 1 | 37056 | 0 | - | 标题自行退出 | exited itself (notifyDestroyed) after 1 fram |
| vault/bouncetale_e568o65n.jar | 356 | Bounce Tales 2 | - | 2.0.3 | MIDP-2.0 | nokia-ui | 19 | 16 | 600 | 70993550 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/bouncetale_xzz6ksx6.jar | 1027 | Bounce Tales mod by Gamemen | - | 2.0.3 | MIDP-2.0 | nokia-ui | 19 | 16 | 600 | 71396419 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/brick_breaker_explos_131333.jar | 99 | Brick Breaker Explosive | - | 1.0.0 | MIDP-2.0 | - | 14 | 16 | 600 | 2575182490 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/brick_breaker_revolu_360x640_138536.jar | 494 | BB Revolution2 | - | 1.0.11 | MIDP-2.0 | - | 57 | 16 | 600 | 53847478 | 0 | java/io/EOFException,javax/micro | 运行正常 |  |
| vault/brickbreak_tbpx16jj.jar | 1097 | Block Breaker Deluxe 2 by D@ | - | 1.1.5 | MIDP-2.0 | - | 11 | 16 | 600 | 7570822418 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/buffy.jar | 180 | Buffy, The Vampire Slayer | - | 1.1.1 | MIDP-1.0 | nokia-ui | 7 | 16 | 600 | 4245904 | 0 | - | 运行正常 |  |
| vault/buggy-x3d_yktouq6k.jar | 253 | X-buggy 3d-Zub | - | 1.0 | MIDP-2.0 | - | 9 | 16 | 600 | 71735 | 0 | javax/microedition/rms/InvalidRe | 运行正常 |  |
| vault/bulldozerinc_k750.jar | 209 | BulldozerInc | - | 1.0 | MIDP-2.0 | - | 5 | 16 | 600 | 3771512 | 0 | java/io/IOException,java/lang/Nu | 运行正常 |  |
| vault/button_s40.jar | 62 | Button_s40 | - | F1.1 | MIDP-1.0 | nokia-ui | 14 | 16 | 600 | 1753620 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/castle_of_magic_256724.jar | 1333 | Castle of Magic | - | 1.1.0 | MIDP-2.0 | nokia-ui | 10 | 16 | 600 | 165318 | 0 | - | 运行正常 |  |
| vault/castlevani_hvcp9fbl.jar | 405 | Castlevania Aria Of Sorrow | - | 1.0.0 | MIDP-2.0 | - | 5 | 16 | 600 | 12154849 | 0 | javax/microedition/rms/InvalidRe | 运行正常 |  |
| vault/castlevania_dawn_of_240x320_168931.jar | 610 | Castlevania:Paccвeт пeчaли | - | 0.1.2 | MIDP-2.0No | - | 20 | 16 | 600 | 930170 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/chocolate3_klm926l6.jar | 455 | 3D Tornado Mania | - | 1.0.0 | MIDP-2.0 | m3g,nokia-ui | 24 | 16 | 600 | 17420007 | 0 | java/io/EOFException,java/lang/N | 运行正常 |  |
| vault/city_builder_se_MIDP1_0_0_1.jar | 47 | City Builder | - | 0.0.1 | MIDP-1.0 | - | 8 | 16 | 600 | 6288935965 | 0 | - | 运行正常 |  |
| vault/city_rush_256058.jar | 717 | CityRush | - | 1.0.0 | MIDP-2.0 | - | 95 | 16 | 600 | 13257866 | 0 | java/io/IOException,java/lang/Nu | 运行正常 |  |
| vault/command_amp_conque_261392.jar | 1143 | C&C4 Tiberian Twilight | - | 4.1.58 | MIDP-2.0 | - | 37 | 16 | 600 | 3998424 | 0 | - | 运行正常 |  |
| vault/connect2media_f_Acer_S200NeoTouch_X1i_en_castlevaniaorderofshadows_v0_0_1.jar | 555 | CastlevaniaOoS | - | 0.0.1 | MIDP-2.0 | - | 70 | 16 | 600 | 1023283 | 0 | javax/microedition/rms/InvalidRe | 运行正常 |  |
| vault/cowboys_amp_aliens_261159.jar | 3442 | Cowboys And Aliens | - | 1.2.3 | MIDP-2.0 | - | 18 | 16 | 600 | 107512974 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/critter_crunch_133978_1657.jar | 222 | Critter Crunch | - | 1.0.0 | MIDP-1.0 | nokia-ui | 10 | 16 | 600 | 176126061 | 0 | java/io/EOFException,java/io/IOE | 运行正常 |  |
| vault/cubico.jar | 198 | Cubico | - | 1.0.9 | MIDP-2.0 | m3g | 41 | 16 | 600 | 22322131 | 0 | java/io/IOException,java/lang/Ar | 运行正常 |  |
| vault/cyber_punk.jar | 404 | CyberPunk Arasakas Plot Etty | - | 1.3.2 | MIDP-2.0 | nokia-ui | 41 | 16 | 600 | 1079250 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/darkest-fear-176x220-320x480-namobilu.com.jar | 185 | Darkest Fear | - | 1.19 | MIDP-1.0 | - | 20 | 16 | 600 | 4458255645 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/darkest-fear-2-grim-oak-240x320-s40-namobilu.com.jar | 213 | Darkest Fear 2 Grim Oak | - | 1.30 | MIDP-1.0 | - | 14 | 16 | 600 | 1258018603 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/darkest-fear-3-nightmare-352x416-s60-namobilu.com.jar | 283 | Darkest Fear Nightmare | - | 1.20 | MIDP-1.0 | - | 13 | 16 | 600 | 799004 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/darkest_fear_multiscreen-119931.jar | 182 | Darkest Fear | - | 1.19 | MIDP-1.0 | nokia-ui | 20 | 16 | 600 | 4538934483 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/dead-daughters-240x320-namobilu.com.jar | 166 | Dead Daughters srv | - | 1.0.24 | MIDP-2.0 | - | 60 | 16 | 600 | 43834757 | 0 | java/io/IOException | 运行正常 |  |
| vault/death-trap-176x208-namobilu.com.jar | 98 | Death Trap | - | 1.0.0 | MIDP-2.0 | - | 18 | 16 | 600 | 2752016 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/death-trap-2-the-unlocked-code-240x320-namobilu.com.jar | 255 | Death Trap 2 | - | 1.0.0 | MIDP-2.0 | - | 12 | 16 | 600 | 2446559 | 0 | java/lang/ArrayIndexOutOfBoundsE | 运行正常 |  |
| vault/doom-rpg-ii-360x640-s60-touch-namobilu.com.jar | 1103 | Doom II RPG | - | 5.4.14 | MIDP-2.0 | - | 37 | 16 | 600 | 15806798 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/doom-rpg-mobile-240x320-namobilu.com.jar | 295 | Doom RPG | - | 1.8.84 | MIDP-1.0 | - | 26 | 16 | 600 | 7897808 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/doomrpg_tmmcss29.jar | 274 | Doom RPG | - | 1.0.20 | MIDP-2.0 | - | 23 | 16 | 600 | 17311305 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/dragonisla_ortv8jio.jar | 76 | Dragon Island | - | 1.0.2 | MIDP-1.0 | nokia-ui | 26 | 16 | 600 | 39669 | 0 | - | 运行正常 |  |
| vault/drives-the-evil-spirit-240x320-namobilu.com.jar | 495 | Drives the Evil Spirit | - | 1.0 | MIDP-2.0 | - | 27 | 16 | 600 | 378405 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/dust_n_dirt.jar | 196 | DustNDirt | - | 1.0.0 | MIDP-1.0 | nokia-ui | 5 | 16 | 600 | 14501 | 0 | java/lang/NullPointerException,j | 运行正常 | painted 600 frames but made no draw calls (b |
| vault/ea_fifa_2007_176x208_fixed_s60_j2me.jar | 314 | FIFA2007 | - | 1.2.47 | MIDP-1.0 | - | 5 | 16 | 600 | 3455525 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/edge_extended_260821.jar | 396 | Edge EX | - | 1.0.3 | MIDP-1.0 | - | 2 | 16 | 600 | 21812705 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/edragon.jar | 120 | Eragon | - | 1.2.8 | MIDP-1.0 | - | 2 | 16 | 600 | 3776537398 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/eightfatet_tt83sn7j.jar | 574 | Eight Fate The Horror Before | - | 1.0 | MIDP-1.0 | - | 10 | 16 | 600 | 2458090130 | 0 | - | 运行正常 |  |
| vault/electronicarts_tombraiderunderworld_de_en_es_fr_it_Nokia_5530_v1_8_8.jar | 1304 | TR Underworld | - | 1.8.8 | MIDP-1.0 | m3g | 7 | 16 | 600 | 17834022 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/eliminator.jar | 303 | Eliminator Elite | - | 1.0 | MIDP-2.0 | - | 18 | 16 | 600 | 146244 | 0 | - | 运行正常 |  |
| vault/elvenchron_66js6a7i.jar | 232 | Elven Chronicles by street_r | - | 1.0.5 | MIDP-2.0 | nokia-ui | 11 | 16 | 600 | 13418297 | 0 | java/lang/NumberFormatException | 运行正常 |  |
| vault/empiresk700i.jar | 291 | Rise of Lost Empires | - | 1.0.2 | MIDP-2.0 | - | 9 | 16 | 601 | 3758575253 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/epub_reader_albite_136959_great.jar | 798 | Albite Reader | - | 2.2 | MIDP-2.0 | - | 149 | 16 | 60 | 296 | 0 | java/lang/NullPointerException | 标题自身异常 | title passes null to Displayable.setTitle()  |
| vault/fifa23 j2me.jar | 1050 | FIFA 23 | - | 1.1.0 | MIDP-2.0 | - | 29 | 16 | 600 | 57670362 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/fifa_2005_infinity.jar | 138 | EA FIFA 2005 | - | 0.5.4 | MIDP-1.0 | - | 3 | 16 | 600 | 654501 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/fightnight_t9dyled6.jar | 333 | FNR 3 (Mobilecastle.biz) | - | 4.6.0 | MIDP-2.0 | m3g | 25 | 16 | 600 | 8015249 | 0 | java/lang/NullPointerException,j | 运行正常 | runs without input; the generic key script t |
| vault/fileshare.ro_KOFighters_Nokia6280_EN_IGPE_EU_114.jar | 514 | KO Fighters | - | 1.1.4 | MIDP-2.0 | - | 9 | 16 | 600 | 7070930581 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/fishhunt.jar | 85 | FishHunt | - | 1.0 | MIDP-1.0 | nokia-ui | 5 | 16 | 600 | 103205 | 0 | - | 运行正常 |  |
| vault/flatout.jar | 353 | FlatOut Racing3D | - | 0.6.3 | MIDP-2.0 | m3g | 54 | 16 | 625 | 134818141 | 0 | java/io/IOException,java/lang/Cl | 运行正常 |  |
| vault/game-of-death-240x320-namobilu.com.jar | 596 | 死亡游戏(3D增强版) | - | 1.0.0 | MIDP-2.0 | - | 26 | 16 | 600 | 16008257 | 0 | - | 运行正常 |  |
| vault/gameloft.medieval.combat.v1.04.s60.java.retail-binpda.jar | 200 | Medieval Combat - Age Of Glo | - | 1.0.4 | MIDP-1.0 | nokia-ui | 5 | 16 | 600 | 3542268305 | 0 | - | 运行正常 |  |
| vault/gangstar_1_city.jar | 299 | Gangstar -leo5354- | - | 1.0.5 | MIDP-2.0 | - | 5 | 16 | 600 | 5166302369 | 0 | javax/microedition/rms/InvalidRe | 运行正常 |  |
| vault/gangstar_2_kings_of_260766.jar | 645 | Gangstar 2 | - | 1.0.3 | MIDP-2.0 | - | 12 | 16 | 0 | 0 | 0 | - | 超时 |  |
| vault/gangstar_rio_city_o_260851.jar | 2111 | Gangstar Rio City of Saints | - | 1.1.3 | MIDP-2.0 | - | 15 | 16 | 600 | 666857885 | 0 | - | 运行正常 |  |
| vault/glu_sonicthehedgehog2dash_sonye_k800_de_en_es_fr_it_pt_v1_0_0_full.jar | 515 | Sonic the Hedgehog 2 Dash | - | 1.0.0 | MIDP-2.0 | - | 28 | 16 | 600 | 328770 | 0 | java/io/IOException,javax/microe | 运行正常 |  |
| vault/go_to_hell_139738.jar | 124 | Go To Hell | - | 5.0 | MIDP-2.0 | - | 14 | 16 | 600 | 463520193 | 0 | - | 运行正常 |  |
| vault/god_of_war_betrayal_258309.jar | 417 | God of War | - | 1.0.0 | MIDP-1.0 | - | 7 | 16 | 600 | 20980967 | 0 | java/lang/NumberFormatException, | 运行正常 |  |
| vault/gothic_3_-_the_beginning_multiscreen-57752.jar | 318 | Gothic3TheBeginning | - | 1.0 | MIDP-2.0 | nokia-ui | 5 | 16 | 600 | 3625161 | 0 | java/io/IOException,java/lang/Nu | 运行正常 |  |
| vault/gumballjou_alRKKtCJ.jar | 1624 | Gumball | - | 1.1.2 | MIDP-2.0 | - | 16 | 16 | 600 | 3942772 | 0 | - | 运行正常 |  |
| vault/hauntings-the-abandoned-mansion-240x320-namobilu.com.jar | 422 | Haunting Mansion | - | 1.0.0 | MIDP-2.0 | - | 5 | 16 | 600 | 26871628 | 0 | - | 运行正常 |  |
| vault/holywar_fsb7evml.jar | 207 | HolyWars | - | 01.01.02 | MIDP-2.0 | nokia-ui | 19 | 16 | 600 | 1015556 | 0 | - | 运行正常 |  |
| vault/house-13-240x320-namobilu.com.jar | 322 | House N13 | - | 1.0 | MIDP-2.0 | - | 4 | 16 | 600 | 232304 | 0 | - | 运行正常 |  |
| vault/hummer3d_240x320_nokia5300_6270_7373_e65_n73_n95.jar | 544 | HUMMER Jump and Race by serv | - | 1.0.0 | MIDP-2.0 | m3g | 28 | 16 | 600 | 43886670 | 0 | javax/microedition/rms/InvalidRe | 运行正常 |  |
| vault/iSudoku_320x240.jar | 164 | iSudoku | - | 1.0.0 | MIDP-1.0 | - | 65 | 16 | 600 | 642194 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/iceage2.jar | 187 | Ice Age 2 | - | 1.0.1 | MIDP-1.0 | - | 5 | 16 | 600 | 4650890 | 0 | java/io/EOFException,javax/micro | 运行正常 |  |
| vault/king_kong.jar | 212 | King Kong The Official Mobil | - | 1.0.8 | MIDP-2.0 | - | 9 | 16 | 600 | 5154179434 | 0 | - | 运行正常 |  |
| vault/knight_tales_land_o_240742.jar | 168 | KnightTales | - | 01.00.04 | MIDP-1.0 | nokia-ui | 11 | 16 | 600 | 602675 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/kof.jar | 147 | KingOfFighters | - | 0.1.5 | MIDP-1.0 | nokia-ui | 9 | 16 | 600 | 724824 | 0 | javax/microedition/rms/InvalidRe | 运行正常 |  |
| vault/kuju_wireless_lotus_challenge_city_racing.jar | 100 | Lotus | - | 1.0.8 | MIDP-1.0 | - | 20 | 16 | 600 | 96898 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/kviz-pro-kazdeho-cz.jar | 398 | Common Knowledge Quiz | - | 1.0.1 | MIDP-2.0 | nokia-ui | 6 | 16 | 600 | 3171250317 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/land_of_the_dead_day_320x240_136780.jar | 306 | LOTD | - | 1.1.14 | MIDP-1.0 | - | 10 | 16 | 600 | 314909437 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/lego_batman_the_mob_261440.jar | 2192 | Lego Batman | - | 1.1.9 | MIDP-2.0 | - | 17 | 16 | 600 | 391839254 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/macrospace_anakonda.jar | 82 | Anakonda | - | 1.0.0 | MIDP-1.0 | - | 15 | 16 | 600 | 1986658 | 0 | java/io/IOException,java/lang/Nu | 运行正常 |  |
| vault/mafia.jar | 375 | Mafia Wars 3 | - | 1.2.03 | MIDP-2.0 | nokia-ui | 26 | 16 | 600 | 121897 | 0 | - | 运行正常 |  |
| vault/miami_nights_2_the_258694.jar | 1620 | Miami Nights 2 | - | 1.0.7 | MIDP-2.0 | - | 9 | 16 | 600 | 1323143672 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/midnight_bowling2_176x220.jar | 284 | Midnight Bowling 2 | - | 1.1.1 | MIDP-2.0 | - | 5 | 16 | 600 | 22299706 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/midnight_pool_3_258300.jar | 2301 | Midnight Pool 3 | - | 1.2.3 | MIDP-2.0 | - | 30 | 16 | 600 | 1143091305 | 0 | java/lang/ArrayIndexOutOfBoundsE | 运行正常 |  |
| vault/mini3draci_ix48iqod.jar | 252 | MINI 3D | - | 1.0.22 | MIDP-2.0 | mascot | 43 | 16 | 600 | 500491 | 0 | java/lang/IllegalArgumentExcepti | 运行正常 |  |
| vault/mobman_m2.jar | 30 | MobMan | - | 1.0 | MIDP-1.0 | - | 3 | 16 | 600 | 70429 | 0 | - | 运行正常 | runs without input; the generic key script m |
| vault/modern_combat_2_bla_261155.jar | 3061 | Modern Combat 2: Black Pegas | - | 1.1.1 | MIDP-2.0 | - | 14 | 16 | 600 | 597976756 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/monster jam.jar | 190 | Monster Jam | - | 1.0.10 | MIDP-2.0 | - | 15 | 16 | 600 | 3450616 | 0 | java/lang/ArrayIndexOutOfBoundsE | 运行正常 |  |
| vault/montgolfieres-se-176x220-en.jar | 227 | Montgolfières en Folie by hu | - | 1.0.0 | MIDP-2.0 | - | 9 | 16 | 600 | 1891534 | 0 | java/io/EOFException,java/lang/N | 运行正常 |  |
| vault/mortal komb.jar | 413 | Mortal Kombat II | - | 0.1 | MIDP-2.0 | - | 8 | 16 | 600 | 1312891809 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/moto gp 09.jar | 754 | MotoGP09 | - | 1.0.8 | MIDP-2.0 | - | 4 | 16 | 600 | 77750628 | 0 | java/lang/ClassCastException,jav | 运行正常 |  |
| vault/mradio.jar | 38 | Mocha Radio | - | 1.1.0 | MIDP-2.0 | - | 11 | 16 | 600 | 162717 | 0 | java/io/IOException | 运行正常 |  |
| vault/my_life_in_new_york_258886.jar | 2235 | My Life in New York | - | 1.1.0 | MIDP-2.0 | - | 15 | 16 | 600 | 86799025 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/narnia adventure-176x220.jar | 387 | Narnia Adventure | - | 1.0.1 | MIDP-1.0 | - | 43 | 16 | 0 | 0 | 0 | - | 非可用包 | not a usable MIDlet suite (bad JAR or no MID |
| vault/naruto.jar | 171 | Naruto Fighter (RoS) | - | 1.0 | MIDP-1.0 | nokia-ui | 21 | 16 | 600 | 6437340 | 0 | java/io/EOFException,java/io/IOE | 运行正常 |  |
| vault/nate adams.jar | 148 | NateAdamsFMX [CZ] | - | 1.5.0 | MIDP-2.0 | - | 52 | 16 | 600 | 1796531 | 0 | java/lang/ClassCastException,jav | 运行正常 |  |
| vault/naughty_chat.jar | 154 | Naughty chat | - | 1.0 | MIDP-1.0 | nokia-ui | 13 | 16 | 600 | 1600189 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/need_for_speed_hot_p_259337.jar | 1022 | Need for Speed Hot Pursuit B | - | 4.16.46 | MIDP-2.0 | - | 42 | 16 | 600 | 10846363 | 0 | java/io/IOException,java/lang/Nu | 运行正常 |  |
| vault/nokia solitare 128x160.jar | 88 | Solitaire | - | 2.1 | MIDP-2.0 | nokia-ui | 6 | 16 | 600 | 1180440 | 0 | java/io/IOException,java/lang/Nu | 运行正常 |  |
| vault/nowhere-360x640-touch-namobilu.com.jar | 275 | Nowhere | - | 02.00.00 | MIDP-2.0 | - | 11 | 16 | 600 | 4611840 | 0 | - | 运行正常 |  |
| vault/nowhere_360x640_173649.jar | 275 | Nowhere | - | 02.00.00 | MIDP-2.0 | nokia-ui | 11 | 16 | 600 | 4626033 | 0 | - | 运行正常 |  |
| vault/nuclear col.jar | 427 | NuclearCollapse | - | 1.0.2 | MIDP-2.0 | - | 5 | 16 | 600 | 1786851 | 0 | javax/microedition/rms/InvalidRe | 运行正常 |  |
| vault/offroad dirt motocross.jar | 537 | Off-Road Dirt Motocross | - | 1.0.5 | MIDP-2.0 | - | 10 | 16 | 600 | 2490396476 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/oktoberfest.unlimited.fun.240x320.jar | 581 | Oktoberfest - Unlimited Fun  | - | 1.0.4 | MIDP-2.0 | nokia-ui | 6 | 16 | 600 | 4473442485 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/openseason.jar | 296 | Open Season by k700i.wu.cz | - | 1.1.3 | MIDP-2.0 | - | 6 | 16 | 600 | 3234688997 | 0 | javax/microedition/media/MediaEx | 运行正常 |  |
| vault/opera.jar | 96 | Opera Mini | - | 2.0 | MIDP-2.0 | - | 17 | 16 | 600 | 1033853 | 0 | java/io/IOException,java/io/UTFD | 运行正常 |  |
| vault/pacman_149998.jar | 25 | Pac Man | - | 1.2 | MIDP-2.0 | - | 10 | 16 | 600 | 12036 | 0 | javax/microedition/rms/RecordSto | 运行正常 | runs without input; the generic key script m |
| vault/plants vs zombies (symbian).jar | 1746 | Plants vs Zombies | - | 4.3.26 | MIDP-2.0 | - | 99 | 16 | 600 | 6619525 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/playman_extreme_runn_260866.jar | 215 | Playman Extreme Running | - | 1.1.2 | MIDP-2.0 | nokia-ui | 8 | 16 | 600 | 16528428 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/pro-moto-racing-176x220.jar | 538 | PRO MOTO RACING | - | 1.2.0 | MIDP-2.0 | - | 8 | 16 | 600 | 22957408 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/projectgot_ooumypd9.jar | 889 | PGR | - | 1.0.4 | MIDP-1.0 | m3g | 46 | 16 | 600 | 9181587 | 0 | java/io/IOException,java/lang/Il | 运行正常 |  |
| vault/pvplayer304new.jar | 352 | - | - | - | - | - | 0 | 16 | 0 | 0 | 0 | - | 非可用包 | not a zip file |
| vault/radikal_snowboard_240x400_195557.jar | 425 | Radikal Snowboard | - | 1.0.5 | MIDP-2.0 | nokia-ui | 18 | 16 | 600 | 17052949 | 0 | java/lang/ArrayIndexOutOfBoundsE | 运行正常 |  |
| vault/ragnarok_archer.jar | 158 | Ragnarok Archer | - | 1.0 | MIDP-2.0 | nokia-ui | 12 | 16 | 400 | 746621 | 0 | javax/microedition/rms/InvalidRe | 运行正常 | exited itself (notifyDestroyed) |
| vault/rayman3.jar | 119 | Rayman3 | - | 1.0.1 | MIDP-2.0 | - | 5 | 16 | 600 | 5902921527 | 0 | - | 运行正常 |  |
| vault/rayman3_1.0.2.jar | 122 | Rayman3 | - | 1.0.2 | MIDP-1.0 | nokia-ui | 5 | 16 | 600 | 1111479 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/rayman_3_241466.jar | 172 | Rayman3 | - | 1.1.8 | MIDP-2.0 | - | 5 | 16 | 600 | 3820499961 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/raymanbowling.jar | 79 | Rayman Bowling | - | 1.1.0 | MIDP-1.0 | - | 16 | 16 | 600 | 154573 | 0 | - | 运行正常 |  |
| vault/real_1_soccer_2005_ifp63zdu.jar | 89 | RealSoccer2005 | - | 1.1.0 | MIDP-1.0 | nokia-ui | 13 | 16 | 600 | 3001593 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/residentev_4hfni67z.jar | 900 | Resident Evil - The Missions | - | 1.0.0 | MIDP-2.0 | m3g | 9 | 16 | 600 | 881456 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/residentev_6pju7m54.jar | 262 | Resident Evil - The Missions | - | 1.0.0 | MIDP-2.0 | - | 3 | 16 | 600 | 1733424 | 0 | java/lang/ArrayIndexOutOfBoundsE | 运行正常 |  |
| vault/residentev_ksnzt715.jar | 880 | RE-AfterLife-Waryam | - | 1.3.98 | MIDP-1.0 | - | 95 | 16 | 600 | 3204805 | 0 | - | 运行正常 |  |
| vault/rollercoaster_1.jar | 930 | Roller Rev 99 | - | 1.1.9 | MIDP-2.0 | nokia-ui | 45 | 16 | 601 | 71108301 | 0 | java/io/EOFException,java/lang/O | 运行正常 |  |
| vault/rollercoaster_2.jar | 273 | 3D Coaster Rush by street_ra | - | 1.0.0 | MIDP-2.0 | m3g,nokia-ui | 19 | 16 | 600 | 14448748 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/rotate__ibor_.jar | 132 | Rotate It | - | 1.1.3 | MIDP-1.0 | nokia-ui | 3 | 16 | 600 | 229957583 | 0 | java/io/EOFException | 运行正常 |  |
| vault/rsb.jar | 22 | RSBgames | - | 1.0 | MIDP-1.0 | - | 7 | 16 | 969 | 1185577 | 0 | - | 运行正常 |  |
| vault/scoobydoo.jar | 82 | Scooby-Doo 2 Dark Dungeons | - | 1.0.0 | MIDP-1.0 | nokia-ui | 9 | 16 | 600 | 631598 | 0 | - | 运行正常 |  |
| vault/seadrive.jar | 77 | SeaDrive | - | 1.0 | MIDP-1.0 | nokia-ui | 7 | 16 | 600 | 151231 | 0 | - | 运行正常 |  |
| vault/sekhmet_spanish.jar | 108 | Sekhmet | - | 2.0.5 | MIDP-1.0 | nokia-ui | 14 | 16 | 600 | 11347963 | 0 | - | 运行正常 |  |
| vault/sensible_soccer.jar | 93 | Sensible Soccer | - | 1.06 | MIDP-1.0 | - | 30 | 16 | 600 | 1562568 | 0 | - | 运行正常 |  |
| vault/shadow_zone_chernobyl__the_nuclear_apocalypse240320.jar | 278 | ShadowZoneCNA | - | 1.0.09 | MIDP-2.0 | - | 8 | 16 | 600 | 2533221 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/shadowwalk_lGB8Oa7P.jar | 461 | Shadowalker | - | 1.0.6 | MIDP-1.0 | nokia-ui | 12 | 16 | 600 | 11137330 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/shiverõ.jar | 314 | Shiver 3D | - | 1.0 | MIDP-2.0 | nokia-ui | 8 | 16 | 600 | 158722770 | 0 | java/io/IOException,java/lang/Ar | 运行正常 |  |
| vault/shooter.jar | 298 | Shooter supplied by negroher | - | 1.0.7 | MIDP-2.0 | - | 23 | 16 | 600 | 17485994 | 0 | java/lang/ArrayIndexOutOfBoundsE | 运行正常 |  |
| vault/siberianstrike.jar | 63 | SiberianStrike | - | 1.0.6 | MIDP-1.0 | nokia-ui | 16 | 16 | 600 | 4456005719 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/silent-hill-mobile-2-352x416-s60-namobilu.com.jar | 482 | Silent Hill Mobile 2 | - | 1.0.0 | MIDP-2.0 | - | 12 | 16 | 600 | 7940321 | 0 | java/io/IOException,java/lang/Nu | 运行正常 |  |
| vault/silent-hill-mobile-3-320x240-mot-namobilu.com.jar | 580 | Silent Hill 3 | - | 1.1.0 | MIDP-2.0 | - | 13 | 16 | 600 | 3110690 | 0 | java/lang/NumberFormatException, | 运行正常 |  |
| vault/silent-hill-orphan-352x416-s60-namobilu.com.jar | 487 | Silent Hill | - | 0.1.0 | MIDP-2.0 | - | 10 | 16 | 600 | 3859596 | 0 | java/io/IOException,javax/microe | 运行正常 |  |
| vault/sk_8.jar | 30 | SK8 | - | 1.0.2 | MIDP-1.0 | nokia-ui | 10 | 16 | 470 | 97775 | 0 | - | 运行正常 | exited itself (notifyDestroyed) |
| vault/skoki3ds60v2.jar | 118 | Skoki3D | - | 1.0 | MIDP-1.0 | nokia-ui | 15 | 16 | 600 | 602929 | 0 | - | 运行正常 |  |
| vault/sky warryor.jar | 126 | Sky Warrior By BerON | - | 1.0 | MIDP-2.0 | - | 5 | 16 | 600 | 3835256 | 0 | - | 运行正常 |  |
| vault/snake_2.jar | 42 | Snake EX2 | - | 1.19 | MIDP-1.0 | nokia-ui | 10 | 16 | 600 | 107938 | 0 | java/lang/ClassNotFoundException | 运行正常 | runs without input; the generic key script m |
| vault/snake_3.jar | 260 | Snake III | - | 3.07.2 | MIDP-2.0 | m3g | 49 | 16 | 600 | 133916 | 0 | java/util/EmptyStackException,ja | 运行正常 | runs without input; the generic key script m |
| vault/snowboardh_zmu8yadv.jar | 795 | Snowboard Hero | - | 1.0.1 | MIDP-2.0 | mascot | 133 | 16 | 600 | 86872175 | 0 | java/io/IOException,java/lang/Nu | 运行正常 |  |
| vault/snowboardr_m6km8xw5.jar | 374 | Pro Snowboard | - | 1.02 | MIDP-2.0 | m3g,nokia-ui | 26 | 16 | 600 | 2497309 | 0 | java/io/EOFException,javax/micro | 运行正常 |  |
| vault/solitaire_b1q8jwpn.jar | 86 | Solitaire7650 | - | 1.0.6 | MIDP-1.0 | nokia-ui | 3 | 16 | 600 | 4222514 | 0 | - | 运行正常 |  |
| vault/sonic_advance_259061.jar | 1634 | Sonic Advance | - | 1.3.5 | MIDP-2.0 | - | 177 | 16 | 600 | 3597509 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/sonic_amp_sega_all_261109.jar | 1069 | Sonic Racing | - | 1.2.8 | MIDP-2.0 | - | 30 | 16 | 600 | 43168734 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/sonic_jump_2_240x320_164217.jar | 334 | Sonic Jump 2 supplied by lua | - | 1.0.4 | MIDP-1.0 | - | 7 | 16 | 600 | 2705134 | 0 | java/io/IOException,javax/microe | 运行正常 |  |
| vault/sonicforno_ja9diknl.jar | 509 | SonicJump | - | 1.0 | MIDP-1.0 | nokia-ui | 7 | 16 | 603 | 1600177 | 0 | java/io/IOException,java/lang/Nu | 运行正常 |  |
| vault/sonicthehedgehog2-crash_de_en_es_fr_it_pt_SonyEricssonK800i.jar | 718 | Sonic the Hedgehog 2 Crash | - | 1.1.1 | MIDP-2.0 | nokia-ui | 34 | 16 | 600 | 385040 | 0 | javax/microedition/rms/InvalidRe | 运行正常 |  |
| vault/soulofdark_7xlflnqy.jar | 873 | Soul of darkness 2 | - | 1.0 | MIDP-1.0 | nokia-ui | 10 | 16 | 600 | 3445248517 | 0 | - | 运行正常 |  |
| vault/southpark10thegameCZ.jar | 239 | South Park 10 | - | 1.0.12 | MIDP-2.0 | nokia-ui | 4 | 16 | 600 | 500759107 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/soviet-spy-3-k750-176x220.jar | 182 | Soviet Spy3 | - | 1.1.0 | MIDP-2.0 | - | 3 | 16 | 600 | 71494367 | 0 | java/lang/IllegalArgumentExcepti | 运行正常 |  |
| vault/special_crime_unit_cz.jar | 287 | Special Crime Unit | - | 1.0.3 | MIDP-2.0 | - | 5 | 16 | 600 | 6279982293 | 0 | - | 运行正常 |  |
| vault/spider-man_toxic_ci_258680.jar | 930 | Spider-Man: Toxic City | - | 1.0.5 | MIDP-2.0 | - | 12 | 16 | 600 | 137100 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/spidersolitaire.jar | 53 | Spider Solitaire | - | 1.0 | MIDP-1.0 | nokia-ui | 12 | 16 | 600 | 40989 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/splinter_cell_1_mobile_.jar | 159 | Splinter Cell | - | 1.1.8 | MIDP-2.0 | - | 8 | 16 | 600 | 3195628621 | 0 | - | 运行正常 |  |
| vault/splinter_cell_2_exten_240x320_157266.jar | 136 | SC-EO sefan.ru | - | 1.0.2 | MIDP-2.0 | - | 4 | 16 | 600 | 4018521337 | 0 | - | 运行正常 |  |
| vault/splinter_cell_3.jar | 318 | Splinter Cell DA | - | 1.0.5 | MIDP-2.0 | - | 6 | 16 | 600 | 3911300240 | 0 | - | 运行正常 |  |
| vault/splinter_cell_4_tom_clancys_.jar | 228 | SC-PT | - | 1.0.6 | MIDP-2.0 | nokia-ui | 14 | 16 | 600 | 1734065112 | 0 | javax/microedition/rms/InvalidRe | 运行正常 |  |
| vault/splinter_cell_extended_ops.jar | 146 | Splinter Cell - Extended Ops | - | 1.3.9 | MIDP-1.0 | nokia-ui | 4 | 16 | 600 | 3755194839 | 0 | - | 运行正常 |  |
| vault/splintercell.jar | 63 | Splinter Cell | - | 1.1.8 | MIDP-1.0 | nokia-ui | 13 | 16 | 600 | 4315242745 | 0 | - | 运行正常 |  |
| vault/spongebob_bowling.jar | 120 | SpongeBob Bowling | - | 1.0.9 | MIDP-1.0 | - | 24 | 16 | 600 | 137400 | 0 | java/lang/NullPointerException,j | 运行正常 | runs without input; the generic key script m |
| vault/spotreba.jar | 25 | Spotreba | - | 1.0.0 | MIDP-1.0 | - | 19 | 16 | 600 | 2029 | 0 | java/lang/NumberFormatException | 运行正常 |  |
| vault/spyro_k7927_nguy_156863.jar | 465 | Spyro Dawn of the Dragon | - | 0.5.7 | MIDP-2.0 | - | 8 | 16 | 600 | 2147471 | 0 | java/io/IOException,javax/microe | 运行正常 |  |
| vault/stalker-176x220-k750.jar | 343 | Stalker | - | 1.1.0 | MIDP-2.0 | mascot | 42 | 16 | 600 | 722835151 | 0 | java/lang/ArrayIndexOutOfBoundsE | 运行正常 |  |
| vault/stalknshoot.jar | 62 | Stalk N Shoot | - | 1.40.25 | MIDP-1.0 | nokia-ui | 3 | 16 | 600 | 2037105493 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/startrek_nemesis.jar | 90 | Star Trek Nemesis | - | 1.0.3 | MIDP-1.0 | nokia-ui | 7 | 16 | 600 | 2675777 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/stayalive_lzrustoe.jar | 482 | StayAlive SEfan.RU | - | 03.30.01 | MIDP-2.0 | - | 9 | 16 | 600 | 868762 | 0 | javax/microedition/rms/InvalidRe | 运行正常 |  |
| vault/stranded_157079_3963.jar | 233 | Stranded | - | 1.0.0 | MIDP-1.0 | - | 9 | 16 | 600 | 6879359 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/striptbricks_with_natacha.jar | 197 | Striptbricks with Natacha | - | 1.1 | MIDP-2.0 | - | 8 | 16 | 600 | 852962 | 0 | javax/microedition/rms/InvalidRe | 运行正常 |  |
| vault/stunt racing.jar | 401 | StuntRacing 99 | - | 1.0.25 | MIDP-2.0 | - | 22 | 16 | 600 | 33623574 | 0 | java/io/EOFException,javax/micro | 运行正常 |  |
| vault/subway_by_oidi500.jar | 81 | Subway | - | 1.6 | MIDP-1.0 | nokia-ui | 3 | 16 | 600 | 1668266 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/summer games-176x220.jar | 248 | Playman Summer Games 3 | - | 1.0.21 | MIDP-2.0 | nokia-ui | 6 | 16 | 600 | 596012684 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/supreme com.jar | 162 | Supreme Commando | - | 1.1.0 | MIDP-2.0 | - | 11 | 16 | 600 | 400315 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/swarm.jar | 34 | Swarm | - | 1.0 | MIDP-1.0 | - | 4 | 16 | 600 | 780471 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/tcpmp.smartphone.0.72RC1.jar | 685 | - | - | - | - | - | 0 | 16 | 0 | 0 | 0 | - | 非可用包 | not a zip file |
| vault/tempest.jar | 61 | Tempest | - | 1.1.2 | MIDP-2.0 | - | 26 | 16 | 600 | 154848 | 0 | java/lang/ArrayStoreException | 运行正常 |  |
| vault/terrors-seed-352x416-namobilu.com.jar | 394 | TerrorsSeed yMoBi.ru | - | 1.0.1 | MIDP-2.0 | - | 20 | 16 | 600 | 155417147 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/tetris.jar | 130 | Tetris | - | 2.2.9 | MIDP-1.0 | nokia-ui | 46 | 16 | 600 | 252883 | 0 | java/util/NoSuchElementException | 运行正常 |  |
| vault/the_phantom_240x320_158584.jar | 113 | The Phantom | - | 1.1.3 | MIDP-2.0 | - | 4 | 16 | 600 | 199053711 | 0 | - | 运行正常 |  |
| vault/the_sims_2.jar | 296 | The Sims 2 Mobile | - | 1.2.0 | MIDP-1.0 | nokia-ui | 26 | 16 | 600 | 3757222785 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/the_sims_3_ambition_260250.jar | 837 | The Sims 3 Ambitions | - | 4.5.81 | MIDP-2.0 | - | 28 | 16 | 600 | 11698629 | 0 | java/io/EOFException,java/lang/N | 运行正常 |  |
| vault/the_sims_3_winter_ed_259820.jar | 332 | The Sims 3 Winter Edition | - | 0.2.81 | MIDP-2.0 | - | 28 | 16 | 600 | 10383245 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/the_witcher_159073_4136.jar | 261 | The Witcher | - | 1.0.0 | MIDP-2.0 | - | 7 | 16 | 600 | 6191615 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/the_wolfman_253792.jar | 624 | The Wolfman by Namco | - | 1.0.0 | MIDP-2.0 | - | 6 | 16 | 600 | 13937096 | 0 | - | 运行正常 |  |
| vault/thefastand_qldun6af.jar | 321 | FF Fugitive 2D | - | 0.9.1 | MIDP-2.0 | - | 9 | 16 | 600 | 6157782 | 0 | java/lang/IllegalArgumentExcepti | 运行正常 |  |
| vault/themute_spanish.jar | 61 | The Mute | - | 1.5.0 | MIDP-1.0 | nokia-ui | 10 | 16 | 600 | 142452 | 0 | - | 运行正常 |  |
| vault/thesims2_49upin1x.jar | 299 | SIMS 2 Mobile | - | 1.0.5 | MIDP-1.0 | nokia-ui | 18 | 16 | 600 | 3625668685 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/thesims2cz.jar | 254 | The Sims 2 Mobile CZ by Ecko | - | 1.0.5 | MIDP-1.0 | nokia-ui | 18 | 16 | 600 | 3731621595 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/thesims2pets176208.jar | 387 | The Sims 2 Pets | - | 4.4.80 | MIDP-2.0 | - | 36 | 16 | 600 | 7646006 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/tong-yuan-240x320-namobilu.com.jar | 523 | TongYuan By BerON | - | 1.0 | MIDP-2.0 | - | 10 | 16 | 600 | 1473601 | 0 | - | 运行正常 |  |
| vault/tooth_bride_320x240_189157.jar | 213 | Tooth Bride | - | 1.0 | MIDP-2.0 | - | 6 | 16 | 600 | 1096244 | 0 | - | 运行正常 |  |
| vault/topgun.jar | 110 | TopGun | - | 1.5.2 | MIDP-1.0 | - | 9 | 16 | 600 | 135590 | 0 | java/lang/NumberFormatException, | 运行正常 |  |
| vault/tornado_mania_360x640_187050.jar | 382 | Tornado Mania! | - | 1.7.40 | MIDP-2.0 | - | 10 | 16 | 600 | 155223 | 0 | java/io/EOFException,javax/micro | 运行正常 |  |
| vault/total over.jar | 172 | TotalOverdose | - | 1.1 | MIDP-2.0 | - | 10 | 16 | 600 | 2881916 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/totally_spies_the_m_240159.jar | 285 | Totally Spies **MASYAKA** | - | 1.1.7 | MIDP-2.0 | - | 5 | 16 | 600 | 2879617662 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/tover blox.jar | 221 | Tower Bloxx(TM) | - | 1.5.07 | MIDP-2.0 | m3g,nokia-ui | 33 | 16 | 600 | 142875 | 0 | - | 运行正常 |  |
| vault/towniek750i.jar | 190 | Townsmen3 | - | 1.0 | MIDP-1.0 | - | 5 | 16 | 600 | 622592 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/track_furious_sek700.ic.cz.jar | 152 | TF | - | 1.0 | MIDP-1.0 | nokia-ui | 5 | 16 | 0 | 0 | 0 | - | 超时 |  |
| vault/trailbiker.jar | 44 | Trail Biker | - | 1.0.0 | MIDP-1.0 | nokia-ui | 12 | 16 | 600 | 46680 | 0 | java/lang/NullPointerException,j | 运行正常 | runs without input; the generic key script m |
| vault/treasure_towers_240x320-113258.jar | 307 | Treasure Towers | - | 1.4.4 | MIDP-2.0 | mascot | 5 | 16 | 600 | 5717241 | 0 | java/lang/IllegalArgumentExcepti | 运行正常 |  |
| vault/trezor11.jar | 35 | Trezor | - | 1.1 | MIDP-1.0 | - | 36 | 16 | 600 | 147420 | 0 | - | 运行正常 | painted 604 frames but made no draw calls (b |
| vault/tron_snowboarding_240x320_192401.jar | 922 | TRON-mod by SL280296 | - | 1.0.1 | MIDP-2.0 | m3g | 135 | 16 | 600 | 68906346 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/ultimate_brick_break_260584.jar | 399 | Ultimate Brick Breaker | - | 1.0.0 | MIDP-2.0 | - | 31 | 16 | 600 | 74766497 | 0 | java/lang/ClassCastException,jav | 运行正常 |  |
| vault/ultimate_brick_break_260678.jar | 404 | Ultimate Brick Breaker 2 | - | 1.1.3 | MIDP-2.0 | - | 31 | 16 | 600 | 75376767 | 0 | java/lang/ClassCastException,jav | 运行正常 |  |
| vault/uno_gameloft.jar | 816 | UNO | - | 1.0.3 | MIDP-2.0 | nokia-ui | 14 | 16 | 600 | 352890189 | 0 | - | 运行正常 | runs without input; the generic key script d |
| vault/vNes.jar | 80 | vNes | - | 1.2.0 | MIDP-1.0 | - | 4 | 16 | 600 | 541095352 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/vampires-dawnk750.jar | 583 | VampiresDawn | - | 1.19.2 | MIDP-2.0 | - | 14 | 16 | 600 | 27475 | 0 | java/lang/NullPointerException | 运行正常 | runs without input; the generic key script m |
| vault/vampiresas_46oqnqm4.jar | 562 | Vampire's Assistant | - | 0.0.58 | MIDP-2.0 | - | 72 | 16 | 600 | 7901818 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/vbirds.jar | 60 | VBirds | - | 0.0.1 | MIDP-1.0 | nokia-ui | 16 | 16 | 600 | 129354 | 0 | java/io/IOException,javax/microe | 运行正常 |  |
| vault/vedecka kalkulacka.jar | 81 | xcalc | - | 1.1.06 | MIDP-2.0 | - | 13 | 16 | 600 | 792522 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/vers12527.jar | 132 | PornManager2 | - | 1.0 | MIDP-2.0 | - | 15 | 16 | 600 | 20098 | 0 | java/io/IOException,javax/microe | 运行正常 | painted 600 frames but made no draw calls (b |
| vault/virus-240x320-namobilu.com.jar | 157 | Virus | - | 1.0.0 | MIDP-2.0 | nokia-ui | 33 | 16 | 600 | 713620 | 0 | java/io/EOFException,java/lang/A | 运行正常 |  |
| vault/wolf-moon-240x320-namobilu.com.jar | 217 | WolfMoon | - | 1.06 | MIDP-1.0 | - | 17 | 16 | 600 | 1709957722 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/xiii_240x320_169505.jar | 175 | XIII | - | 1.0.0 | MIDP-2.0 | - | 4 | 16 | 600 | 2826504344 | 0 | - | 运行正常 |  |
| vault/zombie_infection_259648.jar | 615 | Zombie Infection | - | 1.0.1 | MIDP-2.0 | - | 8 | 16 | 600 | 1674810208 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/zombie_infection_2_240x320_146117.jar | 1068 | Zombie Infection 2 | - | 1.2.3 | MIDP-2.0 | - | 12 | 16 | 600 | 6332668034 | 0 | java/lang/ArrayIndexOutOfBoundsE | 运行正常 |  |
| vnes.jar | 148 | vNes | - | 1.0.0 | - | nokia-ui | 67 | 16 | 600 | 1092674 | 0 | java/io/IOException,java/lang/Nu | 运行正常 | painted 602 frames but made no draw calls (b |
| 七龙珠－终极原始大乱斗_5589.jar | 986 | 七龙珠-终极原始大乱斗 | - | 1.0.0 | MIDP-1.0 | - | 26 | 16 | 600 | 1110423 | 0 | - | 运行正常 |  |
| 三国争霸之赤壁大战_11681.jar | 233 | 三国争霸之赤壁大战 | - | 1.0.0 | MIDP-1.0 | - | 11 | 16 | 600 | 1572481 | 0 | - | 运行正常 |  |
| 三国塔防－曹操传_1610.jar | 1066 | 三国塔防-曹操传 | - | 2.0.1 | MIDP-1.0 | nokia-ui | 148 | 16 | 600 | 167975001 | 0 | java/lang/ArrayIndexOutOfBoundsE | 运行正常 |  |
| 三国志_10617.jar | 213 | C2S－三国志 | - | 1.0 | MIDP-1.0 | - | 2 | 16 | 600 | 4191995597 | 0 | - | 运行正常 |  |
| 三国志吞食天地_8144.jar | 513 | 三国志：吞食天地 | - | 1.0.2 | MIDP-2.0 | - | 4 | 16 | 600 | 308890 | 0 | java/lang/NullPointerException | 运行正常 | runs without input; the generic key script m |
| 三国志霸王的大陆V3.2.2_5005.jar | 674 | BWDL | - | 1.0 | MIDP-2.0 | - | 7 | 16 | 600 | 543736 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| 三国杀2012_809.jar | 1203 | sanguosha | - | 1.0 | MIDP-1.0 | nokia-ui | 81 | 16 | 600 | 2244480 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| 三国杀2．2美化版_1347.jar | 1171 | SanGuoQ | - | 2.2 | MIDP-2.0 | - | 376 | 16 | 600 | 5713751 | 0 | java/io/IOException | 运行正常 |  |
| 三国杀单机BT版_4227.jar | 318 | 三国杀 | - | 1.0.0 | MIDP-2.0 | - | 17 | 16 | 600 | 598295 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| 三国杀单机战功修改版_4143.jar | 894 | SGSJB | - | 1.0.0 | MIDP-2.0 | - | 17 | 16 | 600 | 8343720 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| 三国杀单机版_4312.jar | 895 | SGSJB | - | 1.0.0 | MIDP-2.0 | - | 17 | 16 | 600 | 8343720 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| 三国群英传_11539.jar | 58 | 奥汀-三国群英传正版 | - | 1.0 | MIDP-1.0 | nokia-ui | 9 | 16 | 600 | 577648 | 0 | javax/microedition/rms/InvalidRe | 运行正常 |  |
| 三国群雄争霸_9023.jar | 271 | 超值-三国群雄争霸 | - | 1.0 | MIDP-1.0 | nokia-ui | 3 | 16 | 600 | 135065 | 0 | - | 运行正常 |  |
| 三国赤壁小乔秘史_7976.jar | 643 | 小乔秘史wap.91soyo.com破解 | - | 1.0.0 | MIDP-2.0 | nokia-ui | 24 | 16 | 600 | 3505835874 | 0 | java/lang/NullPointerException | 运行正常 | runs without input; the generic key script t |
| 世嘉头文字D－宿命对决_1247.jar | 1008 | 世嘉头文字D-宿命对决 | - | 1.0.0 | MIDP-1.0 | m3g | 159 | 16 | 600 | 444229 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| 世界五子棋大奖赛_355.jar | 426 | 世界五子棋大奖赛 | - | 1.0 | MIDP-1.0 | - | 9 | 16 | 600 | 59103 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| 世界冠军拉力赛3D_8360.jar | 635 | WRC 3D | - | 0.8.4 | MIDP-2.0 | m3g | 4 | 16 | 600 | 8743405 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| 世界最强-银星围棋_2631.jar | 402 | wjzq | - | 1.0 | MIDP-1.0 | - | 24 | 16 | 600 | 2742537 | 0 | java/lang/NullPointerException | 运行正常 |  |
| 世界武术巡回赛教学版_3484.jar | 442 | 世界武术巡回赛-教学版 | - | 0.3.3 | MIDP-2.0 | - | 8 | 16 | 600 | 3736236763 | 0 | java/lang/ArrayIndexOutOfBoundsE | 运行正常 |  |
| 世界足球2012中文版_1724.jar | 881 | 世界足球2012 | - | 1.0.0 | MIDP-2.0 | - | 16 | 16 | 600 | 41233303 | 0 | java/lang/NullPointerException | 运行正常 |  |
| 世界足球2012中文版_780.jar | 881 | 世界足球2012 | - | 1.0.0 | MIDP-2.0 | - | 16 | 16 | 600 | 41283038 | 0 | java/lang/NullPointerException | 运行正常 |  |
| 丧尸危机2-自由之翼BT版_997.jar | 772 | 丧尸危机2试玩版 | - | 1.0 | MIDP-2.0 | - | 124 | 16 | 600 | 560462 | 0 | - | 运行正常 |  |
| 丧尸围城_10590.jar | 289 | Dead Rising | - | 1.0.1 | MIDP-2.0 | - | 43 | 16 | 600 | 2536708 | 0 | java/lang/IllegalArgumentExcepti | 运行正常 |  |
| 中华麻将馆－极限胡牌版_244.jar | 310 | 中华麻将馆-极限胡牌版 | - | 1.0 | MIDP-1.0 | - | 18 | 16 | 600 | 3362919507 | 0 | java/io/IOException,java/lang/Nu | 运行正常 |  |
| 中国象棋_10893.jar | 293 | 中国象棋三国经典版 | - | 1.0 | MIDP-2.0 | - | 9 | 16 | 600 | 918640 | 0 | javax/microedition/rms/InvalidRe | 运行正常 |  |
| 中国象棋之水墨珍藏版_871.jar | 375 | 中国象棋之水墨珍藏版 | - | 1.0.0 | MIDP-2.0 | - | 12 | 16 | 624 | 3809606484 | 0 | java/lang/ArrayIndexOutOfBoundsE | 运行正常 |  |
| 乱世Q三国_1649.jar | 977 | 乱世Q三国 | - | 1.0 | MIDP-1.0 | - | 232 | 16 | 0 | 128 | 0 | javax/microedition/rms/RecordSto | 未呈现画面 | exited itself (notifyDestroyed) without pain |
| 乱舞三国之群英会_2972.jar | 817 | 乱舞三国之群英会 | - | 1.0 | MIDP-1.0 | - | 35 | 16 | 600 | 251952589 | 0 | java/io/IOException,java/lang/Nu | 运行正常 |  |
| 五子棋_10940.jar | 42 | XOXO | - | 1.0 | MIDP-1.0 | - | 24 | 16 | 600 | 523710 | 0 | - | 运行正常 |  |
| 仙剑奇侠传(2008)_12406.jar | 695 | pal | - | 1.0 | MIDP-1.0 | nokia-ui | 3 | 16 | 600 | 201979 | 0 | - | 运行正常 |  |
| 仙剑奇侠传_10388.jar | 297 | 仙剑奇侠传 | - | 0.0.1 | MIDP-2.0 | - | 6 | 16 | 600 | 11468181 | 0 | javax/microedition/media/MediaEx | 运行正常 |  |
| 仙剑奇侠传完美版_11857.jar | 360 | 仙剑奇侠传 | - | 06.03.10 | MIDP-1.0 | nokia-ui | 4 | 16 | 600 | 4025277 | 0 | - | 运行正常 |  |
| 仙逆－诛仙弑神_1441.jar | 1356 | 仙逆-诛仙弑神 | - | 1.0.0 | MIDP-1.0 | - | 48 | 16 | 600 | 4866156523 | 0 | java/io/IOException,java/lang/Cl | 运行正常 |  |
| 仙魔飞行棋_417.jar | 294 | 仙魔飞行棋 | - | 1.0 | MIDP-2.0 | - | 21 | 16 | 600 | 2531840 | 0 | java/lang/ArrayIndexOutOfBoundsE | 运行正常 |  |
| 众神殿之王子归来_8404.jar | 103 | 王子归来 | - | 1.0.0 | MIDP-2.0 | nokia-ui | 14 | 16 | 600 | 1031707 | 0 | - | 运行正常 |  |
| 使命召唤4现代战争_9388.jar | 449 | Call of Duty 4 by serviak | - | 1.0.4 | MIDP-2.0 | - | 5 | 16 | 600 | 9933288 | 0 | javax/microedition/rms/InvalidRe | 运行正常 |  |
| 使命召唤5-战争世界中文版_4583.jar | 416 | 使命召唤5-战争世界 | - | 1.0.1 | MIDP-2.0 | - | 22 | 16 | 600 | 1250125289 | 0 | java/lang/IllegalArgumentExcepti | 运行正常 |  |
| 侠盗飞车_6246.jar | 360 | 侠盗飞车-Gameloft正版 | - | 1.0.0 | MIDP-2.0 | - | 6 | 16 | 600 | 4380167311 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| 侠盗飞车加强版_5640.jar | 350 | 侠盗飞车-Gameloft正版 | - | 1.00 | MIDP-2.0 | - | 11 | 16 | 600 | 3987995869 | 0 | javax/microedition/rms/InvalidRe | 运行正常 |  |
| 俄罗斯方块中文版_2832.jar | 121 | 俄罗斯方块 | - | 1.0.0 | MIDP-2.0 | - | 56 | 16 | 600 | 806052 | 0 | java/io/IOException,java/util/No | 运行正常 |  |
| 倚天屠龙之君临天下_12230.jar | 564 | 倚天屠龙之君临天下 | - | 1.0 | MIDP-2.0 | - | 64 | 16 | 600 | 2649695 | 0 | java/lang/NumberFormatException, | 运行正常 |  |
| 倚天屠龙记_4827.jar | 836 | 倚天屠龙记 | - | 1.0 | MIDP-1.0 | nokia-ui | 62 | 16 | 600 | 11057413 | 0 | - | 运行正常 |  |
| 倚天屠龙记之魔教教主_4663.jar | 798 | GEYTTJZ | - | 1.0.4 | MIDP-2.0 | - | 7 | 16 | 600 | 4034690381 | 0 | - | 运行正常 |  |
| 傲世三国志－群雄争霸_5633.jar | 514 | 傲世三国志-群雄争霸 | - | 1.0 | MIDP-1.0 | nokia-ui | 15 | 16 | 600 | 3452411219 | 0 | - | 运行正常 |  |
| 傲气雄鹰_10432.jar | 249 | 傲气雄鹰汉化版 | - | 1.08 | MIDP-2.0 | - | 10 | 16 | 431 | 557478 | 0 | - | 运行正常 | exited itself (notifyDestroyed) |
| 傲视三国之赵云传_11024.jar | 235 | 傲视三国之赵云传 | - | 1.0 | MIDP-1.0 | nokia-ui | 22 | 16 | 600 | 1558427 | 0 | java/lang/ArithmeticException | 运行正常 |  |
| 僵尸危机2_4349.jar | 1068 | Zombie Infection 2 | - | 1.2.3 | MIDP-2.0 | - | 12 | 16 | 600 | 6277608664 | 0 | java/lang/ArrayIndexOutOfBoundsE | 运行正常 |  |
| 兔子波比2冰原之旅汉化版_11509.jar | 68 | BC2 - Winterland | - | 1.0.0 | MIDP-1.0 | nokia-ui | 11 | 16 | 600 | 185160 | 0 | - | 运行正常 |  |
| 冠军足球经理2010汉化版_5198.jar | 543 | CM-2010 | - | 1.2.1 | MIDP-2.0 | nokia-ui | 21 | 16 | 600 | 11691419 | 0 | - | 运行正常 |  |
| 冰淇淋少女成长日记_7307.jar | 344 | 冰淇淋少女成长日记_玉品 | - | 1.0 | MIDP-2.0 | nokia-ui | 9 | 16 | 600 | 471620 | 0 | - | 运行正常 | runs without input; the generic key script m |
| 凡人修仙传2-乱星海_12474.jar | 979 | 凡人修仙传2-乱星海 | - | 1.00 | MIDP-1.0 | - | 18 | 16 | 600 | 4321367 | 0 | java/lang/NullPointerException | 运行正常 |  |
| 凡人修仙传之七派六宗_4145.jar | 984 | 凡人修仙传 | - | 1.0.0 | MIDP-1.0 | nokia-ui | 16 | 16 | 600 | 174388 | 0 | java/io/IOException,java/lang/Nu | 运行正常 |  |
| 凤凰战机_10636.jar | 216 | Ultra Phoenix - Resurgence | - | 1.1.3 | MIDP-2.0 | - | 32 | 16 | 600 | 6679060 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| 初恋物语_7612.jar | 286 | 初恋物语 | - | 1.0 | MIDP-1.0 | - | 9 | 16 | 600 | 36276 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| 刺客信条3_266.jar | 1623 | Assassin's Creed III | - | 1.1.1 | MIDP-2.0 | nokia-ui | 12 | 16 | 600 | 115471976 | 0 | java/lang/NullPointerException | 运行正常 |  |
| 刺客信条兄弟会中文版_3668.jar | 1213 | 刺客信条-兄弟会 | - | 1.0.0 | MIDP-2.0 | - | 12 | 16 | 601 | 20099777 | 0 | java/lang/NullPointerException | 运行正常 |  |
| 剑侠奇缘-玉女问情_2208.jar | 703 | 剑侠奇缘-玉女问情 | - | 1.0 | MIDP-1.0 | - | 30 | 16 | 600 | 18169298 | 0 | java/lang/NumberFormatException | 运行正常 |  |
| 剑侣情缘Ⅱ魔神至尊传(无bug)_12464.jar | 904 | 剑侣情缘Ⅱ魔神至尊传 | - | 1.0 | MIDP-1.0 | - | 20 | 16 | 600 | 2972000 | 0 | java/io/EOFException | 运行正常 |  |
| 剑灵－古剑魂梦_676.jar | 1531 | 剑灵-古剑魂梦 | - | 1.0.0 | MIDP-2.0 | nokia-ui | 50 | 16 | 600 | 736297 | 0 | - | 运行正常 |  |
| 午夜保龄球2中文版_9782.jar | 589 | 终极保龄球2 | - | 1.0.0 | MIDP-2.0 | nokia-ui | 12 | 16 | 600 | 5357819877 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| 午夜台球圣诞中文版_8677.jar | 221 | 极限台球挑战赛 | - | 1.04 | MIDP-2.0 | - | 6 | 16 | 600 | 606484002 | 0 | - | 运行正常 |  |
| 危城无双之三国群英_4891.jar | 220 | 危城无双 | - | 1.0 | MIDP-1.0 | nokia-ui | 2 | 16 | 600 | 2514972500 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| 反恐精英2正式版_5836.jar | 510 | CT episod 2 | - | 1.0 | MIDP-2.0 | nokia-ui | 21 | 16 | 600 | 7608715 | 0 | java/lang/IllegalArgumentExcepti | 运行正常 |  |
| 口袋仓鼠_11936.jar | 441 | Lab Hamster\gkataria | - | 1.0 | MIDP-2.0 | - | 23 | 16 | 470 | 1426038 | 0 | java/lang/NullPointerException,j | 运行正常 | exited itself (notifyDestroyed) |
| 口袋妖怪_7872.jar | 54 | 口袋妖怪 | - | 1.0 | MIDP-1.0 | nokia-ui | 8 | 16 | 600 | 1507513 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| 口袋妖怪水晶汉化版_4524.jar | 1067 | MeBoy | - | 1.5.0 | MIDP-2.0 | - | 7 | 16 | 600 | 812 | 0 | javax/microedition/rms/RecordSto | 运行正常 | painted 600 frames but made no draw calls (b |
| 口袋灵兽_9902.jar | 582 | 口袋灵兽 | - | 1.0 | MIDP-2.0 | nokia-ui | 41 | 16 | 600 | 231120 | 0 | - | 运行正常 |  |
| 古剑奇缘-诛仙_4913.jar | 954 | 古剑奇缘-诛仙 | - | 1.0.0 | MIDP-1.0 | - | 64 | 16 | 600 | 1054356932 | 0 | - | 运行正常 |  |
| 台球传奇_87.jar | 917 | 台球传奇 | - | 1.0 | MIDP-1.0 | - | 53 | 16 | 600 | 11174477 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| 史前部落_11059.jar | 486 | Prehistoric Tribes | - | 0.1.28 | MIDP-2.0 | - | 7 | 16 | 600 | 895907166 | 0 | - | 运行正常 |  |
| 合金弹头2011_5629.jar | 401 | 合金弹头2011 | - | 1.0 | MIDP-1.0 | - | 7 | 16 | 600 | 6337796010 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| 合金弹头2013BT版_678.jar | 1021 | 合金弹头2013(正版) | - | 1.0 | MIDP-1.0 | - | 19 | 16 | 600 | 247741 | 0 | java/lang/ClassNotFoundException | 运行正常 |  |
| 合金弹头4(完美汉化版)_4053.jar | 798 | Metal Slug 4 | - | 1.0.1 | MIDP-2.0 | - | 6 | 16 | 600 | 6069759 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| 周末的沙滩_7453.jar | 1480 | Beach Games 12-Pack | - | 1.0.0 | MIDP-2.0 | nokia-ui | 37 | 16 | 600 | 13288063 | 0 | java/io/EOFException,javax/micro | 运行正常 |  |
| 哆啦A梦-秘密道具大作战_6557.jar | 552 | 哆啦A梦-秘密道具大作战(正版) | - | 1.0 | MIDP-1.0 | nokia-ui | 26 | 16 | 600 | 7005892389 | 0 | - | 运行正常 |  |
| 哆啦A梦运动会_10939.jar | 348 | 哆啦A梦运动会(正版)W | - | 1.0.0 | MIDP-1.0 | - | 17 | 16 | 600 | 588446 | 0 | - | 运行正常 |  |
| 哆啦A梦－3D卡丁车增值版_1560.jar | 1104 | 哆啦A梦-3D卡丁车 | - | 2.01 | MIDP-1.0 | nokia-ui | 73 | 16 | 600 | 31953320 | 0 | java/io/IOException,java/lang/Nu | 运行正常 |  |
| 喜羊羊与灰太狼_9241.jar | 296 | 喜羊羊与灰太狼 | - | 1.0.0 | MIDP-2.0 | nokia-ui | 11 | 16 | 600 | 2824791350 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| 国色天香MM斗地主真人版_11364.jar | 324 | 国色天香-MM斗地主(真人版) | - | 1.0 | MIDP-1.0 | - | 6 | 16 | 600 | 592239 | 0 | javax/microedition/rms/InvalidRe | 运行正常 |  |
| 国色天香斗地主_1364.jar | 1249 | 国色天香斗地主 | - | 1.0 | MIDP-2.0 | nokia-ui | 58 | 16 | 600 | 22127502 | 0 | - | 运行正常 |  |
| 圣百合女子学园-纯色序曲_4875.jar | 993 | SaintLily | - | 1.0.0 | MIDP-2.0 | - | 12 | 16 | 600 | 539631 | 0 | - | 运行正常 |  |
| 地下城与勇士之鬼剑士_9408.jar | 381 | 地下城与勇士-鬼剑士 | - | 1.0.0 | MIDP-2.0 | - | 27 | 16 | 600 | 57405975 | 0 | - | 运行正常 |  |
| 地心探险(地心游记)_12471.jar | 94 | 新浪-地心游记 | - | 1.0 | MIDP-2.0 | - | 9 | 16 | 600 | 5389863967 | 0 | - | 运行正常 |  |
| 坦克大战（经典版）_190.jar | 570 | 3D坦克大战(经典版) | - | 1.0 | MIDP-1.0 | - | 18 | 16 | 600 | 24844431 | 0 | - | 运行正常 |  |
| 塔塔_2848.jar | 693 | 塔塔 | - | 1.0 | MIDP-1.0 | - | 6 | 16 | 600 | 185455421 | 0 | java/lang/ClassNotFoundException | 运行正常 |  |
| 夜半鬼吹灯_8363.jar | 222 | 夜半鬼吹灯 | - | 1.0 | MIDP-2.0 | nokia-ui | 5 | 16 | 600 | 69149 | 0 | - | 运行正常 |  |
| 夜店3D斗地主_2296.jar | 847 | 夜店3D斗地主 | - | 1.0.0 | MIDP-2.0 | - | 15 | 16 | 600 | 33240 | 0 | javax/microedition/rms/InvalidRe | 运行正常 |  |
| 大富翁4纵横四海_8749.jar | 273 | 大富翁4纵横四海 | - | 1.0 | MIDP-1.0 | - | 13 | 16 | 600 | 346713 | 0 | - | 运行正常 |  |
| 大富翁4－大宇正版_467.jar | 636 | 大富翁4大宇正版 | - | 1.0 | MIDP-2.0 | - | 17 | 16 | 600 | 34762835 | 0 | java/lang/NullPointerException | 运行正常 |  |
| 大富翁中国之旅_11873.jar | 97 | DFWZGZL | - | 1.0 | MIDP-2.0 | - | 10 | 16 | 600 | 225742 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| 大魔界村_11996.jar | 1234 | Ghouls N Ghosts | - | 1.1.64 | MIDP-2.0 | nokia-ui | 103 | 16 | 600 | 3314050 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| 大鱼吃小鱼－深海探险2_614.jar | 633 | 大鱼吃小鱼-深海探险2 | - | 1.0.0 | MIDP-1.0 | - | 63 | 16 | 600 | 350816 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| 太空侵略者3_archive.jar | 85 | Space Impact III | - | 1.2 | MIDP-1.0 | nokia-ui | 11 | 16 | 600 | 1596978 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| 太空冲击303_archive.jar | 85 | Space Impact III | - | 1.2 | MIDP-1.0 | nokia-ui | 11 | 16 | 600 | 1596548 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| 头文字D之疾风骤雨_2140.jar | 938 | 世嘉头文字D-疾风骤雨 | - | 1.0.0 | MIDP-2.0 | m3g | 26 | 16 | 600 | 467575 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| 奥特曼-宇宙英雄超银河传说_2233.jar | 1245 | 奥特曼-宇宙英雄超银河传说 | - | 1.0.0 | MIDP-1.0 | - | 139 | 16 | 600 | 226860 | 0 | - | 运行正常 |  |
| 女仆咖啡厅_1244.jar | 640 | 女仆咖啡厅 | - | 1.0 | MIDP-2.0 | - | 42 | 16 | 600 | 2320165 | 0 | - | 运行正常 |  |
| 女神转校生-心跳回忆_4993.jar | 908 | 女神转校生-心跳回忆 | - | 1.0 | MIDP-2.0 | - | 58 | 16 | 600 | 1702793 | 0 | java/lang/ClassNotFoundException | 运行正常 |  |
| 孢子生命起源_6515.jar | 658 | EA-孢子生命起源 | - | 1.0.0 | MIDP-2.0 | - | 42 | 16 | 600 | 3087087572 | 0 | java/io/IOException,java/lang/Nu | 运行正常 |  |
| 孤岛余生2时间之谜汉化版_7371.jar | 616 | 孤岛余生2 | - | 1.0.3 | MIDP-2.0 | m3g | 36 | 16 | 600 | 10307357 | 0 | javax/microedition/rms/InvalidRe | 运行正常 |  |
| 孤岛余生汉化版_10145.jar | 453 | Stranded | - | 1.0.1 | MIDP-1.0 | - | 9 | 16 | 600 | 2394798 | 0 | java/lang/NullPointerException | 运行正常 |  |
| 学姐的秘密_9071.jar | 121 | 学姐的秘密 | - | 1.0 | MIDP-2.0 | - | 10 | 16 | 600 | 77419 | 0 | - | 运行正常 |  |
| 宇宙战机_10521.jar | 175 | Triad | - | 1.0.0 | MIDP-2.0 | - | 19 | 16 | 600 | 3247571527 | 0 | java/io/IOException,java/lang/Cl | 运行正常 |  |
| 实况足球2008中文版_9284.jar | 567 | 世界足球2008-明星正版 | - | 1.01 | MIDP-2.0 | - | 7 | 16 | 600 | 1805077570 | 0 | - | 运行正常 |  |
| 实况足球2012_2122.jar | 905 | Real Football 2012 | - | 1.0.5 | MIDP-2.0 | - | 14 | 16 | 600 | 169304882 | 0 | java/lang/NullPointerException | 运行正常 |  |
| 实况足球经理2009_10609.jar | 652 | RF MANAGER 09 | - | 1.1.3 | MIDP-2.0 | nokia-ui | 8 | 16 | 600 | 691058475 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| 宠物之王_12445.jar | 933 | 91soyo.com-宠物之王 | - | 1.0 | MIDP-2.0 | - | 63 | 16 | 620 | 1634709 | 0 | java/lang/RuntimeException,javax | 运行正常 |  |
| 宠物狗狗中文版_11635.jar | 253 | 无敌Z狗狗 | - | 1.02 | MIDP-2.0 | nokia-ui | 6 | 16 | 600 | 3955333389 | 0 | java/lang/NullPointerException | 运行正常 |  |
| 宠物王国2紫_5550.jar | 438 | cwwg2_z | - | 1.0.0 | MIDP-1.0 | nokia-ui | 19 | 16 | 600 | 1732173 | 0 | - | 运行正常 |  |
| 宠物王国3金_4892.jar | 585 | 宠物王国3(宝软网baoruan.com免费分享 | - | 1.0 | MIDP-1.0 | nokia-ui | 22 | 16 | 600 | 163931 | 0 | java/io/IOException | 运行正常 |  |
| 宠物王国4-白金_6531.jar | 699 | 宠物王国4-白金 | - | 1.0 | MIDP-2.0 | nokia-ui | 16 | 16 | 600 | 541443 | 0 | - | 运行正常 |  |
| 宠物王国5-彩虹_3413.jar | 1029 | 宠物王国5-彩虹 | - | 1.0 | MIDP-2.0 | nokia-ui | 23 | 16 | 600 | 384605 | 0 | - | 运行正常 |  |
| 宠物王国5-彩虹二次BT版_3356.jar | 1125 | 宠物王国5-彩虹 | - | 1.0 | MIDP-2.0 | nokia-ui | 28 | 16 | 600 | 394557 | 0 | - | 运行正常 |  |
| 宠物王国6(去模拟器限制)_12473.jar | 1387 | PetKing6 | - | 1.0 | MIDP-2.0 | - | 72 | 16 | 600 | 5366395 | 0 | java/io/EOFException | 运行正常 |  |
| 宠物王国6－烈火_465.jar | 1449 | 宠物王国6-烈火 | - | 1.0 | MIDP-2.0 | - | 121 | 16 | 600 | 12325919 | 0 | java/io/EOFException,java/lang/C | 运行正常 |  |
| 宠物精灵爱之体验_2861.jar | 1391 | Lovepet | - | 1.0 | MIDP-2.0 | - | 6 | 16 | 600 | 413015 | 0 | - | 运行正常 |  |
| 家园4汉化版_10405.jar | 215 | Townsmen4 | - | 1.0 | MIDP-2.0 | nokia-ui | 8 | 16 | 600 | 3129625 | 0 | java/lang/IllegalArgumentExcepti | 运行正常 |  |
| 家园5汉化版_10376.jar | 239 | Townsmen5 | - | 1.5 | MIDP-1.0 | nokia-ui | 7 | 16 | 600 | 2911027 | 0 | java/lang/ArrayIndexOutOfBoundsE | 运行正常 |  |
| 家园6之革命_7692.jar | 643 | Townsmen 6 | - | 1.0.0 | MIDP-2.0 | - | 26 | 16 | 601 | 1544789 | 0 | java/io/IOException,java/lang/Nu | 运行正常 |  |
| 家园6之革命完美汉化版_7583.jar | 590 | 家园6_光辉革命 | - | 1.0.0 | MIDP-2.0 | - | 26 | 16 | 601 | 1340942 | 0 | java/io/IOException,java/lang/Nu | 运行正常 |  |
| 寂静岭2汉化版_10177.jar | 507 | Silent Hill Mobile 2 | - | 1.0.0 | MIDP-2.0 | - | 12 | 16 | 600 | 8493631 | 0 | java/io/IOException,java/lang/Nu | 运行正常 |  |
| 寂静岭汉化版_9575.jar | 415 | Silent Hill | - | 0.1.0 | MIDP-2.0 | - | 11 | 16 | 600 | 3520267 | 0 | java/io/IOException,javax/microe | 运行正常 |  |
| 密室逃脱_7300.jar | 103 | 密室逃脱 | - | 1.0 | MIDP-1.0 | nokia-ui | 5 | 16 | 600 | 1069482 | 0 | - | 运行正常 |  |
| 寻秦皇陵的诅咒_12450.jar | 573 | 寻秦 | - | 1.0.0 | MIDP-1.0 | nokia-ui | 33 | 16 | 600 | 424167 | 0 | - | 运行正常 |  |
| 帝国时代2汉化版_7864.jar | 62 | 帝国时代2（微软正版） | - | 1.0 | MIDP-1.0 | nokia-ui | 9 | 16 | 600 | 1564663 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| 帝国时代3-亚洲王朝_6288.jar | 595 | 帝国时代III亚洲王朝 | - | 1.0.0 | MIDP-2.0 | - | 41 | 16 | 600 | 6841143 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| 帝国时代Ⅲ汉化版_10379.jar | 426 | Age Of Empires III Mobile | - | 1.0.1 | MIDP-1.0 | - | 10 | 16 | 600 | 3202349 | 0 | java/lang/NullPointerException | 运行正常 |  |
| 开心仓鼠大作战_3911.jar | 506 | kxcsdz | - | 1.0.0 | MIDP-1.0 | - | 38 | 16 | 600 | 30921288 | 0 | java/lang/IllegalArgumentExcepti | 运行正常 |  |
| 开心农场BT版_6075.jar | 624 | 开心农场 | - | 1.0 | MIDP-1.0 | - | 13 | 16 | 600 | 1568054 | 0 | java/io/IOException,java/lang/Nu | 运行正常 |  |
| 开心农场_240.jar | 374 | happy_farm | - | 1.30.04 | MIDP-2.0 | - | 39 | 16 | 600 | 1625602 | 0 | - | 运行正常 |  |
| 开心农场_6370.jar | 374 | happy_farm | - | 1.30.04 | MIDP-2.0 | - | 39 | 16 | 600 | 1625274 | 0 | - | 运行正常 |  |
| 开心餐厅2012_745.jar | 794 | 开心餐厅2012 | - | 1.2 | MIDP-2.0 | - | 76 | 16 | 600 | 149165 | 0 | java/lang/NullPointerException | 运行正常 |  |
| 异形战机汉化版_8382.jar | 268 | R-Type | - | 1.0 | MIDP-2.0 | - | 5 | 16 | 600 | 4126882 | 0 | java/io/IOException,java/lang/Nu | 运行正常 |  |
| 彩京打击者1945_10623.jar | 206 | Strikers 1945 | - | 1.0.0 | MIDP-2.0 | - | 4 | 16 | 600 | 15043 | 0 | - | 运行正常 |  |
| 彩虹岛汉化版_10241.jar | 124 | Rainbow Islands [MICHANG] | - | 1.0.6 | MIDP-2.0 | - | 16 | 16 | 600 | 8296960 | 0 | java/io/IOException | 运行正常 |  |
| 德州扑克2_3884.jar | 985 | Texas Hold'Em Poker 2 | - | 1.2.9 | MIDP-2.0 | - | 26 | 16 | 600 | 224464607 | 0 | java/lang/NullPointerException | 运行正常 |  |
| 忍者神龟之最终的判决_8288.jar | 357 | TMNT The Ninja Tribunal | - | 1.22 | MIDP-2.0 | - | 4 | 16 | 600 | 123004824 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| 快乐农场_2089.jar | 782 | 快乐农场之僵尸来袭 | - | 1.0.0 | MIDP-2.0 | - | 36 | 16 | 600 | 2149580 | 0 | - | 运行正常 |  |
| 快打旋风_10825.jar | 273 | FinalFight | - | 1.2.5 | MIDP-2.0 | - | 4 | 16 | 600 | 2959966 | 0 | - | 运行正常 |  |
| 急速公路狂飙－新年版_84.jar | 880 | 急速公路狂飙新年版 | - | 1.0 | MIDP-2.0 | m3g | 119 | 16 | 600 | 22516562 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| 急速狂飙完美破解版_4964.jar | 1301 | HighSpeed 3D | - | 1.6.0 | MIDP-2.0 | m3g | 48 | 16 | 600 | 93210572 | 0 | java/io/EOFException,java/io/IOE | 运行正常 |  |
| 恋爱物语－我的机器女友_6219.jar | 973 | cyborg_lover | - | 1.30.01 | MIDP-2.0 | - | 66 | 16 | 600 | 24672551 | 0 | java/lang/NegativeArraySizeExcep | 运行正常 |  |
| 恋色狂想之魔女恋歌_4899.jar | 774 | LCMS | - | 01.30.00 | MIDP-2.0 | - | 88 | 16 | 600 | 6870832 | 0 | - | 运行正常 |  |
| 愤怒的小鸟-高清版_2353.jar | 16451 | Angry Birds - by novchik | - | 0.1.1 | MIDP-2.0 | - | 45 | 32 | 600 | 6239238 | 0 | java/io/IOException,java/lang/Il | 运行正常 |  |
| 愤怒的小鸟_383.jar | 6333 | Angry Birds | - | 1.1 | MIDP-2.1 | - | 145 | 16 | 600 | 63719 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| 我们要恋爱之校园篇_1815.jar | 779 | 我们要恋爱之校园篇 | - | 1.0.0 | MIDP-2.0 | - | 32 | 16 | 600 | 146909 | 0 | java/lang/NullPointerException | 运行正常 |  |
| 我的宠物我做主BT版_3055.jar | 633 | 我的宠物我做主(正版)_玉品 | - | 1.0 | MIDP-1.0 | - | 20 | 16 | 600 | 20830827 | 0 | - | 运行正常 |  |
| 战争机器1945_5414.jar | 466 | 1945 | - | 1.0.0 | MIDP-1.0 | - | 11 | 16 | 600 | 647290 | 0 | java/lang/StringIndexOutOfBounds | 运行正常 |  |
| 战争艺术2之全球联盟中文版_1918.jar | 573 | 战争艺术2 | - | 1.0.0 | MIDP-2.0 | - | 15 | 16 | 600 | 6437374 | 0 | java/io/EOFException,java/lang/I | 运行正常 |  |
| 扫雷行动_10904.jar | 559 | Minesweeper Mobile | - | 0.0.1 | MIDP-2.0 | nokia-ui | 11 | 16 | 600 | 14178084 | 0 | java/lang/NullPointerException | 运行正常 |  |
| 拳王2009南拳北腿_1167.jar | 331 | 拳王2009-南拳北腿 | - | 1.0 | MIDP-1.0 | - | 8 | 16 | 600 | 976487 | 0 | - | 运行正常 |  |
| 拳王争霸_11585.jar | 582 | Lucha Libre | - | 1.0.8 | MIDP-2.0 | nokia-ui | 6 | 16 | 601 | 973635283 | 0 | java/lang/NullPointerException | 运行正常 |  |
| 拳皇98终极之战_7534.jar | 831 | 拳皇98-终极之战 | - | 1.0 | MIDP-1.0 | - | 37 | 16 | 600 | 7206460 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| 捕鱼达人3金装版_1511.jar | 634 | 深海猎奇-捕鱼达人 | - | 1.00 | MIDP-1.0 | nokia-ui | 15 | 16 | 600 | 12099602 | 0 | java/io/IOException,java/lang/Nu | 运行正常 |  |
| 文明3_10522.jar | 160 | Civilization 3 | - | 1.0.0 | MIDP-2.0 | - | 7 | 16 | 600 | 10730519 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| 斗po苍穹bt版_1127.jar | 1609 | 斗破苍穹 | - | 1.0.0 | MIDP-1.0 | - | 37 | 16 | 600 | 30015097 | 0 | java/io/EOFException,java/io/IOE | 运行正常 |  |
| 斩妖伏魔录3_6306.jar | 1016 | 斩妖伏魔录3 | - | 1.0 | MIDP-2.0 | nokia-ui | 21 | 16 | 600 | 6426943 | 0 | java/io/IOException | 运行正常 |  |
| 斩妖伏魔录4(QQ挑战)_2922.jar | 1043 | 斩妖伏魔录4 | - | 1.0 | MIDP-2.0 | nokia-ui | 55 | 16 | 600 | 8772626 | 0 | java/io/IOException | 运行正常 |  |
| 斩妖伏魔录_6231.jar | 584 | 斩妖伏魔录(华娱) | - | 1.0 | MIDP-1.0 | nokia-ui | 19 | 16 | 600 | 6238487 | 0 | java/io/IOException,java/lang/Nu | 运行正常 |  |
| 新2011拳皇_4132.jar | 948 | 新拳皇2011 | - | 1.0 | MIDP-1.0 | - | 33 | 16 | 600 | 2681528 | 0 | java/io/EOFException,javax/micro | 运行正常 |  |
| 新三国志_10225.jar | 186 | 新三国志 | - | 1.0.0 | MIDP-2.0 | - | 17 | 16 | 600 | 58775 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| 新牧场物语2恋爱季节_1340.jar | 991 | 新牧场物语2恋爱季节 | - | 1.0.0 | MIDP-2.0 | - | 73 | 16 | 600 | 3351960 | 0 | java/lang/NullPointerException | 运行正常 |  |
| 新西游记－格斗天王_1211.jar | 1327 | 新西游记-格斗天王 | - | 1.0 | MIDP-1.0 | - | 56 | 16 | 600 | 32116705 | 0 | - | 运行正常 |  |
| 时光八音盒2忆之篇_3865.jar | 1014 | 时光八音盒II_忆之篇_玉品 | - | 1.0.0 | MIDP-1.0 | - | 11 | 16 | 600 | 25620804 | 0 | java/io/IOException,java/lang/Ar | 运行正常 |  |
| 时光八音盒萌之篇_3806.jar | 961 | sgbyh | - | 1.0.0 | MIDP-1.0 | - | 11 | 16 | 600 | 32937192 | 0 | java/lang/ArrayIndexOutOfBoundsE | 运行正常 |  |
| 时光樱花册－遗失之心_1009.jar | 1380 | 时光樱花册-遗失之心 | - | 1.0 | MIDP-1.0 | - | 33 | 16 | 600 | 335281525 | 0 | - | 运行正常 |  |
| 明星制造梦工厂_11048.jar | 220 | 明星制造梦工厂 | - | 1.2 | MIDP-1.0 | - | 8 | 16 | 600 | 169668804 | 0 | java/io/IOException | 运行正常 |  |
| 明星麻将馆美女_9760.jar | 362 | 明星麻将馆(美女真人版) | - | 1.0.0 | MIDP-1.0 | nokia-ui | 11 | 16 | 325 | 195441 | 0 | java/lang/NullPointerException | 运行正常 | exited itself (notifyDestroyed) |
| 星空恋曲-遥远的记忆(修正版)_4008.jar | 982 | STAR | - | 01.30.00 | MIDP-2.0 | - | 84 | 16 | 600 | 4365985 | 0 | - | 运行正常 |  |
| 星辰变S60V5完整版_3364.jar | 926 | 星辰变(盛大正版) | - | 1.0.0 | MIDP-1.0 | nokia-ui | 40 | 16 | 600 | 3395370874 | 0 | java/io/IOException | 运行正常 |  |
| 是男人就下100层_1882.jar | 636 | 是男人就下100层 | - | 2.01 | MIDP-1.0 | nokia-ui | 75 | 16 | 600 | 53838323 | 0 | java/io/IOException,java/lang/Nu | 运行正常 |  |
| 暗黑启示录2－黑暗王朝_888.jar | 1268 | 暗黑启示录2黑暗王朝 | - | 1.0 | MIDP-1.0 | - | 214 | 16 | 600 | 3007996994 | 0 | java/io/EOFException | 运行正常 |  |
| 有女倾城破解版_11525.jar | 258 | 有女倾城 | - | 1.0 | MIDP-1.0 | - | 9 | 16 | 0 | 44358 | 0 | - | 未呈现画面 | exited itself (notifyDestroyed) without pain |
| 机甲风暴_7888.jar | 673 | 机甲风暴 | - | 1.0 | MIDP-2.0 | nokia-ui | 41 | 16 | 600 | 146994 | 0 | - | 运行正常 |  |
| 杀戮机器-纳粹僵尸_3610.jar | 995 | 杀戮机器-纳粹僵尸 | - | 1.0 | MIDP-2.0 | m3g | 37 | 16 | 600 | 706824 | 0 | java/lang/NumberFormatException | 运行正常 |  |
| 杀鬼子-敌后武工队_2185.jar | 515 | 杀鬼子-敌后武工队 | - | 1.0.0 | MIDP-1.0 | nokia-ui | 37 | 16 | 432 | 2086150141 | 0 | java/io/IOException,java/lang/Nu | 运行正常 | exited itself (notifyDestroyed) |
| 极品飞车13之变速英文3D版_6646.jar | 754 | Need for Speed Shift | - | 7.2.18 | MIDP-2.0 | m3g | 38 | 16 | 600 | 31603545 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| 极品飞车14之热力追踪中文版_4160.jar | 1219 | 极品飞车：热血追踪 | - | 1.0.0 | MIDP-2.0 | m3g | 44 | 16 | 611 | 14976668 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| 极品飞车16：亡命狂飙_1519.jar | 1046 | Need for Speed The Run | - | 4.4.88 | MIDP-2.0 | m3g,nokia-ui | 41 | 16 | 610 | 65520528 | 0 | java/lang/IllegalArgumentExcepti | 运行正常 |  |
| 极品飞车之无间风云FX中文版_4518.jar | 1000 | 极品飞车无间风云FX | - | 1.0.0 | MIDP-2.0 | m3g | 63 | 16 | 600 | 10471073 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| 极品飞车精装版_7557.jar | 159 | 极品飞车精装版 | - | 1.0 | MIDP-1.0 | - | 16 | 16 | 600 | 3430303 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| 柯楠之密室杀人事件_3821.jar | 429 | 柯楠－密室杀人事件 | - | 1.0 | MIDP-2.0 | nokia-ui | 39 | 16 | 600 | 1477770 | 0 | javax/microedition/media/MediaEx | 运行正常 |  |
| 校园回忆_1940.jar | 358 | 校园回忆 | - | 1.0 | MIDP-1.0 | - | 16 | 16 | 600 | 110874 | 0 | - | 运行正常 |  |
| 格斗2011-拳王争霸_4253.jar | 805 | GDQWZB | - | 1.2.0 | MIDP-2.0 | - | 8 | 16 | 600 | 3945751624 | 0 | java/lang/NullPointerException | 运行正常 |  |
| 格斗三国五虎将精装版_9085.jar | 275 | 格斗三国五虎将(精装版) | - | 1.0 | MIDP-1.0 | - | 8 | 16 | 600 | 1649717 | 0 | javax/microedition/rms/InvalidRe | 运行正常 |  |
| 格斗少女_3271.jar | 709 | gdsn | - | 1.0 | MIDP-2.0 | - | 14 | 16 | 600 | 1083963 | 0 | - | 运行正常 |  |
| 格斗火柴人_3938.jar | 416 | Enter The Flip | - | 1.0 | MIDP-2.0 | - | 11 | 16 | 600 | 7835690 | 0 | - | 运行正常 |  |
| 梦幻西游_4223.jar | 1024 | 梦幻西游 | - | 1.0 | MIDP-1.0 | - | 88 | 16 | 600 | 10885499 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| 植物大战僵尸(新角色加强版)_982.jar | 421 | 植物大战僵尸 | - | 1.0 | MIDP-1.0 | - | 37 | 16 | 600 | 62834 | 0 | - | 运行正常 |  |
| 植物大战僵尸2011加强版_2912.jar | 533 | 植物大战僵尸(2011加强版) | - | 1.0 | MIDP-1.0 | - | 56 | 16 | 600 | 467684 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| 植物大战僵尸2013完美升级版_326.jar | 516 | 植物大战僵尸 | - | 1.0 | MIDP-1.0 | - | 41 | 16 | 600 | 288547 | 0 | - | 运行正常 |  |
| 植物大战僵尸BT版_650.jar | 905 | 植物大战僵尸 | - | 2.2 | MIDP-2.0 | - | 99 | 16 | 601 | 141760083 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| 植物大战僵尸欧美大波版_407.jar | 510 | 植物大战僵尸2013欧美大波版 | - | 1.0 | MIDP-1.0 | - | 34 | 16 | 600 | 852664 | 0 | - | 运行正常 |  |
| 模拟人生2_10762.jar | 273 | 模拟人生2 | - | 1.0.0 | MIDP-1.0 | - | 31 | 16 | 600 | 3599339883 | 0 | java/io/IOException,javax/microe | 运行正常 |  |
| 模拟人生2完美版_11722.jar | 417 | The Sims 2 | - | 1.8.13 | MIDP-2.0 | nokia-ui | 20 | 16 | 600 | 1160764557 | 0 | javax/microedition/rms/InvalidRe | 运行正常 |  |
| 模拟人生2宠物当家_10928.jar | 410 | 模拟人生2宠物当家 | - | 1.0 | MIDP-2.0 | - | 35 | 16 | 600 | 6391362 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| 模拟人生3_7562.jar | 559 | The Sims 3 | - | 7.5.73 | MIDP-2.0 | - | 28 | 16 | 600 | 35051065 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| 模拟人生3中文版_6162.jar | 492 | 模拟人生3(EA正版) | - | 1.0.0 | MIDP-2.0 | - | 25 | 16 | 600 | 6365034 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| 模拟人生3之野心_4936.jar | 817 | The Sims 3 Ambitions | - | 4.2.28 | MIDP-2.0 | - | 26 | 16 | 600 | 12535509 | 0 | java/io/EOFException | 运行正常 |  |
| 模拟人生3繁体bt版_6026.jar | 497 | The Sims 3 | - | 2.0.0 | MIDP-2.0 | - | 27 | 16 | 600 | 7459623 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| 模拟恋爱2_11543.jar | 291 | SimBabe2 by serviak | - | 1.4 | MIDP-2.0 | - | 2 | 16 | 600 | 9757143 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| 武林外传-明星斗地主增值版_3971.jar | 689 | 武林外传-明星斗地主(电影正版) | - | 1.0.0 | MIDP-2.0 | - | 52 | 16 | 600 | 416816 | 0 | - | 运行正常 |  |
| 武林外传_9847.jar | 400 | 武林外传-RPG巨作正版 | - | 1.0 | MIDP-2.0 | nokia-ui | 85 | 16 | 600 | 3540056 | 0 | javax/microedition/media/MediaEx | 运行正常 |  |
| 歪歪隋唐英雄传_11206.jar | 52 | waiwai | - | 1.0 | MIDP-1.0 | nokia-ui | 6 | 16 | 600 | 41788 | 0 | java/io/IOException | 运行正常 |  |
| 毁灭战士3_10598.jar | 1590 | Doom3D | - | 1.0 | MIDP-2.0 | m3g | 48 | 16 | 600 | 2131835 | 0 | java/io/IOException | 运行正常 |  |
| 比基尼美女海底影相_9643.jar | 275 | Foto Quest Bikini | - | 2.0.6 | MIDP-1.0 | nokia-ui | 14 | 16 | 600 | 12876699 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| 水浒108将-激斗篇_2196.jar | 625 | 水浒108将-激斗篇 | - | 1.0.0 | MIDP-1.0 | - | 33 | 16 | 600 | 538236922 | 0 | java/io/IOException,java/lang/Nu | 运行正常 |  |
| 法拉利GT3－世界赛道BT版_776.jar | 2881 | Ferrari GT 3 - World Track | - | 1.1.2 | MIDP-2.0 | nokia-ui | 22 | 16 | 600 | 2818670183 | 0 | java/lang/NullPointerException | 运行正常 |  |
| 波斯王子之时之沙中文版_11315.jar | 60 | 波斯王子-时之沙 | - | 1.0.1 | MIDP-1.0 | nokia-ui | 6 | 16 | 600 | 1212757899 | 0 | - | 运行正常 |  |
| 波斯王子之武者之心_8424.jar | 202 | Prince Of Persia | - | 1.0.2 | MIDP-2.0 | - | 5 | 16 | 600 | 2666948208 | 0 | - | 运行正常 |  |
| 波斯王子经典版_9715.jar | 326 | Prince of Persia Classic | - | 1.0.3 | MIDP-2.0 | - | 9 | 16 | 600 | 7122116215 | 0 | java/lang/NullPointerException | 运行正常 |  |
| 浴火银河2完整版_7689.jar | 1639 | Galaxy on Fire 2 | - | 1.0.3 | MIDP-2.0 | m3g | 154 | 16 | 600 | 385998708 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| 浴火银河2汉化版_6950.jar | 1680 | Galaxy on Fire 2 | - | 1.0.3 | MIDP-2.0 | m3g | 155 | 16 | 600 | 424024272 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| 浴火银河2瓦尔基里(汉化)_3067.jar | 9771 | Galaxy on Fire 2 | - | 1.0.3 | MIDP-2.0 | m3g | 155 | 16 | 600 | 430079527 | 0 | java/lang/IllegalArgumentExcepti | 运行正常 |  |
| 浴火银河2瓦尔基里_3093.jar | 9746 | Galaxy on Fire 2 | - | 1.0.3 | MIDP-2.0 | m3g | 154 | 16 | 600 | 369973718 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| 浴火银河_10189.jar | 510 | 3D Galaxy on Fire | - | 1.0.3 | MIDP-2.0 | mascot | 101 | 16 | 600 | 52690489 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| 浴火银河_3621.jar | 510 | 3D Galaxy on Fire | - | 1.0.3 | MIDP-2.0 | mascot | 101 | 16 | 600 | 57514583 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| 海绵宝宝卡丁车_4684.jar | 651 | HMBBKDC | - | 0.1.0 | MIDP-2.0 | - | 9 | 16 | 600 | 3823256705 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| 涂鸦地带_模拟可玩_12472.jar | 62 | 涂鸦地带 | - | 1.0 | MIDP-2.0 | - | 22 | 16 | 600 | 895557 | 0 | java/io/IOException,javax/microe | 运行正常 |  |
| 涂鸦跳跃中文豪华版(汉化)_2524.jar | 1424 | 涂鸦跳跃 | - | 1.8.2 | MIDP-2.0 | - | 13 | 16 | 600 | 571630937 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| 灌篮高手-爆发!樱木_7947.jar | 327 | 灌篮高手-爆发！樱木 | - | 1.0.0 | MIDP-2.0 | - | 44 | 16 | 600 | 945097 | 0 | java/lang/ArithmeticException,ja | 运行正常 |  |
| 火影忍者-终极格斗_6604.jar | 673 | 火影忍者-精品格斗2010 | - | 1.0.0 | MIDP-1.0 | nokia-ui | 41 | 16 | 600 | 6952582 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| 火影忍者_11531.jar | 335 | Naruto | - | Mod | MIDP-1.0 | nokia-ui | 9 | 16 | 600 | 723405 | 0 | javax/microedition/rms/InvalidRe | 运行正常 |  |
| 火影忍者格斗2010_4601.jar | 652 | 火影忍者-精品格斗2010 | - | 1.0.0 | MIDP-1.0 | nokia-ui | 39 | 16 | 600 | 5104700 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| 火影忍者汉化版_10735.jar | 261 | Ninja Fighter (RoS) | - | 1.0 | MIDP-1.0 | nokia-ui | 22 | 16 | 600 | 13297643 | 0 | java/io/EOFException,java/io/IOE | 运行正常 |  |
| 火焰-纹章之谜_12128.jar | 316 | 火焰-纹章之谜 | - | 1.9 | MIDP-1.0 | nokia-ui | 17 | 16 | 600 | 909521 | 0 | java/io/IOException,java/lang/Nu | 运行正常 |  |
| 火焰-纹章传说第一章_12129.jar | 365 | 火焰-纹章传说第一章 | - | 1.6 | MIDP-1.0 | nokia-ui | 18 | 16 | 600 | 862478 | 0 | java/io/IOException,java/lang/Nu | 运行正常 |  |
| 火焰-纹章传说第三章_12130.jar | 354 | 火焰-纹章传说第三章 | - | 1.8 | MIDP-1.0 | nokia-ui | 18 | 16 | 600 | 1133634 | 0 | java/io/IOException,java/lang/Nu | 运行正常 |  |
| 火焰-纹章传说第二章_12131.jar | 343 | 火焰-纹章传说第二章 | - | 1.8 | MIDP-1.0 | nokia-ui | 18 | 16 | 600 | 958010 | 0 | java/io/IOException,java/lang/Nu | 运行正常 |  |
| 火焰纹章勇者之剑_10337.jar | 163 | 圣火纹章-勇者之剑 | - | 1.0 | MIDP-1.0 | - | 16 | 16 | 600 | 337156 | 0 | - | 运行正常 |  |
| 灵异村庄_11169.jar | 94 | The Village | - | 1.2 | MIDP-1.0 | nokia-ui | 19 | 16 | 600 | 2075862 | 0 | java/io/IOException,javax/microe | 运行正常 |  |
| 炸弹人_10311.jar | 302 | Bomberman | - | 1.0.0 | MIDP-1.0 | nokia-ui | 12 | 16 | 600 | 21843732 | 0 | javax/microedition/rms/InvalidRe | 运行正常 |  |
| 热血沙滩9球赛_894.jar | 472 | 热血沙滩9球赛 | - | 1.0.0 | MIDP-2.0 | - | 27 | 16 | 600 | 7838910 | 0 | java/lang/ArrayIndexOutOfBoundsE | 运行正常 |  |
| 爆笑三国斗地主_10552.jar | 312 | 爆笑三国斗地主 | - | 1.0 | MIDP-1.0 | nokia-ui | 8 | 16 | 600 | 350951 | 0 | - | 运行正常 |  |
| 狂野飙车4-gameloft(中文版)_2603.jar | 647 | 狂野飙车4 | - | 1.0.0 | MIDP-2.0 | - | 22 | 16 | 600 | 205776496 | 0 | - | 运行正常 |  |
| 狂野飙车4-亡命都市_2875.jar | 616 | 狂野飙车-亡命都市 | - | 1.0.0 | MIDP-2.0 | - | 13 | 16 | 600 | 219755947 | 0 | - | 运行正常 |  |
| 狂野飙车4_6959.jar | 596 | 狂野飙车4-gameloft | - | 1.0.0 | MIDP-2.0 | - | 12 | 16 | 600 | 68228042 | 0 | - | 运行正常 |  |
| 猿人时代BT版_4519.jar | 489 | 猿人时代 | - | 1.0.0 | MIDP-2.0 | - | 49 | 16 | 600 | 10571455 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| 生化危机-险境求生_3290.jar | 914 | 生化危机 | - | 1.0.0 | MIDP-1.0 | nokia-ui | 53 | 16 | 600 | 3320452195 | 0 | - | 运行正常 |  |
| 生化危机3D(CAPCOM经典)_3671.jar | 983 | shwjd | - | 1.0 | MIDP-2.0 | m3g | 21 | 16 | 601 | 625583 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| 生化危机5核变_866.jar | 1181 | 生化危机5核变 | - | 1.0 | MIDP-1.0 | nokia-ui | 61 | 16 | 600 | 698887 | 0 | - | 运行正常 |  |
| 生化危机之恶化_8448.jar | 372 | RED http://www.universosymbi | - | 1.0.0 | MIDP-2.0 | - | 10 | 16 | 601 | 2911589 | 0 | java/lang/NumberFormatException | 运行正常 |  |
| 生化惊悚2死亡寂静中文版_2607.jar | 1144 | shjswjj | - | 0.0.5 | MIDP-2.0 | - | 26 | 16 | 600 | 255114666 | 0 | java/lang/ArrayIndexOutOfBoundsE | 运行正常 |  |
| 电子宠物蛋_11972.jar | 95 | Tamagotchi 240x320 | - | 1.0.96 | MIDP-1.0 | nokia-ui | 8 | 16 | 600 | 1900063 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| 电玩美女麻将_7260.jar | 439 | 电玩美女麻将 | - | 1.0.0 | MIDP-1.0 | - | 9 | 16 | 600 | 2350199 | 0 | javax/microedition/rms/InvalidRe | 运行正常 |  |
| 男摄影师_11003.jar | 250 | 男摄影师 | - | 1.0.0 | MIDP-1.0 | - | 19 | 16 | 600 | 8739756 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| 疯狂企鹅中文版_10228.jar | 140 | 功夫企鹅 | - | 1.0.0 | MIDP-2.0 | - | 13 | 16 | 600 | 12931222 | 0 | java/io/EOFException,javax/micro | 运行正常 |  |
| 疯狂农场2_2375.jar | 551 | Farm Frenzy 2 | - | 1.0.3 | MIDP-2.0 | - | 42 | 16 | 600 | 11154059 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| 疯狂农场汉化版_5998.jar | 400 | Farm Frenzy | - | 2.0.0 | MIDP-2.0 | - | 33 | 16 | 600 | 3822584 | 0 | java/io/EOFException,java/io/IOE | 运行正常 |  |
| 疯狂出租车中文版_721.jar | 491 | 疯狂出租车 | - | 1.0 | MIDP-1.0 | - | 49 | 16 | 600 | 181355 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| 疯狂战机_2013.jar | 219 | Srcombat20110928 | - | 1.0.0 | MIDP-2.0 | - | 65 | 16 | 600 | 3557933684 | 0 | java/io/IOException | 运行正常 |  |
| 百战天虫2010中文版_5380.jar | 560 | PC原版-百战天虫2010-EA | - | 1.0.0 | MIDP-2.0 | - | 25 | 16 | 600 | 50812938 | 0 | java/lang/NullPointerException | 运行正常 |  |
| 百鬼商店破解版_10526.jar | 382 | BAI_GUI_SHANG_DIAN | - | 1.0 | MIDP-2.0 | - | 4 | 16 | 600 | 1073682 | 0 | - | 运行正常 | runs without input; the generic key script m |
| 真三国无双横扫千军_12449.jar | 270 | 横扫千军.宝软网baoruan.com免费分享 | - | 1.0 | MIDP-2.0 | - | 14 | 16 | 600 | 1011016 | 0 | - | 运行正常 |  |
| 真人快打3中文版_3252.jar | 299 | 真人快打3 | - | 1.0.0 | MIDP-2.0 | - | 50 | 16 | 600 | 15059362 | 0 | java/lang/ClassCastException,jav | 运行正常 |  |
| 真幻想三国志完整版_12469.jar | 877 | 真幻想三国志 | - | 1.0 | MIDP-1.0 | nokia-ui | 138 | 16 | 600 | 2336333268 | 0 | - | 运行正常 |  |
| 真武三国BT版_3953.jar | 1013 | 真武三国 | - | 1.0 | MIDP-2.0 | - | 23 | 16 | 600 | 7074078 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| 真武三国_3962.jar | 1018 | 真武三国 | - | 1.0 | MIDP-2.0 | - | 22 | 16 | 600 | 7074305 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| 真西游记_6848.jar | 738 | 真西游记(华娱) | - | 1.0 | MIDP-1.0 | - | 18 | 16 | 600 | 1666461 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| 神庙逃亡2（Temple_18.jar | 680 | Run2 | - | 2.0.0 | MIDP-2.0 | - | 4 | 16 | 600 | 706602334 | 0 | - | 运行正常 |  |
| 科比职业篮球2008中文版_11136.jar | 520 | 科比大灌篮(gameloft正版) | - | 1.02 | MIDP-1.0 | - | 10 | 16 | 600 | 4978521884 | 0 | java/lang/ArrayIndexOutOfBoundsE | 运行正常 |  |
| 空战1944_10321.jar | 309 | Air Strike 1944 | - | 1.0.0 | MIDP-2.0 | - | 6 | 16 | 600 | 109002377 | 0 | - | 运行正常 |  |
| 穿越火线-韩国正版_6874.jar | 480 | chuanyuehuoxian | - | 1.0.0 | MIDP-1.0 | - | 21 | 16 | 600 | 3995859030 | 0 | - | 运行正常 | runs without input; the generic key script m |
| 竞技者之极限酷跑_5431.jar | 250 | 竞技者之极限酷跑 | - | 1.0.4 | MIDP-2.0 | - | 7 | 16 | 600 | 702246467 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| 精品美女斗地主2013_104.jar | 1203 | 精品美女斗地主 | - | 1.0 | MIDP-1.0 | nokia-ui | 61 | 16 | 600 | 801902647 | 0 | java/io/IOException,java/lang/Nu | 运行正常 |  |
| 红色警戒3_7037.jar | 784 | C&C: Red Alert Mobile by bli | - | 4.15.48 | MIDP-2.0 | - | 33 | 16 | 600 | 4505309 | 0 | - | 运行正常 |  |
| 纯爱物语3之梦想之翼_5112.jar | 978 | L3D | - | 1.30.0 | MIDP-2.0 | - | 82 | 16 | 600 | 6240009 | 0 | - | 运行正常 |  |
| 纯爱物语之湛蓝回忆简体版_5164.jar | 1007 | LoveMemory | - | 1.0 | MIDP-1.0 | - | 66 | 16 | 600 | 84447 | 0 | java/lang/NullPointerException | 运行正常 |  |
| 纽约人生_1339.jar | 879 | My Life in New York | - | 1.0.6 | MIDP-2.0 | - | 12 | 16 | 600 | 227742141 | 0 | java/lang/NullPointerException | 运行正常 |  |
| 细胞分裂4混沌法则中文版_11671.jar | 101 | 细胞分裂3-混沌法则 | - | 1.02 | MIDP-1.0 | nokia-ui | 4 | 16 | 600 | 2899696718 | 0 | - | 运行正常 |  |
| 终极雷电_4125.jar | 293 | 终极雷电 | - | 1.0.0 | MIDP-1.0 | - | 57 | 16 | 600 | 740222 | 0 | - | 运行正常 |  |
| 经典推箱子_1070.jar | 247 | 纸盒人 | - | 1.0.0 | MIDP-1.0 | - | 34 | 16 | 600 | 147742 | 0 | java/io/IOException | 运行正常 |  |
| 绝密反击破解版_8377.jar | 174 | 绝密反击 | - | 1.0 | MIDP-1.0 | nokia-ui | 39 | 16 | 600 | 2498920 | 0 | - | 运行正常 |  |
| 网球大满贯_11950.jar | 143 | 网球大满贯 | - | 1.0 | MIDP-1.0 | - | 11 | 16 | 600 | 231966335 | 0 | java/io/IOException,javax/microe | 运行正常 |  |
| 美女台球_9377.jar | 205 | Billard Girls | - | 1.0.11 | MIDP-1.0 | - | 40 | 16 | 600 | 20861790 | 0 | java/lang/NullPointerException | 运行正常 |  |
| 美女斗地主(真人语音版)_1609.jar | 1089 | 写真美女斗地主(真人语音) | - | 1.0.0 | MIDP-1.0 | - | 29 | 16 | 600 | 2011070 | 0 | java/lang/NullPointerException | 运行正常 |  |
| 美女斗地主_1088.jar | 717 | 美女斗地主 | - | 1.0 | MIDP-2.0 | nokia-ui | 14 | 16 | 600 | 13434069 | 0 | java/lang/NullPointerException | 运行正常 |  |
| 美女滑雪胜地_2.jar | 290 | 美女滑雪胜地 | - | 1.0.0 | MIDP-2.0 | - | 23 | 16 | 600 | 13849060 | 0 | - | 运行正常 |  |
| 美少女梦幻工厂_11443.jar | 181 | 美少女梦幻工厂 | - | 1.0 | MIDP-1.0 | nokia-ui | 4 | 16 | 600 | 57924 | 0 | javax/microedition/rms/InvalidRe | 运行正常 |  |
| 美男狂想曲_3876.jar | 856 | Hotguys Rhapsody | - | 1.0.0 | MIDP-1.0 | - | 11 | 16 | 600 | 4375729 | 0 | java/io/IOException,java/lang/Nu | 运行正常 |  |
| 聊斋之倩女幽魂_11185.jar | 300 | 聊斋之倩女幽魂 | - | 1.0 | MIDP-2.0 | nokia-ui | 6 | 16 | 600 | 2040328663 | 0 | - | 运行正常 |  |
| 航海世纪2_10583.jar | 157 | Tradewinds 2 | - | 1.1.4 | MIDP-2.0 | - | 4 | 16 | 600 | 1359977841 | 0 | java/lang/IllegalArgumentExcepti | 运行正常 |  |
| 色戒之虚拟人生_7230.jar | 584 | 色戒之虚拟人生 | - | 1.0.0 | MIDP-1.0 | - | 19 | 16 | 600 | 2000033 | 0 | - | 运行正常 |  |
| 花样少年恋爱学院_5977.jar | 764 | love_school | - | 1.30.02 | MIDP-2.0 | - | 45 | 16 | 600 | 37405760 | 0 | java/lang/NullPointerException | 运行正常 |  |
| 花样少年恋爱学院特别版_5527.jar | 776 | love-school-sp | - | 1.30.11 | MIDP-2.0 | - | 45 | 16 | 600 | 36445565 | 0 | java/lang/NullPointerException | 运行正常 |  |
| 苍穹默示录完美运行版_4035.jar | 1790 | 苍穹默示录 | - | 1.0.0 | MIDP-1.0 | - | 53 | 16 | 600 | 4719443965 | 0 | java/io/IOException,java/lang/Cl | 运行正常 |  |
| 荒岛余生(GLU正版)_12478.jar | 451 | 荒岛余生(GLU正版) | - | 1.0.1 | MIDP-1.0 | - | 9 | 16 | 600 | 6481853 | 0 | java/lang/NullPointerException | 运行正常 |  |
| 荒岛余生2时空探秘_6794.jar | 486 | 荒岛余生2时空探秘 | - | 1.0.1 | MIDP-2.0 | - | 31 | 16 | 600 | 5472856 | 0 | javax/microedition/rms/InvalidRe | 运行正常 |  |
| 荣誉勋章之空降神兵改良版_2993.jar | 559 | Medal Of Honor Airborne Elit | - | 4.18.80 | MIDP-2.0 | - | 32 | 16 | 600 | 23906478 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| 蓝牙真3D格斗_3424.jar | 602 | 真3D格斗 | - | 1.0 | MIDP-2.0 | m3g,nokia-ui | 26 | 16 | 600 | 7389802 | 0 | javax/bluetooth/BluetoothStateEx | 运行正常 |  |
| 蓝白五子棋_9790.jar | 104 | Gomoku by drAdeLante | - | 1.0.0 | MIDP-2.0 | - | 15 | 16 | 600 | 3699157607 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| 蛋蛋历险记_11460.jar | 162 | Bird ETTY | - | 1.4.6 | MIDP-2.0 | - | 2 | 16 | 600 | 802390610 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| 血战三国之吕布貂蝉_4012.jar | 347 | 血战三国-吕布VS貂蝉 | - | 1.0.0 | MIDP-1.0 | - | 10 | 16 | 600 | 53993 | 0 | javax/microedition/rms/InvalidRe | 运行正常 |  |
| 血战轩辕之魔神蚩尤_11172.jar | 198 | 血战轩辕之魔神蚩尤 | - | 1.0 | MIDP-1.0 | nokia-ui | 12 | 16 | 600 | 1327057 | 0 | java/lang/IllegalArgumentExcepti | 运行正常 |  |
| 街头反恐之格斗女王_822.jar | 285 | 街头反恐之格斗女王 | - | 1.0.0 | MIDP-2.0 | - | 10 | 16 | 600 | 2774739 | 0 | - | 运行正常 |  |
| 街头霸王-街机经典_2664.jar | 563 | 街头霸王 | - | 1.0.0 | MIDP-1.0 | - | 12 | 16 | 603 | 2054074 | 0 | java/lang/IllegalArgumentExcepti | 运行正常 |  |
| 街头霸王2_11388.jar | 493 | StreetFighter2 | - | 1.2.4 | MIDP-1.0 | - | 5 | 16 | 600 | 24756103 | 0 | java/lang/NullPointerException | 运行正常 |  |
| 街头霸王2冠军典藏版_1812.jar | 730 | jtbw | - | 1.0.0 | MIDP-2.0 | - | 9 | 16 | 600 | 2854201 | 0 | - | 运行正常 |  |
| 街霸2012(加强版)_2302.jar | 1117 | 街霸2012加强版 | - | 1.0 | MIDP-1.0 | - | 40 | 16 | 600 | 126744699 | 0 | java/io/EOFException,java/lang/C | 运行正常 |  |
| 西伯利亚大反击_11466.jar | 205 | 西伯利亚大反击 | - | 1.0 | MIDP-1.0 | - | 2 | 16 | 600 | 2208912282 | 0 | - | 运行正常 |  |
| 诈金花_6358.jar | 557 | 诈金花 | - | 1.0 | MIDP-1.0 | nokia-ui | 24 | 16 | 600 | 763525 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| 诛神－斗破苍穹_1402.jar | 1190 | 诛神-斗破苍穹 | - | 1.0.0 | MIDP-2.0 | - | 62 | 16 | 600 | 373019135 | 0 | java/io/EOFException,java/lang/A | 运行正常 |  |
| 诸神战纪-堕落天使路西法_5070.jar | 1324 | 诸神战纪-堕落天使路西法 | - | 1.0 | MIDP-1.0 | - | 28 | 16 | 600 | 5604179 | 0 | - | 运行正常 |  |
| 贪吃蛇经典版_4515.jar | 124 | Snake | - | 1.3 | MIDP-2.0 | - | 68 | 16 | 470 | 9742 | 0 | - | 运行正常 | exited itself (notifyDestroyed) |
| 赤色要塞－FC移植版_5285.jar | 282 | 赤色要塞 | - | 1.0.0 | MIDP-1.0 | nokia-ui | 43 | 16 | 600 | 2757154 | 0 | java/io/EOFException,javax/micro | 运行正常 |  |
| 超时空帝国机甲风暴_12065.jar | 542 | 超时空帝国机甲风暴 | - | 1.0 | MIDP-2.0 | nokia-ui | 20 | 16 | 600 | 279001 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| 超级坦克大战2汉化版_531.jar | 655 | (发烧)超级坦克大战II(增强版) | - | 1.0.1 | MIDP-1.0 | nokia-ui | 24 | 16 | 600 | 3454049521 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| 超级酒吧女生_4728.jar | 569 | 超级酒吧女生 | - | 1.0.0 | MIDP-2.0 | - | 10 | 16 | 600 | 3678410022 | 0 | javax/microedition/rms/InvalidRe | 运行正常 |  |
| 超级马里奥_10283.jar | 534 | 超级马里奥 | - | 1.0.0 | MIDP-2.0 | - | 11 | 16 | 600 | 3461432131 | 0 | - | 运行正常 |  |
| 足球经理2010汉化版_4756.jar | 517 | CM-2010 | - | 1.2.1 | MIDP-2.0 | nokia-ui | 21 | 16 | 600 | 5817111 | 0 | - | 运行正常 |  |
| 跑跑卡丁车-经典中秋版_2117.jar | 623 | 跑跑卡丁车 | - | 1.0.0 | MIDP-2.0 | - | 21 | 16 | 691 | 649080 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| 蹦球传说_7801.jar | 375 | Bounce Tales | - | 2.0.3 | MIDP-2.0 | nokia-ui | 21 | 16 | 91 | 56862 | 0 | - | 标题自行退出 | exited itself (notifyDestroyed) after 91 fra |
| 蹦球传说美化版_2959.jar | 416 | Bounce Tales | - | 2.0.3 | MIDP-2.0 | nokia-ui | 19 | 16 | 600 | 72257386 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| 轩辕神剑_2419.jar | 1324 | 轩辕神剑 | - | 1.0 | MIDP-2.0 | - | 71 | 16 | 600 | 3416600 | 0 | java/io/EOFException,java/lang/N | 运行正常 |  |
| 迈阿密之夜_9566.jar | 315 | MiamiNights | - | 1.0.2 | MIDP-2.0 | - | 6 | 16 | 600 | 179557349 | 0 | javax/microedition/rms/InvalidRe | 运行正常 |  |
| 远古帝国2汉化版_10357.jar | 168 | Ancient Empires II | - | 1.0.2 | MIDP-1.0 | - | 10 | 16 | 600 | 479098 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| 速度与激情5中文版_3043.jar | 380 | Fast Five The Movie Game | - | 1.0.2 | MIDP-2.0 | nokia-ui | 25 | 16 | 600 | 4021268527 | 0 | - | 运行正常 |  |
| 速度与激情6_9.jar | 1917 | Fast & Furious 6 | - | 1.1.2 | MIDP-2.0 | - | 19 | 16 | 600 | 55158805 | 0 | java/lang/NullPointerException | 运行正常 |  |
| 都市摩天楼_6192.jar | 160 | Tower Bloxx(TM) | - | 1.2.4 | MIDP-1.0 | m3g,nokia-ui | 16 | 16 | 600 | 3955597 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| 都市摩天楼中文版_3219.jar | 1026 | 都市摩天楼 | - | 1.0.0 | MIDP-2.0 | nokia-ui | 46 | 16 | 479 | 109900363 | 0 | java/io/EOFException,javax/micro | 运行正常 | exited itself (notifyDestroyed) |
| 都市赛车3-暴力街头3D版_10199.jar | 285 | Asphalt 3 3D Street Rules | - | 1.0.2 | MIDP-2.0 | - | 5 | 16 | 600 | 4032684549 | 0 | javax/microedition/rms/InvalidRe | 运行正常 |  |
| 都市赛车3_10192.jar | 286 | Asphalt3: Street Rules | - | 1.0.4 | MIDP-2.0 | - | 5 | 16 | 600 | 3181590435 | 0 | javax/microedition/rms/InvalidRe | 运行正常 |  |
| 都市赛车6火线追击_2230.jar | 940 | Asphalt 6 Adrenaline | - | 1.0.0 | MIDP-2.0 | - | 21 | 16 | 601 | 20675020 | 0 | - | 运行正常 |  |
| 都市赛车_11517.jar | 223 | Asphalt | - | 1.0.7 | MIDP-2.0 | - | 5 | 16 | 600 | 1900922655 | 0 | javax/microedition/rms/InvalidRe | 运行正常 |  |
| 野人岛2宠物精灵_7061.jar | 534 | 宝软网baoruan.com免费分享 | - | 1.0 | MIDP-1.0 | nokia-ui | 24 | 16 | 600 | 3022722 | 0 | - | 运行正常 |  |
| 钻石狂潮_1335.jar | 293 | Diamond Rush | - | 1.1.7 | MIDP-2.0 | - | 12 | 16 | 600 | 8143284 | 0 | javax/microedition/rms/InvalidRe | 运行正常 |  |
| 铠甲勇士2-刑天_2895.jar | 1120 | 铠甲勇士2刑天 | - | 1.0.0 | MIDP-1.0 | - | 37 | 16 | 600 | 2635810883 | 0 | javax/microedition/rms/InvalidRe | 运行正常 |  |
| 闪电战机－雷霆出击_118.jar | 1388 | 闪电战机-雷霆出击 | - | 2.01 | MIDP-1.0 | - | 129 | 16 | 600 | 189333061 | 0 | java/io/IOException | 运行正常 |  |
| 阿尔卑斯牧场物语_7043.jar | 824 | 阿尔卑斯牧场物 | - | 1.0 | MIDP-1.0 | nokia-ui | 8 | 16 | 600 | 4067758 | 0 | java/lang/NullPointerException | 运行正常 |  |
| 雷欧历险记_11451.jar | 175 | FilaoFriedChicken | - | 01.03.08 | MIDP-1.0 | - | 11 | 16 | 600 | 15406976 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| 雷电2012末日浩劫增值版_3029.jar | 879 | 雷电2012末日浩劫 | - | 1.0.0 | MIDP-1.0 | - | 40 | 16 | 600 | 464642 | 0 | - | 运行正常 |  |
| 雷电4_39.jar | 545 | 雷电4 | - | 1.0 | MIDP-2.0 | - | 26 | 16 | 600 | 548007 | 0 | java/io/IOException | 运行正常 |  |
| 雷电战机(精装升级版)_3138.jar | 876 | 雷电战机(精装升级版) | - | 1.0.0 | MIDP-1.0 | - | 48 | 16 | 600 | 4429483825 | 0 | java/io/IOException,java/lang/Cl | 运行正常 |  |
| 雷电战机精装升级BT版_3110.jar | 869 | 雷电战机(精装升级版) | - | 1.0.0 | MIDP-1.0 | - | 48 | 16 | 600 | 4399523728 | 0 | java/io/IOException,java/lang/Cl | 运行正常 |  |
| 雷霆战机2012BT版_3101.jar | 851 | 雷霆战机2012 | - | 1.0.0 | MIDP-1.0 | - | 30 | 16 | 600 | 18537798 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| 鬼吹灯(网络火热)_7647.jar | 1007 | qcd | - | 1.0.0 | MIDP-1.0 | nokia-ui | 21 | 16 | 600 | 3409972780 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| 鬼泣3D中文版_3376.jar | 1118 | 鬼泣(CAPCOM正版) | - | 1.0.0 | MIDP-2.1 | mascot | 26 | 16 | 600 | 6123810 | 0 | java/io/EOFException | 运行正常 |  |
| 魂斗罗3代-完全版_3784.jar | 610 | 魂斗罗3代 | - | 1.0 | MIDP-1.0 | nokia-ui | 22 | 16 | 600 | 159841 | 0 | java/lang/NullPointerException | 运行正常 | runs without input; the generic key script m |
| 魂斗罗4中文版_2226.jar | 530 | 魂斗罗4 | - | 1.0.0 | MIDP-2.0 | nokia-ui | 56 | 16 | 600 | 1947826 | 0 | javax/microedition/rms/InvalidRe | 运行正常 |  |
| 魂斗罗V铁血兵团_119.jar | 990 | 魂斗罗V铁血兵团 | - | 1.0.0 | MIDP-1.0 | - | 68 | 16 | 600 | 4258777 | 0 | - | 运行正常 |  |
| 魂斗罗_5652.jar | 896 | 魂斗罗 | - | 1.0.0 | MIDP-2.0 | nokia-ui | 54 | 16 | 600 | 10696771704 | 0 | java/lang/NullPointerException | 运行正常 |  |
| 魔兽争霸_10622.jar | 59 | MSZB | - | 1.0 | MIDP-2.0 | nokia-ui | 22 | 16 | 600 | 2785641475 | 0 | - | 运行正常 |  |
| 魔兽塔防_12480.jar | 243 | 魔兽塔防 | - | 1.0 | MIDP-1.0 | - | 18 | 16 | 600 | 815773 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| 魔塔_8052.jar | 86 | 魔塔 | - | 1.00 | MIDP-1.0 | nokia-ui | 21 | 16 | 600 | 84547 | 0 | - | 运行正常 |  |
| 魔塔－英雄联盟_582.jar | 1451 | 魔塔-英雄联盟 | - | 3.11 | MIDP-1.0 | nokia-ui | 63 | 16 | 600 | 57961274 | 0 | java/lang/NullPointerException | 运行正常 |  |
| 魔法少女_10523.jar | 204 | 魔法少女 | - | 1.0 | MIDP-1.0 | - | 5 | 16 | 600 | 232485 | 0 | - | 运行正常 |  |
| 鲁宾逊漂流记汉化BT版_4677.jar | 279 | - | - | - | - | - | 0 | 16 | 0 | 0 | 0 | - | 非可用包 | not a zip file |
| 鲁宾逊漂流记汉化版_702.jar | 367 | 鲁宾逊漂流记-海难生存 | - | 1.0.0 | MIDP-2.0 | - | 34 | 16 | 600 | 6712947 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| 麻将三国_5157.jar | 732 | MJSG | - | 1.0 | MIDP-2.0 | nokia-ui | 38 | 16 | 600 | 630184 | 0 | java/io/EOFException,java/io/IOE | 运行正常 |  |
| 麻将无双之乱世群英_10298.jar | 173 | 麻将无双之乱世群英 | - | 1.0 | MIDP-1.0 | nokia-ui | 2 | 16 | 600 | 3502857359 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| 麻将象棋五子棋3合1_12014.jar | 311 | 麻将象棋五子棋3合1 | - | 1.0 | MIDP-1.0 | - | 8 | 16 | 600 | 2708485 | 0 | - | 运行正常 |  |
| 黑暗破坏神II_9009.jar | 84 | 黑暗破坏神II | - | 1.0 | MIDP-2.0 | - | 10 | 16 | 600 | 3049624399 | 0 | - | 运行正常 |  |
| 龙之谷之天空战记_5383.jar | 885 | 龙之谷之天空战记 | - | 1.0.0 | MIDP-2.0 | - | 60 | 16 | 600 | 4233478 | 0 | - | 运行正常 |  |
| 龙火2加强版_12437.jar | 117 | 龙火2加强版 | - | 1.0 | MIDP-1.0 | - | 22 | 16 | 600 | 1252629 | 0 | - | 运行正常 |  |
| 龙珠Z传说_9583.jar | 319 | 龙珠Z传说 | - | 1.0 | MIDP-1.0 | - | 8 | 16 | 321 | 333629 | 0 | - | 运行正常 | exited itself (notifyDestroyed) |
| 龙鸟战机_9762.jar | 345 | E-Brid | - | 1.0 | MIDP-1.0 | - | 13 | 16 | 600 | 229895261 | 0 | - | 运行正常 |  |
