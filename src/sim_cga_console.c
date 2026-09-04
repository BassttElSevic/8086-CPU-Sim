#include "sim/sim_cga_console.h"

#ifdef _WIN32
#include <stdlib.h>
#include <string.h>
#include <windows.h>

enum {
    CGA_TEXT_CELLS = SIM_CGA_TEXT_COLUMNS * SIM_CGA_TEXT_ROWS
};

typedef struct {
    HWND window;
    HFONT font;
    SimKeyboard *keyboard;
    unsigned columns;
    int cell_width;
    int cell_height;
    uint8_t characters[CGA_TEXT_CELLS];
    uint8_t attributes[CGA_TEXT_CELLS];
    uint16_t cursor;
    bool blink_phase;
    bool graphics;
    uint32_t pixels[SIM_CGA_FRAME_WIDTH * SIM_CGA_FRAME_HEIGHT];
    uint64_t visual_digest;
    bool visual_digest_valid;
    bool close_requested;
    bool embedded;
} SimCgaConsole;

static const COLORREF cga_palette[16] = {
    RGB(0, 0, 0), RGB(0, 0, 170), RGB(0, 170, 0), RGB(0, 170, 170),
    RGB(170, 0, 0), RGB(170, 0, 170), RGB(170, 85, 0), RGB(170, 170, 170),
    RGB(85, 85, 85), RGB(85, 85, 255), RGB(85, 255, 85), RGB(85, 255, 255),
    RGB(255, 85, 85), RGB(255, 85, 255), RGB(255, 255, 85), RGB(255, 255, 255)
};

static BITMAPINFO cga_frame_info(void)
{
    BITMAPINFO info = {0};

    info.bmiHeader.biSize = sizeof(info.bmiHeader);
    info.bmiHeader.biWidth = SIM_CGA_FRAME_WIDTH;
    info.bmiHeader.biHeight = -(LONG)SIM_CGA_FRAME_HEIGHT;
    info.bmiHeader.biPlanes = 1u;
    info.bmiHeader.biBitCount = 32u;
    info.bmiHeader.biCompression = BI_RGB;
    return info;
}

static void render_graphics(SimCgaConsole *console, const SimCgaState *state)
{
    bool high_resolution = (state->mode_control & 0x10u) != 0u;
    uint8_t background = state->color_select & 0x0Fu;
    bool intensity = (state->color_select & 0x10u) != 0u;
    static const uint8_t palette_0[4] = {0u, 2u, 4u, 6u};
    static const uint8_t palette_1[4] = {0u, 3u, 5u, 7u};
    const uint8_t *palette = (state->color_select & 0x20u) != 0u ? palette_1 : palette_0;

    if ((state->mode_control & 0x08u) == 0u) {
        memset(console->pixels, 0, sizeof(console->pixels));
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
            console->pixels[y * SIM_CGA_FRAME_WIDTH + x] = cga_palette[colour & 0x0Fu];
        }
    }
}

static uint8_t set1_scancode(WPARAM key)
{
    static const uint8_t digit_codes[] = {
        0x0Bu, 0x02u, 0x03u, 0x04u, 0x05u,
        0x06u, 0x07u, 0x08u, 0x09u, 0x0Au
    };
    static const uint8_t letter_codes[] = {
        0x1Eu, 0x30u, 0x2Eu, 0x20u, 0x12u, 0x21u, 0x22u,
        0x23u, 0x17u, 0x24u, 0x25u, 0x26u, 0x32u, 0x31u,
        0x18u, 0x19u, 0x10u, 0x13u, 0x1Fu, 0x14u, 0x16u,
        0x2Fu, 0x11u, 0x2Du, 0x15u, 0x2Cu
    };

    if (key >= '0' && key <= '9') return digit_codes[key - '0'];
    if (key >= 'A' && key <= 'Z') return letter_codes[key - 'A'];
    if (key >= VK_F1 && key <= VK_F10) return (uint8_t)(0x3Bu + (key - VK_F1));
    if (key == VK_F11) return 0x57u;
    if (key == VK_F12) return 0x58u;
    switch (key) {
    case VK_SHIFT:
    case VK_LSHIFT: return 0x2Au;
    case VK_RSHIFT: return 0x36u;
    case VK_CAPITAL: return 0x3Au;
    case VK_RETURN: return 0x1Cu;
    case VK_BACK: return 0x0Eu;
    case VK_TAB: return 0x0Fu;
    case VK_ESCAPE: return 0x01u;
    case VK_SPACE: return 0x39u;
    case VK_OEM_MINUS: return 0x0Cu;
    case VK_OEM_PLUS: return 0x0Du;
    case VK_OEM_4: return 0x1Au;
    case VK_OEM_6: return 0x1Bu;
    case VK_OEM_5: return 0x2Bu;
    case VK_OEM_1: return 0x27u;
    case VK_OEM_7: return 0x28u;
    case VK_OEM_3: return 0x29u;
    case VK_OEM_COMMA: return 0x33u;
    case VK_OEM_PERIOD: return 0x34u;
    case VK_OEM_2: return 0x35u;
    default: return 0u;
    }
}

