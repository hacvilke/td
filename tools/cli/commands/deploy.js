'use strict';

// td deploy -path DIR -config FILE [--target gh-pages|gh-release|static|zip]
//                                [--dry-run] [--out FILE]
//
// Ships a built game (produced by `td build` or `td bundle`) to a place
// players can reach. Distinct from `td bundle` (which produces the artifact
// locally). Deploy never rebuilds — it only ships.
//
// Targets:
//   gh-pages    Push the dist/ web build to the repo's gh-pages branch.
//               Live at https://<owner>.github.io/<repo>/
//   gh-release  Upload the .exe installer to a GitHub Release.
//               Live at https://github.com/<owner>/<repo>/releases/tag/<tag>
//   static      rsync the dist/ to a user-provided server over SSH.
//               Live at the user-provided URL.
//   zip         Produce a .zip locally. No network. For manual upload.
//
// Config (deploy.json or bundle.json's "deploy" field):
//   {
//     "target":   "gh-pages",       // required
//     "repo":     "owner/repo",     // gh-pages, gh-release
//     "branch":   "gh-pages",       // gh-pages only, default "gh-pages"
//     "releaseTag": "v1.0.0",       // gh-release only, default "v"+version
//     "sshHost":  "user@host",      // static only
//     "sshPath":  "/var/www/game",  // static only
//     "zipOut":   "./my-game.zip",  // zip only, default "./<id>-<target>.zip"
//     "token":    "",               // gh-release only, default $GH_TOKEN env
//     "message":  "deploy v1.0.0"   // commit message (gh-pages) or release notes
//   }
//
// CLI overrides: --target, --repo, --branch, --release-tag, --ssh-host,
// --ssh-path, --zip-out, --token, --message, --dry-run, --out

const fs = require('fs');
const path = require('path');
const { execFileSync, spawnSync } = require('child_process');
const {
  findEngineRoot, ok, info, warn, err,
  isFile, isDir, readJson, resolvePath, spawnInherit, COLORS,
} = require('../lib/util');

function help() {
  console.log(`
td deploy -path DIR -config FILE [--target TARGET] [options]

Ships a built game to where players can reach it. Does NOT rebuild —
run \`td build\` (for web) or \`td bundle\` (for Windows installer) first.

Required:
  -path DIR         Game folder (must contain index.html for web targets,
                    or the .exe for gh-release/zip if --artifact is given)
  -config FILE      JSON config file (see below)

Targets (--target or config.target):
  gh-pages    Push dist/ to the repo's gh-pages branch.
              Live at https://<owner>.github.io/<repo>/
  gh-release  Upload the .exe installer to a GitHub Release.
              Live at https://github.com/<owner>/<repo>/releases/tag/<tag>
  static      rsync dist/ to user@host:path over SSH.
  zip         Produce a .zip locally. No network.

Config file (JSON):
  {
    "target":     "gh-pages",
    "repo":       "owner/repo",          // gh-pages, gh-release
    "branch":     "gh-pages",            // gh-pages, default "gh-pages"
    "releaseTag": "v1.0.0",              // gh-release, default "v"+version
    "sshHost":    "user@host",           // static
    "sshPath":    "/var/www/game",       // static
    "zipOut":     "./my-game.zip",       // zip, default "./<id>-<target>.zip"
    "token":      "",                    // gh-release, default $GH_TOKEN
    "message":    "deploy v1.0.0",       // commit msg / release notes
    "artifact":   "./MyGame-Setup.exe"   // gh-release, zip (override -path)
  }

CLI overrides:
  --target, --repo, --branch, --release-tag, --ssh-host, --ssh-path,
  --zip-out, --token, --message, --artifact, --dry-run, --out

Examples:
  td deploy -path ./dist -config deploy.json
  td deploy -path ./dist -config deploy.json --target gh-pages --dry-run
  td deploy -path . -config bundle.json --target gh-release --artifact Setup.exe
  td deploy -path ./dist -config deploy.json --target zip --out game.zip
`);
}

