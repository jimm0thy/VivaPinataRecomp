#!/usr/bin/env bash
# Builds the Linux port of Viva Pinata Recomp.
#
#   linux/build-linux.sh                     build (or resume: finished steps are skipped)
#   linux/build-linux.sh path/to/game.iso    same, extracting the game from that ISO
#   linux/build-linux.sh --clean             remove build output (keeps the SDK and game files)
#
# Everything stays inside the repository (all of it is git-ignored):
#   game_files/                    extracted from the ISO on the first run
#   rexglue/sdk-src/               the ReXGlue SDK fork source and its build tree
#   rexglue/linux-amd64/           the SDK, installed from that build
#   out/build/local-lin-relwithdebinfo/vivapinata   the game
#   out/build-logs/                one log per run
#
# The ISO is found as: the argument, else $VP_ISO, else the only *.iso in the
# repository folder, else the only *.iso in the folder above it.
# VP_SDK_SRC and VP_LOG_DIR move the SDK source and the logs elsewhere.
set -euo pipefail

LINUX_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT="$(dirname "$LINUX_DIR")"
SDK_SRC="${VP_SDK_SRC:-$PROJECT/rexglue/sdk-src}"
SDK_PREFIX="$PROJECT/rexglue/linux-amd64"
SDK_REPO="https://github.com/crabinacrabic/rexglue-sdk.git"
SDK_COMMIT="717fbdf"   # accurate-edram-20261016-717fbdf, what the Windows build pins
PRESET="local-lin-relwithdebinfo"
BUILD_DIR="$PROJECT/out/build/$PRESET"
XEX_SHA1="130dbbe05328eeca23ab830bc8ed3337064eddd3"

ISO_ARG=""
CLEAN=0
for arg in "$@"; do
  case "$arg" in
    --clean) CLEAN=1 ;;
    -h|--help) sed -n '2,18p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'; exit 0 ;;
    *) ISO_ARG="$arg" ;;
  esac
done

if ((CLEAN)); then
  rm -rf "$PROJECT/out/build" "$PROJECT/generated/default" "$SDK_SRC/out"
  echo "Removed build output. Game files, the installed SDK and the ISO are kept."
  exit 0
fi

LOG_DIR="${VP_LOG_DIR:-$PROJECT/out/build-logs}"
mkdir -p "$LOG_DIR"
LOG="$LOG_DIR/build-$(date +%Y%m%d-%H%M%S).log"
exec > >(tee -a "$LOG") 2>&1
step() { printf '\n==== %s ====\n' "$*"; }
fail() { printf '\nERROR: %s\nFull log: %s\n' "$*" "$LOG"; exit 1; }
trap 'fail "a command failed (line $LINENO)"' ERR

# --- 1. Tools ----------------------------------------------------------------
step "Checking tools"
missing=()
for tool in git cmake ninja clang clang++ sha1sum; do
  command -v "$tool" >/dev/null || missing+=("$tool")
