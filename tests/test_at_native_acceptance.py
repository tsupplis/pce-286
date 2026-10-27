#!/usr/bin/env python3
"""Native AT acceptance through BIOS keys, DOS writes, and guest Ctrl-Alt-Del.

Keyboard events enter through the monitor's normal scan-code queue; this is
headless input-path acceptance, not manual GUI/physical-keyboard testing.
"""
import argparse
import hashlib
import os
from pathlib import Path
import shutil
import subprocess
from test_at_native_boot import ROOT, prepare_native_image, run


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--emulator', required=True, type=Path)
    parser.add_argument('--bios', choices=('ibm', 'ami'), default='ibm')
    parser.add_argument('--full-ram', action='store_true', help='Keep configured 15232K extended RAM')
    parser.add_argument('--timeout', type=int, help='Seconds for both boots (default depends on BIOS/RAM)')
    parser.add_argument('--output', type=Path, default=ROOT / 'tests/memory-validation/native-acceptance')
    args = parser.parse_args()
    timeout = args.timeout or (1200 if args.bios == 'ami' and args.full_ram else
                               600 if args.bios == 'ami' or args.full_ram else 150)
    ram = 15232 if args.full_ram else 1024
    print(f'Testing {args.bios.upper()} BIOS, 640K+{ram}K RAM; timeout={timeout}s', flush=True)
    for tool in ('mformat', 'mcopy', 'nasm'):
        if shutil.which(tool) is None:
            parser.error(f'{tool} is required')
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    source_hash = hashlib.sha256((ROOT / 'images/dos.img').read_bytes()).hexdigest()
    payload = output / 'tataccept.com'
    run(['nasm', '-f', 'bin', '-o', str(payload), 'tests/asm/tataccept.asm'])
    image, cfg = prepare_native_image(output, 'acceptance', payload.read_bytes(), readonly=False,
                                     bios=args.bios, full_ram=args.full_ram)
    commands = ('g\ns cpu\nkey a +Shift a -Shift Return\ng\ns cpu\n'
                'key +Ctrl +Alt Delete -Alt -Ctrl\ng\ns cpu\ng\nq\n')
    env = dict(os.environ, ASAN_OPTIONS='detect_leaks=0', UBSAN_OPTIONS='halt_on_error=1')
    log = output / 'acceptance.log'
    with log.open('w') as stream:
        result = subprocess.run([str(args.emulator.resolve()), '-c', str(cfg), '-t', 'null'],
                                input=commands, text=True, cwd=ROOT, env=env,
                                stdout=stream, stderr=subprocess.STDOUT, timeout=timeout)
    text = log.read_text()
    assert result.returncode == 0, (result.returncode, log)
    assert all(marker in text for marker in ('BX=1111', 'BX=2222', 'DI=3333')), 'Guest checkpoints missing'
    assert 'unknown key' not in text
    assert 'ERROR: AddressSanitizer' not in text and 'runtime error:' not in text
    recovered = output / 'VERIFY.BIN'
    run(['mcopy', '-o', '-i', str(image), '::VERIFY.BIN', str(recovered)])
    expected = bytes(((i * 37) ^ (i >> 3) ^ 0xa5) & 255 for i in range(2049))
    assert recovered.read_bytes() == expected, 'Host verification of persistent file failed'
    assert hashlib.sha256((ROOT / 'images/dos.img').read_bytes()).hexdigest() == source_hash
    print('PASS BIOS INT16 input: a, Shift+a, Return')
    print('PASS DOS create/write/close/reopen/read: 2049 bytes; host image matches')
    print('PASS guest Ctrl-Alt-Del: DOS boots again and verifies the persisted file')
    print('Original DOS image unchanged; no BIOS patches or host reset command used.')


if __name__ == '__main__':
    main()
