#!/usr/bin/env python3
"""Run a Solar2D test project and require an explicit console result marker."""
import argparse
import math
import os
from pathlib import Path
import queue
import shutil
import subprocess
import tempfile
import threading
import time
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / 'scripts'))
from dev_config import Config
from local_simulator_project import stage


def main():
    root = Path(__file__).resolve().parent.parent
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('project', nargs='?', default=str(root / 'tests/ribbon_simulator'))
    parser.add_argument('--timeout', type=float, default=180)
    parser.add_argument('--config', type=Path)
    parser.add_argument('--simulator', type=Path)
    parser.add_argument('--interactive', action='store_true',
                        help='Stay open until Simulator exits; ignore result markers and timeout')
    parser.add_argument('--plugin', type=Path,
                        help='Atomically install this freshly built dylib before starting the test')
    parser.add_argument('--local-plugin', action='store_true',
                        help='With --plugin: stage a self-contained project, disable downloads, load that dylib directly')
    args = parser.parse_args()
    project = Path(args.project).resolve()
    try:
        simulator = Config(args.config).get('simulator', args.simulator)
    except ValueError as error:
        parser.error(str(error))
    if not (project / 'main.lua').is_file():
        parser.error(f'Project does not contain main.lua: {project}')
    if not simulator.is_file():
        parser.error(f'Simulator not found: {simulator}')
    if not math.isfinite(args.timeout) or args.timeout <= 0:
        parser.error('--timeout must be finite and positive')
    if args.local_plugin and not args.plugin:
        parser.error('--local-plugin requires an explicit --plugin')
    if args.plugin:
        source = args.plugin.expanduser().resolve()
        if not source.is_file() or source.name != 'plugin_geometry2d.dylib':
            parser.error('--plugin must identify a built plugin_geometry2d.dylib')
        if args.local_plugin:
            with tempfile.TemporaryDirectory(prefix='geometry2d-local-project-') as temporary:
                try:
                    staged = stage(project, source, temporary)
                except (OSError, ValueError) as error:
                    parser.error(str(error))
                print('SIMULATOR_RUNNER: isolated local plugin (downloads disabled)', flush=True)
                return run_simulator(simulator, staged, args.interactive, args.timeout)
        destination = Path.home() / 'Library/Application Support/Corona/Simulator/Plugins'
        destination.mkdir(parents=True, exist_ok=True)
        # Rename a new inode into place, rather than overwriting a mapped dylib.
        with tempfile.TemporaryDirectory(prefix='geometry2d-install-', dir=destination) as staging:
            staged = Path(staging) / source.name
            shutil.copy2(source, staged)
            os.replace(staged, destination / source.name)
        print(f'SIMULATOR_RUNNER: installed {source}', flush=True)

    return run_simulator(simulator, project, args.interactive, args.timeout)


def run_simulator(simulator, project, interactive, timeout):

    # Equivalent to build_and_test.sh --no-build, with a process-local Cocoa
    # argument used by the engine's own test runners to skip crash restoration.
    command = [str(simulator), '-no-console', 'YES', '-ApplePersistenceIgnoreState', 'YES', str(project)]
    print(f'SIMULATOR_RUNNER: project={project} simulator={simulator}', flush=True)
    if interactive:
        process = subprocess.Popen(command)
        try:
            return process.wait()
        except KeyboardInterrupt:
            if process.poll() is None:
                process.kill()
            process.wait()
            return 130
    process = subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                               text=True, encoding='utf-8', errors='replace', bufsize=1)
    lines = queue.Queue()

    def read_output():
        try:
            for line in process.stdout:
                lines.put(line)
        finally:
            lines.put(None)

    threading.Thread(target=read_output, daemon=True).start()
    deadline = time.monotonic() + timeout
    result = 1
    try:
        while True:
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                print('SIMULATOR_RUNNER: TIMEOUT', flush=True)
                break
            try:
                line = lines.get(timeout=remaining)
            except queue.Empty:
                print('SIMULATOR_RUNNER: TIMEOUT', flush=True)
                break
            if line is None:
                print('SIMULATOR_RUNNER: ERROR process ended without result marker', flush=True)
                break
            print(line, end='', flush=True)
            if line.strip() in ('SIMULATOR_TEST_EXIT: 0', 'SIMULATOR_TEST_EXIT: 1'):
                result = int(line.strip()[-1])
                break
    except KeyboardInterrupt:
        print('SIMULATOR_RUNNER: INTERRUPTED', flush=True)
    finally:
        # The local Simulator can crash in SIGTERM cleanup. Kill only the exact
        # test process we created, after recording its result (or timeout).
        if process.poll() is None:
            process.kill()
        process.wait()
        process.stdout.close()
    print(f'SIMULATOR_RUNNER: EXIT result={result} process_reaped=true', flush=True)
    return result


if __name__ == '__main__':
    raise SystemExit(main())
