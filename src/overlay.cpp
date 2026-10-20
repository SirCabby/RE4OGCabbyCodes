#include "overlay.h"

#include <cfloat>
#include <cstdio>
#include <cstring>

#include "adopt.h"
#include "cheats.h"
#include "config.h"
#include "dispatch.h"
#include "game.h"
#include "imgui.h"
#include "imgui_internal.h"
#include "backends/imgui_impl_win32.h"
#include "inventory.h"
#include "itembox.h"
#include "items.h"
#include "log.h"
#include "savefiles.h"
#include "version.h"

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);

namespace re4cc::overlay {
namespace {

HWND g_hwnd = nullptr;
WNDPROC g_orig_wndproc = nullptr;
bool g_context_ready = false;
bool g_visible = false;
bool g_user_hidden = false;  // the toggle key, while the panel's screen is up

// The panel is drawn half as big again as ImGui's own sizes: the game draws at
// the display's resolution, where ImGui's 13-pixel font is small (the size the
// user settled on for RE3-1999CabbyCodes, 2026-10-07). Every size below that
// is written in pixels is at ImGui's own scale and goes through scaled().
constexpr int kScalePercent = 150;
constexpr float kScale = kScalePercent / 100.0f;
float scaled(float px) { return px * kScale; }

// Kept in the ImGui layout file ([RE4OGCabbyCodes][Panel]) beside the panel's
// position and size: which sections are folded away, and the scale that size
// was saved at (a size saved at another scale is brought to this one once).
bool g_cheats_open = true;
bool g_results_open = true;
bool g_case_open = true;
bool g_add_open = true;
bool g_key_open = false;
bool g_key_add_open = false;
bool g_files_open = false;
bool g_files_add_open = false;
bool g_treasures_open = false;
bool g_treasures_add_open = false;
bool g_box_open = true;
int g_layout_percent = 100;
bool g_layout_settled = false;

// The panel's two windows, each with its own place, size and collapsed state in
// the layout file: the pause panel, and the save file manager beside the load
// and save lists. The version is only in their titles (after ### comes the
// name ImGui keeps them by), so a new version keeps the layout.
const char* const kPanelWindow = "Resident Evil 4 - Cabby Codes  v" RE4CC_VERSION "###RE4CC panel";
const char* const kFilesWindow = "Resident Evil 4 - Cabby Codes: save files  v" RE4CC_VERSION "###RE4CC save files";

CRITICAL_SECTION g_imgui_cs;
bool g_imgui_cs_ready = false;

// What the panel is taking this frame, published for the input guard.
volatile LONG g_capture_mouse = 0;
volatile LONG g_capture_keyboard = 0;
volatile LONG g_visible_flag = 0;

const ImVec4 kOrange(1.0f, 0.6f, 0.3f, 1.0f);

LRESULT CALLBACK hk_wndproc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
  const dispatch::Snapshot s = dispatch::snapshot();
  const bool showable = s.show_panel || config::get().always_show;
  // F-keys above F9 arrive as WM_SYSKEYDOWN; accept both, ignore auto-repeat.
  if ((msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN) && !(lp & (1 << 30)) &&
      static_cast<int>(wp) == config::get().toggle_key && showable) {
    ImGuiLock guard;  // wants_draw reads and clears this
    g_user_hidden = !g_user_hidden;
    logf("panel %s by the toggle key", g_user_hidden ? "hidden" : "shown");
    return 0;
  }
  // ImGui sees every message, visible or not: a button release that arrives
  // after the panel is hidden would otherwise never be delivered, leaving it
  // convinced the button is still held (and the window stuck to the pointer).
  if (g_context_ready) {
    bool swallow = false;
    {
      ImGuiLock guard;
      if (ImGui_ImplWin32_WndProcHandler(hwnd, msg, wp, lp)) return 1;
      // Swallowing is the visible panel's business, and only for what it is
      // using. The game reads its keys and mouse through DirectInput rather
      // than from here (input.cpp does the real work).
      if (g_visible) {
        const ImGuiIO& io = ImGui::GetIO();
        if (io.WantCaptureMouse && msg >= WM_MOUSEFIRST && msg <= WM_MOUSELAST) swallow = true;
        if (io.WantTextInput && msg >= WM_KEYFIRST && msg <= WM_KEYLAST && wp != VK_ESCAPE) swallow = true;
      }
    }
    if (swallow) return 0;
  }
  // The game's own window procedure runs outside the lock.
  return CallWindowProcA(g_orig_wndproc, hwnd, msg, wp, lp);
}

// Keep the current window inside the display: moved back whenever any part of
// it has ended up outside (a saved position from a bigger window, a panel that
// grew).
void keep_in_reach(const ImVec2& display) {
  const ImVec2 pos = ImGui::GetWindowPos(), size = ImGui::GetWindowSize();
  ImVec2 want = pos;
  if (want.x + size.x > display.x) want.x = display.x - size.x;
  if (want.y + size.y > display.y) want.y = display.y - size.y;
  if (want.x < 0) want.x = 0;
  if (want.y < 0) want.y = 0;
  if (want.x != pos.x || want.y != pos.y) ImGui::SetWindowPos(want);
}

// The help for the item just drawn, on hover (a disabled one too).
void item_help(const char* help) {
  if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled) && ImGui::BeginTooltip()) {
    ImGui::PushTextWrapPos(scaled(380.0f));
    ImGui::TextUnformatted(help);
    ImGui::PopTextWrapPos();
    ImGui::EndTooltip();
  }
}

void help_marker(const char* help) {
  ImGui::SameLine();
  ImGui::TextDisabled("(?)");
  item_help(help);
}

void cheat_row(cheats::Kind k, const char* label, const char* help) {
  const bool available = cheats::available(k);
  bool v = cheats::enabled(k);
  if (!available) ImGui::BeginDisabled();
  if (ImGui::Checkbox(label, &v)) {
    cheats::set_enabled(k, v);
    logf("%s %s (panel)", cheats::name(k), v ? "ON" : "OFF");
  }
  if (!available) ImGui::EndDisabled();
  if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled) && ImGui::BeginTooltip()) {
    ImGui::PushTextWrapPos(scaled(380.0f));
    ImGui::TextUnformatted(help);
    if (!available) {
      ImGui::Separator();
      ImGui::TextUnformatted("Its code was not found in this version of the game - see the log.");
    }
    ImGui::PopTextWrapPos();
    ImGui::EndTooltip();
  }
  if (!available) {
    ImGui::SameLine();
    ImGui::TextDisabled("(not found)");
  }
}

void hms(int seconds, char* out, size_t n) {
  if (seconds < 0) {
    std::snprintf(out, n, "?");
    return;
  }
  std::snprintf(out, n, "%d:%02d:%02d", seconds / 3600, (seconds / 60) % 60, seconds % 60);
}

// An amount with thousands separators.
void grouped(int v, char* out, size_t n) {
  if (v < 0) std::snprintf(out, n, "?");
  else if (v >= 1000000) std::snprintf(out, n, "%d,%03d,%03d", v / 1000000, (v / 1000) % 1000, v % 1000);
  else if (v >= 1000) std::snprintf(out, n, "%d,%03d", v / 1000, v % 1000);
  else std::snprintf(out, n, "%d", v);
}

int bank_of(int id) {
  const items::Def* d = items::find(id);
  return d ? static_cast<int>(d->bank) : static_cast<int>(items::kBankKey);
}

// The lists off the case's grid, each a section of its own: the key items (the
// maps with them), the files - every item the game calls one - and the
// treasures (gems and bottle caps with them). An item the table does not know
// is taken for a key item.
enum OffGrid { kKeyItems, kFiles, kTreasures };
int list_of(int id) {
  const items::Def* d = items::find(id);
  if (d && d->bank == items::kBankTreasures) return kTreasures;
  return d && d->type == items::kFile ? kFiles : kKeyItems;
}
const char* list_name(int list) {
  return list == kTreasures ? "treasures" : list == kFiles ? "files" : "key items";
}

// An item as the panel names it: its name, or its number for an id the table
// does not know.
const char* item_label(int id, char* buf, size_t n) {
  if (items::find(id)) return items::name(id);
  std::snprintf(buf, n, "item %d", id);
  return buf;
}

// Whether an item answers a search: every word typed is part of its name
// (case aside), or - a number - its item number. Nothing typed: every item.
bool item_matches(int id, const char* name, const char* query) {
  auto lower = [](char c) { return c >= 'A' && c <= 'Z' ? static_cast<char>(c + 32) : c; };
  for (const char* q = query ? query : ""; *q;) {
    while (*q == ' ') ++q;
    const char* word = q;
    while (*q && *q != ' ') ++q;
    const size_t n = static_cast<size_t>(q - word);
    if (!n) break;
    bool hit = false;
    for (const char* p = name; *p && !hit; ++p) {
      size_t i = 0;
      while (i < n && p[i] && lower(p[i]) == lower(word[i])) ++i;
      hit = i == n;
    }
    if (!hit) {
      int num = 0;
      size_t i = 0;
      while (i < n && word[i] >= '0' && word[i] <= '9' && num < 100000) num = num * 10 + (word[i++] - '0');
      hit = i == n && num == id;
    }
    if (!hit) return false;
  }
  return true;
}

// --- the cheats --------------------------------------------------------------------------
// Edits the panel is typing: kept until Set, then let go so the field follows the game again.
struct Edit {
  int v = 0;
  bool typing = false;
};

void int_editor(const char* id, Edit& e, int live, int lo, int hi, float width, bool* set) {
  if (!e.typing) e.v = live;
  ImGui::SetNextItemWidth(scaled(width));
  if (ImGui::InputInt(id, &e.v, 0, 0)) e.typing = true;
  if (e.v < lo) e.v = lo;
  if (e.v > hi) e.v = hi;
  *set = false;
  ImGui::SameLine();
  char b[48];
  std::snprintf(b, sizeof(b), "Set%s", id);
  if (ImGui::SmallButton(b)) {
    *set = true;
    e.typing = false;
  }
}

Edit g_pesetas, g_saves, g_counters[8];
int g_pt_h = 0, g_pt_m = 0, g_pt_s = 0;
bool g_pt_typing = false;
int g_cd_m = 0, g_cd_s = 0;
bool g_cd_typing = false;

void forget_edits() {
  g_pesetas.typing = false;
  g_saves.typing = false;
  for (Edit& e : g_counters) e.typing = false;
  g_pt_typing = false;
  g_cd_typing = false;
}

