.code16
.section .text
.global _start

_start:
    cli
    xorw %ax, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %ss
    movw $0x7000, %sp

    movw $int13, 0x004C
    movw $0xF000, 0x004E

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

int13:
    pushw %bx
    pushw %cx
    pushw %dx
    pushw %di
    cmpb $0x00, %ah
    jne int13_check_geometry
    jmp int13_success
int13_check_geometry:
    cmpb $0x08, %ah
    jne int13_check_read
    jmp int13_geometry
int13_check_read:
    cmpb $0x02, %ah
    je int13_transfer
    cmpb $0x03, %ah
    je int13_transfer
    jmp int13_failure

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

wait_for_drq:
    movw $0x03F6, %dx
    inb %dx, %al
    testb $0x01, %al
    jnz int13_transfer_failure
    testb $0x08, %al
    jz wait_for_drq

    movw $0x01F0, %dx
    movw %bx, %di
    popw %ax
    movb %al, %ch
    xorb %cl, %cl
    cmpb $0x02, %ah
    jne copy_sector_write
copy_sector:
    inw %dx, %ax
    movw %ax, %es:(%di)
    incw %di
    incw %di
    loop copy_sector
    xorb %ah, %ah
    jmp int13_success

copy_sector_write:
    movw %es:(%di), %ax
    outw %ax, %dx
    incw %di
    incw %di
    loop copy_sector_write
    xorb %ah, %ah
    jmp int13_success

int13_transfer_failure:
    popw %ax
    jmp int13_failure

int13_geometry:
    testb $0x80, %dl
    jnz int13_hard_geometry
    movw $0x4F12, %cx
    movb $0x01, %dh
    movb $0x01, %dl
    xorw %ax, %ax
    jmp int13_success

int13_hard_geometry:
    movw $0xFFFF, %cx
    movb $0x0F, %dh
    movb $0x01, %dl
    xorw %ax, %ax
    jmp int13_success

int13_success:
    clc
    popw %di
    popw %dx
    popw %cx
    popw %bx
    iret

int13_failure:
    movb $0x01, %ah
    stc
    popw %di
    popw %dx
    popw %cx
    popw %bx
    iret

disk_error:
    hlt
    jmp disk_error

.org 0xFFF0
    ljmp $0xF000, $0x0000
