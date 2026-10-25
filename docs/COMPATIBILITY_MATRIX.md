# Compatibility matrix

One row per JAR in `tests/games` (961 titles), from one headless replay per title
with the same recipe, so neighbouring rows are comparable:

* port `headless`, screen `240x320`, `--fps 30`, `--run-frames 600`;
* heap `16 MB`, retried at `32 MB` only after an out-of-memory;
* one shared input script (fire, both soft keys, two screen taps, four arrows,
  digits, `*`), a fresh RMS directory per title;
* `运行正常` = exit 0, at least half the frames painted, nothing uncaught.
* `未呈现画面` = exit 0, ran to the end on its own, never painted a frame (the run loop had nothing to show - a quiet title, not a crash).

Corpus provenance and size are in `docs/CORPUS.md`; regenerate this table
with `tools/make_compat_matrix.py <rows dir>` after a new sweep.

Built from `59befc9`, 2026-10-25.

| verdict | titles |
| --- | --- |
| 运行正常 | 957 |
| 未呈现画面 | 2 |
| 非可用包 | 1 |
| 标题自行退出 | 1 |

| 3D API used | titles |
| --- | --- |
| - | 818 |
| nokia-ui | 114 |
| m3g | 21 |
| m3g,nokia-ui | 5 |
| mascot | 3 |

