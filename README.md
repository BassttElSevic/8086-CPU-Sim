# 8086-CPU-Sim

## 项目目的

本项目核心使用 C 语言，gui部分则使用C++的qt库制作而成。在寄存器级别模拟一颗 8086 CPU，并为它提供可以运行 DOS 的最小 PC 环境。

CPU 的实现是本项目的核心。CPU 部分几乎完整描述了 8086 的寄存器、指令执行所需的逻辑单元和内部控制关系，并模拟取指、译码、执行、总线访问、BIU/EU 协作以及相关时序行为。代码用于展示一条指令如何经过数据通路和控制逻辑，最终改变寄存器、Flags、内存或外设状态。

项目主要用于学习计算机组成原理。阅读和调试这些模块，可以把寄存器、ALU、总线、状态机、中断和存储器访问放到同一个可运行系统中观察。掌握这套结构后，可以继续在现有框架上尝试实现更复杂的处理器结构，例如超标量发射、流水线冒险处理、乱序执行、寄存器重命名和提交阶段。

实测可以完成ms-dos的下载，但是由于微软的版权©，很抱歉，我们没法做成三张软盘的.img也打包进入，请自己前往相应的地方下载。

先把第一张软盘的.img放进去，然后启动你的这个模拟器，接着等他在跑的时候，点击硬盘那里，点击一个new,这会创造一个.img文件作为这个假的C盘，后面模拟安系统的时候，就会一直整这个你自己新建的.img文件，放心不会整你真实的

安的时候看提示，如果提醒你要换“软盘的时候”就取出来，然后塞入第二张，然后点击reload,再enter,这个就可以“换盘”了。

现在相关的驱动以及啥还没有做，我们会在未来不久做完这些

~~copyleft万岁！！！~~

<img width="1911" height="1078" alt="图片" src="https://github.com/user-attachments/assets/6e44b0a6-b621-4b28-a4bb-691f2a910b73" />

<img width="1911" height="1078" alt="图片" src="https://github.com/user-attachments/assets/236a08ed-b6b0-4b48-955c-2859101ff099" />

<img width="1918" height="1077" alt="图片" src="https://github.com/user-attachments/assets/5f718d0c-4c2b-48fa-a763-4e8942ae0bdd" />

<img width="1911" height="1073" alt="图片" src="https://github.com/user-attachments/assets/b6642278-0d78-43b3-9fd2-c3e8e58ed2ba" />

<img width="1920" height="1080" alt="图片" src="https://github.com/user-attachments/assets/737b7ea1-c3d4-4583-aa38-839c165084e9" />


## 项目边界

8086 CPU 的实现是重中之重。CPU 使用独立的状态区保存寄存器和内部控制状态，通过逻辑周期推进状态变化，并保留总线事务的 T1、T2、T3、WAIT、T4 阶段。

其他模块承担运行环境的职责，采用“能用则行”的行为级模型，目标是满足当前 CPU、BIOS 和 DOS 启动流程的接口要求。RAM、BUS、PIC、PIT、CGA、键盘、DMA 和磁盘控制器都可以替换。使用者可以寻找更精良的外设实现，将它们挂接到现有的总线和状态接口上。

BIOS 位于 `firmware/`。它是项目自制的 BIOS，几乎只能完成一个 DOS 安装软盘的引导和安装流程。它只兼容仓库中已有的 CPU、RAM、CGA、键盘、PIC、PIT、DMA 和磁盘控制器，没有实现对更多真实 PC 外设的兼容，不能当作通用 BIOS 使用。

前端位于 `apps/`。当前默认前端是跨平台 Qt6 GUI（`apps/frontend-qt/`，仅通过前端 API `sim_frontend.h` 与引擎交互），提供 CGA 显示、键盘、BIOS/软盘/硬盘选择与启动控制。它不模拟真实微机系统精准时钟节拍，也不尝试精确维持约 4.77 MHz 速度；每次 GUI 轮询推进 2048 个 ticks，倍速通过调整批量推进量实现。旧的 Win32 前端（`apps/pc_sim_launcher.c` / `src/sim_cga_console.c`）保留用于参考，但不再由默认构建生成。

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
|-- apps/       前端：跨平台 Qt6 GUI（apps/frontend-qt/）；旧 Win32 启动器保留作参考
|-- docs/       内核、CPU、总线和外设的设计说明
|-- firmware/   自制 BIOS 源码、目标文件和 ROM 镜像
|-- include/    C 头文件和模块接口
|-- src/        CPU、内核、总线、内存和外设实现
|-- build/      构建中间目标文件（.o，可删除，已 gitignore）
|-- dist/       本地构建产物（启动器、BIOS 镜像；已 gitignore，不提交）
|-- .github/    GitHub Actions：打 tag 自动构建并发布 Release（开箱即用）
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

## 开箱即用（免编译）

不想装编译器？直接下载打包好的发行版即可 —— 解压/安装后就能跑，无需任何安装。文件名带版本号 + 日期（如 `pc-sim-1.0.0-20250904`）。

