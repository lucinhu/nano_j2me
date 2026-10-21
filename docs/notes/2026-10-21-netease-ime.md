# 网易云音乐搜索页内联中文输入（2026-10-21）

`2026-10-17-download-corpus.md` 的 jar-03 只记到「网易云音乐主列表」。本轮把桌面真实
输入法推进到应用内表单：主列表 → 搜索页的 TextField 里用宿主机 fcitx5 直接输入中文，
不需要打开什么全屏编辑窗口。

## 流程与证据

- `build/nj2me`（默认构建，链 `/usr/lib/libSDL2-2.0.so.0` = sdl2-compat），
  `XMODIFIERS=@im=fcitx`，本地 loopback 503 代理（避免联网等待）；
  260x? 不，240x320、heap 16、fps 30。
- 方向键选中「搜索」→ 回车进入搜索页（标题「搜索」，首行蓝色聚焦的
  `输入搜索内容`，下一行是「搜索」命令）。
- 在搜索页**直接输入**：`Ctrl+Space` 激活 fcitx 拼音 → 逐字输入 `nihao` →
  空格提交，表单字段立即显示「你好」：
  [06-committed.png](/home/lucinhu/workspace/c/nano_j2me/.local-tools/debug-runs/corpus-round20-20261021/netease-ime2/06-committed.png)
  （SHA-256 `ae9265fe4a74a1b9…`；同目录 `04-ime-on.png` 是输入前的空搜索页
  `e3dccb1742270f91…`，`01-list.png` 是主列表 `a003b4e0ca25cc9a…`）。
- 触屏/回车在空字段上按会直接触发「搜索」命令并退回主列表——**先输入、后提交**才是
  正确顺序；之前的「编辑窗打不开」结论是把内联编辑当成了需要单独打开编辑器。
- 运行器：`.local-tools/debug-runs/corpus-round20-20261021/run_netease_ime2.py`
  （窗口截图 + `xdotool windowfocus/type`，manifest 记录二进制/JAR 哈希、每张截图哈希、
  代理请求；原版 JAR `app(2).jar` 前后 SHA-256 一致）。

## 机制

- `text_focus` 会返回当前 TextBox，或当前 Form 的聚焦 TextField（
  `src/midp/nj_lib_lcdui.c:8512`），所以 Form 内联字段本身就带 text session；
  SDL2 的 `SDL_TEXTINPUT` 经端口变成 `NJ_EV_TEXT_INPUT`，`nj_midp_text_event`
  校验 session 后走 `text_change`/`tf_sync` 直接改字段
  （`src/midp/nj_lib_lcdui.c:8604`、`:8695 nj_midp_text_edit`、`:8700-8726`）。
- 因此应用里「点进表单字段再打字」与游戏里「打开全屏编辑器」是两条路径；前者不需要
  额外按键，输入法提交即写入。
- 退格可以删掉内联字段里的文字（本轮先用 `BackSpace` 清掉误输入的「差别」再输入
  「你好」，`NJTEXT text=差别你好 n=4 ok=1` → `NJTEXT text=你好 n=2 ok=1`）。

## 边界

- 只验证了「输入框可输入并显示中文」；按回车提交搜索后回到主列表，未验证在线搜索结果
  （代理是固定 503，歌曲元数据/播放仍未覆盖）。
- 输入法仍是宿主机 fcitx5；Android 侧的中文输入另见
  [Android 中文输入法记录](2026-10-21-android-chinese-ime.md)。
