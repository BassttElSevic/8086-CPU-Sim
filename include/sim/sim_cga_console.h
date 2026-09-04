#ifndef SIM_SIM_CGA_CONSOLE_H
#define SIM_SIM_CGA_CONSOLE_H

#include "sim_cga.h"
#include "sim_keyboard.h"

/* Windows builds render into a native window rather than a host console. */
void sim_cga_console_set_keyboard(SimCga *cga, SimKeyboard *keyboard);
/* Attach the native CGA child view to a launcher-owned host window. */
bool sim_cga_console_attach_to_host(SimCga *cga, void *native_host);
void sim_cga_console_set_bounds(SimCga *cga, int x, int y, int width, int height);
void sim_cga_console_focus(SimCga *cga);
bool sim_cga_console_pump_messages(SimCga *cga);
void sim_cga_console_render(SimCga *cga, const SimCgaState *state);
void sim_cga_console_destroy(SimCga *cga);

#endif
