/*
 * Compact PC-compatible BIOS for the simulated platform.
 *
 * Service boundaries were cross-checked against the MIT-licensed 8086tiny
 * BIOS source. This ROM uses only standard 8086 instructions and this
 * project's ATA PIO, CGA, 8259, 8254, and keyboard-controller contracts.
 */
.code16
.arch i8086
.section .text
.global _start

.set BDA_SEGMENT, 0x0040
.set BDA_KB_HEAD, 0x001A
.set BDA_KB_TAIL, 0x001C
.set BDA_KB_BEGIN, 0x001E
.set BDA_KB_END, 0x003E
.set BDA_EQUIPMENT, 0x0010
.set BDA_MEMORY_KIB, 0x0013
.set BDA_KB_FLAGS, 0x0017
.set BDA_KB_PREFIX, 0x0018
.set BDA_VIDEO_MODE, 0x0049
.set BDA_VIDEO_COLUMNS, 0x004A
.set BDA_VIDEO_PAGE_SIZE, 0x004C
.set BDA_SIM_DISK_FLAGS, 0x0090
.set BDA_SIM_FLOPPY_CYLINDERS, 0x0092
.set BDA_SIM_FLOPPY_HEADS, 0x0094
.set BDA_SIM_FLOPPY_SECTORS, 0x0095
.set BDA_SIM_HDD_CYLINDERS, 0x0096
.set BDA_SIM_HDD_HEADS, 0x0098
.set BDA_SIM_HDD_SECTORS, 0x0099
.set BDA_SIM_HDD_TOTAL_SECTORS, 0x009A
.set BDA_SIM_INT13_SEQUENCE, 0x00E0
.set BDA_SIM_INT13_AX, 0x00E2
.set BDA_SIM_INT13_BX, 0x00E4
.set BDA_SIM_INT13_CX, 0x00E6
.set BDA_SIM_INT13_DX, 0x00E8
.set BDA_SIM_INT13_RESULT, 0x00EA
.set BDA_CURSOR, 0x0050
.set BDA_TICKS_LOW, 0x006C
.set BDA_TICKS_HIGH, 0x006E

_start:
    cli
    xorw %ax, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %ss
    movw $0x7000, %sp

    movw $int8, 0x0020
    movw $0xF000, 0x0022
    movw $int9, 0x0024
    movw $0xF000, 0x0026
    movw $int10, 0x0040
    movw $0xF000, 0x0042
    movw $int11, 0x0044
    movw $0xF000, 0x0046
    movw $int12, 0x0048
    movw $0xF000, 0x004A
    movw $int13, 0x004C
    movw $0xF000, 0x004E
    movw $int14, 0x0050
    movw $0xF000, 0x0052
    movw $int15, 0x0054
    movw $0xF000, 0x0056
    movw $int16, 0x0058
    movw $0xF000, 0x005A
    movw $int17, 0x005C
    movw $0xF000, 0x005E
    movw $int19, 0x0064
    movw $0xF000, 0x0066
    movw $int1a, 0x0068
    movw $0xF000, 0x006A
    movw $int70, 0x01C0
    movw $0xF000, 0x01C2
    movw $int70, 0x01C4
    movw $0xF000, 0x01C6
    movw $int70, 0x01C8
    movw $0xF000, 0x01CA
    movw $int70, 0x01CC
    movw $0xF000, 0x01CE
    movw $int70, 0x01D0
    movw $0xF000, 0x01D2
    movw $int70, 0x01D4
    movw $0xF000, 0x01D6
    movw $int70, 0x01D8
    movw $0xF000, 0x01DA
    movw $int70, 0x01DC
    movw $0xF000, 0x01DE

    movw $BDA_SEGMENT, %ax
    movw %ax, %ds
    movw $BDA_KB_BEGIN, BDA_KB_HEAD
    movw $BDA_KB_BEGIN, BDA_KB_TAIL
    movw $0, BDA_KB_FLAGS
    movw $0x0020, BDA_EQUIPMENT
    testb $0x01, BDA_SIM_DISK_FLAGS
    jz post_no_floppy
    orw $0x0001, BDA_EQUIPMENT
post_no_floppy:
    movw $640, BDA_MEMORY_KIB
    movb $0x03, BDA_VIDEO_MODE
    movw $80, BDA_VIDEO_COLUMNS
    movw $4000, BDA_VIDEO_PAGE_SIZE
    movw $0, BDA_CURSOR
    movw $0, BDA_TICKS_LOW
    movw $0, BDA_TICKS_HIGH
    xorw %ax, %ax
    movw %ax, %ds

    /* PIC pair: master 08h-0Fh, slave 70h-77h on master IRQ2, 8086 mode. */
    movb $0x11, %al
    outb %al, $0x20
    movb $0x08, %al
    outb %al, $0x21
    movb $0x04, %al
    outb %al, $0x21
    movb $0x01, %al
    outb %al, $0x21
    movb $0xF8, %al
    outb %al, $0x21
    movb $0x11, %al
    outb %al, $0xA0
    movb $0x70, %al
    outb %al, $0xA1
    movb $0x02, %al
    outb %al, $0xA1
    movb $0x01, %al
    outb %al, $0xA1
    movb $0xFF, %al
    outb %al, $0xA1

    /* 8237A POST baseline: master-clear leaves all four channels masked. */
    movb $0, %al
    outb %al, $0x0D

    /* Re-enable the 8042 IRQ1 gate after PIC initialization. */
    movb $0x60, %al
    outb %al, $0x64
    movb $0x01, %al
    outb %al, $0x60

    /* PIT channel 0: mode 2, divisor zero (= 65536), approximately 18.2 Hz. */
    movb $0x34, %al
    outb %al, $0x43
    xorw %ax, %ax
    outb %al, $0x40
    outb %al, $0x40

    movw $0xB800, %ax
    movw %ax, %es
    xorw %di, %di
    movw $0x0720, %ax
    movw $2000, %cx
    rep stosw
    xorw %ax, %ax
    movw %ax, %ds
    movw %ax, %es
boot_from_disk:
    movw $0x7C00, %bx
    movw $0x0201, %ax
    movw $0x0001, %cx
    xorw %dx, %dx
    int $0x13
    jnc boot_sector_ready
    movw $0x7C00, %bx
    movw $0x0201, %ax
    movw $0x0001, %cx
    movw $0x0080, %dx
    int $0x13
    jnc boot_sector_ready
    jmp disk_error

boot_sector_ready:
    ljmp $0x0000, $0x7C00

/* IRQ0: maintain the standard BIOS tick counter and acknowledge PIC. */
int8:
    pushw %ax
    pushw %ds
    movw $BDA_SEGMENT, %ax
    movw %ax, %ds
    addw $1, BDA_TICKS_LOW
    adcw $0, BDA_TICKS_HIGH
    movb $0x20, %al
    outb %al, $0x20
    popw %ds
    popw %ax
    iret

/* IRQ1: scan-code set 1 -> BIOS Data Area keyboard ring. */
int9:
    pushw %ax
    pushw %bx
    pushw %di
    pushw %ds
    pushw %es
    inb $0x60, %al
    cmpb $0xE0, %al
    jne int9_not_extended_prefix
    movw $BDA_SEGMENT, %bx
    movw %bx, %es
    movb $1, %es:BDA_KB_PREFIX
    jmp int9_ack
