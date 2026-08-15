'use strict';

// =============================================================================
// TD Engine — TDScript Compiler Tests
// File: tests/tdscript/test_compiler.js
//
// Exercises the JS implementation of the TDScript compiler:
//   - Lexer: token kinds, comments, string escapes
//   - Parser: imports, structs, classes, fields, methods, RPCs, control flow
//   - Codegen: emitted JS is valid and runs against tdscript_runtime.js
//
// Run: node tests/tdscript/test_compiler.js
// =============================================================================

const path = require('path');
const assert = require('assert');
const { compile, tokenize, parse, generateJs, TK, NK } = require(path.join(__dirname, '..', '..', 'tools', 'tdscript', 'tdscript.js'));

let pass = 0, fail = 0;

function check(name, fn) {
  try { fn(); pass++; console.log('  ok  ' + name); }
  catch (e) { fail++; console.log('FAIL  ' + name + '\n      ' + e.message); }
}

// Helper: lex + parse in one step
function parseSrc(src) {
  const { tokens } = tokenize(src);
  return parse(tokens);
}

// Reset runtime globals between sub-tests that run generated code
function resetRuntime() {
  // Load the runtime if not already loaded
  const runtimePath = path.join(__dirname, '..', '..', 'web', 'tdscript_runtime.js');
  delete require.cache[require.resolve(runtimePath)];
  // Remove any classes left over from previous tests
  const toDelete = Object.keys(global).filter(k => /^[A-Z]/.test(k) && typeof global[k] === 'function');
  toDelete.forEach(k => { if (k !== 'Math' && k !== 'Vector3') delete global[k]; });
  // Also remove TD runtime globals so the runtime re-installs them fresh
  delete global.Vector3;
  delete global.Log;
  delete global.Physics;
  delete global.Network;
  delete global.__td_rpc_register;
  delete global.__td_repl_register;
  delete global.__td_script_main;
  delete global.TDScriptRuntime;
  require(runtimePath);
}

// =============================================================================
// Lexer tests
// =============================================================================
console.log('\n--- Lexer ---');

check('tokenizes import statement', () => {
  const { tokens } = tokenize('import "engine/networking";');
  assert.strictEqual(tokens[0].kind, TK.KwImport);
  assert.strictEqual(tokens[1].kind, TK.StringLit);
  assert.strictEqual(tokens[1].text, 'engine/networking');
  assert.strictEqual(tokens[2].kind, TK.Semicolon);
});

check('tokenizes struct with field', () => {
  const { tokens } = tokenize('struct Vec { int32 x; }');
  assert.strictEqual(tokens[0].kind, TK.KwStruct);
  assert.strictEqual(tokens[1].kind, TK.Ident);
  assert.strictEqual(tokens[1].text, 'Vec');
  assert.strictEqual(tokens[2].kind, TK.LBrace);
  assert.strictEqual(tokens[3].kind, TK.KwInt32);
  assert.strictEqual(tokens[4].kind, TK.Ident);
  assert.strictEqual(tokens[5].kind, TK.Semicolon);
  assert.strictEqual(tokens[6].kind, TK.RBrace);
});

check('tokenizes @rpc decorator', () => {
  const { tokens } = tokenize('@rpc(reliable)');
  assert.strictEqual(tokens[0].kind, TK.At);
  assert.strictEqual(tokens[1].kind, TK.Ident);
  assert.strictEqual(tokens[1].text, 'rpc');
  assert.strictEqual(tokens[2].kind, TK.LParen);
  assert.strictEqual(tokens[3].kind, TK.Ident);
  assert.strictEqual(tokens[3].text, 'reliable');
  assert.strictEqual(tokens[4].kind, TK.RParen);
});

check('handles line + block comments', () => {
  const { tokens } = tokenize('// line comment\nstruct A {} /* block */');
  assert.strictEqual(tokens[0].kind, TK.KwStruct);
  assert.strictEqual(tokens[1].text, 'A');
});

check('string escapes', () => {
  const { tokens } = tokenize('"a\\nb\\"c"');
  assert.strictEqual(tokens[0].text, 'a\nb"c');
});

check('float with f suffix', () => {
  const { tokens } = tokenize('3.14f');
  assert.strictEqual(tokens[0].kind, TK.FloatLit);
  assert.strictEqual(tokens[0].text, '3.14');
});

// =============================================================================
// Parser tests
// =============================================================================
console.log('\n--- Parser ---');

