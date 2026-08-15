// =============================================================================
// TD Engine - Real ScriptVM implementation (Tier 1.3, wave1-scriptvm)
//
// Replaces the SKELETON stub with a real custom Lua-like bytecode VM called
// "tdscript". Single-file, portable C++17, no external libraries (honors the
// engine's "zero external libraries" principle).
//
// Architecture (4 stages, all in this file):
//   1. Lexer        — tokenizes tdscript source.
//   2. Parser       — recursive-descent, builds an AST.
//   3. Compiler     — walks the AST, emits flat bytecode (vector<uint8_t>).
//   4. VM           — stack-based dispatch loop runs the bytecode.
//
// The td.* library is registered into every script's globals and bridges to
// the engine's World / SignalBus / InputState / TimeState.
//
// See script_vm_internal.h for the test/internal API surface.
// See script_vm.h for the frozen public API.
//
// Performance: switch-dispatch with a tight inner loop. Targets ~10M
// opcodes/sec on a modern desktop CPU.
//
// Known limitations (vs. real Lua):
//   - Upvalues are SNAPSHOT-captured: a closure sees the value of an
//     outer-scope variable at closure-CREATION time. Mutating the closure's
//     copy does not propagate back. (Top-level locals are script globals,
//     so they DO see mutation.)
//   - `...` varargs evaluates to a TABLE of extra args, not a multi-value.
//     `local a, b = ...` therefore sets a=table, b=nil.
//   - `return f()` propagates only f's first return value.
//   - No metatables, no coroutines, no goto/labels, no pcall.
//   - String patterns (Lua's `string.find` with patterns) are NOT supported.
//   - Integer/float distinction is collapsed to double.
//   - Tables use std::map (O(log N) per access) — fine for gameplay scripts.
// =============================================================================

#include "script_vm.h"
#include "script_vm_internal.h"

#include <algorithm>
#include <atomic>
#include <cassert>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>   // std::rand, std::srand, RAND_MAX — MSVC's <fstream> pulls this in transitively but it's not guaranteed
#include <cstring>
#include <ctime>
#include <deque>
#include <fstream>
#include <map>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <sys/stat.h>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

// sys/stat.h is needed for file mtime; on Windows MinGW it provides _stat.
// Portable wrapper:
#ifdef _WIN32
  #define TD_STAT _stat
  typedef struct _stat td_stat_t;
#else
  #define TD_STAT stat
  typedef struct stat td_stat_t;
#endif

namespace td {
namespace script {

// ===========================================================================
// Value / Table method implementations (forward-declared in internal header)
// ===========================================================================

Value Value::makeTable() {
    Value v; v.type = Type::Table; v.tblVal = std::make_shared<Table>(); return v;
}
Value Value::makeFunc(std::shared_ptr<Function> f) {
    Value v; v.type = Type::Function; v.fnVal = std::move(f); return v;
}

std::string Value::typeName() const {
    switch (type) {
        case Type::Nil:      return "nil";
        case Type::Bool:     return "boolean";
        case Type::Number:   return "number";
        case Type::String:   return "string";
        case Type::Table:    return "table";
        case Type::Function: return "function";
    }
    return "unknown";
}

std::string Value::toString() const {
    switch (type) {
        case Type::Nil:      return "nil";
        case Type::Bool:     return boolVal ? "true" : "false";
        case Type::Number: {
            if (std::isnan(numVal)) return "nan";
            if (std::isinf(numVal)) return numVal < 0 ? "-inf" : "inf";
            if (std::floor(numVal) == numVal && std::abs(numVal) < 1e15) {
                char buf[32];
                std::snprintf(buf, sizeof(buf), "%lld", (long long)numVal);
                return buf;
            }
            char buf[32];
            std::snprintf(buf, sizeof(buf), "%.14g", numVal);
            return buf;
        }
        case Type::String:   return *strVal;
        case Type::Table: {
            char buf[48];
            std::snprintf(buf, sizeof(buf), "table: %p", (void*)tblVal.get());
            return buf;
        }
        case Type::Function: {
            char buf[48];
            std::snprintf(buf, sizeof(buf), "function: %p", (void*)fnVal.get());
            return buf;
        }
    }
    return "unknown";
}

bool Value::equals(const Value& o) const {
    if (type != o.type) return false;
    switch (type) {
        case Type::Nil:      return true;
        case Type::Bool:     return boolVal == o.boolVal;
        case Type::Number:   return numVal == o.numVal;
        case Type::String:   return *strVal == *o.strVal;
        case Type::Table:    return tblVal.get() == o.tblVal.get();
        case Type::Function: return fnVal.get() == o.fnVal.get();
    }
    return false;
}

bool Value::lessThan(const Value& o) const {
    if (type == Type::Number && o.type == Type::Number) return numVal < o.numVal;
    if (type == Type::String && o.type == Type::String) return *strVal < *o.strVal;
    throw std::runtime_error("attempt to compare two " + typeName() + " values");
}

bool Value::lessEqual(const Value& o) const {
    if (type == Type::Number && o.type == Type::Number) return numVal <= o.numVal;
    if (type == Type::String && o.type == Type::String) return *strVal <= *o.strVal;
    throw std::runtime_error("attempt to compare two " + typeName() + " values");
}

bool operator<(const Value& a, const Value& b) {
    if (a.type != b.type) return (int)a.type < (int)b.type;
    switch (a.type) {
        case Value::Type::Nil:      return false;
        case Value::Type::Bool:     return (int)a.boolVal < (int)b.boolVal;
        case Value::Type::Number:   return a.numVal < b.numVal;
        case Value::Type::String:   return *a.strVal < *b.strVal;
        case Value::Type::Table:    return a.tblVal.get() < b.tblVal.get();
        case Value::Type::Function: return a.fnVal.get() < b.fnVal.get();
    }
    return false;
}

int Table::length() const {
    int n = 0;
    while (true) {
        auto it = map.find(Value::makeNum(n + 1));
        if (it == map.end()) break;
        n++;
        if (n > 100000000) break;  // safety
    }
    return n;
}

// ===========================================================================
// Lexer
// ===========================================================================

enum class Tok : uint8_t {
    Number, String, Name,
    KwLocal, KwFunction, KwEnd, KwIf, KwThen, KwElseif, KwElse,
    KwWhile, KwDo, KwFor, KwIn, KwReturn, KwBreak,
    KwTrue, KwFalse, KwNil, KwAnd, KwOr, KwNot,
    OpAdd, OpSub, OpMul, OpDiv, OpMod, OpPow, OpConcat, OpLen, OpEq, OpNe,
    OpLt, OpGt, OpLe, OpGe, OpAssign, OpLParen, OpRParen, OpLBrace, OpRBrace,
    OpLBracket, OpRBracket, OpSemicolon, OpColon, OpComma, OpDot, OpVararg,
    Eof,
};

struct Token {
    Tok tok = Tok::Eof;
    std::string text;
    double num = 0.0;
    int line = 1;
    int col = 1;
};

class Lexer {
public:
    Lexer(const char* src, std::string name) : src_(src), name_(std::move(name)) {}
    std::vector<Token> tokenize();
    const std::string& name() const { return name_; }

private:
    const char* src_;
    std::string name_;
    size_t pos_ = 0;
    int line_ = 1;
    int col_ = 1;

    [[noreturn]] void error(const std::string& msg) {
        throw std::runtime_error(name_ + ":" + std::to_string(line_) + ":" +
                                 std::to_string(col_) + ": " + msg);
    }
    char peek(size_t ahead = 0) const {
        return src_[pos_ + ahead] ? src_[pos_ + ahead] : '\0';
    }
    char advance() {
        char c = src_[pos_];
        if (c == '\0') return '\0';
        pos_++;
        if (c == '\n') { line_++; col_ = 1; } else col_++;
        return c;
    }
    bool match(char c) { if (peek() == c) { advance(); return true; } return false; }
    Token make(Tok t, int line, int col) { Token tok; tok.tok = t; tok.line = line; tok.col = col; return tok; }

    static Tok keyword(const std::string& s) {
        if (s == "local")    return Tok::KwLocal;
        if (s == "function") return Tok::KwFunction;
        if (s == "end")      return Tok::KwEnd;
        if (s == "if")       return Tok::KwIf;
        if (s == "then")     return Tok::KwThen;
        if (s == "elseif")   return Tok::KwElseif;
        if (s == "else")     return Tok::KwElse;
        if (s == "while")    return Tok::KwWhile;
        if (s == "do")       return Tok::KwDo;
        if (s == "for")      return Tok::KwFor;
        if (s == "in")       return Tok::KwIn;
        if (s == "return")   return Tok::KwReturn;
        if (s == "break")    return Tok::KwBreak;
        if (s == "true")     return Tok::KwTrue;
        if (s == "false")    return Tok::KwFalse;
        if (s == "nil")      return Tok::KwNil;
        if (s == "and")      return Tok::KwAnd;
        if (s == "or")       return Tok::KwOr;
        if (s == "not")      return Tok::KwNot;
        return Tok::Eof;
    }

    Token nextToken();
};

Token Lexer::nextToken() {
    while (true) {
        char c = peek();
        if (c == '\0') return make(Tok::Eof, line_, col_);
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') { advance(); continue; }
        if (c == '-' && peek(1) == '-') {
            advance(); advance();
            if (peek() == '[' && peek(1) == '[') {
                advance(); advance();
                while (peek() != '\0') {
                    if (peek() == ']' && peek(1) == ']') { advance(); advance(); break; }
                    advance();
                }
            } else {
                while (peek() != '\0' && peek() != '\n') advance();
            }
            continue;
        }
        break;
    }

    int sl = line_, sc = col_;
    char c = peek();

    if (std::isalpha((unsigned char)c) || c == '_') {
        std::string s;
        while (std::isalnum((unsigned char)peek()) || peek() == '_') s += advance();
        Tok k = keyword(s);
        if (k != Tok::Eof) return make(k, sl, sc);
        Token t = make(Tok::Name, sl, sc);
        t.text = std::move(s);
        return t;
    }

    if (std::isdigit((unsigned char)c) || (c == '.' && std::isdigit((unsigned char)peek(1)))) {
        std::string s;
        if (c == '.') { s += advance(); }
        while (std::isdigit((unsigned char)peek())) s += advance();
        if (peek() == '.') {
            if (s.find('.') != std::string::npos) error("malformed number");
            s += advance();
            while (std::isdigit((unsigned char)peek())) s += advance();
        }
        if (peek() == 'e' || peek() == 'E') {
            s += advance();
            if (peek() == '+' || peek() == '-') s += advance();
            while (std::isdigit((unsigned char)peek())) s += advance();
        }
        if (s == "0" && (peek() == 'x' || peek() == 'X')) {
            s += advance();
            while (std::isxdigit((unsigned char)peek())) s += advance();
            Token t = make(Tok::Number, sl, sc);
            t.num = (double)std::strtoull(s.c_str() + 2, nullptr, 16);
            return t;
        }
        Token t = make(Tok::Number, sl, sc);
        t.num = std::strtod(s.c_str(), nullptr);
        return t;
    }

    if (c == '"' || c == '\'') {
        char quote = advance();
        std::string s;
        while (true) {
            char ch = peek();
            if (ch == '\0') error("unterminated string");
            if (ch == '\n') error("unterminated string (newline)");
            if (ch == quote) { advance(); break; }
            if (ch == '\\') {
                advance();
                char esc = advance();
                switch (esc) {
                    case 'n': s += '\n'; break;
                    case 't': s += '\t'; break;
                    case 'r': s += '\r'; break;
                    case '\\': s += '\\'; break;
                    case '\'': s += '\''; break;
                    case '"': s += '"'; break;
                    case '0': s += '\0'; break;
                    case 'a': s += '\a'; break;
                    case 'b': s += '\b'; break;
                    case 'f': s += '\f'; break;
                    case 'v': s += '\v'; break;
                    default:  s += esc; break;
                }
            } else {
                s += advance();
            }
        }
        Token t = make(Tok::String, sl, sc);
        t.text = std::move(s);
        return t;
    }

    if (c == '[' && peek(1) == '[') {
        advance(); advance();
        std::string s;
        if (peek() == '\n') advance();
        while (true) {
            char ch = peek();
            if (ch == '\0') error("unterminated long string");
            if (ch == ']' && peek(1) == ']') { advance(); advance(); break; }
            s += advance();
        }
        Token t = make(Tok::String, sl, sc);
        t.text = std::move(s);
        return t;
    }

    advance();
    switch (c) {
        case '+': return make(Tok::OpAdd, sl, sc);
        case '-': return make(Tok::OpSub, sl, sc);
        case '*': return make(Tok::OpMul, sl, sc);
        case '/': return make(Tok::OpDiv, sl, sc);
        case '%': return make(Tok::OpMod, sl, sc);
        case '^': return make(Tok::OpPow, sl, sc);
        case '#': return make(Tok::OpLen, sl, sc);
        case '(': return make(Tok::OpLParen, sl, sc);
        case ')': return make(Tok::OpRParen, sl, sc);
        case '{': return make(Tok::OpLBrace, sl, sc);
        case '}': return make(Tok::OpRBrace, sl, sc);
        case '[': return make(Tok::OpLBracket, sl, sc);
        case ']': return make(Tok::OpRBracket, sl, sc);
        case ';': return make(Tok::OpSemicolon, sl, sc);
        case ':': return make(Tok::OpColon, sl, sc);
        case ',': return make(Tok::OpComma, sl, sc);
        case '=': return match('=') ? make(Tok::OpEq, sl, sc) : make(Tok::OpAssign, sl, sc);
        case '~': if (match('=')) return make(Tok::OpNe, sl, sc); error("expected '~='");
        case '<': return match('=') ? make(Tok::OpLe, sl, sc) : make(Tok::OpLt, sl, sc);
        case '>': return match('=') ? make(Tok::OpGe, sl, sc) : make(Tok::OpGt, sl, sc);
        case '.':
            if (peek() == '.') {
                advance();
                if (peek() == '.') { advance(); return make(Tok::OpVararg, sl, sc); }
                return make(Tok::OpConcat, sl, sc);
            }
            return make(Tok::OpDot, sl, sc);
    }
    error(std::string("unexpected character '") + c + "'");
}

std::vector<Token> Lexer::tokenize() {
    std::vector<Token> out;
    while (true) {
        Token t = nextToken();
        out.push_back(t);
        if (t.tok == Tok::Eof) break;
    }
    return out;
}

// ===========================================================================
// AST
// ===========================================================================

struct Expr;
struct Block;
using ExprPtr = std::shared_ptr<Expr>;
using BlockPtr = std::shared_ptr<Block>;

enum class ExprKind : uint8_t {
    Number, String, Bool, Nil, Name, Index, Field, Call, MethodCall,
    Function, BinOp, UnOp, Table, Vararg,
};

struct TableField {
    bool isArray = false;
    ExprPtr key;
    ExprPtr value;
};

struct Expr {
    ExprKind kind = ExprKind::Nil;
    double numVal = 0.0;
    std::string strVal;
    bool boolVal = false;
    Tok op = Tok::Eof;
    std::string name;
    ExprPtr left;
    ExprPtr right;
    ExprPtr operand;
    std::vector<ExprPtr> args;
    std::vector<TableField> fields;
    std::vector<std::string> params;
    bool isVariadic = false;
    BlockPtr body;
    std::string method;
    int line = 0;
};

enum class StmtKind : uint8_t {
    Local, Assign, If, While, ForNum, ForIn, Return, Break,
    FunctionDecl, ExprStmt, DoBlock,
};

struct Stmt {
    StmtKind kind = StmtKind::ExprStmt;
    std::vector<std::string> names;
    std::vector<ExprPtr> targets;
    std::vector<ExprPtr> values;
    std::vector<std::pair<ExprPtr, BlockPtr>> branches;
    BlockPtr elseBody;
    ExprPtr cond;
    ExprPtr start, end, step;
    std::string varName;
    std::vector<std::string> varNames;
    std::vector<ExprPtr> inExprs;
    BlockPtr body;
    std::vector<ExprPtr> retValues;
    std::string funcName;
    bool isLocal = false;
    ExprPtr funcExpr;
    ExprPtr expr;
    int line = 0;
};

struct Block { std::vector<Stmt> stmts; };

// ===========================================================================
// Parser (recursive descent, precedence climbing)
// ===========================================================================

class Parser {
public:
    Parser(std::vector<Token> toks, std::string name)
        : toks_(std::move(toks)), name_(std::move(name)) {}
    BlockPtr parseChunk();
    const std::string& name() const { return name_; }

private:
    std::vector<Token> toks_;
    std::string name_;
    size_t pos_ = 0;

