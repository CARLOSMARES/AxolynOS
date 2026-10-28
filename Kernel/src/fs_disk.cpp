/*
 * AxolynOS - AxFS en disco.
 *
 * Implementacion real del formateador y del sistema de ficheros que usa el
 * instalador.  El diseno esta documentado en Kernel/include/ax_fs_disk.h.
 *
 * Todas las operaciones se hacen sin heap: la tabla de entradas (8 KiB) y el
 * bitmap (4 KiB) viven en .bss y se cargan al montar.  El buffer de bloque
 * evita leer/escribir directamente sobre el buffer del llamante, que puede
 * no tener espacio para un bloque completo.
 */
#include "ax_fs_disk.h"
#include "ax_disk.h"
#include "ax_console.h"
#include "axos/abi.h"

static bool          g_mounted = false;
static int           g_drive = -1;
static AxfsDiskSuper g_super;
static AxfsDiskEntry g_entries[AXFS_DISK_MAX_ENTRIES];
static uint8_t       g_bitmap[AXFS_DISK_BITMAP_SECT * 512];
static uint8_t       g_block[AXFS_DISK_BLOCK_SIZE];

/* ------------------------------------------------------------------ */
/* Utilidades                                                          */
/* ------------------------------------------------------------------ */
static void fs_memzero(void* dst, uint64_t n)
{
    uint8_t* p = (uint8_t*)dst;
    while (n--) {
        *p++ = 0;
    }
}

static void fs_memcpy(void* dst, const void* src, uint64_t n)
{
    uint8_t* d = (uint8_t*)dst;
    const uint8_t* s = (const uint8_t*)src;
    while (n--) {
        *d++ = *s++;
    }
}

static uint64_t fs_strlen(const char* s)
{
    uint64_t n = 0;
    if (s) {
        while (s[n]) {
            n++;
        }
    }
    return n;
}

static int fs_streq(const char* a, const char* b)
{
    if (!a || !b) {
        return 0;
    }
    while (*a && *b) {
        if (*a != *b) {
            return 0;
        }
        a++;
        b++;
    }
    return *a == *b;
}

static bool fs_starts_with(const char* s, const char* prefix)
{
    while (*prefix) {
        if (*s++ != *prefix++) {
            return false;
        }
    }
    return true;
}

static bool fs_has_slash(const char* s)
{
    while (*s) {
        if (*s == '/') {
            return true;
        }
        s++;
    }
    return false;
}

/*
 * Normaliza una ruta de AxFS-D:
 *   - quita las barras iniciales y finales
 *   - colapsa barras repetidas
 *   - rechaza "." y ".." (el disco no tiene enlaces simbolicos)
 * El resultado se guarda sin '/' inicial.
 */
static int fs_norm(const char* path, char* out, uint64_t max)
{
    if (!path || !out || max < 2) {
        return AX_EINVAL;
    }
    while (*path == '/') {
        path++;
    }
    uint64_t j = 0;
    while (*path) {
        while (*path == '/') {
            path++;
        }
        if (!*path) {
            break;
        }

        /* Extrae el componente para validar "." y "..". */
        const char* start = path;
        while (*path && *path != '/') {
            path++;
        }
        uint64_t len = (uint64_t)(path - start);
        if (len == 1 && start[0] == '.') {
            continue;
        }
        if (len == 2 && start[0] == '.' && start[1] == '.') {
            return AX_EINVAL;
        }
        if (j && j + 1 < max) {
            out[j++] = '/';
        }
        for (uint64_t i = 0; i < len; ++i) {
            if (j + 1 >= max) {
                return AX_ERANGE;
            }
            out[j++] = start[i];
        }
    }
    out[j] = '\0';
    return 0;
}

/* Ruta del padre y nombre final. */
static void fs_split(const char* norm, char* parent, uint64_t parent_max,
                     const char** leaf)
{
    const char* last = norm;
    const char* p = norm;
    while (*p) {
        if (*p == '/') {
            last = p + 1;
        }
        p++;
    }
    uint64_t n = (uint64_t)(last - norm);
    if (n >= parent_max) {
        n = parent_max - 1;
    }
    fs_memcpy(parent, norm, n);
    parent[n] = '\0';
    if (n > 0 && parent[n - 1] == '/') {
        parent[n - 1] = '\0';
    }
    *leaf = last;
}

