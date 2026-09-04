#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>

#include "sim/cpu8086.h"
#include "sim/sim_cga_console.h"
#include "sim/sim_system.h"
#include "sim/sim_trace.h"

#ifdef _WIN32
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0600
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commdlg.h>
#include <shellapi.h>

#define LAUNCHER_WINDOW_STYLE \
    (WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_THICKFRAME | \
     WS_CLIPCHILDREN)

enum {
    HDD_CYLINDERS = 256u,
    HDD_HEADS = 16u,
    HDD_SECTORS = 63u,
    CYCLES_PER_GUI_POLL = 2048u,
    CGA_REFRESH_TIMER_ID = 1u,
    CGA_REFRESH_PERIOD_MS = 33u,
    UTF8_PATH_CAPACITY = MAX_PATH * 4u,
    WINDOW_CLIENT_WIDTH = 1530,
    WINDOW_CLIENT_HEIGHT = 720,
    WINDOW_MIN_CLIENT_WIDTH = 1530,
    WINDOW_MIN_CLIENT_HEIGHT = 720,
    DISPLAY_X = 20,
    DISPLAY_Y = 62,
    DISPLAY_INITIAL_WIDTH = 1080,
    DISPLAY_RIGHT_MARGIN = 20,
    DISPLAY_BOTTOM_MARGIN = 20,
    DISPLAY_ASPECT_WIDTH = 4,
    DISPLAY_ASPECT_HEIGHT = 3,
    PANEL_X = 1140,
    PANEL_WIDTH = 345,
    PANEL_RIGHT_MARGIN = 45,
    PATH_COMBO_WIDTH = 240,
    PATH_BUTTON_X = PANEL_X + 248,
    PATH_BUTTON_WIDTH = 97,
    CONTROL_LABEL_HEIGHT = 26,
    CONTROL_ROW_HEIGHT = 34,
    BIOS_LABEL_Y = 58,
    BIOS_ROW_Y = 84,
    FLOPPY_LABEL_Y = 132,
    FLOPPY_ROW_Y = 158,
    FLOPPY_EJECT_Y = 202,
    HDD_LABEL_Y = 250,
    HDD_ROW_Y = 276,
    HDD_BUTTON_Y = 320,
    MOUNT_BUTTON_Y = 358,
    MOUNTED_LABEL_Y = 404,
    STATUS_Y = 430,
    STATUS_HEIGHT = 96,
    SPEED_LABEL_Y = 540,
    SPEED_ROW_Y = 566
};

enum {
    ID_BIOS_COMBO = 100,
    ID_BIOS_BROWSE,
    ID_FLOPPY_COMBO,
    ID_FLOPPY_BROWSE,
    ID_FLOPPY_EJECT,
    ID_FLOPPY_RELOAD,
    ID_HDD_COMBO,
    ID_HDD_BROWSE,
    ID_HDD_EJECT,
    ID_HDD_NEW,
    ID_MOUNT_RESTART,
    ID_SPEED_COMBO,
    ID_STATUS
};

typedef struct {
    HWND window;
    HWND cga_label;
    HWND media_label;
    HWND bios_label;
    HWND floppy_label;
    HWND hdd_label;
    HWND mounted_label;
    HWND speed_label;
    HWND bios_combo;
    HWND floppy_combo;
    HWND hdd_combo;
    HWND speed_combo;
    HWND bios_browse;
    HWND floppy_browse;
    HWND floppy_eject;
    HWND floppy_reload;
    HWND hdd_browse;
    HWND hdd_eject;
    HWND hdd_new;
    HWND mount_restart;
    HWND status;
    SimSystem system;
    bool system_ready;
    bool running;
    bool hdd_should_flush;
    bool hdd_create_requested;
    unsigned speed_multiplier;
    uint16_t last_int13_sequence;
    char mounted_hdd_path[UTF8_PATH_CAPACITY];
} LauncherUi;

static void report_error(HWND owner, const wchar_t *message)
{
    (void)MessageBoxW(owner, message, L"8086-PC-Sim", MB_ICONERROR | MB_OK);
}

static void set_process_dpi_aware(void)
{
    typedef BOOL(WINAPI *SetProcessDpiAwareFunction)(void);
    HMODULE user32 = LoadLibraryW(L"user32.dll");
    SetProcessDpiAwareFunction set_dpi_aware;

    if (user32 == NULL) return;
    set_dpi_aware = (SetProcessDpiAwareFunction)GetProcAddress(
        user32, "SetProcessDPIAware");
    if (set_dpi_aware != NULL) (void)set_dpi_aware();
    (void)FreeLibrary(user32);
}

static bool wide_to_utf8(const wchar_t *source, char *destination, size_t capacity)
{
    if (source == NULL || destination == NULL || capacity == 0u ||
        capacity > (size_t)INT_MAX) {
        return false;
    }
    return WideCharToMultiByte(CP_UTF8, 0u, source, -1, destination,
                               (int)capacity, NULL, NULL) > 0;
}