- **Windows**：下载 `pc-sim-<版本>-<日期>.win-x86_64.zip`，解压后双击里面的 `pc_sim_launcher.exe`。
- **Debian / Ubuntu**：下载 `pc-sim_<版本>+<日期>_amd64.deb`，用 `sudo apt install ./<file>.deb` 安装。
- **Red Hat / Fedora**：下载 `pc-sim-<版本>-<日期>.x86_64.rpm`，用 `sudo rpm -i <file>.rpm` 安装。
- **Linux 通用便携**：下载 `pc-sim-<版本>-<日期>.linux-x86_64.tar.gz`，解压后在终端执行 `./pc_sim_launcher`。

> 这些包由 GitHub Actions 在发布时自动构建（见 `.github/workflows/release.yml`），已内置 BIOS 镜像，**不需要编译、不需要配置**，打开即可用。请在仓库的 **Releases / 发行版** 页面下载。
>
> 跨平台 Qt6 前端替换了旧的 Win32 启动器，使 Linux / macOS / Windows 都能获得真实的图形界面；打包流水线仍在按平台补齐（见设计大纲第 7 章）。

如果你愿意自己编译，请继续看下面的「构建」章节。

## 构建

项目已支持跨平台构建。Makefile 会自动检测宿主平台（Windows / Linux / ~~macOS现在还没好~~），并选用对应的编译器、可执行后缀（`Windows` 用 `.exe`）、GUI 链接库与文件操作方式。

前置依赖：`make`（GNU Make）、C 编译器（GCC / Clang / MinGW），以及 binutils 的 `as`、`objcopy`（用于把 `firmware/` 里的 `.S` 汇编成 BIOS 镜像；各平台通用）。

构建 Qt 前端还需 Qt6（`Qt6Widgets`/`Qt6Gui`/`Qt6Core`，用 `pkg-config` 定位）。`FE` 变量控制前端：

- `FE=auto`（默认）：源码工具链齐全时构建 Qt 前端，否则只构建引擎库。
- `FE=qt`：强制构建 Qt 前端。
- `FE=none`：只构建引擎库 `build/libsim.a` + 固件（无 GUI）。

引擎库 `libsim.a` 为纯 C，已把旧的 Win32/GDI CGA 显示模块从引擎库中剔除（见 `src/sim_cga_console.c`，保留供参考），因此可在各平台编译。

### Linux  原生构建

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

### macOS 上，就，emmmm

私密马赛，我没macOS，这上面还有小问题，如果有人有点话，欢迎提交PR

qwq

### 在 Linux 上交叉编译 Windows 版

本机装有 MinGW 交叉编译器时，指定 `TARGET` 与 `CC` 即可产出 Windows `.exe`：

```text
make TARGET=windows CC=i686-w64-mingw32-gcc
```

`TARGET` 取值：`auto`（默认，自动检测）| `windows` | `unix`。所有工具（`CC` / `AS` / `OBJCOPY`）都可在命令行覆盖。

> 交叉编译（如 Linux → Windows 引擎库）默认只构建 `libsim`，不构建带 GUI 的前端；Qt 前端在各平台原生构建。

### 产物

构建后在 `build/` 生成中间目标文件与 `build/libsim.a`，并产出：

```text
apps/pc_sim_launcher(.exe)        前端（Qt6）
firmware/pc_compat_bios.bin       BIOS ROM 镜像
dist/apps/pc_sim_launcher(.exe)   打包后的前端
dist/firmware/pc_compat_bios.bin  打包后的 BIOS 镜像
```

### 只做语法/模块验证与清理

```text
gcc -std=c11 -Wall -Wextra -Wpedantic -Iinclude -c src/*.c
make clean
```

> 平台说明：核心引擎（CPU、内核、总线、外设）为纯 C，除 `sim_disk`/`sim_rom`/`sim_clock` 的文件 I/O 与计时胶水外不含平台头。前端为 Qt6（Core/Gui/Widgets），在 Linux / macOS / Windows 原生构建，不依赖 Win32 API。

具体模块的接口、行为和已知限制见 `docs/`。其中 `docs/01-simulation-kernel.md` 说明时钟和提交模型，`docs/06-cpu8086.md` 说明 CPU 结构，其他文档分别说明 RAM/BUS、PIC、PIT、CGA、键盘、DMA、磁盘和 BIOS。

## 运行

`dist/` 是本地构建产物目录（`make` 后自动生成，已 gitignore），**不随仓库提交**。作为最终用户，建议直接下载上方「开箱即用」章节里的 Release 包。

自己编译后按平台启动：

- Windows：`dist\apps\pc_sim_launcher.exe`（双击即可，BIOS 自动加载）
- Linux ：`./dist/apps/pc_sim_launcher`
- ~~macOS快了，快了（也许吧~~

命令行可传 `--bios`、`--floppy`、`--hdd`、`--create-hdd`，以及 `--run`（启动即用所选介质挂载运行）。

你搞gui也行

图形启动器中可选择 BIOS ROM、启动软盘镜像和可写硬盘镜像后观察启动流程。磁盘文件使用原始扇区镜像格式，具体容量和挂载方式以 Launcher 当前支持范围为准。

项目当前的可运行范围取决于 BIOS、DOS 镜像和各行为级设备模型之间的配合。CPU 模块适合继续进行指令级和微结构级学习，整机外设部分适合进行接口替换、兼容性验证和时序实验。
