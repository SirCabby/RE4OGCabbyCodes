#include "inventory.h"

#include <windows.h>

#include <cstdarg>
#include <cstdio>
#include <cstring>

#include "items.h"
#include "log.h"

namespace re4cc::inventory {
namespace {

CRITICAL_SECTION g_lock;
bool g_ready = false;
View g_view;  // published under the lock

struct Request {
  enum Kind { kCount, kWeapon, kRemove, kAdd, kStore, kTake, kBoxCount, kDiscard } kind = kCount;
  Row seen;
  int a = 0;
  int levels[4] = {};
  itembox::Entry entry;
};
Request make(Request::Kind k, const Row& seen = Row{}, int a = 0) {
  Request r;
  r.kind = k;
  r.seen = seen;
  r.a = a;
  return r;
}
constexpr int kMaxRequests = 16;
Request g_req[kMaxRequests];
int g_nreq = 0;
char g_note[160] = {};
bool g_note_error = false;

void push(const Request& r) {
  if (!g_ready) return;
  EnterCriticalSection(&g_lock);
  if (g_nreq < kMaxRequests) g_req[g_nreq++] = r;
  LeaveCriticalSection(&g_lock);
}

void note(bool error, const char* fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  std::vsnprintf(g_note, sizeof(g_note), fmt, ap);
  va_end(ap);
  g_note_error = error;
  logf("inventory: %s", g_note);
}

int difficulty() {
  game::Counters c;
  return game::counters(&c) ? c.difficulty : 5;
}

// How many of an item one piece in the case (or one entry off it) holds: the
// game's own itemInfo count for its kind at this difficulty.
int stack_max(int id) {
  const int kind = game::item_kind(id);
  const items::Def* d = items::find(id);
  const int k = kind >= 0 ? kind : d ? d->kind : 23;
  const items::Counts c = items::counts(k, difficulty());
  return c.max > 0 ? c.max : 1;
}

int type_of(int id) {
  const int kind = game::item_kind(id);
  if (kind >= 0) return items::counts(kind, 5).type;
  const items::Def* d = items::find(id);
  return d ? d->type : 0;
}

// Off the grid: key items, files, maps and cases, treasures, gems, bottle caps,
// the Mercenaries' bonuses.
bool off_grid(int type) {
  return type == items::kKeyItem || type == items::kFile || type == items::kMap || type == items::kTreasure ||
         type == items::kGem || type == items::kBottleCap || type == items::kBonus;
}

Row make_row(const game::Item& it) {
  Row r;
  r.index = it.index;
  r.id = it.id;
  r.num = it.num;
  r.equipped = it.equipped;
  r.onboard = it.onboard;
  r.stack_max = stack_max(it.id);
  const int cap1 = game::charge(it.id, 1);
  r.weapon = cap1 != -1 && game::max_level(it.id, 0) > 0;
  if (r.weapon) {
    levels_of(it.weapon, r.levels);
    for (int s = 0; s < 4; ++s) {
      r.max_levels[s] = game::max_level(it.id, s) + items::exclusive(it.id, s);
      if (r.max_levels[s] < 1) r.max_levels[s] = 1;
    }
    r.capacity = game::charge(it.id, r.levels[3]);
    r.ammo = it.ammo;
  }
  return r;
}

bool same(const game::Item& it, const Row& seen) { return it.index == seen.index && it.id == seen.id; }

// The item a request names, if it is still the one the panel showed.
bool find(const Row& seen, game::Item* out) {
  static game::Item list[game::kMaxItems];
  const int n = game::items(list, game::kMaxItems);
  for (int i = 0; i < n; ++i)
    if (same(list[i], seen)) {
      *out = list[i];
      return true;
    }
  return false;
}

// What the character in play carries of an item: its pieces (or entries), and
// how much more they could take, each up to what one holds. The game's own
// placement counts the same - every item of that id the character has.
struct Held {
  int entries = 0;
  int room = 0;
};
Held held(int id) {
  static game::Item list[game::kMaxItems];
  const int n = game::items(list, game::kMaxItems);
  const int max = stack_max(id);
  Held h;
  for (int i = 0; i < n; ++i)
    if (list[i].id == id) {
      ++h.entries;
      if (list[i].num < max) h.room += max - list[i].num;
    }
  return h;
}

// Up to `num` of an item into the game, as a pickup brings it - into the case
// (or off the grid, by its type) - and never more than it takes in whole: how
// many went in, 0 with the reason in `why`. A weapon's rounds and levels
// follow it in.
//  - In the case, the game's placement (PutInCase) tops up the pieces of it
//    already there first and puts what is left in a new piece, turning it if it
//    has to - all or nothing, and no more than one piece's worth. With no room
//    for a new piece, it is given only what the pieces there can still take.
//  - Off the grid (cItemMgr::get), a treasure or a gem the character holds goes
//    onto that entry, and the game drops what goes past one entry's worth; a
//    file or a bottle cap held already is taken as done, with nothing added.
//    So neither is given more than it keeps.
int bring_in(const itembox::Entry& e, int num, int* index_out, const char** why) {
  static const char* const kNoRoom = "no room in the attache case";
  if (index_out) *index_out = -1;
  *why = kNoRoom;
  const int type = type_of(e.id);
  const int max = stack_max(e.id);
  int want = e.weapon ? 1 : num < max ? num : max;
  int index = -1, moved = 0;
  if (off_grid(type)) {
    const Held h = held(e.id);
    if ((type == items::kFile || type == items::kBottleCap) && h.entries) {
      *why = "it is held already";
      return 0;
    }
    if ((type == items::kTreasure || type == items::kGem) && h.entries) {
      if (h.room <= 0) {
        *why = "the one held is full";
        return 0;
      }
      if (want > h.room) want = h.room;
    }
    *why = "the game did not take it";
    if (want > 0 && game::item_get(e.id, want, &index)) moved = want;
  } else if (!items::in_case(e.id)) {
    *why = "it has no piece for the attache case";
    return 0;
  } else if (game::put_in_case(e.id, want, &index)) {
    moved = want;
  } else if (!e.weapon && max > 1) {
    const int room = held(e.id).room;
    const int top = room < want ? room : want;
    if (top > 0 && game::put_in_case(e.id, top, &index)) moved = top;
  }
  if (moved && e.weapon) {
    if (index >= 0) game::set_item_weapon(index, e.id, e.levels, e.ammo);
    else logf("inventory: %s went in, but its new entry was not found - its levels and rounds are the game's", items::name(e.id));
  }
  if (index_out) *index_out = index;
  return moved;
}

// " x350" for an item's count in the box, "" for a weapon.
const char* box_quantity(const itembox::Entry& e, int num, char* out, size_t n) {
  if (e.weapon) out[0] = '\0';
  else std::snprintf(out, n, " x%d", num);
  return out;
}

void apply(const Request& r, const game::Run& run) {
  game::Item it;
  const bool boxed = r.kind == Request::kTake || r.kind == Request::kBoxCount || r.kind == Request::kDiscard;
  const char* nm = items::name(boxed ? r.entry.id : r.kind == Request::kAdd ? r.a : r.seen.id);
  // The box is the game's own (box_of): the main game's or Separate Ways'.
  itembox::Box box = itembox::Box::kMain;
  if ((boxed || r.kind == Request::kStore) && !box_of(run.mode, &box))
    return note(true, "%s stays where it is - only the main game and Separate Ways have an item box", nm);
  switch (r.kind) {
    case Request::kCount: {
      if (!find(r.seen, &it)) return note(true, "%s changed before the edit - nothing was done", nm);
      const int max = stack_max(it.id);
      const int num = r.a < 1 ? 1 : r.a > max ? max : r.a;
      if (game::set_item_num(it.index, it.id, num)) note(false, "%s: count set to %d", nm, num);
      break;
    }
    case Request::kWeapon: {
      if (!find(r.seen, &it)) return note(true, "%s changed before the edit - nothing was done", nm);
      int lv[4];
      for (int s = 0; s < 4; ++s) {
        const int max = game::max_level(it.id, s) + items::exclusive(it.id, s);
        lv[s] = r.levels[s] < 1 ? 1 : r.levels[s] > max ? max : r.levels[s];
      }
      const int cap = game::charge(it.id, lv[3]);
      const int ammo = cap == 0x8000 || cap < 0 ? it.ammo : r.a < 0 ? 0 : r.a > cap ? cap : r.a;
      if (game::set_item_weapon(it.index, it.id, levels_word(lv), ammo))
        note(false, "%s: firepower %d, firing speed %d, reload speed %d, capacity %d, %d round(s) loaded", nm, lv[0],
             lv[1], lv[2], lv[3], ammo);
      break;
    }
    case Request::kRemove:
      if (!find(r.seen, &it)) return note(true, "%s changed before it could be thrown away - nothing was done", nm);
      // The case's size is which of these the game finds held: not an item to take away.
      if (items::is_case(it.id)) return note(true, "%s is the attache case's size - left as it is", nm);
      if (game::item_erase(it.index, it.id)) note(false, off_grid(type_of(it.id)) ? "%s removed" : "%s thrown away", nm);
      else note(true, "%s could not be taken out", nm);
      break;
    case Request::kAdd: {
      const int max = stack_max(r.a);
      itembox::Entry e;
      e.id = r.a;
      const int num = r.levels[0] < 1 ? 1 : r.levels[0] > max ? max : r.levels[0];
      const char* why = "";
      const int moved = bring_in(e, num, nullptr, &why);
      if (moved == num) note(false, "%s x%d added", nm, moved);
      else if (moved) note(false, "%s x%d added - the other %d found no room", nm, moved, num - moved);
      else note(true, "%s could not be added - %s", nm, why);
      break;
    }
    case Request::kStore: {
      if (!find(r.seen, &it)) return note(true, "%s changed before it could be stored - nothing was done", nm);
      // The box is for what takes room in the case: the key items, files and
      // treasures live off its grid (and the case's size is no item at all).
      if (off_grid(type_of(it.id)) || items::is_case(it.id))
        return note(true, "%s takes no room in the attache case - it does not go in the item box", nm);
      if (!itembox::view(box).usable)
        return note(true, "%s stays where it is - the item box's file could not be read as the game started (see the log)", nm);
      itembox::Entry e;
      e.id = it.id;
      e.num = it.num;
      // A weapon keeps an entry of its own in the box, with its levels and
      // rounds; of everything else the box keeps a count. Which is the item's
      // own: the game sets up levels and rounds for its weapons only, and in
      // any other item's cItem they are what the entry's last occupant left
      // there (or, a part's, the weapon it is attached to).
      e.weapon = items::keeps_state(it.id);
      if (e.weapon) {
        e.levels = it.weapon;
        e.ammo = it.ammo;
        if (e.num < 1) e.num = 1;
      }
      if (e.num < 1) return note(true, "%s holds nothing to store", nm);
      // Into the box (and its file) first: an item is never only in memory.
      if (!itembox::put(box, e))
        return note(true, "%s stays where it is - the item box's file could not be written (see the log)", nm);
      if (!game::item_erase(it.index, it.id)) {
        itembox::unput(box, e);
        return note(true, "%s could not be taken out of the case - it stays there", nm);
      }
      char q[24];
      note(false, "%s%s put in the item box", nm, box_quantity(e, e.num, q, sizeof(q)));
      break;
    }
    case Request::kTake: {
      // The game first, then the box: an item the file cannot be told about
      // stays in it as well, rather than being lost.
      const itembox::View v = itembox::view(box);
      if (r.a < 0 || r.a >= static_cast<int>(v.entry.size()) || !itembox::same(v.entry[r.a], r.entry))
        return note(true, "the item box changed before %s could be taken - nothing was done", nm);
      const itembox::Entry& e = v.entry[r.a];
      const char* why = "";
      const int moved = bring_in(e, e.num, nullptr, &why);
      if (!moved) return note(true, "%s stays in the item box - %s", nm, why);
      char q[24];
      if (!itembox::take(box, r.a, e, moved))
        return note(true, "%s%s taken out - but the item box's file could not be written, so it still lists it (see the log)",
                    nm, box_quantity(e, moved, q, sizeof(q)));
      if (!e.weapon && moved < e.num)
        note(false, "%s x%d taken out of the item box - %d left in it", nm, moved, e.num - moved);
      else
        note(false, "%s%s taken out of the item box", nm, box_quantity(e, moved, q, sizeof(q)));
      break;
    }
    case Request::kBoxCount: {
      const itembox::View v = itembox::view(box);
      if (r.a < 0 || r.a >= static_cast<int>(v.entry.size()) || !itembox::same(v.entry[r.a], r.entry))
        return note(true, "the item box changed before %s's count could be set - nothing was done", nm);
      // A weapon's entry is the one weapon, with its levels and rounds.
      if (r.entry.weapon) return note(true, "%s is a weapon of its own in the item box - it has no count to set", nm);
      const int num = r.levels[0] < 1 ? 1 : r.levels[0] > itembox::kMaxCount ? itembox::kMaxCount : r.levels[0];
      if (!itembox::set_count(box, r.a, r.entry, num))
        return note(true, "%s stays at %d in the item box - its file could not be written (see the log)", nm,
                    r.entry.num);
      note(false, "%s: count in the item box set to %d", nm, num);
      break;
    }
    case Request::kDiscard: {
      const itembox::View v = itembox::view(box);
      if (r.a < 0 || r.a >= static_cast<int>(v.entry.size()) || !itembox::same(v.entry[r.a], r.entry))
        return note(true, "the item box changed before %s could be thrown away - nothing was done", nm);
      char q[24];
      if (!itembox::discard(box, r.a, r.entry))
        return note(true, "%s was not thrown away - the item box's file could not be written (see the log)", nm);
      note(false, "%s%s thrown out of the item box", nm, box_quantity(r.entry, r.entry.num, q, sizeof(q)));
      break;
    }
  }
}

void publish(const View& v) {
  EnterCriticalSection(&g_lock);
  g_view = v;
  LeaveCriticalSection(&g_lock);
}

}  // namespace

bool goes_in_case(int id) { return !off_grid(type_of(id)); }

bool box_of(game::Mode m, itembox::Box* out) {
  if (m == game::Mode::kMain) *out = itembox::Box::kMain;
  else if (m == game::Mode::kSeparateWays) *out = itembox::Box::kSeparateWays;
  else return false;
  return true;
}

int levels_word(const int lv[4]) {
  int w = 0;
  for (int s = 0; s < 4; ++s) w |= ((lv[s] < 1 ? 0 : lv[s] - 1) & 0xF) << (s * 4);
  return w;
}

void levels_of(int word, int out[4]) {
  for (int s = 0; s < 4; ++s) out[s] = ((word >> (s * 4)) & 0xF) + 1;
}

void init() {
  if (g_ready) return;
  InitializeCriticalSection(&g_lock);
  g_ready = true;
}

View view() {
  if (!g_ready) return View{};
  EnterCriticalSection(&g_lock);
  const View v = g_view;
  LeaveCriticalSection(&g_lock);
  return v;
}

void request_count(const Row& seen, int num) { push(make(Request::kCount, seen, num)); }
void request_weapon(const Row& seen, const int levels[4], int ammo) {
  Request r = make(Request::kWeapon, seen, ammo);
  for (int s = 0; s < 4; ++s) r.levels[s] = levels[s];
  push(r);
}
void request_remove(const Row& seen) { push(make(Request::kRemove, seen)); }
void request_add(int id, int num) {
  Request r = make(Request::kAdd, Row{}, id);
  r.levels[0] = num;
  push(r);
}
void request_store(const Row& seen) { push(make(Request::kStore, seen)); }
void request_take(int box_index, const itembox::Entry& seen) {
  Request r = make(Request::kTake, Row{}, box_index);
  r.entry = seen;
  push(r);
}
void request_box_count(int box_index, const itembox::Entry& seen, int num) {
  Request r = make(Request::kBoxCount, Row{}, box_index);
  r.entry = seen;
  r.levels[0] = num;
  push(r);
}
void request_discard(int box_index, const itembox::Entry& seen) {
  Request r = make(Request::kDiscard, Row{}, box_index);
  r.entry = seen;
  push(r);
}

void tick(const game::Run& run) {
  if (!g_ready) return;
  // The panel is only drawn while the game is paused (or on the load and save
  // lists, where there is no case to show), so the view is only built then.
  static bool was_paused = false;
  if (!run.paused) {
    if (was_paused) {
      View v;
      v.known = game::items_known();
      std::snprintf(v.why, sizeof(v.why), "the attache case is edited while the game is paused");
      std::memcpy(v.note, g_note, sizeof(v.note));
      v.note_error = g_note_error;
      publish(v);
    }
    was_paused = false;
    // Requests left from a pause that has ended are dropped, not carried into play.
    EnterCriticalSection(&g_lock);
    g_nreq = 0;
    LeaveCriticalSection(&g_lock);
    return;
  }
  was_paused = true;
  Request req[kMaxRequests];
  EnterCriticalSection(&g_lock);
  const int nreq = g_nreq;
  std::memcpy(req, g_req, sizeof(Request) * static_cast<size_t>(nreq));
  g_nreq = 0;
  LeaveCriticalSection(&g_lock);
  const bool calls = game::item_rules_known() && game::case_calls_known();
  for (int i = 0; i < nreq; ++i) apply(req[i], run);

  View v;
  v.known = game::items_known();
  v.editable = v.known && calls;
  if (!v.known) std::snprintf(v.why, sizeof(v.why), "the game's item manager was not found");
  else if (!calls) std::snprintf(v.why, sizeof(v.why), "the game's item tables or case functions were not found");
  v.character = game::item_character();
  v.case_size = game::case_size();
  v.difficulty = difficulty();
  static game::Item list[game::kMaxItems];
  const int n = game::items(list, game::kMaxItems);
  for (int i = 0; i < n; ++i) {
    const Row r = make_row(list[i]);
    if (off_grid(type_of(r.id))) {
      if (v.nkey < kMaxKey) v.key[v.nkey++] = r;
    } else if (v.ncase < kMaxCase) {
      v.in_case[v.ncase++] = r;
    }
  }
  std::memcpy(v.note, g_note, sizeof(v.note));
  v.note_error = g_note_error;
  publish(v);
}

}  // namespace re4cc::inventory
