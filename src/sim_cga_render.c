#include "sim/sim_cga_render.h"

#include <string.h>

#include "sim/cga_font_8x8.h"

/*
 * CGA palette in RGBA (alpha ignored).  Order matches the 16 CGA colours:
 * black, blue, green, cyan, red, magenta, brown, light grey,
 * dark grey, light blue, light green, light cyan, light red, light magenta,
 * yellow, white.
 */
static const uint32_t cga_palette[16] = {
    0xFF000000u, 0xFF0000AAu, 0xFF00AA00u, 0xFF00AAAAu,
    0xFFAA0000u, 0xFFAA00AAu, 0xFFAA5500u, 0xFFAAAAAAu,
    0xFF555555u, 0xFF5555FFu, 0xFF55FF55u, 0xFF55FFFFu,
    0xFFFF5555u, 0xFFFF55FFu, 0xFFFFFF55u, 0xFFFFFFFFu
};

/*
 * FNV-1a digest over the fields that drive the picture.  This mirrors the
 * Win32 console's visual_digest so the "changed" flag reports real visual
 * differences and not merely "a tick happened".
 */
static uint64_t cga_visual_digest(const SimCgaState *state)
{
    uint64_t hash = UINT64_C(1469598103934665603);
    size_t index;

    if (state == NULL) return UINT64_C(0);
    hash ^= state->mode_control;
    hash *= UINT64_C(1099511628211);
    hash ^= state->color_select;
    hash *= UINT64_C(1099511628211);
    hash ^= state->blink_phase ? 1u : 0u;
    hash *= UINT64_C(1099511628211);
    for (index = 0u; index < SIM_CGA_CRTC_REGISTERS; ++index) {
        hash ^= state->crtc[index];
        hash *= UINT64_C(1099511628211);
    }
    for (index = 0u; index < SIM_CGA_VRAM_SIZE; ++index) {
        hash ^= state->vram[index];
        hash *= UINT64_C(1099511628211);
    }
    return hash;
}

static void render_graphics(const SimCgaState *state, uint32_t *pixels)
{
    bool high_resolution = (state->mode_control & 0x10u) != 0u;
    uint8_t background = state->color_select & 0x0Fu;
    bool intensity = (state->color_select & 0x10u) != 0u;
    static const uint8_t palette_0[4] = {0u, 2u, 4u, 6u};
    static const uint8_t palette_1[4] = {0u, 3u, 5u, 7u};
    const uint8_t *palette = (state->color_select & 0x20u) != 0u ? palette_1 : palette_0;

    if ((state->mode_control & 0x08u) == 0u) {
        memset(pixels, 0, sizeof(uint32_t) * SIM_CGA_FRAME_WIDTH * SIM_CGA_FRAME_HEIGHT);
        return;
    }
    for (unsigned y = 0u; y < SIM_CGA_FRAME_HEIGHT; ++y) {
        unsigned bank = (y & 1u) == 0u ? 0u : 0x2000u;
        unsigned row = y >> 1u;
        for (unsigned x = 0u; x < SIM_CGA_FRAME_WIDTH; ++x) {
            uint8_t colour;
            if (high_resolution) {
                uint8_t byte = state->vram[bank + row * 80u + (x >> 3u)];
                colour = (byte & (uint8_t)(0x80u >> (x & 7u))) != 0u
                    ? (state->color_select & 0x0Fu) : 0u;
            } else {
                unsigned source_x = x >> 1u;
                uint8_t byte = state->vram[bank + row * 80u + (source_x >> 2u)];
                uint8_t value = (uint8_t)((byte >> (6u - 2u * (source_x & 3u))) & 3u);
                colour = value == 0u ? background : palette[value];
                if (intensity && value != 0u) colour = (uint8_t)(colour + 8u);
            }
            pixels[y * SIM_CGA_FRAME_WIDTH + x] = cga_palette[colour & 0x0Fu];
        }
    }
}

