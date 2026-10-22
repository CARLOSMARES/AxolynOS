; ==========================================================================
; AxolynOS - Bootloader stage 1 (MBR, 512 bytes exactos)
; ==========================================================================
; Formato de disco propietario "AXDSK":
;   LBA 0  : este sector (stage1), firma 0xAA55 en el offset 0x1FE
;   LBA 1  : stage2 (stage2_sectors sectores), se carga en 0x8000
;   LBA 64 : cabecera de disco "AXDSK001" (1 sector)
;   LBA +  : kernel.elf (kernel_sectors sectores)
;   LBA +  : initrd.axfs (initrd_sectors sectores)
;
; Responsabilidades de este sector:
;   1. Preparar segmentos, pila e interrupciones.
;   2. Instalar un INT 13h propio que traduce CHS -> LBA (discos grandes).
;   3. Leer la geometria del disco (INT 13h AH=08h).
;   4. Leer la cabecera AXDSK (LBA 64) para conocer stage2_sectors.
;   5. Cargar stage2 (LBA 1) en 0x8000 con INT 13h AH=42h (LBA).
;   6. Saltar a stage2 pasandole geometria y unidad en los registros.
;
; Contrato de registros entregado a stage2:
;   AL = numero de sectores de stage2 cargados
;   BX = 0x8000 (direccion de stage2)
;   CH = sectores por pista
;   CL = numero maximo de cabezas
;   DH = numero maximo de cilindros (mod 256)
;   DL = unidad de arranque
; ==========================================================================

        [bits 16]
        [org 0x7C00]

STAGE2_ADDR   equ 0x8000        ; destino de la carga de stage2
HEADER_LBA    equ 64           ; LBA de la cabecera AXDSK

; --------------------------------------------------------------------------
; Punto de entrada. El BIOS nos carga en 0000:7C00 con DL = unidad de
; arranque y CS:IP apuntando a 0x7C00.
; --------------------------------------------------------------------------
start:
        cli
        xor     ax, ax
        mov     ds, ax                  ; DS = 0 (todo el codigo vive en el
        mov     es, ax                  ;        segmento 0)
        mov     ss, ax
        mov     sp, 0x7C00              ; la pila crece hacia abajo
        sti
        cld
        mov     [boot_drive], dl         ; guardamos la unidad de arranque

        ; ------------------------------------------------------------------
        ; Instalar nuestro INT 13h (guardamos antes el vector original).
        ; ------------------------------------------------------------------
        cli
        mov     ax, [0x13*4]
        mov     [orig13], ax
        mov     ax, [0x13*4+2]
        mov     [orig13+2], ax
        mov     word [0x13*4], int13
        mov     word [0x13*4+2], 0
        sti

        ; ------------------------------------------------------------------
        ; Geometria del disco: INT 13h AH=08h
        ;   CH = cilindros (8 bits bajos), CL = sectores/pista (bits 0-5)
        ;   DH = cabezas maximas, DL = numero de unidades
        ; ------------------------------------------------------------------
        mov     ah, 0x08
        mov     dl, [boot_drive]
        int     0x13
        jc      disk_error
        mov     [max_cyl], ch
        and     cl, 0x3F
        mov     [spt], cl
        mov     [max_head], dh

        ; ------------------------------------------------------------------
        ; Leer la cabecera AXDSK (LBA 64) en 0x8000. Necesitamos
        ; stage2_sectors para saber cuanto cargar.
        ; ------------------------------------------------------------------
        mov     word [dap+2], 1          ; 1 sector
        mov     word [dap+4], STAGE2_ADDR ; offset destino
        mov     word [dap+6], 0          ; segmento destino
        mov     dword [dap+8], HEADER_LBA
        mov     dword [dap+12], 0
        mov     si, dap
        mov     bx, dap                  ; DS:SI y ES:BX -> DAP
        mov     ah, 0x42
        mov     dl, [boot_drive]
        int     0x13
        jc      disk_error

        ; Comprueba la firma "AXDSK001"
        cmp     dword [STAGE2_ADDR+0x00], 'AXDS'
        jne     disk_error
        cmp     dword [STAGE2_ADDR+0x04], 'K001'
        jne     disk_error

        ; ------------------------------------------------------------------
        ; Cargar stage2 (LBA 1, stage2_sectors sectores) en 0x8000.
        ; El espacio reservado es LBA 1..63 -> como maximo 63 sectores.
        ; ------------------------------------------------------------------
        mov     ax, [STAGE2_ADDR+0x0C]   ; stage2_sectors
        test    ax, ax
        jz      disk_error
        cmp     ax, 63
        jbe     .sectors_ok
        mov     ax, 63                   ; truncamos por seguridad
.sectors_ok:
        mov     [stage2_cnt], ax

        mov     word [dap+2], ax          ; sectores a leer
        mov     word [dap+4], STAGE2_ADDR
        mov     word [dap+6], 0
        mov     dword [dap+8], 1          ; LBA 1
        mov     dword [dap+12], 0
        mov     si, dap
        mov     bx, dap
        mov     ah, 0x42
        mov     dl, [boot_drive]
        int     0x13
        jc      disk_error

        ; ------------------------------------------------------------------
        ; Salto a stage2 con la geometria en los registros.
        ; ------------------------------------------------------------------
        mov     bx, STAGE2_ADDR
        mov     al, [stage2_cnt]         ; sectores cargados
        mov     ch, [spt]                ; sectores por pista
        mov     cl, [max_head]           ; cabezas maximas
        mov     dh, [max_cyl]            ; cilindros maximos
        mov     dl, [boot_drive]         ; unidad de arranque
        jmp     0x0000:STAGE2_ADDR

