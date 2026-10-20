#!/usr/bin/env python3
"""Generate src/items.h - the item table the panel shows - from bio4.exe.

    python3 tools/gen_items.py --exe "$GAME_DIR/bio4.exe"

Each item's kind (the case of itemInfo's switch), whether it has a piece for
the attache case and whether it is a weapon with a magazine or upgrade levels
are read out of the exe (the same tables src/sigs.h finds at run time); the
type each kind means is itemInfo's own constants. The names are the English
game's, written out here: the game keeps its text in compressed archives the
mod does not read. Pure stdlib.
"""
import argparse
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from check_sigs import DEFAULT_EXE, Image, parse_sigs  # noqa: E402

# itemInfo's switch: kind -> type (the second byte it writes). The difficulty-
# dependent counts are items.h's stack_max(), written from the same switch.
KIND_TYPE = {0: 2, 1: 3, 2: 1, 3: 2, 4: 6, 5: 2, 6: 1, 7: 2, 8: 2, 9: 2, 10: 2, 11: 9, 12: 2, 13: 10, 14: 11,
             15: 12, 16: 5, 17: 2, 18: 8, 19: 2, 20: 8, 21: 5, 22: 13, 23: 7}
TYPE_NAME = {1: "weapon", 2: "ammo", 3: "grenade/launcher/knife", 5: "treasure", 6: "recovery", 7: "key item",
             8: "Mercenaries bonus", 9: "weapon part", 10: "file", 11: "map/case", 12: "gem", 13: "bottle cap"}
# The types that live off the case's grid (inventory.cpp's off_grid): everything else needs a piece.
OFF_GRID = (5, 7, 8, 10, 11, 12, 13)

# The panel's banks - the tabs of the item box and of the Add list - and
# the groups the Add list heads its items with, in the order it shows them,
# bank by bank. A group names its items in the order a player knows them: the
# weapons class by class in the merchant's order, the ammo in the same order,
# the treasures and gems in the merchant's price list's (0xC16E90), the files
# by the game's own file numbers, which are the story's order. The rest of the
# items of its types, and its ALSO items, follow by name. The ids are build
# 10112090's.
BANKS = [("kBankWeapons", "Weapons"), ("kBankGrenades", "Grenades"), ("kBankAttachments", "Attachments"),
         ("kBankAmmo", "Ammo"), ("kBankRecovery", "Recovery"), ("kBankKey", "Key items"), ("kBankTreasures", "Treasures")]
WEAPONS, GRENADES, ATTACHMENTS, AMMO, RECOVERY, KEY, TREASURES = range(len(BANKS))
FILES = list(range(172, 184)) + list(range(72, 81)) + list(range(244, 252)) + list(range(262, 267))
TREASURE_LIST = ([88, 137, 89, 138, 90, 91, 92, 93, 94] + list(range(98, 105)) + [184] + list(range(188, 195)) + [198] +
                 list(range(202, 209)) + [143, 152, 112, 147, 144, 145, 150, 155, 156, 154, 157, 158, 159, 209] +
                 list(range(213, 220)))
