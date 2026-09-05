# 8086-CPU-Sim 前端 API 参考

版本：0.1.0（对应 `sim_fe_abi_version() == 1`）
适用源码：`include/sim/sim_frontend.h`、`include/sim/sim_cga_render.h`
说明：本文描述的是 Phase 0 已实现、且经编译与运行时冒烟测试验证的接口。每个函数/类型的签名与行为均以头文件与实现为准。

---

## 1. 概述

前端控制器（frontend controller）是图形界面与模拟器引擎之间的唯一交互边界。GUI（Qt、SDL、终端、测试驱动、语言绑定）通过本 API 控制整机并读取快照，不直接访问 `SimSystem` 内部。

本 API 提供五类能力：

- 生命周期：创建、销毁、复位。
- 运行控制：批量推进（`sim_fe_run_slice`）、单步（`sim_fe_step`）、倍速（`sim_fe_set_speed`）。
- 显示：CGA 帧缓冲（`sim_fe_frame`）。
- 输入：注入键盘扫描码（`sim_fe_inject_scancode`）。
- 观察与可视化：CPU 快照（`sim_fe_cpu_snapshot`）、追踪（`sim_fe_trace_*`）、内存读取（`sim_fe_memory_read`）、结构化状态（`sim_fe_status`）。
- 介质：BIOS、软盘、固定磁盘的挂载/重载/弹出/创建/刷新。

渲染相关接口位于 `sim_cga_render.h`，被 `sim_fe_frame` 复用；前端一般不需要直接调用它，除非要绕过控制器单独渲染一帧。

---

## 2. 构建与包含

- 头文件：`#include "sim/sim_frontend.h"`，`#include "sim/sim_cga_render.h"`。
- 引擎与控制器实现位于 `src/*.c`，与现有工程一致，用 `-Iinclude` 编译。
- 依赖：本 API 依赖引擎的 `SimSystem`、`SimTrace`、`Cpu8086State` 等类型，这些类型由头文件自动包含。
- 前端使用 `sim_fe_abi_version()` 在运行时校验接口版本。

---

## 3. 线程、所有权与指针生命周期

- **线程**：引擎是单线程的。本 API 的所有函数都应在同一个线程内调用。
- **配置字符串**：`sim_fe_create` 会复制 `SimFeConfig` 中的路径字符串（`bios_path`、`floppy_path`、`hdd_path`）。调用方传入的配置字符串在 `sim_fe_create` 返回后即可失效。内部副本由 `sim_fe_destroy`/`sim_fe_reset` 维护。
- **返回值所有权**：
  - `sim_fe_frame` 返回**指向控制器内部帧缓冲的只读指针**，前端不负责释放。该指针在下次 `sim_fe_run_slice`、`sim_fe_step`、`sim_fe_reset`、`sim_fe_destroy` 之后失效。
  - `sim_fe_cpu_snapshot`、`sim_fe_status` 把数据拷贝到调用方提供的结构体。
  - `sim_fe_trace_copy` 把追踪记录拷贝到调用方提供的缓冲区，返回实际拷贝条数。
  - `sim_fe_memory_read` 把字节拷贝到调用方提供的缓冲区。
- **错误消息**：`sim_fe_last_error` 返回的指针指向控制器内部缓冲区，在下次失败调用前有效。注意：若 `sim_fe_create` 失败，控制器对象不存在，此时无法读取错误详情（见第 9 节已知限制）。

---

## 4. 类型与枚举

### 4.1 `SimFeConfig`

```c
typedef struct {
    const char *bios_path;      /* 必填，BIOS ROM 路径（UTF-8）。 */
    const char *floppy_path;    /* 可选，软盘镜像路径。 */
    const char *hdd_path;       /* 可选，固定磁盘镜像路径。 */
    bool        create_hdd;     /* 为 true 时把 hdd_path 创建为新的零填充镜像。 */
    uint16_t    hdd_cylinders;  /* 固定磁盘柱面；为 0 时用默认值 256。 */
    uint8_t     hdd_heads;      /* 磁头；为 0 时用默认值 16。 */
    uint8_t     hdd_sectors;    /* 每道扇区；为 0 时用默认值 63。 */
    uint32_t    speed_multiplier; /* 运行切片倍率；为 0 时用默认值 1。 */
    bool        trace_enabled;  /* 是否开启逐拍追踪；默认 false。 */
    SimRamConfig ram_config;    /* RAM 配置；capacity_bytes 为 0 时用 1MB、基址 0。 */
} SimFeConfig;
```

说明：

