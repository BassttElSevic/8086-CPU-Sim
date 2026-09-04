#include "sim/sim_rom.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
extern FILE *_wfopen(const wchar_t *path, const wchar_t *mode);

static FILE *open_binary_file(const char *path)
{
    int characters;
    wchar_t *wide_path;
    FILE *file;

    if (path == NULL) return NULL;
    characters = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                                     path, -1, NULL, 0);
    if (characters <= 0) return NULL;
    wide_path = malloc((size_t)characters * sizeof(*wide_path));
    if (wide_path == NULL) return NULL;
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, -1,
                            wide_path, characters) <= 0) {
        free(wide_path);
        return NULL;
    }
    file = _wfopen(wide_path, L"rb");
    free(wide_path);
    return file;
}
#else
static FILE *open_binary_file(const char *path)
{
    return path == NULL ? NULL : fopen(path, "rb");
}
#endif

static bool contains(uint32_t address, size_t span)
{
    uint64_t first = address;
    uint64_t last = first + span;
    return first >= SIM_ROM_BASE && last <= SIM_ROM_BASE + SIM_ROM_SIZE;
}

static size_t span(const SimBusRequest *request)
{
    return request->width == SIM_BUS_WORD &&
           (request->byte_enable & SIM_BUS_BYTE_ENABLE_HIGH) != 0u ? 2u : 1u;
}

static bool probe(void *instance, const SimBusRequest *request)
{
    (void)instance;
    return request != NULL && request->valid && request->kind == SIM_BUS_MEMORY &&
           contains(request->address, span(request));
}

static void evaluate(void *instance, const SimState *current, const SimBusState *transaction,
                     SimBusResponse *response)
{
    SimRom *rom = instance;
    size_t offset;
    (void)current;
    if (rom == NULL || transaction == NULL || response == NULL || !probe(rom, &transaction->request)) {
        if (response != NULL) response->error = true;
        return;
    }
    response->ready = true;
    if (transaction->request.direction == SIM_BUS_WRITE) {
        response->error = true;
        return;
    }
    offset = (size_t)(transaction->request.address - SIM_ROM_BASE);
    if (transaction->request.width == SIM_BUS_WORD) {
        response->data = rom->bytes[offset];
        if ((transaction->request.byte_enable & SIM_BUS_BYTE_ENABLE_HIGH) != 0u)
            response->data |= (uint16_t)((uint16_t)rom->bytes[offset + 1u] << 8u);
    } else {
        response->data = transaction->request.byte_enable == SIM_BUS_BYTE_ENABLE_HIGH
            ? (uint16_t)((uint16_t)rom->bytes[offset] << 8u) : rom->bytes[offset];
    }
}

static void commit(void *instance, const SimState *current, SimState *next,
                   const SimBusRequest *request, const SimBusResponse *response)
{
    (void)instance;
    (void)current;
    (void)next;
    (void)request;
    (void)response;
}

void sim_rom_init(SimRom *rom, uint8_t fill_value)
{
    if (rom != NULL) memset(rom->bytes, fill_value, sizeof(rom->bytes));
}

bool sim_rom_load_bytes(SimRom *rom, uint32_t physical_address,
                        const uint8_t *source, size_t size)
{
    if (rom == NULL || source == NULL || size == 0u || !contains(physical_address, size)) return false;
    memcpy(rom->bytes + (physical_address - SIM_ROM_BASE), source, size);
    return true;
}

bool sim_rom_load_file(SimRom *rom, const char *path)
{
    FILE *file;
    size_t count;
    int extra;
    if (rom == NULL || path == NULL) return false;
    file = open_binary_file(path);
    if (file == NULL) return false;
    count = fread(rom->bytes, 1u, sizeof(rom->bytes), file);
    extra = fgetc(file);
    fclose(file);
    return count != 0u && extra == EOF;
}

SimBusTarget sim_rom_bus_target(SimRom *rom, const char *name, uint32_t target_id)
{
    SimBusTarget target = {0};
    target.name = name;
    target.target_id = target_id;
    target.instance = rom;
    target.probe = probe;
    target.evaluate = evaluate;
    target.commit = commit;
    return target;
}
