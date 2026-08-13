#pragma once
#include "token.h"
#include <cstdint>

namespace td {

enum class NodeType : uint8_t {
    Program,
    
    // Declarations
    FunctionDecl,
    StructDecl,
    EntityDecl,
    VarDecl,
    
    // Statements
    Block,
    IfStmt,
    WhileStmt,
    ForStmt,
    ReturnStmt,
    BreakStmt,
    ContinueStmt,
    ExprStmt,
    
    // Expressions
    BinaryExpr,
    UnaryExpr,
    CallExpr,
    MemberExpr,
    IndexExpr,
    LiteralExpr,
    IdentifierExpr,
    AssignExpr,
    ThisExpr,
    ArrayExpr
};

struct ASTNode;

// Program node
struct ProgramNode {
    ASTNode** statements;
    int statementCount;
};

// Function declaration
struct FunctionDeclNode {
    char name[64];
    char paramNames[16][64];
    char paramTypes[16][32];
    int paramCount;
    char returnType[32];
    ASTNode* body;
    bool isMethod;
};

// Struct declaration
struct StructDeclNode {
    char name[64];
    struct {
        char name[64];
        char type[32];
    } fields[32];
    int fieldCount;
};

// Entity declaration (game object definition)
struct EntityDeclNode {
    char name[64];
    ASTNode* body;
};

// Variable declaration
struct VarDeclNode {
    char name[64];
    char type[32];
    ASTNode* initializer;
    bool isConst;
};

// Block statement
struct BlockNode {
    ASTNode** statements;
    int statementCount;
};

// If statement
struct IfStmtNode {
    ASTNode* condition;
    ASTNode* thenBranch;
    ASTNode* elseBranch;
};

// While statement
struct WhileStmtNode {
    ASTNode* condition;
    ASTNode* body;
};

// For statement
struct ForStmtNode {
    char varName[64];
    ASTNode* start;
    ASTNode* end;
    ASTNode* step;
    ASTNode* body;
};

// Return statement
struct ReturnStmtNode {
    ASTNode* value;
};

// Expression statement
struct ExprStmtNode {
    ASTNode* expression;
};

// Binary expression
struct BinaryExprNode {
    ASTNode* left;
    TokenType op;
    ASTNode* right;
};

// Unary expression
struct UnaryExprNode {
    TokenType op;
    ASTNode* operand;
    bool prefix;
};

// Call expression
struct CallExprNode {
    ASTNode* callee;
    ASTNode** arguments;
    int argCount;
};

// Member access expression
struct MemberExprNode {
    ASTNode* object;
    char member[64];
};

// Index expression
struct IndexExprNode {
    ASTNode* object;
    ASTNode* index;
};

// Literal expression
struct LiteralExprNode {
    Token value;
};

// Identifier expression
struct IdentifierExprNode {
    char name[64];
};

// Assignment expression
struct AssignExprNode {
    ASTNode* target;
    TokenType op;
    ASTNode* value;
};

// Array expression
struct ArrayExprNode {
    ASTNode** elements;
    int elementCount;
};

// AST Node structure
struct ASTNode {
    NodeType type;
    int line;
    
    union {
        ProgramNode program;
        FunctionDeclNode funcDecl;
        StructDeclNode structDecl;
        EntityDeclNode entityDecl;
        VarDeclNode varDecl;
        BlockNode block;
        IfStmtNode ifStmt;
        WhileStmtNode whileStmt;
        ForStmtNode forStmt;
        ReturnStmtNode returnStmt;
        ExprStmtNode exprStmt;
        BinaryExprNode binaryExpr;
        UnaryExprNode unaryExpr;
        CallExprNode callExpr;
        MemberExprNode memberExpr;
        IndexExprNode indexExpr;
        LiteralExprNode literalExpr;
        IdentifierExprNode identExpr;
        AssignExprNode assignExpr;
        ArrayExprNode arrayExpr;
    };
};

// Memory management
ASTNode* astCreate(NodeType type, int line);
void astFree(ASTNode* node);
void astFreeAll(ASTNode* root);

} // namespace td
