#!/usr/bin/env python3
"""Shared, data-only local development configuration. No shell evaluation."""
import argparse
import json
import os
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
ENV = {'coronaRoot': 'CORONA_ROOT', 'simulator': 'SOLAR2D_SIMULATOR',
       'localPluginServerRoot': 'LOCAL_PLUGIN_SERVER_ROOT'}


class Config:
    def __init__(self, path=None, environ=None):
        self.env = os.environ if environ is None else environ
        explicit = path or self.env.get('GEOMETRY2D_DEV_CONFIG')
        self.path = Path(explicit or ROOT / 'dev.local.json').expanduser().resolve()
        self.data = {}
        if self.env.get('CI', '').lower() not in ('', '0', 'false'):
            if explicit:
                raise ValueError('Local config is disabled in CI; use environment variables')
            return
        try:
            self.data = json.loads(self.path.read_text(encoding='utf-8'))
        except FileNotFoundError:
            if explicit:
                raise ValueError('Specified development config does not exist') from None
            return
        except (OSError, ValueError):
            raise ValueError('Cannot read development config as JSON') from None
        if not isinstance(self.data, dict) or set(self.data) - set(ENV):
            raise ValueError('Config must be an object with only: ' + ', '.join(ENV))
        for key, value in self.data.items():
            if value is not None and (not isinstance(value, str) or not value.strip()
                                      or any(c in value for c in '\x00\n\r')):
                raise ValueError(f'{key} must be a nonempty path string or null')

    def get(self, key, override=None, required=True):
        if key not in ENV:
            raise ValueError('Unknown development setting')
        value = override if override is not None else self.env.get(ENV[key])
        base = Path.cwd()
        if value is None:
            value = self.data.get(key)
            base = self.path.parent
        if value is None and key == 'simulator':
            corona = self.get('coronaRoot', required=False)
            if corona:
                value = corona / 'platform/mac/build/Release/Corona Simulator.app'
        if value is None:
            if required:
                raise ValueError(f'Set {ENV[key]} or {key} in dev.local.json')
            return None
        value = str(value)
        if not value.strip() or any(c in value for c in '\x00\n\r'):
            raise ValueError(f'Invalid path for {key}')
        path = Path(value).expanduser()
        path = (base / path).resolve() if not path.is_absolute() else path.resolve()
        if key == 'simulator':
            if path.suffix == '.app':
                path = path / 'Contents/MacOS/Corona Simulator'
            if not path.is_file() or not os.access(path, os.X_OK):
                raise ValueError('Configured Simulator executable is missing or not executable')
        elif not path.is_dir():
            raise ValueError(f'Configured {key} directory does not exist')
        return path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--config', type=Path)
    parser.add_argument('--get', choices=ENV, required=True)
    args = parser.parse_args()
    try:
        print(Config(args.config).get(args.get))
    except ValueError as error:
        parser.error(str(error))


if __name__ == '__main__':
    main()
