/*
 * AxolynOS - Fachada C++ de la biblioteca de usuario (libaxos).
 *
 * Este header reune TODA la API de AxolynOS en el namespace `axos`:
 *
 *      #include "axos/axos.hpp"
 *
 *      int main(int argc, char** argv)
 *      {
 *          axos::println("Hola desde AxolynOS");
 *          axos::println_int(axos::uptime_ms());
 *          return 0;
 *      }
 *
 * Convencion de retorno de todas las llamadas: >= 0 exito, < 0 error
 * (-errno, ver axos/abi.h).
 *
 * Organizacion de la fachada:
 *
 *      axos::print / println / print_int ...   consola (fd 1)
 *      axos::fs::open / read / write / close   ficheros
 *      axos::File                              fichero con RAII
 *      axos::Process                           procesos hijos
 *      axos::ipc::send / recv                  mensajes entre procesos
 *      axos::Module                            modulos del kernel
 *      axos::mem::info / heap                  memoria fisica y heap
 *      axos::system::info / threads            estado del kernel
 *      axos::Vm::run / status / stop           maquina virtual Axolyn
 *      axos::to_string / axos::String          texto (no hay STL)
 */
#ifndef AXOS_HPP
#define AXOS_HPP

#include "axos/abi.h"
#include "axos/syscall.h"
#include "axos/string.h"
#include "axos/console.h"
#include "axos/process.h"
#include "axos/fs.h"
#include "axos/ipc.h"
#include "axos/module.h"
#include "axos/vm.h"
#include "axos/mem.h"
#include "axos/time.h"

namespace axos {

/* ================================================================== */
/* Cadenas: sustituyen a std::string (no hay STL ni heap en user mode) */
/* ================================================================== */

/*
 * Cadena de texto con buffer interno fijo (kCapacity bytes utiles).
 * Se copia por valor (no usa memoria dinamica) y se puede concatenar con
 * el operador '+', igual que haria std::string:
 *
 *      axos::println("codigo de salida: " + axos::to_string(status));
 */
class String {
public:
    static const unsigned long kCapacity = 256; /* utiles, sin contar el '\0' */

    String() noexcept
    {
        buf_[0] = '\0';
        len_    = 0;
    }

    String(const char* text) noexcept
    {
        assign(text);
    }

    String(const String& other) noexcept
    {
        assign(other.buf_);
    }

    String& operator=(const String& other) noexcept
    {
        if (this != &other) {
            assign(other.buf_);
        }
        return *this;
    }

    String& operator=(const char* text) noexcept
    {
        assign(text);
        return *this;
    }

    ~String() noexcept {}

    void clear() noexcept
    {
        buf_[0] = '\0';
        len_    = 0;
    }

    String& append(const char* text) noexcept
    {
        if (!text) {
            return *this;
        }
        while (*text != '\0' && len_ + 1 < kCapacity) {
            buf_[len_++] = *text++;
        }
        buf_[len_] = '\0';
        return *this;
    }

    String& append(char ch) noexcept
    {
        if (len_ + 1 < kCapacity) {
            buf_[len_++] = ch;
            buf_[len_]   = '\0';
        }
        return *this;
    }

    String& append(long value) noexcept
    {
        char tmp[32];
        long n = ax_itoa(value, tmp, sizeof(tmp));
        for (long i = 0; i < n && len_ + 1 < kCapacity; ++i) {
            buf_[len_++] = tmp[i];
        }
        buf_[len_] = '\0';
        return *this;
    }

    String& append_hex(unsigned long value) noexcept
    {
        char tmp[32];
        long n = ax_utoa_hex(value, tmp, sizeof(tmp));
        for (long i = 0; i < n && len_ + 1 < kCapacity; ++i) {
            buf_[len_++] = tmp[i];
        }
        buf_[len_] = '\0';
        return *this;
    }

    String& operator+=(const String& other) noexcept
    {
        return append(other.buf_);
    }
    String& operator+=(const char* text) noexcept { return append(text); }
    String& operator+=(char ch) noexcept { return append(ch); }
    String& operator+=(long value) noexcept { return append(value); }

    const char*   c_str()  const noexcept { return buf_; }
    unsigned long size()   const noexcept { return len_; }
    unsigned long length() const noexcept { return len_; }
    bool          empty()  const noexcept { return len_ == 0; }

private:
    void assign(const char* text) noexcept
    {
        len_ = 0;
        if (!text) {
            buf_[0] = '\0';
            return;
        }
        while (*text != '\0' && len_ + 1 < kCapacity) {
            buf_[len_++] = *text++;
        }
        buf_[len_] = '\0';
    }

