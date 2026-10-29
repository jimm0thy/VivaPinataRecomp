// vivapinata_app.h - ReXApp lifecycle for Viva Pinata (2006, 4D5307F2)
//
// Lifecycle (rex/rex_app.h):
//   SetupEnvironment   OnConfigurePaths -> config TOML -> logging -> OnPostInitLogging
//   SetupPresentation  OnPreSetup(RuntimeConfig&) -> window/ImGui (OnConfigureFonts)
//                      -> OnCreateDialogs
//   OnFinalizePaths    launcher (src/launcher.h); runtime is built after PLAY
//   ConstructRuntime   OnLoadXexImage -> LoadXexImage -> OnPostLoadXexImage -> OnPostSetup
//   LaunchModule       OnPreLaunchModule -> OnPostLaunchModule(XThread*) -> OnGuestThreadExit
//   OnShutdown
#pragma once

#include <cstdlib>
#include <memory>

#include <rex/cvar.h>
#include <rex/input/flags.h>
#include <rex/input/input_system.h>
#include <rex/logging.h>
#include <rex/memory/utils.h>
#include <rex/rex_app.h>
#include <rex/runtime.h>
#include <rex/system/gpu_plugin.h>
#include <rex/ui/keybinds.h>

#include <fstream>
#include <iterator>
#include <vector>

#if !defined(_WIN32)
#include <SDL3/SDL.h>
#include <rex/ui/image_decode.h>
#endif

#if !defined(_WIN32)
#include <sys/resource.h>
#endif

#include "debug_tools.h"
#include "game_cvars.h"
#include "game_timing.h"
#include "input_mode.h"
#include "launcher.h"
#include "utils.h"
#include "vp_tools/crash_log.h"
#include "vp_tools/perf_overlay.h"
#include "vp_tools/remote_input.h"
#include "vp_tools/tools_dialog.h"

class VivapinataApp : public rex::ReXApp
{
public:
  using rex::ReXApp::ReXApp;

  static std::unique_ptr<rex::ui::WindowedApp> Create(rex::ui::WindowedAppContext &ctx)
  {
    return std::unique_ptr<VivapinataApp>(new VivapinataApp(ctx, "vivapinata", PPCImageConfig));
  }

  void OnConfigurePaths(rex::PathConfig &paths) override
  {
    // Run from the executable's folder whatever the caller's working
    // directory: the SDK's logs/ and logs/vp_crash.txt are relative to it.
    // Logging starts after this hook, so the logs land next to the game.
    {
      std::error_code ec;
      std::filesystem::current_path(rex::filesystem::GetExecutableFolder(), ec);
    }

#if !defined(_WIN32)
    // Wayland app_id / X11 class: KDE shows the taskbar icon of the .desktop
    // file with this name (add-vivapinata-shortcut.sh writes
    // vivapinata-recomp.desktop). Read when the window is created, later.
    SDL_SetHint(SDL_HINT_APP_ID, "vivapinata-recomp");
#endif

    // --game_data_root / REX_GAME_DATA_ROOT are read before any config file,
    // so if they were given the path is already non-empty here.
    // game_files/ is the extracted disc: default.xex + Beta\ (Rare's data
    // root: bundles_packages, movie, font, xwavebank...). The SDK mounts it
    // as game: and d:, so guest paths like game:\Beta\... resolve directly.
    if (paths.game_data_root.empty())
    {
      paths.game_data_root = utils::RepoRoot() / "game_files";
    }

    // Portable mode: with portable.txt next to the game, saves and the shader
    // cache live in user/ beside it instead of ~/.local/share/vivapinata.
    // --user_data_root / --cache_root still win when given.
    if (std::filesystem::exists(utils::RepoRoot() / "portable.txt"))
    {
      const std::filesystem::path user = utils::RepoRoot() / "user";
      if (REXCVAR_GET(user_data_root).empty())
      {
        paths.user_data_root = user;
      }
      if (REXCVAR_GET(cache_root).empty())
      {
        paths.cache_root = paths.user_data_root / "cache";
      }
    }

    // settings/hardware.toml becomes the SDK's primary config file (also the
    // target of the in-game Settings overlay "Save to config").
    paths.config_path = utils::SettingsDir() / "hardware.toml";

    // First pass: window/input cvars exist already. GPU cvars register later
    // and are picked up by the second pass in OnPostSetup.
    utils::LoadSettingsFiles();
  }

