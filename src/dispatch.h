#pragma once

#include <windows.h>

#include "game.h"

// The tick: everything that reads or writes the game runs here, once a frame,
// on the game's own thread. bio4.exe runs its game on the main thread (the frame
// loop in WinMain) and draws on a render thread of its own, which is also the
// one that owns the window and calls PeekMessageA - so the siblings' message
// pump tick would be on the wrong thread here. The frame loop instead calls
// SetThreadAffinityMask twice a frame (a timing workaround around its
// QueryPerformanceCounter reads); the second call comes right after the frame
// wait, as the next frame starts, and that is where the tick runs: the exe's
// import of SetThreadAffinityMask is re-pointed at a hook that ticks when it is
// called from that one place (its return address, found by signature) and then
// does what it was asked. An import hook chains with any other tool's. The
// frame loop's next QueryPerformanceCounter call (only timing stores and an
// empty call between the two) is hooked the same way and ticks in a frame the
// first call did not come: a tool that replaces the frame wait's code (its own
// frame limiter) takes that call away with it.
namespace re4cc::dispatch {

void init();      // from DllMain: records the main thread, makes the locks
bool install();   // from DllMain: the import hook (does nothing until discovery has found the frame loop)
void uninstall();

struct Snapshot {
  bool ready = false;        // discovery found what the panel needs
  game::Run run;             // the run as this tick saw it
  bool card_list = false;    // a load or save list is up: the panel is the save file manager
  bool show_panel = false;   // the visibility rule, evaluated on the main thread
  unsigned long main_thread = 0;
  unsigned long long ticks = 0;
};
Snapshot snapshot();  // any thread

bool on_main_thread();
HANDLE first_tick_event();  // signalled once, from the first tick

}  // namespace re4cc::dispatch