    const Token& peek(size_t ahead = 0) const {
        static Token eof;
        if (pos_ + ahead >= toks_.size()) return eof;
        return toks_[pos_ + ahead];
    }
    const Token& advance() {
        const Token& t = toks_[pos_];
        if (pos_ < toks_.size() - 1) pos_++;
        return t;
    }
    bool check(Tok t) const { return peek().tok == t; }
    bool accept(Tok t) { if (check(t)) { advance(); return true; } return false; }
    const Token& expect(Tok t, const char* what) {
        if (!check(t)) error(std::string("expected ") + what);
        return advance();
    }
    [[noreturn]] void error(const std::string& msg) {
        const Token& tk = peek();
        throw std::runtime_error(name_ + ":" + std::to_string(tk.line) + ":" +
                                 std::to_string(tk.col) + ": " + msg);
    }

    Stmt parseStmt();
    Stmt parseLocal();
    Stmt parseFunctionDecl();
    ExprPtr parseFunctionBody();
    Stmt parseIf();
    Stmt parseWhile();
    Stmt parseFor();
    Stmt parseReturn();
    Stmt parseExprOrAssign();
    BlockPtr parseBlock(std::initializer_list<Tok> terms);
    ExprPtr parseExpr() { return parseBinary(0); }
    ExprPtr parseBinary(int minPrec);
    ExprPtr parseUnary();
    ExprPtr parseSuffixedExpr();
    ExprPtr parseCallArgs(ExprPtr func, const std::string& method, int line);
    ExprPtr parsePrimary();
    ExprPtr parseTable();

    int prec(Tok t) {
        switch (t) {
            case Tok::OpPow:    return 12;
            case Tok::OpMul: case Tok::OpDiv: case Tok::OpMod: return 10;
            case Tok::OpAdd: case Tok::OpSub: return 9;
            case Tok::OpConcat: return 8;
            case Tok::OpLt: case Tok::OpGt: case Tok::OpLe: case Tok::OpGe:
            case Tok::OpEq: case Tok::OpNe:  return 7;
            case Tok::KwAnd:    return 4;
            case Tok::KwOr:     return 3;
            default:            return -1;
        }
    }
    bool rightAssoc(Tok t) { return t == Tok::OpPow || t == Tok::OpConcat; }
};

BlockPtr Parser::parseChunk() {
    auto b = std::make_shared<Block>();
    while (peek().tok != Tok::Eof) {
        b->stmts.push_back(parseStmt());
    }
    return b;
}

Stmt Parser::parseStmt() {
    const Token& t = peek();
    switch (t.tok) {
        case Tok::KwLocal:    return parseLocal();
        case Tok::KwIf:       return parseIf();
        case Tok::KwWhile:    return parseWhile();
        case Tok::KwFor:      return parseFor();
        case Tok::KwReturn:   return parseReturn();
        case Tok::KwBreak:    { advance(); Stmt s; s.kind = StmtKind::Break; s.line = t.line; return s; }
        case Tok::KwFunction: return parseFunctionDecl();
        case Tok::KwDo: {
            advance();
            Stmt s; s.kind = StmtKind::DoBlock;
            s.body = parseBlock({Tok::KwEnd});
            expect(Tok::KwEnd, "'end'");
            s.line = t.line;
            return s;
        }
        case Tok::OpSemicolon: advance(); return parseStmt();
        default:              return parseExprOrAssign();
    }
}

Stmt Parser::parseLocal() {
    const Token& t = advance();
    Stmt s; s.kind = StmtKind::Local; s.line = t.line;
    if (check(Tok::KwFunction)) {
        advance();
        const Token& name = expect(Tok::Name, "function name");
        s.names.push_back(name.text);
        s.funcExpr = parseFunctionBody();
        s.isLocal = true;
        return s;
    }
    s.names.push_back(expect(Tok::Name, "local name").text);
    while (accept(Tok::OpComma)) s.names.push_back(expect(Tok::Name, "local name").text);
    if (accept(Tok::OpAssign)) {
        s.values.push_back(parseExpr());
        while (accept(Tok::OpComma)) s.values.push_back(parseExpr());
    }
    return s;
}

Stmt Parser::parseFunctionDecl() {
    const Token& t = advance();
    Stmt s; s.kind = StmtKind::FunctionDecl; s.line = t.line; s.isLocal = false;
    const Token& first = expect(Tok::Name, "function name");
    ExprPtr target = std::make_shared<Expr>();
    target->kind = ExprKind::Name;
    target->name = first.text;
    target->line = first.line;
    while (check(Tok::OpDot)) {
        advance();
        const Token& fld = expect(Tok::Name, "field name after '.'");
        auto fe = std::make_shared<Expr>();
        fe->kind = ExprKind::Field;
        fe->left = target;
        fe->name = fld.text;
        fe->line = fld.line;
        target = fe;
    }
    std::string methodName;
    if (accept(Tok::OpColon)) {
        const Token& m = expect(Tok::Name, "method name after ':'");
        methodName = m.text;
    }
    ExprPtr fn = parseFunctionBody();
    if (!methodName.empty()) {
        fn->params.insert(fn->params.begin(), "self");
    }
    if (target->kind == ExprKind::Name) {
        s.funcName = methodName.empty() ? target->name : (target->name + "." + methodName);
        if (!methodName.empty()) {
            // function name:method() -> name.method = function(self,...) end
            s.targets.push_back(target);
            s.values.push_back(fn);
            s.kind = StmtKind::Assign;
            s.funcName.clear();
        } else {
            s.funcExpr = fn;
        }
    } else {
        s.targets.push_back(target);
        s.values.push_back(fn);
        s.kind = StmtKind::Assign;
    }
    return s;
}

ExprPtr Parser::parseFunctionBody() {
    expect(Tok::OpLParen, "'('");
    auto fn = std::make_shared<Expr>();
    fn->kind = ExprKind::Function;
    fn->line = peek().line;
    if (!check(Tok::OpRParen)) {
        while (true) {
            if (check(Tok::OpVararg)) { advance(); fn->isVariadic = true; break; }
            const Token& p = expect(Tok::Name, "parameter name");
            fn->params.push_back(p.text);
            if (!accept(Tok::OpComma)) break;
        }
    }
    expect(Tok::OpRParen, "')'");
    fn->body = parseBlock({Tok::KwEnd});
    expect(Tok::KwEnd, "'end'");
    return fn;
}

Stmt Parser::parseIf() {
    const Token& t = advance();
    Stmt s; s.kind = StmtKind::If; s.line = t.line;
    ExprPtr cond = parseExpr();
    expect(Tok::KwThen, "'then'");
    BlockPtr body = parseBlock({Tok::KwElseif, Tok::KwElse, Tok::KwEnd});
    s.branches.push_back({cond, body});
    while (check(Tok::KwElseif)) {
        advance();
        ExprPtr c = parseExpr();
        expect(Tok::KwThen, "'then'");
        BlockPtr b = parseBlock({Tok::KwElseif, Tok::KwElse, Tok::KwEnd});
        s.branches.push_back({c, b});
    }
    if (accept(Tok::KwElse)) s.elseBody = parseBlock({Tok::KwEnd});
    expect(Tok::KwEnd, "'end'");
    return s;
}

Stmt Parser::parseWhile() {
    const Token& t = advance();
    Stmt s; s.kind = StmtKind::While; s.line = t.line;
    s.cond = parseExpr();
    expect(Tok::KwDo, "'do'");
    s.body = parseBlock({Tok::KwEnd});
    expect(Tok::KwEnd, "'end'");
    return s;
}

Stmt Parser::parseFor() {
    const Token& t = advance();
    Stmt s; s.line = t.line;
    const Token& first = expect(Tok::Name, "for variable");
    if (accept(Tok::OpAssign)) {
        s.kind = StmtKind::ForNum;
        s.varName = first.text;
        s.start = parseExpr();
        expect(Tok::OpComma, "','");
        s.end = parseExpr();
        if (accept(Tok::OpComma)) s.step = parseExpr();
        expect(Tok::KwDo, "'do'");
        s.body = parseBlock({Tok::KwEnd});
        expect(Tok::KwEnd, "'end'");
    } else {
        s.kind = StmtKind::ForIn;
        s.varNames.push_back(first.text);
        while (accept(Tok::OpComma)) s.varNames.push_back(expect(Tok::Name, "for variable").text);
        expect(Tok::KwIn, "'in'");
        s.inExprs.push_back(parseExpr());
        while (accept(Tok::OpComma)) s.inExprs.push_back(parseExpr());
        expect(Tok::KwDo, "'do'");
        s.body = parseBlock({Tok::KwEnd});
        expect(Tok::KwEnd, "'end'");
    }
    return s;
}

Stmt Parser::parseReturn() {
    const Token& t = advance();
    Stmt s; s.kind = StmtKind::Return; s.line = t.line;
    if (!check(Tok::KwEnd) && !check(Tok::KwElse) && !check(Tok::KwElseif) &&
        !check(Tok::Eof) && !check(Tok::OpSemicolon)) {
        s.retValues.push_back(parseExpr());
        while (accept(Tok::OpComma)) s.retValues.push_back(parseExpr());
    }
    accept(Tok::OpSemicolon);
    return s;
}

Stmt Parser::parseExprOrAssign() {
    const Token& t = peek();
    ExprPtr first = parseSuffixedExpr();
    if (check(Tok::OpAssign) || check(Tok::OpComma)) {
        Stmt s; s.kind = StmtKind::Assign; s.line = t.line;
        s.targets.push_back(first);
        while (accept(Tok::OpComma)) s.targets.push_back(parseSuffixedExpr());
        expect(Tok::OpAssign, "'='");
        s.values.push_back(parseExpr());
        while (accept(Tok::OpComma)) s.values.push_back(parseExpr());
        return s;
    }
    Stmt s; s.kind = StmtKind::ExprStmt; s.line = t.line; s.expr = first;
    return s;
}

BlockPtr Parser::parseBlock(std::initializer_list<Tok> terms) {
    auto b = std::make_shared<Block>();
    while (true) {
        Tok t = peek().tok;
        bool isTerm = false;
        for (Tok tk : terms) if (t == tk) { isTerm = true; break; }
        if (isTerm || t == Tok::Eof) break;
        b->stmts.push_back(parseStmt());
    }
    return b;
}

ExprPtr Parser::parseBinary(int minPrec) {
    ExprPtr left = parseUnary();
    while (true) {
        Tok t = peek().tok;
        int p = prec(t);
        if (p < minPrec) break;
        int nextMin = rightAssoc(t) ? p : p + 1;
        advance();
        ExprPtr right = parseBinary(nextMin);
        auto bin = std::make_shared<Expr>();
        bin->kind = ExprKind::BinOp;
        bin->op = t;
        bin->left = left;
        bin->right = right;
        bin->line = left->line;
        left = bin;
    }
    return left;
}

ExprPtr Parser::parseUnary() {
    const Token& t = peek();
    if (t.tok == Tok::OpSub || t.tok == Tok::KwNot || t.tok == Tok::OpLen) {
        advance();
        ExprPtr operand = parseUnary();
        auto u = std::make_shared<Expr>();
        u->kind = ExprKind::UnOp;
        u->op = t.tok;
        u->operand = operand;
        u->line = t.line;
        return u;
    }
    return parseSuffixedExpr();
}

ExprPtr Parser::parseSuffixedExpr() {
    ExprPtr e = parsePrimary();
    while (true) {
        const Token& t = peek();
        if (t.tok == Tok::OpDot) {
            advance();
            const Token& fld = expect(Tok::Name, "field name after '.'");
            auto f = std::make_shared<Expr>();
            f->kind = ExprKind::Field;
            f->left = e; f->name = fld.text; f->line = fld.line;
            e = f;
        } else if (t.tok == Tok::OpLBracket) {
            advance();
            ExprPtr idx = parseExpr();
            expect(Tok::OpRBracket, "']'");
            auto i = std::make_shared<Expr>();
            i->kind = ExprKind::Index;
            i->left = e; i->right = idx; i->line = t.line;
            e = i;
        } else if (t.tok == Tok::OpColon) {
            advance();
            const Token& m = expect(Tok::Name, "method name after ':'");
            e = parseCallArgs(e, m.text, t.line);
        } else if (t.tok == Tok::OpLParen) {
            e = parseCallArgs(e, "", t.line);
        } else if (t.tok == Tok::OpLBrace) {
            ExprPtr tbl = parseTable();
            auto call = std::make_shared<Expr>();
            call->kind = ExprKind::Call;
            call->left = e; call->args.push_back(tbl); call->line = t.line;
            e = call;
        } else if (t.tok == Tok::String) {
            const Token& s = advance();
            auto str = std::make_shared<Expr>();
            str->kind = ExprKind::String; str->strVal = s.text; str->line = s.line;
            auto call = std::make_shared<Expr>();
            call->kind = ExprKind::Call;
            call->left = e; call->args.push_back(str); call->line = s.line;
            e = call;
        } else {
            break;
        }
    }
    return e;
}

ExprPtr Parser::parseCallArgs(ExprPtr func, const std::string& method, int line) {
    auto call = std::make_shared<Expr>();
    call->kind = method.empty() ? ExprKind::Call : ExprKind::MethodCall;
    call->left = func;
    call->method = method;
    call->line = line;
    if (check(Tok::OpLParen)) {
        advance();
        if (!check(Tok::OpRParen)) {
            call->args.push_back(parseExpr());
            while (accept(Tok::OpComma)) call->args.push_back(parseExpr());
        }
        expect(Tok::OpRParen, "')'");
    } else if (check(Tok::OpLBrace)) {
        call->args.push_back(parseTable());
    } else if (check(Tok::String)) {
        const Token& s = advance();
        auto str = std::make_shared<Expr>();
        str->kind = ExprKind::String; str->strVal = s.text; str->line = s.line;
        call->args.push_back(str);
    }
    return call;
}

ExprPtr Parser::parsePrimary() {
    const Token& t = peek();
    switch (t.tok) {
        case Tok::Number: { advance(); auto e = std::make_shared<Expr>(); e->kind = ExprKind::Number; e->numVal = t.num; e->line = t.line; return e; }
        case Tok::String: { advance(); auto e = std::make_shared<Expr>(); e->kind = ExprKind::String; e->strVal = t.text; e->line = t.line; return e; }
        case Tok::KwTrue:  { advance(); auto e = std::make_shared<Expr>(); e->kind = ExprKind::Bool; e->boolVal = true;  e->line = t.line; return e; }
        case Tok::KwFalse: { advance(); auto e = std::make_shared<Expr>(); e->kind = ExprKind::Bool; e->boolVal = false; e->line = t.line; return e; }
        case Tok::KwNil:   { advance(); auto e = std::make_shared<Expr>(); e->kind = ExprKind::Nil;  e->line = t.line; return e; }
        case Tok::KwFunction: {
            advance();
            auto e = std::make_shared<Expr>();
            e->kind = ExprKind::Function;
            e->line = t.line;
            ExprPtr f = parseFunctionBody();
            e->params = f->params;
            e->isVariadic = f->isVariadic;
            e->body = f->body;
            return e;
        }
        case Tok::OpVararg: { advance(); auto e = std::make_shared<Expr>(); e->kind = ExprKind::Vararg; e->line = t.line; return e; }
        case Tok::OpLParen: { advance(); ExprPtr inner = parseExpr(); expect(Tok::OpRParen, "')'"); return inner; }
        case Tok::OpLBrace: return parseTable();
        case Tok::Name: { advance(); auto e = std::make_shared<Expr>(); e->kind = ExprKind::Name; e->name = t.text; e->line = t.line; return e; }
        default:
            error(std::string("unexpected token '") + "<tok>" + "' in expression");
    }
}

ExprPtr Parser::parseTable() {
    const Token& open = expect(Tok::OpLBrace, "'{'");
    auto tbl = std::make_shared<Expr>();
    tbl->kind = ExprKind::Table;
    tbl->line = open.line;
    if (!check(Tok::OpRBrace)) {
        while (true) {
            TableField f;
            if (check(Tok::OpLBracket)) {
                advance();
                f.key = parseExpr();
                expect(Tok::OpRBracket, "']'");
                expect(Tok::OpAssign, "'='");
                f.value = parseExpr();
                f.isArray = false;
            } else if (check(Tok::Name) && peek(1).tok == Tok::OpAssign) {
                const Token& n = advance();
                auto nameExpr = std::make_shared<Expr>();
                nameExpr->kind = ExprKind::String;
                nameExpr->strVal = n.text;
                nameExpr->line = n.line;
                f.key = nameExpr;
                advance();
                f.value = parseExpr();
                f.isArray = false;
            } else {
                f.value = parseExpr();
                f.isArray = true;
            }
            tbl->fields.push_back(f);
            if (!accept(Tok::OpComma) && !accept(Tok::OpSemicolon)) break;
            if (check(Tok::OpRBrace)) break;
        }
    }
    expect(Tok::OpRBrace, "'}'");
    return tbl;
}

// ===========================================================================
// Compiler
// ===========================================================================

class Compiler {
public:
    Compiler() { pushFunction("<main>", true); }
    std::shared_ptr<Proto> compile(BlockPtr chunk);

private:
    struct LocalEntry { std::string name; int slot = 0; };
    struct FuncCompiler {
        std::shared_ptr<Proto> proto;
        std::vector<LocalEntry> locals;
        std::vector<size_t> scopeMarks;
        std::vector<std::pair<bool, int>> upvalues;
        FuncCompiler* parent = nullptr;
        std::vector<size_t> breakJumps;
        int loopDepth = 0;
        int maxLocals = 0;  // peak locals.count() — needed because
                            // compileBlock's leaveScope shrinks `locals`
                            // before popFunction captures numLocals.
    };