| title | size KB | MIDlet name | vendor | version | profile | 3D | classes | heap MB | frames | bytecodes | uncaught | exceptions | verdict | note |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| (新)三国之关云长战曹贼_2550.jar | 934 | (新)三国之关云长战曹贼 | Forcegen | 1.0.0 | MIDP-1.0 | - | 21 | 16 | 600 | 64338 | 0 | - | 运行正常 |  |
| 1942空战_10403.jar | 582 | 1942AirCombat supplied by ne | Lemon Quest supplied | 1.0.17 | MIDP-2.0 | - | 14 | 16 | 600 | 2028382 | 0 | java/lang/ArrayIndexOutOfBoundsE | 运行正常 |  |
| 1945空战英雄_6105.jar | 588 | ACH1945 Lite | Uwan | 1.0.0 | MIDP-2.0 | nokia-ui | 32 | 16 | 600 | 4441180 | 0 | - | 运行正常 | needs --device nokia6230 |
| 3D台球之王_5014.jar | 314 | 3D台球之王(EA正版) | 丰尚佳诚 | 1.0.0 | MIDP-2.0 | - | 23 | 16 | 600 | 130087 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| 3D坦克对战1944_7139.jar | 345 | Tank Ace | RESETgame | 1.03 | MIDP-2.0 | - | 32 | 16 | 600 | 10868601 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| 3D城市桌球_8299.jar | 262 | Biliardo 3D Urban Tour | I-play | 1.5.9 | MIDP-1.0 | m3g | 4 | 16 | 600 | 162260 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| 3D孤胆枪手2010_4634.jar | 985 | Alien Shooter 3D | NET Lizard | 2.0 | MIDP-2.0 | nokia-ui | 19 | 16 | 600 | 179208943 | 0 | java/io/IOException,java/lang/Ar | 运行正常 |  |
| 3D拉力赛-进化汉化版_6366.jar | 347 | 3DRallyEvolution | DDJ | 1.0 | MIDP-2.0 | m3g | 12 | 16 | 600 | 439402 | 0 | javax/microedition/rms/InvalidRe | 运行正常 |  |
| 3D摩托赛车_9287.jar | 335 | 3DMotoRacing by Racer_Team | Falcon Mobile Inc. / | 1.0 | MIDP-2.0 | m3g | 12 | 16 | 600 | 438985 | 0 | javax/microedition/rms/InvalidRe | 运行正常 |  |
| 3D武装直升机2_archive.jar | 80 | Attack Chopper 2 | Adam Schmelzle | 1.01 | MIDP-2.0 | - | 16 | 16 | 600 | 606 | 0 | java/lang/NullPointerException | 运行正常 | runs to the last frame; the screen itself st |
| 3D滚球_11586.jar | 69 | 3D QBlast | microjocs | 1.0.0 | MIDP-1.0 | nokia-ui | 19 | 16 | 600 | 139809 | 0 | - | 运行正常 |  |
| 3D激爆拳击_8805.jar | 1024 | K.O. Legends | Gameloft SA | 1.0.8 | MIDP-2.0 | m3g,nokia-ui | 23 | 16 | 600 | 219563106 | 0 | java/lang/NullPointerException | 运行正常 |  |
| 3D版田园物语_6744.jar | 969 | 3D版田园物语 | 华动飞天 | 1.0 | MIDP-2.0 | - | 65 | 16 | 600 | 15764 | 0 | - | 运行正常 |  |
| 3D疯狂过山车S60v5版_3875.jar | 797 | Roller Rev 99 | Digital Chocolate, I | 1.0.29 | MIDP-2.0 | - | 39 | 16 | 600 | 18557 | 0 | java/io/EOFException | 运行正常 |  |
| 3D科林麦克雷拉力赛之尘埃_6600.jar | 5601 | Colin McRae DiRT 3D_konno | Glu Mobile | 1.2.5 | MIDP-1.0 | m3g,nokia-ui | 39 | 16 | 600 | 291470 | 0 | - | 运行正常 |  |
| 3D跳台滑雪2011_4507.jar | 964 | SkokiNarciarskie2011 | Baltoro Games Studio | 1.1.0 | MIDP-2.0 | m3g | 91 | 16 | 600 | 2472233 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| Boun_en_ar_de_he-IL_ru-RU.jar | 40 | Bounce | Nokia | 1.24 | MIDP-1.0 | nokia-ui | 8 | 16 | 600 | 5955 | 0 | - | 运行正常 |  |
| CCTV贺岁游戏-三国演义_6527.jar | 929 | 三国演义 | wavegame | 1.0.0 | MIDP-1.0 | nokia-ui | 33 | 16 | 600 | 75692762 | 0 | - | 运行正常 |  |
| CS反恐特警-狙击精英_6539.jar | 417 | (国际巨作)CS反恐特警-狙击精英 | 深蓝创娱 | 1.0.0 | MIDP-1.0 | - | 20 | 16 | 600 | 1541069 | 0 | java/lang/ArrayIndexOutOfBoundsE | 运行正常 |  |
| CS反恐特警真3D版2_6747.jar | 255 | CS反恐特警真3D版II | obm | 1.0 | MIDP-1.0 | - | 34 | 16 | 600 | 453766506 | 0 | - | 运行正常 |  |
| CS反恐精英(蓝牙对战)_6858.jar | 335 | Micro Counter Strike v1.1 | www.m3gworks.com | 1.1.0 | MIDP-2.0 | m3g | 61 | 16 | 600 | 351 | 0 | - | 运行正常 |  |
| FIFA09足球(EA中文版)_7962.jar | 695 | FIFA 09宝软网baoruan.com免费分享 | 掌趣科技 | 1.0 | MIDP-2.0 | - | 36 | 16 | 600 | 229968 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| Flippy跳跃大冒险_archive.jar | 141 | Flippy's Flying Frenzy | Kiloo | 1.07 | MIDP-1.0 | nokia-ui | 13 | 16 | 600 | 1167359 | 0 | java/io/EOFException,javax/micro | 运行正常 |  |
| GT赛车2_36.jar | 1815 | GT Racing 2 | Gameloft SA | 1.0.9 | MIDP-2.0 | nokia-ui | 11 | 16 | 600 | 18648935 | 0 | java/lang/NullPointerException | 运行正常 |  |
| LOVExLOVE恋爱学院_4424.jar | 860 | LS | AUER | 01.30.00 | MIDP-2.0 | - | 75 | 16 | 600 | 204408 | 0 | - | 运行正常 |  |
| NBA2010中文版_6247.jar | 849 | NBA 10（EA正版） | 掌趣科技 | 1.0.0 | MIDP-2.0 | - | 35 | 16 | 600 | 7558 | 0 | - | 运行正常 |  |
| NBA2010授权正版_5060.jar | 826 | NBA2010（授权正版） | 空中猛犸 | 1.0.0 | MIDP-2.0 | - | 13 | 16 | 600 | 308701 | 0 | - | 运行正常 |  |
| NBA篮球2007_10887.jar | 298 | NBA LIVE 07 | Electronic Arts Inc. | 4.2.23 | MIDP-2.0 | - | 86 | 16 | 600 | 4679582 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| NBA职业篮球2010豪华版_7111.jar | 841 | NBA Pro Basketball 2010 | Gameloft SA | 1.0.8 | MIDP-2.0 | - | 9 | 16 | 600 | 811488500 | 0 | - | 运行正常 |  |
| PC正版-使命召唤5_7199.jar | 407 | PC正版-使命召唤5 | CaiFun | 1.0.0 | MIDP-2.0 | - | 21 | 16 | 600 | 13863775 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| QQ飞车2010_5772.jar | 457 | QQ2010 | gameworld | 1.0 | MIDP-2.0 | - | 30 | 16 | 600 | 206842112 | 0 | java/io/EOFException | 运行正常 |  |
| Vime_en_ar_de_he-IL_ru-RU.jar | 62 | Virtual Me | Nokia | 0.17 | MIDP-1.0 | nokia-ui | 9 | 16 | 600 | 942244 | 0 | javax/microedition/rms/InvalidRe | 运行正常 |  |
| book4.jar | 20 | J2ME Games Examples | frog-parrot.net | 2.0 | MIDP-2.0 | - | 10 | 16 | 600 | 151 | 0 | - | 运行正常 |  |
| book5.jar | 29 | J2ME Games Examples | frog-parrot.net | 2.0 | MIDP-2.0 | - | 14 | 16 | 600 | 155 | 0 | - | 运行正常 |  |
| book6.jar | 38 | J2ME Games Examples | frog-parrot.net | 2.0 | MIDP-2.0 | - | 16 | 16 | 600 | 167 | 0 | - | 运行正常 |  |
| book7.jar | 39 | J2ME Games Examples | frog-parrot.net | 2.0 | MIDP-2.0 | - | 17 | 16 | 600 | 167 | 0 | - | 运行正常 |  |
| bubblet.jar | 27 | Bubblet | see the source proje | 1.0.0 | - | nokia-ui | 14 | 16 | 600 | 5138599 | 0 | - | 运行正常 |  |
| game15.jar | 3 | Game15MIDlet | see the source proje | 1.0.0 | - | - | 2 | 16 | 600 | 583462 | 0 | - | 运行正常 |  |
| hello.jar | 2 | Hello World | frog-parrot.net | 2.0 | MIDP-2.0 | - | 2 | 16 | 600 | 29446 | 0 | - | 运行正常 |  |
| jump.jar | 15 | J2ME Games Examples | frog-parrot.net | 2.0 | MIDP-2.0 | - | 7 | 16 | 600 | 143 | 0 | - | 运行正常 |  |
| maze.jar | 7 | J2ME Games Examples | frog-parrot.net | 1.0 | MIDP-1.0 | - | 4 | 16 | 600 | 332510 | 0 | - | 运行正常 |  |
| slimevolley.jar | 8 | SlimeVolleyBallMidlet | see the source proje | 1.0.0 | - | - | 7 | 16 | 600 | 15038 | 0 | - | 运行正常 |  |
| vault/000-Tomb Raider The Osiris Codex v1.0.9 (176x208) [English].jar | 134 | TombRaider1 | - | 1.0.9 | MIDP-1.0 | - | 5 | 16 | 600 | 1006636 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/001-Tomb Raider The Osiris Codex v1.1.3 (128x128) [German].jar | 63 | Tomb Raider The Osiris Codex | - | 1.1.3 | MIDP-1.0 | - | 6 | 16 | 600 | 3539767 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/002-Tomb Raider The Osiris Codex v1.1.3 (176x208) [German].jar | 135 | TombRaider1 | - | 1.1.3 | MIDP-1.0 | - | 5 | 16 | 600 | 466028 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/003-Tomb Raider The Osiris Codex v1.1.4 (176x208) [Italian].jar | 135 | TombRaider1 | - | 1.1.4 | MIDP-1.0 | - | 5 | 16 | 601 | 514540 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/004-Tomb Raider The Osiris Codex v1.1.8 (176x208) [Spanish].jar | 93 | TombRaider1 | - | 1.1.8 | MIDP-1.0 | - | 5 | 16 | 600 | 834858 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/005-Tomb Raider The Osiris Codex v1.2.5 (128x128) [Chinese A].jar | 58 | 地狱书上 | - | 1.2.5 | MIDP-1.0 | - | 6 | 16 | 600 | 2172757 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/006-Tomb Raider The Osiris Codex v1.2.5 (128x128) [Chinese B].jar | 58 | 地狱书下 | - | 1.2.5 | MIDP-1.0 | - | 6 | 16 | 600 | 2112289 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/007 - Quantum of Solace.jar | 584 | Quantum Of Solace | - | 1.0.6 | MIDP-1.0 | - | 7 | 16 | 600 | 62759612 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/007-Tomb Raider The Osiris Codex v1.5.6 (240x320) [English].jar | 118 | TombRaider1 | - | 1.5.6 | MIDP-1.0 | - | 6 | 16 | 601 | 208905 | 0 | - | 运行正常 |  |
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
| vault/2004 Real Football.jar | 189 | 2004 Real Football | - | 1.0.0 | MIDP-2.0 | - | 9 | 16 | 600 | 214675124 | 0 | - | 运行正常 |  |
| vault/2005 Real Football.jar | 201 | 2005RealFootball | - | 1.1.5 | MIDP-1.0 | - | 6 | 16 | 600 | 1861217 | 0 | - | 运行正常 |  |
| vault/2006 Real Football 3D.jar | 753 | 2006RealFootball3D | - | 1.0.7 | MIDP-2.0 | - | 7 | 16 | 602 | 2983997402 | 0 | java/lang/NumberFormatException | 运行正常 |  |
| vault/2006 Real Football.jar | 308 | 2006 Real Football | - | 1.1.0 | MIDP-2.0 | - | 6 | 16 | 600 | 17201678 | 0 | - | 运行正常 |  |
| vault/2012 Apocalypse.jar | 449 | 2012 Apocalypse | - | 1.0.0 | MIDP-1.0 | - | 48 | 16 | 600 | 19279792 | 0 | - | 运行正常 |  |
| vault/24 - Jack Bauer.jar | 591 | 24 Jack Bauer | - | 2.0.5 | MIDP-2.0 | - | 20 | 16 | 600 | 11657663 | 0 | java/io/EOFException,javax/micro | 运行正常 |  |
| vault/24 - Special Ops.jar | 302 | 24 Special Ops | - | 1.0.28 | MIDP-2.0 | - | 13 | 16 | 600 | 14303970 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/3D Bomberman Atomic.jar | 995 | Bomberman 3D | - | 1.0.3 | MIDP-2.0 | - | 36 | 16 | 600 | 482359 | 0 | java/io/EOFException,javax/micro | 运行正常 |  |
| vault/3D Fast and Furious - The Movie.jar | 930 | 3D Fast and Furious The Movi | - | 1.0.7 | MIDP-2.0 | - | 57 | 16 | 600 | 2075400 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/3D Fomula Extreme.jar | 1206 | 3DFormulaExtreeme | - | 1.0.0 | MIDP-2.0 | - | 118 | 16 | 600 | 5649290 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/3D Marble Madness.jar | 545 | Marble Madness | - | 4.10.10 | MIDP-2.0 | - | 34 | 16 | 600 | 2855380 | 0 | javax/microedition/rms/InvalidRe | 运行正常 |  |
| vault/3D Need for Drifting.jar | 312 | 3DNeedForDrift | - | 1.0 | MIDP-2.0 | - | 12 | 16 | 600 | 766746 | 0 | javax/microedition/rms/InvalidRe | 运行正常 |  |
| vault/3D Planet Riders.jar | 1278 | PlanetRiders | - | 1.2.3 | MIDP-2.0 | - | 116 | 16 | 600 | 5525126 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/3D Rally Pro Contest.jar | 200 | RallyProContest | - | 1.1 | MIDP-1.0 | - | 20 | 16 | 600 | 3023713 | 0 | javax/microedition/rms/InvalidRe | 运行正常 |  |
| vault/3D Rollercoaster Rush.jar | 282 | 3D Coaster Rush | - | 1.1.9 | MIDP-2.0 | - | 19 | 16 | 600 | 1386400 | 0 | java/io/EOFException,java/lang/N | 运行正常 |  |
| vault/3D Solid Weapon 2.jar | 529 | 3D Solid Weapon 2 | - | 1.0.1 | MIDP-2.0 | - | 64 | 16 | 600 | 14424209 | 0 | - | 运行正常 |  |
| vault/3D Solid Weapon.jar | 335 | 3D Solid Weapon - Ballacker | - | 1.0 | MIDP-2.0 | - | 36 | 16 | 600 | 365361 | 0 | - | 运行正常 |  |
| vault/3D Street Rail Racing.jar | 589 | 3D Street Rail Racing | - | 1.0.22 | MIDP-2.0 | - | 89 | 16 | 600 | 4307255 | 0 | - | 运行正常 |  |
| vault/3D Super Ball.jar | 221 | 3D SuperBall | - | 1.1.4 | MIDP-2.0 | - | 34 | 16 | 600 | 4711664 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/4 Wheel Xtreme 3D.jar | 313 | 4 Wheel Xtreme 3D | - | 0.5.15 | MIDP-2.0 | - | 56 | 16 | 600 | 18317558 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/4 Wheel Xtreme.jar | 237 | 4 Wheel Xtreme | - | 0.7.0 | MIDP-2.0 | - | 19 | 16 | 600 | 1441541 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/4x4 Extreme Rally - World Tour.jar | 378 | 4x4 Extreme Rally World Tour | - | 1.0.2 | MIDP-2.0 | - | 9 | 16 | 600 | 614280 | 0 | java/io/IOException,java/lang/Nu | 运行正常 |  |
| vault/4x4 Extreme Rally 2006.jar | 154 | 4x4 Extreme Rally 2006 | - | 1.0.3 | MIDP-2.0 | - | 10 | 16 | 600 | 10220652 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/4x4 Extreme Rally 3D.jar | 207 | 4x4 Extreme Rally 3D | - | 1.0.2 | MIDP-2.0 | - | 11 | 16 | 600 | 10052309 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/7 Wonders.jar | 309 | 7 Wonders | - | 1.0.9 | MIDP-2.0 | - | 4 | 16 | 600 | 769455347 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/A Dog's Adventure.jar | 264 | A Dog's Adventure | - | 1.0.4 | MIDP-2.0 | - | 9 | 16 | 600 | 30770 | 0 | - | 运行正常 |  |
| vault/A Good Day to Die Hard.jar | 1593 | A Good Day To Die Hard DEDOM | - | 1.1.1 | MIDP-2.0 | - | 9 | 16 | 600 | 165599089 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/AND1 Streetball.jar | 298 | AND1 Street Basketball | - | 1.0.7 | MIDP-2.0 | - | 4 | 16 | 600 | 267309418 | 0 | - | 运行正常 |  |
| vault/Abracadaball.jar | 581 | Abracadaball | - | 1.0.6 | MIDP-2.0 | - | 5 | 16 | 600 | 192824135 | 0 | java/lang/ArrayIndexOutOfBoundsE | 运行正常 |  |
| vault/Absolute Blockers Deluxe.jar | 99 | Blockers Deluxe | - | 1.1.15 | MIDP-2.0 | - | 19 | 16 | 600 | 483935 | 0 | java/lang/NumberFormatException, | 运行正常 |  |
| vault/Ace Combat - Northern Wings.jar | 848 | Ace Combat | - | 1.0.0 | MIDP-2.0 | - | 8 | 16 | 600 | 11625921 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/Aces of the Luftwaffe 2.jar | 619 | Aces Of The Luftwaffe 2 | - | 1.0 | MIDP-2.0 | - | 17 | 16 | 600 | 2470024 | 0 | java/io/IOException,javax/microe | 运行正常 |  |
| vault/Aces of the Luftwaffe.jar | 444 | Aces Of The Luftwaffe | - | 1.0 | MIDP-2.0 | - | 6 | 16 | 600 | 1986863 | 0 | java/io/IOException,javax/microe | 运行正常 |  |
| vault/Adventure Time - Heroes of OOO.jar | 320 | Adventure Time | - | 1.0.2 | MIDP-2.0 | - | 12 | 16 | 600 | 1624009 | 0 | - | 运行正常 |  |
| vault/Air Strike 1944 - Flight for Freedom.jar | 294 | Air Strike 1944 | - | 1.0.9 | MIDP-2.0 | - | 6 | 16 | 600 | 52088838 | 0 | - | 运行正常 |  |
| vault/AirBuster 3D.jar | 299 | AirBurster3d | - | 1.0.1 | MIDP-2.0 | - | 6 | 16 | 600 | 5564308 | 0 | java/io/IOException,java/lang/Ar | 运行正常 |  |
| vault/Aircraft Gear.jar | 265 | Aircraft Gear | - | 1.0.0 | MIDP-2.0 | - | 12 | 16 | 600 | 3030575 | 0 | java/io/IOException,java/lang/Nu | 运行正常 |  |
| vault/Alien Quarantine.jar | 1918 | Alien Quarantine DEDOMIL.NET | - | 1.2.3 | MIDP-2.0 | - | 17 | 16 | 600 | 45504487 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/Alpha Wing 2.jar | 553 | Alpha Wing 2 | - | 1.0.2 | MIDP-1.0 | - | 20 | 16 | 600 | 6476328 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/America's Army - Special Operations.jar | 329 | America's Army | - | 1.0.5 | MIDP-2.0 | - | 6 | 16 | 600 | 15498105 | 0 | - | 运行正常 |  |
| vault/American Gangster - The Mobile Game.jar | 543 | American Gangster | - | 1.0.9 | MIDP-2.0 | - | 6 | 16 | 600 | 11574333 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/Amy Pumpkin's Nightmare.jar | 252 | AmyPumpkin | - | 1.0 | MIDP-2.0 | - | 6 | 16 | 600 | 707975 | 0 | java/io/EOFException,javax/micro | 运行正常 |  |
| vault/Anakonda.jar | 83 | Anakonda | - | 1.0.0 | MIDP-1.0 | - | 13 | 16 | 600 | 1021025 | 0 | java/io/IOException,java/lang/Nu | 运行正常 |  |
| vault/Ancient Empires 2.jar | 264 | Ancient Empires II | - | 1.0.2 | MIDP-1.0 | - | 9 | 16 | 600 | 636956 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/Ancient Empires.jar | 125 | Ancient Empires | - | 1.0.3 | MIDP-1.0 | - | 9 | 16 | 600 | 122070 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/Arcade Side Golf.jar | 194 | ArcadeSideGolf | - | 1.0.2 | MIDP-2.0 | - | 6 | 16 | 600 | 20245697 | 0 | java/io/IOException,java/lang/Nu | 运行正常 |  |
| vault/Arkanoid Evolution.jar | 597 | Arkanoid Evolution | - | 1.1.0 | MIDP-2.0 | - | 19 | 16 | 600 | 4018079 | 0 | java/lang/ArrayIndexOutOfBoundsE | 运行正常 |  |
| vault/Arkanoid.jar | 92 | Arkanoid | - | 1.8.6 | MIDP-1.0 | - | 12 | 16 | 600 | 382505 | 0 | - | 运行正常 |  |
| vault/Army Men - Mobile Ops.jar | 654 | ArmyMen | - | 1.0.0 | MIDP-2.0 | - | 89 | 16 | 600 | 23250280 | 0 | java/io/EOFException,java/lang/A | 运行正常 |  |
| vault/Asphalt - Urban GT 3D.jar | 339 | Asphalt Urban GT 3D | - | 1.0.5 | MIDP-2.0 | - | 27 | 16 | 600 | 12886671 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/Asphalt - Urban GT.jar | 250 | AsphaltUrbanGT | - | 1.0.1 | MIDP-2.0 | - | 7 | 16 | 600 | 221253009 | 0 | javax/microedition/rms/InvalidRe | 运行正常 |  |
| vault/Asphalt 2 - Urban GT.jar | 309 | Asphalt2 | - | 1.0.0 | MIDP-2.0 | - | 6 | 16 | 600 | 8822272 | 0 | - | 运行正常 |  |
| vault/Asphalt 3 - Street Rules 3D.jar | 1027 | Asphalt 3 Street Rules 3D | - | 1.0.0 | MIDP-2.0 | - | 19 | 16 | 600 | 141142083 | 0 | java/lang/ClassNotFoundException | 运行正常 |  |
| vault/Asphalt 3 - Street Rules.jar | 285 | Asphalt3 | - | 1.0.2 | MIDP-2.0 | - | 5 | 16 | 600 | 324602040 | 0 | javax/microedition/rms/InvalidRe | 运行正常 |  |
| vault/Asphalt 4 - Elite Racing.jar | 370 | Asphalt 4 : Elite Racing | - | 1.1.7 | MIDP-2.0 | - | 10 | 16 | 600 | 10091593 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/Asphalt 6 - Adrenaline.jar | 1500 | Asphalt6 | - | 1.0.0 | MIDP-2.0 | - | 22 | 16 | 600 | 168720803 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/Asphalt Nitro.jar | 2086 | Asphalt Nitro | - | 1.1.7 | MIDP-2.0 | - | 21 | 16 | 600 | 185969608 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/Assassin's Creed - Brotherhood.jar | 1171 | Assassin's Creed Brotherhood | - | 1.1.8 | MIDP-2.0 | - | 11 | 16 | 600 | 183741858 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/Assassin's Creed - Revelations.jar | 1596 | Assassin's Creed Revelations | - | 1.0.6 | MIDP-2.0 | - | 14 | 16 | 600 | 40767011 | 0 | java/lang/IndexOutOfBoundsExcept | 运行正常 |  |
| vault/Assassin's Creed II.jar | 1025 | Assassin's Creed 2 | - | 1.2.3 | MIDP-2.0 | - | 9 | 16 | 600 | 180784094 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/Assassin's Creed III.jar | 1625 | Assassin's Creed III | - | 1.1.3 | MIDP-2.0 | - | 12 | 16 | 600 | 116503061 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/Assassin's Creed.jar | 612 | Assassin's Creed | - | 1.0.2 | MIDP-2.0 | - | 5 | 16 | 600 | 648882219 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/Astro Knight Galaxy.jar | 182 | Knight | - | 1.0 | MIDP-1.0 | - | 3 | 16 | 600 | 287345774 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/Atlantis SkyPatrol.jar | 468 | Atlantis Sky Patrol | - | 1.0.0 | MIDP-2.0 | - | 12 | 16 | 600 | 750784 | 0 | - | 运行正常 |  |
| vault/Avalanche Snowboarding.jar | 836 | Avalanche Snowboarding | - | 1.2.0 | MIDP-2.0 | - | 17 | 16 | 600 | 18593864 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/Avenging Angel.jar | 221 | Avenging Angel | - | 2.00.36 | MIDP-2.0 | - | 5 | 16 | 600 | 638148 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/Batman - Guardian of Gotham.jar | 638 | Batman(TM) - Guardian of Got | - | 2.0.0 | MIDP-2.0 | - | 82 | 16 | 600 | 5917024 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/Battle for The White House.jar | 576 | Battle For The White House | - | 1.0.4 | MIDP-2.0 | - | 4 | 16 | 600 | 13409832 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/Battleships - Sea on Fire.jar | 222 | Battleships 2010 | - | 1.13.5 | MIDP-2.0 | - | 17 | 16 | 600 | 437581 | 0 | - | 运行正常 |  |
| vault/Battlestar Galactica.jar | 440 | Battlestar Galactica | - | 1.0.0 | MIDP-2.0 | - | 61 | 16 | 600 | 5716326 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/Beach Mini Golf 3D.jar | 238 | Minigolf 3D | - | 1.3.01 | MIDP-1.0 | - | 55 | 16 | 600 | 12688892 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/Bejeweled 3.jar | 550 | Bejeweled 3 | - | 4.0.78 | MIDP-2.0 | - | 42 | 16 | 600 | 2937296 | 0 | - | 运行正常 |  |
| vault/Bejeweled Twist.jar | 574 | BejeweledTwist | - | 01.00.04 | MIDP-2.0 | - | 5 | 16 | 600 | 31599371 | 0 | java/io/IOException,javax/microe | 运行正常 |  |
| vault/Bejeweled.jar | 369 | Bejeweled | - | 1.4.48 | MIDP-2.0 | - | 14 | 16 | 600 | 9436484 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/Beowulf - The Mobile Game.jar | 576 | Beowulf | - | 1.1.1 | MIDP-2.0 | - | 7 | 16 | 600 | 11182462 | 0 | - | 运行正常 |  |
| vault/Big Range Hunting 2.jar | 1048 | Big Range Hunting 2 | - | 1.0.0 | MIDP-2.0 | - | 13 | 16 | 600 | 14764809 | 0 | java/lang/ArrayIndexOutOfBoundsE | 运行正常 |  |
| vault/Big Range Hunting.jar | 628 | Big Range Hunting | - | 1.0.1 | MIDP-2.0 | - | 9 | 16 | 600 | 340162399 | 0 | java/lang/ClassNotFoundException | 运行正常 |  |
| vault/Bikini Volleyball.jar | 621 | Bikini Volleyball | - | 1.0.7 | MIDP-2.0 | - | 8 | 16 | 600 | 4434518 | 0 | java/lang/ArrayIndexOutOfBoundsE | 运行正常 |  |
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
| vault/Blood+.jar | 677 | Blood+ | - | 1.0.0 | MIDP-2.0 | - | 23 | 16 | 600 | 7544461 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/Blur Racing Mobile.jar | 624 | Blur | - | 1.0.0 | MIDP-2.0 | - | 22 | 16 | 600 | 259433233 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/Bolt.jar | 432 | Bolt | - | 1.0.0 | MIDP-2.0 | - | 21 | 16 | 600 | 562021 | 0 | java/io/EOFException,java/io/IOE | 运行正常 |  |
| vault/Bomberman 2007.jar | 291 | Bomberman | - | 1.0.32 | MIDP-1.0 | - | 12 | 16 | 600 | 23434154 | 0 | javax/microedition/rms/InvalidRe | 运行正常 |  |
| vault/Bomberman Deluxe.jar | 166 | Bomberman Deluxe | - | 1.28.2 | MIDP-1.0 | - | 5 | 16 | 600 | 26684634 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/Bounce Tales.jar | 592 | Bounce Tales | - | 2.0.25 | MIDP-2.0 | - | 20 | 16 | 600 | 11624377 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/Brave - The Official Game.jar | 1373 | Brave | - | 1.1.1 | MIDP-2.0 | - | 29 | 16 | 600 | 17818806 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/Brothers in Arms - Art of War.jar | 594 | Brothers in Arms Art of War | - | 1.3.8 | MIDP-2.0 | - | 6 | 16 | 600 | 63293982 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/Brothers in Arms - Earned in Blood.jar | 257 | Brothers In Arms | - | 1.0.1 | MIDP-2.0 | - | 9 | 16 | 600 | 1103978 | 0 | - | 运行正常 |  |
| vault/Bubble Bash 2.jar | 994 | Bubble Bash 2 | - | 1.1.3 | MIDP-2.0 | - | 24 | 16 | 600 | 63257984 | 0 | - | 运行正常 |  |
| vault/Bubble Bash 3.jar | 1224 | Bubble Bash 3 | - | 1.1.3 | MIDP-2.0 | - | 20 | 16 | 600 | 702965020 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/Bubble Bash Mania.jar | 955 | Bubble Bash Mania | - | 1.2.9 | MIDP-2.0 | - | 16 | 16 | 600 | 11805597068 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/Bubble Bash.jar | 320 | Bubble Bash | - | 1.0.2 | MIDP-2.0 | - | 9 | 16 | 600 | 663207020 | 0 | - | 运行正常 |  |
| vault/Bubble Bobble Evolution.jar | 545 | Bubble Bobble Evolution | - | 1.5.2 | MIDP-1.0 | - | 15 | 16 | 600 | 4252198 | 0 | java/lang/IllegalArgumentExcepti | 运行正常 |  |
| vault/Bubble Bobble.jar | 137 | Bubble Bobble | - | 1.4.9 | MIDP-2.0 | - | 12 | 16 | 600 | 423368 | 0 | - | 运行正常 |  |
| vault/Bugs Bunny - Rabbit Rescue.jar | 576 | Bugs Bunny | - | 1.0.9 | MIDP-2.0 | - | 38 | 16 | 600 | 3440861 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/Bulldozer Inc.jar | 190 | BulldozerInc | - | 1.0 | MIDP-2.0 | - | 5 | 16 | 600 | 315201 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/Bumper Car City.jar | 316 | Autoscooter | - | 1.0.58 | MIDP-2.0 | - | 14 | 16 | 600 | 18969582 | 0 | java/io/EOFException,java/lang/N | 运行正常 |  |
| vault/Burger Time Delight.jar | 265 | BurgerTime Delight | - | 1.0.0 | MIDP-2.0 | - | 38 | 16 | 600 | 6510060 | 0 | - | 运行正常 |  |
| vault/Burger Time Special.jar | 149 | Burger Time Special | - | 1.0.56 | MIDP-1.0 | - | 9 | 16 | 600 | 4751277 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/Burning Tires.jar | 412 | 3D Burning Tires | - | 1.3.6 | MIDP-2.0 | - | 115 | 16 | 600 | 3805776 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/Burnout 3D.jar | 371 | Burnout | - | 4.2.36 | MIDP-2.0 | - | 10 | 16 | 600 | 16499908 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/Burnout.jar | 354 | Burnout | - | 4.3.21 | MIDP-2.0 | - | 8 | 16 | 600 | 4671537 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/Caesar.jar | 266 | Caesar | - | 0.4.6 | MIDP-1.0 | - | 5 | 16 | 600 | 1345361 | 0 | - | 运行正常 |  |
| vault/Cake Mania - My Story.jar | 967 | Cake Mania | - | 1.0.1 | MIDP-2.0 | - | 43 | 16 | 600 | 2600879 | 0 | java/io/EOFException,javax/micro | 运行正常 |  |
| vault/California Gold Rush - Bonanza!.jar | 1227 | CGRBonanza | - | 1.0.8 | MIDP-2.0 | - | 71 | 16 | 600 | 837406 | 0 | java/io/EOFException,javax/micro | 运行正常 |  |
| vault/Cannon Rats.jar | 1050 | Cannon Rats | - | 1.1.5 | MIDP-2.0 | - | 49 | 16 | 600 | 197684627 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/Castle of Magic.jar | 946 | Castle of Magic | - | 1.1.3 | MIDP-2.0 | - | 12 | 16 | 600 | 454394165 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/Castlevania - Aria of Sorrow.jar | 405 | Castlevania Aria Of Sorrow | - | 1.0.0 | MIDP-2.0 | - | 4 | 16 | 600 | 8945551 | 0 | javax/microedition/rms/InvalidRe | 运行正常 |  |
| vault/Castlevania - Dawn of Sorrow.jar | 610 | Castlevania Dawn of Sorrow | - | 1.0.0 | MIDP-2.0 | - | 19 | 16 | 600 | 4025754 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/Castlevania - Order of Shadows.jar | 521 | CastlevaniaOoS | - | 0.0.7 | MIDP-2.0 | - | 66 | 16 | 600 | 1037964 | 0 | javax/microedition/rms/InvalidRe | 运行正常 |  |
| vault/Chuck Norris - Bring on the Pain.jar | 621 | Chuck Norris - Bring on the  | - | 1.1.0 | MIDP-2.0 | - | 7 | 16 | 600 | 27381970 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/City Bloxx.jar | 422 | City Bloxx | - | 1.0.22 | MIDP-2.0 | - | 19 | 16 | 600 | 13757665 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/Clash of the Titans - The Movie.jar | 713 | Clash of Titans Movie | - | 1.0.0 | MIDP-2.0 | - | 17 | 16 | 600 | 265234483 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/Colin McRae - DiRT 3D.jar | 867 | Colin McRae DiRT 3D srv | - | 1.0.5 | MIDP-1.0 | - | 39 | 16 | 600 | 14487631 | 0 | - | 运行正常 |  |
| vault/Colin McRae - DiRT.jar | 568 | Colin McRae DiRT | - | 1.2.5 | MIDP-1.0 | - | 21 | 16 | 600 | 5785988 | 0 | - | 运行正常 |  |
| vault/Command & Conquer 3 - Tiberium Wars.jar | 594 | Command & Conquer 3 - Tiberi | - | 4.1.64 | MIDP-2.0 | - | 29 | 16 | 600 | 2847419 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/Command & Conquer 4 - Tiberian Twilight.jar | 1097 | C&C4 Tiberian Twilight | - | 4.1.10 | MIDP-2.0 | - | 38 | 16 | 600 | 2466996 | 0 | - | 运行正常 |  |
| vault/Commandos.jar | 609 | Commandos | - | 1.2.3 | MIDP-2.0 | - | 20 | 16 | 600 | 33509532 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/Cowboys & Aliens.jar | 1255 | Cowboys And Aliens | - | 1.1.8 | MIDP-2.0 | - | 16 | 16 | 600 | 192651415 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/Crash Arena 3D.jar | 379 | CrashArena 3D | - | 1.3.0 | MIDP-2.0 | - | 81 | 16 | 600 | 3692984 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/Crash Bamdicoot - Mutant Island.jar | 905 | Crash Mutant Island | - | 1.0.4 | MIDP-1.0 | - | 49 | 16 | 600 | 5360357 | 0 | java/io/IOException,java/lang/Nu | 运行正常 |  |
| vault/Crash Car Mania 3D.jar | 505 | Crash Car Mania by piguro | - | 1.5.1 | MIDP-2.0 | - | 36 | 16 | 600 | 3954829 | 0 | java/lang/ClassCastException,jav | 运行正常 |  |
| vault/Crash of the Titans.jar | 282 | Crash Titans | - | 1.2.1 | MIDP-2.0 | - | 11 | 16 | 600 | 332424 | 0 | - | 运行正常 |  |
| vault/Crazy Window Cleaners.jar | 473 | WindowCleaners | - | 1.0.52 | MIDP-2.0 | - | 15 | 16 | 600 | 3096060 | 0 | java/io/EOFException,java/lang/N | 运行正常 |  |
| vault/Crystal Defenders.jar | 384 | Crystal Defenders | - | 1.0.0 | MIDP-2.0 | - | 17 | 16 | 600 | 7431381 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/Dakar 2008.jar | 409 | Dakar 2008 | - | 1.0.1 | MIDP-1.0 | - | 3 | 16 | 600 | 285362232 | 0 | - | 运行正常 |  |
| vault/Dakar 2009.jar | 849 | DAKAR Rally 2009 | - | 1.1.3 | MIDP-2.0 | - | 46 | 16 | 600 | 2974967 | 0 | java/io/IOException,java/lang/Nu | 运行正常 |  |
| vault/Dakar 2010.jar | 975 | DAKAR Rally 2010 | - | 0.1.8 | MIDP-2.0 | - | 47 | 16 | 600 | 10384560 | 0 | java/io/IOException,java/lang/Nu | 运行正常 |  |
| vault/Danger Dash.jar | 1282 | Danger Dash | - | 1.0.5 | MIDP-2.0 | - | 24 | 16 | 600 | 567152521 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/Death Race.jar | 609 | Death Race: The Mobile Game | - | 1.1.4 | MIDP-2.0 | - | 17 | 16 | 600 | 86994141 | 0 | javax/microedition/rms/InvalidRe | 运行正常 |  |
| vault/Death Trap 2 - The Unlocked Code.jar | 237 | Death Trap 2.TUC | - | 1.0.0 | MIDP-2.0 | - | 12 | 16 | 600 | 566189 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/Death Zone.jar | 306 | Death Zone serviak and Black | - | 1.0.0 | MIDP-2.0 | - | 24 | 16 | 600 | 638687205 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/Dewy's Adventure.jar | 416 | Dewy's Adventure | - | 1.0.0 | MIDP-2.0 | - | 2 | 16 | 600 | 6765217759 | 0 | javax/microedition/rms/InvalidRe | 运行正常 |  |
| vault/Diamond Rush.jar | 296 | Diamond Rush | - | 1.2.0 | MIDP-2.0 | - | 10 | 16 | 600 | 1179769 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/Diamond Twister (1).jar | 594 | Diamond Twister | - | 1.0.8 | MIDP-2.0 | - | 6 | 16 | 600 | 9648119 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/Diamond Twister 2.jar | 1003 | Diamond Twister 2 | - | 1.1.3 | MIDP-2.0 | - | 8 | 16 | 601 | 7009717 | 0 | - | 运行正常 |  |
| vault/Dictator Defense.jar | 656 | DictatorDef | - | 1.0.18 | MIDP-2.0 | - | 32 | 16 | 600 | 2021228 | 0 | java/io/EOFException,javax/micro | 运行正常 |  |
| vault/Die Hard 4.0 - The Mobile Game.jar | 593 | Die Hard 4.0 | - | 1.0.6 | MIDP-2.0 | - | 5 | 16 | 600 | 591469450 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/Doodle Jump.jar | 452 | Doodle Jump | - | 1.3.0 | MIDP-2.0 | - | 7 | 16 | 600 | 989325546 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/Doom II RPG.jar | 748 | Doom II RPG | - | 5.1.89 | MIDP-2.0 | - | 35 | 16 | 600 | 4648340 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/Doom RPG.jar | 295 | Doom RPG | - | 1.4.67 | MIDP-2.0 | - | 26 | 16 | 600 | 258335 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/Dragon Mania.jar | 1604 | Dragon Mania | - | 1.0.0 | MIDP-2.0 | - | 24 | 16 | 600 | 596170174 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/Driver - San Francisco.jar | 1265 | Driver San Francisco | - | 1.2.4 | MIDP-2.0 | - | 11 | 16 | 600 | 68282003 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/Driver L.A. Undercover.jar | 539 | Driver | - | 1.0.5 | MIDP-2.0 | - | 6 | 16 | 600 | 229352757 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/Dungeon Hunter - Curse Of Heaven.jar | 1012 | Dungeon Hunter Curse of Heav | - | 1.0.0 | MIDP-2.0 | - | 16 | 16 | 600 | 50845586 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/Dungeon Hunter 3.jar | 1334 | DUNGEON HUNTER 3 | - | 1.3.0 | MIDP-2.0 | - | 13 | 16 | 600 | 240999944 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/Earthworm Jim.jar | 1026 | Earthworm Jim | - | 1.1.7 | MIDP-2.0 | - | 17 | 16 | 600 | 112910616 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/El Che.jar | 386 | El Che | - | 1.0.4 | MIDP-2.0 | - | 26 | 16 | 600 | 2244141 | 0 | - | 运行正常 |  |
| vault/Elven Chronicles.jar | 233 | Elven Chronicles | - | 1.0.9 | MIDP-2.0 | - | 11 | 16 | 600 | 14479823 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/FIFA 2007.jar | 419 | FIFA 07 | - | 1.7.11 | MIDP-1.0 | - | 5 | 16 | 600 | 365342 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/Family Guy - Uncensored.jar | 471 | Family Guy Uncensored | - | 1.0.4 | MIDP-2.0 | - | 10 | 16 | 600 | 7200005 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/Family Guy 2.jar | 808 | Family Guy 2 | - | 1.0.0 | MIDP-2.0 | - | 25 | 16 | 600 | 15171259 | 0 | java/lang/ArrayIndexOutOfBoundsE | 运行正常 |  |
| vault/FarCry 2.jar | 626 | Far Cry 2 | - | 1.0.8 | MIDP-2.0 | - | 17 | 16 | 600 | 126040750 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/Fast Five The Movie - Official Game.jar | 1226 | Fast Five | - | 1.2.2 | MIDP-2.0 | - | 12 | 16 | 600 | 21604947 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/Fast and Furious 6.jar | 1917 | Fast & Furious 6 | - | 1.1.2 | MIDP-2.0 | - | 19 | 16 | 600 | 54786277 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/Ferrari GT - Evolution.jar | 739 | Ferrari GT | - | 1.0.0 | MIDP-2.0 | - | 14 | 16 | 600 | 2314691 | 0 | - | 运行正常 |  |
| vault/Ferrari GT 2 - Revolution.jar | 992 | Ferrari GT 2: Revolution | - | 1.1.2 | MIDP-2.0 | - | 19 | 16 | 601 | 202666626 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/Ferrari GT 3 - World Track.jar | 1562 | Ferrari GT 3: World Track | - | 1.3.3 | MIDP-2.0 | - | 36 | 16 | 600 | 71671324 | 0 | java/io/IOException,java/lang/Nu | 运行正常 |  |
| vault/Ferrari World Championship 2009.jar | 988 | Ferrari World Championship 2 | - | 1.0.6 | MIDP-2.0 | - | 15 | 16 | 600 | 26432938 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/Ferrari World Championship.jar | 595 | Ferrari World Championship | - | 1.0.8 | MIDP-2.0 | - | 13 | 16 | 600 | 31749912 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/Final Fantasy II.jar | 1343 | Final Fantasy II | - | 1.0.0 | MIDP-2.0 | - | 13 | 16 | 600 | 5323413 | 0 | java/lang/ArrayIndexOutOfBoundsE | 运行正常 |  |
| vault/Final Fantasy.jar | 1148 | Final Fantasy | - | 1.0.0 | MIDP-2.0 | - | 12 | 16 | 600 | 3555013 | 0 | - | 运行正常 |  |
| vault/FlatOut 3D.jar | 348 | FlatOut Racing3D | - | 0.4.9 | MIDP-2.0 | - | 54 | 16 | 600 | 68779800 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/FlatOut.jar | 402 | FlatOut Racing | - | 0.5.7 | MIDP-2.0 | - | 22 | 16 | 600 | 24841953 | 0 | java/lang/IllegalArgumentExcepti | 运行正常 |  |
| vault/Frogger Beats n' Bounces.jar | 216 | Frogger Beats n Bounces | - | 1.0.7 | MIDP-2.0 | - | 2 | 16 | 601 | 880065 | 0 | - | 运行正常 |  |
| vault/Frogger Evolution.jar | 224 | Frogger Evolution | - | 1.0.0 | MIDP-2.0 | - | 2 | 16 | 600 | 4709889 | 0 | - | 运行正常 |  |
| vault/G.I. Joe - The Rise of Cobra.jar | 678 | G.I. JOE | - | 7.2.50 | MIDP-2.0 | - | 13 | 16 | 600 | 1006582 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/GT Racing - Motor Academy.jar | 992 | GT Racing: Motor Academy | - | 1.2.2 | MIDP-2.0 | - | 12 | 16 | 600 | 2878434 | 0 | - | 运行正常 |  |
| vault/GT Racing 2 - The Real Car Experience.jar | 1812 | GT Racing 2 | - | 1.0.9 | MIDP-2.0 | - | 11 | 16 | 600 | 17605209 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/Galaga 3.jar | 141 | Galaga 3 | - | 1.0.2 | MIDP-2.0 | - | 13 | 16 | 600 | 12001759 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/Galaga X.jar | 505 | Galaga X | - | 1.0.5 | MIDP-2.0 | - | 33 | 16 | 600 | 40249450 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/Galazer Deluxe.jar | 715 | Galazer Deluxe | - | 1.1.6 | MIDP-2.0 | - | 63 | 16 | 600 | 428041 | 0 | - | 运行正常 |  |
| vault/Gangstar - Crime City.jar | 324 | Gangstar - Crime City | - | 1.0.3 | MIDP-2.0 | - | 5 | 16 | 600 | 471501210 | 0 | javax/microedition/rms/InvalidRe | 运行正常 |  |
| vault/Gangstar - Miami Vindication.jar | 1268 | GangStar: Miami Vindication | - | 1.1.8 | MIDP-2.0 | - | 10 | 16 | 600 | 424838050 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/Gangstar 2 - Kings of L.A..jar | 987 | Gangstar 2 | - | 1.0.7 | MIDP-2.0 | - | 11 | 16 | 600 | 360167875 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/Gangstar City.jar | 1621 | Gangstar City | - | 1.1.1 | MIDP-2.0 | - | 16 | 16 | 600 | 547991627 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/Gangstar Rio - City of Saints.jar | 1449 | Gangstar Rio: City of Saints | - | 1.0.9 | MIDP-2.0 | - | 13 | 16 | 600 | 274430988 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/God of War - Betrayal.jar | 488 | God Of War | - | 1.4.46 | MIDP-2.0 | - | 6 | 16 | 600 | 142966717 | 0 | java/lang/NumberFormatException, | 运行正常 |  |
| vault/Gorilla Rampage.jar | 957 | GorillaAngriff | - | 1.0.1 | MIDP-2.0 | - | 38 | 16 | 600 | 742024 | 0 | java/io/EOFException,javax/micro | 运行正常 |  |
| vault/Green Farm 3.jar | 1435 | Green Farm 3 | - | 1.0.7 | MIDP-2.0 | - | 22 | 16 | 600 | 63728788 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/Guitar Hero - Warriors of Rock Mobile More Music.jar | 615 | Guitar Hero Warriors of Rock | - | 3.1.0 | MIDP-2.0 | - | 25 | 16 | 600 | 20853841 | 0 | java/lang/ArrayIndexOutOfBoundsE | 运行正常 |  |
| vault/Guitar Hero - Warriors of Rock Mobile.jar | 1013 | Guitar Hero Warriors of Rock | - | 1.0.0 | MIDP-2.0 | - | 24 | 16 | 600 | 25984147 | 0 | javax/microedition/rms/InvalidRe | 运行正常 |  |
| vault/Guitar Hero World Your - Mobile.jar | 720 | Guitar Hero World Tour | - | 1.0.11 | MIDP-2.0 | - | 33 | 16 | 600 | 1002215 | 0 | java/io/IOException | 运行正常 |  |
| vault/Guitar Rock Tour.jar | 598 | Guitar Rock Tour | - | 1.0.8 | MIDP-2.0 | - | 6 | 16 | 600 | 321189571 | 0 | java/lang/NumberFormatException | 运行正常 |  |
| vault/GunFever.jar | 167 | GunFever | - | 1.0.4 | MIDP-2.0 | - | 9 | 16 | 600 | 540289 | 0 | - | 运行正常 |  |
| vault/Guns, Wheels & Madheads 2.jar | 310 | Guns Wheels Madheads 2 | - | 1.1.0 | MIDP-2.0 | - | 27 | 16 | 600 | 708419 | 0 | - | 运行正常 |  |
| vault/Guns, Wheels & Madheads.jar | 329 | Guns Wheels Madheads | - | 1.1.0 | MIDP-2.0 | - | 28 | 16 | 600 | 80131483 | 0 | - | 运行正常 |  |
| vault/Hero of Sparta.jar | 993 | Hero Of Sparta | - | 1.1.9 | MIDP-2.0 | - | 9 | 16 | 600 | 636602976 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/Ice Age 4 - Continental Drift.jar | 1527 | Ice Age 4: Continental Drift | - | 1.1.6 | MIDP-2.0 | - | 17 | 16 | 600 | 196936691 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/Ice Age Scrat-ventures.jar | 1016 | ICE AGE SCRAT-VENTURES | - | 1.0.1 | MIDP-2.0 | - | 24 | 16 | 600 | 84498029 | 0 | - | 运行正常 |  |
| vault/Ice Age Village.jar | 1791 | Ice Age Village | - | 1.0.9 | MIDP-2.0 | - | 19 | 16 | 600 | 196237724 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/Immortals.jar | 1387 | Immortals | - | 1.0.7 | MIDP-2.0 | - | 13 | 16 | 600 | 130566992 | 0 | - | 运行正常 |  |
| vault/Iron Man 2.jar | 1074 | Iron Man 2 | - | 1.1.4 | MIDP-2.0 | - | 9 | 16 | 600 | 133717447 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/Iron Man 3.jar | 1753 | Iron Man 3 | - | 1.1.2 | MIDP-2.0 | - | 12 | 16 | 600 | 166184845 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/James Cameron's Avatar.jar | 998 | Avatar | - | 1.0.6 | MIDP-2.0 | - | 9 | 16 | 600 | 21523502 | 0 | - | 运行正常 |  |
| vault/Jurassic Park.jar | 1102 | Jurassic Park | - | 1.1.2 | MIDP-2.0 | - | 12 | 16 | 600 | 141527149 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/K.O. Legends.jar | 1024 | K.O. Legends | - | 1.0.6 | MIDP-2.0 | - | 23 | 16 | 600 | 226749888 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/Kartmania 3D.jar | 934 | Kartmania3D | - | 1.0.2 | MIDP-2.0 | - | 123 | 16 | 600 | 4141190 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/King Kong.jar | 297 | KingKong | - | 1.0.3 | MIDP-2.0 | - | 5 | 16 | 600 | 60703102 | 0 | - | 运行正常 |  |
| vault/LOST.jar | 308 | LOST | - | 1.0.2 | MIDP-2.0 | - | 6 | 16 | 600 | 346817663 | 0 | - | 运行正常 |  |
| vault/Let's Go Bowling.jar | 961 | Let's go bowling! | - | 1.1.8 | MIDP-2.0 | - | 23 | 16 | 600 | 239404490 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/Let's Golf.jar | 1027 | Let's Golf! | - | 1.1.1 | MIDP-2.0 | - | 7 | 16 | 600 | 112541540 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/Looney Tunes Monster Match.jar | 674 | LooneyTunes Monster Match | - | 1.0.0 | MIDP-2.0 | - | 44 | 16 | 600 | 5111906 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/Lucha Libre - Desafio Total.jar | 517 | Lucha Libre Desafio Total | - | 1.1.7 | MIDP-2.0 | - | 6 | 16 | 601 | 354716059 | 0 | - | 运行正常 |  |
| vault/Magic Wing II Ep.1.jar | 140 | MagicWingIIEp1\gkataria | - | 1.0 | MIDP-1.0 | - | 3 | 16 | 600 | 225925262 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/Marble Madness.jar | 636 | Marble Madness | - | 4.5.84 | MIDP-2.0 | - | 32 | 16 | 600 | 5168146 | 0 | javax/microedition/rms/InvalidRe | 运行正常 |  |
| vault/March of Heroes.jar | 1496 | March Of Heroes | - | 1.1.2 | MIDP-2.0 | - | 17 | 16 | 209 | 38698074 | 0 | java/lang/NullPointerException | 标题自行退出 | exited itself (notifyDestroyed) after 209 fr |
| vault/Mech Strategy Wars.jar | 698 | MechStrategyWars | - | 1.0.6 | MIDP-2.0 | - | 13 | 16 | 600 | 4504832 | 0 | - | 运行正常 |  |
| vault/Medal of Honor - Airborne - Elite.jar | 559 | Medal Of Honor Airborne Elit | - | 1.0.0 | MIDP-2.0 | - | 32 | 16 | 600 | 10557545 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/Medal of Honor - Airborne 3D.jar | 587 | Medal Of Honor Airborne | - | 7.1.33 | MIDP-2.0 | - | 40 | 16 | 600 | 8893457 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/Medal of Honor - Airborne.jar | 547 | Medal Of Honor Airborne | - | 4.5.25 | MIDP-2.0 | - | 32 | 16 | 600 | 10342281 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/Medal of Honor 2010.jar | 1118 | Medal of Honor 2010 | - | 4.4.47 | MIDP-2.0 | - | 33 | 16 | 600 | 1531108 | 0 | - | 运行正常 |  |
| vault/Medal of Honor.jar | 418 | Medal of Honor | - | 1.12.73 | MIDP-2.0 | - | 41 | 16 | 600 | 618759417 | 0 | - | 运行正常 |  |
| vault/Medieval Combat - Age of Glory.jar | 267 | Medieval Combat - Age of Glo | - | 1.0.4 | MIDP-2.0 | - | 6 | 16 | 600 | 162138922 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/Mega Tower Assault.jar | 848 | Mega Tower Assault | - | 1.2.6 | MIDP-2.0 | - | 16 | 16 | 600 | 5583007 | 0 | - | 运行正常 |  |
| vault/Men in Black 3.jar | 1618 | Men in Black 3 | - | 1.3.0 | MIDP-2.0 | - | 17 | 16 | 600 | 79140841 | 0 | - | 运行正常 |  |
| vault/Metal Gear Acid 2 3D.jar | 595 | Metal Gear Acid 2 3D | - | 1.0.0 | MIDP-2.0 | - | 37 | 16 | 600 | 9462724 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/Metal Gear Acid 2.jar | 426 | Metal Gear Acid 2 | - | 1.0.0 | MIDP-2.0 | - | 24 | 16 | 600 | 10986848 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/Metal Gear Acid 3D.jar | 561 | MGA Mobile | - | 1.0.0 | MIDP-1.0 | - | 6 | 16 | 600 | 11365114 | 0 | java/lang/IllegalArgumentExcepti | 运行正常 |  |
| vault/Metal Gear Acid.jar | 373 | MGA Mobile | - | 1.0.0 | MIDP-1.0 | - | 5 | 16 | 600 | 11343243 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/Metal Gear.jar | 301 | Metal Gear | - | 0.0.4 | MIDP-2.0 | - | 12 | 16 | 600 | 1571147 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/Metal Slug 4 Mobile.jar | 681 | Metal Slug M4 | - | 1.1.1 | MIDP-2.0 | - | 6 | 16 | 600 | 3272600 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/Meteos - Astro Blocks.jar | 290 | Meteos | - | 1.0.5 | MIDP-2.0 | - | 9 | 16 | 600 | 254565967 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/Midnight Bowling 2.jar | 588 | Midnight Bowling 2 | - | 1.1.1 | MIDP-2.0 | - | 5 | 16 | 600 | 185538582 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/Midnight Bowling 3.jar | 1430 | Midnight Bowling 3 | - | 1.1.3 | MIDP-2.0 | - | 30 | 16 | 600 | 38737350 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/Midnight Bowling.jar | 260 | MidnightBowling | - | 1.0.1 | MIDP-2.0 | - | 5 | 16 | 600 | 2984109 | 0 | javax/microedition/rms/InvalidRe | 运行正常 |  |
| vault/Midnight Pool 2.jar | 605 | Midnight Pool 2 | - | 1.0.9 | MIDP-2.0 | - | 13 | 16 | 600 | 396033219 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/Midnight Pool 3.jar | 1348 | Midnight Pool 3 | - | 1.2.2 | MIDP-2.0 | - | 29 | 16 | 600 | 51654719 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/Midnight Pool 4.jar | 656 | Midnight Pool | - | 1.0.2 | MIDP-2.0 | - | 26 | 16 | 600 | 43069956 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/Midnight Pool.jar | 598 | Midnight Pool | - | 1.0.3 | MIDP-2.0 | - | 6 | 16 | 600 | 1838293 | 0 | - | 运行正常 |  |
| vault/Mig 2D Retro Shooter.jar | 1494 | Mig29 2D Retro Shooter | - | 1.0.0 | MIDP-2.0 | - | 75 | 16 | 600 | 36769345 | 0 | java/io/EOFException,java/io/IOE | 运行正常 |  |
| vault/Might and Magic II.jar | 610 | MightAndMagicII | - | 1.1.8 | MIDP-2.0 | - | 12 | 16 | 600 | 190609006 | 0 | java/lang/ClassNotFoundException | 运行正常 |  |
| vault/Might and Magic.jar | 189 | MightAndMagic | - | 1.0.0 | MIDP-2.0 | - | 5 | 16 | 600 | 172832600 | 0 | - | 运行正常 |  |
| vault/Million Dollar Poker.jar | 594 | Million Dollar Poker | - | 1.0.6 | MIDP-2.0 | - | 10 | 16 | 600 | 186553723 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/Millionaire City.jar | 751 | MillCity | - | 1.0.25 | MIDP-2.0 | - | 64 | 16 | 600 | 1855441 | 0 | java/io/EOFException,javax/micro | 运行正常 |  |
| vault/Mini Golf Magic 3D.jar | 334 | MiniGolfMagic by [wl] | - | 1.5.05 | MIDP-2.0 | - | 18 | 16 | 600 | 1657947 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/MiniGolf Theme Park - 99 Holes.jar | 469 | MGolf Park 99 | - | 1.0.11 | MIDP-2.0 | - | 36 | 16 | 600 | 12320689 | 0 | java/io/EOFException,javax/micro | 运行正常 |  |
| vault/Minigolf Revolution - Pirate Park.jar | 581 | Minigolf Revolution Pirate P | - | 1.0.1 | MIDP-2.0 | - | 7 | 16 | 600 | 9194388 | 0 | - | 运行正常 |  |
| vault/Mission Impossible III.jar | 313 | Mission Impossible III | - | 1.0.0 | MIDP-2.0 | - | 4 | 16 | 600 | 430581033 | 0 | - | 运行正常 |  |
| vault/Modern Combat 2 - Black Pegasus.jar | 1344 | Modern Combat 2: Black Pegas | - | 1.2.2 | MIDP-2.0 | - | 13 | 16 | 600 | 161802566 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/Modern Combat 4 - Zero Hour.jar | 1539 | Modern Combat4: Zero Hour | - | 1.1.2 | MIDP-2.0 | - | 12 | 16 | 600 | 270126237 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/Monsters University.jar | 1355 | Monsters University | - | 1.1.2 | MIDP-2.0 | - | 13 | 16 | 600 | 21945927 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/Moto Racing  Fever 3D.jar | 312 | Moto Racing Fever | - | 1.2.0 | MIDP-2.0 | - | 27 | 16 | 600 | 12647329 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/N.O.V.A. 3.jar | 1536 | N.O.V.A. 3 | - | 1.1.9 | MIDP-2.0 | - | 9 | 16 | 600 | 13654416 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/N.O.V.A. Legacy.jar | 1226 | N.O.V.A. Legacy | - | 1.0.8 | MIDP-2.0 | - | 12 | 16 | 600 | 30280340 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/N.O.V.A..jar | 1195 | N.O.V.A. | - | 1.0.6 | MIDP-2.0 | - | 14 | 16 | 600 | 179286175 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/NBA Smash!.jar | 617 | NBA Smash | - | 1.0.9 | MIDP-2.0 | - | 9 | 16 | 600 | 137407896 | 0 | java/lang/ArrayIndexOutOfBoundsE | 运行正常 |  |
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
| vault/Ninja Prophecy.jar | 1021 | Ninja Prophecy | - | 1.1.5 | MIDP-2.0 | - | 54 | 16 | 600 | 160199619 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/Nitro Street Racing 2.jar | 976 | NitroStreet Racing2 | - | 1.2.0 | MIDP-2.0 | - | 16 | 16 | 600 | 342833686 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/Nitro Street Racing 3D.jar | 1151 | NitroStreet Racing | - | 1.1.2 | MIDP-2.0 | - | 25 | 16 | 600 | 6934450652 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/Nitro Street Racing.jar | 608 | NitroStreet Racing | - | 1.1.0 | MIDP-2.0 | - | 9 | 16 | 600 | 603300673 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/Off-Road Dirt Motocross.jar | 579 | Xtreme Dirt Bike | - | 1.0.8 | MIDP-2.0 | - | 11 | 16 | 600 | 473424685 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/Orlen Team - Dakar 2007.jar | 159 | Orlen Team - Dakar 2007 | - | 1.0.0 | MIDP-2.0 | - | 10 | 16 | 600 | 6934379 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/Pac-Man - Championship Edition.jar | 697 | PAC-MAN CHAMPIONSHIP EDITION | - | 1.1.0 | MIDP-2.0 | - | 22 | 16 | 600 | 27840332 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/Pac-Man Party.jar | 836 | PAC-MAN Party by Namco | - | 1.0.0 | MIDP-2.0 | - | 5 | 16 | 600 | 28549657 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/Pac-Man.jar | 209 | PAC-MAN | - | 1.8.6 | MIDP-2.0 | - | 44 | 16 | 600 | 17688906 | 0 | javax/bluetooth/BluetoothStateEx | 运行正常 |  |
| vault/Pac-Mania.jar | 169 | PAC-MANIA | - | 1.1.8 | MIDP-2.0 | - | 4 | 16 | 600 | 12124954044 | 0 | - | 运行正常 |  |
| vault/Paperboy - Wheels on Fire.jar | 320 | Paperboy Wheels On Fire | - | 01.00.01 | MIDP-2.0 | - | 11 | 16 | 600 | 2897066 | 0 | - | 运行正常 |  |
| vault/Phantom Duck.jar | 465 | Phantom Duck | - | 1.0.2 | MIDP-1.0 | - | 11 | 16 | 600 | 1964582 | 0 | java/io/EOFException | 运行正常 |  |
| vault/Pirates of the Caribbean - At World's End.jar | 283 | Pirates 3 | - | 1.0.2 | MIDP-1.0 | - | 19 | 16 | 600 | 2748242 | 0 | java/io/EOFException,java/lang/N | 运行正常 |  |
| vault/Pirates of the Seven Seas.jar | 581 | Pirates of the Seven Seas | - | 1.1.6 | MIDP-2.0 | - | 7 | 16 | 600 | 1517883 | 0 | - | 运行正常 |  |
| vault/Platinum Solitaire 2.jar | 763 | Platinum Solitaire 2 | - | 1.1.7 | MIDP-2.0 | - | 8 | 16 | 600 | 13509400 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/Platinum Solitaire 3.jar | 1243 | Platinum Solitaire 3 | - | 1.1.5 | MIDP-2.0 | - | 6 | 16 | 600 | 66574060 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/Platinum Solitaire.jar | 595 | Platinum Solitaire | - | 1.0.12 | MIDP-2.0 | - | 6 | 16 | 600 | 305385259 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/Platinum Sudoku 2.jar | 964 | Platinum Sudoku 2 | - | 1.0.8 | MIDP-2.0 | - | 14 | 16 | 600 | 72399404 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/Platinum Sudoku.jar | 262 | Platinum Sudoku | - | 1.2.2 | MIDP-2.0 | - | 8 | 16 | 600 | 291108007 | 0 | - | 运行正常 |  |
| vault/Pocket Chef.jar | 630 | Pocket Chef | - | 1.0.0 | MIDP-2.0 | - | 12 | 16 | 600 | 84522518 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/Postal Babes.jar | 337 | Postal Babes | - | 1.4.0 | MIDP-2.0 | - | 20 | 16 | 600 | 8236184 | 0 | java/io/EOFException,java/io/IOE | 运行正常 |  |
| vault/Predators.jar | 959 | Predators | - | 1.0.8 | MIDP-2.0 | - | 17 | 16 | 600 | 110755314 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/Prince of Persia (2008).jar | 620 | Prince of Persia | - | 1.0.5 | MIDP-2.0 | - | 6 | 16 | 600 | 635351818 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/Prince of Persia - The Forgotten Sands.jar | 995 | Prince of Persia The Forgott | - | 1.0.7 | MIDP-2.0 | - | 11 | 16 | 600 | 31180620 | 0 | java/lang/ArrayIndexOutOfBoundsE | 运行正常 |  |
| vault/Prince of Persia - The Two Thrones.jar | 306 | Prince Of Persia 3 | - | 1.0.1 | MIDP-2.0 | - | 7 | 16 | 600 | 343346649 | 0 | - | 运行正常 |  |
| vault/Prince of Persia - Warrior Within.jar | 221 | Prince Of Persia - Warrior W | - | 1.0.0 | MIDP-2.0 | - | 7 | 16 | 600 | 429412142 | 0 | - | 运行正常 |  |
| vault/Prince of Persia Classic.jar | 617 | Prince of Persia Classic | - | 1.0.6 | MIDP-2.0 | - | 7 | 16 | 600 | 226197433 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/Pro Evolution Soccer 2008.jar | 346 | PES 2008 | - | 1.0.1 | MIDP-2.0 | - | 8 | 16 | 601 | 519765820 | 0 | java/lang/IndexOutOfBoundsExcept | 运行正常 |  |
| vault/Pro Evolution Soccer 2010.jar | 458 | PES 2010 | - | 1.0.0 | MIDP-2.0 | - | 9 | 16 | 600 | 6218142070 | 0 | java/lang/IndexOutOfBoundsExcept | 运行正常 |  |
| vault/Pro Evolution Soccer 2011.jar | 558 | PES 2011 | - | 1.0.0 | MIDP-2.0 | - | 9 | 16 | 600 | 5127301551 | 0 | java/lang/IndexOutOfBoundsExcept | 运行正常 |  |
| vault/Project Gotham Racing.jar | 941 | PGR | - | 1.0.2 | MIDP-1.0 | - | 45 | 16 | 600 | 2520430 | 0 | - | 运行正常 |  |
| vault/Pub Mania.jar | 566 | PubMania | - | 1.0.1 | MIDP-2.0 | - | 5 | 16 | 600 | 275058540 | 0 | javax/microedition/rms/InvalidRe | 运行正常 |  |
| vault/Puzzle Bobble Evolution.jar | 531 | Puzzle Bobble Evolution | - | 0.8.0 | MIDP-1.0 | - | 17 | 16 | 600 | 4466891 | 0 | java/lang/IllegalArgumentExcepti | 运行正常 |  |
| vault/Puzzle Bobble.jar | 145 | Puzzle Bobble | - | 1.4.8 | MIDP-2.0 | - | 12 | 16 | 600 | 241572 | 0 | java/io/IOException | 运行正常 |  |
| vault/Puzzle Quest - Warlords.jar | 441 | Puzzle Quest - Warlords | - | 1.0.63 | MIDP-1.0 | - | 15 | 16 | 600 | 574263 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/R-Type.jar | 272 | R-Type | - | 0.0.5 | MIDP-2.0 | - | 7 | 16 | 600 | 606469 | 0 | java/io/IOException,java/lang/Nu | 运行正常 |  |
| vault/Race Driver - Grid.jar | 970 | Race Driver Grid | - | 1.0.2 | MIDP-2.0 | - | 66 | 16 | 600 | 12176266 | 0 | - | 运行正常 |  |
| vault/Racing Fever GT.jar | 253 | Racing Fever GT 3D | - | 1.0.6 | MIDP-2.0 | - | 28 | 16 | 600 | 12609122 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
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
| vault/Red Bull Motocross.jar | 834 | RedBullMotoCross2D | - | 0.3.9 | MIDP-2.0 | - | 52 | 16 | 600 | 6251660 | 0 | java/io/EOFException,javax/micro | 运行正常 |  |
| vault/Red Out Racer.jar | 244 | Red Out Racer | - | 1.0.2 | MIDP-2.0 | - | 23 | 16 | 600 | 1890019 | 0 | javax/microedition/media/MediaEx | 运行正常 |  |
| vault/Resident Evil - The Missions.jar | 974 | Resident Evil - The Missions | - | 1.0.0 | MIDP-2.0 | - | 5 | 16 | 601 | 1497848 | 0 | - | 运行正常 |  |
| vault/Ridge Racer Drift.jar | 1268 | RidgeRacerD | - | 1.0.1 | MIDP-2.0 | - | 28 | 16 | 600 | 190565764 | 0 | javax/microedition/rms/InvalidRe | 运行正常 |  |
| vault/Ridge Racer.jar | 398 | Ridge Racer | - | 1.0.7 | MIDP-2.0 | - | 35 | 16 | 600 | 82292567 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/Robobombo.jar | 338 | Robobombo | - | 1.0.7 | MIDP-2.0 | - | 61 | 16 | 600 | 2259389 | 0 | java/lang/ClassNotFoundException | 运行正常 |  |
| vault/Robot Battle Tactics.jar | 321 | Robot Battle Tactics | - | 1.0.0 | MIDP-2.0 | - | 14 | 16 | 600 | 4454379 | 0 | - | 运行正常 |  |
| vault/Roll that Ball 2.jar | 512 | Roll that Ball 2 | - | 1.0.1 | MIDP-2.0 | - | 18 | 16 | 600 | 369622431 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/Roll that Ball.jar | 568 | Roll that Ball | - | 1.0.0 | MIDP-2.0 | - | 18 | 16 | 600 | 330747742 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/Rollercoaster Revolution - 99 Tracks.jar | 845 | Abahn Rev 99 | - | 1.0.10 | MIDP-2.0 | - | 39 | 16 | 600 | 3381751 | 0 | java/io/EOFException,javax/micro | 运行正常 |  |
| vault/Rome - Throne of Destiny.jar | 232 | Rome Throne Of Destiny | - | 1.0.0 | MIDP-2.0 | - | 12 | 16 | 600 | 1170921 | 0 | java/io/IOException,javax/microe | 运行正常 |  |
| vault/SEGA Rally 3D.jar | 958 | SEGA Rally 3D | - | 1.0.5 | MIDP-2.0 | - | 44 | 16 | 600 | 217522597 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/SU-30.jar | 434 | Su-30 | - | 1.8.0 | MIDP-2.0 | - | 55 | 16 | 600 | 4917863 | 0 | java/io/EOFException,java/io/IOE | 运行正常 |  |
| vault/ShadoWalker.jar | 518 | Shadowalker | - | 1.0.3 | MIDP-1.0 | - | 12 | 16 | 600 | 722344 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/Siberian Strike.jar | 206 | Siberian Strike | - | 1.0.0 | MIDP-2.0 | - | 6 | 16 | 600 | 1216158 | 0 | - | 运行正常 |  |
| vault/Silent Hill Mobile 2.jar | 475 | Silent Hill Mobile 2 | - | 1.0.0 | MIDP-2.0 | - | 12 | 16 | 600 | 8286718 | 0 | java/io/IOException,java/lang/Nu | 运行正常 |  |
| vault/Silent Hill Mobile 3.jar | 579 | Silent Hill 3 | - | 1.0.20 | MIDP-2.0 | - | 13 | 16 | 600 | 2680857 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/Silent Hill Mobile.jar | 476 | Silent Hill | - | 0.1.0 | MIDP-2.0 | - | 10 | 16 | 600 | 3291811 | 0 | java/io/IOException,javax/microe | 运行正常 |  |
| vault/SimCity Deluxe.jar | 914 | SimCity Deluxe | - | 1.12.37 | MIDP-2.0 | - | 180 | 16 | 600 | 8488099 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/Snake III.jar | 293 | Snake III | - | 3.07.2 | MIDP-2.0 | - | 49 | 16 | 600 | 131592 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/Snowboard Hero.jar | 751 | SnowboardHero | - | 1.0.1 | MIDP-2.0 | - | 135 | 16 | 600 | 8889770 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/Sonic & SEGA All-Star Racing.jar | 1015 | Sonic Racing | - | 1.2.2 | MIDP-2.0 | - | 24 | 16 | 600 | 8347579 | 0 | - | 运行正常 |  |
| vault/Sonic Advance.jar | 1073 | Sonic Advance | - | 1.3.0 | MIDP-2.0 | - | 174 | 16 | 600 | 2391943 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/Sonic Jump.jar | 328 | Sonic Jump | - | 1.0.3 | MIDP-1.0 | - | 7 | 16 | 602 | 4337055 | 0 | java/io/IOException,javax/microe | 运行正常 |  |
| vault/Sonic Runners Adventures.jar | 966 | Sonic Runners Adventure | - | 1.0.8 | MIDP-2.0 | - | 14 | 16 | 600 | 27246883 | 0 | - | 运行正常 |  |
| vault/Sonic Unleashed.jar | 848 | SonicUnleashed | - | 1.2.5 | MIDP-2.0 | - | 15 | 16 | 600 | 183726898 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/Soul Calibur Mobile.jar | 1334 | SOULCALIBUR Mobile by Namco | - | 1.0.0 | MIDP-2.0 | - | 14 | 16 | 600 | 6769462 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/Soul of Darkness.jar | 631 | Soul Of Darkness | - | 1.3.2 | MIDP-2.0 | - | 8 | 16 | 600 | 363502553 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/Speed Racer.jar | 886 | Speed Racer 3D | - | 1.2.9 | MIDP-2.0 | - | 26 | 16 | 600 | 842903 | 0 | - | 运行正常 |  |
| vault/Spider-Man - Toxic City.jar | 995 | Spider-Man: Toxic City | - | 1.1.6 | MIDP-2.0 | - | 15 | 16 | 600 | 95483971 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/Spider-Man - Ultimate Power.jar | 1542 | Spider-Man: Ultimate Power | - | 1.2.3 | MIDP-2.0 | - | 18 | 16 | 600 | 69901982 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/Splatterhouse.jar | 549 | Splatterhouse by Namco | - | 1.0.0 | MIDP-2.0 | - | 14 | 16 | 600 | 13180845 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/Spongebob - Bikini Bottom Pursuit.jar | 390 | Bob Esponja | - | 1.1.0 | MIDP-2.0 | - | 21 | 16 | 600 | 496533 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/Spore Creatures.jar | 758 | SPORE CREATURES | - | 4.0.52 | MIDP-2.0 | - | 42 | 16 | 600 | 63668253 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/Street Fighter Alpha - Warriors' Dreams.jar | 1041 | Street Fighter Alpha | - | 1.1.5 | MIDP-2.0 | - | 19 | 16 | 600 | 59446444 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/Super Bomberman.jar | 150 | Super Bomberman | - | 1.1.1 | MIDP-1.0 | - | 4 | 16 | 600 | 4918907 | 0 | java/lang/ArrayIndexOutOfBoundsE | 运行正常 |  |
| vault/Super Breakout.jar | 222 | Super Breakout | - | 1.0.4 | MIDP-2.0 | - | 28 | 16 | 600 | 31839645 | 0 | java/lang/ClassCastException,jav | 运行正常 |  |
| vault/Super Monkey Ball - Tip'n Tilt 2.jar | 300 | SMB TNT2 | - | 1.2.8 | MIDP-2.0 | - | 28 | 16 | 600 | 532390 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/Super Monkey Ball - Tip'n Tilt.jar | 362 | Super Monkey Ball Tip n Tilt | - | 1.0.1 | MIDP-2.0 | - | 7 | 16 | 600 | 7179413 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/Super Puzzle Bobble.jar | 142 | Super Puzzle Bobble | - | 1.4.8 | MIDP-2.0 | - | 12 | 16 | 600 | 231472 | 0 | java/io/IOException | 运行正常 |  |
| vault/Super Water Bomber.jar | 318 | Super Water Bomber | - | 1.0.3 | MIDP-2.0 | - | 18 | 16 | 600 | 12200233 | 0 | java/io/EOFException,javax/micro | 运行正常 |  |
| vault/Superman & Batman - Heroes United.jar | 457 | Superman Batman Heroes Unite | - | 1.0.5 | MIDP-2.0 | - | 6 | 16 | 600 | 2899754 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/Tekken Mobile.jar | 759 | TEKKEN MOBILE | - | 1.0.7 | MIDP-2.0 | - | 32 | 16 | 600 | 5411541 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/Temple Rush 2.jar | 267 | Temple Rush 2 | - | 1.0.4 | MIDP-2.0 | - | 21 | 16 | 600 | 319098 | 0 | java/io/IOException,java/lang/St | 运行正常 |  |
| vault/Temple Rush.jar | 269 | Temple Rush | - | 1.0.3 | MIDP-2.0 | - | 21 | 16 | 600 | 319161 | 0 | java/io/IOException,java/lang/St | 运行正常 |  |
| vault/Terminator Salvation.jar | 939 | Terminator Salvation | - | 1.1.0 | MIDP-2.0 | - | 8 | 16 | 600 | 243872880 | 0 | - | 运行正常 |  |
| vault/Tetris (2008).jar | 102 | Tetris | - | 1.21.29 | MIDP-1.0 | - | 57 | 16 | 600 | 1432780 | 0 | java/util/NoSuchElementException | 运行正常 |  |
| vault/Tetris Mobile.jar | 361 | TETRIS® | - | 1.0.0 | MIDP-2.0 | - | 34 | 16 | 600 | 8616310 | 0 | - | 运行正常 |  |
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
| vault/Tomb Raider - Anniversary.jar | 291 | TombRaiderAnniversary | - | 0.2.8 | MIDP-2.0 | - | 4 | 16 | 600 | 1154824 | 0 | java/lang/ArrayIndexOutOfBoundsE | 运行正常 |  |
| vault/Tomb Raider - Legend Tokyo.jar | 516 | Tomb Raider Legend | - | 1.15.41 | MIDP-2.0 | - | 21 | 16 | 600 | 1187578 | 0 | - | 运行正常 |  |
| vault/Tomb Raider - Underworld.jar | 1127 | TR Underworld | - | 1.7.0 | MIDP-1.0 | - | 7 | 16 | 600 | 12861635 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/Tony Hawk - Vert.jar | 458 | Tony Hawk VERT | - | 1.0.0 | MIDP-2.0 | - | 49 | 16 | 600 | 5812065 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/Top Gear - The Mobile Game.jar | 604 | Top Gear: the mobile game | - | 1.1.0 | MIDP-2.0 | - | 17 | 16 | 600 | 77632713 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/Tornado Mania 3D.jar | 455 | 3D Tornado Mania | - | 1.0.0 | MIDP-2.0 | - | 24 | 16 | 600 | 12093672 | 0 | java/io/EOFException,java/lang/N | 运行正常 |  |
| vault/Tornado Mania.jar | 339 | Tornado Mania! | - | 1.1.0 | MIDP-2.0 | - | 10 | 16 | 600 | 13843036 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/Totally Spies.jar | 306 | TotallySpies! | - | 1.0.0 | MIDP-2.0 | - | 8 | 16 | 600 | 111300189 | 0 | - | 运行正常 |  |
| vault/Tower Bloxx - My City.jar | 927 | TBloxx My City | - | 1.0.4 | MIDP-2.0 | - | 39 | 16 | 600 | 17704855 | 0 | java/io/EOFException,javax/micro | 运行正常 |  |
| vault/Tower Bloxx - New York.jar | 1088 | Tower Bloxx NY | - | 1.0.0 | MIDP-2.0 | - | 56 | 16 | 600 | 5546675 | 0 | java/io/EOFException,javax/micro | 运行正常 |  |
| vault/Tower Bloxx - Revolution.jar | 779 | TowerBloxxRev | - | 1.0.2 | MIDP-2.0 | - | 52 | 16 | 600 | 5325095 | 0 | java/io/EOFException,javax/micro | 运行正常 |  |
| vault/Tower Bloxx Deluxe.jar | 426 | Tower Bloxx Deluxe | - | 1.0.2 | MIDP-2.0 | - | 20 | 16 | 600 | 12265510 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/Tower Bloxx.jar | 165 | Tower Bloxx(TM) | - | 1.2.6 | MIDP-1.0 | - | 16 | 16 | 600 | 12653534 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/Tower Mania.jar | 247 | Tower Mania | - | 1.0.1 | MIDP-2.0 | - | 22 | 16 | 600 | 13025299 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/Treasure Grab.jar | 443 | Treasure Grab | - | 1.0.44 | MIDP-1.0 | - | 10 | 16 | 600 | 196792598 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/Tropical Madness.jar | 309 | Tropical Madness | - | 1.1.5 | MIDP-2.0 | - | 6 | 16 | 600 | 38446869 | 0 | - | 运行正常 |  |
| vault/Turtle Dash.jar | 825 | Turtle Dash | - | 1.0.0 | MIDP-2.0 | - | 34 | 16 | 600 | 3021039 | 0 | java/io/EOFException,javax/micro | 运行正常 |  |
| vault/Ultimate Street Football.jar | 462 | Street Football | - | 1.0.0 | MIDP-2.0 | - | 8 | 16 | 600 | 450672577 | 0 | - | 运行正常 |  |
| vault/Ultra Phoenix - Resurgence.jar | 221 | Ultra Phoenix - Resurgence | - | 1.1.3 | MIDP-2.0 | - | 30 | 16 | 600 | 7783282 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/V-Rally 3D.jar | 304 | V-Rally 3D | - | 1.5.2 | MIDP-2.0 | - | 99 | 16 | 600 | 293785 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/Vodafone McLaren Mercedes Team Racing.jar | 202 | VMM Team Racing by NH | - | 1.0.10 | MIDP-1.0 | - | 14 | 16 | 600 | 613357 | 0 | java/io/EOFException,java/lang/N | 运行正常 |  |
| vault/War Hero 1944.jar | 278 | War Hero 1944 Shared by Mate | - | 1.0.62 | MIDP-1.0 | - | 15 | 16 | 600 | 12641036 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/Watchmen.jar | 607 | Watchmen | - | 1.3.3 | MIDP-2.0 | - | 125 | 16 | 600 | 3951695 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vault/Wild West Guns.jar | 1472 | Wild West Guns | - | 1.2.6 | MIDP-2.0 | - | 17 | 16 | 600 | 189574104 | 0 | java/lang/NullPointerException | 运行正常 |  |
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
| vault/Zombie Infection 2.jar | 1224 | Zombie Infection 2 | - | 1.1.9 | MIDP-2.0 | - | 13 | 16 | 600 | 202116318 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/Zombie Infection.jar | 1013 | Zombie Infection | - | 1.2.0 | MIDP-2.0 | - | 8 | 16 | 601 | 22099539 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| vault/Zombie Mob Defense.jar | 772 | Zombie Mob | - | 1.0.0 | MIDP-2.0 | - | 39 | 16 | 600 | 4088760 | 0 | java/io/EOFException,javax/micro | 运行正常 |  |
| vault/Zombiewood.jar | 820 | Zombiewood | - | 1.2.9 | MIDP-2.0 | - | 42 | 16 | 600 | 93994054 | 0 | java/lang/NullPointerException | 运行正常 |  |
| vault/Zoo Keeper.jar | 366 | ZOO KEEPER | - | 1.1.1 | MIDP-2.0 | - | 9 | 16 | 600 | 1274023133 | 0 | - | 运行正常 |  |
| vault/Zuma's Revenge!.jar | 1118 | Zumas Revenge | - | 00.01.47 | MIDP-2.0 | - | 8 | 16 | 601 | 120025308 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| vnes.jar | 148 | vNes | see the source proje | 1.0.0 | - | nokia-ui | 67 | 16 | 600 | 1117091 | 0 | java/io/IOException,java/lang/Nu | 运行正常 |  |
| 七龙珠－终极原始大乱斗_5589.jar | 986 | 七龙珠-终极原始大乱斗 | 无限天机 | 1.0.0 | MIDP-1.0 | - | 26 | 16 | 600 | 183996 | 0 | - | 运行正常 |  |
| 三国争霸之赤壁大战_11681.jar | 233 | 三国争霸之赤壁大战 | MIG | 1.0.0 | MIDP-1.0 | - | 11 | 16 | 600 | 32223 | 0 | - | 运行正常 |  |
| 三国塔防－曹操传_1610.jar | 1066 | 三国塔防-曹操传 | CNMOSUN | 2.0.1 | MIDP-1.0 | nokia-ui | 148 | 16 | 600 | 24096525 | 0 | java/lang/ArrayIndexOutOfBoundsE | 运行正常 |  |
| 三国志_10617.jar | 213 | C2S－三国志 | 中计科创 | 1.0 | MIDP-1.0 | - | 2 | 16 | 600 | 200119561 | 0 | - | 运行正常 |  |
| 三国志吞食天地_8144.jar | 513 | 三国志：吞食天地 | CAPCOM / BiNPDA | 1.0.2 | MIDP-2.0 | - | 4 | 16 | 600 | 304541 | 0 | java/lang/NullPointerException | 运行正常 |  |
| 三国志霸王的大陆V3.2.2_5005.jar | 674 | BWDL | JustGame.Studio | 1.0 | MIDP-2.0 | - | 7 | 16 | 600 | 451610 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| 三国杀2012_809.jar | 1203 | sanguosha | un | 1.0 | MIDP-1.0 | nokia-ui | 81 | 16 | 600 | 12212673 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| 三国杀2．2美化版_1347.jar | 1171 | SanGuoQ | UMLife | 2.2 | MIDP-2.0 | - | 376 | 16 | 600 | 5492974 | 0 | - | 运行正常 |  |
| 三国杀单机BT版_4227.jar | 318 | 三国杀 | M2me | 1.0.0 | MIDP-2.0 | - | 17 | 16 | 600 | 608192 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| 三国杀单机战功修改版_4143.jar | 894 | SGSJB | M2me | 1.0.0 | MIDP-2.0 | - | 17 | 16 | 600 | 8627078 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| 三国杀单机版_4312.jar | 895 | SGSJB | M2me | 1.0.0 | MIDP-2.0 | - | 17 | 16 | 600 | 8627078 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| 三国群英传_11539.jar | 58 | 奥汀-三国群英传正版 | 数字鱼移动娱乐 | 1.0 | MIDP-1.0 | nokia-ui | 9 | 16 | 600 | 114859 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| 三国群雄争霸_9023.jar | 271 | 超值-三国群雄争霸 | 北京金建衡 | 1.0 | MIDP-1.0 | nokia-ui | 3 | 16 | 600 | 127500 | 0 | - | 运行正常 |  |
| 三国赤壁小乔秘史_7976.jar | 643 | 小乔秘史wap.91soyo.com破解 | HaKuNa | 1.0.0 | MIDP-2.0 | nokia-ui | 24 | 16 | 600 | 193433363 | 0 | java/lang/NullPointerException | 运行正常 |  |
| 世嘉头文字D－宿命对决_1247.jar | 1008 | 世嘉头文字D-宿命对决 | BMIT | 1.0.0 | MIDP-1.0 | m3g | 159 | 16 | 600 | 85355 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| 世界五子棋大奖赛_355.jar | 426 | 世界五子棋大奖赛 | 阿里奥斯 | 1.0 | MIDP-1.0 | - | 9 | 16 | 600 | 39963 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| 世界冠军拉力赛3D_8360.jar | 635 | WRC 3D | I-play | 0.8.4 | MIDP-2.0 | m3g | 4 | 16 | 600 | 5349785 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| 世界最强-银星围棋_2631.jar | 402 | wjzq | 阿里奥斯 | 1.0 | MIDP-1.0 | - | 24 | 16 | 600 | 136019 | 0 | - | 运行正常 |  |
| 世界武术巡回赛教学版_3484.jar | 442 | 世界武术巡回赛-教学版 | ifeng | 0.3.3 | MIDP-2.0 | - | 8 | 16 | 600 | 164416071 | 0 | - | 运行正常 |  |
| 世界足球2012中文版_1724.jar | 881 | 世界足球2012 | Gameloft SA | 1.0.0 | MIDP-2.0 | - | 16 | 16 | 600 | 1875230 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| 世界足球2012中文版_780.jar | 881 | 世界足球2012 | Gameloft SA | 1.0.0 | MIDP-2.0 | - | 16 | 16 | 600 | 1875230 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| 丧尸危机2-自由之翼BT版_997.jar | 772 | 丧尸危机2试玩版 | moonic | 1.0 | MIDP-2.0 | - | 124 | 16 | 600 | 570673 | 0 | - | 运行正常 |  |
| 丧尸围城_10590.jar | 289 | Dead Rising | Capcom Interactive,  | 1.0.1 | MIDP-2.0 | - | 43 | 16 | 600 | 4233667 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| 中华麻将馆－极限胡牌版_244.jar | 310 | 中华麻将馆-极限胡牌版 | CNDColor | 1.0 | MIDP-1.0 | - | 18 | 16 | 600 | 308569765 | 0 | - | 运行正常 |  |
| 中国象棋_10893.jar | 293 | 中国象棋三国经典版 | 掌中地带 | 1.0 | MIDP-2.0 | - | 9 | 16 | 600 | 73495 | 0 | javax/microedition/rms/InvalidRe | 运行正常 |  |
| 中国象棋之水墨珍藏版_871.jar | 375 | 中国象棋之水墨珍藏版 | zhangyoutianxia | 1.0.0 | MIDP-2.0 | - | 12 | 16 | 600 | 534667883 | 0 | java/lang/ArrayIndexOutOfBoundsE | 运行正常 |  |
| 乱世Q三国_1649.jar | 977 | 乱世Q三国 | - | 1.0 | MIDP-1.0 | - | 232 | 16 | 0 | 128 | 0 | javax/microedition/rms/RecordSto | 未呈现画面 | exited itself (notifyDestroyed) without pain |
| 乱舞三国之群英会_2972.jar | 817 | 乱舞三国之群英会 | 掌趣科技 | 1.0 | MIDP-1.0 | - | 35 | 16 | 600 | 482917 | 0 | java/lang/NullPointerException | 运行正常 |  |
| 五子棋_10940.jar | 42 | XOXO | 3D Reality | 1.0 | MIDP-1.0 | - | 24 | 16 | 600 | 4341300 | 0 | - | 运行正常 |  |
| 仙剑奇侠传(2008)_12406.jar | 695 | pal | softstarmobile | 1.0 | MIDP-1.0 | nokia-ui | 3 | 16 | 600 | 209727 | 0 | - | 运行正常 |  |
| 仙剑奇侠传_10388.jar | 297 | 仙剑奇侠传 | Icepeach/Kim1997 | 0.0.1 | MIDP-2.0 | - | 6 | 16 | 600 | 11599230 | 0 | - | 运行正常 |  |
| 仙剑奇侠传完美版_11857.jar | 360 | 仙剑奇侠传 | kim1997 | 06.03.10 | MIDP-1.0 | nokia-ui | 4 | 16 | 600 | 115864 | 0 | - | 运行正常 |  |
| 仙逆－诛仙弑神_1441.jar | 1356 | 仙逆-诛仙弑神 | Joymeng | 1.0.0 | MIDP-1.0 | - | 48 | 16 | 600 | 293187111 | 0 | java/io/IOException,java/lang/Cl | 运行正常 |  |
| 仙魔飞行棋_417.jar | 294 | 仙魔飞行棋 | 久盛易途 | 1.0 | MIDP-2.0 | - | 21 | 16 | 600 | 516863 | 0 | - | 运行正常 |  |
| 众神殿之王子归来_8404.jar | 103 | 王子归来 | GTS | 1.0.0 | MIDP-2.0 | nokia-ui | 14 | 16 | 600 | 1293718 | 0 | - | 运行正常 |  |
| 使命召唤4现代战争_9388.jar | 449 | Call of Duty 4 by serviak | Glu Mobile, serviak | 1.0.4 | MIDP-2.0 | - | 5 | 16 | 600 | 2386864 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| 使命召唤5-战争世界中文版_4583.jar | 416 | 使命召唤5-战争世界 | TOM | 1.0.1 | MIDP-2.0 | - | 22 | 16 | 600 | 426494410 | 0 | java/lang/IllegalArgumentExcepti | 运行正常 |  |
| 侠盗飞车_6246.jar | 360 | 侠盗飞车-Gameloft正版 | Gameloft SA | 1.0.0 | MIDP-2.0 | - | 6 | 16 | 600 | 120467146 | 0 | - | 运行正常 |  |
| 侠盗飞车加强版_5640.jar | 350 | 侠盗飞车-Gameloft正版 | Gameloft SA | 1.00 | MIDP-2.0 | - | 11 | 16 | 600 | 182566374 | 0 | javax/microedition/rms/InvalidRe | 运行正常 |  |
| 俄罗斯方块中文版_2832.jar | 121 | 俄罗斯方块 | 科讯达 | 1.0.0 | MIDP-2.0 | - | 56 | 16 | 600 | 394288 | 0 | - | 运行正常 |  |
| 倚天屠龙之君临天下_12230.jar | 564 | 倚天屠龙之君临天下 | 上海乐游 | 1.0 | MIDP-2.0 | - | 64 | 16 | 600 | 2763649 | 0 | java/lang/NumberFormatException, | 运行正常 |  |
| 倚天屠龙记_4827.jar | 836 | 倚天屠龙记 | asionsky | 1.0 | MIDP-1.0 | nokia-ui | 62 | 16 | 600 | 3408045 | 0 | - | 运行正常 |  |
| 倚天屠龙记之魔教教主_4663.jar | 798 | GEYTTJZ | YFLH | 1.0.4 | MIDP-2.0 | - | 7 | 16 | 600 | 282392814 | 0 | - | 运行正常 |  |
| 傲世三国志－群雄争霸_5633.jar | 514 | 傲世三国志-群雄争霸 | 天地游 | 1.0 | MIDP-1.0 | nokia-ui | 15 | 16 | 600 | 344067537 | 0 | - | 运行正常 |  |
| 傲气雄鹰_10432.jar | 249 | 傲气雄鹰汉化版 | Quake漢化 | 1.08 | MIDP-2.0 | - | 10 | 16 | 600 | 849272 | 0 | - | 运行正常 |  |
| 傲视三国之赵云传_11024.jar | 235 | 傲视三国之赵云传 | 掌上神通 | 1.0 | MIDP-1.0 | nokia-ui | 22 | 16 | 600 | 388550 | 0 | - | 运行正常 |  |
| 僵尸危机2_4349.jar | 1068 | Zombie Infection 2 | Gameloft SA | 1.2.3 | MIDP-2.0 | - | 12 | 16 | 600 | 6006772722 | 0 | java/lang/ArrayIndexOutOfBoundsE | 运行正常 |  |
| 兔子波比2冰原之旅汉化版_11509.jar | 68 | BC2 - Winterland | FDGSoft | 1.0.0 | MIDP-1.0 | nokia-ui | 11 | 16 | 600 | 82438 | 0 | - | 运行正常 |  |
| 冠军足球经理2010汉化版_5198.jar | 543 | CM-2010 | SoGoYo.cn | 1.2.1 | MIDP-2.0 | nokia-ui | 21 | 16 | 600 | 32142124 | 0 | - | 运行正常 |  |
| 冰淇淋少女成长日记_7307.jar | 344 | 冰淇淋少女成长日记_玉品 | 杭州超易 | 1.0 | MIDP-2.0 | nokia-ui | 9 | 16 | 600 | 470532 | 0 | - | 运行正常 |  |
| 凡人修仙传2-乱星海_12474.jar | 979 | 凡人修仙传2-乱星海 | Gamewave | 1.00 | MIDP-1.0 | - | 18 | 16 | 600 | 151226 | 0 | java/lang/NullPointerException | 运行正常 |  |
| 凡人修仙传之七派六宗_4145.jar | 984 | 凡人修仙传 | Gamewave | 1.0.0 | MIDP-1.0 | nokia-ui | 16 | 16 | 600 | 156258 | 0 | java/io/IOException,java/lang/Nu | 运行正常 |  |
| 凤凰战机_10636.jar | 216 | Ultra Phoenix - Resurgence | Uwan | 1.1.3 | MIDP-2.0 | - | 32 | 16 | 600 | 6802252 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| 初恋物语_7612.jar | 286 | 初恋物语 | 掌上天空 | 1.0 | MIDP-1.0 | - | 9 | 16 | 600 | 35787 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| 刺客信条3_266.jar | 1623 | Assassin's Creed III | mob.ua | 1.1.1 | MIDP-2.0 | nokia-ui | 12 | 16 | 600 | 117335556 | 0 | java/lang/NullPointerException | 运行正常 |  |
| 刺客信条兄弟会中文版_3668.jar | 1213 | 刺客信条-兄弟会 | 空中猛犸 | 1.0.0 | MIDP-2.0 | - | 12 | 16 | 600 | 14060232 | 0 | java/lang/NullPointerException | 运行正常 |  |
| 剑侠奇缘-玉女问情_2208.jar | 703 | 剑侠奇缘-玉女问情 | songge | 1.0 | MIDP-1.0 | - | 30 | 16 | 600 | 1778988 | 0 | - | 运行正常 |  |
| 剑侣情缘Ⅱ魔神至尊传(无bug)_12464.jar | 904 | 剑侣情缘Ⅱ魔神至尊传 | songge | 1.0 | MIDP-1.0 | - | 20 | 16 | 600 | 513439 | 0 | - | 运行正常 |  |
| 剑灵－古剑魂梦_676.jar | 1531 | 剑灵-古剑魂梦 | ty | 1.0.0 | MIDP-2.0 | nokia-ui | 50 | 16 | 600 | 56540 | 0 | - | 运行正常 |  |
| 午夜保龄球2中文版_9782.jar | 589 | 终极保龄球2 | Gameloft SA | 1.0.0 | MIDP-2.0 | nokia-ui | 12 | 16 | 600 | 427644108 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| 午夜台球圣诞中文版_8677.jar | 221 | 极限台球挑战赛 | Gameloft SA | 1.04 | MIDP-2.0 | - | 6 | 16 | 600 | 252490978 | 0 | - | 运行正常 |  |
| 危城无双之三国群英_4891.jar | 220 | 危城无双 | 雪鲤鱼科技 | 1.0 | MIDP-1.0 | nokia-ui | 2 | 16 | 600 | 247186600 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| 反恐精英2正式版_5836.jar | 510 | CT episod 2 | NET Lizard | 1.0 | MIDP-2.0 | nokia-ui | 21 | 16 | 600 | 8254105 | 0 | java/io/IOException | 运行正常 |  |
| 口袋仓鼠_11936.jar | 441 | Lab Hamster\gkataria | Sprite Interactive L | 1.0 | MIDP-2.0 | - | 23 | 16 | 600 | 5316027 | 0 | java/lang/NullPointerException | 运行正常 |  |
| 口袋妖怪_7872.jar | 54 | 口袋妖怪 | DigiFUN | 1.0 | MIDP-1.0 | nokia-ui | 8 | 16 | 600 | 852934 | 0 | - | 运行正常 |  |
| 口袋妖怪水晶汉化版_4524.jar | 1067 | MeBoy | Bjorn Carlin, www.ar | 1.5.0 | MIDP-2.0 | - | 7 | 16 | 600 | 643 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| 口袋灵兽_9902.jar | 582 | 口袋灵兽 | lxs | 1.0 | MIDP-2.0 | nokia-ui | 41 | 16 | 600 | 47519 | 0 | - | 运行正常 |  |
| 古剑奇缘-诛仙_4913.jar | 954 | 古剑奇缘-诛仙 | x6game | 1.0.0 | MIDP-1.0 | - | 64 | 16 | 600 | 46107181 | 0 | java/lang/NumberFormatException | 运行正常 |  |
| 台球传奇_87.jar | 917 | 台球传奇 | qqgame | 1.0 | MIDP-1.0 | - | 53 | 16 | 600 | 403070 | 0 | java/lang/NumberFormatException, | 运行正常 |  |
| 史前部落_11059.jar | 486 | Prehistoric Tribes | THQ | 0.1.28 | MIDP-2.0 | - | 7 | 16 | 600 | 143811972 | 0 | - | 运行正常 |  |
| 合金弹头2011_5629.jar | 401 | 合金弹头2011 | s | 1.0 | MIDP-1.0 | - | 7 | 16 | 600 | 465334397 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| 合金弹头2013BT版_678.jar | 1021 | 合金弹头2013(正版) | GAMECHO | 1.0 | MIDP-1.0 | - | 19 | 16 | 600 | 244487 | 0 | java/lang/ClassNotFoundException | 运行正常 |  |
| 合金弹头4(完美汉化版)_4053.jar | 798 | Metal Slug 4 | I-play | 1.0.1 | MIDP-2.0 | - | 6 | 16 | 600 | 3828627 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| 周末的沙滩_7453.jar | 1480 | Beach Games 12-Pack | Digital Chocolate, I | 1.0.0 | MIDP-2.0 | nokia-ui | 37 | 16 | 600 | 3006903 | 0 | java/io/EOFException,javax/micro | 运行正常 |  |
| 哆啦A梦-秘密道具大作战_6557.jar | 552 | 哆啦A梦-秘密道具大作战(正版) | CNMOSUN | 1.0 | MIDP-1.0 | nokia-ui | 26 | 16 | 600 | 548955346 | 0 | - | 运行正常 |  |
| 哆啦A梦运动会_10939.jar | 348 | 哆啦A梦运动会(正版)W | cnmosun | 1.0.0 | MIDP-1.0 | - | 17 | 16 | 600 | 542524 | 0 | javax/microedition/media/MediaEx | 运行正常 |  |
| 哆啦A梦－3D卡丁车增值版_1560.jar | 1104 | 哆啦A梦-3D卡丁车 | CNMOSUN | 2.01 | MIDP-1.0 | nokia-ui | 73 | 16 | 600 | 5721931 | 0 | java/io/IOException,java/lang/Nu | 运行正常 |  |
| 喜羊羊与灰太狼_9241.jar | 296 | 喜羊羊与灰太狼 | 积创网络 | 1.0.0 | MIDP-2.0 | nokia-ui | 11 | 16 | 600 | 180177712 | 0 | - | 运行正常 |  |
| 国色天香MM斗地主真人版_11364.jar | 324 | 国色天香-MM斗地主(真人版) | 北京金建衡 | 1.0 | MIDP-1.0 | - | 6 | 16 | 600 | 93953 | 0 | javax/microedition/rms/InvalidRe | 运行正常 |  |
| 国色天香斗地主_1364.jar | 1249 | 国色天香斗地主 | CWA | 1.0 | MIDP-2.0 | nokia-ui | 58 | 16 | 600 | 17379876 | 0 | java/lang/NumberFormatException | 运行正常 |  |
| 圣百合女子学园-纯色序曲_4875.jar | 993 | SaintLily | GemmyPlanet | 1.0.0 | MIDP-2.0 | - | 12 | 16 | 600 | 445931 | 0 | - | 运行正常 |  |
| 地下城与勇士之鬼剑士_9408.jar | 381 | 地下城与勇士-鬼剑士 | ZIO | 1.0.0 | MIDP-2.0 | - | 27 | 16 | 600 | 11220086 | 0 | - | 运行正常 |  |
| 地心探险(地心游记)_12471.jar | 94 | 新浪-地心游记 | 新浪无线 | 1.0 | MIDP-2.0 | - | 9 | 16 | 600 | 4267624621 | 0 | - | 运行正常 |  |
| 坦克大战（经典版）_190.jar | 570 | 3D坦克大战(经典版) | 南京浩普 | 1.0 | MIDP-1.0 | - | 18 | 16 | 600 | 1461959 | 0 | - | 运行正常 |  |
| 塔塔_2848.jar | 693 | 塔塔 | gamecho | 1.0 | MIDP-1.0 | - | 6 | 16 | 600 | 9377 | 0 | java/lang/ClassNotFoundException | 运行正常 |  |
| 夜半鬼吹灯_8363.jar | 222 | 夜半鬼吹灯 | 浙江广视 | 1.0 | MIDP-2.0 | nokia-ui | 5 | 16 | 600 | 26581 | 0 | - | 运行正常 |  |
| 夜店3D斗地主_2296.jar | 847 | 夜店3D斗地主 | UU | 1.0.0 | MIDP-2.0 | - | 15 | 16 | 600 | 22189 | 0 | javax/microedition/rms/InvalidRe | 运行正常 |  |
| 大富翁4纵横四海_8749.jar | 273 | 大富翁4纵横四海 | 伟特力群 | 1.0 | MIDP-1.0 | - | 13 | 16 | 600 | 341626 | 0 | - | 运行正常 |  |
| 大富翁4－大宇正版_467.jar | 636 | 大富翁4大宇正版 | TOM | 1.0 | MIDP-2.0 | - | 17 | 16 | 600 | 7762359 | 0 | - | 运行正常 |  |
| 大富翁中国之旅_11873.jar | 97 | DFWZGZL | WTLQ | 1.0 | MIDP-2.0 | - | 10 | 16 | 600 | 33580 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| 大魔界村_11996.jar | 1234 | Ghouls N Ghosts | Capcom/BiNPDA | 1.1.64 | MIDP-2.0 | nokia-ui | 103 | 16 | 600 | 2020218 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| 大鱼吃小鱼－深海探险2_614.jar | 633 | 大鱼吃小鱼-深海探险2 | PFUmobile | 1.0.0 | MIDP-1.0 | - | 63 | 16 | 600 | 130579 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| 太空侵略者3_archive.jar | 85 | Space Impact III | Nokia goto www.nokia | 1.2 | MIDP-1.0 | nokia-ui | 11 | 16 | 600 | 499835 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| 太空冲击303_archive.jar | 85 | Space Impact III | Nokia goto www.nokia | 1.2 | MIDP-1.0 | nokia-ui | 11 | 16 | 600 | 500106 | 0 | - | 运行正常 |  |
| 头文字D之疾风骤雨_2140.jar | 938 | 世嘉头文字D-疾风骤雨 | KongZhong | 1.0.0 | MIDP-2.0 | m3g | 26 | 16 | 600 | 81788 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| 奥特曼-宇宙英雄超银河传说_2233.jar | 1245 | 奥特曼-宇宙英雄超银河传说 | forcegen | 1.0.0 | MIDP-1.0 | - | 139 | 16 | 600 | 38517 | 0 | javax/microedition/media/MediaEx | 运行正常 |  |
| 女仆咖啡厅_1244.jar | 640 | 女仆咖啡厅 | suole | 1.0 | MIDP-2.0 | - | 42 | 16 | 600 | 44674 | 0 | - | 运行正常 |  |
| 女神转校生-心跳回忆_4993.jar | 908 | 女神转校生-心跳回忆 | NPC Interactive.Inc | 1.0 | MIDP-2.0 | - | 58 | 16 | 600 | 1672691 | 0 | java/lang/ClassNotFoundException | 运行正常 |  |
| 孢子生命起源_6515.jar | 658 | EA-孢子生命起源 | 掌趣科技 | 1.0.0 | MIDP-2.0 | - | 42 | 16 | 600 | 43089 | 0 | - | 运行正常 |  |
| 孤岛余生2时间之谜汉化版_7371.jar | 616 | 孤岛余生2 | Glu Mobile serviak | 1.0.3 | MIDP-2.0 | m3g | 36 | 16 | 600 | 2117892 | 0 | javax/microedition/rms/InvalidRe | 运行正常 |  |
| 孤岛余生汉化版_10145.jar | 453 | Stranded | Glu Mobile | 1.0.1 | MIDP-1.0 | - | 9 | 16 | 600 | 591789 | 0 | - | 运行正常 |  |
| 学姐的秘密_9071.jar | 121 | 学姐的秘密 | Unknown | 1.0 | MIDP-2.0 | - | 10 | 16 | 600 | 74704 | 0 | - | 运行正常 |  |
| 宇宙战机_10521.jar | 175 | Triad | Inlogic Software Ltd | 1.0.0 | MIDP-2.0 | - | 19 | 16 | 600 | 12128087 | 0 | java/lang/ClassNotFoundException | 运行正常 |  |
| 实况足球2008中文版_9284.jar | 567 | 世界足球2008-明星正版 | Gameloft SA | 1.01 | MIDP-2.0 | - | 7 | 16 | 600 | 141318119 | 0 | - | 运行正常 |  |
| 实况足球2012_2122.jar | 905 | Real Football 2012 | Gameloft SA | 1.0.5 | MIDP-2.0 | - | 14 | 16 | 600 | 156941479 | 0 | java/lang/NullPointerException | 运行正常 |  |
| 实况足球经理2009_10609.jar | 652 | RF MANAGER 09 | Gameloft SA | 1.1.3 | MIDP-2.0 | nokia-ui | 8 | 16 | 600 | 527361808 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| 宠物之王_12445.jar | 933 | 91soyo.com-宠物之王 | moonic | 1.0 | MIDP-2.0 | - | 63 | 16 | 600 | 1700904 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| 宠物狗狗中文版_11635.jar | 253 | 无敌Z狗狗 | Gameloft SAK47 | 1.02 | MIDP-2.0 | nokia-ui | 6 | 16 | 600 | 351410103 | 0 | java/lang/NullPointerException | 运行正常 |  |
| 宠物王国2紫_5550.jar | 438 | cwwg2_z | 华娱无线 | 1.0.0 | MIDP-1.0 | nokia-ui | 19 | 16 | 600 | 507396 | 0 | - | 运行正常 |  |
| 宠物王国3金_4892.jar | 585 | 宠物王国3(宝软网baoruan.com免费分享 | 华娱无线 | 1.0 | MIDP-1.0 | nokia-ui | 22 | 16 | 600 | 143450 | 0 | - | 运行正常 |  |
| 宠物王国4-白金_6531.jar | 699 | 宠物王国4-白金 | CWA | 1.0 | MIDP-2.0 | nokia-ui | 16 | 16 | 600 | 603400 | 0 | java/lang/NumberFormatException | 运行正常 |  |
| 宠物王国5-彩虹_3413.jar | 1029 | 宠物王国5-彩虹 | CWA | 1.0 | MIDP-2.0 | nokia-ui | 23 | 16 | 600 | 124222 | 0 | - | 运行正常 |  |
| 宠物王国5-彩虹二次BT版_3356.jar | 1125 | 宠物王国5-彩虹 | CWA | 1.0 | MIDP-2.0 | nokia-ui | 28 | 16 | 600 | 133927 | 0 | - | 运行正常 |  |
| 宠物王国6(去模拟器限制)_12473.jar | 1387 | PetKing6 | CWA | 1.0 | MIDP-2.0 | - | 72 | 16 | 600 | 2718209 | 0 | java/io/EOFException,java/lang/N | 运行正常 |  |
| 宠物王国6－烈火_465.jar | 1449 | 宠物王国6-烈火 | CWA | 1.0 | MIDP-2.0 | - | 121 | 16 | 600 | 2203830 | 0 | java/io/EOFException | 运行正常 |  |
| 宠物精灵爱之体验_2861.jar | 1391 | Lovepet | 悦秀天下 | 1.0 | MIDP-2.0 | - | 6 | 16 | 600 | 190665 | 0 | - | 运行正常 |  |
| 家园4汉化版_10405.jar | 215 | Townsmen4 | www.handy-games.com/ | 1.0 | MIDP-2.0 | nokia-ui | 8 | 16 | 600 | 3286686 | 0 | java/lang/NullPointerException | 运行正常 |  |
| 家园5汉化版_10376.jar | 239 | Townsmen5 | www.handy-games.com  | 1.5 | MIDP-1.0 | nokia-ui | 7 | 16 | 600 | 2153459 | 0 | - | 运行正常 |  |
| 家园6之革命_7692.jar | 643 | Townsmen 6 | www.handy-games.com  | 1.0.0 | MIDP-2.0 | - | 26 | 16 | 601 | 1333313 | 0 | java/lang/NullPointerException | 运行正常 |  |
| 家园6之革命完美汉化版_7583.jar | 590 | 家园6_光辉革命 | www.handy-games.com  | 1.0.0 | MIDP-2.0 | - | 26 | 16 | 601 | 924707 | 0 | java/lang/NullPointerException | 运行正常 |  |
| 寂静岭2汉化版_10177.jar | 507 | Silent Hill Mobile 2 | Konami | 1.0.0 | MIDP-2.0 | - | 12 | 16 | 600 | 6880487 | 0 | java/io/IOException,java/lang/Nu | 运行正常 |  |
| 寂静岭汉化版_9575.jar | 415 | Silent Hill | Glu | 0.1.0 | MIDP-2.0 | - | 11 | 16 | 600 | 3208468 | 0 | java/io/IOException,javax/microe | 运行正常 |  |
| 密室逃脱_7300.jar | 103 | 密室逃脱 | 新热力 | 1.0 | MIDP-1.0 | nokia-ui | 5 | 16 | 600 | 501359 | 0 | - | 运行正常 |  |
| 寻秦皇陵的诅咒_12450.jar | 573 | 寻秦 | Shenfasoft | 1.0.0 | MIDP-1.0 | nokia-ui | 33 | 16 | 600 | 374128 | 0 | - | 运行正常 |  |
| 帝国时代2汉化版_7864.jar | 62 | 帝国时代2（微软正版） | In-Fusio | 1.0 | MIDP-1.0 | nokia-ui | 9 | 16 | 600 | 797237 | 0 | javax/microedition/rms/InvalidRe | 运行正常 |  |
| 帝国时代3-亚洲王朝_6288.jar | 595 | 帝国时代III亚洲王朝 | Glu Mobile LTD | 1.0.0 | MIDP-2.0 | - | 41 | 16 | 600 | 2044721 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| 帝国时代Ⅲ汉化版_10379.jar | 426 | Age Of Empires III Mobile | Glu_Mobile | 1.0.1 | MIDP-1.0 | - | 10 | 16 | 600 | 4792690 | 0 | - | 运行正常 |  |
| 开心仓鼠大作战_3911.jar | 506 | kxcsdz | CaiFun | 1.0.0 | MIDP-1.0 | - | 38 | 16 | 600 | 312641 | 0 | java/lang/IllegalArgumentExcepti | 运行正常 |  |
| 开心农场BT版_6075.jar | 624 | 开心农场 | COM2USCHINA | 1.0 | MIDP-1.0 | - | 13 | 16 | 600 | 326998 | 0 | java/lang/NullPointerException | 运行正常 |  |
| 开心农场_240.jar | 374 | happy_farm | Auer | 1.30.04 | MIDP-2.0 | - | 39 | 16 | 600 | 44786 | 0 | - | 运行正常 |  |
| 开心农场_6370.jar | 374 | happy_farm | Auer | 1.30.04 | MIDP-2.0 | - | 39 | 16 | 600 | 44786 | 0 | - | 运行正常 |  |
| 开心餐厅2012_745.jar | 794 | 开心餐厅2012 | MuchHappy | 1.2 | MIDP-2.0 | - | 76 | 16 | 600 | 82540 | 0 | java/lang/NullPointerException | 运行正常 |  |
| 异形战机汉化版_8382.jar | 268 | R-Type | Gyrox | 1.0 | MIDP-2.0 | - | 5 | 16 | 600 | 340766 | 0 | java/io/IOException,java/lang/Nu | 运行正常 |  |
| 彩京打击者1945_10623.jar | 206 | Strikers 1945 | Psikoy, wait4u | 1.0.0 | MIDP-2.0 | - | 4 | 16 | 600 | 9973 | 0 | - | 运行正常 |  |
| 彩虹岛汉化版_10241.jar | 124 | Rainbow Islands [MICHANG] | Taito/daddyfatsax | 1.0.6 | MIDP-2.0 | - | 16 | 16 | 600 | 860130 | 0 | - | 运行正常 |  |
| 德州扑克2_3884.jar | 985 | Texas Hold'Em Poker 2 | Gameloft SA | 1.2.9 | MIDP-2.0 | - | 26 | 16 | 600 | 87769472 | 0 | java/lang/NullPointerException | 运行正常 |  |
| 忍者神龟之最终的判决_8288.jar | 357 | TMNT The Ninja Tribunal | Nostromo | 1.22 | MIDP-2.0 | - | 4 | 16 | 600 | 11353431 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| 快乐农场_2089.jar | 782 | 快乐农场之僵尸来袭 | TOM | 1.0.0 | MIDP-2.0 | - | 36 | 16 | 600 | 101431 | 0 | - | 运行正常 |  |
| 快打旋风_10825.jar | 273 | FinalFight | CAPCOM/daddyfatsax | 1.2.5 | MIDP-2.0 | - | 4 | 16 | 600 | 412661 | 0 | - | 运行正常 |  |
| 急速公路狂飙－新年版_84.jar | 880 | 急速公路狂飙新年版 | lingdong | 1.0 | MIDP-2.0 | m3g | 119 | 16 | 600 | 18883611 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| 急速狂飙完美破解版_4964.jar | 1301 | HighSpeed 3D | HeroCraft & APetrus | 1.6.0 | MIDP-2.0 | m3g | 48 | 16 | 600 | 78212684 | 0 | java/io/EOFException,java/io/IOE | 运行正常 |  |
| 恋爱物语－我的机器女友_6219.jar | 973 | cyborg_lover | Auer | 1.30.01 | MIDP-2.0 | - | 66 | 16 | 600 | 38404 | 0 | - | 运行正常 |  |
| 恋色狂想之魔女恋歌_4899.jar | 774 | LCMS | AUER | 01.30.00 | MIDP-2.0 | - | 88 | 16 | 600 | 616401 | 0 | - | 运行正常 |  |
| 愤怒的小鸟-高清版_2353.jar | 16451 | Angry Birds - by novchik | Zed | 0.1.1 | MIDP-2.0 | - | 45 | 16 | 600 | 4697031 | 0 | java/lang/ClassNotFoundException | 运行正常 |  |
| 愤怒的小鸟_383.jar | 6333 | Angry Birds | mob.ua | 1.1 | MIDP-2.1 | - | 145 | 16 | 600 | 66336 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| 我们要恋爱之校园篇_1815.jar | 779 | 我们要恋爱之校园篇 | HalenZhang | 1.0.0 | MIDP-2.0 | - | 32 | 16 | 600 | 18488 | 0 | javax/microedition/media/MediaEx | 运行正常 |  |
| 我的宠物我做主BT版_3055.jar | 633 | 我的宠物我做主(正版)_玉品 | alioth | 1.0 | MIDP-1.0 | - | 20 | 16 | 600 | 374175 | 0 | - | 运行正常 |  |
| 战争机器1945_5414.jar | 466 | 1945 | Midlet Suite Vendor | 1.0.0 | MIDP-1.0 | - | 11 | 16 | 600 | 47988 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| 战争艺术2之全球联盟中文版_1918.jar | 573 | 战争艺术2 | i-free | 1.0.0 | MIDP-2.0 | - | 15 | 16 | 600 | 2200699 | 0 | java/io/EOFException,java/lang/N | 运行正常 |  |
| 扫雷行动_10904.jar | 559 | Minesweeper Mobile | Electronic Arts | 0.0.1 | MIDP-2.0 | nokia-ui | 11 | 16 | 600 | 29383792 | 0 | java/lang/NullPointerException | 运行正常 |  |
| 拳王2009南拳北腿_1167.jar | 331 | 拳王2009-南拳北腿 | GAMEZERO | 1.0 | MIDP-1.0 | - | 8 | 16 | 600 | 144248 | 0 | - | 运行正常 |  |
| 拳王争霸_11585.jar | 582 | Lucha Libre | Gameloft SA | 1.0.8 | MIDP-2.0 | nokia-ui | 6 | 16 | 600 | 748775617 | 0 | - | 运行正常 |  |
| 拳皇98终极之战_7534.jar | 831 | 拳皇98-终极之战 | 5wan | 1.0 | MIDP-1.0 | - | 37 | 16 | 600 | 512853 | 0 | - | 运行正常 |  |
| 捕鱼达人3金装版_1511.jar | 634 | 深海猎奇-捕鱼达人 | Gamewave | 1.00 | MIDP-1.0 | nokia-ui | 15 | 16 | 600 | 169932 | 0 | java/io/IOException,java/lang/Nu | 运行正常 |  |
| 文明3_10522.jar | 160 | Civilization 3 | COM2US | 1.0.0 | MIDP-2.0 | - | 7 | 16 | 600 | 277231 | 0 | - | 运行正常 |  |
| 斗po苍穹bt版_1127.jar | 1609 | 斗破苍穹 | GameWave | 1.0.0 | MIDP-1.0 | - | 37 | 16 | 600 | 90329 | 0 | java/io/IOException,java/lang/Nu | 运行正常 |  |
| 斩妖伏魔录3_6306.jar | 1016 | 斩妖伏魔录3 | CWA | 1.0 | MIDP-2.0 | nokia-ui | 21 | 16 | 600 | 6079154 | 0 | java/io/IOException,java/lang/Nu | 运行正常 |  |
| 斩妖伏魔录4(QQ挑战)_2922.jar | 1043 | 斩妖伏魔录4 | cwa | 1.0 | MIDP-2.0 | nokia-ui | 55 | 16 | 600 | 8411797 | 0 | java/io/IOException,java/lang/Nu | 运行正常 |  |
| 斩妖伏魔录_6231.jar | 584 | 斩妖伏魔录(华娱) | 华娱无线 | 1.0 | MIDP-1.0 | nokia-ui | 19 | 16 | 600 | 3823140 | 0 | java/io/IOException,java/lang/Nu | 运行正常 |  |
| 新2011拳皇_4132.jar | 948 | 新拳皇2011 | tyznw.com | 1.0 | MIDP-1.0 | - | 33 | 16 | 600 | 484431 | 0 | java/io/EOFException,javax/micro | 运行正常 |  |
| 新三国志_10225.jar | 186 | 新三国志 | 北京数字顽石无线科技 | 1.0.0 | MIDP-2.0 | - | 17 | 16 | 600 | 46509 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| 新牧场物语2恋爱季节_1340.jar | 991 | 新牧场物语2恋爱季节 | Auer | 1.0.0 | MIDP-2.0 | - | 73 | 16 | 600 | 76848 | 0 | - | 运行正常 |  |
| 新西游记－格斗天王_1211.jar | 1327 | 新西游记-格斗天王 | IMANIMAL | 1.0 | MIDP-1.0 | - | 56 | 16 | 600 | 1480567 | 0 | java/lang/NumberFormatException | 运行正常 |  |
| 时光八音盒2忆之篇_3865.jar | 1014 | 时光八音盒II_忆之篇_玉品 | Alioth | 1.0.0 | MIDP-1.0 | - | 11 | 16 | 600 | 126318 | 0 | java/lang/ArrayIndexOutOfBoundsE | 运行正常 |  |
| 时光八音盒萌之篇_3806.jar | 961 | sgbyh | Alioth | 1.0.0 | MIDP-1.0 | - | 11 | 16 | 600 | 126298 | 0 | java/lang/ArrayIndexOutOfBoundsE | 运行正常 |  |
| 时光樱花册－遗失之心_1009.jar | 1380 | 时光樱花册-遗失之心 | Alioth | 1.0 | MIDP-1.0 | - | 33 | 16 | 600 | 603706 | 0 | - | 运行正常 |  |
| 明星制造梦工厂_11048.jar | 220 | 明星制造梦工厂 | 明特网络 | 1.2 | MIDP-1.0 | - | 8 | 16 | 600 | 228872 | 0 | - | 运行正常 |  |
| 明星麻将馆美女_9760.jar | 362 | 明星麻将馆(美女真人版) | 闪联互动 | 1.0.0 | MIDP-1.0 | nokia-ui | 11 | 16 | 600 | 4317743 | 0 | - | 运行正常 |  |
| 星空恋曲-遥远的记忆(修正版)_4008.jar | 982 | STAR | AUER | 01.30.00 | MIDP-2.0 | - | 84 | 16 | 600 | 513646 | 0 | - | 运行正常 |  |
| 星辰变S60V5完整版_3364.jar | 926 | 星辰变(盛大正版) | M2me | 1.0.0 | MIDP-1.0 | nokia-ui | 40 | 16 | 600 | 250348802 | 0 | java/io/IOException | 运行正常 |  |
| 是男人就下100层_1882.jar | 636 | 是男人就下100层 | CNMOSUN | 2.01 | MIDP-1.0 | nokia-ui | 75 | 16 | 600 | 6964968 | 0 | java/io/IOException,java/lang/Nu | 运行正常 |  |
| 暗黑启示录2－黑暗王朝_888.jar | 1268 | 暗黑启示录2黑暗王朝 | leichan | 1.0 | MIDP-1.0 | - | 214 | 16 | 600 | 127252238 | 0 | - | 运行正常 |  |
| 有女倾城破解版_11525.jar | 258 | 有女倾城 | - | 1.0 | MIDP-1.0 | - | 9 | 16 | 0 | 53901 | 0 | - | 未呈现画面 | exited itself (notifyDestroyed) without pain |
| 机甲风暴_7888.jar | 673 | 机甲风暴 | lxs | 1.0 | MIDP-2.0 | nokia-ui | 41 | 16 | 600 | 95517 | 0 | - | 运行正常 |  |
| 杀戮机器-纳粹僵尸_3610.jar | 995 | 杀戮机器-纳粹僵尸 | CWA | 1.0 | MIDP-2.0 | m3g | 37 | 16 | 600 | 741606 | 0 | java/lang/NumberFormatException | 运行正常 |  |
| 杀鬼子-敌后武工队_2185.jar | 515 | 杀鬼子-敌后武工队 | linktone | 1.0.0 | MIDP-1.0 | nokia-ui | 37 | 16 | 600 | 147638505 | 0 | java/io/IOException | 运行正常 |  |
| 极品飞车13之变速英文3D版_6646.jar | 754 | Need for Speed Shift | Electronic Arts | 7.2.18 | MIDP-2.0 | m3g | 38 | 16 | 600 | 15073340 | 0 | - | 运行正常 |  |
| 极品飞车14之热力追踪中文版_4160.jar | 1219 | 极品飞车：热血追踪 | Kexunda | 1.0.0 | MIDP-2.0 | m3g | 44 | 16 | 600 | 256001 | 0 | - | 运行正常 |  |
| 极品飞车16：亡命狂飙_1519.jar | 1046 | Need for Speed The Run | Electronic Arts Inc. | 4.4.88 | MIDP-2.0 | m3g,nokia-ui | 41 | 16 | 600 | 11625128 | 0 | - | 运行正常 |  |
| 极品飞车之无间风云FX中文版_4518.jar | 1000 | 极品飞车无间风云FX | Electronic Arts Inc. | 1.0.0 | MIDP-2.0 | m3g | 63 | 16 | 600 | 3602809 | 0 | - | 运行正常 |  |
| 极品飞车精装版_7557.jar | 159 | 极品飞车精装版 | 金海科技 | 1.0 | MIDP-1.0 | - | 16 | 16 | 600 | 245889 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| 柯楠之密室杀人事件_3821.jar | 429 | 柯楠－密室杀人事件 | 新浪飞扬 | 1.0 | MIDP-2.0 | nokia-ui | 39 | 16 | 600 | 109418 | 0 | - | 运行正常 |  |
| 校园回忆_1940.jar | 358 | 校园回忆 | 北京都瑞网络技术有限公司 | 1.0 | MIDP-1.0 | - | 16 | 16 | 600 | 40540 | 0 | - | 运行正常 |  |
| 格斗2011-拳王争霸_4253.jar | 805 | GDQWZB | ZYHD | 1.2.0 | MIDP-2.0 | - | 8 | 16 | 600 | 282374014 | 0 | java/lang/NullPointerException | 运行正常 |  |
| 格斗三国五虎将精装版_9085.jar | 275 | 格斗三国五虎将(精装版) | 创行软件 | 1.0 | MIDP-1.0 | - | 8 | 16 | 600 | 215449 | 0 | javax/microedition/rms/InvalidRe | 运行正常 |  |
| 格斗少女_3271.jar | 709 | gdsn | 汉娱 | 1.0 | MIDP-2.0 | - | 14 | 16 | 600 | 25710 | 0 | - | 运行正常 |  |
| 格斗火柴人_3938.jar | 416 | Enter The Flip | XTPL serviak | 1.0 | MIDP-2.0 | - | 11 | 16 | 600 | 176281 | 0 | - | 运行正常 |  |
| 梦幻西游_4223.jar | 1024 | 梦幻西游 | HornGame | 1.0 | MIDP-1.0 | - | 88 | 16 | 600 | 4636022 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| 植物大战僵尸(新角色加强版)_982.jar | 421 | 植物大战僵尸 | JWH | 1.0 | MIDP-1.0 | - | 37 | 16 | 600 | 40248 | 0 | - | 运行正常 |  |
| 植物大战僵尸2011加强版_2912.jar | 533 | 植物大战僵尸(2011加强版) | tyznw.com | 1.0 | MIDP-1.0 | - | 56 | 16 | 600 | 119063 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| 植物大战僵尸2013完美升级版_326.jar | 516 | 植物大战僵尸 | MIDlet Suite Vendor | 1.0 | MIDP-1.0 | - | 41 | 16 | 600 | 265101 | 0 | - | 运行正常 |  |
| 植物大战僵尸BT版_650.jar | 905 | 植物大战僵尸 | PopCap Games | 2.2 | MIDP-2.0 | - | 99 | 16 | 601 | 14420166 | 0 | java/lang/NumberFormatException | 运行正常 |  |
| 植物大战僵尸欧美大波版_407.jar | 510 | 植物大战僵尸2013欧美大波版 | MIDlet Suite Vendor | 1.0 | MIDP-1.0 | - | 34 | 16 | 600 | 284872 | 0 | - | 运行正常 |  |
| 模拟人生2_10762.jar | 273 | 模拟人生2 | 北京丰尚佳诚 | 1.0.0 | MIDP-1.0 | - | 31 | 16 | 600 | 163486289 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| 模拟人生2完美版_11722.jar | 417 | The Sims 2 | Electronic Arts/BiNP | 1.8.13 | MIDP-2.0 | nokia-ui | 20 | 16 | 600 | 23770411 | 0 | - | 运行正常 |  |
| 模拟人生2宠物当家_10928.jar | 410 | 模拟人生2宠物当家 | fonesun | 1.0 | MIDP-2.0 | - | 35 | 16 | 600 | 1394791 | 0 | - | 运行正常 |  |
| 模拟人生3_7562.jar | 559 | The Sims 3 | Electronic Arts/BiNP | 7.5.73 | MIDP-2.0 | - | 28 | 16 | 600 | 14123626 | 0 | - | 运行正常 |  |
| 模拟人生3中文版_6162.jar | 492 | 模拟人生3(EA正版) | Fonesun | 1.0.0 | MIDP-2.0 | - | 25 | 16 | 600 | 8148660 | 0 | - | 运行正常 |  |
| 模拟人生3之野心_4936.jar | 817 | The Sims 3 Ambitions | Electronic Arts | 4.2.28 | MIDP-2.0 | - | 26 | 16 | 600 | 10020844 | 0 | java/io/EOFException | 运行正常 |  |
| 模拟人生3繁体bt版_6026.jar | 497 | The Sims 3 | Electronic Arts Inc. | 2.0.0 | MIDP-2.0 | - | 27 | 16 | 600 | 10114466 | 0 | - | 运行正常 |  |
| 模拟恋爱2_11543.jar | 291 | SimBabe2 by serviak | Nostromo, serviak | 1.4 | MIDP-2.0 | - | 2 | 16 | 600 | 12591353 | 0 | - | 运行正常 |  |
| 武林外传-明星斗地主增值版_3971.jar | 689 | 武林外传-明星斗地主(电影正版) | ETgame | 1.0.0 | MIDP-2.0 | - | 52 | 16 | 600 | 41867 | 0 | - | 运行正常 |  |
| 武林外传_9847.jar | 400 | 武林外传-RPG巨作正版 | 新浪飞扬 | 1.0 | MIDP-2.0 | nokia-ui | 85 | 16 | 600 | 417636 | 0 | - | 运行正常 |  |
| 歪歪隋唐英雄传_11206.jar | 52 | waiwai | 岩浆数码 | 1.0 | MIDP-1.0 | nokia-ui | 6 | 16 | 600 | 43508 | 0 | java/io/IOException | 运行正常 |  |
| 毁灭战士3_10598.jar | 1590 | Doom3D | Vendor | 1.0 | MIDP-2.0 | m3g | 48 | 16 | 600 | 100207 | 0 | java/io/IOException,java/lang/Nu | 运行正常 |  |
| 比基尼美女海底影相_9643.jar | 275 | Foto Quest Bikini | Digital Chocolate, I | 2.0.6 | MIDP-1.0 | nokia-ui | 14 | 16 | 600 | 328295 | 0 | java/io/EOFException | 运行正常 |  |
| 水浒108将-激斗篇_2196.jar | 625 | 水浒108将-激斗篇 | 丰尚佳诚 | 1.0.0 | MIDP-1.0 | - | 33 | 16 | 600 | 15601 | 0 | java/io/IOException | 运行正常 |  |
| 法拉利GT3－世界赛道BT版_776.jar | 2881 | Ferrari GT 3 - World Track | Gameloft SA | 1.1.2 | MIDP-2.0 | nokia-ui | 22 | 16 | 600 | 507509797 | 0 | java/lang/ArrayIndexOutOfBoundsE | 运行正常 |  |
| 波斯王子之时之沙中文版_11315.jar | 60 | 波斯王子-时之沙 | 智乐软件 | 1.0.1 | MIDP-1.0 | nokia-ui | 6 | 16 | 600 | 116655100 | 0 | - | 运行正常 |  |
| 波斯王子之武者之心_8424.jar | 202 | Prince Of Persia | EricSon | 1.0.2 | MIDP-2.0 | - | 5 | 16 | 600 | 279945525 | 0 | - | 运行正常 |  |
| 波斯王子经典版_9715.jar | 326 | Prince of Persia Classic | Gameloft SA | 1.0.3 | MIDP-2.0 | - | 9 | 16 | 600 | 543579651 | 0 | java/lang/NullPointerException | 运行正常 |  |
| 浴火银河2完整版_7689.jar | 1639 | Galaxy on Fire 2 | www.fishlabs.net | 1.0.3 | MIDP-2.0 | m3g | 154 | 16 | 600 | 5740016 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| 浴火银河2汉化版_6950.jar | 1680 | Galaxy on Fire 2 | www.fishlabs.net | 1.0.3 | MIDP-2.0 | m3g | 155 | 16 | 600 | 10297388 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| 浴火银河2瓦尔基里(汉化)_3067.jar | 9771 | Galaxy on Fire 2 | www.d.cn | 1.0.3 | MIDP-2.0 | m3g | 155 | 16 | 600 | 10322399 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| 浴火银河2瓦尔基里_3093.jar | 9746 | Galaxy on Fire 2 | www.d.cn | 1.0.3 | MIDP-2.0 | m3g | 154 | 16 | 600 | 5813844 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| 浴火银河_10189.jar | 510 | 3D Galaxy on Fire | www.fishlabs.net / s | 1.0.3 | MIDP-2.0 | mascot | 101 | 16 | 600 | 10057792 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| 浴火银河_3621.jar | 510 | 3D Galaxy on Fire | www.fishlabs.net / s | 1.0.3 | MIDP-2.0 | mascot | 101 | 16 | 600 | 11239941 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| 海绵宝宝卡丁车_4684.jar | 651 | HMBBKDC | IMANIMAL | 0.1.0 | MIDP-2.0 | - | 9 | 16 | 600 | 171785782 | 0 | javax/microedition/rms/InvalidRe | 运行正常 |  |
| 涂鸦地带_模拟可玩_12472.jar | 62 | 涂鸦地带 | Loser008 | 1.0 | MIDP-2.0 | - | 22 | 16 | 600 | 312614 | 0 | java/io/IOException,javax/microe | 运行正常 |  |
| 涂鸦跳跃中文豪华版(汉化)_2524.jar | 1424 | 涂鸦跳跃 | 小克拉 | 1.8.2 | MIDP-2.0 | - | 13 | 16 | 600 | 559291924 | 0 | - | 运行正常 |  |
| 灌篮高手-爆发!樱木_7947.jar | 327 | 灌篮高手-爆发！樱木 | 无限天机 | 1.0.0 | MIDP-2.0 | - | 44 | 16 | 600 | 426385 | 0 | java/lang/ArithmeticException,ja | 运行正常 |  |
| 火影忍者-终极格斗_6604.jar | 673 | 火影忍者-精品格斗2010 | 赛诺斯 | 1.0.0 | MIDP-1.0 | nokia-ui | 41 | 16 | 600 | 5282513 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| 火影忍者_11531.jar | 335 | Naruto | N-Gage Planet | Mod | MIDP-1.0 | nokia-ui | 9 | 16 | 600 | 12818 | 0 | javax/microedition/rms/InvalidRe | 运行正常 |  |
| 火影忍者格斗2010_4601.jar | 652 | 火影忍者-精品格斗2010 | 赛诺斯 | 1.0.0 | MIDP-1.0 | nokia-ui | 39 | 16 | 600 | 5282513 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| 火影忍者汉化版_10735.jar | 261 | Ninja Fighter (RoS) | King of Mobile | 1.0 | MIDP-1.0 | nokia-ui | 22 | 16 | 687 | 2619492 | 0 | java/io/EOFException,java/io/IOE | 运行正常 |  |
| 火焰-纹章之谜_12128.jar | 316 | 火焰-纹章之谜 | 拉阔游戏 | 1.9 | MIDP-1.0 | nokia-ui | 17 | 16 | 600 | 857891 | 0 | java/io/IOException,java/lang/Nu | 运行正常 |  |
| 火焰-纹章传说第一章_12129.jar | 365 | 火焰-纹章传说第一章 | Gameislive | 1.6 | MIDP-1.0 | nokia-ui | 18 | 16 | 600 | 823448 | 0 | java/io/IOException,java/lang/Nu | 运行正常 |  |
| 火焰-纹章传说第三章_12130.jar | 354 | 火焰-纹章传说第三章 | gameislive | 1.8 | MIDP-1.0 | nokia-ui | 18 | 16 | 600 | 1296165 | 0 | java/io/IOException,java/lang/Nu | 运行正常 |  |
| 火焰-纹章传说第二章_12131.jar | 343 | 火焰-纹章传说第二章 | Gameislive | 1.8 | MIDP-1.0 | nokia-ui | 18 | 16 | 600 | 961598 | 0 | java/io/IOException,java/lang/Nu | 运行正常 |  |
| 火焰纹章勇者之剑_10337.jar | 163 | 圣火纹章-勇者之剑 | 空中猛犸 | 1.0 | MIDP-1.0 | - | 16 | 16 | 600 | 92731 | 0 | - | 运行正常 |  |
| 灵异村庄_11169.jar | 94 | The Village | Tracebit Ltd | 1.2 | MIDP-1.0 | nokia-ui | 19 | 16 | 600 | 183501 | 0 | java/io/IOException | 运行正常 |  |
| 炸弹人_10311.jar | 302 | Bomberman | Living Mobile/BiNPDA | 1.0.0 | MIDP-1.0 | nokia-ui | 12 | 16 | 600 | 23220942 | 0 | javax/microedition/rms/InvalidRe | 运行正常 |  |
| 热血沙滩9球赛_894.jar | 472 | 热血沙滩9球赛 | yflh | 1.0.0 | MIDP-2.0 | - | 27 | 16 | 600 | 89025 | 0 | - | 运行正常 |  |
| 爆笑三国斗地主_10552.jar | 312 | 爆笑三国斗地主 | 深蓝创娱 | 1.0 | MIDP-1.0 | nokia-ui | 8 | 16 | 600 | 63794 | 0 | - | 运行正常 |  |
| 狂野飙车4-gameloft(中文版)_2603.jar | 647 | 狂野飙车4 | Gameloft SA | 1.0.0 | MIDP-2.0 | - | 22 | 16 | 600 | 128449912 | 0 | - | 运行正常 |  |
| 狂野飙车4-亡命都市_2875.jar | 616 | 狂野飙车-亡命都市 | 智乐软件 | 1.0.0 | MIDP-2.0 | - | 13 | 16 | 600 | 130423755 | 0 | java/lang/NumberFormatException | 运行正常 |  |
| 狂野飙车4_6959.jar | 596 | 狂野飙车4-gameloft | Gameloft SA | 1.0.0 | MIDP-2.0 | - | 12 | 16 | 600 | 3496881 | 0 | - | 运行正常 |  |
| 猿人时代BT版_4519.jar | 489 | 猿人时代 | 东虹移动 | 1.0.0 | MIDP-2.0 | - | 49 | 16 | 600 | 49638 | 0 | - | 运行正常 |  |
| 生化危机-险境求生_3290.jar | 914 | 生化危机 | joymeng | 1.0.0 | MIDP-1.0 | nokia-ui | 53 | 16 | 600 | 309287209 | 0 | - | 运行正常 |  |
| 生化危机3D(CAPCOM经典)_3671.jar | 983 | shwjd | CAPCOM | 1.0 | MIDP-2.0 | m3g | 21 | 16 | 600 | 226187 | 0 | java/lang/NullPointerException | 运行正常 |  |
| 生化危机5核变_866.jar | 1181 | 生化危机5核变 | MR | 1.0 | MIDP-1.0 | nokia-ui | 61 | 16 | 600 | 696460 | 0 | - | 运行正常 |  |
| 生化危机之恶化_8448.jar | 372 | RED http://www.universosymbi | Glu Mobile | 1.0.0 | MIDP-2.0 | - | 10 | 16 | 600 | 75803 | 0 | java/lang/IllegalArgumentExcepti | 运行正常 |  |
| 生化惊悚2死亡寂静中文版_2607.jar | 1144 | shjswjj | 空中猛犸 | 0.0.5 | MIDP-2.0 | - | 26 | 16 | 600 | 261955550 | 0 | java/lang/ArrayIndexOutOfBoundsE | 运行正常 |  |
| 电子宠物蛋_11972.jar | 95 | Tamagotchi 240x320 | Living Mobile | 1.0.96 | MIDP-1.0 | nokia-ui | 8 | 16 | 600 | 775378 | 0 | - | 运行正常 |  |
| 电玩美女麻将_7260.jar | 439 | 电玩美女麻将 | goodgame | 1.0.0 | MIDP-1.0 | - | 9 | 16 | 600 | 6197304 | 0 | - | 运行正常 |  |
| 男摄影师_11003.jar | 250 | 男摄影师 | 北京业主行 | 1.0.0 | MIDP-1.0 | - | 19 | 16 | 600 | 4130384 | 0 | - | 运行正常 |  |
| 疯狂企鹅中文版_10228.jar | 140 | 功夫企鹅 | 华娱聚友 | 1.0.0 | MIDP-2.0 | - | 13 | 16 | 600 | 249275 | 0 | java/io/EOFException,javax/micro | 运行正常 |  |
| 疯狂农场2_2375.jar | 551 | Farm Frenzy 2 | Alawar Entertainment | 1.0.3 | MIDP-2.0 | - | 42 | 16 | 600 | 2150426 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| 疯狂农场汉化版_5998.jar | 400 | Farm Frenzy | HeroCraft | 2.0.0 | MIDP-2.0 | - | 33 | 16 | 600 | 1164574 | 0 | java/io/EOFException,java/io/IOE | 运行正常 |  |
| 疯狂出租车中文版_721.jar | 491 | 疯狂出租车 | BMIT | 1.0 | MIDP-1.0 | - | 49 | 16 | 600 | 172564 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| 疯狂战机_2013.jar | 219 | Srcombat20110928 | Srcombat20110928 | 1.0.0 | MIDP-2.0 | - | 65 | 16 | 600 | 183755923 | 0 | java/io/IOException | 运行正常 |  |
| 百战天虫2010中文版_5380.jar | 560 | PC原版-百战天虫2010-EA | fonesun | 1.0.0 | MIDP-2.0 | - | 25 | 16 | 600 | 6688050 | 0 | - | 运行正常 |  |
| 百鬼商店破解版_10526.jar | 382 | BAI_GUI_SHANG_DIAN | tongyasoft | 1.0 | MIDP-2.0 | - | 4 | 16 | 600 | 1163463 | 0 | - | 运行正常 |  |
| 真三国无双横扫千军_12449.jar | 270 | 横扫千军.宝软网baoruan.com免费分享 | HaKuNa | 1.0 | MIDP-2.0 | - | 14 | 16 | 600 | 826935 | 0 | - | 运行正常 |  |
| 真人快打3中文版_3252.jar | 299 | 真人快打3 | 科讯达 | 1.0.0 | MIDP-2.0 | - | 50 | 16 | 600 | 5014 | 0 | - | 运行正常 |  |
| 真幻想三国志完整版_12469.jar | 877 | 真幻想三国志 | 仙掌软件 | 1.0 | MIDP-1.0 | nokia-ui | 138 | 16 | 600 | 154729926 | 0 | - | 运行正常 |  |
| 真武三国BT版_3953.jar | 1013 | 真武三国 | CWA | 1.0 | MIDP-2.0 | - | 23 | 16 | 600 | 589612 | 0 | java/lang/NumberFormatException | 运行正常 |  |
| 真武三国_3962.jar | 1018 | 真武三国 | CWA | 1.0 | MIDP-2.0 | - | 22 | 16 | 600 | 589612 | 0 | java/lang/NumberFormatException | 运行正常 |  |
| 真西游记_6848.jar | 738 | 真西游记(华娱) | 华娱无线 | 1.0 | MIDP-1.0 | - | 18 | 16 | 600 | 994834 | 0 | - | 运行正常 |  |
| 神庙逃亡2（Temple_18.jar | 680 | Run2 | ZZFT | 2.0.0 | MIDP-2.0 | - | 4 | 16 | 600 | 50508 | 0 | - | 运行正常 |  |
| 科比职业篮球2008中文版_11136.jar | 520 | 科比大灌篮(gameloft正版) | 空中猛犸 | 1.02 | MIDP-1.0 | - | 10 | 16 | 600 | 484369359 | 0 | java/lang/ArrayIndexOutOfBoundsE | 运行正常 |  |
| 空战1944_10321.jar | 309 | Air Strike 1944 | Gameloft SA | 1.0.0 | MIDP-2.0 | - | 6 | 16 | 600 | 59727677 | 0 | - | 运行正常 |  |
| 穿越火线-韩国正版_6874.jar | 480 | chuanyuehuoxian | vendor | 1.0.0 | MIDP-1.0 | - | 21 | 16 | 600 | 319357696 | 0 | - | 运行正常 |  |
| 竞技者之极限酷跑_5431.jar | 250 | 竞技者之极限酷跑 | Mr. Goodliving Ltd | 1.0.4 | MIDP-2.0 | - | 7 | 16 | 600 | 599006234 | 0 | java/io/EOFException | 运行正常 |  |
| 精品美女斗地主2013_104.jar | 1203 | 精品美女斗地主 | LXWX | 1.0 | MIDP-1.0 | nokia-ui | 61 | 16 | 600 | 236357136 | 0 | java/io/IOException,java/lang/Nu | 运行正常 |  |
| 红色警戒3_7037.jar | 784 | C&C: Red Alert Mobile by bli | Electronic Arts / re | 4.15.48 | MIDP-2.0 | - | 33 | 16 | 600 | 4837888 | 0 | - | 运行正常 |  |
| 纯爱物语3之梦想之翼_5112.jar | 978 | L3D | AUER | 1.30.0 | MIDP-2.0 | - | 82 | 16 | 600 | 834220 | 0 | - | 运行正常 |  |
| 纯爱物语之湛蓝回忆简体版_5164.jar | 1007 | LoveMemory | 北京艾佛格 | 1.0 | MIDP-1.0 | - | 66 | 16 | 600 | 29667 | 0 | - | 运行正常 |  |
| 纽约人生_1339.jar | 879 | My Life in New York | Gameloft SA | 1.0.6 | MIDP-2.0 | - | 12 | 16 | 600 | 118392562 | 0 | - | 运行正常 |  |
| 细胞分裂4混沌法则中文版_11671.jar | 101 | 细胞分裂3-混沌法则 | 空中网 | 1.02 | MIDP-1.0 | nokia-ui | 4 | 16 | 600 | 284676121 | 0 | - | 运行正常 |  |
| 终极雷电_4125.jar | 293 | 终极雷电 | tyznw.com | 1.0.0 | MIDP-1.0 | - | 57 | 16 | 600 | 917268 | 0 | - | 运行正常 |  |
| 经典推箱子_1070.jar | 247 | 纸盒人 | MIDlet bf | 1.0.0 | MIDP-1.0 | - | 34 | 16 | 600 | 129564 | 0 | java/io/IOException | 运行正常 |  |
| 绝密反击破解版_8377.jar | 174 | 绝密反击 | 大连博涛 | 1.0 | MIDP-1.0 | nokia-ui | 39 | 16 | 600 | 74147 | 0 | - | 运行正常 |  |
| 网球大满贯_11950.jar | 143 | 网球大满贯 | 5wan | 1.0 | MIDP-1.0 | - | 11 | 16 | 600 | 244313943 | 0 | java/io/IOException,javax/microe | 运行正常 |  |
| 美女台球_9377.jar | 205 | Billard Girls | FDG Mobile Games | 1.0.11 | MIDP-1.0 | - | 40 | 16 | 600 | 293845 | 0 | java/lang/NullPointerException | 运行正常 |  |
| 美女斗地主(真人语音版)_1609.jar | 1089 | 写真美女斗地主(真人语音) | MR | 1.0.0 | MIDP-1.0 | - | 29 | 16 | 600 | 61721 | 0 | - | 运行正常 |  |
| 美女斗地主_1088.jar | 717 | 美女斗地主 | ledao | 1.0 | MIDP-2.0 | nokia-ui | 14 | 16 | 600 | 5252365 | 0 | - | 运行正常 |  |
| 美女滑雪胜地_2.jar | 290 | 美女滑雪胜地 | MIDlet Suite Vendor | 1.0.0 | MIDP-2.0 | - | 23 | 16 | 600 | 98374115 | 0 | - | 运行正常 |  |
| 美少女梦幻工厂_11443.jar | 181 | 美少女梦幻工厂 | 手中网 | 1.0 | MIDP-1.0 | nokia-ui | 4 | 16 | 600 | 45451 | 0 | - | 运行正常 |  |
| 美男狂想曲_3876.jar | 856 | Hotguys Rhapsody | LIVEWARE | 1.0.0 | MIDP-1.0 | - | 11 | 16 | 600 | 180828 | 0 | java/io/IOException,java/lang/Nu | 运行正常 |  |
| 聊斋之倩女幽魂_11185.jar | 300 | 聊斋之倩女幽魂 | 北京中西网联网络技术有限公司 | 1.0 | MIDP-2.0 | nokia-ui | 6 | 16 | 600 | 179825178 | 0 | - | 运行正常 |  |
| 航海世纪2_10583.jar | 157 | Tradewinds 2 | Indiagames Ltd. | 1.1.4 | MIDP-2.0 | - | 4 | 16 | 600 | 252042887 | 0 | java/lang/IllegalArgumentExcepti | 运行正常 |  |
| 色戒之虚拟人生_7230.jar | 584 | 色戒之虚拟人生 | Midlet Suite Vendor | 1.0.0 | MIDP-1.0 | - | 19 | 16 | 600 | 163294 | 0 | - | 运行正常 |  |
| 花样少年恋爱学院_5977.jar | 764 | love_school | Auer | 1.30.02 | MIDP-2.0 | - | 45 | 16 | 600 | 60687 | 0 | - | 运行正常 |  |
| 花样少年恋爱学院特别版_5527.jar | 776 | love-school-sp | Auer | 1.30.11 | MIDP-2.0 | - | 45 | 16 | 600 | 58968 | 0 | - | 运行正常 |  |
| 苍穹默示录完美运行版_4035.jar | 1790 | 苍穹默示录 | Joymeng | 1.0.0 | MIDP-1.0 | - | 53 | 16 | 600 | 226191783 | 0 | java/io/IOException,java/lang/Cl | 运行正常 |  |
| 荒岛余生(GLU正版)_12478.jar | 451 | 荒岛余生(GLU正版) | MIG | 1.0.1 | MIDP-1.0 | - | 9 | 16 | 600 | 255215 | 0 | - | 运行正常 |  |
| 荒岛余生2时空探秘_6794.jar | 486 | 荒岛余生2时空探秘 | motohy.cn | 1.0.1 | MIDP-2.0 | - | 31 | 16 | 600 | 1466729 | 0 | - | 运行正常 |  |
| 荣誉勋章之空降神兵改良版_2993.jar | 559 | Medal Of Honor Airborne Elit | Electronic Arts | 4.18.80 | MIDP-2.0 | - | 32 | 16 | 600 | 10557011 | 0 | - | 运行正常 |  |
| 蓝牙真3D格斗_3424.jar | 602 | 真3D格斗 | Unknown | 1.0 | MIDP-2.0 | m3g,nokia-ui | 26 | 16 | 600 | 1079179 | 0 | javax/microedition/rms/InvalidRe | 运行正常 |  |
| 蓝白五子棋_9790.jar | 104 | Gomoku by drAdeLante | drAdeLante | 1.0.0 | MIDP-2.0 | - | 15 | 16 | 600 | 196018758 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| 蛋蛋历险记_11460.jar | 162 | Bird ETTY | kaogames/ETTY | 1.4.6 | MIDP-2.0 | - | 2 | 16 | 600 | 650788 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| 血战三国之吕布貂蝉_4012.jar | 347 | 血战三国-吕布VS貂蝉 | FunLikeDown | 1.0.0 | MIDP-1.0 | - | 10 | 16 | 600 | 30957 | 0 | javax/microedition/rms/InvalidRe | 运行正常 |  |
| 血战轩辕之魔神蚩尤_11172.jar | 198 | 血战轩辕之魔神蚩尤 | 风雷电科技 | 1.0 | MIDP-1.0 | nokia-ui | 12 | 16 | 600 | 5289757 | 0 | java/lang/IllegalArgumentExcepti | 运行正常 |  |
| 街头反恐之格斗女王_822.jar | 285 | 街头反恐之格斗女王 | ETGAME | 1.0.0 | MIDP-2.0 | - | 10 | 16 | 600 | 32204 | 0 | - | 运行正常 |  |
| 街头霸王-街机经典_2664.jar | 563 | 街头霸王 | CAPCOM | 1.0.0 | MIDP-1.0 | - | 12 | 16 | 600 | 12111 | 0 | - | 运行正常 |  |
| 街头霸王2_11388.jar | 493 | StreetFighter2 | DDJ | 1.2.4 | MIDP-1.0 | - | 5 | 16 | 600 | 62542919 | 0 | - | 运行正常 | needs --device nokia6230 |
| 街头霸王2冠军典藏版_1812.jar | 730 | jtbw | CAPCOM | 1.0.0 | MIDP-2.0 | - | 9 | 16 | 600 | 13644 | 0 | - | 运行正常 |  |
| 街霸2012(加强版)_2302.jar | 1117 | 街霸2012加强版 | Si | 1.0 | MIDP-1.0 | - | 40 | 16 | 600 | 569867 | 0 | java/io/EOFException,java/lang/C | 运行正常 |  |
| 西伯利亚大反击_11466.jar | 205 | 西伯利亚大反击 | 北京金建衡 | 1.0 | MIDP-1.0 | - | 2 | 16 | 600 | 174310943 | 0 | - | 运行正常 |  |
| 诈金花_6358.jar | 557 | 诈金花 | 仙境乐网 | 1.0 | MIDP-1.0 | nokia-ui | 24 | 16 | 600 | 551461 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| 诛神－斗破苍穹_1402.jar | 1190 | 诛神-斗破苍穹 | LongStudio | 1.0.0 | MIDP-2.0 | - | 62 | 16 | 600 | 19243239 | 0 | - | 运行正常 |  |
| 诸神战纪-堕落天使路西法_5070.jar | 1324 | 诸神战纪-堕落天使路西法 | Tom | 1.0 | MIDP-1.0 | - | 28 | 16 | 600 | 316781 | 0 | - | 运行正常 |  |
| 贪吃蛇经典版_4515.jar | 124 | Snake | Vanixon GmbH | 1.3 | MIDP-2.0 | - | 68 | 16 | 600 | 8924 | 0 | - | 运行正常 |  |
| 赤色要塞－FC移植版_5285.jar | 282 | 赤色要塞 | KONAMI | 1.0.0 | MIDP-1.0 | nokia-ui | 43 | 16 | 600 | 1385141 | 0 | java/io/EOFException,java/lang/N | 运行正常 |  |
| 超时空帝国机甲风暴_12065.jar | 542 | 超时空帝国机甲风暴 | Vendor | 1.0 | MIDP-2.0 | nokia-ui | 20 | 16 | 600 | 655617 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| 超级坦克大战2汉化版_531.jar | 655 | (发烧)超级坦克大战II(增强版) | mob.ua | 1.0.1 | MIDP-1.0 | nokia-ui | 24 | 16 | 600 | 268231077 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| 超级酒吧女生_4728.jar | 569 | 超级酒吧女生 | Gameloft SA | 1.0.0 | MIDP-2.0 | - | 10 | 16 | 600 | 180288875 | 0 | - | 运行正常 |  |
| 超级马里奥_10283.jar | 534 | 超级马里奥 | SP | 1.0.0 | MIDP-2.0 | - | 11 | 16 | 600 | 661801744 | 0 | - | 运行正常 |  |
| 足球经理2010汉化版_4756.jar | 517 | CM-2010 | SoGoYo.cn | 1.2.1 | MIDP-2.0 | nokia-ui | 21 | 16 | 600 | 10639826 | 0 | - | 运行正常 |  |
| 跑跑卡丁车-经典中秋版_2117.jar | 623 | 跑跑卡丁车 | ophyer | 1.0.0 | MIDP-2.0 | - | 21 | 16 | 677 | 574860 | 0 | - | 运行正常 |  |
| 蹦球传说_7801.jar | 375 | Bounce Tales | Nokia | 2.0.3 | MIDP-2.0 | nokia-ui | 21 | 16 | 600 | 8777201 | 0 | - | 运行正常 | needs --device nokia6230 |
| 蹦球传说美化版_2959.jar | 416 | Bounce Tales | mob.ua | 2.0.3 | MIDP-2.0 | nokia-ui | 19 | 16 | 600 | 10704326 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| 轩辕神剑_2419.jar | 1324 | 轩辕神剑 | CWA | 1.0 | MIDP-2.0 | - | 71 | 16 | 600 | 539674 | 0 | java/io/EOFException,java/lang/N | 运行正常 |  |
| 迈阿密之夜_9566.jar | 315 | MiamiNights | Gameloft SA | 1.0.2 | MIDP-2.0 | - | 6 | 16 | 600 | 436851882 | 0 | - | 运行正常 |  |
| 远古帝国2汉化版_10357.jar | 168 | Ancient Empires II | Macrospace | 1.0.2 | MIDP-1.0 | - | 10 | 16 | 600 | 569583 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| 速度与激情5中文版_3043.jar | 380 | Fast Five The Movie Game | Gameloft SA | 1.0.2 | MIDP-2.0 | nokia-ui | 25 | 16 | 600 | 233038764 | 0 | - | 运行正常 |  |
| 速度与激情6_9.jar | 1917 | Fast & Furious 6 | mob.ua | 1.1.2 | MIDP-2.0 | - | 19 | 16 | 600 | 54804536 | 0 | java/lang/NullPointerException | 运行正常 |  |
| 都市摩天楼_6192.jar | 160 | Tower Bloxx(TM) | Dchoc | 1.2.4 | MIDP-1.0 | m3g,nokia-ui | 16 | 16 | 600 | 12259461 | 0 | - | 运行正常 |  |
| 都市摩天楼中文版_3219.jar | 1026 | 都市摩天楼 | PALMLINK | 1.0.0 | MIDP-2.0 | nokia-ui | 46 | 16 | 600 | 22886194 | 0 | java/io/EOFException,javax/micro | 运行正常 |  |
| 都市赛车3-暴力街头3D版_10199.jar | 285 | Asphalt 3 3D Street Rules | Gameloftt SA | 1.0.2 | MIDP-2.0 | - | 5 | 16 | 600 | 170042039 | 0 | - | 运行正常 |  |
| 都市赛车3_10192.jar | 286 | Asphalt3: Street Rules | Gameloft SA | 1.0.4 | MIDP-2.0 | - | 5 | 16 | 600 | 17276374 | 0 | - | 运行正常 |  |
| 都市赛车6火线追击_2230.jar | 940 | Asphalt 6 Adrenaline | Gameloft SA | 1.0.0 | MIDP-2.0 | - | 21 | 16 | 600 | 19711084 | 0 | - | 运行正常 |  |
| 都市赛车_11517.jar | 223 | Asphalt | Gameloft SA | 1.0.7 | MIDP-2.0 | - | 5 | 16 | 600 | 451225 | 0 | - | 运行正常 |  |
| 野人岛2宠物精灵_7061.jar | 534 | 宝软网baoruan.com免费分享 | 华娱无线 | 1.0 | MIDP-1.0 | nokia-ui | 24 | 16 | 600 | 2340393 | 0 | - | 运行正常 |  |
| 钻石狂潮_1335.jar | 293 | Diamond Rush | Nokia | 1.1.7 | MIDP-2.0 | - | 12 | 16 | 600 | 2104688 | 0 | javax/microedition/rms/InvalidRe | 运行正常 |  |
| 铠甲勇士2-刑天_2895.jar | 1120 | 铠甲勇士2刑天 | socogame | 1.0.0 | MIDP-1.0 | - | 37 | 16 | 600 | 215977034 | 0 | - | 运行正常 |  |
| 闪电战机－雷霆出击_118.jar | 1388 | 闪电战机-雷霆出击 | CNMOSUN | 2.01 | MIDP-1.0 | - | 129 | 16 | 600 | 45559027 | 0 | java/io/IOException | 运行正常 |  |
| 阿尔卑斯牧场物语_7043.jar | 824 | 阿尔卑斯牧场物 | 北京和讯 | 1.0 | MIDP-1.0 | nokia-ui | 8 | 16 | 600 | 4002605 | 0 | java/lang/NullPointerException | 运行正常 |  |
| 雷欧历险记_11451.jar | 175 | FilaoFriedChicken | Filao | 01.03.08 | MIDP-1.0 | - | 11 | 16 | 600 | 12288499 | 0 | - | 运行正常 |  |
| 雷电2012末日浩劫增值版_3029.jar | 879 | 雷电2012末日浩劫 | HYSJ | 1.0.0 | MIDP-1.0 | - | 40 | 16 | 600 | 319417 | 0 | java/lang/NumberFormatException | 运行正常 |  |
| 雷电4_39.jar | 545 | 雷电4 | vendor | 1.0 | MIDP-2.0 | - | 26 | 16 | 600 | 12042910 | 0 | java/io/IOException | 运行正常 |  |
| 雷电战机(精装升级版)_3138.jar | 876 | 雷电战机(精装升级版) | joymeng | 1.0.0 | MIDP-1.0 | - | 48 | 16 | 600 | 224139191 | 0 | java/io/IOException,java/lang/Cl | 运行正常 |  |
| 雷电战机精装升级BT版_3110.jar | 869 | 雷电战机(精装升级版) | joymeng | 1.0.0 | MIDP-1.0 | - | 48 | 16 | 600 | 244099191 | 0 | java/io/IOException,java/lang/Cl | 运行正常 |  |
| 雷霆战机2012BT版_3101.jar | 851 | 雷霆战机2012 | ls | 1.0.0 | MIDP-1.0 | - | 30 | 16 | 600 | 302745 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| 鬼吹灯(网络火热)_7647.jar | 1007 | qcd | MIG | 1.0.0 | MIDP-1.0 | nokia-ui | 21 | 16 | 600 | 270566049 | 0 | - | 运行正常 |  |
| 鬼泣3D中文版_3376.jar | 1118 | 鬼泣(CAPCOM正版) | CAPCOM | 1.0.0 | MIDP-2.1 | mascot | 26 | 16 | 600 | 3345332 | 0 | java/io/EOFException | 运行正常 |  |
| 魂斗罗3代-完全版_3784.jar | 610 | 魂斗罗3代 | KONAMI | 1.0 | MIDP-1.0 | nokia-ui | 22 | 16 | 600 | 111936 | 0 | java/lang/NullPointerException | 运行正常 |  |
| 魂斗罗4中文版_2226.jar | 530 | 魂斗罗4 | KONAMI | 1.0.0 | MIDP-2.0 | nokia-ui | 56 | 16 | 600 | 90646 | 0 | - | 运行正常 |  |
| 魂斗罗V铁血兵团_119.jar | 990 | 魂斗罗V铁血兵团 | KONAMI | 1.0.0 | MIDP-1.0 | - | 68 | 16 | 600 | 524173 | 0 | java/lang/NumberFormatException | 运行正常 |  |
| 魂斗罗_5652.jar | 896 | 魂斗罗 | KONAMI | 1.0.0 | MIDP-2.0 | nokia-ui | 54 | 16 | 600 | 11772983782 | 0 | java/lang/NullPointerException | 运行正常 |  |
| 魔兽争霸_10622.jar | 59 | MSZB | wap.motoly.com | 1.0 | MIDP-2.0 | nokia-ui | 22 | 16 | 600 | 161335098 | 0 | - | 运行正常 |  |
| 魔兽塔防_12480.jar | 243 | 魔兽塔防 | In-Fusio | 1.0 | MIDP-1.0 | - | 18 | 16 | 600 | 120260 | 0 | java/lang/NullPointerException,j | 运行正常 |  |
| 魔塔_8052.jar | 86 | 魔塔 | 空中丝路 | 1.00 | MIDP-1.0 | nokia-ui | 21 | 16 | 600 | 69919 | 0 | - | 运行正常 |  |
| 魔塔－英雄联盟_582.jar | 1451 | 魔塔-英雄联盟 | FunLikeDown | 3.11 | MIDP-1.0 | nokia-ui | 63 | 16 | 600 | 7265092 | 0 | - | 运行正常 |  |
| 魔法少女_10523.jar | 204 | 魔法少女 | 淘气乐园 | 1.0 | MIDP-1.0 | - | 5 | 16 | 600 | 37313 | 0 | - | 运行正常 |  |
| 鲁宾逊漂流记汉化BT版_4677.jar | 279 | - | - | - | - | - | 0 | 16 | 0 | 0 | 0 | - | 非可用包 | not a usable MIDlet suite (bad JAR or no MID |
| 鲁宾逊漂流记汉化版_702.jar | 367 | 鲁宾逊漂流记-海难生存 | Exkee | 1.0.0 | MIDP-2.0 | - | 34 | 16 | 600 | 4385881 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| 麻将三国_5157.jar | 732 | MJSG | CWA | 1.0 | MIDP-2.0 | nokia-ui | 38 | 16 | 600 | 11032 | 0 | - | 运行正常 |  |
| 麻将无双之乱世群英_10298.jar | 173 | 麻将无双之乱世群英 | 广州摩讯科技 | 1.0 | MIDP-1.0 | nokia-ui | 2 | 16 | 600 | 609881916 | 0 | javax/microedition/rms/RecordSto | 运行正常 |  |
| 麻将象棋五子棋3合1_12014.jar | 311 | 麻将象棋五子棋3合1 | 新浪 | 1.0 | MIDP-1.0 | - | 8 | 16 | 600 | 266045 | 0 | - | 运行正常 |  |
| 黑暗破坏神II_9009.jar | 84 | 黑暗破坏神II | SINA | 1.0 | MIDP-2.0 | - | 10 | 16 | 600 | 165084748 | 0 | - | 运行正常 |  |
| 龙之谷之天空战记_5383.jar | 885 | 龙之谷之天空战记 | M2me | 1.0.0 | MIDP-2.0 | - | 60 | 16 | 600 | 4312895 | 0 | - | 运行正常 |  |
| 龙火2加强版_12437.jar | 117 | 龙火2加强版 | MET | 1.0 | MIDP-1.0 | - | 22 | 16 | 600 | 187457 | 0 | - | 运行正常 |  |
| 龙珠Z传说_9583.jar | 319 | 龙珠Z传说 | sina | 1.0 | MIDP-1.0 | - | 8 | 16 | 600 | 213055 | 0 | - | 运行正常 |  |
| 龙鸟战机_9762.jar | 345 | E-Brid | Mediaplazza | 1.0 | MIDP-1.0 | - | 13 | 16 | 600 | 527805 | 0 | - | 运行正常 |  |
