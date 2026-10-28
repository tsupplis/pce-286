#!/usr/bin/env python3
"""Makefile entry point for native AT tests; no sample installations required."""
import argparse
import os
from pathlib import Path
import shutil
import sys
from guest_runner import run_logged

ROOT = Path(__file__).resolve().parent.parent


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--suite', choices=('boot', 'acceptance', 'floppy', 'disk', 'pm'), required=True)
    p.add_argument('--emulator', type=Path, required=True)
    p.add_argument('--bios', choices=('ibm', 'ami'), default='ibm')
    p.add_argument('--full-ram', action='store_true')
    p.add_argument('--timeout', type=int)
    p.add_argument('--cycles', type=int, default=3)
    p.add_argument('--rounds', type=int, default=64)
    p.add_argument('--runs', type=int, default=3)
    p.add_argument('--disk', type=Path)
    p.add_argument('--fixture', type=Path)
    p.add_argument('--output', type=Path, required=True)
    a = p.parse_args()
    if a.timeout is not None and a.timeout <= 0:
        p.error('--timeout must be positive')
    if a.cycles < 1 or not 1 <= a.rounds <= 65535 or not 1 <= a.runs <= 10:
        p.error('positive cycles, rounds in 1..65535 and runs in 1..10 required')
    if not a.emulator.is_file() or not os.access(a.emulator, os.X_OK):
        p.error(f'emulator not found: {a.emulator}; build pce-ibmpc first')
    if a.suite == 'disk' and (a.disk is None or not a.disk.is_file()):
        p.error('set AT_DISK_IMAGE to a standalone bootable DOS FAT16 hard-disk image')
    if a.suite == 'pm' and (a.fixture is None or not all((a.fixture / n).is_file()
                                                       for n in ('hd.img', 'native.cfg'))):
        p.error('native disk fixture missing; run make check-at-disk AT_DISK_IMAGE=/path/to/disk.img')
    tools = ['mcopy']
    if a.suite in ('boot', 'acceptance', 'floppy'):
        tools.append('mformat')
    if a.suite != 'boot':
        tools.append('nasm')
    if a.suite in ('disk', 'floppy'):
        tools.append('mtype')
    required = []
    if a.suite != 'pm':
        bios = 'ami-at-158c.rom' if a.suite == 'disk' or a.bios == 'ami' else 'ibm-at-1985-11-15-02.rom'
        required = [ROOT / 'testroms' / bios, ROOT / 'testroms/ibm-vga.rom']
        if a.suite != 'disk':
            required.append(ROOT / 'images/dos.img')
    missing = [str(f) for f in required if not f.is_file()]
    missing += [f'{t} (tool on PATH)' for t in tools if shutil.which(t) is None]
    if missing:
        p.error('missing native AT prerequisites:\n  ' + '\n  '.join(missing) +
                '\nSee README.md. ROM-free tests: make check-unit or check-at-unit.')
    output = a.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    scripts = {'boot': 'test_at_native_boot.py', 'acceptance': 'test_at_native_acceptance.py',
               'floppy': 'test_floppy_soak.py', 'disk': 'test_at_native_disk.py',
               'pm': 'test_286_pm_soak.py'}
    cmd = [sys.executable, ROOT / 'tests' / scripts[a.suite], '--emulator', a.emulator.resolve(),
           '--output', output]
    if a.suite in ('boot', 'acceptance', 'floppy'):
        cmd += ['--bios', a.bios]
    if a.full_ram:
        if a.suite not in ('boot', 'acceptance'):
            p.error('--full-ram applies only to boot and acceptance')
        cmd.append('--full-ram')
    if a.suite == 'floppy':
        cmd += ['--cycles', str(a.cycles), '--rounds', str(a.rounds)]
    elif a.suite == 'disk':
        cmd += ['--disk', a.disk.resolve(), '--bios', ROOT / 'testroms/ami-at-158c.rom',
                '--vga-rom', ROOT / 'testroms/ibm-vga.rom']
    elif a.suite == 'pm':
        cmd += ['--fixture', a.fixture.resolve(), '--runs', str(a.runs)]
    if a.timeout is not None:
        cmd += ['--timeout', str(a.timeout)]
    defaults = {'boot': 1200 if a.full_ram else 300 if a.bios == 'ami' else 90,
                'acceptance': 1200 if a.full_ram else 600 if a.bios == 'ami' else 150,
                'floppy': 600, 'disk': 180, 'pm': 60}
    limit = a.timeout or defaults[a.suite]
    repeats = 2 if a.suite == 'boot' else a.cycles if a.suite == 'floppy' else a.runs if a.suite == 'pm' else 1
    return run_logged(cmd, output / 'runner.log', limit * repeats + 60, ROOT)


if __name__ == '__main__':
    sys.exit(main())
