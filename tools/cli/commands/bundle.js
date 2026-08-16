'use strict';

// td bundle -path DIR -config FILE
//
// Wraps tools/bundler/bundle.py (the Inno Setup generator) so users can drive
// it through the unified `td` CLI instead of invoking Python directly.
//
// Config file format (JSON):
//
//   {
//     "name":        "My Cool Game",        // required
//     "version":     "1.0.0",               // default: 1.0.0
//     "description": "A short tagline.",    // written to bundle.json sidecar
//     "publisher":   "Some Studio",         // default: "Unknown Publisher"
//     "id":          "my-cool-game",        // default: slugified name
//     "icon":        "./assets/icon.ico",   // optional, .ico file
//     "url":         "https://...",         // optional
//     "out":         "MyCoolGame-Setup.exe",// default: <id>-setup.exe
//     "bundle_runtime": false,              // bundle WebView2 bootstrapper
//     "license":     "./LICENSE",           // optional
//     "readme":      "./README.md",         // optional
//     "host_exe":    "./build/host.exe",    // optional, override host shell
//     "web_dir":     "./web",               // optional, override engine web/
//     "iscc":        "C:/Program Files (x86)/Inno Setup 6/ISCC.exe",
//     "dry_run":     false                  // stage + write .iss, don't invoke ISCC
//   }
//
// Any CLI flag (e.g. --dry-run) overrides the equivalent config field.
//
// This command does NOT require Python: if Python is unavailable, it
// reimplements the staging + .iss templating in Node.js directly. If Python
// IS available, it delegates to bundle.py for parity.

const fs = require('fs');
const path = require('path');
const {
  findEngineRoot, ok, info, warn, err,
  isFile, isDir, copyDir, rmrf, readJson, resolvePath, spawnInherit, COLORS,
} = require('../lib/util');

function help() {
  console.log(`
td bundle -path DIR -config FILE

Builds a Windows installer (.exe) for a finished web game, using Inno Setup
for the install/uninstall UI.

Required:
  -path DIR         Game folder (must contain index.html)
  -config FILE      JSON config file (see below)

Config file (JSON):
  {
    "name":           "My Cool Game",          // required
    "version":        "1.0.0",
    "description":    "Tagline for the installer",   // optional
    "publisher":      "Some Studio",
    "id":             "my-cool-game",           // default: slug of name
    "icon":           "./assets/icon.ico",      // optional
    "url":            "https://example.com",
    "out":            "MyGame-Setup.exe",       // default: <id>-setup.exe
    "bundle_runtime": false,                    // bundle WebView2 bootstrapper
    "license":        "./LICENSE",              // optional
    "readme":         "./README.md",            // optional
    "host_exe":       "...",                    // override host shell
    "web_dir":        "...",                    // override engine web/
    "iscc":           "...",                    // override ISCC.exe path
    "dry_run":        false                     // stage only, no ISCC
  }

CLI overrides (any of these override the config file):
  --name, --version, --publisher, --id, --icon, --url, --out,
  --host-exe, --web-dir, --iscc, --bundle-runtime, --license, --readme,
  --dry-run, --keep-staging

Examples:
  td bundle -path ./my-game -config bundle.json
  td bundle -path ./build -config bundle.json --dry-run
  td bundle -path . -config bundle.json --out Release/Setup.exe
`);
}

