# 可选二维图形加速接口

本文说明二维图形加速接口的设计契约、适配边界，以及未来 M3G / Mascot GPU 后端需要补充的能力。核心继续使用纯 C；平台可以在适配层使用 OpenGL、OpenGL ES、Vulkan、DMA 或其他实现。完整软件路径始终保留。项目另提供可选的 [GLES2 移植层实现](GLES2_BACKEND.md)，由平台管理独立 context；Vulkan 尚未实现，也没有性能收益结论。

公开接口见 [nj_gfx.h](../include/nj2me/nj_gfx.h)，平台字段见 [nj_port.h](../include/nj2me/nj_port.h)，默认宏见 [nj_config.h](../include/nj2me/nj_config.h)。

## 启用与回退

新增编译宏 `NJ_ENABLE_GFX_ACCEL`，默认值为 `0`，只允许 `0` 或 `1`。宏为 `0` 时，不编译新后端的绘制调度分支，`nj_gfx` 不包含 `accel` / `accel_ud` 字段，`nj_port` 不包含 `gfx_accel` / `gfx_accel_ud` 字段，`nj_gfx_set_accel` 是空操作。这里的“无开销”指新后端的调度和实例状态，不包括已有的 `gfx_fill_rect` 回调。

CMake 构建可以直接开启该选项：

```sh
cmake -S . -B build-gfx -DNJ_ENABLE_GFX_ACCEL=1
cmake --build build-gfx
ctest --test-dir build-gfx -R '^test_gfx_accel' --output-on-failure
```

使用 `-DNJ_ENABLE_GFX_ACCEL=0` 可构建默认软件配置；手工构建则对全部相关 C 翻译单元统一传入该宏。

由于宏改变 `nj_gfx` 和 `nj_port` 的结构布局，核心库、平台后端和直接使用它们的调用者必须使用相同配置。不能仅给适配层增加宏、继续链接用另一配置构建的核心库。访问 port 的新增字段也必须放入 `#if NJ_ENABLE_GFX_ACCEL`。

启用宏后，平台可以通过 `nj_port` 末尾的 `const nj_gfx_accel *gfx_accel` 与 `void *gfx_accel_ud` 提供后端，独立的 C 图形调用者可以通过 `nj_gfx_set_accel` 为图形上下文安装后端。函数表版本为 `NJ_GFX_ACCEL_VERSION`，当前值为 `1u`。后端为空、版本不匹配、相应操作未实现或拒绝操作时，继续执行原软件路径。`gfx_accel_ud` 独立于 `port.ud`，允许有意设为 NULL，不会自动替换为 `port.ud`。

安装函数表不转移所有权，也不复制函数表。平台应让函数表和私有状态在全部相关绘制调用期间有效；`nj_gfx_init` 会清除该上下文的后端，独立 C 调用者需要在初始化之后安装。

MIDP 通过内部 `nj_midp_gfx_init` 为 LCDUI、GameCanvas / Sprite 等绘制上下文安装 port 后端，同时覆盖屏幕和可变 Image 目标。媒体 direct-video 的同尺寸图像复制也经过 blit；缩放分支仍逐像素执行软件路径。启动器等独立初始化的 `nj_gfx` 不会从某个全局 port 自动取得后端。局部直接读写像素、`nj_surface_set` 以及未经过 fill / blit 入口的光栅路径仍由 CPU 完成。

不透明矩形填充的优先级为：

1. 新加速后端。
2. 已有 `gfx_fill_rect` 回调。
3. CPU 软件绘制。

新增接口不取代 `NJ_ENABLE_M3G`、`NJ_ENABLE_MASCOT`。后两个宏控制 Java API 包是否存在，不承担选择 GPU 或软件实现的职责。

## 同步执行契约

一次回调必须具有以下可观察行为：

