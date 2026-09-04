# 8086-CPU-Sim

## 项目目的

本项目使用 C 语言，在寄存器级别模拟一颗 8086 CPU，并为它提供可以运行 DOS 的最小 PC 环境。

CPU 的实现是本项目的核心。CPU 部分几乎完整描述了 8086 的寄存器、指令执行所需的逻辑单元和内部控制关系，并模拟取指、译码、执行、总线访问、BIU/EU 协作以及相关时序行为。代码用于展示一条指令如何经过数据通路和控制逻辑，最终改变寄存器、Flags、内存或外设状态。

项目主要用于学习计算机组成原理。阅读和调试这些模块，可以把寄存器、ALU、总线、状态机、中断和存储器访问放到同一个可运行系统中观察。掌握这套结构后，可以继续在现有框架上尝试实现更复杂的处理器结构，例如超标量发射、流水线冒险处理、乱序执行、寄存器重命名和提交阶段。

<img width="1094" height="664" alt="DOS安装" src="https://github.com/user-attachments/assets/e7c054c7-13c5-400a-91cb-1055931a81d5" />

<img width="1264" height="660" alt="Q-BASIC解释器的运行" src="https://github.com/user-attachments/assets/54f17c69-fb13-4f1d-b5b1-0f8d32984009" />

## 项目边界

8086 CPU 的实现是重中之重。CPU 使用独立的状态区保存寄存器和内部控制状态，通过逻辑周期推进状态变化，并保留总线事务的 T1、T2、T3、WAIT、T4 阶段。

其他模块承担运行环境的职责，采用“能用则行”的行为级模型，目标是满足当前 CPU、BIOS 和 DOS 启动流程的接口要求。RAM、BUS、PIC、PIT、CGA、键盘、DMA 和磁盘控制器都可以替换。使用者可以寻找更精良的外设实现，将它们挂接到现有的总线和状态接口上。

BIOS 位于 `firmware/`。它是项目自制的 BIOS，几乎只能完成一个 DOS 安装软盘的引导和安装流程。它只兼容仓库中已有的 CPU、RAM、CGA、键盘、PIC、PIT、DMA 和磁盘控制器，没有实现对更多真实 PC 外设的兼容，不能当作通用 BIOS 使用。

Launcher 位于 `apps/`，目前属于半成品。它提供 CGA 文本显示、键盘输入、BIOS/软盘/硬盘选择和启动控制，方便观察启动流程。它不具备模拟真实微机系统并提供精准时钟节拍的能力，也不尝试精确维持约 4.77 MHz 的运行速度。Launcher 在 Windows 窗口事务中推进模拟器：默认每次 GUI 轮询推进 2048 个 ticks，速度选项通过调整每次批量推进的 ticks 数量实现。

## 系统架构

```text
Launcher
    |
    v
SimSystem
    |
    +-- SimKernel ---- SimState / SimTrace
    |
    +-- CPU8086
    |       +-- CPU8086State
    |       +-- BIU / Prefetch
    |       +-- EU / Decoder / EA
    |       +-- ALU / Shifter / MulDiv / BCD
    |       +-- Interrupt and control logic
    |
    +-- SimBus
    |       +-- SimRam / SimRom
    |       +-- SimCga
    |       +-- SimKeyboard
    |       +-- SimPic / SimPit
    |       +-- SimDma
    |       +-- SimDisk
    |
    +-- Firmware ROM and DOS disk images
```

每个逻辑周期遵循以下顺序：

```text
读取 current 状态
    -> 模块求值并产生请求、响应和 next 状态
    -> BUS 推进当前事务阶段
    -> T4 提交总线目标的写入副作用
    -> rising edge / commit
    -> 进入下一周期的 current 状态
```

模块通过 `SimModule` 挂接到 `SimKernel`，总线设备通过 `SimBusTarget` 注册到 `SimBus`。公共状态由 `SimState` 的状态区域保存。模块在求值阶段读取当前状态，在提交阶段写入下一状态，避免模块调用顺序改变结果。

## 目录结构

```text
8086-CPU-Sim/
|-- apps/       启动器：Windows（Win32 图形界面）/ Linux、macOS（控制台 stub）
|-- docs/       内核、CPU、总线和外设的设计说明
|-- firmware/   自制 BIOS 源码、目标文件和 ROM 镜像
|-- include/    C 头文件和模块接口
|-- src/        CPU、内核、总线、内存和外设实现
|-- build/      构建中间目标文件（.o，可删除，已 gitignore）
|-- dist/       构建产物（启动器、BIOS 镜像）
|-- Makefile    跨平台构建入口（自动检测宿主平台）
|-- README.md   项目说明
```

