#pragma once
#include "ast.h"
#include "lexer.h"

namespace td {

class Parser {
public:
    bool parse(const char* source, ASTNode** outProgram);
    const char* getError() const { return m_error; }
    int getErrorLine() const { return m_errorLine; }
    
private:
    // Declarations
    ASTNode* parseProgram();
    ASTNode* parseDeclaration();
    ASTNode* parseFunctionDecl();
    ASTNode* parseStructDecl();
    ASTNode* parseEntityDecl();
    ASTNode* parseVarDecl(bool isConst);
    
    // Statements
    ASTNode* parseStatement();
    ASTNode* parseBlock();
    ASTNode* parseIfStmt();
    ASTNode* parseWhileStmt();
    ASTNode* parseForStmt();
    ASTNode* parseReturnStmt();
    ASTNode* parseExpressionStatement();
    
    // Expressions (precedence climbing)
    ASTNode* parseExpression();
    ASTNode* parseAssignment();
    ASTNode* parseOr();
    ASTNode* parseAnd();
    ASTNode* parseEquality();
    ASTNode* parseComparison();
    ASTNode* parseAdditive();
    ASTNode* parseMultiplicative();
    ASTNode* parseUnary();
    ASTNode* parsePostfix();
    ASTNode* parseCall();
    ASTNode* parsePrimary();
    
    // Helpers
    bool check(TokenType type) const;
    bool match(TokenType type);
    Token advance();
    Token consume(TokenType type, const char* message);
    bool isAtEnd() const;
    Token peek() const;
    Token previous() const;
    void synchronize();
    
    void error(const char* message);
    void errorAt(const Token& token, const char* message);
    
    Lexer m_lexer;
    Token m_current;
    Token m_previous;
    bool m_hadError = false;
    bool m_panicMode = false;
    char m_error[256] = {};
    int m_errorLine = 0;
};

} // namespace td