    std::deque<FuncCompiler> funcs_;
    std::shared_ptr<Proto> mainProto_;

    FuncCompiler& current() { return funcs_.back(); }

    void pushFunction(const std::string& name, bool isMain) {
        FuncCompiler fc;
        fc.proto = std::make_shared<Proto>();
        fc.proto->name = name;
        if (isMain) mainProto_ = fc.proto;
        if (!funcs_.empty()) {
            fc.parent = &funcs_.back();
            funcs_.back().proto->protos.push_back(fc.proto);
        }
        funcs_.push_back(std::move(fc));
        enterScope();
    }
    std::shared_ptr<Proto> popFunction() {
        auto& fc = current();
        emit(fc.proto, (uint8_t)Op::Nil);
        emit(fc.proto, (uint8_t)Op::Return);
        emitU8(fc.proto, 1);
        auto proto = fc.proto;
        proto->upvalues = fc.upvalues;
        // Use PEAK locals count, not the current size — compileBlock's
        // leaveScope has already shrunk `locals` back to the function-entry
        // scope, but the bytecode references slots that were live during
        // the function. Using current size here would make the VM allocate
        // too small a locals vector and StoreLocal/LoadLocal would write/
        // read out of bounds.
        proto->numLocals = fc.maxLocals;
        leaveScope();
        funcs_.pop_back();
        return proto;
    }

    void enterScope() { current().scopeMarks.push_back(current().locals.size()); }
    void leaveScope() {
        auto& fc = current();
        if (fc.scopeMarks.empty()) return;
        size_t mark = fc.scopeMarks.back();
        fc.locals.resize(mark);
        fc.scopeMarks.pop_back();
    }

    int declareLocal(const std::string& name) {
        auto& fc = current();
        int slot = (int)fc.locals.size();
        fc.locals.push_back({name, slot});
        if ((int)fc.locals.size() > fc.maxLocals) {
            fc.maxLocals = (int)fc.locals.size();
        }
        return slot;
    }
    int resolveLocal(const std::string& name) {
        auto& fc = current();
        for (int i = (int)fc.locals.size() - 1; i >= 0; i--) {
            if (fc.locals[i].name == name) return fc.locals[i].slot;
        }
        return -1;
    }
    int resolveUpvalue(const std::string& name, int depth) {
        if (depth <= 0) return -1;
        FuncCompiler* p = funcs_[depth].parent;
        if (!p) return -1;
        for (int i = (int)p->locals.size() - 1; i >= 0; i--) {
            if (p->locals[i].name == name) return addUpvalue(depth, true, i);
        }
        int parentIdx = resolveUpvalue(name, depth - 1);
        if (parentIdx >= 0) return addUpvalue(depth, false, parentIdx);
        return -1;
    }
    int addUpvalue(int depth, bool isLocal, int idx) {
        FuncCompiler& fc = funcs_[depth];
        for (size_t i = 0; i < fc.upvalues.size(); i++) {
            if (fc.upvalues[i].first == isLocal && fc.upvalues[i].second == idx) return (int)i;
        }
        fc.upvalues.push_back({isLocal, idx});
        return (int)fc.upvalues.size() - 1;
    }

    void emit(std::shared_ptr<Proto> p, uint8_t b) { p->code.push_back(b); }
    void emitU8(std::shared_ptr<Proto> p, uint8_t b) { p->code.push_back(b); }
    void emitU16(std::shared_ptr<Proto> p, uint16_t v) {
        p->code.push_back((uint8_t)(v & 0xFF));
        p->code.push_back((uint8_t)((v >> 8) & 0xFF));
    }
    void emitI16(std::shared_ptr<Proto> p, int16_t v) { emitU16(p, (uint16_t)v); }
    size_t emitPlaceholderI16(std::shared_ptr<Proto> p) {
        size_t off = p->code.size();
        p->code.push_back(0); p->code.push_back(0);
        return off;
    }
    void patchI16(std::shared_ptr<Proto> p, size_t off, int16_t v) {
        p->code[off]     = (uint8_t)((uint16_t)v & 0xFF);
        p->code[off + 1] = (uint8_t)(((uint16_t)v >> 8) & 0xFF);
    }
    int addConstant(std::shared_ptr<Proto> p, Value v) {
        for (size_t i = 0; i < p->constants.size(); i++) {
            if (p->constants[i].equals(v)) return (int)i;
        }
        p->constants.push_back(std::move(v));
        return (int)p->constants.size() - 1;
    }
    int addStringConstant(std::shared_ptr<Proto> p, const std::string& s) {
        return addConstant(p, Value::makeStr(s));
    }