void draw_cheats(const cheats::Status& st) {
  const game::Run& run = st.run;
  const bool main_game = run.mode == game::Mode::kMain;
  ImGui::SeparatorText("Player");
  cheat_row(cheats::kGodMode, "God mode",
            "No hit takes any health off you: every hit goes through the game's one damage routine, and the player's "
            "subtraction there is taken out. The hits that kill outright instead (an attack made to kill in one blow, a "
            "falling rock, the truck) knock you down. Your health is held at its maximum too, so what you had lost when "
            "you switch it on comes back. You still flinch and get knocked about, and grabs still play out; a grab or a "
            "scene that ends in a death of its own (a fall, a missed QTE) is left to the game.");
  if (st.health_ok && st.health.max > 0) {
    ImGui::SameLine();
    ImGui::TextDisabled("(%d / %d)", st.health.hp, st.health.max);
  }
  if (main_game) {
    cheat_row(cheats::kAshleyGod, "Ashley takes no damage",
              "The same for Ashley: no hit takes any of her health, and it is held at its maximum. What would kill her "
              "outright does not: an explosion close by (the catapults' shots, a grenade, dynamite) or the truck makes "
              "her duck, as an explosion further off does, and El Gigante's stomp and a falling rock leave her be, as "
              "they leave her armour (Special 2). Being carried off is not damage - the game ends that its own way.");
    if (st.health_ok && st.health.sub_max > 0) {
      ImGui::SameLine();
      ImGui::TextDisabled("(%d / %d)", st.health.sub_hp, st.health.sub_max);
    } else {
      ImGui::SameLine();
      ImGui::TextDisabled("(not with you)");
    }
  }
  cheat_row(cheats::kOneHitKills, "One hit kills",
            "Any hit that does damage takes all of an enemy's health, so the game's own code kills it - deaths, drops "
            "and parasites as usual. El Gigante, which the game hurts only through its parasite, falls to the first "
            "hit anywhere, as it does to a rocket (a flash grenade still only blinds it). Krauser cannot die in the "
            "first part of his fight: there the first hit with damage makes him flinch and retreat, as he does once he "
            "has taken enough. Mutated, he falls to the first hit, even on the arm he guards with. Any other enemy the "
            "game keeps alive for now (a scripted moment) keeps its last point, and the game decides. Bosses whose "
            "fights are scripted around their health may end sooner.");
  cheat_row(cheats::kInfiniteAmmo, "Infinite ammo",
            "Firing uses no ammo: the gun in your hands keeps its loaded rounds, and the bowgun and Krauser's bow "
            "use no bolts or arrows. Reloads and pickups work as usual; grenades and the rocket launcher are used "
            "up as the game uses them.");
  cheat_row(cheats::kSteadyAim, "Steady aim",
            "Your aim holds still, and your shots go where it points. Without it, the game makes an aim you hold "
            "wander (the laser sight's dot, or the view through a rifle's scope, drifts by up to a few degrees), "
            "throws it at a shot, sends each bullet a little off it at random, and has the laser and the bullets "
            "follow the gun in Leon's hand, which sways with his idle aiming stance and kicks at every shot. With "
            "it, Leon holds his aiming stance still, so the gun points where the laser does; once you stop moving "
            "the aim, the laser and every bullet stay on that exact line - through the recoil too - until you move "
            "it again. Handguns, magnums, machine guns and the mounted gun shoot straight down it. A shotgun keeps "
            "its spread of pellets, centred on the aim, and a thrown grenade or egg gets no random sideways push. "
            "Moving the aim works as before.");
  cheat_row(cheats::kAutoQte, "Automatic QTEs",
            "Quick time events play themselves. A prompt to press buttons in time - dodging Krauser's knife or a "
            "boss's blow, the boulder, the statue's swings, the lasers, Saddler's bridges - succeeds the moment it "
            "comes up, and you do what the buttons would have done. Where a prompt wants presses as fast as you can "
            "- running from the boulder, swimming back to the boat, climbing up, the knife in El Gigante's "
            "parasite, the end of Krauser's knife fight - the game sees a press every frame; running from the "
            "statue, Leon goes at full speed from his first step, and a grab is shaken off at once. The prompts "
            "still flash up.");
  if (main_game) {
    cheat_row(cheats::kAutoGallery, "Automatic shooting gallery",
              "The merchant's shooting gallery plays itself for the most points it gives. Start a game at the "
              "gallery and let it play: each Ganado target is hit in the head once it has risen (100 points, 200 for "
              "the special ones), the Ashley targets are never hit, and every bonus target that five hits in a row "
              "bring on is hit too (500). Where the gallery offers game C or D, it also hits the props that are "
              "worth 300 more, in the moment that costs no streak. Leave your gun be while it plays: a shot of yours "
              "that misses ends the streak, as it always does. The targets fall as if shot, with their points shown; "
              "no shots are fired, so your gun does not move.");
    if (st.gallery_ok && st.gallery_state >= 1 && st.gallery_state <= 3) {
      ImGui::SameLine();
      ImGui::TextDisabled("(game %c: %d points, streak %d)", "?ABCD"[st.gallery_game >= 1 && st.gallery_game <= 4
                                                                        ? st.gallery_game : 0],
                          st.gallery_score, st.gallery_streak);
    }
  }
  if (main_game) {
    static const char* const kSets[] = {"Normal", "Special 1", "Special 2"};
    int set = st.costume_set;
    ImGui::BeginDisabled(set < 0 || !run.paused);
    ImGui::SetNextItemWidth(scaled(160.0f));
    if (ImGui::Combo("Costume", &set, kSets, 3) && set != st.costume_set) cheats::request_costume_set(set);
    ImGui::EndDisabled();
    item_help(
        "The costume set the title's Special Costume choice picks, worn by both: Normal, Special 1 (Leon in his "
        "R.P.D. uniform, Ashley as a pop star) or Special 2 (Leon in a gangster's suit, Ashley in armour - nothing "
        "can hurt her or carry her off in it). The game dresses Leon at each door, so he changes at the next one; "
        "your saves keep the set.");
    if (st.costume_pending) {
      ImGui::SameLine();
      ImGui::TextColored(kOrange, "(Leon changes at the next door)");
    }
  }
  if (main_game || run.mode == game::Mode::kSeparateWays) {
    // The game's own values (+847C): Amateur 1, Easy 3, Normal 5, Professional 6.
    static const int kValues[] = {1, 3, 5, 6};
    static const char* const kNames[] = {"Amateur", "Easy", "Normal", "Professional"};
    const int d = st.counters_ok ? st.counters.difficulty : -1;
    int cur = d <= 1 ? 0 : d <= 3 ? 1 : d <= 5 ? 2 : 3;
    ImGui::BeginDisabled(d < 0 || !run.paused);
    ImGui::SetNextItemWidth(scaled(160.0f));
    if (ImGui::Combo("Difficulty", &cur, kNames, 4) && kValues[cur] != d) cheats::request_difficulty(kValues[cur]);
    ImGui::EndDisabled();
    item_help(
        "Switch the game you are playing between the game's difficulties on the spot. What the game reads as it "
        "goes follows at once: the damage you take, how much ammo a pickup holds, Amateur's doubled magazines. "
        "What it set up when the area loaded stays until the next one. Your next save records it.");
    if (st.counters_ok && st.counters.da_level > 0) {
      ImGui::SameLine();
      ImGui::TextDisabled("(game rank %d)", st.counters.da_level);
      item_help("The dynamic difficulty the game keeps on top of it (1-10): it rises as you play well and falls as "
                "you take damage.");
    }
  }
  // The Mercenaries' own: shown only there (the switches carry over into the next game of it).
  if (run.mode == game::Mode::kMercenaries) {
    ImGui::SeparatorText("The Mercenaries");
    char score[32];
    grouped(st.merc_ok ? st.merc.score : -1, score, sizeof(score));
    cheat_row(cheats::kEndlessCombo, "Endless combo",
              "A combo never runs out of time: every kill counts as the next one in it however long since the last, so "
              "its bonus keeps climbing to its most (from the tenth kill in a row on) and stays there. The game puts "
              "what a combo has earned onto your score only once the combo is over; with this on, it goes on as it is "
              "earned instead, each bonus shown as it comes. Bonus Points pickups work as before, and switching this "
              "off lets the combo run out as usual.");
    if (st.merc_ok && st.merc.combo > 0) {
      ImGui::SameLine();
      if (st.merc.bonus > 0) ImGui::TextDisabled("(combo %d, +%d coming)", st.merc.combo, st.merc.bonus);
      else ImGui::TextDisabled("(combo %d)", st.merc.combo);
    }
    cheat_row(cheats::kAutoKill, "Kill enemies as they appear",
              "Every enemy dies the moment it is in play, wherever it is - those already about when you switch this on "
              "too - as if shot: the game's own death, and the kill scores as yours do, with its points, the combo and "
              "the kill count. The big ones (the chainsaws, the Garradors, J.J.) as well. The game sends a stage's "
              "enemies only from the parts of it you stand in; with this on, every part sends its own wherever you "
              "are, so you need not go looking. They still come at the game's pace: at most ten about at once, one "
              "that falls counting for a moment more. Every enemy and every body takes some of the game's model "
              "memory, and a new one the game cannot fit in it vanishes before it appears, lost to the stage - so the "
              "bodies go quickly (half a second of the fall, then they sink and fade), the oldest die first, the kills "
              "wait in the rare moment the bodies would leave no room for the next ones (\"waiting for bodies to "
              "clear\"), and one lost anyway is sent again once there is room. With every part sending, a house's "
              "enemies come from the start; the village's last 11, which come with three Bonus Time hourglasses in the "
              "game, come with the rest whether you pick them up or not, and a chainsaw the game had no room for as "
              "its pair came (each pair comes once) comes later. The castle's parts send in turn, a couple at a time "
              "(all at once is more than it has room for), any that find no room are sent again, and once it has "
              "nothing left to send its two gates open by themselves (their short scene plays). Waterworld's seven "
              "zones each send up to one short of their end and then their last five at once, as when you leave a "
              "zone and come back - the most each sends - and its one-off spots (the tower's top among them) send "
              "once each: all 156. It waits for the clock to start (and, "
              "with Bonus time always on, for that time to run) and stops when time is up or you are down; with "
              "Endless combo every kill adds to one combo. The log notes the kills and the memory as it goes.");
    if (st.merc_ok) {
      ImGui::SameLine();
      ImGui::TextDisabled("(%d kills, %s points)", st.merc.kills, score);
      if (st.merc_waiting) {  // under the label, as long as the kills wait
        const float indent = ImGui::GetFrameHeight() + ImGui::GetStyle().ItemInnerSpacing.x;
        ImGui::Indent(indent);
        ImGui::TextDisabled("waiting for bodies to clear (the game's model memory is full)");
        ImGui::Unindent(indent);
      }
    }
    cheat_row(cheats::kBonusTime, "Bonus time always on",
              "The Bonus Points time - the one a Bonus Points hourglass starts, in which every kill's combo bonus is at "
              "least 1,000 - runs from the moment the clock starts and never runs out. The game puts what a combo "
              "earns onto your score only once that time is over (and the combo too); with this on, it goes on once "
              "the combo is over, or as it is earned with Endless combo. Switching it off leaves 30 seconds of it, as "
              "a pickup would.");
    if (st.merc_ok && st.merc.bonus_time) {
      ImGui::SameLine();
      ImGui::TextDisabled("(%d kills in it)", st.merc.bonus_kills);
    }
  }

  ImGui::SeparatorText("Saves, time & money");
  cheat_row(cheats::kNoSaveCount, "Save without counting",
            "Saving at a typewriter does not add to the save count - the number the game's load and save lists show "
            "for each file, beside its play time. The save itself is made as usual.");
  // Only the main game and Separate Ways save (the others' runs have no files).
  if (run.in_game && (main_game || run.mode == game::Mode::kSeparateWays) && st.counters_ok) {
    ImGui::BeginDisabled(!run.paused);
    ImGui::Text("Saves: %d", st.counters.saves);
    ImGui::SameLine();
    bool set = false;
    int_editor("##saves", g_saves, st.counters.saves, 0, game::kSavesMax, 90.0f, &set);
    if (set) cheats::request_saves(g_saves.v);
    ImGui::EndDisabled();
    item_help(
        "The saves made in this game so far (the game counts up to 999). Each save writes it into its file plus one "
        "- or as it is, with Save without counting on - and the lists show that file's count from then on. Loading "
        "a save brings back the count that save was made with.");
  }
  {
    char t[32];
    hms(st.counters_ok ? st.counters.play_time : -1, t, sizeof(t));
    cheat_row(cheats::kFreezePlaytime, "Freeze play time",
              "The play time stops where it is - the time your saves show and the results screen records - and runs "
              "on again from there the moment you switch this off: the time it was frozen for never counts. Loading "
              "a save brings back that save's own time.");
    // The values are a game's: outside one there is nothing to show or set,
    // and the switches above carry over into the next.
    if (run.in_game) {
      ImGui::BeginDisabled(!st.counters_ok || !run.paused);
      ImGui::Text("Play time: %s", t);
      if (!g_pt_typing && st.counters_ok) {
        g_pt_h = st.counters.play_time / 3600;
        g_pt_m = (st.counters.play_time / 60) % 60;
        g_pt_s = st.counters.play_time % 60;
      }
      ImGui::SameLine();
      ImGui::SetNextItemWidth(scaled(50.0f));
      if (ImGui::InputInt("h##pt", &g_pt_h, 0, 0)) g_pt_typing = true;
      ImGui::SameLine();
      ImGui::SetNextItemWidth(scaled(40.0f));
      if (ImGui::InputInt("m##pt", &g_pt_m, 0, 0)) g_pt_typing = true;
      ImGui::SameLine();
      ImGui::SetNextItemWidth(scaled(40.0f));
      if (ImGui::InputInt("s##pt", &g_pt_s, 0, 0)) g_pt_typing = true;
      ImGui::SameLine();
      if (ImGui::SmallButton("Set##pt")) {
        g_pt_h = g_pt_h < 0 ? 0 : g_pt_h > 99 ? 99 : g_pt_h;
        g_pt_m = g_pt_m < 0 ? 0 : g_pt_m > 59 ? 59 : g_pt_m;
        g_pt_s = g_pt_s < 0 ? 0 : g_pt_s > 59 ? 59 : g_pt_s;
        cheats::request_playtime(g_pt_h * 3600 + g_pt_m * 60 + g_pt_s);
        g_pt_typing = false;
      }
      ImGui::EndDisabled();
      item_help("The play time the game records, up to its own limit of 99:59:59. It counts on from what you set.");
    }
  }
  {
    const game::Countdown& cd = st.countdown;
    cheat_row(cheats::kFreezeCountdown, "Freeze countdown timer",
              "The countdown on screen stops where it is, so the time never runs out: the Mercenaries' clock and "
              "every timed scene of the game. The Mercenaries' time bonuses still add to it, and it runs on again "
              "the moment you switch this off.");
    if (cd.known && cd.running) {
      const int left = static_cast<int>(cd.frames / 30.0f);
      ImGui::SameLine();
      if (st.patched[cheats::kFreezeCountdown]) ImGui::TextColored(kOrange, "(%d:%02d left, frozen)", left / 60, left % 60);
      else ImGui::TextDisabled("(%d:%02d left)", left / 60, left % 60);
    }
    if (cd.known && cd.running && cd.editable) {
      const int left = static_cast<int>(cd.frames / 30.0f);
      if (!g_cd_typing) {
        g_cd_m = left / 60;
        g_cd_s = left % 60;
      }
      ImGui::BeginDisabled(!run.paused);
      ImGui::Text("Countdown:");
      ImGui::SameLine();
      ImGui::SetNextItemWidth(scaled(50.0f));
      if (ImGui::InputInt("m##cd", &g_cd_m, 0, 0)) g_cd_typing = true;
      ImGui::SameLine();
      ImGui::SetNextItemWidth(scaled(40.0f));
      if (ImGui::InputInt("s##cd", &g_cd_s, 0, 0)) g_cd_typing = true;
      ImGui::SameLine();
      if (ImGui::SmallButton("Set##cd")) {
        g_cd_m = g_cd_m < 0 ? 0 : g_cd_m > 99 ? 99 : g_cd_m;
        g_cd_s = g_cd_s < 0 ? 0 : g_cd_s > 59 ? 59 : g_cd_s;
        cheats::request_countdown(g_cd_m * 60 + g_cd_s);
        g_cd_typing = false;
      }
      ImGui::EndDisabled();
      item_help("What is left of the countdown running now. It counts down from what you set once you leave the "
                "pause.");
    }
  }
  cheat_row(cheats::kInfiniteMoney, "Infinite money",
            "The merchant takes no pesetas: buying from him and tuning up a weapon leave your money as it was, and "
            "selling to him still adds to it. He still only sells what your pesetas cover - the game checks that "
            "first - so for something dearer, set the pesetas below.");
  if (run.in_game && run.mode != game::Mode::kMercenaries && st.counters_ok) {
    char p[32];
    grouped(st.counters.pesetas, p, sizeof(p));
    ImGui::BeginDisabled(!run.paused);
    ImGui::Text("Pesetas: %s", p);
    ImGui::SameLine();
    bool set = false;
    int_editor("##ptas", g_pesetas, st.counters.pesetas, 0, game::kPesetasMax, 130.0f, &set);
    if (set) cheats::request_pesetas(g_pesetas.v);
    ImGui::EndDisabled();
    item_help("The money the merchant takes. Up to 99,999,999, what his screen shows.");
  }
  if (st.last_action[0]) ImGui::TextDisabled("%s", st.last_action);
}

