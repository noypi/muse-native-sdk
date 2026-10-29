'use strict';
// @muse/cli — argument parsing and dispatch for the muse-native binary plus supported muse alias.
// FR-WORKFLOW owns command options later; this file only routes argv.
const path = require('node:path');

const COMMANDS = ['init', 'build', 'dev', 'run', 'gallery-proof'];
const INVOKED = path.basename(process.argv[1] || '', '.js');

if (INVOKED === 'muse') {
  process.stderr.write(`Warning: '${INVOKED}' is deprecated, use 'muse-native' instead.\n`);
}

// Error thrown for unknown options, invalid values and incompatible
// combinations. Carries the offending `option` flag and the `accepted`
// values description. Message format keeps `Error: <option> ... Expected
// ...` so CLI stderr names the option and the accepted values.
class OptionError extends Error {
  constructor(option, accepted) {
    super(`Error: ${option}: Expected ${accepted}`);
    this.name = 'OptionError';
    this.option = option;
    this.accepted = accepted;
  }
}

function loadStages() {
  try {
    const stages = require('./stages.js').STAGES;
    if (Array.isArray(stages) && stages.length > 0) return stages;
  } catch (_) {
    // Fall through to the static list below.
  }
  return ['configure', 'typecheck', 'compile', 'emit', 'native-build', 'package', 'install'];
}

const ACCEPTED_OPTIONS = '--stage, --print-config, --name, --version, --help, --with-sample, --abi, --release, --modules, --output, --strict, --archs, --packaging, --keystore, --key-alias';

// Validate the option layer (`--` flags) of an argv array. Positional
// operands (the command, `<appDir>`, and the one target operand
// `examples/m3-gallery`) and `-h`/`--help` are skipped — help
// and `--print-config` handling is unaffected. Throws OptionError naming
// the option and the accepted values for: unknown options,
// `--flag=value` form
// (only `--<name> <value>` is accepted), a missing/invalid `--stage`
// value, and an invalid `--packaging` value (only `fat|split|both`).
// Never validates config keys — unknown CONFIG keys stay ConfigError
// (T1/T2).
function validateOptions(argv) {
  const args = Array.isArray(argv) ? argv : [];
  const STAGES = loadStages();
  const VALUE_FLAGS = new Set(['--stage', '--name', '--version', '--abi', '--modules', '--output', '--archs', '--packaging', '--keystore', '--key-alias']);
  const BOOL_FLAGS = new Set(['--print-config', '--help', '-h', '--with-sample', '--release', '--strict']);
  for (let i = 0; i < args.length; i++) {
    const arg = args[i];
    if (typeof arg !== 'string' || arg.length === 0) continue;
    if (!arg.startsWith('-')) continue;
    if (arg === '-' || arg === '--') {
      throw new OptionError(arg, `one of: ${ACCEPTED_OPTIONS}`);
    }
    if (!arg.startsWith('--')) {
      // Single-dash cluster: only -h is known.
      if (arg === '-h') continue;
      throw new OptionError(arg, `one of: ${ACCEPTED_OPTIONS}`);
    }
    const eq = arg.indexOf('=');
    if (eq !== -1) {
      // Only the space form (`--<name> <value>`) is accepted; `--flag=value`
      // is rejected here at the option layer so validation and execution
      // agree (the config/stage layers only read the space form).
      throw new OptionError(arg, `one of: ${ACCEPTED_OPTIONS}`);
    }
    const name = arg;
    if (!VALUE_FLAGS.has(name) && !BOOL_FLAGS.has(name)) {
      throw new OptionError(name, `one of: ${ACCEPTED_OPTIONS}`);
    }
    if (BOOL_FLAGS.has(name)) {
      continue;
    }
    // Value flag: `--<name> <value>` (space form only).
    let value;
    {
      const next = args[i + 1];
      if (typeof next !== 'string' || next.startsWith('-')) {
        if (name === '--stage') {
          throw new OptionError(name, `one of: ${STAGES.join(', ')}`);
        }
        throw new OptionError(name, 'a value');
      }
      value = next;
      i++;
    }
    if (name === '--stage') {
      if (!STAGES.includes(value)) {
        throw new OptionError(`--stage '${value}'`, `one of: ${STAGES.join(', ')}`);
      }
    }
    if (name === '--packaging') {
      if (!['fat', 'split', 'both'].includes(value)) {
        throw new OptionError('--packaging', 'one of: fat, split, both');
      }
    }
  }
}

function printHelp() {
  process.stdout.write(
    [
      'Usage: muse-native <init|build|dev|run|gallery-proof> [args]',
      '',
      'Commands:',
      '  init    scaffold a new project',
      '  build   build the project',
      '  dev     start the dev server',
      '  run     run the project',
      '  gallery-proof    run the gallery device proof',
      '',
      'Options:',
      '  -h, --help    show this help',
      '',
    ].join('\n'),
  );
}

