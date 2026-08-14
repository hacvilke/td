// =============================================================================
// TD Engine - ScriptVM internal API (Tier 1.3, wave1-scriptvm)
//
// This header exposes the internals of the custom "tdscript" VM that
// script_vm.cpp implements. It is NOT part of the public engine API; only
// tests and the ScriptVM implementation itself include it.
//
// The public API (script_vm.h) is intentionally minimal and frozen. This
// internal header is the escape hatch for:
//   - Tests that need to load a script from an in-memory string (no disk).
//   - Tests that need to inject engine dependencies (World, InputState,
//     TimeState) into the singleton VM.
//   - Tests that need to read a script's globals or call a script function
//     by name.
//   - The td.* library implementation, which constructs/inspects Values.
//
// Nothing in here is part of the stable engine ABI. It may change between
// releases without notice.
// =============================================================================
#pragma once

#include "script_vm.h"
#include "../ecs/world.h"
#include "../ecs/component.h"
#include "../ecs/system.h"
#include "../core/signal.h"
#include "../core/logger.h"
#include "../platform/platform.h"  // InputState, TimeState

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace td {
namespace script {

// ---------------------------------------------------------------------------
// Value — the tagged union used by the VM. Heap types (String, Table,
// Function) are held by std::shared_ptr so copy/move is cheap and lifetime
// is automatic.
// ---------------------------------------------------------------------------
struct Value {
    enum class Type : uint8_t { Nil, Bool, Number, String, Table, Function };

    Type type = Type::Nil;
    bool boolVal = false;
    double numVal = 0.0;
    std::shared_ptr<std::string> strVal;
    std::shared_ptr<struct Table> tblVal;
    std::shared_ptr<struct Function> fnVal;

    static Value makeNil() { Value v; return v; }
    static Value makeBool(bool b) { Value v; v.type = Type::Bool; v.boolVal = b; return v; }
    static Value makeNum(double n) { Value v; v.type = Type::Number; v.numVal = n; return v; }
    static Value makeStr(std::string s) {
        Value v; v.type = Type::String; v.strVal = std::make_shared<std::string>(std::move(s)); return v;
    }
    static Value makeTable();
    static Value makeFunc(std::shared_ptr<Function> f);

    bool isNil()      const { return type == Type::Nil; }
    bool isBool()     const { return type == Type::Bool; }
    bool isNumber()   const { return type == Type::Number; }
    bool isString()   const { return type == Type::String; }
    bool isTable()    const { return type == Type::Table; }
    bool isFunction() const { return type == Type::Function; }

    // Lua truthiness: only nil and false are falsy.
    bool truthy() const {
        if (type == Type::Nil) return false;
        if (type == Type::Bool && !boolVal) return false;
        return true;
    }

    std::string typeName() const;
    std::string toString() const;       // For print() / error messages.
    bool equals(const Value& o) const;
    bool lessThan(const Value& o) const;  // For < and std::map ordering.
    bool lessEqual(const Value& o) const;
};

// Ordering so Value can be a key in std::map (used by Table).
bool operator<(const Value& a, const Value& b);

// ---------------------------------------------------------------------------
// Table — Lua-style associative array. Backed by std::map<Value, Value>
// which gives O(log N) lookups but simple semantics (any Value can be a
// key, except Nil). The `#` operator scans for the first integer key N
// where t[N] is nil and returns N-1 (Lua's "border" definition).
// ---------------------------------------------------------------------------
struct Table {
    std::map<Value, Value> map;

    Value get(const Value& key) const {
        auto it = map.find(key);
        return it != map.end() ? it->second : Value::makeNil();
    }
    void set(const Value& key, Value val) {
        if (val.isNil()) map.erase(key);
        else map[key] = std::move(val);
    }
    int length() const;  // Lua `#t`
};

// ---------------------------------------------------------------------------
// Proto — a compiled function prototype. Owns its bytecode, constant pool,
// and nested child protos (for closures). Upvalues are described here so
// OP_CLOSURE can fill them at runtime.
// ---------------------------------------------------------------------------
struct Proto {
    std::vector<uint8_t> code;
    std::vector<Value> constants;
    std::vector<std::shared_ptr<Proto>> protos;   // nested function protos
    std::vector<std::string> paramNames;
    std::vector<std::pair<bool, int>> upvalues;   // (isLocal, idx) per upvalue
    int numParams  = 0;
    int numLocals  = 0;     // params + locals + temps (slots to allocate)
    bool isVariadic = false;
    std::string name;
};

// ---------------------------------------------------------------------------
// Function — either a script function (carries a Proto + captured upvalues)
// or a native C++ function (carries a std::function callable).
// ---------------------------------------------------------------------------
using NativeFn = std::function<std::vector<Value>(std::vector<Value>&)>;

struct Function {
    enum Kind : uint8_t { Script, Native };
    Kind kind = Script;
    std::shared_ptr<Proto> proto;
    std::vector<Value> upvalues;   // snapshot upvalues (see Known Limitations)
    NativeFn native;
    std::string name;
};

// ---------------------------------------------------------------------------
// Bytecode opcodes. Kept small (~32 ops). Args are 8/16-bit, read inline
// from the code stream. See script_vm.cpp for semantics.
// ---------------------------------------------------------------------------
enum class Op : uint8_t {
    Nil, True, False,
    Const,         // A: const idx (16-bit)         -> push constants[A]
    LoadLocal,     // A: slot (16-bit)              -> push locals[A]
    StoreLocal,    // A: slot (16-bit)              -> pop; locals[A] = v
    LoadGlobal,    // A: const idx (16-bit, name)   -> push globals[constants[A]]
    StoreGlobal,   // A: const idx (16-bit, name)   -> pop; globals[name] = v
    LoadUpval,     // A: upval idx (8-bit)          -> push upvalues[A]
    StoreUpval,    // A: upval idx (8-bit)          -> pop; upvalues[A] = v
    GetField,      // A: const idx (16-bit, name)   -> pop obj; push obj.name
    SetField,      // A: const idx (16-bit, name)   -> pop val, obj; obj.name = val
    GetIndex,      // -> pop key, obj; push obj[key]
    SetIndex,      // -> pop val, key, obj; obj[key] = val
    NewTable,      // -> push empty table
    SetFieldKeep,  // A: const idx; pop val; peek obj; obj.name = val (table ctor)
    SetIndexKeep,  // pop val, key; peek obj; obj[key] = val (table ctor)
    Add, Sub, Mul, Div, Mod, Pow,
    Neg, Not, Len,
    Concat,
    Eq, Ne, Lt, Le, Gt, Ge,
    Jump,             // A: signed 16-bit offset
    JumpIfFalse,      // A: signed 16-bit offset; pop; if falsy jump
    JumpIfTrue,       // A: signed 16-bit offset; pop; if truthy jump
    JumpIfFalseKeep,  // A: signed 16-bit offset; peek (no pop); if falsy jump
    JumpIfTrueKeep,   // A: signed 16-bit offset; peek (no pop); if truthy jump
    Pop,              // A: count (8-bit); pop A values
    Dup,              // duplicate top of stack
    Call,             // A: nargs (8-bit), B: nresults (8-bit, 0xFF = all)
    CallTail,         // A: nresults (8-bit); nargs = stack.count above function
    Closure,          // A: proto idx (16-bit) -> push Function with upvalues filled
    LoadVararg,       // A: nresults (8-bit, 0xFF = all as table)
    AppendTable,      // pop value; peek table below; t[#t+1] = value
    Return,           // A: nret (8-bit)
    // (Break is compiled as Jump to loop end; no separate op.)
};

// ---------------------------------------------------------------------------
// ScriptEnv — per-script state. Each loaded script gets its own env with
// its own globals (sandboxing) and its own compiled Proto. Reload swaps
// the Proto and resets globals; existing signal subscriptions survive
// because they reference the env by handle.
// ---------------------------------------------------------------------------
struct ScriptEnv {
    int id = -1;
    std::string name;
    std::string path;
    std::string source;
    std::shared_ptr<Proto> mainProto;
    std::map<std::string, Value> globals;
    int64_t mtime = 0;        // file mtime at last load (0 for source-only)
    bool fromSource = false;  // true if loaded via loadScriptFromSource (no file)
    std::vector<SubscriptionHandle> subscriptions;
};

// ---------------------------------------------------------------------------
// Test / internal API. These free functions live in script_vm.cpp and
// operate on the ScriptVM singleton. Tests include this header and call
// them; the public ScriptVM class API (script_vm.h) stays untouched.
//
// All functions are safe to call only from the main thread (the engine is
// single-threaded today; the VM is reentrant across scripts but the
// dependency pointers — World*, InputState*, TimeState* — are shared).
// ---------------------------------------------------------------------------

// Load (compile + execute the chunk) from an in-memory source string.
// Returns { -1 } on failure (error logged). The script's globals persist
// in the VM after this returns.
ScriptHandle loadScriptFromSource(ScriptVM& vm, const char* src, const char* name);

// Inject engine dependencies. The VM does NOT own these pointers; the
// caller must keep them alive for the VM's lifetime (or until reset).
void setWorld(ScriptVM& vm, World* w);
void setInputState(ScriptVM& vm, InputState* s);
void setTimeState(ScriptVM& vm, TimeState* t);

// Read a global from a loaded script. Returns Nil if the script or global
// doesn't exist.
Value getScriptGlobal(ScriptHandle h, const char* name);

// Call a named function in a loaded script. Returns the function's return
// values (a vector; empty if the script/function doesn't exist).
std::vector<Value> callScriptFunction(ScriptHandle h, const char* name,
                                      std::vector<Value> args);

// True if the script has a function with the given name.
bool scriptHasFunction(ScriptHandle h, const char* name);

// Direct execution of a chunk with no script-env registration. Returns
// the chunk's return values. Used by tests that want to evaluate an
// expression without polluting the global script registry.
std::vector<Value> evalSource(ScriptVM& vm, const char* src,
                              std::vector<Value> args);

// Set a global on a loaded script (for tests that need to seed state).
void setScriptGlobal(ScriptHandle h, const char* name, Value v);

} // namespace script
} // namespace td
