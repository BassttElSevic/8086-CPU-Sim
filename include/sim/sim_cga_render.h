#ifndef SIM_SIM_CGA_RENDER_H
#define SIM_SIM_CGA_RENDER_H

#include <stdbool.h>
#include <stdint.h>

#include "sim_cga.h"

/*
 * Portable CGA framebuffer renderer.
 *
 * This module converts the committed CGA video state (SimCgaState) into a
 * plain 640x200 RGBA pixel buffer plus a small amount of text-mode metadata.
 * It has no dependency on any windowing or graphics library, so it can be
 * consumed by a Qt frontend, an SDL frontend, a terminal, or a headless test.
 *
 * The frame layout is always SIM_CGA_FRAME_WIDTH x SIM_CGA_FRAME_HEIGHT
 * (640x200) pixels, regardless of whether the CGA is in text or graphics mode.
 * In text mode each glyph of the embedded 8x8 bitmap font is scaled into the
 * CGA cell grid (80 columns -> 8x8 cells, 40 columns -> 16x8 cells).
 */

/* The "change detection" state stored inside the frame between calls. */
typedef struct {
    uint64_t digest;
    bool digest_valid;
} SimCgaRenderDigest;

typedef struct {
    /* RGBA 0x00RRGGBB, alpha ignored.  Row-major, 640 columns per row. */
    uint32_t pixels[SIM_CGA_FRAME_WIDTH * SIM_CGA_FRAME_HEIGHT];
    /* True when the CGA is in graphics mode; otherwise text mode. */
    bool graphics;
    /* Text mode: number of character columns (80 or 40). 0 in graphics mode. */
    unsigned columns;
    /* Text mode: cursor cell index (row*columns + col), or UINT16_MAX if none. */
    uint16_t cursor;
    /* Text mode: current blink phase, used to swap a blinking attribute. */
    bool blink_phase;
    /* True when the visual output differs from the previous call. */
    bool changed;
    /* Internal change-detection state; the caller preserves it between calls. */
    SimCgaRenderDigest digest_state;
} SimCgaRenderFrame;

/* Renders the committed CGA state into frame. Returns frame. */
void sim_cga_render_frame(const SimCgaState *state, SimCgaRenderFrame *frame);

#endif
