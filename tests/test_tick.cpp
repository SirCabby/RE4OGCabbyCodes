// The tick's two points (src/dispatch.cpp), outside the game, under Wine:
//
//   i686-w64-mingw32-g++ -std=c++20 -O2 -Wall -DWIN32_LEAN_AND_MEAN -DNOMINMAX -static -Isrc tests/test_tick.cpp
//       src/dispatch.cpp src/config.cpp src/crash.cpp tests/log_stub.cpp -o tests/build/test_tick.exe
//   wine tests/build/test_tick.exe
//
// The real dispatch.cpp hooks this exe's SetThreadAffinityMask and
// QueryPerformanceCounter imports, as it hooks bio4.exe's; the two return
// addresses the tick goes by (game::tick_return / tick_return_late, stubbed
// below) are two calls of this exe's own, playing the frame loop's: the
// SetThreadAffinityMask call after the frame wait, then the same frame's
// QueryPerformanceCounter call. The rest of what a tick does is stubbed (the
// game is not ready); a tick is counted by the snapshot it publishes. A frame
// must tick once: from the first point when it comes, from the second when it
// does not (another tool's frame limiter took it away), never twice, never
// from another thread or another call.
#include <windows.h>

#include <cstdio>
#include <cstring>

#include "cheats.h"
#include "crash.h"
#include "dispatch.h"
#include "game.h"
#include "input.h"
#include "inventory.h"
#include "overlay.h"
#include "savefiles.h"

namespace {
uintptr_t g_first = 0, g_second = 0;
}

// What a tick calls, with no game.
namespace re4cc::game {
bool ready() { return false; }
void trace_tick() {}
bool run(Run*) { return false; }
const char* mode_name(Mode) { return "?"; }
const char* character_name(int) { return "?"; }
uintptr_t tick_return() { return g_first; }
uintptr_t tick_return_late() { return g_second; }
}  // namespace re4cc::game
namespace re4cc::cheats {
void tick(const game::Run&) {}
}
namespace re4cc::savefiles {
void tick(const game::Run&) {}
bool list_up() { return false; }
}  // namespace re4cc::savefiles
namespace re4cc::inventory {
void tick(const game::Run&) {}
}
namespace re4cc::input {
void tick(bool) {}
}
namespace re4cc::overlay::dx9 {
void log_rates() {}
}

namespace {

int g_failures = 0;
volatile int g_sink = 0;

void check(bool ok, const char* what) {
  std::printf("  %s  %s\n", ok ? "ok  " : "FAIL", what);
  if (!ok) ++g_failures;
}

void** import_slot(HMODULE m, const char* dll, const char* fn) {
  auto* base = reinterpret_cast<BYTE*>(m);
  auto* nt = reinterpret_cast<IMAGE_NT_HEADERS*>(base + reinterpret_cast<IMAGE_DOS_HEADER*>(base)->e_lfanew);
  const IMAGE_DATA_DIRECTORY& dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
  for (auto* d = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(base + dir.VirtualAddress); d->Name; ++d) {
    if (_stricmp(reinterpret_cast<char*>(base + d->Name), dll) != 0) continue;
    auto* names = reinterpret_cast<IMAGE_THUNK_DATA*>(base + (d->OriginalFirstThunk ? d->OriginalFirstThunk : d->FirstThunk));
    auto* slots = reinterpret_cast<void**>(base + d->FirstThunk);
    for (int i = 0; names[i].u1.AddressOfData; ++i) {
      if (IMAGE_SNAP_BY_ORDINAL(names[i].u1.Ordinal)) continue;
      auto* by_name = reinterpret_cast<IMAGE_IMPORT_BY_NAME*>(base + names[i].u1.AddressOfData);
      if (std::strcmp(reinterpret_cast<char*>(by_name->Name), fn) == 0) return &slots[i];
    }
  }
  return nullptr;
}

// The frame loop's two calls (and another QueryPerformanceCounter call), each
// `call [import]` with work after it, so none is a tail jump - and noipa, so
// GCC's identical code folding cannot make the two counter calls one.
__attribute__((noipa)) void frame_wait_call() {
  SetThreadAffinityMask(GetCurrentThread(), 1);
  g_sink = g_sink + 1;
}
__attribute__((noipa)) void counter_call() {
  LARGE_INTEGER t;
  QueryPerformanceCounter(&t);
  g_sink = g_sink + static_cast<int>(t.LowPart & 1);
}
__attribute__((noipa)) void another_counter_call() {
  LARGE_INTEGER t;
  QueryPerformanceCounter(&t);
  g_sink = g_sink + 2 + static_cast<int>(t.LowPart & 3);
}

// Where `fn`'s `call [slot]` returns to.
uintptr_t return_of(void (*fn)(), void** slot) {
  const auto* p = reinterpret_cast<const BYTE*>(fn);
  for (int i = 0; i < 64; ++i)
    if (p[i] == 0xFF && p[i + 1] == 0x15 && *reinterpret_cast<void** const*>(p + i + 2) == slot)
      return reinterpret_cast<uintptr_t>(p + i + 6);
  return 0;
}

unsigned long long ticks() { return re4cc::dispatch::snapshot().ticks; }

void frames(int n, bool first_point) {
  for (int i = 0; i < n; ++i) {
    if (first_point) frame_wait_call();
    another_counter_call();
    counter_call();
    another_counter_call();
  }
}

DWORD WINAPI other_thread(LPVOID) {
  frames(5, true);
  return 0;
}

}  // namespace