- 返回 `NJ_GFX_ACCEL_DONE`（`1`）表示本次操作已经完整执行，目标 CPU 像素缓冲立即包含最终结果。后续软件绘制、像素读取、图像复制和显示提交不需要再等待后端。
- `NJ_GFX_ACCEL_DECLINE`（`0`）及其他返回值表示拒绝，所有 CPU 像素必须保持调用前的内容，不能留有待执行写入；调用方随后完整执行软件回退。不能先修改部分目标，再以“拒绝”要求软件重画。
- 后端只能借用本次调用的图形状态、surface、源图像、mask 和相关描述。返回后不得继续使用这些指针，也不得将它们留给异步工作。
- 回调不能重入 VM。平台内部使用其他线程时，必须在返回前完成同步，并保证这些线程没有借用到期后的访问。
- 回调不能额外修改只读源或 mask；目标写入仅限本次操作允许的范围，不能覆盖行尾 padding 或裁剪区域之外的像素。源目标别名时，还要遵守该入口已有的读取语义。

返回 `1` 不是“已进入 GPU 命令队列”，也不是“已更新独立 GPU 纹理”。只有 CPU 缓冲中的操作结果可立即观察时，才满足成功契约。

对于执行中途的设备错误，普通拒绝不能代替恢复：若目标已经被部分修改，后端必须先恢复，或者使用临时目标与提交步骤保证对外原子性。透明混合、XOR、加减色等操作尤其不能在部分执行后重复回退。

## 操作描述与能力范围

函数表的实际声明如下，`fill_rect` 和 `blit` 可以分别为 NULL：

```c
typedef struct nj_gfx_accel {
    nj_u4 version;
    int (*fill_rect)(void *ud, const nj_gfx *g,
                     int x, int y, int w, int h);
    int (*blit)(void *ud, const nj_gfx *g, const nj_gfx_blit *blit);
} nj_gfx_accel;

void nj_gfx_set_accel(nj_gfx *g, const struct nj_gfx_accel *backend, void *ud);
void nj_gfx_blit_software(const nj_gfx *g, const nj_gfx_blit *blit);
```

`fill_rect` 接收完整的 `const nj_gfx` 状态和已经裁剪到目标 surface 的 device 坐标矩形。`g->surf` 是目标；`g->color` 是未按目标格式打包的 `0xAARRGGBB`，需要同时考虑 `g->blend`、`g->use_xor`、`g->xor_color`。后端不能默认所有矩形都是不透明覆盖，也不应再次应用平移。旧 `nj_gfx_fill_fn` 则接收已经打包的颜色，两者不能直接混用。

`blit` 接收借用的 `nj_gfx_blit` 描述，目标同样来自 `g->surf`：

| 字段 | 单位和语义 |
| --- | --- |
| `pixels` | 源缓冲起点，尚未应用 `sx` / `sy` 偏移；只在回调期间有效。 |
| `stride` | `nj_s8`，每行字节数，当前调度入口使用正 stride。 |
| `format` | 源 `NJ_PIXFMT_*`；RGB565 每像素 2 字节，RGB888 为 R、G、B 三字节，ARGB8888 为本机 `nj_u4` 的 `0xAARRGGBB`，不能假设内存中总是 A、R、G、B 字节序。 |
| `sx`, `sy`, `width`, `height` | 变换前的源区域，以源像素为单位。源区域不会因为目标裁剪而改写成可见子区域。 |
| `transform` | `NJ_TRANS_*`，先对所选源区域变换，再定位到目的矩形。 |
| `dx`, `dy` | **用户坐标**；完整目的原点为 `(nj_s8)dx + g->translate_x` 和 `(nj_s8)dy + g->translate_y`。使用宽整数，避免溢出。 |
| `clip_x`, `clip_y`, `clip_w`, `clip_h` | 已考虑变换、图形裁剪及 mask 尺寸的最终非空 **设备坐标** 矩形。仅在这个矩形内执行写入。 |
| `mask`, `mask_width`, `mask_height` | 可选的 packed ARGB8888 mask；行宽以像素计，没有独立 byte stride，每行字节数为 `mask_width * 4`。mask 坐标相对于完整目的原点，不随源变换旋转，不随目标裁剪重设原点。alpha 为 0 时跳过，非 0 时通过，不是连续的覆盖率乘数。 |
| `rop` | 保留调用者请求的 `NJ_ROP_SET`、`NJ_ROP_PAINT` 或 `NJ_ROP_OPAQUE`，尤其不会提前抹掉 OPAQUE 与 PAINT 的区别。 |
| `xor_enabled`, `xor_color` | 此次 blit 的最终 XOR 状态，已合并入口参数及图形状态；应使用这些字段，不能只看 `g->use_xor`。 |
| `alpha` | 已限制到 `[0,255]` 的全局权重。它是描述的一部分，不表示 SET 也要执行 alpha 混合。 |

