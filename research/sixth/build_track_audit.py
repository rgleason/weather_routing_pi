#!/usr/bin/env python3
"""Build the independent control-replay client against qualified libraries."""
import argparse
import shlex
import subprocess
from pathlib import Path

ROOT=Path(__file__).resolve().parents[2]


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--build',type=Path,default=ROOT/'build-atlas')
    p.add_argument('--output',type=Path,default=ROOT/'build-atlas/tests/atlas_track_audit')
    a=p.parse_args()
    flags=shlex.split(subprocess.check_output(
        ['pkg-config','--cflags','--libs','jsoncpp'],text=True))
    includes=[ROOT/f'vendor/{s}' for s in ['atlas-routing-engine/include',
        'weather-routing-engine/include','weather-routing-engine/src',
        'original-routing-engine/include']]
    subprocess.run(['c++','-std=c++20','-O2','-pthread',
        *['-I'+str(s) for s in includes],str(Path(__file__).with_name('audit_tracks.cpp')),
        str(a.build/'tests/original-routing-engine/libhardened_original_engine.a'),
        str(a.build/'weather-routing-engine/libweather_routing_engine.a'),
        *flags,'-o',str(a.output)],check=True)
    print(a.output)


if __name__=='__main__':main()
