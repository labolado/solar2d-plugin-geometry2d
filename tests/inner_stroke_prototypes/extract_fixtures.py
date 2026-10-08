#!/usr/bin/env python3
"""Extract only geometry; never copy business metadata or change source data."""
import argparse
import hashlib
import json
from pathlib import Path

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('backup', type=Path)
a = p.parse_args()
source = json.loads(a.backup.read_text())
cases = []
for entry in source['info']['data']:
    if entry.get('script') != 'poly_beam' or entry.get('index') not in (46, 47, 48):
        continue
    for child in entry['polyData']['data']:
        info = child['pathInfo']
        groups = [[info['paths'][i-1] for i in [node[0]] + node[1]] for node in info['tree']]
        cases.append({'name': str(entry['index']), 'groups': groups})
assert sorted(c['name'] for c in cases) == ['46','47','48']
out = Path(__file__).parent / 'fixtures'
out.mkdir(exist_ok=True)
payload = json.dumps(cases, ensure_ascii=True, separators=(',',':')) + '\n'
target = out / 'backup_geometry.json'
if target.exists() and target.read_text() != payload:
    raise SystemExit('Existing fixtures differ; inspect source changes before replacing')
target.write_text(payload)
(out / 'backup_geometry.sha256').write_text(hashlib.sha256(payload.encode()).hexdigest()+'  backup_geometry.json\n')
print('EXTRACTED', [(c['name'], sum(len(r)//2 for g in c['groups'] for r in g)) for c in cases])
