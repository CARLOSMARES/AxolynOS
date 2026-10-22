/*
 * AxolynOS - API de la maquina virtual de bytecode Axolyn (AXB).
 *
 * Syscalls usadas (ver axos/abi.h):
 *
 *      AX_SYS_VMRUN    (17) rdi=ruta rsi=argv -> pid de la VM
 *      AX_SYS_VMSTOP   (18) rdi=pid
 *      AX_SYS_VMSTATUS (27) rdi=AxVmInfo*
 *
 * Convencion de retorno: >= 0 exito, < 0 error (-errno).
 */
#ifndef AXOS_VM_H
#define AXOS_VM_H

#include "axos/abi.h"
#include "axos/syscall.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Ejecuta una aplicacion escrita en lenguaje Axolyn (bytecode AXB).
 *   path : ruta del fichero .axb dentro del sistema de ficheros.
 *   argv : array de punteros a cadenas terminado en NULL (puede ser NULL).
 * Devuelve el PID del proceso de la VM o -errno.
 */
long ax_vm_run(const char* path, char* const argv[]);

/* Detiene la VM indicada. Devuelve 0 o -errno. */
long ax_vm_stop(long pid);

/*
 * Consulta el estado de una VM (uptime, pid, estado, instrucciones, nombre).
 * Devuelve 0 o -errno.
 */
long ax_vm_status(AxVmInfo* out);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* AXOS_VM_H */
