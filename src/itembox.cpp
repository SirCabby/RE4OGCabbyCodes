#include "itembox.h"

#include <windows.h>
#include <io.h>

#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "files.h"
#include "items.h"
#include "log.h"

namespace re4cc::itembox {
namespace {

CRITICAL_SECTION g_lock;  // one for every box
bool g_ready = false;

// A box and its file.
struct State {
  const char* file;           // its name beside the DLL
  const char* game;           // whose box it is, for the file's own note
  bool usable = false;        // the file was read, or is not there yet
  bool write_failed = false;  // the last change could not be written
  bool keep_first = false;    // lines were skipped or folded: the file is kept as .bak before it is first rewritten
  char path[MAX_PATH] = {};
  char tmp[MAX_PATH + 8] = {};
  char bak[MAX_PATH + 8] = {};
  std::vector<Entry> box{};   // in the panel's order; changed on the main thread only, under the lock
};
State g_boxes[kBoxes] = {{"RE4OGCabbyCodes.itembox.ini", "the main game"},
                         {"RE4OGCabbyCodes.itembox.separate-ways.ini", "Separate Ways"}};
State& state(Box b) { return g_boxes[static_cast<int>(b)]; }

struct Lock {
  Lock() { EnterCriticalSection(&g_lock); }
  ~Lock() { LeaveCriticalSection(&g_lock); }
};

// The panel's order (the Add list's), and for one item: its count first, then
// its weapons, the most upgraded first, then the fullest.
int rank_of(int id) {
  const items::Def* d = items::find(id);
  return d ? d->rank : items::kCount + id;
}
int upgrades(int word) {
  int n = 0;
  for (int s = 0; s < 4; ++s) n += (word >> (s * 4)) & 0xF;
  return n;
}
bool before(const Entry& a, const Entry& b) {
  if (a.id != b.id) {
    const int ra = rank_of(a.id), rb = rank_of(b.id);
    return ra != rb ? ra < rb : a.id < b.id;
  }
  if (a.weapon != b.weapon) return !a.weapon;
  if (upgrades(a.levels) != upgrades(b.levels)) return upgrades(a.levels) > upgrades(b.levels);
  if (a.levels != b.levels) return a.levels > b.levels;
  return a.ammo > b.ammo;
}

// What the box keeps of `e`: levels and rounds only for an item whose cItem
// keeps them, a weapon. In any other item's cItem those words are what its
// entry's last occupant left there (or, a part's, the weapon it is on), so it
// is counted with the rest of its kind.
Entry kept(Entry e) {
  if (e.weapon && !items::keeps_state(e.id)) {
    e.weapon = false;
    e.levels = 0;
    e.ammo = 0;
  }
  return e;
}

// `e` into a box: onto its item's entry, or (a weapon) into a place of its own.
void insert(std::vector<Entry>* box, const Entry& e) {
  if (!e.weapon)
    for (Entry& b : *box)
      if (!b.weapon && b.id == e.id) {
        const long long sum = static_cast<long long>(b.num) + e.num;
        b.num = static_cast<int>(sum > kMaxCount ? kMaxCount : sum);
        return;
      }
  box->insert(std::upper_bound(box->begin(), box->end(), e, before), e);
}

int total(const std::vector<Entry>& box) {
  long long n = 0;
  for (const Entry& e : box) n += e.num;
  return static_cast<int>(n > kMaxCount ? kMaxCount : n);
}

// --- the file beside the DLL --------------------------------------------------------------------
// One line per entry:  <item id>:<count>  or, for a weapon,  <item id>:<count>:<levels>:<rounds>,
// each followed by its name after ";" (for reading only).
bool write_file(State& st, const std::vector<Entry>& box) {
  if (st.keep_first) {
    st.keep_first = false;
    if (CopyFileA(st.path, st.bak, FALSE))
      logf("itembox: %s had lines that were skipped or folded - kept as %s before it is rewritten", st.path, st.bak);
  }
  FILE* f = std::fopen(st.tmp, "w");
  if (!f) {
    logf("ERROR: itembox: cannot write %s", st.tmp);
    return false;
  }
  std::fprintf(f,
               "# RE4OGCabbyCodes - the item box of %s\n"
               "#\n"
               "# What the panel's Item box holds in %s: one box, kept here, outside the\n"
               "# game. Every save file and new game of %s sees the same box, and nothing the\n"
               "# game does changes it - only the panel's Store, Take, x and its count fields do,\n"
               "# each written here at once. The main game and Separate Ways each have a box of\n"
               "# their own, %s and %s;\n"
               "# Assignment Ada and the Mercenaries, played in one sitting, have none.\n"
               "#\n"
               "#   <item id>:<count>                      everything of one item, on one line\n"
               "#   <item id>:<count>:<levels>:<rounds>    a weapon, a line of its own\n"
               "#\n"
               "# <levels> is a weapon's upgrade levels as the game keeps them, level - 1 in each of\n"
               "# four hex digits: capacity, reload speed, firing speed, firepower, from the left\n"
               "# (0x1111 is level 2 in all four). <rounds> is what it has loaded. The ids are the\n"
               "# game's own; what follows \";\" on a line is only its name. The mod reads this file\n"
               "# as the game starts and writes it whole at every change, so edit it only while the\n"
               "# game is closed. Deleting it empties the box.\n",
               st.game, st.game, st.game, g_boxes[0].file, g_boxes[1].file);
  for (const Entry& e : box) {
    char data[48], note[160];
    if (e.weapon) {
      std::snprintf(data, sizeof(data), "%d:%d:0x%04X:%d", e.id, e.num, static_cast<unsigned>(e.levels & 0xFFFF), e.ammo);
      std::snprintf(note, sizeof(note), "%s - %d round%s loaded, firepower %d, firing speed %d, reload speed %d, capacity %d",
                    items::name(e.id), e.ammo, e.ammo == 1 ? "" : "s", (e.levels & 0xF) + 1, ((e.levels >> 4) & 0xF) + 1,
                    ((e.levels >> 8) & 0xF) + 1, ((e.levels >> 12) & 0xF) + 1);
    } else {
      std::snprintf(data, sizeof(data), "%d:%d", e.id, e.num);
      std::snprintf(note, sizeof(note), "%s", items::name(e.id));
    }
    std::fprintf(f, "%-22s ; %s\n", data, note);
  }
  // Onto the disk before the rename: fclose only hands the bytes to the OS
  // cache, so an unclean shutdown could land the rename with nothing behind it.
  bool ok = std::fflush(f) == 0 && !std::ferror(f);
  const HANDLE h = reinterpret_cast<HANDLE>(_get_osfhandle(_fileno(f)));
  if (ok && h != INVALID_HANDLE_VALUE) FlushFileBuffers(h);
  ok = std::fclose(f) == 0 && ok;
  // Replaced in one step: a crash halfway leaves the old file, not half of a new one.
  if (!ok || !files::replace(st.tmp, st.path, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
    logf("ERROR: itembox: cannot replace %s (error %lu)", st.path, GetLastError());
    DeleteFileA(st.tmp);
    return false;
  }
  logf("itembox: %s written - %d entr%s, %d item(s)", st.path, static_cast<int>(box.size()),
       box.size() == 1 ? "y" : "ies", total(box));
  return true;
}

// One line of the file into `out`: false for a line that is no entry (a
// comment, a blank) through `blank`, or one that makes no sense.
bool parse_line(std::string line, Entry* out, bool* blank) {
  *blank = false;
  const size_t cut = line.find_first_of(";#");
  if (cut != std::string::npos) line.resize(cut);
  const char* s = line.c_str();
  while (*s == ' ' || *s == '\t') ++s;
  if (!*s || *s == '\r' || *s == '[') {
    *blank = true;
    return false;
  }
  long v[4] = {};
  int n = 0;
  while (n < 4) {
    char* end = nullptr;
    v[n] = std::strtol(s, &end, n == 2 ? 0 : 10);  // the levels may be hex
    if (end == s) return false;
    ++n;
    s = end;
    if (*s != ':') break;
    ++s;
  }
  while (*s == ' ' || *s == '\t' || *s == '\r') ++s;
  if (*s || (n != 2 && n != 4)) return false;
  if (v[0] < 0 || v[0] >= items::kCount || v[1] < 1 || v[1] > kMaxCount) return false;
  Entry e;
  e.id = static_cast<int>(v[0]);
  e.num = static_cast<int>(v[1]);
  if (n == 4) {
    // The game keeps the levels in a 16-bit word and the rounds shifted left by
    // three in another.
    if (v[2] < 0 || v[2] > 0xFFFF || v[3] < 0 || v[3] > 0x1FFF) return false;
    e.weapon = true;
    e.levels = static_cast<int>(v[2]);
    e.ammo = static_cast<int>(v[3]);
  }
  *out = e;
  return true;
}

void read_file(State& st) {
  if (GetFileAttributesA(st.path) == INVALID_FILE_ATTRIBUTES) {
    st.usable = true;
    logf("itembox: no %s yet - the box of %s starts empty", st.path, st.game);
    return;
  }
  FILE* f = std::fopen(st.path, "r");
  if (!f) {
    logf("ERROR: itembox: %s is there but could not be read - the box of %s is left alone this run (nothing is written "
         "over it)",
         st.path, st.game);
    return;
  }
  int lines = 0, skipped = 0, folded = 0;
  std::string line;
  for (int c = std::fgetc(f);; c = std::fgetc(f)) {
    if (c != '\n' && c != EOF) {
      line += static_cast<char>(c);
      continue;
    }
    ++lines;
    Entry e;
    bool blank = false;
    if (parse_line(line, &e, &blank)) {
      // A line of levels and rounds for an item that keeps none (a Store before
      // 2026-10-15 took what another item had left in its slot for its state).
      const Entry k = kept(e);
      if (!same(k, e)) {
        ++folded;
        logf("itembox: line %d of %s gives %s levels and rounds, which it does not keep - counted with the rest of it: "
             "%s",
             lines, st.path, items::name(e.id), line.c_str());
      }
      insert(&st.box, k);
    } else if (!blank) {
      ++skipped;
      logf("itembox: line %d of %s makes no sense - skipped: %s", lines, st.path, line.c_str());
    }
    line.clear();
    if (c == EOF) break;
  }
  const bool failed = std::ferror(f) != 0;
  std::fclose(f);
  if (failed) {
    st.box.clear();
    logf("ERROR: itembox: %s could not be read to the end - the box of %s is left alone this run (nothing is written "
         "over it)",
         st.path, st.game);
    return;
  }
  st.usable = true;
  st.keep_first = skipped > 0 || folded > 0;
  logf("itembox: %s read - %d entr%s, %d item(s)%s", st.path, static_cast<int>(st.box.size()),
       st.box.size() == 1 ? "y" : "ies", total(st.box),
       skipped && folded ? " (some lines were skipped and some folded, see above)"
       : skipped         ? " (some lines were skipped, see above)"
       : folded          ? " (some lines were folded, see above)"
                         : "");
}

// The box as it is now, to change and write before it is put in place. Only
// the main thread changes the box, so nothing moves while it is being written
// (outside the lock, so the panel is not held up by the disk).
std::vector<Entry> copy(const State& st) {
  Lock lock;
  return st.box;
}
void replace(State& st, std::vector<Entry>* next, bool written) {
  Lock lock;
  st.box.swap(*next);
  st.write_failed = !written;
}
void write_failed(State& st) {
  Lock lock;
  st.write_failed = true;
}

bool valid(const std::vector<Entry>& box, int index, const Entry& seen) {
  return index >= 0 && index < static_cast<int>(box.size()) && same(box[index], seen);
}

}  // namespace

bool same(const Entry& a, const Entry& b) {
  return a.id == b.id && a.num == b.num && a.weapon == b.weapon && a.levels == b.levels && a.ammo == b.ammo;
}

const char* file_name(Box b) { return state(b).file; }

void init(const char* dir) {
  if (g_ready) return;
  InitializeCriticalSection(&g_lock);
  for (State& st : g_boxes) {
    std::snprintf(st.path, sizeof(st.path), "%s%s", dir, st.file);
    std::snprintf(st.tmp, sizeof(st.tmp), "%s.tmp", st.path);
    std::snprintf(st.bak, sizeof(st.bak), "%s.bak", st.path);
  }
  g_ready = true;
  for (State& st : g_boxes) read_file(st);
}

View view(Box b) {
  View v;
  v.which = b;
  if (!g_ready) return v;
  const State& st = state(b);
  Lock lock;
  v.usable = st.usable;
  v.write_failed = st.write_failed;
  v.entry = st.box;
  return v;
}

bool put(Box b, const Entry& e) {
  State& st = state(b);
  if (!g_ready || !st.usable || e.id < 0 || e.id >= items::kCount || e.num < 1) return false;
  std::vector<Entry> next = copy(st);
  insert(&next, kept(e));
  if (!write_file(st, next)) {
    write_failed(st);
    return false;
  }
  replace(st, &next, true);
  return true;
}

void unput(Box b, const Entry& given) {
  State& st = state(b);
  if (!g_ready || !st.usable) return;
  const Entry e = kept(given);  // as put() kept it
  std::vector<Entry> next = copy(st);
  for (size_t i = 0; i < next.size(); ++i) {
    Entry& b = next[i];
    if (b.id != e.id || b.weapon != e.weapon) continue;
    if (e.weapon && !same(b, e)) continue;
    if (!e.weapon && b.num > e.num) b.num -= e.num;
    else next.erase(next.begin() + static_cast<std::ptrdiff_t>(i));
    const bool ok = write_file(st, next);
    if (!ok) logf("ERROR: itembox: %s x%d is back out of the box, but the file still has it", items::name(e.id), e.num);
    replace(st, &next, ok);
    return;
  }
}

bool take(Box b, int index, const Entry& seen, int num) {
  State& st = state(b);
  if (!g_ready || !st.usable) return false;
  std::vector<Entry> next = copy(st);
  if (!valid(next, index, seen)) {
    logf("ERROR: itembox: entry %d is not %s x%d any more - nothing taken out of the box", index, items::name(seen.id),
         seen.num);
    return false;
  }
  if (next[index].weapon || num >= next[index].num) next.erase(next.begin() + index);
  else next[index].num -= num;
  // The item is in the game already: the box is changed whether the file takes
  // it or not (if not, the file still holds it until a later change is written).
  const bool ok = write_file(st, next);
  replace(st, &next, ok);
  return ok;
}

bool set_count(Box b, int index, const Entry& seen, int num) {
  State& st = state(b);
  if (!g_ready || !st.usable || num < 1 || num > kMaxCount) return false;
  std::vector<Entry> next = copy(st);
  if (!valid(next, index, seen) || next[index].weapon) return false;
  if (next[index].num == num) return true;
  // The count is not part of the box's order: one item has one entry.
  next[index].num = num;
  if (!write_file(st, next)) {
    write_failed(st);
    return false;
  }
  replace(st, &next, true);
  return true;
}

bool discard(Box b, int index, const Entry& seen) {
  State& st = state(b);
  if (!g_ready || !st.usable) return false;
  std::vector<Entry> next = copy(st);
  if (!valid(next, index, seen)) return false;
  next.erase(next.begin() + index);
  if (!write_file(st, next)) {
    write_failed(st);
    return false;
  }
  replace(st, &next, true);
  return true;
}

}  // namespace re4cc::itembox