GROUPS = [
    # (heading, bank, its items in order, the types it takes the rest of)
    ("Handguns", WEAPONS, [35, 37, 33, 39, 3], ()),
    ("Magnums", WEAPONS, [41, 42, 55], ()),
    ("Shotguns", WEAPONS, [44, 148, 45, 71], ()),
    ("Rifles", WEAPONS, [46, 47], ()),
    ("Machine guns", WEAPONS, [48, 62, 52, 83], ()),
    ("Special weapons", WEAPONS, [54, 53, 23, 109, 65, 16, 82], ()),
    ("Other weapons", WEAPONS, [], (1, 3)),  # leftovers, none of them offered
    ("Grenades", GRENADES, [1, 2, 14], ()),
    ("Attachments", ATTACHMENTS, [66, 67, 68, 69, 197, 170], (9,)),  # then two leftover parts, not offered
    ("Ammo", AMMO, [4, 0, 26, 24, 7, 32, 106, 70, 17, 114], (2,)),
    ("Recovery", RECOVERY, [5, 6, 25, 28, 18, 19, 20, 22, 168, 21, 8, 9, 10, 149, 151], (6,)),
    ("Key items", KEY, [], (7,)),
    ("Maps", KEY, [169, 84, 85, 267, 268, 269, 270, 271], (11,)),
    ("Files", KEY, FILES, (10,)),
    ("Treasures", TREASURES, TREASURE_LIST, (5,)),
    ("Gems", TREASURES, [87, 86, 161, 119, 95, 96, 97, 185, 186, 187, 199, 200, 201, 210, 211, 212], (12,)),
    # The shooting gallery's prizes: the collection (a treasure to the game,
    # which the gallery gives) and the 24 caps, in the gallery's order. The
    # gallery counts the caps held only while the collection is (FUN_00842180).
    ("Bottle caps", TREASURES, [162] + list(range(220, 244)), (13,)),
    ("Mercenaries pickups", TREASURES, [], (8,)),  # turned into time and points: none offered
]
ALSO = {12: "Key items"}  # Plaga Sample: a weapon to the game, Assignment Ada's quest item
# The attache case's sizes S, M, L and XL: items to the game (maps, to
# itemInfo), but what they are is the case's size - the sub-screen's set-up
# (FUN_007690C0) sets SubScreenWk+2AA to 0..3 by which of them is held, the
# last one held winning. The panel shows the size as the case's own, and
# offers none of them as an item.
CASES = [124, 125, 126, 127]