static void draw_cell(const SimCgaState *state, uint32_t *pixels,
                      unsigned row, unsigned column,
                      unsigned cell_width, unsigned cell_height,
                      uint8_t character, uint8_t attribute)
{
    uint8_t foreground = attribute & 0x0Fu;
    uint8_t background = (attribute >> 4u) & 0x07u;
    uint32_t fg = cga_palette[foreground];
    uint32_t bg = cga_palette[background];
    unsigned x0 = column * cell_width;
    unsigned y0 = row * cell_height;

    if (((attribute & 0x80u) != 0u) && state->blink_phase) {
        uint32_t tmp = fg;
        fg = bg;
        bg = tmp;
    }
    for (unsigned py = 0u; py < cell_height; ++py) {
        unsigned fy = (py * 8u) / cell_height;
        uint8_t byte = cga_font_8x8[character & 0xFFu][fy];
        for (unsigned px = 0u; px < cell_width; ++px) {
            unsigned fx = (px * 8u) / cell_width;
            bool on = (byte & (uint8_t)(0x80u >> fx)) != 0u;
            pixels[(y0 + py) * SIM_CGA_FRAME_WIDTH + (x0 + px)] = on ? fg : bg;
        }
    }
}

static void render_text(const SimCgaState *state, uint32_t *pixels,
                        SimCgaRenderFrame *frame)
{
    static const uint16_t text_cells = SIM_CGA_VRAM_SIZE / 2u;
    unsigned columns = (state->mode_control & 0x01u) != 0u ? 80u : 40u;
    unsigned cell_width = SIM_CGA_FRAME_WIDTH / columns;
    unsigned cell_height = SIM_CGA_FRAME_HEIGHT / SIM_CGA_TEXT_ROWS;
    uint16_t start;
    uint16_t cursor;

    start = (uint16_t)(((uint16_t)state->crtc[12] << 8u) | state->crtc[13]);
    start = (uint16_t)(start % text_cells);
    cursor = (uint16_t)(((uint16_t)state->crtc[14] << 8u) | state->crtc[15]);
    cursor = (uint16_t)((cursor + text_cells - start) % text_cells);

    frame->graphics = false;
    frame->columns = columns;
    frame->cursor = cursor < (uint16_t)(SIM_CGA_TEXT_ROWS * columns) ? cursor : UINT16_MAX;
    frame->blink_phase = state->blink_phase;

    /* Background fill: black. */
    memset(pixels, 0, sizeof(uint32_t) * SIM_CGA_FRAME_WIDTH * SIM_CGA_FRAME_HEIGHT);
    for (unsigned row = 0u; row < SIM_CGA_TEXT_ROWS; ++row) {
        for (unsigned column = 0u; column < columns; ++column) {
            unsigned cell = row * columns + column;
            uint16_t source_cell = (uint16_t)((start + cell) % text_cells);
            uint8_t character = state->vram[source_cell * 2u];
            uint8_t attribute = state->vram[source_cell * 2u + 1u];
            draw_cell(state, pixels, row, column,
                      cell_width, cell_height, character, attribute);
        }
    }

    /* Hardware block cursor: a white bar across the bottom two rows. */
    if (frame->cursor != UINT16_MAX) {
        unsigned cx = (frame->cursor % columns) * cell_width;
        unsigned cy = (frame->cursor / columns) * cell_height;
        for (unsigned py = 0u; py < 2u && py < cell_height; ++py) {
            unsigned y = cy + (cell_height - 1u - py);
            for (unsigned px = 0u; px < cell_width; ++px) {
                pixels[y * SIM_CGA_FRAME_WIDTH + cx + px] = 0xFFFFFFFFu;
            }
        }
    }
}

void sim_cga_render_frame(const SimCgaState *state, SimCgaRenderFrame *frame)
{
    uint64_t digest;

    if (state == NULL || frame == NULL) return;

    digest = cga_visual_digest(state);
    frame->changed = !frame->digest_state.digest_valid ||
                     frame->digest_state.digest != digest;
    frame->digest_state.digest = digest;
    frame->digest_state.digest_valid = true;

    if ((state->mode_control & 0x02u) != 0u) {
        frame->graphics = true;
        frame->columns = 0u;
        frame->cursor = UINT16_MAX;
        frame->blink_phase = state->blink_phase;
        render_graphics(state, frame->pixels);
    } else {
        render_text(state, frame->pixels, frame);
    }
}
