# Linux port (local)

Local changes on branch `linux-port`, on top of upstream a510fc7:

- `src/launcher.h`: Linux fonts (Noto/DejaVu/Liberation Sans), a portable
  SHA-1 for the version check, restart via fork/exec, locale-based language.
  Render mode is fixed to Accurate on Linux (Vulkan FSI + lazy readback):
  Fast (FBO) loses the Vulkan device on NVIDIA during the intro, so it is
  hidden and old `fbo` settings are migrated. The Precise timer checkbox is
  Windows only and hidden. New options: Controls (auto / keyboard+mouse /
  controller, `vp_input_mode`) and Anisotropic filtering
  (`anisotropic_override`).
- `src/utils.h` + `settings/hardware.linux.toml`: Linux overrides loaded after
  hardware.toml / mapping.toml (Vulkan, SDL input, `vp_fast_save`).
- `src/vivapinata_app.h`: SDL input instead of XInput; chdir to the exe
  folder and portable mode (`portable.txt` -> saves and cache in `user/`);
  open-file limit raised to the hard limit (the NVIDIA driver uses one fd per
  fence; Save & Exit used to lock up); F12 quits; window/taskbar icon from
  `game_files/settingssave.png`; SDL app id `vivapinata-recomp`.
- `src/input_mode.h`: releases / captures the mouse per `vp_input_mode`
  (auto releases it as soon as a gamepad is used).
- `src/game_fixes.h` section 2: `sub_821FA490` without its Sleep(0) per
  record. The game counts its save records every time it appends one, so a
  save made ~n^2/2 Sleep(0) calls; the SDK turns Sleep(0) on a below-normal
  thread into a 100 us nanosleep, so saves and loads took 15-20 s and the
  music stalled. Now a plain yield (near-instant). `vp_fast_save = false`
  restores the original code.
- `src/vp_tools/*`: F1 / F2 overlay toggles through ImGui.
- `CMakeLists.txt`, `CMakeUserPresets.json`: use the SDK fork built from
  source (`rexglue/linux-amd64/.vp-source-build`), plain `clang`/`clang++`.

- `linux/`: build, run and packaging scripts, the menu-entry script and the
  SDK patch (`sdk-patches/`: `__ImageBase` replaced with `dladdr` on Linux in
  `vulkan/command_processor.cpp`). How to build and play: `linux/README.md`.
