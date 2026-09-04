# =============================================================================
# firmware 构建：把 .S 汇编成裸二进制 BIOS 镜像
# 使用 binutils 的 as + objcopy（Linux/macOS/Windows MinGW 都有，跨平台通用）。
# 默认用 PATH 里的 as/objcopy；可由上级 Makefile 传递 AS/OBJCOPY 覆盖。
# =============================================================================

AS      ?= as
OBJCOPY ?= objcopy
ASFLAGS ?= --32

.PHONY: all clean

all: boot_bios.bin pc_compat_bios.bin

boot_bios.o: boot_bios.S
	$(AS) $(ASFLAGS) -o $@ $<

boot_bios.bin: boot_bios.o
	$(OBJCOPY) -O binary $< $@

pc_compat_bios.o: pc_compat_bios.S
	$(AS) $(ASFLAGS) -o $@ $<

pc_compat_bios.bin: pc_compat_bios.o
	$(OBJCOPY) -O binary $< $@

clean:
	rm -f *.o *.bin
