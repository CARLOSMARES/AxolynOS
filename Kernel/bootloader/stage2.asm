; ==========================================================================
; AxolynOS - Bootloader stage 2
; ==========================================================================
; Cargado por stage1 en 0000:8000 (LBA 1). Hace todo el trabajo real:
;   1. A20 (INT 15h AX=2401h, con repliegue a AX=2402h).
;   2. Construccion de la GDT de 32 y de 64 bits.
;   3. Mapa de memoria con INT 15h AX=E820h (32 entradas en 0x9000).
;   4. Lectura de la cabecera de disco "AXDSK001" (LBA 64).
;   5. Carga del kernel.elf (ELF64, segmentos PT_LOAD) en kernel_phys.
;   6. Carga del initrd.axfs en initrd_phys.
;   7. Tablas de pagina de 4 niveles en 0x70000.
;   8. Relleno de AxBootInfo en 0x1000.
;   9. Modo protegido de 32 bits -> modo largo -> salto a e_entry.
;
; ORDEN REAL/PROTEGIDO:
;   Las llamadas al BIOS (INT 13h / INT 15h) se hacen en modo real porque
;   SeaBIOS calcula las direcciones lineales como (segmento<<4)+desplazamiento
;   (GET_SEG), lo que es incompatible con un GDT plano en modo protegido.
;   Por eso la GDT se construye aqui (en modo real) y la entrada en modo
;   protegido se hace DESPUES de cargar el disco, tal y como exige el
;   diseno (GDT null/0x08/0x10, CR0.PE y salto lejano a 0x08).
;
; CONVENCIONES DE NASM QUE IMPORTAN:
;   * En [bits 16] el tamano de operando por defecto es 16 bits.  Para
;     mover 32 bits hay que escribirlo explicitamente (dword) o usar
;     registros de 32 bits, que NASM promociona a 32 bits.
;   * En un binario plano con [org], las referencias a etiquetas se
;     reubican a la direccion ABSOLUTA (org + desplazamiento).  Por eso
;     los saltos lejanos NO llevan +STAGE2_BASE (NASM ya lo suma).
;   * Las direcciones de datos de este fichero viven en 0x8000+, asi que
;     en modo real (DS=0) un displacement de 16 bits es suficiente.
; ==========================================================================

        [bits 16]
        [org 0x8000]

; --- Direcciones de trabajo en memoria baja -------------------------------
STAGE_BUF    equ 0x2000          ; buffer de staging (8 sectores = 4 KiB)
DAP          equ 0x3000          ; Disk Address Packet
HDR_BUF      equ 0x0700          ; cabecera de disco (512 bytes)
ELF_BUF      equ 0x0900          ; cabecera ELF64 (512 bytes)
PHDR_BUF     equ 0x0A00          ; cabecera de programa ELF (64 bytes)
BOOTINFO     equ 0x1000          ; estructura AxBootInfo (72 bytes)
ENTRY_SAVE   equ 0x6000          ; e_entry del kernel (8 bytes)
BOOTINFO_PTR equ 0x6010          ; direccion fisica de AxBootInfo (8 bytes)
GDT32        equ 0x6800          ; GDT de 32 bits (null, 0x08, 0x10)
GDT64        equ 0x6840          ; GDT de 64 bits (null, 0x08, 0x10)
STACK_TOP    equ 0x6F00          ; cima de la pila (crece hacia abajo)
E820_BUF     equ 0x9000          ; tabla E820 (32 entradas x 24 bytes)

; --- Direcciones de las tablas de pagina ----------------------------------
PML4_ADDR    equ 0x70000
PDPT_LOW     equ 0x71000
PDPT_HIGH    equ 0x72000
PD_LOW0      equ 0x73000         ; 4 PD: 0x73000, 0x74000, 0x75000, 0x76000
PD_HIGH      equ 0x77000
PT_KERNEL    equ 0x78000         ; PT del kernel (0x78000..0x7FFFFF)

; --- Parametros -----------------------------------------------------------
E820_MAX     equ 32              ; entradas E820 como maximo
E820_STRIDE  equ 24              ; {u64 base; u64 length; u32 type; u32 res}
SECTOR_SIZE  equ 512
STAGE_SECT   equ 8              ; sectores que caben en el buffer de staging

; --- Puerto serie (COM1) para diagnostico ----------------------------------
COM1         equ 0x3F8           ; base del UART 16550

; --- Flags de las entradas de tabla de pagina ------------------------------
PTE_P        equ 0x001           ; presente
PTE_RW       equ 0x002           ; escribible
PTE_PS       equ 0x080           ; pagina de 2 MiB

