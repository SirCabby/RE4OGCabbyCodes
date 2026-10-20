// The mod's own discovery (src/game.cpp) run over the real bio4.exe, mapped by
// the Windows loader as an image (not run), under Wine:
//
//   i686-w64-mingw32-g++ -std=c++20 -O2 -DWIN32_LEAN_AND_MEAN -DNOMINMAX -static -Isrc \
//       -Wl,--image-base=0x20000000 -Wl,--disable-dynamicbase \
//       tests/test_discover.cpp src/game.cpp src/config.cpp tests/log_stub.cpp -o tests/build/test_discover.exe
//   wine tests/build/test_discover.exe "Z:\path\to\bio4.exe"
//
// It checks that every signature resolves and that what discovery derives is
// what tools/check_sigs.py derives (the VAs below are build 10112090's, at the
// exe's preferred base). A load like this one leaves the image where the loader
// put it without applying its base relocations (Wine maps it away from
// 0x400000, where the test itself sits), so the test applies them as the
// loader does for the running game - which also exercises discovery at another
// base, as ASLR gives it on Windows.
#include <windows.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "aimhold.h"
#include "gallery.h"
#include "game.h"
#include "items.h"
#include "mercmem.h"
#include "mercwater.h"
#include "sigs.h"

using namespace re4cc;

static int g_fail = 0;
#define CHECK(cond, ...)                 \
  do {                                   \
    if (!(cond)) {                       \
      ++g_fail;                          \
      std::printf("FAIL: " __VA_ARGS__); \
      std::printf("\n");                 \
    }                                    \
  } while (0)

// The image's base relocations applied for where it was mapped (HIGHLOW only:
// all a 32-bit image has).
static bool relocate(HMODULE m) {
  const auto base = reinterpret_cast<uintptr_t>(m);
  auto* nt = reinterpret_cast<IMAGE_NT_HEADERS*>(base + reinterpret_cast<IMAGE_DOS_HEADER*>(base)->e_lfanew);
  const intptr_t delta = static_cast<intptr_t>(base - nt->OptionalHeader.ImageBase);
  if (!delta) return true;
  const IMAGE_DATA_DIRECTORY& dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC];
  if (!dir.VirtualAddress) return false;
  DWORD old = 0;
  VirtualProtect(m, nt->OptionalHeader.SizeOfImage, PAGE_EXECUTE_READWRITE, &old);
  for (uintptr_t p = base + dir.VirtualAddress, end = p + dir.Size; p < end;) {
    const auto* block = reinterpret_cast<const IMAGE_BASE_RELOCATION*>(p);
    if (!block->SizeOfBlock) break;
    const auto* entry = reinterpret_cast<const WORD*>(p + sizeof(IMAGE_BASE_RELOCATION));
    const int n = static_cast<int>((block->SizeOfBlock - sizeof(IMAGE_BASE_RELOCATION)) / 2);
    for (int i = 0; i < n; ++i)
      if ((entry[i] >> 12) == IMAGE_REL_BASED_HIGHLOW)
        *reinterpret_cast<uint32_t*>(base + block->VirtualAddress + (entry[i] & 0xFFF)) += static_cast<uint32_t>(delta);
    p += block->SizeOfBlock;
  }
  return true;
}

// The countdown's editor, run: the option screen's opening copies the CountDown
// aside (FUN_005a3cf0, thiscall(CountDown, copy)) and its closing copies it back
// (FUN_005a3d80), on the image's own Cockpit (0xC50FF8: the CountDown at +98,
// the copy at +AC), with the clock shown (the ID system's unit 0x10 of 0x23 -
// here the lookup's dummy - bit 3) and not. An edit to the CountDown alone in
// between, what the editor wrote until 2026-10-18, is undone as the screen
// closes; set_countdown() writes the copy too, and the clock comes back as set.
static void run_countdown(uintptr_t base) {
  const uintptr_t rel = base - 0x400000;
  uint8_t* const cd = reinterpret_cast<uint8_t*>(0xC51090 + rel);
  uint8_t* const copy = reinterpret_cast<uint8_t*>(0xC510A4 + rel);
  uint8_t* const unit = reinterpret_cast<uint8_t*>(0xC599D0 + rel);
  uint8_t* const option = reinterpret_cast<uint8_t*>(0xC79A99 + rel);
  using Copy = void(__attribute__((thiscall))*)(void* countdown, void* copy);
  const auto keep = reinterpret_cast<Copy>(0x5A3CF0 + rel);
  const auto back = reinterpret_cast<Copy>(0x5A3D80 + rel);
  game::Countdown c;
  CHECK(game::countdown(&c) && c.known && c.editable, "the option screen's copy of the countdown not found");
  if (!c.editable) return;
  static uint8_t cd_saved[0x14], copy_saved[0x10], unit_saved[0x144];
  std::memcpy(cd_saved, cd, sizeof(cd_saved));
  std::memcpy(copy_saved, copy, sizeof(copy_saved));
  std::memcpy(unit_saved, unit, sizeof(unit_saved));
  const uint8_t option_saved = *option;
  uint32_t& state = *reinterpret_cast<uint32_t*>(cd);
  float& frames = *reinterpret_cast<float*>(cd + 8);
  // A minute left on a running clock (its warning from 10 s), and the option screen opening over it.
  auto open = [&](bool shown) {
    state = 1;
    frames = 1800.0f;
    *reinterpret_cast<uint32_t*>(cd + 0xC) = 300;
    unit[0] = shown ? 8 : 0;
    keep(cd, copy);
    *option = 1;
  };
  auto close = [&] {
    *option = 0;
    back(cd, copy);
  };
  for (bool shown : {false, true}) {
    const char* how = shown ? "shown" : "not shown";
    open(shown);
    CHECK(*reinterpret_cast<float*>(copy + 4) == 1800.0f && (state & 0x10) == (shown ? 0x10u : 0u),
          "the clock (%s) kept aside: %.1f, its state %X", how, *reinterpret_cast<float*>(copy + 4), state);
    frames = 300.0f;  // the CountDown alone
    close();
    CHECK(frames == 1800.0f, "the clock (%s) set to 10 s in the CountDown alone comes back at %.1f frames as the option "
          "screen closes - expected 1800 (the game's copy)", how, frames);
    open(shown);
    CHECK(game::set_countdown(300.0f), "set_countdown() (the clock %s) refused with the option screen up", how);
    CHECK(game::countdown(&c) && c.running && c.frames == 300.0f, "the panel reads %.1f frames after the edit (the "
          "clock %s) - expected 300", c.frames, how);
    close();
    CHECK(frames == 300.0f && state == 1 && (!shown || (unit[0] & 8)), "set_countdown() (the clock %s): %.1f frames "
          "and state %X once the option screen has closed - expected 300, 1%s", how, frames, state,
          shown ? ", shown again" : "");
  }
  // Not with the option screen down (no copy to come back from), nor with no clock running.
  open(false);
  *option = 0;
  CHECK(!game::set_countdown(300.0f) && frames == 1800.0f, "set_countdown() went in with the option screen down");
  *option = 1;
  state = 0;
  *reinterpret_cast<uint32_t*>(copy) = 0;
  CHECK(!game::set_countdown(300.0f) && frames == 1800.0f, "set_countdown() went in with no clock running");
  std::memcpy(cd, cd_saved, sizeof(cd_saved));
  std::memcpy(copy, copy_saved, sizeof(copy_saved));
  std::memcpy(unit, unit_saved, sizeof(unit_saved));
  *option = option_saved;
  std::printf("countdown: the option screen puts the clock back from its own copy as it closes - an edit to the "
              "CountDown alone is undone, set_countdown()'s stays (the clock shown and not)\n");
}

// A save the game asked for that its scheduler turned away, run: the game's own start of a task at a level
// (FUN_006c5c10) on the image's scheduler, level 1 held by a movie's task, after the flags a save of the system
// data alone puts into GLOBAL_WK+4 (98, as FUN_00964260 does) - turned away. Then the card's own check
// (FUN_00962500, routine 1, its last two steps) on a stand-in card in save mode, with the flags the clear save
// asks for (8, 40, as an ending's FUN_009641c0(0, 12) does) on top. Without the fix the flags are left, and the
// clear save's check goes on to the system data alone (routine 8) - no file list, what Separate Ways' clear save
// did. With it (kSaveTurnedAway, in the start itself) the start that turns the card away clears them (a load's 4
// and the start-up check's 1 kept), and the check goes on to the file list (routine 2); the start clears nothing
// when the level holds the card's task (a save there clears them at its end) or another task is turned away, and
// a start that goes ahead is the game's.
static void run_save_request(uintptr_t base) {
  const uintptr_t rel = base - 0x400000;
  uint8_t* const level1 = reinterpret_cast<uint8_t*>(0xC67D40 + 0x348 + rel);
  uintptr_t& level1_task = *reinterpret_cast<uintptr_t*>(level1 + 0x33C);
  const uintptr_t pg = *reinterpret_cast<uintptr_t*>(0xC06F3C + rel);
  uint32_t& flags = *reinterpret_cast<uint32_t*>(pg + 4);
  uint32_t& card_ec = *reinterpret_cast<uint32_t*>(0x12222EC + rel);
  using Start = uintptr_t(__cdecl*)(int level, uintptr_t task, int arg);
  using Routine = void(__attribute__((fastcall))*)(void* card);
  const auto start = reinterpret_cast<Start>(0x6C5C10 + rel);
  const auto check = reinterpret_cast<Routine>(0x962500 + rel);
  const uintptr_t card_task = 0x4041D3 + rel, movie_task = 0x406C94 + rel;
  mem::Patch* fix = game::patch(game::Site::kSaveTurnedAway);
  CHECK(fix && fix->at == 0x6C5C27 + rel && fix->replacement.size() == 34,
        "the fix of a save turned away is not the 34 bytes at +2C5C27");
  if (!fix) return;
  static uint8_t level_saved[0x348], fake[0x5A0];
  std::memcpy(level_saved, level1, sizeof(level_saved));
  const uint32_t flags_saved = flags, ec_saved = card_ec;
  // The routine the card goes on to after its check, on a stand-in card in save mode at step 7 (step 8 too, when
  // 7 sends it there), with pG+4 at `f` for it.
  auto after_check = [&](uint32_t f) -> int {
    std::memset(fake, 0, sizeof(fake));
    *reinterpret_cast<int32_t*>(fake + 0x14) = 1;
    fake[4] = 1;
    fake[5] = 7;
    const uint32_t was = flags;
    flags = f;
    check(fake);
    if (fake[4] == 1 && fake[5] == 8) check(fake);
    flags = was;
    return fake[4];
  };
  for (const bool in : {false, true}) {
    const char* how = in ? "with the fix" : "without the fix";
    CHECK(in ? fix->apply() : !fix->applied, "the fix did not go in");
    level1[4] = 0;
    CHECK(start(1, movie_task, 0) != 0 && level1[4] == 1, "%s, the start did not give a free level 1 to a movie", how);
    level1[4] = 3;
    flags = 0x5 | 0x98;
    CHECK(start(1, card_task, 1) == 0 && level1[4] == 3 && level1_task == movie_task,
          "%s, the start did not turn the card's task away from a level 1 held by a movie", how);
    const uint32_t want = in ? 0x5 : 0x9D;
    CHECK(flags == want, "%s, pG+4 is %X once the card's task was turned away - expected %X", how, flags, want);
    const int routine = after_check(flags | 0x48);
    CHECK(routine == (in ? 2 : 8), "%s, the clear save's check after it goes on to routine %d - expected %d (%s)", how,
          routine, in ? 2 : 8, in ? "the file list" : "the system data alone");
  }
  level1[4] = 0;
  flags = 0x5 | 0x48;
  CHECK(start(1, card_task, 1) != 0 && level1[4] == 1 && level1_task == card_task && flags == 0x4D,
        "with the fix, the clear save's start was not the game's (pG+4 %X)", flags);
  flags |= 0x98;
  CHECK(start(1, card_task, 1) == 0 && flags == 0xDD,
        "with the fix, a save asked for while the card's task starts at level 1 cleared the flags (pG+4 %X)", flags);
  level1[4] = 3;
  CHECK(start(1, card_task, 1) == 0 && flags == 0xDD,
        "with the fix, a save asked for while the card's task runs at level 1 cleared the flags (pG+4 %X)", flags);
  level1_task = movie_task;
  CHECK(start(1, movie_task, 0) == 0 && flags == 0xDD, "with the fix, another task turned away cleared the flags (pG+4 %X)",
        flags);
  CHECK(fix->revert() && !fix->applied, "the fix did not come out");
  std::memcpy(level1, level_saved, sizeof(level_saved));
  flags = flags_saved;
  card_ec = ec_saved;
  std::printf("save requests: a system-data save turned away from a busy level 1 leaves 98 in pG+4 and the clear "
              "save's check goes to the system data alone (routine 8); with the fix the start that turns it away "
              "clears them and the check goes to the file list (routine 2) - not with the card's task at the level, "
              "nor for another task\n");
}

// Automatic QTEs, run: the game's own prompt check (cActionButton::checkButton,
// FUN_0058a300) on a prompt of every kind with no button down, and its grab
// shake-off (FUN_00680d80), before the patches, with them and after. The image
// is mapped for real and both only touch the game's own data (the key state,
// GLOBAL_WK, the action buttons' object), all zeroed in a fresh mapping.
static void run_qte(uintptr_t base) {
  const uintptr_t rel = base - 0x400000;
  mem::Patch* k13 = game::patch(game::Site::kQteKind13);
  mem::Patch* kinds = game::patch(game::Site::kQteKinds);
  mem::Patch* kinds_lr = game::patch(game::Site::kQteKindsLR);
  mem::Patch* shake = game::patch(game::Site::kQteShake);
  if (!k13 || !kinds || !kinds_lr || !shake) return;
  auto* flag = reinterpret_cast<volatile uint8_t*>(*reinterpret_cast<uint32_t*>(k13->at - 9 + 4));
  using Check = uint8_t(__attribute__((thiscall))*)(void*, void*);
  const auto check = reinterpret_cast<Check>(0x58A300 + rel);
  static uint8_t self[0x200];
  // The answer (0/1) and whether the QTE flag went up, for each kind 1..19.
  auto answers = [&](int* ret, int* raised) {
    for (int kind = 1; kind <= 19; ++kind) {
      uint8_t work[0x18] = {};
      work[0xF] = static_cast<uint8_t>(kind);
      *flag = 0;
      ret[kind] = check(self, work);
      raised[kind] = *flag;
    }
  };
  auto qte = [](int kind) { return kind == 2 || kind == 3 || kind == 4 || kind == 9 || kind == 10 || kind == 13; };
  int ret[20], raised[20];
  answers(ret, raised);
  for (int kind = 1; kind <= 19; ++kind)
    CHECK(ret[kind] == 0 && raised[kind] == (qte(kind) ? 1 : 0), "unpatched kind %d: answer %d, QTE flag %d", kind,
          ret[kind], raised[kind]);
  CHECK(k13->apply() && kinds->apply() && kinds_lr->apply(), "the QTE check's patches did not go in");
  answers(ret, raised);
  for (int kind = 1; kind <= 19; ++kind)
    CHECK(ret[kind] == (qte(kind) ? 1 : 0) && raised[kind] == (qte(kind) ? 1 : 0),
          "patched kind %d: answer %d, QTE flag %d", kind, ret[kind], raised[kind]);
  // An ordinary prompt still answers to its button: kind 1, the action button
  // (GLOBAL_WK+501C bit 0x4000, set by the key handler).
  const uintptr_t pg = *reinterpret_cast<uint32_t*>(0xC06F3C + rel);
  *reinterpret_cast<uint32_t*>(pg + 0x501C) |= 0x4000;
  uint8_t work[0x18] = {};
  work[0xF] = 1;
  CHECK(check(self, work) == 1, "patched: kind 1 does not answer its button");
  *reinterpret_cast<uint32_t*>(pg + 0x501C) &= ~0x4000u;
  CHECK(kinds_lr->revert() && kinds->revert() && k13->revert(), "the QTE check's patches did not come out");
  answers(ret, raised);
  for (int kind = 1; kind <= 19; ++kind)
    CHECK(ret[kind] == 0 && raised[kind] == (qte(kind) ? 1 : 0), "reverted kind %d: answer %d, QTE flag %d", kind,
          ret[kind], raised[kind]);

  // The shake-off adds to the player's +568 for the buttons pressed: none here.
  const auto shake_off = reinterpret_cast<void (*)()>(0x680D80 + rel);
  auto* player_slot = reinterpret_cast<uint32_t*>(*reinterpret_cast<uint32_t*>(0x680D80 + rel + 3));
  static uint8_t player[0x1000];
  *player_slot = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(player));
  auto* struggle = reinterpret_cast<volatile int32_t*>(player + 0x568);
  shake_off();
  CHECK(*struggle == 0, "unpatched shake-off: %d with no button down", *struggle);
  CHECK(shake->apply(), "the shake-off's patch did not go in");
  shake_off();
  shake_off();
  CHECK(*struggle == 32, "patched shake-off: %d after two frames, expected 32", *struggle);
  CHECK(shake->revert(), "the shake-off's patch did not come out");
  shake_off();
  CHECK(*struggle == 32, "reverted shake-off: %d", *struggle);
  *player_slot = 0;
  std::printf("QTEs run: the prompt check answers kinds 2, 3, 4, 9, 10, 13 \"pressed\" with the patches only; the "
              "shake-off adds 16 a frame\n");
}

// The statue chase's run, run: the game's own run step (FUN_00858b20, cdecl).
// A press adds the frames since the last one, cut to whole 1/30 s by the CRT's
// ftol, to the run's counter (0..49; the run's speed is a tenth of it, 0..4);
// with no press for longer than its limit (6/30 s at DA level 5) the counter
// falls by 3 a step - at 60 fps from the 13th step on. At 60 fps (a delta of
// 0.5) a press every frame adds nothing, the key's or Automatic QTEs' forced one
// alike, and at 30 fps one a frame. With the speed's patch in, a press puts the
// counter at its top. The speed the counter picks is made the current one
// before each step, and a fall stays within one speed (39 to 30), so the step
// never sets a motion (the player is untouched).
static void run_statue(uintptr_t base) {
  const uintptr_t rel = base - 0x400000;
  mem::Patch* press = game::patch(game::Site::kQteStatueRun);
  mem::Patch* top = game::patch(game::Site::kQteStatueSpeed);
  if (!press || !top) return;
  using Step = uint8_t (*)(int32_t*, int32_t*, int32_t*, float*, int, int, int, int, void*, void*);
  const auto step = reinterpret_cast<Step>(0x858B20 + rel);
  const uintptr_t pg = *reinterpret_cast<uint32_t*>(0xC06F3C + rel);
  auto* delta = reinterpret_cast<float*>(pg + 0x70);
  auto* trg = reinterpret_cast<uint64_t*>(0xC62F10 + rel);  // Key.trg
  *reinterpret_cast<uint8_t*>(pg + 0x4F98) = 5;            // the DA level
  // The counter after some frames from `from`, the run's arguments as both its
  // callers pass them (10 a speed, a limit of 5 frames + the DA level's 1, 3 off
  // a frame past it, 5 speeds).
  auto run = [&](float d, bool key, int frames, int32_t from = 0) {
    int32_t counter = from, current = 0, speed = 0;
    float since = 0;
    *delta = d;
    *trg = key ? 0x80000 : 0;  // A
    for (int i = 0; i < frames; ++i) {
      current = counter / 10 > 4 ? 4 : counter / 10;
      step(&counter, &current, &speed, &since, 10, 5, 3, 5, nullptr, nullptr);
    }
    *trg = 0;
    return counter;
  };
  int n = run(0.5f, true, 60);
  CHECK(n == 0, "unpatched, 60 fps, A every frame: the run's counter %d after a second, expected 0", n);
  n = run(1.0f, true, 10);
  CHECK(n == 10, "unpatched, 30 fps, A every frame: the run's counter %d after 10 frames, expected 10", n);
  n = run(0.5f, false, 15, 39);
  CHECK(n == 30, "unpatched, 60 fps, no press: the run's counter %d after 15 frames from 39, expected 30", n);
  CHECK(press->apply(), "the statue run's press patch did not go in");
  n = run(0.5f, false, 60);
  CHECK(n == 0, "the press alone, 60 fps: the run's counter %d after a second, expected 0", n);
  CHECK(top->apply(), "the statue run's speed patch did not go in");
  n = run(0.5f, false, 1);
  CHECK(n == 49, "patched, 60 fps: the run's counter %d after one frame, expected 49", n);
  n = run(1.0f, false, 1);
  CHECK(n == 49, "patched, 30 fps: the run's counter %d after one frame, expected 49", n);
  n = run(0.5f, false, 60);
  CHECK(n == 49, "patched, 60 fps: the run's counter %d after a second, expected 49", n);
  CHECK(top->revert() && press->revert(), "the statue run's patches did not come out");
  n = run(0.5f, true, 60);
  CHECK(n == 0, "reverted, 60 fps, A every frame: the run's counter %d after a second, expected 0", n);
  n = run(0.5f, false, 15, 39);
  CHECK(n == 30, "reverted, 60 fps, no press: the run's counter %d after 15 frames from 39, expected 30", n);
  *delta = 0;
  *reinterpret_cast<uint8_t*>(pg + 0x4F98) = 0;
  std::printf("the statue's run, run: at 60 fps a press every frame leaves the run's counter at 0 (30 fps: +1 a "
              "frame); with the speed's patch a press puts it at its top, 49\n");
}

// The partner's heavy hits, run: her hit check (cSubChar::damageCheck,
// FUN_0075edf0, ecx = the partner) on a stand-in Ashley with a hit of type 13
// (an explosion) waiting at +32C/+32E, its distance squared at +33C. Nearer than
// 3000 the game takes 9999 off her and starts the fall that ends in her death
// (routine 1, reaction 7 or 9 at +574) whatever her health; further off she
// ducks (routine 0, step 7: +FC = 0x700). With kAshleyBlast in, a near one is
// treated as a far one. The check's only calls on her are two virtuals (+28,
// +2C) the stand-in answers with nothing.
static void __attribute__((thiscall)) partner_nop(void*, int) {}
static void run_blast(uintptr_t base) {
  const uintptr_t rel = base - 0x400000;
  mem::Patch* blast = game::patch(game::Site::kAshleyBlast);
  if (!blast) return;
  CHECK(blast->at == 0x75EF89 + rel, "the partner's heavy hit's jp at +%X", static_cast<unsigned>(blast->at - base));
  using Check = void(__attribute__((fastcall))*)(void*);
  const auto damage_check = reinterpret_cast<Check>(0x75EDF0 + rel);
  const uintptr_t pg = *reinterpret_cast<uint32_t*>(0xC06F3C + rel);
  static void* vtable[16];
  static uint8_t ashley[0x900];
  for (void*& f : vtable) f = reinterpret_cast<void*>(&partner_nop);
  struct Outcome {
    uint32_t routine;
    int reaction, hp;
  };
  auto hit = [&](float dist_sq) {
    std::memset(ashley, 0, sizeof(ashley));
    *reinterpret_cast<void***>(ashley) = vtable;
    ashley[0x100] = 5;     // a partner, for LifeDownSet2
    ashley[0x32C] = 1;     // a hit waiting
    ashley[0x32E] = 0x13;  // an explosion's
    std::memcpy(ashley + 0x33C, &dist_sq, sizeof(dist_sq));
    *reinterpret_cast<int*>(ashley + 0x574) = -1;
    *reinterpret_cast<int16_t*>(pg + 0x4FB8) = 600;
    *reinterpret_cast<int16_t*>(pg + 0x4FBA) = 600;
    damage_check(ashley);
    return Outcome{*reinterpret_cast<uint32_t*>(ashley + 0xFC), *reinterpret_cast<int*>(ashley + 0x574),
                   *reinterpret_cast<int16_t*>(pg + 0x4FB8)};
  };
  const float close_by = 0.0f, far_off = 1.0e7f;  // squared: 0 and 3162
  auto fall = [](const Outcome& o) { return o.routine == 1 && (o.reaction == 7 || o.reaction == 9) && o.hp == 0; };
  auto duck = [](const Outcome& o) { return o.routine == 0x700 && o.reaction == -1 && o.hp == 600; };
  Outcome o = hit(close_by);
  CHECK(fall(o), "unpatched, near: routine %X, reaction %d, health %d - expected the fall (1, 7 or 9, 0)", o.routine,
        o.reaction, o.hp);
  o = hit(far_off);
  CHECK(duck(o), "unpatched, far: routine %X, reaction %d, health %d - expected ducking (700)", o.routine, o.reaction,
        o.hp);
  CHECK(blast->apply(), "the heavy hit's patch did not go in");
  o = hit(close_by);
  CHECK(duck(o), "patched, near: routine %X, reaction %d, health %d - expected ducking (700)", o.routine, o.reaction,
        o.hp);
  o = hit(far_off);
  CHECK(duck(o), "patched, far: routine %X, reaction %d, health %d", o.routine, o.reaction, o.hp);
  CHECK(blast->revert(), "the heavy hit's patch did not come out");
  o = hit(close_by);
  CHECK(fall(o), "reverted, near: routine %X, reaction %d, health %d", o.routine, o.reaction, o.hp);
  *reinterpret_cast<int16_t*>(pg + 0x4FB8) = 0;
  *reinterpret_cast<int16_t*>(pg + 0x4FBA) = 0;
  std::printf("the partner's heavy hits run: a near one kills her unpatched, makes her duck patched\n");
}