static uint8_t set1_break_scancode(WPARAM key)
{
    switch (key) {
    case VK_SHIFT:
    case VK_LSHIFT: return 0xAAu;
    case VK_RSHIFT: return 0xB6u;
    case VK_CAPITAL: return 0xBAu;
    default: return 0u;
    }
}

static uint8_t set1_extended_scancode(WPARAM key)
{
    switch (key) {
    case VK_UP: return 0x48u;
    case VK_DOWN: return 0x50u;
    case VK_LEFT: return 0x4Bu;
    case VK_RIGHT: return 0x4Du;
    case VK_HOME: return 0x47u;
    case VK_END: return 0x4Fu;
    case VK_PRIOR: return 0x49u;
    case VK_NEXT: return 0x51u;
    case VK_INSERT: return 0x52u;
    case VK_DELETE: return 0x53u;
    default: return 0u;
    }
}

/*
 * A CGA text byte always owns exactly one character cell.  Do not pass an
 * entire row to GDI: Unicode fallback can give box-drawing glyphs a different
 * advance width and make them overlap their neighbours.  Clipping each glyph
 * to its own opaque cell preserves the hardware's fixed 80-column grid.
 */
static void draw_cell(const SimCgaConsole *console, HDC dc, unsigned row,
                      unsigned column, uint8_t character, uint8_t attribute,
                      bool blink)
{
    char source = character == 0u ? ' ' : (char)character;
    wchar_t glyph;
    int x = (int)column * console->cell_width;
    int y = (int)row * console->cell_height;
    RECT bounds = {x, y, x + console->cell_width, y + console->cell_height};
    uint8_t foreground = attribute & 0x0Fu;
    uint8_t background = (attribute >> 4u) & 0x07u;

    if ((attribute & 0x80u) != 0u && blink) foreground = background;
    if (MultiByteToWideChar(437u, 0u, &source, 1, &glyph, 1) == 0) {
        glyph = L'?';
    }
    (void)SetTextColor(dc, cga_palette[foreground]);
    (void)SetBkColor(dc, cga_palette[background]);
    (void)ExtTextOutW(dc, x, y, ETO_OPAQUE | ETO_CLIPPED, &bounds, &glyph, 1u, NULL);
}

static void paint_cells(SimCgaConsole *console, HDC dc)
{
    for (unsigned row = 0u; row < SIM_CGA_TEXT_ROWS; ++row) {
        for (unsigned column = 0u; column < console->columns; ++column) {
            unsigned cell = row * console->columns + column;
            draw_cell(console, dc, row, column, console->characters[cell],
                      console->attributes[cell], console->blink_phase);
        }
    }
    if (console->cursor < CGA_TEXT_CELLS) {
        int x = (int)(console->cursor % console->columns) * console->cell_width;
        int y = (int)(console->cursor / console->columns) * console->cell_height;
        RECT cursor = {x, y + console->cell_height - 2,
                       x + console->cell_width, y + console->cell_height};
        (void)FillRect(dc, &cursor, (HBRUSH)GetStockObject(WHITE_BRUSH));
    }
}

