/*
 * AxolynOS - Analizador lexico del lenguaje Axolyn.
 *
 * Produce una secuencia de tokens con posicion (linea/columna) para el
 * parser descendente recursivo.
 */
#ifndef AXC_LEXER_H
#define AXC_LEXER_H

#include <string>
#include <vector>

namespace axc {

/* Posicion de un token en el fichero fuente. */
struct SourcePos {
    int line = 1;
    int column = 1;
};

/* Diagnostico (error o aviso) con posicion. */
struct Diag {
    int line = 0;
    int column = 0;
    std::string message;

    Diag() = default;
    Diag(int l, int c, std::string m) : line(l), column(c), message(std::move(m)) {}
    /* Formato "fichero:linea:columna: mensaje". */
    std::string format(const std::string& file) const;
};

enum class TokenType {
    End = 0,
    Ident,
    IntLit,
    StringLit,

    /* palabras clave */
    KwFn, KwVar, KwLet, KwIf, KwElse, KwWhile, KwFor, KwReturn, KwBreak,
    KwContinue, KwStruct, KwImport, KwInt, KwBool, KwString, KwVoid, KwTrue,
    KwFalse, KwNew,

    /* operadores y punctuacion */
    Plus, Minus, Star, Slash, Percent,
    Assign, PlusAssign, MinusAssign, StarAssign, SlashAssign, PercentAssign,
    Eq, NotEq, Lt, Le, Gt, Ge,
    AndAnd, OrOr, Bang, Amp, Dot,
    LParen, RParen, LBrace, RBrace, LBracket, RBracket,
    Comma, Semi, Colon
};

const char* tokenTypeName(TokenType type);

struct Token {
    TokenType type = TokenType::End;
    std::string text;      /* identificadores */
    long long intValue = 0;/* literales enteros */
    int line = 1;
    int column = 1;

    bool is(TokenType t) const { return type == t; }
};

/*
 * Analizador lexico.
 *
 * next() devuelve false cuando encuentra un error; el diagnostico queda
 * disponible en diagnostics().
 */
class Lexer {
public:
    Lexer(const std::string& source, std::string fileName);

    bool next(Token& out);
    /* Analiza todo el fichero de una vez (util para tests). */
    bool tokenize(std::vector<Token>& out);

    const std::vector<Diag>& diagnostics() const { return diags_; }
    const std::string& fileName() const { return file_; }

private:
    bool fail(int line, int column, const std::string& msg);
    void skipSpaceAndComments();
    bool lexNumber(Token& out);
    bool lexString(Token& out);
    bool lexIdent(Token& out);

    const std::string& src_;
    std::string file_;
    size_t pos_ = 0;
    int line_ = 1;
    int col_ = 1;
    std::vector<Diag> diags_;
    bool failed_ = false;
};

} /* namespace axc */

#endif /* AXC_LEXER_H */
