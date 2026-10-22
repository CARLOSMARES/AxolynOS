/*
 * AxolynOS - Analizador lexico del lenguaje Axolyn.
 */
#include "axc/lexer.h"

#include <cctype>
#include <cstdlib>

namespace axc {

/* ------------------------------------------------------------------ */
/* Utilidades                                                         */
/* ------------------------------------------------------------------ */
std::string Diag::format(const std::string& file) const {
    std::string out = file.empty() ? std::string("<entrada>") : file;
    out += ":";
    out += std::to_string(line);
    out += ":";
    out += std::to_string(column);
    out += ": ";
    out += message;
    return out;
}

namespace {

struct KeywordEntry {
    const char* text;
    TokenType type;
};

const KeywordEntry kKeywords[] = {
    { "fn",       TokenType::KwFn },
    { "var",      TokenType::KwVar },
    { "let",      TokenType::KwLet },
    { "if",       TokenType::KwIf },
    { "else",     TokenType::KwElse },
    { "while",    TokenType::KwWhile },
    { "for",      TokenType::KwFor },
    { "return",   TokenType::KwReturn },
    { "break",    TokenType::KwBreak },
    { "continue", TokenType::KwContinue },
    { "struct",   TokenType::KwStruct },
    { "import",   TokenType::KwImport },
    { "int",      TokenType::KwInt },
    { "bool",     TokenType::KwBool },
    { "string",   TokenType::KwString },
    { "void",     TokenType::KwVoid },
    { "true",     TokenType::KwTrue },
    { "false",    TokenType::KwFalse },
    { "new",      TokenType::KwNew }
};

bool isIdentStart(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}
bool isIdentChar(char c) {
    return isIdentStart(c) || (c >= '0' && c <= '9');
}
bool isDigit(char c) { return c >= '0' && c <= '9'; }

} /* namespace */

const char* tokenTypeName(TokenType type) {
    switch (type) {
        case TokenType::End: return "fin";
        case TokenType::Ident: return "identificador";
        case TokenType::IntLit: return "entero";
        case TokenType::StringLit: return "cadena";
        case TokenType::KwFn: return "'fn'";
        case TokenType::KwVar: return "'var'";
        case TokenType::KwLet: return "'let'";
        case TokenType::KwIf: return "'if'";
        case TokenType::KwElse: return "'else'";
        case TokenType::KwWhile: return "'while'";
        case TokenType::KwFor: return "'for'";
        case TokenType::KwReturn: return "'return'";
        case TokenType::KwBreak: return "'break'";
        case TokenType::KwContinue: return "'continue'";
        case TokenType::KwStruct: return "'struct'";
        case TokenType::KwImport: return "'import'";
        case TokenType::KwInt: return "'int'";
        case TokenType::KwBool: return "'bool'";
        case TokenType::KwString: return "'string'";
        case TokenType::KwVoid: return "'void'";
        case TokenType::KwTrue: return "'true'";
        case TokenType::KwFalse: return "'false'";
        case TokenType::KwNew: return "'new'";
        case TokenType::Plus: return "'+'";
        case TokenType::Minus: return "'-'";
        case TokenType::Star: return "'*'";
        case TokenType::Slash: return "'/'";
        case TokenType::Percent: return "'%'";
        case TokenType::Assign: return "'='";
        case TokenType::PlusAssign: return "'+='";
        case TokenType::MinusAssign: return "'-='";
        case TokenType::StarAssign: return "'*='";
        case TokenType::SlashAssign: return "'/='";
        case TokenType::PercentAssign: return "'%='";
        case TokenType::Eq: return "'=='";
        case TokenType::NotEq: return "'!='";
        case TokenType::Lt: return "'<'";
        case TokenType::Le: return "'<='";
        case TokenType::Gt: return "'>'";
        case TokenType::Ge: return "'>='";
        case TokenType::AndAnd: return "'&&'";
        case TokenType::OrOr: return "'||'";
        case TokenType::Bang: return "'!'";
        case TokenType::Amp: return "'&'";
        case TokenType::Dot: return "'.'";
        case TokenType::LParen: return "'('";
        case TokenType::RParen: return "')'";
        case TokenType::LBrace: return "'{'";
        case TokenType::RBrace: return "'}'";
        case TokenType::LBracket: return "'['";
        case TokenType::RBracket: return "']'";
        case TokenType::Comma: return "','";
        case TokenType::Semi: return "';'";
        case TokenType::Colon: return "':'";
    }
    return "token";
}

/* ------------------------------------------------------------------ */
/* Lexer                                                              */
/* ------------------------------------------------------------------ */
Lexer::Lexer(const std::string& source, std::string fileName)
    : src_(source), file_(std::move(fileName)) {}

bool Lexer::fail(int line, int column, const std::string& msg) {
    diags_.emplace_back(line, column, msg);
    failed_ = true;
    return false;
}

void Lexer::skipSpaceAndComments() {
    while (pos_ < src_.size()) {
        char c = src_[pos_];
        if (c == '\n') {
            pos_++;
            line_++;
            col_ = 1;
        } else if (c == ' ' || c == '\t' || c == '\r') {
            pos_++;
            col_++;
        } else if (c == '/' && pos_ + 1 < src_.size() && src_[pos_ + 1] == '/') {
            while (pos_ < src_.size() && src_[pos_] != '\n') { pos_++; col_++; }
        } else if (c == '/' && pos_ + 1 < src_.size() && src_[pos_ + 1] == '*') {
            int startLine = line_;
            int startCol = col_;
            pos_ += 2;
            col_ += 2;
            bool closed = false;
            while (pos_ < src_.size()) {
                if (src_[pos_] == '*' && pos_ + 1 < src_.size() && src_[pos_ + 1] == '/') {
                    pos_ += 2;
                    col_ += 2;
                    closed = true;
                    break;
                }
                if (src_[pos_] == '\n') { line_++; col_ = 1; } else { col_++; }
                pos_++;
            }
            if (!closed) {
                fail(startLine, startCol, "comentario de bloque sin cerrar");
                return;
            }
        } else {
            return;
        }
    }
}

bool Lexer::lexNumber(Token& out) {
    int line = line_;
    int col = col_;
    size_t start = pos_;
    unsigned long long value = 0;
    bool overflow = false;

    if (src_[pos_] == '0' && pos_ + 1 < src_.size() &&
        (src_[pos_ + 1] == 'x' || src_[pos_ + 1] == 'X')) {
        pos_ += 2;
        col_ += 2;
        size_t digitsStart = pos_;
        while (pos_ < src_.size() && std::isxdigit((unsigned char)src_[pos_])) {
            char c = src_[pos_];
            unsigned d;
            if (c >= '0' && c <= '9') d = (unsigned)(c - '0');
            else if (c >= 'a' && c <= 'f') d = (unsigned)(c - 'a' + 10);
            else d = (unsigned)(c - 'A' + 10);
            if (value > (0xFFFFFFFFFFFFFFFFull - d) / 16ull) overflow = true;
            value = value * 16ull + d;
            pos_++;
            col_++;
        }
        if (pos_ == digitsStart) return fail(line, col, "numero hexadecimal invalido");
    } else {
        while (pos_ < src_.size() && isDigit(src_[pos_])) {
            unsigned d = (unsigned)(src_[pos_] - '0');
            if (value > (0xFFFFFFFFFFFFFFFFull - d) / 10ull) overflow = true;
            value = value * 10ull + d;
            pos_++;
            col_++;
        }
    }
    if (pos_ < src_.size() && (isIdentStart(src_[pos_]))) {
        return fail(line, col, "literal numerico invalido");
    }
    if (overflow) return fail(line, col, "literal numerico demasiado grande");
    out.type = TokenType::IntLit;
    out.intValue = (long long)value;
    out.text = src_.substr(start, pos_ - start);
    out.line = line;
    out.column = col;
    return true;
}

bool Lexer::lexString(Token& out) {
    int line = line_;
    int col = col_;
    std::string value;
    pos_++; /* comilla inicial */
    col_++;
    while (true) {
        if (pos_ >= src_.size()) return fail(line, col, "cadena sin cerrar");
        char c = src_[pos_];
        if (c == '\n') return fail(line, col, "cadena sin cerrar (salto de linea)");
        if (c == '"') {
            pos_++;
            col_++;
            break;
        }
        if (c == '\\') {
            pos_++;
            col_++;
            if (pos_ >= src_.size()) return fail(line, col, "cadena sin cerrar");
            char e = src_[pos_];
            pos_++;
            col_++;
            switch (e) {
                case 'n': value.push_back('\n'); break;
                case 't': value.push_back('\t'); break;
                case 'r': value.push_back('\r'); break;
                case '0': value.push_back('\0'); break;
                case 'e': value.push_back('\033'); break;
                case '\\': value.push_back('\\'); break;
                case '"': value.push_back('"'); break;
                case '\'': value.push_back('\''); break;
                default:
                    return fail(line, col, std::string("secuencia de escape desconocida '\\") +
                                            e + "'");
            }
            continue;
        }
        value.push_back(c);
        pos_++;
        col_++;
    }
    out.type = TokenType::StringLit;
    out.text = value;
    out.line = line;
    out.column = col;
    return true;
}

bool Lexer::lexIdent(Token& out) {
    int line = line_;
    int col = col_;
    size_t start = pos_;
    while (pos_ < src_.size() && isIdentChar(src_[pos_])) {
        pos_++;
        col_++;
    }
    out.text = src_.substr(start, pos_ - start);
    out.line = line;
    out.column = col;
    out.type = TokenType::Ident;
    for (const KeywordEntry& kw : kKeywords) {
        if (out.text == kw.text) {
            out.type = kw.type;
            break;
        }
    }
    return true;
}

bool Lexer::next(Token& out) {
    if (failed_) return false;
    skipSpaceAndComments();
    if (failed_) return false;

    out = Token();
    if (pos_ >= src_.size()) {
        out.type = TokenType::End;
        out.line = line_;
        out.column = col_;
        return true;
    }

    char c = src_[pos_];
    int line = line_;
    int col = col_;

    if (isDigit(c)) return lexNumber(out);
    if (isIdentStart(c)) return lexIdent(out);
    if (c == '"') return lexString(out);

    auto two = [&](char a, char b) {
        return c == a && pos_ + 1 < src_.size() && src_[pos_ + 1] == b;
    };

    pos_++;
    col_++;
    out.line = line;
    out.column = col;
    out.text = std::string(1, c);

    if (c == '+') {
        if (pos_ < src_.size() && src_[pos_] == '=') { pos_++; col_++; out.type = TokenType::PlusAssign; }
        else out.type = TokenType::Plus;
    } else if (c == '-') {
        if (pos_ < src_.size() && src_[pos_] == '=') { pos_++; col_++; out.type = TokenType::MinusAssign; }
        else out.type = TokenType::Minus;
    } else if (c == '*') {
        if (pos_ < src_.size() && src_[pos_] == '=') { pos_++; col_++; out.type = TokenType::StarAssign; }
        else out.type = TokenType::Star;
    } else if (c == '/') {
        if (pos_ < src_.size() && src_[pos_] == '=') { pos_++; col_++; out.type = TokenType::SlashAssign; }
        else out.type = TokenType::Slash;
    } else if (c == '%') {
        if (pos_ < src_.size() && src_[pos_] == '=') { pos_++; col_++; out.type = TokenType::PercentAssign; }
        else out.type = TokenType::Percent;
    } else if (c == '=') {
        if (pos_ < src_.size() && src_[pos_] == '=') { pos_++; col_++; out.type = TokenType::Eq; }
        else out.type = TokenType::Assign;
    } else if (c == '!') {
        if (pos_ < src_.size() && src_[pos_] == '=') { pos_++; col_++; out.type = TokenType::NotEq; }
        else out.type = TokenType::Bang;
    } else if (c == '<') {
        if (pos_ < src_.size() && src_[pos_] == '=') { pos_++; col_++; out.type = TokenType::Le; }
        else out.type = TokenType::Lt;
    } else if (c == '>') {
        if (pos_ < src_.size() && src_[pos_] == '=') { pos_++; col_++; out.type = TokenType::Ge; }
        else out.type = TokenType::Gt;
    } else if (c == '&') {
        if (pos_ < src_.size() && src_[pos_] == '&') { pos_++; col_++; out.type = TokenType::AndAnd; }
        else out.type = TokenType::Amp;
    } else if (c == '|') {
        if (pos_ < src_.size() && src_[pos_] == '|') { pos_++; col_++; out.type = TokenType::OrOr; }
        else return fail(line, col, "el operador '|' no existe (usa '||')");
    } else if (c == '(') out.type = TokenType::LParen;
    else if (c == ')') out.type = TokenType::RParen;
    else if (c == '{') out.type = TokenType::LBrace;
    else if (c == '}') out.type = TokenType::RBrace;
    else if (c == '[') out.type = TokenType::LBracket;
    else if (c == ']') out.type = TokenType::RBracket;
    else if (c == ',') out.type = TokenType::Comma;
    else if (c == ';') out.type = TokenType::Semi;
    else if (c == ':') out.type = TokenType::Colon;
    else if (c == '.') out.type = TokenType::Dot;
    else if (c == '@' || c == '#' || c == '$' || c == '?' || c == '~' || c == '^')
        return fail(line, col, std::string("caracter no permitido '") + c + "'");
    else return fail(line, col, std::string("caracter no permitido '") + c + "'");

    (void)two;
    return true;
}

bool Lexer::tokenize(std::vector<Token>& out) {
    Token tok;
    while (true) {
        if (!next(tok)) return false;
        out.push_back(tok);
        if (tok.type == TokenType::End) break;
    }
    return true;
}

} /* namespace axc */