- `bios_path` 必须存在，否则 `sim_fe_create` 失败。
- 若既没有 `floppy_path` 也没有 `hdd_path`，控制器仍能创建（引擎已初始化），但 `SimFeStatus.running` 为 `false`，`sim_fe_run_slice` 会返回 `SIM_FE_SIM_ERROR`（此时表示“未运行”，而非引擎故障）。
- 几何默认值仅在 `sim_fe_create` 的初始挂载中生效；`sim_fe_mount_hdd`/`sim_fe_create_hdd` 单独传几何。

### 4.2 `SimFeRunResult`

```c
typedef enum {
    SIM_FE_OK = 0,      /* 推进完成。 */
    SIM_FE_CPU_FAULT,   /* EU 进入 FAULTED 态。 */
    SIM_FE_SIM_ERROR    /* 引擎返回错误，或控制器未处于运行态。 */
} SimFeRunResult;
```

### 4.3 `SimFeStatus`

```c
typedef struct {
    bool     floppy_present;   /* A: 是否已挂载软盘。 */
    bool     hdd_present;      /* C: 是否已挂载固定磁盘。 */
    bool     running;          /* 模拟是否在推进。 */
    uint32_t speed_multiplier; /* 当前倍速。 */
    uint8_t  int13_result;     /* INT 13h 结果：0 成功、1 失败、2 未定、0xFF 未更新。 */
    uint8_t  int13_ah;
    uint8_t  int13_al;
    uint8_t  int13_ch;
    uint8_t  int13_cl;
    uint8_t  int13_dh;
    uint8_t  int13_dl;
} SimFeStatus;
```

说明：`int13_*` 由控制器根据 BIOS 写入 RAM 的诊断区填充，前端不再直接探测该地址。

### 4.4 `SimFeCpuSnapshot`

```c
typedef struct {
    uint16_t reg[8];            /* AX CX DX BX SP BP SI DI。 */
    uint16_t segment[4];        /* ES CS SS DS。 */
    uint16_t ip;
    uint16_t flags;

    Cpu8086EuPhase      eu_phase;      /* 取指/取操作数/执行/Wait/Halt/Fault。 */
    Cpu8086Instruction  instruction;   /* 当前指令枚举。 */
    uint8_t             opcode;
    uint8_t             modrm;
    uint8_t             micro_step;
    uint16_t            instruction_start_ip;

    uint8_t  prefetch[6];       /* 预取队列字节。 */
    unsigned prefetch_count;    /* 实际填充的字节数，<= 6。 */

    Cpu8086BiuTransferPhase biu_phase; /* 总线事务阶段。 */
    Cpu8086BiuBusKind       biu_kind;  /* MEMORY / IO / INTERRUPT_ACK。 */
    bool        biu_write;
    uint32_t    biu_address;
    uint16_t    biu_data;

    uint8_t fault;              /* 故障码，0 表示无。 */
} SimFeCpuSnapshot;
```

说明：字段取自 `Cpu8086State`（见 `cpu8086_state.h`）。BIU 事务字段取自 `biu.data.*`。

### 4.5 `SimCgaRenderFrame`（`sim_cga_render.h`）

```c
typedef struct {
    uint64_t digest;
    bool digest_valid;
} SimCgaRenderDigest;

typedef struct {
    uint32_t pixels[640 * 200];  /* 0x00RRGGBB，alpha 忽略，行优先。 */
    bool     graphics;           /* true 图形模式；false 文本模式。 */
    unsigned columns;            /* 文本模式列数（80 或 40）；图形模式为 0。 */
    uint16_t cursor;             /* 文本模式光标格索引（row*columns+col）；无则 UINT16_MAX。 */
    bool     blink_phase;        /* 当前闪烁相位，用于闪烁属性的前景/背景交换。 */
    bool     changed;            /* 本次与上一次调用的画面相比是否有变化。 */
    SimCgaRenderDigest digest_state; /* 内部变更检测状态，须在帧之间保留。 */
} SimCgaRenderFrame;

void sim_cga_render_frame(const SimCgaState *state, SimCgaRenderFrame *frame);
```

说明：

- 帧缓冲始终为 640x200。文本模式把所有屏幕字符按内嵌 8x8 位图字体缩放进单元网格：80 列 -> 8x8 单元，40 列 -> 16x8 单元；图形模式按 CGA 像素格式生成。
- `changed` 基于对 `vram`、`crtc`、`mode_control`、`color_select`、`blink_phase` 的摘要比较。若调用方想获得稳定的 `changed` 语义，应让同一个 `SimCgaRenderFrame` 跨多次调用保存。
- `digest_state` 是内部状态，调用方不应直接修改；但要用同一个结构体持续调用以启用 `changed` 检测。

---

## 5. 生命周期

### `sim_fe_abi_version`

```c
uint32_t sim_fe_abi_version(void);
```

返回接口版本号。前端与链接库运行时比较，版本不一致时前端应拒绝继续。

