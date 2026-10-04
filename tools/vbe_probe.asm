; ============================================================
;  BerryOS - VBE probe (16-bit real mode diagnostic)
;
;  Settles the VESAModeInfoBlock offsets by observation.  They are not
;  consistent across firmware revisions: dumping a text mode shows
;  80 / 40 / 16 sitting at 0x10 / 0x12 / 0x19, while boot.S in this
;  project reads XResolution from 0x12 and YResolution from 0x14.  A
;  wrong offset is silent -- you read a bitfield, get a plausible
;  framebuffer address, and the screen is black rather than erroring.
;  (That is not hypothetical: the 32-bit BerryOS tree reads
;  PhysBasePtr from 0x28, which is GreenLsbPosition, and never draws.)
;
;  This version queries ONE known-good mode instead of walking the list,
;  because PhysBasePtr is only filled in once the mode is ACTIVE, and
;  switching modes mid-walk upsets the list cursor.
;
;  Two stages: a boot sector is 512 bytes and this is bigger.  Stage 1
;  reads stage 2 to 0x7E00 and jumps to it -- the same trick the main
;  bootloader uses.
;
;  Output goes to QEMU's debugcon at port 0xE9 (no UART setup needed).
;
;  NASM requires numeric labels in column 1.
;
;  Build: nasm -f bin vbe_probe.asm -o vbe_probe.bin
;  Run:   qemu -drive file=vbe_probe.bin,format=raw,if=floppy \
;           -debugcon file:probe.log
; ============================================================
BITS 16
ORG 0x7C00

; ---------------- stage 1 ----------------
start:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00
    mov al, '1'               ; stage 1 is alive
    out 0xE9, al

    ; CHS read (INT 13h AH=02): no DAP, so nothing to disagree about.
    mov bx, 0x7E00
    mov dl, [boot_drv]
    mov ah, 0x02
    mov al, 2                 ; two sectors
    xor ch, ch                ; cylinder 0
    mov cl, 2                 ; sector 2 (1-based)
    xor dh, dh                ; head 0
    int 0x13
    jnc .read_ok
    mov al, 'E'
    out 0xE9, al
    jmp  halt
.read_ok:
    mov al, '2'
    out 0xE9, al

    ; CS is 0, so a near jump to a stage-2 label is an absolute jump.
    jmp  probe_entry

halt:
    hlt
    jmp halt

boot_drv: db 0

hexdig2:                     ; AL = nibble -> hex char
    push ax
    cmp al, 10
    jb   .num
    add al, 55
    jmp  .emit
.num:
    add al, 48
.emit:
    out 0xE9, al
    pop ax
    ret

    times 510 - ($ - $$) db 0
    dw 0xAA55

; ---------------- stage 2 ----------------
; No ORG here: ORG 0x7C00 above already established the addresses, so
; file offset 512 is memory address 0x7E00, which is where stage 1 loads.

putc:                       ; AL = char
    out 0xE9, al
    ret

puts:                       ; DS:SI = NUL-terminated
    push ax
lputs_next:
    lodsb
    test al, al
    jz   lputs_done
    out 0xE9, al
    jmp  lputs_next
lputs_done:
    pop ax
    ret

hexdig:                     ; AL = nibble -> one hex char
    cmp al, 10
    jb   hd_num
    add al, 55
    jmp  hd_out
hd_num:
    add al, 48
hd_out:
    out 0xE9, al
    ret

putb:                       ; AL = byte -> 2 hex chars
    push bx
    push dx
    mov dl, al
    mov al, dl
    shr al, 4
    call hexdig
    mov al, dl
    and al, 0x0F
    call hexdig
    pop dx
    pop bx
    ret

putw:                       ; AX = word -> 4 hex chars
    push dx
    push bx
    mov dx, ax
    mov al, dh
    call putb
    mov al, dl
    call putb
    pop bx
    pop dx
    ret

putnl:
    mov al, 13
    call putc
    mov al, 10
    call putc
    ret

sep:       db " ", 0
banner:    db "=== VBE PROBE (single mode) ===", 10, 13, 0
msg_ver:   db "VBEver=", 0
msg_set:   db "SetMode ret AL=", 0
msg_get:   db " GetModeInfo ret AL=", 0
msg_tail:  db ")", 10, 13, 0
msg_modes: db "--- mode attr x y bpp l32 b28 b3d b1c b20 b40 ---", 10, 13, 0
msg_dump:  db "--- raw mode-info ---", 10, 13, 0
msg_done:  db "=== END ===", 10, 13, 0

TEST_MODE   equ 0x11B        ; 1280x1024x32, present on SeaBIOS and OVMF

probe_entry:
    mov al, '3'               ; stage 2 is running
    out 0xE9, al
    mov si, banner
    call puts

    ; ---- firmware version ----
    mov ax, 0x4F00
    mov di, 0x0500
    int 0x10
    mov si, msg_ver
    call puts
    mov ax, [0x0504]
    call putw
    call putnl

    ; ---- ACTIVATE the mode (bit 14 = linear framebuffer) ----
    mov bx, TEST_MODE
    or  bx, 0x4000
    xor ax, ax
    mov es, ax
    mov ax, 0x4F02
    int 0x10
    mov si, msg_set
    call puts
    call putb
    mov ax, TEST_MODE
    call putw

    ; ---- now read its mode-info ----
    mov cx, TEST_MODE
    xor ax, ax
    mov es, ax                ; ES must be 0: 0x4F01 writes to ES:DI
    mov di, 0x0600
    mov ax, 0x4F01
    int 0x10
    mov si, msg_get
    call puts
    call putb
    lea si, [msg_tail]
    call puts

    ; Skip the per-field printout: 128 raw bytes below contain all of it, and
    ; one lodsb loop is fewer things to go wrong than a dozen calls.

    ; ---- raw dump ----
    mov si, msg_dump
    call puts
    mov cx, 8                 ; 8 lines x 16 bytes
    mov di, 0x0000
dump_line:
    push cx
    mov ax, di
    call putb
    mov al, ':'
    call putc
    mov al, ' '
    call putc
    mov bx, 0x0600
    add bx, di
    mov si, bx
    mov cx, 16
dump_byte:
    lodsb
    call putb
    mov al, ' '
    call putc
    loop dump_byte
    call putnl
    pop cx
    add di, 16
    cmp di, 0x80
    jb   dump_line

    mov si, msg_done
    call puts
    jmp  $
