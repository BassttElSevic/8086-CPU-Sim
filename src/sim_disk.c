#include "sim/sim_disk.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>

/* MinGW hides this Microsoft CRT extension when compiling strict C11. */
extern FILE *_wfopen(const wchar_t *path, const wchar_t *mode);
#endif

#include "sim/sim_pic.h"
#include "sim/sim_state.h"
#include "sim/sim_trace.h"

enum {
    ATA_DATA = 0u,
    ATA_ERROR_FEATURE = 1u,
    ATA_SECTOR_COUNT = 2u,
    ATA_SECTOR_NUMBER = 3u,
    ATA_CYLINDER_LOW = 4u,
    ATA_CYLINDER_HIGH = 5u,
    ATA_DRIVE_HEAD = 6u,
    ATA_STATUS_COMMAND = 7u,
    ATA_COMMAND_READ_SECTORS = 0x20u,
    ATA_COMMAND_WRITE_SECTORS = 0x30u,
    ATA_COMMAND_IDENTIFY = 0xECu,
    ATA_COMMAND_CACHE_FLUSH = 0xE7u,
    ATA_ERROR_ABRT = 0x04u
};

static uint8_t request_byte(const SimBusRequest *request)
{
    return request->byte_enable == SIM_BUS_BYTE_ENABLE_HIGH
        ? (uint8_t)(request->data >> 8u) : (uint8_t)request->data;
}

static uint16_t response_byte(const SimBusRequest *request, uint8_t value)
{
    return request->byte_enable == SIM_BUS_BYTE_ENABLE_HIGH
        ? (uint16_t)((uint16_t)value << 8u) : value;
}

static FILE *open_binary_file(const char *path, bool write)
{
#ifdef _WIN32
    int characters;
    wchar_t *wide_path;
    FILE *file;

    if (path == NULL) return NULL;
    characters = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, -1, NULL, 0);
    if (characters <= 0) return NULL;
    wide_path = malloc((size_t)characters * sizeof(*wide_path));
    if (wide_path == NULL) return NULL;
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, -1, wide_path, characters) <= 0) {
        free(wide_path);
        return NULL;
    }
    file = _wfopen(wide_path, write ? L"wb" : L"rb");
    free(wide_path);
    return file;
#else
    return path == NULL ? NULL : fopen(path, write ? "wb" : "rb");
#endif
}

static void media_clear(SimDiskMedia *media)
{
    if (media == NULL) return;
    free(media->bytes);
    memset(media, 0, sizeof(*media));
}

static bool geometry_is_valid(uint16_t cylinders, uint8_t heads, uint8_t sectors)
{
    return cylinders != 0u && heads != 0u && sectors != 0u;
}

static bool media_size(uint16_t cylinders, uint8_t heads, uint8_t sectors, size_t *size)
{
    uint64_t result;
    if (!geometry_is_valid(cylinders, heads, sectors) || size == NULL) return false;
    result = (uint64_t)cylinders * heads * sectors * SIM_DISK_SECTOR_SIZE;
    if (result > SIZE_MAX) return false;
    *size = (size_t)result;
    return true;
}

typedef struct {
    uint16_t cylinders;
    uint8_t heads;
    uint8_t sectors;
    uint64_t size_bytes;
} SimFloppyGeometry;

static const SimFloppyGeometry standard_floppy_geometries[] = {
    {40u, 1u, 8u, UINT64_C(163840)},
    {40u, 1u, 9u, UINT64_C(184320)},
    {40u, 2u, 8u, UINT64_C(327680)},
    {40u, 2u, 9u, UINT64_C(368640)},
    {80u, 2u, 8u, UINT64_C(655360)},
    {80u, 2u, 9u, UINT64_C(737280)},
    {80u, 2u, 15u, UINT64_C(1228800)},
    {80u, 2u, 18u, UINT64_C(1474560)},
    {80u, 2u, 36u, UINT64_C(2949120)},
    {77u, 1u, 8u, UINT64_C(315392)},
    {77u, 2u, 8u, UINT64_C(630784)},
    {77u, 2u, 15u, UINT64_C(1182720)}
};