    char          buf_[kCapacity + 1];
    unsigned long len_;
};

inline String operator+(const String& a, const String& b) noexcept
{
    String r(a);
    r += b;
    return r;
}

inline String operator+(const String& a, const char* b) noexcept
{
    String r(a);
    r += b;
    return r;
}

inline String operator+(const char* a, const String& b) noexcept
{
    String r(a);
    r += b;
    return r;
}

inline bool operator==(const String& a, const String& b) noexcept
{
    return ax_strcmp(a.c_str(), b.c_str()) == 0;
}

inline bool operator!=(const String& a, const String& b) noexcept
{
    return ax_strcmp(a.c_str(), b.c_str()) != 0;
}

/* Convierte un entero con signo en texto decimal. */
inline String to_string(long value) noexcept
{
    String s;
    s.append(value);
    return s;
}

/* Convierte un entero sin signo en texto hexadecimal (0x...). */
inline String to_hex(unsigned long value) noexcept
{
    String s;
    s.append_hex(value);
    return s;
}

/* ================================================================== */
/* Consola                                                            */
/* ================================================================== */

/* Imprime una cadena en la salida estandar (fd 1). */
inline void print(const char* text) noexcept
{
    ax_console_print(text);
}

/* Imprime una cadena (tipo axos::String) en la salida estandar. */
inline void print(const String& text) noexcept
{
    ax_console_print(text.c_str());
}

/* Imprime una cadena y un salto de linea. */
inline void println(const char* text) noexcept
{
    ax_console_println(text);
}

/* Imprime una cadena (tipo axos::String) y un salto de linea. */
inline void println(const String& text) noexcept
{
    ax_console_println(text.c_str());
}

/* Imprime un entero con signo (sin salto de linea). */
inline void print_int(long value) noexcept
{
    ax_console_print_int(value);
}

/* Imprime un entero con signo y un salto de linea. */
inline void println_int(long value) noexcept
{
    ax_console_println_int(value);
}

/* Imprime un entero sin signo (sin salto de linea). */
inline void print_uint(unsigned long value) noexcept
{
    ax_console_print_uint(value);
}

/* Imprime un entero sin signo y un salto de linea. */
inline void println_uint(unsigned long value) noexcept
{
    ax_console_println_uint(value);
}

/* Imprime un entero en hexadecimal (0x..., sin salto de linea). */
inline void print_hex(unsigned long value) noexcept
{
    ax_console_print_hex(value);
}

/* Imprime un entero en hexadecimal y un salto de linea. */
inline void println_hex(unsigned long value) noexcept
{
    ax_console_println_hex(value);
}

/* Sobrecargas para enteros con tipo concreto (comodo con estructuras). */
inline void print(int value) noexcept { ax_console_print_int(value); }
inline void print(long value) noexcept { ax_console_print_int(value); }
inline void print(long long value) noexcept { ax_console_print_int((long)value); }
inline void print(unsigned int value) noexcept { ax_console_print_uint(value); }
inline void print(unsigned long value) noexcept { ax_console_print_uint(value); }
inline void print(unsigned long long value) noexcept { ax_console_print_uint((unsigned long)value); }

inline void println(int value) noexcept { ax_console_println_int(value); }
inline void println(long value) noexcept { ax_console_println_int(value); }
inline void println(long long value) noexcept { ax_console_println_int((long)value); }
inline void println(unsigned int value) noexcept { ax_console_println_uint(value); }
inline void println(unsigned long value) noexcept { ax_console_println_uint(value); }
inline void println(unsigned long long value) noexcept { ax_console_println_uint((unsigned long)value); }

/* Solo un salto de linea. */
inline void newline() noexcept
{
    ax_console_newline();
}

/* Escribe bytes crudos en un descriptor. */
inline long write(int fd, const void* buf, unsigned long len) noexcept
{
    return ax_console_write(fd, buf, len);
}

/* Lee bytes crudos de un descriptor. */
inline long read(int fd, void* buf, unsigned long len) noexcept
{
    return ax_syscall3(AX_SYS_READ, fd, (long)buf, (long)len);
}

/* Imprime "mensaje: error N" en la salida de error (fd 2). */
inline void print_error(const char* what, long err) noexcept
{
    ax_console_print(what);
    ax_console_print(": error ");
    ax_console_print_int(-err);
    ax_console_newline();
}

/* ================================================================== */
/* Ficheros                                                           */
/* ================================================================== */

namespace fs {

/* Abre un fichero y devuelve el descriptor (>= 0) o -errno. */
inline long open(const char* path, int flags = AX_O_RDONLY) noexcept
{
    return ax_fs_open(path, flags);
}

/* Abre un fichero con RAII (cierra el descriptor al destruirse el objeto). */
inline File open_file(const char* path, int flags = AX_O_RDONLY) noexcept
{
    return File(path, flags);
}

/* Lee bytes de un descriptor. */
inline long read(int fd, void* buf, unsigned long len) noexcept
{
    return ax_fs_read(fd, buf, len);
}

/* Escribe bytes en un descriptor. */
inline long write(int fd, const void* buf, unsigned long len) noexcept
{
    return ax_fs_write(fd, buf, len);
}

/* Cierra un descriptor. */
inline long close(int fd) noexcept
{
    return ax_fs_close(fd);
}

/* Reposiciona un descriptor. */
inline long lseek(int fd, long offset, int whence = AX_SEEK_SET) noexcept
{
    return ax_fs_lseek(fd, offset, whence);
}

/* Lee una linea de texto de un descriptor. */
inline long gets(char* buf, unsigned long max) noexcept
{
    return ax_fs_gets(buf, max);
}

/* Escribe una cadena terminada en '\0' en un descriptor. */
inline long puts(int fd, const char* text) noexcept
{
    return ax_fs_puts(fd, text);
}

} /* namespace fs */

/* Versiones planas de la API de ficheros (atajo sin espacio de nombres). */
inline long open(const char* path, int flags = AX_O_RDONLY) noexcept
{
    return ax_fs_open(path, flags);
}

/* Lee bytes de un descriptor. */
inline long read_file(int fd, void* buf, unsigned long len) noexcept
{
    return ax_fs_read(fd, buf, len);
}

/* Escribe bytes en un descriptor. */
inline long write_file(int fd, const void* buf, unsigned long len) noexcept
{
    return ax_fs_write(fd, buf, len);
}

/* Cierra un descriptor. */
inline long close(int fd) noexcept
{
    return ax_fs_close(fd);
}

/* Reposiciona un descriptor. */
inline long lseek(int fd, long offset, int whence = AX_SEEK_SET) noexcept
{
    return ax_fs_lseek(fd, offset, whence);
}

/* Lee una linea de texto de un descriptor. */
inline long gets(char* buf, unsigned long max) noexcept
{
    return ax_fs_gets(buf, max);
}

/* ================================================================== */
/* Procesos                                                           */
/* ================================================================== */

/*
 * Process: envoltorio de un proceso hijo.
 *
 *  - Se construye con Process::spawn(path).
 *  - wait() espera a que termine y devuelve su codigo de salida.
 *  - Es movible (no copiable) para no esperar dos veces al mismo proceso.
 */
class Process {
public:
    Process() noexcept : pid_(-1), error_(0) {}

