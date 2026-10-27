#!/usr/bin/env python3
"""
make-cmos.py — generate a valid MC146818A CMOS image for the PCE IBM PC/AT emulator.

Usage:
  python3 tools/make-cmos.py [options] -o OUTPUT.nvr

Options:
  -o FILE           Output file (default: at-cmos.nvr)
  --base KB         Conventional RAM in KB  (default: 640)
  --ext  KB         Extended RAM in KB      (default: 0)
  --fd0  TYPE       Drive 0 type: 0=none 1=360K 2=1.2M 3=720K 4=1.44M 5=2.88M (default: 4)
  --fd1  TYPE       Drive 1 type (default: 4)
  --display TYPE    Display: cga80 | cga40 | mono (default: cga80)
  --no-fpu          Mark FPU absent (default: absent — not emulated)

The output is a 64-byte binary suitable for the PCE [cmos] file= key.

CMOS layout written:
  0x0B  Reg B: 0x02  (24h BCD mode)
  0x0E  POST status: 0x00 (no error)
  0x10  Floppy types (hi=drive0, lo=drive1)
  0x12  HDD types: 0x00 (PCE uses INT 13h)
  0x14  Equipment byte
  0x15  Base memory low byte
  0x16  Base memory high byte
  0x17  Extended memory low byte
  0x18  Extended memory high byte
  0x2E  Checksum high byte (sum of 0x10-0x2D)
  0x2F  Checksum low byte
"""

import argparse
import struct
import sys


DISP_CGA80 = 0x02
DISP_CGA40 = 0x01
DISP_MONO  = 0x03


def build_cmos(base_kb, ext_kb, fd0, fd1, disp):
    data = bytearray(64)

    # Reg B: 24h mode, BCD, no IRQs
    data[0x0b] = 0x02

    # POST diagnostic: no errors
    data[0x0e] = 0x00

    # Floppy types
    data[0x10] = ((fd0 & 0x0f) << 4) | (fd1 & 0x0f)

    # HDD types: 0 — PCE uses INT 13h hook
    data[0x12] = 0x00

    # Equipment byte
    fd_count = (1 if fd0 else 0) + (1 if fd1 else 0)
    floppy_present = 1 if fd_count > 0 else 0
    floppy_count_bits = (fd_count - 1) if fd_count > 0 else 0
    data[0x14] = (
        ((floppy_count_bits & 0x03) << 6) |
        ((disp & 0x03) << 4) |
        floppy_present
    )

    # Base memory
    base_kb = min(base_kb, 640)
    data[0x15] = base_kb & 0xff
    data[0x16] = (base_kb >> 8) & 0xff

    # Extended memory
    ext_kb = min(ext_kb, 0xffff)
    data[0x17] = ext_kb & 0xff
    data[0x18] = (ext_kb >> 8) & 0xff

    # Checksum over bytes 0x10–0x2D
    cksum = sum(data[0x10:0x2e]) & 0xffff
    data[0x2e] = (cksum >> 8) & 0xff
    data[0x2f] = cksum & 0xff

    return data


def main():
    p = argparse.ArgumentParser(description='Generate PCE AT CMOS .nvr image')
    p.add_argument('-o', '--output', default='at-cmos.nvr', metavar='FILE')
    p.add_argument('--base',    type=int, default=640,  metavar='KB')
    p.add_argument('--ext',     type=int, default=0,    metavar='KB')
    p.add_argument('--fd0',     type=int, default=4,    metavar='TYPE')
    p.add_argument('--fd1',     type=int, default=4,    metavar='TYPE')
    p.add_argument('--display', default='cga80',
                   choices=['cga80', 'cga40', 'mono'])
    p.add_argument('--no-fpu',  action='store_true')
    args = p.parse_args()

    disp = {'cga80': DISP_CGA80, 'cga40': DISP_CGA40, 'mono': DISP_MONO}[args.display]
    data = build_cmos(args.base, args.ext, args.fd0, args.fd1, disp)

    with open(args.output, 'wb') as f:
        f.write(data)

    # Summary
    cksum = (data[0x2e] << 8) | data[0x2f]
    print(f'Written {len(data)} bytes to {args.output}')
    print(f'  Base RAM : {args.base} KB  ({args.base:#06x})')
    print(f'  Ext  RAM : {args.ext} KB  ({args.ext:#06x})')
    print(f'  Floppy 0 : type {args.fd0}')
    print(f'  Floppy 1 : type {args.fd1}')
    print(f'  Display  : {args.display} ({disp:#04x})')
    print(f'  0x10 fd  : {data[0x10]:#04x}')
    print(f'  0x14 eqp : {data[0x14]:#04x}')
    print(f'  0x15/16  : {data[0x15]:#04x} {data[0x16]:#04x}  ({args.base} KB)')
    print(f'  0x17/18  : {data[0x17]:#04x} {data[0x18]:#04x}  ({args.ext} KB)')
    print(f'  Checksum : {cksum:#06x}')


if __name__ == '__main__':
    main()
