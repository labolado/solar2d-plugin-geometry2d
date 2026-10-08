#!/usr/bin/env python3
"""Check tracked/nonignored publication candidates; never inspect private config contents."""
import re
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent


def main():
    names = subprocess.check_output(
        ['git', 'ls-files', '-z', '--cached', '--others', '--exclude-standard'], cwd=ROOT)
    problems = []
    personal = re.compile(r'/(?:Users|home)/[^/\s]+|/(?:private/)?var/folders/|labo_[a-z0-9_]+')
    for name in sorted(set(names.decode().split('\0')) - {''}):
        path = ROOT / name
        if (name == 'dev.local.json' or name.startswith(('plugins/', '.local/', 'crashes/'))
                or (name.startswith('tests/') and '/results/' in name and name.endswith('.txt'))):
            problems.append(f'{name}: private/generated file is tracked or not ignored')
            continue
        if path.is_symlink():
            # Existing platform SDK links are portable installation conventions,
            # not personal paths; CI provisions this exact SDK location.
            if name in ('src/mac/Native', 'src/ios/Native', 'src/tvos/Native') and str(path.readlink()) == '/Applications/CoronaEnterprise':
                continue
            if not path.resolve().is_relative_to(ROOT):
                problems.append(f'{name}: external symbolic link')
            continue
        if not path.is_file():
            continue  # submodule entry or deleted file
        data = path.read_bytes()
        if b'\0' in data:
            continue
        for line, text in enumerate(data.decode('utf-8', errors='replace').splitlines(), 1):
            if personal.search(text):
                problems.append(f'{name}:{line}: personal path or private project identifier')
    for problem in problems:
        print(problem)
    print(f'PUBLIC_TREE: {"FAIL" if problems else "PASS"} ({len(problems)} findings; not a history/secret audit)')
    return bool(problems)


if __name__ == '__main__':
    raise SystemExit(main())