async function run(args, opts) {
  // ---- Required args -----------------------------------------------------
  const gameDirRaw = opts.path || opts.p;
  if (!gameDirRaw) {
    err('Missing -path. See `td help deploy`.');
    return 1;
  }
  const gameDir = resolvePath(gameDirRaw);
  if (!isDir(gameDir)) {
    err(`Folder not found: ${gameDir}`);
    return 1;
  }

  const configFileRaw = opts.config || opts.c;
  let cfg = {};
  if (configFileRaw) {
    const configFile = resolvePath(configFileRaw);
    if (!isFile(configFile)) {
      err(`Config file not found: ${configFile}`);
      return 1;
    }
    cfg = readJson(configFile) || {};
    // If the config is a bundle.json, look for its "deploy" sub-object.
    if (cfg.deploy && typeof cfg.deploy === 'object' && !cfg.target) {
      cfg = Object.assign({}, cfg, cfg.deploy);
    }
  }

  // ---- Merge config + CLI overrides -------------------------------------
  const target = opts.target || cfg.target;
  if (!target) {
    err('No deploy target. Pass --target <gh-pages|gh-release|static|zip> or set "target" in config.');
    return 1;
  }
  const valid = ['gh-pages', 'gh-release', 'static', 'zip'];
  if (!valid.includes(target)) {
    err(`Unknown target "${target}". Valid: ${valid.join(', ')}`);
    return 1;
  }

  const merged = {
    target,
    repo:         opts.repo         || cfg.repo         || guessRepoFromGit(),
    branch:       opts.branch       || cfg.branch       || 'gh-pages',
    releaseTag:   opts['release-tag'] || cfg.releaseTag || null,
    sshHost:      opts['ssh-host']  || cfg.sshHost      || null,
    sshPath:      opts['ssh-path']  || cfg.sshPath      || null,
    zipOut:       opts['zip-out']   || cfg.zipOut       || opts.out || null,
    token:        opts.token        || cfg.token        || process.env.GH_TOKEN || process.env.GITHUB_TOKEN || null,
    message:      opts.message      || cfg.message      || `deploy ${new Date().toISOString()}`,
    artifact:     opts.artifact     || cfg.artifact     || null,
    version:      opts.version      || cfg.version      || '1.0.0',
    id:           opts.id           || cfg.id           || path.basename(gameDir),
    dryRun:       opts['dry-run']   === true || cfg.dry_run === true,
  };

  // ---- Dispatch ----------------------------------------------------------
  info(`Deploy target: ${target}${merged.dryRun ? ' (DRY RUN)' : ''}`);
  if (merged.dryRun) warn('Dry run — no network calls will be made.');

  switch (target) {
    case 'gh-pages':   return deployGhPages(gameDir, merged);
    case 'gh-release': return deployGhRelease(gameDir, merged);
    case 'static':     return deployStatic(gameDir, merged);
    case 'zip':        return deployZip(gameDir, merged);
  }
  return 1;
}

// ---------------------------------------------------------------------------
// gh-pages: push dist/ to the repo's gh-pages branch
// ---------------------------------------------------------------------------

