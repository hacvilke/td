'use strict';

// td init [name]
//
// Scaffolds a new game folder with:
//   index.html          — minimal HTML host
//   game.js             — minimal game logic (creates entity, draws sprite)
//   bundle.json         — installer config (name, version, icon, ...)
//   README.txt          — short orientation
//
// If --template <name> is given, uses tools/cli/templates/<name>/ instead of
// the default template. Built-in templates: "minimal" (default).

const fs = require('fs');
const path = require('path');
const {
  findEngineRoot, ok, info, warn, err,
  isDir, copyDir, resolvePath, COLORS,
} = require('../lib/util');

function help() {
  console.log(`
td init [name] [--template NAME]

Scaffolds a new game folder.

Arguments:
  name              Folder name (default: my-game)

Options:
  --template NAME   Template to use (default: minimal). Available: minimal
  --here            Use the current directory instead of creating a subfolder

Examples:
  td init my-cool-game
  td init my-cool-game --template minimal
`);
}

async function run(args, opts) {
  const name = args[0] || 'my-game';
  const templateName = opts.template || 'minimal';
  const here = !!opts.here;

  const engineRoot = findEngineRoot();
  if (!engineRoot) {
    err('Could not locate engine root. Set TD_ENGINE_ROOT env var.');
    return 1;
  }

  const templateDir = path.join(engineRoot, 'tools', 'cli', 'templates', templateName);
  if (!isDir(templateDir)) {
    err(`Template not found: ${templateName} (looked in ${templateDir})`);
    return 1;
  }

  const target = here ? process.cwd() : resolvePath(name);
  if (!here && fs.existsSync(target)) {
    err(`Target already exists: ${target}`);
    return 1;
  }

  info(`Scaffolding "${name}" from template "${templateName}" into ${target}`);
  copyDir(templateDir, target);

  // Substitute the project name into the template placeholders.
  patchFile(path.join(target, 'bundle.json'), (s) =>
    s.replace(/__GAME_NAME__/g, name)
     .replace(/__GAME_ID__/g, slugify(name))
  );
  patchFile(path.join(target, 'project.td'), (s) =>
    s.replace(/__GAME_NAME__/g, name)
     .replace(/__GAME_ID__/g, slugify(name))
  );
  patchFile(path.join(target, 'index.html'), (s) =>
    s.replace(/__GAME_NAME__/g, name)
  );
  patchFile(path.join(target, 'README.txt'), (s) =>
    s.replace(/__GAME_NAME__/g, name)
  );
  patchFile(path.join(target, 'game.js'), (s) =>
    s.replace(/__GAME_NAME__/g, name)
  );

  ok(`Created game at: ${target}`);
  info('Next steps:');
  info(`  cd ${path.relative(process.cwd(), target) || '.'}`);
  info('  td serve .');
  return 0;
}

function patchFile(p, fn) {
  if (!fs.existsSync(p)) return;
  const src = fs.readFileSync(p, 'utf-8');
  fs.writeFileSync(p, fn(src), 'utf-8');
}

function slugify(name) {
  return name.toLowerCase().trim()
    .replace(/[^a-z0-9]+/g, '-')
    .replace(/^-+|-+$/g, '') || 'td-game';
}

module.exports = { run, help };
