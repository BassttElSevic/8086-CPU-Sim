# shellcheck disable=all
#       ^----- Makefile 不是 shell 脚本; shellcheck 无法解析 GNU make 的 ifneq/define 语法，
#                 会误报 SC1073/SC1065/SC1064/SC1072。此指令让 shellcheck 跳过本文件。
# =============================================================================
# 8086-CPU-Sim — 跨平台构建 (Windows / Linux / macOS)
#
# 三层架构：
#   engine (libsim, 纯 C)  ->  frontend API (sim_frontend.h)  ->  GUI
#
# 目标平台由 TARGET 决定：auto（默认，自动检测）| windows | unix
#   在 Windows 上运行 make  → 自动检测宿主平台, 构建引擎库 + (视 Qt 可用与否) 前端
#   在 Linux/macOS 上运行   → 同上
#   在 Linux 上交叉编译 Win 版： make TARGET=windows CC=i686-w64-mingw32-gcc
#      （此时默认只构建引擎库 libsim，不构建带 GUI 的前端）
#
# 前端由 FE 决定：auto（默认，自动检测）| none | qt
#   auto: 当宿主与目标平台一致且 pkg-config 能找到 Qt6Widgets 时选用 qt，否则 none。
#   none: 只构建引擎库 + 固件（无 GUI）。
#   qt:   构建 Qt6 Widgets 前端（apps/frontend-qt），产物复用启动器名。
#
# 所有工具（CC/AS/OBJCOPY/AR/STRIP/CXX）都可用命令行覆盖。
# =============================================================================

# ---------- 平台检测 ----------
HOST := unix
ifneq ($(OS),Windows_NT)
  uname_s := $(shell uname -s 2>/dev/null)
  ifneq ($(findstring MINGW,$(uname_s)),)
    HOST := windows
  else ifneq ($(findstring MSYS,$(uname_s)),)
    HOST := windows
  else ifneq ($(findstring CYGWIN,$(uname_s)),)
    HOST := windows
  endif
else
  HOST := windows
endif

TARGET ?= $(HOST)
ifeq ($(TARGET),auto)
  TARGET := $(HOST)
endif

# ---------- 按平台设定变量 ----------
ifeq ($(TARGET),windows)
  EXEEXT    := .exe
  GUI_FLAGS := -mwindows
  GUI_LIBS  := -lcomdlg32 -lshell32
  TEST_LIBS := -lgdi32 -luser32
  AS        ?= as
  OBJCOPY   ?= objcopy
else
  EXEEXT    :=
  GUI_FLAGS :=
  GUI_LIBS  :=
  TEST_LIBS :=
  AS        ?= as
  OBJCOPY   ?= objcopy
endif

# 编译器：Windows 原生 MinGW 默认 gcc；Linux/macOS 默认 cc。命令行或环境变量可覆盖。
ifeq ($(origin CC),default)
  CC := $(if $(filter windows,$(TARGET)),gcc,cc)
endif

# 优化级别可覆盖：make OPTIMIZE=-O3 或 -O0（调试）。默认 -O2（与原仓库一致，稳定）。
OPTIMIZE ?= -O2

CFLAGS   := -std=c11 $(OPTIMIZE) -Wall -Wextra -Wpedantic -Iinclude
CPPFLAGS :=

# ---------- 目录/产物 ----------
BUILD_DIR      := build
DIST_DIR       := dist
APP_NAME       := pc_sim_launcher$(EXEEXT)
LAUNCHER       := apps/$(APP_NAME)
DIST_LAUNCHER  := $(DIST_DIR)/apps/$(APP_NAME)
DIST_BIOS      := $(DIST_DIR)/firmware/pc_compat_bios.bin
BIOS_IMAGE     := firmware/pc_compat_bios.bin

