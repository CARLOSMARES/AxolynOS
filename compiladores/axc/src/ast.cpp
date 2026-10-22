/*
 * AxolynOS - Utilidades del AST: volcado legible para '--dump-ast'.
 */
#include "axc/ast.h"
#include "axc/axc.h"

#include <ostream>
#include <sstream>

namespace axc {

namespace {

void indent(std::ostream& os, int n) {
    for (int i = 0; i < n; i++) os << "  ";
}

std::string typeText(const TypeRefPtr& t) {
    if (!t) return "?";
    return t->describe();
}

const char* symName(IdentExpr::Sym sym) {
    switch (sym) {
        case IdentExpr::Sym::None:       return "sin resolver";
        case IdentExpr::Sym::Local:      return "local";
        case IdentExpr::Sym::Global:     return "global";
        case IdentExpr::Sym::Function:   return "funcion";
        case IdentExpr::Sym::Namespace:  return "namespace";
        case IdentExpr::Sym::StructType: return "tipo";
    }
    return "?";
}

void dumpExpr(std::ostream& os, const Expr* e, int ind);

void dumpChildren(std::ostream& os, const Expr* e, int ind) {
    if (!e) return;
    if (const auto* b = dynamic_cast<const BinaryExpr*>(e)) {
        dumpExpr(os, b->lhs.get(), ind);
        dumpExpr(os, b->rhs.get(), ind);
    } else if (const auto* u = dynamic_cast<const UnaryExpr*>(e)) {
        dumpExpr(os, u->operand.get(), ind);
    } else if (const auto* f = dynamic_cast<const FieldExpr*>(e)) {
        dumpExpr(os, f->base.get(), ind);
    } else if (const auto* i = dynamic_cast<const IndexExpr*>(e)) {
        dumpExpr(os, i->base.get(), ind);
        dumpExpr(os, i->index.get(), ind);
    } else if (const auto* c = dynamic_cast<const CallExpr*>(e)) {
        dumpExpr(os, c->callee.get(), ind);
        for (const ExprPtr& a : c->args) dumpExpr(os, a.get(), ind);
    } else if (const auto* a = dynamic_cast<const AssignExpr*>(e)) {
        dumpExpr(os, a->target.get(), ind);
        dumpExpr(os, a->value.get(), ind);
    }
}

void dumpExpr(std::ostream& os, const Expr* e, int ind) {
    if (!e) {
        indent(os, ind);
        os << "<expresion nula>\n";
        return;
    }
    indent(os, ind);
    if (const auto* v = dynamic_cast<const IntExpr*>(e)) {
        os << "Entero " << v->value;
    } else if (const auto* v = dynamic_cast<const BoolExpr*>(e)) {
        os << "Booleano " << (v->value ? "true" : "false");
    } else if (const auto* v = dynamic_cast<const StringExpr*>(e)) {
        os << "Cadena \"" << v->value << "\"";
    } else if (const auto* v = dynamic_cast<const IdentExpr*>(e)) {
        os << "Identificador " << v->name << " (" << symName(v->sym);
        if (v->sym == IdentExpr::Sym::Local || v->sym == IdentExpr::Sym::Global) {
            os << ", celda " << v->index;
        }
        os << ")";
    } else if (const auto* v = dynamic_cast<const FieldExpr*>(e)) {
        os << "Campo ." << v->name << " (offset " << v->offset << ")";
    } else if (dynamic_cast<const IndexExpr*>(e)) {
        os << "Indexacion []";
    } else if (const auto* v = dynamic_cast<const UnaryExpr*>(e)) {
        os << "Unario " << v->op;
    } else if (const auto* v = dynamic_cast<const BinaryExpr*>(e)) {
        os << "Binario " << binOpName(v->op);
    } else if (const auto* v = dynamic_cast<const CallExpr*>(e)) {
        os << "Llamada ";
        if (v->kind == CallExpr::Kind::Native) {
            os << "nativa id=" << v->nativeId;
        } else if (v->kind == CallExpr::Kind::User) {
            os << "funcion indice=" << v->funcIndex;
        } else {
            os << "sin resolver";
        }
        os << " (" << v->args.size() << " argumento(s))";
    } else if (const auto* v = dynamic_cast<const AssignExpr*>(e)) {
        os << "Asignacion " << binOpName(v->op);
    } else if (const auto* v = dynamic_cast<const NewExpr*>(e)) {
        os << "Nuevo " << typeText(v->type);
    } else {
        os << "Expresion";
    }
    os << " : " << typeText(e->type) << "\n";
    dumpChildren(os, e, ind + 1);
}

void dumpStmt(std::ostream& os, const Stmt* s, int ind);

void dumpBody(std::ostream& os, const std::vector<StmtPtr>& body, int ind) {
    for (const StmtPtr& s : body) {
        if (s) dumpStmt(os, s.get(), ind);
    }
}

void dumpStmt(std::ostream& os, const Stmt* s, int ind) {
    if (!s) return;
    indent(os, ind);
    if (const auto* v = dynamic_cast<const VarDeclStmt*>(s)) {
        os << (v->isLet ? "Constante " : "Variable ") << v->name << " : "
           << typeText(v->type) << " (celda " << v->slot << ")\n";
        if (v->init) dumpExpr(os, v->init.get(), ind + 1);
    } else if (const auto* e = dynamic_cast<const ExprStmt*>(s)) {
        os << "Expresion\n";
        dumpExpr(os, e->expr.get(), ind + 1);
    } else if (const auto* a = dynamic_cast<const AssignStmt*>(s)) {
        os << "Asignacion " << binOpName(a->op) << "\n";
        dumpExpr(os, a->target.get(), ind + 1);
        dumpExpr(os, a->value.get(), ind + 1);
    } else if (const auto* b = dynamic_cast<const BlockStmt*>(s)) {
        os << "Bloque\n";
        dumpBody(os, b->body, ind + 1);
    } else if (const auto* i = dynamic_cast<const IfStmt*>(s)) {
        os << "Si\n";
        dumpExpr(os, i->cond.get(), ind + 1);
        os << "  entonces:\n";
        dumpBody(os, i->thenBody, ind + 2);
        if (i->hasElse) {
            os << "  si no:\n";
            dumpBody(os, i->elseBody, ind + 2);
        }
    } else if (const auto* w = dynamic_cast<const WhileStmt*>(s)) {
        os << "Mientras\n";
        dumpExpr(os, w->cond.get(), ind + 1);
        dumpBody(os, w->body, ind + 1);
    } else if (const auto* f = dynamic_cast<const ForStmt*>(s)) {
        os << "Para\n";
        if (f->initVar) {
            indent(os, ind + 1);
            os << "inicializador: " << f->initVar->name << " : "
               << typeText(f->initVar->type) << " (celda " << f->initVar->slot << ")\n";
            if (f->initVar->init) dumpExpr(os, f->initVar->init.get(), ind + 2);
        } else if (f->initExpr) {
            indent(os, ind + 1);
            os << "inicializador:\n";
            dumpExpr(os, f->initExpr.get(), ind + 2);
        }
        indent(os, ind + 1);
        os << "condicion:\n";
        dumpExpr(os, f->cond.get(), ind + 2);
        indent(os, ind + 1);
        os << "paso:\n";
        dumpExpr(os, f->step.get(), ind + 2);
        dumpBody(os, f->body, ind + 1);
    } else if (const auto* r = dynamic_cast<const ReturnStmt*>(s)) {
        os << "Devolver\n";
        if (r->value) dumpExpr(os, r->value.get(), ind + 1);
    } else if (dynamic_cast<const BreakStmt*>(s)) {
        os << "Break\n";
    } else if (dynamic_cast<const ContinueStmt*>(s)) {
        os << "Continue\n";
    } else {
        os << "Sentencia\n";
    }
}

} /* namespace */

/* ------------------------------------------------------------------ */
/* Volcado del AST                                                     */
/* ------------------------------------------------------------------ */
void dumpAst(const Program& prog, std::ostream& os) {
    os << "=== AST de Axolyn ===\n";

    os << "\n-- imports (" << prog.imports.size() << ") --\n";
    for (const ImportDecl& imp : prog.imports) {
        os << "  import " << imp.ns << "   (linea " << imp.pos.line << ")\n";
    }

    os << "\n-- structs (" << prog.structs.size() << ") --\n";
    for (const StructDef& def : prog.structs) {
        os << "  struct " << def.name << " (" << def.size << " bytes)\n";
        for (const StructDef::Field& f : def.fields) {
            os << "    " << f.name << " : " << typeText(f.type)
               << "  (offset " << f.offset << ")\n";
        }
    }

    os << "\n-- globales (" << prog.globals.size() << ") --\n";
    for (const GlobalDecl& g : prog.globals) {
        os << "  " << (g.isLet ? "const " : "var ") << g.name << " : "
           << typeText(g.type) << "  (celda " << g.slot << ")\n";
        if (g.init) dumpExpr(os, g.init.get(), 2);
    }

    os << "\n-- funciones (" << prog.funcs.size() << ") --\n";
    for (const FuncDecl& fn : prog.funcs) {
        os << "  fn " << fn.name << "(";
        for (size_t i = 0; i < fn.params.size(); i++) {
            if (i) os << ", ";
            os << fn.params[i].name << " : " << typeText(fn.params[i].type)
               << " (celda " << fn.params[i].slot << ")";
        }
        os << ") : " << typeText(fn.retType)
           << "  [indice " << fn.index << ", locales " << fn.localCount << "]\n";
        dumpBody(os, fn.body, 2);
    }

    os << "\n-- natives usadas (" << prog.nativeUses.size() << ") --\n";
    for (const Program::NativeUse& n : prog.nativeUses) {
        os << "  " << n.name << " (id " << n.id << ")\n";
    }
}

} /* namespace axc */