static bool path_is_file(const wchar_t *path)
{
    DWORD attributes;

    if (path == NULL || *path == L'\0') return false;
    attributes = GetFileAttributesW(path);
    return attributes != INVALID_FILE_ATTRIBUTES &&
           (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0u;
}

static void trim_path(wchar_t *path)
{
    wchar_t *first;
    wchar_t *last;

    if (path == NULL) return;
    first = path;
    while (*first == L' ' || *first == L'\t') ++first;
    if (first != path) {
        memmove(path, first, (wcslen(first) + 1u) * sizeof(*path));
    }
    last = path + wcslen(path);
    while (last > path && (last[-1] == L' ' || last[-1] == L'\t')) --last;
    *last = L'\0';
}

static void combo_set_path(HWND combo, const wchar_t *path)
{
    LRESULT index;

    if (combo == NULL) return;
    if (path == NULL || *path == L'\0') {
        (void)SetWindowTextW(combo, L"");
        return;
    }
    index = SendMessageW(combo, CB_FINDSTRINGEXACT, (WPARAM)-1, (LPARAM)path);
    if (index == CB_ERR) {
        (void)SendMessageW(combo, CB_ADDSTRING, 0u, (LPARAM)path);
    }
    (void)SetWindowTextW(combo, path);
}

static bool combo_get_path(HWND combo, wchar_t path[MAX_PATH])
{
    if (combo == NULL || GetWindowTextW(combo, path, MAX_PATH) <= 0) return false;
    trim_path(path);
    return path[0] != L'\0';
}

static void set_status(LauncherUi *ui, const wchar_t *message)
{
    if (ui != NULL && ui->status != NULL) {
        (void)SetWindowTextW(ui->status, message);
    }
}

static void update_media_status(LauncherUi *ui);

static void render_cga(LauncherUi *ui)
{
    const SimCgaState *state;

    if (ui == NULL || !ui->system_ready) return;
    state = sim_cga_current_state(&ui->system.cga, &ui->system.state);
    if (state != NULL) sim_cga_console_render(&ui->system.cga, state);
}

static bool initialize_speed_selector(LauncherUi *ui)
{
    static const wchar_t *const labels[] = {L"1x", L"2x", L"4x", L"8x"};
    static const unsigned multipliers[] = {1u, 2u, 4u, 8u};
    size_t index;

    if (ui == NULL || ui->speed_combo == NULL) return false;
    for (index = 0u; index < sizeof(labels) / sizeof(labels[0]); ++index) {
        LRESULT item = SendMessageW(ui->speed_combo, CB_ADDSTRING, 0u,
                                   (LPARAM)labels[index]);
        if (item == CB_ERR || item == CB_ERRSPACE ||
            SendMessageW(ui->speed_combo, CB_SETITEMDATA, (WPARAM)item,
                         (LPARAM)multipliers[index]) == CB_ERR) {
            return false;
        }
    }
    ui->speed_multiplier = 1u;
    return SendMessageW(ui->speed_combo, CB_SETCURSEL, 0u, 0u) != CB_ERR;
}

static void select_speed_multiplier(LauncherUi *ui)
{
    LRESULT selected;
    LRESULT item_data;

    if (ui == NULL || ui->speed_combo == NULL) return;
    selected = SendMessageW(ui->speed_combo, CB_GETCURSEL, 0u, 0u);
    if (selected == CB_ERR) return;
    item_data = SendMessageW(ui->speed_combo, CB_GETITEMDATA, (WPARAM)selected, 0u);
    if (item_data == CB_ERR || item_data <= 0) return;
    ui->speed_multiplier = (unsigned)item_data;
    update_media_status(ui);
    if (ui->system_ready && ui->running) sim_cga_console_focus(&ui->system.cga);
}

static void update_media_status(LauncherUi *ui)
{
    wchar_t floppy[MAX_PATH] = L"";
    wchar_t hdd[MAX_PATH] = L"";
    wchar_t text[256];
    const wchar_t *floppy_state;
    const wchar_t *hdd_state;

    if (ui == NULL) return;
    (void)combo_get_path(ui->floppy_combo, floppy);
    (void)combo_get_path(ui->hdd_combo, hdd);
    floppy_state = floppy[0] == L'\0' ? L"empty" : ui->running ? L"mounted" : L"ready";
    hdd_state = hdd[0] == L'\0' ? L"empty" : ui->running ? L"mounted, writable" : L"ready";
    (void)wsprintfW(text, L"A: %s\r\nC: %s\r\nSimulation speed: %ux",
                    floppy_state, hdd_state,
                    ui->speed_multiplier == 0u ? 1u : ui->speed_multiplier);
    set_status(ui, text);
}

static bool read_ram_word(const SimRam *ram, uint32_t address, uint16_t *value)
{
    uint8_t low;
    uint8_t high;

    if (ram == NULL || value == NULL || !sim_ram_peek_byte(ram, address, &low) ||
        !sim_ram_peek_byte(ram, address + 1u, &high)) {
        return false;
    }
    *value = (uint16_t)(low | ((uint16_t)high << 8u));
    return true;
}

static void update_int13_diagnostic(LauncherUi *ui)
{
    enum {
        INT13_DIAGNOSTIC_BASE = 0x4E0u,
        INT13_DIAGNOSTIC_SEQUENCE = INT13_DIAGNOSTIC_BASE,
        INT13_DIAGNOSTIC_AX = INT13_DIAGNOSTIC_BASE + 2u,
        INT13_DIAGNOSTIC_CX = INT13_DIAGNOSTIC_BASE + 6u,
        INT13_DIAGNOSTIC_DX = INT13_DIAGNOSTIC_BASE + 8u,
        INT13_DIAGNOSTIC_RESULT = INT13_DIAGNOSTIC_BASE + 10u
    };
    uint16_t sequence;
    uint16_t ax;
    uint16_t cx;
    uint16_t dx;
    uint8_t result;
    wchar_t text[256];
    const wchar_t *result_text;

    if (ui == NULL || !ui->system_ready ||
        !read_ram_word(&ui->system.ram, INT13_DIAGNOSTIC_SEQUENCE, &sequence) ||
        sequence == 0u || sequence == ui->last_int13_sequence ||
        !read_ram_word(&ui->system.ram, INT13_DIAGNOSTIC_AX, &ax) ||
        !read_ram_word(&ui->system.ram, INT13_DIAGNOSTIC_CX, &cx) ||
        !read_ram_word(&ui->system.ram, INT13_DIAGNOSTIC_DX, &dx) ||
        !sim_ram_peek_byte(&ui->system.ram, INT13_DIAGNOSTIC_RESULT, &result)) {
        return;
    }
    ui->last_int13_sequence = sequence;
    result_text = result == 0u ? L"success" : result == 1u ? L"failure" : L"pending";
    (void)wsprintfW(text,
                    L"A: mounted\r\nC: mounted, writable\r\n"
                    L"INT 13h AH=%02X AL=%02X CHS=%02X/%02X/%02X DL=%02X: %s\r\n"
                    L"Simulation speed: %ux",
                    (unsigned)(ax >> 8u), (unsigned)(ax & 0xFFu),
                    (unsigned)(cx >> 8u), (unsigned)(dx >> 8u),
                    (unsigned)(cx & 0x3Fu), (unsigned)(dx & 0xFFu), result_text,
                    ui->speed_multiplier == 0u ? 1u : ui->speed_multiplier);
    set_status(ui, text);
}

static bool choose_file(HWND owner, HWND combo, const wchar_t *filter)
{
    OPENFILENAMEW dialog = {0};
    wchar_t path[MAX_PATH] = L"";

    (void)combo_get_path(combo, path);
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = owner;
    dialog.lpstrFile = path;
    dialog.nMaxFile = MAX_PATH;
    dialog.lpstrFilter = filter;
    dialog.nFilterIndex = 1u;
    dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY;
    if (GetOpenFileNameW(&dialog) == 0) return false;
    combo_set_path(combo, path);
    return true;
}

static bool choose_new_file(HWND owner, HWND combo, const wchar_t *filter)
{
    OPENFILENAMEW dialog = {0};
    wchar_t path[MAX_PATH] = L"";

    (void)combo_get_path(combo, path);
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = owner;
    dialog.lpstrFile = path;
    dialog.nMaxFile = MAX_PATH;
    dialog.lpstrFilter = filter;
    dialog.nFilterIndex = 1u;
    dialog.lpstrDefExt = L"img";
    dialog.Flags = OFN_PATHMUSTEXIST | OFN_HIDEREADONLY;
    if (GetSaveFileNameW(&dialog) == 0) return false;
    if (path_is_file(path)) {
        report_error(owner, L"The selected output path already exists. Choose a new file.");
        return false;
    }
    combo_set_path(combo, path);
    return true;
}

static bool create_new_hdd_file(HWND owner, const wchar_t *path)
{
    char utf8_path[UTF8_PATH_CAPACITY];

    if (path == NULL || !wide_to_utf8(path, utf8_path, sizeof(utf8_path)) ||
        !sim_disk_create_file(utf8_path, HDD_CYLINDERS, HDD_HEADS, HDD_SECTORS)) {
        report_error(owner, L"Unable to create the new virtual disk image.");
        return false;
    }
    return true;
}

static void default_bios_path(wchar_t path[MAX_PATH])
{
    wchar_t resolved[MAX_PATH];
    wchar_t *separator;
    DWORD length;

    path[0] = L'\0';
    length = GetModuleFileNameW(NULL, path, MAX_PATH);
    if (length == 0u || length >= MAX_PATH) {
        (void)wcscpy(path, L"firmware\\pc_compat_bios.bin");
        return;
    }
    separator = wcsrchr(path, L'\\');
    if (separator == NULL || (size_t)(separator - path) + 31u >= MAX_PATH) {
        (void)wcscpy(path, L"firmware\\pc_compat_bios.bin");
        return;
    }
    (void)wcscpy(separator, L"\\..\\firmware\\pc_compat_bios.bin");
    if (GetFullPathNameW(path, MAX_PATH, resolved, NULL) != 0u) {
        (void)wcscpy(path, resolved);
    }
}

static bool save_and_destroy_system(LauncherUi *ui)
{
    bool saved = true;

    if (ui == NULL || !ui->system_ready) return true;
    ui->running = false;
    if (ui->hdd_should_flush && ui->mounted_hdd_path[0] != '\0' &&
        !sim_disk_flush_file(&ui->system.disk, 1u, ui->mounted_hdd_path)) {
        report_error(ui->window, L"Unable to write the mounted virtual disk image.");
        saved = false;
    }
    sim_system_destroy(&ui->system);
    ui->system_ready = false;
    ui->hdd_should_flush = false;
    ui->mounted_hdd_path[0] = '\0';
    return saved;
}

static void move_control(HWND control, int x, int y, int width, int height)
{
    if (control != NULL) (void)MoveWindow(control, x, y, width, height, TRUE);
}

static void layout_controls(LauncherUi *ui, int client_width, int client_height)
{
    RECT display;
    int panel_x;

    if (ui == NULL) return;
    panel_x = client_width - PANEL_WIDTH - PANEL_RIGHT_MARGIN;
    if (panel_x < PANEL_X) panel_x = PANEL_X;

    /* Model the approximately 4:3 physical CGA display while keeping the
       simulated frame buffer at its real 640x200 addressable raster. */
    display.left = DISPLAY_X;
    display.top = DISPLAY_Y;
    display.right = panel_x - DISPLAY_RIGHT_MARGIN;
    display.bottom = client_height - DISPLAY_BOTTOM_MARGIN;
    if (display.right <= display.left || display.bottom <= display.top) return;
    {
        int available_width = display.right - display.left;
        int available_height = display.bottom - display.top;
        int width = available_width;
        int height = (width * DISPLAY_ASPECT_HEIGHT) / DISPLAY_ASPECT_WIDTH;
        if (height > available_height) {
            height = available_height;
            width = (height * DISPLAY_ASPECT_WIDTH) / DISPLAY_ASPECT_HEIGHT;
        }
        display.left += (available_width - width) / 2;
        display.top += (available_height - height) / 2;
        display.right = display.left + width;
        display.bottom = display.top + height;
    }

    move_control(ui->cga_label, display.left, 24,
                 display.right - display.left, CONTROL_LABEL_HEIGHT);
    move_control(ui->media_label, panel_x, 24, PANEL_WIDTH, CONTROL_LABEL_HEIGHT);
    move_control(ui->bios_label, panel_x, BIOS_LABEL_Y, PANEL_WIDTH,
                 CONTROL_LABEL_HEIGHT);
    move_control(ui->bios_combo, panel_x, BIOS_ROW_Y, PATH_COMBO_WIDTH, 240);
    move_control(ui->bios_browse, panel_x + 248, BIOS_ROW_Y,
                 PATH_BUTTON_WIDTH, CONTROL_ROW_HEIGHT);
    move_control(ui->floppy_label, panel_x, FLOPPY_LABEL_Y, PANEL_WIDTH,
                 CONTROL_LABEL_HEIGHT);
    move_control(ui->floppy_combo, panel_x, FLOPPY_ROW_Y, PATH_COMBO_WIDTH, 240);
    move_control(ui->floppy_browse, panel_x + 248, FLOPPY_ROW_Y,
                 PATH_BUTTON_WIDTH, CONTROL_ROW_HEIGHT);
    move_control(ui->floppy_eject, panel_x + 248, FLOPPY_EJECT_Y,
                 PATH_BUTTON_WIDTH, CONTROL_ROW_HEIGHT);
    move_control(ui->floppy_reload, panel_x, FLOPPY_EJECT_Y,
                 PATH_COMBO_WIDTH, CONTROL_ROW_HEIGHT);
    move_control(ui->hdd_label, panel_x, HDD_LABEL_Y, PANEL_WIDTH,
                 CONTROL_LABEL_HEIGHT);
    move_control(ui->hdd_combo, panel_x, HDD_ROW_Y, PATH_COMBO_WIDTH, 240);
    move_control(ui->hdd_browse, panel_x + 248, HDD_ROW_Y,
                 PATH_BUTTON_WIDTH, CONTROL_ROW_HEIGHT);
    move_control(ui->hdd_new, panel_x, HDD_BUTTON_Y, 80, CONTROL_ROW_HEIGHT);
    move_control(ui->hdd_eject, panel_x + 248, HDD_BUTTON_Y,
                 PATH_BUTTON_WIDTH, CONTROL_ROW_HEIGHT);
    move_control(ui->mount_restart, panel_x, MOUNT_BUTTON_Y, PANEL_WIDTH, 34);
    move_control(ui->mounted_label, panel_x, MOUNTED_LABEL_Y, PANEL_WIDTH,
                 CONTROL_LABEL_HEIGHT);
    move_control(ui->status, panel_x, STATUS_Y, PANEL_WIDTH, STATUS_HEIGHT);
    move_control(ui->speed_label, panel_x, SPEED_LABEL_Y, PANEL_WIDTH,
                 CONTROL_LABEL_HEIGHT);
    move_control(ui->speed_combo, panel_x, SPEED_ROW_Y, PANEL_WIDTH,
                 CONTROL_ROW_HEIGHT);

    if (ui->system_ready) {
        sim_cga_console_set_bounds(&ui->system.cga, display.left, display.top,
                                   display.right - display.left,
                                   display.bottom - display.top);
    }
}

static bool mount_selected_media(LauncherUi *ui)
{
    SimRamConfig ram = {0u, 0x100000u, 0u, 0u, 0u};
    const SimCgaState *cga_state;
    wchar_t bios_wide[MAX_PATH] = L"";
    wchar_t floppy_wide[MAX_PATH] = L"";
    wchar_t hdd_wide[MAX_PATH] = L"";
    char bios_path[UTF8_PATH_CAPACITY];
    char floppy_path[UTF8_PATH_CAPACITY];
    char hdd_path[UTF8_PATH_CAPACITY];
    bool floppy_present;
    bool hdd_present;
    bool floppy_loaded = true;
    bool hdd_loaded = true;
    bool hdd_create_failed = false;

    if (ui == NULL) return false;
    if (!combo_get_path(ui->bios_combo, bios_wide) || !path_is_file(bios_wide)) {
        report_error(ui->window, L"Select an existing BIOS ROM file.");
        return false;
    }
    floppy_present = combo_get_path(ui->floppy_combo, floppy_wide);
    hdd_present = combo_get_path(ui->hdd_combo, hdd_wide);
    if (!floppy_present && !hdd_present) {
        report_error(ui->window, L"Mount a floppy image, a virtual disk image, or both.");
        return false;
    }
    if ((floppy_present && !path_is_file(floppy_wide)) ||
        (hdd_present && !ui->hdd_create_requested && !path_is_file(hdd_wide))) {
        report_error(ui->window, L"A selected media path does not name an existing file.");
        return false;
    }
    if (!wide_to_utf8(bios_wide, bios_path, sizeof(bios_path)) ||
        (floppy_present && !wide_to_utf8(floppy_wide, floppy_path, sizeof(floppy_path))) ||
        (hdd_present && !wide_to_utf8(hdd_wide, hdd_path, sizeof(hdd_path)))) {
        report_error(ui->window, L"A selected path cannot be converted to UTF-8.");
        return false;
    }

    (void)save_and_destroy_system(ui);
    if (!sim_system_init(&ui->system, &ram)) {
        report_error(ui->window, L"Unable to initialize the simulated PC.");
        return false;
    }
    ui->system_ready = true;
    sim_trace_set_enabled(&ui->system.trace, false);
    sim_cga_set_console_enabled(&ui->system.cga, true);
    sim_cga_console_set_keyboard(&ui->system.cga, &ui->system.keyboard);
    if (!sim_cga_console_attach_to_host(&ui->system.cga, ui->window)) {
        report_error(ui->window, L"Unable to create the CGA display surface.");
        goto failed;
    }
    {
        RECT client;
        if (GetClientRect(ui->window, &client)) {
            layout_controls(ui, client.right - client.left, client.bottom - client.top);
        }
    }
    cga_state = sim_cga_current_state(&ui->system.cga, &ui->system.state);
    if (cga_state != NULL) sim_cga_console_render(&ui->system.cga, cga_state);

    if (floppy_present) {
        floppy_loaded = sim_disk_mount_floppy_file(&ui->system.disk, 0u,
                                                    floppy_path, false);
    }
    if (hdd_present) {
        if (ui->hdd_create_requested) {
            if (!sim_disk_create_file(hdd_path, HDD_CYLINDERS, HDD_HEADS,
                                      HDD_SECTORS)) {
                hdd_loaded = false;
                hdd_create_failed = true;
            } else {
                ui->hdd_create_requested = false;
            }
        }
        if (hdd_loaded) {
            hdd_loaded = sim_disk_mount_file(&ui->system.disk, 1u, hdd_path,
                                             HDD_CYLINDERS, HDD_HEADS,
                                             HDD_SECTORS, true);
        }
    }
    if (!floppy_loaded || !hdd_loaded || !sim_rom_load_file(&ui->system.rom, bios_path) ||
        !sim_system_publish_bios_configuration(&ui->system)) {
        report_error(ui->window, !floppy_loaded
                     ? L"The floppy image geometry could not be determined."
                     : !hdd_loaded
                       ? (hdd_create_failed
                          ? L"Unable to create the new virtual disk image."
                          : L"The virtual disk image must be a 256 x 16 x 63 raw image.")
                       : L"Unable to load the BIOS ROM or publish the media configuration.");
        goto failed;
    }

    ui->hdd_should_flush = hdd_present;
    if (hdd_present) (void)strcpy(ui->mounted_hdd_path, hdd_path);
    ui->running = true;
    ui->hdd_create_requested = false;
    update_media_status(ui);
    sim_cga_console_focus(&ui->system.cga);
    return true;

failed:
    sim_system_destroy(&ui->system);
    ui->system_ready = false;
    ui->hdd_should_flush = false;
    ui->mounted_hdd_path[0] = '\0';
    return false;
}

static bool reload_selected_floppy(LauncherUi *ui)
{
    wchar_t floppy_wide[MAX_PATH] = L"";
    char floppy_path[UTF8_PATH_CAPACITY];

    if (ui == NULL || !ui->system_ready || !ui->running) {
        report_error(ui == NULL ? NULL : ui->window,
                     L"Start the simulated PC before reloading its floppy image.");
        return false;
    }
    if (!combo_get_path(ui->floppy_combo, floppy_wide) || !path_is_file(floppy_wide)) {
        report_error(ui->window, L"Select an existing floppy image to reload.");
        return false;
    }
    if (!wide_to_utf8(floppy_wide, floppy_path, sizeof(floppy_path)) ||
        !sim_disk_reload_floppy_file(&ui->system.disk, &ui->system.state,
                                     0u, floppy_path, false) ||
        !sim_system_publish_bios_configuration(&ui->system)) {
        report_error(ui->window, L"Unable to reload the selected floppy image.");
        return false;
    }
    update_media_status(ui);
    {
        wchar_t status[128];
        (void)wsprintfW(status, L"A: floppy image reloaded\r\nC: mounted, writable\r\n"
                        L"Simulation speed: %ux",
                        ui->speed_multiplier == 0u ? 1u : ui->speed_multiplier);
        set_status(ui, status);
    }
    sim_cga_console_focus(&ui->system.cga);
    return true;
}

static void eject_floppy(LauncherUi *ui)
{
    if (ui == NULL) return;
    if (ui->system_ready &&
        (!sim_disk_eject_media(&ui->system.disk, &ui->system.state, 0u) ||
         !sim_system_publish_bios_configuration(&ui->system))) {
        report_error(ui->window, L"Unable to eject the mounted floppy image.");
        return;
    }
    combo_set_path(ui->floppy_combo, L"");
    update_media_status(ui);
}

static void run_simulation_slice(LauncherUi *ui)
{
    const Cpu8086State *cpu;
    uint64_t cycles_due;

    if (ui == NULL || !ui->system_ready || !ui->running) return;
    cycles_due = (uint64_t)CYCLES_PER_GUI_POLL *
                 (ui->speed_multiplier == 0u ? 1u : ui->speed_multiplier);
    for (uint64_t i = 0u; i < cycles_due; ++i) {
        if (!sim_system_tick(&ui->system)) {
            ui->running = false;
            report_error(ui->window, L"Simulation kernel error.");
            return;
        }
        cpu = cpu8086_current_state(&ui->system.cpu, &ui->system.state);
        if (cpu != NULL && cpu->eu.phase == CPU8086_EU_FAULTED) {
            wchar_t message[128];
            (void)wsprintfW(message, L"CPU fault %u at %04X:%04X.",
                            (unsigned)cpu->eu.fault,
                            cpu->segment[CPU8086_SEG_CS], cpu->ip);
            ui->running = false;
            report_error(ui->window, message);
            update_media_status(ui);
            return;
        }
    }
    update_int13_diagnostic(ui);
}

static HWND make_control(HWND parent, const wchar_t *class_name, const wchar_t *text,
                         DWORD style, int x, int y, int width, int height, int identifier)
{
    HWND control = CreateWindowExW(0u, class_name, text, WS_CHILD | WS_VISIBLE | style,
                                   x, y, width, height, parent,
                                   (HMENU)(INT_PTR)identifier, GetModuleHandleW(NULL), NULL);
    if (control != NULL) {
        (void)SendMessageW(control, WM_SETFONT,
                           (WPARAM)GetStockObject(DEFAULT_GUI_FONT), TRUE);
    }
    return control;
}

static bool create_controls(LauncherUi *ui)
{
    if (ui == NULL || ui->window == NULL) return false;
    ui->cga_label = make_control(ui->window, L"STATIC", L"CGA display", SS_LEFT,
                                 DISPLAY_X, 24, DISPLAY_INITIAL_WIDTH,
                                 CONTROL_LABEL_HEIGHT, 0);
    ui->media_label = make_control(ui->window, L"STATIC", L"Machine media", SS_LEFT,
                                   PANEL_X, 24, PANEL_WIDTH,
                                   CONTROL_LABEL_HEIGHT, 0);
    ui->bios_label = make_control(ui->window, L"STATIC", L"BIOS ROM", SS_LEFT,
                                  PANEL_X, BIOS_LABEL_Y, PANEL_WIDTH,
                                  CONTROL_LABEL_HEIGHT, 0);
    ui->floppy_label = make_control(ui->window, L"STATIC", L"Floppy A:", SS_LEFT,
                                    PANEL_X, FLOPPY_LABEL_Y, PANEL_WIDTH,
                                    CONTROL_LABEL_HEIGHT, 0);
    ui->hdd_label = make_control(ui->window, L"STATIC", L"Virtual disk C:", SS_LEFT,
                                 PANEL_X, HDD_LABEL_Y, PANEL_WIDTH,
                                 CONTROL_LABEL_HEIGHT, 0);
    ui->mounted_label = make_control(ui->window, L"STATIC", L"Mounted slots", SS_LEFT,
                                     PANEL_X, MOUNTED_LABEL_Y, PANEL_WIDTH,
                                     CONTROL_LABEL_HEIGHT, 0);
    ui->speed_label = make_control(ui->window, L"STATIC", L"Simulation speed", SS_LEFT,
                                   PANEL_X, SPEED_LABEL_Y, PANEL_WIDTH,
                                   CONTROL_LABEL_HEIGHT, 0);
    if (ui->cga_label == NULL || ui->media_label == NULL || ui->bios_label == NULL ||
        ui->floppy_label == NULL || ui->hdd_label == NULL ||
        ui->mounted_label == NULL || ui->speed_label == NULL) return false;
    ui->bios_combo = make_control(ui->window, L"COMBOBOX", L"",
                                  CBS_DROPDOWN | CBS_AUTOHSCROLL | WS_VSCROLL,
                                  PANEL_X, BIOS_ROW_Y, PATH_COMBO_WIDTH, 240,
                                  ID_BIOS_COMBO);
    ui->floppy_combo = make_control(ui->window, L"COMBOBOX", L"",
                                    CBS_DROPDOWN | CBS_AUTOHSCROLL | WS_VSCROLL,
                                    PANEL_X, FLOPPY_ROW_Y, PATH_COMBO_WIDTH, 240,
                                    ID_FLOPPY_COMBO);
    ui->hdd_combo = make_control(ui->window, L"COMBOBOX", L"",
                                 CBS_DROPDOWN | CBS_AUTOHSCROLL | WS_VSCROLL,
                                 PANEL_X, HDD_ROW_Y, PATH_COMBO_WIDTH, 240,
                                 ID_HDD_COMBO);
    ui->speed_combo = make_control(ui->window, L"COMBOBOX", L"",
                                   CBS_DROPDOWNLIST | WS_VSCROLL,
                                   PANEL_X, SPEED_ROW_Y, PANEL_WIDTH, 160,
                                   ID_SPEED_COMBO);
    if (ui->bios_combo == NULL || ui->floppy_combo == NULL || ui->hdd_combo == NULL ||
        ui->speed_combo == NULL || !initialize_speed_selector(ui) ||
        (ui->bios_browse = make_control(ui->window, L"BUTTON", L"Browse...",
                                        BS_PUSHBUTTON, PATH_BUTTON_X, BIOS_ROW_Y,
                                        PATH_BUTTON_WIDTH, CONTROL_ROW_HEIGHT,
                                        ID_BIOS_BROWSE)) == NULL ||
        (ui->floppy_browse = make_control(ui->window, L"BUTTON", L"Browse...",
                                          BS_PUSHBUTTON, PATH_BUTTON_X, FLOPPY_ROW_Y,
                                          PATH_BUTTON_WIDTH, CONTROL_ROW_HEIGHT,
                                          ID_FLOPPY_BROWSE)) == NULL ||
        (ui->floppy_eject = make_control(ui->window, L"BUTTON", L"Eject",
                                         BS_PUSHBUTTON, PATH_BUTTON_X, FLOPPY_EJECT_Y,
                                         PATH_BUTTON_WIDTH, CONTROL_ROW_HEIGHT,
                                         ID_FLOPPY_EJECT)) == NULL ||
        (ui->floppy_reload = make_control(ui->window, L"BUTTON", L"Reload floppy image",
                                          BS_PUSHBUTTON, PANEL_X, FLOPPY_EJECT_Y,
                                          PATH_COMBO_WIDTH, CONTROL_ROW_HEIGHT,
                                          ID_FLOPPY_RELOAD)) == NULL ||
        (ui->hdd_browse = make_control(ui->window, L"BUTTON", L"Browse...",
                                       BS_PUSHBUTTON, PATH_BUTTON_X, HDD_ROW_Y,
                                       PATH_BUTTON_WIDTH, CONTROL_ROW_HEIGHT,
                                       ID_HDD_BROWSE)) == NULL ||
        (ui->hdd_eject = make_control(ui->window, L"BUTTON", L"Eject",
                                      BS_PUSHBUTTON, PATH_BUTTON_X, HDD_BUTTON_Y,
                                      PATH_BUTTON_WIDTH, CONTROL_ROW_HEIGHT,
                                      ID_HDD_EJECT)) == NULL ||
        (ui->hdd_new = make_control(ui->window, L"BUTTON", L"New...",
                                    BS_PUSHBUTTON, PANEL_X, HDD_BUTTON_Y, 80,
                                    CONTROL_ROW_HEIGHT, ID_HDD_NEW)) == NULL ||
        (ui->mount_restart = make_control(ui->window, L"BUTTON", L"Mount / Restart",
                                          BS_DEFPUSHBUTTON, PANEL_X, MOUNT_BUTTON_Y,
                                          PANEL_WIDTH, 34, ID_MOUNT_RESTART)) == NULL) {
        return false;
    }
    ui->status = make_control(ui->window, L"STATIC", L"",
                              SS_LEFT | SS_NOPREFIX,
                              PANEL_X, STATUS_Y, PANEL_WIDTH, STATUS_HEIGHT, ID_STATUS);
    return ui->status != NULL;
}

static void set_initial_configuration(LauncherUi *ui)
{
    LPWSTR *arguments;
    int count;
    int index;
    wchar_t bios[MAX_PATH];

    if (ui == NULL) return;
    default_bios_path(bios);
    combo_set_path(ui->bios_combo, bios);
    arguments = CommandLineToArgvW(GetCommandLineW(), &count);
    if (arguments == NULL) return;
    for (index = 1; index < count; ++index) {
        if (wcscmp(arguments[index], L"--bios") == 0 && index + 1 < count) {
            combo_set_path(ui->bios_combo, arguments[++index]);
        } else if (wcscmp(arguments[index], L"--floppy") == 0 && index + 1 < count) {
            combo_set_path(ui->floppy_combo, arguments[++index]);
        } else if (wcscmp(arguments[index], L"--hdd") == 0 && index + 1 < count) {
            combo_set_path(ui->hdd_combo, arguments[++index]);
        } else if (wcscmp(arguments[index], L"--create-hdd") == 0 && index + 1 < count) {
            combo_set_path(ui->hdd_combo, arguments[++index]);
            ui->hdd_create_requested = true;
        } else if (arguments[index][0] != L'-' &&
                   SendMessageW(ui->floppy_combo, WM_GETTEXTLENGTH, 0u, 0u) == 0) {
            combo_set_path(ui->floppy_combo, arguments[index]);
        }
    }
    LocalFree(arguments);
    update_media_status(ui);
}

static LRESULT CALLBACK launcher_window_procedure(HWND window, UINT message,
                                                   WPARAM wparam, LPARAM lparam)
{
    LauncherUi *ui = (LauncherUi *)GetWindowLongPtrW(window, GWLP_USERDATA);

    if (message == WM_NCCREATE) {
        CREATESTRUCTW *create = (CREATESTRUCTW *)lparam;
        ui = create->lpCreateParams;
        if (ui != NULL) ui->window = window;
        (void)SetWindowLongPtrW(window, GWLP_USERDATA, (LONG_PTR)ui);
    }
    switch (message) {
    case WM_CREATE:
        if (ui == NULL || !create_controls(ui)) return -1;
        (void)SetTimer(window, CGA_REFRESH_TIMER_ID, CGA_REFRESH_PERIOD_MS, NULL);
        {
            RECT client;
            if (GetClientRect(window, &client)) {
                layout_controls(ui, client.right - client.left,
                                client.bottom - client.top);
            }
        }
        return 0;
    case WM_TIMER:
        if (wparam == CGA_REFRESH_TIMER_ID) render_cga(ui);
        return 0;
    case WM_GETMINMAXINFO:
        {
            MINMAXINFO *limits = (MINMAXINFO *)lparam;
            RECT minimum = {0, 0, WINDOW_MIN_CLIENT_WIDTH,
                            WINDOW_MIN_CLIENT_HEIGHT};
            if (limits != NULL &&
                AdjustWindowRectEx(&minimum, LAUNCHER_WINDOW_STYLE, FALSE, 0u)) {
                limits->ptMinTrackSize.x = minimum.right - minimum.left;
                limits->ptMinTrackSize.y = minimum.bottom - minimum.top;
            }
            return 0;
        }
    case WM_SIZE:
        if (ui != NULL) {
            layout_controls(ui, (int)LOWORD(lparam), (int)HIWORD(lparam));
        }
        return 0;
    case WM_COMMAND:
        if (ui == NULL) break;
        switch (LOWORD(wparam)) {
        case ID_BIOS_BROWSE:
            (void)choose_file(window, ui->bios_combo,
                              L"BIOS ROM (*.bin;*.rom)\0*.bin;*.rom\0All files\0*.*\0\0");
            return 0;
        case ID_FLOPPY_BROWSE:
            if (choose_file(window, ui->floppy_combo,
                            L"Floppy images (*.img;*.ima;*.vfd)\0*.img;*.ima;*.vfd\0All files\0*.*\0\0")) {
                update_media_status(ui);
            }
            return 0;
        case ID_HDD_BROWSE:
            if (choose_file(window, ui->hdd_combo,
                            L"Raw disk images (*.img;*.ima)\0*.img;*.ima\0All files\0*.*\0\0")) {
                ui->hdd_create_requested = false;
                update_media_status(ui);
            }
            return 0;
        case ID_FLOPPY_EJECT:
            eject_floppy(ui);
            return 0;
        case ID_FLOPPY_RELOAD:
            (void)reload_selected_floppy(ui);
            return 0;
        case ID_HDD_EJECT:
            combo_set_path(ui->hdd_combo, L"");
            ui->hdd_create_requested = false;
            update_media_status(ui);
            return 0;
        case ID_HDD_NEW:
            if (choose_new_file(window, ui->hdd_combo,
                                L"Raw disk images (*.img)\0*.img\0All files\0*.*\0\0")) {
                wchar_t new_path[MAX_PATH] = L"";
                if (combo_get_path(ui->hdd_combo, new_path) &&
                    create_new_hdd_file(window, new_path)) {
                    ui->hdd_create_requested = false;
                    update_media_status(ui);
                } else {
                    combo_set_path(ui->hdd_combo, L"");
                }
            }
            return 0;
        case ID_MOUNT_RESTART:
            (void)mount_selected_media(ui);
            return 0;
        case ID_SPEED_COMBO:
            if (HIWORD(wparam) == CBN_SELENDOK) select_speed_multiplier(ui);
            return 0;
        default:
            if (HIWORD(wparam) == CBN_SELENDOK) update_media_status(ui);
            break;
        }
        break;
    case WM_SETFOCUS:
        if (ui != NULL && ui->running) sim_cga_console_focus(&ui->system.cga);
        return 0;
    case WM_PAINT:
        {
            PAINTSTRUCT paint_info;
            HDC dc = BeginPaint(window, &paint_info);
            RECT client;
            GetClientRect(window, &client);
            (void)FillRect(dc, &client, (HBRUSH)GetStockObject(WHITE_BRUSH));
            EndPaint(window, &paint_info);
            return 0;
        }
    case WM_DESTROY:
        (void)KillTimer(window, CGA_REFRESH_TIMER_ID);
        if (ui != NULL) (void)save_and_destroy_system(ui);
        PostQuitMessage(0);
        return 0;
    default:
        break;
    }
    return DefWindowProcW(window, message, wparam, lparam);
}

int main(int argc, char **argv)
{
    LauncherUi ui = {0};
    MSG message;
    WNDCLASSW window_class = {0};
    HINSTANCE instance = GetModuleHandleW(NULL);
    RECT bounds = {0, 0, WINDOW_CLIENT_WIDTH, WINDOW_CLIENT_HEIGHT};

    (void)argc;
    (void)argv;
    set_process_dpi_aware();
    window_class.lpfnWndProc = launcher_window_procedure;
    window_class.hInstance = instance;
    window_class.hCursor = LoadCursorW(NULL, MAKEINTRESOURCEW(32512));
    window_class.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    window_class.lpszClassName = L"PcSimLauncherWindow";
    if (RegisterClassW(&window_class) == 0 && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        report_error(NULL, L"Unable to register the launcher window class.");
        return 1;
    }
    if (!AdjustWindowRectEx(&bounds, LAUNCHER_WINDOW_STYLE, FALSE, 0u)) {
        report_error(NULL, L"Unable to calculate launcher window bounds.");
        return 1;
    }
    ui.window = CreateWindowExW(0u, window_class.lpszClassName, L"8086-PC-Sim",
                                LAUNCHER_WINDOW_STYLE,
                                CW_USEDEFAULT, CW_USEDEFAULT,
                                bounds.right - bounds.left, bounds.bottom - bounds.top,
                                NULL, NULL, instance, &ui);
    if (ui.window == NULL) {
        report_error(NULL, L"Unable to create the launcher window.");
        return 1;
    }
    set_initial_configuration(&ui);
    ShowWindow(ui.window, SW_SHOW);
    (void)UpdateWindow(ui.window);

    for (;;) {
        while (PeekMessageW(&message, NULL, 0u, 0u, PM_REMOVE) != 0) {
            if (message.message == WM_QUIT) return 0;
            (void)TranslateMessage(&message);
            (void)DispatchMessageW(&message);
        }
        if (ui.running) {
            run_simulation_slice(&ui);
            (void)MsgWaitForMultipleObjects(0u, NULL, FALSE, 1u, QS_ALLINPUT);
        } else {
            WaitMessage();
        }
    }
}
#else
int main(void)
{
    (void)fprintf(stderr, "The graphical launcher is available on Windows only.\n");
    return 1;
}
#endif
