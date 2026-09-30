#!/bin/bash
# Bakes the watchtower cabin (tower.bin) and the surroundings seen from the deck (outside.bin).
set -e
cd "$(dirname "$0")"
PY=/tmp/venv/bin/python
# the cabin box and the outside's exclusion box are the same, so nothing (the deck) is baked twice
PRI="Fire_Meter,RADIO,Radio,Computer,Monitor,Keyboard,Stove,IRONBARK,Chair,SinkONLY,drawer,door,Lamp,Fridge,Bed,bed,Table"
SKIP="Tower/Door" PRI="$PRI" PYTHONPATH=. BUDGET=2600 $PY bake_level.py Watchtower 311.4 318.6 43.5 47 323.4 330.6 /tmp/bake1 64
cp /tmp/bake1/level.bin ../rom/nitrofs/tower.bin
OBB=1.5 SIZEW=1 SCALE=4 BUDGET=1600 TERRAIN=3 EXCL=311.4,318.6,43.5,47,323.4,330.6 PYTHONPATH=. \
    $PY bake_level.py Watchtower 290 335 20 52 290 345 /tmp/bake_out 64
cp /tmp/bake_out/level.bin ../rom/nitrofs/outside.bin
ONLY=Tower/Door PIVOT=311.972,44.888,325.419 BUDGET=300 PYTHONPATH=. \
    $PY bake_level.py Watchtower 305 320 40 50 320 332 /tmp/bake_door 64
cp /tmp/bake_door/level.bin ../rom/nitrofs/door.bin
