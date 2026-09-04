#ifndef SIM_SIM_DMA_H
#define SIM_SIM_DMA_H

#include "sim_bus.h"
#include "sim_kernel.h"

#define SIM_DMA_CHANNEL_COUNT 4u
#define SIM_DMA_MASTER_ID 0u

typedef enum {
    SIM_DMA_TRANSFER_VERIFY = 0x00u,
    SIM_DMA_TRANSFER_WRITE = 0x04u,
    SIM_DMA_TRANSFER_READ = 0x08u
} SimDmaTransferType;

typedef enum {
    SIM_DMA_MODE_DEMAND = 0x00u,
    SIM_DMA_MODE_SINGLE = 0x40u,
    SIM_DMA_MODE_BLOCK = 0x80u,
    SIM_DMA_MODE_CASCADE = 0xC0u
} SimDmaServiceMode;

typedef enum {
    SIM_DMA_PHASE_IDLE = 0,
    SIM_DMA_PHASE_IO_READ,
    SIM_DMA_PHASE_WAIT_IO_READ,
    SIM_DMA_PHASE_MEMORY_READ,
    SIM_DMA_PHASE_WAIT_MEMORY_READ,
    SIM_DMA_PHASE_MEMORY_WRITE,
    SIM_DMA_PHASE_WAIT_MEMORY_WRITE,
    SIM_DMA_PHASE_IO_WRITE,
    SIM_DMA_PHASE_WAIT_IO_WRITE,
    SIM_DMA_PHASE_VERIFY_READ,
    SIM_DMA_PHASE_WAIT_VERIFY_READ,
    SIM_DMA_PHASE_MEMORY_TO_MEMORY_READ,
    SIM_DMA_PHASE_WAIT_MEMORY_TO_MEMORY_READ,
    SIM_DMA_PHASE_MEMORY_TO_MEMORY_WRITE,
    SIM_DMA_PHASE_WAIT_MEMORY_TO_MEMORY_WRITE,
    SIM_DMA_PHASE_CASCADE
} SimDmaPhase;

typedef struct {
    uint16_t base_address;
    uint16_t current_address;
    uint16_t base_count;
    uint16_t current_count;
    uint8_t page;
    uint8_t mode;
} SimDmaChannelState;

typedef struct {
    SimDmaChannelState channel[SIM_DMA_CHANNEL_COUNT];
    uint8_t command;
    uint8_t status;
    uint8_t software_request;
    uint8_t mask;
    uint8_t temporary;
    bool flip_flop_high;
    uint8_t active_channel;
    uint8_t last_channel;
    SimDmaPhase phase;
    bool cascade_granted;
} SimDmaState;

typedef struct {
    uint16_t io_port;
    bool connected;
} SimDmaChannelBinding;

typedef struct {
    SimDmaChannelBinding binding[SIM_DMA_CHANNEL_COUNT];
    uint8_t dreq_lines;
    bool eop_asserted;
    SimStateRegionId state_region;
    bool attached;
} SimDma;

void sim_dma_init(SimDma *dma);
void sim_dma_set_dreq(SimDma *dma, unsigned channel, bool asserted);
/* EOP terminates the active transfer at its next completed byte boundary. */
void sim_dma_set_eop(SimDma *dma, bool asserted);
bool sim_dma_bind_io_port(SimDma *dma, unsigned channel, uint16_t io_port);
SimBusTarget sim_dma_bus_target(SimDma *dma, const char *name, uint32_t target_id);
bool sim_dma_attach(SimDma *dma, SimKernel *kernel);
const SimDmaState *sim_dma_current_state(const SimDma *dma, const SimState *state);

#endif