image / region 入口会按源图像尺寸校验或截取所请求区域；直接 C 调用的 raw drawRGB 接口没有缓冲容量参数，调用者仍须提供可读的源存储。后端接收的是已成立的调用描述，不能从中取得任意外部分配的完整容量。

后端应按软件语义处理 ROP：SET 覆盖源值，不能擅自换成 source-over；PAINT 使用源 alpha 和全局权重；OPAQUE 先忽略源 alpha，再保留全局权重、mask 及 XOR 规则。XOR 也不能作为普通 alpha blending 的别名。仅实现其中少数形式的后端可以在写入前拒绝其他形式。

能力不足时，拒绝是正常路径，例如后端仅支持某种像素格式、不支持 XOR 或 mask，或者复制操作涉及当前实现不能正确处理的别名。接口存在不要求后端实现每一种组合。

实现时尤其需要保持：

- 目标像素格式和字节布局，包括 RGB565、RGB888、ARGB8888 及其 stride。
- 源的格式、透明信息、global alpha 与目标混合规则。
- 裁剪后的源坐标、变换后的采样关系、mask 对齐及 ROP 语义。
- 重叠存储的读取语义。后端无法正确处理别名时应在写入前拒绝。LCDUI `copyArea` 会先建立临时快照；一般直接 C blit 不因此自动获得重叠快照或 `memmove` 保证，调用者仍须满足对应软件入口的前提。
- 软件实现中的整数取整、alpha 处理和目标格式转换。仅颜色看起来接近不足以满足严格像素一致性。

`nj_gfx_blit_software` 是软件 blit 参考入口。它复制图形状态，禁用新后端及旧 fill 回调，然后调用原软件实现；不会改写调用方的图形状态，但会绘制到同一个目标像素缓冲。

只能传入 blit 回调收到的原始 `g` / 描述，不能构造未经验证的描述，或修改其中的裁剪和目标后期待参考入口重新提供容量验证。测试两个输出时，应从相同的初始像素出发分别执行绘制；对同一目标连续执行后端和参考会重复应用混合。

后端如果已经调用软件参考完成操作，应返回 `NJ_GFX_ACCEL_DONE`，不能再请求调用方执行第二次软件回退。

## 平台接入流程

平台可以从少量操作开始实现，例如某个像素格式上的不透明填充和无变换复制，其余情况直接拒绝。

建议接入顺序：

1. 用 `NJ_ENABLE_GFX_ACCEL=1` 构建，并按版本 `1` 初始化函数表；不支持的回调保持空值。
2. 将函数表和实例私有状态安装到 port，或为独立的 C 图形上下文调用 `nj_gfx_set_accel`。
3. 在每个回调中先验证当前状态、格式、别名及平台资源是否受支持；拒绝判定必须先于任何目标写入。
4. 完整执行操作并使结果对 CPU 可见后返回 `1`。
5. 与软件参考比较结果，覆盖混合绘制和拒绝后的软件回退。

下面是完整的接线示例，使用 CPU 填充和软件 blit 参考来展示成功、拒绝和安装。它没有实现 GL / Vulkan，也没有加速收益；真实驱动可以逐步替换回调内部操作，但必须保持相同同步契约。