; --- Offsets de la cabecera AXDSK -----------------------------------------
OFF_MAGIC    equ 0x00
OFF_VERSION  equ 0x08
OFF_S2SECT   equ 0x0C
OFF_S2LBA    equ 0x10
OFF_KLBA     equ 0x18
OFF_KSECT    equ 0x20
OFF_ILBA     equ 0x28
OFF_ISECT    equ 0x30
OFF_KPHYS    equ 0x38
OFF_KVIRT    equ 0x40
OFF_IPHYS    equ 0x48
OFF_BIPHYS   equ 0x50
OFF_E820P    equ 0x58
OFF_MEMSIZE  equ 0x60

; ==========================================================================
; Entrada de stage2 (modo real). Llega con:
;   AL = sectores cargados, BX = 0x8000, CH = sectores/pista,
;   CL = cabezas, DH = cilindros, DL = unidad de arranque
; ==========================================================================
stage2_entry:
        cli
        cld
        xor     ax, ax
        mov     ds, ax                  ; DS = 0 (segmento plano)
        mov     es, ax
        mov     ss, ax
        mov     sp, STACK_TOP
        sti
        mov     [drive], dl              ; unidad de arranque (de stage1)

        ; ------------------------------------------------------------------
        ; -1. Diagnostico por el puerto serie (COM1)
        ; ------------------------------------------------------------------
        call    serial_init
        mov     si, msg_banner
        call    serial_puts

        ; ------------------------------------------------------------------
        ; 0. Limpiar el Disk Address Packet
        ; ------------------------------------------------------------------
        xor     eax, eax
        mov     edi, DAP
        mov     ecx, 8
        rep     stosd

        ; ------------------------------------------------------------------
        ; 1. Habilitar A20
        ; ------------------------------------------------------------------
        mov     ax, 0x2401
        int     0x15
        jnc     .a20_ok
        mov     ax, 0x2402              ; repliegue: via teclado
        int     0x15
.a20_ok:
        mov     si, msg_a20
        call    serial_puts

        ; ------------------------------------------------------------------
        ; 2. Construir las GDT (32 y 64 bits). Se cargan mas adelante.
        ; ------------------------------------------------------------------
        call    build_gdt
        mov     si, msg_gdt
        call    serial_puts

        ; ------------------------------------------------------------------
        ; 3. Mapa de memoria E820 -> 0x9000 y calculo de mem_size
        ; ------------------------------------------------------------------
        call    read_e820
        mov     si, msg_e820
        call    serial_puts
        mov     eax, [mem_size]
        call    serial_puthex32
        mov     si, msg_nl
        call    serial_puts

        ; ------------------------------------------------------------------
        ; 4. Cabecera de disco (LBA 64) -> 0x0700
        ; ------------------------------------------------------------------
        mov     eax, 64
        xor     esi, esi
        mov     ecx, SECTOR_SIZE
        mov     edi, HDR_BUF
        call    read_file_bytes
        jc      .err_header
        cmp     dword [HDR_BUF+OFF_MAGIC], 'AXDS'
        jne     .err_header
        cmp     dword [HDR_BUF+OFF_MAGIC+4], 'K001'
        jne     .err_header

        ; Valores de 32 bits de la cabecera
        mov     eax, [HDR_BUF+OFF_KLBA]
        mov     [kernel_lba], eax
        mov     eax, [HDR_BUF+OFF_KSECT]
        mov     [kernel_sectors], eax
        mov     eax, [HDR_BUF+OFF_ILBA]
        mov     [initrd_lba], eax
        mov     eax, [HDR_BUF+OFF_ISECT]
        mov     [initrd_sectors], eax
        mov     eax, [HDR_BUF+OFF_KPHYS]
        mov     [kernel_phys], eax
        mov     eax, [HDR_BUF+OFF_IPHYS]
        mov     [initrd_phys], eax
        mov     eax, [HDR_BUF+OFF_BIPHYS]
        mov     [bootinfo_phys], eax
        mov     eax, [HDR_BUF+OFF_E820P]
        mov     [e820_phys], eax
        jmp     .hdr_ok
.err_header:
        mov     si, msg_header
        call    fatal_real
.hdr_ok:
        mov     si, msg_hdr
        call    serial_puts
        mov     eax, [kernel_lba]
        call    serial_puthex32
        mov     si, msg_space
        call    serial_puts
        mov     eax, [kernel_sectors]
        call    serial_puthex32
        mov     si, msg_nl
        call    serial_puts

        ; ------------------------------------------------------------------
        ; 5. Cargar el kernel (ELF64): cabecera y segmentos PT_LOAD
        ; ------------------------------------------------------------------
        call    load_kernel
        mov     si, msg_kern
        call    serial_puts

        ; ------------------------------------------------------------------
        ; 6. Cargar el initrd completo en initrd_phys
        ; ------------------------------------------------------------------
        mov     eax, [initrd_lba]
        xor     esi, esi
        mov     ecx, [initrd_sectors]
        shl     ecx, 9
        mov     edi, [initrd_phys]
        call    read_file_bytes
        jc      .err_initrd
        mov     eax, [initrd_sectors]
        shl     eax, 9
        mov     [initrd_size], eax
        jmp     .initrd_ok
