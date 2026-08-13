#pragma once
#include "token.h"

namespace td {

class Lexer {
public:
    void init(const char* source);
    Token nextToken();
    Token peekToken();
    bool isAtEnd() const;
    int getLine() const { return m_line; }
    int getColumn() const { return m_column; }
    
private:
    char advance();
    char peek() const;
    char peekNext() const;
    bool match(char expected);
    void skipWhitespace();
    void skipLineComment();
    void skipBlockComment();
    
    Token makeToken(TokenType type);
    Token errorToken(const char* message);
    Token numberToken();
    Token stringToken();
    Token identifierToken();
    
    TokenType checkKeyword(int start, int length, const char* rest, TokenType type);
    TokenType identifierType();
    
    const char* m_source = nullptr;
    const char* m_start = nullptr;
    const char* m_current = nullptr;
    int m_line = 1;
    int m_column = 1;
    
    bool m_hasPeeked = false;
    Token m_peekedToken;
};

} // namespace td