```c
#include "nj2me/nj_gfx.h"
#include "nj2me/nj_port.h"

/* Caller owns one instance for the lifetime of the attached port/context. */
typedef struct example_accel_state {
    unsigned fills;
    unsigned blits;
} example_accel_state;

#if NJ_ENABLE_GFX_ACCEL
static int example_fill(void *ud, const nj_gfx *g,
                        int x, int y, int w, int h)
{
    example_accel_state *state = (example_accel_state *)ud;
    int ix, iy;

    /* Decide support before the first target write. */
    if (!g || !g->surf || g->surf->format != NJ_PIXFMT_ARGB8888
        || g->use_xor || (g->color >> 24) != 255u
        || (g->blend != NJ_BLEND_NONE && g->blend != NJ_BLEND_SRC_OVER)) {
        return NJ_GFX_ACCEL_DECLINE;
    }
    /* x/y/w/h are already clipped DEVICE coordinates. nj_surface_set is
       CPU-only, so it does not recurse through this backend. */
    for (iy = 0; iy < h; ++iy) {
        for (ix = 0; ix < w; ++ix) {
            nj_surface_set(g->surf, x + ix, y + iy, g->color);
        }
    }
    if (state) ++state->fills;
    return NJ_GFX_ACCEL_DONE;
}

static int example_blit(void *ud, const nj_gfx *g,
                        const nj_gfx_blit *blit)
{
    example_accel_state *state = (example_accel_state *)ud;
    /* Keep the received state/descriptor unchanged. The helper disables
       callbacks and completes all CPU writes before returning. */
    nj_gfx_blit_software(g, blit);
    if (state) ++state->blits;
    return NJ_GFX_ACCEL_DONE;
}

static const nj_gfx_accel example_backend = {
    NJ_GFX_ACCEL_VERSION,
    example_fill,
    example_blit
};
#endif

/* Call after the platform has initialized its normal nj_port fields.
   `state` can be NULL, or point to a caller-owned, zero-initialized instance. */
void example_attach_port(nj_port *port, example_accel_state *state)
{
#if NJ_ENABLE_GFX_ACCEL
    if (port) {
        port->gfx_accel = &example_backend;
        port->gfx_accel_ud = state;
    }
#else
    (void)port;
    (void)state;
#endif
}

/* Independent C contexts need explicit attachment after nj_gfx_init. */
void example_init_gfx(nj_gfx *g, nj_surface *surface,
                      example_accel_state *state)
{
    nj_gfx_init(g, surface);
#if NJ_ENABLE_GFX_ACCEL
    nj_gfx_set_accel(g, &example_backend, state);
#else
    (void)state;
#endif
}
```

实际平台只实现 fill 时，可以将函数表的 `blit` 设为 NULL，全部 blit 自然走原软件路径。要卸载独立图形上下文的后端，调用 `nj_gfx_set_accel(g, NULL, NULL)`；这不销毁平台状态或 GPU 资源，也不卸载旧 fill 回调。不要在仍有绘制使用函数表或私有状态时释放它们。

## 适配验证建议

为后端增加支持范围时，应从相同初始缓冲比较后端和纯软件结果，并检查目标行尾 padding / clip 外哨兵字节。建议覆盖：

- RGB565、RGB888、ARGB8888 与非紧凑目标 stride，以及不保证自然对齐的源存储。
- 非零平移、部分和完全裁剪、源子区域以及全部八种 `NJ_TRANS_*`。
- SET / PAINT / OPAQUE、源 alpha 为 0 / 255 / 中间值、全局 alpha、XOR 和 mask；mask alpha 为 1 与 255 都应通过。
- NULL 后端、缺失的单项回调、版本不匹配、返回 0 或其他非 1 值后的回退；新后端拒绝后，旧 fill 只在原先支持的条件下执行。
- 一个目标上交替进行后端和软件操作，然后立刻读取 CPU 像素；同一源地址被软件原地修改后再次绘制。
- 共享屏幕以外的可变 Image 目标、多个 port / 图形上下文以及有意为 NULL 的后端 userdata。