// One hit kills on El Gigante, run: its hit check (FUN_004b07c0, eax = El
// Gigante, as its move calls it) on a stand-in El Gigante (em 2B, 3000 health,
// about its business: +FC 1) with a hit waiting (+32C) from a weapon (+32E) on a
// part (+340: its +26 the part's number, 3F the parasite). Unpatched, a
// handgun's hit (30 damage at Normal) on the body takes no health and adds its
// damage to the stagger (+A18); on the parasite it takes twice that; a flash
// (17) on the body blinds it (+FC 0x502); a rocket's (0D) kills it on any part
// (the health at 0 or below, +FC 3: the death). Patched, a hit with damage but a
// flash's kills it as the rocket's does, also on a body the game lets be (in a
// routine of its damage, +408 bit 8, or holding someone, +3D0 bit 8); a flash,
// and a hit that does no damage (an incendiary's, 16: none on El Gigante), are
// as before. The knife on its back: its 8 bytes run on their own, 70 off
// unpatched and all of it patched. The hit's effects and sounds (FUN_005b3a20,
// FUN_0096e230, both cdecl) are stubbed with a ret at their thunks for the run -
// they draw and play, and touch nothing the check reads - and GLOBAL_WK is put
// back as it was (the death counts a kill and marks the enemy dead).
// An enemy's hit check called as its move calls it: eax = the enemy.
__attribute__((noinline)) static void hit_check(uintptr_t fn, void* em) {
  uintptr_t a;
  asm volatile("call *%1" : "=a"(a) : "r"(fn), "0"(em) : "ecx", "edx", "memory", "cc");
  (void)a;
}
static void run_gigante(uintptr_t base) {
  const uintptr_t rel = base - 0x400000;
  mem::Patch* hurt = game::patch(game::Site::kHurtGigante);
  mem::Patch* knife = game::patch(game::Site::kHurtGiganteKnife);
  if (!hurt || !knife) return;
  CHECK(hurt->at == 0x4B0AC2 + rel && hurt->replacement.size() == 42, "El Gigante's hit check's site at +%X",
        static_cast<unsigned>(hurt->at - base));
  CHECK(knife->at == 0x4B63D4 + rel, "the knife on El Gigante's back at +%X", static_cast<unsigned>(knife->at - base));
  uint8_t* const thunks[] = {reinterpret_cast<uint8_t*>(0x406415 + rel), reinterpret_cast<uint8_t*>(0x40ADC1 + rel)};
  for (uint8_t* t : thunks)
    if (*t != 0xE9) {
      CHECK(false, "the hit's effects' or sound's thunk at +%X is no jmp",
            static_cast<unsigned>(reinterpret_cast<uintptr_t>(t) - base));
      return;
    }
  for (uint8_t* t : thunks) *t = 0xC3;
  const uintptr_t pg = *reinterpret_cast<uint32_t*>(0xC06F3C + rel);
  static uint8_t pg_saved[0x8690];
  std::memcpy(pg_saved, reinterpret_cast<const void*>(pg), sizeof(pg_saved));
  *reinterpret_cast<uint8_t*>(pg + 0x847C) = 5;  // Normal (below 2 the damage asks the player for more)
  const uintptr_t check = 0x4B07C0 + rel;
  static uint8_t em[0xC00];
  static uint8_t part[0x40];
  struct Outcome {
    int hp;
    uint32_t routine;
    int stagger;
  };
  auto hit = [&](int weapon, int part_no, uint32_t flags408 = 0, uint32_t flags3d0 = 0) {
    std::memset(em, 0, sizeof(em));
    std::memset(part, 0, sizeof(part));
    em[0x100] = 0x2B;
    *reinterpret_cast<uint32_t*>(em + 0xFC) = 1;
    *reinterpret_cast<int16_t*>(em + 0x324) = 3000;
    em[0x32C] = 1;
    em[0x32E] = static_cast<uint8_t>(weapon);
    *reinterpret_cast<uint8_t**>(em + 0x340) = part;
    *reinterpret_cast<uint32_t*>(em + 0x3D0) = flags3d0;
    *reinterpret_cast<uint32_t*>(em + 0x408) = flags408;
    *reinterpret_cast<int16_t*>(part + 0x26) = static_cast<int16_t>(part_no);
    hit_check(check, em);
    return Outcome{*reinterpret_cast<int16_t*>(em + 0x324), *reinterpret_cast<uint32_t*>(em + 0xFC),
                   *reinterpret_cast<int*>(em + 0xA18)};
  };
  constexpr int kHandgun = 2, kRocket = 0xD, kIncendiary = 0x16, kFlash = 0x17, kBody = 2, kParasite = 0x3F;
  constexpr uint32_t kInDamage = 8, kHolding = 8, kParasiteOut = 0x2000;
  auto is = [](const Outcome& o, int hp, uint32_t routine, int stagger) {
    return o.hp == hp && o.routine == routine && o.stagger == stagger;
  };
  auto dead = [](const Outcome& o) { return o.hp <= 0 && o.routine == 3; };
  auto as_the_game = [&](const char* when) {
    Outcome o = hit(kHandgun, kBody);
    CHECK(is(o, 3000, 1, 30), "%s, a handgun on the body: health %d, routine %X, stagger %d - expected 3000, 1, 30",
          when, o.hp, o.routine, o.stagger);
    o = hit(kHandgun, kParasite);
    CHECK(is(o, 2940, 1, 0), "%s, a handgun on the parasite: health %d, routine %X - expected 2940, 1", when, o.hp,
          o.routine);
    o = hit(kHandgun, kBody, kInDamage);
    CHECK(is(o, 3000, 1, 0), "%s, a handgun on the body in its damage routine: health %d, routine %X, stagger %d",
          when, o.hp, o.routine, o.stagger);
    o = hit(kRocket, kBody);
    CHECK(dead(o), "%s, a rocket on the body: health %d, routine %X - expected the death", when, o.hp, o.routine);
  };
  as_the_game("unpatched");
  Outcome o = hit(kFlash, kBody);
  CHECK(is(o, 3000, 0x502, 0), "unpatched, a flash on the body: health %d, routine %X - expected 3000, 502", o.hp,
        o.routine);
  CHECK(hurt->apply(), "El Gigante's hit check's patch did not go in");
  for (int part_no : {kBody, kParasite}) {
    const char* where = part_no == kBody ? "the body" : "the parasite";
    o = hit(kHandgun, part_no);
    CHECK(is(o, 0, 3, 0), "patched, a handgun on %s: health %d, routine %X, stagger %d - expected the death (0, 3)",
          where, o.hp, o.routine, o.stagger);
    o = hit(kRocket, part_no);
    CHECK(is(o, 0, 3, 0), "patched, a rocket on %s: health %d, routine %X", where, o.hp, o.routine);
  }
  o = hit(kHandgun, kBody, kInDamage);
  CHECK(is(o, 0, 3, 0), "patched, a handgun on the body in its damage routine: health %d, routine %X", o.hp, o.routine);
  o = hit(kHandgun, kBody, 0, kHolding);
  CHECK(is(o, 0, 3, 0), "patched, a handgun on the body holding someone: health %d, routine %X", o.hp, o.routine);
  o = hit(kHandgun, kParasite, kParasiteOut);
  CHECK(is(o, 0, 3, 0), "patched, a handgun on the parasite out: health %d, routine %X", o.hp, o.routine);
  o = hit(kFlash, kBody);
  CHECK(is(o, 3000, 0x502, 0), "patched, a flash on the body: health %d, routine %X - expected 3000, 502", o.hp,
        o.routine);
  o = hit(kFlash, kParasite);
  CHECK(is(o, 3000, 1, 0), "patched, a flash on the parasite: health %d, routine %X - expected 3000, 1", o.hp,
        o.routine);
  o = hit(kIncendiary, kBody);
  CHECK(is(o, 3000, 1, 0), "patched, an incendiary on the body: health %d, routine %X, stagger %d - expected as before",
        o.hp, o.routine, o.stagger);
  o = hit(kIncendiary, kParasite);
  CHECK(is(o, 3000, 1, 0), "patched, an incendiary on the parasite: health %d, routine %X", o.hp, o.routine);
  CHECK(hurt->revert(), "El Gigante's hit check's patch did not come out");
  as_the_game("reverted");

  // The slash: mov eax,[esp+4]; <its 8 bytes>; ret, on the stand-in's health.
  static uint8_t* code = static_cast<uint8_t*>(VirtualAlloc(nullptr, 32, MEM_COMMIT | MEM_RESERVE,
                                                             PAGE_EXECUTE_READWRITE));
  auto slash = [&](const std::vector<uint8_t>& eight) {
    *reinterpret_cast<int16_t*>(em + 0x324) = 3000;
    const uint8_t head[] = {0x8B, 0x44, 0x24, 0x04};
    std::memcpy(code, head, sizeof(head));
    std::memcpy(code + sizeof(head), eight.data(), 8);
    code[sizeof(head) + 8] = 0xC3;
    FlushInstructionCache(GetCurrentProcess(), code, 32);
    reinterpret_cast<void(__cdecl*)(void*)>(code)(em);
    return static_cast<int>(*reinterpret_cast<int16_t*>(em + 0x324));
  };
  if (code) {
    int left = slash(knife->original);
    CHECK(left == 2930, "the game's slash leaves %d of 3000, expected 2930", left);
    left = slash(knife->replacement);
    CHECK(left == 0, "the patched slash leaves %d of 3000, expected 0", left);
  }
  CHECK(knife->apply() && knife->revert(), "the knife's patch did not go in and come out");

  std::memcpy(reinterpret_cast<void*>(pg), pg_saved, sizeof(pg_saved));
  for (uint8_t* t : thunks) *t = 0xE9;
  std::printf("El Gigante run: a handgun's hit takes no health on its body and twice its damage on the parasite "
              "unpatched, and kills it as a rocket's does patched; a flash and an incendiary as before; a slash 70, "
              "patched all of it\n");
}

// One hit kills on Krauser, run: his hit check (FUN_00548200, eax = Krauser, as
// his move calls it) on a stand-in Krauser (em 39) with a hit waiting (+32C)
// from a weapon (+32E) on a part (+340: its +26 the part's number - 5 the head,
// 62 one of the armoured arm's), at Normal with the dynamic difficulty at 5 (the
// damage as the table has it: a handgun 15 on him) in his fight's room (31C,
// where his death is the room script's: no routine of its own). Type 1 is the
// first part of the fight (10000 health), type 2 the mutated Krauser (7000).
// Unpatched, a handgun's hit takes 15 (30 and a flinch, +FC 0x102, on the head)
// and adds as much to his count of the damage (+AA4); on a mutated arm it
// glances off; a flash (17) does no damage and stuns him (type 1: 0x102).
// Patched (with LifeDownSet2's own patch), a hit with damage counts 1000: type 1
// keeps his last point (the game's "survive at 1"), flinches (+FC 2, or 0x102 on
// the head) and has his stage's whole share (1000) - unless a flinch is under way
// already (+408 bit 3), which the hit leaves to end; type 2 dies, also of a hit on
// the arm; a flash is as before. Outside room 31C a death is his death routine
// (+FC 0x103). The hit's effects and sounds are stubbed as for El Gigante, and
// GLOBAL_WK is put back as it was (a death counts a kill and marks him dead).
static void run_krauser(uintptr_t base) {
  const uintptr_t rel = base - 0x400000;
  mem::Patch* enemy = game::patch(game::Site::kHurtEnemy);
  mem::Patch* hurt = game::patch(game::Site::kHurtKrauser);
  mem::Patch* arm = game::patch(game::Site::kHurtKrauserArm);
  if (!enemy || !hurt || !arm) return;
  CHECK(hurt->at == 0x546BF0 + rel && hurt->replacement.size() == 31, "Krauser's damage's site at +%X",
        static_cast<unsigned>(hurt->at - base));
  CHECK(arm->at == 0x5483AC + rel && arm->replacement == std::vector<uint8_t>({0xEB, 0x4C}),
        "Krauser's arm's site at +%X", static_cast<unsigned>(arm->at - base));
  uint8_t* const thunks[] = {reinterpret_cast<uint8_t*>(0x406415 + rel), reinterpret_cast<uint8_t*>(0x40ADC1 + rel)};
  for (uint8_t* t : thunks)
    if (*t != 0xE9) {
      CHECK(false, "the hit's effects' or sound's thunk at +%X is no jmp",
            static_cast<unsigned>(reinterpret_cast<uintptr_t>(t) - base));
      return;
    }
  for (uint8_t* t : thunks) *t = 0xC3;
  const uintptr_t pg = *reinterpret_cast<uint32_t*>(0xC06F3C + rel);
  static uint8_t pg_saved[0x8690];
  std::memcpy(pg_saved, reinterpret_cast<const void*>(pg), sizeof(pg_saved));
  const uintptr_t check = 0x548200 + rel;
  static uint8_t em[0xD00];
  static uint8_t part[0x40];
  struct Outcome {
    int hp;
    uint32_t routine;
    int count;  // +AA4: his stage's count of the damage
  };
  auto hit = [&](int type, int weapon, int part_no, int room = 0x31C, uint32_t flags408 = 0) {
    std::memset(em, 0, sizeof(em));
    std::memset(part, 0, sizeof(part));
    // Each hit from the same GLOBAL_WK: a big hit's and a death's dynamic difficulty points (FUN_0063afa0) put
    // the level (+4F98) LifeDownSet2 scales by where the points say.
    std::memcpy(reinterpret_cast<void*>(pg), pg_saved, sizeof(pg_saved));
    *reinterpret_cast<uint8_t*>(pg + 0x847C) = 5;  // Normal (below 2 the damage asks the player for more)
    *reinterpret_cast<uint8_t*>(pg + 0x4F98) = 5;  // the dynamic difficulty at which LifeDownSet2 takes it as it is
    *reinterpret_cast<int16_t*>(pg + 0x4FAC) = static_cast<int16_t>(room);
    em[0x100] = 0x39;
    em[0x101] = static_cast<uint8_t>(type);
    *reinterpret_cast<uint32_t*>(em + 0xFC) = 1;
    const int16_t full = type < 2 ? 10000 : 7000;
    *reinterpret_cast<int16_t*>(em + 0x324) = full;
    *reinterpret_cast<int16_t*>(em + 0x326) = full;
    em[0x32C] = 1;
    em[0x32E] = static_cast<uint8_t>(weapon);
    *reinterpret_cast<uint8_t**>(em + 0x340) = part;
    *reinterpret_cast<uint32_t*>(em + 0x408) = flags408;
    *reinterpret_cast<int16_t*>(part + 0x26) = static_cast<int16_t>(part_no);
    hit_check(check, em);
    return Outcome{*reinterpret_cast<int16_t*>(em + 0x324), *reinterpret_cast<uint32_t*>(em + 0xFC),
                   *reinterpret_cast<int*>(em + 0xAA4)};
  };
  constexpr int kFirst = 1, kMutated = 2, kHandgun = 2, kFlash = 0x17, kBody = 2, kHead = 5, kArm = 0x62;
  constexpr uint32_t kFlinching = 8;
  auto is = [](const Outcome& o, int hp, uint32_t routine, int count) {
    return o.hp == hp && o.routine == routine && o.count == count;
  };
  auto as_the_game = [&](const char* when) {
    Outcome o = hit(kFirst, kHandgun, kBody);
    CHECK(is(o, 9985, 1, 15), "%s, a handgun on the first part's Krauser: health %d, routine %X, count %d - expected "
          "9985, 1, 15", when, o.hp, o.routine, o.count);
    o = hit(kFirst, kHandgun, kHead);
    CHECK(is(o, 9970, 0x102, 30), "%s, a handgun on his head: health %d, routine %X, count %d - expected 9970, 102, 30",
          when, o.hp, o.routine, o.count);
    o = hit(kMutated, kHandgun, kBody);
    CHECK(is(o, 6985, 1, 15), "%s, a handgun on the mutated Krauser: health %d, routine %X, count %d", when, o.hp,
          o.routine, o.count);
    o = hit(kMutated, kHandgun, kArm);
    CHECK(is(o, 7000, 1, 0), "%s, a handgun on his armoured arm: health %d, routine %X, count %d - expected it to "
          "glance off", when, o.hp, o.routine, o.count);
    o = hit(kFirst, kFlash, kBody);
    CHECK(is(o, 10000, 0x102, 0), "%s, a flash: health %d, routine %X, count %d - expected 10000, 102, 0", when, o.hp,
          o.routine, o.count);
  };
  as_the_game("unpatched");
  CHECK(enemy->apply() && hurt->apply() && arm->apply(), "One hit kills' patches did not go in");
  Outcome o = hit(kFirst, kHandgun, kBody);
  CHECK(is(o, 1, 2, 1000), "patched, a handgun on the first part's Krauser: health %d, routine %X, count %d - "
        "expected 1 (he survives), a flinch (2) and his stage's whole share (1000)", o.hp, o.routine, o.count);
  o = hit(kFirst, kHandgun, kHead);
  CHECK(is(o, 1, 0x102, 1000), "patched, a handgun on his head: health %d, routine %X, count %d", o.hp, o.routine,
        o.count);
  o = hit(kFirst, kHandgun, kBody, 0x31C, kFlinching);
  CHECK(is(o, 1, 1, 1000), "patched, a handgun on him flinching already: health %d, routine %X, count %d", o.hp,
        o.routine, o.count);
  const int kills = *reinterpret_cast<const int*>(pg_saved + 0x8468);
  o = hit(kMutated, kHandgun, kBody);
  CHECK(o.hp <= 0 && o.routine == 1, "patched, a handgun on the mutated Krauser: health %d, routine %X - expected the "
        "death, left to the room's script", o.hp, o.routine);
  CHECK(*reinterpret_cast<int*>(pg + 0x8468) == kills + 1, "patched, the mutated Krauser's death counts no kill");
  o = hit(kMutated, kHandgun, kArm);
  CHECK(o.hp <= 0, "patched, a handgun on his armoured arm: health %d - expected the death", o.hp);
  o = hit(kMutated, kFlash, kArm);
  CHECK(is(o, 7000, 1, 0), "patched, a flash on his armoured arm: health %d, routine %X, count %d - expected as "
        "before", o.hp, o.routine, o.count);
  o = hit(kFirst, kFlash, kBody);
  CHECK(is(o, 10000, 0x102, 0), "patched, a flash: health %d, routine %X, count %d - expected as before", o.hp,
        o.routine, o.count);
  o = hit(3, kHandgun, kBody, 0x51C);
  CHECK(o.hp <= 0 && o.routine == 0x103, "patched, a handgun on Separate Ways' Krauser: health %d, routine %X - "
        "expected his death routine (103)", o.hp, o.routine);
  CHECK(arm->revert() && hurt->revert() && enemy->revert(), "One hit kills' patches did not come out");
  as_the_game("reverted");

  std::memcpy(reinterpret_cast<void*>(pg), pg_saved, sizeof(pg_saved));
  for (uint8_t* t : thunks) *t = 0xE9;
  std::printf("Krauser run: a handgun's hit takes 15 and counts 15 unpatched, glancing off his armoured arm; "
              "patched the first part's Krauser keeps his last point, flinches and counts his stage's whole share, "
              "and the mutated one dies, of a hit on the arm too; a flash as before\n");
}

// Steady aim, run: the game's own aim sway (FUN_00684330, cdecl(player, flags,
// float* up_down, float* left_right)) on a stand-in player whose weapon sways as
// a handgun does (box 0.1, speeds 0.005), at 30 fps (a delta of 1) and with the
// shot's throw on (0.2). Unpatched, a held aim drifts and a shot throws it off
// its centre; patched, neither moves it - and the same random numbers are drawn
// (the random function's state, 0xC67458, ends where it does unpatched).
static void run_aim(uintptr_t base) {
  const uintptr_t rel = base - 0x400000;
  mem::Patch* sway_v = game::patch(game::Site::kAimSwayV);
  mem::Patch* sway_h = game::patch(game::Site::kAimSwayH);
  mem::Patch* kick_v = game::patch(game::Site::kAimKickV);
  mem::Patch* kick_h = game::patch(game::Site::kAimKickH);
  if (!sway_v || !sway_h || !kick_v || !kick_h) return;
  CHECK(sway_v->at == 0x684445 + rel && sway_h->at == 0x68445B + rel && kick_v->at == 0x6843B5 + rel &&
            kick_h->at == 0x6843D6 + rel,
        "the aim's sites at +%X, +%X, +%X, +%X", static_cast<unsigned>(sway_v->at - base),
        static_cast<unsigned>(sway_h->at - base), static_cast<unsigned>(kick_v->at - base),
        static_cast<unsigned>(kick_h->at - base));
  using Sway = void(__cdecl*)(void*, int, float*, float*);
  const auto sway = reinterpret_cast<Sway>(0x684330 + rel);
  // player+7D8: the weapon work (cPlWep: +28/+2C the aim's centre, +34 the
  // weapon); the weapon (cObjWep): +334 the sway box (and the shot's up-down
  // throw), +338 the shot's left-right throw, +33C/+340 the drift's speeds.
  static uint8_t player[0x800], plwep[0x50], objwep[0x400];
  *reinterpret_cast<void**>(player + 0x7D8) = plwep;
  *reinterpret_cast<void**>(plwep + 0x34) = objwep;
  const float bure[4] = {0.1f, 0.05f, 0.005f, 0.005f};
  std::memcpy(objwep + 0x334, bure, sizeof(bure));
  const uintptr_t pg = *reinterpret_cast<uint32_t*>(0xC06F3C + rel);
  auto* delta = reinterpret_cast<float*>(pg + 0x70);
  auto* throw_scale = reinterpret_cast<float*>(0xC66BA8 + rel);
  auto* timer = reinterpret_cast<float*>(0xC66BFC + rel);
  auto* seed = reinterpret_cast<uint16_t*>(0xC67458 + rel);
  *delta = 1.0f;
  *throw_scale = 0.2f;
  const float* centre = reinterpret_cast<const float*>(plwep + 0x28);
  const double half_pi = 1.5707963705062866;  // the up-down angle's scale in and out (+-1 is +-90 degrees)

  // The aim put at (0.25, -0.1) - flag 1, as the aiming does when it moves -
  // then held for 90 frames (flag 0) or shot from 8 times (flag 2), starting
  // from one random state: the largest move from where it was put, each way,
  // and the largest distance from the centre after a shot.
  const uint16_t seed0 = *seed;
  struct Run {
    float moved_v = 0, moved_h = 0, off_v = 0, off_h = 0;
    uint16_t seed = 0;
    bool in_box = true;
  };
  auto run = [&](int flags, int calls) {
    Run r;
    float v = 0.25f, h = -0.1f;
    sway(player, 1, &v, &h);
    const float v0 = v, h0 = h;
    *seed = seed0;
    *timer = 0;
    for (int i = 0; i < calls; ++i) {
      if (flags == 2) {
        v = v0 + 0.01f;  // drifted off a little: a shot puts it back
        h = h0 - 0.01f;
      }
      sway(player, flags, &v, &h);
      const float dv = static_cast<float>(std::fabs(v - v0)), dh = std::fabs(h - h0);
      r.moved_v = dv > r.moved_v ? dv : r.moved_v;
      r.moved_h = dh > r.moved_h ? dh : r.moved_h;
      const float ov = static_cast<float>(std::fabs(v * half_pi - centre[0])), oh = std::fabs(h - centre[1]);
      r.off_v = ov > r.off_v ? ov : r.off_v;
      r.off_h = oh > r.off_h ? oh : r.off_h;
      r.in_box = r.in_box && ov <= bure[0] + 1e-6f && oh <= bure[0] + 1e-6f;
    }
    r.seed = *seed;
    return r;
  };
  const Run held = run(0, 90), shot = run(2, 8);
  CHECK(held.moved_v > 1e-3f && held.moved_h > 1e-3f && held.in_box,
        "unpatched: a held aim moved %g up-down, %g left-right (inside its box: %d)", held.moved_v, held.moved_h,
        held.in_box);
  CHECK(shot.off_v > 1e-4f && shot.off_h > 1e-4f, "unpatched: a shot threw the aim %g up-down, %g left-right off "
        "its centre", shot.off_v, shot.off_h);
  CHECK(sway_v->apply() && sway_h->apply() && kick_v->apply() && kick_h->apply(), "Steady aim's patches did not go in");
  const Run held_p = run(0, 90), shot_p = run(2, 8);
  // Up-down goes through the game's own *pi/2 and back each call: a float's
  // rounding is all that may move it; left-right is not scaled at all.
  CHECK(held_p.moved_v <= 1e-6f && held_p.moved_h == 0, "patched: a held aim moved %g up-down, %g left-right",
        held_p.moved_v, held_p.moved_h);
  CHECK(shot_p.off_v <= 1e-6f && shot_p.off_h == 0, "patched: a shot left the aim %g up-down, %g left-right off its "
        "centre", shot_p.off_v, shot_p.off_h);
  CHECK(held_p.seed == held.seed && shot_p.seed == shot.seed && held.seed != seed0,
        "patched: the random numbers drawn differ (state %04X / %04X, unpatched %04X / %04X)", held_p.seed,
        shot_p.seed, held.seed, shot.seed);
  CHECK(kick_h->revert() && kick_v->revert() && sway_h->revert() && sway_v->revert(),
        "Steady aim's patches did not come out");
  const Run held_r = run(0, 90);
  CHECK(held_r.moved_v == held.moved_v && held_r.moved_h == held.moved_h && held_r.seed == held.seed,
        "reverted: a held aim moved %g / %g, not as before", held_r.moved_v, held_r.moved_h);
  *delta = 0;
  *throw_scale = 0;
  std::printf("aim run: unpatched, a held aim drifts up to %.4f up-down, %.4f left-right in 3 s and a shot throws it "
              "%.4f / %.4f; patched, %g / %g and %g / %g, the same random numbers drawn\n",
              held.moved_v, held.moved_h, shot.off_v, shot.off_h, held_p.moved_v, held_p.moved_h, shot_p.off_v,
              shot_p.off_h);
}

// The shots' spread, for Steady aim: every site is the fmul that scales a random
// number just drawn (call random; fmul scale), and becomes fstp st(0); fldz. Each
// one's six bytes, as the game has them and as patched, run on their own after a
// stand-in random number of 1: the game's give its scale (the operand is the
// game's own constant, relocated with the image), the patch gives 0 - and both
// leave the x87 stack as deep as they found it.
static void run_spread(uintptr_t base) {
  const uintptr_t rel = base - 0x400000;
  // void f(float* out) { fld1; <six bytes>; fstp [out] } -> the x87 stack's top moved (0: no)
  static uint8_t* code = static_cast<uint8_t*>(VirtualAlloc(nullptr, 64, MEM_COMMIT | MEM_RESERVE,
                                                             PAGE_EXECUTE_READWRITE));
  if (!code) return;
  auto run6 = [&](const uint8_t* six, float* out) {
    const uint8_t head[] = {0x8B, 0x4C, 0x24, 0x04,  // mov ecx,[esp+4]
                            0x9B, 0xDF, 0xE0,        // fstsw ax
                            0x66, 0x8B, 0xD0,        // mov dx,ax
                            0xD9, 0xE8};             // fld1
    const uint8_t tail[] = {0xD9, 0x19,                    // fstp dword [ecx]
                            0x9B, 0xDF, 0xE0,              // fstsw ax
                            0x66, 0x33, 0xC2,              // xor ax,dx
                            0x25, 0x00, 0x38, 0x00, 0x00,  // and eax,3800 (the top of stack)
                            0xC3};                         // ret
    std::memcpy(code, head, sizeof(head));
    std::memcpy(code + sizeof(head), six, 6);
    std::memcpy(code + sizeof(head) + 6, tail, sizeof(tail));
    FlushInstructionCache(GetCurrentProcess(), code, 64);
    return reinterpret_cast<uint32_t(__cdecl*)(float*)>(code)(out);
  };
  const struct {
    game::Site s;
    uintptr_t va;
    float scale;
  } sites[] = {{game::Site::kSpreadGun1, 0x8FD553, 200},      {game::Site::kSpreadGun2, 0x8FD561, 200},
               {game::Site::kSpreadGun3, 0x918B23, 200},      {game::Site::kSpreadGun4, 0x918B31, 200},
               {game::Site::kSpreadAuto1, 0x920322, 200},     {game::Site::kSpreadAuto2, 0x920333, 200},
               {game::Site::kSpreadAuto3, 0x92050D, 200},     {game::Site::kSpreadAuto4, 0x92051E, 200},
               {game::Site::kSpreadShotgun1, 0x92531A, 100},  {game::Site::kSpreadShotgun2, 0x92532B, 100},
               {game::Site::kSpreadMounted1, 0x72BE2C, 1000}, {game::Site::kSpreadMounted2, 0x72BE3A, 1000},
               {game::Site::kSpreadThrow, 0x739F45, 15}};
  const std::vector<uint8_t> zero = {0xDD, 0xD8, 0xD9, 0xEE, 0x90, 0x90};
  int ran = 0;
  for (const auto& t : sites) {
    mem::Patch* p = game::patch(t.s);
    if (!p) continue;
    CHECK(p->at == t.va + rel && p->replacement == zero, "%s at +%X, expected +%X", game::site_name(t.s),
          static_cast<unsigned>(p->at - base), static_cast<unsigned>(t.va - 0x400000));
    float out = -1;
    const uint32_t moved = run6(p->original.data(), &out);
    CHECK(out == t.scale && !moved, "%s: the game's fmul gives %g (stack moved %X), expected %g", game::site_name(t.s),
          out, moved, t.scale);
    out = -1;
    const uint32_t moved_p = run6(p->replacement.data(), &out);
    CHECK(out == 0 && !moved_p, "%s: patched gives %g (stack moved %X)", game::site_name(t.s), out, moved_p);
    CHECK(p->apply() && !std::memcmp(reinterpret_cast<const void*>(p->at), zero.data(), 6) && p->revert() &&
              !std::memcmp(reinterpret_cast<const void*>(p->at), p->original.data(), 6),
          "%s: the patch did not go in and come out", game::site_name(t.s));
    ++ran;
  }
  std::printf("spread run: %d sites, each the game's scale (x200 a handgun's or machine gun's, x100 a shotgun's centre "
              "pellet, x1000 the mounted gun, x15 a throw) unpatched and 0 patched\n", ran);
}

