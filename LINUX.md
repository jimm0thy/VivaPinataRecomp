# Linux port (local)

Local changes on branch `linux-port`, on top of upstream a510fc7:

- `src/launcher.h`: Linux fonts (Noto/DejaVu/Liberation Sans), a portable
  SHA-1 for the version check, restart via fork/exec, locale-based language,
  and the Fast render mode on Vulkan (FBO + fast readback) instead of D3D12.
- `src/utils.h` + `settings/hardware.linux.toml`: Linux overrides loaded after
  hardware.toml / mapping.toml (Vulkan, SDL input).
- `src/vivapinata_app.h`: SDL input instead of XInput.
- `src/vp_tools/*`: F1 / F2 overlay toggles through ImGui.
- `CMakeLists.txt`, `CMakeUserPresets.json`: use the SDK fork built from
  source (`rexglue/linux-amd64/.vp-source-build`), plain `clang`/`clang++`.

Build and run with `../build-linux.sh` and `../run-vivapinata.sh`.
