CC = C:/MinGW/bin/gcc.exe
CFLAGS = -std=c11 -O2 -Wall -Wextra -Wpedantic -Iinclude
SIM_SOURCES = $(wildcard src/*.c)

.PHONY: launcher firmware test

launcher: firmware apps/pc_sim_launcher.exe

apps/pc_sim_launcher.exe: apps/pc_sim_launcher.c $(SIM_SOURCES)
	$(CC) $(CFLAGS) -mwindows -o $@ $^ -lcomdlg32 -lshell32

firmware:
	C:/MinGW/bin/mingw32-make.exe -C firmware pc_compat_bios.bin

test: firmware tests/test_cga.exe tests/test_bios_scroll.exe
	./tests/test_cga.exe
	./tests/test_bios_scroll.exe

tests/test_cga.exe: tests/test_cga.c $(SIM_SOURCES)
	$(CC) $(CFLAGS) -o $@ $^ -lgdi32 -luser32

tests/test_bios_scroll.exe: tests/test_bios_scroll.c $(SIM_SOURCES)
	$(CC) $(CFLAGS) -o $@ $^ -lgdi32 -luser32
