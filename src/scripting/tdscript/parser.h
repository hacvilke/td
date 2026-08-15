// =============================================================================
// TD Engine — TDScript Parser (Tier 4)
//
// Recursive-descent parser. Token stream → AST.
//
// Grammar (simplified):
//   module       := (import | struct | class)*
//   import       := 'import' STRING ';'
//   struct       := 'struct' IDENT '{' field* '}'
//   class        := 'class' IDENT '{' (field | method)* '}'
//   field        := ['replicated'] [visibility] type IDENT ['=' expr] ';'
//   method       := ['@rpc' '(' (IDENT) ')'] [visibility] type IDENT '(' params ')' block
//   type         := 'int32' | 'uint32' | ... | IDENT
//   block        := '{' stmt* '}'
//   stmt         := var_decl | if | for | while | return | break | continue | block | expr_stmt
//   expr         := assignment
//   assignment   := logical_or (('=' | '+=' | ...) assignment)?
//   logical_or   := logical_and ('||' logical_and)*
//   logical_and  := equality ('&&' equality)*
//   equality     := comparison (('==' | '!=') comparison)*
//   comparison   := additive (('<' | '>' | '<=' | '>=') additive)*
//   additive     := multiplicative (('+' | '-') multiplicative)*
//   multiplicative := unary (('*' | '/' | '%') unary)*
//   unary        := ('!' | '-') unary | postfix
//   postfix      := primary ('.' IDENT | '[' expr ']' | '(' args ')')*
//   primary      := INT | FLOAT | STRING | BOOL | 'null' | 'this' | IDENT | '(' expr ')'
// =============================================================================
#pragma once

#include "ast.h"
#include "lexer.h"
#include <string>
#include <vector>

namespace td::tdscript {

class Parser {
public:
    explicit Parser(std::vector<Token> tokens);

    // Parse the entire token stream into a Module AST node.
    // Returns nullptr on fatal error; check diagnostics() for details.
    NodePtr parseModule();

    const std::vector<Diagnostic>& diagnostics() const { return m_diags; }

private:
    std::vector<Token> m_tokens;
    size_t m_pos = 0;
    std::vector<Diagnostic> m_diags;

    // --- Token cursor helpers ---
    const Token& peek(size_t off = 0) const;
    const Token& advance();
    bool atEnd() const;
    bool check(TokenKind k) const;
    bool match(TokenKind k);
    const Token& expect(TokenKind k, const char* what);

    // --- Top-level ---
    NodePtr parseImport();
    NodePtr parseStruct();
    NodePtr parseClass();
    NodePtr parseFieldOrMethod(NodePtr parentClass);

    // --- Declarations ---
    NodePtr parseField(Visibility vis, bool isReplicated);
    NodePtr parseMethod(Visibility vis, RpcMode rpc);
    NodeList parseParams();
    NodePtr parseType();
    RpcMode parseRpcDecorator();  // called after '@'

    // --- Statements ---
    NodePtr parseBlock();
    NodePtr parseStatement();
    NodePtr parseVarDecl();
    NodePtr parseIf();
    NodePtr parseFor();
    NodePtr parseWhile();
    NodePtr parseReturn();
    NodePtr parseBreak();
    NodePtr parseContinue();
    NodePtr parseExprStatement();

    // --- Expressions ---
    NodePtr parseExpression();
    NodePtr parseAssignment();
    NodePtr parseBinary(int minPrec);
    NodePtr parseUnary();
    NodePtr parsePostfix();
    NodePtr parsePrimary();
    NodePtr parseCallArgs(NodePtr callee);

    // --- Helpers ---
    Visibility parseOptionalVisibility();
    void emitError(const std::string& msg, int line, int col);
    void synchronize();  // skip tokens until we hit a likely recovery point
};

// Operator precedence for binary expressions (1 = lowest).
int operatorPrecedence(TokenKind k);
bool isBinaryOperator(TokenKind k);

} // namespace td::tdscript