    void compileBlock(const Block& b) {
        enterScope();
        for (const auto& s : b.stmts) compileStmt(s);
        leaveScope();
    }
    void compileStmt(const Stmt& s);
    void compileLocal(const Stmt& s);
    void compileAssign(const Stmt& s);
    void compileAssignTarget(const ExprPtr& target);
    void compileIf(const Stmt& s);
    void compileWhile(const Stmt& s);
    void compileForNum(const Stmt& s);
    void compileForIn(const Stmt& s);
    void compileReturn(const Stmt& s);
    void compileBreak(const Stmt& s);
    void compileFunctionDecl(const Stmt& s);
    void compileExprStmt(const Stmt& s);
    void compileExpr(const ExprPtr& e, int nresults);
    void compileFunctionExpr(const ExprPtr& e);
    void compileTableExpr(const ExprPtr& e);
    void compileBinOp(const ExprPtr& e);
    void compileUnOp(const ExprPtr& e);
};

std::shared_ptr<Proto> Compiler::compile(BlockPtr chunk) {
    compileBlock(*chunk);
    // Implicit return nil.
    auto& fc = current();
    emit(fc.proto, (uint8_t)Op::Nil);
    emit(fc.proto, (uint8_t)Op::Return);
    emitU8(fc.proto, 1);
    popFunction();
    return mainProto_;
}

void Compiler::compileStmt(const Stmt& s) {
    switch (s.kind) {
        case StmtKind::Local:        compileLocal(s); break;
        case StmtKind::Assign:       compileAssign(s); break;
        case StmtKind::If:           compileIf(s); break;
        case StmtKind::While:        compileWhile(s); break;
        case StmtKind::ForNum:       compileForNum(s); break;
        case StmtKind::ForIn:        compileForIn(s); break;
        case StmtKind::Return:       compileReturn(s); break;
        case StmtKind::Break:        compileBreak(s); break;
        case StmtKind::FunctionDecl: compileFunctionDecl(s); break;
        case StmtKind::ExprStmt:     compileExprStmt(s); break;
        case StmtKind::DoBlock:      compileBlock(*s.body); break;
    }
}

void Compiler::compileLocal(const Stmt& s) {
    auto& fc = current();
    if (s.isLocal && s.funcExpr) {
        int slot = declareLocal(s.names[0]);
        compileExpr(s.funcExpr, 1);
        emit(fc.proto, (uint8_t)Op::StoreLocal);
        emitU16(fc.proto, (uint16_t)slot);
        return;
    }
    size_t want = s.names.size();
    size_t provided = s.values.size();
    for (size_t i = 0; i < provided; i++) {
        compileExpr(s.values[i], 1);
    }
    for (size_t i = provided; i < want; i++) {
        emit(fc.proto, (uint8_t)Op::Nil);
    }
    // Store in reverse (top of stack is last value).
    for (size_t i = 0; i < want; i++) {
        int slot = declareLocal(s.names[want - 1 - i]);
        emit(fc.proto, (uint8_t)Op::StoreLocal);
        emitU16(fc.proto, (uint16_t)slot);
    }
}

void Compiler::compileAssign(const Stmt& s) {
    size_t ntargets = s.targets.size();
    size_t nvalues = s.values.size();
    for (size_t i = 0; i < nvalues; i++) compileExpr(s.values[i], 1);
    for (size_t i = nvalues; i < ntargets; i++) {
        auto& fc = current();
        emit(fc.proto, (uint8_t)Op::Nil);
    }
    for (size_t i = 0; i < ntargets; i++) {
        compileAssignTarget(s.targets[ntargets - 1 - i]);
    }
}

void Compiler::compileAssignTarget(const ExprPtr& target) {
    auto& fc = current();
    if (target->kind == ExprKind::Name) {
        int slot = resolveLocal(target->name);
        if (slot >= 0) {
            emit(fc.proto, (uint8_t)Op::StoreLocal);
            emitU16(fc.proto, (uint16_t)slot);
            return;
        }
        int uv = resolveUpvalue(target->name, (int)funcs_.size() - 1);
        if (uv >= 0) {
            emit(fc.proto, (uint8_t)Op::StoreUpval);
            emitU8(fc.proto, (uint8_t)uv);
            return;
        }
        int ci = addStringConstant(fc.proto, target->name);
        emit(fc.proto, (uint8_t)Op::StoreGlobal);
        emitU16(fc.proto, (uint16_t)ci);
        return;
    }
    if (target->kind == ExprKind::Field) {
        compileExpr(target->left, 1);
        int ci = addStringConstant(fc.proto, target->name);
        emit(fc.proto, (uint8_t)Op::SetField);
        emitU16(fc.proto, (uint16_t)ci);
        return;
    }
    if (target->kind == ExprKind::Index) {
        compileExpr(target->left, 1);
        compileExpr(target->right, 1);
        emit(fc.proto, (uint8_t)Op::SetIndex);
        return;
    }
    throw std::runtime_error("cannot assign to this expression");
}

void Compiler::compileIf(const Stmt& s) {
    auto& fc = current();
    std::vector<size_t> endJumps;
    for (const auto& br : s.branches) {
        compileExpr(br.first, 1);
        emit(fc.proto, (uint8_t)Op::JumpIfFalse);
        size_t j1 = emitPlaceholderI16(fc.proto);
        compileBlock(*br.second);
        emit(fc.proto, (uint8_t)Op::Jump);
        size_t endJ = emitPlaceholderI16(fc.proto);
        endJumps.push_back(endJ);
        patchI16(fc.proto, j1, (int16_t)(fc.proto->code.size() - (j1 + 2)));
    }
    if (s.elseBody) compileBlock(*s.elseBody);
    for (size_t j : endJumps) {
        patchI16(fc.proto, j, (int16_t)(fc.proto->code.size() - (j + 2)));
    }
}

void Compiler::compileWhile(const Stmt& s) {
    auto& fc = current();
    size_t start = fc.proto->code.size();
    compileExpr(s.cond, 1);
    emit(fc.proto, (uint8_t)Op::JumpIfFalse);
    size_t exitJ = emitPlaceholderI16(fc.proto);
    auto savedBreaks = fc.breakJumps;
    fc.breakJumps.clear();
    fc.loopDepth++;
    compileBlock(*s.body);
    fc.loopDepth--;
    emit(fc.proto, (uint8_t)Op::Jump);
    int16_t back = (int16_t)(start - (fc.proto->code.size() + 2));
    emitI16(fc.proto, back);
    size_t end = fc.proto->code.size();
    patchI16(fc.proto, exitJ, (int16_t)(end - (exitJ + 2)));
    for (size_t bj : fc.breakJumps) patchI16(fc.proto, bj, (int16_t)(end - (bj + 2)));
    fc.breakJumps = std::move(savedBreaks);
}

void Compiler::compileForNum(const Stmt& s) {
    auto& fc = current();
    enterScope();
    int startSlot = declareLocal("__for_start");
    int endSlot   = declareLocal("__for_end");
    int stepSlot  = declareLocal("__for_step");
    int varSlot   = declareLocal(s.varName);
    compileExpr(s.start, 1);
    emit(fc.proto, (uint8_t)Op::StoreLocal); emitU16(fc.proto, (uint16_t)startSlot);
    compileExpr(s.end, 1);
    emit(fc.proto, (uint8_t)Op::StoreLocal); emitU16(fc.proto, (uint16_t)endSlot);
    if (s.step) compileExpr(s.step, 1);
    else {
        emit(fc.proto, (uint8_t)Op::Const);
        int one = addConstant(fc.proto, Value::makeNum(1.0));
        emitU16(fc.proto, (uint16_t)one);
    }
    emit(fc.proto, (uint8_t)Op::StoreLocal); emitU16(fc.proto, (uint16_t)stepSlot);
    emit(fc.proto, (uint8_t)Op::LoadLocal); emitU16(fc.proto, (uint16_t)startSlot);
    emit(fc.proto, (uint8_t)Op::StoreLocal); emitU16(fc.proto, (uint16_t)varSlot);

    size_t loopStart = fc.proto->code.size();
    // Test: (step >= 0 and var <= end) or (step < 0 and var >= end)
    emit(fc.proto, (uint8_t)Op::LoadLocal); emitU16(fc.proto, (uint16_t)stepSlot);
    emit(fc.proto, (uint8_t)Op::Const);
    int zero = addConstant(fc.proto, Value::makeNum(0.0));
    emitU16(fc.proto, (uint16_t)zero);
    emit(fc.proto, (uint8_t)Op::Ge);
    emit(fc.proto, (uint8_t)Op::JumpIfFalse);
    size_t negBranch = emitPlaceholderI16(fc.proto);
    emit(fc.proto, (uint8_t)Op::LoadLocal); emitU16(fc.proto, (uint16_t)varSlot);
    emit(fc.proto, (uint8_t)Op::LoadLocal); emitU16(fc.proto, (uint16_t)endSlot);
    emit(fc.proto, (uint8_t)Op::Le);
    emit(fc.proto, (uint8_t)Op::Jump);
    size_t skipNeg = emitPlaceholderI16(fc.proto);
    patchI16(fc.proto, negBranch, (int16_t)(fc.proto->code.size() - (negBranch + 2)));
    emit(fc.proto, (uint8_t)Op::LoadLocal); emitU16(fc.proto, (uint16_t)varSlot);
    emit(fc.proto, (uint8_t)Op::LoadLocal); emitU16(fc.proto, (uint16_t)endSlot);
    emit(fc.proto, (uint8_t)Op::Ge);
    patchI16(fc.proto, skipNeg, (int16_t)(fc.proto->code.size() - (skipNeg + 2)));
    emit(fc.proto, (uint8_t)Op::JumpIfFalse);
    size_t exitJ = emitPlaceholderI16(fc.proto);

    auto savedBreaks = fc.breakJumps;
    fc.breakJumps.clear();
    fc.loopDepth++;
    compileBlock(*s.body);
    fc.loopDepth--;
    emit(fc.proto, (uint8_t)Op::LoadLocal); emitU16(fc.proto, (uint16_t)varSlot);
    emit(fc.proto, (uint8_t)Op::LoadLocal); emitU16(fc.proto, (uint16_t)stepSlot);
    emit(fc.proto, (uint8_t)Op::Add);
    emit(fc.proto, (uint8_t)Op::StoreLocal); emitU16(fc.proto, (uint16_t)varSlot);
    emit(fc.proto, (uint8_t)Op::Jump);
    int16_t back = (int16_t)(loopStart - (fc.proto->code.size() + 2));
    emitI16(fc.proto, back);
    size_t end = fc.proto->code.size();
    patchI16(fc.proto, exitJ, (int16_t)(end - (exitJ + 2)));
    for (size_t bj : fc.breakJumps) patchI16(fc.proto, bj, (int16_t)(end - (bj + 2)));
    fc.breakJumps = std::move(savedBreaks);
    leaveScope();
}

void Compiler::compileForIn(const Stmt& s) {
    auto& fc = current();
    enterScope();
    int fSlot   = declareLocal("__for_f");
    int sSlot   = declareLocal("__for_s");
    int varSlot = declareLocal("__for_var");
    // Evaluate the expression list — we only support single-expr `for ... in EXPR`.
    // The expr should produce 3 values (iterator, state, init_var).
    if (s.inExprs.size() != 1) {
        throw std::runtime_error("for-in: only single expression supported (use pairs/ipairs)");
    }
    compileExpr(s.inExprs[0], 3);
    emit(fc.proto, (uint8_t)Op::StoreLocal); emitU16(fc.proto, (uint16_t)varSlot);
    emit(fc.proto, (uint8_t)Op::StoreLocal); emitU16(fc.proto, (uint16_t)sSlot);
    emit(fc.proto, (uint8_t)Op::StoreLocal); emitU16(fc.proto, (uint16_t)fSlot);

    enterScope();
    std::vector<int> userSlots;
    for (const auto& name : s.varNames) userSlots.push_back(declareLocal(name));
    int nvars = (int)s.varNames.size();

    size_t loopStart = fc.proto->code.size();
    emit(fc.proto, (uint8_t)Op::LoadLocal); emitU16(fc.proto, (uint16_t)fSlot);
    emit(fc.proto, (uint8_t)Op::LoadLocal); emitU16(fc.proto, (uint16_t)sSlot);
    emit(fc.proto, (uint8_t)Op::LoadLocal); emitU16(fc.proto, (uint16_t)varSlot);
    emit(fc.proto, (uint8_t)Op::Call);
    emitU8(fc.proto, 2);
    emitU8(fc.proto, (uint8_t)nvars);
    for (int i = 0; i < nvars; i++) {
        emit(fc.proto, (uint8_t)Op::StoreLocal);
        emitU16(fc.proto, (uint16_t)userSlots[nvars - 1 - i]);
    }
    // If first var is nil, exit.
    emit(fc.proto, (uint8_t)Op::LoadLocal); emitU16(fc.proto, (uint16_t)userSlots[0]);
    emit(fc.proto, (uint8_t)Op::Nil);
    emit(fc.proto, (uint8_t)Op::Eq);
    emit(fc.proto, (uint8_t)Op::JumpIfTrue);
    size_t exitJ = emitPlaceholderI16(fc.proto);
    emit(fc.proto, (uint8_t)Op::LoadLocal); emitU16(fc.proto, (uint16_t)userSlots[0]);
    emit(fc.proto, (uint8_t)Op::StoreLocal); emitU16(fc.proto, (uint16_t)varSlot);

    auto savedBreaks = fc.breakJumps;
    fc.breakJumps.clear();
    fc.loopDepth++;
    compileBlock(*s.body);
    fc.loopDepth--;
    emit(fc.proto, (uint8_t)Op::Jump);
    int16_t back = (int16_t)(loopStart - (fc.proto->code.size() + 2));
    emitI16(fc.proto, back);
    size_t end = fc.proto->code.size();
    patchI16(fc.proto, exitJ, (int16_t)(end - (exitJ + 2)));
    for (size_t bj : fc.breakJumps) patchI16(fc.proto, bj, (int16_t)(end - (bj + 2)));
    fc.breakJumps = std::move(savedBreaks);
    leaveScope();
    leaveScope();
}

void Compiler::compileReturn(const Stmt& s) {
    auto& fc = current();
    for (const auto& v : s.retValues) compileExpr(v, 1);
    emit(fc.proto, (uint8_t)Op::Return);
    emitU8(fc.proto, (uint8_t)s.retValues.size());
}

void Compiler::compileBreak(const Stmt&) {
    auto& fc = current();
    if (fc.loopDepth == 0) throw std::runtime_error("'break' outside of a loop");
    emit(fc.proto, (uint8_t)Op::Jump);
    fc.breakJumps.push_back(emitPlaceholderI16(fc.proto));
}

void Compiler::compileFunctionDecl(const Stmt& s) {
    if (s.kind == StmtKind::Assign) {
        compileAssign(s);
        return;
    }
    // Global function: foo = function() ... end
    Stmt assign;
    assign.kind = StmtKind::Assign;
    auto target = std::make_shared<Expr>();
    target->kind = ExprKind::Name;
    target->name = s.funcName;
    target->line = s.line;
    assign.targets.push_back(target);
    assign.values.push_back(s.funcExpr);
    compileAssign(assign);
}

void Compiler::compileExprStmt(const Stmt& s) {
    compileExpr(s.expr, 0);
}

void Compiler::compileExpr(const ExprPtr& e, int nresults) {
    auto& fc = current();
    switch (e->kind) {
        case ExprKind::Nil:
            emit(fc.proto, (uint8_t)Op::Nil);
            break;
        case ExprKind::Bool:
            emit(fc.proto, (uint8_t)(e->boolVal ? Op::True : Op::False));
            break;
        case ExprKind::Number: {
            int ci = addConstant(fc.proto, Value::makeNum(e->numVal));
            emit(fc.proto, (uint8_t)Op::Const);
            emitU16(fc.proto, (uint16_t)ci);
            break;
        }
        case ExprKind::String: {
            int ci = addStringConstant(fc.proto, e->strVal);
            emit(fc.proto, (uint8_t)Op::Const);
            emitU16(fc.proto, (uint16_t)ci);
            break;
        }
        case ExprKind::Vararg: {
            emit(fc.proto, (uint8_t)Op::LoadVararg);
            emitU8(fc.proto, 0xFF);
            break;
        }
        case ExprKind::Name: {
            int slot = resolveLocal(e->name);
            if (slot >= 0) {
                emit(fc.proto, (uint8_t)Op::LoadLocal);
                emitU16(fc.proto, (uint16_t)slot);
                break;
            }
            int uv = resolveUpvalue(e->name, (int)funcs_.size() - 1);
            if (uv >= 0) {
                emit(fc.proto, (uint8_t)Op::LoadUpval);
                emitU8(fc.proto, (uint8_t)uv);
                break;
            }
            int ci = addStringConstant(fc.proto, e->name);
            emit(fc.proto, (uint8_t)Op::LoadGlobal);
            emitU16(fc.proto, (uint16_t)ci);
            break;
        }
        case ExprKind::Field: {
            compileExpr(e->left, 1);
            int ci = addStringConstant(fc.proto, e->name);
            emit(fc.proto, (uint8_t)Op::GetField);
            emitU16(fc.proto, (uint16_t)ci);
            break;
        }
        case ExprKind::Index: {
            compileExpr(e->left, 1);
            compileExpr(e->right, 1);
            emit(fc.proto, (uint8_t)Op::GetIndex);
            break;
        }
        case ExprKind::Call: {
            compileExpr(e->left, 1);
            int nargs = 0;
            for (const auto& a : e->args) {
                compileExpr(a, 1);
                nargs++;
            }
            emit(fc.proto, (uint8_t)Op::Call);
            emitU8(fc.proto, (uint8_t)nargs);
            emitU8(fc.proto, (uint8_t)(nresults >= 0 ? nresults : 0xFF));
            return;
        }
        case ExprKind::MethodCall: {
            // Push obj, get obj.method, push obj (self) via Dup, push args.
            compileExpr(e->left, 1);
            // Stack: [obj]
            // Get field "method": pop obj, push obj.method. But we need obj later for self.
            // Use Dup: dup obj, then GetField on the dup.
            emit(fc.proto, (uint8_t)Op::Dup);             // [obj, obj]
            int ci = addStringConstant(fc.proto, e->method);
            emit(fc.proto, (uint8_t)Op::GetField);
            emitU16(fc.proto, (uint16_t)ci);              // [obj, obj.method]
            // Swap so order is [obj.method, obj, args...]
            // Easiest: we have [obj, obj.method]. We want [obj.method, obj, args...].
            // Pop obj.method into a temp, then push obj.method back, then args.
            // Or, simpler: re-arrange by emitting a swap op. We don't have one.
            // Alternative approach: emit obj, emit obj.method (re-evaluating
            // the receiver which is safe for lvalues), then push obj, then args.
            //
            // The current approach with Dup gives us [obj, obj.method]. To
            // call method(obj, args), we need [obj.method, obj, args].
            //
            // Let's pop obj.method, dup obj, push obj.method, push args. But
            // pop+push requires storing somewhere.
            //
            // Easier fix: don't use Dup. Instead, emit the receiver twice.
            // For Name, Field, Index this is safe and idempotent.
            //
            // Pop the Dup'd obj.method we just pushed (cancel the GetField):
            // Actually let me re-do this from scratch.
            //
            // Reset: emit Pop to discard obj.method, then re-emit obj twice.
            emit(fc.proto, (uint8_t)Op::Pop); emitU8(fc.proto, 1);  // [obj]
            emit(fc.proto, (uint8_t)Op::Pop); emitU8(fc.proto, 1);  // []
            // Now re-emit obj twice.
            compileExpr(e->left, 1);   // [obj]
            emit(fc.proto, (uint8_t)Op::Dup);  // [obj, obj]
            emit(fc.proto, (uint8_t)Op::GetField);
            emitU16(fc.proto, (uint16_t)ci);   // [obj, obj.method]
            // Still wrong order. Hmm.
            //
            // OK let me just add a Swap op. Easier.
            // For now, do this: pop obj.method, then pop obj, then push obj.method, then push obj, then args.
            //
            // Actually the simplest fix: change OP_CALL to handle method calls
            // by checking a "method call" bit. Or, do the reorder via temp
            // locals.
            //
            // Let me use temp locals. Allocate 2 temp slots at function scope.
            //
            // Pop obj.method into temp1, pop obj into temp2, push temp1, push temp2, push args.
            int tempMethod = declareLocal("__method_tmp");
            int tempObj = declareLocal("__obj_tmp");
            emit(fc.proto, (uint8_t)Op::StoreLocal); emitU16(fc.proto, (uint16_t)tempMethod);  // []
            emit(fc.proto, (uint8_t)Op::StoreLocal); emitU16(fc.proto, (uint16_t)tempObj);     // []
            emit(fc.proto, (uint8_t)Op::LoadLocal); emitU16(fc.proto, (uint16_t)tempMethod);   // [method]
            emit(fc.proto, (uint8_t)Op::LoadLocal); emitU16(fc.proto, (uint16_t)tempObj);      // [method, obj]
            int nargs = 1;
            for (const auto& a : e->args) {
                compileExpr(a, 1);
                nargs++;
            }
            emit(fc.proto, (uint8_t)Op::Call);
            emitU8(fc.proto, (uint8_t)nargs);
            emitU8(fc.proto, (uint8_t)(nresults >= 0 ? nresults : 0xFF));
            return;
        }
        case ExprKind::Function: {
            compileFunctionExpr(e);
            break;
        }
        case ExprKind::Table: {
            compileTableExpr(e);
            break;
        }
        case ExprKind::BinOp: {
            compileBinOp(e);
            break;
        }
        case ExprKind::UnOp: {
            compileUnOp(e);
            break;
        }
    }
    if (nresults == 0) {
        emit(fc.proto, (uint8_t)Op::Pop);
        emitU8(fc.proto, 1);
    } else if (nresults > 1) {
        for (int i = 1; i < nresults; i++) emit(fc.proto, (uint8_t)Op::Nil);
    }
}

void Compiler::compileFunctionExpr(const ExprPtr& e) {
    pushFunction("<anonymous>", false);
    auto& fc = current();
    fc.proto->name = "<anonymous>";
    fc.proto->isVariadic = e->isVariadic;
    fc.proto->numParams = (int)e->params.size();
    fc.proto->paramNames = e->params;
    enterScope();
    for (const auto& p : e->params) declareLocal(p);
    for (const auto& s : e->body->stmts) compileStmt(s);
    auto proto = fc.proto;
    proto->upvalues = fc.upvalues;
    // Use peak locals count (see popFunction for why).
    proto->numLocals = fc.maxLocals;
    leaveScope();
    funcs_.pop_back();

    auto& parent = current();
    int protoIdx = -1;
    for (size_t i = 0; i < parent.proto->protos.size(); i++) {
        if (parent.proto->protos[i].get() == proto.get()) { protoIdx = (int)i; break; }
    }
    if (protoIdx < 0) {
        parent.proto->protos.push_back(proto);
        protoIdx = (int)parent.proto->protos.size() - 1;
    }
    emit(parent.proto, (uint8_t)Op::Closure);
    emitU16(parent.proto, (uint16_t)protoIdx);
}

void Compiler::compileTableExpr(const ExprPtr& e) {
    auto& fc = current();
    emit(fc.proto, (uint8_t)Op::NewTable);
    for (const auto& f : e->fields) {
        if (f.isArray) {
            compileExpr(f.value, 1);
            // Stack: [t, value]. AppendTable pops value, peeks t, sets t[#t+1] = value.
            emit(fc.proto, (uint8_t)Op::AppendTable);
        } else {
            if (f.key && f.key->kind == ExprKind::String) {
                int ci = addStringConstant(fc.proto, f.key->strVal);
                compileExpr(f.value, 1);
                // Stack: [t, value]. SetFieldKeep pops value, peeks table,
                // sets t[name]=value, leaves table on stack for next field.
                emit(fc.proto, (uint8_t)Op::SetFieldKeep);
                emitU16(fc.proto, (uint16_t)ci);
            } else {
                compileExpr(f.key, 1);
                compileExpr(f.value, 1);
                // Stack: [t, key, value]. SetIndexKeep pops val+key, peeks t.
                emit(fc.proto, (uint8_t)Op::SetIndexKeep);
            }
        }
    }
}

void Compiler::compileBinOp(const ExprPtr& e) {
    auto& fc = current();
    if (e->op == Tok::KwAnd) {
        compileExpr(e->left, 1);
        emit(fc.proto, (uint8_t)Op::JumpIfFalseKeep);
        size_t j = emitPlaceholderI16(fc.proto);
        emit(fc.proto, (uint8_t)Op::Pop); emitU8(fc.proto, 1);
        compileExpr(e->right, 1);
        patchI16(fc.proto, j, (int16_t)(fc.proto->code.size() - (j + 2)));
        return;
    }
    if (e->op == Tok::KwOr) {
        compileExpr(e->left, 1);
        emit(fc.proto, (uint8_t)Op::JumpIfTrueKeep);
        size_t j = emitPlaceholderI16(fc.proto);
        emit(fc.proto, (uint8_t)Op::Pop); emitU8(fc.proto, 1);
        compileExpr(e->right, 1);
        patchI16(fc.proto, j, (int16_t)(fc.proto->code.size() - (j + 2)));
        return;
    }
    compileExpr(e->left, 1);
    compileExpr(e->right, 1);
    Op op = Op::Nil;
    switch (e->op) {
        case Tok::OpAdd: op = Op::Add; break;
        case Tok::OpSub: op = Op::Sub; break;
        case Tok::OpMul: op = Op::Mul; break;
        case Tok::OpDiv: op = Op::Div; break;
        case Tok::OpMod: op = Op::Mod; break;
        case Tok::OpPow: op = Op::Pow; break;
        case Tok::OpConcat: op = Op::Concat; break;
        case Tok::OpEq:  op = Op::Eq; break;
        case Tok::OpNe:  op = Op::Ne; break;
        case Tok::OpLt:  op = Op::Lt; break;
        case Tok::OpGt:  op = Op::Gt; break;
        case Tok::OpLe:  op = Op::Le; break;
        case Tok::OpGe:  op = Op::Ge; break;
        default: throw std::runtime_error("unknown binary operator");
    }
    emit(fc.proto, (uint8_t)op);
}

void Compiler::compileUnOp(const ExprPtr& e) {
    auto& fc = current();
    compileExpr(e->operand, 1);
    switch (e->op) {
        case Tok::OpSub: emit(fc.proto, (uint8_t)Op::Neg); break;
        case Tok::KwNot: emit(fc.proto, (uint8_t)Op::Not); break;
        case Tok::OpLen: emit(fc.proto, (uint8_t)Op::Len); break;
        default: throw std::runtime_error("unknown unary operator");
    }
}

// ===========================================================================
// VM
// ===========================================================================

// Engine dependencies (set via the internal API). File-scope statics.
// Single-threaded engine means this is safe; if we go multithreaded later
// these become thread_local.

// Forward declaration — defined later in td::script. Used by td_connect's
// signal-callback lambda to look up the calling script's env at dispatch
// time (so the callback survives hot reload).
ScriptEnv* lookupScriptEnv(int id);

namespace {
struct VMDeps {
    World* world = nullptr;
    InputState* input = nullptr;
    TimeState* time = nullptr;
};
VMDeps& deps() { static VMDeps d; return d; }

// Beat state for td.beat_*
struct BeatState {
    bool active = false;
    float bpm = 120.0f;
    float spb = 0.5f;       // 60/bpm
    double startTime = 0.0;
    double lastBeatTime = 0.0;
    double nextBeatTime = 0.0;
    int beatCount = 0;
    float windowHalf = 0.15f;
    int combo = 0;
    int bestCombo = 0;
};
BeatState& beatState() { static BeatState b; return b; }

// Current emit args (for td.emit -> callback dispatch).
thread_local std::vector<Value> g_emitArgs;

// Current script env (for td.* functions that need to know which script
// is calling — e.g., td.connect needs to associate the callback with the
// calling script so its subscriptions can be auto-disconnected on unload).
thread_local ScriptEnv* g_currentEnv = nullptr;

double nowSeconds() {
    auto t = std::chrono::steady_clock::now();
    auto us = std::chrono::duration_cast<std::chrono::microseconds>(t.time_since_epoch()).count();
    return (double)us * 1e-6;
}
} // namespace

class VM {
public:
    VM() = default;

