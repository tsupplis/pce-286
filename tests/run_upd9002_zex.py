"""Run unmodified CP/M ZEX diagnostics under DOS on an emulated XT."""
import argparse
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parent.parent


def run(name, binary, output, timeout):
    with tempfile.TemporaryDirectory(prefix=f'pce-{name}-') as tmp:
        work = Path(tmp)
        image = work / 'boot.img'
        shutil.copyfile(ROOT / 'images/dos.img', image)
        shutil.copyfile(binary, work / 'guest.cpm')
        batch = work / 'autoexec.bat'
        batch.write_bytes(b'@echo off\r\nzexrun guest.cpm >result.txt\r\npceexit\r\n')
        subprocess.run(['mcopy', '-o', '-i', image, ROOT / 'tests/asm/zexrun.com',
                        ROOT / 'src/arch/ibmpc/pceutils/pceexit.com',
                        work / 'guest.cpm', batch, '::'], check=True)
        command = [ROOT / 'src/arch/ibmpc/pce-ibmpc', '-R', '-c', ROOT / 'tests/test.cfg',
                   '-p', 'upd9002', '-t', 'null', '-I', 'disk {', '-I', 'drive = 0',
                   '-I', 'type = "auto"', '-I', f'file = "{image}"', '-I', '}']
        with (output / f'{name}.log').open('w') as log:
            subprocess.run(command, cwd=ROOT, stdout=log, stderr=log,
                           timeout=timeout, check=True)
        report = subprocess.check_output(['mtype', '-i', image, '::RESULT.TXT'])
    text = report.decode('ascii').replace('\n\r', '\n')
    (output / f'{name}.txt').write_text(text)
    passed = sum(line.endswith('  OK') for line in text.splitlines())
    print(f'{name}: {passed}/67 groups passed; see {output / (name + ".txt")}', flush=True)
    return passed == 67 and 'Tests complete' in text and 'ERROR' not in text


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('directory', type=Path, help='directory containing zexdoc.com and zexall.com')
    parser.add_argument('--output', type=Path, default=ROOT / 'tests/memory-validation/zex')
    parser.add_argument('--timeout', type=int, default=3600, help='seconds per suite')
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    results = [run(name, args.directory / f'{name}.com', args.output, args.timeout)
               for name in ('zexdoc', 'zexall')]
    return 0 if all(results) else 1


if __name__ == '__main__':
    raise SystemExit(main())