static uint16_t floppy_word(const uint8_t *sector, size_t offset)
{
    return (uint16_t)(sector[offset] | ((uint16_t)sector[offset + 1u] << 8u));
}

static uint32_t floppy_dword(const uint8_t *sector, size_t offset)
{
    return (uint32_t)sector[offset] |
           ((uint32_t)sector[offset + 1u] << 8u) |
           ((uint32_t)sector[offset + 2u] << 16u) |
           ((uint32_t)sector[offset + 3u] << 24u);
}

static bool geometry_from_bpb(const uint8_t *sector, uint64_t image_size,
                              uint16_t *cylinders, uint8_t *heads,
                              uint8_t *sectors)
{
    uint16_t bytes_per_sector;
    uint16_t sectors_per_track;
    uint16_t bpb_heads;
    uint32_t total_sectors;
    uint64_t cylinder_count;

    if (sector == NULL || image_size == 0u ||
        floppy_word(sector, 11u) != SIM_DISK_SECTOR_SIZE ||
        sector[13] == 0u || sector[16] == 0u ||
        floppy_word(sector, 17u) == 0u) return false;
    bytes_per_sector = floppy_word(sector, 11u);
    sectors_per_track = floppy_word(sector, 24u);
    bpb_heads = floppy_word(sector, 26u);
    total_sectors = floppy_word(sector, 19u);
    if (total_sectors == 0u) total_sectors = floppy_dword(sector, 32u);
    if (bytes_per_sector != SIM_DISK_SECTOR_SIZE || sectors_per_track == 0u ||
        sectors_per_track > UINT8_MAX || bpb_heads == 0u || bpb_heads > UINT8_MAX ||
        total_sectors == 0u || image_size != (uint64_t)total_sectors * SIM_DISK_SECTOR_SIZE) {
        return false;
    }
    cylinder_count = total_sectors / ((uint64_t)bpb_heads * sectors_per_track);
    if (cylinder_count == 0u || cylinder_count > UINT16_MAX ||
        cylinder_count * bpb_heads * sectors_per_track != total_sectors) return false;
    *cylinders = (uint16_t)cylinder_count;
    *heads = (uint8_t)bpb_heads;
    *sectors = (uint8_t)sectors_per_track;
    return true;
}

bool sim_disk_floppy_geometry_from_size(uint64_t size_bytes,
                                        uint16_t *cylinders, uint8_t *heads,
                                        uint8_t *sectors)
{
    size_t index;
    uint64_t total_sectors;
    uint32_t best_cylinders = UINT32_MAX;
    uint8_t best_heads = 0u;
    uint8_t best_sectors = 0u;

    if (cylinders == NULL || heads == NULL || sectors == NULL ||
        size_bytes == 0u || size_bytes % SIM_DISK_SECTOR_SIZE != 0u) return false;
    for (index = 0u; index < sizeof(standard_floppy_geometries) /
                              sizeof(standard_floppy_geometries[0]); ++index) {
        if (standard_floppy_geometries[index].size_bytes == size_bytes) {
            *cylinders = standard_floppy_geometries[index].cylinders;
            *heads = standard_floppy_geometries[index].heads;
            *sectors = standard_floppy_geometries[index].sectors;
            return true;
        }
    }

    total_sectors = size_bytes / SIM_DISK_SECTOR_SIZE;
    if (total_sectors > UINT32_MAX) return false;
    /*
     * A raw image has no intrinsic CHS metadata.  Accept only a geometry
     * that the legacy INT 13h CHS interface can represent and prefer the
     * conventional two-sided layout.  Callers needing a nonstandard layout
     * can still use sim_disk_mount_file with explicit geometry.
     */
    for (unsigned candidate_heads = 2u; candidate_heads <= 2u; ++candidate_heads) {
        for (unsigned candidate_sectors = 1u; candidate_sectors <= 63u; ++candidate_sectors) {
            uint64_t divisor = (uint64_t)candidate_heads * candidate_sectors;
            uint64_t candidate_cylinders;
            if (total_sectors % divisor != 0u) continue;
            candidate_cylinders = total_sectors / divisor;
            if (candidate_cylinders == 0u || candidate_cylinders > UINT16_MAX) continue;
            if (candidate_cylinders < best_cylinders ||
                (candidate_cylinders == best_cylinders && candidate_heads == 2u)) {
                best_cylinders = (uint32_t)candidate_cylinders;
                best_heads = (uint8_t)candidate_heads;
                best_sectors = (uint8_t)candidate_sectors;
            }
        }
    }
    if (best_heads == 0u) return false;
    *cylinders = (uint16_t)best_cylinders;
    *heads = best_heads;
    *sectors = best_sectors;
    return true;
}