    // Run a function with the given args. Returns the function's return values.
    std::vector<Value> call(std::shared_ptr<Function> fn, std::vector<Value> args,
                            std::map<std::string, Value>* globals);

    // Compile + execute a source chunk in a fresh ephemeral env.
    std::vector<Value> evalSource(const char* src, const std::string& name,
                                  std::vector<Value> args,
                                  std::map<std::string, Value>* globals);
};

// Forward decls for the stdlib + td.* registration.
void registerStdlib(std::map<std::string, Value>& globals);
void registerTdLib(std::map<std::string, Value>& globals);

std::vector<Value> VM::call(std::shared_ptr<Function> fn, std::vector<Value> args,
                            std::map<std::string, Value>* globals) {
    if (!fn) return {Value::makeNil()};
    if (fn->kind == Function::Native) {
        if (!fn->native) return {Value::makeNil()};
        return fn->native(args);
    }
    // Script function.
    auto& proto = fn->proto;
    if (!proto) return {Value::makeNil()};

    // Set up the frame.
    std::vector<Value> locals(proto->numLocals, Value::makeNil());
    int numParams = proto->numParams;
    for (int i = 0; i < numParams && i < (int)args.size(); i++) {
        locals[i] = args[i];
    }
    std::vector<Value> varargs;
    if (proto->isVariadic && (int)args.size() > numParams) {
        for (int i = numParams; i < (int)args.size(); i++) {
            varargs.push_back(args[i]);
        }
    }
    std::vector<Value> stack;
    stack.reserve(32);

    size_t ip = 0;
    const auto& code = proto->code;
    auto readU8 = [&]() -> uint8_t { return code[ip++]; };
    auto readU16 = [&]() -> uint16_t {
        uint16_t v = (uint16_t)code[ip] | ((uint16_t)code[ip + 1] << 8);
        ip += 2;
        return v;
    };
    auto readI16 = [&]() -> int16_t {
        uint16_t v = readU16();
        return (int16_t)v;
    };

    try {
        while (ip < code.size()) {
            Op op = (Op)code[ip++];
            switch (op) {
                case Op::Nil:  stack.push_back(Value::makeNil()); break;
                case Op::True: stack.push_back(Value::makeBool(true)); break;
                case Op::False:stack.push_back(Value::makeBool(false)); break;
                case Op::Const: {
                    uint16_t idx = readU16();
                    stack.push_back(proto->constants[idx]);
                    break;
                }
                case Op::LoadLocal: {
                    uint16_t idx = readU16();
                    stack.push_back(locals[idx]);
                    break;
                }
                case Op::StoreLocal: {
                    uint16_t idx = readU16();
                    locals[idx] = stack.back();
                    stack.pop_back();
                    break;
                }
                case Op::LoadGlobal: {
                    uint16_t idx = readU16();
                    const std::string& name = *proto->constants[idx].strVal;
                    if (globals) {
                        auto it = globals->find(name);
                        stack.push_back(it != globals->end() ? it->second : Value::makeNil());
                    } else {
                        stack.push_back(Value::makeNil());
                    }
                    break;
                }
                case Op::StoreGlobal: {
                    uint16_t idx = readU16();
                    const std::string& name = *proto->constants[idx].strVal;
                    if (globals) (*globals)[name] = stack.back();
                    stack.pop_back();
                    break;
                }
                case Op::LoadUpval: {
                    uint8_t idx = readU8();
                    stack.push_back(fn->upvalues[idx]);
                    break;
                }
                case Op::StoreUpval: {
                    uint8_t idx = readU8();
                    fn->upvalues[idx] = stack.back();
                    stack.pop_back();
                    break;
                }
                case Op::GetField: {
                    uint16_t idx = readU16();
                    Value obj = stack.back(); stack.pop_back();
                    if (!obj.isTable()) {
                        throw std::runtime_error("attempt to index a " + obj.typeName() + " value");
                    }
                    const std::string& name = *proto->constants[idx].strVal;
                    stack.push_back(obj.tblVal->get(Value::makeStr(name)));
                    break;
                }
                case Op::SetField: {
                    // Stack convention: [value, table] (value pushed by
                    // compileAssign, then table pushed by compileAssignTarget).
                    // Pop table first (top), then value (below).
                    uint16_t idx = readU16();
                    Value obj = stack.back(); stack.pop_back();
                    Value v = stack.back(); stack.pop_back();
                    if (!obj.isTable()) {
                        throw std::runtime_error("attempt to index a " + obj.typeName() + " value");
                    }
                    const std::string& name = *proto->constants[idx].strVal;
                    obj.tblVal->set(Value::makeStr(name), v);
                    break;
                }
                case Op::SetFieldKeep: {
                    // Stack convention: [table, value] (table pushed by
                    // NewTable, value pushed by compileExpr). Pops value,
                    // peeks table, leaves table on stack for next field.
                    uint16_t idx = readU16();
                    Value v = stack.back(); stack.pop_back();
                    Value& obj = stack.back();
                    if (!obj.isTable()) {
                        throw std::runtime_error("attempt to index a " + obj.typeName() + " value");
                    }
                    const std::string& name = *proto->constants[idx].strVal;
                    obj.tblVal->set(Value::makeStr(name), v);
                    break;
                }
                case Op::GetIndex: {
                    Value key = stack.back(); stack.pop_back();
                    Value obj = stack.back(); stack.pop_back();
                    if (obj.isTable()) {
                        stack.push_back(obj.tblVal->get(key));
                    } else if (obj.isString()) {
                        // string indexing: s[n] returns the n-th char (1-based)
                        if (key.isNumber()) {
                            int n = (int)key.numVal;
                            if (n >= 1 && n <= (int)obj.strVal->size()) {
                                stack.push_back(Value::makeStr(std::string(1, (*obj.strVal)[n - 1])));
                            } else {
                                stack.push_back(Value::makeNil());
                            }
                        } else {
                            stack.push_back(Value::makeNil());
                        }
                    } else {
                        throw std::runtime_error("attempt to index a " + obj.typeName() + " value");
                    }
                    break;
                }
                case Op::SetIndex: {
                    // Stack convention: [value, key, table] (value pushed
                    // first by compileAssign, then key + table by target).
                    Value obj = stack.back(); stack.pop_back();
                    Value key = stack.back(); stack.pop_back();
                    Value v = stack.back(); stack.pop_back();
                    if (!obj.isTable()) {
                        throw std::runtime_error("attempt to index a " + obj.typeName() + " value");
                    }
                    obj.tblVal->set(key, v);
                    break;
                }
                case Op::SetIndexKeep: {
                    // Stack convention: [table, key, value] (table pushed by
                    // NewTable, then key + value). Pops val+key, peeks table.
                    Value v = stack.back(); stack.pop_back();
                    Value key = stack.back(); stack.pop_back();
                    Value& obj = stack.back();
                    if (!obj.isTable()) {
                        throw std::runtime_error("attempt to index a " + obj.typeName() + " value");
                    }
                    obj.tblVal->set(key, v);
                    break;
                }
                case Op::NewTable:
                    stack.push_back(Value::makeTable());
                    break;
                case Op::AppendTable: {
                    Value v = stack.back(); stack.pop_back();
                    Value t = stack.back();
                    if (!t.isTable()) throw std::runtime_error("append to non-table");
                    int n = t.tblVal->length();
                    t.tblVal->set(Value::makeNum(n + 1), v);
                    break;
                }
                case Op::Add: case Op::Sub: case Op::Mul: case Op::Div:
                case Op::Mod: case Op::Pow: {
                    Value b = stack.back(); stack.pop_back();
                    Value a = stack.back(); stack.pop_back();
                    if (!a.isNumber() || !b.isNumber()) {
                        throw std::runtime_error("arithmetic on non-number");
                    }
                    double r = 0;
                    switch (op) {
                        case Op::Add: r = a.numVal + b.numVal; break;
                        case Op::Sub: r = a.numVal - b.numVal; break;
                        case Op::Mul: r = a.numVal * b.numVal; break;
                        case Op::Div: r = a.numVal / b.numVal; break;
                        case Op::Mod: r = std::fmod(a.numVal, b.numVal); break;
                        case Op::Pow: r = std::pow(a.numVal, b.numVal); break;
                        default: break;
                    }
                    stack.push_back(Value::makeNum(r));
                    break;
                }
                case Op::Neg: {
                    Value a = stack.back(); stack.pop_back();
                    if (!a.isNumber()) throw std::runtime_error("negation of non-number");
                    stack.push_back(Value::makeNum(-a.numVal));
                    break;
                }
                case Op::Not: {
                    Value a = stack.back(); stack.pop_back();
                    stack.push_back(Value::makeBool(!a.truthy()));
                    break;
                }
                case Op::Len: {
                    Value a = stack.back(); stack.pop_back();
                    if (a.isString()) {
                        stack.push_back(Value::makeNum((double)a.strVal->size()));
                    } else if (a.isTable()) {
                        stack.push_back(Value::makeNum((double)a.tblVal->length()));
                    } else {
                        throw std::runtime_error("attempt to get length of a " + a.typeName());
                    }
                    break;
                }
                case Op::Concat: {
                    Value b = stack.back(); stack.pop_back();
                    Value a = stack.back(); stack.pop_back();
                    if ((!a.isString() && !a.isNumber()) || (!b.isString() && !b.isNumber())) {
                        throw std::runtime_error("attempt to concatenate a non-string/number");
                    }
                    stack.push_back(Value::makeStr(a.toString() + b.toString()));
                    break;
                }
                case Op::Eq: {
                    Value b = stack.back(); stack.pop_back();
                    Value a = stack.back(); stack.pop_back();
                    stack.push_back(Value::makeBool(a.equals(b)));
                    break;
                }
                case Op::Ne: {
                    Value b = stack.back(); stack.pop_back();
                    Value a = stack.back(); stack.pop_back();
                    stack.push_back(Value::makeBool(!a.equals(b)));
                    break;
                }
                case Op::Lt: {
                    Value b = stack.back(); stack.pop_back();
                    Value a = stack.back(); stack.pop_back();
                    stack.push_back(Value::makeBool(a.lessThan(b)));
                    break;
                }
                case Op::Gt: {
                    Value b = stack.back(); stack.pop_back();
                    Value a = stack.back(); stack.pop_back();
                    stack.push_back(Value::makeBool(b.lessThan(a)));
                    break;
                }
                case Op::Le: {
                    Value b = stack.back(); stack.pop_back();
                    Value a = stack.back(); stack.pop_back();
                    stack.push_back(Value::makeBool(a.lessEqual(b)));
                    break;
                }
                case Op::Ge: {
                    Value b = stack.back(); stack.pop_back();
                    Value a = stack.back(); stack.pop_back();
                    stack.push_back(Value::makeBool(b.lessEqual(a)));
                    break;
                }
                case Op::Jump: {
                    int16_t off = readI16();
                    ip = (size_t)((int)ip + off);
                    break;
                }
                case Op::JumpIfFalse: {
                    int16_t off = readI16();
                    Value v = stack.back(); stack.pop_back();
                    if (!v.truthy()) ip = (size_t)((int)ip + off);
                    break;
                }
                case Op::JumpIfTrue: {
                    int16_t off = readI16();
                    Value v = stack.back(); stack.pop_back();
                    if (v.truthy()) ip = (size_t)((int)ip + off);
                    break;
                }
                case Op::JumpIfFalseKeep: {
                    int16_t off = readI16();
                    Value v = stack.back();
                    if (!v.truthy()) ip = (size_t)((int)ip + off);
                    break;
                }
                case Op::JumpIfTrueKeep: {
                    int16_t off = readI16();
                    Value v = stack.back();
                    if (v.truthy()) ip = (size_t)((int)ip + off);
                    break;
                }
                case Op::Pop: {
                    uint8_t n = readU8();
                    for (uint8_t i = 0; i < n; i++) stack.pop_back();
                    break;
                }
                case Op::Dup: {
                    stack.push_back(stack.back());
                    break;
                }
                case Op::Call: {
                    uint8_t nargs = readU8();
                    uint8_t nresults = readU8();
                    // The function is at stack.size() - nargs - 1.
                    size_t fnIdx = stack.size() - nargs - 1;
                    std::shared_ptr<Function> callee = stack[fnIdx].fnVal;
                    if (!stack[fnIdx].isFunction() || !callee) {
                        throw std::runtime_error("attempt to call a " + stack[fnIdx].typeName() + " value");
                    }
                    std::vector<Value> callArgs(stack.begin() + fnIdx + 1, stack.end());
                    stack.erase(stack.begin() + fnIdx, stack.end());
                    std::vector<Value> results = call(callee, std::move(callArgs), globals);
                    if (nresults == 0xFF) {
                        for (auto& r : results) stack.push_back(std::move(r));
                    } else {
                        for (uint8_t i = 0; i < nresults; i++) {
                            if (i < results.size()) stack.push_back(std::move(results[i]));
                            else stack.push_back(Value::makeNil());
                        }
                    }
                    break;
                }
                case Op::CallTail: {
                    uint8_t nresults = readU8();
                    (void)nresults;
                    throw std::runtime_error("CallTail not implemented");
                }
                case Op::Closure: {
                    uint16_t idx = readU16();
                    auto childProto = proto->protos[idx];
                    auto newFn = std::make_shared<Function>();
                    newFn->kind = Function::Script;
                    newFn->proto = childProto;
                    newFn->name = childProto->name;
                    // Fill upvalues from current frame.
                    for (const auto& uv : childProto->upvalues) {
                        if (uv.first) {
                            newFn->upvalues.push_back(locals[uv.second]);
                        } else {
                            newFn->upvalues.push_back(fn->upvalues[uv.second]);
                        }
                    }
                    stack.push_back(Value::makeFunc(newFn));
                    break;
                }
                case Op::LoadVararg: {
                    uint8_t nres = readU8();
                    if (nres == 0xFF) {
                        // Push all varargs as a table.
                        Value t = Value::makeTable();
                        for (size_t i = 0; i < varargs.size(); i++) {
                            t.tblVal->set(Value::makeNum(i + 1), varargs[i]);
                        }
                        stack.push_back(t);
                    } else {
                        for (uint8_t i = 0; i < nres; i++) {
                            stack.push_back(i < varargs.size() ? varargs[i] : Value::makeNil());
                        }
                    }
                    break;
                }
                case Op::Return: {
                    uint8_t nret = readU8();
                    std::vector<Value> rets;
                    rets.reserve(nret);
                    for (uint8_t i = 0; i < nret; i++) {
                        rets.push_back(stack.back());
                        stack.pop_back();
                    }
                    std::reverse(rets.begin(), rets.end());
                    return rets;
                }
                default:
                    throw std::runtime_error("unknown opcode " + std::to_string((int)op));
            }
        }
    } catch (std::exception& e) {
        // Wrap with function name for better error messages.
        throw std::runtime_error("in " + proto->name + ": " + e.what());
    }
    return {Value::makeNil()};
}

std::vector<Value> VM::evalSource(const char* src, const std::string& name,
                                  std::vector<Value> args,
                                  std::map<std::string, Value>* globals) {
    Lexer lex(src, name);
    auto toks = lex.tokenize();
    Parser p(std::move(toks), name);
    auto ast = p.parseChunk();
    Compiler c;
    auto proto = c.compile(ast);
    auto fn = std::make_shared<Function>();
    fn->kind = Function::Script;
    fn->proto = proto;
    fn->name = name;
    return call(fn, std::move(args), globals);
}

// ===========================================================================
// Standard library: print, type, tostring, tonumber, pairs, ipairs,
// math, table, string
// ===========================================================================

namespace {

Value makeNative(NativeFn fn, const std::string& name) {
    auto f = std::make_shared<Function>();
    f->kind = Function::Native;
    f->native = std::move(fn);
    f->name = name;
    return Value::makeFunc(f);
}

std::vector<Value> fn_print(std::vector<Value>& args) {
    std::string s;
    for (size_t i = 0; i < args.size(); i++) {
        if (i > 0) s += "\t";
        s += args[i].toString();
    }
    TD_LOG_INFO("[script] %s", s.c_str());
    return {};
}

std::vector<Value> fn_type(std::vector<Value>& args) {
    if (args.empty()) return {Value::makeStr("nil")};
    return {Value::makeStr(args[0].typeName())};
}

std::vector<Value> fn_tostring(std::vector<Value>& args) {
    if (args.empty()) return {Value::makeStr("nil")};
    return {Value::makeStr(args[0].toString())};
}

std::vector<Value> fn_tonumber(std::vector<Value>& args) {
    if (args.empty()) return {Value::makeNil()};
    if (args[0].isNumber()) return {args[0]};
    if (args[0].isString()) {
        char* end = nullptr;
        double d = std::strtod(args[0].strVal->c_str(), &end);
        if (end == args[0].strVal->c_str()) return {Value::makeNil()};
        return {Value::makeNum(d)};
    }
    return {Value::makeNil()};
}

// pairs(t) -> (iterator, t, nil). The iterator takes (t, prev_key) and
// returns (next_key, next_value) or nil.
std::vector<Value> fn_pairs(std::vector<Value>& args) {
    if (args.empty() || !args[0].isTable()) {
        throw std::runtime_error("pairs: expected table");
    }
    static auto iter = std::make_shared<Function>();
    static bool init = false;
    if (!init) {
        iter->kind = Function::Native;
        iter->name = "pairs_iter";
        iter->native = [](std::vector<Value>& a) -> std::vector<Value> {
            if (a.size() < 2 || !a[0].isTable()) return {Value::makeNil()};
            auto& map = a[0].tblVal->map;
            if (a[1].isNil()) {
                if (map.empty()) return {Value::makeNil()};
                auto it = map.begin();
                return {it->first, it->second};
            }
            auto it = map.find(a[1]);
            if (it == map.end() || ++it == map.end()) return {Value::makeNil()};
            return {it->first, it->second};
        };
        init = true;
    }
    return {Value::makeFunc(iter), args[0], Value::makeNil()};
}

std::vector<Value> fn_ipairs(std::vector<Value>& args) {
    if (args.empty() || !args[0].isTable()) {
        throw std::runtime_error("ipairs: expected table");
    }
    static auto iter = std::make_shared<Function>();
    static bool init = false;
    if (!init) {
        iter->kind = Function::Native;
        iter->name = "ipairs_iter";
        iter->native = [](std::vector<Value>& a) -> std::vector<Value> {
            if (a.size() < 2 || !a[0].isTable()) return {Value::makeNil()};
            int i = (int)(a[1].isNumber() ? a[1].numVal : 0) + 1;
            Value k = Value::makeNum(i);
            Value v = a[0].tblVal->get(k);
            if (v.isNil()) return {Value::makeNil()};
            return {k, v};
        };
        init = true;
    }
    return {Value::makeFunc(iter), args[0], Value::makeNum(0)};
}

std::vector<Value> fn_select(std::vector<Value>& args) {
    if (args.empty()) return {};
    if (args[0].isString() && *args[0].strVal == "#") {
        return {Value::makeNum((double)(args.size() - 1))};
    }
    if (args[0].isNumber()) {
        int n = (int)args[0].numVal;
        if (n < 1) throw std::runtime_error("select: index out of range");
        std::vector<Value> out;
        for (size_t i = (size_t)n; i < args.size(); i++) out.push_back(args[i]);
        return out;
    }
    return {};
}

std::vector<Value> fn_error(std::vector<Value>& args) {
    std::string msg = args.empty() ? "<no message>" : args[0].toString();
    throw std::runtime_error("script error: " + msg);
}

std::vector<Value> fn_assert(std::vector<Value>& args) {
    if (args.empty() || !args[0].truthy()) {
        std::string msg = args.size() >= 2 ? args[1].toString() : "assertion failed!";
        throw std::runtime_error(msg);
    }
    return args;
}

void registerMathLib(std::map<std::string, Value>& g) {
    Value m = Value::makeTable();
    auto addFn = [&](const char* name, NativeFn fn) {
        m.tblVal->set(Value::makeStr(name), makeNative(std::move(fn), std::string("math.") + name));
    };
    addFn("floor", [](std::vector<Value>& a) -> std::vector<Value> {
        return a.empty() ? std::vector<Value>{Value::makeNil()} : std::vector<Value>{Value::makeNum(std::floor(a[0].numVal))};
    });
    addFn("ceil", [](std::vector<Value>& a) -> std::vector<Value> {
        return a.empty() ? std::vector<Value>{Value::makeNil()} : std::vector<Value>{Value::makeNum(std::ceil(a[0].numVal))};
    });
    addFn("abs", [](std::vector<Value>& a) -> std::vector<Value> {
        return a.empty() ? std::vector<Value>{Value::makeNil()} : std::vector<Value>{Value::makeNum(std::abs(a[0].numVal))};
    });
    addFn("sqrt", [](std::vector<Value>& a) -> std::vector<Value> {
        return a.empty() ? std::vector<Value>{Value::makeNil()} : std::vector<Value>{Value::makeNum(std::sqrt(a[0].numVal))};
    });
    addFn("sin", [](std::vector<Value>& a) -> std::vector<Value> {
        return a.empty() ? std::vector<Value>{Value::makeNil()} : std::vector<Value>{Value::makeNum(std::sin(a[0].numVal))};
    });
    addFn("cos", [](std::vector<Value>& a) -> std::vector<Value> {
        return a.empty() ? std::vector<Value>{Value::makeNil()} : std::vector<Value>{Value::makeNum(std::cos(a[0].numVal))};
    });
    addFn("tan", [](std::vector<Value>& a) -> std::vector<Value> {
        return a.empty() ? std::vector<Value>{Value::makeNil()} : std::vector<Value>{Value::makeNum(std::tan(a[0].numVal))};
    });
    addFn("atan", [](std::vector<Value>& a) -> std::vector<Value> {
        return a.empty() ? std::vector<Value>{Value::makeNil()} : std::vector<Value>{Value::makeNum(std::atan(a[0].numVal))};
    });
    addFn("atan2", [](std::vector<Value>& a) -> std::vector<Value> {
        if (a.size() < 2) return {Value::makeNil()};
        return {Value::makeNum(std::atan2(a[0].numVal, a[1].numVal))};
    });
    addFn("exp", [](std::vector<Value>& a) -> std::vector<Value> {
        return a.empty() ? std::vector<Value>{Value::makeNil()} : std::vector<Value>{Value::makeNum(std::exp(a[0].numVal))};
    });
    addFn("log", [](std::vector<Value>& a) -> std::vector<Value> {
        return a.empty() ? std::vector<Value>{Value::makeNil()} : std::vector<Value>{Value::makeNum(std::log(a[0].numVal))};
    });
    addFn("pow", [](std::vector<Value>& a) -> std::vector<Value> {
        if (a.size() < 2) return {Value::makeNil()};
        return {Value::makeNum(std::pow(a[0].numVal, a[1].numVal))};
    });
    addFn("max", [](std::vector<Value>& a) -> std::vector<Value> {
        if (a.empty()) return {Value::makeNil()};
        double m = a[0].numVal;
        for (size_t i = 1; i < a.size(); i++) m = std::max(m, a[i].numVal);
        return {Value::makeNum(m)};
    });
    addFn("min", [](std::vector<Value>& a) -> std::vector<Value> {
        if (a.empty()) return {Value::makeNil()};
        double m = a[0].numVal;
        for (size_t i = 1; i < a.size(); i++) m = std::min(m, a[i].numVal);
        return {Value::makeNum(m)};
    });
    addFn("random", [](std::vector<Value>& a) -> std::vector<Value> {
        if (a.empty()) {
            double r = (double)std::rand() / (RAND_MAX + 1.0);
            return {Value::makeNum(r)};
        }
        if (a.size() == 1) {
            int hi = (int)a[0].numVal;
            return {Value::makeNum((double)(std::rand() % hi + 1))};
        }
        int lo = (int)a[0].numVal, hi = (int)a[1].numVal;
        return {Value::makeNum((double)(lo + (std::rand() % (hi - lo + 1))))};
    });
    m.tblVal->set(Value::makeStr("pi"), Value::makeNum(3.14159265358979323846));
    m.tblVal->set(Value::makeStr("huge"), Value::makeNum(std::numeric_limits<double>::infinity()));
    g["math"] = m;
}

void registerTableLib(std::map<std::string, Value>& g) {
    Value t = Value::makeTable();
    auto addFn = [&](const char* name, NativeFn fn) {
        t.tblVal->set(Value::makeStr(name), makeNative(std::move(fn), std::string("table.") + name));
    };
    addFn("insert", [](std::vector<Value>& a) -> std::vector<Value> {
        if (a.empty() || !a[0].isTable()) return {};
        if (a.size() == 2) {
            int n = a[0].tblVal->length();
            a[0].tblVal->set(Value::makeNum(n + 1), a[1]);
        } else if (a.size() >= 3) {
            int pos = (int)a[1].numVal;
            int n = a[0].tblVal->length();
            for (int i = n; i >= pos; i--) {
                a[0].tblVal->set(Value::makeNum(i + 1), a[0].tblVal->get(Value::makeNum(i)));
            }
            a[0].tblVal->set(Value::makeNum(pos), a[2]);
        }
        return {};
    });
    addFn("remove", [](std::vector<Value>& a) -> std::vector<Value> {
        if (a.empty() || !a[0].isTable()) return {Value::makeNil()};
        int n = a[0].tblVal->length();
        int pos = a.size() >= 2 ? (int)a[1].numVal : n;
        if (pos < 1 || pos > n) return {Value::makeNil()};
        Value removed = a[0].tblVal->get(Value::makeNum(pos));
        for (int i = pos; i < n; i++) {
            a[0].tblVal->set(Value::makeNum(i), a[0].tblVal->get(Value::makeNum(i + 1)));
        }
        a[0].tblVal->set(Value::makeNum(n), Value::makeNil());
        return {removed};
    });
    addFn("concat", [](std::vector<Value>& a) -> std::vector<Value> {
        if (a.empty() || !a[0].isTable()) return {Value::makeStr("")};
        std::string sep = a.size() >= 2 ? *a[1].strVal : "";
        int i = a.size() >= 3 ? (int)a[2].numVal : 1;
        int j = a.size() >= 4 ? (int)a[3].numVal : a[0].tblVal->length();
        std::string out;
        bool first = true;
        for (int k = i; k <= j; k++) {
            Value v = a[0].tblVal->get(Value::makeNum(k));
            if (!first) out += sep;
            out += v.toString();
            first = false;
        }
        return {Value::makeStr(out)};
    });
    addFn("getn", [](std::vector<Value>& a) -> std::vector<Value> {
        if (a.empty() || !a[0].isTable()) return {Value::makeNum(0)};
        return {Value::makeNum((double)a[0].tblVal->length())};
    });
    g["table"] = t;
}

void registerStringLib(std::map<std::string, Value>& g) {
    Value s = Value::makeTable();
    auto addFn = [&](const char* name, NativeFn fn) {
        s.tblVal->set(Value::makeStr(name), makeNative(std::move(fn), std::string("string.") + name));
    };
    addFn("len", [](std::vector<Value>& a) -> std::vector<Value> {
        if (a.empty() || !a[0].isString()) return {Value::makeNum(0)};
        return {Value::makeNum((double)a[0].strVal->size())};
    });
    addFn("sub", [](std::vector<Value>& a) -> std::vector<Value> {
        if (a.empty() || !a[0].isString()) return {Value::makeStr("")};
        const std::string& str = *a[0].strVal;
        int len = (int)str.size();
        int from = a.size() >= 2 ? (int)a[1].numVal : 1;
        int to   = a.size() >= 3 ? (int)a[2].numVal : len;
        if (from < 0) from = len + from + 1;
        if (to < 0)   to = len + to + 1;
        if (from < 1) from = 1;
        if (to > len) to = len;
        if (from > to) return {Value::makeStr("")};
        return {Value::makeStr(str.substr(from - 1, to - from + 1))};
    });
    addFn("upper", [](std::vector<Value>& a) -> std::vector<Value> {
        if (a.empty() || !a[0].isString()) return {Value::makeStr("")};
        std::string s = *a[0].strVal;
        std::transform(s.begin(), s.end(), s.begin(), ::toupper);
        return {Value::makeStr(s)};
    });
    addFn("lower", [](std::vector<Value>& a) -> std::vector<Value> {
        if (a.empty() || !a[0].isString()) return {Value::makeStr("")};
        std::string s = *a[0].strVal;
        std::transform(s.begin(), s.end(), s.begin(), ::tolower);
        return {Value::makeStr(s)};
    });
    addFn("rep", [](std::vector<Value>& a) -> std::vector<Value> {
        if (a.empty() || !a[0].isString()) return {Value::makeStr("")};
        int n = a.size() >= 2 ? (int)a[1].numVal : 0;
        std::string out;
        for (int i = 0; i < n; i++) out += *a[0].strVal;
        return {Value::makeStr(out)};
    });
    addFn("reverse", [](std::vector<Value>& a) -> std::vector<Value> {
        if (a.empty() || !a[0].isString()) return {Value::makeStr("")};
        std::string s = *a[0].strVal;
        std::reverse(s.begin(), s.end());
        return {Value::makeStr(s)};
    });
    addFn("find", [](std::vector<Value>& a) -> std::vector<Value> {
        // Plain substring search only (no Lua patterns).
        if (a.size() < 2 || !a[0].isString() || !a[1].isString()) return {Value::makeNil()};
        const std::string& hay = *a[0].strVal;
        const std::string& needle = *a[1].strVal;
        int start = a.size() >= 3 ? (int)a[2].numVal : 1;
        if (start < 1) start = 1;
        size_t pos = hay.find(needle, start - 1);
        if (pos == std::string::npos) return {Value::makeNil()};
        return {Value::makeNum((double)(pos + 1)), Value::makeNum((double)(pos + needle.size()))};
    });
    addFn("format", [](std::vector<Value>& a) -> std::vector<Value> {
        if (a.empty() || !a[0].isString()) return {Value::makeStr("")};
        // Simple printf-style: %s, %d, %f, %x, %%
        std::string out;
        const std::string& fmt = *a[0].strVal;
        size_t argIdx = 1;
        for (size_t i = 0; i < fmt.size(); i++) {
            if (fmt[i] != '%') { out += fmt[i]; continue; }
            i++;
            if (i >= fmt.size()) { out += '%'; break; }
            char c = fmt[i];
            if (c == '%') { out += '%'; continue; }
            if (argIdx >= a.size()) { out += "<missing>"; continue; }
            Value v = a[argIdx++];
            switch (c) {
                case 's': out += v.toString(); break;
                case 'd': out += std::to_string((long long)v.numVal); break;
                case 'f': {
                    char b[32]; std::snprintf(b, sizeof(b), "%f", v.numVal); out += b;
                    break;
                }
                case 'x': {
                    char b[32]; std::snprintf(b, sizeof(b), "%x", (unsigned int)(long long)v.numVal); out += b;
                    break;
                }
                case 'g': {
                    char b[32]; std::snprintf(b, sizeof(b), "%g", v.numVal); out += b;
                    break;
                }
                default: out += '%'; out += c; break;
            }
        }
        return {Value::makeStr(out)};
    });
    g["string"] = s;
}

} // namespace

void registerStdlib(std::map<std::string, Value>& g) {
    g["print"]    = makeNative(fn_print, "print");
    g["type"]     = makeNative(fn_type, "type");
    g["tostring"] = makeNative(fn_tostring, "tostring");
    g["tonumber"] = makeNative(fn_tonumber, "tonumber");
    g["pairs"]    = makeNative(fn_pairs, "pairs");
    g["ipairs"]   = makeNative(fn_ipairs, "ipairs");
    g["select"]   = makeNative(fn_select, "select");
    g["error"]    = makeNative(fn_error, "error");
    g["assert"]   = makeNative(fn_assert, "assert");
    registerMathLib(g);
    registerTableLib(g);
    registerStringLib(g);
}

// ===========================================================================
// td.* library
// ===========================================================================

namespace {

std::vector<Value> td_create_entity(std::vector<Value>& a) {
    World* w = deps().world;
    if (!w) return {Value::makeNum((double)INVALID_ENTITY)};
    const char* name = (a.size() >= 1 && a[0].isString()) ? a[0].strVal->c_str() : "ScriptEntity";
    EntityId id = w->createEntity(name);
    return {Value::makeNum((double)id)};
}

std::vector<Value> td_destroy_entity(std::vector<Value>& a) {
    World* w = deps().world;
    if (!w || a.empty() || !a[0].isNumber()) return {};
    w->destroyEntity((EntityId)a[0].numVal);
    return {};
}

std::vector<Value> td_set_position(std::vector<Value>& a) {
    World* w = deps().world;
    if (!w || a.size() < 3) return {};
    EntityId id = (EntityId)a[0].numVal;
    auto* p = w->getComponent<PositionComponent>(id);
    if (!p) p = w->addComponent<PositionComponent>(id);
    if (p) { p->x = (float)a[1].numVal; p->y = (float)a[2].numVal; }
    return {};
}

std::vector<Value> td_get_position(std::vector<Value>& a) {
    World* w = deps().world;
    if (!w || a.empty()) return {Value::makeNil(), Value::makeNil()};
    EntityId id = (EntityId)a[0].numVal;
    auto* p = w->getComponent<PositionComponent>(id);
    if (!p) return {Value::makeNil(), Value::makeNil()};
    return {Value::makeNum(p->x), Value::makeNum(p->y)};
}

std::vector<Value> td_set_velocity(std::vector<Value>& a) {
    World* w = deps().world;
    if (!w || a.size() < 3) return {};
    EntityId id = (EntityId)a[0].numVal;
    auto* v = w->getComponent<VelocityComponent>(id);
    if (!v) v = w->addComponent<VelocityComponent>(id);
    if (v) { v->vx = (float)a[1].numVal; v->vy = (float)a[2].numVal; }
    return {};
}

std::vector<Value> td_set_sprite(std::vector<Value>& a) {
    World* w = deps().world;
    if (!w || a.size() < 7) return {};
    EntityId id = (EntityId)a[0].numVal;
    auto* s = w->getComponent<SpriteComponent>(id);
    if (!s) s = w->addComponent<SpriteComponent>(id);
    if (s) {
        s->width  = (float)a[1].numVal;
        s->height = (float)a[2].numVal;
        s->r = (float)a[3].numVal;
        s->g = (float)a[4].numVal;
        s->b = (float)a[5].numVal;
        s->a = (float)a[6].numVal;
    }
    return {};
}

std::vector<Value> td_set_collider(std::vector<Value>& a) {
    World* w = deps().world;
    if (!w || a.size() < 3) return {};
    EntityId id = (EntityId)a[0].numVal;
    auto* c = w->getComponent<ColliderComponent>(id);
    if (!c) c = w->addComponent<ColliderComponent>(id);
    if (c) { c->width = (float)a[1].numVal; c->height = (float)a[2].numVal; }
    return {};
}

std::vector<Value> td_is_key_down(std::vector<Value>& a) {
    InputState* in = deps().input;
    if (!in || a.empty()) return {Value::makeBool(false)};
    int key = (int)a[0].numVal;
    return {Value::makeBool(in->keyDown(key))};
}

std::vector<Value> td_is_mouse_down(std::vector<Value>& a) {
    InputState* in = deps().input;
    if (!in || a.empty()) return {Value::makeBool(false)};
    int btn = (int)a[0].numVal;
    return {Value::makeBool(in->mouseDown(btn))};
}

std::vector<Value> td_get_mouse_pos(std::vector<Value>&) {
    InputState* in = deps().input;
    if (!in) return {Value::makeNum(0), Value::makeNum(0)};
    return {Value::makeNum(in->mouseX), Value::makeNum(in->mouseY)};
}

std::vector<Value> td_get_delta_time(std::vector<Value>&) {
    TimeState* t = deps().time;
    return {Value::makeNum(t ? (double)t->deltaTime : 0.0)};
}

std::vector<Value> td_get_time(std::vector<Value>&) {
    TimeState* t = deps().time;
    return {Value::makeNum(t ? t->totalTime : 0.0)};
}

std::vector<Value> td_log(std::vector<Value>& a) {
    std::string s;
    for (size_t i = 0; i < a.size(); i++) {
        if (i > 0) s += "\t";
        s += a[i].toString();
    }
    TD_LOG_INFO("[script] %s", s.c_str());
    return {};
}

std::vector<Value> td_find_by_name(std::vector<Value>& a) {
    World* w = deps().world;
    if (!w || a.empty() || !a[0].isString()) return {Value::makeNil()};
    EntityId id = w->findEntityByName(a[0].strVal->c_str());
    if (id == INVALID_ENTITY) return {Value::makeNil()};
    return {Value::makeNum((double)id)};
}

std::vector<Value> td_find_by_tag(std::vector<Value>& a) {
    World* w = deps().world;
    Value out = Value::makeTable();
    if (!w || a.empty() || !a[0].isString()) return {out};
    EntityId ids[256];
    int n = w->findEntitiesByTag(a[0].strVal->c_str(), ids, 256);
    for (int i = 0; i < n; i++) {
        out.tblVal->set(Value::makeNum(i + 1), Value::makeNum((double)ids[i]));
    }
    return {out};
}

std::vector<Value> td_connect(std::vector<Value>& a) {
    if (a.size() < 2) return {Value::makeNum(-1)};
    if (!a[0].isString()) return {Value::makeNum(-1)};
    std::string signalName = *a[0].strVal;
    ScriptEnv* env = g_currentEnv;
    if (!env) return {Value::makeNum(-1)};

    // The callback can be either a function (snapshot) or a string (looked
    // up by name at dispatch time — hot-reload-safe).
    std::shared_ptr<Function> fnSnapshot;
    std::string fnName;
    if (a[1].isFunction()) {
        fnSnapshot = a[1].fnVal;
    } else if (a[1].isString()) {
        fnName = *a[1].strVal;
    } else {
        return {Value::makeNum(-1)};
    }

    int envId = env->id;
    auto handle = SignalBus::get().on(signalName.c_str(),
        [envId, fnSnapshot, fnName](const SignalPayload& p) {
            (void)p;
            // Look up the env in the VM registry (forward-declared at the
            // top of td::script — survives hot reload).
            ScriptEnv* env2 = lookupScriptEnv(envId);
            if (!env2) return;
            ScriptEnv* savedEnv = g_currentEnv;
            g_currentEnv = env2;
            std::shared_ptr<Function> fn = fnSnapshot;
            if (!fn && !fnName.empty()) {
                auto it = env2->globals.find(fnName);
                if (it != env2->globals.end() && it->second.isFunction()) {
                    fn = it->second.fnVal;
                }
            }
            if (fn) {
                try {
                    static VM vm;
                    std::vector<Value> args = g_emitArgs;
                    vm.call(fn, std::move(args), &env2->globals);
                } catch (std::exception& e) {
                    TD_LOG_WARN("[script] signal callback error: %s", e.what());
                }
            }
            g_currentEnv = savedEnv;
        });
    env->subscriptions.push_back(handle);
    return {Value::makeNum((double)((env->subscriptions.size() - 1) | (handle.eventIdx << 16)))};
}

std::vector<Value> td_emit(std::vector<Value>& a) {
    if (a.empty() || !a[0].isString()) return {};
    std::string name = *a[0].strVal;
    auto saved = g_emitArgs;
    g_emitArgs.assign(a.begin() + 1, a.end());
    SignalPayload p;
    // Pack a few numeric args into the payload too (best-effort).
    if (g_emitArgs.size() >= 1 && g_emitArgs[0].isNumber()) p.intValue = (int)g_emitArgs[0].numVal;
    if (g_emitArgs.size() >= 1 && g_emitArgs[0].isNumber()) p.f[0] = (float)g_emitArgs[0].numVal;
    if (g_emitArgs.size() >= 2 && g_emitArgs[1].isNumber()) p.f[1] = (float)g_emitArgs[1].numVal;
    if (g_emitArgs.size() >= 3 && g_emitArgs[2].isNumber()) p.f[2] = (float)g_emitArgs[2].numVal;
    if (g_emitArgs.size() >= 4 && g_emitArgs[3].isNumber()) p.f[3] = (float)g_emitArgs[3].numVal;
    SignalBus::get().emit(name.c_str(), p);
    g_emitArgs = saved;
    return {};
}

std::vector<Value> td_beat_start(std::vector<Value>& a) {
    auto& b = beatState();
    b.bpm = a.empty() ? 120.0f : (float)a[0].numVal;
    b.spb = 60.0f / b.bpm;
    double now = deps().time ? deps().time->totalTime : nowSeconds();
    b.startTime = now;
    b.lastBeatTime = now;
    b.nextBeatTime = now + b.spb;
    b.beatCount = 0;
    b.combo = 0;
    b.bestCombo = 0;
    b.active = true;
    return {};
}

std::vector<Value> td_beat_stop(std::vector<Value>&) {
    beatState().active = false;
    return {};
}

std::vector<Value> td_beat_is_on_beat(std::vector<Value>&) {
    auto& b = beatState();
    if (!b.active) return {Value::makeBool(false)};
    double now = deps().time ? deps().time->totalTime : nowSeconds();
    double distFromLast = std::fabs(now - b.lastBeatTime);
    double distFromNext = std::fabs(now - b.nextBeatTime);
    return {Value::makeBool(distFromLast <= b.windowHalf || distFromNext <= b.windowHalf)};
}

std::vector<Value> td_beat_get_combo(std::vector<Value>&) {
    return {Value::makeNum((double)beatState().combo)};
}

std::vector<Value> td_beat_register_hit(std::vector<Value>&) {
    auto& b = beatState();
    bool onBeat = false;
    if (b.active) {
        double now = deps().time ? deps().time->totalTime : nowSeconds();
        double distFromLast = std::fabs(now - b.lastBeatTime);
        double distFromNext = std::fabs(now - b.nextBeatTime);
        onBeat = distFromLast <= b.windowHalf || distFromNext <= b.windowHalf;
    }
    if (onBeat) {
        b.combo++;
        if (b.combo > b.bestCombo) b.bestCombo = b.combo;
    } else {
        b.combo = 0;
    }
    return {Value::makeBool(onBeat)};
}

std::vector<Value> td_beat_get_count(std::vector<Value>&) {
    // Tick the beat counter on each call (cheap metronome update).
    auto& b = beatState();
    if (b.active) {
        double now = deps().time ? deps().time->totalTime : nowSeconds();
        int safety = 16;
        while (now >= b.nextBeatTime && safety-- > 0) {
            b.lastBeatTime = b.nextBeatTime;
            b.nextBeatTime += b.spb;
            b.beatCount++;
        }
    }
    return {Value::makeNum((double)b.beatCount)};
}

} // namespace

void registerTdLib(std::map<std::string, Value>& g) {
    Value td = Value::makeTable();
    auto add = [&](const char* name, NativeFn fn) {
        td.tblVal->set(Value::makeStr(name), makeNative(std::move(fn), std::string("td.") + name));
    };
    add("create_entity",      td_create_entity);
    add("destroy_entity",     td_destroy_entity);
    add("set_position",       td_set_position);
    add("get_position",       td_get_position);
    add("set_velocity",       td_set_velocity);
    add("set_sprite",         td_set_sprite);
    add("set_collider",       td_set_collider);
    add("is_key_down",        td_is_key_down);
    add("is_mouse_down",      td_is_mouse_down);
    add("get_mouse_pos",      td_get_mouse_pos);
    add("get_delta_time",     td_get_delta_time);
    add("get_time",           td_get_time);
    add("log",                td_log);
    add("find_by_name",       td_find_by_name);
    add("find_by_tag",        td_find_by_tag);
    add("connect",            td_connect);
    add("emit",               td_emit);
    add("beat_start",         td_beat_start);
    add("beat_stop",          td_beat_stop);
    add("beat_is_on_beat",    td_beat_is_on_beat);
    add("beat_get_combo",     td_beat_get_combo);
    add("beat_register_hit",  td_beat_register_hit);
    add("beat_get_count",     td_beat_get_count);
    g["td"] = td;
}

// ===========================================================================
// Script registry + internal API
// ===========================================================================

namespace {
struct ScriptRegistry {
    std::mutex mtx;
    std::map<int, ScriptEnv> envs;
    int nextId = 1;
    VM vm;
};
ScriptRegistry& registry() { static ScriptRegistry r; return r; }
}

// Implemented below — referenced from td_connect's lambda.
ScriptEnv* lookupScriptEnv(int id) {
    auto& r = registry();
    std::lock_guard<std::mutex> lk(r.mtx);
    auto it = r.envs.find(id);
    return it != r.envs.end() ? &it->second : nullptr;
}

static ScriptEnv* getEnv(ScriptHandle h) {
    if (!h.valid()) return nullptr;
    auto& r = registry();
    std::lock_guard<std::mutex> lk(r.mtx);
    auto it = r.envs.find(h.id);
    return it != r.envs.end() ? &it->second : nullptr;
}

ScriptHandle loadScriptFromSource(ScriptVM& vm, const char* src, const char* name) {
    (void)vm;
    auto& r = registry();
    int id;
    ScriptEnv* env;
    {
        std::lock_guard<std::mutex> lk(r.mtx);
        id = r.nextId++;
        env = &r.envs[id];
    }
    env->id = id;
    env->name = name ? name : "<source>";
    env->source = src;
    env->fromSource = true;
    registerStdlib(env->globals);
    registerTdLib(env->globals);

    try {
        Lexer lex(src, env->name);
        auto toks = lex.tokenize();
        Parser p(std::move(toks), env->name);
        auto ast = p.parseChunk();
        Compiler c;
        env->mainProto = c.compile(ast);
        // Run the chunk to define functions.
        auto fn = std::make_shared<Function>();
        fn->kind = Function::Script;
        fn->proto = env->mainProto;
        fn->name = env->name;
        ScriptEnv* savedEnv = g_currentEnv;
        g_currentEnv = env;
        r.vm.call(fn, {}, &env->globals);
        g_currentEnv = savedEnv;
    } catch (std::exception& e) {
        TD_LOG_ERROR("[script] load error: %s", e.what());
        std::lock_guard<std::mutex> lk(r.mtx);
        r.envs.erase(id);
        return { -1 };
    }
    return { id };
}

void setWorld(ScriptVM&, World* w)      { deps().world = w; }
void setInputState(ScriptVM&, InputState* s) { deps().input = s; }
void setTimeState(ScriptVM&, TimeState* t)   { deps().time = t; }

Value getScriptGlobal(ScriptHandle h, const char* name) {
    ScriptEnv* env = getEnv(h);
    if (!env || !name) return Value::makeNil();
    auto it = env->globals.find(name);
    return it != env->globals.end() ? it->second : Value::makeNil();
}

void setScriptGlobal(ScriptHandle h, const char* name, Value v) {
    ScriptEnv* env = getEnv(h);
    if (!env || !name) return;
    env->globals[name] = std::move(v);
}

std::vector<Value> callScriptFunction(ScriptHandle h, const char* name, std::vector<Value> args) {
    ScriptEnv* env = getEnv(h);
    if (!env || !name) return {};
    auto it = env->globals.find(name);
    if (it == env->globals.end() || !it->second.isFunction()) return {};
    auto& r = registry();
    ScriptEnv* savedEnv = g_currentEnv;
    g_currentEnv = env;
    try {
        auto results = r.vm.call(it->second.fnVal, std::move(args), &env->globals);
        g_currentEnv = savedEnv;
        return results;
    } catch (std::exception& e) {
        TD_LOG_ERROR("[script] call error in %s.%s: %s", env->name.c_str(), name, e.what());
        g_currentEnv = savedEnv;
        return {};
    }
}

bool scriptHasFunction(ScriptHandle h, const char* name) {
    ScriptEnv* env = getEnv(h);
    if (!env || !name) return false;
    auto it = env->globals.find(name);
    return it != env->globals.end() && it->second.isFunction();
}

std::vector<Value> evalSource(ScriptVM& vm, const char* src, std::vector<Value> args) {
    (void)vm;
    std::map<std::string, Value> globals;
    registerStdlib(globals);
    registerTdLib(globals);
    auto& r = registry();
    try {
        return r.vm.evalSource(src, "<eval>", std::move(args), &globals);
    } catch (std::exception& e) {
        TD_LOG_ERROR("[script] eval error: %s", e.what());
        return {};
    }
}

} // namespace (anonymous)
} // namespace script