int9_not_extended_prefix:
    testb $0x80, %al
    jnz int9_break
    cmpb $0x2A, %al
    jne int9_not_left_shift_make
    jmp int9_left_shift_make
int9_not_left_shift_make:
    cmpb $0x36, %al
    jne int9_not_right_shift_make
    jmp int9_right_shift_make
int9_not_right_shift_make:
    cmpb $0x3A, %al
    jne int9_not_caps_lock_make
    jmp int9_caps_lock_make
int9_not_caps_lock_make:
    movb %al, %ah
    xorw %bx, %bx
    movb %al, %bl
    pushw %cs
    popw %ds
    movb ascii_table(%bx), %al
    movw $BDA_SEGMENT, %bx
    movw %bx, %es
    movb $0, %es:BDA_KB_PREFIX
    cmpb $'a', %al
    jb int9_store_key
    cmpb $'z', %al
    ja int9_store_key
    movb %es:BDA_KB_FLAGS, %bl
    andb $0x03, %bl
    jz int9_shift_clear
    movb $1, %bh
    jmp int9_shift_ready
int9_shift_clear:
    movb $0, %bh
int9_shift_ready:
    movb %es:BDA_KB_FLAGS, %bl
    andb $0x40, %bl
    jz int9_case_ready
    xorb $1, %bh
int9_case_ready:
    testb %bh, %bh
    jz int9_store_key
    subb $0x20, %al
int9_store_key:
    movw %es:BDA_KB_TAIL, %bx
    movw %bx, %di
    addw $2, %di
    cmpw $BDA_KB_END, %di
    jb int9_no_wrap
    movw $BDA_KB_BEGIN, %di
int9_no_wrap:
    cmpw %es:BDA_KB_HEAD, %di
    je int9_ack
    movw %ax, %es:(%bx)
    movw %di, %es:BDA_KB_TAIL
    jmp int9_ack
int9_break:
    movw $BDA_SEGMENT, %bx
    movw %bx, %es
    movb $0, %es:BDA_KB_PREFIX
    cmpb $0xAA, %al
    jne int9_not_left_shift_break
    andb $0xFD, %es:BDA_KB_FLAGS
    jmp int9_ack
int9_not_left_shift_break:
    cmpb $0xB6, %al
    jne int9_ack
    andb $0xFE, %es:BDA_KB_FLAGS
    jmp int9_ack
int9_left_shift_make:
    movw $BDA_SEGMENT, %bx
    movw %bx, %es
    orb $0x02, %es:BDA_KB_FLAGS
    movb $0, %es:BDA_KB_PREFIX
    jmp int9_ack
int9_right_shift_make:
    movw $BDA_SEGMENT, %bx
    movw %bx, %es
    orb $0x01, %es:BDA_KB_FLAGS
    movb $0, %es:BDA_KB_PREFIX
    jmp int9_ack
int9_caps_lock_make:
    movw $BDA_SEGMENT, %bx
    movw %bx, %es
    xorb $0x40, %es:BDA_KB_FLAGS
    movb $0, %es:BDA_KB_PREFIX
int9_ack:
    movb $0x20, %al
    outb %al, $0x20
    popw %es
    popw %ds
    popw %di
    popw %bx
    popw %ax
    iret

/* INT 10h: single-page 80x25 CGA text-mode service set. */
int10:
    cmpb $0x0E, %ah
    jne int10_not_teletype
    jmp int10_teletype
int10_not_teletype:
    cmpb $0x00, %ah
    jne int10_not_set_mode
    jmp int10_set_mode
int10_not_set_mode:
    cmpb $0x01, %ah
    jne int10_not_set_cursor_shape
    jmp int10_set_cursor_shape
int10_not_set_cursor_shape:
    cmpb $0x02, %ah
    jne int10_not_set_cursor
    jmp int10_set_cursor
int10_not_set_cursor:
    cmpb $0x03, %ah
    jne int10_not_get_cursor
    jmp int10_get_cursor
int10_not_get_cursor:
    cmpb $0x05, %ah
    jne int10_not_set_page
    jmp int10_set_page
int10_not_set_page:
    cmpb $0x06, %ah
    jne int10_not_scroll_up
    jmp int10_scroll_up
int10_not_scroll_up:
    cmpb $0x07, %ah
    jne int10_not_scroll_down
    jmp int10_scroll_down_real
int10_not_scroll_down:
    cmpb $0x08, %ah
    jne int10_not_read_cell
    jmp int10_read_cell
int10_not_read_cell:
    cmpb $0x09, %ah
    jne int10_not_write_cell
    jmp int10_write_cell
int10_not_write_cell:
    cmpb $0x0A, %ah
    jne int10_not_write_character
    jmp int10_write_character
int10_not_write_character:
    cmpb $0x0F, %ah
    jne int10_unsupported
    jmp int10_get_mode
int10_unsupported:
    iret

/* AH=01h: program the MC6845 cursor scan-line start and end registers. */
int10_set_cursor_shape:
    pushw %ax
    pushw %dx
    movw $0x3D4, %dx
    movb $10, %al
    outb %al, %dx
    incw %dx
    movb %ch, %al
    outb %al, %dx
    decw %dx
    movb $11, %al
    outb %al, %dx
    incw %dx
    movb %cl, %al
    outb %al, %dx
    popw %dx
    popw %ax
    iret

/* AH=05h: this machine implements only display page zero. */
int10_set_page:
    iret

/* AH=08h: return the character and attribute at the current page-zero cursor. */
int10_read_cell:
    pushw %bx
    pushw %dx
    pushw %ds
    pushw %es
    movw $BDA_SEGMENT, %dx
    movw %dx, %ds
    movw BDA_CURSOR, %bx
    shlw $1, %bx
    movw $0xB800, %dx
    movw %dx, %es
    movw %es:(%bx), %ax
    popw %es
    popw %ds
    popw %dx
    popw %bx
    iret

/* AH=09h: write AL with BL attribute CX times without advancing the cursor. */
int10_write_cell:
    pushw %ax
    pushw %bx
    pushw %cx
    pushw %di
    pushw %ds
    pushw %es
    movb %al, %ah
    movb %bl, %al
    xchgb %al, %ah
    movw $BDA_SEGMENT, %bx
    movw %bx, %ds
    movw BDA_CURSOR, %di
    shlw $1, %di
    movw $0xB800, %bx
    movw %bx, %es
int10_write_cell_loop:
    jcxz int10_write_cell_done
    movw %ax, %es:(%di)
    addw $2, %di
    cmpw $4000, %di
    jb int10_write_cell_continue
    xorw %di, %di
int10_write_cell_continue:
    loop int10_write_cell_loop
int10_write_cell_done:
    popw %es
    popw %ds
    popw %di
    popw %cx
    popw %bx
    popw %ax
    iret

