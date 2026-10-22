/*
 * AxolynOS - Generador de codigo de Axolyn (bytecode AXB).
 *
 * Emite el formato descrito en Kernel/vm/axbytecode.h.
 */
#ifndef AXC_CODEGEN_H
#define AXC_CODEGEN_H

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "axc/ast.h"
#include "axc/lexer.h"

namespace axc {

/* ------------------------------------------------------------------ */
/* Imagen AXB en memoria                                              */
/* ------------------------------------------------------------------ */
struct BytecodeImage {
    struct FuncInfo {
        std::string name;
        uint32_t addr = 0;
        uint32_t argc = 0;
        uint32_t nlocals = 0;
        uint32_t stackSize = 0;
    };
    struct GlobalInfo {
        std::string name;
        uint32_t slot = 0;
        uint32_t size = 8;
        uint32_t flags = 0;
    };
    struct NativeInfo {
        std::string name;
        uint32_t id = 0;
    };
    struct ImportInfo {
        std::string ns;
        uint32_t nativeBase = 0;
        uint32_t count = 0;
    };

    std::vector<uint8_t>  code;
    std::vector<int64_t>  consts;
    std::vector<std::string> strings;
    std::vector<FuncInfo>   funcs;
    std::vector<GlobalInfo> globals;
    std::vector<NativeInfo> natives;
    std::vector<ImportInfo> imports;

    uint32_t entryFunc = 0xFFFFFFFFu;
    uint32_t mainFunc = 0xFFFFFFFFu;
    uint32_t globalSlots = 0;

    /* Listado legible del bytecode. */
    std::string disassembly() const;
};

/* ------------------------------------------------------------------ */
/* Generador                                                           */
/* ------------------------------------------------------------------ */
class CodeGen {
public:
    explicit CodeGen(const Program& prog) : prog_(prog) {}

    bool generate(BytecodeImage& out, std::vector<Diag>& diags);

private:
    struct LoopCtx {
        size_t breakLabel = 0;
        size_t continueLabel = 0;
        size_t stepLabel = 0;
    };

    /* --- emision basica --- */
    void emit(uint8_t op);
    void emitU32(uint32_t v);
    void emitU64(uint64_t v);
    void emitPushI64(int64_t v);
    void emitPushStr(const std::string& s);
    size_t newLabel();
    void bindLabel(size_t label);
    void patchLabel(size_t label);
    size_t emitJump(uint8_t op, size_t label);

    /* --- codigo --- */
    void genFunction(const FuncDecl& fn, BytecodeImage& out);
    void genStatements(const std::vector<StmtPtr>& body);
    void genStatement(const Stmt& stmt);
    void genExpr(const Expr* expr);
    void genAddr(const Expr* expr);
    void genAssign(const Expr* target, BinOp op, const Expr* value, bool wantValue);
    void genBlock(const std::vector<StmtPtr>& body);

    /* --- ayudas --- */
    uint32_t stringIndex(const std::string& s);
    void error(const SourcePos& pos, const std::string& msg);

    const Program& prog_;
    std::vector<uint8_t> code_;
    std::vector<std::string> strings_;
    std::map<std::string, uint32_t> stringIndex_;
    std::vector<size_t> labelPos_;
    struct PendingJump { size_t operandPos; size_t label; };
    std::vector<PendingJump> pending_;
    std::vector<LoopCtx> loops_;
    std::vector<Diag>* diags_ = nullptr;
    bool failed_ = false;
};

/* Escribe la imagen en un fichero .axb. */
bool writeAxBFile(const std::string& path, const BytecodeImage& image, std::string& error);

} /* namespace axc */

#endif /* AXC_CODEGEN_H */