  // Before the GPU is set up. On Linux, NVIDIA's Vulkan driver uses a file
  // descriptor per fence, and Accurate mode's lazy readback can keep hundreds
  // of submissions (one fence each) in flight, e.g. on Save & Exit. With the
  // usual soft limit of 1024 the process runs out of descriptors, after which
  // fences, threads, memory and file opens all fail and the game locks up.
  // Raise the soft limit to the hard limit, as Proton and native games do.
  void OnPostInitLogging() override
  {
#if !defined(_WIN32)
    rlimit limit{};
    if (getrlimit(RLIMIT_NOFILE, &limit) == 0 && limit.rlim_cur < limit.rlim_max)
    {
      const rlim_t old_limit = limit.rlim_cur;
      limit.rlim_cur = limit.rlim_max;
      if (setrlimit(RLIMIT_NOFILE, &limit) == 0)
      {
        REXLOG_INFO("Raised the open file limit from {} to {}", uint64_t(old_limit),
                    uint64_t(limit.rlim_cur));
      }
      else
      {
        REXLOG_WARN("Could not raise the open file limit (still {})", uint64_t(old_limit));
      }
    }
#endif
  }

  void OnPreSetup(rex::RuntimeConfig &config) override
  {
    // Before the GPU API is chosen: the old Accurate mode (D3D12 ROV) is Vulkan
    // FSI now. Then the settings files are loaded again: the SDK loads its
    // primary config (settings/hardware.toml) after OnConfigurePaths, which
    // undoes launcher.toml for keys that hardware.toml has too, such as
    // graphics_backend. This also updates the values kept for the GPU cvars,
    // which register with the plugin below.
    vp_launcher::MigrateLegacyRenderMode(utils::SettingsDir() / "launcher.toml");
    utils::LoadSettingsFiles();

    // Native XInput avoids SDL controller-mapping surprises on Windows;
    // settings/mapping.toml can still override input_backend. Linux has no
    // XInput, so it always uses SDL.
#if defined(_WIN32)
    REXCVAR_SET(input_backend, "xinput");
#else
    REXCVAR_SET(input_backend, "sdl");
#endif

    std::string backend = REXCVAR_GET(graphics_backend);
    vp_launcher::ActiveBackend() = backend;
    if (backend != "any" && !config.gpu_plugin.empty())
    {
      config.graphics = rex::system::LoadGpuPlugin(config.gpu_plugin, backend);
      if (!config.graphics)
      {
        REXLOG_WARN("graphics_backend '{}' unavailable, falling back to automatic selection",
                    backend);
        vp_launcher::BackendFallback() = true;
      }
    }
  }

  // Pre-commit the physical aliases (0xA0000000 uncached, 0xC0000000,
  // 0xE0000000) so a GPU ring-buffer write through an alias never faults
  // on an uncommitted page (AO2 sub_82A50FF0 incident, KB ERROR_LOOKUP_TABLE).
  // Cheap insurance for a Xenos title; remove if profiling shows RSS pressure.
  static void PrecommitMemoryAliases()
  {
    auto *rt = rex::Runtime::instance();
    uint8_t *base = rt ? rt->virtual_membase() : nullptr;
    if (!base)
    {
      return;
    }

#if defined(_WIN32)
    VirtualAlloc(base + 0xA0000000ull, 0x20000000ull, MEM_COMMIT, PAGE_READWRITE);
    VirtualAlloc(base + 0xC0000000ull, 0x20000000ull, MEM_COMMIT, PAGE_READWRITE);
    VirtualAlloc(base + 0xE0000000ull, 0x1FD00000ull, MEM_COMMIT, PAGE_READWRITE);
#else
    rex::memory::AllocFixed(base + 0xA0000000ull, 0x20000000ull, rex::memory::AllocationType::kCommit, rex::memory::PageAccess::kReadWrite);
    rex::memory::AllocFixed(base + 0xC0000000ull, 0x20000000ull, rex::memory::AllocationType::kCommit, rex::memory::PageAccess::kReadWrite);
    rex::memory::AllocFixed(base + 0xE0000000ull, 0x1FD00000ull, rex::memory::AllocationType::kCommit, rex::memory::PageAccess::kReadWrite);
#endif
    REXLOG_INFO("Pre-committed guest physical aliases (0xA0000000, 0xC0000000, 0xE0000000)");
  }

