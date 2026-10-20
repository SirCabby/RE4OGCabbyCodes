#include "dispatch.h"

#include "cheats.h"
#include "config.h"
#include "crash.h"
#include "input.h"
#include "inventory.h"
#include "log.h"
#include "mem.h"
#include "overlay.h"
#include "savefiles.h"

namespace re4cc::dispatch {
namespace {

DWORD g_main_thread = 0;  // the process's primary thread: DllMain(PROCESS_ATTACH) runs on it, and so does the game
using AffinityFn = DWORD_PTR(WINAPI*)(HANDLE, DWORD_PTR);
using CounterFn = BOOL(WINAPI*)(LARGE_INTEGER*);
AffinityFn g_real_affinity = nullptr;
CounterFn g_real_counter = nullptr;
uintptr_t g_affinity_slot = 0, g_counter_slot = 0;
// The frame's tick came from its SetThreadAffinityMask call: the
// QueryPerformanceCounter call after it in the same frame lets it be (main
// thread only).
bool g_ticked_first = false;
bool g_first_point_ticked = false;  // ever: a frame without it later is logged once
volatile LONG g_second_logged = 0;
Snapshot g_snap;
CRITICAL_SECTION g_snap_lock;
bool g_lock_ready = false;
volatile LONG g_first_tick = 0;
volatile LONG g_foreign_logged = 0;
HANDLE g_first_event = nullptr;
DWORD g_last_filter = 0;
DWORD g_last_rates = 0;
bool g_in_game_prev = false;
bool g_paused_prev = false;
bool g_card_prev = false;

void tick(const char* from) {
  const DWORD now = GetTickCount();
  if (!g_first_tick) {
    InterlockedExchange(&g_first_tick, 1);
    if (g_first_event) SetEvent(g_first_event);
    logf("main-thread tick running on thread %lu (the frame loop's %s)", GetCurrentThreadId(), from);
  }
  if (now - g_last_filter > 1000) {
    g_last_filter = now;
    reassert_crash_filter();
  }
  if (now - g_last_rates >= 1000) {
    g_last_rates = now;
    overlay::dx9::log_rates();
  }

  Snapshot s;
  s.main_thread = g_main_thread;
  s.ticks = g_snap.ticks + 1;
  s.ready = game::ready();
  if (s.ready && !config::get().disable_game) {
    game::trace_tick();
    game::run(&s.run);
    if (s.run.in_game != g_in_game_prev) {
      logf("game %s (%s, %s)", s.run.in_game ? "started" : "ended", game::mode_name(s.run.mode),
           game::character_name(s.run.character));
      g_in_game_prev = s.run.in_game;
    }
    if (s.run.paused != g_paused_prev) {
      if (config::get().trace) logf("pause %s", s.run.paused ? "opened" : "closed");
      g_paused_prev = s.run.paused;
    }
    // The cheats run every tick, paused or not: their patches follow the
    // switches at once, and the holds are re-asserted every frame.
    cheats::tick(s.run);
    // The load and save lists' save file manager.
    savefiles::tick(s.run);
    // The attache case is only written while the game is paused.
    inventory::tick(s.run);
    s.card_list = savefiles::list_up();
    if (s.card_list != g_card_prev) {
      if (config::get().trace) logf("load/save list %s", s.card_list ? "up" : "gone");
      g_card_prev = s.card_list;
    }
  }
  s.show_panel = s.run.paused || s.card_list;
  // The game's mouse is let go while the panel is up (its own thread does it).
  input::tick(s.show_panel || config::get().always_show);
  if (g_lock_ready) {
    EnterCriticalSection(&g_snap_lock);
    g_snap = s;
    LeaveCriticalSection(&g_snap_lock);
  }
}

void foreign_thread() {
  if (!InterlockedExchange(&g_foreign_logged, 1))
    logf("dispatch: the frame loop called from thread %lu, not the main thread %lu - no tick", GetCurrentThreadId(),
         g_main_thread);
}

DWORD_PTR WINAPI hk_affinity(HANDLE thread, DWORD_PTR mask) {
  // Ours is the call the frame loop makes as each frame starts; every other
  // call goes straight through.
  const uintptr_t ret = reinterpret_cast<uintptr_t>(__builtin_return_address(0));
  if (ret == game::tick_return() && ret) {
    if (GetCurrentThreadId() == g_main_thread) {
      tick("SetThreadAffinityMask");
      g_ticked_first = g_first_point_ticked = true;
    } else {
      foreign_thread();
    }
  }
  return g_real_affinity(thread, mask);
}

// The same frame's QueryPerformanceCounter call, a little after that one: the
// tick when the first call did not come this frame (code that replaces the
// frame wait's - another tool's frame limiter - takes it away). The game calls
// QueryPerformanceCounter from many places and threads; only that call is
// looked at.
BOOL WINAPI hk_counter(LARGE_INTEGER* out) {
  const uintptr_t ret = reinterpret_cast<uintptr_t>(__builtin_return_address(0));
  if (ret == game::tick_return_late() && ret) {
    if (GetCurrentThreadId() != g_main_thread) {
      foreign_thread();
    } else if (g_ticked_first) {
      g_ticked_first = false;
    } else {
      if (g_first_point_ticked && !InterlockedExchange(&g_second_logged, 1))
        logf("dispatch: the frame loop's SetThreadAffinityMask call no longer comes (its code was replaced after the "
             "start: another tool's frame limiter?) - the tick comes from the same frame's QueryPerformanceCounter call");
      tick("QueryPerformanceCounter after the frame wait");
    }
  }
  return g_real_counter(out);
}

}  // namespace

void init() {
  g_main_thread = GetCurrentThreadId();
  g_first_event = CreateEventA(nullptr, TRUE, FALSE, nullptr);
  InitializeCriticalSection(&g_snap_lock);
  g_lock_ready = true;
}

bool install() {
  HMODULE exe = GetModuleHandleA(nullptr);
  if (void* prev = mem::iat_hook(exe, "KERNEL32.dll", "SetThreadAffinityMask", reinterpret_cast<void*>(&hk_affinity))) {
    g_real_affinity = reinterpret_cast<AffinityFn>(prev);
    g_affinity_slot = mem::iat_slot(exe, "KERNEL32.dll", "SetThreadAffinityMask");
  }
  if (void* prev = mem::iat_hook(exe, "KERNEL32.dll", "QueryPerformanceCounter", reinterpret_cast<void*>(&hk_counter))) {
    g_real_counter = reinterpret_cast<CounterFn>(prev);
    g_counter_slot = mem::iat_slot(exe, "KERNEL32.dll", "QueryPerformanceCounter");
  }
  if (!g_real_affinity && !g_real_counter) {
    logf("ERROR: dispatch: bio4.exe imports neither SetThreadAffinityMask nor QueryPerformanceCounter - no tick");
    return false;
  }
  logf("dispatch: bio4.exe's %s%s%s import%s hooked - the frame loop ticks the mod",
       g_real_affinity ? "SetThreadAffinityMask" : "", g_real_affinity && g_real_counter ? " and " : "",
       g_real_counter ? "QueryPerformanceCounter" : "", g_real_affinity && g_real_counter ? "s" : "");
  return true;
}

void uninstall() {
  if (g_affinity_slot && g_real_affinity &&
      mem::read<uintptr_t>(g_affinity_slot) == reinterpret_cast<uintptr_t>(&hk_affinity))
    mem::write<uintptr_t>(g_affinity_slot, reinterpret_cast<uintptr_t>(g_real_affinity));
  if (g_counter_slot && g_real_counter &&
      mem::read<uintptr_t>(g_counter_slot) == reinterpret_cast<uintptr_t>(&hk_counter))
    mem::write<uintptr_t>(g_counter_slot, reinterpret_cast<uintptr_t>(g_real_counter));
}

Snapshot snapshot() {
  if (!g_lock_ready) return Snapshot{};
  EnterCriticalSection(&g_snap_lock);
  const Snapshot s = g_snap;
  LeaveCriticalSection(&g_snap_lock);
  return s;
}

bool on_main_thread() { return GetCurrentThreadId() == g_main_thread; }
HANDLE first_tick_event() { return g_first_event; }

}  // namespace re4cc::dispatch
