#pragma once
#include "compiler.h"
#include "../ecs/world.h"

namespace td {

enum class TDValueType : uint8_t {
    Int, Float, String, Bool, Null, Object, Function, EntityRef
};

struct TDValue {
    TDValueType type = TDValueType::Null;
    union {
        int32_t intValue;
        float floatValue;
        bool boolValue;
        const char* stringValue;
        struct { int structDefIndex; TDValue* fields; int fieldCount; } object;
        int functionIndex;
        uint32_t entityRef;
    };

    static TDValue makeInt(int32_t v)       { TDValue r; r.type = TDValueType::Int;   r.intValue = v; return r; }
    static TDValue makeFloat(float v)       { TDValue r; r.type = TDValueType::Float; r.floatValue = v; return r; }
    static TDValue makeBool(bool v)         { TDValue r; r.type = TDValueType::Bool;  r.boolValue = v; return r; }
    static TDValue makeNull()              { TDValue r; r.type = TDValueType::Null;  return r; }
    static TDValue makeString(const char* v){ TDValue r; r.type = TDValueType::String; r.stringValue = v; return r; }
    static TDValue makeFunc(int idx)        { TDValue r; r.type = TDValueType::Function; r.functionIndex = idx; return r; }

    float toFloat() const;
    int32_t toInt() const;
    bool toBool() const;
};

struct TDFrame {
    CompiledFunction* function;
    uint8_t* ip;
    int stackBase;
    int localBase;
};

#define TD_VM_MAX_STACK 1024
#define TD_VM_MAX_FRAMES 64

class TDVM {
public:
    bool load(CompiledScript* script);
    bool run();
    TDValue callFunction(const char* name, TDValue* args = nullptr, int argCount = 0);
    void setWorld(World* world) { m_world = world; }
    void setPrintCallback(void(*callback)(const char*)) { m_printCallback = callback; }
    const char* getError() const { return m_error; }
    bool hasError() const { return m_hadError; }
    void reset();

private:
    void push(TDValue value);
    TDValue pop();
    TDValue peek(int distance = 0);
    bool callValue(int argCount);

    uint8_t readByte();
    int32_t readInt();
    float readFloat();

    void runtimeError(const char* msg);
    bool executeInstruction();

    void engineCall(int funcIndex, int argCount);

    CompiledScript* m_script = nullptr;
    TDValue m_stack[TD_VM_MAX_STACK];
    int m_stackTop = 0;
    TDFrame m_frames[TD_VM_MAX_FRAMES];
    int m_frameCount = 0;
    TDValue m_globals[256];
    World* m_world = nullptr;
    void(*m_printCallback)(const char*) = nullptr;
    char m_error[256] = {};
    bool m_hadError = false;
    bool m_running = false;
};

} // namespace td