    static Process spawn(const char* path, char* const argv[] = nullptr, long argc = 0) noexcept
    {
        Process p;
        long   r = ax_proc_spawn(path, argv, argc);
        if (r < 0) {
            p.error_ = r;
            p.pid_   = -1;
        } else {
            p.pid_ = r;
        }
        return p;
    }

    Process(const Process&)            = delete;
    Process& operator=(const Process&) = delete;

    Process(Process&& other) noexcept : pid_(other.pid_), error_(other.error_)
    {
        other.pid_ = -1;
    }

    Process& operator=(Process&& other) noexcept
    {
        if (this != &other) {
            pid_        = other.pid_;
            error_      = other.error_;
            other.pid_  = -1;
            other.error_ = 0;
        }
        return *this;
    }

    ~Process() noexcept {}

    /* PID del proceso (negativo si el spawn fallo). */
    long pid() const noexcept { return pid_; }

    /* true si el proceso se lanzo correctamente. */
    bool ok() const noexcept { return pid_ >= 0; }

    explicit operator bool() const noexcept { return pid_ >= 0; }

    /* Codigo de error (-errno) del spawn, o 0 si fue correcto. */
    long error() const noexcept { return error_; }

    /* Espera a que termine y devuelve su codigo de salida. */
    long wait() noexcept
    {
        if (pid_ < 0) {
            return AX_EINVAL;
        }
        return ax_proc_wait(pid_);
    }

