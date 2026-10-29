// input_mode.h - which input device owns the mouse (vp_input_mode)
//
// The SDK's keyboard+mouse driver captures and hides the mouse while
// mnk_mouse is on (the mouse turns the camera). This decides mnk_mouse every
// frame from vp_input_mode:
//   kbm         always captured (the previous behaviour)
//   controller  never captured; the keyboard still works
//   auto        released as soon as a gamepad is used, captured again on a
//               click in the game window or a game key (moving the mouse
//               alone never grabs it, so it can leave the window freely)
#pragma once

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <Xinput.h>
#pragma comment(lib, "xinput.lib")
#else
#include <SDL3/SDL.h>
#endif

#include <imgui.h>

#include <rex/cvar.h>
#include <rex/logging.h>
#include <rex/ui/imgui_dialog.h>

#include "game_cvars.h"

namespace vp_input
{
  class InputModeWatcher : public rex::ui::ImGuiDialog
  {
  public:
    explicit InputModeWatcher(rex::ui::ImGuiDrawer *drawer)
        : rex::ui::ImGuiDialog(drawer), pad_last_(GamepadConnected())
    {
    }

  protected:
    void OnDraw(ImGuiIO &io) override
    {
      const std::string &mode = REXCVAR_GET(vp_input_mode);
      bool capture = true;
      if (mode == "controller")
      {
        capture = false;
      }
      else if (mode == "auto")
      {
        if (GamepadUsed())
        {
          pad_last_ = true;
        }
        else if (KeyboardOrMouseUsed(io))
        {
          pad_last_ = false;
        }
        capture = !pad_last_;
      }
      if (capture != rex::cvar::Query<bool>("mnk_mouse"))
      {
        rex::cvar::SetFlagByName("mnk_mouse", capture ? "true" : "false");
        REXLOG_INFO("input: mouse {} ({} mode)", capture ? "captured for the camera" : "released",
                    mode);
      }
    }

  private:
    // A gamepad is plugged in at startup: auto starts with the mouse free.
    static bool GamepadConnected()
    {
#if defined(_WIN32)
      for (DWORD user = 0; user < XUSER_MAX_COUNT; ++user)
      {
        XINPUT_STATE state{};
        if (XInputGetState(user, &state) == ERROR_SUCCESS)
        {
          return true;
        }
      }
      return false;
#else
      return SDL_HasGamepad();
#endif
    }

    // Any button, or a stick / trigger past a dead zone, on any gamepad.
    // Windows reads XInput (the game's input backend there, as the F1 Back
    // hold in vp_tools does); elsewhere SDL, the input backend on Linux.
    static bool GamepadUsed()
    {
#if defined(_WIN32)
      for (DWORD user = 0; user < XUSER_MAX_COUNT; ++user)
      {
        XINPUT_STATE state{};
        if (XInputGetState(user, &state) != ERROR_SUCCESS)
        {
          continue;
        }
        const XINPUT_GAMEPAD &g = state.Gamepad;
        auto stick = [](SHORT v) { return v > 12000 || v < -12000; };
        if (g.wButtons != 0 || g.bLeftTrigger > XINPUT_GAMEPAD_TRIGGER_THRESHOLD ||
            g.bRightTrigger > XINPUT_GAMEPAD_TRIGGER_THRESHOLD || stick(g.sThumbLX) ||
            stick(g.sThumbLY) || stick(g.sThumbRX) || stick(g.sThumbRY))
        {
          return true;
        }
      }
      return false;
#else
      int count = 0;
      SDL_JoystickID *ids = SDL_GetGamepads(&count);
      bool used = false;
      for (int i = 0; ids && i < count && !used; ++i)
      {
        SDL_Gamepad *pad = SDL_GetGamepadFromID(ids[i]);
        if (!pad)
        {
          continue;
        }
        for (int b = 0; b < SDL_GAMEPAD_BUTTON_COUNT && !used; ++b)
        {
          used = SDL_GetGamepadButton(pad, SDL_GamepadButton(b));
        }
        for (int a = 0; a < SDL_GAMEPAD_AXIS_COUNT && !used; ++a)
        {
          const int value = SDL_GetGamepadAxis(pad, SDL_GamepadAxis(a));
          const bool trigger = a == SDL_GAMEPAD_AXIS_LEFT_TRIGGER || a == SDL_GAMEPAD_AXIS_RIGHT_TRIGGER;
          used = trigger ? value > 8000 : (value > 12000 || value < -12000);
        }
      }
      SDL_free(ids);
      return used;
#endif
    }

    // A click in the game window (not on an ImGui window) or a default game
    // key from settings/mapping.toml. Mouse movement alone does not count.
    static bool KeyboardOrMouseUsed(const ImGuiIO &io)
    {
      if (!io.WantCaptureMouse)
      {
        for (bool clicked : io.MouseClicked)
        {
          if (clicked)
          {
            return true;
          }
        }
      }
      if (io.WantCaptureKeyboard)
      {
        return false;
      }
      static constexpr ImGuiKey kGameKeys[] = {
          ImGuiKey_W,         ImGuiKey_A,          ImGuiKey_S,        ImGuiKey_D,
          ImGuiKey_Space,     ImGuiKey_Backspace,  ImGuiKey_Enter,    ImGuiKey_Tab,
          ImGuiKey_Q,         ImGuiKey_E,          ImGuiKey_L,        ImGuiKey_P,
          ImGuiKey_F,         ImGuiKey_K,          ImGuiKey_1,        ImGuiKey_3,
          ImGuiKey_UpArrow,   ImGuiKey_DownArrow,  ImGuiKey_LeftArrow, ImGuiKey_RightArrow,
      };
      for (ImGuiKey key : kGameKeys)
      {
        if (ImGui::IsKeyPressed(key, false))
        {
          return true;
        }
      }
      return false;
    }

    bool pad_last_;
  };
}  // namespace vp_input
