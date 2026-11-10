#!/usr/bin/env python3
"""Validate publication metadata without executing or changing scientific code."""
from pathlib import Path
import argparse,hashlib,json,re,struct,zlib,xml.etree.ElementTree as ET
import yaml,jsonschema
from layout import payload
p=argparse.ArgumentParser();p.add_argument('--root',type=Path,default=Path(__file__).resolve().parents[2]);a=p.parse_args();root=a.root.resolve();pub=root/'verification/publication';checks=[]
def require(value,name):
 if not value:raise ValueError(name)
 checks.append(name)
source=json.loads((root/'RELEASE_STATUS.json').read_text());version=re.search(r'VERSION\s+(\d+\.\d+\.\d+)',(root/'CMakeLists.txt').read_text()).group(1)
cff=yaml.safe_load((root/'CITATION.cff').read_text());schema=json.loads((pub/'cff-schema-1.2.0.json').read_text());jsonschema.Draft7Validator(schema,format_checker=jsonschema.FormatChecker()).validate(cff);checks.append('CITATION.cff validates against upstream 1.2.0 schema')
require(cff['version']==version and cff['license']=='Apache-2.0','Citation version/license match the current application')
require(cff['authors']==[{'family-names':'Moraru','given-names':'Alexandru'}],'Citation identifies the actual author without invented affiliation/DOI')
license_origin=json.loads((pub/'license_source.json').read_text());require(hashlib.sha256((root/'LICENSE').read_bytes()).hexdigest()==license_origin['sha256'],'Unmodified official Apache-2.0 license text')
require('Copyright 2026 Alexandru Moraru' in (root/'NOTICE.md').read_text(),'First-party copyright and scope notice present')
for item in json.loads((pub/'qualified_payload.json').read_text()):require(hashlib.sha256((root/item['path']).read_bytes()).hexdigest()==item['sha256'],'Qualified bytes unchanged: '+item['path'])
files=sorted([root/'CMakeLists.txt',*root.glob('src/*.c'),*root.glob('src/*.h'),*root.glob('src/*.h.in'),*root.glob('adapters/*.c'),*root.glob('include/*.h')],key=lambda p:p.relative_to(root).as_posix());material=''.join(p.relative_to(root).as_posix()+':'+hashlib.sha256(p.read_bytes()).hexdigest()+'\n' for p in files)
require(hashlib.sha256(material.encode()).hexdigest()==source['source_identity'],'Scientific source fingerprint unchanged')
readme=(root/'README.md').read_text();targets=re.findall(r'\]\(([^)]+)\)',readme)+re.findall(r'<img[^>]+src="([^"]+)"',readme)
for target in targets:
 if target.startswith(('https://','http://','#')):continue
 path=(root/target.split('#',1)[0]).resolve();require(path.is_relative_to(root) and path.is_file(),'README local target: '+target)
for name in ['caelum-banner','intervention-error','empirical-fit','calibration']:
 path=root/'docs/assets'/(name+'.svg');text=path.read_text();svg=ET.fromstring(text);require(svg.tag.endswith('svg'),'SVG parses: '+name);require('#000000' in text,'Black SVG background: '+name)
 nodes=list(svg.iter())
 require(all(n.tag.rsplit('}',1)[-1] not in {'script','foreignObject'} for n in nodes),'No active SVG content: '+name)
 require(all(not attr.lower().startswith('on') for n in nodes for attr in n.attrib),'No SVG event handlers: '+name)
 require(all(value.startswith('#') for n in nodes for attr,value in n.attrib.items() if attr.rsplit('}',1)[-1]=='href'),'No external SVG assets: '+name)
 if name!='caelum-banner':
  image=(root/'docs/assets'/(name+'.png')).read_bytes();require(image.startswith(b'\x89PNG\r\n\x1a\n'),'PNG signature: '+name);at=8;data=b''
  while at<len(image):
   size=struct.unpack('>I',image[at:at+4])[0];kind=image[at+4:at+8];block=image[at+8:at+8+size]
   if kind==b'IDAT':data+=block
   at+=12+size
  raw=zlib.decompress(data);require(raw[1:4]==b'\0\0\0','Black rendered PNG corner: '+name)
for path in sorted((root/'.github/workflows').glob('*.yml')):
 workflow=yaml.load(path.read_text(),Loader=yaml.BaseLoader);require(workflow['permissions']['contents']=='read','Workflow default token is read-only: '+path.name)
 for job in workflow['jobs'].values():
  for step in job.get('steps',[]):
   if 'uses' in step:require(re.fullmatch(r'actions/[a-z-]+@[0-9a-f]{40}',step['uses']) is not None,'Action pinned to a full commit: '+step['uses'].split('@')[0])
 release=yaml.load((root/'.github/workflows/release.yml').read_text(),Loader=yaml.BaseLoader)
require(set(release['on'])=={'workflow_dispatch'},'Release publishing is owner-triggered only')
require('--draft' in (root/'.github/workflows/release.yml').read_text(),'Release preparation creates only a draft')
require('--verify-tag' in (root/'.github/workflows/release.yml').read_text(),'Release preparation requires an existing tag')
large=[p.relative_to(root).as_posix() for p in payload(root) if p.is_file() and p.stat().st_size>=100*1024*1024];require(not large,'No file exceeds GitHub 100 MiB regular-file limit')
report={'status':'PASS','checks':len(checks),'citation_schema':'1.2.0','license':'Apache-2.0','version':version,'scientific_source_identity':source['source_identity'],'scientific_code_and_binaries':'UNCHANGED','github_execution':'NOT_RUN_HERE','details':checks};print(json.dumps(report,indent=2))
