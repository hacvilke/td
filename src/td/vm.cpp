#include "vm.h"
#include "../core/logger.h"
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <cmath>

namespace td {

float TDValue::toFloat() const {
    switch (type) {
        case TDValueType::Float: return floatValue;
        case TDValueType::Int:   return (float)intValue;
        case TDValueType::Bool:  return boolValue ? 1.0f : 0.0f;
        default: return 0.0f;
    }
}

int32_t TDValue::toInt() const {
    switch (type) {
        case TDValueType::Int:   return intValue;
        case TDValueType::Float: return (int32_t)floatValue;
        case TDValueType::Bool:  return boolValue ? 1 : 0;
        default: return 0;
    }
}

bool TDValue::toBool() const {
    switch (type) {
        case TDValueType::Bool:   return boolValue;
        case TDValueType::Int:    return intValue != 0;
        case TDValueType::Float:  return floatValue != 0.0f;
        case TDValueType::Null:   return false;
        case TDValueType::String: return stringValue && stringValue[0] != '\0';
        default: return true;
    }
}

bool TDVM::load(CompiledScript* script) {
    m_script = script;
    m_hadError = false;
    m_error[0] = '\0';
    m_stackTop = 0;
    m_frameCount = 0;
    for (int i = 0; i < 256; i++) m_globals[i] = TDValue::makeNull();
    return true;
}

void TDVM::reset() {
    m_stackTop = 0;
    m_frameCount = 0;
    m_hadError = false;
    m_running = false;
}

void TDVM::push(TDValue value) {
    if (m_stackTop >= TD_VM_MAX_STACK) {
        runtimeError("Stack overflow");
        return;
    }
    m_stack[m_stackTop++] = value;
}

TDValue TDVM::pop() {
    if (m_stackTop <= 0) {
        runtimeError("Stack underflow");
        return TDValue::makeNull();
    }
    return m_stack[--m_stackTop];
}

TDValue TDVM::peek(int distance) {
    if (m_stackTop - 1 - distance < 0) return TDValue::makeNull();
    return m_stack[m_stackTop - 1 - distance];
}

uint8_t TDVM::readByte() {
    TDFrame& frame = m_frames[m_frameCount - 1];
    return *frame.ip++;
}

int32_t TDVM::readInt() {
    TDFrame& frame = m_frames[m_frameCount - 1];
    int32_t val = (int32_t)frame.ip[0] |
                  ((int32_t)frame.ip[1] << 8) |
                  ((int32_t)frame.ip[2] << 16) |
                  ((int32_t)frame.ip[3] << 24);
    frame.ip += 4;
    return val;
}

float TDVM::readFloat() {
    TDFrame& frame = m_frames[m_frameCount - 1];
    float val;
    memcpy(&val, frame.ip, 4);
    frame.ip += 4;
    return val;
}

void TDVM::runtimeError(const char* msg) {
    m_hadError = true;
    m_running = false;
    int line = 0;
    if (m_frameCount > 0) {
        TDFrame& frame = m_frames[m_frameCount - 1];
        line = (int)(frame.ip - frame.function->bytecode);
    }
    snprintf(m_error, sizeof(m_error), "Runtime error (offset %d): %s", line, msg);
    TD_LOG_ERROR("%s", m_error);
}

bool TDVM::callValue(int argCount) {
    TDValue callee = pop();

    if (callee.type == TDValueType::Function) {
        int funcIdx = callee.functionIndex;
        if (funcIdx < 0 || funcIdx >= m_script->functionCount) {
            runtimeError("Invalid function index");
            return false;
        }
        CompiledFunction* func = m_script->functions[funcIdx];
        if (m_frameCount >= TD_VM_MAX_FRAMES) {
            runtimeError("Call stack overflow");
            return false;
        }
        TDFrame& frame = m_frames[m_frameCount++];
        frame.function = func;
        frame.ip = func->bytecode;
        frame.stackBase = m_stackTop - argCount;
        frame.localBase = m_stackTop - argCount;
        return true;
    }

    if (callee.type == TDValueType::String) {
        // Lookup function by name
        int funcIdx = m_script->findFunction(callee.stringValue);
        if (funcIdx >= 0) {
            CompiledFunction* func = m_script->functions[funcIdx];
            if (m_frameCount >= TD_VM_MAX_FRAMES) {
                runtimeError("Call stack overflow");
                return false;
            }
            TDFrame& frame = m_frames[m_frameCount++];
            frame.function = func;
            frame.ip = func->bytecode;
            frame.stackBase = m_stackTop - argCount;
            frame.localBase = m_stackTop - argCount;
            return true;
        }

        // Built-in function check
        if (strcmp(callee.stringValue, "print") == 0) {
            engineCall(0, argCount);
            return true;
        }
        if (strcmp(callee.stringValue, "sin") == 0) { engineCall(5, argCount); return true; }
        if (strcmp(callee.stringValue, "cos") == 0) { engineCall(6, argCount); return true; }
        if (strcmp(callee.stringValue, "sqrt") == 0) { engineCall(7, argCount); return true; }

        char err[128];
        snprintf(err, sizeof(err), "Undefined function: %s", callee.stringValue);
        runtimeError(err);
        return false;
    }

    runtimeError("Value is not callable");
    return false;
}

void TDVM::engineCall(int funcIndex, int argCount) {
    switch (funcIndex) {
        case 0: { // print
            if (argCount >= 1) {
                TDValue arg = pop();
                char buf[512];
                switch (arg.type) {
                    case TDValueType::Int:    snprintf(buf, sizeof(buf), "%d", arg.intValue); break;
                    case TDValueType::Float:  snprintf(buf, sizeof(buf), "%g", arg.floatValue); break;
                    case TDValueType::Bool:   snprintf(buf, sizeof(buf), "%s", arg.boolValue ? "true" : "false"); break;
                    case TDValueType::String: snprintf(buf, sizeof(buf), "%s", arg.stringValue ? arg.stringValue : "null"); break;
                    case TDValueType::Null:   snprintf(buf, sizeof(buf), "null"); break;
                    default:                  snprintf(buf, sizeof(buf), "<object>"); break;
                }
                if (m_printCallback) { m_printCallback(buf); }
                else { printf("%s\n", buf); }
            }
            push(TDValue::makeNull());
            break;
        }
        case 5: { // sin
            TDValue a = (argCount >= 1) ? pop() : TDValue::makeFloat(0);
            push(TDValue::makeFloat(sinf(a.toFloat())));
            break;
        }
        case 6: { // cos
            TDValue a = (argCount >= 1) ? pop() : TDValue::makeFloat(0);
            push(TDValue::makeFloat(cosf(a.toFloat())));
            break;
        }
        case 7: { // sqrt
            TDValue a = (argCount >= 1) ? pop() : TDValue::makeFloat(0);
            push(TDValue::makeFloat(sqrtf(a.toFloat())));
            break;
        }
        default:
            // Pop args and push null
            for (int i = 0; i < argCount; i++) pop();
            push(TDValue::makeNull());
            break;
    }
}

bool TDVM::executeInstruction() {
    OpCode op = (OpCode)readByte();
    TDFrame& frame = m_frames[m_frameCount - 1];

    switch (op) {
        case OpCode::PUSH_INT:   push(TDValue::makeInt(readInt())); break;
        case OpCode::PUSH_FLOAT: push(TDValue::makeFloat(readFloat())); break;
        case OpCode::PUSH_BOOL:  push(TDValue::makeBool(readByte() != 0)); break;
        case OpCode::PUSH_NULL:  push(TDValue::makeNull()); break;
        case OpCode::PUSH_STRING: {
            int idx = readInt();
            if (idx >= 0 && idx < m_script->stringCount)
                push(TDValue::makeString(m_script->stringTable[idx]));
            else
                push(TDValue::makeNull());
            break;
        }
        case OpCode::POP: pop(); break;
        case OpCode::DUP: push(peek()); break;

        case OpCode::ADD: {
            TDValue b = pop(), a = pop();
            if (a.type == TDValueType::Float || b.type == TDValueType::Float)
                push(TDValue::makeFloat(a.toFloat() + b.toFloat()));
            else
                push(TDValue::makeInt(a.toInt() + b.toInt()));
            break;
        }
        case OpCode::SUB: {
            TDValue b = pop(), a = pop();
            if (a.type == TDValueType::Float || b.type == TDValueType::Float)
                push(TDValue::makeFloat(a.toFloat() - b.toFloat()));
            else
                push(TDValue::makeInt(a.toInt() - b.toInt()));
            break;
        }
        case OpCode::MUL: {
            TDValue b = pop(), a = pop();
            if (a.type == TDValueType::Float || b.type == TDValueType::Float)
                push(TDValue::makeFloat(a.toFloat() * b.toFloat()));
            else
                push(TDValue::makeInt(a.toInt() * b.toInt()));
            break;
        }
        case OpCode::DIV: {
            TDValue b = pop(), a = pop();
            float bv = b.toFloat();
            if (bv == 0.0f) { runtimeError("Division by zero"); return false; }
            push(TDValue::makeFloat(a.toFloat() / bv));
            break;
        }
        case OpCode::MOD: {
            TDValue b = pop(), a = pop();
            int bv = b.toInt();
            if (bv == 0) { runtimeError("Modulo by zero"); return false; }
            push(TDValue::makeInt(a.toInt() % bv));
            break;
        }
        case OpCode::NEGATE: {
            TDValue a = pop();
            if (a.type == TDValueType::Float) push(TDValue::makeFloat(-a.floatValue));
            else push(TDValue::makeInt(-a.toInt()));
            break;
        }
        case OpCode::EQUAL:         { TDValue b = pop(), a = pop(); push(TDValue::makeBool(a.toFloat() == b.toFloat())); break; }
        case OpCode::NOT_EQUAL:     { TDValue b = pop(), a = pop(); push(TDValue::makeBool(a.toFloat() != b.toFloat())); break; }
        case OpCode::LESS:          { TDValue b = pop(), a = pop(); push(TDValue::makeBool(a.toFloat() < b.toFloat())); break; }
        case OpCode::LESS_EQUAL:    { TDValue b = pop(), a = pop(); push(TDValue::makeBool(a.toFloat() <= b.toFloat())); break; }
        case OpCode::GREATER:       { TDValue b = pop(), a = pop(); push(TDValue::makeBool(a.toFloat() > b.toFloat())); break; }
        case OpCode::GREATER_EQUAL: { TDValue b = pop(), a = pop(); push(TDValue::makeBool(a.toFloat() >= b.toFloat())); break; }
        case OpCode::NOT:           { TDValue a = pop(); push(TDValue::makeBool(!a.toBool())); break; }
        case OpCode::AND:           { TDValue b = pop(), a = pop(); push(TDValue::makeBool(a.toBool() && b.toBool())); break; }
        case OpCode::OR:            { TDValue b = pop(), a = pop(); push(TDValue::makeBool(a.toBool() || b.toBool())); break; }

        case OpCode::LOAD_LOCAL: {
            int idx = readInt();
            push(m_stack[frame.localBase + idx]);
            break;
        }
        case OpCode::STORE_LOCAL: {
            int idx = readInt();
            TDValue val = pop();
            if (frame.localBase + idx < TD_VM_MAX_STACK)
                m_stack[frame.localBase + idx] = val;
            break;
        }
        case OpCode::LOAD_GLOBAL:  { int idx = readInt(); push(m_globals[idx]); break; }
        case OpCode::STORE_GLOBAL: { int idx = readInt(); m_globals[idx] = pop(); break; }

        case OpCode::JUMP: {
            int32_t offset = readInt();
            frame.ip += offset;
            break;
        }
        case OpCode::JUMP_IF_FALSE: {
            int32_t offset = readInt();
            if (!peek().toBool()) frame.ip += offset;
            break;
        }
        case OpCode::JUMP_IF_TRUE: {
            int32_t offset = readInt();
            if (peek().toBool()) frame.ip += offset;
            break;
        }
        case OpCode::LOOP: {
            int32_t offset = readInt();
            frame.ip -= offset;
            break;
        }

        case OpCode::CALL: {
            int argCount = readInt();
            if (!callValue(argCount)) return false;
            break;
        }
        case OpCode::RETURN: {
            TDValue result = pop();
            m_frameCount--;
            if (m_frameCount == 0) { m_running = false; push(result); return true; }
            m_stackTop = frame.stackBase;
            push(result);
            break;
        }
        case OpCode::RETURN_VOID: {
            m_frameCount--;
            if (m_frameCount == 0) { m_running = false; return true; }
            m_stackTop = frame.stackBase;
            push(TDValue::makeNull());
            break;
        }

        case OpCode::CONST_STRING: {
            int idx = readInt();
            if (idx >= 0 && idx < m_script->stringCount)
                push(TDValue::makeString(m_script->stringTable[idx]));
            else
                push(TDValue::makeNull());
            break;
        }

        case OpCode::GET_FIELD:
        case OpCode::SET_FIELD:
        case OpCode::CREATE_STRUCT:
        case OpCode::LOAD_THIS:
        case OpCode::STORE_THIS:
            readInt(); // consume operand
            push(TDValue::makeNull());
            break;

        case OpCode::ENGINE_CALL: {
            int funcIdx = readInt();
            engineCall(funcIdx, 1);
            break;
        }

        case OpCode::HALT:
            m_running = false;
            return true;

        default:
            runtimeError("Unknown opcode");
            return false;
    }
    return true;
}

bool TDVM::run() {
    if (!m_script || m_script->functionCount == 0) {
        runtimeError("No functions to execute");
        return false;
    }

    // Find and call "main" function
    int mainIdx = m_script->findFunction("main");
    if (mainIdx < 0) mainIdx = 0;

    CompiledFunction* func = m_script->functions[mainIdx];
    TDFrame& frame = m_frames[0];
    frame.function = func;
    frame.ip = func->bytecode;
    frame.stackBase = 0;
    frame.localBase = 0;
    m_frameCount = 1;
    m_running = true;

    // Allocate space for locals
    m_stackTop = func->localCount;
    for (int i = 0; i < func->localCount; i++) {
        m_stack[i] = TDValue::makeNull();
    }

    while (m_running && !m_hadError) {
        TDFrame& currentFrame = m_frames[m_frameCount - 1];
        int offset = (int)(currentFrame.ip - currentFrame.function->bytecode);
        if (offset >= currentFrame.function->bytecodeSize) {
            m_running = false;
            break;
        }
        if (!executeInstruction()) break;
    }

    return !m_hadError;
}

TDValue TDVM::callFunction(const char* name, TDValue* args, int argCount) {
    int funcIdx = m_script->findFunction(name);
    if (funcIdx < 0) {
        runtimeError("Function not found");
        return TDValue::makeNull();
    }

    // Push arguments
    for (int i = 0; i < argCount; i++) {
        push(args[i]);
    }

    // Push function reference and call
    push(TDValue::makeFunc(funcIdx));
    if (!callValue(argCount)) {
        return TDValue::makeNull();
    }

    // Run until this frame returns
    m_running = true;
    while (m_running && !m_hadError && m_frameCount > 0) {
        TDFrame& currentFrame = m_frames[m_frameCount - 1];
        int offset = (int)(currentFrame.ip - currentFrame.function->bytecode);
        if (offset >= currentFrame.function->bytecodeSize) break;
        if (!executeInstruction()) break;
    }

    if (m_stackTop > 0) return pop();
    return TDValue::makeNull();
}

} // namespace td
