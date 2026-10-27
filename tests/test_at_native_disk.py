#!/usr/bin/env python3
"""Native AMI/WD1003 DOS acceptance using a disposable copy of the user's disk.

Requires the supplied IBM DOS FAT16 disk, AMI 12/15/89 BIOS, VGA ROM, nasm
and mtools. No PCEX ROM or BIOS disk hooks are used. Original media is untouched.
"""
import argparse
import hashlib
import os
from pathlib import Path
import struct
import subprocess

ROOT = Path(__file__).resolve().parent.parent


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--emulator', required=True, type=Path)
    parser.add_argument('--disk', type=Path, default=ROOT / 'ibmxt-dos-5.0/hd0-ibmxt.img')
    parser.add_argument('--bios', required=True, type=Path)
    parser.add_argument('--vga-rom', required=True, type=Path)
    parser.add_argument('--output', type=Path, default=ROOT / 'tests/memory-validation/native-wd')
    parser.add_argument('--timeout', type=int, default=180)
    args = parser.parse_args()
    out = args.output.resolve()
    source = args.disk.resolve()
    image = out / 'hd.img'
    if image == source:
        parser.error('output must not overwrite the source image')
    original = source.read_bytes()
    digest = hashlib.sha256(original).digest()
    data = bytearray(original)
    start, count = struct.unpack_from('<II', data, 454)
    if data[510:512] != b'\x55\xaa' or data[446] != 128 or data[450] not in (4, 6):
        parser.error('expected active FAT16 partition in first MBR entry')
    if not start or start + count > len(data) // 512 or len(data) > 940 * 8 * 17 * 512:
        parser.error('partition does not fit the type 4 test geometry')
    out.mkdir(parents=True, exist_ok=True)
    (out / 'result.txt').unlink(missing_ok=True)

    # Change only the disposable image's geometry, retaining its FAT volume.
    def chs(lba):
        cylinder, rest = divmod(lba, 8 * 17)
        head, sector = divmod(rest, 17)
        return bytes((head, (sector + 1) | ((cylinder >> 2) & 192), cylinder & 255))

    data[447:450] = chs(start)
    data[451:454] = chs(start + count - 1)
    struct.pack_into('<HH', data, start * 512 + 24, 17, 8)
    image.write_bytes(data)
    with image.open('ab') as stream:
        stream.truncate(940 * 8 * 17 * 512)
    volume = f'{image}@@{start * 512}'
    assembly = (ROOT / 'tests/asm/tataccept.asm').read_text().replace("'VERIFY.BIN'", "'C:\\NATIVE.BIN'")
    (out / 'accept.asm').write_text(assembly)
    subprocess.run(['nasm', '-f', 'bin', str(out / 'accept.asm'), '-o', str(out / 'ACCEPT.COM')], check=True)
    old = subprocess.run(['mtype', '-i', volume, '::NATIVE.BIN'], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    if old.returncode == 0:
        parser.error('source already contains NATIVE.BIN; use a fresh fixture')
    (out / 'AUTOEXEC.BAT').write_bytes(b'@ECHO OFF\r\nPATH C:\\DOS;C:\\PCE;C:\\BIN\r\nC:\\ACCEPT.COM\r\n')
    for name in ('ACCEPT.COM', 'AUTOEXEC.BAT'):
        subprocess.run(['mcopy', '-o', '-i', volume, str(out / name), '::' + name], check=True)
    cfg = f'''path="."
system {{ model="5170" boot=128 memtest=1 floppy_disk_drives=2 patch_bios_init=0 patch_bios_int19=0 }}
cpu {{ model="80286" speed=24 }}
ram {{ address=0 size=640K }}
ram {{ address=0x100000 size=4096K }}
rom {{ address=0xf0000 size=64K }}
load {{ format="binary" address=0xf0000 file="{args.bios.resolve()}" }}
video {{ device="vga" enable_irq=0 rom {{ address=0xc0000 size=24K file="{args.vga_rom.resolve()}" }} }}
terminal {{ driver="null" }}
speaker {{ driver="null" }}
ems {{ size=16M segment=0xe000 }}
xms {{ xms_size=0 umb_size=88K umb_segm=0xca00 hma=1 }}
cmos {{ floppy0_type=1 floppy1_type=1 hard_disk0_type=4 }}
fdc {{ drive0=0 drive1=1 accurate=0 }}
wd1003 {{ drive0=128 }}
disk {{ drive=128 type="auto" file="hd.img" c=940 h=8 s=17 }}
'''
    (out / 'native.cfg').write_text(cfg)
    commands = ('gb\ns cpu\nkey a +Shift a -Shift Return\ngb\ns cpu\n'
                'key +Ctrl +Alt Delete -Alt -Ctrl\ngb\ns cpu\nq\n')
    (out / 'monitor.txt').write_text(commands)
    try:
        with (out / 'acceptance.log').open('w') as log:
            result = subprocess.run([str(args.emulator.resolve()), '-c', 'native.cfg', '-g', 'vga', '-t', 'null'],
                                    cwd=out, input=commands, text=True, stdout=log, stderr=subprocess.STDOUT,
                                    timeout=args.timeout, env=dict(os.environ, ASAN_OPTIONS='detect_leaks=0', UBSAN_OPTIONS='halt_on_error=1'))
        text = (out / 'acceptance.log').read_text()
        assert result.returncode == 0, 'emulator failed; inspect acceptance.log'
        for marker in ('BX=1111', 'BX=2222', 'DI=3333'):
            assert marker in text, f'missing {marker}; inspect acceptance.log'
        assert 'ERROR: AddressSanitizer' not in text and 'runtime error:' not in text
        recovered = subprocess.check_output(['mtype', '-i', volume, '::NATIVE.BIN'])
        assert recovered == bytes(((i * 37) ^ (i >> 3) ^ 0xa5) & 255 for i in range(2049))
    finally:
        assert hashlib.sha256(source.read_bytes()).digest() == digest, 'source changed'
    message = ('PASS native AMI WD1003 DOS boot, BIOS keys, file write/readback, Ctrl-Alt-Del, '
               'post-reboot verification and host extraction; PCEX absent; source unchanged.\n')
    (out / 'result.txt').write_text(message + 'Source SHA256: ' + digest.hex() + '\n')
    print(message, end='')


if __name__ == '__main__':
    main()