bool sim_disk_detect_floppy_geometry(const char *path, uint16_t *cylinders,
                                     uint8_t *heads, uint8_t *sectors)
{
    FILE *file;
    uint8_t boot_sector[SIM_DISK_SECTOR_SIZE] = {0};
    long end;
    uint64_t size;

    if (path == NULL || cylinders == NULL || heads == NULL || sectors == NULL) return false;
    file = open_binary_file(path, false);
    if (file == NULL || fseek(file, 0L, SEEK_END) != 0) {
        if (file != NULL) fclose(file);
        return false;
    }
    end = ftell(file);
    if (end <= 0L || fseek(file, 0L, SEEK_SET) != 0) {
        fclose(file);
        return false;
    }
    size = (uint64_t)end;
    if (fread(boot_sector, 1u, sizeof(boot_sector), file) == sizeof(boot_sector) &&
        geometry_from_bpb(boot_sector, size, cylinders, heads, sectors)) {
        fclose(file);
        return true;
    }
    fclose(file);
    return sim_disk_floppy_geometry_from_size(size, cylinders, heads, sectors);
}

static SimDiskMedia *selected_media(SimDisk *disk, const SimDiskState *state)
{
    unsigned index;
    if (disk == NULL || state == NULL) return NULL;
    index = state->selected_drive;
    return index < SIM_DISK_DRIVE_COUNT ? &disk->media[index] : NULL;
}

static uint32_t lba_from_registers(const SimDisk *disk, const SimDiskState *state)
{
    const SimDiskMedia *media;
    uint32_t cylinder;
    uint32_t head;
    uint32_t sector;

    if (disk == NULL || state == NULL) return UINT32_MAX;
    if ((state->drive_head & 0x40u) != 0u) {
        return ((uint32_t)(state->drive_head & 0x0Fu) << 24u) |
               ((uint32_t)state->cylinder_high << 16u) |
               ((uint32_t)state->cylinder_low << 8u) |
               state->sector_number;
    }
    media = selected_media((SimDisk *)disk, state);
    if (media == NULL || !geometry_is_valid(media->cylinders, media->heads,
                                             media->sectors_per_track) ||
        state->sector_number == 0u || state->sector_number > media->sectors_per_track ||
        (state->drive_head & 0x0Fu) >= media->heads) return UINT32_MAX;
    cylinder = (uint32_t)state->cylinder_low | ((uint32_t)state->cylinder_high << 8u);
    head = state->drive_head & 0x0Fu;
    sector = state->sector_number - 1u;
    return ((cylinder * media->heads + head) * media->sectors_per_track) + sector;
}

static bool sector_is_available(const SimDiskMedia *media, uint32_t lba)
{
    uint64_t offset;
    if (media == NULL || !media->present || media->bytes == NULL) return false;
    offset = (uint64_t)lba * SIM_DISK_SECTOR_SIZE;
    return offset + SIM_DISK_SECTOR_SIZE <= media->size_bytes;
}

static void finish_with_error(SimDiskState *state)
{
    state->error = ATA_ERROR_ABRT;
    state->status = SIM_DISK_STATUS_DRDY | SIM_DISK_STATUS_ERR;
    state->write_transfer = false;
    state->transfer_index = 0u;
    state->sectors_remaining = 0u;
}

static void raise_irq(SimDisk *disk, SimDiskState *state)
{
    state->irq_pending = true;
    state->irq_pulse = true;
    if (disk->pic != NULL) sim_pic_pulse_irq(disk->pic, disk->irq_line);
}

