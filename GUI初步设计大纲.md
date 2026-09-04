# 8086-CPU-Sim GUI 初步设计大纲

版本：draft 0.1
日期：2026-09-05（星期六）
状态：设计初稿，未进入实现。本文件不修改任何源码。

> 时效性提醒：本文撰写于 2026-09-05。文中涉及的外部技术栈（Qt 版本号、跨平台配置、依赖项、许可条款、GitHub Actions 行为）会随时间变化。后续读者在使用本大纲时，务必按当时的最新文档重新核对该部分，特别要注意：
>
> - Qt 的推荐版本与 LTS 状态（文中推荐 Qt6 6.8 LTS 或更新，需以当时实际 LTS 为准）；
> - Qt 模块的 LGPL/GPL-only 划分（文中列出的 GPL-only 模块清单会随版本调整）；
> - Qt 在 CI / 打包链路上的做法；
> - 文中所引用的现有代码路径与函数名，以仓库当前状态为标准。
>
> 本文的架构与接口设计属于项目长期方向，时效性影响较小；版本敏感内容集中在第 7、8 章。请在落地时逐条复核。

---

## 0. 文档说明

### 0.1 目标读者

本大纲读者：项目维护者、后续实现前端与引擎接口的开发者。

### 0.2 文档范围

本大纲负责：

- 说明现状与耦合点；
- 给出目标架构（严格前后端分离、全平台通用、避免 Win32 API）；
- 定义引擎与前端之间的边界与接口（API 初稿）；
- 明确解耦性与可扩展性的设计方向；
- 为 CPU 指令集可视化提供接口与数据来源；
- 给出工程实现、前端落地清单、CI 与许可约束。

### 0.3 范围外事项

- 所有视觉外观效果、皮肤、动效、字体、玻璃拟态等，均不在本大纲范围。相关讨论另行进行，后期调整。
- 引擎 CPU 指令行为、外设行为模型的正确性不在本大纲范围。本大纲假定引擎现有行为正确。
- 本大纲不包含代码改动本身，仅给出设计与接口签名。

### 0.4 约定

- 术语“引擎”指模拟器核心（CPU、内核、总线、内存、外设、CGA 状态），对应 `src/`、`include/sim/` 下除前端宿主相关以外的 C 代码。
- 术语“前端”指图形界面宿主，当前为 `apps/pc_sim_launcher.c`，目标为基于 Qt 的跨平台前端。
- 术语“前端 API”指介于引擎与具体 GUI 之间的稳定 C 接口，命名前缀 `sim_fe_`。
- 命名前缀与现有 `sim_`、`cpu8086_` 保持一致。数组、结构体、枚举均沿用现有风格。

---

## 1. 现状分析（基于现有代码）

### 1.1 整体结构

- 引擎以 `SimSystem` 为整机装配体（见 `include/sim/sim_system.h`），内部持有一个状态区、一个追踪器、一个内核、一个总线，以及 RAM、ROM、PIC、PIC 从片、PIT、CGA、磁盘、键盘、DMA 和 CPU 各模块。
- 引擎顶层接口：`sim_system_init`、`sim_system_destroy`、`sim_system_reset`、`sim_system_publish_bios_configuration`、`sim_system_tick`（见 `src/sim_system.c`）。
- 前端通过顶层接口驱动整机。当前前端为 Win32 GUI（`apps/pc_sim_launcher.c`，约 999 行）。

### 1.2 引擎侧现状（可移植性）

- 引擎的 CPU、内核、总线、内存与各外设状态逻辑为纯 C，不含平台头文件。以下文件因文件 I/O 与计时需要，已有 `_WIN32` 与 POSIX 分支，属可移植的平台胶水：
  - `src/sim_disk.c`：文件读写（Windows 用 Win32 文件 API，POSIX 用 open/read/write）。
  - `src/sim_rom.c`：ROM 文件加载。
  - `src/sim_clock.c`：计时（Windows 用 QueryPerformanceCounter，POSIX 用 clock_gettime）。
- 唯一强依赖 Win32/GDI 的实现是 `src/sim_cga_console.c`：它负责把 `SimCgaState` 渲染成窗口像素、维护 CGA 显示子窗口、把 Win32 键码映射为 8042 扫描码、处理键盘事件与窗口消息。
- 该文件在各平台编译路径上产出不同实现：`_WIN32` 分支为完整实现，非 Windows 分支为“仅 Windows 可用”桩。

### 1.3 前端侧现状（Win32 耦合）

`apps/pc_sim_launcher.c` 承担全部 GUI 职责，包括：

- 窗口类注册、消息循环、定时器。
- 组合框（BIOS / 软盘 / 硬盘 / 倍速）、按钮（浏览 / 弹出 / 重载 / 新建 / 挂载重启）、状态文本。
- CGA 显示子窗口（调用 `sim_cga_console_*` 系列）。
- 键盘事件到 8042 扫描码。
- 文件对话框（`GetOpenFileNameW` / `GetSaveFileNameW`）。
- 初始化配置、命令行参数解析、默认 BIOS 路径解析。

### 1.4 现有耦合点与硬编码清单

以下为当前前端与引擎、平台之间的强耦合点，是实现“可解耦、全平台”时必须处理的项：