// ===========================================================================
// ScriptVM public API (frozen header — these methods are declared in
// script_vm.h and must be implemented here without changing the header).
//
// These methods are members of td::ScriptVM (declared in td). They must be
// defined in td (or the global namespace), not in td::script. We bring
// td::script's names (including the anonymous-namespace helpers) into
// scope with `using namespace script` so the method bodies can reference
// registry(), g_currentEnv, Lexer, etc. unqualified.
// ===========================================================================
namespace td {
using namespace td::script;

bool td::ScriptVM::init() {
    if (m_initialized) return true;
    TD_LOG_INFO("ScriptVM: init (tdscript custom VM, wave1-scriptvm)");
    std::srand((unsigned)std::time(nullptr));
    m_initialized = true;
    return true;
}

void td::ScriptVM::shutdown() {
    if (!m_initialized) return;
    auto& r = registry();
    std::lock_guard<std::mutex> lk(r.mtx);
    for (auto& [id, env] : r.envs) {
        for (auto& h : env.subscriptions) {
            SignalBus::get().off(h);
        }
    }
    r.envs.clear();
    TD_LOG_INFO("ScriptVM: shutdown, unloaded %d scripts", m_loadedCount);
    m_initialized = false;
    m_loadedCount = 0;
}

td::ScriptHandle td::ScriptVM::loadScript(const char* path) {
    if (!m_initialized) {
        TD_LOG_WARN("ScriptVM::loadScript('%s') called before init()", path);
        return { -1 };
    }
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        TD_LOG_ERROR("ScriptVM: cannot open '%s'", path);
        return { -1 };
    }
    std::stringstream ss;
    ss << f.rdbuf();
    std::string source = ss.str();
    int64_t mtime = 0;
    td_stat_t st;
    if (TD_STAT(path, &st) == 0) {
        mtime = (int64_t)st.st_mtime;
    }
    ScriptHandle h = loadScriptFromSource(*this, source.c_str(), path);
    if (!h.valid()) return h;
    if (ScriptEnv* env = getEnv(h)) {
        env->path = path;
        env->fromSource = false;
        env->mtime = mtime;
    }
    m_loadedCount++;
    return h;
}

