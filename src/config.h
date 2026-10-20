#pragma once

namespace re4cc::config {

// One value the Watch diagnostic logs whenever it changes (reverse engineering
// aid only: the addresses are the user's, typed into the ini as offsets from
// bio4.exe's base - the exe moves with ASLR on Windows; nothing the mod does
// depends on them).
struct Watch {
  unsigned rva = 0;
  unsigned size = 0;  // 1, 2 or 4 bytes
};
constexpr int kMaxWatch = 24;

struct Settings {
  int toggle_key = 0x76;  // VK_F7: hide/show the panel while it is up
  // Each cheat's switch. The panel changes them, and every change is written
  // back to the ini (save_cheats), so they come back on the next launch.
  bool god_mode = false;
  bool ashley_god = false;       // Ashley takes no damage
  bool one_hit_kills = false;
  bool infinite_ammo = false;
  bool freeze_playtime = false;
  bool freeze_countdown = false;
  bool no_save_count = false;    // a save does not add to the save count
  bool infinite_money = false;   // the merchant takes no pesetas
  bool no_miss_count = false;    // a missed shot is not counted (the results)
  bool no_death_count = false;   // a death is not counted (the results)
  bool auto_qte = false;         // QTEs pass by themselves
  bool steady_aim = false;       // the aim does not sway, and shots do not spread
  bool auto_gallery = false;     // the shooting gallery plays itself for the most points
  bool endless_combo = false;    // the Mercenaries: a combo never runs out of time
  bool auto_kill = false;        // the Mercenaries: every enemy is killed as it appears
  bool bonus_time = false;       // the Mercenaries: a Bonus Points time always runs
  bool trace = false;        // verbose diagnostics
  bool always_show = false;  // debug: draw the panel everywhere, not only on its screens
  int nwatch = 0;            // "Watch = 0x846F9C:2,..." - log those values as they change (offsets from the exe's base)
  Watch watch[kMaxWatch];
  // "Disable = overlay,tick,game,input" turns whole subsystems off so a fault
  // can be bisected to one of them.
  bool disable_overlay = false;
  bool disable_tick = false;
  bool disable_game = false;
  bool disable_input = false;
};

const Settings& get();

// RE4OGCabbyCodes.ini next to the DLL. Written with documented defaults the
// first time so the options are discoverable.
void load(const char* dir);
const char* dir();  // the DLL's directory, with a trailing separator

// The cheat switches as they are now, written into the ini: each one's line
// rewritten in place (or added, for an ini written before it existed), every
// other line and comment kept as it was.
struct Cheats {
  bool god_mode, ashley_god, one_hit_kills, infinite_ammo, freeze_playtime, freeze_countdown, no_save_count,
      infinite_money, no_miss_count, no_death_count, auto_qte, steady_aim, auto_gallery, endless_combo, auto_kill,
      bonus_time;
};
bool save_cheats(const Cheats& c);

}  // namespace re4cc::config
