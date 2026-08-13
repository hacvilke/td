#include "parser.h"
#include <cstring>
#include <cstdlib>
#include <cstdio>   // snprintf

namespace td {

// AST memory management
static ASTNode* allocatedNodes[10000];
static int allocatedNodeCount = 0;

ASTNode* astCreate(NodeType type, int line) {
    ASTNode* node = (ASTNode*)malloc(sizeof(ASTNode));
    memset(node, 0, sizeof(ASTNode));
    node->type = type;
    node->line = line;
    
    if (allocatedNodeCount < 10000) {
        allocatedNodes[allocatedNodeCount++] = node;
    }
    
    return node;
}

void astFree(ASTNode* node) {
    if (!node) return;
    free(node);
}

void astFreeAll(ASTNode* root) {
    (void)root;
    for (int i = 0; i < allocatedNodeCount; i++) {
        free(allocatedNodes[i]);
    }
    allocatedNodeCount = 0;
}

// Parser implementation
bool Parser::parse(const char* source, ASTNode** outProgram) {
    m_lexer.init(source);
    m_hadError = false;
    m_panicMode = false;
    m_error[0] = '\0';
    m_errorLine = 0;
    
    advance();
    
    *outProgram = parseProgram();
    
    return !m_hadError;
}

Token Parser::advance() {
    m_previous = m_current;
    
    while (true) {
        m_current = m_lexer.nextToken();
        
        if (m_current.type != TokenType::Error) break;
        
        errorAt(m_current, m_current.lexeme);
    }
    
    return m_previous;
}

bool Parser::check(TokenType type) const {
    return m_current.type == type;
}

bool Parser::match(TokenType type) {
    if (!check(type)) return false;
    advance();
    return true;
}

Token Parser::consume(TokenType type, const char* message) {
    if (check(type)) {
        return advance();
    }
    
    errorAt(m_current, message);
    return m_current;
}

bool Parser::isAtEnd() const {
    return m_current.type == TokenType::EndOfFile;
}

Token Parser::peek() const {
    return m_current;
}

Token Parser::previous() const {
    return m_previous;
}

void Parser::error(const char* message) {
    errorAt(m_previous, message);
}

void Parser::errorAt(const Token& token, const char* message) {
    if (m_panicMode) return;
    m_panicMode = true;
    
    m_errorLine = token.line;
    snprintf(m_error, sizeof(m_error), "[line %d] Error at '%s': %s",
             token.line, token.lexeme, message);
    
    m_hadError = true;
}

void Parser::synchronize() {
    m_panicMode = false;
    
    while (!isAtEnd()) {
        if (m_previous.type == TokenType::Semicolon) return;
        
        switch (m_current.type) {
            case TokenType::Fn:
            case TokenType::Let:
            case TokenType::Const:
            case TokenType::For:
            case TokenType::If:
            case TokenType::While:
            case TokenType::Return:
            case TokenType::Struct:
            case TokenType::Entity:
                return;
            default:
                break;
        }
        
        advance();
    }
}

ASTNode* Parser::parseProgram() {
    ASTNode* program = astCreate(NodeType::Program, 1);
    
    ASTNode* statements[1000];
    int count = 0;
    
    while (!isAtEnd() && count < 1000) {
        ASTNode* decl = parseDeclaration();
        if (decl) {
            statements[count++] = decl;
        }
        
        if (m_panicMode) {
            synchronize();
        }
    }
    
    program->program.statements = (ASTNode**)malloc(count * sizeof(ASTNode*));
    memcpy(program->program.statements, statements, count * sizeof(ASTNode*));
    program->program.statementCount = count;
    
    return program;
}

ASTNode* Parser::parseDeclaration() {
    if (match(TokenType::Fn)) return parseFunctionDecl();
    if (match(TokenType::Struct)) return parseStructDecl();
    if (match(TokenType::Entity)) return parseEntityDecl();
    if (match(TokenType::Let)) return parseVarDecl(false);
    if (match(TokenType::Const)) return parseVarDecl(true);
    
    return parseStatement();
}

ASTNode* Parser::parseFunctionDecl() {
    ASTNode* node = astCreate(NodeType::FunctionDecl, m_previous.line);
    
    consume(TokenType::Identifier, "Expected function name");
    strncpy(node->funcDecl.name, m_previous.lexeme, 63);
    
    consume(TokenType::LeftParen, "Expected '(' after function name");
    
    node->funcDecl.paramCount = 0;
    
    if (!check(TokenType::RightParen)) {
        do {
            if (node->funcDecl.paramCount >= 16) {
                error("Cannot have more than 16 parameters");
            }
            
            consume(TokenType::Identifier, "Expected parameter name");
            strncpy(node->funcDecl.paramNames[node->funcDecl.paramCount], 
                    m_previous.lexeme, 63);
            
            if (match(TokenType::Colon)) {
                consume(TokenType::Identifier, "Expected parameter type");
                strncpy(node->funcDecl.paramTypes[node->funcDecl.paramCount],
                        m_previous.lexeme, 31);
            } else {
                strcpy(node->funcDecl.paramTypes[node->funcDecl.paramCount], "any");
            }
            
            node->funcDecl.paramCount++;
        } while (match(TokenType::Comma));
    }
    
    consume(TokenType::RightParen, "Expected ')' after parameters");
    
    // Return type
    if (match(TokenType::Arrow)) {
        consume(TokenType::Identifier, "Expected return type");
        strncpy(node->funcDecl.returnType, m_previous.lexeme, 31);
    } else {
        strcpy(node->funcDecl.returnType, "void");
    }
    
    consume(TokenType::LeftBrace, "Expected '{' before function body");
    node->funcDecl.body = parseBlock();
    
    return node;
}

ASTNode* Parser::parseStructDecl() {
    ASTNode* node = astCreate(NodeType::StructDecl, m_previous.line);
    
    consume(TokenType::Identifier, "Expected struct name");
    strncpy(node->structDecl.name, m_previous.lexeme, 63);
    
    consume(TokenType::LeftBrace, "Expected '{' after struct name");
    
    node->structDecl.fieldCount = 0;
    
    while (!check(TokenType::RightBrace) && !isAtEnd()) {
        if (node->structDecl.fieldCount >= 32) {
            error("Cannot have more than 32 fields");
            break;
        }
        
        consume(TokenType::Identifier, "Expected field name");
        strncpy(node->structDecl.fields[node->structDecl.fieldCount].name,
                m_previous.lexeme, 63);
        
        consume(TokenType::Colon, "Expected ':' after field name");
        consume(TokenType::Identifier, "Expected field type");
        strncpy(node->structDecl.fields[node->structDecl.fieldCount].type,
                m_previous.lexeme, 31);
        
        node->structDecl.fieldCount++;
        
        if (!match(TokenType::Comma) && !check(TokenType::RightBrace)) {
            match(TokenType::Semicolon);
        }
    }
    
    consume(TokenType::RightBrace, "Expected '}' after struct body");
    
    return node;
}

ASTNode* Parser::parseEntityDecl() {
    ASTNode* node = astCreate(NodeType::EntityDecl, m_previous.line);
    
    consume(TokenType::Identifier, "Expected entity name");
    strncpy(node->entityDecl.name, m_previous.lexeme, 63);
    
    consume(TokenType::LeftBrace, "Expected '{' after entity name");
    node->entityDecl.body = parseBlock();
    
    return node;
}

ASTNode* Parser::parseVarDecl(bool isConst) {
    ASTNode* node = astCreate(NodeType::VarDecl, m_previous.line);
    node->varDecl.isConst = isConst;
    
    consume(TokenType::Identifier, "Expected variable name");
    strncpy(node->varDecl.name, m_previous.lexeme, 63);
    
    if (match(TokenType::Colon)) {
        consume(TokenType::Identifier, "Expected type");
        strncpy(node->varDecl.type, m_previous.lexeme, 31);
    } else {
        strcpy(node->varDecl.type, "auto");
    }
    
    if (match(TokenType::Equals)) {
        node->varDecl.initializer = parseExpression();
    }
    
    consume(TokenType::Semicolon, "Expected ';' after variable declaration");
    
    return node;
}

ASTNode* Parser::parseStatement() {
    if (match(TokenType::If)) return parseIfStmt();
    if (match(TokenType::While)) return parseWhileStmt();
    if (match(TokenType::For)) return parseForStmt();
    if (match(TokenType::Return)) return parseReturnStmt();
    if (match(TokenType::LeftBrace)) return parseBlock();
    
    if (match(TokenType::Break)) {
        ASTNode* node = astCreate(NodeType::BreakStmt, m_previous.line);
        consume(TokenType::Semicolon, "Expected ';' after 'break'");
        return node;
    }
    
    if (match(TokenType::Continue)) {
        ASTNode* node = astCreate(NodeType::ContinueStmt, m_previous.line);
        consume(TokenType::Semicolon, "Expected ';' after 'continue'");
        return node;
    }
    
    return parseExpressionStatement();
}

ASTNode* Parser::parseBlock() {
    ASTNode* node = astCreate(NodeType::Block, m_previous.line);
    
    ASTNode* statements[1000];
    int count = 0;
    
    while (!check(TokenType::RightBrace) && !isAtEnd() && count < 1000) {
        ASTNode* stmt = parseDeclaration();
        if (stmt) {
            statements[count++] = stmt;
        }
    }
    
    consume(TokenType::RightBrace, "Expected '}' after block");
    
    node->block.statements = (ASTNode**)malloc(count * sizeof(ASTNode*));
    memcpy(node->block.statements, statements, count * sizeof(ASTNode*));
    node->block.statementCount = count;
    
    return node;
}

ASTNode* Parser::parseIfStmt() {
    ASTNode* node = astCreate(NodeType::IfStmt, m_previous.line);
    
    consume(TokenType::LeftParen, "Expected '(' after 'if'");
    node->ifStmt.condition = parseExpression();
    consume(TokenType::RightParen, "Expected ')' after if condition");
    
    node->ifStmt.thenBranch = parseStatement();
    
    if (match(TokenType::Else)) {
        node->ifStmt.elseBranch = parseStatement();
    }
    
    return node;
}

ASTNode* Parser::parseWhileStmt() {
    ASTNode* node = astCreate(NodeType::WhileStmt, m_previous.line);
    
    consume(TokenType::LeftParen, "Expected '(' after 'while'");
    node->whileStmt.condition = parseExpression();
    consume(TokenType::RightParen, "Expected ')' after while condition");
    
    node->whileStmt.body = parseStatement();
    
    return node;
}

ASTNode* Parser::parseForStmt() {
    ASTNode* node = astCreate(NodeType::ForStmt, m_previous.line);
    
    consume(TokenType::LeftParen, "Expected '(' after 'for'");
    
    consume(TokenType::Identifier, "Expected variable name");
    strncpy(node->forStmt.varName, m_previous.lexeme, 63);
    
    consume(TokenType::Equals, "Expected '=' in for loop");
    node->forStmt.start = parseExpression();
    
    consume(TokenType::Comma, "Expected ',' after start value");
    node->forStmt.end = parseExpression();
    
    if (match(TokenType::Comma)) {
        node->forStmt.step = parseExpression();
    }
    
    consume(TokenType::RightParen, "Expected ')' after for clauses");
    
    node->forStmt.body = parseStatement();
    
    return node;
}

ASTNode* Parser::parseReturnStmt() {
    ASTNode* node = astCreate(NodeType::ReturnStmt, m_previous.line);
    
    if (!check(TokenType::Semicolon)) {
        node->returnStmt.value = parseExpression();
    }
    
    consume(TokenType::Semicolon, "Expected ';' after return value");
    
    return node;
}

ASTNode* Parser::parseExpressionStatement() {
    ASTNode* node = astCreate(NodeType::ExprStmt, m_current.line);
    node->exprStmt.expression = parseExpression();
    consume(TokenType::Semicolon, "Expected ';' after expression");
    return node;
}

ASTNode* Parser::parseExpression() {
    return parseAssignment();
}

ASTNode* Parser::parseAssignment() {
    ASTNode* expr = parseOr();
    
    if (match(TokenType::Equals) || match(TokenType::PlusEquals) ||
        match(TokenType::MinusEquals) || match(TokenType::StarEquals) ||
        match(TokenType::SlashEquals)) {
        
        TokenType op = m_previous.type;
        ASTNode* value = parseAssignment();
        
        ASTNode* node = astCreate(NodeType::AssignExpr, m_previous.line);
        node->assignExpr.target = expr;
        node->assignExpr.op = op;
        node->assignExpr.value = value;
        return node;
    }
    
    return expr;
}

ASTNode* Parser::parseOr() {
    ASTNode* expr = parseAnd();
    
    while (match(TokenType::Or)) {
        ASTNode* node = astCreate(NodeType::BinaryExpr, m_previous.line);
        node->binaryExpr.left = expr;
        node->binaryExpr.op = TokenType::Or;
        node->binaryExpr.right = parseAnd();
        expr = node;
    }
    
    return expr;
}

ASTNode* Parser::parseAnd() {
    ASTNode* expr = parseEquality();
    
    while (match(TokenType::And)) {
        ASTNode* node = astCreate(NodeType::BinaryExpr, m_previous.line);
        node->binaryExpr.left = expr;
        node->binaryExpr.op = TokenType::And;
        node->binaryExpr.right = parseEquality();
        expr = node;
    }
    
    return expr;
}

ASTNode* Parser::parseEquality() {
    ASTNode* expr = parseComparison();
    
    while (match(TokenType::EqualsEquals) || match(TokenType::BangEquals)) {
        TokenType op = m_previous.type;
        ASTNode* node = astCreate(NodeType::BinaryExpr, m_previous.line);
        node->binaryExpr.left = expr;
        node->binaryExpr.op = op;
        node->binaryExpr.right = parseComparison();
        expr = node;
    }
    
    return expr;
}

ASTNode* Parser::parseComparison() {
    ASTNode* expr = parseAdditive();
    
    while (match(TokenType::Less) || match(TokenType::LessEqual) ||
           match(TokenType::Greater) || match(TokenType::GreaterEqual)) {
        TokenType op = m_previous.type;
        ASTNode* node = astCreate(NodeType::BinaryExpr, m_previous.line);
        node->binaryExpr.left = expr;
        node->binaryExpr.op = op;
        node->binaryExpr.right = parseAdditive();
        expr = node;
    }
    
    return expr;
}

ASTNode* Parser::parseAdditive() {
    ASTNode* expr = parseMultiplicative();
    
    while (match(TokenType::Plus) || match(TokenType::Minus)) {
        TokenType op = m_previous.type;
        ASTNode* node = astCreate(NodeType::BinaryExpr, m_previous.line);
        node->binaryExpr.left = expr;
        node->binaryExpr.op = op;
        node->binaryExpr.right = parseMultiplicative();
        expr = node;
    }
    
    return expr;
}

ASTNode* Parser::parseMultiplicative() {
    ASTNode* expr = parseUnary();
    
    while (match(TokenType::Star) || match(TokenType::Slash) || 
           match(TokenType::Percent)) {
        TokenType op = m_previous.type;
        ASTNode* node = astCreate(NodeType::BinaryExpr, m_previous.line);
        node->binaryExpr.left = expr;
        node->binaryExpr.op = op;
        node->binaryExpr.right = parseUnary();
        expr = node;
    }
    
    return expr;
}

ASTNode* Parser::parseUnary() {
    if (match(TokenType::Bang) || match(TokenType::Minus) ||
        match(TokenType::PlusPlus) || match(TokenType::MinusMinus)) {
        TokenType op = m_previous.type;
        ASTNode* node = astCreate(NodeType::UnaryExpr, m_previous.line);
        node->unaryExpr.op = op;
        node->unaryExpr.operand = parseUnary();
        node->unaryExpr.prefix = true;
        return node;
    }
    
    return parsePostfix();
}

ASTNode* Parser::parsePostfix() {
    ASTNode* expr = parseCall();
    
    while (match(TokenType::PlusPlus) || match(TokenType::MinusMinus)) {
        TokenType op = m_previous.type;
        ASTNode* node = astCreate(NodeType::UnaryExpr, m_previous.line);
        node->unaryExpr.op = op;
        node->unaryExpr.operand = expr;
        node->unaryExpr.prefix = false;
        expr = node;
    }
    
    return expr;
}

ASTNode* Parser::parseCall() {
    ASTNode* expr = parsePrimary();
    
    while (true) {
        if (match(TokenType::LeftParen)) {
            ASTNode* node = astCreate(NodeType::CallExpr, m_previous.line);
            node->callExpr.callee = expr;
            
            ASTNode* args[256];
            int argCount = 0;
            
            if (!check(TokenType::RightParen)) {
                do {
                    if (argCount >= 256) {
                        error("Cannot have more than 256 arguments");
                    }
                    args[argCount++] = parseExpression();
                } while (match(TokenType::Comma));
            }
            
            consume(TokenType::RightParen, "Expected ')' after arguments");
            
            node->callExpr.arguments = (ASTNode**)malloc(argCount * sizeof(ASTNode*));
            memcpy(node->callExpr.arguments, args, argCount * sizeof(ASTNode*));
            node->callExpr.argCount = argCount;
            
            expr = node;
        }
        else if (match(TokenType::Dot)) {
            consume(TokenType::Identifier, "Expected property name after '.'");
            
            ASTNode* node = astCreate(NodeType::MemberExpr, m_previous.line);
            node->memberExpr.object = expr;
            strncpy(node->memberExpr.member, m_previous.lexeme, 63);
            
            expr = node;
        }
        else if (match(TokenType::LeftBracket)) {
            ASTNode* node = astCreate(NodeType::IndexExpr, m_previous.line);
            node->indexExpr.object = expr;
            node->indexExpr.index = parseExpression();
            
            consume(TokenType::RightBracket, "Expected ']' after index");
            
            expr = node;
        }
        else {
            break;
        }
    }
    
    return expr;
}

ASTNode* Parser::parsePrimary() {
    if (match(TokenType::Integer) || match(TokenType::Float) ||
        match(TokenType::String) || match(TokenType::True) ||
        match(TokenType::False) || match(TokenType::Null)) {
        ASTNode* node = astCreate(NodeType::LiteralExpr, m_previous.line);
        node->literalExpr.value = m_previous;
        return node;
    }
    
    if (match(TokenType::This)) {
        return astCreate(NodeType::ThisExpr, m_previous.line);
    }
    
    if (match(TokenType::Identifier)) {
        ASTNode* node = astCreate(NodeType::IdentifierExpr, m_previous.line);
        strncpy(node->identExpr.name, m_previous.lexeme, 63);
        return node;
    }
    
    if (match(TokenType::LeftParen)) {
        ASTNode* expr = parseExpression();
        consume(TokenType::RightParen, "Expected ')' after expression");
        return expr;
    }
    
    if (match(TokenType::LeftBracket)) {
        ASTNode* node = astCreate(NodeType::ArrayExpr, m_previous.line);
        
        ASTNode* elements[1000];
        int count = 0;
        
        if (!check(TokenType::RightBracket)) {
            do {
                if (count >= 1000) {
                    error("Array too large");
                    break;
                }
                elements[count++] = parseExpression();
            } while (match(TokenType::Comma));
        }
        
        consume(TokenType::RightBracket, "Expected ']' after array elements");
        
        node->arrayExpr.elements = (ASTNode**)malloc(count * sizeof(ASTNode*));
        memcpy(node->arrayExpr.elements, elements, count * sizeof(ASTNode*));
        node->arrayExpr.elementCount = count;
        
        return node;
    }
    
    error("Expected expression");
    return nullptr;
}

} // namespace td