// Steady aim's hold. Each site turns a lookup of the gun's or the hand's part
// into mov eax, a held copy. Run: the game's own laser line (FUN_0073dc60,
// thiscall(weapon, start*, end*)) for a handgun (weapon 2) - unpatched it takes
// the weapon model's part 0, patched the held copy; the player's routine the
// hold keys on (its aim and fire steps, in the exe and read off a stand-in
// player); and the hold's decisions (aimhold.h) over poses: moving, raising the
// gun, coming to rest, a burst and the gun's return, a quick shot, a shot after
// a jolt, shots as the gun comes up or the aim moves, breathing.
static void run_hold(uintptr_t base) {
  const uintptr_t rel = base - 0x400000;
  const struct {
    game::Site s;
    uintptr_t va;
    size_t len;
  } sites[] = {{game::Site::kHoldLaser, 0x73E1A9, 6},  {game::Site::kHoldHand1, 0x8FD511, 9},
               {game::Site::kHoldHand2, 0x918AE1, 9},  {game::Site::kHoldAuto1, 0x9202D5, 9},
               {game::Site::kHoldAuto2, 0x9204C0, 9},  {game::Site::kHoldShotgun, 0x9251A0, 9}};
  uint32_t to_gun = 0, to_hand = 0;
  bool hands_agree = true;
  for (const auto& t : sites) {
    const mem::Patch* p = game::patch(t.s);
    if (!p) return;
    bool mov = p->at == t.va + rel && p->replacement.size() == t.len && p->replacement[0] == 0xB8;
    for (size_t i = 5; mov && i < t.len; ++i) mov = p->replacement[i] == 0x90;
    CHECK(mov, "%s at +%X: not mov eax,held; nop...", game::site_name(t.s), static_cast<unsigned>(p->at - base));
    uint32_t to = 0;
    if (mov) std::memcpy(&to, &p->replacement[1], 4);
    if (t.s == game::Site::kHoldLaser) to_gun = to;
    else if (!to_hand) to_hand = to;
    else hands_agree = hands_agree && to == to_hand;
  }
  CHECK(to_gun && to_hand && hands_agree && to_gun != to_hand,
        "the hold sites do not point at the two held copies (the gun's, and one hand's for every shot)");
  CHECK(game::aim_hold_known(), "the hold is not known (the player's slot)");

  // The laser's line: start = part * the weapon's muzzle (a table the function
  // fills on its first call), end = start + the part's turn * (-50000, 0, 0).
  using Line = void(__attribute__((thiscall))*)(void*, float*, float*);
  const auto line = reinterpret_cast<Line>(0x73DC60 + rel);
  const uintptr_t pg = *reinterpret_cast<uint32_t*>(0xC06F3C + rel);
  auto* weapon_no = reinterpret_cast<uint8_t*>(pg + 0x4FC0);
  const uint8_t weapon_was = *weapon_no;
  *weapon_no = 2;
  // A model with its parts in an array (+4 has 0x2000, +F4 the first part).
  static uint8_t model[0x800], parts[0x1D8 * 2];
  *reinterpret_cast<uint32_t*>(model + 4) = 0x2000;
  *reinterpret_cast<void**>(model + 0xF4) = parts;
  // Part 0 turned 30 degrees about y and moved; the held copy turned -45 about z and moved elsewhere.
  const float c30 = std::cos(0.5235988f), s30 = std::sin(0.5235988f), c45 = std::cos(-0.7853982f),
              s45 = std::sin(-0.7853982f);
  const float live[12] = {c30, 0, s30, 100, 0, 1, 0, 200, -s30, 0, c30, 300};
  const float held[12] = {c45, -s45, 0, -50, s45, c45, 0, 75, 0, 0, 1, 1000};
  std::memcpy(parts + 0xC, live, sizeof(live));
  float start[3], end[3];
  auto expect = [&](const float* m, const char* what) {
    const float* muzzle = reinterpret_cast<const float*>(0xC79550 + rel + 2 * 0xC);
    float s[3], e[3];
    for (int r = 0; r < 3; ++r) {
      s[r] = m[4 * r] * muzzle[0] + m[4 * r + 1] * muzzle[1] + m[4 * r + 2] * muzzle[2] + m[4 * r + 3];
      e[r] = s[r] + m[4 * r] * -50000.0f;
    }
    float worst = 0;
    for (int r = 0; r < 3; ++r)
      worst = std::fmax(worst, std::fmax(std::fabs(start[r] - s[r]), std::fabs(end[r] - e[r])));
    CHECK(worst < 0.05f, "the laser's line from %s is off by %g (start %g %g %g, end %g %g %g)", what, worst, start[0],
          start[1], start[2], end[0], end[1], end[2]);
  };
  line(model, start, end);
  expect(live, "the weapon's part");
  mem::Patch* laser = game::patch(game::Site::kHoldLaser);
  game::set_held_pose(held, held);
  CHECK(laser->apply(), "the laser's hold site did not go in");
  line(model, start, end);
  expect(held, "the held copy");
  CHECK(laser->revert(), "the laser's hold site did not come out");
  line(model, start, end);
  expect(live, "the weapon's part, again");
  *weapon_no = weapon_was;

  // What the hold keys on: where the player is in its weapon's routine. r_no_0
  // (+FC) 0 dispatches by r_no_1 (+FD) through 0xC1BCD0 (FUN_007632e0), whose
  // entry 6 is the weapon's routine (FUN_00763ff0: a call through 0xC7A7CC, the
  // routine set the weapon put there); in each set whose shots read the hand,
  // step 1 (the aim) draws the laser (FUN_0067de90) and goes to step 2 itself
  // (mov [esi+FC],20600: r_no_2 2, r_no_3 0) for a shot, and step 2's first
  // sub-step is the fire routine with the hold site in it.
  auto u32 = [rel](uintptr_t va) { return *reinterpret_cast<const uint32_t*>(va + rel); };
  auto calls = [rel, &u32](uintptr_t at, uintptr_t fn) {  // E8 rel32 at `at`, to fn's thunk or to fn (VAs as built)
    const uint8_t* c = reinterpret_cast<const uint8_t*>(at + rel);
    if (c[0] != 0xE8) return false;
    uintptr_t to = at + 5 + u32(at + 1);
    if (*reinterpret_cast<const uint8_t*>(to + rel) == 0xE9) to = to + 5 + u32(to + 1);
    return to == fn;
  };
  CHECK(*reinterpret_cast<const uint32_t*>(0x7632F8 + rel + 3) == 0xFD && u32(0x763307 + 3) == 0xC1BCD0 + rel &&
            u32(0xC1BCD0 + 6 * 4) == 0x763FF0 + rel && u32(0x76402C + 2) == 0xC7A7CC + rel,
        "the player's routine 0 does not reach the weapon's routine through r_no_1 6");
  static const uint8_t kToFire[] = {0xC7, 0x86, 0xFC, 0x00, 0x00, 0x00, 0x00, 0x06, 0x02, 0x00};
  const struct {
    const char* what;
    uintptr_t steps, aim, laser, to_fire, subs, fire;
  } sets[] = {{"the handguns'", 0xC288CC, 0x8FDFB0, 0x8FDFDB, 0x8FE054, 0xC288C0, 0x8FD410},
              {"the Red9 and Matilda's", 0xC2A4C0, 0x919570, 0x9195B9, 0x919612, 0xC2A4B4, 0x9189B0},
              {"the machine guns'", 0xC2A7C0, 0x9212C0, 0x9212EB, 0x921368, 0xC2A7B4, 0x920290},
              {"the shotguns'", 0xC2A9B8, 0x925E30, 0x925E5B, 0x925EC3, 0xC2A9AC, 0x925040}};
  for (const auto& s : sets)
    CHECK(u32(s.steps + 4) == s.aim + rel && calls(s.laser, 0x67DE90) &&
              std::memcmp(reinterpret_cast<const void*>(s.to_fire + rel), kToFire, sizeof(kToFire)) == 0 &&
              u32(s.subs) == s.fire + rel,
          "%s routine: step 1 is not the aim that draws the laser and goes to step 2, or step 2 does not fire",
          s.what);

  // aim_pose on a stand-in player: its cPlWep (+7D8), its weapon object (+34),
  // both models with their parts in an array, the routine bytes at +FC.
  auto* player_slot = reinterpret_cast<uint32_t*>(0xC57054 + rel);
  const uint32_t slot_was = *player_slot;
  alignas(4) static uint8_t player[0x1000], plwep[0x50], wep[0x800], pl_parts[0x1D8 * 11], wep_parts[0x1D8];
  *reinterpret_cast<void**>(player + 0x7D8) = plwep;
  *reinterpret_cast<void**>(plwep + 0x34) = wep;
  *reinterpret_cast<uint32_t*>(player + 4) = 0x2000;
  *reinterpret_cast<void**>(player + 0xF4) = pl_parts;
  *reinterpret_cast<uint32_t*>(wep + 4) = 0x2000;
  *reinterpret_cast<void**>(wep + 0xF4) = wep_parts;
  std::memcpy(wep_parts + 0xC, live, sizeof(live));
  std::memcpy(pl_parts + 10 * 0x1D8 + 0xC, held, sizeof(held));
  *reinterpret_cast<float*>(plwep + 0x28) = 0.25f;
  *reinterpret_cast<float*>(plwep + 0x2C) = -0.5f;
  *reinterpret_cast<float*>(player + 0x448) = -0.5f;
  *reinterpret_cast<float*>(player + 0xA4) = 1.5f;
  *reinterpret_cast<float*>(player + 0x98) = 1000.0f;
  *player_slot = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(player));
  *weapon_no = 2;
  auto aim_at = [&](uint8_t r0, uint8_t r1, uint8_t r2) {
    player[0xFC] = r0, player[0xFD] = r1, player[0xFE] = r2, player[0xFF] = 1;
    game::AimPose p;
    CHECK(game::aim_pose(&p), "aim_pose: no pose from the stand-in player");
    return p;
  };
  const game::AimPose read = aim_at(0, 6, 1);
  CHECK(read.ok && read.step == game::AimStep::kAim && read.weapon == reinterpret_cast<uintptr_t>(wep) &&
            read.weapon_no == 2 && read.centre[0] == 0.25f && read.centre[1] == -0.5f && read.aim_lr == -0.5f &&
            read.yaw == 1.5f && read.pos[1] == 1000.0f && std::memcmp(read.gun, live, sizeof(live)) == 0 &&
            std::memcmp(read.hand, held, sizeof(held)) == 0,
        "aim_pose: the stand-in player read wrong");
  CHECK(aim_at(0, 6, 2).step == game::AimStep::kFire && aim_at(0, 6, 0).step == game::AimStep::kNone &&
            aim_at(0, 6, 3).step == game::AimStep::kNone && aim_at(0, 5, 1).step == game::AimStep::kNone &&
            aim_at(1, 6, 1).step == game::AimStep::kNone,
        "aim_pose: the routine bytes' step read wrong");
  *player_slot = slot_was;
  *weapon_no = weapon_was;

  // The hold's decisions. A pose: the aim's centre, the gun turned `deg` about
  // z, the routine's step.
  using game::AimStep;
  auto pose = [](float centre, float deg, AimStep step) {
    game::AimPose p;
    p.ok = true;
    p.weapon = 0x1234;
    p.weapon_no = 2;
    p.step = step;
    p.centre[0] = centre;
    const float a = deg * 0.017453292f;
    const float m[12] = {std::cos(a), -std::sin(a), 0, 0, std::sin(a), std::cos(a), 0, 0, 0, 0, 1, 0};
    std::memcpy(p.gun, m, sizeof(m));
    std::memcpy(p.hand, m, sizeof(m));
    return p;
  };
  using aimhold::Act;
  constexpr AimStep kAim = AimStep::kAim, kFire = AimStep::kFire, kNone = AimStep::kNone;
  constexpr int kSettle = aimhold::Hold::kSettle;
  aimhold::Hold h;
  int holds = 0, releases = 0;
  auto run = [&](const game::AimPose& p) {
    const Act a = h.step(p);
    holds += a == Act::kHold;
    releases += a == Act::kRelease;
    return a;
  };
  auto held_deg = [&] { return std::atan2(h.held().gun[4], h.held().gun[0]) / 0.017453292f; };
  for (int i = 0; i < 8; ++i) run(pose(0.01f * i, 0, kAim));  // moving the aim
  CHECK(!holds && !h.holding(), "held while the aim was moving");
  for (int i = 0; i < 10; ++i) run(pose(0.5f, 20.0f - 2.0f * i, kAim));  // the gun still coming up
  CHECK(!holds, "held while the gun was still turning 2 degrees a frame");
  for (int i = 0; i < kSettle; ++i) run(pose(0.5f, 0, kAim));  // at rest: the first frame there still moved
  CHECK(!holds, "held before the gun had been still for %d frames", kSettle);
  run(pose(0.5f, 0, kAim));
  CHECK(holds == 1 && h.holding() && std::fabs(held_deg()) < 1e-4f, "not held once the gun came to rest (%d holds)",
        holds);
  // A burst (a machine gun's: the fire step throughout, its motion starting over
  // at every shot), the gun blending back as the aim returns, and at rest again.
  for (int i = 0; i < 12; ++i) run(pose(0.5f, (i % 3) ? 6.0f - 2.0f * (i % 3) : 1.0f, kFire));
  for (float d : {3.0f, 1.5f, 0.75f, 0.3f, 0.1f}) run(pose(0.5f, d, kAim));
  for (int i = 0; i <= kSettle; ++i) run(pose(0.5f, 0, kAim));
  CHECK(holds == 1 && !releases && h.holding() && std::fabs(held_deg()) < 1e-4f,
        "the hold did not stay as it was through the burst and the gun's return (%d holds, held at %g degrees)", holds,
        held_deg());
  run(pose(0.6f, 0, kAim));  // the aim moves
  CHECK(releases == 1 && !h.holding(), "not let go when the aim moved");
  // A quick shot: the gun slowing as it comes to rest, then the fire step - the
  // tick sees the aim's last frame there, calm, and holds it.
  run(pose(0.7f, 1.0f, kAim));
  run(pose(0.7f, 0.8f, kAim));
  run(pose(0.7f, 0.7f, kFire));
  CHECK(holds == 2 && h.holding() && std::fabs(held_deg() - 0.7f) < 1e-3f,
        "a quick shot did not hold the aim's last pose (held at %g degrees)", held_deg());
  run(pose(0.7f, 6.0f, kFire));  // the shot's recoil
  // Back in the aim, the gun comes to rest 0.3 degrees on: held there.
  for (int i = 0; i <= kSettle; ++i) run(pose(0.7f, 1.0f, kAim));
  CHECK(holds == 3 && releases == 1 && h.holding() && std::fabs(held_deg() - 1.0f) < 1e-3f,
        "not held again where the gun came to rest (held at %g degrees)", held_deg());
  // The fire step's first tick, not calm: the aim's last calm pose.
  run(pose(0.9f, 0, kAim));  // the aim moves: let go
  run(pose(0.9f, 0.1f, kAim));
  run(pose(0.9f, 3.0f, kAim));
  run(pose(0.9f, 6.0f, kFire));
  CHECK(holds == 4 && h.holding() && std::fabs(held_deg() - 0.1f) < 1e-3f,
        "a shot after a jolt did not hold the aim's last calm pose (held at %g degrees)", held_deg());
  // Fired as the gun came up (no calm frame), and fired as the aim moved: nothing to hold.
  run(pose(1.0f, 0, kNone));
  CHECK(!h.holding() && releases == 3, "not let go when the aim ended");
  for (int i = 0; i < 4; ++i) run(pose(1.0f, 20.0f - 4.0f * i, kAim));
  run(pose(1.0f, 4.0f, kFire));
  run(pose(1.1f, 0, kAim));
  run(pose(1.1f, 0, kAim));
  run(pose(1.2f, 0, kFire));
  run(pose(1.2f, 0, kFire));
  CHECK(holds == 4 && !h.holding(), "held a shot fired as the gun came up or as the aim moved");
  // Fired straight from no aim (the tick never saw the aim step): nothing.
  run(pose(1.2f, 0, kNone));
  run(pose(1.2f, 0, kFire));
  CHECK(holds == 4 && !h.holding(), "held a shot whose aim the tick never saw");
  // Breathing (calm, never still): not held until a shot, then the shot's.
  for (int i = 0; i < 12; ++i) run(pose(1.3f, 0.05f * (i & 3), kAim));
  CHECK(holds == 4 && !h.holding(), "held a gun that never came to rest");
  run(pose(1.3f, 0.05f, kFire));
  for (int i = 0; i < 12; ++i) run(pose(1.3f, 0.05f * (i & 3), kAim));
  CHECK(holds == 5 && h.holding() && std::fabs(held_deg() - 0.05f) < 1e-3f,
        "a breathing aim's shot was not held, or the hold followed the breathing (held at %g degrees)", held_deg());
  game::AimPose none;
  CHECK(run(none) == Act::kRelease && !h.holding(), "not let go without a player");
  std::printf("hold run: the laser's line comes from the weapon's part, and from the held copy with the hold in; "
              "the routine's aim (step 1, the laser) and fire (step 2) read from the player; held once the gun "
              "rests, through a burst and the gun's return, from the aim's last frame for a quick shot; let go "
              "when the aim moves or ends\n");
}

// Steady aim's stance. Each aim's set-up starts its idle loop with attribute 4
// (loop); the sites make it 0xC (loop, the clock stopped). Run: the game's own
// motion clock (FUN_0065fd80, cdecl(MOTION_INFO*): the frame += delta * speed,
// wrapping a loop) on a stand-in 60-frame loop at 30 fps, with 4 and with 0xC;
// then the stance's tick on a stand-in player whose loop is held and blended
// with the aim's helper's motion B, between the game moving both clocks.
static void run_stance(uintptr_t base) {
  const uintptr_t rel = base - 0x400000;
  const struct {
    game::Site s;
    uintptr_t va;
    int helper;  // the helper's operand in the set-up
  } sites[] = {{game::Site::kAimLoopHandgun, 0x8FD3A8, 33}, {game::Site::kAimLoopWesker, 0x8FEF88, 33},
               {game::Site::kAimLoopBow, 0x9128C8, 33},     {game::Site::kAimLoopLaser, 0x905C67, 27},
               {game::Site::kAimLoopBowgun, 0x906FB8, 27},  {game::Site::kAimLoopGrenade, 0x915885, 27},
               {game::Site::kAimLoopRed9, 0x918935, 27},    {game::Site::kAimLoopMine, 0x91C1BE, 27},
               {game::Site::kAimLoopRocket, 0x91D8D5, 27},  {game::Site::kAimLoopShotgun, 0x924FD8, 30},
               {game::Site::kAimLoopAuto, 0x92021C, 38}};
  uintptr_t helper = 0;
  for (const auto& t : sites) {
    const mem::Patch* p = game::patch(t.s);
    if (!p) return;
    CHECK(p->at == t.va + rel && p->original == std::vector<uint8_t>({0x6A, 0x04}) &&
              p->replacement == std::vector<uint8_t>({0x6A, 0x0C}),
          "%s at +%X: not push 4 -> push C", game::site_name(t.s), static_cast<unsigned>(p->at - base));
    const uintptr_t h = *reinterpret_cast<const uint32_t*>(t.va + rel + t.helper);
    CHECK(!helper || h == helper, "%s: another blend helper (%X)", game::site_name(t.s), static_cast<unsigned>(h));
    if (!helper) helper = h;
  }
  CHECK(helper == 0xC7A7E8 + rel, "the blend helper at +%X, expected +87A7E8", static_cast<unsigned>(helper - base));
  CHECK(game::aim_stance_known(), "the stance is not known (the blend helper, the player's slot)");

  // A MOTION_INFO: +40 attribute, +AC..+BB the clock (+AC the frame in 10.6
  // fixed point, +B8 as a float), +BC the frames in it, +C0 the speed.
  using Clock = uint16_t(__cdecl*)(void*);
  const auto clock = reinterpret_cast<Clock>(0x65FD80 + rel);
  const uintptr_t pg = *reinterpret_cast<uint32_t*>(0xC06F3C + rel);
  auto* delta = reinterpret_cast<float*>(pg + 0x70);
  *delta = 1.0f;
  auto motion = [](uint8_t* m, uint16_t attr, float frame) {
    std::memset(m, 0, 0xD0);
    *reinterpret_cast<uint16_t*>(m + 0x40) = attr;
    *reinterpret_cast<int32_t*>(m + 0xAC) = static_cast<int32_t>(frame * 64);
    *reinterpret_cast<float*>(m + 0xB8) = frame;
    *reinterpret_cast<uint16_t*>(m + 0xBC) = 60;
    *reinterpret_cast<float*>(m + 0xC0) = 1.0f;
  };
  auto frame_of = [](const uint8_t* m) { return *reinterpret_cast<const float*>(m + 0xB8); };
  auto fixed_of = [](const uint8_t* m) { return *reinterpret_cast<const uint16_t*>(m + 0xAC); };
  alignas(4) static uint8_t loop[0xD0];
  motion(loop, 4, 0);
  int wraps = 0;
  for (int i = 0; i < 90; ++i) wraps += clock(loop) == 1;
  CHECK(frame_of(loop) == 30 && fixed_of(loop) == 30 * 64 && wraps == 1,
        "the game's clock, a loop: frame %g (%u/64) after 90 frames, %d wrap(s); expected 30, one", frame_of(loop),
        fixed_of(loop), wraps);
  motion(loop, 0xC, 0);
  for (int i = 0; i < 90; ++i) clock(loop);
  CHECK(frame_of(loop) == 0 && fixed_of(loop) == 0, "the game's clock, a loop stopped: frame %g after 90 frames",
        frame_of(loop));

  // The player (cPlayer: its motion at +1D8, the motion blended with it at +2A8)
  // with its aim's loop held at frame 0, blended with the helper's B (+18).
  auto* player_slot = reinterpret_cast<uint32_t*>(0xC57054 + rel);
  const uint32_t slot_was = *player_slot;
  alignas(4) static uint8_t player[0x1000];
  *player_slot = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(player));
  uint8_t* main_motion = player + 0x1D8;
  auto* b = reinterpret_cast<uint8_t*>(helper + 0x18);
  *reinterpret_cast<uint8_t**>(player + 0x2A8) = b;
  motion(main_motion, 0xC, 0);
  motion(b, 4, 0);
  // Frames: the tick, then the game moves both clocks (as FUN_00662b90 does).
  float b_seen = 0;
  for (int i = 0; i < 20; ++i) {
    game::keep_stance();
    b_seen = std::fmax(b_seen, frame_of(b));
    clock(main_motion);
    clock(b);
  }
  CHECK(b_seen == 0 && frame_of(main_motion) == 0 && frame_of(b) == 1,
        "held: B was at frame %g when drawn (the loop at %g), expected 0", b_seen, frame_of(main_motion));
  // Not held (the loop runs), or blended with another motion: B is the game's.
  motion(main_motion, 4, 0);
  motion(b, 4, 5);
  game::keep_stance();
  CHECK(frame_of(b) == 5, "a loop not held: B moved to %g", frame_of(b));
  motion(main_motion, 0xC, 0);
  alignas(4) static uint8_t other[0xD0];
  *reinterpret_cast<uint8_t**>(player + 0x2A8) = other;
  game::keep_stance();
  CHECK(frame_of(b) == 5, "another blend: B moved to %g", frame_of(b));
  // The switch off: the held loop runs on from its frame, B with it.
  *reinterpret_cast<uint8_t**>(player + 0x2A8) = b;
  game::let_stance_go();
  CHECK(*reinterpret_cast<uint16_t*>(main_motion + 0x40) == 4 && frame_of(b) == 0,
        "let go: attribute %X, B at %g (expected 4, 0)", *reinterpret_cast<uint16_t*>(main_motion + 0x40), frame_of(b));
  clock(main_motion);
  clock(b);
  CHECK(frame_of(main_motion) == 1 && frame_of(b) == 1, "let go: the loop at %g, B at %g after a frame (expected 1)",
        frame_of(main_motion), frame_of(b));
  std::memset(b, 0, 0xD0);
  *player_slot = slot_was;
  *delta = 0;
  std::printf("stance run: 11 aims' loops (push 4 -> push C); the game's clock runs a loop 30 frames on in 90 and a "
              "stopped one none; held, B is put back on the loop's frame each tick; let go, both run on\n");
}

// The shooting gallery, at the game's side: discovery's slots (build 10112090's,
// at this base); the game's own scoring (FUN_008431c0) on a stand-in gallery
// work - its popup of the points (FUN_00841ab0) finds no free slot of its eight
// and draws nothing; and gallery() and gallery_hit() over a stand-in enemy pool.
static void run_gallery(uintptr_t base) {
  const uintptr_t rel = base - 0x400000;
  CHECK(game::gallery_known(), "the shooting gallery is not known");
  if (!game::gallery_known()) return;
  auto u32 = [&](uintptr_t va) -> uint32_t& { return *reinterpret_cast<uint32_t*>(va + rel); };
  const uintptr_t pg = u32(0xC06F3C);
  if (!pg) {
    CHECK(false, "no GLOBAL_WK in the mapped image");
    return;
  }
  uint32_t& work_slot = u32(0xC7C524);
  const uint32_t work_was = work_slot;
  alignas(4) static uint8_t work[0x100];
  std::memset(work, 0, sizeof(work));
  for (int i = 0; i < 8; ++i) *reinterpret_cast<float*>(work + 0xDC + 4 * i) = 100.0f;  // the popups' slots, all busy
  work[0] = 2;
  *reinterpret_cast<float*>(pg + 0x70) = 0;  // the frame's delta: every popup slot stays busy
  work_slot = reinterpret_cast<uintptr_t>(work);
  using Score = void(__cdecl*)(int type, int part, const float* at, char kill, int frames);
  const auto score = reinterpret_cast<Score>(0x8431C0 + rel);
  const float at[3] = {};
  auto w32 = [&](int off) -> int32_t& { return *reinterpret_cast<int32_t*>(work + off); };
  // A head (part 1) of each kind, as a kill: type 0 100, type 1 200, type 5 100,
  // the bonus (3) 500 and no hit counted, Ashley (2, 4) -1000 and the streak gone.
  struct Case {
    int type, part, score, hits, streak;
  } cases[] = {{0, 1, 100, 1, 1}, {1, 1, 300, 2, 2}, {5, 1, 400, 3, 3}, {3, 1, 900, 3, 4}, {0, 0, 950, 4, 5},
               {2, 1, -50, 4, 0}, {1, 1, 150, 5, 1}, {4, 1, -850, 5, 0}, {0, 1, -750, 6, 1}, {14, 0, -750, 6, 0}};
  for (const Case& c : cases) {
    score(c.type, c.part, at, 1, 0);
    CHECK(w32(0x10) == c.score && w32(0xC) == c.hits && w32(0x24) == c.streak,
          "the game's scoring, type %d part %d: score %d, hits %d, streak %d (expected %d, %d, %d)", c.type, c.part,
          w32(0x10), w32(0xC), w32(0x24), c.score, c.hits, c.streak);
  }
  CHECK(work[0x46] == 1 && work[0x48] == 1 && work[0x47] == 1, "the game's scoring: bonus %d, Ashley %d, body %d",
        work[0x46], work[0x48], work[0x47]);

  // gallery() and gallery_hit(): the room, the main loop, the pool and its list.
  uint32_t& count_slot = u32(0xBFDB0C);
  uint32_t& stride_slot = u32(0xBFDB10);
  uint32_t& base_slot = u32(0xBFDB08);
  uint32_t& list_slot = u32(0xBFDB18);
  const uint32_t pool_was[4] = {count_slot, stride_slot, base_slot, list_slot};
  constexpr int kStride = 0x7E0, kN = 6;
  static uint8_t pool[kN * kStride];
  std::memset(pool, 0, sizeof(pool));
  auto em = [&](int i) { return pool + i * kStride; };
  const int types[kN] = {0, 2, 14, 3, 1, 9};
  for (int i = 0; i < kN; ++i) {
    uint8_t* e = em(i);
    *reinterpret_cast<uint32_t*>(e + 4) = i == 5 ? 0x201 : 1;  // the last one dead
    *reinterpret_cast<uint8_t**>(e + 8) = i + 1 < kN ? em(i + 1) : nullptr;
    e[0x100] = 0x3E;
    e[0x101] = static_cast<uint8_t>(types[i]);
    *reinterpret_cast<int16_t*>(e + 0x324) = types[i] >= 10 ? 1000 : 1;
    e[0xFC] = static_cast<uint8_t>(i % 4);
    *reinterpret_cast<float*>(e + 0x4FC) = 10.0f * i;
    *reinterpret_cast<float*>(e + 0x94) = -5000.0f;
    for (int k = 0; k < 3; ++k) *reinterpret_cast<float*>(e + 0x344 + 0xC + 4 * k) = 100.0f * (k + 1);
  }
  count_slot = kN;
  stride_slot = kStride;
  base_slot = reinterpret_cast<uintptr_t>(pool);
  list_slot = reinterpret_cast<uintptr_t>(em(0));
  auto& rno = *reinterpret_cast<uint8_t*>(pg + 0x20);
  auto& room = *reinterpret_cast<uint16_t*>(pg + 0x4FAC);
  auto& f174 = *reinterpret_cast<uint32_t*>(pg + 0x174);
  const uint8_t rno_was = rno;
  const uint16_t room_was = room;
  const uint32_t f174_was = f174;
  game::Gallery g;
  rno = 3;
  room = 0x22B;
  CHECK(!game::gallery(&g), "gallery() outside the gallery's room");
  room = 0x22C;
  w32(0x1C) = 4;
  w32(0x20) = 1;
  f174 = 0x40000000;
  CHECK(game::gallery(&g) && g.n == 5 && g.state == 2 && g.game == 4 && g.script == 1 && g.streak == 0 &&
            g.score == -750 && g.bonus_phase,
        "gallery(): %d targets, state %d, game %d/%d, streak %d, score %d, bonus %d", g.n, g.state, g.game, g.script,
        g.streak, g.score, g.bonus_phase);
  for (int i = 0; i < g.n && i < 5; ++i)
    CHECK(g.t[i].em == reinterpret_cast<uintptr_t>(em(i)) && g.t[i].type == types[i] && g.t[i].routine == i % 4 &&
              g.t[i].age == 10.0f * i && g.t[i].x == -5000.0f && !g.t[i].pending,
          "gallery(): target %d", i);
  rno = 6;
  CHECK(game::gallery(&g) && g.n == 5, "gallery() paused in the gallery");
  CHECK(!game::gallery_hit(reinterpret_cast<uintptr_t>(em(0)), game::kGalleryHead), "gallery_hit() while paused");
  rno = 3;
  CHECK(!game::gallery_hit(reinterpret_cast<uintptr_t>(em(5)), game::kGalleryHead), "gallery_hit() on a dead one");
  CHECK(!game::gallery_hit(reinterpret_cast<uintptr_t>(em(0)) + 4, game::kGalleryHead), "gallery_hit() off the pool");
  *reinterpret_cast<uint8_t*>(pg + 0x4FC0) = 7;
  CHECK(game::gallery_hit(reinterpret_cast<uintptr_t>(em(0)), game::kGalleryHead), "gallery_hit() on a target");
  const uint8_t* e = em(0);
  CHECK(e[0x32C] == 1 && e[0x32D] == 10 && e[0x32E] == 7 &&
            *reinterpret_cast<uint8_t* const*>(e + 0x340) == e + 0x344 && *reinterpret_cast<const float*>(e + 0x33C) >= 0,
        "gallery_hit(): the record %X %X %X, part +%X", e[0x32C], e[0x32D], e[0x32E],
        static_cast<unsigned>(*reinterpret_cast<uint8_t* const*>(e + 0x340) - e));
  CHECK(!game::gallery_hit(reinterpret_cast<uintptr_t>(em(0)), game::kGalleryHead), "gallery_hit() on a hit pending");
  CHECK(game::gallery(&g) && g.t[0].pending, "gallery(): the hit pending");
  // A link out of the pool: nothing from the list.
  *reinterpret_cast<uint8_t**>(em(2) + 8) = em(2) + 1;
  CHECK(!game::gallery(&g) && g.n == 0, "gallery() with a list out of the pool");
  *reinterpret_cast<uint8_t*>(pg + 0x4FC0) = 0;
  rno = rno_was;
  room = room_was;
  f174 = f174_was;
  count_slot = pool_was[0];
  stride_slot = pool_was[1];
  base_slot = pool_was[2];
  list_slot = pool_was[3];
  work_slot = work_was;
  std::printf("gallery run: the game's scoring (heads 100/200/100, the bonus 500, Ashley -1000, a prop ends the "
              "streak); gallery() and gallery_hit() over a stand-in pool\n");
}

