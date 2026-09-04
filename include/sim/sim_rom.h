#ifndef SIM_SIM_ROM_H
#define SIM_SIM_ROM_H

#include "sim_bus.h"

#define SIM_ROM_BASE 0xF0000u
#define SIM_ROM_SIZE 0x10000u

typedef struct {
    uint8_t bytes[SIM_ROM_SIZE];
} SimRom;

void sim_rom_init(SimRom *rom, uint8_t fill_value);
bool sim_rom_load_bytes(SimRom *rom, uint32_t physical_address,
                        const uint8_t *source, size_t size);
bool sim_rom_load_file(SimRom *rom, const char *path);
SimBusTarget sim_rom_bus_target(SimRom *rom, const char *name, uint32_t target_id);

#endif
