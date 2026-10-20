#pragma once

#include <cstdint>
#include <cstring>

#include "game.h"

// Kill enemies as they appear and the models' memory (game.h): once a tick of
// the Mercenaries' main loop, from the pools and the room's enemy list as they
// stand (game::MercMemory, game::MercEntries), how many kills may go out and
// which spent entries to send again. Kept apart from the game so the tests can
// drive it with stages of their own.
//
// A stage sends a Ganado whenever fewer than kCap are about. Each takes an enemy
// slot and one for the weapon in its hands, a model record and its parts; a body
// keeps its slot, record and parts until the stage's sweep (its weapon goes with
// its life). One whose set-up finds the records or parts short destroys itself
// with its entry spent - sent, with no enemy and not dead - and the stage never
// sends that entry again; one that finds no slot is not sent at all (a list tries
// it again, a pickup's event or a chainsaws' wave never does). So:
//
// - What is coming is the places of the dying (killed, still counted about) and
//   those below the cap: the stage sends that many as soon as it can. Each takes
//   a Ganado's parts and records; one in a dying Ganado's place one slot more (the
//   dying one's weapon slot comes free), one below the cap two.
// - A kill makes one more coming: it goes out only while the pools have room for
//   it beyond what is coming and a chainsaws' wave (kReserve: two sent at once,
//   whatever is about) - unless no body is there and none is dying, when nothing
//   would free the pools by waiting.
// - A spent entry is found either by the room's dead mask (sent, no enemy, not
//   dead) or, without one, as it is spent (sent since the last tick, no enemy),
//   and is sent again while the pools have room for a Ganado, one each: sending
//   it again makes no Ganado come by itself (the stage sends as many as it is
//   short of the cap, from whatever is not sent), it only keeps the entry - and
//   with no room the stage would spend it again at once. Where the pools cannot
//   take as many as the cap, the stage spends what is not sent every frame it is
//   short, whatever the kills do: those come back here as the bodies go.
// - In the village (game::MercLists), a pickup's event's Ganado not sent - its
//   hourglass not picked up - or a chainsaws' wave's (refused as the wave went,
//   with no free slot) is put in the place of an area list's entry that is dead
//   and gone, which its list then sends again: the stage's 124 come whether the
//   hourglasses are picked up or not, and whatever the waves found. One for each
//   Ganado's room, as a resend.
namespace re4cc::mercmem {

constexpr int kCap = 10;             // the stages send while fewer are about
constexpr int kReserve = 2;          // Ganados' room kept spare from the kills
constexpr int kModelsPerGanado = 3;  // model records a Ganado may take: its own, a weapon's, a margin
constexpr int kPartsMargin = 8;      // parts a Ganado may take besides its model's
constexpr int kEmsPerGanado = 2;     // enemy slots a Ganado takes: its own and its weapon's

struct Plan {
  int kills = -1;           // kills that may go out this tick (-1 any)
  uint32_t resend[8] = {};  // spent entries to send again
  int found = 0;            // entries found spent this tick
  static constexpr int kMaxSwaps = 16;
  uint8_t swap_from[kMaxSwaps] = {}, swap_area[kMaxSwaps] = {};  // events' and waves' Ganados into area lists' places
  int swaps = 0;
};

class Memory {
  static int min(int a, int b) { return a < b ? a : b; }

 public:
  void reset() { *this = Memory{}; }
  // A tick not watched (the switch off, paused): the next one only notes what is sent.
  void pause() { tracking_ = false; }

  Plan step(const game::MercMemory* mm, const game::MercEntries* en, const game::MercLists* lists = nullptr) {
    Plan p;
    // `room`: new Ganados the pools could take now; `spare`: kills they have room
    // for beyond what is coming and the reserve (-1 each: not known, no limit).
    int room = -1, spare = -1;
    if (mm) {
      if (mm->ganado_parts > parts_) parts_ = mm->ganado_parts;
      if (parts_ > 0 && mm->parts > 0 && mm->models > 0) {
        const int per_parts = parts_ + kPartsMargin;
        const int below = mm->alive < kCap ? kCap - mm->alive : 0, coming = mm->dying + below;
        const int free_parts = mm->parts - mm->parts_used, free_models = mm->models - mm->models_used;
        room = min(free_parts / per_parts, free_models / kModelsPerGanado);
        spare = min((free_parts - (coming + kReserve) * per_parts) / per_parts,
                    (free_models - (coming + kReserve) * kModelsPerGanado) / kModelsPerGanado);
        if (mm->ems > 0) {
          const int free_ems = mm->ems - mm->ems_used;
          room = min(room, free_ems / kEmsPerGanado);
          // A kill: one slot more when its place is filled (its weapon's comes free).
          spare = min(spare, free_ems - mm->dying - below * kEmsPerGanado - kReserve * kEmsPerGanado);
        }
        if (room < 0) room = 0;
        if (spare < 0) spare = 0;
      }
    }
    if (en) {
      for (int w = 0; w < 8; ++w) {
        const uint32_t spent = en->ganado[w] & en->sent[w] & ~en->enemy[w] & ~en->dead[w];
        const uint32_t fresh = en->dead_known ? spent : tracking_ ? spent & ~sent_[w] : 0;
        p.found += __builtin_popcount(fresh & ~lost_[w]);
        lost_[w] = (lost_[w] | fresh) & spent;
      }
      int n = room;
      for (int i = 0; i < 256 && n != 0; ++i) {
        const uint32_t bit = 1u << (i & 31);
        if (!(lost_[i >> 5] & bit)) continue;
        p.resend[i >> 5] |= bit;
        if (n > 0) --n;
      }
      if (lists && en->dead_known) {
        int area = 0;  // the next area list's entry to look at
        for (int i = 0; i < 256 && n != 0 && p.swaps < Plan::kMaxSwaps; ++i) {
          const int w = i >> 5;
          const uint32_t bit = 1u << (i & 31);
          if (!((lists->events[w] | lists->waves[w]) & en->ganado[w] & bit) || (en->sent[w] & bit) ||
              (en->dead[w] & bit) || (en->enemy[w] & bit))
            continue;
          for (; area < 256; ++area) {
            const int aw = area >> 5;
            const uint32_t ab = 1u << (area & 31);
            if ((lists->area[aw] & en->ganado[aw] & en->sent[aw] & en->dead[aw] & ab) && !(en->enemy[aw] & ab)) break;
          }
          if (area >= 256) break;  // no area list's entry free yet
          p.swap_from[p.swaps] = static_cast<uint8_t>(i);
          p.swap_area[p.swaps] = static_cast<uint8_t>(area);
          ++p.swaps;
          ++area;
          if (n > 0) --n;
        }
      }
    }
    if (spare >= 0 && mm && (mm->bodies > 0 || mm->dying > 0)) p.kills = spare;
    found_ += p.found;
    return p;
  }

  // After the tick's resends: what is sent now, as they left it (the next tick's
  // "since the last tick"), and how many were sent again.
  void sent(const uint32_t now[8], int resent) {
    std::memcpy(sent_, now, sizeof(sent_));
    for (int w = 0; w < 8; ++w) lost_[w] &= now[w];
    tracking_ = true;
    resent_ += resent;
  }

  int found() const { return found_; }      // entries found spent, this game
  int resent() const { return resent_; }    // sent again
  int ganado_parts() const { return parts_; }

 private:
  uint32_t sent_[8] = {}, lost_[8] = {};
  bool tracking_ = false;
  int parts_ = 0;  // the most parts a Ganado had, this game
  int found_ = 0, resent_ = 0;
};

}  // namespace re4cc::mercmem
