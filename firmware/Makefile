AS = C:\\MinGW\\bin\\as.exe
OBJCOPY = C:\\MinGW\\bin\\objcopy.exe

all: boot_bios.bin pc_compat_bios.bin

boot_bios.o: boot_bios.S
	$(AS) --32 -o $@ $<

boot_bios.bin: boot_bios.o
	$(OBJCOPY) -O binary $< $@

pc_compat_bios.o: pc_compat_bios.S
	$(AS) --32 -o $@ $<

pc_compat_bios.bin: pc_compat_bios.o
	$(OBJCOPY) -O binary $< $@