/* AH=0Ah: write AL CX times, retaining the existing attribute bytes. */
int10_write_character:
    pushw %ax
    pushw %bx
    pushw %cx
    pushw %di
    pushw %ds
    pushw %es
    movb %al, %ah
    movw $BDA_SEGMENT, %bx
    movw %bx, %ds
    movw BDA_CURSOR, %di
    shlw $1, %di
    movw $0xB800, %bx
    movw %bx, %es
int10_write_character_loop:
    jcxz int10_write_character_done
    movb %ah, %es:(%di)
    addw $2, %di
    cmpw $4000, %di
    jb int10_write_character_continue
    xorw %di, %di
int10_write_character_continue:
    loop int10_write_character_loop
int10_write_character_done:
    popw %es
    popw %ds
    popw %di
    popw %cx
    popw %bx
    popw %ax
    iret

/* AH=06h: scroll an inclusive CH:CL..DH:DL window up, or clear when AL=0. */
int10_scroll_up:
    jmp int10_scroll_up_real

/*
 * AH=06h/07h common worker, used by the compact BIOS rather than the legacy
 * arithmetic-heavy code below.  The caller preserves all general registers;
 * only BDA scratch words are used while the operation is in progress.
 *
 * The essential CGA invariant is explicit here: a text row is always 80
 * words (160 bytes), even when the requested window is narrower.
 */
.if 0
    pushf
    pushw %ax
    pushw %bx
    pushw %cx
    pushw %dx
    pushw %si
    pushw %di
    pushw %bp
    pushw %ds
    pushw %es
    cld
    movb %al, %bl
    xorw %ax, %ax
    movb %bh, %al
    pushw %ax
    movb $0, %bh
    pushw %cx
    xorw %ax, %ax
    movb %dl, %al
    subb %cl, %al
    incb %al
    movw %ax, %bp
    xorw %ax, %ax
    movb %dh, %al
    subb %ch, %al
    incb %al
    movw %ax, %di
    popw %cx
    xorw %dx, %dx
    movb %cl, %dl
    xorw %ax, %ax
    movb %ch, %al
    movw $80, %cx
    mulw %cx
    addw %dx, %ax
    movw %ax, %dx              /* Window origin in cells. */
    testw %bx, %bx
    jnz int10_scroll_up_count_set
    movw %di, %bx
int10_scroll_up_count_set:
    cmpw %di, %bx
    jbe int10_scroll_up_count_clamped
    movw %di, %bx
int10_scroll_up_count_clamped:
    movw %bx, %ax
    mulw %bp
    pushw %ax                  /* Number of cells to clear. */
    movw %bx, %ax
    movw $80, %cx
    mulw %cx
    addw %dx, %ax
    movw %ax, %si
    shlw $1, %si
    movw %dx, %di
    shlw $1, %di
    subw %bx, %di              /* Temporary: rows still to move. */
    shrw $1, %di
    movw %di, %bx
    movw $160, %ax
    movw %bp, %di
    shlw $1, %di
    subw %di, %ax              /* Bytes to skip after each row. */
    movw $0xB800, %ax
    movw %ax, %ds
    movw %ax, %es
int10_scroll_up_row:
    testw %bx, %bx
    jz int10_scroll_up_fill
    pushw %bx
    movw %bp, %cx
    rep movsw
    popw %bx
    addw %ax, %si
    addw %ax, %di
    decw %bx
    jmp int10_scroll_up_row
int10_scroll_up_fill:
    popw %cx
    popw %ax
    movb %al, %ah
    movb $0x20, %al
    rep stosw
    popw %es
    popw %ds
    popw %bp
    popw %di
    popw %si
    popw %dx
    popw %cx
    popw %bx
    popw %ax
    popf
    iret

/* AH=07h: scroll an inclusive CH:CL..DH:DL window down, or clear when AL=0. */
int10_scroll_down:
    pushf
    pushw %ax
    pushw %bx
    pushw %cx
    pushw %dx
    pushw %si
    pushw %di
    pushw %bp
    pushw %ds
    pushw %es
    cld
    movb %al, %bl
    xorw %ax, %ax
    movb %bh, %al
    pushw %ax
    movb $0, %bh
    pushw %cx
    xorw %ax, %ax
    movb %dl, %al
    subb %cl, %al
    incb %al
    movw %ax, %bp
    xorw %ax, %ax
    movb %dh, %al
    subb %ch, %al
    incb %al
    movw %ax, %di
    popw %cx
    xorw %dx, %dx
    movb %cl, %dl
    xorw %ax, %ax
    movb %ch, %al
    movw $80, %cx
    mulw %cx
    addw %dx, %ax
    movw %ax, %dx              /* Window origin in cells. */
    testw %bx, %bx
    jnz int10_scroll_down_count_set
    movw %di, %bx
int10_scroll_down_count_set:
    cmpw %di, %bx
    jbe int10_scroll_down_count_clamped
    movw %di, %bx
int10_scroll_down_count_clamped:
    subw %bx, %di
    movw %bx, %ax
    mulw %bp
    pushw %ax                  /* Number of cells to clear. */
    movw %bx, %ax
    movw $80, %cx
    mulw %cx
    pushw %ax                  /* Downward displacement in cells. */
    movw %di, %ax
    decw %ax
    mulw %cx
    addw %dx, %ax
    addw %bp, %ax
    decw %ax
    movw %ax, %si              /* Last source cell in the window. */
    popw %ax
    movw %si, %di
    addw %ax, %di              /* Last destination cell. */
    shlw $1, %si
    shlw $1, %di
    movw %di, %ax
    subw %si, %ax
    pushw %ax                  /* Keep destination-source distance. */
    movw $160, %ax
    movw %bp, %di
    shlw $1, %di
    subw %di, %ax              /* Bytes to skip back after each row. */
    movw %ax, %di
    movw %si, %ax
    addw %di, %ax
    subw %ax, %si
    popw %ax
    movw %si, %di
    addw %ax, %di
    movw %di, %ax
    subw %si, %ax
    pushw %ax
    movw %di, %ax
    subw %si, %ax
    movw %di, %ax
    movw %si, %di
    addw %ax, %di
    movw %di, %ax
    popw %di
    movw %di, %ax
    movw %si, %di
    addw %ax, %di
    movw %di, %ax
    subw %si, %ax
    movw %ax, %di
    movw %si, %ax
    movw %di, %si
    movw %ax, %di
    movw %di, %ax
    subw %si, %ax
    movw %ax, %di
    movw %si, %ax
    movw %di, %si
    movw %ax, %di
    movw %di, %ax
    subw %si, %ax
    movw %ax, %di
    movw %si, %ax
    movw %di, %si
    movw %ax, %di
    movw $0xB800, %ax
    movw %ax, %ds
    movw %ax, %es
    std
    rep movsw
    cld
    movw %dx, %di
    shlw $1, %di
    movw %bx, %ax
    mulw %bp
    movw %ax, %cx
    popw %ax
    movb %al, %ah
    movb $0x20, %al
    rep stosw
    popw %es
    popw %ds
    popw %bp
    popw %di
    popw %si
    popw %dx
    popw %cx
    popw %bx
    popw %ax
    popf
    iret

.endif

