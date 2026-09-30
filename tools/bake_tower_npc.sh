#!/bin/bash
# Watchtower NPC / prop models (Seq 5 deer skull, Seq 6 bad guy, Seq 8 cult camp) -> rom/nitrofs/tower_npc/*.
# The Seq 5 / Seq 8 cultist reuses camp/cultist.bin and ranger Billy reuses trail/ranger.bin.
set -e
cd "$(dirname "$0")"
PY=/tmp/venv/bin/python
OUT=../rom/nitrofs/tower_npc; W=/tmp/w/wt; mkdir -p $OUT $W
lvl() { $PY bake_level.py Watchtower $R $W/$1 32 | tail -1; cp $W/$1/level.bin $OUT/$1.bin; }
R="305 315 40 48 320 330" FORCE="Seq 5,DeerSkull" PIVOT=315,45.25,327 ONLY="Seq 5/DeerSkull" BUDGET=150 lvl skull
R="300 312 20 35 300 311" FORCE="Seq 6,BadGuy_Worker,Casual 11" PIVOT=305.97,27.09,305.51 POSE=stand ONLY="Seq 6/BadGuy_Worker" BUDGET=300 lvl badguy
R="300 345 5 30 440 475" FORCE="Seq 8,Cult Camp,Campfire,DeadMan,Tent,Candles" PIVOT=320,13,458 ONLY="Seq 8/Cult Camp" BUDGET=500 lvl camp
ls -la $OUT
