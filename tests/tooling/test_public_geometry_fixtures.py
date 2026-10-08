"""The approved precision fixtures must stay geometry-only and byte-identical."""
import hashlib
import json
import math
from pathlib import Path
import unittest

FIXTURE = (Path(__file__).resolve().parents[1] / 'inner_stroke_prototypes'
           / 'fixtures/backup_geometry.json')


class PublicGeometryFixtures(unittest.TestCase):
    def test_integrity_and_geometry_only_schema(self):
        payload = FIXTURE.read_bytes()
        digest, name = FIXTURE.with_suffix('.sha256').read_text().split()
        self.assertEqual(name, FIXTURE.name)
        self.assertEqual(hashlib.sha256(payload).hexdigest(), digest)
        cases = json.loads(payload)
        self.assertEqual([c['name'] for c in cases], ['46', '47', '48'])
        for case in cases:
            self.assertEqual(set(case), {'name', 'groups'})
            self.assertTrue(case['groups'])
            for group in case['groups']:
                self.assertTrue(group)
                for ring in group:
                    self.assertGreaterEqual(len(ring), 6)
                    self.assertEqual(len(ring) % 2, 0)
                    for value in ring:
                        self.assertIn(type(value), (int, float))
                        self.assertTrue(math.isfinite(value))


if __name__ == '__main__':
    unittest.main()
