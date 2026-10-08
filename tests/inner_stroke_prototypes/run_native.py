#!/usr/bin/env python3
"""Isolated native diagnostic: no plugin installation or production source edits."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import tempfile
import sys

here=Path(__file__).resolve().parent
root=here.parents[1]
sys.path.insert(0,str(root/'scripts'))
from dev_config import Config
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--config',type=Path)
p.add_argument('--private-fixtures',type=Path,help='Opt-in geometry JSON with adjacent .sha256; not part of public tests')
p.add_argument('--sanitize',action='store_true')
p.add_argument('--export',action='store_true',help='Export accepted shared-slot tiles and build macOS test buffer loader; requires CORONA_ROOT')
a=p.parse_args()
try:
    development=Config(a.config)
    engine=development.get('coronaRoot') if a.export else None
except ValueError as error:
    p.error(str(error))
fixture=(a.private_fixtures.expanduser().resolve() if a.private_fixtures
         else here/'fixtures/backup_geometry.json')
expected=fixture.with_suffix('.sha256').read_text().split()[0]
if hashlib.sha256(fixture.read_bytes()).hexdigest()!=expected:
    raise SystemExit('Fixture checksum mismatch')
cases=json.loads(fixture.read_text())
print('FIXTURES: '+('custom' if a.private_fixtures else 'public 46/47/48'))
def rect(x,y,w,h): return [x,y,x+w,y,x+w,y+h,x,y+h]
cases += [
 {'name':'convex','groups':[[rect(0,0,100,100)]]},
 {'name':'hole','groups':[[rect(0,0,100,100),rect(30,30,40,40)]]},
 {'name':'vanish','groups':[[rect(0,0,6,100)]]},
 {'name':'split','groups':[[[0,0,40,0,40,18,60,18,60,0,100,0,100,40,60,40,60,22,40,22,40,40,0,40]]]},
 {'name':'island','groups':[[rect(0,0,100,100),rect(20,20,60,60)],[rect(40,40,20,20)]]},
 {'name':'degenerate','groups':[[[0,0,10,0,20,0]]]},
]
lines=[str(len(cases))]
for case in cases:
    lines.append(f"{case['name']} {len(case['groups'])}")
    for group in case['groups']:
        lines.append(str(len(group)))
        for ring in group: lines.extend([str(len(ring)//2),' '.join(map(str,ring))])
work=Path(tempfile.mkdtemp(prefix='geometry2d-inner-prototypes-'))
flags=['-O1','-g','-fsanitize=address,undefined,float-cast-overflow','-fno-sanitize-recover=all'] if a.sanitize else ['-O2']
if sys.platform=='darwin':
    flags += ['-isysroot',subprocess.check_output(['xcrun','--sdk','macosx','--show-sdk-path'],text=True).strip()]
clip=root/'third_party/clipper2/CPP/Clipper2Lib'
cmd=[os.environ.get('CXX','c++'),'-std=c++17',*flags,'-I',str(root/'src/shared'),'-I',str(root/'third_party/earcut/include'),'-I',str(clip/'include'),
 str(here/'native/main.cpp'),str(here/'native/region.cpp'),str(here/'native/tiles.cpp'),str(root/'src/shared/sdf_builder.cpp'),str(root/'src/shared/earcut_stroke_builder.cpp'),
 str(clip/'src/clipper.engine.cpp'),str(clip/'src/clipper.offset.cpp'),'-o',str(work/'test')]
print('WORK',work,flush=True)
subprocess.run(cmd,check=True)
result=subprocess.run([str(work/'test')]+([str(work)] if a.export else []),input='\n'.join(lines)+'\n',text=True,capture_output=True,check=True)
print(result.stdout,end='');print(result.stderr,end='',file=sys.stderr)
rows=[json.loads(line) for line in result.stdout.splitlines() if line.startswith('{')]
assert len(rows)==len(cases)*6*3
for row in rows:
    if row['case'] in ('48','convex','hole','vanish','split','island') and row['method']=='region':
        assert row['success'],row
    if row['case'] in ('48','convex') and row['method'].startswith('tiles'):
        assert row['success'],row
    if row['case']=='degenerate': assert not row['success'],row
    if row['method']=='tilesShared11' and row['case']!='degenerate':
        assert row['success'] and row['tileOutputVertices']>0,row
        assert row['tileOutputVertices']==row['tileOutputTriangles']*2,row
        assert row['tileOutputBytes']==row['tileOutputVertices']//4*844,row
print('CONTROL_ASSERTIONS: PASS; inspect per-case success, not a full prototype acceptance marker')
if a.export:
    subprocess.run([os.environ.get('CXX','c++'),'-std=c++17','-O2','-dynamiclib','-undefined','dynamic_lookup',
        '-isysroot',subprocess.check_output(['xcrun','--sdk','macosx','--show-sdk-path'],text=True).strip(),
        '-I',str(engine/'external/lua-5.1.3/src'),'-I',str(engine/'librtt/Corona'),'-I',str(root/'src/shared'),
        str(here/'native/buffer_module.cpp'),str(root/'src/shared/corona_buffer.cpp'),'-o',str(work/'buffers.dylib')],check=True)
    print('EXPORT_DIRECTORY',work)
