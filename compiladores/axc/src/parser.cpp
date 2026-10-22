/*
 * AxolynOS - Parser descendente recursivo del lenguaje Axolyn.
 */
#include "axc/parser.h"

#include <utility>

namespace axc {

/* ------------------------------------------------------------------ */
/* Utilidades                                                         */
/* ------------------------------------------------------------------ */
Parser::Parser(const std::string& source, std::string fileName)
    : lexer_(source, std::move(fileName)) {
    /* Se carga el primer token: 'bufPos_' apunta a el. */
    peek(0);
}

const Token& Parser::peek(size_t ahead) {
    while (buf_.size() <= bufPos_ + ahead) {
        if (lexerFailed_) {
            Token t;
            t.type = TokenType::End;
            t.line = buf_.empty() ? 1 : buf_.back().line;
            t.column = buf_.empty() ? 1 : buf_.back().column;
            buf_.push_back(t);
            continue;
        }
        Token t;
        if (!lexer_.next(t)) {
            lexerFailed_ = true;
            for (const Diag& d : lexer_.diagnostics()) diags_.push_back(d);
            t = Token();
            t.type = TokenType::End;
            buf_.push_back(t);
            continue;
        }
        buf_.push_back(t);
    }
    return buf_[bufPos_ + ahead];
}

void Parser::advance() {
    peek(1);
    bufPos_++;
}

bool Parser::match(TokenType t) {
    if (cur().type != t) return false;
    advance();
    return true;
}

bool Parser::expect(TokenType t, const char* what) {
    if (cur().type == t) {
        advance();
        return true;
    }
    return failHere(std::string("se esperaba ") + what + " pero se encontro " +
                    tokenTypeName(cur().type));
}

bool Parser::fail(const Token& tok, const std::string& msg) {
    diags_.emplace_back(tok.line, tok.column, msg);
    failed_ = true;
    return false;
}

void Parser::skipToStatementEnd() {
    int depth = 0;
    while (!atEnd()) {
        TokenType t = cur().type;
        if (t == TokenType::LBrace || t == TokenType::LParen || t == TokenType::LBracket) depth++;
        if (t == TokenType::RBrace || t == TokenType::RParen || t == TokenType::RBracket) {
            if (depth == 0) return;
            depth--;
        }
        if (t == TokenType::Semi && depth == 0) { advance(); return; }
        if (t == TokenType::RBrace && depth == 0) return;
        advance();
    }
}

/* ------------------------------------------------------------------ */
/* Programa                                                            */
/* ------------------------------------------------------------------ */
bool Parser::parseProgram(Program& out) {
    while (!atEnd()) {
        if (failed_) return false;
        switch (cur().type) {
            case TokenType::KwImport:
                if (!parseImport(out)) return false;
                break;
            case TokenType::KwFn:
                if (!parseFunction(out)) return false;
                break;
            case TokenType::KwStruct:
                if (!parseStruct(out)) return false;
                break;
            case TokenType::KwVar:
            case TokenType::KwLet:
                if (!parseGlobal(out)) return false;
                break;
            default:
                return failHere("se esperaba 'import', 'fn', 'struct', 'var' o 'let' pero se encontro " +
                                std::string(tokenTypeName(cur().type)));
        }
    }
    return !failed_;
}

bool Parser::parseImport(Program& prog) {
    Token start = cur();
    advance(); /* 'import' */
    if (!check(TokenType::Ident)) {
        return failHere("se esperaba el nombre de un namespace tras 'import'");
    }
    ImportDecl decl;
    decl.ns = cur().text;
    decl.pos = SourcePos{ start.line, start.column };
    advance();
    if (!expect(TokenType::Semi, "';'")) return false;
    prog.imports.push_back(std::move(decl));
    return true;
}

bool Parser::parseStruct(Program& prog) {
    Token start = cur();
    advance(); /* 'struct' */
    if (!check(TokenType::Ident)) {
        return failHere("se esperaba el nombre del struct tras 'struct'");
    }
    StructDef def;
    def.name = cur().text;
    def.pos = SourcePos{ start.line, start.column };
    advance();
    if (!expect(TokenType::LBrace, "'{'")) return false;

    if (!check(TokenType::RBrace)) {
        while (true) {
            if (!check(TokenType::Ident)) {
                return failHere("se esperaba el nombre de un campo");
            }
            StructDef::Field field;
            field.name = cur().text;
            advance();
            if (!expect(TokenType::Colon, "':'")) return false;
            if (!parseTypeInto(field.type)) return false;
            def.fields.push_back(std::move(field));
            if (match(TokenType::Comma)) {
                /* Se admite la coma final antes de '}'. */
                if (check(TokenType::RBrace)) break;
                continue;
            }
            break;
        }
    }
    if (!expect(TokenType::RBrace, "'}'")) return false;
    prog.structs.push_back(std::move(def));
    return true;
}

bool Parser::parseFunction(Program& prog) {
    Token start = cur();
    advance(); /* 'fn' */
    if (!check(TokenType::Ident)) {
        return failHere("se esperaba el nombre de la funcion tras 'fn'");
    }
    FuncDecl fn;
    fn.name = cur().text;
    fn.pos = SourcePos{ start.line, start.column };
    fn.order = order_++;
    advance();
    if (!expect(TokenType::LParen, "'('")) return false;
    if (!check(TokenType::RParen)) {
        while (true) {
            if (!check(TokenType::Ident)) {
                return failHere("se esperaba el nombre de un parametro");
            }
            FuncDecl::Param param;
            param.name = cur().text;
            advance();
            if (!expect(TokenType::Colon, "':'")) return false;
            if (!parseTypeInto(param.type)) return false;
            fn.params.push_back(std::move(param));
            if (match(TokenType::Comma)) continue;
            break;
        }
    }
    if (!expect(TokenType::RParen, "')'")) return false;
    if (match(TokenType::Colon)) {
        if (!parseTypeInto(fn.retType)) return false;
    }
    if (!parseBlock(fn.body)) return false;
    prog.funcs.push_back(std::move(fn));
    return true;
}

bool Parser::parseGlobal(Program& prog) {
    Token start = cur();
    bool isLet = cur().type == TokenType::KwLet;
    advance();
    if (!check(TokenType::Ident)) {
        return failHere("se esperaba el nombre de la variable");
    }
    GlobalDecl decl;
    decl.isLet = isLet;
    decl.name = cur().text;
    decl.pos = SourcePos{ start.line, start.column };
    decl.order = order_++;
    advance();
    if (!expect(TokenType::Colon, "':'")) return false;
    if (!parseTypeInto(decl.type)) return false;
    if (match(TokenType::Assign)) {
        decl.init = parseExpr();
        if (!decl.init) return false;
    }
    if (!expect(TokenType::Semi, "';'")) return false;
    prog.globals.push_back(std::move(decl));
    return true;
}

/* ------------------------------------------------------------------ */
/* Sentencias                                                          */
/* ------------------------------------------------------------------ */
bool Parser::parseBlock(std::vector<StmtPtr>& out) {
    if (!expect(TokenType::LBrace, "'{'")) return false;
    while (!check(TokenType::RBrace)) {
        if (atEnd()) return failHere("falta '}' para cerrar el bloque");
        if (!parseStatement(out)) return false;
    }
    advance(); /* '}' */
    return true;
}

bool Parser::parseStatement(std::vector<StmtPtr>& out) {
    switch (cur().type) {
        case TokenType::KwVar:
        case TokenType::KwLet:
            return parseVarDecl(out, true);
        case TokenType::KwIf:
            return parseIf(out);
        case TokenType::KwWhile:
            return parseWhile(out);
        case TokenType::KwFor:
            return parseFor(out);
        case TokenType::KwReturn:
            return parseReturn(out);
        case TokenType::KwBreak:
        case TokenType::KwContinue:
            return parseBreakContinue(out);
        case TokenType::LBrace: {
            auto block = std::make_unique<BlockStmt>();
            block->pos = SourcePos{ cur().line, cur().column };
            if (!parseBlock(block->body)) return false;
            out.push_back(std::move(block));
            return true;
        }
        default: break;
    }

    /* Asignacion o expresion. */
    ExprPtr e = parseExpr();
    if (!e) return false;
    BinOp op;
    bool isAssign = true;
    switch (cur().type) {
        case TokenType::Assign:     op = BinOp::Assign; break;
        case TokenType::PlusAssign: op = BinOp::AddAssign; break;
        case TokenType::MinusAssign:op = BinOp::SubAssign; break;
        case TokenType::StarAssign: op = BinOp::MulAssign; break;
        case TokenType::SlashAssign:op = BinOp::DivAssign; break;
        case TokenType::PercentAssign: op = BinOp::ModAssign; break;
        default: isAssign = false; op = BinOp::Assign; break;
    }
    if (isAssign) {
        advance();
        auto stmt = std::make_unique<AssignStmt>();
        stmt->pos = e->pos;
        stmt->target = std::move(e);
        stmt->op = op;
        stmt->value = parseExpr();
        if (!stmt->value) return false;
        if (!expect(TokenType::Semi, "';'")) return false;
        out.push_back(std::move(stmt));
        return true;
    }

    auto stmt = std::make_unique<ExprStmt>();
    stmt->pos = e->pos;
    stmt->expr = std::move(e);
    if (!expect(TokenType::Semi, "';'")) return false;
    out.push_back(std::move(stmt));
    return true;
}

bool Parser::parseVarDecl(std::vector<StmtPtr>& out, bool consumeSemi) {
    Token start = cur();
    bool isLet = cur().type == TokenType::KwLet;
    advance();
    if (!check(TokenType::Ident)) {
        return failHere("se esperaba el nombre de la variable");
    }
    auto stmt = std::make_unique<VarDeclStmt>();
    stmt->pos = SourcePos{ start.line, start.column };
    stmt->isLet = isLet;
    stmt->name = cur().text;
    advance();
    if (!expect(TokenType::Colon, "':'")) return false;
    if (!parseTypeInto(stmt->type)) return false;
    if (match(TokenType::Assign)) {
        stmt->init = parseExpr();
        if (!stmt->init) return false;
    }
    if (consumeSemi && !expect(TokenType::Semi, "';'")) return false;
    out.push_back(std::move(stmt));
    return true;
}

bool Parser::parseIf(std::vector<StmtPtr>& out) {
    Token start = cur();
    advance(); /* 'if' */
    auto stmt = std::make_unique<IfStmt>();
    stmt->pos = SourcePos{ start.line, start.column };
    if (!expect(TokenType::LParen, "'('")) return false;
    stmt->cond = parseExpr();
    if (!stmt->cond) return false;
    if (!expect(TokenType::RParen, "')'")) return false;
    if (!parseBlock(stmt->thenBody)) return false;
    if (match(TokenType::KwElse)) {
        stmt->hasElse = true;
        if (check(TokenType::KwIf)) {
            if (!parseIf(stmt->elseBody)) return false;
        } else {
            if (!parseBlock(stmt->elseBody)) return false;
        }
    }
    out.push_back(std::move(stmt));
    return true;
}

bool Parser::parseWhile(std::vector<StmtPtr>& out) {
    Token start = cur();
    advance(); /* 'while' */
    auto stmt = std::make_unique<WhileStmt>();
    stmt->pos = SourcePos{ start.line, start.column };
    if (!expect(TokenType::LParen, "'('")) return false;
    stmt->cond = parseExpr();
    if (!stmt->cond) return false;
    if (!expect(TokenType::RParen, "')'")) return false;
    if (!parseBlock(stmt->body)) return false;
    out.push_back(std::move(stmt));
    return true;
}

bool Parser::parseFor(std::vector<StmtPtr>& out) {
    Token start = cur();
    advance(); /* 'for' */
    auto stmt = std::make_unique<ForStmt>();
    stmt->pos = SourcePos{ start.line, start.column };
    if (!expect(TokenType::LParen, "'('")) return false;

    if (check(TokenType::KwVar) || check(TokenType::KwLet)) {
        std::vector<StmtPtr> tmp;
        if (!parseVarDecl(tmp, false)) return false;
        VarDeclStmt* vd = static_cast<VarDeclStmt*>(tmp[0].release());
        stmt->initVar.reset(vd);
    } else {
        stmt->initExpr = parseExprOrAssign();
        if (!stmt->initExpr) return false;
    }
    if (!expect(TokenType::Semi, "';'")) return false;

    stmt->cond = parseExpr();
    if (!stmt->cond) return false;
    if (!expect(TokenType::Semi, "';'")) return false;

    stmt->step = parseExprOrAssign();
    if (!stmt->step) return false;
    if (!expect(TokenType::RParen, "')'")) return false;
    if (!parseBlock(stmt->body)) return false;
    out.push_back(std::move(stmt));
    return true;
}

bool Parser::parseReturn(std::vector<StmtPtr>& out) {
    Token start = cur();
    advance(); /* 'return' */
    auto stmt = std::make_unique<ReturnStmt>();
    stmt->pos = SourcePos{ start.line, start.column };
    if (!check(TokenType::Semi)) {
        stmt->value = parseExpr();
        if (!stmt->value) return false;
    }
    if (!expect(TokenType::Semi, "';'")) return false;
    out.push_back(std::move(stmt));
    return true;
}

bool Parser::parseBreakContinue(std::vector<StmtPtr>& out) {
    Token start = cur();
    bool isBreak = cur().type == TokenType::KwBreak;
    advance();
    if (!expect(TokenType::Semi, "';'")) return false;
    if (isBreak) {
        auto stmt = std::make_unique<BreakStmt>();
        stmt->pos = SourcePos{ start.line, start.column };
        out.push_back(std::move(stmt));
    } else {
        auto stmt = std::make_unique<ContinueStmt>();
        stmt->pos = SourcePos{ start.line, start.column };
        out.push_back(std::move(stmt));
    }
    return true;
}

/* ------------------------------------------------------------------ */
/* Expresiones                                                         */
/* ------------------------------------------------------------------ */
ExprPtr Parser::parseExpr() { return parseLogOr(); }

ExprPtr Parser::parseExprOrAssign() {
    ExprPtr e = parseExpr();
    if (!e) return nullptr;
    BinOp op;
    switch (cur().type) {
        case TokenType::Assign:       op = BinOp::Assign; break;
        case TokenType::PlusAssign:   op = BinOp::AddAssign; break;
        case TokenType::MinusAssign:  op = BinOp::SubAssign; break;
        case TokenType::StarAssign:   op = BinOp::MulAssign; break;
        case TokenType::SlashAssign:  op = BinOp::DivAssign; break;
        case TokenType::PercentAssign: op = BinOp::ModAssign; break;
        default: return e;
    }
    advance();
    auto assign = std::make_unique<AssignExpr>();
    assign->pos = e->pos;
    assign->target = std::move(e);
    assign->op = op;
    assign->value = parseExpr();
    if (!assign->value) return nullptr;
    return assign;
}

ExprPtr Parser::parseLogOr() {
    ExprPtr left = parseLogAnd();
    if (!left) return nullptr;
    while (check(TokenType::OrOr)) {
        SourcePos pos{ cur().line, cur().column };
        advance();
        ExprPtr right = parseLogAnd();
        if (!right) return nullptr;
        auto node = std::make_unique<BinaryExpr>();
        node->pos = SourcePos{ pos.line, pos.column };
        node->op = BinOp::Or;
        node->lhs = std::move(left);
        node->rhs = std::move(right);
        left = std::move(node);
    }
    return left;
}

ExprPtr Parser::parseLogAnd() {
    ExprPtr left = parseEquality();
    if (!left) return nullptr;
    while (check(TokenType::AndAnd)) {
        SourcePos pos{ cur().line, cur().column };
        advance();
        ExprPtr right = parseEquality();
        if (!right) return nullptr;
        auto node = std::make_unique<BinaryExpr>();
        node->pos = SourcePos{ pos.line, pos.column };
        node->op = BinOp::And;
        node->lhs = std::move(left);
        node->rhs = std::move(right);
        left = std::move(node);
    }
    return left;
}

ExprPtr Parser::parseEquality() {
    ExprPtr left = parseComparison();
    if (!left) return nullptr;
    while (check(TokenType::Eq) || check(TokenType::NotEq)) {
        BinOp op = cur().type == TokenType::Eq ? BinOp::Eq : BinOp::Ne;
        SourcePos pos{ cur().line, cur().column };
        advance();
        ExprPtr right = parseComparison();
        if (!right) return nullptr;
        auto node = std::make_unique<BinaryExpr>();
        node->pos = SourcePos{ pos.line, pos.column };
        node->op = op;
        node->lhs = std::move(left);
        node->rhs = std::move(right);
        left = std::move(node);
    }
    return left;
}

ExprPtr Parser::parseComparison() {
    ExprPtr left = parseAdditive();
    if (!left) return nullptr;
    while (check(TokenType::Lt) || check(TokenType::Le) ||
           check(TokenType::Gt) || check(TokenType::Ge)) {
        BinOp op = BinOp::Lt;
        switch (cur().type) {
            case TokenType::Lt: op = BinOp::Lt; break;
            case TokenType::Le: op = BinOp::Le; break;
            case TokenType::Gt: op = BinOp::Gt; break;
            default:           op = BinOp::Ge; break;
        }
        SourcePos pos{ cur().line, cur().column };
        advance();
        ExprPtr right = parseAdditive();
        if (!right) return nullptr;
        auto node = std::make_unique<BinaryExpr>();
        node->pos = SourcePos{ pos.line, pos.column };
        node->op = op;
        node->lhs = std::move(left);
        node->rhs = std::move(right);
        left = std::move(node);
    }
    return left;
}

ExprPtr Parser::parseAdditive() {
    ExprPtr left = parseMultiplicative();
    if (!left) return nullptr;
    while (check(TokenType::Plus) || check(TokenType::Minus)) {
        BinOp op = cur().type == TokenType::Plus ? BinOp::Add : BinOp::Sub;
        SourcePos pos{ cur().line, cur().column };
        advance();
        ExprPtr right = parseMultiplicative();
        if (!right) return nullptr;
        auto node = std::make_unique<BinaryExpr>();
        node->pos = SourcePos{ pos.line, pos.column };
        node->op = op;
        node->lhs = std::move(left);
        node->rhs = std::move(right);
        left = std::move(node);
    }
    return left;
}

ExprPtr Parser::parseMultiplicative() {
    ExprPtr left = parseUnary();
    if (!left) return nullptr;
    while (check(TokenType::Star) || check(TokenType::Slash) || check(TokenType::Percent)) {
        BinOp op = BinOp::Mul;
        switch (cur().type) {
            case TokenType::Star:    op = BinOp::Mul; break;
            case TokenType::Slash:   op = BinOp::Div; break;
            default:                 op = BinOp::Mod; break;
        }
        SourcePos pos{ cur().line, cur().column };
        advance();
        ExprPtr right = parseUnary();
        if (!right) return nullptr;
        auto node = std::make_unique<BinaryExpr>();
        node->pos = SourcePos{ pos.line, pos.column };
        node->op = op;
        node->lhs = std::move(left);
        node->rhs = std::move(right);
        left = std::move(node);
    }
    return left;
}

ExprPtr Parser::parseUnary() {
    TokenType t = cur().type;
    if (t == TokenType::Minus || t == TokenType::Bang || t == TokenType::Star ||
        t == TokenType::Amp) {
        char op = (t == TokenType::Minus) ? '-' : (t == TokenType::Bang) ? '!'
              : (t == TokenType::Star) ? '*' : '&';
        SourcePos pos{ cur().line, cur().column };
        advance();
        ExprPtr operand = parseUnary();
        if (!operand) return nullptr;
        auto node = std::make_unique<UnaryExpr>();
        node->pos = SourcePos{ pos.line, pos.column };
        node->op = op;
        node->operand = std::move(operand);
        return node;
    }
    return parsePostfix();
}

ExprPtr Parser::parsePostfix() {
    ExprPtr expr = parsePrimary();
    if (!expr) return nullptr;
    while (true) {
        if (check(TokenType::LBracket)) {
            SourcePos pos{ cur().line, cur().column };
            advance();
            ExprPtr index = parseExpr();
            if (!index) return nullptr;
            if (!expect(TokenType::RBracket, "']'")) return nullptr;
            auto node = std::make_unique<IndexExpr>();
            node->pos = SourcePos{ pos.line, pos.column };
            node->base = std::move(expr);
            node->index = std::move(index);
            expr = std::move(node);
        } else if (check(TokenType::LParen)) {
            SourcePos pos{ cur().line, cur().column };
            advance();
            auto node = std::make_unique<CallExpr>();
            node->pos = SourcePos{ pos.line, pos.column };
            node->callee = std::move(expr);
            if (!check(TokenType::RParen)) {
                while (true) {
                    ExprPtr arg = parseExpr();
                    if (!arg) return nullptr;
                    node->args.push_back(std::move(arg));
                    if (match(TokenType::Comma)) continue;
                    break;
                }
            }
            if (!expect(TokenType::RParen, "')'")) return nullptr;
            expr = std::move(node);
        } else if (check(TokenType::Dot)) {
            SourcePos pos{ cur().line, cur().column };
            advance();
            if (!check(TokenType::Ident)) {
                failHere("se esperaba el nombre de un campo tras '.'");
                return nullptr;
            }
            auto node = std::make_unique<FieldExpr>();
            node->pos = SourcePos{ pos.line, pos.column };
            node->base = std::move(expr);
            node->name = cur().text;
            advance();
            expr = std::move(node);
        } else {
            break;
        }
    }
    return expr;
}

ExprPtr Parser::parsePrimary() {
    Token tok = cur();
    switch (tok.type) {
        case TokenType::IntLit: {
            advance();
            auto node = std::make_unique<IntExpr>();
            node->pos = SourcePos{ tok.line, tok.column };
            node->value = tok.intValue;
            return node;
        }
        case TokenType::StringLit: {
            advance();
            auto node = std::make_unique<StringExpr>();
            node->pos = SourcePos{ tok.line, tok.column };
            node->value = tok.text;
            return node;
        }
        case TokenType::KwTrue:
        case TokenType::KwFalse: {
            bool value = tok.type == TokenType::KwTrue;
            advance();
            auto node = std::make_unique<BoolExpr>();
            node->pos = SourcePos{ tok.line, tok.column };
            node->value = value;
            return node;
        }
        case TokenType::LParen: {
            advance();
            ExprPtr inner = parseExpr();
            if (!inner) return nullptr;
            if (!expect(TokenType::RParen, "')'")) return nullptr;
            return inner;
        }
        case TokenType::Ident: {
            advance();
            auto node = std::make_unique<IdentExpr>();
            node->pos = SourcePos{ tok.line, tok.column };
            node->name = tok.text;
            return node;
        }
        case TokenType::KwNew: {
            advance();
            auto node = std::make_unique<NewExpr>();
            node->pos = SourcePos{ tok.line, tok.column };
            if (!parseTypeInto(node->type)) return nullptr;
            return node;
        }
        default:
            fail(tok, "se esperaba una expresion pero se encontro " +
                      std::string(tokenTypeName(tok.type)));
            return nullptr;
    }
}

/* ------------------------------------------------------------------ */
/* Tipos                                                               */
/* ------------------------------------------------------------------ */
TypeRefPtr Parser::parseType() {
    TypeRefPtr type;
    if (!parseTypeInto(type)) return nullptr;
    return type;
}

bool Parser::parseTypeInto(TypeRefPtr& out) {
    Token tok = cur();
    switch (tok.type) {
        case TokenType::KwInt: {
            advance();
            out = makeTypeInt();
            out->pos = SourcePos{ tok.line, tok.column };
            return true;
        }
        case TokenType::KwBool: {
            advance();
            out = makeTypeBool();
            out->pos = SourcePos{ tok.line, tok.column };
            return true;
        }
        case TokenType::KwString: {
            advance();
            out = makeTypeString();
            out->pos = SourcePos{ tok.line, tok.column };
            return true;
        }
        case TokenType::KwVoid: {
            advance();
            out = makeTypeVoid();
            out->pos = SourcePos{ tok.line, tok.column };
            return true;
        }
        case TokenType::Star: {
            advance();
            TypeRefPtr elem;
            if (!parseTypeInto(elem)) return false;
            out = makeTypePointer(elem);
            out->pos = SourcePos{ tok.line, tok.column };
            return true;
        }
        case TokenType::LBracket: {
            advance();
            ExprPtr count = parseExpr();
            if (!count) return false;
            if (!expect(TokenType::RBracket, "']'")) return false;
            long long value = 0;
            {
                const IntExpr* lit = dynamic_cast<const IntExpr*>(count.get());
                if (!lit) {
                    return fail(tok, "el tamaño de un array debe ser una constante entera");
                }
                value = lit->value;
            }
            if (value <= 0) {
                return fail(tok, "el tamaño de un array debe ser mayor que cero");
            }
            TypeRefPtr elem;
            if (!parseTypeInto(elem)) return false;
            out = makeTypeArray(value, elem);
            out->pos = SourcePos{ tok.line, tok.column };
            return true;
        }
        case TokenType::Ident: {
            advance();
            out = std::make_shared<TypeRef>();
            out->kind = TypeRef::Kind::Struct;
            out->structName = tok.text;
            out->pos = SourcePos{ tok.line, tok.column };
            return true;
        }
        default:
            return fail(tok, "se esperaba un tipo pero se encontro " +
                             std::string(tokenTypeName(tok.type)));
    }
}

} /* namespace axc */