.err_initrd:
        mov     si, msg_initrd_err
        call    fatal_real
.initrd_ok:
        mov     si, msg_initrd
        call    serial_puts

        ; ------------------------------------------------------------------
        ; 7..9. Modo protegido de 32 bits: tablas de pagina, bootinfo y
        ;       entrada en modo largo.
        ; ------------------------------------------------------------------
        cli                             ; sin IRQ hasta el modo largo
        lgdt    [gdt32_ptr]
        mov     eax, cr0
        or      eax, 1                  ; CR0.PE
        mov     cr0, eax
        ; NASM reubica la etiqueta a su direccion absoluta (0x8000 + offset),
        ; asi que aqui NO hay que sumar STAGE2_BASE.  El operando "dword"
        ; genera la forma 66h EA o32 s16 (salto lejano con offset de 32
        ; bits issuing en modo real), que es la que permite apuntar mas
        ; alla de los 64 KiB.
        jmp     far dword 0x08:protected_entry

; ==========================================================================
; Rutinas de modo real
; ==========================================================================

; --------------------------------------------------------------------------
; Diagnostico por el puerto serie COM1 (16550).  El arranque no tiene VGA
; utilizable en modo headless, asi que todo el progreso se escribe aqui.
; --------------------------------------------------------------------------
serial_init:
        push    ax
        push    dx
        mov     dx, COM1+1
        xor     al, al
        out     dx, al              ; interrupciones del UART desactivadas
        mov     dx, COM1+3
        mov     al, 0x80
        out     dx, al              ; DLAB = 1 (prepara el divisor)
        mov     dx, COM1+0
        mov     al, 0x01
        out     dx, al              ; divisor bajo = 1 (115200 baudios)
        mov     dx, COM1+1
        xor     al, al
        out     dx, al              ; divisor alto = 0
        mov     dx, COM1+3
        mov     al, 0x03
        out     dx, al              ; 8 bits, sin paridad, 1 stop
        mov     dx, COM1+2
        mov     al, 0xC7
        out     dx, al              ; FIFO activa y reiniciada
        mov     dx, COM1+4
        mov     al, 0x03
        out     dx, al              ; DTR | RTS | OUT2
        pop     dx
        pop     ax
        ret

; --------------------------------------------------------------------------
; serial_putc: escribe AL en el puerto serie.  Espera a que el registro de
; transmision este libre (LSR bit 5).
; --------------------------------------------------------------------------
serial_putc:
        push    ax
        push    dx
        mov     dx, COM1+5
.wait:  in      al, dx
        test    al, 0x20
        jz      .wait
        pop     dx
        pop     ax
        mov     dx, COM1
        out     dx, al
        ret

; --------------------------------------------------------------------------
; serial_puts: escribe la cadena terminada en 0 pointed to by SI.
; --------------------------------------------------------------------------
serial_puts:
        push    ax
        push    si
.loop:  lodsb
        test    al, al
        jz      .fin
        call    serial_putc
        jmp     .loop
.fin:   pop     si
        pop     ax
        ret

; --------------------------------------------------------------------------
; serial_puthex32: imprime EAX como 8 digitos hexadecimales.
; --------------------------------------------------------------------------
serial_puthex32:
        push    eax
        push    ebx
        push    ecx
        push    edx
        mov     ebx, eax                ; EBX = valor que se va rotando
        mov     ecx, 8
.loop:  rol     ebx, 4                  ; el nibble mas alto pasa a ser el bajo
        mov     eax, ebx
        and     al, 0x0F
        add     al, '0'
        cmp     al, '9'
        jbe     .emit
        add     al, 7
.emit:  call    serial_putc             ; AL = digito (serial_putc preserva EAX)
        loop    .loop
        pop     edx
        pop     ecx
        pop     ebx
        pop     eax
        ret

; --------------------------------------------------------------------------
; Construye las dos GDT en memoria.
;   GDT32: null, codigo 32 bits 0x08, datos 32 bits 0x10 (planas, 4 GiB)
;   GDT64: null, codigo 64 bits 0x08, datos 64 bits 0x10 (planas, 4 GiB)
; Cada descriptor ocupa 8 bytes:
;   limite(15:0) | base(15:0) | base(23:16) | acceso | flags | limite(19:16) | base(31:24)
; --------------------------------------------------------------------------
build_gdt:
        push    edi
        ; --- GDT de 32 bits ---------------------------------------------
        mov     edi, GDT32
        mov     dword [edi+0],  0x00000000       ; descriptor nulo
        mov     dword [edi+4],  0x00000000
        mov     dword [edi+8],  0x0000FFFF       ; limite bajo + base baja
        mov     dword [edi+12], 0x00CF9A00       ; base media, acceso, flags
        mov     dword [edi+16], 0x0000FFFF       ; codigo: no, datos:
        mov     dword [edi+20], 0x00CF9200       ; acceso 0x92, G=1, D=1
        ; --- GDT de 64 bits ---------------------------------------------
        mov     edi, GDT64
        mov     dword [edi+0],  0x00000000
        mov     dword [edi+4],  0x00000000
        mov     dword [edi+8],  0x0000FFFF
        mov     dword [edi+12], 0x00AF9A00       ; acceso 0x9A, G=1, L=1
        mov     dword [edi+16], 0x0000FFFF
        mov     dword [edi+20], 0x00CF9200
        pop     edi
        ret

