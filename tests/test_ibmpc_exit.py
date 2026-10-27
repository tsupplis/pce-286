#!/usr/bin/env python3
"""Check guest abort results through the real CLI and machine cleanup path."""
import pathlib
import subprocess
import sys
import tempfile

root = pathlib.Path(__file__).resolve().parent.parent
binary = pathlib.Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else root / "src/arch/ibmpc/pce-ibmpc"

with tempfile.TemporaryDirectory(prefix="exit-test-", dir=root / "tests") as tmp:
    tmp = pathlib.Path(tmp)
    for cpu in ("8088", "80286"):
        for result in (0, 1):
            rom = bytearray([0xff]) * 65536
            # MOV AX,0101h; MOV BX,result; PCE hook; HLT.
            rom[:9] = bytes([0xb8, 1, 1, 0xbb, result, 0, 0xd5, 0, 0xf4])
            # Both legacy and 286 reset fetches enter the low BIOS by far jump.
            rom[0xfff0:0xfff5] = bytes([0xea, 0, 0, 0, 0xf0])
            image = tmp / "exit.rom"
            image.write_bytes(rom)
            cfg = tmp / "exit.cfg"
            cfg.write_text(f"""system {{
 model = "5160"
 patch_bios_init = 0
 patch_bios_int19 = 0
}}
cpu {{ model = "{cpu}" }}
ram {{ address = 0 size = 640K }}
rom {{ address = 0xf0000 size = 64K }}
load {{ format = "binary" address = 0xf0000 file = "{image}" }}
terminal {{ driver = "null" }}
video {{ device = "cga" }}
""")
            run = subprocess.run([str(binary), "-R", "-c", str(cfg)], cwd=root,
                                 stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                 timeout=10)
            if run.returncode != result:
                sys.stdout.buffer.write(run.stdout)
                raise SystemExit(f"FAIL {cpu}: expected exit {result}, got {run.returncode}")
            print(f"PASS {cpu}: guest result {result} survives cleanup")
