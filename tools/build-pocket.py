#!/usr/bin/env python3
"""Build the separate ARM64 Pocket provider and optional installer ZIP."""
import argparse
from pathlib import Path
import subprocess
import zipfile
ROOT=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--ndk',required=True,type=Path)
p.add_argument('--out',type=Path,default=ROOT/'build/pocket')
a=p.parse_args()
a.ndk=a.ndk.resolve()
a.out=a.out.resolve()
def run(*argv):subprocess.run(list(map(str,argv)),check=True,cwd=ROOT)
run('cmake','-S',ROOT/'loader/src','-B',a.out,'-G','Ninja',
    '-DCMAKE_TOOLCHAIN_FILE='+str(a.ndk/'build/cmake/android.toolchain.cmake'),
    '-DANDROID_ABI=arm64-v8a','-DANDROID_PLATFORM=android-28','-DANDROID_STL=c++_static',
    '-DCMAKE_BUILD_TYPE=Release','-DNEO_BUILD_POCKET=ON','-DZKSU_VERSION=v2.4-pocket')
run('cmake','--build',a.out,'--target','neozygisk_pocket','neo_pocket_fixture','-j','4')
with zipfile.ZipFile(a.out/'neozygisk-pocket-0.1.zip','w',zipfile.ZIP_DEFLATED) as z:
    for name in ('module.prop','customize.sh'):z.write(ROOT/'pocket'/name,name)
    z.write(ROOT/'LICENSE','LICENSE')
    z.write(a.out/'libneozygisk_pocket.so','system/lib64/libpocket_zygisk.so')
print(a.out/'neozygisk-pocket-0.1.zip')