static void load_current_sector(SimDisk *disk, SimDiskState *state)
{
    SimDiskMedia *media = selected_media(disk, state);
    if (!sector_is_available(media, state->current_lba)) {
        finish_with_error(state);
        raise_irq(disk, state);
        return;
    }
    memcpy(state->data, media->bytes + (size_t)state->current_lba * SIM_DISK_SECTOR_SIZE,
           SIM_DISK_SECTOR_SIZE);
    state->transfer_index = 0u;
    state->write_transfer = false;
    state->status = SIM_DISK_STATUS_DRDY | SIM_DISK_STATUS_DRQ;
    raise_irq(disk, state);
}

static void start_identify(SimDisk *disk, SimDiskState *state)
{
    SimDiskMedia *media = selected_media(disk, state);
    uint16_t words[SIM_DISK_SECTOR_SIZE / 2u] = {0};
    uint32_t sectors = media == NULL ? 0u : (uint32_t)(media->size_bytes / SIM_DISK_SECTOR_SIZE);
    if (media == NULL || !media->present) {
        finish_with_error(state);
        raise_irq(disk, state);
        return;
    }
    words[0] = 0x0040u;
    words[1] = media->cylinders;
    words[3] = media->heads;
    words[6] = media->sectors_per_track;
    words[47] = 0x8001u;
    words[49] = 0x0200u;
    words[53] = 0x0001u;
    words[60] = (uint16_t)sectors;
    words[61] = (uint16_t)(sectors >> 16u);
    memcpy(state->data, words, sizeof(words));
    state->transfer_index = 0u;
    state->write_transfer = false;
    state->status = SIM_DISK_STATUS_DRDY | SIM_DISK_STATUS_DRQ;
    raise_irq(disk, state);
}

static void start_write_sector(SimDisk *disk, SimDiskState *state)
{
    SimDiskMedia *media = selected_media(disk, state);
    if (!sector_is_available(media, state->current_lba) || !media->writable) {
        finish_with_error(state);
        raise_irq(disk, state);
        return;
    }
    memset(state->data, 0, sizeof(state->data));
    state->transfer_index = 0u;
    state->write_transfer = true;
    state->status = SIM_DISK_STATUS_DRDY | SIM_DISK_STATUS_DRQ;
    raise_irq(disk, state);
}

static void start_pending_command(SimDisk *disk, SimDiskState *state)
{
    switch (state->pending_command) {
    case ATA_COMMAND_READ_SECTORS: load_current_sector(disk, state); break;
    case ATA_COMMAND_WRITE_SECTORS: start_write_sector(disk, state); break;
    case ATA_COMMAND_IDENTIFY: start_identify(disk, state); break;
    case ATA_COMMAND_CACHE_FLUSH:
        state->status = SIM_DISK_STATUS_DRDY;
        raise_irq(disk, state);
        break;
    default:
        finish_with_error(state);
        raise_irq(disk, state);
        break;
    }
}

static void transfer_finished(SimDisk *disk, SimDiskState *state)
{
    SimDiskMedia *media = selected_media(disk, state);
    if (state->write_transfer) {
        if (!sector_is_available(media, state->current_lba) || !media->writable) {
            finish_with_error(state);
            raise_irq(disk, state);
            return;
        }
        memcpy(media->bytes + (size_t)state->current_lba * SIM_DISK_SECTOR_SIZE,
               state->data, SIM_DISK_SECTOR_SIZE);
    }
    if (state->pending_command == ATA_COMMAND_IDENTIFY || state->sectors_remaining <= 1u) {
        state->sectors_remaining = 0u;
        state->write_transfer = false;
        state->status = SIM_DISK_STATUS_DRDY;
        return;
    }
    state->sectors_remaining--;
    state->current_lba++;
    if (state->write_transfer) start_write_sector(disk, state);
    else load_current_sector(disk, state);
}

static bool disk_probe(void *instance, const SimBusRequest *request)
{
    SimDisk *disk = instance;
    if (disk == NULL || request == NULL || !request->valid || request->kind != SIM_BUS_IO) return false;
    return (request->address >= disk->io_base && request->address < disk->io_base + 8u) ||
           request->address == disk->control_port;
}

