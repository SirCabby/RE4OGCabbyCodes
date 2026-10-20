#!/usr/bin/env python3
"""Check every signature src/sigs.h lists against bio4.exe on disk.

    python3 tools/check_sigs.py --exe "$GAME_DIR/bio4.exe"      (or: make sigs)

bio4.exe is not packed, so the file is what runs. For each X(id, pattern,
matches, what) entry this finds the pattern in .text, checks the match count,
and then does what src/game.cpp does with it: reads the operands out of the
match, follows calls through the incremental-link thunks, and runs the same
cross-checks (two sites naming the same global, a patch site holding the bytes
the cheat expects, a table looking like the table it should be). It prints
what each site resolved to in build 10112090's addresses - the ones CLAUDE.md
records - and exits non-zero when anything is off. Pure stdlib.
"""
import argparse
import os
import re
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
SIGS_H = os.path.join(HERE, "..", "src", "sigs.h")
DEFAULT_EXE = os.path.expanduser("~/.local/share/Steam/steamapps/common/Resident Evil 4/Bin32/bio4.exe")


class Image:
    """The exe laid out as the loader maps it (at its preferred base)."""

    def __init__(self, path):
        d = open(path, "rb").read()
        pe = struct.unpack_from("<I", d, 0x3C)[0]
        nsec = struct.unpack_from("<H", d, pe + 6)[0]
        opt = pe + 24
        opt_sz = struct.unpack_from("<H", d, pe + 20)[0]
        self.base = struct.unpack_from("<I", d, opt + 28)[0]
        size = struct.unpack_from("<I", d, opt + 56)[0]
        self.mem = bytearray(size)
        hdr = struct.unpack_from("<I", d, opt + 60)[0]
        self.mem[:hdr] = d[:hdr]
        self.sections = {}
        for i in range(nsec):
            o = opt + opt_sz + i * 40
            name = d[o:o + 8].rstrip(b"\0").decode("latin-1")
            vsize, va, rsize, rptr = struct.unpack_from("<IIII", d, o + 8)
            self.mem[va:va + min(rsize, vsize or rsize)] = d[rptr:rptr + min(rsize, vsize or rsize)]
            self.sections[name] = (self.base + va, self.base + va + max(vsize, rsize))
        # import slots by name (the IAT is bound at load; here we only want where they are)
        self.imports = {}
        imp_rva = struct.unpack_from("<I", d, opt + 96 + 8)[0]
        p = imp_rva
        while True:
            oft, _, _, name_rva, ft = struct.unpack_from("<IIIII", self.mem, p)
            if not name_rva:
                break
            dll = self.cstr(self.base + name_rva).lower()
            t, slot = oft or ft, ft
            while True:
                v = struct.unpack_from("<I", self.mem, t)[0]
                if not v:
                    break
                if not v & 0x80000000:
                    self.imports[(dll, self.cstr(self.base + v + 2))] = self.base + slot
                t += 4
                slot += 4
            p += 20

    def u8(self, va): return self.mem[va - self.base]
    def u16(self, va): return struct.unpack_from("<H", self.mem, va - self.base)[0]
    def i16(self, va): return struct.unpack_from("<h", self.mem, va - self.base)[0]
    def u32(self, va): return struct.unpack_from("<I", self.mem, va - self.base)[0]
    def i32(self, va): return struct.unpack_from("<i", self.mem, va - self.base)[0]
    def b(self, va, n): return bytes(self.mem[va - self.base:va - self.base + n])

    def cstr(self, va):
        o = va - self.base
        return self.mem[o:self.mem.index(b"\0", o)].decode("latin-1")

    def find(self, pattern):
        rx = b""
        for t in pattern.split():
            rx += b"." if t.startswith("?") else re.escape(bytes([int(t, 16)]))
        lo, hi = self.sections[".text"]
        return [self.base + m.start() for m in re.compile(rx, re.S).finditer(self.mem, lo - self.base, hi - self.base)]

    def call_target(self, va):
        """E8 rel32 at va, followed through an incremental-link `jmp rel32` thunk."""
        if self.u8(va) != 0xE8:
            return 0
        t = (va + 5 + self.i32(va + 1)) & 0xFFFFFFFF
        if self.u8(t) == 0xE9:
            t = (t + 5 + self.i32(t + 1)) & 0xFFFFFFFF
        return t

    def in_section(self, va, name):
        lo, hi = self.sections[name]
        return lo <= va < hi


def parse_sigs():
    text = open(SIGS_H).read()
    out = []
    for m in re.finditer(r'X\((kSig\w+),(?:\s|\\)*((?:"[^"]*"(?:\s|\\)*)+),(?:\s|\\)*(\d+),', text):
        pattern = "".join(re.findall(r'"([^"]*)"', m.group(2)))
        out.append((m.group(1), pattern, int(m.group(3))))
    return out


class Checker:
    def __init__(self, img):
        self.img = img
        self.errors = 0
        self.found = {}

    def fail(self, msg):
        print("  FAIL: " + msg)
        self.errors += 1

    def ok(self, msg):
        print("  " + msg)

    def expect(self, cond, msg):
        if cond:
            self.ok(msg)
        else:
            self.fail(msg)

    def operand(self, sig, off):
        """The dword at +off of every match; all must agree."""
        vals = {self.img.u32(a + off) for a in self.found[sig]}
        if len(vals) != 1:
            self.fail("%s: the %d matches disagree at +%d (%s)" % (sig, len(self.found[sig]), off,
                                                                  ", ".join("%X" % v for v in sorted(vals))))
            return 0
        return vals.pop()


