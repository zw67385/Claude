#!/bin/bash
# Ironbark trailhead guard house (Trail Start) -> rom/nitrofs/trail/*. Walk data in the world frame (PIVOT 0),
# models baked around their own pivots and drawn at world positions (DS x = ux, z = -uz).
set -e
cd "$(dirname "$0")"
PY=/tmp/venv/bin/python
OUT=../rom/nitrofs/trail; W=/tmp/w/tr; mkdir -p $OUT $W
S="Trail Start"
export TERRAIN_ASSET="TerrainData/New Terrain 2.asset" TERRAIN_OFS=179.5,0,25.1
R=(615 650 -2 8 415 460)
MARK="Door Trigger,Player Outside,Player Inside,Conversation Trigger,Invisible wall,Enter Trigger,Colliders,GuardHouse/GameObject"
lvl() { $PY bake_level.py "$S" ${RR:-${R[@]}} $W/$1 ${2:-32} | tail -1; cp $W/$1/level.bin $OUT/$1.bin; }
# guard house + gate posts (static)
PIVOT=633,0,440 ONLY="GuardHouse/,TRAIL GATE/entry gate_Baked.001,TRAIL GATE/entry gate_Baked.002" \
  SKIP="$MARK,ParkRanger,BadGuy,GuardHouse/Door$" BUDGET=1400 lvl guard 64
PIVOT=634.13,0,440.29 ONLY="GuardHouse/Door$" BUDGET=60 lvl gdoor
PIVOT=637.51,0,433.68 ONLY="TRAIL GATE/entry gate_Baked$" BUDGET=200 lvl gate
# walking data (world frame)
WCLEAR="633.1,634.05,440.0,441.0;637.0,638.1,434.0,439.3" PIVOT=0,0,0 ONLY="GuardHouse/,TRAIL GATE/entry gate_Baked.00" SKIP="$MARK,ParkRanger,BadGuy,GuardHouse/Door$" \
  $PY bake_walls.py "$S" "${R[@]}" $OUT/trail.wal | tail -1
PIVOT=0,0,0 SKIP="$MARK,ParkRanger,BadGuy,RV/,UI Manager,FirstPersonController" $PY bake_floor.py "$S" "${R[@]}" $OUT/trail.flr | tail -1
# characters, pivot at the feet
PIVOT=635.28,0.35,441.40 POSE=stand:0:0.35 ONLY="GuardHouse/ParkRanger/" BUDGET=350 lvl ranger
PIVOT=635.28,0.35,441.40 POSE=sit:0.45:0.35 ONLY="GuardHouse/ParkRanger/" BUDGET=350 lvl ranger_sit
FORCE="BadGuy,GuardHouse" PIVOT=633.63,0,453.16 POSE=stand:0:0 ONLY="GuardHouse/BadGuy/" BUDGET=300 lvl bagguy
RR="580 592 -1 5 150 160" PIVOT=586.15,1.3,154.6 ONLY="RV Interior/Bag" BUDGET=150 lvl bag
ls -la $OUT
