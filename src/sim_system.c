#include "sim/sim_system.h"

#include <string.h>

bool sim_system_init(SimSystem *system, const SimRamConfig *ram_config)
{
    SimBusTarget ram_target;
    SimBusTarget rom_target;
    SimBusTarget pic_target;
    SimBusTarget pic_slave_target;
    SimBusTarget pit_target;
    SimBusTarget cga_target;
    SimBusTarget disk_target;
    SimBusTarget keyboard_target;
    SimBusTarget dma_target;
    Cpu8086Config cpu_config = {0};

    if (system == NULL || ram_config == NULL) {
        return false;
    }

    memset(system, 0, sizeof(*system));
    sim_state_init(&system->state);
    if (!sim_trace_init(&system->trace, 0u)) {
        return false;
    }
    if (!sim_kernel_init(&system->kernel, &system->state, &system->trace)) {
        sim_trace_destroy(&system->trace);
        return false;
    }
    if (!sim_ram_init(&system->ram, ram_config)) {
        sim_kernel_destroy(&system->kernel);
        sim_trace_destroy(&system->trace);
        return false;
    }

    sim_bus_init(&system->bus);
    sim_rom_init(&system->rom, 0xFFu);
    sim_cga_init(&system->cga);
    sim_pic_init(&system->pic);
    sim_pic_init(&system->pic_slave);
    sim_dma_init(&system->dma);
    sim_pic_set_ports(&system->pic_slave, 0xA0u, 0xA1u);
    sim_pic_connect_slave(&system->pic, &system->pic_slave, 2u);
    cga_target = sim_cga_bus_target(&system->cga, "cga", 4u);
    if (!sim_bus_attach_target(&system->bus, &cga_target) ||
        !sim_cga_attach(&system->cga, &system->kernel)) {
        sim_ram_destroy(&system->ram);
        sim_kernel_destroy(&system->kernel);
        sim_trace_destroy(&system->trace);
        return false;
    }
    rom_target = sim_rom_bus_target(&system->rom, "rom", 6u);
    ram_target = sim_ram_bus_target(&system->ram, "ram", 0u);
    if (!sim_bus_attach_target(&system->bus, &rom_target) ||
        !sim_bus_attach_target(&system->bus, &ram_target)) {
        sim_ram_destroy(&system->ram);
        sim_kernel_destroy(&system->kernel);
        sim_trace_destroy(&system->trace);
        return false;
    }
    pic_target = sim_pic_bus_target(&system->pic, "pic8259", 1u);
    if (!sim_bus_attach_target(&system->bus, &pic_target) ||
        !sim_pic_attach(&system->pic, &system->kernel)) {
        sim_ram_destroy(&system->ram);
        sim_kernel_destroy(&system->kernel);
        sim_trace_destroy(&system->trace);
        return false;
    }
    pic_slave_target = sim_pic_bus_target(&system->pic_slave, "pic8259-slave", 3u);
    if (!sim_bus_attach_target(&system->bus, &pic_slave_target) ||
        !sim_pic_attach(&system->pic_slave, &system->kernel)) {
        sim_ram_destroy(&system->ram);
        sim_kernel_destroy(&system->kernel);
        sim_trace_destroy(&system->trace);
        return false;
    }
    sim_pit_init(&system->pit, &system->pic);
    pit_target = sim_pit_bus_target(&system->pit, "pit8253", 2u);
    if (!sim_bus_attach_target(&system->bus, &pit_target) ||
        !sim_pit_attach(&system->pit, &system->kernel)) {
        sim_ram_destroy(&system->ram);
        sim_kernel_destroy(&system->kernel);
        sim_trace_destroy(&system->trace);
        return false;
    }
    sim_disk_init(&system->disk, &system->pic);
    disk_target = sim_disk_bus_target(&system->disk, "ata-pio", 5u);
    if (!sim_bus_attach_target(&system->bus, &disk_target) ||
        !sim_disk_attach(&system->disk, &system->kernel)) {
        sim_disk_destroy(&system->disk);
        sim_ram_destroy(&system->ram);
        sim_kernel_destroy(&system->kernel);
        sim_trace_destroy(&system->trace);
        return false;
    }
    sim_keyboard_init(&system->keyboard, &system->pic);
    keyboard_target = sim_keyboard_bus_target(&system->keyboard, "keyboard8042", 7u);
    if (!sim_bus_attach_target(&system->bus, &keyboard_target) ||
        !sim_keyboard_attach(&system->keyboard, &system->kernel)) {
        sim_disk_destroy(&system->disk);
        sim_ram_destroy(&system->ram);
        sim_kernel_destroy(&system->kernel);
        sim_trace_destroy(&system->trace);
        return false;
    }
    dma_target = sim_dma_bus_target(&system->dma, "dma8237", 8u);
    if (!sim_bus_attach_target(&system->bus, &dma_target) ||
        !sim_dma_attach(&system->dma, &system->kernel)) {
        sim_disk_destroy(&system->disk);
        sim_ram_destroy(&system->ram);
        sim_kernel_destroy(&system->kernel);
        sim_trace_destroy(&system->trace);
        return false;
    }
    cpu_config.master_id = 1u;
    if (!cpu8086_init(&system->cpu, &cpu_config) ||
        !cpu8086_attach(&system->cpu, &system->kernel)) {
        sim_disk_destroy(&system->disk);
        sim_ram_destroy(&system->ram);
        sim_kernel_destroy(&system->kernel);
        sim_trace_destroy(&system->trace);
        return false;
    }

    sim_kernel_set_bus_resolver(&system->kernel,
                                sim_bus_resolve,
                                sim_bus_finalize,
                                &system->bus);
    sim_system_reset(system);
    return true;
}

