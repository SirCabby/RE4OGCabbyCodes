# RE4OGCabbyCodes

A cheat panel for **Resident Evil 4** - the 2005 original, as the *Ultimate HD Edition* on Steam (app
`254700`, `Bin32/bio4.exe`). Pause the game - its option menu, Start on a pad - and a panel appears beside
it:

- **God mode** - no hit takes any health off you: every hit goes through the game's one damage routine,
  and the player's subtraction there is taken out. The hits made to kill you outright instead (an attack
  meant to kill in one blow, a falling rock, the truck) knock you down. Your health is held at its maximum
  too, so what you had lost when you switch it on comes back. You still flinch and get knocked about, and
  grabs still play out; a grab or a scene that ends in a death of its own (a fall, a missed QTE) is left to
  the game.
- **Ashley takes no damage** - the same for Ashley, in the main game: no hit takes any of her health, and it
  is held at its maximum. What would kill her outright does not: an explosion close by (the catapults' shots
  in the castle, a grenade, dynamite) or the truck makes her duck, as an explosion further off does, and El
  Gigante's stomp and a falling rock leave her be, as they leave her armour. Being carried off is not damage
  - the game ends that its own way.
- **One hit kills** - any hit that does damage takes all of an enemy's health, so the game's own code kills
  it - deaths, drops and parasites as usual. El Gigante, which the game hurts only through its parasite,
  falls to the first hit anywhere, as it does to a rocket (a flash grenade still only blinds it). Krauser
  cannot die in the first part of his fight (the game keeps him alive until it moves the fight on): there the
  first hit with damage makes him flinch and retreat, as he does once he has taken enough. Mutated, he falls
  to the first hit, even on the arm he guards with. Any other enemy the game keeps alive for now (a scripted
  moment) keeps its last point, and the game decides.
- **Infinite ammo** - firing uses no ammo: the gun in your hands keeps its loaded rounds, and the bowgun and
  Krauser's bow use no bolts or arrows. Reloads and pickups work as usual; grenades and the rocket launcher
  are used up as the game uses them.
- **Steady aim** - your aim holds still, and your shots go where it points. The game makes an aim you hold
  wander (the laser sight's dot, or the view through a rifle's scope, drifts about where you point it, by up
  to a few degrees), throws it a little at a shot, sends each bullet a little off it at random, and draws the
  laser and the bullets from the gun in Leon's hand, which sways with his idle aiming stance and kicks at
  every shot. With this on, Leon holds his aiming stance still, so the gun points where the laser does; once
  you stop moving the aim, the laser and every bullet stay on that exact line - through the recoil too -
  until you move it again. Handguns, magnums, machine guns and the mounted gun shoot straight down it. A
  shotgun keeps its spread of pellets, centred on the aim, and a thrown grenade or egg gets no random
  sideways push. Moving the aim works as before.
- **Automatic QTEs** - the quick time events play themselves. A prompt to press buttons in time - dodging
  Krauser's knife or a boss's blow, the boulder, the statue's swings, the lasers, Saddler's bridges - succeeds
  the moment it comes up, and you do what the buttons would have done. Where a prompt wants presses as fast as
  you can - running from the boulder, swimming back to the boat, climbing up, the knife in El Gigante's
  parasite, the end of Krauser's knife fight - the game sees a press every frame; running from the statue,
  Leon goes at full speed from his first step, and a grab is shaken off at once. The prompts still flash up.
  Turning a crank is no QTE, and is left to you.
- **Automatic shooting gallery** - the merchant's shooting gallery plays itself for the most points it
  gives. Start a game there and let it play: every Ganado target is hit in the head (100 points, 200 for the
  special ones, 100 for the one carrying a bomb - never the bomb), the Ashley targets are never hit, and
  every bonus target that five hits in a row bring on is hit too (500). It keeps the hits in step with the
  streak so that no bonus target is lost, and where the gallery offers game C or D it also hits the two
  props that are worth 300 more, at the moment that costs no streak. The targets fall as if shot, with
  their points shown; no shots are fired, so your gun does not move. Leave your gun be while it plays: a
  shot of yours that misses ends the streak, as it always does. The most each game gives, as the mod reads
  the game's code: every target's head (3000 points; 3100 in game D before you hold three of its caps), six
  bonus targets (3000) and, where the gallery offers game C or D, the props' 300 - **6300** there (**6400**
  for that game D), **6000** where it offers only A or B.