# id: (name, offered) - offered = the panel's add lists offer it (Add to the
# case what has a piece, the key items & files' and the treasures' the rest).
# Items the game turns into something else when picked up (pesetas, time,
# points), the GameCube build's leftovers and the unused entries are named for
# an inventory that holds one, but not offered. Not offered either: the guns
# with a part attached (no piece in the case), 63, a silencer for the Handgun,
# and 34, a part the game names "Punisher w/ Silencer" (re4_tweaks: a scope for
# the P.R.L. 412) - leftovers for parts the final game's guns do not take. 27
# and 118 are key items room scripts check for, beside the treasure and the gem
# of those names.
NAMES = {
    0: ("Magnum Ammo", 1), 1: ("Hand Grenade", 1), 2: ("Incendiary Grenade", 1), 3: ("Matilda", 1),
    4: ("Handgun Ammo", 1), 5: ("First Aid Spray", 1), 6: ("Green Herb", 1), 7: ("Rifle Ammo", 1),
    8: ("Chicken Egg", 1), 9: ("Brown Chicken Egg", 1), 10: ("Gold Chicken Egg", 1), 11: ("Luis' Drug (unused)", 0),
    12: ("Plaga Sample", 1), 13: ("Krauser's Knife (unused)", 0), 14: ("Flash Grenade", 1),
    15: ("Salazar Family Insignia", 1), 16: ("Bowgun", 1), 17: ("Bowgun Bolts", 1), 18: ("Mixed Herbs (G+G)", 1),
    19: ("Mixed Herbs (G+G+G)", 1), 20: ("Mixed Herbs (G+R)", 1), 21: ("Mixed Herbs (G+R+Y)", 1),
    22: ("Mixed Herbs (G+Y)", 1), 23: ("Rocket Launcher (Special)", 1), 24: ("Shotgun Shells", 1), 25: ("Red Herb", 1),
    26: ("Handcannon Ammo", 1), 27: ("Hourglass w/ Gold Decor (key item)", 1), 28: ("Yellow Herb", 1),
    29: ("Stone Tablet", 1), 30: ("Lion Ornament", 1), 31: ("Goat Ornament", 1), 32: ("TMP Ammo", 1),
    33: ("Punisher", 1), 34: ("Punisher w/ Silencer (unused)", 0), 35: ("Handgun", 1), 36: ("Handgun w/ Silencer", 0),
    37: ("Red9", 1), 38: ("Red9 w/ Stock", 0), 39: ("Blacktail", 1), 40: ("Blacktail w/ Silencer", 0),
    41: ("Broken Butterfly", 1), 42: ("Killer7", 1), 43: ("Killer7 w/ Silencer", 0), 44: ("Shotgun", 1),
    45: ("Striker", 1), 46: ("Rifle", 1), 47: ("Rifle (semi-auto)", 1), 48: ("TMP", 1),
    49: ("Activation Key (blue)", 1), 50: ("TMP w/ Stock", 0), 51: ("Activation Key (red)", 1),
    52: ("Chicago Typewriter", 1), 53: ("Rocket Launcher", 1), 54: ("Mine Thrower", 1), 55: ("Handcannon", 1),
    56: ("Knife", 0), 57: ("Serpent Ornament", 1), 58: ("Moonstone (right half)", 1), 59: ("Insignia Key", 1),
    60: ("Round Insignia", 1), 61: ("False Eye", 1), 62: ("Custom TMP", 1), 63: ("Silencer (Handgun, unused)", 0),
    64: ("Punisher (2)", 0), 65: ("P.R.L. 412", 1), 66: ("Stock (Red9)", 1), 67: ("Stock (TMP)", 1),
    68: ("Scope (Rifle)", 1), 69: ("Scope (semi-auto Rifle)", 1), 70: ("Mine-Darts", 1), 71: ("Shotgun (Ada)", 1),
    72: ("Capture Luis Sera", 1), 73: ("Target Practice", 1), 74: ("Luis' Memo", 1), 75: ("Castellan Memo", 1),
    76: ("Female Intruder", 1), 77: ("Butler's Memo", 1), 78: ("Sample Retrieved", 1), 79: ("Ritual Preparation", 1),
    80: ("Luis' Memo 2", 1), 81: ("Rifle (semi-auto) w/ Infrared Scope", 0), 82: ("Krauser's Bow", 1),
    83: ("Chicago Typewriter (Ada)", 1), 84: ("Treasure Map (Castle)", 1), 85: ("Treasure Map (Island)", 1),
    86: ("Velvet Blue", 1), 87: ("Spinel", 1), 88: ("Pearl Pendant", 1), 89: ("Brass Pocket Watch", 1),
    90: ("Elegant Headdress", 1), 91: ("Antique Pipe", 1), 92: ("Gold Bangle w/ Pearls", 1), 93: ("Amber Ring", 1),
    94: ("Beerstein", 1), 95: ("Green Catseye", 1), 96: ("Red Catseye", 1), 97: ("Yellow Catseye", 1),
    98: ("Beerstein w/ (G)", 1), 99: ("Beerstein w/ (R)", 1), 100: ("Beerstein w/ (Y)", 1),
    101: ("Beerstein w/ (G,R)", 1), 102: ("Beerstein w/ (G,Y)", 1), 103: ("Beerstein w/ (R,Y)", 1),
    104: ("Beerstein w/ (G,R,Y)", 1), 105: ("Moonstone (left half)", 1), 106: ("Chicago Typewriter Ammo", 1),
    107: ("Rifle w/ Scope", 0), 108: ("Rifle (semi-auto) w/ Scope", 0), 109: ("Infinite Launcher", 1),
    110: ("King's Grail", 1), 111: ("Queen's Grail", 1), 112: ("Staff of Royalty", 1), 113: ("Gold Bars", 0),
    114: ("Arrows", 1), 115: ("Bonus Time", 0), 116: ("Emergency Lock Card Key", 1), 117: ("Bonus Points", 0),
    118: ("Green Catseye (key item)", 1), 119: ("Ruby", 1), 120: ("Treasure Box (S)", 0), 121: ("Treasure Box (L)", 0),
    122: ("Blue Moonstone", 1), 123: ("Key to the Mine", 1), 124: ("Attache Case S", 0), 125: ("Attache Case M", 0),
    126: ("Attache Case L", 0), 127: ("Attache Case XL", 0), 128: ("Golden Sword", 1), 129: ("Iron Key", 1),
    130: ("Stone of Sacrifice", 1), 131: ("Storage Room Card Key", 1), 132: ("Freezer Card Key", 1),
    133: ("Piece of the Holy Beast, Panther", 1), 134: ("Piece of the Holy Beast, Serpent", 1),
    135: ("Piece of the Holy Beast, Eagle", 1), 136: ("Jet-ski Key", 1), 137: ("Dirty Pearl Pendant", 1),
    138: ("Dirty Brass Pocket Watch", 1), 139: ("Old Key", 1), 140: ("Camp Key", 1), 141: ("Dynamite", 1),
    142: ("Lift Activation Key", 1), 143: ("Gold Bangle", 1), 144: ("Elegant Perfume Bottle", 1),
    145: ("Mirror w/ Pearls & Rubies", 1), 146: ("Waste Disposal Card Key", 1), 147: ("Elegant Chessboard", 1),
    148: ("Riot Gun", 1), 149: ("Black Bass", 1), 150: ("Hourglass w/ Gold Decor", 1), 151: ("Black Bass (L)", 1),
    152: ("Illuminados Pendant", 1), 153: ("Rifle w/ Infrared Scope", 0), 154: ("Crown", 1), 155: ("Crown Jewel", 1),
    156: ("Royal Insignia", 1), 157: ("Crown with Jewels", 1), 158: ("Crown with an Insignia", 1),
    159: ("Salazar Family Crown", 1), 160: ("(unused 160)", 0), 161: ("Emerald", 1),
    162: ("Bottle Caps (collection)", 1), 163: ("Gallery Key", 1), 164: ("Emblem (right half)", 1),
    165: ("Emblem (left half)", 1), 166: ("Hexagonal Emblem", 1), 167: ("Castle Gate Key", 1),
    168: ("Mixed Herbs (R+Y)", 1), 169: ("Treasure Map (Village)", 1), 170: ("Scope (Mine Thrower)", 1),
    171: ("Mine Thrower w/ Scope", 0), 172: ("Playing Manual 1", 1), 173: ("Info on Ashley", 1),
    174: ("Playing Manual 2", 1), 175: ("Alert Order", 1), 176: ("About the Blue Medallions", 1),
    177: ("Chief's Note", 1), 178: ("Closure of the Church", 1), 179: ("Anonymous Letter", 1),
    180: ("Playing Manual 3", 1), 181: ("Sera and the 3rd Party", 1), 182: ("Two Routes", 1),
    183: ("Village's Last Defense", 1), 184: ("Butterfly Lamp", 1), 185: ("Green Eye", 1), 186: ("Red Eye", 1),
    187: ("Blue Eye", 1), 188: ("Butterfly Lamp w/ (G)", 1), 189: ("Butterfly Lamp w/ (R)", 1),
    190: ("Butterfly Lamp w/ (B)", 1), 191: ("Butterfly Lamp w/ (G,R)", 1), 192: ("Butterfly Lamp w/ (G,B)", 1),
    193: ("Butterfly Lamp w/ (R,B)", 1), 194: ("Butterfly Lamp w/ (R,G,B)", 1), 195: ("Prison Key", 1),
    196: ("Platinum Sword", 1), 197: ("Infrared Scope", 1), 198: ("Elegant Mask", 1), 199: ("Green Gem", 1),
    200: ("Red Gem", 1), 201: ("Purple Gem", 1), 202: ("Elegant Mask w/ (G)", 1), 203: ("Elegant Mask w/ (R)", 1),
    204: ("Elegant Mask w/ (P)", 1), 205: ("Elegant Mask w/ (G,R)", 1), 206: ("Elegant Mask w/ (G,P)", 1),
    207: ("Elegant Mask w/ (R,P)", 1), 208: ("Elegant Mask w/ (R,G,P)", 1), 209: ("Golden Lynx", 1),
    210: ("Green Stone of Judgement", 1), 211: ("Red Stone of Faith", 1), 212: ("Blue Stone of Treason", 1),
    213: ("Golden Lynx w/ (G)", 1), 214: ("Golden Lynx w/ (R)", 1), 215: ("Golden Lynx w/ (B)", 1),
    216: ("Golden Lynx w/ (G,R)", 1), 217: ("Golden Lynx w/ (G,B)", 1), 218: ("Golden Lynx w/ (R,B)", 1),
    219: ("Golden Lynx w/ (R,G,B)", 1), 220: ("Bottle Cap (Leon w/ Rocket Launcher)", 1),
    221: ("Bottle Cap (Leon w/ Shotgun)", 1), 222: ("Bottle Cap (Leon w/ Handgun)", 1),
    223: ("Bottle Cap (Ashley Graham)", 1), 224: ("Bottle Cap (Luis Sera)", 1), 225: ("Bottle Cap (Don Jose)", 1),
    226: ("Bottle Cap (Don Diego)", 1), 227: ("Bottle Cap (Don Esteban)", 1), 228: ("Bottle Cap (Don Manuel)", 1),
    229: ("Bottle Cap (Dr. Salvador)", 1), 230: ("Bottle Cap (Merchant)", 1), 231: ("Bottle Cap (Zealot w/ Scythe)", 1),
    232: ("Bottle Cap (Zealot w/ Shield)", 1), 233: ("Bottle Cap (Zealot w/ Bowgun)", 1),
    234: ("Bottle Cap (Leader Zealot)", 1), 235: ("Bottle Cap (Soldier w/ Dynamite)", 1),
    236: ("Bottle Cap (Soldier w/ Stun-rod)", 1), 237: ("Bottle Cap (Soldier w/ Hammer)", 1),
    238: ("Bottle Cap (Isabel)", 1), 239: ("Bottle Cap (Maria)", 1), 240: ("Bottle Cap (Ada Wong)", 1),
    241: ("Bottle Cap (Bella Sisters)", 1), 242: ("Bottle Cap (Don Pedro)", 1), 243: ("Bottle Cap (J.J)", 1),
    244: ("Letter from Ada", 1), 245: ("Luis' Memo 3", 1), 246: ("Paper Airplane", 1), 247: ("Our Plan", 1),
    248: ("Luis' Memo 4", 1), 249: ("Krauser's Note", 1), 250: ("Luis' Memo 5", 1), 251: ("Our Mission", 1),
    252: ("(unused file 30)", 0), 253: ("(unused file 31)", 0), 254: ("Tactical Vest (unused)", 0),
    255: ("(unused 255)", 0), 256: ("(unused 256)", 0), 257: ("(unused 257)", 0), 258: ("(unused 258)", 0),
    259: ("(unused 259)", 0), 260: ("(unused 260)", 0), 261: ("(unused 261)", 0), 262: ("Mission Directives 1", 1),
    263: ("Mission Directives 2", 1), 264: ("Mission Directives 3", 1), 265: ("Mission Directives 4", 1),
    266: ("Mission Directives 5", 1), 267: ("Mission 1 Treasure Map", 1), 268: ("Mission 2 Treasure Map", 1),
    269: ("Mission 3 Treasure Map", 1), 270: ("Mission 4 Treasure Map", 1), 271: ("Mission 5 Treasure Map", 1),
}


