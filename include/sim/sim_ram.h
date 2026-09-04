#ifndef SIM_SIM_RAM_H
#define SIM_SIM_RAM_H

#include "sim_bus.h"

typedef struct {
    uint32_t physical_base;
    size_t capacity_bytes;
    uint32_t read_wait_states;
    uint32_t write_wait_states;
    uint8_t initial_value;
} SimRamConfig;

/* RAM owns bytes; BUS owns the active transaction and its phase. */
typedef struct {
    SimRamConfig config;
    uint8_t *bytes;
} SimRam;

bool sim_ram_init(SimRam *ram, const SimRamConfig *config);
void sim_ram_destroy(SimRam *ram);

/* This creates a BUS target; it never exposes RAM storage to a CPU module. */
SimBusTarget sim_ram_bus_target(SimRam *ram, const char *name, uint32_t target_id);

/* Read-only inspection for tests, debugger tooling, and trace validation. */
bool sim_ram_peek_byte(const SimRam *ram, uint32_t physical_address, uint8_t *value);

/* Host-side setup only: load firmware/test bytes before simulated execution. */
bool sim_ram_load_bytes(SimRam *ram,
                        uint32_t physical_address,
                        const uint8_t *source,
                        size_t size);

#endif
