'use strict';

// =============================================================================
// td script <subcommand> [args]
//
// Subcommands:
//   compile <file.td> [-o out.js] [--target js|cpp]
//     Compiles a .td file to JS (default) or C++ (stub).
//   check <file.td>
//     Type-checks / parses only; emits diagnostics, no output file.
//   run <file.td> [--main ClassName]
//     Compiles + loads against tdscript_runtime.js + invokes onServerStart.
//     Useful for smoke-testing server scripts locally.
// =============================================================================

const fs = require('fs');
const path = require('path');
const vm = require('vm');
const {
  findEngineRoot, ok, info, warn, err,
  isFile, resolvePath, COLORS,
} = require('../lib/util');
const { compile } = require('../../tdscript/tdscript.js');

function help() {
  console.log(`
td script <subcommand> [args]

Subcommands:
  compile <file.td> [-o out.js] [--target js|cpp]
          Compiles a .td source file to the target language.
  check   <file.td>
          Parses + reports diagnostics. No output file.
  run     <file.td> [--main ClassName]
          Compiles, loads against the TDScript runtime, and runs onServerStart.

Options:
  -o PATH          Output file path (compile only). Default: <file>.js
  --target LANG    Target language: "js" (default) or "cpp" (stub).
  --main NAME      Main class to instantiate (run only). Default: ServerMain

Examples:
  td script compile src/server/server_main.td -o build/server_main.js
  td script check src/server/server_main.td
  td script run src/server/server_main.td --main ServerMain
`);
}

async function run(args, opts) {
  if (args.length === 0) { help(); return 1; }
  const sub = args[0];
  const rest = args.slice(1);

  switch (sub) {
    case 'compile': return cmdCompile(rest, opts);
    case 'check':   return cmdCheck(rest, opts);
    case 'run':     return cmdRun(rest, opts);
    case '--help':
    case '-h':
    case 'help':
      help();
      return 0;
    default:
      err(`Unknown subcommand: ${sub}`);
      help();
      return 1;
  }
}

function loadFile(p) {
  const fullPath = resolvePath(p);
  if (!isFile(fullPath)) {
    err(`File not found: ${fullPath}`);
    return null;
  }
  return fs.readFileSync(fullPath, 'utf-8');
}

function cmdCompile(args, opts) {
  if (args.length === 0) { err('Usage: td script compile <file.td> [-o out.js] [--target js|cpp]'); return 1; }
  const file = args[0];
  const target = opts.target || 'js';
  const src = loadFile(file);
  if (src === null) return 1;

  const result = compile(src, target);
  if (!result.ok) {
    err('Compilation failed:');
    console.error(result.error);
    return 1;
  }

  let outPath = opts.o || opts.output;
  if (!outPath) {
    // Default: replace .td extension with .js (or .cpp)
    outPath = file.replace(/\.td$/, '.' + target);
  }
  outPath = resolvePath(outPath);
  fs.mkdirSync(path.dirname(outPath), { recursive: true });
  fs.writeFileSync(outPath, result.code, 'utf-8');
  ok(`Compiled ${path.basename(file)} → ${path.relative(process.cwd(), outPath)} (${result.code.length} bytes)`);
  return 0;
}

function cmdCheck(args, opts) {
  if (args.length === 0) { err('Usage: td script check <file.td>'); return 1; }
  const file = args[0];
  const src = loadFile(file);
  if (src === null) return 1;

  const result = compile(src, 'js');
  if (result.ok) {
    ok(`${path.basename(file)}: OK (${result.code.length} bytes of JS generated)`);
    return 0;
  }
  err(`${path.basename(file)}: ${result.error}`);
  return 1;
}

function cmdRun(args, opts) {
  if (args.length === 0) { err('Usage: td script run <file.td> [--main ClassName]'); return 1; }
  const file = args[0];
  const mainClass = opts.main || 'ServerMain';
  const src = loadFile(file);
  if (src === null) return 1;

  const result = compile(src, 'js');
  if (!result.ok) {
    err('Compilation failed:');
    console.error(result.error);
    return 1;
  }

  // Load the TDScript runtime into a sandbox
  const engineRoot = findEngineRoot();
  const runtimePath = path.join(engineRoot, 'web', 'tdscript_runtime.js');
  if (!isFile(runtimePath)) {
    err(`Runtime not found: ${runtimePath}`);
    return 1;
  }
  const runtimeSrc = fs.readFileSync(runtimePath, 'utf-8');

  // Create a sandbox with a `global` that points to itself
  const sandbox = { console: console, process: process, require: require, module: module, setTimeout: setTimeout };
  sandbox.global = sandbox;
  sandbox.window = sandbox;  // so the runtime's IIFE picks it up
  vm.createContext(sandbox);

  try {
    vm.runInContext(runtimeSrc, sandbox, { filename: 'tdscript_runtime.js' });
    vm.runInContext(result.code, sandbox, { filename: path.basename(file) + '.js' });
    const inst = sandbox.__td_script_main(mainClass);
    if (!inst) {
      err(`Main class "${mainClass}" not found or failed to instantiate.`);
      return 1;
    }
    ok(`Ran ${path.basename(file)} — main class ${mainClass} instantiated.`);
    info(`Replicated fields:`);
    const repl = sandbox.TDScriptRuntime.replTable.get(mainClass) || [];
    for (const f of repl) {
      info(`  ${mainClass}.${f} = ${JSON.stringify(inst['_' + f])}`);
    }
    info(`Registered RPCs:`);
    for (const [key, entry] of sandbox.TDScriptRuntime.rpcTable.entries()) {
      if (key.startsWith(mainClass + '.')) {
        info(`  ${key} [${entry.mode}]`);
      }
    }
    return 0;
  } catch (e) {
    err(`Runtime error: ${e.message}`);
    console.error(e.stack);
    return 1;
  }
}

module.exports = { run, help };