check('parses import', () => {
  const { module, errors } = parseSrc('import "engine/foo";');
  assert.strictEqual(errors.length, 0);
  assert.strictEqual(module.children[0].kind, NK.ImportStmt);
  assert.strictEqual(module.children[0].text, 'engine/foo');
});

check('parses struct with fields', () => {
  const src = 'struct PlayerInput { uint32 entityId; float moveX; bool isJumping; }';
  const { module, errors } = parseSrc(src);
  assert.strictEqual(errors.length, 0);
  const s = module.children[0];
  assert.strictEqual(s.kind, NK.StructDecl);
  assert.strictEqual(s.text, 'PlayerInput');
  assert.strictEqual(s.children.length, 3);
  assert.strictEqual(s.children[0].text, 'entityId');
  assert.strictEqual(s.children[0].type.text, 'uint32');
  assert.strictEqual(s.children[2].text, 'isJumping');
  assert.strictEqual(s.children[2].type.text, 'bool');
});

check('parses class with replicated field', () => {
  const src = 'class ServerPlayer { replicated int32 playerHealth; }';
  const { module, errors } = parseSrc(src);
  assert.strictEqual(errors.length, 0);
  const c = module.children[0];
  assert.strictEqual(c.kind, NK.ClassDecl);
  assert.strictEqual(c.text, 'ServerPlayer');
  assert.strictEqual(c.children[0].isReplicated, true);
});

check('parses @rpc(reliable) method', () => {
  const src = 'class H { @rpc(reliable) public void processPlayerDamage(int32 dmg) { return; } }';
  const { module, errors } = parseSrc(src);
  assert.strictEqual(errors.length, 0);
  const m = module.children[0].children[0];
  assert.strictEqual(m.kind, NK.MethodDecl);
  assert.strictEqual(m.rpcMode, 'reliable');
  assert.strictEqual(m.visibility, 'public');
  assert.strictEqual(m.text, 'processPlayerDamage');
  assert.strictEqual(m.type.text, 'void');
  assert.strictEqual(m.children.length, 1);  // one param
  assert.strictEqual(m.children[0].text, 'dmg');
});

check('parses if/else', () => {
  const src = 'class C { public void f() { if (true) { return; } else { return; } } }';
  const { module, errors } = parseSrc(src);
  assert.strictEqual(errors.length, 0);
  const m = module.children[0].children[0];
  const body = m.body;
  const ifStmt = body.children[0];
  assert.strictEqual(ifStmt.kind, NK.IfStmt);
  assert.ok(ifStmt.thenBranch);
  assert.ok(ifStmt.elseBranch);
});

check('parses for loop', () => {
  const src = 'class C { public void f() { for (int32 i = 0; i < 10; i = i + 1) { } } }';
  const { module, errors } = parseSrc(src);
  assert.strictEqual(errors.length, 0);
  const m = module.children[0].children[0];
  const forStmt = m.body.children[0];
  assert.strictEqual(forStmt.kind, NK.ForStmt);
  assert.strictEqual(forStmt.lhs.text, 'i');
  assert.ok(forStmt.cond);
  assert.ok(forStmt.step);
});

check('parses binary expression precedence', () => {
  const src = 'class C { public void f() { int32 x = 1 + 2 * 3; } }';
  const { module, errors } = parseSrc(src);
  assert.strictEqual(errors.length, 0);
  const decl = module.children[0].children[0].body.children[0];
  // 1 + (2 * 3) — top-level binary op should be '+'
  assert.strictEqual(decl.initExpr.kind, NK.Binary);
  assert.strictEqual(decl.initExpr.text, '+');
  assert.strictEqual(decl.initExpr.rhs.text, '*');  // right side is multiplication
});

check('emits diagnostic on missing semicolon', () => {
  const src = 'struct A { int32 x }';
  const { errors } = parseSrc(src);
  assert.ok(errors.length > 0);
  assert.ok(/expected ';'/.test(errors[0].message));
});

// =============================================================================
// Codegen tests
// =============================================================================
console.log('\n--- Codegen ---');

check('emits valid JS for simple struct', () => {
  const src = 'struct Point { int32 x; int32 y = 5; }';
  const { ok, code, error } = compile(src, 'js');
  assert.ok(ok, error);
  assert.ok(/global\.Point = class Point/.test(code));
  assert.ok(/this\.x = 0/.test(code));
  assert.ok(/this\.y = 5/.test(code));
});