# ---------- 引擎库 libsim（纯 C，零平台头，除文件/计时胶水） ----------
SIM_SOURCES     := $(wildcard src/*.c)
# 引擎库排除旧的 Win32/GDI 宿主模块（src/sim_cga_console.c）。
# 该模块由前端层（apps/frontend-qt 走 sim_cga_render）取代，不再属于引擎库。
ENGINE_SOURCES  := $(filter-out src/sim_cga_console.c,$(SIM_SOURCES))
ENGINE_OBJECTS  := $(patsubst src/%.c,$(BUILD_DIR)/%.o,$(ENGINE_SOURCES))
LIB_SIM         := $(BUILD_DIR)/libsim.a

AR ?= ar

# ---------- 前端选择 ----------
FE       ?= auto
QT_PKG   := Qt6Widgets

# 默认用 pkg-config 探测并取编译/链接参数；跨平台 CI（Windows/macOS 可能无 pkg-config）
# 可通过环境变量 QT_CFLAGS / QT_LIBS 显式覆盖。
QT_CFLAGS ?= $(shell pkg-config --cflags $(QT_PKG) 2>/dev/null)
QT_LIBS   ?= $(shell pkg-config --libs $(QT_PKG) 2>/dev/null) -pthread

# 判定 Qt 是否可用：pkg-config 给出参数，或外部用 QT_CFLAGS/QT_LIBS 显式提供。
HAVE_QT := $(if $(strip $(QT_CFLAGS) $(QT_LIBS)),yes,no)

ifeq ($(FE),auto)
  ifeq ($(TARGET),$(HOST))
    FE := $(if $(filter yes,$(HAVE_QT)),qt,none)
  else
    # 交叉编译：假定目标工具链没有宿主 Qt，只构建引擎库。
    FE := none
  endif
endif

# ---------- 跨平台文件操作 ----------
# 注意：这里的拷贝是「本机文件操作」，必须跟随 HOST（跑 make 的机器），而不是 TARGET。
ifeq ($(HOST),windows)
define MKDIR_P
powershell.exe -NoProfile -Command "New-Item -ItemType Directory -Force -Path '$(1)' | Out-Null"
endef
define COPY_TO_DIST
powershell.exe -NoProfile -Command "New-Item -ItemType Directory -Force -Path '$(1)' | Out-Null; Copy-Item -LiteralPath '$(2)' -Destination '$(3)' -Force"
endef
else
define MKDIR_P
mkdir -p $(1)
endef
define COPY_TO_DIST
mkdir -p $(1) && cp $(2) $(3)
endef
endif

.PHONY: all package launcher firmware libsim test clean

# ---------- 默认目标 ----------
all: package

# ---------- 引擎对象 ----------
$(BUILD_DIR)/%.o: src/%.c
	@$(call MKDIR_P,$(dir $@))
	$(CC) $(CFLAGS) $(CPPFLAGS) -c -o $@ $<

# ---------- 引擎静态库 ----------
$(LIB_SIM): $(ENGINE_OBJECTS)
	@$(call MKDIR_P,$(dir $@))
	$(AR) rcs $@ $(ENGINE_OBJECTS)

libsim: $(LIB_SIM)

# ---------- Qt 前端 ----------
QT_CXX       ?= g++
QT_CXXFLAGS  = -std=c++17 $(OPTIMIZE) -Wall -Wextra -Iinclude $(QT_CFLAGS)
QT_SOURCES   := $(wildcard apps/frontend-qt/*.cpp)
QT_OBJECTS   := $(patsubst apps/frontend-qt/%.cpp,$(BUILD_DIR)/qt/%.o,$(QT_SOURCES))
# 升级后的前端复用「启动器」这个产物名，作为跨平台 GUI 的默认形态。
QT_APP       := $(LAUNCHER)

$(BUILD_DIR)/qt/%.o: apps/frontend-qt/%.cpp
	@$(call MKDIR_P,$(dir $@))
	$(QT_CXX) $(QT_CXXFLAGS) -c -o $@ $<

$(QT_APP): $(QT_OBJECTS) $(LIB_SIM)
	@$(call MKDIR_P,$(dir $@))
	$(QT_CXX) $(QT_CXXFLAGS) -o $@ $(QT_OBJECTS) $(LIB_SIM) $(QT_LIBS)

# ---------- 前端产物（随 FE 变化） ----------
ifeq ($(FE),qt)
  FRONTEND := $(QT_APP)
else
  FRONTEND :=
endif

launcher: $(FRONTEND)

# ---------- firmware ----------
$(BIOS_IMAGE): firmware/Makefile firmware/*.S
	$(MAKE) -C firmware pc_compat_bios.bin AS=$(AS) OBJCOPY=$(OBJCOPY)

firmware: $(BIOS_IMAGE)

# ---------- 打包 ----------
$(DIST_BIOS): $(BIOS_IMAGE)
	$(call COPY_TO_DIST,$(dir $@),$<,$@)

$(DIST_LAUNCHER): $(FRONTEND)
	$(call COPY_TO_DIST,$(dir $@),$<,$@)

PACKAGE_DEPS := $(LIB_SIM) $(DIST_BIOS)
ifneq ($(FRONTEND),)
  PACKAGE_DEPS += $(DIST_LAUNCHER)
endif

package: $(PACKAGE_DEPS)
ifeq ($(FRONTEND),)
	@echo "FE=$(FE): engine library + firmware built (no GUI frontend)."
	@echo "  Install Qt6 (pkg-config Qt6Widgets) or run: make FE=qt"
endif

# ---------- 测试 ----------
# tests/ 目录当前不存在，且既有测试依赖 Windows GDI/User32，仅在 Windows 且源文件存在时构建。
TEST_SOURCES := $(wildcard tests/test_*.c)
ifeq ($(TARGET),windows)
  ifneq ($(strip $(TEST_SOURCES)),)
test: firmware $(patsubst tests/%.c,build/%.exe,$(TEST_SOURCES))
	$(foreach t,$(patsubst tests/%.c,build/%.exe,$(TEST_SOURCES)),$(t);)
  else
test:
	@echo "tests/ 目录不存在，跳过。"
  endif
else
test:
	@echo "测试仅支持 Windows（依赖 GDI/User32），当前平台为 unix，已跳过。"
endif

# ---------- 清理（同样跟随 HOST） ----------
ifeq ($(HOST),windows)
clean:
	powershell.exe -NoProfile -Command "Remove-Item -Recurse -Force '$(BUILD_DIR)','$(DIST_DIR)' -ErrorAction SilentlyContinue; Remove-Item -Force '$(LAUNCHER)' -ErrorAction SilentlyContinue; Get-ChildItem -Path 'firmware' -Filter *.o -File -ErrorAction SilentlyContinue | Remove-Item -Force"
else
clean:
	rm -rf $(BUILD_DIR) $(DIST_DIR) $(LAUNCHER) firmware/*.o
endif
