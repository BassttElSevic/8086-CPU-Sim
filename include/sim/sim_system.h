#ifndef SIM_SIM_SYSTEM_H
#define SIM_SIM_SYSTEM_H

#include "sim_kernel.h"
#include "sim_ram.h"
#include "sim_rom.h"
#include "sim_pic.h"
#include "sim_pit.h"
#include "sim_cga.h"
#include "sim_disk.h"
#include "sim_keyboard.h"
#include "sim_dma.h"
#include "cpu8086.h"

/*
 * A deliberately small machine assembly fixture. It replaces the retired
 * direct-RAM test coordinator and demonstrates the permanent Kernel/BUS/RAM
 * wiring without pretending to be an 8086 CPU.
 */
typedef struct {
    SimState state;
    SimTrace trace;
    SimKernel kernel;
    SimBus bus;
    SimRam ram;
    SimRom rom;
    SimPic pic;
    SimPic pic_slave;
    SimPit pit;
    SimCga cga;
    SimDisk disk;
    SimKeyboard keyboard;
    SimDma dma;
    Cpu8086 cpu;
} SimSystem;

bool sim_system_init(SimSystem *system, const SimRamConfig *ram_config);
void sim_system_destroy(SimSystem *system);
void sim_system_reset(SimSystem *system);
/* Publish mounted floppy/fixed-disk geometry for the ROM before its first edge. */
bool sim_system_publish_bios_configuration(SimSystem *system);
bool sim_system_tick(SimSystem *system);

#endif
