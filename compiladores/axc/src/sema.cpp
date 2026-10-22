/*
 * AxolynOS - Analisis semantico del lenguaje Axolyn.
 */
#include "axc/sema.h"

#include <algorithm>
#include <utility>

/* Los ids de los natives son los de la maquina virtual (Kernel/vm/axvm.h). */
#include "axvm.h"

namespace axc {

/* ------------------------------------------------------------------ */
/* Tipos basicos                                                      */
/* ------------------------------------------------------------------ */
TypeRefPtr makeTypeInt() {
    static const TypeRefPtr t = [] {
        auto p = std::make_shared<TypeRef>();
        p->kind = TypeRef::Kind::Int;
        return p;
    }();
    return t;
}

TypeRefPtr makeTypeBool() {
    static const TypeRefPtr t = [] {
        auto p = std::make_shared<TypeRef>();
        p->kind = TypeRef::Kind::Bool;
        return p;
    }();
    return t;
}

TypeRefPtr makeTypeString() {
    static const TypeRefPtr t = [] {
        auto p = std::make_shared<TypeRef>();
        p->kind = TypeRef::Kind::String;
        return p;
    }();
    return t;
}

TypeRefPtr makeTypeVoid() {
    static const TypeRefPtr t = [] {
        auto p = std::make_shared<TypeRef>();
        p->kind = TypeRef::Kind::Void;
        return p;
    }();
    return t;
}

TypeRefPtr makeTypePointer(TypeRefPtr elem) {
    auto p = std::make_shared<TypeRef>();
    p->kind = TypeRef::Kind::Pointer;
    p->elem = std::move(elem);
    return p;
}

TypeRefPtr makeTypeArray(long long count, TypeRefPtr elem) {
    auto p = std::make_shared<TypeRef>();
    p->kind = TypeRef::Kind::Array;
    p->count = count;
    p->elem = std::move(elem);
    return p;
}

namespace {
TypeRefPtr makeTypeUnknown() {
    auto p = std::make_shared<TypeRef>();
    p->kind = TypeRef::Kind::Unknown;
    return p;
}
} /* namespace */

std::string TypeRef::describe() const {
    switch (kind) {
        case Kind::Int: return "int";
        case Kind::Bool: return "bool";
        case Kind::String: return "string";
        case Kind::Void: return "void";
        case Kind::Pointer: return "*" + (elem ? elem->describe() : "?");
        case Kind::Array:
            return "[" + std::to_string(count) + "]" + (elem ? elem->describe() : "?");
        case Kind::Struct: return structName;
        default: return "?";
    }
}

const char* binOpName(BinOp op) {
    switch (op) {
        case BinOp::Add: return "+";
        case BinOp::Sub: return "-";
        case BinOp::Mul: return "*";
        case BinOp::Div: return "/";
        case BinOp::Mod: return "%";
        case BinOp::Eq: return "==";
        case BinOp::Ne: return "!=";
        case BinOp::Lt: return "<";
        case BinOp::Le: return "<=";
        case BinOp::Gt: return ">";
        case BinOp::Ge: return ">=";
        case BinOp::And: return "&&";
        case BinOp::Or: return "||";
        case BinOp::Assign: return "=";
        case BinOp::AddAssign: return "+=";
        case BinOp::SubAssign: return "-=";
        case BinOp::MulAssign: return "*=";
        case BinOp::DivAssign: return "/=";
        case BinOp::ModAssign: return "%=";
    }
    return "?";
}

bool isAssignOp(BinOp op) {
    switch (op) {
        case BinOp::Assign:
        case BinOp::AddAssign:
        case BinOp::SubAssign:
        case BinOp::MulAssign:
        case BinOp::DivAssign:
        case BinOp::ModAssign:
            return true;
        default:
            return false;
    }
}

BinOp assignToBaseOp(BinOp op) {
    switch (op) {
        case BinOp::AddAssign: return BinOp::Add;
        case BinOp::SubAssign: return BinOp::Sub;
        case BinOp::MulAssign: return BinOp::Mul;
        case BinOp::DivAssign: return BinOp::Div;
        case BinOp::ModAssign: return BinOp::Mod;
        default: return BinOp::Add;
    }
}

/* ------------------------------------------------------------------ */
/* Tabla de natives del lenguaje                                      */
/* ------------------------------------------------------------------ */
const std::vector<NativeFunc>& nativeTable() {
    static const std::vector<NativeFunc> table = [] {
        std::vector<NativeFunc> t;
        auto add = [&t](const char* ns, const char* name, uint32_t id,
                        std::vector<TypeRefPtr> params, TypeRefPtr ret) {
            NativeFunc f;
            f.ns = ns;
            f.name = name;
            f.id = id;
            f.params = std::move(params);
            f.ret = std::move(ret);
            f.hasRet = (f.ret != nullptr);
            t.push_back(std::move(f));
        };
        TypeRefPtr bytePtr = makeTypePointer(makeTypeInt());

        /* core */
        add("core", "print", AX_NT_CORE_PRINT, { makeTypeString() }, nullptr);
        add("core", "println", AX_NT_CORE_PRINTLN, { makeTypeString() }, nullptr);
        add("core", "print_int", AX_NT_CORE_PRINT_INT, { makeTypeInt() }, nullptr);
        add("core", "strlen", AX_NT_CORE_STRLEN, { makeTypeString() }, makeTypeInt());
        add("core", "int_to_str", AX_NT_CORE_INT_TO_STR, { makeTypeInt() }, makeTypeString());
        /* console */
        add("console", "write", AX_NT_CONSOLE_WRITE, { makeTypeString() }, nullptr);
        add("console", "read_line", AX_NT_CONSOLE_READLN, {}, makeTypeString());
        add("console", "clear", AX_NT_CONSOLE_CLEAR, {}, nullptr);
        /* fs */
        add("fs", "open", AX_NT_FS_OPEN, { makeTypeString() }, makeTypeInt());
        add("fs", "read", AX_NT_FS_READ,
            { makeTypeInt(), bytePtr, makeTypeInt() }, makeTypeInt());
        add("fs", "write", AX_NT_FS_WRITE,
            { makeTypeInt(), bytePtr, makeTypeInt() }, makeTypeInt());
        add("fs", "close", AX_NT_FS_CLOSE, { makeTypeInt() }, nullptr);
        add("fs", "list", AX_NT_FS_LIST, {}, makeTypeInt());
        add("fs", "stat", AX_NT_FS_STAT, { makeTypeString() }, makeTypeInt());
        /* proc */
        add("proc", "exit", AX_NT_PROC_EXIT, { makeTypeInt() }, nullptr);
        add("proc", "pid", AX_NT_PROC_PID, {}, makeTypeInt());
        add("proc", "spawn", AX_NT_PROC_SPAWN, { makeTypeString() }, makeTypeInt());
        add("proc", "gets", AX_NT_PROC_GETS,
            { bytePtr, makeTypeInt() }, makeTypeInt());
        /* mem */
        add("mem", "info", AX_NT_MEM_INFO, {}, makeTypeInt());
        add("mem", "alloc", AX_NT_MEM_ALLOC, { makeTypeInt() }, makeTypeInt());
        add("mem", "free", AX_NT_MEM_FREE, { bytePtr }, nullptr);
        /* time */
        add("time", "now_ms", AX_NT_TIME_NOW_MS, {}, makeTypeInt());
        add("time", "sleep", AX_NT_TIME_SLEEP, { makeTypeInt() }, nullptr);
        return t;
    }();
    return table;
}

const NativeFunc* findNative(const std::string& ns, const std::string& name) {
    for (const NativeFunc& f : nativeTable()) {
        if (f.ns == ns && f.name == name) return &f;
    }
    return nullptr;
}

std::string nativeFullName(uint32_t id) {
    for (const NativeFunc& f : nativeTable()) {
        if (f.id == id) return f.ns + "." + f.name;
    }
    return "native#" + std::to_string(id);
}

bool nativeNamespaceRange(const std::string& ns, uint32_t& firstId, uint32_t& count) {
    bool found = false;
    uint32_t first = 0xFFFFFFFFu;
    uint32_t n = 0;
    for (const NativeFunc& f : nativeTable()) {
        if (f.ns != ns) continue;
        found = true;
        if (f.id < first) first = f.id;
        n++;
    }
    if (!found) return false;
    firstId = first;
    count = n;
    return true;
}

namespace {
bool isNativeNamespace(const std::string& ns) {
    uint32_t first = 0, count = 0;
    return nativeNamespaceRange(ns, first, count);
}
} /* namespace */

/* ------------------------------------------------------------------ */
/* Utilidades del analizador                                          */
/* ------------------------------------------------------------------ */
namespace {

void collectStmtNames(const Stmt& stmt, std::set<std::string>& names) {
    if (const auto* v = dynamic_cast<const VarDeclStmt*>(&stmt)) {
        names.insert(v->name);
    } else if (const auto* b = dynamic_cast<const BlockStmt*>(&stmt)) {
        for (const StmtPtr& s : b->body) collectStmtNames(*s, names);
    } else if (const auto* i = dynamic_cast<const IfStmt*>(&stmt)) {
        for (const StmtPtr& s : i->thenBody) collectStmtNames(*s, names);
        for (const StmtPtr& s : i->elseBody) collectStmtNames(*s, names);
    } else if (const auto* w = dynamic_cast<const WhileStmt*>(&stmt)) {
        for (const StmtPtr& s : w->body) collectStmtNames(*s, names);
    } else if (const auto* f = dynamic_cast<const ForStmt*>(&stmt)) {
        if (f->initVar) names.insert(f->initVar->name);
        for (const StmtPtr& s : f->body) collectStmtNames(*s, names);
    }
}

/* Devuelve true si el bloque termina siempre con un 'return'. */
bool alwaysReturns(const std::vector<StmtPtr>& body) {
    if (body.empty()) return false;
    const Stmt& last = *body.back();
    if (dynamic_cast<const ReturnStmt*>(&last)) return true;
    if (const auto* b = dynamic_cast<const BlockStmt*>(&last)) return alwaysReturns(b->body);
    if (const auto* i = dynamic_cast<const IfStmt*>(&last)) {
        return i->hasElse && alwaysReturns(i->thenBody) && alwaysReturns(i->elseBody);
    }
    return false;
}

} /* namespace */

/* ------------------------------------------------------------------ */
/* Sema                                                               */
/* ------------------------------------------------------------------ */
void Sema::error(const SourcePos& pos, const std::string& msg) {
    diags_.emplace_back(pos.line, pos.column, msg);
    hasError_ = true;
}

void Sema::warn(const SourcePos& pos, const std::string& msg) {
    diags_.emplace_back(pos.line, pos.column, "aviso: " + msg);
}

void Sema::pushScope() {
    scopes_.emplace_back();
}

void Sema::popScope() {
    if (!scopes_.empty()) scopes_.pop_back();
}

bool Sema::declare(const std::string& name, const Symbol& sym, const SourcePos& pos) {
    if (!scopes_.empty()) {
        Scope& scope = scopes_.back();
        for (const auto& entry : scope) {
            if (entry.first == name) {
                error(pos, "el simbolo '" + name + "' ya esta declarado en este ambito");
                return false;
            }
        }
    }
    if (!scopes_.empty()) scopes_.back().emplace_back(name, sym);
    return true;
}

const Sema::Symbol* Sema::lookup(const std::string& name) const {
    for (auto it = scopes_.rbegin(); it != scopes_.rend(); ++it) {
        for (const auto& entry : *it) {
            if (entry.first == name) return &entry.second;
        }
    }
    return nullptr;
}

/* ------------------------------------------------------------------ */
/* Structs                                                            */
/* ------------------------------------------------------------------ */
void Sema::collectStructs() {
    for (StructDef& def : prog_.structs) {
        if (structs_.count(def.name)) {
            error(def.pos, "el struct '" + def.name + "' ya esta declarado");
            continue;
        }
        uint64_t offset = 0;
        for (StructDef::Field& field : def.fields) {
            resolveTypeNames(field.type, def.pos);
            field.offset = (uint32_t)offset;
            offset += 8u;   /* cada campo ocupa una celda de 8 bytes */
        }
        def.size = offset ? offset : 8u;
        structs_[def.name] = &def;
    }
}

bool Sema::resolveTypeNames(TypeRefPtr& type, const SourcePos& pos) {
    if (!type) return true;
    if (type->kind == TypeRef::Kind::Struct) {
        auto it = structs_.find(type->structName);
        if (it == structs_.end()) {
            error(pos, "tipo desconocido: '" + type->structName + "'");
            type = makeTypeUnknown();
            return false;
        }
        type->def = it->second;
    }
    if (type->elem) resolveTypeNames(type->elem, pos);
    return true;
}

/* ------------------------------------------------------------------ */
/* Funciones                                                          */
/* ------------------------------------------------------------------ */
void Sema::collectFunctions() {
    for (size_t i = 0; i < prog_.funcs.size(); i++) {
        FuncDecl& fn = prog_.funcs[i];
        fn.index = (int)i;
        if (funcIndex_.count(fn.name)) {
            error(fn.pos, "la funcion '" + fn.name + "' ya esta declarada");
            continue;
        }
        funcIndex_[fn.name] = (int)i;
        funcNames_.insert(fn.name);
    }
    for (GlobalDecl& g : prog_.globals) globalNames_.insert(g.name);
}

void Sema::collectFuncDecls(const FuncDecl& fn, std::set<std::string>& names) {
    for (const StmtPtr& s : fn.body) collectStmtNames(*s, names);
}

void Sema::analyzeFunction(FuncDecl& fn) {
    FuncCtx ctx;
    ctx.fn = &fn;
    ctx.retType = fn.retType ? fn.retType : makeTypeVoid();

    declaredInFunc_.clear();
    collectFuncDecls(fn, declaredInFunc_);

    pushScope();
    for (size_t i = 0; i < fn.params.size(); i++) {
        FuncDecl::Param& param = fn.params[i];
        resolveTypeNames(param.type, fn.pos);
        Symbol sym;
        sym.kind = Symbol::Kind::Local;
        sym.type = param.type;
        sym.index = (int)i;
        sym.isLet = true;
        sym.line = fn.pos.line;
        param.slot = (int)i;
        if (!declare(param.name, sym, fn.pos)) {
            /* continua: se usa el mismo nombre pero otro ambito */
        }
        if ((int)i + 1 > ctx.maxLocal) ctx.maxLocal = (int)i + 1;
    }
    ctx.curLocal = (int)fn.params.size();

    analyzeStatements(fn.body, ctx);

    popScope();
    fn.localCount = ctx.maxLocal;

    if (ctx.retType->kind != TypeRef::Kind::Void && !alwaysReturns(fn.body)) {
        error(fn.pos, "la funcion '" + fn.name + "' debe devolver un valor de tipo '" +
                      ctx.retType->describe() + "'");
    }
}

/* ------------------------------------------------------------------ */
/* Sentencias                                                         */
/* ------------------------------------------------------------------ */
void Sema::analyzeStatements(std::vector<StmtPtr>& body, FuncCtx& ctx) {
    int saved = ctx.curLocal;
    for (StmtPtr& stmt : body) {
        if (!stmt) continue;
        analyzeStatement(*stmt, ctx);
    }
    ctx.curLocal = saved;
}

void Sema::analyzeStatement(Stmt& stmt, FuncCtx& ctx) {
    if (auto* v = dynamic_cast<VarDeclStmt*>(&stmt)) {
        resolveTypeNames(v->type, v->pos);
        if (v->init) {
            TypeRefPtr t = analyzeExpr(v->init.get(), ctx);
            checkAssign(v->type.get(), t.get(), v->pos, "la variable '" + v->name + "'");
        } else if (v->isLet) {
            error(v->pos, "falta el valor inicial de '" + v->name + "'");
        }
        Symbol sym;
        sym.kind = Symbol::Kind::Local;
        sym.type = v->type;
        sym.isLet = v->isLet;
        sym.line = v->pos.line;
        sym.index = newLocal(ctx, v->name, v->type, v->pos);
        v->slot = sym.index;
        declare(v->name, sym, v->pos);
        return;
    }
    if (auto* e = dynamic_cast<ExprStmt*>(&stmt)) {
        analyzeExpr(e->expr.get(), ctx);
        return;
    }
    if (auto* a = dynamic_cast<AssignStmt*>(&stmt)) {
        analyzeAssign(a->target.get(), a->op, a->value.get(), ctx);
        return;
    }
    if (auto* b = dynamic_cast<BlockStmt*>(&stmt)) {
        pushScope();
        analyzeStatements(b->body, ctx);
        popScope();
        return;
    }
    if (auto* i = dynamic_cast<IfStmt*>(&stmt)) {
        TypeRefPtr ct = analyzeExpr(i->cond.get(), ctx);
        if (ct && ct->kind != TypeRef::Kind::Bool && ct->kind != TypeRef::Kind::Unknown) {
            error(i->cond->pos, "la condicion de un 'if' debe ser de tipo 'bool' (es '" +
                                ct->describe() + "')");
        }
        pushScope();
        analyzeStatements(i->thenBody, ctx);
        popScope();
        if (i->hasElse) {
            pushScope();
            analyzeStatements(i->elseBody, ctx);
            popScope();
        }
        return;
    }
    if (auto* w = dynamic_cast<WhileStmt*>(&stmt)) {
        TypeRefPtr ct = analyzeExpr(w->cond.get(), ctx);
        if (ct && ct->kind != TypeRef::Kind::Bool && ct->kind != TypeRef::Kind::Unknown) {
            error(w->cond->pos, "la condicion de un 'while' debe ser de tipo 'bool' (es '" +
                                ct->describe() + "')");
        }
        pushScope();
        ctx.loopDepth++;
        analyzeStatements(w->body, ctx);
        ctx.loopDepth--;
        popScope();
        return;
    }
    if (auto* f = dynamic_cast<ForStmt*>(&stmt)) {
        pushScope();
        if (f->initVar) {
            VarDeclStmt& v = *f->initVar;
            resolveTypeNames(v.type, v.pos);
            if (v.init) {
                TypeRefPtr t = analyzeExpr(v.init.get(), ctx);
                checkAssign(v.type.get(), t.get(), v.pos, "la variable '" + v.name + "'");
            } else {
                error(v.pos, "falta el valor inicial de '" + v.name + "'");
            }
            Symbol sym;
            sym.kind = Symbol::Kind::Local;
            sym.type = v.type;
            sym.isLet = v.isLet;
            sym.line = v.pos.line;
            sym.index = newLocal(ctx, v.name, v.type, v.pos);
            v.slot = sym.index;
            declare(v.name, sym, v.pos);
        } else if (f->initExpr) {
            analyzeExpr(f->initExpr.get(), ctx);
        }
        if (f->cond) {
            TypeRefPtr ct = analyzeExpr(f->cond.get(), ctx);
            if (ct && ct->kind != TypeRef::Kind::Bool && ct->kind != TypeRef::Kind::Unknown) {
                error(f->cond->pos, "la condicion de un 'for' debe ser de tipo 'bool' (es '" +
                                    ct->describe() + "')");
            }
        }
        if (f->step) analyzeExpr(f->step.get(), ctx);
        ctx.loopDepth++;
        analyzeStatements(f->body, ctx);
        ctx.loopDepth--;
        popScope();
        return;
    }
    if (auto* r = dynamic_cast<ReturnStmt*>(&stmt)) {
        ctx.sawReturn = true;
        if (r->value) {
            TypeRefPtr t = analyzeExpr(r->value.get(), ctx);
            if (ctx.retType->kind == TypeRef::Kind::Void) {
                error(r->pos, "una funcion de tipo 'void' no puede devolver un valor");
            } else {
                checkAssign(ctx.retType.get(), t.get(), r->pos, "el valor devuelto");
            }
        } else if (ctx.retType->kind != TypeRef::Kind::Void) {
            error(r->pos, "falta el valor a devolver (se espera '" +
                          ctx.retType->describe() + "')");
        }
        return;
    }
    if (dynamic_cast<BreakStmt*>(&stmt)) {
        if (ctx.loopDepth <= 0) {
            error(stmt.pos, "'break' solo puede usarse dentro de un bucle");
        }
        return;
    }
    if (dynamic_cast<ContinueStmt*>(&stmt)) {
        if (ctx.loopDepth <= 0) {
            error(stmt.pos, "'continue' solo puede usarse dentro de un bucle");
        }
        return;
    }
    error(stmt.pos, "sentencia no soportada");
}

int Sema::newLocal(FuncCtx& ctx, const std::string& name, const TypeRefPtr& type,
                    const SourcePos& pos) {
    (void)name;
    (void)type;
    (void)pos;
    int slot = ctx.curLocal++;
    if (ctx.curLocal > ctx.maxLocal) ctx.maxLocal = ctx.curLocal;
    return slot;
}

/* ------------------------------------------------------------------ */
/* Expresiones                                                        */
/* ------------------------------------------------------------------ */
TypeRefPtr Sema::analyzeExpr(Expr* expr, FuncCtx& ctx) {
    if (!expr) return makeTypeUnknown();
    TypeRefPtr t;
    if (auto* e = dynamic_cast<IntExpr*>(expr)) {
        (void)e;
        t = makeTypeInt();
    } else if (auto* e = dynamic_cast<BoolExpr*>(expr)) {
        (void)e;
        t = makeTypeBool();
    } else if (auto* e = dynamic_cast<StringExpr*>(expr)) {
        (void)e;
        t = makeTypeString();
    } else if (auto* e = dynamic_cast<IdentExpr*>(expr)) {
        t = analyzeIdent(e, ctx);
    } else if (auto* e = dynamic_cast<FieldExpr*>(expr)) {
        t = analyzeField(e, ctx);
    } else if (auto* e = dynamic_cast<IndexExpr*>(expr)) {
        t = analyzeIndex(e, ctx);
    } else if (auto* e = dynamic_cast<UnaryExpr*>(expr)) {
        t = analyzeUnary(e, ctx);
    } else if (auto* e = dynamic_cast<BinaryExpr*>(expr)) {
        t = analyzeBinary(e, ctx);
    } else if (auto* e = dynamic_cast<CallExpr*>(expr)) {
        t = analyzeCall(e, ctx);
    } else if (auto* e = dynamic_cast<AssignExpr*>(expr)) {
        t = analyzeAssign(e->target.get(), e->op, e->value.get(), ctx);
    } else if (auto* e = dynamic_cast<NewExpr*>(expr)) {
        t = analyzeNew(e, ctx);
    } else {
        error(expr->pos, "expresion no soportada");
        t = makeTypeUnknown();
    }
    expr->type = t ? t : makeTypeUnknown();
    return expr->type;
}

TypeRefPtr Sema::analyzeIdent(IdentExpr* expr, FuncCtx& ctx) {
    (void)ctx;
    const Symbol* sym = lookup(expr->name);
    if (sym) {
        switch (sym->kind) {
            case Symbol::Kind::Local:
                expr->sym = IdentExpr::Sym::Local;
                expr->index = sym->index;
                return sym->type ? sym->type : makeTypeUnknown();
            case Symbol::Kind::Global:
                expr->sym = IdentExpr::Sym::Global;
                expr->index = sym->index;
                return sym->type ? sym->type : makeTypeUnknown();
            case Symbol::Kind::Func:
                error(expr->pos, "no se puede usar la funcion '" + expr->name +
                                "' como un valor");
                expr->sym = IdentExpr::Sym::Function;
                expr->index = sym->index;
                return makeTypeUnknown();
            default:
                break;
        }
    }
    if (funcNames_.count(expr->name)) {
        error(expr->pos, "no se puede usar la funcion '" + expr->name + "' como un valor");
        expr->sym = IdentExpr::Sym::Function;
        return makeTypeUnknown();
    }
    if (declaredInFunc_.count(expr->name)) {
        error(expr->pos, "uso antes de definicion: la variable '" + expr->name +
                        "' todavia no esta declarada");
    } else if (globalNames_.count(expr->name)) {
        error(expr->pos, "uso antes de definicion: el global '" + expr->name +
                        "' se declara despues");
    } else {
        error(expr->pos, "variable no declarada: '" + expr->name + "'");
    }
    expr->sym = IdentExpr::Sym::None;
    return makeTypeUnknown();
}

TypeRefPtr Sema::analyzeField(FieldExpr* expr, FuncCtx& ctx) {
    /* 'ns.fun' solo tiene sentido dentro de una llamada: se resuelve alli. */
    if (auto* id = dynamic_cast<IdentExpr*>(expr->base.get())) {
        if (isNativeNamespace(id->name)) {
            id->sym = IdentExpr::Sym::Namespace;
            error(expr->pos, "no se puede usar el namespace '" + id->name +
                            "' como un valor");
            return makeTypeUnknown();
        }
    }
    TypeRefPtr baseType = analyzeExpr(expr->base.get(), ctx);
    if (!baseType || baseType->kind == TypeRef::Kind::Unknown) return makeTypeUnknown();
    if (baseType->kind != TypeRef::Kind::Struct) {
        error(expr->pos, "el tipo '" + baseType->describe() + "' no tiene campos");
        return makeTypeUnknown();
    }
    const StructDef* def = baseType->def;
    if (!def) {
        error(expr->pos, "el tipo '" + baseType->describe() + "' no tiene campos");
        return makeTypeUnknown();
    }
    for (const StructDef::Field& f : def->fields) {
        if (f.name == expr->name) {
            expr->offset = f.offset;
            expr->resolved = true;
            return f.type ? f.type : makeTypeUnknown();
        }
    }
    error(expr->pos, "el struct '" + def->name + "' no tiene el campo '" + expr->name + "'");
    return makeTypeUnknown();
}

TypeRefPtr Sema::analyzeIndex(IndexExpr* expr, FuncCtx& ctx) {
    TypeRefPtr baseType = analyzeExpr(expr->base.get(), ctx);
    TypeRefPtr idxType = analyzeExpr(expr->index.get(), ctx);
    if (idxType && idxType->kind != TypeRef::Kind::Int &&
        idxType->kind != TypeRef::Kind::Unknown) {
        error(expr->index->pos, "el indice de un array debe ser de tipo 'int' (es '" +
                                idxType->describe() + "')");
    }
    if (!baseType || baseType->kind == TypeRef::Kind::Unknown) return makeTypeUnknown();
    if (baseType->kind == TypeRef::Kind::String) {
        /* Los caracteres de una cadena se leen como 'int'. */
        return makeTypeInt();
    }
    if (baseType->kind == TypeRef::Kind::Array || baseType->kind == TypeRef::Kind::Pointer) {
        return baseType->elem ? baseType->elem : makeTypeUnknown();
    }
    error(expr->pos, "no se puede indexar un valor de tipo '" + baseType->describe() + "'");
    return makeTypeUnknown();
}

TypeRefPtr Sema::analyzeUnary(UnaryExpr* expr, FuncCtx& ctx) {
    TypeRefPtr t = analyzeExpr(expr->operand.get(), ctx);
    if (!t || t->kind == TypeRef::Kind::Unknown) return makeTypeUnknown();
    switch (expr->op) {
        case '-':
            if (t->kind != TypeRef::Kind::Int) {
                error(expr->pos, "el operador '-' solo se puede aplicar a un 'int' (es '" +
                                 t->describe() + "')");
                return makeTypeUnknown();
            }
            return makeTypeInt();
        case '!':
            if (t->kind != TypeRef::Kind::Bool) {
                error(expr->pos, "el operador '!' solo se puede aplicar a un 'bool' (es '" +
                                 t->describe() + "')");
                return makeTypeUnknown();
            }
            return makeTypeBool();
        case '*':
            if (t->kind != TypeRef::Kind::Pointer) {
                error(expr->pos, "el operador '*' solo se puede aplicar a un puntero (es '" +
                                 t->describe() + "')");
                return makeTypeUnknown();
            }
            return t->elem ? t->elem : makeTypeUnknown();
        case '&':
            return makeTypePointer(t);
        default:
            error(expr->pos, "operador desconocido");
            return makeTypeUnknown();
    }
}

TypeRefPtr Sema::analyzeBinary(BinaryExpr* expr, FuncCtx& ctx) {
    TypeRefPtr l = analyzeExpr(expr->lhs.get(), ctx);
    TypeRefPtr r = analyzeExpr(expr->rhs.get(), ctx);
    if (!l || !r) return makeTypeUnknown();
    if (l->kind == TypeRef::Kind::Unknown || r->kind == TypeRef::Kind::Unknown) {
        return makeTypeUnknown();
    }

    switch (expr->op) {
        case BinOp::And:
        case BinOp::Or:
            if (l->kind != TypeRef::Kind::Bool || r->kind != TypeRef::Kind::Bool) {
                error(expr->pos, std::string("el operador '") + binOpName(expr->op) +
                                "' solo se puede aplicar a dos 'bool'");
                return makeTypeUnknown();
            }
            return makeTypeBool();
        case BinOp::Eq:
        case BinOp::Ne: {
            bool lStr = (l->kind == TypeRef::Kind::String);
            bool rStr = (r->kind == TypeRef::Kind::String);
            if (lStr != rStr) {
                error(expr->pos, "no se pueden comparar valores de tipos distintos ('" +
                                 l->describe() + "' y '" + r->describe() + "')");
                return makeTypeUnknown();
            }
            if (!lStr && l->kind != r->kind) {
                error(expr->pos, "no se pueden comparar valores de tipos distintos ('" +
                                 l->describe() + "' y '" + r->describe() + "')");
                return makeTypeUnknown();
            }
            return makeTypeBool();
        }
        case BinOp::Lt:
        case BinOp::Le:
        case BinOp::Gt:
        case BinOp::Ge:
            if (l->kind == TypeRef::Kind::String && r->kind == TypeRef::Kind::String) {
                /* Las cadenas se comparan lexicograficamente. */
                return makeTypeBool();
            }
            if (l->kind != TypeRef::Kind::Int || r->kind != TypeRef::Kind::Int) {
                error(expr->pos, "las comparaciones numericas solo admiten 'int' ('" +
                                 l->describe() + "' y '" + r->describe() + "')");
                return makeTypeUnknown();
            }
            return makeTypeBool();
        case BinOp::Add: {
            if (l->kind == TypeRef::Kind::String && r->kind == TypeRef::Kind::String) {
                return makeTypeString();
            }
            if (l->kind == TypeRef::Kind::String || r->kind == TypeRef::Kind::String) {
                error(expr->pos, "el operador '+' con cadenas solo admite dos 'string' ('" +
                                 l->describe() + "' y '" + r->describe() + "')");
                return makeTypeUnknown();
            }
            if (l->kind != TypeRef::Kind::Int || r->kind != TypeRef::Kind::Int) {
                error(expr->pos, "el operador '+' solo se puede aplicar a 'int' o a dos "
                                 "'string' ('" + l->describe() + "' y '" + r->describe() + "')");
                return makeTypeUnknown();
            }
            return makeTypeInt();
        }
        case BinOp::Sub:
        case BinOp::Mul:
        case BinOp::Div:
        case BinOp::Mod:
            if (l->kind != TypeRef::Kind::Int || r->kind != TypeRef::Kind::Int) {
                error(expr->pos, std::string("el operador '") + binOpName(expr->op) +
                                "' solo se puede aplicar a 'int' ('" + l->describe() +
                                "' y '" + r->describe() + "')");
                return makeTypeUnknown();
            }
            return makeTypeInt();
        default:
            break;
    }
    error(expr->pos, "operador desconocido");
    return makeTypeUnknown();
}

TypeRefPtr Sema::analyzeCall(CallExpr* expr, FuncCtx& ctx) {
    /* Llamada a un native: 'ns.fun(...)' */
    if (auto* field = dynamic_cast<FieldExpr*>(expr->callee.get())) {
        if (auto* id = dynamic_cast<IdentExpr*>(field->base.get())) {
            if (isNativeNamespace(id->name)) {
                id->sym = IdentExpr::Sym::Namespace;
                if (!importedNs_.count(id->name)) {
                    error(expr->pos, "el namespace '" + id->name +
                                    "' no esta importado (añade 'import " + id->name + ";')");
                    return makeTypeUnknown();
                }
                const NativeFunc* nat = findNative(id->name, field->name);
                if (!nat) {
                    error(expr->pos, "el namespace '" + id->name + "' no tiene la funcion '" +
                                    field->name + "'");
                    return makeTypeUnknown();
                }
                if (expr->args.size() != nat->params.size()) {
                    error(expr->pos, "'" + id->name + "." + field->name + "' espera " +
                                    std::to_string(nat->params.size()) + " argumento(s) y recibe " +
                                    std::to_string(expr->args.size()));
                    return makeTypeUnknown();
                }
                for (size_t i = 0; i < expr->args.size(); i++) {
                    TypeRefPtr at = analyzeExpr(expr->args[i].get(), ctx);
                    checkAssign(nat->params[i].get(), at.get(), expr->args[i]->pos,
                                "el argumento " + std::to_string(i + 1) + " de '" +
                                id->name + "." + field->name + "'");
                }
                expr->kind = CallExpr::Kind::Native;
                expr->nativeId = nat->id;
                field->resolved = true;
                std::string full = id->name + "." + field->name;
                if (usedNatives_.insert(full).second) {
                    Program::NativeUse use;
                    use.name = full;
                    use.id = nat->id;
                    prog_.nativeUses.push_back(use);
                }
                return nat->ret ? nat->ret : makeTypeVoid();
            }
        }
    }

    /* Llamada a una funcion del programa. */
    auto* id = dynamic_cast<IdentExpr*>(expr->callee.get());
    if (!id) {
        error(expr->pos, "no se puede llamar a esta expresion");
        for (ExprPtr& a : expr->args) analyzeExpr(a.get(), ctx);
        return makeTypeUnknown();
    }
    auto it = funcIndex_.find(id->name);
    if (it == funcIndex_.end()) {
        error(expr->pos, "funcion no declarada: '" + id->name + "'");
        for (ExprPtr& a : expr->args) analyzeExpr(a.get(), ctx);
        return makeTypeUnknown();
    }
    FuncDecl& fn = prog_.funcs[(size_t)it->second];
    id->sym = IdentExpr::Sym::Function;
    id->index = it->second;

    if (expr->args.size() != fn.params.size()) {
        error(expr->pos, "'" + fn.name + "' espera " + std::to_string(fn.params.size()) +
                        " argumento(s) y recibe " + std::to_string(expr->args.size()));
        for (ExprPtr& a : expr->args) analyzeExpr(a.get(), ctx);
        return makeTypeUnknown();
    }
    for (size_t i = 0; i < expr->args.size(); i++) {
        TypeRefPtr at = analyzeExpr(expr->args[i].get(), ctx);
        checkAssign(fn.params[i].type.get(), at.get(), expr->args[i]->pos,
                    "el argumento " + std::to_string(i + 1) + " de '" + fn.name + "'");
    }
    expr->kind = CallExpr::Kind::User;
    expr->funcIndex = it->second;
    return fn.retType ? fn.retType : makeTypeVoid();
}

TypeRefPtr Sema::analyzeAssign(Expr* target, BinOp op, Expr* value, FuncCtx& ctx) {
    if (!target) return makeTypeUnknown();
    TypeRefPtr tt = analyzeExpr(target, ctx);
    TypeRefPtr vt = analyzeExpr(value, ctx);
    if (!tt || tt->kind == TypeRef::Kind::Unknown) return makeTypeUnknown();
    if (!vt || vt->kind == TypeRef::Kind::Unknown) return tt;

    /* El destino debe ser modificable. */
    bool assignable = false;
    if (auto* id = dynamic_cast<IdentExpr*>(target)) {
        assignable = (id->sym == IdentExpr::Sym::Local || id->sym == IdentExpr::Sym::Global);
        const Symbol* sym = lookup(id->name);
        if (sym && sym->isLet) {
            error(target->pos, "'" + id->name +
                            "' es constante y no se puede reasignar");
            return tt;
        }
    } else if (dynamic_cast<FieldExpr*>(target)) {
        assignable = true;
    } else if (dynamic_cast<IndexExpr*>(target)) {
        assignable = true;
        const auto* ix = dynamic_cast<const IndexExpr*>(target);
        if (ix && ix->base && ix->base->type &&
            ix->base->type->kind == TypeRef::Kind::String) {
            error(target->pos, "no se puede asignar a un caracter de una cadena");
            return tt;
        }
    } else if (auto* u = dynamic_cast<UnaryExpr*>(target)) {
        assignable = (u->op == '*');
    }
    if (!assignable) {
        error(target->pos, "este valor no puede usarse como destino de una asignacion");
        return tt;
    }

    if (op == BinOp::Assign) {
        checkAssign(tt.get(), vt.get(), value->pos, "la variable");
        return tt;
    }

    /* Operadores compuestos: '+=' admite int y string. */
    if (tt->kind == TypeRef::Kind::String) {
        if (op != BinOp::AddAssign) {
            error(target->pos, "el operador '" + std::string(binOpName(op)) +
                            "' no se puede aplicar a un 'string'");
            return tt;
        }
        if (vt->kind != TypeRef::Kind::String) {
            error(value->pos, "no se puede asignar un 'string' a un valor de tipo '" +
                              tt->describe() + "'");
        }
        return tt;
    }
    if (tt->kind != TypeRef::Kind::Int || vt->kind != TypeRef::Kind::Int) {
        error(value->pos, "el operador '" + std::string(binOpName(op)) +
                          "' solo se puede aplicar a 'int' ('" + tt->describe() + "' y '" +
                          vt->describe() + "')");
    }
    return tt;
}

TypeRefPtr Sema::analyzeNew(NewExpr* expr, FuncCtx& ctx) {
    (void)ctx;
    resolveTypeNames(expr->type, expr->pos);
    if (!expr->type) return makeTypeUnknown();
    switch (expr->type->kind) {
        case TypeRef::Kind::Struct:
        case TypeRef::Kind::Array:
            return expr->type;
        case TypeRef::Kind::Unknown:
            return makeTypeUnknown();
        default:
            return makeTypePointer(expr->type);
    }
}

/* ------------------------------------------------------------------ */
/* Tipos                                                              */
/* ------------------------------------------------------------------ */
bool Sema::typesCompatible(const TypeRef* a, const TypeRef* b) const {
    if (!a || !b) return true;
    if (a->kind == TypeRef::Kind::Unknown || b->kind == TypeRef::Kind::Unknown) return true;
    if (a->kind != b->kind) return false;
    switch (a->kind) {
        case TypeRef::Kind::Int:
        case TypeRef::Kind::Bool:
        case TypeRef::Kind::String:
        case TypeRef::Kind::Void:
            return true;
        case TypeRef::Kind::Pointer:
            return typesCompatible(a->elem.get(), b->elem.get());
        case TypeRef::Kind::Array:
            return a->count == b->count && typesCompatible(a->elem.get(), b->elem.get());
        case TypeRef::Kind::Struct:
            return a->structName == b->structName;
        default:
            return true;
    }
}

bool Sema::checkAssign(const TypeRef* target, const TypeRef* value, const SourcePos& pos,
                       const std::string& what) {
    if (!target || !value) return true;
    if (target->kind == TypeRef::Kind::Unknown || value->kind == TypeRef::Kind::Unknown) {
        return true;
    }
    if (typesCompatible(target, value)) return true;
    error(pos, "tipo incompatible: " + what + " es de tipo '" + target->describe() +
                "' pero el valor es de tipo '" + value->describe() + "'");
    return false;
}

/* ------------------------------------------------------------------ */
/* Entrada principal                                                  */
/* ------------------------------------------------------------------ */
bool Sema::run(std::vector<Diag>& diags) {
    pushScope();   /* ambito global */
    collectStructs();
    collectFunctions();

    for (const ImportDecl& imp : prog_.imports) {
        uint32_t first = 0, count = 0;
        if (!nativeNamespaceRange(imp.ns, first, count)) {
            error(imp.pos, "namespace desconocido: '" + imp.ns +
                           "' (disponibles: core, console, fs, proc, mem, time)");
            continue;
        }
        if (!importedNs_.insert(imp.ns).second) {
            warn(imp.pos, "el namespace '" + imp.ns + "' ya estaba importado");
        }
    }

    /* Se recorren las declaraciones de arriba abajo para detectar usos
     * antes de definicion. */
    struct Item {
        int order;
        bool isFunc;
        size_t index;
    };
    std::vector<Item> items;
    for (size_t i = 0; i < prog_.globals.size(); i++) {
        items.push_back({ prog_.globals[i].order, false, i });
    }
    for (size_t i = 0; i < prog_.funcs.size(); i++) {
        items.push_back({ prog_.funcs[i].order, true, i });
    }
    std::stable_sort(items.begin(), items.end(),
                     [](const Item& a, const Item& b) { return a.order < b.order; });

    FuncCtx globalCtx;
    globalCtx.retType = makeTypeVoid();

    int nextSlot = 0;
    for (const Item& item : items) {
        if (item.isFunc) {
            analyzeFunction(prog_.funcs[item.index]);
        } else {
            GlobalDecl& g = prog_.globals[item.index];
            resolveTypeNames(g.type, g.pos);
            if (g.init) {
                TypeRefPtr t = analyzeExpr(g.init.get(), globalCtx);
                checkAssign(g.type.get(), t.get(), g.init->pos, "el global '" + g.name + "'");
            } else if (g.isLet) {
                error(g.pos, "falta el valor inicial del global '" + g.name + "'");
            }
            Symbol sym;
            sym.kind = Symbol::Kind::Global;
            sym.type = g.type;
            sym.index = nextSlot++;
            sym.isLet = g.isLet;
            sym.line = g.pos.line;
            g.slot = sym.index;
            declare(g.name, sym, g.pos);
        }
    }

    /* La funcion main es obligatoria. */
    auto mainIt = funcIndex_.find("main");
    if (mainIt == funcIndex_.end()) {
        Diag d(1, 1, "el programa debe definir la funcion 'fn main()'");
        diags_.emplace_back(d);
        hasError_ = true;
    } else {
        FuncDecl& fn = prog_.funcs[(size_t)mainIt->second];
        if (!fn.params.empty()) {
            error(fn.pos, "'main' no puede tener parametros");
        }
    }

    for (const Diag& d : diags_) diags.push_back(d);
    return !hasError_;
}

} /* namespace axc */