// The shooting gallery played through: each of its eight scripts (four games, two
// versions each) read out of the exe and run under the gallery's rules as the
// code was read - the script's clock and its fast-forward over empty stretches
// (FUN_00844a10), the targets' commands (cEmMark's routines), a hit's record up
// for ten frames (FUN_005aefa0), the scoring and the streak (FUN_008431c0), the
// bonus target's task (FUN_008443b0), the props sliding out (FUN_00844610) and
// their effects (FUN_00845240) - with gallery.h choosing every hit. With the props
// of a gallery offering A or A-B (they give nothing) and of one offering C or D
// (the lights' prop's 300), with the targets moving before the gallery's loop in a
// frame and after it, and the bonus task first running in the frame it is made
// and in the next; at 30 fps and at 60. At 60 the frame's delta is 0.5: the
// script's clock, the targets' commands, a hit's record and the timers in the
// gallery's work go by the delta, while the bonus task's 30 and 15 frames, the
// props' slide (100 a frame) and a target's going down count frames, and take
// half the time. Each must come to the most the script gives - every scoring
// target's head, six bonus targets, and the props' 300 where there is one - with
// no target standing risen (or half way) for more than kMostWait before its hit.
namespace gsim {

constexpr float kMostWait = 4.0f;  // seconds

struct Cmd {
  int op = 4;  // 0 rise, 1 go down, 2 wait, 3 move, 4 stay
  int n = 0;   // wait: frames; move: speed
  float to[3] = {};
};
struct Entry {
  int t = 0, type = 0;
  float pos[3] = {};
  std::vector<Cmd> path;
};
struct Mark {
  int type = 0, hp = 0;
  float pos[3] = {};
  std::vector<Cmd> path;
  size_t pc = 0;
  int routine = 0, sub = 0;
  float timer = 0, age = 0;
  int dmg = 0, part = 0;
  float dmg_time = 0;  // the record's delta not yet a whole frame (+328)
  int up_at = -1;      // the frame it was first risen or half way
  bool live = true;
};

std::vector<Cmd> read_path(uintptr_t p) {
  std::vector<Cmd> out;
  auto i32 = [](uintptr_t a) { return *reinterpret_cast<const int32_t*>(a); };
  for (int guard = 0; guard < 64; ++guard) {
    Cmd c;
    c.op = i32(p);
    if (c.op == 2) c.n = i32(p + 4);
    if (c.op == 3) {
      for (int k = 0; k < 3; ++k) c.to[k] = static_cast<float>(i32(p + 4 + 4 * k));
      c.n = i32(p + 16);
    }
    if (c.op < 0 || c.op > 4) break;
    out.push_back(c);
    if (c.op == 1 || c.op == 4) break;
    p += c.op == 2 ? 8 : c.op == 3 ? 0x14 : 4;
  }
  return out;
}
Entry read_entry(uintptr_t e) {
  Entry x;
  x.t = *reinterpret_cast<const int32_t*>(e);
  x.type = *reinterpret_cast<const int32_t*>(e + 4);
  for (int k = 0; k < 3; ++k) x.pos[k] = static_cast<float>(*reinterpret_cast<const int32_t*>(e + 8 + 4 * k));
  if (x.type < 0xFC) x.path = read_path(e + 0x14);
  return x;
}

struct Result {
  int score = 0, hits = 0, bonuses = 0, ashley = 0, frames = 0, shots = 0, bad = 0;
  int longest = 0, longest_type = -1;  // the most frames a target stood up before its hit
  uint32_t flags = 0;
  bool ended = false;
};

struct Sim {
  int pts[8][5] = {};
  std::vector<Entry> script;
  int end = 0;
  Entry bonus;
  int game = 1, version = 1;
  bool lights = false, moves_first = true, task_now = false;
  float delta = 1.0f;  // the frame's delta (pG+70): 1 at 30 fps, 0.5 at 60
  int longest = 0, longest_type = -1;
  // The gallery's work, and pG+174's two bits.
  int state = 2, score = 0, hits = 0, streak = 0, bonuses = 0, ashley = 0, shots = 0, bad = 0;
  float clock = 0, pause = 0, hold = 0, cool = 0;
  uint32_t flags = 0;
  bool under_way = false, props_out = false;
  int task = 0, task_n = 0;
  size_t bonus_mark = 0;
  std::vector<Mark> marks;
  size_t prop_out = 0, prop_in = 0;  // the prop sliding to +5000, the one to -5000

  void add(int type, const float pos[3], const std::vector<Cmd>& path, int hp) {
    Mark m;
    m.type = type;
    m.hp = hp;
    for (int k = 0; k < 3; ++k) m.pos[k] = pos[k];
    m.path = path;
    m.routine = path.empty() ? 4 : path[0].op;
    marks.push_back(m);
  }
  bool trace = false;
  int frame = 0;
  void scored(int type, int part, bool kill) {
    if (state != 2) return;
    if (trace) std::printf("  %4d clock %5.0f: scored type %d part %d (streak %d, B %d)\n", frame, clock, type, part, streak, under_way);
    if (type > 9) {
      streak = 0;
      return;
    }
    if (type == 3) ++bonuses;
    if (type == 2) ++ashley;
    const int p = part == 4 ? 200 : pts[type][part];
    score += p;
    if (kill && p > 0 && type != 3) ++hits;
    streak = p > 0 ? streak + 1 : 0;
  }
  void effect(int type, int part) {
    if (state != 2 || cool > 0) return;
    int e = -1;
    if (type == 12 && part == 0x408) e = 0;
    if (type == 13) e = part == 0x408 ? 1 : part == 0x43C ? 2 : part == 0x470 ? 3 : -1;
    if (type == 14) e = part == 0x408 ? 5 : part == 0x43C ? 4 : part == 0x470 ? 6 : -1;
    if (type == 15) e = part == 0x408 ? 5 : part == 0x43C ? 4 : -1;
    if (e == 6) {
      flags |= 1;
    } else if (e == 5) {
      if ((flags & 1) && !(flags & 2)) {
        flags |= 2;
        cool = 450 + 1 - delta;
        score += 300;
      } else {
        flags |= 8;
      }
    } else {
      ++bad;  // an effect gallery.h has no business starting (4 is an explosion)
    }
  }
  void next(Mark& m) {
    if (m.path[m.pc].op != 4 && m.pc + 1 < m.path.size()) ++m.pc;
    m.routine = m.path[m.pc].op;
    m.sub = 0;
  }
  void move(Mark& m) {
    if (!m.live) return;
    if (m.dmg > 0 && (m.dmg_time += delta) >= 1) {  // a frame off the record for each whole one of delta
      --m.dmg;
      m.dmg_time -= 1;
    }
    if (m.hp > 0 && m.dmg > 0) {  // the hit check (FUN_00589c50)
      if (m.type > 9) {
        effect(m.type, m.part);
        scored(m.type, 0, true);
      } else if (--m.hp > 0) {
        scored(m.type, (m.part == game::kGalleryHead) + 2, false);
      } else {
        scored(m.type, m.part == game::kGalleryHead, true);
        m.routine = 1;
        m.sub = 0;
      }
    }
    switch (m.routine) {
      case 0:  // rise: ten frames' delta after the first frame
        if (m.sub == 0) {
          m.sub = 1;
          m.timer = 0;
        } else if ((m.timer += delta) >= 10) {
          next(m);
        }
        break;
      case 1:  // go down: the health goes at once, the target some frames later
        if (m.sub == 0) {
          if (trace && m.hp > 0) std::printf("  %4d clock %5.0f: ESCAPED type %d (streak %d, B %d)\n", frame, clock, m.type, streak, under_way);
          m.sub = m.hp > 0 ? 1 : 10;
          m.timer = 10;
          m.hp = 0;
        } else if (m.sub == 1) {
          if (--m.timer <= 0) m.sub = 10;
        } else if (m.sub == 10) {
          m.sub = 11;
          m.timer = 0;
        } else if (m.sub == 11) {
          if ((m.timer += 1) > 9) m.live = false;
          if (!m.live && trace) std::printf("  %4d gone: type %d\n", frame, m.type);
        }
        break;
      case 2:  // wait: N frames' delta (and the first frame's)
        if (m.sub == 0) {
          const int n = m.path[m.pc].n;
          m.timer = n > 1 ? n + 1 - delta : static_cast<float>(n);
          m.sub = 1;
        } else if ((m.timer -= delta) <= 0) {
          next(m);
        }
        break;
      case 3: {
        m.sub = 1;
        const Cmd& c = m.path[m.pc];
        float d[3], dd = 0;
        for (int k = 0; k < 3; ++k) {
          d[k] = c.to[k] - m.pos[k];
          dd += d[k] * d[k];
        }
        if (dd <= static_cast<float>(c.n) * c.n) {
          for (int k = 0; k < 3; ++k) m.pos[k] = c.to[k];
          next(m);
        } else {
          const float l = std::sqrt(dd);
          for (int k = 0; k < 3; ++k) m.pos[k] += d[k] / l * c.n * delta;
        }
        break;
      }
      default:
        break;
    }
    if (m.routine == 2 || m.routine == 3) m.age += delta;
  }
  // As game.cpp's target_life() works it out from the game's own commands.
  static int life(const Mark& m) {
    if (m.type >= 10) return -1;
    float frames = 0, pos[3] = {m.pos[0], m.pos[1], m.pos[2]};
    for (size_t k = m.pc; k < m.path.size(); ++k) {
      const Cmd& c = m.path[k];
      const bool now = k == m.pc;
      if (now && m.routine == 1) return 0;
      switch (c.op) {
        case 0: frames += now && m.sub ? 10 - m.timer : 11; break;
        case 1: return static_cast<int>(frames) + 1;
        case 2: frames += now && m.sub ? (m.timer > 0 ? m.timer : 0) : c.n + 1; break;
        case 3: {
          float d = 0;
          for (int q = 0; q < 3; ++q) d += (c.to[q] - pos[q]) * (c.to[q] - pos[q]);
          frames += std::sqrt(d) / c.n + 1;
          for (int q = 0; q < 3; ++q) pos[q] = c.to[q];
          break;
        }
        default: return -1;
      }
    }
    return -1;
  }
  int alive() const {
    int n = 0;
    for (const Mark& m : marks)
      if (m.live && m.hp > 0 && (m.type < 2 || m.type == 3)) ++n;
    return n;
  }
  void spawn(const Entry& e) {
    if (e.type == 0xFC) {
      for (Mark& m : marks)
        if (m.live && m.type == 4) {
          m.routine = 1;
          m.sub = 0;
        }
    } else if (e.type == 0xFD) {
      hold = 30 + 1 - delta;
    } else if (e.type == 0xFE) {
      pause = 120 + 1 - delta;
    } else {
      add(e.type, e.pos, e.path, 1);
      if (trace) std::printf("  %4d clock %5.0f: spawn type %d\n", frame, clock, e.type);
    }
  }
  void loop() {  // FUN_00844a10
    if (state != 2) return;
    if (!(hold > 0) && pause > 0) {
      if (!under_way) pause -= delta;
    } else {
      for (int guard = 0; guard < 100000; ++guard) {
        for (const Entry& e : script)
          if (clock < e.t + delta && e.t - delta < clock) spawn(e);
        if (end < clock) {
          if (!under_way) {
            state = 3;
            break;
          }
          hold = 1;
        }
        if (streak > 4 && state == 2) {
          streak = 0;
          if (!under_way) {
            task = 1;
            hold = 3 + 1 - delta;
          }
        }
        clock += delta;
        if (alive() || pause > 0 || hold > 0) break;
      }
    }
    if ((game == 2 && version == 2) || (game == 3 && version == 2) || game == 4) props_out = true;
    if (hold > 0) hold -= delta;
    if (cool > 0) cool -= delta;
  }
  // FUN_008443b0: 30 frames, its target until it is down, 15 frames - a frame at a
  // time whatever the delta (FUN_006c4e30(1)).
  void run_task() {
    switch (task) {
      case 1:
        under_way = true;
        task_n = 30;
        task = 2;
        [[fallthrough]];
      case 2:
        props_out = true;
        if (--task_n == 0) task = 3;
        return;
      case 3:
        if (trace) std::printf("  %4d clock %5.0f: bonus target\n", frame, clock);
        add(3, bonus.pos, bonus.path, 1);
        bonus_mark = marks.size() - 1;
        task = 4;
        [[fallthrough]];
      case 4: {
        const Mark& b = marks[bonus_mark];
        if (b.live && b.hp > 0) {
          props_out = true;
          return;
        }
        task_n = 15;
        task = 5;
        [[fallthrough]];
      }
      case 5:
        props_out = true;
        if (--task_n == 0) task = 6;
        return;
      case 6:
        if (trace) std::printf("  %4d clock %5.0f: bonus over\n", frame, clock);
        under_way = false;
        task = 0;
        return;
      default:
        return;
    }
  }
  void slide() {  // FUN_00844610: 100 a frame, whatever the delta
    float& o = marks[prop_out].pos[0];
    float& i = marks[prop_in].pos[0];
    if (props_out) {
      props_out = false;
      if (o < 5000) o += 100;
      if (-5000 < i) i -= 100;
    } else {
      if (o > 0) o -= 100;
      i = i < 0 ? i + 100 : 0;
    }
  }
  void tick(gallery::Auto& a) {
    game::Gallery g;
    g.state = state;
    g.game = game;
    g.script = version;
    g.score = score;
    g.hits = hits;
    g.streak = streak;
    g.flags = flags;
    g.cooldown = cool;
    g.bonus_phase = under_way;
    for (size_t k = 0; k < marks.size() && g.n < game::kMaxGalleryTargets; ++k) {
      const Mark& m = marks[k];
      if (!m.live) continue;
      game::GalleryTarget& t = g.t[g.n++];
      t.em = k + 1;
      t.type = m.type;
      t.hp = m.hp;
      t.routine = m.routine;
      t.pending = m.dmg > 0;
      t.age = m.age;
      t.x = m.pos[0];
      t.life = life(m);
      t.rise = m.routine != 0 ? 1.0f : m.sub ? m.timer / 10.0f : 0.0f;
    }
    // How long each scoring target (and bonus target) stands up, as gallery.h
    // sees it, before its hit.
    for (Mark& m : marks) {
      const bool up = m.routine == 2 || m.routine == 3 ||
                      (m.routine == 0 && m.sub && m.timer / 10.0f >= gallery::Auto::kHalfUp);
      if (m.live && m.hp > 0 && (m.type < 2 || m.type == 3 || m.type == 5) && up && m.up_at < 0) m.up_at = frame;
    }
    const gallery::Shot s = a.step(g);
    if (!s.em) return;
    Mark& m = marks[s.em - 1];
    if (m.up_at >= 0 && frame - m.up_at > longest) {
      longest = frame - m.up_at;
      longest_type = m.type;
    }
    // What gallery_hit() refuses; and what gallery.h must never ask for.
    if (!m.live || m.hp <= 0 || m.dmg > 0) {
      ++bad;
      return;
    }
    if (m.type == 2 || m.type == 4 || (m.type < 10 && s.part != game::kGalleryHead) ||
        (m.type >= 10 && !(m.type == 14 && (s.part == game::kGalleryPropLegs || s.part == game::kGalleryPropChest))))
      ++bad;
    m.dmg = 10;
    m.dmg_time = 0;
    m.part = s.part;
    ++shots;
  }
  Result play() {
    gallery::Auto a;
    const std::vector<Cmd> still = {Cmd{}};
    const float zero[3] = {};
    add(lights ? 14 : 12, zero, still, 1000);
    prop_in = marks.size() - 1;
    add(lights ? 15 : 13, zero, still, 1000);
    prop_out = marks.size() - 1;
    Result r;
    for (r.frames = 0; state == 2 && r.frames < 30000 / delta; ++r.frames) {
      frame = r.frames;
      tick(a);
      if (moves_first)
        for (size_t k = 0; k < marks.size(); ++k) move(marks[k]);
      const bool fresh = task == 0;
      loop();
      if (task_now || !fresh) run_task();
      slide();
      if (!moves_first)
        for (size_t k = 0; k < marks.size(); ++k) move(marks[k]);
    }
    r.ended = state == 3;
    r.score = score;
    r.hits = hits;
    r.bonuses = bonuses;
    r.ashley = ashley;
    r.shots = shots;
    r.bad = bad;
    r.longest = longest;
    r.longest_type = longest_type;
    r.flags = flags;
    return r;
  }
};

}  // namespace gsim

static void run_gallery_games(uintptr_t base) {
  const uintptr_t rel = base - 0x400000;
  struct Script {
    const char* name;
    int game, version;
    uintptr_t table;
  } scripts[] = {{"A1", 1, 1, 0xC20EA8}, {"A2", 1, 2, 0xC21448}, {"B1", 2, 1, 0xC21CF0}, {"B2", 2, 2, 0xC222F8},
                 {"C1", 3, 1, 0xC22C28}, {"C2", 3, 2, 0xC234B8}, {"D1", 4, 1, 0xC240B0}, {"D2", 4, 2, 0xC24C78}};
  // The game's start picks them (mov [work+4C], table: 0x84306F on), and the bonus
  // task spawns its target from 0xC25B8C (push at 0x8443F7).
  const uint32_t picks[8] = {0x84306F, 0x843078, 0x8430BA, 0x8430C3, 0x8430A0, 0x8430A9, 0x843086, 0x84308F};
  for (int i = 0; i < 8; ++i)
    CHECK(*reinterpret_cast<const uint32_t*>(picks[i] + rel + 3) == scripts[i].table + rel,
          "the gallery's start does not pick script %s at %X", scripts[i].name, scripts[i].table);
  CHECK(*reinterpret_cast<const uint32_t*>(0x8443F7 + rel + 1) == 0xC25B8C + rel, "the bonus target's entry");
  gsim::Sim proto;
  for (int t = 0; t < 8; ++t)
    for (int p = 0; p < 5; ++p) proto.pts[t][p] = *reinterpret_cast<const int32_t*>(0xB2BFA8 + rel + (t * 5 + p) * 4);
  proto.bonus = gsim::read_entry(0xC25B8C + rel);
  CHECK(proto.bonus.type == 3 && proto.pts[3][1] == 500, "the bonus target: type %d, %d points", proto.bonus.type,
        proto.pts[3][1]);
  int runs = 0, best[8] = {};
  for (int i = 0; i < 8; ++i) {
    const uintptr_t tab = scripts[i].table + rel;
    gsim::Sim s = proto;
    s.game = scripts[i].game;
    s.version = scripts[i].version;
    const int n = *reinterpret_cast<const int32_t*>(tab);
    s.end = *reinterpret_cast<const int32_t*>(tab + 4);
    int heads = 0, scoring = 0;
    for (int k = 0; k < n; ++k) {
      s.script.push_back(gsim::read_entry(*reinterpret_cast<const uint32_t*>(tab + 8 + 4 * k)));
      const int ty = s.script.back().type;
      if (ty == 0 || ty == 1 || ty == 5) {
        heads += s.pts[ty][1];
        ++scoring;
      }
    }
    CHECK(scoring == 25, "script %s: %d scoring targets", scripts[i].name, scoring);
    for (const float delta : {1.0f, 0.5f})
      for (int lights = 0; lights < 2; ++lights)
        for (int order = 0; order < 4; ++order) {
          gsim::Sim run = s;
          run.delta = delta;
          run.lights = lights;
          run.moves_first = order & 1;
          run.task_now = order & 2;
          run.trace = std::getenv("GSIM") && !std::strcmp(std::getenv("GSIM"), scripts[i].name) && lights && !order;
          const gsim::Result r = run.play();
          const int want = heads + 6 * s.pts[3][1] + (lights ? 300 : 0);
          const float fps = 30 / delta;
          const char* props = lights ? "props worth 300" : "props worth nothing";
          ++runs;
          CHECK(r.ended && r.score == want && r.hits == 25 && r.bonuses == 6 && !r.ashley && !r.bad,
                "script %s at %.0f fps (%s, order %d): score %d (want %d), hits %d, bonus targets %d, Ashley %d, wrong "
                "hits %d, %s after %d frames", scripts[i].name, fps, props, order, r.score, want, r.hits, r.bonuses,
                r.ashley, r.bad, r.ended ? "ended" : "still running", r.frames);
          CHECK(r.longest <= gsim::kMostWait * fps,
                "script %s at %.0f fps (%s, order %d): a target of type %d stood up %.1f s before its hit",
                scripts[i].name, fps, props, order, r.longest_type, r.longest / fps);
          if (lights && order == 0) {
            best[i] = r.score;
            std::printf("gallery %s at %.0f fps: %d targets (heads %d), %d shots, %d frames, a target up %.1f s at most "
                        "before its hit: %d points where the gallery offers A or B, %d where it offers C or D\n",
                        scripts[i].name, fps, scoring, heads, r.shots, r.frames, r.longest / fps, want - 300, r.score);
          }
        }
  }
  std::printf("gallery games: %d runs of the eight scripts; best %d %d %d %d %d %d %d %d\n", runs, best[0], best[1],
              best[2], best[3], best[4], best[5], best[6], best[7]);
}