    /* Codigo de salida sin esperar. */
    long exit_code() const noexcept
    {
        if (pid_ < 0) {
            return AX_EINVAL;
        }
        return ax_proc_exit_code(pid_);
    }

    /* Envia una senal de terminacion. */
    long kill() noexcept
    {
        if (pid_ < 0) {
            return AX_EINVAL;
        }
        return ax_proc_kill(pid_);
    }

private:
    long pid_;
    long error_;
};

namespace proc {

/* Lanza otra aplicacion; devuelve el PID del proceso nuevo. */
inline long spawn(const char* path, char* const argv[] = nullptr, long argc = 0) noexcept
{
    return ax_proc_spawn(path, argv, argc);
}

/* Espera a un proceso y devuelve su codigo de salida. */
inline long wait(long process) noexcept
{
    return ax_proc_wait(process);
}

/* Codigo de salida de un proceso (sin esperar). */
inline long exit_code(long process) noexcept
{
    return ax_proc_exit_code(process);
}

/* Termina un proceso. */
inline long kill(long process) noexcept
{
    return ax_proc_kill(process);
}

/* PID del proceso actual. */
inline long pid() noexcept
{
    return ax_proc_getpid();
}

} /* namespace proc */

/* PID del proceso actual. */
inline long pid() noexcept
{
    return ax_proc_getpid();
}

/* Lanza otra aplicacion; devuelve el PID del proceso nuevo. */
inline long spawn(const char* path, char* const argv[] = nullptr, long argc = 0) noexcept
{
    return ax_proc_spawn(path, argv, argc);
}

/* Espera a un proceso y devuelve su codigo de salida. */
inline long wait(long process) noexcept
{
    return ax_proc_wait(process);
}

/* Codigo de salida de un proceso (sin esperar). */
inline long exit_code(long process) noexcept
{
    return ax_proc_exit_code(process);
}

/* Termina un proceso. */
inline long kill(long process) noexcept
{
    return ax_proc_kill(process);
}

/* Cede el CPU al planificador. */
inline long yield() noexcept
{
    return ax_proc_yield();
}

/* Termina el proceso actual con el codigo indicado (no retorna). */
AX_NORETURN inline void exit_app(int code) noexcept
{
    ax_proc_exit(code);
}

/* ================================================================== */
/* IPC                                                                */
/* ================================================================== */

/*
 * IpcMessage: mensaje para un puerto de IPC.
 *
 *   msg.port    = 7;            // puerto de destino
 *   msg.data    = "hola";       // contenido
 *   msg.length  = 4;            // opcional: si es 0 se calcula con strlen
 */
struct IpcMessage {
    int          port;
    const char*  data;
    unsigned long length;

    IpcMessage() noexcept : port(0), data(nullptr), length(0) {}
};

namespace ipc {

/* Envia un mensaje (si length es 0 se usa strlen(data)). */
inline long send(const IpcMessage& msg) noexcept
{
    if (msg.port <= 0) {
        return AX_EINVAL;
    }
    const char*  text = msg.data ? msg.data : "";
    unsigned long len  = msg.length ? msg.length : ax_strlen(text);
    return ax_ipc_send(msg.port, text, len);
}

/* Envia bytes crudos a un puerto. */
inline long send(int port, const void* buf, unsigned long len) noexcept
{
    return ax_ipc_send(port, buf, len);
}

/* Recibe un mensaje de un puerto. */
inline long recv(int port, void* buf, unsigned long max) noexcept
{
    return ax_ipc_recv(port, buf, max);
}

} /* namespace ipc */

/* Envia un mensaje a un puerto. */
inline long ipc_send(int port, const void* buf, unsigned long len) noexcept
{
    return ax_ipc_send(port, buf, len);
}

/* Recibe un mensaje de un puerto. */
inline long ipc_recv(int port, void* buf, unsigned long max) noexcept
{
    return ax_ipc_recv(port, buf, max);
}

/* ================================================================== */
/* Modulos del kernel                                                 */
/* ================================================================== */

/*
 * Module: envoltorio de un modulo del kernel.
 *
 *   axos::Module::load("axmem");      // carga por nombre
 *   axos::Module::list(buffer, 16);   // lista los modulos cargados
 */
class Module {
public:
    Module() noexcept : index_(-1), error_(0) {}

    /* Carga un modulo por su nombre. */
    static Module load(const char* name) noexcept
    {
        Module m;
        long   r = ax_mod_load(name);
        if (r < 0) {
            m.error_ = r;
        }
        return m;
    }

