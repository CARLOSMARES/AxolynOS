/*
 * AxolynOS - Runner de host para las imagenes .axb.
 *
 *   axrun <programa.axb> [argumentos...]
 *
 * Usa el MISMO fuente de la maquina virtual que el kernel
 * (Kernel/vm/axvm.cpp), de modo que una prueba que pasa en el host se
 * comporta igual dentro del sistema.
 */
#include "axvm.h"
#include "axbytecode.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#if defined(_WIN32)
#include <process.h>
#define axGetPid() (unsigned)_getpid()
#else
#include <unistd.h>
#define axGetPid() (unsigned)getpid()
#endif

/* ------------------------------------------------------------------ */
/* Reloj para la VM (enlace debil en axvm.cpp)                         */
/* ------------------------------------------------------------------ */
extern "C" uint64_t axvm_host_millis(void) {
    using namespace std::chrono;
    return (uint64_t)std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

namespace {

/* ------------------------------------------------------------------ */
/* Utilidades de memoria                                               */
/* ------------------------------------------------------------------ */
void* hostAlloc(void* ctx, uint64_t size) {
    (void)ctx;
    if (size == 0) size = 1;
    return std::malloc((size_t)size);
}

void hostFree(void* ctx, void* ptr) {
    (void)ctx;
    if (ptr) std::free(ptr);
}

void hostPrint(void* ctx, const char* s, uint64_t len) {
    (void)ctx;
    if (!s || len == 0) return;
    std::fwrite(s, 1, (size_t)len, stdout);
    std::fflush(stdout);
}

/* ------------------------------------------------------------------ */
/* Nativas del host                                                    */
/* ------------------------------------------------------------------ */
struct RunnerState {
    int exitCode = 0;
    bool exited = false;
};

const char* asString(ax_val_t v) {
    return (const char*)(uintptr_t)v;
}

int nativeCall(void* ctx, uint32_t id, ax_val_t* args, uint32_t argc, ax_val_t* ret) {
    RunnerState* state = (RunnerState*)ctx;
    if (ret) *ret = 0;

    switch (id) {
        /* --- core --- */
        case AX_NT_CORE_PRINT: {
            if (argc < 1) return -1;
            hostPrint(ctx, asString(args[0]), std::strlen(asString(args[0])));
            return 0;
        }
        case AX_NT_CORE_PRINTLN: {
            if (argc < 1) return -1;
            hostPrint(ctx, asString(args[0]), std::strlen(asString(args[0])));
            hostPrint(ctx, "\n", 1);
            return 0;
        }
        case AX_NT_CORE_PRINT_INT: {
            if (argc < 1) return -1;
            std::printf("%lld", (long long)(int64_t)args[0]);
            std::fflush(stdout);
            return 0;
        }
        case AX_NT_CORE_STRLEN: {
            if (argc < 1) return -1;
            if (ret) *ret = (ax_val_t)std::strlen(asString(args[0]));
            return 0;
        }
        case AX_NT_CORE_INT_TO_STR: {
            static char buffers[8][32];
            static int next = 0;
            if (argc < 1) return -1;
            char* buf = buffers[next];
            next = (next + 1) % 8;
            std::snprintf(buf, sizeof(buffers[0]), "%lld", (long long)(int64_t)args[0]);
            if (ret) *ret = (ax_val_t)(uintptr_t)buf;
            return 0;
        }

        /* --- console --- */
        case AX_NT_CONSOLE_WRITE: {
            if (argc < 1) return -1;
            hostPrint(ctx, asString(args[0]), std::strlen(asString(args[0])));
            return 0;
        }
        case AX_NT_CONSOLE_READLN: {
            static std::string line;
            if (!std::getline(std::cin, line)) line.clear();
            if (ret) *ret = (ax_val_t)(uintptr_t)line.c_str();
            return 0;
        }
        case AX_NT_CONSOLE_CLEAR: {
            hostPrint(ctx, "\033[2J\033[H", 7);
            return 0;
        }

        /* --- fs --- */
        case AX_NT_FS_OPEN:
        case AX_NT_FS_READ:
        case AX_NT_FS_WRITE:
        case AX_NT_FS_CLOSE:
        case AX_NT_FS_LIST:
        case AX_NT_FS_STAT:
            /* El runner de host no tiene sistema de ficheros de AxolynOS. */
            return -2;

        /* --- proc --- */
        case AX_NT_PROC_EXIT: {
            int code = 0;
            if (argc >= 1) code = (int)(int64_t)args[0];
            if (state) {
                state->exitCode = code;
                state->exited = true;
            }
            std::fflush(stdout);
            std::exit(code);
            return 0;
        }
        case AX_NT_PROC_PID: {
            if (ret) *ret = (ax_val_t)axGetPid();
            return 0;
        }
        case AX_NT_PROC_SPAWN:
        case AX_NT_PROC_GETS:
            return -2;

        /* --- mem --- */
        case AX_NT_MEM_INFO: {
            if (ret) *ret = (ax_val_t)(64u * 1024u);
            return 0;
        }
        case AX_NT_MEM_ALLOC:
        case AX_NT_MEM_FREE:
            /* El heap de la VM ya gestiona la memoria del programa. */
            return -2;

        /* --- time --- */
        case AX_NT_TIME_NOW_MS: {
            if (ret) *ret = (ax_val_t)axvm_host_millis();
            return 0;
        }
        case AX_NT_TIME_SLEEP: {
            uint64_t ms = (argc >= 1) ? (uint64_t)args[0] : 0u;
            if (ms) std::this_thread::sleep_for(std::chrono::milliseconds(ms));
            return 0;
        }

        default:
            return -3;
    }
}

/* ------------------------------------------------------------------ */
/* Lectura del fichero .axb                                            */
/* ------------------------------------------------------------------ */
bool readFile(const std::string& path, std::vector<uint8_t>& out) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    in.seekg(0, std::ios::end);
    std::streamoff size = in.tellg();
    if (size < 0) return false;
    in.seekg(0, std::ios::beg);
    out.resize((size_t)size);
    if (size > 0) in.read((char*)out.data(), size);
    return (bool)in || size == 0;
}

} /* namespace */

