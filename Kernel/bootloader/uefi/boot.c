/*
 * AxolynOS - Cargador UEFI (BOOTX64.EFI).
 *
 * Responsabilidades:
 *   1. Abrir el volumen EFI y cargar \EFI\AXOLYNOS\axolyn.elf junto con el
 *      initrd (opcional).
 *   2. Construir un AxBootInfo en memoria baja con el mismo formato que el del
 *      bootloader BIOS, incluida una tabla E820 sintetica obtenida del mapa
 *      de memoria UEFI (el kernel solo entiende E820).
 *   3. Salir de Boot Services y, ya sin firmware, copiar los segmentos
 *      PT_LOAD del kernel a su direccion fisica de enlazado (el kernel NO es
 *      reubicable) y publicar su ventana virtual en las tablas de pagina.
 *   4. Saltar al punto de entrada del kernel con el puntero a AxBootInfo en
 *      RDI (ABI SysV, no MS-x64).
 *
 * IMPORTANTE: el rango fisico del kernel (0x200000..0xA8C000 en el layout
 * tipico) puede coincidir con regiones que el firmware reserva como ACPI NVS
 * o BootServicesData.  Por eso no se reserva con AllocateAddress: la copia se
 * hace DESPUES de ExitBootServices, cuando toda esa RAM es del sistema.  La
 * tabla E820 y el propio AxBootInfo se colocan por debajo de los 64 MiB (lo
 * unico mapeado por las tablas temporales de boot.S) y fuera del rango del
 * kernel.
 */

#include <efi.h>
#include <efilib.h>

#include "elf.h"
#include "bootinfo.h"

#define KERNEL_PATH    L"\\EFI\\AXOLYNOS\\axolyn.elf"
#define INITRD_PATH    L"\\EFI\\AXOLYNOS\\initrd.axfs"

#define LOWMEM_LIMIT     0x3F00000ULL   /* por debajo de los 64 MiB de boot.S */
#define E820_MAX         128

/* Punto de entrada del kernel: convencion SysV (primer argumento en RDI). */
typedef void (*KernelEntry)(uint64_t bootinfo) __attribute__((sysv_abi));

/* ------------------------------------------------------------------ */
/* Utilidades                                                          */
/* ------------------------------------------------------------------ */
static void ax_zero(void* dst, UINTN len)
{
    SetMem(dst, len, 0);
}

/*
 * Copia y relleno propios.  Se usan DESPUES de ExitBootServices, cuando ya no
 * se puede llamar a ningun servicio del firmware (ni siquiera a CopyMem/SetMem
 * de la libreria si esta decidiera delegar en Boot Services).
 */
static void ax_copy_bytes(void* dst, const void* src, UINTN count)
{
    volatile UINT8* d = (volatile UINT8*)dst;
    const UINT8* s = (const UINT8*)src;
    for (UINTN i = 0; i < count; ++i) {
        d[i] = s[i];
    }
}

static void ax_fill_bytes(void* dst, UINT8 value, UINTN count)
{
    volatile UINT8* d = (volatile UINT8*)dst;
    for (UINTN i = 0; i < count; ++i) {
        d[i] = value;
    }
}

/*
 * Reserva "pages" paginas por debajo de LOWMEM_LIMIT evitando el rango
 * [avoid_lo, avoid_hi) que ocupa el kernel.  AllocateMaxAddress entrega
 * siempre la direccion libre mas alta, asi que en la practica cae por encima
 * del kernel; si no fuera asi se libera y se reintenta por debajo.
 */
static EFI_STATUS ax_alloc_low_outside(UINTN pages, UINT64 avoid_lo,
                                       UINT64 avoid_hi, void** out)
{
    UINT64 max = LOWMEM_LIMIT;

    for (int intento = 0; intento < 4; ++intento) {
        EFI_PHYSICAL_ADDRESS addr = max;
        EFI_STATUS status = uefi_call_wrapper(BS->AllocatePages, 4,
                                              AllocateMaxAddress,
                                              EfiLoaderData, pages, &addr);
        if (EFI_ERROR(status)) {
            return status;
        }

        UINT64 lo = (UINT64)addr;
        UINT64 hi = lo + (UINT64)pages * 0x1000ULL;
        if (hi <= avoid_lo || lo >= avoid_hi) {
            *out = (void*)(UINTN)addr;
            return EFI_SUCCESS;
        }

        uefi_call_wrapper(BS->FreePages, 2, addr, pages);
        if (max <= avoid_lo || avoid_lo == 0) {
            break;
        }
        max = avoid_lo;         /* reintentar por debajo del kernel */
    }

    return EFI_NOT_FOUND;
}

