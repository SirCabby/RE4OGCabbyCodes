// The item box (src/itembox.cpp) and the panel's Store / Take / count / throw away
// (src/inventory.cpp), run against a fake attache case, under Wine:
//
//   i686-w64-mingw32-g++ -std=c++20 -O2 -DWIN32_LEAN_AND_MEAN -DNOMINMAX -static -Isrc
//       tests/test_itembox.cpp src/itembox.cpp src/inventory.cpp tests/log_stub.cpp -o tests/build/test_itembox.exe
//   wine tests/build/test_itembox.exe 'Z:\some\scratch\dir'
//
// The fake game below does what the game's own functions were read to do
// (CLAUDE.md, "Items"): PutInCase tops up the pieces of an item first and puts
// the rest in a new piece, all or nothing; cItemMgr::get puts a treasure or a
// gem onto the entry held (dropping what goes past it) and takes a file held
// already as done; a new entry is set up as FUN_00707780 does it - a weapon's
// levels and a full magazine, a part not attached - and anything else keeps
// the levels word and rounds its entry's last occupant left there. Each phase
// runs in a process of its own (the box keeps static state, and is read once,
// as the mod loads): the test starts itself once per phase over the same
// directory.
#include <windows.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "game.h"
#include "inventory.h"
#include "itembox.h"
#include "items.h"

using namespace re4cc;

static int g_fail = 0;
#define CHECK(cond, ...)                 \
  do {                                   \
    if (!(cond)) {                       \
      ++g_fail;                          \
      std::printf("FAIL: " __VA_ARGS__); \
      std::printf("  [line %d]\n", __LINE__); \
    }                                    \
  } while (0)

// --- the fake game ------------------------------------------------------------------------------------
namespace {
struct FakeItem {
  bool used = false;
  int id = 0, num = 0, weapon = 0, ammo = 0;
};
std::vector<FakeItem> g_case(64);
int g_free_pieces = 8;    // room for this many new pieces on the grid
int g_erase_fails = 0;    // the next erase(s) refuse
int g_weapon_writes = 0;  // set_item_weapon calls that changed an entry

int type_of(int id) { return items::counts(items::kItems[id].kind, 5).type; }
int max_of(int id) {
  const int t = type_of(id);
  if (t == items::kWeapon || t == items::kPart) return 1;
  const int m = items::counts(items::kItems[id].kind, 5).max;
  return m > 0 ? m : 1;
}
// A new entry in the first free one, set up as the game's FUN_00707780 does: a
// weapon gets its levels word (level 1) and a full magazine, a part "not
// attached" (+6 0, +8 0xFFFF - read as 0x1FFF rounds), and anything else keeps
// what the entry's last occupant left there (erase only clears the in-use bit).
constexpr int kNotAttached = 0xFFFF >> 3;
int add(int id, int num) {
  for (int i = 0; i < static_cast<int>(g_case.size()); ++i)
    if (!g_case[i].used) {
      FakeItem& f = g_case[i];
      f.used = true;
      f.id = id;
      f.num = num;
      if (type_of(id) == items::kWeapon) f.weapon = 0, f.ammo = 10;
      else if (type_of(id) == items::kPart) f.weapon = 0, f.ammo = kNotAttached;
      return i;
    }
  return -1;
}
int carried(int id) {
  int n = 0;
  for (const FakeItem& f : g_case)
    if (f.used && f.id == id) n += f.num;
  return n;
}
}  // namespace

namespace re4cc::game {
bool counters(Counters* out) {
  *out = Counters{};
  out->difficulty = 5;
  return true;
}
int items(Item* out, int max) {
  int k = 0;
  for (int i = 0; i < static_cast<int>(g_case.size()) && k < max; ++i)
    if (g_case[i].used) {
      Item& o = out[k++];
      o = Item{};
      o.index = i;
      o.id = g_case[i].id;
      o.num = g_case[i].num;
      o.weapon = g_case[i].weapon;
      o.ammo = g_case[i].ammo;
      o.onboard = items::in_case(o.id);
    }
  return k;
}
bool items_known() { return true; }
bool item_rules_known() { return true; }
bool case_calls_known() { return true; }
int item_character() { return 0; }
int case_size() { return 1; }
int item_kind(int id) { return id >= 0 && id < items::kCount ? items::kItems[id].kind : -1; }
int charge(int id, int) { return type_of(id) == items::kWeapon ? 10 : -1; }
int max_level(int id, int) { return type_of(id) == items::kWeapon ? 5 : 0; }
bool set_item_num(int index, int id, int num) {
  if (!g_case[index].used || g_case[index].id != id) return false;
  g_case[index].num = num;
  return true;
}
bool set_item_weapon(int index, int id, int levels, int ammo) {
  if (index < 0 || !g_case[index].used || g_case[index].id != id) return false;
  g_case[index].weapon = levels;
  g_case[index].ammo = ammo;
  ++g_weapon_writes;
  return true;
}
bool item_erase(int index, int id) {
  if (g_erase_fails > 0) {
    --g_erase_fails;
    return false;
  }
  if (!g_case[index].used || g_case[index].id != id) return false;
  g_case[index].used = false;
  return true;
}
bool put_in_case(int id, int num, int* new_index) {
  *new_index = -1;
  const int max = max_of(id);
  if (num > max) num = max;
  int room = 0;
  for (const FakeItem& f : g_case)
    if (f.used && f.id == id) room += max - f.num;
  if (num <= room) {
    for (FakeItem& f : g_case)
      if (f.used && f.id == id && num > 0) {
        const int put = max - f.num < num ? max - f.num : num;
        f.num += put;
        num -= put;
      }
    return true;
  }
  if (g_free_pieces <= 0) return false;
  --g_free_pieces;
  for (FakeItem& f : g_case)
    if (f.used && f.id == id) {
      num -= max - f.num;
      f.num = max;
    }
  *new_index = add(id, num);
  return true;
}
bool item_get(int id, int num, int* new_index) {
  *new_index = -1;
  const int t = type_of(id), max = max_of(id);
  for (FakeItem& f : g_case)
    if (f.used && f.id == id) {
      if (t == items::kTreasure || t == items::kGem) {
        f.num = f.num + num <= max ? f.num + num : max;  // the game drops the rest
        return true;
      }
      if (t == items::kFile || t == items::kBottleCap) return true;  // held: nothing added
    }
  *new_index = add(id, num < max ? num : max);
  return *new_index >= 0;
}
}  // namespace re4cc::game