; --------------------------------------------------------------------------
; Mapa de memoria con INT 15h AX=E820h.  Rellena hasta E820_MAX entradas
; en 0x9000 con {u64 base; u64 length; u32 type; u32 reservado} (24 bytes)
; y suma en mem_size los tramos utilizables (type == 1).
; La continuacion del firmware viaja en EDX:EBX.
; --------------------------------------------------------------------------
read_e820:
        push    edi
        push    ebx
        push    edx
        mov     dword [e820_count], 0
        mov     dword [mem_size], 0
        mov     dword [mem_size_hi], 0
        mov     dword [e820_ptr], E820_BUF
        xor     ebx, ebx                ; continuacion (32 bits bajos)
        xor     edx, edx                ; continuacion (32 bits altos)
.loop:
        mov     eax, 0xE820
        mov     ecx, E820_STRIDE        ; tamano de la entrada (24 bytes)
        mov     edi, [e820_ptr]         ; destino de la entrada
        int     0x15
        ; --- diagnostico: resultado de la llamada y contenido de la entrada --
        setc    al
        mov     [e820_cf], al
        mov     [e820_ax], ax
        mov     si, msg_eax
        call    serial_puts
        mov     ax, [e820_ax]
        call    serial_puthex32
        mov     si, msg_cf
        call    serial_puts
        mov     al, [e820_cf]
        call    serial_puthex32
        mov     si, msg_nl
        call    serial_puts
        mov     edi, [e820_ptr]
        mov     si, msg_base
        call    serial_puts
        mov     eax, [edi+0]
        call    serial_puthex32
        mov     si, msg_len
        call    serial_puts
        mov     eax, [edi+8]
        call    serial_puthex32
        mov     si, msg_type
        call    serial_puts
        mov     eax, [edi+16]
        call    serial_puthex32
        mov     si, msg_nl
        call    serial_puts
        cmp     byte [e820_cf], 0
        jne     .fin                    ; CF = 1 -> fin del mapa
        ; longitud invalida -> fin del mapa
        mov     eax, [edi+8]
        cmp     eax, 0x1000
        jb      .fin
        ; sumar la memoria utilizable
        mov     eax, [edi+16]           ; type
        cmp     eax, 1
        jne     .siguiente
        mov     eax, [edi+8]            ; longitud
        add     [mem_size], eax
        adc     [mem_size_hi], 0
.siguiente:
        inc     dword [e820_count]
        cmp     dword [e820_count], E820_MAX
        jae     .fin
        add     dword [e820_ptr], E820_STRIDE
        ; la continuacion 0 significa que no hay mas entradas
        test    ebx, ebx
        jnz     .loop
        test    edx, edx
        jnz     .loop
        jmp     .fin
.fin:
        ; sin entradas: 64 MiB por defecto
        cmp     dword [e820_count], 0
        jne     .ok
        mov     dword [mem_size], 0x04000000
.ok:
        pop     edx
        pop     ebx
        pop     edi
        ret

; --------------------------------------------------------------------------
; read_file_bytes: lee ECX bytes de un fichero del disco y los copia a la
; direccion lineal EDI.  La lectura se hace por sectores usando el buffer
; de staging (0x2000) como puente, asi que el destino puede estar
; desalineado respecto a los sectores.
;   Entrada: EAX = LBA base del fichero, ESI = offset dentro del fichero,
;            ECX = numero de bytes, EDI = destino
;   Salida:  CF = 1 si el BIOS fallo
; --------------------------------------------------------------------------
read_file_bytes:
        push    esi
        push    edi
        push    ebx
        push    ecx
        push    edx
        push    eax
        push    ebp
        mov     ebx, ecx                ; bytes que faltan
        mov     ebp, eax                ; LBA base
        mov     eax, esi                ; desplazamiento dentro del fichero
        shr     eax, 9                  ; sectores completos que hay que saltar
        add     ebp, eax                ; primer sector a leer
        and     esi, 511                ; posicion dentro de ese sector
.loop:
        test    ebx, ebx
        jz      .fin
        ; sectores necesarios, como mucho STAGE_SECT por lectura
        mov     eax, ebx
        add     eax, esi
        add     eax, SECTOR_SIZE-1
        shr     eax, 9
        cmp     eax, STAGE_SECT
        jbe     .n_ok
        mov     eax, STAGE_SECT
