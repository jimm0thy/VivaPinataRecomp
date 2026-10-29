#!/usr/bin/env bash
# Starts the game from the build tree. Extra arguments go to the game,
# e.g. --vp_show_launcher=false to skip the launcher.
LINUX_DIR="$(cd "$(dirname "$(readlink -f "${BASH_SOURCE[0]}")")" && pwd)"
BUILD_DIR="$(dirname "$LINUX_DIR")/out/build/local-lin-relwithdebinfo"
if [[ ! -x "$BUILD_DIR/vivapinata" ]]; then
  echo "Not built yet: run $LINUX_DIR/build-linux.sh first." >&2
  exit 1
fi
exec "$BUILD_DIR/vivapinata" "$@"
