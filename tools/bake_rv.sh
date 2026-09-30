#!/bin/bash
# Bakes the RV (drive scene) the player sits in.
set -e
cd "$(dirname "$0")"
ONLY="RV/" SKIP="Headlight,Beam,Volumetric,Rv Bounds,Collider,RV Windows,Door Window" PIVOT=608.8,-0.04,274.4 SCALE=2 BUDGET=1400 \
    PYTHONPATH=. /tmp/venv/bin/python bake_level.py "First Scene" 600 618 -2 6 264 286 /tmp/w/rv 32
cp /tmp/w/rv/level.bin ../rom/nitrofs/rv.bin