.n_ok:
        mov     ecx, eax                ; sectores a leer
        mov     eax, ebp                ; LBA
        call    read_sectors_staged
        jc      .err
        mov     edx, ecx                ; EDX = sectores leidos
        ; bytes copiables desde la posicion actual del buffer
        mov     eax, edx
        shl     eax, 9
        sub     eax, esi
        cmp     eax, ebx
        jbe     .copia
        mov     eax, ebx
.copia:
        push    eax                    ; [esp] = bytes copiados
        push    esi                    ; [esp+4] = posicion en el buffer
        mov     ecx, eax
        mov     esi, STAGE_BUF
        add     esi, [esp]              ; origen = buffer + posicion
        call    copy_bytes
        pop     esi                    ; recupera la posicion
        pop     eax                    ; bytes copiados
        add     edi, eax                ; destino += copiados
        add     esi, eax                ; posicion += copiados
        sub     ebx, eax                ; faltan menos bytes
        ; ¿se ha agotado el buffer?
        mov     eax, edx
        shl     eax, 9
        cmp     esi, eax
        jb      .loop
        sub     esi, eax                ; posicion = 0
        add     ebp, edx                ; siguiente sector
        jmp     .loop
.fin:
        pop     ebp
        pop     eax
        pop     edx
        pop     ecx
        pop     ebx
        pop     edi
        pop     esi
        clc
        ret
.err:
        pop     ebp
        pop     eax
        pop     edx
        pop     ecx
        pop     ebx
        pop     edi
        pop     esi
        stc
        ret

; --------------------------------------------------------------------------
; read_sectors_staged: lee ECX sectores del LBA EAX en el buffer de staging
; (0x2000) usando INT 13h AH=42h.
;   Entrada: EAX = LBA, ECX = sectores (como mucho 8)
;   Salida:  CF = 1 si el BIOS fallo
; El DAP usa el formato estandar (el de SeaBIOS, GRUB y el nucleo de
; Linux).  Ojo: el byte +0 es el TAMANO del paquete y tiene que valer 0x10;
; SeaBIOS lo comprueba y devuelve error si no coincide.
;   +0 tamano (0x10), +1 reservado, +2 sectores, +4 offset, +6 segmento,
;   +8 LBA (64 bits).
; --------------------------------------------------------------------------
read_sectors_staged:
        push    ebx
        push    edx
        push    esi
        push    eax
        push    ecx
        mov     byte [DAP+0], 0x10       ; tamano del DAP (16 bytes)
        mov     byte [DAP+1], 0          ; reservado
        mov     word [DAP+2], cx         ; numero de sectores
        mov     word [DAP+4], STAGE_BUF ; offset del buffer
        mov     word [DAP+6], 0          ; segmento del buffer
        mov     dword [DAP+8], eax      ; LBA (32 bits bajos)
        mov     dword [DAP+12], 0       ; LBA (32 bits altos)
        mov     si, DAP
        mov     dl, [drive]
        mov     ah, 0x42
        int     0x13
        jc      .fail
        clc
.fail:
        pop     ecx                    ; el BIOS puede alterar EAX/ECX
        pop     eax
        pop     esi
        pop     edx
        pop     ebx
        ret

; --------------------------------------------------------------------------
; copy_bytes: copia ECX bytes de ESI a EDI (direccionamiento de 32 bits; en
; modo real con DS=0 la direccion lineal es el propio desplazamiento).
; --------------------------------------------------------------------------
copy_bytes:
        push    esi
        push    edi
        push    ecx
        push    ebx
.c4:
        test    ecx, ecx
        jz      .fin
        cmp     ecx, 4
        jb      .byte
        mov     eax, [esi]
        mov     [edi], eax
        add     esi, 4
        add     edi, 4
        sub     ecx, 4
        jmp     .c4
.byte:
        mov     al, [esi]
        mov     [edi], al
        inc     esi
        inc     edi
        dec     ecx
        jmp     .c4               ; hay que volver a la comprobacion de ECX
.fin:
        pop     ebx
        pop     ecx
        pop     edi
        pop     esi
        ret

; --------------------------------------------------------------------------
; load_kernel: lee la cabecera ELF64 y copia cada segmento PT_LOAD a su
; direccion fisica (p_paddr).  Guarda e_entry en 0x6000.
; --------------------------------------------------------------------------
load_kernel:
        ; --- Cabecera ELF (primer sector del kernel) -> 0x0900 ------------
        mov     eax, [kernel_lba]
        xor     esi, esi
        mov     ecx, SECTOR_SIZE
        mov     edi, ELF_BUF
        call    read_file_bytes
        jc      .err

        ; --- Comprobaciones minimas ---------------------------------------
        cmp     byte [ELF_BUF+0], 0x7F
        jne     .err
        cmp     byte [ELF_BUF+1], 'E'
        jne     .err
        cmp     byte [ELF_BUF+2], 'L'
        jne     .err
        cmp     byte [ELF_BUF+3], 'F'
        jne     .err
        cmp     byte [ELF_BUF+4], 2          ; ELFCLASS64
        jne     .err
        cmp     byte [ELF_BUF+5], 1          ; ELFDATA2LSB
        jne     .err

        ; --- e_entry (offset 0x18) -> 0x6000 -------------------------------
        mov     eax, [ELF_BUF+0x18]
        mov     [ENTRY_SAVE], eax
        mov     eax, [ELF_BUF+0x1C]
        mov     [ENTRY_SAVE+4], eax

        ; --- e_phoff / e_phnum / e_phentsize ------------------------------
        mov     eax, [ELF_BUF+0x20]            ; e_phoff
        mov     [ph_off], eax
        movzx   eax, word [ELF_BUF+0x38]        ; e_phnum
        cmp     eax, 32
        jbe     .ok_phnum
        mov     eax, 32
