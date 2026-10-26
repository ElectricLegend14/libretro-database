#!/usr/bin/env bash
# Build the FE3H co-op exlaunch module.
#
#   ./switch/build.sh            build into switch/exlaunch/deploy/
#   RYU_PATH=... ./switch/build.sh deploy-ryu   build and copy into Ryujinx's mods folder
#
# Needs devkitPro with devkitA64 and libnx installed and DEVKITPRO set:
# https://devkitpro.org/wiki/Getting_Started
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(dirname "$HERE")"
EXL="$HERE/exlaunch"

# Fire Emblem: Three Houses (check your emulator's game list if yours differs).
PROGRAM_ID="${PROGRAM_ID:-010055D009F78000}"
# exlaunch revision this mod was written against.
EXL_COMMIT="f9f4b0dd07b68f97958cb9c79228bbca22ca80d5"

if [ -z "${DEVKITPRO:-}" ]; then
    echo "DEVKITPRO isn't set. Install devkitPro (devkitA64 + libnx) first:" >&2
    echo "  https://devkitpro.org/wiki/Getting_Started" >&2
    exit 1
fi

if [ ! -d "$EXL/.git" ]; then
    git clone https://github.com/shadowninja108/exlaunch "$EXL"
fi
git -C "$EXL" checkout -q "$EXL_COMMIT" 2>/dev/null || {
    git -C "$EXL" fetch -q origin
    git -C "$EXL" checkout -q "$EXL_COMMIT"
}

# Our code goes in as two exlaunch "modules": the core and the game glue.
rm -rf "$EXL/source/coop"
mkdir -p "$EXL/source/coop"
cp "$ROOT"/include/coop/*.hpp "$ROOT"/src/*.cpp "$EXL/source/coop/"
cp "$HERE/main.cpp" "$EXL/source/program/main.cpp"

# NPDM for the game: exlaunch's application template with FE3H's title id
# and SD card access so coop.ini can be read.
python3 - "$EXL/misc/npdm-json/application.json" "$EXL/misc/npdm-json/fe3h-coop.json" "$PROGRAM_ID" <<'EOF'
import json, sys
src, dst, tid = sys.argv[1:]
npdm = json.load(open(src))
npdm["name"] = "fe3h-coop"
npdm["title_id"] = "0x" + tid.lower()
npdm["filesystem_access"]["permissions"] = "0xffffffffffffffff"
json.dump(npdm, open(dst, "w"), indent=4)
EOF

sed -i \
    -e "s|^LOAD_KIND :=.*|LOAD_KIND := Module|" \
    -e "s|^PROGRAM_ID :=.*|PROGRAM_ID := $PROGRAM_ID|" \
    -e "s|^NPDM_JSON :=.*|NPDM_JSON := fe3h-coop.json|" \
    "$EXL/config.mk"
if [ -n "${RYU_PATH:-}" ]; then
    sed -i "s|^RYU_PATH :=.*|RYU_PATH := $RYU_PATH|" "$EXL/config.mk"
fi

cd "$EXL"
./exlaunch.sh build
if [ "${1:-}" = "deploy-ryu" ]; then
    ./exlaunch.sh deploy-ryu
fi

echo
echo "Built: $EXL/deploy/subsdk9 and $EXL/deploy/main.npdm"
echo "Copy both into atmosphere/contents/$PROGRAM_ID/exefs/ (Switch)"
echo "or <Ryujinx>/mods/contents/$PROGRAM_ID/fe3h-coop/exefs/ (emulator)."
