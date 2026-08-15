// =============================================================================
// TD Engine — TDScript C++ Compiler Smoke Test
// File: tests/test_tdscript_compiler.cpp
//
// Verifies that the C++ TDScript compiler (lexer + parser + JS codegen)
// produces the same output as the JS implementation. Run via `make test`.
//
// Build: g++ -std=c++17 -Isrc tests/test_tdscript_compiler.cpp
//        src/scripting/tdscript/lexer.cpp src/scripting/tdscript/parser.cpp
//        src/scripting/tdscript/codegen_js.cpp src/scripting/tdscript/tdscript.cpp
//        -o test_tdscript_compiler && ./test_tdscript_compiler
// =============================================================================

#include "scripting/tdscript/tdscript.h"
#include <cassert>
#include <iostream>
#include <string>

static int pass = 0, fail = 0;

#define CHECK(name, cond) do { \
    if (cond) { pass++; std::cout << "  ok  " << name << "\n"; } \
    else { fail++; std::cout << "FAIL  " << name << " (" #cond ")\n"; } \
} while (0)

int main() {
    using namespace td::tdscript;

    std::cout << "--- TDScript C++ Compiler ---\n";

    // Test 1: empty source compiles
    {
        std::string err;
        std::string out = compileSource("", "js", &err);
        CHECK("empty source: compiles without error", err.empty());
        CHECK("empty source: emits header comment", out.find("Auto-generated") != std::string::npos);
        CHECK("empty source: emits 'use strict'", out.find("'use strict';") != std::string::npos);
    }

    // Test 2: import statement
    {
        std::string src = "import \"engine/networking\";";
        std::string err;
        std::string out = compileSource(src, "js", &err);
        CHECK("import: no error", err.empty());
        CHECK("import: emits require", out.find("require('engine/networking')") != std::string::npos);
    }

    // Test 3: struct with fields
    {
        std::string src = "struct Point { int32 x; int32 y = 5; }";
        std::string err;
        std::string out = compileSource(src, "js", &err);
        CHECK("struct: no error", err.empty());
        CHECK("struct: emits class", out.find("class Point") != std::string::npos);
        CHECK("struct: emits default init", out.find("this.x = 0") != std::string::npos);
        CHECK("struct: emits explicit init", out.find("this.y = 5") != std::string::npos);
    }

    // Test 4: class with replicated field
    {
        std::string src = "class C { replicated int32 hp = 100; }";
        std::string err;
        std::string out = compileSource(src, "js", &err);
        CHECK("replicated field: no error", err.empty());
        CHECK("replicated field: emits backing field", out.find("this._hp = 100") != std::string::npos);
        CHECK("replicated field: emits register call", out.find("__td_repl_register") != std::string::npos);
    }

    // Test 5: RPC decorator
    {
        std::string src = "class C { @rpc(reliable) public void foo(int32 a) { return; } }";
        std::string err;
        std::string out = compileSource(src, "js", &err);
        CHECK("rpc: no error", err.empty());
        CHECK("rpc: emits register call", out.find("__td_rpc_register('C', 'foo', 'reliable'") != std::string::npos);
    }

    // Test 6: parse error
    {
        std::string src = "struct A { int32 x }";  // missing semicolon
        std::string err;
        std::string out = compileSource(src, "js", &err);
        CHECK("parse error: reports error", !err.empty());
        CHECK("parse error: mentions semicolon", err.find("';'") != std::string::npos);
    }

    // Test 7: full server_main.td-style example
    {
        std::string src = R"TD(
            import "engine/networking";
            struct PlayerInput { uint32 entityId; float moveX; bool isJumping; }
            class ServerMain {
                replicated int32 playerHealth = 100;
                public void onServerStart() { Log.info("online"); }
                @rpc(reliable)
                public void processPlayerDamage(int32 dmg) {
                    this.playerHealth = this.playerHealth - dmg;
                }
            }
        )TD";
        std::string err;
        std::string out = compileSource(src, "js", &err);
        CHECK("full example: no error", err.empty());
        CHECK("full example: has struct class", out.find("class PlayerInput") != std::string::npos);
        CHECK("full example: has server class", out.find("class ServerMain") != std::string::npos);
        CHECK("full example: has onServerStart", out.find("onServerStart()") != std::string::npos);
        CHECK("full example: has rpc registration", out.find("__td_rpc_register('ServerMain', 'processPlayerDamage'") != std::string::npos);
    }

    // Test 8: custom-type var decl (Ident Ident pattern)
    {
        std::string src = "class C { public void f() { Vector3 v = Vector3(1, 2, 3); } }";
        std::string err;
        std::string out = compileSource(src, "js", &err);
        CHECK("custom-type var decl: no error", err.empty());
        CHECK("custom-type var decl: emits let", out.find("let v = Vector3(1, 2, 3)") != std::string::npos);
    }

    std::cout << "\n--- Summary ---\n";
    std::cout << "  pass: " << pass << "\n";
    std::cout << "  fail: " << fail << "\n";
    return fail > 0 ? 1 : 0;
}
