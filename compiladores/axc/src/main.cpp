/*
 * AxolynOS - Linea de ordenes del compilador Axolyn (axc).
 *
 *   axc <fuente.ax> [-o <salida.axb>] [--dump-ast] [--dump-bytecode] [--verbose]
 *
 * Devuelve 0 si todo va bien y 1 si hay errores.
 */
#include "axc/axc.h"
#include "axc/parser.h"
#include "axc/sema.h"

#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace {

const char* kUsage =
    "axc - compilador del lenguaje Axolyn\n"
    "\n"
    "Uso:\n"
    "  axc <fuente.ax> [-o <salida.axb>] [--dump-ast] [--dump-bytecode] [--verbose]\n"
    "\n"
    "Opciones:\n"
    "  -o <fichero>      nombre del binario .axb de salida\n"
    "                    (por defecto: el fuente con extension .axb)\n"
    "  --dump-ast        imprime el arbol de sintaxis abstracta\n"
    "  --dump-bytecode   imprime el desensamblado del bytecode\n"
    "  --verbose         muestra mas informacion del proceso\n"
    "  -h, --help        muestra esta ayuda\n";

/* Cambia la extension de un nombre de fichero. */
std::string replaceExtension(const std::string& path, const std::string& ext) {
    size_t slash = path.find_last_of("/\\");
    size_t dot = path.find_last_of('.');
    if (dot == std::string::npos || (slash != std::string::npos && dot < slash)) {
        return path + ext;
    }
    return path.substr(0, dot) + ext;
}

bool endsWith(const std::string& s, const std::string& suffix) {
    if (s.size() < suffix.size()) return false;
    return s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

} /* namespace */

int main(int argc, char** argv) {
    axc::CompileOptions options;
    std::string input;
    bool sawOutput = false;

    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "-h" || arg == "--help") {
            std::cout << kUsage;
            return 0;
        }
        if (arg == "-o") {
            if (i + 1 >= argc) {
                std::cerr << "axc: falta el nombre del fichero de salida tras '-o'\n";
                return 1;
            }
            options.outputPath = argv[++i];
            sawOutput = true;
            continue;
        }
        if (arg.rfind("-o", 0) == 0 && arg.size() > 2) {
            options.outputPath = arg.substr(2);
            sawOutput = true;
            continue;
        }
        if (arg == "--dump-ast") { options.dumpAst = true; continue; }
        if (arg == "--dump-bytecode") { options.dumpBytecode = true; continue; }
        if (arg == "--verbose" || arg == "-v") { options.verbose = true; continue; }
        if (!arg.empty() && arg[0] == '-') {
            std::cerr << "axc: opcion desconocida '" << arg << "'\n";
            std::cerr << kUsage;
            return 1;
        }
        if (!input.empty()) {
            std::cerr << "axc: solo se admite un fichero de entrada\n";
            return 1;
        }
        input = arg;
    }

    if (input.empty()) {
        std::cerr << "axc: falta el fichero de entrada\n";
        std::cerr << kUsage;
        return 1;
    }
    options.inputPath = input;
    if (!sawOutput || options.outputPath.empty()) {
        options.outputPath = replaceExtension(input, ".axb");
    }

    if (options.verbose) {
        std::cerr << "axc: fuente   = " << input << "\n";
        std::cerr << "axc: salida   = " << options.outputPath << "\n";
    }

    axc::CompileResult result;
    axc::compileFile(options, result);

    if (!result.diags.empty()) {
        axc::dumpDiags(result.diags, input, std::cerr);
    }

    if (!result.ok) {
        std::cerr << "axc: '" << input << "' no se pudo compilar ("
                  << result.diags.size() << " diagnostico(s))\n";
        return 1;
    }

    if (options.dumpAst) {
        /* El AST ya no esta disponible tras la compilacion: se vuelve a
         * analizar el fuente para poder mostrarlo. */
        std::ifstream in(input, std::ios::binary);
        if (in) {
            std::ostringstream buffer;
            buffer << in.rdbuf();
            axc::Program prog;
            axc::Parser parser(buffer.str(), input);
            if (parser.parseProgram(prog)) {
                axc::Sema sema(prog);
                std::vector<axc::Diag> diags;
                sema.run(diags);
                axc::dumpAst(prog, std::cout);
            }
        }
    }

    if (options.dumpBytecode) {
        std::string listing = result.image.disassembly();
        std::cout << listing;
        /* Tambien se deja en disco junto a la salida. */
        std::string disPath = options.outputPath + ".dis.txt";
        std::ofstream dis(disPath, std::ios::binary | std::ios::trunc);
        if (dis) {
            dis << listing;
            if (options.verbose) std::cerr << "axc: listado en " << disPath << "\n";
        } else if (options.verbose) {
            std::cerr << "axc: no se pudo escribir " << disPath << "\n";
        }
    }

    if (options.verbose) {
        std::cerr << "axc: " << result.image.funcs.size() << " funcion(es), "
                  << result.image.globals.size() << " global(es), "
                  << result.image.code.size() << " bytes de codigo\n";
    }

    if (!endsWith(options.outputPath, ".axb") && options.verbose) {
        std::cerr << "axc: aviso: la salida no termina en '.axb'\n";
    }

    std::cout << "axc: '" << input << "' -> '" << options.outputPath << "'\n";
    return 0;
}