// The results screen's counters, and the switches that keep them clean, in a
// section of their own: only the main game and Separate Ways keep them.
bool has_results(const cheats::Status& st) {
  return st.counters_ok && (st.run.mode == game::Mode::kMain || st.run.mode == game::Mode::kSeparateWays);
}
bool is_results_switch(cheats::Kind k) { return k == cheats::kNoMissCount || k == cheats::kNoDeathCount; }

void draw_results(const cheats::Status& st) {
  const game::Run& run = st.run;
  cheat_row(cheats::kNoMissCount, "Misses don't count",
            "A shot that misses is not counted at all: only a shot that hits adds to the shots fired (as it adds to "
            "the hits), so every shot counted from here on is a hit. Hits are counted as usual.");
  cheat_row(cheats::kNoDeathCount, "Deaths don't count",
            "Dying and continuing adds nothing to the deaths - neither this chapter's nor the whole game's. The game "
            "still keeps its own tally of retries in the same place, which some of its scenes look at.");
  const game::Counters& c = st.counters;
  struct RowDef {
    const char* name;
    game::Counter chapter, total;
    int vc, vt, max;
  } rows[] = {
      {"Deaths", game::Counter::kDeathsChapter, game::Counter::kDeathsTotal, c.deaths_chapter, c.deaths_total, 999},
      {"Enemies killed", game::Counter::kKillsChapter, game::Counter::kKillsTotal, c.kills_chapter, c.kills_total,
       99999},
      {"Shots fired", game::Counter::kShotsChapter, game::Counter::kShotsTotal, c.shots_chapter, c.shots_total, 999999},
      {"Hits", game::Counter::kHitsChapter, game::Counter::kHitsTotal, c.hits_chapter, c.hits_total, 999999},
  };
  ImGui::BeginDisabled(!run.paused);
  if (ImGui::BeginTable("results", 3, ImGuiTableFlags_SizingFixedFit)) {
    ImGui::TableSetupColumn("");
    ImGui::TableSetupColumn("this chapter");
    ImGui::TableSetupColumn("the whole game");
    ImGui::TableHeadersRow();
    for (int r = 0; r < 4; ++r) {
      ImGui::PushID(r);
      ImGui::TableNextRow();
      ImGui::TableNextColumn();
      ImGui::TextUnformatted(rows[r].name);
      bool set = false;
      ImGui::TableNextColumn();
      int_editor("##c", g_counters[r * 2], rows[r].vc, 0, rows[r].max, 90.0f, &set);
      if (set) cheats::request_counter(rows[r].chapter, g_counters[r * 2].v);
      ImGui::TableNextColumn();
      int_editor("##t", g_counters[r * 2 + 1], rows[r].vt, 0, rows[r].max, 90.0f, &set);
      if (set) cheats::request_counter(rows[r].total, g_counters[r * 2 + 1].v);
      ImGui::PopID();
    }
    ImGui::EndTable();
  }
  ImGui::EndDisabled();
  const int ratio_c = c.shots_chapter ? c.hits_chapter * 100 / c.shots_chapter : 0;
  const int ratio_t = c.shots_total ? c.hits_total * 100 / c.shots_total : 0;
  ImGui::TextDisabled("Hit ratio: %d%% this chapter, %d%% in all", ratio_c, ratio_t);
  help_marker(
      "What the results screen at the end of each chapter (and at the end of the game) shows: this chapter's and "
      "the whole game's deaths, enemies killed and hit ratio (hits of shots fired). Your saves keep them.");
}

// --- the attache case, the item box, the key items, the files and the treasures ----------
// A row being edited: the edit lives here until Apply (and is let go when the
// panel goes, so a reopened panel shows the case as it is).
struct RowEdit {
  bool dirty = false;
  int id = 0;
  int num = 0;
  int ammo = 0;
  int levels[4] = {};
};
RowEdit g_edits[game::kMaxItems];

// A count being typed in the item box, by item (all of an item is one entry
// there, but a weapon, which has no count): kept until Apply, and let go when
// the entry's count changes under it, or when the panel goes.
struct BoxEdit {
  bool dirty = false;
  int from = 0;  // the entry's count when the typing began
  int num = 0;
};
BoxEdit g_box_edits[items::kCount];
BoxEdit* box_edit(int id) { return id >= 0 && id < items::kCount ? &g_box_edits[id] : nullptr; }

// The confirmations.
enum class Ask { kNone, kApply, kRemove, kAdd, kBoxCount, kDiscardBox };
Ask g_ask = Ask::kNone;
inventory::Row g_ask_row;
RowEdit g_ask_edit;
int g_ask_box = -1;
itembox::Entry g_ask_entry;
int g_ask_box_num = 0;                     // the count a box entry's Apply asks about
int g_ask_add_id = -1, g_ask_add_num = 1;  // the item an Add asks about, and how many

void forget_item_dialogs() {
  for (RowEdit& e : g_edits) e.dirty = false;
  for (BoxEdit& e : g_box_edits) e.dirty = false;
  g_ask = Ask::kNone;
}

const char* case_size_name(int s) {
  static const char* const kNames[] = {"S (10x6)", "M (11x7)", "L (12x8)", "XL (15x8)"};
  return s >= 0 && s < 4 ? kNames[s] : "?";
}

// "x12" for a stack, "13 rounds" for a weapon, "" for a single item.
void quantity(const inventory::Row& r, char* out, size_t n) {
  if (r.weapon) {
    if (r.capacity == 0x8000) std::snprintf(out, n, "infinite");
    else std::snprintf(out, n, "%d/%d", r.ammo, r.capacity);
  } else if (r.stack_max > 1 || r.num > 1) {
    std::snprintf(out, n, "x%d", r.num);
  } else {
    out[0] = '\0';
  }
}