- C1. `src/sim_cga_console.c` 使用 Win32/GDI 渲染与字体，是引擎侧唯一的非可移植实现。
- C2. `apps/pc_sim_launcher.c` 使用 Win32 窗口与控件，是整个 GUI 宿主不可移植。
- C3. 前端直接探测内存（`update_int13_diagnostic`），通过硬编码地址 `0x4E0`（`INT13_DIAGNOSTIC_BASE`）读取序列号、AX、CX、DX、结果。这部分逻辑属于引擎状态上报，不应由前端直接读 RAM。
- C4. 前端硬编码磁盘几何：`HDD_CYLINDERS=256`、`HDD_HEADS=16`、`HDD_SECTORS=63`。
- C5. 前端持有并传递倍速（`speed_multiplier`），应作为配置/运行参数而非前端私有状态。
- C6. 前端硬编码窗口与布局常量（`WINDOW_CLIENT_WIDTH`、`PANEL_X`、`DISPLAY_*` 等）。这些属于前端展示偏好，应归属前端，但需明确其边界，避免与引擎耦合。
- C7. 前端自行做命令行参数解析与默认 BIOS 路径解析（`set_initial_configuration`、`default_bios_path`）。该逻辑应下沉为可复用的配置层，或作为前端装配的一部分，但不应与引擎耦合。
- C8. 前端直接调用多个底层引擎函数（`sim_disk_*`、`sim_rom_load_file`、`sim_system_publish_bios_configuration`、`sim_trace_set_enabled` 等），这些调用应被前端 API 收拢，避免前端依赖引擎内部细节。

---

## 2. 目标架构

### 2.1 分层模型（三层）

```
+--------------------------------------------------------------+
| 前端层（Frontend）：纯展示。                                   |
|   窗口、控件、渲染、动画、文件对话框、键码到扫描码、定时器。      |
|   不包含模拟逻辑，不直接访问引擎内部。                           |
+------------------------------+-------------------------------+
                               | 调用前端 API（extern "C"）
+------------------------------+-------------------------------+
| 前端 API / 控制器（Frontend API）：唯一交互边界。                 |
|   生命周期、运行切片、单步、帧、CPU 快照、追踪、内存读取、        |
|   状态上报、媒体与速度配置。                                    |
+------------------------------+-------------------------------+
                               | 读写可移植引擎
+------------------------------+-------------------------------+
| 引擎库 libsim（纯 C）：                                        |
|   SimSystem、CPU、内核、总线、RAM/ROM、PIC/PIT/CGA/DMA/磁盘/键盘。|
|   可移植 CGA 渲染器。                                           |
|   CPU/内核/总线/外设逻辑零平台头；文件I/O与计时为可移植胶水。    |
+--------------------------------------------------------------+
```

### 2.2 边界规则

- 前端层只通过 `sim_frontend.h` 与引擎交互；不 `#include` 引擎内部头文件，不直接访问 `SimSystem` 字段。
- 前端 API 层只读写引擎，不包含任何 GUI 平台代码。
- 引擎库只提供模拟能力，不提供窗口、不渲染到屏幕、不依赖任何 GUI 库。
- 引擎的 CPU、内核、总线与外设状态逻辑不含任何平台头文件。文件 I/O 与计时胶水（`sim_disk.c`、`sim_rom.c`、`sim_clock.c`）按平台分支使用平台 API，属可移植逻辑；GUI 不使用 Win32 API。

### 2.3 设计目标

- 前后端严格分离：引擎与 GUI 相互独立可替换。
- 全平台通用：引擎的 CPU/内核/总线/外设状态逻辑不含平台头文件，文件 I/O 与计时使用可移植胶水；前端通过跨平台 GUI 库实现。
- 可解耦：消除硬编码地址、硬编码几何、前端直接探测内存等耦合。
- 可扩展：为后续 CPU 指令集可视化、新增设备、新增观察面板、新增 GUI 后端预留接口。

---

## 3. 前后端分离设计

### 3.1 分离原则

- 引擎是唯一状态来源（source of truth）。
- 前端是引擎的观察者与控制者，通过调用接口改变引擎行为、读取引擎快照。
- 前端不直接修改引擎状态，只通过接口提交意图（如挂载介质、注入扫描码、推进切片）。

### 3.2 引擎边界

引擎对外暴露两类能力：

- 行为控制：创建、销毁、复位、推进、挂载/弹出介质、加载 BIOS、设置追踪开关、设置倍速。
- 状态读取：CGA 帧、CPU 快照、追踪记录、内存读取、结构化状态上报。

引擎不暴露：

- 任何窗口、控件、字体、渲染后端接口（CGA 渲染以像素/文本帧形式输出）。
- 任何文件对话框。
- 任何 GUI 库头文件依赖。

### 3.3 前端 API 边界

前端 API 是引擎与 GUI 之间唯一通道。它负责：

- 封装 `SimSystem` 生命周期与装配。
- 封装运行切片、单步、介质操作、状态上报。
- 为可视化提供 CPU 快照、追踪、内存读取。
- 以值快照或只读指针形式返回数据，避免前端持有一个可变引擎引用带来的耦合。
- 提供 ABI 版本校验，保证前后端独立演进时相互可识别。

### 3.4 数据交换方式

- 引擎内部互不暴露：前端通过 API 拿到的均为拷贝快照或指向引擎内部缓冲区的只读指针；指针生命周期由 API 约定。
- 采用值快照结构体承载跨边界数据（CPU 快照、状态、追踪记录），以保持 ABI 稳定。
- 配置通过结构体传入，避免前端散落硬编码。
- 引入一个稳定的 ABI 版本号，前端与引擎在运行时校验。

---

## 4. 可解耦性设计

### 4.1 解耦目标

