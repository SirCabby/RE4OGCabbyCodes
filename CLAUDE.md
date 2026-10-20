# RE4OGCabbyCodes — project guide

A client-side mod for **Resident Evil 4 (2005)** — the *Ultimate HD Edition* on Steam, app 254700,
`Bin32/bio4.exe` build 10112090 (PE32, image base 0x400000 with DYNAMIC_BASE, not packed) —
cross-built on Linux with mingw-w64. It draws a Dear ImGui panel beside the game's **pause** (its
option screen) with God mode, Ashley takes no damage, One hit kills, Infinite ammo, Steady aim, Automatic QTEs,
Automatic shooting gallery, Endless combo, Kill enemies as they appear and Bonus time always on (the Mercenaries'
own, shown only there),
Infinite money, Save without counting, Misses don't count, Deaths don't count, Freeze play time, Freeze countdown timer, editors
for the save count, the play time, the countdown, the pesetas, the results screen's
counters (deaths, kills, shots, hits; chapter and total), the costume set and the difficulty, an
**attache case editor** (counts, loaded rounds, upgrade levels, throw away), an **Add to the case** list
(what takes room in the case, filed under tabs and headings, with a search box), the **key items**, the
**files** and the **treasures** (lists of their own, each with its own add picker and remove), and a
limitless **item box** of the mod's own (RE4 has none) for what takes room in the case, kept outside the
game in a file of its own - one box for the main game and one for Separate Ways, each for every save and new
game of it (none in Assignment Ada or the Mercenaries), changed only by the panel (see "The item box");
beside the
title's Load list and a typewriter's save list, a **save file manager**
(delete / copy a save), and one fix of the game's own, a patch with no switch: a save the game asked for and its
scheduler turned away no longer leaves its flags for the next one (Separate Ways' clear save, which showed no file
list - see "A save turned away"). Every cheat switch is written
back to the ini when it changes. `README.md` is the user-facing doc (`README_BBCode.txt` the same for
forums); this file records **what was reverse-engineered**.