// One item of the case, the key items, the files or the treasures, with its
// editor. Only a piece of the case (`piece`) can go in the item box: what lives
// off the grid takes no room in the case. `box` is the game's box, or null in a
// game with none (inventory::box_of): no Store there.
void item_row(const inventory::Row& r, bool editable, bool piece, const itembox::View* box) {
  ImGui::PushID(r.index);
  RowEdit& e = g_edits[r.index & (game::kMaxItems - 1)];
  if (e.dirty && e.id != r.id) e.dirty = false;
  if (!e.dirty) {
    e.id = r.id;
    e.num = r.num;
    e.ammo = r.ammo;
    for (int s = 0; s < 4; ++s) e.levels[s] = r.levels[s];
  }
  char lb[32];
  ImGui::TableNextRow();
  ImGui::TableNextColumn();
  ImGui::TextUnformatted(item_label(r.id, lb, sizeof(lb)));
  if (r.equipped) {
    ImGui::SameLine();
    ImGui::TextColored(kOrange, "E");
    item_help("The weapon in your hands. Storing or throwing it away unequips it.");
  }
  if (!r.onboard && !r.weapon && items::in_case(r.id)) {
    ImGui::SameLine();
    ImGui::TextDisabled("(off the grid)");
  }

  // The count, or a weapon's rounds and levels.
  ImGui::TableNextColumn();
  ImGui::BeginDisabled(!editable);
  if (r.weapon) {
    if (r.capacity == 0x8000) {
      ImGui::TextDisabled("infinite");
    } else {
      ImGui::SetNextItemWidth(scaled(70.0f));
      if (ImGui::InputInt("##ammo", &e.ammo, 0, 0)) e.dirty = true;
      const int cap = game::charge(r.id, e.levels[3]);
      if (e.ammo < 0) e.ammo = 0;
      if (cap > 0 && cap != 0x8000 && e.ammo > cap) e.ammo = cap;
      if (ImGui::IsItemHovered() && ImGui::BeginTooltip()) {
        ImGui::Text("Rounds loaded - up to %d at capacity level %d", cap, e.levels[3]);
        ImGui::EndTooltip();
      }
    }
    static const char* const kStat[4] = {"F", "S", "R", "C"};
    static const char* const kStatName[4] = {"Firepower", "Firing speed", "Reload speed", "Capacity"};
    for (int s = 0; s < 4; ++s) {
      ImGui::SameLine();
      ImGui::PushID(s);
      ImGui::SetNextItemWidth(scaled(34.0f));
      char lab[8];
      std::snprintf(lab, sizeof(lab), "%s", kStat[s]);
      if (ImGui::InputInt(lab, &e.levels[s], 0, 0)) e.dirty = true;
      if (e.levels[s] < 1) e.levels[s] = 1;
      if (e.levels[s] > r.max_levels[s]) e.levels[s] = r.max_levels[s];
      if (ImGui::IsItemHovered() && ImGui::BeginTooltip()) {
        const int extra = items::exclusive(r.id, s);
        ImGui::Text("%s level, 1 to %d%s", kStatName[s], r.max_levels[s],
                    extra ? " (the last is the merchant's exclusive upgrade)" : "");
        ImGui::EndTooltip();
      }
      ImGui::PopID();
    }
  } else if (r.stack_max > 1) {
    ImGui::SetNextItemWidth(scaled(80.0f));
    if (ImGui::InputInt("##num", &e.num, 0, 0)) e.dirty = true;
    if (e.num < 1) e.num = 1;
    if (e.num > r.stack_max) e.num = r.stack_max;
    if (ImGui::IsItemHovered() && ImGui::BeginTooltip()) {
      ImGui::Text("Up to %d in one %s", r.stack_max, items::in_case(r.id) ? "piece" : "entry");
      ImGui::EndTooltip();
    }
  } else if (piece) {
    ImGui::TextDisabled("-");
  }
  ImGui::EndDisabled();

  ImGui::TableNextColumn();
  ImGui::BeginDisabled(!editable);
  if (e.dirty) {
    if (ImGui::SmallButton("Apply")) {
      g_ask = Ask::kApply;
      g_ask_row = r;
      g_ask_edit = e;
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Undo")) e.dirty = false;
    ImGui::SameLine();
  }
  if (piece && box) {
    ImGui::BeginDisabled(!box->usable);
    if (ImGui::SmallButton("Store")) inventory::request_store(r);
    ImGui::EndDisabled();
    item_help(box->usable ? "Put all of it in the item box (below)."
                          : "The item box's file could not be read as the game started - see the log.");
    ImGui::SameLine();
  }
  if (ImGui::SmallButton("x")) {
    g_ask = Ask::kRemove;
    g_ask_row = r;
  }
  item_help(piece                          ? "Throw it away."
            : list_of(r.id) != kKeyItems ? "Remove it."
                                         : "Remove it. A key item the story still needs is gone until you add it back.");
  ImGui::EndDisabled();
  ImGui::PopID();
}

// `box`: the game's item box, or null in a game with none.
void draw_case(const inventory::View& v, const itembox::View* box) {
  ImGui::SeparatorText("Attache case");
  help_marker(
      "The attache case of the character you are playing, straight from the game. Change a count, a weapon's loaded "
      "rounds or its upgrade levels (F firepower, S firing speed, R reload speed, C capacity) and press Apply; "
      "Store puts the item in this game's item box (Assignment Ada and the Mercenaries have none); x throws it away. "
      "New ones come in from Add to the case, below.\n\n"
      "The game's own case screen shows every change the next time you open it.");
  if (!v.known) {
    ImGui::TextColored(kOrange, "The game's items were not found in this version - see the log.");
    return;
  }
  if (v.why[0] && !v.editable) ImGui::TextDisabled("(%s)", v.why);
  ImGui::Text("%s's case, size %s - %d item%s", game::character_name(v.character), case_size_name(v.case_size), v.ncase,
              v.ncase == 1 ? "" : "s");
  item_help("The case's size is the game's own: which Attache Case it finds you hold (S, M, L or XL - the merchant "
            "sells the bigger ones). It is the case's, not an item, so no list here has it.");
  if (v.ncase && ImGui::BeginTable("case", 3, ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_RowBg)) {
    ImGui::TableSetupColumn("item");
    ImGui::TableSetupColumn("count / rounds, levels");
    ImGui::TableSetupColumn("");
    for (int i = 0; i < v.ncase; ++i) item_row(v.in_case[i], v.editable, true, box);
    ImGui::EndTable();
  }
}

// An item picker: a search box, tabs for the banks (or not), the items under
// their groups' headings, how many, and Add - the Add list's. Three of them, one
// for each place an item goes, each offering only its own: the attache case's
// (Add to the case), and the key items', the files' and the treasures', which list
// only what they can still take.
struct Picker {
  const char* id;          // its ImGui ids
  char query[48] = {};
  int tab = -1;            // the bank shown, -1 every one (with tabs)
  int want_tab = -2;       // a tab to bring forward next frame (-2 none)
  int listed_tab = -1;     // the tab the list was last drawn for
  bool to_pick = false;    // scroll the list to the item picked
  int pick = -1, num = 1;  // the item picked (-1 none: 0 is Magnum Ammo) and how many
};
Picker g_add{"add"}, g_key_add{"key add"}, g_file_add{"file add"}, g_treasure_add{"treasure add"};
using Accept = bool (*)(int id, const inventory::View& v);

void ask_add(const Picker& p) {
  g_ask = Ask::kAdd;
  g_ask_add_id = p.pick;
  g_ask_add_num = p.num;
}

// How many one piece in the case (or one entry off it) holds, at the
// difficulty being played.
int piece_max(int id, int difficulty) {
  const int kind = game::item_kind(id);
  const items::Def* d = items::find(id);
  const int max = items::counts(kind >= 0 ? kind : d ? d->kind : 23, difficulty >= 0 ? difficulty : 5).max;
  return max > 0 ? max : 1;
}

void draw_picker(Picker& p, const inventory::View& v, bool tabs, Accept accept, float rows, const char* help) {
  const ImGuiStyle& style = ImGui::GetStyle();
  char id[64];
  const float clear_w = p.query[0] ? ImGui::CalcTextSize("Clear").x + style.FramePadding.x * 2.0f + style.ItemSpacing.x
                                   : 0.0f;
  ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - clear_w - ImGui::CalcTextSize("(?)").x -
                          style.ItemSpacing.x);
  std::snprintf(id, sizeof(id), "##%s search", p.id);
  const bool enter = ImGui::InputTextWithHint(id, "search: part of a name, or an item number", p.query, sizeof(p.query),
                                              ImGuiInputTextFlags_EnterReturnsTrue);
  bool edited = ImGui::IsItemEdited();
  if (p.query[0]) {
    ImGui::SameLine();
    std::snprintf(id, sizeof(id), "Clear##%s", p.id);
    if (ImGui::SmallButton(id)) {
      p.query[0] = '\0';
      edited = true;
    }
  }
  help_marker(help);

  // How many items match under each tab, which tabs have any at all, and
  // whether there is more than one heading's worth (the files are one).
  int matched[items::kBanks] = {}, offers[items::kBanks] = {}, total = 0, one_group = -1;
  bool headings = false;
  for (const short i : items::kByRank) {
    const items::Def& d = items::kItems[i];
    if (!accept(i, v)) continue;
    ++offers[d.bank];
    if (one_group < 0) one_group = d.group;
    else if (d.group != one_group) headings = true;
    if (item_matches(i, d.name, p.query)) {
      ++matched[d.bank];
      ++total;
    }
  }
  if (!tabs) p.tab = -1;
  std::snprintf(id, sizeof(id), "%s banks", p.id);
  if (tabs && ImGui::BeginTabBar(id)) {
    for (int k = -1; k < items::kBanks; ++k) {
      if (k >= 0 && !offers[k]) continue;  // a bank the list has nothing of (the case's: the treasures)
      char tab[48];
      const char* name = k < 0 ? "All" : items::bank_name(k);
      if (p.query[0]) std::snprintf(tab, sizeof(tab), "%s %d###%s%d", name, k < 0 ? total : matched[k], p.id, k + 1);
      else std::snprintf(tab, sizeof(tab), "%s###%s%d", name, p.id, k + 1);
      if (ImGui::BeginTabItem(tab, nullptr, k == p.want_tab ? ImGuiTabItemFlags_SetSelected : 0)) {
        p.tab = k;
        ImGui::EndTabItem();
      }
    }
    p.want_tab = -2;
    ImGui::EndTabBar();
  }

  // The list, under its headings; it keeps its height while it is narrowed.
  const int tab = p.tab;
  int first = -1;
  std::snprintf(id, sizeof(id), "##%s list", p.id);
  if (ImGui::BeginChild(id, ImVec2(-FLT_MIN, ImGui::GetTextLineHeightWithSpacing() * rows + style.WindowPadding.y * 2.0f),
                        ImGuiChildFlags_Borders)) {
    if (edited || tab != p.listed_tab) ImGui::SetScrollY(0.0f);  // a new search or tab starts at the top
    p.listed_tab = tab;
    int group = -1;
    for (const short i : items::kByRank) {
      const items::Def& d = items::kItems[i];
      if (!accept(i, v) || (tab >= 0 && d.bank != tab) || !item_matches(i, d.name, p.query)) continue;
      if (first < 0) first = i;
      if (headings && d.group != group) {
        group = d.group;
        ImGui::SeparatorText(items::kGroups[group].name);
      }
      ImGui::PushID(i);
      if (ImGui::Selectable(d.name, i == p.pick, ImGuiSelectableFlags_AllowDoubleClick)) {
        if (i != p.pick) p.num = 1;
        p.pick = i;
        if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && v.editable) ask_add(p);
      }
      if (i == p.pick && p.to_pick) {
        ImGui::SetScrollHereY(0.5f);
        p.to_pick = false;
      }
      ImGui::PopID();
    }
    p.to_pick = false;  // the pick is not in this list
    if (first < 0 && total > 0) {
      ImGui::TextDisabled("Nothing under %s matches \"%s\".", items::bank_name(tab), p.query);
      char all[32];
      std::snprintf(all, sizeof(all), "Show all %d", total);
      if (ImGui::SmallButton(all)) p.want_tab = -1;
    } else if (first < 0 && p.query[0]) {
      ImGui::TextDisabled("Nothing matches \"%s\".", p.query);
    } else if (first < 0) {
      ImGui::TextDisabled("Nothing to add - you have them all.");
    }
  }
  ImGui::EndChild();
  if (enter && first >= 0) {  // Enter picks the first match
    if (first != p.pick) p.num = 1;
    p.pick = first;
    p.to_pick = true;
  }

  // The item picked, how many, and Add. A pick the list no longer offers (it
  // was added, or the search moved on) is let go.
  if (p.pick >= 0 && !accept(p.pick, v)) p.pick = -1;
  const items::Def* d = items::find(p.pick);
  if (!d) {
    ImGui::TextDisabled("Pick an item in the list (a double-click adds it).");
    return;
  }
  ImGui::BeginDisabled(!v.editable);
  ImGui::AlignTextToFramePadding();
  ImGui::TextUnformatted(d->name);
  const int max = piece_max(p.pick, v.difficulty);
  ImGui::SameLine();
  ImGui::BeginDisabled(max <= 1);
  ImGui::SetNextItemWidth(scaled(70.0f));
  std::snprintf(id, sizeof(id), "##%s num", p.id);
  ImGui::InputInt(id, &p.num, 0, 0);
  if (p.num < 1) p.num = 1;
  if (p.num > max) p.num = max;
  ImGui::EndDisabled();
  if (max > 1) item_help("How many: up to what one piece or entry of it holds, for your difficulty.");
  ImGui::SameLine();
  std::snprintf(id, sizeof(id), "Add##%s", p.id);
  if (ImGui::SmallButton(id)) ask_add(p);
  ImGui::EndDisabled();
}

