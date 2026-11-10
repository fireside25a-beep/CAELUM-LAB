"""Shared immutable-payload rules for repository checks and release packaging."""
from pathlib import Path
import hashlib,json
EXACT_MUTABLE={'.git','dist','runs','.venv'}
def mutable(path:Path)->bool:
 parts=path.parts
 return (parts[0] in EXACT_MUTABLE or (len(parts)>1 and parts[0].startswith('build')) or parts[0].startswith('.venv-') or '__pycache__' in parts or parts[-1] in {'.DS_Store','Thumbs.db'} or parts[-1].endswith(('.pyc','.pyo')))
def payload(root:Path):
 return sorted((p for p in root.rglob('*') if (p.is_file() or p.is_symlink()) and not mutable(p.relative_to(root))),key=lambda p:p.relative_to(root).as_posix())
def entry(root:Path,path:Path):
 rel=path.relative_to(root).as_posix()
 if path.is_symlink():
  target=path.readlink()
  if target.is_absolute() or not path.resolve().is_relative_to(root):raise ValueError('Unsafe symlink: '+rel)
  data=str(target).encode();kind='symlink'
 else:
  data=path.read_bytes();kind='file'
  if not data:raise ValueError('Empty immutable file: '+rel)
 return {'path':rel,'kind':kind,'bytes':len(data),'sha256':hashlib.sha256(data).hexdigest()}
def verify(root:Path):
 manifest=json.loads((root/'MANIFEST.json').read_text());items=manifest['files'];expected={e['path']:e for e in items}
 if len(expected)!=len(items):raise ValueError('Duplicate manifest path')
 for name in expected:
  p=Path(name)
  if p.is_absolute() or '..' in p.parts or mutable(p):raise ValueError('Invalid manifest path: '+name)
 actual={p.relative_to(root).as_posix():p for p in payload(root) if p.name!='MANIFEST.json' or p.parent!=root}
 omitted={'README.md','docs/assets/caelum-banner.svg'}
 missing=set(expected)-set(actual)
 archive_view=missing==omitted
 if set(actual)-set(expected) or (missing and not archive_view):raise ValueError('Missing/extra immutable files: '+str(sorted(set(actual)^set(expected))))
 for name,path in actual.items():
  if entry(root,path)!=expected[name]:raise ValueError('Modified '+name)
 return {'status':'PASS','files_and_links':len(actual),'manifest':'MANIFEST.json'}