bool td::ScriptVM::reloadScript(ScriptHandle h) {
    if (!h.valid()) return false;
    ScriptEnv* env = getEnv(h);
    if (!env) return false;
    if (env->fromSource || env->path.empty()) {
        TD_LOG_WARN("ScriptVM::reloadScript(%d): no path (loaded from source)", h.id);
        return false;
    }
    td_stat_t st;
    if (TD_STAT(env->path.c_str(), &st) == 0) {
        if ((int64_t)st.st_mtime == env->mtime) return true;
        env->mtime = (int64_t)st.st_mtime;
    }
    std::ifstream f(env->path, std::ios::binary);
    if (!f) {
        TD_LOG_ERROR("ScriptVM::reloadScript: cannot re-open '%s'", env->path.c_str());
        return false;
    }
    std::stringstream ss;
    ss << f.rdbuf();
    std::string source = ss.str();
    try {
        Lexer lex(source.c_str(), env->name);
        auto toks = lex.tokenize();
        Parser p(std::move(toks), env->name);
        auto ast = p.parseChunk();
        Compiler c;
        auto proto = c.compile(ast);
        env->source = source;
        env->mainProto = proto;
        auto fn = std::make_shared<Function>();
        fn->kind = Function::Script;
        fn->proto = proto;
        fn->name = env->name;
        auto& r = registry();
        ScriptEnv* savedEnv = g_currentEnv;
        g_currentEnv = env;
        r.vm.call(fn, {}, &env->globals);
        g_currentEnv = savedEnv;
        TD_LOG_INFO("ScriptVM: reloaded '%s' (handle %d)", env->path.c_str(), h.id);
        return true;
    } catch (std::exception& e) {
        TD_LOG_ERROR("[script] reload error: %s", e.what());
        return false;
    }
}