消除前端对引擎内部细节与平台细节的依赖，使引擎可在任意平台编译，前端可任意替换。

### 4.2 逐一处理现有硬编码

| 编号 | 现状 | 处理方式 |
| ------ | ------ | ---------- |
| C1 | `sim_cga_console.c` 用 Win32/GDI | 用可移植 CGA 渲染器替代，输出像素/文本帧；Win32/GDI 实现删除或仅作回归参考 |
| C3 | 前端读 `0x4E0` | 由后端通过结构化状态上报（软盘/硬盘是否挂载、INT13 结果），前端不再直接读 RAM |
| C4 | 前端硬编码磁盘几何 | 几何作为 `SimFeConfig` 的默认值，可由前端传入覆盖 |
| C5 | 前端持有倍速 | 通过 `sim_fe_set_speed` 配置，不留在前端私有状态 |
| C6 | 前端窗口/布局常量 | 明确属于前端展示层，归前端所有；不与引擎耦合 |
| C7 | 命令行参数与默认路径解析 | 下沉为前端装配/配置层逻辑，作为调用 API 前的输入准备 |
| C8 | 前端直接调用多个底层引擎函数 | 收拢到前端 API；前端不再直接依赖引擎内部接口 |

### 4.3 配置下沉规则

- 一切可变的整机配置（RAM 配置、磁盘几何、路径、倍速、追踪策略）统一进入配置结构，由前端与 API 层传递。
- 前端只负责收集用户输入与展示，不负责决定引擎行为参数。
- 引擎不持有任何用户界面偏好。

### 4.4 错误与状态上报

- 状态上报采用结构化结构体。格式化字符串由前端按需生成（利于本地化与前端自由排版）。
- 错误采用返回码 + 最近错误信息（`sim_fe_last_error`），避免在前端散落错误处理。

---

## 5. 可扩展性设计

### 5.1 扩展原则

- 新增能力首先询问引擎状态，仅在必要时新增接口。
- 接口以版本化、参数化结构体演进，避免破坏已有调用方。
- 前端与引擎各自可独立扩展。

### 5.2 面向未来新增功能的接口形态

- 读取型功能：通过快照/追踪/内存读取实现，不改引擎。例：CPU 指令可视化、寄存器查看器、内存查看器。
- 行为型功能：通过控制接口提交意图，不改引擎接口签名。例：挂载新介质、改变倍速。
- 新引擎能力：在引擎侧新增模块或新接口，并在前端 API 暴露对应入口。

### 5.3 引擎侧扩展点

- 引擎已有模块化注册机制：`SimModule`（见 `include/sim/sim_kernel.h`）通过 reset/drive/sample/trace/destroy 四个回调接入内核；设备通过 `SimBusTarget`（见 `include/sim/sim_bus.h`）注册到总线。
- 新增设备（如 8255、8251、串口、并行口）可在不修改现有模块的前提下挂接。
- 前端 API 预留一个可选的扩展注册入口，允许外部在启动前向系统注入额外模块（仅在确有需要时启用，默认不暴露引擎内部类型）。

### 5.4 前端侧扩展点

- 前端 API 是稳定的 C ABI。任何满足该契约的 GUI 后端（Qt、SDL、终端、Python/Rust 绑定、测试驱动）均可直接使用。
- 新增观察面板只需消费现有快照/追踪/内存接口，无需改引擎。
- 为未来插件提供版本化扩展登记点，但本阶段不强制实现。

---

## 6. 前端 API 设计（接口初稿）

### 6.1 命名与位置

- 头文件：`include/sim/sim_frontend.h`
- 实现：`src/sim_frontend.c`
- 前缀：`sim_fe_`
- 类型：`SimFrontend`（不透明句柄），`SimFe*` 系列结构体。

### 6.2 生命周期与 ABI 校验

```c
/* 返回前端 API 的 ABI 版本，前端与引擎运行时比对。 */
uint32_t sim_fe_abi_version(void);

/* 创建前端控制器；config 为一次性装配输入。失败返回 NULL。 */
SimFrontend *sim_fe_create(const SimFeConfig *config);

/* 销毁前端控制器并释放引擎资源。 */
void sim_fe_destroy(SimFrontend *fe);

/* 使用保存的配置重新装配整机（可选，用于“重挂载/重启”）。 */
bool sim_fe_reset(SimFrontend *fe);
```

### 6.3 配置结构

```c
typedef struct {
    const char *bios_path;      /* 必填，BIOS ROM 文件路径（UTF-8）。 */
    const char *floppy_path;    /* 可选，软盘镜像路径。 */
    const char *hdd_path;       /* 可选，硬盘镜像路径。 */
    bool        create_hdd;     /* 为 true 时把 hdd_path 创建为新镜像。 */
    uint16_t    hdd_cylinders;  /* 硬盘几何默认值，可由调用方覆盖，默认 256。 */
    uint8_t     hdd_heads;      /* 默认 16。 */
    uint8_t     hdd_sectors;    /* 默认 63。 */
    uint32_t    speed_multiplier; /* 运行切片倍率，默认 1。 */
    bool        trace_enabled;  /* 是否开启追踪，默认关闭。 */
    SimRamConfig ram_config;    /* RAM 配置，可空指针用默认（1MB、基址 0）。 */
} SimFeConfig;
```

说明：`SimRamConfig` 见 `include/sim/sim_ram.h`。实现内部若不传则使用 `{0, 0x100000, 0, 0, 0}`。

### 6.4 运行控制

