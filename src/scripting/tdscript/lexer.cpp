// =============================================================================
// TD Engine — TDScript Lexer Implementation (Tier 4)
// =============================================================================
#include "lexer.h"
#include <cctype>
#include <unordered_map>

namespace td::tdscript {

Lexer::Lexer(const std::string& source) : m_src(source) {}

char Lexer::peek(size_t off) const {
    size_t i = m_pos + off;
    return i < m_src.size() ? m_src[i] : '\0';
}

char Lexer::advance() {
    if (m_pos >= m_src.size()) return '\0';
    char c = m_src[m_pos++];
    if (c == '\n') { m_line++; m_col = 1; }
    else { m_col++; }
    return c;
}

bool Lexer::match(char expected) {
    if (peek() == expected) { advance(); return true; }
    return false;
}

void Lexer::skipWhitespaceAndComments() {
    while (m_pos < m_src.size()) {
        char c = peek();
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
            advance();
        } else if (c == '/' && peek(1) == '/') {
            // line comment
            while (m_pos < m_src.size() && peek() != '\n') advance();
        } else if (c == '/' && peek(1) == '*') {
            // block comment
            advance(); advance();
            while (m_pos < m_src.size() && !(peek() == '*' && peek(1) == '/')) advance();
            if (m_pos < m_src.size()) { advance(); advance(); }
        } else {
            break;
        }
    }
}

static const std::unordered_map<std::string, TokenKind> kKeywords = {
    {"import",     TokenKind::KwImport},
    {"struct",     TokenKind::KwStruct},
    {"class",      TokenKind::KwClass},
    {"public",     TokenKind::KwPublic},
    {"private",    TokenKind::KwPrivate},
    {"protected",  TokenKind::KwProtected},
    {"void",       TokenKind::KwVoid},
    {"return",     TokenKind::KwReturn},
    {"if",         TokenKind::KwIf},
    {"else",       TokenKind::KwElse},
    {"for",        TokenKind::KwFor},
    {"while",      TokenKind::KwWhile},
    {"break",      TokenKind::KwBreak},
    {"continue",   TokenKind::KwContinue},
    {"true",       TokenKind::KwTrue},
    {"false",      TokenKind::KwFalse},
    {"null",       TokenKind::KwNull},
    {"replicated", TokenKind::KwReplicated},
    {"var",        TokenKind::KwVar},
    {"const",      TokenKind::KwConst},
    {"function",   TokenKind::KwFunction},
    {"int32",      TokenKind::KwInt32},
    {"uint32",     TokenKind::KwUint32},
    {"int64",      TokenKind::KwInt64},
    {"uint64",     TokenKind::KwUint64},
    {"float",      TokenKind::KwFloat},
    {"double",     TokenKind::KwDouble},
    {"bool",       TokenKind::KwBool},
    {"string",     TokenKind::KwString},
    {"auto",       TokenKind::KwAuto},
};

Token Lexer::lexIdentifierOrKeyword() {
    int startLine = m_line, startCol = m_col;
    std::string s;
    while (m_pos < m_src.size()) {
        char c = peek();
        if (std::isalnum((unsigned char)c) || c == '_') {
            s += advance();
        } else break;
    }
    auto it = kKeywords.find(s);
    TokenKind k = (it != kKeywords.end()) ? it->second : TokenKind::Ident;
    return makeToken(k, s, startLine, startCol);
}

Token Lexer::lexNumber() {
    int startLine = m_line, startCol = m_col;
    std::string s;
    bool isFloat = false;
    while (m_pos < m_src.size() && std::isdigit((unsigned char)peek())) s += advance();
    if (peek() == '.' && std::isdigit((unsigned char)peek(1))) {
        isFloat = true;
        s += advance();
        while (m_pos < m_src.size() && std::isdigit((unsigned char)peek())) s += advance();
    }
    // exponent
    if (peek() == 'e' || peek() == 'E') {
        isFloat = true;
        s += advance();
        if (peek() == '+' || peek() == '-') s += advance();
        while (m_pos < m_src.size() && std::isdigit((unsigned char)peek())) s += advance();
    }
    // suffixes: f, F → float; u, U → unsigned; l, L → long
    if (peek() == 'f' || peek() == 'F') { isFloat = true; advance(); }
    return makeToken(isFloat ? TokenKind::FloatLit : TokenKind::IntLit, s, startLine, startCol);
}

