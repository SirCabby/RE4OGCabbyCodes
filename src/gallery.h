#pragma once

#include <climits>
#include <cmath>

#include "game.h"

// The automatic shooting gallery: once a frame, from the gallery as it stands
// (game::Gallery), which target to hit, if any. Kept apart from the game so the
// tests can drive it with galleries of their own.
//
// What it plays for is the most the gallery gives (see game.h, "the shooting
// gallery"): every Ganado in the head, Ashley never, and the bonus target every
// time five hits in a row bring it on. Every one of the 25 scoring targets is
// needed for the sixth bonus target, so none is let go.
//
// A target is hit once it is half way up (rising: the game takes a hit on it all
// the same), one hit at a time: the next waits until the last is taken off its
// target's record, and a few frames more. The ones carrying a bomb go first, then
// the one with the least time left. Only the other targets keep the script's
// clock running: with none of them standing the script hurries on to its next
// targets. So the last of them is left standing while a bomb's carrier is (or
// the script would hurry on past it), and in a window until its bonus target
// shows - else the next targets come up inside the window, where its holds keep
// them waiting; once shown, the bonus target keeps the clock running itself. Not
// at a streak of four, before the hit that starts a window: the window starts
// whenever that hit comes, and the script hurries on into it all the same, so a
// hold there only kept a target waiting (up to half a minute).
//
// The streak decides the rest. With no bonus under way any hit may make it five
// (the gallery brings the bonus on); while one is under way a fifth hit is lost,
// so hits stop at four - and until the bonus target is down, at three, leaving it
// room. Those holds give way to a target about to go down (kUrgent): a lost
// streak costs a bonus at most, a lost target its points and the sixth bonus.
//
// The props' 300 points (the lights' prop's legs, then its chest, where the
// gallery offers game C or D) end the streak like any prop, so they are taken in
// the first bonus's window, where the gallery has just started the streak again:
// everything else waits for them, the bonus target too, until the props have
// slid all the way out - and for no longer than kSecretWait.
namespace re4cc::gallery {

struct Shot {
  uintptr_t em = 0;  // 0: none this frame
  int part = 0;
  const char* what = "";
};

class Auto {
 public:
  static constexpr int kGap = 3;            // frames from one hit being taken to the next hit
  static constexpr float kHalfUp = 0.5f;    // how far up a rising target is first hit
  static constexpr int kUrgent = 6;         // frames of a target's time left at which nothing holds its hit
  static constexpr int kSecretWait = 120;   // the frames of a bonus's window the props' 300 may hold it
  static constexpr float kPropOut = 4900.0f;  // the props slide to +-5000 (100 a frame) as they come out

  Shot step(const game::Gallery& g) {
    if (g.state != 2) {
      game_ = Game{};
      return {};
    }
    ++since_;
    if (g.bonus_phase && !game_.phase) {
      game_.phase = true;
      game_.phase_frames = 0;
      game_.bonus_seen = false;
      game_.no_secret = false;
    }
    if (!g.bonus_phase) game_.phase = false;
    if (game_.phase) ++game_.phase_frames;

    const game::GalleryTarget *bonus = nullptr, *lights = nullptr, *pick = nullptr, *urgent = nullptr;
    int holders = 0;       // targets keeping the script's clock running (types 0, 1 and 3 with health)
    bool bomb_up = false;  // a target carrying a bomb, risen or rising
    for (int i = 0; i < g.n; ++i) {
      const game::GalleryTarget& t = g.t[i];
      // A hit still on a record: a target's until its move takes it; a prop's for
      // as long as it stays up, as each frame of it ends the streak again.
      if (t.pending && (t.hp > 0 || t.type >= 10)) {
        since_ = 0;
        return {};
      }
      if (t.hp <= 0) continue;
      if (t.type < 2 || t.type == 3) ++holders;
      if (t.type == 5) bomb_up = true;
      if (t.type == 3) bonus = &t;
      if (t.type == 14) lights = &t;
      if (!(t.type == 0 || t.type == 1 || t.type == 3 || t.type == 5) || !up(t)) continue;
      if (t.life >= 0 && t.life <= kUrgent && (!urgent || t.life < urgent->life)) urgent = &t;
      if (t.type != 3 && (!pick || before(t, *pick))) pick = &t;
    }
    if (bonus) game_.bonus_seen = true;
    if (urgent && since_ >= 1)
      return shoot(urgent, game::kGalleryHead, urgent->type == 3 ? "the bonus target (going)" : "a target (going)");
    if (since_ <= kGap || g.streak >= 5) return {};  // at five the gallery has yet to start the streak again

    if (lights && !(g.flags & 2) && game_.phase && !game_.no_secret) {
      // The window's streak is the gallery's fresh one - unless a hit made in the
      // frame before the window opened is in it, which the props would throw away.
      if (g.streak != 0 || game_.phase_frames > kSecretWait) {
        game_.no_secret = true;
      } else if (g.cooldown <= 0 && std::fabs(lights->x) >= kPropOut) {
        return shoot(lights, (g.flags & 1) ? game::kGalleryPropChest : game::kGalleryPropLegs,
                     (g.flags & 1) ? "the lights' prop's chest (the 300 points)" : "the lights' prop's legs (the lights)");
      } else {
        return {};
      }
    }
    const int cap = game_.phase ? 4 : 5;
    if (bonus && up(*bonus) && g.streak + 1 <= cap) return shoot(bonus, game::kGalleryHead, "the bonus target");
    if (!pick) return {};
    // The last target keeping the clock running stays up while a bomb is (the
    // clock would hurry on past it), and in a bonus's window until its bonus
    // target shows: with nothing standing there the script hurries on to its next
    // targets, which the window's holds then keep from being hit.
    const bool last = pick->type != 5 && holders == 1;
    if (last && (bomb_up || (game_.phase && !game_.bonus_seen))) return {};
    const int room = game_.phase && !(game_.bonus_seen && !bonus) ? 1 : 0;  // the bonus target's, until it is down
    if (g.streak + 1 + room <= cap) return shoot(pick, game::kGalleryHead, "a target's head");
    return {};
  }

  void reset() { *this = Auto{}; }

 private:
  // Risen (waiting or moving), or rising and half way up: a target is hit as it
  // turns up, as soon as its head would show over the counter.
  static bool up(const game::GalleryTarget& t) {
    return t.routine == 2 || t.routine == 3 || (t.routine == 0 && t.rise >= kHalfUp);
  }
  // The bombs first, then the least time left (a target that stays, last), then
  // the longest standing.
  static bool before(const game::GalleryTarget& a, const game::GalleryTarget& b) {
    if ((a.type == 5) != (b.type == 5)) return a.type == 5;
    const int la = a.life < 0 ? INT_MAX : a.life, lb = b.life < 0 ? INT_MAX : b.life;
    if (la != lb) return la < lb;
    return a.age > b.age;
  }
  Shot shoot(const game::GalleryTarget* t, int part, const char* what) {
    since_ = 0;
    return Shot{t->em, part, what};
  }

  // One game's: its bonus's window (the gallery's flag), and whether the props'
  // 300 is off for this window.
  struct Game {
    bool phase = false, bonus_seen = false, no_secret = false;
    int phase_frames = 0;
  };
  Game game_;
  int since_ = 1 << 20;
};

}  // namespace re4cc::gallery