// Endless combo, run: the Mercenaries' own scoring (FUN_0070f520, cdecl(kind,
// points)) and HUD (FUN_00710c10, cdecl(work)) on the image's own work (0xC710A8)
// at 30 fps and at 60, with the patches out, in and out again. The HUD draws
// through the ID system, whose lookup answers a unit it does not have with a
// dummy of its own (0xC599D0); its animations are made to have ended at every
// frame (+91 bits 0 and 1), so a combo's end and a bonus's showing each take the
// frame after they start. Unpatched, a lone kill's combo ends when its 300
// frames are out, and two kills' when their window is, the bonus (20) going onto
// the score after; patched, neither ends, and each kill's bonus goes onto the
// score as it comes (12 Ganados in a row: 3600 and 3770).
static void run_merc_combo(uintptr_t base) {
  const uintptr_t rel = base - 0x400000;
  mem::Patch* first = game::patch(game::Site::kMercFirst);
  mem::Patch* window = game::patch(game::Site::kMercWindow);
  mem::Patch* bonus = game::patch(game::Site::kMercBonus);
  CHECK(game::merc_known(), "the Mercenaries' work is not known");
  if (!first || !window || !bonus || !game::merc_known()) return;
  CHECK(first->at == 0x710C48 + rel && window->at == 0x710D43 + rel && bonus->at == 0x711183 + rel,
        "the combo's sites at +%X, +%X, +%X", static_cast<unsigned>(first->at - base),
        static_cast<unsigned>(window->at - base), static_cast<unsigned>(bonus->at - base));
  const uintptr_t pg = *reinterpret_cast<uint32_t*>(0xC06F3C + rel);
  static uint8_t pg_saved[0x8690];
  std::memcpy(pg_saved, reinterpret_cast<const void*>(pg), sizeof(pg_saved));
  uint8_t* const work = reinterpret_cast<uint8_t*>(0xC710A8 + rel);
  uint8_t* const unit = reinterpret_cast<uint8_t*>(0xC599D0 + rel);
  static uint8_t work_saved[0xC4], unit_saved[0x144];
  std::memcpy(work_saved, work, sizeof(work_saved));
  std::memcpy(unit_saved, unit, sizeof(unit_saved));
  *reinterpret_cast<uint32_t*>(pg + 0x54) |= 0x40000000;  // the Mercenaries
  *reinterpret_cast<uint8_t*>(pg + 0x20) = 3;             // their main loop
  float& delta = *reinterpret_cast<float*>(pg + 0x70);
  using Score = char(__cdecl*)(int kind, int points);
  using Hud = int(__cdecl*)(void* work);
  const auto score = reinterpret_cast<Score>(0x70F520 + rel);
  const auto hud = reinterpret_cast<Hud>(0x710C10 + rel);
  auto i32 = [&](int off) -> int32_t& { return *reinterpret_cast<int32_t*>(work + off); };
  auto frames = [&](int n) {
    for (int i = 0; i < n; ++i) {
      unit[0x91] |= 3;
      hud(work);
    }
  };
  auto fresh = [&](float d) {
    std::memset(work, 0, sizeof(work_saved));
    delta = d;
  };
  constexpr int kGanado = 0;
  const int fps30 = 300, fps60 = 600;  // the window, in frames
  // Unpatched: the combo runs out.
  fresh(1.0f);
  score(kGanado, 0);
  frames(fps30 - 1);
  CHECK(i32(0x24) == 300 && i32(0x28) == 1, "unpatched, a lone kill: score %d, combo %d - expected 300, 1", i32(0x24),
        i32(0x28));
  frames(1);
  CHECK(i32(0x28) == 0, "unpatched, a lone kill's combo is %d once its window is out - expected 0", i32(0x28));
  fresh(1.0f);
  score(kGanado, 0);
  score(kGanado, 0);
  CHECK(i32(0x28) == 2 && i32(0x48) == 20, "unpatched, two kills: combo %d, earned %d - expected 2, 20", i32(0x28),
        i32(0x48));
  frames(fps30 - 1);
  CHECK(i32(0x28) == 2 && i32(0x24) == 600, "unpatched, two kills a moment before the window is out: combo %d, score "
        "%d - expected 2, 600", i32(0x28), i32(0x24));
  frames(4);
  CHECK(i32(0x28) == 0 && i32(0x24) == 620 && i32(0x48) == 0, "unpatched, two kills after their window: combo %d, "
        "score %d, earned %d - expected 0, 620 (the bonus on), 0", i32(0x28), i32(0x24), i32(0x48));
  // Patched: neither timer runs out, and the bonus goes on as it is earned.
  CHECK(first->apply() && window->apply() && bonus->apply(), "Endless combo's patches did not go in");
  for (float d : {1.0f, 0.5f}) {
    const int fps = d == 1.0f ? 30 : 60, window_frames = d == 1.0f ? fps30 : fps60;
    fresh(d);
    score(kGanado, 0);
    frames(window_frames * 20);
    CHECK(i32(0x28) == 1, "patched at %d fps, a lone kill's combo is %d long after its window - expected 1", fps,
          i32(0x28));
    for (int k = 2; k <= 12; ++k) {
      score(kGanado, 0);
      frames(k == 12 ? window_frames * 20 : 3);
    }
    CHECK(i32(0x28) == 12 && i32(0x24) == 3600 + 3770 && i32(0x48) == 0 && i32(0x3C) == 12,
          "patched at %d fps, 12 kills in a row, the last long ago: combo %d, score %d, earned %d, kills %d - expected "
          "12, 7370, 0, 12", fps, i32(0x28), i32(0x24), i32(0x48), i32(0x3C));
  }
  game::Merc m;
  CHECK(game::merc(&m) && m.score == 7370 && m.combo == 12 && m.bonus == 0 && m.kills == 12,
        "merc(): score %d, combo %d, bonus %d, kills %d", m.score, m.combo, m.bonus, m.kills);
  CHECK(bonus->revert() && window->revert() && first->revert(), "Endless combo's patches did not come out");
  delta = 1.0f;
  frames(fps30 + 4);
  CHECK(i32(0x28) == 0 && i32(0x24) == 7370, "reverted, the combo is %d and the score %d after its window - expected 0, "
        "7370", i32(0x28), i32(0x24));

  // Bonus time always on. A Bonus Points pickup (FUN_0070f6c0(frames)) starts
  // the time (+54), the kills in it from 0 (+40) and its showing (+64 bit
  // 400000); every kill in it earns at least 1000; what is earned waits for the
  // time to be over as well as for the combo.
  mem::Patch* btime = game::patch(game::Site::kMercBonusTime);
  mem::Patch* bbank = game::patch(game::Site::kMercBonusBank);
  CHECK(btime && bbank, "Bonus time always on's sites not prepared");
  if (btime && bbank) {
    CHECK(btime->at == 0x710FC3 + rel && bbank->at == 0x711194 + rel, "Bonus time always on's sites at +%X, +%X",
          static_cast<unsigned>(btime->at - base), static_cast<unsigned>(bbank->at - base));
    const auto pickup = reinterpret_cast<char(__cdecl*)(int frames)>(0x70F6C0 + rel);
    auto f32 = [&](int off) -> float& { return *reinterpret_cast<float*>(work + off); };
    auto u32 = [&](int off) -> uint32_t& { return *reinterpret_cast<uint32_t*>(work + off); };
    // Unpatched: the time runs out, and the bonus goes on only after it.
    fresh(1.0f);
    pickup(900);
    CHECK(f32(0x54) == 900.0f && (u32(0x64) & 0x400000) && i32(0x40) == 0, "a pickup: time %.1f, flags %08X, kills "
          "in it %d - expected 900, 400000, 0", f32(0x54), u32(0x64), i32(0x40));
    score(kGanado, 0);
    score(kGanado, 0);
    CHECK(i32(0x48) == 2000 && i32(0x40) == 2, "unpatched, two kills in the Bonus Points time: earned %d, kills in it "
          "%d - expected 2000, 2", i32(0x48), i32(0x40));
    frames(fps30 + 4);
    CHECK(i32(0x28) == 0 && i32(0x24) == 600 && i32(0x48) == 2000, "unpatched, their combo over in the time: combo %d, "
          "score %d, earned %d - expected 0, 600, 2000 (waiting for the time)", i32(0x28), i32(0x24), i32(0x48));
    frames(900);
    CHECK(f32(0x54) == 0.0f && i32(0x24) == 2600 && i32(0x48) == 0, "unpatched, after the time: time %.1f, score %d, "
          "earned %d - expected 0, 2600, 0", f32(0x54), i32(0x24), i32(0x48));
    // Patched: started from merc_bonus_time() as a pickup, it never runs out,
    // and what is earned goes on once the combo is over - or as it comes, with
    // Endless combo.
    CHECK(btime->apply() && bbank->apply(), "Bonus time always on's patches did not go in");
    for (float d : {1.0f, 0.5f}) {
      const int fps = d == 1.0f ? 30 : 60, window_frames = d == 1.0f ? fps30 : fps60;
      fresh(d);
      i32(0x40) = 7;
      CHECK(game::merc_bonus_time() && f32(0x54) == 900.0f && (u32(0x64) & 0x400000) && i32(0x40) == 0,
            "merc_bonus_time() at %d fps: time %.1f, flags %08X, kills in it %d - expected 900, 400000, 0", fps,
            f32(0x54), u32(0x64), i32(0x40));
      frames(1);
      CHECK(!(u32(0x64) & 0x400000) && game::merc(&m) && m.bonus_time, "patched at %d fps, the time is not shown "
            "running (flags %08X)", fps, u32(0x64));
      score(kGanado, 0);
      score(kGanado, 0);
      frames(window_frames * 20);
      CHECK(f32(0x54) == 900.0f && i32(0x28) == 0 && i32(0x24) == 2600 && i32(0x48) == 0 && i32(0x40) == 2,
            "patched at %d fps, two kills, long after: time %.1f, combo %d, score %d, earned %d, kills in it %d - "
            "expected 900, 0, 2600, 0, 2", fps, f32(0x54), i32(0x28), i32(0x24), i32(0x48), i32(0x40));
      CHECK(game::merc(&m) && m.bonus_time && m.bonus_kills == 2, "merc() at %d fps: the time %d, kills in it %d", fps,
            m.bonus_time, m.bonus_kills);
    }
    CHECK(first->apply() && window->apply() && bonus->apply(), "Endless combo's patches did not go in with Bonus time's");
    fresh(0.5f);
    game::merc_bonus_time();
    for (int k = 1; k <= 12; ++k) {
      score(kGanado, 0);
      frames(fps60 * 3);
    }
    CHECK(i32(0x28) == 12 && i32(0x24) == 3600 + 12000 && i32(0x48) == 0 && f32(0x54) == 900.0f,
          "with Endless combo, 12 kills in the time, each long after the last: combo %d, score %d, earned %d, time "
          "%.1f - expected 12, 15600, 0, 900", i32(0x28), i32(0x24), i32(0x48), f32(0x54));
    for (mem::Patch* p : {bonus, window, first}) p->revert();
    // merc_bonus_time() on one running (put back at 900, its kills kept) and one ending (started again).
    fresh(1.0f);
    f32(0x54) = 500.0f;
    i32(0x40) = 3;
    CHECK(game::merc_bonus_time() && f32(0x54) == 900.0f && i32(0x40) == 3 && !(u32(0x64) & 0x400000),
          "merc_bonus_time() on one running: time %.1f, kills in it %d, flags %08X", f32(0x54), i32(0x40), u32(0x64));
    f32(0x54) = 0.5f;
    u32(0x64) = 0x200000;
    CHECK(game::merc_bonus_time() && f32(0x54) == 900.0f && i32(0x40) == 0 && u32(0x64) == 0x400000,
          "merc_bonus_time() on one ending: time %.1f, kills in it %d, flags %08X", f32(0x54), i32(0x40), u32(0x64));
    CHECK(bbank->revert() && btime->revert(), "Bonus time always on's patches did not come out");
    frames(fps30 * 4);
    CHECK(f32(0x54) == 0.0f, "reverted, the time is %.1f after 30 s - expected 0", f32(0x54));
  }
  *reinterpret_cast<uint32_t*>(pg + 0x54) &= ~0x40000000u;
  CHECK(!game::merc(&m), "merc() outside the Mercenaries");
  std::memcpy(work, work_saved, sizeof(work_saved));
  std::memcpy(unit, unit_saved, sizeof(unit_saved));
  std::memcpy(reinterpret_cast<void*>(pg), pg_saved, sizeof(pg_saved));
  std::printf("Mercenaries combo run: unpatched a combo ends when its window is out and its bonus goes on after; "
              "patched it never ends, at 30 and 60 fps, and each kill's bonus goes on as it comes; a Bonus Points "
              "time runs out and holds the bonus back till then, patched it never runs out and holds nothing back\n");
}

// Kill enemies as they appear, run: merc_enemies() and merc_kill() over a
// stand-in pool of Ganados (the image's own cEm10 vtable; their hit parts linked
// as their set-up links them: the head, +344, then the body, +834, part 2), and
// the Ganados' own hit check (em10DmCk, FUN_00457d30, eax = the enemy, as its move
// calls it) on each record merc_kill() wrote. The hit's damage (LifeDownSet2)
// takes the point merc_kill() left, and the hit check sends the hit to a
// handgun's reaction (FUN_00458b90), which scores the kill (FUN_0043a290 ->
// FUN_0070f520: by the type, 300 a Ganado, 10000 type 2, 5000 type 16, 7000 a
// Garrador - type A, whose body the hit's damage is cut to an eighth on - and 300
// type 6, which takes no damage and dies of any hit) and starts the death (+FC
// 0x203: routine 3, the death 2). The Mercenaries' flag up, their
// main loop, Normal, the dynamic difficulty at 5; in room 51B, where a death
// against a wall (FUN_00435780) is let be - its ray tests want a room loaded.
// Each stand-in is far from the player (+378), so the hit check leaves its notice
// of the player be. The hit's effects and sounds are stubbed as for El Gigante;
// GLOBAL_WK, the work, the pool's slots and the player's are put back.
static void run_merc_kill(uintptr_t base) {
  const uintptr_t rel = base - 0x400000;
  CHECK(game::merc_kills_known(), "the Ganados' class is not known");
  if (!game::merc_kills_known()) return;
  uint8_t* const thunks[] = {reinterpret_cast<uint8_t*>(0x406415 + rel), reinterpret_cast<uint8_t*>(0x40ADC1 + rel)};
  for (uint8_t* t : thunks)
    if (*t != 0xE9) {
      CHECK(false, "the hit's effects' or sound's thunk at +%X is no jmp",
            static_cast<unsigned>(reinterpret_cast<uintptr_t>(t) - base));
      return;
    }
  for (uint8_t* t : thunks) *t = 0xC3;
  auto u32 = [&](uintptr_t va) -> uint32_t& { return *reinterpret_cast<uint32_t*>(va + rel); };
  const uintptr_t pg = u32(0xC06F3C);
  static uint8_t pg_saved[0x8690];
  std::memcpy(pg_saved, reinterpret_cast<const void*>(pg), sizeof(pg_saved));
  uint8_t* const work = reinterpret_cast<uint8_t*>(0xC710A8 + rel);
  static uint8_t work_saved[0xC4];
  std::memcpy(work_saved, work, sizeof(work_saved));
  std::memset(work, 0, sizeof(work_saved));
  uint32_t& count_slot = u32(0xBFDB0C);
  uint32_t& stride_slot = u32(0xBFDB10);
  uint32_t& base_slot = u32(0xBFDB08);
  uint32_t& list_slot = u32(0xBFDB18);
  uint32_t& player_slot = u32(0xC57054);
  const uint32_t slots_was[5] = {count_slot, stride_slot, base_slot, list_slot, player_slot};
  *reinterpret_cast<uint32_t*>(pg + 0x54) |= 0x40000000;  // the Mercenaries
  *reinterpret_cast<uint8_t*>(pg + 0x20) = 3;             // their main loop
  *reinterpret_cast<float*>(pg + 0x70) = 1.0f;
  *reinterpret_cast<uint8_t*>(pg + 0x847C) = 5;  // Normal
  *reinterpret_cast<uint8_t*>(pg + 0x4F98) = 5;
  *reinterpret_cast<uint16_t*>(pg + 0x4FAC) = 0x51B;
  static uint8_t player[0x900];
  std::memset(player, 0, sizeof(player));
  player_slot = reinterpret_cast<uintptr_t>(player);
  // The pool: five Ganados about their business, one in a hit's reaction, one
  // scored already, one of another class, one dead.
  constexpr int kStride = 0xE08, kN = 9, kLive = 7;
  alignas(8) static uint8_t pool[kN * kStride];
  alignas(8) static uint8_t parts[0x1D8];  // a model's parts, for the effects' lookups (an array: +4 bit 0x2000)
  std::memset(pool, 0, sizeof(pool));
  std::memset(parts, 0, sizeof(parts));
  auto em = [&](int i) { return pool + i * kStride; };
  const uint32_t vtable = 0xB1035C + rel;
  const int types[kN] = {0, 2, 0x16, 0xA, 6, 0, 0, 0, 0};
  const int routines[kN] = {1, 1, 1, 1, 1, 2, 1, 1, 1};
  for (int i = 0; i < kN; ++i) {
    uint8_t* e = em(i);
    *reinterpret_cast<uint32_t*>(e) = i == 7 ? vtable + 0x100 : vtable;
    *reinterpret_cast<uint32_t*>(e + 4) = (i == 8 ? 0x201 : 1) | 0x2000;
    *reinterpret_cast<uint8_t**>(e + 8) = i + 1 < kN ? em(i + 1) : nullptr;
    *reinterpret_cast<uint8_t**>(e + 0xF4) = parts;
    e[0xFC] = static_cast<uint8_t>(routines[i]);
    e[0x100] = 0x15;
    e[0x101] = static_cast<uint8_t>(types[i]);
    *reinterpret_cast<int16_t*>(e + 0x324) = 400;
    *reinterpret_cast<int16_t*>(e + 0x326) = 400;
    *reinterpret_cast<int16_t*>(e + 0x344 + 0x26) = 5;  // the head
    *reinterpret_cast<uint8_t**>(e + 0x344 + 0x30) = e + 0x834;
    *reinterpret_cast<int16_t*>(e + 0x834 + 0x26) = 2;  // the body
    *reinterpret_cast<float*>(e + 0x378) = 2000000.0f;  // far from the player
    if (i == 6) e[0x814] = 1;
  }
  count_slot = kN;
  stride_slot = kStride;
  base_slot = reinterpret_cast<uintptr_t>(pool);
  list_slot = reinterpret_cast<uintptr_t>(em(0));
  auto addr = [&](int i) { return reinterpret_cast<uintptr_t>(em(i)); };

  game::MercEnemy list[game::kMaxMercEnemies];
  int n = game::merc_enemies(list, game::kMaxMercEnemies);
  CHECK(n == kLive, "merc_enemies(): %d Ganados, expected %d (not the other class, not the dead one)", n, kLive);
  for (int i = 0; i < n && i < kLive; ++i)
    CHECK(list[i].em == addr(i) && list[i].type == types[i] && list[i].routine == routines[i] && list[i].hp == 400 &&
              !list[i].pending && list[i].scored == (i == 6),
          "merc_enemies(): Ganado %d", i);
  CHECK(!game::merc_kill(addr(5)), "merc_kill() on one in a hit's reaction that the stage does not count as about");
  CHECK(!game::merc_kill(addr(6)), "merc_kill() on one scored already");
  CHECK(!game::merc_kill(addr(7)), "merc_kill() on another class");
  CHECK(!game::merc_kill(addr(8)), "merc_kill() on a dead one");
  CHECK(!game::merc_kill(addr(0) + 4), "merc_kill() off the pool");
  *reinterpret_cast<uint8_t*>(pg + 0x20) = 6;
  CHECK(!game::merc_kill(addr(0)) && game::merc_enemies(list, game::kMaxMercEnemies) == 0, "merc_kill() paused");
  *reinterpret_cast<uint8_t*>(pg + 0x20) = 3;
  *reinterpret_cast<uint32_t*>(pg + 0x54) &= ~0x40000000u;
  CHECK(!game::merc_kill(addr(0)), "merc_kill() outside the Mercenaries");
  *reinterpret_cast<uint32_t*>(pg + 0x54) |= 0x40000000;
  *reinterpret_cast<int16_t*>(em(0) + 0x834 + 0x26) = 5;
  CHECK(!game::merc_kill(addr(0)), "merc_kill() on a Ganado whose second part is no body");
  *reinterpret_cast<int16_t*>(em(0) + 0x834 + 0x26) = 2;

  const uintptr_t check = 0x457D30 + rel;
  struct Want {
    int i, score, combo, earned;
  } wants[] = {{0, 300, 1, 0}, {1, 10300, 2, 500}, {2, 15300, 3, 1500}, {3, 22300, 4, 3000}, {4, 22600, 5, 3200}};
  auto i32 = [&](int off) { return *reinterpret_cast<int32_t*>(work + off); };
  for (const Want& w : wants) {
    uint8_t* e = em(w.i);
    CHECK(game::merc_kill(addr(w.i)), "merc_kill() on Ganado %d", w.i);
    CHECK(*reinterpret_cast<int16_t*>(e + 0x324) == 1 && e[0x32C] == 1 && e[0x32D] == 10 && e[0x32E] == 2 &&
              *reinterpret_cast<uint8_t**>(e + 0x340) == e + 0x834 && *reinterpret_cast<float*>(e + 0x33C) >= 0,
          "merc_kill(): Ganado %d's health %d, record %X %X %X, part +%X", w.i, *reinterpret_cast<int16_t*>(e + 0x324),
          e[0x32C], e[0x32D], e[0x32E], static_cast<unsigned>(*reinterpret_cast<uint8_t**>(e + 0x340) - e));
    CHECK(!game::merc_kill(addr(w.i)), "merc_kill() on Ganado %d with its hit waiting", w.i);
    hit_check(check, e);
    // Type 6's death leaves the record up, as it does a shot's (its frames take it down).
    const int left = types[w.i] == 6 ? 1 : 0;
    CHECK(e[0x814] == 1 && *reinterpret_cast<uint32_t*>(e + 0xFC) == 0x203 && e[0x32C] == left &&
              *reinterpret_cast<int16_t*>(e + 0x324) == 0,
          "the hit check on Ganado %d (type %X): scored %d, routine %X, hit %d, health %d - expected 1, 203, %d, 0", w.i,
          types[w.i], e[0x814], *reinterpret_cast<uint32_t*>(e + 0xFC), e[0x32C], *reinterpret_cast<int16_t*>(e + 0x324),
          left);
    CHECK(i32(0x24) == w.score && i32(0x28) == w.combo && i32(0x48) == w.earned && i32(0x3C) == w.i + 1,
          "after Ganado %d: score %d, combo %d, earned %d, kills %d - expected %d, %d, %d, %d", w.i, i32(0x24), i32(0x28),
          i32(0x48), i32(0x3C), w.score, w.combo, w.earned, w.i + 1);
  }
  game::Merc m;
  CHECK(game::merc(&m) && m.score == 22600 && m.combo == 5 && m.bonus == 3200 && m.kills == 5,
        "merc(): score %d, combo %d, bonus %d, kills %d", m.score, m.combo, m.bonus, m.kills);
  n = game::merc_enemies(list, game::kMaxMercEnemies);
  bool scored = n == kLive;
  for (int i = 0; scored && i < 5; ++i) scored = list[i].scored;
  CHECK(scored && !list[5].scored && !list[5].alive, "merc_enemies() after: the five scored, the one in a hit's "
        "reaction not");
  // The one in a hit's reaction (routine 2, its +FD the reaction), counted as
  // about (+3CC bit 5): the same hit kills it, from the hit's reaction.
  {
    uint8_t* e = em(5);
    e[0xFD] = 0xB;
    *reinterpret_cast<uint32_t*>(e + 0x3CC) |= 0x20;
    n = game::merc_enemies(list, game::kMaxMercEnemies);
    CHECK(n == kLive && list[5].alive && list[5].routine == 2, "merc_enemies(): the one in a hit's reaction about");
    CHECK(game::merc_kill(addr(5)), "merc_kill() on one in a hit's reaction, about");
    hit_check(check, e);
    CHECK(e[0x814] == 1 && *reinterpret_cast<uint32_t*>(e + 0xFC) == 0x203 && e[0x32C] == 0 &&
              *reinterpret_cast<int16_t*>(e + 0x324) == 0,
          "the hit check on the one in a hit's reaction: scored %d, routine %X, hit %d, health %d - expected 1, 203, 0, "
          "0", e[0x814], *reinterpret_cast<uint32_t*>(e + 0xFC), e[0x32C], *reinterpret_cast<int16_t*>(e + 0x324));
    CHECK(game::merc(&m) && m.score == 22900 && m.combo == 6 && m.kills == 6,
          "after the one in a hit's reaction: score %d, combo %d, kills %d - expected 22900, 6, 6", m.score, m.combo,
          m.kills);
  }
  // The pool as the log has it: eight in use (the dead one's slot being let
  // go), the one the hit check left counted as about (its death routine takes
  // that off), the other seven bodies.
  game::MercPool pool_now;
  CHECK(game::merc_pool(&pool_now) && pool_now.slots == kN && pool_now.used == kN - 1 && pool_now.going == 1 &&
            pool_now.alive == 1 && pool_now.bodies == 7 && pool_now.melted == 0,
        "merc_pool(): %d slots, %d in use, %d going, %d about, %d bodies, %d gone - expected 9, 8, 1, 1, 7, 0",
        pool_now.slots, pool_now.used, pool_now.going, pool_now.alive, pool_now.bodies, pool_now.melted);
  game::merc_census("the test's pool");

  // Ten at once, as the tick hands them out: every record written in one pass,
  // then the hit checks one after another as the enemy manager's moves run them
  // in the frame, with no HUD frame between. Each kill takes the next step of the
  // combo and that step's bonus (a Ganado's: 0, 20, 50 ... 400, then 700); with
  // Endless combo's patches the HUD then puts the whole bonus on the score.
  constexpr int kBurst = 10;
  alignas(8) static uint8_t burst[kBurst * kStride];
  std::memset(burst, 0, sizeof(burst));
  std::memset(work, 0, sizeof(work_saved));
  for (int i = 0; i < kBurst; ++i) {
    uint8_t* e = burst + i * kStride;
    *reinterpret_cast<uint32_t*>(e) = vtable;
    *reinterpret_cast<uint32_t*>(e + 4) = 1 | 0x2000;
    *reinterpret_cast<uint8_t**>(e + 8) = i + 1 < kBurst ? e + kStride : nullptr;
    *reinterpret_cast<uint8_t**>(e + 0xF4) = parts;
    e[0xFC] = 1;
    e[0x100] = 0x15;
    *reinterpret_cast<int16_t*>(e + 0x324) = 400;
    *reinterpret_cast<int16_t*>(e + 0x344 + 0x26) = 5;
    *reinterpret_cast<uint8_t**>(e + 0x344 + 0x30) = e + 0x834;
    *reinterpret_cast<int16_t*>(e + 0x834 + 0x26) = 2;
    *reinterpret_cast<float*>(e + 0x378) = 2000000.0f;
  }
  count_slot = kBurst;
  base_slot = reinterpret_cast<uintptr_t>(burst);
  list_slot = reinterpret_cast<uintptr_t>(burst);
  n = game::merc_enemies(list, game::kMaxMercEnemies);
  int handed = 0;
  for (int i = 0; i < n; ++i) handed += game::merc_kill(list[i].em) ? 1 : 0;
  CHECK(n == kBurst && handed == kBurst, "ten at once: %d listed, %d hits handed out", n, handed);
  for (int i = 0; i < kBurst; ++i) hit_check(check, burst + i * kStride);
  CHECK(i32(0x24) == 3000 && i32(0x28) == kBurst && i32(0x3C) == kBurst && i32(0x48) == 2370 && i32(0x44) == kBurst,
        "ten at once: score %d, combo %d, kills %d, earned %d, best combo %d - expected 3000, 10, 10, 2370, 10",
        i32(0x24), i32(0x28), i32(0x3C), i32(0x48), i32(0x44));
  mem::Patch* combo_sites[] = {game::patch(game::Site::kMercFirst), game::patch(game::Site::kMercWindow),
                               game::patch(game::Site::kMercBonus)};
  if (combo_sites[0] && combo_sites[1] && combo_sites[2]) {
    uint8_t* const unit = reinterpret_cast<uint8_t*>(0xC599D0 + rel);
    static uint8_t unit_saved[0x144];
    std::memcpy(unit_saved, unit, sizeof(unit_saved));
    for (mem::Patch* p : combo_sites) p->apply();
    const auto hud = reinterpret_cast<int(__cdecl*)(void*)>(0x710C10 + rel);
    for (int f = 0; f < 3; ++f) {
      unit[0x91] |= 3;
      hud(work);
    }
    CHECK(i32(0x24) == 5370 && i32(0x48) == 0 && i32(0x28) == kBurst,
          "ten at once, with Endless combo: score %d, earned %d, combo %d - expected 5370, 0, 10", i32(0x24), i32(0x48),
          i32(0x28));
    for (int k = 2; k >= 0; --k) combo_sites[k]->revert();
    std::memcpy(unit, unit_saved, sizeof(unit_saved));
  }

  // Every area: the scene's area flags' clear each frame, and merc_all_areas().
  if (mem::Patch* areas = game::patch(game::Site::kMercAreas)) {
    CHECK(areas->at == 0x6BDDD0 + rel && areas->original == std::vector<uint8_t>({0x89, 0x99, 0x7C, 0x01, 0x00, 0x00}),
          "the area flags' clear at +%X", static_cast<unsigned>(areas->at - base));
    CHECK(areas->apply() && areas->revert(), "the area flags' patch did not go in and come out");
  } else {
    CHECK(false, "the area flags' clear not prepared");
  }
  uint32_t& area_flags = *reinterpret_cast<uint32_t*>(pg + 0x17C);
  area_flags = 0;
  CHECK(game::merc_all_areas(0) && area_flags == 0xFFFFFFFFu, "merc_all_areas(): the area flags %08X",
        static_cast<unsigned>(area_flags));
  area_flags = 0;
  *reinterpret_cast<uint32_t*>(pg + 0x54) &= ~0x40000000u;
  CHECK(!game::merc_all_areas(0) && area_flags == 0, "merc_all_areas() outside the Mercenaries");

  count_slot = slots_was[0];
  stride_slot = slots_was[1];
  base_slot = slots_was[2];
  list_slot = slots_was[3];
  player_slot = slots_was[4];
  std::memcpy(work, work_saved, sizeof(work_saved));
  std::memcpy(reinterpret_cast<void*>(pg), pg_saved, sizeof(pg_saved));
  for (uint8_t* t : thunks) *t = 0xE9;
  std::printf("Mercenaries kill run: merc_kill()'s hit on a Ganado's body, taken by the game's own hit check, starts "
              "its death and scores it (300, 10000, 5000, 7000 a Garrador, 300 type 6, 300 one in a hit's reaction; "
              "the combo and its bonus; the kills); ten in one frame take ten steps of the combo and each step's "
              "bonus; every area put up\n");
}

