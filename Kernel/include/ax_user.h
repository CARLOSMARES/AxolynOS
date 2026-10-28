/*
 * AxolynOS - Sistema de usuarios y permisos
 */
#ifndef AX_USER_H
#define AX_USER_H

#include "ax_types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define AX_USER_MAX_NAME 32
#define AX_USER_MAX_PASS 64
#define AX_USER_MAX_USERS 16
#define AX_USER_ROOT_UID 0
#define AX_USER_GUEST_UID 1000

#define AX_PERM_READ  0x04
#define AX_PERM_WRITE 0x02
#define AX_PERM_EXEC  0x01
#define AX_PERM_ALL   0x07

typedef struct {
    uint32_t uid;
    uint32_t gid;
    char username[AX_USER_MAX_NAME];
    char password[AX_USER_MAX_PASS];
    bool active;
    bool is_admin;
    uint32_t perms;
} AxUser;

void ax_user_init(void);
int ax_user_login(const char* username, const char* password);
int ax_user_logout(void);
int ax_user_add(const char* username, const char* password, bool is_admin);
int ax_user_del(const char* username);
int ax_user_list(char* out, uint64_t max);
int ax_user_count(void);
AxUser* ax_user_current(void);
uint32_t ax_user_getuid(void);
bool ax_user_is_root(void);
bool ax_user_check_perm(const char* path, uint32_t need_perm);

/*
 * Persistencia en AxFS-D (/etc/passwd).
 *
 * ax_user_boot() se llama despues de montar el disco: carga la base de
 * usuarios si existe.  Si no existe, ax_user_needs_setup() devuelve true y
 * la shell pide crear la primera cuenta.
 */
void ax_user_boot(void);
bool ax_user_needs_setup(void);
int  ax_user_setup(const char* username, const char* password);
int  ax_user_save(void);

#ifdef __cplusplus
}
#endif

#endif /* AX_USER_H */