def castle_groups(img, f):
    """The castle's spawner's counted sends, as game.cpp reads them: {counter: (cap, [wrap, ...])}, the wraps'
    in-use test, FUN_007d4ff0 (the tail's sends) and how many places send. A send: cmp dword [reg+counter],cap
    (83 B8+r), then the wrap into ecx right before a call of the in-use test - mov ecx,reg or mov ecx,[the work's
    slot] [add ecx,wrap x 0C], or lea ecx,[reg+wrap x 0C] - or push cap; add esi,counter; push entry; mov
    edi,wrap; call FUN_007d4ff0."""
    body = img.b(f, 0x2000)
    body = body[:body.find(b"\xCC\xCC\xCC")]
    found = []  # (counter, cap, wrap, the call's target, tail)
    for p in range(len(body) - 16):
        if body[p] == 0x83 and (body[p + 1] & 0xF8) == 0xB8 and (body[p + 1] & 7) != 4:
            off, cap = struct.unpack_from("<I", body, p + 2)[0], body[p + 6]
            if not 0x500 <= off < 0x600 or off & 3:
                continue
            for q in range(p + 7, min(p + 0x48, len(body) - 5)):
                if body[q] != 0xE8:
                    continue
                disp = None
                def mov_ecx(e):  # mov ecx,reg or mov ecx,[the work's slot] ending at e
                    return (body[e - 2] == 0x8B and (body[e - 1] & 0xF8) == 0xC8) or body[e - 6:e - 4] == b"\x8B\x0D"
                if body[q - 6:q - 4] == b"\x81\xC1" and mov_ecx(q - 6):
                    disp = struct.unpack_from("<I", body, q - 4)[0]
                elif body[q - 3:q - 1] == b"\x83\xC1" and mov_ecx(q - 3):
                    disp = body[q - 1]
                elif body[q - 6] == 0x8D and (body[q - 5] & 0xF8) == 0x88:
                    disp = struct.unpack_from("<I", body, q - 4)[0]
                elif body[q - 3] == 0x8D and (body[q - 2] & 0xF8) == 0x48:
                    disp = body[q - 1]
                elif body[q - 2] == 0x8B and (body[q - 1] & 0xF8) == 0xC8:
                    disp = 0
                if disp is None:
                    continue
                if disp % 0xC == 0:
                    found.append((off, cap, disp // 0xC, img.call_target(f + q), False))
                break
        elif (body[p] == 0x6A and body[p + 2:p + 4] == b"\x81\xC6"):
            off, cap = struct.unpack_from("<I", body, p + 4)[0], body[p + 1]
            q = p + 8
            q += 2 if body[q] == 0x6A else 5 if body[q] == 0x68 else 0
            if not 0x500 <= off < 0x600 or body[q] != 0xBF or body[q + 5] != 0xE8:
                continue
            found.append((off, cap, struct.unpack_from("<I", body, q + 1)[0], img.call_target(f + q + 5), True))
    inuse = {t for _, _, _, t, tail in found if not tail}
    tails = {t for _, _, _, t, tail in found if tail}
    groups = {}
    for off, cap, wrap, _, _ in found:
        g = groups.setdefault(off, (cap, []))
        if g[0] != cap or wrap in g[1] or wrap >= 0x558 // 0xC:
            return {}, None, None, 0
        g[1].append(wrap)
    if len(inuse) != 1 or len(tails) != 1:
        return {}, None, None, 0
    return groups, inuse.pop(), tails.pop(), len(found)


def padded_length(img, f, limit):
    """A function's length up to its padding (three int3), 0 past `limit`."""
    n = 0
    while n < limit and img.b(f + n, 3) != b"\xCC\xCC\xCC":
        n += 1
    return n if n < limit else 0


def water_stage(img, task, at_slot, player):
    """The Mercenaries' water stage, as game.cpp reads it (prepare_water): its tasks (push task; push 14; call
    FUN_006c4d10), seven zones - each: push area; call the inside test (FUN_006b4da0: [the triggers' work]+10), then
    five sends (cmp dword [eax],0A; jae - call FUN_00682420; test eax,40000 - test byte [the entry's flags],2 - push
    entry twice - add dword [eax+counter],ebx), then cmp dword [eax+counter],ends; jae (it returns) - the eighth (mov
    eax,[work]; mov ecx,[eax+8]; sub ecx,[eax]; cmp ecx,N; jae; its sends a jmp away), and the loop's one-shot areas
    (push area; call the inside test) with their sends (mov eax,entry; call the stage's send). Returns a dict, or a
    string saying what does not fit."""
    b = img.u8
    d = img.u32
    slot = d(task + 24)
    create = img.call_target(task + 85)
    n = padded_length(img, task, 0x400)
    tasks, loop = [d(task + 53)], 0
    for p in range(task + 90, task + n - 12):
        if b(p) == 0x68 and b(p + 5) == 0x6A and b(p + 6) == 0x14 and b(p + 7) == 0xE8 and \
                img.call_target(p + 7) == create:
            tasks.append(d(p + 1))
            loop = p + 12
    inside_pat = bytes.fromhex("558BEC8B45088BC883E11FBA00000080D3EA8B0D") + struct.pack("<I", at_slot) + \
        bytes.fromhex("C1E805855481100F95C05DC3")
    is_inside = lambda t: t and img.b(t, len(inside_pat)) == inside_pat
    inside, send, ats, oneshots, direct, inline = 0, 0, [], set(), [], 0
    for p in range(loop, task + n - 10):
        if b(p) == 0x6A and b(p + 2) == 0xE8 and is_inside(img.call_target(p + 2)):
            inside = inside or img.call_target(p + 2)
            ats.append(b(p + 1))
        if b(p) == 0xB8 and b(p + 5) == 0xE8 and d(p + 1) < 256:
            send = send or img.call_target(p + 5)
            if img.call_target(p + 5) == send:
                oneshots.add(d(p + 1))
                inline += 1
        if b(p) == 0xE8:
            raw = (p + 5 + img.i32(p + 1)) & 0xFFFFFFFF
            if img.in_section(raw, ".text") and b(raw) != 0xE9 and raw not in direct:
                direct.append(raw)
    sends = inline

    def sends_in(f, limit, into):
        k = 0
        for p in range(f, f + padded_length(img, f, limit) - 10):
            if b(p) == 0xB8 and b(p + 5) == 0xE8 and img.call_target(p + 5) == send and d(p + 1) < 256:
                into.add(d(p + 1))
                k += 1
        return k

    for f in direct:
        if f != send:
            sends += sends_in(f, 0x400, oneshots)
    if len(tasks) != 8 or not inside or not send or len(oneshots) != sends:
        return "its tasks (%d), inside test %X, send %X, one-shot sends %d of %d" % (len(tasks), inside, send,
                                                                                      len(oneshots), sends)
    zones, lab, lab_at, state = [], set(), 0, 0
    for f in tasks:
        fl = padded_length(img, f, 0x400)
        if b(f) == 0xA1 and d(f + 1) == slot and img.b(f + 5, 7) == bytes.fromhex("8B48082B0883F9") and \
                b(f + 13) == 0x73:
            lab_at = b(f + 12)
            for p in range(f, f + fl):
                if b(p) == 0xE9:
                    sends_in((p + 5 + img.i32(p + 1)) & 0xFFFFFFFF, 0x200, lab)
                    break
            continue
        z = {"task": f, "at": None, "entries": [], "counter": 0, "ends": None}
        gates = skips = slots = 0
        for p in range(f, f + fl - 10):
            if b(p) == 0x6A and b(p + 2) == 0xE8 and img.call_target(p + 2) == inside:
                if z["at"] is not None:
                    return "zone %X: two areas" % f
                z["at"] = b(p + 1)
            if b(p) == 0xF6 and b(p + 1) & 0xF8 == 0x80 and b(p + 1) & 7 != 4 and b(p + 6) == 2:
                off = d(p + 2)
                if 0x5410 <= off < 0x5410 + 256 * 0x20 and (off - 0x5410) % 0x20 == 0:
                    z["entries"].append((off - 0x5410) // 0x20)
            if b(p) == 0x83 and b(p + 1) == 0x38 and b(p + 2) == 0x0A and \
                    (b(p + 3) == 0x73 or (b(p + 3) == 0x0F and b(p + 4) == 0x83)):
                gates += 1
            if b(p) == 0xE8 and b(p + 5) == 0xA9 and d(p + 6) == 0x40000:
                state = state or img.call_target(p)
                skips += img.call_target(p) == state
            if b(p) == 0x01 and b(p + 1) == 0x58 and b(p + 2) != 8:
                if z["counter"] and b(p + 2) != z["counter"]:
                    return "zone %X: two counters" % f
                z["counter"] = b(p + 2)
            if b(p) == 0x83 and b(p + 1) == 0x78 and b(p + 4) == 0x73 and z["counter"] and b(p + 2) == z["counter"]:
                if z["ends"] is not None:
                    return "zone %X: two ends" % f
                z["ends"] = b(p + 3)
            if b(p) == 0xA1 and d(p + 1) == slot:
                slots += 1
        pushed = [sum(1 for p in range(f, f + fl - 6) if b(p) == 0x68 and d(p + 1) == e and b(p + 5) == 0xE8)
                  for e in z["entries"]]
        if z["at"] is None or len(z["entries"]) != 5 or gates != 5 or skips != 5 or not slots or \
                z["ends"] is None or not 0x14 <= z["counter"] < 0x30 or z["counter"] & 3 or pushed != [2] * 5:
            return "zone %X: area %s, entries %s, gates %d, ladder tests %d, counter %X, ends %s, pushes %s" % (
                f, z["at"], z["entries"], gates, skips, z["counter"], z["ends"], pushed)
        zones.append(z)
    # FUN_00682420: r_no_0 through a table, r_no_0 0's r_no_1 through a second; the r_no_1 whose case is 0x40000.
    ladder = []
    head = img.b(state, 50)
    if head[:2] != b"\x8B\x15" or d(state + 2) != player or head[6:13] != bytes.fromhex("0FB68AFC000000") or \
            head[24:27] != b"\xFF\x24\x8D" or head[31:38] != bytes.fromhex("0FB68AFD000000") or \
            head[38:41] != bytes.fromhex("83F912") or img.b(state + 47, 3) != b"\xFF\x24\x8D":
        return "the player's routine bits (%X) not the code expected" % state
    t0, t1 = d(state + 27), d(state + 50)
    if d(t0) != state + 31 or any(img.b(d(t0 + 4 * r), 5) == bytes.fromhex("B800000400") for r in range(1, 5)):
        return "the player's routine bits' first table (%X)" % t0
    ladder = [r for r in range(0x13) if img.b(d(t1 + 4 * r), 5) == bytes.fromhex("B800000400")]
    return {"slot": slot, "zones": zones[::-1], "oneshot_ats": ats, "oneshots": oneshots, "lab": lab, "lab_at": lab_at,
            "ladder": ladder, "inside": inside, "send": send, "state": state}


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--exe", default=DEFAULT_EXE)
    args = ap.parse_args()
    img = Image(args.exe)
    c = Checker(img)
    sigs = parse_sigs()
    print("%s: base %08X, %d signatures in src/sigs.h" % (args.exe, img.base, len(sigs)))
    for sig, pattern, want in sigs:
        hits = img.find(pattern)
        c.found[sig] = hits
        tag = "ok" if len(hits) == want else "FAIL"
        print("%-16s %d match(es) (want %d) %s  %s" % (sig, len(hits), want, " ".join("%X" % h for h in hits[:4]), tag))
        if len(hits) != want:
            c.errors += 1
    if c.errors:
        print("\n%d signature(s) did not match as they should - not checking further" % c.errors)
        return 1
    f = c.found
    one = lambda s: f[s][0]

    print("\n-- globals")
    pg = c.operand("kSigGlobal", 1)
    c.expect(pg == c.operand("kSigGlobal", 14), "pG slot %X (named twice by the same site)" % pg)
    c.expect(img.in_section(pg, ".data"), "pG's slot is in .data")
    item_mgr = c.operand("kSigItemMgr", 14)
    c.expect(img.in_section(item_mgr, ".data"), "ItemMgr %X (both sites agree, in .data)" % item_mgr)
    sub = c.operand("kSigSubScreen", 1)
    c.expect(img.in_section(sub, ".data"), "SubScreenWk %X" % sub)
    cockpit = c.operand("kSigCockpit", 5)
    c.expect(img.in_section(cockpit, ".data"), "Cockpit %X (CountDown at %X)" % (cockpit, cockpit + 0x98))
    option = c.operand("kSigOption", 1)
    c.expect(option == c.operand("kSigOption", 6) - 1 or img.in_section(option, ".data"),
             "OptionOpenFlag %X" % option)

    print("\n-- the save card")
    storage = c.operand("kSigCard", 2)
    c.expect(storage == c.operand("kSigCard", 15), "remote storage slot %X (read and written by the same site)" % storage)
    imp = c.operand("kSigCard", 10)
    c.expect(imp == img.imports.get(("steam_api.dll", "SteamRemoteStorage")),
             "the call goes through bio4.exe's steam_api.dll!SteamRemoteStorage import (%X)" % imp)
    card = c.operand("kSigCard", 21)
    c.expect(card == c.operand("kSigCard", 29), "cCard slot %X" % card)
    c.expect(card == c.operand("kSigCardFiles", 2), "the file header scan reads the same cCard slot")
    exists = c.operand("kSigCardFiles", 52)
    c.expect(img.in_section(exists, ".data"), "per-file 'exists' bytes %X (20 of them)" % exists)

    c.expect(c.operand("kSigCheckpoint", 1) == pg, "the continue reads pG's slot")
    cp = c.operand("kSigCheckpoint", 7)
    c.expect(img.in_section(cp, ".data"), "checkpoint work slot %X" % cp)

    # A save asked for: a save to a file and one of the system data alone each put their flags into pG+4 and hand
    # the card's task to the scheduler's level 1, which turns it away while the level holds a task - and the flags
    # stay for the next save (the mod clears them with no card up or about to start).
    sysreq, filereq, start = one("kSigSaveSystem"), one("kSigSaveFile"), one("kSigTaskStart")
    task = c.operand("kSigSaveSystem", 24)
    table = c.operand("kSigTaskStart", 69)
    c.expect(c.operand("kSigSaveSystem", 10) == pg and c.operand("kSigSaveFile", 5) == pg and
             c.operand("kSigSaveFile", 21) == pg, "both save requests (%X, %X) put their flags into pG+4" %
             (sysreq, filereq))
    c.expect(c.operand("kSigSaveFile", 28) == task and img.call_target(sysreq + 30) == start and
             img.call_target(filereq + 40) == start,
             "both hand the card's task (%X) to the scheduler's start (FUN_%X)" % (task, start))
    body = (task + 5 + img.i32(task + 1)) & 0xFFFFFFFF if img.u8(task) == 0xE9 else task
    c.expect(img.b(body, 18) == bytes.fromhex("558BECA1") + struct.pack("<I", pg) + bytes.fromhex("81882850000000080000"),
             "the card's task (FUN_%X) marks the card busy as it starts: pG+5028 |= 800" % body)
    c.expect(c.operand("kSigTaskStart", 16) == table + 4 and img.in_section(table, ".data"),
             "the scheduler's levels at %X (0x348 bytes each: +4 the state byte, +33C the task); level 1's at %X" %
             (table, table + 0x348))
    lo, hi = img.sections[".text"]
    hands = [lo + m.start() for m in re.finditer(re.escape(b"\x68" + struct.pack("<I", task)), img.mem[lo - img.base:hi - img.base])]
    to_level1 = []
    for h in hands:
        call = img.b(h + 7, 12).find(b"\xE8")  # past the push of level 1 (a store may sit between it and the call)
        to_level1.append(img.b(h + 5, 2) == b"\x6A\x01" and call >= 0 and img.call_target(h + 7 + call) == start)
    c.expect(len(hands) == 4 and all(to_level1),
             "the card's task is handed over in 4 places (the load list, a save to a file, the system data's, the "
             "start-up check: %s), each to level 1" % ", ".join("%X" % h for h in hands))
    # The fix of a save turned away goes where the start turns a task away: 34 bytes that only call two empty
    # debug prints (each a lone ret), between the je past them and the return of 0.
    prints = start + 23
    p1, p2 = img.call_target(prints + 6), img.call_target(prints + 26)
    c.expect(img.u8(start + 21) == 0x74 and start + 23 + img.u8(start + 22) == start + 61 and
             img.b(prints + 34, 4) == bytes.fromhex("33C05DC3") and img.u8(p1) == 0xC3 and img.u8(p2) == 0xC3,
             "the fix: the 34 bytes at %X call only two empty debug prints (%X, %X), then 0 is returned" %
             (prints, p1, p2))

    print("\n-- patch sites")
    a = one("kSigHurtLeon") + 13
    c.expect(img.b(a, 7) == bytes.fromhex("6629BEB44F0000"), "God mode: sub [esi+4FB4],di at %X" % a)
    a = one("kSigHurtAshley") + 13
    c.expect(img.b(a, 7) == bytes.fromhex("6629BEB84F0000"), "Ashley: sub [esi+4FB8],di at %X" % a)
    a = one("kSigHurtEnemy") + 7
    c.expect(img.b(a, 3) == bytes.fromhex("3BC77D"), "One hit kills: cmp eax,edi; jge at %X" % a)
    # The kills that go round LifeDownSet2. The player's zero the HP and go on to the damage reaction.
    def jump(va):
        op = img.u8(va)
        if 0x70 <= op <= 0x7F or op == 0xEB:
            return va + 2 + struct.unpack_from("<b", img.b(va + 1, 1))[0]
        if op == 0x0F and img.u8(va + 1) & 0xF0 == 0x80:
            return va + 6 + img.i32(va + 2)
        return 0

    def near(va, n, pattern):
        rx = re.compile(b"".join(b"." if t.startswith("?") else re.escape(bytes([int(t, 16)])) for t in pattern.split()),
                        re.S)
        m = rx.search(img.b(va, n + len(pattern.split())))
        return va + m.start() if m and m.start() < n else 0
    # El Gigante's hit check takes its damage without LifeDownSet2: a body hit none (the stagger), the parasite
    # twice the damage. Below 1 is the death (+FC = 3), from the body's check (the jne's end) and from the
    # parasite's way on (je to the body's own "above 0", jle to the death).
    g = one("kSigHurtGigante")
    body = jump(g + 6)
    death = near(body + 10, 0x30, "C7 86 FC 00 00 00 03 00 00 00")
    c.expect(body == g + 0x72 and jump(g + 61) == body - 14 and jump(g + 71) == body + 10 and
             img.b(body - 14, 10) == bytes.fromhex("6683BE24030000000F8F") and
             img.b(body, 9) == bytes.fromhex("6683BE24030000007F") and death,
             "One hit kills: El Gigante's hit check at %X (42 bytes -> the health zeroed at %X for a hit with damage but "
             "a flash's), the body's check at %X, the death below 1 at %X (+FC = 3 at %X)"
             % (g, g + 42, body, body + 10, death))
    k = one("kSigHurtGiganteKnife") + 9
    c.expect(img.b(k, 8) == bytes.fromhex("66838024030000BA"),
             "One hit kills: the knife on El Gigante's back, add [eax+324],-70 at %X (-> and [eax+324],0)" % k)
    # Krauser's hit check: mutated (types 2, 3) a hit on his armoured arm glances off; else the hit's damage goes
    # through LifeDownSet2 - the one kSigHurtEnemy is in - with "survive at 1" unless mutated.
    kr = one("kSigHurtKrauser")
    arm = img.call_target(kr + 38)
    to_damage = jump(kr + 45)
    damage = img.call_target(to_damage)
    lds = img.call_target(to_damage + 29)
    prl = near(damage, 0x60, "80 BE 2E 03 00 00 2E 75 15 8A 8E 01 01 00 00 80 F9 02 74 05 80 F9 03 75 05 B8 0F 27 00 "
                             "00 C3")
    c.expect(img.b(arm, 12)[:9] == bytes.fromhex("8A81010100003C0274") and img.b(arm + 10, 2) == b"\x3C\x03",
             "Krauser's arm test (%X) answers only for types 2 and 3" % arm)
    c.expect(img.u8(kr + 45) == 0x74 and near(to_damage, 1, "E8 ?? ?? ?? ?? 8B F8 8A 86 01 01 00 00 3C 02 74 ?? 3C 03 "
                                                             "74 ?? 6A 01 EB ?? 53 53 57 56 E8") and
             lds < one("kSigHurtEnemy") < lds + 0x500,
             "One hit kills: Krauser's arm's je at %X (-> jmp) to his damage (%X), then LifeDownSet2 (%X) with "
             "'survive at 1' unless mutated" % (kr + 45, damage, lds))
    c.expect(prl == damage + 0x50,
             "One hit kills: the damage's tail at %X, the P.R.L. 412's 9999 on him mutated (31 bytes -> at least "
             "1000 for any damage)" % prl)
    k1, k2 = f["kSigKillFlag"]
    rock = one("kSigCrushRock")
    react = img.call_target(rock + 24)
    c.expect(c.operand("kSigKillFlag", 8) == pg and c.operand("kSigCrushRock", 7) == pg and
             c.operand("kSigCrushTruck", 1) == pg, "the player's kills zero the HP through pG's slot")
    c.expect(img.call_target(k1 + 26) == react and img.call_target(k2 + 27) == react,
             "the attacks' kill flag (EmAtkSetDamagePL %X, EmAtkHitCk %X) and the rock go on to one damage reaction "
             "(PlDamageSet, %X)" % (k1, k2, react))
    for a in (k1, k2):
        c.expect(img.u8(a + 4) == 0x74 and jump(a + 4) == a + 21 and img.b(a + 14, 2) == b"\x66\x89" and
                 img.b(a + 17, 4) == bytes.fromhex("B44F0000"),
                 "God mode: the kill flag's je skips exactly its mov [pG+4FB4],0 at %X (-> NOPs)" % (a + 14))
    c.expect(img.b(rock + 17, 7) == bytes.fromhex("668981B44F0000"),
             "God mode: a rock's mov [ecx+4FB4],ax (0) at %X, PlDamageSet(8) after it" % (rock + 17))
    truck = one("kSigCrushTruck")
    push8 = near(truck + 20, 0x60, "6A 08 E8")
    c.expect(img.b(truck + 13, 7) == bytes.fromhex("668990B44F0000") and push8 and
             img.call_target(push8 + 2) == react,
             "God mode: the truck's mov [eax+4FB4],dx (0) at %X, PlDamageSet(8) at %X" % (truck + 13, push8 + 2))
    # The partner's: her hit check's heavy hits, and the kills her armour is let off.
    blast = one("kSigAshleyBlast")
    radius = c.operand("kSigAshleyBlast", 2)
    radius_sq = struct.unpack_from("<f", img.b(radius, 4))[0]
    fall = jump(blast + 17)
    c.expect(img.in_section(radius, ".rdata") and radius_sq == 9000000.0,
             "her heavy hits: the radius %g (3000 squared, [%X])" % (radius_sq, radius))
    c.expect(img.b(blast + 17, 1) == b"\x7A" and img.b(fall, 8) == bytes.fromhex("575353680F270000"),
             "Ashley: jp at %X to the fall (%X: 9999 damage); past it she ducks (routine 7) (-> NOPs)"
             % (blast + 17, fall))
    stomp = one("kSigAshleyStomp")
    c.expect(c.operand("kSigAshleyStomp", 14) == pg and jump(stomp + 25) == jump(stomp + 7),
             "Ashley: El Gigante's stomp - the armour's je at %X goes where 'already hit' does (%X) (-> nop; jmp)"
             % (stomp + 25, jump(stomp + 25)))
    c.expect(c.operand("kSigAshleyTruck", 1) == pg, "the truck's two checks read pG's slot")
    for a in f["kSigAshleyTruck"]:
        hit13 = near(a + 14, 0x40, "6A 13 50 50")
        zero = near(jump(a + 12), 12, "66 89 ?? B8 4F 00 00")
        c.expect(img.u8(a + 12) == 0x75 and hit13 and zero,
                 "Ashley: the truck's jne at %X to the zeroing (%X); the armour's heavy hit (push 13) at %X (-> NOPs)"
                 % (a + 12, zero, hit13))
    stone = one("kSigAshleyRock")
    c.expect(jump(stone + 16) == jump(stone + 7),
             "Ashley: a rock - the armour's je at %X goes where the others that let her be do (-> jmp)" % (stone + 16))
    a = one("kSigAmmo") + 12
    c.expect(img.b(a, 4) == bytes.fromhex("66895708"), "Infinite ammo: mov [edi+8],dx at %X" % a)
    for s, item in (("kSigArrows", 0x72), ("kSigBolts", 0x11)):
        a = one(s)
        c.expect(img.u8(a + 1) == item, "%s: push %X then the call at %X" % (s, item, a + 4))
    consume = {img.call_target(one(s) + 4) for s in ("kSigArrows", "kSigBolts")}
    c.expect(len(consume) == 1, "both call the same item-use routine (%s)" % ", ".join("%X" % t for t in consume))
    a = one("kSigPlayTime")
    c.expect(c.operand("kSigPlayTime", 2) == pg, "the play time update reads pG's slot")
    c.expect(img.b(a + 9, 6) == bytes.fromhex("0186A44F0000"), "Freeze play time: add [esi+4FA4],eax at %X" % (a + 9))
    a = one("kSigCountdown") + 3
    c.expect(img.b(a, 3) == bytes.fromhex("D86170"), "Freeze countdown: fsub [ecx+70] at %X" % a)
    # The option screen keeps the countdown aside while it is up (the editor writes the copy too).
    countdown, keep = cockpit + 0x98, c.operand("kSigClockKeep", 1)
    c.expect(c.operand("kSigClockKeep", 6) == countdown and keep == cockpit + 0xAC,
             "the option screen's opening copies the CountDown (%X) to the Cockpit's +AC (%X)" % (countdown, keep))
    c.expect(c.operand("kSigClockKeep", 38) == option, "then puts OptionOpenFlag (%X) up" % option)
    keep_fn = img.call_target(one("kSigClockKeep") + 10)
    c.expect(img.b(keep_fn, 20) == bytes.fromhex("558BEC568B7508578BF98B078906D94708D95E04"),
             "FUN_%X keeps the state and the frames: copy+0 = CountDown+0, copy+4 = CountDown+8" % keep_fn)
    a = one("kSigClockBack")
    c.expect(c.operand("kSigClockBack", 18) == keep and c.operand("kSigClockBack", 23) == countdown,
             "the option screen's closing puts the CountDown back from the same copy")
    back_fn = img.call_target(a + 27)
    c.expect(img.b(back_fn, 20) == bytes.fromhex("558BEC53568B75088B068BD98903D94604D95B08"),
             "FUN_%X puts them back: CountDown+0 = copy+0, CountDown+8 = copy+4" % back_fn)
    start = a & ~0xF  # the function's start: aligned, after the padding
    while img.b(start - 1, 1) != b"\xCC" and a - start < 0x200:
        start -= 0x10
    head = img.b(start, a - start)
    put_down = head.find(b"\xA2" + struct.pack("<I", option))
    c.expect(put_down > 0 and b"\x32\xC0" in head[:put_down],
             "the closing (FUN_%X) puts OptionOpenFlag down (xor al,al; mov [flag],al at %X) before it" %
             (start, start + put_down))
    # Every other place that keeps it or puts it back (the sub-screen, the title's options) uses the same copy.
    lo, hi = img.sections[".text"]
    sites = {keep_fn: [], back_fn: []}
    for m in re.finditer(b"\xE8", img.mem[lo - img.base:hi - img.base]):
        s = lo + m.start()
        t = (s + 5 + img.i32(s + 1)) & 0xFFFFFFFF
        if lo <= t < hi and (img.u8(t) != 0xE9 or (t + 5 + img.i32(t + 1)) & 0xFFFFFFFF in sites):
            t = img.call_target(s)
            if t in sites:
                sites[t].append(s)
    by_address = b"\x68" + struct.pack("<I", keep) + b"\xB9" + struct.pack("<I", countdown)  # push copy; mov ecx,CD
    by_cockpit = bytes.fromhex("8D81AC000000" "50" "81C198000000")  # lea eax,[ecx+AC]; push eax; add ecx,98
    odd = [s for s in sites[keep_fn] + sites[back_fn] if img.b(s - 10, 10) != by_address and
           img.b(s - 13, 13) != by_cockpit]
    c.expect(not odd and one("kSigClockKeep") + 10 in sites[keep_fn] and a + 27 in sites[back_fn],
             "every call keeps or puts back the Cockpit's own copy (kept at %s; put back at %s)%s" %
             (" ".join("%X" % s for s in sites[keep_fn]), " ".join("%X" % s for s in sites[back_fn]),
              "; not at " + " ".join("%X" % s for s in odd) if odd else ""))
    a = one("kSigSaveCount")
    c.expect(c.operand("kSigSaveCount", 1) == pg and c.operand("kSigSaveCount", 18) == pg,
             "the save builder reads pG's slot (twice)")
    c.expect(img.b(a + 10, 7) == bytes.fromhex("6601B09C4F0000"), "Save without counting: add [eax+4F9C],si at %X"
             % (a + 10))
    c.expect(c.operand("kSigSaveCount", 23) == 999, "the save count's cap: %d" % c.operand("kSigSaveCount", 23))
    a = one("kSigBuy")
    c.expect(c.operand("kSigBuy", 23) == pg, "the merchant's purchase reads pG's slot")
    c.expect(img.b(a + 37, 2) == bytes.fromhex("2918"), "Infinite money: sub [eax],ebx (the price) at %X" % (a + 37))
    a = one("kSigTuneUp")
    c.expect(c.operand("kSigTuneUp", 26) == pg, "the merchant's tune-up reads pG's slot")
    c.expect(c.operand("kSigTuneUp", 13) == item_mgr and c.operand("kSigTuneUp", 5) == item_mgr + 0xC,
             "and ItemMgr (+C, the equipped weapon) before it")
    c.expect(img.b(a + 30, 6) == bytes.fromhex("2988A84F0000"), "Infinite money: sub [eax+4FA8],ecx at %X" % (a + 30))
    for s, jcc, slots in (("kSigShotA", 9, (12, 23, 34, 45)), ("kSigShotB", 5, (8, 19, 30, 41))):
        a = one(s)
        c.expect(all(c.operand(s, o) == pg for o in slots), "%s: the four adds go through pG's slot" % s)
        c.expect(img.u8(a + jcc + 1) == 0x16 and img.u8(a + jcc) in (0x74, 0x7E),
                 "Misses don't count: %s's jump past the hits at %X (+16; +2C passes the shots too)" % (s, a + jcc))
    a = one("kSigDeaths")
    c.expect(all(c.operand("kSigDeaths", o) == pg for o in (2, 15, 29)), "the continue's death count reads pG's slot")
    c.expect(img.u8(a + 19) == 0x47 and img.u8(a + 33) == 0x43,
             "Deaths don't count: inc edi (the chapter) at %X, inc ebx (the game) at %X" % (a + 19, a + 33))
    c.expect(one("kSigCheckpoint") + 0x6A == a, "it is in the continue kSigCheckpoint reads (+6A on)")

    print("\n-- Steady aim")
    sway, kick = one("kSigAimSway"), one("kSigAimKick")
    fv, fh = c.operand("kSigAimSway", 7), c.operand("kSigAimSway", 18)
    c.expect(fv != fh and c.operand("kSigAimSway", 33) == fv and c.operand("kSigAimSway", 55) == fh and
             img.in_section(fv, ".data") and img.in_section(fh, ".data"),
             "the drift: the random factors stored at %X (up-down) and %X (left-right) are the ones it loads"
             % (fv, fh))
    rng = {img.call_target(a) for a in (sway, sway + 11, kick + 4, kick + 35)}
    c.expect(len(rng) == 1,
             "the drift and the shot draw from one random function (%s)" % ", ".join("%X" % t for t in rng))
    c.expect(c.operand("kSigAimSway", 27) == pg and c.operand("kSigAimKick", 17) == pg and
             c.operand("kSigAimKick", 47) == pg, "both read the frame's delta through pG's slot")
    scale = c.operand("kSigAimKick", 23)
    c.expect(scale == c.operand("kSigAimKick", 56) and img.in_section(scale, ".data"),
             "the shot: both throws scaled by %X" % scale)
    drift = kick + 4 + struct.unpack_from("<b", img.b(kick + 3, 1))[0]
    c.expect(drift < sway < drift + 0x40, "the shot's test sends every other call to the drift (%X), 0x%X bytes "
             "before its factors are drawn" % (drift, sway - drift))
    for a, what in ((sway + 31, "fld [up-down factor]"), (sway + 53, "fld [left-right factor]")):
        c.expect(img.b(a, 2) == b"\xD9\x05", "Steady aim: %s at %X (-> fldz)" % (what, a))
    for a, what in ((kick + 21, "up-down"), (kick + 54, "left-right")):
        c.expect(img.b(a, 2) == b"\xD8\x0D" and img.u32(a + 2) == scale,
                 "Steady aim: fmul [%X] (the shot's %s throw) at %X (-> fstp st(0); fldz)" % (scale, what, a))
    # The shots' spread: each site an fmul right after a call to the same random function.
    rng = rng.pop() if len(rng) == 1 else None
    f64 = lambda va: struct.unpack_from("<d", img.b(va, 8))[0]
    f32 = lambda va: struct.unpack_from("<f", img.b(va, 4))[0]
    fwd = c.operand("kSigSpreadGun", 2)
    gun_scale = c.operand("kSigSpreadGun", 16)
    vec = c.operand("kSigSpreadAuto", 8)
    c.expect(fwd == c.operand("kSigSpreadAuto", 2) and f32(fwd) == -50000.0,
             "the handguns' and machine guns' shots: %g forward ([%X])" % (f32(fwd), fwd))
    c.expect(all(c.operand(s, o) == gun_scale for s, o in (("kSigSpreadGun", 30), ("kSigSpreadAuto", 19),
                                                            ("kSigSpreadAuto", 36))) and f64(gun_scale) == 200.0,
             "and %g sideways at random, both ways ([%X])" % (f64(gun_scale), gun_scale))
    c.expect(img.in_section(vec, ".data") and c.operand("kSigSpreadAuto", 25) == vec + 4 and
             c.operand("kSigSpreadAuto", 41) == vec, "the machine guns' two shots build one vector at %X" % vec)
    sh = c.operand("kSigSpreadShotgun", 7)
    c.expect(sh == c.operand("kSigSpreadShotgun", 24) and f64(sh) == 100.0,
             "a shotgun's centre pellet: %g sideways at random ([%X])" % (f64(sh), sh))
    mt = c.operand("kSigSpreadMounted", 7)
    c.expect(mt == c.operand("kSigSpreadMounted", 21) and img.in_section(mt, ".data") and f32(mt) == 1000.0,
             "the mounted gun: %g sideways at random ([%X], a float in .data)" % (f32(mt), mt))
    th = img.u32(one("kSigSpreadThrow") + 7)
    c.expect(f64(th) == 15.0, "a throw: %g sideways speed at random ([%X])" % (f64(th), th))
    sites = [(h + o, "kSigSpreadGun") for h in f["kSigSpreadGun"] for o in (14, 28)]
    sites += [(h + o, "kSigSpreadAuto") for h in f["kSigSpreadAuto"] for o in (17, 34)]
    sites += [(one("kSigSpreadShotgun") + o, "kSigSpreadShotgun") for o in (5, 22)]
    sites += [(one("kSigSpreadMounted") + o, "kSigSpreadMounted") for o in (5, 19)]
    sites += [(one("kSigSpreadThrow") + 5, "kSigSpreadThrow")]
    for a, s in sites:
        c.expect(img.u8(a) in (0xD8, 0xDC) and img.u8(a + 1) == 0x0D and img.call_target(a - 5) == rng,
                 "Steady aim: %s: call random; fmul [%X] at %X (-> fstp st(0); fldz)" % (s, img.u32(a + 2), a))
    # The hold: the player's slot, and the part lookups the laser and the shots make.
    player = c.operand("kSigPlayer", 2)
    c.expect(c.operand("kSigPlayer", 19) == pg and img.in_section(player, ".data"), "the player's slot %X" % player)
    lz = one("kSigHoldLaser")
    part = img.call_target(lz + 20)
    c.expect(c.operand("kSigHoldLaser", 29) == pg, "the laser's line picks its part by pG's weapon number")
    c.expect(img.b(part, 3) == b"\x55\x8B\xEC" and b"\xC2\x04\x00" in img.b(part, 0x50),
             "the part lookup %X: thiscall(model, n), ret 4" % part)
    c.expect(c.operand("kSigHoldAuto", 13) == vec + 0xC and c.operand("kSigHoldAuto", 18) == vec + 0xC,
             "the machine guns' start vector at %X, after their direction" % (vec + 0xC))
    holds = [(lz + 19, 1, "the laser: push part; call")]
    holds += [(h, 4, "%s: push 10; mov ecx,esi; call" % s) for s in ("kSigHoldHand", "kSigHoldAuto", "kSigHoldShotgun")
              for h in f[s]]
    for a, call, what in holds:
        c.expect(img.call_target(a + call) == part, "Steady aim's hold: %s at %X (-> mov eax,held)" % (what, a))
    # The stance: every aim's set-up hands the one blend helper its idle loop (attribute 4: push 4 -> push C, the
    # clock stopped), then sets the blend and moves the motion, through the same three functions.
    encs = (("kSigAimLoopFar", 33, 37, 44, 50, 57, 64), ("kSigAimLoopNear", 27, 31, 38, 44, 51, 58),
            ("kSigAimLoopShotgun", 30, 34, 41, 47, 54, 61), ("kSigAimLoopAuto", 38, 42, 49, 55, 62, 69))
    seen = {"helper": set(), "set": set(), "blend": set(), "set_blend": set(), "move": set()}
    loops = []
    for s, helper, set_, blend, helper2, set_blend, move in encs:
        for h in f[s]:
            seen["helper"].update((img.u32(h + helper), img.u32(h + helper2)))
            seen["set"].add(img.call_target(h + set_))
            seen["blend"].add(img.u32(h + blend))
            seen["set_blend"].add(img.call_target(h + set_blend))
            seen["move"].add(img.call_target(h + move))
            loops.append((h, s))
    told = ", ".join("%s %s" % (k, "/".join("%X" % x for x in v if x)) for k, v in seen.items())
    one_each = {k: next(iter(v)) if len(v) == 1 else None for k, v in seen.items()}
    c.expect(all(one_each.values()), "the aims' set-ups agree on one helper, motion set, blend, blend set and move "
             "(%s)" % told)
    helper = one_each["helper"]
    c.expect(helper and img.in_section(helper, ".data") and one_each["blend"] and
             img.in_section(one_each["blend"], ".data"), "the blend helper %s and the blend %s in .data"
             % ("%X" % helper if helper else "-", "%X" % one_each["blend"] if one_each["blend"] else "-"))
    ms = one_each["set"]
    # The motion set: B (the helper's +18) gets the aim-up motion and the loop's first frame, attribute 4 of its own.
    c.expect(ms and img.b(ms, 3) == b"\x55\x8B\xEC" and b"\x6A\x04" in img.b(ms, 0x80) and
             b"\x8D\x5E\x18" in img.b(ms, 0x80), "the motion set %s: thiscall(helper, ...), motion B at the helper's "
             "+18" % ("%X" % ms if ms else "-"))
    c.expect(len(loops) == 11, "eleven aims' idle loops (%d)" % len(loops))
    for a, s in loops:
        c.expect(img.b(a, 2) == b"\x6A\x04", "Steady aim's stance: %s: push 4 at %X (-> push C)" % (s, a))

    print("\n-- Automatic QTEs")
    a = one("kSigQteCheck")
    table = c.operand("kSigQteCheck", 51)
    c.expect(a < table < a + 0x800, "the action buttons' check: its kinds' jump table at %X, after the dispatch" % table)
    entry = [img.u32(table + 4 * i) for i in range(19)]  # kinds 1..19
    k13 = entry[12]
    flag = img.u32(k13 + 4)
    c.expect(img.b(k13, 4) == bytes.fromhex("5F5EC605") and img.b(k13 + 8, 10) == bytes.fromhex("0132C05B8BE55DC20400"),
             "kind 13 (%X): pop edi; pop esi; mov byte [%X],1; xor al,al (at %X); ...; ret 4" % (k13, flag, k13 + 9))
    raise_flag = b"\xC6\x05" + flag.to_bytes(4, "little") + b"\x01"
    raisers = [k for k in range(1, 20) if a < entry[k - 1] < table and raise_flag in img.b(entry[k - 1], 24)]
    c.expect(raisers == [2, 3, 4, 9, 10, 13], "the kinds whose code raises the QTE flag: %s (2-4 and 9-10 are sent to "
             "13's code, entries at %X and %X)" % (raisers, table + 4, table + 32))
    keytrg = set()
    for s, call, test in (("kSigQteBoulder", 14, 22), ("kSigQteSwim", 14, 22), ("kSigQteRope", 18, 26),
                          ("kSigQteGigante", 16, 24), ("kSigQteMinecart", 14, 22), ("kSigQteStatueRun", 18, 26),
                          ("kSigQteStatueClimb", 0, 8), ("kSigQteKrauser", 10, 18), ("kSigQteBridgeClimb", 0, 8),
                          ("kSigQteRobotGrab", 38, 46)):
        for h in f[s]:
            keytrg.add(img.call_target(h + call))
            c.expect(img.b(h + test, 4)[:2] == b"\x85\xC0" and img.u8(h + test + 2) in (0x74, 0xA1),
                     "%s: KeyTrgCheck, then test eax,eax at %X" % (s, h + test))
    c.expect(len(keytrg) == 1, "every press poll calls the one key trigger check (%s)" % ", ".join("%X" % t for t in keytrg))
    c.operand("kSigQteGigante", 3)
    for s, call in (("kSigQteStatueEnd", 24), ("kSigQteLasersLast", 24)):
        t = img.call_target(one(s) + call)
        body = img.b(t, 0x80)
        reads = sorted({struct.unpack_from("<I", body, i + 2)[0] for i in range(len(body) - 6)
                        if body[i] in (0x80, 0x38) and body[i + 1] in (0x3D, 0x05)})
        c.expect(len(reads) == 2 and reads[1] == reads[0] + 1,
                 "%s: test eax,eax after %X, which reads the two trigger bytes (%s)" % (s, t, ", ".join("%X" % r for r in reads)))
        c.expect(img.b(one(s) + 29, 2) == b"\x85\xC0", "%s: test eax,eax at %X" % (s, one(s) + 29))
    # The statue's run: a press adds ftol(the frames since the last one) to the run's counter and holds it to its top.
    a = one("kSigQteStatueRun")
    ftol = img.call_target(a + 35)
    c.expect(img.b(a + 30, 6) == bytes.fromhex("8B7D14D907E8") and b"\xF2\x0F\x2C" in img.b(ftol, 0x20) and
             img.b(a + 46, 2) == b"\x01\x06" and img.b(a + 48, 3) == bytes.fromhex("8D43FF") and
             img.b(a + 55, 4) == bytes.fromhex("7E028906"),
             "the statue's run: a press adds ftol(the frames since the last) (%X, a truncation) to the counter at %X; "
             "jle past its top at %X" % (ftol, a + 46, a + 55))
    a = one("kSigQteStatueJump")
    c.expect(img.b(a, 2) == b"\x0F\x84" and img.i32(a + 2) < 0,
             "the statue's jumps: je back to 'not yet' (%X) at %X" % (a + 6 + img.i32(a + 2), a))
    for a in f["kSigQteLasers"]:
        c.expect(img.u8(a + 29) == 0x74 and a + 20 + struct.unpack_from("<b", img.b(a + 19, 1))[0] == a + 49,
                 "the laser room: je at %X; the dodge at %X, where the Wii's jne (+18) goes too" % (a + 29, a + 49))
    a = one("kSigQteBridge")
    jump = a + 27 + img.i32(a + 23)
    routine = img.u32(jump + 1) if img.u8(jump) == 0x68 else 0
    c.expect(img.b(a + 12, 2) == b"\x74\x0D" and img.b(a + 21, 2) == b"\x0F\x85" and
             all(routine < h < routine + 0x800 for h in f["kSigQteBridgeClimb"]),
             "Saddler's bridges: je at %X, jne at %X to the jump (%X), which sets the player's routine to %X - the one "
             "with both bridge climbs" % (a + 12, a + 21, jump, routine))
    a = one("kSigQteShake")
    c.expect(img.b(a + 59, 8) == bytes.fromhex("740CFF8768050000"),
             "a grab's shake-off: je; inc [edi+568] (a direction) at %X" % (a + 59))

    print("\n-- the tick")
    a = one("kSigTick")
    gct = c.operand("kSigTick", 5)
    c.expect(gct == img.imports.get(("kernel32.dll", "GetCurrentThread")), "GetCurrentThread import %X" % gct)
    c.expect(img.b(a + 10, 2) == bytes.fromhex("FFD7"), "call edi (SetThreadAffinityMask) - returns to %X" % (a + 12))
    sta = img.imports.get(("kernel32.dll", "SetThreadAffinityMask"))
    c.expect(sta is not None, "bio4.exe imports SetThreadAffinityMask (slot %X)" % (sta or 0))
    # The second point: the same frame's QueryPerformanceCounter call, through
    # ebx, which WinMain loads from the import before its loop.
    late = one("kSigTickLate")
    qpc = img.imports.get(("kernel32.dll", "QueryPerformanceCounter"))
    c.expect(qpc is not None and late > a and late - a < 0x80,
             "QueryPerformanceCounter (slot %X) called at %X, %X after the first point - returns to %X" %
             (qpc or 0, late + 7, late - a, late + 9))
    c.expect(c.operand("kSigTickLate", 2) == c.operand("kSigTickLate", 11), "the counter it reads is the one it fills")
    load = b"\x8B\x1D" + struct.pack("<I", qpc or 0)
    c.expect(load in img.b(late - 0x1000, 0x1000), "mov ebx,[QueryPerformanceCounter] before it in WinMain")
    # Between the two points only the frame wait's timing stores and a call to
    # an empty function: the tick sees the same frame from either.
    between = a + 12
    c.expect(img.b(between, 6)[:2] == b"\x8B\x8D" and img.u8(img.call_target(late - 5)) == 0xC3,
             "between them (%X..%X): the counter's stores and a call to %X, an empty function" %
             (between, late, img.call_target(late - 5)))

    print("\n-- the approved game calls")
    a = one("kSigPutInCase")
    c.expect(c.operand("kSigPutInCase", 3) == sub + 0x2AA, "PutInCase's case size byte is SubScreenWk+2AA")
    put = img.call_target(a + 18)
    c.expect(img.in_section(put, ".text"), "PutInCase %X" % put)
    a = one("kSigItemGet")
    c.expect(c.operand("kSigItemGet", 3) == item_mgr, "cItemMgr::get is called on ItemMgr")
    c.expect(c.operand("kSigItemGet", 13) == item_mgr + 0x18, "and m_pNew (ItemMgr+18) read after it")
    get = img.call_target(a + 7)
    c.expect(img.in_section(get, ".text"), "cItemMgr::get %X" % get)
    erase = img.call_target(one("kSigItemErase") + 1)
    c.expect(img.in_section(erase, ".text"), "cItemMgr::erase %X" % erase)

    c.expect(c.operand("kSigCharge", 52) == pg, "WeaponId2ChargeNum reads the difficulty through pG's slot")
    print("\n-- the item rules")
    table = c.operand("kSigItemInfo", 18)
    c.expect(img.in_section(table, ".text"), "itemInfo's kind table %X (0x110 ids)" % table)
    kinds = [img.u8(table + i) for i in range(0x110)]
    c.expect(max(kinds) <= 0x17, "every kind is one of the switch's 24 (max %d)" % max(kinds))
    pieces = c.operand("kSigPieces", 3)
    c.expect(pieces == c.operand("kSigPieces", 45) and img.in_section(pieces, ".data"), "the piece table %X" % pieces)
    n, case_ids = 0, []
    while img.i16(pieces + n * 0x58) != -1 and n < 256:
        case_ids.append(img.i16(pieces + n * 0x58))
        n += 1
    c.expect(0 < n < 256 and 35 in case_ids, "%d pieces (the Handgun's among them)" % n)
    rows_slot = c.operand("kSigCharge", 2)
    charge = c.operand("kSigCharge", 18)
    rows = img.u32(rows_slot)
    c.expect(0 < rows < 64, "WeaponId2ChargeNum: %d rows at %X (count at %X)" % (rows, charge, rows_slot))
    ids = [img.i16(charge + r * 22) for r in range(rows)]
    c.expect(35 in ids and 3 in ids, "the charge table has the Handgun (35) and the Matilda (3)")
    lv = c.operand("kSigMaxLevel", 7)
    lv_end = c.operand("kSigMaxLevel", 23)
    c.expect((lv_end - lv) % 6 == 0 and 0 < (lv_end - lv) // 6 < 64,
             "WeaponId2MaxLevel: %d rows at %X" % ((lv_end - lv) // 6, lv))

    print("\n-- the shooting gallery")
    work = c.operand("kSigGalleryScore", 1)
    score = one("kSigGalleryScore") - 3
    c.expect(img.in_section(work, ".data") and c.operand("kSigGalleryStreak", 90) == work,
             "the gallery's work slot %X (the scoring and the gallery's loop agree)" % work)
    c.expect(c.operand("kSigGalleryStreak", 2) == pg and c.operand("kSigGalleryStreak", 44) == pg,
             "the loop's miss and bonus flags through pG's slot (+5024, +174)")
    c.expect(img.b(score, 3) == b"\x55\x8B\xEC" and img.call_target(one("kSigGalleryHit") + 90) == score,
             "the scoring starts at %X, and a target's hit check calls it" % score)
    points = c.operand("kSigGalleryScore", 81)
    row = lambda t: [img.i32(points + (t * 5 + p) * 4) for p in range(2)]
    c.expect(row(0) == [50, 100] and row(1) == [50, 200] and row(5) == [50, 100] and row(3) == [500, 500] and
             row(2) == [-1000, -1000] and row(4) == [-1000, -1000],
             "the points (body, head) at %X: Ganados %s %s, a bomb's carrier %s, the bonus target %s, Ashley %s %s"
             % (points, row(0), row(1), row(5), row(3), row(2), row(4)))
    task = c.operand("kSigGalleryStreak", 71)
    spawn = near(task, 0x80, "6A 3E B9")
    tmpl = img.u32(spawn + 15) if spawn and img.b(spawn + 12, 3) == b"\x8B\xF0\x68" else 0  # call; mov esi,eax; push entry
    c.expect(img.b(task, 1) == b"\xA1" and tmpl and img.i32(tmpl + 4) == 3,
             "the bonus target's task %X spawns a target of type 3 (its entry %X)" % (task, tmpl))
    count = c.operand("kSigEmPool", 1)
    c.expect(img.in_section(count, ".data") and c.operand("kSigEmPool", 41) == count + 4 and
             c.operand("kSigEmPool", 50) == count - 4 and c.operand("kSigGalleryTargets", 2) == count + 0xC,
             "the enemy manager at %X: +4 the pool's base, +8 its count, +C the stride, +14 the live list"
             % (count - 8))
    step = one("kSigGalleryStep")
    table = c.operand("kSigGalleryStep", 18)
    sizes = []
    for i in range(5):
        dest = img.u32(table + 4 * i)
        sizes.append(img.u32(dest + 1) if img.u8(dest) == 0xB8 else 0 if img.b(dest, 2) == b"\x33\xC0" else -1)
    c.expect(step < table < step + 0x60 and sizes == [4, 4, 8, 0x14, 0],
             "a target's commands: rise and go down 4 bytes, wait 8, move 0x14, stay 0 (%s)" % sizes)
    c.expect(c.operand("kSigGalleryWait", 22) == pg, "a target's wait counts its +4DC down by the frame's delta")

    print("\n-- the Mercenaries")
    # A kill scores on the work's fields by address: the score (+24), the combo (+28), the first kill's timer (+30).
    score = c.operand("kSigMercScore", 32)
    window = c.operand("kSigMercScore", 52)
    c.expect(c.operand("kSigMercScore", 1) == pg and img.in_section(score, ".data") and
             c.operand("kSigMercScore", 43) == score + 4 and c.operand("kSigMercScore", 63) == score + 0xC,
             "a kill scores (%X) on the work at %X: the score +24, the combo +28, the first kill's timer +30"
             % (one("kSigMercScore") - 3, score - 0x24))
    c.expect(img.in_section(window, ".data") and img.i32(window) == 300 and img.i32(window + 4) == 120,
             "the combo's window: %d frames ([%X]), blinking from %d ([%X])" % (img.i32(window), window,
                                                                               img.i32(window + 4), window + 4))
    first, win, bonus = one("kSigMercFirst"), one("kSigMercWindow"), one("kSigMercBonus")
    c.expect(first < win < bonus < first + 0x800 and c.operand("kSigMercFirst", 13) == pg and
             c.operand("kSigMercWindow", 35) == pg and c.operand("kSigMercWindow", 50) == window + 4,
             "the HUD (%X..%X): the timers less pG's delta, the window's blink mark the same constant" % (first, bonus))
    c.expect(img.b(first + 20, 3) == bytes.fromhex("D86070"),
             "Endless combo: the first kill's timer, fsub [eax+70] at %X (-> NOPs)" % (first + 20))
    c.expect(img.b(win + 39, 3) == bytes.fromhex("D86170"),
             "Endless combo: the combo's window, fsub [ecx+70] at %X (-> NOPs)" % (win + 39))
    near_jump = lambda va: va + 6 + img.i32(va + 2)  # 0F 8x rel32
    c.expect(img.b(bonus + 17, 2) == b"\x0F\x8E" and img.b(bonus + 26, 2) == b"\x0F\x85" and
             near_jump(bonus + 17) == near_jump(bonus + 26),
             "Endless combo: the bonus waits for the combo's end, jne at %X (-> NOPs) - to %X, where nothing earned "
             "goes too" % (bonus + 26, near_jump(bonus + 26)))
    # The Bonus Points time (+54) is counted down the same way, and the bonus waits for it to be over too.
    bt = one("kSigMercBonusTime")
    c.expect(bt < bonus < bt + 0x400 and img.b(bt + 23, 2) == b"\x0F\x8A" and near_jump(bt + 23) == bonus and
             c.operand("kSigMercBonusTime", 30) == pg and c.operand("kSigMercBonusTime", 48) == window + 8 and
             img.i32(window + 8) == 120,
             "the Bonus Points time (%X): less pG's delta unless it is ending, blinking from %d ([%X]); past it, %X"
             % (bt, img.i32(window + 8), window + 8, bonus))
    c.expect(img.b(bt + 37, 3) == bytes.fromhex("D86070"),
             "Bonus time always on: the Bonus Points time, fsub [eax+70] at %X (-> NOPs)" % (bt + 37))
    c.expect(img.b(bonus + 32, 4) == bytes.fromhex("8D4E5453") and img.u8(bonus + 36) == 0xE8 and
             img.b(bonus + 41, 4) == bytes.fromhex("84C00F84") and near_jump(bonus + 43) == near_jump(bonus + 17),
             "Bonus time always on: the bonus waits for the Bonus Points time to be over (lea ecx,[esi+54]; call %X), "
             "je at %X (-> NOPs) - to %X" % (img.call_target(bonus + 36), bonus + 43, near_jump(bonus + 43)))
    # The Ganados' class: its vtable's move runs its hit check, which takes a hit's damage through LifeDownSet2.
    vt = c.operand("kSigEm10", 7)
    move = img.u32(vt + 0x10) if img.in_section(vt, ".rdata") else 0
    if move and img.u8(move) == 0xE9:
        move = (move + 5 + img.i32(move + 1)) & 0xFFFFFFFF
    call = near(move, 0x40, "80 BE FC 00 00 00 00 74 07 8B C6 E8") if move else 0
    hit = img.call_target(call + 11) if call else 0
    damage = one("kSigEm10Damage")
    lds = img.call_target(damage + 15)
    c.expect(hit and hit == one("kSigEm10Hit"),
             "the Ganados' class: vtable %X, its move %X runs its hit check (%X) once past its set-up"
             % (vt, move, hit))
    c.expect(hit < damage < hit + 0x900 and lds < one("kSigHurtEnemy") < lds + 0x500,
             "the hit check's damage at %X goes through LifeDownSet2 (%X)" % (damage, lds))
    # The scene's area triggers clear the room's area flags each frame, then set those of the areas the player is in.
    a = one("kSigAreaReset")
    # The models' memory: modelInit's record pool and a model's parts pool, each through its manager's slot.
    models_slot, parts_slot = c.operand("kSigModelInfos", 2), c.operand("kSigModelParts", 8)
    message = c.operand("kSigModelInfos", 20)
    c.expect(img.in_section(models_slot, ".data") and parts_slot == models_slot + 4 and
             img.b(message, 37) == b"modelInit() cModelInfo alloc failed.\0",
             "the models' memory: the model pool's manager in [%X] (modelInit, %X), the parts pool's in [%X]"
             % (models_slot, one("kSigModelInfos"), parts_slot))
    # The village's sweep: its work's frames since the last, past 300 cEmMgr::destroy on every body gone.
    v = one("kSigVillageSweep")
    village = c.operand("kSigVillageSweep", 2)
    sweep = img.call_target(v + 69)
    test = near(sweep, 0x80, "8B 16 8B 42 48 8B CE FF D0 85 C0 74 ?? 56 B9")
    c.expect(img.in_section(village, ".data") and c.operand("kSigVillageSweep", 28) == pg and
             c.operand("kSigVillageSweep", 44) == pg and img.u32(v + 60) == 300 and test and
             img.u32(test + 15) == c.operand("kSigEmPool", 1) - 8,
             "the village's work in [%X]: its +C past 300 sweeps (%X: cEmMgr::destroy on every body gone, vtable +48)"
             % (village, sweep))
    # The village's pickups' events: each function sends its entries only while fewer than ten are about.
    ev = one("kSigVillageEvents")
    records = c.operand("kSigVillageEvents", 39)
    c.expect(all(c.operand("kSigVillageEvents", o) == pg for o in (26, 55, 104, 136)) and
             all(c.operand("kSigVillageEvents", o) == records for o in (68, 117, 149)),
             "the village's pickups' events (%X): the room's records through the manager at %X" % (ev, records))
    for k, call in enumerate((ev + 3, ev + 80, ev + 165)):
        fn = img.call_target(call)
        body = img.b(fn, 0x400)
        end = body.find(b"\xC3\xCC")
        checks = [i for i in range(end) if body[i:i + 3] == b"\x83\x38\x0A"]
        sends = len(re.findall(rb"\x6A\xFF\x6A.\x8D\x4D.\xE8", body[:end], re.S))
        c.expect(body[:4] == bytes.fromhex("558BECA1") and img.u32(fn + 4) == village and end > 0 and
                 len(checks) == sends > 0,
                 "the village's pickup's event %d (%X): %d entries, each sent only while fewer than ten are about "
                 "(cmp [work],0A at %s -> FF)" % (k + 1, fn, sends, ", ".join("%X" % (fn + i + 2) for i in checks)))
    # The village's area lists (its function calls each by its area flag) and its events: the entries each sends.
    def sent_entries(f):
        body = img.b(f, 0x1000)
        end = body.find(b"\xC3\xCC")
        entries, by_register, targets = set(), 0, set()
        for m in re.finditer(rb"\x6A\xFF(?:\x6A(.)|\x68(.)\x00\x00\x00|[\x50-\x57])\x8D(?:\x4D.|\x8D....)\xE8", body[:end],
                             re.S):
            targets.add(img.call_target(f + m.end() - 1))
            if m.group(1) is not None:
                entries.add(m.group(1)[0])
            elif m.group(2) is not None:
                entries.add(m.group(2)[0])
            else:
                by_register += 1
        return entries, by_register, targets
    vf = one("kSigVillageSweep") - 0x20
    body = img.b(vf, 0x300)
    area_fns = [vf + m.end() - 1 for m in re.finditer(rb"(?:\x85.\x7C\x01\x00\x00|\xF7\x80\x7C\x01\x00\x00....)\x74.\xE8",
                                                      body, re.S)]
    area, registers, setem = set(), 0, set()
    for call in area_fns:
        e, r, t = sent_entries(img.call_target(call))
        area |= e
        registers += r
        setem |= t
    events = set()
    for call in (ev + 3, ev + 80, ev + 165):
        e, r, t = sent_entries(img.call_target(call))
        events |= e
        setem |= t
    c.expect(len(area_fns) == 7 and len(area) == 101 and registers == 1 and len(events) == 11 and not (area & events)
             and len(setem) == 1,
             "the village's lists: 7 area lists send %d entries (%d by a register), its events %d (%s), none in both, all "
             "through setEm (%s)" % (len(area), registers, len(events), " ".join("%X" % x for x in sorted(events)),
                                    " ".join("%X" % x for x in setem)))
    # Its chainsaws' waves, each once: the flag (pG+174, bit 80000000 >> n) put up by FUN_004ab510(n), then two
    # entries sent whatever is about by a function that clears each one's sent bit and dead mark before setEm.
    wv = one("kSigVillageWaves")
    layout = ((0, 21, 31, 38, 39, 51, 52, 59, 62, 63), (68, 89, 99, 106, 107, 119, 120, 127, 130, 131),
              (136, 156, 166, 173, 174, 186, 187, 194, 197, 198))
    waves, setters, senders = [], set(), set()
    for start, pg_at, flag_at, n_at, set_at, e_at, send1, disp_at, e2_at, send2 in layout:
        flag, n, e = img.u32(wv + flag_at), img.b(wv + n_at, 1)[0], img.b(wv + e_at, 1)[0]
        disp = struct.unpack("<b", img.b(wv + disp_at, 1))[0]
        c.expect(img.u32(wv + start + 1) == village and img.u32(wv + pg_at) == pg and n < 32 and
                 flag == 0x80000000 >> n and img.b(wv + e2_at, 1)[0] == e and disp == 1 - e,
                 "the village's chainsaws' wave at %X: at %d deaths, flag %08X (n %d), entries %X and %X"
                 % (wv + start, img.b(wv + start + 16, 1)[0], flag, n, e, e + 1))
        waves.append((e, e + 1))
        setters.add(img.call_target(wv + set_at))
        senders |= {img.call_target(wv + send1), img.call_target(wv + send2)}
    setter, sender = min(setters), min(senders)
    c.expect(len(setters) == 1 and img.b(setter, 0x26) == bytes.fromhex(
        "558BEC8B4D088B15") + struct.pack("<I", pg) + bytes.fromhex("8BC1C1E8058D848274010000" "83E11FBA00000080D3EA09105DC3"),
             "the waves' flag (%X): pG+174's bit 80000000 >> n of the word n / 32 put up" % setter)
    head = img.b(sender, 0x80)
    m = re.search(rb"\x6A\x01\x6A\x01\x6A\x01\x6A\xFF\x56\x8D\x4D.\xE8", head, re.S)
    c.expect(len(senders) == 1 and m and img.call_target(sender + m.end() - 1) in setem and
             bytes.fromhex("8BC6C1E005") + bytes.fromhex("80A40810540000FD") in head and
             bytes.fromhex("8D840A2C500000") in head,
             "the waves' send (%X): the entry (esi) not sent (its +0 bit 2 cleared) nor dead (the mask at +502C), then "
             "setEm" % sender)
    in_waves = {e for w in waves for e in w}
    c.expect(len(in_waves) == 6 and not (in_waves & area) and not (in_waves & events) and
             wv == ev + 170,
             "the village's chainsaws: entries %s, in no area list or event, sent right after the events"
             % " ".join("%X" % e for e in sorted(in_waves)))
    # The Mercenaries' castle: its sweep every 300 of the timer, its spawner's counted groups (each send into a wrap
    # of its own, the counter + 1 whether it took), and the two gates' area triggers its set-up gives events.
    cs = one("kSigCastleSweep")
    castle = c.operand("kSigCastleSweep", 7)
    timer = c.operand("kSigCastleSweep", 16)
    c.expect(c.operand("kSigCastleSweep", 1) == pg and img.in_section(castle, ".data") and
             all(c.operand("kSigCastleSweep", o) == timer for o in (22, 34, 49)) and
             struct.unpack("<f", img.b(c.operand("kSigCastleSweep", 28), 4))[0] == 300.0,
             "the castle's work in [%X]: its +%X past 300 sweeps (%X)" % (castle, timer, img.call_target(cs + 53)
                                                                          if img.b(cs + 53, 1) == b"\xE8" else
                                                                          (cs + 58 + img.i32(cs + 54)) & 0xFFFFFFFF))
    task = one("kSigCastleTask")
    ce = one("kSigCastleEvents")
    c.expect(c.operand("kSigCastleTask", 17) == castle and img.u32(ce + 9) == task,
             "the castle's spawner (%X), started by its set-up (%X)" % (task, ce))
    groups, inuse, tail, sites = castle_groups(img, task)
    caps = sum(cap for cap, _ in groups.values())
    wraps = [w for _, ws in groups.values() for w in ws]
    c.expect(len(groups) == 14 and caps == 288 and len(wraps) == 46 and len(set(wraps)) == 46 and inuse and tail,
             "the castle's counted groups: %d (%d sends from %d places), the wraps' in-use test %X, the tail's %X: %s"
             % (len(groups), caps, len(wraps), inuse, tail,
                ", ".join("+%X %d x%d" % (off, cap, len(ws)) for off, (cap, ws) in sorted(groups.items()))))
    at_find = one("kSigAtFind")
    at_set = img.call_target(ce + 40)
    ids = (img.b(ce + 39, 1)[0], img.b(ce + 59, 1)[0])
    cbs = (img.u32(ce + 30), img.u32(ce + 50))
    guards = [re.search(rb"\xB9(....).{0,8}?\x85[\x88-\x8F]\x74\x01\x00\x00\x0F\x85....\x09[\x88-\x8F]\x74\x01\x00\x00",
                        img.b(cb, 0x30), re.S) for cb in cbs]  # mov ecx,flag; test [reg+174],ecx; jne; or [reg+174],ecx
    c.expect(at_set == img.call_target(ce + 60) and img.in_section(c.operand("kSigAtFind", 4), ".data") and
             at_find in [img.call_target(at_set + i) for i in range(0x20) if img.b(at_set + i, 1) == b"\xE8"] and
             all(g for g in guards) and
             [struct.unpack("<I", g.group(1))[0] for g in guards] == [0x40000000, 0x80000000],
             "the castle's gates: area triggers %02X (%X) and %02X (%X), each once (pG+174 %s); the triggers' list in "
             "[%X]+8C" % (ids[0], cbs[0], ids[1], cbs[1],
                          " / ".join("%08X" % struct.unpack("<I", g.group(1))[0] for g in guards if g),
                          c.operand("kSigAtFind", 4)))
    # The Mercenaries' water stage: its zones, one-shot areas and once-at-30, each reading the area triggers' own
    # "inside" bits; the player's ladder routine, whose sends it skips.
    wt = one("kSigWaterTask")
    water = water_stage(img, wt, c.operand("kSigAtFind", 4), player)
    if isinstance(water, str):
        c.fail("the water stage's spawner (%X): %s" % (wt, water))
    else:
        zones = water["zones"]
        most = sum(z["ends"] - 1 + 5 for z in zones) + len(water["oneshots"]) + 1
        c.expect(c.operand("kSigWaterTask", 41) == water["slot"] and img.in_section(water["slot"], ".data") and
                 [z["at"] for z in zones] == [0x18, 0x16, 0x17, 0x0C, 0x0A, 0x09, 0x08] and
                 [z["ends"] for z in zones] == [15, 15, 10, 15, 15, 15, 15] and
                 len({e for z in zones for e in z["entries"]}) == 35 and
                 water["oneshot_ats"] == [0x0F, 0x10, 0x11, 0x15, 0x12, 0x13, 0x14] and len(water["oneshots"]) == 17 and
                 water["lab_at"] == 30 and water["lab"] == {0xF5, 0xFB} and water["ladder"] == [0x10] and most == 146,
                 "the water stage's spawner (%X), its work in [%X]: zones in the order they run %s; one-shot areas %s "
                 "(%d Ganados); once at %d sends (%s); the ladder r_no 0/%s (%X); %d at most besides its start group"
                 % (wt, water["slot"], ", ".join("%02X +%X ends %d (%s)" % (z["at"], z["counter"], z["ends"], " ".join(
                     "%X" % e for e in z["entries"])) for z in zones), " ".join("%02X" % a for a in water["oneshot_ats"]),
                    len(water["oneshots"]), water["lab_at"], " ".join("%X" % e for e in sorted(water["lab"])),
                    " ".join("%X" % r for r in water["ladder"]), water["state"], most))
    # The base: its 22 area lists and two one-time sends (FUN_007db150, esi the entry, a flag of pG+174 each).
    bf = one("kSigBaseFrame")
    bl = padded_length(img, bf, 0x400)
    lists, ents, waves, esi = 0, set(), [], 0
    for p in range(bf, bf + bl - 5):
        if img.u8(p) == 0xBE and img.u32(p + 1) >= 0x100 and not img.u32(p + 1) & (img.u32(p + 1) - 1):
            esi = img.u32(p + 1)
        call = p + 8 if (img.u8(p) == 0x85 and img.b(p + 2, 4) == bytes.fromhex("7C010000") and img.u8(p + 6) == 0x74
                         and img.u8(p + 8) == 0xE8) else \
            p + 12 if (img.b(p, 6) == bytes.fromhex("F7807C010000") and img.u8(p + 10) == 0x74 and img.u8(p + 12) == 0xE8) \
            else 0
        if call:
            lists += 1
            fn = img.call_target(call)
            for q in range(fn, fn + padded_length(img, fn, 0x400) - 8):
                if img.b(q, 2) == b"\x6A\xFF" and img.u8(q + 2) in (0x6A, 0x68):
                    e, r = (img.u8(q + 3), q + 4) if img.u8(q + 2) == 0x6A else (img.u32(q + 3), q + 7)
                    r += 3 if img.b(r, 2) == b"\x8D\x4D" else 6 if img.b(r, 2) == b"\x8D\x8D" else 99
                    if e < 256 and img.u8(r) == 0xE8:
                        ents.add(e)
        if img.u8(p) == 0xBE and img.u32(p + 1) < 0x100 and img.u8(p + 5) == 0xE8:
            flag = img.u32(p - 4) if img.b(p - 10, 6) == bytes.fromhex("818874010000") else \
                esi if img.b(p - 6, 6) == bytes.fromhex("09B074010000") else 0
            waves.append((img.u32(p + 1), flag, img.call_target(p + 5)))
    c.expect(c.operand("kSigBaseFrame", 2) == pg and lists == 22 and len(ents) == 104 and 0x41 not in ents and
             [(e, fl) for e, fl, _ in waves] == [(0x89, 0x40000000), (0x8A, 0x20000000)] and waves[0][2] == waves[1][2],
             "the base's function (%X): %d area lists, %d entries (41 never: 40 is named twice); one-time sends %s (%X)"
             % (bf, lists, len(ents), ", ".join("%X flag %08X" % (e, fl) for e, fl, _ in waves),
                waves[0][2] if waves else 0))
    cd = one("kSigCastleDoor")
    c.expect(img.b(cd + 23, 2) == b"\x74\x30" and img.b(cd + 25 + 13, 5) == bytes.fromhex("68DC000000"),
             "the castle's door 1B: je at %X past DC and DD (-> NOPs)" % (cd + 23))
    c.expect(all(c.operand("kSigAreaReset", o) == pg for o in (1, 28, 45)) and
             img.b(a + 37, 6) == bytes.fromhex("89997C010000") and img.b(a + 55, 6) == bytes.fromhex("899A80010000"),
             "Kill enemies as they appear: the area flags' clear, mov [ecx+17C],ebx at %X (-> NOPs; +180's at %X stays)"
             % (a + 37, a + 55))

    print("\n%s" % ("all signatures check out" if not c.errors else "%d check(s) FAILED" % c.errors))
    return 1 if c.errors else 0


if __name__ == "__main__":
    sys.exit(main())