.ok_phnum:
        mov     [ph_num], eax
        movzx   eax, word [ELF_BUF+0x36]        ; e_phentsize
        mov     [ph_ent], eax
        test    eax, eax
        jz      .fin
        mov     eax, [ph_off]
        mov     [ph_cur], eax                   ; offset del phdr actual
        mov     dword [ph_idx], 0

        ; --- Recorrido de las cabeceras de programa -----------------------
.phloop:
        mov     eax, [ph_idx]
        cmp     eax, [ph_num]
        jae     .fin
        ; Leer el phdr (56 bytes) en PHDR_BUF
        mov     eax, [kernel_lba]
        mov     esi, [ph_cur]
        mov     ecx, 56
        mov     edi, PHDR_BUF
        call    read_file_bytes
        jc      .err
        ; p_type == PT_LOAD ?
        cmp     dword [PHDR_BUF], 1
        jne     .siguiente
        ; p_offset (0x08), p_paddr (0x18), p_filesz (0x20)
        mov     esi, [PHDR_BUF+0x08]
        mov     edi, [PHDR_BUF+0x18]
        mov     ecx, [PHDR_BUF+0x20]
        test    ecx, ecx
        jz      .siguiente
        mov     eax, [kernel_lba]
        call    read_file_bytes
        jc      .err
.siguiente:
        inc     dword [ph_idx]
        mov     eax, [ph_ent]
        add     dword [ph_cur], eax
        jmp     .phloop
.fin:
        clc
        ret
.err:
        stc
        ret

; --------------------------------------------------------------------------
; fatal_real: error irrecuperable en modo real.  Escribe el mensaje (terminado
; en '$') en la VGA con INT 10h y tambien por el puerto serie, que es lo
; unico visible cuando se arranca sin ventana.
; --------------------------------------------------------------------------
fatal_real:
        push    ax
        push    si
        ; Copia el mensaje a una cadena terminada en 0 para poder escribirlo
        ; por el puerto serie (serial_puts espera un final 0, no '$').
        mov     di, msg_tmp
        mov     cx, 128
.copy:  lodsb
        mov     [di], al
        inc     di
        cmp     al, '$'
        je      .copy_end
        cmp     al, 0
        je      .copy_end
        loop    .copy
.copy_end:
        mov     byte [di], 0
        mov     si, msg_tmp
        call    serial_puts
        pop     si
        pop     ax
        mov     ah, 0x09
        int     0x10
        cli
.hang:  hlt
        jmp     .hang

; ==========================================================================
; Modo protegido de 32 bits (se entra con un salto lejano a 0x08)
; ==========================================================================
        [bits 32]

; --- Diagnostico por el puerto serie en 32 bits ---------------------------
serial_putc32:
        push    eax
        push    edx
        mov     dx, COM1+5
.wait:  in      al, dx
        test    al, 0x20
        jz      .wait
        pop     edx
        pop     eax
        mov     dx, COM1
        out     dx, al
        ret

serial_puts32:                     ; ESI = cadena terminada en 0
        push    eax
        push    esi
.loop:  lodsb
        test    al, al
        jz      .fin
        call    serial_putc32
        jmp     .loop
.fin:   pop     esi
        pop     eax
        ret