// The case's Add list: what takes room in the attache case - every item with a
// piece for it - under a tab for its bank and a heading for its group (the item
// table's filing), with a search over all of it. What lives off the grid is
// added from its own list (draw_offgrid).
bool accept_case(int id, const inventory::View&) {
  const items::Def& d = items::kItems[id];
  return d.offered && d.w;
}

// Inside the case's section, which says why nothing can be added while it can
// not.
void draw_add(const inventory::View& v) {
  draw_picker(g_add, v, true, accept_case, 10.5f,
              "What takes room in the attache case - weapons, ammo, grenades, recovery items, attachments, and "
              "Assignment Ada's Plaga Samples (filed with the key items) - under a tab for its kind and a heading for "
              "what it is. Type in the search box to keep only the items whose name holds every word you type (or an "
              "item number); while you search, each tab says how many of its items match. Pick an item and press Add, "
              "or double-click it: it goes into the case where the game's own placement finds it room (turning it if it "
              "has to) - or, with no room, nowhere. A new weapon arrives loaded, at level 1.\n\n"
              "Key items, maps, files and treasures take no room in the case: the Key items, Files and Treasures "
              "sections below add them.");
  const items::Def* d = items::find(g_add.pick);
  if (d) ImGui::TextDisabled("Goes into the attache case, if the game finds room for its %dx%d piece.", d->w, d->h);
}

// The items off the case's grid, in lists of their own (OffGrid): the key
// items and maps, the files, and the treasures. They take no room in the case,
// so the item box is not for them: x removes, and each list adds what it lacks
// with a picker of its own, folding inside the list's section (as Add to the
// case does inside the case's). The Attache Case S..XL items are the case's
// size (items::is_case) - shown with the case, in no list.
bool in_list(int id, int list) { return !items::is_case(id) && list_of(id) == list; }
int held_num(const inventory::View& v, int id) {
  int n = 0;
  for (int i = 0; i < v.nkey; ++i)
    if (v.key[i].id == id) n += v.key[i].num;
  return n;
}
int count_in(const inventory::View& v, int list) {
  int n = 0;
  for (int i = 0; i < v.nkey; ++i) n += in_list(v.key[i].id, list) ? 1 : 0;
  return n;
}
// What each list's picker offers: an item of the list that takes no room in the
// case, and that you do not have - or, a treasure or a gem, of which you hold
// less than one entry holds.
bool accept_key(int id, const inventory::View& v) {
  const items::Def& d = items::kItems[id];
  return d.offered && !d.w && in_list(id, kKeyItems) && !held_num(v, id);
}
bool accept_file(int id, const inventory::View& v) {
  const items::Def& d = items::kItems[id];
  return d.offered && !d.w && in_list(id, kFiles) && !held_num(v, id);
}
bool accept_treasure(int id, const inventory::View& v) {
  const items::Def& d = items::kItems[id];
  return d.offered && !d.w && in_list(id, kTreasures) && held_num(v, id) < piece_max(id, v.difficulty);
}

void draw_offgrid(const inventory::View& v, int list) {
  if (!v.known) return;
  if (!v.editable && v.why[0]) ImGui::TextDisabled("(%s)", v.why);
  // What you carry, in the Add list's order under its headings.
  int order[inventory::kMaxKey];
  int n = 0;
  for (int i = 0; i < v.nkey; ++i)
    if (in_list(v.key[i].id, list)) order[n++] = i;
  const auto rank = [&](int i) {
    const items::Def* d = items::find(v.key[i].id);
    return d ? d->rank : items::kCount + v.key[i].id;
  };
  for (int i = 1; i < n; ++i)
    for (int j = i; j > 0 && rank(order[j]) < rank(order[j - 1]); --j) {
      const int t = order[j];
      order[j] = order[j - 1];
      order[j - 1] = t;
    }
  int groups = 0;
  for (int k = 0, g = -1; k < n; ++k) {
    const items::Def* d = items::find(v.key[order[k]].id);
    const int gk = d ? d->group : -2;
    if (gk != g) ++groups, g = gk;
  }
  // The files are one heading's worth: the section's own says it.
  const bool headings = list != kFiles;
  if (!headings) groups = 0;
  if (!n) {
    ImGui::TextDisabled("You carry no %s.", list == kKeyItems ? "key items or maps" : list_name(list));
  } else {
    // As tall as it is (its rows with a count field are taller than a line of
    // text), and past a dozen lines, a dozen and a scroll bar.
    const bool scroll = n + groups > 12;
    if (ImGui::BeginTable(list_name(list), 3,
                          ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_RowBg | (scroll ? ImGuiTableFlags_ScrollY : 0),
                          ImVec2(0.0f, scroll ? ImGui::GetFrameHeightWithSpacing() * 12.5f : 0.0f))) {
      ImGui::TableSetupColumn("item");
      ImGui::TableSetupColumn("count");
      ImGui::TableSetupColumn("");
      int g = -1;
      for (int k = 0; k < n; ++k) {
        const inventory::Row& r = v.key[order[k]];
        const items::Def* d = items::find(r.id);
        const int gk = d ? d->group : -2;
        if (headings && gk != g) {
          g = gk;
          ImGui::TableNextRow();
          ImGui::TableNextColumn();
          ImGui::TextDisabled("%s", d ? items::kGroups[gk].name : "Other");
        }
        item_row(r, v.editable, false, nullptr);
      }
      ImGui::EndTable();
    }
  }
}

// A list's Add, inside its section (which says why nothing can be added while
// it can not).
void draw_offgrid_add(const inventory::View& v, int list) {
  if (list == kKeyItems) {
    draw_picker(g_key_add, v, false, accept_key, 7.5f,
                "The key items and maps you do not have yet, in the story's order. Pick one and press Add, or "
                "double-click it: it joins the list above, as a pickup of it would.");
  } else if (list == kFiles) {
    draw_picker(g_file_add, v, false, accept_file, 7.5f,
                "The files you do not have yet, in the story's order. Pick one and press Add, or double-click it: it "
                "joins the list above, as a pickup of it would.");
  } else {
    draw_picker(g_treasure_add, v, false, accept_treasure, 7.5f,
                "The treasures, gems and bottle caps you do not have yet, and the ones you hold fewer of than one entry "
                "holds (a treasure added to one you carry is added to its count). Pick one, say how many, and press Add, "
                "or double-click it: it joins the list above. The shooting gallery counts the bottle caps you hold only "
                "while you hold the Bottle Caps collection too.");
  }
}