CPU 源文件主要包括：

```text
src/cpu8086.c
src/cpu8086_state.c
src/cpu8086_biu.c
src/cpu8086_prefetch.c
src/cpu8086_eu.c
src/cpu8086_decoder.c
src/cpu8086_ea.c
src/cpu8086_alu.c
src/cpu8086_shifter.c
src/cpu8086_muldiv.c
src/cpu8086_bcd.c
```

内核和平台设备主要包括：

```text
src/sim_state.c       状态区域和 current/next 状态
src/sim_kernel.c      逻辑周期和统一提交
src/sim_trace.c       周期、事务和状态变化记录
src/sim_bus.c         总线仲裁、阶段推进和设备访问
src/sim_ram.c         RAM 行为模型
src/sim_rom.c         ROM 行为模型
src/sim_pic.c         8259A 行为模型
src/sim_pit.c         8253 行为模型
src/sim_cga.c         CGA 寄存器和显存模型
src/sim_cga_console.c Windows CGA 显示窗口
src/sim_keyboard.c    8042 风格键盘控制器
src/sim_dma.c         8237 风格 DMA 控制器
src/sim_disk.c        软盘和硬盘扇区访问
src/sim_system.c      整机挂载和启动配置
```

## 构建

项目已支持跨平台构建。Makefile 会自动检测宿主平台（Windows / Linux / macOS），并选用对应的编译器、可执行后缀（`Windows` 用 `.exe`）、GUI 链接库与文件操作方式。

前置依赖：`make`（GNU Make）、C 编译器（GCC / Clang / MinGW），以及 binutils 的 `as`、`objcopy`（用于把 `firmware/` 里的 `.S` 汇编成 BIOS 镜像；各平台通用）。

### Linux / macOS 原生构建

在仓库根目录执行：

```text
make
```

### Windows 原生构建（MinGW）

Windows 上使用 MinGW 自带的 make 执行：

```text
mingw32-make
# 或 make（若已在 PATH 中）
```

### 在 Linux 上交叉编译 Windows 版

本机装有 MinGW 交叉编译器时，指定 `TARGET` 与 `CC` 即可产出 Windows `.exe`：

```text
make TARGET=windows CC=i686-w64-mingw32-gcc
```

`TARGET` 取值：`auto`（默认，自动检测）| `windows` | `unix`。所有工具（`CC` / `AS` / `OBJCOPY`）都可在命令行覆盖。

### 产物

构建后在 `build/` 生成中间目标文件，并产出：

```text
apps/pc_sim_launcher(.exe)        启动器
firmware/pc_compat_bios.bin       BIOS ROM 镜像
dist/apps/pc_sim_launcher(.exe)   打包后的启动器
dist/firmware/pc_compat_bios.bin  打包后的 BIOS 镜像
```

### 只做语法/模块验证与清理

```text
gcc -std=c11 -Wall -Wextra -Wpedantic -Iinclude -c src/*.c
make clean
```

> 平台说明：核心 CPU、内核、外设代码均使用 `#ifdef _WIN32` + POSIX 分支，可在各平台编译运行。Windows 下的 `apps/` 是 Win32 图形界面；Linux / macOS 下 Win32 API 不可用，`apps/` 会编译成一个仅打印「仅在 Windows 可用」的控制台 stub。一个真正的跨平台图形启动器（GTK / SDL 等）属于后续工作。

具体模块的接口、行为和已知限制见 `docs/`。其中 `docs/01-simulation-kernel.md` 说明时钟和提交模型，`docs/06-cpu8086.md` 说明 CPU 结构，其他文档分别说明 RAM/BUS、PIC、PIT、CGA、键盘、DMA、磁盘和 BIOS。

## 运行

`dist/` 目录内的文件为可运行的构建产物（`make` 后自动生成），可以直接启动。

构建完成后按平台启动：

- Windows：`dist\apps\pc_sim_launcher.exe`
- Linux / macOS：`./dist/apps/pc_sim_launcher`（当前为控制台 stub，仅提示「仅在 Windows 可用」）

图形启动器中选择 BIOS ROM、启动软盘镜像和可写硬盘镜像后即可观察启动流程。磁盘文件使用原始扇区镜像格式，具体容量和挂载方式以 Launcher 当前支持范围为准。

项目当前的可运行范围取决于 BIOS、DOS 镜像和各行为级设备模型之间的配合。CPU 模块适合继续进行指令级和微结构级学习，整机外设部分适合进行接口替换、兼容性验证和时序实验。