protected_entry:
        cli
        mov     ax, 0x10                 ; selector de datos del GDT
        mov     ds, ax
        mov     es, ax
        mov     ss, ax                  ; SS valido antes de usar la pila
        mov     esp, STACK_TOP           ; pila de 32 bits

        mov     esi, msg_pm
        call    serial_puts32

        ; ------------------------------------------------------------------
        ; Tablas de pagina
        ;   PML4  0x70000 : [0] -> PDPT_low, [511] -> PDPT_high
        ;   PDPT  0x71000 : [0..3] -> PD_low[0..3]  (identidad 0..4 GiB)
        ;         0x72000 : [510] -> PD_high        (kernel 0xFFFFFFFF80200000)
        ;   PD    0x73000..0x76000 : 4 PD con paginas de 2 MiB
        ;         0x77000 : PD_high
        ;   PT    0x78000 : paginas de 4 KiB del inicio del kernel
        ; ------------------------------------------------------------------
        mov     edi, PML4_ADDR
        mov     ecx, (0x80000 - PML4_ADDR) / 4
        xor     eax, eax
        rep     stosd                   ; se pone a cero toda la zona

        ; --- PML4 ----------------------------------------------------------
        mov     dword [PML4_ADDR+0*8], PDPT_LOW  | PTE_P | PTE_RW
        mov     dword [PML4_ADDR+511*8], PDPT_HIGH | PTE_P | PTE_RW

        ; --- PDPT_low: 4 GiB identidad (PD_low[0..3]) ----------------------
        mov     dword [PDPT_LOW+0*8], PD_LOW0+0x0000 | PTE_P | PTE_RW
        mov     dword [PDPT_LOW+1*8], PD_LOW0+0x1000 | PTE_P | PTE_RW
        mov     dword [PDPT_LOW+2*8], PD_LOW0+0x2000 | PTE_P | PTE_RW
        mov     dword [PDPT_LOW+3*8], PD_LOW0+0x3000 | PTE_P | PTE_RW

        ; --- PD_low: paginas de 2 MiB (PS=1) identidad 0..4 GiB ------------
        mov     edi, PD_LOW0
        mov     eax, 0                  ; base fisica de la PD actual
        mov     ebp, 4                  ; numero de PDs
.ploop:
        xor     ebx, 0
        xor     edx, 0
.pinner:
        mov     ecx, ebx
        shl     ecx, 21                 ; j * 2 MiB
        add     ecx, eax
        or      ecx, PTE_P | PTE_RW | PTE_PS
        mov     [edi+edx*8], ecx
        inc     edx
        inc     ebx
        cmp     edx, 512
        jne     .pinner
        add     edi, 0x1000
        add     eax, 0x40000000         ; siguiente PD -> siguiente GiB
        dec     ebp
        jnz     .ploop

        ; --- PDPT_high[510] -> PD_high ------------------------------------
        ;     El indice 510 es el correcto para 0xFFFFFFFF80200000
        ;     (PDPT[0] corresponde a 0xFFFFFF8000000000).
        mov     dword [PDPT_HIGH+510*8], PD_HIGH | PTE_P | PTE_RW

        ; --- PD_high[1] -> PT (4 KiB) para 0xFFFFFFFF80200000 --------------
        mov     dword [PD_HIGH+1*8], PT_KERNEL | PTE_P | PTE_RW

        ; --- PD_high[2..16] -> paginas de 2 MiB hasta 32 MiB --------------
        mov     edi, PD_HIGH + 2*8
        mov     eax, 0x400000           ; fisica de PD_high[2]
        mov     ebp, 15
.philoop:
        mov     edx, eax
        or      edx, PTE_P | PTE_RW | PTE_PS
        mov     [edi], edx
        add     edi, 8
        add     eax, 0x200000
        dec     ebp
        jnz     .philoop

        ; --- PT del kernel: 512 paginas de 4 KiB --------------------------
        mov     edi, PT_KERNEL
        xor     eax, eax
        xor     edx, edx
.ptloop:
        mov     ecx, eax
        shl     ecx, 12                 ; j * 4 KiB
        add     ecx, 0x200000           ; + base fisica del kernel
        or      ecx, PTE_P | PTE_RW
        mov     [edi+edx*8], ecx
        inc     edx
        cmp     edx, 512
        jne     .ptloop

        ; ------------------------------------------------------------------
        ; Estructura AxBootInfo (72 bytes) en bootinfo_phys (0x1000)
        ;   magic, kernel_phys, kernel_virt, initrd_phys, initrd_size,
        ;   mem_size, e820_entries, e820_ptr, cmdline
        ; ------------------------------------------------------------------
        mov     edi, [bootinfo_phys]
        mov     ecx, 18                 ; 72 bytes = 18 dwords
        xor     eax, eax
        rep     stosd                   ; se limpia el bloque (72 bytes)
        mov     edi, [bootinfo_phys]
        mov     dword [edi+0],  0x4F54494E       ; "AXBOOTIN"
        mov     dword [edi+4],  0x4158424F
        mov     eax, [kernel_phys]
        mov     [edi+8],  eax
        mov     eax, [HDR_BUF+OFF_KVIRT]         ; kernel_virt (64 bits)
        mov     [edi+16], eax
        mov     eax, [HDR_BUF+OFF_KVIRT+4]
        mov     [edi+20], eax
        mov     eax, [initrd_phys]
        mov     [edi+24], eax
        mov     eax, [initrd_size]
        mov     [edi+32], eax
        mov     eax, [mem_size]
        mov     [edi+40], eax
        mov     eax, [mem_size_hi]
        mov     [edi+44], eax
        mov     eax, [e820_count]
        mov     [edi+48], eax
        mov     eax, [e820_phys]
        mov     [edi+56], eax
        mov     dword [edi+64], 0                ; cmdline = NULL
        mov     eax, [bootinfo_phys]
        mov     [BOOTINFO_PTR], eax
        mov     dword [BOOTINFO_PTR+4], 0

        ; ------------------------------------------------------------------
        ; Comprobacion de soporte de modo largo (CPUID)
        ; ------------------------------------------------------------------
        mov     eax, 0x80000000
        cpuid
        cmp     eax, 0x80000001
        jb      .no_lm
        mov     eax, 0x80000001
        cpuid
        test    edx, 1 << 29            ; bit LM
        jz      .no_lm

        ; --- CR4.PAE -------------------------------------------------------
        mov     eax, cr4
        or      eax, 1 << 5
        mov     cr4, eax

        ; --- EFER.LME (MSR 0xC0000080) -------------------------------------
        mov     ecx, 0xC0000080
        rdmsr
        or      eax, 1 << 8             ; LME
        wrmsr

        ; --- CR3 = PML4 ----------------------------------------------------
        mov     eax, PML4_ADDR
        mov     cr3, eax

        ; --- CR0.PG -------------------------------------------------------
        mov     eax, cr0
        or      eax, 1 << 31
        mov     cr0, eax

        ; --- GDT de 64 bits y salto a modo largo ---------------------------
        lgdt    [gdt64_ptr]
        jmp     far dword 0x08:long_entry
