/*
 * AxolynOS - Gestor de memoria virtual (paginacion de 4 niveles).
 *
 * AxolynOS usa UN UNICO espacio de direcciones: el kernel vive en la mitad
 * superior (0xFFFFFFFF80200000) y las aplicaciones en la mitad baja.  El
 * mapa bajo es de identidad, de modo que una direccion virtual de user mode
 * coincide con la direccion fisica y el planificador puede repartir los
 * frames de la RAM entre los procesos sin necesidad de mover datos.
 *
 * Mapa que instala ax_vmm_init():
 *
 *   0x0000000000000000 - 0x00000000001FFFFF   4 KiB, supervisor (BIOS, VGA)
 *   0x0000000000200000 - 0x00000000021FFFFF   4 KiB, supervisor (region kernel)
 *   0x0000000002200000 - min(ram, 2 GiB)      2 MiB, user
 *   0xFFFFFFFF80200000 - 0xFFFFFFFF821FFFFF   2 MiB, supervisor (ventana kernel)
 *   0xFFFFFFFFA0000000 - 0xFFFFFFFFA01FFFFF   2 MiB, supervisor (mapa sombra)
 *
 * Las tablas de pagina se reservan en AX_PTABLES_PHYS (0xA00000), dentro de la
 * region reservada del kernel, y se acceden a ellas por el mapa de identidad.
 */

#include "ax_types.h"
#include "ax_memory.h"
#include "ax_cpu.h"
#include "ax_console.h"

/* ------------------------------------------------------------------ */
/* Estado del VMM                                                      */
/* ------------------------------------------------------------------ */
static uint64_t ax_vmm_pml4_phys_addr = 0;
static uint64_t ax_vmm_next_table = AX_PTABLES_PHYS;
static uint64_t ax_vmm_low_bytes = 0;
static bool     ax_vmm_batch = false;    /* durante la inicializacion no hay TLB flush */

/* ------------------------------------------------------------------ */
/* Utilidades propias                                                   */
/* ------------------------------------------------------------------ */
static void ax_memset(void* dst, int value, uint64_t count)
{
    uint8_t* p = (uint8_t*)dst;
    for (uint64_t i = 0; i < count; ++i) {
        p[i] = (uint8_t)value;
    }
}

/* ------------------------------------------------------------------ */
/* Asignacion de tablas de pagina                                      */
/* ------------------------------------------------------------------ */
static uint64_t* ax_vmm_alloc_table(void)
{
    if (ax_vmm_next_table + AX_PAGE_SIZE > AX_PTABLES_PHYS + AX_PTABLES_MAX) {
        return nullptr;
    }
    uint64_t* table = (uint64_t*)ax_vmm_next_table;
    ax_vmm_next_table += AX_PAGE_SIZE;
    ax_memset(table, 0, AX_PAGE_SIZE);
    return table;
}

/* ------------------------------------------------------------------ */
/* Construccion de las entradas                                        */
/* ------------------------------------------------------------------ */
static inline uint64_t ax_vmm_make_flags(uint64_t flags)
{
    /* Solo se admiten los bits de permiso del contrato; el bit de presencia
       se pone siempre (una entrada ausente no es un mapeo). */
    uint64_t entry = flags & (AX_PAGE_WRITE | AX_PAGE_USER |
                               AX_PAGE_WRITE_THROUGH | AX_PAGE_CACHE_DISABLE);
    entry |= AX_PAGE_PRESENT;
    return entry;
}

static inline uint64_t ax_vmm_pte_addr(uint64_t entry)
{
    return entry & 0x000FFFFFFFFFF000ull;
}

/*
 * Devuelve la direccion fisica de la tabla del nivel indicado para la
 * direccion virtual "virt": nivel 1 = tabla de paginas, 2 = directorio de
 * paginas, 3 = directorio de punteros, 4 = PML4.
 *
 * Si "create" es true se crean las tablas intermedias que falten; si es
 * false, la funcion devuelve 0 en cuanto encuentra un hueco, de modo que el
 * llamante puede distinguir "no mapeado" de "mapeado".
 */