int10_set_mode:
    pushw %ax
    pushw %cx
    pushw %di
    pushw %es
    movw $0xB800, %ax
    movw %ax, %es
    xorw %di, %di
    movw $0x0720, %ax
    movw $2000, %cx
    rep stosw
    popw %es
    popw %di
    popw %cx
    popw %ax
    jmp int10_cursor_zero

int10_set_cursor:
    pushw %ax
    pushw %bx
    pushw %dx
    pushw %cx
    pushw %ds
    movw $BDA_SEGMENT, %ax
    movw %ax, %ds
    movb %dl, %cl
    xorw %ax, %ax
    movb %dh, %al
    movw $80, %bx
    mulw %bx
    xorw %bx, %bx
    movb %cl, %bl
    addw %bx, %ax
    movw %ax, BDA_CURSOR
    popw %ds
    popw %cx
    popw %dx
    popw %bx
    popw %ax
    call int10_update_cursor
    iret

int10_get_cursor:
    pushw %ax
    pushw %bx
    pushw %cx
    pushw %ds
    movw $BDA_SEGMENT, %ax
    movw %ax, %ds
    movw BDA_CURSOR, %ax
    xorw %dx, %dx
    movw $80, %bx
    divw %bx
    movb %al, %dh
    movb %dl, %dl
    popw %ds
    popw %cx
    popw %bx
    popw %ax
    iret

int10_get_mode:
    movb $0x03, %al
    movb $80, %ah
    movb $0, %bh
    iret

int10_teletype:
    cmpb $0x0D, %al
    je int10_carriage_return
    cmpb $0x0A, %al
    je int10_line_feed
    cmpb $0x08, %al
    je int10_backspace
    pushw %ax
    pushw %bx
    pushw %cx
    pushw %ds
    pushw %es
    pushw %di
    movb %al, %cl
    movw $BDA_SEGMENT, %ax
    movw %ax, %ds
    movw BDA_CURSOR, %di
    shlw $1, %di
    movb %cl, %al
    movb $0x07, %ah
    movw $0xB800, %bx
    movw %bx, %es
    movw %ax, %es:(%di)
    shrw $1, %di
    incw %di
    cmpw $2000, %di
    jb int10_store_cursor
    call int10_scroll_up_one
    movw $1920, %di
int10_store_cursor:
    movw %di, BDA_CURSOR
    popw %di
    popw %es
    popw %ds
    popw %cx
    popw %bx
    popw %ax
    call int10_update_cursor
    iret

/* AH=0Eh, AL=08h: move the text cursor left without drawing a glyph. */
int10_backspace:
    pushw %ax
    pushw %ds
    movw $BDA_SEGMENT, %ax
    movw %ax, %ds
    cmpw $0, BDA_CURSOR
    je int10_backspace_done
    decw BDA_CURSOR
int10_backspace_done:
    popw %ds
    popw %ax
    call int10_update_cursor
    iret

int10_carriage_return:
    pushw %ax
    pushw %bx
    pushw %dx
    pushw %ds
    movw $BDA_SEGMENT, %ax
    movw %ax, %ds
    movw BDA_CURSOR, %ax
    xorw %dx, %dx
    movw $80, %bx
    divw %bx
    mulw %bx
    movw %ax, BDA_CURSOR
    popw %ds
    popw %dx
    popw %bx
    popw %ax
    call int10_update_cursor
    iret

int10_line_feed:
    pushw %ax
    pushw %ds
    movw $BDA_SEGMENT, %ax
    movw %ax, %ds
    addw $80, BDA_CURSOR
    cmpw $2000, BDA_CURSOR
    jb int10_lf_done
    call int10_scroll_up_one
    movw $1920, BDA_CURSOR
int10_lf_done:
    popw %ds
    popw %ax
    call int10_update_cursor
    iret

int10_update_cursor:
    pushw %ax
    pushw %bx
    pushw %dx
    pushw %ds
    movw $BDA_SEGMENT, %ax
    movw %ax, %ds
    movw BDA_CURSOR, %bx
    movw $0x3D4, %dx
    movb $14, %al
    outb %al, %dx
    incw %dx
    movb %bh, %al
    outb %al, %dx
    decw %dx
    movb $15, %al
    outb %al, %dx
    incw %dx
    movb %bl, %al
    outb %al, %dx
    popw %ds
    popw %dx
    popw %bx
    popw %ax
    ret

/* Move rows 1..24 to rows 0..23, then clear the newly exposed last row. */
int10_scroll_up_one:
    pushf
    pushw %ax
    pushw %cx
    pushw %si
    pushw %di
    pushw %ds
    pushw %es
    cld
    movw $0xB800, %ax
    movw %ax, %ds
    movw %ax, %es
    movw $160, %si
    xorw %di, %di
    movw $1920, %cx
    rep movsw
    movw $0x0720, %ax
    movw $80, %cx
    rep stosw
    popw %es
    popw %ds
    popw %di
    popw %si
    popw %cx
    popw %ax
    popf
    ret

int10_cursor_zero:
    pushw %ax
    pushw %ds
    movw $BDA_SEGMENT, %ax
    movw %ax, %ds
    movw $0, BDA_CURSOR
    popw %ds
    popw %ax
    call int10_update_cursor
    iret

int10_scroll_up_real:
    pushf
    pushw %ax
    pushw %bx
    pushw %cx
    pushw %dx
    pushw %si
    pushw %di
    pushw %bp
    pushw %ds
    pushw %es
    movw %sp, %bp
    subw $14, %sp
    cld
    /* Locals: width, height, count, move_rows, inter-row gap, origin. */
    /* Saved input registers: DX=10, CX=12, BX=14, AX=16 bytes above BP. */
    /* width = DL(right) - CL(left) + 1. */
    movb 12(%bp), %dl
    movb 10(%bp), %al
    cmpb %dl, %al
    jb int10_scroll_return
    subb %dl, %al
    incb %al
    xorb %ah, %ah
    movw %ax, -2(%bp)

    /* height = DH(bottom) - CH(top) + 1. */
    movb 13(%bp), %dl
    movb 11(%bp), %al
    cmpb %dl, %al
    jb int10_scroll_return
    subb %dl, %al
    incb %al
    xorb %ah, %ah
    movw %ax, -4(%bp)

    /* origin = top * 160 + left * 2.  Every text row remains 80 cells. */
    movb 13(%bp), %al
    xorb %ah, %ah
    movw $160, %bx
    mulw %bx
    movb 12(%bp), %bl
    xorb %bh, %bh
    shlw $1, %bx
    addw %bx, %ax
    movw %ax, -12(%bp)

    /* AL=0 clears the full window; nonzero counts are clamped to height. */
    movb 16(%bp), %al
    xorb %ah, %ah
    testw %ax, %ax
    jnz int10_scroll_up_count_ready
    movw -4(%bp), %ax
int10_scroll_up_count_ready:
    cmpw -4(%bp), %ax
    jbe int10_scroll_up_count_clamped
    movw -4(%bp), %ax