```c
typedef enum {
    SIM_FE_OK = 0,            /* 正常。 */
    SIM_FE_CPU_FAULT,         /* 到达 CPU 故障态。 */
    SIM_FE_SIM_ERROR          /* 模拟内核返回错误。 */
} SimFeRunResult;

/* 推进指定拍数；返回运行结果，并记录错误到 sim_fe_last_error。 */
SimFeRunResult sim_fe_run_slice(SimFrontend *fe, uint32_t ticks);

/* 单拍推进，用于 CPU 指令可视化精细控制。 */
bool sim_fe_step(SimFrontend *fe);

/* 设置运行倍率。 */
void sim_fe_set_speed(SimFrontend *fe, uint32_t multiplier);
```

### 6.5 CGA 帧

```c
typedef struct {
    const uint32_t *pixels;     /* 图形模式：640*200 RGBA。 */
    const uint8_t  *characters; /* 文本模式：80*25 字符。 */
    const uint8_t  *attributes; /* 文本模式：80*25 属性。 */
    uint16_t        cursor;     /* 文本模式光标位置。 */
    unsigned        columns;    /* 文本模式每行列数，80 或 40。 */
    bool            blink_phase;
    bool            graphics;   /* true 为图形模式，false 为文本模式。 */
    bool            changed;    /* 是否较上次有变化。 */
} SimFeFrame;

/* 返回当前帧只读指针；指针在下次 sim_fe_run_slice/step 之前有效。 */
const SimFeFrame *sim_fe_frame(const SimFrontend *fe);
```

说明：CGA 帧由可移植渲染器（替代 `sim_cga_console.c` 的 `sim_cga_console_render`）生成，不依赖任何 GUI 库。

### 6.6 输入

```c
/* 注入一个 PC/AT 扫描码集合 1 的字节（含 0xE0 前缀与断码）。 */
bool sim_fe_inject_scancode(SimFrontend *fe, uint8_t set1);
```

说明：对应现有 `sim_keyboard_enqueue_scancode`（见 `include/sim/sim_keyboard.h`），前端负责键码到扫描码的映射。

### 6.7 CPU 快照（供 CPU 指令集可视化）

```c
typedef struct {
    uint16_t    reg[8];           /* 通用寄存器：AX/CX/DX/BX/SP/BP/SI/DI。 */
    uint16_t    segment[4];       /* ES/CS/SS/DS。 */
    uint16_t    ip;
    uint16_t    flags;

    Cpu8086EuPhase      eu_phase;      /* 取指/取操作数/执行/Wait/Halt/Fault。 */
    Cpu8086Instruction  instruction;   /* 当前指令枚举。 */
    uint8_t             opcode;
    uint8_t             modrm;
    uint8_t             micro_step;
    uint16_t            instruction_start_ip;

    uint8_t  prefetch[6];        /* 预取队列字节。 */
    unsigned prefetch_count;

    /* BIU 当前总线事务。 */
    Cpu8086BiuTransferPhase biu_phase;
    Cpu8086BiuBusKind       biu_kind;
    bool        biu_write;
    uint32_t    biu_address;
    uint16_t    biu_data;

    /* 由 Cpu8086State.eu.fault 映射到对外可读值。 */
    uint8_t fault;
} SimFeCpuSnapshot;

/* 把当前 CPU 状态拷贝到调用方提供的结构体。成功返回 true。 */
bool sim_fe_cpu_snapshot(const SimFrontend *fe, SimFeCpuSnapshot *out);
```

说明：字段来源为 `include/sim/cpu8086_state.h` 中的 `Cpu8086State`（general/segment/ip/flags/prefetch/biu/eu），以及 `cpu8086_current_state`（见 `include/sim/cpu8086.h`）。其中 BIU 事务字段取自 `Cpu8086State.biu.data`（`Cpu8086BiuDataState`，含 phase/kind/write/address/data）。该结构体用于可视化“一条指令如何在 CPU 中跑”，不要求前端直接 include 引擎内部结构体。

### 6.8 追踪

```c
/* 拷贝出 count 条追踪记录到 out。返回实际拷贝条数。 */
size_t sim_fe_trace_copy(const SimFrontend *fe, SimTraceRecord *out, size_t count);

/* 清空内部追踪缓冲。 */
void sim_fe_trace_clear(SimFrontend *fe);

bool sim_fe_trace_enabled(const SimFrontend *fe);
void sim_fe_trace_set_enabled(SimFrontend *fe, bool enabled);
```

说明：`SimTraceRecord` 见 `include/sim/sim_trace.h`，包含 cycle、kernel_before/after、requests、response、state_changes（owner/field/before/after）。用于数据流/时序可视化。

### 6.9 内存读取

```c
/* 读取物理地址 addr 起 len 字节（只读，用于内存/显存查看器）。 */
bool sim_fe_memory_read(const SimFrontend *fe, uint32_t addr, uint8_t *buf, size_t len);
```

说明：对应 `sim_ram_peek_byte`/`sim_ram_load_bytes` 的只读能力，供前端观察内存与显存。

### 6.10 状态与媒体操作

