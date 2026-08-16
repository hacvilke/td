'use strict';

// td build [path]
//
// Builds the engine's WASM binary (via `make web`) and copies the engine
// runtime into <game>/build/ alongside the user's game files. Produces a
// self-contained folder you can deploy to any static host.
//
// Output: <game>/build/
//   index.html          (copied from <game>/)
//   game.js + assets    (copied from <game>/, recursively)
//   runtime/
//     td-engine.js      (Emscripten glue, from web/)
//     td-engine.wasm    (compiled C++ engine, from web/)
//     js_bridge.js, td_api.js, net_*.js, profiler.js, ...  (from web/)
//
// The build folder is what `td bundle` packs into a Windows installer.

const fs = require('fs');
const path = require('path');
const {
  findEngineRoot, ok, info, warn, err,
  isFile, isDir, copyDir, rmrf, resolvePath, spawnInherit,
} = require('../lib/util');

function help() {
  console.log(`
td build [path]

Builds the WASM engine + assembles a deployable build/ folder.

Arguments:
  path              Game folder (default: current directory)

Options:
  --no-wasm         Skip the WASM build (use existing web/td-engine.wasm)
  --clean           Wipe <game>/build/ before assembling
  --out DIR         Output directory (default: <game>/build)

Examples:
  td build my-game
  td build my-game --clean
`);
}

async function run(args, opts) {
  const gameDir = resolvePath(args[0] || '.');
  if (!isDir(gameDir)) {
    err(`Game folder not found: ${gameDir}`);
    return 1;
  }
  if (!isFile(path.join(gameDir, 'index.html'))) {
    err(`Game folder must contain index.html: ${gameDir}`);
    return 1;
  }
  const engineRoot = findEngineRoot();
  if (!engineRoot) {
    err('Could not locate engine root. Set TD_ENGINE_ROOT.');
    return 1;
  }
  const webDir = path.join(engineRoot, 'web');

  // 1. Build WASM unless --no-wasm or already built.
  const wasmFile = path.join(webDir, 'td-engine.wasm');
  const jsFile = path.join(webDir, 'td-engine.js');
  if (opts.wasm === false) {
    info('Skipping WASM build (--no-wasm).');
  } else if (isFile(wasmFile) && isFile(jsFile) && !opts.clean) {
    info(`WASM already built: ${path.relative(engineRoot, wasmFile)}`);
  } else {
    info('Building WASM engine (make web)...');
    const code = await spawnInherit('make', ['web'], { cwd: engineRoot });
    if (code !== 0) {
      err(`make web failed with exit code ${code}`);
      return code;
    }
    if (!isFile(wasmFile) || !isFile(jsFile)) {
      err(`Build finished but ${wasmFile} or ${jsFile} missing.`);
      return 1;
    }
    ok('WASM built.');
  }

  // 2. Assemble build folder.
  const outDir = opts.out ? resolvePath(opts.out) : path.join(gameDir, 'build');
  if (opts.clean && isDir(outDir)) {
    rmrf(outDir);
    info(`Cleaned ${outDir}`);
  }
  fs.mkdirSync(outDir, { recursive: true });

  // Copy game files (excluding build/ itself and node_modules).
  const skipDirs = new Set(['build', 'node_modules', '.git']);
  for (const entry of fs.readdirSync(gameDir, { withFileTypes: true })) {
    if (skipDirs.has(entry.name)) continue;
    const src = path.join(gameDir, entry.name);
    const dst = path.join(outDir, entry.name);
    if (entry.isDirectory()) copyDir(src, dst);
    else if (entry.isFile()) fs.copyFileSync(src, dst);
  }

  // Copy engine runtime into build/runtime/.
  const runtimeDir = path.join(outDir, 'runtime');
  fs.mkdirSync(runtimeDir, { recursive: true });
  const RUNTIME_FILES = [
    'td-engine.js', 'td-engine.wasm', 'js_bridge.js', 'td_api.js',
    'net_websocket.js', 'net_peer.js', 'server_router.js',
    'inspector.js', 'profiler.js', 'persistence.js',
    'error_boundary.js', 'deprecated_tracker.js',
    'net_interpolation.js', 'net_auth_server.js',
    'tdscript_runtime.js', 'td_client_bootstrap.js',
  ];
  let copied = 0;
  for (const name of RUNTIME_FILES) {
    const src = path.join(webDir, name);
    if (isFile(src)) {
      fs.copyFileSync(src, path.join(runtimeDir, name));
      copied++;
    }
  }
  ok(`Assembled build/ with ${copied} runtime files.`);

  // Rewrite index.html to load runtime/ from the new location.
  // (If the user's index.html already references /engine/ or runtime/,
  // we leave it alone. Otherwise we patch script srcs that look like
  // td-api.js / js_bridge.js / td-engine.js to point at runtime/.)
  patchIndexHtml(path.join(outDir, 'index.html'));

  ok(`Build complete: ${outDir}`);
  info('To ship: deploy this folder to any static host (GitHub Pages, Netlify, itch.io).');
  info('To ship as a Windows installer: td bundle -path build -config bundle.json');
  return 0;
}

function patchIndexHtml(file) {
  if (!isFile(file)) return;
  let src = fs.readFileSync(file, 'utf-8');
  // Convert /engine/foo.js -> runtime/foo.js (after build, no /engine route).
  src = src.replace(/\/engine\//g, 'runtime/');
  fs.writeFileSync(file, src, 'utf-8');
}

module.exports = { run, help };