static void render_text_pixels(SimCgaConsole *console)
{
    BITMAPINFO info = cga_frame_info();
    HDC dc = CreateCompatibleDC(NULL);
    HBITMAP bitmap;
    HGDIOBJ old_bitmap;
    HGDIOBJ old_font;
    void *bits = NULL;

    if (dc == NULL) return;
    bitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS, &bits, NULL, 0u);
    if (bitmap == NULL || bits == NULL) {
        if (bitmap != NULL) (void)DeleteObject(bitmap);
        (void)DeleteDC(dc);
        return;
    }
    old_bitmap = SelectObject(dc, bitmap);
    old_font = SelectObject(dc, console->font);
    (void)FillRect(dc, &(RECT){0, 0, SIM_CGA_FRAME_WIDTH, SIM_CGA_FRAME_HEIGHT},
                   (HBRUSH)GetStockObject(BLACK_BRUSH));
    (void)SetTextAlign(dc, TA_LEFT | TA_TOP | TA_NOUPDATECP);
    paint_cells(console, dc);
    memcpy(console->pixels, bits, sizeof(console->pixels));
    (void)SelectObject(dc, old_font);
    (void)SelectObject(dc, old_bitmap);
    (void)DeleteObject(bitmap);
    (void)DeleteDC(dc);
}

static uint64_t visual_digest(const SimCgaState *state)
{
    uint64_t hash = UINT64_C(1469598103934665603);
    size_t index;

    if (state == NULL) return 0u;
    hash ^= state->mode_control;
    hash *= UINT64_C(1099511628211);
    hash ^= state->color_select;
    hash *= UINT64_C(1099511628211);
    hash ^= state->blink_phase ? 1u : 0u;
    hash *= UINT64_C(1099511628211);
    for (index = 0u; index < sizeof(state->crtc); ++index) {
        hash ^= state->crtc[index];
        hash *= UINT64_C(1099511628211);
    }
    for (index = 0u; index < sizeof(state->vram); ++index) {
        hash ^= state->vram[index];
        hash *= UINT64_C(1099511628211);
    }
    return hash;
}

/*
 * The launcher sizes this child to the physical CGA viewport aspect ratio.
 * Render the complete fixed grid first, then scale that finished raster.  The
 * simulated 640x200 frame remains unchanged; only the host presentation maps
 * its non-square pixels into the approximately 4:3 CRT display area.
 */
static void paint(SimCgaConsole *console, HDC dc)
{
    RECT client;
    BITMAPINFO info;

    if (!GetClientRect(WindowFromDC(dc), &client)) return;
    if (client.right <= 0 || client.bottom <= 0) return;
    /* The launcher sizes this child to the 4:3 physical viewport.  Stretching
       to its full client area avoids introducing a second letterbox inside
       the already-sized viewport. */
    info = cga_frame_info();
    (void)SetStretchBltMode(dc, HALFTONE);
    (void)StretchDIBits(dc, 0, 0, client.right, client.bottom, 0, 0,
                        SIM_CGA_FRAME_WIDTH, SIM_CGA_FRAME_HEIGHT,
                        console->pixels, &info, DIB_RGB_COLORS, SRCCOPY);
}

static LRESULT CALLBACK window_procedure(HWND window, UINT message,
                                         WPARAM wparam, LPARAM lparam)
{
    SimCgaConsole *console = (SimCgaConsole *)GetWindowLongPtrA(window, GWLP_USERDATA);

    if (message == WM_NCCREATE) {
        CREATESTRUCTA *create = (CREATESTRUCTA *)lparam;
        console = create->lpCreateParams;
        (void)SetWindowLongPtrA(window, GWLP_USERDATA, (LONG_PTR)console);
    }
    switch (message) {
    case WM_GETDLGCODE:
        return DLGC_WANTARROWS | DLGC_WANTCHARS;
    case WM_KEYDOWN:
        if (console != NULL && (lparam & (1L << 30)) == 0) {
            if (wparam == VK_F10 && !console->embedded) {
                console->close_requested = true;
                (void)DestroyWindow(window);
            } else if (console->keyboard != NULL) {
                uint8_t code = set1_scancode(wparam);
                if (code != 0u) {
                    (void)sim_keyboard_enqueue_scancode(console->keyboard, code);
                } else {
                    code = set1_extended_scancode(wparam);
                    if (code != 0u) {
                        (void)sim_keyboard_enqueue_scancode(console->keyboard, 0xE0u);
                        (void)sim_keyboard_enqueue_scancode(console->keyboard, code);
                    }
                }
            }
        }
        return 0;
    case WM_KEYUP:
        if (console != NULL && console->keyboard != NULL) {
            uint8_t code = set1_break_scancode(wparam);
            if (code != 0u) {
                (void)sim_keyboard_enqueue_scancode(console->keyboard, code);
            }
        }
        return 0;
    case WM_PAINT:
        if (console != NULL) {
            PAINTSTRUCT paint_info;
            HDC dc = BeginPaint(window, &paint_info);
            paint(console, dc);
            EndPaint(window, &paint_info);
            return 0;
        }
        break;
    case WM_ERASEBKGND:
        return 1;
    case WM_CLOSE:
        if (console != NULL && !console->embedded) console->close_requested = true;
        (void)DestroyWindow(window);
        return 0;
    case WM_DESTROY:
        if (console != NULL && !console->embedded) console->close_requested = true;
        return 0;
    default:
        break;
    }
    return DefWindowProcA(window, message, wparam, lparam);
}

