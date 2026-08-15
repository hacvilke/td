'use strict';

// td serve [path] [--port N]
//
// Starts a tiny static file server for the game folder, with:
//   - Live reload: watches the game folder + the engine's web/ dir; on any
//     change, sends a `reload` event to all connected browsers via WebSocket.
//   - /engine/* routes map to the engine's web/ directory (so the game's
//     index.html can <script src="/engine/td_api.js"></script> without
//     copying the engine files into every game folder).
//   - Default port 8080.
//
// The server is intentionally minimal: no SSR, no transpilation, no HMR of
// JS modules. Just files + a reload ping. Keeps the dev loop fast.

const fs = require('fs');
const http = require('http');
const path = require('path');
const url = require('url');
const { WebSocketServer } = require('ws'); // lazily required below

const {
  findEngineRoot, ok, info, warn, err,
  isFile, isDir, resolvePath, COLORS,
} = require('../lib/util');

function help() {
  console.log(`
td serve [path] [--port N]

Starts a dev server for a TD game folder.

Arguments:
  path              Game folder (default: current directory)

Options:
  --port N          Port to listen on (default: 8080)
  --no-reload       Disable live reload
  --open            Open the browser automatically

Routes:
  /                 Serves files from the game folder
  /engine/*         Serves files from the engine's web/ directory

Examples:
  td serve .
  td serve my-game --port 3000 --open
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

  const port = parseInt(opts.port || '8080', 10);
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

    // Prevent path traversal.
    if (filePath.indexOf(gameDir) !== 0 && filePath.indexOf(webDir) !== 0) {
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