function printGalleryProofHelp() {
  process.stdout.write(
    [
      'Usage: muse-native gallery-proof --tiers --themes --assert-counts --no-blank [--out <dir>] [--apk <path>]',
      '',
      'Options:',
      '  --tiers            check tiers',
      '  --themes           check themes',
      '  --assert-counts    assert row/page counts against the device',
      '  --no-blank         fail on blank/diagnostic-only viewports',
      '  --out <dir>        output directory for page screenshots',
      '  --apk <path>       gallery APK to install',
      '  -h, --help         show this help',
      '',
    ].join('\n'),
  );
}

function printUsageError(message) {
  process.stderr.write(`${message}\nUsage: muse-native <init|build|dev|run|gallery-proof> [args]\n`);
}

function printBuildHelp() {
  const { getOptionSchema } = require('./config.js');
  const schema = getOptionSchema();
  const lines = ['Usage: muse-native build <appDir> [options]', '', 'Options:'];
  for (const entry of schema) {
    let line = `  --${entry.name} <${entry.type}> (default: ${String(entry.default)})`;
    if (Array.isArray(entry.allowed)) {
      line += ` (allowed: ${entry.allowed.map(String).join(', ')})`;
    }
    lines.push(line);
  }
  lines.push('', '  -h, --help    show this help', '');
  process.stdout.write(lines.join('\n'));
}

function main() {
  const argv = process.argv.slice(2);
  if (argv[0] === 'gallery-proof') {
    if (argv.includes('--help') || argv.includes('-h')) {
      printGalleryProofHelp();
      process.exitCode = 0;
      return;
    }
    const proof = require('./gallery-proof.js');
    let opts;
    try {
      opts = proof.parseGalleryProofArgs(argv);
    } catch (err) {
      process.stderr.write(`${(err && err.message) || String(err)}\nUsage: muse-native gallery-proof --tiers --themes --assert-counts --no-blank [--out <dir>] [--apk <path>]\n`);
      process.exitCode = 2;
      return;
    }
    proof.runGalleryProof({
      serial: process.env.ANDROID_SERIAL,
      androidJar: process.env.ANDROID_JAR,
      outDir: opts.outDir,
      apkPath: opts.apkPath || undefined,
    }).then(
      (result) => {
        process.stdout.write(`gallery-proof ok: rows=${result.rows} pages=${result.pages} shots=${result.shots.length}\n`);
      },
      (err) => {
        process.stderr.write(`${(err && err.message) || String(err)}\n`);
        process.exitCode = 1;
      },
    );
    return;
  }
  if (argv[0] === 'build' && (argv.includes('--help') || argv.includes('-h'))) {
    printBuildHelp();
    process.exitCode = 0;
    return;
  }
  if (argv.includes('-h') || argv.includes('--help') || argv.length === 0) {
    if (argv.length === 0) {
      printUsageError('Usage: muse-native <init|build|dev|run|gallery-proof> [args]');
      process.exitCode = 2;
      return;
    }
    printHelp();
    process.exitCode = 0;
    return;
  }
  const [command] = argv;
  if (!COMMANDS.includes(command)) {
    printUsageError(`Unknown command: ${command}`);
    process.exitCode = 2;
    return;
  }
  if (command === 'init') {
    // `<dir>` stays positional; `--force` (any position after `init`)
    // permits scaffolding into a non-empty directory while preserving
    // pre-existing files (guard lives in init.js).
    const rest = argv.slice(1);
    const force = rest.includes('--force');
    const dir = rest.find((a) => a !== '--force');
    require('./init.js')(dir, { force });
    return;
  }
  if (command === 'build') {
    // `<appDir>` stays positional; every flag after it — including
    // `--stage <name>` and `--print-config` — passes through to build.js.
    // Option validation runs here at `configure`, before any compilation:
    // unknown/invalid/incompatible flags exit 2 naming the option and the
    // accepted values, with no `dist/` writes.
    try {
      validateOptions(argv);
    } catch (err) {
      const message = err && err.message ? err.message : String(err);
      process.stderr.write(message.endsWith('\n') ? message : message + '\n');
      try {
        process.exitCode = require('./exit-codes.js').getExitCode('config');
      } catch (_) {
        process.exitCode = 2;
      }
      return;
    }
    require('./build.js')(argv[1], argv.slice(2));
    return;
  }
  if (command === 'dev') {
    require('./dev.js')(argv[1]);
    return;
  }
  if (command === 'run') {
    require('./run.js')(argv[1]);
    return;
  }
}

module.exports = { validateOptions, OptionError, main };

// Run on direct invocation and when loaded through the `muse-native` (or
// `muse` alias) bin wrapper. Do not auto-run when required in-process (unit
// tests), where `require.main` is the test runner.
(function autoRun() {
  try {
    const entry = require.main && require.main.filename ? String(require.main.filename) : '';
    if (/(muse-native|muse)\.js$/.test(entry)) {
      main();
      return;
    }
  } catch (_) {
    // Fall through to the module.parent check below.
  }
  if (!module.parent) {
    main();
  }
})();
