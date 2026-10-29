#!/usr/bin/env python3
"""Configured NPX BIOS/DOS smoke. Uses disposable images; needs testroms and images/dos.img."""
import argparse, hashlib, re, subprocess
from pathlib import Path
from test_at_native_boot import ROOT, prepare_native_image

def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--emulator',type=Path,default=ROOT/'src/arch/ibmpc/pce-ibmpc')
    ap.add_argument('--output',type=Path,default=ROOT/'tests/memory-validation/npx-board-guest')
    ap.add_argument('--machine',choices=('xt','ibm','ami','all'),default='all')
    args=ap.parse_args()
    original=hashlib.sha256((ROOT/'images/dos.img').read_bytes()).hexdigest()
    for machine in (('xt','ibm','ami') if args.machine=='all' else (args.machine,)):
        out=args.output.resolve()/machine;out.mkdir(parents=True,exist_ok=True)
        payload=out/'tnpxboard.com'
        subprocess.run(['nasm','-f','bin',*(['-DAT'] if machine!='xt' else []),'-o',str(payload),'tests/asm/tnpxboard.asm'],cwd=ROOT,check=True)
        image,cfg=prepare_native_image(out,'npx',payload.read_bytes(),bios='ami' if machine=='ami' else 'ibm')
        if machine=='xt':
            text=(ROOT/'tests/test.cfg').read_text().replace('model = "80286"','model = "8088"')
            # Keep the standalone template, append only the disposable image.
            text+='\ndisk { drive=0 type="auto" file="'+str(image)+'" readonly=1 }\n'
            text+='npx { model="8087" }\n'
        else:
            text=cfg.read_text()+'\nnpx { model="80287" }\n'
            if machine=='ami': text=re.sub(r'speed\s*=\s*2\b','speed = 24',text)
        cfg.write_text(text)
        with (out/'guest.log').open('w') as log:
            p=subprocess.run([str(args.emulator.resolve()),'-R','-c',str(cfg),'-t','null'],cwd=ROOT,stdout=log,stderr=subprocess.STDOUT,timeout=90)
        assert p.returncode==0,(machine,p.returncode,out/'guest.log')
        print('PASS',machine,'BIOS equipment / arithmetic / error handler / recovery',flush=True)
    assert hashlib.sha256((ROOT/'images/dos.img').read_bytes()).hexdigest()==original
if __name__=='__main__': main()
