#ifndef SIM_SIM_DISK_H
#define SIM_SIM_DISK_H

#include "sim_bus.h"
#include "sim_kernel.h"

#define SIM_DISK_SECTOR_SIZE 512u
#define SIM_DISK_DRIVE_COUNT 2u

enum {
    SIM_DISK_STATUS_ERR = 0x01u,
    SIM_DISK_STATUS_DRQ = 0x08u,
    SIM_DISK_STATUS_DRDY = 0x40u,
    SIM_DISK_STATUS_BSY = 0x80u
};

typedef struct SimPic SimPic;

typedef struct {
    uint8_t *bytes;
    size_t size_bytes;
    uint16_t cylinders;
    uint8_t heads;
    uint8_t sectors_per_track;
    bool present;
    bool writable;
} SimDiskMedia;

typedef struct {
    uint8_t error;
    uint8_t feature;
    uint8_t sector_count;
    uint8_t sector_number;
    uint8_t cylinder_low;
    uint8_t cylinder_high;
    uint8_t drive_head;
    uint8_t command;
    uint8_t status;
    uint8_t pending_command;
    uint8_t selected_drive;
    uint16_t sectors_remaining;
    uint16_t transfer_index;
    uint32_t current_lba;
    uint32_t busy_cycles;
    bool write_transfer;
    bool irq_pending;
    bool irq_pulse;
    uint8_t data[SIM_DISK_SECTOR_SIZE];
} SimDiskState;

typedef struct {
    uint16_t io_base;
    uint16_t control_port;
    uint32_t command_latency_cycles;
    uint8_t irq_line;
    SimPic *pic;
    SimDiskMedia media[SIM_DISK_DRIVE_COUNT];
    SimStateRegionId state_region;
    bool attached;
} SimDisk;

void sim_disk_init(SimDisk *disk, SimPic *pic);
void sim_disk_destroy(SimDisk *disk);
void sim_disk_set_latency(SimDisk *disk, uint32_t cycles);

bool sim_disk_mount_bytes(SimDisk *disk, unsigned drive,
                          const uint8_t *source, size_t size_bytes,
                          uint16_t cylinders, uint8_t heads,
                          uint8_t sectors_per_track, bool writable);
bool sim_disk_create_media(SimDisk *disk, unsigned drive,
                           uint16_t cylinders, uint8_t heads,
                           uint8_t sectors_per_track, bool writable);
bool sim_disk_mount_file(SimDisk *disk, unsigned drive, const char *path,
                         uint16_t cylinders, uint8_t heads,
                         uint8_t sectors_per_track, bool writable);
/* Resolve a raw floppy image size to a usable CHS geometry. */
bool sim_disk_floppy_geometry_from_size(uint64_t size_bytes,
                                        uint16_t *cylinders, uint8_t *heads,
                                        uint8_t *sectors_per_track);
/* Prefer a valid FAT BPB, then use the raw image size resolver. */
bool sim_disk_detect_floppy_geometry(const char *path, uint16_t *cylinders,
                                     uint8_t *heads, uint8_t *sectors_per_track);
bool sim_disk_mount_floppy_file(SimDisk *disk, unsigned drive, const char *path,
                                bool writable);
/* Host UI operation between simulation edges: replace or eject a removable drive. */
bool sim_disk_reload_floppy_file(SimDisk *disk, SimState *state, unsigned drive,
                                 const char *path, bool writable);
bool sim_disk_eject_media(SimDisk *disk, SimState *state, unsigned drive);
/* Create a zero-filled raw image without replacing an existing file. */
bool sim_disk_create_file(const char *path, uint16_t cylinders, uint8_t heads,
                          uint8_t sectors_per_track);
bool sim_disk_flush_file(const SimDisk *disk, unsigned drive, const char *path);

SimBusTarget sim_disk_bus_target(SimDisk *disk, const char *name, uint32_t target_id);
bool sim_disk_attach(SimDisk *disk, SimKernel *kernel);
const SimDiskState *sim_disk_current_state(const SimDisk *disk, const SimState *state);

#endif
