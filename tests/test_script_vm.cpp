// =============================================================================
// TD Engine - ScriptVM tests (wave1-scriptvm)
//
// Tests the real "tdscript" custom Lua-like VM: lexer, parser, compiler,
// interpreter, and the td.* engine library. Loads scripts from in-memory
// source strings (no disk I/O) so the test is hermetic.
//
// Build:
//   g++ -std=c++17 -Wall -Wextra -O2 -Isrc
//       tests/test_script_vm.cpp
//       src/scripting/script_vm.cpp
//       src/ecs/world.cpp src/ecs/entity.cpp src/ecs/beat_system.cpp
//       src/physics/aabb.cpp src/physics/collision.cpp
//       tests/stub_logger.cpp
//       -o build/test_script_vm -lpthread
// =============================================================================
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <memory>
#include <cmath>

#include "../src/scripting/script_vm.h"
#include "../src/scripting/script_vm_internal.h"
#include "../src/ecs/world.h"

using td::script::Value;
using td::script::evalSource;
using td::script::loadScriptFromSource;
using td::script::callScriptFunction;
using td::script::getScriptGlobal;
using td::script::setScriptGlobal;
using td::script::scriptHasFunction;
using td::script::setWorld;
using td::script::setInputState;
using td::script::setTimeState;
using td::ScriptHandle;
using td::ScriptVM;
using td::World;

static int g_pass = 0, g_fail = 0;

#define CHECK(cond, ...) do { \
    if (cond) { g_pass++; std::fprintf(stderr, "PASS: " __VA_ARGS__); std::fprintf(stderr, "\n"); } \
    else { g_fail++; std::fprintf(stderr, "FAIL: " __VA_ARGS__); std::fprintf(stderr, "\n"); } \
} while (0)

#define EXPECT_NUM(val, expected) \
    ((val).type == Value::Type::Number && std::abs((val).numVal - (expected)) < 1e-6)

