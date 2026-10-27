#!/usr/bin/env python3
"""Test a disposable copy of the supplied IBM XT DOS hard disk.

Default reboot is an explicit emulator reset, not guest Ctrl-Alt-Del.
--reboot keyboard exercises the guest path, including original BIOS POST.
Requires the user's local disk/config and ROMs; none are distributed here.
"""
import argparse
import hashlib
import os
from pathlib import Path
import shutil
import subprocess

ROOT = Path(__file__).resolve().parent.parent


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--emulator', required=True, type=Path)
    parser.add_argument('--setup', type=Path, default=ROOT / 'ibmxt-dos-5.0')
    parser.add_argument('--rom-dir', required=True, type=Path)
    parser.add_argument('--reboot', choices=('reset', 'keyboard'), default='reset')
    parser.add_argument('--output', type=Path,
                        default=ROOT / 'tests/memory-validation/xt-daily-acceptance')
    parser.add_argument('--timeout', type=int, default=60)
    args = parser.parse_args()
    for tool in ('mtype', 'mcopy', 'nasm'):
        if shutil.which(tool) is None:
            parser.error(f'{tool} is required')
    setup, output = args.setup.resolve(), args.output.resolve()
    source = setup / 'hd0-ibmxt.img'
    image = output / 'hd0-ibmxt.img'
    if image == source:
        parser.error('output must not be the source setup directory')
    output.mkdir(parents=True, exist_ok=True)
    # Do not leave a previous passing summary beside a failed rerun.
    (output / 'result.txt').unlink(missing_ok=True)
    original = source.read_bytes()
    source_hash = hashlib.sha256(original).hexdigest()
    # This fixture has an active FAT16 partition in the first MBR entry.
    entry = original[446:462]
    if original[510:512] != b'\x55\xaa' or entry[0] != 0x80 or entry[4] not in (4, 6):
        parser.error('expected active FAT16 partition in first MBR entry')
    offset = int.from_bytes(entry[8:12], 'little') * 512
    shutil.copy2(source, image)
    volume = f'{image}@@{offset}'

    def run(*command):
        return subprocess.check_output(command, cwd=ROOT)

    autoexec = run('mtype', '-i', volume, '::AUTOEXEC.BAT')
    # Retain the existing DOS drivers and startup commands.
    (output / 'AUTOEXEC.BAT').write_bytes(autoexec + b'\r\nC:\\XTACCEPT.COM\r\n')
    assembly = (ROOT / 'tests/asm/tataccept.asm').read_text()
    assembly = assembly.replace('cpu 286', 'cpu 8086')
    assembly = assembly.replace("'VERIFY.BIN'", "'C:\\XTVERIFY.BIN'")
    (output / 'accept.asm').write_text(assembly)
    run('nasm', '-f', 'bin', '-o', str(output / 'XTACCEPT.COM'), str(output / 'accept.asm'))
    # A previous result in the supplied image must not masquerade as a new test.
    check = subprocess.run(['mtype', '-i', volume, '::XTVERIFY.BIN'],
                           stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    if check.returncode == 0:
        parser.error('source already contains XTVERIFY.BIN; use a fresh source fixture')
    for name in ('AUTOEXEC.BAT', 'XTACCEPT.COM'):
        run('mcopy', '-o', '-i', volume, str(output / name), '::' + name)
    cfg = (setup / 'ibmxt.cfg').read_text()
    cfg = cfg.replace('path = "../rom"', f'path = "{args.rom_dir.resolve()}"')
    cfg = cfg.replace('sdl:lowpass=0', 'null')
    (output / 'accept.cfg').write_text(cfg)
    reboot = 'm emu.reset 1' if args.reboot == 'reset' else 'key +Ctrl +Alt Delete -Alt -Ctrl'
    # gb checks the stop flag after each instruction. At speed=24, g can
    # execute both the final STOP and ABORT in one batch, then resume past exit.
    # The IBM 1986 common POST failure address makes the known keyboard-path
    # checksum failure immediate instead of waiting for a long timeout.
    resume = 'gb' if args.reboot == 'reset' else 'gb f000:e0ab'
    commands = ('gb\ns cpu\nkey a +Shift a -Shift Return\ngb\ns cpu\n'
                f'{reboot}\n{resume}\ns cpu\nq\n')
    (output / 'monitor.txt').write_text(commands)
    log = output / 'acceptance.log'
    try:
        with log.open('w') as stream:
            result = subprocess.run(
                [str(args.emulator.resolve()), '-c', 'accept.cfg', '-g', 'vga', '-t', 'null'],
                cwd=output, input=commands, text=True, stdout=stream,
                stderr=subprocess.STDOUT, timeout=args.timeout,
                env=dict(os.environ, ASAN_OPTIONS='detect_leaks=0', UBSAN_OPTIONS='halt_on_error=1'))
        text = log.read_text()
        assert result.returncode == 0, f'emulator exit {result.returncode}: {log}'
        for marker in ('BX=1111', 'BX=2222', 'DI=3333'):
            assert marker in text, f'missing {marker}; reboot={args.reboot}; inspect {log}'
        assert 'ERROR: AddressSanitizer' not in text and 'runtime error:' not in text
        recovered = run('mtype', '-i', volume, '::XTVERIFY.BIN')
        assert recovered == bytes(((i * 37) ^ (i >> 3) ^ 0xa5) & 255 for i in range(2049))
    finally:
        assert hashlib.sha256(source.read_bytes()).hexdigest() == source_hash, 'source changed'
    message = (f'PASS XT BIOS keys, DOS hard-disk write/readback, reboot={args.reboot}, '
               'post-boot verification and host extraction. Source disk unchanged.\n')
    (output / 'result.txt').write_text(message + f'Source SHA256: {source_hash}\n')
    print(message, end='')


if __name__ == '__main__':
    main()
