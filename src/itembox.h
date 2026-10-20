#pragma once

#include <vector>

// The item box: an item box of the mod's own, which RE4 does not have, kept
// outside the game in a file beside the mod, as RE0CabbyCodes keeps its
// storage. Each game that saves has a box of its own (the user, 2026-10-17):
// the main game's (RE4OGCabbyCodes.itembox.ini) and Separate Ways'
// (RE4OGCabbyCodes.itembox.separate-ways.ini); Assignment Ada and the
// Mercenaries, played in one sitting with no saves, have none (inventory.h's
// box_of says which box a game uses). A box is one for every save file and
// every new game of its game, and nothing the game does changes it: not a save,
// a load, a new game, nor a death and its continue. Only the panel's Store,
// Take, count and throw away change it, and each change is written to the
// box's file before it counts, so the file always holds the box as it is.
//
// It has no limit. Everything of one item is one entry, however much of it is
// stored - its counts are added up, with no stack size, and the panel can set
// one to any count - except a weapon, whose upgrade levels and loaded rounds
// are a state of its own (items::keeps_state): it keeps an entry of its own, as
// it keeps a piece of its own in the case, and has no count to set. Nothing
// else has levels or rounds, whatever its cItem holds there (what the entry's
// last occupant left), so the box never keeps them for anything else - not from
// a Store, nor from a line of the file. A box keeps itself in the panel's order
// (the Add list's: bank, group, the group's own order; one item's weapons by
// level, then rounds), so an index the panel draws is the box's own.
//
// Each box is read from its file as the mod loads. If a file is there but
// cannot be read, that box is left alone for the run: nothing is written over a
// file that could not be read.
namespace re4cc::itembox {

// The boxes.
enum class Box { kMain, kSeparateWays };
constexpr int kBoxes = 2;

// The most of one item an entry counts. The box has no limit of the game's:
// this only keeps a count that is typed, added up or read from the file within
// what makes sense.
constexpr int kMaxCount = 999999999;

struct Entry {
  int id = 0;
  int num = 0;          // how many, 1..kMaxCount (a weapon: its own count, 1)
  bool weapon = false;  // one entry each, with its state below (only an item that keeps one: items::keeps_state)
  int levels = 0;       // its levels word: level - 1 in four nibbles (firepower, firing speed, reload, capacity)
  int ammo = 0;         // its loaded rounds
};
bool same(const Entry& a, const Entry& b);

// A copy of a box for the panel, taken under the lock.
struct View {
  Box which = Box::kMain;
  bool usable = false;       // read (or not there yet): the box can change
  bool write_failed = false; // the last change could not be written (see the log)
  std::vector<Entry> entry;  // in the panel's order
};

void init(const char* dir);  // from DllMain: the lock, and every box read from its file
View view(Box b);            // any thread
const char* file_name(Box b);  // "RE4OGCabbyCodes.itembox.ini", "RE4OGCabbyCodes.itembox.separate-ways.ini"

// A box's changes, main thread only (inventory.cpp, which moves the items in
// and out of the game). Each writes the box's file before it returns.
//   put      `e` into the box (its count onto the item's entry, or a weapon's own
//            entry; levels and rounds given for an item that keeps none are
//            dropped). false: the file could not be written, and the box is as
//            it was - so the item must stay where it is.
//   unput    takes back what a put put in (a store the game then refused).
//   take     `num` out of the entry at `index`, which must still be `seen` (a
//            weapon's entry goes whole). The item has already gone into the game,
//            so this always takes it out of the box; false only when the entry
//            is not there (or not `seen`) any more, or the file could not be
//            written - the box in memory is right, the file a change behind.
//   set_count  the count of the entry at `index` (still `seen`) set to `num`,
//            1..kMaxCount. A weapon's entry is one weapon, with no count to set.
//            false: it is not there any more, it is a weapon's, or the file
//            could not be written and the count is as it was.
//   discard  throws the entry at `index` (still `seen`) away. false: it is not
//            there any more, or the file could not be written and it is kept.
bool put(Box b, const Entry& e);
void unput(Box b, const Entry& e);
bool take(Box b, int index, const Entry& seen, int num);
bool set_count(Box b, int index, const Entry& seen, int num);
bool discard(Box b, int index, const Entry& seen);

}  // namespace re4cc::itembox