int main() {
    std::fprintf(stderr, "==========================================\n");
    std::fprintf(stderr, " ScriptVM (tdscript) Tests\n");
    std::fprintf(stderr, "==========================================\n");
    std::fflush(stderr);

    ScriptVM& vm = ScriptVM::get();
    vm.init();

    // --- Test 1: Arithmetic expressions -----------------------------------
    std::fprintf(stderr, "\n--- Test 1: Arithmetic ---\n");
    {
        auto r = evalSource(vm, "return 2 + 3 * 4", {});
        CHECK(r.size() == 1, "return 2 + 3 * 4 yields 1 value");
        CHECK(EXPECT_NUM(r[0], 14.0), "2 + 3 * 4 = 14 (got %g)", r[0].numVal);
    }
    {
        auto r = evalSource(vm, "return (2 + 3) * 4", {});
        CHECK(EXPECT_NUM(r[0], 20.0), "(2 + 3) * 4 = 20");
    }
    {
        auto r = evalSource(vm, "return 7 % 3", {});
        CHECK(EXPECT_NUM(r[0], 1.0), "7 %% 3 = 1");
    }
    {
        auto r = evalSource(vm, "return 2 ^ 10", {});
        CHECK(EXPECT_NUM(r[0], 1024.0), "2 ^ 10 = 1024");
    }
    {
        auto r = evalSource(vm, "return -5 + 8", {});
        CHECK(EXPECT_NUM(r[0], 3.0), "-5 + 8 = 3");
    }

    // --- Test 2: Strings --------------------------------------------------
    std::fprintf(stderr, "\n--- Test 2: Strings ---\n");
    {
        auto r = evalSource(vm, "return 'hello' .. ' ' .. 'world'", {});
        CHECK(r.size() == 1 && r[0].type == Value::Type::String,
              "concat yields string");
        CHECK(*r[0].strVal == "hello world",
              "string concat = 'hello world'");
    }
    {
        auto r = evalSource(vm, "return #'tdengine'", {});
        CHECK(EXPECT_NUM(r[0], 8.0), "#'tdengine' = 8");
    }

    // --- Test 3: Booleans + control flow ---------------------------------
    std::fprintf(stderr, "\n--- Test 3: Booleans + control flow ---\n");
    {
        auto r = evalSource(vm, "return 5 > 3 and 'yes' or 'no'", {});
        CHECK(*r[0].strVal == "yes", "5 > 3 and 'yes' or 'no' = 'yes'");
    }
    {
        auto r = evalSource(vm,
            "local x = 10\n"
            "if x > 5 then return 'big' else return 'small' end", {});
        CHECK(*r[0].strVal == "big", "if x>5 returns 'big'");
    }
    {
        auto r = evalSource(vm,
            "local sum = 0\n"
            "for i = 1, 10 do sum = sum + i end\n"
            "return sum", {});
        CHECK(EXPECT_NUM(r[0], 55.0), "sum 1..10 = 55");
    }
    {
        auto r = evalSource(vm,
            "local i = 0\n"
            "local n = 0\n"
            "while i < 5 do i = i + 1; n = n + i end\n"
            "return n", {});
        CHECK(EXPECT_NUM(r[0], 15.0), "while-loop sum 1..5 = 15");
    }

    // --- Test 4: Tables ---------------------------------------------------
    std::fprintf(stderr, "\n--- Test 4: Tables ---\n");
    {
        auto r = evalSource(vm,
            "local t = {1, 2, 3, 4, 5}\n"
            "local sum = 0\n"
            "for i, v in ipairs(t) do sum = sum + v end\n"
            "return sum", {});
        CHECK(EXPECT_NUM(r[0], 15.0), "ipairs sum of {1..5} = 15");
    }
    {
        auto r = evalSource(vm,
            "local t = {a=10, b=20, c=30}\n"
            "return t.b", {});
        CHECK(EXPECT_NUM(r[0], 20.0), "t.b = 20");
    }
    {
        auto r = evalSource(vm,
            "local t = {}\n"
            "t.x = 100\n"
            "t.y = 200\n"
            "return t.x + t.y", {});
        CHECK(EXPECT_NUM(r[0], 300.0), "t.x + t.y = 300");
    }

    // --- Test 5: Functions + closures ------------------------------------
    std::fprintf(stderr, "\n--- Test 5: Functions ---\n");
    {
        auto r = evalSource(vm,
            "local function add(a, b) return a + b end\n"
            "return add(3, 4)", {});
        CHECK(EXPECT_NUM(r[0], 7.0), "add(3, 4) = 7");
    }
    {
        // Recursion via GLOBAL function (snapshot upvalues don't support
        // local-function self-reference — see script_vm.cpp limitations).
        auto r = evalSource(vm,
            "function fact(n)\n"
            "  if n <= 1 then return 1 end\n"
            "  return n * fact(n - 1)\n"
            "end\n"
            "return fact(5)", {});
        CHECK(EXPECT_NUM(r[0], 120.0), "fact(5) = 120 (global recursion)");
    }
    {
        // Closures execute and return a value (snapshot upvalue semantics
        // are documented as a known limitation — counter state may not
        // accumulate as in real Lua).
        auto r = evalSource(vm,
            "local function make_greeting(prefix)\n"
            "  return function(name) return prefix .. ' ' .. name end\n"
            "end\n"
            "local greet = make_greeting('Hello')\n"
            "return greet('World')", {});
        CHECK(r.size() == 1 && r[0].type == Value::Type::String,
              "closure returns a string");
        if (r[0].type == Value::Type::String) {
            CHECK(*r[0].strVal == "Hello World",
                  "greet('World') = 'Hello World' (snapshot upvalue works for read-only)");
        }
    }

    // --- Test 6: Loading a script + calling its functions ----------------
    std::fprintf(stderr, "\n--- Test 6: Script load + call ---\n");
    {
        // Use a GLOBAL for hp so all functions see mutations (top-level
        // locals in tdscript are scoped to the chunk's main function and
        // not shared across separately-compiled closures).
        const char* src =
            "hp = 100\n"
            "function init() hp = 100 end\n"
            "function damage(n) hp = hp - n; return hp end\n"
            "function get_hp() return hp end\n";
        ScriptHandle h = loadScriptFromSource(vm, src, "test_script");
        CHECK(h.valid(), "loadScriptFromSource returned valid handle");
        CHECK(scriptHasFunction(h, "damage"),
              "script has 'damage' function");

        auto r = callScriptFunction(h, "damage", {Value::makeNum(30.0)});
        CHECK(r.size() == 1 && EXPECT_NUM(r[0], 70.0),
              "damage(30) returns 70 hp");

        auto r2 = callScriptFunction(h, "get_hp", {});
        CHECK(r2.size() == 1 && EXPECT_NUM(r2[0], 70.0),
              "get_hp() returns 70 after damage");

        vm.unloadScript(h);
    }

    // --- Test 7: td.* library --------------------------------------------
    std::fprintf(stderr, "\n--- Test 7: td.* library ---\n");
    {
        // World is ~8MB so heap-allocate it.
        std::unique_ptr<World> world(new World());
        setWorld(vm, world.get());

        const char* src =
            "function test_create()\n"
            "  local id = td.create_entity('ScriptEntity')\n"
            "  td.set_position(id, 42, 99)\n"
            "  return id\n"
            "end\n"
            "function test_query()\n"
            "  local id = td.find_by_name('ScriptEntity')\n"
            "  if id == nil then return -1 end\n"
            "  return id\n"
            "end\n";
        ScriptHandle h = loadScriptFromSource(vm, src, "tdlib_test");
        CHECK(h.valid(), "tdlib script loaded");

        auto r = callScriptFunction(h, "test_create", {});
        CHECK(r.size() == 1 && r[0].type == Value::Type::Number,
              "td.create_entity returns a number id");
        td::EntityId createdId = (td::EntityId)r[0].numVal;
        CHECK(createdId > 0, "entity id is positive");

        auto r2 = callScriptFunction(h, "test_query", {});
        CHECK(r2.size() == 1 && r2[0].type == Value::Type::Number,
              "td.find_by_name returns a number");
        CHECK((td::EntityId)r2[0].numVal == createdId,
              "find_by_name returns the same id we created");

        // Verify the position was set on the actual World.
        auto* pos = world->getComponent<td::PositionComponent>(createdId);
        CHECK(pos != nullptr, "PositionComponent exists on entity");
        if (pos) {
            CHECK(std::abs(pos->x - 42.0f) < 0.001f && std::abs(pos->y - 99.0f) < 0.001f,
                  "entity position is (42, 99) as set via td.set_position");
        }

        vm.unloadScript(h);
    }

    // --- Test 8: Hot reload preserves handle -----------------------------
    std::fprintf(stderr, "\n--- Test 8: Hot reload preserves handle ---\n");
    {
        const char* src1 = "function v() return 1 end";
        ScriptHandle h1 = loadScriptFromSource(vm, src1, "reload_test");
        CHECK(h1.valid(), "first load valid");

        auto r1 = callScriptFunction(h1, "v", {});
        CHECK(EXPECT_NUM(r1[0], 1.0), "first version returns 1");

        vm.unloadScript(h1);
        const char* src2 = "function v() return 2 end";
        ScriptHandle h2 = loadScriptFromSource(vm, src2, "reload_test");
        CHECK(h2.valid(), "second load valid");

        auto r2 = callScriptFunction(h2, "v", {});
        CHECK(EXPECT_NUM(r2[0], 2.0), "second version returns 2");

        vm.unloadScript(h2);
    }

    // --- Test 9: Error handling ------------------------------------------
    std::fprintf(stderr, "\n--- Test 9: Error handling ---\n");
    {
        // Syntax error should not crash; loadScriptFromSource returns invalid.
        ScriptHandle h = loadScriptFromSource(vm,
            "local x = \n"
            "return x",
            "syntax_err");
        CHECK(!h.valid(), "syntax error returns invalid handle");

        // Runtime error inside a script is caught by the VM.
        ScriptHandle h2 = loadScriptFromSource(vm,
            "function boom() return undefined_global.field end",
            "runtime_err");
        CHECK(h2.valid(), "script with runtime error still loads");
        auto r = callScriptFunction(h2, "boom", {});
        CHECK(r.empty() || r[0].type == Value::Type::Nil,
              "runtime error caught, returns nil/empty");
        vm.unloadScript(h2);
    }

    // --- Test 10: Math library ------------------------------------------
    std::fprintf(stderr, "\n--- Test 10: math library ---\n");
    {
        auto r = evalSource(vm, "return math.floor(3.7)", {});
        CHECK(EXPECT_NUM(r[0], 3.0), "math.floor(3.7) = 3");

        auto r2 = evalSource(vm, "return math.abs(-42)", {});
        CHECK(EXPECT_NUM(r2[0], 42.0), "math.abs(-42) = 42");

        auto r3 = evalSource(vm, "return math.max(1, 5, 3, 2)", {});
        CHECK(EXPECT_NUM(r3[0], 5.0), "math.max(1,5,3,2) = 5");

        auto r4 = evalSource(vm, "return math.min(7, 2, 9)", {});
        CHECK(EXPECT_NUM(r4[0], 2.0), "math.min(7,2,9) = 2");
    }

    vm.shutdown();

    std::fprintf(stderr, "\n==========================================\n");
    std::fprintf(stderr, " ScriptVM Tests: %d passed, %d failed\n", g_pass, g_fail);
    std::fprintf(stderr, "==========================================\n");
    return g_fail == 0 ? 0 : 1;
}
