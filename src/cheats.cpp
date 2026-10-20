#include "cheats.h"

#include <windows.h>

#include <cstdarg>
#include <cstdio>
#include <cstring>

#include "aimhold.h"
#include "gallery.h"
#include "mercmem.h"
#include "mercwater.h"
#include "config.h"
#include "log.h"
#include "mem.h"

namespace re4cc::cheats {
namespace {

volatile LONG g_enabled[kCount] = {};
// g_status is the main thread's working copy; g_published is the only thing
// another thread may look at, swapped over under the lock (an unlocked copy of
// a struct this size can pair one tick's values with another's).
Status g_status;
Status g_published;
CRITICAL_SECTION g_status_lock;
bool g_status_lock_ready = false;
const char* kNames[kCount] = {"God mode",         "Ashley takes no damage", "One hit kills",
                              "Infinite ammo",    "Freeze play time",       "Freeze countdown timer",
                              "Save without counting", "Infinite money",          "Misses don't count",
                              "Deaths don't count", "Automatic QTEs",         "Steady aim",
                              "Automatic shooting gallery", "Endless combo",  "Kill enemies as they appear",
                              "Bonus time always on"};

bool on(Kind k) { return g_enabled[k] != 0; }

// --- requests (panel -> main thread) ----------------------------------------------------
struct Request {
  enum Type { kPesetas, kPlaytime, kCountdown, kCounter, kSaves, kCostume, kDifficulty } type;
  int a;
  int b = 0;
};
Request g_req[16];
int g_req_n = 0;
CRITICAL_SECTION g_req_lock;
bool g_lock_ready = false;

void push(Request r) {
  if (!g_lock_ready) return;
  EnterCriticalSection(&g_req_lock);
  if (g_req_n < 16) g_req[g_req_n++] = r;
  LeaveCriticalSection(&g_req_lock);
}
int drain(Request* out, int max) {
  if (!g_lock_ready) return 0;
  EnterCriticalSection(&g_req_lock);
  const int n = g_req_n < max ? g_req_n : max;
  std::memcpy(out, g_req, sizeof(Request) * static_cast<size_t>(n));
  g_req_n = 0;
  LeaveCriticalSection(&g_req_lock);
  return n;
}

void publish(const Status& st) {
  g_status = st;
  if (!g_status_lock_ready) return;
  EnterCriticalSection(&g_status_lock);
  g_published = st;
  LeaveCriticalSection(&g_status_lock);
}

void action(const char* fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  std::vsnprintf(g_status.last_action, sizeof(g_status.last_action), fmt, ap);
  va_end(ap);
  logf("cheats: %s", g_status.last_action);
}

// Each cheat's patch sites. A switch's patches go in together and come out
// together; one that cannot be applied (another tool changed the bytes) leaves
// the others as they are and says so once.
constexpr game::Site kSitesGod[] = {game::Site::kHurtLeon, game::Site::kKillFlag1, game::Site::kKillFlag2,
                                    game::Site::kCrushRock, game::Site::kCrushTruck};
// The truck's sites lean on kAshleyBlast (game::needs), listed before them.
constexpr game::Site kSitesAshley[] = {game::Site::kHurtAshley,   game::Site::kAshleyBlast,   game::Site::kAshleyStomp,
                                       game::Site::kAshleyTruck1, game::Site::kAshleyTruck2, game::Site::kAshleyRock};
constexpr game::Site kSitesOneHit[] = {game::Site::kHurtEnemy, game::Site::kHurtGigante,
                                       game::Site::kHurtGiganteKnife, game::Site::kHurtKrauser,
                                       game::Site::kHurtKrauserArm};
constexpr game::Site kSitesAmmo[] = {game::Site::kAmmo, game::Site::kArrows, game::Site::kBolts};
constexpr game::Site kSitesPlay[] = {game::Site::kPlayTime};
constexpr game::Site kSitesCountdown[] = {game::Site::kCountdown};
constexpr game::Site kSitesSaves[] = {game::Site::kSaveCount};
constexpr game::Site kSitesMoney[] = {game::Site::kBuy, game::Site::kTuneUp};
constexpr game::Site kSitesMisses[] = {game::Site::kMissA, game::Site::kMissB};
constexpr game::Site kSitesDeaths[] = {game::Site::kDeathsChapter, game::Site::kDeathsTotal};
constexpr game::Site kSitesAim[] = {
    game::Site::kAimSwayV,       game::Site::kAimSwayH,       game::Site::kAimKickV,
    game::Site::kAimKickH,       game::Site::kSpreadGun1,     game::Site::kSpreadGun2,
    game::Site::kSpreadGun3,     game::Site::kSpreadGun4,     game::Site::kSpreadAuto1,
    game::Site::kSpreadAuto2,    game::Site::kSpreadAuto3,    game::Site::kSpreadAuto4,
    game::Site::kSpreadShotgun1, game::Site::kSpreadShotgun2, game::Site::kSpreadMounted1,
    game::Site::kSpreadMounted2, game::Site::kSpreadThrow,     game::Site::kAimLoopHandgun,
    game::Site::kAimLoopWesker,  game::Site::kAimLoopBow,      game::Site::kAimLoopLaser,
    game::Site::kAimLoopBowgun,  game::Site::kAimLoopGrenade,  game::Site::kAimLoopRed9,
    game::Site::kAimLoopMine,    game::Site::kAimLoopRocket,   game::Site::kAimLoopShotgun,
    game::Site::kAimLoopAuto};
// Steady aim's hold: put in and taken out by the tick (update_hold), not with the switch.
constexpr game::Site kSitesHold[] = {game::Site::kHoldLaser, game::Site::kHoldHand1, game::Site::kHoldHand2,
                                     game::Site::kHoldAuto1, game::Site::kHoldAuto2, game::Site::kHoldShotgun};
// The prompts' answer before the kinds sent to it (game::needs).
constexpr game::Site kSitesQte[] = {
    game::Site::kQteKind13,       game::Site::kQteKinds,         game::Site::kQteKindsLR,
    game::Site::kQteBoulder,      game::Site::kQteSwim,          game::Site::kQteRope,
    game::Site::kQteGigante1,     game::Site::kQteGigante2,      game::Site::kQteGigante3,
    game::Site::kQteMinecart,     game::Site::kQteStatueRun,     game::Site::kQteStatueSpeed,
    game::Site::kQteStatueJump,   game::Site::kQteStatueEnd,     game::Site::kQteStatueClimb1,
    game::Site::kQteStatueClimb2, game::Site::kQteKrauser,       game::Site::kQteLasers1,
    game::Site::kQteLasers2,      game::Site::kQteLasersLast,    game::Site::kQteBridge1,
    game::Site::kQteBridge2,      game::Site::kQteBridgeClimb1,  game::Site::kQteBridgeClimb2,
    game::Site::kQteRobotGrab,    game::Site::kQteShake};
constexpr game::Site kSitesCombo[] = {game::Site::kMercFirst, game::Site::kMercWindow, game::Site::kMercBonus};
constexpr game::Site kSitesBonusTime[] = {game::Site::kMercBonusTime, game::Site::kMercBonusBank};

struct Sites {
  const game::Site* s;
  int n;
};
Sites sites_of(Kind k) {
  switch (k) {
    case kGodMode: return {kSitesGod, static_cast<int>(sizeof(kSitesGod) / sizeof(kSitesGod[0]))};
    case kAshleyGod: return {kSitesAshley, static_cast<int>(sizeof(kSitesAshley) / sizeof(kSitesAshley[0]))};
    case kOneHitKills: return {kSitesOneHit, static_cast<int>(sizeof(kSitesOneHit) / sizeof(kSitesOneHit[0]))};
    case kInfiniteAmmo: return {kSitesAmmo, 3};
    case kFreezePlaytime: return {kSitesPlay, 1};
    case kFreezeCountdown: return {kSitesCountdown, 1};
    case kNoSaveCount: return {kSitesSaves, 1};
    case kInfiniteMoney: return {kSitesMoney, 2};
    case kNoMissCount: return {kSitesMisses, 2};
    case kNoDeathCount: return {kSitesDeaths, 2};
    case kAutoQte: return {kSitesQte, static_cast<int>(sizeof(kSitesQte) / sizeof(kSitesQte[0]))};
    case kSteadyAim: return {kSitesAim, static_cast<int>(sizeof(kSitesAim) / sizeof(kSitesAim[0]))};
    case kEndlessCombo: return {kSitesCombo, 3};
    case kBonusTime: return {kSitesBonusTime, 2};
    default: return {nullptr, 0};
  }
}

bool g_warned[static_cast<int>(game::Site::kCount)] = {};

// Whether a site that leans on `s` (game::needs) is in.
bool leaned_on(const Sites& ss, game::Site s) {
  for (int i = 0; i < ss.n; ++i) {
    const mem::Patch* p = game::patch(ss.s[i]);
    if (game::needs(ss.s[i]) == s && p && p->applied) return true;
  }
  return false;
}

// Puts a cheat's patches in (or takes them out) to match its switch; true when
// all of them are as the switch says. They go in in the order listed and come
// out in reverse, and a site another one leans on (game::needs) is listed
// before it: that one goes in only once it is in, and it comes out only once
// nothing leaning on it is left in.
bool toggle(Kind k, bool want) {
  const Sites ss = sites_of(k);
  bool all = true;
  for (int j = 0; j < ss.n; ++j) {
    const game::Site s = ss.s[want ? j : ss.n - 1 - j];
    mem::Patch* p = game::patch(s);
    if (!p) {
      all = false;
      continue;
    }
    if (p->applied == want) continue;
    const int w = static_cast<int>(s);
    const game::Site need = game::needs(s);
    const mem::Patch* q = need != game::Site::kCount ? game::patch(need) : nullptr;
    const bool held = want ? need != game::Site::kCount && !(q && q->applied) : leaned_on(ss, s);
    if (held) {
      if (!g_warned[w]) {
        g_warned[w] = true;
        logf("cheats: %s not %s at %s - %s", name(k), want ? "patched in" : "taken out", game::site_name(s),
             want ? "what it leans on could not go in" : "something leaning on it is still in");
      }
      all = false;
      continue;
    }
    const bool ok = want ? p->apply() : p->revert();
    if (ok) {
      logf("cheats: %s %s at %s", name(k), want ? "patched in" : "taken out", game::site_name(s));
      g_warned[w] = false;
    } else if (!g_warned[w]) {
      g_warned[w] = true;
      logf("cheats: %s could not be %s at %s - the bytes there are not the game's (another tool's patch?)", name(k),
           want ? "patched in" : "taken out", game::site_name(s));
    }
    all = all && (p->applied == want);
  }
  return all && want;
}

// Steady aim's hold (aimhold.h): while the aim is still, the laser and the
// shots take their line from a held copy of the gun's and the hand's pose, so
// neither the idle aim's breathing nor a shot's recoil moves it.
aimhold::Hold g_hold;

void hold_sites(bool in) {
  for (game::Site s : kSitesHold) {
    mem::Patch* p = game::patch(s);
    if (!p || p->applied == in) continue;
    const int w = static_cast<int>(s);
    if (in ? p->apply() : p->revert()) {
      g_warned[w] = false;
    } else if (!g_warned[w]) {
      g_warned[w] = true;
      logf("cheats: Steady aim's hold could not be %s at %s - the bytes there are not the game's (another tool's "
           "patch?)", in ? "put in" : "taken out", game::site_name(s));
    }
  }
}

void update_hold(bool on) {
  if (!on) {
    hold_sites(false);
    g_hold.reset();
    return;
  }
  game::AimPose pose;
  game::aim_pose(&pose);
  switch (g_hold.step(pose)) {
    case aimhold::Act::kHold:
      game::set_held_pose(g_hold.held().gun, g_hold.held().hand);
      hold_sites(true);
      if (config::get().trace) logf("cheats: Steady aim holds the aim's line (%s)", g_hold.why());
      break;
    case aimhold::Act::kRelease:
      hold_sites(false);
      if (config::get().trace) logf("cheats: Steady aim lets the aim's line go (%s)", g_hold.why());
      break;
    case aimhold::Act::kNone:
      break;
  }
}

// Steady aim's stance (game.h): with the switch in, each aim's idle loop starts
// held on its first frame; each tick keeps the aim-up or aim-down motion blended
// with it on that frame too. When the switch goes off in a game, a loop still
// held runs again (from the tick: the teardown leaves it, the game's next motion
// replaces it).
bool g_stance = false;

void update_stance(bool on, bool in_game) {
  if (on) game::keep_stance();
  else if (g_stance && in_game) game::let_stance_go();
  g_stance = on;
}

// The automatic shooting gallery (gallery.h): no patch, a hit handed to a target
// from here each time the gallery's state allows one - the target's own move
// scores it, as it would a shot. Read every tick in the gallery's room (the
// panel shows the score); the play goes on across a pause, and starts over when
// the room or the switch goes.
gallery::Auto g_gallery;

void update_gallery(bool on, const game::Run& run, Status* st) {
  game::Gallery g;
  st->gallery_ok = run.in_game && game::gallery_known() && game::gallery(&g);
  if (st->gallery_ok) {
    st->gallery_state = g.state;
    st->gallery_game = g.game;
    st->gallery_score = g.score;
    st->gallery_streak = g.streak;
  } else if (!run.paused) {
    g_gallery.reset();
  }
  if (!on) g_gallery.reset();
  if (!on || !st->gallery_ok || run.routine != 3) return;  // plays in the main loop only (not paused)
  const gallery::Shot shot = g_gallery.step(g);
  if (!shot.em) return;
  const bool hit = game::gallery_hit(shot.em, shot.part);
  if (config::get().trace || !hit)
    logf("cheats: Automatic shooting gallery: %s %s (score %d, streak %d%s)", hit ? "hits" : "could not hit", shot.what,
         g.score, g.streak, g.bonus_phase ? ", a bonus under way" : "");
}

// The Mercenaries' play: their main loop, the player alive and time on the
// clock (the game scores what dies before it reads the score, after the time is
// up too).
bool merc_playing(const game::Run& run) {
  if (run.mode != game::Mode::kMercenaries || run.routine != 3) return false;
  game::Health h;
  game::Countdown cd;
  return !((game::health(&h) && h.hp <= 0) || (game::countdown(&cd) && (!cd.running || cd.frames <= 0)));
}

// Bonus time always on (game.h, "the Mercenaries"): with its patches in, the
// Bonus Points time is started from here as a pickup starts it, at the first tick
// of play, and put back at its 30 s whenever it has less - it never runs out, and
// the patches let what the combo earns in it go onto the score as it comes. True
// while it runs: the kills wait for it.
bool g_bonus_started = false;  // in this game, for the log

bool update_bonus_time(bool patched, const game::Run& run, const game::Merc& m) {
  if (run.mode != game::Mode::kMercenaries) g_bonus_started = false;
  if (!patched || !merc_playing(run)) return false;
  const bool was = m.bonus_time;
  if (!game::merc_bonus_time()) return false;
  if (!was && !g_bonus_started) {
    g_bonus_started = true;
    logf("cheats: Bonus time always on started the Bonus Points time");
  }
  return was;  // a start goes up in the game's next frame: the kills go from the next tick
}

// Kill enemies as they appear (game.h, "the Mercenaries"): no patch, a killing hit
// handed from here to each Ganado about its business or in a hit's reaction - its
// own move takes it the same frame and scores the kill as a shot's. Only in play
// (merc_playing). A Ganado the hit leaves alive (one the game holds in a state of
// its own) gets another kRetry frames on.
constexpr unsigned kRetry = 15;
struct Recent {
  uintptr_t em = 0;
  unsigned tick = 0;
};
Recent g_recent[game::kMaxMercEnemies];
unsigned g_ticks = 0;
int g_kill_hits = 0;  // in this game, for the log

// The models' memory (mercmem.h): kills wait while the pools could not take the
// Ganados that would come in their place, and an entry spent on a failed set-up
// is sent again once they can.
mercmem::Memory g_memory;
int g_held = 0;          // ticks a kill waited for the pools, since the last pace line
bool g_waiting = false;  // the last tick's kills waited
int g_swapped = 0;       // the village's pickups' Ganados put in the area lists' places, this game
int g_swapped_waves = 0;  // and its chainsaws' (not sent as their wave went)
int g_given = 0;          // the castle's counted sends its pool refused, given back, this game

void watch_memory(game::MercMemory* mm, bool* mm_ok, mercmem::Plan* plan) {
  *mm_ok = game::merc_memory(mm);
  game::MercEntries en;
  const bool en_ok = game::merc_entries(&en);
  game::MercLists lists;
  const bool lists_ok = game::merc_village_lists(&lists);
  const int found_before = g_memory.found();
  *plan = g_memory.step(*mm_ok ? mm : nullptr, en_ok ? &en : nullptr, lists_ok ? &lists : nullptr);
  if (!en_ok) {
    g_memory.pause();
    return;
  }
  int resent = 0;
  for (int i = 0; i < 256; ++i) {
    const uint32_t bit = 1u << (i & 31);
    if (!(plan->resend[i >> 5] & bit) || !game::merc_resend(i)) continue;
    en.sent[i >> 5] &= ~bit;
    ++resent;
  }
  for (int k = 0; k < plan->swaps; ++k) {
    const int from = plan->swap_from[k], area = plan->swap_area[k];
    const bool wave = lists_ok && (lists.waves[from >> 5] & (1u << (from & 31)));
    if (!game::merc_swap_entry(from, area)) continue;
    en.sent[from >> 5] |= 1u << (from & 31);
    en.sent[area >> 5] &= ~(1u << (area & 31));
    if (wave) {
      logf("cheats: Kill enemies as they appear: the stage's one-time send of Ganado %d (a chainsaws' wave in the "
           "village) found the enemy pool or the models' memory full - it comes in the place of an area list's Ganado "
           "that is gone, %d", from, area);
      ++g_swapped_waves;
      continue;
    }
    if (!g_swapped)
      logf("cheats: Kill enemies as they appear: the village's hourglasses' Ganados come with the rest, each in the "
           "place of an area list's Ganado that is gone (the first, entry %d, in %d's place)", from, area);
    ++g_swapped;
  }
  g_memory.sent(en.sent, resent);
  if (plan->found && !found_before)
    logf("cheats: Kill enemies as they appear: the stage spent %d enemy entr%s on Ganados whose set-up failed (the "
         "models' memory was full) - sending them again as it frees", plan->found, plan->found == 1 ? "y" : "ies");
}

// Its pace, for the log: every kPaceMs of play with it on, a line - the kills in
// that time, what the stage counts as about, the pool, the models' memory; and
// once no kill has come for kStallMs of play, the census of every Ganado in the
// pool (game::merc_census), again each kStallAgainMs it lasts.
constexpr ULONGLONG kPaceMs = 15000, kStallMs = 12000, kStallAgainMs = 60000;
struct Pace {
  int kills = -1;        // the kills last seen
  int kills_then = 0;    // at the last pace line
  ULONGLONG prev = 0;    // the last tick in play (0: none since play stopped)
  ULONGLONG played = 0;  // since the last pace line
  ULONGLONG still = 0;   // since the last kill
  ULONGLONG census_at = kStallMs;
};
Pace g_pace;

void pace(const game::Merc& m, const game::MercMemory* mm) {
  const ULONGLONG now = GetTickCount64();
  const ULONGLONG step = g_pace.prev && now > g_pace.prev ? (now - g_pace.prev < 250 ? now - g_pace.prev : 250) : 0;
  g_pace.prev = now;
  if (g_pace.kills < 0) g_pace.kills = g_pace.kills_then = m.kills;
  if (m.kills != g_pace.kills) {
    g_pace.kills = m.kills;
    g_pace.still = 0;
    g_pace.census_at = kStallMs;
  } else {
    g_pace.still += step;
  }
  g_pace.played += step;
  if (g_pace.played >= kPaceMs) {
    game::MercPool p;
    char models[96] = "", castle[128] = "";
    if (mm)
      std::snprintf(models, sizeof(models), "; models %d/%d, parts %d/%d (a Ganado %d)", mm->models_used, mm->models,
                    mm->parts_used, mm->parts, g_memory.ganado_parts());
    game::MercGate gates[2];
    if (game::merc_castle_gates(gates))
      std::snprintf(castle, sizeof(castle), "; the castle: %d refused send(s) given back, gates open %d of 2", g_given,
                    (gates[0].found && !gates[0].enabled) + (gates[1].found && !gates[1].enabled));
    if (game::merc_pool(&p))
      logf("cheats: Kill enemies as they appear: %d kill(s) in the last %u s, %d in all; the stage counts %d about "
           "(it sends more below 10); the pool %d of %d in use, %d bodies (%d gone, for the sweep)%s; %d of the "
           "room's %d Ganados not sent yet, %d spent on a failed set-up and %d sent again, %d hourglasses' and %d "
           "chainsaws' Ganados sent in an area list's place in all; kills waited %d tick(s) for the models' memory%s",
           m.kills - g_pace.kills_then, static_cast<unsigned>(g_pace.played / 1000), m.kills, p.alive, p.used,
           p.slots, p.bodies, p.melted, models, p.unsent, p.listed, g_memory.found(), g_memory.resent(), g_swapped,
           g_swapped_waves, g_held, castle);
    g_pace.played = 0;
    g_pace.kills_then = m.kills;
    g_held = 0;
  }
  if (g_pace.still >= g_pace.census_at) {
    char why[48];
    std::snprintf(why, sizeof(why), "no kill for %u s", static_cast<unsigned>(g_pace.still / 1000));
    game::merc_census(why);
    g_pace.census_at += kStallAgainMs;
  }
}

// The castle (game.h): its counted sends the pool refused, given back each tick of
// its main loop; and its two gates - once its stage has run dry (in play, nothing
// about, no kill for kDryMs of play) the next gate still shut is opened: its area
// trigger widened round the player for one tick, so the game runs its event that
// frame as if he had gone there, then put back. Another try only after another
// kDryMs with nothing; kGateTries a gate at most.
constexpr ULONGLONG kDryMs = 3000;
constexpr int kGateTries = 3;
struct GateRun {
  int armed = -1;          // the gate armed last tick, to put back
  ULONGLONG tried_at = 0;  // g_pace.still when one was last armed
  int tries[2] = {};
  bool open[2] = {};  // seen open, for the log
};
GateRun g_gate_run;

void put_gate_back() {
  if (g_gate_run.armed < 0) return;
  game::merc_castle_arm_gate(g_gate_run.armed, false);
  g_gate_run.armed = -1;
}

void update_gates(const game::MercMemory* mm) {
  put_gate_back();
  game::MercGate gates[2];
  if (!game::merc_castle_gates(gates)) return;
  for (int k = 0; k < 2; ++k)
    if (gates[k].found && !gates[k].enabled && !g_gate_run.open[k]) {
      g_gate_run.open[k] = true;
      logf("cheats: Kill enemies as they appear: the castle's gate %d (area trigger %02X) is open%s", k + 1, gates[k].at,
           g_gate_run.tries[k] ? "" : " - the player opened it");
    }
  if (g_pace.still < g_gate_run.tried_at) g_gate_run.tried_at = 0;  // a kill since the last try
  if (!mm || mm->alive || mm->dying || g_pace.still < kDryMs || g_pace.still < g_gate_run.tried_at + kDryMs) return;
  for (int k = 0; k < 2; ++k) {
    if (!gates[k].found || !gates[k].enabled || g_gate_run.tries[k] >= kGateTries) continue;
    if (game::merc_castle_arm_gate(k, true)) {
      g_gate_run.armed = k;
      g_gate_run.tried_at = g_pace.still;
      ++g_gate_run.tries[k];
      logf("cheats: Kill enemies as they appear: the castle's stage has run dry - its gate %d (area trigger %02X) set "
           "off as if the player had gone there (try %d)", k + 1, gates[k].at, g_gate_run.tries[k]);
    }
    break;
  }
}

// The water stage (game.h, MercWater): while the switch is on there, its spawn
// areas are the cheat's - each tick of its main loop, those mercwater::plan has up
// (its zones each to the most it sends, its one-shot areas once each), the rest
// off; put back as they were when the switch goes off or the game ends.
struct WaterRun {
  bool held = false;        // its areas are the cheat's
  bool told = false;        // the game's first line logged
  uint32_t done = 0;        // the zones logged as done, a bit each
  bool oneshots = false, eighth = false;  // logged
  int most = 0, zones = 0;
};
WaterRun g_water;

void let_water_go() {
  if (g_water.held) game::merc_water_release();
  g_water.held = false;
}

void update_water() {
  game::MercWater w;
  if (!game::merc_water(&w)) {
    let_water_go();
    return;
  }
  const mercwater::Plan p = mercwater::plan(w);
  if (!game::merc_water_set(p.zones, p.oneshots, p.about_at_least)) return;
  g_water.held = true;
  if (!w.started) return;
  g_water.most = w.most;
  g_water.zones = w.zones;
  if (!g_water.told) {
    g_water.told = true;
    logf("cheats: Kill enemies as they appear: the water stage's spawn areas are the cheat's - each zone sends up to "
         "one short of its end, then all five at once (19, zone 17's 14: the most a zone sends), the one-shot areas "
         "once each; at most %d in all with its start group of %d", w.most, w.start);
  }
  for (int k = 0; k < w.zones; ++k) {
    const game::MercWaterZone& z = w.zone[k];
    if (z.sent < z.ends || (g_water.done & (1u << k))) continue;
    g_water.done |= 1u << k;
    logf("cheats: Kill enemies as they appear: the water stage's zone %02X has sent all it sends: %d of %d", z.at,
         z.sent, z.ends - 1 + 5);
  }
  if (!g_water.oneshots && w.oneshots && w.oneshots_sent == w.oneshots) {
    g_water.oneshots = true;
    logf("cheats: Kill enemies as they appear: the water stage's one-shot areas have sent their %d", w.oneshots);
  }
  if (!g_water.eighth && w.lab_done) {
    g_water.eighth = true;
    logf("cheats: Kill enemies as they appear: the water stage's once-at-30 has sent its one");
  }
}

void update_autokill(bool on, const game::Run& run, bool wait) {
  ++g_ticks;
  if (!on || run.mode != game::Mode::kMercenaries) let_water_go();
  if (run.mode != game::Mode::kMercenaries) {
    if (g_water.told)
      logf("cheats: Kill enemies as they appear: in that game %d of the water stage's %d zones sent all they send, its "
           "one-shot areas %s, its once-at-30 %s (at most %d in all)", __builtin_popcount(g_water.done), g_water.zones,
           g_water.oneshots ? "all theirs" : "not all theirs", g_water.eighth ? "its one" : "nothing", g_water.most);
    g_water = WaterRun{};
    if (g_kill_hits) logf("cheats: Kill enemies as they appear handed out %d killing hit(s) in that game", g_kill_hits);
    if (g_memory.found())
      logf("cheats: Kill enemies as they appear: in that game %d enemy entr%s spent on a failed set-up, %d sent again",
           g_memory.found(), g_memory.found() == 1 ? "y was" : "ies were", g_memory.resent());
    g_kill_hits = 0;
    for (Recent& r : g_recent) r = Recent{};
    g_pace = Pace{};
    g_memory.reset();
    if (g_given) logf("cheats: Kill enemies as they appear: in that game the castle's pool refused %d counted send(s), "
                      "each given back to its group", g_given);
    g_held = 0;
    g_swapped = 0;
    g_swapped_waves = 0;
    g_given = 0;
    put_gate_back();
    g_gate_run = GateRun{};
    return;
  }
  if (!on || run.routine != 3) {  // the main loop only (not paused)
    g_pace.prev = 0;
    g_memory.pause();
    put_gate_back();
    if (!run.paused) g_waiting = false;
    return;
  }
  g_waiting = false;
  // The models' memory, every tick of the main loop: the bodies hurried along
  // first (their models go sooner), then what the pools can take.
  game::merc_hurry_bodies();
  game::MercMemory mm;
  bool mm_ok = false;
  mercmem::Plan plan;
  watch_memory(&mm, &mm_ok, &plan);
  if (const int back = game::merc_castle_give_back(g_ticks); back > 0) {
    if (!g_given)
      logf("cheats: Kill enemies as they appear: the castle's pool refused %d counted send(s) - given back to %s "
           "group, which sends again", back, back == 1 ? "its" : "their");
    g_given += back;
  }
  update_water();  // its spawner runs whenever the main loop does
  if (!merc_playing(run)) {
    g_pace.prev = 0;  // not play: its time is not counted
    put_gate_back();
    return;
  }
  game::Merc m;
  if (game::merc(&m)) pace(m, mm_ok ? &mm : nullptr);
  update_gates(mm_ok ? &mm : nullptr);
  if (wait) return;
  int kills = plan.kills;  // -1: any
  game::MercEnemy e[game::kMaxMercEnemies];
  const int n = game::merc_enemies(e, game::kMaxMercEnemies);
  // The list has the newest first: the oldest go first, so none waits long.
  for (int i = n - 1; i >= 0; --i) {
    if (!(e[i].routine == 1 || (e[i].routine == 2 && e[i].alive)) || e[i].scored || e[i].pending) continue;
    Recent* slot = &g_recent[0];
    bool recent = false;
    for (Recent& r : g_recent) {
      if (r.em == e[i].em && g_ticks - r.tick < kRetry) recent = true;
      if (g_ticks - r.tick > g_ticks - slot->tick) slot = &r;  // the oldest, for this one
    }
    if (recent) continue;
    if (kills == 0) {
      ++g_held;
      g_waiting = true;
      break;
    }
    if (!game::merc_kill(e[i].em)) continue;
    *slot = Recent{e[i].em, g_ticks};
    ++g_kill_hits;
    if (kills > 0) --kills;
    if (config::get().trace)
      logf("cheats: Kill enemies as they appear: a killing hit on enemy %02X type %d (it had %d health)", e[i].id,
           e[i].type, e[i].hp);
  }
}

// The stages send their enemies only from the areas the player stands in
// (game.h): with the kills on, every area sends them. The area flags' clear
// each frame is taken out in a game of the Mercenaries and put back outside one
// or with the switch off (the game then clears them again the next frame); the
// flags go up each tick. With every area sending, ten are about whenever a list
// has an entry left, so the village's pickups' events (their checks of the ten,
// game.h) are let send all they have, at the same times.
constexpr game::Site kSitesStage[] = {game::Site::kMercAreas, game::Site::kMercEvent1, game::Site::kMercEvent2,
                                      game::Site::kMercEvent3, game::Site::kCastleDoor};

void update_areas(bool on, const game::Run& run) {
  const bool want = on && run.in_game && run.mode == game::Mode::kMercenaries;
  for (const game::Site s : kSitesStage) {
    mem::Patch* p = game::patch(s);
    if (!p || p->applied == want) continue;
    const int w = static_cast<int>(s);
    if (want ? p->apply() : p->revert()) {
      logf("cheats: Kill enemies as they appear %s at %s", want ? "patched in" : "taken out", game::site_name(s));
      g_warned[w] = false;
    } else if (!g_warned[w]) {
      g_warned[w] = true;
      logf("cheats: Kill enemies as they appear could not be %s at %s - the bytes there are not the game's (another "
           "tool's patch?)", want ? "patched in" : "taken out", game::site_name(s));
    }
  }
  const mem::Patch* areas = game::patch(game::Site::kMercAreas);
  if (want && areas && areas->applied) game::merc_all_areas(g_ticks);
}

// A fix of the game's own, with no switch (game.h, "a save is asked for"): a save the game asks for that its
// scheduler turns away no longer leaves its flags for the next save - Separate Ways' clear save showed no file list
// after the records' save at its results was turned away. The patch is the game's own code at that moment: it goes
// in once, at the first tick (on the game's thread between its frames, so nothing runs in the bytes being written),
// and comes out only as the mod unloads.
bool g_fix_tried = false;

void put_in_fix() {
  if (g_fix_tried) return;
  g_fix_tried = true;
  mem::Patch* p = game::patch(game::Site::kSaveTurnedAway);
  if (!p) return;  // discovery said why
  if (p->apply())
    logf("fixes: a save the game turns away no longer leaves its flags for the next save - put in at %s",
         game::site_name(game::Site::kSaveTurnedAway));
  else
    logf("fixes: a save turned away could not be fixed at %s - the bytes there are not the game's (another tool's "
         "patch?)", game::site_name(game::Site::kSaveTurnedAway));
}

// The switches as the ini last had them: a change from the panel is written
// back on the next tick.
config::Cheats g_saved{};

config::Cheats switches() {
  return config::Cheats{on(kGodMode),        on(kAshleyGod),       on(kOneHitKills),
                        on(kInfiniteAmmo),    on(kFreezePlaytime),  on(kFreezeCountdown),
                        on(kNoSaveCount),     on(kInfiniteMoney),   on(kNoMissCount),
                        on(kNoDeathCount),    on(kAutoQte),         on(kSteadyAim),
                        on(kAutoGallery),     on(kEndlessCombo),    on(kAutoKill),
                        on(kBonusTime)};
}

void save_if_changed() {
  const config::Cheats now = switches();
  if (!std::memcmp(&now, &g_saved, sizeof(now))) return;
  g_saved = now;
  if (config::save_cheats(now)) logf("cheats: switches saved to the ini");
}

// The costume set takes effect for Leon when a room is next set up; the panel
// says so until the room changes.
int g_costume_room = -1;

const char* costume_name(int v) {
  static const char* const kSets[] = {"Normal", "Special 1", "Special 2"};
  return v >= 0 && v < 3 ? kSets[v] : "?";
}

const char* difficulty_name(int v) {
  switch (v) {
    case 0: case 1: return "Amateur";
    case 2: case 3: return "Easy";
    case 4: case 5: return "Normal";
    case 6: return "Professional";
    default: return "?";
  }
}

void hms(int s, char* out, size_t n) { std::snprintf(out, n, "%d:%02d:%02d", s / 3600, (s / 60) % 60, s % 60); }

void apply_requests(const game::Run& run) {
  Request req[16];
  const int n = drain(req, 16);
  for (int i = 0; i < n; ++i) {
    const Request& r = req[i];
    // Edits are made while the game is paused (the panel's only screen), so
    // nothing the game is in the middle of sees a value change under it.
    if (!run.paused) {
      logf("cheats: an edit came in while the game was not paused - dropped");
      continue;
    }
    char t[32];
    switch (r.type) {
      case Request::kPesetas:
        if (game::set_pesetas(r.a)) action("pesetas set to %d", r.a);
        break;
      case Request::kPlaytime:
        if (game::set_play_time(r.a)) {
          hms(r.a, t, sizeof(t));
          action("play time set to %s", t);
        }
        break;
      case Request::kCountdown:
        if (game::set_countdown(static_cast<float>(r.a) * 30.0f)) {
          hms(r.a, t, sizeof(t));
          action("countdown set to %s", t);
        } else {
          action("no countdown is running");
        }
        break;
      case Request::kCounter:
        if (game::set_counter(static_cast<game::Counter>(r.a), r.b)) action("results counter %d set to %d", r.a, r.b);
        break;
      case Request::kSaves:
        if (game::set_saves(r.a)) action("save count set to %d", r.a);
        break;
      case Request::kCostume:
        if (game::set_costume_set(r.a)) {
          g_costume_room = run.room;
          action("costume set changed to %s", costume_name(r.a));
        }
        break;
      case Request::kDifficulty:
        if (game::set_difficulty(r.a)) action("difficulty set to %s", difficulty_name(r.a));
        break;
    }
  }
}

}  // namespace

void init() {
  if (g_lock_ready) return;
  InitializeCriticalSection(&g_req_lock);
  InitializeCriticalSection(&g_status_lock);
  g_status_lock_ready = true;
  g_lock_ready = true;
  const config::Settings& c = config::get();
  set_enabled(kGodMode, c.god_mode);
  set_enabled(kAshleyGod, c.ashley_god);
  set_enabled(kOneHitKills, c.one_hit_kills);
  set_enabled(kInfiniteAmmo, c.infinite_ammo);
  set_enabled(kFreezePlaytime, c.freeze_playtime);
  set_enabled(kFreezeCountdown, c.freeze_countdown);
  set_enabled(kNoSaveCount, c.no_save_count);
  set_enabled(kInfiniteMoney, c.infinite_money);
  set_enabled(kNoMissCount, c.no_miss_count);
  set_enabled(kNoDeathCount, c.no_death_count);
  set_enabled(kAutoQte, c.auto_qte);
  set_enabled(kSteadyAim, c.steady_aim);
  set_enabled(kAutoGallery, c.auto_gallery);
  set_enabled(kEndlessCombo, c.endless_combo);
  set_enabled(kAutoKill, c.auto_kill);
  set_enabled(kBonusTime, c.bonus_time);
  g_saved = switches();
}

const char* name(Kind k) { return k >= 0 && k < kCount ? kNames[k] : "?"; }
bool enabled(Kind k) { return k >= 0 && k < kCount && g_enabled[k] != 0; }
void set_enabled(Kind k, bool v) {
  if (k >= 0 && k < kCount) InterlockedExchange(&g_enabled[k], v ? 1 : 0);
}
bool available(Kind k) {
  if (k == kAutoGallery) return game::gallery_known();
  if (k == kAutoKill) return game::merc_kills_known();
  const Sites ss = sites_of(k);
  for (int i = 0; i < ss.n; ++i)
    if (!game::patch(ss.s[i])) return false;
  if (k == kSteadyAim && !(game::aim_hold_known() && game::aim_stance_known())) return false;
  return ss.n > 0;
}
Status status() {
  if (!g_status_lock_ready) return Status{};
  EnterCriticalSection(&g_status_lock);
  const Status s = g_published;
  LeaveCriticalSection(&g_status_lock);
  return s;
}

void request_pesetas(int v) { push({Request::kPesetas, v}); }
void request_playtime(int seconds) { push({Request::kPlaytime, seconds}); }
void request_countdown(int seconds) { push({Request::kCountdown, seconds}); }
void request_counter(game::Counter c, int v) { push({Request::kCounter, static_cast<int>(c), v}); }
void request_saves(int v) { push({Request::kSaves, v}); }
void request_costume_set(int v) { push({Request::kCostume, v}); }
void request_difficulty(int v) { push({Request::kDifficulty, v}); }

void tick(const game::Run& run) {
  put_in_fix();
  Status st = g_status;
  st.run = run;
  // The patches follow the switches whatever is on screen: a patch is the
  // game's own code doing less, which is harmless at the title too.
  for (int k = 0; k < kCount; ++k) st.patched[k] = toggle(static_cast<Kind>(k), on(static_cast<Kind>(k)));
  update_stance(st.patched[kSteadyAim] && run.in_game, run.in_game);
  update_hold(st.patched[kSteadyAim] && run.in_game && game::aim_hold_known());
  update_gallery(on(kAutoGallery) && game::gallery_known(), run, &st);
  // Bonus time always on goes first: with it on, the kills wait for its time to run.
  game::Merc merc;
  st.merc_ok = run.in_game && run.mode == game::Mode::kMercenaries && game::merc(&merc);
  const bool bonus_up = update_bonus_time(st.patched[kBonusTime] && st.merc_ok, run, merc);
  update_autokill(on(kAutoKill) && game::merc_kills_known(), run, st.patched[kBonusTime] && !bonus_up);
  update_areas(on(kAutoKill) && game::merc_kills_known(), run);
  st.merc_ok = run.in_game && run.mode == game::Mode::kMercenaries && game::merc(&st.merc);
  st.merc_waiting = st.merc_ok && on(kAutoKill) && g_waiting;
  save_if_changed();

  st.health_ok = game::health(&st.health);
  // The holds: health back at its maximum - what is already lost when the
  // switch goes on, and whatever a path other than LifeDownSet2 takes. Only in
  // a game, and only while there is a maximum to hold to (a death's own
  // scripted zero is the game's, and is never undone from here).
  if (run.in_game && st.health_ok) {
    if (on(kGodMode) && st.health.max > 0 && st.health.hp > 0 && st.health.hp < st.health.max)
      game::set_hp(st.health.max);
    if (on(kAshleyGod) && st.health.sub_max > 0 && st.health.sub_hp > 0 && st.health.sub_hp < st.health.sub_max)
      game::set_sub_hp(st.health.sub_max);
  }
  apply_requests(run);
  st.health_ok = game::health(&st.health);
  st.counters_ok = game::counters(&st.counters);
  st.costume_set = game::costume_set();
  if (g_costume_room >= 0 && run.room != g_costume_room) g_costume_room = -1;
  st.costume_pending = g_costume_room >= 0;
  game::countdown(&st.countdown);
  std::memcpy(st.last_action, g_status.last_action, sizeof(st.last_action));
  publish(st);
}

void remove_hooks() {
  g_stance = false;  // no game writes here (not the game's thread): a loop still held runs again at its next motion
  update_hold(false);
  for (const game::Site s : kSitesStage)  // the game clears the area flags again, the events check the ten again
    if (mem::Patch* p = game::patch(s)) p->revert();
  for (int k = 0; k < kCount; ++k) toggle(static_cast<Kind>(k), false);
  if (mem::Patch* p = game::patch(game::Site::kSaveTurnedAway)) p->revert();  // the fix: the scheduler's own again
}

}  // namespace re4cc::cheats