check('emits class with method', () => {
  const src = 'class C { public int32 f(int32 a) { return a; } }';
  const { ok, code } = compile(src, 'js');
  assert.ok(ok);
  assert.ok(/global\.C = class C/.test(code));
  assert.ok(/f\(a\) {/.test(code));
  assert.ok(/return a;/.test(code));
});

check('emits replicated field with backing field', () => {
  const src = 'class C { replicated int32 hp = 100; }';
  const { ok, code } = compile(src, 'js');
  assert.ok(ok);
  assert.ok(/this\._hp = 100;/.test(code), 'expected backing field _hp');
  assert.ok(/__td_repl_register\('C', \["hp"\]\)/.test(code), 'expected repl register call');
});

check('emits RPC registration call', () => {
  const src = 'class C { @rpc(reliable) public void foo(int32 a) { return; } }';
  const { ok, code } = compile(src, 'js');
  assert.ok(ok);
  assert.ok(/__td_rpc_register\('C', 'foo', 'reliable'/.test(code));
});

check('emits if/else chain', () => {
  const src = 'class C { public void f() { if (true) { Log.info("y"); } else { Log.warn("n"); } } }';
  const { ok, code } = compile(src, 'js');
  assert.ok(ok);
  assert.ok(/if \(true\) {/.test(code));
  assert.ok(/} else {/.test(code));
});

// =============================================================================
// End-to-end: compile + load + run against runtime
// =============================================================================
console.log('\n--- End-to-End ---');

check('compiled code runs against TDScriptRuntime', () => {
  resetRuntime();
  const src = `
    import "engine/networking";
    class TestHandler {
      replicated int32 counter = 0;
      public void onServerStart() {
        Log.info("server start");
        this.counter = 1;
      }
      @rpc(reliable)
      public void bump(int32 amount) {
        this.counter = this.counter + amount;
      }
    }
  `;
  const { ok, code, error } = compile(src, 'js');
  assert.ok(ok, error);
  // Evaluate the compiled code in the global scope
  eval(code);
  // Run the entry hook
  const inst = global.__td_script_main('TestHandler');
  assert.ok(inst);
  assert.strictEqual(inst.counter, 1);  // set by onServerStart
  // Call an RPC
  const rpcEntry = global.TDScriptRuntime.rpcTable.get('TestHandler.bump');
  assert.ok(rpcEntry);
  rpcEntry.fn(inst, [5]);
  assert.strictEqual(inst.counter, 6);
  resetRuntime();
});

check('replicated field setter broadcasts state on mutation', () => {
  resetRuntime();
  const src = `
    class ReplTest {
      replicated int32 score = 0;
      public void onServerStart() {}
    }
  `;
  const { ok, code, error } = compile(src, 'js');
  assert.ok(ok, error);
  eval(code);
  const inst = global.__td_script_main('ReplTest');
  let capturedFrame = null;
  global.TDScriptRuntime.Network.lastFrame = null;
  // Mutate the replicated field — this should call Network.broadcastState
  inst.score = 42;
  assert.ok(global.TDScriptRuntime.Network.lastFrame, 'expected broadcast frame');
  assert.strictEqual(global.TDScriptRuntime.Network.lastFrame.method, 'tdscript.repl');
  assert.strictEqual(global.TDScriptRuntime.Network.lastFrame.params.field, 'ReplTest.score');
  assert.strictEqual(global.TDScriptRuntime.Network.lastFrame.params.value, 42);
  resetRuntime();
});

check('client→server RPC dispatch via dispatchRpc', () => {
  resetRuntime();
  const src = `
    class RpcDispatch {
      replicated int32 hp = 100;
      public void onServerStart() {}
      @rpc(reliable)
      public void takeDamage(int32 dmg) {
        this.hp = this.hp - dmg;
      }
    }
  `;
  const { ok, code, error } = compile(src, 'js');
  assert.ok(ok, error);
  eval(code);
  const inst = global.__td_script_main('RpcDispatch');
  // Simulate an incoming RPC frame
  const frame = {
    jsonrpc: '2.0',
    method: 'tdscript.rpc',
    params: { class: 'RpcDispatch', method: 'takeDamage', args: [30], mode: 'reliable' },
  };
  const ok2 = global.TDScriptRuntime.Network.dispatchRpc(frame, 'peer-1');
  assert.ok(ok2);
  assert.strictEqual(inst.hp, 70);
  resetRuntime();
});

// =============================================================================
console.log('\n--- Summary ---');
console.log(`  pass: ${pass}`);
console.log(`  fail: ${fail}`);

// Export `run` for the td test runner. Also auto-exit when invoked directly.
function run() {
  if (fail > 0) process.exit(1);
  return 0;
}

if (require.main === module) {
  process.exit(fail > 0 ? 1 : 0);
}

module.exports = { run };
