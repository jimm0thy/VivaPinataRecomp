#!/usr/bin/env bash
# Makes a portable copy of the built game: the executable, its libraries,
# settings, launcher art, the game files and your saves/shader cache, all in
# one folder. Nothing outside that folder is needed to play.
#
#   linux/package-linux.sh                     create or update VivaPinata/ next to the repository
#   linux/package-linux.sh ~/Games/VivaPinata  create or update the portable folder there
#
# Running it again after a rebuild refreshes the executable and libraries; the
# settings, saves and shader cache in the portable folder are left as they are.
# The game files (~5 GB) are moved, not copied: the repository keeps a link to
# them (game_files -> portable folder) so linux/build-linux.sh still works.
set -euo pipefail

LINUX_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT="$(dirname "$LINUX_DIR")"
BUILD_DIR="$PROJECT/out/build/local-lin-relwithdebinfo"
PKG="$(realpath -m "${1:-$(dirname "$PROJECT")/VivaPinata}")"
LEGACY_USER="${XDG_DATA_HOME:-$HOME/.local/share}/vivapinata"

fail() { printf 'ERROR: %s\n' "$*" >&2; exit 1; }
[[ -x "$BUILD_DIR/vivapinata" ]] || fail "not built yet: run $LINUX_DIR/build-linux.sh first"
[[ "$PKG" != "$PROJECT" && "$PKG" != "$PROJECT"/* ]] || fail "the portable folder must be outside the repository"

mkdir -p "$PKG/settings" "$PKG/assets"

echo "Executable and libraries"
cp -f "$BUILD_DIR/vivapinata" "$PKG/vivapinata"
for lib in "$BUILD_DIR"/*.so*; do
  cp -fL "$lib" "$PKG/$(basename "$lib")"   # -L: real files, not the build-tree links
done
cp -f "$BUILD_DIR/gamecontrollerdb.txt" "$PKG/"

echo "Settings (existing ones in $PKG/settings are kept)"
for f in hardware.toml mapping.toml hardware.linux.toml launcher.toml; do
  if [[ -f "$PROJECT/settings/$f" && ! -f "$PKG/settings/$f" ]]; then
    cp "$PROJECT/settings/$f" "$PKG/settings/$f"
  fi
done

echo "Launcher art and menu-entry script"
cp -rf "$PROJECT/assets/launcher" "$PKG/assets/"
cp -f "$PROJECT/assets/launcher/icon.png" "$PKG/icon.png"
cp -f "$LINUX_DIR/add-vivapinata-shortcut.sh" "$PKG/"
chmod +x "$PKG/add-vivapinata-shortcut.sh"

if [[ ! -e "$PKG/game_files" ]]; then
  if [[ -d "$PROJECT/game_files" && ! -L "$PROJECT/game_files" ]]; then
    echo "Moving the game files into $PKG/game_files"
    mv "$PROJECT/game_files" "$PKG/game_files"
    ln -s "$PKG/game_files" "$PROJECT/game_files"
  else
    fail "no game files to package: run $LINUX_DIR/build-linux.sh first"
  fi
fi

# Portable mode: saves and the shader cache go to user/ in the portable folder.
touch "$PKG/portable.txt"
if [[ ! -d "$PKG/user" && -d "$LEGACY_USER" ]]; then
  echo "Copying your saves and shader cache from $LEGACY_USER"
  cp -a "$LEGACY_USER" "$PKG/user"
fi
mkdir -p "$PKG/user"

chmod +x "$PKG/vivapinata"

echo "Done: $PKG ($(du -sh "$PKG" | cut -f1))"
echo "Play:       $PKG/vivapinata"
echo "Menu entry: $PKG/add-vivapinata-shortcut.sh"