/* Lee un fichero entero del volumen EFI. */
/*
 * Mapea en las tablas de pagina ACTUALES (las del firmware) la ventana
 * virtual del kernel:
 *
 *   0xFFFFFFFF80200000..0xFFFFFFFF821FFFFF  ->  0x0000000000200000..
 *
 * El kernel esta enlazado en la mitad alta y su punto de entrada es
 * 0xFFFFFFFF80200000, pero el firmware solo mapea RAM en identidad (mitad
 * baja).  En vez de construir una PML4 nueva (que dejaria fuera al propio
 * cargador), se injerta la ventana en las tablas existentes: PML4[511] ->
 * PDPT_high, PDPT_high[510] -> PD_high y PD_high[1..16] como paginas de 2 MiB.
 * Todo lo demas del firmware sigue mapeado, de modo que el codigo del
 * cargador y la pila siguen siendo accesibles hasta el salto.
 *
 * Se ejecuta DESPUES de ExitBootServices: no llama a ningun servicio y las
 * paginas de las tablas ya estan reservadas (scratch).
 */
static void ax_map_kernel_window(void* scratch)
{
    UINT64* pml4_nueva = (UINT64*)scratch;
    UINT64* pdpt_nueva = (UINT64*)((UINT8*)scratch + 0x1000);
    UINT64* pd_nueva   = (UINT64*)((UINT8*)scratch + 0x2000);

    UINT64 cr3 = 0;
    __asm__ volatile("mov %%cr3, %0" : "=r"(cr3));
    UINT64* pml4_vieja = (UINT64*)(UINTN)(cr3 & ~0xFFFULL);

    /*
     * Las tablas del firmware estan mapeadas como solo lectura (CR0.WP=1), asi
     * que no se pueden modificar in situ.  Se clona la PML4 completa en una
     * pagina propia (conserva el mapa de identidad y cualquier otro mapeo) y
     * solo se sustituye la mitad alta por la ventana del kernel.
     */
    for (int i = 0; i < 512; ++i) {
        pml4_nueva[i] = pml4_vieja[i];
    }

    ax_fill_bytes(pdpt_nueva, 0, 0x1000);
    ax_fill_bytes(pd_nueva, 0, 0x1000);
    pdpt_nueva[510] = (UINT64)(UINTN)pd_nueva | 0x3ULL;

    /* PD[1] cubre 0xFFFFFFFF80200000 (base + 2 MiB). 16 entradas de 2 MiB
       cubren los 32 MiB de la region del kernel. */
    for (int j = 1; j <= 16; ++j) {
        pd_nueva[j] = (0x200000ULL + (UINT64)(j - 1) * 0x200000ULL) | 0x83ULL;
    }

    pml4_nueva[511] = (UINT64)(UINTN)pdpt_nueva | 0x3ULL;

    /* Cargar la PML4 propia: ademas de publicar la ventana, invalida la TLB. */
    __asm__ volatile("mov %0, %%cr3" : : "r"((UINT64)(UINTN)pml4_nueva)
                     : "memory");
}