static uint32_t fs_checksum(const AxfsDiskSuper* s)
{
    const uint8_t* p = (const uint8_t*)s;
    uint32_t sum = 0;
    uint32_t skip_off = (uint32_t)((const uint8_t*)&s->checksum - p);
    for (uint32_t i = 0; i < sizeof(AxfsDiskSuper); ++i) {
        if (i >= skip_off && i < skip_off + 4) {
            continue;
        }
        sum = sum * 33u + p[i];
    }
    return sum;
}

static bool fs_bit_test(uint64_t block)
{
    return (g_bitmap[block >> 3] & (uint8_t)(1u << (block & 7))) != 0;
}

static void fs_bit_set(uint64_t block)
{
    g_bitmap[block >> 3] |= (uint8_t)(1u << (block & 7));
}

static void fs_bit_clear(uint64_t block)
{
    g_bitmap[block >> 3] &= (uint8_t)~(1u << (block & 7));
}

static int fs_write_meta(void)
{
    g_super.free_blocks = 0;
    for (uint64_t i = 0; i < g_super.data_blocks; ++i) {
        if (!fs_bit_test(i)) {
            g_super.free_blocks++;
        }
    }
    g_super.checksum = fs_checksum(&g_super);

    uint8_t sector[512];
    fs_memzero(sector, sizeof(sector));
    fs_memcpy(sector, &g_super, sizeof(g_super));
    if (ax_disk_write(g_drive, AXFS_DISK_LBA, 1, sector) != 0) {
        return AX_EIO;
    }
    if (ax_disk_write(g_drive, g_super.entry_lba,
                      (uint32_t)(sizeof(g_entries) / 512), g_entries) != 0) {
        return AX_EIO;
    }
    if (ax_disk_write(g_drive, g_super.bitmap_lba, g_super.bitmap_sectors,
                      g_bitmap) != 0) {
        return AX_EIO;
    }
    return 0;
}

static AxfsDiskEntry* fs_find(const char* name)
{
    for (uint32_t i = 0; i < g_super.entry_max; ++i) {
        if (g_entries[i].type != AXFS_DISK_TYPE_FREE &&
            fs_streq(g_entries[i].name, name)) {
            return &g_entries[i];
        }
    }
    return nullptr;
}

static AxfsDiskEntry* fs_alloc_entry(void)
{
    for (uint32_t i = 0; i < g_super.entry_max; ++i) {
        if (g_entries[i].type == AXFS_DISK_TYPE_FREE) {
            fs_memzero(&g_entries[i], sizeof(g_entries[i]));
            return &g_entries[i];
        }
    }
    return nullptr;
}

static void fs_free_blocks(uint64_t first, uint64_t count)
{
    for (uint64_t i = 0; i < count; ++i) {
        if (first + i < g_super.data_blocks) {
            fs_bit_clear(first + i);
        }
    }
}

/*
 * Busca "count" bloques libres contiguos.  Devuelve el indice del primero o
 * (uint64_t)-1 si no hay espacio.
 */
static uint64_t fs_alloc_blocks(uint64_t count)
{
    if (count == 0) {
        return 0;
    }
    if (count > g_super.data_blocks) {
        return (uint64_t)-1;
    }
    for (uint64_t start = 0; start + count <= g_super.data_blocks; ++start) {
        if (fs_bit_test(start)) {
            continue;
        }
        uint64_t i = 1;
        while (i < count && !fs_bit_test(start + i)) {
            i++;
        }
        if (i == count) {
            for (i = 0; i < count; ++i) {
                fs_bit_set(start + i);
            }
            return start;
        }
    }
    return (uint64_t)-1;
}

static bool fs_valid(void)
{
    return g_mounted && g_drive >= 0;
}

