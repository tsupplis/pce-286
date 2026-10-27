#!/bin/sh
# tests/run_test.sh
#
# Run a single COM test program inside the PCE IBM PC emulator using the
# pre-built DOS image as a boot floppy.
#
# Usage:
#   tests/run_test.sh [OPTIONS] <test.com>
#
# Options:
#   -p <cpu_model>   CPU model to pass to pce-ibmpc (default: 8088)
#   -t <timeout>     Timeout in seconds (default: 30)
#   -e <emulator>    Path to pce-ibmpc binary (default: src/arch/ibmpc/pce-ibmpc)
#   -b <base_img>    Base floppy image (default: images/dos.img)
#   -c <config>      Config file (default: tests/test.cfg)
#   -v               Verbose: show emulator output
#
# Exit codes:
#   0  Test passed (emulator exited 0)
#   1  Test failed (emulator exited non-zero)
#   2  Setup error (missing files, tools, etc.)
#   3  Timeout
#
# Requirements:
#   - mtools (mcopy) on PATH
#   - pce-ibmpc binary already built

set -e

# ---- defaults --------------------------------------------------------
CPU_MODEL="8088"
TIMEOUT_SECS=30
EMU="src/arch/ibmpc/pce-ibmpc"
BASE_IMG="images/dos.img"
TEST_IMG_DIR="images"
CFG="tests/test.cfg"
VERBOSE=0
PCEEXIT="src/arch/ibmpc/pceutils/pceexit.com"

# ---- parse options ---------------------------------------------------
while getopts "p:t:e:b:c:v" opt; do
	case "$opt" in
		p) CPU_MODEL="$OPTARG" ;;
		t) TIMEOUT_SECS="$OPTARG" ;;
		e) EMU="$OPTARG" ;;
		b) BASE_IMG="$OPTARG" ;;
		c) CFG="$OPTARG" ;;
		v) VERBOSE=1 ;;
		*) echo "Usage: $0 [-p cpu] [-t secs] [-e emu] [-b img] [-c cfg] [-v] <test.com>" >&2
		   exit 2 ;;
	esac
done
shift $((OPTIND - 1))

TEST_COM="$1"
if [ -z "$TEST_COM" ]; then
	echo "Error: no test .com file specified" >&2
	exit 2
fi

# ---- locate mtools ---------------------------------------------------
MCOPY=$(command -v mcopy 2>/dev/null)
if [ -z "$MCOPY" ]; then
	echo "Error: mcopy not found on PATH (install mtools)" >&2
	exit 2
fi

# ---- sanity checks ---------------------------------------------------
for f in "$EMU" "$BASE_IMG" "$CFG" "$TEST_COM" "$PCEEXIT"; do
	if [ ! -f "$f" ]; then
		echo "Error: required file not found: $f" >&2
		exit 2
	fi
done

# ---- create temp working area ----------------------------------------
TMPDIR_RUN=$(mktemp -d "${TEST_IMG_DIR}/run_XXXXXX")
trap 'rm -rf "$TMPDIR_RUN"' EXIT

TMPIMG="$TMPDIR_RUN/boot.img"
TMPBAT="$TMPDIR_RUN/AUTOEXEC.BAT"

# ---- copy base image -------------------------------------------------
cp "$BASE_IMG" "$TMPIMG"
chmod 644 "$TMPIMG"

# ---- build AUTOEXEC.BAT from template --------------------------------
# The test program name (uppercase, 8.3) is extracted from the path.
TEST_NAME=$(basename "$TEST_COM" | tr 'a-z' 'A-Z')
sed "s/%%TESTPROG%%/$TEST_NAME/" tests/autoexec-template.bat > "$TMPBAT"
# Normalise line endings to DOS CR+LF
sed -i '' 's/$/\r/' "$TMPBAT" 2>/dev/null || sed -i 's/$/\r/' "$TMPBAT"

# ---- inject files into the floppy image ------------------------------
# Use MTOOLS_NO_VFAT=1 to force 8.3 names; -D o to overwrite existing files
export MTOOLS_NO_VFAT=1

"$MCOPY" -D o -i "$TMPIMG" "$TEST_COM"   ::/"$TEST_NAME"
"$MCOPY" -D o -i "$TMPIMG" "$PCEEXIT"    ::/PCEEXIT.COM
"$MCOPY" -D o -i "$TMPIMG" "$TMPBAT"     ::/AUTOEXEC.BAT

# ---- run the emulator ------------------------------------------------
LOGFILE="$TMPDIR_RUN/emu.log"

if [ "$VERBOSE" -eq 1 ]; then
	LOGFILE="/dev/stderr"
fi

# The emulator receives the disk image via ini-append (-I) overrides so that
# test.cfg itself doesn't need to name a specific file.
# Each -I value is one INI line; the parser uses newlines as separators so
# we must NOT embed semicolons.  The disk section is spread across 5 -I flags.

EMU_CMD="$EMU -R -c $CFG -p $CPU_MODEL -I 'disk {' -I 'drive = 0' -I 'type = \"auto\"' -I 'file = \"$TMPIMG\"' -I '}'"
echo "  CMD    $EMU_CMD" >&2

EMU_EXIT=0
if command -v timeout >/dev/null 2>&1; then
	timeout "$TIMEOUT_SECS" "$EMU" -R \
		-c "$CFG" \
		-p "$CPU_MODEL" \
		-I "disk {" \
		-I "drive = 0" \
		-I "type = \"auto\"" \
		-I "file = \"$TMPIMG\"" \
		-I "}" \
		>"$LOGFILE" 2>&1 || EMU_EXIT=$?
else
	# macOS: gtimeout from coreutils, or fallback to background process + killer
	"$EMU" -R \
		-c "$CFG" \
		-p "$CPU_MODEL" \
		-I "disk {" \
		-I "drive = 0" \
		-I "type = \"auto\"" \
		-I "file = \"$TMPIMG\"" \
		-I "}" \
		>"$LOGFILE" 2>&1 &
	EMU_PID=$!
	(sleep "$TIMEOUT_SECS" && kill -TERM "$EMU_PID" 2>/dev/null) &
	KILLER_PID=$!
	wait "$EMU_PID" 2>/dev/null && EMU_EXIT=0 || EMU_EXIT=$?
	kill "$KILLER_PID" 2>/dev/null || true
fi

# timeout(1) exits 124 on timeout; SIGTERM produces 143 in bash
if [ "$EMU_EXIT" -eq 124 ] || [ "$EMU_EXIT" -eq 143 ]; then
	echo "TIMEOUT  $TEST_COM  (cpu=$CPU_MODEL, ${TIMEOUT_SECS}s)" >&2
	[ "$VERBOSE" -eq 0 ] && cat "$LOGFILE" >&2
	exit 3
fi

# ---- report result ---------------------------------------------------
if [ "$EMU_EXIT" -eq 0 ]; then
	printf "PASS  %-30s  cpu=%-8s\n" "$TEST_COM" "$CPU_MODEL"
	exit 0
else
	printf "FAIL  %-30s  cpu=%-8s  exit=%d\n" "$TEST_COM" "$CPU_MODEL" "$EMU_EXIT" >&2
	[ "$VERBOSE" -eq 0 ] && cat "$LOGFILE" >&2
	exit 1
fi