// The models' memory and the room's enemy list, read and written by game.cpp
// over stand-ins: the two pools' managers in the slots the game reads them from
// (cManagers: +4 the records, +8 how many, +C the stride; a record's +4 & 0x601
// in use), a pool of Ganados (their parts' count at +102, their list entry at
// +3A0, their life at +3CC bit 5) and GLOBAL_WK's list (+5410, an entry's +0
// bit 2 sent, +1 its id, +18 its room) with its dead mask (+502C, the list in
// play at +4FB3). The Mercenaries' flag up, their main loop, room 51B.
static void run_merc_memory_game(uintptr_t base) {
  const uintptr_t rel = base - 0x400000;
  CHECK(game::merc_memory_known(), "the models' pools are not known");
  if (!game::merc_memory_known()) return;
  auto u32 = [&](uintptr_t va) -> uint32_t& { return *reinterpret_cast<uint32_t*>(va + rel); };
  const uintptr_t pg = u32(0xC06F3C);
  static uint8_t pg_saved[0x8690];
  std::memcpy(pg_saved, reinterpret_cast<const void*>(pg), sizeof(pg_saved));
  uint32_t* const slots[] = {&u32(0xC07110), &u32(0xC07114), &u32(0xBFDB0C), &u32(0xBFDB10), &u32(0xBFDB08),
                             &u32(0xBFDB18)};
  uint32_t slots_was[6];
  for (int i = 0; i < 6; ++i) slots_was[i] = *slots[i];
  // The pools: 50 records (20 in use, 4 being let go - still taken for the
  // allocator), 300 parts (120 in use).
  struct Manager {
    uint32_t vtable, base, count, stride, mode, list;
  };
  constexpr int kModels = 50, kModelStride = 0x40, kParts = 300, kPartStride = 0x20;
  alignas(8) static uint8_t model_slots[kModels * kModelStride], part_slots[kParts * kPartStride];
  std::memset(model_slots, 0, sizeof(model_slots));
  std::memset(part_slots, 0, sizeof(part_slots));
  for (int i = 0; i < 24; ++i) *reinterpret_cast<uint32_t*>(model_slots + i * kModelStride + 4) = i < 20 ? 1 : 0x600;
  for (int i = 0; i < 120; ++i) *reinterpret_cast<uint32_t*>(part_slots + (i * 2) * kPartStride + 4) = 1;
  static Manager models, parts;
  models = Manager{0, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(model_slots)), kModels, kModelStride, 0, 0};
  parts = Manager{0, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(part_slots)), kParts, kPartStride, 0, 0};
  *slots[0] = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&models));
  *slots[1] = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&parts));
  *reinterpret_cast<uint32_t*>(pg + 0x54) |= 0x40000000;  // the Mercenaries
  *reinterpret_cast<uint8_t*>(pg + 0x20) = 3;             // their main loop
  *reinterpret_cast<uint16_t*>(pg + 0x4FAC) = 0x51B;
  *reinterpret_cast<int8_t*>(pg + 0x4FB3) = 0;  // list 0
  // The enemy pool: Ganados of entries 3..8 - 3, 4, 5 about (5 killed: scored),
  // 6 and 7 bodies, 8 a body gone - and a chicken (28) of entry 13.
  constexpr int kStride = 0xE08, kN = 7;
  alignas(8) static uint8_t pool[kN * kStride];
  std::memset(pool, 0, sizeof(pool));
  const uint32_t vtable = 0xB1035C + rel;
  const int entries[kN] = {3, 4, 5, 6, 7, 8, 13};
  const int part_counts[kN] = {45, 52, 45, 45, 40, 45, 30};
  for (int i = 0; i < kN; ++i) {
    uint8_t* e = pool + i * kStride;
    *reinterpret_cast<uint32_t*>(e) = vtable;
    *reinterpret_cast<uint32_t*>(e + 4) = 1;
    *reinterpret_cast<uint8_t**>(e + 8) = i + 1 < kN ? e + kStride : nullptr;
    e[0x100] = i == 6 ? 0x28 : 0x15;
    e[0x102] = static_cast<uint8_t>(part_counts[i]);
    e[0x3A0] = static_cast<uint8_t>(entries[i]);
    if (i < 3) *reinterpret_cast<uint32_t*>(e + 0x3CC) = 0x20;
    if (i == 2) e[0x814] = 1;
    if (i == 5) e[0x7F5] = 1;
  }
  *slots[2] = kN;
  *slots[3] = kStride;
  *slots[4] = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(pool));
  *slots[5] = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(pool));
  // The list: entries 0..11 Ganados (15) of room 51B, 0..9 sent, 0 and 1 dead;
  // 12 a Ganado of another room, 13 a chicken. So 2 and 9 are spent.
  uint8_t* const list = reinterpret_cast<uint8_t*>(pg + 0x5410);
  std::memset(list, 0, 0x2000);
  for (int i = 0; i <= 13; ++i) {
    uint8_t* e = list + i * 0x20;
    e[1] = i == 13 ? 0x28 : 0x15;
    *reinterpret_cast<uint16_t*>(e + 0x18) = i == 12 ? 0x400 : 0x51B;
    if (i <= 9 || i == 13) e[0] = 3;
  }
  uint32_t* const dead = reinterpret_cast<uint32_t*>(pg + 0x502C);
  std::memset(dead, 0, 0x20);
  dead[0] = 0xC0000000u;  // entries 0 and 1

  game::MercMemory mm;
  CHECK(game::merc_memory(&mm) && mm.models == kModels && mm.models_used == 24 && mm.parts == kParts &&
            mm.parts_used == 120 && mm.ems == kN && mm.ems_used == kN && mm.ganado_parts == 52 && mm.alive == 3 &&
            mm.dying == 1 && mm.bodies == 3,
        "merc_memory(): records %d/%d, parts %d/%d, enemy slots %d/%d, a Ganado %d, about %d, dying %d, bodies %d - "
        "expected 24/50, 120/300, 7/7, 52, 3, 1, 3", mm.models_used, mm.models, mm.parts_used, mm.parts, mm.ems_used,
        mm.ems, mm.ganado_parts, mm.alive, mm.dying, mm.bodies);
  game::MercEntries en;
  CHECK(game::merc_entries(&en) && en.dead_known && en.ganado[0] == 0xFFFu && en.sent[0] == 0x3FFu &&
            en.dead[0] == 0x3u && en.enemy[0] == 0x1F8u,
        "merc_entries(): Ganados %X, sent %X, dead %X, with an enemy %X (dead mask %d) - expected FFF, 3FF, 3, 1F8, 1",
        en.ganado[0], en.sent[0], en.dead[0], en.enemy[0], en.dead_known);
  CHECK(!game::merc_resend(0) && !game::merc_resend(3) && !game::merc_resend(10) && !game::merc_resend(12) &&
            !game::merc_resend(13) && !game::merc_resend(-1) && !game::merc_resend(256),
        "merc_resend() on one dead, one with an enemy, one not sent, another room's, a chicken's or out of the list");
  CHECK(game::merc_resend(2) && game::merc_resend(9) && list[2 * 0x20] == 1 && list[9 * 0x20] == 1 &&
            list[3 * 0x20] == 3 && !game::merc_resend(2),
        "merc_resend() on the spent 2 and 9: their flags %X, %X", list[2 * 0x20], list[9 * 0x20]);
  game::merc_census("the test's stage");
  // The bodies hurried: one waiting to go (its death's 3, 0, sub-step 1) on the
  // Ganados' control, whose eighth counter is back at 0; in the village, with a
  // body gone, the frames since its sweep at 300 - not elsewhere.
  alignas(8) static uint8_t control[0x40];
  std::memset(control, 0, sizeof(control));
  control[0xC] = 0x12;
  *reinterpret_cast<int16_t*>(control + 0x21) = 17;
  uint8_t* const waiting = pool + 3 * kStride;
  waiting[0xFC] = 3;
  waiting[0xFE] = 1;
  *reinterpret_cast<uint8_t**>(waiting + 0x6E0) = control;
  alignas(8) static uint32_t village[4];
  village[3] = 120;
  uint32_t& village_slot = u32(0xC7BF9C);
  const uint32_t village_was = village_slot;
  village_slot = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(village));
  CHECK(game::merc_hurry_bodies() == 1 && *reinterpret_cast<int16_t*>(control + 0x21) == 0 && village[3] == 120,
        "merc_hurry_bodies() in room 51B: the control's eighth counter %d, the village's frames since its sweep %u - "
        "expected 0, 120 (not the village)", *reinterpret_cast<int16_t*>(control + 0x21), village[3]);
  *reinterpret_cast<uint16_t*>(pg + 0x4FAC) = 0x400;
  *reinterpret_cast<int16_t*>(control + 0x21) = 21;
  CHECK(game::merc_hurry_bodies() == 2 && *reinterpret_cast<int16_t*>(control + 0x21) == 0 && village[3] == 300,
        "merc_hurry_bodies() in the village: the counter %d, the frames since its sweep %u - expected 0, 300",
        *reinterpret_cast<int16_t*>(control + 0x21), village[3]);
  CHECK(game::merc_hurry_bodies() == 0, "merc_hurry_bodies() again with nothing to hurry");
  // One falling (die 2's sub-step 1, its timer from 73): on to die 0 once 15
  // units have played, not before; one melting (die 3's sub-step 1): its waits at 0.
  uint8_t* const falling = pool + 0 * kStride;
  uint8_t* const melting = pool + 1 * kStride;
  falling[0xFC] = 3;
  falling[0xFD] = 2;
  falling[0xFE] = 1;
  *reinterpret_cast<float*>(falling + 0x4B4) = 60.0f;
  melting[0xFC] = 3;
  melting[0xFD] = 3;
  melting[0xFE] = 1;
  *reinterpret_cast<float*>(melting + 0x4B4) = 15.0f;
  *reinterpret_cast<float*>(melting + 0x4B8) = 53.0f;
  CHECK(game::merc_hurry_bodies() == 1 && *reinterpret_cast<uint32_t*>(falling + 0xFC) == 0x00010203u &&
            *reinterpret_cast<float*>(melting + 0x4B4) == 0.0f && *reinterpret_cast<float*>(melting + 0x4B8) == 0.0f,
        "merc_hurry_bodies() with 13 units of a fall played: routine %08X (expected its own, 00010203), the melt's waits "
        "%.0f, %.0f (expected 0, 0)", *reinterpret_cast<uint32_t*>(falling + 0xFC),
        *reinterpret_cast<float*>(melting + 0x4B4), *reinterpret_cast<float*>(melting + 0x4B8));
  *reinterpret_cast<float*>(falling + 0x4B4) = 58.0f;
  CHECK(game::merc_hurry_bodies() == 1 && *reinterpret_cast<uint32_t*>(falling + 0xFC) == 0x01000003u,
        "merc_hurry_bodies() with 15 units of a fall played: routine %08X - expected 01000003 (die 0, as the fall's end "
        "sends it)", *reinterpret_cast<uint32_t*>(falling + 0xFC));
  village_slot = village_was;
  *reinterpret_cast<uint16_t*>(pg + 0x4FAC) = 0x51B;
  // The village's lists, as read from its code: the area lists' 102 entries (one
  // of their 103 sends by a register), the events' 11 (0x28-0x2A, 0x35-0x39,
  // 0x3C-0x3E), none in both; only in the village (room 400).
  game::MercLists lists;
  CHECK(!game::merc_village_lists(&lists), "merc_village_lists() outside the village");
  *reinterpret_cast<uint16_t*>(pg + 0x4FAC) = 0x400;
  CHECK(game::merc_village_lists(&lists), "merc_village_lists() in the village");
  int area_n = 0, events_n = 0, both = 0;
  for (int w = 0; w < 8; ++w) {
    area_n += __builtin_popcount(lists.area[w]);
    events_n += __builtin_popcount(lists.events[w]);
    both += __builtin_popcount(lists.area[w] & lists.events[w]);
  }
  bool events_right = true;
  for (int e : {0x28, 0x29, 0x2A, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3C, 0x3D, 0x3E})
    events_right = events_right && (lists.events[e >> 5] & (1u << (e & 31)));
  CHECK(area_n == 101 && events_n == 11 && both == 0 && events_right && (lists.area[0] & (1u << 0xB)),
        "the village's lists: %d area entries (expected 101 read, entry 2 by a register), %d events' (11), %d in both",
        area_n, events_n, both);
  // An event's entry (0x3C, not sent) into an area list's dead one's place (0x0B,
  // sent and dead, its enemy gone): swapped, 0x0B not sent and not dead, 0x3C sent
  // and dead; not again, nor with an area entry that has an enemy or is not dead.
  for (int i : {0x0B, 0x3C}) {
    uint8_t* e = list + i * 0x20;
    std::memset(e, 0, 0x20);
    e[1] = 0x16;
    *reinterpret_cast<uint16_t*>(e + 0x18) = 0x400;
  }
  list[0x0B * 0x20] = 3;  // sent
  list[0x0B * 0x20 + 2] = 4;
  list[0x3C * 0x20 + 2] = 12;
  *reinterpret_cast<int16_t*>(list + 0x3C * 0x20 + 8) = 1800;
  *reinterpret_cast<int8_t*>(pg + 0x4FB3) = 0;
  dead[0] = 0x80000000u >> 0x0B;
  dead[1] = 0;
  CHECK(!game::merc_swap_entry(0x0B, 0x3C) && !game::merc_swap_entry(0x3C, 0x0C),
        "merc_swap_entry() the wrong way round, or with an area entry not dead");
  CHECK(game::merc_swap_entry(0x3C, 0x0B) && list[0x0B * 0x20] == 0 && list[0x0B * 0x20 + 2] == 12 &&
            *reinterpret_cast<int16_t*>(list + 0x0B * 0x20 + 8) == 1800 && list[0x3C * 0x20] == 3 &&
            list[0x3C * 0x20 + 2] == 4 && dead[0] == 0 && dead[1] == (0x80000000u >> (0x3C & 31)),
        "merc_swap_entry(3C, 0B): 0B flags %X type %d health %d, 3C flags %X type %d, dead %08X %08X",
        list[0x0B * 0x20], list[0x0B * 0x20 + 2], *reinterpret_cast<int16_t*>(list + 0x0B * 0x20 + 8),
        list[0x3C * 0x20], list[0x3C * 0x20 + 2], dead[0], dead[1]);
  CHECK(!game::merc_swap_entry(0x3C, 0x0B), "merc_swap_entry() again");
  // The chainsaws' waves, as read from its code (11 and 12, 61 and 62, 76 and
  // 77): a wave's entries are the lists' only once it has gone (its flag in
  // pG+174 up) - before, it sends them itself, whatever their state. Wave 2's 62,
  // not sent (refused as it went): not swapped before its flag, swapped after
  // into 0x0C's place (sent and dead, its enemy gone).
  uint32_t& flags174 = *reinterpret_cast<uint32_t*>(pg + 0x174);
  flags174 = 0x10000000u;  // the first wave gone
  CHECK(game::merc_village_lists(&lists) && lists.waves[0] == ((1u << 0x11) | (1u << 0x12)) && !lists.waves[1] &&
            !lists.waves[2] && !lists.waves[3],
        "the village's waves with the first gone: %08X %08X %08X %08X - expected 00060000 0 0 0", lists.waves[0],
        lists.waves[1], lists.waves[2], lists.waves[3]);
  for (int i : {0x0C, 0x62}) {
    uint8_t* e = list + i * 0x20;
    std::memset(e, 0, 0x20);
    e[1] = 0x10;
    *reinterpret_cast<uint16_t*>(e + 0x18) = 0x400;
  }
  list[0x0C * 0x20] = 2;  // sent
  list[0x0C * 0x20 + 2] = 3;
  list[0x62 * 0x20 + 2] = 12;
  *reinterpret_cast<int16_t*>(list + 0x62 * 0x20 + 8) = 6500;
  dead[0] |= 0x80000000u >> 0x0C;
  CHECK(!game::merc_swap_entry(0x62, 0x0C), "merc_swap_entry() of wave 2's entry before the wave has gone");
  flags174 |= 0x08000000u;
  CHECK(game::merc_village_lists(&lists) && lists.waves[3] == ((1u << (0x61 & 31)) | (1u << (0x62 & 31))),
        "the village's waves with the second gone: word 3 %08X - expected 00000006", lists.waves[3]);
  CHECK(game::merc_swap_entry(0x62, 0x0C) && list[0x0C * 0x20] == 0 && list[0x0C * 0x20 + 2] == 12 &&
            *reinterpret_cast<int16_t*>(list + 0x0C * 0x20 + 8) == 6500 && list[0x62 * 0x20] == 2 &&
            list[0x62 * 0x20 + 2] == 3 && !(dead[0] & (0x80000000u >> 0x0C)) &&
            (dead[3] & (0x80000000u >> (0x62 & 31))),
        "merc_swap_entry(62, 0C): 0C flags %X type %d health %d, 62 flags %X type %d, dead %08X %08X",
        list[0x0C * 0x20], list[0x0C * 0x20 + 2], *reinterpret_cast<int16_t*>(list + 0x0C * 0x20 + 8),
        list[0x62 * 0x20], list[0x62 * 0x20 + 2], dead[0], dead[3]);
  game::merc_census("the test's village");
  *reinterpret_cast<uint16_t*>(pg + 0x4FAC) = 0x51B;
  // With no list in play (+4FB3 -1) there is no dead mask to read.
  *reinterpret_cast<int8_t*>(pg + 0x4FB3) = -1;
  CHECK(game::merc_entries(&en) && !en.dead_known && en.dead[0] == 0, "merc_entries() with no list in play: dead %X",
        en.dead[0]);
  *reinterpret_cast<uint32_t*>(pg + 0x54) &= ~0x40000000u;
  CHECK(!game::merc_memory(&mm) && !game::merc_entries(&en), "the models' memory read outside the Mercenaries");
  for (int i = 0; i < 6; ++i) *slots[i] = slots_was[i];
  std::memcpy(reinterpret_cast<void*>(pg), pg_saved, sizeof(pg_saved));
  std::printf("models' memory run: the pools' records and parts in use, a Ganado's parts, the room's list (sent, dead, "
              "with an enemy) read; a spent entry sent again, a dead one or one with an enemy not\n");
}

// The castle (room 402): its counted groups and gates as read from its code; its
// areas two at a time in turn; its sweep hurried; refused sends given back on a
// stand-in work and pool (a send that took, one the pool refused, a slot the pool
// gave again, a set-up that failed, a tick missed); a gate's area trigger widened
// round a stand-in player - the game's own test (FUN_006b3540, FUN_0058b440) finds
// him in it only then - and put back as it was.
static void run_castle(uintptr_t base) {
  const uintptr_t rel = base - 0x400000;
  auto u32 = [&](uintptr_t va) -> uint32_t& { return *reinterpret_cast<uint32_t*>(va + rel); };
  auto addr = [](const void* p) { return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p)); };
  game::MercCastleGroup groups[16];
  const int n = game::merc_castle_groups(groups, 16);
  int sends = 0, wraps = 0, g58c = -1;
  for (int g = 0; g < n; ++g) {
    sends += groups[g].cap;
    wraps += groups[g].n;
    if (groups[g].counter == 0x58C) g58c = g;
  }
  CHECK(n == 14 && sends == 288 && wraps == 46 && g58c >= 0 && groups[g58c].cap == 30 && groups[g58c].n == 4 &&
            groups[g58c].wraps[0] == 0 && groups[g58c].wraps[3] == 3,
        "the castle's counted groups: %d (%d sends, %d wraps) - expected 14 (288, 46), +58C 30 from wraps 0-3", n, sends,
        wraps);
  if (n != 14 || g58c < 0) return;
  const uintptr_t pg = u32(0xC06F3C);
  static uint8_t pg_saved[0x8690];
  std::memcpy(pg_saved, reinterpret_cast<const void*>(pg), sizeof(pg_saved));
  uint32_t* const slots[] = {&u32(0xC7BFA8), &u32(0xC67728), &u32(0xC57054), &u32(0xBFDB0C), &u32(0xBFDB10),
                             &u32(0xBFDB08), &u32(0xBFDB18)};
  uint32_t slots_was[7];
  for (int i = 0; i < 7; ++i) slots_was[i] = *slots[i];
  *reinterpret_cast<uint32_t*>(pg + 0x54) |= 0x40000000;  // the Mercenaries
  *reinterpret_cast<uint8_t*>(pg + 0x20) = 3;             // their main loop
  uint16_t& room = *reinterpret_cast<uint16_t*>(pg + 0x4FAC);
  room = 0x402;
  alignas(8) static uint8_t work[0x5A8];
  std::memset(work, 0, sizeof(work));
  *slots[0] = addr(work);
  constexpr int kStride = 0xE08, kN = 4;
  alignas(8) static uint8_t pool[kN * kStride];
  std::memset(pool, 0, sizeof(pool));
  for (int i = 0; i < kN; ++i) {
    *reinterpret_cast<uint32_t*>(pool + i * kStride) = 0xB1035C + rel;  // cEm10
    pool[i * kStride + 0x100] = 0x1C;
  }
  *slots[3] = kN;
  *slots[4] = kStride;
  *slots[5] = *slots[6] = addr(pool);

  // The areas: two at a time in turn, sixteen ticks round; every area elsewhere.
  uint32_t& areas = *reinterpret_cast<uint32_t*>(pg + 0x17C);
  bool turn = true;
  for (unsigned t : {0u, 5u, 15u, 16u, 31u})
    turn = turn && game::merc_all_areas(t) && areas == ((0x00010001u << (t & 15)) | 0x4000000u);
  room = 0x400;
  CHECK(turn && game::merc_all_areas(3) && areas == 0xFFFFFFFFu,
        "merc_all_areas(): the castle's area flags not two at a time in turn, or the village's not all (%08X)", areas);
  room = 0x402;

  // Given back: per group, its counter's rise against the wraps that took a new enemy.
  int32_t& counter = *reinterpret_cast<int32_t*>(work + 0x58C);
  auto wrap = [&](int w, uint8_t* em) {
    *reinterpret_cast<uint32_t*>(work + w * 0xC) = em ? addr(em) : 0;
    work[w * 0xC + 7] = em ? 1 : 0;
  };
  auto enemy = [&](uint8_t* em, bool in_use, int16_t hp, bool scored) {
    *reinterpret_cast<uint32_t*>(em + 4) = in_use ? 1 : 0;
    *reinterpret_cast<int16_t*>(em + 0x324) = hp;
    em[0x814] = scored ? 1 : 0;
  };
  uint8_t* const a = pool;
  uint8_t* const b = pool + kStride;
  unsigned tick = 100;
  const int first = game::merc_castle_give_back(tick);
  counter = 2;  // two tries: wrap 0 took A, wrap 1's the pool refused
  enemy(a, true, 900, false);
  wrap(0, a);
  const int refused = game::merc_castle_give_back(++tick);
  const int32_t after_refused = counter;
  counter = 2;  // wrap 1 takes B
  enemy(b, true, 800, false);
  wrap(1, b);
  const int took = game::merc_castle_give_back(++tick);
  enemy(a, true, 0, true);  // A killed: a body
  const int killed = game::merc_castle_give_back(++tick);
  counter = 3;  // the sweep lets A go and the pool gives its slot to C, in wrap 0
  enemy(a, true, 700, false);
  const int reused = game::merc_castle_give_back(++tick);
  counter = 4;  // wrap 2's send whose set-up failed: no enemy in use, the wrap not set
  const int failed = game::merc_castle_give_back(++tick);
  const int32_t after_failed = counter;
  counter = 4;
  tick += 2;  // a tick not called: only notes
  const int missed = game::merc_castle_give_back(tick);
  CHECK(first == 0 && refused == 1 && after_refused == 1 && took == 0 && killed == 0 && reused == 0 && failed == 1 &&
            after_failed == 3 && missed == 0 && counter == 4,
        "merc_castle_give_back(): first %d, a refusal %d (counter %d), a send that took %d, a kill %d, a slot given "
        "again %d, a failed set-up %d (counter %d), a tick missed %d (counter %d) - expected 0, 1 (1), 0, 0, 0, 1 (3), "
        "0 (4)", first, refused, after_refused, took, killed, reused, failed, after_failed, missed, counter);
  room = 0x400;
  CHECK(game::merc_castle_give_back(++tick) == -1, "merc_castle_give_back() outside the castle");
  room = 0x402;

  // The sweep hurried: a gone body (+7F5) and the castle's timer is put at its 300.
  uint8_t* const gone = pool + 3 * kStride;
  enemy(gone, true, 0, true);
  gone[0x7F5] = 1;
  float& timer = *reinterpret_cast<float*>(work + 0x5A4);
  timer = 12.f;
  game::merc_hurry_bodies();
  CHECK(timer == 300.f, "merc_hurry_bodies() in the castle: its sweep's timer %.0f - expected 300", timer);

  // The gates: the scene's list - a link to follow, another trigger, then the castle's two.
  alignas(8) static uint8_t scene[0x100], link[0x10], other[0xA0], gate10[0xA0], gate0f[0xA0];
  for (uint8_t* p : {scene, link, other, gate10, gate0f}) std::memset(p, 0, p == scene ? sizeof(scene) : p == link ? sizeof(link) : 0xA0);
  *slots[1] = addr(scene);
  *reinterpret_cast<uint32_t*>(scene + 0x8C) = addr(link) | 1;
  *reinterpret_cast<uint32_t*>(link) = addr(other);
  *reinterpret_cast<uint32_t*>(other) = addr(gate10);
  other[0x34] = 3;
  other[0x35] = 4;
  other[0x36] = 3;
  *reinterpret_cast<uint32_t*>(gate10) = addr(gate0f);
  *reinterpret_cast<uint32_t*>(gate0f) = 0xFFFFFFFFu;
  const float shape10[11] = {14313, 1000, 750, 11949, -57026, 16120, -57016, 16066, -55444, 11996, -55414};
  const float shape0f[11] = {13200, 1000, 750, -1062, -49929, 4267, -49230, 4355, -41632, -1274, -42478};
  auto trigger = [&](uint8_t* t, int id, uint8_t set_off, uint8_t b37, uint32_t event, const float* shape) {
    t[4] = t[5] = 1;  // a four-cornered area
    std::memcpy(t + 8, shape, 11 * sizeof(float));
    t[0x34] = 3;
    t[0x35] = 2;
    t[0x36] = static_cast<uint8_t>(id);
    t[0x37] = b37;
    t[0x38] = set_off;
    t[0x39] = 1;
    *reinterpret_cast<uint32_t*>(t + 0x40) = event;
  };
  trigger(gate10, 0x10, 8, 1, 0x7D49D0 + rel, shape10);
  trigger(gate0f, 0x0F, 1, 0, 0x7D7040 + rel, shape0f);
  alignas(8) static uint8_t player[0x900];
  std::memset(player, 0, sizeof(player));
  const float at[3] = {500.f, 9000.f, -30000.f};
  std::memcpy(player + 0x94, at, sizeof(at));
  *slots[2] = addr(player);
  using ShapeFn = void(__cdecl*)(void* out, void* trigger);
  using InsideFn = int(__cdecl*)(void* shape, const float* point);
  const auto shape_of = reinterpret_cast<ShapeFn>(0x6B3540 + rel);
  const auto inside = reinterpret_cast<InsideFn>(0x58B440 + rel);
  auto in = [&](uint8_t* t, float dx, float dz) {
    uint32_t shape[16] = {};
    shape_of(shape, t);
    const float point[3] = {at[0] + dx, at[1] + 100.f, at[2] + dz};
    return (inside(shape, point) & 0xFF) != 0;
  };
  game::MercGate gates[2];
  const bool listed = game::merc_castle_gates(gates) && gates[0].at == 0x10 && gates[0].found && gates[0].enabled &&
                      gates[1].at == 0x0F && gates[1].found && gates[1].enabled;
  const bool far_before = !in(gate0f, 0, 0) && !in(gate10, 0, 0);
  const bool armed = game::merc_castle_arm_gate(1, true) && !game::merc_castle_arm_gate(1, true);
  const bool in_armed = in(gate0f, 0, 0) && in(gate0f, 1000, -1000) && in(gate0f, -1000, 1000) && gate0f[0x38] == 1;
  const bool back = game::merc_castle_arm_gate(1, false) && !std::memcmp(gate0f + 8, shape0f, sizeof(shape0f)) &&
                    gate0f[0x38] == 1 && !in(gate0f, 0, 0);
  const bool armed10 = game::merc_castle_arm_gate(0, true) && gate10[0x38] == 1 && in(gate10, 0, 1000) &&
                       game::merc_castle_arm_gate(0, false) && gate10[0x38] == 8 &&
                       !std::memcmp(gate10 + 8, shape10, sizeof(shape10));
  gate10[0x34] = 2;  // its event has been: off
  const bool off = game::merc_castle_gates(gates) && gates[0].found && !gates[0].enabled &&
                   !game::merc_castle_arm_gate(0, true);
  gate10[0x34] = 3;
  *reinterpret_cast<uint32_t*>(gate0f + 0x40) = 0x7D49D0 + rel;  // another event
  const bool other_event = game::merc_castle_gates(gates) && !gates[1].found && !game::merc_castle_arm_gate(1, true);
  CHECK(listed && far_before && armed && in_armed && back && armed10 && off && other_event,
        "the castle's gates: listed %d, the player outside both %d, armed once %d, inside it then %d, put back %d, the "
        "action button's armed and put back %d, refused once off %d, not the castle's with another event %d",
        listed, far_before, armed, in_armed, back, armed10, off, other_event);
  *reinterpret_cast<uint32_t*>(gate0f + 0x40) = 0x7D7040 + rel;
  game::merc_census("the test's castle");
  // The base (room 403): its area lists' entries, and a one-time send's entry only once its flag is up.
  room = 0x403;
  uint32_t& f174 = *reinterpret_cast<uint32_t*>(pg + 0x174);
  f174 = 0x40000000;
  game::MercLists bl;
  const bool base_read = game::merc_village_lists(&bl);
  int base_area = 0;
  for (const uint32_t w : bl.area) base_area += __builtin_popcount(w);
  const bool first_only = (bl.waves[0x89 >> 5] & (1u << (0x89 & 31))) && !(bl.waves[0x8A >> 5] & (1u << (0x8A & 31)));
  f174 = 0x60000000;
  game::merc_village_lists(&bl);
  const bool both = (bl.waves[0x8A >> 5] & (1u << (0x8A & 31))) != 0;
  CHECK(base_read && base_area == 104 && first_only && both && !(bl.area[0x41 >> 5] & (1u << (0x41 & 31))),
        "the base's lists: read %d, %d area entries (104 read, not 41), its one-time sends 89 then 8A gone %d %d", base_read,
        base_area, first_only, both);
  room = 0x402;

  for (int i = 0; i < 7; ++i) *slots[i] = slots_was[i];
  std::memcpy(reinterpret_cast<void*>(pg), pg_saved, sizeof(pg_saved));
  std::printf("castle run: %d counted groups (%d sends, %d wraps); areas in turn; refused sends given back; its sweep "
              "hurried; its gates widened round the player and put back\n", n, sends, wraps);
}

// Kill enemies as they appear's memory plan (mercmem.h) over a stand-in village,
// as the code reads the stage and the user's logs measured it (2026-10-18): the
// 124 entries - 5 placed at the start, the lists' 102 sent in order from frame
// 450, three chainsaws' waves of 2 at 10, 25 and 35 deaths (tries less those
// about), three pickups' events of 3, 5 and 3 fired at set frames - in the
// village's order each frame (the lists, the events, the waves), the lists and
// the events trying each entry only while fewer than 10 tries and Ganados about
// stand (the events, unpatched), the waves always, an event or a wave once. A
// try takes an enemy slot (80, the scene holding 37; a Ganado about holds two,
// itself and its weapon, a body one) or is refused, its entry not sent (lost for
// an event or a wave); then a model's 34 parts from 1673, the scene holding 650,
// or the set-up fails and the entry is spent. A killed Ganado falls for 146
// frames (still about), waits its turn to melt (20 frames after the last), melts
// for 156 and goes at the sweep, every 300 frames - hurried: falls 30 frames, one
// starts melting each frame, melts 20 and goes the next frame.
struct SimVillage {
  enum S : uint8_t { kUnsent, kAbout, kDying, kBody, kMelting, kMelted, kGone, kSpent };
  static constexpr int kN = 124, kPlaced = 5, kAreaEnd = 107, kBossEnd = 113;
  static constexpr int kEms = 80, kEmsScene = 37, kParts = 1673, kPer = 34;
  int scene_parts = 650;
  bool hurried = false, events_free = false;
  bool wave_short = false;  // the pool full as each wave goes: its second send refused
  int event_at[3] = {600, 900, 1200};  // the items picked up at 10, 15 and 20 s
  static constexpr int kEventFirst[3] = {113, 116, 121}, kEventCount[3] = {3, 5, 3};
  S s[kN] = {};
  int t[kN] = {}, about_since[kN] = {};
  bool sent[kN] = {}, dead[kN] = {};
  bool wave[3] = {}, event_done[3] = {};
  int control = 0, frame = 0, kills = 0, fails = 0, refused = 0, since_sweep = 1, tries = kPlaced;
  int longest = 0;  // the most frames a Ganado stood about before its kill