; --------------------------------------------------------------------------
; Error de disco: mensaje por pantalla y parada definitiva.
; --------------------------------------------------------------------------
disk_error:
        mov     si, msg_error
        mov     ah, 0x09
        int     0x10
        cli
.hang:  hlt
        jmp     .hang

; ==========================================================================
; INT 13h propio.
;   AH=00h : reset              -> reenviado al BIOS
;   AH=08h : geometria          -> reenviado al BIOS
;   AH=02h : lectura CHS        -> traducida a LBA y hecha con AH=42h
;   AH=42h : lectura extendida  -> reenviada al BIOS
; ==========================================================================
int13:
        cmp     ah, 0x02
        je      i13_chs
        cmp     ah, 0x42
        je      i13_bios
        cmp     ah, 0x00
        je      i13_bios
        cmp     ah, 0x08
        je      i13_bios
        stc
        retf    2

; --- Reenvio al INT 13h original del BIOS --------------------------------
; Entrada como interrupcion: el frame del BIOS consume 6 bytes igual que
; empujo la CPU con "int", asi que "retf 2" deja la pila equilibrada.
i13_bios:
        call    do_bios
        retf    2

; --- Reenvio interno (usado tambien por la traduccion CHS -> LBA) --------
; Para no depender de la convencion de retorno del BIOS, restauramos el
; vector original, llamamos con "int 0x13" y reinstallamos el nuestro.
;
; OJO: hay que conservar AH (el numero de funcion) durante el intercambio de
; vectores.  Si se pierde, el BIOS recibe otra funcion y devuelve error.
; Por eso AX se apila al entrar y se restaura con las interrupciones
; desactivadas, justo antes de la llamada.
do_bios:
        push    ax
        cli
        mov     ax, [orig13]
        mov     [0x13*4], ax
        mov     ax, [orig13+2]
        mov     [0x13*4+2], ax
        pop     ax                     ; AH = funcion pedida por el llamante
        sti
        int     0x13
        push    ax                      ; salva el estado devuelto (AH)
        cli
        mov     word [0x13*4], int13
        mov     word [0x13*4+2], 0
        pop     ax
        sti
        ret

; --- Lectura CHS -> se traduce a LBA y se hace con AH=42h -----------------
i13_chs:
        pusha
        push    ds
        push    es
        mov     [chs_cnt], al           ; AL = numero de sectores
        mov     [chs_buf], bx           ; ES:BX = buffer destino
        mov     ax, es
        mov     [chs_seg], ax

        movzx   ebx, cl                 ; sector - 1
        and     ebx, 0x3F
        dec     ebx

        movzx   ecx, ch                 ; cilindro (8 bits bajos)
        movzx   eax, cl                 ; cilindro (2 bits altos)
        shr     eax, 6
        and     eax, 3
        shl     eax, 8
        add     ecx, eax

        movzx   eax, byte [max_head]    ; LBA = (C*(cabezas+1) + cabeza)*spt
        inc     eax
        imul    eax, ecx
        movzx   ecx, byte [max_head]
        add     eax, ecx
        movzx   ecx, byte [spt]
        imul    eax, ecx
        add     eax, ebx

        mov     ax, [chs_cnt]
        mov     word [dap+2], ax        ; sectores
        mov     ax, [chs_buf]
        mov     word [dap+4], ax        ; offset destino
        mov     ax, [chs_seg]
        mov     word [dap+6], ax        ; segmento destino
        mov     dword [dap+8], eax       ; LBA (dword bajo)
        mov     dword [dap+12], 0       ; LBA (dword alto)

        xor     ax, ax
        mov     ds, ax                  ; DS:SI -> DAP
        mov     es, ax
        mov     si, dap
        mov     bx, dap
        mov     ah, 0x42
        call    do_bios

        pop     es
        pop     ds
        popa
        retf    2

; ==========================================================================
; Datos
; ==========================================================================
boot_drive: db 0
spt:        db 0                     ; sectores por pista
max_head:   db 0                     ; cabezas maximas
max_cyl:    db 0                     ; cilindros maximos (8 bits bajos)
stage2_cnt: dw 0                     ; sectores de stage2 cargados
chs_cnt:    dw 0                     ; AL en una lectura CHS
chs_buf:    dw 0                     ; BX en una lectura CHS
chs_seg:    dw 0                     ; ES en una lectura CHS
orig13:     dw 0, 0                  ; vector original de INT 13h

msg_error:  db "AXOS: error de disco", 0

; Disk Address Packet: tamano, reservado, sectores, offset, segmento, LBA
dap:        db 0x10, 0
            dw 1
            dw 0
            dw 0
            dq 0

; --- Relleno hasta 510 y firma MBR ----------------------------------------
        times 510-($-$$) db 0
        dw 0xAA55