/* ------------------------------------------------------------------ */
/* Formato y montaje                                                   */
/* ------------------------------------------------------------------ */
int ax_fs_disk_format(int drive)
{
    AxDiskInfo info;
    if (ax_disk_get_info(drive, &info) != 0) {
        return AX_ENOENT;
    }
    if (info.sectors < AXFS_DISK_LBA + 64) {
        return AX_ENOSPC;
    }

    fs_memzero(&g_super, sizeof(g_super));
    fs_memzero(g_entries, sizeof(g_entries));
    fs_memzero(g_bitmap, sizeof(g_bitmap));

    fs_memcpy(g_super.magic, AXFS_DISK_MAGIC, AXFS_DISK_MAGIC_LEN);
    g_super.version        = AXFS_DISK_VERSION;
    g_super.block_size     = AXFS_DISK_BLOCK_SIZE;
    g_super.total_sectors  = info.sectors;
    g_super.entry_lba      = AXFS_DISK_LBA + 1;
    g_super.entry_max      = AXFS_DISK_MAX_ENTRIES;
    g_super.entry_count    = 0;
    g_super.bitmap_lba     = g_super.entry_lba +
                             (sizeof(g_entries) / 512);
    g_super.bitmap_sectors = AXFS_DISK_BITMAP_SECT;
    g_super.data_lba       = g_super.bitmap_lba + g_super.bitmap_sectors;

    uint64_t blocks = (info.sectors > g_super.data_lba)
                          ? (info.sectors - g_super.data_lba) /
                                AXFS_DISK_BLOCK_SECT
                          : 0;
    if (blocks > AXFS_DISK_MAX_BLOCKS) {
        blocks = AXFS_DISK_MAX_BLOCKS;
    }
    g_super.block_count    = blocks;
    g_super.data_blocks    = blocks;
    g_super.free_blocks    = blocks;
    g_super.flags          = 1;
    g_super.checksum       = fs_checksum(&g_super);

    g_drive = drive;
    g_mounted = true;      /* fs_write_meta necesita el disco */

    int rc = fs_write_meta();
    if (rc != 0) {
        g_mounted = false;
        g_drive = -1;
        return rc;
    }

    ax_printf("[axfs] disco %d formateado: %llu bloques de %u bytes "
              "(%llu MiB)\n",
              drive, (unsigned long long)g_super.data_blocks,
              (unsigned)AXFS_DISK_BLOCK_SIZE,
              (unsigned long long)((g_super.data_blocks *
                                    AXFS_DISK_BLOCK_SIZE) / (1024ull * 1024ull)));
    return 0;
}

int ax_fs_disk_mount(int drive)
{
    AxDiskInfo info;
    if (ax_disk_get_info(drive, &info) != 0) {
        return AX_ENOENT;
    }

    uint8_t sector[512];
    if (ax_disk_read(drive, AXFS_DISK_LBA, 1, sector) != 0) {
        return AX_EIO;
    }
    fs_memzero(&g_super, sizeof(g_super));
    fs_memcpy(&g_super, sector, sizeof(g_super));

    if (g_super.magic[0] != 'A' || g_super.magic[1] != 'X' ||
        g_super.magic[2] != 'F' || g_super.magic[3] != 'S' ||
        g_super.magic[4] != 'D') {
        return AX_EINVAL;
    }
    if (g_super.version != AXFS_DISK_VERSION ||
        g_super.block_size != AXFS_DISK_BLOCK_SIZE ||
        g_super.entry_max == 0 ||
        g_super.entry_max > AXFS_DISK_MAX_ENTRIES ||
        g_super.data_blocks > AXFS_DISK_MAX_BLOCKS ||
        g_super.bitmap_sectors > AXFS_DISK_BITMAP_SECT) {
        return AX_EINVAL;
    }
    if (fs_checksum(&g_super) != g_super.checksum) {
        return AX_EINVAL;
    }

    if (ax_disk_read(drive, g_super.entry_lba,
                     (uint32_t)(sizeof(g_entries) / 512), g_entries) != 0) {
        return AX_EIO;
    }
    if (ax_disk_read(drive, g_super.bitmap_lba, g_super.bitmap_sectors,
                     g_bitmap) != 0) {
        return AX_EIO;
    }

    g_drive = drive;
    g_mounted = true;
    ax_printf("[axfs] disco %d montado: %llu bloques, %llu libres\n",
              drive, (unsigned long long)g_super.data_blocks,
              (unsigned long long)g_super.free_blocks);
    return 0;
}

int ax_fs_disk_unmount(void)
{
    g_mounted = false;
    g_drive = -1;
    return 0;
}

