#!/bin/bash
# Bakes the RV (drive scene) the player sits in. Only the cab is ever seen from the driver's seat, so the
# back rooms are skipped and the budget goes to the wheel, dashboard and seats.
set -e
cd "$(dirname "$0")"
ONLY="RV/" SKIP="Headlight,Beam,Volumetric,Rv Bounds,Collider,RV Windows,Door Window,Bedroom,Shower,Bath,Closet,Toilet,Bed_,Wheel Front,Wheel Rear,Axis,sparewheel,Lamp_wall,Oven,Pillow,KP_,Beer,Book,Cover,Curtain,Globe,TV,Poster,Fridge,Cutlery,Dish,Cloth,Bag,Painting,cupboard,Ceiling,Kitchen,Overhead storage back,Needle,Doorknob,Mirror Glass,Bench,Table,Newspaper,Papers,Pedals,Cube.0,Arm rest,wiper 1 mount,wiper 2 mount" \
PRI="Steering,Seat,Dashboard,Wiper,Mirror Sunshade,Front seat,RV Interior,Overhead Storage Front" \
MINTRI="SteeringWheel:360,Dashboard:500,Steering mount:60,RV Seat:160,Front seat:160,Mirror Sunshade:60,Wiper:40" \
PIVOT=608.8,-0.04,274.4 SCALE=2 VISEYE=608.23,2.01,277.39 BUDGET=2600 RELIEF=1.3 \
    PYTHONPATH=. /tmp/venv/bin/python bake_level.py "First Scene" 600 618 -2 6 264 286 /tmp/w/rv 128
cp /tmp/w/rv/level.bin ../rom/nitrofs/rv.bin
