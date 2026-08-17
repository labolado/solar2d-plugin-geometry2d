#!/bin/bash

cd $(dirname $0)
current_dir=$(pwd)

cp -f data.tgz $HOME/Documents/dev/app/github/local_server_4_solar2d_plugins/plugins/mac-sim/plugin.geometry2d/2025.3720-mac-sim.tgz

cd ../../examples/solar2d
"/Applications/Corona/Corona Simulator.app/Contents/MacOS/Corona Simulator" -no-console YES "main.lua"
