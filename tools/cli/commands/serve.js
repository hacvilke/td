'use strict';

// td serve [path] [--port N] [--no-net]
//
// Starts a dev server for the game folder, with:
//   - Static file server (HTML/JS/WASM) on the main port (default 8080)
//   - Live reload via WebSocket at /__reload
//   - /engine/* routes map to the engine's web/ directory
//   - Game-net WebSocket server on a SECOND port (default 8081) — this is the
//     "default test server" that the engine routes test games to for
//     multiplayer testing. It loads the project.td's serverScript (a .td
//     file), compiles it via TDScript, and runs it against the TDScript
//     runtime. Clients connect via the URL in project.td's networking config.
//
// If --no-net is passed, only the static server boots (no game-net server).
// If project.td is missing or has no entry.serverScript, the game-net server
// is skipped with a warning.

const fs = require('fs');
const http = require('http');
const path = require('path');
const url = require('url');
const vm = require('vm');

const {
  findEngineRoot, ok, info, warn, err,
  isFile, isDir, resolvePath, COLORS,
} = require('../lib/util');
const { loadProjectTds, resolveServerScript, parseServerUrl } = require('../lib/project_tds');
const { compile } = require('../../tdscript/tdscript.js');

function help() {
  console.log(`
td serve [path] [--port N] [--no-net]

Starts a dev server for a TD game folder, with a game-net server for multiplayer testing.

Arguments:
  path              Game folder (default: current directory)

Options:
  --port N          Static server port (default: 8080)
  --net-port N      Game-net WebSocket server port (default: from project.td or 8081)
  --no-reload       Disable live reload
  --no-net          Skip booting the game-net server (static files only)
  --open            Open the browser automatically

Routes:
  /                 Serves files from the game folder
  /engine/*         Serves files from the engine's web/ directory
  /project.td       Serves the project.td config (so the client can read it)

Game-net server:
  Reads project.td's networking.defaultServer URL to determine its port.
  Loads project.td's entry.serverScript (.td file), compiles it via TDScript,
  and runs it against tdscript_runtime.js. Clients connect via WebSocket
  and exchange JSON-RPC frames (same wire format as net_websocket.js).

Examples:
  td serve .
  td serve my-game --port 3000 --open
  td serve my-game --no-net  # static only
`);
}

const MIME = {
  '.html': 'text/html; charset=utf-8',
  '.js':   'application/javascript; charset=utf-8',
  '.mjs':  'application/javascript; charset=utf-8',
  '.json': 'application/json; charset=utf-8',
  '.css':  'text/css; charset=utf-8',
  '.png':  'image/png',
  '.jpg':  'image/jpeg',
  '.jpeg': 'image/jpeg',
  '.gif':  'image/gif',
  '.webp': 'image/webp',
  '.svg':  'image/svg+xml',
  '.ico':  'image/x-icon',
  '.wasm': 'application/wasm',
  '.woff': 'font/woff',
  '.woff2': 'font/woff2',
  '.ttf':  'font/ttf',
  '.mp3':  'audio/mpeg',
  '.wav':  'audio/wav',
  '.ogg':  'audio/ogg',
  '.txt':  'text/plain; charset=utf-8',
  '.map':  'application/json; charset=utf-8',
};

const RELOAD_CLIENT_SNIPPET = `
<!-- td serve reload client -->
<script>
(function(){
  var ws = new WebSocket('ws://' + location.host + '/__reload');
  ws.onmessage = function(ev){
    if (ev.data === 'reload') { location.reload(); }
  };
  ws.onclose = function(){
    setTimeout(function(){ location.reload(); }, 1000);
  };
})();
</script>
`;