### `sim_fe_create`

```c
SimFrontend *sim_fe_create(const SimFeConfig *config);
```

按 `config` 装配整机（初始化 `SimSystem`、加载 BIOS、按需创建并挂载介质、发布 BIOS 介质配置）。成功返回控制器句柄，失败返回 `NULL`。

失败条件：`config` 为 `NULL`、内存不足、`sim_system_init` 失败、BIOS 加载失败、介质挂载/创建失败、发布介质配置失败。

注意：失败时控制器句柄不存在，`sim_fe_last_error` 无法拿到详情（见第 9 节）。

### `sim_fe_destroy`

```c
void sim_fe_destroy(SimFrontend *fe);
```

销毁控制器并释放引擎。若挂载了可写固定磁盘，会先刷新到磁盘。`fe` 为 `NULL` 时安全。

### `sim_fe_reset`

```c
bool sim_fe_reset(SimFrontend *fe);
```

用创建时的配置重新装配整机（等价于销毁后重建）。成功返回 `true`。失败时控制器保持一个不可运行、但可读取 `sim_fe_last_error` 的引擎。

---

## 6. 运行控制

### `sim_fe_run_slice`

```c
SimFeRunResult sim_fe_run_slice(SimFrontend *fe, uint32_t ticks);
```

推进 `ticks * speed_multiplier` 拍。返回：

- `SIM_FE_OK`：正常完成。
- `SIM_FE_CPU_FAULT`：读到 EU 进入 `CPU8086_EU_FAULTED`，同时 `running` 置为 `false`。
- `SIM_FE_SIM_ERROR`：`sim_system_tick` 返回失败，或控制器未就绪/未运行。

前端通常在定时器回调中调用它。

### `sim_fe_step`

```c
bool sim_fe_step(SimFrontend *fe);
```

单拍推进，用于逐拍观察。成功返回 `true`；未就绪/未运行、引擎错误、CPU 故障时返回 `false`。

### `sim_fe_set_speed`

```c
void sim_fe_set_speed(SimFrontend *fe, uint32_t multiplier);
```

设置运行倍率；`multiplier == 0` 视为 `1`。只影响之后 `sim_fe_run_slice` 的推进量。

---

## 7. 显示与输入

### `sim_fe_frame`

```c
const SimCgaRenderFrame *sim_fe_frame(SimFrontend *fe);
```

把当前已提交的 CGA 状态渲染进控制器内部帧缓冲并返回其指针。未就绪时返回 `NULL`。返回的指针由控制器拥有，前端不释放；在下次推进/复位/销毁后失效。

### `sim_fe_inject_scancode`

```c
bool sim_fe_inject_scancode(SimFrontend *fe, uint8_t set1);
```

向 8042 控制器注入一个 PC/AT 扫描码集合 1 的字节（含 `0xE0` 前缀与断码字节）。未就绪时返回 `false`。键码到扫描码的映射由前端负责。

---

## 8. 观察与可视化

### `sim_fe_cpu_snapshot`

```c
bool sim_fe_cpu_snapshot(const SimFrontend *fe, SimFeCpuSnapshot *out);
```

把当前 CPU 状态拷贝到 `out`。成功返回 `true`；未就绪或无状态时返回 `false`。

### `sim_fe_trace_copy`

```c
size_t sim_fe_trace_copy(const SimFrontend *fe, SimTraceRecord *out, size_t count);
```

把追踪缓冲区中最多 `count` 条记录拷贝到 `out`，返回实际拷出的条数。追踪需先通过 `sim_fe_trace_set_enabled(true)` 开启。`SimTraceRecord` 为值拷贝，包含 `cycle`、`kernel_before/after`、`requests`、`response`、`state_changes`。

### `sim_fe_trace_clear`

```c
void sim_fe_trace_clear(SimFrontend *fe);
```

清空追踪缓冲。

### `sim_fe_trace_enabled` / `sim_fe_trace_set_enabled`

```c
bool sim_fe_trace_enabled(const SimFrontend *fe);
void sim_fe_trace_set_enabled(SimFrontend *fe, bool enabled);
```

读取/设置追踪开关。

### `sim_fe_memory_read`

```c
bool sim_fe_memory_read(const SimFrontend *fe, uint32_t address,
                        uint8_t *buffer, size_t length);
```

从物理地址 `address` 起读取 `length` 字节到 `buffer`。任一字节读取失败（越界等）返回 `false` 且不保证 `buffer` 内容。当前仅覆盖 RAM。

### `sim_fe_status`

```c
void sim_fe_status(const SimFrontend *fe, SimFeStatus *out);
```

以结构化结构体填充当前状态，前端据此自行排版。

---

## 9. 介质操作

