"""Exercise startup, XT equipment switches, and CMOS through the real CLI."""
import os
from pathlib import Path
import struct
import signal
import subprocess
import sys
import tempfile

binary = Path(sys.argv[1]).resolve()
failures = []


def check(name, condition):
    print(("PASS " if condition else "FAIL ") + name)
    if not condition:
        failures.append(name)


def limit_output_file():
    import resource
    signal.signal(signal.SIGXFSZ, signal.SIG_IGN)
    resource.setrlimit(resource.RLIMIT_FSIZE, (32, 32))


with tempfile.TemporaryDirectory() as temp:
    work = Path(temp)
    rom = work / "test.rom"

    def boot(code, model, extra="", limit=None, loads=""):
        image = bytearray(65536)
        image[:len(code)] = code
        image[0xfff0:0xfff5] = bytes.fromhex("ea 00 00 00 f0")
        rom.write_bytes(image)
        cpu = "80286" if model == "5170" else "8088"
        cfg = work / "test.cfg"
        cfg.write_text(f'''system {{ model="{model}" patch_bios_init=0 patch_bios_int19=0 }}
cpu {{ model="{cpu}" }}
ram {{ address=0 size=192K }}
rom {{ address=0xf0000 size=64K }}
load {{ address=0xf0000 file="{rom}" }}
{loads}
terminal {{ driver="null" }}
video {{ device="cga" }}
{extra}
''')
        return subprocess.run([binary, "-R", "-c", cfg], cwd=work,
                              stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                              timeout=10, preexec_fn=limit)

    # Return BL as the guest exit code.
    finish = bytes.fromhex("b7 00 b8 01 01 d5 00 f4")
    def expect_al(value):
        # CMP AL,value / MOV BL,0 / JE done / INC BL. Exit 1 on mismatch.
        return bytes([0x3c, value]) + bytes.fromhex("b3 00 74 02 fe c3") + finish

    success = bytes.fromhex("b3 00") + finish
    missing = work / "missing-bios.rom"
    result = boot(success, "5170", loads=f'load {{ address=0xf0000 file="{missing}" }}')
    check("missing BIOS prevents guest execution and names the file",
          result.returncode == 1 and b"loading failed" in result.stdout
          and os.fsencode(missing) in result.stdout)

    for npx in ("none", "8087"):
        # Select the RAM switches via port B and return port C's low nibble.
        read_ram = bytes.fromhex("b0 04 e6 61 e4 62 24 0f")
        result = boot(read_ram + expect_al(4), "5150", f'npx {{ model="{npx}" }}')
        check(f"5150 192 KiB RAM switches with {npx}", result.returncode == 0)

    nvram = work / "guest.nvr"
    cmos = f'cmos {{ file="{nvram}" sync_rtc_on_start=0 }}'
    # Write a sentinel to CMOS byte 38h then read it on the next launch.
    select = bytes.fromhex("b0 38 e6 70")
    result = boot(select + bytes.fromhex("b0 2a e6 71") + success, "5170", cmos)
    check("CMOS save succeeds", result.returncode == 0 and nvram.is_file() and nvram.stat().st_size == 64)
    result = boot(select + bytes.fromhex("e4 71") + expect_al(42), "5170", cmos)
    check("CMOS survives reopen", result.returncode == 0)

    if os.name == "posix":
        before = nvram.read_bytes()
        result = boot(select + bytes.fromhex("b0 55 e6 71") + success,
                      "5170", cmos, limit_output_file)
        check("failed CMOS write preserves previous bytes", nvram.read_bytes() == before)
        check("failed CMOS write diagnosed", os.fsencode(nvram) in result.stdout
              and b"save CMOS" in result.stdout)

        # BH tells the guest which WD1003 completion status to expect.
        program = work / "write.bin"
        source = Path(__file__).resolve().parent / "asm" / "twdflush.asm"
        subprocess.run(["nasm", "-f", "bin", "-o", program, source], check=True)
        disk = work / "raw.img"
        headers = {
            "image": b"",
            "dosemu": (b"DOSEMU\0" + struct.pack("<4I", 1, 2, 1, 128)).ljust(128, b"\0"),
            "pce": struct.pack(">8I", 0x50494d47, 0, 512, 2, 1, 1, 2, 512).ljust(512, b"\0"),
        }
        for fmt, header in headers.items():
            storage = f'disk {{ drive=128 type="{fmt}" file="{disk}" c=1 h=1 s=2 }}\nwd1003 {{ drive0=128 }}'
            for expected, limit in ((0, None), (1, limit_output_file)):
                disk.write_bytes(header + bytes(1024))
                result = boot(bytes([0xb7, expected]) + program.read_bytes(), "5170", storage, limit)
                data = header + bytes(512) + (bytes(512) if expected else bytes([0x55]) * 512)
                check(f"{fmt} WD1003 write reports ERR={expected} and image agrees",
                      result.returncode == 0 and disk.read_bytes() == data)

sys.exit(bool(failures))
