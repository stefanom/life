#!/bin/bash
# Run Conway's Game of Life simulation
#
# This script builds the binary (if needed) and runs it with the provided arguments.
# Run with --help to see all available options.

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BINARY="$SCRIPT_DIR/game_of_life"

# Build using make (handles dependency checking)
make -C "$SCRIPT_DIR" game_of_life --quiet

# Pass all arguments to the binary
exec "$BINARY" "$@"