Token Lexer::lexString() {
    int startLine = m_line, startCol = m_col;
    advance();  // consume opening "
    std::string s;
    while (m_pos < m_src.size() && peek() != '"') {
        char c = peek();
        if (c == '\\') {
            advance();
            char esc = advance();
            switch (esc) {
                case 'n': s += '\n'; break;
                case 't': s += '\t'; break;
                case 'r': s += '\r'; break;
                case '"': s += '"';  break;
                case '\\': s += '\\'; break;
                case '0': s += '\0'; break;
                default: s += '\\'; s += esc; break;
            }
        } else {
            s += advance();
        }
    }
    if (m_pos >= m_src.size()) {
        emitError("unterminated string literal", startLine, startCol);
    } else {
        advance();  // consume closing "
    }
    return makeToken(TokenKind::StringLit, s, startLine, startCol);
}

Token Lexer::lexOperator() {
    int startLine = m_line, startCol = m_col;
    char c = advance();

    auto two = [&](char a, TokenKind twoMatch, TokenKind oneMatch) -> Token {
        if (peek() == a) { advance(); return makeToken(twoMatch, std::string() + c + a, startLine, startCol); }
        return makeToken(oneMatch, std::string() + c, startLine, startCol);
    };

    switch (c) {
        case '(': return makeToken(TokenKind::LParen, "(", startLine, startCol);
        case ')': return makeToken(TokenKind::RParen, ")", startLine, startCol);
        case '{': return makeToken(TokenKind::LBrace, "{", startLine, startCol);
        case '}': return makeToken(TokenKind::RBrace, "}", startLine, startCol);
        case '[': return makeToken(TokenKind::LBracket, "[", startLine, startCol);
        case ']': return makeToken(TokenKind::RBracket, "]", startLine, startCol);
        case ';': return makeToken(TokenKind::Semicolon, ";", startLine, startCol);
        case ',': return makeToken(TokenKind::Comma, ",", startLine, startCol);
        case '.': return makeToken(TokenKind::Dot, ".", startLine, startCol);
        case ':': return makeToken(TokenKind::Colon, ":", startLine, startCol);
        case '@': return makeToken(TokenKind::At, "@", startLine, startCol);
        case '+':
            if (peek() == '=') { advance(); return makeToken(TokenKind::PlusAssign, "+=", startLine, startCol); }
            return makeToken(TokenKind::Plus, "+", startLine, startCol);
        case '-':
            if (peek() == '=') { advance(); return makeToken(TokenKind::MinusAssign, "-=", startLine, startCol); }
            return makeToken(TokenKind::Minus, "-", startLine, startCol);
        case '*':
            if (peek() == '=') { advance(); return makeToken(TokenKind::StarAssign, "*=", startLine, startCol); }
            return makeToken(TokenKind::Star, "*", startLine, startCol);
        case '/':
            if (peek() == '=') { advance(); return makeToken(TokenKind::SlashAssign, "/=", startLine, startCol); }
            return makeToken(TokenKind::Slash, "/", startLine, startCol);
        case '%': return makeToken(TokenKind::Percent, "%", startLine, startCol);
        case '=':
            if (peek() == '=') { advance(); return makeToken(TokenKind::Eq, "==", startLine, startCol); }
            return makeToken(TokenKind::Assign, "=", startLine, startCol);
        case '!':
            if (peek() == '=') { advance(); return makeToken(TokenKind::NotEq, "!=", startLine, startCol); }
            return makeToken(TokenKind::Not, "!", startLine, startCol);
        case '<':
            if (peek() == '=') { advance(); return makeToken(TokenKind::LtEq, "<=", startLine, startCol); }
            return makeToken(TokenKind::Lt, "<", startLine, startCol);
        case '>':
            if (peek() == '=') { advance(); return makeToken(TokenKind::GtEq, ">=", startLine, startCol); }
            return makeToken(TokenKind::Gt, ">", startLine, startCol);
        case '&':
            if (peek() == '&') { advance(); return makeToken(TokenKind::And, "&&", startLine, startCol); }
            emitError("bitwise & not supported (use &&)", startLine, startCol);
            return makeToken(TokenKind::And, "&", startLine, startCol);
        case '|':
            if (peek() == '|') { advance(); return makeToken(TokenKind::Or, "||", startLine, startCol); }
            emitError("bitwise | not supported (use ||)", startLine, startCol);
            return makeToken(TokenKind::Or, "|", startLine, startCol);
        default:
            emitError(std::string("unexpected character '") + c + "'", startLine, startCol);
            return makeToken(TokenKind::Semicolon, ";", startLine, startCol);  // skip
    }
}

