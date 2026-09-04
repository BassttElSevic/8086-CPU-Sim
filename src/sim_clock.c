#include "sim/sim_clock.h"

#include <string.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <time.h>
#endif

enum { NANOSECONDS_PER_SECOND = UINT64_C(1000000000) };

bool sim_clock_init(SimClock *clock, uint64_t frequency_hz,
                    uint64_t max_catchup_cycles)
{
    if (clock == NULL || frequency_hz == 0u || max_catchup_cycles == 0u) {
        return false;
    }
    memset(clock, 0, sizeof(*clock));
    clock->frequency_hz = frequency_hz;
    clock->max_catchup_cycles = max_catchup_cycles;
    return true;
}

void sim_clock_start(SimClock *clock, uint64_t now_ns)
{
    if (clock == NULL) return;
    clock->last_time_ns = now_ns;
    clock->fractional = 0u;
    clock->pending_cycles = 0u;
    clock->running = true;
}

void sim_clock_stop(SimClock *clock, uint64_t now_ns)
{
    if (clock == NULL) return;
    clock->last_time_ns = now_ns;
    clock->fractional = 0u;
    clock->pending_cycles = 0u;
    clock->running = false;
}

void sim_clock_set_frequency(SimClock *clock, uint64_t frequency_hz,
                             uint64_t now_ns)
{
    if (clock == NULL || frequency_hz == 0u) return;
    clock->frequency_hz = frequency_hz;
    clock->last_time_ns = now_ns;
    clock->fractional = 0u;
    clock->pending_cycles = 0u;
}

uint64_t sim_clock_cycles_due(SimClock *clock, uint64_t now_ns)
{
    return sim_clock_take_cycles(clock, now_ns,
                                 clock == NULL ? 0u : clock->max_catchup_cycles);
}

uint64_t sim_clock_take_cycles(SimClock *clock, uint64_t now_ns,
                               uint64_t max_cycles)
{
    uint64_t elapsed;
    uint64_t whole_seconds;
    uint64_t remainder_ns;
    uint64_t generated;
    uint64_t product;
    uint64_t take;

    if (clock == NULL || !clock->running || max_cycles == 0u) {
        return 0u;
    }
    if (now_ns > clock->last_time_ns) {
        elapsed = now_ns - clock->last_time_ns;
        clock->last_time_ns = now_ns;
        whole_seconds = elapsed / NANOSECONDS_PER_SECOND;
        remainder_ns = elapsed % NANOSECONDS_PER_SECOND;
        if (whole_seconds > UINT64_MAX / clock->frequency_hz) {
            generated = UINT64_MAX;
            clock->fractional = 0u;
        } else {
            generated = whole_seconds * clock->frequency_hz;
            if (remainder_ns > (UINT64_MAX - clock->fractional) /
                              clock->frequency_hz) {
                product = UINT64_MAX;
            } else {
                product = remainder_ns * clock->frequency_hz + clock->fractional;
            }
            generated += product / NANOSECONDS_PER_SECOND;
            clock->fractional = product % NANOSECONDS_PER_SECOND;
        }
        if (generated > clock->max_catchup_cycles -
                        (clock->pending_cycles > clock->max_catchup_cycles
                         ? clock->max_catchup_cycles
                         : clock->pending_cycles)) {
            clock->pending_cycles = clock->max_catchup_cycles;
            clock->fractional = 0u;
        } else {
            clock->pending_cycles += generated;
        }
    }
    take = clock->pending_cycles < max_cycles
        ? clock->pending_cycles : max_cycles;
    clock->pending_cycles -= take;
    return take;
}

uint64_t sim_clock_now_ns(void)
{
#ifdef _WIN32
    static LARGE_INTEGER frequency;
    LARGE_INTEGER counter;
    uint64_t seconds;
    uint64_t remainder;

    if (frequency.QuadPart == 0 && !QueryPerformanceFrequency(&frequency)) {
        return 0u;
    }
    if (!QueryPerformanceCounter(&counter)) return 0u;
    seconds = (uint64_t)counter.QuadPart / (uint64_t)frequency.QuadPart;
    remainder = (uint64_t)counter.QuadPart % (uint64_t)frequency.QuadPart;
    return seconds * NANOSECONDS_PER_SECOND +
           remainder * NANOSECONDS_PER_SECOND /
           (uint64_t)frequency.QuadPart;
#else
    struct timespec timestamp;
    if (clock_gettime(CLOCK_MONOTONIC, &timestamp) != 0) return 0u;
    return (uint64_t)timestamp.tv_sec * NANOSECONDS_PER_SECOND +
           (uint64_t)timestamp.tv_nsec;
#endif
}
