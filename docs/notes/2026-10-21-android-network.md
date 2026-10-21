# Android 受控网络测试：侠义 OL 的 HTTP 启动请求（2026-10-21）

目标要求「对网游检查 HTTP/socket 等协议和服务器可用性，以本地受控网络测试验证实现」。
桌面侧已经用 loopback 503/受控分配响应验证过 UC 与侠义（见 `2026-10-20-uc-browser.md`、
`2026-10-17-xy-ime.md`），本轮把同样的受控测试搬到 Android 平台，确认 MIDP
`HttpConnection` → Android 端口层的 HTTP 通告真的发出、并且响应被游戏逻辑消费。

## 方法

- 宿主机受控服务器：`/home/lucinhu/workspace/c/nano_j2me/.local-tools/debug-runs/android-net-20261021/proxy.py`
  （`proxy503.py` 是固定 503 的早期版本）监听 `127.0.0.1:8000`，把每个请求的方法、
  绝对 URI、Host、头、body 长度与 SHA-256 记进 JSONL，然后按环境变量返回固定
  status/body。宿主 80 端口不可用（无 sudo，`net.ipv4.ip_unprivileged_port_start=1024`），
  但侠义本来就用 `:8000`，不需要端口重定向。
- 模拟器（`OpenHV_API26_googleapis`，x86_64）：
  `adb shell settings put global http_proxy 10.0.2.2:8000`（10.0.2.2 是宿主机 loopback），
  直接以 `-n com.lucinhu.nj2me/.GameActivity -e jar xiayi.jar -e jarpath … -e title xyOnline
  --ei w 240 --ei h 320 --ei heap 16` 启动原版侠义 JAR
  （`bfa87c2046aa0d2b3847521ffdd34baf27a3de252b07031830d3b966eb2e66d2`，未修改）。
  - **2026-10-22 更正**：上面这条 `am start … GameActivity` 在 API 26 上**跑不通**——
    `GameActivity` 在 `src/port/android/manifest/AndroidManifest.xml` 里是
    `android:exported="false"`，这台 shell 没有 `START_ANY_ACTIVITY`，会得到
    `java.lang.SecurityException: Permission Denial: starting Intent { flg=0x10000000 cmp=com.lucinhu.nj2me/.GameActivity (has extras) } from null (pid=4508, uid=2000) not exported from uid 10078`。
    正确做法是启动 `com.lucinhu.nj2me/.LibraryActivity` 再点条目；本轮按此路径复跑，
    5 个 POST 与 2 个 POST 的结果与上文一致。见
    [Android 与桌面原生中文输入 / 受控网络回归](2026-10-22-android-desktop-ime-regression.md)。
- 系统连通性检查（gstatic/google/play 的 204）也会经由该全局代理，那是 Android 自己的
  请求，与游戏无关。

## 结果

- 503：`requests.jsonl`（SHA-256 `a924be55d9a1ed2b96d9addc…`）记录到侠义启动的 5 个
  POST，每个 body 48 字节：
  `http://xy.liujin.cn:8000/center/GameComm`、
  `http://xy.ljsy.net:8000/center/GameComm` ×2、
  `http://117.135.140.227:8000/center/GameComm` ×2。
  游戏画面随后显示「请检查设备网络连接是否已经开启！」+「返回」
  （[01-http503-network-error.png](/home/lucinhu/workspace/c/nano_j2me/.local-tools/debug-runs/android-net-20261021/01-http503-network-error.png)，SHA-256 `7481a62dcdf2a493a0205a76…`）。
- 200（空 body）：`requests-200.jsonl`（SHA-256 `06b5f3204d7cdd92c5089d5f…`）只有 2 个
  POST，且都发往 `xy.liujin.cn:8000`；游戏停在标题画面、没有弹网络错误
  （[02-http200-empty-title.png](/home/lucinhu/workspace/c/nano_j2me/.local-tools/debug-runs/android-net-20261021/02-http200-empty-title.png)，SHA-256 `f20bd460cf8bdae230aa3ef5…`）。
  两次请求的重试次数与 UI 反应不同，说明 503 与 200 都被游戏逻辑消费，而不是只在
  socket 层失败。

## 边界

- 这是「Android 平台 HTTP 通告可用 + 受控失败/成功可区分」的证据，不是「服务器还活着」
  或「能登录」的证据：xy.liujin.cn 等历史服务器不可依赖，200 空 body 也不是有效协议
  负载，没有跑通登录、选区或游戏内数据。
- 只覆盖 HTTP；socket/HTTPS 与 UC 的响应语法仍未在 Android 上验证。
- 模拟器无头跑法有 SystemUI 崩溃坑，规避方式见
  [Android 中文输入法记录](2026-10-21-android-chinese-ime.md)。
