# Android socket 收尾与回归记录（2026-10-17）

## 修改范围

- `src/port/android/java/com/lucinhu/nj2me/PlatformSocket.java`：保留 `Entry` 的 final socket/in/out；同连接读、写分别使用 `readLock`、`writeLock`；map 查询、注册和移除使用短锁。`close` 移除句柄后直接关闭 socket，不等待读写锁，可打断阻塞读。去掉对 final 字段赋 null 的编译错误。
- `connect` 失败时关闭创建的 socket；先完成流初始化及连接日志，再注册句柄，避免注册后失败遗留条目。解析 `socket://[::1]:port;parameters/`，拒绝无括号 IPv6 和缺失分隔符的地址；检查端口范围。
- 新增 `available(long id)`：返回当前可读字节数，失效句柄或 I/O 错误返回 -1，不等待 `readLock`。读、写增加缓冲区边界检查。
- `src/port/android/nj_port_android.c`：仅 socket 相关区域增加 available 的 JNI 方法缓存、`(J)I` 方法解析、调用、port hook 和销毁清理。JNI read 的预检、分配及异常错误返回 -2；真正 EOF 保留 -1。
- 正式回归放在 `tools/test/PlatformSocketTest.java`、`tools/test/check_android_socket.py`。Android Context/Log stubs、编译产物及日志只生成到 `.local-tools/debug-runs/`。未修改其他 Android IME/UI 文件或桌面端文件，未 commit；同一个 C 文件的 IME 改动由 native-ime 负责。

## 实际验证

### 桌面 JDK

在项目根目录执行，退出码 0：

```sh
python3 tools/test/check_android_socket.py
```

实际输出：

```text
PASS concurrent read/write and independent connections
PASS close interrupts blocked read and removes handle
PASS timeout, EOF, available and URL parameters 127.0.0.1
PASS timeout, EOF, available and URL parameters ::1
PASS failed connect cleanup (fd 6 -> 6) and invalid URLs
PlatformSocket regression: PASS
```

证据：`.local-tools/debug-runs/android-socket-dnf6rxxf/run.log`。

### Android 真机

`adb devices -l` 显示设备 `425610ca`，型号 `23049RAD8C`，产品 `marble`。SDK 位于 `/home/lucinhu/Android/Sdk`，有 android-35/36 平台及 d8。执行，退出码 0：

```sh
python3 tools/test/check_android_socket.py --device 425610ca
```

脚本用真实 Android SDK 编译，d8 转 dex，通过独立 `app_process` 运行同一组测试，未安装或替换应用。实际输出：

```text
PASS concurrent read/write and independent connections
PASS close interrupts blocked read and removes handle
PASS timeout, EOF, available and URL parameters 127.0.0.1
PASS timeout, EOF, available and URL parameters ::1
PASS failed connect cleanup (fd 59 -> 59) and invalid URLs
PlatformSocket regression: PASS
```

证据：`.local-tools/debug-runs/android-socket-dw8irt3a/run.log`。测试覆盖阻塞读期间写入、另一连接读入、close 打断 30 秒读、100 ms 读超时、EOF、available 队列字节数、IPv4/IPv6 带参数 URL、非法 URL 与缓冲区边界，以及连续 128 次连接拒绝后的 fd/map 清理。

测试服务在测试进程内创建并关闭。脚本 finally 删除唯一设备文件 `/data/local/tmp/nj-socket-android-socket-dw8irt3a.jar`，随后通过以下命令确认不存在，退出码 0：

```sh
adb -s 425610ca shell test ! -e /data/local/tmp/nj-socket-android-socket-dw8irt3a.jar
```

### NDK C/JNI 语法检查

执行，退出码 0：

```sh
/home/lucinhu/Android/Sdk/ndk/28.2.13676358/toolchains/llvm/prebuilt/linux-x86_64/bin/aarch64-linux-android21-clang \
  -std=c99 -Wall -fsyntax-only \
  -Iinclude -Isrc/core -Isrc/midp -Isrc/gfx -Isrc/zip \
  src/port/android/nj_port_android.c
```

唯一警告为已有 `nj_vm` typedef 重复声明（`include/nj2me/nj_device.h:50` 与 `include/nj2me/nj_types.h:95`），没有 socket 编译错误。`git diff --check` 通过。

## 验证限制

IPv6 和带 `;parameters` 的 URL 用例直接调用 `PlatformSocket.connect`，仅证明 Java backend 的解析和连接行为。Lead 后续审查确认 `src/midp/nj_lib_socket.c:140` 的公共 `socket_split` 仍用首个冒号分割，且要求端口数字后的字符为 NUL，因此 VM 的 `Connector.open` 在调用 backend 前仍会拒绝这些 URL。不能据此称 VM 端到端 IPv6/参数 URL 已支持；本轮未扩大范围修改公共解析器。

本次连接均为测试进程自身创建的 loopback TCP 服务，不代表真实游戏服务器、公网、DNS 故障或连接建立超时已验证。100 ms 超时用例验证的是读超时；失败资源清理用例验证连接拒绝，未故障注入流初始化失败。真机使用真实 Android `java.net`，但独立 app_process 未经过完整 J2ME→C→JNI 调用链；NDK 本次检查也仅为 C 语法检查，完整 APK 构建与应用内验证交由 native-ime 的集成构建继续完成。
