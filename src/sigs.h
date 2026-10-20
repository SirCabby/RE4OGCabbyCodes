#pragma once

// Every byte signature the mod finds bio4.exe's code and data by. One table,
// read by both src/game.cpp and tools/check_sigs.py (which checks each one
// against the exe on disk - bio4.exe is not packed, so what is on disk is what
// runs). Each entry is X(id, pattern, matches, what):
//   id       the name game.cpp uses
//   pattern  IDA-style bytes, ?? = any byte
//   matches  how many times the pattern is in .text (all must agree on what is
//            read out of them)
//   what     what it is, with the operands game.cpp reads out of it
// The offsets inside each pattern that game.cpp reads are written beside the
// code that reads them; the VAs in CLAUDE.md are build 10112090's only. The exe
// has DYNAMIC_BASE, so on Windows it moves: nothing is ever an absolute address.
#define RE4CC_SIGS(X)                                                                                                   \
  /* --- globals ------------------------------------------------------------------------------------------------ */ \
  X(kSigGlobal, "A1 ?? ?? ?? ?? B9 FF FF FF 7F 21 48 ?? A1", 1,                                                         \
    "mov eax,[pG]; mov ecx,7FFFFFFF; and [eax+?],ecx; mov eax,[pG] - +1 and +14: the GLOBAL_WK pointer's slot")        \
  X(kSigItemMgr, "80 B9 C0 4F 00 00 10 0F 84 ?? ?? ?? ?? B9", 2,                                                        \
    "cmp [ecx+4FC0],10 (the weapon number); je; mov ecx,ItemMgr - +14: the item manager (cItemMgr, static)")             \
  X(kSigSubScreen, "68 ?? ?? ?? ?? E8 ?? ?? ?? ?? 68 00 00 00 F0 E8", 1,                                                \
    "push SubScreenWk; call memclr; push F0000000 - +1: the sub screen's work (its +2AA is the attache case size)")      \
  X(kSigCockpit, "FF FE FF FF B9", 1, "and ...,FFFFFEFF; mov ecx,Cckpt - +5: the HUD (its +98 is the CountDown)")        \
  X(kSigOption, "A2 ?? ?? ?? ?? A2 ?? ?? ?? ?? A1 ?? ?? ?? ?? 81 48 ?? ?? ?? ?? ?? E9", 1,                              \
    "mov [OptionOpenFlag],al ... - +1: the byte the option screen sets while it is up")                                 \
  X(kSigCard, "39 1D ?? ?? ?? ?? 75 0B FF 15 ?? ?? ?? ?? A3 ?? ?? ?? ?? 89 35 ?? ?? ?? ?? EB 06 89 1D", 1,              \
    "if (!storage) storage = SteamRemoteStorage(); card = new cCard - +2/+15 the storage slot, +10 the import, +21 the " \
    "cCard pointer's slot")                                                                                             \
  X(kSigCardFiles,                                                                                                      \
    "8B 0D ?? ?? ?? ?? 8B 89 E8 04 00 00 0F BE C3 83 BC 81 74 1E 00 00 00 74 ?? 69 C0 FC CA 00 00 81 BC 08 EC 1E 00 00 " \
    "02 04 00 00 75 ?? 53 68 ?? ?? ?? ?? C6 83 ?? ?? ?? ?? 01",                                                         \
    1,                                                                                                                  \
    "the file header scan (card+4E8 buffer, +1E74 flags, CAFC per file, +1EEC version 402) - +2 the cCard slot, +52 "    \
    "the per-file 'exists' bytes")                                                                                      \
  X(kSigCheckpoint,                                                                                                     \
    "A1 ?? ?? ?? ?? 8B 0D ?? ?? ?? ?? 53 0F B7 98 62 84 00 00 56 0F B7 B0 A0 4F 00 00 57 0F B7 B8 60 84 00 00 8B 80 A4 " \
    "4F 00 00",                                                                                                         \
    1,                                                                                                                  \
    "the continue (a death's retry): reads the death counts and the play time, then restores the checkpoint - +1 pG's " \
    "slot, +7 the checkpoint work's slot (+8 its copy of GLOBAL_WK's save region)")                                     \
  /* --- a save asked for, and the scheduler's start it is handed to (the fix of a save turned away) ----------- */ \
  X(kSigSaveSystem,                                                                                                     \
    "80 3D ?? ?? ?? ?? 00 75 ?? A1 ?? ?? ?? ?? 81 48 04 98 00 00 00 6A 01 68 ?? ?? ?? ?? 6A 01 E8 ?? ?? ?? ?? 6A 01 E8",  \
    1,                                                                                                                  \
    "a save of the system data alone asked for (FUN_00964260: the clear times' records as an ending's results come "    \
    "up, an option changed, the Mercenaries' and Assignment Ada's ends): pG+4 |= 98 (80: the system data only, no "     \
    "file list), then the card's task handed to the scheduler's level 1, which turns it away while the level holds "   \
    "a task - and the flags stay - +10 pG's slot, +24 the card's task, +30 the call to the level's start")              \
  X(kSigSaveFile,                                                                                                       \
    "A8 20 74 0C A1 ?? ?? ?? ?? 81 48 04 98 00 00 00 8A 45 08 8B 0D ?? ?? ?? ?? 6A 01 68 ?? ?? ?? ?? 6A 01 88 81 A2 "   \
    "4F 00 00 E8",                                                                                                      \
    1,                                                                                                                  \
    "a save to a file asked for (FUN_009641c0: a typewriter, a chapter's end, an ending's clear save): its flags into " \
    "pG+4 (8, 10, 20, 40), then the same task to the same level - +5/+21 pG's slot, +28 the card's task, +40 the "    \
    "call to the level's start")                                                                                        \
  X(kSigTaskStart,                                                                                                      \
    "55 8B EC 8B 4D 08 8B C1 69 C0 48 03 00 00 80 B8 ?? ?? ?? ?? 00 74 ?? 51 68 ?? ?? ?? ?? E8 ?? ?? ?? ?? 68 ?? ?? ?? " \
    "?? 68 45 01 00 00 68 ?? ?? ?? ?? E8 ?? ?? ?? ?? 83 C4 14 33 C0 5D C3 8B 4D 0C 8B 55 10 8D 80 ?? ?? ?? ?? C7 80 38 " \
    "03 00 00 ?? ?? ?? ?? 89 88 3C 03 00 00 89 50 14 C6 40 07 06 66 C7 40 04 01 0F 5D C3",                             \
    1,                                                                                                                  \
    "the scheduler's start of a task at a level of its own (FUN_006c5c10, scheduler.cpp): 'TASK DON'T EXEC' and 0 "   \
    "while the level's state byte is not 0, else the task (+33C) and its argument (+14) put in, the state 1 - +16 the " \
    "levels' state bytes (the table + 4), +69 the table (0x348 bytes a level); +23: the 34 bytes of the two empty "     \
    "debug prints as it turns a task away, eax the level's offset (the fix of a save turned away goes there)")         \
  /* --- code the cheats patch ---------------------------------------------------------------------------------- */ \
  X(kSigHurtLeon, "0F BF 86 B4 4F 00 00 3B C7 7D 02 8B F8 66 29 BE B4 4F 00 00", 1,                                     \
    "LifeDownSet2, the player: min(hp, dmg); sub [esi+4FB4],di - +13: the subtraction (God mode)")                      \
  X(kSigHurtAshley, "0F BF 86 B8 4F 00 00 3B C7 7D 02 8B F8 66 29 BE B8 4F 00 00", 1,                                   \
    "LifeDownSet2, the partner: sub [esi+4FB8],di - +13: the subtraction (Ashley takes no damage)")                     \
  X(kSigHurtEnemy, "0F BF 86 24 03 00 00 3B C7 7D 02 8B F8 66 29 BE 24 03 00 00", 1,                                    \
    "LifeDownSet2, an enemy: cmp hp,dmg; jge; mov dmg,hp - +7: the compare (One hit kills)")                             \
  /* --- El Gigante's health, which goes round LifeDownSet2 (One hit kills) --------------------------------------- */ \
  X(kSigHurtGigante,                                                                                                    \
    "66 39 5F 26 5F 5B 75 6A 8A 86 2E 03 00 00 3C 17 74 21 3C 2A 74 1D 3C 2E 74 10 8B 45 FC F7 D8 03 C0 66 01 86 24 "   \
    "03 00 00 EB 09 33 C9 66 89 8E 24 03 00 00 F7 86 08 04 00 00 00 20 00 00 74 25 66 83 BE 24 03 00 00 00 7E 33",      \
    1,                                                                                                                  \
    "El Gigante's hit check (FUN_004b07c0), past the hit's effects: the part hit is the parasite (3F)? else jne to "    \
    "the body's (+6: cmp [esi+324],0; jg on, the death below it), where no damage comes off; on the parasite twice "    \
    "the damage ([ebp-4]) comes off the health, none for a flash (17, 2A), all of it for the P.R.L. 412 (2E) - +0: "    \
    "the 42 bytes One hit kills replaces, +42: the health zeroed, +51: on to the death below 1")                        \
  X(kSigHurtGiganteKnife, "8B 86 C0 03 00 00 83 C4 28 66 83 80 24 03 00 00 BA C7 86 44 04 00 00 01 00 00 00", 1,        \
    "the knife on El Gigante's back (FUN_004b5b40, the player's callback through the climb): a slash, add "             \
    "[gigante+324],-70 - +9: the add (One hit kills)")                                                                  \
  /* --- Krauser's health, which LifeDownSet2 keeps at 1 in the first part of his fight (One hit kills) ----------- */ \
  X(kSigHurtKrauser,                                                                                                    \
    "8A 96 2E 03 00 00 66 C7 86 2C 03 00 00 00 01 80 FA 10 75 07 C6 86 2D 03 00 00 11 8B 86 40 03 00 00 8B CE "         \
    "89 45 FC E8 ?? ?? ?? ?? 85 C0 74 ?? 80 FA 19 0F 84",                                                               \
    1,                                                                                                                  \
    "Krauser's hit check (FUN_00548200), a hit waiting (the weapon, +340 the part); mutated (types 2 and 3) a hit "     \
    "on his armoured arm glances off - +38 the arm's test (FUN_005479d0), +45: je on to the hit's damage when it is "   \
    "no arm (One hit kills: -> jmp); at its target the damage (FUN_00546ba0), then LifeDownSet2 with 'survive at 1' "   \
    "unless mutated")                                                                                                   \
  /* --- the kills that go round LifeDownSet2 (God mode, Ashley takes no damage) ---------------------------------- */ \
  X(kSigKillFlag, "F6 46 0A 08 74 0F 8B ?? ?? ?? ?? ?? 33 ?? 66 89 ?? B4 4F 00 00 6A 00 6A 00", 2,                       \
    "an enemy's attack on the player (EmAtkSetDamagePL FUN_005b61a0, EmAtkHitCk FUN_005bde20): after its damage, the "  \
    "attack's 'kill' flag (+A bit 8) zeroes the HP, then PlDamageSet - +8 pG's slot, +14: the store of 0 (God mode)")   \
  X(kSigCrushRock, "E8 ?? ?? ?? ?? 8B 0D ?? ?? ?? ?? 33 C0 50 50 6A 08 66 89 81 B4 4F 00 00 E8", 1,                      \
    "a rock (cEmRock, FUN_005d5e90) reaching the player: the HP zeroed, then PlDamageSet(8) - +7 pG's slot, +17: the "   \
    "store of 0, +24 PlDamageSet (God mode)")                                                                           \
  X(kSigCrushTruck, "A1 ?? ?? ?? ?? D9 05 ?? ?? ?? ?? 33 D2 66 89 90 B4 4F 00 00 A1 ?? ?? ?? ?? 83 EC 08", 1,             \
    "the truck (cEm3b, FUN_00563a80) running the player over: the HP zeroed, then PlDamageSet(8) - +1 pG's slot, +13: " \
    "the store of 0 (God mode)")                                                                                        \
  X(kSigAshleyBlast,                                                                                                    \
    "D9 05 ?? ?? ?? ?? D8 9E 3C 03 00 00 DF E0 F6 C4 05 7A ?? 6A 3C C7 86 FC 00 00 00 00 07 00 00", 1,                  \
    "the partner's hit check (cSubChar::damageCheck, FUN_0075edf0), a heavy hit (0D, 12, 13: explosions, the mounted "  \
    "gun): nearer than its radius (+2, 3000 squared) 9999 damage and a fall that ends in her death whatever her HP, "    \
    "else she ducks (routine 7) - +17: jp to the fall (Ashley takes no damage)")                                        \
  X(kSigAshleyStomp,                                                                                                    \
    "80 B8 2D 03 00 00 00 0F 85 ?? ?? ?? ?? A1 ?? ?? ?? ?? 80 B8 CB 4F 00 00 02 0F 84 ?? ?? ?? ?? 8D 4D", 1,            \
    "El Gigante's stomp on the partner (FUN_004af860): skipped in her armour, else her HP zeroed under its foot - +14 "  \
    "pG's slot, +25: je past it in the armour (Ashley takes no damage)")                                                \
  X(kSigAshleyTruck, "A1 ?? ?? ?? ?? 80 B8 CB 4F 00 00 02 75 ?? ?? ?? F4 00 00 00", 2,                                   \
    "the truck (cEm3b: FUN_00563a80, FUN_00563eb0) running the partner over: in her armour a heavy hit (13) on her, "   \
    "else her HP zeroed - +1 pG's slot, +12: jne to the zeroing (Ashley takes no damage)")                              \
  X(kSigAshleyRock, "83 BE 54 04 00 00 00 74 ?? 80 B9 CB 4F 00 00 02 74 ?? 6A 00 8B CE E8", 1,                          \
    "a rock (cEmRock, FUN_005da3b0) reaching the partner: it passes her armour, else it crushes her - +16: je past it "  \
    "in the armour (Ashley takes no damage)")                                                                           \
  X(kSigAmmo, "8D 14 C5 F8 FF FF FF 83 E1 07 0B D1 66 89 57 08 5E 5F B0 01", 1,                                         \
    "cItemMgr::trigger: ammo - 1 back into the weapon - +12: the store (Infinite ammo)")                                 \
  X(kSigArrows, "6A 72 8B CB E8 ?? ?? ?? ?? 5E 5F 5B 8B E5 5D C2 04 00", 1,                                                         \
    "cItemMgr::trigger, Krauser's bow: use up one arrow - the push and the call (Infinite ammo)")                         \
  X(kSigBolts, "6A 11 8B CB E8 ?? ?? ?? ?? 5E 5F 5B 8B E5 5D C2 04 00", 1,                                                          \
    "cItemMgr::trigger, the bowgun: use up one bolt - the push and the call (Infinite ammo)")                            \
  X(kSigPlayTime, "8B 0D ?? ?? ?? ?? 2B 41 5C 01 86 A4 4F 00 00", 1,                                                    \
    "the play time's update: now - [pG+5C] onto [pG+4FA4] - +2 pG's slot, +9: the add (Freeze play time)")              \
  X(kSigSaveCount,                                                                                                      \
    "A1 ?? ?? ?? ?? BE 01 00 00 00 66 01 B0 9C 4F 00 00 A1 ?? ?? ?? ?? B9 E7 03 00 00 66 39 88 9C 4F 00 00", 1,         \
    "the save builder: mov esi,1; add [pG+4F9C],si (save_cnt, which the load and save lists show); then the cap, "      \
    "999 - +1/+18 pG's slot, +10: the add (Save without counting), +23 the cap")                                        \
  X(kSigBuy,                                                                                                            \
    "8B 45 10 39 18 0F 8C ?? ?? ?? ?? B9 40 00 00 00 66 3B CF 75 ?? 8B 0D ?? ?? ?? ?? 81 89 8C 52 00 00 00 00 00 10 "   \
    "29 18",                                                                                                            \
    1,                                                                                                                  \
    "the merchant's purchase (FUN_00714700): if (*money < price) refuse, then *money -= price - +23 pG's slot, +37: "   \
    "the sub (Infinite money)")                                                                                         \
  X(kSigTuneUp,                                                                                                         \
    "8B 47 24 39 05 ?? ?? ?? ?? 75 0B 50 B9 ?? ?? ?? ?? E8 ?? ?? ?? ?? 8B 4F 28 A1 ?? ?? ?? ?? 29 88 A8 4F 00 00", 1,   \
    "the merchant's tune-up (FUN_00797680): sub [pG+4FA8],cost - +5 ItemMgr+C, +13 ItemMgr, +26 pG's slot, +30: "       \
    "the sub (Infinite money)")                                                                                         \
  X(kSigShotA,                                                                                                          \
    "85 F6 75 ?? 8D 4E 01 85 DB 74 16 A1 ?? ?? ?? ?? 01 88 6C 84 00 00 A1 ?? ?? ?? ?? 01 88 70 84 00 00 A1 ?? ?? ?? ?? "\
    "01 88 74 84 00 00 A1 ?? ?? ?? ?? 01 88 78 84 00 00",                                                               \
    1,                                                                                                                  \
    "a shot is counted (one of two sites): if it hit, hits +1 (+846C/+8470), then shots +1 (+8474/+8478) - +12/+23/"    \
    "+34/+45 pG's slot, +9: the je past the hits (Misses don't count sends it past the shots too)")                     \
  X(kSigShotB,                                                                                                          \
    "B9 01 00 00 00 7E 16 A1 ?? ?? ?? ?? 01 88 6C 84 00 00 A1 ?? ?? ?? ?? 01 88 70 84 00 00 A1 ?? ?? ?? ?? 01 88 74 84 "\
    "00 00 A1 ?? ?? ?? ?? 01 88 78 84 00 00",                                                                           \
    1,                                                                                                                  \
    "the other site that counts a shot, the same four adds - +8/+19/+30/+41 pG's slot, +5: the jle past the hits "      \
    "(Misses don't count)")                                                                                             \
  X(kSigDeaths,                                                                                                         \
    "8B 15 ?? ?? ?? ?? 46 66 89 B2 A0 4F 00 00 A1 ?? ?? ?? ?? 47 66 89 B8 60 84 00 00 8B 0D ?? ?? ?? ?? 43 66 89 99 "   \
    "62 84 00 00",                                                                                                      \
    1,                                                                                                                  \
    "the continue (FUN_0063D890) counts the death: +4FA0 (the area's retries), +8460 (the chapter's deaths), +8462 "    \
    "(the game's) - +2/+15/+29 pG's slot, +19: inc edi (the chapter), +33: inc ebx (the game) (Deaths don't count)")    \
  X(kSigCountdown, "D9 46 08 D8 61 70 D9 5E 08", 1,                                                                     \
    "CountDown::move: frame -= delta - +3: the fsub (Freeze countdown timer)")                                          \
  X(kSigClockKeep,                                                                                                      \
    "68 ?? ?? ?? ?? B9 ?? ?? ?? ?? E8 ?? ?? ?? ?? 57 E8 ?? ?? ?? ?? 57 57 6A 0A 6A 01 50 E8 ?? ?? ?? ?? 83 C4 18 C6 05 "\
    "?? ?? ?? ?? 01",                                                                                                   \
    1,                                                                                                                  \
    "the option screen's opening (FUN_00743AD0) keeps the CountDown aside, then puts OptionOpenFlag up - +1 the "       \
    "backup (the Cockpit's +AC), +6 the CountDown, +10 the call (FUN_005A3CF0), +38 OptionOpenFlag")                    \
  X(kSigClockBack, "6A 2A 68 FF 00 00 00 B9 ?? ?? ?? ?? E8 ?? ?? ?? ?? 68 ?? ?? ?? ?? B9 ?? ?? ?? ?? E8 ?? ?? ?? ?? C3",\
    1,                                                                                                                  \
    "the option screen's closing (FUN_00740260) ends by putting the CountDown back from that backup - +18 the "         \
    "backup, +23 the CountDown, +27 the call (FUN_005A3D80)")                                                           \
  /* --- Steady aim: the aim's sway (FUN_00684330: the weapon's "bure", held aim drifting round its centre) ------- */  \
  X(kSigAimSway,                                                                                                        \
    "E8 ?? ?? ?? ?? D9 1D ?? ?? ?? ?? E8 ?? ?? ?? ?? D9 1D ?? ?? ?? ?? EB 02 DD D8 A1 ?? ?? ?? ?? D9 05 ?? ?? ?? ?? D8 "\
    "0D ?? ?? ?? ?? 8B 4D 14 D8 48 70 D8 06 D9 1E D9 05 ?? ?? ?? ?? D8 0D ?? ?? ?? ?? D8 48 70 D8 01 D9 19",            \
    1,                                                                                                                  \
    "the drift: two random factors drawn (+7/+18 where they go), then aim += factor * speed * delta - +27 pG's slot, "  \
    "+31/+53: fld factor (up-down, left-right; +33/+55 the same two globals)")                                          \
  X(kSigAimKick,                                                                                                        \
    "A8 02 74 ?? E8 ?? ?? ?? ?? D8 0D ?? ?? ?? ?? 8B 15 ?? ?? ?? ?? D8 0D ?? ?? ?? ?? D8 4A 70 D8 47 28 D9 1E E8 ?? ?? "\
    "?? ?? D8 0D ?? ?? ?? ?? A1 ?? ?? ?? ?? 8B 4D 14 D8 0D ?? ?? ?? ?? D8 48 70 D8 47 2C 5F D9 19",                     \
    1,                                                                                                                  \
    "a shot (flag 2): aim = centre + random * sway * scale * delta - +2 the je to the drift, +17/+47 pG's slot, "       \
    "+21/+54: fmul scale (up-down, left-right; +23/+56 the same global)")                                               \
  /* --- Steady aim: the shots' random spread (call random; fmul scale -> fstp st(0); fldz) ----------------------- */  \
  X(kSigSpreadGun,                                                                                                      \
    "D9 05 ?? ?? ?? ?? D9 5D F0 E8 ?? ?? ?? ?? DC 0D ?? ?? ?? ?? D9 5D F4 E8 ?? ?? ?? ?? DC 0D ?? ?? ?? ??", 2,         \
    "a handgun's shot (FUN_008fd410; and FUN_009189b0, the Red9 and Matilda's class): direction (-50000, random * "     \
    "200, random * 200) in the hand's frame - +2 the forward length, +14/+28: fmul spread (+16/+30 the same scale)")    \
  X(kSigSpreadAuto,                                                                                                     \
    "D9 05 ?? ?? ?? ?? D9 1D ?? ?? ?? ?? E8 ?? ?? ?? ?? DC 0D ?? ?? ?? ?? D9 1D ?? ?? ?? ?? E8 ?? ?? ?? ?? DC 0D ?? "   \
    "?? ?? ?? 68 ?? ?? ?? ??",                                                                                          \
    2,                                                                                                                  \
    "a machine gun's shot (FUN_00920290: the first, then held fire), the same into a static vector - +2 the forward "   \
    "length, +8 the vector (+25 its second float, +41 pushed), +17/+34: fmul spread (+19/+36 the same scale)")          \
  X(kSigSpreadShotgun,                                                                                                  \
    "E8 ?? ?? ?? ?? DC 0D ?? ?? ?? ?? D8 45 C4 D9 5D C4 E8 ?? ?? ?? ?? DC 0D ?? ?? ?? ?? 8B 96 D8 07 00 00", 1,         \
    "a shotgun's blast (FUN_00925040): the centre pellet's direction += random * 100, twice - +5/+22: fmul spread "     \
    "(+7/+24 the same scale; the other pellets' rings are the shotgun's own and are left alone)")                       \
  X(kSigSpreadMounted,                                                                                                  \
    "E8 ?? ?? ?? ?? D8 0D ?? ?? ?? ?? D9 5D E4 E8 ?? ?? ?? ?? D8 0D ?? ?? ?? ?? 6A 01 8B CE D9 5D E8", 1,               \
    "the mounted machine gun (SetObj43, FUN_0072bdc0): its barrel's direction (random * 1000, random * 1000, "          \
    "50000) - +5/+19: fmul spread (+7/+21 the same scale, a float)")                                                    \
  X(kSigSpreadThrow, "E8 ?? ?? ?? ?? DC 0D ?? ?? ?? ?? 8D 4D F0 51 D9 5D F0 8D 55 E4 D9 EE", 1,                         \
    "a thrown grenade or egg (FUN_00739e40): its sideways speed = random * 15 - +5: fmul spread")                       \
  /* --- Steady aim's hold: the laser's and the shots' lookups of the gun's and the hand's parts ----------------- */   \
  X(kSigPlayer, "8B 0D ?? ?? ?? ?? 8A 91 2F 01 00 00 88 96 2F 01 00 00 A1 ?? ?? ?? ?? 0F B7 80 AC 4F 00 00", 1,         \
    "a weapon's move (FUN_0073f320) copies the player's +12F - +2 the player's slot (cPlayer*), +19 pG's slot")         \
  X(kSigHoldLaser,                                                                                                      \
    "80 FA 0E 74 09 80 FA 2E 74 04 33 C0 EB 05 B8 01 00 00 00 50 E8 ?? ?? ?? ?? 8D 78 0C A1 ?? ?? ?? ?? 0F B6 80 C0 4F "\
    "00 00",                                                                                                            \
    1,                                                                                                                  \
    "the laser's line (FUN_0073dc60): the gun's part 0 (1 for weapons E and 2E), its matrix at +C - +19: push index; "  \
    "call part (+21: the part lookup, thiscall, ret 4), +29 pG's slot")                                                 \
  X(kSigHoldHand, "6A 0A 8B CE E8 ?? ?? ?? ?? D9 05 ?? ?? ?? ?? D9 5D E4 8D 78 0C D9 05 ?? ?? ?? ?? 8D 45 E4", 2,       \
    "a handgun's shot (FUN_008fd410, FUN_009189b0): the player's part 10, the hand - +0: push 10; mov ecx,esi; call "   \
    "part")                                                                                                             \
  X(kSigHoldAuto, "6A 0A 8B CE E8 ?? ?? ?? ?? D9 EE D9 1D ?? ?? ?? ?? 68 ?? ?? ?? ?? D9 05 ?? ?? ?? ?? 8D 78 0C", 2,    \
    "a machine gun's shot (FUN_00920290's two): the hand, into the static start vector (+13, pushed +18) - +0: push "   \
    "10; mov ecx,esi; call part")                                                                                       \
  X(kSigHoldShotgun, "6A 0A 8B CE E8 ?? ?? ?? ?? 89 45 88 85 DB 75 08 D9 05 ?? ?? ?? ?? EB 06 D9 05", 1,                \
    "a shotgun's pellet (FUN_00925040's loop): the hand, each pellet - +0: push 10; mov ecx,esi; call part")            \
  /* --- Steady aim's stance: each aim's set-up starts its idle loop (the blend helper's two motions, attribute 4) -- */ \
  X(kSigAimLoopFar,                                                                                                     \
    "6A 04 6A 00 8B C8 6A 03 6A 00 03 D1 52 8B 90 ?? ?? ?? ?? 8B 80 ?? ?? ?? ?? 03 D1 52 03 C1 50 56 B9 ?? ?? ?? ?? E8 " \
    "?? ?? ?? ?? D9 05 ?? ?? ?? ?? 51 B9 ?? ?? ?? ?? D9 1C 24 E8 ?? ?? ?? ?? 8B CE E8",                                 \
    3,                                                                                                                  \
    "an aim's set-up (the handguns', Wesker's, Krauser's bow's): its idle loop with the aim-up and aim-down motions "   \
    "(blended in over 3 frames, no sub-sequence), then the blend set and the first move - +0: push 4 (the loop's "     \
    "attribute), +33/+50 the blend helper, +37 the motion set, +44 the blend, +57 the blend's set, +64 the move")       \
  X(kSigAimLoopNear,                                                                                                    \
    "6A 04 6A 00 8B C8 6A 03 6A 00 03 D1 52 8B 50 ?? 8B 40 ?? 03 D1 52 03 C1 50 56 B9 ?? ?? ?? ?? E8 ?? ?? ?? ?? D9 05 " \
    "?? ?? ?? ?? 51 B9 ?? ?? ?? ?? D9 1C 24 E8 ?? ?? ?? ?? 8B CE E8",                                                   \
    6,                                                                                                                  \
    "the same with short table offsets (the P.R.L. 412's, Ada's bowgun's, a grenade's, the Red9 and Matilda's, the "  \
    "mine thrower's, the rocket launcher's) - +0: push 4, +27/+44 the helper, +31, +38, +51, +58")                      \
  X(kSigAimLoopShotgun,                                                                                                 \
    "6A 04 6A 00 8B C8 6A 03 6A 00 03 D1 52 8B 90 ?? ?? ?? ?? 8B 40 ?? 03 D1 52 03 C1 50 56 B9 ?? ?? ?? ?? E8 ?? ?? ?? " \
    "?? D9 05 ?? ?? ?? ?? 51 B9 ?? ?? ?? ?? D9 1C 24 E8 ?? ?? ?? ?? 8B CE E8",                                          \
    1, "the same, the shotguns' - +0: push 4, +30/+47 the helper, +34, +41, +54, +61")                                  \
  X(kSigAimLoopAuto,                                                                                                    \
    "6A 04 83 E2 FD 6A 00 83 C2 03 52 8B 90 ?? ?? ?? ?? 8B C8 6A 00 03 D1 52 8B 50 ?? 8B 40 ?? 03 D1 52 03 C1 50 56 B9 " \
    "?? ?? ?? ?? E8 ?? ?? ?? ?? D9 05 ?? ?? ?? ?? 51 B9 ?? ?? ?? ?? D9 1C 24 E8 ?? ?? ?? ?? 8B CE E8",                  \
    1,                                                                                                                  \
    "the same, the machine guns' (blended in over 3 frames, or none while +428 is set) - +0: push 4, +38/+55 the "     \
    "helper, +42, +49, +62, +69")                                                                                       \
  /* --- Automatic QTEs: the prompts' own check, and the scenes that poll their prompt's buttons themselves ------- */  \
  X(kSigQteCheck,                                                                                                       \
    "53 8B 5D 08 0F B6 4B 0F 0F B6 C0 99 56 8B 35 ?? ?? ?? ?? 89 55 FC 33 D2 49 57 8B F8 A1 ?? ?? ?? ?? 89 55 FC 89 55 "\
    "FC 83 F9 12 0F 87 ?? ?? ?? ?? FF 24 8D ?? ?? ?? ??",                                                               \
    1,                                                                                                                  \
    "cActionButton::checkButton (FUN_0058a300): movzx ecx,[work+F] (the prompt's kind); dec ecx; cmp ecx,12; ja; jmp "  \
    "[ecx*4+table] - +51: the kinds' jump table (the QTE kinds - 2, 3, 4, 9, 10, 13 - each raise the QTE flag)")        \
  X(kSigQteBoulder, "68 00 00 01 00 6A 00 6A 00 68 00 00 08 00 E8 ?? ?? ?? ?? 83 C4 10 85 C0 74 4F", 1,                 \
    "the boulder run: KeyTrgCheck(A); add esp,10 - +22: test eax,eax (a press)")                                        \
  X(kSigQteSwim,                                                                                                        \
    "68 00 00 01 00 6A 00 6A 00 68 00 00 08 00 E8 ?? ?? ?? ?? 83 C4 10 85 C0 74 ?? A1 ?? ?? ?? ?? 66 83 B8 B4 4F 00 00 "\
    "01",                                                                                                               \
    1, "Del Lago: swimming back to the boat (cmp [pG+4FB4],1 after it) - +22: test eax,eax (a press)")                  \
  X(kSigQteRope,                                                                                                        \
    "FF FE FF FF 68 00 00 01 00 6A 00 6A 00 68 00 00 08 00 E8 ?? ?? ?? ?? 83 C4 10 85 C0 74 06 FF 86 E8 00 00 00", 1,   \
    "Del Lago: the rope (presses counted at +E8) - +26: test eax,eax")                                                  \
  X(kSigQteGigante, "8B 04 ?? ?? ?? ?? ?? 68 00 00 01 00 99 ?? 52 50 E8 ?? ?? ?? ?? 83 C4 10 85 C0 74", 3,              \
    "El Gigante's parasite, the knife: KeyTrgCheck(the button from a table) - +3 the table (the three agree), +24: test "\
    "eax,eax")                                                                                                          \
  X(kSigQteMinecart,                                                                                                    \
    "68 00 00 01 00 6A 00 6A 00 68 00 00 00 80 E8 ?? ?? ?? ?? 83 C4 10 85 C0 74 ?? D9 EE D8 9E 0C 04 00 00", 1,         \
    "the minecart's climb: KeyTrgCheck(OK), presses left at +40C - +22: test eax,eax")                                  \
  X(kSigQteStatueRun,                                                                                                   \
    "C6 45 FF 01 68 00 00 01 00 6A 00 6A 00 68 00 00 08 00 E8 ?? ?? ?? ?? 83 C4 10 85 C0 74 1D ?? ?? ?? ?? ?? ?? ?? ?? "\
    "?? ?? 0F AF 5D 24 D9 EE 01 06 8D 43 FF D9 1F 39 06 7E 02 89 06",                                                   \
    1,                                                                                                                  \
    "the statue chase's run (FUN_00858b20): KeyTrgCheck(A); a press adds the frames since the last one, cut to "        \
    "whole 1/30 s, to the run's counter (+30: mov edi,[ebp+14]; fld [edi]; call ftol - re4_tweaks' FixQTE hooks "       \
    "those 10 bytes), which is held to its top - +26: test eax,eax, +55: jle past the top")                             \
  X(kSigQteStatueJump,                                                                                                  \
    "0F 84 ?? ?? FF FF A1 ?? ?? ?? ?? 81 A0 74 01 00 00 FF FF FF DF A1 ?? ?? ?? ?? 81 88 74 01 00 00 00 00 00 10", 1,   \
    "the statue chase: the jumps (one trigger, the other, both) meet at one je back to 'not yet' - +0: that je")        \
  X(kSigQteStatueEnd,                                                                                                   \
    "6A 04 6A 03 6A 42 6A 00 6A 00 6A 05 6A 0C B9 ?? ?? ?? ?? E8 ?? ?? ?? ?? E8 ?? ?? ?? ?? 85 C0 0F 84", 1,            \
    "the statue chase's last jump: its prompt, then FUN_00859810 (both triggers) - +29: test eax,eax")                  \
  X(kSigQteStatueClimb,                                                                                                 \
    "E8 ?? ?? ?? ?? 83 C4 10 85 C0 74 0B A1 ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? 6A 00 6A 00 6A 02 6A 02 6A 02 6A 00 6A 00 6A "\
    "05 6A 19",                                                                                                         \
    2, "the statue chase: climbing (presses at [C7C718]+168), its prompt after - +8: test eax,eax")                     \
  X(kSigQteKrauser,                                                                                                     \
    "83 FF 02 75 ?? 68 00 00 08 00 E8 ?? ?? ?? ?? 83 C4 10 85 C0 ?? ?? ?? ?? ?? 74 02 ?? ?? 39 30 0F 8C", 1,            \
    "Krauser's knife fight: the last presses ([C7C258]) - +18: test eax,eax")                                           \
  X(kSigQteLasers,                                                                                                      \
    "75 14 8B 0D ?? ?? ?? ?? 81 E1 00 00 01 00 33 C0 0B C1 75 1D EB ?? ?? ?? ?? ?? ?? ?? ?? 74 ?? ?? ?? ?? ?? ?? ?? ?? "\
    "74 ?? 66 C7 05 ?? ?? ?? ?? 00 00",                                                                                 \
    2,                                                                                                                  \
    "the laser room's dodges: both triggers, else past - +29: the first je ('not pressed'), +49 the dodge (the Wii's "  \
    "jne at +18 goes there too)")                                                                                       \
  X(kSigQteLasersLast,                                                                                                  \
    "6A 12 6A 03 6A 02 6A 00 6A 00 6A 05 6A 25 B9 ?? ?? ?? ?? E8 ?? ?? ?? ?? E8 ?? ?? ?? ?? 85 C0 74 ?? FE 86 FE 00 00 "\
    "00",                                                                                                               \
    1, "the laser room's last dodge: its prompt, then FUN_008064b0 (both triggers) - +29: test eax,eax")                \
  X(kSigQteBridge,                                                                                                      \
    "E8 ?? ?? ?? ?? 80 3D ?? ?? ?? ?? 00 74 0D 80 3D ?? ?? ?? ?? 00 0F 85 ?? ?? ?? ?? 6A 00 68 00 00 40 00", 1,         \
    "Saddler's bridges: the prompt, then both triggers - +12: je (one is up), +21: jne (both: the jump)")               \
  X(kSigQteBridgeClimb,                                                                                                 \
    "E8 ?? ?? ?? ?? 83 C4 10 85 C0 74 0B A1 ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? 6A 00 6A 00 6A 02 6A 02 6A 02 6A 00 6A 00 6A "\
    "0C 6A 19",                                                                                                         \
    2, "Saddler's bridges: climbing after the jump (presses at [C7C1C4]+84) - +8: test eax,eax")                        \
  X(kSigQteRobotGrab,                                                                                                   \
    "6A 11 6A 02 6A 02 6A 00 6A 00 6A 05 6A 19 B9 ?? ?? ?? ?? E8 ?? ?? ?? ?? 68 00 00 01 00 6A 00 6A 00 68 00 00 08 00 "\
    "E8 ?? ?? ?? ?? 83 C4 10 85 C0 74",                                                                                 \
    1, "em3a's grab (re4_tweaks: 'Robot'): presses left at +418 - +46: test eax,eax")                                   \
  X(kSigQteShake,                                                                                                       \
    "57 8B 3D ?? ?? ?? ?? 6A 00 6A 00 6A 11 6A 0B 6A 02 6A 00 6A 00 6A 05 6A 2B B9 ?? ?? ?? ?? E8 ?? ?? ?? ?? 33 D2 83 "\
    "3D ?? ?? ?? ?? 01 74 ?? 8B 0D ?? ?? ?? ?? 8B C1 83 E0 0F 0B C2 74 0C FF 87 68 05 00 00",                           \
    1,                                                                                                                  \
    "shaking off a grab (FUN_00680d80, 18 enemies' grabs): each button adds to the player's +568 - +59: je; inc "       \
    "[edi+568] (a direction)")                                                                                          \
  /* --- the shooting gallery (r22c.cpp): its work, its targets (cEmMark, em 3E) and how a hit on one is scored ---- */ \
  X(kSigEmPool,                                                                                                         \
    "A1 ?? ?? ?? ?? 56 33 F6 33 C9 89 75 F0 89 4D D8 85 C0 0F 84 ?? ?? ?? ?? 53 57 8B 7D 10 3B C8 72 07 33 DB 89 5D F4 " \
    "EB 13 A1 ?? ?? ?? ?? 0F AF C1 03 05 ?? ?? ?? ??",                                                                  \
    1,                                                                                                                  \
    "a walk over every enemy (FUN_005baf00): the enemy manager's pool, cManager<cEm> - +1 its count's slot, +41 the "   \
    "stride's, +50 the base's")                                                                                         \
  X(kSigGalleryTargets,                                                                                                 \
    "8B 0D ?? ?? ?? ?? 33 C0 85 C9 74 ?? 8B 51 04 81 E2 01 02 00 00 83 FA 01 75 ?? 80 B9 00 01 00 00 3E 75 ?? 66 83 B9 " \
    "24 03 00 00 00 7E ?? 8A 91 01 01 00 00 80 FA 01 76 ?? 80 FA 03 75 ?? 40 8B 49 08",                                 \
    1,                                                                                                                  \
    "the gallery's count of targets up (FUN_00843160): the live enemies' list (+8 the next), id 3E, HP +324, type "    \
    "+101 - +2 the list's head (the manager's +14)")                                                                    \
  X(kSigGalleryScore,                                                                                                   \
    "A1 ?? ?? ?? ?? 80 38 02 0F 85 ?? ?? ?? ?? 57 8B 7D 08 83 FF 0A 7C 0A C7 40 24 00 00 00 00 5F 5D C3 53 BB 01 00 00 " \
    "00 83 FF 03 75 05 00 58 46 EB 08 83 FF 02 75 03 00 58 48 8B 4D 0C 56 83 F9 04 75 07 BE C8 00 00 00 EB 0C 8D 14 B9 "\
    "03 D7 8B 34 95 ?? ?? ?? ?? 01 70 10",                                                                              \
    1,                                                                                                                  \
    "a hit scored (FUN_008431c0(type, part, ...)): only while the game runs (work+0 == 2); a prop (type 10 on) ends "   \
    "the streak (+24) - +1 the gallery work's slot, +81 the points table (5 ints a type), +85 add [work+10] (the score)")\
  X(kSigGalleryHit,                                                                                                     \
    "0F B7 86 24 03 00 00 66 85 C0 0F 8E ?? ?? ?? ?? 80 BE 2C 03 00 00 00 0F 84 ?? ?? ?? ?? 8A 96 01 01 00 00 80 FA 0A " \
    "72 ?? E8 ?? ?? ?? ?? 8D 8E 28 03 00 00 E8 ?? ?? ?? ?? D9 86 FC 04 00 00 E8 ?? ?? ?? ?? 0F B6 8E 01 01 00 00 50 8B " \
    "86 40 03 00 00 6A 01 83 C0 0C 50 6A 00 51 E8 ?? ?? ?? ?? 83 C4 14 5E C3 8D 48 FF 8A 86 2E 03 00 00 57 66 89 8E 24 "\
    "03 00 00 3C 13 74 ?? 3C 12 74 ?? 80 FA 05 75 ?? 8D 96 A4 04 00 00 39 96 40 03 00 00 75 ?? BF 02 00 00 00 EB ?? 33 "\
    "D2 8D 86 44 03 00 00 39 86 40 03 00 00 0F 94 C2",                                                                  \
    1,                                                                                                                  \
    "a target's hit check (cEmMark, FUN_00589c50): HP left and a hit pending (+32C); the part hit (+340) is the head " \
    "when it is +344 - +90: the call to the scoring (a prop's)")                                                       \
  X(kSigGalleryStep,                                                                                                    \
    "8B 81 D8 04 00 00 8B 00 83 F8 04 77 ?? 33 D2 FF 24 85 ?? ?? ?? ?? B8 04 00 00 00 EB 10 B8 08 00 00 00 EB 09 B8 14 " \
    "00 00 00 EB 02 33 C0 C1 E8 02 03 C0 03 C0 01 81 D8 04 00 00 8B 81 D8 04 00 00 8A 00 88 81 FC 00 00 00",            \
    1,                                                                                                                  \
    "a target's next command (FUN_00589110): +4D8 the command it is on; past one of 0 or 1 (rise, go down) 4 bytes, 2 " \
    "(wait) 8, 3 (move) 14, 4 (stay) none; the new one's number into +FC - +18: the jump table (0..4)")                 \
  X(kSigGalleryWait,                                                                                                    \
    "0F B6 81 FD 00 00 00 83 E8 00 8B 91 D8 04 00 00 74 ?? 48 75 ?? A1 ?? ?? ?? ?? D9 81 DC 04 00 00 D8 60 70 D9 99 DC " \
    "04 00 00",                                                                                                         \
    1,                                                                                                                  \
    "a target's wait (FUN_005894f0): sub-step +FD; the frames left at +4DC, less the frame's delta (pG via +22)")       \
  X(kSigGalleryStreak,                                                                                                  \
    "8B 15 ?? ?? ?? ?? F7 82 24 50 00 00 00 00 00 01 74 07 C7 46 24 00 00 00 00 83 7E 24 05 7C ?? 80 3E 02 75 ?? C7 46 " \
    "24 00 00 00 00 A1 ?? ?? ?? ?? F7 80 74 01 00 00 00 00 00 40 75 ?? 6A 00 DD D8 6A 02 6A 00 6A 00 68 ?? ?? ?? ?? 6A " \
    "14 E8 ?? ?? ?? ?? D9 05 ?? ?? ?? ?? 8B 35 ?? ?? ?? ??",                                                            \
    1,                                                                                                                  \
    "the gallery's loop (FUN_00844a10): a miss (pG+5024 bit 01000000) ends the streak; at five, the streak starts "    \
    "again and, unless one is under way (pG+174 bit 40000000), the bonus target's task - +2/+44 pG's slot, +71 the "   \
    "task, +90 the gallery work's slot")                                                                                \
  /* --- the Mercenaries (mercenaries.cpp): how a kill scores, and the combo's timers in its HUD -------------------- */ \
  X(kSigMercScore,                                                                                                      \
    "A1 ?? ?? ?? ?? F7 40 54 00 00 00 40 0F 84 ?? ?? ?? ?? 56 8B 75 08 83 FE 09 75 0E 8B 4D 0C 01 0D ?? ?? ?? ?? 5E B0 01 " \
    "5D C3 8B 0D ?? ?? ?? ?? D9 EE 57 8B 3D ?? ?? ?? ?? 83 F9 01 75 1F D8 15 ?? ?? ?? ??",                              \
    1,                                                                                                                  \
    "a kill scored (FUN_0070f520(kind, points)), in the Mercenaries only (Flags_SYSTEM 40000000): the combo goes on, "  \
    "the kind's points onto the score and its combo bonus onto what the combo has earned - +1 pG's slot, +32 the "      \
    "score (the work's +24), +43 the combo (+28), +52 the combo's window in frames, +63 the first kill's timer (+30)")  \
  X(kSigMercFirst,                                                                                                      \
    "D9 EE D8 56 30 DF E0 F6 C4 05 7A ?? A1 ?? ?? ?? ?? D9 46 30 D8 60 70 D9 5E 30 D8 56 30 DF E0 F6 C4 01 75 ?? D9 5E " \
    "30 C7 46 28 00 00 00 00",                                                                                          \
    1,                                                                                                                  \
    "the Mercenaries' HUD each frame (FUN_00710c10, esi = the work): the first kill's timer (+30) less the frame's "    \
    "delta, and at 0 the combo (+28) is over - +13 pG's slot, +20: the fsub (Endless combo)")                           \
  X(kSigMercWindow,                                                                                                     \
    "BB 00 00 80 00 85 5E 64 0F 85 ?? ?? ?? ?? D9 EE D8 5E 2C DF E0 F6 C4 05 0F 8A ?? ?? ?? ?? D9 46 2C 8B 0D ?? ?? ?? " \
    "?? D8 61 70 D9 5E 2C D9 46 2C DB 05 ?? ?? ?? ??",                                                                  \
    1,                                                                                                                  \
    "the same: unless the combo is ending (+64 bit 800000), its window (+2C) less the frame's delta - past the blink "  \
    "mark the combo blinks, at 0 it ends - +35 pG's slot, +39: the fsub (Endless combo), +50 the blink mark")           \
  X(kSigMercBonus,                                                                                                      \
    "BF 00 00 10 00 85 7E 64 0F 85 ?? ?? ?? ?? 39 5E 48 0F 8E ?? ?? ?? ?? 39 5E 28 0F 85 ?? ?? ?? ?? 8D 4E 54 53 E8 ?? " \
    "?? ?? ?? 84 C0 0F 84 ?? ?? ?? ??",                                                                                 \
    1,                                                                                                                  \
    "the same: what the combo has earned (+48) is shown and goes onto the score once the combo is over (+28 = 0) and "  \
    "no Bonus Points time runs (+54, FUN_00428820(&+54, 0): within a frame of 0) - +17 the jle (nothing earned), +26: " \
    "the jne on the combo (Endless combo), +36 the call, +43: the je on the Bonus Points time (Bonus time always on)")  \
  X(kSigMercBonusTime,                                                                                                  \
    "F7 46 64 00 00 20 00 0F 85 ?? ?? ?? ?? D9 EE D8 5E 54 DF E0 F6 C4 05 0F 8A ?? ?? ?? ?? A1 ?? ?? ?? ?? D9 46 54 D8 " \
    "60 70 D9 5E 54 D9 46 54 DB 05 ?? ?? ?? ??",                                                                        \
    1,                                                                                                                  \
    "the same: unless it is ending (+64 bit 200000), the Bonus Points time (+54, frames; a pickup's 30 s is 900) less " \
    "the frame's delta - past the blink mark it blinks, at 1 it ends - +23 the jp past it (to kSigMercBonus), +30 pG's " \
    "slot, +37: the fsub (Bonus time always on), +48 the blink mark")                                                   \
  /* --- the Ganados' class (cEm10: every enemy the Mercenaries score) and its hit check ---------------------------- */ \
  X(kSigEm10, "53 56 8B F1 57 C7 06 ?? ?? ?? ?? 8B 86 10 06 00 00 33 FF 3B C7 74 ?? 8B 48 04 81 E1 01 02 00 00", 1,     \
    "cEm10's destructor (FUN_0044b8a0): mov [esi],vtable, then its weapon (+610) let go - +7 the vtable (its +10 the "  \
    "move, which runs the hit check, eax = the enemy, once past its init)")                                             \
  X(kSigEm10Hit,                                                                                                        \
    "56 8B F0 E8 ?? ?? ?? ?? 85 C0 0F 85 ?? ?? ?? ?? F6 46 04 02 57 0F 84 ?? ?? ?? ?? 38 86 2C 03 00 00 0F 85 ?? ?? ?? " \
    "?? 38 86 2D 03 00 00",                                                                                             \
    1,                                                                                                                  \
    "cEm10's hit check (em10DmCk, FUN_00457d30, eax = the enemy): the room's damage areas unless a hit is waiting "     \
    "(+32C, its frames at +32D) - the start")                                                                           \
  X(kSigEm10Damage, "8B FE E8 ?? ?? ?? ?? 6A 00 8B F8 6A 00 57 56 E8 ?? ?? ?? ?? 83 C4 10 66 29 BE BC 07 00 00", 1,     \
    "the hit check's damage: the weapon's by the part hit (FUN_0044ad40), then LifeDownSet2(enemy, damage, 0, 0) - "    \
    "+15 LifeDownSet2")                                                                                                 \
  X(kSigAreaReset,                                                                                                      \
    "A1 ?? ?? ?? ?? F7 80 70 01 00 00 00 00 40 00 0F 85 ?? ?? ?? ?? E8 ?? ?? ?? ?? 8B 0D ?? ?? ?? ?? A1 ?? ?? ?? ?? 89 " \
    "99 7C 01 00 00 8B 15 ?? ?? ?? ?? 83 C0 10 6A 20 50 89 9A 80 01 00 00",                                             \
    1,                                                                                                                  \
    "the scene's area triggers each frame (FUN_006bdd20): unless pG+170 bit 400000, the room's area flags (pG+17C, "    \
    "+180) cleared, then each area the player stands in sets its own again - +1/+28/+45 pG's slot, +37: the clear of "  \
    "+17C, mov [ecx+17C],ebx (0) (Kill enemies as they appear, in the Mercenaries)")                                    \
  /* --- the models' memory (every enemy is a model: its set-up takes a record and its parts from these pools) ------- */ \
  X(kSigModelInfos, "8B 0D ?? ?? ?? ?? 53 57 E8 ?? ?? ?? ?? 8B D8 85 DB 75 ?? 68 ?? ?? ?? ?? 50 50 E8", 1,                \
    "modelInit (FUN_0065eba0): mov ecx,[the model pool's manager]; push tex; push bin; call (a record from it); "      \
    "no record: \"modelInit() cModelInfo alloc failed.\" and 0 - +2 the slot of the pool's manager (a cManager: +4 "  \
    "the records, +8 how many, +C the stride; a record's +4 & 0x601 in use), +20 the message")                         \
  X(kSigModelParts, "8A 87 02 01 00 00 8B 0D ?? ?? ?? ?? 0F B6 F0 56 E8 ?? ?? ?? ?? 89 87 F4 00 00 00 85 C0", 1,      \
    "a model's parts (FUN_0065e9a0): its count (+102, a byte), mov ecx,[the parts pool's manager]; the parts from it " \
    "into +F4 - +8 the slot of the parts pool's manager (a cManager as the model pool's)")                            \
  X(kSigVillageSweep,                                                                                                   \
    "8B 0D ?? ?? ?? ?? BE 01 00 00 00 83 C4 08 89 01 BF 00 00 00 80 39 71 0C 75 ?? 8B 15 ?? ?? ?? ?? 85 BA 74 01 00 00 " \
    "75 ?? 89 41 08 A1 ?? ?? ?? ?? 09 B8 74 01 00 00 01 71 0C 81 79 0C 2C 01 00 00 76 ?? 89 71 0C E8",                 \
    1,                                                                                                                  \
    "the Mercenaries' village each frame (FUN_007d3b00, room 400's function): the Ganados about into its work's +0, "  \
    "then +C (frames since its sweep) + 1 and past 300 the sweep (FUN_007d3a60: cEmMgr::destroy on every Ganado "     \
    "whose body is gone, vtable +48) - +2 the slot of the work, +28/+44 pG's slot, +60 the 300, +69 the sweep's call") \
  X(kSigVillageEvents,                                                                                                  \
    "09 78 04 E8 ?? ?? ?? ?? 68 81 00 00 00 E8 ?? ?? ?? ?? 83 C4 04 3C 01 75 ?? A1 ?? ?? ?? ?? 0F B7 80 AC 4F 00 00 50 " \
    "B9 ?? ?? ?? ?? E8 ?? ?? ?? ?? 85 70 04 75 ?? 8B 0D ?? ?? ?? ?? 0F B7 81 AC 4F 00 00 50 B9 ?? ?? ?? ?? E8 ?? ?? ?? " \
    "?? 09 70 04 E8 ?? ?? ?? ?? 68 82 00 00 00 E8 ?? ?? ?? ?? 83 C4 04 3C 01 75 ?? 8B 15 ?? ?? ?? ?? 0F B7 82 AC 4F 00 " \
    "00 50 B9 ?? ?? ?? ?? E8 ?? ?? ?? ?? F7 40 04 00 00 00 20 75 ?? A1 ?? ?? ?? ?? 0F B7 80 AC 4F 00 00 50 B9 ?? ?? ?? " \
    "?? E8 ?? ?? ?? ?? 81 48 04 00 00 00 20 E8",                                                                         \
    1,                                                                                                                  \
    "the same, its pickups' events: once the item of area trigger 80, 81 or 82 is picked up (FUN_006b9170) and the "  \
    "room's record (FUN_006b0a20 on the manager at +39) has not its bit 80000000, 40000000 or 20000000, the bit set "  \
    "and the event's function, which sends its 3, 5 or 3 entries each only while fewer than 10 are about (cmp "       \
    "[work],0A; jae) - +3, +80, +165 the three calls, +26/+55/+104/+136 pG's slot, +39/+68/+117/+149 the manager")  \
  X(kSigVillageWaves,                                                                                                   \
    "A1 ?? ?? ?? ?? 8B 48 08 85 C9 74 3D 2B 08 83 F9 ?? 72 36 8B 0D ?? ?? ?? ?? F7 81 74 01 00 00 ?? ?? ?? ?? 75 24 6A " \
    "?? E8 ?? ?? ?? ?? 33 FF 83 C4 04 8D 77 ?? E8 ?? ?? ?? ?? 8D 7E ?? 8D 77 ?? E8 ?? ?? ?? ?? A1 ?? ?? ?? ?? 8B 48 08 " \
    "85 C9 74 3D 2B 08 83 F9 ?? 72 36 8B 15 ?? ?? ?? ?? F7 82 74 01 00 00 ?? ?? ?? ?? 75 24 6A ?? E8 ?? ?? ?? ?? 33 FF " \
    "83 C4 04 8D 77 ?? E8 ?? ?? ?? ?? 8D 7E ?? 8D 77 ?? E8 ?? ?? ?? ?? A1 ?? ?? ?? ?? 8B 48 08 85 C9 74 37 2B 08 83 F9 " \
    "?? 72 30 A1 ?? ?? ?? ?? F7 80 74 01 00 00 ?? ?? ?? ?? 75 1F 6A ?? E8 ?? ?? ?? ?? 33 FF 83 C4 04 8D 77 ?? E8 ?? ?? " \
    "?? ?? 8D 7E ?? 8D 77 ?? E8 ?? ?? ?? ?? 5F 5E C3",                                                                   \
    1,                                                                                                                  \
    "the same, its chainsaws' waves, each once: the work's tries less those about past 10, 25 or 35 and the wave's "  \
    "flag (pG+174, bit 80000000 >> n) not up, FUN_004ab510(n) puts it up and FUN_007d0600 sends entries E and E + 1 " \
    "whatever is about (esi: the entry's sent bit and dead mark cleared, setEm, moved to the chainsaws' spot) - the "  \
    "waves at +0, +68, +136: the work's slot +1/+69/+137, pG's +21/+89/+156, the flag +31/+99/+166, n "              \
    "+38/+106/+173, its call +39/+107/+174, E +51/+119/+186 (lea esi,[edi+E], edi 0, then 1 - E +59/+127/+194), "    \
    "the sends +52/+63, +120/+131, +187/+198")                                                                       \
  X(kSigCastleSweep,                                                                                                    \
    "A1 ?? ?? ?? ?? 8B 0D ?? ?? ?? ?? D9 40 70 D8 81 ?? ?? ?? ?? D9 99 ?? ?? ?? ?? D9 05 ?? ?? ?? ?? D8 99 ?? ?? ?? ?? " \
    "DF E0 F6 C4 05 7A 0D D9 E8 D9 99 ?? ?? ?? ?? E9 ?? ?? ?? ?? C3",                                                  \
    1,                                                                                                                  \
    "the Mercenaries' castle each frame (FUN_007d4870, room 402's function): its work's timer + the frame's delta, "  \
    "and past 300 back to 1 and its sweep (0x7d46f0: every wrap whose enemy is in use, not about and gone "          \
    "(ckResetEnable) destroyed) - +1 pG's slot, +7 the slot of the work, +16/+22/+34/+49 the timer, +28 the 300")    \
  X(kSigCastleTask, "55 8B EC 83 EC 24 A1 ?? ?? ?? ?? 33 C5 89 45 FC A1 ?? ?? ?? ?? D9 EE 33 C9 56 89 88 ?? ?? ?? ?? 89 88", \
    1,                                                                                                                  \
    "the castle's spawner (FUN_007d5040, a task): its counters zeroed, then each frame its groups by the area flags " \
    "(pG+17C), each send into a wrap of its own (cEmWrap, 0xC bytes, from the work's +0) while the wrap is free, a "  \
    "counted group's counter + 1 whether the send took (EmSetEvent, FUN_005b2a60, refuses without a free slot) - "   \
    "+17 the slot of the work, +28 the first counter")                                                               \
  X(kSigCastleEvents,                                                                                                   \
    "6A 00 6A 02 6A 00 6A 00 68 ?? ?? ?? ?? 6A 14 E8 ?? ?? ?? ?? E8 ?? ?? ?? ?? 6A 01 6A 00 68 ?? ?? ?? ?? 6A 00 6A 14 " \
    "6A ?? E8 ?? ?? ?? ?? 6A 01 6A 00 68 ?? ?? ?? ?? 6A 00 6A 14 6A ?? E8 ?? ?? ?? ?? 83 C4 48",                       \
    1,                                                                                                                  \
    "the castle's set-up (FUN_007d72d0): its spawner started as a task (+9), then two area triggers given events "    \
    "(SceAtDataSet_exec, FUN_006b4c60(at, 0x14, 0, callback, 0, 1)): +39 the first's trigger (10, the action "        \
    "button by a gate) and +30 its callback (FUN_007d49d0), +59 the second's (0F, walked into) and +50 its "           \
    "(FUN_007d7040), +40/+60 the calls")                                                                              \
  X(kSigBaseFrame,                                                                                                      \
    "8B 0D ?? ?? ?? ?? F7 81 1C 50 00 00 00 10 00 00 0F 85 ?? ?? ?? ?? A1 ?? ?? ?? ?? BA 01 00 00 00 56 BE 00 00 00 80", \
    1,                                                                                                                  \
    "the Mercenaries' base's function each frame (room 403, FUN_007dc120): +2 pG's slot, +23 its work's; its 22 area "  \
    "lists by pG+17C's flags (test; je; call), then two sends once each (FUN_007db150, esi the entry) at 20 and 45 "     \
    "of its tries less those about, their flags in pG+174")                                                             \
  X(kSigCastleDoor, "6A 1B E8 ?? ?? ?? ?? 8B 55 ?? 83 C4 0C F7 82 D0 03 00 00 00 00 00 10 74 ?? A1", 1,                  \
    "the castle's spawner: door object 1B (FUN_0061c810) open (+3D0 0x10000000) before its two Ganados DC, DD - +23 "     \
    "the je past them")                                                                                                 \
  X(kSigWaterTask,                                                                                                      \
    "55 8B EC 51 56 6A 01 E8 ?? ?? ?? ?? 68 ?? 00 00 00 E8 ?? ?? ?? ?? 8B 0D ?? ?? ?? ?? 6A FF 50 89 41 04 E8 ?? ?? ?? " \
    "?? 8B 0D ?? ?? ?? ?? 33 F6 56 6A 02 56 56 68 ?? ?? ?? ?? 6A 14 89 41 08 89 01 89 71 14 89 71 18 89 71 1C 89 71 20 " \
    "89 71 24 89 71 28 89 71 2C E8",                                                                                    \
    1,                                                                                                                  \
    "the Mercenaries' water stage's spawner (room 404, FUN_007e01b0, a task its set-up starts): a frame's wait, then "  \
    "its work (+24 and +41 the slot): +4 the id it counts (entry +13's, FUN_005b1f60), +0 and +8 the Ganados about "    \
    "(FUN_006c1010), its zones' counters from +14 zeroed; its eight tasks started (FUN_006c4d10(0x14, task, 0, 0, 2, "  \
    "0): +53 the first, +85 the call), then each frame the count again and its one-shot areas (FUN_006b4da0)")        \
  X(kSigAtFind,                                                                                                         \
    "55 8B EC A1 ?? ?? ?? ?? 8B 55 08 05 8C 00 00 00 8B 08 83 F9 FF 74 11 8B C1 83 E0 FE F6 C1 01 74 0B 8B 08 83 F9 " \
    "FF 75 EF 33 C0 5D C3 85 C0 74 F8 0F B6 48 36 3B CA 75 D8 5D C3",                                                  \
    1,                                                                                                                  \
    "the scene's area triggers: one by its number (FUN_006b4af0) - [+4] their work, its +8C the list (a word a "      \
    "node: -1 the end, bit 0 a link to follow, else the trigger: +0 the next, +36 its number)")                      \
  /* --- the tick -------------------------------------------------------------------------------------------------- */ \
  X(kSigTick, "D9 58 70 FF 15 ?? ?? ?? ?? 50 FF D7 8B 8D ?? ?? ?? ?? 8B 95 ?? ?? ?? ?? 89 0D", 1,                       \
    "the frame loop, after the frame wait: fstp [pG+70] (the frame's delta); SetThreadAffinityMask(GetCurrentThread(), " \
    "..) - +5 the GetCurrentThread import, +12: the return address the tick is keyed on")                                \
  X(kSigTickLate,                                                                                                       \
    "8D 85 ?? ?? ?? ?? 50 FF D3 DF AD ?? ?? ?? ?? 8D 8D ?? ?? ?? ?? 51 DC 35 ?? ?? ?? ?? DC 25 ?? ?? ?? ?? DD 1D ?? ?? ?? " \
    "?? FF D3",                                                                                                         \
    1,                                                                                                                  \
    "the same frame, a little later (after the frame wait's code): QueryPerformanceCounter(&t) through ebx (WinMain "   \
    "loads it from the import) - +2 and +11 the same counter, +9: the tick's second return address")                    \
  /* --- the approved game calls (the attache case's own) ----------------------------------------------------------- */ \
  X(kSigPutInCase, "0F BE 15 ?? ?? ?? ?? 0F B7 46 20 0F B7 4E 1C 52 50 51 E8", 1,                                       \
    "movsx edx,[SubScreenWk+2AA]; push size,num,id; call PutInCase - +3 the case size byte, +18 the call")               \
  X(kSigItemGet, "56 50 B9 ?? ?? ?? ?? E8 ?? ?? ?? ?? A1", 1,                                                            \
    "mov ecx,ItemMgr; call cItemMgr::get; mov eax,[ItemMgr+18] - +3 ItemMgr, +7 the call, +13 m_pNew")                  \
  X(kSigItemErase, "56 E8 ?? ?? ?? ?? 8A 45 ?? 8B 4D ?? 24 ?? 66 0F", 1, "push item; call cItemMgr::erase - +1 the call") \
  /* --- the game's item rules ---------------------------------------------------------------------------------------- */ \
  X(kSigItemInfo, "0F B7 45 08 3D 0F 01 00 00 0F 87 ?? ?? ?? ?? 0F B6 80 ?? ?? ?? ?? FF 24 85", 1,                     \
    "itemInfo: ids up to 10F; movzx eax,[id+table] - +18: the item -> kind byte table")                                  \
  X(kSigPieces,                                                                                                         \
    "66 8B 0D ?? ?? ?? ?? 56 0F B7 33 33 C0 66 3B CE 74 ?? 0F B7 D1 EB ?? 8D 49 00 B9 FF FF 00 00 66 3B D1 74 ?? 40 8B " \
    "D0 6B D2 58 0F B7 92 ?? ?? ?? ??",                                                                                 \
    1,                                                                                                                  \
    "the case's piece lookup: +3 and +45 the piece table (rows of 0x58: id, width, height, ..; FFFF ends it) - an item " \
    "without a piece cannot go into the case")                                                                          \
  X(kSigCharge,                                                                                                         \
    "8B 15 ?? ?? ?? ?? 33 C0 56 85 D2 7E ?? 66 8B 75 08 B9 ?? ?? ?? ?? 8D A4 24 00 00 00 00 66 3B 31 74 ?? 40 83 C1 16 " \
    "3B C2 7C ?? B8 FF FF 00 00 5E 5D C3 8B 0D ?? ?? ?? ?? 6B C0 0B 03 45 0C 80 B9 7C 84 00 00 01",                     \
    1,                                                                                                                  \
    "WeaponId2ChargeNum: +2 the row count's slot, +18 the table (rows of 11 shorts: id, .., ammo id, capacity by "       \
    "level), +52 pG's slot (amateur doubles the capacity)")                                                             \
  X(kSigMaxLevel, "66 8B 55 08 33 C9 B8 ?? ?? ?? ?? 8B FF 66 3B 10 74 ?? 83 C0 06 41 3D ?? ?? ?? ??", 1,               \
    "WeaponId2MaxLevel: +7 the table, +23 its end (rows: id, max firepower / speed / reload / capacity)")