  SimVillage() {
    for (int i = 0; i < kPlaced; ++i) {
      s[i] = kAbout;
      sent[i] = true;
    }
  }
  bool holds(int i) const { return s[i] >= kAbout && s[i] <= kMelted; }
  int count(S x) const {
    int n = 0;
    for (int i = 0; i < kN; ++i) n += s[i] == x ? 1 : 0;
    return n;
  }
  int about() const { return count(kAbout) + count(kDying); }
  int ems_used() const { return kEmsScene + 2 * about() + count(kBody) + count(kMelting) + count(kMelted); }
  int parts_used() const {
    int n = 0;
    for (int i = 0; i < kN; ++i) n += holds(i) ? 1 : 0;
    return scene_parts + n * kPer;
  }
  // One try at an entry, as setEm makes it.
  void send(int i, int* w0, bool short_of_slots = false) {
    ++*w0;
    ++tries;
    if (short_of_slots || kEms - ems_used() < 2) {
      ++refused;
      return;
    }
    sent[i] = true;
    if (kParts - parts_used() >= kPer) {
      s[i] = kAbout;
      about_since[i] = frame;
    } else {
      s[i] = kSpent;
      ++fails;
    }
  }
  void step() {
    const int fall = hurried ? 30 : 146, melt = hurried ? 20 : 156;
    for (int i = 0; i < kN; ++i)
      if (s[i] == kDying && --t[i] <= 0) s[i] = kBody;
    if (control > 0) --control;
    for (int i = 0; i < kN && control == 0; ++i)
      if (s[i] == kBody) {
        s[i] = kMelting;
        t[i] = melt;
        control = 20;
      }
    for (int i = 0; i < kN; ++i)
      if (s[i] == kMelting && --t[i] <= 0) s[i] = kMelted;
    ++frame;
    if (++since_sweep > 300) {
      since_sweep = 1;
      for (int i = 0; i < kN; ++i)
        if (s[i] == kMelted) s[i] = kGone;
    }
    (void)fall;
    if (frame < 450) return;
    int w0 = about();
    for (int i = kPlaced; i < kAreaEnd && w0 < 10; ++i)
      if (!sent[i]) send(i, &w0);
    for (int e = 0; e < 3; ++e) {
      if (event_done[e] || frame < event_at[e]) continue;
      event_done[e] = true;
      for (int k = 0; k < kEventCount[e]; ++k) {
        const int i = kEventFirst[e] + k;
        if (!sent[i] && (events_free || w0 < 10)) send(i, &w0);
      }
    }
    const int deaths = tries - w0;
    for (int wv = 0; wv < 3; ++wv) {
      static const int at[3] = {10, 25, 35};
      if (wave[wv] || deaths < at[wv]) continue;
      wave[wv] = true;
      for (int k = 0; k < 2; ++k) {
        const int i = kAreaEnd + wv * 2 + k;
        sent[i] = false;
        send(i, &w0, wave_short && k == 1);
      }
    }
  }
  game::MercMemory memory() const {
    game::MercMemory m;
    m.models = 1000;
    m.models_used = 0;
    m.parts = kParts;
    m.parts_used = parts_used();
    m.ems = kEms;
    m.ems_used = ems_used();
    m.ganado_parts = kPer;
    m.alive = about();
    m.dying = count(kDying);
    m.bodies = count(kBody) + count(kMelting) + count(kMelted);
    return m;
  }
  game::MercEntries entries(bool dead_known) const {
    game::MercEntries e;
    e.dead_known = dead_known;
    for (int i = 0; i < kN; ++i) {
      const uint32_t bit = 1u << (i & 31);
      e.ganado[i >> 5] |= bit;
      if (sent[i]) e.sent[i >> 5] |= bit;
      if (dead_known && dead[i]) e.dead[i >> 5] |= bit;
      if (holds(i)) e.enemy[i >> 5] |= bit;
    }
    return e;
  }
  void kill(int i) {
    s[i] = kDying;
    t[i] = hurried ? 30 : 146;
    dead[i] = true;
    ++kills;
    if (frame - about_since[i] > longest) longest = frame - about_since[i];
  }
  // merc_hurry_bodies(): the control's counter back at 0, and with a body gone
  // the frames since the sweep at 300.
  void hurry() {
    control = 0;
    if (count(kMelted) && since_sweep < 300) since_sweep = 300;
  }
};

struct SimResult {
  int kills, seconds, fails, refused, resent, held_ticks, ticks, longest, swaps, wave_swaps;
};

static SimResult run_village(SimVillage v, bool plan_on, bool dead_known, bool swap = false) {
  mercmem::Memory memory;
  game::MercLists lists;
  for (int i = SimVillage::kPlaced; i < SimVillage::kAreaEnd; ++i) lists.area[i >> 5] |= 1u << (i & 31);
  for (int i = SimVillage::kBossEnd; i < SimVillage::kN; ++i) lists.events[i >> 5] |= 1u << (i & 31);
  int f = 0, held = 0, swaps = 0, wave_swaps = 0;
  for (; f < 60 * 600 && v.kills < SimVillage::kN; ++f) {
    for (int wv = 0; wv < 3; ++wv)  // a wave's entries the lists' once it has gone (its flag up)
      for (int k = 0; k < 2 && v.wave[wv]; ++k) {
        const int i = SimVillage::kAreaEnd + wv * 2 + k;
        lists.waves[i >> 5] |= 1u << (i & 31);
      }
    if (v.hurried) v.hurry();
    int kills = -1;
    if (plan_on) {
      const game::MercMemory mm = v.memory();
      game::MercEntries en = v.entries(dead_known);
      const mercmem::Plan plan = memory.step(&mm, &en, swap ? &lists : nullptr);
      int resent = 0;
      for (int i = 0; i < SimVillage::kN; ++i) {
        const uint32_t bit = 1u << (i & 31);
        if (!(plan.resend[i >> 5] & bit)) continue;
        if (v.s[i] != SimVillage::kSpent) {
          CHECK(false, "the plan sends entry %d again, which is not spent (state %d)", i, v.s[i]);
          continue;
        }
        v.s[i] = SimVillage::kUnsent;
        v.sent[i] = false;
        en.sent[i >> 5] &= ~bit;
        ++resent;
      }
      for (int k = 0; k < plan.swaps; ++k) {  // as merc_swap_entry(): the other's Ganado into the area's place
        const int ev = plan.swap_from[k], area = plan.swap_area[k];
        CHECK(v.s[ev] == SimVillage::kUnsent && !v.sent[ev] && v.s[area] == SimVillage::kGone && v.dead[area],
              "the plan swaps entry %d (state %d) into %d's place (state %d)", ev, v.s[ev], area, v.s[area]);
        v.s[area] = SimVillage::kUnsent;
        v.sent[area] = false;
        v.dead[area] = false;
        v.s[ev] = SimVillage::kGone;
        v.sent[ev] = true;
        v.dead[ev] = true;
        en.sent[ev >> 5] |= 1u << (ev & 31);
        en.sent[area >> 5] &= ~(1u << (area & 31));
        ++swaps;
        if (ev >= SimVillage::kAreaEnd && ev < SimVillage::kBossEnd) ++wave_swaps;
      }
      memory.sent(en.sent, resent);
      kills = plan.kills;
    }
    // The oldest about first, as the tick takes them.
    bool waited = false;
    for (;;) {
      int oldest = -1;
      for (int i = 0; i < SimVillage::kN; ++i)
        if (v.s[i] == SimVillage::kAbout && (oldest < 0 || v.about_since[i] < v.about_since[oldest])) oldest = i;
      if (oldest < 0) break;
      if (kills == 0) {
        waited = true;
        break;
      }
      v.kill(oldest);
      if (kills > 0) --kills;
    }
    held += waited ? 1 : 0;
    v.step();
  }
  return SimResult{v.kills, f / 60, v.fails, v.refused, memory.resent(), held, f, v.longest, swaps, wave_swaps};
}

static void run_merc_memory_sim() {
  const struct {
    const char* what;
    bool plan, hurried, events_free, dead_known;
    int scene_parts;
  } runs[] = {
      {"as it was (every Ganado killed as it appears)", false, false, false, true, 650},
      {"the plan, the bodies as the game has them", true, false, false, true, 650},
      {"the plan, the bodies hurried", true, true, false, true, 650},
      {"the plan, the bodies hurried, the events let send", true, true, true, true, 650},
      {"the same without the dead mask", true, true, true, false, 650},
      {"the same, the scene holding 850 parts (room for 24)", true, true, true, true, 850},
  };
  SimResult r[6];
  for (int k = 0; k < 6; ++k) {
    SimVillage v;
    v.hurried = runs[k].hurried;
    v.events_free = runs[k].events_free;
    v.scene_parts = runs[k].scene_parts;
    r[k] = run_village(v, runs[k].plan, runs[k].dead_known);
    std::printf("village (%s): %d of %d killed in %d s at 60 fps; %d set-ups failed, %d tries refused, %d sent again; "
                "kills waited %d of %d ticks; the longest a Ganado stood before its kill %.1f s\n",
                runs[k].what, r[k].kills, SimVillage::kN, r[k].seconds, r[k].fails, r[k].refused, r[k].resent,
                r[k].held_ticks, r[k].ticks, r[k].longest / 60.0);
  }
  CHECK(r[0].kills < 70 && r[0].fails > 30, "as it was: %d killed, %d set-ups failed - expected the stall", r[0].kills,
        r[0].fails);
  CHECK(r[2].kills < SimVillage::kN, "the bodies hurried, the events unpatched: %d killed - expected the events' "
        "entries lost (sent only below the ten, which every area keeps up)", r[2].kills);
  // No hourglass picked up: the events never fire. Without the swaps the 11 never
  // come; with them all 124 do, the 11 in the area lists' places.
  for (bool swap : {false, true}) {
    SimVillage v;
    v.hurried = true;
    v.events_free = true;
    for (int& at : v.event_at) at = 1 << 30;
    const SimResult n = run_village(v, true, true, swap);
    std::printf("village (no hourglass picked up, the events' Ganados %s): %d of %d killed in %d s; %d swapped in; the "
                "longest a Ganado stood before its kill %.1f s\n", swap ? "swapped into the lists" : "left",
                n.kills, SimVillage::kN, n.seconds, n.swaps, n.longest / 60.0);
    CHECK(swap ? n.kills == SimVillage::kN && n.swaps == 11 && n.longest <= 60 : n.kills == SimVillage::kN - 11,
          "no hourglass picked up, swaps %d: %d killed, %d swapped in", swap, n.kills, n.swaps);
  }
  // Each wave short of a slot as it goes (the user's 2026-10-18 game: wave 2's
  // second, entry 98, refused in both games): without the swaps its three
  // refused never come; with them each comes in an area list's place.
  for (bool swap : {false, true}) {
    SimVillage v;
    v.hurried = true;
    v.events_free = true;
    v.wave_short = true;
    const SimResult n = run_village(v, true, true, swap);
    std::printf("village (each wave's second refused, %s): %d of %d killed in %d s; %d tries refused, %d swapped in "
                "(%d of them the waves'); the longest a Ganado stood before its kill %.1f s\n",
                swap ? "swapped into the lists" : "left", n.kills, SimVillage::kN, n.seconds, n.refused, n.swaps,
                n.wave_swaps, n.longest / 60.0);
    CHECK(swap ? n.kills == SimVillage::kN && n.wave_swaps == 3 && n.swaps == 14 && n.longest <= 60
               : n.kills == SimVillage::kN - 3,
          "each wave's second refused, swaps %d: %d killed, %d swapped in (%d the waves')", swap, n.kills, n.swaps,
          n.wave_swaps);
  }
  for (int k : {3, 4, 5})
    CHECK(r[k].kills == SimVillage::kN && r[k].fails == 0 && r[k].refused == 0 && r[k].longest <= (k == 5 ? 90 : 60),
          "%s: %d of %d killed, %d set-ups failed, %d tries refused, the longest wait %.1f s - expected all, none "
          "failed or refused, none standing over %.1f s", runs[k].what, r[k].kills, SimVillage::kN, r[k].fails,
          r[k].refused, r[k].longest / 60.0, k == 5 ? 1.5 : 1.0);
}

// The castle (room 402) as its code reads (game.h): 114 wraps; the set-up's 8;
// fourteen counted groups (their caps, wraps, area flags and how few must be about
// for them: 288 sends) and the one-shot steps (the bosses at 20 and 90 kills, a
// Garrador at 25, the chained steps, the two gates' 12 - and the gate by the action
// button turning one group to another 25): 348. Each frame: the area flags, the
// castle's sweep (every 300 of its timer, or the next frame with a body gone), its
// spawner (each group's free wraps at once while few enough are about - a send the
// pool refuses (fewer than two slots: the Ganado and its weapon) still counts), then
// the deaths (a fall counting as about, a body waiting its turn to melt, melting,
// gone). The pool 100 slots, the scene 34. The kills by mercmem.h's plan, oldest
// first; at 60 fps (a delta of 0.5).
struct SimCastle {
  static constexpr int kWraps = 114, kSlots = 100, kScene = 34;
  enum Phase : uint8_t { kFree, kAbout, kFalling, kWaiting, kMelting, kGone };
  struct Wrap {
    Phase phase = kFree;
    float t = 0.f;
    int since = 0;  // the frame it came
  };
  struct Group {
    int cap, area, under;  // its cap, its area flag, sends while fewer than `under` are about
    std::vector<int> wraps;
    int count = 0;
  };
  bool areas_in_turn = false, sweep_hurried = false, give_back = false, gates = false, bodies_hurried = true;
  Wrap w[kWraps];
  std::vector<Group> groups;
  int frame = 0, kills = 0, refused = 0, given = 0, longest = 0, gates_open = 0, last_kill = 0;
  float sweep = 0.f;
  int control = 0;
  uint32_t flags = 0;  // the one-shot steps done (a bit each)
  int six = 0, fivestep = 0, boss1_at = -1;
  bool gate10 = false, gate0f = false;

  SimCastle() {
    auto g = [&](int cap, int area, int under, std::vector<int> ws) { groups.push_back(Group{cap, area, under, ws}); };
    g(5, 0, 9, {0x31, 0x32});                   // 0: after the set-up's first two are gone
    g(30, 0x4000000, 7, {0, 1, 2, 3});
    g(15, 0x2000, 8, {0x5A, 0x5B, 0x5C});
    g(10, 0x80000, 7, {0xF, 0x10, 0x11, 0x12});  // 3: after its thirteen steps
    g(20, 0x40000, 7, {0xC, 0xD, 0xE});
    g(30, 0x80000000, 8, {0x15, 0x16, 0x17});    // 5: after its two steps
    g(18, 0x40000000, 8, {0x63, 0x64});          // 6: after its two steps
    g(25, 0x20000000, 7, {0x18, 0x19, 0x13, 0x14});  // 7: after its step
    g(10, 0x10000000, 9, {0x58, 0x59});
    g(15, 0x10000, 8, {0x60, 0x61, 0x62});
    g(20, 0x4000, 8, {0x55, 0x56, 0x57});
    g(20, 0x20000, 7, {4, 5, 6, 7});              // 11: until the action button's gate
    g(25, 0x20000, 7, {8, 9, 0xA, 0xB});          // 12: once it
    g(45, 0x8000000, 6, {0x33, 0x34, 0x35, 0x36, 0x37});
    for (int i = 0x27; i <= 0x2E; ++i) come(i);  // the set-up's 8
  }
  int used() const {
    int n = kScene;
    for (const Wrap& x : w) n += x.phase == kAbout || x.phase == kFalling ? 2 : x.phase != kFree ? 1 : 0;
    return n;
  }
  int about() const {
    int n = 0;
    for (const Wrap& x : w) n += x.phase == kAbout || x.phase == kFalling ? 1 : 0;
    return n;
  }
  void come(int i) {
    w[i].phase = kAbout;
    w[i].since = frame;
  }
  bool send(int i) {  // EmSetEvent into wrap i
    if (kSlots - used() < 2) {
      ++refused;
      return false;
    }
    come(i);
    return true;
  }
  bool step_once(int bit, uint32_t area, int under, int iv, uint32_t up, std::vector<int> ws) {
    if ((flags & (1u << bit)) || !(up & area) || iv >= under) return false;
    flags |= 1u << bit;
    for (int i : ws) send(i);
    return true;
  }
  // One frame of the game: the area flags `up`, the sweep, the spawner, the deaths.
  void step(uint32_t up) {
    ++frame;
    constexpr float kDelta = 0.5f;
    sweep += kDelta;
    if (sweep > 300.f) {
      sweep = 1.f;
      for (Wrap& x : w)
        if (x.phase == kGone) x.phase = kFree;
    }
    int iv = about();
    if (!(flags & (1u << 20)) && w[0x27].phase == kFree && w[0x28].phase == kFree) flags |= 1u << 20;
    for (size_t g = 0; g < groups.size(); ++g) {
      Group& grp = groups[g];
      if (grp.area && !(up & static_cast<uint32_t>(grp.area))) continue;
      if (iv >= grp.under) continue;
      if (g == 0 && !(flags & (1u << 20))) continue;
      if (g == 3 && six < 13) continue;
      if (g == 5 && !(flags & (1u << 6))) continue;
      if (g == 6 && !(flags & (1u << 8))) continue;
      if (g == 7 && !(flags & (1u << 10))) continue;
      if (g == 11 && gate10) continue;
      if (g == 12 && !gate10) continue;
      for (int i : grp.wraps)
        if (grp.count < grp.cap && w[i].phase == kFree) {
          send(i);
          ++grp.count;
        }
    }
    // The one-shot steps (each its own wraps).
    if ((up & 0x8000) && iv < 10 && kills >= 25 && !(flags & (1u << 1))) {
      flags |= 1u << 1;
      send(0x38);
    }
    if ((up & 0x4000000) && iv < 9) {
      if (!(flags & (1u << 2)) && kills >= 20 && (!(flags & (1u << 1)) || w[0x38].phase != kAbout)) {
        flags |= 1u << 2;
        send(0x6C);
        boss1_at = frame;
      } else if ((flags & (1u << 2)) && !(flags & (1u << 3)) && frame - boss1_at > 300 && kills >= 90) {
        flags |= 1u << 3;
        send(0x6D);
      }
    }
    if ((up & 0x100000) && iv < 7 && fivestep < 4) {
      static const std::vector<int> steps[4] = {{0x6E, 0x6F}, {0x70, 0x71}, {0x48}, {0x49}};
      for (int i : steps[fivestep]) send(i);
      ++fivestep;
    }
    if ((up & 0x80000) && iv < 7 && six < 13) {
      send(0x1A + six);
      ++six;
    }
    step_once(5, 0x80000000, 9, iv, up, {0x39, 0x3A});
    if (!step_once(6, 0x80000000, 8, iv, up, {0x3B, 0x3C, 0x3D})) step_once(7, 0x80000000, 8, iv, up, {0x4A, 0x4C});
    if (!step_once(8, 0x40000000, 8, iv, up, {0x3E, 0x3F, 0x40})) step_once(9, 0x40000000, 8, iv, up, {0x46, 0x47});
    step_once(11, 0x2000000, 9, iv, up, {0x41, 0x42});
    step_once(10, 0x20000000, 7, iv, up, {0x43, 0x44});
    step_once(12, 0x800000, 9, iv, up, {0x53, 0x54});
    // The deaths: a fall counts as about; then one body at a time starts melting.
    const float fall = bodies_hurried ? 15.f : 73.f, melt = bodies_hurried ? 10.f : 78.f;
    if (control > 0) --control;
    for (Wrap& x : w) {
      if (x.phase == kFalling && (x.t -= kDelta) <= 0.f) x.phase = kWaiting;
      if (x.phase == kMelting && (x.t -= kDelta) <= 0.f) x.phase = kGone;
    }
    for (Wrap& x : w)
      if (x.phase == kWaiting && control == 0) {
        x.phase = kMelting;
        x.t = melt;
        control = bodies_hurried ? 0 : 20;
        if (!bodies_hurried) break;
      }
  }
  int total() const { return 348; }
};

static void run_castle_sim() {
  struct Run {
    const char* what;
    bool in_turn, sweep, give_back, gates;
  };
  const Run runs[] = {
      {"as the user's game had it (every area up, the sweep every 300, no gates)", false, false, false, false},
      {"every area up, the sweep hurried", false, true, false, false},
      {"the areas in turn, the sweep hurried, sends given back", true, true, true, false},
      {"the same with the gates opened once the stage runs dry", true, true, true, true},
      {"the same with every area up at once", false, true, true, true},
  };
  int got[5] = {};
  for (int r = 0; r < 5; ++r) {
    SimCastle c;
    c.areas_in_turn = runs[r].in_turn;
    c.sweep_hurried = runs[r].sweep;
    c.give_back = runs[r].give_back;
    c.gates = runs[r].gates;
    mercmem::Memory memory;
    unsigned tick = 0;
    int f = 0;
    for (; f < 60 * 600 && c.kills < c.total(); ++f, ++tick) {
      // The tick: the counts given back, the kills, the gates, the sweep hurried, the area flags.
      std::vector<int> counts_before;
      game::MercMemory mm;
      mm.models = 2000;
      mm.parts = 4000;
      mm.parts_used = 100;
      mm.ganado_parts = 38;
      mm.ems = SimCastle::kSlots;
      mm.ems_used = c.used();
      mm.alive = c.about();
      for (const auto& x : c.w) {
        mm.dying += x.phase == SimCastle::kFalling ? 1 : 0;
        mm.bodies += x.phase >= SimCastle::kWaiting ? 1 : 0;
      }
      const mercmem::Plan plan = memory.step(&mm, nullptr);
      int kills = plan.kills;
      for (;;) {
        int oldest = -1;
        for (int i = 0; i < SimCastle::kWraps; ++i)
          if (c.w[i].phase == SimCastle::kAbout && (oldest < 0 || c.w[i].since < c.w[oldest].since)) oldest = i;
        if (oldest < 0 || kills == 0) break;
        c.w[oldest].phase = SimCastle::kFalling;
        c.w[oldest].t = c.bodies_hurried ? 15.f : 73.f;
        ++c.kills;
        c.last_kill = c.frame;
        if (c.frame - c.w[oldest].since > c.longest) c.longest = c.frame - c.w[oldest].since;
        if (kills > 0) --kills;
      }
      bool gone = false;
      for (const auto& x : c.w) gone = gone || x.phase == SimCastle::kGone;
      if (c.sweep_hurried && gone && c.sweep < 300.f) c.sweep = 300.f;
      if (c.gates && c.about() == 0 && c.frame - c.last_kill >= 180) {
        if (!c.gate10) {
          c.gate10 = true;
          c.last_kill = c.frame;
          if (c.about() < 5)
            for (int i = 0x65; i <= 0x6B; ++i) c.send(i);
          ++c.gates_open;
        } else if (!c.gate0f) {
          c.gate0f = true;
          c.last_kill = c.frame;
          for (int i : {0x5D, 0x5E, 0x5F, 0x4D, 0x4E}) c.send(i);
          ++c.gates_open;
        }
      }
      const uint32_t up = c.areas_in_turn ? (0x00010001u << (tick & 15)) | 0x4000000u : 0xFFFFFFFFu;
      for (auto& g : c.groups) counts_before.push_back(g.count);
      std::vector<SimCastle::Phase> before;
      for (const auto& x : c.w) before.push_back(x.phase);
      c.step(up);
      if (c.give_back)  // the next tick: each group's rise against the wraps that took (a new one alive there)
        for (size_t g = 0; g < c.groups.size(); ++g) {
          int took = 0;
          for (int i : c.groups[g].wraps) took += before[i] != SimCastle::kAbout && c.w[i].phase == SimCastle::kAbout;
          const int back = c.groups[g].count - counts_before[g] - took;
          if (back > 0) {
            c.groups[g].count -= back;
            c.given += back;
          }
        }
    }
    got[r] = c.kills;
    std::printf("castle (%s): %d of %d killed in %d s at 60 fps; %d sends refused, %d given back; gates opened %d; "
                "the longest a Ganado stood before its kill %.1f s\n", runs[r].what, c.kills, c.total(), f / 60,
                c.refused, c.given, c.gates_open, c.longest / 60.0);
  }
  // Every area up at once, the bursts lose one-shot sends (no count to give back).
  CHECK(got[0] < 348 && got[2] == 348 - 37 && got[3] == 348 && got[4] < 348,
        "the castle: %d as the user's game had it (expected fewer than 348), %d without the gates (311), %d with "
        "everything (348), %d with every area up (fewer than 348)", got[0], got[2], got[3], got[4]);
}

