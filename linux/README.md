# Viva Piñata Recomp on Linux

Builds and runs natively on Linux (Vulkan, SDL input). Tested on CachyOS
(Arch) with KDE Plasma 6 and an NVIDIA RTX 4070 SUPER. What the port changes
in the code is listed in [`../LINUX.md`](../LINUX.md).

## Requirements

- Your own disc image of **Viva Piñata (USA, Europe)**; `default.xex` must
  have SHA-1 `130dbbe05328eeca23ab830bc8ed3337064eddd3` (checked by the build).
- clang 20 or newer, cmake, ninja, git, and `extract-xiso`. On Arch/CachyOS:

  ```sh
  sudo pacman -S --needed git cmake ninja clang
  paru -S extract-xiso        # AUR
  ```
- A Vulkan driver with fragment shader interlock (current NVIDIA, Mesa
  RADV/ANV).

## Build

```sh
git clone -b linux-port https://github.com/jimm0thy/VivaPinataRecomp.git
cd VivaPinataRecomp
linux/build-linux.sh "/path/to/Viva Pinata (USA, Europe).iso"
```

The first run extracts the disc into `game_files/`, clones and builds the
ReXGlue SDK fork (`crabinacrabic/rexglue-sdk` at `717fbdf`, which only ships
Windows builds) into `rexglue/`, applies the Linux fixes in `sdk-patches/`,
runs the code generator and builds the game. That takes a while; later runs
only rebuild what changed. Everything stays inside the repository folder
(all of it git-ignored), and every run writes a log to `out/build-logs/`.

- `linux/build-linux.sh` without an argument looks for a single `.iso` in the
  repository folder or the folder above it.
- `linux/build-linux.sh --clean` removes the build output but keeps the SDK
  and the game files.

## Play

```sh
linux/run-vivapinata.sh                   # from the build tree
```

Or make a portable folder that holds everything the game needs, including
your saves and shader cache (`portable.txt` puts them in `user/`), and add it
to the application menu:

```sh
linux/package-linux.sh ~/Games/VivaPinata
~/Games/VivaPinata/add-vivapinata-shortcut.sh
```

The game files are moved into the portable folder (the repository keeps a
link to them). Run `package-linux.sh` again after a rebuild to update the
executable and libraries; settings and saves there are left alone.

Without `portable.txt`, saves go to `~/.local/share/vivapinata`.

## Notes

- **Render mode:** only Accurate (Vulkan fragment shader interlock) is offered.
  Fast (Vulkan render targets) lost the device on NVIDIA during the intro
  movie, so it is hidden on Linux.
- **Keys:** F12 quits, F1 tools, F2 performance overlay, F4 settings overlay.
- **Mouse:** the launcher's *Controls* option chooses who owns the mouse:
  auto (released when a controller is used), keyboard + mouse, or controller.
- **Saves:** `vp_fast_save = true` in `settings/hardware.linux.toml` makes
  saving and loading near-instant (see `LINUX.md`); set it to `false` for the
  original behaviour.