static bool create_window(SimCgaConsole *console, HWND parent)
{
    static const char class_name[] = "SimCgaDisplayWindow";
    WNDCLASSA window_class = {0};
    RECT bounds = {0, 0, 0, 0};
    TEXTMETRICA metrics;
    HDC desktop;
    HINSTANCE instance = GetModuleHandleA(NULL);

    window_class.lpfnWndProc = window_procedure;
    window_class.hInstance = instance;
    window_class.hCursor = LoadCursor(NULL, IDC_ARROW);
    window_class.lpszClassName = class_name;
    if (RegisterClassA(&window_class) == 0 && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        return false;
    }
    console->font = CreateFontA(-8, 8, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                OEM_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                NONANTIALIASED_QUALITY, FIXED_PITCH | FF_MODERN,
                                "Terminal");
    if (console->font == NULL) return false;
    desktop = GetDC(NULL);
    if (desktop == NULL) return false;
    (void)SelectObject(desktop, console->font);
    if (!GetTextMetricsA(desktop, &metrics)) {
        (void)ReleaseDC(NULL, desktop);
        return false;
    }
    (void)ReleaseDC(NULL, desktop);
    (void)metrics;
    console->columns = SIM_CGA_TEXT_COLUMNS;
    console->cell_width = 8;
    console->cell_height = 8;
    bounds.right = SIM_CGA_TEXT_COLUMNS * console->cell_width;
    bounds.bottom = (bounds.right * 3) / 4;
    console->embedded = parent != NULL;
    if (console->embedded) {
        console->window = CreateWindowExA(0u, class_name, "",
                                          WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                                          0, 0, bounds.right, bounds.bottom,
                                          parent, NULL, instance, console);
    } else {
        if (!AdjustWindowRect(&bounds,
                              WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
                              FALSE)) {
            return false;
        }
        console->window = CreateWindowExA(0u, class_name, "8086-PC-Sim",
                                          WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
                                          CW_USEDEFAULT, CW_USEDEFAULT,
                                          bounds.right - bounds.left,
                                          bounds.bottom - bounds.top,
                                          NULL, NULL, instance, console);
    }
    if (console->window == NULL) return false;
    ShowWindow(console->window, SW_SHOW);
    (void)UpdateWindow(console->window);
    return true;
}

bool sim_cga_console_attach_to_host(SimCga *cga, void *native_host)
{
    SimCgaConsole *console;
    HWND host = (HWND)native_host;

    if (cga == NULL || host == NULL) return false;
    console = (SimCgaConsole *)cga->console_instance;
    if (console != NULL) {
        return console->embedded && GetParent(console->window) == host;
    }
    console = calloc(1u, sizeof(*console));
    if (console == NULL) return false;
    console->keyboard = (SimKeyboard *)cga->console_keyboard;
    cga->console_instance = console;
    if (!create_window(console, host)) {
        DeleteObject(console->font);
        free(console);
        cga->console_instance = NULL;
        return false;
    }
    return true;
}

void sim_cga_console_set_bounds(SimCga *cga, int x, int y, int width, int height)
{
    SimCgaConsole *console;

    if (cga == NULL || width <= 0 || height <= 0) return;
    console = (SimCgaConsole *)cga->console_instance;
    if (console != NULL && console->embedded && console->window != NULL) {
        (void)MoveWindow(console->window, x, y, width, height, TRUE);
    }
}

void sim_cga_console_focus(SimCga *cga)
{
    SimCgaConsole *console;

    if (cga == NULL) return;
    console = (SimCgaConsole *)cga->console_instance;
    if (console != NULL && console->window != NULL) {
        (void)SetFocus(console->window);
    }
}

