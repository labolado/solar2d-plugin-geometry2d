import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'scripts'))
import dev_config
from local_simulator_project import stage
from release_policy import validate


class DevelopmentTools(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='geometry2d tooling ')
        self.addCleanup(self.temp.cleanup)
        self.dir = Path(self.temp.name).resolve()
        self.config = self.dir / 'dev.local.json'
        self.config.write_text('{}')

    def write(self, value):
        self.config.write_text(json.dumps(value))

    def test_precedence_and_relative_paths(self):
        for name in ('local', 'env', 'cli'):
            (self.dir / name).mkdir()
        self.write({'coronaRoot': 'local', 'localPluginServerRoot': None})
        c = dev_config.Config(self.config, environ={})
        self.assertEqual(c.get('coronaRoot'), self.dir / 'local')
        self.assertIsNone(c.get('localPluginServerRoot', required=False))
        c = dev_config.Config(self.config, environ={'CORONA_ROOT': str(self.dir / 'env')})
        self.assertEqual(c.get('coronaRoot'), self.dir / 'env')
        self.assertEqual(c.get('coronaRoot', self.dir / 'cli'), self.dir / 'cli')

    def test_invalid_config(self):
        for value in ([], {'unknown': 1}, {'simulator': False}, {'coronaRoot': ''}, {'coronaRoot': 'a\nb'}):
            self.write(value)
            with self.assertRaises(ValueError):
                dev_config.Config(self.config, environ={})
        self.config.write_text('{')
        with self.assertRaises(ValueError):
            dev_config.Config(self.config, environ={})

    def test_missing_and_ci(self):
        with patch.object(dev_config, 'ROOT', self.dir / 'missing'):
            self.assertIsNone(dev_config.Config(environ={}).get('coronaRoot', required=False))
            self.assertEqual(dev_config.Config(environ={'CI': 'true', 'CORONA_ROOT': str(self.dir)}).get('coronaRoot'), self.dir)
        with self.assertRaises(ValueError):
            dev_config.Config(self.dir / 'missing.json', environ={})
        with self.assertRaises(ValueError):
            dev_config.Config(self.config, environ={'CI': 'true'})

    def test_app_and_unused_paths(self):
        exe = self.dir / 'Simulator.app/Contents/MacOS/Corona Simulator'
        exe.parent.mkdir(parents=True)
        exe.write_text('#!/bin/sh\nexit 0\n'); exe.chmod(0o700)
        self.write({'simulator': 'Simulator.app', 'localPluginServerRoot': 'missing'})
        self.assertEqual(dev_config.Config(self.config, environ={}).get('simulator'), exe)
        with self.assertRaises(ValueError):
            dev_config.Config(self.config, environ={}).get('localPluginServerRoot')

    def launch(self, code, interactive=False, timeout='2'):
        exe = self.dir / 'fake simulator'
        exe.write_text('#!' + sys.executable + '\n' + code + '\n'); exe.chmod(0o700)
        self.write({'simulator': str(exe)})
        project = self.dir / 'test project'; project.mkdir(exist_ok=True)
        (project / 'main.lua').write_text('-- mock project\n')
        command = [sys.executable, str(ROOT / 'tests/run_simulator.py'), str(project),
                   '--config', str(self.config), '--timeout', timeout]
        if interactive:
            command.append('--interactive')
        env = dict(os.environ, CI='false')
        env.pop('SOLAR2D_SIMULATOR', None)
        return subprocess.run(command, capture_output=True, text=True, env=env, timeout=8)

    def test_pass_fail_and_missing_marker(self):
        self.assertEqual(self.launch("print('SIMULATOR_TEST_EXIT: 0', flush=True)").returncode, 0)
        self.assertEqual(self.launch("print('SIMULATOR_TEST_EXIT: 1', flush=True)").returncode, 1)
        self.assertEqual(self.launch('pass').returncode, 1)

    def test_timeout_and_interactive(self):
        result = self.launch('import time; time.sleep(2)', timeout='.2')
        self.assertEqual(result.returncode, 1)
        self.assertIn('TIMEOUT', result.stdout)
        result = self.launch("import time; print('SIMULATOR_TEST_EXIT: 1'); time.sleep(.3)",
                             interactive=True, timeout='.05')
        self.assertEqual(result.returncode, 0)
        self.assertNotIn('TIMEOUT', result.stdout)

    def test_release_policy(self):
        example = 'local plugin_version = "v1"'
        for version in ('1', '2', '10'):
            self.assertTrue(validate('push', 'refs/tags/v' + version, version,
                                     'local plugin_version = "v' + version + '"'))
        for event, ref in [('push', 'refs/heads/main'), ('pull_request', 'refs/pull/1/merge'),
                           ('workflow_dispatch', 'refs/tags/v1'), ('repository_dispatch', 'refs/heads/main')]:
            self.assertFalse(validate(event, ref, '1', example))
        for tag in ('v2', 'v0', 'v01', 'v1.0.0', 'v1-rc1', 'v1;bad', 'V1'):
            with self.assertRaises(ValueError):
                validate('push', 'refs/tags/' + tag, '1', example)
        for version in ('0', '01', '-1', '1.0.0', 'v1', '1-rc1', ''):
            with self.assertRaises(ValueError):
                validate('push', 'refs/heads/main', version, 'local plugin_version = "v' + version + '"')
        with self.assertRaises(ValueError):
            validate('push', 'refs/heads/main', '2', example)

    def test_checked_in_release_policy(self):
        version = (ROOT / 'VERSION').read_text().strip()
        example = (ROOT / 'examples/solar2d/build.settings').read_text()
        self.assertTrue(validate('push', 'refs/tags/v' + version, version, example))
        workflow = (ROOT / '.github/workflows/publish.yml').read_text()
        self.assertNotIn('github.run_number', workflow)
        self.assertNotIn('repository_dispatch:', workflow)
        self.assertIn("if: ${{ github.event_name == 'push' && startsWith(github.ref, 'refs/tags/v') }}", workflow)
        self.assertIn('armeabi-v7a arm64-v8a x86 x86_64', workflow)
        self.assertIn('--verify-tag --draft', workflow)
        self.assertNotIn('--clobber', workflow)

    def test_isolated_project(self):
        source = self.dir / 'source'; source.mkdir()
        (source / 'main.lua').write_text('print("original")')
        settings = 'settings={orientation={default="landscapeRight"},plugins={}}'
        (source / 'build.settings').write_text(settings)
        plugin = self.dir / 'plugin_geometry2d.dylib'; plugin.write_bytes(b'test binary')
        with tempfile.TemporaryDirectory(dir=self.dir) as temporary:
            target = stage(source, plugin, temporary)
            self.assertEqual((source / 'build.settings').read_text(), settings)
            self.assertEqual((source / 'main.lua').read_text(), 'print("original")')
            self.assertTrue((target / 'build.settings').read_text().endswith('settings.plugins = nil\n'))
            self.assertIn('package.preload', (target / 'main.lua').read_text())
            self.assertEqual((target / 'plugin_geometry2d.dylib').read_bytes(), b'test binary')
        self.assertFalse(target.exists())


if __name__ == '__main__':
    unittest.main()
