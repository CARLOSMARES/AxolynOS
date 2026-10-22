; ==========================================================================
; AxolynOS - Kernel de prueba del bootloader
; ==========================================================================
; Este fichero NO forma parte del kernel real: es un kernel minimo que solo
; sirve para comprobar, bajo QEMU, que el bootloader hace bien su trabajo:
;
;   1. Lee el disco con INT 13h AH=42h (stage1 y stage2).
;   2. Detecta la memoria con INT 15h AX=E820h.
;   3. Carga el ELF64 en kernel_phys y el initrd en initrd_phys.
;   4. Monta las tablas de pagina y salta a modo largo.
;   5. Salta a e_entry con RDI = direccion fisica del AxBootInfo.
;
; Al entrar, el kernel de prueba:
;   - Configura el puerto serie COM1 (0x3F8) para volcar la informacion.
;   - Imprime el contenido del AxBootInfo (magic, kernel_phys/virt,
;     initrd_phys/size, mem_size, e820_entries, e820_ptr, cmdline).
;   - Imprime el mapa E820 completo.
;   - Comprueba que la identidad de la memoria baja funciona leyendo el
;     AxBootInfo a traves del mapa de identidad del bootloader.
;   - Se queda parado.
;
; El enlace se hace con testkernel.ld: la imagen entra en 0x200000 fisico y
; se ejecuta en 0xFFFFFFFF80200000 virtual (la ventana que mapea el
; bootloader).
;
; Salida esperada (consola serie de QEMU):
;
;   === AxolynOS: kernel de prueba ===
;   magic            = 0x4158424f4f54494e
;   kernel_phys      = 0x0000000000200000
;   ...
;   AXOS-TEST: OK
; ==========================================================================

        [bits 64]
        default rel

; --- Puertos del puerto serie COM1 ----------------------------------------
COM1_DATA   equ 0x3F8
COM1_INT_EN equ 0x3F9
COM1_FIFO   equ 0x3FA
COM1_LCR    equ 0x3FB
COM1_LSR    equ 0x3FD

; --- Direcciones utiles ---------------------------------------------------
VGA_TEXT    equ 0xB8000

section .text
global _start
_start:
        cli
        cld
        mov     rsp, stack_top

        call    serial_init

        lea     rdi, [rel msg_banner]
        call    serial_puts

        ; RDI = direccion fisica del AxBootInfo que entrega el bootloader.
        mov     rbx, rdi
        test    rbx, rbx
        jz      .sin_bootinfo

        ; --- magic ---------------------------------------------------------
        lea     rdi, [rel msg_magic]
        call    serial_puts
        mov     rdi, [rbx+0]
        call    serial_puthex
        lea     rdi, [rel nl]
        call    serial_puts

        ; Comprobacion: la magic debe ser "AXBOOTIN" (0x4158424F4F54494E).
        mov     rax, [rbx+0]
        mov     rcx, 0x4158424F4F54494E
        cmp     rax, rcx
        jne     .magic_mal

        ; --- kernel_phys ---------------------------------------------------
        lea     rdi, [rel msg_kphys]
        call    serial_puts
        mov     rdi, [rbx+8]
        call    serial_puthex
        call    newline

        ; --- kernel_virt ---------------------------------------------------
        lea     rdi, [rel msg_kvirt]
        call    serial_puts
        mov     rdi, [rbx+16]
        call    serial_puthex
        call    newline

        ; --- initrd_phys ---------------------------------------------------
        lea     rdi, [rel msg_iphys]
        call    serial_puts
        mov     rdi, [rbx+24]
        call    serial_puthex
        call    newline

        ; --- initrd_size ---------------------------------------------------
        lea     rdi, [rel msg_isize]
        call    serial_puts
        mov     rdi, [rbx+32]
        call    serial_puthex
        call    newline

        ; --- mem_size ------------------------------------------------------
        lea     rdi, [rel msg_memsize]
        call    serial_puts
        mov     rdi, [rbx+40]
        call    serial_puthex
        call    newline

        ; --- e820_entries --------------------------------------------------
        lea     rdi, [rel msg_e820n]
        call    serial_puts
        mov     rdi, [rbx+48]
        call    serial_putdec
        call    newline

        ; --- e820_ptr ------------------------------------------------------
        lea     rdi, [rel msg_e820p]
        call    serial_puts
        mov     rdi, [rbx+56]
        call    serial_puthex
        call    newline

        ; --- cmdline -------------------------------------------------------
        lea     rdi, [rel msg_cmdline]
        call    serial_puts
        mov     rdi, [rbx+64]
        call    serial_puthex
        call    newline

        ; --- mapa E820 -----------------------------------------------------
        call    dump_e820

        ; --- comprobacion del mapa de identidad -----------------------------
        ; El bootloader mapea identidad los primeros 4 GiB con paginas de
        ; 2 MiB.  Leyendo 0x1000 a traves de esa identidad tiene que dar el
        ; mismo AxBootInfo que nos pasaron en RDI.
        lea     rdi, [rel msg_ident]
        call    serial_puts
        mov     rax, [0x1000]
        mov     rcx, [rbx+0]
        cmp     rax, rcx
        jne     .ident_mal
        lea     rdi, [rel msg_ok]
        call    serial_puts

        ; --- estado de la CPU ----------------------------------------------
        lea     rdi, [rel msg_cr0]
        call    serial_puts
        mov     rax, cr0
        call    serial_puthex
        call    newline

        lea     rdi, [rel msg_cr3]
        call    serial_puts
        mov     rax, cr3
        call    serial_puthex
        call    newline

        lea     rdi, [rel msg_ok]
        call    serial_puts

        lea     rdi, [rel msg_vga]
        call    vga_puts

        jmp     .hang

