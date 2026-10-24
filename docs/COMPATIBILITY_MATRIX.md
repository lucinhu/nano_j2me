# Compatibility matrix

One row per JAR in `tests/games` (398 titles), from one headless replay per title
with the same recipe, so neighbouring rows are comparable:

* port `headless`, screen `240x320`, `--fps 30`, `--run-frames 600`;
* heap `16 MB`, retried at `32 MB` only after an out-of-memory;
* one shared input script (fire, both soft keys, two screen taps, four arrows,
  digits, `*`), a fresh RMS directory per title;
* `运行正常` = exit 0, at least half the frames painted, nothing uncaught.

Corpus provenance and size are in `docs/CORPUS.md`; regenerate this table
with `tools/make_compat_matrix.py <rows dir>` after a new sweep.

Built from `1234d75`, 2026-10-25.

| verdict | titles |
| --- | --- |
| 运行正常 | 395 |
| 失败 | 2 |
| 非可用包 | 1 |

| 3D API used | titles |
| --- | --- |
| - | 257 |
| nokia-ui | 112 |
| m3g | 21 |
| m3g,nokia-ui | 5 |
| mascot | 3 |

| title | size KB | MIDlet name | 3D | classes | heap MB | frames | bytecodes | exceptions | verdict | note |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| (新)三国之关云长战曹贼_2550.jar | 934 | (新)三国之关云长战曹贼 | - | 21 | 16 | 600 | 64338 | - | 运行正常 |  |
| 1942空战_10403.jar | 582 | 1942AirCombat supplied by ne | - | 14 | 16 | 600 | 2028382 | java/lang/ArrayIndexOutOfBoundsE | 运行正常 |  |
| 1945空战英雄_6105.jar | 588 | ACH1945 Lite | nokia-ui | 32 | 16 | 600 | 4441180 | - | 运行正常 | needs --device nokia6230 |
| 3D台球之王_5014.jar | 314 | 3D台球之王(EA正版) | - | 23 | 16 | 600 | 130087 | javax/microedition/rms/RecordSto | 运行正常 |  |
| 3D坦克对战1944_7139.jar | 345 | Tank Ace | - | 32 | 16 | 600 | 10868601 | javax/microedition/rms/RecordSto | 运行正常 |  |
| 3D城市桌球_8299.jar | 262 | Biliardo 3D Urban Tour | m3g | 4 | 16 | 600 | 162260 | javax/microedition/rms/RecordSto | 运行正常 |  |
| 3D孤胆枪手2010_4634.jar | 985 | Alien Shooter 3D | nokia-ui | 19 | 16 | 600 | 179208943 | java/io/IOException,java/lang/Ar | 运行正常 |  |
| 3D拉力赛-进化汉化版_6366.jar | 347 | 3DRallyEvolution | m3g | 12 | 16 | 600 | 439402 | javax/microedition/rms/InvalidRe | 运行正常 |  |
| 3D摩托赛车_9287.jar | 335 | 3DMotoRacing by Racer_Team | m3g | 12 | 16 | 600 | 438985 | javax/microedition/rms/InvalidRe | 运行正常 |  |
| 3D武装直升机2_archive.jar | 80 | Attack Chopper 2 | - | 16 | 16 | 600 | 606 | java/lang/NullPointerException | 运行正常 | runs to the last frame; the screen itself st |
| 3D滚球_11586.jar | 69 | 3D QBlast | nokia-ui | 19 | 16 | 600 | 139809 | - | 运行正常 |  |
| 3D激爆拳击_8805.jar | 1024 | K.O. Legends | m3g,nokia-ui | 23 | 16 | 600 | 219563106 | java/lang/NullPointerException | 运行正常 |  |
| 3D版田园物语_6744.jar | 969 | 3D版田园物语 | - | 65 | 16 | 600 | 15764 | - | 运行正常 |  |
| 3D疯狂过山车S60v5版_3875.jar | 797 | Roller Rev 99 | - | 39 | 16 | 600 | 18557 | java/io/EOFException | 运行正常 |  |
| 3D科林麦克雷拉力赛之尘埃_6600.jar | 5601 | Colin McRae DiRT 3D_konno | m3g,nokia-ui | 39 | 16 | 600 | 291470 | - | 运行正常 |  |
| 3D跳台滑雪2011_4507.jar | 964 | SkokiNarciarskie2011 | m3g | 91 | 16 | 600 | 2472233 | java/lang/NullPointerException,j | 运行正常 |  |
| Boun_en_ar_de_he-IL_ru-RU.jar | 40 | Bounce | nokia-ui | 8 | 16 | 600 | 5955 | - | 运行正常 |  |
| CCTV贺岁游戏-三国演义_6527.jar | 929 | 三国演义 | nokia-ui | 33 | 16 | 600 | 75692762 | - | 运行正常 |  |
| CS反恐特警-狙击精英_6539.jar | 417 | (国际巨作)CS反恐特警-狙击精英 | - | 20 | 16 | 600 | 1541069 | java/lang/ArrayIndexOutOfBoundsE | 运行正常 |  |
| CS反恐特警真3D版2_6747.jar | 255 | CS反恐特警真3D版II | - | 34 | 16 | 600 | 453766506 | - | 运行正常 |  |
| CS反恐精英(蓝牙对战)_6858.jar | 335 | Micro Counter Strike v1.1 | m3g | 61 | 16 | 600 | 351 | - | 运行正常 |  |
| FIFA09足球(EA中文版)_7962.jar | 695 | FIFA 09宝软网baoruan.com免费分享 | - | 36 | 16 | 600 | 229968 | javax/microedition/rms/RecordSto | 运行正常 |  |
| Flippy跳跃大冒险_archive.jar | 141 | Flippy's Flying Frenzy | nokia-ui | 13 | 16 | 600 | 1167359 | java/io/EOFException,javax/micro | 运行正常 |  |
| GT赛车2_36.jar | 1815 | GT Racing 2 | nokia-ui | 11 | 16 | 600 | 18648935 | java/lang/NullPointerException | 运行正常 |  |
| LOVExLOVE恋爱学院_4424.jar | 860 | LS | - | 75 | 16 | 600 | 204408 | - | 运行正常 |  |
| NBA2010中文版_6247.jar | 849 | NBA 10（EA正版） | - | 35 | 16 | 600 | 7558 | - | 运行正常 |  |
| NBA2010授权正版_5060.jar | 826 | NBA2010（授权正版） | - | 13 | 16 | 600 | 308701 | - | 运行正常 |  |
| NBA篮球2007_10887.jar | 298 | NBA LIVE 07 | - | 86 | 16 | 600 | 4679582 | javax/microedition/rms/RecordSto | 运行正常 |  |
| NBA职业篮球2010豪华版_7111.jar | 841 | NBA Pro Basketball 2010 | - | 9 | 16 | 600 | 811488500 | - | 运行正常 |  |
| PC正版-使命召唤5_7199.jar | 407 | PC正版-使命召唤5 | - | 21 | 16 | 600 | 13863775 | java/lang/NullPointerException,j | 运行正常 |  |
| QQ飞车2010_5772.jar | 457 | QQ2010 | - | 30 | 16 | 600 | 206842112 | java/io/EOFException | 运行正常 |  |
| Vime_en_ar_de_he-IL_ru-RU.jar | 62 | Virtual Me | nokia-ui | 9 | 16 | 600 | 942244 | javax/microedition/rms/InvalidRe | 运行正常 |  |
| book4.jar | 20 | J2ME Games Examples | - | 10 | 16 | 600 | 151 | - | 运行正常 |  |
| book5.jar | 29 | J2ME Games Examples | - | 14 | 16 | 600 | 155 | - | 运行正常 |  |
| book6.jar | 38 | J2ME Games Examples | - | 16 | 16 | 600 | 167 | - | 运行正常 |  |
| book7.jar | 39 | J2ME Games Examples | - | 17 | 16 | 600 | 167 | - | 运行正常 |  |
| bubblet.jar | 27 | Bubblet | nokia-ui | 14 | 16 | 600 | 5138599 | - | 运行正常 |  |
| game15.jar | 3 | Game15MIDlet | - | 2 | 16 | 600 | 583462 | - | 运行正常 |  |
| hello.jar | 2 | Hello World | - | 2 | 16 | 600 | 29446 | - | 运行正常 |  |
| jump.jar | 15 | J2ME Games Examples | - | 7 | 16 | 600 | 143 | - | 运行正常 |  |
| maze.jar | 7 | J2ME Games Examples | - | 4 | 16 | 600 | 332510 | - | 运行正常 |  |
| slimevolley.jar | 8 | SlimeVolleyBallMidlet | - | 7 | 16 | 600 | 15038 | - | 运行正常 |  |
| vnes.jar | 148 | vNes | nokia-ui | 67 | 16 | 600 | 1117091 | java/io/IOException,java/lang/Nu | 运行正常 |  |
| 七龙珠－终极原始大乱斗_5589.jar | 986 | 七龙珠-终极原始大乱斗 | - | 26 | 16 | 600 | 183996 | - | 运行正常 |  |
| 三国争霸之赤壁大战_11681.jar | 233 | 三国争霸之赤壁大战 | - | 11 | 16 | 600 | 32223 | - | 运行正常 |  |
| 三国塔防－曹操传_1610.jar | 1066 | 三国塔防-曹操传 | nokia-ui | 148 | 16 | 600 | 24096525 | java/lang/ArrayIndexOutOfBoundsE | 运行正常 |  |
| 三国志_10617.jar | 213 | C2S－三国志 | - | 2 | 16 | 600 | 200119561 | - | 运行正常 |  |
| 三国志吞食天地_8144.jar | 513 | 三国志：吞食天地 | - | 4 | 16 | 600 | 304541 | java/lang/NullPointerException | 运行正常 |  |
| 三国志霸王的大陆V3.2.2_5005.jar | 674 | BWDL | - | 7 | 16 | 600 | 451610 | javax/microedition/rms/RecordSto | 运行正常 |  |
| 三国杀2012_809.jar | 1203 | sanguosha | nokia-ui | 81 | 16 | 600 | 12212673 | javax/microedition/rms/RecordSto | 运行正常 |  |
| 三国杀2．2美化版_1347.jar | 1171 | SanGuoQ | - | 376 | 16 | 600 | 5492974 | - | 运行正常 |  |
| 三国杀单机BT版_4227.jar | 318 | 三国杀 | - | 17 | 16 | 600 | 608192 | javax/microedition/rms/RecordSto | 运行正常 |  |
| 三国杀单机战功修改版_4143.jar | 894 | SGSJB | - | 17 | 16 | 600 | 8627078 | javax/microedition/rms/RecordSto | 运行正常 |  |
| 三国杀单机版_4312.jar | 895 | SGSJB | - | 17 | 16 | 600 | 8627078 | javax/microedition/rms/RecordSto | 运行正常 |  |
| 三国群英传_11539.jar | 58 | 奥汀-三国群英传正版 | nokia-ui | 9 | 16 | 600 | 114859 | java/lang/NullPointerException,j | 运行正常 |  |
| 三国群雄争霸_9023.jar | 271 | 超值-三国群雄争霸 | nokia-ui | 3 | 16 | 600 | 127500 | - | 运行正常 |  |
| 三国赤壁小乔秘史_7976.jar | 643 | 小乔秘史wap.91soyo.com破解 | nokia-ui | 24 | 16 | 600 | 193433363 | java/lang/NullPointerException | 运行正常 |  |
| 世嘉头文字D－宿命对决_1247.jar | 1008 | 世嘉头文字D-宿命对决 | m3g | 159 | 16 | 600 | 85355 | javax/microedition/rms/RecordSto | 运行正常 |  |
| 世界五子棋大奖赛_355.jar | 426 | 世界五子棋大奖赛 | - | 9 | 16 | 600 | 39963 | javax/microedition/rms/RecordSto | 运行正常 |  |
| 世界冠军拉力赛3D_8360.jar | 635 | WRC 3D | m3g | 4 | 16 | 600 | 5349785 | java/lang/NullPointerException,j | 运行正常 |  |
| 世界最强-银星围棋_2631.jar | 402 | wjzq | - | 24 | 16 | 600 | 136019 | - | 运行正常 |  |
| 世界武术巡回赛教学版_3484.jar | 442 | 世界武术巡回赛-教学版 | - | 8 | 16 | 600 | 164416071 | - | 运行正常 |  |
| 世界足球2012中文版_1724.jar | 881 | 世界足球2012 | - | 16 | 16 | 600 | 1875230 | java/lang/NullPointerException,j | 运行正常 |  |
| 世界足球2012中文版_780.jar | 881 | 世界足球2012 | - | 16 | 16 | 600 | 1875230 | java/lang/NullPointerException,j | 运行正常 |  |
| 丧尸危机2-自由之翼BT版_997.jar | 772 | 丧尸危机2试玩版 | - | 124 | 16 | 600 | 570673 | - | 运行正常 |  |
| 丧尸围城_10590.jar | 289 | Dead Rising | - | 43 | 16 | 600 | 4233667 | javax/microedition/rms/RecordSto | 运行正常 |  |
| 中华麻将馆－极限胡牌版_244.jar | 310 | 中华麻将馆-极限胡牌版 | - | 18 | 16 | 600 | 308569765 | - | 运行正常 |  |
| 中国象棋_10893.jar | 293 | 中国象棋三国经典版 | - | 9 | 16 | 600 | 73495 | javax/microedition/rms/InvalidRe | 运行正常 |  |
| 中国象棋之水墨珍藏版_871.jar | 375 | 中国象棋之水墨珍藏版 | - | 12 | 16 | 600 | 534667883 | java/lang/ArrayIndexOutOfBoundsE | 运行正常 |  |
| 乱世Q三国_1649.jar | 977 | 乱世Q三国 | - | 232 | 16 | 0 | 128 | javax/microedition/rms/RecordSto | 失败 |  |
| 乱舞三国之群英会_2972.jar | 817 | 乱舞三国之群英会 | - | 35 | 16 | 600 | 482917 | java/lang/NullPointerException | 运行正常 |  |
| 五子棋_10940.jar | 42 | XOXO | - | 24 | 16 | 600 | 4341300 | - | 运行正常 |  |
| 仙剑奇侠传(2008)_12406.jar | 695 | pal | nokia-ui | 3 | 16 | 600 | 209727 | - | 运行正常 |  |
| 仙剑奇侠传_10388.jar | 297 | 仙剑奇侠传 | - | 6 | 16 | 600 | 11599230 | - | 运行正常 |  |
| 仙剑奇侠传完美版_11857.jar | 360 | 仙剑奇侠传 | nokia-ui | 4 | 16 | 600 | 115864 | - | 运行正常 |  |
| 仙逆－诛仙弑神_1441.jar | 1356 | 仙逆-诛仙弑神 | - | 48 | 16 | 600 | 293187111 | java/io/IOException,java/lang/Cl | 运行正常 |  |
| 仙魔飞行棋_417.jar | 294 | 仙魔飞行棋 | - | 21 | 16 | 600 | 516863 | - | 运行正常 |  |
| 众神殿之王子归来_8404.jar | 103 | 王子归来 | nokia-ui | 14 | 16 | 600 | 1293718 | - | 运行正常 |  |
| 使命召唤4现代战争_9388.jar | 449 | Call of Duty 4 by serviak | - | 5 | 16 | 600 | 2386864 | javax/microedition/rms/RecordSto | 运行正常 |  |
| 使命召唤5-战争世界中文版_4583.jar | 416 | 使命召唤5-战争世界 | - | 22 | 16 | 600 | 426494410 | java/lang/IllegalArgumentExcepti | 运行正常 |  |
| 侠盗飞车_6246.jar | 360 | 侠盗飞车-Gameloft正版 | - | 6 | 16 | 600 | 120467146 | - | 运行正常 |  |
| 侠盗飞车加强版_5640.jar | 350 | 侠盗飞车-Gameloft正版 | - | 11 | 16 | 600 | 182566374 | javax/microedition/rms/InvalidRe | 运行正常 |  |
| 俄罗斯方块中文版_2832.jar | 121 | 俄罗斯方块 | - | 56 | 16 | 600 | 394288 | - | 运行正常 |  |
| 倚天屠龙之君临天下_12230.jar | 564 | 倚天屠龙之君临天下 | - | 64 | 16 | 600 | 2763649 | java/lang/NumberFormatException, | 运行正常 |  |
| 倚天屠龙记_4827.jar | 836 | 倚天屠龙记 | nokia-ui | 62 | 16 | 600 | 3408045 | - | 运行正常 |  |
| 倚天屠龙记之魔教教主_4663.jar | 798 | GEYTTJZ | - | 7 | 16 | 600 | 282392814 | - | 运行正常 |  |
| 傲世三国志－群雄争霸_5633.jar | 514 | 傲世三国志-群雄争霸 | nokia-ui | 15 | 16 | 600 | 344067537 | - | 运行正常 |  |
| 傲气雄鹰_10432.jar | 249 | 傲气雄鹰汉化版 | - | 10 | 16 | 600 | 849272 | - | 运行正常 |  |
| 傲视三国之赵云传_11024.jar | 235 | 傲视三国之赵云传 | nokia-ui | 22 | 16 | 600 | 388550 | - | 运行正常 |  |
| 僵尸危机2_4349.jar | 1068 | Zombie Infection 2 | - | 12 | 16 | 600 | 6006772722 | java/lang/ArrayIndexOutOfBoundsE | 运行正常 |  |
| 兔子波比2冰原之旅汉化版_11509.jar | 68 | BC2 - Winterland | nokia-ui | 11 | 16 | 600 | 82438 | - | 运行正常 |  |
| 冠军足球经理2010汉化版_5198.jar | 543 | CM-2010 | nokia-ui | 21 | 16 | 600 | 32142124 | - | 运行正常 |  |
| 冰淇淋少女成长日记_7307.jar | 344 | 冰淇淋少女成长日记_玉品 | nokia-ui | 9 | 16 | 600 | 470532 | - | 运行正常 |  |
| 凡人修仙传2-乱星海_12474.jar | 979 | 凡人修仙传2-乱星海 | - | 18 | 16 | 600 | 151226 | java/lang/NullPointerException | 运行正常 |  |
| 凡人修仙传之七派六宗_4145.jar | 984 | 凡人修仙传 | nokia-ui | 16 | 16 | 600 | 156258 | java/io/IOException,java/lang/Nu | 运行正常 |  |
| 凤凰战机_10636.jar | 216 | Ultra Phoenix - Resurgence | - | 32 | 16 | 600 | 6802252 | javax/microedition/rms/RecordSto | 运行正常 |  |
| 初恋物语_7612.jar | 286 | 初恋物语 | - | 9 | 16 | 600 | 35787 | javax/microedition/rms/RecordSto | 运行正常 |  |
| 刺客信条3_266.jar | 1623 | Assassin's Creed III | nokia-ui | 12 | 16 | 600 | 117335556 | java/lang/NullPointerException | 运行正常 |  |
| 刺客信条兄弟会中文版_3668.jar | 1213 | 刺客信条-兄弟会 | - | 12 | 16 | 600 | 14060232 | java/lang/NullPointerException | 运行正常 |  |
| 剑侠奇缘-玉女问情_2208.jar | 703 | 剑侠奇缘-玉女问情 | - | 30 | 16 | 600 | 1778988 | - | 运行正常 |  |
| 剑侣情缘Ⅱ魔神至尊传(无bug)_12464.jar | 904 | 剑侣情缘Ⅱ魔神至尊传 | - | 20 | 16 | 600 | 513439 | - | 运行正常 |  |
| 剑灵－古剑魂梦_676.jar | 1531 | 剑灵-古剑魂梦 | nokia-ui | 50 | 16 | 600 | 56540 | - | 运行正常 |  |
| 午夜保龄球2中文版_9782.jar | 589 | 终极保龄球2 | nokia-ui | 12 | 16 | 600 | 427644108 | java/lang/NullPointerException,j | 运行正常 |  |
| 午夜台球圣诞中文版_8677.jar | 221 | 极限台球挑战赛 | - | 6 | 16 | 600 | 252490978 | - | 运行正常 |  |
| 危城无双之三国群英_4891.jar | 220 | 危城无双 | nokia-ui | 2 | 16 | 600 | 247186600 | javax/microedition/rms/RecordSto | 运行正常 |  |
| 反恐精英2正式版_5836.jar | 510 | CT episod 2 | nokia-ui | 21 | 16 | 600 | 8254105 | java/io/IOException | 运行正常 |  |
| 口袋仓鼠_11936.jar | 441 | Lab Hamster\gkataria | - | 23 | 16 | 600 | 5316027 | java/lang/NullPointerException | 运行正常 |  |
| 口袋妖怪_7872.jar | 54 | 口袋妖怪 | nokia-ui | 8 | 16 | 600 | 852934 | - | 运行正常 |  |
| 口袋妖怪水晶汉化版_4524.jar | 1067 | MeBoy | - | 7 | 16 | 600 | 643 | javax/microedition/rms/RecordSto | 运行正常 |  |
| 口袋灵兽_9902.jar | 582 | 口袋灵兽 | nokia-ui | 41 | 16 | 600 | 47519 | - | 运行正常 |  |
| 古剑奇缘-诛仙_4913.jar | 954 | 古剑奇缘-诛仙 | - | 64 | 16 | 600 | 46107181 | java/lang/NumberFormatException | 运行正常 |  |
| 台球传奇_87.jar | 917 | 台球传奇 | - | 53 | 16 | 600 | 403070 | java/lang/NumberFormatException, | 运行正常 |  |
| 史前部落_11059.jar | 486 | Prehistoric Tribes | - | 7 | 16 | 600 | 143811972 | - | 运行正常 |  |
| 合金弹头2011_5629.jar | 401 | 合金弹头2011 | - | 7 | 16 | 600 | 465334397 | javax/microedition/rms/RecordSto | 运行正常 |  |
| 合金弹头2013BT版_678.jar | 1021 | 合金弹头2013(正版) | - | 19 | 16 | 600 | 244487 | java/lang/ClassNotFoundException | 运行正常 |  |
| 合金弹头4(完美汉化版)_4053.jar | 798 | Metal Slug 4 | - | 6 | 16 | 600 | 3828627 | javax/microedition/rms/RecordSto | 运行正常 |  |
| 周末的沙滩_7453.jar | 1480 | Beach Games 12-Pack | nokia-ui | 37 | 16 | 600 | 3006903 | java/io/EOFException,javax/micro | 运行正常 |  |
| 哆啦A梦-秘密道具大作战_6557.jar | 552 | 哆啦A梦-秘密道具大作战(正版) | nokia-ui | 26 | 16 | 600 | 548955346 | - | 运行正常 |  |
| 哆啦A梦运动会_10939.jar | 348 | 哆啦A梦运动会(正版)W | - | 17 | 16 | 600 | 542524 | javax/microedition/media/MediaEx | 运行正常 |  |
| 哆啦A梦－3D卡丁车增值版_1560.jar | 1104 | 哆啦A梦-3D卡丁车 | nokia-ui | 73 | 16 | 600 | 5721931 | java/io/IOException,java/lang/Nu | 运行正常 |  |
| 喜羊羊与灰太狼_9241.jar | 296 | 喜羊羊与灰太狼 | nokia-ui | 11 | 16 | 600 | 180177712 | - | 运行正常 |  |
| 国色天香MM斗地主真人版_11364.jar | 324 | 国色天香-MM斗地主(真人版) | - | 6 | 16 | 600 | 93953 | javax/microedition/rms/InvalidRe | 运行正常 |  |
| 国色天香斗地主_1364.jar | 1249 | 国色天香斗地主 | nokia-ui | 58 | 16 | 600 | 17379876 | java/lang/NumberFormatException | 运行正常 |  |
| 圣百合女子学园-纯色序曲_4875.jar | 993 | SaintLily | - | 12 | 16 | 600 | 445931 | - | 运行正常 |  |
| 地下城与勇士之鬼剑士_9408.jar | 381 | 地下城与勇士-鬼剑士 | - | 27 | 16 | 600 | 11220086 | - | 运行正常 |  |
| 地心探险(地心游记)_12471.jar | 94 | 新浪-地心游记 | - | 9 | 16 | 600 | 4267624621 | - | 运行正常 |  |
| 坦克大战（经典版）_190.jar | 570 | 3D坦克大战(经典版) | - | 18 | 16 | 600 | 1461959 | - | 运行正常 |  |
| 塔塔_2848.jar | 693 | 塔塔 | - | 6 | 16 | 600 | 9377 | java/lang/ClassNotFoundException | 运行正常 |  |
| 夜半鬼吹灯_8363.jar | 222 | 夜半鬼吹灯 | nokia-ui | 5 | 16 | 600 | 26581 | - | 运行正常 |  |
| 夜店3D斗地主_2296.jar | 847 | 夜店3D斗地主 | - | 15 | 16 | 600 | 22189 | javax/microedition/rms/InvalidRe | 运行正常 |  |
| 大富翁4纵横四海_8749.jar | 273 | 大富翁4纵横四海 | - | 13 | 16 | 600 | 341626 | - | 运行正常 |  |
| 大富翁4－大宇正版_467.jar | 636 | 大富翁4大宇正版 | - | 17 | 16 | 600 | 7762359 | - | 运行正常 |  |
| 大富翁中国之旅_11873.jar | 97 | DFWZGZL | - | 10 | 16 | 600 | 33580 | java/lang/NullPointerException,j | 运行正常 |  |
| 大魔界村_11996.jar | 1234 | Ghouls N Ghosts | nokia-ui | 103 | 16 | 600 | 2020218 | javax/microedition/rms/RecordSto | 运行正常 |  |
| 大鱼吃小鱼－深海探险2_614.jar | 633 | 大鱼吃小鱼-深海探险2 | - | 63 | 16 | 600 | 130579 | javax/microedition/rms/RecordSto | 运行正常 |  |
| 太空冲击303_archive.jar | 85 | Space Impact III | nokia-ui | 11 | 16 | 600 | 500106 | - | 运行正常 |  |
| 头文字D之疾风骤雨_2140.jar | 938 | 世嘉头文字D-疾风骤雨 | m3g | 26 | 16 | 600 | 81788 | javax/microedition/rms/RecordSto | 运行正常 |  |
| 奥特曼-宇宙英雄超银河传说_2233.jar | 1245 | 奥特曼-宇宙英雄超银河传说 | - | 139 | 16 | 600 | 38517 | javax/microedition/media/MediaEx | 运行正常 |  |
| 女仆咖啡厅_1244.jar | 640 | 女仆咖啡厅 | - | 42 | 16 | 600 | 44674 | - | 运行正常 |  |
| 女神转校生-心跳回忆_4993.jar | 908 | 女神转校生-心跳回忆 | - | 58 | 16 | 600 | 1672691 | java/lang/ClassNotFoundException | 运行正常 |  |
| 孢子生命起源_6515.jar | 658 | EA-孢子生命起源 | - | 42 | 16 | 600 | 43089 | - | 运行正常 |  |
| 孤岛余生2时间之谜汉化版_7371.jar | 616 | 孤岛余生2 | m3g | 36 | 16 | 600 | 2117892 | javax/microedition/rms/InvalidRe | 运行正常 |  |
| 孤岛余生汉化版_10145.jar | 453 | Stranded | - | 9 | 16 | 600 | 591789 | - | 运行正常 |  |
| 学姐的秘密_9071.jar | 121 | 学姐的秘密 | - | 10 | 16 | 600 | 74704 | - | 运行正常 |  |
| 宇宙战机_10521.jar | 175 | Triad | - | 19 | 16 | 600 | 12128087 | java/lang/ClassNotFoundException | 运行正常 |  |
| 实况足球2008中文版_9284.jar | 567 | 世界足球2008-明星正版 | - | 7 | 16 | 600 | 141318119 | - | 运行正常 |  |
| 实况足球2012_2122.jar | 905 | Real Football 2012 | - | 14 | 16 | 600 | 156941479 | java/lang/NullPointerException | 运行正常 |  |
| 实况足球经理2009_10609.jar | 652 | RF MANAGER 09 | nokia-ui | 8 | 16 | 600 | 527361808 | java/lang/NullPointerException,j | 运行正常 |  |
| 宠物之王_12445.jar | 933 | 91soyo.com-宠物之王 | - | 63 | 16 | 600 | 1700904 | javax/microedition/rms/RecordSto | 运行正常 |  |
| 宠物狗狗中文版_11635.jar | 253 | 无敌Z狗狗 | nokia-ui | 6 | 16 | 600 | 351410103 | java/lang/NullPointerException | 运行正常 |  |
| 宠物王国2紫_5550.jar | 438 | cwwg2_z | nokia-ui | 19 | 16 | 600 | 507396 | - | 运行正常 |  |
| 宠物王国3金_4892.jar | 585 | 宠物王国3(宝软网baoruan.com免费分享 | nokia-ui | 22 | 16 | 600 | 143450 | - | 运行正常 |  |
| 宠物王国4-白金_6531.jar | 699 | 宠物王国4-白金 | nokia-ui | 16 | 16 | 600 | 603400 | java/lang/NumberFormatException | 运行正常 |  |
| 宠物王国5-彩虹_3413.jar | 1029 | 宠物王国5-彩虹 | nokia-ui | 23 | 16 | 600 | 124222 | - | 运行正常 |  |
| 宠物王国5-彩虹二次BT版_3356.jar | 1125 | 宠物王国5-彩虹 | nokia-ui | 28 | 16 | 600 | 133927 | - | 运行正常 |  |
| 宠物王国6(去模拟器限制)_12473.jar | 1387 | PetKing6 | - | 72 | 16 | 600 | 2718209 | java/io/EOFException,java/lang/N | 运行正常 |  |
| 宠物王国6－烈火_465.jar | 1449 | 宠物王国6-烈火 | - | 121 | 16 | 600 | 2203830 | java/io/EOFException | 运行正常 |  |
| 宠物精灵爱之体验_2861.jar | 1391 | Lovepet | - | 6 | 16 | 600 | 190665 | - | 运行正常 |  |
| 家园4汉化版_10405.jar | 215 | Townsmen4 | nokia-ui | 8 | 16 | 600 | 3286686 | java/lang/NullPointerException | 运行正常 |  |
| 家园5汉化版_10376.jar | 239 | Townsmen5 | nokia-ui | 7 | 16 | 600 | 2153459 | - | 运行正常 |  |
| 家园6之革命_7692.jar | 643 | Townsmen 6 | - | 26 | 16 | 601 | 1333313 | java/lang/NullPointerException | 运行正常 |  |
| 家园6之革命完美汉化版_7583.jar | 590 | 家园6_光辉革命 | - | 26 | 16 | 601 | 924707 | java/lang/NullPointerException | 运行正常 |  |
| 寂静岭2汉化版_10177.jar | 507 | Silent Hill Mobile 2 | - | 12 | 16 | 600 | 6880487 | java/io/IOException,java/lang/Nu | 运行正常 |  |
| 寂静岭汉化版_9575.jar | 415 | Silent Hill | - | 11 | 16 | 600 | 3208468 | java/io/IOException,javax/microe | 运行正常 |  |
| 密室逃脱_7300.jar | 103 | 密室逃脱 | nokia-ui | 5 | 16 | 600 | 501359 | - | 运行正常 |  |
| 寻秦皇陵的诅咒_12450.jar | 573 | 寻秦 | nokia-ui | 33 | 16 | 600 | 374128 | - | 运行正常 |  |
| 帝国时代2汉化版_7864.jar | 62 | 帝国时代2（微软正版） | nokia-ui | 9 | 16 | 600 | 797237 | javax/microedition/rms/InvalidRe | 运行正常 |  |
| 帝国时代3-亚洲王朝_6288.jar | 595 | 帝国时代III亚洲王朝 | - | 41 | 16 | 600 | 2044721 | javax/microedition/rms/RecordSto | 运行正常 |  |
| 帝国时代Ⅲ汉化版_10379.jar | 426 | Age Of Empires III Mobile | - | 10 | 16 | 600 | 4792690 | - | 运行正常 |  |
| 开心仓鼠大作战_3911.jar | 506 | kxcsdz | - | 38 | 16 | 600 | 312641 | java/lang/IllegalArgumentExcepti | 运行正常 |  |
| 开心农场BT版_6075.jar | 624 | 开心农场 | - | 13 | 16 | 600 | 326998 | java/lang/NullPointerException | 运行正常 |  |
| 开心农场_240.jar | 374 | happy_farm | - | 39 | 16 | 600 | 44786 | - | 运行正常 |  |
| 开心农场_6370.jar | 374 | happy_farm | - | 39 | 16 | 600 | 44786 | - | 运行正常 |  |
| 开心餐厅2012_745.jar | 794 | 开心餐厅2012 | - | 76 | 16 | 600 | 82540 | java/lang/NullPointerException | 运行正常 |  |
| 异形战机汉化版_8382.jar | 268 | R-Type | - | 5 | 16 | 600 | 340766 | java/io/IOException,java/lang/Nu | 运行正常 |  |
| 彩京打击者1945_10623.jar | 206 | Strikers 1945 | - | 4 | 16 | 600 | 9973 | - | 运行正常 |  |
| 彩虹岛汉化版_10241.jar | 124 | Rainbow Islands [MICHANG] | - | 16 | 16 | 600 | 860130 | - | 运行正常 |  |
| 德州扑克2_3884.jar | 985 | Texas Hold'Em Poker 2 | - | 26 | 16 | 600 | 87769472 | java/lang/NullPointerException | 运行正常 |  |
| 忍者神龟之最终的判决_8288.jar | 357 | TMNT The Ninja Tribunal | - | 4 | 16 | 600 | 11353431 | java/lang/NullPointerException,j | 运行正常 |  |
| 快乐农场_2089.jar | 782 | 快乐农场之僵尸来袭 | - | 36 | 16 | 600 | 101431 | - | 运行正常 |  |
| 快打旋风_10825.jar | 273 | FinalFight | - | 4 | 16 | 600 | 412661 | - | 运行正常 |  |
| 急速公路狂飙－新年版_84.jar | 880 | 急速公路狂飙新年版 | m3g | 119 | 16 | 600 | 18883611 | javax/microedition/rms/RecordSto | 运行正常 |  |
| 急速狂飙完美破解版_4964.jar | 1301 | HighSpeed 3D | m3g | 48 | 16 | 600 | 78212684 | java/io/EOFException,java/io/IOE | 运行正常 |  |
| 恋爱物语－我的机器女友_6219.jar | 973 | cyborg_lover | - | 66 | 16 | 600 | 38404 | - | 运行正常 |  |
| 恋色狂想之魔女恋歌_4899.jar | 774 | LCMS | - | 88 | 16 | 600 | 616401 | - | 运行正常 |  |
| 愤怒的小鸟-高清版_2353.jar | 16451 | Angry Birds - by novchik | - | 45 | 16 | 600 | 4697031 | java/lang/ClassNotFoundException | 运行正常 |  |
| 愤怒的小鸟_383.jar | 6333 | Angry Birds | - | 145 | 16 | 600 | 66336 | java/lang/NullPointerException,j | 运行正常 |  |
| 我们要恋爱之校园篇_1815.jar | 779 | 我们要恋爱之校园篇 | - | 32 | 16 | 600 | 18488 | javax/microedition/media/MediaEx | 运行正常 |  |
| 我的宠物我做主BT版_3055.jar | 633 | 我的宠物我做主(正版)_玉品 | - | 20 | 16 | 600 | 374175 | - | 运行正常 |  |
| 战争机器1945_5414.jar | 466 | 1945 | - | 11 | 16 | 600 | 47988 | javax/microedition/rms/RecordSto | 运行正常 |  |
| 战争艺术2之全球联盟中文版_1918.jar | 573 | 战争艺术2 | - | 15 | 16 | 600 | 2200699 | java/io/EOFException,java/lang/N | 运行正常 |  |
| 扫雷行动_10904.jar | 559 | Minesweeper Mobile | nokia-ui | 11 | 16 | 600 | 29383792 | java/lang/NullPointerException | 运行正常 |  |
| 拳王2009南拳北腿_1167.jar | 331 | 拳王2009-南拳北腿 | - | 8 | 16 | 600 | 144248 | - | 运行正常 |  |
| 拳王争霸_11585.jar | 582 | Lucha Libre | nokia-ui | 6 | 16 | 600 | 748775617 | - | 运行正常 |  |
| 拳皇98终极之战_7534.jar | 831 | 拳皇98-终极之战 | - | 37 | 16 | 600 | 512853 | - | 运行正常 |  |
| 捕鱼达人3金装版_1511.jar | 634 | 深海猎奇-捕鱼达人 | nokia-ui | 15 | 16 | 600 | 169932 | java/io/IOException,java/lang/Nu | 运行正常 |  |
| 文明3_10522.jar | 160 | Civilization 3 | - | 7 | 16 | 600 | 277231 | - | 运行正常 |  |
| 斗po苍穹bt版_1127.jar | 1609 | 斗破苍穹 | - | 37 | 16 | 600 | 90329 | java/io/IOException,java/lang/Nu | 运行正常 |  |
| 斩妖伏魔录3_6306.jar | 1016 | 斩妖伏魔录3 | nokia-ui | 21 | 16 | 600 | 6079154 | java/io/IOException,java/lang/Nu | 运行正常 |  |
| 斩妖伏魔录4(QQ挑战)_2922.jar | 1043 | 斩妖伏魔录4 | nokia-ui | 55 | 16 | 600 | 8411797 | java/io/IOException,java/lang/Nu | 运行正常 |  |
| 斩妖伏魔录_6231.jar | 584 | 斩妖伏魔录(华娱) | nokia-ui | 19 | 16 | 600 | 3823140 | java/io/IOException,java/lang/Nu | 运行正常 |  |
| 新2011拳皇_4132.jar | 948 | 新拳皇2011 | - | 33 | 16 | 600 | 484431 | java/io/EOFException,javax/micro | 运行正常 |  |
| 新三国志_10225.jar | 186 | 新三国志 | - | 17 | 16 | 600 | 46509 | javax/microedition/rms/RecordSto | 运行正常 |  |
| 新牧场物语2恋爱季节_1340.jar | 991 | 新牧场物语2恋爱季节 | - | 73 | 16 | 600 | 76848 | - | 运行正常 |  |
| 新西游记－格斗天王_1211.jar | 1327 | 新西游记-格斗天王 | - | 56 | 16 | 600 | 1480567 | java/lang/NumberFormatException | 运行正常 |  |
| 时光八音盒2忆之篇_3865.jar | 1014 | 时光八音盒II_忆之篇_玉品 | - | 11 | 16 | 600 | 126318 | java/lang/ArrayIndexOutOfBoundsE | 运行正常 |  |
| 时光八音盒萌之篇_3806.jar | 961 | sgbyh | - | 11 | 16 | 600 | 126298 | java/lang/ArrayIndexOutOfBoundsE | 运行正常 |  |
| 时光樱花册－遗失之心_1009.jar | 1380 | 时光樱花册-遗失之心 | - | 33 | 16 | 600 | 603706 | - | 运行正常 |  |
| 明星制造梦工厂_11048.jar | 220 | 明星制造梦工厂 | - | 8 | 16 | 600 | 228872 | - | 运行正常 |  |
| 明星麻将馆美女_9760.jar | 362 | 明星麻将馆(美女真人版) | nokia-ui | 11 | 16 | 600 | 4317743 | - | 运行正常 |  |
| 星空恋曲-遥远的记忆(修正版)_4008.jar | 982 | STAR | - | 84 | 16 | 600 | 513646 | - | 运行正常 |  |
| 星辰变S60V5完整版_3364.jar | 926 | 星辰变(盛大正版) | nokia-ui | 40 | 16 | 600 | 250348802 | java/io/IOException | 运行正常 |  |
| 是男人就下100层_1882.jar | 636 | 是男人就下100层 | nokia-ui | 75 | 16 | 600 | 6964968 | java/io/IOException,java/lang/Nu | 运行正常 |  |
| 暗黑启示录2－黑暗王朝_888.jar | 1268 | 暗黑启示录2黑暗王朝 | - | 214 | 16 | 600 | 127252238 | - | 运行正常 |  |
| 有女倾城破解版_11525.jar | 258 | 有女倾城 | - | 9 | 16 | 0 | 53901 | - | 失败 |  |
| 机甲风暴_7888.jar | 673 | 机甲风暴 | nokia-ui | 41 | 16 | 600 | 95517 | - | 运行正常 |  |
| 杀戮机器-纳粹僵尸_3610.jar | 995 | 杀戮机器-纳粹僵尸 | m3g | 37 | 16 | 600 | 741606 | java/lang/NumberFormatException | 运行正常 |  |
| 杀鬼子-敌后武工队_2185.jar | 515 | 杀鬼子-敌后武工队 | nokia-ui | 37 | 16 | 600 | 147638505 | java/io/IOException | 运行正常 |  |
| 极品飞车13之变速英文3D版_6646.jar | 754 | Need for Speed Shift | m3g | 38 | 16 | 600 | 15073340 | - | 运行正常 |  |
| 极品飞车14之热力追踪中文版_4160.jar | 1219 | 极品飞车：热血追踪 | m3g | 44 | 16 | 600 | 256001 | - | 运行正常 |  |
| 极品飞车16：亡命狂飙_1519.jar | 1046 | Need for Speed The Run | m3g,nokia-ui | 41 | 16 | 600 | 11625128 | - | 运行正常 |  |
| 极品飞车之无间风云FX中文版_4518.jar | 1000 | 极品飞车无间风云FX | m3g | 63 | 16 | 600 | 3602809 | - | 运行正常 |  |
| 极品飞车精装版_7557.jar | 159 | 极品飞车精装版 | - | 16 | 16 | 600 | 245889 | javax/microedition/rms/RecordSto | 运行正常 |  |
| 柯楠之密室杀人事件_3821.jar | 429 | 柯楠－密室杀人事件 | nokia-ui | 39 | 16 | 600 | 109418 | - | 运行正常 |  |
| 校园回忆_1940.jar | 358 | 校园回忆 | - | 16 | 16 | 600 | 40540 | - | 运行正常 |  |
| 格斗2011-拳王争霸_4253.jar | 805 | GDQWZB | - | 8 | 16 | 600 | 282374014 | java/lang/NullPointerException | 运行正常 |  |
| 格斗三国五虎将精装版_9085.jar | 275 | 格斗三国五虎将(精装版) | - | 8 | 16 | 600 | 215449 | javax/microedition/rms/InvalidRe | 运行正常 |  |
| 格斗少女_3271.jar | 709 | gdsn | - | 14 | 16 | 600 | 25710 | - | 运行正常 |  |
| 格斗火柴人_3938.jar | 416 | Enter The Flip | - | 11 | 16 | 600 | 176281 | - | 运行正常 |  |
| 梦幻西游_4223.jar | 1024 | 梦幻西游 | - | 88 | 16 | 600 | 4636022 | javax/microedition/rms/RecordSto | 运行正常 |  |
| 植物大战僵尸(新角色加强版)_982.jar | 421 | 植物大战僵尸 | - | 37 | 16 | 600 | 40248 | - | 运行正常 |  |
| 植物大战僵尸2011加强版_2912.jar | 533 | 植物大战僵尸(2011加强版) | - | 56 | 16 | 600 | 119063 | javax/microedition/rms/RecordSto | 运行正常 |  |
| 植物大战僵尸2013完美升级版_326.jar | 516 | 植物大战僵尸 | - | 41 | 16 | 600 | 265101 | - | 运行正常 |  |
| 植物大战僵尸BT版_650.jar | 905 | 植物大战僵尸 | - | 99 | 16 | 601 | 14420166 | java/lang/NumberFormatException | 运行正常 |  |
| 植物大战僵尸欧美大波版_407.jar | 510 | 植物大战僵尸2013欧美大波版 | - | 34 | 16 | 600 | 284872 | - | 运行正常 |  |
| 模拟人生2_10762.jar | 273 | 模拟人生2 | - | 31 | 16 | 600 | 163486289 | javax/microedition/rms/RecordSto | 运行正常 |  |
| 模拟人生2完美版_11722.jar | 417 | The Sims 2 | nokia-ui | 20 | 16 | 600 | 23770411 | - | 运行正常 |  |
| 模拟人生2宠物当家_10928.jar | 410 | 模拟人生2宠物当家 | - | 35 | 16 | 600 | 1394791 | - | 运行正常 |  |
| 模拟人生3_7562.jar | 559 | The Sims 3 | - | 28 | 16 | 600 | 14123626 | - | 运行正常 |  |
| 模拟人生3中文版_6162.jar | 492 | 模拟人生3(EA正版) | - | 25 | 16 | 600 | 8148660 | - | 运行正常 |  |
| 模拟人生3之野心_4936.jar | 817 | The Sims 3 Ambitions | - | 26 | 16 | 600 | 10020844 | java/io/EOFException | 运行正常 |  |
| 模拟人生3繁体bt版_6026.jar | 497 | The Sims 3 | - | 27 | 16 | 600 | 10114466 | - | 运行正常 |  |
| 模拟恋爱2_11543.jar | 291 | SimBabe2 by serviak | - | 2 | 16 | 600 | 12591353 | - | 运行正常 |  |
| 武林外传-明星斗地主增值版_3971.jar | 689 | 武林外传-明星斗地主(电影正版) | - | 52 | 16 | 600 | 41867 | - | 运行正常 |  |
| 武林外传_9847.jar | 400 | 武林外传-RPG巨作正版 | nokia-ui | 85 | 16 | 600 | 417636 | - | 运行正常 |  |
| 歪歪隋唐英雄传_11206.jar | 52 | waiwai | nokia-ui | 6 | 16 | 600 | 43508 | java/io/IOException | 运行正常 |  |
| 毁灭战士3_10598.jar | 1590 | Doom3D | m3g | 48 | 16 | 600 | 100207 | java/io/IOException,java/lang/Nu | 运行正常 |  |
| 比基尼美女海底影相_9643.jar | 275 | Foto Quest Bikini | nokia-ui | 14 | 16 | 600 | 328295 | java/io/EOFException | 运行正常 |  |
| 水浒108将-激斗篇_2196.jar | 625 | 水浒108将-激斗篇 | - | 33 | 16 | 600 | 15601 | java/io/IOException | 运行正常 |  |
| 法拉利GT3－世界赛道BT版_776.jar | 2881 | Ferrari GT 3 - World Track | nokia-ui | 22 | 16 | 600 | 507509797 | java/lang/ArrayIndexOutOfBoundsE | 运行正常 |  |
| 波斯王子之时之沙中文版_11315.jar | 60 | 波斯王子-时之沙 | nokia-ui | 6 | 16 | 600 | 116655100 | - | 运行正常 |  |
| 波斯王子之武者之心_8424.jar | 202 | Prince Of Persia | - | 5 | 16 | 600 | 279945525 | - | 运行正常 |  |
| 波斯王子经典版_9715.jar | 326 | Prince of Persia Classic | - | 9 | 16 | 600 | 543579651 | java/lang/NullPointerException | 运行正常 |  |
| 浴火银河2完整版_7689.jar | 1639 | Galaxy on Fire 2 | m3g | 154 | 16 | 600 | 5740016 | javax/microedition/rms/RecordSto | 运行正常 |  |
| 浴火银河2汉化版_6950.jar | 1680 | Galaxy on Fire 2 | m3g | 155 | 16 | 600 | 10297388 | javax/microedition/rms/RecordSto | 运行正常 |  |
| 浴火银河2瓦尔基里(汉化)_3067.jar | 9771 | Galaxy on Fire 2 | m3g | 155 | 16 | 600 | 10322399 | javax/microedition/rms/RecordSto | 运行正常 |  |
| 浴火银河2瓦尔基里_3093.jar | 9746 | Galaxy on Fire 2 | m3g | 154 | 16 | 600 | 5813844 | javax/microedition/rms/RecordSto | 运行正常 |  |
| 浴火银河_10189.jar | 510 | 3D Galaxy on Fire | mascot | 101 | 16 | 600 | 10057792 | javax/microedition/rms/RecordSto | 运行正常 |  |
| 浴火银河_3621.jar | 510 | 3D Galaxy on Fire | mascot | 101 | 16 | 600 | 11239941 | javax/microedition/rms/RecordSto | 运行正常 |  |
| 海绵宝宝卡丁车_4684.jar | 651 | HMBBKDC | - | 9 | 16 | 600 | 171785782 | javax/microedition/rms/InvalidRe | 运行正常 |  |
| 涂鸦地带_模拟可玩_12472.jar | 62 | 涂鸦地带 | - | 22 | 16 | 600 | 312614 | java/io/IOException,javax/microe | 运行正常 |  |
| 涂鸦跳跃中文豪华版(汉化)_2524.jar | 1424 | 涂鸦跳跃 | - | 13 | 16 | 600 | 559291924 | - | 运行正常 |  |
| 灌篮高手-爆发!樱木_7947.jar | 327 | 灌篮高手-爆发！樱木 | - | 44 | 16 | 600 | 426385 | java/lang/ArithmeticException,ja | 运行正常 |  |
| 火影忍者-终极格斗_6604.jar | 673 | 火影忍者-精品格斗2010 | nokia-ui | 41 | 16 | 600 | 5282513 | javax/microedition/rms/RecordSto | 运行正常 |  |
| 火影忍者_11531.jar | 335 | Naruto | nokia-ui | 9 | 16 | 600 | 12818 | javax/microedition/rms/InvalidRe | 运行正常 |  |
| 火影忍者格斗2010_4601.jar | 652 | 火影忍者-精品格斗2010 | nokia-ui | 39 | 16 | 600 | 5282513 | javax/microedition/rms/RecordSto | 运行正常 |  |
| 火影忍者汉化版_10735.jar | 261 | Ninja Fighter (RoS) | nokia-ui | 22 | 16 | 687 | 2619492 | java/io/EOFException,java/io/IOE | 运行正常 |  |
| 火焰-纹章之谜_12128.jar | 316 | 火焰-纹章之谜 | nokia-ui | 17 | 16 | 600 | 857891 | java/io/IOException,java/lang/Nu | 运行正常 |  |
| 火焰-纹章传说第一章_12129.jar | 365 | 火焰-纹章传说第一章 | nokia-ui | 18 | 16 | 600 | 823448 | java/io/IOException,java/lang/Nu | 运行正常 |  |
| 火焰-纹章传说第三章_12130.jar | 354 | 火焰-纹章传说第三章 | nokia-ui | 18 | 16 | 600 | 1296165 | java/io/IOException,java/lang/Nu | 运行正常 |  |
| 火焰-纹章传说第二章_12131.jar | 343 | 火焰-纹章传说第二章 | nokia-ui | 18 | 16 | 600 | 961598 | java/io/IOException,java/lang/Nu | 运行正常 |  |
| 火焰纹章勇者之剑_10337.jar | 163 | 圣火纹章-勇者之剑 | - | 16 | 16 | 600 | 92731 | - | 运行正常 |  |
| 灵异村庄_11169.jar | 94 | The Village | nokia-ui | 19 | 16 | 600 | 183501 | java/io/IOException | 运行正常 |  |
| 炸弹人_10311.jar | 302 | Bomberman | nokia-ui | 12 | 16 | 600 | 23220942 | javax/microedition/rms/InvalidRe | 运行正常 |  |
| 热血沙滩9球赛_894.jar | 472 | 热血沙滩9球赛 | - | 27 | 16 | 600 | 89025 | - | 运行正常 |  |
| 爆笑三国斗地主_10552.jar | 312 | 爆笑三国斗地主 | nokia-ui | 8 | 16 | 600 | 63794 | - | 运行正常 |  |
| 狂野飙车4-gameloft(中文版)_2603.jar | 647 | 狂野飙车4 | - | 22 | 16 | 600 | 128449912 | - | 运行正常 |  |
| 狂野飙车4-亡命都市_2875.jar | 616 | 狂野飙车-亡命都市 | - | 13 | 16 | 600 | 130423755 | java/lang/NumberFormatException | 运行正常 |  |
| 狂野飙车4_6959.jar | 596 | 狂野飙车4-gameloft | - | 12 | 16 | 600 | 3496881 | - | 运行正常 |  |
| 猿人时代BT版_4519.jar | 489 | 猿人时代 | - | 49 | 16 | 600 | 49638 | - | 运行正常 |  |
| 生化危机-险境求生_3290.jar | 914 | 生化危机 | nokia-ui | 53 | 16 | 600 | 309287209 | - | 运行正常 |  |
| 生化危机3D(CAPCOM经典)_3671.jar | 983 | shwjd | m3g | 21 | 16 | 600 | 226187 | java/lang/NullPointerException | 运行正常 |  |
| 生化危机5核变_866.jar | 1181 | 生化危机5核变 | nokia-ui | 61 | 16 | 600 | 696460 | - | 运行正常 |  |
| 生化危机之恶化_8448.jar | 372 | RED http://www.universosymbi | - | 10 | 16 | 600 | 75803 | java/lang/IllegalArgumentExcepti | 运行正常 |  |
| 生化惊悚2死亡寂静中文版_2607.jar | 1144 | shjswjj | - | 26 | 16 | 600 | 261955550 | java/lang/ArrayIndexOutOfBoundsE | 运行正常 |  |
| 电子宠物蛋_11972.jar | 95 | Tamagotchi 240x320 | nokia-ui | 8 | 16 | 600 | 775378 | - | 运行正常 |  |
| 电玩美女麻将_7260.jar | 439 | 电玩美女麻将 | - | 9 | 16 | 600 | 6197304 | - | 运行正常 |  |
| 男摄影师_11003.jar | 250 | 男摄影师 | - | 19 | 16 | 600 | 4130384 | - | 运行正常 |  |
| 疯狂企鹅中文版_10228.jar | 140 | 功夫企鹅 | - | 13 | 16 | 600 | 249275 | java/io/EOFException,javax/micro | 运行正常 |  |
| 疯狂农场2_2375.jar | 551 | Farm Frenzy 2 | - | 42 | 16 | 600 | 2150426 | java/lang/NullPointerException,j | 运行正常 |  |
| 疯狂农场汉化版_5998.jar | 400 | Farm Frenzy | - | 33 | 16 | 600 | 1164574 | java/io/EOFException,java/io/IOE | 运行正常 |  |
| 疯狂出租车中文版_721.jar | 491 | 疯狂出租车 | - | 49 | 16 | 600 | 172564 | javax/microedition/rms/RecordSto | 运行正常 |  |
| 疯狂战机_2013.jar | 219 | Srcombat20110928 | - | 65 | 16 | 600 | 183755923 | java/io/IOException | 运行正常 |  |
| 百战天虫2010中文版_5380.jar | 560 | PC原版-百战天虫2010-EA | - | 25 | 16 | 600 | 6688050 | - | 运行正常 |  |
| 百鬼商店破解版_10526.jar | 382 | BAI_GUI_SHANG_DIAN | - | 4 | 16 | 600 | 1163463 | - | 运行正常 |  |
| 真三国无双横扫千军_12449.jar | 270 | 横扫千军.宝软网baoruan.com免费分享 | - | 14 | 16 | 600 | 826935 | - | 运行正常 |  |
| 真人快打3中文版_3252.jar | 299 | 真人快打3 | - | 50 | 16 | 600 | 5014 | - | 运行正常 |  |
| 真幻想三国志完整版_12469.jar | 877 | 真幻想三国志 | nokia-ui | 138 | 16 | 600 | 154729926 | - | 运行正常 |  |
| 真武三国BT版_3953.jar | 1013 | 真武三国 | - | 23 | 16 | 600 | 589612 | java/lang/NumberFormatException | 运行正常 |  |
| 真武三国_3962.jar | 1018 | 真武三国 | - | 22 | 16 | 600 | 589612 | java/lang/NumberFormatException | 运行正常 |  |
| 真西游记_6848.jar | 738 | 真西游记(华娱) | - | 18 | 16 | 600 | 994834 | - | 运行正常 |  |
| 神庙逃亡2（Temple_18.jar | 680 | Run2 | - | 4 | 16 | 600 | 50508 | - | 运行正常 |  |
| 科比职业篮球2008中文版_11136.jar | 520 | 科比大灌篮(gameloft正版) | - | 10 | 16 | 600 | 484369359 | java/lang/ArrayIndexOutOfBoundsE | 运行正常 |  |
| 空战1944_10321.jar | 309 | Air Strike 1944 | - | 6 | 16 | 600 | 59727677 | - | 运行正常 |  |
| 穿越火线-韩国正版_6874.jar | 480 | chuanyuehuoxian | - | 21 | 16 | 600 | 319357696 | - | 运行正常 |  |
| 竞技者之极限酷跑_5431.jar | 250 | 竞技者之极限酷跑 | - | 7 | 16 | 600 | 599006234 | java/io/EOFException | 运行正常 |  |
| 精品美女斗地主2013_104.jar | 1203 | 精品美女斗地主 | nokia-ui | 61 | 16 | 600 | 236357136 | java/io/IOException,java/lang/Nu | 运行正常 |  |
| 红色警戒3_7037.jar | 784 | C&C: Red Alert Mobile by bli | - | 33 | 16 | 600 | 4837888 | - | 运行正常 |  |
| 纯爱物语3之梦想之翼_5112.jar | 978 | L3D | - | 82 | 16 | 600 | 834220 | - | 运行正常 |  |
| 纯爱物语之湛蓝回忆简体版_5164.jar | 1007 | LoveMemory | - | 66 | 16 | 600 | 29667 | - | 运行正常 |  |
| 纽约人生_1339.jar | 879 | My Life in New York | - | 12 | 16 | 600 | 118392562 | - | 运行正常 |  |
| 细胞分裂4混沌法则中文版_11671.jar | 101 | 细胞分裂3-混沌法则 | nokia-ui | 4 | 16 | 600 | 284676121 | - | 运行正常 |  |
| 终极雷电_4125.jar | 293 | 终极雷电 | - | 57 | 16 | 600 | 917268 | - | 运行正常 |  |
| 经典推箱子_1070.jar | 247 | 纸盒人 | - | 34 | 16 | 600 | 129564 | java/io/IOException | 运行正常 |  |
| 绝密反击破解版_8377.jar | 174 | 绝密反击 | nokia-ui | 39 | 16 | 600 | 74147 | - | 运行正常 |  |
| 网球大满贯_11950.jar | 143 | 网球大满贯 | - | 11 | 16 | 600 | 244313943 | java/io/IOException,javax/microe | 运行正常 |  |
| 美女台球_9377.jar | 205 | Billard Girls | - | 40 | 16 | 600 | 293845 | java/lang/NullPointerException | 运行正常 |  |
| 美女斗地主(真人语音版)_1609.jar | 1089 | 写真美女斗地主(真人语音) | - | 29 | 16 | 600 | 61721 | - | 运行正常 |  |
| 美女斗地主_1088.jar | 717 | 美女斗地主 | nokia-ui | 14 | 16 | 600 | 5252365 | - | 运行正常 |  |
| 美女滑雪胜地_2.jar | 290 | 美女滑雪胜地 | - | 23 | 16 | 600 | 98374115 | - | 运行正常 |  |
| 美少女梦幻工厂_11443.jar | 181 | 美少女梦幻工厂 | nokia-ui | 4 | 16 | 600 | 45451 | - | 运行正常 |  |
| 美男狂想曲_3876.jar | 856 | Hotguys Rhapsody | - | 11 | 16 | 600 | 180828 | java/io/IOException,java/lang/Nu | 运行正常 |  |
| 聊斋之倩女幽魂_11185.jar | 300 | 聊斋之倩女幽魂 | nokia-ui | 6 | 16 | 600 | 179825178 | - | 运行正常 |  |
| 航海世纪2_10583.jar | 157 | Tradewinds 2 | - | 4 | 16 | 600 | 252042887 | java/lang/IllegalArgumentExcepti | 运行正常 |  |
| 色戒之虚拟人生_7230.jar | 584 | 色戒之虚拟人生 | - | 19 | 16 | 600 | 163294 | - | 运行正常 |  |
| 花样少年恋爱学院_5977.jar | 764 | love_school | - | 45 | 16 | 600 | 60687 | - | 运行正常 |  |
| 花样少年恋爱学院特别版_5527.jar | 776 | love-school-sp | - | 45 | 16 | 600 | 58968 | - | 运行正常 |  |
| 苍穹默示录完美运行版_4035.jar | 1790 | 苍穹默示录 | - | 53 | 16 | 600 | 226191783 | java/io/IOException,java/lang/Cl | 运行正常 |  |
| 荒岛余生(GLU正版)_12478.jar | 451 | 荒岛余生(GLU正版) | - | 9 | 16 | 600 | 255215 | - | 运行正常 |  |
| 荒岛余生2时空探秘_6794.jar | 486 | 荒岛余生2时空探秘 | - | 31 | 16 | 600 | 1466729 | - | 运行正常 |  |
| 荣誉勋章之空降神兵改良版_2993.jar | 559 | Medal Of Honor Airborne Elit | - | 32 | 16 | 600 | 10557011 | - | 运行正常 |  |
| 蓝牙真3D格斗_3424.jar | 602 | 真3D格斗 | m3g,nokia-ui | 26 | 16 | 600 | 1079179 | javax/microedition/rms/InvalidRe | 运行正常 |  |
| 蓝白五子棋_9790.jar | 104 | Gomoku by drAdeLante | - | 15 | 16 | 600 | 196018758 | javax/microedition/rms/RecordSto | 运行正常 |  |
| 蛋蛋历险记_11460.jar | 162 | Bird ETTY | - | 2 | 16 | 600 | 650788 | javax/microedition/rms/RecordSto | 运行正常 |  |
| 血战三国之吕布貂蝉_4012.jar | 347 | 血战三国-吕布VS貂蝉 | - | 10 | 16 | 600 | 30957 | javax/microedition/rms/InvalidRe | 运行正常 |  |
| 血战轩辕之魔神蚩尤_11172.jar | 198 | 血战轩辕之魔神蚩尤 | nokia-ui | 12 | 16 | 600 | 5289757 | java/lang/IllegalArgumentExcepti | 运行正常 |  |
| 街头反恐之格斗女王_822.jar | 285 | 街头反恐之格斗女王 | - | 10 | 16 | 600 | 32204 | - | 运行正常 |  |
| 街头霸王-街机经典_2664.jar | 563 | 街头霸王 | - | 12 | 16 | 600 | 12111 | - | 运行正常 |  |
| 街头霸王2_11388.jar | 493 | StreetFighter2 | - | 5 | 16 | 600 | 62542919 | - | 运行正常 | needs --device nokia6230 |
| 街头霸王2冠军典藏版_1812.jar | 730 | jtbw | - | 9 | 16 | 600 | 13644 | - | 运行正常 |  |
| 街霸2012(加强版)_2302.jar | 1117 | 街霸2012加强版 | - | 40 | 16 | 600 | 569867 | java/io/EOFException,java/lang/C | 运行正常 |  |
| 西伯利亚大反击_11466.jar | 205 | 西伯利亚大反击 | - | 2 | 16 | 600 | 174310943 | - | 运行正常 |  |
| 诈金花_6358.jar | 557 | 诈金花 | nokia-ui | 24 | 16 | 600 | 551461 | javax/microedition/rms/RecordSto | 运行正常 |  |
| 诛神－斗破苍穹_1402.jar | 1190 | 诛神-斗破苍穹 | - | 62 | 16 | 600 | 19243239 | - | 运行正常 |  |
| 诸神战纪-堕落天使路西法_5070.jar | 1324 | 诸神战纪-堕落天使路西法 | - | 28 | 16 | 600 | 316781 | - | 运行正常 |  |
| 贪吃蛇经典版_4515.jar | 124 | Snake | - | 68 | 16 | 600 | 8924 | - | 运行正常 |  |
| 赤色要塞－FC移植版_5285.jar | 282 | 赤色要塞 | nokia-ui | 43 | 16 | 600 | 1385141 | java/io/EOFException,java/lang/N | 运行正常 |  |
| 超时空帝国机甲风暴_12065.jar | 542 | 超时空帝国机甲风暴 | nokia-ui | 20 | 16 | 600 | 655617 | javax/microedition/rms/RecordSto | 运行正常 |  |
| 超级坦克大战2汉化版_531.jar | 655 | (发烧)超级坦克大战II(增强版) | nokia-ui | 24 | 16 | 600 | 268231077 | java/lang/NullPointerException,j | 运行正常 |  |
| 超级酒吧女生_4728.jar | 569 | 超级酒吧女生 | - | 10 | 16 | 600 | 180288875 | - | 运行正常 |  |
| 超级马里奥_10283.jar | 534 | 超级马里奥 | - | 11 | 16 | 600 | 661801744 | - | 运行正常 |  |
| 足球经理2010汉化版_4756.jar | 517 | CM-2010 | nokia-ui | 21 | 16 | 600 | 10639826 | - | 运行正常 |  |
| 跑跑卡丁车-经典中秋版_2117.jar | 623 | 跑跑卡丁车 | - | 21 | 16 | 677 | 574860 | - | 运行正常 |  |
| 蹦球传说_7801.jar | 375 | Bounce Tales | nokia-ui | 21 | 16 | 600 | 8777201 | - | 运行正常 | needs --device nokia6230 |
| 蹦球传说美化版_2959.jar | 416 | Bounce Tales | nokia-ui | 19 | 16 | 600 | 10704326 | javax/microedition/rms/RecordSto | 运行正常 |  |
| 轩辕神剑_2419.jar | 1324 | 轩辕神剑 | - | 71 | 16 | 600 | 539674 | java/io/EOFException,java/lang/N | 运行正常 |  |
| 迈阿密之夜_9566.jar | 315 | MiamiNights | - | 6 | 16 | 600 | 436851882 | - | 运行正常 |  |
| 远古帝国2汉化版_10357.jar | 168 | Ancient Empires II | - | 10 | 16 | 600 | 569583 | java/lang/NullPointerException,j | 运行正常 |  |
| 速度与激情5中文版_3043.jar | 380 | Fast Five The Movie Game | nokia-ui | 25 | 16 | 600 | 233038764 | - | 运行正常 |  |
| 速度与激情6_9.jar | 1917 | Fast & Furious 6 | - | 19 | 16 | 600 | 54804536 | java/lang/NullPointerException | 运行正常 |  |
| 都市摩天楼_6192.jar | 160 | Tower Bloxx(TM) | m3g,nokia-ui | 16 | 16 | 600 | 12259461 | - | 运行正常 |  |
| 都市摩天楼中文版_3219.jar | 1026 | 都市摩天楼 | nokia-ui | 46 | 16 | 600 | 22886194 | java/io/EOFException,javax/micro | 运行正常 |  |
| 都市赛车3-暴力街头3D版_10199.jar | 285 | Asphalt 3 3D Street Rules | - | 5 | 16 | 600 | 170042039 | - | 运行正常 |  |
| 都市赛车3_10192.jar | 286 | Asphalt3: Street Rules | - | 5 | 16 | 600 | 17276374 | - | 运行正常 |  |
| 都市赛车6火线追击_2230.jar | 940 | Asphalt 6 Adrenaline | - | 21 | 16 | 600 | 19711084 | - | 运行正常 |  |
| 都市赛车_11517.jar | 223 | Asphalt | - | 5 | 16 | 600 | 451225 | - | 运行正常 |  |
| 野人岛2宠物精灵_7061.jar | 534 | 宝软网baoruan.com免费分享 | nokia-ui | 24 | 16 | 600 | 2340393 | - | 运行正常 |  |
| 钻石狂潮_1335.jar | 293 | Diamond Rush | - | 12 | 16 | 600 | 2104688 | javax/microedition/rms/InvalidRe | 运行正常 |  |
| 铠甲勇士2-刑天_2895.jar | 1120 | 铠甲勇士2刑天 | - | 37 | 16 | 600 | 215977034 | - | 运行正常 |  |
| 闪电战机－雷霆出击_118.jar | 1388 | 闪电战机-雷霆出击 | - | 129 | 16 | 600 | 45559027 | java/io/IOException | 运行正常 |  |
| 阿尔卑斯牧场物语_7043.jar | 824 | 阿尔卑斯牧场物 | nokia-ui | 8 | 16 | 600 | 4002605 | java/lang/NullPointerException | 运行正常 |  |
| 雷欧历险记_11451.jar | 175 | FilaoFriedChicken | - | 11 | 16 | 600 | 12288499 | - | 运行正常 |  |
| 雷电2012末日浩劫增值版_3029.jar | 879 | 雷电2012末日浩劫 | - | 40 | 16 | 600 | 319417 | java/lang/NumberFormatException | 运行正常 |  |
| 雷电4_39.jar | 545 | 雷电4 | - | 26 | 16 | 600 | 12042910 | java/io/IOException | 运行正常 |  |
| 雷电战机(精装升级版)_3138.jar | 876 | 雷电战机(精装升级版) | - | 48 | 16 | 600 | 224139191 | java/io/IOException,java/lang/Cl | 运行正常 |  |
| 雷电战机精装升级BT版_3110.jar | 869 | 雷电战机(精装升级版) | - | 48 | 16 | 600 | 244099191 | java/io/IOException,java/lang/Cl | 运行正常 |  |
| 雷霆战机2012BT版_3101.jar | 851 | 雷霆战机2012 | - | 30 | 16 | 600 | 302745 | javax/microedition/rms/RecordSto | 运行正常 |  |
| 鬼吹灯(网络火热)_7647.jar | 1007 | qcd | nokia-ui | 21 | 16 | 600 | 270566049 | - | 运行正常 |  |
| 鬼泣3D中文版_3376.jar | 1118 | 鬼泣(CAPCOM正版) | mascot | 26 | 16 | 600 | 3345332 | java/io/EOFException | 运行正常 |  |
| 魂斗罗3代-完全版_3784.jar | 610 | 魂斗罗3代 | nokia-ui | 22 | 16 | 600 | 111936 | java/lang/NullPointerException | 运行正常 |  |
| 魂斗罗4中文版_2226.jar | 530 | 魂斗罗4 | nokia-ui | 56 | 16 | 600 | 90646 | - | 运行正常 |  |
| 魂斗罗V铁血兵团_119.jar | 990 | 魂斗罗V铁血兵团 | - | 68 | 16 | 600 | 524173 | java/lang/NumberFormatException | 运行正常 |  |
| 魂斗罗_5652.jar | 896 | 魂斗罗 | nokia-ui | 54 | 16 | 600 | 11772983782 | java/lang/NullPointerException | 运行正常 |  |
| 魔兽争霸_10622.jar | 59 | MSZB | nokia-ui | 22 | 16 | 600 | 161335098 | - | 运行正常 |  |
| 魔兽塔防_12480.jar | 243 | 魔兽塔防 | - | 18 | 16 | 600 | 120260 | java/lang/NullPointerException,j | 运行正常 |  |
| 魔塔_8052.jar | 86 | 魔塔 | nokia-ui | 21 | 16 | 600 | 69919 | - | 运行正常 |  |
| 魔塔－英雄联盟_582.jar | 1451 | 魔塔-英雄联盟 | nokia-ui | 63 | 16 | 600 | 7265092 | - | 运行正常 |  |
| 魔法少女_10523.jar | 204 | 魔法少女 | - | 5 | 16 | 600 | 37313 | - | 运行正常 |  |
| 鲁宾逊漂流记汉化BT版_4677.jar | 279 | - | - | 0 | 16 | 0 | 0 | - | 非可用包 |  |
| 鲁宾逊漂流记汉化版_702.jar | 367 | 鲁宾逊漂流记-海难生存 | - | 34 | 16 | 600 | 4385881 | javax/microedition/rms/RecordSto | 运行正常 |  |
| 麻将三国_5157.jar | 732 | MJSG | nokia-ui | 38 | 16 | 600 | 11032 | - | 运行正常 |  |
| 麻将无双之乱世群英_10298.jar | 173 | 麻将无双之乱世群英 | nokia-ui | 2 | 16 | 600 | 609881916 | javax/microedition/rms/RecordSto | 运行正常 |  |
| 麻将象棋五子棋3合1_12014.jar | 311 | 麻将象棋五子棋3合1 | - | 8 | 16 | 600 | 266045 | - | 运行正常 |  |
| 黑暗破坏神II_9009.jar | 84 | 黑暗破坏神II | - | 10 | 16 | 600 | 165084748 | - | 运行正常 |  |
| 龙之谷之天空战记_5383.jar | 885 | 龙之谷之天空战记 | - | 60 | 16 | 600 | 4312895 | - | 运行正常 |  |
| 龙火2加强版_12437.jar | 117 | 龙火2加强版 | - | 22 | 16 | 600 | 187457 | - | 运行正常 |  |
| 龙珠Z传说_9583.jar | 319 | 龙珠Z传说 | - | 8 | 16 | 600 | 213055 | - | 运行正常 |  |
| 龙鸟战机_9762.jar | 345 | E-Brid | - | 13 | 16 | 600 | 527805 | - | 运行正常 |  |
