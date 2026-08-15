// =============================================================================
// TD Engine — TDScript Parser Implementation (Tier 4)
// =============================================================================
#include "parser.h"
#include <stdexcept>

namespace td::tdscript {

Parser::Parser(std::vector<Token> tokens) : m_tokens(std::move(tokens)) {}

const Token& Parser::peek(size_t off) const {
    size_t i = m_pos + off;
    static Token sentinel{TokenKind::EndOfFile, "<eof>", 0, 0};
    if (i >= m_tokens.size()) return sentinel;
    return m_tokens[i];
}

const Token& Parser::advance() {
    if (m_pos < m_tokens.size()) return m_tokens[m_pos++];
    return m_tokens.back();
}

bool Parser::atEnd() const { return peek().kind == TokenKind::EndOfFile; }

bool Parser::check(TokenKind k) const { return peek().kind == k; }

bool Parser::match(TokenKind k) {
    if (peek().kind == k) { advance(); return true; }
    return false;
}

const Token& Parser::expect(TokenKind k, const char* what) {
    if (peek().kind == k) return advance();
    emitError(std::string("expected ") + what + " but got '" + peek().text + "'", peek().line, peek().col);
    static Token dummy{TokenKind::EndOfFile, "<error>", 0, 0};
    return dummy;
}

void Parser::emitError(const std::string& msg, int line, int col) {
    Diagnostic d;
    d.severity = Diagnostic::Error;
    d.message = msg;
    d.line = line;
    d.col = col;
    m_diags.push_back(d);
}

void Parser::synchronize() {
    // Skip tokens until we reach a statement boundary or top-level decl.
    while (!atEnd()) {
        TokenKind k = peek().kind;
        if (k == TokenKind::Semicolon) { advance(); return; }
        if (k == TokenKind::RBrace) return;
        if (k == TokenKind::KwImport || k == TokenKind::KwStruct || k == TokenKind::KwClass) return;
        if (k == TokenKind::KwIf || k == TokenKind::KwFor || k == TokenKind::KwWhile ||
            k == TokenKind::KwReturn || k == TokenKind::KwBreak || k == TokenKind::KwContinue) return;
        advance();
    }
}

// -----------------------------------------------------------------------------
// Top-level
// -----------------------------------------------------------------------------
NodePtr Parser::parseModule() {
    auto mod = Node::make(NodeKind::Module);
    while (!atEnd()) {
        TokenKind k = peek().kind;
        if (k == TokenKind::KwImport) {
            mod->children.push_back(parseImport());
        } else if (k == TokenKind::KwStruct) {
            mod->children.push_back(parseStruct());
        } else if (k == TokenKind::KwClass) {
            mod->children.push_back(parseClass());
        } else if (k == TokenKind::EndOfFile) {
            break;
        } else {
            emitError(std::string("expected 'import', 'struct', or 'class' but got '") + peek().text + "'",
                      peek().line, peek().col);
            synchronize();
        }
    }
    return mod;
}

NodePtr Parser::parseImport() {
    expect(TokenKind::KwImport, "'import'");
    const Token& tok = expect(TokenKind::StringLit, "string literal after 'import'");
    expect(TokenKind::Semicolon, "';'");
    auto node = Node::make(NodeKind::ImportStmt);
    node->text = tok.text;
    node->line = tok.line;
    node->col = tok.col;
    return node;
}

NodePtr Parser::parseStruct() {
    const Token& tok = expect(TokenKind::KwStruct, "'struct'");
    const Token& name = expect(TokenKind::Ident, "struct name");
    auto node = Node::make(NodeKind::StructDecl);
    node->text = name.text;
    node->line = name.line;
    node->col = name.col;
    expect(TokenKind::LBrace, "'{'");
    while (!check(TokenKind::RBrace) && !atEnd()) {
        // struct fields: [visibility] type IDENT;
        Visibility vis = parseOptionalVisibility();
        bool isRepl = match(TokenKind::KwReplicated);
        NodePtr type = parseType();
        const Token& fieldName = expect(TokenKind::Ident, "field name");
        NodePtr field = Node::make(NodeKind::FieldDecl);
        field->text = fieldName.text;
        field->type = type;
        field->isReplicated = isRepl;
        field->visibility = vis;
        field->line = fieldName.line;
        field->col = fieldName.col;
        if (match(TokenKind::Assign)) {
            field->initExpr = parseExpression();
        }
        expect(TokenKind::Semicolon, "';'");
        node->children.push_back(field);
    }
    expect(TokenKind::RBrace, "'}'");
    return node;
}

NodePtr Parser::parseClass() {
    const Token& tok = expect(TokenKind::KwClass, "'class'");
    const Token& name = expect(TokenKind::Ident, "class name");
    auto node = Node::make(NodeKind::ClassDecl);
    node->text = name.text;
    node->line = name.line;
    node->col = name.col;
    expect(TokenKind::LBrace, "'{'");
    while (!check(TokenKind::RBrace) && !atEnd()) {
        // class members: [visibility] ['replicated'] (field | method)
        // OR @rpc decorator before a method
        RpcMode rpc = RpcMode::None;
        if (match(TokenKind::At)) {
            rpc = parseRpcDecorator();
        }
        Visibility vis = parseOptionalVisibility();
        bool isRepl = match(TokenKind::KwReplicated);

        // Now we expect: type IDENT
        NodePtr type = parseType();
        const Token& memberName = expect(TokenKind::Ident, "member name");

        if (check(TokenKind::LParen)) {
            // Method
            auto method = Node::make(NodeKind::MethodDecl);
            method->text = memberName.text;
            method->type = type;
            method->rpcMode = rpc;
            method->visibility = vis;
            method->isReplicated = false;
            method->line = memberName.line;
            method->col = memberName.col;
            method->children = parseParams();
            method->thenBranch = parseBlock();  // body
            node->children.push_back(method);
        } else {
            // Field
            auto field = Node::make(NodeKind::FieldDecl);
            field->text = memberName.text;
            field->type = type;
            field->isReplicated = isRepl;
            field->visibility = vis;
            field->rpcMode = rpc;  // shouldn't have rpc on a field, but track it
            field->line = memberName.line;
            field->col = memberName.col;
            if (match(TokenKind::Assign)) {
                field->initExpr = parseExpression();
            }
            expect(TokenKind::Semicolon, "';'");
            node->children.push_back(field);
        }
    }
    expect(TokenKind::RBrace, "'}'");
    return node;
}

RpcMode Parser::parseRpcDecorator() {
    // @rpc(reliable) | @rpc(unreliable) | @rpc
    // Caller has already consumed the '@'. Next token must be Ident "rpc".
    if (!check(TokenKind::Ident)) {
        emitError("expected 'rpc' after '@'", peek().line, peek().col);
        return RpcMode::Reliable;
    }
    const Token& rpcTok = advance();
    if (rpcTok.text != "rpc") {
        emitError("unknown decorator '@" + rpcTok.text + "' (only @rpc is supported)", rpcTok.line, rpcTok.col);
        return RpcMode::Reliable;
    }
    RpcMode mode = RpcMode::Reliable;  // default if bare @rpc
    if (match(TokenKind::LParen)) {
        if (!check(TokenKind::Ident)) {
            emitError("expected 'reliable' or 'unreliable' in @rpc(...)", peek().line, peek().col);
        } else {
            const Token& modeTok = advance();
            if (modeTok.text == "reliable") mode = RpcMode::Reliable;
            else if (modeTok.text == "unreliable") mode = RpcMode::Unreliable;
            else emitError("invalid rpc mode '" + modeTok.text + "' (expected 'reliable' or 'unreliable')",
                           modeTok.line, modeTok.col);
        }
        expect(TokenKind::RParen, "')'");
    }
    return mode;
}

Visibility Parser::parseOptionalVisibility() {
    if (match(TokenKind::KwPublic)) return Visibility::Public;
    if (match(TokenKind::KwPrivate)) return Visibility::Private;
    if (match(TokenKind::KwProtected)) return Visibility::Protected;
    return Visibility::Public;  // default
}

NodeList Parser::parseParams() {
    NodeList params;
    expect(TokenKind::LParen, "'('");
    if (!check(TokenKind::RParen)) {
        do {
            NodePtr type = parseType();
            const Token& name = expect(TokenKind::Ident, "parameter name");
            auto p = Node::make(NodeKind::ParamDecl);
            p->text = name.text;
            p->type = type;
            p->line = name.line;
            p->col = name.col;
            params.push_back(p);
        } while (match(TokenKind::Comma));
    }
    expect(TokenKind::RParen, "')'");
    return params;
}

NodePtr Parser::parseType() {
    auto t = Node::make(NodeKind::TypeRef);
    TokenKind k = peek().kind;
    switch (k) {
        case TokenKind::KwInt32:   t->text = "int32"; advance(); break;
        case TokenKind::KwUint32:  t->text = "uint32"; advance(); break;
        case TokenKind::KwInt64:   t->text = "int64"; advance(); break;
        case TokenKind::KwUint64:  t->text = "uint64"; advance(); break;
        case TokenKind::KwFloat:   t->text = "float"; advance(); break;
        case TokenKind::KwDouble:  t->text = "double"; advance(); break;
        case TokenKind::KwBool:    t->text = "bool"; advance(); break;
        case TokenKind::KwString:  t->text = "string"; advance(); break;
        case TokenKind::KwVoid:    t->text = "void"; advance(); break;
        case TokenKind::KwAuto:    t->text = "auto"; advance(); break;
        case TokenKind::Ident:     t->text = peek().text; advance(); break;
        default:
            emitError(std::string("expected a type but got '") + peek().text + "'",
                      peek().line, peek().col);
            t->text = "<error>";
            break;
    }
    // generic type: Type<...>  (e.g. List<PlayerInputState>)
    if (match(TokenKind::Lt)) {
        std::string gen = "<";
        while (!check(TokenKind::Gt) && !atEnd()) {
            NodePtr inner = parseType();
            gen += inner->text;
            if (match(TokenKind::Comma)) gen += ", ";
        }
        expect(TokenKind::Gt, "'>'");
        gen += ">";
        t->text += gen;
    }
    return t;
}

// -----------------------------------------------------------------------------
// Statements
// -----------------------------------------------------------------------------
NodePtr Parser::parseBlock() {
    expect(TokenKind::LBrace, "'{'");
    auto block = Node::make(NodeKind::Block);
    while (!check(TokenKind::RBrace) && !atEnd()) {
        block->children.push_back(parseStatement());
    }
    expect(TokenKind::RBrace, "'}'");
    return block;
}

NodePtr Parser::parseStatement() {
    TokenKind k = peek().kind;
    switch (k) {
        case TokenKind::LBrace:    return parseBlock();
        case TokenKind::KwIf:      return parseIf();
        case TokenKind::KwFor:     return parseFor();
        case TokenKind::KwWhile:   return parseWhile();
        case TokenKind::KwReturn:  return parseReturn();
        case TokenKind::KwBreak:   return parseBreak();
        case TokenKind::KwContinue: return parseContinue();
        case TokenKind::KwVar:
        case TokenKind::KwConst:
        case TokenKind::KwInt32:
        case TokenKind::KwUint32:
        case TokenKind::KwInt64:
        case TokenKind::KwUint64:
        case TokenKind::KwFloat:
        case TokenKind::KwDouble:
        case TokenKind::KwBool:
        case TokenKind::KwString:
        case TokenKind::KwVoid:
        case TokenKind::KwAuto:
            return parseVarDecl();
        default: {
            // Detect custom-type var decl: `Ident Ident [= ...]` or `Ident<...> Ident [= ...]`
            // Look ahead: if the current token is an identifier AND the token after
            // (skipping any generic args) is also an identifier, treat as var decl.
            if (k == TokenKind::Ident) {
                size_t lookahead = 1;
                if (peek(1).kind == TokenKind::Lt) {
                    // Skip the generic argument list
                    int depth = 1; lookahead = 2;
                    while (depth > 0 && peek(lookahead).kind != TokenKind::EndOfFile) {
                        if (peek(lookahead).kind == TokenKind::Lt) depth++;
                        else if (peek(lookahead).kind == TokenKind::Gt) depth--;
                        lookahead++;
                    }
                }
                if (peek(lookahead).kind == TokenKind::Ident) {
                    return parseVarDecl();
                }
            }
            return parseExprStatement();
        }
    }
}

NodePtr Parser::parseVarDecl() {
    auto stmt = Node::make(NodeKind::VarDeclStmt);
    bool isConst = match(TokenKind::KwConst);
    bool isVar = match(TokenKind::KwVar);
    if (!isConst && !isVar) {
        stmt->type = parseType();
    }
    const Token& name = expect(TokenKind::Ident, "variable name");
    stmt->text = name.text;
    if (match(TokenKind::Assign)) {
        stmt->initExpr = parseExpression();
    }
    expect(TokenKind::Semicolon, "';'");
    return stmt;
}

NodePtr Parser::parseIf() {
    expect(TokenKind::KwIf, "'if'");
    expect(TokenKind::LParen, "'('");
    auto node = Node::make(NodeKind::IfStmt);
    node->cond = parseExpression();
    expect(TokenKind::RParen, "')'");
    node->thenBranch = parseStatement();
    if (match(TokenKind::KwElse)) {
        node->elseBranch = parseStatement();
    }
    return node;
}

NodePtr Parser::parseFor() {
    expect(TokenKind::KwFor, "'for'");
    expect(TokenKind::LParen, "'('");
    auto node = Node::make(NodeKind::ForStmt);
    // init: either a var decl or an expression or empty
    if (!check(TokenKind::Semicolon)) {
        if (peek().kind == TokenKind::KwVar || peek().kind == TokenKind::KwConst ||
            peek().kind == TokenKind::KwInt32 || peek().kind == TokenKind::KwUint32 ||
            peek().kind == TokenKind::KwInt64 || peek().kind == TokenKind::KwUint64 ||
            peek().kind == TokenKind::KwFloat || peek().kind == TokenKind::KwDouble ||
            peek().kind == TokenKind::KwBool || peek().kind == TokenKind::KwString ||
            peek().kind == TokenKind::KwAuto) {
            node->lhs = parseVarDecl();  // includes trailing semicolon
        } else {
            node->lhs = parseExprStatement();  // includes trailing semicolon
        }
    } else {
        expect(TokenKind::Semicolon, "';'");
    }
    // condition
    if (!check(TokenKind::Semicolon)) {
        node->cond = parseExpression();
    }
    expect(TokenKind::Semicolon, "';'");
    // step
    if (!check(TokenKind::RParen)) {
        node->step = parseExpression();
    }
    expect(TokenKind::RParen, "')'");
    node->thenBranch = parseStatement();
    return node;
}

NodePtr Parser::parseWhile() {
    expect(TokenKind::KwWhile, "'while'");
    expect(TokenKind::LParen, "'('");
    auto node = Node::make(NodeKind::WhileStmt);
    node->cond = parseExpression();
    expect(TokenKind::RParen, "')'");
    node->thenBranch = parseStatement();
    return node;
}

NodePtr Parser::parseReturn() {
    expect(TokenKind::KwReturn, "'return'");
    auto node = Node::make(NodeKind::ReturnStmt);
    if (!check(TokenKind::Semicolon)) {
        node->initExpr = parseExpression();
    }
    expect(TokenKind::Semicolon, "';'");
    return node;
}

NodePtr Parser::parseBreak() {
    expect(TokenKind::KwBreak, "'break'");
    expect(TokenKind::Semicolon, "';'");
    return Node::make(NodeKind::BreakStmt);
}

NodePtr Parser::parseContinue() {
    expect(TokenKind::KwContinue, "'continue'");
    expect(TokenKind::Semicolon, "';'");
    return Node::make(NodeKind::ContinueStmt);
}

NodePtr Parser::parseExprStatement() {
    auto node = Node::make(NodeKind::ExprStmt);
    node->initExpr = parseExpression();
    expect(TokenKind::Semicolon, "';'");
    return node;
}

// -----------------------------------------------------------------------------
// Expressions
// -----------------------------------------------------------------------------
NodePtr Parser::parseExpression() {
    return parseAssignment();
}

NodePtr Parser::parseAssignment() {
    NodePtr left = parseBinary(1);
    TokenKind k = peek().kind;
    if (k == TokenKind::Assign) {
        advance();
        NodePtr right = parseAssignment();
        auto n = Node::make(NodeKind::Assign);
        n->lhs = left;
        n->rhs = right;
        return n;
    } else if (k == TokenKind::PlusAssign || k == TokenKind::MinusAssign ||
               k == TokenKind::StarAssign || k == TokenKind::SlashAssign) {
        advance();
        NodePtr right = parseAssignment();
        auto n = Node::make(NodeKind::CompoundAssign);
        n->text = tokenKindName(k);
        n->lhs = left;
        n->rhs = right;
        return n;
    }
    return left;
}

int operatorPrecedence(TokenKind k) {
    switch (k) {
        case TokenKind::Or:        return 1;
        case TokenKind::And:       return 2;
        case TokenKind::Eq:
        case TokenKind::NotEq:     return 3;
        case TokenKind::Lt:
        case TokenKind::Gt:
        case TokenKind::LtEq:
        case TokenKind::GtEq:      return 4;
        case TokenKind::Plus:
        case TokenKind::Minus:     return 5;
        case TokenKind::Star:
        case TokenKind::Slash:
        case TokenKind::Percent:   return 6;
        default:                   return 0;  // not a binary operator
    }
}

bool isBinaryOperator(TokenKind k) {
    return operatorPrecedence(k) > 0;
}

NodePtr Parser::parseBinary(int minPrec) {
    NodePtr left = parseUnary();
    while (true) {
        TokenKind k = peek().kind;
        int prec = operatorPrecedence(k);
        if (prec < minPrec) break;
        advance();
        NodePtr right = parseBinary(prec + 1);
        auto n = Node::make(NodeKind::Binary);
        n->text = tokenKindName(k);
        n->lhs = left;
        n->rhs = right;
        left = n;
    }
    return left;
}

NodePtr Parser::parseUnary() {
    if (check(TokenKind::Not) || check(TokenKind::Minus)) {
        const Token& op = advance();
        auto n = Node::make(NodeKind::Unary);
        n->text = op.text;
        n->lhs = parseUnary();
        return n;
    }
    return parsePostfix();
}

NodePtr Parser::parsePostfix() {
    NodePtr expr = parsePrimary();
    while (true) {
        if (check(TokenKind::Dot)) {
            advance();
            const Token& name = expect(TokenKind::Ident, "member name after '.'");
            auto n = Node::make(NodeKind::MemberAccess);
            n->lhs = expr;
            n->text = name.text;
            expr = n;
        } else if (check(TokenKind::LBracket)) {
            advance();
            NodePtr idx = parseExpression();
            expect(TokenKind::RBracket, "']'");
            auto n = Node::make(NodeKind::IndexAccess);
            n->lhs = expr;
            n->rhs = idx;
            expr = n;
        } else if (check(TokenKind::LParen)) {
            expr = parseCallArgs(expr);
        } else {
            break;
        }
    }
    return expr;
}

NodePtr Parser::parseCallArgs(NodePtr callee) {
    expect(TokenKind::LParen, "'('");
    auto call = Node::make(NodeKind::Call);
    call->lhs = callee;
    if (!check(TokenKind::RParen)) {
        do {
            call->children.push_back(parseExpression());
        } while (match(TokenKind::Comma));
    }
    expect(TokenKind::RParen, "')'");
    return call;
}

NodePtr Parser::parsePrimary() {
    const Token& t = peek();
    switch (t.kind) {
        case TokenKind::IntLit: {
            advance();
            auto n = Node::make(NodeKind::IntLiteral);
            n->text = t.text;
            try { n->intVal = std::stoll(t.text); } catch (...) { n->intVal = 0; }
            n->line = t.line; n->col = t.col;
            return n;
        }
        case TokenKind::FloatLit: {
            advance();
            auto n = Node::make(NodeKind::FloatLiteral);
            n->text = t.text;
            try { n->floatVal = std::stod(t.text); } catch (...) { n->floatVal = 0.0; }
            n->line = t.line; n->col = t.col;
            return n;
        }
        case TokenKind::StringLit: {
            advance();
            auto n = Node::make(NodeKind::StringLiteral);
            n->text = t.text;
            n->line = t.line; n->col = t.col;
            return n;
        }
        case TokenKind::KwTrue: {
            advance();
            auto n = Node::make(NodeKind::BoolLiteral);
            n->boolVal = true;
            return n;
        }
        case TokenKind::KwFalse: {
            advance();
            auto n = Node::make(NodeKind::BoolLiteral);
            n->boolVal = false;
            return n;
        }
        case TokenKind::KwNull: {
            advance();
            return Node::make(NodeKind::NullLiteral);
        }
        case TokenKind::KwFunction: {
            // anonymous function — not yet supported, but parse and emit
            emitError("'function' keyword not yet supported in TDScript expressions", t.line, t.col);
            advance();
            return Node::make(NodeKind::NullLiteral);
        }
        case TokenKind::Ident: {
            advance();
            auto n = Node::make(NodeKind::Identifier);
            n->text = t.text;
            n->line = t.line; n->col = t.col;
            return n;
        }
        case TokenKind::LParen: {
            advance();
            NodePtr e = parseExpression();
            expect(TokenKind::RParen, "')'");
            return e;
        }
        default:
            emitError(std::string("unexpected token '") + t.text + "' in expression", t.line, t.col);
            advance();
            return Node::make(NodeKind::NullLiteral);
    }
}

} // namespace td::tdscript
