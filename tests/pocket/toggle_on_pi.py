#!/usr/bin/env python3
"""Check module disable/re-enable in fresh dedicated fixture processes on the Pi."""
import io
import json
from pathlib import Path
import re
import subprocess
import sys
import time
sys.path.insert(0,'/home/pi/lineage-cuttlefish/tools')
import cuttlefish_arm64 as c
from pocketctl import request
c.require_virtual()
config=json.loads((Path.home()/'.config/pocket-pi-debug.json').read_text())
def call(*argv):
    out,err=io.BytesIO(),io.BytesIO()
    rc=request(config,2,argv,stdout=out,stderr=err)
    if rc:raise RuntimeError((rc,err.getvalue().decode(errors='replace')))
    return out.getvalue()
def stop_fixture():
    events=call('/system/bin/toybox','tail','-c','16000','/data/local/tmp/neo-pocket-events').decode()
    pids={int(x) for x in re.findall(r'^preApp uid=0 target=10019 pid=(\d+)$',events,re.M)}
    stopped=[]
    for pid in pids:
        try:cmd=call('/system/bin/toybox','cat',f'/proc/{pid}/cmdline').split(b'\0')[0]
        except RuntimeError:continue
        if cmd not in (b'dev.kreijstal.pocketsyscalltest:adapted',b'dev.kreijstal.pocketsyscalltest:baseline'):continue
        call('/system/bin/toybox','kill','-TERM',str(pid));stopped.append(pid)
    time.sleep(2)
    return stopped
marker='/data/adb/modules/pocket_syscall_profiles/disable'
profile='/data/adb/pocket-syscall-profiles/dev.kreijstal.pocketsyscalltest:adapted.profile'
original=call('/system/bin/toybox','cat',profile)
results={}
try:
    call('/system/bin/toybox','touch',marker)
    stopped=stop_fixture()
    subprocess.run(['python3','/home/pi/test_syscall_app_on_pi.py','--launch-existing','--expect-disabled','--output','/home/pi/neo-pocket-module-disabled.json'],check=True)
    results['disabled']={'stoppedFixturePids':stopped,'app':json.loads(Path('/home/pi/neo-pocket-module-disabled.json').read_text())}
finally:
    call('/system/bin/toybox','rm','-f',marker)
    stop_fixture()
subprocess.run(['python3','/home/pi/test_syscall_app_on_pi.py','--launch-existing','--output','/home/pi/neo-pocket-module-reenabled.json'],check=True)
results['reenabled']=json.loads(Path('/home/pi/neo-pocket-module-reenabled.json').read_text())
assert call('/system/bin/toybox','cat',profile)==original
results.update(exitCode=0,profilePreserved=True,hostRootUsed=False,command='python3 /home/pi/toggle-neo-pocket.py')
Path('/home/pi/neo-pocket-toggle-validation.json').write_text(json.dumps(results,indent=2)+'\n')
print('PASS: disable and re-enable affect fresh app processes; profile preserved; no host root')
