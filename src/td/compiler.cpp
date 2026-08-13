#include "compiler.h"
#include <cstring>
#include <cstdlib>
#include <cstdio>

namespace td {

// ==================== CompiledFunction ====================

CompiledFunction::CompiledFunction()
    : bytecode(nullptr), bytecodeSize(0), bytecodeCapacity(0),
      localCount(0), paramCount(0) {
    name[0] = '\0';
}

CompiledFunction::~CompiledFunction() {
    if (bytecode) {
        free(bytecode);
        bytecode = nullptr;
    }
}

void CompiledFunction::emit(uint8_t byte) {
    if (bytecodeSize >= bytecodeCapacity) {
        bytecodeCapacity = bytecodeCapacity == 0 ? 256 : bytecodeCapacity * 2;
        bytecode = (uint8_t*)realloc(bytecode, bytecodeCapacity);
    }
    bytecode[bytecodeSize++] = byte;
}

void CompiledFunction::emitOp(OpCode op) {
    emit((uint8_t)op);
}

void CompiledFunction::emitInt(int32_t value) {
    emit((uint8_t)(value & 0xFF));
    emit((uint8_t)((value >> 8) & 0xFF));
    emit((uint8_t)((value >> 16) & 0xFF));
    emit((uint8_t)((value >> 24) & 0xFF));
}

void CompiledFunction::emitFloat(float value) {
    uint8_t* bytes = (uint8_t*)&value;
    emit(bytes[0]); emit(bytes[1]); emit(bytes[2]); emit(bytes[3]);
}

int CompiledFunction::emitJump(OpCode jumpOp) {
    emitOp(jumpOp);
    int offset = bytecodeSize;
    emitInt(0); // placeholder
    return offset;
}

void CompiledFunction::patchJump(int offset) {
    int jump = bytecodeSize - offset - 4;
    bytecode[offset]     = (uint8_t)(jump & 0xFF);
    bytecode[offset + 1] = (uint8_t)((jump >> 8) & 0xFF);
    bytecode[offset + 2] = (uint8_t)((jump >> 16) & 0xFF);
    bytecode[offset + 3] = (uint8_t)((jump >> 24) & 0xFF);
}

int CompiledFunction::currentOffset() const {
    return bytecodeSize;
}

// ==================== CompiledScript ====================

CompiledScript::CompiledScript()
    : functionCount(0), stringCount(0), structCount(0), globalCount(0) {
    memset(functions, 0, sizeof(functions));
    memset(stringTable, 0, sizeof(stringTable));
}

CompiledScript::~CompiledScript() {
    for (int i = 0; i < functionCount; i++) {
        delete functions[i];
    }
    for (int i = 0; i < stringCount; i++) {
        free(stringTable[i]);
    }
}

int CompiledScript::addString(const char* str) {
    int existing = findString(str);
    if (existing >= 0) return existing;
    if (stringCount >= 1024) return -1;
    stringTable[stringCount] = (char*)malloc(strlen(str) + 1);
    strcpy(stringTable[stringCount], str);
    return stringCount++;
}

int CompiledScript::findString(const char* str) const {
    for (int i = 0; i < stringCount; i++) {
        if (strcmp(stringTable[i], str) == 0) return i;
    }
    return -1;
}

int CompiledScript::findFunction(const char* name) const {
    for (int i = 0; i < functionCount; i++) {
        if (strcmp(functions[i]->name, name) == 0) return i;
    }
    return -1;
}

int CompiledScript::findGlobal(const char* name) const {
    for (int i = 0; i < globalCount; i++) {
        if (strcmp(globals[i].name, name) == 0) return i;
    }
    return -1;
}

// ==================== TDCompiler ====================

void TDCompiler::error(const char* msg, int line) {
    if (m_hadError) return;
    m_hadError = true;
    m_errorLine = line;
    snprintf(m_error, sizeof(m_error), "[line %d] Compile error: %s", line, msg);
}

int TDCompiler::resolveLocal(const char* name) {
    for (int i = m_localCount - 1; i >= 0; i--) {
        if (strcmp(m_locals[i].name, name) == 0) return i;
    }
    return -1;
}

int TDCompiler::addLocal(const char* name) {
    if (m_localCount >= 256) {
        error("Too many local variables");
        return -1;
    }
    Local& local = m_locals[m_localCount];
    strncpy(local.name, name, 63);
    local.name[63] = '\0';
    local.depth = m_scopeDepth;
    return m_localCount++;
}

bool TDCompiler::compile(ASTNode* program, CompiledScript& out) {
    m_script = &out;
    m_hadError = false;
    m_error[0] = '\0';
    m_localCount = 0;
    m_scopeDepth = 0;

    if (!program || program->type != NodeType::Program) {
        error("Expected program node");
        return false;
    }

    for (int i = 0; i < program->program.statementCount; i++) {
        ASTNode* stmt = program->program.statements[i];
        if (!stmt) continue;

        switch (stmt->type) {
            case NodeType::FunctionDecl:
                compileFunction(stmt);
                break;
            case NodeType::StructDecl:
                compileStructDecl(stmt);
                break;
            case NodeType::VarDecl:
                // Top-level var declarations become globals
                if (m_script->globalCount < 256) {
                    strncpy(m_script->globals[m_script->globalCount].name,
                            stmt->varDecl.name, 63);
                    strncpy(m_script->globals[m_script->globalCount].type,
                            stmt->varDecl.type, 31);
                    m_script->globalCount++;
                }
                break;
            default:
                break;
        }
    }

    return !m_hadError;
}

void TDCompiler::compileFunction(ASTNode* node) {
    CompiledFunction* func = new CompiledFunction();
    strncpy(func->name, node->funcDecl.name, 63);
    func->paramCount = node->funcDecl.paramCount;

    CompiledFunction* previousFunc = m_currentFunc;
    int previousLocalCount = m_localCount;
    m_currentFunc = func;
    m_localCount = 0;
    m_scopeDepth = 0;

    // Add parameters as locals
    for (int i = 0; i < func->paramCount; i++) {
        addLocal(node->funcDecl.paramNames[i]);
    }

    // Compile body
    if (node->funcDecl.body) {
        compileBlock(node->funcDecl.body);
    }

    // Ensure function ends with return
    func->emitOp(OpCode::RETURN_VOID);
    func->localCount = m_localCount;

    // Add to script
    if (m_script->functionCount < 256) {
        m_script->functions[m_script->functionCount++] = func;
    }

    m_currentFunc = previousFunc;
    m_localCount = previousLocalCount;
}

void TDCompiler::compileBlock(ASTNode* node) {
    if (!node) return;
    m_scopeDepth++;

    int previousLocalCount = m_localCount;

    for (int i = 0; i < node->block.statementCount; i++) {
        compileNode(node->block.statements[i]);
    }

    // Pop locals from this scope
    while (m_localCount > previousLocalCount) {
        m_localCount--;
    }

    m_scopeDepth--;
}

void TDCompiler::compileNode(ASTNode* node) {
    if (!node || m_hadError) return;

    switch (node->type) {
        case NodeType::Block:       compileBlock(node); break;
        case NodeType::IfStmt:      compileIf(node); break;
        case NodeType::WhileStmt:   compileWhile(node); break;
        case NodeType::ForStmt:     compileFor(node); break;
        case NodeType::ReturnStmt:  compileReturn(node); break;
        case NodeType::ExprStmt:    compileExprStmt(node); break;
        case NodeType::VarDecl:     compileVarDecl(node); break;
        case NodeType::BinaryExpr:  compileBinary(node); break;
        case NodeType::UnaryExpr:   compileUnary(node); break;
        case NodeType::CallExpr:    compileCall(node); break;
        case NodeType::MemberExpr:  compileMember(node); break;
        case NodeType::LiteralExpr: compileLiteral(node); break;
        case NodeType::IdentifierExpr: compileIdentifier(node); break;
        case NodeType::AssignExpr:  compileAssignment(node); break;
        case NodeType::IndexExpr:   compileIndex(node); break;
        case NodeType::FunctionDecl: compileFunction(node); break;
        case NodeType::StructDecl:  compileStructDecl(node); break;
        default: break;
    }
}

void TDCompiler::compileIf(ASTNode* node) {
    compileNode(node->ifStmt.condition);
    int elseJump = m_currentFunc->emitJump(OpCode::JUMP_IF_FALSE);
    m_currentFunc->emitOp(OpCode::POP);
    compileNode(node->ifStmt.thenBranch);

    if (node->ifStmt.elseBranch) {
        int endJump = m_currentFunc->emitJump(OpCode::JUMP);
        m_currentFunc->patchJump(elseJump);
        m_currentFunc->emitOp(OpCode::POP);
        compileNode(node->ifStmt.elseBranch);
        m_currentFunc->patchJump(endJump);
    } else {
        m_currentFunc->patchJump(elseJump);
        m_currentFunc->emitOp(OpCode::POP);
    }
}

void TDCompiler::compileWhile(ASTNode* node) {
    int loopStart = m_currentFunc->currentOffset();
    compileNode(node->whileStmt.condition);
    int exitJump = m_currentFunc->emitJump(OpCode::JUMP_IF_FALSE);
    m_currentFunc->emitOp(OpCode::POP);
    compileNode(node->whileStmt.body);
    // Loop back
    m_currentFunc->emitOp(OpCode::LOOP);
    int loopBackDist = m_currentFunc->currentOffset() - loopStart + 4;
    m_currentFunc->emitInt(loopBackDist);
    m_currentFunc->patchJump(exitJump);
    m_currentFunc->emitOp(OpCode::POP);
}

void TDCompiler::compileFor(ASTNode* node) {
    int localSlot = addLocal(node->forStmt.varName);
    compileNode(node->forStmt.start);
    m_currentFunc->emitOp(OpCode::STORE_LOCAL);
    m_currentFunc->emitInt(localSlot);

    int loopStart = m_currentFunc->currentOffset();

    // Condition: localVar < end
    m_currentFunc->emitOp(OpCode::LOAD_LOCAL);
    m_currentFunc->emitInt(localSlot);
    compileNode(node->forStmt.end);
    m_currentFunc->emitOp(OpCode::LESS);

    int exitJump = m_currentFunc->emitJump(OpCode::JUMP_IF_FALSE);
    m_currentFunc->emitOp(OpCode::POP);

    compileNode(node->forStmt.body);

    // Increment
    if (node->forStmt.step) {
        m_currentFunc->emitOp(OpCode::LOAD_LOCAL);
        m_currentFunc->emitInt(localSlot);
        compileNode(node->forStmt.step);
        m_currentFunc->emitOp(OpCode::ADD);
    } else {
        m_currentFunc->emitOp(OpCode::LOAD_LOCAL);
        m_currentFunc->emitInt(localSlot);
        m_currentFunc->emitOp(OpCode::PUSH_INT);
        m_currentFunc->emitInt(1);
        m_currentFunc->emitOp(OpCode::ADD);
    }
    m_currentFunc->emitOp(OpCode::STORE_LOCAL);
    m_currentFunc->emitInt(localSlot);

    m_currentFunc->emitOp(OpCode::LOOP);
    int loopBackDist = m_currentFunc->currentOffset() - loopStart + 4;
    m_currentFunc->emitInt(loopBackDist);

    m_currentFunc->patchJump(exitJump);
    m_currentFunc->emitOp(OpCode::POP);
}

void TDCompiler::compileReturn(ASTNode* node) {
    if (node->returnStmt.value) {
        compileNode(node->returnStmt.value);
        m_currentFunc->emitOp(OpCode::RETURN);
    } else {
        m_currentFunc->emitOp(OpCode::RETURN_VOID);
    }
}

void TDCompiler::compileExprStmt(ASTNode* node) {
    compileNode(node->exprStmt.expression);
    m_currentFunc->emitOp(OpCode::POP);
}

void TDCompiler::compileVarDecl(ASTNode* node) {
    int slot = addLocal(node->varDecl.name);
    if (node->varDecl.initializer) {
        compileNode(node->varDecl.initializer);
    } else {
        m_currentFunc->emitOp(OpCode::PUSH_NULL);
    }
    m_currentFunc->emitOp(OpCode::STORE_LOCAL);
    m_currentFunc->emitInt(slot);
}

void TDCompiler::compileBinary(ASTNode* node) {
    compileNode(node->binaryExpr.left);
    compileNode(node->binaryExpr.right);

    switch (node->binaryExpr.op) {
        case TokenType::Plus:         m_currentFunc->emitOp(OpCode::ADD); break;
        case TokenType::Minus:        m_currentFunc->emitOp(OpCode::SUB); break;
        case TokenType::Star:         m_currentFunc->emitOp(OpCode::MUL); break;
        case TokenType::Slash:        m_currentFunc->emitOp(OpCode::DIV); break;
        case TokenType::Percent:      m_currentFunc->emitOp(OpCode::MOD); break;
        case TokenType::EqualsEquals: m_currentFunc->emitOp(OpCode::EQUAL); break;
        case TokenType::BangEquals:   m_currentFunc->emitOp(OpCode::NOT_EQUAL); break;
        case TokenType::Less:         m_currentFunc->emitOp(OpCode::LESS); break;
        case TokenType::LessEqual:    m_currentFunc->emitOp(OpCode::LESS_EQUAL); break;
        case TokenType::Greater:      m_currentFunc->emitOp(OpCode::GREATER); break;
        case TokenType::GreaterEqual: m_currentFunc->emitOp(OpCode::GREATER_EQUAL); break;
        case TokenType::And:          m_currentFunc->emitOp(OpCode::AND); break;
        case TokenType::Or:           m_currentFunc->emitOp(OpCode::OR); break;
        default:
            error("Unknown binary operator", node->line);
    }
}

void TDCompiler::compileUnary(ASTNode* node) {
    compileNode(node->unaryExpr.operand);
    switch (node->unaryExpr.op) {
        case TokenType::Minus: m_currentFunc->emitOp(OpCode::NEGATE); break;
        case TokenType::Bang:  m_currentFunc->emitOp(OpCode::NOT); break;
        default:
            error("Unknown unary operator", node->line);
    }
}

void TDCompiler::compileCall(ASTNode* node) {
    // Push arguments
    for (int i = 0; i < node->callExpr.argCount; i++) {
        compileNode(node->callExpr.arguments[i]);
    }
    compileNode(node->callExpr.callee);
    m_currentFunc->emitOp(OpCode::CALL);
    m_currentFunc->emitInt(node->callExpr.argCount);
}

void TDCompiler::compileMember(ASTNode* node) {
    compileNode(node->memberExpr.object);
    int strIdx = m_script->addString(node->memberExpr.member);
    m_currentFunc->emitOp(OpCode::GET_FIELD);
    m_currentFunc->emitInt(strIdx);
}

void TDCompiler::compileLiteral(ASTNode* node) {
    Token& val = node->literalExpr.value;
    switch (val.type) {
        case TokenType::Integer:
            m_currentFunc->emitOp(OpCode::PUSH_INT);
            m_currentFunc->emitInt((int32_t)val.intValue);
            break;
        case TokenType::Float:
            m_currentFunc->emitOp(OpCode::PUSH_FLOAT);
            m_currentFunc->emitFloat((float)val.floatValue);
            break;
        case TokenType::String: {
            // Remove quotes from lexeme
            char str[128];
            int len = (int)strlen(val.lexeme);
            if (len >= 2) {
                strncpy(str, val.lexeme + 1, len - 2);
                str[len - 2] = '\0';
            } else {
                str[0] = '\0';
            }
            int idx = m_script->addString(str);
            m_currentFunc->emitOp(OpCode::PUSH_STRING);
            m_currentFunc->emitInt(idx);
            break;
        }
        case TokenType::True:
            m_currentFunc->emitOp(OpCode::PUSH_BOOL);
            m_currentFunc->emit(1);
            break;
        case TokenType::False:
            m_currentFunc->emitOp(OpCode::PUSH_BOOL);
            m_currentFunc->emit(0);
            break;
        case TokenType::Null:
            m_currentFunc->emitOp(OpCode::PUSH_NULL);
            break;
        default:
            error("Unknown literal type", node->line);
    }
}

void TDCompiler::compileIdentifier(ASTNode* node) {
    int localIdx = resolveLocal(node->identExpr.name);
    if (localIdx >= 0) {
        m_currentFunc->emitOp(OpCode::LOAD_LOCAL);
        m_currentFunc->emitInt(localIdx);
    } else {
        int globalIdx = m_script->findGlobal(node->identExpr.name);
        if (globalIdx >= 0) {
            m_currentFunc->emitOp(OpCode::LOAD_GLOBAL);
            m_currentFunc->emitInt(globalIdx);
        } else {
            // Could be a function name — push as string for CALL lookup
            int strIdx = m_script->addString(node->identExpr.name);
            m_currentFunc->emitOp(OpCode::CONST_STRING);
            m_currentFunc->emitInt(strIdx);
        }
    }
}

void TDCompiler::compileAssignment(ASTNode* node) {
    compileNode(node->assignExpr.value);

    ASTNode* target = node->assignExpr.target;
    if (!target) {
        error("Invalid assignment target", node->line);
        return;
    }

    if (target->type == NodeType::IdentifierExpr) {
        int localIdx = resolveLocal(target->identExpr.name);
        if (localIdx >= 0) {
            m_currentFunc->emitOp(OpCode::STORE_LOCAL);
            m_currentFunc->emitInt(localIdx);
        } else {
            int globalIdx = m_script->findGlobal(target->identExpr.name);
            if (globalIdx >= 0) {
                m_currentFunc->emitOp(OpCode::STORE_GLOBAL);
                m_currentFunc->emitInt(globalIdx);
            } else {
                error("Undefined variable in assignment", node->line);
            }
        }
    } else if (target->type == NodeType::MemberExpr) {
        compileNode(target->memberExpr.object);
        int strIdx = m_script->addString(target->memberExpr.member);
        m_currentFunc->emitOp(OpCode::SET_FIELD);
        m_currentFunc->emitInt(strIdx);
    } else {
        error("Invalid assignment target", node->line);
    }
}

void TDCompiler::compileStructDecl(ASTNode* node) {
    if (m_script->structCount >= 64) {
        error("Too many struct definitions", node->line);
        return;
    }
    auto& def = m_script->structs[m_script->structCount];
    strncpy(def.name, node->structDecl.name, 63);
    def.fieldCount = node->structDecl.fieldCount;
    for (int i = 0; i < def.fieldCount; i++) {
        strncpy(def.fieldNames[i], node->structDecl.fields[i].name, 63);
    }
    m_script->structCount++;
}

void TDCompiler::compileIndex(ASTNode* node) {
    compileNode(node->indexExpr.object);
    compileNode(node->indexExpr.index);
    // Use GET_FIELD with a numeric index (simplified)
    m_currentFunc->emitOp(OpCode::GET_FIELD);
    m_currentFunc->emitInt(-1); // -1 signals index-based access
}

} // namespace td
