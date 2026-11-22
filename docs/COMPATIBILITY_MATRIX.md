# Compatibility matrix

One row per JAR from the compatibility corpus (1817 titles): Fresh rows from the 2026-11-22 round-184 full sweep on the 0.5.30 CLI over tests/games plus the 646-file persistent corpus cache (1050 rows); the round-184 batch includes the Canvas-instance and Mascot AffineTrans fixes. Earlier batches from rounds 167-182 supply titles that are no longer cached. Rows overlay by file name; compare within a batch.

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

Built from `HEAD`, 2026-11-22 (recorded by `tools/sweep_compat.py`).

| verdict | titles |
| --- | --- |
| 运行正常 | 1799 |
| 标题自身异常 | 4 |
| 非可用包 | 4 |
| 标题自行退出 | 3 |
| 超时 | 3 |
| 失败 | 2 |
| 未呈现画面 | 2 |

| 3D API used | titles |
| --- | --- |
| - | 1462 |
| nokia-ui | 272 |
| m3g | 53 |
| m3g,nokia-ui | 23 |
| mascot | 7 |

| title | size KB | MIDlet name | vendor | version | profile | 3D | classes | heap MB | frames | bytecodes | uncaught | exceptions | verdict | note |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| (新)三国之关云长战曹贼_2550.jar | 934 | (新)三国之关云长战曹贼 | - | 1.0.0 | MIDP-1.0 | - | 21 | 16 | 600 | 4305643 | 0 | - | 运行正常 |  |
| 1942空战_10403.jar | 582 | 1942AirCombat supplied by ne | - | 1.0.17 | MIDP-2.0 | - | 14 | 16 | 600 | 2459460 | 0 | java/lang/ArrayIndexOutOfBoundsE | 运行正常 |  |
| 1945空战英雄_6105.jar | 588 | ACH1945 Lite | - | 1.0.0 | MIDP-2.0 | nokia-ui | 32 | 16 | 600 | 4737534 | 0 | - | 运行正常 | needs --device nokia6230 |
| 3D台球之王_5014.jar | 314 | 3D台球之王(EA正版) | - | 1.0.0 | MIDP-2.0 | - | 23 | 16 | 600 | 122441 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| 3D坦克对战1944_7139.jar | 345 | Tank Ace | - | 1.03 | MIDP-2.0 | - | 32 | 16 | 600 | 52860579 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| 3D城市桌球_8299.jar | 262 | Biliardo 3D Urban Tour | - | 1.5.9 | MIDP-1.0 | m3g | 4 | 16 | 600 | 2034167 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| 3D孤胆枪手2010_4634.jar | 985 | Alien Shooter 3D | - | 2.0 | MIDP-2.0 | nokia-ui | 19 | 16 | 600 | 233661830 | 0 | java/lang/ArrayIndexOutOfBoundsE | 运行正常 |  |
| 3D拉力赛-进化汉化版_6366.jar | 347 | 3DRallyEvolution | - | 1.0 | MIDP-2.0 | m3g | 12 | 16 | 600 | 429398 | 0 | javax/microedition/rms/InvalidRe | 运行正常 |  |
| 3D摩托赛车_9287.jar | 335 | 3DMotoRacing by Racer_Team | - | 1.0 | MIDP-2.0 | m3g | 12 | 16 | 600 | 835490 | 0 | java/lang/IllegalStateException, | 运行正常 |  |
| 3D武装直升机2_archive.jar | 80 | Attack Chopper 2 | - | 1.01 | MIDP-2.0 | - | 16 | 16 | 600 | 911 | 0 | java/io/IOException,java/lang/Nu | 运行正常 | painted 602 frames but made no draw calls (b |
| 3D滚球_11586.jar | 69 | 3D QBlast | - | 1.0.0 | MIDP-1.0 | nokia-ui | 19 | 16 | 600 | 2505949 | 0 | - | 运行正常 |  |
| 3D激爆拳击_8805.jar | 1024 | K.O. Legends | - | 1.0.8 | MIDP-2.0 | m3g,nokia-ui | 23 | 16 | 600 | 3986320348 | 0 | java/lang/NullPointerException | 运行正常 |  |
| 3D版田园物语_6744.jar | 969 | 3D版田园物语 | - | 1.0 | MIDP-2.0 | - | 65 | 16 | 600 | 25152247 | 0 | java/lang/NullPointerException | 运行正常 |  |
| 3D疯狂过山车S60v5版_3875.jar | 797 | Roller Rev 99 | - | 1.0.29 | MIDP-2.0 | - | 39 | 16 | 600 | 186009 | 0 | java/io/EOFException | 运行正常 |  |
| 3D科林麦克雷拉力赛之尘埃_6600.jar | 5601 | Colin McRae DiRT 3D_konno | - | 1.2.5 | MIDP-1.0 | m3g,nokia-ui | 39 | 16 | 600 | 19621048 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| 3D跳台滑雪2011_4507.jar | 964 | SkokiNarciarskie2011 | - | 1.1.0 | MIDP-2.0 | m3g | 91 | 16 | 600 | 7032024 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| Boun_en_ar_de_he-IL_ru-RU.jar | 40 | Bounce | - | 1.24 | MIDP-1.0 | nokia-ui | 8 | 16 | 600 | 5094 | 0 | - | 运行正常 | runs without input; the generic key script m |
| CCTV贺岁游戏-三国演义_6527.jar | 929 | 三国演义 | - | 1.0.0 | MIDP-1.0 | nokia-ui | 33 | 16 | 600 | 2557606870 | 0 | - | 运行正常 |  |
| CS反恐特警-狙击精英_6539.jar | 417 | (国际巨作)CS反恐特警-狙击精英 | - | 1.0.0 | MIDP-1.0 | - | 20 | 16 | 600 | 1859126 | 0 | java/lang/ArrayIndexOutOfBoundsE | 运行正常 |  |
| CS反恐特警真3D版2_6747.jar | 255 | CS反恐特警真3D版II | - | 1.0 | MIDP-1.0 | - | 34 | 16 | 600 | 308603545 | 0 | - | 运行正常 |  |
| CS反恐精英(蓝牙对战)_6858.jar | 335 | Micro Counter Strike v1.1 | - | 1.1.0 | MIDP-2.0 | m3g | 61 | 16 | 600 | 665 | 0 | - | 运行正常 | painted 600 frames but made no draw calls (b |
| FIFA09足球(EA中文版)_7962.jar | 695 | FIFA 09宝软网baoruan.com免费分享 | - | 1.0 | MIDP-2.0 | - | 36 | 16 | 600 | 753543 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| Flippy跳跃大冒险_archive.jar | 141 | Flippy's Flying Frenzy | - | 1.07 | MIDP-1.0 | nokia-ui | 13 | 16 | 600 | 1510802 | 0 | java/io/EOFException,javax/micro | 运行正常 |  |
| GT赛车2_36.jar | 1815 | GT Racing 2 | - | 1.0.9 | MIDP-2.0 | nokia-ui | 11 | 16 | 600 | 312029494 | 0 | java/lang/NullPointerException | 运行正常 |  |
| JBenchmark HD.jar | 343 | JBenchmark HD | - | 4.0.3 | MIDP-2.0 | m3g | 14 | 16 | 600 | 14596773 | 0 | - | 运行正常 | runs without input; the generic key script m |
| JBenchmark.jar | 25 | JBenchmark | - | 1.1.1 | MIDP-1.0 | - | 4 | 16 | 600 | 412886 | 0 | java/lang/ArithmeticException | 运行正常 |  |
| JBenchmark2.jar | 62 | JBenchmark2 | - | 2.1.1 | MIDP-2.0 | - | 6 | 16 | 600 | 9008686650 | 0 | java/lang/ArithmeticException,ja | 运行正常 |  |
| JBenchmark3D.jar | 80 | JBenchmark3D | - | 3.1.0 | MIDP-2.0 | m3g | 5 | 16 | 600 | 24580614 | 0 | - | 运行正常 |  |
| LOVExLOVE恋爱学院_4424.jar | 860 | LS | - | 01.30.00 | MIDP-2.0 | - | 75 | 16 | 600 | 560011 | 0 | - | 运行正常 |  |
| MinecraftJ2ME_PE_0.6.1_V2.2.jar | 1214 | MinecraftJ2ME_PE_0.6.1 | - | 0.6.1 | MIDP-2.0 | m3g,nokia-ui | 170 | 16 | 600 | 6177610659 | 0 | javax/microedition/io/Connection | 运行正常 |  |
| NBA2010中文版_6247.jar | 849 | NBA 10（EA正版） | - | 1.0.0 | MIDP-2.0 | - | 35 | 16 | 771 | 7762189456 | 0 | javax/microedition/rms/InvalidRe | 运行正常 |  |
| NBA2010授权正版_5060.jar | 826 | NBA2010（授权正版） | - | 1.0.0 | MIDP-2.0 | - | 13 | 16 | 659 | 10405002 | 0 | - | 运行正常 |  |
| NBA篮球2007_10887.jar | 298 | NBA LIVE 07 | - | 4.2.23 | MIDP-2.0 | - | 86 | 16 | 1060 | 12822321 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| NBA职业篮球2010豪华版_7111.jar | 841 | NBA Pro Basketball 2010 | - | 1.0.8 | MIDP-2.0 | - | 9 | 16 | 600 | 4313681360 | 0 | java/lang/NullPointerException | 运行正常 |  |
| PC正版-使命召唤5_7199.jar | 407 | PC正版-使命召唤5 | - | 1.0.0 | MIDP-2.0 | - | 21 | 16 | 600 | 12093662 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| QQ飞车2010_5772.jar | 457 | QQ2010 | - | 1.0 | MIDP-2.0 | - | 30 | 16 | 600 | 3427830212 | 0 | java/io/EOFException | 运行正常 |  |
| Vime_en_ar_de_he-IL_ru-RU.jar | 62 | Virtual Me | - | 0.17 | MIDP-1.0 | nokia-ui | 9 | 16 | 600 | 838717 | 0 | javax/microedition/rms/InvalidRe | 运行正常 | runs without input; the generic key script m |
| book4.jar | 20 | J2ME Games Examples | - | 2.0 | MIDP-2.0 | - | 10 | 16 | 600 | 146 | 0 | - | 运行正常 | runs without input; the generic key script m |
| book5.jar | 29 | J2ME Games Examples | - | 2.0 | MIDP-2.0 | - | 14 | 16 | 600 | 150 | 0 | - | 运行正常 | runs without input; the generic key script m |
| book6.jar | 38 | J2ME Games Examples | - | 2.0 | MIDP-2.0 | - | 16 | 16 | 600 | 162 | 0 | - | 运行正常 | runs without input; the generic key script m |
| book7.jar | 39 | J2ME Games Examples | - | 2.0 | MIDP-2.0 | - | 17 | 16 | 600 | 162 | 0 | - | 运行正常 | runs without input; the generic key script m |
| bubblet.jar | 27 | Bubblet | - | 1.0.0 | - | nokia-ui | 14 | 16 | 600 | 937883 | 0 | - | 运行正常 |  |
| game15.jar | 3 | Game15MIDlet | - | 1.0.0 | - | - | 2 | 16 | 600 | 120056 | 0 | - | 运行正常 |  |
| hello.jar | 2 | Hello World | - | 2.0 | MIDP-2.0 | - | 2 | 16 | 600 | 9582 | 0 | - | 运行正常 |  |
| jump.jar | 15 | J2ME Games Examples | - | 2.0 | MIDP-2.0 | - | 7 | 16 | 600 | 138 | 0 | - | 运行正常 | runs without input; the generic key script m |
| maze.jar | 7 | J2ME Games Examples | - | 1.0 | MIDP-1.0 | - | 4 | 16 | 600 | 863164 | 0 | - | 运行正常 |  |
| slimevolley.jar | 8 | SlimeVolleyBallMidlet | - | 1.0.0 | - | - | 7 | 16 | 470 | 167153 | 0 | - | 运行正常 | exited itself (notifyDestroyed) |
| vault/(128x128) SolaRola 1.0.3 5716 v2.jar | 233 | SolaRola | - | 1.0.3 | MIDP-2.0 | - | 2 | 16 | 600 | 3780262 | 0 | javax/microedition/rms/InvalidRe | 运行正常 |  |
| vault/(128x128) SolaRola 1.0.3 5716.jar | 62 | SolaRola | - | 1.0.3 | MIDP-1.0 | nokia-ui | 2 | 16 | 600 | 773958 | 0 | javax/microedition/rms/InvalidRe | 运行正常 |  |
| vault/(128x128) SolaRola 1.0.3 5751.jar | 117 | SolaRola | - | 1.0.3 | MIDP-2.0 | nokia-ui | 2 | 16 | 600 | 1683453 | 0 | javax/microedition/rms/InvalidRe | 运行正常 |  |
| vault/(128x160) SolaRola 1.0.3 5716 v2.jar | 233 | SolaRola | - | 1.0.3 | MIDP-2.0 | - | 2 | 16 | 600 | 3423225 | 0 | javax/microedition/rms/InvalidRe | 运行正常 |  |
| vault/(128x160) SolaRola 1.0.3 5716.jar | 112 | SolaRola | - | 1.0.3 | MIDP-2.0 | nokia-ui | 2 | 16 | 600 | 2022689 | 0 | javax/microedition/rms/InvalidRe | 运行正常 |  |
| vault/(128x160) SolaRola 1.0.3 5745.jar | 98 | SolaRola | - | 1.0.3 | MIDP-2.0 | - | 2 | 16 | 600 | 2077583 | 0 | javax/microedition/rms/InvalidRe | 运行正常 |  |
| vault/(128x160) SolaRola 1.0.4 5856.jar | 232 | SolaRola | - | 1.0.4 | MIDP-2.0 | - | 2 | 16 | 600 | 2619778 | 0 | javax/microedition/rms/InvalidRe | 运行正常 |  |
| vault/(176x204) SolaRola 1.0.3 5733.jar | 169 | Sola Rola | - | 1.0.3 | MIDP-2.0 | - | 2 | 16 | 600 | 2002291 | 0 | javax/microedition/rms/InvalidRe | 运行正常 |  |
| vault/(176x208) SolaRola 1.0.3 5716.jar | 417 | SolaRola | - | 1.0.3 | MIDP-2.0 | nokia-ui | 2 | 16 | 600 | 2257065 | 0 | javax/microedition/rms/InvalidRe | 运行正常 |  |
| vault/(176x208) SolaRola 1.0.3 5725 v2.jar | 238 | SolaRola | - | 1.0.3 | MIDP-2.0 | nokia-ui | 2 | 16 | 600 | 3724194 | 0 | javax/microedition/rms/InvalidRe | 运行正常 |  |
| vault/(176x208) SolaRola 1.0.3 5725.jar | 228 | SolaRola | - | 1.0.3 | MIDP-2.0 | nokia-ui | 2 | 16 | 600 | 2298705 | 0 | javax/microedition/rms/InvalidRe | 运行正常 |  |
| vault/(176x208) SolaRola 1.0.4 5831.jar | 201 | SolaRola | - | 1.0.4 | MIDP-1.0 | nokia-ui | 2 | 16 | 600 | 3521550 | 0 | javax/microedition/rms/InvalidRe | 运行正常 |  |
| vault/(176x220) SolaRola 1.0.3 5716 v2.jar | 417 | SolaRola | - | 1.0.3 | MIDP-2.0 | - | 2 | 16 | 600 | 3725490 | 0 | javax/microedition/rms/InvalidRe | 运行正常 |  |
| vault/(176x220) SolaRola 1.0.3 5716.jar | 237 | SolaRola | - | 1.0.3 | MIDP-2.0 | - | 2 | 16 | 600 | 3707181 | 0 | javax/microedition/rms/InvalidRe | 运行正常 |  |
| vault/(176x220) SolaRola 1.0.4 5733.jar | 237 | SolaRola | - | 1.0.3 | MIDP-2.0 | - | 2 | 16 | 600 | 2090142 | 0 | javax/microedition/rms/InvalidRe | 运行正常 |  |
| vault/(176x220) SolaRola 1.0.4 5814.jar | 163 | SolaRola | - | 1.0.4 | MIDP-2.0 | - | 2 | 16 | 600 | 2942753 | 0 | javax/microedition/rms/InvalidRe | 运行正常 |  |
| vault/(208x208) SolaRola 1.0.3 5716.jar | 231 | SolaRola | - | 1.0.3 | MIDP-2.0 | - | 2 | 16 | 600 | 3636654 | 0 | javax/microedition/rms/InvalidRe | 运行正常 |  |
| vault/(240x320) SolaRola 1.0.3 5716 v2.jar | 237 | SolaRola | - | 1.0.3 | MIDP-2.0 | - | 2 | 16 | 600 | 3555442 | 0 | javax/microedition/rms/InvalidRe | 运行正常 |  |
| vault/(240x320) SolaRola 1.0.3 5716.jar | 417 | SolaRola | - | 1.0.3 | MIDP-2.0 | - | 2 | 16 | 600 | 4219489 | 0 | javax/microedition/rms/InvalidRe | 运行正常 |  |
| vault/(240x320) SolaRola 1.0.3 5725 v2.jar | 231 | SolaRola | - | 1.0.3 | MIDP-2.0 | nokia-ui | 2 | 16 | 600 | 3949121 | 0 | javax/microedition/rms/InvalidRe | 运行正常 |  |
| vault/(240x320) SolaRola 1.0.3 5725 v3.jar | 418 | SolaRola | - | 1.0.3 | MIDP-2.0 | nokia-ui | 2 | 16 | 600 | 4256280 | 0 | javax/microedition/rms/InvalidRe | 运行正常 |  |
| vault/(240x320) SolaRola 1.0.3 5725.jar | 417 | SolaRola | - | 1.0.3 | MIDP-2.0 | nokia-ui | 2 | 16 | 600 | 4075582 | 0 | javax/microedition/rms/InvalidRe | 运行正常 |  |
| vault/(240x320) SolaRola 1.0.3 5764.jar | 234 | SolaRola | - | 1.0.3 | MIDP-2.0 | - | 2 | 16 | 600 | 2320133 | 0 | javax/microedition/rms/InvalidRe | 运行正常 |  |
| vault/(240x320) SolaRola 1.0.4 5888.jar | 417 | SolaRola | - | 1.0.4 | MIDP-2.0 | nokia-ui | 2 | 16 | 600 | 4069527 | 0 | javax/microedition/rms/InvalidRe | 运行正常 |  |
| vault/(320x240) SolaRola 1.0.3 5725.jar | 417 | SolaRola | - | 1.0.3 | MIDP-2.0 | nokia-ui | 2 | 16 | 600 | 3645420 | 0 | javax/microedition/rms/InvalidRe | 运行正常 |  |
| vault/(352x416) SolaRola 1.0.4 5831.jar | 417 | SolaRola | - | 1.0.4 | MIDP-2.0 | nokia-ui | 2 | 16 | 600 | 2571332 | 0 | javax/microedition/rms/InvalidRe | 运行正常 |  |
| vault/000-Tomb Raider The Osiris Codex v1.0.9 (176x208) [English].jar | 134 | TombRaider1 | - | 1.0.9 | MIDP-1.0 | - | 5 | 16 | 600 | 1006636 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/001-Tomb Raider The Osiris Codex v1.1.3 (128x128) [German].jar | 63 | Tomb Raider The Osiris Codex | - | 1.1.3 | MIDP-1.0 | - | 6 | 16 | 600 | 3539767 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/002-Tomb Raider The Osiris Codex v1.1.3 (176x208) [German].jar | 135 | TombRaider1 | - | 1.1.3 | MIDP-1.0 | - | 5 | 16 | 600 | 466028 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/003-Tomb Raider The Osiris Codex v1.1.4 (176x208) [Italian].jar | 135 | TombRaider1 | - | 1.1.4 | MIDP-1.0 | - | 5 | 16 | 601 | 514540 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/004-Tomb Raider The Osiris Codex v1.1.8 (176x208) [Spanish].jar | 93 | TombRaider1 | - | 1.1.8 | MIDP-1.0 | - | 5 | 16 | 600 | 834858 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/005-Tomb Raider The Osiris Codex v1.2.5 (128x128) [Chinese A].jar | 58 | 地狱书上 | - | 1.2.5 | MIDP-1.0 | - | 6 | 16 | 600 | 2172757 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/006-Tomb Raider The Osiris Codex v1.2.5 (128x128) [Chinese B].jar | 58 | 地狱书下 | - | 1.2.5 | MIDP-1.0 | - | 6 | 16 | 600 | 2112289 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/007 - Quantum of Solace.jar | 584 | Quantum Of Solace | - | 1.0.6 | MIDP-1.0 | - | 7 | 16 | 600 | 62759612 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/007-Tomb Raider The Osiris Codex v1.5.6 (240x320) [English].jar | 118 | TombRaider1 | - | 1.5.6 | MIDP-1.0 | - | 6 | 16 | 601 | 208905 | 0 | - | 运行正常 |  |
| vault/007_Casino_Royale_Rus.jar | 213 | Casino Royale | - | 0.23.7 | MIDP-2.0 | - | 5 | 16 | 600 | 1175764 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
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
| vault/09_Formula_Extreme_(Gameleons_2008)_n70 - Mastiwap.Com.jar | 208 | 09 Formula Extreme | - | 1.0.1 | MIDP-2.0 | nokia-ui | 5 | 16 | 600 | 7667514 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/1 On 1 Basket.jar | 173 | 1 On 1 Basket | - | 1.0.16 | MIDP-1.0 | - | 3 | 16 | 600 | 134324042 | 0 | - | 运行正常 |  |
| vault/100-Tomb Raider Underworld 3D v1.7.1 (CLDC-1.0, MIDP-1.0) (Nokia N96) (240x320).jar | 1081 | TR Underworld | - | 1.7.1 | MIDP-1.0 | - | 7 | 16 | 600 | 12858162 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/101-Tomb Raider Underworld 3D v1.7.1 (CLDC-1.0, MIDP-1.0) (s60v2-N70) (176x208).jar | 1007 | TR Underworld | - | 1.7.1 | MIDP-1.0 | - | 7 | 16 | 600 | 13217797 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/102-Tomb Raider Underworld 3D v1.7.2 (176x208).jar | 1069 | TR Underworld | - | 1.7.2 | MIDP-1.0 | - | 8 | 16 | 600 | 12786370 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/103-Tomb Raider Underworld 3D v1.7.2 (320x240).jar | 1073 | TR Underworld | - | 1.7.2 | MIDP-1.0 | - | 7 | 16 | 600 | 13499048 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/104-Tomb Raider Underworld 3D v1.7.3 (CLDC-1.0, MIDP-1.0) (Sony Ericsson K810) (240x320).jar | 1145 | TR Underworld | - | 1.7.3 | MIDP-1.0 | - | 7 | 16 | 600 | 12994739 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/105-Tomb Raider Underworld 3D v1.8.8 (CLDC-1.0, MIDP-1.0) (Nokia 5800) (360x640).jar | 1304 | TR Underworld | - | 1.8.8 | MIDP-1.0 | - | 7 | 16 | 600 | 13853244 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/1100 A.D. - Battle of Legends.jar | 390 | 1100 AD. Battle of Legends | - | 1.0.0 | MIDP-2.0 | - | 43 | 16 | 600 | 937055 | 0 | - | 运行正常 |  |
| vault/1916 Dogfight.jar | 376 | 1916 Dogfight | - | 1.0.0 | MIDP-2.0 | - | 39 | 16 | 600 | 47128616 | 0 | java/io/EOFException,java/lang/N | 运行正常 |  |
| vault/1942.jar | 400 | 1942 | - | 1.0.0 | MIDP-2.0 | - | 18 | 16 | 600 | 13764818 | 0 | - | 运行正常 |  |
| vault/1943 Sky War.jar | 138 | 1943 SkyWar | - | 1.0 | MIDP-1.0 | - | 16 | 16 | 600 | 3045231 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/1_temple_flee_temple_158578.jar | 997 | 神庙逃亡 | - | 1.0.0 | MIDP-2.0 | - | 32 | 16 | 600 | 2025616 | 0 | java/io/IOException | 运行正常 |  |
| vault/2004 Real Football.jar | 189 | 2004 Real Football | - | 1.0.0 | MIDP-2.0 | - | 9 | 16 | 600 | 214675124 | 0 | - | 运行正常 |  |
| vault/2005 Real Football.jar | 201 | 2005RealFootball | - | 1.1.5 | MIDP-1.0 | - | 6 | 16 | 600 | 1861217 | 0 | - | 运行正常 |  |
| vault/2006 Real Football 3D.jar | 753 | 2006RealFootball3D | - | 1.0.7 | MIDP-2.0 | - | 7 | 16 | 602 | 2983997402 | 0 | java/lang/NumberFormatException | 运行正常 |  |
| vault/2006 Real Football.jar | 308 | 2006 Real Football | - | 1.1.0 | MIDP-2.0 | - | 6 | 16 | 600 | 17201678 | 0 | - | 运行正常 |  |
| vault/2010FIFAWorldCup_4.4.11_320x240.jar | 777 | 2010 FIFA World Cup | - | 4.4.11 | MIDP-2.0 | - | 31 | 16 | 600 | 8870261 | 0 | - | 运行正常 |  |
| vault/2012 Apocalypse.jar | 449 | 2012 Apocalypse | - | 1.0.0 | MIDP-1.0 | - | 48 | 16 | 600 | 19279792 | 0 | - | 运行正常 |  |
| vault/24 - Jack Bauer.jar | 591 | 24 Jack Bauer | - | 2.0.5 | MIDP-2.0 | - | 20 | 16 | 600 | 11657663 | 0 | java/io/EOFException,javax/micro | 运行正常 |  |
| vault/24 - Special Ops.jar | 302 | 24 Special Ops | - | 1.0.28 | MIDP-2.0 | - | 13 | 16 | 600 | 14303970 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/24 Agent Down.jar | 214 | 24 - Agent Down | - | 0.4.6 | MIDP-2.0 | - | 23 | 16 | 600 | 1754671 | 0 | java/io/IOException,java/lang/Nu | 运行正常 |  |
| vault/24-Agent_Down-j2me-samsung-(240x320) - Mastiwap.Com.jar | 215 | 24 - Agent Down | - | 0.4.7 | MIDP-2.0 | - | 24 | 16 | 600 | 1712869 | 0 | java/io/IOException,java/lang/Nu | 运行正常 |  |
| vault/24_Special_Ops_S40_2ed_6230i_8800.jar | 211 | 24 Special Ops | - | 1.0.31 | MIDP-2.0 | nokia-ui | 12 | 16 | 600 | 17659834 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/24_tm-Agent_Down_240x320.jar | 214 | 24 - Agent Down | - | 0.4.6 | MIDP-2.0 | - | 23 | 16 | 600 | 1743267 | 0 | java/io/IOException,java/lang/Nu | 运行正常 |  |
| vault/2890_Stalker_Last_Wi.jar | 1712 | STALKER 3D Last Wish | - | 1.0 | MIDP-2.0 | - | 54 | 16 | 600 | 120801205 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/2933_horror.jar | 661 | Hysteric by -Spirit- | - | 1.0 | MIDP-2.0 | - | 54 | 16 | 600 | 110625220 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/2972_Dead_Shock.jar | 544 | Dead Shock | - | 0.1.78 | MIDP-2.0 | - | 82 | 16 | 600 | 460894371 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/2in1MahjongGames_1.0.0_320x240.jar | 446 | 2in1 Mahjong Games | - | 2.0 | MIDP-2.0 | - | 65 | 16 | 600 | 4570581331 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/3 In 1 sheep.jar | 217 | iGame 3in1Sheep | - | 1.0 | MIDP-2.0 | - | 9 | 16 | 600 | 987088 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/3029_Death_Night.jar | 930 | Death Night | - | 1.0 | MIDP-2.0 | - | 5 | 16 | 600 | 558948791 | 0 | - | 运行正常 |  |
| vault/3050_Dead_Space.jar | 1260 | Dead Space Mobile 3D | - | 1.0 | MIDP-2.0 | - | 54 | 16 | 600 | 115378702 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/3067_Horrors_new.jar | 1709 | Horrors | - | 1.0 | MIDP-2.0 | - | 54 | 16 | 600 | 308383310 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/3161_Silent_Evil_201.jar | 2607 | Silent Evil BETA | - | 1.5 | MIDP-2.0 | - | 68 | 16 | 600 | 3141827938 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/3210_Meat2Eat_II.jar | 422 | Резня 3D.Часть 2 | - | 1.0 | MIDP-2.0 | - | 55 | 16 | 600 | 194251221 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/3220_Raccoon_City.jar | 1515 | RE:Racoon City | - | 1.0.3 | MIDP-2.0 | - | 14 | 16 | 601 | 4793638 | 0 | java/lang/ArrayIndexOutOfBoundsE | 运行正常 |  |
| vault/3235_Into_The_Dead.jar | 1141 | Into the Dead | - | 1.0.0 | MIDP-2.0 | - | 10 | 16 | 601 | 2849756 | 0 | java/lang/NumberFormatException | 运行正常 |  |
| vault/3259_Not_Alone.jar | 286 | Not alone | - | 1.0 | MIDP-2.0 | - | 13 | 16 | 600 | 6649763 | 0 | - | 运行正常 |  |
| vault/3267_Manhunt.jar | 714 | Manhunt: Prison Massacre | - | 1.0 | MIDP-1.0 | - | 5 | 16 | 600 | 183205 | 0 | java/lang/NullPointerException,j | 运行正常 | runs without input; the generic key script m |
| vault/365 Solitaire Gold 12in1-Lite.jar | 217 | 365 Solitaire Gold | - | 0.0.1 | MIDP-2.0 | - | 19 | 16 | 600 | 29743103 | 0 | java/lang/IllegalArgumentExcepti | 运行正常 |  |
| vault/365PuzzleClub.jar | 371 | 365 Puzzle Club | - | 1.5.0 | MIDP-2.0 | - | 9 | 16 | 600 | 16076196 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/365PuzzleClub2.jar | 456 | 365 Puzzle Club | - | 0.0.6 | MIDP-2.0 | - | 8 | 16 | 600 | 24144985 | 0 | - | 运行正常 |  |
| vault/3D Alpha Wing EX.jar | 207 | 沙罗蔓蛇EX | - | 1.0.9 | MIDP-1.0 | - | 16 | 16 | 600 | 1011598 | 0 | java/lang/IllegalArgumentExcepti | 运行正常 |  |
| vault/3D Archery.jar | 221 | 3D Archery | - | 1.0 | MIDP-2.0 | m3g | 12 | 16 | 470 | 365951 | 0 | javax/microedition/rms/RecordSto | 运行正常 | exited itself (notifyDestroyed) |
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
| vault/3DBrickBreakerRevolution.jar | 403 | 3D Brick Breaker | - | 1.0.0 | MIDP-2.0 | m3g | 21 | 16 | 600 | 46959094 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/3DBrickBreakerRevolution2.jar | 578 | 3DBrickBreak2 by BerON | - | 1.0.3 | MIDP-2.0 | m3g | 29 | 16 | 600 | 55284223 | 0 | java/io/EOFException,javax/micro | 运行正常 |  |
| vault/3DBrickBreakerRevolution_1.0.0_320x240.jar | 403 | 3D Brick Breaker | - | 1.0.0 | MIDP-2.0 | m3g,nokia-ui | 21 | 16 | 600 | 49007277 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/3DKamasutra.jar | 207 | 3D Kamasutra Trivia | - | 1.1.3 | MIDP-2.0 | m3g | 13 | 16 | 600 | 2190038 | 0 | - | 运行正常 |  |
| vault/3DPingPong.jar | 210 | Ping Pong | - | 1.0.0 | MIDP-2.0 | m3g,nokia-ui | 27 | 16 | 600 | 1129487 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/3D_Autobahn_Raser_World_Challenge_S40_2ed_6230i_8800_208x208.jar | 215 | 3D Autobahn Raser WC | - | 1.1.0 | MIDP-1.0 | m3g,nokia-ui | 34 | 16 | 600 | 3298456 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/3D_Autobahn_Raser_World_Challenge_S60_5F3ed_N80_E60_352x416.jar | 225 | 3D Autobahn Raser WC | - | 1.1.0 | MIDP-1.0 | m3g,nokia-ui | 34 | 16 | 600 | 369830505 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/3D_Beach_Mini_Golf.jar | 215 | 3D Beach Mini Golf | - | 1.4.01 | MIDP-1.0 | m3g | 55 | 16 | 600 | 343026 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/3D_Superball_(IndiaGames).jar | 221 | 3D SuperBall | - | 1.1.4 | MIDP-2.0 | m3g | 34 | 16 | 600 | 3115084 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/3G.Entertainment.-.Manga.Babes.Trisha's.Seducing.Dance.SE.176x220 - Mastiwap.Com.jar | 211 | Trisha | - | 1.0.0 | MIDP-1.0 | nokia-ui | 6 | 16 | 470 | 335997 | 0 | - | 运行正常 | exited itself (notifyDestroyed) |
| vault/3StyleSnowboard_N70 - Mastiwap.Com.jar | 211 | 3Style Snowboarding | - | 1.0.1 | MIDP-2.0 | nokia-ui | 10 | 16 | 600 | 6874321 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/3d_brick_breaker_revolution_360x640_n97-296782.jar | 420 | 3D Brick Breaker | - | 1.0.0 | MIDP-2.0 | m3g,nokia-ui | 21 | 16 | 600 | 27921957 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/3dbrickbre_lcdvclkl.jar | 566 | 3DBrickBreak2 by Stox | - | 1.0.4 | MIDP-2.0 | m3g | 29 | 16 | 600 | 87228096 | 0 | java/io/EOFException,javax/micro | 运行正常 |  |
| vault/4 Wheel Xtreme 3D.jar | 313 | 4 Wheel Xtreme 3D | - | 0.5.15 | MIDP-2.0 | - | 56 | 16 | 600 | 18317558 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/4 Wheel Xtreme.jar | 237 | 4 Wheel Xtreme | - | 0.7.0 | MIDP-2.0 | - | 19 | 16 | 600 | 1441541 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/4in1 Memory.jar | 213 | 4 in 1 | - | 1.0 | MIDP-2.0 | - | 6 | 16 | 600 | 775192 | 1 | java/io/EOFException,java/lang/N | 标题自身异常 | game code threw java.lang.NullPointerExcepti |
| vault/4x4 Extreme Offroad-Lite.jar | 214 | 4x4 Extreme OffRoad | - | 1.0.0 | MIDP-2.0 | - | 80 | 16 | 600 | 2251866761 | 0 | java/io/IOException,java/lang/Nu | 运行正常 |  |
| vault/4x4 Extreme Rally - World Tour.jar | 378 | 4x4 Extreme Rally World Tour | - | 1.0.2 | MIDP-2.0 | - | 9 | 16 | 600 | 614280 | 0 | java/io/IOException,java/lang/Nu | 运行正常 |  |
| vault/4x4 Extreme Rally 2006.jar | 154 | 4x4 Extreme Rally 2006 | - | 1.0.3 | MIDP-2.0 | - | 10 | 16 | 600 | 10220652 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/4x4 Extreme Rally 3D.jar | 207 | 4x4 Extreme Rally 3D | - | 1.0.2 | MIDP-2.0 | - | 11 | 16 | 600 | 10052309 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/4x4ExtremeRally3D - Mastiwap.Com.jar | 216 | 4x4 Extreme Rally 3D | - | 1.0.2 | MIDP-2.0 | m3g,nokia-ui | 23 | 16 | 600 | 14154734 | 0 | java/io/IOException,java/lang/Nu | 运行正常 |  |
| vault/4x4_Extreme_Rally_3D_240x320 - Mastiwap.Com.jar | 207 | 4x4 Extreme Rally 3D | - | 1.0.2 | MIDP-2.0 | m3g,nokia-ui | 11 | 16 | 600 | 15140300 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/4x4_Extreme_Rally_3D_Samsung_240x320.jar | 207 | 4x4 Extreme Rally 3D | - | 1.0.2 | MIDP-2.0 | m3g,nokia-ui | 11 | 16 | 600 | 15078896 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/50cent_Get_Rich.jar | 221 | Get Rich or Die Tryin' | - | 1.0.6 | MIDP-1.0 | - | 8 | 16 | 600 | 963035 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/639_dead_space_3d.jar | 998 | Dead space 3D | - | 1.0.0 | MIDP-1.0 | - | 121 | 16 | 600 | 537428 | 0 | - | 运行正常 |  |
| vault/7 Wonders.jar | 309 | 7 Wonders | - | 1.0.9 | MIDP-2.0 | - | 4 | 16 | 600 | 769455347 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/A Dog's Adventure.jar | 264 | A Dog's Adventure | - | 1.0.4 | MIDP-2.0 | - | 9 | 16 | 600 | 30770 | 0 | - | 运行正常 |  |
| vault/A Good Day to Die Hard.jar | 1593 | A Good Day To Die Hard DEDOM | - | 1.1.1 | MIDP-2.0 | - | 9 | 16 | 600 | 165599089 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/A.M.A.-.IQ.Booster..SE.176x220 - Mastiwap.Com.jar | 204 | AMA IQ Booster | - | 1.0.0 | MIDP-2.0 | - | 15 | 16 | 600 | 11506421 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/AD1066-WilliamTheConquerorCZ_s40v3 - Mastiwap.Com.jar | 225 | AD1066 | - | 1.0 | MIDP-2.0 | - | 4 | 16 | 600 | 44745138 | 0 | java/io/IOException,java/lang/Il | 运行正常 |  |
| vault/AD1066_1.0_320x240.jar | 324 | AD1066 | - | 1.0 | MIDP-2.0 | - | 42 | 16 | 600 | 24682215 | 0 | java/io/IOException,java/lang/Ar | 运行正常 |  |
| vault/AMA IQ BOOST.jar | 221 | AMA IQ Booster | - | 1.0.1 | MIDP-2.0 | - | 15 | 16 | 600 | 11369958 | 0 | - | 运行正常 |  |
| vault/AMA.IQ.Booster_s40v3 - Mastiwap.Com.jar | 218 | AMA IQ Booster | - | 1.0.6 | MIDP-1.0 | - | 21 | 16 | 600 | 7138128 | 0 | - | 运行正常 |  |
| vault/AMALoveTest_s40v3 - Mastiwap.Com.jar | 222 | AMA Love Test | - | 1.0.0 | MIDP-2.0 | - | 12 | 16 | 600 | 3537330 | 0 | - | 运行正常 |  |
| vault/AMALoveTriangleCZ_s40v3a - Mastiwap.Com.jar | 214 | AMA Milostny trojuhelnik | - | 2.6.1 | MIDP-2.0 | - | 21 | 16 | 600 | 1411468 | 0 | javax/microedition/rms/InvalidRe | 运行正常 |  |
| vault/AMAPerfectBodyDiet_s40v2a - Mastiwap.Com.jar | 210 | AMA Perfect Body Diet | - | 1.0.3 | MIDP-1.0 | nokia-ui | 20 | 16 | 600 | 3973171 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/AMA_IQ_Booster-j2me-samsung-(240x320) - Mastiwap.Com.jar | 222 | AMA IQ Booster | - | 1.0.2 | MIDP-2.0 | - | 15 | 16 | 600 | 11136273 | 0 | - | 运行正常 |  |
| vault/AMA_IQ_Booster_Samsung_240x320.jar | 220 | AMA IQ Booster | - | 1.0.0 | MIDP-2.0 | - | 15 | 16 | 600 | 11365954 | 0 | - | 运行正常 |  |
| vault/AMF_Xtreme_Bowling.jar | 221 | - | - | - | - | m3g | 44 | 16 | 0 | 0 | 0 | - | 非可用包 | not a usable MIDlet suite (bad JAR or no MID |
| vault/AND1 Streetball.jar | 298 | AND1 Street Basketball | - | 1.0.7 | MIDP-2.0 | - | 4 | 16 | 600 | 267309418 | 0 | - | 运行正常 |  |
| vault/AOEIIIAsianDynasties_1.0.0_320x240.jar | 610 | AOE III Asian Dynasties | - | 1.0.0 | MIDP-2.0 | - | 41 | 16 | 600 | 10614894 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/Abracadaball.jar | 581 | Abracadaball | - | 1.0.6 | MIDP-2.0 | - | 5 | 16 | 600 | 192824135 | 0 | java/lang/ArrayIndexOutOfBoundsE | 运行正常 |  |
| vault/Abracadaball_1.0.2_320x240.jar | 472 | Abracadaball | - | 1.0.2 | MIDP-2.0 | - | 5 | 16 | 600 | 24754410 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/Absolute Blockers Deluxe.jar | 99 | Blockers Deluxe | - | 1.1.15 | MIDP-2.0 | - | 19 | 16 | 600 | 483935 | 0 | java/lang/NumberFormatException, | 运行正常 |  |
| vault/AbsoluteSummerSports_s40v3 - Mastiwap.Com.jar | 208 | Absolute Summer Sports | - | 1.0.1 | MIDP-2.0 | - | 24 | 16 | 600 | 4330199 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/Absolute_Summer_Sports_(176x208) - Mastiwap.Com.jar | 208 | Absolute Summer Sports | - | 1.0.1 | MIDP-2.0 | - | 24 | 16 | 600 | 4887998 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/Absolute_Summer_Sports_S60_5F3ed_5500.jar | 208 | Absolute Summer Sports | - | 1.0.1 | MIDP-2.0 | - | 24 | 16 | 600 | 4610277 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/Accorta Institute KeepIn Head.jar | 204 | Keepinhead | - | 1.6.5 | MIDP-1.0 | - | 18 | 16 | 600 | 2377668 | 0 | - | 运行正常 |  |
| vault/Ace Combat - Northern Wings.jar | 848 | Ace Combat | - | 1.0.0 | MIDP-2.0 | - | 8 | 16 | 600 | 11625921 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/Aces of the Luftwaffe 2.jar | 619 | Aces Of The Luftwaffe 2 | - | 1.0 | MIDP-2.0 | - | 17 | 16 | 600 | 2470024 | 0 | java/io/IOException,javax/microe | 运行正常 |  |
| vault/Aces of the Luftwaffe.jar | 444 | Aces Of The Luftwaffe | - | 1.0 | MIDP-2.0 | - | 6 | 16 | 600 | 1986863 | 0 | java/io/IOException,javax/microe | 运行正常 |  |
| vault/Activision Anthology.jar | 210 | ActivisionAnthology | - | 0.2.7 | MIDP-2.0 | nokia-ui | 11 | 16 | 600 | 18287715 | 0 | java/io/IOException,java/lang/Nu | 运行正常 |  |
| vault/ActivisionAnthology.jar | 173 | Activision Anthology  (by GD | - | 01.00.01 | MIDP-2.0 | - | 4 | 16 | 600 | 6660272 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/Advanced Brain Trainer 2__320x240.jar | 293 | Advanced Brain Trainer 2 | - | 00.00.29 | MIDP-2.0 | nokia-ui | 25 | 16 | 600 | 6335755 | 0 | java/lang/ArrayIndexOutOfBoundsE | 运行正常 |  |
| vault/Adventure Time - Heroes of OOO.jar | 320 | Adventure Time | - | 1.0.2 | MIDP-2.0 | - | 12 | 16 | 600 | 1624009 | 0 | - | 运行正常 |  |
| vault/Age of Ampire3 - Mastiwap.Com.jar | 208 | Age Of Empires III Mobile | - | 1.0.4 | MIDP-1.0 | - | 10 | 16 | 600 | 7667836 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/AgeOfEmpiresIIIENG_s40v3 - Mastiwap.Com.jar | 204 | Age Of Empires III Mobile | - | 1.0.1 | MIDP-1.0 | - | 13 | 16 | 600 | 59743371 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/AgeOfHeroes2 - Mastiwap.Com.jar | 224 | Age of Heroes 2 | - | 1.1.0 | MIDP-2.0 | - | 3 | 16 | 600 | 153185 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/AgeOfHeroes4SK_s40v2a - Mastiwap.Com.jar | 225 | Age of Heroes 4 SK | - | 1.1.1 | MIDP-2.0 | - | 3 | 16 | 600 | 338130 | 0 | java/lang/ArrayIndexOutOfBoundsE | 运行正常 |  |
| vault/AgeOfHeroesV-WarriorsWay_s40v2-N6230 - Mastiwap.Com.jar | 224 | AHV Warrior's Way | - | 1.1.0 | MIDP-1.0 | nokia-ui | 8 | 16 | 600 | 289413 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/Age_Of_Empires_3_S40_2ed_6230i_8800.jar | 204 | Age Of Empires III Mobile | - | 1.0.1 | MIDP-1.0 | - | 13 | 16 | 600 | 27119466 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/Age_of_Heroes_4 blood twilight.jar | 221 | Age of Heroes 4 | - | 1.1.0 | MIDP-1.0 | nokia-ui | 3 | 16 | 600 | 395357 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/Age_of_Heroes_4_blood twilight.jar | 224 | Age of Heroes 4 | - | 1.1.0 | MIDP-1.0 | nokia-ui | 3 | 16 | 600 | 409757 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/Agent To The Stars.jar | 221 | Mr10Percent | - | 1.0.0 | MIDP-1.0 | - | 12 | 16 | 600 | 3100125 | 0 | - | 运行正常 |  |
| vault/Air Race.jar | 224 | Air_race_US | - | 1.0 | MIDP-2.0 | - | 27 | 16 | 600 | 3941422 | 0 | java/io/IOException | 运行正常 |  |
| vault/Air Strike 1944 - Flight for Freedom.jar | 294 | Air Strike 1944 | - | 1.0.9 | MIDP-2.0 | - | 6 | 16 | 600 | 52088838 | 0 | - | 运行正常 |  |
| vault/AirBuster 3D.jar | 299 | AirBurster3d | - | 1.0.1 | MIDP-2.0 | - | 6 | 16 | 600 | 5564308 | 0 | java/io/IOException,java/lang/Ar | 运行正常 |  |
| vault/AirHockey_s40v3 - Mastiwap.Com.jar | 215 | OAAirHockey | - | 1.15.39 | MIDP-2.0 | nokia-ui | 75 | 16 | 600 | 4447692 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/AirHockey_s40v3a-N6280 - Mastiwap.Com.jar | 221 | OAAirHockey | - | 1.13.34 | MIDP-2.0 | - | 73 | 16 | 600 | 4607526 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/AirStrike1944FlightForFreedom_1.0.1_320x240.jar | 316 | Air Strike 1944 | - | 1.0.1 | MIDP-2.0 | - | 6 | 16 | 600 | 75340906 | 0 | - | 运行正常 |  |
| vault/Aircraft Gear.jar | 265 | Aircraft Gear | - | 1.0.0 | MIDP-2.0 | - | 12 | 16 | 600 | 3030575 | 0 | java/io/IOException,java/lang/Nu | 运行正常 |  |
| vault/Akiza Dart.jar | 206 | AKIZA Dart by BerON | - | 1.0.0 | MIDP-2.0 | - | 6 | 16 | 600 | 2291964 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/Aladdin 2 The New Adventure_320x240.jar | 643 | Aladdin 2 | - | 1.0 | MIDP-2.0 | - | 8 | 16 | 600 | 4636229 | 0 | - | 运行正常 |  |
| vault/Alco Bench.jar | 209 | AlcoBench | - | 09.8.11 | MIDP-2.0 | nokia-ui | 66 | 16 | 600 | 872875 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/Alex Rider Storm Breaker.jar | 225 | Stormbreaker by street_racer | - | 1.2.0 | MIDP-2.0 | nokia-ui | 9 | 16 | 600 | 4918491281 | 0 | java/lang/ArrayIndexOutOfBoundsE | 运行正常 |  |
| vault/Ali Baba And The Scary Dev.jar | 204 | Ali Baba | - | 1.1.1 | MIDP-2.0 | - | 2 | 16 | 600 | 1279396057 | 0 | java/lang/ArrayIndexOutOfBoundsE | 运行正常 |  |
| vault/AliBaba_and_Scary_Dev_240x320.jar | 204 | AliBaba | - | 1.1.0 | MIDP-2.0 | - | 2 | 16 | 600 | 8363686431 | 0 | java/lang/IllegalArgumentExcepti | 运行正常 |  |
| vault/Alien Hominid Redialed.jar | 209 | AH Redialed | - | 1.0.0 | MIDP-2.0 | - | 14 | 16 | 600 | 3020625 | 0 | java/lang/StringIndexOutOfBounds | 运行正常 |  |
| vault/Alien Hominid.jar | 209 | AH Redialed by Darden | - | 1.0.0 | MIDP-2.0 | - | 14 | 16 | 600 | 3025750 | 0 | java/lang/StringIndexOutOfBounds | 运行正常 |  |
| vault/Alien Quarantine.jar | 1918 | Alien Quarantine DEDOMIL.NET | - | 1.2.3 | MIDP-2.0 | - | 17 | 16 | 600 | 45504487 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/AlienVsPredator2-Requiem_s40v3 - Mastiwap.Com.jar | 220 | AVP Requiem | - | 1.0.0 | MIDP-2.0 | - | 32 | 16 | 600 | 15427844 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/Alien_Hominid_Redialed.jar | 205 | AH Redialed | - | 1.0.1 | MIDP-2.0 | - | 14 | 16 | 600 | 2547954 | 0 | java/lang/StringIndexOutOfBounds | 运行正常 |  |
| vault/Alien_Hominid_Redialed_S60_5F3ed_N80_E60.jar | 210 | AH Redialed | - | 1.0.1 | MIDP-2.0 | - | 14 | 16 | 600 | 2860220 | 0 | java/io/IOException,java/lang/St | 运行正常 |  |
| vault/Alpha Wing 2.jar | 553 | Alpha Wing 2 | - | 1.0.2 | MIDP-1.0 | - | 20 | 16 | 600 | 6476328 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/Alpha Wing EX.jar | 207 | Alpha Wing EX | - | 1.0.9 | MIDP-1.0 | - | 16 | 16 | 600 | 1016776 | 0 | java/lang/IllegalArgumentExcepti | 运行正常 |  |
| vault/AlphaWing - Mastiwap.Com (7).jar | 207 | Alpha Wing | - | 1.1.1 | MIDP-1.0 | - | 16 | 16 | 600 | 618156 | 0 | java/lang/IllegalArgumentExcepti | 运行正常 |  |
| vault/AlphaWingEX (2).jar | 207 | Alpha Wing EX | - | 1.0.9 | MIDP-1.0 | - | 16 | 16 | 600 | 1024020 | 0 | java/lang/IllegalArgumentExcepti | 运行正常 |  |
| vault/AlphaWingEX.jar | 207 | Alpha Wing EX **MASYAKA** | - | 1.0.9 | MIDP-1.0 | - | 16 | 16 | 600 | 1040652 | 0 | java/lang/IllegalArgumentExcepti | 运行正常 |  |
| vault/Alten8.-.Eternity's.Child.SE.176x220 - Mastiwap.Com.jar | 209 | Eternity Child | - | 1.0 | MIDP-2.0 | - | 12 | 16 | 600 | 3448284227 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/Alten8.-.Opal.Quest.SE.176x220 - Mastiwap.Com.jar | 225 | OpalQuest | - | 1.15 | MIDP-2.0 | - | 11 | 16 | 600 | 10705038 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/America's Army - Special Operations.jar | 329 | America's Army | - | 1.0.5 | MIDP-2.0 | - | 6 | 16 | 600 | 15498105 | 0 | - | 运行正常 |  |
| vault/American Gangster - The Mobile Game.jar | 543 | American Gangster | - | 1.0.9 | MIDP-2.0 | - | 6 | 16 | 600 | 11574333 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/American_Gangster _-_The_Mobile_Game_N91 - Mastiwap.Com.jar | 209 | American Gangster | - | 1.0.1 | MIDP-2.0 | nokia-ui | 6 | 16 | 600 | 15685264 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/Americas Cup Racer__320x240.jar | 141 | ACRacer | - | 1.0.5 | MIDP-1.0 | - | 17 | 16 | 600 | 2447091 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/Amy Pumpkin's Nightmare.jar | 252 | AmyPumpkin | - | 1.0 | MIDP-2.0 | - | 6 | 16 | 600 | 707975 | 0 | java/io/EOFException,javax/micro | 运行正常 |  |
| vault/Anakonda.jar | 83 | Anakonda | - | 1.0.0 | MIDP-1.0 | - | 13 | 16 | 600 | 1021025 | 0 | java/io/IOException,java/lang/Nu | 运行正常 |  |
| vault/Anarchy2087_s60v1 - Mastiwap.Com.jar | 221 | Anarchy2087 | - | 1.0 | MIDP-1.0 | nokia-ui | 8 | 16 | 600 | 10476871 | 0 | java/io/IOException,javax/microe | 运行正常 |  |
| vault/Ancient Empires 2.jar | 264 | Ancient Empires II | - | 1.0.2 | MIDP-1.0 | - | 9 | 16 | 600 | 636956 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/Ancient Empires.jar | 125 | Ancient Empires | - | 1.0.3 | MIDP-1.0 | - | 9 | 16 | 600 | 122070 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/AncientEmpires.jar | 125 | Ancient Empires | - | 1.0.3 | MIDP-1.0 | nokia-ui | 9 | 16 | 600 | 498569 | 0 | java/lang/ArrayIndexOutOfBoundsE | 运行正常 |  |
| vault/AncientEmpires2.jar | 264 | Ancient Empires II | - | 1.0.2 | MIDP-1.0 | - | 9 | 16 | 600 | 660806 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/AncientGames.jar | 218 | Ancient Games by Spid3r | - | 1.0.4 | MIDP-1.0 | nokia-ui | 48 | 16 | 600 | 7711913 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/AncientGames_S60V2_Retail_Etty - Mastiwap.Com.jar | 213 | AncientGames | - | 1.0.4 | MIDP-1.0 | nokia-ui | 48 | 16 | 600 | 7726885 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/AncientGames_s40v3a - Mastiwap.Com.jar | 218 | AncientGames | - | 1.0.4 | MIDP-1.0 | nokia-ui | 48 | 16 | 600 | 7774908 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/Ancient_Taxi_x65.jar | 215 | WUP.Ru | - | 1.0.85 | MIDP-2.0 | - | 45 | 16 | 600 | 7404537 | 0 | java/io/IOException,javax/microe | 运行正常 |  |
| vault/AngelsAndDemons.jar | 570 | Angels and Demons | - | 1.0.0 | MIDP-1.0 | - | 12 | 16 | 600 | 10352114 | 0 | java/lang/NumberFormatException, | 运行正常 |  |
| vault/Angry Snake__320x240.jar | 319 | Angry Snake | - | 1.0.0 | MIDP-2.0 | - | 22 | 16 | 600 | 1493002 | 0 | - | 运行正常 |  |
| vault/Animal Tycoon 2_320x240.jar | 1026 | Animal Tycoon 2 | - | 1.0 | MIDP-2.0 | - | 73 | 16 | 600 | 2778158 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/Antar_Expedition - Mastiwap.Com.jar | 213 | Antar | - | 1.02 | MIDP-1.0 | nokia-ui | 33 | 16 | 600 | 53 | 0 | - | 运行正常 | runs without input; the generic key script m |
| vault/Anyplays.-.Sexy.Puzzmaniac.SE.176x220 - Mastiwap.Com.jar | 208 | Puzzmaniac | - | 1.0 | MIDP-2.0 | - | 15 | 16 | 600 | 2381325 | 0 | java/io/EOFException,javax/micro | 运行正常 |  |
| vault/Apocalypse3000.jar | 213 | Apocalypse 3000 | - | 1.26 | MIDP-1.0 | nokia-ui | 18 | 16 | 600 | 778406 | 0 | java/io/EOFException,javax/micro | 运行正常 |  |
| vault/Appu And Pappu.jar | 205 | Appu And Pappu | - | 1.0 | MIDP-2.0 | - | 5 | 16 | 600 | 3962378 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/Aqua Blast.jar | 211 | Aqua Blast | - | 1.0 | MIDP-2.0 | - | 5 | 16 | 600 | 5243982 | 0 | - | 运行正常 |  |
| vault/AquaDig.jar | 160 | AquaDig | - | 1.22 | MIDP-2.0 | - | 14 | 16 | 600 | 6357130 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/AquaLifeX_MIDP2_240X320 - Mastiwap.Com.jar | 225 | AquaLifeX_MIDP2_240X320 | - | 1.0 | MIDP-2.0 | - | 2 | 16 | 600 | 2014606 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/Aqua_Driller_(176x208) - Mastiwap.Com.jar | 217 | AquaDriller | - | 1.20 | MIDP-2.0 | nokia-ui | 24 | 16 | 600 | 231797 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/Aqua_Driller_v1.20 - Mastiwap.Com.jar | 216 | AquaDriller | - | 1.20 | MIDP-2.0 | nokia-ui | 24 | 16 | 600 | 232565 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/Arcade Side Golf.jar | 194 | ArcadeSideGolf | - | 1.0.2 | MIDP-2.0 | - | 6 | 16 | 600 | 20245697 | 0 | java/io/IOException,java/lang/Nu | 运行正常 |  |
| vault/ArcadeClassicsCollection_1.0.0_320x240.jar | 521 | ArcadeClassics | - | 1.0.0 | MIDP-1.0 | - | 51 | 16 | 600 | 14322125 | 0 | java/io/EOFException,java/io/IOE | 运行正常 |  |
| vault/ArcadePark.jar | 243 | Arcade Park 2 | - | 1.0.0 | MIDP-2.0 | - | 13 | 16 | 433 | 1338873 | 0 | java/io/EOFException,java/io/IOE | 运行正常 | exited itself (notifyDestroyed) |
| vault/ArcadePark2.jar | 371 | ArcadePark2 | - | 1.1.2 | MIDP-2.0 | - | 46 | 16 | 60 | 1895 | 0 | java/lang/NoClassDefFoundError,j | 标题自身异常 | title references a class it does not bundle  |
| vault/Archon.jar | 162 | Archon | - | 1.0.1 | MIDP-2.0 | - | 7 | 16 | 600 | 5989486 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/Arkanoid Evolution.jar | 597 | Arkanoid Evolution | - | 1.1.0 | MIDP-2.0 | - | 19 | 16 | 600 | 4018079 | 0 | java/lang/ArrayIndexOutOfBoundsE | 运行正常 |  |
| vault/Arkanoid.jar | 92 | Arkanoid | - | 1.8.6 | MIDP-1.0 | - | 12 | 16 | 600 | 382505 | 0 | - | 运行正常 |  |
| vault/Army Men - Mobile Ops.jar | 654 | ArmyMen | - | 1.0.0 | MIDP-2.0 | - | 89 | 16 | 600 | 23250280 | 0 | java/io/EOFException,java/lang/A | 运行正常 |  |
| vault/Army Men Mobile Ops_320x240.jar | 544 | ArmyMen | - | 1.0.0 | MIDP-2.0 | - | 89 | 16 | 600 | 34553202 | 0 | java/io/EOFException,java/lang/A | 运行正常 |  |
| vault/Asphalt - Urban GT 3D.jar | 339 | Asphalt Urban GT 3D | - | 1.0.5 | MIDP-2.0 | - | 27 | 16 | 600 | 12886671 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/Asphalt - Urban GT.jar | 250 | AsphaltUrbanGT | - | 1.0.1 | MIDP-2.0 | - | 7 | 16 | 600 | 221253009 | 0 | javax/microedition/rms/InvalidRe | 运行正常 |  |
| vault/Asphalt 2 - Urban GT.jar | 309 | Asphalt2 | - | 1.0.0 | MIDP-2.0 | - | 6 | 16 | 600 | 8822272 | 0 | - | 运行正常 |  |
| vault/Asphalt 3 - Street Rules 3D.jar | 1027 | Asphalt 3 Street Rules 3D | - | 1.0.0 | MIDP-2.0 | - | 19 | 16 | 600 | 141142083 | 0 | java/lang/ClassNotFoundException | 运行正常 |  |
| vault/Asphalt 3 - Street Rules.jar | 285 | Asphalt3 | - | 1.0.2 | MIDP-2.0 | - | 5 | 16 | 600 | 324602040 | 0 | javax/microedition/rms/InvalidRe | 运行正常 |  |
| vault/Asphalt 4 - Elite Racing.jar | 370 | Asphalt 4 : Elite Racing | - | 1.1.7 | MIDP-2.0 | - | 10 | 16 | 600 | 10091593 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/Asphalt 6 - Adrenaline.jar | 1500 | Asphalt6 | - | 1.0.0 | MIDP-2.0 | - | 22 | 16 | 600 | 168720803 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/Asphalt Nitro.jar | 2086 | Asphalt Nitro | - | 1.1.7 | MIDP-2.0 | - | 21 | 16 | 600 | 185969608 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/Asphalt3StreetRules_1.1.5_320x240.jar | 1110 | Asphalt 3 3D Street Rules | - | 1.1.5 | MIDP-2.0 | m3g,nokia-ui | 18 | 16 | 600 | 4025807949 | 0 | java/lang/ClassNotFoundException | 运行正常 |  |
| vault/Asphalt4EliteRacing_1.0.0_320x240.jar | 331 | Asphalt 4 Elite Racing | - | 1.0.0 | MIDP-2.0 | - | 11 | 16 | 600 | 3390628669 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/Asphalt5NitroRush_1.1.5_320x240.jar | 957 | Asphalt 5 Nitro Rush 3D | - | 1.1.5 | MIDP-2.0 | m3g,nokia-ui | 18 | 16 | 600 | 3974933715 | 0 | java/lang/ClassNotFoundException | 运行正常 |  |
| vault/Asphalt6Adrenaline_1.0.2_320x240.jar | 1033 | Asphalt 6 Adrenaline | - | 1.0.2 | MIDP-2.0 | - | 20 | 16 | 600 | 330453751 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/Assassin's Creed - Brotherhood.jar | 1171 | Assassin's Creed Brotherhood | - | 1.1.8 | MIDP-2.0 | - | 11 | 16 | 600 | 183741858 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/Assassin's Creed - Revelations.jar | 1596 | Assassin's Creed Revelations | - | 1.0.6 | MIDP-2.0 | - | 14 | 16 | 600 | 40767011 | 0 | java/lang/IndexOutOfBoundsExcept | 运行正常 |  |
| vault/Assassin's Creed II.jar | 1025 | Assassin's Creed 2 | - | 1.2.3 | MIDP-2.0 | - | 9 | 16 | 600 | 180784094 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/Assassin's Creed III.jar | 1625 | Assassin's Creed III | - | 1.1.3 | MIDP-2.0 | - | 12 | 16 | 600 | 116503061 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/Assassin's Creed.jar | 612 | Assassin's Creed | - | 1.0.2 | MIDP-2.0 | - | 5 | 16 | 600 | 648882219 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/AssassinsCreed1_1.0.1_320x240.jar | 376 | Assassin_s Creed | - | 1.0.1 | MIDP-2.0 | - | 5 | 16 | 600 | 3579918791 | 0 | - | 运行正常 |  |
| vault/AssassinsCreed2_1.2.5_320x240.jar | 687 | Assassin's Creed 2 | - | 1.2.5 | MIDP-2.0 | - | 9 | 16 | 600 | 72909174 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/AssassinsCreedBrotherhood_1.2.7_320x240.jar | 1017 | Assassin's Creed Brotherhood | - | 1.2.7 | MIDP-2.0 | - | 10 | 16 | 600 | 117462511 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/AsterixObelixEncounterCleopatra_1.0.3_320x240.jar | 666 | Asterix and Obelix encounter | - | 1.0.3 | MIDP-2.0 | - | 11 | 16 | 600 | 66297638 | 0 | - | 运行正常 |  |
| vault/Asteroids.jar | 368 | Asteroids | - | 1.0.3 | MIDP-2.0 | - | 6 | 16 | 600 | 4609230 | 0 | java/lang/NumberFormatException, | 运行正常 |  |
| vault/Astro Knight Galaxy.jar | 182 | Knight | - | 1.0 | MIDP-1.0 | - | 3 | 16 | 600 | 287345774 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/AtariLegendsPack.jar | 290 | Atari Legends pack | - | 0.0.15 | MIDP-2.0 | - | 47 | 16 | 600 | 993482 | 0 | java/io/IOException,javax/microe | 运行正常 |  |
| vault/AtariLegendsVol1.jar | 83 | Atari Legends Vol1 [terjef] | - | 1.1.6 | MIDP-1.0 | - | 13 | 16 | 600 | 4500406 | 0 | java/lang/StringIndexOutOfBounds | 运行正常 |  |
| vault/AtariLegendsVol2.jar | 78 | Atari Legends 2 [terjef] | - | 1.3.8 | MIDP-2.0 | - | 9 | 16 | 600 | 2702235 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/AtariLegendsVol3.jar | 100 | Atari Legends 3 [terjef] | - | 1.1.0 | MIDP-2.0 | - | 10 | 16 | 600 | 5672213 | 0 | java/lang/IllegalMonitorStateExc | 运行正常 |  |
| vault/Atlantis SkyPatrol.jar | 468 | Atlantis Sky Patrol | - | 1.0.0 | MIDP-2.0 | - | 12 | 16 | 600 | 750784 | 0 | - | 运行正常 |  |
| vault/Avalanche Snowboarding.jar | 836 | Avalanche Snowboarding | - | 1.2.0 | MIDP-2.0 | - | 17 | 16 | 600 | 18593864 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/AvalancheSnowboarding_1.2.1_320x240.jar | 714 | Avalanche Snowboarding | - | 1.2.1 | MIDP-2.0 | - | 18 | 16 | 600 | 61868161 | 0 | javax/microedition/rms/InvalidRe | 运行正常 |  |
| vault/Avatar_1.0.6_320x240.jar | 659 | Avatar | - | 1.0.6 | MIDP-2.0 | - | 8 | 16 | 600 | 30717990 | 0 | - | 运行正常 |  |
| vault/Avenging Angel.jar | 221 | Avenging Angel | - | 2.00.36 | MIDP-2.0 | - | 5 | 16 | 600 | 638148 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/AztecSnake.jar | 488 | Aztec Snake Retail Stox | - | 1.0.0 | MIDP-2.0 | - | 17 | 16 | 600 | 599410 | 0 | - | 运行正常 |  |
| vault/Backgammon.jar | 147 | elkware's Backgammon | - | 1.1.2 | MIDP-2.0 | - | 3 | 16 | 600 | 4055603 | 0 | java/io/IOException,java/lang/Nu | 运行正常 |  |
| vault/Batman - Guardian of Gotham.jar | 638 | Batman(TM) - Guardian of Got | - | 2.0.0 | MIDP-2.0 | - | 82 | 16 | 600 | 5917024 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/Battle for The White House.jar | 576 | Battle For The White House | - | 1.0.4 | MIDP-2.0 | - | 4 | 16 | 600 | 13409832 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/Battleships - Sea on Fire.jar | 222 | Battleships 2010 | - | 1.13.5 | MIDP-2.0 | - | 17 | 16 | 600 | 437581 | 0 | - | 运行正常 |  |
| vault/Battlestar Galactica.jar | 440 | Battlestar Galactica | - | 1.0.0 | MIDP-2.0 | - | 61 | 16 | 600 | 5716326 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/Beach Mini Golf 3D.jar | 238 | Minigolf 3D | - | 1.3.01 | MIDP-1.0 | - | 55 | 16 | 600 | 12688892 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/Bejeweled 3.jar | 550 | Bejeweled 3 | - | 4.0.78 | MIDP-2.0 | - | 42 | 16 | 600 | 2937296 | 0 | - | 运行正常 |  |
| vault/Bejeweled Twist.jar | 574 | BejeweledTwist | - | 01.00.04 | MIDP-2.0 | - | 5 | 16 | 600 | 31599371 | 0 | java/io/IOException,javax/microe | 运行正常 |  |
| vault/Bejeweled.jar | 350 | Bejeweled (mobilegamesarena. | - | 1.5.46 | MIDP-2.0 | nokia-ui | 14 | 16 | 600 | 21184940 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/Bejeweled1_1.5.46_320x240.jar | 371 | Bejeweled | - | 1.5.46 | MIDP-2.0 | nokia-ui | 14 | 16 | 600 | 21426789 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/Bejeweled3_4.7.74_320x240.jar | 656 | Bejeweled 3 | - | 4.7.74 | MIDP-2.0 | m3g | 42 | 16 | 600 | 24448203 | 0 | - | 运行正常 |  |
| vault/BejeweledTwist.jar | 659 | BejeweledTwist by Alexe | - | 01.00.37 | MIDP-2.0 | - | 4 | 16 | 601 | 30389305 | 0 | java/io/IOException,javax/microe | 运行正常 |  |
| vault/Bejeweled_320x240.jar | 350 | Bejeweled | - | 1.5.46 | MIDP-2.0 | nokia-ui | 14 | 16 | 600 | 21581032 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/Beowulf - The Mobile Game.jar | 576 | Beowulf | - | 1.1.1 | MIDP-2.0 | - | 7 | 16 | 600 | 11182462 | 0 | - | 运行正常 |  |
| vault/Beowulf_1.1.6_320x240.jar | 585 | Beowulf | - | 1.1.6 | MIDP-2.0 | nokia-ui | 7 | 16 | 600 | 16247263 | 0 | - | 运行正常 |  |
| vault/Big Range Hunting 2.jar | 1048 | Big Range Hunting 2 | - | 1.0.0 | MIDP-2.0 | - | 13 | 16 | 600 | 14764809 | 0 | java/lang/ArrayIndexOutOfBoundsE | 运行正常 |  |
| vault/Big Range Hunting.jar | 628 | Big Range Hunting | - | 1.0.1 | MIDP-2.0 | - | 9 | 16 | 600 | 340162399 | 0 | java/lang/ClassNotFoundException | 运行正常 |  |
| vault/Bikini Volleyball.jar | 621 | Bikini Volleyball | - | 1.0.7 | MIDP-2.0 | - | 8 | 16 | 600 | 4434518 | 0 | java/lang/ArrayIndexOutOfBoundsE | 运行正常 |  |
| vault/BikiniVolleyball_1.0.0_320x240.jar | 659 | Bikini Volleyball | - | 1.0.0 | MIDP-2.0 | - | 8 | 16 | 600 | 5455682391 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
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
| vault/BlockBreaker2Deluxe.jar | 574 | Block Breaker 2 Deluxe (mobi | - | 1.0.3 | MIDP-2.0 | nokia-ui | 10 | 16 | 600 | 2788348894 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/BlockBreaker3Unlimited_1.0.1_320x240.jar | 417 | Block Breaker 3 Unlimited | - | 1.0.1 | MIDP-2.0 | - | 12 | 16 | 600 | 39148436 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/BlockBreakerDeluxe.jar | 591 | Block Breaker Deluxe | - | 1.0.9 | MIDP-2.0 | m3g,nokia-ui | 7 | 16 | 600 | 5223431680 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/BlockBreakerDeluxe2_1.0.4_320x240.jar | 375 | Block Breaker 2 Deluxe | - | 1.0.4 | MIDP-2.0 | - | 10 | 16 | 600 | 23463562 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/Blokus_1.0.6_320x240.jar | 538 | Blokus | - | 1.0.6 | MIDP-2.0 | - | 16 | 16 | 600 | 23827282 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/Blood+.jar | 677 | Blood+ | - | 1.0.0 | MIDP-2.0 | - | 23 | 16 | 600 | 7544461 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/Blur Racing Mobile.jar | 624 | Blur | - | 1.0.0 | MIDP-2.0 | - | 22 | 16 | 600 | 259433233 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/Bobble Blast Deluxe_320x240_Nokia_E71.jar | 372 | Bobble Blast Deluxe | - | 1.0.0 | MIDP-2.0 | - | 86 | 16 | 600 | 22489963 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/Bobbys Blocks_320x240.jar | 229 | Bobby | - | 1.1.2 | MIDP-2.0 | - | 12 | 16 | 600 | 2709702458 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/Bolt.jar | 432 | Bolt | - | 1.0.0 | MIDP-2.0 | - | 21 | 16 | 600 | 562021 | 0 | java/io/EOFException,java/io/IOE | 运行正常 |  |
| vault/Bomberman 2007.jar | 291 | Bomberman | - | 1.0.32 | MIDP-1.0 | - | 12 | 16 | 600 | 23434154 | 0 | javax/microedition/rms/InvalidRe | 运行正常 |  |
| vault/Bomberman Deluxe.jar | 166 | Bomberman Deluxe | - | 1.28.2 | MIDP-1.0 | - | 5 | 16 | 600 | 26684634 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/BombermanDeluxe.jar | 172 | Bomberman Deluxe | - | 1.28.2 | MIDP-1.0 | - | 5 | 16 | 600 | 27307532 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/BombermanSupreme.jar | 315 | Bomberman | - | 1.0.5 | MIDP-1.0 | - | 19 | 16 | 600 | 22429136 | 0 | javax/microedition/rms/InvalidRe | 运行正常 |  |
| vault/BoulderDash2.jar | 93 | Boulder Dash 2 | - | 1.2 | MIDP-2.0 | - | 9 | 16 | 600 | 808941 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/Bounce (128x128).jar | 54 | Bounce | - | 1.8 | MIDP-1.0 | nokia-ui | 9 | 16 | 600 | 522088 | 0 | java/lang/StackOverflowError | 运行正常 |  |
| vault/Bounce Tales (240x320).jar | 592 | Bounce Tales | - | 2.0.25 | MIDP-2.0 | nokia-ui | 20 | 16 | 600 | 72337396 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/Bounce Tales.jar | 592 | Bounce Tales | - | 2.0.25 | MIDP-2.0 | - | 20 | 16 | 600 | 11624377 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/Brain Genius (240x320).jar | 321 | Brain Genius | - | 0.2.0 | MIDP-1.0 | - | 8 | 16 | 600 | 7148251 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/Brain Genius 2 Deluxe__320x240.jar | 575 | Brain Genius 2 Deluxe | - | 1.1.3 | MIDP-1.0 | nokia-ui | 24 | 16 | 600 | 7321431 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/Brain Tester 24 Pack Vol 2_320x240.jar | 951 | Brain Tester2 | - | 1.0.5 | MIDP-2.0 | nokia-ui | 39 | 16 | 600 | 14170768 | 0 | java/io/EOFException,javax/micro | 运行正常 |  |
| vault/BrainChallenge1_1.0.0_320x240.jar | 241 | Brain Challenge | - | 1.0.0 | MIDP-2.0 | - | 8 | 16 | 600 | 10646029 | 0 | java/lang/ArrayIndexOutOfBoundsE | 运行正常 |  |
| vault/BrainChallenge2StressManagement_1.0.9_320x240.jar | 391 | BRAIN CHALLENGE VOL.2 | - | 1.0.9 | MIDP-2.0 | - | 8 | 16 | 600 | 27416259 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/BrainChallenge3ThinkAgain_1.1.0_320x240.jar | 730 | Brain Challenge 3 Think Agai | - | 1.1.0 | MIDP-2.0 | - | 20 | 16 | 600 | 2142972268 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/BrainChallenge4-BreakingLimits_1.0.5_320x240.jar | 870 | Brain Challenge 4: Breaking  | - | 1.0.5 | MIDP-2.0 | nokia-ui | 10 | 16 | 600 | 331418881 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/BrainTester_1.0.2_320x240.jar | 272 | Brain | - | 0.0.2 | MIDP-2.0 | - | 69 | 16 | 600 | 7622333 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/Brave - The Official Game.jar | 1373 | Brave | - | 1.1.1 | MIDP-2.0 | - | 29 | 16 | 600 | 17818806 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/Break Quest Mobile_320x240.jar | 492 | Break Quest Mobile | - | 1.0.1 | MIDP-2.0 | - | 69 | 16 | 600 | 1228678836 | 0 | java/io/IOException,javax/microe | 运行正常 |  |
| vault/Brick and Bubble Revolution__320x240.jar | 803 | BrickBubbleRev | - | 1.0.2 | MIDP-2.0 | nokia-ui | 70 | 16 | 600 | 36050383 | 0 | java/io/EOFException,java/lang/O | 运行正常 |  |
| vault/BrickBreakerRevolution.jar | 234 | Brick Breaker | - | 1.0.09 | MIDP-2.0 | nokia-ui | 13 | 16 | 600 | 1807228971 | 0 | java/io/EOFException,java/lang/N | 运行正常 |  |
| vault/BrickBreakerRevolution2.jar | 947 | BBR 2 | - | 1.0.4 | MIDP-2.0 | nokia-ui | 57 | 16 | 600 | 210949216 | 0 | java/io/EOFException,javax/micro | 运行正常 |  |
| vault/BrickCrusher_1.0.0_320x240.jar | 291 | Brick Crusher | - | 1.0.0 | MIDP-2.0 | - | 89 | 16 | 600 | 1730857697 | 0 | java/io/IOException,javax/microe | 运行正常 |  |
| vault/Brothers in Arms - Art of War.jar | 594 | Brothers in Arms Art of War | - | 1.3.8 | MIDP-2.0 | - | 6 | 16 | 600 | 63293982 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/Brothers in Arms - Earned in Blood.jar | 257 | Brothers In Arms | - | 1.0.1 | MIDP-2.0 | - | 9 | 16 | 600 | 1103978 | 0 | - | 运行正常 |  |
| vault/BrothersInArmsEarnedInBlood_1.0.1_320x240.jar | 232 | Brothers In Arms:Earned In B | - | 1.0.1 | MIDP-2.0 | - | 7 | 16 | 600 | 2859231446 | 0 | java/lang/ArrayIndexOutOfBoundsE | 运行正常 |  |
| vault/Bubble Bash 2.jar | 994 | Bubble Bash 2 | - | 1.1.3 | MIDP-2.0 | - | 24 | 16 | 600 | 63257984 | 0 | - | 运行正常 |  |
| vault/Bubble Bash 3.jar | 1224 | Bubble Bash 3 | - | 1.1.3 | MIDP-2.0 | - | 20 | 16 | 600 | 702965020 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/Bubble Bash Mania.jar | 955 | Bubble Bash Mania | - | 1.2.9 | MIDP-2.0 | - | 16 | 16 | 600 | 11805597068 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/Bubble Bash.jar | 320 | Bubble Bash | - | 1.0.2 | MIDP-2.0 | - | 9 | 16 | 600 | 663207020 | 0 | - | 运行正常 |  |
| vault/Bubble Bobble Evolution.jar | 545 | Bubble Bobble Evolution | - | 1.5.2 | MIDP-1.0 | - | 15 | 16 | 600 | 4252198 | 0 | java/lang/IllegalArgumentExcepti | 运行正常 |  |
| vault/Bubble Bobble.jar | 137 | Bubble Bobble | - | 1.4.9 | MIDP-2.0 | - | 12 | 16 | 600 | 423368 | 0 | - | 运行正常 |  |
| vault/Bubble Bobble_320x240.jar | 134 | Bubble Bobble | - | 1.4.9 | MIDP-2.0 | - | 12 | 16 | 600 | 2721216 | 0 | java/io/IOException | 运行正常 |  |
| vault/Bubble Boom Challenge 2__320x240.jar | 361 | BB_Challenge2 | - | 1.0.8 | MIDP-2.0 | - | 16 | 16 | 600 | 14215058 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/Bubble Pop_320x240.jar | 938 | BubblePop | - | 1.4.0 | MIDP-2.0 | nokia-ui | 37 | 16 | 600 | 6386107 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/Bubble Town 2_320x240.jar | 435 | Bubble Town 2 | - | 1.2.3 | MIDP-2.0 | - | 61 | 16 | 600 | 959325 | 0 | java/lang/ArrayIndexOutOfBoundsE | 运行正常 |  |
| vault/Bubble X Mania Deluxe_320x240.jar | 315 | Bublex Mania Deluxe | - | 1.0.0 | MIDP-2.0 | - | 20 | 16 | 600 | 3902577964 | 0 | java/io/IOException,javax/microe | 运行正常 |  |
| vault/BubbleBash.jar | 320 | Bubble Bash (mobilegamesaren | - | 1.0.1 | MIDP-2.0 | - | 9 | 16 | 600 | 5158182154 | 0 | - | 运行正常 |  |
| vault/BubbleBash1_4.0.0_320x240.jar | 432 | Bubble Bash | - | 1.0.0 | MIDP-2.0 | - | 8 | 16 | 600 | 5100353893 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/BubbleBash2_1.0.8_320x240.jar | 639 | Bubble Bash 2 | - | 1.0.9 | MIDP-2.0 | - | 20 | 16 | 600 | 29216484 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/Bugs Bunny - Rabbit Rescue.jar | 576 | Bugs Bunny | - | 1.0.9 | MIDP-2.0 | - | 38 | 16 | 600 | 3440861 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/Build-a-lot_320x240.jar | 337 | Build-a-lot serviak | - | 1.0.2 | MIDP-2.0 | - | 42 | 16 | 600 | 18048769 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/Bulldozer Inc.jar | 190 | BulldozerInc | - | 1.0 | MIDP-2.0 | - | 5 | 16 | 600 | 315201 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/Bumper Car City.jar | 316 | Autoscooter | - | 1.0.58 | MIDP-2.0 | - | 14 | 16 | 600 | 18969582 | 0 | java/io/EOFException,java/lang/N | 运行正常 |  |
| vault/Burger Time Delight.jar | 265 | BurgerTime Delight | - | 1.0.0 | MIDP-2.0 | - | 38 | 16 | 600 | 6510060 | 0 | - | 运行正常 |  |
| vault/Burger Time Special.jar | 149 | Burger Time Special | - | 1.0.56 | MIDP-1.0 | - | 9 | 16 | 600 | 4751277 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/Burning Tires.jar | 412 | 3D Burning Tires | - | 1.3.6 | MIDP-2.0 | - | 115 | 16 | 600 | 3805776 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/Burnout 3D.jar | 371 | Burnout | - | 4.2.36 | MIDP-2.0 | - | 10 | 16 | 600 | 16499908 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/Burnout.jar | 354 | Burnout | - | 4.3.21 | MIDP-2.0 | - | 8 | 16 | 600 | 4671537 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/Burnout_2ri68qv5.jar | 412 | Burnout | - | 4.20.50 | MIDP-2.0 | - | 8 | 16 | 600 | 114290587 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/Burnout_4.3.34_320x240.jar | 400 | Burnout | - | 4.3.34 | MIDP-2.0 | - | 8 | 16 | 600 | 105020610 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/CSI2Miami_1.0.5_320x240.jar | 693 | CSI: Miami 2 | - | 1.0.5 | MIDP-2.0 | - | 15 | 16 | 600 | 27259926 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/CSILasVegas_1.03_320x240.jar | 642 | CSI: Las Vegas | - | 1.0.3 | MIDP-2.0 | - | 8 | 16 | 600 | 10220952 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/CSIMiami2.jar | 945 | CSI: Miami 2 | - | 1.1.1 | MIDP-2.0 | nokia-ui | 15 | 16 | 600 | 27639201 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/CSIMiami_1.0.3_320x240.jar | 641 | CSI: Miami | - | 1.0.2 | MIDP-2.0 | - | 6 | 16 | 600 | 234822948 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/CSINewYork.jar | 952 | CSI NY The mobile game | - | 1.0.3 | MIDP-2.0 | - | 8 | 16 | 600 | 97497986 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/CSINewYork_1.1.2_320x240.jar | 375 | CSI: New York | - | 1.1.2 | MIDP-2.0 | - | 7 | 16 | 600 | 49417085 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/Caesar.jar | 267 | CAESAR | - | 0.4.6 | MIDP-1.0 | nokia-ui | 5 | 16 | 600 | 8944016 | 0 | - | 运行正常 |  |
| vault/CafeKakuro.jar | 294 | Cafe Kakuro | - | 1.1.60 | MIDP-2.0 | nokia-ui | 20 | 16 | 600 | 38767366 | 0 | java/io/EOFException,java/lang/N | 运行正常 |  |
| vault/Cake Mania - My Story.jar | 967 | Cake Mania | - | 1.0.1 | MIDP-2.0 | - | 43 | 16 | 600 | 2600879 | 0 | java/io/EOFException,javax/micro | 运行正常 |  |
| vault/California Gold Rush - Bonanza!.jar | 1227 | CGRBonanza | - | 1.0.8 | MIDP-2.0 | - | 71 | 16 | 600 | 837406 | 0 | java/io/EOFException,javax/micro | 运行正常 |  |
| vault/California Gold Rush Bonanza_1.0.7_320x240.jar | 841 | California Gold Rush | - | 1.0.4 | MIDP-2.0 | nokia-ui | 62 | 16 | 600 | 1760254 | 0 | java/io/EOFException,javax/micro | 运行正常 |  |
| vault/Cannon Rats.jar | 1050 | Cannon Rats | - | 1.1.5 | MIDP-2.0 | - | 49 | 16 | 600 | 197684627 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/CannonRats_1.1.6_320x240.jar | 819 | Cannon Rats | - | 1.1.6 | MIDP-2.0 | - | 49 | 16 | 600 | 520476569 | 0 | - | 运行正常 | runs without input; the generic key script d |
| vault/Carcassonne_1.0.8_320x240.jar | 652 | Carcassonne | - | 1.0.8 | MIDP-2.0 | nokia-ui | 82 | 16 | 600 | 2547131965 | 0 | java/io/IOException,javax/microe | 运行正常 |  |
| vault/Castle of Magic.jar | 946 | Castle of Magic | - | 1.1.3 | MIDP-2.0 | - | 12 | 16 | 600 | 454394165 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/Castlevania - Aria of Sorrow.jar | 405 | Castlevania Aria Of Sorrow | - | 1.0.0 | MIDP-2.0 | - | 4 | 16 | 600 | 8945551 | 0 | javax/microedition/rms/InvalidRe | 运行正常 |  |
| vault/Castlevania - Dawn of Sorrow.jar | 610 | Castlevania Dawn of Sorrow | - | 1.0.0 | MIDP-2.0 | - | 19 | 16 | 600 | 4025754 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/Castlevania - Order of Shadows.jar | 521 | CastlevaniaOoS | - | 0.0.7 | MIDP-2.0 | - | 66 | 16 | 600 | 1037964 | 0 | javax/microedition/rms/InvalidRe | 运行正常 |  |
| vault/Castlevania Order Of Shadows_320x240.jar | 523 | Castlevania 2 | - | 0.0.10 | MIDP-2.0 | - | 66 | 16 | 601 | 1789336 | 0 | javax/microedition/rms/InvalidRe | 运行正常 |  |
| vault/Castlevania-AriaOfSorrow_N80.jar | 403 | Castlevania Aria Of Sorrow | - | 1.0.0 | MIDP-2.0 | - | 4 | 16 | 600 | 11739659 | 0 | javax/microedition/rms/InvalidRe | 运行正常 |  |
| vault/Catan The Seafarers_320x240_Nokia_E71.jar | 504 | Catan2 | - | 1.0.5 | MIDP-2.0 | nokia-ui | 110 | 16 | 600 | 3418441391 | 0 | java/io/IOException,javax/microe | 运行正常 |  |
| vault/CatanTheFirstIsland_1.0.2_320x240.jar | 418 | Catan | - | 1.0.2 | MIDP-2.0 | nokia-ui | 103 | 16 | 600 | 3608909687 | 0 | javax/microedition/rms/RecordSto | 运行正常 | runs without input; the generic key script m |
| vault/Chainz.jar | 101 | Chainz | - | 1.0.13 | MIDP-2.0 | nokia-ui | 12 | 16 | 600 | 580371611 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/ChampionshipStreetRacing_1.0.0_320x240.jar | 556 | ChampStreetRacing | - | 1.0.0 | MIDP-2.0 | - | 95 | 16 | 600 | 12432517 | 0 | java/io/IOException,java/lang/Nu | 运行正常 |  |
| vault/ChessMaster_1.0.2_320x240.jar | 189 | Chess Master | - | 1.0.2 | MIDP-2.0 | - | 6 | 16 | 600 | 2358432340 | 0 | - | 运行正常 |  |
| vault/Chessmaster (240x400).jar | 958 | Chessmaster | - | 1.0.5 | MIDP-2.0 | - | 9 | 16 | 600 | 868777116 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/Chessmaster.jar | 139 | Chessmaster | - | 1.0.4 | MIDP-1.0 | nokia-ui | 5 | 16 | 600 | 1907283063 | 0 | - | 运行正常 |  |
| vault/ChristmasMidnightPool_1.0.8_320x240.jar | 248 | Christmas Midnight Pool | - | 1.0.8 | MIDP-1.0 | - | 8 | 16 | 600 | 272297578 | 0 | - | 运行正常 |  |
| vault/Chuck Norris - Bring on the Pain.jar | 621 | Chuck Norris - Bring on the | - | 1.1.0 | MIDP-2.0 | - | 7 | 16 | 600 | 27381970 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/ChuckNorrisBringOnThePain_1.0.2_320x240.jar | 600 | Chuck Norris - Bring on the  | - | 1.0.2 | MIDP-2.0 | - | 6 | 16 | 600 | 37928196 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/City Bloxx.jar | 422 | City Bloxx | - | 1.0.22 | MIDP-2.0 | - | 19 | 16 | 600 | 13757665 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/CityTycoon.jar | 216 | City Tycoon | - | 1.0 | MIDP-2.0 | - | 33 | 16 | 600 | 179786 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/Clash of the Titans - The Movie.jar | 713 | Clash of Titans Movie | - | 1.0.0 | MIDP-2.0 | - | 17 | 16 | 600 | 265234483 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/Climate Mission (640x360).jar | 6347 | Climate Mission | - | 1.3.11 | MIDP-2.0 | nokia-ui | 112 | 16 | 600 | 58790523 | 0 | javax/microedition/rms/InvalidRe | 运行正常 | runs without input; the generic key script m |
| vault/Colin McRae - DiRT 3D.jar | 867 | Colin McRae DiRT 3D srv | - | 1.0.5 | MIDP-1.0 | - | 39 | 16 | 600 | 14487631 | 0 | - | 运行正常 |  |
| vault/Colin McRae - DiRT.jar | 568 | Colin McRae DiRT | - | 1.2.5 | MIDP-1.0 | - | 21 | 16 | 600 | 5785988 | 0 | - | 运行正常 |  |
| vault/Command & Conquer 3 - Tiberium Wars.jar | 594 | Command & Conquer 3 - Tiberi | - | 4.1.64 | MIDP-2.0 | - | 29 | 16 | 600 | 2847419 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/Command & Conquer 4 - Tiberian Twilight.jar | 1097 | C&C4 Tiberian Twilight | - | 4.1.10 | MIDP-2.0 | - | 38 | 16 | 600 | 2466996 | 0 | - | 运行正常 |  |
| vault/Command And Conquer 3 Tiberium Wars_320x240_Nokia_6790.jar | 602 | Command & Conquer 3 - Tiberi | - | 4.3.81 | MIDP-2.0 | nokia-ui | 29 | 16 | 600 | 14026457 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/CommandAndConquer4TiberiumTwilight_4.1.8_320x240.jar | 1087 | C&C4 Tiberian Twilight [Mobi | - | 4.1.8 | MIDP-2.0 | - | 35 | 16 | 600 | 9987491 | 0 | - | 运行正常 |  |
| vault/CommandAndConquerRedAlertMobile_4.8.5_320x240.jar | 804 | C&C: Red Alert Mobile | - | 4.8.5 | MIDP-2.0 | - | 32 | 16 | 600 | 12481586 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/Commandos.jar | 609 | Commandos | - | 1.2.3 | MIDP-2.0 | - | 20 | 16 | 600 | 33509532 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/CommonKnowledge_1.0.5_320x240.jar | 336 | Common Knowledge Quiz | - | 1.0.5 | MIDP-2.0 | nokia-ui | 6 | 16 | 600 | 4307546158 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/CopsLAPolice_1.2.3_320x240.jar | 635 | COPS | - | 1.2.3 | MIDP-2.0 | - | 11 | 16 | 600 | 145964318 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/Cowboys & Aliens.jar | 1255 | Cowboys And Aliens | - | 1.1.8 | MIDP-2.0 | - | 16 | 16 | 600 | 192651415 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/Crash Arena 3D.jar | 379 | CrashArena 3D | - | 1.3.0 | MIDP-2.0 | - | 81 | 16 | 600 | 3692984 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/Crash Bamdicoot - Mutant Island.jar | 905 | Crash Mutant Island | - | 1.0.4 | MIDP-1.0 | - | 49 | 16 | 600 | 5360357 | 0 | java/io/IOException,java/lang/Nu | 运行正常 |  |
| vault/Crash Car Mania 3D.jar | 505 | Crash Car Mania by piguro | - | 1.5.1 | MIDP-2.0 | - | 36 | 16 | 600 | 3954829 | 0 | java/lang/ClassCastException,jav | 运行正常 |  |
| vault/Crash of the Titans.jar | 282 | Crash Titans | - | 1.2.1 | MIDP-2.0 | - | 11 | 16 | 600 | 332424 | 0 | - | 运行正常 |  |
| vault/CrashBandicootNitroKart2.jar | 396 | CBNK | - | 5.0.6 | MIDP-1.0 | - | 23 | 16 | 600 | 19084278 | 0 | java/io/IOException | 运行正常 |  |
| vault/Crazy Window Cleaners.jar | 473 | WindowCleaners | - | 1.0.52 | MIDP-2.0 | - | 15 | 16 | 600 | 3096060 | 0 | java/io/EOFException,java/lang/N | 运行正常 |  |
| vault/Crosspix.jar | 234 | Crosspix | - | 1.0.2 | MIDP-1.0 | - | 14 | 16 | 600 | 11626781 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/Crunch Time Basketball__320x240.jar | 220 | CrunchTimeBasketball | - | 1.0.0 | MIDP-2.0 | - | 8 | 16 | 600 | 2770752 | 0 | - | 运行正常 |  |
| vault/Crystal Defenders.jar | 384 | Crystal Defenders | - | 1.0.0 | MIDP-2.0 | - | 17 | 16 | 600 | 7431381 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/Crystal Defenders_320x240.jar | 368 | Crystal Defenders TEGOS.ru | - | 1.0.0 | MIDP-2.0 | - | 18 | 16 | 600 | 14494026 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/Cthulhu.jar | 401 | Cthulhu_DW1 | - | 1.2.5 | MIDP-1.0 | nokia-ui | 28 | 16 | 600 | 2847417 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/Cubes.jar | 449 | Cubes | - | 1.0.13 | MIDP-2.0 | - | 23 | 16 | 601 | 557755671 | 0 | javax/microedition/media/MediaEx | 运行正常 |  |
| vault/Cubis2.jar | 246 | Cubis 2 | - | 1.2.3 | MIDP-2.0 | - | 12 | 16 | 600 | 3221325 | 0 | java/io/IOException | 运行正常 |  |
| vault/Dakar 2008.jar | 409 | Dakar 2008 | - | 1.0.1 | MIDP-1.0 | - | 3 | 16 | 600 | 285362232 | 0 | - | 运行正常 |  |
| vault/Dakar 2009.jar | 849 | DAKAR Rally 2009 | - | 1.1.3 | MIDP-2.0 | - | 46 | 16 | 600 | 2974967 | 0 | java/io/IOException,java/lang/Nu | 运行正常 |  |
| vault/Dakar 2010.jar | 975 | DAKAR Rally 2010 | - | 0.1.8 | MIDP-2.0 | - | 47 | 16 | 600 | 10384560 | 0 | java/io/IOException,java/lang/Nu | 运行正常 |  |
| vault/Danger Dash.jar | 1282 | Danger Dash | - | 1.0.5 | MIDP-2.0 | - | 24 | 16 | 600 | 567152521 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/DarkWorld2.jar | 439 | Dark World 2_freemobilejavag | - | 1.0 | MIDP-2.0 | - | 26 | 16 | 600 | 208669 | 0 | - | 运行正常 |  |
| vault/DateOrDitch2_1.0.3_320x240.jar | 731 | Date Or Ditch 2 | - | 1.0.3 | MIDP-2.0 | - | 28 | 16 | 600 | 22982307 | 0 | java/lang/ClassNotFoundException | 运行正常 |  |
| vault/DateOrDitch_1.0.3_320x240.jar | 556 | Date or Ditch | - | 1.0.3 | MIDP-2.0 | - | 11 | 16 | 600 | 27059719 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/Death Race.jar | 609 | Death Race: The Mobile Game | - | 1.1.4 | MIDP-2.0 | - | 17 | 16 | 600 | 86994141 | 0 | javax/microedition/rms/InvalidRe | 运行正常 |  |
| vault/Death Trap 2 - The Unlocked Code.jar | 237 | Death Trap 2.TUC | - | 1.0.0 | MIDP-2.0 | - | 12 | 16 | 600 | 566189 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/Death Zone.jar | 306 | Death Zone serviak and Black | - | 1.0.0 | MIDP-2.0 | - | 24 | 16 | 600 | 638687205 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/DerekJeterProBaseball2009_1.2.0_320x240.jar | 601 | Derek Jeter Pro Barketball 2 | - | 1.2.0 | MIDP-2.0 | - | 10 | 16 | 617 | 16914902 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/DesperateHousewives_1.0.2_320x240.jar | 398 | Desperate Housewives | - | 1.0.2 | MIDP-2.0 | - | 8 | 16 | 600 | 3901888817 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/DetectiveRidleyAndTheMysteriousEnigmaa_1.06_320x240.jar | 916 | Detective Ridley And The Mys | - | 1.0.6 | MIDP-2.0 | - | 12 | 16 | 600 | 33688522 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/Dewy's Adventure.jar | 416 | Dewy's Adventure | - | 1.0.0 | MIDP-2.0 | - | 2 | 16 | 600 | 6765217759 | 0 | javax/microedition/rms/InvalidRe | 运行正常 |  |
| vault/Diamond Islands 2_320x240.jar | 1093 | Diamond Islands 2 | - | 1.0.18 | MIDP-2.0 | nokia-ui | 23 | 16 | 600 | 7405065 | 0 | java/io/EOFException,java/lang/C | 运行正常 |  |
| vault/Diamond Rush.jar | 296 | Diamond Rush | - | 1.2.0 | MIDP-2.0 | - | 10 | 16 | 600 | 1179769 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/Diamond Twister (1).jar | 594 | Diamond Twister | - | 1.0.8 | MIDP-2.0 | - | 6 | 16 | 600 | 9648119 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/Diamond Twister 2.jar | 1003 | Diamond Twister 2 | - | 1.1.3 | MIDP-2.0 | - | 8 | 16 | 601 | 7009717 | 0 | - | 运行正常 |  |
| vault/Diamond Twister 2_320x240.jar | 877 | Diamond Twister 2 | - | 1.0.5 | MIDP-2.0 | - | 8 | 16 | 601 | 8298536 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/DiamondIslands.jar | 385 | Diamond Islands | - | 1.0.11 | MIDP-2.0 | - | 11 | 16 | 601 | 8974031 | 0 | java/io/EOFException,javax/micro | 运行正常 |  |
| vault/DiamondIslands2.jar | 530 | Diamond Islands 2 | - | 1.0.21 | MIDP-2.0 | nokia-ui | 20 | 16 | 601 | 32762258 | 0 | java/io/EOFException,java/lang/A | 运行正常 |  |
| vault/DiamondQuest.jar | 440 | Paris Hilton's Diamond Quest | - | 1.0.0 | MIDP-2.0 | - | 7 | 16 | 600 | 1558811772 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/DiamondRush.jar | 408 | Diamond Rush by GDC | - | 1.0.1 | MIDP-2.0 | - | 9 | 16 | 600 | 4054693 | 0 | javax/microedition/rms/InvalidRe | 运行正常 |  |
| vault/DiamondRush_1.1.6_320x240.jar | 300 | Diamond Rush | - | 1.1.6 | MIDP-2.0 | - | 12 | 16 | 600 | 3772584 | 0 | javax/microedition/rms/InvalidRe | 运行正常 |  |
| vault/DiamondTwister.jar | 594 | Diamond Twister (mobilegames | - | 1.0.7 | MIDP-2.0 | - | 6 | 16 | 0 | 0 | 0 | - | 非可用包 | archive entry is corrupt (Error -3 while dec |
| vault/DiamondTwister2.jar | 807 | Diamond Twister 2 | - | 1.1.0 | MIDP-2.0 | - | 8 | 16 | 601 | 11700898 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/Dictator Defense.jar | 656 | DictatorDef | - | 1.0.18 | MIDP-2.0 | - | 32 | 16 | 600 | 2021228 | 0 | java/io/EOFException,javax/micro | 运行正常 |  |
| vault/Die Hard 4.0 - The Mobile Game.jar | 593 | Die Hard 4.0 | - | 1.0.6 | MIDP-2.0 | - | 5 | 16 | 600 | 591469450 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/Dig_Dug_240x400.jar | 192 | Dig Dug by Namco | - | 2.1.0 | MIDP-2.0 | - | 13 | 16 | 600 | 6403704767 | 0 | - | 运行正常 |  |
| vault/Diner Dash Flo On The Go_320x240.jar | 576 | Diner Dash Flo on the Go | - | 1.0.0 | MIDP-2.0 | - | 37 | 16 | 600 | 37747004 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/DiscoveringAngelsAndDemons.jar | 177 | AngelsAndDemons | - | 1.0.0 | MIDP-2.0 | - | 10 | 16 | 600 | 3486205 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/DisneyBoardGames.jar | 624 | Disney Board Games | - | 1.1.1 | MIDP-2.0 | - | 36 | 16 | 600 | 6936298 | 0 | java/io/EOFException,javax/micro | 运行正常 |  |
| vault/Dogz3D_1.1.5_320x240.jar | 932 | DogZ 3D | - | 1.1.5 | MIDP-2.0 | m3g | 20 | 16 | 600 | 60047625 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/DominoFever_1.0.3_320x240.jar | 400 | Domino Fever | - | 1.0.3 | MIDP-2.0 | - | 9 | 16 | 600 | 5605740 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/DonaldDuckQuestDeluxe.jar | 190 | Donald Duck's Quest Deluxe | - | 2.0.0 | MIDP-1.0 | - | 9 | 16 | 600 | 1913681 | 0 | java/io/EOFException,java/lang/N | 运行正常 |  |
| vault/Dont Pull_320x240.jar | 111 | Dont Pull | - | 1.0.0 | MIDP-2.0 | - | 6 | 16 | 600 | 426845100 | 0 | java/lang/ArrayIndexOutOfBoundsE | 运行正常 |  |
| vault/Doodle Jump.jar | 452 | Doodle Jump | - | 1.3.0 | MIDP-2.0 | - | 7 | 16 | 600 | 989325546 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/Doom II RPG.jar | 748 | Doom II RPG | - | 5.1.89 | MIDP-2.0 | - | 35 | 16 | 600 | 4648340 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/Doom RPG.jar | 295 | Doom RPG | - | 1.4.67 | MIDP-2.0 | - | 26 | 16 | 600 | 258335 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/Dragon How To Train Your Dragon_320x240_Nokia_E71.jar | 355 | Dragon Konon.Mobi | - | 1.0.0 | MIDP-2.0 | - | 10 | 16 | 600 | 7229339 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/Dragon Mania.jar | 1604 | Dragon Mania | - | 1.0.0 | MIDP-2.0 | - | 24 | 16 | 600 | 596170174 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/Driver - San Francisco.jar | 1265 | Driver San Francisco | - | 1.2.4 | MIDP-2.0 | - | 11 | 16 | 600 | 68282003 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/Driver L.A. Undercover.jar | 539 | Driver | - | 1.0.5 | MIDP-2.0 | - | 6 | 16 | 600 | 229352757 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/DriverLAUndercover_1.0.5_320x240.jar | 317 | Driver | - | 1.0.5 | MIDP-2.0 | nokia-ui | 8 | 16 | 600 | 9954802 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/DriverSanFrancisco_1.0.4_320x240.jar | 840 | Driver San Francisco | - | 1.0.4 | MIDP-2.0 | - | 12 | 16 | 600 | 169117122 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/Dungeon Hunter - Curse Of Heaven.jar | 1012 | Dungeon Hunter Curse of Heav | - | 1.0.0 | MIDP-2.0 | - | 16 | 16 | 600 | 50845586 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/Dungeon Hunter 3.jar | 1334 | DUNGEON HUNTER 3 | - | 1.3.0 | MIDP-2.0 | - | 13 | 16 | 600 | 240999944 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/DungeonHunter3_1.1.0_320x240.jar | 887 | Dungeon Hunter 3 | - | 1.1.0 | MIDP-2.0 | - | 14 | 16 | 600 | 5101983716 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/Earthworm Jim.jar | 1026 | Earthworm Jim | - | 1.1.7 | MIDP-2.0 | - | 17 | 16 | 600 | 112910616 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/EarthwormJim_1.1.7_320x240.jar | 675 | Earthworm Jim | - | 1.1.7 | MIDP-2.0 | nokia-ui | 17 | 16 | 600 | 110743135 | 0 | - | 运行正常 |  |
| vault/Edge_320x240_Nokia_E71.jar | 376 | Edge Konon.Mobi | - | 4.0.0 | MIDP-2.0 | - | 4 | 16 | 600 | 16446957 | 0 | javax/microedition/io/Connection | 运行正常 |  |
| vault/El Che.jar | 386 | El Che | - | 1.0.4 | MIDP-2.0 | - | 26 | 16 | 600 | 2244141 | 0 | - | 运行正常 |  |
| vault/Elven Chronicles.jar | 233 | Elven Chronicles | - | 1.0.9 | MIDP-2.0 | - | 11 | 16 | 600 | 14479823 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/EverydayEnglishTrainer_1.0.2_320x240.jar | 605 | Everyday English Trainer | - | 1.0.2 | MIDP-2.0 | - | 4 | 16 | 600 | 5950937327 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/FIFA 2007.jar | 419 | FIFA 07 | - | 1.7.11 | MIDP-1.0 | - | 5 | 16 | 600 | 365342 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/Family Guy - Uncensored.jar | 471 | Family Guy Uncensored | - | 1.0.4 | MIDP-2.0 | - | 10 | 16 | 600 | 7200005 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/Family Guy 2.jar | 808 | Family Guy 2 | - | 1.0.0 | MIDP-2.0 | - | 25 | 16 | 600 | 15171259 | 0 | java/lang/ArrayIndexOutOfBoundsE | 运行正常 |  |
| vault/FarCry 2.jar | 626 | Far Cry 2 | - | 1.0.8 | MIDP-2.0 | - | 17 | 16 | 600 | 126040750 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/FarCry2_1.0.8_320x240.jar | 634 | Far Cry 2 | - | 1.0.8 | MIDP-2.0 | - | 17 | 16 | 600 | 127915436 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/Farm Tycoon__320x240.jar | 739 | FarmTycoon | - | 1.0 | MIDP-2.0 | - | 19 | 16 | 600 | 5104155 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/Fast Five The Movie - Official Game.jar | 1226 | Fast Five | - | 1.2.2 | MIDP-2.0 | - | 12 | 16 | 600 | 21604947 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/Fast and Furious 6.jar | 1917 | Fast & Furious 6 | - | 1.1.2 | MIDP-2.0 | - | 19 | 16 | 600 | 54786277 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/FastAndFurious6_1.0.6_320x240.jar | 1287 | Fast & Furious 6 | - | 1.0.6 | MIDP-2.0 | - | 15 | 16 | 601 | 128303740 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/Ferrari GT - Evolution.jar | 739 | Ferrari GT | - | 1.0.0 | MIDP-2.0 | - | 14 | 16 | 600 | 2314691 | 0 | - | 运行正常 |  |
| vault/Ferrari GT 2 - Revolution.jar | 992 | Ferrari GT 2: Revolution | - | 1.1.2 | MIDP-2.0 | - | 19 | 16 | 601 | 202666626 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/Ferrari GT 3 - World Track.jar | 1562 | Ferrari GT 3: World Track | - | 1.3.3 | MIDP-2.0 | - | 36 | 16 | 600 | 71671324 | 0 | java/io/IOException,java/lang/Nu | 运行正常 |  |
| vault/Ferrari World Championship 2009.jar | 988 | Ferrari World Championship 2 | - | 1.0.6 | MIDP-2.0 | - | 15 | 16 | 600 | 26432938 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/Ferrari World Championship.jar | 595 | Ferrari World Championship | - | 1.0.8 | MIDP-2.0 | - | 13 | 16 | 600 | 31749912 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/FerrariGT2Revolution_1.0.9_320x240.jar | 689 | Ferrari GT 2 Revolution | - | 1.0.9 | MIDP-2.0 | nokia-ui | 19 | 16 | 600 | 178090736 | 0 | - | 运行正常 |  |
| vault/Final Fantasy II.jar | 1343 | Final Fantasy II | - | 1.0.0 | MIDP-2.0 | - | 13 | 16 | 600 | 5323413 | 0 | java/lang/ArrayIndexOutOfBoundsE | 运行正常 |  |
| vault/Final Fantasy.jar | 1148 | Final Fantasy | - | 1.0.0 | MIDP-2.0 | - | 12 | 16 | 600 | 3555013 | 0 | - | 运行正常 |  |
| vault/FinalFantasy1_1.0.0_320x240_Nokia_E71.jar | 1153 | Final Fantasy | - | 1.0.0 | MIDP-2.0 | - | 13 | 16 | 600 | 5810698 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/FinalFantasyII_1.0.0_320x240.jar | 998 | Final fantasy II | - | 1.0.0 | MIDP-2.0 | - | 14 | 16 | 600 | 15144744 | 0 | java/lang/ArrayIndexOutOfBoundsE | 运行正常 |  |
| vault/Flat Out Racing 3D_320x240_Nokia_E71.jar | 344 | FlatOut Racing3D | - | 0.5.4 | MIDP-2.0 | m3g | 54 | 16 | 625 | 91559597 | 0 | java/io/IOException,java/lang/Cl | 运行正常 |  |
| vault/FlatOut 3D.jar | 348 | FlatOut Racing3D | - | 0.4.9 | MIDP-2.0 | - | 54 | 16 | 600 | 68779800 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/FlatOut.jar | 402 | FlatOut Racing | - | 0.5.7 | MIDP-2.0 | - | 22 | 16 | 600 | 24841953 | 0 | java/lang/IllegalArgumentExcepti | 运行正常 |  |
| vault/Fling_320x240.jar | 306 | Fling | - | 0.1.8 | MIDP-2.0 | nokia-ui | 30 | 16 | 600 | 756447 | 0 | java/io/IOException,java/lang/Nu | 运行正常 |  |
| vault/FortBoyard_1.0.2_320x240.jar | 577 | Fort Boyard | - | 1.0.2 | MIDP-2.0 | nokia-ui | 14 | 16 | 600 | 12337429 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/Frantic Factory_320x240.jar | 361 | Frantic Factory | - | 1.0.1 | MIDP-1.0 | - | 12 | 16 | 600 | 17107270 | 0 | - | 运行正常 |  |
| vault/Frogger Beats n' Bounces.jar | 216 | Frogger Beats n Bounces | - | 1.0.7 | MIDP-2.0 | - | 2 | 16 | 601 | 880065 | 0 | - | 运行正常 |  |
| vault/Frogger Evolution.jar | 224 | Frogger Evolution | - | 1.0.0 | MIDP-2.0 | - | 2 | 16 | 600 | 4709889 | 0 | - | 运行正常 |  |
| vault/FrozenPenguin2_1.0_320x240.jar | 515 | Frozen Penguin 2 | - | 1.0 | MIDP-2.0 | - | 44 | 16 | 600 | 36612252 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/FruitZum_1.0_320x240.jar | 252 | Fruit Zum | - | 1.0 | MIDP-2.0 | - | 12 | 16 | 600 | 2230553 | 0 | java/io/IOException,java/lang/Nu | 运行正常 |  |
| vault/Fullmetal Fortress_320x240.jar | 114 | FULLMETAL FORTRESS | - | 1.1.0 | MIDP-1.0 | - | 7 | 16 | 600 | 82820 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/FurbyIsland.jar | 372 | Furby Island | - | 1.0.17 | MIDP-2.0 | - | 17 | 16 | 600 | 3061067 | 0 | java/lang/ArrayIndexOutOfBoundsE | 运行正常 |  |
| vault/G.I. Joe - The Rise of Cobra.jar | 678 | G.I. JOE | - | 7.2.50 | MIDP-2.0 | - | 13 | 16 | 600 | 1006582 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/GT Racing - Motor Academy.jar | 992 | GT Racing: Motor Academy | - | 1.2.2 | MIDP-2.0 | - | 12 | 16 | 600 | 2878434 | 0 | - | 运行正常 |  |
| vault/GT Racing 2 - The Real Car Experience.jar | 1812 | GT Racing 2 | - | 1.0.9 | MIDP-2.0 | - | 11 | 16 | 600 | 17605209 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/Galaga 3.jar | 141 | Galaga 3 | - | 1.0.2 | MIDP-2.0 | - | 13 | 16 | 600 | 12001759 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/Galaga X.jar | 505 | Galaga X | - | 1.0.5 | MIDP-2.0 | - | 33 | 16 | 600 | 40249450 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/Galaga3.jar | 141 | Galaga 3 | - | 1.0.2 | MIDP-2.0 | - | 13 | 16 | 600 | 4093613524 | 0 | java/util/NoSuchElementException | 运行正常 |  |
| vault/Galazer Deluxe.jar | 715 | Galazer Deluxe | - | 1.1.6 | MIDP-2.0 | - | 63 | 16 | 600 | 428041 | 0 | - | 运行正常 |  |
| vault/Gameloft Brother In Arms Earned In Bloods 1.2.4.jar | 222 | Brothers In Arms | - | 1.2.5 | MIDP-2.0 | nokia-ui | 7 | 16 | 600 | 2817078104 | 0 | java/io/IOException | 运行正常 |  |
| vault/Gameloft Christmas Block Breaker.jar | 148 | ChristmasBlockBreaker | - | 1.0.2 | MIDP-1.0 | nokia-ui | 8 | 16 | 600 | 2062678520 | 0 | - | 运行正常 |  |
| vault/Gameloft Midnight Bowling.jar | 217 | MidnightBowling [stuart2773] | - | 1.0.7 | MIDP-2.0 | - | 4 | 16 | 600 | 3911142158 | 0 | java/lang/IllegalArgumentExcepti | 运行正常 |  |
| vault/Gameloft.Mystery.Mansion.Pinball.1.2.1-Retail.JAVA.teamSMPDA.jar | 222 | MysteryMansionPinball | - | 1.2.1 | MIDP-1.0 | nokia-ui | 5 | 16 | 600 | 7067590214 | 0 | javax/microedition/rms/InvalidRe | 运行正常 |  |
| vault/Gangstar - Crime City.jar | 324 | Gangstar - Crime City | - | 1.0.3 | MIDP-2.0 | - | 5 | 16 | 600 | 471501210 | 0 | javax/microedition/rms/InvalidRe | 运行正常 |  |
| vault/Gangstar - Miami Vindication.jar | 1268 | GangStar: Miami Vindication | - | 1.1.8 | MIDP-2.0 | - | 10 | 16 | 600 | 424838050 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/Gangstar 2 - Kings of L.A..jar | 987 | Gangstar 2 | - | 1.0.7 | MIDP-2.0 | - | 11 | 16 | 600 | 360167875 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/Gangstar City.jar | 1621 | Gangstar City | - | 1.1.1 | MIDP-2.0 | - | 16 | 16 | 600 | 547991627 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/Gangstar Rio - City of Saints.jar | 1449 | Gangstar Rio: City of Saints | - | 1.0.9 | MIDP-2.0 | - | 13 | 16 | 600 | 274430988 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/Gangstar1CrimeCity_1.0.0_320x240.jar | 321 | Gangstar Crime City | - | 1.0.0 | MIDP-2.0 | - | 6 | 16 | 600 | 5239558850 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/Gangstar2KingsOfLA_1.1.2_320x240.jar | 692 | Gangstar 2: Kings of L.A | - | 1.1.2 | MIDP-2.0 | - | 11 | 16 | 600 | 1246282 | 0 | - | 运行正常 | self-paced title: runs 600 frames unpaced (t |
| vault/Gangstar3MiamiVindication_1.0.0_320x240.jar | 348 | Gangstar Miami | - | 1.0.0 | MIDP-2.0 | - | 5 | 16 | 600 | 5244366039 | 0 | javax/microedition/rms/InvalidRe | 运行正常 |  |
| vault/Gauntlet_320x240.jar | 969 | Gauntlet | - | 4.2.3 | MIDP-2.0 | - | 23 | 16 | 600 | 4546063 | 0 | - | 运行正常 |  |
| vault/GemDropDeluxe.jar | 1243 | Gem Drop Deluxe | - | 1.0.3 | MIDP-2.0 | nokia-ui | 2 | 16 | 600 | 6564472905 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/GhostRecon2AdvancedWarfighter_1.0.0_320x240.jar | 330 | G.R.A.W. 2 | - | 1.0.0 | MIDP-2.0 | - | 6 | 16 | 600 | 2377578014 | 0 | - | 运行正常 |  |
| vault/GianaSisters.jar | 103 | Super Mario | - | 1.0.2 | MIDP-2.0 | nokia-ui | 23 | 16 | 600 | 2333753 | 0 | java/io/IOException,java/lang/Nu | 运行正常 |  |
| vault/Go Go Rescue Squad_320x240.jar | 393 | Go! Go! Rescue Squad | - | 0.0.8 | MIDP-2.0 | - | 12 | 16 | 600 | 30088756 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/God of War - Betrayal.jar | 488 | God Of War | - | 1.4.46 | MIDP-2.0 | - | 6 | 16 | 600 | 142966717 | 0 | java/lang/NumberFormatException, | 运行正常 |  |
| vault/GoldDigger.jar | 924 | Gold Digger | - | 1.0 | MIDP-2.0 | nokia-ui | 23 | 16 | 600 | 420039160 | 0 | java/lang/IllegalArgumentExcepti | 运行正常 |  |
| vault/GoldenSolitaire.jar | 233 | Golden Solitaire | - | 1.0.3 | MIDP-2.0 | - | 17 | 16 | 600 | 35324527 | 0 | javax/microedition/rms/InvalidRe | 运行正常 |  |
| vault/Gorilla Rampage.jar | 957 | GorillaAngriff | - | 1.0.1 | MIDP-2.0 | - | 38 | 16 | 600 | 742024 | 0 | java/io/EOFException,javax/micro | 运行正常 |  |
| vault/Green Farm 3.jar | 1435 | Green Farm 3 | - | 1.0.7 | MIDP-2.0 | - | 22 | 16 | 600 | 63728788 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/Guitar Hero - Warriors of Rock Mobile More Music.jar | 615 | Guitar Hero Warriors of Rock | - | 3.1.0 | MIDP-2.0 | - | 25 | 16 | 600 | 20853841 | 0 | java/lang/ArrayIndexOutOfBoundsE | 运行正常 |  |
| vault/Guitar Hero - Warriors of Rock Mobile.jar | 1013 | Guitar Hero Warriors of Rock | - | 1.0.0 | MIDP-2.0 | - | 24 | 16 | 600 | 25984147 | 0 | javax/microedition/rms/InvalidRe | 运行正常 |  |
| vault/Guitar Hero World Your - Mobile.jar | 720 | Guitar Hero World Tour | - | 1.0.11 | MIDP-2.0 | - | 33 | 16 | 600 | 1002215 | 0 | java/io/IOException | 运行正常 |  |
| vault/Guitar Rock Tour.jar | 598 | Guitar Rock Tour | - | 1.0.8 | MIDP-2.0 | - | 6 | 16 | 600 | 321189571 | 0 | java/lang/NumberFormatException | 运行正常 |  |
| vault/Gumball Journey to the Moon-3-240x320.jar | 794 | Gumball | - | 1.1.0 | MIDP-2.0 | - | 15 | 16 | 600 | 1651445 | 0 | - | 运行正常 |  |
| vault/GunFever.jar | 167 | GunFever | - | 1.0.4 | MIDP-2.0 | - | 9 | 16 | 600 | 540289 | 0 | - | 运行正常 |  |
| vault/Guns, Wheels & Madheads 2.jar | 310 | Guns Wheels Madheads 2 | - | 1.1.0 | MIDP-2.0 | - | 27 | 16 | 600 | 708419 | 0 | - | 运行正常 |  |
| vault/Guns, Wheels & Madheads.jar | 329 | Guns Wheels Madheads | - | 1.1.0 | MIDP-2.0 | - | 28 | 16 | 600 | 80131483 | 0 | - | 运行正常 |  |
| vault/HarryPotterAndTheDeathlyHallowsPart2_1.2.4_320x240.jar | 956 | Harry Potter 7 P2 | - | 1.2.4 | MIDP-2.0 | - | 17 | 16 | 600 | 22833482 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/Hero of Sparta.jar | 993 | Hero Of Sparta | - | 1.1.9 | MIDP-2.0 | - | 9 | 16 | 600 | 636602976 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/HeroOfSparta_1.0.2_320x240.jar | 602 | Hero Of Sparta | - | 1.0.2 | MIDP-2.0 | - | 9 | 16 | 606 | 513217712 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/HeroesLore.jar | 827 | Heroes Lore by SunYu_911 | - | 2.0.7 | MIDP-2.0 | - | 90 | 16 | 601 | 4923613 | 0 | java/lang/ClassCastException,jav | 运行正常 |  |
| vault/Heroes_1.0.0_320x240.jar | 536 | Heroes | - | 1.0.0 | MIDP-2.0 | - | 5 | 16 | 600 | 132936624 | 0 | java/lang/ArrayIndexOutOfBoundsE | 运行正常 |  |
| vault/Hexic.jar | 198 | Hexic | - | 1.0.9 | MIDP-2.0 | - | 12 | 16 | 600 | 5895248 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/IQ Booster Logic__320x240.jar | 508 | IQ Booster Logic | - | 1.0.0 | MIDP-2.0 | - | 101 | 16 | 600 | 44664150 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/Ice Age 4 - Continental Drift.jar | 1527 | Ice Age 4: Continental Drift | - | 1.1.6 | MIDP-2.0 | - | 17 | 16 | 600 | 196936691 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/Ice Age Scrat-ventures.jar | 1016 | ICE AGE SCRAT-VENTURES | - | 1.0.1 | MIDP-2.0 | - | 24 | 16 | 600 | 84498029 | 0 | - | 运行正常 |  |
| vault/Ice Age Village.jar | 1791 | Ice Age Village | - | 1.0.9 | MIDP-2.0 | - | 19 | 16 | 600 | 196237724 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/IdolLines.jar | 295 | Idol Lines (www.mobilegamesa | - | 1.0 | MIDP-2.0 | - | 10 | 16 | 600 | 3039075 | 0 | java/io/IOException,java/lang/Ar | 运行正常 |  |
| vault/Immortals.jar | 1387 | Immortals | - | 1.0.7 | MIDP-2.0 | - | 13 | 16 | 600 | 130566992 | 0 | - | 运行正常 |  |
| vault/IncaQuest.jar | 301 | Inca Quest | - | 0.8.1 | MIDP-1.0 | - | 16 | 16 | 600 | 4365348 | 0 | java/io/IOException,javax/microe | 运行正常 |  |
| vault/IncaStory.jar | 152 | IncaStory | - | 1.0 | MIDP-2.0 | - | 4 | 16 | 600 | 3631816285 | 0 | - | 运行正常 |  |
| vault/Iron Man 2.jar | 1074 | Iron Man 2 | - | 1.1.4 | MIDP-2.0 | - | 9 | 16 | 600 | 133717447 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/Iron Man 3.jar | 1753 | Iron Man 3 | - | 1.1.2 | MIDP-2.0 | - | 12 | 16 | 600 | 166184845 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/IstanBUL.jar | 2793 | istanBUL | - | 1.0.0 | MIDP-2.0 | - | 21 | 16 | 600 | 517812 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/JMEBoy_with-games.jar | 698 | JMEBoy | - | 1.5.3 | MIDP-2.0 | nokia-ui | 94 | 16 | 600 | 2010203 | 0 | java/lang/NullPointerException,j | 运行正常 | painted 604 frames but made no draw calls (b |
| vault/James Cameron's Avatar.jar | 998 | Avatar | - | 1.0.6 | MIDP-2.0 | - | 9 | 16 | 600 | 21523502 | 0 | - | 运行正常 |  |
| vault/JetSetWilly.jar | 177 | Jet Set Willy | - | 1.0.1 | MIDP-1.0 | nokia-ui | 14 | 16 | 600 | 1087727 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/Jewel Conquest__320x240.jar | 270 | Jewel | - | 1.0 | MIDP-2.0 | - | 8 | 16 | 600 | 7193590 | 0 | - | 运行正常 |  |
| vault/Jewel Explosion_320x240.jar | 354 | Jewel Explosion | - | 1.0.0 | MIDP-2.0 | - | 35 | 16 | 600 | 2199134113 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/Jewel Quest III World Adventure_320x240_Nokia_E71.jar | 764 | Jewel Quest III World Advent | - | 4.5.57 | MIDP-2.0 | - | 25 | 16 | 600 | 10956344 | 0 | - | 运行正常 |  |
| vault/JewelPack2in1_2.0_320x240.jar | 394 | Jewel Pack 2in1 | - | 2.0 | MIDP-2.0 | - | 77 | 16 | 600 | 1313481065 | 0 | java/io/IOException,java/lang/Nu | 运行正常 |  |
| vault/JewelQuest.jar | 164 | Jewel Quest | - | 1.0.5 | MIDP-1.0 | - | 19 | 16 | 600 | 440266 | 0 | java/io/IOException | 运行正常 |  |
| vault/JewelQuest2.jar | 315 | JewelQuest2 | - | 0.3.2 | MIDP-1.0 | - | 14 | 16 | 600 | 1159895 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/JewelQuest3.jar | 585 | Jewel Quest III World Advent | - | 4.2.84 | MIDP-2.0 | - | 25 | 16 | 600 | 2666813 | 0 | - | 运行正常 |  |
| vault/JewelQuestDeluxe.jar | 743 | Jewel Quest Deluxe | - | 1.1.2 | MIDP-2.0 | - | 5 | 16 | 600 | 402167 | 1 | java/io/IOException,java/lang/Cl | 标题自身异常 | game code threw an exception in a thread whi |
| vault/Jurassic Park.jar | 1102 | Jurassic Park | - | 1.1.2 | MIDP-2.0 | - | 12 | 16 | 600 | 141527149 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/JurassicPark_1.2.2_320x240.jar | 911 | Jurassic Park | - | 1.2.2 | MIDP-2.0 | - | 12 | 16 | 600 | 267806096 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/K.O. Legends.jar | 1024 | K.O. Legends | - | 1.0.6 | MIDP-2.0 | - | 23 | 16 | 600 | 226749888 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/KOFighters_1.0.0_320x240.jar | 323 | KO Fighters | - | 1.0.0 | MIDP-2.0 | - | 12 | 16 | 600 | 24737884 | 0 | java/lang/ArrayIndexOutOfBoundsE | 运行正常 |  |
| vault/Kartmania 3D.jar | 934 | Kartmania3D | - | 1.0.2 | MIDP-2.0 | - | 123 | 16 | 600 | 4141190 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/KasparovChess.jar | 189 | Kasparov Chess | - | 1.0.1 | MIDP-1.0 | - | 6 | 16 | 600 | 5063860 | 0 | javax/microedition/rms/InvalidRe | 运行正常 |  |
| vault/King Kong.jar | 297 | KingKong | - | 1.0.3 | MIDP-2.0 | - | 5 | 16 | 600 | 60703102 | 0 | - | 运行正常 |  |
| vault/KoraDeluxe3D_1.0.4_320x240.jar | 947 | KORa Deluxe | - | 1.0.4 | MIDP-2.0 | m3g | 123 | 16 | 600 | 200149794 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/KungFuPanda_4.9.12_320x240.jar | 544 | Kung Fu Panda | - | 4.9.12 | MIDP-2.0 | nokia-ui | 26 | 16 | 600 | 13427344 | 0 | java/lang/IllegalMonitorStateExc | 运行正常 |  |
| vault/LOST.jar | 308 | LOST | - | 1.0.2 | MIDP-2.0 | - | 6 | 16 | 600 | 346817663 | 0 | - | 运行正常 |  |
| vault/Lara Crofts Poker Party (CLDC-1.0, MIDP-1.0) v1.0.1 (132x176).jar | 102 | Lara Crofts Poker Party | - | 1.0.1 | MIDP-1.0 | - | 15 | 16 | 600 | 1941201 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/LasVegasNights_1.1.6_320x240.jar | 635 | Las Vegas Nights | - | 1.1.6 | MIDP-2.0 | - | 10 | 16 | 600 | 11968307 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/LegoBatman_1.3.0_320x240.jar | 1004 | Lego Batman | - | 1.3.0 | MIDP-2.0 | - | 18 | 16 | 600 | 203678435 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/Let's Go Bowling.jar | 961 | Let's go bowling! | - | 1.1.8 | MIDP-2.0 | - | 23 | 16 | 600 | 239404490 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/Let's Golf.jar | 1027 | Let's Golf! | - | 1.1.1 | MIDP-2.0 | - | 7 | 16 | 600 | 112541540 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/LetsGoBowling_1.0.4_320x240.jar | 698 | Let's go bowling! | - | 1.0.4 | MIDP-2.0 | m3g | 22 | 16 | 601 | 66451489 | 0 | - | 运行正常 |  |
| vault/LetsGolf_1.0.7_320x240.jar | 634 | Let's Golf! | - | 1.0.7 | MIDP-2.0 | - | 9 | 16 | 600 | 62761123 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/LiveFreeOrDieHard_1.0.0_320x240.jar | 326 | Live Free Or Die Hard | - | 1.0.0 | MIDP-2.0 | nokia-ui | 5 | 16 | 600 | 4950806219 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/Looney Tunes Monster Match.jar | 674 | LooneyTunes Monster Match | - | 1.0.0 | MIDP-2.0 | - | 44 | 16 | 600 | 5111906 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/LooneyTunesMonsterMatch.jar | 675 | LooneyTunes Monster Match (w | - | 1.0.0 | MIDP-2.0 | - | 44 | 16 | 600 | 22104063 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/LostPlanet2_1.0.7_320x240.jar | 625 | Lost Planet 2 | - | 1.0.7 | MIDP-2.0 | - | 16 | 16 | 600 | 134243004 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/Lost_1.0.0_320x240.jar | 301 | LOST | - | 1.0.0 | MIDP-2.0 | - | 6 | 16 | 600 | 7029393042 | 0 | - | 运行正常 |  |
| vault/Lucha Libre - Desafio Total.jar | 517 | Lucha Libre Desafio Total | - | 1.1.7 | MIDP-2.0 | - | 6 | 16 | 601 | 354716059 | 0 | - | 运行正常 |  |
| vault/Lumines_1.0.0_320x240.jar | 428 | Lumines | - | 1.0.0 | MIDP-2.0 | - | 6 | 16 | 600 | 2957451199 | 0 | - | 运行正常 |  |
| vault/Luxor Quest_320x240.jar | 374 | Luxor Quest | - | 1.1.9 | MIDP-2.0 | - | 21 | 16 | 600 | 4966219 | 0 | java/io/IOException,java/lang/Nu | 运行正常 |  |
| vault/Luxor2.jar | 650 | LUXOR 2 | - | 1.0.11 | MIDP-2.0 | nokia-ui | 6 | 16 | 600 | 376526048 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/LuxorQuest.jar | 755 | Luxor Quest_freemobilejavaga | - | 1.1.1 | MIDP-2.0 | - | 22 | 16 | 600 | 3459692 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/Magic Wing II Ep.1.jar | 140 | MagicWingIIEp1\gkataria | - | 1.0 | MIDP-1.0 | - | 3 | 16 | 600 | 225925262 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/MagicGarden.jar | 237 | MagicGarden by drAdeLante | - | 1.0.1 | MIDP-2.0 | - | 13 | 16 | 601 | 400403 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/MahjongAdventure_1.0.2_320x240.jar | 269 | Mahjong Adventure | - | 1.0.2 | MIDP-2.0 | - | 33 | 16 | 600 | 5023467796 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/MahjongQuest.jar | 214 | Mah Jong Quest | - | 1.0.5 | MIDP-1.0 | - | 14 | 16 | 600 | 439897 | 0 | java/io/IOException,java/lang/Nu | 运行正常 |  |
| vault/MahjongStar_1.0.0_320x240.jar | 235 | Mahjong | - | 1.3.6 | MIDP-2.0 | - | 12 | 16 | 600 | 3007008 | 0 | java/io/IOException | 运行正常 |  |
| vault/Mahjong_1.3.6_320x240.jar | 264 | Mahjong Stars | - | 1.0.0 | MIDP-2.0 | - | 33 | 16 | 600 | 5055297452 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/Majesty Royal Simulator_320x240.jar | 1137 | Majesty | - | 1.13.34 | MIDP-2.0 | - | 77 | 16 | 600 | 36147681 | 0 | java/io/EOFException,java/io/IOE | 运行正常 |  |
| vault/Marble Madness.jar | 636 | Marble Madness | - | 4.5.84 | MIDP-2.0 | - | 32 | 16 | 600 | 5168146 | 0 | javax/microedition/rms/InvalidRe | 运行正常 |  |
| vault/March of Heroes.jar | 1496 | March Of Heroes | - | 1.1.2 | MIDP-2.0 | - | 17 | 16 | 209 | 38698074 | 0 | java/lang/NullPointerException | 标题自行退出 | exited itself (notifyDestroyed) after 209 fr |
| vault/MarchOfHeroes_1.0.9_320x240.jar | 970 | March Of Heroes | - | 1.0.9 | MIDP-2.0 | - | 19 | 16 | 600 | 119667025 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/Mech Strategy Wars.jar | 698 | MechStrategyWars | - | 1.0.6 | MIDP-2.0 | - | 13 | 16 | 600 | 4504832 | 0 | - | 运行正常 |  |
| vault/Medal Chase 2__320x240.jar | 179 | MedalChase2 | - | 1.0.0 | MIDP-2.0 | - | 9 | 16 | 600 | 3742838 | 0 | - | 运行正常 |  |
| vault/Medal Honor10_4.14.37_320x240.jar | 652 | Medal Honor 10 | - | 4.14.37 | MIDP-2.0 | - | 32 | 16 | 600 | 21883815 | 0 | - | 运行正常 |  |
| vault/Medal of Honor - Airborne - Elite.jar | 559 | Medal Of Honor Airborne Elit | - | 1.0.0 | MIDP-2.0 | - | 32 | 16 | 600 | 10557545 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/Medal of Honor - Airborne 3D.jar | 587 | Medal Of Honor Airborne | - | 7.1.33 | MIDP-2.0 | - | 40 | 16 | 600 | 8893457 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/Medal of Honor - Airborne.jar | 547 | Medal Of Honor Airborne | - | 4.5.25 | MIDP-2.0 | - | 32 | 16 | 600 | 10342281 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/Medal of Honor 2010.jar | 1118 | Medal of Honor 2010 | - | 4.4.47 | MIDP-2.0 | - | 33 | 16 | 600 | 1531108 | 0 | - | 运行正常 |  |
| vault/Medal of Honor.jar | 418 | Medal of Honor | - | 1.12.73 | MIDP-2.0 | - | 41 | 16 | 600 | 618759417 | 0 | - | 运行正常 |  |
| vault/Medieval Combat - Age of Glory.jar | 267 | Medieval Combat - Age of Glo | - | 1.0.4 | MIDP-2.0 | - | 6 | 16 | 600 | 162138922 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/Mega Tower Assault.jar | 848 | Mega Tower Assault | - | 1.2.6 | MIDP-2.0 | - | 16 | 16 | 600 | 5583007 | 0 | - | 运行正常 |  |
| vault/MegaMan 3_320x240.jar | 210 | Mega Man III | - | 1.0.0 | MIDP-2.0 | - | 14 | 16 | 600 | 2808839 | 0 | java/io/EOFException,java/lang/A | 运行正常 |  |
| vault/MegaTowerAssault_1.3.3_320x240.jar | 772 | Mega Tower Assault | - | 1.3.3 | MIDP-2.0 | nokia-ui | 16 | 16 | 600 | 5922729 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/Men in Black 3.jar | 1618 | Men in Black 3 | - | 1.3.0 | MIDP-2.0 | - | 17 | 16 | 600 | 79140841 | 0 | - | 运行正常 |  |
| vault/Metal Gear Acid 2 3D.jar | 595 | Metal Gear Acid 2 3D | - | 1.0.0 | MIDP-2.0 | - | 37 | 16 | 600 | 9462724 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/Metal Gear Acid 2.jar | 426 | Metal Gear Acid 2 | - | 1.0.0 | MIDP-2.0 | - | 24 | 16 | 600 | 10986848 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/Metal Gear Acid 3D.jar | 561 | MGA Mobile | - | 1.0.0 | MIDP-1.0 | - | 6 | 16 | 600 | 11365114 | 0 | java/lang/IllegalArgumentExcepti | 运行正常 |  |
| vault/Metal Gear Acid.jar | 373 | MGA Mobile | - | 1.0.0 | MIDP-1.0 | - | 5 | 16 | 600 | 11343243 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/Metal Gear.jar | 301 | Metal Gear | - | 0.0.4 | MIDP-2.0 | - | 12 | 16 | 600 | 1571147 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/Metal Slug 4 Mobile.jar | 681 | Metal Slug M4 | - | 1.1.1 | MIDP-2.0 | - | 6 | 16 | 600 | 3272600 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/MetalGearAcid.jar | 410 | MGA Mobile | - | 1.0.0 | MIDP-1.0 | - | 6 | 16 | 600 | 7870358 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/MetalGearAcid2.jar | 604 | Metal Gear Acid 2 3D | - | 1.0.0 | MIDP-2.0 | m3g | 34 | 16 | 600 | 10202901 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/MetalSlug3.jar | 302 | Metal Slug Mobile 3 | - | 0.4.8 | MIDP-2.0 | - | 3 | 16 | 600 | 5934016 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/MetalSlug4.jar | 768 | Metal Slug 4 | - | 1.0.1 | MIDP-2.0 | - | 6 | 16 | 600 | 8760198 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/MetalSlug4_1.1.5_320x240.jar | 748 | Metal Slug 4 | - | 1.1.5 | MIDP-2.0 | - | 4 | 16 | 600 | 5626819 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/Meteos - Astro Blocks.jar | 290 | Meteos | - | 1.0.5 | MIDP-2.0 | - | 9 | 16 | 600 | 254565967 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/MeteosAstroBlocks.jar | 296 | Meteos - Astro Blocks | - | 1.0.4 | MIDP-2.0 | - | 7 | 16 | 600 | 4299911726 | 0 | java/lang/ArrayIndexOutOfBoundsE | 运行正常 |  |
| vault/MiamiNights2_1.0.3_320x240.jar | 632 | Miami Nights 2 | - | 1.0.3 | MIDP-2.0 | - | 7 | 16 | 600 | 643052093 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/Midnight Bowling 2.jar | 588 | Midnight Bowling 2 | - | 1.1.1 | MIDP-2.0 | - | 5 | 16 | 600 | 185538582 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/Midnight Bowling 3.jar | 1430 | Midnight Bowling 3 | - | 1.1.3 | MIDP-2.0 | - | 30 | 16 | 600 | 38737350 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/Midnight Bowling.jar | 260 | MidnightBowling | - | 1.0.1 | MIDP-2.0 | - | 5 | 16 | 600 | 2984109 | 0 | javax/microedition/rms/InvalidRe | 运行正常 |  |
| vault/Midnight Pool 2.jar | 605 | Midnight Pool 2 | - | 1.0.9 | MIDP-2.0 | - | 13 | 16 | 600 | 396033219 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/Midnight Pool 3.jar | 1348 | Midnight Pool 3 | - | 1.2.2 | MIDP-2.0 | - | 29 | 16 | 600 | 51654719 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/Midnight Pool 4.jar | 656 | Midnight Pool | - | 1.0.2 | MIDP-2.0 | - | 26 | 16 | 600 | 43069956 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/Midnight Pool.jar | 598 | Midnight Pool | - | 1.0.3 | MIDP-2.0 | - | 6 | 16 | 600 | 1838293 | 0 | - | 运行正常 |  |
| vault/MidnightBowling2_1.0.1_320x240.jar | 558 | Midnight Bowling 2 | - | 1.0.1 | MIDP-2.0 | nokia-ui | 5 | 16 | 600 | 274255869 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/MidnightDarts_1.0.3_320x240.jar | 563 | MIDNIGHT DARTS | - | 1.0.3 | MIDP-2.0 | - | 6 | 16 | 600 | 13410005 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/MidnightPool1_1.0.0_320x240.jar | 205 | Midnight Pool | - | 1.0.0 | MIDP-2.0 | - | 5 | 16 | 600 | 6190021211 | 0 | - | 运行正常 |  |
| vault/MidnightPool2_1.0.1_320x240.jar | 406 | Midnight Pool 2 | - | 1.0.1 | MIDP-2.0 | - | 14 | 16 | 600 | 4911607752 | 0 | java/lang/ClassNotFoundException | 运行正常 |  |
| vault/MidnightPool3_1.2.4_320x240.jar | 1265 | Midnight Pool 3 | - | 1.2.4 | MIDP-2.0 | - | 29 | 16 | 600 | 115642823 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/Mig 2D Retro Shooter.jar | 1494 | Mig29 2D Retro Shooter | - | 1.0.0 | MIDP-2.0 | - | 75 | 16 | 600 | 36769345 | 0 | java/io/EOFException,java/io/IOE | 运行正常 |  |
| vault/Might and Magic II.jar | 610 | MightAndMagicII | - | 1.1.8 | MIDP-2.0 | - | 12 | 16 | 600 | 190609006 | 0 | java/lang/ClassNotFoundException | 运行正常 |  |
| vault/Might and Magic.jar | 189 | MightAndMagic | - | 1.0.0 | MIDP-2.0 | - | 5 | 16 | 600 | 172832600 | 0 | - | 运行正常 |  |
| vault/MightAndMagic.jar | 229 | MightAndMagic | - | 1.0.6 | MIDP-2.0 | - | 7 | 16 | 600 | 5056280408 | 0 | - | 运行正常 |  |
| vault/MightAndMagicII_1.0.2_320x240.jar | 609 | Might And Magic II | - | 1.0.2 | MIDP-2.0 | nokia-ui | 9 | 16 | 600 | 202248500 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/Million Dollar Poker.jar | 594 | Million Dollar Poker | - | 1.0.6 | MIDP-2.0 | - | 10 | 16 | 600 | 186553723 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/MillionDollarPokerFeatGusHansen_1.0.0_320x240.jar | 480 | Million Dollar Poker | - | 1.0.0 | MIDP-2.0 | nokia-ui | 10 | 16 | 600 | 3386976321 | 0 | - | 运行正常 |  |
| vault/Millionaire City.jar | 751 | MillCity | - | 1.0.25 | MIDP-2.0 | - | 64 | 16 | 600 | 1855441 | 0 | java/io/EOFException,javax/micro | 运行正常 |  |
| vault/MindGames.jar | 186 | Mind Games by Kowciu | - | 1.0.17 | MIDP-2.0 | - | 17 | 16 | 600 | 2836393 | 0 | javax/microedition/rms/InvalidRe | 运行正常 |  |
| vault/MindGames2.jar | 461 | Mind Games 2 | - | 1.0.0 | MIDP-2.0 | nokia-ui | 24 | 16 | 600 | 3001397 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/Mini Golf 99 Challenge 2010_320x240.jar | 611 | MGolf 99 Chall | - | 1.0.3 | MIDP-2.0 | nokia-ui | 37 | 16 | 600 | 18362706 | 0 | java/io/EOFException,java/lang/N | 运行正常 |  |
| vault/Mini Golf Magic 3D.jar | 334 | MiniGolfMagic by [wl] | - | 1.5.05 | MIDP-2.0 | - | 18 | 16 | 600 | 1657947 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/MiniGolf Theme Park - 99 Holes.jar | 469 | MGolf Park 99 | - | 1.0.11 | MIDP-2.0 | - | 36 | 16 | 600 | 12320689 | 0 | java/io/EOFException,javax/micro | 运行正常 |  |
| vault/MiniGolfThemePark.jar | 744 | MGolf Park 99_www.antofungky | - | 1.0.15 | MIDP-2.0 | nokia-ui | 39 | 16 | 600 | 12449365 | 0 | java/io/EOFException,java/lang/C | 运行正常 |  |
| vault/Minigolf Revolution - Pirate Park.jar | 581 | Minigolf Revolution Pirate P | - | 1.0.1 | MIDP-2.0 | - | 7 | 16 | 600 | 9194388 | 0 | - | 运行正常 |  |
| vault/Mission Impossible III.jar | 313 | Mission Impossible III | - | 1.0.0 | MIDP-2.0 | - | 4 | 16 | 600 | 430581033 | 0 | - | 运行正常 |  |
| vault/MissionImpossible3_1.0.0_320x240.jar | 293 | Mission Impossible 3 | - | 1.0.0 | MIDP-2.0 | nokia-ui | 4 | 16 | 600 | 3037750513 | 0 | - | 运行正常 |  |
| vault/Modern Combat 2 - Black Pegasus.jar | 1344 | Modern Combat 2: Black Pegas | - | 1.2.2 | MIDP-2.0 | - | 13 | 16 | 600 | 161802566 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/Modern Combat 4 - Zero Hour.jar | 1539 | Modern Combat4: Zero Hour | - | 1.1.2 | MIDP-2.0 | - | 12 | 16 | 600 | 270126237 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/MonopolyUBuild_1.9.2_320x240.jar | 518 | MONOPOLY U-Build www.wapx.ws | - | 1.9.2 | MIDP-2.0 | - | 28 | 16 | 600 | 25791597 | 0 | javax/microedition/rms/InvalidRe | 运行正常 |  |
| vault/Monsters University.jar | 1355 | Monsters University | - | 1.1.2 | MIDP-2.0 | - | 13 | 16 | 600 | 21945927 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/Moto Racing  Fever 3D.jar | 312 | Moto Racing Fever | - | 1.2.0 | MIDP-2.0 | - | 27 | 16 | 600 | 12647329 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/MotocrossTrailExtreme_1.0.5_320x240.jar | 1049 | Motocross Trial Extreme | - | 1.0.5 | MIDP-2.0 | - | 11 | 16 | 600 | 182535681 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/MrSudoku.jar | 112 | MrSuDoku Tegos.ru | - | 1.0 | MIDP-2.0 | nokia-ui | 3 | 16 | 600 | 656612 | 0 | java/io/IOException,javax/microe | 运行正常 |  |
| vault/Ms Pac Man_320x240.jar | 237 | Ms. PAC-MAN | - | 1.4.21 | MIDP-2.0 | - | 47 | 16 | 600 | 3550146 | 0 | java/io/EOFException,javax/bluet | 运行正常 |  |
| vault/Ms. PAC-MAN (176x220) (1.0.0).jar | 59 | Ms. PAC-MAN | - | 1.0.0 | MIDP-2.0 | - | 10 | 16 | 600 | 276539341 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/Ms. PAC-MAN Demo (176x220) (1.00).jar | 73 | Ms. PAC-MAN DEMO | - | 1.00 | MIDP-2.0 | - | 2 | 16 | 600 | 3705278513 | 0 | - | 运行正常 |  |
| vault/Ms. PAC-MAN Demo by Namco (128x160) (1.5.1).jar | 59 | Ms. PAC-MAN Demo by Namco | - | 1.5.1 | MIDP-1.0 | - | 10 | 16 | 600 | 800594 | 0 | - | 运行正常 |  |
| vault/Ms. PAC-MAN Demo by Namco (176x220) (2.1.0).jar | 80 | Ms. PAC-MAN by Namco | - | 2.1.0 | MIDP-2.0 | - | 21 | 16 | 600 | 3538539 | 0 | javax/microedition/io/Connection | 运行正常 |  |
| vault/Ms. PAC-MAN Demo by Namco (240x400) (2.1.0).jar | 190 | Ms. PAC-MAN by Namco | - | 2.1.0 | MIDP-2.0 | - | 22 | 16 | 600 | 3124639 | 0 | java/io/IOException,java/lang/Nu | 运行正常 |  |
| vault/MsPacMan.jar | 248 | Ms. PAC-MAN | - | 1.4.5 | MIDP-2.0 | nokia-ui | 47 | 16 | 600 | 3845132 | 0 | java/io/EOFException,javax/bluet | 运行正常 |  |
| vault/MyLifeInNewYork_1.0.4_320x240.jar | 406 | My Life in New York | - | 1.0.4 | MIDP-2.0 | - | 6 | 16 | 600 | 5924025 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/MyMonsterPet.jar | 535 | MyMonsterPet-zetrO | - | 1.3.5 | MIDP-1.0 | - | 50 | 16 | 600 | 24761600 | 0 | - | 运行正常 |  |
| vault/N.O.V.A. 3.jar | 1536 | N.O.V.A. 3 | - | 1.1.9 | MIDP-2.0 | - | 9 | 16 | 600 | 13654416 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/N.O.V.A. Legacy.jar | 1226 | N.O.V.A. Legacy | - | 1.0.8 | MIDP-2.0 | - | 12 | 16 | 600 | 30280340 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/N.O.V.A..jar | 1195 | N.O.V.A. | - | 1.0.6 | MIDP-2.0 | - | 14 | 16 | 600 | 179286175 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/NBA ProBasketBall2010_1.0.1_320x240.jar | 552 | NBA Pro Basketball 2010 | - | 1.0.1 | MIDP-2.0 | - | 9 | 16 | 600 | 223667450 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/NBA Smash!.jar | 617 | NBA Smash | - | 1.0.9 | MIDP-2.0 | - | 9 | 16 | 600 | 137407896 | 0 | java/lang/ArrayIndexOutOfBoundsE | 运行正常 |  |
| vault/NFL2011_1.0.1_320x240 C3.jar | 819 | NFL 2011 | - | 1.0.1 | MIDP-2.1 | - | 22 | 16 | 600 | 685083014 | 0 | java/lang/ClassNotFoundException | 运行正常 |  |
| vault/NOVA Near Orbit Vanguard Alliance_320x240.jar | 866 | N.O.V.A. | - | 1.0.6 | MIDP-2.0 | - | 14 | 16 | 600 | 82377919 | 0 | - | 运行正常 |  |
| vault/Namco Arcade Golf_320x240.jar | 288 | NAMCO ARCADE GOLF | - | 1.3.6 | MIDP-1.0 | - | 5 | 16 | 600 | 9833589 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
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
| vault/NeonRunner.jar | 330 | Neon Runner | - | 1.0.1 | MIDP-2.0 | nokia-ui | 21 | 16 | 600 | 1850277834 | 0 | - | 运行正常 |  |
| vault/New York Nights.v1.00.jar | 190 | NYNights | - | 0.0.75 | MIDP-1.0 | nokia-ui | 7 | 16 | 600 | 5344018705 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/NewYorkNights2FriendsForLife_1.0.5_320x240.jar | 664 | New York Nights 2 | - | 1.0.5 | MIDP-2.0 | - | 8 | 16 | 600 | 9518637 | 0 | java/lang/ClassNotFoundException | 运行正常 |  |
| vault/NightAtTheMuseum2_1.0.2_320x240.jar | 324 | Night at the Museum. Battle  | - | 1.0.2 | MIDP-2.0 | - | 13 | 16 | 600 | 8684256 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/Nightmare Creatures.jar | 160 | NightmareCreatures | - | 0.3.2 | MIDP-1.0 | nokia-ui | 4 | 16 | 600 | 2276053633 | 0 | - | 运行正常 |  |
| vault/Ninja Prophecy.jar | 1021 | Ninja Prophecy | - | 1.1.5 | MIDP-2.0 | - | 54 | 16 | 600 | 160199619 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/Ninja Prophecy_1.0.3_320x240.jar | 723 | Ninja Prophecy yMoBi.ru | - | 1.0.3 | MIDP-2.0 | nokia-ui | 54 | 16 | 600 | 68794740 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/NinjaProphecy_1.0.3_320x240.jar | 723 | Ninja Prophecy | - | 1.0.3 | MIDP-2.0 | nokia-ui | 54 | 16 | 600 | 68763062 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/Nitro Street Racing 2.jar | 976 | NitroStreet Racing2 | - | 1.2.0 | MIDP-2.0 | - | 16 | 16 | 600 | 342833686 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/Nitro Street Racing 3D.jar | 1151 | NitroStreet Racing | - | 1.1.2 | MIDP-2.0 | - | 25 | 16 | 600 | 6934450652 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/Nitro Street Racing.jar | 608 | NitroStreet Racing | - | 1.1.0 | MIDP-2.0 | - | 9 | 16 | 600 | 603300673 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/NitroStreetRacing2_1.1.3_320x240.jar | 628 | Nitro Street Racing 2 | - | 1.1.3 | MIDP-2.0 | - | 18 | 16 | 600 | 629697593 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/Numba.jar | 203 | Numba | - | 0.0.3 | MIDP-2.0 | - | 15 | 16 | 600 | 105018300 | 0 | java/io/IOException,javax/microe | 运行正常 |  |
| vault/Off-Road Dirt Motocross.jar | 579 | Xtreme Dirt Bike | - | 1.0.8 | MIDP-2.0 | - | 11 | 16 | 600 | 473424685 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/OffRoadDirtMotocross_1.0.0_320x240.jar | 358 | Off-Road Dirt Motocross | - | 1.0.0 | MIDP-2.0 | - | 12 | 16 | 600 | 3760596446 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/Onimusha Puzzle Dark Castle_320x240.jar | 161 | Onimusha Puzzle Dark Castle | - | 1.0.0 | MIDP-2.0 | - | 4 | 16 | 600 | 2349372 | 0 | java/lang/ArrayIndexOutOfBoundsE | 运行正常 |  |
| vault/OregonTrailAmericanSettlers_1.1.4_320x240.jar | 917 | Oregon Trail American Settle | - | 1.1.4 | MIDP-2.0 | nokia-ui | 23 | 16 | 600 | 29470637 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/OregonTrailGoldRush_1.4.3_320x240.jar | 654 | The Oregon Trail Gold Rush | - | 1.4.3 | MIDP-2.0 | - | 20 | 16 | 600 | 55957004 | 0 | - | 运行正常 |  |
| vault/Orlen Team - Dakar 2007.jar | 159 | Orlen Team - Dakar 2007 | - | 1.0.0 | MIDP-2.0 | - | 10 | 16 | 600 | 6934379 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/PAC-MAN (176x220) (1.0.0).jar | 86 | PAC-MAN | - | 1.0.0 | MIDP-1.0 | - | 8 | 16 | 600 | 40035 | 0 | java/lang/StringIndexOutOfBounds | 运行正常 |  |
| vault/PAC-MAN Ms. PAC-MAN Demo (176x220) (2.1.0).jar | 68 | PAC-MAN and Ms. PAC-MAN DEMO | - | 2.1.0 | MIDP-2.0 | - | 18 | 16 | 547 | 901992 | 0 | - | 运行正常 | exited itself (notifyDestroyed) |
| vault/PAC-MAN Ms. PAC-MAN Demo (240x320) (2.1.1).jar | 67 | PAC-MAN / Ms. PAC-MAN DEMO b | - | 2.1.1 | MIDP-2.0 | - | 18 | 16 | 600 | 1173911 | 0 | - | 运行正常 |  |
| vault/PAC-MAN Ms. PAC-MAN Demo (240x320) (2.6.0).jar | 181 | PAC-MAN \ Ms. PAC-MAN DEMO b | - | 2.6.0 | MIDP-2.0 | - | 29 | 16 | 600 | 233790930 | 0 | - | 运行正常 |  |
| vault/PAC-MAN Ms.PAC-MAN Demo (128x160) (2.6.0).jar | 111 | PAC-MAN \ Ms. PAC-MAN DEMO | - | 2.6.0 | MIDP-2.0 | - | 28 | 16 | 600 | 165426670 | 0 | - | 运行正常 |  |
| vault/PAC-MANIA 3D by Namco (176x220) (1.0.0).jar | 184 | PAC-MANIA (3D) by Namco | - | 1.0.0 | MIDP-2.0 | - | 19 | 16 | 600 | 781 | 0 | java/lang/StringIndexOutOfBounds | 运行正常 | runs without input; the generic key script m |
| vault/Pac Man Kart Rally 2D-3D_320x240.jar | 500 | PAC-MAN Kart Rally | - | 1.0.1 | MIDP-2.0 | - | 25 | 16 | 600 | 6593675 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/Pac-Man - Championship Edition.jar | 697 | PAC-MAN CHAMPIONSHIP EDITION | - | 1.1.0 | MIDP-2.0 | - | 22 | 16 | 600 | 27840332 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/Pac-Man Championship Edition_320x240.jar | 441 | PAC-MAN CHAMPIONSHIP EDITION | - | 1.1.0 | MIDP-2.0 | - | 22 | 16 | 600 | 22769808 | 0 | javax/microedition/rms/InvalidRe | 运行正常 |  |
| vault/Pac-Man Party.jar | 836 | PAC-MAN Party by Namco | - | 1.0.0 | MIDP-2.0 | - | 5 | 16 | 600 | 28549657 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/Pac-Man Pinball 2_320x240.jar | 421 | PAC-MAN Pinball 2 | - | 1.0.3 | MIDP-2.0 | nokia-ui | 24 | 16 | 600 | 8637731 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/Pac-Man.jar | 209 | PAC-MAN | - | 1.8.6 | MIDP-2.0 | - | 44 | 16 | 600 | 17688906 | 0 | javax/bluetooth/BluetoothStateEx | 运行正常 |  |
| vault/Pac-Mania.jar | 169 | PAC-MANIA | - | 1.1.8 | MIDP-2.0 | - | 4 | 16 | 600 | 12124954044 | 0 | - | 运行正常 |  |
| vault/PacManChampionship.jar | 558 | PAC-MAN CHAMPIONSHIP EDITION | - | 1.1.0 | MIDP-2.0 | - | 23 | 16 | 600 | 30917940 | 0 | javax/microedition/rms/InvalidRe | 运行正常 |  |
| vault/Paperboy - Wheels on Fire.jar | 320 | Paperboy Wheels On Fire | - | 01.00.01 | MIDP-2.0 | - | 11 | 16 | 600 | 2897066 | 0 | - | 运行正常 |  |
| vault/ParcoGiochi.jar | 283 | Parco Giochi | - | 1.0.48 | MIDP-2.0 | nokia-ui | 14 | 16 | 600 | 18051893 | 0 | java/io/EOFException,java/lang/N | 运行正常 |  |
| vault/ParisHiltonsDiamondQuest_1.1.0_320x240.jar | 323 | Paris Hilton's Diamond Quest | - | 1.1.0 | MIDP-2.0 | - | 8 | 16 | 600 | 6458246193 | 0 | javax/microedition/rms/InvalidRe | 运行正常 |  |
| vault/ParisNights_1.0.0_320x240.jar | 320 | Paris Nights | - | 1.0.0 | MIDP-2.0 | - | 10 | 16 | 600 | 1942186 | 0 | java/lang/ArrayIndexOutOfBoundsE | 运行正常 |  |
| vault/Petz_1.0.2_320x240.jar | 643 | Petz | - | 1.0.2 | MIDP-2.0 | - | 11 | 16 | 600 | 22016673 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/Phantom Duck.jar | 465 | Phantom Duck | - | 1.0.2 | MIDP-1.0 | - | 11 | 16 | 600 | 1964582 | 0 | java/io/EOFException | 运行正常 |  |
| vault/Pictionary_320x240.jar | 208 | Pictionary | - | 1.13.24 | MIDP-2.0 | - | 13 | 16 | 600 | 681011472 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/Pirates of the Caribbean - At World's End.jar | 283 | Pirates 3 | - | 1.0.2 | MIDP-1.0 | - | 19 | 16 | 600 | 2748242 | 0 | java/io/EOFException,java/lang/N | 运行正常 |  |
| vault/Pirates of the Seven Seas.jar | 581 | Pirates of the Seven Seas | - | 1.1.6 | MIDP-2.0 | - | 7 | 16 | 600 | 1517883 | 0 | - | 运行正常 |  |
| vault/PiratesOfTheCaribbean.jar | 265 | Pirates 3 | - | 1.0.4 | MIDP-1.0 | nokia-ui | 19 | 16 | 600 | 5433362 | 0 | java/io/EOFException,java/lang/N | 运行正常 |  |
| vault/Pixeline ZOO.jar | 53 | PixZoo | - | 1.0.1 | MIDP-1.0 | nokia-ui | 10 | 16 | 600 | 375311 | 0 | java/io/IOException | 运行正常 |  |
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
| vault/Platinum Solitaire 2.jar | 763 | Platinum Solitaire 2 | - | 1.1.7 | MIDP-2.0 | - | 8 | 16 | 600 | 13509400 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/Platinum Solitaire 3.jar | 1243 | Platinum Solitaire 3 | - | 1.1.5 | MIDP-2.0 | - | 6 | 16 | 600 | 66574060 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/Platinum Solitaire.jar | 595 | Platinum Solitaire | - | 1.0.12 | MIDP-2.0 | - | 6 | 16 | 600 | 305385259 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/Platinum Sudoku 2.jar | 964 | Platinum Sudoku 2 | - | 1.0.8 | MIDP-2.0 | - | 14 | 16 | 600 | 72399404 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/Platinum Sudoku 2_320x240.jar | 389 | Platinum Sudoku 2 | - | 1.2.0 | MIDP-2.0 | - | 13 | 16 | 600 | 6527292 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/Platinum Sudoku.jar | 262 | Platinum Sudoku | - | 1.2.2 | MIDP-2.0 | - | 8 | 16 | 600 | 291108007 | 0 | - | 运行正常 |  |
| vault/PlatinumMahjong.jar | 288 | Platinum Mahjong | - | 1.0.9 | MIDP-2.0 | - | 4 | 16 | 600 | 3730087310 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/PlatinumSolitaire2_1.0.4_320x240.jar | 529 | Platinum Solitaire 2 | - | 1.0.4 | MIDP-2.0 | - | 8 | 16 | 600 | 3572359023 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/PlatinumSolitaire3_1.0.7_320x240.jar | 1094 | Platinum Solitaire 3 | - | 1.0.7 | MIDP-2.0 | - | 6 | 16 | 600 | 4511365320 | 0 | - | 运行正常 |  |
| vault/PlatinumSudoku.jar | 269 | Platinum Sudoku | - | 1.0.1 | MIDP-2.0 | - | 9 | 16 | 600 | 5411647998 | 0 | - | 运行正常 |  |
| vault/PlatinumSudoku1_1.0.1_320x240.jar | 334 | Platinum Sudoku | - | 1.0.1 | MIDP-2.0 | - | 12 | 16 | 600 | 4814665512 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/PlatinumSudoku2.jar | 629 | Platinum Sudoku 2 | - | 1.1.3 | MIDP-2.0 | - | 14 | 16 | 600 | 84451425 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/Playman Summer Games 3_320x240.jar | 414 | Playman Summer Games 3 | - | 1.0.21 | MIDP-2.0 | - | 6 | 16 | 600 | 6230591542 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/Pocket Chef.jar | 630 | Pocket Chef | - | 1.0.0 | MIDP-2.0 | - | 12 | 16 | 600 | 84522518 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/PocketChef_1.0.3_320x240.jar | 565 | Pocket Chef | - | 1.0.3 | MIDP-2.0 | - | 11 | 16 | 600 | 121729895 | 0 | - | 运行正常 |  |
| vault/Postal Babes.jar | 337 | Postal Babes | - | 1.4.0 | MIDP-2.0 | - | 20 | 16 | 600 | 8236184 | 0 | java/io/EOFException,java/io/IOE | 运行正常 |  |
| vault/Predators.jar | 959 | Predators | - | 1.0.8 | MIDP-2.0 | - | 17 | 16 | 600 | 110755314 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/Predators_1.3.0_320x240.jar | 663 | Predators | - | 1.3.0 | MIDP-2.0 | - | 19 | 16 | 600 | 187466395 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/Prehistoric Fun Park_320x240_Nokia_E71.jar | 213 | Prehistoric Fun Park | - | 0.1.20 | MIDP-2.0 | - | 7 | 16 | 600 | 43808 | 0 | - | 运行正常 |  |
| vault/Prince of Persia (2008).jar | 620 | Prince of Persia | - | 1.0.5 | MIDP-2.0 | - | 6 | 16 | 600 | 635351818 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/Prince of Persia - The Forgotten Sands.jar | 995 | Prince of Persia The Forgott | - | 1.0.7 | MIDP-2.0 | - | 11 | 16 | 600 | 31180620 | 0 | java/lang/ArrayIndexOutOfBoundsE | 运行正常 |  |
| vault/Prince of Persia - The Two Thrones.jar | 306 | Prince Of Persia 3 | - | 1.0.1 | MIDP-2.0 | - | 7 | 16 | 600 | 343346649 | 0 | - | 运行正常 |  |
| vault/Prince of Persia - Warrior Within.jar | 221 | Prince Of Persia - Warrior W | - | 1.0.0 | MIDP-2.0 | - | 7 | 16 | 600 | 429412142 | 0 | - | 运行正常 |  |
| vault/Prince of Persia Classic.jar | 617 | Prince of Persia Classic | - | 1.0.6 | MIDP-2.0 | - | 7 | 16 | 600 | 226197433 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/PrinceOfPersia.jar | 196 | PrinceOfPersiaTheSandsOfTime | - | 1.0.1 | MIDP-2.0 | - | 6 | 16 | 600 | 16523961 | 0 | - | 运行正常 |  |
| vault/PrinceOfPersia2.jar | 211 | PrinceOfPersia-WarriorWithin | - | 1.0.1 | MIDP-1.0 | nokia-ui | 5 | 16 | 600 | 2463283401 | 0 | - | 运行正常 |  |
| vault/PrinceOfPersia3.jar | 275 | Prince Of Persia 3 | - | 1.0 | MIDP-2.0 | - | 7 | 16 | 600 | 3042850484 | 0 | - | 运行正常 |  |
| vault/PrinceOfPersia3TheTwoThrones_1.0.3_320x240.jar | 326 | Prince Of Persia 3 | - | 1.0.3 | MIDP-2.0 | - | 8 | 16 | 891 | 1635259427 | 0 | - | 运行正常 |  |
| vault/PrinceOfPersia4.jar | 600 | Prince of Persia | - | 1.0.4 | MIDP-2.0 | - | 6 | 16 | 600 | 1111485596 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/PrinceOfPersiaClassic_1.0.1_320x240.jar | 614 | Prince of Persia Classic by  | - | 1.0.1 | MIDP-2.0 | - | 7 | 16 | 600 | 6489284861 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/PrinceOfPersiaThe ForgottenSands_1.0.4_320x240.jar | 737 | Prince of Persia The Forgott | - | 1.0.4 | MIDP-2.0 | - | 11 | 16 | 600 | 42754424 | 0 | javax/microedition/media/MediaEx | 运行正常 |  |
| vault/PrinceOfPersia_1.0.0_320x240.jar | 591 | Prince of Persia | - | 1.0.0 | MIDP-2.0 | - | 6 | 16 | 600 | 102617738 | 0 | - | 运行正常 |  |
| vault/Pro Evolution Soccer 2008.jar | 346 | PES 2008 | - | 1.0.1 | MIDP-2.0 | - | 8 | 16 | 601 | 519765820 | 0 | java/lang/IndexOutOfBoundsExcept | 运行正常 |  |
| vault/Pro Evolution Soccer 2010.jar | 458 | PES 2010 | - | 1.0.0 | MIDP-2.0 | - | 9 | 16 | 600 | 6218142070 | 0 | java/lang/IndexOutOfBoundsExcept | 运行正常 |  |
| vault/Pro Evolution Soccer 2011.jar | 558 | PES 2011 | - | 1.0.0 | MIDP-2.0 | - | 9 | 16 | 600 | 5127301551 | 0 | java/lang/IndexOutOfBoundsExcept | 运行正常 |  |
| vault/ProEvolutionSoccer2014_1.0.8_320x240.jar | 1045 | PES 2014 | - | 1.0.8 | MIDP-2.0 | - | 18 | 16 | 600 | 440072 | 0 | java/io/IOException,java/lang/Il | 运行正常 |  |
| vault/ProGolf2010_1.1.0_320x240.jar | 652 | Pro Golf 2010 | - | 1.1.0 | MIDP-2.0 | - | 6 | 16 | 600 | 2486851284 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/Project Gotham Racing.jar | 941 | PGR | - | 1.0.2 | MIDP-1.0 | - | 45 | 16 | 600 | 2520430 | 0 | - | 运行正常 |  |
| vault/Pub Mania.jar | 566 | PubMania | - | 1.0.1 | MIDP-2.0 | - | 5 | 16 | 600 | 275058540 | 0 | javax/microedition/rms/InvalidRe | 运行正常 |  |
| vault/Puzzle Bobble Evolution.jar | 531 | Puzzle Bobble Evolution | - | 0.8.0 | MIDP-1.0 | - | 17 | 16 | 600 | 4466891 | 0 | java/lang/IllegalArgumentExcepti | 运行正常 |  |
| vault/Puzzle Bobble.jar | 145 | Puzzle Bobble | - | 1.4.8 | MIDP-2.0 | - | 12 | 16 | 600 | 241572 | 0 | java/io/IOException | 运行正常 |  |
| vault/Puzzle Challenge Deluxe_320x240_Nokia_E71.jar | 259 | Puzzle Challenge Deluxe | - | 1.0.0 | MIDP-1.0 | - | 12 | 16 | 600 | 11754296 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/Puzzle Quest - Warlords.jar | 441 | Puzzle Quest - Warlords | - | 1.0.63 | MIDP-1.0 | - | 15 | 16 | 600 | 574263 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/Puzzle Tracks__320x240.jar | 348 | Puzzle Tracks | - | 1.2.0 | MIDP-2.0 | - | 12 | 16 | 600 | 2992163 | 0 | java/io/IOException | 运行正常 |  |
| vault/PuzzleQuest.jar | 327 | Puzzle Quest - Warlords | - | 1.0.47 | MIDP-1.0 | - | 11 | 16 | 600 | 18851165 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/Puzzlebox_1.0.0_320x240.jar | 340 | Puzzle Box | - | 1.2.0 | MIDP-2.0 | - | 58 | 16 | 600 | 86386 | 0 | - | 运行正常 |  |
| vault/QBlox_320x240.jar | 382 | QBlox | - | 1.0.8 | MIDP-2.0 | nokia-ui | 14 | 16 | 600 | 101204775 | 0 | java/io/IOException,java/lang/Ar | 运行正常 |  |
| vault/R-Type.jar | 272 | R-Type | - | 0.0.5 | MIDP-2.0 | - | 7 | 16 | 600 | 606469 | 0 | java/io/IOException,java/lang/Nu | 运行正常 |  |
| vault/Race Driver - Grid.jar | 970 | Race Driver Grid | - | 1.0.2 | MIDP-2.0 | - | 66 | 16 | 600 | 12176266 | 0 | - | 运行正常 |  |
| vault/Race Electrix_320x240.jar | 488 | Race Electrix | - | 1.1.5 | MIDP-1.0 | - | 3 | 16 | 600 | 1922056 | 0 | - | 运行正常 |  |
| vault/Racing Fever GT.jar | 253 | Racing Fever GT 3D | - | 1.0.6 | MIDP-2.0 | - | 28 | 16 | 600 | 12609122 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/Rainbow Invaders_320x240.jar | 112 | Rainbow Invaders | - | 1.0 | MIDP-2.0 | - | 13 | 16 | 600 | 26099214 | 0 | java/io/IOException,java/lang/Nu | 运行正常 |  |
| vault/Rally Master Pro.jar | 1155 | Rally Master Pro | - | 1.0 | MIDP-2.0 | - | 179 | 16 | 600 | 10834336 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/Rally Stars.jar | 512 | RallyStars-3D | - | 1.1.0 | MIDP-2.0 | - | 45 | 16 | 600 | 14994339 | 0 | java/io/IOException,java/lang/Nu | 运行正常 |  |
| vault/Rayman 3.jar | 174 | Rayman3 | - | 1.0.3 | MIDP-2.0 | - | 5 | 16 | 600 | 178945842 | 0 | - | 运行正常 |  |
| vault/Rayman Raving Rabbids.jar | 315 | Rayman Raving Rabbids | - | 1.0.5 | MIDP-2.0 | - | 5 | 16 | 600 | 198383772 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/Real Football 2007.jar | 341 | Real Football 2007 | - | 1.0.1 | MIDP-2.0 | - | 9 | 16 | 600 | 79129495 | 0 | java/lang/NumberFormatException | 运行正常 |  |
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
| vault/RealFootball2007-3D_1.0.6_320x240.jar | 1003 | RealFootball2007 3D | - | 1.0.6 | MIDP-2.0 | m3g,nokia-ui | 7 | 16 | 600 | 5790428979 | 0 | java/io/IOException,java/lang/Nu | 运行正常 |  |
| vault/RealFootball2012_1.1.2_320x240.jar | 898 | Real Football 2012 | - | 1.1.2 | MIDP-2.0 | nokia-ui | 14 | 16 | 600 | 584402638 | 0 | - | 运行正常 | runs without input; the generic key script d |
| vault/RealFootballManagerEdition_1.0.0_320x240.jar | 599 | Real Football Manager Editio | - | 1.0.0 | MIDP-2.0 | - | 10 | 16 | 600 | 3411852919 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/RealRuby_1.0.5_320x240.jar | 484 | Real Rugby | - | 1.0.5 | MIDP-2.0 | - | 7 | 16 | 600 | 7498826305 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/Real_2_Football_2007.jar | 335 | RealFootball2007 | - | 1.0.7 | MIDP-2.0 | - | 7 | 16 | 600 | 3192858014 | 0 | - | 运行正常 |  |
| vault/Red Bull Motocross.jar | 834 | RedBullMotoCross2D | - | 0.3.9 | MIDP-2.0 | - | 52 | 16 | 600 | 6251660 | 0 | java/io/EOFException,javax/micro | 运行正常 |  |
| vault/Red Out Racer.jar | 244 | Red Out Racer | - | 1.0.2 | MIDP-2.0 | - | 23 | 16 | 600 | 1890019 | 0 | javax/microedition/media/MediaEx | 运行正常 |  |
| vault/Resident Evil - The Missions.jar | 974 | Resident Evil - The Missions | - | 1.0.0 | MIDP-2.0 | - | 5 | 16 | 601 | 1497848 | 0 | - | 运行正常 |  |
| vault/ResidentEvil-Degeneration_s40v3a.jar | 371 | Resident Evil Degeneration | - | 1.0.0 | MIDP-2.0 | - | 10 | 16 | 601 | 2905480 | 0 | java/lang/NumberFormatException | 运行正常 |  |
| vault/Ridge Racer Drift.jar | 1268 | RidgeRacerD | - | 1.0.1 | MIDP-2.0 | - | 28 | 16 | 600 | 190565764 | 0 | javax/microedition/rms/InvalidRe | 运行正常 |  |
| vault/Ridge Racer.jar | 398 | Ridge Racer | - | 1.0.7 | MIDP-2.0 | - | 35 | 16 | 600 | 82292567 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/RiseOfLostEmpires_1.0.0_320x240.jar | 320 | Rise of Lost Empires | - | 1.0.0 | MIDP-2.0 | - | 10 | 16 | 600 | 3965575547 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/Risk_320x240.jar | 956 | RISK | - | 1.11.2 | MIDP-2.0 | nokia-ui | 12 | 16 | 600 | 11532114 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/Robin Hood_320x240_Nokia_E71.jar | 998 | Robin Hood | - | 4.8.0 | MIDP-2.0 | - | 15 | 16 | 600 | 14111384 | 0 | java/io/EOFException | 运行正常 |  |
| vault/Robobombo.jar | 338 | Robobombo | - | 1.0.7 | MIDP-2.0 | - | 61 | 16 | 600 | 2259389 | 0 | java/lang/ClassNotFoundException | 运行正常 |  |
| vault/Robot Battle Tactics.jar | 321 | Robot Battle Tactics | - | 1.0.0 | MIDP-2.0 | - | 14 | 16 | 600 | 4454379 | 0 | - | 运行正常 |  |
| vault/Robot Maker_320x240.jar | 357 | Robot Maker TEGOS.ru | - | 1.0.0 | MIDP-2.0 | - | 10 | 16 | 600 | 3913050 | 0 | - | 运行正常 |  |
| vault/RolandGarros2009_1.0.8_320x240.jar | 604 | Roland-Garros 2009 | - | 1.0.8 | MIDP-2.0 | - | 11 | 16 | 600 | 87533247 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/Roll that Ball 2.jar | 512 | Roll that Ball 2 | - | 1.0.1 | MIDP-2.0 | - | 18 | 16 | 600 | 369622431 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/Roll that Ball.jar | 568 | Roll that Ball | - | 1.0.0 | MIDP-2.0 | - | 18 | 16 | 600 | 330747742 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/Rollercoaster Revolution - 99 Tracks.jar | 845 | Abahn Rev 99 | - | 1.0.10 | MIDP-2.0 | - | 39 | 16 | 600 | 3381751 | 0 | java/io/EOFException,javax/micro | 运行正常 |  |
| vault/Rollercoaster Revolution 99 Tracks_320x240.jar | 666 | Roller Rev 99 | - | 1.0.20 | MIDP-2.0 | nokia-ui | 39 | 16 | 600 | 63262544 | 0 | java/io/EOFException,java/lang/C | 运行正常 |  |
| vault/Rome - Throne of Destiny.jar | 232 | Rome Throne Of Destiny | - | 1.0.0 | MIDP-2.0 | - | 12 | 16 | 600 | 1170921 | 0 | java/io/IOException,javax/microe | 运行正常 |  |
| vault/SEGA Rally 3D.jar | 958 | SEGA Rally 3D | - | 1.0.5 | MIDP-2.0 | - | 44 | 16 | 600 | 217522597 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/SU-30.jar | 434 | Su-30 | - | 1.8.0 | MIDP-2.0 | - | 55 | 16 | 600 | 4917863 | 0 | java/io/EOFException,java/io/IOE | 运行正常 |  |
| vault/SallysStudio_1.0.4_320x240.jar | 1004 | Sallys Studio | - | 1.0.4 | MIDP-2.0 | - | 14 | 16 | 600 | 67287548 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/Scrabble Mobile_1.1.1_320x240-en.jar | 825 | Scrabble | - | 1.1.1 | MIDP-2.0 | - | 4 | 16 | 600 | 7041074271 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/ShadoWalker.jar | 518 | Shadowalker | - | 1.0.3 | MIDP-1.0 | - | 12 | 16 | 600 | 722344 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/Shadowalker.jar | 591 | Shadowalker | - | 1.0.6 | MIDP-1.0 | - | 14 | 16 | 600 | 20832210 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/SherlockHolmes_1.0.9_320x240.jar | 640 | Sherlock Holmes: The Officia | - | 1.0.9 | MIDP-2.0 | - | 10 | 16 | 600 | 23136129 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/ShrekForeverAfter_1.0.3_320x240.jar | 437 | Shrek Forever After | - | 1.0.2 | MIDP-2.0 | - | 15 | 16 | 600 | 2560805 | 0 | java/lang/ClassNotFoundException | 运行正常 |  |
| vault/ShrekParty_1.0.1_320x240.jar | 678 | Shrek Party | - | 1.0.1 | MIDP-2.0 | - | 7 | 16 | 600 | 29512482 | 0 | java/lang/ArrayIndexOutOfBoundsE | 运行正常 |  |
| vault/ShrekTheThird_1.1.2_320x240.jar | 579 | Shrek the Third: The Officia | - | 1.1.2 | MIDP-2.0 | - | 7 | 16 | 600 | 3174275193 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/Siberian Strike.jar | 206 | Siberian Strike | - | 1.0.0 | MIDP-2.0 | - | 6 | 16 | 600 | 1216158 | 0 | - | 运行正常 |  |
| vault/Sid Meiers Civilization 4 Defender Of The Gates_320x240_Nokia_E71.jar | 363 | Civilization IV Defenders Of | - | 1.2.6 | MIDP-2.0 | - | 12 | 16 | 600 | 6014852 | 0 | java/io/IOException | 运行正常 |  |
| vault/SidMeiersCivilization5_1.0.6_320x240.jar | 635 | Sid Meier's Civilization V M | - | 1.0.6 | MIDP-2.0 | - | 11 | 16 | 600 | 11673159 | 0 | - | 运行正常 |  |
| vault/Silent Hill Mobile 2.jar | 475 | Silent Hill Mobile 2 | - | 1.0.0 | MIDP-2.0 | - | 12 | 16 | 600 | 8286718 | 0 | java/io/IOException,java/lang/Nu | 运行正常 |  |
| vault/Silent Hill Mobile 3.jar | 579 | Silent Hill 3 | - | 1.0.20 | MIDP-2.0 | - | 13 | 16 | 600 | 2680857 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/Silent Hill Mobile.jar | 476 | Silent Hill | - | 0.1.0 | MIDP-2.0 | - | 10 | 16 | 600 | 3291811 | 0 | java/io/IOException,javax/microe | 运行正常 |  |
| vault/Silent Hunter_320x240_Nokia_E71.jar | 730 | Silent Hunter | - | 1.0 | MIDP-2.0 | - | 45 | 16 | 600 | 3635065 | 0 | java/io/IOException,javax/microe | 运行正常 |  |
| vault/SimCity Deluxe.jar | 914 | SimCity Deluxe | - | 1.12.37 | MIDP-2.0 | - | 180 | 16 | 600 | 8488099 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/SimCity Metropolis_320x240.jar | 779 | SimCityM | - | 1.23.87 | MIDP-2.0 | - | 25 | 16 | 600 | 152451180 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/Snake III.jar | 293 | Snake III | - | 3.07.2 | MIDP-2.0 | - | 49 | 16 | 600 | 131592 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/Snake Reloaded_320x240_Nokia_E71.jar | 619 | Snake Reloaded | - | 1.0.17 | MIDP-2.0 | nokia-ui | 47 | 16 | 600 | 69829247 | 0 | java/io/EOFException,javax/micro | 运行正常 |  |
| vault/Snowboard Hero.jar | 751 | SnowboardHero | - | 1.0.1 | MIDP-2.0 | - | 135 | 16 | 600 | 8889770 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/Sokoban.jar | 35 | Sokoban | - | 1.1 | MIDP-1.0 | - | 14 | 16 | 600 | 9578389 | 0 | java/io/EOFException | 运行正常 |  |
| vault/Solitaire 8 in 1 2011_320x240.jar | 366 | Solitaire8x1 | - | 2.0 | MIDP-2.0 | nokia-ui | 8 | 16 | 600 | 8557032 | 0 | - | 运行正常 |  |
| vault/Solitaire Deluxe 16 Pack_320x240.jar | 427 | Solitaire Dlx | - | 1.5.24 | MIDP-2.0 | - | 51 | 16 | 600 | 26895270 | 0 | java/io/IOException,javax/microe | 运行正常 |  |
| vault/Solitaire Pop_320x240.jar | 526 | Solitaire Pop | - | 1.0.0 | MIDP-2.0 | - | 7 | 16 | 600 | 10464058 | 0 | java/lang/ArrayIndexOutOfBoundsE | 运行正常 |  |
| vault/Sonic & SEGA All-Star Racing.jar | 1015 | Sonic Racing | - | 1.2.2 | MIDP-2.0 | - | 24 | 16 | 600 | 8347579 | 0 | - | 运行正常 |  |
| vault/Sonic Advance.jar | 1073 | Sonic Advance | - | 1.3.0 | MIDP-2.0 | - | 174 | 16 | 600 | 2391943 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/Sonic Jump.jar | 328 | Sonic Jump | - | 1.0.3 | MIDP-1.0 | - | 7 | 16 | 602 | 4337055 | 0 | java/io/IOException,javax/microe | 运行正常 |  |
| vault/Sonic Runners Adventures.jar | 966 | Sonic Runners Adventure | - | 1.0.8 | MIDP-2.0 | - | 14 | 16 | 600 | 27246883 | 0 | - | 运行正常 |  |
| vault/Sonic Spinball.jar | 637 | Sonic Spinball by konon | - | 4.0.20 | MIDP-2.0 | - | 14 | 16 | 600 | 19404427 | 0 | java/lang/ArrayIndexOutOfBoundsE | 运行正常 |  |
| vault/Sonic Spinball_320x240.jar | 560 | Sonic Spinball | - | 4.0.75 | MIDP-2.0 | - | 16 | 16 | 600 | 22392282 | 0 | java/lang/IllegalArgumentExcepti | 运行正常 |  |
| vault/Sonic Unleashed.jar | 848 | SonicUnleashed | - | 1.2.5 | MIDP-2.0 | - | 15 | 16 | 600 | 183726898 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/SonicAdvance_1.4.0_320x240.jar | 1057 | Sonic Advance | - | 1.4.0 | MIDP-2.0 | - | 174 | 16 | 600 | 2848464 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/SonicSegaAllStars_1.0.2_320x240.jar | 659 | Sonic Racing | - | 1.0.2 | MIDP-2.0 | - | 26 | 16 | 600 | 43137794 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/SonicTheHedgehogPartOne_6280.jar | 278 | Sonic The Hedgehog Part One | - | 1.0.46 | MIDP-2.0 | - | 5 | 16 | 600 | 4200206 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/SonicTheHedgehogPartTwo_6280.jar | 339 | Sonic The Hedgehog Part Two | - | 1.0.2 | MIDP-2.0 | nokia-ui | 5 | 16 | 600 | 1497453 | 0 | java/io/IOException,java/lang/Nu | 运行正常 |  |
| vault/SonicUnleashed_1.2.2_320x240.jar | 642 | SonicUnleashed | - | 1.2.2 | MIDP-2.0 | - | 11 | 16 | 600 | 265199525 | 0 | - | 运行正常 |  |
| vault/Soul Calibur Mobile.jar | 1334 | SOULCALIBUR Mobile by Namco | - | 1.0.0 | MIDP-2.0 | - | 14 | 16 | 600 | 6769462 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/Soul of Darkness.jar | 631 | Soul Of Darkness | - | 1.3.2 | MIDP-2.0 | - | 8 | 16 | 600 | 363502553 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/SoulOfDarkness_HTC_P3450_EN_IGPL_EU_101_S8000.jar | 617 | Soul Of Darkness | - | 1.0.1 | MIDP-2.0 | - | 6 | 16 | 0 | 0 | 0 | - | 超时 |  |
| vault/SpaceInvaderEvolution_0.5.6_320x240.jar | 350 | Space Invader Evolution | - | 0.5.6 | MIDP-2.0 | - | 42 | 16 | 600 | 4448991 | 0 | - | 运行正常 |  |
| vault/Speed Racer.jar | 886 | Speed Racer 3D | - | 1.2.9 | MIDP-2.0 | - | 26 | 16 | 600 | 842903 | 0 | - | 运行正常 |  |
| vault/Spider-Man - Toxic City.jar | 995 | Spider-Man: Toxic City | - | 1.1.6 | MIDP-2.0 | - | 15 | 16 | 600 | 95483971 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/Spider-Man - Ultimate Power.jar | 1542 | Spider-Man: Ultimate Power | - | 1.2.3 | MIDP-2.0 | - | 18 | 16 | 600 | 69901982 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/Splatterhouse.jar | 549 | Splatterhouse by Namco | - | 1.0.0 | MIDP-2.0 | - | 14 | 16 | 600 | 13180845 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/SplinterCellConviction_1.1.5_320x240_bresilien.jar | 638 | Splinter Cell Conviction | - | 1.1.5 | MIDP-2.0 | - | 11 | 16 | 600 | 11733541 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/SplinterCellDoubleAgent_1.0.1_320x240.jar | 317 | Splinter Cell DA | - | 1.0.1 | MIDP-2.0 | nokia-ui | 6 | 16 | 601 | 67484413 | 0 | java/lang/ArrayIndexOutOfBoundsE | 运行正常 |  |
| vault/Spongebob - Bikini Bottom Pursuit.jar | 390 | Bob Esponja | - | 1.1.0 | MIDP-2.0 | - | 21 | 16 | 600 | 496533 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/Spooks_1.0.0_320x240.jar | 411 | Spooks The Mobile Game | - | 1.0.0 | MIDP-2.0 | - | 7 | 16 | 600 | 18380676 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/Spore Creatures.jar | 758 | SPORE CREATURES | - | 4.0.52 | MIDP-2.0 | - | 42 | 16 | 600 | 63668253 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/SporeCreatures_4.9.20_320x240.jar | 663 | SPORE CREATURES | - | 4.9.20 | MIDP-2.0 | - | 42 | 16 | 600 | 5352215 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/SporeOrigins_4.9.0_320x240.jar | 805 | SPORE | - | 4.9.0 | MIDP-2.0 | - | 28 | 16 | 600 | 3280227135 | 0 | java/lang/IllegalArgumentExcepti | 运行正常 |  |
| vault/Spyro Eternal Night_320x240.jar | 370 | SpyroEternal | - | 0.5.9 | MIDP-2.0 | nokia-ui | 6 | 16 | 600 | 7527371 | 0 | java/io/EOFException,javax/micro | 运行正常 |  |
| vault/Star Invasion_320x240.jar | 1003 | Star Invasion | - | 1.0.21 | MIDP-2.0 | nokia-ui | 17 | 16 | 600 | 53748847 | 0 | java/io/EOFException,javax/micro | 运行正常 |  |
| vault/Star Wars The Clone Wars_320x240.jar | 416 | Star Wars - The Clone Wars | - | 1.0.35 | MIDP-1.0 | nokia-ui | 7 | 16 | 600 | 9580990 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/Street Fighter Alpha - Warriors' Dreams.jar | 1041 | Street Fighter Alpha | - | 1.1.5 | MIDP-2.0 | - | 19 | 16 | 600 | 59446444 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/Sudoku DChoc Cafe__320x240.jar | 290 | Sudoku | - | 1.1.25 | MIDP-2.0 | nokia-ui | 18 | 16 | 600 | 13663722 | 0 | java/io/EOFException,java/lang/N | 运行正常 |  |
| vault/Super Bomberman.jar | 150 | Super Bomberman | - | 1.1.1 | MIDP-1.0 | - | 4 | 16 | 600 | 4918907 | 0 | java/lang/ArrayIndexOutOfBoundsE | 运行正常 |  |
| vault/Super Breakout.jar | 222 | Super Breakout | - | 1.0.4 | MIDP-2.0 | - | 28 | 16 | 600 | 31839645 | 0 | java/lang/ClassCastException,jav | 运行正常 |  |
| vault/Super Monkey Ball - Tip'n Tilt 2.jar | 300 | SMB TNT2 | - | 1.2.8 | MIDP-2.0 | - | 28 | 16 | 600 | 532390 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/Super Monkey Ball - Tip'n Tilt.jar | 362 | Super Monkey Ball Tip n Tilt | - | 1.0.1 | MIDP-2.0 | - | 7 | 16 | 600 | 7179413 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/Super Puzzle Bobble.jar | 142 | Super Puzzle Bobble | - | 1.4.8 | MIDP-2.0 | - | 12 | 16 | 600 | 231472 | 0 | java/io/IOException | 运行正常 |  |
| vault/Super Water Bomber.jar | 318 | Super Water Bomber | - | 1.0.3 | MIDP-2.0 | - | 18 | 16 | 600 | 12200233 | 0 | java/io/EOFException,javax/micro | 运行正常 |  |
| vault/SuperMarioTheLostLevels_1.0_320x240.jar | 133 | 超级玛利兄弟2 | - | 1.0 | MIDP-2.0 | - | 17 | 16 | 600 | 3473056 | 0 | java/io/IOException | 运行正常 |  |
| vault/Superman & Batman - Heroes United.jar | 457 | Superman Batman Heroes Unite | - | 1.0.5 | MIDP-2.0 | - | 6 | 16 | 600 | 2899754 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/SurfIsUp_1.2.3_320x240.jar | 310 | Surf's Up | - | 1.3.3 | MIDP-2.0 | - | 6 | 16 | 600 | 4198549303 | 0 | - | 运行正常 |  |
| vault/Swapper_320x240.jar | 266 | Swapper | - | 1.0.0 | MIDP-2.0 | - | 34 | 16 | 600 | 2752833969 | 0 | java/io/IOException,java/lang/Cl | 运行正常 |  |
| vault/TRANSFORMERS (176x208) 1.0.4 [Andrew-Lviv.net].jar | 566 | Transformers by street_racer | - | 1.0.4 | MIDP-2.0 | m3g | 8 | 16 | 600 | 31530032 | 0 | java/lang/IllegalArgumentExcepti | 运行正常 | runs without input; the generic key script t |
| vault/TRANSFORMERS (176x208) 1.0.4 [Dedomil.net].jar | 569 | Transformers by street_racer | - | 1.0.4 | MIDP-2.0 | m3g | 8 | 16 | 600 | 31529823 | 0 | java/lang/IllegalArgumentExcepti | 运行正常 | runs without input; the generic key script t |
| vault/TRANSFORMERS (240x320) 1.0.1 [Andrew-Lviv.net].jar | 520 | Transformers | - | 1.0.2 | MIDP-2.0 | m3g | 86 | 16 | 600 | 31518905 | 0 | java/lang/IndexOutOfBoundsExcept | 运行正常 | runs without input; the generic key script t |
| vault/TRANSFORMERS (240x320) 1.0.1 [Dedomil.net].jar | 614 | Transformers | - | 1.0.1 | MIDP-2.0 | m3g | 8 | 16 | 601 | 9202477 | 0 | javax/microedition/rms/InvalidRe | 运行正常 |  |
| vault/TRANSFORMERS (240x320) 1.0.2 [Dedomil.net].jar | 523 | Transformers | - | 1.0.2 | MIDP-2.0 | m3g | 86 | 16 | 600 | 31518915 | 0 | java/lang/IndexOutOfBoundsExcept | 运行正常 | runs without input; the generic key script t |
| vault/TRANSFORMERS (320x240) 1.0.1 [Andrew-Lviv.net].jar | 626 | Transformers | - | 1.0.1 | MIDP-2.0 | m3g | 8 | 16 | 600 | 31530030 | 0 | java/lang/IndexOutOfBoundsExcept | 运行正常 | runs without input; the generic key script t |
| vault/TRANSFORMERS (320x240) 1.0.1 [Dedomil.net].jar | 629 | Transformers | - | 1.0.1 | MIDP-2.0 | m3g | 8 | 16 | 600 | 31530032 | 0 | java/lang/IndexOutOfBoundsExcept | 运行正常 | runs without input; the generic key script t |
| vault/TRANSFORMERS G1_ AWAKENING (240x320) 1.0.4 [Dedomil.net].jar | 646 | Transformers G1 Awakening | - | 1.0.4 | MIDP-1.0 | m3g | 18 | 16 | 600 | 44609065 | 0 | java/io/EOFException | 运行正常 |  |
| vault/TRANSFORMERS G1_ AWAKENING (320x240) 1.0.0 [Dedomil.net].jar | 337 | Transformers G1 Awakening | - | 1.0.0 | MIDP-1.0 | - | 12 | 16 | 600 | 27580082 | 0 | java/io/EOFException | 运行正常 |  |
| vault/TRANSFORMERS_ DARK OF THE MOON (240x320) 4.4.95 [Andrew-Lviv.net].jar | 1019 | Transformers Dark of the Moo | - | 4.4.95 | MIDP-2.0 | - | 84 | 16 | 600 | 6810966 | 0 | - | 运行正常 |  |
| vault/TRANSFORMERS_ DARK OF THE MOON (240x320) 4.4.95 [Dedomil.net].jar | 1019 | Transformers Dark of the Moo | - | 4.4.95 | MIDP-2.0 | - | 84 | 16 | 600 | 6947163 | 0 | - | 运行正常 |  |
| vault/TRANSFORMERS_ DARK OF THE MOON (240x320) 4.5.15 [Dedomil.net].jar | 1020 | Transformers Dark of the Moo | - | 4.5.15 | MIDP-2.0 | - | 84 | 16 | 600 | 6947851 | 0 | - | 运行正常 |  |
| vault/TRANSFORMERS_ DARK OF THE MOON (240x320) 4.5.20 [Dedomil.net].jar | 1023 | Transformers Dark of the Moo | - | 4.5.20 | MIDP-2.0 | - | 84 | 16 | 600 | 7233290 | 0 | - | 运行正常 |  |
| vault/TRANSFORMERS_ DARK OF THE MOON (240x320) 4.7.22 [Andrew-Lviv.net].jar | 802 | Transformers Dark of the Moo | - | 4.7.22 | MIDP-2.0 | - | 82 | 16 | 600 | 4769049 | 0 | - | 运行正常 |  |
| vault/TRANSFORMERS_ DARK OF THE MOON (240x320) 4.7.22 [Dedomil.net].jar | 802 | Transformers Dark of the Moo | - | 4.7.22 | MIDP-2.0 | - | 82 | 16 | 600 | 4765467 | 0 | - | 运行正常 |  |
| vault/TRANSFORMERS_ DARK OF THE MOON (320x240) 4.6.74 [Andrew-Lviv.net].jar | 786 | Transformers Dark of the Moo | - | 4.9.49 | MIDP-2.0 | - | 80 | 16 | 600 | 12017121 | 0 | - | 运行正常 |  |
| vault/TRANSFORMERS_ DARK OF THE MOON (320x240) 4.6.74 [Dedomil.net].jar | 786 | Transformers Dark of the Moo | - | 4.6.74 | MIDP-2.0 | - | 80 | 16 | 600 | 12016861 | 0 | - | 运行正常 |  |
| vault/TRANSFORMERS_ DARK OF THE MOON (360x640) 4.6.51 [Dedomil.net].jar | 1012 | Transformers.3.:.Dark.of.the | - | 4.6.51 | MIDP-2.0 | - | 85 | 16 | 600 | 5457267 | 0 | - | 运行正常 |  |
| vault/TRANSFORMERS_ DARK OF THE MOON (360x640) 4.7.86 [Dedomil.net].jar | 1012 | Transformers.3.:.Dark.of.the | - | 4.7.86 | MIDP-2.0 | - | 85 | 16 | 600 | 5448077 | 0 | - | 运行正常 |  |
| vault/TRANSFORMERS_ DARK OF THE MOON (360x640) 4.8.25 [Andrew-Lviv.net].jar | 1012 | Transformers.3.:.Dark.of.the | - | 4.8.25 | MIDP-2.0 | - | 85 | 16 | 600 | 5111562 | 0 | - | 运行正常 |  |
| vault/TRANSFORMERS_ DARK OF THE MOON (360x640) 4.8.25 [Dedomil.net].jar | 1012 | Transformers.3.:.Dark.of.the | - | 4.8.25 | MIDP-2.0 | - | 85 | 16 | 600 | 5113381 | 0 | - | 运行正常 |  |
| vault/TRANSFORMERS_ REVENGE OF THE FALLEN (240x320) 1.0.0 Multi6 [Andrew-Lviv.net].jar | 721 | TRANSFORMERS: Revenge of the | - | 1.0.0 | MIDP-2.0 | - | 13 | 16 | 600 | 34478295 | 0 | java/lang/ArrayIndexOutOfBoundsE | 运行正常 |  |
| vault/TRANSFORMERS_ REVENGE OF THE FALLEN (240x320) 1.0.0 Multi6 [Dedomil.net].jar | 721 | TRANSFORMERS: Revenge of the | - | 1.0.0 | MIDP-2.0 | - | 13 | 16 | 600 | 34623730 | 0 | java/lang/ArrayIndexOutOfBoundsE | 运行正常 |  |
| vault/TRANSFORMERS_ REVENGE OF THE FALLEN (320x240) 1.0.0 Multi3 [PHONEKY].jar | 597 | TRANSFORMERS: ROTF | - | 1.0.0 | MIDP-2.0 | - | 31 | 16 | 600 | 52064480 | 0 | java/lang/ArrayIndexOutOfBoundsE | 运行正常 |  |
| vault/TRANSFORMERS_ REVENGE OF THE FALLEN (360x640) 1.0.0 [Glu Mobile Ltd].jar | 731 | TRANSFORMERS Revenge of the  | - | 1.0.0 | MIDP-2.0 | - | 36 | 16 | 600 | 34548654 | 0 | java/lang/ArrayIndexOutOfBoundsE | 运行正常 |  |
| vault/Taiko_no_Tatsujin_Japanese128x128-40143-mobiles24.jar | 214 | Taiko no Tatsujin | - | 1.0.0 | MIDP-2.0 | - | 10 | 16 | 600 | 92015423 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/Team Monster Us vs Them_320x240.jar | 526 | Team Monster | - | 0.9.15 | MIDP-2.0 | - | 5 | 16 | 600 | 16876168 | 0 | java/io/IOException,javax/microe | 运行正常 |  |
| vault/Tekken Mobile.jar | 759 | TEKKEN MOBILE | - | 1.0.7 | MIDP-2.0 | - | 32 | 16 | 600 | 5411541 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/Temple Rush 2.jar | 267 | Temple Rush 2 | - | 1.0.4 | MIDP-2.0 | - | 21 | 16 | 600 | 319098 | 0 | java/io/IOException,java/lang/St | 运行正常 |  |
| vault/Temple Rush.jar | 269 | Temple Rush | - | 1.0.3 | MIDP-2.0 | - | 21 | 16 | 600 | 319161 | 0 | java/io/IOException,java/lang/St | 运行正常 |  |
| vault/Terminator Salvation.jar | 939 | Terminator Salvation | - | 1.1.0 | MIDP-2.0 | - | 8 | 16 | 600 | 243872880 | 0 | - | 运行正常 |  |
| vault/TerminatorSalvation_1.0.5_320x240.jar | 471 | Terminator Salvation | - | 1.0.5 | MIDP-2.0 | - | 16 | 16 | 600 | 8159268 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/Tetris (2008).jar | 102 | Tetris | - | 1.21.29 | MIDP-1.0 | - | 57 | 16 | 600 | 1432780 | 0 | java/util/NoSuchElementException | 运行正常 |  |
| vault/Tetris Blockout_320x240.jar | 202 | Tetris Blockout | - | 4.6.17 | MIDP-2.0 | nokia-ui | 14 | 16 | 601 | 4409949 | 0 | java/io/IOException,java/lang/Ar | 运行正常 |  |
| vault/Tetris Mobile.jar | 361 | TETRIS® | - | 1.0.0 | MIDP-2.0 | - | 34 | 16 | 600 | 8616310 | 0 | - | 运行正常 |  |
| vault/Tetris_1.0.0_320x240.jar | 423 | TETRIS® | - | 1.0.0 | MIDP-2.0 | nokia-ui | 34 | 16 | 600 | 82001 | 0 | - | 运行正常 | self-paced title: runs 600 frames unpaced (t |
| vault/Tetris_320x240.jar | 636 | Tetris-New | - | 4.13.15 | MIDP-2.0 | m3g,nokia-ui | 114 | 16 | 600 | 31082242 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/TexasHoldemPoker1_1.1.0_320x240.jar | 413 | Texas HoldEm Poker | - | 1.1.0 | MIDP-2.0 | nokia-ui | 14 | 16 | 600 | 8556795938 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/TexasHoldemPoker2_1.3.3_320x240.jar | 1026 | Texas Hold'Em Poker 2 | - | 1.3.3 | MIDP-2.0 | - | 25 | 16 | 600 | 314704904 | 0 | java/lang/NullPointerException | 运行正常 |  |
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
| vault/The Oregon Trail - American Settler.jar | 1291 | Oregon Trail American Settle | - | 1.2.3 | MIDP-2.0 | - | 24 | 16 | 600 | 20864178 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/The Oregon Trail - Gold Rush.jar | 987 | The Oregon Trail: Gold Rush | - | 1.5.4 | MIDP-2.0 | - | 20 | 16 | 600 | 72665495 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/The Oregon Trail.jar | 594 | The Oregon Trail | - | 1.1.6 | MIDP-2.0 | - | 10 | 16 | 600 | 6470649 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/The Overtaker 3D.jar | 499 | The Overtaker 3D | - | 1.0.8 | MIDP-2.0 | - | 8 | 16 | 600 | 13279855 | 0 | java/io/IOException,java/lang/Nu | 运行正常 |  |
| vault/The Settlers.jar | 596 | The Settlers | - | 1.0.3 | MIDP-2.0 | - | 6 | 16 | 600 | 11058406 | 0 | - | 运行正常 |  |
| vault/The Simpsons - Itchy & Scratchy Land.jar | 767 | The Simpsons 2 | - | 7.1.97 | MIDP-2.0 | - | 8 | 16 | 600 | 3577384 | 0 | javax/microedition/rms/InvalidRe | 运行正常 |  |
| vault/The Simpsons - Minutes to Meltdown.jar | 395 | The Simpsons | - | 4.1.18 | MIDP-2.0 | - | 10 | 16 | 600 | 137426 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/The Sims 3 Ambitions_320x240.jar | 817 | The Sims 3 Ambitions | - | 4.2.28 | MIDP-2.0 | - | 26 | 16 | 600 | 12533407 | 0 | java/io/EOFException | 运行正常 |  |
| vault/The Sims DJ_320x240.jar | 636 | The Sims DJ | - | 7.2.47 | MIDP-2.0 | m3g | 34 | 16 | 600 | 44849433 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/TheAdventuresOfTintin_1.0.1_320x240.jar | 1068 | The Adventures of Tintin | - | 1.0.1 | MIDP-2.0 | - | 13 | 16 | 600 | 61812472 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/TheMummyTheTombOfTheDragonEmperor_1.0.0_320x240.jar | 634 | The Mummy: TOTDE | - | 1.0.0 | MIDP-2.0 | - | 10 | 16 | 600 | 653252347 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/TheSettlers_1.1.1_320x240.jar | 538 | The settlers | - | 1.1.1 | MIDP-2.0 | - | 6 | 16 | 600 | 10483025 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/TheSims3WinterEdition_0.2.12_320x240.jar | 719 | The Sims 3 Winter Edition | - | 0.2.12 | MIDP-2.0 | m3g | 37 | 16 | 600 | 11938645 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/TheSims3_7.6.60_320x240.jar | 523 | The Sims 3 | - | 7.6.60 | MIDP-2.0 | - | 27 | 16 | 600 | 14247018 | 0 | java/lang/ArrayIndexOutOfBoundsE | 运行正常 |  |
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
| vault/TomClancysHAWX_1.0.2_320x240.jar | 319 | Tom Clancy's H.A.W.X | - | 1.0.2 | MIDP-2.0 | - | 8 | 16 | 600 | 5463769904 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
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
| vault/Top Gear - The Mobile Game.jar | 604 | Top Gear: the mobile game | - | 1.1.0 | MIDP-2.0 | - | 17 | 16 | 600 | 77632713 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/Tornado Mania 3D.jar | 455 | 3D Tornado Mania | - | 1.0.0 | MIDP-2.0 | - | 24 | 16 | 600 | 12093672 | 0 | java/io/EOFException,java/lang/N | 运行正常 |  |
| vault/Tornado Mania.jar | 339 | Tornado Mania! | - | 1.1.0 | MIDP-2.0 | - | 10 | 16 | 600 | 13843036 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/TotalConquest_1.0.1_320x240.jar | 919 | Total Conquest by Ankit | - | 1.0.1 | MIDP-2.0 | - | 25 | 16 | 600 | 92962593 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/Totally Spies.jar | 306 | TotallySpies! | - | 1.0.0 | MIDP-2.0 | - | 8 | 16 | 600 | 111300189 | 0 | - | 运行正常 |  |
| vault/TotallySpies S60.jar | 193 | TotallySpies! | - | 1.1.4 | MIDP-1.0 | nokia-ui | 5 | 16 | 600 | 2754198846 | 0 | java/io/EOFException,java/lang/A | 运行正常 |  |
| vault/Tour de France 2010_320x240.jar | 297 | Tour De France 2010 | - | 1.0 | MIDP-2.0 | nokia-ui | 13 | 16 | 600 | 3751661 | 0 | - | 运行正常 |  |
| vault/Tournament BlackJack_320x240.jar | 323 | Tournament BJ | - | 1.0.17 | MIDP-2.0 | - | 49 | 16 | 600 | 28210450 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/Tower Bloxx - My City.jar | 927 | TBloxx My City | - | 1.0.4 | MIDP-2.0 | - | 39 | 16 | 600 | 17704855 | 0 | java/io/EOFException,javax/micro | 运行正常 |  |
| vault/Tower Bloxx - New York.jar | 1088 | Tower Bloxx NY | - | 1.0.0 | MIDP-2.0 | - | 56 | 16 | 600 | 5546675 | 0 | java/io/EOFException,javax/micro | 运行正常 |  |
| vault/Tower Bloxx - Revolution.jar | 779 | TowerBloxxRev | - | 1.0.2 | MIDP-2.0 | - | 52 | 16 | 600 | 5325095 | 0 | java/io/EOFException,javax/micro | 运行正常 |  |
| vault/Tower Bloxx Deluxe.jar | 426 | Tower Bloxx Deluxe | - | 1.0.2 | MIDP-2.0 | - | 20 | 16 | 600 | 12265510 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/Tower Bloxx New York_320x240_Nokia_E71.jar | 995 | Tower Bloxx NY | - | 1.0.4 | MIDP-2.0 | nokia-ui | 56 | 16 | 600 | 23503443 | 0 | java/io/EOFException,javax/micro | 运行正常 |  |
| vault/Tower Bloxx.jar | 165 | Tower Bloxx(TM) | - | 1.2.6 | MIDP-1.0 | - | 16 | 16 | 600 | 12653534 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/Tower Mania.jar | 247 | Tower Mania | - | 1.0.1 | MIDP-2.0 | - | 22 | 16 | 600 | 13025299 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/TowerDefense_1.0_320x240.jar | 204 | Tower defense | - | 1.0 | MIDP-2.0 | - | 20 | 16 | 600 | 17093085 | 0 | - | 运行正常 |  |
| vault/Townsmen 6 Revolution_320x240.jar | 630 | Townsmen 6 xsmart.ru | - | 1.0.0 | MIDP-2.0 | - | 24 | 16 | 601 | 3811760 | 0 | java/io/IOException,java/lang/Nu | 运行正常 |  |
| vault/Treasure Grab.jar | 443 | Treasure Grab | - | 1.0.44 | MIDP-1.0 | - | 10 | 16 | 600 | 196792598 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/Tropical Madness.jar | 309 | Tropical Madness | - | 1.1.5 | MIDP-2.0 | - | 6 | 16 | 600 | 38446869 | 0 | - | 运行正常 |  |
| vault/Turtle Dash.jar | 825 | Turtle Dash | - | 1.0.0 | MIDP-2.0 | - | 34 | 16 | 600 | 3021039 | 0 | java/io/EOFException,javax/micro | 运行正常 |  |
| vault/Turtle Dash_320x240.jar | 729 | Turtle Dash | - | 1.0.0 | MIDP-2.0 | nokia-ui | 34 | 16 | 600 | 59446855 | 0 | java/io/EOFException,javax/micro | 运行正常 |  |
| vault/Ultimate Brick Breaker 2_320x240.jar | 341 | Ultimate Brick Breaker 2 | - | 1.1.1 | MIDP-2.0 | - | 27 | 16 | 600 | 73895266 | 0 | java/lang/ClassCastException,jav | 运行正常 |  |
| vault/Ultimate Street Football.jar | 462 | Street Football | - | 1.0.0 | MIDP-2.0 | - | 8 | 16 | 600 | 450672577 | 0 | - | 运行正常 |  |
| vault/Ultra Phoenix - Resurgence.jar | 221 | Ultra Phoenix - Resurgence | - | 1.1.3 | MIDP-2.0 | - | 30 | 16 | 600 | 7783282 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/UnoSpin_1.0.6_320x240.jar | 757 | UNO Spin | - | 1.0.6 | MIDP-2.0 | - | 30 | 16 | 600 | 46156714 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/Uno_1.0.1_320x240.jar | 547 | UNO | - | 1.0.1 | MIDP-2.0 | - | 15 | 16 | 600 | 99992745 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/V-Rally 3D.jar | 304 | V-Rally 3D | - | 1.5.2 | MIDP-2.0 | - | 99 | 16 | 600 | 293785 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/VampireRomance_1.1.0_320x240.jar | 1053 | Vampire Romance | - | 1.1.0 | MIDP-2.0 | nokia-ui | 16 | 16 | 600 | 35741790 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/Vodafone McLaren Mercedes Team Racing.jar | 202 | VMM Team Racing by NH | - | 1.0.10 | MIDP-1.0 | - | 14 | 16 | 600 | 613357 | 0 | java/io/EOFException,java/lang/N | 运行正常 |  |
| vault/War Hero 1944.jar | 278 | War Hero 1944 Shared by Mate | - | 1.0.62 | MIDP-1.0 | - | 15 | 16 | 600 | 12641036 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/Watchmen.jar | 607 | Watchmen | - | 1.3.3 | MIDP-2.0 | - | 125 | 16 | 600 | 3951695 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/WhereInTheWorldIsCarmenSandiego_1.0.1_320x240.jar | 663 | Where in The World is Carmen | - | 1.0.1 | MIDP-2.0 | - | 5 | 16 | 600 | 33501829 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/WheresWallyNow_1.0.1_320x240.jar | 988 | Where is Wally Now | - | 1.0.1 | MIDP-2.0 | - | 7 | 16 | 600 | 43794996 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/Wild West Guns.jar | 1472 | Wild West Guns | - | 1.2.6 | MIDP-2.0 | - | 17 | 16 | 600 | 189574104 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/Wimbledon2009_1.0.4_320x240.jar | 706 | wimbledon 2009 | - | 1.0.4 | MIDP-2.0 | - | 11 | 16 | 600 | 90475906 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/Wizardry.jar | 187 | Wizardry | - | 1.75 | MIDP-1.0 | - | 19 | 16 | 600 | 57929768 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/Womens Murder Club_320x240_Nokia_E71.jar | 965 | Women's Murder Club Konon.Mo | - | 1.2.2 | MIDP-2.0 | - | 59 | 16 | 600 | 1333844 | 0 | java/lang/ArrayIndexOutOfBoundsE | 运行正常 |  |
| vault/Wonder Blocks.jar | 612 | Wonder Blocks | - | 1.1.0 | MIDP-2.0 | - | 10 | 16 | 600 | 22741070 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/Wonder Zoo.jar | 837 | Wonder Zoo | - | 1.2.9 | MIDP-2.0 | - | 14 | 16 | 600 | 17815357 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/Woody Woodpecker in Waterfools.jar | 615 | woody | - | 2.0.0 | MIDP-2.0 | - | 21 | 16 | 600 | 1044972 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/World Rally Championship 3D.jar | 635 | WRC 3D | - | 0.8.4 | MIDP-2.0 | - | 4 | 16 | 600 | 5337759 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/World at Arms.jar | 1957 | World At Arms | - | 1.0.9 | MIDP-2.0 | - | 16 | 16 | 600 | 422495137 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/Worms 2008.jar | 567 | Worms 2008 | - | 1.3.11 | MIDP-2.0 | - | 20 | 16 | 600 | 792340 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/Worms 2010.jar | 996 | Worms | - | 4.10.60 | MIDP-2.0 | - | 22 | 16 | 600 | 7856881 | 0 | - | 运行正常 |  |
| vault/Worms 2011 Armageddon.jar | 702 | Worms 2011 Armageddon | - | 4.1.18 | MIDP-2.0 | - | 34 | 16 | 601 | 3009588 | 0 | - | 运行正常 |  |
| vault/Worms Reloaded.jar | 757 | Worms Reloaded | - | 4.4.53 | MIDP-2.0 | - | 35 | 16 | 600 | 12965135 | 0 | - | 运行正常 |  |
| vault/Wrath of the Titans.jar | 1557 | Wrath of the Titans | - | 1.3.2 | MIDP-2.0 | - | 20 | 16 | 600 | 175367175 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/XIII - Covert Identity.jar | 598 | XIII_2 | - | 1.0.0 | MIDP-2.0 | - | 5 | 16 | 600 | 530645441 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/Xmas Jewel Explosion__320x240.jar | 180 | X-mas Jewel Explosion | - | 1.0.2 | MIDP-2.0 | - | 36 | 16 | 600 | 2135508319 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/Zombie Infection 2.jar | 1224 | Zombie Infection 2 | - | 1.1.9 | MIDP-2.0 | - | 13 | 16 | 600 | 202116318 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/Zombie Infection.jar | 1013 | Zombie Infection | - | 1.2.0 | MIDP-2.0 | - | 8 | 16 | 601 | 22099539 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/Zombie Mob Defense.jar | 772 | Zombie Mob | - | 1.0.0 | MIDP-2.0 | - | 39 | 16 | 600 | 4088760 | 0 | java/io/EOFException,javax/micro | 运行正常 |  |
| vault/ZombieInfection1_1.0.0_320x240.jar | 358 | Zombie Infection | - | 1.0.0 | MIDP-2.0 | - | 9 | 16 | 600 | 554243348 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/ZombieInfection2_1.0.3_320x240.jar | 430 | Zombie Infection 2 | - | 1.0.3 | MIDP-2.0 | - | 50 | 16 | 600 | 3547284421 | 0 | java/lang/ClassNotFoundException | 运行正常 |  |
| vault/ZombieInfection_SonyEricsson_X1_EN_IGPL_EU_TS_101.jar | 615 | Zombie Infection | - | 1.0.1 | MIDP-2.0 | - | 8 | 16 | 600 | 1632813305 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/Zombiewood.jar | 820 | Zombiewood | - | 1.2.9 | MIDP-2.0 | - | 42 | 16 | 600 | 93994054 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/Zoo Keeper.jar | 366 | ZOO KEEPER | - | 1.1.1 | MIDP-2.0 | - | 9 | 16 | 600 | 1274023133 | 0 | - | 运行正常 |  |
| vault/Zuma's Revenge!.jar | 1118 | Zumas Revenge | - | 00.01.47 | MIDP-2.0 | - | 8 | 16 | 601 | 120025308 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/adventure_time_hero_259515.jar | 232 | Adventure Time | - | 1.0.2 | MIDP-2.0 | - | 12 | 16 | 600 | 7355326 | 0 | java/lang/ArrayIndexOutOfBoundsE | 运行正常 |  |
| vault/adventuret_b1N4sR1I.jar | 276 | Adventure Time | - | 1.0.2 | MIDP-2.0 | - | 15 | 16 | 600 | 2397054 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/age_of_empires_iii_m_259219.jar | 296 | Age Of Empires III Mobile | - | 1.0.1 | MIDP-1.0 | - | 13 | 16 | 600 | 77065734 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/aloneinthe_x1i5e1cw.jar | 2939 | Alone in the dark | - | 1.0 | MIDP-2.0 | - | 54 | 16 | 600 | 93028074 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/alpha_wing_bqYpzkwZ.jar | 261 | Alpha Wing | - | 1.0.0 | MIDP-1.0 | - | 16 | 16 | 600 | 862571 | 0 | java/lang/IllegalArgumentExcepti | 运行正常 |  |
| vault/ancientemp_wn2uiomk.jar | 125 | Ancient Empires | - | 1.0.3 | MIDP-1.0 | nokia-ui | 9 | 16 | 600 | 194593 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/app.jar | 468 | Tomb Raider Legend | - | 1.17 | MIDP-2.0 | m3g | 22 | 16 | 600 | 11044948 | 0 | java/lang/IllegalArgumentExcepti | 运行正常 |  |
| vault/asphalt_3_street_rule_360x640_176330.jar | 1103 | Asphalt 3 3D Street Rules | - | 1.1.5 | MIDP-2.0 | m3g,nokia-ui | 18 | 16 | 600 | 4027072595 | 0 | java/lang/ClassNotFoundException | 运行正常 |  |
| vault/asphalt_6_adrenalin_258989.jar | 2684 | Asphalt6 Adrenaline | - | 1.0.3 | MIDP-2.0 | - | 20 | 32 | 600 | 519137752 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/assassins_creed_260615.jar | 633 | Assassins Creed | - | 1.0.0 | MIDP-2.0 | - | 6 | 16 | 0 | 0 | 0 | - | 超时 |  |
| vault/avenging-angel-352x416-s60-namobilu.com.jar | 291 | Avenging Angel | - | 2.00.36 | MIDP-2.0 | - | 5 | 16 | 600 | 5323001 | 0 | java/io/IOException,java/lang/Nu | 运行正常 |  |
| vault/biohazard-240x320-namobilu.com.jar | 640 | BioHazard | - | 1.0.0 | MIDP-1.0 | - | 61 | 16 | 0 | 0 | 0 | - | 失败 |  |
| vault/blades_and_magic_3d_multiscreen-27836.jar | 1142 | Blades and Magic | - | 1.0.9 | MIDP-2.0 | mascot | 86 | 16 | 600 | 7840845 | 0 | java/io/IOException,java/lang/Nu | 运行正常 |  |
| vault/bobby_carrot_2_wint_240201.jar | 109 | BC2 - Winterland | - | 1.0.6 | MIDP-1.0 | nokia-ui | 4 | 16 | 600 | 191690 | 0 | - | 运行正常 |  |
| vault/boggle_small-en.jar | 123 | Boggle | - | 1.4.4 | MIDP-2.0 | - | 3 | 16 | 600 | 2105290 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/bounce_Nokia.jar | 39 | Bounce | - | 1.24 | MIDP-1.0 | nokia-ui | 8 | 16 | 600 | 5071 | 0 | - | 运行正常 | runs without input; the generic key script m |
| vault/bounce_tale_qy5sf1si.jar | 426 | - | - | - | - | - | 0 | 16 | 0 | 0 | 0 | - | 非可用包 | not a zip file |
| vault/bounce_tales_travel_240x320_191806.jar | 521 | Bounce Tales | - | 2.0.3 | MIDP-2.0 | nokia-ui | 19 | 16 | 600 | 71706612 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/bounce_water_dash_240x320_191284.jar | 93 | Bounce Water Dash v0.3 TEGOS | - | 1.0.0 | MIDP-2.0 | - | 25 | 16 | 600 | 870057 | 0 | java/lang/ClassNotFoundException | 运行正常 |  |
| vault/bounceback_64b1jou2.jar | 96 | Bounce Back | - | 1.13 | MIDP-1.0 | nokia-ui | 10 | 16 | 600 | 101939 | 0 | - | 运行正常 |  |
| vault/bouncedash_shItN4ur.jar | 153 | Bounce Dash | - | 1.0.0 | MIDP-2.0 | - | 26 | 16 | 600 | 3264798848 | 0 | java/lang/ClassNotFoundException | 运行正常 |  |
| vault/bouncetale_9s1otkmz.jar | 368 | Bounce Tales | - | 2.0.3 | MIDP-2.0 | nokia-ui | 19 | 16 | 1 | 37056 | 0 | - | 标题自行退出 | exited itself (notifyDestroyed) after 1 fram |
| vault/bouncetale_e568o65n.jar | 356 | Bounce Tales 2 | - | 2.0.3 | MIDP-2.0 | nokia-ui | 19 | 16 | 600 | 71476810 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/bouncetale_xzz6ksx6.jar | 1027 | Bounce Tales mod by Gamemen | - | 2.0.3 | MIDP-2.0 | nokia-ui | 19 | 16 | 600 | 72178217 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/brick_breaker_explos_131333.jar | 99 | Brick Breaker Explosive | - | 1.0.0 | MIDP-2.0 | - | 14 | 16 | 600 | 2389613663 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/brick_breaker_revolu_360x640_138536.jar | 494 | BB Revolution2 | - | 1.0.11 | MIDP-2.0 | - | 57 | 16 | 600 | 53847417 | 0 | java/io/EOFException,javax/micro | 运行正常 |  |
| vault/brickbreak_tbpx16jj.jar | 1097 | Block Breaker Deluxe 2 by D@ | - | 1.1.5 | MIDP-2.0 | - | 11 | 16 | 600 | 7931950139 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/button_s40.jar | 62 | Button_s40 | - | F1.1 | MIDP-1.0 | nokia-ui | 14 | 16 | 600 | 1740312 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/castle_of_magic_256724.jar | 1333 | Castle of Magic | - | 1.1.0 | MIDP-2.0 | nokia-ui | 10 | 16 | 600 | 165318 | 0 | - | 运行正常 |  |
| vault/castlevani_hvcp9fbl.jar | 405 | Castlevania Aria Of Sorrow | - | 1.0.0 | MIDP-2.0 | - | 5 | 16 | 600 | 12119843 | 0 | javax/microedition/rms/InvalidRe | 运行正常 |  |
| vault/castlevania_dawn_of_240x320_168931.jar | 610 | Castlevania:Paccвeт пeчaли | - | 0.1.2 | MIDP-2.0No | - | 20 | 16 | 600 | 930170 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/chocolate3_klm926l6.jar | 455 | 3D Tornado Mania | - | 1.0.0 | MIDP-2.0 | m3g,nokia-ui | 24 | 16 | 600 | 17435353 | 0 | java/io/EOFException,java/lang/N | 运行正常 |  |
| vault/city_rush_256058.jar | 717 | CityRush | - | 1.0.0 | MIDP-2.0 | - | 95 | 16 | 600 | 13268167 | 0 | java/io/IOException,java/lang/Nu | 运行正常 |  |
| vault/command_amp_conque_261392.jar | 1143 | C&C4 Tiberian Twilight | - | 4.1.58 | MIDP-2.0 | - | 37 | 16 | 600 | 3973070 | 0 | - | 运行正常 |  |
| vault/connect2media_f_Acer_S200NeoTouch_X1i_en_castlevaniaorderofshadows_v0_0_1.jar | 555 | CastlevaniaOoS | - | 0.0.1 | MIDP-2.0 | - | 70 | 16 | 600 | 1023283 | 0 | javax/microedition/rms/InvalidRe | 运行正常 |  |
| vault/cowboys_amp_aliens_261159.jar | 3442 | Cowboys And Aliens | - | 1.2.3 | MIDP-2.0 | - | 18 | 16 | 600 | 107566405 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/critter_crunch_133978_1657.jar | 222 | Critter Crunch | - | 1.0.0 | MIDP-1.0 | nokia-ui | 10 | 16 | 600 | 174101833 | 0 | java/io/EOFException,java/io/IOE | 运行正常 |  |
| vault/cyber_punk.jar | 404 | CyberPunk Arasakas Plot Etty | - | 1.3.2 | MIDP-2.0 | nokia-ui | 41 | 16 | 600 | 1079250 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/darkest-fear-176x220-320x480-namobilu.com.jar | 185 | Darkest Fear | - | 1.19 | MIDP-1.0 | - | 20 | 16 | 600 | 4458255645 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/darkest-fear-2-grim-oak-240x320-s40-namobilu.com.jar | 213 | Darkest Fear 2 Grim Oak | - | 1.30 | MIDP-1.0 | - | 14 | 16 | 600 | 1258018603 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/darkest-fear-3-nightmare-352x416-s60-namobilu.com.jar | 283 | Darkest Fear Nightmare | - | 1.20 | MIDP-1.0 | - | 13 | 16 | 600 | 799004 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/darkest_fear_multiscreen-119931.jar | 182 | Darkest Fear | - | 1.19 | MIDP-1.0 | nokia-ui | 20 | 16 | 600 | 4405914483 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/dead-daughters-240x320-namobilu.com.jar | 166 | Dead Daughters srv | - | 1.0.24 | MIDP-2.0 | - | 60 | 16 | 600 | 43834757 | 0 | java/io/IOException | 运行正常 |  |
| vault/death-trap-176x208-namobilu.com.jar | 98 | Death Trap | - | 1.0.0 | MIDP-2.0 | - | 18 | 16 | 600 | 2752016 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/death-trap-2-the-unlocked-code-240x320-namobilu.com.jar | 255 | Death Trap 2 | - | 1.0.0 | MIDP-2.0 | - | 12 | 16 | 600 | 2446559 | 0 | java/lang/ArrayIndexOutOfBoundsE | 运行正常 |  |
| vault/doom-rpg-ii-360x640-s60-touch-namobilu.com.jar | 1103 | Doom II RPG | - | 5.4.14 | MIDP-2.0 | - | 37 | 16 | 600 | 15806798 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/doom-rpg-mobile-240x320-namobilu.com.jar | 295 | Doom RPG | - | 1.8.84 | MIDP-1.0 | - | 26 | 16 | 600 | 7897808 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/doomrpg_tmmcss29.jar | 274 | Doom RPG | - | 1.0.20 | MIDP-2.0 | - | 23 | 16 | 600 | 17455962 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/dragonisla_ortv8jio.jar | 76 | Dragon Island | - | 1.0.2 | MIDP-1.0 | nokia-ui | 26 | 16 | 600 | 39698 | 0 | - | 运行正常 |  |
| vault/drives-the-evil-spirit-240x320-namobilu.com.jar | 495 | Drives the Evil Spirit | - | 1.0 | MIDP-2.0 | - | 27 | 16 | 600 | 378405 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/edge_extended_260821.jar | 396 | Edge EX | - | 1.0.3 | MIDP-1.0 | - | 2 | 16 | 600 | 21797619 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/eightfatet_tt83sn7j.jar | 574 | Eight Fate The Horror Before | - | 1.0 | MIDP-1.0 | - | 10 | 16 | 600 | 2458090130 | 0 | - | 运行正常 |  |
| vault/electronicarts_tombraiderunderworld_de_en_es_fr_it_Nokia_5530_v1_8_8.jar | 1304 | TR Underworld | - | 1.8.8 | MIDP-1.0 | m3g | 7 | 16 | 600 | 17832608 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/elvenchron_66js6a7i.jar | 232 | Elven Chronicles by street_r | - | 1.0.5 | MIDP-2.0 | nokia-ui | 11 | 16 | 600 | 12897194 | 0 | java/lang/NumberFormatException | 运行正常 |  |
| vault/epub_reader_albite_136959_great.jar | 798 | Albite Reader | - | 2.2 | MIDP-2.0 | - | 149 | 16 | 60 | 296 | 0 | java/lang/NullPointerException | 标题自身异常 | title passes null to Displayable.setTitle()  |
| vault/fifa23 j2me.jar | 1050 | FIFA 23 | - | 1.1.0 | MIDP-2.0 | - | 29 | 16 | 600 | 57756297 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/game-of-death-240x320-namobilu.com.jar | 596 | 死亡游戏(3D增强版) | - | 1.0.0 | MIDP-2.0 | - | 26 | 16 | 600 | 16008257 | 0 | - | 运行正常 |  |
| vault/gameloft.medieval.combat.v1.04.s60.java.retail-binpda.jar | 200 | Medieval Combat - Age Of Glo | - | 1.0.4 | MIDP-1.0 | nokia-ui | 5 | 16 | 600 | 3493893649 | 0 | - | 运行正常 |  |
| vault/gangstar_1_city.jar | 299 | Gangstar -leo5354- | - | 1.0.5 | MIDP-2.0 | - | 5 | 16 | 600 | 5314902369 | 0 | javax/microedition/rms/InvalidRe | 运行正常 |  |
| vault/gangstar_2_kings_of_260766.jar | 645 | Gangstar 2 | - | 1.0.3 | MIDP-2.0 | - | 12 | 16 | 0 | 0 | 0 | - | 超时 |  |
| vault/gangstar_rio_city_o_260851.jar | 2111 | Gangstar Rio City of Saints | - | 1.1.3 | MIDP-2.0 | - | 15 | 16 | 600 | 666913338 | 0 | - | 运行正常 |  |
| vault/glu_sonicthehedgehog2dash_sonye_k800_de_en_es_fr_it_pt_v1_0_0_full.jar | 515 | Sonic the Hedgehog 2 Dash | - | 1.0.0 | MIDP-2.0 | - | 28 | 16 | 600 | 915862 | 0 | java/io/IOException,javax/microe | 运行正常 |  |
| vault/go_to_hell_139738.jar | 124 | Go To Hell | - | 5.0 | MIDP-2.0 | - | 14 | 16 | 600 | 463520193 | 0 | - | 运行正常 |  |
| vault/god_of_war_betrayal_258309.jar | 417 | God of War | - | 1.0.0 | MIDP-1.0 | - | 7 | 16 | 600 | 20964406 | 0 | java/lang/NumberFormatException, | 运行正常 |  |
| vault/gothic_3_-_the_beginning_multiscreen-57752.jar | 318 | Gothic3TheBeginning | - | 1.0 | MIDP-2.0 | nokia-ui | 5 | 16 | 600 | 3621240 | 0 | java/io/IOException,java/lang/Nu | 运行正常 |  |
| vault/gumballjou_alRKKtCJ.jar | 1624 | Gumball | - | 1.1.2 | MIDP-2.0 | - | 16 | 16 | 600 | 3920598 | 0 | - | 运行正常 |  |
| vault/hauntings-the-abandoned-mansion-240x320-namobilu.com.jar | 422 | Haunting Mansion | - | 1.0.0 | MIDP-2.0 | - | 5 | 16 | 600 | 26871628 | 0 | - | 运行正常 |  |
| vault/holywar_fsb7evml.jar | 207 | HolyWars | - | 01.01.02 | MIDP-2.0 | nokia-ui | 19 | 16 | 600 | 1018123 | 0 | - | 运行正常 |  |
| vault/house-13-240x320-namobilu.com.jar | 322 | House N13 | - | 1.0 | MIDP-2.0 | - | 4 | 16 | 600 | 232304 | 0 | - | 运行正常 |  |
| vault/iSudoku_320x240.jar | 164 | iSudoku | - | 1.0.0 | MIDP-1.0 | - | 65 | 16 | 600 | 642207 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/knight_tales_land_o_240742.jar | 168 | KnightTales | - | 01.00.04 | MIDP-1.0 | nokia-ui | 11 | 16 | 600 | 599772 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/land_of_the_dead_day_320x240_136780.jar | 306 | LOTD | - | 1.1.14 | MIDP-1.0 | - | 10 | 16 | 600 | 314909437 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/lego_batman_the_mob_261440.jar | 2192 | Lego Batman | - | 1.1.9 | MIDP-2.0 | - | 17 | 16 | 600 | 391961232 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/miami_nights_2_the_258694.jar | 1620 | Miami Nights 2 | - | 1.0.7 | MIDP-2.0 | - | 9 | 16 | 600 | 1340968064 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/midnight_pool_3_258300.jar | 2301 | Midnight Pool 3 | - | 1.2.3 | MIDP-2.0 | - | 30 | 16 | 600 | 1091771851 | 0 | java/lang/ArrayIndexOutOfBoundsE | 运行正常 |  |
| vault/mini3draci_ix48iqod.jar | 252 | MINI 3D | - | 1.0.22 | MIDP-2.0 | mascot | 43 | 16 | 600 | 494280 | 0 | java/lang/IllegalArgumentExcepti | 运行正常 |  |
| vault/modern_combat_2_bla_261155.jar | 3061 | Modern Combat 2: Black Pegas | - | 1.1.1 | MIDP-2.0 | - | 14 | 16 | 600 | 599283014 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/mradio.jar | 38 | Mocha Radio | - | 1.1.0 | MIDP-2.0 | - | 11 | 16 | 600 | 162717 | 0 | java/io/IOException | 运行正常 |  |
| vault/my_life_in_new_york_258886.jar | 2235 | My Life in New York | - | 1.1.0 | MIDP-2.0 | - | 15 | 16 | 600 | 86481464 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/need_for_speed_hot_p_259337.jar | 1022 | Need for Speed Hot Pursuit B | - | 4.16.46 | MIDP-2.0 | - | 42 | 16 | 600 | 10846362 | 0 | java/io/IOException,java/lang/Nu | 运行正常 |  |
| vault/nokia solitare 128x160.jar | 88 | Solitaire | - | 2.1 | MIDP-2.0 | nokia-ui | 6 | 16 | 600 | 1164910 | 0 | java/io/IOException,java/lang/Nu | 运行正常 |  |
| vault/nowhere-360x640-touch-namobilu.com.jar | 275 | Nowhere | - | 02.00.00 | MIDP-2.0 | - | 11 | 16 | 600 | 4611840 | 0 | - | 运行正常 |  |
| vault/nowhere_360x640_173649.jar | 275 | Nowhere | - | 02.00.00 | MIDP-2.0 | nokia-ui | 11 | 16 | 600 | 4623593 | 0 | - | 运行正常 |  |
| vault/pacman_149998.jar | 25 | Pac Man | - | 1.2 | MIDP-2.0 | - | 10 | 16 | 600 | 12036 | 0 | javax/microedition/rms/RecordSto | 运行正常 | runs without input; the generic key script m |
| vault/plants vs zombies (symbian).jar | 1746 | Plants vs Zombies | - | 4.3.26 | MIDP-2.0 | - | 99 | 16 | 600 | 6619525 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/playman_extreme_runn_260866.jar | 215 | Playman Extreme Running | - | 1.1.2 | MIDP-2.0 | nokia-ui | 8 | 16 | 600 | 16528429 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/radikal_snowboard_240x400_195557.jar | 425 | Radikal Snowboard | - | 1.0.5 | MIDP-2.0 | nokia-ui | 18 | 16 | 600 | 125539015 | 0 | java/lang/ArrayIndexOutOfBoundsE | 运行正常 |  |
| vault/rayman_3_241466.jar | 172 | Rayman3 | - | 1.1.8 | MIDP-2.0 | - | 5 | 16 | 600 | 3882779961 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/real_1_soccer_2005_ifp63zdu.jar | 89 | RealSoccer2005 | - | 1.1.0 | MIDP-1.0 | nokia-ui | 13 | 16 | 600 | 3414775 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/residentev_4hfni67z.jar | 900 | Resident Evil - The Missions | - | 1.0.0 | MIDP-2.0 | m3g | 9 | 16 | 600 | 881456 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/residentev_6pju7m54.jar | 262 | Resident Evil - The Missions | - | 1.0.0 | MIDP-2.0 | - | 3 | 16 | 600 | 1733424 | 0 | java/lang/ArrayIndexOutOfBoundsE | 运行正常 |  |
| vault/residentev_ksnzt715.jar | 880 | RE-AfterLife-Waryam | - | 1.3.98 | MIDP-1.0 | - | 95 | 16 | 600 | 3204805 | 0 | - | 运行正常 |  |
| vault/rollercoaster_1.jar | 930 | Roller Rev 99 | - | 1.1.9 | MIDP-2.0 | nokia-ui | 45 | 16 | 601 | 71106920 | 0 | java/io/EOFException,java/lang/O | 运行正常 |  |
| vault/rollercoaster_2.jar | 273 | 3D Coaster Rush by street_ra | - | 1.0.0 | MIDP-2.0 | m3g,nokia-ui | 19 | 16 | 600 | 14449082 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/shadowwalk_lGB8Oa7P.jar | 461 | Shadowalker | - | 1.0.6 | MIDP-1.0 | nokia-ui | 12 | 16 | 600 | 11234464 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/siberianstrike.jar | 63 | SiberianStrike | - | 1.0.6 | MIDP-1.0 | nokia-ui | 16 | 16 | 600 | 4490445719 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/silent-hill-mobile-2-352x416-s60-namobilu.com.jar | 482 | Silent Hill Mobile 2 | - | 1.0.0 | MIDP-2.0 | - | 12 | 16 | 600 | 7940321 | 0 | java/io/IOException,java/lang/Nu | 运行正常 |  |
| vault/silent-hill-mobile-3-320x240-mot-namobilu.com.jar | 580 | Silent Hill 3 | - | 1.1.0 | MIDP-2.0 | - | 13 | 16 | 600 | 3110690 | 0 | java/lang/NumberFormatException, | 运行正常 |  |
| vault/silent-hill-orphan-352x416-s60-namobilu.com.jar | 487 | Silent Hill | - | 0.1.0 | MIDP-2.0 | - | 10 | 16 | 600 | 3859596 | 0 | java/io/IOException,javax/microe | 运行正常 |  |
| vault/snake_2.jar | 42 | Snake EX2 | - | 1.19 | MIDP-1.0 | nokia-ui | 10 | 16 | 600 | 107938 | 0 | java/lang/ClassNotFoundException | 运行正常 | runs without input; the generic key script m |
| vault/snake_3.jar | 260 | Snake III | - | 3.07.2 | MIDP-2.0 | m3g | 49 | 16 | 600 | 133916 | 0 | java/util/EmptyStackException,ja | 运行正常 | runs without input; the generic key script m |
| vault/snowboardh_zmu8yadv.jar | 795 | Snowboard Hero | - | 1.0.1 | MIDP-2.0 | mascot | 133 | 16 | 600 | 87090605 | 0 | java/io/IOException,java/lang/Nu | 运行正常 |  |
| vault/snowboardr_m6km8xw5.jar | 374 | Pro Snowboard | - | 1.02 | MIDP-2.0 | m3g,nokia-ui | 26 | 16 | 600 | 2497309 | 0 | java/io/EOFException,javax/micro | 运行正常 |  |
| vault/solitaire_b1q8jwpn.jar | 86 | Solitaire7650 | - | 1.0.6 | MIDP-1.0 | nokia-ui | 3 | 16 | 600 | 4194439 | 0 | - | 运行正常 |  |
| vault/sonic_advance_259061.jar | 1634 | Sonic Advance | - | 1.3.5 | MIDP-2.0 | - | 177 | 16 | 600 | 3605132 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/sonic_amp_sega_all_261109.jar | 1069 | Sonic Racing | - | 1.2.8 | MIDP-2.0 | - | 30 | 16 | 600 | 43211971 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/sonic_jump_2_240x320_164217.jar | 334 | Sonic Jump 2 supplied by lua | - | 1.0.4 | MIDP-1.0 | - | 7 | 16 | 600 | 2657108 | 0 | java/io/IOException,javax/microe | 运行正常 |  |
| vault/sonicforno_ja9diknl.jar | 509 | SonicJump | - | 1.0 | MIDP-1.0 | nokia-ui | 7 | 16 | 603 | 1600177 | 0 | java/io/IOException,java/lang/Nu | 运行正常 |  |
| vault/sonicthehedgehog2-crash_de_en_es_fr_it_pt_SonyEricssonK800i.jar | 718 | Sonic the Hedgehog 2 Crash | - | 1.1.1 | MIDP-2.0 | nokia-ui | 34 | 16 | 600 | 423494 | 0 | javax/microedition/rms/InvalidRe | 运行正常 |  |
| vault/soulofdark_7xlflnqy.jar | 873 | Soul of darkness 2 | - | 1.0 | MIDP-1.0 | nokia-ui | 10 | 16 | 600 | 3494366077 | 0 | - | 运行正常 |  |
| vault/spider-man_toxic_ci_258680.jar | 930 | Spider-Man: Toxic City | - | 1.0.5 | MIDP-2.0 | - | 12 | 16 | 600 | 136339 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/splinter_cell_1_mobile_.jar | 159 | Splinter Cell | - | 1.1.8 | MIDP-2.0 | - | 8 | 16 | 600 | 3345578416 | 0 | - | 运行正常 |  |
| vault/splinter_cell_2_exten_240x320_157266.jar | 136 | SC-EO sefan.ru | - | 1.0.2 | MIDP-2.0 | - | 4 | 16 | 600 | 3943339774 | 0 | - | 运行正常 |  |
| vault/splinter_cell_3.jar | 318 | Splinter Cell DA | - | 1.0.5 | MIDP-2.0 | - | 6 | 16 | 600 | 3271240280 | 0 | - | 运行正常 |  |
| vault/splinter_cell_4_tom_clancys_.jar | 228 | SC-PT | - | 1.0.6 | MIDP-2.0 | nokia-ui | 14 | 16 | 600 | 1936700024 | 0 | javax/microedition/rms/InvalidRe | 运行正常 |  |
| vault/spyro_k7927_nguy_156863.jar | 465 | Spyro Dawn of the Dragon | - | 0.5.7 | MIDP-2.0 | - | 8 | 16 | 600 | 2139670 | 0 | java/io/IOException,javax/microe | 运行正常 |  |
| vault/stayalive_lzrustoe.jar | 482 | StayAlive SEfan.RU | - | 03.30.01 | MIDP-2.0 | - | 9 | 16 | 600 | 868762 | 0 | javax/microedition/rms/InvalidRe | 运行正常 |  |
| vault/stranded_157079_3963.jar | 233 | Stranded | - | 1.0.0 | MIDP-1.0 | - | 9 | 16 | 600 | 6708262 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/terrors-seed-352x416-namobilu.com.jar | 394 | TerrorsSeed yMoBi.ru | - | 1.0.1 | MIDP-2.0 | - | 20 | 16 | 600 | 155417147 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/the_phantom_240x320_158584.jar | 113 | The Phantom | - | 1.1.3 | MIDP-2.0 | - | 4 | 16 | 600 | 199053711 | 0 | - | 运行正常 |  |
| vault/the_sims_3_ambition_260250.jar | 837 | The Sims 3 Ambitions | - | 4.5.81 | MIDP-2.0 | - | 28 | 16 | 600 | 11697421 | 0 | java/io/EOFException,java/lang/N | 运行正常 |  |
| vault/the_sims_3_winter_ed_259820.jar | 332 | The Sims 3 Winter Edition | - | 0.2.81 | MIDP-2.0 | - | 28 | 16 | 600 | 11346324 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/the_witcher_159073_4136.jar | 261 | The Witcher | - | 1.0.0 | MIDP-2.0 | - | 7 | 16 | 600 | 6191615 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/the_wolfman_253792.jar | 624 | The Wolfman by Namco | - | 1.0.0 | MIDP-2.0 | - | 6 | 16 | 600 | 13937096 | 0 | - | 运行正常 |  |
| vault/tong-yuan-240x320-namobilu.com.jar | 523 | TongYuan By BerON | - | 1.0 | MIDP-2.0 | - | 10 | 16 | 600 | 1473601 | 0 | - | 运行正常 |  |
| vault/tooth_bride_320x240_189157.jar | 213 | Tooth Bride | - | 1.0 | MIDP-2.0 | - | 6 | 16 | 600 | 1096244 | 0 | - | 运行正常 |  |
| vault/tornado_mania_360x640_187050.jar | 382 | Tornado Mania! | - | 1.7.40 | MIDP-2.0 | - | 10 | 16 | 600 | 155223 | 0 | java/io/EOFException,javax/micro | 运行正常 |  |
| vault/totally_spies_the_m_240159.jar | 285 | Totally Spies **MASYAKA** | - | 1.1.7 | MIDP-2.0 | - | 5 | 16 | 600 | 2881037662 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/treasure_towers_240x320-113258.jar | 307 | Treasure Towers | - | 1.4.4 | MIDP-2.0 | mascot | 5 | 16 | 600 | 5698950 | 0 | java/lang/IllegalArgumentExcepti | 运行正常 |  |
| vault/tron_snowboarding_240x320_192401.jar | 922 | TRON-mod by SL280296 | - | 1.0.1 | MIDP-2.0 | m3g | 135 | 16 | 600 | 69256111 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/ultimate_brick_break_260584.jar | 399 | Ultimate Brick Breaker | - | 1.0.0 | MIDP-2.0 | - | 31 | 16 | 600 | 75303942 | 0 | java/lang/ClassCastException,jav | 运行正常 |  |
| vault/ultimate_brick_break_260678.jar | 404 | Ultimate Brick Breaker 2 | - | 1.1.3 | MIDP-2.0 | - | 31 | 16 | 600 | 74101335 | 0 | java/lang/ClassCastException,jav | 运行正常 |  |
| vault/uno_gameloft.jar | 816 | UNO | - | 1.0.3 | MIDP-2.0 | nokia-ui | 14 | 16 | 600 | 352213560 | 0 | - | 运行正常 | runs without input; the generic key script d |
| vault/vampiresas_46oqnqm4.jar | 562 | Vampire's Assistant | - | 0.0.58 | MIDP-2.0 | - | 72 | 16 | 600 | 7901818 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/virus-240x320-namobilu.com.jar | 157 | Virus | - | 1.0.0 | MIDP-2.0 | nokia-ui | 33 | 16 | 600 | 713620 | 0 | java/io/EOFException,java/lang/A | 运行正常 |  |
| vault/wolf-moon-240x320-namobilu.com.jar | 217 | WolfMoon | - | 1.06 | MIDP-1.0 | - | 17 | 16 | 600 | 1709957722 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/xiii_240x320_169505.jar | 175 | XIII | - | 1.0.0 | MIDP-2.0 | - | 4 | 16 | 600 | 2852036516 | 0 | - | 运行正常 |  |
| vault/zombie_infection_259648.jar | 615 | Zombie Infection | - | 1.0.1 | MIDP-2.0 | - | 8 | 16 | 600 | 1528880586 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/zombie_infection_2_240x320_146117.jar | 1068 | Zombie Infection 2 | - | 1.2.3 | MIDP-2.0 | - | 12 | 16 | 600 | 6332668034 | 0 | java/lang/ArrayIndexOutOfBoundsE | 运行正常 |  |
| vnes.jar | 148 | vNes | - | 1.0.0 | - | nokia-ui | 67 | 16 | 600 | 1092674 | 0 | java/io/IOException,java/lang/Nu | 运行正常 | painted 602 frames but made no draw calls (b |
| 七龙珠－终极原始大乱斗_5589.jar | 986 | 七龙珠-终极原始大乱斗 | - | 1.0.0 | MIDP-1.0 | - | 26 | 16 | 600 | 1111572 | 0 | - | 运行正常 |  |
| 三国争霸之赤壁大战_11681.jar | 233 | 三国争霸之赤壁大战 | - | 1.0.0 | MIDP-1.0 | - | 11 | 16 | 600 | 1552009 | 0 | - | 运行正常 |  |
| 三国塔防－曹操传_1610.jar | 1066 | 三国塔防-曹操传 | - | 2.0.1 | MIDP-1.0 | nokia-ui | 148 | 16 | 600 | 174620478 | 0 | java/lang/ArrayIndexOutOfBoundsE | 运行正常 |  |
| 三国志_10617.jar | 213 | C2S－三国志 | - | 1.0 | MIDP-1.0 | - | 2 | 16 | 600 | 4177804973 | 0 | - | 运行正常 |  |
| 三国志吞食天地_8144.jar | 513 | 三国志：吞食天地 | - | 1.0.2 | MIDP-2.0 | - | 4 | 16 | 600 | 308900 | 0 | java/lang/NullPointerException | 运行正常 | runs without input; the generic key script m |
| 三国志霸王的大陆V3.2.2_5005.jar | 674 | BWDL | - | 1.0 | MIDP-2.0 | - | 7 | 16 | 600 | 543734 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| 三国杀2012_809.jar | 1203 | sanguosha | - | 1.0 | MIDP-1.0 | nokia-ui | 81 | 16 | 600 | 7793434 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| 三国杀2．2美化版_1347.jar | 1171 | SanGuoQ | - | 2.2 | MIDP-2.0 | - | 376 | 16 | 600 | 5713666 | 0 | java/io/IOException | 运行正常 |  |
| 三国杀单机BT版_4227.jar | 318 | 三国杀 | - | 1.0.0 | MIDP-2.0 | - | 17 | 16 | 600 | 597691 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| 三国杀单机战功修改版_4143.jar | 894 | SGSJB | - | 1.0.0 | MIDP-2.0 | - | 17 | 16 | 600 | 8343486 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| 三国杀单机版_4312.jar | 895 | SGSJB | - | 1.0.0 | MIDP-2.0 | - | 17 | 16 | 600 | 8345562 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| 三国群英传_11539.jar | 58 | 奥汀-三国群英传正版 | - | 1.0 | MIDP-1.0 | nokia-ui | 9 | 16 | 600 | 577781 | 0 | javax/microedition/rms/InvalidRe | 运行正常 |  |
| 三国群雄争霸_9023.jar | 271 | 超值-三国群雄争霸 | - | 1.0 | MIDP-1.0 | nokia-ui | 3 | 16 | 600 | 134753 | 0 | - | 运行正常 |  |
| 三国赤壁小乔秘史_7976.jar | 643 | 小乔秘史wap.91soyo.com破解 | - | 1.0.0 | MIDP-2.0 | nokia-ui | 24 | 16 | 600 | 3553855874 | 0 | java/lang/NullPointerException | 运行正常 | runs without input; the generic key script t |
| 世嘉头文字D－宿命对决_1247.jar | 1008 | 世嘉头文字D-宿命对决 | - | 1.0.0 | MIDP-1.0 | m3g | 159 | 16 | 600 | 444323 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| 世界五子棋大奖赛_355.jar | 426 | 世界五子棋大奖赛 | - | 1.0 | MIDP-1.0 | - | 9 | 16 | 600 | 58950 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| 世界冠军拉力赛3D_8360.jar | 635 | WRC 3D | - | 0.8.4 | MIDP-2.0 | m3g | 4 | 16 | 600 | 8752244 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| 世界最强-银星围棋_2631.jar | 402 | wjzq | - | 1.0 | MIDP-1.0 | - | 24 | 16 | 600 | 2742478 | 0 | java/lang/NullPointerException | 运行正常 |  |
| 世界武术巡回赛教学版_3484.jar | 442 | 世界武术巡回赛-教学版 | - | 0.3.3 | MIDP-2.0 | - | 8 | 16 | 600 | 3970459699 | 0 | java/lang/ArrayIndexOutOfBoundsE | 运行正常 |  |
| 世界足球2012中文版_1724.jar | 881 | 世界足球2012 | - | 1.0.0 | MIDP-2.0 | - | 16 | 16 | 600 | 41233545 | 0 | java/lang/NullPointerException | 运行正常 |  |
| 世界足球2012中文版_780.jar | 881 | 世界足球2012 | - | 1.0.0 | MIDP-2.0 | - | 16 | 16 | 600 | 41233581 | 0 | java/lang/NullPointerException | 运行正常 |  |
| 丧尸危机2-自由之翼BT版_997.jar | 772 | 丧尸危机2试玩版 | - | 1.0 | MIDP-2.0 | - | 124 | 16 | 600 | 560722 | 0 | - | 运行正常 |  |
| 丧尸围城_10590.jar | 289 | Dead Rising | - | 1.0.1 | MIDP-2.0 | - | 43 | 16 | 600 | 2536700 | 0 | java/lang/IllegalArgumentExcepti | 运行正常 |  |
| 中华麻将馆－极限胡牌版_244.jar | 310 | 中华麻将馆-极限胡牌版 | - | 1.0 | MIDP-1.0 | - | 18 | 16 | 600 | 3469594475 | 0 | java/io/IOException,java/lang/Nu | 运行正常 |  |
| 中国象棋_10893.jar | 293 | 中国象棋三国经典版 | - | 1.0 | MIDP-2.0 | - | 9 | 16 | 600 | 920135 | 0 | javax/microedition/rms/InvalidRe | 运行正常 |  |
| 中国象棋之水墨珍藏版_871.jar | 375 | 中国象棋之水墨珍藏版 | - | 1.0.0 | MIDP-2.0 | - | 12 | 16 | 624 | 3560906484 | 0 | java/lang/ArrayIndexOutOfBoundsE | 运行正常 |  |
| 乱世Q三国_1649.jar | 977 | 乱世Q三国 | - | 1.0 | MIDP-1.0 | - | 232 | 16 | 0 | 128 | 0 | javax/microedition/rms/RecordSto | 未呈现画面 | exited itself (notifyDestroyed) without pain |
| 乱舞三国之群英会_2972.jar | 817 | 乱舞三国之群英会 | - | 1.0 | MIDP-1.0 | - | 35 | 16 | 600 | 284938024 | 0 | java/io/IOException,java/lang/Nu | 运行正常 |  |
| 五子棋_10940.jar | 42 | XOXO | - | 1.0 | MIDP-1.0 | - | 24 | 16 | 600 | 284275 | 0 | - | 运行正常 |  |
| 仙剑奇侠传(2008)_12406.jar | 695 | pal | - | 1.0 | MIDP-1.0 | nokia-ui | 3 | 16 | 600 | 201560 | 0 | - | 运行正常 |  |
| 仙剑奇侠传_10388.jar | 297 | 仙剑奇侠传 | - | 0.0.1 | MIDP-2.0 | - | 6 | 16 | 600 | 11468175 | 0 | javax/microedition/media/MediaEx | 运行正常 |  |
| 仙剑奇侠传完美版_11857.jar | 360 | 仙剑奇侠传 | - | 06.03.10 | MIDP-1.0 | nokia-ui | 4 | 16 | 600 | 4025277 | 0 | - | 运行正常 |  |
| 仙逆－诛仙弑神_1441.jar | 1356 | 仙逆-诛仙弑神 | - | 1.0.0 | MIDP-1.0 | - | 48 | 16 | 600 | 4665296376 | 0 | java/io/IOException,java/lang/Cl | 运行正常 |  |
| 仙魔飞行棋_417.jar | 294 | 仙魔飞行棋 | - | 1.0 | MIDP-2.0 | - | 21 | 16 | 600 | 2531840 | 0 | java/lang/ArrayIndexOutOfBoundsE | 运行正常 |  |
| 众神殿之王子归来_8404.jar | 103 | 王子归来 | - | 1.0.0 | MIDP-2.0 | nokia-ui | 14 | 16 | 600 | 1034152 | 0 | - | 运行正常 |  |
| 使命召唤4现代战争_9388.jar | 449 | Call of Duty 4 by serviak | - | 1.0.4 | MIDP-2.0 | - | 5 | 16 | 600 | 8277840 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| 使命召唤5-战争世界中文版_4583.jar | 416 | 使命召唤5-战争世界 | - | 1.0.1 | MIDP-2.0 | - | 22 | 16 | 600 | 1293429993 | 0 | java/lang/IllegalArgumentExcepti | 运行正常 |  |
| 侠盗飞车_6246.jar | 360 | 侠盗飞车-Gameloft正版 | - | 1.0.0 | MIDP-2.0 | - | 6 | 16 | 600 | 4525414511 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| 侠盗飞车加强版_5640.jar | 350 | 侠盗飞车-Gameloft正版 | - | 1.00 | MIDP-2.0 | - | 11 | 16 | 600 | 4006939421 | 0 | javax/microedition/rms/InvalidRe | 运行正常 |  |
| 俄罗斯方块中文版_2832.jar | 121 | 俄罗斯方块 | - | 1.0.0 | MIDP-2.0 | - | 56 | 16 | 600 | 805891 | 0 | java/io/IOException,java/util/No | 运行正常 |  |
| 倚天屠龙之君临天下_12230.jar | 564 | 倚天屠龙之君临天下 | - | 1.0 | MIDP-2.0 | - | 64 | 16 | 600 | 2649647 | 0 | java/lang/NumberFormatException, | 运行正常 |  |
| 倚天屠龙记_4827.jar | 836 | 倚天屠龙记 | - | 1.0 | MIDP-1.0 | nokia-ui | 62 | 16 | 600 | 10061139 | 0 | - | 运行正常 |  |
| 倚天屠龙记之魔教教主_4663.jar | 798 | GEYTTJZ | - | 1.0.4 | MIDP-2.0 | - | 7 | 16 | 600 | 4034221788 | 0 | - | 运行正常 |  |
| 傲世三国志－群雄争霸_5633.jar | 514 | 傲世三国志-群雄争霸 | - | 1.0 | MIDP-1.0 | nokia-ui | 15 | 16 | 600 | 3452891219 | 0 | - | 运行正常 |  |
| 傲气雄鹰_10432.jar | 249 | 傲气雄鹰汉化版 | - | 1.08 | MIDP-2.0 | - | 10 | 16 | 600 | 1962909 | 0 | - | 运行正常 |  |
| 傲视三国之赵云传_11024.jar | 235 | 傲视三国之赵云传 | - | 1.0 | MIDP-1.0 | nokia-ui | 22 | 16 | 600 | 1560897 | 0 | java/lang/ArithmeticException | 运行正常 |  |
| 僵尸危机2_4349.jar | 1068 | Zombie Infection 2 | - | 1.2.3 | MIDP-2.0 | - | 12 | 16 | 600 | 6331177101 | 0 | java/lang/ArrayIndexOutOfBoundsE | 运行正常 |  |
| 兔子波比2冰原之旅汉化版_11509.jar | 68 | BC2 - Winterland | - | 1.0.0 | MIDP-1.0 | nokia-ui | 11 | 16 | 600 | 182911 | 0 | - | 运行正常 |  |
| 冠军足球经理2010汉化版_5198.jar | 543 | CM-2010 | - | 1.2.1 | MIDP-2.0 | nokia-ui | 21 | 16 | 600 | 11690552 | 0 | - | 运行正常 |  |
| 冰淇淋少女成长日记_7307.jar | 344 | 冰淇淋少女成长日记_玉品 | - | 1.0 | MIDP-2.0 | nokia-ui | 9 | 16 | 600 | 472091 | 0 | - | 运行正常 | runs without input; the generic key script m |
| 凡人修仙传2-乱星海_12474.jar | 979 | 凡人修仙传2-乱星海 | - | 1.00 | MIDP-1.0 | - | 18 | 16 | 433 | 157743 | 0 | java/lang/NullPointerException | 运行正常 | exited itself (notifyDestroyed) |
| 凡人修仙传之七派六宗_4145.jar | 984 | 凡人修仙传 | - | 1.0.0 | MIDP-1.0 | nokia-ui | 16 | 16 | 600 | 174186 | 0 | java/io/IOException,java/lang/Nu | 运行正常 |  |
| 凤凰战机_10636.jar | 216 | Ultra Phoenix - Resurgence | - | 1.1.3 | MIDP-2.0 | - | 32 | 16 | 600 | 6657353 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| 初恋物语_7612.jar | 286 | 初恋物语 | - | 1.0 | MIDP-1.0 | - | 9 | 16 | 600 | 36255 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| 刺客信条3_266.jar | 1623 | Assassin's Creed III | - | 1.1.1 | MIDP-2.0 | nokia-ui | 12 | 16 | 600 | 115011954 | 0 | java/lang/NullPointerException | 运行正常 |  |
| 刺客信条兄弟会中文版_3668.jar | 1213 | 刺客信条-兄弟会 | - | 1.0.0 | MIDP-2.0 | - | 12 | 16 | 601 | 20099777 | 0 | java/lang/NullPointerException | 运行正常 |  |
| 剑侠奇缘-玉女问情_2208.jar | 703 | 剑侠奇缘-玉女问情 | - | 1.0 | MIDP-1.0 | - | 30 | 16 | 600 | 18169510 | 0 | java/lang/NumberFormatException | 运行正常 |  |
| 剑侣情缘Ⅱ魔神至尊传(无bug)_12464.jar | 904 | 剑侣情缘Ⅱ魔神至尊传 | - | 1.0 | MIDP-1.0 | - | 20 | 16 | 600 | 2974608 | 0 | java/io/EOFException | 运行正常 |  |
| 剑灵－古剑魂梦_676.jar | 1531 | 剑灵-古剑魂梦 | - | 1.0.0 | MIDP-2.0 | nokia-ui | 50 | 16 | 600 | 11831012 | 0 | java/io/EOFException | 运行正常 |  |
| 午夜保龄球2中文版_9782.jar | 589 | 终极保龄球2 | - | 1.0.0 | MIDP-2.0 | nokia-ui | 12 | 16 | 600 | 5279957147 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| 午夜台球圣诞中文版_8677.jar | 221 | 极限台球挑战赛 | - | 1.04 | MIDP-2.0 | - | 6 | 16 | 600 | 613577369 | 0 | - | 运行正常 |  |
| 危城无双之三国群英_4891.jar | 220 | 危城无双 | - | 1.0 | MIDP-1.0 | nokia-ui | 2 | 16 | 600 | 2541311013 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| 反恐精英2正式版_5836.jar | 510 | CT episod 2 | - | 1.0 | MIDP-2.0 | nokia-ui | 21 | 16 | 600 | 8159826 | 0 | java/lang/IllegalArgumentExcepti | 运行正常 |  |
| 口袋仓鼠_11936.jar | 441 | Lab Hamster\gkataria | - | 1.0 | MIDP-2.0 | - | 23 | 16 | 470 | 1426038 | 0 | java/lang/NullPointerException,j | 运行正常 | exited itself (notifyDestroyed) |
| 口袋妖怪_7872.jar | 54 | 口袋妖怪 | - | 1.0 | MIDP-1.0 | nokia-ui | 8 | 16 | 600 | 1486128 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| 口袋妖怪水晶汉化版_4524.jar | 1067 | MeBoy | - | 1.5.0 | MIDP-2.0 | - | 7 | 16 | 600 | 812 | 0 | javax/microedition/rms/RecordSto | 运行正常 | painted 600 frames but made no draw calls (b |
| 口袋灵兽_9902.jar | 582 | 口袋灵兽 | - | 1.0 | MIDP-2.0 | nokia-ui | 41 | 16 | 600 | 231915 | 0 | - | 运行正常 |  |
| 古剑奇缘-诛仙_4913.jar | 954 | 古剑奇缘-诛仙 | - | 1.0.0 | MIDP-1.0 | - | 64 | 16 | 600 | 1126873345 | 0 | - | 运行正常 |  |
| 台球传奇_87.jar | 917 | 台球传奇 | - | 1.0 | MIDP-1.0 | - | 53 | 16 | 600 | 11175832 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| 史前部落_11059.jar | 486 | Prehistoric Tribes | - | 0.1.28 | MIDP-2.0 | - | 7 | 16 | 600 | 912527172 | 0 | - | 运行正常 |  |
| 合金弹头2011_5629.jar | 401 | 合金弹头2011 | - | 1.0 | MIDP-1.0 | - | 7 | 16 | 600 | 6028396010 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| 合金弹头2013BT版_678.jar | 1021 | 合金弹头2013(正版) | - | 1.0 | MIDP-1.0 | - | 19 | 16 | 600 | 247961 | 0 | java/lang/ClassNotFoundException | 运行正常 |  |
| 合金弹头4(完美汉化版)_4053.jar | 798 | Metal Slug 4 | - | 1.0.1 | MIDP-2.0 | - | 6 | 16 | 600 | 6086868 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| 周末的沙滩_7453.jar | 1480 | Beach Games 12-Pack | - | 1.0.0 | MIDP-2.0 | nokia-ui | 37 | 16 | 600 | 13287983 | 0 | java/io/EOFException,javax/micro | 运行正常 |  |
| 哆啦A梦-秘密道具大作战_6557.jar | 552 | 哆啦A梦-秘密道具大作战(正版) | - | 1.0 | MIDP-1.0 | nokia-ui | 26 | 16 | 600 | 7200712389 | 0 | - | 运行正常 |  |
| 哆啦A梦运动会_10939.jar | 348 | 哆啦A梦运动会(正版)W | - | 1.0.0 | MIDP-1.0 | - | 17 | 16 | 600 | 587844 | 0 | - | 运行正常 |  |
| 哆啦A梦－3D卡丁车增值版_1560.jar | 1104 | 哆啦A梦-3D卡丁车 | - | 2.01 | MIDP-1.0 | nokia-ui | 73 | 16 | 600 | 31953326 | 0 | java/io/IOException,java/lang/Nu | 运行正常 |  |
| 喜羊羊与灰太狼_9241.jar | 296 | 喜羊羊与灰太狼 | - | 1.0.0 | MIDP-2.0 | nokia-ui | 11 | 16 | 604 | 3015305630 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| 国色天香MM斗地主真人版_11364.jar | 324 | 国色天香-MM斗地主(真人版) | - | 1.0 | MIDP-1.0 | - | 6 | 16 | 600 | 592726 | 0 | javax/microedition/rms/InvalidRe | 运行正常 |  |
| 国色天香斗地主_1364.jar | 1249 | 国色天香斗地主 | - | 1.0 | MIDP-2.0 | nokia-ui | 58 | 16 | 600 | 22127966 | 0 | - | 运行正常 |  |
| 圣百合女子学园-纯色序曲_4875.jar | 993 | SaintLily | - | 1.0.0 | MIDP-2.0 | - | 12 | 16 | 600 | 539553 | 0 | - | 运行正常 |  |
| 地下城与勇士之鬼剑士_9408.jar | 381 | 地下城与勇士-鬼剑士 | - | 1.0.0 | MIDP-2.0 | - | 27 | 16 | 600 | 57510861 | 0 | - | 运行正常 |  |
| 地心探险(地心游记)_12471.jar | 94 | 新浪-地心游记 | - | 1.0 | MIDP-2.0 | - | 9 | 16 | 600 | 5389863967 | 0 | - | 运行正常 |  |
| 坦克大战（经典版）_190.jar | 570 | 3D坦克大战(经典版) | - | 1.0 | MIDP-1.0 | - | 18 | 16 | 600 | 24694689 | 0 | - | 运行正常 |  |
| 塔塔_2848.jar | 693 | 塔塔 | - | 1.0 | MIDP-1.0 | - | 6 | 16 | 600 | 194135523 | 0 | java/lang/ClassNotFoundException | 运行正常 |  |
| 夜半鬼吹灯_8363.jar | 222 | 夜半鬼吹灯 | - | 1.0 | MIDP-2.0 | nokia-ui | 5 | 16 | 600 | 68823 | 0 | - | 运行正常 |  |
| 夜店3D斗地主_2296.jar | 847 | 夜店3D斗地主 | - | 1.0.0 | MIDP-2.0 | - | 15 | 16 | 600 | 33240 | 0 | javax/microedition/rms/InvalidRe | 运行正常 |  |
| 大富翁4纵横四海_8749.jar | 273 | 大富翁4纵横四海 | - | 1.0 | MIDP-1.0 | - | 13 | 16 | 600 | 323918 | 0 | - | 运行正常 |  |
| 大富翁4－大宇正版_467.jar | 636 | 大富翁4大宇正版 | - | 1.0 | MIDP-2.0 | - | 17 | 16 | 600 | 13365087 | 0 | java/lang/NullPointerException | 运行正常 |  |
| 大富翁中国之旅_11873.jar | 97 | DFWZGZL | - | 1.0 | MIDP-2.0 | - | 10 | 16 | 600 | 201419 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| 大魔界村_11996.jar | 1234 | Ghouls N Ghosts | - | 1.1.64 | MIDP-2.0 | nokia-ui | 103 | 16 | 600 | 3314297 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| 大鱼吃小鱼－深海探险2_614.jar | 633 | 大鱼吃小鱼-深海探险2 | - | 1.0.0 | MIDP-1.0 | - | 63 | 16 | 600 | 353890 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| 太空侵略者3_archive.jar | 85 | Space Impact III | - | 1.2 | MIDP-1.0 | nokia-ui | 11 | 16 | 600 | 1594576 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| 太空冲击303_archive.jar | 85 | Space Impact III | - | 1.2 | MIDP-1.0 | nokia-ui | 11 | 16 | 600 | 1604336 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| 头文字D之疾风骤雨_2140.jar | 938 | 世嘉头文字D-疾风骤雨 | - | 1.0.0 | MIDP-2.0 | m3g | 26 | 16 | 600 | 467678 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| 奥特曼-宇宙英雄超银河传说_2233.jar | 1245 | 奥特曼-宇宙英雄超银河传说 | - | 1.0.0 | MIDP-1.0 | - | 139 | 16 | 600 | 227337 | 0 | - | 运行正常 |  |
| 女仆咖啡厅_1244.jar | 640 | 女仆咖啡厅 | - | 1.0 | MIDP-2.0 | - | 42 | 16 | 600 | 2313768 | 0 | - | 运行正常 |  |
| 女神转校生-心跳回忆_4993.jar | 908 | 女神转校生-心跳回忆 | - | 1.0 | MIDP-2.0 | - | 58 | 16 | 600 | 1766429 | 0 | java/lang/ClassNotFoundException | 运行正常 |  |
| 孢子生命起源_6515.jar | 658 | EA-孢子生命起源 | - | 1.0.0 | MIDP-2.0 | - | 42 | 16 | 600 | 3109298700 | 0 | java/io/IOException,java/lang/Nu | 运行正常 |  |
| 孤岛余生2时间之谜汉化版_7371.jar | 616 | 孤岛余生2 | - | 1.0.3 | MIDP-2.0 | m3g | 36 | 16 | 600 | 10316820 | 0 | javax/microedition/rms/InvalidRe | 运行正常 |  |
| 孤岛余生汉化版_10145.jar | 453 | Stranded | - | 1.0.1 | MIDP-1.0 | - | 9 | 16 | 600 | 2385509 | 0 | java/lang/NullPointerException | 运行正常 |  |
| 学姐的秘密_9071.jar | 121 | 学姐的秘密 | - | 1.0 | MIDP-2.0 | - | 10 | 16 | 600 | 77194 | 0 | - | 运行正常 |  |
| 宇宙战机_10521.jar | 175 | Triad | - | 1.0.0 | MIDP-2.0 | - | 19 | 16 | 600 | 3216785337 | 0 | java/io/IOException,java/lang/Cl | 运行正常 |  |
| 实况足球2008中文版_9284.jar | 567 | 世界足球2008-明星正版 | - | 1.01 | MIDP-2.0 | - | 7 | 16 | 600 | 1771284430 | 0 | - | 运行正常 |  |
| 实况足球2012_2122.jar | 905 | Real Football 2012 | - | 1.0.5 | MIDP-2.0 | - | 14 | 16 | 600 | 180193677 | 0 | java/lang/NullPointerException | 运行正常 |  |
| 实况足球经理2009_10609.jar | 652 | RF MANAGER 09 | - | 1.1.3 | MIDP-2.0 | nokia-ui | 8 | 16 | 600 | 690977847 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| 宠物之王_12445.jar | 933 | 91soyo.com-宠物之王 | - | 1.0 | MIDP-2.0 | - | 63 | 16 | 621 | 1634709 | 0 | java/lang/RuntimeException,javax | 运行正常 |  |
| 宠物狗狗中文版_11635.jar | 253 | 无敌Z狗狗 | - | 1.02 | MIDP-2.0 | nokia-ui | 6 | 16 | 600 | 3935683588 | 0 | java/lang/NullPointerException | 运行正常 |  |
| 宠物王国2紫_5550.jar | 438 | cwwg2_z | - | 1.0.0 | MIDP-1.0 | nokia-ui | 19 | 16 | 600 | 1736710 | 0 | - | 运行正常 |  |
| 宠物王国3金_4892.jar | 585 | 宠物王国3(宝软网baoruan.com免费分享 | - | 1.0 | MIDP-1.0 | nokia-ui | 22 | 16 | 600 | 151536 | 0 | - | 运行正常 |  |
| 宠物王国4-白金_6531.jar | 699 | 宠物王国4-白金 | - | 1.0 | MIDP-2.0 | nokia-ui | 16 | 16 | 600 | 541434 | 0 | - | 运行正常 |  |
| 宠物王国5-彩虹_3413.jar | 1029 | 宠物王国5-彩虹 | - | 1.0 | MIDP-2.0 | nokia-ui | 23 | 16 | 600 | 384539 | 0 | - | 运行正常 |  |
| 宠物王国5-彩虹二次BT版_3356.jar | 1125 | 宠物王国5-彩虹 | - | 1.0 | MIDP-2.0 | nokia-ui | 28 | 16 | 600 | 148609 | 0 | - | 运行正常 |  |
| 宠物王国6(去模拟器限制)_12473.jar | 1387 | PetKing6 | - | 1.0 | MIDP-2.0 | - | 72 | 16 | 600 | 5350212 | 0 | java/io/EOFException | 运行正常 |  |
| 宠物王国6－烈火_465.jar | 1449 | 宠物王国6-烈火 | - | 1.0 | MIDP-2.0 | - | 121 | 16 | 600 | 12279208 | 0 | java/io/EOFException,java/lang/C | 运行正常 |  |
| 宠物精灵爱之体验_2861.jar | 1391 | Lovepet | - | 1.0 | MIDP-2.0 | - | 6 | 16 | 600 | 399811 | 0 | - | 运行正常 |  |
| 家园4汉化版_10405.jar | 215 | Townsmen4 | - | 1.0 | MIDP-2.0 | nokia-ui | 8 | 16 | 600 | 3125884 | 0 | java/lang/IllegalArgumentExcepti | 运行正常 |  |
| 家园5汉化版_10376.jar | 239 | Townsmen5 | - | 1.5 | MIDP-1.0 | nokia-ui | 7 | 16 | 600 | 2914291 | 0 | java/lang/ArrayIndexOutOfBoundsE | 运行正常 |  |
| 家园6之革命_7692.jar | 643 | Townsmen 6 | - | 1.0.0 | MIDP-2.0 | - | 26 | 16 | 601 | 1544837 | 0 | java/io/IOException,java/lang/Nu | 运行正常 |  |
| 家园6之革命完美汉化版_7583.jar | 590 | 家园6_光辉革命 | - | 1.0.0 | MIDP-2.0 | - | 26 | 16 | 601 | 1340488 | 0 | java/io/IOException,java/lang/Nu | 运行正常 |  |
| 寂静岭2汉化版_10177.jar | 507 | Silent Hill Mobile 2 | - | 1.0.0 | MIDP-2.0 | - | 12 | 16 | 600 | 8458128 | 0 | java/io/IOException,java/lang/Nu | 运行正常 |  |
| 寂静岭汉化版_9575.jar | 415 | Silent Hill | - | 0.1.0 | MIDP-2.0 | - | 11 | 16 | 600 | 3520173 | 0 | java/io/IOException,javax/microe | 运行正常 |  |
| 密室逃脱_7300.jar | 103 | 密室逃脱 | - | 1.0 | MIDP-1.0 | nokia-ui | 5 | 16 | 600 | 1058764 | 0 | - | 运行正常 |  |
| 寻秦皇陵的诅咒_12450.jar | 573 | 寻秦 | - | 1.0.0 | MIDP-1.0 | nokia-ui | 33 | 16 | 600 | 422796 | 0 | - | 运行正常 |  |
| 帝国时代2汉化版_7864.jar | 62 | 帝国时代2（微软正版） | - | 1.0 | MIDP-1.0 | nokia-ui | 9 | 16 | 600 | 1119468 | 0 | javax/microedition/rms/InvalidRe | 运行正常 |  |
| 帝国时代3-亚洲王朝_6288.jar | 595 | 帝国时代III亚洲王朝 | - | 1.0.0 | MIDP-2.0 | - | 41 | 16 | 600 | 6847622 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| 帝国时代Ⅲ汉化版_10379.jar | 426 | Age Of Empires III Mobile | - | 1.0.1 | MIDP-1.0 | - | 10 | 16 | 600 | 3218742 | 0 | java/lang/NullPointerException | 运行正常 |  |
| 开心仓鼠大作战_3911.jar | 506 | kxcsdz | - | 1.0.0 | MIDP-1.0 | - | 38 | 16 | 600 | 30350162 | 0 | java/lang/IllegalArgumentExcepti | 运行正常 |  |
| 开心农场BT版_6075.jar | 624 | 开心农场 | - | 1.0 | MIDP-1.0 | - | 13 | 16 | 600 | 1567799 | 0 | java/io/IOException,java/lang/Nu | 运行正常 |  |
| 开心农场_240.jar | 374 | happy_farm | - | 1.30.04 | MIDP-2.0 | - | 39 | 16 | 600 | 1625863 | 0 | - | 运行正常 |  |
| 开心农场_6370.jar | 374 | happy_farm | - | 1.30.04 | MIDP-2.0 | - | 39 | 16 | 600 | 1635132 | 0 | - | 运行正常 |  |
| 开心餐厅2012_745.jar | 794 | 开心餐厅2012 | - | 1.2 | MIDP-2.0 | - | 76 | 16 | 600 | 149282 | 0 | java/lang/NullPointerException | 运行正常 |  |
| 异形战机汉化版_8382.jar | 268 | R-Type | - | 1.0 | MIDP-2.0 | - | 5 | 16 | 600 | 4141503 | 0 | java/io/IOException,java/lang/Nu | 运行正常 |  |
| 彩京打击者1945_10623.jar | 206 | Strikers 1945 | - | 1.0.0 | MIDP-2.0 | - | 4 | 16 | 600 | 15029 | 0 | - | 运行正常 |  |
| 彩虹岛汉化版_10241.jar | 124 | Rainbow Islands [MICHANG] | - | 1.0.6 | MIDP-2.0 | - | 16 | 16 | 600 | 8578522 | 0 | java/io/IOException | 运行正常 |  |
| 德州扑克2_3884.jar | 985 | Texas Hold'Em Poker 2 | - | 1.2.9 | MIDP-2.0 | - | 26 | 16 | 600 | 224555504 | 0 | java/lang/NullPointerException | 运行正常 |  |
| 忍者神龟之最终的判决_8288.jar | 357 | TMNT The Ninja Tribunal | - | 1.22 | MIDP-2.0 | - | 4 | 16 | 600 | 123114219 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| 快乐农场_2089.jar | 782 | 快乐农场之僵尸来袭 | - | 1.0.0 | MIDP-2.0 | - | 36 | 16 | 600 | 2149359 | 0 | - | 运行正常 |  |
| 快打旋风_10825.jar | 273 | FinalFight | - | 1.2.5 | MIDP-2.0 | - | 4 | 16 | 600 | 3475314 | 0 | - | 运行正常 |  |
| 急速公路狂飙－新年版_84.jar | 880 | 急速公路狂飙新年版 | - | 1.0 | MIDP-2.0 | m3g | 119 | 16 | 600 | 22516527 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| 急速狂飙完美破解版_4964.jar | 1301 | HighSpeed 3D | - | 1.6.0 | MIDP-2.0 | m3g | 48 | 16 | 600 | 93295279 | 0 | java/io/EOFException,java/io/IOE | 运行正常 |  |
| 恋爱物语－我的机器女友_6219.jar | 973 | cyborg_lover | - | 1.30.01 | MIDP-2.0 | - | 66 | 16 | 600 | 18592486 | 0 | java/lang/NegativeArraySizeExcep | 运行正常 |  |
| 恋色狂想之魔女恋歌_4899.jar | 774 | LCMS | - | 01.30.00 | MIDP-2.0 | - | 88 | 16 | 600 | 6871928 | 0 | - | 运行正常 |  |
| 愤怒的小鸟-高清版_2353.jar | 16451 | Angry Birds - by novchik | - | 0.1.1 | MIDP-2.0 | - | 45 | 32 | 600 | 6146201 | 0 | java/io/IOException,java/lang/Il | 运行正常 |  |
| 愤怒的小鸟_383.jar | 6333 | Angry Birds | - | 1.1 | MIDP-2.1 | - | 145 | 16 | 600 | 63719 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| 我们要恋爱之校园篇_1815.jar | 779 | 我们要恋爱之校园篇 | - | 1.0.0 | MIDP-2.0 | - | 32 | 16 | 600 | 156136 | 0 | java/lang/NullPointerException | 运行正常 |  |
| 我的宠物我做主BT版_3055.jar | 633 | 我的宠物我做主(正版)_玉品 | - | 1.0 | MIDP-1.0 | - | 20 | 16 | 600 | 20831448 | 0 | - | 运行正常 |  |
| 战争机器1945_5414.jar | 466 | 1945 | - | 1.0.0 | MIDP-1.0 | - | 11 | 16 | 600 | 633486 | 0 | java/lang/StringIndexOutOfBounds | 运行正常 |  |
| 战争艺术2之全球联盟中文版_1918.jar | 573 | 战争艺术2 | - | 1.0.0 | MIDP-2.0 | - | 15 | 16 | 600 | 6425484 | 0 | java/io/EOFException,java/lang/I | 运行正常 |  |
| 扫雷行动_10904.jar | 559 | Minesweeper Mobile | - | 0.0.1 | MIDP-2.0 | nokia-ui | 11 | 16 | 600 | 15058084 | 0 | java/lang/NullPointerException | 运行正常 |  |
| 拳王2009南拳北腿_1167.jar | 331 | 拳王2009-南拳北腿 | - | 1.0 | MIDP-1.0 | - | 8 | 16 | 600 | 976487 | 0 | - | 运行正常 |  |
| 拳王争霸_11585.jar | 582 | Lucha Libre | - | 1.0.8 | MIDP-2.0 | nokia-ui | 6 | 16 | 600 | 973443966 | 0 | java/lang/NullPointerException | 运行正常 |  |
| 拳皇98终极之战_7534.jar | 831 | 拳皇98-终极之战 | - | 1.0 | MIDP-1.0 | - | 37 | 16 | 600 | 7185988 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| 捕鱼达人3金装版_1511.jar | 634 | 深海猎奇-捕鱼达人 | - | 1.00 | MIDP-1.0 | nokia-ui | 15 | 16 | 600 | 6089020 | 0 | java/io/IOException,java/lang/Nu | 运行正常 |  |
| 文明3_10522.jar | 160 | Civilization 3 | - | 1.0.0 | MIDP-2.0 | - | 7 | 16 | 600 | 9382721 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| 斗po苍穹bt版_1127.jar | 1609 | 斗破苍穹 | - | 1.0.0 | MIDP-1.0 | - | 37 | 16 | 600 | 30346373 | 0 | java/io/EOFException,java/io/IOE | 运行正常 |  |
| 斩妖伏魔录3_6306.jar | 1016 | 斩妖伏魔录3 | - | 1.0 | MIDP-2.0 | nokia-ui | 21 | 16 | 600 | 6412330 | 0 | java/io/IOException | 运行正常 |  |
| 斩妖伏魔录4(QQ挑战)_2922.jar | 1043 | 斩妖伏魔录4 | - | 1.0 | MIDP-2.0 | nokia-ui | 55 | 16 | 600 | 8779945 | 0 | java/io/IOException | 运行正常 |  |
| 斩妖伏魔录_6231.jar | 584 | 斩妖伏魔录(华娱) | - | 1.0 | MIDP-1.0 | nokia-ui | 19 | 16 | 600 | 4283335 | 0 | java/io/IOException,java/lang/Nu | 运行正常 |  |
| 新2011拳皇_4132.jar | 948 | 新拳皇2011 | - | 1.0 | MIDP-1.0 | - | 33 | 16 | 600 | 2681311 | 0 | java/io/EOFException,javax/micro | 运行正常 |  |
| 新三国志_10225.jar | 186 | 新三国志 | - | 1.0.0 | MIDP-2.0 | - | 17 | 16 | 600 | 58737 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| 新牧场物语2恋爱季节_1340.jar | 991 | 新牧场物语2恋爱季节 | - | 1.0.0 | MIDP-2.0 | - | 73 | 16 | 600 | 2892196 | 0 | java/lang/NullPointerException | 运行正常 |  |
| 新西游记－格斗天王_1211.jar | 1327 | 新西游记-格斗天王 | - | 1.0 | MIDP-1.0 | - | 56 | 16 | 600 | 32650882 | 0 | - | 运行正常 |  |
| 时光八音盒2忆之篇_3865.jar | 1014 | 时光八音盒II_忆之篇_玉品 | - | 1.0.0 | MIDP-1.0 | - | 11 | 16 | 600 | 25620804 | 0 | java/io/IOException,java/lang/Ar | 运行正常 |  |
| 时光八音盒萌之篇_3806.jar | 961 | sgbyh | - | 1.0.0 | MIDP-1.0 | - | 11 | 16 | 600 | 32935888 | 0 | java/lang/ArrayIndexOutOfBoundsE | 运行正常 |  |
| 时光樱花册－遗失之心_1009.jar | 1380 | 时光樱花册-遗失之心 | - | 1.0 | MIDP-1.0 | - | 33 | 16 | 600 | 332847464 | 0 | - | 运行正常 |  |
| 明星制造梦工厂_11048.jar | 220 | 明星制造梦工厂 | - | 1.2 | MIDP-1.0 | - | 8 | 16 | 600 | 170449164 | 0 | java/io/IOException | 运行正常 |  |
| 明星麻将馆美女_9760.jar | 362 | 明星麻将馆(美女真人版) | - | 1.0.0 | MIDP-1.0 | nokia-ui | 11 | 16 | 324 | 195441 | 0 | java/lang/NullPointerException | 运行正常 | exited itself (notifyDestroyed) |
| 星空恋曲-遥远的记忆(修正版)_4008.jar | 982 | STAR | - | 01.30.00 | MIDP-2.0 | - | 84 | 16 | 600 | 4361242 | 0 | - | 运行正常 |  |
| 星辰变S60V5完整版_3364.jar | 926 | 星辰变(盛大正版) | - | 1.0.0 | MIDP-1.0 | nokia-ui | 40 | 16 | 600 | 3284370874 | 0 | java/io/IOException | 运行正常 |  |
| 是男人就下100层_1882.jar | 636 | 是男人就下100层 | - | 2.01 | MIDP-1.0 | nokia-ui | 75 | 16 | 600 | 53923668 | 0 | java/io/IOException,java/lang/Nu | 运行正常 |  |
| 暗黑启示录2－黑暗王朝_888.jar | 1268 | 暗黑启示录2黑暗王朝 | - | 1.0 | MIDP-1.0 | - | 214 | 16 | 600 | 3084316994 | 0 | java/io/EOFException | 运行正常 |  |
| 有女倾城破解版_11525.jar | 258 | 有女倾城 | - | 1.0 | MIDP-1.0 | - | 9 | 16 | 0 | 44358 | 0 | - | 未呈现画面 | exited itself (notifyDestroyed) without pain |
| 机甲风暴_7888.jar | 673 | 机甲风暴 | - | 1.0 | MIDP-2.0 | nokia-ui | 41 | 16 | 600 | 146994 | 0 | - | 运行正常 |  |
| 杀戮机器-纳粹僵尸_3610.jar | 995 | 杀戮机器-纳粹僵尸 | - | 1.0 | MIDP-2.0 | m3g | 37 | 16 | 600 | 706821 | 0 | java/lang/NumberFormatException | 运行正常 |  |
| 杀鬼子-敌后武工队_2185.jar | 515 | 杀鬼子-敌后武工队 | - | 1.0.0 | MIDP-1.0 | nokia-ui | 37 | 16 | 432 | 2122185841 | 0 | java/io/IOException,java/lang/Nu | 运行正常 | exited itself (notifyDestroyed) |
| 极品飞车13之变速英文3D版_6646.jar | 754 | Need for Speed Shift | - | 7.2.18 | MIDP-2.0 | m3g | 38 | 16 | 600 | 31770982 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| 极品飞车14之热力追踪中文版_4160.jar | 1219 | 极品飞车：热血追踪 | - | 1.0.0 | MIDP-2.0 | m3g | 44 | 16 | 612 | 16059269 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| 极品飞车16：亡命狂飙_1519.jar | 1046 | Need for Speed The Run | - | 4.4.88 | MIDP-2.0 | m3g,nokia-ui | 41 | 16 | 610 | 66433260 | 0 | java/lang/IllegalArgumentExcepti | 运行正常 |  |
| 极品飞车之无间风云FX中文版_4518.jar | 1000 | 极品飞车无间风云FX | - | 1.0.0 | MIDP-2.0 | m3g | 63 | 16 | 600 | 10428513 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| 极品飞车精装版_7557.jar | 159 | 极品飞车精装版 | - | 1.0 | MIDP-1.0 | - | 16 | 16 | 600 | 3444270 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| 柯楠之密室杀人事件_3821.jar | 429 | 柯楠－密室杀人事件 | - | 1.0 | MIDP-2.0 | nokia-ui | 39 | 16 | 600 | 1470544 | 0 | javax/microedition/media/MediaEx | 运行正常 |  |
| 校园回忆_1940.jar | 358 | 校园回忆 | - | 1.0 | MIDP-1.0 | - | 16 | 16 | 600 | 110418 | 0 | - | 运行正常 |  |
| 格斗2011-拳王争霸_4253.jar | 805 | GDQWZB | - | 1.2.0 | MIDP-2.0 | - | 8 | 16 | 600 | 3859988011 | 0 | java/lang/NullPointerException | 运行正常 |  |
| 格斗三国五虎将精装版_9085.jar | 275 | 格斗三国五虎将(精装版) | - | 1.0 | MIDP-1.0 | - | 8 | 16 | 600 | 1619720 | 0 | javax/microedition/rms/InvalidRe | 运行正常 |  |
| 格斗少女_3271.jar | 709 | gdsn | - | 1.0 | MIDP-2.0 | - | 14 | 16 | 600 | 1088446 | 0 | - | 运行正常 |  |
| 格斗火柴人_3938.jar | 416 | Enter The Flip | - | 1.0 | MIDP-2.0 | - | 11 | 16 | 600 | 7835658 | 0 | - | 运行正常 |  |
| 梦幻西游_4223.jar | 1024 | 梦幻西游 | - | 1.0 | MIDP-1.0 | - | 88 | 16 | 600 | 10849567 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| 植物大战僵尸(新角色加强版)_982.jar | 421 | 植物大战僵尸 | - | 1.0 | MIDP-1.0 | - | 37 | 16 | 600 | 63623 | 0 | - | 运行正常 |  |
| 植物大战僵尸2011加强版_2912.jar | 533 | 植物大战僵尸(2011加强版) | - | 1.0 | MIDP-1.0 | - | 56 | 16 | 600 | 465457 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| 植物大战僵尸2013完美升级版_326.jar | 516 | 植物大战僵尸 | - | 1.0 | MIDP-1.0 | - | 41 | 16 | 600 | 288573 | 0 | - | 运行正常 |  |
| 植物大战僵尸BT版_650.jar | 905 | 植物大战僵尸 | - | 2.2 | MIDP-2.0 | - | 99 | 16 | 601 | 141762797 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| 植物大战僵尸欧美大波版_407.jar | 510 | 植物大战僵尸2013欧美大波版 | - | 1.0 | MIDP-1.0 | - | 34 | 16 | 600 | 852376 | 0 | - | 运行正常 |  |
| 模拟人生2_10762.jar | 273 | 模拟人生2 | - | 1.0.0 | MIDP-1.0 | - | 31 | 16 | 600 | 3725339883 | 0 | java/io/IOException,javax/microe | 运行正常 |  |
| 模拟人生2完美版_11722.jar | 417 | The Sims 2 | - | 1.8.13 | MIDP-2.0 | nokia-ui | 20 | 16 | 600 | 1341498031 | 0 | javax/microedition/rms/InvalidRe | 运行正常 |  |
| 模拟人生2宠物当家_10928.jar | 410 | 模拟人生2宠物当家 | - | 1.0 | MIDP-2.0 | - | 35 | 16 | 600 | 6415327 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| 模拟人生3_7562.jar | 559 | The Sims 3 | - | 7.5.73 | MIDP-2.0 | - | 28 | 16 | 600 | 38550179 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| 模拟人生3中文版_6162.jar | 492 | 模拟人生3(EA正版) | - | 1.0.0 | MIDP-2.0 | - | 25 | 16 | 600 | 6558748 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| 模拟人生3之野心_4936.jar | 817 | The Sims 3 Ambitions | - | 4.2.28 | MIDP-2.0 | - | 26 | 16 | 600 | 12532532 | 0 | java/io/EOFException | 运行正常 |  |
| 模拟人生3繁体bt版_6026.jar | 497 | The Sims 3 | - | 2.0.0 | MIDP-2.0 | - | 27 | 16 | 600 | 7436522 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| 模拟恋爱2_11543.jar | 291 | SimBabe2 by serviak | - | 1.4 | MIDP-2.0 | - | 2 | 16 | 600 | 9816768 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| 武林外传-明星斗地主增值版_3971.jar | 689 | 武林外传-明星斗地主(电影正版) | - | 1.0.0 | MIDP-2.0 | - | 52 | 16 | 600 | 418964 | 0 | - | 运行正常 |  |
| 武林外传_9847.jar | 400 | 武林外传-RPG巨作正版 | - | 1.0 | MIDP-2.0 | nokia-ui | 85 | 16 | 600 | 3560016 | 0 | javax/microedition/media/MediaEx | 运行正常 |  |
| 歪歪隋唐英雄传_11206.jar | 52 | waiwai | - | 1.0 | MIDP-1.0 | nokia-ui | 6 | 16 | 600 | 41788 | 0 | java/io/IOException | 运行正常 |  |
| 毁灭战士3_10598.jar | 1590 | Doom3D | - | 1.0 | MIDP-2.0 | m3g | 48 | 16 | 600 | 2130619 | 0 | java/io/IOException | 运行正常 |  |
| 比基尼美女海底影相_9643.jar | 275 | Foto Quest Bikini | - | 2.0.6 | MIDP-1.0 | nokia-ui | 14 | 16 | 600 | 9994477 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| 水浒108将-激斗篇_2196.jar | 625 | 水浒108将-激斗篇 | - | 1.0.0 | MIDP-1.0 | - | 33 | 16 | 600 | 590937210 | 0 | java/io/IOException,java/lang/Nu | 运行正常 |  |
| 法拉利GT3－世界赛道BT版_776.jar | 2881 | Ferrari GT 3 - World Track | - | 1.1.2 | MIDP-2.0 | nokia-ui | 22 | 16 | 600 | 2761123713 | 0 | java/lang/NullPointerException | 运行正常 |  |
| 波斯王子之时之沙中文版_11315.jar | 60 | 波斯王子-时之沙 | - | 1.0.1 | MIDP-1.0 | nokia-ui | 6 | 16 | 600 | 1215277903 | 0 | - | 运行正常 |  |
| 波斯王子之武者之心_8424.jar | 202 | Prince Of Persia | - | 1.0.2 | MIDP-2.0 | - | 5 | 16 | 600 | 2667711111 | 0 | - | 运行正常 |  |
| 波斯王子经典版_9715.jar | 326 | Prince of Persia Classic | - | 1.0.3 | MIDP-2.0 | - | 9 | 16 | 600 | 7106409487 | 0 | java/lang/NullPointerException | 运行正常 |  |
| 浴火银河2完整版_7689.jar | 1639 | Galaxy on Fire 2 | - | 1.0.3 | MIDP-2.0 | m3g | 154 | 16 | 600 | 399003726 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| 浴火银河2汉化版_6950.jar | 1680 | Galaxy on Fire 2 | - | 1.0.3 | MIDP-2.0 | m3g | 155 | 16 | 600 | 478754628 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| 浴火银河2瓦尔基里(汉化)_3067.jar | 9771 | Galaxy on Fire 2 | - | 1.0.3 | MIDP-2.0 | m3g | 155 | 16 | 600 | 450602065 | 0 | java/lang/IllegalArgumentExcepti | 运行正常 |  |
| 浴火银河2瓦尔基里_3093.jar | 9746 | Galaxy on Fire 2 | - | 1.0.3 | MIDP-2.0 | m3g | 154 | 16 | 600 | 392467684 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| 浴火银河_10189.jar | 510 | 3D Galaxy on Fire | - | 1.0.3 | MIDP-2.0 | mascot | 101 | 16 | 600 | 56995116 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| 浴火银河_3621.jar | 510 | 3D Galaxy on Fire | - | 1.0.3 | MIDP-2.0 | mascot | 101 | 16 | 600 | 60428681 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| 海绵宝宝卡丁车_4684.jar | 651 | HMBBKDC | - | 0.1.0 | MIDP-2.0 | - | 9 | 16 | 600 | 3946116712 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| 涂鸦地带_模拟可玩_12472.jar | 62 | 涂鸦地带 | - | 1.0 | MIDP-2.0 | - | 22 | 16 | 600 | 874372 | 0 | java/io/IOException,javax/microe | 运行正常 |  |
| 涂鸦跳跃中文豪华版(汉化)_2524.jar | 1424 | 涂鸦跳跃 | - | 1.8.2 | MIDP-2.0 | - | 13 | 16 | 600 | 561961027 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| 灌篮高手-爆发!樱木_7947.jar | 327 | 灌篮高手-爆发！樱木 | - | 1.0.0 | MIDP-2.0 | - | 44 | 16 | 600 | 944763 | 0 | java/lang/ArithmeticException,ja | 运行正常 |  |
| 火影忍者-终极格斗_6604.jar | 673 | 火影忍者-精品格斗2010 | - | 1.0.0 | MIDP-1.0 | nokia-ui | 41 | 16 | 600 | 2925386 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| 火影忍者_11531.jar | 335 | Naruto | - | Mod | MIDP-1.0 | nokia-ui | 9 | 16 | 600 | 723405 | 0 | javax/microedition/rms/InvalidRe | 运行正常 |  |
| 火影忍者格斗2010_4601.jar | 652 | 火影忍者-精品格斗2010 | - | 1.0.0 | MIDP-1.0 | nokia-ui | 39 | 16 | 600 | 6495481 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| 火影忍者汉化版_10735.jar | 261 | Ninja Fighter (RoS) | - | 1.0 | MIDP-1.0 | nokia-ui | 22 | 16 | 600 | 14257616 | 0 | java/io/EOFException,java/io/IOE | 运行正常 |  |
| 火焰-纹章之谜_12128.jar | 316 | 火焰-纹章之谜 | - | 1.9 | MIDP-1.0 | nokia-ui | 17 | 16 | 600 | 910500 | 0 | java/io/IOException,java/lang/Nu | 运行正常 |  |
| 火焰-纹章传说第一章_12129.jar | 365 | 火焰-纹章传说第一章 | - | 1.6 | MIDP-1.0 | nokia-ui | 18 | 16 | 600 | 845195 | 0 | java/io/IOException,java/lang/Nu | 运行正常 |  |
| 火焰-纹章传说第三章_12130.jar | 354 | 火焰-纹章传说第三章 | - | 1.8 | MIDP-1.0 | nokia-ui | 18 | 16 | 600 | 1133578 | 0 | java/io/IOException,java/lang/Nu | 运行正常 |  |
| 火焰-纹章传说第二章_12131.jar | 343 | 火焰-纹章传说第二章 | - | 1.8 | MIDP-1.0 | nokia-ui | 18 | 16 | 600 | 974942 | 0 | java/io/IOException,java/lang/Nu | 运行正常 |  |
| 火焰纹章勇者之剑_10337.jar | 163 | 圣火纹章-勇者之剑 | - | 1.0 | MIDP-1.0 | - | 16 | 16 | 600 | 336695 | 0 | - | 运行正常 |  |
| 灵异村庄_11169.jar | 94 | The Village | - | 1.2 | MIDP-1.0 | nokia-ui | 19 | 16 | 600 | 2058518 | 0 | java/io/IOException,javax/microe | 运行正常 |  |
| 炸弹人_10311.jar | 302 | Bomberman | - | 1.0.0 | MIDP-1.0 | nokia-ui | 12 | 16 | 600 | 22009850 | 0 | javax/microedition/rms/InvalidRe | 运行正常 |  |
| 热血沙滩9球赛_894.jar | 472 | 热血沙滩9球赛 | - | 1.0.0 | MIDP-2.0 | - | 27 | 16 | 600 | 7674682 | 0 | java/lang/ArrayIndexOutOfBoundsE | 运行正常 |  |
| 爆笑三国斗地主_10552.jar | 312 | 爆笑三国斗地主 | - | 1.0 | MIDP-1.0 | nokia-ui | 8 | 16 | 600 | 350927 | 0 | - | 运行正常 |  |
| 狂野飙车4-gameloft(中文版)_2603.jar | 647 | 狂野飙车4 | - | 1.0.0 | MIDP-2.0 | - | 22 | 16 | 600 | 192774779 | 0 | - | 运行正常 |  |
| 狂野飙车4-亡命都市_2875.jar | 616 | 狂野飙车-亡命都市 | - | 1.0.0 | MIDP-2.0 | - | 13 | 16 | 600 | 211753125 | 0 | - | 运行正常 |  |
| 狂野飙车4_6959.jar | 596 | 狂野飙车4-gameloft | - | 1.0.0 | MIDP-2.0 | - | 12 | 16 | 600 | 68667649 | 0 | - | 运行正常 |  |
| 猿人时代BT版_4519.jar | 489 | 猿人时代 | - | 1.0.0 | MIDP-2.0 | - | 49 | 16 | 600 | 10579826 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| 生化危机-险境求生_3290.jar | 914 | 生化危机 | - | 1.0.0 | MIDP-1.0 | nokia-ui | 53 | 16 | 600 | 3273832195 | 0 | - | 运行正常 |  |
| 生化危机3D(CAPCOM经典)_3671.jar | 983 | shwjd | - | 1.0 | MIDP-2.0 | m3g | 21 | 16 | 601 | 625355 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| 生化危机5核变_866.jar | 1181 | 生化危机5核变 | - | 1.0 | MIDP-1.0 | nokia-ui | 61 | 16 | 600 | 698850 | 0 | - | 运行正常 |  |
| 生化危机之恶化_8448.jar | 372 | RED http://www.universosymbi | - | 1.0.0 | MIDP-2.0 | - | 10 | 16 | 601 | 2911611 | 0 | java/lang/NumberFormatException | 运行正常 |  |
| 生化惊悚2死亡寂静中文版_2607.jar | 1144 | shjswjj | - | 0.0.5 | MIDP-2.0 | - | 26 | 16 | 600 | 255146334 | 0 | java/lang/ArrayIndexOutOfBoundsE | 运行正常 |  |
| 电子宠物蛋_11972.jar | 95 | Tamagotchi 240x320 | - | 1.0.96 | MIDP-1.0 | nokia-ui | 8 | 16 | 600 | 1899913 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| 电玩美女麻将_7260.jar | 439 | 电玩美女麻将 | - | 1.0.0 | MIDP-1.0 | - | 9 | 16 | 600 | 2350199 | 0 | javax/microedition/rms/InvalidRe | 运行正常 |  |
| 男摄影师_11003.jar | 250 | 男摄影师 | - | 1.0.0 | MIDP-1.0 | - | 19 | 16 | 600 | 8760415 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| 疯狂企鹅中文版_10228.jar | 140 | 功夫企鹅 | - | 1.0.0 | MIDP-2.0 | - | 13 | 16 | 600 | 12931222 | 0 | java/io/EOFException,javax/micro | 运行正常 |  |
| 疯狂农场2_2375.jar | 551 | Farm Frenzy 2 | - | 1.0.3 | MIDP-2.0 | - | 42 | 16 | 600 | 11211738 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| 疯狂农场汉化版_5998.jar | 400 | Farm Frenzy | - | 2.0.0 | MIDP-2.0 | - | 33 | 16 | 600 | 3818442 | 0 | java/io/EOFException,java/io/IOE | 运行正常 |  |
| 疯狂出租车中文版_721.jar | 491 | 疯狂出租车 | - | 1.0 | MIDP-1.0 | - | 49 | 16 | 600 | 181786 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| 疯狂战机_2013.jar | 219 | Srcombat20110928 | - | 1.0.0 | MIDP-2.0 | - | 65 | 16 | 600 | 2143750774 | 0 | java/io/IOException | 运行正常 |  |
| 百战天虫2010中文版_5380.jar | 560 | PC原版-百战天虫2010-EA | - | 1.0.0 | MIDP-2.0 | - | 25 | 16 | 600 | 52247354 | 0 | java/lang/NullPointerException | 运行正常 |  |
| 百鬼商店破解版_10526.jar | 382 | BAI_GUI_SHANG_DIAN | - | 1.0 | MIDP-2.0 | - | 4 | 16 | 600 | 1074172 | 0 | - | 运行正常 | runs without input; the generic key script m |
| 真三国无双横扫千军_12449.jar | 270 | 横扫千军.宝软网baoruan.com免费分享 | - | 1.0 | MIDP-2.0 | - | 14 | 16 | 600 | 1011016 | 0 | - | 运行正常 |  |
| 真人快打3中文版_3252.jar | 299 | 真人快打3 | - | 1.0.0 | MIDP-2.0 | - | 50 | 16 | 600 | 15056594 | 0 | java/lang/ClassCastException,jav | 运行正常 |  |
| 真幻想三国志完整版_12469.jar | 877 | 真幻想三国志 | - | 1.0 | MIDP-1.0 | nokia-ui | 138 | 16 | 600 | 2528816738 | 0 | - | 运行正常 |  |
| 真武三国BT版_3953.jar | 1013 | 真武三国 | - | 1.0 | MIDP-2.0 | - | 23 | 16 | 600 | 7074305 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| 真武三国_3962.jar | 1018 | 真武三国 | - | 1.0 | MIDP-2.0 | - | 22 | 16 | 600 | 7074393 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| 真西游记_6848.jar | 738 | 真西游记(华娱) | - | 1.0 | MIDP-1.0 | - | 18 | 16 | 600 | 1666295 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| 神庙逃亡2（Temple_18.jar | 680 | Run2 | - | 2.0.0 | MIDP-2.0 | - | 4 | 16 | 600 | 709721855 | 0 | - | 运行正常 |  |
| 科比职业篮球2008中文版_11136.jar | 520 | 科比大灌篮(gameloft正版) | - | 1.02 | MIDP-1.0 | - | 10 | 16 | 600 | 5150481884 | 0 | java/lang/ArrayIndexOutOfBoundsE | 运行正常 |  |
| 空战1944_10321.jar | 309 | Air Strike 1944 | - | 1.0.0 | MIDP-2.0 | - | 6 | 16 | 600 | 108970733 | 0 | - | 运行正常 |  |
| 穿越火线-韩国正版_6874.jar | 480 | chuanyuehuoxian | - | 1.0.0 | MIDP-1.0 | - | 21 | 16 | 600 | 3939579030 | 0 | - | 运行正常 | runs without input; the generic key script m |
| 竞技者之极限酷跑_5431.jar | 250 | 竞技者之极限酷跑 | - | 1.0.4 | MIDP-2.0 | - | 7 | 16 | 600 | 699293842 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| 精品美女斗地主2013_104.jar | 1203 | 精品美女斗地主 | - | 1.0 | MIDP-1.0 | nokia-ui | 61 | 16 | 600 | 784417357 | 0 | java/io/IOException,java/lang/Nu | 运行正常 |  |
| 红色警戒3_7037.jar | 784 | C&C: Red Alert Mobile by bli | - | 4.15.48 | MIDP-2.0 | - | 33 | 16 | 600 | 4505359 | 0 | - | 运行正常 |  |
| 纯爱物语3之梦想之翼_5112.jar | 978 | L3D | - | 1.30.0 | MIDP-2.0 | - | 82 | 16 | 600 | 6222329 | 0 | - | 运行正常 |  |
| 纯爱物语之湛蓝回忆简体版_5164.jar | 1007 | LoveMemory | - | 1.0 | MIDP-1.0 | - | 66 | 16 | 600 | 121444 | 0 | java/lang/NullPointerException | 运行正常 |  |
| 纽约人生_1339.jar | 879 | My Life in New York | - | 1.0.6 | MIDP-2.0 | - | 12 | 16 | 600 | 122006469 | 0 | - | 运行正常 |  |
| 细胞分裂4混沌法则中文版_11671.jar | 101 | 细胞分裂3-混沌法则 | - | 1.02 | MIDP-1.0 | nokia-ui | 4 | 16 | 600 | 2897736718 | 0 | - | 运行正常 |  |
| 终极雷电_4125.jar | 293 | 终极雷电 | - | 1.0.0 | MIDP-1.0 | - | 57 | 16 | 600 | 740217 | 0 | - | 运行正常 |  |
| 经典推箱子_1070.jar | 247 | 纸盒人 | - | 1.0.0 | MIDP-1.0 | - | 34 | 16 | 600 | 147822 | 0 | java/io/IOException | 运行正常 |  |
| 绝密反击破解版_8377.jar | 174 | 绝密反击 | - | 1.0 | MIDP-1.0 | nokia-ui | 39 | 16 | 600 | 2980128 | 0 | - | 运行正常 |  |
| 网球大满贯_11950.jar | 143 | 网球大满贯 | - | 1.0 | MIDP-1.0 | - | 11 | 16 | 600 | 228688391 | 0 | java/io/IOException,javax/microe | 运行正常 |  |
| 美女台球_9377.jar | 205 | Billard Girls | - | 1.0.11 | MIDP-1.0 | - | 40 | 16 | 600 | 17677735 | 0 | java/lang/NullPointerException | 运行正常 |  |
| 美女斗地主(真人语音版)_1609.jar | 1089 | 写真美女斗地主(真人语音) | - | 1.0.0 | MIDP-1.0 | - | 29 | 16 | 600 | 2011449 | 0 | java/lang/NullPointerException | 运行正常 |  |
| 美女斗地主_1088.jar | 717 | 美女斗地主 | - | 1.0 | MIDP-2.0 | nokia-ui | 14 | 16 | 600 | 13746861 | 0 | java/lang/NullPointerException | 运行正常 |  |
| 美女滑雪胜地_2.jar | 290 | 美女滑雪胜地 | - | 1.0.0 | MIDP-2.0 | - | 23 | 16 | 600 | 13933331 | 0 | - | 运行正常 |  |
| 美少女梦幻工厂_11443.jar | 181 | 美少女梦幻工厂 | - | 1.0 | MIDP-1.0 | nokia-ui | 4 | 16 | 600 | 101455 | 0 | javax/microedition/rms/InvalidRe | 运行正常 |  |
| 美男狂想曲_3876.jar | 856 | Hotguys Rhapsody | - | 1.0.0 | MIDP-1.0 | - | 11 | 16 | 600 | 214552 | 0 | java/io/IOException,java/lang/Nu | 运行正常 | runs without input; the generic key script m |
| 聊斋之倩女幽魂_11185.jar | 300 | 聊斋之倩女幽魂 | - | 1.0 | MIDP-2.0 | nokia-ui | 6 | 16 | 600 | 1999385033 | 0 | - | 运行正常 |  |
| 航海世纪2_10583.jar | 157 | Tradewinds 2 | - | 1.1.4 | MIDP-2.0 | - | 4 | 16 | 600 | 1466739279 | 0 | java/lang/IllegalArgumentExcepti | 运行正常 |  |
| 色戒之虚拟人生_7230.jar | 584 | 色戒之虚拟人生 | - | 1.0.0 | MIDP-1.0 | - | 19 | 16 | 600 | 2000726 | 0 | - | 运行正常 |  |
| 花样少年恋爱学院_5977.jar | 764 | love_school | - | 1.30.02 | MIDP-2.0 | - | 45 | 16 | 600 | 35265697 | 0 | java/lang/NullPointerException | 运行正常 |  |
| 花样少年恋爱学院特别版_5527.jar | 776 | love-school-sp | - | 1.30.11 | MIDP-2.0 | - | 45 | 16 | 600 | 27847405 | 0 | java/lang/NullPointerException | 运行正常 |  |
| 苍穹默示录完美运行版_4035.jar | 1790 | 苍穹默示录 | - | 1.0.0 | MIDP-1.0 | - | 53 | 16 | 600 | 4915244130 | 0 | java/io/IOException,java/lang/Cl | 运行正常 |  |
| 荒岛余生(GLU正版)_12478.jar | 451 | 荒岛余生(GLU正版) | - | 1.0.1 | MIDP-1.0 | - | 9 | 16 | 600 | 6490113 | 0 | java/lang/NullPointerException | 运行正常 |  |
| 荒岛余生2时空探秘_6794.jar | 486 | 荒岛余生2时空探秘 | - | 1.0.1 | MIDP-2.0 | - | 31 | 16 | 600 | 5517220 | 0 | javax/microedition/rms/InvalidRe | 运行正常 |  |
| 荣誉勋章之空降神兵改良版_2993.jar | 559 | Medal Of Honor Airborne Elit | - | 4.18.80 | MIDP-2.0 | - | 32 | 16 | 600 | 23655959 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| 蓝牙真3D格斗_3424.jar | 602 | 真3D格斗 | - | 1.0 | MIDP-2.0 | m3g,nokia-ui | 26 | 16 | 600 | 7385271 | 0 | javax/bluetooth/BluetoothStateEx | 运行正常 |  |
| 蓝白五子棋_9790.jar | 104 | Gomoku by drAdeLante | - | 1.0.0 | MIDP-2.0 | - | 15 | 16 | 600 | 3690958719 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| 蛋蛋历险记_11460.jar | 162 | Bird ETTY | - | 1.4.6 | MIDP-2.0 | - | 2 | 16 | 600 | 734312649 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| 血战三国之吕布貂蝉_4012.jar | 347 | 血战三国-吕布VS貂蝉 | - | 1.0.0 | MIDP-1.0 | - | 10 | 16 | 600 | 54089 | 0 | javax/microedition/rms/InvalidRe | 运行正常 |  |
| 血战轩辕之魔神蚩尤_11172.jar | 198 | 血战轩辕之魔神蚩尤 | - | 1.0 | MIDP-1.0 | nokia-ui | 12 | 16 | 600 | 1340069 | 0 | java/lang/IllegalArgumentExcepti | 运行正常 |  |
| 街头反恐之格斗女王_822.jar | 285 | 街头反恐之格斗女王 | - | 1.0.0 | MIDP-2.0 | - | 10 | 16 | 600 | 2772973 | 0 | - | 运行正常 |  |
| 街头霸王-街机经典_2664.jar | 563 | 街头霸王 | - | 1.0.0 | MIDP-1.0 | - | 12 | 16 | 603 | 2063404 | 0 | java/lang/IllegalArgumentExcepti | 运行正常 |  |
| 街头霸王2_11388.jar | 493 | StreetFighter2 | - | 1.2.4 | MIDP-1.0 | - | 5 | 16 | 600 | 24803187 | 0 | java/lang/NullPointerException | 运行正常 |  |
| 街头霸王2冠军典藏版_1812.jar | 730 | jtbw | - | 1.0.0 | MIDP-2.0 | - | 9 | 16 | 600 | 2844481 | 0 | - | 运行正常 |  |
| 街霸2012(加强版)_2302.jar | 1117 | 街霸2012加强版 | - | 1.0 | MIDP-1.0 | - | 40 | 16 | 600 | 126744461 | 0 | java/io/EOFException,java/lang/C | 运行正常 |  |
| 西伯利亚大反击_11466.jar | 205 | 西伯利亚大反击 | - | 1.0 | MIDP-1.0 | - | 2 | 16 | 600 | 2103212282 | 0 | - | 运行正常 |  |
| 诈金花_6358.jar | 557 | 诈金花 | - | 1.0 | MIDP-1.0 | nokia-ui | 24 | 16 | 600 | 765705 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| 诛神－斗破苍穹_1402.jar | 1190 | 诛神-斗破苍穹 | - | 1.0.0 | MIDP-2.0 | - | 62 | 16 | 600 | 373094687 | 0 | java/io/EOFException,java/lang/A | 运行正常 |  |
| 诸神战纪-堕落天使路西法_5070.jar | 1324 | 诸神战纪-堕落天使路西法 | - | 1.0 | MIDP-1.0 | - | 28 | 16 | 600 | 5580858 | 0 | - | 运行正常 |  |
| 贪吃蛇经典版_4515.jar | 124 | Snake | - | 1.3 | MIDP-2.0 | - | 68 | 16 | 470 | 9742 | 0 | - | 运行正常 | exited itself (notifyDestroyed) |
| 赤色要塞－FC移植版_5285.jar | 282 | 赤色要塞 | - | 1.0.0 | MIDP-1.0 | nokia-ui | 43 | 16 | 600 | 2755550 | 0 | java/io/EOFException,javax/micro | 运行正常 |  |
| 超时空帝国机甲风暴_12065.jar | 542 | 超时空帝国机甲风暴 | - | 1.0 | MIDP-2.0 | nokia-ui | 20 | 16 | 600 | 278988 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| 超级坦克大战2汉化版_531.jar | 655 | (发烧)超级坦克大战II(增强版) | - | 1.0.1 | MIDP-1.0 | nokia-ui | 24 | 16 | 600 | 3218309521 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| 超级酒吧女生_4728.jar | 569 | 超级酒吧女生 | - | 1.0.0 | MIDP-2.0 | - | 10 | 16 | 600 | 3908330022 | 0 | javax/microedition/rms/InvalidRe | 运行正常 |  |
| 超级马里奥_10283.jar | 534 | 超级马里奥 | - | 1.0.0 | MIDP-2.0 | - | 11 | 16 | 600 | 3562335675 | 0 | - | 运行正常 |  |
| 足球经理2010汉化版_4756.jar | 517 | CM-2010 | - | 1.2.1 | MIDP-2.0 | nokia-ui | 21 | 16 | 600 | 5817822 | 0 | - | 运行正常 |  |
| 跑跑卡丁车-经典中秋版_2117.jar | 623 | 跑跑卡丁车 | - | 1.0.0 | MIDP-2.0 | - | 21 | 16 | 690 | 570173 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| 蹦球传说_7801.jar | 375 | Bounce Tales | - | 2.0.3 | MIDP-2.0 | nokia-ui | 21 | 16 | 91 | 56862 | 0 | - | 标题自行退出 | exited itself (notifyDestroyed) after 91 fra |
| 蹦球传说美化版_2959.jar | 416 | Bounce Tales | - | 2.0.3 | MIDP-2.0 | nokia-ui | 19 | 16 | 600 | 71798598 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| 轩辕神剑_2419.jar | 1324 | 轩辕神剑 | - | 1.0 | MIDP-2.0 | - | 71 | 16 | 600 | 3424846 | 0 | java/io/EOFException,java/lang/N | 运行正常 |  |
| 迈阿密之夜_9566.jar | 315 | MiamiNights | - | 1.0.2 | MIDP-2.0 | - | 6 | 16 | 600 | 178335960 | 0 | javax/microedition/rms/InvalidRe | 运行正常 |  |
| 远古帝国2汉化版_10357.jar | 168 | Ancient Empires II | - | 1.0.2 | MIDP-1.0 | - | 10 | 16 | 600 | 479083 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| 速度与激情5中文版_3043.jar | 380 | Fast Five The Movie Game | - | 1.0.2 | MIDP-2.0 | nokia-ui | 25 | 16 | 600 | 3870807278 | 0 | - | 运行正常 |  |
| 速度与激情6_9.jar | 1917 | Fast & Furious 6 | - | 1.1.2 | MIDP-2.0 | - | 19 | 16 | 600 | 55129735 | 0 | java/lang/NullPointerException | 运行正常 |  |
| 都市摩天楼_6192.jar | 160 | Tower Bloxx(TM) | - | 1.2.4 | MIDP-1.0 | m3g,nokia-ui | 16 | 16 | 600 | 3950276 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| 都市摩天楼中文版_3219.jar | 1026 | 都市摩天楼 | - | 1.0.0 | MIDP-2.0 | nokia-ui | 46 | 16 | 479 | 109901608 | 0 | java/io/EOFException,javax/micro | 运行正常 | exited itself (notifyDestroyed) |
| 都市赛车3-暴力街头3D版_10199.jar | 285 | Asphalt 3 3D Street Rules | - | 1.0.2 | MIDP-2.0 | - | 5 | 16 | 600 | 4034044549 | 0 | javax/microedition/rms/InvalidRe | 运行正常 |  |
| 都市赛车3_10192.jar | 286 | Asphalt3: Street Rules | - | 1.0.4 | MIDP-2.0 | - | 5 | 16 | 600 | 3191419021 | 0 | javax/microedition/rms/InvalidRe | 运行正常 |  |
| 都市赛车6火线追击_2230.jar | 940 | Asphalt 6 Adrenaline | - | 1.0.0 | MIDP-2.0 | - | 21 | 16 | 601 | 20674968 | 0 | - | 运行正常 |  |
| 都市赛车_11517.jar | 223 | Asphalt | - | 1.0.7 | MIDP-2.0 | - | 5 | 16 | 600 | 1935256293 | 0 | javax/microedition/rms/InvalidRe | 运行正常 |  |
| 野人岛2宠物精灵_7061.jar | 534 | 宝软网baoruan.com免费分享 | - | 1.0 | MIDP-1.0 | nokia-ui | 24 | 16 | 600 | 3029872 | 0 | - | 运行正常 |  |
| 钻石狂潮_1335.jar | 293 | Diamond Rush | - | 1.1.7 | MIDP-2.0 | - | 12 | 16 | 600 | 4006089 | 0 | javax/microedition/rms/InvalidRe | 运行正常 |  |
| 铠甲勇士2-刑天_2895.jar | 1120 | 铠甲勇士2刑天 | - | 1.0.0 | MIDP-1.0 | - | 37 | 16 | 600 | 2509618194 | 0 | javax/microedition/rms/InvalidRe | 运行正常 |  |
| 闪电战机－雷霆出击_118.jar | 1388 | 闪电战机-雷霆出击 | - | 2.01 | MIDP-1.0 | - | 129 | 16 | 600 | 189608776 | 0 | java/io/IOException | 运行正常 |  |
| 阿尔卑斯牧场物语_7043.jar | 824 | 阿尔卑斯牧场物 | - | 1.0 | MIDP-1.0 | nokia-ui | 8 | 16 | 600 | 4067790 | 0 | java/lang/NullPointerException | 运行正常 |  |
| 雷欧历险记_11451.jar | 175 | FilaoFriedChicken | - | 01.03.08 | MIDP-1.0 | - | 11 | 16 | 600 | 15294287 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| 雷电2012末日浩劫增值版_3029.jar | 879 | 雷电2012末日浩劫 | - | 1.0.0 | MIDP-1.0 | - | 40 | 16 | 600 | 765881 | 0 | - | 运行正常 |  |
| 雷电4_39.jar | 545 | 雷电4 | - | 1.0 | MIDP-2.0 | - | 26 | 16 | 600 | 548007 | 0 | java/io/IOException | 运行正常 |  |
| 雷电战机(精装升级版)_3138.jar | 876 | 雷电战机(精装升级版) | - | 1.0.0 | MIDP-1.0 | - | 48 | 16 | 600 | 4288403825 | 0 | java/io/IOException,java/lang/Cl | 运行正常 |  |
| 雷电战机精装升级BT版_3110.jar | 869 | 雷电战机(精装升级版) | - | 1.0.0 | MIDP-1.0 | - | 48 | 16 | 600 | 4057243728 | 0 | java/io/IOException,java/lang/Cl | 运行正常 |  |
| 雷霆战机2012BT版_3101.jar | 851 | 雷霆战机2012 | - | 1.0.0 | MIDP-1.0 | - | 30 | 16 | 600 | 308090 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| 鬼吹灯(网络火热)_7647.jar | 1007 | qcd | - | 1.0.0 | MIDP-1.0 | nokia-ui | 21 | 16 | 600 | 3361432780 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| 鬼泣3D中文版_3376.jar | 1118 | 鬼泣(CAPCOM正版) | - | 1.0.0 | MIDP-2.1 | mascot | 26 | 16 | 600 | 6154425 | 0 | java/io/EOFException | 运行正常 |  |
| 魂斗罗3代-完全版_3784.jar | 610 | 魂斗罗3代 | - | 1.0 | MIDP-1.0 | nokia-ui | 22 | 16 | 600 | 957322 | 0 | java/lang/NullPointerException | 运行正常 |  |
| 魂斗罗4中文版_2226.jar | 530 | 魂斗罗4 | - | 1.0.0 | MIDP-2.0 | nokia-ui | 56 | 16 | 600 | 1970578 | 0 | javax/microedition/rms/InvalidRe | 运行正常 |  |
| 魂斗罗V铁血兵团_119.jar | 990 | 魂斗罗V铁血兵团 | - | 1.0.0 | MIDP-1.0 | - | 68 | 16 | 600 | 4234850 | 0 | - | 运行正常 |  |
| 魂斗罗_5652.jar | 896 | 魂斗罗 | - | 1.0.0 | MIDP-2.0 | nokia-ui | 54 | 16 | 600 | 10700437990 | 0 | java/lang/NullPointerException | 运行正常 |  |
| 魔兽争霸_10622.jar | 59 | MSZB | - | 1.0 | MIDP-2.0 | nokia-ui | 22 | 16 | 600 | 2415541475 | 0 | - | 运行正常 |  |
| 魔兽塔防_12480.jar | 243 | 魔兽塔防 | - | 1.0 | MIDP-1.0 | - | 18 | 16 | 600 | 816745 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| 魔塔_8052.jar | 86 | 魔塔 | - | 1.00 | MIDP-1.0 | nokia-ui | 21 | 16 | 600 | 83289 | 0 | - | 运行正常 |  |
| 魔塔－英雄联盟_582.jar | 1451 | 魔塔-英雄联盟 | - | 3.11 | MIDP-1.0 | nokia-ui | 63 | 16 | 600 | 57961774 | 0 | java/lang/NullPointerException | 运行正常 |  |
| 魔法少女_10523.jar | 204 | 魔法少女 | - | 1.0 | MIDP-1.0 | - | 5 | 16 | 600 | 234318 | 0 | - | 运行正常 |  |
| 鲁宾逊漂流记汉化BT版_4677.jar | 279 | - | - | - | - | - | 0 | 16 | 0 | 0 | 0 | - | 非可用包 | not a zip file |
| 鲁宾逊漂流记汉化版_702.jar | 367 | 鲁宾逊漂流记-海难生存 | - | 1.0.0 | MIDP-2.0 | - | 34 | 16 | 600 | 6712931 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| 麻将三国_5157.jar | 732 | MJSG | - | 1.0 | MIDP-2.0 | nokia-ui | 38 | 16 | 600 | 630213 | 0 | java/io/EOFException,java/io/IOE | 运行正常 |  |
| 麻将无双之乱世群英_10298.jar | 173 | 麻将无双之乱世群英 | - | 1.0 | MIDP-1.0 | nokia-ui | 2 | 16 | 600 | 4277218409 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| 麻将象棋五子棋3合1_12014.jar | 311 | 麻将象棋五子棋3合1 | - | 1.0 | MIDP-1.0 | - | 8 | 16 | 600 | 2898487 | 0 | - | 运行正常 |  |
| 黑暗破坏神II_9009.jar | 84 | 黑暗破坏神II | - | 1.0 | MIDP-2.0 | - | 10 | 16 | 600 | 2950224399 | 0 | - | 运行正常 |  |
| 龙之谷之天空战记_5383.jar | 885 | 龙之谷之天空战记 | - | 1.0.0 | MIDP-2.0 | - | 60 | 16 | 600 | 4233307 | 0 | - | 运行正常 |  |
| 龙火2加强版_12437.jar | 117 | 龙火2加强版 | - | 1.0 | MIDP-1.0 | - | 22 | 16 | 600 | 1252629 | 0 | - | 运行正常 |  |
| 龙珠Z传说_9583.jar | 319 | 龙珠Z传说 | - | 1.0 | MIDP-1.0 | - | 8 | 16 | 321 | 333629 | 0 | - | 运行正常 | exited itself (notifyDestroyed) |
| 龙鸟战机_9762.jar | 345 | E-Brid | - | 1.0 | MIDP-1.0 | - | 13 | 16 | 600 | 232572548 | 0 | - | 运行正常 |  |