def place(typ):
    """Each item's group, and every item in rank order: group by group, each
    group's own items in its order, then the rest of its types and its ALSO
    items by name (the offered ones first)."""
    names = [g[0] for g in GROUPS]
    banks = [g[1] for g in GROUPS]
    if banks != sorted(banks):
        sys.exit("GROUPS must go bank by bank")
    group = {}
    for g, (_, _, ids, _) in enumerate(GROUPS):
        for i in ids:
            if i in group:
                sys.exit("item %d is named by two groups" % i)
            group[i] = g
    for i, gname in ALSO.items():
        if i in group:
            sys.exit("item %d is named by a group and ALSO" % i)
        group[i] = names.index(gname)
    taker = {}
    for g, (_, _, _, types) in enumerate(GROUPS):
        for t in types:
            if t in taker:
                sys.exit("type %d is taken by two groups" % t)
            taker[t] = g
    for i in range(0x110):
        if i not in group:
            if typ[i] not in taker:
                sys.exit("item %d (%s, a %s) has no group" % (i, NAMES[i][0], TYPE_NAME.get(typ[i], "?")))
            group[i] = taker[typ[i]]
    by_rank = []
    for g, (_, _, ids, _) in enumerate(GROUPS):
        rest = [i for i in range(0x110) if group[i] == g and i not in ids]
        by_rank += list(ids) + sorted(rest, key=lambda i: (not NAMES[i][1], NAMES[i][0].lower(), i))
    return group, by_rank


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--exe", default=DEFAULT_EXE)
    ap.add_argument("--out", default=os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "src", "items.h"))
    args = ap.parse_args()
    img = Image(args.exe)
    sigs = {name: pattern for name, pattern, _ in parse_sigs()}
    info = img.find(sigs["kSigItemInfo"])
    pieces_at = img.find(sigs["kSigPieces"])
    charge_at = img.find(sigs["kSigCharge"])
    levels_at = img.find(sigs["kSigMaxLevel"])
    if len(info) != 1 or len(pieces_at) != 1 or len(charge_at) != 1 or len(levels_at) != 1:
        sys.exit("the item tables' signatures do not match this exe (run tools/check_sigs.py)")
    kinds = img.u32(info[0] + 18)
    pieces = img.u32(pieces_at[0] + 3)
    piece = {}
    n = 0
    while img.i16(pieces + n * 0x58) != -1:
        a = pieces + n * 0x58
        piece[img.i16(a)] = (img.u8(a + 4), img.u8(a + 5))
        n += 1
    kind = {i: img.u8(kinds + i) for i in range(0x110)}
    typ = {i: KIND_TYPE[kind[i]] for i in range(0x110)}
    # The items whose cItem keeps upgrade levels (+6) and loaded rounds (+8): a
    # new entry's set-up (FUN_00707780) gives every weapon (type 1) a levels word
    # and a full magazine, and the weapons are the ones with a row in
    # WeaponId2ChargeNum's or WeaponId2MaxLevel's table - all but the Plaga
    # Sample. Every other item's +6 and +8 hold what the entry's last occupant
    # left there (the set-up gives a part its "attached to" link there, and a
    # file its place in the list, but neither is a state the item box keeps).
    rows = img.u32(img.u32(charge_at[0] + 2))
    charge = img.u32(charge_at[0] + 18)
    charged = {img.i16(charge + r * 22) for r in range(rows)}
    levelled = {img.i16(a) for a in range(img.u32(levels_at[0] + 7), img.u32(levels_at[0] + 23), 6)}
    state = {i: typ[i] == 1 and (i in charged or i in levelled) for i in range(0x110)}
    group, by_rank = place(typ)
    for i in range(0x110):
        if NAMES[i][1] and typ[i] not in OFF_GRID and i not in piece:
            sys.exit("item %d (%s) is offered, but the case has no piece for it" % (i, NAMES[i][0]))
    rank = {i: r for r, i in enumerate(by_rank)}
    out = []
    w = out.append
    w("#pragma once")
    w("")
    w("// Generated by tools/gen_items.py from bio4.exe (build 10112090) - do not edit by hand.")
    w("// Each item's kind (itemInfo's switch case) and the size of its piece in the")
    w("// attache case come from the exe's own tables; the names are the English")
    w("// game's. The game reads the same tables at run time (game.cpp), and the")
    w("// panel prefers those: this table is the names, the panel's filing and a")
    w("// fallback.")
    w("namespace re4cc::items {")
    w("")
    w("enum Type : unsigned char {")
    w("  kWeapon = 1, kAmmo = 2, kGrenade = 3, kTreasure = 5, kRecovery = 6, kKeyItem = 7, kBonus = 8, kPart = 9,")
    w("  kFile = 10, kMap = 11, kGem = 12, kBottleCap = 13,")
    w("};")
    w("")
    w("// The panel's banks (the tabs of the item box and of the Add list) and the")
    w("// groups the Add list heads its items with, in the order they are listed.")
    w("enum Bank : unsigned char { %s, kBanks };" % ", ".join(e for e, _ in BANKS))
    w("inline const char* bank_name(int b) {")
    w("  static const char* const kNames[kBanks] = {%s};" % ", ".join('"%s"' % n for _, n in BANKS))
    w("  return b >= 0 && b < kBanks ? kNames[b] : \"?\";")
    w("}")
    w("struct Group {")
    w("  const char* name;")
    w("  unsigned char bank;")
    w("};")
    w("constexpr int kGroupCount = %d;" % len(GROUPS))
    w("inline constexpr Group kGroups[kGroupCount] = {")
    for (gname, bank, _, _) in GROUPS:
        w('    {"%s", %s},' % (gname, BANKS[bank][0]))
    w("};")
    w("")
    w("struct Def {")
    w("  short id;")
    w("  const char* name;")
    w("  unsigned char kind;     // itemInfo's switch case")
    w("  unsigned char type;     // what that case says the item is")
    w("  unsigned char bank;     // the panel's tab (its group's)")
    w("  unsigned char group;    // the Add list's heading over it")
    w("  unsigned char offered;  // the panel's add lists offer it (the case's, if it has a piece)")
    w("  unsigned char state;    // a weapon: its cItem keeps upgrade levels and loaded rounds")
    w("  unsigned char w, h;     // its piece in the attache case (0x0: none - it lives off the grid)")
    w("  short rank;             // its place in the panel's lists: bank, group, then the group's own order")
    w("};")
    w("")
    w("constexpr int kCount = 0x110;")
    w("inline constexpr Def kItems[kCount] = {")
    for i in range(0x110):
        name, offered = NAMES[i]
        g = group[i]
        pw, ph = piece.get(i, (0, 0))
        w('    {%d, "%s", %d, %d, %d, %d, %d, %d, %d, %d, %d},  // %s%s; %s' % (
            i, name.replace('"', '\\"'), kind[i], typ[i], GROUPS[g][1], g, offered, state[i], pw, ph, rank[i],
            TYPE_NAME.get(typ[i], "?"), " (levels, rounds)" if state[i] else "", GROUPS[g][0]))
    w("};")
    w("")
    w("// Every item in rank order: the Add list's order (the offered ones).")
    w("inline constexpr short kByRank[kCount] = {")
    for k in range(0, len(by_rank), 16):
        w("    " + " ".join("%d," % i for i in by_rank[k:k + 16]))
    w("};")
    w("")
    w("inline const Def* find(int id) { return id >= 0 && id < kCount ? &kItems[id] : nullptr; }")
    w("inline const char* name(int id) { const Def* d = find(id); return d ? d->name : \"?\"; }")
    w("inline bool in_case(int id) { const Def* d = find(id); return d && d->w; }")
    w("")
    w("// Whether an item's cItem keeps a state of its own - upgrade levels (+6) and")
    w("// loaded rounds (+8): the weapons, which a new entry's set-up (FUN_00707780)")
    w("// gives a levels word and a full magazine. Any other item's +6 and +8 hold")
    w("// what the entry's last occupant left there (a part's are the weapon it is")
    w("// attached to, a file's its place in the list): no state of the item's own.")
    w("inline bool keeps_state(int id) { const Def* d = find(id); return d && d->state; }")
    w("")
    w("// The attache case's sizes S, M, L, XL: items to the game, but the size of the")
    w("// case is which of them is held (the sub-screen's set-up, FUN_007690C0, sets")
    w("// SubScreenWk+2AA by it) - the case's own state, not an item to add or remove.")
    w("inline constexpr short kCaseIds[4] = {%s};" % ", ".join(str(i) for i in CASES))
    w("inline bool is_case(int id) { return id >= kCaseIds[0] && id <= kCaseIds[3]; }")
    w("")
    w("// itemInfo's counts for a kind: how many one pickup gives (def) and how many")
    w("// one slot or entry holds (max), by difficulty (+847C: Amateur below 2, Easy")
    w("// below 4). Weapons and parts are one each; treasures and gems stack to 999.")
    w("struct Counts {")
    w("  int type, def, max;")
    w("};")
    w("inline Counts counts(int kind, int difficulty) {")
    w("  const bool amateur = difficulty < 2, easy = difficulty < 4;")
    w("  switch (kind) {")
    w("    case 0: return amateur ? Counts{2, 10, 20} : Counts{2, 5, 10};")
    w("    case 1: return {3, 1, 1};")
    w("    case 2: return {1, 0, 0};")
    w("    case 3: return amateur ? Counts{2, 20, 150} : Counts{2, 10, 50};")
    w("    case 4: return {6, 1, 1};")
    w("    case 5: return {2, amateur ? 10 : 5, easy ? 20 : 10};")
    w("    case 6: return {1, 0, 0};")
    w("    case 7: return amateur ? Counts{2, 10, 40} : Counts{2, 5, 20};")
    w("    case 8: return amateur ? Counts{2, 20, 50} : Counts{2, 10, easy ? 25 : 15};")
    w("    case 9: return amateur ? Counts{2, 20, 20} : Counts{2, 10, 10};")
    w("    case 10: return amateur ? Counts{2, 100, 200} : Counts{2, 50, 100};")
    w("    case 11: return {9, 0, 0};")
    w("    case 12: return {2, amateur ? 2 : 1, easy ? 10 : 5};")
    w("    case 13: return {10, 1, 1};")
    w("    case 14: return {11, 1, 1};")
    w("    case 15: return {12, 1, 999};")
    w("    case 16: return {5, 1, 999};")
    w("    case 17: return amateur ? Counts{2, 100, 200} : Counts{2, 50, 100};")
    w("    case 18: return {8, 5, 999};")
    w("    case 19: return amateur ? Counts{2, 10, 40} : Counts{2, 5, 20};")
    w("    case 20: return {8, 30, 999};")
    w("    case 21: return {5, 1, 1};")
    w("    case 22: return {13, 1, 1};")
    w("    default: return {7, 1, 1};")
    w("  }")
    w("}")
    w("")
    w("// The upgrade a weapon's exclusive tune-up adds beyond its normal maximum, per")
    w("// stat (firepower, firing speed, reload speed, capacity): the merchant's")
    w("// Exclusive upgrade is one more level of these. The weapon's levels word holds")
    w("// level - 1 in four nibbles.")
    w("struct Exclusive {")
    w("  short id;")
    w("  unsigned char extra[4];")
    w("};")
    w("inline constexpr Exclusive kExclusive[] = {")
    for (iid, ex) in ((47, (0, 1, 0, 0)), (48, (1, 0, 0, 0)), (148, (1, 0, 0, 0)), (3, (0, 0, 0, 1)), (45, (0, 0, 0, 1)),
                      (46, (1, 0, 0, 0)), (39, (1, 0, 0, 0)), (37, (1, 0, 0, 0)), (41, (1, 0, 0, 0)),
                      (54, (1, 0, 0, 0)), (35, (1, 0, 0, 0)), (44, (1, 0, 0, 0)), (33, (1, 0, 0, 0)),
                      (55, (1, 0, 0, 1)), (52, (1, 0, 0, 1)), (83, (1, 0, 0, 1))):
        w("    {%d, {%d, %d, %d, %d}},  // %s" % (iid, ex[0], ex[1], ex[2], ex[3], NAMES[iid][0]))
    w("};")
    w("inline int exclusive(int id, int stat) {")
    w("  for (const Exclusive& e : kExclusive)")
    w("    if (e.id == id) return e.extra[stat];")
    w("  return 0;")
    w("}")
    w("")
    w("}  // namespace re4cc::items")
    open(args.out, "w").write("\n".join(out) + "\n")
    print("%s: %d items (%d offered), %d with a piece in the case, %d keeping levels and rounds" % (
        args.out, len(NAMES), sum(o for _, o in NAMES.values()), len(piece), sum(state.values())))


if __name__ == "__main__":
    main()