bool ax_fs_disk_mounted(void)
{
    return g_mounted;
}

int ax_fs_disk_drive(void)
{
    return g_drive;
}

void ax_fs_disk_init(void)
{
    g_mounted = false;
    g_drive = -1;
    fs_memzero(&g_super, sizeof(g_super));
    fs_memzero(g_entries, sizeof(g_entries));
    fs_memzero(g_bitmap, sizeof(g_bitmap));

    ax_printf("[axfs] buscando un volumen AxFS-D...\n");
    for (int i = 0; i < ax_disk_count(); ++i) {
        if (ax_fs_disk_mount(i) == 0) {
            return;
        }
    }
    ax_printf("[axfs] no hay volumen montado (usa 'format <disco>')\n");
}

/* ------------------------------------------------------------------ */
/* Operaciones de ficheros                                             */
/* ------------------------------------------------------------------ */
int ax_fs_disk_mkdir(const char* path)
{
    if (!fs_valid()) {
        return AX_ENOENT;
    }
    char name[AXFS_DISK_NAME_MAX];
    int rc = fs_norm(path, name, sizeof(name));
    if (rc != 0) {
        return rc;
    }
    if (!name[0]) {
        return AX_EINVAL;
    }
    if (fs_find(name)) {
        return AX_EEXIST;
    }
    AxfsDiskEntry* e = fs_alloc_entry();
    if (!e) {
        return AX_ENOSPC;
    }
    fs_memcpy(e->name, name, fs_strlen(name) + 1);
    e->type = AXFS_DISK_TYPE_DIR;
    e->blocks = 0;
    e->first_block = 0;
    e->size = 0;
    g_super.entry_count++;
    rc = fs_write_meta();
    if (rc != 0) {
        fs_memzero(e, sizeof(*e));
        g_super.entry_count--;
    }
    return rc;
}

int ax_fs_disk_create(const char* path)
{
    return ax_fs_disk_write(path, nullptr, 0);
}

int ax_fs_disk_write(const char* path, const void* data, uint64_t size)
{
    if (!fs_valid()) {
        return AX_ENOENT;
    }
    if (size > g_super.data_blocks * (uint64_t)AXFS_DISK_BLOCK_SIZE) {
        return AX_ENOSPC;
    }

    char name[AXFS_DISK_NAME_MAX];
    int rc = fs_norm(path, name, sizeof(name));
    if (rc != 0) {
        return rc;
    }
    if (!name[0]) {
        return AX_EINVAL;
    }
    if (fs_strlen(name) >= AXFS_DISK_NAME_MAX) {
        return AX_ERANGE;
    }

    AxfsDiskEntry* e = fs_find(name);
    bool is_new = (e == nullptr);
    if (is_new) {
        e = fs_alloc_entry();
        if (!e) {
            return AX_ENOSPC;
        }
    } else if (e->type == AXFS_DISK_TYPE_DIR) {
        return AX_EINVAL;
    }

    uint64_t needed = (size + AXFS_DISK_BLOCK_SIZE - 1) /
                      AXFS_DISK_BLOCK_SIZE;
    uint64_t old_first = e->first_block;
    uint64_t old_blocks = e->blocks;
    uint64_t first = old_first;
    bool moved = false;

    if (!is_new && old_blocks >= needed) {
        first = old_first;
        /* Libera los bloques sobrantes de la extension. */
        if (old_blocks > needed) {
            fs_free_blocks(old_first + needed, old_blocks - needed);
        }
    } else {
        first = fs_alloc_blocks(needed);
        if (needed > 0 && first == (uint64_t)-1) {
            if (is_new) {
                fs_memzero(e, sizeof(*e));
            }
            return AX_ENOSPC;
        }
        moved = true;
    }

    /* Escritura de los datos, bloque a bloque. */
    const uint8_t* src = (const uint8_t*)data;
    for (uint64_t b = 0; b < needed; ++b) {
        fs_memzero(g_block, sizeof(g_block));
        uint64_t offset = b * AXFS_DISK_BLOCK_SIZE;
        uint64_t chunk = size - offset;
        if (chunk > AXFS_DISK_BLOCK_SIZE) {
            chunk = AXFS_DISK_BLOCK_SIZE;
        }
        if (src && chunk) {
            fs_memcpy(g_block, src + offset, chunk);
        }
        uint64_t lba = g_super.data_lba +
                       (first + b) * AXFS_DISK_BLOCK_SECT;
        if (ax_disk_write(g_drive, lba, AXFS_DISK_BLOCK_SECT, g_block) != 0) {
            if (moved) {
                fs_free_blocks(first, needed);
            }
            return AX_EIO;
        }
    }

    if (moved && old_blocks > 0) {
        fs_free_blocks(old_first, old_blocks);
    }

    fs_memzero(e->name, AXFS_DISK_NAME_MAX);
    fs_memcpy(e->name, name, fs_strlen(name) + 1);
    e->type = AXFS_DISK_TYPE_FILE;
    e->blocks = (uint32_t)needed;
    e->first_block = needed ? first : 0;
    e->size = size;
    if (is_new) {
        g_super.entry_count++;
    }

    rc = fs_write_meta();
    if (rc != 0) {
        return rc;
    }
    return 0;
}

