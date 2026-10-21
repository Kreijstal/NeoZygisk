#!/usr/bin/env python3
"""Verify a running NeoZygisk Pocket provider using the Pi's paired owner console."""
import argparse
import io
import json
from pathlib import Path
import re
import sys
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--config',type=Path,default=Path.home()/'.config/pocket-pi-debug.json')
p.add_argument('--app-report',type=Path,default=Path.home()/'neo-pocket-app-validation.json')
p.add_argument('--output',type=Path,default=Path.home()/'neo-pocket-validation.json')
a=p.parse_args()
sys.path.insert(0,'/home/pi/lineage-cuttlefish/tools')
import cuttlefish_arm64 as c
from pocketctl import request
c.require_virtual()
config=json.loads(a.config.read_text())
def call(op,argv=()):
    out,err=io.BytesIO(),io.BytesIO()
    code=request(config,op,argv,stdout=out,stderr=err)
    assert code==0,(code,err.getvalue().decode(errors='replace'))
    return out.getvalue().decode(errors='replace')
assert json.loads(call(1))['ready']
log=call(2,['/system/bin/sh','-c',"/system/bin/toybox grep -E 'NEO_POCKET_READY|POCKET_ZYGISK_INSTALL' /logs/manager-init.log | /system/bin/toybox tail -c 8000"])
assert 'NEO_POCKET_READY' in log and 'POCKET_ZYGISK_INSTALL result=0' in log,log
events=call(2,['/system/bin/toybox','tail','-c','16000','/data/local/tmp/neo-pocket-events'])
rows=[(name,int(uid),int(target),int(pid)) for name,uid,target,pid in
      re.findall(r'^(\w+) uid=(\d+) target=(-?\d+) pid=(\d+)$',events,re.M)]
app_pids={pid for name,uid,target,pid in rows if name=='preApp' and target==10019}
assert len(app_pids)==2,events
required={'onLoad','preApp','moduleDir','flags','unsupportedCompanion','jniHook','pltHook','postApp','exemptFd'}
for pid in app_pids:
    own=[r for r in rows if r[3]==pid]
    assert required<={r[0] for r in own},(pid,events)
    assert any(r[:3]==('preApp',0,10019) for r in own),own
    assert any(r[:3]==('postApp',10019,10019) for r in own),own
assert any(r[:3]==('preServer',0,1000) for r in rows),events
assert any(r[:3]==('postServer',1000,1000) for r in rows),events
app=json.loads(a.app_report.read_text())
assert app['exitCode']==0 and app['profiles']['adapted']['directOk']
assert not app['profiles']['baseline']['directOk']
assert app['profiles']['adapted']['kernelUid']==app['profiles']['baseline']['kernelUid']!=0
report={'environment':'Pi LineageOS ARM64 VM -> Pocket Android 9',
        'command':'python3 /home/pi/verify-neo-pocket.py','exitCode':0,
        'providerLog':log,'events':events,'appProcesses':sorted(app_pids),
        'appResult':app,'companionSupported':False,'kernelNamespacesSupported':False}
a.output.write_text(json.dumps(report,indent=2)+'\n')
print('PASS: Neo provider; server/app lifecycle, JNI, PLT, module directory, FD exemption, unsupported companion, selected syscall translation')