void sim_cga_console_set_keyboard(SimCga *cga, SimKeyboard *keyboard)
{
    SimCgaConsole *console;

    if (cga == NULL) return;
    cga->console_keyboard = keyboard;
    console = (SimCgaConsole *)cga->console_instance;
    if (console != NULL) console->keyboard = keyboard;
}

bool sim_cga_console_pump_messages(SimCga *cga)
{
    SimCgaConsole *console;
    MSG message;

    if (cga == NULL) return false;
    console = (SimCgaConsole *)cga->console_instance;
    if (console == NULL) return true;
    while (PeekMessageA(&message, NULL, 0u, 0u, PM_REMOVE) != 0) {
        (void)TranslateMessage(&message);
        (void)DispatchMessageA(&message);
    }
    return !console->close_requested;
}

void sim_cga_console_render(SimCga *cga, const SimCgaState *state)
{
    SimCgaConsole *console;
    const uint16_t text_cells = SIM_CGA_VRAM_SIZE / 2u;
    uint16_t start;
    uint16_t cursor;
    uint64_t digest;

    if (cga == NULL || state == NULL) return;
    console = (SimCgaConsole *)cga->console_instance;
    if (console == NULL) {
        console = calloc(1u, sizeof(*console));
        if (console == NULL) return;
        console->keyboard = (SimKeyboard *)cga->console_keyboard;
        cga->console_instance = console;
        if (!create_window(console, NULL)) {
            DeleteObject(console->font);
            free(console);
            cga->console_instance = NULL;
            return;
        }
    }
    digest = visual_digest(state);
    if (console->visual_digest_valid && console->visual_digest == digest) return;
    console->visual_digest = digest;
    console->visual_digest_valid = true;
    console->graphics = (state->mode_control & 0x02u) != 0u;
    if (console->graphics) {
        render_graphics(console, state);
        (void)InvalidateRect(console->window, NULL, FALSE);
        return;
    }
    console->columns = (state->mode_control & 0x01u) != 0u ? 80u : 40u;
    console->cell_width = (int)(SIM_CGA_FRAME_WIDTH / console->columns);
    /* CRTC registers 12/13 select the first displayed character word. */
    start = (uint16_t)(((uint16_t)state->crtc[12] << 8u) | state->crtc[13]);
    start = (uint16_t)(start % text_cells);
    for (unsigned i = 0u; i < SIM_CGA_TEXT_ROWS * console->columns; ++i) {
        uint16_t source_cell = (uint16_t)((start + i) % text_cells);
        console->characters[i] = state->vram[source_cell * 2u];
        console->attributes[i] = state->vram[source_cell * 2u + 1u];
    }
    cursor = (uint16_t)(((uint16_t)state->crtc[14] << 8u) | state->crtc[15]);
    cursor = (uint16_t)((cursor + text_cells - start) % text_cells);
    console->cursor = cursor < SIM_CGA_TEXT_ROWS * console->columns ? cursor : UINT16_MAX;
    console->blink_phase = state->blink_phase;
    render_text_pixels(console);
    (void)InvalidateRect(console->window, NULL, FALSE);
}

void sim_cga_console_destroy(SimCga *cga)
{
    SimCgaConsole *console;

    if (cga == NULL) return;
    console = (SimCgaConsole *)cga->console_instance;
    if (console != NULL) {
        if (console->window != NULL) (void)DestroyWindow(console->window);
        if (console->font != NULL) (void)DeleteObject(console->font);
        free(console);
    }
    cga->console_instance = NULL;
}
#else
void sim_cga_console_set_keyboard(SimCga *cga, SimKeyboard *keyboard)
{
    (void)cga;
    (void)keyboard;
}

bool sim_cga_console_attach_to_host(SimCga *cga, void *native_host)
{
    (void)cga;
    (void)native_host;
    return false;
}

void sim_cga_console_set_bounds(SimCga *cga, int x, int y, int width, int height)
{
    (void)cga;
    (void)x;
    (void)y;
    (void)width;
    (void)height;
}

void sim_cga_console_focus(SimCga *cga)
{
    (void)cga;
}

bool sim_cga_console_pump_messages(SimCga *cga)
{
    return cga != NULL;
}

void sim_cga_console_render(SimCga *cga, const SimCgaState *state)
{
    (void)cga;
    (void)state;
}

void sim_cga_console_destroy(SimCga *cga)
{
    if (cga != NULL) cga->console_instance = NULL;
}
#endif