async function run(args, opts) {
  // ---- Parse required args ------------------------------------------------
  const gameDirRaw = opts.path || opts.p;
  if (!gameDirRaw) {
    err('Missing -path. See `td help bundle`.');
    return 1;
  }
  const gameDir = resolvePath(gameDirRaw);
  if (!isDir(gameDir)) {
    err(`Game folder not found: ${gameDir}`);
    return 1;
  }
  if (!isFile(path.join(gameDir, 'index.html'))) {
    err(`Game folder must contain index.html: ${gameDir}`);
    return 1;
  }

  const configFileRaw = opts.config || opts.c;
  if (!configFileRaw) {
    err('Missing -config. See `td help bundle`.');
    return 1;
  }
  const configFile = resolvePath(configFileRaw);
  if (!isFile(configFile)) {
    err(`Config file not found: ${configFile}`);
    return 1;
  }
  const cfg = readJson(configFile);
  if (!cfg || typeof cfg !== 'object') {
    err(`Config file is not valid JSON: ${configFile}`);
    return 1;
  }

  // ---- Merge config + CLI overrides --------------------------------------
  const engineRoot = findEngineRoot() || process.cwd();
  const merged = {
    name:           opts.name       || cfg.name        || path.basename(gameDir),
    version:        opts.version    || cfg.version     || '1.0.0',
    description:    opts.description|| cfg.description || '',
    publisher:      opts.publisher  || cfg.publisher   || 'Unknown Publisher',
    id:             opts.id         || cfg.id          || slugify(opts.name || cfg.name || path.basename(gameDir)),
    icon:           opts.icon       || cfg.icon        || null,
    url:            opts.url        || cfg.url         || '',
    out:            opts.out        || cfg.out         || null,
    bundle_runtime: opts['bundle-runtime'] === true || cfg.bundle_runtime === true,
    webview2_bootstrapper: opts['webview2-bootstrapper'] || cfg.webview2_bootstrapper || null,
    license:        opts.license    || cfg.license     || null,
    readme:         opts.readme     || cfg.readme      || null,
    host_exe:       opts['host-exe']|| cfg.host_exe    || path.join(engineRoot, 'build', 'bin', 'td-host.exe'),
    web_dir:        opts['web-dir'] || cfg.web_dir     || path.join(engineRoot, 'web'),
    iscc:           opts.iscc       || cfg.iscc        || process.env.ISCC || null,
    dry_run:        opts['dry-run'] === true || cfg.dry_run === true,
    keep_staging:   opts['keep-staging'] === true || cfg.keep_staging === true,
  };

  // Validate --bundle-runtime requires --webview2-bootstrapper (matches bundle.py).
  if (merged.bundle_runtime && !merged.webview2_bootstrapper) {
    err('--bundle-runtime requires --webview2-bootstrapper PATH');
    return 1;
  }

  // ---- Resolve output path -----------------------------------------------
  let outPath = merged.out
    ? resolvePath(merged.out)
    : path.join(process.cwd(), `${merged.id}-setup.exe`);
  fs.mkdirSync(path.dirname(outPath), { recursive: true });

  // ---- Prefer Python bundle.py if available (single source of truth) -----
  const pythonBin = process.platform === 'win32' ? 'python' : 'python3';
  let pythonAvailable = false;
  try {
    const { spawnSync } = require('child_process');
    const r = spawnSync(pythonBin, ['--version'], { stdio: 'pipe' });
    pythonAvailable = r.status === 0;
  } catch { pythonAvailable = false; }

  const bundlePy = path.join(engineRoot, 'tools', 'bundler', 'bundle.py');
  if (pythonAvailable && isFile(bundlePy)) {
    info('Delegating to Python bundle.py for parity.');
    const pyArgs = [
      bundlePy,
      '--game', gameDir,
      '--name', merged.name,
      '--version', merged.version,
      '--publisher', merged.publisher,
      '--id', merged.id,
      '--out', outPath,
      '--host-exe', merged.host_exe,
      '--web-dir', merged.web_dir,
    ];
    if (merged.icon) pyArgs.push('--icon', resolvePath(merged.icon));
    if (merged.url) pyArgs.push('--url', merged.url);
    if (merged.license) pyArgs.push('--license', resolvePath(merged.license));
    if (merged.readme) pyArgs.push('--readme', resolvePath(merged.readme));
    if (merged.iscc) pyArgs.push('--iscc', merged.iscc);
    if (merged.bundle_runtime) {
      pyArgs.push('--bundle-runtime');
      if (merged.webview2_bootstrapper) {
        pyArgs.push('--webview2-bootstrapper', resolvePath(merged.webview2_bootstrapper));
      }
    }
    if (merged.dry_run) pyArgs.push('--dry-run');
    if (merged.keep_staging) pyArgs.push('--keep-staging');

    const code = await spawnInherit(pythonBin, pyArgs);
    if (code !== 0) {
      err(`bundle.py exited with code ${code}`);
      return code;
    }
    if (!merged.dry_run) ok(`Installer: ${outPath}`);
    return 0;
  }

  // ---- Fallback: pure Node.js staging + ISS templating -------------------
  warn('Python not available — using built-in Node.js bundler.');
  warn('This produces the same staging + .iss file but does NOT invoke ISCC.');
  warn('Install Inno Setup + Python for end-to-end installer generation.');

  const stagingDir = path.join(engineRoot, 'build', `bundle-${merged.id}`);
  if (isDir(stagingDir)) rmrf(stagingDir);
  fs.mkdirSync(stagingDir, { recursive: true });

  // Stage host, runtime, game.
  const targetExeName = exeNameFor(merged.name);
  if (!isFile(merged.host_exe)) {
    err(`Host .exe not found: ${merged.host_exe}`);
    err('  Build it with: cmake --build build --target td-host');
    return 1;
  }
  fs.mkdirSync(path.join(stagingDir, 'host'), { recursive: true });
  fs.copyFileSync(merged.host_exe, path.join(stagingDir, 'host', targetExeName));

  if (!isDir(merged.web_dir)) {
    err(`Engine web/ dir not found: ${merged.web_dir}`);
    return 1;
  }
  const runtimeDir = path.join(stagingDir, 'runtime');
  fs.mkdirSync(runtimeDir, { recursive: true });
  const RUNTIME_FILES = [
    'td-engine.js', 'td-engine.wasm', 'js_bridge.js', 'td_api.js',
    'net_websocket.js', 'net_peer.js', 'server_router.js',
    'inspector.js', 'profiler.js', 'persistence.js',
    'error_boundary.js', 'deprecated_tracker.js',
    'net_interpolation.js', 'net_auth_server.js',
    'tdscript_runtime.js', 'td_client_bootstrap.js',
  ];
  for (const name of RUNTIME_FILES) {
    const src = path.join(merged.web_dir, name);
    if (isFile(src)) fs.copyFileSync(src, path.join(runtimeDir, name));
  }

  // Stage game files.
  const gameStage = path.join(stagingDir, 'game');
  fs.mkdirSync(gameStage, { recursive: true });
  copyDir(gameDir, gameStage);

  // Stage icon if provided.
  let iconSource = 'host.exe';
  if (merged.icon) {
    const iconPath = resolvePath(merged.icon);
    if (!isFile(iconPath)) {
      err(`Icon file not found: ${iconPath}`);
      return 1;
    }
    fs.copyFileSync(iconPath, path.join(stagingDir, 'app.ico'));
    iconSource = path.join(stagingDir, 'app.ico');
  }

  // Render .iss from the template.
  const templatePath = path.join(engineRoot, 'installer', 'td-game-template.iss');
  if (!isFile(templatePath)) {
    err(`Installer template not found: ${templatePath}`);
    return 1;
  }
  const issPath = path.join(stagingDir, `${merged.id}.iss`);
  renderIss(templatePath, issPath, {
    APP_NAME: merged.name,
    APP_VERSION: merged.version,
    APP_PUBLISHER: merged.publisher,
    APP_ID: merged.id,
    APP_EXE: targetExeName,
    APP_ICON: iconSource,
    APP_URL: merged.url,
  }, {
    bundleRuntime: merged.bundle_runtime,
    licenseFile: merged.license ? resolvePath(merged.license) : null,
    readmeFile: merged.readme ? resolvePath(merged.readme) : null,
    stagingDir,
  });

  // Write a bundle.json sidecar with the resolved config (useful for
  // debugging + lets the user see what was actually used).
  const sidecar = {
    name: merged.name,
    version: merged.version,
    description: merged.description,
    publisher: merged.publisher,
    id: merged.id,
    icon: merged.icon,
    url: merged.url,
    out: outPath,
    bundle_runtime: merged.bundle_runtime,
    staged_at: new Date().toISOString(),
  };
  fs.writeFileSync(path.join(stagingDir, 'bundle.json'),
    JSON.stringify(sidecar, null, 2) + '\n', 'utf-8');

  if (merged.dry_run) {
    info('Dry run — not invoking ISCC.');
    info(`  Staging: ${stagingDir}`);
    info(`  Script:  ${issPath}`);
    info(`  To build: ISCC "${issPath}"`);
    return 0;
  }

  // Try to invoke ISCC directly (Windows path lookup).
  const isccPath = merged.iscc || findIscc();
  if (!isccPath) {
    err('ISCC.exe not found. Install Inno Setup 6+ from https://jrsoftware.org/isdl.php');
    err(`  Or pass --iscc /path/to/ISCC.exe`);
    err('  (Staging + .iss are ready; you can compile them manually.)');
    return 1;
  }
  info(`Invoking ISCC: ${isccPath}`);
  const code = await spawnInherit(isccPath, [issPath]);
  if (code !== 0) {
    err(`ISCC failed with exit code ${code}`);
    return code;
  }

  // ISCC writes <id>-setup.exe next to the staging dir.
  const produced = path.join(path.dirname(stagingDir), `${merged.id}-setup.exe`);
  if (isFile(produced)) {
    fs.copyFileSync(produced, outPath);
    ok(`Installer: ${outPath} (${(fs.statSync(outPath).size / 1024 / 1024).toFixed(1)} MB)`);
  } else {
    err(`Setup .exe not found after ISCC. Expected: ${produced}`);
    return 1;
  }

  if (!merged.keep_staging) {
    rmrf(stagingDir);
  }
  return 0;
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

function slugify(name) {
  return String(name).toLowerCase().trim()
    .replace(/[^a-z0-9]+/g, '-')
    .replace(/^-+|-+$/g, '') || 'td-game';
}

function exeNameFor(name) {
  const s = String(name).replace(/[^A-Za-z0-9]+/g, '');
  return (s || 'TDGame') + '.exe';
}

function findIscc() {
  // Try `which` package if installed.
  let whichFn = null;
  try { whichFn = require('which'); } catch {}
  if (whichFn) {
    try { return whichFn.sync('ISCC'); } catch {}
  }
  // Fallback to PATH search via shell.
  try {
    const { spawnSync } = require('child_process');
    const cmd = process.platform === 'win32' ? 'where' : 'which';
    const r = spawnSync(cmd, ['ISCC'], { stdio: 'pipe' });
    if (r.status === 0 && r.stdout) {
      const p = r.stdout.toString().split(/\r?\n/)[0].trim();
      if (p && isFile(p)) return p;
    }
  } catch {}
  // Common Windows install paths.
  const candidates = [
    'C:\\Program Files (x86)\\Inno Setup 6\\ISCC.exe',
    'C:\\Program Files\\Inno Setup 6\\ISCC.exe',
  ];
  for (const c of candidates) if (isFile(c)) return c;
  return null;
}

// Minimal .iss templating — same placeholder syntax as bundle.py.
// We re-implement here so the Node fallback works without Python.
function renderIss(templatePath, outPath, subst, opts) {
  let text = fs.readFileSync(templatePath, 'utf-8');

  // Conditional blocks: {{NAME_BEGIN}}...{{NAME_END}}
  text = text.replace(/\{\{([A-Z0-9_]+)_BEGIN\}\}([\s\S]*?)\{\{\1_END\}\}/g,
    (m, name, body) => {
      if (name === 'WEBVIEW2_CHECK_GUARD') {
        return opts.bundleRuntime ? '' : body;
      }
      return body;
    });

  // Simple placeholders.
  for (const [k, v] of Object.entries(subst)) {
    text = text.split(`{{${k}}}`).join(v);
  }

  // WebView2 bootstrapper lines.
  if (opts.bundleRuntime) {
    text = text.split('{{WEBVIEW2_BOOTSTRAPPER_LINE}}').join(
      'WebView2Installer=webview2\\MicrosoftEdgeWebview2Setup.exe\n' +
      'WebView2InstallerArgs=/silent /install');
    text = text.split('{{WEBVIEW2_FILES_LINE}}').join(
      'Source: "webview2\\MicrosoftEdgeWebview2Setup.exe"; DestDir: "{tmp}"; ' +
      'Flags: deleteafterinstall');
  } else {
    text = text.split('{{WEBVIEW2_BOOTSTRAPPER_LINE}}').join('');
    text = text.split('{{WEBVIEW2_FILES_LINE}}').join('');
  }

  // License + readme.
  if (opts.licenseFile && isFile(opts.licenseFile)) {
    text = text.split('{{LICENSE_FILE_LINE}}').join(
      `Source: "${path.basename(opts.licenseFile)}"; DestDir: "{app}"; ` +
      `Flags: ignoreversion; Components: docs`);
  } else {
    text = text.split('{{LICENSE_FILE_LINE}}').join('; (no license file)');
  }
  if (opts.readmeFile && isFile(opts.readmeFile)) {
    text = text.split('{{README_FILE_LINE}}').join(
      `Source: "${path.basename(opts.readmeFile)}"; DestDir: "{app}"; ` +
      `Flags: ignoreversion isreadme; Components: docs`);
  } else {
    text = text.split('{{README_FILE_LINE}}').join('; (no readme file)');
  }

  // OutputDir.
  const outDir = path.dirname(outPath);
  text = text.split('OutputBaseFilename=').join(`OutputDir=${outDir}\nOutputBaseFilename=`);

  fs.writeFileSync(outPath, text.replace(/\r?\n/g, '\r\n'), 'utf-8');
}

module.exports = { run, help };