/* ------------------------------------------------------------------ */
/* main                                                                */
/* ------------------------------------------------------------------ */
int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "Uso: axrun <programa.axb> [argumentos...]\n";
        return 2;
    }

    const std::string path = argv[1];
    std::vector<uint8_t> image;
    if (!readFile(path, image)) {
        std::cerr << "axrun: no se puede leer '" << path << "'\n";
        return 2;
    }

    RunnerState state;
    AxVMConfig cfg;
    std::memset(&cfg, 0, sizeof(cfg));
    cfg.alloc = hostAlloc;
    cfg.alloc_ctx = 0;
    cfg.free = hostFree;
    cfg.free_ctx = 0;
    cfg.print = hostPrint;
    cfg.print_ctx = 0;
    cfg.native = nativeCall;
    cfg.native_ctx = &state;
    cfg.stack_size = 64u * 1024u;
    cfg.heap_size = 4u * 1024u * 1024u;

    AxVM* vm = 0;
    if (axvm_create(&cfg, &vm) != 0 || !vm) {
        std::cerr << "axrun: no se pudo crear la maquina virtual\n";
        return 2;
    }

    if (axvm_load(vm, image.data(), image.size()) != 0) {
        std::cerr << "axrun: no se pudo cargar '" << path << "': "
                  << axvm_last_error(vm) << "\n";
        axvm_destroy(vm);
        return 2;
    }

    int exitCode = 0;
    for (;;) {
        AxVMStatus status = axvm_run(vm, &exitCode);
        if (status == AX_VM_HALTED) break;
        if (status == AX_VM_ERROR) {
            std::cerr << "axrun: error de ejecucion: " << axvm_last_error(vm) << "\n";
            axvm_destroy(vm);
            return 1;
        }
        if (status == AX_VM_YIELD) {
            std::this_thread::yield();
            continue;
        }
        if (status == AX_VM_SLEEP) {
            uint64_t remaining = axvm_sleep_remaining(vm);
            if (remaining > 50u) remaining = 50u;
            if (remaining) std::this_thread::sleep_for(std::chrono::milliseconds(remaining));
            continue;
        }
        if (status == AX_VM_RUNNING) {
            /* La VM se detuvo a proposito: se pide seguir. */
            continue;
        }
        break;
    }

    std::fflush(stdout);
    axvm_destroy(vm);
    return exitCode;
}
