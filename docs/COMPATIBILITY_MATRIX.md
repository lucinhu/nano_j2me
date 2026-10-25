# Compatibility matrix

One row per JAR in `tests/games` (399 titles), from one headless replay per title
with the same recipe, so neighbouring rows are comparable:

* port `headless`, screen `240x320`, `--fps 30`, `--run-frames 600`;
* heap `16 MB`, retried at `32 MB` only after an out-of-memory;
* one shared input script (fire, both soft keys, two screen taps, four arrows,
  digits, `*`), a fresh RMS directory per title;
* `运行正常` = exit 0, at least half the frames painted, nothing uncaught.
* `未呈现画面` = exit 0, ran to the end on its own, never painted a frame (the run loop had nothing to show - a quiet title, not a crash).

Corpus provenance and size are in `docs/CORPUS.md`; regenerate this table
with `tools/make_compat_matrix.py <rows dir>` after a new sweep.

Built from `1d47ccc`, 2026-10-25.

| verdict | titles |
| --- | --- |
| 运行正常 | 396 |
| 未呈现画面 | 2 |
| 非可用包 | 1 |

| 3D API used | titles |
| --- | --- |
| - | 257 |
| nokia-ui | 113 |
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
| 乱世Q三国_1649.jar | 977 | 乱世Q三国 | moveber | 1.0 | MIDP-1.0 | - | 232 | 16 | 0 | 128 | 0 | javax/microedition/rms/RecordSto | 未呈现画面 | never installs a Displayable (one caught Rec |
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
| 有女倾城破解版_11525.jar | 258 | 有女倾城 | Unknown | 1.0 | MIDP-1.0 | - | 9 | 16 | 0 | 53901 | 0 | - | 未呈现画面 | its Canvas quits from run() before the first |
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
