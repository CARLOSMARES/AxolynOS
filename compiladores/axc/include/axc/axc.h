/*
 * AxolynOS - Interfaz publica del compilador de Axolyn (axc).
 *
 * axc compila un fichero .ax a un binario .axb (formato AXB) que ejecuta
 * la maquina virtual de Kernel/vm/axvm.cpp.
 */
#ifndef AXC_AXC_H
#define AXC_AXC_H

#include <ostream>
#include <string>
#include <vector>

#include "axc/ast.h"
#include "axc/codegen.h"
#include "axc/lexer.h"

namespace axc {

struct CompileOptions {
    std::string inputPath;
    std::string outputPath;
    bool dumpAst = false;
    bool dumpBytecode = false;
    bool verbose = false;
};

struct CompileResult {
    bool ok = false;
    std::vector<Diag> diags;
    BytecodeImage image;
};

/* Compila un fuente ya en memoria. */
CompileResult compileSource(const std::string& source, const std::string& fileName,
                            const CompileOptions& options);

/* Compila un fichero .ax de disco. */
bool compileFile(const CompileOptions& options, CompileResult& result);

/* Vuelca el AST en texto legible. */
void dumpAst(const Program& prog, std::ostream& os);

/* Vuelca los diagnosticos. */
void dumpDiags(const std::vector<Diag>& diags, const std::string& fileName, std::ostream& os);

} /* namespace axc */

#endif /* AXC_AXC_H */
