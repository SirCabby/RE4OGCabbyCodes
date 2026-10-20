// RE4OGCabbyCodes - proxy entry point.
//
// We ship as steam_api.dll (see proxy.cpp for why). bio4.exe imports it
// statically and is not packed, so by the time DllMain runs the exe is mapped,
// relocated and its imports bound - but none of its code has run. DllMain loads
// the original Steam DLL, finds what the mod uses in the game (game.cpp: byte
// signatures over .text, a few milliseconds), patches the exe's import slots it
// needs (the frame loop's SetThreadAffinityMask for the tick, Direct3DCreate9
// for the panel, DirectInput8Create for the input guard, the crash filter) and
// starts a thread that only reports until the first tick comes.

#include <windows.h>

#include <cstdio>
#include <cstring>

#include "cheats.h"
#include "config.h"
#include "crash.h"
#include "dispatch.h"
#include "game.h"
#include "input.h"
#include "inventory.h"
#include "itembox.h"
#include "log.h"
#include "mem.h"
#include "overlay.h"
#include "proxy.h"
#include "savefiles.h"
#include "version.h"

namespace {

HMODULE g_self = nullptr;
char g_dir[MAX_PATH] = {};

// Directory this DLL was loaded from, with a trailing separator.
void resolve_own_dir() {
  char path[MAX_PATH] = {};
  GetModuleFileNameA(g_self, path, MAX_PATH);
  char* slash = std::strrchr(path, '\\');
  if (!slash) slash = std::strrchr(path, '/');
  if (slash) {
    size_t len = static_cast<size_t>(slash - path) + 1;
    if (len < sizeof(g_dir)) {
      std::memcpy(g_dir, path, len);
      g_dir[len] = '\0';
    }
  }
}

// A window's text without sending it a message: GetWindowText asks the
// window's thread for it and waits, and a thread that is not pumping would hold
// the mod thread for ever. InternalGetWindowText reads the stored text.
void window_text(HWND h, char* out, int n) {
  wchar_t w[256] = {};
  InternalGetWindowText(h, w, 256);
  if (!WideCharToMultiByte(CP_ACP, 0, w, -1, out, n, nullptr, nullptr)) out[0] = '\0';
  out[n - 1] = '\0';
}

// "class 'title'" for each of this process's top-level windows - an error box
// from the game says here what it says on screen.
struct WindowList {
  char text[512];
  size_t len;
};

BOOL CALLBACK list_window(HWND hwnd, LPARAM lp) {
  auto* w = reinterpret_cast<WindowList*>(lp);
  DWORD pid = 0;
  GetWindowThreadProcessId(hwnd, &pid);
  if (pid != GetCurrentProcessId() || !IsWindowVisible(hwnd) || w->len >= sizeof(w->text) - 1) return TRUE;
  char cls[64] = {}, title[96] = {};
  GetClassNameA(hwnd, cls, sizeof(cls));
  window_text(hwnd, title, sizeof(title));
  w->len += std::snprintf(w->text + w->len, sizeof(w->text) - w->len, "%s[%s '%s']", w->len ? "; " : "", cls, title);
  return TRUE;
}

// Until the first tick, say every 5 s what is on screen: a game that never
// reaches its frame loop is never a log that just stops.
DWORD WINAPI mod_thread(LPVOID) {
  using namespace re4cc;
  HANDLE ev = dispatch::first_tick_event();
  for (int s = 5; ev && s <= 120 && WaitForSingleObject(ev, 5000) == WAIT_TIMEOUT; s += 5) {
    WindowList w{};
    EnumWindows(list_window, reinterpret_cast<LPARAM>(&w));
    logf("%d s after start: no tick yet; windows: %s", s, w.len ? w.text : "none");
  }
  return 0;
}

}  // namespace

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID reserved) {
  using namespace re4cc;
  switch (reason) {
    case DLL_PROCESS_ATTACH: {
      g_self = module;
      DisableThreadLibraryCalls(module);
      resolve_own_dir();
      log_init(g_dir);
      logf("RE4OGCabbyCodes " RE4CC_VERSION " loaded (steam_api.dll proxy), dir=%s pid=%lu thread=%lu", g_dir,
           GetCurrentProcessId(), GetCurrentThreadId());

      if (!proxy::load_original(g_dir)) {
        MessageBoxA(nullptr, proxy::failure(), "RE4OGCabbyCodes", MB_OK | MB_ICONERROR);
        return FALSE;
      }

      install_crash_logger();
      HMODULE exe = GetModuleHandleA(nullptr);
      if (mem::iat_hook(exe, "KERNEL32.dll", "SetUnhandledExceptionFilter", crash_filter_hook()))
        logf("crash: bio4.exe's SetUnhandledExceptionFilter import hooked - the game's filter is chained, ours stays on top");
      config::load(g_dir);
      cheats::init();
      inventory::init();
      savefiles::init();
      itembox::init(g_dir);
      dispatch::init();

      if (config::get().disable_game) logf("game hooks disabled by config");
      else game::discover();
      if (!config::get().disable_tick) dispatch::install();
      // Import-table patches only - no game reads under the loader lock.
      if (!config::get().disable_overlay) overlay::install();
      input::install(exe);

      if (HANDLE t = CreateThread(nullptr, 0, mod_thread, nullptr, 0, nullptr)) CloseHandle(t);
      break;
    }
    case DLL_PROCESS_DETACH:
      // `reserved` is non-null when the whole process is ending (not a
      // FreeLibrary). Windows has already ended every other thread by then and
      // this runs under the loader lock, so touching the window, the Direct3D
      // device or ImGui here is unsafe - RE0CabbyCodes hung the game on close
      // that way on Windows (2026-09-25). The OS reclaims all of it: do nothing.
      if (reserved) {
        logf("process exiting - leaving the hooks for the OS to reclaim");
        break;
      }
      // Undo every patch before this image goes away: anything still pointing
      // into the DLL would fault the moment the game touched it.
      logf("unloading - removing hooks");
      dispatch::uninstall();
      cheats::remove_hooks();
      input::uninstall();
      overlay::uninstall();
      logf("hooks removed cleanly");
      log_shutdown();
      break;
    default:
      break;
  }
  return TRUE;
}
