"""Guard fixed Apple deployment defaults and explicit per-invocation overrides."""
from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[2]


class AppleDeployment(unittest.TestCase):
    def test_projects_and_all_build_invocations(self):
        for platform, setting, version, count in (
            ('mac', 'MACOSX_DEPLOYMENT_TARGET', '12.0', 2),
            ('ios', 'IPHONEOS_DEPLOYMENT_TARGET', '15.0', 3),
            ('tvos', 'TVOS_DEPLOYMENT_TARGET', '15.0', 3),
        ):
            with self.subTest(platform=platform):
                base = ROOT / 'src' / platform
                project = (base / 'Plugin.xcodeproj/project.pbxproj').read_text()
                self.assertEqual(re.findall(setting + r' = ([^;]+);', project),
                                 [version, version])
                script = (base / 'build.sh').read_text()
                self.assertIn('deployment_target=${' + setting + ':-' + version + '}', script)
                commands = script.replace('\\\n', ' ').splitlines()
                builds = [line for line in commands if line.startswith('xcodebuild -project ')]
                self.assertEqual(len(builds), count)
                for line in builds:
                    self.assertIn(setting + '="$deployment_target"', line)
                self.assertIn('ARCHS="x86_64 arm64" ONLY_ACTIVE_ARCH=NO', script)


if __name__ == '__main__':
    unittest.main()
