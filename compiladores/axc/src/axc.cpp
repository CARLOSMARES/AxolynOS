/*
 * AxolynOS - Tuberia del compilador Axolyn (axc).
 *
 *   fuente -> lexer -> parser -> sema -> codegen -> imagen AXB
 */
#include "axc/axc.h"

#include "axc/codegen.h"
#include "axc/parser.h"
#include "axc/sema.h"

#include <fstream>
#include <sstream>

namespace axc {

/* ------------------------------------------------------------------ */
/* Compilacion de un fuente en memoria                                 */
/* ------------------------------------------------------------------ */
CompileResult compileSource(const std::string& source, const std::string& fileName,
                             const CompileOptions& options) {
    (void)options;
    CompileResult result;
    Program prog;

    /* --- fase 1 y 2: lexico y sintactico --- */
    Parser parser(source, fileName);
    if (!parser.parseProgram(prog)) {
        result.diags = parser.diagnostics();
        result.ok = false;
        return result;
    }

    /* --- fase 3: analisis semantico --- */
    Sema sema(prog);
    if (!sema.run(result.diags)) {
        result.ok = false;
        return result;
    }

    /* --- fase 4: generacion de codigo --- */
    CodeGen gen(prog);
    if (!gen.generate(result.image, result.diags)) {
        result.ok = false;
        return result;
    }

    result.ok = true;
    return result;
}

/* ------------------------------------------------------------------ */
/* Compilacion de un fichero                                           */
/* ------------------------------------------------------------------ */
bool compileFile(const CompileOptions& options, CompileResult& result) {
    std::ifstream in(options.inputPath, std::ios::binary);
    if (!in) {
        result.diags.emplace_back(1, 1,
                                  "no se puede abrir '" + options.inputPath + "'");
        result.ok = false;
        return false;
    }
    std::ostringstream buffer;
    buffer << in.rdbuf();
    in.close();

    result = compileSource(buffer.str(), options.inputPath, options);
    if (!result.ok) return false;

    std::string error;
    if (!writeAxBFile(options.outputPath, result.image, error)) {
        result.diags.emplace_back(1, 1, error);
        result.ok = false;
        return false;
    }
    return true;
}

/* ------------------------------------------------------------------ */
/* Diagnosticos                                                        */
/* ------------------------------------------------------------------ */
void dumpDiags(const std::vector<Diag>& diags, const std::string& fileName, std::ostream& os) {
    for (const Diag& d : diags) {
        os << fileName << ":" << d.line << ":" << d.column << ": " << d.message << "\n";
    }
}

} /* namespace axc */
