"""Bounded guest-test execution with persistent diagnostics."""
import os
from pathlib import Path
import signal
import subprocess


def run_logged(command, log, timeout, cwd):
    log = Path(log)
    log.parent.mkdir(parents=True, exist_ok=True)
    print(f'  RUN    {" ".join(map(str, command))}', flush=True)
    with log.open('w') as stream:
        process = subprocess.Popen(list(map(str, command)), cwd=cwd,
                                   stdin=subprocess.DEVNULL, stdout=stream,
                                   stderr=subprocess.STDOUT,
                                   start_new_session=os.name == 'posix')
        try:
            code = process.wait(timeout=timeout)
        except subprocess.TimeoutExpired:
            print(f'  TIMEOUT after {timeout:g}s', flush=True)
            code = 124
        finally:
            if process.poll() is None:
                if os.name == 'posix':
                    os.killpg(process.pid, signal.SIGKILL)
                else:
                    process.kill()
                process.wait()
    if code:
        print(f'  FAIL   exit={code}; log: {log.resolve()}', flush=True)
        print('\n'.join(log.read_text(errors='replace').splitlines()[-40:]), flush=True)
    else:
        print(f'  PASS   log: {log.resolve()}', flush=True)
    return code
