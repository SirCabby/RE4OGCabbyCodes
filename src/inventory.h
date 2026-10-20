#pragma once

#include "game.h"
#include "itembox.h"

// The attache case and the key items, edited from the panel, and the item
// box's Store and Take. RE4's case is a grid of pieces, so nothing goes in
// by writing a slot: an item is added the way a pickup is - the game's own
// PutInCase finds it a place (topping up the pieces of it already there first,
// turning a new one if it has to) or says there is no room - and taken out by
// the game's own cItemMgr::erase, which also unequips a weapon going out. Key
// items, files and treasures, which live off the grid, come in through
// cItemMgr::get, as a pickup of them does. Those three calls are the only game
// functions the mod calls (approved 2026-10-13); what an item holds - its
// count, a weapon's loaded rounds and upgrade levels - is a field write.
// Everything is done on the main thread, while the game is paused.
namespace re4cc::inventory {

struct Row {
  int index = -1;     // in the game's item array
  int id = 0;
  int num = 0;
  int stack_max = 1;  // how many one piece / entry holds (the game's own number, by difficulty)
  bool equipped = false;
  bool onboard = false;  // placed on the case's grid
  bool weapon = false;   // has a magazine and upgrade levels
  int ammo = 0;          // loaded rounds
  int capacity = -1;     // rounds a full magazine holds at its capacity level; 0x8000 infinite; -1 none
  int levels[4] = {};    // firepower, firing speed, reload speed, capacity (1-based)
  int max_levels[4] = {};  // the most each goes to, the exclusive upgrade included
};

constexpr int kMaxCase = 96, kMaxKey = 192;

struct View {
  bool known = false;     // the item manager was found
  bool editable = false;  // the game is paused and the case's calls were found
  char why[120] = {};     // ...or why not
  int character = -1;     // whose items (the game's character in play)
  int case_size = -1;     // 0 S, 1 M, 2 L, 3 XL
  int difficulty = -1;
  int ncase = 0;
  Row in_case[kMaxCase];  // the case's items (weapons, ammo, grenades, recovery, parts)
  int nkey = 0;
  Row key[kMaxKey];       // key items, files, maps, treasures
  char note[160] = {};    // what the last change came to
  bool note_error = false;
};

void init();  // from DllMain: the locks
View view();  // any thread

// Requests from the panel (applied on the next tick, only while paused). `seen`
// is the row as the panel showed it: an item that has changed since is left
// alone.
void request_count(const Row& seen, int num);
void request_weapon(const Row& seen, const int levels[4], int ammo);
void request_remove(const Row& seen);
void request_add(int id, int num);
// The item box (itembox.h) of the game being played (box_of): the whole of a
// piece of the case into the box (the key items, files and treasures take no
// room in the case and do not go in); out of it, one piece's worth of an item at
// a time (the box's own count has no limit), or a weapon; an entry's count set
// (not a weapon's: it is one); and an entry thrown away.
void request_store(const Row& seen);
void request_take(int box_index, const itembox::Entry& seen);
void request_box_count(int box_index, const itembox::Entry& seen, int num);
void request_discard(int box_index, const itembox::Entry& seen);
// The item box a game uses: each game that saves has its own, the main game's
// and Separate Ways' (the user, 2026-10-17). Assignment Ada and the Mercenaries,
// played in one sitting with no saves, have none (false): the panel shows
// neither the box nor Store there, and the tick refuses a box request that
// comes all the same.
bool box_of(game::Mode m, itembox::Box* out);

void tick(const game::Run& run);  // main thread

// Whether an item comes in through the case's placement (the rest - key items,
// maps, files, treasures - come in off the grid). Any thread: it reads the
// exe's constant item tables only.
bool goes_in_case(int id);

// The levels word a weapon keeps (level - 1 in four nibbles) and back.
int levels_word(const int levels[4]);
void levels_of(int word, int out[4]);

}  // namespace re4cc::inventory
