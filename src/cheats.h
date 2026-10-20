#pragma once

#include "game.h"

// The cheats. Their switches are shared between the panel (it runs on the
// game's render thread) and the tick that applies them (the game's own frame,
// on the main thread), so the switches are plain atomics, edits travel through
// a small request queue, and everything that touches the game runs from tick().
namespace re4cc::cheats {

enum Kind : int {
  kGodMode = 0,      // the player takes no damage, and health is held at its maximum
  kAshleyGod,        // the same for the partner (Ashley)
  kOneHitKills,      // any hit that does damage takes all of an enemy's health
  kInfiniteAmmo,     // firing uses no rounds (nor arrows, nor bolts)
  kFreezePlaytime,
  kFreezeCountdown,  // the HUD's countdown: the Mercenaries' clock, and the main game's timed scenes
  kNoSaveCount,      // a save does not add to the save count (the one the load and save lists show)
  kInfiniteMoney,    // the merchant takes no pesetas for a purchase or a tune-up
  kNoMissCount,      // a shot that misses adds nothing to the shots fired (the results)
  kNoDeathCount,     // a death adds nothing to the chapter's or the game's deaths (the results)
  kAutoQte,          // QTEs pass by themselves: dodges at once, presses every frame, grabs shaken off
  kSteadyAim,        // the aim stays where it is put (no sway, no throw at a shot), and shots and throws go
                     // straight along it (no random spread)
  kAutoGallery,      // the shooting gallery plays itself for the most points: every Ganado in the head, Ashley
                     // never, the bonus target every time (field writes from the tick, no patch)
  kEndlessCombo,     // the Mercenaries: a combo never runs out of time, and what it earns is scored as it goes
  kAutoKill,         // the Mercenaries: every enemy dies as it appears, its kill scored as a shot's (field writes
                     // from the tick, no patch)
  kBonusTime,        // the Mercenaries: a Bonus Points time runs from the start and never runs out, and what the
                     // combo earns in it is scored as it goes
  kCount
};

// Every switch starts as the ini left it, and every change is written back to
// it, so the cheats stay as they were left from one launch to the next.
void init();  // from DllMain: the locks
const char* name(Kind k);
bool enabled(Kind k);
void set_enabled(Kind k, bool on);  // any thread; takes effect on the next tick
bool available(Kind k);             // its code sites were found (from discovery on)

// What the panel shows (built by tick, handed over under a lock).
struct Status {
  game::Run run;
  bool health_ok = false;
  game::Health health;
  bool counters_ok = false;
  game::Counters counters;
  int costume_set = -1;          // 0 Normal, 1 Special 1, 2 Special 2
  bool costume_pending = false;  // a set changed here, waiting for the next room to dress Leon in it
  game::Countdown countdown;
  bool gallery_ok = false;       // in the shooting gallery's room
  int gallery_state = -1, gallery_game = 0, gallery_score = 0, gallery_streak = 0;
  bool merc_ok = false;          // in the Mercenaries
  game::Merc merc;
  bool merc_waiting = false;     // Kill enemies as they appear waits for the stage to clear bodies away
  bool patched[kCount] = {};     // the switch's patches are in
  char last_action[120] = {};
};
Status status();

// Requests from the panel (applied on the next tick, on the main thread, and
// only while the game is paused).
void request_pesetas(int v);
void request_playtime(int seconds);
void request_countdown(int seconds);
void request_counter(game::Counter c, int v);
void request_saves(int v);
void request_costume_set(int v);
void request_difficulty(int v);

// Main thread.
void tick(const game::Run& run);
void remove_hooks();  // teardown: revert every patch

}  // namespace re4cc::cheats
