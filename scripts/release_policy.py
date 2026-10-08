#!/usr/bin/env python3
"""Validate release intent without creating tags, releases or network requests."""
import argparse
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parent.parent
VERSION = re.compile(r'[1-9][0-9]*')


def validate(event, ref, version, example):
    if not VERSION.fullmatch(version):
        raise ValueError('VERSION must contain a positive integer without leading zeros (no v prefix)')
    match = re.search(r'local plugin_version = "([^"]+)"', example)
    if not match or match[1] != 'v' + version:
        raise ValueError('Example plugin_version must match VERSION')
    if event == 'push' and ref.startswith('refs/tags/'):
        if ref != 'refs/tags/v' + version:
            raise ValueError('Release tag must be v followed by VERSION (for example v1 or v2)')
        return True
    return False


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--event', required=True)
    parser.add_argument('--ref', required=True)
    args = parser.parse_args()
    try:
        publish = validate(args.event, args.ref, (ROOT / 'VERSION').read_text().strip(),
                           (ROOT / 'examples/solar2d/build.settings').read_text())
    except ValueError as error:
        parser.error(str(error))
    print('RELEASE_POLICY: ' + ('tag release permitted' if publish else 'compile only'))


if __name__ == '__main__':
    main()
