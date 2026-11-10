#!/usr/bin/env python3
"""Independent digest, complete public-symbol and real-header conformance."""
import ctypes as C
import hashlib
import json
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import numpy as np
import pyarrow as pa
build=Path(sys.argv[1]).resolve();root=Path(__file__).resolve().parent.parent
lib=C.CDLL(str(build/'libcaelum_port.so'));lib.cp_sha256.argtypes=[C.c_void_p,C.c_size_t,C.c_void_p]
rng=np.random.default_rng(338111);checks=0
for n in [0,1,2,3,55,56,57,63,64,65,119,120,121,127,128,129,255,256,257,1024,1000000]:
 data=rng.integers(0,256,n,dtype=np.uint8).tobytes();out=(C.c_uint8*32)()
 lib.cp_sha256(data,len(data),out);assert bytes(out)==hashlib.sha256(data).digest();checks+=1
symbols=set()
for file in (root/'include').glob('*.h'):
 symbols.update(re.findall(r'CP_API[^;]*?\b(cp_\w+)\s*\(',file.read_text()))
exports=set(re.findall(r'\b(cp_\w+)$',subprocess.check_output(['nm','-D','--defined-only',str(build/'libcaelum_port.so')],text=True),re.M))
assert symbols==exports,(symbols-exports,exports-symbols);checks+=len(symbols)
with tempfile.TemporaryDirectory(prefix='caelum-header-') as folder:
 for headers in [['arrow/c/abi.h','caelum_exchange.h'],['caelum_exchange.h','arrow/c/abi.h']]:
  file=Path(folder)/'headers.cpp';file.write_text(''.join('#include <'+h+'>\n' for h in headers)+'int main(){ return sizeof(ArrowArray)==0; }\n')
  subprocess.run(['g++','-std=c++17','-Wall','-Wextra','-Werror','-fsyntax-only','-I',str(root/'include'),'-I',pa.get_include(),str(file)],check=True)
  checks+=1
print(json.dumps({'status':'PASS','checks':checks,'sha256_lengths':21,'public_implemented_symbols':len(symbols),'real_arrow_cpp_include_orders':2}))
