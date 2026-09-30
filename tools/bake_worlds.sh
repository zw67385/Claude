#!/bin/bash
# Bakes the chunked outdoor worlds: the RV drive, the trailhead and the campsite.
set -e
cd "$(dirname "$0")"
N64=30 /tmp/venv/bin/python bake_world.py drive1 ../rom/nitrofs/drive1.wld
/tmp/venv/bin/python bake_world.py trail ../rom/nitrofs/trail.wld
/tmp/venv/bin/python bake_world.py camp ../rom/nitrofs/camp.wld