```c
typedef struct {
    bool     floppy_present;
    bool     hdd_present;
    bool     running;
    uint32_t speed_multiplier;
    /* INT13 诊断，由后端填充，前端不再直接读 RAM。 */
    uint8_t  int13_result;     /* 0 成功 / 1 失败 / 2 未定。 */
    uint8_t  int13_ah;
    uint8_t  int13_al;
    uint8_t  int13_ch;
    uint8_t  int13_cl;
    uint8_t  int13_dh;
    uint8_t  int13_dl;
} SimFeStatus;

void sim_fe_status(const SimFrontend *fe, SimFeStatus *out);

/* 挂载/重载/弹出介质与 BIOS。 */
bool sim_fe_load_bios(SimFrontend *fe, const char *path);
bool sim_fe_mount_floppy(SimFrontend *fe, const char *path);
bool sim_fe_reload_floppy(SimFrontend *fe);
bool sim_fe_eject_floppy(SimFrontend *fe);
bool sim_fe_mount_hdd(SimFrontend *fe, const char *path,
                      uint16_t cyl, uint8_t head, uint8_t sec);
bool sim_fe_create_hdd(SimFrontend *fe, const char *path,
                       uint16_t cyl, uint8_t head, uint8_t sec);
bool sim_fe_eject_hdd(SimFrontend *fe);
bool sim_fe_flush_hdd(SimFrontend *fe);
```

### 6.11 错误

```c
/* 返回最近一次错误描述（线程内静态），用于前端展示。 */
const char *sim_fe_last_error(const SimFrontend *fe);
```

### 6.12 接口签名汇总（可编译伪代码）

见上文 6.2–6.11。此处不再重复。

### 6.13 实现要点

- 内部持有 `SimSystem`、`SimFeConfig`、CGA 渲染器状态、追踪缓冲、帧缓冲、最近错误。
- `sim_fe_frame` 复用引擎 `sim_cga_current_state` 与可移植渲染器，仅在 `changed`（可用状态摘要哈希判断）变化时重新渲染。
- `sim_fe_run_slice` 循环调用 `sim_system_tick`，并在每拍检查 `cpu8086_current_state` 的 `eu.phase` 是否为 `CPU8086_EU_FAULTED`，据此返回 `SIM_FE_CPU_FAULT`。
- `sim_fe_status` 由前端 API 层（`sim_frontend.c`）读取并填充 INT13 诊断，GUI 不再直接探测地址 `0x4E0`。更彻底的做法是让引擎在运行时上报该信息，本阶段由 API 层集中读取即可。
- `sim_fe_destroy` 应处理已挂载可写硬盘的刷新（与现有 `save_and_destroy_system` 行为一致）；前端也可在销毁前显式调用 `sim_fe_flush_hdd`。
- 磁盘几何默认值来自配置，可被 `sim_fe_mount_hdd`/`sim_fe_create_hdd` 覆盖。

---

## 7. 工程设计与实现

### 7.1 构建系统（Makefile 演进）

- 引擎对象与当前一致，编译为 `build/*.o`，并汇总为静态库 `build/libsim.a`（可选动态库）。
- 增加前端开关变量 `FE`：
  - `FE ?= auto|none|qt|sdl|test`
  - `auto`：自动探测，Windows 默认 Qt（如有）否则 none；Linux/macOS 默认 Qt（如有）否则 none。
  - `none`：只建引擎库（无前端），供无头测试。
  - `qt`：构建 Qt 前端。
  - `sdl`：预留，构建 SDL 前端（本阶段不实现）。
  - `test`：构建无头测试驱动，验证前端 API。
- 引擎对象的编译规则不随 `FE` 变化；仅前端链接目标随 `FE` 变化。
- `make` 一键语义保留：默认目标仍为 `all`，默认 `FE` 为 `auto`，软件栈齐全时产出前端可执行文件，否则产出引擎库并提示。
- 前端工具链可通过 `pkg-config`（`Qt6Widgets`/`Qt6Quick`/`Qt6Gui`/`Qt6Core`）或环境变量获取编译与链接参数。
- 若前端类含 `Q_OBJECT` 宏（信号槽），需在编译前用 `moc` 预处理（可经 `pkg-config` 取编译器参数）；若完全不使用 `Q_OBJECT`（如仅用 lambda 连接），可省略 `moc`。
- 保留现有的 `TARGET`（依赖宿主平台检测）与交叉编译路径。

### 7.2 目录与文件布局

```
include/sim/sim_frontend.h    前端 API 头文件
src/sim_frontend.c            前端 API 实现
src/sim_cga_render.c          可移植 CGA 渲染器（替代 sim_cga_console.c 的渲染部分）
include/sim/sim_cga_render.h  渲染器接口
apps/frontend-qt/             Qt 前端（C++），仅 include sim/sim_frontend.h
  pc_sim_app.cpp/.h
  cga_view.cpp/.h             （绘制帧、倍速、键码映射、定时器）
  main.cpp
tests/test_frontend.c         无头测试驱动（不使用 GUI）
```

建议的演进顺序：先 `sim_frontend.c` + `sim_cga_render.c`，再 `frontend-qt`，最后替换 `sim_cga_console.c`。

可移植 CGA 渲染器应内嵌公有领域的 CP437 位图字体，例如 `dhepper/font8x8`（基于 IBM 公有领域 VGA 字体）。文本模式下 CGA 字符胞为 8x8 像素，位图字体按 1:1 写入 640x200 光栅即像素级清晰，比现有 Win32 路径（GDI `Terminal` 字体 + `StretchDIBits` HALFTONE 平滑）更锐利，也更贴近原版 CGA。字体以 8x8 为主（对应 CGA 80/40 列模式、25 行）；可另建一个 8x16（VGA 风）位图供可选“清晰文本”路径，但 8x16 单胞高 16 像素会改变与 640x200 光栅的映射，默认应使用 8x8。显示缩放建议用最近邻整数倍放大（Qt QPainter `Qt::FastTransformation`、Qt Quick `Image.smooth=false`），或叠 CRT 扫描线，以保持位图字体的锐利像素边缘；避免平滑/双线性插值，否则 8x8 放大后会发糊。该方案跨平台确定一致，不依赖宿主字体。