.sin_bootinfo:
        lea     rdi, [rel msg_noboot]
        call    serial_puts
        jmp     .hang
.magic_mal:
        lea     rdi, [rel msg_badmagic]
        call    serial_puts
        jmp     .hang
.ident_mal:
        lea     rdi, [rel msg_badident]
        call    serial_puts
        jmp     .hang
.hang:
        cli
        hlt
        jmp     .hang

; --------------------------------------------------------------------------
; dump_e820: imprime el mapa de memoria que dejo el firmware.  Usa RBX, que
; sigue apuntando al AxBootInfo.
; --------------------------------------------------------------------------
dump_e820:
        push    rbx
        push    r12
        push    r13
        push    r14
        push    r15
        mov     r12, [rbx+48]        ; numero de entradas
        mov     r13, [rbx+56]        ; puntero a la tabla
        lea     rdi, [rel msg_e820]
        call    serial_puts
        xor     r14, r14
.loop:
        cmp     r14, r12
        jae     .done
        ; r15 = direccion de la entrada (24 bytes)
        mov     rax, r14
        shl     rax, 5
        sub     rax, r14
        add     rax, r13              ; r14 * 24
        mov     r15, rax
        mov     rdi, [r15]            ; base
        call    serial_puthex
        lea     rdi, [rel sep]
        call    serial_puts
        mov     rdi, [r15+8]          ; longitud
        call    serial_puthex
        lea     rdi, [rel sep]
        call    serial_puts
        movzx   rdi, dword [r15+16]   ; tipo
        call    serial_putdec
        call    newline
        inc     r14
        jmp     .loop
.done:
        pop     r15
        pop     r14
        pop     r13
        pop     r12
        pop     rbx
        ret

; ==========================================================================
; Rutinas de bajo nivel
; ==========================================================================

; --- serial_init: configura COM1 a 115200 baudios, 8N1 -------------------
serial_init:
        push    rax
        push    rdx
        mov     dx, COM1_INT_EN
        xor     al, al
        out     dx, al
        mov     dx, COM1_LCR
        mov     al, 0x80                ; DLAB = 1
        out     dx, al
        mov     dx, COM1_DATA
        mov     al, 0x01                ; divisor 1 -> 115200 baudios
        out     dx, al
        mov     dx, COM1_INT_EN
        xor     al, al
        out     dx, al
        mov     dx, COM1_LCR
        mov     al, 0x03                ; 8 bits, sin paridad, 1 bit de parada
        out     dx, al
        mov     dx, COM1_FIFO
        mov     al, 0xC7                ; activar y vaciar la FIFO
        out     dx, al
        pop     rdx
        pop     rax
        ret

; --- serial_putc: AL = caracter ------------------------------------------
serial_putc:
        push    rbx
        push    rdx
        mov     bl, al
