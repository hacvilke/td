// =============================================================================
// TD Engine — TDScript AST (Tier 4)
//
// All AST nodes are value types (std::shared_ptr<std::string> for identifiers)
// so they can be cheaply copied and stored in vectors. No virtual dispatch
// on the node itself; the codegen uses std::visit-style if-chains on the
// NodeKind tag.
//
// Design choice: we use a tagged union via std::variant-like dispatch instead
// of a class hierarchy. This keeps the AST allocation-friendly (one type,
// one allocator) and makes the parser/codegen easier to reason about.
// =============================================================================
#pragma once

#include <string>
#include <memory>
#include <vector>
#include <cstdint>

namespace td::tdscript {

// Forward
struct Node;
using NodePtr = std::shared_ptr<Node>;
using NodeList = std::vector<NodePtr>;

// -----------------------------------------------------------------------------
// Token kinds (shared between lexer and parser; declared here so the AST can
// reference them without including lexer.h)
// -----------------------------------------------------------------------------
enum class TokenKind {
    // Punctuation
    LParen, RParen, LBrace, RBrace, LBracket, RBracket,
    Semicolon, Comma, Dot, Colon, Arrow, At,
    // Operators
    Assign, Plus, Minus, Star, Slash, Percent,
    Eq, NotEq, Lt, Gt, LtEq, GtEq,
    And, Or, Not,
    PlusAssign, MinusAssign, StarAssign, SlashAssign,
    // Literals
    IntLit, FloatLit, StringLit, BoolLit,
    // Keywords
    KwImport, KwStruct, KwClass, KwPublic, KwPrivate, KwProtected,
    KwVoid, KwReturn, KwIf, KwElse, KwFor, KwWhile, KwBreak, KwContinue,
    KwTrue, KwFalse, KwNull,
    KwReplicated,  // `replicated` qualifier
    KwVar, KwConst, KwFunction,  // explicit decl keywords (optional alt)
    // Type keywords
    KwInt32, KwUint32, KwInt64, KwUint64, KwFloat, KwDouble, KwBool, KwString, KwAuto,
    // Identifier
    Ident,
    // Special
    EndOfFile,
};

// -----------------------------------------------------------------------------
// AST node kinds
// -----------------------------------------------------------------------------
enum class NodeKind {
    // Top-level
    Module,           // root: list of declarations + imports
    ImportStmt,       // import "path";
    StructDecl,       // struct Name { fields... }
    ClassDecl,        // class Name { members... }
    FieldDecl,        // [replicated] Type name [= init];
    MethodDecl,       // [@rpc(...)] [public|private] Ret name(params) { body }
    ParamDecl,        // Type name
    // Statements
    Block,            // { ... }
    IfStmt,           // if (cond) {...} else {...}
    ForStmt,          // for (init; cond; step) {...}
    WhileStmt,        // while (cond) {...}
    ReturnStmt,       // return [expr];
    BreakStmt,        // break;
    ContinueStmt,     // continue;
    VarDeclStmt,      // [Type|var|const] name [= expr];
    ExprStmt,         // expr;
    // Expressions
    IntLiteral,
    FloatLiteral,
    StringLiteral,
    BoolLiteral,
    NullLiteral,
    Identifier,
    MemberAccess,     // expr.name
    IndexAccess,      // expr[expr]
    Call,             // expr(args...)
    Unary,            // op expr
    Binary,           // expr op expr
    Assign,           // lhs = rhs
    CompoundAssign,   // lhs op= rhs
    New,              // new Type(args...)
    Cast,             // (Type) expr
    This,             // this
    // Types
    TypeRef,          // int32, Vector3, etc.
    RpcDecorator,     // @rpc(reliable|unreliable)
};

// RPC modes
enum class RpcMode {
    None,         // not an RPC
    Reliable,     // @rpc(reliable) — guaranteed delivery, ordered
    Unreliable,   // @rpc(unreliable) — may drop, may reorder (UDP-style)
};

// Visibility
enum class Visibility {
    Public,
    Private,
    Protected,
};

// -----------------------------------------------------------------------------
// AST node — tagged union via a single struct with optional fields.
// We use a flat struct (not std::variant) for simplicity; most fields are
// unused for any given NodeKind, which is fine for AST sizes (<1000 nodes).
// -----------------------------------------------------------------------------
struct Node {
    NodeKind kind;
    std::string text;             // identifier name, struct/class name, type name, op string
    int64_t intVal = 0;           // IntLiteral
    double floatVal = 0.0;        // FloatLiteral
    bool boolVal = false;         // BoolLiteral
    bool isReplicated = false;    // FieldDecl
    RpcMode rpcMode = RpcMode::None;  // MethodDecl
    Visibility visibility = Visibility::Public;  // MethodDecl, FieldDecl
    NodeList children;            // children (params, body, etc.)
    NodePtr type;                 // declared type (for FieldDecl, VarDecl, ParamDecl, MethodDecl return type)
    NodePtr initExpr;             // initial value (for FieldDecl, VarDecl)
    NodePtr lhs;                  // Binary/Assign/MemberAccess/IndexAccess
    NodePtr rhs;                  // Binary/Assign
    NodePtr cond;                 // IfStmt/WhileStmt/ForStmt condition
    NodePtr thenBranch;           // IfStmt
    NodePtr elseBranch;           // IfStmt
    NodePtr step;                 // ForStmt step expression
    int line = 0;                 // source line (for error messages)
    int col = 0;                  // source column

    explicit Node(NodeKind k) : kind(k) {}

    // Convenience constructors
    static NodePtr make(NodeKind k) { return std::make_shared<Node>(k); }
    static NodePtr makeText(NodeKind k, const std::string& s) {
        auto n = std::make_shared<Node>(k);
        n->text = s;
        return n;
    }
};

// -----------------------------------------------------------------------------
// Diagnostic (error/warning) — collected during lex/parse/codegen
// -----------------------------------------------------------------------------
struct Diagnostic {
    enum Severity { Error, Warning, Info };
    Severity severity = Error;
    std::string message;
    int line = 0;
    int col = 0;
};

} // namespace td::tdscript