### 7.3 无头化与测试

- 前端 API 设计为可在无 GUI 环境下运行（创建/推进/读取快照/读取状态），因此可直接被测试驱动调用。
- 新增 `test` 目标：创建 `SimFrontend`，挂载已知 BIOS 与磁盘镜像，执行若干拍，校验 CPU 状态、CGA 帧、媒体状态与错误路径。
- 该测试不依赖任何 GUI 库，可在 Linux/macOS/Windows 原生与 Linux 交叉环境下运行。

### 7.4 跨平台 CI 构建矩阵

CI 需区分两类目标：GitHub Actions 的“运行器操作系统（runner）”与“发行包格式”。运行器只负责在某个操作系统上编译出可执行文件；同一产物可再打成本地发行包格式。现有 `.github/workflows/release.yml` 在 `ubuntu-latest` 上已产出 `deb`、`rpm`、`tar.gz` 与 Windows `zip`，并非只做 Debian。

- 构建矩阵（三个原生运行器），分别在 `ubuntu-latest`、`macos-latest`、`windows-latest`：
  - 编译 `libsim`：`cc -std=c11 -Wall -Wextra -Wpedantic -Iinclude -c src/*.c`，要求 CPU/内核/总线/外设状态逻辑零平台头；文件 I/O 与计时走各自平台分支。
  - 构建前端（Qt 或 none）：在对应平台原生构建。
- 保留现有 Linux 交叉编译 Windows（`TARGET=windows CC=i686-w64-mingw32-gcc`）用于引擎库。前端 Windows 版可在 `windows-latest` 用 `install-qt-action` 安装 Qt（默认 6.8.x，可用 `win64_mingw` 架构与 MinGW 工具链），也可在 Linux runner 上以 `win64_mingw` 架构交叉构建。
- 新增 `libsim` 全平台编译检查，作为“跨平台可被持续验证”的硬性门槛。

### 7.5 许可证与依赖约束

- 引擎库保持纯 C，许可 MIT。
- 前端若采用 Qt6，则使用 LGPLv3 版本并动态链接。前端自身代码仍为 MIT。
- 只链接 Qt6 的 Core/Gui/Widgets（与可选 Quick）。禁止引入 GPL-only 模块（Qt Charts、Qt Data Visualization、Qt Graphs、Qt GRPC、Qt Quick 3D、Qt Quick 3D Physics、Qt Quick Timeline）。
- Linux 包将 Qt 列为系统依赖：Debian/Ubuntu 用 `libqt6widgets6` 等，Fedora/RHEL 用 `qt6-qtbase`，Arch 用 `qt6-base`。Windows 需随包附 Qt 库、许可文本与源码获取方式；macOS 打包相应 framework。

### 7.6 跨平台发行包与安装格式

现有 CI 已覆盖部分格式，需按平台补齐。下表列出目标格式、对应安装器与最直接构建方式。

| 平台 / 发行版 | 包格式 | 构建方式 | 现状 |
| ------------ | ------ | -------- | ---- |
| Debian / Ubuntu | `.deb` | `dpkg-deb` | 已有 |
| Red Hat / Fedora / RHEL 系 | `.rpm` | `rpmbuild` | 已有 |
| Arch Linux | `.pkg.tar.zst` | `makepkg`（需 PKGBUILD） | 待补 |
| Linux 通用便携 | `.tar.gz` | 直接打包 | 已有 |
| Windows | `.zip` | `zip` | 已有 |
| macOS | `.dmg`（可选） | `hdiutil` / `create-dmg` | 待补 |

说明：

- Arch 无 GitHub 原生运行器。需在 CI 内用 Arch 容器（如 `docker://archlinux`）安装 `base-devel` 与 `qt6-base`，用 `makepkg` 产出 `.pkg.tar.zst` 后上传 Release。Arch 打包需提供 `PKGBUILD`，依赖 `qt6-base`（无需 `qt6-charts` 等 GPL-only 模块）。
- RPM 依赖以目标发行版为准；针对 RHEL/Fedora 应在对应 el/版本上校验动态链接依赖（Qt6、glibc）。
- 各 Linux 发行版安装 Qt：Debian/Ubuntu `libqt6widgets6` 等；Fedora/RHEL `qt6-qtbase`；Arch `qt6-base`。
- Windows `.zip` 需内附 Qt DLL（LGPLv3 合规：随包附许可文本与源码获取方式，并允许替换）。
- macOS 可选打包 `.dmg` 并内嵌 Qt framework，或要求 Homebrew 安装 Qt。

---

## 8. 前端落地指南与清单

### 8.1 技术选型与前置依赖

- 推荐 Qt6（建议 6.8 LTS 或更新的 LTS），组件 Core/Gui/Widgets（可选 Quick）。不使用 Qt5。
- 前端语言 C++；引擎为 C，通过 `extern "C"` 暴露。前端只引入 `sim_frontend.h`。
- 前置：Qt6 开发库、pkg-config（或等价）、C/C++ 编译器、make、binutils（AS/OBJCOPY）。moc/uic 随 Qt 工具链提供；若使用 Q_OBJECT 信号槽则需调用 moc。

### 8.2 前端目录结构