static uint64_t ax_vmm_table_at(uint64_t virt, int level, bool create)
{
    uint64_t table = ax_vmm_pml4_phys_addr;

    for (int l = 4; l > level; --l) {
        uint64_t index = (virt >> (12 + 9 * (l - 1))) & 0x1FF;
        uint64_t* entry = (uint64_t*)(table + index * 8);
        if (!(*entry & AX_PAGE_PRESENT)) {
            if (!create) {
                return 0;
            }
            uint64_t* next = ax_vmm_alloc_table();
            if (!next) {
                return 0;
            }
            /*
             * Las tablas intermedias se marcan siempre como "user" (U/S=1).
             * En x86-64 el bit U/S se comprueba en TODOS los niveles de la
             * jerarquia: si una sola tabla intermedia es de supervisor, una
             * aplicacion en ring 3 recibe un #PF al tocar la pagina, aunque
             * la entrada de nivel 1 tenga U/S=1.  Ponerlo a 1 aqui es
             * inofensivo: la ultima decision la toma siempre la entrada de
             * nivel 1, que si es de supervisor sigue protegiendo la pagina.
             */
            *entry = (uint64_t)(uintptr_t)next | AX_PAGE_PRESENT | AX_PAGE_WRITE |
                     AX_PAGE_USER;
        } else if (*entry & 0x80) {
            /* Una pagina grande en este nivel impide bajar al siguiente. */
            return 0;
        }
        table = ax_vmm_pte_addr(*entry);
    }
    return table;
}

/*
 * Recorre la jerarquia de tablas hasta la tabla de paginas (nivel 1) de la
 * direccion virtual indicada, creando las tablas intermedias que falten.
 * Devuelve la direccion fisica de la tabla de paginas, o 0 si no se han
 * podido crear.
 */
static uint64_t ax_vmm_walk_create(uint64_t virt)
{
    return ax_vmm_table_at(virt, 1, true);
}

/*
 * Igual que ax_vmm_walk_create() pero sin crear tablas: devuelve 0 si la
 * traduccion no existe.
 */
static uint64_t ax_vmm_walk_find(uint64_t virt)
{
    return ax_vmm_table_at(virt, 1, false);
}

/* ------------------------------------------------------------------ */
/* API de mapeo                                                        */
/* ------------------------------------------------------------------ */
void* ax_vmm_map(uint64_t virt, uint64_t phys, uint64_t flags)
{
    /* Si la direccion cae dentro de una pagina de 2 MiB ya existente hay que
       sustituirla por una tabla de paginas: primero la localizamos en el
       nivel 2 (directorio de paginas). */
    uint64_t pd = ax_vmm_table_at(virt, 2, true);
    if (!pd) {
        return nullptr;
    }
    uint64_t pd_index = (virt >> 21) & 0x1FF;
    uint64_t* pd_entry = (uint64_t*)(pd + pd_index * 8);

    if (*pd_entry & 0x80) {
        /* Reproducimos la pagina de 2 MiB en 512 paginas de 4 KiB para no
           perder lo que ya estaba mapeado. */
        uint64_t base = ax_vmm_pte_addr(*pd_entry);
        uint64_t entry_flags = *pd_entry & 0x0F;   /* permisos de la pagina */

        uint64_t* new_pt = ax_vmm_alloc_table();
        if (!new_pt) {
            return nullptr;
        }
        for (uint64_t i = 0; i < 512; ++i) {
            new_pt[i] = (base + i * AX_PAGE_SIZE) | entry_flags;
        }
        *pd_entry = (uint64_t)(uintptr_t)new_pt | AX_PAGE_PRESENT | AX_PAGE_WRITE |
                    (entry_flags & AX_PAGE_USER);
    }

    uint64_t pt = ax_vmm_walk_create(virt);
    if (!pt) {
        return nullptr;
    }
    uint64_t index = (virt >> AX_PAGE_SHIFT) & 0x1FF;
    uint64_t* entry = (uint64_t*)(pt + index * 8);

    *entry = (phys & 0x000FFFFFFFFFF000ull) | ax_vmm_make_flags(flags);
    if (!ax_vmm_batch) {
        ax_flush_tlb();
    }
    return (void*)(uintptr_t)virt;
}

