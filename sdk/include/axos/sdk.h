/*
 * AxolynOS SDK - API para programar aplicaciones, servicios y módulos
 */
#ifndef AXOS_SDK_H
#define AXOS_SDK_H

#include "axos/abi.h"
#include "axos/syscall.h"
#include "axos/console.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Entrada de aplicación */
int main(int argc, char** argv);

/* Utilidades del SDK */
void sdk_init(void);
int  sdk_version(void);

/* Gestión de energía */
int  sdk_poweroff(void);
int  sdk_reboot(void);
int  sdk_suspend(void);
void sdk_halt(void);

/* Acceso a disco */
int sdk_disk_read(ax_u64 lba, void* buf, ax_u32 count);
int sdk_disk_write(ax_u64 lba, const void* buf, ax_u32 count);

/* Información del sistema */
int sdk_sysinfo(AxSysInfo* info);
int sdk_meminfo(AxMemInfo* info);

/* Servicios del sistema */
int sdk_service_start(const char* name);
int sdk_service_stop(const char* name);
int sdk_service_list(AxServiceInfo* info, uint32_t max_me me me me me me me me me me me me me me me me);

/* Módulos del kernel */
int sdk_module_load(const char* name);
int sdk_module_unload(unsigned int index);
int sdk_module_list(AxModInfo* out, uint32_t max_me me me me me me me me);

/* Nuevas funcionalidades del SDK */

/* Graficos */
int sdk_gfx_init(int width, int height);
void sdk_gfx_pixel(int x, int y, int r, int g, int b);
void sdk_gfx_line(int x1, int y1, int x2, int y2, int r, int g, int b);
void sdk_gfx_rect(int x, int y, int w, int h, int r, int g, int b);
void sdk_gfx_clear(void);

/* Manejo de archivos */
int sdk_file_exists(const char* path);
int sdk_file_read(const char* path, void* buf, ax_u32 max_size);
int sdk_file_write(const char* path, const void* buf, ax_u32 size);
int sdk_file_delete(const char* path);

/* Manejo de directorios */
int sdk_dir_exists(const char* path);
int sdk_dir_create(const char* path);
int sdk_dir_delete(const char* path);
int sdk_dir_list(const char* path, char** entries, ax_u32 max_entries);

/* Encriptación de datos */
int sdk_encrypt_data(const void* input, ax_u32 input_size, void* output, ax_u32* output_size);
int sdk_decrypt_data(const void* input, ax_u32 input_size, void* output, ax_u32* output_size);

/* Manejo de Usuarios */
int sdk_user_create(const char* username, const char* password);
int sdk_user_exists(const char* username);
int sdk_user_delete(const char* username);
int sdk_user_list(char** usernames, ax_u32* count);

/* Manejo del compilador */
int sdk_compiler_add_native(const char* name, int id, int (*func)(uint64_t*, uint64_t*));
int sdk_compiler_remove_native(const char* name);
int sdk_compiler_info(void);

/* Tipos de datos basicos */
int sdk_tipo_entero(int valor);
int sdk_tipo_flotante(float valor);
int sdk_tipo_doble(double valor);
int sdk_tipo_bool(int valor);
int sdk_tipo_string(const char* str);

/* Recolector de basura */
void sdk_gc(void);
int sdk_gc_generation(int gen);

#ifdef __cplusplus
}
#endif

#endif /* AXOS_SDK_H */