#pragma once

#include <cmath>

#include "game.h"

// Steady aim's hold: once a frame, from the game's state (game::AimPose), whether
// the laser and the shots should take their line from a held copy of the gun
// and the hand (the hold sites in) or from the live ones (out). Kept apart from
// the game so the tests can drive it with poses of their own.
//
// The animation moves the gun and the hand while the aim itself stands still: a
// shot recoils (a machine gun's fire motion starts over at every shot), the gun
// comes up as the aim starts and settles back after a shot. So while the player
// aims (the weapon routine's aim step), a pose that has stood still for a few
// frames is held - and held again whenever it comes to rest somewhere else, so
// the hold is the gun at rest. A shot (the fire step) keeps the hold. The aim
// goes to the fire step in the frame before the first shot, so the tick that
// first sees it sees the aim's last pose; with no hold in yet (the shot comes
// right after the gun came up) that pose is held if it is calm (no more than a
// quarter of a degree from the frame before), else the aim's last calm one, and
// with none at all (fired as the gun came up) the shots are the game's. A hold
// put in there serves every shot of the burst. Any move of the aim (its centre,
// left-right, the body's turn or place, the weapon), or the end of the aim,
// lets it go: the live pose follows the aim again, and is held again once it
// settles.
namespace re4cc::aimhold {

enum class Act { kNone, kHold, kRelease };

class Hold {
 public:
  static constexpr int kSettle = 4;           // still frames before the aim's pose is held
  static constexpr float kCalm = 0.0044f;     // radians the gun may turn between frames and be a shot's pose
  static constexpr float kStillTurn = 1e-5f;  // a part's turn (its matrix's 3x3) between frames, at rest
  static constexpr float kStillMove = 0.1f;   // its place
  static constexpr float kStill = 1e-6f;      // the aim's angles
  static constexpr float kStillPos = 0.5f;    // the body's place

  // This frame's pose; kHold: hold held() from now (in place of any held
  // before), kRelease: let it go. why() says what decided the last of them.
  Act step(const game::AimPose& p) {
    if (!p.ok) {
      have_last_ = cand_ok_ = false;
      calm_ = still_ = 0;
      return let_go("no player or weapon");
    }
    const bool same = have_last_ && same_aim(p, last_);
    const bool was_aim = have_last_ && last_.step == game::AimStep::kAim;
    calm_ = (same && turned(p.gun, last_.gun) <= kCalm) ? calm_ + 1 : 0;
    still_ = (same && !moved(p, last_)) ? still_ + 1 : 0;
    last_ = p;
    have_last_ = true;
    if (!same || p.step == game::AimStep::kNone) {
      cand_ok_ = false;
      return let_go(same ? "the aim ended" : "the aim moved");
    }
    if (p.step == game::AimStep::kFire) {
      if (holding_ || !was_aim) return Act::kNone;  // after the burst's first frame every pose is a shot's
      if (calm_ >= 1) return hold(p, "a shot");
      return cand_ok_ ? hold(cand_, "a shot") : Act::kNone;
    }
    if (calm_ >= 1) {
      cand_ = p;
      cand_ok_ = true;
    }
    if (still_ >= kSettle && (!holding_ || moved(p, held_))) return hold(p, "the aim settled");
    return Act::kNone;
  }

  bool holding() const { return holding_; }
  const game::AimPose& held() const { return held_; }
  const char* why() const { return why_; }
  void reset() { *this = Hold{}; }

  // The angle between two matrices' X axes (the barrel's line), in radians
  // (the chord between the unit axes, near enough at these sizes).
  static float turned(const float a[12], const float b[12]) {
    const float la = std::sqrt(a[0] * a[0] + a[4] * a[4] + a[8] * a[8]);
    const float lb = std::sqrt(b[0] * b[0] + b[4] * b[4] + b[8] * b[8]);
    if (la <= 0 || lb <= 0) return 1.0f;
    const float dx = a[0] / la - b[0] / lb, dy = a[4] / la - b[4] / lb, dz = a[8] / la - b[8] / lb;
    return std::sqrt(dx * dx + dy * dy + dz * dz);
  }
  // Whether a part's matrix (3x4, rows: the turn, then the place in each row's
  // last column) is anywhere else.
  static bool moved(const float a[12], const float b[12]) {
    for (int i = 0; i < 12; ++i)
      if (!(std::fabs(a[i] - b[i]) <= ((i & 3) == 3 ? kStillMove : kStillTurn))) return true;
    return false;
  }

 private:
  static bool same_aim(const game::AimPose& a, const game::AimPose& b) {
    auto within = [](float x, float y, float e) { return std::fabs(x - y) <= e; };
    return a.weapon == b.weapon && a.weapon_no == b.weapon_no && within(a.centre[0], b.centre[0], kStill) &&
           within(a.centre[1], b.centre[1], kStill) && within(a.aim_lr, b.aim_lr, kStill) &&
           within(a.yaw, b.yaw, kStill) && within(a.pos[0], b.pos[0], kStillPos) &&
           within(a.pos[1], b.pos[1], kStillPos) && within(a.pos[2], b.pos[2], kStillPos);
  }
  static bool moved(const game::AimPose& a, const game::AimPose& b) {
    return moved(a.gun, b.gun) || moved(a.hand, b.hand);
  }
  Act hold(const game::AimPose& p, const char* why) {
    held_ = p;
    holding_ = true;
    why_ = why;
    return Act::kHold;
  }
  Act let_go(const char* why) {
    if (!holding_) return Act::kNone;
    holding_ = false;
    why_ = why;
    return Act::kRelease;
  }

  game::AimPose last_, cand_, held_;
  bool have_last_ = false, cand_ok_ = false, holding_ = false;
  int calm_ = 0, still_ = 0;
  const char* why_ = "";
};

}  // namespace re4cc::aimhold
