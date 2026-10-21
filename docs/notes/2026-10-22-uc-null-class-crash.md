# UC 浏览器输入崩溃：没有类的数组把解释器带进 SIGSEGV（2026-10-22）

## 结论

UC 浏览器（`app.jar`）在**地址编辑页里输入文本**时，模拟器会在 MIDlet 的 `paint()`
里**段错误退出（exit -11 / 139）**。根因不是帧时序、不是网络、也不是在途的解释器快路径：

> **运行时自己造出来的数组没有类**（`String.getBytes()` 交出的 `byte[]` 走
> `nj_new_array(vm, NULL, …)`），而 `invokevirtual` 的派发在 `recv->cls == NULL` 时
> 找不到方法，接着**用这个空指针去拼错误消息**——`recv->cls->name` 就是 SIGSEGV 的现场。

修复分三处，全是「把未定义行为变成定义好的答案」：

1. `src/core/nj_interp.c`：错误消息里的类型名改用新的 `nj_recv_type_name()`（有类用类名，
   没类就按元素类型拼 `[B`/`[I`/…），**两处** `recv->cls->name` 都换掉。
2. `src/core/nj_interp.c`：**没有类的接收者按 `java/lang/Object` 派发**。一个没有类的数组
   仍然是 Object，手机上 `getBytes().toString()` 的答案是 `"[B@1a2b3c"`，模拟器现在也是。
3. `src/core/nj_lib_object.c`：`obj_toString` 对没有类的数组用 `object_type_name()` 取名，
   于是打印出来也是 `"[B@…"`，和手机一致。

另外修掉了 UBSan 报出的**空串未定义行为**：`src/core/nj_lib_string.c` 里
`String.startsWith/endsWith/equals` 在参数是空串时会把 `memcmp(x, NULL, 0)` 交给 libc，
C 标准不允许把空指针交给 `memcmp`（哪怕长度是 0）。现在改成先判断再比较，**可观察行为不变**
（空前缀仍然是 true）。

## 环境与命令

| 项目 | 值 |
|---|---|
| 修复前二进制 | `.local-tools/head-tip-20261022/build/nj2me`，SHA-256 `4d2625ebf7dd6d66444d086baf7f3738c8909bd54e4b0cfd6e6c678f0c0f93d5`（tip 干净构建，`git archive HEAD` 展开） |
| 修复后二进制 | `build/nj2me`，SHA-256 `e8459c132b2ca9bb4f0230a902caf24f4f6147bdea942d3d2011cd39ba03e94f` |
| UC 原始 JAR | `/home/lucinhu/Downloads/jar/app.jar`，SHA-256 `38e7c3f7a9d06c8f6d04d4e6f83b33dda787fa7191445e619c4d9d11c3bcb3a2`（运行前后一致，未修改） |
| 复现台架 | `tests/arrcls/ArrClsDemo.java`（源码入库）+ `tools/make_arrcls_jar.sh`（**生成** `tests/fixtures/arrcls.jar`；`tests/fixtures/` 是 gitignore 的生成目录，所以 JAR 本身不入库，SHA 每次构建都会变）+ `tools/check_arrcls.sh`（ctest `check_arrcls`） |
| UC 回放台架 | `.local-tools/debug-runs/uc-regress-20261022/uc_harness.py`（`--mode assignment|503|real`，`--out` 必须绝对路径；`--gdb` 会用 `gdb -batch -ex run -ex 'bt 40'` 跑） |

## 复现步骤

### 1. 确定性复现（推荐，最快）

```bash
cd /home/lucinhu/workspace/c/nano_j2me
# 台架 JAR 是生成的（tests/fixtures/ 入 gitignore），先构建
bash tools/make_arrcls_jar.sh
# 修复前：段错误，退出码 139
.local-tools/head-tip-20261022/build/nj2me --port headless --heap 8 \
  --size 176x208 --run-frames 5 tests/fixtures/arrcls.jar; echo "exit=$?"
# 修复后：正常退出，输出 ARRCLS 各行
build/nj2me --port headless --heap 8 --size 176x208 --run-frames 5 \
  tests/fixtures/arrcls.jar; echo "exit=$?"
# 回归检查脚本（修复前 FAIL，修复后 PASS；JAR 缺失时会自动构建）
NJ_BIN=$PWD/build/nj2me bash tools/check_arrcls.sh
```

台架 MIDlet 的核心只有两行（`tests/arrcls/ArrClsDemo.java`）：

```java
byte[] raw = "abc".getBytes();   // 运行时造的数组：没有类
String s = raw.toString();        // 以前在这里 SIGSEGV
```

### 2. 在真实标题上复现（UC）

```bash
cd /home/lucinhu/workspace/c/nano_j2me
G=.local-tools/debug-runs/uc-regress-20261022
python3 $G/uc_harness.py --jar "/home/lucinhu/Downloads/jar/app.jar" \
  --out "$PWD/.local-tools/debug-runs/uc-fixregress-20261022/a6-1" \
  --binary "$PWD/build/nj2me" --mode assignment --plan "$G/plan-a6.json" \
  --frames 2400 --size 240x320
# --gdb 追加一次可拿到 bt 40
```

`plan-a6.json` = 等 6 s → `F1` → `Down`×3 → `Up`×3 → 截图 03 → `Return` → 截图 04（URL 编辑页）
→ 聚焦输入框 → 输入 `http://example.com` → 截图 05..08。

