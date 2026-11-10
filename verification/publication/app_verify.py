#!/usr/bin/env python3
"""Verify the standalone application package; user run outputs are mutable."""
from pathlib import Path
import hashlib,json,sys
root=Path(__file__).resolve().parent
try:
 items=json.loads((root/'MANIFEST.json').read_text())['files'];expected={x['path']:x for x in items}
 if len(expected)!=len(items):raise ValueError('Duplicate path')
 files={p.relative_to(root).as_posix():p for p in root.rglob('*') if (p.is_file() or p.is_symlink()) and p.relative_to(root).parts[0]!='runs' and p.name not in {'.DS_Store','Thumbs.db'}}
 files.pop('MANIFEST.json',None)
 if set(files)!=set(expected):raise ValueError('Missing or extra application files')
 for name,p in files.items():
  e=expected[name];rel=Path(name)
  if rel.is_absolute() or '..' in rel.parts:raise ValueError('Unsafe path')
  if p.is_symlink():
   if p.readlink().is_absolute() or not p.resolve().is_relative_to(root) or e['kind']!='symlink':raise ValueError('Unsafe link')
   data=str(p.readlink()).encode()
  else:
   if e['kind']!='file':raise ValueError('Wrong file kind')
   data=p.read_bytes()
  if len(data)!=e['bytes'] or hashlib.sha256(data).hexdigest()!=e['sha256']:raise ValueError('Modified '+name)
 print(json.dumps({'status':'PASS','application_files_and_links':len(files)}))
except (OSError,ValueError,KeyError) as error:
 print('FAIL:',error,file=sys.stderr);raise SystemExit(1)