done
command -v extract-xiso >/dev/null || [[ -f "$PROJECT/game_files/default.xex" ]] || missing+=("extract-xiso")
if ((${#missing[@]})); then
  fail "missing: ${missing[*]}  (Arch/CachyOS: sudo pacman -S --needed git cmake ninja clang; paru -S extract-xiso)"
fi
clang_major="$(clang --version | sed -n 's/.*clang version \([0-9]*\).*/\1/p' | head -1)"
(( clang_major >= 20 )) || fail "clang $clang_major found, the SDK needs clang 20 or newer"
echo "clang $clang_major, $(cmake --version | head -1), ninja $(ninja --version)"
[[ -f "$PROJECT/vivapinata_manifest.toml" ]] || fail "$PROJECT does not look like the VivaPinataRecomp repository"

# --- 2. Game files -----------------------------------------------------------
GAME="$PROJECT/game_files"
find_iso() {  # sets $iso
  iso="${ISO_ARG:-${VP_ISO:-}}"
  [[ -z "$iso" ]] || return 0
  local dir isos
  for dir in "$PROJECT" "$(dirname "$PROJECT")"; do
    shopt -s nullglob
    isos=("$dir"/*.iso)
    shopt -u nullglob
    if ((${#isos[@]} == 1)); then iso="${isos[0]}"; return 0; fi
    ((${#isos[@]} == 0)) || fail "more than one .iso in $dir; pass the one to use: $0 path/to/game.iso"
  done
  fail "no .iso found: pass it as $0 path/to/game.iso (or put it in $PROJECT)"
}
if [[ ! -f "$GAME/default.xex" ]]; then
  find_iso
  [[ -f "$iso" ]] || fail "ISO not found: $iso"
  step "Extracting the game from $(basename "$iso")"
  mkdir -p "$GAME"
  extract-xiso -x -d "$GAME" "$iso"
  # extract-xiso may create a subfolder; default.xex and Beta/ must be at the top.
  if [[ ! -f "$GAME/default.xex" ]]; then
    sub="$(find "$GAME" -mindepth 2 -maxdepth 2 -name default.xex -printf '%h\n' | head -1)"
    [[ -n "$sub" ]] || fail "default.xex not found after extraction"
    shopt -s dotglob; mv "$sub"/* "$GAME"/; shopt -u dotglob; rmdir "$sub"
  fi
fi
step "Checking the game version"
sha="$(sha1sum "$GAME/default.xex" | cut -d' ' -f1)"
[[ "$sha" == "$XEX_SHA1" ]] || fail "default.xex SHA-1 is $sha, expected $XEX_SHA1: this edition is not supported"
[[ -f "$GAME/Beta/bundles/englishus.bnl" ]] || fail "game_files/Beta/bundles/englishus.bnl is missing"
echo "default.xex OK (Viva Pinata USA/Europe, 4D5307F2)"

# --- 3. ReXGlue SDK fork from source -------------------------------------------
# The fork publishes Windows SDK builds only, so the same commit is built here.
sdk_needs_build=0
if [[ ! -f "$SDK_PREFIX/.vp-source-build" ]]; then
  step "Getting the ReXGlue SDK fork ($SDK_COMMIT)"
  if [[ ! -d "$SDK_SRC/.git" ]]; then
    git clone "$SDK_REPO" "$SDK_SRC"
  fi
  git -C "$SDK_SRC" fetch --tags origin
  git -C "$SDK_SRC" checkout -f "$SDK_COMMIT"
  git -C "$SDK_SRC" submodule update --init --recursive --jobs 8
  sdk_needs_build=1
fi

# Linux fixes to the fork (linux/sdk-patches/*.patch). Each is applied once; a
# newly applied patch triggers an incremental SDK rebuild below.
shopt -s nullglob
for patch in "$LINUX_DIR"/sdk-patches/*.patch; do
  if git -C "$SDK_SRC" apply --reverse --check "$patch" 2>/dev/null; then
    continue
  fi
  step "Applying SDK patch $(basename "$patch")"
  git -C "$SDK_SRC" apply "$patch"
  sdk_needs_build=1
done
shopt -u nullglob

if ((sdk_needs_build)); then
  step "Building the SDK (RelWithDebInfo; the first time this is the long part)"
  if [[ ! -f "$SDK_SRC/out/build/linux-amd64/CMakeCache.txt" ]]; then
    (cd "$SDK_SRC" && cmake --preset linux-amd64 -DCMAKE_INSTALL_PREFIX="$SDK_PREFIX")
  fi
  cmake --build "$SDK_SRC/out/build/linux-amd64" --config RelWithDebInfo --target install

  # The SDK's install registers itself in ~/.cmake/packages; this project
  # finds it by path, so remove the entry to keep nothing outside the repo.
  reg="$HOME/.cmake/packages/rexglue/$(printf '%s' "$SDK_PREFIX" | md5sum | cut -d' ' -f1)"
  rm -f "$reg"
  printf 'crabinacrabic/rexglue-sdk %s (%s)\n' "$SDK_COMMIT" "$(git -C "$SDK_SRC" describe --tags --always)" \
    > "$SDK_PREFIX/.vp-source-build"
fi
echo "SDK: $(cat "$SDK_PREFIX/.vp-source-build")"

# The SDK installs its code generator (the rexglue CLI) only from a Release
# build, and its CMake config maps every configuration to that Release tool.
# Build just that one target in Release and register it with the install.
if [[ ! -x "$SDK_PREFIX/bin/rexglue" ]]; then
  step "Building the code generator (rexglue CLI, Release)"
  cmake --build "$SDK_SRC/out/build/linux-amd64" --config Release --target rexglue
  tool="$(find "$SDK_SRC/out" -path '*/Release/*' -type f -name rexglue -perm -u+x | head -1)"
  [[ -n "$tool" ]] || fail "the Release rexglue tool was not found in the SDK build tree"
  # A symlink keeps the tool's build-tree RUNPATH, so its libraries resolve.
  mkdir -p "$SDK_PREFIX/bin"
  ln -sf "$tool" "$SDK_PREFIX/bin/rexglue"
  cat > "$SDK_PREFIX/lib/cmake/rexglue/rexglueTargets-release.cmake" <<'CMAKE'
# Written by linux/build-linux.sh: only the Release rexglue CLI is built.
set_property(TARGET rex::rexglue APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(rex::rexglue PROPERTIES
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/bin/rexglue")
CMAKE
  "$SDK_PREFIX/bin/rexglue" --help >/dev/null 2>&1 || fail "the rexglue tool does not run: $tool"
fi
echo "Code generator: $(readlink -f "$SDK_PREFIX/bin/rexglue")"

# --- 4. The game --------------------------------------------------------------
step "Configuring the game (the first time also runs the code generator)"
cd "$PROJECT"
cmake --preset "$PRESET"

step "Building the game"
cmake --build "$BUILD_DIR"

# The runtime and GPU plugin are shared libraries found next to the exe
# (rpath $ORIGIN). Link them from the SDK so an SDK rebuild needs no copy.
for lib in "$SDK_PREFIX"/lib/*.so*; do
  ln -sfr "$lib" "$BUILD_DIR/$(basename "$lib")"
done

step "Done"
echo "Game:     $BUILD_DIR/vivapinata"
echo "Run:      $LINUX_DIR/run-vivapinata.sh"
echo "Portable: $LINUX_DIR/package-linux.sh ~/Games/VivaPinata"
echo "Log:      $LOG"
