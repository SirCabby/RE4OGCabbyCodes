#pragma once

#include <windows.h>

#include <cstdint>

#include "game.h"

// The save files, managed beside the game's load and save lists - the title's
// Load and a typewriter's Save: while the list is up the panel lists the twenty
// files, and can delete a save or copy one into another file, empty or not.
//
// RE4 keeps every save in one file, savegame00.sav, in Steam Cloud: the game
// reads it as it starts into a buffer of its own (0xFFAA0 bytes: a system part,
// then twenty files of 0xCAFC bytes) and writes the whole buffer back through
// Steam's ISteamRemoteStorage::FileWrite after every save (game.h). A change is
// therefore made to the game's buffer - the copy it would write back at its
// next save - and the buffer is then written the way the game writes it,
// through the game's own remote storage object (approved 2026-10-13), and read
// back to check. A copy of the file as it was is kept beside the mod first.
//
// The list shows a file by its "in use" dword in the buffer (+1E74), the
// screen's per-file status word and a table of "exists" bytes, so a delete
// clears all three (and the file's version, so nothing rescanning the buffer
// finds it), and a copy sets them as the source's. Changes are made only while
// the list is up and waiting for the player.
namespace re4cc::savefiles {

constexpr int kFiles = game::kFiles;

// One file, as the buffer holds it.
struct Save {
  bool known = false;     // the buffer could be read
  bool used = false;      // a save is in the file
  bool broken = false;    // the game shows it as damaged
  int play_time = 0;      // seconds
  int saves = 0;          // the save count the game's list shows (GLOBAL_WK's save_cnt as this save made it)
  int chapter = -1;       // GLOBAL_WK's chapter byte
  int difficulty = -1;
  int room = -1;          // stage << 8 | room
  int character = -1;
  int pesetas = 0;
  int year = 0, month = 0, day = 0, hour = 0, minute = 0;  // when it was saved
  uint32_t crc = 0;       // the game's own checksums of the file (head and whole): which save it is
};
// Two readings of a file are the same save: what a request checks before it acts.
bool same_save(const Save& a, const Save& b);
// "Chapter 2-1, 3:02:11, 12 saves, Normal - 2026-10-13 14:42" - or "no data".
const char* describe(const Save& s, char* out, size_t n);
const char* chapter_name(int chapter, char* out, size_t n);

// A copy of everything the panel draws, taken under the lock.
struct View {
  bool ready = false;    // the card and its buffer were found
  bool up = false;       // a load or save screen is up
  bool waiting = false;  // its list is up, waiting for the player: changes can be made
  int mode = -1;         // 0 load, 1 save
  int cursor = -1;       // the file the game's cursor is on
  Save save[kFiles];
  char note[240] = {};   // what the last change came to
  bool note_error = false;
};

void init();      // from DllMain: the lock
View view();      // any thread
bool list_up();   // main thread: the list is up, so the panel belongs on screen

// Requests from the panel, carried out on the next main-thread tick while the
// game's list waits. What the panel saw of the files goes with them: the
// buffer can change between the click and the tick, and changing a save that
// is not the one clicked is worse than not changing anything.
void request_delete(int file, const Save& seen);
void request_copy(int from, int to, const Save& from_seen, const Save& to_seen);

void tick(const game::Run& run);  // main thread, every tick

}  // namespace re4cc::savefiles