  // Segoe UI with Cyrillic for the launcher (the SDK font is Latin-1 only).
  void OnConfigureFonts(ImFontAtlas *atlas) override
  {
    vp_launcher::LoadFonts(atlas);
  }

  // Launcher: keep the runtime unbuilt until PLAY, so the GPU settings
  // chosen there (render path, resolution scale) apply to this launch.
  // --vp_show_launcher=false (or unticking it in the launcher) starts directly.
  std::optional<rex::PathConfig> OnFinalizePaths(const rex::PathConfig &defaults,
                                                 std::function<void(rex::PathConfig)> resume) override
  {
    if (!REXCVAR_GET(vp_show_launcher) || !imgui_drawer())
    {
      ApplyGameLanguage(defaults.game_data_root);
      return defaults;
    }
    launcher_ = std::make_unique<vp_launcher::LauncherDialog>(
        imgui_drawer(), immediate_drawer(), utils::RepoRoot(), defaults.game_data_root,
        [this, defaults, resume]() {
          // Called inside the launcher's ImGui frame: leave it first, then drop
          // the dialog and build the runtime.
          app_context().CallInUIThreadDeferred([this, defaults, resume]() {
            launcher_.reset();
            ApplyGameLanguage(defaults.game_data_root);
            resume(defaults);
          });
        },
        [this]() {
          app_context().CallInUIThreadDeferred([this]() {
            if (window())
            {
              window()->RequestClose();
            }
          });
        });
    return std::nullopt;
  }

  void OnPostLoadXexImage() override
  {
    PrecommitMemoryAliases();
    // Byte patches to the loaded image go here (game_patches::* once any
    // address is verified against this XEX).
  }

  // VP Tools menu (src/vp_tools/tools_dialog.h): stays registered and draws
  // nothing until F1 / a Back hold opens it in the game. The performance
  // overlay (src/vp_tools/perf_overlay.h) likewise until F2.
  void OnCreateDialogs(rex::ui::ImGuiDrawer *drawer) override
  {
    tools_ = std::make_unique<vp_tools::ToolsDialog>(drawer, window());
    perf_overlay_ = std::make_unique<vp_tools::PerfOverlay>(drawer, window());
#if !defined(_WIN32)
    SetWindowIconFromGame();
#endif
    // Mouse capture by input mode (src/input_mode.h, vp_input_mode).
    input_mode_ = std::make_unique<vp_input::InputModeWatcher>(drawer);

    // Quit key (default F12; rebind as bind_quit in the settings overlay, F4).
    // Closing the window is the same as the close button: the SDK shuts the
    // title down cleanly.
    rex::ui::RegisterBind("bind_quit", "F12", "Quit the game", [this]() {
      app_context().CallInUIThreadDeferred([this]() {
        if (window())
        {
          window()->RequestClose();
        }
      });
    });
  }

