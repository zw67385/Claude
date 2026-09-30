#!/bin/bash
# Roseburg diner + parking lot (First Scene) -> rom/nitrofs/diner/*. Diner frame: PIVOT 1115,0,955 (unity).
set -e
cd "$(dirname "$0")"
PY=/tmp/venv/bin/python
OUT=../rom/nitrofs/diner; W=/tmp/w/dn; mkdir -p $OUT $W
export FORCE="Roseburg (DISABLE ON BUILD),Batch1,Batch2,Batch3,Batch4"
export TERRAIN_ASSET="TerrainData/New Terrain 1.asset" TERRAIN_OFS=0,0,0
R=(1095 1135 -1 9 930 975)
NPC="Diner/Waitress,Diner/Trucker,Diner/SuitGuy,Diner/Chef,Diner/BadGuy,Bad Guy OLD,ParkingLotGuy"
BASE="UI Manager,OLD/,Traffic Manager,RV/,Follower"
lvl() { $PY bake_level.py "First Scene" 1095 1135 -1 9 930 975 $W/$1 ${2:-32} | tail -1; cp $W/$1/level.bin $OUT/$1.bin; }
# static level (no NPCs, no food)
PIVOT=1115,0,955 SKIP="$BASE,$NPC,Food Tray,DoorInner_A" BUDGET=3600 lvl diner 64
PIVOT=1118.98,0,950.99 ONLY="DoorInner_A" SKIP="$BASE" BUDGET=60 lvl wdoor
# walking data
PIVOT=1115,0,955 SKIP="$BASE,$NPC,DoorInner_A,Bar Doors" $PY bake_walls.py "First Scene" "${R[@]}" $OUT/diner.wal | tail -1
PIVOT=1115,0,955 SKIP="$BASE,$NPC" $PY bake_floor.py "First Scene" "${R[@]}" $OUT/diner.flr | tail -1
PIVOT=1115,0,955 $PY bake_collide.py "First Scene" "${R[@]}" $OUT/diner.col | tail -1
# NPCs in place (diner frame)
PIVOT=1115,0,955 POSE=sit:0.75:0.45 ONLY="Diner/Trucker1" BUDGET=300 lvl trucker1
PIVOT=1115,0,955 POSE=sit:0.5:0.45 ONLY="Diner/Trucker2" BUDGET=300 lvl trucker2
PIVOT=1115,0,955 POSE=sit:0.75:0.45 ONLY="Diner/SuitGuy" BUDGET=300 lvl suitguy
PIVOT=1115,0,955 POSE=stand:0:0.45 ONLY="Diner/Chef" BUDGET=300 lvl chef
PIVOT=1115,0,955 POSE=sit:0.5:0.45 ONLY="Diner/BadGuy/" BUDGET=300 lvl badguy_sit
# movable NPCs, pivot at their feet (drawn at a position + yaw)
PIVOT=1117.0,0.45,958.7 POSE=stand SKIP="Food Tray,Old Tray,Tray (3)" ONLY="Diner/Waitress" BUDGET=350 lvl waitress
PIVOT=1114.9,0.45,955.6 POSE=stand:0:0.45 ONLY="Diner/BadGuy/" BUDGET=300 lvl badguy
PIVOT=1107.2,0.27,939.3 POSE=stand ONLY="ParkingLotGuy" BUDGET=300 lvl plg
PIVOT=1115.11,1.24,962.08 ONLY="Diner/Food Tray/" SKIP="Food Tray (1)" BUDGET=200 lvl food
ls -la $OUT
