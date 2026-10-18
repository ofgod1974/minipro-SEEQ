"""Build with an x64 MinGW-w64 GCC (e.g. w64devkit) and a static zlib.
Usage: python tools/build_windows_seeq.py --gcc PATH/gcc.exe --zlib PATH/zlib
Use --test to build and execute the protocol tests instead of minipro.
"""
import argparse
from pathlib import Path
import subprocess

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--gcc',required=True,type=Path)
p.add_argument('--zlib',type=Path)
p.add_argument('--test',action='store_true')
a=p.parse_args()
root=Path(__file__).resolve().parents[1]
build=root/('build-seeq-tests' if a.test else 'build-seeq');build.mkdir(exist_ok=True)
gcc=a.gcc.resolve()
flags=['-std=gnu11','-O2','-g','-ffunction-sections','-fdata-sections','-iquote',str(root/'src')]
if a.test:
    sources=['src/seeq52b33.c','src/t48.c','src/bitbang.c','tests/test_seeq52b33.c']
    flags+=['-DSEEQ52B33_TEST']
    exe=build/'test-seeq52b33.exe'
else:
    if not a.zlib:p.error('--zlib is required for the program build')
    zlib=a.zlib.resolve()
    flags+=['-I'+str(zlib),'-DSHARE_INSTDIR="."']
    # This backend uses Windows USB APIs; libusb is not required.
    sources=[str(s.relative_to(root)) for s in (root/'src').glob('*.c')
             if s.name not in ('usb_nix.c','version.c')]
    (root/'src/version.h').write_text('#define VERSION "0.7.4-seeq1"\n'
        '#define GIT_BRANCH "seeq52b33"\n#define GIT_HASH "cfe2171+seeq"\n'
        '#define GIT_DATE "2026-10-18"\n#define SHARE_INSTDIR "."\n')
    exe=build/'minipro.exe'
objects=[]
for src in sources:
    obj=build/(Path(src).stem+'.o');objects.append(str(obj))
    extra=['-Wall','-Wextra','-Werror'] if Path(src).name in ('seeq52b33.c','test_seeq52b33.c') else []
    subprocess.run([str(gcc),*flags,*extra,'-c',str(root/src),'-o',str(obj)],check=True)
libs=[] if a.test else [str(zlib/'libz.a'),'-lsetupapi','-lwinusb','-lshlwapi']
subprocess.run([str(gcc),'-static','-Wl,--gc-sections',*objects,*libs,'-o',str(exe)],check=True)
if a.test:subprocess.run([str(exe)],check=True,cwd=root)
print('Built:',exe)
