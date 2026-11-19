#!/bin/bash
# Deskimon PC Simulator Launch Script

cd "$(dirname "$0")/build"

if [ ! -f "./deskimon_sim" ]; then
    echo "Error: Simulator not built. Run 'cmake .. && make' in the build directory first."
    exit 1
fi

echo "Launching Deskimon PC Simulator..."
./deskimon_sim
