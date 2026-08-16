#!/usr/bin/env node
// =============================================================================
// TD Engine — Command-line interface
//
// Usage:
//   td <command> [args] [options]
//
// Commands:
//   init   [name]              Scaffold a new game folder
//   serve  [path] [--port N]   Start dev server with live reload
//   build  [path]              Build WASM + bundle web assets
//   test   [pattern]           Run tests (JS + C++)
//   script <subcmd> [args]     Compile / check / run TDScript (.td) files
//   bundle -path DIR -config FILE   Build a Windows installer (.exe) via Inno Setup
//   deploy -path DIR -config FILE   Ship a built game to gh-pages / gh-release / static / zip
//   version, --version, -v     Print engine version
//   help, --help, -h [command] Show help
//
// Design:
//   - Minimal npm dependencies (only `ws` for the dev-server live-reload +
//     game-net server; everything else is pure Node.js fs/path/http/child_process).
//   - Each command lives in tools/cli/commands/<name>.js and exports a
//     function: (args, opts) => Promise<exitCode>.
//   - The dispatcher parses --flag=value and --flag value into opts; bare
//     positionals into args. Commands receive both.
// =============================================================================

'use strict';

const path = require('path');
const fs = require('fs');

const ENGINE_VERSION = '0.1.0';

const COMMANDS_DIR = path.join(__dirname, 'commands');

const COMMAND_ALIASES = {
  '-v': 'version',
  '--version': 'version',
  '-h': 'help',
  '--help': 'help',
};

// ---------------------------------------------------------------------------
// Argument parser — splits argv into (args, opts)
//
//   td serve ./my-game --port 8080 --verbose
//   args = ['./my-game']
//   opts = { port: '8080', verbose: true }
//
// Supports three forms:
//   --flag value       (next token is the value, unless it starts with -)
//   --flag=value       (inline)
//   --flag             (boolean true)
// ---------------------------------------------------------------------------
function parseArgv(argv) {
  const args = [];
  const opts = {};
  for (let i = 0; i < argv.length; i++) {
    const tok = argv[i];
    if (tok.startsWith('--') || (tok.startsWith('-') && tok.length > 1)) {
      // Strip leading dashes for the key.
      const eq = tok.indexOf('=');
      if (eq >= 0) {
        const key = tok.slice(tok.startsWith('--') ? 2 : 1, eq);
        opts[key] = tok.slice(eq + 1);
      } else {
        const key = tok.slice(tok.startsWith('--') ? 2 : 1);
        // Support --no-FLAG form → opts[FLAG] = false
        if (key.startsWith('no-')) {
          opts[key.slice(3)] = false;
          continue;
        }
        const next = argv[i + 1];
        // Treat next token as a value if it exists and isn't itself a flag.
        // (Negative numbers and other dashed strings are treated as flags,
        // which is the safer default — use --flag=value to pass such values.)
        if (next !== undefined && !next.startsWith('--') && !(next.startsWith('-') && next.length > 1 && !/^-?\d/.test(next))) {
          opts[key] = next;
          i++;
        } else {
          opts[key] = true;
        }
      }
    } else {
      args.push(tok);
    }
  }
  return { args, opts };
}

function loadCommand(name) {
  const alias = COMMAND_ALIASES[name];
  if (alias) name = alias;
  const file = path.join(COMMANDS_DIR, `${name}.js`);
  if (!fs.existsSync(file)) return null;  // file genuinely doesn't exist
  try {
    return require(file);
  } catch (e) {
    console.error(`[td] Command "${name}" failed to load: ${e.message}`);
    return { __loadError: true, error: e };
  }
}

function printRootHelp() {
  console.log(`
TD Engine — CLI v${ENGINE_VERSION}

Usage:
  td <command> [args] [options]

Commands:
  init   [name]                    Scaffold a new game folder
  serve  [path] [--port N]         Start dev server with live reload + game-net server
  build  [path]                    Build WASM + bundle web assets
  test   [pattern]                 Run tests (JS + C++)
  script <compile|check|run>       Compile / check / run TDScript (.td) files
  bundle -path DIR -config FILE    Build a Windows installer via Inno Setup
  deploy -path DIR -config FILE    Ship a built game (gh-pages/gh-release/static/zip)
  version                          Print engine version
  help    [command]                Show help for a command

Options:
  --quiet, -q                      Less output
  --verbose                        More output
  --no-color                       Disable ANSI colors
  --version, -v                    Print version
  --help, -h [command]             Show help

Examples:
  td init my-game
  td serve my-game --port 8080
  td build my-game
  td test
  td script compile src/server/server_main.td -o build/server_main.js
  td script run src/server/server_main.td --main ServerMain
  td bundle -path ./my-game -config bundle.json
  td deploy -path ./dist -config deploy.json --target gh-pages
  td deploy -path . -config bundle.json --target gh-release --artifact Setup.exe

Environment:
  TD_ENGINE_ROOT    Path to the engine repo (default: auto-detected from this
                    script's location, two directories up)
  EMCC              Path to emcc (default: emcc on PATH)
  ISCC              Path to Inno Setup compiler (default: ISCC on PATH)
  GH_TOKEN          GitHub auth token for 'td deploy --target gh-release'
`);
}

function main() {
  const argv = process.argv.slice(2);
  if (argv.length === 0) {
    printRootHelp();
    process.exit(0);
  }

  const cmdName = argv[0];
  const rest = argv.slice(1);
  const { args, opts } = parseArgv(rest);

  // Built-in pseudo-commands.
  if (cmdName === 'version' || cmdName === '--version' || cmdName === '-v') {
    console.log(ENGINE_VERSION);
    process.exit(0);
  }
  if (cmdName === 'help' || cmdName === '--help' || cmdName === '-h') {
    if (args.length > 0) {
      const cmd = loadCommand(args[0]);
      if (cmd && cmd.__loadError) {
        console.error(`[td] Cannot show help for "${args[0]}" — the command failed to load (see error above).`);
        process.exit(1);
      }
      if (cmd && typeof cmd.help === 'function') {
        cmd.help();
      } else if (!cmd) {
        console.log(`No help available for "${args[0]}". (Unknown command — run \`td help\` for the list.)`);
      } else {
        console.log(`No help available for "${args[0]}".`);
      }
    } else {
      printRootHelp();
    }
    process.exit(0);
  }

  const cmd = loadCommand(cmdName);
  if (cmd && cmd.__loadError) {
    console.error(`[td] Command "${cmdName}" exists but failed to load — see error above.`);
    process.exit(2);
  }
  if (!cmd) {
    console.error(`[td] Unknown command: "${cmdName}"`);
    console.error('Run `td help` for a list of commands.');
    process.exit(2);
  }

  if (typeof cmd.run !== 'function') {
    console.error(`[td] Command "${cmdName}" is missing a run() export.`);
    process.exit(2);
  }

  // Merge process.env-derived defaults into opts.
  if (process.env.TD_ENGINE_ROOT && !opts.engineRoot) {
    opts.engineRoot = process.env.TD_ENGINE_ROOT;
  }

  Promise.resolve(cmd.run(args, opts))
    .then((code) => {
      process.exit(typeof code === 'number' ? code : 0);
    })
    .catch((err) => {
      console.error(`[td] Command "${cmdName}" failed: ${err && err.stack ? err.stack : err}`);
      process.exit(1);
    });
}

main();