- **The Mercenaries** - three switches of their own, shown only while you play the Mercenaries, with the
  combo, the kills and the score beside them:
  - **Endless combo** - a combo never runs out of time: every kill counts as the next one in it however long
    since the last, so its bonus keeps climbing to its most (from the tenth kill in a row on) and stays
    there. The game puts what a combo has earned onto your score only once the combo is over; with this on,
    it goes on as it is earned instead, each bonus shown as it comes. The Bonus Points pickups work as
    before, and switching it off lets the combo run out as usual.
  - **Kill enemies as they appear** - every enemy dies the moment it is in play, wherever it is (those
    already about when you switch it on too), as if shot: the game's own death, and the kill scores as yours
    do - its points, the combo and the kill count. The big ones (the chainsaws, the Garradors, J.J.) as well.
    The game sends a stage's enemies only from the parts of it you stand in (the village's 102 come in seven
    lists, one for each part), so standing still you would only ever see those parts' enemies; with this on,
    every part sends its own wherever you are. They come at the game's pace: at most ten about at once. Every enemy and every body takes some of the game's model memory, and a new one the game cannot
    fit in it vanishes before it appears - lost to the stage for good. So the bodies go quickly (half a second
    of the fall, then they sink and fade at once, and are cleared away the moment they have gone), the
    oldest enemies die first, the kills wait in the rare moment the bodies still about would leave no room
    for the enemies that come next (the panel then says "waiting for bodies to clear"), and an enemy lost
    anyway is sent again once there is room. With every part sending, a house's enemies come from the start,
    so entering one brings nothing new. The village's last 11 come with three Bonus Time hourglasses in the
    game - only once each is picked up; with this on they come with the rest whether you pick them up or not
    (the hourglasses still give their time). Its chainsaws come in pairs at 10, 25 and 35 kills, each pair only
    once, and one the game has no room for as its pair comes would never come; with this on it comes later with
    the rest. So all 124 come. The castle sends its 348 its own way: with every part sending at once it would
    send more in a moment than it has room for, and an enemy it has no room for is lost to it - so there its
    parts send in turn, a couple at a time, any that still find no room are sent again, and its bodies are
    cleared away the moment they have gone instead of every ten seconds. Two gates there open only when you go
    to them (one you walk up to, one you open with the action button) and let out 12 more, and the second also
    brings 25 later; with this on, once the castle has nothing left to send, each gate opens by itself as if you
    had gone there - the game's own short scene of the gate plays. So all 348 come. Waterworld sends from seven
    zones while you stand in them: each sends 15 (one 10) and is done, but one that has a single send left when
    you leave and come back sends five at once - 19 (14), the most it sends, and how the fastest runs reach 156.
    Its last 17 come once each from spots you have to reach (the tower's top among them), and one comes at 30
    kills. With this on, the stage's spots are the mod's: each zone sends up to one short of its end and then its
    last five together, the spots send once each, wherever you are. So all 156 come - never more, as each
    zone still stops where the game stops it. It waits for the clock to
    start (with Bonus time always on, for that time to be running too) and stops when time is up or you are
    down. With Endless combo on too, every kill of the game adds to one combo. Every 15 seconds of play the
    mod's log (RE4OGCabbyCodes.log) notes the kills and the game's memory, and if the kills stop for 12
    seconds it records what the stage has about at that moment - send it along if a stage stops sending
    enemies.
  - **Bonus time always on** - the Bonus Points time (the one a Bonus Points hourglass starts, in which every
    kill's combo bonus is at least 1,000) runs from the moment the clock starts and never runs out; the kills
    in it are shown beside the switch. The game puts what a combo earns onto your score only once that time
    is over (and the combo too); with this on, it goes on once the combo is over - or as it is earned, with
    Endless combo. Switching it off leaves 30 seconds of it, as a pickup would.
- **Save without counting** and a **save count** editor - saving at a typewriter adds nothing to the save
  count: the number the game's load and save lists show for each file, beside its play time. The save
  itself is made as usual. The editor sets the count of the game you are playing (up to the game's own
  999); your next save writes it into its file, plus one unless Save without counting is on, and loading a
  save brings back that save's count. A continue after a death keeps the count you set.
- **Freeze play time** and a **play time** editor - the play time your saves show and the results screen
  records stops where it is, and runs on again from there the moment you switch it off: the time it was
  frozen for never counts. The editor sets it (hours, minutes, seconds, up to the game's own 99:59:59), and
  the game counts on from what you set. Loading a save brings back that save's own time.
- **Freeze countdown timer** and a countdown editor - the countdown on screen stops where it is, so the time
  never runs out: the Mercenaries' clock and every timed scene of the game. The Mercenaries' time bonuses
  still add to it. The editor sets what is left of the countdown running now.
- **Infinite money** - the merchant takes no pesetas: buying from him and tuning up a weapon leave your money
  as it was, and selling to him still adds to it. He still only sells what your pesetas cover (the game
  checks that first), so for something dearer, set your pesetas with the editor.
- **Pesetas** editor - the money the merchant takes, up to 99,999,999.
- **Costume** - in the main game, the costume set the title's Special Costume choice picks, worn by both:
  Normal, Special 1 (Leon in his R.P.D. uniform, Ashley as a pop star) or Special 2 (Leon in a gangster's
  suit, Ashley in armour - nothing can hurt her or carry her off in it). The game dresses Leon at each door,
  so he changes at the next one; your saves keep the set.
- **Difficulty** - in the main game and Separate Ways, switch the game you are playing between Amateur, Easy,
  Normal and Professional on the spot. What the game reads as it goes follows at once: the damage you take,
  how much ammo a pickup holds, Amateur's doubled magazines. What it set up when the area loaded stays until
  the next one. Your next save records it. The game's own dynamic difficulty (its rank, 1-10) is shown
  beside it.
- **Results** - a section of its own, in the main game and Separate Ways: the deaths, enemies killed, shots
  fired and hits the results screen shows at the end of each chapter, for this chapter and for the whole
  game, each with an editor, and the hit ratio they make. Your saves keep them. Two switches keep them
  clean:
  - **Misses don't count** - a shot that misses is not counted at all: only a shot that hits adds to the
    shots fired (as it adds to the hits), so every shot counted from then on is a hit.
  - **Deaths don't count** - dying and continuing adds nothing to the deaths, this chapter's or the whole
    game's. (The game's own tally of retries in one place, which some of its scenes look at, still counts.)
- **Attache case editor** - the case of the character you are playing, straight from the game: change a
  count, a weapon's loaded rounds or its upgrade levels (firepower, firing speed, reload speed, capacity -
  up to the exclusive upgrade) and *Apply*; **Store** puts an item in the game's item box (Assignment Ada
  and the Mercenaries have none), **x** throws it away. The case's size (S, M, L or XL) is shown with it:
  the game keeps it as the Attache Case item you hold, which is the case's own and in no list.