int ax_fs_disk_read(const char* path, void* buf, uint64_t size, uint64_t* out)
{
    if (out) {
        *out = 0;
    }
    if (!fs_valid()) {
        return AX_ENOENT;
    }

    char name[AXFS_DISK_NAME_MAX];
    int rc = fs_norm(path, name, sizeof(name));
    if (rc != 0) {
        return rc;
    }
    AxfsDiskEntry* e = fs_find(name);
    if (!e) {
        return AX_ENOENT;
    }
    if (e->type != AXFS_DISK_TYPE_FILE) {
        return AX_EINVAL;
    }
    if (!buf && e->size > 0) {
        return AX_EINVAL;
    }

    uint64_t to_read = e->size;
    if (to_read > size) {
        to_read = size;
    }

    uint64_t done = 0;
    while (done < to_read) {
        uint64_t b = done / AXFS_DISK_BLOCK_SIZE;
        uint64_t lba = g_super.data_lba +
                       (e->first_block + b) * AXFS_DISK_BLOCK_SECT;
        if (ax_disk_read(g_drive, lba, AXFS_DISK_BLOCK_SECT, g_block) != 0) {
            return AX_EIO;
        }
        uint64_t offset = done % AXFS_DISK_BLOCK_SIZE;
        uint64_t chunk = to_read - done;
        if (chunk > AXFS_DISK_BLOCK_SIZE - offset) {
            chunk = AXFS_DISK_BLOCK_SIZE - offset;
        }
        fs_memcpy((uint8_t*)buf + done, g_block + offset, chunk);
        done += chunk;
    }

    if (out) {
        *out = done;
    }
    return 0;
}

int ax_fs_disk_unlink(const char* path)
{
    if (!fs_valid()) {
        return AX_ENOENT;
    }
    char name[AXFS_DISK_NAME_MAX];
    int rc = fs_norm(path, name, sizeof(name));
    if (rc != 0) {
        return rc;
    }
    AxfsDiskEntry* e = fs_find(name);
    if (!e) {
        return AX_ENOENT;
    }
    if (e->type == AXFS_DISK_TYPE_DIR) {
        return AX_EINVAL;
    }
    fs_free_blocks(e->first_block, e->blocks);
    fs_memzero(e, sizeof(*e));
    if (g_super.entry_count > 0) {
        g_super.entry_count--;
    }
    return fs_write_meta();
}

int ax_fs_disk_rmdir(const char* path)
{
    if (!fs_valid()) {
        return AX_ENOENT;
    }
    char name[AXFS_DISK_NAME_MAX];
    int rc = fs_norm(path, name, sizeof(name));
    if (rc != 0) {
        return rc;
    }
    if (!name[0]) {
        return AX_EINVAL;
    }
    AxfsDiskEntry* e = fs_find(name);
    if (!e) {
        return AX_ENOENT;
    }
    if (e->type != AXFS_DISK_TYPE_DIR) {
        return AX_EINVAL;
    }

    /* No se puede borrar un directorio con contenido. */
    char prefix[AXFS_DISK_NAME_MAX + 1];
    uint64_t plen = fs_strlen(name);
    fs_memcpy(prefix, name, plen);
    prefix[plen] = '/';
    prefix[plen + 1] = '\0';
    for (uint32_t i = 0; i < g_super.entry_max; ++i) {
        if (g_entries[i].type != AXFS_DISK_TYPE_FREE &&
            fs_starts_with(g_entries[i].name, prefix)) {
            return AX_EBUSY;
        }
    }

    fs_memzero(e, sizeof(*e));
    if (g_super.entry_count > 0) {
        g_super.entry_count--;
    }
    return fs_write_meta();
}