见 7.2。前端代码全部位于 `apps/frontend-qt/`，与引擎、旧 `apps/pc_sim_launcher.c` 隔离。

### 8.3 功能实现清单

前端需实现以下职责，全部通过 `sim_frontend.h`：

- 创建与销毁 `SimFrontend`，构造 `SimFeConfig`（从用户输入收集路径与几何）。
- 建立窗口与主循环；用定时器周期性调用 `sim_fe_run_slice`。
- 显示 CGA 帧：从 `sim_fe_frame` 取像素/文本，执行缩放与绘制（缩放为最近邻，保持像素比例）。
- 键盘事件：将 Qt 键码映射为扫描码集合 1，调用 `sim_fe_inject_scancode`。
- 文件选择：BIOS/软盘/硬盘 的文件对话框（Qt 原生）。
- 媒体操作：挂载、重载、弹出、新建硬盘、退出时刷新（对应 `sim_fe_*` 媒体接口）。
- 速度控制：`sim_fe_set_speed`。
- 状态显示：`sim_fe_status` 格式化输出。
- 错误提示：`sim_fe_last_error`。
- 预留 CPU 指令可视化面板：消费 `sim_fe_cpu_snapshot`、`sim_fe_trace_copy`、`sim_fe_step`、`sim_fe_memory_read`。

### 8.4 前端职责边界

- 前端不直接访问引擎内部结构体或函数。
- 前端不读取固定 RAM 地址用于状态判断。
- 前端不确定引擎行为参数（几何、RAM 配置）除作为默认值外不再重复。
- 前端不包含任何平台相关的引擎逻辑。

---

## 9. CPU 指令集可视化方案

### 9.1 数据来源

- 寄存器/IP/Flags/EU 阶段/指令/微步/预取/BIU 事务：`sim_fe_cpu_snapshot`。
- 每拍的总线请求/响应/状态变化：`sim_fe_trace_copy`（`SimTraceRecord` 含 `cycle`、`kernel_before/after`、`requests`、`response`、`state_changes`）。
- 内存/显存：`sim_fe_memory_read`。

### 9.2 可视化信息分解

将一条指令的执行拆为可观察的阶段：

- 取指与预取：展示 `prefetch` 队列字节、`biu_phase`（IDLE/PENDING/INFLIGHT/COMPLETE）、`biu_kind`（MEMORY/IO/INTERRUPT_ACK）、`biu_address`/`biu_data`。
- 译码与取操作数：展示 `eu_phase`（NEED_OPCODE/NEED_OPERANDS）、`opcode`、`modrm`、`embedded_register`、`operand_count`、`immediate_bytes`、`displacement_bytes`。
- 执行与微步：展示 `instruction`（枚举）、`micro_step`、`instruction_start_ip`、`repeat_prefix`、`segment_override`。
- 结果写回：展示寄存器与 Flags 差异，可通过 `SimTraceStateChange`（owner/field/before/after）呈现。
- 状态摘要：`kernel_before/after` 可展示总线阶段变迁（T1/T2/T3/T4）。

### 9.3 交互与控制

- 提供“单步”（`sim_fe_step`）与“运行切片”（`sim_fe_run_slice`）切换。
- 暂停/继续；重置（`sim_fe_reset`）。
- 速度档位（`sim_fe_set_speed`）。
- 可暂停在某条指令的某个微步，逐拍观察。

### 9.4 与引擎的关系

- 可视化仅为引擎快照的消费者，不修改引擎。
- 不要求前端 include 引擎内部头文件；所需字段已通过 `SimFeCpuSnapshot` 与 `SimTraceRecord` 对外。
- 后续如需更多字段（如完整解码信息、指令助记符），在快照结构中追加并按 ABI 版本兼容。

---

## 10. 未来新增功能的扩展方式

### 10.1 新增读取型功能（观察面板）

- 直接消费 `sim_fe_cpu_snapshot`、`sim_fe_trace_copy`、`sim_fe_memory_read`、`sim_fe_status`。
- 不修改引擎，不修改前端 API 签名。

### 10.2 新增引擎行为（新设备、新指令）

- 引擎侧按现有 `SimModule`/`SimBusTarget` 注册机制新增模块或设备。
- 在 `SimSystem` 装配中挂接，或在 `sim_frontend.c` 内新增装配入口。
- 如需外部注入，在前端 API 预留可选扩展注册入口（默认不暴露引擎内部类型）。

### 10.3 新增一个 API 能力

- 在 `sim_frontend.c` 新增函数，并在 `sim_frontend.h` 声明。
- 递增 ABI 版本号；保持既有函数签名不变以满足向后兼容。
- 若需扩展参数，优先采用“新增函数”或“新增结构体字段并加版本”的方式，避免破坏已有调用。

### 10.4 新增一个 GUI 后端

- 实现 `sim_frontend.h` 契约（生命周期/运行/帧/输入/状态/媒体/快照）。
- 运行时校验 `sim_fe_abi_version`。
- 现有引擎与其余前端零改动。

### 10.5 新增一个可视化面板

- 在 Qt 前端新增面板，消费现有快照/追踪/内存接口。
- 不改引擎，不改前端 API。

---

## 11. 阶段计划（里程碑）