- **Add to the case** - inside the attache case's section, folding on its own: what takes room in the
  attache case, in a list filed under tabs (weapons, grenades, attachments, ammo, recovery), the weapons
  under headings: handguns, magnums, shotguns, rifles, machine guns, special weapons - and Assignment Ada's
  Plaga Samples, the one key item that takes room in the case, under a key items tab of its own. The
  search box above it narrows the list as you type: every word you type must be in the name (`scope rifle`
  finds both rifles' scopes), or type an item number; each tab says how many of its items match. Pick an
  item and press **Add**, or double-click it: it goes into the case where the game's own placement finds it
  room - turning it if it has to, as a pickup does - or, with no room, nowhere; a new weapon arrives loaded,
  at level 1. Counts are held to what the game itself fits in one piece, for your difficulty. Key items,
  maps, files and treasures are added from their own lists below.
- **Key items** - the key items and maps you carry, under those headings. They take no room in the case, so
  they never go in the item box: **x** removes one (a key item the story still needs is gone until you add
  it back). **Add key items**, inside its section and folding on its own, offers the ones you do not have -
  search it, pick one and press Add, or double-click it.
- **Files** - the files you carry, like the Playing Manuals, in a list of their own: **x** removes one, and
  **Add files**, inside its section and folding on its own, offers the ones you do not have, in the story's
  order.
- **Treasures** - the treasures, gems and bottle caps you carry, in a list of their own: change a count and
  *Apply*, **x** removes one. **Add treasures**, inside its section and folding on its own, offers the ones
  you do not have, and those you hold fewer of than one entry holds (999). Like the key items, they take no
  room in the case and do not go in the item box. The shooting gallery counts the bottle caps you hold only
  while you hold its Bottle Caps collection too.
- **Item box** - an item box of the mod's own (RE4 has none), with no limit on what it holds, kept outside the
  game in a file beside the mod, for what takes room in the attache case. **Store**
  beside any item of the case puts all of it in the box; **Take** puts it back where the game's own placement
  finds it room (topping up what you carry of it first, turning a new piece if it has to). Everything of one
  item is one entry, however much of it you store: 350 handgun rounds are *Handgun Ammo x350*, and Take hands
  them over one piece's worth at a time (what one piece holds on your difficulty). A weapon keeps an entry of
  its own, with its upgrade levels and the rounds it has loaded, and comes back with them; nothing else has
  either. A stock or scope stored while it is on a gun comes off it, and Take hands it back loose, to attach
  again in the game's case screen. The box is filed under the case's tabs - weapons, grenades, attachments,
  ammo, recovery - in the order Add to the case lists them, with a filter box. Change an entry's count and
  *Apply* to set it to any number (a weapon's entry is that one weapon, with no count); **x** throws an entry
  away.
  **One box for each game that saves**: the main game has its box, and Separate Ways one of its own, each
  in a file of its own (`RE4OGCabbyCodes.itembox.ini`, `RE4OGCabbyCodes.itembox.separate-ways.ini`). Every
  save file and every new game of that game sees the same box, and nothing the game does changes it - not a
  save, a load, a new game, nor a death and its continue. Only Store, Take, a count you set and x do, and
  each is written to the file at once. Your save file never holds any of it, so removing the mod just leaves
  the boxes behind. Assignment Ada and the Mercenaries, played in one sitting with no saves, have no item
  box: the panel shows neither the box nor Store there.

### Save files

Open the title's **Load** list, or save at a **typewriter**, and the panel lists the game's twenty save files
beside it - chapter, play time, difficulty and when it was saved - with the one the game's cursor is on
highlighted:

- **Delete** empties a file: the game shows it as empty from then on, like a file that was never used.
- **Copy to...** puts a copy of a save into any other file, empty or not (a save already there is replaced).

Both ask first and change your saves straight away. RE4 keeps all twenty in one file, `savegame00.sav`, in
Steam Cloud: the change is made in the game's own copy of it and written back the way the game writes it
after every save, through Steam's own storage, then read back to check. A copy of `savegame00.sav` as it was
is kept first in `RE4OGCabbyCodes.save-backup` beside the mod (the ten newest). The item box is not part of
any save, so neither touches it.

While the panel is up the game lets go of the mouse, and the panel draws its own pointer. While the pointer
is over the panel the game does not see the mouse, and while you type in one of its boxes it does not see the
keyboard (Esc and Alt stay the game's).

Tick or edit what you want and unpause. The switches are kept: the mod writes them into
`RE4OGCabbyCodes.ini` as you change them, so the next start begins with them as you left them. Click a
section's header to fold it away; the panel remembers which are folded. **F7** hides or shows the panel.

Works on Windows and on Linux/Proton with no launch options. It leaves `dinput8.dll` alone, so it can sit
beside re4_tweaks.

### A fix for the game: Separate Ways' clear save

Always on, with nothing to switch. When you beat Separate Ways the game asks whether to save, and sometimes
no file list comes up: "Saving..." shows for a few seconds, then the title, and no clear file is made.
Players have long worked round this by replaying the ending. The cause is in the game. As the results come up,
it saves its records (the clear times) on their own, without a file list. When that save cannot start
(the game's scheduler is busy with something else at that moment), it leaves its "records only" mark behind.
The save you ask for next then runs as a records-only save. The fix is a small change to the game's code at
the one place where a save gets turned away: the mark is cleared right there, as the game clears it after any
save that runs, so the clear save shows its file list. It is put in once as the game starts, and does nothing
until a save is turned away. Your records are saved with the clear save, since every save writes them. Any
other save the game turns away the same way (an option changed, the Mercenaries' results) no longer spills
into the next one either.

### What the other mods have that RE4 has no use for

The sibling mods' other features have nothing to act on in RE4, so they are not here:

- **Infinite ink ribbons** / **Save without ink ribbons** - RE4 has no ink ribbons: its typewriters save
  freely.
- **Skip cutscenes** (RE3 1999's) - RE4 lets you skip its cutscenes itself.
- The **recovery total**, the **reloading tool**, the **Mercenaries money** and the **epilogues** its clear
  count unlocks are RE3 (1999)'s own systems.
- **Item box without counting**, **Recovery items without counting**, the **step count** and the **rank at
  the clear** are for the remakes' records and ranks; RE4's results screen gives no rank, and it keeps no
  records.
- **No durability loss** and **Infinite wooden boards** - RE4's knife never wears down, and it has no
  windows to board up.

## Install

1. Open the game's **`Bin32`** folder - in Steam, right-click the game, *Manage*, *Browse local files*, then
   `Bin32` (the folder with `bio4.exe`).
2. Rename the existing `steam_api.dll` to `steam_api_orig.dll`.
3. Copy the mod's `steam_api.dll` in beside it.

Steam's *Verify integrity of game files* puts the stock DLL back; just repeat step 3 if that happens. To
uninstall, delete the mod's `steam_api.dll` and rename `steam_api_orig.dll` back.

### If it does not work

The mod says what is wrong - in a message box as the game starts, or in the panel:

- *The game's own Steam DLL is missing* - there is no `steam_api_orig.dll`: step 2 was skipped, or the mod's
  DLL was copied over the game's. In Steam, right-click the game, *Properties*, *Installed Files*, *Verify
  integrity of game files* (that brings the game's `steam_api.dll` back), then do steps 2 and 3 again.
- *steam_api_orig.dll is not the game's own Steam DLL* - the file renamed in step 2 is not the one the game
  came with, and the game cannot run with it. Delete `steam_api_orig.dll` first - verifying the game files
  leaves it where it is - then the same fix: verify the game files, and steps 2 and 3.
- *RE4OGCabbyCodes does not work with this version of Resident Evil 4*, or in the panel *not a version the
  mod was made for* - not the current Steam version (build 10112090). The mod leaves that version of the
  game alone.

For anything else, send `RE4OGCabbyCodes.log` and `RE4OGCabbyCodes.prev.log` from the `Bin32` folder - the
files themselves: they are the record of what the mod found.

## Please read before using

- **This first version has not been run in play yet.** Everything it uses was found in `bio4.exe` and
  checked against the file on disk: its start-up runs against the real exe under Wine, and it has started
  with the game (to the title screen) under Proton. The first sessions with it are its real test - back up
  your saves first: `Steam/userdata/<your id>/254700/remote/savegame00.sav`.
- It is meant to sit beside re4_tweaks (it stays off the code re4_tweaks patches), but the two have not
  been tried together yet.
- The cheats change what the results screen records (play time, deaths, hits), and a save made while
  cheating records the game as it was.
- One hit kills changes boss fights: a boss whose fight is scripted around its health may end sooner.
- The automatic shooting gallery has been played through offline only, against the game's eight target
  scripts as the mod reads them; game A's harder script (once you hold three of its caps) leaves it only a
  few frames to spare for its sixth bonus target.
- The Mercenaries' switches have been tried in the village: in the first three games the stage stopped sending
  enemies after 50 to 60 kills (the enemies it sent while the game's model memory was full vanished, and the
  stage never sends an enemy twice); in the fourth, with the waiting and the sending again, 111 of 124 - the
  pickups' enemies and a pair of chainsaws never came, and the kills waited too long; in the fifth, 113, quickly,
  every chainsaw - the 11 that come with the three Bonus Time hourglasses never came, none being picked up; in
  the next two, 123 each, the hourglasses' enemies with the rest - one chainsaw, the second of the pair
  at 25 kills, found no room as its pair came, both times; then all 124. The castle stopped at 285 of 348: its
  gates never opened (nobody went to them), one of its bosses and 25 others found no room; with its sends in
  turn, its sends again and its gates opening by themselves, all 348. The castle then needed a walk to one door for its last two, and its second boss came late; now its boss's part keeps sending and the door's two come without it. The base came to 116 once: one of its two one-time enemies found no room; now it comes later with the rest. Before that, the base: all 117 (every enemy its code
  sends but the 20 of its two start groups not chosen, and one its code names twice by mistake). Waterworld
  stopped at 125 of 156: every zone sent its 15 (10), none its last five at once, and the tower's top's 3 never
  came. Its zones' last five at once and its spots sent from anywhere have been run offline only (a stand-in
  Waterworld), not yet in a game.
- The attache case, the key items, the files and the treasures are the game's own: a change is kept by your
  next save and lost, like anything else, if you quit without saving - an item thrown away is gone for good
  once you save. They change only while the game is paused, and a change the mod cannot make safely is
  refused, with the reason in the panel.
- The item box is kept apart from the game: it changes only when you Store, Take, set a count or throw away,
  and each change is in its file at once - but the game keeps your attache case as it is only when you save.
  An item you take out of the box and then lose with the game (you quit without saving, or die and continue
  from before you took it) is gone from both; one you store and then do the same with is in both places.
- The save files' Delete and Copy change your saves (and Steam Cloud) at once. A backup of the file is kept
  beside the mod first, but back up `savegame00.sav` yourself before trying them.
- The fix for Separate Ways' clear save comes from the game's code and an offline run of it; it has not yet
  been seen at the end of a game.
- Everything the mod uses is found by byte signature in the game's code, so in a build where something is
  missing that feature is greyed out (or the panel says what is missing), and the log says what it was.

## Config

`RE4OGCabbyCodes.ini` is created next to the DLL on first run:

| Key | Default | Meaning |
| --- | --- | --- |
| `ToggleKey` | `0x76` (F7) | virtual-key code that hides/shows the panel |
| `GodMode`, `AshleyGodMode`, `OneHitKills`, `InfiniteAmmo`, `SteadyAim`, `AutoQTE`, `AutoGallery`, `EndlessCombo`, `AutoKill`, `BonusTime`, `NoSaveCount`, `FreezePlaytime`, `FreezeCountdown`, `InfiniteMoney`, `NoMissCount`, `NoDeathCount` | `0` | each cheat's state when the game starts; the panel writes its switches here as you change them |
| `AlwaysShow` | `0` | debug: draw the panel everywhere, not only on its screens |
| `Trace` | `0` | verbose diagnostics in `RE4OGCabbyCodes.log` |
| `Watch` | | debug: `0x846F9C:2,0x80A99:1` logs those values (offsets from `bio4.exe`'s base) as they change |
| `Disable` | | comma list of subsystems to turn off: `overlay,tick,game,input` |

`RE4OGCabbyCodes.log` beside the DLL records what the mod found and did, and the run before it is kept as
`RE4OGCabbyCodes.prev.log` - attach both when reporting a problem. `RE4OGCabbyCodes.imgui.ini` remembers
the panel's two windows - the one at the pause and the save files beside the load and save lists - each
with its own place, size and collapsed state, and which sections are folded (written half a second after a
change, and as the panel goes away). `RE4OGCabbyCodes.itembox.ini` holds the main game's item box and
`RE4OGCabbyCodes.itembox.separate-ways.ini` Separate Ways', one line per entry: `4:350` is 350 handgun rounds,
and `35:1:0x1111:13` a Handgun at level 2 in everything with 13 rounds loaded (what follows `;` on a line is
the item's name, for reading). Each is plain text, written whole at every change: edit it only while the game
is closed. Deleting one empties that box. If one is there but cannot be read as the game starts, the mod
leaves it alone and that box cannot be used until the next start.
`RE4OGCabbyCodes.save-backup` keeps the copies of `savegame00.sav` made before each Delete or Copy.

## How it works

The mod ships as a proxy `steam_api.dll` in front of the game's own (renamed `steam_api_orig.dll`): every
export is forwarded untouched through generated jump thunks, so it loads with the game and needs no
injector. `bio4.exe` is never modified on disk, and nothing is a fixed address: everything the mod uses is
found in the game's code by short byte signatures, each global read out of the instruction that uses it and
cross-checked between sites. The mod runs on the game's own thread, once a frame: the frame loop calls
`SetThreadAffinityMask` as each frame starts, and the mod's import of it acts only on that call. The cheats
are small patches of the game's code, put in and taken out as you switch them - the damage routine's
subtractions, the shot's round, the bow's arrow and the bowgun's bolt, the play time's update and the
countdown's tick, the QTE prompts' check and each scene that reads its own prompt's buttons - and the editors
write the game's own values while it is paused. The fix for Separate Ways' clear save is one such patch with
no switch: put in once as the game starts, in the scheduler's code that turns a task away (where the game only
called two empty debug prints). The automatic shooting gallery hands a target the record a
shot that hit its head would leave, and the target's own code scores it. The attache case's
changes go through the game's own functions: an item comes in the way a pickup does (the case's own
placement, or the key items' list) and goes out through the item manager's own removal, which also
unequips a weapon. The item boxes are the mod's own files, which the game never sees. The save files are
written through the game's own Steam storage object, as the game writes them. The panel is Dear ImGui,
drawn on the game's render thread from a hook on the Direct3D 9 device's `Present`; the mouse and keyboard
guard sits on the game's DirectInput devices. See `CLAUDE.md` for the reverse-engineering record.

The game's structures are named after [re4_tweaks](https://github.com/nipkownix/re4_tweaks)' SDK, which
documents this exe.

## Building from source

Linux with mingw-w64 (`i686-w64-mingw32-g++`); no Windows or MSVC needed.

```sh
cp config.mk.example config.mk   # set GAME_DIR (the Bin32 folder with bio4.exe)
make                             # build/steam_api.dll
make install                     # deploy into GAME_DIR (renames the stock DLL once)
make uninstall                   # put the stock DLL back
make sigs                        # check every signature against the installed bio4.exe
make proxy                       # regenerate the export list from the stock DLL
```

`tools/` holds the reverse-engineering helpers - the signature checker, the item table generator, the proxy's
export list and the Ghidra query script - described in `CLAUDE.md`; `tests/` runs the mod's own start-up
against the real `bio4.exe` under Wine. Dear ImGui (MIT) is vendored under `contrib/imgui`. Licensed under
the GPL-3.0; see `LICENSE`.
