#!/usr/bin/env python3
"""Real source-protection tests: no receipt may overwrite its input inode."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile

if len(sys.argv)!=2:
    raise SystemExit("usage: path_alias_oracle.py /path/to/caelum-port")
exe=Path(sys.argv[1]).resolve()
assert exe.is_file()
checks=0
with tempfile.TemporaryDirectory(prefix="caelum-source-identity-") as directory:
    root=Path(directory)
    source=root/"original.csv"
    payload=b"1,2,3\n"*100
    source.write_bytes(payload)
    for kind in ("hardlink","symlink","same_path"):
        result=root/f"receipt_{kind}.cpr"
        if kind=="hardlink":
            os.link(source,result)
        elif kind=="symlink":
            result.symlink_to(source)
        else:
            result=source
        attempt=subprocess.run([str(exe),"lab-discover",str(source),"1","1234",str(result)],capture_output=True,text=True)
        assert attempt.returncode!=0,(kind,attempt.stdout,attempt.stderr)
        assert source.read_bytes()==payload,kind
        checks+=2
print(f"PASS receipt source-alias rejection: {checks} checks, hardlink/symlink/identical path preserved")