所有介质函数在未就绪或路径为空时返回 `false`，并通过 `sim_fe_last_error` 记录原因。挂载/弹出后会重新发布 BIOS 介质配置。

```c
bool sim_fe_load_bios(SimFrontend *fe, const char *path);       /* 重新加载 BIOS ROM。 */
bool sim_fe_mount_floppy(SimFrontend *fe, const char *path);    /* 挂载软盘。 */
bool sim_fe_reload_floppy(SimFrontend *fe);                     /* 重载创建时配置的软盘。 */
bool sim_fe_eject_floppy(SimFrontend *fe);                      /* 弹出软盘。 */
bool sim_fe_mount_hdd(SimFrontend *fe, const char *path,
                      uint16_t cylinders, uint8_t heads, uint8_t sectors); /* 挂载固定磁盘。 */
bool sim_fe_create_hdd(SimFrontend *fe, const char *path,
                       uint16_t cylinders, uint8_t heads, uint8_t sectors); /* 创建并挂载新固定磁盘。 */
bool sim_fe_eject_hdd(SimFrontend *fe);                         /* 弹出固定磁盘。 */
bool sim_fe_flush_hdd(SimFrontend *fe);                         /* 刷新可写固定磁盘。 */
```

说明：

- `sim_fe_reload_floppy` 使用创建时记录的 `floppy_path`。
- `sim_fe_flush_hdd` 仅当创建时挂载了可写磁盘（`hdd_needs_flush`）才写入；否则直接返回 `true`。
- `sim_fe_create_hdd` 内部先创建文件，再调用 `sim_fe_mount_hdd`。

### `sim_fe_last_error`

```c
const char *sim_fe_last_error(const SimFrontend *fe);
```

返回最近一次错误描述（控制器内部缓冲区）。`fe` 为 `NULL` 时返回 `"invalid handle"`。

---

## 10. 快速开始示例

以下代码对应已验证的测试路径。

```c
#include <stdio.h>
#include "sim/sim_frontend.h"

int main(void)
{
    SimFeConfig cfg = {0};
    SimFrontend *fe;
    const SimCgaRenderFrame *frame;
    SimFeStatus status;

    if (sim_fe_abi_version() != 1) return 1;

    cfg.bios_path = "firmware/pc_compat_bios.bin";
    cfg.hdd_path = "/tmp/fe_hdd.img";
    cfg.create_hdd = true;
    cfg.hdd_cylinders = 8;
    cfg.hdd_heads = 2;
    cfg.hdd_sectors = 16;

    fe = sim_fe_create(&cfg);
    if (fe == NULL) return 1;

    if (sim_fe_run_slice(fe, 4096) != SIM_FE_OK) return 1;

    frame = sim_fe_frame(fe);
    if (frame != NULL) {
        (void)frame->changed;
    }

    sim_fe_status(fe, &status);
    /* 显示 status.running / status.hdd_present / status.int13_* */

    sim_fe_destroy(fe);
    return 0;
}
```

---

## 11. 已知限制

- 若 `sim_fe_create` 失败，无法通过 `sim_fe_last_error` 读取具体原因（控制器句柄不存在）。后续可考虑引入独立于句柄的错误通道。
- `sim_fe_create` 在 `create_hdd=true` 且 `hdd_path` 已存在时会失败（`sim_disk_create_file` 不覆盖已有文件）。调用方应确保该路径不存在，或改用新建路径。
- `sim_fe_memory_read` 当前只覆盖 RAM，不映射 ROM、VGA 帧缓冲或 I/O 端口。
- `sim_fe_mount_hdd`/`sim_fe_create_hdd` 传入的路径不会改写创建时的 `hdd_path`，因此 `sim_fe_flush_hdd` 仍针对创建时的那条路径刷新。若需刷新另一条路径，需在刷新前暂存或作为后续工作。
- 引擎为单线程；大规模逐拍追踪可能影响界面响应（见设计大纲第 12 章关于线程模型的讨论）。
- `sim_fe_run_slice` 在“未运行”与“引擎错误”两种情况下都返回 `SIM_FE_SIM_ERROR`。如需区分二者，可在后续版本拆分返回码。
- `sim_fe_reset` 复用创建时的配置；若该配置 `create_hdd=true` 且目标已存在，复位会失败（与 `sim_fe_create` 同因）。
- 渲染器内嵌的 8x8 位图字体取自系统的 VGA 控制台字体；高字节区（0x80-0xFF）的字形映射未必与严格 CP437 完全一致，可后续替换为准确 CP437 字体。

---

## 12. 变更记录

| 版本 | 说明 |
| --- | --- |
| 0.1.0 | Phase 0 初版。实现前端控制器与控制器头文件、可移植 CGA 渲染器；通过整库编译与运行时冒烟测试。 |