    /* Escribe la informacion de los modulos cargados en `out`. */
    static long list(AxModInfo* out, unsigned long max) noexcept
    {
        return ax_mod_list(out, max);
    }

    /* Numero de modulos cargados. */
    static long count() noexcept
    {
        static AxModInfo buffer[64];
        return ax_mod_list(buffer, 64);
    }

    /* Descarga el modulo indicado por su indice. */
    static long unload(unsigned int index) noexcept
    {
        return ax_mod_unload(index);
    }

    /* Descarga este modulo (si se conoce su indice). */
    long unload() noexcept
    {
        if (index_ < 0) {
            return AX_EINVAL;
        }
        return ax_mod_unload((unsigned int)index_);
    }

    long index() const noexcept { return index_; }
    bool ok() const noexcept { return error_ == 0; }
    long error() const noexcept { return error_; }

private:
    long index_;
    long error_;
};

/* Lista los modulos cargados; devuelve cuantos se escribieron. */
inline long modules(AxModInfo* out, unsigned long max) noexcept
{
    return ax_mod_list(out, max);
}

/* Carga un modulo por su nombre. */
inline long load_module(const char* name) noexcept
{
    return ax_mod_load(name);
}

/* Descarga un modulo por su indice. */
inline long unload_module(unsigned int index) noexcept
{
    return ax_mod_unload(index);
}

/* ================================================================== */
/* Memoria                                                            */
/* ================================================================== */

/* Instantanea de la informacion de memoria fisica. */
struct MemInfo {
    ax_u64 total_phys;
    ax_u64 free_phys;
    ax_u64 total_pages;
    ax_u64 free_pages;
    ax_u64 reserved_pages;
    ax_u64 kernel_virt;
    ax_u64 heap_used;
    ax_u64 heap_total;
    ax_u64 low_mapped;
    long   error; /* 0 si la llamada fue correcta */
};

/* Instantanea de la informacion del heap del kernel. */
struct HeapInfo {
    ax_u64 used;
    ax_u64 total;
    ax_u64 blocks;
    ax_u64 free_blocks;
    long   error;
};

namespace mem {

/* Informacion del gestor de memoria fisico. */
inline MemInfo info() noexcept
{
    MemInfo   m{};
    AxMemInfo raw{};
    long      r = ax_mem_info(&raw);
    m.error = r;
    if (r >= 0) {
        m.total_phys     = raw.total_phys;
        m.free_phys      = raw.free_phys;
        m.total_pages    = raw.total_pages;
        m.free_pages     = raw.free_pages;
        m.reserved_pages = raw.reserved_pages;
        m.kernel_virt    = raw.kernel_virt;
        m.heap_used      = raw.heap_used;
        m.heap_total     = raw.heap_total;
        m.low_mapped     = raw.low_mapped;
    }
    return m;
}

/* Informacion del heap del kernel. */
inline HeapInfo heap() noexcept
{
    HeapInfo   h{};
    AxHeapInfo raw{};
    long       r = ax_heap_info(&raw);
    h.error = r;
    if (r >= 0) {
        h.used        = raw.used;
        h.total       = raw.total;
        h.blocks      = raw.blocks;
        h.free_blocks = raw.free_blocks;
    }
    return h;
}

} /* namespace mem */

/* Informacion del gestor de memoria fisico. */
inline long mem_info(AxMemInfo* out) noexcept
{
    return ax_mem_info(out);
}

/* Informacion del heap del kernel. */
inline long heap_info(AxHeapInfo* out) noexcept
{
    return ax_heap_info(out);
}

/* ================================================================== */
/* Informacion del sistema                                            */
/* ================================================================== */

/* Instantanea del estado general del kernel. */
struct SysInfo {
    ax_u64 uptime_ms;
    ax_u64 unix_ms;
    ax_u32 cpu_count;
    ax_u32 thread_count;
    char   kernel_version[16];
    char   kernel_name[24];
    ax_u64 total_phys;
    ax_u64 free_phys;
    long   error;
};

namespace system {

/* Informacion general del kernel (AX_SYS_SYSINFO). */
inline SysInfo info() noexcept
{
    SysInfo   s{};
    AxSysInfo raw{};
    long      r = ax_syscall1(AX_SYS_SYSINFO, (long)&raw);
    s.error = r;
    if (r >= 0) {
        s.uptime_ms     = raw.uptime_ms;
        s.unix_ms       = raw.unix_ms;
        s.cpu_count     = raw.cpu_count;
        s.thread_count  = raw.thread_count;
        s.total_phys    = raw.total_phys;
        s.free_phys     = raw.free_phys;
        for (int i = 0; i < 16 && i < (int)sizeof(raw.kernel_version); ++i) {
            s.kernel_version[i] = raw.kernel_version[i];
        }
        for (int i = 0; i < 24 && i < (int)sizeof(raw.kernel_name); ++i) {
            s.kernel_name[i] = raw.kernel_name[i];
        }
    }
    return s;
}

/* Lista los hilos del kernel (AX_SYS_THREADS). */
inline long threads(AxThreadInfo* out, unsigned long max) noexcept
{
    return ax_syscall2(AX_SYS_THREADS, (long)out, (long)max);
}

/* Reinicia el sistema (AX_SYS_REBOOT). */
inline long reboot() noexcept
{
    return ax_syscall0(AX_SYS_REBOOT);
}

} /* namespace system */

/* Informacion general del kernel (AX_SYS_SYSINFO). */
inline long sys_info(AxSysInfo* out) noexcept
{
    return ax_syscall1(AX_SYS_SYSINFO, (long)out);
}

/* Lista los hilos del kernel (AX_SYS_THREADS). */
inline long threads(AxThreadInfo* out, unsigned long max) noexcept
{
    return ax_syscall2(AX_SYS_THREADS, (long)out, (long)max);
}

/* Reinicia el sistema (AX_SYS_REBOOT). */
inline long reboot() noexcept
{
    return ax_syscall0(AX_SYS_REBOOT);
}

/* ================================================================== */
/* Maquina virtual de bytecode (Axolyn)                               */
/* ================================================================== */

/* Instantanea del estado de una VM. */
struct VmInfo {
    ax_u64 uptime_ms;
    ax_u32 pid;
    ax_u32 state; /* 0 = detenido, 1 = ejecutando */
    ax_u32 instructions;
    char   name[AX_NAME_LEN];
    long   error;
};

class Vm {
public:
    /* Ejecuta una aplicacion .axb; devuelve el PID de la VM. */
    static long run(const char* path, char* const argv[] = nullptr) noexcept
    {
        return ax_vm_run(path, argv);
    }