// --- helpers -----------------------------------------------------------------------------------------
namespace {
std::string g_dir;
game::Mode g_mode = game::Mode::kMain;          // the game the ticks are in
itembox::Box g_box = itembox::Box::kMain;        // the box the helpers below look at
itembox::View box() { return itembox::view(g_box); }

std::string path(const char* name) { return g_dir + name; }
std::string read_all(const std::string& p) {
  std::string s;
  if (FILE* f = std::fopen(p.c_str(), "rb")) {
    char b[4096];
    for (size_t n; (n = std::fread(b, 1, sizeof(b), f)) > 0;) s.append(b, n);
    std::fclose(f);
  }
  return s;
}
void write_all(const std::string& p, const std::string& s) {
  FILE* f = std::fopen(p.c_str(), "wb");
  std::fwrite(s.data(), 1, s.size(), f);
  std::fclose(f);
}
bool exists(const std::string& p) { return GetFileAttributesA(p.c_str()) != INVALID_FILE_ATTRIBUTES; }

void tick() {
  game::Run run;
  run.known = run.in_game = run.paused = true;
  run.routine = 6;
  run.mode = g_mode;
  inventory::tick(run);
}
// The panel's row of an item in the case or the key items.
bool row_of(int id, inventory::Row* out) {
  const inventory::View v = inventory::view();
  for (int i = 0; i < v.ncase; ++i)
    if (v.in_case[i].id == id) return *out = v.in_case[i], true;
  for (int i = 0; i < v.nkey; ++i)
    if (v.key[i].id == id) return *out = v.key[i], true;
  return false;
}
int box_index(int id, bool weapon = false) {
  const itembox::View b = box();
  for (int i = 0; i < static_cast<int>(b.entry.size()); ++i)
    if (b.entry[i].id == id && b.entry[i].weapon == weapon) return i;
  return -1;
}
int box_num(int id) {
  const int i = box_index(id);
  return i < 0 ? 0 : box().entry[i].num;
}
void store(int id) {
  inventory::Row r;
  CHECK(row_of(id, &r), "%s is not in the case to store", items::name(id));
  inventory::request_store(r);
  tick();
}
void take(int id, bool weapon = false) {
  const int i = box_index(id, weapon);
  CHECK(i >= 0, "%s is not in the box to take", items::name(id));
  if (i >= 0) inventory::request_take(i, box().entry[i]);
  tick();
}
void set_count(int id, int num, bool weapon = false) {
  const int i = box_index(id, weapon);
  CHECK(i >= 0, "%s is not in the box to count", items::name(id));
  if (i >= 0) inventory::request_box_count(i, box().entry[i], num);
  tick();
}
const char* note() {
  static inventory::View v;
  v = inventory::view();
  return v.note;
}

// Lets go of a file held by the test, a moment later (a virus scanner's look at it).
DWORD WINAPI release_later(LPVOID file) {
  Sleep(250);
  CloseHandle(file);
  return 0;
}

// --- the phases --------------------------------------------------------------------------------------
// 1: a file written by hand (CRLF, a duplicate, nonsense lines) read, then
//    every kind of move.
void phase_main() {
  write_all(path("RE4OGCabbyCodes.itembox.ini"),
            "# a note\r\n"
            "4:100   ; Handgun Ammo\r\n"
            "4:30\r\n"
            "35:1:0x1111:13 ; Handgun\r\n"
            "this is not an entry\r\n"
            "999:5\r\n"
            "0:7\r\n"
            "35:1:3\r\n"
            "\r\n");
  const std::string original = read_all(path("RE4OGCabbyCodes.itembox.ini"));
  itembox::init(g_dir.c_str());
  inventory::init();
  itembox::View b = box();
  CHECK(b.usable && !b.write_failed, "the file was not read as usable");
  CHECK(b.entry.size() == 3, "%d entries read, expected 3 (Magnum Ammo, Handgun Ammo merged, Handgun)",
        static_cast<int>(b.entry.size()));
  CHECK(box_num(4) == 130, "Handgun Ammo x%d, expected the two lines merged to 130", box_num(4));
  CHECK(box_num(0) == 7, "Magnum Ammo (id 0) x%d, expected 7", box_num(0));
  const int hg = box_index(35, true);
  CHECK(hg >= 0 && b.entry[hg].levels == 0x1111 && b.entry[hg].ammo == 13, "the Handgun's levels and rounds");
  // The panel's order: the Add list's rank (the Handgun heads it, the ammo after).
  for (size_t i = 1; i < b.entry.size(); ++i)
    CHECK(items::kItems[b.entry[i - 1].id].rank <= items::kItems[b.entry[i].id].rank, "entry %d out of order",
          static_cast<int>(i));

  // A case to move things in and out of.
  const int ammo_piece = add(4, 20);
  const int red9 = add(37, 1);
  g_case[red9].weapon = 0x2101;
  g_case[red9].ammo = 8;
  add(87, 5);  // Spinel (a gem, off the grid)
  add(72, 1);  // a file
  tick();

  // Store: a stack onto the item's count, a weapon an entry of its own.
  store(4);
  CHECK(box_num(4) == 150 && !g_case[ammo_piece].used, "Handgun Ammo stored: x%d in the box", box_num(4));
  CHECK(exists(path("RE4OGCabbyCodes.itembox.ini.bak")) &&
            read_all(path("RE4OGCabbyCodes.itembox.ini.bak")) == original,
        "the file with skipped lines was not kept as .bak before its first rewrite");
  store(37);
  const int r9 = box_index(37, true);
  CHECK(r9 >= 0 && box().entry[r9].levels == 0x2101 && box().entry[r9].ammo == 8,
        "the Red9 lost its state in the box");
  // What lives off the grid takes no room in the case: Store refuses it.
  store(87);
  CHECK(box_num(87) == 0 && carried(87) == 5, "Spinel (off the grid) was stored: x%d in the box", box_num(87));
  CHECK(std::strstr(note(), "takes no room"), "the note does not say why: %s", note());
  {
    const std::string f = read_all(path("RE4OGCabbyCodes.itembox.ini"));
    CHECK(f.find("4:150 ") != std::string::npos && f.find("37:1:0x2101:8 ") != std::string::npos &&
              f.find("; Red9 - 8 rounds loaded") != std::string::npos,
          "the file does not hold what the box does:\n%s", f.c_str());
  }
  // A gem in the box all the same (a file written by hand): Take still never
  // hands the game more than it keeps.
  for (FakeItem& f : g_case)
    if (f.used && f.id == 87) f.used = false;
  itembox::Entry gem;
  gem.id = 87;
  gem.num = 5;
  CHECK(itembox::put(g_box, gem), "put of Spinel");

  // Take: one piece's worth (50 on Normal), the rest stays.
  take(4);
  CHECK(box_num(4) == 100 && carried(4) == 50, "Take of Handgun Ammo: box x%d, case x%d (expected 100 / 50)",
        box_num(4), carried(4));
  // No room for a new piece and a full one: nothing moves.
  g_free_pieces = 0;
  take(4);
  CHECK(box_num(4) == 100 && carried(4) == 50, "a Take with no room moved something: box x%d, case x%d", box_num(4),
        carried(4));
  CHECK(std::strstr(note(), "stays in the item box"), "the note does not say it stayed: %s", note());
  // No room for a new piece, but the piece carried has room for 20: just those.
  for (FakeItem& f : g_case)
    if (f.used && f.id == 4) f.num = 30;
  take(4);
  CHECK(box_num(4) == 80 && carried(4) == 50, "the top-up Take: box x%d, case x%d (expected 80 / 50)", box_num(4),
        carried(4));
  g_free_pieces = 8;
  // A weapon comes back with its levels and rounds.
  take(37, true);
  int back = -1;
  for (int i = 0; i < static_cast<int>(g_case.size()); ++i)
    if (g_case[i].used && g_case[i].id == 37) back = i;
  CHECK(back >= 0 && g_case[back].weapon == 0x2101 && g_case[back].ammo == 8, "the Red9 came back without its state");
  CHECK(box_index(37, true) < 0, "the Red9 is still in the box");
  // A gem held: only what its entry still holds (the game would drop the rest).
  add(87, 997);
  take(87);
  CHECK(carried(87) == 999 && box_num(87) == 3, "Spinel: case x%d, box x%d (expected 999 / 3)", carried(87),
        box_num(87));
  take(87);
  CHECK(box_num(87) == 3, "a Take onto a full Spinel moved something");
  // A file held already: refused (the game would take it as done and add nothing).
  itembox::Entry file;
  file.id = 72;
  file.num = 1;
  CHECK(itembox::put(g_box, file), "put of a file");
  take(72);
  CHECK(box_num(72) == 1 && carried(72) == 1, "a file held already was taken: box x%d", box_num(72));

  // The case's size is no item: removing the Attache Case S is refused; a file is removed.
  const int case_s = add(124, 1);
  tick();
  {
    inventory::Row r;
    CHECK(row_of(124, &r), "the Attache Case S is not in the view");
    inventory::request_remove(r);
    tick();
    CHECK(g_case[case_s].used, "the Attache Case S was removed");
    CHECK(row_of(72, &r), "the file is not in the view");
    inventory::request_remove(r);
    tick();
    CHECK(carried(72) == 0 && std::strstr(note(), "removed"), "the file was not removed: %s", note());
    store(124);
    CHECK(g_case[case_s].used && box_index(124) < 0, "the Attache Case S was stored");
  }

  // Throw away.
  const int mi = box_index(0);
  inventory::request_discard(mi, box().entry[mi]);
  tick();
  CHECK(box_index(0) < 0, "Magnum Ammo was not thrown away");
  // A request for an entry that has changed since the panel drew it does nothing.
  itembox::Entry stale = box().entry[box_index(4)];
  stale.num += 1;
  inventory::request_take(box_index(4), stale);
  tick();
  CHECK(box_num(4) == 80, "a stale Take moved something");

  // A count set: any number, with no piece's limit, and into the file at once.
  set_count(87, 2500);
  CHECK(box_num(87) == 2500 && carried(87) == 999, "Spinel's count set: box x%d (2500), case x%d (999)", box_num(87),
        carried(87));
  CHECK(read_all(path("RE4OGCabbyCodes.itembox.ini")).find("\n87:2500 ") != std::string::npos,
        "the file does not hold the count set");
  // Held to 1..kMaxCount (x throws an entry away).
  set_count(87, 0);
  CHECK(box_num(87) == 1, "a count of 0 set Spinel to x%d, expected 1", box_num(87));
  set_count(87, 2000000000);
  CHECK(box_num(87) == itembox::kMaxCount, "a count past the most set Spinel to x%d, expected %d", box_num(87),
        itembox::kMaxCount);
  // An entry that has changed since the panel drew it, or a weapon (one, with
  // no count): nothing is done.
  stale = box().entry[box_index(87)];
  stale.num = 3;
  inventory::request_box_count(box_index(87), stale, 7);
  tick();
  CHECK(box_num(87) == itembox::kMaxCount && std::strstr(note(), "changed before"), "a stale count: x%d, %s",
        box_num(87), note());
  set_count(35, 5, true);
  CHECK(box().entry[box_index(35, true)].num == 1 && std::strstr(note(), "no count"),
        "the Handgun's count: x%d, %s", box().entry[box_index(35, true)].num, note());
  // A file that cannot be written: the count stays as it was, and the box says so.
  CreateDirectoryA(path("RE4OGCabbyCodes.itembox.ini.tmp").c_str(), nullptr);
  set_count(87, 42);
  CHECK(box_num(87) == itembox::kMaxCount && box().write_failed && std::strstr(note(), "could not be written"),
        "a count whose file could not be written: x%d, %s", box_num(87), note());
  RemoveDirectoryA(path("RE4OGCabbyCodes.itembox.ini.tmp").c_str());
  set_count(87, 4321);
  CHECK(box_num(87) == 4321 && !box().write_failed, "the count after it failed: x%d", box_num(87));
  // The count it already has: nothing to write (it would fail here).
  CreateDirectoryA(path("RE4OGCabbyCodes.itembox.ini.tmp").c_str(), nullptr);
  set_count(87, 4321);
  CHECK(box_num(87) == 4321 && !box().write_failed, "a count set to what it was rewrote the file");
  RemoveDirectoryA(path("RE4OGCabbyCodes.itembox.ini.tmp").c_str());
  // The file held as a virus scanner or the search indexer may hold it on
  // Windows (no sharing but reading): held for good, the count stays as it was;
  // held for a moment, the replace is tried again until it lets go.
  HANDLE held = CreateFileA(path("RE4OGCabbyCodes.itembox.ini").c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                            OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  CHECK(held != INVALID_HANDLE_VALUE, "the box's file could not be held (error %lu)", GetLastError());
  set_count(87, 77);
  CHECK(box_num(87) == 4321 && box().write_failed && std::strstr(note(), "could not be written"),
        "a count whose file stayed held: x%d, %s", box_num(87), note());
  HANDLE releaser = CreateThread(nullptr, 0, release_later, held, 0, nullptr);
  set_count(87, 77);
  WaitForSingleObject(releaser, 5000);
  CloseHandle(releaser);
  CHECK(box_num(87) == 77 && !box().write_failed, "a count whose file was held for a moment: x%d", box_num(87));
  set_count(87, 4321);
  // Take hands over one piece's worth of the count set.
  set_count(4, 500);
  take(4);
  CHECK(box_num(4) == 450 && carried(4) == 100, "a Take after a count set: box x%d, case x%d (expected 450 / 100)",
        box_num(4), carried(4));

  // A store the game refuses: the box gives it back.
  const int sprays = add(5, 1);
  tick();
  g_erase_fails = 1;
  store(5);
  CHECK(g_case[sprays].used && box_num(5) == 0, "a refused store left First Aid Spray in the box (x%d)", box_num(5));
  // A file that cannot be written: the store is not made.
  CreateDirectoryA(path("RE4OGCabbyCodes.itembox.ini.tmp").c_str(), nullptr);
  store(5);
  CHECK(g_case[sprays].used && box_num(5) == 0 && box().write_failed,
        "a store whose file could not be written was made");
  RemoveDirectoryA(path("RE4OGCabbyCodes.itembox.ini.tmp").c_str());
  store(5);
  CHECK(!g_case[sprays].used && box_num(5) == 1 && !box().write_failed, "the store after it failed");
  std::printf("main: box ends with %d entries\n", static_cast<int>(box().entry.size()));
}

// 2: a new start reads back exactly the box the last one left.
void phase_reread() {
  itembox::init(g_dir.c_str());
  const itembox::View b = box();
  CHECK(b.usable, "the file written by the last run was not read");
  CHECK(box_num(4) == 450 && box_num(87) == 4321 && box_num(72) == 1 && box_num(5) == 1 && box_index(35, true) >= 0 &&
            box_index(0) < 0 && box_index(37, true) < 0,
        "the box read back is not the one written");
  CHECK(b.entry.size() == 5, "%d entries read back, expected 5", static_cast<int>(b.entry.size()));
  CHECK(!exists(path("RE4OGCabbyCodes.itembox.ini.tmp")), "a .tmp was left behind");
}

// The lines of the box's file that give levels and rounds, for items that keep
// none (items::keeps_state): there should be none.
int stray_state_lines(const std::string& file) {
  int n = 0;
  size_t at = 0;
  while (at < file.size()) {
    size_t end = file.find('\n', at);
    if (end == std::string::npos) end = file.size();
    std::string line = file.substr(at, end - at);
    at = end + 1;
    line.resize(std::min(line.find_first_of(";#"), line.size()));
    int id = -1, num = 0, ammo = 0;
    unsigned levels = 0;
    if (std::sscanf(line.c_str(), "%d:%d:%x:%d", &id, &num, &levels, &ammo) == 4 && !items::keeps_state(id)) {
      std::printf("  a line of levels and rounds for %s: %s\n", items::name(id), line.c_str());
      ++n;
    }
  }
  return n;
}

// 3: what a Store before 2026-10-15 could leave in the file - ammo, herbs,
// grenades, a launcher and an attachment stored with "levels and rounds", each
// what another item had left in its entry (the user's own box held these
// lines) - is read back as counts, and kept as .bak before the file is first
// rewritten. A Store now never takes such leftovers for a state, and a Take
// never writes any onto the item it brings in.
void phase_leftovers() {
  write_all(path("RE4OGCabbyCodes.itembox.ini"),
            "35:1:0x1111:13         ; Handgun - 13 rounds loaded, firepower 2, firing speed 2, reload speed 2, capacity 2\n"
            "53:1:0x1101:7          ; Rocket Launcher - 7 rounds loaded, firepower 2, firing speed 1, ...\n"
            "14:2                   ; Flash Grenade\n"
            "14:1:0x1111:13         ; Flash Grenade - 13 rounds loaded, ...\n"
            "67:1:0x0001:1          ; Stock (TMP) - 1 round loaded, firepower 2, ...\n"
            "4:220                  ; Handgun Ammo\n"
            "4:40:0x1111:13         ; Handgun Ammo - 13 rounds loaded, ...\n"
            "4:50:0x1101:7          ; Handgun Ammo - 7 rounds loaded, ...\n"
            "6:13                   ; Green Herb\n"
            "6:1:0x1111:13          ; Green Herb - 13 rounds loaded, ...\n"
            "12:1:0x0000:8191       ; Plaga Sample\n");
  const std::string original = read_all(path("RE4OGCabbyCodes.itembox.ini"));
  itembox::init(g_dir.c_str());
  inventory::init();
  const itembox::View b = box();
  CHECK(b.usable && !b.write_failed, "the file was not read as usable");
  CHECK(b.entry.size() == 7, "%d entries read, expected 7 (one per item)", static_cast<int>(b.entry.size()));
  CHECK(box_num(4) == 310 && box_num(6) == 14 && box_num(14) == 3 && box_num(53) == 1 && box_num(67) == 1 &&
            box_num(12) == 1,
        "not counted together: Handgun Ammo x%d (310), Green Herb x%d (14), Flash Grenade x%d (3), Rocket Launcher "
        "x%d, Stock (TMP) x%d, Plaga Sample x%d",
        box_num(4), box_num(6), box_num(14), box_num(53), box_num(67), box_num(12));
  for (const itembox::Entry& e : b.entry)
    CHECK(!e.weapon || e.id == 35, "%s kept levels and rounds", items::name(e.id));
  const int hg = box_index(35, true);
  CHECK(hg >= 0 && b.entry[hg].levels == 0x1111 && b.entry[hg].ammo == 13, "the Handgun lost its levels and rounds");

  // A Handgun stored, and Handgun Ammo then picked up into its entry, which
  // still holds the Handgun's levels and rounds: the ammo goes onto its count.
  const int slot = add(35, 1);
  g_case[slot].weapon = 0x2222;
  g_case[slot].ammo = 16;
  tick();
  store(35);
  CHECK(!g_case[slot].used, "the Handgun was not stored");
  const int ammo = add(4, 30);
  CHECK(ammo == slot && g_case[ammo].weapon == 0x2222 && g_case[ammo].ammo == 16,
        "the fake game did not leave the Handgun's levels and rounds in its entry");
  tick();
  store(4);
  CHECK(box_num(4) == 340 && box_index(4, true) < 0, "Handgun Ammo with leftovers: x%d on the count (340), %s",
        box_num(4), box_index(4, true) < 0 ? "no entry of its own" : "AND an entry of its own");
  // A Green Herb in the same entry: the same.
  CHECK(add(6, 1) == slot, "the herb is not in the Handgun's old entry");
  tick();
  store(6);
  CHECK(box_num(6) == 15 && box_index(6, true) < 0, "Green Herb with leftovers: x%d (15)", box_num(6));
  // A part attached to a weapon holds the link there (+6 1, +8 the weapon's
  // index): it is counted too.
  const int tmp = add(48, 1);
  const int stock = add(67, 1);
  g_case[stock].weapon = 1;
  g_case[stock].ammo = tmp >> 3;
  tick();
  store(67);
  CHECK(!g_case[stock].used && box_num(67) == 2 && box_index(67, true) < 0, "an attached Stock (TMP): x%d (2)",
        box_num(67));
  // The two Handguns kept their own.
  int handguns = 0;
  for (const itembox::Entry& e : box().entry) handguns += e.id == 35 && e.weapon ? 1 : 0;
  CHECK(handguns == 2, "%d Handgun entries, expected 2", handguns);

  // Takes: nothing is written onto an item that keeps no levels or rounds - a
  // part comes in as the game sets it up, attached to nothing.
  const int writes = g_weapon_writes;
  take(67);
  int back = -1;
  for (int i = 0; i < static_cast<int>(g_case.size()); ++i)
    if (g_case[i].used && g_case[i].id == 67) back = i;
  CHECK(back >= 0 && g_case[back].weapon == 0 && g_case[back].ammo == kNotAttached,
        "the Stock (TMP) came back with levels 0x%X and rounds %d (attached to something)",
        back >= 0 ? g_case[back].weapon : -1, back >= 0 ? g_case[back].ammo : -1);
  take(4);
  take(6);
  take(14);
  CHECK(g_weapon_writes == writes, "a Take wrote levels and rounds onto an item that keeps none (%d writes)",
        g_weapon_writes - writes);
  CHECK(box_num(67) == 1 && box_num(4) == 290 && box_num(6) == 14 && box_num(14) == 2,
        "after the Takes: Stock (TMP) x%d (1), Handgun Ammo x%d (290), Green Herb x%d (14), Flash Grenade x%d (2)",
        box_num(67), box_num(4), box_num(6), box_num(14));
  // A weapon still comes back with its own.
  take(35, true);
  CHECK(g_weapon_writes == writes + 1, "the Handgun's Take did not write its levels and rounds");

  // The file as rewritten holds counts only, but for the weapons; what was
  // there before is kept beside it.
  CHECK(!stray_state_lines(read_all(path("RE4OGCabbyCodes.itembox.ini"))), "the file still has lines of leftovers");
  CHECK(exists(path("RE4OGCabbyCodes.itembox.ini.bak")) &&
            read_all(path("RE4OGCabbyCodes.itembox.ini.bak")) == original,
        "the file with folded lines was not kept as .bak before its first rewrite");
}

// 4: a file that is there but cannot be read is left alone - nothing is
// stored, nothing written over it.
void phase_unreadable() {
  CreateDirectoryA(path("RE4OGCabbyCodes.itembox.ini").c_str(), nullptr);  // there, but no file to open
  itembox::init(g_dir.c_str());
  inventory::init();
  CHECK(!box().usable, "an unreadable box was taken as usable");
  const int s = add(5, 1);
  tick();
  store(5);
  CHECK(g_case[s].used, "an item was stored into a box that could not be read");
  CHECK(std::strstr(note(), "could not be read"), "the note does not say why: %s", note());
  RemoveDirectoryA(path("RE4OGCabbyCodes.itembox.ini").c_str());
}

// 5: each game that saves has a box of its own (inventory::box_of), in a file
// of its own - the main game's and Separate Ways' - and a change to one leaves
// the other as it was. Assignment Ada and the Mercenaries have none: Store,
// Take, a count and throw away are each refused there, nothing moves and no
// file changes, while the case's own edits still work.
void phase_modes() {
  const std::string main_path = path("RE4OGCabbyCodes.itembox.ini");
  const std::string sw_path = path("RE4OGCabbyCodes.itembox.separate-ways.ini");
  write_all(main_path, "35:1:0x1111:13\n4:100\n87:5\n");
  write_all(sw_path, "4:7\n");
  itembox::init(g_dir.c_str());
  inventory::init();
  const std::string main_file = read_all(main_path), sw_file = read_all(sw_path);
  CHECK(itembox::view(itembox::Box::kMain).usable && itembox::view(itembox::Box::kMain).entry.size() == 3 &&
            itembox::view(itembox::Box::kSeparateWays).usable &&
            itembox::view(itembox::Box::kSeparateWays).entry.size() == 1,
        "the two files were not read as 3 and 1 entries");
  CHECK(std::strcmp(itembox::file_name(itembox::Box::kSeparateWays), "RE4OGCabbyCodes.itembox.separate-ways.ini") == 0,
        "Separate Ways' box is in %s", itembox::file_name(itembox::Box::kSeparateWays));
  const struct {
    game::Mode m;
    bool has;
    itembox::Box b;
    const char* name;
  } games[] = {{game::Mode::kMain, true, itembox::Box::kMain, "the main game"},
               {game::Mode::kSeparateWays, true, itembox::Box::kSeparateWays, "Separate Ways"},
               {game::Mode::kAssignmentAda, false, itembox::Box::kMain, "Assignment Ada"},
               {game::Mode::kMercenaries, false, itembox::Box::kMain, "the Mercenaries"},
               {game::Mode::kNone, false, itembox::Box::kMain, "no game"}};
  for (const auto& g : games) {
    itembox::Box b = itembox::Box::kMain;
    const bool has = inventory::box_of(g.m, &b);
    CHECK(has == g.has && (!has || b == g.b), "%s: %s", g.name,
          !has ? "no box" : b == itembox::Box::kMain ? "the main game's box" : "Separate Ways' box");
  }
  const int spray = add(5, 1);
  const int red9 = add(37, 1);
  g_case[red9].weapon = 0x2101;
  g_case[red9].ammo = 8;

  // Assignment Ada and the Mercenaries: the panel shows no box there; requests
  // made all the same, at either box, are refused.
  for (const game::Mode m : {game::Mode::kAssignmentAda, game::Mode::kMercenaries}) {
    const char* const name = m == game::Mode::kAssignmentAda ? "Assignment Ada" : "the Mercenaries";
    g_mode = m;
    g_box = itembox::Box::kMain;
    tick();
    store(5);
    CHECK(g_case[spray].used && box_num(5) == 0, "First Aid Spray was stored in %s", name);
    CHECK(std::strstr(note(), "only the main game and Separate Ways have an item box") && inventory::view().note_error,
          "the refused Store's note in %s: %s", name, note());
    store(37);
    CHECK(g_case[red9].used && box_index(37, true) < 0, "the Red9 was stored in %s", name);
    take(4);
    CHECK(box_num(4) == 100 && carried(4) == 0, "a Take in %s: box x%d (100), case x%d (0)", name, box_num(4),
          carried(4));
    take(35, true);
    CHECK(box_index(35, true) >= 0 && carried(35) == 0, "the Handgun was taken in %s", name);
    set_count(87, 7);
    CHECK(box_num(87) == 5, "a count set in %s: Spinel x%d (5)", name, box_num(87));
    const int ammo = box_index(4);
    inventory::request_discard(ammo, box().entry[ammo]);
    tick();
    CHECK(box_num(4) == 100, "Handgun Ammo was thrown out of the box in %s", name);
    g_box = itembox::Box::kSeparateWays;
    take(4);
    CHECK(box_num(4) == 7 && carried(4) == 0, "a Take from Separate Ways' box in %s: box x%d (7)", name, box_num(4));
    CHECK(read_all(main_path) == main_file && read_all(sw_path) == sw_file &&
              !exists(path("RE4OGCabbyCodes.itembox.ini.bak")) &&
              !exists(path("RE4OGCabbyCodes.itembox.separate-ways.ini.bak")),
          "a box's file changed in %s", name);
    // The case itself is still the panel's there: a count set, an item thrown away.
    const int tmp = add(32, 20);
    tick();
    inventory::Row r;
    CHECK(row_of(32, &r), "TMP Ammo is not in the view");
    inventory::request_count(r, 60);
    tick();
    CHECK(g_case[tmp].num == 60, "the case's count was not set in %s: TMP Ammo x%d", name, g_case[tmp].num);
    CHECK(row_of(32, &r), "TMP Ammo is not in the view");
    inventory::request_remove(r);
    tick();
    CHECK(!g_case[tmp].used, "TMP Ammo was not thrown away in %s", name);
  }

  // Separate Ways: its own box. A Store goes into it and a Take comes out of
  // it; the main game's box and file stay as they were.
  g_mode = game::Mode::kSeparateWays;
  g_box = itembox::Box::kSeparateWays;
  store(5);
  CHECK(!g_case[spray].used && box_num(5) == 1, "First Aid Spray was not stored in Separate Ways' box: %s", note());
  CHECK(read_all(sw_path).find("\n5:1 ") != std::string::npos, "Separate Ways' file lacks the First Aid Spray");
  take(4);
  CHECK(box_num(4) == 0 && carried(4) == 7, "Separate Ways' Take: box x%d (0), case x%d (7)", box_num(4), carried(4));
  g_box = itembox::Box::kMain;
  CHECK(box_num(4) == 100 && box_num(5) == 0 && read_all(main_path) == main_file,
        "Separate Ways changed the main game's box: Handgun Ammo x%d, First Aid Spray x%d", box_num(4), box_num(5));

  // The main game: its own box again, Separate Ways' left alone.
  g_mode = game::Mode::kMain;
  const std::string sw_after = read_all(sw_path);
  take(4);
  CHECK(box_num(4) == 50 && carried(4) == 57, "the main game's Take: box x%d (50), case x%d (57)", box_num(4),
        carried(4));
  store(37);
  CHECK(box_index(37, true) >= 0 && !g_case[red9].used, "the Red9 was not stored in the main game's box");
  CHECK(read_all(sw_path) == sw_after, "the main game changed Separate Ways' file");
  g_box = itembox::Box::kSeparateWays;
  CHECK(box_index(37, true) < 0 && box_num(5) == 1, "the main game changed Separate Ways' box");
}

// 6: a new start reads each box back from its own file, as the last one left it.
void phase_modes_reread() {
  itembox::init(g_dir.c_str());
  g_box = itembox::Box::kMain;
  CHECK(box().usable && box().entry.size() == 4 && box_num(4) == 50 && box_num(87) == 5 &&
            box_index(35, true) >= 0 && box_index(37, true) >= 0 && box_num(5) == 0,
        "the main game's box read back is not the one written (%d entries)", static_cast<int>(box().entry.size()));
  g_box = itembox::Box::kSeparateWays;
  CHECK(box().usable && box().entry.size() == 1 && box_num(5) == 1,
        "Separate Ways' box read back is not the one written (%d entries)", static_cast<int>(box().entry.size()));
}

int run_phase(const char* exe, const char* phase) {
  // The directory goes without its last separator: a backslash before the
  // closing quote would escape it.
  char cmd[1024];
  std::snprintf(cmd, sizeof(cmd), "\"%s\" %s \"%s\"", exe, phase, g_dir.substr(0, g_dir.size() - 1).c_str());
  STARTUPINFOA si{};
  si.cb = sizeof(si);
  PROCESS_INFORMATION pi{};
  if (!CreateProcessA(nullptr, cmd, nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi)) return 99;
  WaitForSingleObject(pi.hProcess, INFINITE);
  DWORD code = 99;
  GetExitCodeProcess(pi.hProcess, &code);
  CloseHandle(pi.hProcess);
  CloseHandle(pi.hThread);
  return static_cast<int>(code);
}
}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) {
    std::printf("usage: test_itembox <scratch dir>\n");
    return 2;
  }
  // test_itembox <dir> runs every phase, each as "test_itembox <phase> <dir>".
  const bool child = argc >= 3;
  g_dir = argv[child ? 2 : 1];
  if (g_dir.back() != '\\' && g_dir.back() != '/') g_dir += '\\';
  if (child) {
    const std::string phase = argv[1];
    if (phase == "main") phase_main();
    else if (phase == "reread") phase_reread();
    else if (phase == "leftovers") phase_leftovers();
    else if (phase == "unreadable") phase_unreadable();
    else if (phase == "modes") phase_modes();
    else if (phase == "modes-reread") phase_modes_reread();
    else return std::printf("no phase %s\n", phase.c_str()), 2;
    std::fflush(stdout);
    return g_fail ? 1 : 0;
  }
  CreateDirectoryA(g_dir.c_str(), nullptr);
  const auto clean = [] {
    for (const char* f :
         {"RE4OGCabbyCodes.itembox.ini", "RE4OGCabbyCodes.itembox.ini.bak", "RE4OGCabbyCodes.itembox.ini.tmp",
          "RE4OGCabbyCodes.itembox.separate-ways.ini", "RE4OGCabbyCodes.itembox.separate-ways.ini.bak",
          "RE4OGCabbyCodes.itembox.separate-ways.ini.tmp"})
      DeleteFileA(path(f).c_str());
  };
  clean();
  int failed = 0;
  for (const char* phase : {"main", "reread"}) {
    std::printf("--- %s\n", phase);
    std::fflush(stdout);
    if (run_phase(argv[0], phase)) ++failed;
  }
  clean();
  std::printf("--- leftovers\n");
  std::fflush(stdout);
  if (run_phase(argv[0], "leftovers")) ++failed;
  clean();
  std::printf("--- unreadable\n");
  std::fflush(stdout);
  if (run_phase(argv[0], "unreadable")) ++failed;
  clean();
  for (const char* phase : {"modes", "modes-reread"}) {
    std::printf("--- %s\n", phase);
    std::fflush(stdout);
    if (run_phase(argv[0], phase)) ++failed;
  }
  clean();
  std::printf(failed ? "%d phase(s) failed\n" : "all checks pass\n", failed);
  return failed ? 1 : 0;
}
