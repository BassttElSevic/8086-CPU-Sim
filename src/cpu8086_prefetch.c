#include "sim/cpu8086_prefetch.h"

#include <string.h>

bool cpu8086_prefetch_peek(const Cpu8086PrefetchState *queue, uint8_t *byte)
{
    if (queue == NULL || queue->count == 0u) {
        return false;
    }

    if (byte != NULL) {
        *byte = queue->bytes[queue->head];
    }
    return true;
}

void cpu8086_prefetch_clear(Cpu8086PrefetchState *queue)
{
    if (queue != NULL) {
        memset(queue, 0, sizeof(*queue));
    }
}

bool cpu8086_prefetch_advance(const Cpu8086PrefetchState *current,
                               Cpu8086PrefetchState *next,
                               bool consume,
                               const uint8_t *produced_bytes,
                               uint8_t produced_byte_count)
{
    uint8_t tail;
    uint8_t index;
    uint8_t count_after_consume;

    if (current == NULL || next == NULL || current->count > CPU8086_PREFETCH_CAPACITY ||
        current->head >= CPU8086_PREFETCH_CAPACITY ||
        (consume && current->count == 0u) ||
        produced_byte_count > 2u ||
        (produced_byte_count != 0u && produced_bytes == NULL)) {
        return false;
    }

    count_after_consume = (uint8_t)(current->count - (consume ? 1u : 0u));
    if (produced_byte_count > CPU8086_PREFETCH_CAPACITY - count_after_consume) {
        return false;
    }

    *next = *current;
    if (consume) {
        next->head = (uint8_t)((next->head + 1u) % CPU8086_PREFETCH_CAPACITY);
        next->count -= 1u;
    }
    for (index = 0u; index < produced_byte_count; ++index) {
        tail = (uint8_t)((next->head + next->count) % CPU8086_PREFETCH_CAPACITY);
        next->bytes[tail] = produced_bytes[index];
        next->count += 1u;
    }
    return true;
}