static EFI_STATUS ax_read_file(EFI_FILE_PROTOCOL* root, CHAR16* path,
                               void** buffer, UINTN* size)
{
    EFI_FILE_PROTOCOL* file = NULL;
    EFI_STATUS status = uefi_call_wrapper(root->Open, 5, root, &file, path,
                                          EFI_FILE_MODE_READ, EFI_FILE_READ_ONLY);
    if (EFI_ERROR(status)) {
        return status;
    }

    UINTN info_size = SIZE_OF_EFI_FILE_INFO + 512;
    EFI_FILE_INFO* info = NULL;
    status = uefi_call_wrapper(BS->AllocatePool, 3, EfiLoaderData, info_size,
                               (void**)&info);
    if (EFI_ERROR(status)) {
        uefi_call_wrapper(file->Close, 1, file);
        return status;
    }

    status = uefi_call_wrapper(file->GetInfo, 4, file, &gEfiFileInfoGuid,
                               &info_size, info);
    if (EFI_ERROR(status)) {
        uefi_call_wrapper(BS->FreePool, 1, info);
        uefi_call_wrapper(file->Close, 1, file);
        return status;
    }

    UINTN file_size = (UINTN)info->FileSize;
    uefi_call_wrapper(BS->FreePool, 1, info);

    void* data = NULL;
    status = uefi_call_wrapper(BS->AllocatePool, 3, EfiLoaderData,
                               file_size ? file_size : 1, &data);
    if (EFI_ERROR(status)) {
        uefi_call_wrapper(file->Close, 1, file);
        return status;
    }

    UINTN read_size = file_size;
    status = uefi_call_wrapper(file->Read, 3, file, &read_size, data);
    uefi_call_wrapper(file->Close, 1, file);
    if (EFI_ERROR(status)) {
        uefi_call_wrapper(BS->FreePool, 1, data);
        return status;
    }

    *buffer = data;
    *size   = read_size;
    return EFI_SUCCESS;
}

/* ------------------------------------------------------------------ */
/* Traduccion del mapa UEFI a una tabla E820                           */
/* ------------------------------------------------------------------ */
static UINT64 ax_build_e820(EFI_MEMORY_DESCRIPTOR* map, UINTN map_size,
                            UINTN desc_size, AxE820Entry* out, UINTN max,
                            UINT64* mem_size)
{
    UINTN count = 0;
    UINT64 highest = 0;

    for (UINTN off = 0; off + desc_size <= map_size && count < max;
         off += desc_size) {
        EFI_MEMORY_DESCRIPTOR* d = (EFI_MEMORY_DESCRIPTOR*)((UINT8*)map + off);
        UINT64 base = (UINT64)d->PhysicalStart;
        UINT64 length = (UINT64)d->NumberOfPages * 4096ULL;

        if (length == 0) {
            continue;
        }

        /*
         * Memoria utilizable por el kernel una vez que ha llamado a
         * ExitBootServices:
         *   - EfiConventionalMemory (7)
         *   - EfiBootServicesCode   (3)  ya no hace falta el firmware
         *   - EfiBootServicesData   (4)
         * Todo lo demas (incluidos los buffers EfiLoaderData que acabamos de
         * reservar, el codigo del cargador y la memoria ACPI) queda reservado.
         */
        bool usable = (d->Type == EfiConventionalMemory ||
                       d->Type == EfiBootServicesCode ||
                       d->Type == EfiBootServicesData);
        uint32_t type = usable ? AX_E820_USABLE : 2;

        out[count].base     = base;
        out[count].length   = length;
        out[count].type     = type;
        out[count].reserved = 0;
        count++;

        if (type == AX_E820_USABLE && base + length > highest) {
            highest = base + length;
        }
    }

    *mem_size = highest;
    return count;
}