async function deployGhPages(gameDir, cfg) {
  if (!cfg.repo) {
    err('gh-pages needs --repo owner/repo (or set "repo" in config, or run from a git repo with a github remote).');
    return 1;
  }
  if (!isFile(path.join(gameDir, 'index.html'))) {
    err(`gh-pages needs an index.html in -path. Got: ${gameDir}`);
    err('Run `td build` first to produce the web dist.');
    return 1;
  }
  if (!commandExists('git')) {
    err('git not found on PATH. Install git to use gh-pages deploy.');
    return 1;
  }

  info(`Repo: ${cfg.repo}, branch: ${cfg.branch}`);

  // Use a temp dir for the gh-pages checkout.
  const tmpDir = path.join(require('os').tmpdir(), `td-deploy-ghpages-${process.pid}`);
  if (isDir(tmpDir)) fs.rmSync(tmpDir, { recursive: true, force: true });
  fs.mkdirSync(tmpDir, { recursive: true });

  // Clone the gh-pages branch (or empty if it doesn't exist yet).
  const cloneUrl = `https://${cfg.token ? cfg.token + '@' : ''}github.com/${cfg.repo}.git`;
  info(`Cloning ${cfg.repo} (${cfg.branch}) to ${tmpDir}...`);
  let cloneResult;
  if (cfg.dryRun) {
    info('[dry-run] would: git clone --depth 1 --branch ' + cfg.branch + ' ' + cloneUrl);
    cloneResult = { status: 0 };
  } else {
    // Try cloning the branch; if it doesn't exist, clone main and create an orphan branch.
    cloneResult = spawnSync('git', ['clone', '--depth', '1', '--branch', cfg.branch, cloneUrl, tmpDir], { stdio: 'pipe' });
    if (cloneResult.status !== 0) {
      info(`Branch ${cfg.branch} doesn't exist yet — creating it as an orphan.`);
      cloneResult = spawnSync('git', ['clone', '--depth', '1', cloneUrl, tmpDir], { stdio: 'pipe' });
      if (cloneResult.status !== 0) {
        err('git clone failed: ' + (cloneResult.stderr ? cloneResult.stderr.toString() : 'unknown'));
        return 1;
      }
      const r = spawnSync('git', ['checkout', '--orphan', cfg.branch], { cwd: tmpDir, stdio: 'pipe' });
      if (r.status !== 0) {
        err('git checkout --orphan failed: ' + (r.stderr ? r.stderr.toString() : 'unknown'));
        return 1;
      }
      // Clear the working tree (we're starting fresh).
      fs.readdirSync(tmpDir).forEach(name => {
        if (name === '.git') return;
        const p = path.join(tmpDir, name);
        fs.rmSync(p, { recursive: true, force: true });
      });
    }
  }
  if (cfg.dryRun) {
    info(`[dry-run] would: copy ${gameDir}/* into ${tmpDir}/`);
    info(`[dry-run] would: git add -A && git commit -m "${cfg.message}" && git push origin ${cfg.branch}`);
    ok(`[dry-run] would be live at https://${cfg.repo.split('/')[0]}.github.io/${cfg.repo.split('/')[1]}/`);
    return 0;
  }

  // Replace the contents of tmpDir (except .git) with gameDir.
  fs.readdirSync(tmpDir).forEach(name => {
    if (name === '.git') return;
    const p = path.join(tmpDir, name);
    fs.rmSync(p, { recursive: true, force: true });
  });
  copyDirContents(gameDir, tmpDir);

  // Commit + push.
  const git = (args) => spawnSync('git', args, { cwd: tmpDir, stdio: 'pipe' });
  git(['add', '-A']);
  const commit = git(['commit', '-m', cfg.message, '--allow-empty']);
  if (commit.status !== 0) {
    warn('git commit had nothing to commit (no changes since last deploy).');
  }
  const push = git(['push', 'origin', cfg.branch]);
  if (push.status !== 0) {
    err('git push failed: ' + (push.stderr ? push.stderr.toString() : 'unknown'));
    return 1;
  }

  const [owner, repo] = cfg.repo.split('/');
  ok(`Deployed to https://${owner}.github.io/${repo}/`);
  ok('(GitHub Pages may take 30-60s to rebuild.)');
  return 0;
}

// ---------------------------------------------------------------------------
// gh-release: upload .exe to a GitHub Release
// ---------------------------------------------------------------------------