int ax_fs_disk_rename(const char* old_path, const char* new_path)
{
    if (!fs_valid()) {
        return AX_ENOENT;
    }
    char old_name[AXFS_DISK_NAME_MAX];
    char new_name[AXFS_DISK_NAME_MAX];
    int rc = fs_norm(old_path, old_name, sizeof(old_name));
    if (rc != 0) {
        return rc;
    }
    rc = fs_norm(new_path, new_name, sizeof(new_name));
    if (rc != 0) {
        return rc;
    }
    if (!old_name[0] || !new_name[0]) {
        return AX_EINVAL;
    }
    if (fs_strlen(new_name) >= AXFS_DISK_NAME_MAX) {
        return AX_ERANGE;
    }
    AxfsDiskEntry* e = fs_find(old_name);
    if (!e) {
        return AX_ENOENT;
    }
    if (fs_find(new_name)) {
        return AX_EEXIST;
    }
    fs_memzero(e->name, AXFS_DISK_NAME_MAX);
    fs_memcpy(e->name, new_name, fs_strlen(new_name) + 1);
    return fs_write_meta();
}

int ax_fs_disk_stat(const char* path, uint32_t* type, uint64_t* size)
{
    if (!fs_valid()) {
        return AX_ENOENT;
    }
    char name[AXFS_DISK_NAME_MAX];
    int rc = fs_norm(path, name, sizeof(name));
    if (rc != 0) {
        return rc;
    }
    AxfsDiskEntry* e = fs_find(name);
    if (!e) {
        return AX_ENOENT;
    }
    if (type) {
        *type = e->type;
    }
    if (size) {
        *size = e->size;
    }
    return 0;
}

/*
 * Lista las entradas directas de un directorio.  Cada linea tiene el formato:
 *
 *   D nombre
 *   F tamano nombre
 */
static char fs_lower(char c)
{
    if (c >= 'A' && c <= 'Z') {
        return (char)(c - 'A' + 'a');
    }
    return c;
}

static bool fs_has_ext(const char* name, const char* ext)
{
    uint64_t n = fs_strlen(name);
    uint64_t e = fs_strlen(ext);
    if (n < e) {
        return false;
    }
    const char* p = name + n - e;
    for (uint64_t i = 0; i < e; ++i) {
        if (fs_lower(p[i]) != fs_lower(ext[i])) {
            return false;
        }
    }
    return true;
}

static bool fs_is_executable_name(const char* name)
{
    return fs_has_ext(name, ".axe") || fs_has_ext(name, ".elf") ||
           fs_has_ext(name, ".exe") || fs_has_ext(name, ".com") ||
           fs_has_ext(name, ".bat") || fs_has_ext(name, ".bin") ||
           fs_has_ext(name, ".axb") || fs_has_ext(name, ".axmod");
}

