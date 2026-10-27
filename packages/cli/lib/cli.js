'use strict';
// @muse/cli — argument parsing and dispatch for the muse-native/muse/capo binaries.
// FR-WORKFLOW owns command options later; this file only routes argv.
const path = require('node:path');

const COMMANDS = ['init', 'build', 'dev', 'run'];
const INVOKED = path.basename(process.argv[1] || '', '.js');

if (INVOKED === 'muse' || INVOKED === 'capo') {
  process.stderr.write(`Warning: '${INVOKED}' is deprecated, use 'muse-native' instead.\n`);
}

function printHelp() {
  process.stdout.write(
    [
      'Usage: muse-native <init|build|dev|run> [args]',
      '',
      'Commands:',
      '  init    scaffold a new project (not implemented)',
      '  build   build the project (not implemented)',
      '  dev     start the dev server (not implemented)',
      '  run     run the project (not implemented)',
      '',
      'Options:',
      '  -h, --help    show this help',
      '',
    ].join('\n'),
  );
}

function printUsageError(message) {
  process.stderr.write(`${message}\nUsage: muse-native <init|build|dev|run> [args]\n`);
}

function main() {
  const argv = process.argv.slice(2);
  if (argv.includes('-h') || argv.includes('--help') || argv.length === 0) {
    if (argv.length === 0) {
      printUsageError('Usage: muse-native <init|build|dev|run> [args]');
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
    require('./init.js')(argv[1]);
    return;
  }
  if (command === 'build') {
    require('./build.js')(argv[1]);
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
  process.stderr.write(`Command '${command}' is not implemented yet.\n`);
  process.exitCode = 1;
}

main();