void* ax_vmm_map_2m(uint64_t virt, uint64_t phys, uint64_t flags)
{
    /* La entrada de nivel 2 (PD) es la que puede contener paginas de 2 MiB. */
    uint64_t pd = ax_vmm_table_at(virt, 2, true);
    if (!pd) {
        return nullptr;
    }
    uint64_t index = (virt >> 21) & 0x1FF;
    uint64_t* entry = (uint64_t*)(pd + index * 8);

    *entry = (phys & 0x000FFFFFFFE00000ull) | ax_vmm_make_flags(flags) | 0x80ull;
    if (!ax_vmm_batch) {
        ax_flush_tlb();
    }
    return (void*)(uintptr_t)virt;
}

int ax_vmm_unmap(uint64_t virt)
{
    uint64_t pt = ax_vmm_walk_find(virt);
    if (!pt) {
        return -1;
    }
    uint64_t index = (virt >> AX_PAGE_SHIFT) & 0x1FF;
    uint64_t* entry = (uint64_t*)(pt + index * 8);
    if (!(*entry & AX_PAGE_PRESENT)) {
        return -1;
    }
    *entry = 0;
    ax_flush_tlb();
    return 0;
}

uint64_t ax_vmm_translate(uint64_t virt)
{
    uint64_t table = ax_vmm_pml4_phys_addr;

    for (int level = 4; level > 1; --level) {
        uint64_t index = (virt >> (12 + 9 * (level - 1))) & 0x1FF;
        uint64_t* entry = (uint64_t*)(table + index * 8);
        if (!(*entry & AX_PAGE_PRESENT)) {
            return 0;
        }
        if (*entry & 0x80) {
            /* Pagina grande: la direccion fisica es la base mas el
               desplazamiento dentro de la pagina. */
            uint64_t base = ax_vmm_pte_addr(*entry);
            uint64_t size = (level == 3) ? (1ull << 30) : (1ull << 21);
            return base + (virt & (size - 1));
        }
        table = ax_vmm_pte_addr(*entry);
    }

    uint64_t index = (virt >> AX_PAGE_SHIFT) & 0x1FF;
    uint64_t* entry = (uint64_t*)(table + index * 8);
    if (!(*entry & AX_PAGE_PRESENT)) {
        return 0;
    }
    return ax_vmm_pte_addr(*entry) + (virt & (AX_PAGE_SIZE - 1));
}

int ax_vmm_protect(uint64_t virt, uint64_t flags)
{
    uint64_t pt = ax_vmm_walk_find(virt);
    if (!pt) {
        return -1;
    }
    uint64_t index = (virt >> AX_PAGE_SHIFT) & 0x1FF;
    uint64_t* entry = (uint64_t*)(pt + index * 8);
    if (!(*entry & AX_PAGE_PRESENT)) {
        return -1;
    }
    uint64_t address = ax_vmm_pte_addr(*entry);
    *entry = address | ax_vmm_make_flags(flags);
    ax_flush_tlb();
    return 0;
}

uint64_t ax_vmm_pml4_phys(void)
{
    return ax_vmm_pml4_phys_addr;
}

uint64_t ax_vmm_low_mapped_bytes(void)
{
    return ax_vmm_low_bytes;
}

