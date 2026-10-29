#!/usr/bin/env python3
"""Fast CPU-family regression gate; all native audit cases are mandatory."""
import argparse
import datetime
import json
from pathlib import Path
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[1]
GROUPS = {
    "NPX foundation and CPU attachment": ["test_npx_format", "test_npx_state", "test_npx_cpu", "test_npx_arithmetic", "test_npx_execute", "test_npx_memory", "test_npx_convert", "test_npx_trans", "test_npx_board"],
    "8080 + shared Z80 regression": ["test_8080_audit"],
    "Shared repeat path: all seven models": ["test_repeat_compliance"],
    "8086/8088 + 80186/80188": ["test_x86_compliance", "test_186ext"],
    "80286": ["test_disasm_286", "test_286_msw", "test_286_addr",
              "test_286_flags", "test_286_pm_fields", "test_286_prot",
              "test_286_idt", "test_286_sysops", "test_286_exec",
              "test_286_tss", "test_286_extmem", "test_loadall"],
    "V20/V30 repaired behavior": ["test_v20", "test_v20ext", "test_nec_native", "test_nec_8080"],
}
# No native-audit exemptions remain. Crashes, timeouts and all FAIL lines
# are regressions, even when the subprocess incorrectly returns success.

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--compare", type=Path, help="previous results.json")
    args = parser.parse_args()
    out = args.output or ROOT / "tests/memory-validation/cpu-regression" / datetime.datetime.now().strftime("%Y%m%d-%H%M%S-%f")
    out.mkdir(parents=True, exist_ok=True)
    results = []
    started = time.monotonic()
    def run(name, group, audit=False):
        t = time.monotonic()
        try:
            p = subprocess.run([str(ROOT / "tests" / name)], cwd=ROOT,
                               capture_output=True, text=True, timeout=30)
            log, rc = p.stdout + p.stderr, p.returncode
        except (OSError, subprocess.TimeoutExpired) as exc:
            log, rc = str(exc), -1
        (out / (name + ".log")).write_text(log)
        failed = [line for line in log.splitlines() if line.startswith("FAIL ")]
        unexpected = rc != 0 or bool(failed)
        state = "REGRESSION" if unexpected else "PASS"
        record = dict(test=name, group=group, state=state, returncode=rc,
                      seconds=round(time.monotonic()-t, 3), failures=failed)
        results.append(record)
        print(f"{state:12} {name} ({record['seconds']:.3f}s)", flush=True)
    for group, tests in GROUPS.items():
        print(group, flush=True)
        for test in tests:
            run(test, group)
    run("test_v20_native_audit", "V20/V30 open-gap audit", audit=True)
    report = dict(results=results, seconds=round(time.monotonic()-started, 3))
    if args.compare:
        previous = json.loads(args.compare.read_text())["results"]
        old = {r["test"]: set(r["failures"]) for r in previous}
        report["changes"] = {r["test"]: {
            "new_failures": sorted(set(r["failures"])-old.get(r["test"], set())),
            "resolved": sorted(old.get(r["test"], set())-set(r["failures"]))}
            for r in results if set(r["failures"]) != old.get(r["test"], set())}
        print("Changes:", json.dumps(report["changes"], indent=2))
    (out / "results.json").write_text(json.dumps(report, indent=2)+"\n")
    bad = any(r["state"] == "REGRESSION" for r in results)
    print(f"{'FAIL' if bad else 'REGRESSION GATE PASS'} — tested scope only; not exhaustive hardware conformance; {report['seconds']:.3f}s")
    print(f"Report: {out / 'results.json'}")
    return int(bad)

if __name__ == "__main__":
    sys.exit(main())
