#include "game.h"

#include <windows.h>

#include <cmath>
#include <cstdio>
#include <cstring>

#include "config.h"
#include "log.h"
#include "sigs.h"

namespace re4cc::game {
namespace {

enum SigId {
#define SIG_ID(id, pattern, matches, what) id,
  RE4CC_SIGS(SIG_ID)
#undef SIG_ID
      kSigCount
};
struct SigDef {
  const char* name;
  const char* pattern;
  int matches;
};
constexpr SigDef kSigs[] = {
#define SIG_DEF(id, pattern, matches, what) {#id, pattern, matches},
    RE4CC_SIGS(SIG_DEF)
#undef SIG_DEF
};

HMODULE g_exe = nullptr;
mem::Range g_text;
bool g_ready = false;
char g_status[256] = "not looked for yet";

// What discovery found (0 = not found). The static objects' own addresses, the
// pointers' slots.
uintptr_t g_pg_slot = 0;
uintptr_t g_item_mgr = 0, g_subscreen = 0, g_cockpit = 0, g_option_flag = 0;
uintptr_t g_clock_keep = 0;  // the copy of the CountDown the option screen keeps while it is up (the Cockpit's +AC)
uintptr_t g_storage_slot = 0, g_card_slot = 0, g_exists = 0, g_checkpoint_slot = 0;
// The scheduler's levels (scheduler.cpp: a task each, 0x348 bytes a level) and the save card's task, as a save
// asked for hands it to level 1.
uintptr_t g_sched = 0, g_card_task = 0;
uintptr_t g_tick_return = 0;
uintptr_t g_tick_return_late = 0;  // the same frame's QueryPerformanceCounter call after it (the tick's second point)
uintptr_t g_put_in_case = 0, g_item_get = 0, g_item_erase = 0;
uintptr_t g_kind_table = 0, g_charge = 0, g_charge_rows_slot = 0, g_levels = 0, g_levels_end = 0;
uintptr_t g_player_slot = 0;  // cPlayer* (Steady aim's hold reads the player, its weapon and their parts)
uintptr_t g_aim_blend = 0;    // the aim's blend helper (static; Steady aim's stance)
uintptr_t g_gallery_slot = 0;  // the shooting gallery's work pointer
uintptr_t g_em_count_slot = 0, g_em_stride_slot = 0, g_em_base_slot = 0, g_em_list_slot = 0;  // the enemy pool
uintptr_t g_merc_work = 0;    // the Mercenaries' work (static)
uintptr_t g_em10_vtable = 0;  // the Ganados' class (cEm10), whose hit check takes a hit's record
uintptr_t g_models_slot = 0, g_parts_slot = 0;  // the model pool's and the parts pool's managers (cManager*)
uintptr_t g_village_slot = 0;  // the Mercenaries' village's work (its +C the frames since its sweep)
// The village's lists, from its code: the entries its seven area lists send, and
// those its three pickups' events send (a bit an entry, entry i word i / 32).
uint32_t g_village_area[8] = {}, g_village_events[8] = {};
bool g_village_lists = false;
// Its chainsaws' waves: each one's flag in pG+174 (up once it has gone) and the
// first of the two entries it sends.
struct VillageWave {
  uint32_t flag = 0;
  int entry = 0;
};
VillageWave g_village_waves[3];
bool g_village_waves_known = false;
// The Mercenaries' castle (room 402), from its code: its work (the wraps from +0,
// the groups' counters, the sweep's timer), its spawner's counted groups (the
// counter's place in the work, its cap, its wraps), and its two gates' area
// triggers (their numbers and events) in the scene's list.
uintptr_t g_castle_slot = 0;
int g_castle_timer = 0;      // the sweep's timer in the work (float, frames of delta)
float g_castle_every = 0.f;  // past it the sweep runs
struct CastleGroup {
  int counter = 0, cap = 0, n = 0;
  uint8_t wraps[8] = {};
};
constexpr int kCastleGroups = 16, kCastleWraps = 0x558 / 0xC;
CastleGroup g_castle_groups[kCastleGroups];
int g_castle_group_n = 0;
uintptr_t g_at_slot = 0;  // the scene's area triggers' work: +8C their list
struct CastleGate {
  int at = -1;
  uintptr_t event = 0;
};
CastleGate g_castle_gates[2];
bool g_castle_gates_known = false;
// The Mercenaries' water stage (room 404), from its code: its work's slot, its
// zones (each one's area trigger, its counter's place in the work, the count its
// task ends at and its five entries, in the order their tasks were started), its
// one-shot areas and their Ganados, its eighth task's threshold and two entries,
// and the player's routines (r_no_1 under r_no_0 0, a bit each) whose sends the
// stage skips: on a ladder.
uintptr_t g_water_slot = 0;
struct WaterZone {
  int at = -1, counter = 0, ends = 0;
  uint8_t entry[5] = {};
};
WaterZone g_water_zones[kWaterZones];
int g_water_zone_n = 0;
uint8_t g_water_oneshot_at[8] = {};
int g_water_oneshot_n = 0;
uint32_t g_water_oneshots[8] = {}, g_water_lab[8] = {};
int g_water_lab_at = 0;
uint32_t g_water_ladder = 0;
bool g_water_known = false;
// The Mercenaries' base (room 403), from its function: the entries its 22 area
// lists send, and its two one-time sends (each its flag in pG+174 and its entry).
uint32_t g_base_area[8] = {};
VillageWave g_base_waves[2];
bool g_base_known = false;
mem::Patch g_patch[static_cast<int>(Site::kCount)];

// What Steady aim's hold sites point the game at in place of a model's part:
// only the part's world matrix (+C, 3x4 floats, rows) is read there.
struct HeldPart {
  uint8_t head[0xC];
  float m[12];
};
HeldPart g_held_gun, g_held_hand;

// GLOBAL_WK and the item array, validated when they change (a pointer read
// every frame must not cost a VirtualQuery every frame: RE2CabbyCodes lost 90
// fps to that under Wine).
uintptr_t g_pg_ok = 0;
uintptr_t g_items_ok = 0;
int g_items_ok_n = 0;
uintptr_t g_card_ok = 0;
uintptr_t g_buffer_ok = 0;

// GLOBAL_WK offsets (re4_tweaks' SDK/global.h names them; each was checked
// against the code that uses it - see CLAUDE.md, "GLOBAL_WK").
constexpr int kGlobalSize = 0x8690;
constexpr int kRno0 = 0x20, kRno1 = 0x21;
constexpr int kFlagsSystem = 0x54;  // Flags_SYSTEM_0
constexpr int kTimeBase = 0x5C;     // GetTickCount()/1000 when the play time was last brought up to date
constexpr int kDaLevel = 0x4F98, kChapter = 0x4F9A, kSaveCount = 0x4F9C;
constexpr int kPlayTime = 0x4FA4, kPesetas = 0x4FA8, kRoom = 0x4FAC;
constexpr int kHp = 0x4FB4, kHpMax = 0x4FB6, kSubHp = 0x4FB8, kSubHpMax = 0x4FBA;
constexpr int kPlType = 0x4FC8, kSubCostume = 0x4FCB;
constexpr int kDeathsChapter = 0x8460, kDeathsTotal = 0x8462, kKillsChapter = 0x8464, kKillsTotal = 0x8468;
constexpr int kHitsChapter = 0x846C, kHitsTotal = 0x8470, kShotsChapter = 0x8474, kShotsTotal = 0x8478;
constexpr int kDifficulty = 0x847C;
constexpr int kSaveRegion = 0x4F90;
constexpr uint32_t kSysAssignmentAda = 0x80000000u, kSysMercenaries = 0x40000000u, kSysSeparateWays = 0x1000u;
constexpr uint32_t kSysHardMode = 0x20u;

// cItemMgr and cItem.
constexpr int kMgrWeapon = 0xC, kMgrChar = 0x13, kMgrItems = 0x14, kMgrCount = 0x1C;
constexpr int kItemSize = 0xE;
constexpr int kCountdown = 0x98;  // CountDown in the Cockpit

// The player (cPlayer, a cModel), its weapon work (cPlWep) and its weapon object
// (cObjWep, a cModel too); a model's parts (FUN_0065b7c0's lookup).
constexpr int kWeaponNo = 0x4FC0;
constexpr int kPlayerSize = 0x814, kPlRno = 0xFC, kPlPos = 0x94, kPlYaw = 0xA4, kPlAimLr = 0x448, kPlWep = 0x7D8;
constexpr int kPlWepSize = 0x50, kWepCentre = 0x28, kWepObj = 0x34;
constexpr int kObjWepSize = 0x76C;
constexpr uint8_t kRnoWeapon = 6, kStepAim = 1, kStepFire = 2;  // r_no_1, and the weapon routine's r_no_2
constexpr int kModelFlags = 0x4, kModelParts = 0xF4, kPartSize = 0x1D8, kPartMatrix = 0xC;
// A model's motion (re4_tweaks' MOTION_INFO, at +1D8; +2A8 the motion blended
// with it, or 0): +40 the attribute (4 loop, 8 its clock stopped), +AC..+BB the
// clock (the frame as 10.6 fixed point, the two before it, the frame as a
// float), +BC the frames in it. The aim's blend helper: +18 its motion B.
constexpr int kMotion = 0x1D8, kMotionB = 0x2A8;
constexpr int kMotAttr = 0x40, kMotClock = 0xAC, kMotClockSize = 0x10, kMotFrame = 0xB8, kMotFrames = 0xBC;
constexpr uint16_t kMotLoop = 4, kMotStop = 8;
constexpr int kBlendB = 0x18;

// cCard.
constexpr int kCardSize = 0x5A0;
constexpr int kCardRoutine = 0x4, kCardStep = 0x5, kCardMode = 0x14, kCardSlot = 0x1C, kCardCursor = 0x1D;
constexpr int kCardStatus = 0x44, kCardBuffer = 0x4E8;
// A save asked for: its flags in GLOBAL_WK+4, its task handed to the scheduler's level 1 (a level, 0x348 bytes:
// +4 its state byte - 0 free, 1 starting, 3 running, 80 held - +33C its task).
constexpr int kTaskState = 0x4, kTaskFn = 0x33C;
constexpr int kCardLevel = 1;

uint32_t u32_at(uintptr_t a) { return mem::read<uint32_t>(a); }

// The sole match of a signature (or each of its matches, all agreeing on the
// dword at +off): 0 when it is missing or the matches disagree.
std::vector<uintptr_t> g_hits[kSigCount];

bool find_sig(int id) {
  const SigDef& s = kSigs[id];
  g_hits[id] = mem::find_all(g_text, s.pattern, 8);
  const bool ok = static_cast<int>(g_hits[id].size()) == s.matches;
  if (!ok)
    logf("game: signature %s found %d time(s), expected %d - what it finds is left out", s.name,
         static_cast<int>(g_hits[id].size()), s.matches);
  return ok;
}
uintptr_t at(int id) { return g_hits[id].size() == static_cast<size_t>(kSigs[id].matches) ? g_hits[id][0] : 0; }
uintptr_t operand(int id, int off) {
  if (!at(id)) return 0;
  const uint32_t v = u32_at(g_hits[id][0] + off);
  for (uintptr_t h : g_hits[id])
    if (u32_at(h + off) != v) {
      logf("game: %s's matches disagree at +%d - left out", kSigs[id].name, off);
      return 0;
    }
  return v;
}
// An E8 call's destination, followed through an incremental-link `jmp` thunk.
uintptr_t call_target(uintptr_t site) {
  if (!site || mem::read<uint8_t>(site) != 0xE8) return 0;
  uintptr_t t = site + 5 + mem::read<int32_t>(site + 1);
  if (g_text.contains(t) && mem::read<uint8_t>(t) == 0xE9) t = t + 5 + mem::read<int32_t>(t + 1);
  return g_text.contains(t) ? t : 0;
}
bool in_image(uintptr_t a) { return mem::module_range(g_exe).contains(a); }

// Match `i` of a signature that matches more than once (address order), 0 when
// the count is not the one expected.
uintptr_t hit(int id, int i) { return at(id) && i < static_cast<int>(g_hits[id].size()) ? g_hits[id][i] : 0; }
uintptr_t plus(uintptr_t a, int off) { return a ? a + off : 0; }

void prepare_at(Site s, uintptr_t site, const char* expect, const std::vector<uint8_t>& bytes) {
  mem::Patch& p = g_patch[static_cast<int>(s)];
  if (!site || !p.prepare(site, expect, bytes, site_name(s)))
    logf("game: patch site %s not usable - its cheat is unavailable", site_name(s));
}
void prepare(Site s, int sig, int off, const char* expect, const std::vector<uint8_t>& bytes) {
  prepare_at(s, at(sig) ? at(sig) + off : 0, expect, bytes);
}

// Steady aim. The aim's sway (FUN_00684330) is called with the aim's up-down and
// left-right angles by the aiming each frame, and by a weapon's shot. While
// nothing moves the aim it drifts by a random factor (a new one every 1/30 s)
// times the weapon's sway speed and the frame's delta, kept inside the weapon's
// sway box round the centre the aim was last put at; a shot puts it back at
// that centre plus a random throw. Then the shot itself turns away from the aim
// by random factors of its own: a handgun's or machine gun's direction is
// (-50000, random * 200, random * 200) in the hand's frame, a shotgun's centre
// pellet's gets random * 100 more (its other pellets' rings are the shotgun's
// own, and are left as they are), the mounted gun's random * 1000 on 50000; a
// thrown grenade or egg gets a sideways speed of random * 15. Each random factor
// becomes zero where it is used - the drift's two (fld factor -> fldz), the
// rest at the fmul that scales the random number just drawn (fmul scale ->
// fstp st(0); fldz) - and the random numbers are still drawn, so whatever else
// the game draws them for comes out as it would.
void prepare_steady_aim() {
  const uintptr_t sway = at(kSigAimSway), kick = at(kSigAimKick);
  if (!sway || !kick) {
    logf("game: the aim's sway not found - Steady aim is unavailable");
    return;
  }
  // The factors the drift uses are the two the random numbers were just stored
  // in; both paths draw from one random function and read the delta through
  // pG; and the shot's test sends every other call on to the drift, a little
  // before the factors are drawn.
  const uintptr_t factor_v = operand(kSigAimSway, 7), factor_h = operand(kSigAimSway, 18);
  const uintptr_t rng = call_target(sway);
  const uintptr_t drift = kick + 4 + mem::read<int8_t>(kick + 3);
  if (!in_image(factor_v) || !in_image(factor_h) || factor_v == factor_h || operand(kSigAimSway, 33) != factor_v ||
      operand(kSigAimSway, 55) != factor_h || !rng || call_target(sway + 11) != rng ||
      call_target(kick + 4) != rng || call_target(kick + 35) != rng || !g_pg_slot ||
      operand(kSigAimSway, 27) != g_pg_slot || operand(kSigAimKick, 17) != g_pg_slot ||
      operand(kSigAimKick, 47) != g_pg_slot || operand(kSigAimKick, 23) != operand(kSigAimKick, 56) ||
      !(drift < sway && sway - drift < 0x40)) {
    logf("game: the aim's sway is not the code expected - Steady aim is unavailable");
    return;
  }
  const std::vector<uint8_t> fldz = {0xD9, 0xEE, 0x90, 0x90, 0x90, 0x90};  // fldz
  const std::vector<uint8_t> zero = {0xDD, 0xD8, 0xD9, 0xEE, 0x90, 0x90};  // fstp st(0); fldz
  prepare_at(Site::kAimSwayV, sway + 31, "D9 05 ?? ?? ?? ??", fldz);
  prepare_at(Site::kAimSwayH, sway + 53, "D9 05 ?? ?? ?? ??", fldz);
  prepare_at(Site::kAimKickV, kick + 21, "D8 0D ?? ?? ?? ??", zero);
  prepare_at(Site::kAimKickH, kick + 54, "D8 0D ?? ?? ?? ??", zero);

  // The shots' spread: each fmul scales the random number the call just before
  // it returned. The handguns' and the machine guns' turn by the same scale on
  // the same forward length, each pair of factors shares its scale, and the
  // machine guns' two shots (the first, held fire) build one static vector.
  auto spread = [&](Site s, uintptr_t fmul, const char* expect) {
    prepare_at(s, fmul && call_target(fmul - 5) == rng ? fmul : 0, expect, zero);
  };
  const uintptr_t gun_scale = operand(kSigSpreadGun, 16), auto_vec = operand(kSigSpreadAuto, 8);
  if (gun_scale && operand(kSigSpreadGun, 30) == gun_scale && operand(kSigSpreadAuto, 19) == gun_scale &&
      operand(kSigSpreadAuto, 36) == gun_scale && operand(kSigSpreadGun, 2) &&
      operand(kSigSpreadAuto, 2) == operand(kSigSpreadGun, 2) && in_image(auto_vec) &&
      operand(kSigSpreadAuto, 25) == auto_vec + 4 && operand(kSigSpreadAuto, 41) == auto_vec) {
    spread(Site::kSpreadGun1, plus(hit(kSigSpreadGun, 0), 14), "DC 0D ?? ?? ?? ??");
    spread(Site::kSpreadGun2, plus(hit(kSigSpreadGun, 0), 28), "DC 0D ?? ?? ?? ??");
    spread(Site::kSpreadGun3, plus(hit(kSigSpreadGun, 1), 14), "DC 0D ?? ?? ?? ??");
    spread(Site::kSpreadGun4, plus(hit(kSigSpreadGun, 1), 28), "DC 0D ?? ?? ?? ??");
    spread(Site::kSpreadAuto1, plus(hit(kSigSpreadAuto, 0), 17), "DC 0D ?? ?? ?? ??");
    spread(Site::kSpreadAuto2, plus(hit(kSigSpreadAuto, 0), 34), "DC 0D ?? ?? ?? ??");
    spread(Site::kSpreadAuto3, plus(hit(kSigSpreadAuto, 1), 17), "DC 0D ?? ?? ?? ??");
    spread(Site::kSpreadAuto4, plus(hit(kSigSpreadAuto, 1), 34), "DC 0D ?? ?? ?? ??");
  } else {
    logf("game: the handguns' and machine guns' shots are not the code expected - Steady aim is unavailable");
  }
  if (operand(kSigSpreadShotgun, 7) && operand(kSigSpreadShotgun, 24) == operand(kSigSpreadShotgun, 7)) {
    spread(Site::kSpreadShotgun1, plus(at(kSigSpreadShotgun), 5), "DC 0D ?? ?? ?? ??");
    spread(Site::kSpreadShotgun2, plus(at(kSigSpreadShotgun), 22), "DC 0D ?? ?? ?? ??");
  } else {
    logf("game: a shotgun's centre pellet is not the code expected - Steady aim is unavailable");
  }
  if (operand(kSigSpreadMounted, 7) && operand(kSigSpreadMounted, 21) == operand(kSigSpreadMounted, 7)) {
    spread(Site::kSpreadMounted1, plus(at(kSigSpreadMounted), 5), "D8 0D ?? ?? ?? ??");
    spread(Site::kSpreadMounted2, plus(at(kSigSpreadMounted), 19), "D8 0D ?? ?? ?? ??");
  } else {
    logf("game: the mounted gun's shot is not the code expected - Steady aim is unavailable");
  }
  spread(Site::kSpreadThrow, plus(at(kSigSpreadThrow), 5), "DC 0D ?? ?? ?? ??");

  // The hold: the laser's lookup of the gun's part and each shot's of the hand's
  // become mov eax, a held copy. They all call the one part lookup (thiscall,
  // ret 4), so its push goes with the call; so does a shot's mov ecx,esi (the
  // lookup leaves ecx as it was, and nothing after reads it before setting it).
  if (operand(kSigPlayer, 19) == g_pg_slot) g_player_slot = operand(kSigPlayer, 2);
  if (g_player_slot && !in_image(g_player_slot)) g_player_slot = 0;
  const uintptr_t part = call_target(plus(at(kSigHoldLaser), 20));
  if (!g_player_slot || !part || operand(kSigHoldLaser, 29) != g_pg_slot || !auto_vec ||
      operand(kSigHoldAuto, 13) != auto_vec + 0xC || operand(kSigHoldAuto, 18) != auto_vec + 0xC) {
    logf("game: the laser's and the shots' part lookups are not the code expected - Steady aim is unavailable");
    return;
  }
  auto hold = [&](Site s, uintptr_t site, int call, const char* expect, const HeldPart* held, size_t len) {
    const uintptr_t a = reinterpret_cast<uintptr_t>(held);
    std::vector<uint8_t> mov_eax(len, 0x90);  // mov eax,held; nop...
    mov_eax[0] = 0xB8;
    for (int b = 0; b < 4; ++b) mov_eax[1 + b] = static_cast<uint8_t>(a >> (8 * b));
    prepare_at(s, site && call_target(site + call) == part ? site : 0, expect, mov_eax);
  };
  const char* hand = "6A 0A 8B CE E8 ?? ?? ?? ??";  // push 10; mov ecx,esi; call part
  hold(Site::kHoldLaser, plus(at(kSigHoldLaser), 19), 1, "50 E8 ?? ?? ?? ??", &g_held_gun, 6);
  hold(Site::kHoldHand1, hit(kSigHoldHand, 0), 4, hand, &g_held_hand, 9);
  hold(Site::kHoldHand2, hit(kSigHoldHand, 1), 4, hand, &g_held_hand, 9);
  hold(Site::kHoldAuto1, hit(kSigHoldAuto, 0), 4, hand, &g_held_hand, 9);
  hold(Site::kHoldAuto2, hit(kSigHoldAuto, 1), 4, hand, &g_held_hand, 9);
  hold(Site::kHoldShotgun, at(kSigHoldShotgun), 4, hand, &g_held_hand, 9);
}

// Steady aim's stance. Each weapon's aim (its routine's step 1) starts with a
// set-up that hands the aim's blend helper (FUN_0067e190, thiscall(helper,
// model, motion, aim-up, aim-down, sub-sequence, frames to blend in, mode,
// attribute, first frame)) the idle aim: a loop (attribute 4) on the player,
// blended with the aim-up or aim-down motion (the helper's motion B) by how far
// up or down the aim is, then sets the blend and moves the motion once. The
// loop's breathing moves the gun and the hand however still the aim is. Its
// attribute becomes 0xC: a loop whose clock is stopped (bit 8, which the
// motion's clock (FUN_0065fd80) checks first and the game's own "motion stop"
// (FUN_0065eed0) sets), so it holds its first frame; the blend-in and the blend
// still work. Eleven set-ups in four encodings (the knife's ready stance, a set-
// up like them without the move after it, is left alone), all agreeing on the
// helper, the motion set, the blend, the blend's set and the move.
void prepare_aim_loops() {
  struct Enc {
    int sig, helper, set, blend, helper2, set_blend, move;
  };
  const Enc encs[] = {{kSigAimLoopFar, 33, 37, 44, 50, 57, 64},
                      {kSigAimLoopNear, 27, 31, 38, 44, 51, 58},
                      {kSigAimLoopShotgun, 30, 34, 41, 47, 54, 61},
                      {kSigAimLoopAuto, 38, 42, 49, 55, 62, 69}};
  uintptr_t helper = 0, set = 0, blend = 0, set_blend = 0, move = 0;
  bool agree = true;
  for (const Enc& e : encs) {
    if (!at(e.sig)) {
      agree = false;
      break;
    }
    const uintptr_t h = g_hits[e.sig][0];
    const uintptr_t vals[] = {operand(e.sig, e.helper), call_target(h + e.set), operand(e.sig, e.blend),
                              call_target(h + e.set_blend), call_target(h + e.move)};
    uintptr_t* known[] = {&helper, &set, &blend, &set_blend, &move};
    agree = agree && operand(e.sig, e.helper2) == vals[0];
    for (uintptr_t a : g_hits[e.sig])  // operand() checks every match; the calls are checked here
      agree = agree && call_target(a + e.set) == vals[1] && call_target(a + e.set_blend) == vals[3] &&
              call_target(a + e.move) == vals[4];
    for (int i = 0; i < 5; ++i) {
      agree = agree && vals[i];
      if (!*known[i]) *known[i] = vals[i];
      agree = agree && *known[i] == vals[i];
    }
  }
  if (!agree || !in_image(helper) || g_text.contains(helper) || !in_image(blend)) {
    logf("game: the aims' set-ups are not the code expected - Steady aim is unavailable");
    return;
  }
  g_aim_blend = helper;
  const Site sites[] = {Site::kAimLoopHandgun, Site::kAimLoopWesker, Site::kAimLoopBow,    Site::kAimLoopLaser,
                        Site::kAimLoopBowgun,  Site::kAimLoopGrenade, Site::kAimLoopRed9,  Site::kAimLoopMine,
                        Site::kAimLoopRocket,  Site::kAimLoopShotgun, Site::kAimLoopAuto};
  int n = 0;
  for (const Enc& e : encs)
    for (uintptr_t a : g_hits[e.sig]) prepare_at(sites[n++], a, "6A 04", {0x6A, kMotLoop | kMotStop});
}

// Automatic QTEs. The action buttons' check (cActionButton::checkButton) takes
// the prompt's kind through a jump table; the kinds whose code raises the QTE
// flag are the QTEs: 2 one button, 3 both triggers, 4 two buttons, 9 and 10 one
// trigger, 13 the Wii's (on this version it never answers "pressed", and no
// prompt seen registers it). Kind 13's code raises the flag and answers: its answer
// becomes "pressed", and kinds 2-4 and 9-10 are sent to it - every QTE prompt
// raises the flag as before and is answered "pressed" the first time it is
// checked, so what it does on success happens then. The scenes that poll their
// prompt's buttons themselves are patched where they do.
void prepare_qte_check() {
  const uintptr_t fn = at(kSigQteCheck);
  const uintptr_t table = operand(kSigQteCheck, 51);
  if (!fn || !g_text.contains(table) || table < fn || table - fn > 0x800) {
    logf("game: the action buttons' jump table not found - Automatic QTEs is unavailable");
    return;
  }
  auto entry = [&](int kind) { return static_cast<uintptr_t>(u32_at(table + (kind - 1) * 4)); };
  // Kind 13: pop edi; pop esi; mov byte [flag],1; xor al,al; pop ebx; mov esp,ebp; pop ebp; ret 4.
  const uintptr_t k13 = entry(13);
  const mem::Pattern k13_code = mem::parse_pattern("5F 5E C6 05 ?? ?? ?? ?? 01 32 C0 5B 8B E5 5D C2 04 00");
  if (k13 < fn || k13 >= table || !mem::matches(k13, k13_code)) {
    logf("game: the action buttons' kind 13 is not the code expected - Automatic QTEs is unavailable");
    return;
  }
  const uint32_t flag = u32_at(k13 + 4);
  // Each kind sent there raises the same flag first thing (within its first 24
  // bytes): the proof that it is a QTE kind.
  for (int kind : {2, 3, 4, 9, 10}) {
    const uintptr_t code = entry(kind);
    bool raises = code >= fn && code < table;
    if (raises) {
      raises = false;
      for (int i = 0; i + 7 <= 24 && !raises; ++i)  // mov byte [flag],1
        raises = mem::read<uint16_t>(code + i) == 0x05C6 && u32_at(code + i + 2) == flag &&
                 mem::read<uint8_t>(code + i + 6) == 1;
    }
    if (!raises) {
      logf("game: the action buttons' kind %d does not raise the QTE flag - Automatic QTEs is unavailable", kind);
      return;
    }
  }
  std::vector<uint8_t> to13;
  for (int i = 0; i < 3; ++i)
    for (int b = 0; b < 4; ++b) to13.push_back(static_cast<uint8_t>(k13 >> (8 * b)));
  // xor al,al -> mov al,1
  prepare_at(Site::kQteKind13, k13 + 9, "32 C0", {0xB0, 0x01});
  prepare_at(Site::kQteKinds, table + 4, "?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ??", to13);
  prepare_at(Site::kQteKindsLR, table + 32, "?? ?? ?? ?? ?? ?? ?? ??",
             std::vector<uint8_t>(to13.begin(), to13.begin() + 8));
}

void prepare_qte_polls() {
  // A poll's "test eax,eax" after its key check -> "or al,1": pressed, whatever
  // the check said (and whoever else hooked the call before it, re4_tweaks
  // included: its hook's answer is what comes back in eax).
  const std::vector<uint8_t> pressed = {0x0C, 0x01};
  prepare(Site::kQteBoulder, kSigQteBoulder, 22, "85 C0", pressed);
  prepare(Site::kQteSwim, kSigQteSwim, 22, "85 C0", pressed);
  prepare(Site::kQteRope, kSigQteRope, 26, "85 C0", pressed);
  // The knife on El Gigante's back: three polls, one button table between them.
  const bool gigante = operand(kSigQteGigante, 3) != 0;
  prepare_at(Site::kQteGigante1, gigante ? plus(hit(kSigQteGigante, 0), 24) : 0, "85 C0", pressed);
  prepare_at(Site::kQteGigante2, gigante ? plus(hit(kSigQteGigante, 1), 24) : 0, "85 C0", pressed);
  prepare_at(Site::kQteGigante3, gigante ? plus(hit(kSigQteGigante, 2), 24) : 0, "85 C0", pressed);
  prepare(Site::kQteMinecart, kSigQteMinecart, 22, "85 C0", pressed);
  prepare(Site::kQteStatueRun, kSigQteStatueRun, 26, "85 C0", pressed);
  // The statue's run: a press adds the frames since the last one to the run's
  // counter (its speed is a tenth of it), cut to whole 1/30 s - at 60 fps a press
  // every frame adds ftol(0.5), nothing, and Leon keeps to the slowest run until
  // the statue's swing gets him. The jle past the top goes: a press puts the run
  // at its top speed.
  prepare(Site::kQteStatueSpeed, kSigQteStatueRun, 55, "7E 02", mem::nops(2));
  // The statue's jumps (one trigger, the other, both) all end at one je back to
  // "not yet": without it, the jump.
  prepare(Site::kQteStatueJump, kSigQteStatueJump, 0, "0F 84 ?? ?? FF FF", mem::nops(6));
  prepare(Site::kQteStatueEnd, kSigQteStatueEnd, 29, "85 C0", pressed);
  prepare_at(Site::kQteStatueClimb1, plus(hit(kSigQteStatueClimb, 0), 8), "85 C0", pressed);
  prepare_at(Site::kQteStatueClimb2, plus(hit(kSigQteStatueClimb, 1), 8), "85 C0", pressed);
  prepare(Site::kQteKrauser, kSigQteKrauser, 18, "85 C0", pressed);
  // The laser room: both triggers, or on past the dodge. The first je becomes a
  // jmp to the dodge, where the Wii's own branch (+18) goes as well.
  for (int i = 0; i < 2; ++i) {
    const Site s = i ? Site::kQteLasers2 : Site::kQteLasers1;
    const uintptr_t h = hit(kSigQteLasers, i);
    if (h && h + 20 + mem::read<int8_t>(h + 19) != h + 49) {
      logf("game: %s: the Wii's branch does not go where the dodge was expected - left out", site_name(s));
      continue;
    }
    prepare_at(s, plus(h, 29), "74 ??", {0xEB, 0x12});
  }
  prepare(Site::kQteLasersLast, kSigQteLasersLast, 29, "85 C0", pressed);
  // Saddler's bridges: cmp [right],0; je no; cmp [left],0; jne jump. The je goes,
  // and the jne becomes a jmp (nop; jmp with the same displacement).
  prepare(Site::kQteBridge1, kSigQteBridge, 12, "74 0D", mem::nops(2));
  prepare(Site::kQteBridge2, kSigQteBridge, 21, "0F 85", {0x90, 0xE9});
  prepare_at(Site::kQteBridgeClimb1, plus(hit(kSigQteBridgeClimb, 0), 8), "85 C0", pressed);
  prepare_at(Site::kQteBridgeClimb2, plus(hit(kSigQteBridgeClimb, 1), 8), "85 C0", pressed);
  prepare(Site::kQteRobotGrab, kSigQteRobotGrab, 46, "85 C0", pressed);
  // A grab's shake-off: je no-direction; inc [edi+568] -> add [edi+568],16; nop -
  // sixteen a frame, where the enemies let go at 10 to 90.
  prepare(Site::kQteShake, kSigQteShake, 59, "74 0C FF 87 68 05 00 00",
          {0x83, 0x87, 0x68, 0x05, 0x00, 0x00, 0x10, 0x90});
}

// The enemy manager (cManager<cEm>): +4 its pool's base, +8 the count, +C the
// stride, +14 the list of the live ones (the room's set-up clears the base, the
// count and the list together). The gallery walks that list for its targets, and
// the Mercenaries' kills for the Ganados.
void prepare_em_pool() {
  const uintptr_t count = operand(kSigEmPool, 1);
  if (!count || !in_image(count) || operand(kSigEmPool, 41) != count + 4 || operand(kSigEmPool, 50) != count - 4 ||
      operand(kSigGalleryTargets, 2) != count + 0xC) {
    logf("game: the enemy manager's pool is not where it was expected - Automatic shooting gallery and Kill enemies "
         "as they appear are unavailable");
    return;
  }
  g_em_count_slot = count;
  g_em_stride_slot = count + 4;
  g_em_base_slot = count - 4;
  g_em_list_slot = count + 0xC;
}

// The shooting gallery. Its work's slot, read by the scoring and the gallery's
// loop alike; the target's hit check hands a prop's hit to that scoring.
void prepare_gallery() {
  const uintptr_t work = operand(kSigGalleryScore, 1);
  const uintptr_t score = plus(at(kSigGalleryScore), -3);  // push ebp; mov ebp,esp before it
  if (!work || !in_image(work) || operand(kSigGalleryStreak, 90) != work || !g_pg_slot ||
      operand(kSigGalleryStreak, 2) != g_pg_slot || operand(kSigGalleryStreak, 44) != g_pg_slot ||
      !mem::matches(score, mem::parse_pattern("55 8B EC")) || call_target(plus(at(kSigGalleryHit), 90)) != score) {
    logf("game: the shooting gallery is not the code expected - Automatic shooting gallery is unavailable");
    return;
  }
  // The targets' commands as target_life() reads them (the offsets are in the patterns).
  if (!at(kSigGalleryStep) || !at(kSigGalleryWait) || operand(kSigGalleryWait, 22) != g_pg_slot) {
    logf("game: the gallery targets' commands are not the code expected - Automatic shooting gallery is unavailable");
    return;
  }
  g_gallery_slot = work;
}

// Where a short or near jump at `a` goes (0 when there is none there).
uintptr_t jump_target(uintptr_t a) {
  if (!a) return 0;
  const uint8_t op = mem::read<uint8_t>(a);
  if ((op >= 0x70 && op <= 0x7F) || op == 0xEB) return a + 2 + mem::read<int8_t>(a + 1);
  if (op == 0x0F && (mem::read<uint8_t>(a + 1) & 0xF0) == 0x80) return a + 6 + mem::read<int32_t>(a + 2);
  return 0;
}
// The first place a pattern starts within `n` bytes from `a` (0: none).
uintptr_t find_near(uintptr_t a, size_t n, const char* pattern) {
  if (!a) return 0;
  const mem::Pattern p = mem::parse_pattern(pattern);
  for (size_t i = 0; i < n; ++i)
    if (mem::matches(a + i, p)) return a + i;
  return 0;
}

// The kills that go round LifeDownSet2 (see game.h). The player's: the store of
// 0 goes, and the damage reaction after it (PlDamageSet, the one function all
// four call) knocks the player down instead. The partner's: the jumps her armour
// takes are taken whatever she wears, and a close heavy hit takes the branch a
// far one does (she ducks: routine 7).
void prepare_kills() {
  const uintptr_t k1 = hit(kSigKillFlag, 0), k2 = hit(kSigKillFlag, 1), rock = at(kSigCrushRock);
  uintptr_t react = 0;  // PlDamageSet: what each of these calls once the HP is zeroed
  bool one_react = true;
  for (uintptr_t call : {plus(k1, 26), plus(k2, 27), plus(rock, 24)}) {
    const uintptr_t t = call_target(call);
    one_react = one_react && !(t && react && t != react);
    if (t) react = t;
  }
  if (!one_react) {
    logf("game: the player's kills do not go on to one damage reaction - God mode leaves them be");
    react = 0;
  }
  if (operand(kSigKillFlag, 8) == g_pg_slot && react && call_target(plus(k1, 26)) == react &&
      call_target(plus(k2, 27)) == react) {
    prepare_at(Site::kKillFlag1, plus(k1, 14), "66 89 ?? B4 4F 00 00", mem::nops(7));
    prepare_at(Site::kKillFlag2, plus(k2, 14), "66 89 ?? B4 4F 00 00", mem::nops(7));
  }
  if (operand(kSigCrushRock, 7) == g_pg_slot && react && call_target(plus(rock, 24)) == react)
    prepare(Site::kCrushRock, kSigCrushRock, 17, "66 89 81 B4 4F 00 00", mem::nops(7));
  const uintptr_t truck = at(kSigCrushTruck);
  const uintptr_t truck_react = find_near(plus(truck, 20), 0x60, "6A 08 E8");  // push 8; call PlDamageSet
  if (operand(kSigCrushTruck, 1) == g_pg_slot && react && truck_react && call_target(truck_react + 2) == react)
    prepare(Site::kCrushTruck, kSigCrushTruck, 13, "66 89 90 B4 4F 00 00", mem::nops(7));

  // The partner's heavy hit: nearer than the radius (a float, 3000 squared) the
  // jp goes to the 9999 damage and the fall; past it, routine 7 (ducking).
  const uintptr_t blast = at(kSigAshleyBlast);
  const uintptr_t radius = operand(kSigAshleyBlast, 2);
  if (radius && in_image(radius) && mem::read<float>(radius) == 9000000.0f &&
      find_near(jump_target(plus(blast, 17)), 1, "57 53 53 68 0F 27 00 00"))  // ...; push 9999 (LifeDownSet2)
    prepare(Site::kAshleyBlast, kSigAshleyBlast, 17, "7A ??", mem::nops(2));
  // El Gigante's stomp: the armour's je goes where the "already hit" jne does,
  // past the zeroing (je -> nop; jmp, the same rel32).
  const uintptr_t stomp = at(kSigAshleyStomp);
  if (operand(kSigAshleyStomp, 14) == g_pg_slot && jump_target(plus(stomp, 25)) &&
      jump_target(plus(stomp, 25)) == jump_target(plus(stomp, 7)))
    prepare(Site::kAshleyStomp, kSigAshleyStomp, 25, "0F 84", {0x90, 0xE9});
  // The truck, twice: past the armour's jne its heavy hit on her (push 13), and at
  // the jne's end the zeroing of her HP.
  if (operand(kSigAshleyTruck, 1) == g_pg_slot) {
    for (int i = 0; i < 2; ++i) {
      const uintptr_t t = hit(kSigAshleyTruck, i);
      const Site s = i ? Site::kAshleyTruck2 : Site::kAshleyTruck1;
      if (find_near(plus(t, 14), 0x40, "6A 13 50 50") && find_near(jump_target(plus(t, 12)), 12, "66 89 ?? B8 4F 00 00"))
        prepare_at(s, plus(t, 12), "75 ??", mem::nops(2));
      else
        logf("game: %s: the armour's path or the zeroing is not where it was expected - left out", site_name(s));
    }
  }
  // A rock: the armour's je goes where the others that let her be do (je -> jmp).
  const uintptr_t stone = at(kSigAshleyRock);
  if (stone && jump_target(stone + 16) == jump_target(stone + 7))
    prepare(Site::kAshleyRock, kSigAshleyRock, 16, "74 ??", {0xEB, mem::read<uint8_t>(stone + 17)});
}

// One hit kills on El Gigante (em 2B), whose hit check (FUN_004b07c0, eax = the
// enemy, from its move) takes its damage without LifeDownSet2. Past the hit's
// effects it asks whether the part hit (+340, kept at [ebp-8]) is the parasite
// (3F). A hit anywhere else takes no health: its damage ([ebp-4]) adds to the
// stagger, and 1000 of it drop El Gigante to its knees with the parasite out.
// One on the parasite takes twice the damage (none for a flash, 17 or 2A, and
// all of it for the P.R.L. 412, 2E). Health below 1 is the death (+FC = 3) from
// whatever El Gigante is doing - a rocket's hit zeroes it on any part (its case
// in the effects' switch), and a grab lets Leon or Ashley go when it sees it.
// The 42 bytes from the part's test become: a hit with damage, but a flash's,
// takes the P.R.L.'s store (+42, the health zeroed) and goes on as the
// parasite's does (+51), to the death; any other goes where the game sends it
// (the body's check, or on past the parasite's subtraction, which takes nothing
// off those). The knife on its back takes 70 a slash (the player's callback
// through the climb), and the climb ends in the death if the health is below 1
// by then: the slash's add [gigante+324],-70 becomes and [gigante+324],0.
void prepare_gigante() {
  const uintptr_t g = at(kSigHurtGigante);
  const uintptr_t body = jump_target(plus(g, 6));  // cmp [esi+324],0; jg on - and below it the death
  if (g && body == g + 0x72 && jump_target(g + 61) == body - 14 && jump_target(g + 71) == body + 10 &&
      mem::matches(body - 14, mem::parse_pattern("66 83 BE 24 03 00 00 00 0F 8F")) &&
      mem::matches(body, mem::parse_pattern("66 83 BE 24 03 00 00 00 7F")) &&
      find_near(body + 10, 0x30, "C7 86 FC 00 00 00 03 00 00 00")) {
    const std::vector<uint8_t> all = {
        0x5F, 0x5B,                          // pop edi; pop ebx (the part's test made its compare before them)
        0x8A, 0x86, 0x2E, 0x03, 0x00, 0x00,  // mov al,[esi+32E] (the weapon)
        0x3C, 0x17, 0x74, 0x0C,              // cmp al,17; je keep
        0x3C, 0x2A, 0x74, 0x08,              // cmp al,2A; je keep
        0x83, 0x7D, 0xFC, 0x00, 0x7E, 0x02,  // cmp dword [ebp-4],0; jle keep
        0xEB, 0x12,                          // jmp +42: xor ecx,ecx; mov [esi+324],cx
        0x8B, 0x4D, 0xF8,                    // keep: mov ecx,[ebp-8]
        0x66, 0x83, 0x79, 0x26, 0x3F,        // cmp word [ecx+26],3F
        0x75, 0x50,                          // jne the body's
        0xEB, 0x0F,                          // jmp +51
        0x90, 0x90, 0x90, 0x90, 0x90, 0x90};
    prepare_at(Site::kHurtGigante, g,
               "66 39 5F 26 5F 5B 75 6A 8A 86 2E 03 00 00 3C 17 74 21 3C 2A 74 1D 3C 2E 74 10 8B 45 FC F7 D8 03 C0 "
               "66 01 86 24 03 00 00 EB 09",
               all);
  } else if (g) {
    logf("game: El Gigante's hit check does not go on to its death the way expected - One hit kills leaves it be");
  }
  prepare(Site::kHurtGiganteKnife, kSigHurtGiganteKnife, 9, "66 83 80 24 03 00 00 BA",
          {0x66, 0x83, 0xA0, 0x24, 0x03, 0x00, 0x00, 0x00});
}

// One hit kills on Krauser (em 39), whose hit check (FUN_00548200, eax = the
// enemy) takes a hit's damage (FUN_00546ba0: the damage table's, twice on the
// head, and 9999 for the P.R.L. 412 on him mutated) through LifeDownSet2 - with
// "survive at 1" unless he is mutated (types 2 and 3). The first part of his
// fight (room 31C, type 1) the game ends its own way, and what beats him there
// is his own count of the damage taken: a stage of it (+AA4, 1000 the first and
// 500 after, reset as he hides) holding its share when a flinch ends (the flinch
// a hit sets off: +AA0 past 200, a head shot, an explosion) sends him off to
// hide, and in his last stage he stays hidden. Killing him there would leave
// him standing at 0 (the room script only waits for the mutated one's death).
// So the damage function's tail - the P.R.L.'s rule - becomes "any damage at
// all is at least 1000": he flinches and retreats at the first hit with damage,
// and mutated he dies of it as before (the P.R.L.'s 9999 or this, it kills
// him). A hit on his armoured arm, mutated, glances off before any damage is
// worked out (FUN_005479d0); the je past it becomes jmp, so it counts as a hit
// anywhere else does.
void prepare_krauser() {
  const uintptr_t k = at(kSigHurtKrauser);
  if (!k) return;
  const uintptr_t arm = call_target(k + 38);
  const uintptr_t to_damage = jump_target(k + 45);
  const uintptr_t damage = call_target(to_damage);
  // The damage, then LifeDownSet2(Krauser, damage, 0, survive): push 1 unless the type is 2 or 3.
  const bool survive = find_near(to_damage, 1, "E8 ?? ?? ?? ?? 8B F8 8A 86 01 01 00 00 3C 02 74 ?? 3C 03 74 ?? 6A 01 "
                                               "EB ?? 53 53 57 56 E8") != 0;
  const uintptr_t lds = survive ? call_target(to_damage + 29) : 0;
  const uintptr_t enemy = at(kSigHurtEnemy);  // LifeDownSet2's own enemy branch
  const char* tail = "80 BE 2E 03 00 00 2E 75 15 8A 8E 01 01 00 00 80 F9 02 74 05 80 F9 03 75 05 B8 0F 27 00 00 C3";
  const uintptr_t prl = find_near(damage, 0x60, tail);
  if (!arm || !mem::matches(arm, mem::parse_pattern("8A 81 01 01 00 00 3C 02 74 ?? 3C 03")) || !prl || !lds ||
      !(enemy > lds && enemy < lds + 0x500)) {
    logf("game: Krauser's hit check does not take its damage the way expected - One hit kills leaves him be");
    return;
  }
  std::vector<uint8_t> least = {
      0x85, 0xC0,                    // test eax,eax
      0x7E, 0x0C,                    // jle ret (no damage: as it is)
      0x3D, 0xE8, 0x03, 0x00, 0x00,  // cmp eax,1000
      0x7D, 0x05,                    // jge ret
      0xB8, 0xE8, 0x03, 0x00, 0x00,  // mov eax,1000
      0xC3};                         // ret
  least.resize(31, 0x90);
  prepare_at(Site::kHurtKrauser, prl, tail, least);
  prepare(Site::kHurtKrauserArm, kSigHurtKrauser, 45, "74 ??", {0xEB, mem::read<uint8_t>(k + 46)});
}

// The Mercenaries. A kill scores in one place (FUN_0070f520, the Mercenaries
// only), on their work's fields by address: the score, the combo and the first
// kill's timer (+24, +28, +30 of the work the HUD is handed). The HUD
// (FUN_00710c10, each frame, esi = the work) counts the combo's two timers down -
// the first kill's (+30), and from the second kill on the window each kill starts
// again (+2C) - and either running out ends the combo; what the combo has earned
// (+48) goes onto the score once it is over. Endless combo takes out the two
// subtractions, and the jne that waits for the combo to be over (nothing earned
// skips the same code, past a jle to the same place). The window is the score's
// constant (300 frames), the blink mark the next dword (120). The Bonus Points
// time (+54) is counted down in the same way, its blink mark the dword after
// (120), and what a combo has earned waits for it to be over too - a je after the
// jne, to the same place: Bonus time always on takes out its subtraction and that
// je.
void prepare_merc() {
  const uintptr_t score = operand(kSigMercScore, 32), window = operand(kSigMercScore, 52);
  if (!at(kSigMercScore) || !g_pg_slot || operand(kSigMercScore, 1) != g_pg_slot || !in_image(score) ||
      operand(kSigMercScore, 43) != score + 4 || operand(kSigMercScore, 63) != score + 0xC || !in_image(window) ||
      mem::read<int32_t>(window) <= 0) {
    logf("game: the Mercenaries' scoring is not the code expected - their cheats are unavailable");
    return;
  }
  g_merc_work = score - 0x24;
  const uintptr_t first = at(kSigMercFirst), win = at(kSigMercWindow), bonus = at(kSigMercBonus);
  if (first && win && bonus && first < win && win < bonus && bonus - first < 0x800 &&
      operand(kSigMercFirst, 13) == g_pg_slot && operand(kSigMercWindow, 35) == g_pg_slot &&
      operand(kSigMercWindow, 50) == window + 4 && jump_target(bonus + 17) &&
      jump_target(bonus + 17) == jump_target(bonus + 26)) {
    prepare_at(Site::kMercFirst, first + 20, "D8 60 70", mem::nops(3));
    prepare_at(Site::kMercWindow, win + 39, "D8 61 70", mem::nops(3));
    prepare_at(Site::kMercBonus, bonus + 26, "0F 85 ?? ?? ?? ??", mem::nops(6));
  } else {
    logf("game: the Mercenaries' combo timers are not the code expected - Endless combo is unavailable");
  }
  const uintptr_t time = at(kSigMercBonusTime);
  if (bonus && time && time < bonus && bonus - time < 0x400 && jump_target(time + 23) == bonus &&
      operand(kSigMercBonusTime, 30) == g_pg_slot && operand(kSigMercBonusTime, 48) == window + 8 &&
      call_target(bonus + 36) && jump_target(bonus + 17) && jump_target(bonus + 43) == jump_target(bonus + 17)) {
    prepare_at(Site::kMercBonusTime, time + 37, "D8 60 70", mem::nops(3));
    prepare_at(Site::kMercBonusBank, bonus + 43, "0F 84 ?? ?? ?? ??", mem::nops(6));
  } else {
    logf("game: the Mercenaries' Bonus Points time is not the code expected - Bonus time always on is unavailable");
  }
}

// The Ganados' class (cEm10, every enemy the Mercenaries score): its vtable's
// move (+10, through its thunk) runs its hit check once past its set-up (cmp byte
// [esi+FC],0; je; mov eax,esi; call), and the hit check takes a hit's damage
// through LifeDownSet2 - the one kSigHurtEnemy is in.
void prepare_em10() {
  const uintptr_t vt = operand(kSigEm10, 7);
  uintptr_t move = vt && in_image(vt) && !g_text.contains(vt) ? mem::read<uintptr_t>(vt + 0x10) : 0;
  if (move && g_text.contains(move) && mem::read<uint8_t>(move) == 0xE9) move = move + 5 + mem::read<int32_t>(move + 1);
  const uintptr_t call = g_text.contains(move) ? find_near(move, 0x40, "80 BE FC 00 00 00 00 74 07 8B C6 E8") : 0;
  const uintptr_t hit = call_target(plus(call, 11)), damage = at(kSigEm10Damage);
  const uintptr_t lds = call_target(plus(damage, 15)), enemy = at(kSigHurtEnemy);
  if (!hit || hit != at(kSigEm10Hit) || !(damage > hit && damage < hit + 0x900) || !lds ||
      !(enemy > lds && enemy < lds + 0x500)) {
    logf("game: the Ganados' class and its hit check are not the code expected - Kill enemies as they appear is "
         "unavailable");
    return;
  }
  g_em10_vtable = vt;
}

// The scene's area triggers (FUN_006bdd20): each frame, unless pG+170 bit 400000
// holds them, the room's area flags (pG+17C, +180) are cleared, and every area
// the player stands in sets its own again (FUN_006b3b60's or). The Mercenaries'
// stages send their enemies from the lists of the areas whose flag is up.
void prepare_merc_areas() {
  if (!at(kSigAreaReset) || !g_pg_slot || operand(kSigAreaReset, 1) != g_pg_slot ||
      operand(kSigAreaReset, 28) != g_pg_slot || operand(kSigAreaReset, 45) != g_pg_slot) {
    logf("game: the scene's area triggers are not the code expected - the Mercenaries' stages send their enemies as "
         "the game does");
    return;
  }
  prepare(Site::kMercAreas, kSigAreaReset, 37, "89 99 7C 01 00 00", mem::nops(6));
}

// The models' memory (game.h): modelInit's model pool and a model's parts pool,
// each read through the slot the game reads its manager from (the sub-screens
// put their own managers there while they are up).
void prepare_models() {
  const uintptr_t models = operand(kSigModelInfos, 2), parts = operand(kSigModelParts, 8);
  if (!at(kSigModelInfos) || !at(kSigModelParts) || !in_image(models) || parts != models + 4) {
    logf("game: the models' pools are not the code expected - Kill enemies as they appear does not watch the models' "
         "memory");
    return;
  }
  g_models_slot = models;
  g_parts_slot = parts;
}

// The Mercenaries' village (room 400): its function each frame counts the frames
// since its sweep in its work's +C and past 300 sweeps - cEmMgr::destroy on every
// Ganado whose body is gone (vtable +48, ckResetEnable).
void prepare_village() {
  const uintptr_t a = at(kSigVillageSweep);
  const uintptr_t sweep = a ? call_target(a + 69) : 0;
  const uintptr_t test = sweep ? find_near(sweep, 0x80, "8B 16 8B 42 48 8B CE FF D0 85 C0 74 ?? 56 B9") : 0;
  if (!a || !g_pg_slot || operand(kSigVillageSweep, 28) != g_pg_slot || operand(kSigVillageSweep, 44) != g_pg_slot ||
      mem::read<uint32_t>(a + 60) != 300 || !test || !g_em_count_slot ||
      mem::read<uintptr_t>(test + 15) != g_em_count_slot - 8 || !in_image(operand(kSigVillageSweep, 2))) {
    logf("game: the Mercenaries' village is not the code expected - its bodies go at the game's pace");
    return;
  }
  g_village_slot = operand(kSigVillageSweep, 2);
}

// The village's pickups' events (kSigVillageEvents): each event's function sends
// its entries one by one, each only while fewer than 10 are about - cmp dword
// [the work],0A (83 38 0A), a jae after it - and the event is spent once fired.
// With every area sending, ten are about whenever a list has an entry left, so
// an event fired then sends nothing, ever. One patch per function covers its
// checks, each 0A made FF (cmp [work],-1: the jae is never taken). Each function
// starts by reading the village's work, and has as many checks as it sends
// entries (push -1; push entry; lea ecx,[its wrap]; call setEm).
// The entries a stage's sending function sends: each setEm's entry, pushed last
// (FUN_008d7740(entry, -1, 1, 1, 1)): push -1; push entry (an imm8, an imm32 from
// 0x80 on, or a register); lea ecx,[ebp-n] (its wrap); call - every call to the same place
// (`*setem`, 0 for the first function read). Up to the function's end (ret, then
// int3). How many sends it has, and how many push their entry from a register
// (not read); -1 if it is not that shape.
int sent_entries(uintptr_t f, uint32_t out[8], int* by_register, uintptr_t* setem) {
  int n = 0;
  *by_register = 0;
  if (!f) return -1;
  for (uintptr_t p = f; p < f + 0x1000; ++p) {
    if (mem::read<uint8_t>(p) == 0xC3 && mem::read<uint8_t>(p + 1) == 0xCC) return n;
    if (mem::read<uint8_t>(p) != 0x6A || mem::read<uint8_t>(p + 1) != 0xFF) continue;
    uintptr_t q = p + 2;
    int entry = -1;
    if (mem::read<uint8_t>(q) == 0x6A) {
      entry = mem::read<uint8_t>(q + 1);
      q += 2;
    } else if (mem::read<uint8_t>(q) == 0x68 && mem::read<uint32_t>(q + 1) < 0x100) {
      entry = static_cast<int>(mem::read<uint32_t>(q + 1));
      q += 5;
    } else if ((mem::read<uint8_t>(q) & 0xF8) == 0x50) {
      q += 1;
    } else {
      continue;
    }
    if (mem::read<uint8_t>(q) != 0x8D) continue;
    if (mem::read<uint8_t>(q + 1) == 0x4D) q += 3;
    else if (mem::read<uint8_t>(q + 1) == 0x8D) q += 6;
    else continue;
    if (mem::read<uint8_t>(q) != 0xE8) continue;
    const uintptr_t target = call_target(q);
    if (!*setem) *setem = target;
    if (!target || target != *setem) return -1;
    ++n;
    if (entry >= 0) out[entry >> 5] |= 1u << (entry & 31);
    else ++*by_register;
  }
  return -1;  // no end found
}

// The village's seven area lists: its function (kSigVillageSweep sits 0x20 into
// it: mov eax,[pG]; test [eax+501C],1000; jne) calls each when its area flag is
// up - test [eax+17C],reg or imm32; je; call.
void prepare_village_lists() {
  const uintptr_t sweep = at(kSigVillageSweep);
  const uintptr_t f = sweep ? sweep - 0x20 : 0;
  if (!f || !g_village_slot || !mem::matches(f, mem::parse_pattern("A1 ?? ?? ?? ?? F7 80 1C 50 00 00 00 10 00 00 0F 85")) ||
      u32_at(f + 1) != g_pg_slot) {
    logf("game: the village's area lists are not the code expected - its pickups' Ganados come only with their pickups");
    return;
  }
  const mem::Pattern by_reg = mem::parse_pattern("85 ?? 7C 01 00 00 74 ?? E8"),
                     by_imm = mem::parse_pattern("F7 80 7C 01 00 00 ?? ?? ?? ?? 74 ?? E8");
  uint32_t area[8] = {};
  uintptr_t setem = 0;
  int lists = 0, sends = 0, registers = 0;
  for (uintptr_t p = f; p < f + 0x300; ++p) {
    uintptr_t call = 0;
    if (mem::matches(p, by_reg)) call = p + 8;
    else if (mem::matches(p, by_imm)) call = p + 12;
    if (!call) continue;
    int by_register = 0;
    const int n = sent_entries(call_target(call), area, &by_register, &setem);
    if (n <= 0) {
      logf("game: the village's area list %d is not the code expected", lists + 1);
      return;
    }
    ++lists;
    sends += n;
    registers += by_register;
  }
  uint32_t events[8] = {};
  if (const uintptr_t a = at(kSigVillageEvents))
    for (const uintptr_t call : {a + 3, a + 80, a + 165}) {
      int by_register = 0;
      if (sent_entries(call_target(call), events, &by_register, &setem) <= 0) {
        logf("game: the village's pickups' events are not the code expected - their Ganados come only with them");
        return;
      }
    }
  int area_n = 0, event_n = 0, both = 0;
  for (int w = 0; w < 8; ++w) {
    area_n += __builtin_popcount(area[w]);
    event_n += __builtin_popcount(events[w]);
    both += __builtin_popcount(area[w] & events[w]);
  }
  if (lists != 7 || !area_n || !event_n || both) {
    logf("game: the village's lists are not the code expected (%d area lists, %d entries, %d events' entries, %d in "
         "both) - its pickups' Ganados come only with their pickups", lists, area_n, event_n, both);
    return;
  }
  std::memcpy(g_village_area, area, sizeof(area));
  std::memcpy(g_village_events, events, sizeof(events));
  g_village_lists = true;
  logf("game: the village's area lists send %d entries (%d sends, %d of them by a register), its pickups' events %d",
       area_n, sends, registers, event_n);
}

// The village's chainsaws' waves (kSigVillageWaves, the end of its function),
// each once: when the work's tries less those about reach 10, 25 or 35 and the
// wave's flag is not up, FUN_004ab510(n) puts it up (pG+174, bit 80000000 >> n)
// and FUN_007d0600 sends two entries, E and E + 1 (esi; mov edi,0; lea
// esi,[edi+E]; then lea edi,[esi+1-E]; lea esi,[edi+E]), whatever is about: the
// entry's sent bit and dead mark cleared, setEm, moved to the chainsaws' spot.
// One setEm refuses (no free slot) is left not sent, and no wave goes again.
void prepare_village_waves() {
  struct Layout {
    int start, pg, flag, n, set, entry, send1, disp, entry2, send2;
  };
  constexpr Layout kWaves[3] = {{0, 21, 31, 38, 39, 51, 52, 59, 62, 63},
                                {68, 89, 99, 106, 107, 119, 120, 127, 130, 131},
                                {136, 156, 166, 173, 174, 186, 187, 194, 197, 198}};
  const uintptr_t a = at(kSigVillageWaves);
  VillageWave waves[3];
  uintptr_t set = 0, send = 0;
  bool ok = a && g_village_slot && g_pg_slot && g_village_lists;
  for (int k = 0; ok && k < 3; ++k) {
    const Layout& l = kWaves[k];
    const uint32_t flag = u32_at(a + l.flag);
    const int n = mem::read<uint8_t>(a + l.n), e = mem::read<uint8_t>(a + l.entry);
    if (!set) set = call_target(a + l.set);
    if (!send) send = call_target(a + l.send1);
    ok = u32_at(a + l.start + 1) == g_village_slot && u32_at(a + l.pg) == g_pg_slot && n < 32 &&
         flag == 0x80000000u >> n && mem::read<uint8_t>(a + l.entry2) == e && mem::read<int8_t>(a + l.disp) == 1 - e &&
         e < 255 && set && call_target(a + l.set) == set && send && call_target(a + l.send1) == send &&
         call_target(a + l.send2) == send;
    for (const int i : {e, e + 1})
      ok = ok && !(g_village_area[i >> 5] & (1u << (i & 31))) && !(g_village_events[i >> 5] & (1u << (i & 31)));
    waves[k] = VillageWave{flag, e};
  }
  if (!ok) {
    logf("game: the village's chainsaws' waves are not the code expected - a chainsaw refused as one goes is lost");
    return;
  }
  std::memcpy(g_village_waves, waves, sizeof(waves));
  g_village_waves_known = true;
  logf("game: the village's chainsaws' waves send entries %d and %d, %d and %d, %d and %d", waves[0].entry,
       waves[0].entry + 1, waves[1].entry, waves[1].entry + 1, waves[2].entry, waves[2].entry + 1);
}

// The castle's spawner's counted sends (kSigCastleTask, up to its padding), as
// check_sigs.py reads them: cmp dword [reg+counter],cap (83 B8+r), then the wrap
// into ecx right before the call of the wraps' in-use test (FUN_008d78d0) - mov
// ecx,reg or mov ecx,[the work's slot] [add ecx,wrap x 0C], or lea ecx,[reg+wrap
// x 0C] - or push cap; add esi,counter; push entry; mov edi,wrap; call
// FUN_007d4ff0 (the tail's sends: the same test, then the send). Grouped by
// counter; 0 if the code is not that shape (a cap or a wrap that does not agree,
// more than one in-use test or tail).
int castle_groups(uintptr_t f, CastleGroup* out, int max) {
  auto b = [&](uintptr_t i) { return mem::read<uint8_t>(f + i); };
  auto d = [&](uintptr_t i) { return mem::read<uint32_t>(f + i); };
  uintptr_t len = 0;
  while (len < 0x2000 && !(b(len) == 0xCC && b(len + 1) == 0xCC && b(len + 2) == 0xCC)) ++len;
  if (len >= 0x2000) return 0;
  uintptr_t inuse = 0, tail = 0;
  int n = 0;
  auto add = [&](uint32_t counter, int cap, uint32_t wrap) {
    if (wrap >= static_cast<uint32_t>(kCastleWraps)) return false;
    int g = 0;
    while (g < n && out[g].counter != static_cast<int>(counter)) ++g;
    if (g == n) {
      if (n == max) return false;
      out[n++] = CastleGroup{static_cast<int>(counter), cap, 0, {}};
    }
    CastleGroup& grp = out[g];
    if (grp.cap != cap || grp.n == static_cast<int>(sizeof(grp.wraps))) return false;
    for (int i = 0; i < grp.n; ++i)
      if (grp.wraps[i] == wrap) return false;
    grp.wraps[grp.n++] = static_cast<uint8_t>(wrap);
    return true;
  };
  auto mov_ecx = [&](uintptr_t e) {  // mov ecx,reg or mov ecx,[slot] ending at e
    return (b(e - 2) == 0x8B && (b(e - 1) & 0xF8) == 0xC8) || (b(e - 6) == 0x8B && b(e - 5) == 0x0D);
  };
  for (uintptr_t p = 8; p + 16 < len; ++p) {
    if (b(p) == 0x83 && (b(p + 1) & 0xF8) == 0xB8 && (b(p + 1) & 7) != 4) {
      const uint32_t counter = d(p + 2);
      if (counter < 0x500 || counter >= 0x600 || (counter & 3)) continue;
      for (uintptr_t q = p + 7; q < p + 0x48 && q + 5 < len; ++q) {
        if (b(q) != 0xE8) continue;
        int64_t disp = -1;
        if (b(q - 6) == 0x81 && b(q - 5) == 0xC1 && mov_ecx(q - 6)) disp = d(q - 4);
        else if (b(q - 3) == 0x83 && b(q - 2) == 0xC1 && mov_ecx(q - 3)) disp = b(q - 1);
        else if (b(q - 6) == 0x8D && (b(q - 5) & 0xF8) == 0x88) disp = d(q - 4);
        else if (b(q - 3) == 0x8D && (b(q - 2) & 0xF8) == 0x48) disp = b(q - 1);
        else if (b(q - 2) == 0x8B && (b(q - 1) & 0xF8) == 0xC8) disp = 0;
        if (disp < 0) continue;
        const uintptr_t target = call_target(f + q);
        if (!inuse) inuse = target;
        if (!target || target != inuse || disp % 0xC || !add(counter, b(p + 6), static_cast<uint32_t>(disp / 0xC)))
          return 0;
        break;
      }
    } else if (b(p) == 0x6A && b(p + 2) == 0x81 && b(p + 3) == 0xC6) {
      const uint32_t counter = d(p + 4);
      uintptr_t q = p + 8;
      q += b(q) == 0x6A ? 2 : b(q) == 0x68 ? 5 : 0;
      if (counter < 0x500 || counter >= 0x600 || b(q) != 0xBF || b(q + 5) != 0xE8) continue;
      const uintptr_t target = call_target(f + q + 5);
      if (!tail) tail = target;
      if (!target || target != tail || !add(counter, b(p + 1), d(q + 1))) return 0;
    }
  }
  return inuse && tail ? n : 0;
}

// The Mercenaries' castle (room 402): its sweep (kSigCastleSweep: every 300 of its
// timer, its work's +5A4), its spawner (kSigCastleTask, started by its set-up -
// kSigCastleEvents +9) and its counted groups; the set-up's two area triggers
// given events (kSigCastleEvents: their numbers and events) and the scene's list
// they are in (kSigAtFind).
void prepare_castle() {
  const uintptr_t sweep = at(kSigCastleSweep), task = at(kSigCastleTask), events = at(kSigCastleEvents);
  const int timer = sweep ? static_cast<int>(operand(kSigCastleSweep, 16)) : 0;
  bool ok = sweep && task && events && g_pg_slot && operand(kSigCastleSweep, 1) == g_pg_slot && timer > 0 &&
            timer < 0x1000 && !(timer & 3);
  for (const int off : {22, 34, 49}) ok = ok && static_cast<int>(operand(kSigCastleSweep, off)) == timer;
  const uintptr_t slot = ok ? operand(kSigCastleSweep, 7) : 0, every = ok ? operand(kSigCastleSweep, 28) : 0;
  ok = ok && in_image(slot) && in_image(every) && operand(kSigCastleTask, 17) == slot && u32_at(events + 9) == task;
  CastleGroup groups[kCastleGroups];
  const int n = ok ? castle_groups(task, groups, kCastleGroups) : 0;
  if (n <= 0) {
    logf("game: the Mercenaries' castle is not the code expected - a send its pool refuses stays lost, its bodies go "
         "at its pace, its gates are the player's to open");
    return;
  }
  g_castle_slot = slot;
  g_castle_timer = timer;
  g_castle_every = mem::read<float>(every);
  std::memcpy(g_castle_groups, groups, sizeof(groups));
  g_castle_group_n = n;
  int sends = 0, wraps = 0;
  for (int g = 0; g < n; ++g) {
    sends += groups[g].cap;
    wraps += groups[g].n;
  }
  logf("game: the Mercenaries' castle counts %d groups' sends (%d from %d wraps); its sweep every %.0f", n, sends,
       wraps, g_castle_every);
  const uintptr_t at_slot = at(kSigAtFind) ? operand(kSigAtFind, 4) : 0;
  const uintptr_t set = call_target(events + 40);
  const CastleGate gates[2] = {{mem::read<uint8_t>(events + 39), u32_at(events + 30)},
                               {mem::read<uint8_t>(events + 59), u32_at(events + 50)}};
  if (!in_image(at_slot) || !set || set != call_target(events + 60) || gates[0].at == gates[1].at ||
      !g_text.contains(gates[0].event) || !g_text.contains(gates[1].event)) {
    logf("game: the castle's gates are not the code expected - they are the player's to open");
    return;
  }
  g_at_slot = at_slot;
  std::memcpy(g_castle_gates, gates, sizeof(gates));
  g_castle_gates_known = true;
  logf("game: the castle's gates are its area triggers %02X and %02X", gates[0].at, gates[1].at);
}

// A function's length up to its padding (three int3), 0 past `limit`.
uintptr_t padded_length(uintptr_t f, uintptr_t limit) {
  uintptr_t n = 0;
  while (n < limit && !(mem::read<uint8_t>(f + n) == 0xCC && mem::read<uint8_t>(f + n + 1) == 0xCC &&
                        mem::read<uint8_t>(f + n + 2) == 0xCC))
    ++n;
  return n < limit ? n : 0;
}

// The water stage's code, as check_sigs.py reads it (game.h, MercWater). The
// enemy list's entries' flags: GLOBAL_WK +5410, 0x20 bytes an entry, bit 2 sent.
constexpr uint32_t kWaterList = 0x5410, kWaterEntry = 0x20;

// FUN_006b4da0, a trigger's "inside" bit: [the triggers' work]+10, a bit a trigger.
bool water_inside_test(uintptr_t f) {
  static const mem::Pattern p = mem::parse_pattern(
      "55 8B EC 8B 45 08 8B C8 83 E1 1F BA 00 00 00 80 D3 EA 8B 0D ?? ?? ?? ?? C1 E8 05 85 54 81 10 0F 95 C0 5D C3");
  return f && g_at_slot && mem::matches(f, p) && u32_at(f + 20) == g_at_slot;
}

// FUN_00682420, the player's routine as a bit: r_no_0 through a table, r_no_0 0's
// r_no_1 through a second, each case a mov eax,bit. The r_no_1 values whose case
// is 0x40000 - the bit the stage's sends skip on (a ladder) - a bit each; 0 if the
// code is not that shape or another r_no_0 has the bit.
uint32_t water_ladder(uintptr_t f) {
  static const mem::Pattern p = mem::parse_pattern(
      "8B 15 ?? ?? ?? ?? 0F B6 8A FC 00 00 00 33 C0 83 F9 04 0F 87 ?? ?? ?? ?? FF 24 8D ?? ?? ?? ?? 0F B6 8A FD 00 00 00 "
      "83 F9 12 0F 87 ?? ?? ?? ?? FF 24 8D");
  if (!f || !g_player_slot || !mem::matches(f, p) || u32_at(f + 2) != g_player_slot) return 0;
  static const mem::Pattern bit = mem::parse_pattern("B8 00 00 04 00");
  const uintptr_t t0 = u32_at(f + 27), t1 = u32_at(f + 50);
  if (!g_text.contains(t0) || !g_text.contains(t1) || u32_at(t0) != f + 31) return 0;
  for (int r0 = 1; r0 <= 4; ++r0)
    if (!g_text.contains(u32_at(t0 + 4 * r0)) || mem::matches(u32_at(t0 + 4 * r0), bit)) return 0;
  uint32_t mask = 0;
  for (int r1 = 0; r1 <= 0x12; ++r1) {
    const uintptr_t c = u32_at(t1 + 4 * r1);
    if (!g_text.contains(c)) return 0;
    if (mem::matches(c, bit)) mask |= 1u << r1;
  }
  return mask;
}

// A zone's task: push its area; call the inside test - then five sends, each:
// cmp dword [eax],0A; jae (fewer than 10 about), call FUN_00682420; test eax,40000
// (not on a ladder), test byte [the entry's flags],2 (sent?) - push entry before
// setEm and before the look-up of its Ganado - and add dword [eax+counter],ebx;
// at the end cmp dword [eax+counter],ends; jae (the task returns).
bool water_zone(uintptr_t f, uintptr_t inside, uintptr_t* state, WaterZone* z) {
  auto b = [](uintptr_t a) { return mem::read<uint8_t>(a); };
  const uintptr_t len = padded_length(f, 0x400);
  int ats = 0, tests = 0, gates = 0, skips = 0, ends = 0, slots = 0;
  int counter = 0;
  for (uintptr_t p = f; len && p + 10 <= f + len; ++p) {
    if (b(p) == 0x6A && b(p + 2) == 0xE8 && call_target(p + 2) == inside) {
      z->at = b(p + 1);
      ++ats;
    }
    if (b(p) == 0xF6 && (b(p + 1) & 0xF8) == 0x80 && (b(p + 1) & 7) != 4 && b(p + 6) == 2) {
      const uint32_t off = u32_at(p + 2);
      if (off >= kWaterList && off < kWaterList + 256 * kWaterEntry && !((off - kWaterList) % kWaterEntry)) {
        if (tests == 5) return false;
        z->entry[tests++] = static_cast<uint8_t>((off - kWaterList) / kWaterEntry);
      }
    }
    if (b(p) == 0x83 && b(p + 1) == 0x38 && b(p + 2) == 0x0A && (b(p + 3) == 0x73 || (b(p + 3) == 0x0F && b(p + 4) == 0x83)))
      ++gates;
    if (b(p) == 0xE8 && b(p + 5) == 0xA9 && u32_at(p + 6) == 0x40000) {
      const uintptr_t t = call_target(p);
      if (!*state) *state = t;
      if (!t || t != *state) return false;
      ++skips;
    }
    if (b(p) == 0x01 && b(p + 1) == 0x58 && b(p + 2) != 8) {
      if (counter && b(p + 2) != counter) return false;
      counter = b(p + 2);
    }
    if (b(p) == 0x83 && b(p + 1) == 0x78 && b(p + 4) == 0x73 && counter && b(p + 2) == counter) {
      z->ends = b(p + 3);
      ++ends;
    }
    if (b(p) == 0xA1 && u32_at(p + 1) == g_water_slot) ++slots;
  }
  if (ats != 1 || tests != 5 || gates != 5 || skips != 5 || ends != 1 || !slots || counter < 0x14 || counter >= 0x30 ||
      (counter & 3) || z->ends < 2 || z->ends > 30)
    return false;
  // Each entry pushed twice (push entry; call): its setEm, and the look-up of its Ganado.
  for (int i = 0; i < 5; ++i) {
    int pushed = 0;
    for (uintptr_t p = f; p + 6 <= f + len; ++p)
      pushed += b(p) == 0x68 && u32_at(p + 1) == z->entry[i] && b(p + 5) == 0xE8;
    if (pushed != 2) return false;
  }
  z->counter = counter;
  return true;
}

// The sends of a one-shot (mov eax,entry; call the stage's send) in [f, f + len).
int water_sends(uintptr_t f, uintptr_t len, uintptr_t send, uint32_t into[8]) {
  int n = 0;
  for (uintptr_t p = f; p + 10 <= f + len; ++p)
    if (mem::read<uint8_t>(p) == 0xB8 && mem::read<uint8_t>(p + 5) == 0xE8 && call_target(p + 5) == send &&
        u32_at(p + 1) < 256) {
      into[u32_at(p + 1) >> 5] |= 1u << (u32_at(p + 1) & 31);
      ++n;
    }
  return n;
}

// The Mercenaries' water stage (room 404): its spawner (kSigWaterTask), the
// tasks it starts and its one-shot areas, from their code.
// The Mercenaries' base (room 403; kSigBaseFrame): its area lists as the
// village's are read (test [eax+17C],flag; je; call - each list's setEm sends),
// and its two one-time sends: cmp ecx,N; jb; test [eax+174],flag; jne; or
// [eax+174],flag; mov esi,entry; call FUN_007db150 (the entry's sent bit and dead
// mark cleared, setEm whatever is about) - one setEm refuses (no free slot) is
// never sent again. The first's flag is in esi (mov esi,flag a little before).
void prepare_base() {
  const uintptr_t f = at(kSigBaseFrame);
  auto fail = [] { logf("game: the Mercenaries' base is not the code expected - a one-time send refused stays lost"); };
  if (!f || operand(kSigBaseFrame, 2) != g_pg_slot || !in_image(operand(kSigBaseFrame, 23))) return fail();
  const uintptr_t len = padded_length(f, 0x400);
  const mem::Pattern by_reg = mem::parse_pattern("85 ?? 7C 01 00 00 74 ?? E8"),
                     by_imm = mem::parse_pattern("F7 80 7C 01 00 00 ?? ?? ?? ?? 74 ?? E8");
  uint32_t area[8] = {};
  uintptr_t setem = 0, wave_fn = 0;
  int lists = 0, waves = 0;
  uint32_t esi = 0;
  VillageWave found[2];
  for (uintptr_t p = f; len && p + 5 <= f + len; ++p) {
    if (mem::read<uint8_t>(p) == 0xBE && (u32_at(p + 1) & (u32_at(p + 1) - 1)) == 0 && u32_at(p + 1) >= 0x100)
      esi = u32_at(p + 1);  // mov esi,flag
    uintptr_t call = 0;
    if (mem::matches(p, by_reg)) call = p + 8;
    else if (mem::matches(p, by_imm)) call = p + 12;
    if (call) {
      int by_register = 0;
      if (sent_entries(call_target(call), area, &by_register, &setem) <= 0) return fail();
      ++lists;
      continue;
    }
    // mov esi,entry; call the one-time send - its flag just before: or [eax+174],imm32 or ,esi.
    if (mem::read<uint8_t>(p) == 0xBE && u32_at(p + 1) < 0x100 && mem::read<uint8_t>(p + 5) == 0xE8) {
      const uintptr_t t = call_target(p + 5);
      if (!wave_fn) wave_fn = t;
      if (!t || t != wave_fn || waves == 2) return fail();
      uint32_t flag = 0;
      if (mem::read<uint8_t>(p - 10) == 0x81 && mem::read<uint8_t>(p - 9) == 0x88 && u32_at(p - 8) == 0x174)
        flag = u32_at(p - 4);
      else if (mem::read<uint8_t>(p - 6) == 0x09 && mem::read<uint8_t>(p - 5) == 0xB0 && u32_at(p - 4) == 0x174)
        flag = esi;
      if (!flag || (flag & (flag - 1))) return fail();
      found[waves++] = VillageWave{flag, static_cast<int>(u32_at(p + 1))};
    }
  }
  int area_n = 0;
  for (const uint32_t w : area) area_n += __builtin_popcount(w);
  bool apart = waves == 2 && found[0].flag != found[1].flag;
  for (int k = 0; apart && k < 2; ++k) apart = !(area[found[k].entry >> 5] & (1u << (found[k].entry & 31)));
  if (lists != 22 || !area_n || !apart) return fail();
  std::memcpy(g_base_area, area, sizeof(area));
  std::memcpy(g_base_waves, found, sizeof(found));
  g_base_known = true;
  logf("game: the Mercenaries' base's 22 area lists send %d entries; its one-time sends %d (flag %08X) and %d (%08X)",
       area_n, found[0].entry, found[0].flag, found[1].entry, found[1].flag);
}

void prepare_water() {
  const uintptr_t t = at(kSigWaterTask);
  const uintptr_t slot = t ? operand(kSigWaterTask, 24) : 0, create = t ? call_target(t + 85) : 0;
  auto fail = [](const char* what) {
    logf("game: the Mercenaries' water stage is not the code expected (%s) - it sends from where the player "
         "stands, as the game does", what);
  };
  if (!t || !in_image(slot) || operand(kSigWaterTask, 41) != slot || !create || !g_at_slot || !g_player_slot) {
    fail("its spawner");
    return;
  }
  g_water_slot = slot;
  auto b = [](uintptr_t a) { return mem::read<uint8_t>(a); };
  const uintptr_t len = padded_length(t, 0x400);
  // Its tasks (push task; push 14; call FUN_006c4d10), then its loop.
  uintptr_t tasks[kWaterZones + 2];
  int n = 0;
  tasks[n++] = u32_at(t + 53);
  uintptr_t loop = 0;
  for (uintptr_t p = t + 90; len && p + 12 <= t + len && n < kWaterZones + 2; ++p)
    if (b(p) == 0x68 && b(p + 5) == 0x6A && b(p + 6) == 0x14 && b(p + 7) == 0xE8 && call_target(p + 7) == create) {
      tasks[n++] = u32_at(p + 1);
      loop = p + 12;
    }
  // The loop's one-shot areas (push area; call the inside test), its inline sends
  // (mov eax,entry; call the send) and the one-shots it calls directly.
  uintptr_t inside = 0, send = 0, direct[16];
  int nd = 0;
  uint32_t oneshots[8] = {};
  int at_n = 0, sends = 0;
  uint8_t ats[16] = {};
  for (uintptr_t p = loop; loop && p + 10 <= t + len; ++p) {
    if (b(p) == 0x6A && b(p + 2) == 0xE8 && water_inside_test(call_target(p + 2))) {
      if (!inside) inside = call_target(p + 2);
      if (at_n < 16) ats[at_n] = b(p + 1);
      ++at_n;
    }
    if (b(p) == 0xB8 && b(p + 5) == 0xE8 && u32_at(p + 1) < 256) {
      if (!send) send = call_target(p + 5);
      if (call_target(p + 5) == send) {
        oneshots[u32_at(p + 1) >> 5] |= 1u << (u32_at(p + 1) & 31);
        ++sends;
      }
    }
    if (b(p) == 0xE8) {
      const uintptr_t raw = p + 5 + mem::read<int32_t>(p + 1);
      bool seen = false;
      for (int i = 0; i < nd; ++i) seen = seen || direct[i] == raw;
      if (g_text.contains(raw) && b(raw) != 0xE9 && !seen && nd < 16) direct[nd++] = raw;
    }
  }
  for (int i = 0; i < nd && send; ++i)
    if (direct[i] != send) sends += water_sends(direct[i], padded_length(direct[i], 0x400), send, oneshots);
  if (n != kWaterZones || !inside || !send || at_n < 1 || at_n > 8 || sends < 1) {
    fail("its tasks and one-shot areas");
    g_water_slot = 0;
    return;
  }
  // The tasks: seven zones and the eighth (mov eax,[work]; mov ecx,[eax+8]; sub
  // ecx,[eax]; cmp ecx,N; jae - its send a jmp away).
  static const mem::Pattern eighth = mem::parse_pattern("8B 48 08 2B 08 83 F9 ?? 73");
  uintptr_t state = 0;
  WaterZone zones[kWaterZones];
  int nz = 0, labs = 0;
  uint32_t lab[8] = {};
  for (int i = 0; i < n; ++i) {
    const uintptr_t f = tasks[i];
    if (b(f) == 0xA1 && u32_at(f + 1) == slot && mem::matches(f + 5, eighth)) {
      ++labs;
      g_water_lab_at = b(f + 12);
      const uintptr_t flen = padded_length(f, 0x100);
      for (uintptr_t p = f; flen && p + 5 <= f + flen; ++p)
        if (b(p) == 0xE9) {
          const uintptr_t to = p + 5 + mem::read<int32_t>(p + 1);
          if (g_text.contains(to)) water_sends(to, padded_length(to, 0x200), send, lab);
          break;
        }
      continue;
    }
    if (nz == kWaterZones || !water_zone(f, inside, &state, &zones[nz++])) {
      fail("a zone's task");
      g_water_slot = 0;
      return;
    }
  }
  const uint32_t ladder = water_ladder(state);
  int lab_n = 0, oneshot_n = 0;
  bool apart = true;
  for (int w = 0; w < 8; ++w) {
    lab_n += __builtin_popcount(lab[w]);
    oneshot_n += __builtin_popcount(oneshots[w]);
    apart = apart && !(lab[w] & oneshots[w]);
  }
  for (int z = 0; z < nz; ++z)
    for (const uint8_t e : zones[z].entry)
      apart = apart && !((lab[e >> 5] | oneshots[e >> 5]) & (1u << (e & 31)));
  if (labs != 1 || lab_n < 1 || g_water_lab_at < 1 || !ladder || !apart || oneshot_n != sends) {
    fail("its eighth task, its sends or the ladder");
    g_water_slot = 0;
    return;
  }
  std::memcpy(g_water_zones, zones, sizeof(zones));
  g_water_zone_n = nz;
  g_water_oneshot_n = at_n;
  std::memcpy(g_water_oneshot_at, ats, sizeof(g_water_oneshot_at));
  std::memcpy(g_water_oneshots, oneshots, sizeof(oneshots));
  std::memcpy(g_water_lab, lab, sizeof(lab));
  g_water_ladder = ladder;
  g_water_known = true;
  char zl[160] = "", ol[64] = "";
  int most = 1 + oneshot_n, zn = 0, on = 0;
  for (int z = nz - 1; z >= 0; --z) {  // the order they run
    zn += std::snprintf(zl + zn, sizeof(zl) - zn, "%s%02X %d", zn ? ", " : "", zones[z].at, zones[z].ends);
    most += zones[z].ends - 1 + 5;
  }
  for (int i = 0; i < at_n; ++i) on += std::snprintf(ol + on, sizeof(ol) - on, "%s%02X", on ? " " : "", g_water_oneshot_at[i]);
  logf("game: the Mercenaries' water stage sends from %d zones (area and the count each ends at, in the order they "
       "run: %s - one short, its last frame's sends can be all five), one-shot areas %s (%d Ganados) and once at %d "
       "sends: %d at most besides its start group", nz, zl, ol, oneshot_n, g_water_lab_at, most);
}

void prepare_village_events() {
  const uintptr_t a = at(kSigVillageEvents);
  bool ok = a && g_village_slot && g_pg_slot;
  for (const int off : {26, 55, 104, 136}) ok = ok && operand(kSigVillageEvents, off) == g_pg_slot;
  for (const int off : {68, 117, 149}) ok = ok && operand(kSigVillageEvents, off) == operand(kSigVillageEvents, 39);
  if (!ok) {
    logf("game: the village's pickups' events are not the code expected - they send what they send as the game does");
    return;
  }
  const uintptr_t calls[3] = {a + 3, a + 80, a + 165};
  const Site sites[3] = {Site::kMercEvent1, Site::kMercEvent2, Site::kMercEvent3};
  const mem::Pattern head = mem::parse_pattern("55 8B EC A1"), send = mem::parse_pattern("6A FF 6A ?? 8D 4D ?? E8");
  for (int k = 0; k < 3; ++k) {
    const uintptr_t f = call_target(calls[k]);
    if (!f || !mem::matches(f, head) || mem::read<uintptr_t>(f + 4) != g_village_slot) {
      logf("game: the village's pickups' event %d is not the code expected", k + 1);
      continue;
    }
    std::vector<uintptr_t> checks;
    int sends = 0;
    uintptr_t end = 0;
    for (uintptr_t p = f; p < f + 0x400 && !end; ++p) {
      if (mem::read<uint8_t>(p) == 0xC3 && mem::read<uint8_t>(p + 1) == 0xCC) end = p;
      if (mem::matches(p, send)) ++sends;
      if (mem::read<uint8_t>(p) != 0x83 || mem::read<uint8_t>(p + 1) != 0x38 || mem::read<uint8_t>(p + 2) != 0x0A)
        continue;
      bool jae = false;  // 0F 83 or 73 within the next 16 bytes
      for (uintptr_t q = p + 3; q < p + 19 && !jae; ++q)
        jae = (mem::read<uint8_t>(q) == 0x0F && mem::read<uint8_t>(q + 1) == 0x83) || mem::read<uint8_t>(q) == 0x73;
      if (jae) checks.push_back(p + 2);
    }
    if (!end || checks.empty() || static_cast<int>(checks.size()) != sends) {
      logf("game: the village's pickups' event %d is not the code expected (%d checks for %d entries)", k + 1,
           static_cast<int>(checks.size()), sends);
      continue;
    }
    const uintptr_t from = checks.front(), to = checks.back() + 1;
    std::vector<uint8_t> bytes(to - from);
    std::string expect;
    char hex[4];
    for (uintptr_t p = from; p < to; ++p) {
      bytes[p - from] = mem::read<uint8_t>(p);
      std::snprintf(hex, sizeof(hex), "%02X ", bytes[p - from]);
      expect += hex;
    }
    for (const uintptr_t c : checks) bytes[c - from] = 0xFF;
    prepare_at(sites[k], from, expect.c_str(), bytes);
  }
}

uintptr_t pg() {
  if (!g_pg_slot) return 0;
  const uintptr_t p = mem::read<uintptr_t>(g_pg_slot);
  if (!p) return 0;
  if (p != g_pg_ok) {
    if (!mem::readable(p, kGlobalSize)) return 0;
    g_pg_ok = p;
  }
  return p;
}
template <typename T>
T gv(int off) {
  return mem::read<T>(pg() + off);
}
template <typename T>
void gw(int off, T v) {
  *reinterpret_cast<T*>(pg() + off) = v;
}

// The item array, checked when it moves or changes length.
uintptr_t item_array(int* n) {
  *n = 0;
  if (!g_item_mgr) return 0;
  const uintptr_t a = mem::read<uintptr_t>(g_item_mgr + kMgrItems);
  const int count = mem::read<int>(g_item_mgr + kMgrCount);
  if (!a || count <= 0 || count > 4096) return 0;
  if (a != g_items_ok || count != g_items_ok_n) {
    if (!mem::readable(a, static_cast<size_t>(count) * kItemSize)) return 0;
    g_items_ok = a;
    g_items_ok_n = count;
  }
  *n = count;
  return a;
}

uintptr_t card_ptr() {
  if (!g_card_slot) return 0;
  const uintptr_t c = mem::read<uintptr_t>(g_card_slot);
  if (!c) return 0;
  if (c != g_card_ok) {
    if (!mem::readable(c, kCardSize)) return 0;
    g_card_ok = c;
  }
  return c;
}

// The checkpoint work's copy of GLOBAL_WK's save region (0: none yet). The
// work keeps its pointers as they were when it was made; the game rebases them
// on its own address before use (FUN_0063c390), so the copy is found the same
// way: work + (copy - work's own recorded address).
uintptr_t checkpoint_copy() {
  if (!g_checkpoint_slot) return 0;
  const uintptr_t work = mem::read<uintptr_t>(g_checkpoint_slot);
  if (!work || !mem::readable(work, 0x1C)) return 0;
  const uintptr_t self = mem::read<uintptr_t>(work), copy_rec = mem::read<uintptr_t>(work + 8);
  if (!self || !copy_rec) return 0;
  return work + (copy_rec - self);
}

}  // namespace

const char* site_name(Site s) {
  switch (s) {
    case Site::kHurtLeon: return "the player's damage (LifeDownSet2)";
    case Site::kHurtAshley: return "the partner's damage (LifeDownSet2)";
    case Site::kHurtEnemy: return "an enemy's damage (LifeDownSet2)";
    case Site::kHurtGigante: return "El Gigante's damage (its hit check)";
    case Site::kHurtGiganteKnife: return "El Gigante's damage (the knife on its back)";
    case Site::kHurtKrauser: return "Krauser's damage (a hit's)";
    case Site::kHurtKrauserArm: return "Krauser's armoured arm (a hit on it)";
    case Site::kKillFlag1: return "an attack's kill on the player (EmAtkSetDamagePL)";
    case Site::kKillFlag2: return "an attack's kill on the player (EmAtkHitCk)";
    case Site::kCrushRock: return "a rock crushing the player";
    case Site::kCrushTruck: return "the truck running the player over";
    case Site::kAshleyBlast: return "the partner's fall from a heavy hit (cSubChar::damageCheck)";
    case Site::kAshleyStomp: return "El Gigante's stomp on the partner";
    case Site::kAshleyTruck1: return "the truck running the partner over (1)";
    case Site::kAshleyTruck2: return "the truck running the partner over (2)";
    case Site::kAshleyRock: return "a rock crushing the partner";
    case Site::kAmmo: return "a shot's round (cItemMgr::trigger)";
    case Site::kArrows: return "Krauser's bow's arrow (cItemMgr::trigger)";
    case Site::kBolts: return "the bowgun's bolt (cItemMgr::trigger)";
    case Site::kPlayTime: return "the play time's update";
    case Site::kCountdown: return "the countdown's tick (CountDown::move)";
    case Site::kSaveCount: return "the save count's +1 (the save builder)";
    case Site::kBuy: return "the merchant's price (a purchase)";
    case Site::kTuneUp: return "the merchant's price (a tune-up)";
    case Site::kMissA: return "a missed shot's count";
    case Site::kMissB: return "a missed shot's count (the other site)";
    case Site::kDeathsChapter: return "the chapter's death count (the continue)";
    case Site::kDeathsTotal: return "the game's death count (the continue)";
    case Site::kAimSwayV: return "the aim's drift (up and down)";
    case Site::kAimSwayH: return "the aim's drift (left and right)";
    case Site::kAimKickV: return "a shot's throw of the aim (up and down)";
    case Site::kAimKickH: return "a shot's throw of the aim (left and right)";
    case Site::kSpreadGun1: return "a handgun shot's spread (1)";
    case Site::kSpreadGun2: return "a handgun shot's spread (2)";
    case Site::kSpreadGun3: return "a handgun shot's spread (the Red9 and Matilda's routine, 1)";
    case Site::kSpreadGun4: return "a handgun shot's spread (the Red9 and Matilda's routine, 2)";
    case Site::kSpreadAuto1: return "a machine gun's first shot's spread (1)";
    case Site::kSpreadAuto2: return "a machine gun's first shot's spread (2)";
    case Site::kSpreadAuto3: return "a machine gun's held fire's spread (1)";
    case Site::kSpreadAuto4: return "a machine gun's held fire's spread (2)";
    case Site::kSpreadShotgun1: return "a shotgun's centre pellet's spread (1)";
    case Site::kSpreadShotgun2: return "a shotgun's centre pellet's spread (2)";
    case Site::kSpreadMounted1: return "the mounted gun's spread (1)";
    case Site::kSpreadMounted2: return "the mounted gun's spread (2)";
    case Site::kSpreadThrow: return "a throw's sideways push";
    case Site::kAimLoopHandgun: return "the handguns' idle aim";
    case Site::kAimLoopWesker: return "Wesker's idle aim";
    case Site::kAimLoopBow: return "Krauser's bow's idle aim";
    case Site::kAimLoopLaser: return "the P.R.L. 412's idle aim";
    case Site::kAimLoopBowgun: return "Ada's bowgun's idle aim";
    case Site::kAimLoopGrenade: return "a grenade's idle aim";
    case Site::kAimLoopRed9: return "the Red9 and Matilda's idle aim";
    case Site::kAimLoopMine: return "the mine thrower's idle aim";
    case Site::kAimLoopRocket: return "the rocket launcher's idle aim";
    case Site::kAimLoopShotgun: return "the shotguns' idle aim";
    case Site::kAimLoopAuto: return "the machine guns' idle aim";
    case Site::kHoldLaser: return "the laser's line (the gun's part)";
    case Site::kHoldHand1: return "a handgun shot's line (the hand)";
    case Site::kHoldHand2: return "a handgun shot's line (the Red9 and Matilda's routine)";
    case Site::kHoldAuto1: return "a machine gun's first shot's line";
    case Site::kHoldAuto2: return "a machine gun's held fire's line";
    case Site::kHoldShotgun: return "a shotgun pellet's line";
    case Site::kQteKind13: return "the QTE prompts' answer (pressed)";
    case Site::kQteKinds: return "the QTE prompts' check (one button, both triggers, two buttons)";
    case Site::kQteKindsLR: return "the QTE prompts' check (one trigger)";
    case Site::kQteBoulder: return "the boulder run's presses";
    case Site::kQteSwim: return "Del Lago: swimming back to the boat";
    case Site::kQteRope: return "Del Lago: the rope";
    case Site::kQteGigante1: return "El Gigante's parasite (1)";
    case Site::kQteGigante2: return "El Gigante's parasite (2)";
    case Site::kQteGigante3: return "El Gigante's parasite (3)";
    case Site::kQteMinecart: return "the minecart's climb";
    case Site::kQteStatueRun: return "the statue chase: running";
    case Site::kQteStatueSpeed: return "the statue chase: the run's speed";
    case Site::kQteStatueJump: return "the statue chase: the jumps";
    case Site::kQteStatueEnd: return "the statue chase: the last jump";
    case Site::kQteStatueClimb1: return "the statue chase: climbing (1)";
    case Site::kQteStatueClimb2: return "the statue chase: climbing (2)";
    case Site::kQteKrauser: return "Krauser's knife fight: the last presses";
    case Site::kQteLasers1: return "the laser room: a dodge (1)";
    case Site::kQteLasers2: return "the laser room: a dodge (2)";
    case Site::kQteLasersLast: return "the laser room: the last dodge";
    case Site::kQteBridge1: return "Saddler's bridges: the jump (1)";
    case Site::kQteBridge2: return "Saddler's bridges: the jump (2)";
    case Site::kQteBridgeClimb1: return "Saddler's bridges: climbing (1)";
    case Site::kQteBridgeClimb2: return "Saddler's bridges: climbing (2)";
    case Site::kQteRobotGrab: return "em3a's grab";
    case Site::kQteShake: return "a grab's shake-off";
    case Site::kMercFirst: return "the Mercenaries' combo: the first kill's timer";
    case Site::kMercWindow: return "the Mercenaries' combo: its window";
    case Site::kMercBonus: return "the Mercenaries' combo: its bonus, scored once it is over";
    case Site::kMercBonusTime: return "the Mercenaries' Bonus Points time";
    case Site::kMercBonusBank: return "the Mercenaries' combo: its bonus, scored once the Bonus Points time is over";
    case Site::kMercAreas: return "the scene's area flags, cleared each frame";
    case Site::kMercEvent1: return "the village's first pickup's event, sending while fewer than ten are about";
    case Site::kMercEvent2: return "the village's second pickup's event, sending while fewer than ten are about";
    case Site::kMercEvent3: return "the village's third pickup's event, sending while fewer than ten are about";
    case Site::kCastleDoor: return "the castle's door 1B, before its two Ganados";
    case Site::kSaveTurnedAway: return "the scheduler's start, turning the save card away";
    default: return "?";
  }
}

Site needs(Site s) {
  if (s == Site::kQteKinds || s == Site::kQteKindsLR) return Site::kQteKind13;
  if (s == Site::kAshleyTruck1 || s == Site::kAshleyTruck2) return Site::kAshleyBlast;
  return Site::kCount;
}

bool discover() { return discover_in(GetModuleHandleA(nullptr)); }

bool discover_in(HMODULE exe) {
  g_exe = exe;
  g_text = mem::section(g_exe, ".text");
  if (g_text.empty()) {
    std::snprintf(g_status, sizeof(g_status), "bio4.exe has no .text section");
    logf("game: %s", g_status);
    return false;
  }
  const DWORD t0 = GetTickCount();
  int found = 0;
  for (int i = 0; i < kSigCount; ++i) found += find_sig(i) ? 1 : 0;

  // Globals.
  g_pg_slot = operand(kSigGlobal, 1);
  if (g_pg_slot && operand(kSigGlobal, 14) != g_pg_slot) g_pg_slot = 0;
  g_item_mgr = operand(kSigItemMgr, 14);
  g_subscreen = operand(kSigSubScreen, 1);
  g_cockpit = operand(kSigCockpit, 5);
  g_option_flag = operand(kSigOption, 1);
  for (uintptr_t* p : {&g_pg_slot, &g_item_mgr, &g_subscreen, &g_cockpit, &g_option_flag})
    if (*p && !in_image(*p)) *p = 0;

  // The option screen keeps the countdown aside while it is up: its opening
  // copies the CountDown into the Cockpit's +AC (FUN_005A3CF0: +0 the state, +4
  // the frames) just before it puts OptionOpenFlag up, and its closing copies
  // that back (FUN_005A3D80) - over whatever the CountDown holds by then.
  const uintptr_t keep = operand(kSigClockKeep, 1);
  const uintptr_t keep_fn = call_target(plus(at(kSigClockKeep), 10));
  const uintptr_t back_fn = call_target(plus(at(kSigClockBack), 27));
  // ecx the CountDown, the copy pushed: copy+0 = state, copy+4 = frames; and back.
  const char* keeps = "55 8B EC 56 8B 75 08 57 8B F9 8B 07 89 06 D9 47 08 D9 5E 04";
  const char* puts_back = "55 8B EC 53 56 8B 75 08 8B 06 8B D9 89 03 D9 46 04 D9 5B 08";
  if (g_cockpit && g_option_flag && in_image(keep) && operand(kSigClockKeep, 6) == g_cockpit + kCountdown &&
      operand(kSigClockKeep, 38) == g_option_flag && operand(kSigClockBack, 18) == keep &&
      operand(kSigClockBack, 23) == g_cockpit + kCountdown && keep_fn &&
      mem::matches(keep_fn, mem::parse_pattern(keeps)) && back_fn && mem::matches(back_fn, mem::parse_pattern(puts_back)))
    g_clock_keep = keep;
  else
    logf("game: the option screen's copy of the countdown not found - the countdown cannot be edited");

  // The save card.
  g_storage_slot = operand(kSigCard, 2);
  if (g_storage_slot && operand(kSigCard, 15) != g_storage_slot) g_storage_slot = 0;
  g_card_slot = operand(kSigCard, 21);
  if (g_card_slot && (operand(kSigCard, 29) != g_card_slot || operand(kSigCardFiles, 2) != g_card_slot)) {
    logf("game: the two card sites name different cCard slots - the save files are left out");
    g_card_slot = 0;
  }
  g_exists = operand(kSigCardFiles, 52);
  if (operand(kSigCheckpoint, 1) == g_pg_slot) g_checkpoint_slot = operand(kSigCheckpoint, 7);
  for (uintptr_t* p : {&g_storage_slot, &g_card_slot, &g_exists, &g_checkpoint_slot})
    if (*p && !in_image(*p)) *p = 0;

  // A save asked for (game.h, "a save is asked for"): a save to a file and one of the system data alone each put
  // their flags into pG+4 and hand the card's task to the scheduler's level 1 (FUN_006c5c10), which turns it away
  // while the level holds a task. Both hand over the same task and call the same start; the task's body starts by
  // marking the card busy (pG+5028 |= 800); the levels are read out of the start itself.
  if (at(kSigSaveSystem) && at(kSigSaveFile) && at(kSigTaskStart) && g_pg_slot) {
    const uintptr_t task = operand(kSigSaveSystem, 24), table = operand(kSigTaskStart, 69);
    const uintptr_t body = task && g_text.contains(task) && mem::read<uint8_t>(task) == 0xE9
                               ? task + 5 + mem::read<int32_t>(task + 1)
                               : task;
    char head[64];
    std::snprintf(head, sizeof(head), "55 8B EC A1 %02X %02X %02X %02X 81 88 28 50 00 00 00 08 00 00",
                  static_cast<unsigned>(g_pg_slot & 0xFF), static_cast<unsigned>((g_pg_slot >> 8) & 0xFF),
                  static_cast<unsigned>((g_pg_slot >> 16) & 0xFF), static_cast<unsigned>(g_pg_slot >> 24));
    if (operand(kSigSaveSystem, 10) == g_pg_slot && operand(kSigSaveFile, 5) == g_pg_slot &&
        operand(kSigSaveFile, 21) == g_pg_slot && operand(kSigSaveFile, 28) == task &&
        call_target(at(kSigSaveSystem) + 30) == at(kSigTaskStart) &&
        call_target(at(kSigSaveFile) + 40) == at(kSigTaskStart) && in_image(table) &&
        operand(kSigTaskStart, 16) == table + kTaskState && g_text.contains(body) &&
        mem::matches(body, mem::parse_pattern(head))) {
      g_sched = table;
      g_card_task = task;
    }
  }
  // The fix (game.h, "a save is asked for"): where the scheduler's start turns a task away it calls two empty
  // debug prints ("level %d", "TASK DON'T EXEC"), 34 bytes from the push of the level to the add esp after them,
  // with eax still the level's offset in the table. In their place: unless the level holds the card's task, a
  // turned-away card's task has pG+4's request flags cleared as the card's own end clears them; then 0, as before.
  if (g_sched && g_card_task) {
    const uintptr_t prints = at(kSigTaskStart) + 23;
    const uintptr_t print1 = call_target(prints + 6), print2 = call_target(prints + 26);
    std::vector<uint8_t> b;
    auto u8 = [&](uint8_t v) { b.push_back(v); };
    auto u32 = [&](uintptr_t v) {
      for (int i = 0; i < 4; ++i) b.push_back(static_cast<uint8_t>(v >> (8 * i)));
    };
    u8(0x81), u8(0xB8), u32(g_sched + kTaskFn), u32(g_card_task);  // cmp dword [eax+levels+33C],card's task
    u8(0x74), u8(0x16);                                             // je - a save there clears them at its end
    u8(0x81), u8(0x7D), u8(0x0C), u32(g_card_task);                 // cmp dword [ebp+C],card's task
    u8(0x75), u8(0x0D);                                             // jne - another task turned away
    u8(0x8B), u8(0x0D), u32(g_pg_slot);                             // mov ecx,[pG]
    u8(0x81), u8(0x61), u8(0x04), u32(0x80000007);                  // and dword [ecx+4],80000007
    if (print1 && print2 && mem::read<uint8_t>(print1) == 0xC3 && mem::read<uint8_t>(print2) == 0xC3)
      prepare_at(Site::kSaveTurnedAway, prints,
                 "51 68 ?? ?? ?? ?? E8 ?? ?? ?? ?? 68 ?? ?? ?? ?? 68 45 01 00 00 68 ?? ?? ?? ?? E8 ?? ?? ?? ?? 83 C4 14", b);
  }
  if (!g_patch[static_cast<int>(Site::kSaveTurnedAway)].prepared())
    logf("game: the scheduler's start of the save card not found as expected - a save the game turns away leaves its "
         "flags for the next one, as in the game");

  // The cheats' patch sites: each re-checked byte for byte as it is prepared.
  prepare(Site::kHurtLeon, kSigHurtLeon, 13, "66 29 BE B4 4F 00 00", mem::nops(7));
  prepare(Site::kHurtAshley, kSigHurtAshley, 13, "66 29 BE B8 4F 00 00", mem::nops(7));
  // cmp eax,edi; jge -> test edi,edi; jle: any damage at all takes all the HP.
  prepare(Site::kHurtEnemy, kSigHurtEnemy, 7, "3B C7 7D", {0x85, 0xFF, 0x7E});
  prepare_gigante();
  prepare_krauser();
  prepare_kills();
  prepare(Site::kAmmo, kSigAmmo, 12, "66 89 57 08", mem::nops(4));
  // push item; mov ecx,ebx; call use-one -> mov al,1 (the shot goes ahead, nothing is used up).
  const std::vector<uint8_t> shot_ok = {0xB0, 0x01, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90};
  prepare(Site::kArrows, kSigArrows, 0, "6A 72 8B CB E8 ?? ?? ?? ??", shot_ok);
  prepare(Site::kBolts, kSigBolts, 0, "6A 11 8B CB E8 ?? ?? ?? ??", shot_ok);
  if (at(kSigPlayTime) && operand(kSigPlayTime, 2) == g_pg_slot)
    prepare(Site::kPlayTime, kSigPlayTime, 9, "01 86 A4 4F 00 00", mem::nops(6));
  prepare(Site::kCountdown, kSigCountdown, 3, "D8 61 70", mem::nops(3));
  if (at(kSigSaveCount) && operand(kSigSaveCount, 1) == g_pg_slot && operand(kSigSaveCount, 18) == g_pg_slot)
    prepare(Site::kSaveCount, kSigSaveCount, 10, "66 01 B0 9C 4F 00 00", mem::nops(7));
  // The merchant's two charges: a purchase takes the price off the pesetas it is
  // handed (pG+4FA8; it checks they cover it first, and that check stays), a
  // tune-up takes its cost off pG+4FA8 itself.
  if (at(kSigBuy) && operand(kSigBuy, 23) == g_pg_slot) prepare(Site::kBuy, kSigBuy, 37, "29 18", mem::nops(2));
  if (at(kSigTuneUp) && operand(kSigTuneUp, 26) == g_pg_slot &&
      (!g_item_mgr || (operand(kSigTuneUp, 13) == g_item_mgr && operand(kSigTuneUp, 5) == g_item_mgr + kMgrWeapon)))
    prepare(Site::kTuneUp, kSigTuneUp, 30, "29 88 A8 4F 00 00", mem::nops(6));
  // A shot is counted in one of two places, each: if it hit, the hits +1, then
  // the shots +1. The jump a miss takes past the hits goes past the shots too,
  // so a miss counts nothing (+0x2C instead of +0x16: the four adds' length).
  if (at(kSigShotA) && operand(kSigShotA, 12) == g_pg_slot && operand(kSigShotA, 45) == g_pg_slot)
    prepare(Site::kMissA, kSigShotA, 9, "74 16", {0x74, 0x2C});
  if (at(kSigShotB) && operand(kSigShotB, 8) == g_pg_slot && operand(kSigShotB, 41) == g_pg_slot)
    prepare(Site::kMissB, kSigShotB, 5, "7E 16", {0x7E, 0x2C});
  // The continue adds a death to the chapter's count and the game's (and to the
  // area's retries, +4FA0, which the game's own scenes read and which is left be).
  if (at(kSigDeaths) && operand(kSigDeaths, 2) == g_pg_slot && operand(kSigDeaths, 15) == g_pg_slot &&
      operand(kSigDeaths, 29) == g_pg_slot) {
    prepare(Site::kDeathsChapter, kSigDeaths, 19, "47", {0x90});
    prepare(Site::kDeathsTotal, kSigDeaths, 33, "43", {0x90});
  }
  prepare_steady_aim();
  prepare_aim_loops();
  prepare_qte_check();
  prepare_qte_polls();
  prepare_em_pool();
  prepare_gallery();
  prepare_merc();
  prepare_em10();
  prepare_merc_areas();
  prepare_models();
  prepare_village();
  prepare_village_events();
  prepare_village_lists();
  prepare_village_waves();
  prepare_castle();
  prepare_water();
  prepare_base();
  prepare(Site::kCastleDoor, kSigCastleDoor, 23, "74 ??", {0x90, 0x90});

  // The tick: the frame loop's SetThreadAffinityMask call, keyed on where it
  // returns - and the same frame's QueryPerformanceCounter call a little later
  // (only timing stores and an empty call between the two), which ticks when
  // the first has not: code that replaces the frame wait's (another tool's
  // frame limiter) can take the first call away, never the second.
  g_tick_return = g_tick_return_late = 0;
  if (at(kSigTick)) {
    const uintptr_t gct = mem::iat_slot(g_exe, "KERNEL32.dll", "GetCurrentThread");
    if (gct && operand(kSigTick, 5) == gct) g_tick_return = at(kSigTick) + 12;
    else logf("game: the frame loop's call is not through GetCurrentThread's import - no tick from it");
  }
  if (const uintptr_t late = at(kSigTickLate)) {
    // ebx is the QueryPerformanceCounter import: WinMain loads it before its
    // loop (mov ebx,[slot]) and keeps it there.
    const uintptr_t qpc = mem::iat_slot(g_exe, "KERNEL32.dll", "QueryPerformanceCounter");
    mem::Pattern load;
    load.bytes = {0x8B, 0x1D};
    for (int i = 0; i < 4; ++i) load.bytes.push_back(static_cast<int16_t>((qpc >> (8 * i)) & 0xFF));
    const bool loaded = qpc && late > 0x1000 && mem::find_pattern(mem::Range{late - 0x1000, late}, load);
    if (loaded && operand(kSigTickLate, 2) == operand(kSigTickLate, 11)) g_tick_return_late = late + 9;
    else logf("game: the frame loop's QueryPerformanceCounter call is not through its import - no tick from it");
  }
  if (!g_tick_return && g_tick_return_late)
    logf("game: the frame loop's SetThreadAffinityMask call is not there (its bytes are not the game's: another tool "
         "replaced the frame wait?) - the tick comes from the same frame's QueryPerformanceCounter call");
  else if (!g_tick_return && !g_tick_return_late)
    logf("ERROR: game: neither of the frame loop's calls the tick goes by was found - no tick");

  // The approved game calls.
  if (at(kSigPutInCase) && g_subscreen && operand(kSigPutInCase, 3) == g_subscreen + 0x2AA)
    g_put_in_case = call_target(at(kSigPutInCase) + 18);
  if (at(kSigItemGet) && g_item_mgr && operand(kSigItemGet, 3) == g_item_mgr &&
      operand(kSigItemGet, 13) == g_item_mgr + 0x18)
    g_item_get = call_target(at(kSigItemGet) + 7);
  g_item_erase = call_target(at(kSigItemErase) + 1);

  // The item rules.
  if (const uintptr_t t = operand(kSigItemInfo, 18); t && g_text.contains(t)) g_kind_table = t;
  if (operand(kSigCharge, 52) == g_pg_slot) {
    g_charge_rows_slot = operand(kSigCharge, 2);
    g_charge = operand(kSigCharge, 18);
    if (!in_image(g_charge_rows_slot) || !in_image(g_charge)) g_charge = g_charge_rows_slot = 0;
  }
  g_levels = operand(kSigMaxLevel, 7);
  g_levels_end = operand(kSigMaxLevel, 23);
  if (!in_image(g_levels) || g_levels_end <= g_levels || (g_levels_end - g_levels) % 6) g_levels = g_levels_end = 0;

  const uintptr_t base = reinterpret_cast<uintptr_t>(g_exe);
  auto rva = [&](uintptr_t a) { return a ? static_cast<unsigned>(a - base) : 0u; };
  logf("game: %d of %d signatures matched in %lu ms (exe base %08X)", found, static_cast<int>(kSigCount),
       GetTickCount() - t0, static_cast<unsigned>(base));
  logf("game: pG slot +%X, ItemMgr +%X, SubScreenWk +%X, Cockpit +%X (the pause's copy of its countdown +%X), option "
       "flag +%X", rva(g_pg_slot), rva(g_item_mgr), rva(g_subscreen), rva(g_cockpit), rva(g_clock_keep),
       rva(g_option_flag));
  logf("game: card slot +%X, files' exists bytes +%X, remote storage slot +%X, checkpoint slot +%X; tick returns to +%X "
       "(or +%X)",
       rva(g_card_slot), rva(g_exists), rva(g_storage_slot), rva(g_checkpoint_slot), rva(g_tick_return),
       rva(g_tick_return_late));
  logf("game: a save asked for hands the card's task (+%X) to the scheduler's level %d (its levels +%X)",
       rva(g_card_task), kCardLevel, rva(g_sched));
  logf("game: PutInCase +%X, cItemMgr::get +%X, cItemMgr::erase +%X; item kinds +%X, charge table +%X, levels +%X",
       rva(g_put_in_case), rva(g_item_get), rva(g_item_erase), rva(g_kind_table), rva(g_charge), rva(g_levels));
  logf("game: the shooting gallery's work slot +%X, the enemy manager's list +%X", rva(g_gallery_slot),
       rva(g_em_list_slot));
  logf("game: the Mercenaries' work +%X, the Ganados' class (vtable) +%X; the model pool's slot +%X, the parts "
       "pool's +%X, the village's work slot +%X", rva(g_merc_work), rva(g_em10_vtable), rva(g_models_slot),
       rva(g_parts_slot), rva(g_village_slot));
  for (int i = 0; i < static_cast<int>(Site::kCount); ++i)
    if (g_patch[i].prepared())
      logf("game: patch site %s at +%X", site_name(static_cast<Site>(i)), rva(g_patch[i].at));

  // The panel needs the run (pG) and the option screen; the rest are features
  // that are greyed out on their own when missing.
  g_ready = g_pg_slot && g_option_flag;
  if (g_ready)
    std::snprintf(g_status, sizeof(g_status), "ready");
  else
    std::snprintf(g_status, sizeof(g_status), "%s%s not found in bio4.exe - not a version the mod was made for",
                  g_pg_slot ? "" : "the game's global state", g_option_flag ? "" : (g_pg_slot ? "the option screen" : ", the option screen"));
  logf("game: %s", g_status);
  return g_ready;
}

bool ready() { return g_ready; }
const char* status_text() { return g_status; }
uintptr_t tick_return() { return g_tick_return; }
uintptr_t tick_return_late() { return g_tick_return_late; }
HMODULE exe() { return g_exe; }

void trace_tick() {
  const config::Settings& cfg = config::get();
  if (!cfg.nwatch || !g_exe) return;
  static uint32_t last[config::kMaxWatch];
  static bool seen[config::kMaxWatch];
  const uintptr_t base = reinterpret_cast<uintptr_t>(g_exe);
  for (int i = 0; i < cfg.nwatch; ++i) {
    const uintptr_t a = base + cfg.watch[i].rva;
    uint32_t v = 0;
    if (!mem::readable(a, cfg.watch[i].size)) continue;
    std::memcpy(&v, reinterpret_cast<const void*>(a), cfg.watch[i].size);
    if (seen[i] && v == last[i]) continue;
    logf("watch: +%X = %08X (was %08X)", cfg.watch[i].rva, v, last[i]);
    seen[i] = true;
    last[i] = v;
  }
}

// --- the run ---------------------------------------------------------------------------------------

bool run(Run* out) {
  *out = Run{};
  if (!g_ready || !pg()) return false;
  out->known = true;
  out->routine = gv<uint8_t>(kRno0);
  out->step = gv<uint8_t>(kRno1);
  out->in_game = out->routine >= 2 && out->routine <= 6;
  out->option_screen = mem::read<uint8_t>(g_option_flag) != 0;
  out->paused = out->in_game && out->routine == 6 && out->option_screen;
  const uint32_t sys = gv<uint32_t>(kFlagsSystem);
  out->mode = !out->in_game                ? Mode::kNone
              : (sys & kSysAssignmentAda)  ? Mode::kAssignmentAda
              : (sys & kSysMercenaries)    ? Mode::kMercenaries
              : (sys & kSysSeparateWays)   ? Mode::kSeparateWays
                                           : Mode::kMain;
  out->character = gv<uint8_t>(kPlType);
  out->room = gv<uint16_t>(kRoom);
  out->chapter = gv<uint8_t>(kChapter);
  out->sys = sys;
  return true;
}

const char* mode_name(Mode m) {
  switch (m) {
    case Mode::kMain: return "the main game";
    case Mode::kSeparateWays: return "Separate Ways";
    case Mode::kAssignmentAda: return "Assignment Ada";
    case Mode::kMercenaries: return "The Mercenaries";
    default: return "no game";
  }
}

const char* character_name(int c) {
  static const char* const kNames[] = {"Leon", "Ashley", "Ada", "HUNK", "Krauser", "Wesker"};
  return c >= 0 && c < 6 ? kNames[c] : "?";
}

// --- health ------------------------------------------------------------------------------------------

bool health(Health* out) {
  *out = Health{};
  if (!pg()) return false;
  out->hp = gv<int16_t>(kHp);
  out->max = gv<int16_t>(kHpMax);
  out->sub_hp = gv<int16_t>(kSubHp);
  out->sub_max = gv<int16_t>(kSubHpMax);
  return true;
}

bool set_hp(int hp) {
  if (!pg()) return false;
  gw<int16_t>(kHp, static_cast<int16_t>(hp));
  return true;
}

bool set_sub_hp(int hp) {
  if (!pg()) return false;
  gw<int16_t>(kSubHp, static_cast<int16_t>(hp));
  return true;
}

// --- counters ------------------------------------------------------------------------------------------

bool counters(Counters* out) {
  *out = Counters{};
  if (!pg()) return false;
  // The total as the game would make it now: the time since the base on top -
  // but not while paused, where the option screen brought it up to date as it
  // opened and moves the base past the pause as it closes. The patch of Freeze
  // play time is the game's own add: with it applied nothing goes on.
  int pt = gv<int32_t>(kPlayTime);
  const mem::Patch& frozen = g_patch[static_cast<int>(Site::kPlayTime)];
  const uint32_t base = gv<uint32_t>(kTimeBase), now = GetTickCount() / 1000;
  const bool paused = gv<uint8_t>(kRno0) == 6;
  if (!paused && !frozen.applied && base && now >= base && now - base < 86400) pt += static_cast<int>(now - base);
  out->play_time = pt > kPlayTimeMax ? kPlayTimeMax : pt;
  out->pesetas = gv<int32_t>(kPesetas);
  out->deaths_chapter = gv<uint16_t>(kDeathsChapter);
  out->deaths_total = gv<uint16_t>(kDeathsTotal);
  out->kills_chapter = gv<int32_t>(kKillsChapter);
  out->kills_total = gv<int32_t>(kKillsTotal);
  out->hits_chapter = gv<int32_t>(kHitsChapter);
  out->hits_total = gv<int32_t>(kHitsTotal);
  out->shots_chapter = gv<int32_t>(kShotsChapter);
  out->shots_total = gv<int32_t>(kShotsTotal);
  out->difficulty = gv<uint8_t>(kDifficulty);
  out->da_level = gv<uint8_t>(kDaLevel);
  out->saves = gv<uint16_t>(kSaveCount);
  return true;
}

bool set_play_time(int seconds) {
  if (!pg()) return false;
  if (seconds < 0) seconds = 0;
  if (seconds > kPlayTimeMax) seconds = kPlayTimeMax;
  // The base moves to now as well, so nothing since it is added on top: the
  // time set is the time from this moment.
  gw<int32_t>(kPlayTime, seconds);
  gw<uint32_t>(kTimeBase, GetTickCount() / 1000);
  return true;
}

bool set_pesetas(int v) {
  if (!pg()) return false;
  gw<int32_t>(kPesetas, v < 0 ? 0 : v > kPesetasMax ? kPesetasMax : v);
  return true;
}

bool set_counter(Counter c, int v) {
  if (!pg()) return false;
  if (v < 0) v = 0;
  switch (c) {
    case Counter::kDeathsChapter: gw<uint16_t>(kDeathsChapter, static_cast<uint16_t>(v > 999 ? 999 : v)); break;
    case Counter::kDeathsTotal: gw<uint16_t>(kDeathsTotal, static_cast<uint16_t>(v > 999 ? 999 : v)); break;
    case Counter::kKillsChapter: gw<int32_t>(kKillsChapter, v); break;
    case Counter::kKillsTotal: gw<int32_t>(kKillsTotal, v); break;
    case Counter::kHitsChapter: gw<int32_t>(kHitsChapter, v); break;
    case Counter::kHitsTotal: gw<int32_t>(kHitsTotal, v); break;
    case Counter::kShotsChapter: gw<int32_t>(kShotsChapter, v); break;
    case Counter::kShotsTotal: gw<int32_t>(kShotsTotal, v); break;
  }
  return true;
}

bool set_saves(int v) {
  if (!pg()) return false;
  const uint16_t n = static_cast<uint16_t>(v < 0 ? 0 : v > kSavesMax ? kSavesMax : v);
  gw<uint16_t>(kSaveCount, n);
  if (const uintptr_t copy = checkpoint_copy()) {
    const uintptr_t at = copy + (kSaveCount - kSaveRegion);
    if (mem::readable(at, 2)) *reinterpret_cast<uint16_t*>(at) = n;
  }
  return true;
}

bool set_difficulty(int v) {
  if (!pg()) return false;
  gw<uint8_t>(kDifficulty, static_cast<uint8_t>(v));
  // The game keeps Flags_SYSTEM's hard-mode bit with it (set wherever the
  // difficulty is: Professional and only Professional).
  uint32_t sys = gv<uint32_t>(kFlagsSystem);
  sys = v == 6 ? (sys | kSysHardMode) : (sys & ~kSysHardMode);
  gw<uint32_t>(kFlagsSystem, sys);
  return true;
}

// --- the outfit -----------------------------------------------------------------------------------------

int costume_set() { return pg() ? gv<uint8_t>(kSubCostume) : -1; }

bool set_costume_set(int v) {
  if (!pg() || v < 0 || v > 2) return false;
  gw<uint8_t>(kSubCostume, static_cast<uint8_t>(v));
  return true;
}

// --- the countdown ------------------------------------------------------------------------------------

bool countdown(Countdown* out) {
  *out = Countdown{};
  if (!g_cockpit) return false;
  const uintptr_t cd = g_cockpit + kCountdown;
  out->known = true;
  out->running = (mem::read<uint32_t>(cd) & 1) != 0;
  out->editable = g_clock_keep != 0;
  out->frames = mem::read<float>(cd + 8);
  return true;
}

bool set_countdown(float frames) {
  if (!g_cockpit || !g_clock_keep || !g_option_flag || !mem::read<uint8_t>(g_option_flag)) return false;
  const uintptr_t cd = g_cockpit + kCountdown;
  if (!(mem::read<uint32_t>(cd) & 1) || !(mem::read<uint32_t>(g_clock_keep) & 1)) return false;
  // The clock comes back from the copy the option screen took as it opened, so
  // the edit goes there; and into the CountDown, which the panel shows meanwhile.
  const float f = frames < 1 ? 1 : frames;
  *reinterpret_cast<float*>(g_clock_keep + 4) = f;
  *reinterpret_cast<float*>(cd + 8) = f;
  return true;
}

// --- patch sites ------------------------------------------------------------------------------------

mem::Patch* patch(Site s) {
  mem::Patch& p = g_patch[static_cast<int>(s)];
  return p.prepared() ? &p : nullptr;
}

// --- Steady aim's hold ------------------------------------------------------------------------------

namespace {

// Pointers read every frame, each checked only when it changes (see pg()).
uintptr_t g_player_ok = 0, g_plwep_ok = 0, g_objwep_ok = 0;
bool checked(uintptr_t p, size_t n, uintptr_t* ok) {
  if (!p) return false;
  if (p != *ok) {
    if (!mem::readable(p, n)) return false;
    *ok = p;
  }
  return true;
}

// A model's part n as the game's lookup finds it: +F4 the first part, then an
// array of 0x1D8-byte parts (the model's +4 has 0x2000) or a list through each
// part's +F4. Walked and checked once per model (and first part); a live
// model's parts stay where they are.
struct PartCache {
  uintptr_t model = 0, first = 0, part = 0;
  int n = -1;
};
PartCache g_gun_part, g_hand_part;
uintptr_t model_part(PartCache* c, uintptr_t model, int n) {
  const uintptr_t first = mem::read<uintptr_t>(model + kModelParts);
  if (model == c->model && first == c->first && n == c->n) return c->part;
  *c = PartCache{};
  if (!first) return 0;
  uintptr_t p = first;
  if (mem::read<uint32_t>(model + kModelFlags) & 0x2000) {
    p += static_cast<uintptr_t>(n) * kPartSize;
  } else {
    for (int i = 0; i < n; ++i) {
      if (!mem::readable(p + kModelParts, 4)) return 0;
      p = mem::read<uintptr_t>(p + kModelParts);
      if (!p) return 0;
    }
  }
  if (!mem::readable(p + kPartMatrix, 12 * sizeof(float))) return 0;
  *c = PartCache{model, first, p, n};
  return p;
}

}  // namespace

bool aim_hold_known() {
  if (!g_player_slot) return false;
  for (Site s : {Site::kHoldLaser, Site::kHoldHand1, Site::kHoldHand2, Site::kHoldAuto1, Site::kHoldAuto2,
                 Site::kHoldShotgun})
    if (!patch(s)) return false;
  return true;
}

bool aim_pose(AimPose* out) {
  *out = AimPose{};
  if (!g_player_slot || !pg()) return false;
  const uintptr_t pl = mem::read<uintptr_t>(g_player_slot);
  if (!checked(pl, kPlayerSize, &g_player_ok)) return false;
  const uintptr_t plwep = mem::read<uintptr_t>(pl + kPlWep);
  if (!checked(plwep, kPlWepSize, &g_plwep_ok)) return false;
  const uintptr_t wep = mem::read<uintptr_t>(plwep + kWepObj);
  if (!checked(wep, kObjWepSize, &g_objwep_ok)) return false;
  out->weapon = wep;
  out->weapon_no = gv<uint8_t>(kWeaponNo);
  if (mem::read<uint8_t>(pl + kPlRno) == 0 && mem::read<uint8_t>(pl + kPlRno + 1) == kRnoWeapon) {
    const uint8_t step = mem::read<uint8_t>(pl + kPlRno + 2);
    out->step = step == kStepAim ? AimStep::kAim : step == kStepFire ? AimStep::kFire : AimStep::kNone;
  }
  out->centre[0] = mem::read<float>(plwep + kWepCentre);
  out->centre[1] = mem::read<float>(plwep + kWepCentre + 4);
  out->aim_lr = mem::read<float>(pl + kPlAimLr);
  out->yaw = mem::read<float>(pl + kPlYaw);
  for (int i = 0; i < 3; ++i) out->pos[i] = mem::read<float>(pl + kPlPos + 4 * i);
  // The laser's part: 1 for the mine thrower and the P.R.L. 412, else 0 (as the
  // laser's own lookup picks it); a shot's: the player's 10.
  const int gun_part = out->weapon_no == 0xE || out->weapon_no == 0x2E ? 1 : 0;
  const uintptr_t gun = model_part(&g_gun_part, wep, gun_part), hand = model_part(&g_hand_part, pl, 10);
  if (!gun || !hand) return false;
  std::memcpy(out->gun, reinterpret_cast<const void*>(gun + kPartMatrix), sizeof(out->gun));
  std::memcpy(out->hand, reinterpret_cast<const void*>(hand + kPartMatrix), sizeof(out->hand));
  out->ok = true;
  return true;
}

void set_held_pose(const float gun[12], const float hand[12]) {
  std::memcpy(g_held_gun.m, gun, sizeof(g_held_gun.m));
  std::memcpy(g_held_hand.m, hand, sizeof(g_held_hand.m));
}

// --- Steady aim's stance ------------------------------------------------------------------------------

namespace {

// The player's motion when it is an aim's loop held still: its clock stopped,
// and blended with the aim's helper's motion B. 0 otherwise.
uintptr_t held_aim_loop() {
  if (!g_aim_blend || !g_player_slot) return 0;
  const uintptr_t pl = mem::read<uintptr_t>(g_player_slot);
  if (!checked(pl, kPlayerSize, &g_player_ok)) return 0;
  if (mem::read<uintptr_t>(pl + kMotionB) != g_aim_blend + kBlendB) return 0;
  const uint16_t attr = mem::read<uint16_t>(pl + kMotion + kMotAttr);
  return (attr & (kMotLoop | kMotStop)) == (kMotLoop | kMotStop) ? pl + kMotion : 0;
}

// B is set on the loop's frame (and again on it whenever the aim crosses level)
// and runs at the loop's speed, but on a clock of its own, which the loop's stop
// does not reach. Its clock goes back to the loop's - a frame B has - before
// the game moves the two: what is drawn is the loop's frame in both.
void sync_blend(uintptr_t loop) {
  const uintptr_t b = g_aim_blend + kBlendB;
  const float frame = mem::read<float>(loop + kMotFrame);
  if (!(frame >= 0 && frame < mem::read<uint16_t>(b + kMotFrames))) return;
  std::memcpy(reinterpret_cast<void*>(b + kMotClock), reinterpret_cast<const void*>(loop + kMotClock), kMotClockSize);
}

}  // namespace

bool aim_stance_known() { return g_aim_blend && g_player_slot; }

void keep_stance() {
  if (const uintptr_t loop = held_aim_loop()) sync_blend(loop);
}

// The two clocks run on together from the loop's frame.
void let_stance_go() {
  const uintptr_t loop = held_aim_loop();
  if (!loop) return;
  sync_blend(loop);
  *reinterpret_cast<uint16_t*>(loop + kMotAttr) = static_cast<uint16_t>(mem::read<uint16_t>(loop + kMotAttr) & ~kMotStop);
}

// --- the shooting gallery ------------------------------------------------------------------------------

namespace {

// The gallery's work.
constexpr int kGalWorkSize = 0x80;
constexpr int kGalState = 0x0, kGalHits = 0xC, kGalScore = 0x10, kGalGame = 0x1C, kGalScript = 0x20;
constexpr int kGalStreak = 0x24, kGalFlags = 0x40, kGalCooldown = 0x68;
constexpr int kGlobalFlags174 = 0x174;
constexpr uint32_t kBonusUnderWay = 0x40000000u;  // pG+174: the bonus target's task is running
constexpr int kMainLoop = 3, kOption = 6;
// An enemy (cEm) and a target's (cEmMark) fields; its damage record at +328 as
// a shot writes it (FUN_005aeef0); a hit part's world position at +C.
constexpr int kEmFlags = 0x4, kEmNext = 0x8, kEmPos = 0x94, kEmRno = 0xFC, kEmId = 0x100, kEmType = 0x101;
constexpr int kEmHp = 0x324, kEmDmg = 0x328, kEmDmgPending = 0x32C, kEmDmgFrames = 0x32D, kEmDmgWeapon = 0x32E;
constexpr int kEmDmgFrom = 0x330, kEmDmgDist = 0x33C, kEmDmgPart = 0x340, kEmAge = 0x4FC;
constexpr int kEmSub = 0xFD, kEmCmd = 0x4D8, kEmTimer = 0x4DC;  // the command it is on, the rise's and wait's timer
constexpr int kEmMarkSize = 0x500, kHitPartSize = 0x34, kHitPartPos = 0xC;
constexpr uint8_t kIdMark = 0x3E;
constexpr uint8_t kDmgFrames = 10;  // a shot's record stays up 10 frames
constexpr uint8_t kWeaponMax = 0x30;  // a shot's weapon numbers (FUN_005aeef0 makes any above it a 2)

uintptr_t g_gallery_ok = 0;
uintptr_t g_pool_base = 0, g_pool_end = 0, g_pool_stride = 0;

// The enemy pool as it is now, checked when it changes (the room's set-up makes it).
bool em_pool() {
  const uintptr_t base = mem::read<uintptr_t>(g_em_base_slot);
  const uint32_t count = mem::read<uint32_t>(g_em_count_slot), stride = mem::read<uint32_t>(g_em_stride_slot);
  if (!base || !count || count > 4096 || stride < kEmMarkSize || stride > 0x4000) return false;
  const uintptr_t end = base + static_cast<uintptr_t>(count) * stride;
  if (base != g_pool_base || end != g_pool_end || stride != g_pool_stride) {
    g_pool_base = g_pool_end = g_pool_stride = 0;
    if (!mem::readable(base, end - base)) return false;
    g_pool_base = base;
    g_pool_end = end;
    g_pool_stride = stride;
  }
  return true;
}
bool in_pool(uintptr_t em) {
  return em >= g_pool_base && em < g_pool_end && (em - g_pool_base) % g_pool_stride == 0;
}
bool live_mark(uintptr_t em) {
  return in_pool(em) && (mem::read<uint32_t>(em + kEmFlags) & 0x201) == 1 && mem::read<uint8_t>(em + kEmId) == kIdMark;
}

// The frames a target has before it goes down (its health gone), from the
// script's commands it has left (kSigGalleryStep, the targets' routines): a rise
// takes 11 frames (one to start, ten counted), a wait its count and one, a move
// its distance over its speed and one, and going down takes the health in its
// first frame - whose hit check still finds it. -1: it stays (command 4), or its
// commands are not readable.
int target_life(uintptr_t em) {
  uintptr_t c = mem::read<uintptr_t>(em + kEmCmd);
  const uint8_t sub = mem::read<uint8_t>(em + kEmSub);
  const float timer = mem::read<float>(em + kEmTimer);
  float pos[3];
  for (int i = 0; i < 3; ++i) pos[i] = mem::read<float>(em + kEmPos + 4 * i);
  float frames = 0;
  const mem::Range image = mem::module_range(g_exe);
  for (int n = 0; n < 64; ++n) {
    if (!image.contains(c) || !image.contains(c + 0x13)) return -1;
    const int32_t op = mem::read<int32_t>(c);
    const bool now = n == 0;
    switch (op) {
      case 0:
        frames += now && sub ? 10.0f - timer : 11.0f;
        c += 4;
        break;
      case 1:
        return static_cast<int>(frames) + 1;
      case 2: {
        const float count = static_cast<float>(mem::read<int32_t>(c + 4));
        frames += now && sub ? (timer > 0 ? timer : 0) : count + 1;
        c += 8;
        break;
      }
      case 3: {
        float to[3], d = 0;
        for (int i = 0; i < 3; ++i) {
          to[i] = static_cast<float>(mem::read<int32_t>(c + 4 + 4 * i));
          d += (to[i] - pos[i]) * (to[i] - pos[i]);
        }
        const int32_t speed = mem::read<int32_t>(c + 16);
        if (speed <= 0) return -1;
        frames += std::sqrt(d) / static_cast<float>(speed) + 1;
        for (int i = 0; i < 3; ++i) pos[i] = to[i];
        c += 0x14;
        break;
      }
      default:
        return -1;
    }
  }
  return -1;
}

// The work, in the gallery's room with the game in its main loop (or, to read
// it, paused there).
uintptr_t gallery_work(bool paused_too) {
  if (!g_gallery_slot || !pg()) return 0;
  const uint8_t rno = gv<uint8_t>(kRno0);
  if (!(rno == kMainLoop || (paused_too && rno == kOption)) || gv<uint16_t>(kRoom) != kGalleryRoom) return 0;
  const uintptr_t w = mem::read<uintptr_t>(g_gallery_slot);
  return checked(w, kGalWorkSize, &g_gallery_ok) ? w : 0;
}

}  // namespace

bool gallery_known() { return g_gallery_slot && g_em_list_slot; }

bool gallery(Gallery* out) {
  *out = Gallery{};
  const uintptr_t w = gallery_work(true);
  if (!w || !em_pool()) return false;
  out->state = mem::read<uint8_t>(w + kGalState);
  out->game = mem::read<int32_t>(w + kGalGame);
  out->script = mem::read<int32_t>(w + kGalScript);
  out->score = mem::read<int32_t>(w + kGalScore);
  out->hits = mem::read<int32_t>(w + kGalHits);
  out->streak = mem::read<int32_t>(w + kGalStreak);
  out->flags = mem::read<uint32_t>(w + kGalFlags);
  out->cooldown = mem::read<float>(w + kGalCooldown);
  out->bonus_phase = (gv<uint32_t>(kGlobalFlags174) & kBonusUnderWay) != 0;
  // The list of the live enemies, as the gallery walks it for its targets. A
  // link out of the pool is no list this knows: nothing from it is used.
  uintptr_t em = mem::read<uintptr_t>(g_em_list_slot);
  const uintptr_t most = (g_pool_end - g_pool_base) / g_pool_stride;
  for (uintptr_t i = 0; em && i <= most; ++i, em = mem::read<uintptr_t>(em + kEmNext)) {
    if (!in_pool(em)) {
      out->n = 0;
      return false;
    }
    if (!live_mark(em) || out->n >= kMaxGalleryTargets) continue;
    GalleryTarget& t = out->t[out->n++];
    t.em = em;
    t.type = mem::read<uint8_t>(em + kEmType);
    t.hp = mem::read<int16_t>(em + kEmHp);
    t.routine = mem::read<uint8_t>(em + kEmRno);
    t.pending = mem::read<uint8_t>(em + kEmDmgPending) != 0;
    t.age = mem::read<float>(em + kEmAge);
    t.x = mem::read<float>(em + kEmPos);
    t.life = t.type < 10 ? target_life(em) : -1;
    // The rise counts ten frames at +4DC once started (sub-step 1).
    t.rise = t.routine != 0 ? 1.0f
             : mem::read<uint8_t>(em + kEmSub) ? mem::read<float>(em + kEmTimer) / 10.0f
                                                : 0.0f;
  }
  return true;
}

bool gallery_hit(uintptr_t em, int part) {
  if (!gallery_work(false) || !em_pool() || !live_mark(em)) return false;
  if (part < kGalleryHead || part + kHitPartSize > kEmMarkSize) return false;
  if (mem::read<int16_t>(em + kEmHp) <= 0 || mem::read<uint8_t>(em + kEmDmgPending)) return false;
  const uintptr_t p = em + static_cast<uintptr_t>(part);
  float at[3], from[3];
  for (int i = 0; i < 3; ++i) at[i] = from[i] = mem::read<float>(p + kHitPartPos + 4 * i);
  // From where the player stands, as a shot's record has it (and the distance to
  // the part); the gallery's scoring reads neither.
  if (g_player_slot) {
    const uintptr_t pl = mem::read<uintptr_t>(g_player_slot);
    if (checked(pl, kPlayerSize, &g_player_ok))
      for (int i = 0; i < 3; ++i) from[i] = mem::read<float>(pl + kPlPos + 4 * i);
  }
  const float dx = at[0] - from[0], dy = at[1] - from[1], dz = at[2] - from[2];
  uint8_t weapon = gv<uint8_t>(kWeaponNo);
  if (weapon > kWeaponMax) weapon = 2;
  *reinterpret_cast<uint32_t*>(em + kEmDmg) = 0;
  *reinterpret_cast<uint8_t*>(em + kEmDmgFrames) = kDmgFrames;
  *reinterpret_cast<uint8_t*>(em + kEmDmgWeapon) = weapon;
  for (int i = 0; i < 3; ++i) *reinterpret_cast<float*>(em + kEmDmgFrom + 4 * i) = from[i];
  *reinterpret_cast<float*>(em + kEmDmgDist) = std::sqrt(dx * dx + dy * dy + dz * dz);
  *reinterpret_cast<uintptr_t*>(em + kEmDmgPart) = p;
  *reinterpret_cast<uint8_t*>(em + kEmDmgPending) = 1;  // last: the record is whole when it goes up
  return true;
}

// --- the Mercenaries ------------------------------------------------------------------------------------------

namespace {

// The work (the HUD's esi), and a Ganado (cEm10): +814 its kill scored, its hit
// parts a list from its head (+344) through each one's +30, the second its body
// (+834, the part number 2 at its +26).
constexpr int kMercWorkSize = 0x70;
constexpr int kMercScore = 0x24, kMercCombo = 0x28, kMercKills = 0x3C, kMercBonusKills = 0x40, kMercBonus = 0x48;
constexpr int kMercBanking = 0x4C, kMercBonusTime = 0x54, kMercFlags = 0x64;
constexpr uint32_t kMercBankingShown = 0x100000;  // +64: a bonus on its way onto the score (+4C)
constexpr uint32_t kMercBonusEnding = 0x200000;   // +64: the Bonus Points time's end being shown
constexpr uint32_t kMercBonusStart = 0x400000;    // +64: a Bonus Points time to show from its start
constexpr float kMercBonusFrames = 900.0f;        // a pickup's Bonus Points time (30 s)
constexpr int kEm10Size = 0xC64, kEm10Scored = 0x814, kEm10Head = 0x344, kEm10Body = 0x834;
constexpr int kEm10Melted = 0x7F5;  // its body gone (the stage's sweep then lets its slot go)
constexpr int kEm10Group = 0x6E0;   // the Ganados' one control (type 12): +13 thirteen frame counters
constexpr int kPartNo = 0x26, kPartNext = 0x30;
constexpr int16_t kPartBody = 2;
constexpr uint8_t kRnoMove = 1, kRnoReaction = 2;
// Every enemy's status bits (+3CC): bit 5 its life - the stages count the
// Ganados with it as the ones about (FUN_006c1010) and send more only below ten.
// +3A0 its entry in the enemy list in play (GLOBAL_WK +5410, 0x20 bytes an entry:
// +0 its flags, bit 1 sent; +1 its id; +18 its room), +395 its set (how it starts).
constexpr int kEmStatus = 0x3CC, kEmEntry = 0x3A0, kEmSet = 0x395;
constexpr uint32_t kEmAlive = 0x20;
constexpr int kEmList = 0x5410, kEmListEntry = 0x20, kEmListLength = 256, kEmListRoom = 0x18;
// The list's dead mask (FUN_005B2010 at a death): a bit an entry from the top,
// 0x20 bytes a list, the list in play at +4FB3 (-1: none, and no mask).
constexpr int kEmListDead = 0x502C, kEmListNo = 0x4FB3;
constexpr uint8_t kEntrySent = 2;  // an entry's +0: setEm took it (FUN_005B27B0)
constexpr int kModelPartCount = 0x102;  // how many parts a model has (every enemy is a model; its parts at +F4)
constexpr int kGanadoFirst = 0x10, kGanadoLast = 0x20;  // the ids the stages count
constexpr uint8_t kHandgun = 2;  // a handgun's weapon number: a plain gunshot's reaction
constexpr int kAreaFlags = 0x17C;  // pG+17C: the room's area flags, one a scene area the player stands in

uintptr_t g_merc_ok = 0;

bool merc_game() {
  if (!pg()) return false;
  const uint8_t rno = gv<uint8_t>(kRno0);
  return (gv<uint32_t>(kFlagsSystem) & kSysMercenaries) && rno >= 2 && rno <= 6;
}

bool live_em10(uintptr_t em) {
  return in_pool(em) && g_pool_stride >= static_cast<uintptr_t>(kEm10Size) &&
         (mem::read<uint32_t>(em + kEmFlags) & 0x201) == 1 && mem::read<uintptr_t>(em) == g_em10_vtable;
}

// A Ganado's body, if its list of hit parts has it where its set-up puts it.
uintptr_t em10_body(uintptr_t em) {
  uintptr_t p = em + kEm10Head;
  for (int i = 0; i < 16 && p; ++i) {
    if (p == em + kEm10Body) return mem::read<int16_t>(p + kPartNo) == kPartBody ? p : 0;
    p = mem::read<uintptr_t>(p + kPartNext);
    if (p && (p < em + kEm10Head || p + kHitPartSize > em + kEm10Size)) return 0;  // its parts are its own
  }
  return 0;
}

// The Ganados alive, as the enemy manager's list has them, in the Mercenaries'
// main loop.
bool merc_list_ready() {
  return g_em10_vtable && g_em_list_slot && merc_game() && gv<uint8_t>(kRno0) == kMainLoop && em_pool();
}

}  // namespace

bool merc_known() { return g_merc_work != 0; }

bool merc(Merc* out) {
  *out = Merc{};
  if (!g_merc_work || !merc_game() || !checked(g_merc_work, kMercWorkSize, &g_merc_ok)) return false;
  const uintptr_t w = g_merc_work;
  out->score = mem::read<int32_t>(w + kMercScore);
  out->combo = mem::read<int32_t>(w + kMercCombo);
  out->bonus = mem::read<int32_t>(w + kMercBonus);
  if (mem::read<uint32_t>(w + kMercFlags) & kMercBankingShown) out->bonus += mem::read<int32_t>(w + kMercBanking);
  out->kills = mem::read<int32_t>(w + kMercKills);
  out->bonus_time =
      mem::read<float>(w + kMercBonusTime) >= 1.0f && !(mem::read<uint32_t>(w + kMercFlags) & kMercBonusEnding);
  out->bonus_kills = mem::read<int32_t>(w + kMercBonusKills);
  return true;
}

// As the pickup (FUN_0070f6c0) does: over (below a frame of it left) or ending,
// it starts again from its start - the kills in it from 0 if it was over - and
// one running is put back at a pickup's 900 frames.
bool merc_bonus_time() {
  if (!g_merc_work || !merc_game() || !checked(g_merc_work, kMercWorkSize, &g_merc_ok)) return false;
  const uintptr_t w = g_merc_work;
  const float left = mem::read<float>(w + kMercBonusTime);
  const uint32_t flags = mem::read<uint32_t>(w + kMercFlags);
  if (left < 1.0f || (flags & kMercBonusEnding)) {
    if (left < 1.0f) *reinterpret_cast<int32_t*>(w + kMercBonusKills) = 0;
    *reinterpret_cast<uint32_t*>(w + kMercFlags) = (flags | kMercBonusStart) & ~kMercBonusEnding;
  }
  if (left < kMercBonusFrames) *reinterpret_cast<float*>(w + kMercBonusTime) = kMercBonusFrames;
  return true;
}

bool merc_kills_known() { return g_em10_vtable && g_em_list_slot; }

int merc_enemies(MercEnemy* out, int max) {
  if (!merc_list_ready()) return 0;
  int n = 0;
  uintptr_t em = mem::read<uintptr_t>(g_em_list_slot);
  const uintptr_t most = (g_pool_end - g_pool_base) / g_pool_stride;
  for (uintptr_t i = 0; em && i <= most && n < max; ++i, em = mem::read<uintptr_t>(em + kEmNext)) {
    if (!in_pool(em)) return 0;  // a link out of the pool is no list this knows
    if (!live_em10(em)) continue;
    MercEnemy& e = out[n++];
    e.em = em;
    e.id = mem::read<uint8_t>(em + kEmId);
    e.type = mem::read<uint8_t>(em + kEmType);
    e.routine = mem::read<uint8_t>(em + kEmRno);
    e.hp = mem::read<int16_t>(em + kEmHp);
    e.pending = mem::read<uint8_t>(em + kEmDmgPending) != 0;
    e.scored = mem::read<uint8_t>(em + kEm10Scored) != 0;
    e.alive = (mem::read<uint32_t>(em + kEmStatus) & kEmAlive) != 0;
  }
  return n;
}

bool merc_kill(uintptr_t em) {
  if (!merc_list_ready() || !live_em10(em)) return false;
  const uint8_t rno = mem::read<uint8_t>(em + kEmRno);
  const bool alive = (mem::read<uint32_t>(em + kEmStatus) & kEmAlive) != 0;
  if (!(rno == kRnoMove || (rno == kRnoReaction && alive)) || mem::read<uint8_t>(em + kEm10Scored) ||
      mem::read<uint8_t>(em + kEmDmgPending))
    return false;
  const uintptr_t body = em10_body(em);
  if (!body) return false;
  float at[3], from[3];
  for (int i = 0; i < 3; ++i) at[i] = from[i] = mem::read<float>(body + kHitPartPos + 4 * i);
  // From where the player stands, as a shot's record has it.
  if (g_player_slot) {
    const uintptr_t pl = mem::read<uintptr_t>(g_player_slot);
    if (checked(pl, kPlayerSize, &g_player_ok))
      for (int i = 0; i < 3; ++i) from[i] = mem::read<float>(pl + kPlPos + 4 * i);
  }
  const float dx = at[0] - from[0], dy = at[1] - from[1], dz = at[2] - from[2];
  // One point of health: the hit's damage takes it (a handgun's is 150 on every
  // Ganado, at least 4 after the game's cuts - the Garradors', the dynamic
  // difficulty's), as LifeDownSet2 takes a shot's; type 6, which takes no damage,
  // dies of any hit. The paths that kill a Ganado riding something act only on
  // health above 0, so it is not 0 here.
  *reinterpret_cast<int16_t*>(em + kEmHp) = 1;
  *reinterpret_cast<uint32_t*>(em + kEmDmg) = 0;
  *reinterpret_cast<uint8_t*>(em + kEmDmgFrames) = kDmgFrames;
  *reinterpret_cast<uint8_t*>(em + kEmDmgWeapon) = kHandgun;
  for (int i = 0; i < 3; ++i) *reinterpret_cast<float*>(em + kEmDmgFrom + 4 * i) = from[i];
  *reinterpret_cast<float*>(em + kEmDmgDist) = std::sqrt(dx * dx + dy * dy + dz * dz);
  *reinterpret_cast<uintptr_t*>(em + kEmDmgPart) = body;
  *reinterpret_cast<uint8_t*>(em + kEmDmgPending) = 1;  // last: the record is whole when it goes up
  return true;
}

bool merc_all_areas(unsigned tick) {
  if (!merc_game()) return false;
  constexpr uint16_t kCastle = 0x402;
  // In the castle its bosses' area (4000000) stays up too: the second boss waits 150 frames of it up (+5A0).
  constexpr uint32_t kBosses = 0x4000000;
  gw<uint32_t>(kAreaFlags,
               g_castle_slot && gv<uint16_t>(kRoom) == kCastle ? (0x00010001u << (tick & 15)) | kBosses : 0xFFFFFFFFu);
  return true;
}

bool merc_pool(MercPool* out) {
  *out = MercPool{};
  if (!merc_game() || !em_pool()) return false;
  out->slots = static_cast<int>((g_pool_end - g_pool_base) / g_pool_stride);
  for (uintptr_t em = g_pool_base; em < g_pool_end; em += g_pool_stride) {
    const uint32_t f = mem::read<uint32_t>(em + kEmFlags);
    if (!(f & 0x601)) continue;
    if ((f & 0x201) != 1) {
      ++out->going;
      continue;
    }
    ++out->used;
    const uint8_t id = mem::read<uint8_t>(em + kEmId);
    if (id < kGanadoFirst || id > kGanadoLast) continue;
    if (mem::read<uint32_t>(em + kEmStatus) & kEmAlive) {
      ++out->alive;
    } else {
      ++out->bodies;
      if (g_pool_stride >= static_cast<uintptr_t>(kEm10Size) && mem::read<uintptr_t>(em) == g_em10_vtable &&
          mem::read<uint8_t>(em + kEm10Melted))
        ++out->melted;
    }
  }
  const uintptr_t list = pg() + kEmList;
  const uint16_t room = gv<uint16_t>(kRoom);
  for (int i = 0; i < kEmListLength; ++i) {
    const uintptr_t e = list + static_cast<uintptr_t>(i) * kEmListEntry;
    const uint8_t id = mem::read<uint8_t>(e + 1);
    if (id < kGanadoFirst || id > kGanadoLast || mem::read<uint16_t>(e + kEmListRoom) != room) continue;
    ++out->listed;
    if (!(mem::read<uint8_t>(e) & 2)) ++out->unsent;
  }
  return true;
}

namespace {
uintptr_t castle_work();  // below, with the castle's give-back and gates
}  // namespace

void merc_census(const char* why) {
  MercPool p;
  if (!merc_pool(&p)) return;
  Merc m;
  merc(&m);
  logf("census (%s): kills %d, combo %d, score %d; the pool %d slots - %d in use, %d being let go; the stage counts %d "
       "Ganados about (it sends more below 10), %d bodies (%d gone, for the sweep); %d of the room's %d Ganados not "
       "sent yet; area flags %08X %08X, +170 %08X, +174 %08X, +501C %08X, +5020 %08X",
       why, m.kills, m.combo, m.score, p.slots, p.used, p.going, p.alive, p.bodies, p.melted, p.unsent, p.listed,
       gv<uint32_t>(kAreaFlags), gv<uint32_t>(kAreaFlags + 4), gv<uint32_t>(0x170), gv<uint32_t>(0x174),
       gv<uint32_t>(0x501C), gv<uint32_t>(0x5020));
  // What else is in the pool, by id.
  int by_id[256] = {};
  for (uintptr_t em = g_pool_base; em < g_pool_end; em += g_pool_stride)
    if ((mem::read<uint32_t>(em + kEmFlags) & 0x201) == 1) ++by_id[mem::read<uint8_t>(em + kEmId)];
  char ids[400];
  int len = 0;
  for (int id = 0; id < 256 && len < static_cast<int>(sizeof(ids)) - 16; ++id)
    if (by_id[id]) len += std::snprintf(ids + len, sizeof(ids) - len, "%s%02X x%d", len ? ", " : "", static_cast<unsigned>(id), by_id[id]);
  ids[len] = 0;
  logf("census: in use by id: %s", len ? ids : "none");
  // Which slots the enemy manager's list (the moves) reaches.
  bool listed[512] = {};
  const uintptr_t most = (g_pool_end - g_pool_base) / g_pool_stride;
  uintptr_t em = g_em_list_slot ? mem::read<uintptr_t>(g_em_list_slot) : 0;
  for (uintptr_t i = 0; em && i <= most && in_pool(em); ++i, em = mem::read<uintptr_t>(em + kEmNext)) {
    const uintptr_t slot = (em - g_pool_base) / g_pool_stride;
    if (slot < 512) listed[slot] = true;
  }
  float at[3] = {};
  const uintptr_t pl = g_player_slot ? mem::read<uintptr_t>(g_player_slot) : 0;
  if (pl && checked(pl, kPlayerSize, &g_player_ok))
    for (int i = 0; i < 3; ++i) at[i] = mem::read<float>(pl + kPlPos + 4 * i);
  uintptr_t control = 0;
  for (uintptr_t e = g_pool_base; e < g_pool_end; e += g_pool_stride) {
    if ((mem::read<uint32_t>(e + kEmFlags) & 0x201) != 1) continue;
    const uint8_t id = mem::read<uint8_t>(e + kEmId);
    if (id < kGanadoFirst || id > kGanadoLast) continue;
    const bool em10 = g_pool_stride >= static_cast<uintptr_t>(kEm10Size) && mem::read<uintptr_t>(e) == g_em10_vtable;
    const float dx = mem::read<float>(e + kEmPos) - at[0], dz = mem::read<float>(e + kEmPos + 8) - at[2];
    const uintptr_t slot = (e - g_pool_base) / g_pool_stride;
    auto u8 = [&](int off) { return static_cast<unsigned>(mem::read<uint8_t>(e + off)); };
    const uint32_t status = mem::read<uint32_t>(e + kEmStatus);
    logf("census: slot %2u entry %3u id %02X type %2u set %02X routine %02X %02X %02X %02X status %08X %s hp %d%s%s%s%s"
         "%s, %.0f away",
         static_cast<unsigned>(slot), u8(kEmEntry), static_cast<unsigned>(id), u8(kEmType), u8(kEmSet), u8(kEmRno),
         u8(kEmRno + 1), u8(kEmRno + 2), u8(kEmRno + 3), static_cast<unsigned>(status),
         (status & kEmAlive) ? "about" : "down", static_cast<int>(mem::read<int16_t>(e + kEmHp)),
         u8(kEmDmgPending) ? ", a hit waiting" : "", em10 && u8(kEm10Scored) ? ", scored" : "",
         em10 && u8(kEm10Melted) ? ", body gone" : "", slot < 512 && listed[slot] ? "" : ", off the list",
         em10 ? (em10_body(e) ? "" : ", no body part") : ", not cEm10", std::sqrt(dx * dx + dz * dz));
    if (em10 && !control) control = mem::read<uintptr_t>(e + kEm10Group);
  }
  // The Ganados' one control (type 12): its eighth counter holds back the next
  // body's going (15 to 24 frames after the last).
  if (control && mem::readable(control, 0x40) && mem::read<uint8_t>(control + 0xC) == 0x12) {
    char c[160];
    int n = 0;
    for (int i = 0; i < 13; ++i)
      n += std::snprintf(c + n, sizeof(c) - n, " %d", static_cast<int>(mem::read<int16_t>(control + 0x13 + 2 * i)));
    logf("census: the Ganados' control: counters%s", c);
  }
  // The models' memory, and the room's entries: sent with no enemy and not
  // dead is an entry whose set-up failed.
  MercMemory mm;
  MercEntries en;
  if (merc_memory(&mm))
    logf("census: the models' memory: records %d of %d in use, parts %d of %d (a Ganado here has up to %d)",
         mm.models_used, mm.models, mm.parts_used, mm.parts, mm.ganado_parts);
  if (merc_entries(&en)) {
    int ganados = 0, sent = 0, dead = 0, enemy = 0, lost = 0;
    for (int w = 0; w < 8; ++w) {
      ganados += __builtin_popcount(en.ganado[w]);
      sent += __builtin_popcount(en.ganado[w] & en.sent[w]);
      dead += __builtin_popcount(en.ganado[w] & en.dead[w]);
      enemy += __builtin_popcount(en.ganado[w] & en.enemy[w]);
      lost += __builtin_popcount(en.ganado[w] & en.sent[w] & ~en.enemy[w] & ~en.dead[w]);
    }
    logf("census: the room's list: %d Ganados, %d sent, %d marked dead%s, %d with an enemy in the pool, %d sent with "
         "none and not dead", ganados, sent, dead, en.dead_known ? "" : " (the list in play has no dead mask)", enemy,
         lost);
    // The entries not sent: which, of what type and health, and in the village
    // what sends it.
    MercLists lists;
    const bool village = merc_village_lists(&lists);
    MercWater water;
    const bool waterworld = merc_water(&water);
    uint32_t zoned[8] = {};
    for (int z = 0; waterworld && z < g_water_zone_n; ++z)
      for (const uint8_t e : g_water_zones[z].entry) zoned[e >> 5] |= 1u << (e & 31);
    char unsent[2400];
    int len = 0;
    const uintptr_t list = pg() + kEmList;
    for (int i = 0; i < kEmListLength && len < static_cast<int>(sizeof(unsent)) - 48; ++i) {
      const uint32_t bit = 1u << (i & 31);
      if (!(en.ganado[i >> 5] & bit) || (en.sent[i >> 5] & bit)) continue;
      const uintptr_t e = list + static_cast<uintptr_t>(i) * kEmListEntry;
      const char* by = "";
      if (village) {
        by = lists.area[i >> 5] & bit ? ", an area list's" : lists.events[i >> 5] & bit ? ", a pickup's event's" : "";
        for (const VillageWave& w : g_village_waves)
          if (g_village_waves_known && gv<uint16_t>(kRoom) == 0x400 && (i == w.entry || i == w.entry + 1))
            by = lists.waves[i >> 5] & bit ? ", a chainsaws' wave's (gone)" : ", a chainsaws' wave's (to come)";
      }
      if (waterworld)
        by = zoned[i >> 5] & bit                ? ", a zone's"
             : g_water_oneshots[i >> 5] & bit   ? ", a one-shot area's"
             : g_water_lab[i >> 5] & bit        ? ", the once-at-30's (it sends one of two)"
                                                : ", another start group's (not this game's)";
      len += std::snprintf(unsent + len, sizeof(unsent) - len, "%s%d (type %u, %d%s)", len ? ", " : "", i,
                           static_cast<unsigned>(mem::read<uint8_t>(e + 2)), static_cast<int>(mem::read<int16_t>(e + 8)),
                           by);
    }
    if (len) logf("census: not sent: %s", unsent);
  }
  // The castle: each counted group's sends against its cap, its gates, its flags.
  const uintptr_t work = castle_work();
  if (work && g_castle_group_n) {
    char groups[600];
    int n = 0;
    for (int g = 0; g < g_castle_group_n && n < static_cast<int>(sizeof(groups)) - 32; ++g)
      n += std::snprintf(groups + n, sizeof(groups) - n, "%s+%X %d/%d", n ? ", " : "", g_castle_groups[g].counter,
                         mem::read<int32_t>(work + g_castle_groups[g].counter), g_castle_groups[g].cap);
    MercGate gates[2];
    merc_castle_gates(gates);
    logf("census: the castle's counted groups: %s; its gates (area triggers %02X, %02X): %s, %s; pG+174 %08X, +178 "
         "%08X", groups, gates[0].at, gates[1].at, !gates[0].found ? "not found" : gates[0].enabled ? "shut" : "open",
         !gates[1].found ? "not found" : gates[1].enabled ? "shut" : "open", gv<uint32_t>(0x174), gv<uint32_t>(0x178));
  }
  // The water stage: each zone's sends against the most it sends, its one-shots, the once-at-30.
  MercWater water;
  if (merc_water(&water)) {
    char zones[400];
    int n = 0;
    for (int k = 0; k < water.zones && n < static_cast<int>(sizeof(zones)) - 40; ++k) {
      int ready = 0;
      for (const bool r : water.zone[k].ready) ready += r;
      n += std::snprintf(zones + n, sizeof(zones) - n, "%s%02X %d of %d (%d ready)", n ? ", " : "", water.zone[k].at,
                         water.zone[k].sent, water.zone[k].ends - 1 + 5, ready);
    }
    logf("census: the water stage's zones (area: its sends, of the most it sends; its next frame's ready): %s; its "
         "one-shot areas' Ganados %d of %d sent; its once-at-30 %s (%d of %d); its start group %d; at most %d in all; "
         "its count of those about %d; the player %son a ladder", n ? zones : "none", water.oneshots_sent,
         water.oneshots, water.lab_done ? "has sent" : "waits", water.sends - water.about, water.lab_at, water.start,
         water.most, water.about, water.ladder ? "" : "not ");
  }
}

namespace {

// A cManager's slots in use (+4 & 0x601, as its allocator tests them), its
// region checked again only when the manager, its slots, count or stride change.
struct PoolSeen {
  uintptr_t manager = 0, base = 0;
  uint32_t count = 0, stride = 0;
};
PoolSeen g_models_seen, g_parts_seen;

bool pool_use(uintptr_t slot, PoolSeen* seen, int* count, int* used) {
  const uintptr_t manager = mem::read<uintptr_t>(slot);
  if (!manager) return false;
  if (manager != seen->manager) {
    *seen = PoolSeen{};
    if (!mem::readable(manager, 0x14)) return false;
    seen->manager = manager;
  }
  const uintptr_t base = mem::read<uintptr_t>(manager + 4);
  const uint32_t n = mem::read<uint32_t>(manager + 8), stride = mem::read<uint32_t>(manager + 0xC);
  if (!base || !n || n > 0x10000 || stride < 8 || stride > 0x4000) return false;
  if (base != seen->base || n != seen->count || stride != seen->stride) {
    seen->base = 0;
    if (!mem::readable(base, static_cast<size_t>(n) * stride)) return false;
    seen->base = base;
    seen->count = n;
    seen->stride = stride;
  }
  int u = 0;
  for (uint32_t i = 0; i < n; ++i)
    if (mem::read<uint32_t>(base + static_cast<uintptr_t>(i) * stride + 4) & 0x601) ++u;
  *count = static_cast<int>(n);
  *used = u;
  return true;
}

}  // namespace

bool merc_memory_known() { return g_models_slot && g_parts_slot; }

bool merc_memory(MercMemory* out) {
  *out = MercMemory{};
  if (!merc_memory_known() || !merc_game() || !em_pool()) return false;
  if (!pool_use(g_models_slot, &g_models_seen, &out->models, &out->models_used) ||
      !pool_use(g_parts_slot, &g_parts_seen, &out->parts, &out->parts_used))
    return false;
  const bool em10_fits = g_pool_stride >= static_cast<uintptr_t>(kEm10Size);
  out->ems = static_cast<int>((g_pool_end - g_pool_base) / g_pool_stride);
  for (uintptr_t em = g_pool_base; em < g_pool_end; em += g_pool_stride) {
    const uint32_t flags = mem::read<uint32_t>(em + kEmFlags);
    if (flags & 0x601) ++out->ems_used;  // taken, as the pool's create tests it
    if ((flags & 0x201) != 1) continue;
    const uint8_t id = mem::read<uint8_t>(em + kEmId);
    if (id < kGanadoFirst || id > kGanadoLast) continue;
    const int parts = mem::read<uint8_t>(em + kModelPartCount);
    if (parts > out->ganado_parts) out->ganado_parts = parts;
    if (!(mem::read<uint32_t>(em + kEmStatus) & kEmAlive)) {
      ++out->bodies;
      continue;
    }
    ++out->alive;
    const bool scored = em10_fits && mem::read<uintptr_t>(em) == g_em10_vtable && mem::read<uint8_t>(em + kEm10Scored);
    if (scored || mem::read<uint8_t>(em + kEmDmgPending)) ++out->dying;
  }
  return true;
}

bool merc_entries(MercEntries* out) {
  *out = MercEntries{};
  if (!merc_game() || !em_pool()) return false;
  const uintptr_t list = pg() + kEmList;
  const uint16_t room = gv<uint16_t>(kRoom);
  const int no = gv<int8_t>(kEmListNo);
  out->dead_known = no >= 0 && kEmListDead + no * 0x20 + 0x20 <= kGlobalSize;
  for (int i = 0; i < kEmListLength; ++i) {
    const uintptr_t e = list + static_cast<uintptr_t>(i) * kEmListEntry;
    const uint8_t id = mem::read<uint8_t>(e + 1);
    if (id < kGanadoFirst || id > kGanadoLast || mem::read<uint16_t>(e + kEmListRoom) != room) continue;
    const uint32_t bit = 1u << (i & 31);
    out->ganado[i >> 5] |= bit;
    if (mem::read<uint8_t>(e) & kEntrySent) out->sent[i >> 5] |= bit;
    if (out->dead_known && (gv<uint32_t>(kEmListDead + no * 0x20 + (i >> 5) * 4) & (0x80000000u >> (i & 31))))
      out->dead[i >> 5] |= bit;
  }
  for (uintptr_t em = g_pool_base; em < g_pool_end; em += g_pool_stride) {
    if ((mem::read<uint32_t>(em + kEmFlags) & 0x201) != 1) continue;
    const uint8_t id = mem::read<uint8_t>(em + kEmId);
    if (id < kGanadoFirst || id > kGanadoLast) continue;
    const uint8_t entry = mem::read<uint8_t>(em + kEmEntry);
    if (entry != 0xFF) out->enemy[entry >> 5] |= 1u << (entry & 31);
  }
  return true;
}

int merc_hurry_bodies() {
  if (!merc_list_ready()) return 0;
  constexpr uint16_t kVillage = 0x400;
  constexpr int kSweepFrames = 0xC, kSweepAt = 300;
  constexpr int kCounter7 = 0x13 + 7 * 2;  // the control's counters (shorts) from +13
  constexpr int kTimer = 0x4B4, kTimer2 = 0x4B8;  // a death routine's timers (float units of 1/30 s)
  constexpr float kFallen = 73.0f - 15.0f;       // die 2's timer once half a second of the fall has played
  constexpr uint32_t kToDie0 = 0x01000003;        // +FC..+FF as die 2 sets them when the fall's motion ends
  int n = 0;
  bool gone = false;
  if (g_pool_stride < static_cast<uintptr_t>(kEm10Size)) return 0;
  for (uintptr_t em = g_pool_base; em < g_pool_end; em += g_pool_stride) {
    if (!live_em10(em)) continue;
    const uint8_t id = mem::read<uint8_t>(em + kEmId);
    if (id < kGanadoFirst || id > kGanadoLast) continue;
    if (mem::read<uint8_t>(em + kEm10Melted)) gone = true;
    const uint8_t r0 = mem::read<uint8_t>(em + kEmRno), r1 = mem::read<uint8_t>(em + kEmRno + 1),
                  r2 = mem::read<uint8_t>(em + kEmRno + 2);
    if (r0 != 3 || r2 != 1) continue;
    if (r1 == 2) {  // falling (die 2): on to die 0 as the fall's end would send it
      if (mem::read<float>(em + kTimer) <= kFallen) {
        *reinterpret_cast<uint32_t*>(em + kEmRno) = kToDie0;
        ++n;
      }
      continue;
    }
    if (r1 == 3) {  // melting (die 3): no waits before it sinks and fades
      bool hurried = false;
      for (const int t : {kTimer, kTimer2})
        if (mem::read<float>(em + t) > 0.0f) {
          *reinterpret_cast<float*>(em + t) = 0.0f;
          hurried = true;
        }
      n += hurried ? 1 : 0;
      continue;
    }
    if (r1 != 0) continue;  // waiting to melt (die 0)
    const uintptr_t control = mem::read<uintptr_t>(em + kEm10Group);
    if (!control || !mem::readable(control, 0x40) || mem::read<uint8_t>(control + 0xC) != 0x12 ||
        !mem::read<int16_t>(control + kCounter7))
      continue;
    *reinterpret_cast<int16_t*>(control + kCounter7) = 0;
    ++n;
  }
  if (gone && g_village_slot && gv<uint16_t>(kRoom) == kVillage) {
    const uintptr_t work = mem::read<uintptr_t>(g_village_slot);
    if (work && mem::readable(work, 0x10) && mem::read<uint32_t>(work + kSweepFrames) < kSweepAt) {
      *reinterpret_cast<uint32_t*>(work + kSweepFrames) = kSweepAt;
      ++n;
    }
  }
  constexpr uint16_t kCastle = 0x402;
  if (gone && g_castle_slot && gv<uint16_t>(kRoom) == kCastle) {
    const uintptr_t work = mem::read<uintptr_t>(g_castle_slot);
    if (work && mem::readable(work, g_castle_timer + 4) && mem::read<float>(work + g_castle_timer) < g_castle_every) {
      *reinterpret_cast<float*>(work + g_castle_timer) = g_castle_every;
      ++n;
    }
  }
  return n;
}

bool merc_village_lists(MercLists* out) {
  *out = MercLists{};
  constexpr uint16_t kVillage = 0x400, kBase = 0x403;
  if (!merc_game()) return false;
  if (g_base_known && gv<uint16_t>(kRoom) == kBase) {  // the base: its area lists and its one-time sends gone
    std::memcpy(out->area, g_base_area, sizeof(out->area));
    const uint32_t flags = gv<uint32_t>(kGlobalFlags174);
    for (const VillageWave& w : g_base_waves)
      if (flags & w.flag) out->waves[w.entry >> 5] |= 1u << (w.entry & 31);
    return true;
  }
  if (!g_village_lists || gv<uint16_t>(kRoom) != kVillage) return false;
  std::memcpy(out->area, g_village_area, sizeof(out->area));
  std::memcpy(out->events, g_village_events, sizeof(out->events));
  if (g_village_waves_known) {
    const uint32_t flags = gv<uint32_t>(kGlobalFlags174);
    for (const VillageWave& w : g_village_waves)
      if (flags & w.flag)
        for (const int e : {w.entry, w.entry + 1}) out->waves[e >> 5] |= 1u << (e & 31);
  }
  return true;
}

bool merc_swap_entry(int entry, int area_entry) {
  MercEntries en;
  MercLists lists;
  if (entry < 0 || entry >= kEmListLength || area_entry < 0 || area_entry >= kEmListLength || !merc_entries(&en) ||
      !en.dead_known || !merc_village_lists(&lists))
    return false;
  const int ew = entry >> 5, aw = area_entry >> 5;
  const uint32_t eb = 1u << (entry & 31), ab = 1u << (area_entry & 31);
  const bool waiting = ((lists.events[ew] | lists.waves[ew]) & en.ganado[ew] & eb) && !(en.sent[ew] & eb) &&
                       !(en.dead[ew] & eb) && !(en.enemy[ew] & eb);
  const bool done = (lists.area[aw] & en.ganado[aw] & ab) && (en.sent[aw] & ab) && (en.dead[aw] & ab) &&
                    !(en.enemy[aw] & ab);
  if (!waiting || !done) return false;
  uint8_t* const e = reinterpret_cast<uint8_t*>(pg() + kEmList + static_cast<uintptr_t>(entry) * kEmListEntry);
  uint8_t* const a = reinterpret_cast<uint8_t*>(pg() + kEmList + static_cast<uintptr_t>(area_entry) * kEmListEntry);
  uint8_t held[kEmListEntry];
  std::memcpy(held, e, kEmListEntry);
  std::memcpy(e, a, kEmListEntry);
  std::memcpy(a, held, kEmListEntry);
  a[0] = static_cast<uint8_t>(a[0] & ~kEntrySent);  // the waiting one's, not sent: the area's list sends it
  e[0] = static_cast<uint8_t>(e[0] | kEntrySent);   // the area's, sent and dead
  const int no = gv<int8_t>(kEmListNo);
  uint32_t* const dead = reinterpret_cast<uint32_t*>(pg() + kEmListDead + no * 0x20);
  dead[aw] &= ~(0x80000000u >> (area_entry & 31));
  dead[ew] |= 0x80000000u >> (entry & 31);
  return true;
}

bool merc_resend(int entry) {
  MercEntries en;
  if (entry < 0 || entry >= kEmListLength || !merc_entries(&en)) return false;
  const int w = entry >> 5;
  const uint32_t bit = 1u << (entry & 31);
  if (!(en.ganado[w] & en.sent[w] & bit) || (en.dead[w] & bit) || (en.enemy[w] & bit)) return false;
  uint8_t* const flags = reinterpret_cast<uint8_t*>(pg() + kEmList + static_cast<uintptr_t>(entry) * kEmListEntry);
  *flags = static_cast<uint8_t>(*flags & ~kEntrySent);
  return true;
}

namespace {

constexpr uint16_t kCastleRoom = 0x402;
constexpr int kWrapSize = 0xC, kWrapSet = 7;  // a cEmWrap: +0 its enemy, +7 1 while set (setPtr)

// The castle's work, in a game of the Mercenaries in the castle with its code known.
uintptr_t castle_work() {
  if (!g_castle_slot || !merc_game() || gv<uint16_t>(kRoom) != kCastleRoom) return 0;
  const uintptr_t work = mem::read<uintptr_t>(g_castle_slot);
  return work && mem::readable(work, g_castle_timer + 4) ? work : 0;
}

// A wrap's enemy, if one is in use (FUN_008d78d0's test), and whether it is alive.
uintptr_t wrap_enemy(uintptr_t work, int wrap, bool* alive) {
  const uintptr_t w = work + static_cast<uintptr_t>(wrap) * kWrapSize;
  const uintptr_t em = mem::read<uintptr_t>(w);
  *alive = false;
  if (mem::read<uint8_t>(w + kWrapSet) != 1 || !in_pool(em) || (mem::read<uint32_t>(em + kEmFlags) & 0x201) != 1)
    return 0;
  const bool em10 = g_pool_stride >= static_cast<uintptr_t>(kEm10Size) && mem::read<uintptr_t>(em) == g_em10_vtable;
  *alive = mem::read<int16_t>(em + kEmHp) > 0 && !(em10 && mem::read<uint8_t>(em + kEm10Scored));
  return em;
}

struct CastleSeen {
  uintptr_t work = 0;
  unsigned tick = 0;
  int counters[kCastleGroups] = {};
  uintptr_t em[kCastleWraps] = {};
  bool alive[kCastleWraps] = {};
};
CastleSeen g_castle_seen;

// The scene's area trigger `at` (FUN_006b4af0's walk of the list), 0 if none.
uintptr_t area_trigger(int at) {
  const uintptr_t scene = g_at_slot ? mem::read<uintptr_t>(g_at_slot) : 0;
  if (!scene || !mem::readable(scene + 0x8C, 4)) return 0;
  uintptr_t p = scene + 0x8C;
  for (int i = 0; i < 4096; ++i) {
    const uint32_t v = mem::read<uint32_t>(p);
    if (v == 0xFFFFFFFFu) return 0;
    const uintptr_t node = v & ~1u;
    if (!node || !mem::readable(node, 0x50)) return 0;
    if (!(v & 1) && mem::read<uint8_t>(node + 0x36) == at) return node;
    p = node;
  }
  return 0;
}

// An area trigger's fields (game.h, MercGate): its shape (the floor, the height, a
// radius, the four corners) and how it is set off.
constexpr int kAtShape = 0x8, kAtShapeFloats = 11, kAtCorners = 0x14, kAtOn = 0x34, kAtKind = 0x35, kAtNumber = 0x36;
constexpr int kAtSetOff = 0x38, kAtEvent = 0x40, kAtOnObject = 0x4C;
constexpr uint8_t kAtEventKind = 2, kAtWalkedIn = 1;
struct GateHeld {
  bool armed = false;
  float shape[kAtShapeFloats] = {};
  uint8_t set_off = 0;
};
GateHeld g_gate_held[2];

// Gate k's trigger, if it is the castle's: in the list, an event's, its event the castle's, on no object.
uintptr_t gate_trigger(int k) {
  if (!g_castle_gates_known || k < 0 || k > 1 || !castle_work()) return 0;
  const uintptr_t t = area_trigger(g_castle_gates[k].at);
  return t && mem::read<uint8_t>(t + kAtKind) == kAtEventKind && mem::read<uintptr_t>(t + kAtEvent) ==
         g_castle_gates[k].event && !mem::read<uintptr_t>(t + kAtOnObject) ? t : 0;
}

// The player's place, false if he cannot be read.
bool player_at(float at[3]) {
  const uintptr_t pl = g_player_slot ? mem::read<uintptr_t>(g_player_slot) : 0;
  if (!pl || !checked(pl, kPlayerSize, &g_player_ok)) return false;
  for (int i = 0; i < 3; ++i) at[i] = mem::read<float>(pl + kPlPos + 4 * i);
  return true;
}

// A trigger's shape widened round a place: its corners 30 times as far from their
// middle, the middle there, its floor 20000 below it and 40000 high. The corners
// keep their order, so the game's test (FUN_0058adb0: the floor and height, then
// the corners' cross products) has the place inside.
void widen_round(const float from[kAtShapeFloats], const float at[3], float to[kAtShapeFloats]) {
  constexpr int kCorner = (kAtCorners - kAtShape) / 4;  // the corners' first float in the shape
  constexpr float kWider = 30.f, kAbove = 20000.f;
  std::memcpy(to, from, kAtShapeFloats * sizeof(float));
  float cx = 0.f, cz = 0.f;
  for (int i = 0; i < 4; ++i) {
    cx += from[kCorner + 2 * i] / 4.f;
    cz += from[kCorner + 2 * i + 1] / 4.f;
  }
  for (int i = 0; i < 4; ++i) {
    to[kCorner + 2 * i] = (from[kCorner + 2 * i] - cx) * kWider + at[0];
    to[kCorner + 2 * i + 1] = (from[kCorner + 2 * i + 1] - cz) * kWider + at[2];
  }
  to[0] = at[1] - kAbove;
  to[1] = 2.f * kAbove;
}

// The water stage (room 404): its work, in a game of the Mercenaries there with
// its code known; and its spawn areas as they were before the cheat had them -
// the zones' (in the order they run) then the one-shot areas'.
constexpr uint16_t kWaterRoom = 0x404;
constexpr int kWaterWorkSize = 0x30;
uintptr_t water_work() {
  if (!g_water_known || !merc_game() || gv<uint16_t>(kRoom) != kWaterRoom) return 0;
  const uintptr_t work = mem::read<uintptr_t>(g_water_slot);
  return work && mem::readable(work, kWaterWorkSize) ? work : 0;
}
struct WaterKept {
  uintptr_t node = 0;
  int at = -1;
  uint8_t on = 0;
  float shape[kAtShapeFloats] = {};
};
WaterKept g_water_kept[kWaterZones + 8];

}  // namespace

int merc_castle_give_back(unsigned tick) {
  const uintptr_t work = castle_work();
  if (!work || !g_castle_group_n || !em_pool()) {
    g_castle_seen.work = 0;
    return -1;
  }
  CastleSeen now;
  now.work = work;
  now.tick = tick;
  bool fresh = g_castle_seen.work != work || g_castle_seen.tick + 1 != tick;
  for (int g = 0; g < g_castle_group_n; ++g) {
    const CastleGroup& grp = g_castle_groups[g];
    now.counters[g] = mem::read<int32_t>(work + grp.counter);
    if (now.counters[g] < 0 || now.counters[g] > grp.cap || now.counters[g] < g_castle_seen.counters[g]) fresh = true;
    for (int i = 0; i < grp.n; ++i) now.em[grp.wraps[i]] = wrap_enemy(work, grp.wraps[i], &now.alive[grp.wraps[i]]);
  }
  int given = 0;
  for (int g = 0; g < g_castle_group_n && !fresh; ++g) {
    const CastleGroup& grp = g_castle_groups[g];
    int took = 0;
    for (int i = 0; i < grp.n; ++i) {
      const int w = grp.wraps[i];
      if (now.em[w] && (now.em[w] != g_castle_seen.em[w] || (!g_castle_seen.alive[w] && now.alive[w]))) ++took;
    }
    const int tried = now.counters[g] - g_castle_seen.counters[g];
    if (tried > took) {
      now.counters[g] -= tried - took;
      *reinterpret_cast<int32_t*>(work + grp.counter) = now.counters[g];
      given += tried - took;
    }
  }
  g_castle_seen = now;
  return given;
}

int merc_castle_groups(MercCastleGroup* out, int max) {
  int n = 0;
  for (; n < g_castle_group_n && n < max; ++n) {
    const CastleGroup& g = g_castle_groups[n];
    out[n].counter = g.counter;
    out[n].cap = g.cap;
    out[n].n = g.n;
    std::memcpy(out[n].wraps, g.wraps, sizeof(out[n].wraps));
  }
  return n;
}

bool merc_castle_gates(MercGate out[2]) {
  out[0] = out[1] = MercGate{};
  if (!g_castle_gates_known || !castle_work()) return false;
  for (int k = 0; k < 2; ++k) {
    out[k].at = g_castle_gates[k].at;
    out[k].armed = g_gate_held[k].armed;
    const uintptr_t t = gate_trigger(k);
    out[k].found = t != 0;
    out[k].enabled = t && (mem::read<uint8_t>(t + kAtOn) & 1);
  }
  return true;
}

bool merc_castle_arm_gate(int k, bool arm) {
  if (k < 0 || k > 1) return false;
  GateHeld& held = g_gate_held[k];
  const uintptr_t t = gate_trigger(k);
  if (!arm) {
    if (!held.armed) return false;
    held.armed = false;
    if (!t) return false;
    std::memcpy(reinterpret_cast<void*>(t + kAtShape), held.shape, sizeof(held.shape));
    *reinterpret_cast<uint8_t*>(t + kAtSetOff) = held.set_off;
    return true;
  }
  float at[3];
  if (held.armed || !t || !(mem::read<uint8_t>(t + kAtOn) & 1) || !player_at(at)) return false;
  std::memcpy(held.shape, reinterpret_cast<const void*>(t + kAtShape), sizeof(held.shape));
  held.set_off = mem::read<uint8_t>(t + kAtSetOff);
  float shape[kAtShapeFloats];
  widen_round(held.shape, at, shape);
  std::memcpy(reinterpret_cast<void*>(t + kAtShape), shape, sizeof(shape));
  *reinterpret_cast<uint8_t*>(t + kAtSetOff) = kAtWalkedIn;
  held.armed = true;
  return true;
}

bool merc_water(MercWater* out) {
  *out = MercWater{};
  const uintptr_t work = water_work();
  if (!work || !em_pool()) return false;
  out->started = mem::read<uint32_t>(work + 4) != 0;  // the id it counts, set as its spawner first runs
  out->about = mem::read<int32_t>(work + 0);
  out->sends = mem::read<int32_t>(work + 8);
  const uintptr_t list = pg() + kEmList;
  auto sent = [&](int e) { return (mem::read<uint8_t>(list + static_cast<uintptr_t>(e) * kEmListEntry) & kEntrySent) != 0; };
  // Each entry's Ganado, the first in the pool (FUN_005b2250's look-up).
  uintptr_t ganado[kEmListLength] = {};
  for (uintptr_t em = g_pool_base; em < g_pool_end; em += g_pool_stride) {
    if ((mem::read<uint32_t>(em + kEmFlags) & 0x201) != 1) continue;
    const uint8_t e = mem::read<uint8_t>(em + kEmEntry);
    if (e != 0xFF && !ganado[e]) ganado[e] = em;
  }
  // A send goes for an entry not sent (setEm), or for one whose Ganado's body has
  // gone (cEmWrap: a Ganado's id but 18, then its ckResetEnable - cEm10's +7F5).
  auto ready = [&](int e) {
    if (!sent(e)) return true;
    const uintptr_t em = ganado[e];
    if (!em) return false;
    const uint8_t id = mem::read<uint8_t>(em + kEmId);
    const bool ganado_id = (id >= kGanadoFirst && id <= kGanadoLast) || (id >= 0x40 && id <= 0x4F && id != 0x4B && id != 0x4E);
    return ganado_id && id != 0x18 && g_pool_stride >= static_cast<uintptr_t>(kEm10Size) &&
           mem::read<uintptr_t>(em) == g_em10_vtable && mem::read<uint8_t>(em + kEm10Melted);
  };
  uint32_t mine[8] = {};
  for (int k = 0; k < g_water_zone_n; ++k) {
    const WaterZone& z = g_water_zones[g_water_zone_n - 1 - k];  // the last started runs first
    MercWaterZone& o = out->zone[k];
    o.at = z.at;
    o.sent = mem::read<int32_t>(work + z.counter);
    o.ends = z.ends;
    for (int i = 0; i < 5; ++i) {
      o.entry[i] = z.entry[i];
      o.ready[i] = ready(z.entry[i]);
      mine[z.entry[i] >> 5] |= 1u << (z.entry[i] & 31);
    }
    out->most += z.ends - 1 + 5;
  }
  out->zones = g_water_zone_n;
  out->lab_at = g_water_lab_at;
  for (int i = 0; i < kEmListLength; ++i) {
    const uint32_t bit = 1u << (i & 31);
    if (g_water_lab[i >> 5] & bit) out->lab_done = out->lab_done || sent(i);
    if (g_water_oneshots[i >> 5] & bit) {
      ++out->oneshots;
      out->oneshots_sent += sent(i);
    }
  }
  // The start group: this room's Ganados sent that nothing else here sends.
  const uint16_t room = gv<uint16_t>(kRoom);
  for (int i = 0; i < kEmListLength; ++i) {
    const uintptr_t e = list + static_cast<uintptr_t>(i) * kEmListEntry;
    const uint8_t id = mem::read<uint8_t>(e + 1);
    const uint32_t bit = 1u << (i & 31);
    if (id < kGanadoFirst || id > kGanadoLast || mem::read<uint16_t>(e + kEmListRoom) != room || !sent(i) ||
        ((mine[i >> 5] | g_water_oneshots[i >> 5] | g_water_lab[i >> 5]) & bit))
      continue;
    ++out->start;
  }
  out->most += out->start + out->oneshots + 1;
  // On a ladder (FUN_00682420's 0x40000), or the player not readable: no send goes.
  const uintptr_t pl = g_player_slot ? mem::read<uintptr_t>(g_player_slot) : 0;
  out->ladder = true;
  if (pl && checked(pl, kPlayerSize, &g_player_ok)) {
    const uint8_t r0 = mem::read<uint8_t>(pl + kPlRno), r1 = mem::read<uint8_t>(pl + kPlRno + 1);
    out->ladder = r0 == 0 && r1 <= 0x12 && (g_water_ladder & (1u << r1));
  }
  return true;
}

bool merc_water_set(uint32_t zones, bool oneshots, int about_at_least) {
  const uintptr_t work = water_work();
  if (!work) return false;
  float at[3];
  const bool placed = player_at(at);
  for (int i = 0; i < g_water_zone_n + g_water_oneshot_n; ++i) {
    const bool zone = i < g_water_zone_n;
    const int number = zone ? g_water_zones[g_water_zone_n - 1 - i].at : g_water_oneshot_at[i - g_water_zone_n];
    const bool up = placed && (zone ? (zones >> i) & 1 : oneshots);
    const uintptr_t t = area_trigger(number);
    // Only a plain area (kind 0) on no object: what the stage reads of it is its "inside" bit alone.
    if (!t || mem::read<uint8_t>(t + kAtKind) != 0 || mem::read<uintptr_t>(t + kAtOnObject)) continue;
    WaterKept& kept = g_water_kept[zone ? i : kWaterZones + (i - g_water_zone_n)];
    if (kept.node != t || kept.at != number) {
      kept.node = t;
      kept.at = number;
      kept.on = mem::read<uint8_t>(t + kAtOn);
      std::memcpy(kept.shape, reinterpret_cast<const void*>(t + kAtShape), sizeof(kept.shape));
    }
    float shape[kAtShapeFloats];
    if (up) widen_round(kept.shape, at, shape);
    else std::memcpy(shape, kept.shape, sizeof(shape));
    std::memcpy(reinterpret_cast<void*>(t + kAtShape), shape, sizeof(shape));
    *reinterpret_cast<uint8_t*>(t + kAtOn) = static_cast<uint8_t>(up ? kept.on | 1 : kept.on & ~1);
  }
  if (about_at_least >= 0 && mem::read<int32_t>(work + 0) < about_at_least)
    *reinterpret_cast<int32_t*>(work + 0) = about_at_least;
  return true;
}

void merc_water_release() {
  const bool here = water_work() != 0;
  for (WaterKept& kept : g_water_kept) {
    if (kept.node && here && area_trigger(kept.at) == kept.node) {
      std::memcpy(reinterpret_cast<void*>(kept.node + kAtShape), kept.shape, sizeof(kept.shape));
      *reinterpret_cast<uint8_t*>(kept.node + kAtOn) = kept.on;
    }
    kept = WaterKept{};
  }
}

// --- items --------------------------------------------------------------------------------------------

bool items_known() { return g_item_mgr != 0; }

int item_character() { return g_item_mgr ? mem::read<uint8_t>(g_item_mgr + kMgrChar) : -1; }

int case_size() {
  if (!g_subscreen) return -1;
  const int v = mem::read<int8_t>(g_subscreen + 0x2AA);
  return v >= 0 && v <= 3 ? v : -1;
}

int items(Item* out, int max) {
  int n = 0;
  const uintptr_t a = item_array(&n);
  if (!a) return 0;
  const int chr = mem::read<uint8_t>(g_item_mgr + kMgrChar);
  const uintptr_t weapon = mem::read<uintptr_t>(g_item_mgr + kMgrWeapon);
  int k = 0;
  for (int i = 0; i < n && k < max; ++i) {
    const uintptr_t it = a + static_cast<uintptr_t>(i) * kItemSize;
    if (!(mem::read<uint8_t>(it + 4) & 1) || mem::read<uint8_t>(it + 5) != chr) continue;
    Item& o = out[k++];
    o.index = i;
    o.id = mem::read<uint16_t>(it);
    o.num = mem::read<uint16_t>(it + 2);
    o.chr = chr;
    o.weapon = mem::read<uint16_t>(it + 6);
    o.ammo = mem::read<uint16_t>(it + 8) >> 3;
    o.onboard = mem::read<uint8_t>(it + 0xD) != 0;
    o.equipped = it == weapon;
  }
  return k;
}

namespace {
// The item at `index`, if it is still the one a list said (in use, the id, the
// character in play's).
uintptr_t item_at(int index, int id) {
  int n = 0;
  const uintptr_t a = item_array(&n);
  if (!a || index < 0 || index >= n) return 0;
  const uintptr_t it = a + static_cast<uintptr_t>(index) * kItemSize;
  if (!(mem::read<uint8_t>(it + 4) & 1) || mem::read<uint16_t>(it) != id ||
      mem::read<uint8_t>(it + 5) != mem::read<uint8_t>(g_item_mgr + kMgrChar))
    return 0;
  return it;
}

// Which entries are in use, to find the one a call added.
void in_use(std::vector<bool>* out) {
  int n = 0;
  const uintptr_t a = item_array(&n);
  out->assign(static_cast<size_t>(n), false);
  for (int i = 0; i < n; ++i) (*out)[i] = (mem::read<uint8_t>(a + static_cast<uintptr_t>(i) * kItemSize + 4) & 1) != 0;
}
int added_since(const std::vector<bool>& before, int id) {
  int n = 0;
  const uintptr_t a = item_array(&n);
  for (int i = 0; i < n; ++i) {
    const uintptr_t it = a + static_cast<uintptr_t>(i) * kItemSize;
    const bool was = i < static_cast<int>(before.size()) && before[i];
    if (!was && (mem::read<uint8_t>(it + 4) & 1) && mem::read<uint16_t>(it) == id) return i;
  }
  return -1;
}
}  // namespace

bool set_item_num(int index, int id, int num) {
  const uintptr_t it = item_at(index, id);
  if (!it) return false;
  *reinterpret_cast<uint16_t*>(it + 2) = static_cast<uint16_t>(num);
  return true;
}

bool set_item_weapon(int index, int id, int levels_word, int ammo) {
  const uintptr_t it = item_at(index, id);
  if (!it) return false;
  *reinterpret_cast<uint16_t*>(it + 6) = static_cast<uint16_t>(levels_word);
  const uint16_t low = mem::read<uint16_t>(it + 8) & 7;
  *reinterpret_cast<uint16_t*>(it + 8) = static_cast<uint16_t>((ammo << 3) | low);
  return true;
}

bool put_in_case(int id, int num, int* new_index) {
  if (new_index) *new_index = -1;
  const int size = case_size();
  if (!g_put_in_case || size < 0) return false;
  std::vector<bool> before;
  in_use(&before);
  using Fn = bool(__cdecl*)(unsigned, unsigned, unsigned);
  const bool ok = reinterpret_cast<Fn>(g_put_in_case)(static_cast<unsigned>(id), static_cast<unsigned>(num),
                                                      static_cast<unsigned>(size));
  if (ok && new_index) *new_index = added_since(before, id);
  return ok;
}

bool item_get(int id, int num, int* new_index) {
  if (new_index) *new_index = -1;
  if (!g_item_get || !g_item_mgr) return false;
  std::vector<bool> before;
  in_use(&before);
  using Fn = bool(__attribute__((thiscall))*)(void*, unsigned, unsigned);
  const bool ok = reinterpret_cast<Fn>(g_item_get)(reinterpret_cast<void*>(g_item_mgr), static_cast<unsigned>(id),
                                                   static_cast<unsigned>(num));
  if (ok && new_index) *new_index = added_since(before, id);
  return ok;
}

bool item_erase(int index, int id) {
  const uintptr_t it = item_at(index, id);
  if (!it || !g_item_erase) return false;
  using Fn = void(__attribute__((thiscall))*)(void*, void*);
  reinterpret_cast<Fn>(g_item_erase)(reinterpret_cast<void*>(g_item_mgr), reinterpret_cast<void*>(it));
  return !(mem::read<uint8_t>(it + 4) & 1);
}

// --- item rules ----------------------------------------------------------------------------------------

bool item_rules_known() { return g_kind_table && g_charge && g_levels; }
bool case_calls_known() { return g_put_in_case && g_item_get && g_item_erase; }

int item_kind(int id) {
  if (!g_kind_table || id < 0 || id >= 0x110) return -1;
  return mem::read<uint8_t>(g_kind_table + id);
}

namespace {
// The charge table's row for a weapon (11 shorts), or 0.
uintptr_t charge_row(int id) {
  if (!g_charge || !g_charge_rows_slot) return 0;
  const int rows = mem::read<int>(g_charge_rows_slot);
  for (int r = 0; r < rows && r < 128; ++r)
    if (mem::read<int16_t>(g_charge + r * 22) == id) return g_charge + r * 22;
  return 0;
}
}  // namespace

int charge(int id, int level) {
  const uintptr_t row = charge_row(id);
  if (!row || level < 1 || level > 7) return -1;
  const int v = mem::read<uint16_t>(row + 6 + level * 2);
  if (v == 0x8000) return 0x8000;
  // The game doubles a magazine on Amateur (difficulty 1 and below).
  if (pg() && gv<uint8_t>(kDifficulty) <= 1) return (v * 2) & 0xFFFF;
  return v;
}

int weapon_ammo_item(int id) {
  const uintptr_t row = charge_row(id);
  if (!row) return -1;
  const int v = mem::read<int16_t>(row + 6);
  return v > 0 && v < 0x110 ? v : -1;
}

int max_level(int id, int stat) {
  if (!g_levels || stat < 0 || stat > 3) return 0;
  for (uintptr_t r = g_levels; r < g_levels_end; r += 6)
    if (mem::read<int16_t>(r) == id) return mem::read<uint8_t>(r + 2 + stat);
  return 0;
}

// --- the save card -------------------------------------------------------------------------------------

bool card_known() { return g_card_slot && g_exists && g_storage_slot; }

bool card(Card* out) {
  *out = Card{};
  const uintptr_t c = card_ptr();
  if (!c) return false;
  out->up = true;
  out->routine = mem::read<uint8_t>(c + kCardRoutine);
  out->step = mem::read<uint8_t>(c + kCardStep);
  out->mode = mem::read<int32_t>(c + kCardMode);
  out->slot = mem::read<uint8_t>(c + kCardSlot);
  out->cursor = mem::read<int8_t>(c + kCardCursor);
  const uintptr_t b = mem::read<uintptr_t>(c + kCardBuffer);
  if (b && (b == g_buffer_ok || mem::readable(b, kSaveSize))) {
    g_buffer_ok = b;
    out->buffer = b;
  }
  return true;
}

uint32_t card_status(int file) {
  const uintptr_t c = card_ptr();
  if (!c || file < 0 || file >= kFiles) return 0;
  const int slot = mem::read<uint8_t>(c + kCardSlot);
  if (slot > 2) return 0;
  return mem::read<uint32_t>(c + kCardStatus + static_cast<uintptr_t>(slot * 100 + file) * 4);
}

bool set_card_status(int file, uint32_t v) {
  const uintptr_t c = card_ptr();
  if (!c || file < 0 || file >= kFiles) return false;
  const int slot = mem::read<uint8_t>(c + kCardSlot);
  if (slot > 2) return false;
  *reinterpret_cast<uint32_t*>(c + kCardStatus + static_cast<uintptr_t>(slot * 100 + file) * 4) = v;
  return true;
}

bool card_exists(int file) {
  return g_exists && file >= 0 && file < kFiles && mem::read<uint8_t>(g_exists + file) != 0;
}

bool set_card_exists(int file, bool on) {
  if (!g_exists || file < 0 || file >= kFiles) return false;
  *reinterpret_cast<uint8_t*>(g_exists + file) = on ? 1 : 0;
  return true;
}

void* remote_storage() {
  return g_storage_slot ? reinterpret_cast<void*>(mem::read<uintptr_t>(g_storage_slot)) : nullptr;
}

}  // namespace re4cc::game
