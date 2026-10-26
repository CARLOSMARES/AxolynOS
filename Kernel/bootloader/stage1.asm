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
;   2. Leer la geometria del disco (INT 13h AH=08h).
;   3. Detectar si el BIOS soporta lecturas LBA (INT 13h AH=41h).
;   4. Leer la cabecera AXDSK (LBA 64) para conocer stage2_sectors.
;   5. Cargar stage2 (LBA 1) en 0x8000.
;   6. Saltar a stage2 pasandole geometria, unidad y modo de lectura.
;
; IMPORTANTE: la emulacion de disco duro de El Torito (CD) solo admite
; comandos antiguos de INT 13h (AH <= 0x16); en ese caso AH=42h falla y
; hay que leer por CHS (AH=02h).  Para que el BIOS de la emulacion pueda
; calcular la geometria (SeaBIOS lee el CHS final de la particion 1 del
; MBR), la tabla de particiones lleva una entrada valida que cubre la
; imagen completa.
;
; Contrato de registros entregado a stage2:
;   AL = numero de sectores de stage2 cargados
;   BX = 0x8000 (direccion de stage2)
;   SI = 1 si las lecturas usan LBA, 0 si usan CHS
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
        test    cl, cl
        jz      disk_error              ; sin geometria no se puede leer CHS

        ; ------------------------------------------------------------------
        ; Detectar extensiones LBA (INT 13h AH=41h).
        ;   Salida: CF=0 y BX=0xAA55 si estan disponibles.
        ; En la emulacion de CD de El Torito esta llamada falla: la unidad
        ; emulada solo acepta AH <= 0x16.
        ; ------------------------------------------------------------------
        mov     byte [use_lba], 0
        mov     ah, 0x41
        mov     bx, 0x55AA
        mov     dl, [boot_drive]
        int     0x13
        jc      .sin_lba
        cmp     bx, 0xAA55
        jne     .sin_lba
        mov     byte [use_lba], 1
.sin_lba:

        ; ------------------------------------------------------------------
        ; Leer la cabecera AXDSK (LBA 64) en 0x8000. Necesitamos
        ; stage2_sectors para saber cuanto cargar.
        ; ------------------------------------------------------------------
        mov     ax, HEADER_LBA
        mov     bx, STAGE2_ADDR
        call    read_sector
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

        mov     ax, 1                    ; LBA del primer sector de stage2
        mov     di, STAGE2_ADDR          ; destino
        mov     si, [stage2_cnt]
.load2:
        mov     bx, di
        call    read_sector
        jc      disk_error
        inc     ax
        add     di, 512
        dec     si
        jnz     .load2

        ; ------------------------------------------------------------------
        ; Salto a stage2 con la geometria, la unidad y el modo de lectura.
        ; ------------------------------------------------------------------
        mov     bx, STAGE2_ADDR
        mov     al, [stage2_cnt]         ; sectores cargados
        movzx   si, byte [use_lba]       ; 1 = LBA, 0 = CHS
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

; --------------------------------------------------------------------------
; read_sector: lee un sector (512 bytes) y lo deja en ES:BX.
;   Entrada: AX = LBA (16 bits), ES:BX = destino
;   Salida:  CF = 1 si el BIOS devolvio error
; Usa AH=42h (LBA) si esta disponible; si falla o no existe, traduce el
; LBA a CHS y usa AH=02h.  Un fallo de AH=42h desactiva LBA para el resto
; del arranque.
; --------------------------------------------------------------------------
read_sector:
        pusha
        push    es
        mov     [dap+4], bx              ; offset destino
        mov     [dap+6], es              ; segmento destino
        mov     [rs_lba], ax

        cmp     byte [use_lba], 0
        je      .chs

        ; --- Intento por LBA (AH=42h) ---
        mov     word [dap+2], 1
        movzx   eax, word [rs_lba]
        mov     [dap+8], eax
        mov     dword [dap+12], 0
        mov     si, dap
        mov     ah, 0x42
        mov     dl, [boot_drive]
        int     0x13
        jnc     .done
        mov     byte [use_lba], 0        ; el BIOS no puede con LBA

        ; --- Lectura por CHS (AH=02h) ---
.chs:
        movzx   eax, word [rs_lba]
        xor     edx, edx
        movzx   ecx, byte [max_head]
        inc     ecx
        movzx   ebx, byte [spt]
        imul    ebx, ecx                 ; sectores por cilindro
        div     ebx                      ; EAX = cilindro, EDX = resto
        mov     [rs_cyl], eax
        mov     eax, edx
        xor     edx, edx
        movzx   ebx, byte [spt]
        div     ebx                      ; EAX = cabeza, EDX = sector-1
        mov     [rs_head], al

        mov     ebx, [rs_cyl]
        mov     ch, bl                   ; cilindro (8 bits bajos)
        shr     ebx, 8
        and     bl, 3                    ; cilindro (2 bits altos)
        shl     bl, 6
        mov     cl, dl
        inc     cl                       ; sector (1..spt)
        or      cl, bl                   ; + bits altos del cilindro
        mov     dh, [rs_head]
        mov     dl, [boot_drive]
        mov     bx, [dap+4]
        mov     ax, 0x0201               ; AH=02h (leer), AL=1 sector
        int     0x13
.done:
        pop     es
        popa
        ret

; ==========================================================================
; Datos
; ==========================================================================
boot_drive: db 0
spt:        db 0                     ; sectores por pista
max_head:   db 0                     ; cabezas maximas
max_cyl:    db 0                     ; cilindros maximos (8 bits bajos)
use_lba:    db 0                     ; 1 = usar AH=42h, 0 = usar CHS
stage2_cnt: dw 0                     ; sectores de stage2 cargados
rs_lba:     dw 0                     ; LBA de la lectura en curso
rs_cyl:     dd 0                     ; cilindro calculado
rs_head:    db 0                     ; cabeza calculada

msg_error:  db "AXOS ERR", 0

; Disk Address Packet: tamano, reservado, sectores, offset, segmento, LBA
dap:        db 0x10, 0
            dw 1
            dw 0
            dw 0
            dq 0

; --------------------------------------------------------------------------
; Tabla de particiones (necesaria para la emulacion de CD de El Torito).
; SeaBIOS toma la geometria del disco emulado del CHS final de la particion
; 1; con la tabla a cero el disco emulado no tiene geometria y las lecturas
; CHS fallan.  La entrada cubre de sobra la imagen (hasta 15/15/63).
; --------------------------------------------------------------------------
        times 446-($-$$) db 0
        db 0x00                          ; no activa
        db 0x00, 0x01, 0x00              ; CHS inicio: cil 0, cab 0, sec 1
        db 0x83                          ; tipo Linux
        db 0x0F, 0x3F, 0x0F              ; CHS fin: cil 15, cab 15, sec 63
        dd 1                             ; LBA de inicio
        dd 15119                         ; numero de sectores
        times 48 db 0                    ; entradas 2..4 vacias

; --- Firma MBR ------------------------------------------------------------
        dw 0xAA55
