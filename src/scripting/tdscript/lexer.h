// =============================================================================
// TD Engine — TDScript Lexer (Tier 4)
//
// Source text → stream of Tokens. Single-pass, no backtracking.
//
// Token format:
//   { kind, text, line, col }
//
// Errors (unterminated strings, invalid chars) are emitted as Diagnostics
// and the lexer skips to the next safe boundary (whitespace/semicolon).
// =============================================================================
#pragma once

#include "ast.h"
#include <string>
#include <vector>

namespace td::tdscript {

struct Token {
    TokenKind kind;
    std::string text;
    int line;
    int col;
};

class Lexer {
public:
    explicit Lexer(const std::string& source);

    // Tokenize. Returns true on success (errors are appended to diagnostics
    // but tokenization continues best-effort).
    std::vector<Token> tokenize();

    const std::vector<Diagnostic>& diagnostics() const { return m_diags; }

private:
    std::string m_src;
    size_t m_pos = 0;
    int m_line = 1;
    int m_col = 1;
    std::vector<Diagnostic> m_diags;

    char peek(size_t off = 0) const;
    char advance();
    bool match(char expected);  // consumes if match
    void skipWhitespaceAndComments();
    Token lexIdentifierOrKeyword();
    Token lexNumber();
    Token lexString();
    Token lexOperator();
    void emitError(const std::string& msg, int line, int col);
    Token makeToken(TokenKind k, const std::string& t, int line, int col);
};

// Convert a TokenKind to a human-readable string (for error messages).
const char* tokenKindName(TokenKind k);

} // namespace td::tdscript