static void disk_evaluate(void *instance, const SimState *current, const SimBusState *transaction,
                          SimBusResponse *response)
{
    SimDisk *disk = instance;
    const SimDiskState *state = sim_disk_current_state(disk, current);
    uint16_t port;
    uint16_t value = 0u;
    uint16_t index;
    if (disk == NULL || state == NULL || transaction == NULL || response == NULL) return;
    response->ready = true;
    if (transaction->request.direction == SIM_BUS_WRITE) return;
    port = (uint16_t)transaction->request.address;
    if (port == disk->control_port) { response->data = response_byte(&transaction->request, state->status); return; }
    index = port - disk->io_base;
    if (index == ATA_DATA) {
        if ((state->status & SIM_DISK_STATUS_DRQ) == 0u || state->write_transfer) {
            response->data = 0u;
            return;
        }
        value = state->data[state->transfer_index];
        if (transaction->request.width == SIM_BUS_WORD && state->transfer_index + 1u < SIM_DISK_SECTOR_SIZE)
            value |= (uint16_t)((uint16_t)state->data[state->transfer_index + 1u] << 8u);
        response->data = transaction->request.width == SIM_BUS_WORD ? value : response_byte(&transaction->request, (uint8_t)value);
        return;
    }
    switch (index) {
    case ATA_ERROR_FEATURE: value = state->error; break;
    case ATA_SECTOR_COUNT: value = state->sector_count; break;
    case ATA_SECTOR_NUMBER: value = state->sector_number; break;
    case ATA_CYLINDER_LOW: value = state->cylinder_low; break;
    case ATA_CYLINDER_HIGH: value = state->cylinder_high; break;
    case ATA_DRIVE_HEAD: value = state->drive_head; break;
    case ATA_STATUS_COMMAND: value = state->status; break;
    default: break;
    }
    response->data = response_byte(&transaction->request, (uint8_t)value);
}

static void begin_command(SimDisk *disk, SimDiskState *state, uint8_t command)
{
    state->command = command;
    state->pending_command = command;
    state->selected_drive = (state->drive_head >> 4u) & 1u;
    state->sectors_remaining = state->sector_count == 0u ? 256u : state->sector_count;
    if (command == ATA_COMMAND_READ_SECTORS || command == ATA_COMMAND_WRITE_SECTORS) {
        state->sectors_remaining = state->sector_count == 0u ? 256u : state->sector_count;
        state->current_lba = lba_from_registers(disk, state);
    }
    state->error = 0u;
    state->irq_pending = false;
    state->irq_pulse = false;
    state->write_transfer = false;
    state->transfer_index = 0u;
    state->busy_cycles = disk->command_latency_cycles + 1u;
    state->status = SIM_DISK_STATUS_BSY | SIM_DISK_STATUS_DRDY;
}

static void disk_commit(void *instance, const SimState *current, SimState *next,
                        const SimBusRequest *request, const SimBusResponse *response)
{
    SimDisk *disk = instance;
    SimDiskState *state;
    uint16_t port;
    uint16_t index;
    uint16_t value;
    size_t width;
    (void)current;
    if (disk == NULL || next == NULL || request == NULL || response == NULL || !response->ready || response->error) return;
    state = (SimDiskState *)sim_state_region_next(next, disk->state_region);
    if (state == NULL) return;
    port = (uint16_t)request->address;
    if (request->direction == SIM_BUS_READ) {
        if (port == disk->io_base + ATA_STATUS_COMMAND) state->irq_pending = false;
        if (port == disk->io_base + ATA_DATA && (state->status & SIM_DISK_STATUS_DRQ) != 0u && !state->write_transfer) {
            width = request->width == SIM_BUS_WORD ? 2u : 1u;
            state->transfer_index = (uint16_t)(state->transfer_index + width);
            if (state->transfer_index >= SIM_DISK_SECTOR_SIZE) transfer_finished(disk, state);
        }
        return;
    }
    if (port == disk->control_port) {
        if ((request_byte(request) & 0x04u) != 0u) {
            SimDiskState reset = {0};
            reset.status = SIM_DISK_STATUS_DRDY;
            *state = reset;
        }
        return;
    }
    index = port - disk->io_base;
    if (index == ATA_DATA) {
        if ((state->status & SIM_DISK_STATUS_DRQ) == 0u || !state->write_transfer) return;
        value = request->data;
        state->data[state->transfer_index++] = (uint8_t)value;
        if (request->width == SIM_BUS_WORD && state->transfer_index < SIM_DISK_SECTOR_SIZE)
            state->data[state->transfer_index++] = (uint8_t)(value >> 8u);
        if (state->transfer_index >= SIM_DISK_SECTOR_SIZE) transfer_finished(disk, state);
        return;
    }
    switch (index) {
    case ATA_ERROR_FEATURE: state->feature = request_byte(request); break;
    case ATA_SECTOR_COUNT: state->sector_count = request_byte(request); break;
    case ATA_SECTOR_NUMBER: state->sector_number = request_byte(request); break;
    case ATA_CYLINDER_LOW: state->cylinder_low = request_byte(request); break;
    case ATA_CYLINDER_HIGH: state->cylinder_high = request_byte(request); break;
    case ATA_DRIVE_HEAD: state->drive_head = request_byte(request); break;
    case ATA_STATUS_COMMAND: begin_command(disk, state, request_byte(request)); break;
    default: break;
    }
}

