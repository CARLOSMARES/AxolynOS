/*
 * AxolynOS - Analisis semantico del lenguaje Axolyn.
 *
 * Comprueba: tabla de simbolos con ambitos, inferencia de tipos, aridad y
 * tipos de las llamadas, variables no declaradas y uso antes de definicion.
 */
#ifndef AXC_SEMA_H
#define AXC_SEMA_H

#include <map>
#include <set>
#include <string>
#include <vector>

#include "axc/ast.h"

namespace axc {

/* ------------------------------------------------------------------ */
/* Tabla de funciones nativas del lenguaje                             */
/* ------------------------------------------------------------------ */
struct NativeFunc {
    std::string ns;
    std::string name;      /* "print" */
    uint32_t id = 0;       /* AX_NT_* */
    std::vector<TypeRefPtr> params;
    TypeRefPtr ret;        /* nullptr si no devuelve valor */
    bool hasRet = false;
};

/* Tabla completa (los ids son los de Kernel/vm/axvm.h). */
const std::vector<NativeFunc>& nativeTable();
/* Rango de ids de un namespace: primero y numero de funciones. */
bool nativeNamespaceRange(const std::string& ns, uint32_t& firstId, uint32_t& count);
/* Busca una funcion nativa por namespace y nombre. */
const NativeFunc* findNative(const std::string& ns, const std::string& name);
/* Nombre completo "ns.fun" a partir de un id nativo. */
std::string nativeFullName(uint32_t id);

/* ------------------------------------------------------------------ */
/* Analizador semantico                                               */
/* ------------------------------------------------------------------ */
class Sema {
public:
    explicit Sema(Program& prog) : prog_(prog) {}

    /* Ejecuta el analisis. Devuelve false si se encontraron errores. */
    bool run(std::vector<Diag>& diags);

private:
    struct Symbol {
        enum class Kind { Local, Global, Func, StructType, Namespace };
        Kind kind = Kind::Local;
        TypeRefPtr type;
        int index = -1;
        bool isLet = false;
        int line = 0;
    };
    using Scope = std::vector<std::pair<std::string, Symbol>>;

    struct FuncCtx {
        FuncDecl* fn = nullptr;
        TypeRefPtr retType;
        int curLocal = 0;
        int maxLocal = 0;
        bool sawReturn = false;
        int loopDepth = 0;
    };

    /* --- diagnosticos --- */
    void error(const SourcePos& pos, const std::string& msg);
    void warn(const SourcePos& pos, const std::string& msg);

    /* --- ambitos --- */
    void pushScope();
    void popScope();
    bool declare(const std::string& name, const Symbol& sym, const SourcePos& pos);
    const Symbol* lookup(const std::string& name) const;

    /* --- structs --- */
    void collectStructs();
    bool resolveTypeNames(TypeRefPtr& type, const SourcePos& pos);

    /* --- funciones --- */
    void collectFunctions();
    void collectFuncDecls(const FuncDecl& fn, std::set<std::string>& names);
    void analyzeFunction(FuncDecl& fn);
    void analyzeStatements(std::vector<StmtPtr>& body, FuncCtx& ctx);
    void analyzeStatement(Stmt& stmt, FuncCtx& ctx);

    /* --- expresiones --- */
    TypeRefPtr analyzeExpr(Expr* expr, FuncCtx& ctx);
    TypeRefPtr analyzeIdent(IdentExpr* expr, FuncCtx& ctx);
    TypeRefPtr analyzeField(FieldExpr* expr, FuncCtx& ctx);
    TypeRefPtr analyzeIndex(IndexExpr* expr, FuncCtx& ctx);
    TypeRefPtr analyzeUnary(UnaryExpr* expr, FuncCtx& ctx);
    TypeRefPtr analyzeBinary(BinaryExpr* expr, FuncCtx& ctx);
    TypeRefPtr analyzeCall(CallExpr* expr, FuncCtx& ctx);
    TypeRefPtr analyzeAssign(Expr* target, BinOp op, Expr* value, FuncCtx& ctx);
    TypeRefPtr analyzeNew(NewExpr* expr, FuncCtx& ctx);

    /* --- tipos --- */
    bool typesCompatible(const TypeRef* a, const TypeRef* b) const;
    bool checkAssign(const TypeRef* target, const TypeRef* value, const SourcePos& pos,
                     const std::string& what);
    int  newLocal(FuncCtx& ctx, const std::string& name, const TypeRefPtr& type,
                  const SourcePos& pos);

    Program& prog_;
    std::vector<Diag> diags_;
    std::vector<Scope> scopes_;
    std::map<std::string, const StructDef*> structs_;
    std::map<std::string, int> funcIndex_;
    std::set<std::string> globalNames_;
    std::set<std::string> importedNs_;
    std::set<std::string> funcNames_;
    std::set<std::string> declaredInFunc_;
    std::set<std::string> usedNatives_;
    bool hasError_ = false;
};

} /* namespace axc */

#endif /* AXC_SEMA_H */
