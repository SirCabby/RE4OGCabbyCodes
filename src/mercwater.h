#pragma once

#include <cstdint>

#include "game.h"

// Kill enemies as they appear in the Mercenaries' water stage (game.h,
// MercWater): which of its spawn areas are up each frame, so that it sends every
// Ganado it can and not one more - the most its own code lets it send (156: the
// start group's 10, six zones' 19 and one's 14, the one-shot areas' 17, the once-
// at-30's 1). Kept apart from the game so the tests can drive it with a stage of
// their own.
//
// The tick runs before the frame's scene area pass, and the stage's tasks run
// after it and before anything moves - the once-at-30 first, the zones in turn,
// the spawner last (which counts those about again and runs the one-shot areas) -
// so what each zone sends this frame is known here exactly: its five entries in
// order, each while fewer than 10 are about (the count the work holds, plus what
// the tasks before it send this frame), each whose send goes (game.h: ready).
//
// - A zone's task ends after the frame whose sends bring its counter to its end
//   (15; zone 17's 10). One short of it, a frame's sends can still be all five:
//   19 (14), the most a zone sends - the tool-assisted runs' "spawn glitch". So a
//   zone is up only in a frame whose sends keep it at or below one short, or, one
//   short, when all five go.
// - A zone whose sends this frame would pass one short (its Ganados' bodies gone
//   at once): it alone is up, and the stage's count of those about is put up for
//   the frame so that it sends exactly what is left (the spawner counts them again
//   after the zones).
// - The once-at-30 sends first in its frame, whatever is about: no zone is up
//   then, whichever way round they run. On a ladder its send would be lost (sends
//   are skipped there and it goes once), so the count of those about is put up
//   for the frame and it waits.
// - The one-shot areas are up whenever the player is not on a ladder: the spawner
//   runs them once each, after the zones (their Ganados come whatever is about).
namespace re4cc::mercwater {

constexpr int kCap = 10;   // a zone sends while fewer are about
constexpr int kBatch = 5;  // a zone's entries: the most it sends in a frame

struct Plan {
  uint32_t zones = 0;       // a bit a zone, in MercWater's order: its area up this frame
  bool oneshots = false;    // the one-shot areas up
  int about_at_least = -1;  // the stage's count of those about put this high for the frame (-1: as it is)
};

// What zone z sends this frame with `about` counted as it begins.
inline int sends(const game::MercWaterZone& z, int about) {
  int n = 0;
  for (int i = 0; i < kBatch && about + n < kCap; ++i) n += z.ready[i] ? 1 : 0;
  return n;
}

inline Plan plan(const game::MercWater& w) {
  Plan p;
  if (!w.started) return p;
  const bool eighth_due = !w.lab_done && w.lab_at > 0 && w.sends - w.about >= w.lab_at;
  if (w.ladder) {
    if (eighth_due) p.about_at_least = w.sends - w.lab_at + 1;
    return p;
  }
  p.oneshots = true;
  if (eighth_due) return p;
  // A zone that would pass one short sends what is left, alone.
  for (int k = 0; k < w.zones; ++k) {
    const game::MercWaterZone& z = w.zone[k];
    const int left = z.ends - 1 - z.sent;
    if (left <= 0) continue;
    if (sends(z, w.about) > left) {
      p.zones = 1u << k;
      p.about_at_least = kCap - left;  // above the count as it is: it sent more than `left`
      return p;
    }
  }
  int about = w.about;
  for (int k = 0; k < w.zones; ++k) {
    const game::MercWaterZone& z = w.zone[k];
    if (z.sent >= z.ends) continue;  // done
    const int s = sends(z, about);
    const int left = z.ends - 1 - z.sent;
    if (s > 0 && (s <= left || (left == 0 && s == kBatch))) {
      p.zones |= 1u << k;
      about += s;
    }
  }
  return p;
}

}  // namespace re4cc::mercwater
