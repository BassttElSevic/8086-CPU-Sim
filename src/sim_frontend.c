#include "sim/sim_frontend.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sim/sim_system.h"
#include "sim/sim_cga.h"
#include "sim/sim_disk.h"
#include "sim/sim_keyboard.h"
#include "sim/sim_rom.h"
#include "sim/cpu8086.h"

/* Magic address used by the BIOS to publish INT 13h diagnostics to RAM. */
#define FE_INT13_BASE 0x4E0u

struct SimFrontend {
    SimSystem system;
    SimFeConfig config;
    /* Owned copies of the input paths so reset() can reuse them. */
    char *bios_path;
    char *floppy_path;
    char *hdd_path;
    bool ready;              /* system initialized. */
    bool running;            /* simulation advancing. */
    bool hdd_needs_flush;    /* a writable fixed disk is mounted. */
    uint32_t speed_multiplier;
    SimCgaRenderFrame frame;
    uint16_t last_int13_sequence;
    uint8_t int13_result;
    uint8_t int13_ah;
    uint8_t int13_al;
    uint8_t int13_ch;
    uint8_t int13_cl;
    uint8_t int13_dh;
    uint8_t int13_dl;
    char error[256];
};

uint32_t sim_fe_abi_version(void)
{
    return UINT32_C(1);
}

static void fe_set_error(SimFrontend *fe, const char *message)
{
    if (fe != NULL && message != NULL) {
        (void)snprintf(fe->error, sizeof(fe->error), "%s", message);
    }
}

static char *fe_strdup(const char *source)
{
    size_t length;
    char *copy;

    if (source == NULL) return NULL;
    length = strlen(source);
    copy = (char *)malloc(length + 1u);
    if (copy == NULL) return NULL;
    (void)memcpy(copy, source, length + 1u);
    return copy;
}

static SimRamConfig fe_default_ram(void)
{
    SimRamConfig ram = {0u, 0x100000u, 0u, 0u, 0u};
    return ram;
}

static SimRamConfig fe_resolve_ram(const SimFeConfig *config)
{
    SimRamConfig ram = config->ram_config;
    if (ram.capacity_bytes == 0u) {
        ram = fe_default_ram();
    }
    return ram;
}

static void fe_destroy_engine(SimFrontend *fe)
{
    if (fe == NULL) return;
    if (fe->ready) {
        if (fe->hdd_needs_flush && fe->hdd_path != NULL) {
            (void)sim_disk_flush_file(&fe->system.disk, 1u, fe->hdd_path);
        }
        sim_system_destroy(&fe->system);
        fe->ready = false;
    }
    fe->hdd_needs_flush = false;
    fe->running = false;
}

static bool fe_read_word(const SimFrontend *fe, uint32_t address, uint16_t *value)
{
    uint8_t low;
    uint8_t high;

    if (fe == NULL || value == NULL || !fe->ready) return false;
    if (!sim_ram_peek_byte(&fe->system.ram, address, &low) ||
        !sim_ram_peek_byte(&fe->system.ram, address + 1u, &high)) {
        return false;
    }
    *value = (uint16_t)((uint16_t)low | ((uint16_t)high << 8u));
    return true;
}

static void fe_update_int13(SimFrontend *fe)
{
    uint16_t sequence;
    uint16_t ax;
    uint16_t cx;
    uint16_t dx;
    uint8_t result;

    if (fe == NULL || !fe->ready || !fe->running) return;
    if (!fe_read_word(fe, FE_INT13_BASE, &sequence)) {
        fe->int13_result = 0xFFu;
        return;
    }
    if (sequence == 0u || sequence == fe->last_int13_sequence) return;
    if (!fe_read_word(fe, FE_INT13_BASE + 2u, &ax) ||
        !fe_read_word(fe, FE_INT13_BASE + 6u, &cx) ||
        !fe_read_word(fe, FE_INT13_BASE + 8u, &dx) ||
        !sim_ram_peek_byte(&fe->system.ram, FE_INT13_BASE + 10u, &result)) {
        return;
    }
    fe->last_int13_sequence = sequence;
    fe->int13_result = result;
    fe->int13_ah = (uint8_t)(ax >> 8u);
    fe->int13_al = (uint8_t)(ax & 0xFFu);
    fe->int13_ch = (uint8_t)(cx >> 8u);
    fe->int13_cl = (uint8_t)(cx & 0x3Fu);
    fe->int13_dh = (uint8_t)(dx >> 8u);
    fe->int13_dl = (uint8_t)(dx & 0xFFu);
}