int10_scroll_up_count_clamped:
    movw %ax, -6(%bp)
    movw -4(%bp), %bx
    subw %ax, %bx
    movw %bx, -8(%bp)
    movw -2(%bp), %ax
    shlw $1, %ax
    movw $160, %bx
    subw %ax, %bx
    movw %bx, -10(%bp)

    /* Copy source rows downward-to-upward with a full 160-byte row stride. */
    movw -12(%bp), %di
    movw -12(%bp), %si
    movw -6(%bp), %ax
    movw $160, %bx
    mulw %bx
    addw %ax, %si
    movw $0xB800, %ax
    movw %ax, %ds
    movw %ax, %es
int10_scroll_up_real_row:
    cmpw $0, -8(%bp)
    je int10_scroll_up_real_fill
    movw -2(%bp), %cx
    rep movsw
    addw -10(%bp), %si
    addw -10(%bp), %di
    decw -8(%bp)
    jmp int10_scroll_up_real_row
int10_scroll_up_real_fill:
    movw -6(%bp), %bx
    movw -12(%bp), %di
    movw -4(%bp), %ax
    subw -6(%bp), %ax
    movw $160, %cx
    mulw %cx
    addw %ax, %di
    movw $0x0020, %ax
    movb 15(%bp), %ah
int10_scroll_up_real_fill_row:
    cmpw $0, %bx
    je int10_scroll_return
    movw -2(%bp), %cx
    rep stosw
    addw -10(%bp), %di
    decw %bx
    jmp int10_scroll_up_real_fill_row

int10_scroll_down_real:
    pushf
    pushw %ax
    pushw %bx
    pushw %cx
    pushw %dx
    pushw %si
    pushw %di
    pushw %bp
    pushw %ds
    pushw %es
    movw %sp, %bp
    subw $14, %sp
    cld
    /* Same decoded rectangle as AH=06h: DX=10, CX=12, BX=14, AX=16. */
    movb 12(%bp), %dl
    movb 10(%bp), %al
    cmpb %dl, %al
    jb int10_scroll_return
    subb %dl, %al
    incb %al
    xorb %ah, %ah
    movw %ax, -2(%bp)

    movb 13(%bp), %dl
    movb 11(%bp), %al
    cmpb %dl, %al
    jb int10_scroll_return
    subb %dl, %al
    incb %al
    xorb %ah, %ah
    movw %ax, -4(%bp)

    movb 13(%bp), %al
    xorb %ah, %ah
    movw $160, %bx
    mulw %bx
    movb 12(%bp), %bl
    xorb %bh, %bh
    shlw $1, %bx
    addw %bx, %ax
    movw %ax, -12(%bp)

    movb 16(%bp), %al
    xorb %ah, %ah
    testw %ax, %ax
    jnz int10_scroll_down_count_ready
    movw -4(%bp), %ax
int10_scroll_down_count_ready:
    cmpw -4(%bp), %ax
    jbe int10_scroll_down_count_clamped
    movw -4(%bp), %ax
int10_scroll_down_count_clamped:
    movw %ax, -6(%bp)
    movw -4(%bp), %bx
    subw %ax, %bx
    movw %bx, -8(%bp)
    movw -2(%bp), %ax
    shlw $1, %ax
    movw $160, %bx
    subw %ax, %bx
    movw %bx, -10(%bp)

    /* Start at the last source cell and copy backward to preserve overlap. */
    movw -12(%bp), %si
    movw -8(%bp), %ax
    decw %ax
    movw $160, %bx
    mulw %bx
    addw %ax, %si
    movw -2(%bp), %ax
    decw %ax
    shlw $1, %ax
    addw %ax, %si
    movw %si, %di
    movw -6(%bp), %ax
    movw $160, %bx
    mulw %bx
    addw %ax, %di
    movw $0xB800, %ax
    movw %ax, %ds
    movw %ax, %es
int10_scroll_down_real_row:
    cmpw $0, -8(%bp)
    je int10_scroll_down_real_fill
    movw -2(%bp), %cx
    std
    rep movsw
    cld
    subw -10(%bp), %si
    subw -10(%bp), %di
    decw -8(%bp)
    jmp int10_scroll_down_real_row
int10_scroll_down_real_fill:
    movw -6(%bp), %bx
    movw -12(%bp), %di
    movw $0x0020, %ax
    movb 15(%bp), %ah
int10_scroll_down_real_fill_row:
    cmpw $0, %bx
    je int10_scroll_return
    movw -2(%bp), %cx
    rep stosw
    addw -10(%bp), %di
    decw %bx
    jmp int10_scroll_down_real_fill_row

int10_scroll_return:
    movw %bp, %sp
    popw %es
    popw %ds
    popw %bp
    popw %di
    popw %si
    popw %dx
    popw %cx
    popw %bx
    popw %ax
    popf
    iret

int11:
    pushw %ds
    movw $BDA_SEGMENT, %ax
    movw %ax, %ds
    movw BDA_EQUIPMENT, %ax
    popw %ds
    iret

int12:
    pushw %ds
    movw $BDA_SEGMENT, %ax
    movw %ax, %ds
    movw BDA_MEMORY_KIB, %ax
    popw %ds
    iret

/* INT 15h: system services used by DOS probes. */
int15:
    pushw %bp
    movw %sp, %bp
    cmpb $0x88, %ah
    jne int15_unsupported
    /* The modeled PC exposes 640 KiB conventional RAM and no extended RAM. */
    xorw %ax, %ax
    andw $0xFFFE, 6(%bp)
    popw %bp
    iret
int15_unsupported:
    orw $0x0001, 6(%bp)
    popw %bp
    iret

/* INT 14h: this machine deliberately exposes no serial controller. */
int14:
    pushw %bp
    movw %sp, %bp
    movb $0x80, %ah
    orw $0x0001, 6(%bp)
    popw %bp
    iret

/* INT 17h: report the intentionally absent parallel printer as offline. */
int17:
    movb $0x29, %ah
    iret

/* INT 13h: ATA PIO CHS bridge, retained from the existing tested ROM. */
int13:
    pushw %bx
    pushw %cx
    pushw %dx
    pushw %di
    pushw %bp
    call int13_diagnostic_enter
    cmpb $0x00, %ah
    jne int13_check_geometry
    jmp int13_success
int13_check_geometry:
    cmpb $0x08, %ah
    jne int13_check_read
    jmp int13_geometry
int13_check_read:
    cmpb $0x15, %ah
    jne int13_check_transfer
    jmp int13_drive_type
int13_check_transfer:
    cmpb $0x41, %ah
    jne int13_check_chs_transfer
    jmp int13_extensions
int13_check_chs_transfer:
    cmpb $0x48, %ah
    jne int13_check_transfer_kind
    jmp int13_extended_parameters
int13_check_transfer_kind:
    cmpb $0x02, %ah
    je int13_transfer
    cmpb $0x03, %ah
    je int13_transfer
    cmpb $0x04, %ah
    jne 1f
    jmp int13_verify
1:
    cmpb $0x05, %ah
    jne 1f
    jmp int13_format_track
1:
    jmp int13_other_service
int13_transfer:
    testb %al, %al
    jne int13_valid_count
    jmp int13_failure
