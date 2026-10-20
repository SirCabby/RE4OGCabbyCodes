#pragma once

#include <cstdint>

#include "mem.h"

// The game side: what the mod knows about bio4.exe at run time. Everything is
// found by the signatures in sigs.h, with each global read out of the matched
// instruction's operands and cross-checked between sites; nothing here is an
// absolute address (the exe has DYNAMIC_BASE, so on Windows it moves). The VAs
// in CLAUDE.md document build 10112090 only.
//
// Threads: bio4.exe runs its game on the main thread (the frame loop in
// WinMain) and draws on a thread of its own (the "GX" render thread, which also
// owns the window and pumps its messages). Everything below that reads or
// writes the game runs on the main thread, from the tick (dispatch.cpp); the
// panel, drawn on the render thread, sees only the snapshots the tick publishes.
namespace re4cc::game {

// --- discovery (mod thread) -----------------------------------------------------------
bool discover();            // every signature; ready() once the essentials are known
bool discover_in(HMODULE exe);  // the same over an image mapped by a test
bool ready();
const char* status_text();  // what is missing, for the panel and the log
uintptr_t tick_return();    // where the frame loop's SetThreadAffinityMask call returns (0 unknown)
uintptr_t tick_return_late();  // where the same frame's QueryPerformanceCounter call after it returns (0 unknown)
HMODULE exe();
void trace_tick();          // Watch values (main thread only)

// --- the run (main thread) -------------------------------------------------------------
// GLOBAL_WK (pG) keeps the game's top-level routine in its first four bytes at
// +20: 0 Init, 1 StageInit, 2 RoomInit, 3 MainLoop, 4 Doordemo, 5 Ending, 6
// Option - the pause, the in-game option screen. The title runs outside that
// routine, and a return to the title (a soft reset) zeroes GLOBAL_WK from +1C
// on, so a 0 there is "no game". Its flags at +54 (Flags_SYSTEM) say which game
// is being played.
enum class Mode { kNone, kMain, kSeparateWays, kAssignmentAda, kMercenaries };
struct Run {
  bool known = false;   // pG is set
  int routine = -1;     // Rno0
  int step = -1;        // Rno1
  bool in_game = false; // a game is being played (RoomInit .. Option)
  bool paused = false;  // the Option routine, with the option screen up
  bool option_screen = false;  // the option screen is up (in a game or on the title)
  Mode mode = Mode::kNone;
  int character = -1;   // pl_type: 0 Leon, 1 Ashley, 2 Ada, 3 HUNK, 4 Krauser, 5 Wesker
  int room = -1;        // curRoomId (stage << 8 | room)
  int chapter = -1;
  uint32_t sys = 0;     // Flags_SYSTEM: 0x2000 a new game, 0x100 a loaded one, ...
};
bool run(Run* out);
const char* mode_name(Mode m);
const char* character_name(int c);

// --- health (main thread) -----------------------------------------------------------------
// Leon's (the player's) health and Ashley's (the partner's) live in GLOBAL_WK,
// not in the character objects: +4FB4/+4FB6 current/max, +4FB8/+4FBA the
// partner's. Every hit goes through LifeDownSet2 (sigs.h kSigHurt*), which
// subtracts from these.
struct Health {
  int hp = 0, max = 0;          // the player
  int sub_hp = 0, sub_max = 0;  // the partner (0/0: none in this game)
};
bool health(Health* out);
bool set_hp(int hp);
bool set_sub_hp(int hp);

// --- counters (main thread) ---------------------------------------------------------------
// The play time is seconds at +4FA4 (the game stops at 99:59:59), brought up to
// date only now and then: the time since the base at +5C (GetTickCount()/1000
// when it was last brought up) goes onto it, and the base moves to now. The
// option screen brings it up to date as it opens and moves the base when it
// closes, so while the game is paused the total is exact and anything written
// to it counts on from there.
struct Counters {
  int play_time = 0;            // seconds, as the game would record it now
  int pesetas = 0;
  int deaths_chapter = 0, deaths_total = 0;   // c_/g_continue_cnt (+8460/+8462)
  int kills_chapter = 0, kills_total = 0;     // +8464/+8468
  int hits_chapter = 0, hits_total = 0;       // +846C/+8470
  int shots_chapter = 0, shots_total = 0;     // +8474/+8478
  int difficulty = -1;          // +847C: 1/3 Amateur/Easy (below 2 doubles ammo), 5 Normal, 6 Professional
  int da_level = -1;            // the dynamic difficulty (+4F98, 1..10)
  int saves = 0;                // save_cnt (+4F9C): the saves made in this game; each save writes it, plus one,
                                // into its file, and the load and save lists show each file's
};
constexpr int kPlayTimeMax = 359999;   // 99:59:59, the game's own cap
constexpr int kPesetasMax = 99999999;  // the merchant's display holds 8 digits
constexpr int kSavesMax = 999;         // the save builder's own cap (the lists show three digits)
bool counters(Counters* out);
bool set_play_time(int seconds);
bool set_pesetas(int v);
enum class Counter { kDeathsChapter, kDeathsTotal, kKillsChapter, kKillsTotal, kHitsChapter, kHitsTotal,
                     kShotsChapter, kShotsTotal };
bool set_counter(Counter c, int v);
// The save count, and the checkpoint's copy of it: a continue after a death
// puts the save region back as the last checkpoint had it, and the game's own
// count is always in that copy (a save takes a checkpoint), so an edit goes in
// both and a continue keeps it.
bool set_saves(int v);
bool set_difficulty(int v);

// --- the outfit (main thread) ----------------------------------------------------------------
// The costume set the title's Special Costume choice picks is one byte, the
// partner's costume at +4FCB: 0 Normal, 1 Special 1, 2 Special 2. Ashley wears it
// as it is (and in Special 2, her armour, nothing can hurt or carry her - the
// game checks the byte live). Leon's own costume (+4FC9) is worked out from it
// by PlSetCostume each time a room is set up: Normal is his jacket or the
// sweater the story has him in (or the vest, with the Tactical Vest item),
// Special 1 the R.P.D. uniform, Special 2 the gangster suit. So a change shows
// from the next room, and the game's own rule keeps it consistent. In the other
// games the set is not used (Ada's own costume is chosen at their title).
int costume_set();            // -1 unknown
bool set_costume_set(int v);  // 0..2

// --- the countdown (main thread) ----------------------------------------------------------------
// The HUD's countdown (the Mercenaries' clock and the main game's timed
// scenes) is CountDown at Cockpit+98: +0 state (bit 0 running, bit 3 held by
// the game), +8 the time left in frames of 1/30 s, a float. The option screen
// copies it to the Cockpit's +AC as it opens and copies that back as it closes,
// so an edit made while paused is made to both.
struct Countdown {
  bool known = false;
  bool running = false;
  bool editable = false;  // the option screen's copy was found
  float frames = 0;       // time left, 1/30 s
};
bool countdown(Countdown* out);
bool set_countdown(float frames);  // only while the option screen is up

// --- the cheats' patch sites (prepared at discovery, applied on the main thread) ----------------
// Steady aim: the aim's sway (FUN_00684330) drifts a held aim by random factors
// (kAimSwayV up-down, kAimSwayH left-right) and throws it a little at a shot
// (kAimKickV, kAimKickH), and each gun turns its shot's direction by random
// factors of its own (kSpread*: handguns, machine guns, a shotgun's centre
// pellet, the mounted gun) as a throw does its sideways speed (kSpreadThrow).
// Each factor becomes zero where it is used, and the random numbers are still
// drawn, so the game's other draws come out the same. Each weapon's aim starts
// its idle loop paused (kAimLoop*, see "Steady aim's stance" below). The hold
// sites (kHold*) are not put in with the switch: cheats.cpp puts them in while
// the aim is still (see "Steady aim's hold" below).
// Automatic QTEs: the action buttons' check answers a prompt by its kind
// through a jump table, and the QTE kinds' code raises the QTE flag. Kind 13's
// answer becomes "pressed" (kQteKind13) and the other QTE kinds' entries send
// them to its code (kQteKinds 2-4, kQteKindsLR 9-10); the scenes that poll their
// prompt's buttons themselves see a press every frame. The statue chase's run
// counts a press as the frames since the last one in whole 1/30 s, so at 60 fps a
// press every frame counts for nothing: there a press puts the run at its top
// speed (kQteStatueSpeed).
// God mode and Ashley takes no damage: LifeDownSet2 is where a hit's damage comes
// off (kHurtLeon, kHurtAshley), and some kills go round it, zeroing the HP
// themselves - the main loop starts the game over whenever either HP is below
// 1. An enemy attack flagged to kill outright (kKillFlag*), a rock or the truck
// on the player (kCrushRock, kCrushTruck) store 0 and then play the player's
// damage reaction, so without the store it is a knock-down. The partner's heavy
// hits (explosions) at close range throw her into a fall that zeroes her HP when
// it ends (kAshleyBlast: she ducks instead, as she does further off), and El
// Gigante's stomp, the truck and a rock kill her - each of them leaves her armour
// alone (Special 2), and with these sites she gets what the armour gets
// (kAshleyStomp, kAshleyTruck*, kAshleyRock; the truck's is a heavy hit, which
// kAshleyBlast turns into ducking, so the truck's sites lean on it).
// One hit kills: an enemy's damage comes off in LifeDownSet2 too (kHurtEnemy),
// but El Gigante's hit check does its own: a hit on its body takes no health
// (it adds to the stagger that drops it to its knees), one on its parasite
// twice the damage, the knife on its back 70 a slash. kHurtGigante takes all
// of it for any hit with damage but a flash's, as the game's own code does for
// a rocket, which kills it outright; kHurtGiganteKnife for a slash. Krauser's
// hit check takes his damage through LifeDownSet2, but with "survive at 1"
// until he is mutated: through the first part of his fight he cannot die, and
// what beats him there is his own count of the damage taken - a stage's share
// of it (1000, then 500) and the flinch a hit sets off end in his retreat.
// kHurtKrauser counts any hit with damage as at least 1000, so he flinches and
// retreats at once (and mutated dies as before); kHurtKrauserArm takes a hit
// on his armoured arm, which glances off with no damage at all, as a hit.
// Endless combo: the Mercenaries' HUD counts the combo's two timers down by the
// frame's delta each frame - the first kill's (kMercFirst) and, from the second
// kill on, the window each kill restarts (kMercWindow) - and either running out
// ends the combo. Without the subtraction neither does. What a combo earns goes
// onto the score only once the combo is over; kMercBonus lets it go on as it is
// earned, so an endless combo's bonus still counts. Bonus time always on: the
// HUD counts a Bonus Points pickup's time down the same way (kMercBonusTime), and
// what the combo has earned waits for that time to be over as well
// (kMercBonusBank lets it go on while it runs).
enum class Site {
  kHurtLeon, kHurtAshley, kHurtEnemy, kHurtGigante, kHurtGiganteKnife, kHurtKrauser, kHurtKrauserArm,
  kKillFlag1, kKillFlag2, kCrushRock, kCrushTruck,
  kAshleyBlast, kAshleyStomp, kAshleyTruck1, kAshleyTruck2, kAshleyRock, kAmmo, kArrows, kBolts, kPlayTime, kCountdown,
  kSaveCount, kBuy, kTuneUp,
  kMissA, kMissB, kDeathsChapter, kDeathsTotal, kAimSwayV, kAimSwayH, kAimKickV, kAimKickH,
  kSpreadGun1, kSpreadGun2, kSpreadGun3, kSpreadGun4, kSpreadAuto1, kSpreadAuto2, kSpreadAuto3, kSpreadAuto4,
  kSpreadShotgun1, kSpreadShotgun2, kSpreadMounted1, kSpreadMounted2, kSpreadThrow,
  kAimLoopHandgun, kAimLoopWesker, kAimLoopBow, kAimLoopLaser, kAimLoopBowgun, kAimLoopGrenade, kAimLoopRed9,
  kAimLoopMine, kAimLoopRocket, kAimLoopShotgun, kAimLoopAuto,
  kHoldLaser, kHoldHand1, kHoldHand2, kHoldAuto1, kHoldAuto2, kHoldShotgun,
  kQteKind13, kQteKinds, kQteKindsLR, kQteBoulder, kQteSwim, kQteRope, kQteGigante1, kQteGigante2, kQteGigante3,
  kQteMinecart, kQteStatueRun, kQteStatueSpeed, kQteStatueJump, kQteStatueEnd, kQteStatueClimb1, kQteStatueClimb2,
  kQteKrauser, kQteLasers1, kQteLasers2, kQteLasersLast, kQteBridge1, kQteBridge2, kQteBridgeClimb1, kQteBridgeClimb2,
  kQteRobotGrab, kQteShake, kMercFirst, kMercWindow, kMercBonus, kMercBonusTime, kMercBonusBank, kMercAreas,
  kMercEvent1, kMercEvent2, kMercEvent3, kCastleDoor,
  kSaveTurnedAway,  // a fix of the game's own, no switch: see "a save is asked for", below
  kCount
};
mem::Patch* patch(Site s);  // null when the site was not found
const char* site_name(Site s);
// The site another one only works with (kCount: none). It goes in first and
// comes out last: kQteKinds and kQteKindsLR without kQteKind13 would send those
// prompts to an answer that is never "pressed"; kAshleyTruck* without
// kAshleyBlast would trade the truck's kill for a heavy hit that kills her too.
Site needs(Site s);

// --- Steady aim's hold (main thread) ----------------------------------------------------------------
// The laser and every shot take their line from the animated gun and hand: the
// laser from the weapon's own part 0 (part 1 for the mine thrower and the
// P.R.L. 412), a shot from the player's part 10, each part's world matrix at +C.
// So the idle aim's breathing moves the dot, and a shot's recoil throws it. The
// hold sites turn those lookups into the address of a held copy (HeldPart) for
// as long as cheats.cpp keeps them in. AimPose is what it decides by.
//
// Where the player is in its weapon's routine: r_no_0 (+FC) 0 and r_no_1 (+FD) 6,
// the weapon's routine set (FUN_00763ff0 calls the one the weapon put at
// 0xC7A7CC), whose step r_no_2 (+FE) is 1 for the aim - every laser weapon's
// draws the laser there each frame (FUN_0067de90) - and 2 for a shot (the fire
// routines that read the hand are step 2's). The weapon object's own flags say
// nothing the tick can use: the aim sets its "laser drawn" bit (cObjWep+354
// bit 1) in the player's move, and the weapon's move (FUN_0073f320, run later
// in the frame by the object manager) clears it again.
enum class AimStep : uint8_t { kNone, kAim, kFire };
struct AimPose {
  bool ok = false;          // a player with a weapon object
  uintptr_t weapon = 0;     // the weapon object (cObjWep), the player's cPlWep+34
  int weapon_no = -1;       // pG+4FC0
  AimStep step = AimStep::kNone;
  float centre[2] = {};     // the aim's centre (cPlWep+28 up-down, +2C left-right): moves only when the aim is moved
  float aim_lr = 0;         // the player's +448, the aim's left-right off the body
  float yaw = 0;            // the player's +A4
  float pos[3] = {};        // the player's +94
  float gun[12] = {};       // the laser's part's world matrix (3x4, rows)
  float hand[12] = {};      // the hand's (the player's part 10)
};
bool aim_hold_known();          // the player's slot and every hold site were found
bool aim_pose(AimPose* out);    // false: no player, no weapon, or a part not readable
// The copies the hold sites point the game at.
void set_held_pose(const float gun[12], const float hand[12]);

// --- Steady aim's stance (main thread) ----------------------------------------------------------------
// Leon's idle aim is an animation of its own: each weapon's aim starts a
// looping motion (FUN_0067e190, the aim's blend helper) on the player, blended
// with an aim-up or aim-down motion (the helper's second motion, B) by how far
// up or down the aim is. The loop breathes, and the gun and the hand with it.
// The kAimLoop* sites start that loop with the game's own "motion stop" in its
// attribute (bit 8: its clock does not run), so it holds its first frame. B runs
// on a clock of its own; keep_stance() puts it back on the loop's frame each
// tick while the loop is held, and let_stance_go() lets a held loop run again
// when the switch goes off.
bool aim_stance_known();  // the blend helper and the player's slot were found
void keep_stance();
void let_stance_go();

// --- the shooting gallery (main thread) -------------------------------------------------------------------
// The merchant's shooting gallery is a room of its own (r22c.cpp, room 22C). Its
// work (static pointer) runs one game at a time: +0 the state (1 starting, 2
// running, 3 the results), +1C the game (1-4, A-D), +20 its script (2 once three
// of that game's five bottle caps are held), +10 the score, +C the hits, +24 the
// streak, +40 its flags, +68 the props' cool-down. The targets are enemies of
// their own (cEmMark, id 3E) on the enemy manager's list, set up by the script
// with a type (+101) and one point of health (+324): 0 and 1 Ganados (a head
// shot 100 / 200 points, the body 50), 2 and 4 Ashley (-1000), 3 the bonus
// target five hits in a row bring on (500), 5 a Ganado carrying a bomb (a head
// shot 100; the bomb, +4A4, blows up whatever is round it, Ashley too). Types 12
// to 15 are the room's props (1000 health, scoring nothing; a hit on one ends the
// streak and may start one of the room's effects). A hit is a pending damage
// record on the target (+328, as a shot's FUN_005aeef0 writes it: +32C pending,
// +32D the frames it stays up, +32E the weapon, +330 where from, +33C how far,
// +340 the part hit), which the target's own move scores (FUN_00589c50): the
// head is its part at +344. The streak: each hit that scores adds one, a miss
// (a shot that hits nothing) or a prop ends it, and at five the gallery starts
// it again and - unless one is already under way (pG+174 bit 40000000, from the
// task's start until 15 frames after the bonus target is gone) - brings the
// bonus target on 30 frames later; a fifth hit while one is under way is lost.
constexpr int kGalleryRoom = 0x22C;
constexpr int kGalleryHead = 0x344;                  // a target's head (every type has one)
constexpr int kGalleryPropChest = 0x408, kGalleryPropLegs = 0x470;  // the props' parts the +300 takes
constexpr int kMaxGalleryTargets = 48;
struct GalleryTarget {
  uintptr_t em = 0;
  int type = -1;
  int hp = 0;
  int routine = -1;     // +FC, the script's command: 0 rising, 1 going down, 2 waiting, 3 moving
  bool pending = false; // a hit not yet taken off its record (+32C)
  float age = 0;        // +4FC: frames it has stood up (waiting or moving)
  int life = -1;        // frames until it goes down, from its commands (-1: it stays, or not known)
  float rise = 0;       // how far up it has turned while rising (0..1; 1 once risen)
  float x = 0;          // +94 (the props slide along it, to +-5000, while they are out)
};
struct Gallery {
  int state = -1;
  int game = 0, script = 0;
  int score = 0, hits = 0, streak = 0;
  uint32_t flags = 0;    // +40: bit 0 the lights' prop hit, bit 1 the 300 points given
  float cooldown = 0;    // +68: the props' effects wait for it
  bool bonus_phase = false;
  int n = 0;
  GalleryTarget t[kMaxGalleryTargets];
};
bool gallery_known();
// false outside the gallery's room (or with the game neither in its main loop
// nor paused there).
bool gallery(Gallery* out);
// A hit on a target's part (kGalleryHead, or a prop's), as a shot of the weapon in
// hand would make it from where the player stands; the target's move scores it
// this frame. False: the target is gone, a hit is pending on it already, or the
// game is not in its main loop.
bool gallery_hit(uintptr_t em, int part);

// --- the Mercenaries (main thread) ----------------------------------------------------------------------------
// The Mercenaries keep their game in a static work (mercenaries.cpp): +24 the
// score, +28 the combo (kills in a row), +2C the combo's window and +30 the first
// kill's timer (frames left), +3C the kills, +48 what the combo has earned - its
// bonus, shown and put onto the score once the combo is over - +4C a bonus on its
// way onto the score, +64 the HUD's flags (100000: that bonus is being shown).
// A kill scores in one place (FUN_0070f520(kind, 0)): the kind's points (300 a
// Ganado; the chainsaws and the other big ones 5000, 7000, 10000) go onto the
// score at once, and from a combo's second kill on the bonus its length gives
// (20, 50, 100 ... 700 more for a Ganado, up to the tenth) onto what it has
// earned. The first kill starts a window of 300 frames (10 s); each later one
// starts it again; out of it the combo is over.
// A Bonus Points pickup (item 117, FUN_0070f6c0) starts the Bonus Points time:
// unless one runs already (+54 within a frame of 0) the HUD's flag 400000 shows
// it and the kills in it (+40) start again from 0; +64 bit 200000 (its end being
// shown) goes; +54 += 30 s (900 frames). While it runs, every kill's combo bonus
// is at least 1000 (the score's constant after the window's), and the HUD counts
// +54 down by the frame's delta - blinking from 120 frames left, its end shown
// from 1 (bit 200000), then 0.
struct Merc {
  int score = 0;
  int combo = 0;
  int bonus = 0;  // what the combo has earned, not on the score yet
  int kills = 0;
  bool bonus_time = false;  // a Bonus Points time runs (and is not ending)
  int bonus_kills = 0;      // the kills in it (+40)
};
bool merc_known();       // the work was found
bool merc(Merc* out);    // false outside the Mercenaries
// Starts the Bonus Points time as a pickup does (900 frames) unless one runs that
// is not ending, and puts it back at 900 frames if one runs with less: field
// writes. True when one runs afterwards; false outside a game of the Mercenaries.
bool merc_bonus_time();
// Every enemy the Mercenaries score is a Ganado of one class (cEm10: villagers,
// zealots, soldiers, the chainsaws, the Garradors, J.J.), and its kill is scored
// once (+814 set) as it starts dying - always from its hit check (em10DmCk) or a
// death routine it sends it to: a hit's damage comes off its health
// (LifeDownSet2), and below 1 the death the hit's weapon and part call for.
// Routines (+FC): 0 its set-up, 1 about its business, 2 a hit's reaction (a
// flinch, a knock-down, a fall), 3 dying.
struct MercEnemy {
  uintptr_t em = 0;
  int id = 0;           // +100: 06..20, the Ganados' ids
  int type = 0;         // +101
  int routine = -1;     // +FC
  int hp = 0;
  bool pending = false; // a hit waiting on its record (+32C)
  bool scored = false;  // +814: its kill has scored
  bool alive = false;   // +3CC bit 5: the stages count it as about (and send more only while fewer than ten are)
};
constexpr int kMaxMercEnemies = 64;
bool merc_kills_known();  // the class, its hit check and the enemy manager's list were found
int merc_enemies(MercEnemy* out, int max);  // the Ganados alive (0 outside the Mercenaries' main loop)
// A killing hit on a Ganado about its business or in a hit's reaction (routine 1,
// or 2 while it is about; not scored, no hit waiting): its health to 1, and a
// handgun's hit on its body (its part 2) from where the player stands, as a shot
// writes it. Its move takes the hit this frame - the hit check runs in every
// routine but the set-up - the damage takes the last point, the hit check starts
// the death its weapon and part call for, and the kill scores as a shot's does
// (the combo, the kills, the points).
bool merc_kill(uintptr_t em);
// The stages send their enemies area by area. Each frame the scene's area
// triggers clear the room's area flags (pG+17C) and set those of the areas the
// player stands in; a stage's spawner then sends enemies from the lists of the
// areas whose flag is up (the village: seven lists, 102 enemies, at most 10
// about at once), so standing in one place only those areas' enemies ever come.
// kMercAreas takes out the clear, and merc_all_areas() puts every area's flag up:
// every list sends its enemies wherever the player stands. In the castle (room
// 402, its code known) two at a time instead, in turn, sixteen ticks round (`tick`
// picks them): its groups each send all their free wraps at once when their area
// is up, so every area up at once is dozens of sends in a frame, more than its
// enemy pool holds (merc_castle_give_back). False outside the Mercenaries.
bool merc_all_areas(unsigned tick);
// The stage's enemies, for the log (Kill enemies as they appear's pace). A stage
// sends a Ganado only while it counts fewer than ten about (+3CC bit 5) and the
// enemy pool has a free slot; one that dies stops counting within a few seconds
// (its death's own timer), and its slot comes free once its body has gone (one
// body at a time starts going, through the Ganados' one control) and the stage's
// sweep, every 300 frames, lets it go.
struct MercPool {
  int slots = 0;   // the pool's
  int used = 0;    // in use
  int going = 0;   // being let go
  int alive = 0;   // Ganados the stage counts as about
  int bodies = 0;  // Ganados in use that it does not: dying, or their bodies going or gone
  int melted = 0;  // of those, gone - waiting for the sweep
  int listed = 0;  // the Ganados the enemy list in play has for this room
  int unsent = 0;  // of those, not sent yet
};
bool merc_pool(MercPool* out);  // false outside a game of the Mercenaries
// Logs merc_pool() and every Ganado in the pool - its slot, list entry, type,
// set, routine bytes, status, health, hit record, whether its kill scored, its
// body gone, on the enemy manager's list, its body part, how far from the
// player - with the area and room flags and the Ganados' control's counters.
void merc_census(const char* why);
// The models' memory. Every enemy is a model: its set-up (modelInit,
// FUN_0065eba0) takes a record from the model pool (kSigModelInfos) and its parts
// (bones: the model's +102) from the parts pool (kSigModelParts) - each a
// cManager sized at the room's set-up - and a body keeps both until the stage's
// sweep lets it go. A Ganado whose set-up finds either pool short destroys
// itself at once (FUN_00461580: +FC 0xFF) - after setEm has marked its list
// entry sent (FUN_005B27B0), so the stage never sends that entry again. With
// every enemy killed as it appears the bodies pile up faster than the sweep lets
// them go, and when the ten about fall at once the stages try ten new ones a
// frame: a few frames of a full pool lose dozens of entries.
struct MercMemory {
  int models = 0, models_used = 0;  // the model pool
  int parts = 0, parts_used = 0;    // the parts pool
  int ems = 0, ems_used = 0;        // the enemy pool (a Ganado's weapon in its hands is an enemy of its own, 52)
  int ganado_parts = 0;             // the most parts a Ganado in the enemy pool has
  int alive = 0;                    // Ganados the stage counts as about
  int dying = 0;                    // of those, killed (scored or a hit waiting): each one's place is filled again
  int bodies = 0;                   // Ganados in the enemy pool not about
};
bool merc_memory_known();  // both pools were found
bool merc_memory(MercMemory* out);  // false outside a game of the Mercenaries or with the pools unreadable
// The enemy list in play (GLOBAL_WK +5410, 256 entries) for this room's Ganados,
// a bit an entry (entry i: word i / 32, bit 1 << (i % 32)).
struct MercEntries {
  uint32_t ganado[8] = {};  // this room's Ganados (ids 10-20)
  uint32_t sent[8] = {};    // sent (+0 bit 2: setEm took the entry)
  uint32_t dead[8] = {};    // marked dead in the room's mask (GLOBAL_WK +502C + list x 0x20; FUN_005B2010)
  uint32_t enemy[8] = {};   // an enemy of the entry is in the enemy pool (+3A0)
  bool dead_known = false;  // the list in play has a dead mask (GLOBAL_WK +4FB3 not -1)
};
bool merc_entries(MercEntries* out);  // false outside a game of the Mercenaries
// An entry of this room's Ganados that is sent with no enemy of it in the pool
// and not marked dead: its "sent" bit cleared, so the stage sends it again.
bool merc_resend(int entry);
// The Mercenaries' village (room 400) has 124 Ganados: 5 placed as it starts
// (their entries' +0 bit 1), the rest sent from three places - its seven area
// lists (102, from the start with every area up), the chainsaws' waves (6, at
// 10, 25 and 35 deaths) and three pickups' events (11): each event
// sends its Ganados once its item is picked up - three Bonus Time hourglasses
// (item 115) on the stage's item sets 80-82 (FUN_006b9170: the item's "got" flag)
// - and never otherwise. A wave sends its two whatever is about, once (its flag
// in GLOBAL_WK +174 put up first): one setEm refuses - no free slot in the enemy
// pool - is never sent. The entries each list and each event sends, from their
// code (each setEm's entry, pushed last), and those of the waves gone (their
// flags up); false outside the village.
struct MercLists {
  uint32_t area[8] = {};
  uint32_t events[8] = {};
  uint32_t waves[8] = {};  // the waves' that have gone: no wave sends them again
};
bool merc_village_lists(MercLists* out);
// A Ganado an event or a wave gone would send, not sent (not dead, no enemy),
// put in the place of an area list's entry that is dead with its enemy gone:
// their 0x20 bytes swapped, the area's entry no longer dead (its list sends it
// again - the other's Ganado: its type, health and place), the other's marked
// dead (its event, if its hourglass is picked up later, sends nothing). Field
// writes; needs the room's dead mask.
bool merc_swap_entry(int entry, int area_entry);
// The bodies hurried along (field writes), so their models and slots go in about
// a second instead of five: a Ganado falling (die 2, routine 3, 2, sub-step 1:
// its timer from 73 units, the fall's motion playing) goes on to die 0 once half a
// second of it has played, as the motion's end would send it (+FC..+FF 03 00 00
// 01; die 0 counts the kill for the results, drops its item and stops it counting
// as about); one waiting to melt (die 0's sub-step 1) waits for the Ganados' one
// control's eighth counter, which the body that goes sets to 15..24 frames - it is
// put back at 0, so one goes each frame; one melting (die 3's sub-step 1) has its
// two waits (15 and 53 units) put at 0, so it sinks and fades at once (10 units);
// and in the village (room 400), once a body has gone (+7F5), the frames since its
// sweep are put at 300, so it sweeps the next frame instead of up to 300 frames on
// (a gone body is out of sight already: the sweep only lets its slot and its model
// go); in the castle (room 402) its sweep's timer is put at its 300 likewise. How
// many were hurried.
int merc_hurry_bodies();
// The Mercenaries' castle (room 402) sends its enemies through 114 wraps (cEmWrap,
// 0xC bytes from its work's +0: the enemy, +7 set): its spawner (a task) runs
// each frame and, area by area (pG+17C), sends an entry of the room's list into
// its own wrap whenever that wrap is free (FUN_008d78d0: no enemy of it in use) -
// EmSetEvent (FUN_005b2a60), which takes the list entry whatever it holds, so an
// entry comes again and again. Fourteen groups count their sends (a counter in the
// work each, from +568, against a cap: 288 sends from 46 wraps), one-shot steps
// send the rest - the set-up's 8, the bosses, the two gates' events: 348 in all.
// A counted send the pool refuses (no free slot) still counts. Each frame's tick
// compares every group's counter with the wraps that took a new enemy since the
// last tick - one in use where none was, another, or the same slot alive again
// (the sweep let the last go and the pool gave the new one its slot) - and gives
// the counter back what did not take (field writes). How many sends were given
// back; -1 outside the castle or with its code unknown. The first call of a game,
// or after a tick not called, only notes where things stand.
int merc_castle_give_back(unsigned tick);
// The castle's counted groups as read from its code (its counter's place in the
// work, its cap, its wraps); how many, 0 with its code unknown.
struct MercCastleGroup {
  int counter = 0, cap = 0, n = 0;
  uint8_t wraps[8] = {};
};
int merc_castle_groups(MercCastleGroup* out, int max);
// The castle's two gates: area triggers its set-up gives events (SceAtDataSet_exec):
// 10, the action button by a gate (FUN_007d49d0: once - pG+174 40000000, which
// also turns one of the groups to four entries of its own, 25 more - its 7
// Ganados if fewer than 5 are about, the gate slid open), and 0F, walked into
// (FUN_007d7040: once - pG+174 80000000 - 5 Ganados and a short scene of the gate
// opening). Each event turns its trigger off. A trigger (the scene's list,
// kSigAtFind): +8 its floor, +C its height, +14..+33 its four corners (x, z), +34
// bit 0 on, +35 its kind (2: an event), +36 its number, +38 how it is set off (1
// walked into, 8 the action button), +40 the event.
struct MercGate {
  int at = -1;           // its area trigger's number
  bool found = false;    // in the scene's list, with the castle's event
  bool enabled = false;  // still on: its event has not been
  bool armed = false;    // widened round the player (merc_castle_arm_gate)
};
bool merc_castle_gates(MercGate out[2]);  // false outside the castle or with its gates unknown
// Gate k's trigger widened round the player - its corners 30 times as far from
// their middle, the middle where he stands, its floor and height round him - and
// set off by walking in (field writes): the game's own area pass runs its event
// this frame as if he had gone there. arm false puts it back as it was.
bool merc_castle_arm_gate(int k, bool arm);
// The Mercenaries' water stage (room 404) sends by the scene's area triggers' own
// "inside" bits (FUN_006b4da0: [the triggers' work]+10, a bit a trigger, cleared
// each frame by the scene's area pass and set for each trigger the player stands
// in), every one of them a plain area (kind 0): its set-up sends a start group of
// 10 (one of three, at random with the start point), then its spawner, a task,
// starts eight tasks of its own and each frame counts the Ganados about (work +0,
// FUN_006c1010) and, with few enough about, runs a one-shot area's event once
// (the room's record has it) when the player is in it: 0F 4, 10 3, 11 (or 15 with
// its door broken) 2, 12 3, 13 2, 14 3. Seven of its tasks are zones: while the
// player is in its area, each frame a zone sends its five entries in order, each
// while fewer than 10 are about - not sent yet, setEm; sent, its Ganado reset
// once its body is gone (+7F5; cEmWrap's ckResetEnable and setReset, the same
// slot) - counting each send (a counter of its own in the work), and ends after a
// frame's sends have its counter at 15 (zone 17's at 10). So a zone sends 15, but
// one with a single send left that sends its five at once ends at 19 (zone 17:
// 14) - the tool-assisted runs' "spawn glitch", the player leaving the zone and
// coming back. The eighth sends once, 30 sends after the start (one of two by
// where the player stands). 10 + 6 x 19 + 14 + 17 + 1 = 156 at most. A send is
// skipped (an event's lost) while the player is on a ladder (FUN_00682420
// 0x40000). The tasks run in the reverse of the order they were started - the
// eighth, the zones from the last, the spawner last - after the scene's area
// pass and before the enemies and the player move.
constexpr int kWaterZones = 8;
struct MercWaterZone {
  int at = -1;              // its area trigger
  int sent = 0;             // its counter
  int ends = 0;             // its task ends once a frame's sends have the counter here
  uint8_t entry[5] = {};    // its five, in the order it sends them
  bool ready[5] = {};       // each one's send would go now: not sent yet, or its Ganado's body gone
};
struct MercWater {
  bool started = false;     // its eight tasks run (the spawner has run once before this tick)
  int about = 0;            // its count of Ganados about as its tasks will see it this frame (work +0)
  int sends = 0;            // work +8: its sends and those about as it began
  int zones = 0;
  MercWaterZone zone[kWaterZones];  // in the order the game runs them each frame
  int lab_at = 0;           // the eighth task's: it sends once work +8 - work +0 reaches this
  bool lab_done = false;    // it has sent (either of its entries sent)
  int oneshots = 0, oneshots_sent = 0;  // the one-shot areas' Ganados, and those sent
  int start = 0;            // the start group's (sent, sent by nothing else)
  int most = 0;             // what the stage sends at most this game: the start's, 19 a zone (14 one ending at 10), the one-shots', 1
  bool ladder = false;      // the player on a ladder: every send skipped this frame
};
bool merc_water(MercWater* out);  // false outside the water stage or with its code unknown
// The water stage's spawn areas this frame (field writes): the zones' whose bits
// are set (merc_water's order) and, with `oneshots`, the one-shot areas, each on
// and widened round the player (as a gate, merc_castle_arm_gate) - the game's own
// area pass then has him in it; every other one off, so his own place sends
// nothing. The first write keeps each trigger as it was. With `about_at_least` at
// or above 0 the stage's count of Ganados about is put at least that high for the
// frame (its spawner counts them again after the zones have sent). False outside
// the water stage.
bool merc_water_set(uint32_t zones, bool oneshots, int about_at_least);
// Every spawn area of the water stage put back as it was, if it is still the
// scene's. Nothing outside the water stage (its triggers are gone with it).
void merc_water_release();

// --- items (main thread) ---------------------------------------------------------------------------
// cItemMgr (static) holds every item of every character in one array of cItem:
//   +0 id u16, +2 num u16, +4 be_flag (bit 0 in use), +5 chr (whose), +6 weapon
//   word (upgrade levels - 1, four nibbles: firepower, firing speed, reload,
//   capacity), +8 loaded rounds << 3, +A pos x, +B pos y, +C orientation, +D
//   on the case's board. The manager: +C the equipped weapon (cItem*), +13 the
//   character whose items are in play, +14 the array, +1C its length.
struct Item {
  int index = -1;  // in the array
  int id = 0;
  int num = 0;
  int chr = 0;
  int weapon = 0;  // the levels word
  int ammo = 0;    // loaded rounds
  bool onboard = false;
  bool equipped = false;
};
constexpr int kMaxItems = 256;
bool items_known();
int items(Item* out, int max);  // the items of the character in play
int item_character();           // m_char, -1 unknown
int case_size();                // SubScreenWk+2AA: 0 S, 1 M, 2 L, 3 XL; -1 unknown
// Field writes on an item the list above returned (checked: the same id and
// owner still there).
bool set_item_num(int index, int id, int num);
bool set_item_weapon(int index, int id, int levels_word, int ammo);

// The approved game calls (re4og-game-calls-approved): main thread only, while
// the game is paused.
bool put_in_case(int id, int num, int* new_index);  // the case's own placement; false: no room
bool item_get(int id, int num, int* new_index);     // a key item / treasure (no grid)
bool item_erase(int index, int id);                 // takes the item out (unequips a weapon)

// The game's item rules, read out of its tables (items.h holds the rest).
bool item_rules_known();
bool case_calls_known();  // PutInCase, cItemMgr::get and cItemMgr::erase were all found
int item_kind(int id);            // itemInfo's switch case for the id, -1 unknown
int charge(int id, int level);    // rounds a magazine holds at a capacity level (1-based); 0x8000 infinite; -1 none
int max_level(int id, int stat);  // stat 0 firepower, 1 firing speed, 2 reload, 3 capacity; 0 unknown
int weapon_ammo_item(int id);     // the ammo item a weapon loads, -1 none

// --- the save card (main thread) --------------------------------------------------------------------
// The load and save screens are the GameCube's memory card screens, kept: a
// cCard (made while the screen is up, then freed) runs a routine at +4 (0 slot
// select .. 2 data select - the list of files - 3 load/save ...), its sub-step
// at +5, +14 the mode (0 load, 1 save), +1C the card slot, +1D the file the
// cursor is on, +44 + (slot*100 + file)*4 each file's status (bit 0 there, bit
// 1 broken), +4E8 the save buffer: savegame00.sav as the game holds it, 0xFFAA0
// bytes - +1E74 a dword per file (in use), and from +1EE8 the 20 files of 0xCAFC
// bytes each. The per-file "exists" bytes the list is drawn from are a table of
// their own.
constexpr int kFiles = 20;
constexpr int kFileSize = 0xCAFC;
constexpr int kSaveSize = 0xFFAA0;
struct Card {
  bool up = false;        // a card screen is up
  int routine = -1, step = -1;
  int mode = -1;          // 0 load, 1 save, 2 start
  int cursor = -1;
  int slot = 0;
  uintptr_t buffer = 0;   // the save buffer
};
bool card(Card* out);
bool card_known();
uint32_t card_status(int file);           // the list's status word for a file
bool set_card_status(int file, uint32_t v);
bool card_exists(int file);               // the list's "exists" byte
bool set_card_exists(int file, bool on);
void* remote_storage();                   // the game's ISteamRemoteStorage

// A save is asked for in one of two ways, each putting its flags into GLOBAL_WK+4 and then handing the card's
// task to the scheduler's level 1 (FUN_006c5c10, scheduler.cpp): a save to a file (FUN_009641c0 - a typewriter,
// a chapter's end, an ending's clear save: 8, 10, 20, 40) and a save of the system data alone (FUN_00964260 - the
// clear times' records as an ending's results come up, an option changed, the Mercenaries' and Assignment Ada's
// ends: 98, whose 80 is "the system data only" - "Saving...", no file list). The scheduler turns a task away
// while the level holds one already ("TASK DON'T EXEC"), and neither request takes its flags back: only the card's
// own end clears them (pG+4 &= 80000007), so the next save asked for runs with them. Separate Ways' records save
// turned away as its results came up left 80, and the clear save after them saved the system data only, with no
// file list: no clear file. The card's code is all that reads them (0x95B000..0x965000).
// Site::kSaveTurnedAway is the fix: where the scheduler's start turns a task away it called two empty debug
// prints; in their place, when the task turned away is the card's and the level does not hold the card's task (a
// save already there keeps its flags until its own end), the flags are cleared as the card's end clears them. It
// runs only when a task is turned away; it goes in at the first tick and comes out only as the mod unloads.

}  // namespace re4cc::game
