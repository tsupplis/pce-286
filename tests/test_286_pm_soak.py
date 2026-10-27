#!/usr/bin/env python3
"""Short guest PM soak using a disposable copy of the native DOS acceptance fixture.
Prepare the fixture with test_at_native_disk.py first. No user media is modified.
"""
import argparse
import os
from pathlib import Path
import re
import shutil
import subprocess
import time

ROOT = Path(__file__).resolve().parent.parent


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--emulator', required=True, type=Path)
    p.add_argument('--fixture', required=True, type=Path)
    p.add_argument('--output', type=Path, default=ROOT / 'tests/memory-validation/pm-soak')
    p.add_argument('--runs', type=int, default=3)
    a = p.parse_args()
    out, fixture = a.output.resolve(), a.fixture.resolve()
    if out == fixture or not 1 <= a.runs <= 10:
        p.error('use a separate output directory and 1-10 runs')
    out.mkdir(parents=True, exist_ok=True)
    (out / 'result.txt').unlink(missing_ok=True)
    shutil.copy2(fixture / 'hd.img', out / 'hd.img')
    (out / 'native.cfg').write_text((fixture / 'native.cfg').read_text())
    subprocess.run(['nasm', '-f', 'bin', str(ROOT / 'tests/asm/tpmsoak.asm'), '-o', str(out / 'PMSOAK.COM')], check=True)
    (out / 'AUTOEXEC.BAT').write_bytes(b'@ECHO OFF\r\nC:\\PMSOAK.COM\r\n')
    image = (out / 'hd.img').read_bytes()
    offset = int.from_bytes(image[454:458], 'little') * 512
    for name in ('PMSOAK.COM', 'AUTOEXEC.BAT'):
        subprocess.run(['mcopy', '-o', '-i', f'{out / "hd.img"}@@{offset}', str(out / name), '::' + name], check=True)
    expected = bytearray(1024)
    for n in range(10000):
        expected[n & 1022:(n & 1022) + 2] = (n ^ 0xa55a).to_bytes(2, 'little')
    start = time.monotonic()
    for run in range(1, a.runs + 1):
        log = out / f'run-{run}.log'
        with log.open('w') as f:
            result = subprocess.run([str(a.emulator.resolve()), '-c', 'native.cfg', '-g', 'vga', '-t', 'null'],
                                    cwd=out, input='gb\ns cpu\nd 8000:0 400\nq\n', text=True,
                                    stdout=f, stderr=subprocess.STDOUT, timeout=60,
                                    env=dict(os.environ, ASAN_OPTIONS='detect_leaks=0', UBSAN_OPTIONS='halt_on_error=1'))
        text = log.read_text()
        assert result.returncode == 0, f'process failed: {log}'
        for marker in ('BX=0000', 'BP=2710', 'DI=CAFE', 'CS=0008', 'SS=0020'):
            assert marker in text, f'missing {marker}: {log}'
        assert 'ERROR: AddressSanitizer' not in text and 'runtime error:' not in text
        data = bytearray()
        for line in text.splitlines():
            if re.match(r'8000:[0-9A-F]{4} ', line):
                data.extend(bytes.fromhex(line[11:58].replace('-', ' ')))
        assert data == expected, f'physical memory mismatch (segment base/indexing): {log}'
        print(f'PASS run {run}: 10000 PM rounds, INT/IRET and physical memory verified', flush=True)
    summary = f'PASS {a.runs} fresh DOS boots/PM entries, {a.runs * 10000} CPL0 rounds; {time.monotonic() - start:.1f}s. No privilege/task-switch/PM-exit coverage.\n'
    (out / 'result.txt').write_text(summary)
    print(summary, end='')


if __name__ == '__main__':
    main()
