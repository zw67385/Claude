#!/bin/bash
# Release ROM for the R4i-SDHC card (R4iMenu kernel): pre-patch the card's own DLDI driver ("DEMON IO",
# shipped as moonshl2/dldibody.bin in the card files) so SD access doesn't depend on the menu's patcher.
set -e
OUT=${1:-IronbarkLookout.nds}
cp "$(dirname "$0")/../rom/rom.nds" "$OUT"
cp /tmp/ib/R4CardFilesStuff/moonshl2/dldibody.bin /tmp/r4i_demon.dldi
/opt/wonderful/thirdparty/blocksds/core/tools/dlditool/dlditool /tmp/r4i_demon.dldi "$OUT"
