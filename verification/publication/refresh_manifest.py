#!/usr/bin/env python3
"""Refresh publication metadata only while the qualified science stays unchanged."""
from pathlib import Path
import argparse,hashlib,json,re
from layout import payload,entry
p=argparse.ArgumentParser();p.add_argument('--root',type=Path,default=Path(__file__).resolve().parents[2]);a=p.parse_args();root=a.root.resolve()
# This is not a tool for blessing changed scientific source or binaries.
for item in json.loads((root/'verification/publication/qualified_payload.json').read_text()):
 if hashlib.sha256((root/item['path']).read_bytes()).hexdigest()!=item['sha256']:raise ValueError('Qualified scientific payload changed: '+item['path'])
status=json.loads((root/'RELEASE_STATUS.json').read_text());files=sorted([root/'CMakeLists.txt',*root.glob('src/*.c'),*root.glob('src/*.h'),*root.glob('src/*.h.in'),*root.glob('include/*.h'),*root.glob('adapters/*.c')],key=lambda p:p.relative_to(root).as_posix());material=''.join(p.relative_to(root).as_posix()+':'+hashlib.sha256(p.read_bytes()).hexdigest()+'\n' for p in files)
if hashlib.sha256(material.encode()).hexdigest()!=status['source_identity']:raise ValueError('Scientific source fingerprint changed')
status['publication']={'edition':'github-ready-1','first_party_license':'Apache-2.0','citation_format':'CFF 1.2.0','graphs':'Simple black-background SVG and PNG, from recorded data','github_actions':'Included, not executed on GitHub here','publishing':'Owner-triggered draft only; the owner uploads and publishes'}
for item in status['priorities']:
 if item['id']=='P2-03':item['evidence']=list(dict.fromkeys(item['evidence']+['LICENSE','CITATION.cff','docs/LICENSE_SCOPE.md','verification/publication/check_repository.py']))
(root/'RELEASE_STATUS.json').write_text(json.dumps(status,indent=2)+'\n')
index=json.loads((root/'VOLUME_INDEX.json').read_text());index['publication_edition']='github-ready-1';index['first_party_license']='Apache-2.0';index['citation']='CITATION.cff';index['layout']['.github']='Native CI and manually triggered draft-release packaging';index['layout']['verification/publication']='Metadata validation, citation schema and deterministic packaging';index['payload_files_and_links']=len(payload(root));(root/'VOLUME_INDEX.json').write_text(json.dumps(index,indent=2)+'\n')
paths=payload(root);dirs={root}
for path in paths:
 for parent in path.parents:
  if parent==root:break
  if parent.is_relative_to(root):dirs.add(parent)
allowed=set(paths)|dirs
lines=['# Complete CAELUM 0.4.0 repository tree','','GitHub-ready edition. One current scientific implementation; application assets are packaged separately. Local Git, build, distribution and run outputs are excluded from the immutable manifest. Embedded evidence/data ZIP members are catalogued in [ARCHIVE_CONTENTS.json](evidence/ARCHIVE_CONTENTS.json).','']
def tree(directory,level=0):
 for path in sorted((p for p in directory.iterdir() if p in allowed),key=lambda p:(not p.is_dir() or p.is_symlink(),p.name.lower())):
  if path.is_dir() and not path.is_symlink():lines.append('  '*level+'- **'+path.name+'/**');tree(path,level+1)
  else:
   label=path.name+(' → '+str(path.readlink()) if path.is_symlink() else '');lines.append('  '*level+'- ['+label+']('+path.relative_to(root).as_posix()+')')
tree(root);(root/'TREE.md').write_text('\n'.join(lines)+'\n')
entries=[entry(root,p) for p in payload(root) if p.relative_to(root).as_posix()!='MANIFEST.json'];(root/'MANIFEST.json').write_text(json.dumps({'release':'0.4.0','publication_edition':'github-ready-1','hash':'SHA-256','mutable_roots':['.git','build*','dist','runs','.venv*'],'files':entries},indent=2)+'\n');print(json.dumps({'status':'PASS','immutable_files_and_links':len(entries),'science_source_identity':status['source_identity'],'science_payload':'UNCHANGED'}))
