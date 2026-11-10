#!/usr/bin/env python3
"""Deterministic, offline packaging of qualified binaries and the research tree."""
from pathlib import Path
import argparse,hashlib,json,re,stat,zipfile
from layout import payload,verify
ROOT=Path(__file__).resolve().parents[2]
def blob(path):
 if path.is_symlink():return str(path.readlink()).encode(),stat.S_IFLNK|0o777
 return path.read_bytes(),stat.S_IFREG|(0o755 if path.stat().st_mode&0o111 else 0o644)
def write_zip(path,prefix,files):
 with zipfile.ZipFile(path,'w',zipfile.ZIP_DEFLATED,compresslevel=9) as z:
  for name,(data,mode) in sorted(files.items()):
   info=zipfile.ZipInfo(prefix+'/'+name,(1980,1,1,0,0,0));info.create_system=3;info.external_attr=mode<<16;info.compress_type=zipfile.ZIP_DEFLATED
   z.writestr(info,data,compress_type=zipfile.ZIP_DEFLATED,compresslevel=9)
 with zipfile.ZipFile(path) as z:
  if z.testzip() is not None:raise ValueError('ZIP CRC failure')
def main():
 p=argparse.ArgumentParser();p.add_argument('--root',type=Path,default=ROOT);p.add_argument('--output',type=Path,default=Path('dist'));p.add_argument('--expected-tag');a=p.parse_args();root=a.root.resolve();output=a.output.resolve();verify(root)
 status=json.loads((root/'RELEASE_STATUS.json').read_text());version=re.search(r'VERSION\s+(\d+\.\d+\.\d+)',(root/'CMakeLists.txt').read_text()).group(1)
 if a.expected_tag and a.expected_tag!='v'+version:raise ValueError('Tag must match compiled application version v'+version)
 if status['linux_x86_64']!='QUALIFIED':raise ValueError('Application is not marked Linux-qualified')
 output.mkdir(parents=True,exist_ok=True)
 research={p.relative_to(root).as_posix():blob(p) for p in payload(root)}
 app={}
 for p in payload(root):
  rel=p.relative_to(root).as_posix()
  if rel.startswith(('native/bin/','native/lib/')) and not rel.startswith('native/lib/pkgconfig/'):app[rel]=blob(p)
  elif rel.startswith('examples/finite_scm/') or rel in {'Start-CAELUM.sh','LICENSE','NOTICE.md','CITATION.cff','docs/LICENSE_SCOPE.md','docs/FORMATS.md'}:app[rel]=blob(p)
 app['README.txt']=(('CAELUM '+version+' — Linux x86-64\n\nExtract the entire folder and run ./Start-CAELUM.sh with an X11 display.\nThe included input is a labelled mathematical example. Edit the protocol\nand output path in the native window for your measured data.\nRun / Stop / Resume / Replay use the same native experiment authority.\n\nThis application asset excludes GitHub graphics, source, test programs\nand research archives. The research-volume asset contains those materials.\nFirst-party code: Apache-2.0. See LICENSE, NOTICE.md and docs/LICENSE_SCOPE.md.\nWindows/macOS native execution is not qualified.\n\nVerify application bytes with: python3 verify_integrity.py\n').encode(),stat.S_IFREG|0o644)
 app['verify_integrity.py']=blob(root/'verification/publication/app_verify.py')
 info={k:status[k] for k in ['release','source_identity','production_build_identity','abi']};info['platform']='Linux x86-64';info['application_logic']='Unchanged qualified native binaries';app['BUILD_INFO.json']=(json.dumps(info,indent=2).encode()+b'\n',stat.S_IFREG|0o644)
 entries=[]
 for name,(data,mode) in sorted(app.items()):entries.append({'path':name,'kind':'symlink' if stat.S_ISLNK(mode) else 'file','bytes':len(data),'sha256':hashlib.sha256(data).hexdigest()})
 app['MANIFEST.json']=(json.dumps({'version':version,'hash':'SHA-256','files':entries},indent=2).encode()+b'\n',stat.S_IFREG|0o644)
 files=[('CAELUM-'+version+'-linux-x86_64-app.zip','CAELUM-'+version+'-linux-x86_64',app),('CAELUM-'+version+'-research-volume.zip','CAELUM-'+version,research)]
 result=[]
 for name,prefix,contents in files:
  target=output/name;write_zip(target,prefix,contents);result.append({'filename':name,'bytes':target.stat().st_size,'sha256':hashlib.sha256(target.read_bytes()).hexdigest(),'files_and_links':len(contents)})
 (output/'SHA256SUMS.txt').write_text(''.join(e['sha256']+'  '+e['filename']+'\n' for e in result))
 report={'status':'PASS','version':version,'deterministic_zip_time':'1980-01-01T00:00:00','science_source_identity':status['source_identity'],'native_binaries':'Copied unchanged; no rebuild or scientific requalification is implied','assets':result};(output/'PACKAGE_REPORT.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
if __name__=='__main__':main()