async function deployGhRelease(gameDir, cfg) {
  if (!cfg.repo) {
    err('gh-release needs --repo owner/repo.');
    return 1;
  }
  if (!cfg.token) {
    err('gh-release needs a GitHub token. Set GH_TOKEN env var or pass --token.');
    return 1;
  }
  // Find the .exe to upload.
  let artifact = cfg.artifact ? resolvePath(cfg.artifact) : null;
  if (!artifact) {
    // Look for *-setup.exe in gameDir.
    const candidates = fs.readdirSync(gameDir).filter(n => /-setup\.exe$/i.test(n));
    if (candidates.length === 0) {
      err(`No .exe found in ${gameDir}. Run \`td bundle\` first, or pass --artifact.`);
      return 1;
    }
    artifact = path.join(gameDir, candidates[0]);
  }
  if (!isFile(artifact)) {
    err(`Artifact not found: ${artifact}`);
    return 1;
  }

  const tag = cfg.releaseTag || ('v' + cfg.version);
  const [owner, repo] = cfg.repo.split('/');
  const apiBase = `https://api.github.com/repos/${owner}/${repo}`;
  info(`Repo: ${cfg.repo}, tag: ${tag}`);
  info(`Artifact: ${artifact} (${(fs.statSync(artifact).size / 1024 / 1024).toFixed(1)} MB)`);

  if (cfg.dryRun) {
    info(`[dry-run] would: GET ${apiBase}/releases/tags/${tag}`);
    info(`[dry-run] would: POST ${apiBase}/releases (if missing)`);
    info(`[dry-run] would: upload ${path.basename(artifact)} as release asset`);
    ok(`[dry-run] would be live at https://github.com/${owner}/${repo}/releases/tag/${tag}`);
    return 0;
  }

  // Step 1: look up existing release by tag.
  let releaseId = null;
  let uploadUrl = null;
  const lookup = spawnSync('curl', ['-sL', '-H', `Authorization: token ${cfg.token}`,
    `${apiBase}/releases/tags/${tag}`], { stdio: 'pipe' });
  if (lookup.status === 0 && lookup.stdout) {
    try {
      const obj = JSON.parse(lookup.stdout.toString());
      if (obj && obj.id) {
        releaseId = obj.id;
        uploadUrl = (obj.upload_url || '').replace(/\{.*\}/, '');
        info(`Found existing release id=${releaseId}`);
      }
    } catch { /* ignore */ }
  }

  // Step 2: create release if missing.
  if (!releaseId) {
    info(`Creating release ${tag}...`);
    const body = JSON.stringify({
      tag_name: tag,
      name: tag,
      body: cfg.message || `Release ${tag}`,
      draft: false,
      prerelease: false,
    });
    const create = spawnSync('curl', ['-sL', '-X', 'POST',
      '-H', `Authorization: token ${cfg.token}`,
      '-H', 'Content-Type: application/json',
      '-d', body,
      `${apiBase}/releases`], { stdio: 'pipe' });
    if (create.status !== 0 || !create.stdout) {
      err('Create release failed: ' + (create.stderr ? create.stderr.toString() : 'no output'));
      return 1;
    }
    try {
      const obj = JSON.parse(create.stdout.toString());
      if (!obj || !obj.id) {
        err('Create release returned no id. Response: ' + create.stdout.toString().slice(0, 500));
        return 1;
      }
      releaseId = obj.id;
      uploadUrl = (obj.upload_url || '').replace(/\{.*\}/, '');
    } catch (e) {
      err('Failed to parse create-release response: ' + e.message);
      return 1;
    }
  }

  if (!uploadUrl) {
    err('Could not determine upload URL from release.');
    return 1;
  }

  // Step 3: upload the asset.
  const assetName = path.basename(artifact);
  const uploadUrlWithName = `${uploadUrl}?name=${encodeURIComponent(assetName)}`;
  info(`Uploading ${assetName}...`);
  const upload = await spawnInherit('curl', ['-sL', '-X', 'POST',
    '-H', `Authorization: token ${cfg.token}`,
    '-H', 'Content-Type: application/octet-stream',
    '--data-binary', `@${artifact}`,
    uploadUrlWithName]);
  if (upload !== 0) {
    err(`Upload failed with code ${upload}`);
    return upload;
  }

  ok(`Deployed to https://github.com/${owner}/${repo}/releases/tag/${tag}`);
  return 0;
}

// ---------------------------------------------------------------------------
// static: rsync dist/ to user@host:path over SSH
// ---------------------------------------------------------------------------

