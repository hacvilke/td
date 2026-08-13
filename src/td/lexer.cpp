#include "lexer.h"
#include <cstring>
#include <cstdlib>

namespace td {

const char* tokenTypeName(TokenType type) {
    switch (type) {
        case TokenType::Integer: return "Integer";
        case TokenType::Float: return "Float";
        case TokenType::String: return "String";
        case TokenType::Identifier: return "Identifier";
        case TokenType::Let: return "Let";
        case TokenType::Const: return "Const";
        case TokenType::Fn: return "Fn";
        case TokenType::If: return "If";
        case TokenType::Else: return "Else";
        case TokenType::While: return "While";
        case TokenType::For: return "For";
        case TokenType::Return: return "Return";
        case TokenType::True: return "True";
        case TokenType::False: return "False";
        case TokenType::Null: return "Null";
        case TokenType::Struct: return "Struct";
        case TokenType::Entity: return "Entity";
        case TokenType::This: return "This";
        case TokenType::Break: return "Break";
        case TokenType::Continue: return "Continue";
        case TokenType::IntType: return "IntType";
        case TokenType::FloatType: return "FloatType";
        case TokenType::StringType: return "StringType";
        case TokenType::BoolType: return "BoolType";
        case TokenType::VoidType: return "VoidType";
        case TokenType::Plus: return "Plus";
        case TokenType::Minus: return "Minus";
        case TokenType::Star: return "Star";
        case TokenType::Slash: return "Slash";
        case TokenType::Percent: return "Percent";
        case TokenType::Equals: return "Equals";
        case TokenType::EqualsEquals: return "EqualsEquals";
        case TokenType::BangEquals: return "BangEquals";
        case TokenType::Less: return "Less";
        case TokenType::LessEqual: return "LessEqual";
        case TokenType::Greater: return "Greater";
        case TokenType::GreaterEqual: return "GreaterEqual";
        case TokenType::And: return "And";
        case TokenType::Or: return "Or";
        case TokenType::Bang: return "Bang";
        case TokenType::EndOfFile: return "EOF";
        case TokenType::Error: return "Error";
        default: return "Unknown";
    }
}

void Lexer::init(const char* source) {
    m_source = source;
    m_start = source;
    m_current = source;
    m_line = 1;
    m_column = 1;
    m_hasPeeked = false;
}

bool Lexer::isAtEnd() const {
    return *m_current == '\0';
}

char Lexer::advance() {
    m_column++;
    return *m_current++;
}

char Lexer::peek() const {
    return *m_current;
}

char Lexer::peekNext() const {
    if (isAtEnd()) return '\0';
    return m_current[1];
}

bool Lexer::match(char expected) {
    if (isAtEnd()) return false;
    if (*m_current != expected) return false;
    m_current++;
    m_column++;
    return true;
}

void Lexer::skipWhitespace() {
    while (true) {
        char c = peek();
        switch (c) {
            case ' ':
            case '\r':
            case '\t':
                advance();
                break;
            case '\n':
                m_line++;
                m_column = 0;
                advance();
                break;
            case '/':
                if (peekNext() == '/') {
                    skipLineComment();
                } else if (peekNext() == '*') {
                    skipBlockComment();
                } else {
                    return;
                }
                break;
            default:
                return;
        }
    }
}

void Lexer::skipLineComment() {
    while (peek() != '\n' && !isAtEnd()) {
        advance();
    }
}

void Lexer::skipBlockComment() {
    advance(); // /
    advance(); // *
    
    int depth = 1;
    while (depth > 0 && !isAtEnd()) {
        if (peek() == '/' && peekNext() == '*') {
            advance();
            advance();
            depth++;
        } else if (peek() == '*' && peekNext() == '/') {
            advance();
            advance();
            depth--;
        } else {
            if (peek() == '\n') {
                m_line++;
                m_column = 0;
            }
            advance();
        }
    }
}

Token Lexer::makeToken(TokenType type) {
    Token token;
    token.type = type;
    token.line = m_line;
    token.column = m_column - (int)(m_current - m_start);
    
    int length = (int)(m_current - m_start);
    if (length > 127) length = 127;
    memcpy(token.lexeme, m_start, length);
    token.lexeme[length] = '\0';
    
    return token;
}

Token Lexer::errorToken(const char* message) {
    Token token;
    token.type = TokenType::Error;
    token.line = m_line;
    token.column = m_column;
    strncpy(token.lexeme, message, 127);
    token.lexeme[127] = '\0';
    return token;
}

static bool isDigit(char c) {
    return c >= '0' && c <= '9';
}

static bool isAlpha(char c) {
    return (c >= 'a' && c <= 'z') ||
           (c >= 'A' && c <= 'Z') ||
           c == '_';
}

Token Lexer::numberToken() {
    bool isFloat = false;
    
    while (isDigit(peek())) advance();
    
    // Decimal part
    if (peek() == '.' && isDigit(peekNext())) {
        isFloat = true;
        advance(); // .
        while (isDigit(peek())) advance();
    }
    
    // Exponent
    if (peek() == 'e' || peek() == 'E') {
        isFloat = true;
        advance();
        if (peek() == '+' || peek() == '-') advance();
        while (isDigit(peek())) advance();
    }
    
    Token token = makeToken(isFloat ? TokenType::Float : TokenType::Integer);
    
    if (isFloat) {
        token.floatValue = strtod(token.lexeme, nullptr);
    } else {
        token.intValue = strtoll(token.lexeme, nullptr, 10);
    }
    
    return token;
}

Token Lexer::stringToken() {
    char quote = m_current[-1]; // The opening quote
    
    while (peek() != quote && !isAtEnd()) {
        if (peek() == '\n') {
            m_line++;
            m_column = 0;
        }
        if (peek() == '\\' && peekNext() != '\0') {
            advance(); // Skip escape char
        }
        advance();
    }
    
    if (isAtEnd()) {
        return errorToken("Unterminated string");
    }
    
    advance(); // Closing quote
    return makeToken(TokenType::String);
}

TokenType Lexer::checkKeyword(int start, int length, const char* rest, TokenType type) {
    if (m_current - m_start == start + length &&
        memcmp(m_start + start, rest, length) == 0) {
        return type;
    }
    return TokenType::Identifier;
}

TokenType Lexer::identifierType() {
    switch (m_start[0]) {
        case 'b':
            if (m_current - m_start > 1) {
                switch (m_start[1]) {
                    case 'o': return checkKeyword(2, 2, "ol", TokenType::BoolType);
                    case 'r': return checkKeyword(2, 3, "eak", TokenType::Break);
                }
            }
            break;
        case 'c':
            if (m_current - m_start > 1) {
                switch (m_start[1]) {
                    case 'o': 
                        if (m_current - m_start > 3 && m_start[2] == 'n') {
                            if (m_start[3] == 's') return checkKeyword(4, 1, "t", TokenType::Const);
                            if (m_start[3] == 't') return checkKeyword(4, 4, "inue", TokenType::Continue);
                        }
                        break;
                }
            }
            break;
        case 'e':
            if (m_current - m_start > 1) {
                switch (m_start[1]) {
                    case 'l': return checkKeyword(2, 2, "se", TokenType::Else);
                    case 'n': return checkKeyword(2, 4, "tity", TokenType::Entity);
                }
            }
            break;
        case 'f':
            if (m_current - m_start > 1) {
                switch (m_start[1]) {
                    case 'a': return checkKeyword(2, 3, "lse", TokenType::False);
                    case 'l': return checkKeyword(2, 3, "oat", TokenType::FloatType);
                    case 'n': return m_current - m_start == 2 ? TokenType::Fn : TokenType::Identifier;
                    case 'o': return checkKeyword(2, 1, "r", TokenType::For);
                }
            }
            break;
        case 'i':
            if (m_current - m_start > 1) {
                switch (m_start[1]) {
                    case 'f': return m_current - m_start == 2 ? TokenType::If : TokenType::Identifier;
                    case 'n': return checkKeyword(2, 1, "t", TokenType::IntType);
                }
            }
            break;
        case 'l': return checkKeyword(1, 2, "et", TokenType::Let);
        case 'n': return checkKeyword(1, 3, "ull", TokenType::Null);
        case 'r': return checkKeyword(1, 5, "eturn", TokenType::Return);
        case 's':
            if (m_current - m_start > 1) {
                switch (m_start[1]) {
                    case 't':
                        if (m_current - m_start > 2) {
                            if (m_start[2] == 'r') return checkKeyword(3, 3, "ing", TokenType::StringType);
                            if (m_start[2] == 'u') return checkKeyword(3, 3, "uct", TokenType::Struct);
                        }
                        break;
                }
            }
            break;
        case 't':
            if (m_current - m_start > 1) {
                switch (m_start[1]) {
                    case 'h': return checkKeyword(2, 2, "is", TokenType::This);
                    case 'r': return checkKeyword(2, 2, "ue", TokenType::True);
                }
            }
            break;
        case 'v': return checkKeyword(1, 3, "oid", TokenType::VoidType);
        case 'w': return checkKeyword(1, 4, "hile", TokenType::While);
    }
    
    return TokenType::Identifier;
}

Token Lexer::identifierToken() {
    while (isAlpha(peek()) || isDigit(peek())) {
        advance();
    }
    return makeToken(identifierType());
}

Token Lexer::nextToken() {
    if (m_hasPeeked) {
        m_hasPeeked = false;
        return m_peekedToken;
    }
    
    skipWhitespace();
    
    m_start = m_current;
    
    if (isAtEnd()) {
        return makeToken(TokenType::EndOfFile);
    }
    
    char c = advance();
    
    if (isDigit(c)) return numberToken();
    if (isAlpha(c)) return identifierToken();
    
    switch (c) {
        case '(': return makeToken(TokenType::LeftParen);
        case ')': return makeToken(TokenType::RightParen);
        case '{': return makeToken(TokenType::LeftBrace);
        case '}': return makeToken(TokenType::RightBrace);
        case '[': return makeToken(TokenType::LeftBracket);
        case ']': return makeToken(TokenType::RightBracket);
        case ';': return makeToken(TokenType::Semicolon);
        case ',': return makeToken(TokenType::Comma);
        case '.': return makeToken(TokenType::Dot);
        case ':': return makeToken(TokenType::Colon);
        
        case '+':
            if (match('+')) return makeToken(TokenType::PlusPlus);
            if (match('=')) return makeToken(TokenType::PlusEquals);
            return makeToken(TokenType::Plus);
        case '-':
            if (match('-')) return makeToken(TokenType::MinusMinus);
            if (match('=')) return makeToken(TokenType::MinusEquals);
            if (match('>')) return makeToken(TokenType::Arrow);
            return makeToken(TokenType::Minus);
        case '*':
            if (match('=')) return makeToken(TokenType::StarEquals);
            return makeToken(TokenType::Star);
        case '/':
            if (match('=')) return makeToken(TokenType::SlashEquals);
            return makeToken(TokenType::Slash);
        case '%': return makeToken(TokenType::Percent);
        
        case '!':
            if (match('=')) return makeToken(TokenType::BangEquals);
            return makeToken(TokenType::Bang);
        case '=':
            if (match('=')) return makeToken(TokenType::EqualsEquals);
            return makeToken(TokenType::Equals);
        case '<':
            if (match('=')) return makeToken(TokenType::LessEqual);
            return makeToken(TokenType::Less);
        case '>':
            if (match('=')) return makeToken(TokenType::GreaterEqual);
            return makeToken(TokenType::Greater);
        
        case '&':
            if (match('&')) return makeToken(TokenType::And);
            return errorToken("Expected '&&'");
        case '|':
            if (match('|')) return makeToken(TokenType::Or);
            return errorToken("Expected '||'");
        
        case '"':
        case '\'':
            return stringToken();
    }
    
    return errorToken("Unexpected character");
}

Token Lexer::peekToken() {
    if (m_hasPeeked) {
        return m_peekedToken;
    }
    
    m_peekedToken = nextToken();
    m_hasPeeked = true;
    return m_peekedToken;
}

} // namespace td