// The water stage (room 404): merc_water() over stand-ins (its work, the enemy
// list's entries, the pool's Ganados, the player), then merc_water_set() and
// merc_water_release() on a stand-in scene list - the game's own shape and test
// (FUN_006b3540, FUN_0058b440) find the player in the areas it puts up and in no
// other, and every trigger comes back byte for byte.
static void run_water(uintptr_t base) {
  const uintptr_t rel = base - 0x400000;
  auto u32 = [&](uintptr_t va) -> uint32_t& { return *reinterpret_cast<uint32_t*>(va + rel); };
  auto addr = [](const void* p) { return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p)); };
  const uintptr_t pg = u32(0xC06F3C);
  static uint8_t pg_saved[0x8690];
  std::memcpy(pg_saved, reinterpret_cast<const void*>(pg), sizeof(pg_saved));
  uint32_t* const slots[] = {&u32(0xC7BFEC), &u32(0xC67728), &u32(0xC57054), &u32(0xBFDB0C), &u32(0xBFDB10),
                             &u32(0xBFDB08), &u32(0xBFDB18)};
  uint32_t slots_was[7];
  for (int i = 0; i < 7; ++i) slots_was[i] = *slots[i];
  *reinterpret_cast<uint32_t*>(pg + 0x54) |= 0x40000000;  // the Mercenaries
  *reinterpret_cast<uint8_t*>(pg + 0x20) = 3;             // their main loop
  uint16_t& room = *reinterpret_cast<uint16_t*>(pg + 0x4FAC);
  room = 0x404;
  alignas(8) static uint8_t work[0x30];
  std::memset(work, 0, sizeof(work));
  *slots[0] = addr(work);
  *reinterpret_cast<int32_t*>(work + 0) = 4;     // about
  work[4] = 0x20;                                 // the id it counts: its spawner has run
  *reinterpret_cast<int32_t*>(work + 8) = 40;    // its sends and those about as it began
  *reinterpret_cast<int32_t*>(work + 0x2C) = 3;  // zone 18's counter
  constexpr int kStride = 0xE08, kN = 4;
  alignas(8) static uint8_t pool[kN * kStride];
  std::memset(pool, 0, sizeof(pool));
  for (int i = 0; i < kN; ++i) {
    *reinterpret_cast<uint32_t*>(pool + i * kStride) = 0xB1035C + rel;  // cEm10
    *reinterpret_cast<uint32_t*>(pool + i * kStride + 4) = 1;          // in use
    pool[i * kStride + 0x100] = 0x20;
  }
  *slots[3] = kN;
  *slots[4] = kStride;
  *slots[5] = *slots[6] = addr(pool);
  auto entry = [&](int e, bool sent) {
    uint8_t* const p = reinterpret_cast<uint8_t*>(pg + 0x5410 + e * 0x20);
    p[0] = sent ? 2 : 0;
    p[1] = 0x20;
    *reinterpret_cast<uint16_t*>(p + 0x18) = 0x404;
  };
  for (int e = 0xA0; e <= 0xFE; ++e) entry(e, false);
  for (int e = 0xC8; e <= 0xD1; ++e) entry(e, true);  // the start group's
  // Zone 18's five: D2 not sent; D8 sent, its body gone; D9 sent, its body not gone yet;
  // DA sent with no Ganado; DB sent, its body gone but not a Ganado's class.
  for (const int e : {0xD8, 0xD9, 0xDA, 0xDB}) entry(e, true);
  const int ganado_of[kN] = {0xD8, 0xD9, 0xDB, 0xB4};
  for (int i = 0; i < kN; ++i) {
    pool[i * kStride + 0x3A0] = static_cast<uint8_t>(ganado_of[i]);
    pool[i * kStride + 0x7F5] = i != 1;
  }
  *reinterpret_cast<uint32_t*>(pool + 2 * kStride) = 0xB1035C + rel + 4;  // another class
  entry(0xB4, true);  // a one-shot area's, sent
  entry(0xFB, true);  // the once-at-30's
  alignas(8) static uint8_t player[0x900];
  std::memset(player, 0, sizeof(player));
  const float at[3] = {500.f, 9000.f, -30000.f};
  std::memcpy(player + 0x94, at, sizeof(at));
  *slots[2] = addr(player);
  player[0xFC] = 0;
  player[0xFD] = 0x10;  // on a ladder
  game::MercWater w;
  const bool read = game::merc_water(&w);
  const bool ladder = w.ladder;
  player[0xFD] = 1;
  game::MercWater w2;
  game::merc_water(&w2);
  const game::MercWaterZone& z18 = w.zone[0];
  const bool zones = read && w.started && w.about == 4 && w.sends == 40 && w.zones == 7 && z18.at == 0x18 &&
                     z18.sent == 3 && z18.ends == 15 && z18.entry[0] == 0xD2 && z18.entry[4] == 0xDB &&
                     z18.ready[0] && z18.ready[1] && !z18.ready[2] && !z18.ready[3] && !z18.ready[4] &&
                     w.zone[2].at == 0x17 && w.zone[2].ends == 10 && w.zone[6].at == 0x08;
  const bool rest = w.lab_at == 30 && w.lab_done && w.oneshots == 17 && w.oneshots_sent == 1 && w.start == 10 &&
                    w.most == 156 && ladder && !w2.ladder;
  CHECK(zones && rest, "merc_water(): read %d, its zones %d (zone 18: %d of %d, ready %d%d%d%d%d), the rest %d (the "
        "once-at-30 at %d %s, one-shots %d of %d, start %d, most %d, ladder %d then %d)", read, zones, z18.sent, z18.ends,
        z18.ready[0], z18.ready[1], z18.ready[2], z18.ready[3], z18.ready[4], rest, w.lab_at,
        w.lab_done ? "done" : "not", w.oneshots_sent, w.oneshots, w.start, w.most, ladder, w2.ladder);

  // The scene's list: the stage's fourteen spawn areas (kind 0), one of them (13) an event's instead.
  const int numbers[] = {0x08, 0x09, 0x0A, 0x0C, 0x16, 0x17, 0x18, 0x0F, 0x10, 0x11, 0x15, 0x12, 0x13, 0x14};
  constexpr int kAts = sizeof(numbers) / sizeof(numbers[0]);
  alignas(8) static uint8_t scene[0x100], ats[kAts][0xA0], before[kAts][0xA0];
  std::memset(scene, 0, sizeof(scene));
  *slots[1] = addr(scene);
  *reinterpret_cast<uint32_t*>(scene + 0x8C) = addr(ats[0]);
  for (int i = 0; i < kAts; ++i) {
    uint8_t* const t = ats[i];
    std::memset(t, 0, 0xA0);
    *reinterpret_cast<uint32_t*>(t) = i + 1 < kAts ? addr(ats[i + 1]) : 0xFFFFFFFFu;
    t[4] = t[5] = 1;  // a four-cornered area, far from the player
    const float x = 20000.f + 3000.f * i, z = -60000.f;
    const float shape[11] = {3000.f, 2000.f, 750.f, x, z, x + 2000.f, z, x + 2000.f, z + 1500.f, x, z + 1500.f};
    std::memcpy(t + 8, shape, sizeof(shape));
    t[0x34] = 3;
    t[0x35] = numbers[i] == 0x13 ? 2 : 0;
    t[0x36] = static_cast<uint8_t>(numbers[i]);
    t[0x38] = 2;
    t[0x39] = 1;
    std::memcpy(before[i], t, 0xA0);
  }
  using ShapeFn = void(__cdecl*)(void* out, void* trigger);
  using InsideFn = int(__cdecl*)(void* shape, const float* point);
  const auto shape_of = reinterpret_cast<ShapeFn>(0x6B3540 + rel);
  const auto inside = reinterpret_cast<InsideFn>(0x58B440 + rel);
  auto in = [&](int i) {
    uint32_t shape[16] = {};
    shape_of(shape, ats[i]);
    const float point[3] = {at[0], at[1] + 100.f, at[2]};
    return (inside(shape, point) & 0xFF) != 0;
  };
  auto index = [&](int number) {
    for (int i = 0; i < kAts; ++i)
      if (numbers[i] == number) return i;
    return -1;
  };
  // Up: zone 17 (the third to run) and the one-shot areas; the stage's count of those about put at 7.
  const bool set = game::merc_water_set(1u << 2, true, 7);
  bool up_ok = set && *reinterpret_cast<int32_t*>(work) == 7;
  for (int i = 0; i < kAts; ++i) {
    const int n = numbers[i];
    const bool event = n == 0x13, oneshot = n >= 0x0F && n <= 0x15 && n != 0x13, up = n == 0x17 || oneshot;
    if (event) up_ok = up_ok && !std::memcmp(ats[i], before[i], 0xA0);  // not a plain area: left alone
    else if (up) up_ok = up_ok && (ats[i][0x34] & 1) && in(i);
    else up_ok = up_ok && !(ats[i][0x34] & 1) && !in(i) && !std::memcmp(ats[i] + 8, before[i] + 8, 44);
  }
  // Then nothing up, the count not lowered.
  const bool down = game::merc_water_set(0, false, 2) && *reinterpret_cast<int32_t*>(work) == 7 &&
                    !(ats[index(0x17)][0x34] & 1) && !(ats[index(0x0F)][0x34] & 1) && !in(index(0x17));
  game::merc_water_set(1u << 6, true, -1);
  game::merc_water_release();
  bool back = true;
  for (int i = 0; i < kAts; ++i) back = back && !std::memcmp(ats[i], before[i], 0xA0);
  room = 0x400;
  const bool outside = !game::merc_water(&w) && !game::merc_water_set(1, true, -1);
  room = 0x404;
  CHECK(up_ok && down && back && outside, "the water stage's areas: up (zone 17, the one-shot areas, the count put "
        "at 7) %d, all down after (the count not lowered) %d, put back byte for byte %d, nothing outside the stage %d",
        up_ok, down, back, outside);
  game::merc_census("the test's water stage");

  for (int i = 0; i < 7; ++i) *slots[i] = slots_was[i];
  std::memcpy(reinterpret_cast<void*>(pg), pg_saved, sizeof(pg_saved));
  std::printf("water stage run: its zones, one-shot areas and once-at-30 read from stand-ins; its areas put up round the "
              "player, down, and back as they were\n");
}

// Kill enemies as they appear in the water stage (mercwater.h) over a stand-in
// stage, as its code reads (game.h, MercWater): its start group of 10 about as it
// begins; from its spawner's second frame, each frame the once-at-30 (once work
// +8 - work +0 reaches 30), the seven zones in the order they run (each, with its
// area up, sending its five in order while fewer than 10 are about - an entry not
// sent yet by setEm, a sent one's Ganado reset once its body has gone - counting
// each, and done once a frame's sends bring it to its end), then the spawner
// counting those about again and running its one-shot areas once each (4, 3, 2,
// 3, 2, 3 Ganados; 0F, 10 and 12 with no more than 10 about, the others 15). On a
// ladder every send is skipped (a one-shot's and the once-at-30's lost). The tick
// before the frame kills every Ganado about (it dies as the frame's enemies move)
// and hurries the bodies (a fall 15 units, one starting to melt each frame, a
// melt 10 units). A Ganado counts as about from its send until its fall ends.
struct SimWater {
  enum Phase : uint8_t { kAbout, kHit, kFalling, kWaiting, kMelting, kGone };
  struct G {
    Phase phase = kAbout;
    float t = 0.f;
  };
  struct Zone {
    int ends = 15, sent = 0;
    bool done = false;
    int g[5] = {-1, -1, -1, -1, -1};  // its entries' Ganados (-1: not sent yet)
  };
  static constexpr int kZones = 7, kOneshots = 6;
  static constexpr int kOneshotN[kOneshots] = {4, 3, 2, 3, 2, 3}, kGate[kOneshots] = {10, 10, 15, 10, 15, 15};
  std::vector<G> g;
  Zone zone[kZones];  // in the order they run; the third is zone 17, done at 10
  bool oneshot_done[kOneshots] = {}, lab_done = false;
  int w0 = 0, w2 = 0, frame = 0, kills = 0, sent = 0, lost = 0;
  float dt = 0.5f;
  bool hurried = true;
  int control = 0;

  SimWater() {
    zone[2].ends = 10;
    for (int i = 0; i < 10; ++i) g.push_back(G{});  // the start group
  }
  int about() const {
    int n = 0;
    for (const G& x : g) n += x.phase <= kFalling;
    return n;
  }
  void come(int* slot) {  // a send: a new Ganado, or the entry's own reset
    if (*slot < 0) {
      *slot = static_cast<int>(g.size());
      g.push_back(G{});
    } else {
      g[*slot] = G{};
    }
    ++w0;
    ++w2;
    ++sent;
  }
  // The once-at-30 and the zones, in the order they run.
  void tasks(uint32_t zones, bool ladder) {
    if (!lab_done && w2 - w0 >= 30) {
      lab_done = true;
      int none = -1;
      if (ladder) ++lost;
      else come(&none);
    }
    for (int k = 0; k < kZones; ++k) {
      Zone& z = zone[k];
      if (z.done || !(zones & (1u << k))) continue;
      for (int i = 0; i < 5; ++i) {
        if (w0 >= 10) break;
        if (ladder) continue;
        if (z.g[i] < 0 || g[z.g[i]].phase == kGone) {
          come(&z.g[i]);
          ++z.sent;
        }
      }
      if (z.sent >= z.ends) z.done = true;
    }
  }
  // The spawner's loop: those about counted again, the one-shot areas.
  void spawner(bool oneshots, bool ladder) {
    w0 = about();
    for (int k = 0; k < kOneshots; ++k) {
      if (oneshot_done[k] || !oneshots || w0 > kGate[k]) continue;
      oneshot_done[k] = true;
      for (int i = 0; i < kOneshotN[k]; ++i) {
        int none = -1;
        if (ladder) ++lost;
        else come(&none);
      }
    }
  }
  // The frame after the tick: `zones` up (a bit each), the one-shot areas up, on a ladder.
  // Frame 1 the spawner waits; frame 2 it starts (its count of those about the 10)
  // and its new tasks run after it; from frame 3 they run before it.
  void step(uint32_t zones, bool oneshots, bool ladder) {
    ++frame;
    if (frame == 2) {
      w0 = w2 = about();
      spawner(oneshots, ladder);
      tasks(zones, ladder);
    } else if (frame >= 3) {
      tasks(zones, ladder);
      spawner(oneshots, ladder);
    }
    // The enemies move: the hit die; falls, the wait to melt (one at a time), melts.
    for (G& x : g) {
      if (x.phase == kHit) {
        x.phase = kFalling;
        x.t = 73.f;
        ++kills;
      } else if (x.phase == kFalling && (x.t -= dt) <= 0.f) {
        x.phase = kWaiting;
      } else if (x.phase == kMelting && (x.t -= dt) <= 0.f) {
        x.phase = kGone;
      }
    }
    for (G& x : g)
      if (x.phase == kWaiting && control == 0) {
        x.phase = kMelting;
        x.t = hurried ? 10.f : 78.f;
        control = 15;
        break;
      }
    if (control > 0) --control;
  }
  // The tick: every Ganado about killed, the bodies hurried.
  void tick() {
    for (G& x : g)
      if (x.phase == kAbout) x.phase = kHit;
    if (!hurried) return;
    for (G& x : g)
      if (x.phase == kFalling && x.t <= 73.f - 15.f) x.phase = kWaiting;
    control = 0;
  }
  game::MercWater read(bool ladder) const {
    game::MercWater w;
    w.started = frame >= 2;
    w.about = w0;
    w.sends = w2;
    w.zones = kZones;
    for (int k = 0; k < kZones; ++k) {
      w.zone[k].at = k;
      w.zone[k].sent = zone[k].sent;
      w.zone[k].ends = zone[k].ends;
      for (int i = 0; i < 5; ++i) w.zone[k].ready[i] = zone[k].g[i] < 0 || g[zone[k].g[i]].phase == kGone;
    }
    w.lab_at = 30;
    w.lab_done = lab_done;
    w.ladder = ladder;
    return w;
  }
};

static void run_water_sim() {
  struct Run {
    const char* what;
    int fps;
    bool planned, every_area, walk, ladders;
  };
  const Run runs[] = {
      {"the plan, 60 fps", 60, true, false, false, false},
      {"the plan, 30 fps", 30, true, false, false, false},
      {"the plan, 60 fps, the player on ladders now and then", 60, true, false, false, true},
      {"every area up all the time, 60 fps", 60, false, true, false, false},
      {"the player's walk zone by zone, never on the tower's top, 60 fps", 60, false, false, true, false},
  };
  int got[5] = {};
  bool all_most = true, over = false;
  for (int r = 0; r < 5; ++r) {
    SimWater s;
    s.dt = runs[r].fps == 60 ? 0.5f : 1.f;
    int walking = 0, f = 0, last_kill = 0;
    for (; f < runs[r].fps * 300; ++f) {
      const bool ladder = runs[r].ladders && ((f / 40) % 7 == 3);
      s.tick();
      uint32_t zones = 0;
      bool oneshots = false;
      if (runs[r].planned) {
        const mercwater::Plan p = mercwater::plan(s.read(ladder));
        zones = p.zones;
        oneshots = p.oneshots;
        if (p.about_at_least > s.w0) s.w0 = p.about_at_least;
      } else if (runs[r].every_area) {
        zones = 0x7F;
        oneshots = true;
      } else if (runs[r].walk) {
        while (walking < SimWater::kZones && s.zone[walking].done) ++walking;
        zones = walking < SimWater::kZones ? 1u << walking : 0;
        oneshots = true;
        s.oneshot_done[3] = true;  // the tower's top (12): never reached
      }
      const int before = s.kills;
      s.step(zones, oneshots, ladder);
      if (s.kills != before) last_kill = f;
      over = over || s.sent + 10 > 156;
      if (f - last_kill > runs[r].fps * 20) break;  // nothing for 20 s: all it sends has come
    }
    got[r] = s.kills;
    char zl[96];
    int n = 0;
    for (const auto& z : s.zone) n += std::snprintf(zl + n, sizeof(zl) - n, "%s%d", n ? " " : "", z.sent);
    if (r < 3)
      for (const auto& z : s.zone) all_most = all_most && z.sent == z.ends + 4;
    std::printf("water (%s): %d of 156 killed, the last %.1f s in; zones' sends %s; sends lost on ladders %d\n",
                runs[r].what, s.kills, last_kill / static_cast<double>(runs[r].fps), zl, s.lost);
  }
  CHECK(got[0] == 156 && got[1] == 156 && got[2] == 156 && all_most && !over && got[3] < 156 && got[4] == 125,
        "the water stage: %d, %d and %d with the plan (156 each, every zone at its most %d, never past 156 %d); %d with "
        "every area up (fewer than 156); %d walking zone by zone without the tower's top (125, the user's game)", got[0],
        got[1], got[2], all_most, !over, got[3], got[4]);
}

int main(int argc, char** argv) {
  if (argc < 2) {
    std::printf("usage: test_discover <bio4.exe>\n");
    return 2;
  }
  HMODULE exe = LoadLibraryExA(argv[1], nullptr, DONT_RESOLVE_DLL_REFERENCES);
  if (!exe) {
    std::printf("could not map %s (error %lu)\n", argv[1], GetLastError());
    return 2;
  }
  const uintptr_t base = reinterpret_cast<uintptr_t>(exe);
  std::printf("bio4.exe mapped at %08X\n", static_cast<unsigned>(base));
  if (!relocate(exe)) {
    std::printf("could not relocate the image\n");
    return 2;
  }
  CHECK(game::discover_in(exe), "discovery: %s", game::status_text());
  // Every cheat's patch site, prepared (its bytes re-checked as it was).
  for (int i = 0; i < static_cast<int>(game::Site::kCount); ++i) {
    mem::Patch* p = game::patch(static_cast<game::Site>(i));
    CHECK(p, "patch site %s not prepared", game::site_name(static_cast<game::Site>(i)));
    if (p) std::printf("site %-45s +%X\n", game::site_name(static_cast<game::Site>(i)), static_cast<unsigned>(p->at - base));
  }
  const uintptr_t rel = base - 0x400000;
  CHECK(game::tick_return() == 0x654AED + rel, "tick returns to +%X, expected +254AED",
        static_cast<unsigned>(game::tick_return() - base));
  CHECK(game::tick_return_late() == 0x654B13 + rel, "the tick's second point returns to +%X, expected +254B13",
        static_cast<unsigned>(game::tick_return_late() - base));
  // Automatic QTEs: the action buttons' QTE kinds 2-4 and 9-10 go to kind 13's
  // code (0x58A465, at this base), whose answer becomes "pressed".
  if (mem::Patch* k13 = game::patch(game::Site::kQteKind13)) {
    CHECK(k13->at == 0x58A46E + rel && k13->replacement == std::vector<uint8_t>({0xB0, 0x01}),
          "kind 13's answer at +%X", static_cast<unsigned>(k13->at - base));
    for (game::Site s : {game::Site::kQteKinds, game::Site::kQteKindsLR}) {
      const mem::Patch* p = game::patch(s);
      if (!p) continue;
      const size_t n = s == game::Site::kQteKinds ? 3 : 2;
      bool to13 = p->replacement.size() == n * 4;
      for (size_t i = 0; to13 && i < n; ++i)
        to13 = *reinterpret_cast<const uint32_t*>(&p->replacement[i * 4]) == 0x58A465 + rel;
      CHECK(to13, "%s does not send its kinds to kind 13's code", game::site_name(s));
      CHECK(game::needs(s) == game::Site::kQteKind13, "%s does not lean on kind 13's answer", game::site_name(s));
    }
  }
  // The kills that go round LifeDownSet2: where each site is, at this base, and
  // the truck's partner sites leaning on the heavy hit's.
  const struct {
    game::Site s;
    uintptr_t va;
  } kills[] = {{game::Site::kKillFlag1, 0x5B62BA},    {game::Site::kKillFlag2, 0x5BDE73},
               {game::Site::kCrushRock, 0x5D5F38},    {game::Site::kCrushTruck, 0x563B18},
               {game::Site::kAshleyBlast, 0x75EF89},  {game::Site::kAshleyStomp, 0x4AF8A3},
               {game::Site::kAshleyTruck1, 0x563C19}, {game::Site::kAshleyTruck2, 0x564048},
               {game::Site::kAshleyRock, 0x5DA3F5}};
  for (const auto& k : kills) {
    const mem::Patch* p = game::patch(k.s);
    CHECK(p && p->at == k.va + rel, "%s at +%X, expected +%X", game::site_name(k.s),
          p ? static_cast<unsigned>(p->at - base) : 0u, static_cast<unsigned>(k.va - 0x400000));
  }
  for (game::Site s : {game::Site::kAshleyTruck1, game::Site::kAshleyTruck2})
    CHECK(game::needs(s) == game::Site::kAshleyBlast, "%s does not lean on the heavy hit's site", game::site_name(s));
  // The village's pickups' events: each patch spans its function's checks of the
  // ten, from the first check's 0A to the last's, and changes only those to FF.
  const struct {
    game::Site s;
    uintptr_t first, last;
    int checks;
  } events[] = {{game::Site::kMercEvent1, 0x7D35BD, 0x7D3663, 3},
                {game::Site::kMercEvent2, 0x7D370D, 0x7D3852, 5},
                {game::Site::kMercEvent3, 0x7D391D, 0x7D39C3, 3}};
  for (const auto& ev : events) {
    mem::Patch* p = game::patch(ev.s);
    CHECK(p && p->at == ev.first + rel && p->original.size() == ev.last - ev.first + 1, "%s at +%X, %d bytes - "
          "expected +%X, %d", game::site_name(ev.s), p ? static_cast<unsigned>(p->at - base) : 0u,
          p ? static_cast<int>(p->original.size()) : 0, static_cast<unsigned>(ev.first - 0x400000),
          static_cast<int>(ev.last - ev.first + 1));
    if (!p || p->original.size() != p->replacement.size()) continue;
    int changed = 0;
    bool only = true;
    for (size_t i = 0; i < p->original.size(); ++i) {
      if (p->original[i] == p->replacement[i]) continue;
      ++changed;
      const uint8_t* const op = reinterpret_cast<const uint8_t*>(p->at + i - 2);  // cmp dword [eax],imm8
      only = only && p->original[i] == 0x0A && p->replacement[i] == 0xFF && op[0] == 0x83 && op[1] == 0x38;
    }
    CHECK(changed == ev.checks && only, "%s changes %d bytes (expected its %d checks' 0A to FF)", game::site_name(ev.s),
          changed, ev.checks);
    CHECK(p->apply() && p->revert(), "%s did not go in and come out", game::site_name(ev.s));
  }
  run_countdown(base);
  run_save_request(base);
  run_blast(base);
  run_gigante(base);
  run_krauser(base);
  run_qte(base);
  run_statue(base);
  run_aim(base);
  run_spread(base);
  run_hold(base);
  run_stance(base);
  run_gallery(base);
  run_gallery_games(base);
  run_merc_combo(base);
  run_merc_kill(base);
  run_merc_memory_game(base);
  run_castle(base);
  run_water(base);
  run_merc_memory_sim();
  run_castle_sim();
  run_water_sim();
  CHECK(game::item_rules_known(), "item rules not known");
  CHECK(game::case_calls_known(), "case calls not known");
  CHECK(game::card_known(), "card not known");
  // The rules as read from the exe, against items.h (generated from the same tables).
  int mismatch = 0;
  for (int id = 0; id < items::kCount; ++id)
    if (game::item_kind(id) != items::kItems[id].kind) ++mismatch;
  CHECK(!mismatch, "%d item kinds differ from items.h", mismatch);
  // The panel's filing: every item once in kByRank, in rank and bank order,
  // under a group of its own bank; and the panel's pickers split what they
  // offer by the piece (the case's Add list those with one, the key items',
  // the files' and the treasures' those without), so every item offered that
  // the case takes (all but the off-grid types) has a piece for it, and none of
  // the rest has one. The panel's Files section takes every item the game calls
  // a file, and its headings must agree: those are the items under Files.
  bool filed[items::kCount] = {};
  int misfiled = 0, no_piece = 0, off_grid_piece = 0, files_split = 0;
  for (int r = 0; r < items::kCount; ++r) {
    const int id = items::kByRank[r];
    if (id < 0 || id >= items::kCount || filed[id]) {
      ++misfiled;
      continue;
    }
    filed[id] = true;
    const items::Def& d = items::kItems[id];
    if (d.rank != r || d.group >= items::kGroupCount || items::kGroups[d.group].bank != d.bank ||
        (r > 0 && d.bank < items::kItems[items::kByRank[r - 1]].bank))
      ++misfiled;
    const int type = items::counts(game::item_kind(id), 5).type;
    const bool off_grid = type == items::kKeyItem || type == items::kFile || type == items::kMap ||
                          type == items::kTreasure || type == items::kGem || type == items::kBottleCap ||
                          type == items::kBonus;
    if (d.offered && !off_grid && !d.w) {
      ++no_piece;
      std::printf("offered, but no piece: %d %s\n", id, d.name);
    }
    if (d.offered && off_grid && d.w) {
      ++off_grid_piece;
      std::printf("offered, off the grid, but with a piece: %d %s\n", id, d.name);
    }
    if ((d.type == items::kFile) != (d.group < items::kGroupCount && !std::strcmp(items::kGroups[d.group].name, "Files"))) {
      ++files_split;
      std::printf("a file not under Files, or under Files and no file: %d %s\n", id, d.name);
    }
  }
  CHECK(!misfiled, "%d items misfiled in items.h's ranks and groups", misfiled);
  CHECK(!no_piece, "%d offered case items have no piece", no_piece);
  CHECK(!off_grid_piece, "%d offered off-grid items have a piece", off_grid_piece);
  CHECK(!files_split, "%d items split between the Files section and the Files heading", files_split);
  // The Grenades and Attachments tabs: the three grenades, and every part (the
  // two leftover ones, not offered, among them).
  int banked = 0;
  for (int id = 0; id < items::kCount; ++id) {
    const items::Def& d = items::kItems[id];
    const bool grenade = id == 1 || id == 2 || id == 14;
    if ((d.bank == items::kBankGrenades) != grenade || (d.bank == items::kBankAttachments) != (d.type == items::kPart)) {
      ++banked;
      std::printf("under the wrong tab: %d %s (%s)\n", id, d.name, items::bank_name(d.bank));
    }
  }
  CHECK(!banked, "%d items under the wrong one of the Grenades and Attachments tabs", banked);
  // GLOBAL_WK is static (pG points into .data from the start), zeroed here: a
  // difficulty of 0 would read as Amateur, which doubles every magazine. The
  // game sets it before anything reads it; the test sets Normal.
  game::Counters before;
  CHECK(game::counters(&before), "GLOBAL_WK not readable");
  game::set_difficulty(5);
  CHECK(game::charge(35, 1) == 10 && game::charge(35, 6) == 25, "Handgun capacity %d..%d, expected 10..25",
        game::charge(35, 1), game::charge(35, 6));
  CHECK(game::charge(55, 7) == 0x8000, "Handcannon's exclusive capacity is not infinite (%d)", game::charge(55, 7));
  CHECK(game::max_level(35, 0) == 6 && game::max_level(35, 1) == 3, "Handgun max levels %d/%d",
        game::max_level(35, 0), game::max_level(35, 1));
  CHECK(game::weapon_ammo_item(35) == 4, "Handgun loads item %d, expected 4 (Handgun Ammo)", game::weapon_ammo_item(35));
  CHECK(game::weapon_ammo_item(44) == 24, "Shotgun loads item %d, expected 24", game::weapon_ammo_item(44));
  // What keeps upgrade levels and loaded rounds of its own (items.h's
  // keeps_state, which the item box goes by): a weapon - the type a new entry's
  // set-up, FUN_00707780, gives a levels word and a magazine - with a row in the
  // game's charge or level table, as read here. Not ammo, a herb, a grenade or a
  // part, whatever their cItem holds there.
  int state_differs = 0;
  for (int id = 0; id < items::kCount; ++id) {
    const bool weapon = items::counts(game::item_kind(id), 5).type == items::kWeapon &&
                        (game::charge(id, 1) != -1 || game::max_level(id, 0) > 0);
    if (weapon != items::keeps_state(id)) {
      ++state_differs;
      std::printf("keeps levels and rounds: %d %s - items.h %d, the exe %d\n", id, items::name(id), items::keeps_state(id),
                  weapon);
    }
  }
  CHECK(!state_differs, "%d items differ from items.h in keeping levels and rounds", state_differs);
  CHECK(items::keeps_state(35) && items::keeps_state(16) && !items::keeps_state(4) && !items::keeps_state(6) &&
            !items::keeps_state(1) && !items::keeps_state(53) && !items::keeps_state(67) && !items::keeps_state(12),
        "the Handgun and the Bowgun keep levels and rounds; Handgun Ammo, a Green Herb, a Hand Grenade, the Rocket "
        "Launcher, the Stock (TMP) and the Plaga Sample do not");
  // Last, as it changes the image: a frame limiter of another tool's in place
  // of the game's frame wait - here as one that NOPs 0x654A1E..0x654B0A, the
  // frame loop's SetThreadAffinityMask call with it, and calls its own frame
  // wait instead. No other signature is in those bytes, and the tick goes by
  // its second point, the same frame's QueryPerformanceCounter call.
  {
    std::memset(reinterpret_cast<void*>(0x654A1E + rel), 0x90, 0x654B0A - 0x654A1E);
    const struct {
      const char* name;
      const char* pattern;
      int matches;
    } sigs[] = {
#define SIG_ROW(id, pattern, matches, what) {#id, pattern, matches},
        RE4CC_SIGS(SIG_ROW)
#undef SIG_ROW
    };
    const mem::Range text = mem::section(exe, ".text");
    int broken = 0;
    for (const auto& sig : sigs) {
      const int n = static_cast<int>(mem::find_all(text, sig.pattern, 8).size());
      const bool want = std::strcmp(sig.name, "kSigTick") == 0 ? n == 0 : n == sig.matches;
      if (!want) {
        ++broken;
        std::printf("with the frame wait replaced: %s found %d time(s)\n", sig.name, n);
      }
    }
    CHECK(!broken, "%d signature(s) change with the frame wait's code replaced", broken);
    CHECK(game::discover_in(exe), "discovery with the frame wait replaced: %s", game::status_text());
    CHECK(game::tick_return() == 0 && game::tick_return_late() == 0x654B13 + rel,
          "with the frame wait replaced the tick returns to +%X / +%X, expected none / +254B13",
          static_cast<unsigned>(game::tick_return() ? game::tick_return() - base : 0),
          static_cast<unsigned>(game::tick_return_late() ? game::tick_return_late() - base : 0));
  }
  std::printf("%s (%d failure(s))\n", g_fail ? "FAILED" : "all checks pass", g_fail);
  return g_fail ? 1 : 0;
}