int13_valid_count:
    pushw %ax
    movw %dx, %di
    movw $0x01F2, %dx
    outb %al, %dx
    incw %dx
    movb %cl, %al
    andb $0x3F, %al
    outb %al, %dx
    incw %dx
    movb %ch, %al
    outb %al, %dx
    incw %dx
    movb %cl, %al
    movb $6, %cl
    shrb %cl, %al
    outb %al, %dx
    incw %dx
    xchgw %dx, %di
    movb %dh, %al
    andb $0x0F, %al
    orb $0xA0, %al
    testb $0x80, %dl
    xchgw %dx, %di
    jz int13_select_drive
    orb $0x10, %al
int13_select_drive:
    outb %al, %dx
    incw %dx
    cmpb $0x02, %ah
    jne int13_write_command
    movb $0x20, %al
    jmp int13_start_command
int13_write_command:
    movb $0x30, %al
int13_start_command:
    outb %al, %dx
    popw %ax
    movw %ax, %bp
    andw $0x00FF, %bp
    movw %bx, %di
    cmpb $0x02, %ah
    je int13_read_wait_for_drq
    jmp int13_write_wait_for_drq
int13_read_wait_for_drq:
    movw $0x03F6, %dx
    inb %dx, %al
    testb $0x01, %al
    jnz int13_transfer_failure
    testb $0x08, %al
    jz int13_read_wait_for_drq
    movw $0x01F0, %dx
    movw $256, %cx
copy_sector:
    inw %dx, %ax
    movw %ax, %es:(%di)
    addw $2, %di
    jnc copy_sector_no_segment_wrap
    pushw %ax
    movw %es, %ax
    addw $0x1000, %ax
    movw %ax, %es
    popw %ax
copy_sector_no_segment_wrap:
    loop copy_sector
    decw %bp
    jnz int13_read_wait_for_drq
    xorb %ah, %ah
    jmp int13_success
int13_write_wait_for_drq:
    movw $0x03F6, %dx
    inb %dx, %al
    testb $0x01, %al
    jnz int13_transfer_failure
    testb $0x08, %al
    jz int13_write_wait_for_drq
    movw $0x01F0, %dx
    movw $256, %cx
copy_sector_write:
    movw %es:(%di), %ax
    outw %ax, %dx
    addw $2, %di
    jnc copy_sector_write_no_segment_wrap
    pushw %ax
    movw %es, %ax
    addw $0x1000, %ax
    movw %ax, %es
    popw %ax
copy_sector_write_no_segment_wrap:
    loop copy_sector_write
    decw %bp
    jnz int13_write_wait_for_drq
    xorb %ah, %ah
    jmp int13_success
int13_transfer_failure:
    jmp int13_failure

/*
 * INT 13h AH=04h: verify sectors.  The simulator's media is memory-backed
 * and has no independent low-level verify command.  Confirm that the named
 * BIOS drive is present, then let subsequent AH=02h/AH=03h transfers perform
 * the actual bounds and media checks.  In particular, do not issue an ATA PIO
 * command here: DOS Setup probes its floppy during configuration and expects
 * this service to finish synchronously.
 */
int13_verify:
    testb %al, %al
    jnz 1f
    jmp int13_failure
1:
    pushw %ds
    movw $BDA_SEGMENT, %ax
    movw %ax, %ds
    testb $0x80, %dl
    jnz int13_verify_fixed_disk
    cmpb $0x00, %dl
    jne int13_verify_failure
    testb $0x01, BDA_SIM_DISK_FLAGS
    jz int13_verify_failure
    popw %ds
    xorb %ah, %ah
    jmp int13_success
int13_verify_fixed_disk:
    cmpb $0x80, %dl
    jne int13_verify_failure
    testb $0x02, BDA_SIM_DISK_FLAGS
    jz int13_verify_failure
    popw %ds
    xorb %ah, %ah
    jmp int13_success
int13_verify_failure:
    popw %ds
    jmp int13_failure

/*
 * INT 13h AH=05h: format track.  ATA media has no low-level sector-ID format
 * operation; its faithful behavioral equivalent is to initialize each
 * requested CHS sector with the traditional F6h format fill pattern.  DOS
 * subsequently writes its boot sector, FATs, and root directory normally.
 */
int13_format_track:
    testb %al, %al
    jnz 1f
    jmp int13_failure
1:
    pushw %ax
    movw %dx, %di
    movw $0x01F2, %dx
    outb %al, %dx
    incw %dx
    movb %cl, %al
    andb $0x3F, %al
    outb %al, %dx
    incw %dx
    movb %ch, %al
    outb %al, %dx
    incw %dx
    movb %cl, %al
    movb $6, %cl
    shrb %cl, %al
    outb %al, %dx
    incw %dx
    xchgw %dx, %di
    movb %dh, %al
    andb $0x0F, %al
    orb $0xA0, %al
    testb $0x80, %dl
    xchgw %dx, %di
    jz int13_format_select_drive
    orb $0x10, %al
int13_format_select_drive:
    outb %al, %dx
    incw %dx
    movb $0x30, %al
    outb %al, %dx
    popw %ax
    movw %ax, %bp
    andw $0x00FF, %bp
int13_format_wait_for_drq:
    movw $0x03F6, %dx
    inb %dx, %al
    testb $0x01, %al
    jz 1f
    jmp int13_failure
1:
    testb $0x08, %al
    jz int13_format_wait_for_drq
    movw $0x01F0, %dx
    movw $256, %cx
    movw $0xF6F6, %ax
int13_format_fill_sector:
    outw %ax, %dx
    loop int13_format_fill_sector
    decw %bp
    jnz int13_format_wait_for_drq
    xorb %ah, %ah
    jmp int13_success
int13_geometry:
    pushw %ds
    movw $BDA_SEGMENT, %ax
    movw %ax, %ds
    testb $0x80, %dl
    jnz int13_hdd_geometry
    cmpb $0x00, %dl
    jne int13_geometry_failure
    testb $0x01, BDA_SIM_DISK_FLAGS
    jz int13_geometry_failure
int13_floppy_geometry:
    movw BDA_SIM_FLOPPY_CYLINDERS, %cx
    decw %cx
    movb %cl, %ch
    movb BDA_SIM_FLOPPY_SECTORS, %cl
    movb BDA_SIM_FLOPPY_HEADS, %dh
    decb %dh
    movb $0x01, %dl
    popw %ds
    xorw %ax, %ax
    jmp int13_success_return_cxdx
int13_hdd_geometry:
    cmpb $0x80, %dl
    jne int13_geometry_failure
    testb $0x02, BDA_SIM_DISK_FLAGS
    jne int13_hdd_geometry_present
    jmp int13_geometry_failure
int13_hdd_geometry_present:
    movw BDA_SIM_HDD_CYLINDERS, %cx
    decw %cx
    movb %cl, %ch
    movb BDA_SIM_HDD_SECTORS, %cl
    movb BDA_SIM_HDD_HEADS, %dh
    decb %dh
    movb $0x01, %dl
    popw %ds
    xorw %ax, %ax
    jmp int13_success_return_cxdx
int13_geometry_failure:
    popw %ds
    jmp int13_failure
