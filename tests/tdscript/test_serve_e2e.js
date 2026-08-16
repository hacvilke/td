'use strict';

// =============================================================================
// TD Engine — Default Test Server (td serve) end-to-end test
//
// Verifies that `td serve` boots:
//   1. The static file server on port 8080 (serves /project.td, /engine/*, /index.html)
//   2. The game-net WebSocket server on port 8081 (runs the TDScript server script)
//
// Then simulates a client connecting to the game-net server, invoking an RPC,
// and receiving a replicated state push.
//
// Run: node tests/tdscript/test_serve_e2e.js
// =============================================================================

const path = require('path');
const fs = require('fs');
const http = require('http');
const os = require('os');
const assert = require('assert');
const { spawn } = require('child_process');
const WebSocket = require('ws');

let pass = 0, fail = 0;
function check(name, fn) {
  return Promise.resolve().then(fn).then(() => {
    pass++; console.log('  ok  ' + name);
  }).catch((e) => {
    fail++; console.log('FAIL  ' + name + '\n      ' + e.message);
  });
}

const ENGINE_ROOT = path.join(__dirname, '..', '..');
const TD_BIN = path.join(ENGINE_ROOT, 'tools', 'cli', 'td.js');

async function main() {
  // Create a temp game dir with project.td + server_main.td
  const tmpDir = path.join(os.tmpdir(), 'td-serve-test-' + Date.now());
  fs.mkdirSync(tmpDir, { recursive: true });
  fs.mkdirSync(path.join(tmpDir, 'src', 'server'), { recursive: true });

  // Copy the minimal template
  const templateDir = path.join(ENGINE_ROOT, 'tools', 'cli', 'templates', 'minimal');
  function copyDir(src, dst) {
    for (const entry of fs.readdirSync(src, { withFileTypes: true })) {
      const s = path.join(src, entry.name);
      const d = path.join(dst, entry.name);
      if (entry.isDirectory()) { fs.mkdirSync(d, { recursive: true }); copyDir(s, d); }
      else { fs.copyFileSync(s, d); }
    }
  }
  copyDir(templateDir, tmpDir);

  // Patch project.td to use a known free port
  const projPath = path.join(tmpDir, 'project.td');
  const proj = JSON.parse(fs.readFileSync(projPath, 'utf-8'));
  proj.networking.defaultServer = 'ws://127.0.0.1:18081/room';
  fs.writeFileSync(projPath, JSON.stringify(proj, null, 2));

  // Create a minimal index.html (template may already have one — overwrite)
  fs.writeFileSync(path.join(tmpDir, 'index.html'), '<!DOCTYPE html><html><body><canvas id="game-canvas"></canvas></body></html>');

  // Start td serve
  console.log('Booting td serve in', tmpDir);
  const serveProc = spawn('node', [TD_BIN, 'serve', tmpDir, '--port', '18080', '--no-reload'], {
    cwd: ENGINE_ROOT,
    stdio: ['ignore', 'pipe', 'pipe'],
  });
  let serveOut = '';
  serveProc.stdout.on('data', (d) => { serveOut += d.toString(); });
  serveProc.stderr.on('data', (d) => { serveOut += d.toString(); });

  // Wait for the server to be ready
  await new Promise((resolve) => {
    const interval = setInterval(() => {
      if (serveOut.includes('Game-net server: ws://')) {
        clearInterval(interval);
        setTimeout(resolve, 200);
      } else if (serveOut.includes('Game-net server skipped') || serveOut.includes('Game-net server disabled')) {
        clearInterval(interval);
        resolve();
      }
    }, 100);
    setTimeout(() => { clearInterval(interval); resolve(); }, 5000);
  });

  console.log('--- serve output so far ---');
  console.log(serveOut);
  console.log('----------------------------');

  // --- Tests ---
  await check('static server serves /project.td', async () => {
    const body = await httpGet('http://127.0.0.1:18080/project.td');
    const parsed = JSON.parse(body);
    assert.ok(parsed.name, 'project.td should have a name field');
    assert.ok(parsed.networking, 'project.td should have a networking section');
    assert.ok(parsed.networking.defaultServer, 'project.td should have networking.defaultServer');
  });

  await check('static server serves index.html', async () => {
    const body = await httpGet('http://127.0.0.1:18080/');
    assert.ok(/game-canvas/.test(body));
  });

  await check('static server serves /engine/tdscript_runtime.js', async () => {
    const body = await httpGet('http://127.0.0.1:18080/engine/tdscript_runtime.js');
    assert.ok(/TDScriptRuntime/.test(body));
  });

  await check('game-net server accepts WebSocket connection', async () => {
    const ws = new WebSocket('ws://127.0.0.1:18081/room');
    await new Promise((resolve, reject) => {
      ws.on('open', resolve);
      ws.on('error', reject);
      setTimeout(() => reject(new Error('timeout')), 2000);
    });
    ws.close();
  });

  await check('RPC round-trip: client → server → replicated push', async () => {
    const ws = new WebSocket('ws://127.0.0.1:18081/room');
    await new Promise((resolve, reject) => {
      ws.on('open', resolve);
      ws.on('error', reject);
      setTimeout(() => reject(new Error('timeout')), 2000);
    });

    // Send a reliable RPC: ServerMain.processPlayerDamage(30)
    // The server's onServerStart set playerHealth = 100. After taking 30 damage,
    // it should broadcast a replicated update with playerHealth = 70.
    const rpcFrame = {
      jsonrpc: '2.0',
      method: 'tdscript.rpc',
      params: { class: 'ServerMain', method: 'processPlayerDamage', args: [30], mode: 'reliable' },
    };
    const messages = [];
    const collectPromise = new Promise((resolve) => {
      ws.on('message', (data) => {
        const frame = JSON.parse(data.toString());
        messages.push(frame);
        if (frame.method === 'tdscript.repl' && frame.params.field === 'ServerMain.playerHealth') {
          resolve(frame);
        }
      });
      setTimeout(() => resolve(null), 3000);
    });
    ws.send(JSON.stringify(rpcFrame));
    const replFrame = await collectPromise;
    assert.ok(replFrame, 'expected a replicated playerHealth push');
    assert.strictEqual(replFrame.params.value, 70);
    ws.close();
  });

  // --- Cleanup ---
  serveProc.kill('SIGTERM');
  await new Promise((resolve) => serveProc.on('exit', resolve));
  try { fs.rmSync(tmpDir, { recursive: true, force: true }); } catch (e) {}

  console.log('\n--- Summary ---');
  console.log(`  pass: ${pass}`);
  console.log(`  fail: ${fail}`);
  return fail > 0 ? 1 : 0;
}

function httpGet(url) {
  return new Promise((resolve, reject) => {
    http.get(url, (res) => {
      let body = '';
      res.on('data', (d) => body += d);
      res.on('end', () => resolve(body));
    }).on('error', reject);
  });
}

// Export `run` for the td test runner.
module.exports = { run: main };

if (require.main === module) {
  main().catch((e) => {
    console.error('Test runner crashed:', e);
    process.exit(1);
  }).then((code) => {
    process.exit(code);
  });
}