    /* Detiene la VM indicada. */
    static long stop(long pid) noexcept
    {
        return ax_vm_stop(pid);
    }

    /*
     * Estado de una VM. El kernelfills la informacion de la VM que
     * corresponde a este proceso (AX_SYS_VMSTATUS no recibe el pid).
     */
    static VmInfo status(long pid) noexcept
    {
        (void)pid;
        VmInfo   v{};
        AxVmInfo raw{};
        long     r = ax_vm_status(&raw);
        v.error = r;
        if (r >= 0) {
            v.uptime_ms    = raw.uptime_ms;
            v.pid          = raw.pid;
            v.state        = raw.state;
            v.instructions = raw.instructions;
            for (int i = 0; i < AX_NAME_LEN; ++i) {
                v.name[i] = raw.name[i];
            }
        }
        return v;
    }
};

/* Ejecuta una aplicacion .axb; devuelve el PID de la VM. */
inline long vm_run(const char* path, char* const argv[] = nullptr) noexcept
{
    return ax_vm_run(path, argv);
}

/* Detiene una VM. */
inline long vm_stop(long process) noexcept
{
    return ax_vm_stop(process);
}

/* Estado de una VM. */
inline long vm_status(AxVmInfo* out) noexcept
{
    return ax_vm_status(out);
}

/* ================================================================== */
/* Tiempo                                                             */
/* ================================================================== */

/* Milisegundos de uptime. */
inline long uptime_ms() noexcept
{
    return ax_time_uptime_ms();
}

/* Duerme `ms` milisegundos. */
inline long sleep_ms(unsigned long ms) noexcept
{
    return ax_sleep_ms(ms);
}

/* ================================================================== */
/* Cadenas y memoria (utilidades propias, sin libc)                    */
/* ================================================================== */

inline unsigned long strlen(const char* s) noexcept { return ax_strlen(s); }
inline int   strcmp(const char* a, const char* b) noexcept { return ax_strcmp(a, b); }
inline char* strcpy(char* dst, const char* src) noexcept { return ax_strcpy(dst, src); }
inline void* memcpy(void* dst, const void* src, unsigned long n) noexcept { return ax_memcpy(dst, src, n); }
inline void* memset(void* dst, int value, unsigned long n) noexcept { return ax_memset(dst, value, n); }

} /* namespace axos */

#endif /* AXOS_HPP */