/* ------------------------------------------------------------------ */
/* Punto de entrada                                                    */
/* ------------------------------------------------------------------ */
EFI_STATUS EFIAPI efi_main(EFI_HANDLE image, EFI_SYSTEM_TABLE* st)
{
    InitializeLib(image, st);

    Print(L"[UEFI] AxolynOS UEFI Bootloader\r\n");

    EFI_STATUS status;
    EFI_LOADED_IMAGE_PROTOCOL* loaded = NULL;
    EFI_SIMPLE_FILE_SYSTEM_PROTOCOL* fs = NULL;
    EFI_FILE_PROTOCOL* root = NULL;

    status = uefi_call_wrapper(BS->HandleProtocol, 3, image,
                               &gEfiLoadedImageProtocolGuid, (void**)&loaded);
    if (EFI_ERROR(status)) {
        Print(L"[UEFI] Error: no se pudo obtener LoadedImage (0x%lx)\r\n",
              (UINT64)status);
        return status;
    }

    status = uefi_call_wrapper(BS->HandleProtocol, 3, loaded->DeviceHandle,
                               &gEfiSimpleFileSystemProtocolGuid, (void**)&fs);
    if (EFI_ERROR(status)) {
        Print(L"[UEFI] Error: no se pudo obtener el sistema de ficheros\r\n");
        return status;
    }

    status = uefi_call_wrapper(fs->OpenVolume, 2, fs, &root);
    if (EFI_ERROR(status)) {
        Print(L"[UEFI] Error: no se pudo abrir el volumen EFI\r\n");
        return status;
    }

    /* --- Kernel ---------------------------------------------------- */
    void* kernel_buf = NULL;
    UINTN kernel_size = 0;
    status = ax_read_file(root, KERNEL_PATH, &kernel_buf, &kernel_size);
    if (EFI_ERROR(status)) {
        Print(L"[UEFI] Error: no se encuentra %ls\r\n", KERNEL_PATH);
        Print(L"[UEFI] Copia axolyn.elf en ESP:\\EFI\\AXOLYNOS\\\r\n");
        return status;
    }
    Print(L"[UEFI] axolyn.elf: %d bytes en 0x%lx\r\n", kernel_size,
          (UINT64)(UINTN)kernel_buf);

    Elf64_Ehdr* eh = (Elf64_Ehdr*)kernel_buf;
    if (*(UINT32*)eh->e_ident != ELF_MAGIC || eh->e_ident[4] != 2 ||
        eh->e_machine != 62) {
        Print(L"[UEFI] Error: axolyn.elf no es un ELF64 x86-64 valido\r\n");
        return EFI_LOAD_ERROR;
    }

    /* --- Segmentos del kernel --------------------------------------- */
    /*
     * El kernel NO es reubicable: sus segmentos PT_LOAD estan enlazados a una
     * direccion fisica fija (0x200000.. en este enlazado).  Bajo UEFI esa
     * franja puede estar ocupada parcialmente por el firmware (ACPI NVS,
     * BootServicesData...), asi que NO se reserva con AllocateAddress.
     * En su lugar se copia DESPUES de ExitBootServices, cuando toda la RAM
     * disponible es nuestra y el firmware ya no puede corromperse.
     */
    Elf64_Phdr* ph = (Elf64_Phdr*)((UINT8*)kernel_buf + eh->e_phoff);
    UINT64 kernel_low  = ~0ULL;
    UINT64 kernel_high = 0;
    UINT64 kernel_phys_base = ~0ULL;

    for (UINTN i = 0; i < eh->e_phnum; ++i) {
        if (ph[i].p_type != PT_LOAD || ph[i].p_memsz == 0) {
            continue;
        }
        UINT64 start = (UINT64)ph[i].p_paddr;
        UINT64 end   = start + (UINT64)ph[i].p_memsz;
        if (start < kernel_low)  kernel_low  = start;
        if (end   > kernel_high) kernel_high = end;
        if (start < kernel_phys_base) kernel_phys_base = start;

        Print(L"[UEFI] PT_LOAD: paddr=0x%lx filesz=0x%lx memsz=0x%lx\r\n",
              (UINT64)ph[i].p_paddr, (UINT64)ph[i].p_filesz,
              (UINT64)ph[i].p_memsz);
    }

    if (kernel_high == 0) {
        Print(L"[UEFI] Error: axolyn.elf no tiene segmentos PT_LOAD\r\n");
        return EFI_LOAD_ERROR;
    }

    kernel_low  &= ~0xFFFULL;
    kernel_high  = (kernel_high + 0xFFF) & ~0xFFFULL;
    Print(L"[UEFI] kernel fisico: 0x%lx - 0x%lx\r\n", kernel_low, kernel_high);

    /* El buffer del ELF no puede solaparse con el destino: se copiaria sobre
       si mismo y ademas los segmentos se leen desde ahi. */
    if ((UINT64)(UINTN)kernel_buf < kernel_high &&
        (UINT64)(UINTN)kernel_buf + (UINT64)kernel_size > kernel_low) {
        Print(L"[UEFI] Error: el buffer de axolyn.elf pisa el kernel\r\n");
        return EFI_LOAD_ERROR;
    }


    /* --- Initrd (opcional) ----------------------------------------- */
    void* initrd_buf = NULL;
    UINTN initrd_size = 0;
    status = ax_read_file(root, INITRD_PATH, &initrd_buf, &initrd_size);
    if (EFI_ERROR(status)) {
        Print(L"[UEFI] Aviso: no hay initrd.axfs (se continua sin AXFS)\r\n");
        initrd_buf  = NULL;
        initrd_size = 0;
    } else {
        Print(L"[UEFI] initrd.axfs: %d bytes en 0x%lx\r\n", initrd_size,
              (UINT64)(UINTN)initrd_buf);
        if ((UINT64)(UINTN)initrd_buf < kernel_high &&
            (UINT64)(UINTN)initrd_buf + (UINT64)initrd_size > kernel_low) {
            Print(L"[UEFI] Error: el buffer del initrd pisa el kernel\r\n");
            return EFI_LOAD_ERROR;
        }
    }

    /* --- Reservas en memoria baja (antes del mapa final) ------------ */
    /*
     * AxBootInfo y la tabla E820 tienen que estar por debajo de los 64 MiB:
     * son las unicas tablas de paginas que existen cuando boot.S copia el
     * AxBootInfo.  Se reservan ANTES de pedir el mapa definitivo porque
     * cualquier AllocatePages posterior invalidaria la clave del mapa.
     * Ademas quedan fuera del rango fisico del kernel para que la copia
     * posterior a ExitBootServices no las pise.
     */
    void* bootinfo_mem = NULL;
    status = ax_alloc_low_outside(1, kernel_low, kernel_high, &bootinfo_mem);
    if (EFI_ERROR(status)) {
        Print(L"[UEFI] Error: no hay memoria baja para AxBootInfo (0x%lx)\r\n",
              (UINT64)status);
        return status;
    }

    AxE820Entry* e820 = NULL;
    status = ax_alloc_low_outside(((E820_MAX * sizeof(AxE820Entry)) + 0xFFF) / 0x1000,
                                  kernel_low, kernel_high, (void**)&e820);
    if (EFI_ERROR(status)) {
        Print(L"[UEFI] Error: no hay memoria baja para E820 (0x%lx)\r\n",
              (UINT64)status);
        return status;
    }
    ax_zero(e820, E820_MAX * sizeof(AxE820Entry));

    /*
     * Tres paginas para injertar la ventana del kernel en las tablas del
     * firmware (PML4/PDPT/PD propios).  Se reservan ahora porque despues de
     * ExitBootServices ya no hay AllocatePages.
     */
    void* pt_scratch = NULL;
    status = ax_alloc_low_outside(3, kernel_low, kernel_high, &pt_scratch);
    if (EFI_ERROR(status)) {
        Print(L"[UEFI] Error: no hay memoria para las tablas (0x%lx)\r\n",
              (UINT64)status);
        return status;
    }

    /* ax_map_kernel_window() supone paginacion de 4 niveles. */
    UINT64 cr4 = 0;
    __asm__ volatile("mov %%cr4, %0" : "=r"(cr4));
    if (cr4 & (1ULL << 12)) {
        Print(L"[UEFI] Error: el firmware usa paginacion de 5 niveles\r\n");
        return EFI_UNSUPPORTED;
    }

    /* --- Mapa de memoria ------------------------------------------- */
    UINTN map_key = 0;
    UINTN desc_size = 0;
    UINTN map_size = 0;
    UINT32 desc_version = 0;
    uefi_call_wrapper(BS->GetMemoryMap, 5, &map_size, NULL, &map_key,
                      &desc_size, &desc_version);
    map_size += 8 * desc_size;

    void* map = NULL;
    status = ax_alloc_low_outside((map_size + 0xFFF) / 0x1000,
                                  kernel_low, kernel_high, &map);
    if (EFI_ERROR(status)) {
        Print(L"[UEFI] Error: no hay memoria baja para el mapa (0x%lx)\r\n",
              (UINT64)status);
        return status;
    }
    status = uefi_call_wrapper(BS->GetMemoryMap, 5, &map_size, map, &map_key,
                               &desc_size, &desc_version);
    if (EFI_ERROR(status)) {
        Print(L"[UEFI] Error en GetMemoryMap (0x%lx)\r\n", (UINT64)status);
        return status;
    }

    /* --- Tabla E820 ------------------------------------------------- */
    UINT64 mem_size = 0;
    UINT64 e820_count = ax_build_e820((EFI_MEMORY_DESCRIPTOR*)map, map_size,
                                      desc_size, e820, E820_MAX, &mem_size);
    /*
     * El VMM del kernel mapea en identidad [0, mem_size).  El initrd lo
     * colocamos nosotros (EfiLoaderData) y puede quedar por encima de la
     * ultima region que el firmware declara como utilizable, asi que hay que
     * garantizar que mem_size lo cubre; de lo contrario ax_vmm_init() no lo
     * mapearia y el VFS fallaria al leerlo.
     */
    if (mem_size < kernel_high) {
        mem_size = kernel_high;
    }
    if (initrd_buf) {
        UINT64 initrd_end = (UINT64)(UINTN)initrd_buf + (UINT64)initrd_size;
        initrd_end = (initrd_end + 0xFFF) & ~0xFFFULL;
        if (mem_size < initrd_end) {
            mem_size = initrd_end;
        }
    }

    Print(L"[UEFI] E820 sintetica: %d entradas, %d MiB\r\n",
          e820_count, mem_size / (1024 * 1024));

    /* --- AxBootInfo ------------------------------------------------- */
    AxBootInfo* bi = (AxBootInfo*)bootinfo_mem;
    ax_zero(bi, 4096);
    bi->magic        = AX_BOOTINFO_MAGIC;
    bi->kernel_phys  = kernel_phys_base;
    bi->kernel_virt  = eh->e_entry;
    bi->initrd_phys  = (UINT64)(UINTN)initrd_buf;
    bi->initrd_size  = initrd_size;
    bi->mem_size     = mem_size;
    bi->e820_entries = e820_count;
    bi->e820_ptr     = (UINT64)(UINTN)e820;
    bi->cmdline      = 0;

    Print(L"[UEFI] AxBootInfo en 0x%lx\r\n", (UINT64)(UINTN)bi);
    Print(L"[UEFI] Saltando al kernel en 0x%lx\r\n", (UINT64)eh->e_entry);

    /*
     * La copia del kernel se hace con la pila actual del cargador.  Si el
     * firmware la hubiera colocado dentro del rango del kernel, la copia la
     * pisaria a mitad de camino: mejor abortar que corromper la pila.
     */
    UINT64 rsp_actual = 0;
    __asm__ volatile("mov %%rsp, %0" : "=r"(rsp_actual));
    if (rsp_actual >= kernel_low && rsp_actual < kernel_high) {
        Print(L"[UEFI] Error: la pila (0x%lx) cae dentro del kernel\r\n",
              rsp_actual);
        return EFI_LOAD_ERROR;
    }

    /* --- Salir de Boot Services ------------------------------------ */
    /* OJO: a partir de aqui ya no se puede usar Print() ni ningun servicio
       del firmware; solo queda memoria y el kernel. */
    for (int attempt = 0; attempt < 3; ++attempt) {
        status = uefi_call_wrapper(BS->ExitBootServices, 2, image, map_key);
        if (!EFI_ERROR(status)) {
            break;
        }
        /* El mapa cambia con cada asignacion: hay que refrescarlo. */
        uefi_call_wrapper(BS->GetMemoryMap, 5, &map_size, map,
                          &map_key, &desc_size, &desc_version);
    }
    if (EFI_ERROR(status)) {
        Print(L"[UEFI] Error: ExitBootServices fallo (0x%lx)\r\n",
              (UINT64)status);
        return status;
    }

    /*
     * --- Copia del kernel -------------------------------------------
     * Ya sin firmware, toda la RAM del rango es nuestra: se puede escribir
     * encima de lo que era ACPI NVS o BootServicesData sin romper nada.
     * Primero se pone a cero el segmento completo (cubre .bss) y despues se
     * copian los bytes del fichero (cubre .text/.data/.rodata).
     */
    for (UINTN i = 0; i < eh->e_phnum; ++i) {
        if (ph[i].p_type != PT_LOAD || ph[i].p_memsz == 0) {
            continue;
        }
        UINT8* dst = (UINT8*)(UINTN)ph[i].p_paddr;
        ax_fill_bytes(dst, 0, (UINTN)ph[i].p_memsz);
        ax_copy_bytes(dst, (UINT8*)kernel_buf + ph[i].p_offset,
                      (UINTN)ph[i].p_filesz);
    }

    /*
     * La entrada del kernel es la direccion VIRTUAL de la mitad alta.  Hay
     * que publicar esa ventana en las tablas actuales antes de saltar.
     */
    ax_map_kernel_window(pt_scratch);
    __asm__ volatile("mfence" ::: "memory");

    KernelEntry entry = (KernelEntry)(UINTN)eh->e_entry;
    entry((uint64_t)(UINTN)bi);

    for (;;) {
        __asm__ volatile("hlt");
    }
    return EFI_SUCCESS;
}