.wait:
        mov     dx, COM1_LSR
        in      al, dx
        test    al, 0x20                ; LSR bit 5: THR vacio
        jz      .wait
        pop     rdx
        mov     al, bl
        mov     dx, COM1_DATA
        out     dx, al
        pop     rbx
        ret

; --- serial_puts: RDI = cadena terminada en 0 ----------------------------
serial_puts:
        push    rbx
        push    rdi
        mov     rbx, rdi
.next:
        mov     al, [rbx]
        test    al, al
        jz      .done
        call    serial_putc
        inc     rbx
        jmp     .next
.done:
        pop     rdi
        pop     rbx
        ret

; --- serial_puthex: RDI = valor de 64 bits -------------------------------
serial_puthex:
        push    rbx
        push    rdi
        push    rsi
        mov     rbx, rdi
        mov     al, '0'
        call    serial_putc
        mov     al, 'x'
        call    serial_putc
        mov     rsi, 60
.loop:
        mov     rcx, rsi
        mov     rax, rbx
        shr     rax, cl
        and     al, 0x0F
        cmp     al, 10
        jb      .digit
        add     al, 'a' - 10
        jmp     .emit
.digit:
        add     al, '0'
.emit:
        call    serial_putc
        dec     rsi
        jnz     .loop
        pop     rsi
        pop     rdi
        pop     rbx
        ret

; --- serial_putdec: RDI = valor sin signo de 64 bits ---------------------
serial_putdec:
        push    rbx
        push    rdi
        push    rsi
        push    rdx
        mov     rbx, rdi
        mov     rsi, 10
        lea     rdi, [rel numbuf+31]
        mov     byte [rdi], 0
.next:
        mov     rax, rbx
        xor     rdx, rdx
        div     rsi
        add     dl, '0'
        dec     rdi
        mov     [rdi], dl
        mov     rbx, rax
        test    rbx, rbx
        jnz     .next
        call    serial_puts
        pop     rdx
        pop     rsi
        pop     rdi
        pop     rbx
        ret

; --- newline ---------------------------------------------------------------
newline:
        push    rax
        mov     al, 10
        call    serial_putc
        mov     al, 13
        call    serial_putc
        pop     rax
        ret

; --- vga_puts: RDI = cadena terminada en 0 (texto en 0xB8000) -------------
vga_puts:
        push    rbx
        push    rdi
        push    rsi
        mov     rbx, rdi
        mov     rsi, VGA_TEXT
.next:
        mov     al, [rbx]
        test    al, al
        jz      .done
        mov     [rsi], al
        mov     byte [rsi+1], 0x0F
        add     rsi, 2
        inc     rbx
        jmp     .next
.done:
        pop     rsi
        pop     rdi
        pop     rbx
        ret

; ==========================================================================
; Datos
; ==========================================================================
section .rodata
msg_banner:    db "=== AxolynOS: kernel de prueba ===", 10, 13, 0
msg_magic:     db "magic            = ", 0
msg_kphys:     db "kernel_phys      = ", 0
msg_kvirt:     db "kernel_virt      = ", 0
msg_iphys:     db "initrd_phys      = ", 0
msg_isize:     db "initrd_size      = ", 0
msg_memsize:   db "mem_size         = ", 0
msg_e820n:     db "e820_entries     = ", 0
msg_e820p:     db "e820_ptr         = ", 0
msg_cmdline:   db "cmdline          = ", 0
msg_ident:     db "identidad baja   = ", 0
msg_e820:      db "--- mapa E820 ---", 10, 13, 0
msg_cr0:       db "cr0              = ", 0
msg_cr3:       db "cr3              = ", 0
msg_ok:        db "AXOS-TEST: OK", 10, 13, 0
msg_noboot:    db "AXOS-TEST: no hay AxBootInfo", 10, 13, 0
msg_badmagic:  db "AXOS-TEST: magic incorrecta", 10, 13, 0
msg_badident:  db "AXOS-TEST: identidad rota", 10, 13, 0
msg_vga:       db "AxolynOS: kernel de prueba OK", 0
nl:            db 10, 13, 0
sep:           db " ", 0

section .bss
numbuf: resb 32
stack: resb 4096
stack_top:
