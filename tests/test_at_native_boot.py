#!/usr/bin/env python3
"""Boot native AT DOS with IBM or AMI ROMs from disposable 1.2 MB FAT12 images.

Requires mtools, the local BIOS/VGA ROMs and images/dos.img. No ROM, source
image or tracked configuration is changed. Only the final COM exit uses a
PCE hook; POST, boot-sector and DOS disk access run through the original BIOS.
"""
import argparse
import hashlib
import os
from pathlib import Path
import re
import shutil
import struct
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def run(args, **kwargs):
    return subprocess.run(args, check=True, cwd=ROOT, **kwargs)


def prepare_native_image(output, stem, payload, readonly=True, bios='ibm', full_ram=False):
    """Create a fresh disposable image/config; return their paths."""
    source = ROOT / 'images/dos.img'
    original = source.read_bytes()
    original_hash = hashlib.sha256(original).hexdigest()
    for name in ('IBMBIO.COM', 'IBMDOS.COM', 'COMMAND.COM'):
        run(['mcopy', '-o', '-i', str(source), '::' + name, str(output / name)])
    (output / 'AUTOEXEC.BAT').write_bytes(
        b'@ECHO OFF\r\nECHO NATIVE AT DOS BOOT REACHED\r\nNATIVE.COM\r\n')
    config_name = 'at-5170-ami.cfg' if bios == 'ami' else 'at-5170.cfg'
    config = (ROOT / 'tests' / config_name).read_text()
    assert re.search(r'patch_bios_init\s*=\s*0', config)
    assert re.search(r'patch_bios_int19\s*=\s*0', config)
    if not full_ram:
        config, count = re.subn(r'(size\s*=\s*)15232K', r'\g<1>1024K', config)
        assert count == 1, 'Expected one extended-RAM block'
    image = output / f'{stem}.img'
    run(['mformat', '-C', '-f', '1200', '-i', str(image), '::'])
    data = bytearray(image.read_bytes())
    boot = bytearray(original[:512])
    boot[11:36] = data[11:36]
    data[:512] = boot
    image.write_bytes(data)
    (output / 'NATIVE.COM').write_bytes(payload)
    for name in ('IBMBIO.COM', 'IBMDOS.COM', 'COMMAND.COM', 'AUTOEXEC.BAT', 'NATIVE.COM'):
        run(['mcopy', '-o', '-i', str(image), str(output / name), '::' + name])
    cfg = output / f'{stem}.cfg'
    cfg.write_text(config + '\ndisk { drive = 0 type = "auto" file = "' +
                   str(image) + '" readonly = ' + str(int(readonly)) + ' }\n')
    assert hashlib.sha256(source.read_bytes()).hexdigest() == original_hash
    return image, cfg


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--emulator', type=Path, required=True)
    parser.add_argument('--bios', choices=('ibm', 'ami'), default='ibm')
    parser.add_argument('--full-ram', action='store_true', help='Keep configured 15232K extended RAM')
    parser.add_argument('--timeout', type=int, help='Seconds per boot (default depends on BIOS/RAM)')
    parser.add_argument('--output', type=Path, default=ROOT / 'tests/memory-validation/native-boot')
    args = parser.parse_args()
    timeout = args.timeout or (1200 if args.bios == 'ami' and args.full_ram else
                               300 if args.bios == 'ami' or args.full_ram else 90)
    ram = 15232 if args.full_ram else 1024
    print(f'Testing {args.bios.upper()} BIOS, 640K+{ram}K RAM; timeout={timeout}s per boot', flush=True)
    emulator = args.emulator.resolve()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    for tool in ('mformat', 'mcopy'):
        if shutil.which(tool) is None:
            parser.error(f'{tool} is required')
    env = dict(os.environ, ASAN_OPTIONS='detect_leaks=0', UBSAN_OPTIONS='halt_on_error=1')
    for result in (0, 1):
        payload = bytes.fromhex('b80101bb') + struct.pack('<H', result) + bytes.fromhex('d500f4')
        image, cfg = prepare_native_image(output, f'boot-{result}', payload,
                                         bios=args.bios, full_ram=args.full_ram)
        log = output / f'boot-{result}.log'
        with log.open('w') as stream:
            process = subprocess.run([str(emulator), '-R', '-c', str(cfg), '-t', 'null'],
                                     cwd=ROOT, env=env, stdout=stream, stderr=subprocess.STDOUT,
                                     timeout=timeout)
        content = log.read_text()
        assert process.returncode == result, (result, process.returncode, log)
        assert 'ERROR: AddressSanitizer' not in content and 'runtime error:' not in content
        assert 'model=5170' in content and 'chs=80/2/15' in content
        print(f'PASS native {args.bios.upper()} AT / 640K+{ram}K RAM / 1.2 MB floppy / DOS guest result {result}', flush=True)
    print('Original DOS image unchanged; BIOS initialization and INT19 patches disabled.')


if __name__ == '__main__':
    main()