static void disk_reset(void *instance, SimState *state)
{
    SimDisk *disk = instance;
    SimDiskState reset = {0};
    if (disk == NULL || state == NULL) return;
    reset.status = SIM_DISK_STATUS_DRDY;
    *(SimDiskState *)sim_state_region_current(state, disk->state_region) = reset;
    *(SimDiskState *)sim_state_region_next(state, disk->state_region) = reset;
}

static void disk_sample(void *instance, const SimState *current, SimState *next,
                        const SimCycleResolution *resolution)
{
    SimDisk *disk = instance;
    const SimDiskState *old = sim_disk_current_state(disk, current);
    SimDiskState *state;
    (void)resolution;
    if (disk == NULL || old == NULL || next == NULL) return;
    state = (SimDiskState *)sim_state_region_next(next, disk->state_region);
    if (state == NULL) return;
    if (old->irq_pulse) {
        if (disk->pic != NULL) sim_pic_set_irq_line(disk->pic, disk->irq_line, false);
        state->irq_pulse = false;
    }
    if ((old->status & SIM_DISK_STATUS_BSY) == 0u) return;
    if (old->busy_cycles > 1u) {
        state->busy_cycles = old->busy_cycles - 1u;
        return;
    }
    state->busy_cycles = 0u;
    start_pending_command(disk, state);
}

static void disk_trace(void *instance, const SimState *current, const SimState *next, SimTrace *trace)
{
    SimDisk *disk = instance;
    const SimDiskState *before = sim_disk_current_state(disk, current);
    const SimDiskState *after = sim_disk_current_state(disk, next);
    char old_text[SIM_TRACE_TEXT_SIZE];
    char new_text[SIM_TRACE_TEXT_SIZE];
    if (before == NULL || after == NULL || trace == NULL || before->status == after->status) return;
    (void)snprintf(old_text, sizeof(old_text), "%02x", before->status);
    (void)snprintf(new_text, sizeof(new_text), "%02x", after->status);
    sim_trace_record_state_change(trace, "disk", "status", old_text, new_text);
}

void sim_disk_init(SimDisk *disk, SimPic *pic)
{
    if (disk == NULL) return;
    memset(disk, 0, sizeof(*disk));
    disk->io_base = 0x1F0u;
    disk->control_port = 0x3F6u;
    disk->command_latency_cycles = 2u;
    disk->irq_line = 6u;
    disk->pic = pic;
}

void sim_disk_destroy(SimDisk *disk)
{
    unsigned drive;
    if (disk == NULL) return;
    for (drive = 0u; drive < SIM_DISK_DRIVE_COUNT; ++drive) media_clear(&disk->media[drive]);
    memset(disk, 0, sizeof(*disk));
}

void sim_disk_set_latency(SimDisk *disk, uint32_t cycles)
{
    if (disk != NULL) disk->command_latency_cycles = cycles;
}

