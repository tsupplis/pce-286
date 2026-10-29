#!/usr/bin/env python3
"""Decode raw NPXOVF.BIN hardware observations; never label them PASS/FAIL."""
import argparse
import struct
from pathlib import Path

MAGIC = b"NPXOVF1\0"
COUNT, SIZE = 192, 118

def decode(data):
    if len(data) < 12 or data[:8] != MAGIC:
        raise ValueError("not an NPXOVF version 1 capture")
    count, size = struct.unpack_from("<HH", data, 8)
    if (count, size) != (COUNT, SIZE) or len(data) != 12 + count * size:
        raise ValueError("unexpected record count/size or truncated capture")
    rows = []
    for index in range(count):
        r = data[12+index*size:12+(index+1)*size]
        ident, cw = struct.unpack_from("<HH", r)
        if ident != index:
            raise ValueError(f"record {index}: unexpected case id")
        kind, rest = divmod(index, 96)
        pc, rest = divmod(rest, 32)
        sign, rest = divmod(rest, 16)
        rc, rest = divmod(rest, 4)
        om, pm = divmod(rest, 2)
        width = (24, 53, 64)[pc]
        expected_cw = 0x47 | (0, 0x200, 0x300)[pc] | rc<<10 | om<<3 | pm<<5
        mask = (1<<64)-1
        a = mask if not kind else (mask << (64-width)) & mask
        b = mask if not kind else 1<<63
        ea, eb = 0x7ffe | sign<<15, (0x7ffe if not kind else 0x7ffe-width) | sign<<15
        if (cw & ~0x80) != expected_cw or r[4:24] != struct.pack("<QHQH", a, ea, b, eb):
            raise ValueError(f"record {index}: input/control metadata damaged")
        saved_cw, sw, tags = struct.unpack_from("<HHH", r, 24)
        sig, exp = struct.unpack_from("<QH", r, 38)  # saved logical ST(0)
        rows.append((index, kind, width, sign, rc, om, pm, cw, saved_cw, sw, tags, exp, sig))
    return rows

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("capture", type=Path)
    args = parser.parse_args()
    try:
        rows = decode(args.capture.read_bytes())
    except (OSError, ValueError) as exc:
        parser.error(str(exc))
    print("id,kind,precision,negative,rc,om,pm,input_cw,saved_cw,sw,tags,st0_exp,st0_sig")
    for row in rows:
        print(",".join(str(v) for v in row[:7]) + "," +
              ",".join(f"{v:04x}" for v in row[7:12]) + f",{row[12]:016x}")
    return 0

if __name__ == "__main__":
    raise SystemExit(main())
