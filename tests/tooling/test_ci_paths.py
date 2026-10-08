"""Guard the simple ordered path filters used by automatic CI events."""
import fnmatch
from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[2]


def filters(file, event):
    text = (ROOT / '.github/workflows' / file).read_text()
    block = re.search(r'^  ' + event + r':\n(.*?)(?=^  \w|^\S|\Z)',
                      text, re.M | re.S).group(1)
    paths = block.split('    paths:\n', 1)[1]
    return re.findall(r"^      - '([^']+)'$", paths, re.M)


def included(path, patterns):
    # These filters only use literal paths, trailing /** and extension exclusions.
    result = False
    for pattern in patterns:
        negative = pattern.startswith('!')
        pattern = pattern.lstrip('!')
        if fnmatch.fnmatchcase(path, pattern):
            result = not negative
    return result


class CIPaths(unittest.TestCase):
    def test_automatic_filters_agree(self):
        expected = filters('publish.yml', 'push')
        self.assertEqual(filters('publish.yml', 'pull_request'), expected)
        self.assertEqual(filters('check.yml', 'pull_request'), expected)
        for path in ('docs/api.md', 'README.md', 'AGENTS.md',
                     'tests/ribbon_simulator/README.md', 'src/mac/notes.rst',
                     'VERSION', 'examples/solar2d/build.settings'):
            self.assertFalse(included(path, expected), path)
        for path in ('src/shared/mesh_builder.cpp', 'src/mac/build.sh',
                     'third_party/clipper2', 'tests/tooling/test_ci_paths.py',
                     'tests/inner_stroke_prototypes/fixtures/backup_geometry.json',
                     '.github/workflows/check.yml', '.gitignore', '.gitmodules',
                     'sync_local_plugins.sh', 'dev.example.json'):
            self.assertTrue(included(path, expected), path)

    def test_version_only_filters(self):
        expected = ['VERSION', 'examples/solar2d/build.settings']
        self.assertEqual(filters('version.yml', 'push'), expected)
        self.assertEqual(filters('version.yml', 'pull_request'), expected)
        native = filters('publish.yml', 'push')
        self.assertFalse(any(included(p, native) for p in expected + ['docs/api.md']))
        self.assertTrue(any(included(p, native) for p in expected + ['src/mac/build.sh']))
        publish = (ROOT / '.github/workflows/publish.yml').read_text()
        self.assertIn("tags: ['v*']", publish)
        self.assertIn('  workflow_dispatch:', publish)


if __name__ == '__main__':
    unittest.main()
