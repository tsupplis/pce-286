#!/bin/sh
# tests/build_images.sh
#
# Build the four test boot-floppy images in images/.
# Each image is a copy of images/dos.img with:
#   - the test .com file
#   - pceexit.com
#   - a custom AUTOEXEC.BAT that runs the test then exits
#
# Requirements: mtools (mcopy, mdir) on PATH.
# Run from the repo root:   sh tests/build_images.sh

set -e

# ---- locate tools ----------------------------------------------------
MCOPY=$(command -v mcopy 2>/dev/null)
MDIR=$(command -v  mdir  2>/dev/null)

if [ -z "$MCOPY" ] || [ -z "$MDIR" ]; then
    echo "Error: mtools (mcopy/mdir) not found on PATH" >&2
    exit 1
fi

BASE=images/dos.img
PCEEXIT=src/arch/ibmpc/pceutils/pceexit.com
NASM=$(command -v nasm 2>/dev/null)

for f in "$BASE" "$PCEEXIT"; do
    [ -f "$f" ] || { echo "Error: $f not found"; exit 1; }
done

export MTOOLS_NO_VFAT=1

# ---- rebuild .com files from source ----------------------------------
if [ -n "$NASM" ]; then
    for asm in tests/asm/t186.asm tests/asm/t286.asm \
               tests/asm/tv20.asm tests/asm/ta20.asm \
               tests/asm/tprot.asm tests/asm/textmem.asm \
               tests/asm/tloadall.asm; do
        com="${asm%.asm}.com"
        if [ "$asm" -nt "$com" ] || [ ! -f "$com" ]; then
            echo "  NASM   $com"
            "$NASM" -O6 -Isrc/ -Isrc/arch/ibmpc/ -f bin "$asm" -o "$com"
        fi
    done
else
    echo "Warning: nasm not on PATH — using existing .com files" >&2
fi

# ---- build one image -------------------------------------------------
build_img() {
    local fatname="$1"   # 8.3 DOS filename on the FAT image, e.g. TEST_186.COM
    local com="$2"       # host path to the .com file
    local out="$3"       # output image path

    [ -f "$com" ] || { echo "Error: $com not found (build .com first)"; exit 1; }

    rm -f "$out"
    cp "$BASE" "$out"
    chmod 644 "$out"

    # AUTOEXEC.BAT with DOS CR+LF line endings
    printf '@ECHO OFF\r\nPATH A:\\DOS;A:\\\r\n%s\r\nPCEEXIT.COM\r\n' \
        "$fatname" > /tmp/_pce_autoexec.bat

    "$MCOPY" -D o -i "$out" "$com"                 ::/"$fatname"
    "$MCOPY" -D o -i "$out" "$PCEEXIT"             ::/PCEEXIT.COM
    "$MCOPY" -D o -i "$out" /tmp/_pce_autoexec.bat ::/AUTOEXEC.BAT

    printf "Built %-35s  " "$out"
    "$MDIR" -i "$out" :: 2>&1 | awk '/bytes free/{print $1,$2,$3}' \
        || echo "(verify with: mdir -i $out ::)"
}

# ---- build all images ------------------------------------------------
build_img T186.COM     tests/asm/t186.com     images/t186.img
build_img T286.COM     tests/asm/t286.com     images/t286.img
build_img TV20.COM     tests/asm/tv20.com     images/tv20.img
build_img TA20.COM     tests/asm/ta20.com     images/ta20.img
build_img TPROT.COM    tests/asm/tprot.com    images/tprot.img
build_img TEXTMEM.COM  tests/asm/textmem.com  images/textmem.img
build_img TLOADALL.COM tests/asm/tloadall.com images/tloadall.img
rm -f /tmp/_pce_autoexec.bat

echo ""
echo "=== Image inventory ==="
ls -lh images/*.img

echo ""
echo "=== Directory listings ==="
for img in images/t186.img images/t286.img \
           images/tv20.img images/ta20.img \
           images/tprot.img images/textmem.img \
           images/tloadall.img; do
    echo "--- $img ---"
    "$MDIR" -i "$img" :: 2>&1 | grep -E "COM|BAT|free"
done