async function run(args, opts) {
  const gameDir = resolvePath(args[0] || '.');
  if (!isDir(gameDir)) {
    err(`Game folder not found: ${gameDir}`);
    return 1;
  }
  const engineRoot = findEngineRoot();
  if (!engineRoot) {
    err('Could not locate engine root. Set TD_ENGINE_ROOT.');
    return 1;
  }
  const webDir = path.join(engineRoot, 'web');
  if (!isDir(webDir)) {
    err(`Engine web/ dir not found: ${webDir}`);
    return 1;
  }

  const portRaw = (opts.port === true || opts.port === undefined) ? '8080' : opts.port;
  const port = parseInt(portRaw, 10);
  if (!Number.isInteger(port) || port < 1 || port > 65535) {
    err(`Invalid port: ${opts.port}`);
    return 1;
  }
  const useReload = opts.reload !== false;
  const open = !!opts.open;

  let wss = null;
  if (useReload) {
    try {
      const { WebSocketServer } = require('ws');
      wss = new WebSocketServer({ noServer: true });
    } catch (e) {
      warn('ws package not installed; live reload disabled.');
    }
  }

  const server = http.createServer((req, res) => {
    const parsed = url.parse(req.url);
    let p = decodeURIComponent(parsed.pathname);

    // /project.td — serve the project config so the client can read it
    if (p === '/project.td') {
      const projPath = path.join(gameDir, 'project.td');
      if (!fs.existsSync(projPath)) {
        res.statusCode = 404;
        res.end('Not found: project.td');
        return;
      }
      res.setHeader('Content-Type', 'application/json; charset=utf-8');
      res.setHeader('Access-Control-Allow-Origin', '*');
      fs.createReadStream(projPath).pipe(res);
      return;
    }

    // Engine files: /engine/foo.js -> web/foo.js
    let filePath;
    if (p.startsWith('/engine/')) {
      const rel = p.slice('/engine/'.length);
      filePath = path.join(webDir, rel);
    } else if (p === '/') {
      filePath = path.join(gameDir, 'index.html');
    } else {
      filePath = path.join(gameDir, p);
    }

    // Prevent path traversal — use path-aware check (not substring prefix).
    const relGame = path.relative(gameDir, filePath);
    const relWeb = path.relative(webDir, filePath);
    const outsideGame = relGame.startsWith('..') || path.isAbsolute(relGame);
    const outsideWeb = relWeb.startsWith('..') || path.isAbsolute(relWeb);
    if (outsideGame && outsideWeb) {
      res.statusCode = 403;
      res.end('Forbidden');
      return;
    }

    if (!isFile(filePath)) {
      res.statusCode = 404;
      res.end(`Not found: ${p}`);
      return;
    }

    const ext = path.extname(filePath).toLowerCase();
    const mime = MIME[ext] || 'application/octet-stream';

    // Inject reload snippet into HTML.
    if (useReload && ext === '.html') {
      let body = fs.readFileSync(filePath, 'utf-8');
      body = body.replace('</body>', `${RELOAD_CLIENT_SNIPPET}\n</body>`);
      res.setHeader('Content-Type', mime);
      res.setHeader('Content-Length', Buffer.byteLength(body));
      res.end(body);
      return;
    }

    res.setHeader('Content-Type', mime);
    fs.createReadStream(filePath).pipe(res);
  });

  // --- Game-net server setup (default test server) ---
  const useNet = opts.net !== false;
  let netPort = null;
  let netServer = null;
  let netWss = null;
  let netRuntime = null;
  let netClients = new Map();  // ws → peerId
  let netPeerIdCounter = 0;
  let netMainInstance = null;

  if (useNet) {
    // Load project.td
    const proj = loadProjectTds(gameDir);
    if (!proj.ok) {
      warn(`Game-net server skipped: ${proj.error}`);
    } else {
      const cfg = proj.config;
      const serverUrl = parseServerUrl(cfg.networking.defaultServer);
      if (!serverUrl) {
        warn(`Invalid networking.defaultServer URL: ${cfg.networking.defaultServer}`);
      } else {
        netPort = opts['net-port'] ? parseInt(opts['net-port'], 10) : serverUrl.port;
        const serverScriptPath = resolveServerScript(gameDir, cfg);
        if (!serverScriptPath || !fs.existsSync(serverScriptPath)) {
          warn(`Server script not found: ${serverScriptPath || '(none in project.td)'}`);
        } else {
          // Compile the .td script
          const tdSrc = fs.readFileSync(serverScriptPath, 'utf-8');
          const compileResult = compile(tdSrc, 'js');
          if (!compileResult.ok) {
            err(`TDScript compilation failed:`);
            console.error(compileResult.error);
            process.exit(1);
          }

          // Load the TDScript runtime + compiled script into a sandbox
          const runtimePath = path.join(engineRoot, 'web', 'tdscript_runtime.js');
          const runtimeSrc = fs.readFileSync(runtimePath, 'utf-8');
          netRuntime = { console, process, require, setTimeout };
          netRuntime.global = netRuntime;
          netRuntime.window = netRuntime;
          vm.createContext(netRuntime);
          try {
            vm.runInContext(runtimeSrc, netRuntime, { filename: 'tdscript_runtime.js' });
            vm.runInContext(compileResult.code, netRuntime, { filename: path.basename(serverScriptPath) + '.js' });
            const mainClass = cfg.entry.mainClass || 'ServerMain';
            netMainInstance = netRuntime.__td_script_main(mainClass);
            if (!netMainInstance) {
              err(`Failed to instantiate main server class: ${mainClass}`);
              process.exit(1);
            }
            // Wire the runtime's Network.sendFrame to broadcast via netWss
            netRuntime.__td_net_send = function (frame, opts2) {
              if (!netWss) return;
              const json = JSON.stringify(frame);
              if (opts2 && opts2.broadcast) {
                for (const ws of netClients.keys()) {
                  if (ws.readyState === 1) ws.send(json);
                }
              } else if (opts2 && opts2.peerId) {
                for (const [ws, pid] of netClients.entries()) {
                  if (pid === opts2.peerId && ws.readyState === 1) { ws.send(json); break; }
                }
              }
            };
            ok(`Game-net server: loaded ${cfg.entry.mainClass} from ${path.relative(gameDir, serverScriptPath)}`);
          } catch (e) {
            err(`Failed to run TDScript: ${e.message}`);
            console.error(e.stack);
            process.exit(1);
          }

          // Boot the WebSocket server for game traffic
          try {
            const { WebSocketServer } = require('ws');
            netWss = new WebSocketServer({ noServer: true });
            netServer = http.createServer((req, res) => {
              res.statusCode = 200;
              res.setHeader('Content-Type', 'text/plain');
              res.end('TD Engine game-net server. Connect via WebSocket.\n');
            });
            netServer.on('upgrade', (req, socket, head) => {
              const parsed = url.parse(req.url);
              if (parsed.pathname === serverUrl.path) {
                netWss.handleUpgrade(req, socket, head, (ws) => {
                  const peerId = ++netPeerIdCounter;
                  netClients.set(ws, peerId);
                  ws.on('message', (data) => {
                    let frame;
                    try { frame = JSON.parse(data.toString()); } catch (e) { return; }
                    // Dispatch tdscript.rpc / tdscript.repl frames to the runtime
                    if (frame.method === 'tdscript.rpc') {
                      netRuntime.TDScriptRuntime.Network.dispatchRpc(frame, peerId);
                    } else if (frame.method === 'tdscript.repl') {
                      netRuntime.TDScriptRuntime.Network.applyReplicated(frame);
                    } else {
                      // Unknown method — let the runtime handle it
                      // (future: route to JSON-RPC server for non-tdscript methods)
                    }
                  });
                  ws.on('close', () => { netClients.delete(ws); });
                  ws.on('error', () => { netClients.delete(ws); });
                });
              } else {
                socket.destroy();
              }
            });
            netServer.listen(netPort, () => {
              ok(`Game-net server: ws://localhost:${netPort}${serverUrl.path}`);
            });
          } catch (e) {
            warn(`ws package not installed; game-net server disabled. (${e.message})`);
          }
        }
      }
    }
  }

  if (wss) {
    server.on('upgrade', (req, socket, head) => {
      const parsed = url.parse(req.url);
      if (parsed.pathname === '/__reload') {
        wss.handleUpgrade(req, socket, head, (ws) => {
          ws.send('connected');
        });
      } else {
        socket.destroy();
      }
    });
  }

  server.listen(port, () => {
    ok(`Serving ${path.relative(process.cwd(), gameDir) || '.'} on http://localhost:${port}`);
    if (useReload && wss) info('Live reload: enabled');
    info('Press Ctrl+C to stop.');
    if (open) {
      const openCmd = process.platform === 'win32' ? 'start ""'
                    : process.platform === 'darwin' ? 'open'
                    : 'xdg-open';
      require('child_process').spawn(openCmd, [`http://localhost:${port}`], { shell: true, detached: true });
    }
  });

  if (wss) {
    const watchTargets = [gameDir];
    if (isDir(webDir)) watchTargets.push(webDir);
    for (const root of watchTargets) {
      try {
        fs.watch(root, { recursive: true }, (eventType, filename) => {
          if (!filename) return;
          // Skip editor swap files.
          if (filename.endsWith('.swp') || filename.endsWith('~') || filename.startsWith('.')) return;
          for (const client of wss.clients) {
            if (client.readyState === 1 /* OPEN */) client.send('reload');
          }
        });
      } catch (e) {
        warn(`fs.watch failed for ${root}: ${e.message}`);
      }
    }
  }

  return new Promise(() => {}); // runs until killed
}

module.exports = { run, help };