| 阶段 | 内容 | 产出 | 备注 |
| ------ | ------ | ------ | ------ |
| 0 | 前端 API + 可移植 CGA 渲染器 | `sim_frontend.c/.h`、`sim_cga_render.c/.h` | 引擎行为不变；Win32 前端仍可用，作为回归参照 |
| 1 | 去除引擎侧 Win32（替换 `sim_cga_console.c`） | 引擎库零平台头 | 需验证无头测试与旧前端回归 |
| 2 | 消除硬编码（几何/内存探测/速度/参数解析下沉） | 配置下沉、状态上报 | 前端不再直接读 RAM |
| 3 | Qt6 前端功能实现 | `apps/frontend-qt/` 可执行 | 替换 Win32 前端为默认 |
| 4 | CPU 指令集可视化面板 | 寄存器/时序/总线/解码面板 | 消费快照与追踪 |
| 5 | 跨平台 CI 矩阵、打包 | 三平台构建与检测 | 硬性验证跨平台 |

---

## 12. 风险与限制

- 前端 API 领域较广（运行/媒体/快照/追踪/状态），实现量中等，需保证单测覆盖。
- 可移植 CGA 渲染器需内嵌字体位图，文本模式与图形模式均需覆盖；渲染结果需与现有 Win32 渲染对照。
- Qt 前端若选用 Qt Quick/QML，会增加渲染后端依赖（GPU），弱机需软渲染兜底。
- 跨平台 CI 中 Windows 前端若用 Qt，需在 `windows-latest` 安装 Qt，较 Linux 交叉编译更重。
- ABI 版本化与扩展登记点为长期演进预留，本阶段不强制全部实现。
- 发行包需按各发行版校验动态链接依赖（Qt6 版本、glibc）；Arch 需在 CI 容器内用 `makepkg` 构建，增加流水线复杂度。
- 文本渲染清晰度取决于缩放方式：位图字体按 1:1（8x8）写入光栅时像素级清晰；放大到较大窗口时若用平滑插值会发糊，应默认最近邻整数倍放大或 CRT 扫描线。若追求大尺寸文字更清晰，可另提供 8x16（VGA 风）文本路径，但会改变与 640x200 光栅的映射。
- CPU 指令可视化若开启追踪并逐拍推进，数据量大。当前模拟在 GUI 线程内推进，与现有 Win32 启动器一致；若单拍数据量导致界面卡顿，可考虑用工作线程推进、GUI 线程仅负责渲染与交互，但需保持引擎单一线程（引擎本身无线程）。
- 感官外观效果全部排除在外，后续单独讨论，不影响本架构。

---

## 13. 不在本大纲范围的事项（明确排除）

- 所有视觉外观效果、皮肤、配色、字体、动效、玻璃拟态、像素艺术风、游戏/二次元风格、任何现代或复古 UI 观感。
- CPU 指令行为、外设行为正确性、BIOS 兼容性的修正。
- 具体前端 UI 布局、控件尺寸、图标设计。
- 具体打包美化、图标、安装向导。
- 具体着色器实现细节（仅保留接口与数据来源，不涉及外观）。

---

## 14. 术语表

- 前端（Frontend）：图形界面宿主。
- 引擎（Engine）：模拟器核心，纯 C，零平台头文件。
- 前端 API（Frontend API）：引擎与 GUI 之间的稳定 C 接口，前缀 `sim_fe_`。
- 前端控制器（SimFrontend）：前端 API 的不透明句柄。
- 值快照（Snapshot）：跨边界拷贝出的只读状态结构体。
- ABI：二进制接口约定，用于前端与引擎独立演进时的相互校验。
- CGA 帧：由可移植渲染器产出的像素/文本表示，与 GUI 库无关。
- 追踪（Trace）：每拍的请求/响应/状态变化记录，见 `sim_trace.h`。

---

## 附录 A：参考的现有符号清单

- `SimSystem`：`include/sim/sim_system.h`
- `sim_system_init/destroy/reset/tick/publish_bios_configuration`：`src/sim_system.c`
- `SimRamConfig`、`sim_ram_peek_byte`、`sim_ram_load_bytes`：`include/sim/sim_ram.h`
- `SimRom`、`sim_rom_load_file`：`include/sim/sim_rom.h`
- `SimBusRequest`、`SimBusResponse`、`SimBusTarget`、`SimBusPhase`：`include/sim/sim_bus.h`
- `SimKernel`、`SimModule`、`sim_kernel_attach_module`：`include/sim/sim_kernel.h`
- `SimState`、`SimStateRegion`、`sim_state_add_region`：`include/sim/sim_state.h`
- `SimTrace`、`SimTraceRecord`、`SimTraceStateChange`：`include/sim/sim_trace.h`
- `SimCgaState`、`sim_cga_current_state`：`include/sim/sim_cga.h`
- `SimKeyboard`、`sim_keyboard_enqueue_scancode`：`include/sim/sim_keyboard.h`
- `SimDisk`、`sim_disk_mount_floppy_file`、`sim_disk_mount_file`、`sim_disk_create_file`、`sim_disk_reload_floppy_file`、`sim_disk_eject_media`、`sim_disk_flush_file`：`include/sim/sim_disk.h`
- `Cpu8086State`、`Cpu8086EuPhase`、`Cpu8086Instruction`、`Cpu8086BiuTransferPhase`、`Cpu8086BiuBusKind`、`Cpu8086Fault`：`include/sim/cpu8086_state.h`
- `Cpu8086`、`cpu8086_current_state`：`include/sim/cpu8086.h`
- 现有前端（将被替换/隔离）：`apps/pc_sim_launcher.c`
- 现有渲染宿主（将被替换）：`src/sim_cga_console.c`、`include/sim/sim_cga_console.h`
- 构建系统：`Makefile`、`.github/workflows/release.yml`