void td::ScriptVM::unloadScript(ScriptHandle h) {
    if (!h.valid()) return;
    ScriptEnv* env = getEnv(h);
    if (!env) return;
    for (auto& sh : env->subscriptions) {
        SignalBus::get().off(sh);
    }
    auto& r = registry();
    std::lock_guard<std::mutex> lk(r.mtx);
    r.envs.erase(h.id);
    if (m_loadedCount > 0) m_loadedCount--;
}

void td::ScriptVM::updateAll(EntityId entityId, float dt) {
    auto& r = registry();
    std::vector<int> ids;
    {
        std::lock_guard<std::mutex> lk(r.mtx);
        ids.reserve(r.envs.size());
        for (auto& [id, env] : r.envs) ids.push_back(id);
    }
    for (int id : ids) {
        ScriptEnv* env;
        {
            std::lock_guard<std::mutex> lk(r.mtx);
            auto it = r.envs.find(id);
            if (it == r.envs.end()) continue;
            env = &it->second;
        }
        auto it = env->globals.find("update");
        if (it == env->globals.end() || !it->second.isFunction()) continue;
        std::vector<Value> args = {Value::makeNum((double)entityId), Value::makeNum((double)dt)};
        ScriptEnv* savedEnv = g_currentEnv;
        g_currentEnv = env;
        try {
            r.vm.call(it->second.fnVal, std::move(args), &env->globals);
        } catch (std::exception& e) {
            TD_LOG_ERROR("[script] update error in %s: %s", env->name.c_str(), e.what());
        }
        g_currentEnv = savedEnv;
    }
}

void td::ScriptVM::bindSignal(ScriptHandle h, const char* eventName) {
    if (!h.valid() || !eventName) return;
    ScriptEnv* env = getEnv(h);
    if (!env) return;
    auto handle = SignalBus::get().on(eventName, [](const SignalPayload&) {});
    env->subscriptions.push_back(handle);
}

void td::ScriptVM::startFileWatcher() {
    TD_LOG_INFO("ScriptVM: file watcher started (polling every 1s)");
    static std::thread watcher;
    static std::atomic<bool> running{false};
    if (running.exchange(true)) return;
    watcher = std::thread([]() {
        while (running.load()) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
            auto& r = registry();
            std::vector<int> ids;
            {
                std::lock_guard<std::mutex> lk(r.mtx);
                for (auto& [id, env] : r.envs) {
                    if (!env.fromSource && !env.path.empty()) ids.push_back(id);
                }
            }
            for (int id : ids) {
                td::ScriptVM::get().reloadScript({id});
            }
        }
    });
    watcher.detach();
}

void td::ScriptVM::stopFileWatcher() {
    // Watcher is detached; shutdown() clears all envs which makes it a no-op.
}

} // namespace td