Token Lexer::makeToken(TokenKind k, const std::string& t, int line, int col) {
    Token tok;
    tok.kind = k;
    tok.text = t;
    tok.line = line;
    tok.col = col;
    return tok;
}

void Lexer::emitError(const std::string& msg, int line, int col) {
    Diagnostic d;
    d.severity = Diagnostic::Error;
    d.message = msg;
    d.line = line;
    d.col = col;
    m_diags.push_back(d);
}

std::vector<Token> Lexer::tokenize() {
    std::vector<Token> out;
    while (true) {
        skipWhitespaceAndComments();
        if (m_pos >= m_src.size()) break;
        char c = peek();
        if (std::isalpha((unsigned char)c) || c == '_') {
            out.push_back(lexIdentifierOrKeyword());
        } else if (std::isdigit((unsigned char)c)) {
            out.push_back(lexNumber());
        } else if (c == '"') {
            out.push_back(lexString());
        } else {
            out.push_back(lexOperator());
        }
    }
    out.push_back(makeToken(TokenKind::EndOfFile, "<eof>", m_line, m_col));
    return out;
}

const char* tokenKindName(TokenKind k) {
    switch (k) {
        case TokenKind::LParen: return "(";
        case TokenKind::RParen: return ")";
        case TokenKind::LBrace: return "{";
        case TokenKind::RBrace: return "}";
        case TokenKind::LBracket: return "[";
        case TokenKind::RBracket: return "]";
        case TokenKind::Semicolon: return ";";
        case TokenKind::Comma: return ",";
        case TokenKind::Dot: return ".";
        case TokenKind::Colon: return ":";
        case TokenKind::Arrow: return "->";
        case TokenKind::At: return "@";
        case TokenKind::Assign: return "=";
        case TokenKind::Plus: return "+";
        case TokenKind::Minus: return "-";
        case TokenKind::Star: return "*";
        case TokenKind::Slash: return "/";
        case TokenKind::Percent: return "%";
        case TokenKind::Eq: return "==";
        case TokenKind::NotEq: return "!=";
        case TokenKind::Lt: return "<";
        case TokenKind::Gt: return ">";
        case TokenKind::LtEq: return "<=";
        case TokenKind::GtEq: return ">=";
        case TokenKind::And: return "&&";
        case TokenKind::Or: return "||";
        case TokenKind::Not: return "!";
        case TokenKind::PlusAssign: return "+=";
        case TokenKind::MinusAssign: return "-=";
        case TokenKind::StarAssign: return "*=";
        case TokenKind::SlashAssign: return "/=";
        case TokenKind::IntLit: return "int literal";
        case TokenKind::FloatLit: return "float literal";
        case TokenKind::StringLit: return "string literal";
        case TokenKind::BoolLit: return "bool literal";
        case TokenKind::KwImport: return "import";
        case TokenKind::KwStruct: return "struct";
        case TokenKind::KwClass: return "class";
        case TokenKind::KwPublic: return "public";
        case TokenKind::KwPrivate: return "private";
        case TokenKind::KwProtected: return "protected";
        case TokenKind::KwVoid: return "void";
        case TokenKind::KwReturn: return "return";
        case TokenKind::KwIf: return "if";
        case TokenKind::KwElse: return "else";
        case TokenKind::KwFor: return "for";
        case TokenKind::KwWhile: return "while";
        case TokenKind::KwBreak: return "break";
        case TokenKind::KwContinue: return "continue";
        case TokenKind::KwTrue: return "true";
        case TokenKind::KwFalse: return "false";
        case TokenKind::KwNull: return "null";
        case TokenKind::KwReplicated: return "replicated";
        case TokenKind::KwVar: return "var";
        case TokenKind::KwConst: return "const";
        case TokenKind::KwFunction: return "function";
        case TokenKind::KwInt32: return "int32";
        case TokenKind::KwUint32: return "uint32";
        case TokenKind::KwInt64: return "int64";
        case TokenKind::KwUint64: return "uint64";
        case TokenKind::KwFloat: return "float";
        case TokenKind::KwDouble: return "double";
        case TokenKind::KwBool: return "bool";
        case TokenKind::KwString: return "string";
        case TokenKind::KwAuto: return "auto";
        case TokenKind::Ident: return "identifier";
        case TokenKind::EndOfFile: return "eof";
    }
    return "<unknown>";
}

} // namespace td::tdscript
