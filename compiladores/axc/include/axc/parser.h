/*
 * AxolynOS - Parser descendente recursivo del lenguaje Axolyn.
 *
 * Gramática implementada (ver manuales/10-bytecode-vm.md):
 *
 *   programa    := (importStmt | fnDecl | globalDecl | structDecl)* EOF
 *   importStmt  := 'import' IDENT ';'
 *   fnDecl      := 'fn' IDENT '(' (IDENT ':' type (',' IDENT ':' type)*)? ')'
 *                  (':' type)? block
 *   globalDecl  := ('var'|'let') IDENT ':' type ('=' expr)? ';'
 *   structDecl  := 'struct' IDENT '{' (IDENT ':' type (',' IDENT ':' type)*)? '}'
 *   stmt        := varDecl | ifStmt | whileStmt | forStmt | returnStmt | block
 *                | assignStmt | exprStmt | breakStmt | continueStmt
 *   varDecl     := ('var'|'let') IDENT ':' type ('=' expr)? ';'
 *   assignStmt  := lvalue ('='|'+='|'-='|'*='|'/='|'%=') expr ';'
 *   ifStmt      := 'if' '(' expr ')' block ('else' (ifStmt|block))?
 *   whileStmt   := 'while' '(' expr ')' block
 *   forStmt     := 'for' '(' (varDeclSinPuntoY coma | expr ';') expr ';' expr ')' block
 *   returnStmt  := 'return' expr? ';'
 *   block       := '{' stmt* '}'
 *   lvalue      := IDENT ('[' expr ']')* | '*' expr
 *   expr        := logOr
 *   logOr       := logAnd ('||' logAnd)*
 *   logAnd      := equality ('&&' equality)*
 *   equality    := comparison (('=='|'!=') comparison)*
 *   comparison  := additive (('<'|'<='|'>'|'>=') additive)*
 *   additive    := multiplicative (('+'|'-') multiplicative)*
 *   multiplicative := unary (('*'|'/'|'%') unary)*
 *   unary       := ('-'|'!'|'*'|'&') unary | postfix
 *   postfix     := primary ('[' expr ']' | '(' args ')')*
 *   primary     := INT | STRING | 'true' | 'false' | IDENT | '(' expr ')'
 *   type        := 'int' | 'bool' | 'string' | 'void' | '*' type
 *                | '[' expr ']' type | IDENT
 *
 * Ampliaciones minimas (necesarias para el enunciado):
 *   - 'ns.fun(args)'  : llamada a una funcion nativa de un namespace.
 *   - 'expr.campo'    : acceso a un campo de struct.
 *   - 'new Tipo'      : reserva memoria cero para un tipo.
 *   - asignacion como expresion (necesaria en el paso del bucle 'for').
 */
#ifndef AXC_PARSER_H
#define AXC_PARSER_H

#include <string>
#include <vector>

#include "axc/ast.h"
#include "axc/lexer.h"

namespace axc {

class Parser {
public:
    Parser(const std::string& source, std::string fileName);

    /* Analiza el programa completo. Devuelve false si hubo errores. */
    bool parseProgram(Program& out);

    const std::vector<Diag>& diagnostics() const { return diags_; }
    const std::string& fileName() const { return lexer_.fileName(); }

private:
    /* --- utilidades --- */
    const Token& peek(size_t ahead = 0);
    const Token& cur() { return peek(0); }
    void advance();
    bool atEnd() { return cur().type == TokenType::End; }
    bool check(TokenType t) { return cur().type == t; }
    bool match(TokenType t);
    bool expect(TokenType t, const char* what);
    bool fail(const Token& tok, const std::string& msg);
    bool failHere(const std::string& msg) { return fail(cur(), msg); }
    void skipToStatementEnd();

    /* --- reglas --- */
    bool parseTopLevel(Program& prog);
    bool parseImport(Program& prog);
    bool parseStruct(Program& prog);
    bool parseFunction(Program& prog);
    bool parseGlobal(Program& prog);

    bool parseBlock(std::vector<StmtPtr>& out);
    bool parseStatement(std::vector<StmtPtr>& out);
    bool parseVarDecl(std::vector<StmtPtr>& out, bool consumeSemi);
    bool parseIf(std::vector<StmtPtr>& out);
    bool parseWhile(std::vector<StmtPtr>& out);
    bool parseFor(std::vector<StmtPtr>& out);
    bool parseReturn(std::vector<StmtPtr>& out);
    bool parseBreakContinue(std::vector<StmtPtr>& out);

    ExprPtr parseExpr();
    ExprPtr parseExprOrAssign();
    ExprPtr parseLogOr();
    ExprPtr parseLogAnd();
    ExprPtr parseEquality();
    ExprPtr parseComparison();
    ExprPtr parseAdditive();
    ExprPtr parseMultiplicative();
    ExprPtr parseUnary();
    ExprPtr parsePostfix();
    ExprPtr parsePrimary();

    TypeRefPtr parseType();
    bool parseTypeInto(TypeRefPtr& out);

    Lexer lexer_;
    std::vector<Token> buf_;
    size_t bufPos_ = 0;
    bool lexerFailed_ = false;
    std::vector<Diag> diags_;
    bool failed_ = false;
    int order_ = 0;
};

} /* namespace axc */

#endif /* AXC_PARSER_H */