bool sim_disk_mount_bytes(SimDisk *disk, unsigned drive, const uint8_t *source, size_t size_bytes,
                          uint16_t cylinders, uint8_t heads, uint8_t sectors, bool writable)
{
    SimDiskMedia replacement = {0};
    size_t expected;
    if (disk == NULL || drive >= SIM_DISK_DRIVE_COUNT || source == NULL ||
        !media_size(cylinders, heads, sectors, &expected) || expected != size_bytes) return false;
    replacement.bytes = malloc(size_bytes);
    if (replacement.bytes == NULL) return false;
    memcpy(replacement.bytes, source, size_bytes);
    replacement.size_bytes = size_bytes;
    replacement.cylinders = cylinders;
    replacement.heads = heads;
    replacement.sectors_per_track = sectors;
    replacement.present = true;
    replacement.writable = writable;
    media_clear(&disk->media[drive]);
    disk->media[drive] = replacement;
    return true;
}

bool sim_disk_create_media(SimDisk *disk, unsigned drive, uint16_t cylinders, uint8_t heads,
                           uint8_t sectors, bool writable)
{
    SimDiskMedia replacement = {0};
    size_t size;
    if (disk == NULL || drive >= SIM_DISK_DRIVE_COUNT || !media_size(cylinders, heads, sectors, &size)) return false;
    replacement.bytes = calloc(1u, size);
    if (replacement.bytes == NULL) return false;
    replacement.size_bytes = size;
    replacement.cylinders = cylinders;
    replacement.heads = heads;
    replacement.sectors_per_track = sectors;
    replacement.present = true;
    replacement.writable = writable;
    media_clear(&disk->media[drive]);
    disk->media[drive] = replacement;
    return true;
}

bool sim_disk_mount_file(SimDisk *disk, unsigned drive, const char *path, uint16_t cylinders,
                         uint8_t heads, uint8_t sectors, bool writable)
{
    FILE *file;
    uint8_t *buffer;
    size_t size;
    bool ok;
    if (disk == NULL || path == NULL || !media_size(cylinders, heads, sectors, &size)) return false;
    file = open_binary_file(path, false);
    if (file == NULL) return false;
    buffer = malloc(size);
    if (buffer == NULL) { fclose(file); return false; }
    ok = fread(buffer, 1u, size, file) == size && fgetc(file) == EOF;
    fclose(file);
    if (!ok) { free(buffer); return false; }
    ok = sim_disk_mount_bytes(disk, drive, buffer, size, cylinders, heads, sectors, writable);
    free(buffer);
    return ok;
}

bool sim_disk_mount_floppy_file(SimDisk *disk, unsigned drive, const char *path,
                                bool writable)
{
    uint16_t cylinders;
    uint8_t heads;
    uint8_t sectors;

    if (!sim_disk_detect_floppy_geometry(path, &cylinders, &heads, &sectors)) {
        return false;
    }
    return sim_disk_mount_file(disk, drive, path, cylinders, heads, sectors, writable);
}

static void cancel_media_command(SimDisk *disk, SimState *state, unsigned drive)
{
    SimDiskState *current;
    SimDiskState *next;

    if (disk == NULL || state == NULL || !disk->attached ||
        drive >= SIM_DISK_DRIVE_COUNT) {
        return;
    }

    current = (SimDiskState *)sim_state_region_current(state, disk->state_region);
    next = (SimDiskState *)sim_state_region_next(state, disk->state_region);
    if (current == NULL || next == NULL ||
        (current->selected_drive != drive && next->selected_drive != drive)) {
        return;
    }

    current->error = 0u;
    current->command = 0u;
    current->pending_command = 0u;
    current->status = SIM_DISK_STATUS_DRDY;
    current->sectors_remaining = 0u;
    current->transfer_index = 0u;
    current->busy_cycles = 0u;
    current->write_transfer = false;
    current->irq_pending = false;
    current->irq_pulse = false;
    *next = *current;
    if (disk->pic != NULL) {
        sim_pic_set_irq_line(disk->pic, disk->irq_line, false);
    }
}

bool sim_disk_reload_floppy_file(SimDisk *disk, SimState *state, unsigned drive,
                                 const char *path, bool writable)
{
    if (!sim_disk_mount_floppy_file(disk, drive, path, writable)) {
        return false;
    }
    cancel_media_command(disk, state, drive);
    return true;
}