具体 GL / Vulkan 后端还需要单独验证：失败不留下部分 CPU 目标更新，私有 GPU 命令不影响其他操作，context 恢复后重新同步当前 CPU 内容。[GLES2 后端](GLES2_BACKEND.md) 使用私有 framebuffer 和成功读回后的 CPU 提交来实现这一契约；其他后端仍需分别验证。

### 本次验证（2026-11-12）

- `NJ_ENABLE_GFX_ACCEL=0/1` 的完整 Release 构建成功，无需图形 SDK。两种 Debug 配置各运行 18 项相关 CTest，均通过，覆盖新增接口、LCDUI、GameCanvas、M3G、Mascot、图像解码、存档和生命周期。
- [test_gfx_accel.c](../tests/test_gfx_accel.c) 在两种宏值下各覆盖 3,738 个操作用例。启用时包含独立 CPU 实现的 18 次 fill、72 次 blit，其余复杂组合使用软件参考；两种配置均通过 ASan / UBSan 与 leak 检测。
- [test_gfx_accel_vm.c](../tests/test_gfx_accel_vm.c) 同时创建两个 VM，通过真实 Graphics / Image / Sprite 调用，检查接受与拒绝、旋转、逐像素 getRGB、状态隔离及销毁。
- 本文 C 示例以 C99、`-Wall -Wextra -Werror` 在两种宏值下编译通过；启用后的 rasterizer 通过 NDK r10e GCC 4.9、ARMv5TE / Android API 5 编译。修改的四个实现文件在两种配置下均无可写全局状态。

这些结果验证接口调度、同步 CPU 参考实现与软件兼容性。后续增加的真实 GLES2 后端及其 EGL 测试见 [GLES2 后端说明](GLES2_BACKEND.md)；设备性能仍需单独测量。

## OpenGL / Vulkan 适配

GL/Vulkan 类型和头文件只应出现在平台适配层。核心看到的是 C 函数表、图形描述及私有状态指针。

在本接口下，GPU 后端的一次操作通常需要完成以下工作：

1. 取得所需的源图像和原目标像素。混合或局部更新需要保留调用前目标内容。
2. 上传或同步到 GPU，执行命令，并保持操作顺序。
3. 等待 GPU 完成，将结果写回调用方提供的 CPU 像素缓冲；共享内存平台也必须完成必要的可见性与 cache 同步。
4. 返回成功。

仅调用 GL flush、提交 Vulkan command buffer，或者等待 GPU fence 但没有让 CPU 缓冲可见，都不满足此契约。具体 API 的等待和内存屏障应由平台实现负责，不能假设所有驱动提供相同的隐式同步。

源目标属于同一存储时，还需要避免 GPU feedback 限制与源覆盖问题。不能直接采样正在写入的同一纹理来替代软件复制语义。

此设计允许真实的 GPU 绘制，但同步、上传和读回成本可能超过填充或复制本身的 CPU 成本。它更容易直接服务 DMA 或 CPU/GPU 共享内存平台。本文不提供性能收益承诺；尚无本接口的性能实测结论。

## 纹理缓存与资源生命周期

`nj_surface` 是线性缓冲的借用视图，不是 GPU 资源对象，也没有提供稳定资源身份或内容版本。Java Image 的 surface 还可能是临时结构，像素直接指向 Java 堆内的 `int[]`。

因此不能只用 `surface` 地址或 `pixels` 地址断言 GPU 缓存仍然有效：

- 两次调用之间，软件路径可能直接修改同一像素缓冲。
- 地址可能在对象释放后重新用于其他对象。
- 源图像、mask 或临时复制缓冲的有效期可能仅覆盖本次调用。
- VM 恢复状态、目标尺寸或格式变化、GL context 丢失、Vulkan 设备重建后，旧缓存不再具有原有含义。

第一版正确实现可以每次重新同步输入，避免跨调用缓存内容；如果缓存 GPU 存储以减少分配，也必须重新确认内容。需要可靠内容缓存时，应另行设计资源 ID、内容版本、写入通知及失效机制，不能从指针相同推导内容相同。