It is a sibling of `../RE3-1999CabbyCodes` (the overlay, the D3D9 hooks and the Windows "adopt" path,
config, log, crash logger and `mem.h` came from there), `../RE0CabbyCodes` (steam_api.dll proxy, an
item box kept in a file beside the mod, the DirectInput guard) and `../RE3CabbyCodes` (the tolerant proxy,
no VERSION file).
re4_tweaks ([nipkownix/re4_tweaks](https://github.com/nipkownix/re4_tweaks), a dinput8.dll mod for this
same exe) documents the game's structures in its `dllmain/SDK/*.h`; its names are used here. It is not
vendored - clone it somewhere scratch when you need it.

## Build & deploy

```sh
make            # -> build/steam_api.dll   (config.mk sets GAME_DIR = .../Resident Evil 4/Bin32; gitignored)
make install    # rename stock steam_api.dll -> steam_api_orig.dll (once), deploy ours atomically
make uninstall  # restore the stock DLL
make sigs       # tools/check_sigs.py: every signature in src/sigs.h against the installed bio4.exe
make proxy      # regenerate steam_api.def + src/proxy_exports.inc from the stock DLL's exports
make package    # dist/RE4OGCabbyCodes_v<version>.zip (no VERSION file: 0.0.0)
# The mod's own discovery (src/game.cpp) over the real exe, under Wine (it relocates the image itself,
# so it also proves discovery at another base, as ASLR gives it on Windows):
i686-w64-mingw32-g++ -std=c++20 -O2 -DWIN32_LEAN_AND_MEAN -DNOMINMAX -static -Isrc \
    -Wl,--image-base=0x20000000 -Wl,--disable-dynamicbase \
    tests/test_discover.cpp src/game.cpp src/config.cpp tests/log_stub.cpp -o tests/build/test_discover.exe
wine tests/build/test_discover.exe "Z:$GAME_DIR/bio4.exe"     # "all checks pass"
# The item box and Store / Take / count / throw away against a fake attache case (each phase its own process):
i686-w64-mingw32-g++ -std=c++20 -O2 -DWIN32_LEAN_AND_MEAN -DNOMINMAX -static -Isrc \
    tests/test_itembox.cpp src/itembox.cpp src/inventory.cpp tests/log_stub.cpp -o tests/build/test_itembox.exe
wine tests/build/test_itembox.exe 'Z:\some\scratch\dir'       # "all checks pass"
# The Windows paths, under Wine (a prefix with Graphics = null keeps them off the display; headers say more):
# the panel's D3D9 hooks beside the Steam overlay's way of hooking and a compatibility shim, the real DLL
# loaded by a stand-in for bio4.exe (its window and device on a render thread of their own) -
i686-w64-mingw32-g++ -std=c++20 -O2 -Wall -DWIN32_LEAN_AND_MEAN -static tests/test_adopt.cpp -ld3d9 -ldxguid \
    -luser32 -o tests/build/test_adopt.exe       # + a folder with build/steam_api.dll and the stock DLL as _orig
RE4CC_ADOPT=1 wine tests/build/test_adopt.exe 'Z:\<dir>\' overlay adopt   # also: shim adopt, none adopt,
                                                # none class (no RE4CC_ADOPT); "overlay class" must LOOP
# - and the input guard with the game's devices as Wine's dinput8 makes them or as the overlay wraps them:
i686-w64-mingw32-g++ -std=c++20 -O2 -Wall -DWIN32_LEAN_AND_MEAN -DNOMINMAX -static -Isrc tests/test_input.cpp \
    src/input.cpp src/config.cpp src/crash.cpp tests/log_stub.cpp -ldinput8 -ldxguid -luser32 -o tests/build/test_input.exe
wine tests/build/test_input.exe plain; wine tests/build/test_input.exe overlay     # "PASSED"
# The tick's two points (the real dispatch.cpp, keyed on two calls of the test's own; one tick a frame):
i686-w64-mingw32-g++ -std=c++20 -O2 -Wall -DWIN32_LEAN_AND_MEAN -DNOMINMAX -static -Isrc tests/test_tick.cpp \
    src/dispatch.cpp src/config.cpp src/crash.cpp tests/log_stub.cpp -o tests/build/test_tick.exe
wine tests/build/test_tick.exe                                                   # "PASSED"
python3 tools/gen_items.py --exe "$GAME_DIR/bio4.exe"         # regenerate src/items.h
tools/ghidra/run.sh dec 0x7fe7e0          # decompile (several addresses per run are fine)
tools/ghidra/run.sh xref 0xC06F40         # what refers to an address, and from which function
```

**Always install after a change.** Once a change builds and its checks pass, run `make install` yourself
(check `pgrep -x bio4.exe` first; if the game is running, say so and install as soon as it closes), confirm
with `cmp build/steam_api.dll "$GAME_DIR/steam_api.dll"`, and say in the reply that it is installed. Never
leave a finished change in `build/` or hand the install back to the user (global rule). Installing is not a
release: the version stays as it is.

The VERSION file is the user's: never create or change it, never run `make version`/`make rev`, never
suggest a bump (global rule).

The log (`RE4OGCabbyCodes.log`, the run before it in `.prev.log`), the ini (`RE4OGCabbyCodes.ini`), the
ImGui layout (`RE4OGCabbyCodes.imgui.ini`: the pause panel's window `###RE4CC panel` and the save
files' `###RE4CC save files`, each with its own place, size and collapsed state - the version is in their
titles only - and which sections are folded; written 0.5 s after a change and whenever the panel goes:
ImGui's delay counts only in drawn frames, and the mod does not touch ImGui as the game exits. An older
single window's entry, named with its version, seeds both once and is dropped), the item boxes
(`RE4OGCabbyCodes.itembox.ini` the main game's, `RE4OGCabbyCodes.itembox.separate-ways.ini` Separate Ways')
and the save backups (`RE4OGCabbyCodes.save-backup\`, the ten newest)
sit beside the DLL in `Bin32`. `Trace = 1` adds diagnostics (pause/list transitions, rate changes);
`AlwaysShow = 1` draws the panel everywhere; `Watch = 0x846F9C:2,...` logs values (offsets from the exe's
base) as they change; `Disable = overlay,tick,game,input` bisects a fault. Keep them on only for a
diagnostic run and reset them after.

### Testing without playing

The user play-tests: never drive the game in their session, never send input or take screenshots there.
What can be checked without them is a **headless run** in `~/.cache/re4cc-test/` (outside the repo):

- `run_slr.sh SECONDS` runs a copy of the game the way Steam does - Steam Linux Runtime 4 + Proton
  Experimental, `SteamAppId=254700` - inside `gamescope --backend headless -W 1600 -H 900`, with its own
  prefix (`pfx/`). `game/Bin32` holds a copy of `bio4.exe`, the stock DLL as `steam_api_orig.dll` and the
  mod's `steam_api.dll` (copy `build/steam_api.dll` there first); `game/BIO4` is a symlink to the real
  install's data. Its ini has `AlwaysShow = 1` and `Trace = 1`. Output: `gs.log`, and the mod's log in
  `game/Bin32`.
- `guard.sh` (sourced) has `wait_for_ours` (refuses unless the user's own game is closed and the headless
  gamescope is ours and up) and `shot NAME` (gamescopectl screenshot of *that* display only; the PNG
  lands a moment after the command returns).
- One run proves a mechanism (proxy, discovery, tick, overlay, input hooks: all reached the title screen
  on 2026-10-13, panel drawn; 2026-10-14, `AutoQTE = 1` for that run only: 47 signatures, Automatic QTEs'
  26 sites in at the first tick, 60 fps); then hand over. **Always make sure no headless gamescope is left**
  (`pgrep -f "[g]amescope --backend headless"`), and that `~/.local/share/Steam/userdata/*/254700` was
  not created or touched by the run (the test prefix has no saves; keep it that way). Since the user's
  own first session created that folder (2026-10-13, 16:51), Steam's client rewrites its
  `remotecache.vdf` there as a headless run exits - the same entries, the save's own times and sha (seen
  20:28 that day); `remote/savegame00.sav` itself must stay untouched. The headless copy runs as app
  254700 under the logged-in Steam client, so its Steam Cloud reads and writes are the user's real
  `savegame00.sav`: never let a headless run reach a save or the save file manager.

## ⛔ Rules

- **Never patch `bio4.exe` on disk.** Every code patch is in memory, prepared from a signature with its
  bytes re-checked (`mem::Patch::prepare`), put in and taken out with its switch, and reverted on unload.
- **Never use absolute addresses in code.** The exe has DYNAMIC_BASE (ASLR on Windows): everything is
  found at run time by the signatures in `src/sigs.h`, globals read out of the matched instructions'
  operands and cross-checked between sites. The VAs in this file document build 10112090 at its
  preferred base only.
- **Game memory only from the main thread** (the tick, `dispatch.cpp`). The panel runs on the game's
  render thread: it flips atomics and posts requests; the tick applies them.
- **Edits only while the game is paused** (the Option routine with the option screen up), and save-file
  changes only while the game's list waits for the player.
- **Game calls: only the approved ones** (2026-10-13): `PutInCase`, `cItemMgr::get`, `cItemMgr::erase`
  (the attache case's own, for the case editor and the item box), and the game's
  `ISteamRemoteStorage` object's `FileWrite`/`FileRead` for `savegame00.sav` (the save file manager,
  after a backup). Ask before adding any other call. Field writes and verified byte patches need no
  approval.
- **No dependency on re4_tweaks** (the user, 2026-10-19: "we should have no dependency on re4_tweaks"):
  the mod never needs it, loads it, calls it or tests for it, and must keep working beside it - nothing in
  the code is re4_tweaks-specific. So stay off its sites: it patches the frame loop's `systemVSyncPost`
  call (0x65515E), hooks a great deal else and owns `dinput8.dll`, and its default `ReplaceFramelimiter =
  true` NOPs the frame wait (0x654A1E..0x654B0A, its DisplayTweaks.cpp) - with the frame loop's
  `SetThreadAffinityMask` call the tick went by until 2026-10-19 (see "The tick"). Its source is reading
  material for the game's names only. Coexistence has not been tried in play yet.
- Back up `Steam/userdata/<id>/254700/remote/savegame00.sav` before experiments that write saves.
- The user commits every repo themselves — do not `git commit`/`push` unless asked.

## Why steam_api.dll

`bio4.exe` imports `steam_api.dll` statically (13 functions, by name), the DLL ships in `Bin32`, and it is
not a Wine builtin - so Proton loads a native one with no launch option, on Linux and Windows alike, and
it is mapped before any of the exe's code runs. `dinput8.dll` is re4_tweaks' proxy; `d3d9.dll`,
`xinput1_3.dll` and `winmm.dll` would need `WINEDLLOVERRIDES` under Proton. The proxy
(`src/proxy.cpp`, exports generated by `tools/gen_proxy.py`) forwards all 56 functions (and the one data
export through a PE forwarder) to `steam_api_orig.dll`. It is tolerant, as RE3CabbyCodes': the stock DLL
is recognised by size 106408, link stamp 0x51E436D3 and 57 exports, and any other `steam_api_orig.dll`
that has every function the exe imports still works (the log notes it); a missing one, one that lacks a
function the exe imports, or an exe that imports one the proxy does not export, stops the game with a
message box that says what to do (README, "If it does not work").

## Threads and the tick

`bio4.exe` runs its game on the **main thread**: WinMain (0x6544B0) has the frame loop at ~0x654970..
0x6551E9 (and an outer restart loop, 0x655264 → 0x654542). The **GX render thread** (thread proc
0x955200, made at 0x9556DF; events `GXProcessRequestStartEvent`/`StartEvent`/`EndEvent`) creates the
window (class `RE4HDWnd`, a queued render command), creates the D3D9 device, pumps `PeekMessageA` (its
only call site, 0x955249) and calls `Present`. So neither the window procedure nor Present is on the game's
thread, and PeekMessageA (RE1's and RE3-1999's tick) is no tick here.

- **The tick** (`dispatch.cpp`): the frame loop calls `SetThreadAffinityMask(GetCurrentThread(), ..)`
  twice a frame; the second call, right after `fstp [pG+70]` (the frame's delta), returns to **0x654AED**
  (`kSigTick`, +12). The mod hooks the exe's `SetThreadAffinityMask` import and ticks only when the return
  address is that one and the thread is the one DllMain ran on; every other call goes straight through.
  **Its second point** (2026-10-19): the same frame's `QueryPerformanceCounter` call at 0x654B11, through
  ebx (WinMain loads it from the import at 0x654530 and keeps it), returning to **0x654B13**
  (`kSigTickLate`, +9) - still before the input layer (0x654B80) and the scheduler (0x654BDE); between the
  two only the frame wait's timing stores (0xC65A30/34) and a call to an empty function (FUN_00932740), so
  either sees the same frame. The exe's `QueryPerformanceCounter` import is hooked the same way (the game
  calls it from many places and threads; only that return address is looked at), and ticks in a frame
  the first point did not (`g_ticked_first`, main thread only): a tool that replaces the frame wait's code
  takes the first call with it - re4_tweaks' default frame limiter NOPs 0x654A1E..0x654B0A. In a vanilla
  frame the first point ticks and the second lets it be; with neither, no tick (logged). The discovery
  test NOPs that range and checks that only `kSigTick` goes and the tick moves to the second point;
  `tests/test_tick.cpp` runs the real dispatch.cpp on two calls of its own (one tick a frame either way).
- **The crash logger** hooks the exe's `SetUnhandledExceptionFilter` import, so the game's own filter
  (set at start-up) is chained under ours.
- **The panel** (`overlay.cpp`, `overlay_dx9.cpp`): the exe's `Direct3DCreate9` import →
  `IDirect3D9::CreateDevice` → the device's `Present`/`EndScene`/`Reset`/`CreateAdditionalSwapChain`
  (and swap chains' `Present`). It draws in `Present` on the GX thread, from the snapshot the tick
  publishes. Under Wine the class vtables are hooked; on Windows the device and its swap chains get
  vtables of their own (`adopt.cpp`, RE3-1999's design, so another hooker reading the class vtable never
  meets the mod's functions). The window procedure is subclassed for ImGui's input.
- **The input guard** (`input.cpp`): the exe's `DirectInput8Create` import → `CreateDevice` → the device
  vtable's `Acquire`/`GetDeviceState`/`GetDeviceData`, in each class the game's devices come in (each
  with its own functions to call on to). Wine's dinput8 and Windows' own have one class for every device;
  the Steam overlay on Windows hands the game wrappers instead - its RTTI names `IWrapDirectInput8`,
  `IWrapMouseDevice8` and `IWrapKeyboardDevice8` - so the mouse and the keyboard are two classes, and
  until 2026-10-19 only the first one seen (the mouse's) was hooked: typing into the panel reached the
  pause menu beside the overlay (`tests/test_input.cpp overlay`). The game's mouse is `DISCL_EXCLUSIVE |
  DISCL_FOREGROUND`, its keyboard non-exclusive. While the panel is up the tick unacquires the mouse (on
  the game's thread) and `Acquire` answers `DIERR_OTHERAPPHASPRIO`, the answer the game gets whenever its
  window is not in front; mouse data is blanked while the panel has the pointer, keyboard data (but Esc
  and Alt) while the panel takes text. `GetAsyncKeyState` (imported for Alt+Enter / Alt+F4 only) is
  guarded the same way. The panel draws its own pointer (`io.MouseDrawCursor`).

## Windows (audited offline 2026-10-19; never run there)

Asked 2026-10-19 (the user: "did we ensure this mod will work cleanly on windows like previous mods?"). The
mod has run only under Proton. What the earlier mods met on Windows, and where this one stands:

- **The Steam overlay's D3D9 hooks** (gameoverlayrenderer.dll detours whatever a slot points to, one saved
  original per hook - Fear3CabbyCodes 1.0.0 and RE2CabbyCodes 1.0.2 crashed on it): on Windows the game's
  IDirect3D9, device and swap chains get vtables of their own (`adopt.cpp`, from RE1/RE3-1999), and the
  game's window is known by its class, RE4HDWnd - bio4.exe makes it on its render thread, not on the
  thread that loaded the mod. `tests/test_adopt.cpp` plays the overlay as its RTTI shows it
  (`IWrapIDirect3D9` around d3d9's, no wrapper of the device: its methods hooked by slot at every
  CreateDevice, the swap chains' Present at every GetSwapChain): all 39 checks pass, and the class path
  beside it loops as it would on Windows. The RTTI is read from `GameOverlayRenderer64.dll` (Linux Steam
  ships the 64-bit one, in `~/.local/share/Steam`); the 32-bit one, which bio4.exe gets, is the same code's.
- **Windows' compatibility shim** (apphelp.dll writing its own CreateDevice into d3d9's class vtable at every
  Direct3DCreate9 - RE3-1999CabbyCodes' first Windows run saw no device): every IDirect3D9 the game makes
  is adopted; test_adopt's "shim" passes. Whether Windows shims bio4.exe at all is not known.
- **The overlay's DirectInput** is wrappers, a class for the mouse and one for the keyboard: the guard
  hooks each class (the input guard, above; `tests/test_input.cpp overlay`).
- **Exit**: DllMain does nothing on a process exit (RE0CabbyCodes hung on close: its teardown under the
  loader lock); the DLL pins itself, since adopted objects point into it.
- **ASLR**: bio4.exe is DYNAMIC_BASE (not large-address-aware as shipped; re4_tweaks offers to patch that
  on disk). Every address is found at run time, every pointer check is VirtualQuery's over the whole
  32-bit range (`mem::readable`), and the discovery test runs the exe at another base.
- **The DLL's imports**: Windows' own DLLs and the UCRT's api sets, as RE2/RE3CabbyCodes' DLLs (which
  loaded on Windows 11; Windows 7 and 8.1 need the UCRT update). Wine is told by
  `GetProcAddress(ntdll, "wine_get_version")`, never imported.
- **The files beside the DLL**: a virus scanner or the indexer holding the ini or an item box makes the
  replace fail - under Wine a file held without delete sharing gives error 5, "access denied".
  `files::replace` tries 5 times, 100 ms apart (RE3CabbyCodes' fix of 2026-10-13), the tick waiting: it
  writes while paused. `test_itembox` holds the box's file for good (the count stays) and for 250 ms (it
  goes through). A Take whose write still fails leaves the item in both, never in neither.
- **Swapped mouse buttons** (left-handed): GetAsyncKeyState reads the physical buttons and the messages
  the logical ones; the panel's stuck-button check follows `SM_SWAPBUTTON` (RE3CabbyCodes' fix).
- **The tick beside another tool's frame limiter**: its second point ("The tick").
- **Open**: never run on Windows. The overlay's DirectInput and D3D9 handling is from its RTTI and
  strings and the siblings' findings, not from reading its 32-bit code; that Windows' own dinput8 has one
  class for every device rests on RE0CabbyCodes' Windows run (the same guard design). Paths are ANSI (a
  game folder outside the system code page keeps the stock DLL from loading). bio4.exe's manifest asks for
  asInvoker only - not DPI aware, so Windows scales its window, and the panel's coordinates stay the
  window's.

## What the game does

### Globals (build 10112090)

| What | Where | Notes |
| --- | --- | --- |
| `pG` (GLOBAL_WK*) | slot 0xC06F3C → 0xC5A760 | static from the start; size 0x8690 |
| `pSys` (SYSTEM_SAVE_WORK*) | slot 0xC06F40 → 0xC65A60 | the word after pG's; size 0x9C; not used by the mod (see "Unlocks") |
| cItemMgr | 0xC70FD4 (object) | +C equipped cItem*, +13 m_char, +14 array, +18 m_pNew, +1C count |
| SubScreenWk | 0xC7ACD8 | +2AA the case size 0..3 (S 10x6, M 11x7, L 12x8, XL 15x8), from the Attache Case item held (see "Items") |
| Cockpit (HUD) | 0xC50FF8 | +98 CountDown: +0 state (bit 0 running, bit 3 held by the game), +8 float frames left (30/s); +AC its copy while the option screen is up (+0 the state, +4 the frames) |
| OptionOpenFlag | 0xC79A99 | 1 while the option screen is up (set in FUN_00743AD0, cleared in FUN_00740260) |
| cCard* | slot 0x1222304 | the load/save screen's object, made while it is up |
| files' "exists" bytes | 0x1222340 | one per file, the list draws from them |
| ISteamRemoteStorage* | slot 0x1222358 | `SteamRemoteStorage()`, cached by the card code |
| checkpoint work | slot 0xC59584 | +8 its copy of GLOBAL_WK's save region |
| the Mercenaries' work | 0xC710A8 (static) | +24 score, +28 combo, +2C/+30 its timers, +3C kills, +48 its bonus (see "The Mercenaries") |
| the enemy manager | 0xBFDB04 (cManager<cEm>) | +4 pool, +8 count, +C stride (0xE08 in .data), +14 the live list |
| cEm10 (the Ganados) | vtable 0xB1035C | every Ganado-family enemy, 0xC64 bytes (see "The Mercenaries") |
| the models' pools | slots 0xC07110 / 0xC07114 | the model records' and the parts' cManagers (0xC07150 / 0xC07134; the sub-screens' own while up) |
| the Mercenaries' village work | slot 0xC7BF9C | +0 Ganados about, +8 tries, +C frames since its sweep (room 0x400) |
| the Mercenaries' castle work | slot 0xC7BFA8 | 0x5A8 bytes: 114 cEmWrap from +0, the groups' counters +568..+59C, +5A0 a boss's timer, +5A4 the sweep's (room 0x402) |
| the Mercenaries' water stage's work | slot 0xC7BFEC | 0x30 bytes: +0 its count of Ganados about, +4 the id it counts (set as its spawner first runs), +8 its sends and those about as it began, +14..+2C its zones' counters (room 0x404, "The water stage") |
| the scene's area triggers | slot 0xC67728 | +8C their list (FUN_006B4AF0), +10 and +30 the "inside" bits (see "The castle") |

### GLOBAL_WK (pG)

`+20` Rno0, the top-level routine: 0 Init, 1 StageInit, 2 RoomInit, 3 MainLoop, 4 Doordemo, 5 Ending,
6 Option (the pause). The title runs outside it, and a return to the title (soft reset) zeroes GLOBAL_WK
from `+1C` on - so `in_game` = Rno0 2..6, `paused` = Rno0 6 with OptionOpenFlag set.
`+54` Flags_SYSTEM: 0x80000000 Assignment Ada, 0x40000000 The Mercenaries, 0x1000 Separate Ways, 0x2000 a
new game, 0x100 a loaded one, 0x20 hard mode (kept with difficulty 6). `+5C` the play time's base
(GetTickCount()/1000 when last brought up to date). `+70` the frame's delta (float).

The save region is `+4F90`, 0x36F8 bytes (what a save file and the checkpoint copy):
`+4F94` DA points, `+4F98` DA level (the dynamic difficulty, 1..10), `+4F9A` chapter, `+4F9C` save_cnt
(see "The save count"), `+4F9E` game_cnt (the clear count, "Unlocks"), `+4FA0` the retries in this
place (the continue adds one, cap 999; the chapter's start and FUN_006AF8D0 zero it; FUN_00804A3F and
FUN_009D21DE test it against 2 and 4), `+4FA4` play time (seconds, the game
caps it at 359999), `+4FA8` pesetas, `+4FAC` the room (stage << 8 | room), `+4FB4`/`+4FB6` the player's
HP/max, `+4FB8`/`+4FBA` the partner's, `+4FC8` pl_type (0 Leon, 1 Ashley, 2 Ada, 3 HUNK, 4 Krauser, 5
Wesker), `+4FC9` the player's costume, `+4FCB` the costume set (0 Normal, 1 Special 1, 2 Special 2),
`+530C` a frame counter (++ at 0x654BA8), `+8460`/`+8462` deaths chapter/total (cap 999), `+8464`/`+8468`
kills, `+846C`/`+8470` hits, `+8474`/`+8478` shots, `+847C` difficulty (1 Amateur, 3 Easy, 5 Normal, 6
Professional; 1 and below doubles magazines).

### Damage (God mode, Ashley takes no damage, One hit kills)

Every hit goes through **LifeDownSet2** (FUN_005B5AF0(cModel* who, dmg, ...)) - but El Gigante's (below). The
player: `min(hp, dmg); sub [esi+4FB4],di` - the 7-byte `sub` at **0x5B5C64** is NOPed. The partner: the same
at **0x5B5D83** (`+4FB8`). An enemy (hp at `em+324`): `cmp eax,edi; jge; mov edi,eax` at **0x5B5E98** becomes
`test edi,edi; jle` - any damage at all takes all the HP; the game's "cannot die yet" flag still leaves it its
last point. The holds (cheats.cpp) put the HP back at its maximum every tick while in a game and alive, which
also covers paths around LifeDownSet2. Ashley's armour (costume set 2) already takes no damage in the game
itself.

**The kills that go round it** (the user, 2026-10-15: "ashley (maybe leon too) can be one shot killed by
some attacks like the catapults in the castle area even with god mode on"; both switches were on). The
MainLoop routine (0x63E5E0) calls FUN_0063E560 every frame: a partner there and `pG+4FB8 < 1`, or `pG+4FB4
< 1`, starts the game over, FUN_0063E3C0 (once: `pG+501C` bit 0x100000; the stop flags; its screen 0x5A
frames on through FUN_0063DA70; 28 other callers are scripted ends). So a 0 in either HP is a death, and
about 70 sites store 0 into `+4FB4` and 10 into `+4FB8` outside LifeDownSet2; a 0 the hold never undoes.

- **The player.** PlDamageSet, FUN_005B6050(reaction, dmg, flags): `+32C`/`+32D` (re4_tweaks' cDmgInfo
  `m_Flag_4`/`m_Timer_5` at `+328`: a hit pending / its state, 0x80 dead; every attack and grab check
  wants both 0) set to 0/0x1E, dmg through LifeDownSet2, then with the HP below 1 the death routine (r_no
  2; reactions 6 and 7 play their own death), else FUN_0067ECC0 - a flinch (0-6), a knock-down (7, 8: r_no
  0x101; 9: 0x201). The generic attack hits, re4_tweaks' EmAtkHitCk (FUN_005BDE20, 163 callers, every
  enemy class) and EmAtkSetDamagePL (FUN_005B61A0, 9), take an attack row (`+8` damage, `+A` flags: 4
  "survive at 1", **8 "kill"**): flag 8 stores 0 between LifeDownSet2 and PlDamageSet. The Ganados' rows
  (0xBF3F18, 16 bytes each, picked by em10AtkCk FUN_00455BE0) have one: 0xC, 9999 with flag 8. A rock
  (cEmRock, FUN_005D5E90: the player within `+418` of it on the ground plane) and the truck (cEm3b,
  FUN_00563A80: its parts 0, 1, 4) store 0 and call PlDamageSet(8). God mode NOPs the four stores
  (`kSigKillFlag` x2 at **0x5B62BA**, **0x5BDE73**, `kSigCrushRock` **0x5D5F38**, `kSigCrushTruck`
  **0x563B18**; all four go on to PlDamageSet, which game.cpp checks): a knock-down and its 0x1E. Leon's
  explosions (FUN_005BBE90, re4_tweaks' PlBombHitCk) have no kill of their own: inside an inner radius
  PlDamageSet(9) with no damage, else LifeDownSet2(1200, "survive" above 500) and PlDamageSet(8).
- **Not covered for the player**: the grabs and scenes that kill on their own (cEm10's grab callbacks such
  as FUN_00429D80, El Gigante's, Del Lago, U-3, Krauser, Saddler, the boat's and jet-ski's scenes in
  cPl0e/cPl0f, the room scripts' lasers and falls): the player is under the attacker's control (FUN_00681130
  sets a callback, as FUN_006825F0 does for the partner) with a death motion of its own, and skipping only
  the store would leave him in it. re4_tweaks' invincibility (its DBG_NO_DEATH2) rets from ~30 attack and
  catch functions (its list: Trainer.cpp) and holds the player's `+32D` at 0x80 so that no grab starts -
  no flinch, nothing connects. The game's own debug no-death flags (`pG+68` 0x800000, `pG+6C` 0x400) are
  checked nowhere in this build.
- **The partner.** Her hit check, re4_tweaks' **cSubChar::damageCheck** (FUN_0075EDF0), runs from her move
  (FUN_00761A70) on a pending hit: `+32C` set, `+32E` the hit's weapon number, `+330` where from, `+33C`
  the distance squared. Most numbers: LifeDownSet2(9999), then reaction 2 (routine 1) with HP left, the
  death routine (2) without. **0xD, 0x12, 0x13** (FUN_00686C40's explosions: grenades, rockets, barrels,
  mines, the mounted gun's 0xD, ~30 room scripts' 0x12/0x13, the truck's hit on her armour): nearer than
  3000 (`+33C` <= 9e6, [0xB27ECC]) LifeDownSet2(9999) and reaction 7 or 9 (front or back, FUN_006D02C0)
  *whatever her HP* - her damage routine (FUN_007610A0, routine 1, its reaction at `+574`) zeroes
  `+4FB8` when that fall's motion ends (0x76154C); in her armour 7 and 9 become 8 and 10, a fall she gets
  up from. Further off, routine 0x700: FUN_00759790, she ducks and stands again. In that branch rooms
  0x200 (the castle's first, the catapults' courtyard) and 0x118 give her armour 0x5A of `+32D`, which is
  why the catapults' shots are taken to be these heavy hits - their own code was not found. Ashley takes
  no damage NOPs the `jp` to the fall (`kSigAshleyBlast`, **0x75EF89**): every heavy hit ducks her, no 9999.
- **Her other kills**, each skipped in her armour (`pG+4FCB == 2`), are skipped for her with the switch:
  El Gigante's stomp (FUN_004AF860: under its foot, 0 and FUN_006825F0 with callback 0x4AD2C0) -
  `kSigAshleyStomp`, the armour's `je` at **0x4AF8A3** -> `nop; jmp`; the truck (FUN_00563A80 parts 0, 1,
  4; FUN_00563EB0 part 1: in armour FUN_00686C40(0, her parts' +70, the same, 0x13, 0, 1.0), else 0 and
  callback 0x563250, which zeroes again) - `kSigAshleyTruck`, the `jne` to the zeroing at **0x563C19**,
  **0x564048** NOPed, so a heavy hit, which kAshleyBlast turns into ducking (`game::needs`); a rock
  (FUN_005DA3B0, in cEmRock's routine 0xB (table 0xC01724): her within `+418` + a margin of its part 0
  while `+454` is set, then callback 0x5D79A0, which zeroes) - `kSigAshleyRock`, the armour's `je` at
  **0x5DA3F5** -> `jmp`. FUN_006825F0(controller, callback) is no kill: it puts the partner in routine 4,
  run by the callback (`+834`; `+3C0` the controller) - grabs, carries, rides (25 of its 40 callers are
  cPl0e/cPl0f's). El Gigante's grab (FUN_004B4960, callback FUN_004B1EC0) kills only once its squeeze,
  LifeDownSet2(15, "survive"), has her below 2 - kHurtAshley already holds that off. Her area check
  (FUN_00757F00, the room's damage areas at 0xBFC624) is LifeDownSet2 too. **Not covered**: being carried
  off (FUN_00474850: the carrier gets away, 0 and `+32D` 0x80), and the room scripts' own (0x87C471
  stores -1 from an area check her armour skips; 0x864A00 stores 1).

**El Gigante goes round it** (the user, 2026-10-16: "it doesn't seem like the one hit kill cheat works for el
gigante enemies"). El Gigante is em 0x2B (re4_tweaks' EmNames; `em2b()` in its init's messages, FUN_004B0E30;
its code ~0x4AB000..0x4BE000; types 0-3 at `+101`). Its routines: table 0xBF5508 by `+FC` - 0 init, 1 move,
2 damage (FUN_004ABED0, table 0xBF5578 by `+FD`), 3 death (FUN_004AC110, table 0xBF5594) - each of the last
two setting `+408` bit 8 every frame. Its move (FUN_004B4370) runs its own hit check, **FUN_004B07C0** (eax =
El Gigante), which never calls LifeDownSet2 for it, on a pending record (`+32C`; the knife's `+32D` 0x11):

- Weapons 0x14, 0x19, 0x1F, 0x20 (melee, the eggs) do nothing. The damage is FUN_004B03B0: FUN_005B1A90's
  table by enemy and weapon number (El Gigante's at 0xC00668: a gun 30 - the Chicago Typewriter 25 - the knife
  60, a hand grenade 600, a flash 600, the incendiary 0, the rocket 9999) x the firepower's factor, x1.5 on
  part 5, /4+1 for types 1 and 3; it goes onto `+A38` (minus) and `+A3C` (plus twice), then the effects'
  switch by weapon (jump table 0x4B0C38; the rocket's case, 0xD, zeroes the health on any part), then the part
  hit (`+340`, its `+26`).
- On the parasite (0x3F) the health, `+324`, loses twice the damage - none for a flash (0x17, 0x2A), all of
  it for the P.R.L. 412 (0x2E); with the parasite out (`+408` 0x2000) and `+A3C` past 400 it goes back in
  (routine 0x302). **Anywhere else the health is untouched**: unless El Gigante is in its damage or death
  routine (`+408` bit 8) or holding someone (`+3D0` bit 8), a flash blinds it (0x502) and anything else adds
  the damage to `+A18`, the stagger - explosions (0xD, 0x12, 0x13, 0x2D) stagger it (0x602), and 1000 drop it
  to its knees with the parasite out (0x102, or 2). So shooting the body alone never kills it.
- Health below 1 after any hit is the death, from whatever it is doing: `+FC = 3` (the fall - with a both-
  triggers prompt, kind 3, when Leon is in its way - then the fade). The grabs' callbacks (Leon's FUN_004B1870,
  Ashley's FUN_004B1C20) let go when they see its health below 1: a rocket's kill mid-grab. The climb on its
  back is damage routine 2 (FUN_004BCF30): Leon's callback FUN_004B5B40 (its step copied from El Gigante's)
  slashes `add [gigante+324],-70` (**0x4B63D4**) and nothing checks the health until the climb ends, in the
  death below 1. The mine's lava pit (room 0x224, FUN_004B0520) zeroes it too (0x303).

One hit kills replaces the 42 bytes from the part's test (**0x4B0AC2**, `kSigHurtGigante`): a hit with
damage (`[ebp-4]` > 0) but a flash's goes to the P.R.L.'s store (+42, the health zeroed) and on as the
parasite's way does (+51), which at 0 is the death; any other goes where the game sends it (the body's check,
the jne's end, or past the parasite's subtraction, which takes nothing off those). A hit kills it as a
rocket's does, in any state (bit 8 of `+408` and `+3D0` included, where the game lets a body hit be); its
effects stay the weapon's. The slash's `add` becomes `and [gigante+324],0` (`kSigHurtGiganteKnife`).

**Krauser is kept alive** (the user, 2026-10-17: "the one hit kill cheat doesn't seem to work on krauser"; One hit
kills was on for the session). Krauser is em 0x39 (re4_tweaks' EmNames; cEm39, vtable 0xB127CC, its code
~0x5403C0..0x55CB00). His fight is room 0x31C (r31c.cpp: the ruins and the door of the three Holy Beast pieces),
from two entries of emleon07.esl: 25 (type 1, 10000 health: the first part) and 19 (type 2, 7000: mutated).
Types 0 and 1 carry his weapons (+988, +98C, +990), types 2 and 3 the mutated arm's collision parts (0x62..0x7F).
The other lists have him too: Separate Ways' (omake07) three type 3 in room 0x51C (500, 800, 1200), PS2ETC's
(omake08) a type 1 in 0x31C, and omake00 (re4_tweaks' "ADA") two type 2 in 0x40E. His move (vtable +10,
FUN_005574E0) runs routines by `+FC` (table 0xBF8590: 0 init FUN_00548720, 1 move by `+FD` through 0xBF85A0, two
functions an entry, 2 damage through 0xBF8728, 3 death through 0xBF8740) and starts with his hit check,
**FUN_00548200** (eax = Krauser). On a pending hit (`+32C`):

- Mutated, a hit on the arm (FUN_005479D0: parts 0xE, 0xF, 0x62..0x66, 0x7B..0x7F) glances off - an effect,
  sound 0x4C, no damage worked out at all.
- Else the damage, FUN_00546BA0: FUN_005B1A90's table for 0x39 (0xC00410: a gun 15, the knife 350, an explosion
  (0xD) 4000, 0x12 1000, a flash 0) x the firepower factor (0xC00B18, 7 a weapon), x2 on part 5 (and `+AA0` +=
  200), and 9999 for the P.R.L. 412 (0x2E) on types 2 and 3. It goes to LifeDownSet2 with **"survive at 1" for
  types 0 and 1** (`push 1` at 0x54840F), onto `+A86` (a short, zeroed as he shows at a jump point; past 400,
  the end of routine (1, 6), FUN_0054A660, sends him to hide), and, unless `+408` bit 8 (0x100) is up, onto
  `+AA0` and `+AA4`. The reaction (by weapon: past 200 of `+AA0` a flinch, routine 2; a head shot 0x102; an
  explosion 0x202) waits while `+408` bit 3 is up (routines 2 and 3 set it each frame).
- Below 1: the kill counted (FUN_005B2010 marks his entry dead, FUN_005B20B0 counts it) and, but in room 0x31C,
  his death routine (`+FC` 0x103). In 0x31C the room's task FUN_00815E00 waits for the mutated one's health to go,
  then puts him in his death (vtable +34: `+FC` 3) and plays the ending; nothing waits for the first part's.
  Outside 0x31C his move copies his health into his entry (pG+5418 + 0x20 x `+3A0`).

The first part ends the room's way (the event r31cs02 at AT 0x14, LAB_00816730: the Panther piece, 0x85, once its
callback sets pG+174 bit 31; then r31cs01, FUN_008163B0, his transformation, and FUN_00816270 sets up entry 19
through cEmWrap::setEm, FUN_008D7740). What Leon can do there is drive him back: his stage (`+CD0`) gives a share
of damage (`+AA4`) - 1000 for stage 3 (FUN_005473A0, vtable +24, as the fight starts) and for 0, 500 for 1, 2
and 4, none for 5 - and a flinch that ends (FUN_005538B0, FUN_00553FA0, FUN_005545D0: routine 2's 0, 1 and 2)
with the share held sends him off (`+FC` 0x2501, FUN_00552830) to hide (0x26, FUN_0055A0D0: `+408` bit 0x400 there,
`+AA4` back to 0; stage 1 becomes 2, 4 becomes 5) and back later (FUN_00556130, the room's jump points at
pG+4F30) unless his stage's share is 0. The room's tasks follow his hiding (vtable +28): the first encounter
(FUN_00812CD0) ends at his first retreat or after 2700 frames, and the room sets stages through vtable +2C (1),
+30 (4) and +50 (5, hidden for good). Killing him there would leave him standing at 0, his entry marked dead,
with nothing to take him off.

So One hit kills makes the damage's tail - the P.R.L.'s rule, 31 bytes at **0x546BF0** (`kSigHurtKrauser` finds
the hit check; the tail is looked for in the function its damage call goes to) - "any damage is at least 1000":
the first part's Krauser keeps his point (LifeDownSet2's own patch takes the rest), flinches and holds his
stage's whole share at every hit with damage, and retreats as the flinch ends - what a grenade does to him in the
game. Mutated, LifeDownSet2's patch kills him (the P.R.L.'s 9999 is not needed for that). The arm's `je` past
the glance (**0x5483AC**) becomes `jmp`: a hit there is taken as a hit anywhere else is. A flash does no damage
and is as before; the damage areas' path (no hit pending: 200 or 100 a time, FUN_005A8B40) is left alone.

### Ammo (Infinite ammo)

`cItemMgr::trigger` (FUN_007090F0, a shot): a gun's rounds (`cItem+8` >> 3) minus one are stored back with
`or edx,ecx; mov [edi+8],dx` - the 4-byte store at **0x7091E3** is NOPed. The bowgun and Krauser's bow use
up a bolt / an arrow (items 0x11 / 0x72) through a shared consume function (FUN_00709060, also used for key
items, so its call sites are patched, not the function): `push 0x72; mov ecx,ebx; call` at **0x70919A**
and `push 0x11; ...` at **0x7091B6** become `mov al,1` + NOPs (the shot goes ahead, nothing is used).
Grenades and the rocket launcher are used up elsewhere (FUN_007088B0) and are left alone. Item 0x6D (the
Infinite Launcher) and weapons whose capacity reads 0x8000 are the game's own infinite ones.

### The aim's sway and the shots' spread (Steady aim)

The sway is the weapon's **"bure"** (ブレ, shake): cObjWep `+334` the sway box, `+338` the shot's left-right
throw, `+33C`/`+340` the drift's speeds (re4_tweaks' SDK: `bureX`, `bureY`, `bureSpeedX`, `bureSpeedY`). Each
weapon's `init()` sets them from constants, some by character (cObjSniper::init FUN_00921610,
"cObjMachinegunWep39::init()" FUN_0090DF00, ~30 between 0x8FBE20 and 0x929AB0; a handgun's 0.1, 0.05, 0.005,
0.005 - boxes of 0.02 to 0.122 rad are seen). One function applies them, **FUN_00684330**, cdecl(cPlayer*,
flags, float* up_down, float* left_right). The up-down value is `0xC7A7D4` (-1..1, x pi/2 on the way in and
2/pi out; FUN_0067CE00 blends the aim-up and aim-down motions by it), the left-right one the player's `+448`
(radians off the body's facing, `+A4`). It copies the weapon's four into 0xC66C0C.. and then:

- flag 1 (the aim moved this frame): the centre (cPlWep `+28`/`+2C`) = the aim, nothing else;
- flag 2 (a shot): aim = centre + random x (the box up-down, `+338` left-right) x `[0xC66BA8]` x delta.
  0xC66BA8 is 0 until FUN_00684320 sets it to 0.2, tail-called by FUN_009212C0 (the machine guns' aiming:
  its routine table, 0xC2A780.., holds their fire, FUN_00920290) as one fires; it stays 0.2 for the run;
- flag 0 (held): every 1.0 of delta (1/30 s) two new factors from FUN_006AF040 (-1..1; the game's one
  random function, 409 call sites, its state a u16 at 0xC67458) into 0xC66BF8 / 0xC66BF4; aim += factor x
  speed x delta; both axes clamped to the centre +- the box (`+334` for both).

Callers: FUN_00688090 (the aiming's per-frame control; flag = "the input moved it"), FUN_006860C0 (aiming
at a lock-on target; 1), FUN_00687850 (the Wii's; 1), and nine fire routines (2, right after `cObjWep+352
= 3`). Steady aim makes each random factor zero where it is used: the drift's `fld [0xC66BF8]` /
`fld [0xC66BF4]` at **0x684445** / **0x68445B** become `fldz` + NOPs (`kSigAimSway`), the shot's
`fmul [0xC66BA8]` at **0x6843B5** / **0x6843D6** `fstp st(0); fldz` + NOPs (`kSigAimKick`). The random numbers
are still drawn, so whatever else the game draws them for comes out the same (the discovery test checks
the state ends where it does unpatched). Not touched: the lock-on's pull (FUN_00686490) and the impacts'
effects (FUN_00686700).

**The shots' spread** (added to Steady aim at the user's asking, 2026-10-14). Every player shot is
FUN_00686C40(player, start, end, weapon number, flags, range) - a ray (for explosives a sphere) that damages
what it meets; the weapon number is `pG+4FC0`, set from the charge table's row `+3` (FUN_0070DD30). Each
class of weapon has a fire routine that builds start and end in the hand's frame (FUN_0065B7C0(10), its
matrix at `+C`), and the random parts are all `call random; fmul scale`:

| Fire routine | Whose | Random part | Sites (the fmul) |
| --- | --- | --- | --- |
| FUN_008FD410 | the handguns' and magnums' class (cObjMagnum's among it) | direction (-50000, random x 200, random x 200): +-0.23 degrees | 0x8FD553, 0x8FD561 |
| FUN_009189B0 | a class whose set-up (FUN_00919810) special-cases the Red9 (3) and the Matilda (0x11) | the same | 0x918B23, 0x918B31 |
| FUN_00920290 | the machine guns' (the first shot, then held fire, into a static vector at 0xC7D518) | the same | 0x920322, 0x920333, 0x92050D, 0x92051E |
| FUN_00925040 | the shotguns' (13 pellets; the Striker's, weapon 8, 19) | the centre pellet's direction + random x 100: +-0.11 degrees | 0x92531A, 0x92532B |
| FUN_0072BDC0 | the mounted machine gun (SetObj43: its control shows prompt 0x41 and reads Key 0x80) | barrel (random x 1000, random x 1000, 50000): +-1.15 degrees | 0x72BE2C, 0x72BE3A |
| FUN_00739E40 | a thrown grenade or egg | sideways speed random x 15 (forward 283.5 a grenade, 500 an egg): +-3 / +-1.7 degrees | 0x739F45 |

Each fmul becomes `fstp st(0); fldz` + NOPs (`kSigSpreadGun` x2, `kSigSpreadAuto` x2, `kSigSpreadShotgun`,
`kSigSpreadMounted`, `kSigSpreadThrow`), the random numbers still drawn. The shotgun's other pellets are its
cone, left alone (the user's choice): pellet i's direction gets ring x 1500 x (cos a, sin a) on 50000, ring
= (i-1)/3 + 1 (1.7 degrees a ring, to ~7, the Striker's ~10), a = a + random 0..1 + pi from a random start,
each starting on a hexagon of radius ((i-1)/6 + 1) x 150 round the muzzle. Taking only the randomness out
would put every pellet on one line (a + pi each time). Every pellet but the first passes FUN_00686C40 flag 4,
whose effect in FUN_005BB3C0 was not followed - so whether one blast's pellets stack their damage on one
enemy is not known.

No spread elsewhere: the rifles' shot (FUN_00922840, weapons 9 and 10) is the scope's own eye-to-target line
(FUN_006F4520), and no random number is drawn launching the bows, the bowgun or the rocket and mine
launchers. Not aim: the weapon code's other random calls eject shell casings (x 15, FUN_008FC180 and ~20
like it, spawning through FUN_0066E860); hit effects (FUN_005B36D0), breakables' debris (FUN_006FFCE0) and
FUN_008F78C0's rocking use the same scales elsewhere.

**The hold** (the user, 2026-10-14: the aimed position "changes when shooting" and "slightly moves around a
little still with the character model's idle aiming state"; 2026-10-15, with the stance in: "the first few
shots of the tmp move the aim position a bit"). The laser and the shots do not take their line from the aim's
angles but from animated parts: the laser (FUN_0073DC60, thiscall(weapon, start*, end*)) from the weapon
model's part 0 (1 for the mine thrower and the P.R.L. 412) - start = part x the weapon's muzzle (a table at
0xC79550, 12 bytes per weapon number), end = start + the part's turn x (-50000, 0, 0); rifles and the scoped
mine thrower take the scope's own eye-to-target (FUN_006F4520) - and every shot from the player's part 10, the
hand. So the idle aim's breathing moves the dot and a shot's recoil throws it, whatever the angles do. The
laser is drawn by FUN_0073EA60 (thiscall(weapon, draw, keep, ..): unless `keep`, the line again from
FUN_0073DC60 and a ray test, its end into `cObjWep+35C`): each frame of an aim by FUN_0067DE90 (every laser
weapon's aim step calls it), and through a machine gun's burst by the fire routine - at each shot
(FUN_00920290), the stored line redrawn between shots (FUN_00920770). A machine gun's fire motion
(FUN_0067E190 with the motion set's `+70`, attribute 4, no blend-in) starts over at every shot (FUN_00920770
fires again once its frame reaches 3), so with the line live the dot jumps to the recoil's first frame at the
first shot and stays there for the burst. A part is FUN_0065B7C0 (thiscall(model, n), ret 4): `model+F4` the
first part, then an array of 0x1D8-byte parts (`model+4` has 0x2000) or a list through each part's `+F4`; its
world matrix (3x4 floats, rows: x' = m0 x + m1 y + m2 z + m3) at `+C`. Steady aim keeps a held copy of the
gun's and the hand's matrices (game.cpp's HeldPart) and, while the aim is still, turns the six lookups into
`mov eax, held copy` + NOPs: the laser's `push idx; call` at **0x73E1A9** (`kSigHoldLaser`) and each shot's
`push 10; mov ecx,esi; call` at **0x8FD511**, **0x918AE1** (`kSigHoldHand`), **0x9202D5**, **0x9204C0**
(`kSigHoldAuto`) and **0x9251A0** (the shotgun's pellet loop, `kSigHoldShotgun`) - the lookup cleans its own
push, and nothing after reads ecx before setting it.

The tick decides (aimhold.h) from the player (`[0xC57054]`, `kSigPlayer`): its routine bytes `+FC..+FF`
(r_no_0..3; r_no_0 0 dispatches by r_no_1 through 0xC1BCD0 (FUN_007632E0), whose entry 6, FUN_00763FF0, calls
the weapon's routine set - the function the weapon put at 0xC7A7CC, 18 of them - which dispatches by r_no_2:
in every laser weapon's set step 1 is the aim and calls FUN_0067DE90 each frame, step 2 the fire; the aim goes
there itself for a shot, `mov [esi+FC],20600`, and the fire routine fires the next frame - the four sets whose
shots read the hand have it as step 2's first sub-step), its `+7D8` cPlWep (`+28`/`+2C` the aim's centre, `+34`
the weapon object), `+448`, `+A4` and `+94`. While the player aims (r_no 0, 6, 1), a pose whose gun and hand
have not moved (1e-5 in the matrices' turn, 0.1 in their place) for 4 frames is held - and held again
whenever it comes to rest somewhere else, so the hold is the gun at rest. The tick that first sees the fire
step (0, 6, 2) sees the aim's last frame, before any shot; with no hold in yet it holds that pose if the gun
turned no more than 0.25 degrees since the frame before, else the aim's last pose that did (none: the burst's
shots are the game's). A hold stays through the fire step and the gun's return to rest; any move of the
centre, `+448`, the body's turn or place or the weapon, or the end of the aim, lets it go. The tick reads
poses before the game's update, so a hold put in at a tick serves that whole frame. Until 2026-10-15 the tick
waited for `cObjWep+354` bit 1, and the hold never went in (see "Dead ends").

**The stance** (the user, 2026-10-15: "remove the idle animation of leon moving around slightly so that the
gun aim remains precise to where it will be when shooting"). The breathing is the aim's own animation. A
weapon's routines are the player's `+FD` 6, `+FE` the step (0 ready, 1 aim, 2 fire, ...) and `+FF` the
sub-step, a table of functions per step and per routine set (13 sets aim this way). Step 1 starts with a
set-up (sub-step 0) that hands the aim's blend helper - a static at 0xC7A7E8: `+0` the model, `+4` the
blend, `+8`/`+C`/`+10` the motions, `+14` the mode, `+18` its motion B (a MOTION_INFO, 0xD0 bytes) - to
FUN_0067E190, thiscall(helper, model, motion, aim-up, aim-down, sub-sequence, frames to blend in, mode,
attribute, first frame): the motion goes on the player (`+1D8`, re4_tweaks' MOTION_INFO, through FUN_006611A0)
and the aim-up motion into B, with attribute 4 of its own, and `+2A8` = B. FUN_0067CE00 sets B's weight
(`+C8`) to |0xC7A7D4| each frame and swaps B to the aim-down motion (FUN_0067CD80, at the loop's frame) as
the sign changes. Sub-step 1 only moves the motion (FUN_00662B90: both evaluated and blended, each on a clock
of its own, FUN_0065FD80, cdecl(MOTION_INFO*): `+B8` the frame += the delta x `+C0` the speed, wrapping a
loop (`+40` bit 4) at `+BC` frames, `+AC` the frame again in 10.6 fixed point; B's speed is copied from the
first motion's). The set-up's motion is the idle aim - a loop from frame 0, blended in over 3 frames - and its
keyframes breathe. Bit 8 of the attribute stops a clock: FUN_0065FD80 tests it first, and FUN_0065EED0 (the
game's own "motion stop", used on other models only) sets it; the blend-in (FUN_0065F2E0, counted down by
FUN_00662B90) does not use the clock. Steady aim makes each set-up's `push 4` (the attribute) `push 0xC`:
eleven set-ups (`kSigAimLoopFar` x3, `kSigAimLoopNear` x6, `kSigAimLoopShotgun`, `kSigAimLoopAuto`), all
agreeing on the helper, FUN_0067E190, the blend, FUN_0067CE00 and the move - the handguns' and magnums'
(**0x8FD3A8**), Wesker's (**0x8FEF88**), Krauser's bow's (**0x9128C8**), the P.R.L. 412's (**0x905C67**),
Ada's bowgun's (**0x906FB8**), a grenade's (**0x915885**), the Red9 and Matilda's (**0x918935**), the mine
thrower's (**0x91C1BE**), the rocket launcher's (**0x91D8D5**), the shotguns' (**0x924FD8**) and the machine
guns' (**0x92021C**; blended in over no frames while the player's `+428` is set). Each set is named by the
weapon objects its static initializers pair it with (cObjLaser the P.R.L.'s) or, for the rocket launcher,
by its fire step dropping the weapon unless pG+4FC1 is 2. The knife's ready stance (FUN_00753AC0, pG+50's
motions) has the same set-up without the move after it, and is left alone; a rifle's aim is one motion with
the scope's own view (FUN_00922CB0), not touched. B's clock does not stop (FUN_0067E190 and FUN_0067CD80 give
it attribute 4), so the tick (`game::keep_stance`) copies the loop's clock (`+AC`..`+BB`) into B's while the
player's motion is a stopped loop blended with the helper's B: both are drawn at frame 0. When the switch
goes off in a game, `let_stance_go` lets a held loop run on (the teardown, off the game's thread, leaves it to
the game's next motion). A shot is the game's as before - its fire motion and the rest of it (sub-step 4)
recoil, the hold keeps the line - and the next set-up holds the same pose again.

re4_tweaks writes nothing in FUN_00684330 or at any spread, hold or stance site, nor in FUN_0067E190,
FUN_0067CD80, FUN_0065FD80 or FUN_00662B90 (every pattern in its source run against the exe, 2026-10-14 and
-15); its MouseTurning sets a motion's attribute to 4 in the standing turn (pl_R1_Turn) only. It
hooks FUN_00688090 around the call (0x6881BE, 0x6882A1, 0x688318, 0x688461, 0x6884C9, 0x6886B1, 0x68873E);
its DetachCameraFromAim notes that the Modern mouse setting lost the left-right sway (the camera follows the
aim there).

### The play time (Freeze play time, the editor)

FUN_00656DB0: `pt += now - [pG+5C]; [pG+5C] = now` (now = FUN_00973E50, seconds), capped at 359999,
called now and then (as the option screen opens, at a save unless `pG+4 & 0x60`). The add at
**0x656DC5** is NOPed: the base still moves, so nothing is added while frozen and nothing piles up for
later. The editor writes `+4FA4` and moves the base to now, while paused (the option screen brought the
total up to date as it opened and moves the base past the pause as it closes).

### The save count (Save without counting, the editor)

The save builder (FUN_0095E0E0) does `mov esi,1; add [pG+4F9C],si` at **0x95E142** (`kSigSaveCount`,
+10), caps the count at 999 (`mov ecx,3E7`, +23), then takes a checkpoint (0x95E1B6) and copies the save
region into the file - so a file holds the count *including* its own save. Nothing else touches
`pG+4F9C`; what shows it is the load and save lists: their row drawer (FUN_0095CFF0) reads each file's
copy, `S+0x40`, as three digits beside the chapter (`S+0x3E`) and the play time (`S+0x48`) - and
`S+0x42` (game_cnt) + 1 as two. Save without counting NOPs the 7-byte add. The editor writes `pG+4F9C`
and the checkpoint copy's (`+C` in it): a continue restores the save region from that copy, and the game's
own count is always there too (a save takes a checkpoint), so an edit would otherwise be undone by a death.

### The merchant (Infinite money)

A purchase is FUN_00714700 (its one caller, FUN_00790840, then gives the item through `cItemMgr::get`):
price = FUN_007143D0(id, num); `if (*money < price)` refuse; `*money -= price` - `sub [eax],ebx` at
**0x714753**, money being `&pG->+4FA8`. A tune-up is FUN_00797680: the new levels into the weapon word, the
magazine refreshed, then `sub [pG+4FA8],ecx` at **0x79781E**. Infinite money NOPs both; the "enough
pesetas?" check stays. A sale is FUN_00715AB0 (`*money += price`). Buying item 0x40 (the second Punisher)
sets `pG+528C` bit 0x10000000. Nothing else takes pesetas: the other writes are pickups
(`cItemMgr::get`), new-game amounts, the 99,999,999 cap (0x7750AC) and the character swap's exchange with
`+8454` (FUN_00682350; FUN_0087B910 and FUN_009B9510 fold it back in). FUN_00706140 (an add helper) has no
callers.

### The results' counts (Misses don't count, Deaths don't count)

A shot is counted in exactly two places (0x68706D, 0x905278; the four adds occur nowhere else), each: if
it hit, hits +1 (`+846C`/`+8470`), then shots +1 (`+8474`/`+8478`), the "no hit" branch being a short
jump past the hits only (`je +16` at **0x687076**, `jle +16` at **0x90527D**). Misses don't count makes
it `+2C`, past the shots too: a miss counts nothing, a hit counts both. A death is counted by the continue
(FUN_0063D890) after it restores the checkpoint: `inc esi/edi/ebx` then stores - +4FA0, +8460 (the
chapter, `inc edi` at **0x63D911**) and +8462 (the game, `inc ebx` at **0x63D91F**), each capped at 999.
Deaths don't count NOPs the two incs and leaves +4FA0 (the game's own retry logic) alone. The chapter's
start (0x6C3075, 0x6C3CB1) zeroes the chapter counts.

### The countdown (Freeze countdown timer, the editor)

`CountDown::move`: `fld [esi+8]; fsub [ecx+70]; fstp [esi+8]` (frames -= the frame's delta). The `fsub`
at **0x5A34D4** is NOPed. One CountDown (Cockpit+98) serves the Mercenaries' clock and the main game's
timed scenes; the HUD's digits (`+4` minutes, `+5` seconds, `+6` hundredths) are worked out from `+8` each
frame.

**The pause keeps it aside** (the user, 2026-10-18: "using the countdown timer to change the countdown amount
doesn't work"; their log had "countdown set to 0:00:08" in the Mercenaries, and the clock came back as it
was). The Option routine (FUN_0063A8F0, Rno0 6) opens the option screen with FUN_00743AD0, which copies the
CountDown into the Cockpit's `+AC` (re4_tweaks' COUNT_DOWN_SAVE: `+0` the state, `+4` the frames, `+8` the
clock shown, `+9`/`+A` its display unit's; FUN_005A3CF0, thiscall(CountDown, copy), which also hides a shown
clock: FUN_005A3920(0), the state's 0x10) just before it puts OptionOpenFlag up (0x743C80, `kSigClockKeep`).
Its close, FUN_00740260, puts the flag down and then copies the CountDown back from that copy (FUN_005A3D80,
0x7402EA, `kSigClockBack`), over whatever the CountDown held by then. Until 2026-10-18 the editor wrote `+8`
alone, and every edit was undone as the pause closed. Now `game::set_countdown` (only with the option screen
up, both saying running) writes the copy's `+4` and the CountDown's `+8` (what the panel reads meanwhile);
without both signatures the editor is not shown. The same copy is kept and put back by the sub-screen
(0x76804A / 0x76984E), put back by the title's options' close (FUN_009E66B0, 0x9E67A0), and by a Cockpit pair
nothing calls (0x73F8B0 / 0x73F8D0); check_sigs lists every call. Run offline: the discovery test runs the
game's own two functions on the image's Cockpit around an edit, with the clock shown and not - written to the
CountDown alone it comes back at what it was, through set_countdown it stays.

### QTEs (Automatic QTEs)

The keys are `Key.on` / `Key.trg` (u64 at 0xC62F08 / 0xC62F10; re4_tweaks' `KEY_BTN` bits: A 0x80000, B
0x40000, OK 0x80000000, directions 0xF000000) and the triggers' bytes 0xC62EFA (left) / 0xC62EFB (right),
all rebuilt each frame by the input layer (FUN_00967360, called at 0x654B80 - *after* the mod's tick, so a key
written from the tick is overwritten: no tick-side input is possible). KeyTrgCheck is FUN_00965ce0 (re4_tweaks'
name; -1/0, four pushes) and FUN_00589f70 (bool), KeyOnCheck FUN_004289c0.

**The prompts** are cActionButton's (static, 0xC502C0). `set` (FUN_0058aa10, thiscall) registers a work for this
frame (0x18 bytes: `+4` callback, `+8` its arg, `+C` message, `+D` priority, `+E` mode, `+F` **kind**, `+14`
flags); most callers use FUN_0058ad40, which takes a second kind when `0x12224B0 == 1` - a dword nothing writes
(the Wii's controls): always the first here, and set's own remaps for it (3 → 0x12, 0xD → 2) never happen.
`move` (FUN_0058ab40) runs in the MainLoop routine right after it lowers the **QTE flag** (0xC63A58, at
0x63E636): it takes the first work in reach, draws its prompt (FUN_0058a100) and asks `checkButton`
(FUN_0058a300) - "pressed" runs the callback (mode 0 directly, 1 as a task). checkButton dispatches on the kind
through the jump table at **0x58A7A4**: kinds **2** (one button), **3** (both triggers), **4** (two buttons),
**9 / 10** (one trigger) and **13** (the Wii's; always "not pressed" here) raise the QTE flag - the QTE kinds;
1 is the action button. The flag steers only the input layer's QTE-key mapping (0x96824C..) and the prompt
icons' layout (0x645D83..). Automatic QTEs: kind 13's `xor al,al` (**0x58A46E**) becomes `mov al,1`, and the
table's entries for 2-4 (0x58A7A8) and 9-10 (0x58A7C4) point at kind 13's code (0x58A465): every QTE prompt
still raises the flag and is answered "pressed" the first time it is checked - re4_tweaks' DisableQTE answers
the same (its hook on checkButton's thunk returns true when the flag is up). The table patches lean on kind
13's (`game::needs`): `toggle()` puts them in after it and takes it out after them, as a table entry sent to an
answer that is never "pressed" would make those QTEs impossible. Run offline: `tests/test_discover.cpp` calls
the real checkButton on every kind, before, with and after the patches.

**Prompts without a callback** - the prompt is shown, the scene polls its buttons itself - are patched where
they poll; after a KeyTrgCheck, `test eax,eax` → `or al,1` ("pressed" in eax too):

| Scene | Poll (call) | Notes |
| --- | --- | --- |
| The boulder run | 0x5D9ACC (A) | each press adds the time since the last to the speed |
| Del Lago: swim back to the boat; the rope | 0x8F2BC1; 0x621C0F | |
| El Gigante's parasite (the knife) | 0x4B5FC7, 0x4B6090, 0x4BD16A | the button from the table 0xBF56E8 |
| The minecart's climb | 0x73BB15 (OK) | 10 presses' worth within 90 frames |
| The statue chase: running (below); climbing | 0x858C6F; 0x85B09C, 0x85B22B | climbing: fewer than 11 in 90 frames and Leon falls |
| The statue chase's last jump | FUN_00859810 at 0x85AE4A | both triggers |
| Krauser's knife fight, the last presses | 0x804864 | |
| The laser room's last dodge | FUN_008064b0 at 0x806CE0 | both triggers |
| Saddler's bridges: the climb after the jump | 0x7F7C6E, 0x7F7E5B | in FUN_007f79a0, the player routine the jump sets |
| em3a's grab (re4_tweaks: "Robot") | 0x55CD3B | presses left at +418 (10 by default); unidentified in play |

**The statue's run** (the user, 2026-10-17: "the cheat for auto QTE just failed for chapter 4-4 when running away
from the statue"). Both runs - FUN_0085a760 (the first, the swings to jump) and FUN_0085ac80 (the second, the
floor falling behind, then the last jump and the climb) - take a step of FUN_00858b20(counter, current, speed,
since, 10, 5, 3, 5, motion set, five run motions) on the work at `[0xC7C718]`: `+134` the counter (0..49), `+13C`
the speed (a tenth of it: which run motion), `+140` the frames since the last press. A press adds `ftol(+140)`
(the CRT's `_ftol2`, `cvttsd2si`: a truncation) and zeroes it; with none for over 5 frames (+4 below DA level 4,
+1 to 6, -1 from 10) the counter loses 3 a step. So at 60 fps (a delta of 0.5) a press every frame adds nothing:
the forced press kept Leon at the slowest run (at 30 fps it was +1 a frame). Each swing's script (FUN_00859460,
60 frames, started by FUN_0085a070 once Leon is within reach of its object) opens the jump (`pG+174` bit
0x20000000) only once Leon is past a point before it, and sets `pG+178` bit 0x80000000 unless he jumps within 90%
of its frames (80% with the jump open and a line closing behind him); in the second run, bit 0x40000000 is the
floor gone from under him. A frame handler (0x85B710) turns either into a death God mode does not stop: the game
over (FUN_0063E3C0) and FUN_00859F20 (knocked off) or FUN_00858DE0 (the fall), each zeroing `pG+4FB4`.
`kQteStatueSpeed` NOPs the `jle` past the counter's top (**0x858C94**): a press puts it at 49, the top speed.
re4_tweaks' FixQTE (on by default) replaces the 10 bytes before it (0x858C7B: `mov edi,[ebp+14]; fld [edi]; call
ftol`) with the frames divided by the delta, for its own automatic mashing too; `kSigQteStatueRun` wildcards
them. Every other poll counts presses or adds the time since the last in floats (the boulder, the swim), so a
press every frame is its most at any frame rate; before adding a poll, check what a press is worth at a delta of
0.5. Run offline: the discovery test steps the game's own run at 30 and 60 fps with the patches out, in and out
again.

Triggers read directly: the statue chase's jumps (left 0x85AA28, right 0x85AA0C, both 0x85A9F0) all end at one
`je` back to "not yet" (**0x85AA2F**, NOPed); the laser room's two dodges (0x807975.. and 0x807BE6..): the first
`je` becomes `jmp +12`, to the dodge, where the Wii's own branch goes; Saddler's bridges (FUN_007f8e20, a task:
a walkway's timer passes its mark with Leon on it, a kind-3 prompt): `je` 0x7F8FD4 NOPed and `jne` 0x7F8FDD
made `nop; jmp` to the jump (0x7F9118, which sets FUN_007f79a0 - `check_sigs.py` checks that).

**Grabs**: FUN_00680d80, called each frame of a grab by 18 enemies' code, shows the shake-off prompt (message
0x2B, kind 11 - shown, never answered) and adds the buttons pressed to the player's `+568` (a direction +1,
OK/Cancel +1, LB/RB/L/R +1, +2 on a keyboard); FUN_00680fa0 reads it (×1.5 below DA level 3) and each grab lets
go past its own threshold (10 to 40, one 90). Its first `je; inc [edi+568]` (**0x680DBB**) becomes
`add [edi+568],16; nop`: a grab is shaken off in a few frames.

**Not QTEs, left alone**: cranks (message 0x2A, kind 2 / the Wii's 0x13: FUN_0088d990 and three more; they
turn with time, presses only speed them up), the display-only prompts (kinds 12, 15, 16 with their own polls),
kind-1 prompts with their own polls (r117's message 0x3E, ...). The QTE keys' bindings and icons (0x9682BA,
0x96831F, 0x645xxx) are re4_tweaks'.

**re4_tweaks** hooks every one of these scenes, unconditionally (QTEFixes.cpp; DisableQTE, AutomaticMashingQTE
and FixQTE - on by default - are checked at run time): checkButton's thunk (0x401F96), the prompt's display
check (0x58ABCC), each KeyTrgCheck call's rel32 (not 0x7F7C6E/0x7F7E5B/0x55CD3B), the calls to FUN_00859810 and
FUN_008064b0, the trigger compares (MakeInline, 7 bytes each), the bridges' timer store (0x7F910C), and
FixQTE's speed maths right after several polls (0x5D9AD8, 0x73BB30, 0x858C7B, 0x85B0AD, 0x85B23C, 0x80486E and
the `inc` at 0x804875). Ours are the bytes in between - never one it writes - and `or al,1` rather than a NOP
of the jump because its Krauser stub reads eax for "pressed" and does the increment itself. Its patterns span
some of our bytes, but it scans in its DllMain, before our tick puts anything in; our signatures wildcard all
it writes (checked with its writes simulated, 2026-10-14). With its DisableQTE on as well, both answer the
same.

### The shooting gallery (Automatic shooting gallery)

The merchant's shooting gallery is `r22c.cpp`, room **0x22C** (the room check in FUN_005BB3C0 names it). Its
work is a pointer at **0xC7C524** (set by the room's set-up, FUN_00845574; `kSigGalleryScore`,
`kSigGalleryStreak`): `+0` state (1 starting, 2 running, 3 results, 4 leaving), `+4` the script's clock
(float frames), `+C` hits (the results show up to 25), `+10` **score**, `+14` targets shown, `+18` the
reaction frames, `+1C` the game (1-4, A-D), `+20` its script (1, or 2 once three of that game's five caps
are held: FUN_00842580 at the menu, 0x842EA5), `+24` **the streak**, `+28` a script pause (event FE), `+2C` a
hold that keeps the script running (bonus, event FD), `+40` flags, `+46`/`+47`/`+48` bonus kills / body
shots / Ashley (type 2) hits, `+4C` the script, `+68` the props' cool-down, `+78` the caps won.
The menu (FUN_00842DF0) offers games by the room you came from (pG+4FB0), and the room's set-up puts the same
in `+1C` first: 0x211 (the castle, A-B), 0x220 (the castle, A-C), 0x305/0x31D (the island, A-D), else (the
village) A.

- **The scripts**: eight tables (A1 0xC20EA8, A2 0xC21448, B1 0xC21CF0, B2 0xC222F8, C1 0xC22C28, C2
  0xC234B8, D1 0xC240B0, D2 0xC24C78, picked at 0x84306F..): count, end clock, then entries `{clock, type,
  x, y, z, commands}`; types FC (drop the type-4 targets), FD (+2C = 30), FE (+28 = 120). A target's
  commands (FUN_00589110 steps `+4D8` by them; `+FC` the routine from the table 0xBF9894): 0 rise
  (FUN_00589400: one frame, then `+4DC` counts the delta to 10), 1 go down (FUN_00588E00: health 0 at once,
  gone some 20 frames on, counted in frames), 2 wait N (FUN_005894F0: `+4DC` = N, N + 1 - the delta above 1,
  then down by the delta to 0), 3 move to x,y,z at a speed (FUN_005895B0: speed x the delta a frame), 4
  stay. `+4FC` the frames stood up. Every script has **25 scoring targets**.
- **The loop** (FUN_00844A10, each frame in state 2): with `+2C` out and `+28` running it only counts `+28`
  down (not while a bonus is under way); else it spawns what is due at the clock, ends the game (state 3)
  once the clock passes the end with no bonus under way (with one, `+2C` = 1 and it waits), clears the
  streak on a miss (pG+5024 bit 01000000, set by FUN_00686C40 for a shot that hit nothing), and at a streak
  of five sets it to 0 and - unless pG+174 bit 40000000 is up - starts the bonus task (+2C = 3). Then the
  clock + the delta, and **while no target of type 0, 1 or 3 stands** (FUN_00843160) and both timers are out
  it goes round again: the script fast-forwards over empty stretches. Type 5 does not hold it. The clock and
  the work's timers (`+28`, `+2C`, `+68`: each set to N + 1 - the delta) go by the frame's delta (pG+70: 1 at
  30 fps, 0.5 at 60).
- **The targets** are `cEmMark` (em 0x3E, vtable 0xB12EE8; move FUN_00589EF0) on the enemy manager's list
  (cManager<cEm> at 0xBFDB04: `+4` the pool's base, `+8` count, `+C` stride, `+14` the live list, `+8` of
  each the next; `kSigEmPool`, `kSigGalleryTargets`). Spawned by FUN_00589290 -> FUN_00588320 (type at
  `+101`, health `+324` 1 for types below 7, else 1000). Collision parts: `+344` **the head** (a sphere 1800
  up), `+408` chest, `+43C` waist, `+470` legs; type 5 has `+4A4` beside it (a bomb, x -650).
- **A hit** is the damage record at `+328` as a shot writes it (FUN_005AEEF0 from FUN_00686C40): `+328` 0,
  `+32C` 1 (pending), `+32D` 10 (frames), `+32E` the weapon number (pG+4FC0; above 0x30 the game makes it 2),
  `+330` where from, `+33C` the distance, `+340` the part. The enemy manager runs FUN_005AEFA0 before each
  move (the delta added up at `+328`, `+32D` -1 for each whole frame of it, `+32C` cleared at 0), so the
  record stands 9 hit checks at 30 fps, 19 at 60. The hit check
  (FUN_00589C50, `kSigGalleryHit`): with health and a record, a prop (type 10 on) runs its effect
  (FUN_00589A40) and scores (type, 0); a target loses a point and scores (type, part == `+344`, or 5 for a
  weapon 12/13 - an explosion - or 2 for type 5's bomb part, each counted as the head) and goes down. Type 5's
  bomb part plays FUN_00686C40(0, pos, pos, 0x13, ...): an explosion that hits everything round it.
- **The scoring** (FUN_008431C0, cdecl(type, part, pos, kill, frames)): only in state 2; a type above 9 zeroes
  the streak; else points from the table **0xB2BFA8** (5 ints a type: body, head, two non-kill hits, 0) -
  type 0 50/100, 1 50/200, 2 and 4 -1000 (Ashley), 3 500 (the bonus target), 5 50/100 - onto `+10`; a kill
  that scores adds a hit (not type 3); points above 0 add to the streak, else it is zeroed. A popup of the
  points (FUN_00841AB0) takes the first of eight timer slots at `+DC` (all busy: none drawn).
- **The bonus task** (FUN_008443B0): pG+174 |= 40000000, 30 frames, spawns a type 3 from 0xC25B8C (rise,
  move 4400 at 30, down), waits while it stands, 15 frames, clears the bit. Every frame of it also asks the
  props out (pG+174 bit 10000000, consumed each frame by the props' task FUN_00844610, which slides the
  props 100 a frame to +-5000 and back; the loop also asks every frame in B2, C2, D1 and D2). Both tasks
  count frames (FUN_006C4E30(1) waits one), not the delta: at 60 fps they run twice as fast as the targets'
  commands and the clock.
- **The props**: two cEmMark of types 12/13 (made by `+1C` as the room's set-up left it: 1 or 2, where the
  menu offers A or A-B) or 14/15 (3 or 4: A-C, A-D). Their effects (R22cHitEffect FUN_00845240, only in state 2 with `+68` out): 14's legs 6 (+40
  bit 0, the lights), then 14's or 15's chest 5 - with bit 0 and not bit 1 - FUN_00841F70: **+300 to the
  score**, once a game (0x841FA5; the only other add to `+10` is the scoring's); 14/15's waist 4 is
  FUN_00844E40, an explosion through FUN_00686C40 that hits the targets round it (Ashley too) and, hitting
  none, is a miss; 12's and 13's do nothing to the score.
- **The most a game gives**: 25 heads (3000; D1, with six type-1 targets, 3100) + six bonus targets (3000:
  a bonus needs five scoring hits since the last, its own hit counts, so 25 targets give 6 and need all 25)
  + the props' 300 where the gallery offers C or D: **6300** (D1 6400); **6000** where it offers A or A-B.
  The special cap (0xF0..0xF3) wants 3500 points or 25 hits with no type-2 hit (FUN_00843AA0).

Automatic shooting gallery (`gallery.h` decides, `game::gallery()` reads, `game::gallery_hit()` writes the
record: field writes only, no patch, no game call) plays for that: heads only, never types 2 and 4, type 5's
head (not its bomb), one hit at a time (each taken before the next, kGap frames apart), from half way up a
rise. A streak of five with no bonus under way starts one; under way, hits stop at four, and at three
until its target is down. Each target's frames left come from its commands (`kSigGalleryStep`,
`kSigGalleryWait`), and a target with kUrgent frames left is hit whatever the streak. Type 5 first; the last
target holding the clock is kept up while a type 5 is, and in a bonus's window until its target shows (else
the fast-forward pulls the next waves into the window; once shown, the bonus target keeps the clock running).
Not at a streak of four, before the hit that starts a window (see "Dead ends"). The props' 300 is taken in
the first window with the streak at 0, once the props are out (|x| >= 4900), holding everything else for
up to 120 ticks (4 s at 30 fps, 2 s at 60). The discovery test plays all eight scripts through (the rules
above, with gallery.h choosing; both orders of the targets' moves and the loop, the task's first run in its
frame or the next; at 30 and at 60 fps): every one comes to its most, and no target stands up (risen or half
way) for more than 4 s before its hit - at most 2.7 s at 30 fps, 1.7 s at 60. A2 (its last wave: five
targets crossing in 60 frames) has the least room: at 30 fps, with the bonus task's 30 and 15 frames each 3
longer it loses its sixth bonus (at 60 fps those frames take half the time, and it keeps it).

### The Mercenaries (Endless combo, Kill enemies as they appear, Bonus time always on)

Added at the user's asking (2026-10-17: "add 2 new mercenaries only cheats that only appear when in that mode in
the gui. 1) Combo timer does not wear off, 2) auto kill enemies as they appear (make sure it counts for our
score)"). The panel draws both, with the combo, the kills and the score, only with Flags_SYSTEM's 0x40000000.

The game is `src\mercenaries.cpp` (its name in the asserts: 0x70F000..0x712800). The room's set-up (FUN_007123F0,
"St4ResultInitRoom", from the stages' room scripts) zeroes **the work at 0xC710A8** from +24 (0xAC bytes), sets
+60 the stage (room 0x400, 0x402, 0x403, 0x404 -> 0..3), +5C the character (pl_type 0 Leon, 2 Ada 1, 4 Krauser 2,
3 HUNK 3, 5 Wesker 4) and starts the task FUN_00712220 (FUN_006C4D10(0x14, thunk, the work, ...)): FUN_007108E0
(the start: the clock set, FUN_005A39E0([0xC163E0] = 2, ...), and started, FUN_005A39C0), then each frame the HUD
**FUN_00710C10(work)** until the clock is out - `pG+501C` 0x100000 (the player dead) off, the CountDown running
(bit 0) at 0 or below - calling FUN_00711740 every frame the player is dead (the combo, its window and the Bonus
Points time zeroed). Then FUN_00711740 and the results, FUN_00711B10: its HUD again each frame until the finish
display's animation is over, then FUN_007117F0 ("MercSysResult": the score `+24`, the time, the kills `+3C`, the
best combo `+44`, the character, a rank from 0xC16380) and the high scores in pSys.

The work: `+24` score, `+28` the combo (kills in a row), `+2C` the combo's window and `+30` the first kill's timer
(float frames), `+38` kills this frame (zeroed by the HUD), `+3C` kills, `+40` kills in a Bonus Points time, `+44`
the best combo, `+48` what the combo has earned (its bonus, not on the score yet), `+4C` a bonus on its way onto
the score, `+50` a Bonus Time's seconds (shown), `+54` the Bonus Points time (float frames), `+64` the HUD's flags
(0x1000000 a combo's second kill, 0x800000 the combo ending, 0x400000/0x200000 the Bonus Points display,
0x100000 a bonus being shown, 0x80000 the clock under 30 s, 0x40000 a Bonus Time to show), `+68` a dummy model.
The HUD's ID system is the object at 0xC71044 (re4_tweaks' IDC_GAUGE, 0x22); its unit lookup (FUN_00643580)
answers a unit it lacks with a dummy of its own (0xC599D0), never NULL (the "is NULL" prints are a no-op,
FUN_00974040).

**A kill scores in one place**, FUN_0070F520(kind, points) (`kSigMercScore`), Mercenaries only. Kind 9 adds the
points as they are (no caller passes it). Else: the combo +1 - the first kill starts the first kill's timer
(+30 = [0xC163E8], 300 frames, through FUN_00426AB0: n, or (1 - delta) + n above 1), the second zeroes it
(+64 |= 0x1000000) and each kill from the second on restarts the window (+2C); the best combo kept; the bonus
`points[kind][min(combo, 10) - 1] - base[kind]` (0xB25330, ten ints a kind; base 0xB254C0: 300, but kind 2 5000,
5 7000, 8 10000) onto +48, at least [0xC163E4] = 1000 in a Bonus Points time (+40 +1); base onto the score; +38
and +3C +1. A Ganado's bonus: 0, 20, 50, 100, 200, 250, 300, 350, 400, then 700 from the tenth kill in a row.
Its one caller is **FUN_0043A290** (cEm10's; ecx = the enemy): once per enemy (`+814` set), the kind by its type
(`+101`: 0, 1, 3, 4 and the rest 0; 2 -> 8; 5, 6, 8 -> 4; 7, 9 -> 3; 10, 13 -> 5 (Garradors, 7000); 11, 12 -> 1;
14-21, 23-25 -> 6; 22 -> 2; `+3D0` 0x10000000 -> 2). It is called (20 sites) as the enemy starts dying: in its
hit check's reactions and its death routines. Nothing else scores - only Ganados count.

**The combo's end**, in the HUD: the first kill's timer less the frame's delta, and at 0 the combo is 0
(`kSigMercFirst`, the `fsub` at **0x710C48**); unless the combo is ending, the window less the delta (the `fsub` at
**0x710D43**, `kSigMercWindow`) - past [0xC163EC] = 120 frames it blinks (the gauge, ID 0x30, colour 0x3F), at 0
it fades (0x3E) and +64 |= 0x800000, and when that animation is over the combo is 0. **What a combo earned**
goes onto the score only once the combo is 0 and no Bonus Points time runs (FUN_00428820(&+54, 0): the timer
within a frame's delta of 0): it is shown (ID 0x60, "+N"), moved to +4C, and added to +24 when that display's
animation ends (`kSigMercBonus`: `jle` at 0x71117A on +48, `jne` at **0x711183** on +28, both to 0x71125A). At the
game's end FUN_00711740 zeroes the combo, so the results' frames of the HUD bank a combo in progress - if the
finish display outlasts the "+N" one (not known). The pickups (cItemMgr::get): 115 Bonus Time -> FUN_0070F680
(its seconds onto +50, shown, and into `pG+4F82`, which CountDown::move adds to the clock x 30), 117 Bonus Points
-> FUN_0070F6C0 (frames onto +54; none running, +40 zeroed and its display started).

**The Bonus Points time** (`+54`) is counted down in the HUD as the combo's timers are (`kSigMercBonusTime` at
0x710F9E): unless +64 bit 0x200000 (its end being shown), above 0 it loses the frame's delta (the `fsub` at
**0x710FC3**), shows steady above [0xC163F0] = 120 frames (ID 0x40), blinks from there (0x3F), and at 1 or below
fades (0x3E) with bit 0x200000 up; once that animation is over, +54 = 0 and the bit goes. Bit 0x400000 (a
pickup's start) restarts the display. While it runs, a kill's bonus is at least 1000 and +40 counts the kills.
What a combo earned waits for it to be over too: after the `jne` on the combo, `lea ecx,[esi+54]; push 0; call
FUN_00428820; test al,al; je` (**0x711194**, `kSigMercBonus` +43, to the same 0x71125A). At the game's end and
every frame the player is dead, FUN_00711740 zeroes +54 with the combo, so the results' frames of the HUD may
still bank what waits (as for a combo, above).

**Bonus time always on** (the user, 2026-10-17: "I also noticed that the bonus time that can be activated wears
off. We should have a cheat to have bonus time always on, which is active before killing starts for the auto kill
mode"): `Site::kMercBonusTime` NOPs the `fsub` and `Site::kMercBonusBank` the `je`, and the tick
(`game::merc_bonus_time`, field writes) starts it as the pickup does at the first tick of play (`pG+20` 3, the clock
running with time left, the player up): +54 below 1 or bit 0x200000 up -> +64 |= 0x400000, &= ~0x200000 (and
+40 = 0 when below 1); +54 below 900 -> 900. Kill enemies as they appear waits while it is not running
(`Merc::bonus_time`: +54 at least 1, bit 0x200000 down), so its first kill already has it. Switched off, the
patches come out and the HUD counts its 900 frames down (30 s, as a pickup's). Run offline: the discovery test,
the game's own pickup, FUN_0070F520 and HUD at 30 and 60 fps - unpatched two kills in it earn 2000 and wait for
it to run out (score 600, then 2600); patched it stays at 900, the 2000 goes on once the combo is over, and with
Endless combo twelve kills each 3 s apart score 3600 and 12000 as they come; merc_bonus_time() on one running
(900, its kills kept) and one ending (started again); reverted it runs out in 30 s.

**Endless combo** NOPs the two `fsub`s (the first kill's timer and the window stay where the last kill set them)
and the `jne` on the combo (the bonus goes on as it is earned: shown 2 frames on, on the score after the display).
Turned off, the timers run on from where they stand. Run offline: the discovery test calls the game's own
FUN_0070F520 and FUN_00710C10 on the image's work, its ID dummy's animations ended each frame, at 30 and 60 fps:
unpatched a lone kill's combo ends 300 frames on and two kills' when their window is out (the bonus, 20, on
after); patched neither ends and twelve kills in a row score 3600 and their 3770 as they come; reverted the
combo ends. Death resets the combo still (FUN_00711740).

**The Ganados (cEm10)**: one class for the whole family (re4_tweaks' IsGanado: ids 0x10-0x20, 0x40-0x4F but
0x4B, 0x4E) - every Ganado module's constructor (33 sites, three to a module; each module registers one at
[0xC57068]) sets vtable **0xB1035C** after the base constructor (thunk 0x40295A); `kSigEm10` finds it in the
destructor (FUN_0044B8A0). Its move (vtable +10, FUN_0045AE20) runs **the hit check em10DmCk (FUN_00457D30, eax
= the enemy)** whenever `+FC` (r_no_0) is not 0, then the routine (table 0xBF39B0: 0 the set-up FUN_00461580, 1
about its business, 2 a hit's reaction, 3 dying, 4 FUN_0040C4E6's). Routines are written as FUN_00428F90(0xAABBCCDD)
= bytes AA BB CC DD at +FC (0x3020000 -> +FC 3, +FD 2). The set-up's hit parts (YARARE_INFO, a list from +344
through each one's +30, FUN_0058B570/FUN_0058B660): +344 the head (part 5; 3 for types 10 and 13), **+834 the body
(part 2)**, +868 0x14, +89C 0x18, +8D0 9, +904 0xF, +938 0x13, +96C 0x17, +9A0 8, +9D4 0xE, +A08 5 (0x25 for 10
and 13: the Garrador's back). FUN_0043AA90 zeroes `+814`.

em10DmCk with a hit waiting (`+32C`; else the room's damage areas, FUN_005A8B40, through FUN_004352B0, which
answers 0 whenever a hit waits): riding a truck (`+6A8`) or a drill (`+6B0`) it kills the enemy itself, only
with health above 0; `+FC` 1/`+FD` 0x5E with `+4B0` bit 8 takes the hit off without damage; else the damage,
**FUN_0044AD40** (edi = the enemy): FUN_005B1A90's table by id and weapon (every Ganado: the default table
0xC001B8, a handgun 150) x the firepower factor (0xC00B18, seven a weapon number, by `pG+4FC3`), then cut by
type and part (Garradors, types 10 and 13: x2 on the back, 0x25, else /8, or /32 on FUN_00439CA0's parts, +1;
the others on FUN_00439CA0's parts /4, +1; `+81C` 1 or 2 x0.56 or x0.45, +1; type 2 x0.67, +1; type 6 none;
`+7F0` 4 at most 1000), then **LifeDownSet2(enemy, damage, 0, 0)**
(its enemy branch acts only above 0 health, scales a Ganado's damage by the dynamic difficulty - x(2 - 0.2 x
level) to 5, x(1 - 0.03 x (level - 5)) above - and adds its dynamic difficulty points for a big hit and a
kill). Type 6 dies of a hit of any weapon number but 0x14, 0x16 (the incendiary), 0x17 and 0x2A (flashes), 0xE (the
mine thrower), 0x19, 0x1F and 0x20 (eggs): scored and 0x302 - and returns with the record still up. Else health below 1: FUN_0044B660(0) (a
core's effects), then the reaction by weapon number (0xBF3DB0: a gun's FUN_00458B90 - 1-4, 9-0xC, 0xE, 0x10,
0x11 ...; shotguns' FUN_00459860; magnums' and explosions' FUN_0045A110; kicks' FUN_00458800; a flash's
FUN_0044BC20; the P.R.L.'s FUN_0045AA80). A gun's, health below 1 and none of `+4B0`'s states (a ladder and
the like: those go to a reaction routine that dies itself): a parasite may come out instead (FUN_00457C80:
`+3D0` 0x100000, `+4B0` bit 0x80 not yet: FUN_00452BC0(2) - the same enemy, `+7EC` 1, health 3000, not scored
yet); else the ESL entry marked dead (FUN_005B2010), its item dropped (FUN_005B8CF0), **scored
(FUN_0043A290)**, and the death: types 2, 10, 13, 0x16 0x302; a wall close by (FUN_00435780's ray tests - not
for those types, nor in room 0x51B, nor at dynamic difficulty 10) the reaction 0x207; a leg (parts 8, 0xE) two
times in three the reaction 0x204; else 0x302. On the head (part 5) its head bursts (FUN_00452BC0) and the
outcome is by chance (one way leaves it 1 health and a timer at `+808`). After the reaction FUN_004305E0 may
have it notice the player (vtable +30) - not while it is far (`+378` at least 1e6, `pG+501C` bit 0x800000 off).

**Kill enemies as they appear** (`cheats.cpp` decides, `game::merc_enemies()` lists, `game::merc_kill()` writes:
field writes only, no patch, no game call): each tick of the Mercenaries' main loop (`pG+20` 3), with the clock
running with time left and the player's health above 0, every Ganado on the enemy manager's live list (the
vtable, `+4` & 0x201 == 1, the pool's stride at least 0xC64) about its business (`+FC` 1), not scored (`+814`),
with no hit waiting, gets **its health at 1** and a handgun's hit (weapon number 2: the plain gunshot's reaction)
**on its body** (`+834`, checked in its list with part 2) from where the player stands: the record as a shot
writes it (`+328` 0, `+32D` 10 frames, `+32E`, `+330` from, `+33C` distance, `+340` the part, `+32C` last). Its
move takes it the same frame: the damage (at least 4 for any Ganado after the cuts) takes the last point through
LifeDownSet2 as a shot's would, and the reaction scores the kill and starts its death. At 1, not 0, so the riders'
paths (health above 0) still kill. One the hit leaves about its business (a state that swallows hits) gets
another 15 ticks on. Since 2026-10-18 a Ganado in a reaction (+FC 2) is hit too while it counts as about (status
bit 5): em10DmCk runs in every routine but 0, and a gun's reaction FUN_00458B90 with `+7B4` above 0 (a 60-frame
timer FUN_0046F400 sets) sends a hit, even a killing one, to reaction 0x0B (+FC 0xB02) at health 0 or below
without dying - that one is hit again until it dies. Run offline: the discovery test calls the
game's own em10DmCk on the records merc_kill() wrote, over a stand-in pool (types 0, 2, 0x16, 10 and 6; one in a
reaction not about, one scored, another class and a dead one refused): each dies (+FC 0x302) and scores (300, 10000, 5000,
7000, 300; the combo to 5, its bonus 3200; 5 kills), and the one in a reaction (+FD 0xB) once about dies and
scores too (300, combo 6) - in room 0x51B (no wall's ray tests without a room), far
from the player, the effects and sounds stubbed.

**How the stages send their enemies** (the user, 2026-10-17, with both switches on: "it seems to take a long time
for the next set of enemies to spawn", "on the village level I believe 124 enemies can spawn? I'm sitting stuck at
62"). Each stage's room table entry (the rooms 0x400-0x405 at 0xC13768.., 0xC bytes a room: the set-up, then the
per-frame function; registered at 0x7E7610) runs a spawner, and three of the four send enemies **area by area**: the
scene's area triggers (FUN_006BDD20, from the main loop at 0x63E63C, before the room's function and the tasks)
clear the room's area flags each frame - `pG+17C` and `+180` (flags_ROOM, bit 64 on), and the triggers' own
"inside" bits at `[0xC67728]+10` - and every area trigger the player stands in sets its flag again (FUN_006B3B60:
`+5D` 0 set, else clear, the flag `+5E`). The village (0x400; per-frame FUN_007D3B00, through the table's thunk
0x407040) starts 450 frames in (`pG+174` |= 0x40000000) and runs one list per area flag up (0x80000000
FUN_007D0AE0 .. 0x2000000 FUN_007D3280): each spawns its entries in order (cEmWrap::setEm, FUN_008D7740 ->
FUN_005B27B0, which marks the ESL entry used, bit 2) while fewer than 10 Ganados are about (FUN_006C1010(0x10,
0x20): ids 0x10-0x20, in use, status bit 5) - the lists hold 16, 12, 33, 13, 6, 14 and 8 entries (102), three
pickup-made events 11 more (FUN_006B9170(0x80..0x82): the room's item flags), and 10, 25 and 35 deaths bring
chainsaws (FUN_007D0600, placed by the area the player is in). Room 0x403's (FUN_007DC120) has 22 area lists on
the same flags (0x80000000..0x400); room 0x402's spawner is a task (FUN_007D5040, started at
0x7D770B) gating its groups on the same flags (and its bosses on kills: 20, 25, 90). Room 0x404's task
(FUN_007E01B0) asks the triggers' own inside bits instead (FUN_006B4DA0(n), 143 callers game-wide). So standing in
one place, only the lists of the areas there ever spawn.

**Kill enemies as they appear** therefore also puts every area up in a game of the Mercenaries: `kSigAreaReset` /
`Site::kMercAreas` NOPs the clear of `pG+17C` (`mov [ecx+17C],ebx` at **0x6BDDD0**; `+180`'s and the inside bits'
stay), and the tick writes 0xFFFFFFFF there each tick (`game::merc_all_areas`) - in, while the switch is on and the
mode is the Mercenaries; out otherwise (the game clears the flags again the next frame). The stages' area readers
are their spawners and the village's chainsaw placement (always the 0x20000000 spot then); the other readers of
`pG+17C` are other rooms' scripts. Not 0x404, which reads the triggers' own inside bits: "The water stage".

**The pace of the waves**, the game's own: a Ganado killed counts as about (status bit 5) until its death
motion's 73 frames of delta pass (die 2, FUN_00444EF0, `+4B4`) or the motion ends (die 0, FUN_004507E0), so after
a burst the spawner waits ~2.4 s; its body keeps its slot in the enemy pool (60 by default, [0xBFC044], unless the
room overrides it, FUN_005A5090) until it has melted (die 3, FUN_00450C80: 15 + 53 frames of delta, then 10 of
fading; `+7F5` "reset enable" set) and the room frees it: the village every 300 frames (FUN_007D3A60, from its
per-frame function: cEmMgr::destroy on every melted Ganado), the castle every 300 of delta (FUN_007D4870). And
bodies start melting one at a time per stage: die 0 waits until the shared group controller's (cCtrl12, `+6E0`)
timer 7 is 0, then sets it to 15..24 (FUN_006F7BF0), counted down 1 a frame (FUN_006F7BC0) - so 2 to 4 bodies a
second at 60 fps, half that at 30. A pool full of bodies holds the spawns until a sweep. Kill enemies as they
appear hurries the queue and the village's sweep (below, "The fix").

**The village's stall at 50** (the user, 2026-10-17, with every area up: "only got to 50 this time and that includes
running into all the buildings to spawn extras"; the first game, before kMercAreas, stopped at 62). Read from the
code, nothing that stops the village for good was found:
- Its lists (the per-area functions' `push idx` before each setEm) name 102 entries (two twice in the third),
  every one a Ganado (id 0x16) of room 0x400 in omake01.esl; with the 5 placed at the start, the 11 the pickups'
  events send and the 6 chainsaws (entries 17, 18, 97, 98, 118, 119, 6500 health, none in the area lists), 124.
- An area function counts an attempt as a Ganado about (and towards the deaths that bring chainsaws, `w[2]`)
  whether setEm took it or not, so refused entries eat that frame's ten. FUN_005B27B0 refuses: an entry of another
  room, one sent (bit 2), one dead in the room's mask (`pG+502C` + list x 0x20, set by FUN_005B2010 at a death),
  one whose enemy is in the pool (FUN_005B21D0, by `+3A0`), and no free slot (FUN_005B2350: `+4` & 0x601 == 0).
  The village's entries can only meet the last: a full pool.
- Every enemy on the manager's list is moved each frame (FUN_005B01B0 -> FUN_005AF780: its hit record's frames
  first, FUN_005AEFA0, then the move) unless `pG+5020` bit 0x10000000 without the enemy's `+4` bit 0x800 - no
  enemy sits out, so a record never stays up. A Ganado that sets its `+FC` to 0xFF destroys itself.
- Every Ganado id (0x10-0x20) is cEm10 (each module's placement constructor sets 0xB1035C), every set-up
  (FUN_0043AA90, by `+395`) starts it in routine 1 with status bit 5 (sets 0x13-0x15 without it), and its body
  part (+834, part 2) is set up for every type.
- Bodies: die 0 (FUN_004507E0, sub-step 1) waits on the Ganados' one control (FUN_006F7F50 finds or makes the
  control of type 0x12; its 13 counters at +13), counted down by the control manager's move (0xBFC1C4,
  FUN_005A5B30: vtable +C of each, unless `pG+170` bit 0x2000000) each frame; in rooms of stage 4 at or below
  room 4 every body melts.
- So Kill enemies as they appear logs, every 15 s of play, the kills, what the stage counts about, the pool's use
  and bodies, the room's Ganados not sent yet and the models' memory (`game::merc_pool`, `game::merc_memory`); and
  when no kill comes for 12 s of play, **a census** (`game::merc_census`): the pool, the ids in it, every Ganado
  (slot, entry, type, set, the four routine bytes, `+3CC`, health, a hit waiting, scored, body gone, off the
  manager's list, no body part, how far), the control's counters, the models' memory and the room's list (sent,
  dead, with an enemy, spent) - again each minute it lasts.

**What the census showed** (the user's next game, 2026-10-18, 54 kills; the log's pace lines): the village's enemy
pool is **80** slots (the room's own size, FUN_005A5090(0) through the room's table at `pG+40`), 37 of them the
scene's for good (the player 00, six chickens 28, and doors 51 x6, boxes 53 x13, racks 55 x2, windows 56 x5,
barrels 58 x3 - re4_tweaks' names; a weapon 52 now and then). Fifteen seconds in: 25 kills, 10 about, 10 bodies,
64 of 80 in use, **99 of the room's 124 Ganados not sent**; fifteen seconds on: 43 kills, none about, one body, 38
in use, **15 not sent** - 84 entries sent and 18 kills, the rest nowhere. From there on only the pickups' events
sent anything (3 at a time), and no kill came for minutes. So neither theory: nothing was about (0 < 10) and the
pool had room. **The entries were spent on Ganados that never lived**:
- setEm (FUN_005B27B0) marks the entry sent (bit 2) *before* it runs the new enemy's move once (vtable +10): its
  set-up (routine 0, FUN_00461580) runs the module's data callback (each Ganado module puts its own at
  [0xC5017C]) and FUN_00460920 -> **modelInit** (FUN_0065EBA0: "EM10 pEm->modelInit() failed."), and on failure
  `+FC = 0xFF`, which the move turns into cEmMgr::destroy at once. The entry stays sent, not dead in the mask, with
  no enemy: the stage never sends it again.
- modelInit fails when its pools are short: a model record (FUN_0065E780 -> FUN_0065E150 on the manager in
  [0xC07110]: "modelInit() cModelInfo alloc failed.") or its parts (FUN_0065EA90 -> FUN_0065E9A0, a run of `+102`
  parts from the manager in [0xC07114], else one by one: "ModelInit() Parts allocate was failed."). Both are
  cManagers sized at the room's set-up (FUN_0063C830: FUN_005A5090(7) + [0xC6CC10] + 40 records, FUN_005A5090(6)
  + [0xC6CC10] + 100 parts; the defaults 300 and 1300, [0xBFC044+7*4], [+6*4]; a room's own sizes come from its
  data), and the sub-screens swap their own managers into the slots while up (0xC7B5C8/0xC7B5E8 against
  0xC07150/0xC07134). A body keeps its model until the sweep destroys it.
- With every Ganado killed as it appears the bodies hold the pools longer than the game ever lets them (a kill
  every second or so in play), and when the ten about fall the seven area lists each try their next entries that
  frame - up to ten tries a frame, each try spent if the pools are short. A few frames of full pools spend dozens
  of entries; that is the "stall".

**The fix** (2026-10-18): `kSigModelInfos` / `kSigModelParts` read the pools' managers' slots (`game::merc_memory`:
records and parts in use, the most parts a Ganado in the pool has, the Ganados about, dying - scored or a hit
waiting, still counted about - and bodies), `game::merc_entries` the room's list (sent, dead by the mask -
`pG+502C + list x 0x20`, the list in play `pG+4FB3`, none at -1 - and with an enemy in the pool), and
**mercmem.h** plans each tick of the main loop: what is coming is the dying's places and those below the cap of
ten; a kill goes out only while the pools have room for what is coming plus two (a chainsaws' wave sends two
whatever is about) - unless no body is there and none is dying (waiting would free nothing); an entry spent
(sent, no enemy, not dead - or without the mask, one sent since the last tick with no enemy) is sent again
(`game::merc_resend`: its bit 2 cleared) while the pools have room for a Ganado, one each - sending it again makes
no Ganado come by itself, the stage sends as many as it is short. The panel says "waiting for bodies to clear"
while a kill waits. And `game::merc_hurry_bodies` hurries the bodies' models away: a body waiting to melt (die 0,
sub-step 1) waits on the control's eighth counter (15..24 frames after the last one went), put back at 0 each
tick (one starts each frame), and in the village, with a body gone (`+7F5`), the work's frames since its sweep
are put at 300 (`kSigVillageSweep`: its work in [0xC7BF9C], `+C` counted each frame by FUN_007D3B00, past 300 the
sweep FUN_007D3A60) - a gone body is out of sight already, so the sweep the next frame only frees its slot and
model sooner. The castle's (every 300 of delta too) since 2026-10-18 ("The castle"); 0x403's not looked into (the user's
games there came to its most, 117); 0x404 has no sweep - its bodies stay, the zones reset them ("The water stage").

Run offline: the discovery test reads stand-in pools, Ganados and list through game.cpp (records 24/50 with four
being let go, parts 120/300, a Ganado's 52 parts, three about with one dying, three bodies; the list's sent, dead
and with-an-enemy bits; a spent entry sent again, a dead one, one with an enemy, one not sent, another room's and
a chicken's not; the counter and the village's sweep hurried, not outside the village), and plays a stand-in
village under the plan (5 placed, 102 sent in list order from frame 450 while fewer than 10 are about - every try
counting; 55 parts a Ganado from 1400, the scene holding 300; a death 146 frames, bodies melting one after another
20 frames apart for 156 frames, the sweep every 300): killing as they appear spends 82 entries and kills 25 (the
stall); with the plan all 107 die and none is spent, in 150 s at 60 fps, 78 s with the bodies hurried; with the
scene holding 900 (room for 9, below the cap) the stage's own fill-ups fail 93 times and all 93 are sent again,
all 107 dying in 79 s - with the dead mask and without.

**The second game with the plan** (2026-10-18: "I managed to get to 111 kills this time, but I was expecting 124. I
didn't see new spawns show up when entering the trigger buildings"; "Some enemies took awhile to die, some almost
reached me to hit me before being killed"): 111 kills in ~45 s, none spent. The pools as logged: records 451-487
of 563, parts 1276-1508 of **1673** (a Ganado's model **34**), the scene's own ~550-650 parts and ~300-400 records
(the dropped items' models among them, going as they time out); the enemy pool 63-77 of 80 - each Ganado about
holds a second slot, its weapon (id 52, as many as are about), a body one. The kills waited 808 and 877 of 900
ticks a window: the parts pool held them, bodies keeping their models ~5 s (die 2's fall 73 units, the melt's 15
+ 53 + 10). The 13 never sent: the pickups' **events** - FUN_006B9170(0x80..0x82) is "the item of area trigger
80/81/82 picked up" (FUN_006B4AF0 finds the trigger by `+36`, type 3 at `+35`; FUN_006B5820 tests its item flag:
`+1E` a bit of pG+528C, else `+22` a bit of the room's record `+8`), and the room's record (FUN_006B0A20 on the
manager at 0xC67560: `[+20]` + index x 0xD8, by the stages' room tables at 0xC13FD0/D4) keeps the event spent
(`+4` bit 80000000/40000000/20000000); each event function sends its 3/5/3 entries (0x3C-0x3E, 0x35-0x39,
0x28-0x2A) each only while the work's count is below ten, after the area lists have filled it - so with every area
sending, an event fired while any list has an entry left sends nothing, ever (in the game too, wherever the
player's own areas fill the ten) - and two of the chainsaws (a wave whose setEm found no enemy slot: refused, not
spent, and a wave never tries again).

**The second round** (2026-10-18): `game::merc_hurry_bodies` also moves a falling Ganado (die 2's sub-step 1) on to
die 0 once 15 of its timer's 73 units have played (`+FC..+FF` = 03 00 00 01, what die 2 writes when the fall's
motion ends: die 0 counts the kill for the results - FUN_005B20B0 - drops its item once, `+4` 0x10000, and stops
it counting about) and puts die 3's two waits (`+4B4` 15, `+4B8` 53) at 0, so it sinks and fades at once (10
units): a body goes about 1 s after the kill instead of 5. `kSigVillageEvents` finds the three event calls in the
village's function (`+3`, `+80`, `+165`; pG's slot at +26/+55/+104/+136, the records' manager at +39/+68/+117/+149),
and **Site::kMercEvent1-3** each span one event function's checks of the ten (`83 38 0A` before a jae, as many as
it sends entries - `6A FF 6A ?? 8D 4D ?? E8` - up to its `C3 CC`), the 0A of each made FF (cmp [work],-1: the jae is
never taken): an event sends all it has when its item is picked up. In with kMercAreas, with the switch on in a
game of the Mercenaries. mercmem.h counts the enemy pool too (`MercMemory::ems`/`ems_used`: a Ganado two slots, a
kill's place one more - the dying one's weapon slot comes free), keeps the room for a chainsaws' wave (two fresh)
beyond what is coming, and the tick kills the oldest first (the list has the newest first). The census lists the
entries not sent (entry, type, health).

Run offline: the discovery test's events (each span from the first check's 0A to the last's, 3/5/3 changed, 0A to
FF, in and out), the fall and the melt hurried on stand-ins (13 units of a fall played: left; 15: on to die 0;
the melt's waits 0), and a fuller stand-in village (the 124: 5 placed, the 102 in list order from frame 450, the
chainsaws' waves at 10/25/35 deaths, the events at 10, 15 and 20 s; the village's order each frame; the lists and
the events trying only below ten tries and Ganados about, the waves always; 80 enemy slots, the scene 37, two a
Ganado about and one a body, refused without a slot; 34 parts from 1673, the scene 650, spent without them):
killing as they appear, 43 killed; the plan with the bodies as the game has them, 113 (the events lost, waits up
to 7.6 s); hurried, 121 (the events lost); hurried with the events let send, all 124 in 20 s at 60 fps, none
failed or refused, none standing over 0.5 s (with and without the dead mask; the scene holding 850 parts, all in
20 s, 1.2 s at most). A reserve of five instead of two only made the waits longer.

**The third game** (2026-10-18: "We got to 113 kills this time ... But no spawns when entering the houses and still
shy of the 124 count"): 113 in ~35 s (73 in the first 15), all six chainsaws sent, none spent; the census's
"not sent": entries 0x28-0x2A, 0x35-0x39, 0x3C-0x3E - the three events' 11, never fired. **What fires them**: the
village's room data (`BIO4/St4/r400.udas.lfs`, unpacked as the enemy lists were) holds an ITA (the item sets; its
file table at 0x4E8: CAM SAT LIT ... ITA, the ITA at 0x90F160 of the unpacked file: "ITA\0", then 0x9C-byte
entries - `+0x31` the area trigger's type, 3 an item; `+0x32` n, the trigger 0x80 + n; `+0x74` the item id), and
its first three entries are **item 115, Bonus Time**: three hourglasses (at about -22780/31/-2690, 8180/840/17410,
20290/99/-10570). FUN_006B9170(0x80 + n) is that item's "got" flag (FUN_006B5820: `+1E` a bit of pG+528C, else
`+22` a bit of the room's record `+8`); item triggers can also sit on an enemy (`+46` 1, `+47` its entry) or an
object (`+46` 2; "ITEM SET[%d] failed: ETC[%d] not found", FUN_006BDD20). So an event sends nothing until its
hourglass is picked up - not on entering a house; with every area sending, a house's own Ganados come from the
start, so entering one brings nothing new.

**The hourglasses' Ganados without the hourglasses** (2026-10-18): the village's lists are read from its code at
discovery (`prepare_village_lists`): its function (kSigVillageSweep - 0x20: `mov eax,[pG]; test [eax+501C],1000`)
calls its seven area lists by their flags (`test [eax+17C],reg|imm32; je; call`), and each list, like each event,
sends through setEm (FUN_008D7740, every send the same target): `push -1; push entry; lea ecx,[ebp-n]; call` - the
entry an imm8, an **imm32 from 0x80 on** (FUN_007D1640's last ten), or a register (FUN_007D0AE0's entry 2, `push
ebx`: not read). 101 area entries (104 sends, two named twice), the events' 11, none in both (`game::MercLists`,
in room 400 only). mercmem.h then puts an event's Ganado not sent (not dead, no enemy) in the place of an area
entry that is dead and gone, one per Ganado's room as a resend (`Plan::swap_from/swap_area`), and
`game::merc_swap_entry` swaps the two 0x20-byte entries: the area's place not sent and not dead (its list sends it
again, as the event's Ganado: its type, health, place and set), the event's sent and dead (an hourglass picked up
later sends nothing more). The hourglasses stay where they are and still give their time. Logged once a game and
counted in the pace lines.

Run offline: the lists read from the exe (101 area entries, entry 2 by a register; the events' 11; none in both;
only in room 400), a swap on stand-in entries (3C into 0B's place: 0B the event's type 12, health 1800, flags 0,
not dead; 3C sent and dead; not the wrong way round, not into an entry not dead, not twice), and the stand-in
village with no hourglass picked up: 113 without the swaps, all 124 in 13 s with them (11 swapped in, none
standing over 0.5 s).

**A chainsaw short** (2026-10-18: "I got to 123 kills this time. Are we missing one?"): two games of 123, the
hourglasses' 11 swapped in each within 16 s of the start (the first, entry 40, in 15's place), none spent; the
census: 124 Ganados, 123 sent and dead, **not sent: 98 (type 12, 6500)** - both games. Entry 98 is the second
chainsaw of the wave at 25 deaths. The village's function ends with the three waves (**kSigVillageWaves**, right
after the events' block: at 0x7D3D37, 0x7D3D7B, 0x7D3DBF): `[work+8] - [work+0]` (the tries less those about) at 10,
25, 35 and the wave's flag not up - `test [pG+174],10000000 / 08000000 / 04000000` - then FUN_004AB510(3 / 4 / 5)
(sets flag n: the word n >> 5 from pG+174, bit 80000000 >> (n & 31)) and FUN_007D0600 twice, esi the entry (`xor
edi,edi; lea esi,[edi+E]; call; lea edi,[esi+1-E]; lea esi,[edi+E]; call`: E and E + 1 - 0x11/0x12, 0x61/0x62,
0x76/0x77) and edi which of the pair. FUN_007D0600 clears the entry's sent bit (`and byte [entry],FD`) and its dead
mark, setEm(entry, -1, 1, 1, 1) whatever is about, then moves the enemy to a chainsaw spot by the area flags (with
17C's 20000000, entries 17/18's place, `pG+5630/5650`; else with 180's 80000000 97/98's, `+6030/6050`; else
117/118's, `+62B0/62D0`). A send setEm refuses (no free enemy slot) leaves the entry not sent; the flag is up and the
wave never goes again. The wave sends both at once, beyond the ten, and the pool ran fuller than the plan counts it
(the pace lines: 77 of 80 with 10 about and 9 bodies, ~11 above two a Ganado about and one a body - weapons that
outlive their Ganado among them, 52s in the pool with none about), so the reserve of two was not enough at that
moment. The 124: 5 placed as the room starts (their entries' +0 bit 1, re4_tweaks' EM_BE_FLAG_ALIVE: 5..9), 102 in
the area lists, 11 in the events, 6 in the waves - nothing else sends any.

So `game::MercLists::waves` has the entries of the waves gone (their flags up, read each time from pG+174), and
mercmem.h and `merc_swap_entry` take them as they take the events': one not sent, not dead, with no enemy goes into
an area entry's place that is dead and gone, and its list sends it (at its own place: 98's is one of the spots the
waves use, (1967, 17, -1112)). Never before its wave has gone: the wave clears its entries' sent bits and dead marks
and sends them whatever they hold, so a swap before would bring the area's Ganado back. One spent as its wave went
(sent, no enemy, not dead) is sent again (its sent bit cleared) and then swapped the same way. Each logged ("the
village's chainsaws' Ganado 98 was not sent as its wave went ... it comes in the place of an area list's Ganado that
is gone, N") and counted in the pace lines; the census names what sends each entry not sent (an area list's, a
pickup's event's, a chainsaws' wave's, gone or to come). `prepare_village_waves` reads the waves from the code (the
slots agreeing with the village's and pG's, flag = 80000000 >> n, the pair's arithmetic, one flag setter and one send
for all three, none of the six in the lists or the events); check_sigs also checks the setter's and the send's
bodies.

Run offline: the waves read from the exe (17 and 18, 97 and 98, 118 and 119); on stand-in entries, the waves' entries
the lists' only with their flags up (the first: 11, 12), 62 not swapped before the second wave's flag and swapped
after (into 0C's place: type 12, health 6500, not sent and not dead; 62 sent and dead); the stand-in village with
each wave's second refused: 121 without the swaps, all 124 in 13 s with them (the 3 swapped in, the events' 11 too;
none standing over 0.4 s).

**The castle** (the user, 2026-10-18: "village is working. However it looks like we got stuck on the castle level at
around 285 enemies instead of the expected 348"). The log: 285 kills in ~2 min, then nothing - the census: 0 about,
the pool 34 of 100 (its scene: the player, doors 51 x4, boxes 53 x12, torches 57 x14, barrels 58 x3), 117 Ganados (id
1C) in room 0x402's list (omake01), 88 sent and dead, 29 never sent; the pace lines had the pool at 82-90 of 100 with
23-26 bodies gone and waiting for the sweep. The castle's room table entry (0xC13768 + 2 x 0xC, filled at 0x7E7610):
set-up FUN_007D72D0, per-frame FUN_007D4870.

**How it sends**: the set-up allocates its work (`[0xC7BFA8]`, 0x5A8 bytes, zeroed), sends entries 0xA0-0xA7 (8, list
flag 1: the start's), starts the spawner FUN_007D5040 as a task and gives two area triggers events (below). The
spawner zeroes its 14 counters (+568..+59C) and +5A0, then each frame counts the wraps whose enemy is about (`edi`:
FUN_008D7900, in use and status bit 5) and runs its groups by the area flags (pG+17C), each only while that count is
below its own limit (6 to 10). It never sends through setEm: **EmSetEvent** (FUN_005B2A60(entry*)) takes a slot by id
(FUN_005B2350; 0x0F and 0x25 FUN_005B23F0), copies the entry in (type, set, flags, health, place), sets the entry's
flag to 7 and runs the new enemy's move once - whatever the entry holds (sent or dead), so an entry comes again and
again - or, with no free slot, returns the dummy at [0xC570A0] ("R402EmSetSub : EmSetEvent error"). Each send goes
into a **cEmWrap** of its own (0xC bytes at work + n x 0xC, 114 of them: +0 the enemy, +4 -1, +6 0x1FF - +7 1 while
set - by setPtr, FUN_008D7850, only for an enemy in use; FUN_008D78D0 "in use"; FUN_008D7940 destroys through it,
clearing its entry's flag bit 0, and zeroes it). FUN_007D4760(entry, 1) with `eax` the wrap is the send;
FUN_007D4FF0(entry, cap, 1) with `esi` the counter and `edi` the wrap the tail's.
- **Counted groups** (14, `cmp [reg+counter],cap; jge; ... in-use test of its wrap; jne; push 1; push entry; mov
  eax,wrap; call FUN_007D4760; inc [counter]`): +568 45 (0xAD-0xB1, area 0x8000000, fewer than 6 about), +56C 30
  (0x8C-0x8E, 0x80000000), +570 18 (0xED, 0xEE, 0x40000000), +574 25 (0x90, 0x91, 0x8A, 0x8B, 0x20000000), +578 10
  (0xE1, 0xE2, 0x10000000), +57C 10 (0x85-0x88 through FUN_007D4FF0, 0x80000), +580 20 (0x81-0x83, 0x40000), +584 20
  (0xB7-0xBA, 0x20000) or once pG+174 0x40000000 is up +588 25 (0x7C-0x7F), +58C 30 (0x78-0x7B, 0x4000000), +590 5
  (0xAB, 0xAC, once pG+178 0x20000: the set-up's first two gone), +594 15 (0xE8-0xEA, 0x10000), +598 20 (0xDE-0xE0,
  0x4000), +59C 15 (0xE3, 0xE7, 0xEB, 0x2000): 288 sends from 46 wraps. Every wrap free (FUN_008D78D0 false) is sent
  into in the same frame, and **the counter goes up whether EmSetEvent took the send or not**.
- **One-shot steps** (a flag in pG+174/+178 put up first): the set-up's 8; 0xB3 (a Garrador, area 0x8000, 25 kills);
  the bosses - tasks FUN_007D4AF0 (0xA9, area 0x4000000, 20 kills; +174 0x200; a camera scene, its cage opened) and
  FUN_007D4D70 (0xAA, 150 of +5A0 later and 90 kills; 0x100); 0x100000's four (0xFA+0xF8, 0xFB+0xFC, 0xFD, 0xF9);
  0x80000's thirteen in a row (0x93, 0x94, 0x97, 0x9B, 0x9C, 0x95, 0x96, 0x98, 0x99, 0x9D, 0x9E, 0x9A, 0x9F - flags
  0x21-0x28 through FUN_004AB510), then its counted tail; 0x80000000's 0xD5+0xD6 and 0xD2+0xCE+0xCF, 0xD3+0xDA;
  0x40000000's 0xC8-0xCA, 0xCB+0xCC; 0x2000000's 0xDA+0xDB; 0x20000000's 0xD8+0xD9; 0x800000's 0xDC+0xDD (once object
  0x1B's +3D0 has 0x10000000); and **the two gates** (below): 60 in all. With the 288, **348** - the user's number.
  Entries 0xBB-0xBE, 0xC0-0xC3, 0xC5-0xC7 and 0xD0 are sent by nothing.
- **The sweep**: FUN_007D4870 adds the delta to `+5A4` and **past 300 (10 s)** puts it back at 1 and destroys through
  its wrap every enemy in use, not about and gone (ckResetEnable, vtable +48) - bodies outside a wrap stay.

**The gates** are area triggers (the scene's list, `[0xC67728]+8C`: a word a node - -1 the end, bit 0 a link to
follow, else the trigger, its +0 the next; FUN_006B4AF0 finds one by its number at +36). The room data's AEV
(`BIO4/St4/r402.udas.lfs` unpacked: tag 16 of its file table, "AEV\0", version 0x104, 27 entries of 0x9C bytes from
+0x14 - a trigger's memory from its +4) gives each: +4/+5 the shape (1, a four-cornered area), +8 its floor, +C its
height, +10 a radius, +14..+33 its corners (x, z), +34 bit 0 on, +35 its kind (2 an event, 3 an item, 4 a door, 5,
0xB), +36 its number, +37 (bit 0: tested at the point the caller passes in `edi` - in front of the player - else at
his place in `ecx`; bit 1 a height check by +48/+49), +38 how it is set off (1 walked into, 2, 8 the action button),
+39 who (1 the player). SceAtDataSet_exec (FUN_006B4C60(at, 0x14, 0, event, param, flag)) puts the event at +40 (and
+3A 0x14: run as a task). The castle's set-up gives **0x10** FUN_007D49D0 (the action button, its area at x
11949..16120, z -57026..-55414: once - pG+174 0x40000000, which also switches +584's group to +588's - it turns
itself off (SceAtSetEnable, FUN_006B9670) and, with fewer than 5 wraps about, sends 0xF0-0xF6 and slides objects
0x40/0x41 open, FUN_007D4350(1)) and **0x0F** FUN_007D7040 (walked into, at x -1274..4355, z -49929..-41632: once -
0x80000000 and 0x1000 - turns itself off, sends 0xE4-0xE6, 0xB4, 0xB5, and plays a short scene, camera 0xB, while a
task opens the same objects). Each frame the scene's pass (FUN_006BDD20) calls FUN_006BB180(player, 1): for each
trigger on and his, FUN_006B7DB0 builds its shape (FUN_006B3540: the 12 dwords from +4, moved with an object at +4C
if any) and tests the point (FUN_0058B440 -> FUN_0058ADB0: floor - 1 <= y < floor + height, then the corners' cross
products); inside, an action-button kind (+38 & 8) registers a prompt (cActionButton::set), else (+38 & the caller's
1) runs the kind's handler from 0xC144D0 - kind 2's, FUN_006B37F0, the event directly (+3A 0) or as a task
(FUN_006C4D10(+3A, event, +3C, ...)).

**What the census showed**: never sent - 0x7C-0x7F (the group the action button's gate switches on), 0xF0-0xF6 and
0xE4-0xE6, 0xB4, 0xB5 (the gates', never set off: nobody went there), 0xA9 (the first boss: its flag up, its send
refused) and the twelve nothing sends. So 37 behind the gates, 1 boss, and the 25 more: the counted groups' sends the
pool refused. With every area up (kMercAreas) every group whose wraps are free sends them all in the same frame once
few are about - some 36 sends, 72 slots, against the 66 the scene leaves - and with the sweep only every 10 s the
bodies held 23-26 more.

**The fix** (2026-10-18; kSigCastleSweep, kSigCastleTask, kSigCastleEvents, kSigAtFind): `prepare_castle` reads the
work's slot, the sweep's timer and its 300, the counted groups (`castle_groups`: each `cmp dword [reg+counter],cap`
and the wrap put in ecx right before the in-use test's call - `mov ecx,reg` or `mov ecx,[slot]` with or without `add
ecx,wrap x 0C`, or `lea ecx,[reg+wrap x 0C]` - and FUN_007D4FF0's `push cap; add esi,counter; push entry; mov
edi,wrap`; check_sigs reads them the same way) and the gates (their numbers and events, the list's slot). Then, with
Kill enemies as they appear on in the castle: **its areas in turn** - `merc_all_areas(tick)` puts up bits k and k +
16 (k the tick mod 16) instead of all 32 (the player's own still go up each frame): a frame's sends are at most one
area pair's, seven at most; **its sweep hurried** - with a body gone, `+5A4` at 300 (`merc_hurry_bodies`), so it
sweeps the next frame; **refused sends given back** - `merc_castle_give_back` each tick of the main loop compares
each counter's rise since the last tick with the group's wraps that took a new enemy (in use now where none was,
another, or the same slot alive again after being a body - the pool gives the swept body's slot to the next) and puts
the rest back on the counter; **the gates opened once the stage runs dry** - in play, nothing about or dying and no
kill for 3 s, `merc_castle_arm_gate` widens the next gate still on round the player for one tick (its corners 30
times as far from their middle, the middle at him, its floor 20000 below him and 40000 high) and makes it walked into
(+38 1), so the game's own pass runs its event that frame; the next tick puts it back. 0x10 first (the action
button's group's 20 are done by then), then 0x0F; three tries each, 3 s of quiet apart. Logged (each gate, the first
give-back, a count at the end) and in the pace lines; the census lists each group's count against its cap, the gates
and pG+174/+178. Not the bosses: a boss whose send the pool refused stays lost (with the areas in turn and the sweep
hurried none was in the stand-in).

Run offline: the checker and the discovery test read the 14 groups (288 sends, 46 wraps) and the gates (0x10
FUN_007D49D0, 0x0F FUN_007D7040, their flags 40000000 and 80000000); on stand-ins, the areas in turn (and every area
in the village), the sweep's timer put at 300 with a body gone, the give-back (a send that took, one the pool
refused, a kill, a slot the pool gave again, a failed set-up, a tick missed, outside the castle), and the gates - the
game's own shape and test (FUN_006B3540, FUN_0058B440) find the stand-in player in a gate's trigger only while it is
widened, and the trigger comes back byte for byte; one turned off, or with another event, is left alone. A stand-in
castle (the groups, steps and gates above; 100 slots, the scene 34, two a Ganado about and one a body; refused under
two free; the plan's kills; 60 fps): as the user's game had it 289 (22 refused); the sweep hurried alone 283; the
areas in turn, the sweep hurried and the give-back 311, none refused; with the gates **348 in 35 s**; every area up
with the rest 324 (28 refused - mostly one-shot sends, with no count to give back).

**The base** (room 0x403; checked 2026-10-18, the user: "castle and military base are working"): its set-up (FUN_007DBA50)
sends one of three start groups at random (entries 0-5, B, 11, 17, 1D; 8C-95; 96-9F) and its per-frame function
(FUN_007DC120) 22 area lists on `pG+17C`'s flags (105 entries, 104 read - FUN_007D7AF0's last by a register;
FUN_007D9390 sends entry 0x40 twice and 0x41 never - a slip in the game's code) and two one-time sends through
FUN_007DB150 (esi the entry: its sent bit and dead mark cleared, setEm whatever is about) once its tries less those
about (work +C - +4) reach 20 (0x89, pG+174 0x40000000) and 45 (0x8A, 0x20000000). The user's first game: 117 kills,
all it sends; TASVideos' Base run gives 117 for every version but the NTSC GameCube's (101). Their next (2026-10-18:
"on the military base I only found 116 / 117"): 0x8A not sent, its flag up - its setEm found the pool full (83 of 90
at that time), and the send never comes again, as the village's chainsaws'. **The fix**: `prepare_base`
(kSigBaseFrame) reads the lists' entries and the two sends as the village's are read, and `merc_village_lists` gives
the base's (its area lists', its one-time sends' gone) in room 0x403, so mercmem.h and `merc_swap_entry` put such a
Ganado in the place of an area list's that is dead and gone, as in the village.

**The castle's last three** (the same day: "On the castle I had to go to a place to spawn a few more enemies"): the
second boss (0xAA) waits for `+5A0` to pass 150, counted only in frames with its area (0x4000000) up and fewer than 9
about - with the areas in turn, a frame in 16, so some 80 s; and 0xDC, 0xDD (area 0x800000) wait for door object 1B
(FUN_0061C810) to be open: `+3D0` 0x10000000, which the door's opening routine (FUN_005CAC10, routine 3's end) sets.
Now the castle's area flags are the pair in turn *and* 0x4000000 every tick (the boss's timer at the game's pace; its
group +58C of 4 wraps sends with any pair), and `Site::kCastleDoor` (kSigCastleDoor, the `je` at **0x7D63EB** after
the door's test -> NOPs, with kMercAreas) lets the step send its two whatever the door - once (pG+174 0x8000), the door
left as it is.

**The water stage** (room 0x404, Waterworld; the user, 2026-10-18: "the island or water area I only got 125 / 156
expected enemies"; then "I want to be sure that for all these stages, that we never exceed the possible spawn count,
that we just ensure that every spawn is triggered so we hit exactly the max"). Its room table entry (0xC13768 + 4 x
0xC): set-up FUN_007E0980, per-frame FUN_007DC490 (empty). The set-up allocates its work (`[0xC7BFEC]`, 0x30 bytes,
`+4` zeroed), starts its spawner FUN_007E01B0 as a task, and FUN_007DC5E0 picks one of three start points at random
(FUN_006AEED0 % 3: (-640, 0, -15890); the tower (2734, 8000, -26577); (26356, 3000, -32655)) and sends its group of 10
(FUN_008DA310, setEm: C8-D1; A0-A9; AA-AE, B8, BB, BF, C3, C6). It gives three kind-2 area triggers (19-1B) events
through FUN_006BF4F0 (callbacks FUN_007DC570/FUN_007DC500: FUN_006BFBA0 - effects, no enemies) and AT 0 the action
button's event FUN_007E0590 (a scene). Nothing sweeps its bodies: the pool (150) holds them, and its zones reset them.

The spawner waits a frame, then: `+4` = the id entry F5 has (FUN_005B1F60: 0x20), `+0` = `+8` = the Ganados of that id
about (FUN_006C1010(id, -1): in use, status bit 5), its zones' counters (`+14..+2C`) zeroed, eight tasks started
(FUN_006C4D10(0x14, task, 0, 0, 2, 0)); then each frame it counts those about again into `+0` and runs its one-shot
areas, each once (the room's record, FUN_006B0A20(room)+4, a bit each), each only with its area's inside bit up
(FUN_006B4DA0(n): `[0xC67728]+10`, a bit a trigger number) and few enough about: 0F (no more than 10 about; FUN_007DE270:
B4-B7, record 1000000), 10 (10; FUN_007DE300: BC-BE, 800000), 11 - or 15 with the door object 11 broken (FUN_0061C810,
FUN_005CBF00) - (15; FUN_007DE380: B9, BA, then +3D0 bit 1 on both, 400000), 12 (10; FUN_007DE460: FC-FE, 200000), 13
(15; inline: C4, C5, 100000), 14 (15; FUN_007DE560: C0-C2, 80000): 17 Ganados, each sent by FUN_007DC9D0 (eax the entry,
forced: whatever is about). Seven of its tasks are **zones**, each with an area trigger of its own (08: E1-E5, counter
+20; 09: F0-F2, E6, E7, +14; 0A: DC-DE, EB, EC, +18; 0C: F6-FA, +1C; 17: AF-B3, +24; 16: D3-D7, +28; 18: D2, D8-DB,
+2C): each frame its area is up, it sends its five in order - before each, `cmp [work],0A; jae` ends the frame's sends
(fewer than 10 about), FUN_00682420 & 0x40000 skips the entry (the player on a ladder: r_no 0/0x10, FUN_00765560, a
climb with step sounds) - an entry not sent yet by setEm (counted whether setEm takes it), a sent one by finding its
Ganado (FUN_008D98D0: the first in the pool with `+3A0` the entry, FUN_005B2250; setPtr) and, once its body has gone
(cEmWrap's ckResetEnable, FUN_008D90D0: a Ganado's id but 0x18, then vtable +48 - cEm10's FUN_00436200, `+7F5`),
resetting it (setReset, vtable +4C, FUN_004577A0: the same slot, a new Ganado of the entry) - `+0`, `+8` and its counter
+1 a send; a send after the first heads for the player (setGoto, FUN_008D8F50). After a frame's sends its counter at
**15** (zone 17's **10**: `cmp [eax+24],0A`) ends the task. So a zone sends 15 (10) - but one with a single send left
whose next frame's sends are all five ends at **19** (14): TASVideos' Waterworld run (#6832, "Enemies killed:
60+49+47=156 (Maxed)") calls it the spawn glitch - "if the player leaves the zone and there is only one Ganado left to
respawn, when the player returns to that zone, five ganados will spawn in its place" - and needs it in every zone. The
eighth task sends once (record 2000000) when `+8 - +0` reaches 30: F5 with the player past x 10000, else FB. So the
stage's most: 10 + 6 x 19 + 14 + 17 + 1 = **156**. The tasks run in the reverse of the order they were started -
FUN_006C4D10 puts a task's slot entry (0xC67BE4 + slot x 0xC) at the head of its list (FUN_0064C6C0: list 2's head
0xC67BC4), and FUN_006C4440 runs the lists from 0xC67BF8 (list 15, each list's end linking to the next lower,
FUN_0064C720), each task once a frame - so the eighth, zones 18, 16, 17, 0C, 0A, 09, 08, then the spawner (the counts
again, the one-shot areas); in the frame the spawner first runs it starts them and they run after it. They run in
ScenarioMove (FUN_006C52B0, 0x63E65C), after the scene's area pass (0x63E63C) and the action buttons (0x63E646) and
before EmMgr.move and the player's move: what a zone sends in a frame is known at the tick before it.

The user's game (2026-10-18): 125 kills, the census - 84 Ganados in the room's list, 60 sent: every zone at its end
(15, 10), the tower's top's (12: FC-FE) never sent, the other two start groups' 20 never; 60 bodies in the pool (none
swept, as designed): 10 + 6 x 15 + 10 + 14 + 1. Every area's flag up (kMercAreas) does nothing here.

**The fix** (2026-10-18; kSigWaterTask, with kSigAtFind and kSigPlayer): `prepare_water` reads the spawner, its eight
tasks, each zone's area, entries (the sent-bit tests `test byte [pG+5410+entry x 20],2`, each entry pushed twice),
counter (`add [eax+counter],ebx`) and end (`cmp [eax+counter],end; jae`), its five about checks and ladder tests, the
one-shot areas (`push n; call` the inside test, FUN_006B4DA0 checked byte for byte with the triggers' slot) and their
sends (`mov eax,entry; call` FUN_007DC9D0, inline and in the one-shot functions it calls directly), the eighth (`mov
eax,[work]; mov ecx,[eax+8]; sub ecx,[eax]; cmp ecx,30; jae`, its sends a jmp away) and which player routines are a
ladder (FUN_00682420's two jump tables: r_no 0/0x10 is its only 0x40000); check_sigs.py reads them the same way.
`game::merc_water` reads the stage each tick: `+0`, `+8`, each zone's counter and which of its five would go now (not
sent, or its Ganado's body gone, as the code tests it), the eighth, the ladder. **mercwater.h** plans each frame: a
zone's area up only when its sends this frame keep its counter at or below one short of its end, or when, one short,
all five go - its last frame; a zone whose ready Ganados would pass one short is up alone with the stage's count of
those about (`+0`) put up for the frame, so it sends exactly what is left (the spawner counts again after the zones);
no zone up in the frame the eighth sends; nothing up and the eighth held (the count put up) while the player is on a
ladder (their sends would be skipped, the one-shots' and the eighth's lost); the one-shot areas up otherwise.
`game::merc_water_set` puts the areas up - on (`+34` bit 0) and widened round the player as a castle gate is (the
same `widen_round`) - and every other spawn area off, so the player's own place sends nothing; the first write keeps
each trigger, `merc_water_release` puts them back (the switch off, the game over). Only plain areas (kind 0) on no
object are touched: kind 0's handler (FUN_006B37B0, run only with `pG+501C` 0x40000000) just records who is inside
(`+5C`, cleared each frame). Logged: the stage's first line, each zone as it finishes, the one-shots, the eighth; the
census has each zone's sends against its most, the one-shots, the eighth, the start group and the most.

Run offline: the checker and the discovery test read the stage (zones in the order they run 18 +2C, 16 +28, 17 +24 ends
10, 0C +1C, 0A +18, 09 +14, 08 +20; one-shot areas 0F 10 11 15 12 13 14, 17 Ganados; the eighth at 30, F5/FB; the
ladder r_no 0/10; 146 besides the start group); on stand-ins, merc_water (a zone's five: not sent, a body gone, one not
gone, none, another class; the eighth done, a one-shot sent, the start group, the most 156, the ladder) and the areas
(the game's own shape and test find the player in those put up and in no other, the count put up and not lowered, an
event's trigger left alone, every trigger back byte for byte, nothing outside the stage); and a stand-in Waterworld
(the start's 10; the eighth, the zones in their order, the spawner's count and one-shots; a fall 15 units, one body
starting to melt a frame, a melt 10; kills at each tick): with the plan **156 in 8.5 s at 60 fps** (9.3 at 30, 9.7
with the player on ladders now and then), every zone at its most, never past 156; every area up without the plan 132;
walking zone by zone without the tower's top 125 - the user's game.

### Costume and difficulty

The costume set is `+4FCB`; PlSetCostume (FUN_00680C50) derives Leon's own costume (`+4FC9`) from it as each
room is set up (Normal: his jacket, the sweater the story puts him in, or the vest with the Tactical Vest;
Special 1 the R.P.D. uniform; Special 2 the gangster suit), so a change shows at the next door. Ashley's is
read live. The difficulty is `+847C` with Flags_SYSTEM's 0x20 kept in step (set only for 6); the save
writes a copy into the file's head (`S+3D`). What the game reads as it goes (damage, pickups' ammo,
WeaponId2ChargeNum's Amateur doubling) follows at once; what a room set up stays until the next.

### Items, the attache case, and the three approved calls

`cItem` is 0xE bytes: `+0` id u16, `+2` num u16, `+4` be_flag (bit 0 in use), `+5` chr (whose), `+6` the
weapon word (upgrade levels - 1 in four nibbles: firepower, firing speed, reload, capacity), `+8` loaded
rounds << 3 (low 3 bits kept), `+A`/`+B` the position on the case's board (piece centre × 2), `+C`
orientation 0..7, `+D` on the board. The case is a grid; pieces come from the table at 0xC0B1B8 (stride 0x58:
id, +4 width, +5 height, ...; 64 entries, all rectangles; `kSigPieces`), and an item with no piece cannot
go into the case. itemInfo (FUN_007057D0) switches on a kind byte per id (table 0x705B04, ids < 0x110;
`kSigItemInfo`) and fills the type and the stack limit (difficulty dependent - `items.h` has the same
switch, generated by `tools/gen_items.py`). WeaponId2ChargeNum (FUN_00706590): rows of 11 shorts at
0xB24B10 (count at 0xC1622C): id, .., `+6` the ammo item, then capacity by level 1..7; 0x8000 infinite;
Amateur doubles. WeaponId2MaxLevel (FUN_00706620): rows of 6 bytes 0xB24EC8..0xB24F7C (id, max per stat).

- **PutInCase** (FUN_0068C4D0, cdecl(id, num, size) → bool in AL; its caller `kSigPutInCase`): the pickup's
  own placement - num clamped to the stack maximum (1 for a weapon or a part, types 1/9); FUN_00705FE0
  collects every in-use item of that id the character has, and when their room (max - num each) takes all
  of it they are topped up in order and no piece is placed (AL=1, 0x68C5BA); otherwise a new piece is
  placed - turned if it must - the others filled to max and cItemMgr::get makes the piece with the rest.
  All or nothing: no room for the new piece, nothing changes (AL=0).
- **cItemMgr::get** (FUN_00707A80, thiscall(id, num)): key items, files, maps, treasures (off the grid).
  A treasure (type 5) or gem (12) the character holds goes onto that entry - past the entry's max it is
  set to the max and the rest is dropped, still true; a file (10) or bottle cap (13) held returns true
  with nothing added. Anything else takes the first free cItem (num clamped to the max) - a second key
  item is a second entry. The item box's Take (and Add) hand it no more than the held entry keeps, and a
  held file or cap nothing.
- **cItemMgr::erase** (FUN_00708760, thiscall(cItem*)): removal; unequips a weapon going out.

All three run on the main thread while paused (`inventory.cpp`), and a new entry is found by diffing the
in-use flags before and after. Counts, rounds and levels are field writes on an entry re-checked (index,
id, owner) before each write.

**What `+6` and `+8` hold depends on the item** (2026-10-15: the user's box showed Handgun Ammo and Green
Herbs "with upgrade stats", in entries of their own). cItemMgr::get sets up a new entry with FUN_00707780
(thiscall(cItem*, id)): id, be_flag 1, num 0, chr, `+A..+D` 0, and then by type only - a **weapon** (1) gets
its levels word (0; the Chicago Typewriter 0x6006, firepower and capacity at 7; 0x40 becomes the Punisher,
0x21, firepower 2 if pG+52D0 has 0x8000; in Separate Ways each from a row of 0xC16196, 6 bytes a row by
id) and a full magazine at its capacity level, `+8`'s low 3 bits cleared (a weapon's ammo choice: FUN_00706530
(id, choice) finds the ammo in the charge table by its row's `+2` byte, which is 0 in every row - no weapon
has a second ammo); a **part** (9) `+6` 0 and `+8` 0xFFFF, attached to nothing; a **file** (10) `+6` (a byte)
the number of files held (FUN_007076F0), its place in the list. **Any other item keeps what the entry's last
occupant left there** - erase clears only be_flag, and the first free entry is reused - and nothing reads
it: the trigger (FUN_007090F0) takes a round from `+8` for type 1 only, uses up a grenade, launcher or egg
(types 3, 6) by its count (FUN_007088B0; the bowgun and Krauser's bow take bolts and arrows from the case,
FUN_00709060), and PutInCase stacks by id alone. So ammo picked up into the Handgun's old entry reads
"0x1111, 13 rounds". A part's `+6`/`+8` are a link: 1 and the index of the weapon it is on. FUN_00707E80
attaches (called by the case's combine, FUN_007089A0; a weapon takes one part - another one on it comes off),
cItemMgr::erase of a weapon (type 1, or 0x6D) sets each part linked to it back to 0 / 0xFFFF, and
FUN_00707120 counts a weapon's parts for its "w/" id - 36, 38, 40, 50, 81, 107, 108, 153 and 171 are never a
cItem's id, so they have no piece. `items::keeps_state` (items.h's `state`, by gen_items.py: type 1 with a
row in WeaponId2ChargeNum's or WeaponId2MaxLevel's table - every weapon but the Plaga Sample) says what
keeps levels and rounds; the discovery test checks it against the tables.

**The case's size is an item the case holds.** The sub-screen's set-up (FUN_007690C0) asks the item
manager for each of 0x7C..0x7F - Attache Case S, M, L, XL - in that order and sets SubScreenWk+2AA (and
+2AB) to 0..3 for each one held, so the last one held wins; with Ashley in play (pl_type 1) it sets both to
0. The panel treats those four as the case's own state, as the user put it ("not an item to be stored, it's
a status of the case", 2026-10-13): `items::is_case`, in no list, not offered by any Add, refused by Store
and remove; the size shows on the case's line.

### The item table and the Add list (`tools/gen_items.py` → `src/items.h`)

The panel files every item under a **bank** (the tabs of the Add list and of the item box: Weapons,
Grenades, Attachments, Ammo, Recovery, Key items, Treasures) and a **group** (the Add list's headings:
Handguns, Magnums, Shotguns, Rifles, Machine guns, Special weapons, Other weapons; Grenades; Attachments;
Ammo; Recovery; Key items, Maps, Files; Treasures, Gems, Bottle caps). Grenades and Attachments were
headings under Weapons until the user asked for item box categories of their own (2026-10-15); the banks
are one list for both, so the Add list has the tabs too. Attachments takes every part (type 9), the two
leftover ones not offered; Grenades the three grenades (the launchers are Special weapons, the knife an
Other weapon). `GROUPS` in the generator names each group's items in order; the rest of a group's types
follow by name. `rank` (and `kByRank`) is that order, and the item box sorts by it too. The generator
refuses an offered case item with no piece; the discovery test re-checks the filing, the two tabs, and that
no offered off-grid item has a piece.

The Add list's widget (`draw_picker`) serves four pickers, split by the piece so that each item is added
from one place only (the user, 2026-10-14: "We don't need the duplication"): **Add to the case** (inside
Attache case, `###add`, layout key `AddOpen`; "Add items", a section of its own, until then) offers what has
a piece - weapons, ammo, recovery, and the Plaga Sample (12, Assignment Ada's, type 1, filed with the key
items, so the list keeps a Key items tab for it alone); the key items', the files' and the treasures'
pickers the rest. A bank with nothing to offer gets no tab.

- **Bottle caps**: ids 220..243 are the shooting gallery's 24 caps, named after the figure on them (Leon w/
  Rocket Launcher .. J.J) - unprefixed, they read as a list of characters. 162 is the Bottle Caps
  collection (kind 21, a treasure to the game). The gallery's FUN_00842180 counts the caps held into a
  mask only while 162 is held; FUN_00842220 gives them back through `cItemMgr::get`.
- **The merchant's price lists**: 0xC16E90 (main game) and 0xC171A8 (Separate Ways), 6-byte entries
  (id u16, price / 10 u16, u16), 0xFFFF-terminated - the merchant's own order for weapons, ammo, recovery,
  gems and treasures (the Add list's treasures and gems follow it). Price 0: the Handcannon, the P.R.L. 412
  and a second Punisher (64) - seemingly the specials he hands over free. 254 (Tactical Vest) is in it at
  60,000, and in the table the stock builder (FUN_00714F50) passes FUN_00712900 for room 0x305 (0xC16D40,
  with a First Aid Spray) - not offered by the panel, not tried.
- **Leftovers not offered**: the guns with a part attached (36, 38, 40, 50, 81, 107, 108, 153, 171: no
  piece), 43 (re4_tweaks: lands in the key items as "Killer7 w/ Silencer", no icon), 63 (a Handgun
  silencer) and 34 (a part the game names "Punisher w/ Silencer"; re4_tweaks: a P.R.L. 412 scope), and the
  ones re4_tweaks found hang the game when examined (13, 255..261). 27 and 118 *are* offered: room scripts
  check for them (`cItemMgr::num` is FUN_007061D0; "was this item just used here" is FUN_00706380, on
  the manager's own `+8` - the id last used, set back to 0xFFFF as it answers - not a cItem's).
- `item_matches` (overlay.cpp): every space-separated word is part of the name, or is the item number.
  Before 2026-10-13 the picker used id 0 for "nothing picked", which made Magnum Ammo (id 0) unaddable.

### Checkpoints and continues

The checkpoint (FUN_0063C390, ~60 callers: the room set-up, room scripts, a save) copies the save region
and the items into the work at `[0xC59584]`; a continue after a death (FUN_0063D890, `kSigCheckpoint` is its
read of the death counts and play time) or a retry restores them (FUN_0063C140). The work keeps
self-relative pointers (`+0` its own recorded address, `+8` the save region's copy), so the copy's
frame counter (`+530C` in it) says when the last checkpoint was taken, and the live counter going back says
one was restored. The mod uses the copy for the save count editor only; the item box no longer follows
checkpoints (see below).

### The item box (`itembox.cpp`; Store / Take / count / throw away in `inventory.cpp`)

Until 2026-10-13 the panel had an "Item storage" built on RE0CabbyCodes' rules: a box per save file in
`RE4OGCabbyCodes.storage.ini`, written at each save, put back at each load, emptied for a new game, and
following checkpoints and continues. The user had it removed - they did not want the section changing a
box with the game ("I don't want this section to modify a cleared game box") - and asked for "a limitless
item box that is stored outside of the game in a file". So:

- **Only what takes room in the case** (the user, the same day: key items, files and treasures "aren't items
  that take up space in the attache and therefore shouldn't have options to store"): Store is on the case's
  rows only and `inventory.cpp` refuses an off-grid item. Off-grid entries already in a box (a file written by
  hand) still show, under a tab that appears only then, and still Take or throw away.
- **One box per game that saves**, each a file beside the DLL, read once in DllMain: the main game's,
  `RE4OGCabbyCodes.itembox.ini` (the box as it was before 2026-10-17, contents and all), and Separate Ways',
  `RE4OGCabbyCodes.itembox.separate-ways.ini`. No save, load, new game, continue or save file manager change
  touches either; only Store, Take, a count set and x, each written whole at once (temp file,
  FlushFileBuffers, MoveFileEx), to the box of the game being played. Don't add save-following back unasked.
- **None in Assignment Ada or the Mercenaries** (the user, 2026-10-17: "the virtual item box is available to
  the mercenaries mode and it should not be"; then "The item box should be a separate instance for game modes
  that allow saving your game, which I believe Separate Ways does ... if it's a short game like Mercenaries
  then it should be removed there as well"). `inventory::box_of(mode)` is the one rule: `Mode::kMain` the
  main game's box, `Mode::kSeparateWays` Separate Ways', anything else none (Flags_SYSTEM: 0x80000000
  Assignment Ada, 0x40000000 the Mercenaries; the log's "game started (The Mercenaries, Leon)" that day shows
  the mode read right in play). Without a box the panel draws no Item box section and no Store beside the
  case's rows - the case's own editor, Add to the case and the off-grid lists stay - and the tick refuses
  Store, Take, a count set and x with a note, moving nothing and leaving the files alone. Assignment Ada
  has no saves: a save comes back as the main game or Separate Ways only (see "The save file and the
  card"), so one made in Assignment Ada could not be resumed as it, and the game is known to have no
  typewriter there. "Just like we would do for a new save file game" was read as Separate Ways saving as
  the main game does - each game's box is still one for all of that game's saves and new games, not a box
  per save file.
- **No limit**: everything of one item is one entry (its counts added, no stack size); a weapon - an item
  that keeps levels and rounds, `items::keeps_state` - is an entry of its own. Take hands over one piece's
  (or entry's) worth at a time: PutInCase with up to the stack max, and with no room for a new piece what
  the pieces carried can still take (the top-up path). Lines: `id:count` or `id:count:0xLEVELS:rounds`, `;`
  name.
- **Counts** (2026-10-16, the user: "Add the ability to change the count of items in the item box"): every
  entry but a weapon's has a count field, 1..`itembox::kMaxCount` (999,999,999; no stack size), with Apply,
  Undo and a confirmation as the case's count has (a lower count says how many go for good). The edit is
  kept by item id (one entry per item) and let go when the entry's count changes under it; Apply posts
  `inventory::request_box_count`, and the tick's `itembox::set_count` re-checks the entry (index and all it
  holds, as Take and x do) and writes the file before the count changes. A weapon's entry is that weapon,
  with no count to set.
- **Levels and rounds for a weapon only.** Until 2026-10-15 Store took any non-zero `+6`/`+8` for a state,
  and for anything but a weapon those are leftovers (see "Items"): the user's box had Handgun Ammo, TMP
  Ammo, herbs, a Flash Grenade and the Rocket Launcher in entries of their own carrying the Handgun's
  0x1111/13 or the Rifle's 0x1101/7, and parts carrying their link - which a Take wrote back, linking the
  part to whatever sat at that index. Now Store asks `keeps_state`, `itembox::put` drops levels and rounds
  given for anything else, and the file's read folds such a line into the item's count (logged; the file is
  kept as `.bak` before its first rewrite, as for skipped lines). A part comes back as the game sets one
  up, on no weapon.
- **Never only in memory**: a Store goes into the file first and the case second (a refused erase is
  taken back out); a Take goes into the game first and the file second (a failed write leaves the item in
  both, never in neither). A file that is there but cannot be read leaves the box unusable for the run -
  nothing is written over it; lines that make no sense are logged and the file is kept as `.bak` before
  its first rewrite.
- The game keeps the case only when it saves, so an item taken out and then lost with the game (quit
  without saving, a continue from before) is gone from both; one stored and then lost is in both.

### The key items, the files and the treasures (`overlay.cpp`)

What lives off the case's grid has three lists of its own (`OffGrid`, `list_of()`): the key items and maps
(the item table's Key items bank but its files), the files (every item the game calls one, type 10 - the
discovery test checks they are the items under the Files heading; most of what a player holds is files, like
the Playing Manuals), and the treasures, gems and bottle caps (the Treasures bank). Until 2026-10-14 the key
items and the files were one list; the user had them split ("make key items and files into separate
sections"). Each shows what is held under the Add list's headings (none for the files: the section's own name
says it) with remove (x, "Remove") and, for what stacks, a count editor; and each has its own picker (the Add
list's widget, `draw_picker`, which drops its headings when what it offers is one heading's worth) offering
only what it can still take: a key item, map or file not held, a treasure or gem held below one entry's 999,
a bottle cap not held - what cItemMgr::get would add without dropping any. Add to the case offers none of
them.

Every Add is a sub-section inside its list's section (`subsection()`: one indent step in, folding on its own
and hidden with its parent; the user, 2026-10-14: "the add items sections should be subsections to their
parent associated container section"): Add to the case in Attache case, **Add key items** (`###keyadd`,
layout key `KeyAddOpen`) in Key items (`###key`, `KeyOpen`), **Add files** (`###filesadd`, `FilesAddOpen`)
in Files (`###files`, `FilesOpen`), **Add treasures** (`###treasuresadd`, `TreasuresAddOpen`) in Treasures -
the off-grid ones folded until opened. `draw_items()` lays the item sections out.

### The save file and the card (the save file manager)

All twenty saves are one Steam Cloud file, `savegame00.sav`, read at start-up into a buffer of 0xFFAA0
bytes (`cCard+4E8`, pointer also at 0x1222330) and written back whole after every save through
`ISteamRemoteStorage` (vtable `+0` FileWrite(name, data, size), `+4` FileRead, `+C` FileDelete, `+28`
FileExists, `+30` GetFileSize; FUN_0095F350 reads). The buffer: `+1E40` the SteamID (8 bytes), `+1E48` the
system block (pSys's 0x28 dwords, see "Unlocks"), `+1E74` a dword per file (in use - it is pSys+2C's
struct), and from `+1EE8` the files, 0xCAFC bytes each. A file: `+0` the head CRC (table 0x1221ED8), `+4`
version 0x402, `+8` the timestamp dwords (second, minute, hour, day, month, year, ...), `+34` GLOBAL_WK's
save region (so `S + 0x34 + (off - 0x4F90)`), `+3D` the difficulty copy, `+CAF8` the whole file's CRC. The
save builder is FUN_0095E0E0 (staging at `cCard+4D8`; the file is its `+2000`). It sets the file's `+0x30`
from `pG+4`: 2 with 0x20 (a chapter's end), 3 with 0x40 (a clear), else 1 - when it was saved, not which
game; the row drawer labels by it.

The card is started by four functions, each FUN_006C5C10(1, the card's task 0x4041D3, mode): FUN_00964150
(mode 0, the load list; from FUN_006E0810, FUN_0074AF20 and 0x6DC7F8), FUN_009641C0(.., flags) (mode 1, a
save to a file, `pG+4` flags from its argument; from FUN_006B3D00 - reached only through a table and taken
for the typewriter's handler, with no mode check, only a message 0xA6 case - the chapters' ends
FUN_006C2C00 / FUN_006C3660, and the endings FUN_007FE7E0 and FUN_007CAF60), FUN_00964260 (mode 1 with
`pG+4` |= 0x98 and no file of the caller's; from the extras' results FUN_00711B10 "St4ResultInitStage",
FUN_00740060 when an option has changed, and four more) and FUN_009642A0 (mode 2, the start-up check).
Which game a save is: a load (FUN_00960D70) and a continue (FUN_0063D890; FUN_0063D7E0 too) put the save
region back through FUN_0063C140, which sets Flags_SYSTEM's Separate Ways bit (0x1000) when the room's stage
(`+4FAD`) is 5 and clears it otherwise, and touches neither Assignment Ada's bit nor the Mercenaries' - so a
save comes back as the main game or Separate Ways only. With Assignment Ada's bit, the game's start puts the
player in room 0x405 (0x6D775E), the extras' stage 4.

cCard: `+4` routine (2 data select - the list, 3 load/save running), `+5` step (in data select, 1 =
waiting for the player), `+14` mode (0 load, 1 save, 2 start), `+1C` card slot, `+1D` the cursor's file,
`+44 + (slot*100 + i)*4` each file's status (bit 0 there, bit 1 broken). A delete clears the in-use
dword, the status word, the exists byte and the file's version; a copy copies the file's bytes (CRCs and
all - nothing in a file names its slot) and sets the three as the source's. Saves and loads are seen by
`savefiles.cpp` from the card: in save mode a file whose CRCs change was just saved; in load mode routine
3 with the cursor on a file is that file loading.

**A file's layout past the save region** (the builder, FUN_0095E0E0, from the checkpoint work): `+372C` 0x1204
bytes from its `+C`, `+4930` 16 bytes from its `+10`, `+4940` the room records (0xD8 each; the main game's
count/first at 0xC67564/0xC67568, Separate Ways' - the file's room stage 5 - at 0xC67566/0xC6756A), `+C7D0` and
`+C7D4` two more. **The CRCs**: CRC-32, polynomial 0x04C11DB7 MSB-first, init 0, no final xor (table built by
FUN_0095C450); the head's over `+4..+200`, the whole file's over `+0..+CAF8` (head CRC included). FUN_0095DB70
checks the buffer's SteamID (`+1E40`, 8 bytes, its CRC at `+FFA98`) against the user's and **deletes
savegame00.sav (FileDelete) when it does not match** - never touch that block.

**The map's treasures** (ss_map.cpp, 2026-10-19: the user had a treasure in their Separate Ways save marked
found by hand). FUN_00776400(room) gives the map (0..0x19; Separate Ways' 0x12-0x19, map_obj5a..5h; 0x18 is
rooms 0x518-0x51C, 0x19 none), FUN_0077C260 shows the treasures with that map's Treasure Map held (0xA9, 0x54,
0x55; Separate Ways' Mission 1-5 Treasure Maps 0x10B-0x10F), and FUN_0077D140 draws a star per treasure with a
cross over it when FUN_007775D0(i) says taken: **bit n of `pG+528C`** (FUN_006B3240: dword n >> 5, bit 0x80000000
>> (n & 31)), n from a per-map list built on its stack (map 0x12: 7B-7E; 0x13: 80-84, 7B, 96; 0x14: 85, 87, 86;
0x15: 8A, 8B; 0x17: 8C, 8D; 0x18: 8F-92; the count is the map model's). The rooms' ITA (the unpacked
`St5/r5xx.udas`: "ITA\0", u16 version 0x105, u16 count, then 0x9C-byte entries from +0x10, each an area
trigger's memory) holds each item: kind 3 at `+35`, its number `+36`, the item record at `+5C` - its place
(floats), `+78` the item id, `+7A` its flag in `pG+528C`, `+7E` else its flag in the room's record `+8`
(FUN_006B5820 tests, FUN_006B5770 sets - the pickup's one lasting change). Mission 5's: 8F Blue Eye (r518), 90
Red Eye (r519), 91 Green Eye (r51B), 92 Butterfly Lamp (r51C). Done by hand: the bit set in the file's region
(`+34 + 0x2FC + (n >> 5) x 4`) and the whole file's CRC rewritten, the game closed and the save backed up.

### A save turned away (Separate Ways' clear save; a fix of the game's, always on)

The user, 2026-10-19: "it appears there is a bug in the base game where beating separate ways sometimes doesn't
properly allow you to save your game for a clear file. Make this a native fix in the mod". Their log that day: the
clear save's screen up 4.3 s with no list (no panel), then the title - the pattern of every save of the system
data alone (the Mercenaries' results, Assignment Ada's end: up, ~4.2 s, gone) - and no save screen at the results
before it. Steam's discussion of the game (app 254700, threads from 2014 to 2026) has the same: "Save?" answered,
no file list, no clear file, got round by replaying the ending.

- **The requests.** FUN_009641C0(param, flags), a save to a file - the typewriter (flags 1), the chapters' ends (0xA)
  and the endings' clear save (0x12): pG+4 |= 8 (flag 2), 0x10 (4), 0x20 (8), 0x40 (0x10: a clear, the file's
  `+0x30` 3), 0x98 (0x20: no caller), pG+4FA2 = param, then FUN_006C5C10(1, the card's task 0x4041D3 -
  FUN_00963FF0 -, 1) and a frame's wait. FUN_00964260, a save of the system data alone - the Mercenaries' results
  (FUN_00711B10), an option changed (FUN_00740060), FUN_007CD3E0, Assignment Ada's end (FUN_007E4EE0), the title's
  options (FUN_009E66B0) and the endings' records (FUN_009E6F30): unless `[0xC63A57]` (never set in this build)
  pG+4 |= 0x98, the same start. The load list (FUN_00964150, |= 0x100) and the start-up check (FUN_009642A0) hand
  the same task to level 1 too: four places, all level 1 (check_sigs).
- **The scheduler** (scheduler.cpp): its levels at 0xC67D40, 0x348 bytes each - `+4` the state (0 free, 1 starting,
  2, 3 running; | 0x80 held by FUN_006C56F0's timer), `+7` 6, `+14` the argument, `+338` the wrapper (FUN_006C5400),
  `+33C` the task. FUN_006C5DC0 runs levels 0-5 each frame (0x654BDE, after the mod's tick at 0x654AEB), FUN_006C5CD0
  level 6. Level 0 is the game itself (the Rno0 dispatcher FUN_0063EA20, put in by FUN_006C5550), whose MainLoop runs
  the scene's task lists (ScenarioMove; levels 8-0x13, FUN_006C4D10(0x14, ...) taking the first free from 0x13
  down). FUN_006C5C10(level, task, arg): "TASK DON'T EXEC" (FUN_00974020/30, both empty) and 0 while the level's
  state is not 0. Level 1's other tasks: the movies without 0x400 (FUN_006CE630/790 -> FUN_006CE4E0/580: the endings'
  ending.sfd and adaend_*, Separate Ways' r332s41.sfd; with 0x400, the events' movies, level 3), the game over
  (FUN_0063E3C0 -> FUN_0063DA70), the sub-screen (FUN_007691D0 -> FUN_00767C80, which handles a refusal: FUN_00767C20
  and its flag put back), a door's and AT kind 15's messages (FUN_006B8000, FUN_006B4110), the TV-format task at
  start-up (FUN_006EEA30). The movies, the card and the sub-screen hold level 0 (FUN_006C56F0(0)) while they run.
- **Nothing takes the flags back** when the start is refused: pG+4's bits 3-30 are cleared only by the card's end
  (FUN_0095E920, routine 4's step 2: &= 0x80000007) and its clean-up (FUN_0095C5A0). Every access to pG+4 is in the
  card's code (0x95B000..0x965000; bits 0-2 are the start-up check's 1 and a load's 4). With 0x80 the card's check
  (FUN_00962500, routine 1) goes from step 7 to step 8 and routine 8 - FUN_00961310 entered there: "bh4_system",
  the system data alone, "Saving..." for 3 s (GetTickCount), message 0x28, routine 4 - instead of the list (routine
  2).
- **Separate Ways' end**: area trigger 0x91 of room 0x51E (FUN_007CBA80: r51es10, r332s41.sfd once - the room's
  record bit 0x200000 -, r51es20, then FUN_007CAF60 as a task). FUN_007CAF60: achievement 12, the ranking
  (FUN_009E6F30, ecx 0x127E100: the best clear time into pSys+0x7C+i*4 - always in Separate Ways, stage 5; in the main
  game on Normal and Professional only - then FUN_00964260, then the ranking file), the results (ada_res.dat), the
  ranking's upload, the unlocks (Ada's Report 0x10000, the Typewriter 0x10000000, the gangster costume 0x80000, the
  clear count), "Save?" (message 0x8F, then 0x1810051, "are you sure"), FUN_009641C0(0, 0x12), a frame,
  pG+54 |= 0x4000000 (to the title). The main game's end (FUN_007FE7E0) is the same. The records' save turned away
  leaves 0x98, and the clear save after it saves the system data alone: no list, no clear file.
- **What held level 1** at Separate Ways' results was not found from the code: a movie's task (and the sub-screen's)
  frees level 1 in the same step as it lets level 0 go, so the ending's task never runs beside one, and no card
  screen was in the log.
- **The fix** (`Site::kSaveTurnedAway`, a patch of the game's code with no switch: in once at the first tick, from
  cheats.cpp's `put_in_fix`, out only as the mod unloads; kSigSaveSystem, kSigSaveFile, kSigTaskStart). Where
  FUN_006C5C10 turns a task away it called two empty debug prints - 34 bytes at **0x6C5C27**, from `push ecx` (the
  level) to `add esp,14`, eax still the level's offset (level x 0x348), then `xor eax,eax; pop ebp; ret`. In their
  place: `cmp dword [eax+levels+33C], card's task; je` (a save already there clears its flags at its own end);
  `cmp dword [ebp+C], card's task; jne` (another task turned away); `mov ecx,[pG]; and dword [ecx+4],80000007` - as
  the card's end clears them. It runs only when a task is turned away; nothing runs in a frame for it. The first
  tick is between the game's frames, before its first, so no code of the game's is running in the bytes as they
  change (a task is never inside FUN_006C5C10 across a yield: it has none). The turned-away save is lost, as before
  (the clear save, or the next save, writes pSys whole). Until 2026-10-19's second look it was a check every tick
  (clear the bits with no card up and level 1 not holding the card's task); the user found a per-frame check for a
  once-an-ending bug too much. Run offline: the discovery test - the game's own FUN_006C5C10, the fix out and in,
  turning the card's task away from a level 1 held by a movie, and the card's own check (FUN_00962500) on a
  stand-in card: without the fix pG+4 9D, then DD and the system data alone (routine 8); with it 5, then 4D and the
  list (routine 2); nothing cleared with the card's task starting or running at level 1, or another task turned
  away; the clear save's start the game's.

### Unlocks (SYSTEM_SAVE_WORK +4) and the clear count (GLOBAL_WK +4F9E)

**Not used by the mod.** The panel had an Unlocks section built on this (a switch per bit that gates
something, and a Times cleared editor); the user had it taken out on 2026-10-13, so don't add it back
unasked. The findings stay here.

pSys (0xC65A60, 0x9C bytes) holds `+0` flags_CONFIG, `+4` **flags_EXTRA** (the unlocks, highest bit
first), `+8` language, `+A` brightness ..., `+10..+24` the Mercenaries' scores, `+28` the "new" badges the
title's menus draw, `+2C..` the files' in-use dwords. **Every save** copies 0x28 dwords of it into the
buffer's `+1E48` (`rep movs` at 0x95E629), so all files share one set; **a load ORs** the file's word into
it (0x960E74..: `or [pSys+4],[buf+1E4C]`, `+28` likewise) - a load never takes an unlock away. The
start-up check - the card's mode 2, started by FUN_009642A0 from the title sequence (FUN_006DFDF0, state
0x17 → 0x1D), whose routine 0 is FUN_0095FD30 (the dispatcher FUN_00963E00 calls the table at 0xC2C998 by
mode + routine × 3; modes 0 load and 1 save have other routine-0 functions) - copies the defaults
(0xC5A5D0) into pSys before reading the file and ORing it in. WinMain's restart loop copies pSys+4 into
`[0xC5A680]+24` (FUN_00652EA0, 0x652F26) just before restoring it from there (0x654620), so an in-memory edit
survives that loop. Until a save writes it, an edit made in memory alone is fragile: a load brings back a
bit that was cleared, and the start-up check rebuilds the word from the defaults and the file. Whether a
return to the title re-runs the start-up check has not been seen.

What each bit gates, from its readers:

| Bit | re4_tweaks name | Gates | Granted by |
| --- | --- | --- | --- |
| 0x80000000 | EXT_COSTUME | the title menu's Extras entry (FUN_006D9FB0 maps the menu with it; FUN_006DA530, a menu of 1 or 2 entries, also reads it) and Special costume 1 | the main game's ending (FUN_007FE7E0) |
| 0x40000000 | EXT_HARD_MODE | Professional at a new game (FUN_006D6010) | the main game's ending on Normal or Professional |
| 0x20000000 | EXT_GET_SW500 | the Handcannon (0x37) in the merchant's stock | - |
| 0x10000000 | EXT_GET_TOMPSON | the Chicago Typewriter (0x34) in the stock | FUN_007CAF60 (`r51e.cpp`, an ending with `clears++`) |
| 0x08000000 / 0x04000000 / 0x02000000 / 0x01000000 | EXT_GET_ADA / HUNK / KLAUSER / WESKER | the Mercenaries' character select (FUN_006D6EC0 icons 1..4: Ada, Krauser, HUNK, Wesker; FUN_006DBA73) | - |
| 0x00800000 | EXT_GET_OMAKE_ADA_GAME | **nothing reads it** | the main ending; FUN_00703850 (`eye_of_ada.cpp`) |
| 0x00400000 | EXT_GET_OMAKE_ETC_GAME | entering the Mercenaries **without it wipes its scores** (`+10..+24`, FUN_006DB383) | the same two, zeroing the scores the first time |
| 0x00200000 | EXT_ASHLEY_ARMOR | nothing found reading it | - |
| 0x00100000 | EXT_GET_PS2_ADA_GAME | **nothing reads it** | the main ending |
| 0x00080000 | EXT_COSTUME_MAFIA | Special costume 2 at a new game; Leon's Mercenaries costume (`+4FC9 = 4`, FUN_006DBA73) | FUN_007CAF60 |
| 0x00040000 | EXT_GET_LASER | the P.R.L. 412 (0x41) in the stock | the main ending on Professional (via FUN_00741780) |
| 0x00020000 | EXT_GET_ADA_TOMPSON | Ada's Chicago Typewriter (0x53) in Separate Ways' stock (FUN_00714D80, 0x714E99) | 0x7E509F |
| 0x00010000 | EXT_GET_ADAS_REPORT | Ada's Report, the Extras menu's fifth entry (FUN_006DA690) | FUN_007CAF60 |

The Extras menu (FUN_006DA690) always lists its four modes once it is there; only Ada's Report is gated, so
the three Extras-mode bits (0x00800000, 0x00400000, 0x00100000) are no switches for those modes. Twelve bits
gate something (0xFF0F0000). The merchant's stock is built by FUN_00714F50 at each room set-up
(FUN_0063C830): in the main game (Flags_SYSTEM without 0x1000) the special weapons are added only when
`game_cnt != 0` - Matilda (3) and the Infinite Launcher (0x6D) always then, the Handcannon, the Typewriter
and the P.R.L. with their bits; Separate Ways' stock (FUN_00714D80) adds Ada's Typewriter with its bit and
`game_cnt > 0`. `game_cnt` is incremented (cap 99) at the endings (FUN_007FE7E0, FUN_007CAF60,
FUN_008C66A0, 0x990780), kept by a continue (FUN_0063BF10), and read by ~20 scene scripts (replay
differences).

### Signatures

`src/sigs.h` is the one table (107 entries), read by `game.cpp` and by `tools/check_sigs.py`, which also
cross-checks what they derive (the slots agreeing between sites, the patch sites' bytes, the tick's return
address, the charge table's Handgun and Matilda rows). Each entry says what its operands are. Add a
signature there, never an address in code; run `make sigs` and the discovery test after any change.

## What is not done yet / unverified

Nothing has been run in play yet: the headless run reached the title screen (proxy, every signature,
tick, overlay, input hooks), and the rest is the user's first sessions. In particular:

- The chapter byte's mapping in the save list (1..19 → 1-1 .. the Final Chapter, 20+ Separate Ways) is
  assumed from its range, not seen. The game's own row drawer (FUN_0095CFF0) splits it with FUN_006BF7A0
  and picks its label by the file's `+0x30` (1, 2 - chapter - 1 - or 3): the place to settle it.
- Save without counting and the save count editor (2026-10-13): the site and the list's reading are from
  the code, not yet seen in play. Likewise Infinite money, Misses don't count and Deaths don't count, and
  the layout file's prompter writing.
- Whether the game's load/save list redraws a deleted or copied file at once (it is drawn from the exists
  bytes and status words the mod sets) or only when the list is reopened.
- The mouse release (Unacquire + refused Acquire) under Proton/gamescope, and the panel's own pointer.
- PutInCase / cItemMgr::get / erase called from the pause: the case screen is not up then; the calls
  only touch the item manager, but the case screen's own state has not been seen afterwards.
- The item box and the key items / files / treasures lists (2026-10-13): run offline only -
  `tests/test_itembox.cpp` (the real itembox.cpp and inventory.cpp against a fake case that does what
  PutInCase and cItemMgr::get were read to do) and the panel in the software-rasterised harness - not yet in
  the game. Adding a key item or file through cItemMgr::get from the pause is the call the Add list made
  before. The old `RE4OGCabbyCodes.storage.ini` in the user's Bin32 is no longer read (it held no items).
- The item box's weapon-only levels and rounds, and the fold of the old lines (2026-10-15): from the code
  (FUN_00707780, the trigger, erase), run offline (`test_itembox`'s leftovers phase, a fake case that
  leaves an entry's `+6`/`+8` to its next occupant; the panel drawn from a copy of the user's box file) -
  not yet in the game. Where a part sits on the case's grid while attached, and after its weapon is stored
  (erase unlinks it and leaves it in the case), has not been looked into.
- The item box's counts (2026-10-16): run offline only - `test_itembox` (a count set and read back, held to
  1..kMaxCount, a stale request, a weapon's, a file that cannot be written, the same count, a Take from the
  new count) and the panel drawn and driven under Wine (the real overlay.cpp, scripted ImGui input found
  through the test engine's item hooks, a software rasteriser: typing, Apply, the confirmation, Cancel,
  Undo, each tab's height, not paused) - not yet in the game.
- The boxes per game (2026-10-17): run offline only - `test_itembox`'s modes phases (which box each game
  uses; Store, Take, a count and x refused in Assignment Ada and the Mercenaries with both files untouched,
  the case's own count and x still working there; Separate Ways' Store and Take on its own box and file and
  the main game's on its own, each leaving the other's alone; both read back by a new start) and the panel
  drawn under Wine in all four modes (each saving game's own box; no Item box section and no Store in the
  other two) - not yet seen in the game. That Assignment Ada has no typewriter is from the code (above), not
  seen; Separate Ways' saves are, by the code, the main game's with the room in stage 5.
- The costume descriptions (Special 1 / Special 2) are from the game's known costumes, not seen here.
- A save turned away (2026-10-19, "A save turned away"): from the code and the discovery test only - not yet seen at
  the end of a game. What held the scheduler's level 1 as Separate Ways' results came up is not known (the fix acts
  in the game's own code, and logs nothing when it does).
- Coexistence with re4_tweaks in play: it hands the game an IDirect3D9 and device of its own around the real
  ones (from the call site, D3D9Hook.cpp - the real ones carry the mod's hooks, so both panels should
  draw), and its default frame limiter takes the tick's first point away (the second ticks; offline only).
  Anything on Windows ("Windows").
- The Add list (2026-10-13; Add to the case, case items only, since 2026-10-14) was drawn and driven
  offline only - the real `overlay.cpp` under Wine with scripted ImGui input and a software rasteriser - not
  yet in the game. What the game does with
  another character's weapons (Ada's in Leon's case) or a bottle cap without the collection is not seen.
- Automatic QTEs (2026-10-14): from the code, run offline (the discovery test calls the game's own
  checkButton and shake-off with the patches in and out) and put in at the title headless - no QTE seen in
  play yet. The scenes' names are re4_tweaks' where it hooks the same poll, else from the code around them;
  em3a's grab is unidentified, and which prompts the Mercenaries, Separate Ways and Assignment Ada show is
  not listed (the prompt check and the shake-off are shared). Untried beside re4_tweaks. First seen in play
  (2026-10-17): the statue chase's run failed - a press every frame is worth nothing there at 60 fps; the
  run's speed site (above) is from the code and the discovery test, not yet seen in play.
- Steady aim (2026-10-14; the stance 2026-10-15): from the code, run offline (the discovery test calls the
  game's own sway function on a stand-in weapon with the patches in, out and back, runs each spread site's six
  bytes, the game's and patched, the game's own laser line with the hold in and out, and the game's own motion
  clock on a loop with and without the stop, with the stance's tick between) and the hold's decisions over
  made-up poses - not seen in play. The hold keyed on the routine's steps (2026-10-15, after the user's TMP
  report): run offline (the discovery test checks the routine chain in the exe and reads the routine bytes off
  a stand-in player) - not yet seen in play. A burst fired as the aim moves, or the moment the gun comes up, is
  still the game's (the dot sits on the fire motion's line). That the idle aim's breathing is all in the stopped loop's keyframes (and
  not, say, in the waist or the neck) is from the code: the motion data is in the game's archives, not read.
  Whether the scope's view sways with the idle stance (its eye and target come from the scope object, not
  looked into) is open; the hold's thresholds (0.25 degrees a frame, 4 frames) are from reasoning, not
  measured; which weapons each fire routine and each aim's set-up serves is from their classes' code and
  names; untried beside re4_tweaks.
- The kills that go round LifeDownSet2 (2026-10-15, "Damage"): from the code. Run offline: the partner's
  heavy hit (the discovery test calls the game's own cSubChar::damageCheck on a stand-in partner with a
  type-13 hit near and far, the patch out, in and out again: the fall and 0 HP unpatched, ducking patched);
  the other eight sites checked byte for byte and by where their jumps and calls go (check_sigs, game.cpp)
  only. None seen in play: that the catapults' shots are these heavy hits (from the room 0x200 case in that
  branch), what the knock-down looks like after a rock or the truck, which attacks besides the Ganados' row
  0xC carry the kill flag, and how ducking reads where the game expected the fall.
- One hit kills on El Gigante (2026-10-16, "Damage"): from the code, and run offline - the discovery test
  calls the game's own hit check (FUN_004B07C0) on a stand-in El Gigante (a handgun, a rocket, a flash and an
  incendiary, on the body and on the parasite, in its damage routine and holding someone; the patch out, in
  and out again; the effects and sounds stubbed) and runs the slash's 8 bytes on their own. Not seen in play.
  Not looked into: El Gigante dying with Leon on its back (the climb's step follows El Gigante's routine; the
  game gets there too, with an explosion on the parasite mid-climb). Other bosses were not surveyed; health
  arithmetic of their own is in Verdugo's code (FUN_004CA170), in FUN_0052A2B0 (between Mendez's and the
  Regenerador's inits), and in some objects' and room scripts' - whether a hit on them goes round
  LifeDownSet2 is not known.
- One hit kills on Krauser (2026-10-17, "Damage"): from the code, and run offline - the discovery test calls the
  game's own hit check (FUN_00548200) on a stand-in Krauser (types 1, 2 and 3; a handgun on the body, the head
  and the armoured arm, a flash; one already flinching; in room 0x31C and outside it; the patches out, in and out
  again; the effects and sounds stubbed). Not seen in play: that the first part's Krauser, at 1 health, retreats
  at the end of each flinch as read (the flinch routines' share test sits behind conditions of their own: an
  animation flag at `+28B`, `+AD0`'s jump point, `+ACC`), how the room's tasks take a retreat that comes at once,
  and the other lists' Krausers (Separate Ways' in 0x51C, PS2ETC's in 0x31C, omake00's in 0x40E).

- The shooting gallery (2026-10-15): from the code, and played through offline only (the discovery test:
  the game's own scoring on a stand-in work, `gallery()`/`gallery_hit()` on a stand-in pool, and the eight
  scripts under the rules as read). First used in play 2026-10-17, at 60 fps: it hits the targets, but the
  user asked why it "sometimes not shoot targets right away" - the streak-of-four hold ("Dead ends"), taken
  out that day. Not seen in play: that a hit handed over this way scores and draws as
  a shot's does (the effects, the popup, the target falling), the props' slide and which side is "out",
  whether a target half way up looks hittable, the +300 secret itself (and what the lights' prop does), how
  the gallery's game ends with the bonus timing as modelled, and the special cap.
- Bonus time always on (2026-10-18): from the code, and run offline (above) - not yet in a game.
- The countdown editor writing the option screen's copy (2026-10-18, "The countdown"): from the code and the
  discovery test - not yet seen in play.
- The hourglasses' Ganados swapped into the area lists' places (2026-10-18): from the code and the stand-ins only;
  that the ITA's first three entries are the triggers 80-82 is read from the file's layout (type 3 and n at
  +0x31/+0x32), not seen. Not seen: a swapped-in Ganado coming from where its event's would (its place is the
  event's), and the house Ganados' "set" behaviour away from their event.
  Seen in play since (2026-10-18): all 11 swapped in within 16 s of the start, and killed, in two games.
- The castle (2026-10-18, "The castle"): its areas in turn, its sweep hurried, the give-back and the gates are from
  the code, the checker and the stand-ins only. Not seen: a gate's event run from the widened trigger in play (the
  scene, the objects sliding, the Ganados it sends), how the action button's event looks without its prompt, the
  pace with the areas in turn, whether a boss's send is still ever refused, and what the twelve entries nothing
  sends are for.
- The water stage (2026-10-18, "The water stage"): its zones' last five at once and its spawn areas the cheat's are
  from the code, the checker and the stand-ins only. Not seen: the stage's areas widened round the player in play,
  a zone's reset Ganados heading for him from their own places, how fast it goes there, and the theoretical gap - an
  action button's callback putting the player on a ladder (0x63E646, before the tasks) in the very frame a one-shot
  area or the eighth fires would lose its sends (a zone's are only skipped, and come later).
- A chainsaw refused as its wave went, swapped into an area list's place (2026-10-18): from the code and the
  stand-ins only. Not seen: the chainsaw coming from its own place (the wave would have moved it to a chainsaw
  spot), and what fills the pool beyond two slots a Ganado about and one a body (the weapons that outlive their
  Ganado are the guess).
- The second round (2026-10-18): the fall cut short, the melt hurried, the events let send and the slots counted
  are from the code and the stand-in village only. Not seen: how a Ganado looks cut from its fall to its melt
  (die 3 does not move its motion: it may sink from mid-fall), whether the weapon's slot does go with its life (the
  count of 52 against those about says so), the events sending past ten, and the chainsaws in a full game.
- The village's stall (2026-10-18, "The Mercenaries"): the census showed entries spent on Ganados that never lived;
  the set-up failing on the models' pools is from the code (the only path from a new Ganado to its destruction
  with its entry sent and not dead), not seen - the next log's pace lines carry the pools. The plan (mercmem.h),
  the re-sends and the bodies hurried are from the code and the discovery test's stand-ins only: how many records
  and parts a Ganado takes in fact (its weapon's?), the room's own pool sizes, and how the chainsaws' and the
  pickups' events (one-shot: a spent entry of theirs is not sent again by anything) fare are not seen. The hit in a
  reaction (routine 2) is from the code and the discovery test only.
- The Mercenaries' two switches (2026-10-17, "The Mercenaries"): from the code, and run offline (the
  discovery test: the game's own scoring and HUD with Endless combo's patches out, in and out, at 30 and 60
  fps; the game's own em10DmCk on merc_kill()'s records over a stand-in pool, ten of them in one frame: combo 10,
  each step's bonus, 2370) - first used in play that day (the village, 60 fps): the kills scored, and the spawns
  stalled at 62 standing still (the areas, above). Every area up (kMercAreas) is from the code and the
  checker/discovery test only - not yet seen in play. Not seen: the combo's gauge and the "+N" with the timers held (the HUD's display state was
  stood in for); whether the finish display outlasts a "+N" at the game's end; how a kill looks where it
  happens (out of sight, on a ladder, mid-entrance); whether a Ganado ever stays about its business after the
  hit (a state that swallows hits, retried every 15 ticks); the stages' waves with every enemy killed as it
  spawns; a Plaga bursting out of a killed host in the castle and the island (the same enemy, scored when it
  dies); whether a Plaga that leaves its host as a creature of its own (cEm25?) appears in the Mercenaries -
  it would not be killed (another class) and scores nothing.

## Dead ends worth not repeating

- **Reading the Mercenaries' "stall" as a cap or a full pool** (2026-10-17): two rounds of reasoning about the
  ten-about cap and bodies filling the enemy pool, from the code alone, found nothing that stops a stage for
  good. The census in the user's log answered it at once: the room's list had been spent (sent, no enemy, not
  dead) while nothing was about and the pool had room. Count the list's entries against the kills before
  theorising about what holds a spawner back.

- **The systemVSyncPost call site (0x65515E)** looks like the perfect tick (an empty call once a frame on
  the game's thread), but re4_tweaks patches it; two mods on one call site is a fight. The
  SetThreadAffinityMask import, keyed on its return address, is free.
- **PeekMessageA is no tick here**: it runs on the GX render thread, not the game's.
- **"Nothing reads it" from a grep of `pG+off` alone** dropped the save count wrongly once: the load and
  save lists draw from the files' copies in the card's buffer (`S + 0x34 + (off - 0x4F90)`), not from
  GLOBAL_WK. Search the card code (0x958000..0x966000) for the file offset too before calling a field
  unread.
- **awk address ranges over `ghidra_proj/bio4.asm`** with an end address that is not a line in the file
  ran to the end and dumped 41 MB; compare numerically (`strtonum`) or grep exact addresses.
- **`cObjWep+354` bit 1 as "the laser is drawn"**: FUN_0067DE90 sets it in the player's move as it draws
  the laser, and the weapon's move (FUN_0073F320, vtable `+10`) clears it again later in the same frame - it
  sets it only for bit 0, a request no weapon code makes. The object manager (0xC076C0, FUN_006643B0; the
  player's weapons are its objects - FUN_00684A00 destroys them through it) runs after the player's move in
  the MainLoop (its profiler marks: "EmMgr.move", "Player", "ObjMgr.move", "CameraMove"), so the tick never
  sees the bit. Steady aim's hold waited for it from 2026-10-14 and never went in. Ask the player's routine
  instead (r_no bytes, "The hold").
- **Holding the gallery's last target at a streak of four** (gallery.h, until 2026-10-17): the hit that
  makes five starts a bonus window, and with nothing left standing the script fast-forwards its next wave
  into it, where the window's holds keep them waiting - so the last target was kept up. But the window
  starts whenever that hit comes. The hold ended when another target came up (in the window then anyway)
  or when the held one was about to leave (nothing standing, the script fast-forwards into the window as
  before). In the play-through it left as many targets in the windows or more (B2, D1), made every
  last-moment hit (1-5 a game, none without it) and held targets up to 32 s (D1): a game took 16-81 s at
  60 fps instead of ~12. The window's own hold (until its bonus target shows) does work - without it A2 at
  30 fps loses its sixth bonus - because the bonus target then keeps the clock running.

## Tooling notes

- `ghidra_proj/` (gitignored): the analysed project `RE4` (bio4.exe imported straight from `Bin32` - it is
  not packed) and `bio4.asm`, a full disassembly
  (`i686-w64-mingw32-objdump -d -M intel --no-show-raw-insn bio4.exe > ghidra_proj/bio4.asm`, 76 MB):
  "who touches this global" is a grep. `tools/ghidra/run.sh dec|xref|funcs|strings ADDR...` runs
  `tools/ghidra/Report.java` headless against the project (~5 s; the answer also lands in
  `ghidra_proj/report.txt`). Every function is reached through an incremental-link `jmp` thunk at
  0x401000..0x410000, which Ghidra names `thunk_FUN_...`; `game.cpp`'s `call_target` follows them.
- `tools/check_sigs.py` maps the exe as an image (pure stdlib) and checks `src/sigs.h`; `tools/gen_items.py`
  writes `src/items.h` (272 items: kinds and pieces from the exe, names written out - the game's text is in
  archives the mod does not read - the panel's filing, and the exclusive upgrades); `tools/gen_proxy.py`
  writes the proxy's export list.
- `tests/test_discover.cpp` runs `game.cpp`'s discovery over the real exe under Wine (it applies the
  relocations itself), and exercises the item rules on the static data.
- The game's data files (`BIO4/**/*.lfs`) are RDLX: `RDLX`, 0xFEEEBAAA, the size at +8, the chunk count (u16)
  at +10, then a chunk per 8 bytes from +14 (u16 packed size, u16 size, u32 offset from +14 | 1 when packed),
  packed with XMemCompress (LZX, its 5-byte frame headers). FUN_006AC5F0 unpacks them with the exe's own
  decompressor, stdcall: FUN_00A9B92E(0, 0, 0, &ctx), FUN_00A9B862(ctx, dst, &size, src, packed size),
  FUN_00A9B90A(ctx). A scratch program that maps bio4.exe as the discovery test does, binds kernel32's imports
  (its heap) and calls those three unpacks them under Wine (2026-10-17; not kept). The enemy lists
  (`BIO4/Etc/emleon00..09.esl.lfs`, `omake00..09.esl.lfs`) are 255 or 256 EM_LISTs of 0x20 bytes (re4_tweaks'
  em_set.h: +0 be_flag, +1 id, +2 type, +3 set, +8 health, +18 the room); GLOBAL_WK holds the one in play at
  +5410. re4_tweaks' names for them: ST1_0 .. ST3_1, DUMMY, BIO5 (emleon), ADA, ETC, ETC2, PS2ST1..5, PS2ETC.