  void OnPostSetup() override
  {
    // Second config pass: GPU-backend cvars (vsync, resolution_scale,
    // render_target_path_d3d12...) are registered by now.
    utils::LoadSettingsFiles();

    // Names the guest code behind a bad guest memory access in the log
    // (the SDK reports only the address).
    vp_tools::CrashLog::Install();

    // No guest pad input while the VP Tools menu is open; otherwise the SDK's
    // usual checks (window focus, ImGui not using the mouse). Same scheme as
    // ReTiP's RetipApp::OnPostSetup.
    if (auto *input = static_cast<rex::input::InputSystem *>(runtime()->input_system()))
    {
      // --vp_tools_remote=<dir>: synthetic pad for unattended lab runs; it
      // ignores the focus check below (src/vp_tools/remote_input.h).
      if (const std::string remote = REXCVAR_GET(vp_tools_remote); !remote.empty())
      {
        auto driver = std::make_unique<vp_tools::RemoteInputDriver>(window(), remote);
        if (XSUCCEEDED(driver->Setup()))
        {
          input->AddDriver(std::move(driver));
        }
      }
      input->SetActiveCallback([this]() {
        if (vp_tools::g_menu_open)
        {
          return false;
        }
        if (window() && !window()->HasFocus())
        {
          return false;
        }
        return !imgui_drawer() || !imgui_drawer()->GetIO().WantCaptureMouse;
      });
    }

    if (REXCVAR_GET(vp_high_res_timer))
    {
      vp_timing::EnableHighResTimer();  // TiP-Recomp: timeBeginPeriod(1)
    }

    if (REXCVAR_GET(dev_debug_runtime))
    {
      // Discovery mode. Order matters: the pointer scan asks the dispatcher
      // which targets are registered, the sweep then registers a stub on
      // every remaining address. (PerformMissingFunctionScan is available
      // too but dumps ~1.5M interior addresses - not useful by default.)
      debug_tools::PerformDataPointerScan();
      debug_tools::PerformStubSweep();
    }
  }

  void OnShutdown() override
  {
    rex::ui::UnregisterBind("bind_quit");
    launcher_.reset();
    tools_.reset();
    perf_overlay_.reset();
    input_mode_.reset();
    if (REXCVAR_GET(vp_high_res_timer))
    {
      vp_timing::DisableHighResTimer();
    }
  }

  // Available for later phases:
  // void OnPostInitLogging() override {}
  // void OnLoadXexImage(std::string& xex_image) override {}   // default: game:\default.xex
  // void OnPreLaunchModule() override {}                        // last chance to patch guest memory
  // void OnPostLaunchModule(rex::system::XThread* thread) override {}
  // std::unique_ptr<rex::ui::ImGuiDialog> CreateAchievementsOverlay() override;
  // std::unique_ptr<rex::ui::AchievementNotificationDialog> CreateAchievementNotificationDialog() override;

private:
#if !defined(_WIN32)
  // Window icon from the game's own settingssave.png (game_files/). The SDK's
  // Window::SetIcon takes a Windows .ico and does nothing on Linux, so this
  // goes through SDL directly. Windows uses the .rc icon instead.
  static void SetWindowIconFromGame()
  {
    const std::filesystem::path png = utils::RepoRoot() / "game_files" / "settingssave.png";
    std::ifstream file(png, std::ios::binary);
    if (!file)
    {
      REXLOG_WARN("window icon: {} not found", png.string());
      return;
    }
    const std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(file)),
                                     std::istreambuf_iterator<char>());
    int width = 0;
    int height = 0;
    std::vector<uint8_t> pixels = rex::ui::DecodeImageRGBA(bytes.data(), bytes.size(), width, height);
    if (pixels.empty() || width <= 0 || height <= 0)
    {
      REXLOG_WARN("window icon: could not decode {}", png.string());
      return;
    }
    SDL_Surface *surface =
        SDL_CreateSurfaceFrom(width, height, SDL_PIXELFORMAT_RGBA32, pixels.data(), width * 4);
    if (!surface)
    {
      return;
    }
    int count = 0;
    SDL_Window **windows = SDL_GetWindows(&count);
    for (int i = 0; windows && i < count; ++i)
    {
      SDL_SetWindowIcon(windows[i], surface);
    }
    SDL_free(windows);
    SDL_DestroySurface(surface);
  }
#endif

  // Swaps the text bundles before the guest opens them (vp_language).
  static void ApplyGameLanguage(const std::filesystem::path &game_root)
  {
    vp_launcher::ApplyLanguage(game_root, REXCVAR_GET(vp_language) == "ru");
  }

  std::unique_ptr<vp_launcher::LauncherDialog> launcher_;
  std::unique_ptr<vp_tools::ToolsDialog> tools_;
  std::unique_ptr<vp_tools::PerfOverlay> perf_overlay_;
  std::unique_ptr<vp_input::InputModeWatcher> input_mode_;
};
