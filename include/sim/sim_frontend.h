#ifndef SIM_SIM_FRONTEND_H
#define SIM_SIM_FRONTEND_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "sim_cga_render.h"
#include "sim_ram.h"
#include "sim_trace.h"
#include "cpu8086_state.h"

/*
 * Frontend controller.
 *
 * This is the single boundary between a GUI (Qt, SDL, terminal, test driver,
 * language binding) and the simulation engine.  The frontend never depends on
 * engine internals beyond what these functions return.  It owns the SimSystem
 * lifecycle, drives the simulation, and exposes value snapshots for display
 * and for the future CPU instruction-set visualisation.
 *
 * Threading: the engine is single-threaded.  All functions must be called from
 * one thread.  The frame/pointer returned by sim_fe_frame() stays valid until
 * the next call to sim_fe_run_slice()/sim_fe_step()/sim_fe_reset()/sim_fe_destroy()
 * on the same controller.
 */

/* Opaque handle. */
typedef struct SimFrontend SimFrontend;

/* ABI version for binary compatibility between frontend and engine. */
uint32_t sim_fe_abi_version(void);

/* ------------------------------------------------------------------ */
/* Configuration                                                       */
/* ------------------------------------------------------------------ */

typedef struct {
    /* Required BIOS ROM path (UTF-8). */
    const char *bios_path;
    /* Optional floppy and/or fixed disk image paths (UTF-8). */
    const char *floppy_path;
    const char *hdd_path;
    /* When true, hdd_path is created as a new zero-filled image. */
    bool create_hdd;
    /* Fixed-disk geometry; defaults 256/16/63 when zero. */
    uint16_t hdd_cylinders;
    uint8_t  hdd_heads;
    uint8_t  hdd_sectors;
    /* Ticks multiplier used by sim_fe_run_slice(); default 1. */
    uint32_t speed_multiplier;
    /* Enable the per-cycle trace recorder; default false. */
    bool trace_enabled;
    /* RAM config; when all fields are zero, use 1MB base 0. */
    SimRamConfig ram_config;
} SimFeConfig;

/* Result of a run slice. */
typedef enum {
    SIM_FE_OK = 0,
    SIM_FE_CPU_FAULT,   /* EU entered the FAULTED phase. */
    SIM_FE_SIM_ERROR    /* sim_system_tick returned false. */
} SimFeRunResult;

/* Structured, non-formatting status for the frontend to display. */
typedef struct {
    bool floppy_present;
    bool hdd_present;
    bool running;
    uint32_t speed_multiplier;
    uint8_t int13_result;   /* 0 success, 1 failure, 2 pending, 0xFF not updated. */
    uint8_t int13_ah;
    uint8_t int13_al;
    uint8_t int13_ch;
    uint8_t int13_cl;
    uint8_t int13_dh;
    uint8_t int13_dl;
} SimFeStatus;

/* CPU snapshot for the instruction-set visualisation. */
typedef struct {
    uint16_t reg[8];
    uint16_t segment[4];
    uint16_t ip;
    uint16_t flags;

    Cpu8086EuPhase eu_phase;
    Cpu8086Instruction instruction;
    uint8_t opcode;
    uint8_t modrm;
    uint8_t micro_step;
    uint16_t instruction_start_ip;

    uint8_t prefetch[CPU8086_PREFETCH_CAPACITY];
    unsigned prefetch_count;

    Cpu8086BiuTransferPhase biu_phase;
    Cpu8086BiuBusKind biu_kind;
    bool biu_write;
    uint32_t biu_address;
    uint16_t biu_data;

    uint8_t fault;
} SimFeCpuSnapshot;

/* ------------------------------------------------------------------ */
/* Lifecycle                                                           */
/* ------------------------------------------------------------------ */

/* Buildup an engine, mount the configured media, return a controller.
 * Returns NULL and records an error via sim_fe_last_error() on failure. */
SimFrontend *sim_fe_create(const SimFeConfig *config);

/* Destroy the controller and release the engine. Flushes a mounted,
 * writable fixed disk first, mirroring the existing Win32 launcher. */
void sim_fe_destroy(SimFrontend *fe);

/* Rebuild the engine from the stored config (equivalent to destroy+create). */
bool sim_fe_reset(SimFrontend *fe);

/* ------------------------------------------------------------------ */
/* Running                                                             */
/* ------------------------------------------------------------------ */

SimFeRunResult sim_fe_run_slice(SimFrontend *fe, uint32_t ticks);
bool sim_fe_step(SimFrontend *fe);
void sim_fe_set_speed(SimFrontend *fe, uint32_t multiplier);

/* ------------------------------------------------------------------ */
/* CGA frame                                                           */
/* ------------------------------------------------------------------ */

/* Current committed CGA frame, freshly rendered. Pointer owned by fe. */
const SimCgaRenderFrame *sim_fe_frame(SimFrontend *fe);

/* ------------------------------------------------------------------ */
/* Input                                                               */
/* ------------------------------------------------------------------ */

bool sim_fe_inject_scancode(SimFrontend *fe, uint8_t set1);

/* ------------------------------------------------------------------ */
/* CPU snapshot / trace / memory                                       */
/* ------------------------------------------------------------------ */

bool sim_fe_cpu_snapshot(const SimFrontend *fe, SimFeCpuSnapshot *out);
size_t sim_fe_trace_copy(const SimFrontend *fe, SimTraceRecord *out, size_t count);
void sim_fe_trace_clear(SimFrontend *fe);
bool sim_fe_trace_enabled(const SimFrontend *fe);
void sim_fe_trace_set_enabled(SimFrontend *fe, bool enabled);
bool sim_fe_memory_read(const SimFrontend *fe, uint32_t address,
                        uint8_t *buffer, size_t length);

/* ------------------------------------------------------------------ */
/* Status and media                                                    */
/* ------------------------------------------------------------------ */

void sim_fe_status(const SimFrontend *fe, SimFeStatus *out);

bool sim_fe_load_bios(SimFrontend *fe, const char *path);
bool sim_fe_mount_floppy(SimFrontend *fe, const char *path);
bool sim_fe_reload_floppy(SimFrontend *fe);
bool sim_fe_eject_floppy(SimFrontend *fe);
bool sim_fe_mount_hdd(SimFrontend *fe, const char *path,
                      uint16_t cylinders, uint8_t heads, uint8_t sectors);
bool sim_fe_create_hdd(SimFrontend *fe, const char *path,
                       uint16_t cylinders, uint8_t heads, uint8_t sectors);
bool sim_fe_eject_hdd(SimFrontend *fe);
bool sim_fe_flush_hdd(SimFrontend *fe);

/* Last error description; valid until the next failed call. */
const char *sim_fe_last_error(const SimFrontend *fe);

#endif
