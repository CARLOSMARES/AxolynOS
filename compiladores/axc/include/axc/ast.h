/*
 * AxolynOS - AST del lenguaje Axolyn.
 *
 * El arbol es propio (no depende de ninguna libreria externa). Los nodos
 * llevan la posicion del fuente y, tras el analisis semantico, el tipo
 * inferido y la resolucion de los simbolos (que consume el generador de
 * codigo).
 */
#ifndef AXC_AST_H
#define AXC_AST_H

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "axc/lexer.h"

namespace axc {

/* ------------------------------------------------------------------ */
/* Tipos                                                              */
/* ------------------------------------------------------------------ */
struct TypeRef;
using TypeRefPtr = std::shared_ptr<TypeRef>;

struct StructDef {
    struct Field {
        std::string name;
        TypeRefPtr type;
        uint32_t offset = 0;   /* desplazamiento en bytes (multiplo de 8) */
    };
    std::string name;
    std::vector<Field> fields;
    uint64_t size = 0;
    SourcePos pos;
};

struct TypeRef {
    enum class Kind {
        Unknown,
        Int,
        Bool,
        String,
        Void,
        Pointer,   /* *T  : direccion de un valor de tipo T   */
        Array,     /* [N]T: puntero a N celdas contiguas      */
        Struct     /* Nombre: puntero a un bloque de campos   */
    };

    Kind kind = Kind::Unknown;
    TypeRefPtr elem;            /* Pointer / Array: tipo del contenido */
    long long count = 0;        /* Array: numero de celdas              */
    std::string structName;     /* Struct: nombre del tipo              */
    const StructDef* def = nullptr; /* Struct: definicion resuelta      */
    SourcePos pos;

    std::string describe() const;
    bool isUnknown() const { return kind == Kind::Unknown; }
    bool isNumeric() const { return kind == Kind::Int || kind == Kind::Bool; }
    bool isPointerLike() const {
        return kind == Kind::Pointer || kind == Kind::Array || kind == Kind::Struct;
    }
};

/* Constructores de tipos (sema los usa para crear la tabla de natives). */
TypeRefPtr makeTypeInt();
TypeRefPtr makeTypeBool();
TypeRefPtr makeTypeString();
TypeRefPtr makeTypeVoid();
TypeRefPtr makeTypePointer(TypeRefPtr elem);
TypeRefPtr makeTypeArray(long long count, TypeRefPtr elem);

/* ------------------------------------------------------------------ */
/* Expresiones                                                         */
/* ------------------------------------------------------------------ */
struct Expr {
    SourcePos pos;
    TypeRefPtr type;         /* lo rellena el analizador semantico */
    virtual ~Expr() = default;
};
using ExprPtr = std::unique_ptr<Expr>;

enum class BinOp {
    Add, Sub, Mul, Div, Mod,
    Eq, Ne, Lt, Le, Gt, Ge,
    And, Or,
    Assign, AddAssign, SubAssign, MulAssign, DivAssign, ModAssign
};

const char* binOpName(BinOp op);
bool isAssignOp(BinOp op);
BinOp assignToBaseOp(BinOp op);   /* += -> + */

struct IntExpr : Expr {
    long long value = 0;
};

struct BoolExpr : Expr {
    bool value = false;
};

struct StringExpr : Expr {
    std::string value;
};

struct IdentExpr : Expr {
    std::string name;

    /* Resolucion (sema): */
    enum class Sym { None, Local, Global, Function, Namespace, StructType };
    Sym sym = Sym::None;
    int index = -1;   /* indice de local, global o funcion */
};

struct FieldExpr : Expr {
    ExprPtr base;          /* receptor o namespace */
    std::string name;
    uint32_t offset = 0;   /* desplazamiento del campo (sema) */
    bool resolved = false;
};

struct IndexExpr : Expr {
    ExprPtr base;
    ExprPtr index;
};

struct UnaryExpr : Expr {
    char op = 0;           /* '-', '!', '*', '&' */
    ExprPtr operand;
};

struct BinaryExpr : Expr {
    BinOp op = BinOp::Add;
    ExprPtr lhs;
    ExprPtr rhs;
};

struct CallExpr : Expr {
    ExprPtr callee;
    std::vector<ExprPtr> args;

    /* Resolucion (sema): */
    enum class Kind { Unknown, User, Native };
    Kind kind = Kind::Unknown;
    int funcIndex = -1;
    uint32_t nativeId = 0;
};

struct AssignExpr : Expr {
    ExprPtr target;
    BinOp op = BinOp::Assign;
    ExprPtr value;
};

/* new T : reserva memoria cero para un valor de tipo T. */
struct NewExpr : Expr {
    TypeRefPtr type;
};

/* ------------------------------------------------------------------ */
/* Sentencias                                                          */
/* ------------------------------------------------------------------ */
struct Stmt {
    SourcePos pos;
    virtual ~Stmt() = default;
};
using StmtPtr = std::unique_ptr<Stmt>;

struct VarDeclStmt : Stmt {
    bool isLet = false;
    std::string name;
    TypeRefPtr type;
    ExprPtr init;
    int slot = -1;   /* celda local asignada por el sema */
};

struct ExprStmt : Stmt {
    ExprPtr expr;
};

struct AssignStmt : Stmt {
    ExprPtr target;
    BinOp op = BinOp::Assign;
    ExprPtr value;
};

struct BlockStmt : Stmt {
    std::vector<StmtPtr> body;
};

struct IfStmt : Stmt {
    ExprPtr cond;
    std::vector<StmtPtr> thenBody;
    bool hasElse = false;
    std::vector<StmtPtr> elseBody;
};

struct WhileStmt : Stmt {
    ExprPtr cond;
    std::vector<StmtPtr> body;
};

struct ForStmt : Stmt {
    std::unique_ptr<VarDeclStmt> initVar;  /* forma "var i: int = 0" */
    ExprPtr initExpr;                      /* forma "i = 0"        */
    ExprPtr cond;
    ExprPtr step;
    std::vector<StmtPtr> body;
};

struct ReturnStmt : Stmt {
    ExprPtr value;
};

struct BreakStmt : Stmt {};
struct ContinueStmt : Stmt {};

/* ------------------------------------------------------------------ */
/* Programa                                                            */
/* ------------------------------------------------------------------ */
struct ImportDecl {
    std::string ns;
    SourcePos pos;
};

struct GlobalDecl {
    bool isLet = false;
    std::string name;
    TypeRefPtr type;
    ExprPtr init;
    SourcePos pos;
    int slot = -1;   /* celda global asignada por el sema */
    int order = 0;   /* orden de declaracion en el fichero */
};

struct FuncDecl {
    struct Param {
        std::string name;
        TypeRefPtr type;
        int slot = -1;
    };
    std::string name;
    std::vector<Param> params;
    TypeRefPtr retType;
    std::vector<StmtPtr> body;
    SourcePos pos;
    int index = -1;      /* indice de la funcion en el programa */
    int localCount = 0;  /* numero de celdas locales (sema)   */
    int order = 0;       /* orden de declaracion en el fichero */
};

struct Program {
    std::vector<ImportDecl> imports;
    std::vector<GlobalDecl> globals;
    std::vector<FuncDecl> funcs;
    std::vector<StructDef> structs;

    /* Tabla de natives usada (sema) */
    struct NativeUse {
        std::string name;   /* "core.print" */
        uint32_t id = 0;
    };
    std::vector<NativeUse> nativeUses;
};

} /* namespace axc */

#endif /* AXC_AST_H */