int main() {
  std::printf("the tick's two points (the frame loop's SetThreadAffinityMask, then QueryPerformanceCounter):\n");
  HMODULE exe = GetModuleHandleA(nullptr);
  void** affinity = import_slot(exe, "kernel32.dll", "SetThreadAffinityMask");
  void** counter = import_slot(exe, "kernel32.dll", "QueryPerformanceCounter");
  g_first = return_of(&frame_wait_call, affinity);
  g_second = return_of(&counter_call, counter);
  check(affinity && counter && g_first && g_second &&
            reinterpret_cast<void*>(&counter_call) != reinterpret_cast<void*>(&another_counter_call),
        "the two calls' return addresses found (and the other counter call is another function)");
  if (!g_first || !g_second) return 1;
  re4cc::dispatch::init();
  check(re4cc::dispatch::install(), "both imports hooked");
  unsigned long long t = ticks();
  frames(10, true);
  check(ticks() - t == 10, "a frame as the game has it: one tick, from the first point (10 frames, 10 ticks)");
  t = ticks();
  frames(10, false);
  check(ticks() - t == 10, "the first call gone (a frame limiter of another tool's): one tick a frame, from the second");
  t = ticks();
  frames(10, true);
  check(ticks() - t == 10, "the first call back: one tick a frame, never two");
  t = ticks();
  another_counter_call();
  frame_wait_call();
  frame_wait_call();
  check(ticks() - t == 2, "other QueryPerformanceCounter calls tick nothing; each first point ticks");
  counter_call();
  t = ticks();
  HANDLE th = CreateThread(nullptr, 0, other_thread, nullptr, 0, nullptr);
  WaitForSingleObject(th, 5000);
  CloseHandle(th);
  check(ticks() - t == 0, "the frame loop's calls from another thread tick nothing");
  g_second = 0;  // the second point not found by discovery
  t = ticks();
  frames(10, true);
  check(ticks() - t == 10, "without the second point: one tick a frame from the first");
  frames(3, false);
  check(ticks() - t == 10, "... and none with neither");
  re4cc::dispatch::uninstall();
  check(*affinity != nullptr && *counter != nullptr && return_of(&frame_wait_call, affinity) == g_first,
        "both imports given back");
  t = ticks();
  g_second = return_of(&counter_call, counter);
  frames(3, true);
  check(ticks() - t == 0, "unhooked: no tick");
  std::printf("%s (%d failure(s))\n", g_failures ? "FAILED" : "PASSED", g_failures);
  return g_failures ? 1 : 0;
}