static bool fe_mount(SimFrontend *fe)
{
    SimRamConfig ram = fe_resolve_ram(&fe->config);
    uint16_t cylinders = fe->config.hdd_cylinders != 0u ? fe->config.hdd_cylinders : 256u;
    uint8_t heads = fe->config.hdd_heads != 0u ? fe->config.hdd_heads : 16u;
    uint8_t sectors = fe->config.hdd_sectors != 0u ? fe->config.hdd_sectors : 63u;
    bool floppy_present = fe->floppy_path != NULL && fe->floppy_path[0] != '\0';
    bool hdd_present = fe->hdd_path != NULL && fe->hdd_path[0] != '\0';

    if (!sim_system_init(&fe->system, &ram)) {
        fe_set_error(fe, "sim_system_init failed");
        fe->ready = false;
        return false;
    }
    fe->ready = true;
    sim_trace_set_enabled(&fe->system.trace, fe->config.trace_enabled);

    if (fe->bios_path == NULL ||
        !sim_rom_load_file(&fe->system.rom, fe->bios_path)) {
        fe_set_error(fe, "Failed to load BIOS ROM");
        fe_destroy_engine(fe);
        return false;
    }
    if (floppy_present &&
        !sim_disk_mount_floppy_file(&fe->system.disk, 0u, fe->floppy_path, false)) {
        fe_set_error(fe, "Failed to mount floppy image");
        fe_destroy_engine(fe);
        return false;
    }
    if (hdd_present) {
        if (fe->config.create_hdd) {
            if (!sim_disk_create_file(fe->hdd_path, cylinders, heads, sectors)) {
                fe_set_error(fe, "Failed to create fixed disk image");
                fe_destroy_engine(fe);
                return false;
            }
        }
        if (!sim_disk_mount_file(&fe->system.disk, 1u, fe->hdd_path,
                                 cylinders, heads, sectors, true)) {
            fe_set_error(fe, "Failed to mount fixed disk image");
            fe_destroy_engine(fe);
            return false;
        }
        fe->hdd_needs_flush = true;
    }
    if (!sim_system_publish_bios_configuration(&fe->system)) {
        fe_set_error(fe, "Failed to publish BIOS media configuration");
        fe_destroy_engine(fe);
        return false;
    }
    fe->running = floppy_present || hdd_present;
    return true;
}

SimFrontend *sim_fe_create(const SimFeConfig *config)
{
    SimFrontend *fe;

    if (config == NULL) return NULL;
    fe = (SimFrontend *)calloc(1u, sizeof(*fe));
    if (fe == NULL) return NULL;

    fe->config = *config;
    fe->bios_path = fe_strdup(config->bios_path);
    fe->floppy_path = fe_strdup(config->floppy_path);
    fe->hdd_path = fe_strdup(config->hdd_path);
    fe->speed_multiplier = config->speed_multiplier != 0u ? config->speed_multiplier : 1u;
    fe->int13_result = 0xFFu;

    if (!fe_mount(fe)) {
        SimFrontend *dead = fe;
        fe = NULL;
        free(dead->bios_path);
        free(dead->floppy_path);
        free(dead->hdd_path);
        sim_system_destroy(&dead->system);
        free(dead);
        return NULL;
    }
    return fe;
}

void sim_fe_destroy(SimFrontend *fe)
{
    if (fe == NULL) return;
    fe_destroy_engine(fe);
    free(fe->bios_path);
    free(fe->floppy_path);
    free(fe->hdd_path);
    free(fe);
}

bool sim_fe_reset(SimFrontend *fe)
{
    if (fe == NULL) return false;
    fe->running = false;
    fe->last_int13_sequence = 0u;
    fe->int13_result = 0xFFu;
    fe_destroy_engine(fe);
    if (!fe_mount(fe)) {
        /* Keep a non-running controller so the frontend can read the error. */
        return false;
    }
    return true;
}

SimFeRunResult sim_fe_run_slice(SimFrontend *fe, uint32_t ticks)
{
    const Cpu8086State *cpu;
    uint32_t cycles;

    if (fe == NULL || !fe->ready || !fe->running) return SIM_FE_SIM_ERROR;
    cycles = ticks * (fe->speed_multiplier != 0u ? fe->speed_multiplier : 1u);
    for (uint32_t i = 0u; i < cycles; ++i) {
        if (!sim_system_tick(&fe->system)) {
            fe_set_error(fe, "Simulation kernel error");
            fe->running = false;
            return SIM_FE_SIM_ERROR;
        }
        cpu = cpu8086_current_state(&fe->system.cpu, &fe->system.state);
        if (cpu != NULL && cpu->eu.phase == CPU8086_EU_FAULTED) {
            fe->running = false;
            fe_set_error(fe, "CPU faulted");
            return SIM_FE_CPU_FAULT;
        }
    }
    fe_update_int13(fe);
    return SIM_FE_OK;
}

