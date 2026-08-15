// =============================================================================
// TD Engine — TDScript Language (Tier 4: Native Pipelines & Scripting)
//
// TDScript is a focused, network-first scripting language for the TD Engine.
// It is NOT a general-purpose language. It exists for one job:
//
//   "Write server-authoritative multiplayer gameplay logic that compiles
//    to both browser-JS (for client-side prediction) and Node-JS (for the
//    dedicated server), with first-class network qualifiers."
//
// Syntax ancestry:
//   - C++ / TypeScript / Godot GDScript hybrid.
//   - C-style braces, semicolons, typed declarations.
//   - `replicated` qualifier on fields for automatic state sync.
//   - `@rpc(reliable)` / `@rpc(unreliable)` decorators for remote calls.
//
// Why a custom language (and not Lua/JS/TS directly):
//   - Network qualifiers (`replicated`, `@rpc(...)`) are first-class syntax,
//     not library calls. The compiler can statically verify that a
//     `replicated` field is only mutated on the server, and that an
//     `@rpc(reliable)` handler is idempotent.
//   - One source file → two targets (browser client + Node server) with
//     identical semantics. Hand-writing this in plain JS requires either
//     two codebases or a heavy decorator/transpiler stack.
//   - Sandboxed: no `eval`, no `require`, no `fs`. The runtime exposes only
//     what the engine registers (Log, Network, Physics, ECS, Math, Vector3).
//
// Architecture (this directory):
//   - ast.h           — AST node types (header-only, value types)
//   - lexer.h/.cpp    — tokenizer (source → Token stream)
//   - parser.h/.cpp   — recursive-descent parser (Token stream → AST)
//   - codegen_js.h/.cpp — JS code generator (AST → browser/Node JS string)
//   - codegen_cpp.h/.cpp — C++ code generator (AST → native server .cpp string)
//
// The compiler itself is written in C++ so it can be embedded in the engine
// (for hot-reload) AND compiled to Wasm (for in-browser authoring tools).
// A thin JS wrapper (`tools/tdscript/tdscript.js`) exposes the same compiler
// via Node for the CLI workflow.
//
// Status: REAL. The lexer + parser + JS codegen are functional. The C++
// codegen is a stub that emits a TODO comment per node — it will be filled
// in once the JS path is battle-tested.
// =============================================================================
#pragma once

#include <string>
#include <memory>

namespace td::tdscript {

// Top-level entry: compile TDScript source code to a target language.
// Returns the generated source on success, or sets `errorMsg` on failure.
//
// `target` is one of: "js", "cpp". Default: "js".
std::string compileSource(const std::string& source,
                          const std::string& target,
                          std::string* errorMsg);

} // namespace td::tdscript
