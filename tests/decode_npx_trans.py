#!/usr/bin/env python3
"""Decode NPXTRANS.BIN observations. Does not declare hardware conformance."""
import argparse
import struct
from pathlib import Path

def decode(data):
    if len(data)!=12+80*122 or data[:8]!=b'NPXTRN1\0' or struct.unpack_from('<HH',data,8)!=(80,122):
        raise ValueError('wrong format or incomplete NPXTRANS capture')
    rows=[]
    for i in range(80):
        r=data[12+i*122:12+(i+1)*122]
        ident,cw,op,setup,reserved=struct.unpack_from('<HHBBH',r)
        kind,rest=divmod(i,8);rc,masked=divmod(rest,2)
        expected_op=0xf2 if kind<6 else 0xf0 if kind<8 else 0xf1 if kind==8 else 0xf9
        if ident!=i or reserved or op!=expected_op or setup!=(1 if kind==4 else 2 if kind==5 else 0) or (cw&~0x80)!=(0x346|(rc<<10)|(0x39 if masked else 0)):
            raise ValueError(f'case {i}: damaged metadata')
        saved_cw,sw,tags=struct.unpack_from('<HHH',r,28)
        registers=[struct.unpack_from('<QH',r,42+j*10) for j in range(8)]
        rows.append((i,kind,rc,masked,cw,saved_cw,sw,tags,registers))
    return rows

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('capture',type=Path)
    args=parser.parse_args()
    try:rows=decode(args.capture.read_bytes())
    except (OSError,ValueError) as exc:parser.error(str(exc))
    print('id,kind,rc,masked,input_cw,saved_cw,sw,tags,st0..st7 (exponent:significand)')
    for row in rows:
        print(','.join(map(str,row[:4]))+','+','.join(f'{v:04x}' for v in row[4:8])+','+','.join(f'{e:04x}:{s:016x}' for s,e in row[8]))
if __name__=='__main__':main()