int ax_fs_disk_listdir(const char* path, char* out, uint64_t max)
{
    if (!fs_valid()) {
        return AX_ENOENT;
    }
    if (!out || max == 0) {
        return AX_EINVAL;
    }
    char dir[AXFS_DISK_NAME_MAX];
    int rc = fs_norm(path ? path : "", dir, sizeof(dir));
    if (rc != 0) {
        return rc;
    }
    if (dir[0]) {
        AxfsDiskEntry* d = fs_find(dir);
        if (!d) {
            return AX_ENOENT;
        }
        if (d->type != AXFS_DISK_TYPE_DIR) {
            return AX_EINVAL;
        }
    }

    uint64_t used = 0;
    uint64_t dlen = fs_strlen(dir);

    for (uint32_t i = 0; i < g_super.entry_max; ++i) {
        AxfsDiskEntry* e = &g_entries[i];
        if (e->type == AXFS_DISK_TYPE_FREE) {
            continue;
        }

        const char* leaf = e->name;
        if (dlen > 0) {
            if (!fs_starts_with(e->name, dir) || e->name[dlen] != '/') {
                continue;
            }
            leaf = e->name + dlen + 1;
        }
        if (fs_has_slash(leaf)) {
            continue;               /* pertenece a un subdirectorio */
        }
        if (!leaf[0]) {
            continue;
        }

        char line[128];
        uint64_t n = 0;
        bool exec = (e->type == AXFS_DISK_TYPE_FILE) &&
                    fs_is_executable_name(leaf);
        if (e->type == AXFS_DISK_TYPE_DIR) {
            line[n++] = 'D';
        } else if (exec) {
            line[n++] = 'X'; /* ejecutable */
        } else {
            line[n++] = 'F';
        }
        line[n++] = ' ';
        if (e->type == AXFS_DISK_TYPE_FILE) {
            char digits[24];
            int dn = 0;
            uint64_t v = e->size;
            if (v == 0) {
                digits[dn++] = '0';
            }
            while (v && dn < (int)sizeof(digits)) {
                digits[dn++] = (char)('0' + (v % 10));
                v /= 10;
            }
            while (dn > 0 && n + 1 < sizeof(line)) {
                line[n++] = digits[--dn];
            }
            if (n + 1 < sizeof(line)) {
                line[n++] = ' ';
            }
        }
        uint64_t ln = fs_strlen(leaf);
        for (uint64_t k = 0; k < ln && n + 2 < sizeof(line); ++k) {
            line[n++] = leaf[k];
        }
        line[n++] = '\n';

        if (used + n + 1 > max) {
            break;
        }
        fs_memcpy(out + used, line, n);
        used += n;
    }
    out[used] = '\0';
    return (int)used;
}

int ax_fs_disk_readdir(const char* path, uint32_t index, AxfsDiskDirEntry* out)
{
    if (!fs_valid() || !out) {
        return AX_EINVAL;
    }
    char dir[AXFS_DISK_NAME_MAX];
    int rc = fs_norm(path ? path : "", dir, sizeof(dir));
    if (rc != 0) {
        return rc;
    }
    if (dir[0]) {
        AxfsDiskEntry* d = fs_find(dir);
        if (!d || d->type != AXFS_DISK_TYPE_DIR) {
            return AX_ENOENT;
        }
    }

    uint64_t dlen = fs_strlen(dir);
    uint32_t seen = 0;

    for (uint32_t i = 0; i < g_super.entry_max; ++i) {
        AxfsDiskEntry* e = &g_entries[i];
        if (e->type == AXFS_DISK_TYPE_FREE) {
            continue;
        }

        const char* leaf = e->name;
        if (dlen > 0) {
            if (!fs_starts_with(e->name, dir) || e->name[dlen] != '/') {
                continue;
            }
            leaf = e->name + dlen + 1;
        }
        if (fs_has_slash(leaf) || !leaf[0]) {
            continue;
        }
        if (seen++ != index) {
            continue;
        }

        uint64_t ln = fs_strlen(leaf);
        if (ln >= AXFS_DISK_NAME_MAX) {
            ln = AXFS_DISK_NAME_MAX - 1;
        }
        fs_memzero(out, sizeof(*out));
        fs_memcpy(out->name, leaf, ln);
        out->name[ln] = '\0';
        out->type = e->type;
        out->size = e->size;
        return 0;
    }
    return AX_ENOENT;
}

int ax_fs_disk_list(const char* path, char* out, uint64_t max)
{
    return ax_fs_disk_listdir(path, out, max);
}

uint64_t ax_fs_disk_total_bytes(void)
{
    if (!g_mounted) {
        return 0;
    }
    return g_super.data_blocks * (uint64_t)AXFS_DISK_BLOCK_SIZE;
}

uint64_t ax_fs_disk_free_bytes(void)
{
    if (!g_mounted) {
        return 0;
    }
    return g_super.free_blocks * (uint64_t)AXFS_DISK_BLOCK_SIZE;
}

uint32_t ax_fs_disk_entry_count(void)
{
    if (!g_mounted) {
        return 0;
    }
    return g_super.entry_count;
}