int13_drive_type:
    pushw %ds
    movw $BDA_SEGMENT, %ax
    movw %ax, %ds
    testb $0x80, %dl
    jnz int13_hdd_type
    cmpb $0x00, %dl
    jne int13_type_failure
    testb $0x01, BDA_SIM_DISK_FLAGS
    jz int13_type_failure
int13_floppy_type:
    popw %ds
    movb $0x01, %ah
    xorw %cx, %cx
    xorw %dx, %dx
    jmp int13_success_return_cxdx
int13_hdd_type:
    cmpb $0x80, %dl
    jne int13_type_failure
    testb $0x02, BDA_SIM_DISK_FLAGS
    jne int13_hdd_type_present
    jmp int13_type_failure
int13_hdd_type_present:
    /* AH=15h returns the 32-bit sector total in conventional CX:DX order. */
    movw BDA_SIM_HDD_TOTAL_SECTORS+2, %cx
    movw BDA_SIM_HDD_TOTAL_SECTORS, %dx
    popw %ds
    movb $0x03, %ah
    jmp int13_success_return_cxdx
int13_type_failure:
    popw %ds
    jmp int13_failure
/* INT 13h AH=41h: report EDD extensions for the fixed ATA disk only. */
int13_extensions:
    cmpw $0x55AA, %bx
    jne int13_failure
    cmpb $0x80, %dl
    jne int13_failure
    pushw %ds
    movw $BDA_SEGMENT, %ax
    movw %ax, %ds
    testb $0x02, BDA_SIM_DISK_FLAGS
    jne int13_extensions_present
    popw %ds
    jmp int13_failure
int13_extensions_present:
    popw %ds
    movw $0xAA55, %bx
    movw $0x0001, %cx
    movb $0x30, %ah
    call int13_diagnostic_success
    pushw %bp
    movw %sp, %bp
    andw $0xFFFE, 16(%bp)
    popw %bp
    clc
    popw %bp
    popw %di
    /* AH=41h returns BX=AA55h, so discard the saved BX/CX/DX inputs. */
    addw $6, %sp
    iret
int13_success:
    call int13_diagnostic_success
    /* IRET restores the caller's saved FLAGS, so update its CF bit explicitly. */
    pushw %bp
    movw %sp, %bp
    andw $0xFFFE, 16(%bp)
    popw %bp
    clc
    popw %bp
    popw %di
    popw %dx
    popw %cx
    popw %bx
    iret
int13_success_return_cxdx:
    call int13_diagnostic_success
    /* IRET restores the caller's saved FLAGS, so update its CF bit explicitly. */
    pushw %bp
    movw %sp, %bp
    andw $0xFFFE, 16(%bp)
    popw %bp
    clc
    popw %bp
    popw %di
    /* AH=08/AH=15 return geometry/type in CX:DX. */
    addw $4, %sp
    popw %bx
    iret
int13_failure:
    call int13_diagnostic_failure
    movb $0x01, %ah
    pushw %bp
    movw %sp, %bp
    orw $0x0001, 16(%bp)
    popw %bp
    stc
    popw %bp
    popw %di
    popw %dx
    popw %cx
    popw %bx
    iret

/*
 * Host-visible diagnostic snapshot.  This project-reserved BDA range is
 * deliberately separate from the ROM's media-configuration block.  It lets
 * the launcher report the last BIOS disk service without participating in the
 * simulated bus transaction or changing any caller-visible register.
 */
int13_diagnostic_enter:
    pushw %di
    movw %ax, %di
    pushw %ds
    movw $BDA_SEGMENT, %ax
    movw %ax, %ds
    incw BDA_SIM_INT13_SEQUENCE
    movw %di, BDA_SIM_INT13_AX
    movw %bx, BDA_SIM_INT13_BX
    movw %cx, BDA_SIM_INT13_CX
    movw %dx, BDA_SIM_INT13_DX
    movb $0xFF, BDA_SIM_INT13_RESULT
    popw %ds
    movw %di, %ax
    popw %di
    ret

int13_diagnostic_success:
    pushw %ax
    pushw %ds
    movw $BDA_SEGMENT, %ax
    movw %ax, %ds
    movb $0x00, BDA_SIM_INT13_RESULT
    popw %ds
    popw %ax
    ret

int13_diagnostic_failure:
    pushw %ax
    pushw %ds
    movw $BDA_SEGMENT, %ax
    movw %ax, %ds
    movb $0x01, BDA_SIM_INT13_RESULT
    popw %ds
    popw %ax
    ret

/* INT 13h AH=48h: write the ATA fixed disk's extended parameter packet. */
int13_extended_parameters:
    cmpb $0x80, %dl
    jne int13_failure
    pushw %ds
    movw $BDA_SEGMENT, %ax
    movw %ax, %ds
    testb $0x02, BDA_SIM_DISK_FLAGS
    jne int13_extended_parameters_present
    popw %ds
    jmp int13_failure
int13_extended_parameters_present:
    movw BDA_SIM_HDD_CYLINDERS, %ax
    xorw %bx, %bx
    movb BDA_SIM_HDD_HEADS, %bl
    xorw %cx, %cx
    movb BDA_SIM_HDD_SECTORS, %cl
    movw BDA_SIM_HDD_TOTAL_SECTORS, %di
    movw BDA_SIM_HDD_TOTAL_SECTORS+2, %bp
    popw %ds
    movw $0x001A, (%si)
    movw $0x0000, 2(%si)
    movw %ax, 4(%si)
    movw $0x0000, 6(%si)
    movw %bx, 8(%si)
    movw $0x0000, 10(%si)
    movw %cx, 12(%si)
    movw $0x0000, 14(%si)
    movw %di, 16(%si)
    movw %bp, 18(%si)
    movw $0x0000, 20(%si)
    movw $0x0000, 22(%si)
    movw $512, 24(%si)
    xorw %ax, %ax
    jmp int13_success

/* Keep the historical AH=41h/48h/02h/03h dispatcher layout above unchanged. */
int13_other_service:
    cmpb $0x42, %ah
    jne int13_other_service_check_write
    movb $0x20, %bh
    jmp int13_extended_transfer
int13_other_service_check_write:
    cmpb $0x43, %ah
    je int13_other_service_write
    jmp int13_failure
int13_other_service_write:
    movb $0x30, %bh
    jmp int13_extended_transfer

/* INT 13h AH=42h/43h: EDD packet read/write for the fixed ATA slot. */
int13_extended_transfer:
    cmpb $0x80, %dl
    je int13_edd_is_fixed_disk
    jmp int13_failure
int13_edd_is_fixed_disk:
    pushw %es
    movw $BDA_SEGMENT, %ax
    movw %ax, %es
    testb $0x02, %es:BDA_SIM_DISK_FLAGS
    popw %es
    jnz int13_edd_drive_present
    jmp int13_failure
int13_edd_drive_present:
    pushw %ds
    pushw %si
    cmpb $0x10, (%si)
    je int13_edd_dap_size_valid
    jmp int13_extended_transfer_failure
int13_edd_dap_size_valid:
    cmpb $0x00, 1(%si)
    je int13_edd_dap_reserved_valid
    jmp int13_extended_transfer_failure
int13_edd_dap_reserved_valid:
    movw 2(%si), %bp
    testw %bp, %bp
    jnz int13_edd_dap_nonzero_count
    jmp int13_extended_transfer_failure
