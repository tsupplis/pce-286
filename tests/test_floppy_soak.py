#!/usr/bin/env python3
"""Repeated native IBM/AMI DOS floppy writes/readback and guest reboots.

No timing parameters are changed. Requires local ROMs, mtools and nasm.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import time
from test_at_native_boot import ROOT, prepare_native_image, run


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--emulator', type=Path, required=True)
    p.add_argument('--bios', nargs='+', choices=['ibm', 'ami'], default=['ibm', 'ami'])
    p.add_argument('--cycles', type=int, default=3)
    p.add_argument('--rounds', type=int, default=64)
    p.add_argument('--timeout', type=int, default=600)
    p.add_argument('--output', type=Path, default=ROOT/'tests/memory-validation/floppy-soak')
    args = p.parse_args()
    if not 1 <= args.rounds <= 65535 or args.cycles < 1:
        p.error('positive cycles and rounds in 1..65535 required')
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    source = ROOT/'images/dos.img'
    digest = lambda: hashlib.sha256(source.read_bytes()).hexdigest()
    before = digest()
    results = []
    started = time.monotonic()
    (output/'results.json').write_text('[]\n')
    emulator = args.emulator.resolve()
    (output/'run.json').write_text(json.dumps(dict(emulator=str(emulator),
        sha256=hashlib.sha256(emulator.read_bytes()).hexdigest(), bios=args.bios,
        cycles=args.cycles, rounds=args.rounds, bytes_per_file=8193), indent=2)+'\n')
    try:
        for bios in args.bios:
            for cycle in range(1, args.cycles+1):
                folder = output/f'{bios}-{cycle}'
                folder.mkdir(exist_ok=True)
                payload = folder/'soak.com'
                run(['nasm', '-f', 'bin', f'-DROUNDS={args.rounds}', '-o', str(payload),
                     'tests/asm/tfloppysoak.asm'])
                image, cfg = prepare_native_image(folder, 'soak', payload.read_bytes(),
                                                 readonly=False, bios=bios)
                configuration = re.sub(r'#[^\n]*', '', cfg.read_text())
                roms = re.findall(r'file\s*=\s*"([^"\n]+\.rom)"', configuration)
                expected_bios = 'ami-at-158c.rom' if bios == 'ami' else 'ibm-at-1985-11-15-02.rom'
                assert sorted(roms) == sorted([expected_bios, 'ibm-vga.rom']), roms
                # Evidence that the workload reaches the controller, not just
                # DOS or a PCE INT13 disk shortcut. No timing options change.
                configuration, count = re.subn(r'\bfdc\s*\{', 'fdc {\n verbose = 1', configuration)
                assert count == 1
                cfg.write_text(configuration)
                commands = ('gb\ns cpu\nkey a +Shift a -Shift Return\ngb\ns cpu\n'
                            'key +Ctrl +Alt Delete -Alt -Ctrl\ngb\ns cpu\nq\n')
                log = folder/'soak.log'
                print(f'START {bios} cycle {cycle}/{args.cycles}: {args.rounds} writes/readbacks', flush=True)
                begin = time.monotonic()
                with log.open('w') as stream:
                    result = subprocess.run([str(emulator), '-c', str(cfg), '-t', 'null'],
                        input=commands, text=True, cwd=ROOT, stdout=stream,
                        stderr=subprocess.STDOUT, timeout=args.timeout,
                        env=dict(os.environ, ASAN_OPTIONS='detect_leaks=0', UBSAN_OPTIONS='halt_on_error=1'))
                text = log.read_text()
                assert result.returncode == 0, (result.returncode, log)
                assert all(m in text for m in ('BX=1111', 'BX=2222', 'DI=3333')), log
                assert 'ERROR: AddressSanitizer' not in text and 'runtime error:' not in text, log
                workload = text.split('BX=1111', 1)[1].split('BX=2222', 1)[0]
                reads = len(re.findall(r'E8272: CMD=.*  READ +\[.*  TC', workload))
                writes = len(re.findall(r'E8272: CMD=.*  WRITE +\[.*  TC', workload))
                assert reads >= args.rounds and writes >= args.rounds, (reads, writes, log)
                recovered = subprocess.check_output(['mtype', '-i', str(image), '::VERIFY.BIN'])
                expected = bytearray(((i*37)^(i>>3)^0xa5)&255 for i in range(8193))
                expected[0] = (0xa5 + args.rounds - 1)&255
                assert recovered == expected, f'host readback mismatch: {image}'
                results.append(dict(bios=bios, cycle=cycle, rounds=args.rounds,
                                    fdc_read_completions=reads, fdc_write_completions=writes,
                                    seconds=round(time.monotonic()-begin, 2), passed=True))
                (output/'results.json').write_text(json.dumps(results, indent=2)+'\n')
                print(f'PASS {bios} cycle {cycle}: guest reboot and host readback verified', flush=True)
    finally:
        assert digest() == before, 'source disk changed'
    print(f'PASS {len(results)} cycles, {len(results)*args.rounds} write/readback rounds, '
          f'{time.monotonic()-started:.1f}s elapsed; source unchanged', flush=True)


if __name__ == '__main__':
    main()
