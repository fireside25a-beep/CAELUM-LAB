#!/usr/bin/env python3
"""Check immutable release content while allowing local Git/build/run outputs."""
from pathlib import Path
import sys,json
root=Path(__file__).resolve().parent
sys.path.insert(0,str(root/'verification/publication'))
from layout import verify
try:
 print(json.dumps(verify(root)))
except (OSError,ValueError,KeyError) as error:
 print('FAIL:',error,file=sys.stderr);raise SystemExit(1)