// The item box (itembox.h) of the game being played: the mod's own, kept
// outside the game in a file of its own - the main game's, or Separate Ways' -
// one box for every save and new game of it, with no limit. It keeps itself in
// the panel's order, so a bank is a run of it and an index drawn here is the
// box's own.
void draw_box(const inventory::View& v, const itembox::View& box) {
  char lb[32];
  ImGui::TextDisabled(box.which == itembox::Box::kSeparateWays
                          ? "Separate Ways' own box: one for all its saves, kept outside the game."
                          : "The main game's box: one for all its saves, kept outside the game.");
  help_marker(
      "An item box of the mod's own (RE4 has none), with no limit on what it holds, for what takes room in the "
      "attache case. Store, beside an item of the case, puts all of it in the box; Take puts it back where the game's "
      "own placement finds it room - topping up what you carry of it first, turning a new piece if it has to. "
      "Everything of one item is one entry however much of it you store, and Take hands over one piece's worth at a "
      "time; a weapon keeps an entry of its own, with its upgrade levels and the rounds it has loaded. Change an "
      "entry's count and press Apply to set it to any number (a weapon's entry is that one weapon, with no count); "
      "x throws an entry away. Key items, files and treasures take no room in the case: they have lists of their own "
      "below.\n\n"
      "The main game and Separate Ways each have a box of their own, a file beside the mod "
      "(RE4OGCabbyCodes.itembox.ini, RE4OGCabbyCodes.itembox.separate-ways.ini) and not part of any save: every save "
      "file and new game of the main game sees the main game's box, and likewise for Separate Ways. Nothing the game "
      "does changes a box - not a save, a load, a new game, nor a death and its continue; only Store, Take, a count "
      "you set and x do, each written to the file at once. Assignment Ada and the Mercenaries, played in one sitting "
      "with no saves, have no box.\n\n"
      "The game keeps the attache case as it is only when you save: an item you take out and then lose with the "
      "game (you quit without saving, or continue from before you took it) is gone, and one you store and then do "
      "the same with is in both places.");
  if (!box.usable) {
    ImGui::PushStyleColor(ImGuiCol_Text, kOrange);
    ImGui::TextWrapped("%s is beside the mod but could not be read as the game started, so the box is left alone "
                       "until the next start - see the log.",
                       itembox::file_name(box.which));
    ImGui::PopStyleColor();
    return;
  }
  if (box.write_failed) {
    ImGui::PushStyleColor(ImGuiCol_Text, kOrange);
    ImGui::TextWrapped("%s could not be written at the last change - see the log.", itembox::file_name(box.which));
    ImGui::PopStyleColor();
  }
  const int n = static_cast<int>(box.entry.size());
  static int shown_bank = items::kBankWeapons;
  static char filter[48] = {};
  int held[items::kBanks] = {}, matched[items::kBanks] = {};
  for (const itembox::Entry& e : box.entry) {
    ++held[bank_of(e.id)];
    if (item_matches(e.id, item_label(e.id, lb, sizeof(lb)), filter)) ++matched[bank_of(e.id)];
  }
  if (ImGui::BeginTabBar("banks")) {
    for (int k = 0; k < items::kBanks; ++k) {
      // The case's own banks always; another one (Assignment Ada's Plaga
      // Samples, filed with the key items) while the box holds something of it.
      if (k > items::kBankRecovery && !held[k]) continue;
      char tab[48];
      std::snprintf(tab, sizeof(tab), "%s %d###bank%d", items::bank_name(k), held[k], k);
      if (ImGui::BeginTabItem(tab)) {
        shown_bank = k;
        ImGui::EndTabItem();
      }
    }
    ImGui::EndTabBar();
  }
  const int bank = shown_bank;
  if (n > 0) {
    ImGui::SetNextItemWidth(scaled(230.0f));
    ImGui::InputTextWithHint("##box filter", "type to narrow the list", filter, sizeof(filter));
    if (filter[0]) {
      ImGui::SameLine();
      if (ImGui::SmallButton("Clear")) filter[0] = '\0';
      ImGui::SameLine();
      ImGui::TextDisabled("%d of %d shown", matched[bank], held[bank]);
    }
  }
  char table_id[16];  // each bank keeps its own scroll position
  std::snprintf(table_id, sizeof(table_id), "box%d", bank);
  // As tall as its first ten rows (a count's field is taller than a weapon's
  // line of text) and half a row more: past ten, the half row and the scroll
  // bar say there is more.
  float height = 0.0f, last = 0.0f;
  int rows = 0;
  for (const itembox::Entry& e : box.entry) {
    if (bank_of(e.id) != bank || !item_matches(e.id, item_label(e.id, lb, sizeof(lb)), filter)) continue;
    last = e.weapon ? ImGui::GetTextLineHeightWithSpacing() : ImGui::GetFrameHeightWithSpacing();
    height += rows++ < 10 ? last : last * 0.5f;
    if (rows > 10) break;
  }
  if (rows <= 10) height += last * 0.5f;
  if (n == 0) {
    ImGui::TextDisabled("The box is empty - Store, beside an item of the attache case, puts it in.");
  } else if (held[bank] == 0) {
    ImGui::TextDisabled("Nothing in the box is filed under %s.", items::bank_name(bank));
  } else if (matched[bank] == 0) {
    ImGui::TextDisabled("nothing under %s matches \"%s\"", items::bank_name(bank), filter);
  } else if (ImGui::BeginTable(table_id, 3, ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg,
                               ImVec2(0.0f, height))) {
    ImGui::TableSetupColumn("item");
    ImGui::TableSetupColumn("count");
    ImGui::TableSetupColumn("");
    for (int i = 0; i < n; ++i) {
      const itembox::Entry& e = box.entry[i];
      const char* label = item_label(e.id, lb, sizeof(lb));
      if (bank_of(e.id) != bank || !item_matches(e.id, label, filter)) continue;
      ImGui::PushID(i);
      ImGui::TableNextRow();
      ImGui::TableNextColumn();
      ImGui::TextUnformatted(label);
      // What a Take hands over: a weapon whole, of anything else one piece's
      // (or one entry's) worth; an item with no piece the case cannot take.
      const int max = piece_max(e.id, v.difficulty);
      const bool to_case = inventory::goes_in_case(e.id);
      const bool fits = !to_case || items::in_case(e.id);
      const int take = e.weapon ? 1 : e.num < max ? e.num : max;

      // A weapon's levels and rounds; of anything else the count, which is the
      // box's own (any number, with no piece's limit), kept as typed until Apply.
      ImGui::TableNextColumn();
      BoxEdit* be = e.weapon ? nullptr : box_edit(e.id);
      if (e.weapon) {
        int lv[4];
        inventory::levels_of(e.levels, lv);
        ImGui::Text("%d rounds, F%d S%d R%d C%d", e.ammo, lv[0], lv[1], lv[2], lv[3]);
      } else if (!be) {
        ImGui::Text("x%d", e.num);
      } else {
        if (be->dirty && be->from != e.num) be->dirty = false;  // the count changed under the edit
        if (!be->dirty) be->from = be->num = e.num;
        ImGui::BeginDisabled(!v.editable);
        ImGui::SetNextItemWidth(scaled(90.0f));
        if (ImGui::InputInt("##num", &be->num, 0, 0)) be->dirty = true;
        if (be->num < 1) be->num = 1;
        if (be->num > itembox::kMaxCount) be->num = itembox::kMaxCount;
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled) && ImGui::BeginTooltip()) {
          ImGui::PushTextWrapPos(scaled(380.0f));
          ImGui::TextUnformatted("How many the box holds.");
          ImGui::TextUnformatted("Type a new count and press Apply.");
          if (fits) ImGui::TextDisabled("Take hands over %d at a time.", max);
          ImGui::PopTextWrapPos();
          ImGui::EndTooltip();
        }
      }

      ImGui::TableNextColumn();
      ImGui::BeginDisabled(!v.editable);
      if (be && be->dirty && be->num != e.num) {
        if (ImGui::SmallButton("Apply")) {
          g_ask = Ask::kBoxCount;
          g_ask_box = i;
          g_ask_entry = e;
          g_ask_box_num = be->num;
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("Undo")) be->dirty = false;
        ImGui::SameLine();
      }
      ImGui::EndDisabled();
      ImGui::BeginDisabled(!v.editable || !fits);
      if (ImGui::SmallButton("Take")) inventory::request_take(i, e);
      ImGui::EndDisabled();
      if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled) && ImGui::BeginTooltip()) {
        ImGui::PushTextWrapPos(scaled(380.0f));
        if (!fits)
          ImGui::TextUnformatted("It has no piece for the attache case, so the case cannot take it.");
        else if (e.weapon)
          ImGui::TextUnformatted("Put it back in the attache case where the game finds it room (turning it if it has "
                                 "to), with its levels and loaded rounds.");
        else if (!to_case)
          ImGui::Text("Put %d with the %s.", take, list_name(list_of(e.id)));
        else
          ImGui::Text("Put %d in the attache case: the game tops up the %s you carry first, then finds room for a new "
                      "piece, turning it if it has to.",
                      take, label);
        if (!e.weapon && e.num > take)
          ImGui::TextDisabled("One %s holds %d, so %d stay in the box - Take again for more.", to_case ? "piece" : "entry",
                              max, e.num - take);
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
      }
      ImGui::SameLine();
      ImGui::BeginDisabled(!v.editable);
      if (ImGui::SmallButton("x")) {
        g_ask = Ask::kDiscardBox;
        g_ask_box = i;
        g_ask_entry = e;
      }
      ImGui::EndDisabled();
      item_help("Throw it out of the box.");
      ImGui::PopID();
    }
    ImGui::EndTable();
  }
}

