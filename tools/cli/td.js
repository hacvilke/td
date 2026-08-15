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
//   bundle -path DIR -config FILE   Build a Windows installer (.exe) via Inno Setup
//   version, --version, -v     Print engine version
//   help, --help, -h [command] Show help
//
// Design:
//   - Zero npm dependencies. Pure Node.js (fs, path, http, child_process).
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
    if (tok.startsWith('--') || tok.startsWith('-')) {
      // Strip leading dashes for the key.
      const eq = tok.indexOf('=');
      if (eq >= 0) {
        const key = tok.slice(tok.startsWith('--') ? 2 : 1, eq);
        opts[key] = tok.slice(eq + 1);
      } else {
        const key = tok.slice(tok.startsWith('--') ? 2 : 1);
        const next = argv[i + 1];
        if (next !== undefined && !next.startsWith('-')) {
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
  if (!fs.existsSync(file)) return null;
  try {
    return require(file);
  } catch (e) {
    console.error(`[td] Failed to load command "${name}": ${e.message}`);
    return null;
  }
}

function printRootHelp() {
  console.log(`
TD Engine — CLI v${ENGINE_VERSION}

Usage:
  td <command> [args] [options]

Commands:
  init   [name]                    Scaffold a new game folder
  serve  [path] [--port N]         Start dev server with live reload
  build  [path]                    Build WASM + bundle web assets
  test   [pattern]                 Run tests (JS + C++)
  bundle -path DIR -config FILE    Build a Windows installer via Inno Setup
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
  td bundle -path ./my-game -config bundle.json

Environment:
  TD_ENGINE_ROOT    Path to the engine repo (default: parent of bin/)
  EMCC              Path to emcc (default: emcc on PATH)
  ISCC              Path to Inno Setup compiler (default: ISCC on PATH)
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
      if (cmd && typeof cmd.help === 'function') {
        cmd.help();
      } else {
        console.log(`No help available for "${args[0]}".`);
      }
    } else {
      printRootHelp();
    }
    process.exit(0);
  }

  const cmd = loadCommand(cmdName);
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
