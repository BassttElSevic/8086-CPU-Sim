#ifndef SIM_CPU8086_PREFETCH_H
#define SIM_CPU8086_PREFETCH_H

#include "cpu8086_state.h"

bool cpu8086_prefetch_peek(const Cpu8086PrefetchState *queue, uint8_t *byte);
void cpu8086_prefetch_clear(Cpu8086PrefetchState *queue);

/* Applies one EU dequeue and zero, one, or two BIU enqueues atomically. */
bool cpu8086_prefetch_advance(const Cpu8086PrefetchState *current,
                               Cpu8086PrefetchState *next,
                               bool consume,
                               const uint8_t *produced_bytes,
                               uint8_t produced_byte_count);

#endif