// The confirmations, asked outside the tables so a dialog does not belong to a
// row that may be gone by the time it is answered.
void draw_item_dialogs(const inventory::View& v) {
  char lb[32], q[32];
  static Ask open = Ask::kNone;
  if (g_ask != Ask::kNone && open == Ask::kNone) {
    open = g_ask;
    ImGui::OpenPopup("item change?");
  }
  if (!ImGui::BeginPopupModal("item change?", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
    if (open != Ask::kNone && !ImGui::IsPopupOpen("item change?")) open = g_ask = Ask::kNone;
    return;
  }
  bool close = !v.editable;
  const char* name = item_label(g_ask_row.id, lb, sizeof(lb));
  switch (open) {
    case Ask::kApply: {
      inventory::Row r = g_ask_row;
      if (r.weapon) {
        ImGui::Text("Set %s to %d round(s), firepower %d, firing speed %d, reload speed %d, capacity %d?", name,
                    g_ask_edit.ammo, g_ask_edit.levels[0], g_ask_edit.levels[1], g_ask_edit.levels[2],
                    g_ask_edit.levels[3]);
      } else {
        ImGui::Text("Set %s to %d?", name, g_ask_edit.num);
      }
      ImGui::TextDisabled("This writes straight into the game's items.");
      if (ImGui::Button("Apply")) {
        if (r.weapon) inventory::request_weapon(r, g_ask_edit.levels, g_ask_edit.ammo);
        else inventory::request_count(r, g_ask_edit.num);
        g_edits[r.index & (game::kMaxItems - 1)].dirty = false;
        close = true;
      }
      break;
    }
    case Ask::kRemove: {
      // A piece of the case is thrown away; a key item, file or treasure removed.
      const bool piece = inventory::goes_in_case(g_ask_row.id);
      quantity(g_ask_row, q, sizeof(q));
      ImGui::Text("%s %s%s%s?", piece ? "Throw away" : "Remove", name, q[0] ? " " : "", q);
      ImGui::TextDisabled("It is gone for good once the game is saved.");
      if (ImGui::Button(piece ? "Throw away" : "Remove")) {
        inventory::request_remove(g_ask_row);
        close = true;
      }
      break;
    }
    case Ask::kAdd: {
      ImGui::Text("Add %s x%d?", items::name(g_ask_add_id), g_ask_add_num);
      if (inventory::goes_in_case(g_ask_add_id))
        ImGui::TextDisabled("It goes into the attache case where the game finds it room, as a pickup would.");
      else
        ImGui::TextDisabled("It goes with the %s, as a pickup would.", list_name(list_of(g_ask_add_id)));
      if (ImGui::Button("Add")) {
        inventory::request_add(g_ask_add_id, g_ask_add_num);
        close = true;
      }
      break;
    }
    case Ask::kBoxCount: {
      const itembox::Entry& e = g_ask_entry;
      ImGui::Text("Set %s in the item box to %d?", item_label(e.id, lb, sizeof(lb)), g_ask_box_num);
      if (g_ask_box_num < e.num)
        ImGui::TextDisabled("It holds %d now: the other %d are gone for good.", e.num, e.num - g_ask_box_num);
      else
        ImGui::TextDisabled("It holds %d now.", e.num);
      if (ImGui::Button("Apply")) {
        inventory::request_box_count(g_ask_box, e, g_ask_box_num);
        if (BoxEdit* be = box_edit(e.id)) be->dirty = false;
        close = true;
      }
      break;
    }
    case Ask::kDiscardBox: {
      const itembox::Entry& e = g_ask_entry;
      const char* what = item_label(e.id, lb, sizeof(lb));
      if (e.weapon) {
        int lv[4];
        inventory::levels_of(e.levels, lv);
        ImGui::Text("Throw away %s (%d rounds, F%d S%d R%d C%d) from the item box?", what, e.ammo, lv[0], lv[1], lv[2],
                    lv[3]);
      } else {
        ImGui::Text("Throw away %s x%d from the item box?", what, e.num);
      }
      ImGui::TextDisabled("It is gone for good.");
      if (ImGui::Button("Throw away")) {
        inventory::request_discard(g_ask_box, g_ask_entry);
        close = true;
      }
      break;
    }
    default:
      close = true;
      break;
  }
  if (!close) {
    ImGui::SameLine();
    if (ImGui::Button("Cancel")) close = true;
  }
  if (close) {
    open = g_ask = Ask::kNone;
    ImGui::CloseCurrentPopup();
  }
  ImGui::EndPopup();
}

// --- the save files ----------------------------------------------------------------------
enum { kAskNone = 0, kAskDelete, kAskCopy };
int g_file_ask = kAskNone;
int g_file_from = -1, g_file_to = -1;
savefiles::Save g_file_from_seen, g_file_to_seen;

void forget_file_dialogs() {
  g_file_ask = kAskNone;
  g_file_from = g_file_to = -1;
}

void draw_save_files() {
  const savefiles::View v = savefiles::view();
  bool open_delete = false, open_copy = false;

  ImGui::SeparatorText("Save files");
  help_marker(
      "The game's twenty save files, all kept in one file in Steam Cloud (savegame00.sav).\n\n"
      "Delete removes a save: the game's list shows it as empty from then on, and your next save there fills it "
      "again. Copy to... puts a copy of a save into another file, empty or not.\n\n"
      "Both work while the game's list is waiting for you, and change your saves straight away - the game's own "
      "copy and Steam Cloud's - so neither can be undone. A copy of savegame00.sav as it was is kept first, in "
      "RE4OGCabbyCodes.save-backup beside the mod (the ten newest).");
  if (!v.ready) {
    ImGui::TextColored(kOrange, "The game's save files were not found in this version - see the log.");
    return;
  }
  if (!v.up) {
    ImGui::TextDisabled("(the load or save screen is not up)");
    return;
  }
  if (!v.waiting) ImGui::TextDisabled("Copy and Delete wait for the game's list.");
  const bool can = v.waiting;

  if (ImGui::BeginTable("files", 3, ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_RowBg)) {
    ImGui::TableSetupColumn("file");
    ImGui::TableSetupColumn("save");
    ImGui::TableSetupColumn("");
    ImGui::TableHeadersRow();
    for (int i = 0; i < savefiles::kFiles; ++i) {
      const savefiles::Save& f = v.save[i];
      ImGui::PushID(i);
      ImGui::TableNextRow();
      if (i == v.cursor) ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg1, ImGui::GetColorU32(ImGuiCol_Header));
      ImGui::TableNextColumn();
      ImGui::Text("%d", i + 1);
      ImGui::TableNextColumn();
      char d[160];
      savefiles::describe(f, d, sizeof(d));
      if (f.used) ImGui::TextUnformatted(d);
      else ImGui::TextDisabled("%s", d);
      ImGui::TableNextColumn();
      if (f.used) {
        ImGui::BeginDisabled(!can);
        if (ImGui::SmallButton("Copy to...")) ImGui::OpenPopup("copy to");
        ImGui::SameLine();
        if (ImGui::SmallButton("Delete")) {
          g_file_ask = kAskDelete;
          g_file_to = i;
          g_file_to_seen = f;
          open_delete = true;
        }
        ImGui::EndDisabled();
        if (ImGui::BeginPopup("copy to")) {
          ImGui::TextDisabled("Copy the save in file %d to:", i + 1);
          ImGui::Separator();
          for (int j = 0; j < savefiles::kFiles; ++j) {
            if (j == i) continue;
            const savefiles::Save& t = v.save[j];
            char dj[160], label[200];
            savefiles::describe(t, dj, sizeof(dj));
            std::snprintf(label, sizeof(label), "File %d - %s", j + 1, dj);
            if (ImGui::Selectable(label)) {
              g_file_ask = kAskCopy;
              g_file_from = i;
              g_file_to = j;
              g_file_from_seen = f;
              g_file_to_seen = t;
              open_copy = true;
            }
          }
          ImGui::EndPopup();
        }
      }
      ImGui::PopID();
    }
    ImGui::EndTable();
  }
  if (v.note[0]) {
    if (v.note_error) ImGui::PushStyleColor(ImGuiCol_Text, kOrange);
    ImGui::TextWrapped("%s", v.note);
    if (v.note_error) ImGui::PopStyleColor();
  }

  if (open_delete) ImGui::OpenPopup("delete this save?");
  if (open_copy) ImGui::OpenPopup("copy this save?");
  char d[160];
  if (ImGui::BeginPopupModal("delete this save?", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
    if (g_file_ask != kAskDelete || g_file_to < 0 || !can) {
      forget_file_dialogs();
      ImGui::CloseCurrentPopup();
    } else {
      savefiles::describe(g_file_to_seen, d, sizeof(d));
      ImGui::Text("Delete the save in file %d (%s)?", g_file_to + 1, d);
      ImGui::TextDisabled("Your saves change straight away (a backup of savegame00.sav is kept first).");
      if (ImGui::Button("Delete")) {
        savefiles::request_delete(g_file_to, g_file_to_seen);
        forget_file_dialogs();
        ImGui::CloseCurrentPopup();
      }
      ImGui::SameLine();
      if (ImGui::Button("Cancel")) {
        forget_file_dialogs();
        ImGui::CloseCurrentPopup();
      }
    }
    ImGui::EndPopup();
  }
  if (ImGui::BeginPopupModal("copy this save?", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
    if (g_file_ask != kAskCopy || g_file_from < 0 || g_file_to < 0 || !can) {
      forget_file_dialogs();
      ImGui::CloseCurrentPopup();
    } else {
      savefiles::describe(g_file_from_seen, d, sizeof(d));
      if (g_file_to_seen.used)
        ImGui::Text("Copy the save in file %d (%s) over the save in file %d?", g_file_from + 1, d, g_file_to + 1);
      else
        ImGui::Text("Copy the save in file %d (%s) to the empty file %d?", g_file_from + 1, d, g_file_to + 1);
      if (g_file_to_seen.used) {
        savefiles::describe(g_file_to_seen, d, sizeof(d));
        ImGui::TextDisabled("The save in file %d (%s) is replaced.", g_file_to + 1, d);
      }
      ImGui::TextDisabled("Your saves change straight away (a backup of savegame00.sav is kept first).");
      if (ImGui::Button("Copy")) {
        savefiles::request_copy(g_file_from, g_file_to, g_file_from_seen, g_file_to_seen);
        forget_file_dialogs();
        ImGui::CloseCurrentPopup();
      }
      ImGui::SameLine();
      if (ImGui::Button("Cancel")) {
        forget_file_dialogs();
        ImGui::CloseCurrentPopup();
      }
    }
    ImGui::EndPopup();
  }
}

// A section heading that folds, remembered in the layout file.
bool section(const char* label, bool* open) {
  ImGui::SetNextItemOpen(*open, ImGuiCond_Always);
  const bool now = ImGui::CollapsingHeader(label);
  if (now != *open) {
    *open = now;
    ImGui::MarkIniSettingsDirty();
  }
  return now;
}

// A section within a section (a list's Add): its heading and what it holds set
// in under the parent's, folding on its own.
template <typename Draw>
void subsection(const char* label, bool* open, Draw&& draw) {
  const float indent = ImGui::GetStyle().IndentSpacing;
  ImGui::Spacing();
  ImGui::Indent(indent);
  if (section(label, open)) draw();
  ImGui::Unindent(indent);
}

// The item sections: the attache case, the game's item box (`box`, null in a
// game with none: no section), the key items, the files and the treasures, each
// list with its Add inside it.
void draw_items(const inventory::View& iv, const itembox::View* box) {
  if (section("Attache case###case", &g_case_open)) {
    draw_case(iv, box);
    if (iv.known) subsection("Add to the case###add", &g_add_open, [&] { draw_add(iv); });
  }
  char head[64];
  if (box) {
    std::snprintf(head, sizeof(head), "Item box  (%d)###box", static_cast<int>(box->entry.size()));
    if (section(head, &g_box_open)) draw_box(iv, *box);
  }
  std::snprintf(head, sizeof(head), "Key items  (%d)###key", count_in(iv, kKeyItems));
  if (section(head, &g_key_open)) {
    draw_offgrid(iv, kKeyItems);
    if (iv.known) subsection("Add key items###keyadd", &g_key_add_open, [&] { draw_offgrid_add(iv, kKeyItems); });
  }
  std::snprintf(head, sizeof(head), "Files  (%d)###files", count_in(iv, kFiles));
  if (section(head, &g_files_open)) {
    draw_offgrid(iv, kFiles);
    if (iv.known) subsection("Add files###filesadd", &g_files_add_open, [&] { draw_offgrid_add(iv, kFiles); });
  }
  std::snprintf(head, sizeof(head), "Treasures  (%d)###treasures", count_in(iv, kTreasures));
  if (section(head, &g_treasures_open)) {
    draw_offgrid(iv, kTreasures);
    if (iv.known)
      subsection("Add treasures###treasuresadd", &g_treasures_add_open, [&] { draw_offgrid_add(iv, kTreasures); });
  }
}

// Once, before either window is first drawn (ImGui has read the layout file
// by then). The panel used to be one window for both screens, kept under its
// title, version and all: what the file holds under such a name seeds each of
// the two windows that has nothing of its own yet, and is then dropped. A size
// kept at another scale is brought to this one.
void settle_layout() {
  ImGuiContext& g = *GImGui;
  static const char kOldPrefix[] = "Resident Evil 4 - Cabby Codes  v";
  char old_name[96] = {};
  ImVec2ih pos, size;
  bool collapsed = false;
  for (ImGuiWindowSettings* ws = g.SettingsWindows.begin(); ws; ws = g.SettingsWindows.next_chunk(ws)) {
    const char* n = ws->GetName();
    if (ws->WantDelete || std::strncmp(n, kOldPrefix, sizeof(kOldPrefix) - 1) || std::strstr(n, "###")) continue;
    std::snprintf(old_name, sizeof(old_name), "%s", n);
    pos = ws->Pos;
    size = ws->Size;
    collapsed = ws->Collapsed;
  }
  if (old_name[0]) {
    // (Creating an entry can move the others: the old one's values were copied out first.)
    for (const char* name : {kPanelWindow, kFilesWindow}) {
      if (ImGui::FindWindowSettingsByID(ImHashStr(name))) continue;
      ImGuiWindowSettings* ws = ImGui::CreateNewWindowSettings(name);
      ws->Pos = pos;
      ws->Size = size;
      ws->Collapsed = collapsed;
    }
    ImGui::ClearWindowSettings(old_name);
    ImGui::MarkIniSettingsDirty();
    logf("panel: the layout kept for '%s' now starts the pause panel's and the save files' own", old_name);
  }
  if (g_layout_percent != kScalePercent) {
    const float r = static_cast<float>(kScalePercent) / static_cast<float>(g_layout_percent);
    for (const char* name : {kPanelWindow, kFilesWindow})
      if (ImGuiWindowSettings* ws = ImGui::FindWindowSettingsByID(ImHashStr(name))) {
        logf("panel: a saved size, %dx%d at %d%%, brought to %d%%", ws->Size.x, ws->Size.y, g_layout_percent,
             kScalePercent);
        ws->Size = ImVec2ih(static_cast<short>(ws->Size.x * r), static_cast<short>(ws->Size.y * r));
      }
    g_layout_percent = kScalePercent;
    ImGui::MarkIniSettingsDirty();
  }
}

void draw_panel() {
  const dispatch::Snapshot s = dispatch::snapshot();
  const ImGuiIO& io = ImGui::GetIO();
  // NewFrame has run, so what ImGui wants this frame is settled; the input
  // guard reads these from the game's thread.
  InterlockedExchange(&g_capture_mouse, io.WantCaptureMouse ? 1 : 0);
  InterlockedExchange(&g_capture_keyboard, io.WantTextInput ? 1 : 0);
  if (!g_layout_settled) {
    g_layout_settled = true;
    settle_layout();
  }
  // The load or save list has the save file manager, in a window of its own
  // (nothing else on the panel applies there); everything else is the pause
  // panel. Resizable, not auto-fitted (an auto-fitting window whose wrapped
  // text measures itself against its own width shrinks a little every frame
  // once a scrollbar takes some of it), and never bigger than the display.
  const bool files = s.ready && s.card_list;
  ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x - scaled(600), scaled(40)), ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowSize(ImVec2(scaled(580), scaled(640)), ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowSizeConstraints(ImVec2(scaled(360), scaled(200)), io.DisplaySize);
  if (!ImGui::Begin(files ? kFilesWindow : kPanelWindow, nullptr)) {
    ImGui::End();
    return;
  }
  keep_in_reach(io.DisplaySize);
  if (!s.ready) {
    ImGui::TextColored(kOrange, "Game hooks: %s", game::status_text());
    ImGui::End();
    return;
  }
  if (files) {
    draw_save_files();
    ImGui::Separator();
    ImGui::TextDisabled("F%d hides this panel", config::get().toggle_key - 0x6F);
    ImGui::End();
    return;
  }
  if (!s.run.in_game) {
    ImGui::TextDisabled("Waiting for a game (start a new game or load a save)...");
    ImGui::Separator();
  } else if (s.run.mode != game::Mode::kMain) {
    ImGui::TextDisabled("%s, playing %s", game::mode_name(s.run.mode), game::character_name(s.run.character));
  }

  const cheats::Status st = cheats::status();
  {
    // The switches that are on, counted in their section's heading, so a
    // folded section still says what it holds.
    int on = 0, results_on = 0;
    for (int k = 0; k < cheats::kCount; ++k) {
      const auto kind = static_cast<cheats::Kind>(k);
      if (cheats::enabled(kind)) ++(is_results_switch(kind) ? results_on : on);
    }
    char head[48];
    std::snprintf(head, sizeof(head), "Cheats  (%d on)###cheats", on);
    if (section(head, &g_cheats_open)) draw_cheats(st);
    if (has_results(st)) {
      if (results_on) std::snprintf(head, sizeof(head), "Results  (%d on)###results", results_on);
      else std::snprintf(head, sizeof(head), "Results###results");
      if (section(head, &g_results_open)) draw_results(st);
    }
  }
  if (s.run.in_game) {
    const inventory::View iv = inventory::view();
    itembox::Box which = itembox::Box::kMain;
    const bool boxed = inventory::box_of(s.run.mode, &which);
    const itembox::View box = boxed ? itembox::view(which) : itembox::View{};
    draw_items(iv, boxed ? &box : nullptr);
    draw_item_dialogs(iv);
    // Only what went wrong says so here: what a change did shows in the lists
    // themselves, and the log keeps every one.
    if (iv.note_error && iv.note[0]) {
      ImGui::PushStyleColor(ImGuiCol_Text, kOrange);
      ImGui::TextWrapped("%s", iv.note);
      ImGui::PopStyleColor();
    }
  }
  ImGui::Separator();
  ImGui::TextDisabled("F%d hides this panel", config::get().toggle_key - 0x6F);
  ImGui::End();
}

}  // namespace

bool ensure_context(HWND hwnd) {
  if (g_context_ready) return true;
  if (!hwnd) return false;
  g_hwnd = hwnd;

  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO& io = ImGui::GetIO();
  // Window position persists beside the DLL, under the mod's own name rather
  // than a stray imgui.ini in the game folder.
  static char ini_path[MAX_PATH] = {};
  std::snprintf(ini_path, sizeof(ini_path), "%sRE4OGCabbyCodes.imgui.ini", config::dir());
  io.IniFilename = ini_path;
  // ImGui writes the file a moment after a change (wants_draw writes what is
  // still waiting when the panel goes): half a second, not its usual five, so
  // a game closed straight after a click loses nothing.
  io.IniSavingRate = 0.5f;
  // The panel's own lines in that file, registered before the first frame,
  // which is when ImGui reads it.
  ImGuiSettingsHandler panel{};
  panel.TypeName = "RE4OGCabbyCodes";
  panel.TypeHash = ImHashStr("RE4OGCabbyCodes");
  panel.ReadOpenFn = [](ImGuiContext*, ImGuiSettingsHandler*, const char* name) -> void* {
    return std::strcmp(name, "Panel") == 0 ? &g_cheats_open : nullptr;
  };
  panel.ReadLineFn = [](ImGuiContext*, ImGuiSettingsHandler*, void*, const char* line) {
    int v = 0;
    if (std::sscanf(line, "CheatsOpen=%d", &v) == 1) g_cheats_open = v != 0;
    else if (std::sscanf(line, "ResultsOpen=%d", &v) == 1) g_results_open = v != 0;
    else if (std::sscanf(line, "CaseOpen=%d", &v) == 1) g_case_open = v != 0;
    else if (std::sscanf(line, "AddOpen=%d", &v) == 1) g_add_open = v != 0;
    else if (std::sscanf(line, "KeyOpen=%d", &v) == 1) g_key_open = v != 0;
    else if (std::sscanf(line, "KeyAddOpen=%d", &v) == 1) g_key_add_open = v != 0;
    else if (std::sscanf(line, "FilesOpen=%d", &v) == 1) g_files_open = v != 0;
    else if (std::sscanf(line, "FilesAddOpen=%d", &v) == 1) g_files_add_open = v != 0;
    else if (std::sscanf(line, "TreasuresOpen=%d", &v) == 1) g_treasures_open = v != 0;
    else if (std::sscanf(line, "TreasuresAddOpen=%d", &v) == 1) g_treasures_add_open = v != 0;
    else if (std::sscanf(line, "ItemBoxOpen=%d", &v) == 1) g_box_open = v != 0;
    else if (std::sscanf(line, "ScalePercent=%d", &v) == 1 && v > 0) g_layout_percent = v;
  };
  panel.WriteAllFn = [](ImGuiContext*, ImGuiSettingsHandler* h, ImGuiTextBuffer* out) {
    out->appendf("[%s][Panel]\nCheatsOpen=%d\nResultsOpen=%d\nCaseOpen=%d\nAddOpen=%d\nKeyOpen=%d\nKeyAddOpen=%d\n"
                 "FilesOpen=%d\nFilesAddOpen=%d\nTreasuresOpen=%d\nTreasuresAddOpen=%d\nItemBoxOpen=%d\n"
                 "ScalePercent=%d\n\n",
                 h->TypeName, g_cheats_open ? 1 : 0, g_results_open ? 1 : 0, g_case_open ? 1 : 0, g_add_open ? 1 : 0,
                 g_key_open ? 1 : 0, g_key_add_open ? 1 : 0, g_files_open ? 1 : 0, g_files_add_open ? 1 : 0,
                 g_treasures_open ? 1 : 0, g_treasures_add_open ? 1 : 0, g_box_open ? 1 : 0, g_layout_percent);
  };
  ImGui::AddSettingsHandler(&panel);
  // No keyboard navigation: the game's menus keep their keys. Dragging is
  // limited to the title bar so a click on the panel's background cannot pick
  // the window up by accident.
  io.ConfigWindowsMoveFromTitleBarOnly = true;
  ImGui::StyleColorsDark();
  ImGuiStyle& style = ImGui::GetStyle();
  style.WindowRounding = 4.0f;
  style.ScaleAllSizes(kScale);  // the padding, spacing and the rest; the font has its own factor
  style.FontScaleMain = kScale;

  if (!ImGui_ImplWin32_Init(hwnd)) {
    logf("ERROR: ImGui Win32 backend init failed");
    ImGui::DestroyContext();
    return false;
  }
  g_orig_wndproc = reinterpret_cast<WNDPROC>(SetWindowLongPtrA(hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(hk_wndproc)));
  g_context_ready = true;
  char cls[64] = "?";
  GetClassNameA(hwnd, cls, sizeof(cls));
  logf("overlay ready (hwnd=%p class '%s', scale %d%%, present thread %lu, window thread %lu)",
       static_cast<void*>(hwnd), cls, kScalePercent, GetCurrentThreadId(), GetWindowThreadProcessId(hwnd, nullptr));
  return true;
}

HWND window() { return g_context_ready ? g_hwnd : nullptr; }

bool visible() { return g_visible_flag != 0; }
bool capturing_mouse() { return g_capture_mouse != 0; }
bool capturing_keyboard() { return g_capture_keyboard != 0; }

void lock_imgui() {
  if (g_imgui_cs_ready) EnterCriticalSection(&g_imgui_cs);
}

void unlock_imgui() {
  if (g_imgui_cs_ready) LeaveCriticalSection(&g_imgui_cs);
}

// Called from the present hook with the context lock held.
bool wants_draw() {
  const dispatch::Snapshot s = dispatch::snapshot();
  const bool showable = s.show_panel || config::get().always_show;
  if (!showable) {
    g_user_hidden = false;  // the next time the screen comes up the panel is back
    forget_item_dialogs();
    forget_file_dialogs();
    forget_edits();
  }
  const bool was_visible = g_visible;
  g_visible = g_context_ready && showable && !g_user_hidden;
  InterlockedExchange(&g_visible_flag, g_visible ? 1 : 0);
  // ImGui's delay before it writes the layout file (the sections folded, the
  // panel's place) counts down only in frames, and the panel draws none once
  // it is gone - nor does the mod touch ImGui as the game exits. A change made
  // just before the pause closed would wait for the panel's next showing, and be
  // lost if the game ended first: so whatever is waiting is written now.
  if (was_visible && !g_visible && g_context_ready && GImGui->SettingsDirtyTimer > 0.0f && GImGui->IO.IniFilename)
    ImGui::SaveIniSettingsToDisk(GImGui->IO.IniFilename);
  if (!g_visible) {  // nothing is being taken from the game until it draws again
    InterlockedExchange(&g_capture_mouse, 0);
    InterlockedExchange(&g_capture_keyboard, 0);
  }
  if (!g_context_ready) return false;
  ImGuiIO& io = ImGui::GetIO();
  // The game hides the system pointer (and holds the mouse while it plays):
  // the panel draws one of its own.
  io.MouseDrawCursor = g_visible;
  if (g_visible) {
    // If a release was ever missed the panel would follow the pointer for ever;
    // the physical button is the truth. GetAsyncKeyState reads the physical
    // buttons and the messages the logical ones: with the buttons swapped in
    // Windows' mouse settings (left-handed) the panel's left button is the
    // physical right one (RE3CabbyCodes, 2026-10-13).
    static const int kVk[3] = {VK_LBUTTON, VK_RBUTTON, VK_MBUTTON};
    const bool swapped = GetSystemMetrics(SM_SWAPBUTTON) != 0;
    for (int b = 0; b < 3; ++b) {
      const int vk = swapped && b < 2 ? kVk[1 - b] : kVk[b];
      if (io.MouseDown[b] && !(GetAsyncKeyState(vk) & 0x8000)) io.AddMouseButtonEvent(b, false);
    }
  } else {
    // Input made while the panel is away is not the panel's: ImGui only queues
    // events and drains them in NewFrame, which does not run while the panel is
    // hidden, so they would be replayed onto it the moment it came back (RE0).
    io.ClearEventsQueue();
    io.ClearInputMouse();
    io.ClearInputKeys();
  }
  return g_visible;
}

void draw_frame() {
  if (g_visible) draw_panel();
}

void shutdown_imgui() {
  if (!g_context_ready) return;
  ImGui_ImplWin32_Shutdown();
  if (ImGui::GetCurrentContext()) ImGui::DestroyContext();
  g_context_ready = false;
  g_visible = false;
  InterlockedExchange(&g_visible_flag, 0);
  InterlockedExchange(&g_capture_mouse, 0);
  InterlockedExchange(&g_capture_keyboard, 0);
}

bool install() {
  adopt::init();
  if (!g_imgui_cs_ready) {  // before the window procedure or the present hook exists
    InitializeCriticalSection(&g_imgui_cs);
    g_imgui_cs_ready = true;
  }
  return dx9::install_import(GetModuleHandleA(nullptr));
}

void uninstall() {
  // Window procedure first: a message arriving after our image is gone would
  // jump into freed memory.
  if (g_orig_wndproc && g_hwnd && IsWindow(g_hwnd)) {
    SetWindowLongPtrA(g_hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(g_orig_wndproc));
    g_orig_wndproc = nullptr;
  }
  dx9::uninstall();
  shutdown_imgui();
  logf("overlay hooks removed");
}

}  // namespace re4cc::overlay