后端状态应属于平台或调用者持有的实例，并通过 `gfx_accel_ud` 传递。多个 VM 不应依赖隐藏的可变全局状态；后端状态不能包含需要写入 Java 存档的 GL/Vulkan 句柄。

平台负责初始化和销毁自己拥有的 GPU 资源。本版函数表只包含版本与绘制回调，没有创建、销毁、重置、资源失效通知或 fence 回调；这些生命周期动作由拥有 port / 后端状态的平台管理。没有写入通知时，不能建立依赖所有 CPU 写入都被后端观察到的缓存。

本次回调是同步的，返回后不应有继续引用 VM 缓冲的工作。销毁 VM、释放缓冲或释放后端状态之前，必须保证没有正在执行的回调。共享同一后端状态给多个实例时，线程同步和实例隔离由平台负责。

context 丢失或设备不可用时，后端在任何写入之前拒绝即可保留软件绘制。恢复后重新创建资源，丢弃旧缓存；从当前 CPU 像素重新建立内容，不能沿用丢失前 GPU 内容的假设。

## M3G / Mascot 与二维接口的边界

本接口加速二维填充与 blit。M3G / Mascot 的主要光栅路径直接操作像素，不能因为二维接口存在就认为三维渲染已经交给 GPU。

现另有独立的 [三维片段接口与 GLES2 实现](GLES2_3D.md)：复用原 CPU 覆盖和深度 key，GPU 执行不透明常量颜色/深度合并，再原子同步回 CPU。SDL2/Android 使用同一专用 context 包装层。二维 v1 函数表保持不变，复杂纹理、混合等图元保留完整软件路径。下文的批量几何/纹理流水线仍是未来扩展建议。

当前相关代码位置：

- [nj_lib_m3g.c](../src/midp/nj_lib_m3g.c)：`jr_setup`、`jr_draw`、`jr_emit`、`sw_tri` 是实际 M3G 软件流水线。旧 `nj_m3g_emit_fn` 只携带 XY 和颜色，不能表达深度、UV、透视校正、采样、混合与雾。
- [nj_lib_m3g.c](../src/midp/nj_lib_m3g.c)：`m3g_clear` 创建局部 `nj_gfx`，`m3g_background_image` 直接写像素。二维后端的接入范围应以实际调用链为准。
- [nj_mascot_scene.inc](../src/midp/nj_mascot_scene.inc)：`mascot_scene_flush` 将队列排序后依次绘制点、线和三角形，最后清理队列及纹理引用。
- [nj_mascot_raster.h](../src/midp/nj_mascot_raster.h) 与 [nj_mascot_raster.c](../src/midp/nj_mascot_raster.c)：纯 C Mascot 顶点、材质、纹理和软件光栅实现。

未来适配建议：

### M3G：规范化的 submesh 批次

保留 CPU 场景遍历、变换、光照和裁剪，从 `jr_emit` 收集规范化光栅输入，以 submesh / `jr_draw` 为批量范围，随后交给后端执行。批次应包括完整的顶点、纹理、混合、深度、裁剪、雾和采样语义，并覆盖 Sprite3D 和 clear / background 行为。

应新建清晰的 C 数据契约，不直接公开 `m3g_jr`、`m3g_sw` 或 Java `nj_ref`。这些私有结构混合了解析缓存、借用指针、软件实现细节和 VM 状态，不适合成为长期平台 ABI。

M3G 软件深度保存在 Graphics3D 的 Java `int[]` 中。深度 key 为反向 24 位形式：

```text
key = 1 + round((1 - windowZ) * 16777215)
```

`0` 表示清除，较大的 key 更近，相等值通过。深度有效期属于一次 target binding，期间可以跨显示 present 保留；不能按显示帧擅自重置。

GPU 与软件混用时，需要同时迁移颜色和深度。只读回颜色再回退，会使随后软件三角形按过时深度测试。若不提供深度迁移，必须使用具有完整重放能力的更大回退单元；仅在第一次 draw 时选择后端无法预知该 binding 后续是否出现不支持的状态。

### Mascot：排序后的整个 flush

