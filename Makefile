# =============================================================================
# 8086-CPU-Sim — 跨平台构建 (Windows / Linux / macOS)
#
# 目标平台由 TARGET 决定：auto（默认，自动检测）| windows | unix
#   在 Windows 上运行 make  → 自动构建 Windows GUI 启动器 (.exe)
#   在 Linux/macOS 上运行   → 自动构建 Linux 回退启动器 + firmware
#   在 Linux 上交叉编译 Win 版： make TARGET=windows CC=i686-w64-mingw32-gcc
#
# 所有工具（CC/AS/OBJCOPY/AR/STRIP）都可用命令行覆盖。
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

# 编译器：Windows 原生 MinGW 默认 gcc；Linux/macOS 默认 cc。命令行可覆盖。
CC ?= $(if $(filter windows,$(TARGET)),gcc,cc)

CFLAGS   := -std=c11 -O2 -Wall -Wextra -Wpedantic -Iinclude
CPPFLAGS :=

# ---------- 目录/产物 ----------
BUILD_DIR      := build
DIST_DIR       := dist
APP_NAME       := pc_sim_launcher$(EXEEXT)
LAUNCHER       := apps/$(APP_NAME)
DIST_LAUNCHER  := $(DIST_DIR)/apps/$(APP_NAME)
DIST_BIOS      := $(DIST_DIR)/firmware/pc_compat_bios.bin
BIOS_IMAGE     := firmware/pc_compat_bios.bin

SIM_SOURCES := $(wildcard src/*.c)
SIM_OBJECTS := $(patsubst src/%.c,$(BUILD_DIR)/%.o,$(SIM_SOURCES))

# ---------- 跨平台拷贝（打包到 dist/） ----------
# 注意：这里的拷贝是「本机文件操作」，必须跟随 HOST（跑 make 的机器），而不是 TARGET。
# 在 Linux 上交叉编译 Windows 版时，HOST 是 Linux，不能用 powershell。
ifeq ($(HOST),windows)
define COPY_TO_DIST
powershell.exe -NoProfile -Command "New-Item -ItemType Directory -Force -Path '$(1)' | Out-Null; Copy-Item -LiteralPath '$(2)' -Destination '$(3)' -Force"
endef
else
define COPY_TO_DIST
mkdir -p $(1) && cp $(2) $(3)
endef
endif

.PHONY: all package launcher firmware test clean

# ---------- 默认目标 ----------
all: package

# ---------- 对象 ----------
$(BUILD_DIR)/%.o: src/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(CPPFLAGS) -c -o $@ $<

# ---------- 启动器 ----------
$(LAUNCHER): apps/pc_sim_launcher.c $(SIM_OBJECTS)
	$(CC) $(CFLAGS) $(CPPFLAGS) $(GUI_FLAGS) -o $@ $^ $(GUI_LIBS)

launcher: $(LAUNCHER) $(BIOS_IMAGE)

# ---------- firmware ----------
$(BIOS_IMAGE): firmware/Makefile firmware/*.S
	$(MAKE) -C firmware pc_compat_bios.bin AS=$(AS) OBJCOPY=$(OBJCOPY)

firmware: $(BIOS_IMAGE)

# ---------- 打包 ----------
package: $(DIST_LAUNCHER) $(DIST_BIOS)

$(DIST_LAUNCHER): $(LAUNCHER)
	$(call COPY_TO_DIST,$(dir $@),$<,$@)

$(DIST_BIOS): $(BIOS_IMAGE)
	$(call COPY_TO_DIST,$(dir $@),$<,$@)

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