void sim_system_destroy(SimSystem *system)
{
    if (system == NULL) {
        return;
    }

    sim_disk_destroy(&system->disk);
    sim_ram_destroy(&system->ram);
    sim_kernel_destroy(&system->kernel);
    sim_trace_destroy(&system->trace);
    sim_state_destroy(&system->state);
    memset(system, 0, sizeof(*system));
}

void sim_system_reset(SimSystem *system)
{
    if (system == NULL) {
        return;
    }

    sim_bus_reset(&system->bus);
    sim_kernel_reset(&system->kernel);
    sim_kernel_set_reset(&system->kernel, false);
}

bool sim_system_publish_bios_configuration(SimSystem *system)
{
    const SimDiskMedia *floppy;
    const SimDiskMedia *hard_disk;
    uint8_t configuration[14] = {0};
    uint32_t sectors;

    if (system == NULL) return false;
    floppy = &system->disk.media[0];
    hard_disk = &system->disk.media[1];
    if (floppy->present) {
        configuration[0] = 0x01u;
        configuration[2] = (uint8_t)floppy->cylinders;
        configuration[3] = (uint8_t)(floppy->cylinders >> 8u);
        configuration[4] = floppy->heads;
        configuration[5] = floppy->sectors_per_track;
    }
    if (hard_disk->present) {
        sectors = (uint32_t)(hard_disk->size_bytes / SIM_DISK_SECTOR_SIZE);
        configuration[0] |= 0x02u;
        configuration[6] = (uint8_t)hard_disk->cylinders;
        configuration[7] = (uint8_t)(hard_disk->cylinders >> 8u);
        configuration[8] = hard_disk->heads;
        configuration[9] = hard_disk->sectors_per_track;
        configuration[10] = (uint8_t)sectors;
        configuration[11] = (uint8_t)(sectors >> 8u);
        configuration[12] = (uint8_t)(sectors >> 16u);
        configuration[13] = (uint8_t)(sectors >> 24u);
    }
    return sim_ram_load_bytes(&system->ram, 0x490u, configuration,
                              sizeof(configuration));
}

bool sim_system_tick(SimSystem *system)
{
    return system != NULL && sim_kernel_tick(&system->kernel);
}