.no_lm:
        mov     esi, msg_nolm
        call    fatal_pm

; --------------------------------------------------------------------------
; fatal_pm: error irrecuperable en modo protegido.  Escribe el mensaje en el
; buffer de texto de la VGA y por el puerto serie.
; --------------------------------------------------------------------------
fatal_pm:
        call    serial_puts32
        mov     edi, 0xB8000
.loop:
        movzx   eax, byte [esi]
        test    al, al
        jz      .fin
        cmp     al, '$'
        je      .fin
        mov     [edi], al
        mov     byte [edi+1], 0x0F
        add     edi, 2
        inc     esi
        jmp     .loop
.fin:
        cli
.hang:  hlt
        jmp     .hang

; ==========================================================================
; Modo largo (64 bits)
; ==========================================================================
        [bits 64]
        default abs                        ; direcciones absolutas (0x6000...)

long_entry:
        mov     ax, 0x10
        mov     ds, ax
        mov     es, ax
        mov     ss, ax
        mov     rsp, 0x90000            ; pila provisional del kernel
        xor     rbp, rbp
        xor     rbx, rbx
        ; Argumentos de arranque: RDI = direccion fisica de AxBootInfo
        mov     rdi, [BOOTINFO_PTR]
        xor     rsi, rsi
        xor     rdx, rdx
        ; Entrada del kernel (e_entry, virtual, ya guardado en 0x6000)
        mov     rax, [ENTRY_SAVE]
        sti
        jmp     rax

; ==========================================================================
; Las rutinas de error (fatal_real / fatal_pm) estan al final de sus
; secciones de bits correspondientes (16 y 32).
; ==========================================================================

; ==========================================================================
; Datos
; ==========================================================================
drive:          db 0
kernel_lba:     dd 0
kernel_sectors: dd 0
initrd_lba:     dd 0
initrd_sectors: dd 0
kernel_phys:    dd 0
initrd_phys:    dd 0
bootinfo_phys:  dd 0
e820_phys:      dd 0
initrd_size:    dd 0
mem_size:       dd 0
mem_size_hi:    dd 0
e820_count:     dd 0
e820_ptr:       dd E820_BUF
ph_off:         dd 0
ph_num:         dd 0
ph_ent:         dd 0
ph_cur:         dd 0
ph_idx:         dd 0

; Puntero de 6 bytes para LGDT: limite (16 bits) + base (32 bits)
gdt32_ptr:      dw GDT32+23
                dd GDT32
gdt64_ptr:      dw GDT64+23
                dd GDT64

msg_header:     db "AXOS: cabecera invalida", '$'
msg_initrd_err: db "AXOS: initrd ilegible", '$'
msg_nolm:       db "AXOS: sin modo largo", '$'

; --- Mensajes de diagnostico por el puerto serie --------------------------
msg_banner:     db "AXOS stage2", 13, 10, 0
msg_a20:        db "a20 ok", 13, 10, 0
msg_gdt:        db "gdt ok", 13, 10, 0
msg_e820:       db "e820 mem=", 0
msg_nl:         db 13, 10, 0
msg_hdr:        db "hdr ok lba=", 0
msg_space:      db " sect=", 0
msg_kern:       db "kernel ok", 13, 10, 0
msg_initrd:     db "initrd ok", 13, 10, 0
msg_pm:         db "pm", 13, 10, 0
msg_eax:        db "e820 ax=", 0
msg_cf:         db " cf=", 0
msg_base:       db " base=", 0
msg_len:        db " len=", 0
msg_type:       db " type=", 0
msg_tmp:        resb 128
