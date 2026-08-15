'use strict';

// td test [pattern]
//
// Runs the engine's test suites.
//
// - JS tests:  tests/test_*.js — each file exports a run() function or
//   contains test_* functions; we run them with Node's built-in test runner
//   if available, otherwise a tiny custom runner.
// - C++ tests: tests/test_*.cpp — built via `make test` (desktop) or skipped
//   if no compiler is available.
//
// Pattern: a substring filter on the test file name.

const fs = require('fs');
const path = require('path');
const {
  findEngineRoot, ok, info, warn, err,
  isFile, isDir, walk, spawnInherit,
} = require('../lib/util');

function help() {
  console.log(`
td test [pattern] [--js-only] [--cpp-only]

Runs the engine's test suites.

Arguments:
  pattern           Substring filter on test file names (default: all)

Options:
  --js-only         Skip C++ tests
  --cpp-only        Skip JS tests
  --watch           Re-run on file change

Examples:
  td test
  td test net
  td test --js-only
`);
}

async function run(args, opts) {
  const pattern = args[0] || '';
  const jsOnly = !!opts['js-only'];
  const cppOnly = !!opts['cpp-only'];
  const engineRoot = findEngineRoot();
  if (!engineRoot) {
    err('Could not locate engine root.');
    return 1;
  }
  const testsDir = path.join(engineRoot, 'tests');
  if (!isDir(testsDir)) {
    err(`tests/ dir not found: ${testsDir}`);
    return 1;
  }

  let failures = 0;

  // ---- JS tests ----------------------------------------------------------
  if (!cppOnly) {
    const jsFiles = walk(testsDir, (n) => n.startsWith('test_') && n.endsWith('.js'))
      .filter((p) => !pattern || path.basename(p).includes(pattern));
    if (jsFiles.length === 0) {
      warn('No JS test files matched.');
    } else {
      info(`Running ${jsFiles.length} JS test file(s)...`);
      for (const file of jsFiles) {
        const rel = path.relative(engineRoot, file);
        process.stdout.write(`  ${rel} ... `);
        try {
          // Each test file is expected to call process.exit(0|1) on its own,
          // or return a Promise<exitCode> from its `run` export.
          const mod = require(file);
          if (typeof mod.run === 'function') {
            const code = await Promise.resolve(mod.run());
            if (code === 0 || code === undefined) {
              console.log('PASS');
            } else {
              console.log('FAIL');
              failures++;
            }
          }
        } catch (e) {
          console.log(`ERROR: ${e.message}`);
          failures++;
        }
      }
    }
  }

  // ---- C++ tests ---------------------------------------------------------
  if (!jsOnly) {
    const cppFiles = walk(testsDir, (n) => n.startsWith('test_') && n.endsWith('.cpp'))
      .filter((p) => !pattern || path.basename(p).includes(pattern));
    if (cppFiles.length === 0) {
      warn('No C++ test files matched.');
    } else {
      // Try to build + run via the Makefile `test` target.
      info('Building + running C++ tests (make test)...');
      const code = await spawnInherit('make', ['test'], { cwd: engineRoot });
      if (code !== 0) {
        err(`C++ tests failed (exit code ${code}).`);
        failures++;
      } else {
        ok('C++ tests passed.');
      }
    }
  }

  if (failures > 0) {
    err(`${failures} test group(s) failed.`);
    return 1;
  }
  ok('All tests passed.');
  return 0;
}

module.exports = { run, help };