int13_edd_dap_nonzero_count:
    cmpw $255, %bp
    jbe int13_edd_dap_count_valid
    jmp int13_extended_transfer_failure
int13_edd_dap_count_valid:
    cmpb $0x00, 11(%si)
    je int13_edd_lba_byte_valid
    jmp int13_extended_transfer_failure
int13_edd_lba_byte_valid:
    cmpw $0x0000, 12(%si)
    je int13_edd_lba_high_low_valid
    jmp int13_extended_transfer_failure
int13_edd_lba_high_low_valid:
    cmpw $0x0000, 14(%si)
    je int13_edd_lba_valid
    jmp int13_extended_transfer_failure
int13_edd_lba_valid:
    movw 4(%si), %di
    movw 6(%si), %ax
    movw %ax, %es
    movw $0x01F2, %dx
    movw %bp, %ax
    outb %al, %dx
    incw %dx
    movb 8(%si), %al
    outb %al, %dx
    incw %dx
    movb 9(%si), %al
    outb %al, %dx
    incw %dx
    movb 10(%si), %al
    outb %al, %dx
    incw %dx
    movb $0xF0, %al
    outb %al, %dx
    incw %dx
    movb %bh, %al
    cmpb $0x20, %bh
    jne int13_edd_write_command
    outb %al, %dx
    jmp int13_edd_read_wait_for_drq
int13_edd_write_command:
    outb %al, %dx
    jmp int13_edd_write_wait_for_drq
int13_edd_read_wait_for_drq:
    movw $0x03F6, %dx
    inb %dx, %al
    testb $0x01, %al
    jz int13_edd_read_no_error
    jmp int13_extended_transfer_after_command_failure
int13_edd_read_no_error:
    testb $0x08, %al
    jz int13_edd_read_wait_for_drq
    movw $0x01F0, %dx
    movw $256, %cx
int13_edd_copy_read:
    inw %dx, %ax
    movw %ax, %es:(%di)
    addw $2, %di
    jnc int13_edd_read_no_segment_wrap
    pushw %ax
    movw %es, %ax
    addw $0x1000, %ax
    movw %ax, %es
    popw %ax
int13_edd_read_no_segment_wrap:
    loop int13_edd_copy_read
    decw %bp
    jnz int13_edd_read_wait_for_drq
    movw $0, 2(%si)
    popw %si
    popw %ds
    xorw %ax, %ax
    jmp int13_success
int13_edd_write_wait_for_drq:
    movw $0x03F6, %dx
    inb %dx, %al
    testb $0x01, %al
    jz int13_edd_write_no_error
    jmp int13_extended_transfer_after_command_failure
int13_edd_write_no_error:
    testb $0x08, %al
    jz int13_edd_write_wait_for_drq
    movw $0x01F0, %dx
    movw $256, %cx
int13_edd_copy_write:
    movw %es:(%di), %ax
    outw %ax, %dx
    addw $2, %di
    jnc int13_edd_write_no_segment_wrap
    pushw %ax
    movw %es, %ax
    addw $0x1000, %ax
    movw %ax, %es
    popw %ax
int13_edd_write_no_segment_wrap:
    loop int13_edd_copy_write
    decw %bp
    jnz int13_edd_write_wait_for_drq
    movw $0, 2(%si)
    popw %si
    popw %ds
    xorw %ax, %ax
    jmp int13_success
int13_extended_transfer_failure:
    popw %si
    popw %ds
    jmp int13_failure
int13_extended_transfer_after_command_failure:
    popw %si
    popw %ds
    jmp int13_failure

/* INT 16h consumes the keyboard ring populated by IRQ1. */
int16:
    cmpb $0x00, %ah
    je int16_get_key
    /* AT-compatible extended read: our Set-1 BIOS ring already stores
       extended navigation keys as scan-code:00h words, so it shares AH=00h. */
    cmpb $0x10, %ah
    je int16_get_key
    cmpb $0x01, %ah
    je int16_check_key
    cmpb $0x11, %ah
    je int16_check_key
    cmpb $0x02, %ah
    je int16_shift_flags
    cmpb $0x12, %ah
    je int16_extended_shift_flags
    iret
int16_get_key:
    pushw %bx
    pushw %ds
    movw $BDA_SEGMENT, %bx
    movw %bx, %ds
int16_wait:
    cli
    movw BDA_KB_HEAD, %bx
    cmpw BDA_KB_TAIL, %bx
    sti
    je int16_wait
    movw (%bx), %ax
    addw $2, %bx
    cmpw $BDA_KB_END, %bx
    jb int16_store_head
    movw $BDA_KB_BEGIN, %bx
int16_store_head:
    movw %bx, BDA_KB_HEAD
    popw %ds
    popw %bx
    iret
int16_check_key:
    pushw %bp
    movw %sp, %bp
    pushw %bx
    pushw %ds
    movw $BDA_SEGMENT, %bx
    movw %bx, %ds
    movw BDA_KB_HEAD, %bx
    cmpw BDA_KB_TAIL, %bx
    je int16_empty
    movw (%bx), %ax
    andw $0xFFBF, 6(%bp)
    jmp int16_check_done
int16_empty:
    orw $0x0040, 6(%bp)
int16_check_done:
    popw %ds
    popw %bx
    popw %bp
    iret
int16_shift_flags:
    pushw %ds
    movw $BDA_SEGMENT, %ax
    movw %ax, %ds
    movb BDA_KB_FLAGS, %al
    popw %ds
    iret
int16_extended_shift_flags:
    pushw %ds
    movw $BDA_SEGMENT, %ax
    movw %ax, %ds
    movb BDA_KB_FLAGS, %al
    xorb %ah, %ah
    popw %ds
    iret

int19:
    jmp boot_from_disk

int1a:
    cmpb $0x00, %ah
    jne int1a_failure
    pushw %ds
    movw $BDA_SEGMENT, %ax
    movw %ax, %ds
    movw BDA_TICKS_LOW, %dx
    movw BDA_TICKS_HIGH, %cx
    xorw %ax, %ax
    popw %ds
    pushw %bp
    movw %sp, %bp
    andw $0xFFFE, 6(%bp)
    popw %bp
    iret
int1a_failure:
    pushw %bp
    movw %sp, %bp
    orw $0x0001, 6(%bp)
    popw %bp
    iret

/* No current device is wired to the slave PIC; keep an unexpected IRQ harmless. */
int70:
    pushw %ax
    movb $0x20, %al
    outb %al, $0xA0
    outb %al, $0x20
    popw %ax
    iret

disk_error:
    hlt
    jmp disk_error

/* Set-1 make-code to ASCII map for the text-mode DOS input path. */
ascii_table:
    .byte 0, 0x1B, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', 8, 9
    .byte 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', 13, 0, 'a', 's'
    .byte 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`', 0, '\\', 'z', 'x', 'c', 'v'
    .byte 'b', 'n', 'm', ',', '.', '/', 0, '*', 0, ' ', 0, 0, 0, 0, 0, 0
    .zero 64

.org 0xFFF0
    ljmp $0xF000, $0x0000
