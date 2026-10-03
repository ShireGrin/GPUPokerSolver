#!/bin/bash
# Automatically detect the directory where this script is located
DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" >/dev/null 2>&1 && pwd)"

# Add standard ROCm installation path to library search
export LD_LIBRARY_PATH=/opt/rocm/lib:$LD_LIBRARY_PATH
export HSA_ENABLE_SDMA=0
export AMD_LOG_LEVEL=0

# Automatically load database credentials from .env if it exists
if [ -f "$DIR/.env" ]; then
    set -a
    source "$DIR/.env"
    set +a
fi

# Run the GUI relative to the script's location
"$DIR/build/GPUPokerSolver" "$@"
