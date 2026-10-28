#!/usr/bin/env python3
"""Run fixed CPU guest images without opening test windows or hanging forever."""
import argparse
from pathlib import Path
import sys
from guest_runner import run_logged

ROOT = Path(__file__).resolve().parent.parent


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--emulator', type=Path, required=True)
    p.add_argument('--config', type=Path, default=ROOT / 'tests/test.cfg')
    p.add_argument('--timeout', type=float, default=30)
    p.add_argument('--output', type=Path, default=ROOT / 'tests/memory-validation/e2e')
    a = p.parse_args()
    if a.timeout <= 0:
        p.error('--timeout must be positive')
    if not a.emulator.is_file():
        p.error(f'emulator missing: {a.emulator}')
    cases = [('t186', c) for c in ('80186', '80188', 'v20', 'v30', '80286')]
    cases += [('tv20', c) for c in ('v20', 'v30')]
    cases += [(n, '80286') for n in ('t286', 'ta20', 'tprot', 'textmem', 'tloadall')]
    required = [a.config, ROOT / 'src/arch/ibmpc/bios/ibmpc-pcex.rom']
    required += [ROOT / 'testroms' / n for n in ('ibm-basic-1.10.rom', 'glabios-5160-0.4.2.rom', 'ibm-vga.rom')]
    if any((ROOT / 'images' / (n + '.img')).is_file() for n, c in cases):
        missing = [str(f) for f in required if not f.is_file()]
        if missing:
            p.error('missing test prerequisites:\n  ' + '\n  '.join(missing) + '\nSee README.md.')
    failed = skipped = 0
    for name, cpu in cases:
        image = ROOT / 'images' / (name + '.img')
        if not image.is_file():
            print(f'  SKIP   {image.name}: run make check-images', flush=True)
            skipped += 1
            continue
        print(f'  TEST   {image.name} cpu={cpu}', flush=True)
        cmd = [a.emulator.resolve(), '-R', '-c', a.config.resolve(), '-p', cpu, '-t', 'null',
               '-I', 'disk {', '-I', 'drive = 0', '-I', 'type = "auto"',
               '-I', f'file = "{image}"', '-I', '}']
        failed += run_logged(cmd, a.output / f'{name}-{cpu}.log', a.timeout, ROOT) != 0
    print(f'CPU guest tests: {len(cases)-failed-skipped} passed, {failed} failed, {skipped} skipped.')
    return int(bool(failed))


if __name__ == '__main__':
    sys.exit(main())
