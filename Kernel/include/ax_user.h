/*
 * AxolynOS - Sistema de usuarios, sesiones y permisos.
 *
 * La base de datos de usuarios se guarda cifrada en AxFS-D
 * (/etc/users.db).  Cada cuenta tiene su propia sal, nonce y etiqueta
 * HMAC-SHA256.  La contrasena nunca se guarda en claro: se deriva una
 * clave con PBKDF2-HMAC-SHA256 y con ella se descifra el registro de la
 * cuenta.  El registro solo se considera valido si la etiqueta HMAC
 * coincide y el nombre descifrado coincide con el indice.
 *
 * Este fichero define la API que usa el resto del kernel (shell,
 * syscalls, servicios).  La implementacion esta en Kernel/src/user.cpp.
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

#define AX_USER_DB_MAGIC     "AXUDB002"
#define AX_USER_DB_VERSION   2u
#define AX_USER_SALT_LEN     16u
#define AX_USER_NONCE_LEN    12u
#define AX_USER_TAG_LEN      32u
#define AX_USER_ITERATIONS   4096u
#define AX_USER_SECRET_LEN   64u

/*
 * Representacion en RAM de una cuenta.  "password" ya NO contiene la
 * contrasena: guarda la etiqueta HMAC del registro (32 bytes) y el resto
 * se deja a cero.  Se conserva el nombre del campo por compatibilidad con
 * codigo antiguo que solo lo copiaba.
 */
typedef struct {
    uint32_t uid;
    uint32_t gid;
    char     username[AX_USER_MAX_NAME];
    uint8_t  password[AX_USER_MAX_PASS];
    uint8_t  salt[AX_USER_SALT_LEN];
    uint8_t  nonce[AX_USER_NONCE_LEN];
    uint8_t  tag[AX_USER_TAG_LEN];
    uint32_t iterations;
    bool     active;
    bool     is_admin;
    bool     legacy;
    uint32_t legacy_hash;
    uint32_t perms;
} AxUser;

/* Estructura compartida con user mode (AxUserInfo en axos/abi.h). */
typedef struct {
    uint32_t uid;
    uint32_t gid;
    uint32_t flags;       /* bit 0 = admin, bit 1 = sesion actual */
    char     name[AX_USER_MAX_NAME];
} AxUserInfoK;

void ax_user_init(void);
int  ax_user_login(const char* username, const char* password);
int  ax_user_logout(void);
int  ax_user_add(const char* username, const char* password, bool is_admin);
int  ax_user_del(const char* username);
int  ax_user_list(char* out, uint64_t max);
int  ax_user_count(void);
AxUser* ax_user_current(void);
uint32_t ax_user_getuid(void);
bool ax_user_is_root(void);
bool ax_user_check_perm(const char* path, uint32_t need_perm);

/* Persistencia cifrada en AxFS-D. */
void ax_user_boot(void);
bool ax_user_needs_setup(void);
int  ax_user_setup(const char* username, const char* password);
int  ax_user_save(void);

/* Cambio de contrasena: verifica la actual y re-cifra el registro. */
int  ax_user_change_password(const char* username, const char* old_password,
                             const char* new_password);

/* Verificacion sin cambiar la sesion actual. */
int  ax_user_verify(const char* username, const char* password);

/* Enumeracion para `users`, `passwd`, syscalls y regedit. */
int  ax_user_get_info(uint32_t index, AxUserInfoK* out);
uint32_t ax_user_uid_for_name(const char* username);
bool ax_user_is_admin_name(const char* username);

#ifdef __cplusplus
}
#endif

#endif /* AX_USER_H */