## 证据

| 项目 | 修复前 | 修复后 |
|---|---|---|
| `tests/fixtures/arrcls.jar` 直跑退出码 | **139（SIGSEGV）**，3/3 次复现 | **0**，3/3 次 |
| `tools/check_arrcls.sh` | **FAIL**（3/3） | **PASS**（3/3） |
| UC `plan-a6`（2400 帧） | 偶发崩溃：早先一轮 ~50%，最近一批 0/6 | **4/4 全部 exit 0** |
| `ctest --test-dir build -j4` | 90/90 | **91/91**（新增 `check_arrcls`） |
| 孤岛余生2（jar-01）冷启动 | 142 色 / 堆 2.59 MB / uncaught 0 | 同（无回归） |
| 都市赛车6（jar-29） | 54 色 | 同（无回归） |
| 网易云（jar-03） | 13 色 | 同（无回归） |

修复后 `arrcls` 台架的输出（节选）：

```
ARRCLS heap.threw=none
ARRCLS heap.text=[B@75df8ca
ARRCLS native.threw=none
ARRCLS native.text=[B@75e074e
ARRCLS sw.empty=true … ARRCLS eq.miss=false
ARRCLS sw.nonstring=threw java.lang.ClassCastException
```

即：**有类的数组（`new byte[3]`）与没有类的数组（`getBytes()`）现在给出同样的答案**
（`"[B@…"`），和手机一致；参数不是 String 时是 `ClassCastException`，不是崩溃。

## 异常原文

崩溃现场（gdb，tip 干净构建的 Debug 版）：

```
#0  nj_interp_run () at src/core/nj_interp.c:1944   const char *q = recv->cls->name;
#1  nj_exec () at src/core/nj_engine.c:102
#2  vm_invoke () at src/core/nj_vm.c:1343
#3  nj_call () at src/core/nj_vm.c:1509
#4  canvas_call_abstract () at src/midp/nj_lib_lcdui.c:4652
#5  canvas_call_paint () at src/midp/nj_lib_lcdui.c:4679
#6  canvas_call_paint_rooted () at src/midp/nj_lib_lcdui.c:4722
#7  nj_midp_paint () at src/midp/nj_lib_lcdui.c:5096
#8  nj_midp_frame () at src/midp/nj_lib_midlet.c:354
#9  nj_run () at src/runner/nj_run.c:1174
#10 main () at apps/nj2me_main.c:386
```

`target->name = "toString"`，`target->desc = "()Ljava/lang/String;"`——调用方是
`byte[].toString()`，接收者没有类。

UBSan（ASan+UBSan 构建 `b9f1e639a96a2880b982dbaf9304b5a70c9e14253f289c60bb68347e1d24eb2b`
跑 2400 帧不崩但报错）：

```
src/core/nj_lib_string.c:941:15: runtime error: null pointer passed as argument 2,
  which is declared to never be null
  #0 str_startsWith1 src/core/nj_lib_string.c:941
  #1 nj_invoke_native src/core/nj_interp.c:173
  #2 interp_invoke src/core/nj_interp.c:223
  #3 nj_exec src/core/nj_interp.c:1744
  #4 vm_invoke src/core/nj_vm.c:1343
  #5 nj_call src/core/nj_vm.c:1509
  #6 canvas_call_abstract src/midp/nj_lib_lcdui.c:4652
  #7 canvas_call src/midp/nj_lib_lcdui.c:4670
  #8 nj_midp_key src/midp/nj_lib_lcdui.c:4866
```

同一次运行里 UC 自己抛的异常（**应用侧，不是模拟器缺陷**）：

```
java/lang/NoSuchMethodError at aa.a(ILjava/lang/String;)I pc=12: [I does not implement java/lang/String.length   （×118~127）
java/lang/VerifyError at aa.b(Ljava/lang/String;Ljava/lang/String;)Z pc=28: receiver is not an object reference
javax/microedition/rms/RecordStoreNotFoundException … openRecordStore(Ljava/lang/String;ZIZ) …
java/lang/NullPointerException at javax/microedition/lcdui/Displayable.addCommand(Ljavax/microedition/lcdui/Command;)V pc=0: command is null
java/lang/NullPointerException at bi.o()V pc=29 / at bi.run()V pc=1635
java/io/EOFException at DataInputStream.readUTF() / readByte()
java/lang/ClassCastException at bj.d([B)I
java/lang/IllegalArgumentException … unknown key code
```

## 边界与未验证项

- **UC 的地址栏输入仍未达成**：地址栏是 UC **自绘**的控件，模拟器的 IME 桥只对 MIDP
  `TextBox`/`TextField` 生效（`src/midp/nj_lib_lcdui.c:8511-8566`）。这次修的是「输入时崩溃」，
  不是「自绘控件收不到文字」——后者是应用侧结构问题。
- 崩溃本身**依赖堆布局**：同一台架在修复前有时 0/6 有时 ~50% 复现，因此**不能用「跑一次没崩」
  当作修复证据**；本表用可确定复现的 `arrcls` 台架 + 退出码判定。
- 未验证：真机（arm64）上同一路径、UC 真实线上版本的输入、其他把原生数组当对象用的标题。
- 本轮未改动 `src/port/**`、`include/**`、`tests/embedded/**`、其它会话的 libretro/JIT 文件；
  原始 JAR 与用户 RMS 均未触碰。