/* ------------------------------------------------------------------ */
/* Construccion del mapa inicial                                        */
/* ------------------------------------------------------------------ */
static void ax_vmm_build_low_map(uint64_t ram_bytes)
{
    uint64_t limit = ram_bytes;
    const uint64_t ax_max_low = 0x80000000ull;    /* 2 GiB */

    if (limit > ax_max_low) {
        limit = ax_max_low;
    }
    if (limit < AX_KERNEL_REGION_SIZE) {
        limit = AX_KERNEL_REGION_SIZE;   /* siempre mapear la region del kernel */
    }

    /* --- Region 0..32 MiB: paginas de 4 KiB en modo supervisor ------- */
    /* El modo supervisor es obligatorio: ahi viven la VGA, las tablas del
       firmware y toda la region reservada del kernel (imagen, tablas de
       pagina, bitmap, heap, staging e initrd).  Tiene que ser ESCRIBIBLE:
       el kernel modifica sus propias variables (.bss), las tablas de pagina
       en AX_PTABLES_PHYS y la memoria de video.  Con CR0.WP=1 (como deja el
       firmware UEFI) una pagina de solo lectura provoca #PF al escribir. */
    for (uint64_t addr = 0; addr < AX_KERNEL_REGION_SIZE; addr += AX_PAGE_SIZE) {
        ax_vmm_map(addr, addr, AX_PAGE_WRITE);
    }

    /* --- Region 32 MiB..limite: paginas de 2 MiB en modo user ------- */
    /* Se marcan como USER para que las aplicaciones en ring 3 puedan
       acceder a la RAM que les asigne el planificador, y WRITE porque el
       kernel tambien escribe ahi (carga de ELF, pilas, etc.). */
    uint64_t addr = AX_KERNEL_REGION_SIZE;
    /* La ultima pagina de 2 MiB puede quedar incompleta: la parte final se
       mapea con paginas de 4 KiB. */
    uint64_t aligned_end = limit & ~0x1FFFFFull;

    while (addr < aligned_end) {
        ax_vmm_map_2m(addr, addr, AX_PAGE_WRITE | AX_PAGE_USER);
        addr += 0x200000;
    }
    while (addr < limit) {
        ax_vmm_map(addr, addr, AX_PAGE_WRITE | AX_PAGE_USER);
        addr += AX_PAGE_SIZE;
    }

    ax_vmm_low_bytes = limit;
}

void ax_vmm_init(void)
{
    /* --- Reinicio del|area de tablas de pagina ----------------------- */
    /* Las tablas del arranque (boot.S) viven en el .bss del kernel, asi que
       aqui podemos reutilizar todo AX_PTABLES_PHYS sin riesgo de pisar la
       tabla que esta activa. */
    ax_vmm_next_table = AX_PTABLES_PHYS;
    ax_vmm_low_bytes = 0;

    uint64_t* pml4 = ax_vmm_alloc_table();
    if (!pml4) {
        ax_panic(__FILE__, __LINE__, "no hay memoria para la PML4");
    }
    ax_vmm_pml4_phys_addr = (uint64_t)(uintptr_t)pml4;

    /* Durante la construccion no we flushear la TLB en cada mapeo: se hace
       una sola vez al cargar CR3. */
    ax_vmm_batch = true;

    /* --- Mapa bajo de identidad ------------------------------------- */
    uint64_t ram_bytes = ax_pmm_total_bytes();
    ax_vmm_build_low_map(ram_bytes);

    /* --- Ventana del kernel (mitad superior) ------------------------ */
    /* 0xFFFFFFFF80200000 -> 0x200000, 32 MiB con paginas de 2 MiB.
       ESCRIBIBLE: aqui viven .data/.bss, la pila y el heap del kernel. */
    for (uint64_t i = 0; i < AX_KERNEL_REGION_SIZE / 0x200000; ++i) {
        ax_vmm_map_2m(AX_KERNEL_VIRT_BASE + i * 0x200000,
                      AX_KERNEL_PHYS_BASE + i * 0x200000, AX_PAGE_WRITE);
    }

    /* --- Mapa sombra de las tablas de pagina ------------------------ */
    /* AX_SHADOW_VIRT_BASE cae en el mismo PD que la ventana del kernel, asi
       que se mapea aqui la primera MiB del area de tablas.  ESCRIBIBLE para
       poder modificar las tablas desde el espacio del kernel. */
    ax_vmm_map_2m(AX_SHADOW_VIRT_BASE, AX_PTABLES_PHYS, AX_PAGE_WRITE);

    ax_vmm_batch = false;

    /* --- Activamos la tabla nueva ----------------------------------- */
    ax_load_cr3(ax_vmm_pml4_phys_addr);
}
