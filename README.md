# 8086-PC-Sim

This repository contains a from-scratch, clocked C simulation of an 8086-based general-purpose PC. The long-term system target is to boot a legally obtained DOS disk image through a self-written BIOS. Existing emulators are not used as runtime components.

## Current milestone

The synchronous simulation kernel is complete and the assembled PC can boot a
FreeDOS floppy image through `firmware/pc_compat_bios.S`. The verified path is
the 8086 CPU, BUS, RAM/ROM, CGA text adapter, 8042-style keyboard controller,
8259 PIC, 8253 PIT, and ATA PIO disk controller. No external emulator is used
at runtime.

The foundation remains the kernel:

- deterministic logical clock;
- current-state/next-state register protocol;
- combinational 16-bit adder with 8086-relevant flags;
- clocked T1/T2/T3/WAIT/T4 BUS transaction state machine;
- BUS-mounted byte-addressed RAM with configurable wait states and T4 writes;
- no external C library dependencies beyond the standard C library.

Temporary test drivers and build products belong in the working directory used for development, not in this repository. The formal source is under `include/` and `src/`. The module dependency and data-flow diagrams are in `docs/02-module-map.md`. The formal C-file boundary plan is in `docs/03-formal-file-plan.md`. The State/Kernel/Trace/BUS interface contract is in `docs/04-core-interface-contract.md`. The RAM/BUS compatibility contract is in `docs/05-ram-bus-compatible-contract.md`.

The first interrupt-controller model is documented in
`docs/08-pic8259-behavior.md`. It is a single-chip behavioral 8259A mounted at
I/O ports `20h/21h` and connected to the CPU through two BUS INTA transactions.
The PIT model and its clock-ratio contract are documented in
`docs/09-pit8253-behavior.md`.

The BIOS contract and its FreeDOS boot acceptance evidence are in
`docs/13-pc-compatible-bios.md`.

## Build the current source

From the repository root:

```text
gcc -std=c11 -Wall -Wextra -Wpedantic -Iinclude -c src/sim_state.c src/sim_trace.c src/sim_kernel.c src/sim_bus.c src/sim_ram.c src/sim_pic.c src/sim_pit.c
```

The project is intentionally small enough that the simulation sources can still
be compiled directly, while the repository Makefile builds the BIOS and the
Windows launcher as stable executable boundaries.

## Run FreeDOS interactively

On Windows, build the system ROM and the graphical launcher from the repository
root:

```text
C:\MinGW\bin\mingw32-make.exe firmware launcher
```

Run the launcher:

```text
.\apps\pc_sim_launcher.exe
```

The window contains a CGA display and media controls. Select a BIOS ROM, use
`Browse...` for a raw 360 KB or 1.44 MB floppy image in A:, use `Browse...` or `New...`
for the writable C: raw disk, and press `Mount / Restart`. The launcher also
accepts the old command-line selections as initial values:

```text
.\apps\pc_sim_launcher.exe --floppy "D:\path\to\installer.img" --create-hdd "D:\path\to\dos-hdd.img"
```

After installation, reboot from the persistent hard disk without mounting a
floppy:

```text
.\apps\pc_sim_launcher.exe --hdd "D:\path\to\dos-hdd.img"
```

The C: `New...` action immediately creates a zero-filled 126 MiB `256 x 16 x 63`
raw image at the selected new path. It does not overwrite an existing file;
subsequent simulated writes are flushed back when the machine exits. To boot an installed disk without a
floppy, select only the C: image in the window, or use the command above.

The launcher renders the simulated text VRAM directly in the central native
80x25 CGA area and routes ordinary US-layout letters, digits, punctuation,
Enter, Backspace, Tab, Escape, and navigation keys through the simulated 8042
keyboard controller. Close the launcher window to stop the machine; a mounted
writable C: image is flushed back to its selected raw file at that point.

The disk files are raw sector images, not VHD/QCOW/VDI containers. A: accepts
standard 160 KiB, 180 KiB, 320 KiB, 360 KiB, 640 KiB, 720 KiB, 1.2 MiB,
1.44 MiB, and 2.88 MiB images, reads a valid FAT BPB for nonstandard media,
and accepts other 512-byte-aligned sizes that can be represented as two heads
with 1-63 sectors per track. C: must be exactly
`256 x 16 x 63 x 512` bytes (126 MiB).

DOS 5.0's stock `CONFIG.SYS` loads `HIMEM.SYS` and requests `DOS=HIGH`, which
requires an 80286-compatible HMA path. For this 8086 machine, use a derived
boot image with that driver disabled and `DOS=LOW` (the project test image is
`artifacts/Dos5.0_8086.img`). The original image is not modified.

## Simulation rule

Every cycle has two phases:

```text
evaluate current state -> commit all next state at the rising edge
```

Modules must not modify architectural state during evaluation. This rule is the foundation for the later 8086 data path, BIU/EU control, bus transactions, and peripherals. BUS targets may stage a response in T3/WAIT, but writes become visible only when the T4 transaction is finalized at the simulated rising edge.

The graphical launcher currently uses a fixed simulation batch for interactive
responsiveness: 1x executes 2,048 logical rising edges per GUI poll, while 2x,
4x, and 8x scale that batch. The independent `SimClock` remains available for
future host-time pacing, but is not used by the default launcher because the
current full-system tick cost cannot sustain a physical 4.77 MHz rate.