async function deployStatic(gameDir, cfg) {
  if (!cfg.sshHost || !cfg.sshPath) {
    err('static deploy needs --ssh-host user@host and --ssh-path /path/on/server');
    return 1;
  }
  if (!isFile(path.join(gameDir, 'index.html'))) {
    err(`static deploy needs an index.html in -path. Got: ${gameDir}`);
    return 1;
  }
  if (!commandExists('rsync')) {
    err('rsync not found on PATH. Install rsync to use static deploy.');
    return 1;
  }

  const dest = `${cfg.sshHost}:${cfg.sshPath}`;
  info(`rsync ${gameDir}/ -> ${dest}`);

  if (cfg.dryRun) {
    info(`[dry-run] would: rsync -az --delete ${gameDir}/ ${dest}`);
    ok(`[dry-run] would be live at the server URL you provided.`);
    return 0;
  }

  const code = await spawnInherit('rsync', ['-az', '--delete',
    path.join(gameDir, '') + '/', dest]);
  if (code !== 0) {
    err(`rsync failed with code ${code}`);
    return code;
  }
  ok(`Deployed to ${dest}`);
  return 0;
}

// ---------------------------------------------------------------------------
// zip: produce a .zip locally
// ---------------------------------------------------------------------------

async function deployZip(gameDir, cfg) {
  let outPath = cfg.zipOut
    ? resolvePath(cfg.zipOut)
    : path.join(process.cwd(), `${cfg.id}-deploy.zip`);
  fs.mkdirSync(path.dirname(outPath), { recursive: true });

  info(`Zipping ${gameDir} -> ${outPath}`);

  if (cfg.dryRun) {
    info(`[dry-run] would produce: ${outPath}`);
    return 0;
  }

  // Try `zip` command first (Linux/Mac).
  if (commandExists('zip')) {
    const code = await spawnInherit('zip', ['-r', '-q', outPath, '.'],
      { cwd: gameDir });
    if (code !== 0) {
      err(`zip command failed with code ${code}`);
      return code;
    }
  } else if (commandExists('tar')) {
    // Fallback: tar with gzip (produces .tar.gz — rename if user asked for .zip).
    const tgz = outPath.replace(/\.zip$/i, '.tar.gz');
    const code = await spawnInherit('tar', ['-czf', tgz, '.'], { cwd: gameDir });
    if (code !== 0) {
      err(`tar failed with code ${code}`);
      return code;
    }
    if (tgz !== outPath) {
      warn(`Produced ${tgz} (zip not available; tar fallback used).`);
      outPath = tgz;
    }
  } else {
    err('Neither `zip` nor `tar` found on PATH. Install one to use zip deploy.');
    return 1;
  }

  ok(`Produced: ${outPath} (${(fs.statSync(outPath).size / 1024 / 1024).toFixed(1)} MB)`);
  return 0;
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

function commandExists(cmd) {
  try {
    const which = process.platform === 'win32' ? 'where' : 'which';
    const r = spawnSync(which, [cmd], { stdio: 'pipe' });
    return r.status === 0;
  } catch {
    return false;
  }
}

function guessRepoFromGit() {
  try {
    const r = spawnSync('git', ['remote', 'get-url', 'origin'], { stdio: 'pipe' });
    if (r.status !== 0 || !r.stdout) return null;
    const url = r.stdout.toString().trim();
    // ssh: git@github.com:owner/repo.git
    // https: https://github.com/owner/repo.git
    const m = url.match(/github\.com[:/]([^\/]+)\/([^\/\s]+?)(?:\.git)?$/);
    return m ? `${m[1]}/${m[2]}` : null;
  } catch {
    return null;
  }
}

function copyDirContents(src, dst) {
  for (const name of fs.readdirSync(src)) {
    const s = path.join(src, name);
    const d = path.join(dst, name);
    const stat = fs.statSync(s);
    if (stat.isDirectory()) {
      fs.mkdirSync(d, { recursive: true });
      copyDirContents(s, d);
    } else if (stat.isFile()) {
      fs.copyFileSync(s, d);
    }
  }
}

module.exports = { run, help };
