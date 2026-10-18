#!/bin/sh
# Packages the built game as an installable .cia.
# Usage: cia/build_cia.sh <makerom> <bannertool>   (run from the repo root after `make`)
set -e
MAKEROM=${1:-makerom}
BANNERTOOL=${2:-bannertool}

"$BANNERTOOL" makebanner -i cia/banner.png -a cia/audio.wav -o build/banner.bnr
"$MAKEROM" -f cia -o pixelfarm.cia -target t -exefslogo \
    -elf pixelfarm.elf -rsf cia/app.rsf \
    -icon pixelfarm.smdh -banner build/banner.bnr
ls -l pixelfarm.cia