bool sim_fe_step(SimFrontend *fe)
{
    const Cpu8086State *cpu;

    if (fe == NULL || !fe->ready || !fe->running) return false;
    if (!sim_system_tick(&fe->system)) {
        fe_set_error(fe, "Simulation kernel error");
        fe->running = false;
        return false;
    }
    cpu = cpu8086_current_state(&fe->system.cpu, &fe->system.state);
    if (cpu != NULL && cpu->eu.phase == CPU8086_EU_FAULTED) {
        fe->running = false;
        fe_set_error(fe, "CPU faulted");
        return false;
    }
    fe_update_int13(fe);
    return true;
}

void sim_fe_set_speed(SimFrontend *fe, uint32_t multiplier)
{
    if (fe == NULL) return;
    fe->speed_multiplier = multiplier != 0u ? multiplier : 1u;
}

const SimCgaRenderFrame *sim_fe_frame(SimFrontend *fe)
{
    const SimCgaState *state;

    if (fe == NULL || !fe->ready) return NULL;
    state = sim_cga_current_state(&fe->system.cga, &fe->system.state);
    if (state == NULL) return NULL;
    sim_cga_render_frame(state, &fe->frame);
    return &fe->frame;
}

bool sim_fe_inject_scancode(SimFrontend *fe, uint8_t set1)
{
    if (fe == NULL || !fe->ready) return false;
    return sim_keyboard_enqueue_scancode(&fe->system.keyboard, set1);
}

bool sim_fe_cpu_snapshot(const SimFrontend *fe, SimFeCpuSnapshot *out)
{
    const Cpu8086State *cpu;

    if (fe == NULL || out == NULL || !fe->ready) return false;
    cpu = cpu8086_current_state(&fe->system.cpu, &fe->system.state);
    if (cpu == NULL) return false;

    (void)memcpy(out->reg, cpu->general, sizeof(out->reg));
    (void)memcpy(out->segment, cpu->segment, sizeof(out->segment));
    out->ip = cpu->ip;
    out->flags = cpu->flags;

    out->eu_phase = cpu->eu.phase;
    out->instruction = cpu->eu.instruction;
    out->opcode = cpu->eu.opcode;
    out->modrm = cpu->eu.modrm;
    out->micro_step = cpu->eu.micro_step;
    out->instruction_start_ip = cpu->eu.instruction_start_ip;

    out->prefetch_count = cpu->prefetch.count;
    if (out->prefetch_count > CPU8086_PREFETCH_CAPACITY) {
        out->prefetch_count = CPU8086_PREFETCH_CAPACITY;
    }
    (void)memcpy(out->prefetch, cpu->prefetch.bytes, out->prefetch_count);

    out->biu_phase = cpu->biu.data.phase;
    out->biu_kind = cpu->biu.data.kind;
    out->biu_write = cpu->biu.data.write;
    out->biu_address = cpu->biu.data.address;
    out->biu_data = cpu->biu.data.data;

    out->fault = (uint8_t)cpu->eu.fault;
    return true;
}

size_t sim_fe_trace_copy(const SimFrontend *fe, SimTraceRecord *out, size_t count)
{
    size_t n;

    if (fe == NULL || out == NULL || !fe->ready) return 0u;
    n = fe->system.trace.count < count ? fe->system.trace.count : count;
    if (n > 0u) {
        (void)memcpy(out, fe->system.trace.records, n * sizeof(SimTraceRecord));
    }
    return n;
}

void sim_fe_trace_clear(SimFrontend *fe)
{
    if (fe == NULL || !fe->ready) return;
    fe->system.trace.count = 0u;
    fe->system.trace.active_valid = false;
}

bool sim_fe_trace_enabled(const SimFrontend *fe)
{
    if (fe == NULL || !fe->ready) return false;
    return fe->system.trace.enabled;
}

void sim_fe_trace_set_enabled(SimFrontend *fe, bool enabled)
{
    if (fe == NULL || !fe->ready) return;
    sim_trace_set_enabled(&fe->system.trace, enabled);
}

bool sim_fe_memory_read(const SimFrontend *fe, uint32_t address,
                        uint8_t *buffer, size_t length)
{
    size_t i;

    if (fe == NULL || buffer == NULL || !fe->ready) return false;
    for (i = 0u; i < length; ++i) {
        if (!sim_ram_peek_byte(&fe->system.ram, address + (uint32_t)i, &buffer[i])) {
            return false;
        }
    }
    return true;
}

void sim_fe_status(const SimFrontend *fe, SimFeStatus *out)
{
    if (fe == NULL || out == NULL) return;
    out->floppy_present = fe->ready ? fe->system.disk.media[0].present : false;
    out->hdd_present = fe->ready ? fe->system.disk.media[1].present : false;
    out->running = fe->running;
    out->speed_multiplier = fe->speed_multiplier;
    out->int13_result = fe->int13_result;
    out->int13_ah = fe->int13_ah;
    out->int13_al = fe->int13_al;
    out->int13_ch = fe->int13_ch;
    out->int13_cl = fe->int13_cl;
    out->int13_dh = fe->int13_dh;
    out->int13_dl = fe->int13_dl;
}

