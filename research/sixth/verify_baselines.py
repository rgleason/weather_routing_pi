#!/usr/bin/env python3
"""Verify baseline engine/common-core files byte-for-byte against v1.32."""
import hashlib
import json
import subprocess
from pathlib import Path
root=Path(__file__).resolve().parents[2]
base='1fa62c7'
folders=['vendor/weather-routing-engine','vendor/original-routing-engine','vendor/quick2-routing-engine']
files=subprocess.check_output(['git','ls-tree','-r','--name-only',base,'--',*folders],cwd=root,text=True).splitlines()
changed=[]
for name in files:
    before=subprocess.check_output(['git','show',f'{base}:{name}'],cwd=root)
    if (root/name).read_bytes()!=before:changed.append(name)
result=dict(baseline=base,files_checked=len(files),changed=changed,
            source_sha256={name:hashlib.sha256((root/name).read_bytes()).hexdigest() for name in files})
(root/'research/sixth/baseline-integrity.json').write_text(json.dumps(result,indent=2)+'\n')
print(f'{len(files)} baseline files checked; {len(changed)} changes')
raise SystemExit(bool(changed))