bool sim_disk_eject_media(SimDisk *disk, SimState *state, unsigned drive)
{
    if (disk == NULL || drive >= SIM_DISK_DRIVE_COUNT) {
        return false;
    }
    media_clear(&disk->media[drive]);
    cancel_media_command(disk, state, drive);
    return true;
}

bool sim_disk_create_file(const char *path, uint16_t cylinders, uint8_t heads,
                          uint8_t sectors)
{
    uint8_t zeros[4096] = {0};
    size_t size;
    size_t remaining;
    bool ok = true;

    if (path == NULL || !media_size(cylinders, heads, sectors, &size)) return false;
#ifdef _WIN32
    int characters;
    wchar_t *wide_path;
    HANDLE file;

    characters = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, -1, NULL, 0);
    if (characters <= 0) return false;
    wide_path = malloc((size_t)characters * sizeof(*wide_path));
    if (wide_path == NULL) return false;
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, -1,
                            wide_path, characters) <= 0) {
        free(wide_path);
        return false;
    }
    file = CreateFileW(wide_path, GENERIC_WRITE, 0u, NULL, CREATE_NEW,
                       FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) {
        free(wide_path);
        return false;
    }
    remaining = size;
    while (remaining != 0u) {
        size_t chunk = remaining < sizeof(zeros) ? remaining : sizeof(zeros);
        DWORD written = 0u;
        if (!WriteFile(file, zeros, (DWORD)chunk, &written, NULL) ||
            written != (DWORD)chunk) {
            ok = false;
            break;
        }
        remaining -= chunk;
    }
    if (ok && !FlushFileBuffers(file)) ok = false;
    if (!CloseHandle(file)) ok = false;
    if (!ok) (void)DeleteFileW(wide_path);
    free(wide_path);
#else
    FILE *file = fopen(path, "wbx");
    if (file == NULL) return false;
    remaining = size;
    while (remaining != 0u) {
        size_t chunk = remaining < sizeof(zeros) ? remaining : sizeof(zeros);
        if (fwrite(zeros, 1u, chunk, file) != chunk) {
            ok = false;
            break;
        }
        remaining -= chunk;
    }
    if (fclose(file) != 0) ok = false;
#endif
    return ok;
}

bool sim_disk_flush_file(const SimDisk *disk, unsigned drive, const char *path)
{
    const SimDiskMedia *media;
    FILE *file;
    bool ok;
    if (disk == NULL || drive >= SIM_DISK_DRIVE_COUNT || path == NULL) return false;
    media = &disk->media[drive];
    if (!media->present || media->bytes == NULL) return false;
    file = open_binary_file(path, true);
    if (file == NULL) return false;
    ok = fwrite(media->bytes, 1u, media->size_bytes, file) == media->size_bytes;
    if (fclose(file) != 0) ok = false;
    return ok;
}

SimBusTarget sim_disk_bus_target(SimDisk *disk, const char *name, uint32_t target_id)
{
    SimBusTarget target = {0};
    target.name = name;
    target.target_id = target_id;
    target.instance = disk;
    target.probe = disk_probe;
    target.evaluate = disk_evaluate;
    target.commit = disk_commit;
    return target;
}

bool sim_disk_attach(SimDisk *disk, SimKernel *kernel)
{
    SimDiskState reset = {0};
    SimModule module = {0};
    if (disk == NULL || kernel == NULL || kernel->state == NULL || disk->attached) return false;
    reset.status = SIM_DISK_STATUS_DRDY;
    if (!sim_state_add_region(kernel->state, "disk", sizeof(reset), &reset, &disk->state_region)) return false;
    module.name = "disk";
    module.instance = disk;
    module.reset = disk_reset;
    module.sample = disk_sample;
    module.trace = disk_trace;
    disk->attached = sim_kernel_attach_module(kernel, &module);
    return disk->attached;
}

const SimDiskState *sim_disk_current_state(const SimDisk *disk, const SimState *state)
{
    if (disk == NULL || state == NULL || !disk->attached) return NULL;
    return (const SimDiskState *)sim_state_region_current_const(state, disk->state_region);
}
