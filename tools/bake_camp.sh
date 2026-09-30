#!/bin/bash
# Lacey Trail campsite (Camping) models -> rom/nitrofs/camp/*. World: bake_world.py camp.
set -e
cd "$(dirname "$0")"
PY=/tmp/venv/bin/python
OUT=../rom/nitrofs/camp; W=/tmp/w/cp; mkdir -p $OUT $W
S="Camping"
lvl() { $PY bake_level.py "$S" $RR $W/$1 ${2:-32} | tail -1; cp $W/$1/level.bin $OUT/$1.bin; }
RR="95 115 40 60 90 115" PIVOT=104.29,50.88,101.53 ONLY="Cultist/" BUDGET=400 lvl cultist
RR="75 85 45 55 93 103" PIVOT=80.05,49.99,98.06 ONLY="Water Pot" BUDGET=120 lvl pot
ls -la $OUT