已有排序队列是更直接的批量边界。提交整批点、线和三角形，携带材质、纹理 blob、环境图和 toon 状态，保持排序后的执行顺序。只能合并相邻且兼容的操作，不能为了减少材质切换重新排序透明绘制。

Mascot 没有深度缓冲，回退状态较简单。flush 返回时就会清理纹理引用，异步后端若要越过返回点使用数据，必须另行复制或持有资源；当前同步借用契约不允许这样做。

### 三维逐像素兼容

CPU/GPU 共享了最新像素，只能证明内存一致性，不能证明不同光栅器的输出逐像素相同。

Mascot 软件路径复刻固定点 DDA、取整、palette 采样和 blend 公式；RGB565 目标还包含每次绘制的通道展开与截断。M3G 也有明确的深度量化、采样、LOD 和混合规则。GL/Vulkan 固定功能三角形的边缘覆盖、浮点插值、滤波与硬件混合不能自动保证与这些规则相同。

严格兼容模式应保留软件回退，或由 GPU compute 等实现显式复刻光栅算法。允许视觉误差的模式需要单独声明和验证，不能由“使用了 GPU”推导为与软件等价。

## 2026-11-13/14 实现状态与实测

本文描述的同步借用契约已经落地为两条独立可选路径，默认全部关闭：

1. 二维 `nj_gfx_raster`（fill/blit）：`nj_gfx_raster_fn`，返回 `DONE` 时
   目标颜色已由 CPU 可见。
2. 三维 `nj_gfx_triangles`（三角形批量事务）：`nj_gfx_triangles_fn`，一次
   回调提交一批三角形，返回 `DONE` 时 CPU 颜色、深度和 `*submitted` 已提交；
   其它返回不得写调用方缓冲。校验器 `src/gfx/nj_gfx_triangles.c`，契约见
   [nj_gfx_triangles.h](../include/nj2me/nj_gfx_triangles.h)。

实测（RTX 5080 Laptop）：48 个真实 Minecraft packet/6,432 三角形，软件
1.173842 ms，GPU ROI 7.694929 ms（6.55×），GPU 全表面 32.089298 ms；二维
GPU 在同一主机上比软件慢一个数量级（1×1 fill 11.03 µs vs 0.20 µs）。
真实 Minecraft 在 libretro 硬件 GL 下接受 1,189 个三角形 batch、226,374 次
原子拒绝（回退软件），画面正确。因此**默认软件渲染**，GPU 只作为可选能力，
不宣称加速；逐项数据、Android 的 `EXT_frag_depth` 门控和帧级批处理审计见
[GLES2_3D.md](GLES2_3D.md) 与 [PLATFORM_SUPPORT.md](PLATFORM_SUPPORT.md)。

## 端口默认策略：2D 回调默认不安装（2026-11-14）

2D fill/blit 后端是一个同步事务：每次调用都要导入 CPU 矩形、上传、绘制、
回读并提交。参考主机实测它比软件路径慢，且与尺寸无关（1×1 填充 11 µs vs
0.20 µs，整屏填充 201 µs vs 2.9 µs，整屏 RGB888→RGB565 blit 320 µs vs
67 µs；Maze 300 帧 1.52 s vs 软件 1.33 s）。因此：

- SDL2 端口默认**不**安装 `gfx_accel` 回调；需要实验时设环境变量
  `NJ_SDL2_GLES2_2D=1`（与历史 `NJ_SDL2_GFX` 同一风格）。
- Android 端口默认**不**安装；实验构建加 `-DNJ_ANDROID_GLES2_2D_ACCEL=1`。
- libretro 硬件驱动由前端控制，未改动。

后端的实现和测试（`test_gfx_gles2`、`test_gfx_raster_gles2`、
`test_sdl2_gles2`、`test_android_gles2`）仍完整保留：测试目标显式打开上述
开关，因此契约与像素一致性覆盖不变。三维三角形 list 路径不受影响。
详细拆解与"为什么 GPU 更慢"见 [GLES2_3D.md](GLES2_3D.md)。