static bool fe_require_ready(SimFrontend *fe)
{
    if (fe == NULL || !fe->ready) {
        if (fe != NULL) fe_set_error(fe, "Frontend is not ready");
        return false;
    }
    return true;
}

bool sim_fe_load_bios(SimFrontend *fe, const char *path)
{
    if (!fe_require_ready(fe) || path == NULL) return false;
    if (!sim_rom_load_file(&fe->system.rom, path)) {
        fe_set_error(fe, "Failed to load BIOS ROM");
        return false;
    }
    return true;
}

bool sim_fe_mount_floppy(SimFrontend *fe, const char *path)
{
    if (!fe_require_ready(fe) || path == NULL) return false;
    if (!sim_disk_mount_floppy_file(&fe->system.disk, 0u, path, false)) {
        fe_set_error(fe, "Failed to mount floppy image");
        return false;
    }
    if (!sim_system_publish_bios_configuration(&fe->system)) {
        fe_set_error(fe, "Failed to publish BIOS media configuration");
        return false;
    }
    return true;
}

bool sim_fe_reload_floppy(SimFrontend *fe)
{
    if (!fe_require_ready(fe) || fe->floppy_path == NULL) return false;
    if (!sim_disk_reload_floppy_file(&fe->system.disk, &fe->system.state, 0u,
                                     fe->floppy_path, false)) {
        fe_set_error(fe, "Failed to reload floppy image");
        return false;
    }
    if (!sim_system_publish_bios_configuration(&fe->system)) {
        fe_set_error(fe, "Failed to publish BIOS media configuration");
        return false;
    }
    return true;
}

bool sim_fe_eject_floppy(SimFrontend *fe)
{
    if (!fe_require_ready(fe)) return false;
    if (!sim_disk_eject_media(&fe->system.disk, &fe->system.state, 0u)) {
        fe_set_error(fe, "Failed to eject floppy image");
        return false;
    }
    if (!sim_system_publish_bios_configuration(&fe->system)) {
        fe_set_error(fe, "Failed to publish BIOS media configuration");
        return false;
    }
    return true;
}

bool sim_fe_mount_hdd(SimFrontend *fe, const char *path,
                      uint16_t cylinders, uint8_t heads, uint8_t sectors)
{
    if (!fe_require_ready(fe) || path == NULL) return false;
    if (!sim_disk_mount_file(&fe->system.disk, 1u, path,
                             cylinders, heads, sectors, true)) {
        fe_set_error(fe, "Failed to mount fixed disk image");
        return false;
    }
    fe->hdd_needs_flush = true;
    if (!sim_system_publish_bios_configuration(&fe->system)) {
        fe_set_error(fe, "Failed to publish BIOS media configuration");
        return false;
    }
    return true;
}

bool sim_fe_create_hdd(SimFrontend *fe, const char *path,
                       uint16_t cylinders, uint8_t heads, uint8_t sectors)
{
    if (!fe_require_ready(fe) || path == NULL) return false;
    if (!sim_disk_create_file(path, cylinders, heads, sectors)) {
        fe_set_error(fe, "Failed to create fixed disk image");
        return false;
    }
    return sim_fe_mount_hdd(fe, path, cylinders, heads, sectors);
}

bool sim_fe_eject_hdd(SimFrontend *fe)
{
    if (!fe_require_ready(fe)) return false;
    if (!sim_disk_eject_media(&fe->system.disk, &fe->system.state, 1u)) {
        fe_set_error(fe, "Failed to eject fixed disk image");
        return false;
    }
    fe->hdd_needs_flush = false;
    if (!sim_system_publish_bios_configuration(&fe->system)) {
        fe_set_error(fe, "Failed to publish BIOS media configuration");
        return false;
    }
    return true;
}

bool sim_fe_flush_hdd(SimFrontend *fe)
{
    const char *path;

    if (!fe_require_ready(fe)) return false;
    path = fe->hdd_path != NULL ? fe->hdd_path : NULL;
    (void)path;
    if (fe->hdd_path == NULL || !fe->hdd_needs_flush) return true;
    if (!sim_disk_flush_file(&fe->system.disk, 1u, fe->hdd_path)) {
        fe_set_error(fe, "Failed to flush fixed disk image");
        return false;
    }
    return true;
}

const char *sim_fe_last_error(const SimFrontend *fe)
{
    if (fe == NULL) return "invalid handle";
    return fe->error;
}
