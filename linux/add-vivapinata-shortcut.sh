#!/usr/bin/env bash
# Adds Viva Pinata (the portable Linux build) to the application menu's Games
# section, using the game's own settingssave.png from game_files as the icon.
#
# Usage:
#   ./add-vivapinata-shortcut.sh                    # find the game folder automatically
#   ./add-vivapinata-shortcut.sh ~/Games/VivaPinata # or give the folder
#   ./add-vivapinata-shortcut.sh --remove           # remove the shortcut and icon
set -euo pipefail

APP_ID="vivapinata-recomp"
DATA_HOME="${XDG_DATA_HOME:-$HOME/.local/share}"
DESKTOP_DIR="$DATA_HOME/applications"
DESKTOP_FILE="$DESKTOP_DIR/$APP_ID.desktop"
ICON_DIR="$DATA_HOME/icons"
ICON_FILE="$ICON_DIR/$APP_ID.png"

refresh_menu() {
  command -v update-desktop-database >/dev/null && update-desktop-database -q "$DESKTOP_DIR" 2>/dev/null || true
  if command -v kbuildsycoca6 >/dev/null; then kbuildsycoca6 >/dev/null 2>&1 || true
  elif command -v kbuildsycoca5 >/dev/null; then kbuildsycoca5 >/dev/null 2>&1 || true
  fi
}

if [[ "${1:-}" == "--remove" ]]; then
  rm -f "$DESKTOP_FILE" "$ICON_FILE"
  refresh_menu
  echo "Removed the Viva Pinata shortcut."
  exit 0
fi

# --- Locate the game folder (the one holding vivapinata and game_files) ------
GAME_DIR=""
if [[ $# -ge 1 ]]; then
  GAME_DIR="$1"
  [[ -f "$GAME_DIR" ]] && GAME_DIR="$(dirname "$GAME_DIR")"
else
  mapfile -t found < <(find "$HOME/Games" "$HOME/games" "$HOME/Applications" "$HOME/Desktop" "$HOME" \
                         -maxdepth 4 \( -path "$DATA_HOME" -o -path "$HOME/.cache" -o -path "$HOME/.steam" \
                         -o -path "$HOME/.wine" -o -name game_files -o -name VivaPinataRecomp \) -prune \
                         -o -type f -name vivapinata -perm -u+x -print 2>/dev/null \
                       | while read -r exe; do d="$(dirname "$exe")"; [[ -d "$d/game_files" ]] && echo "$d"; done \
                       | awk '!seen[$0]++')
  if [[ ${#found[@]} -eq 0 ]]; then
    echo "Couldn't find the Viva Pinata folder. Run again with it, e.g.:" >&2
    echo "  $0 ~/Games/VivaPinata" >&2
    exit 1
  elif [[ ${#found[@]} -gt 1 ]]; then
    echo "Found more than one copy; run again with the one you want:" >&2
    printf '  %s\n' "${found[@]}" >&2
    exit 1
  fi
  GAME_DIR="${found[0]}"
fi
GAME_DIR="$(realpath "$GAME_DIR")"
[[ -x "$GAME_DIR/vivapinata" ]] || { echo "No vivapinata executable in $GAME_DIR" >&2; exit 1; }

EXE="$GAME_DIR/vivapinata"

# --- Icon: settingssave.png from the game files (any letter case) ---------------
SRC_ICON="$(find "$GAME_DIR/game_files" -type f -iname 'settingssave.png' 2>/dev/null | head -1)"
if [[ -z "$SRC_ICON" ]]; then
  echo "settingssave.png not found under $GAME_DIR/game_files; using icon.png instead." >&2
  SRC_ICON="$GAME_DIR/icon.png"
fi
[[ -f "$SRC_ICON" ]] || { echo "No icon found." >&2; exit 1; }
mkdir -p "$ICON_DIR" "$DESKTOP_DIR"
cp -f "$SRC_ICON" "$ICON_FILE"   # a copy, so the menu keeps its icon if the game moves

# --- Menu entry ------------------------------------------------------------------
exec_path="$(printf '%s' "$EXE" | sed -e 's/[\\"`$]/\\&/g')"
cat > "$DESKTOP_FILE" <<DESKTOP
[Desktop Entry]
Type=Application
Version=1.0
Name=Viva Piñata
GenericName=Life Simulation Game
Comment=Viva Piñata (Xbox 360), recompiled for Linux
Exec="$exec_path"
Path=$GAME_DIR
Icon=$ICON_FILE
Terminal=false
Categories=Game;Simulation;
StartupWMClass=vivapinata-recomp
Keywords=viva;pinata;garden;rare;xbox;
DESKTOP
chmod +x "$DESKTOP_FILE"

command -v desktop-file-validate >/dev/null && desktop-file-validate "$DESKTOP_FILE" || true
refresh_menu

echo "Added Viva Piñata to Games."
echo "  Launches: $EXE"
echo "  Icon:     $SRC_ICON"
echo "  Shortcut: $DESKTOP_FILE"
