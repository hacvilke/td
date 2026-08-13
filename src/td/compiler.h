#pragma once
#include "ast.h"
#include <cstdint>

namespace td {

enum class OpCode : uint8_t {
    PUSH_INT, PUSH_FLOAT, PUSH_STRING, PUSH_BOOL, PUSH_NULL,
    POP, DUP,
    ADD, SUB, MUL, DIV, MOD, NEGATE,
    EQUAL, NOT_EQUAL, LESS, LESS_EQUAL, GREATER, GREATER_EQUAL,
    NOT, AND, OR,
    LOAD_LOCAL, STORE_LOCAL, LOAD_GLOBAL, STORE_GLOBAL,
    LOAD_THIS, STORE_THIS,
    JUMP, JUMP_IF_FALSE, JUMP_IF_TRUE, LOOP,
    CALL, RETURN, RETURN_VOID,
    DEFINE_FUNCTION,
    CREATE_STRUCT, GET_FIELD, SET_FIELD,
    ENTITY_CREATE, ENTITY_ADD_COMPONENT, ENTITY_GET_COMPONENT,
    ENGINE_CALL,
    CONST_STRING,
    HALT
};

struct CompiledFunction {
    char name[64];
    uint8_t* bytecode;
    int bytecodeSize;
    int bytecodeCapacity;
    int localCount;
    int paramCount;

    CompiledFunction();
    ~CompiledFunction();
    void emit(uint8_t byte);
    void emitOp(OpCode op);
    void emitInt(int32_t value);
    void emitFloat(float value);
    int emitJump(OpCode jumpOp);
    void patchJump(int offset);
    int currentOffset() const;
};

struct CompiledScript {
    CompiledFunction* functions[256];
    int functionCount;
    char* stringTable[1024];
    int stringCount;

    struct StructDef {
        char name[64];
        char fieldNames[32][64];
        int fieldCount;
    } structs[64];
    int structCount;

    struct GlobalVar {
        char name[64];
        char type[32];
    } globals[256];
    int globalCount;

    CompiledScript();
    ~CompiledScript();
    int addString(const char* str);
    int findString(const char* str) const;
    int findFunction(const char* name) const;
    int findGlobal(const char* name) const;
};

class TDCompiler {
public:
    bool compile(ASTNode* program, CompiledScript& out);
    const char* getError() const { return m_error; }
    int getErrorLine() const { return m_errorLine; }

private:
    void compileNode(ASTNode* node);
    void compileFunction(ASTNode* node);
    void compileBlock(ASTNode* node);
    void compileIf(ASTNode* node);
    void compileWhile(ASTNode* node);
    void compileFor(ASTNode* node);
    void compileReturn(ASTNode* node);
    void compileBinary(ASTNode* node);
    void compileUnary(ASTNode* node);
    void compileCall(ASTNode* node);
    void compileMember(ASTNode* node);
    void compileLiteral(ASTNode* node);
    void compileIdentifier(ASTNode* node);
    void compileAssignment(ASTNode* node);
    void compileVarDecl(ASTNode* node);
    void compileStructDecl(ASTNode* node);
    void compileExprStmt(ASTNode* node);
    void compileIndex(ASTNode* node);

    struct Local {
        char name[64];
        int depth;
    };

    int resolveLocal(const char* name);
    int addLocal(const char* name);

    void error(const char* msg, int line = 0);

    CompiledScript* m_script = nullptr;
    CompiledFunction* m_currentFunc = nullptr;
    Local m_locals[256];
    int m_localCount = 0;
    int m_scopeDepth = 0;
    int m_errorLine = 0;
    char m_error[256] = {};
    bool m_hadError = false;
};

} // namespace td
