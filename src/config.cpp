#include "config.h"

#include <windows.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "files.h"
#include "log.h"

namespace re4cc::config {
namespace {

Settings g_settings;
char g_path[MAX_PATH] = {};
char g_dir[MAX_PATH] = {};

bool truthy(const char* v) {
  return !_stricmp(v, "1") || !_stricmp(v, "on") || !_stricmp(v, "true") || !_stricmp(v, "yes");
}

void write_defaults() {
  FILE* f = std::fopen(g_path, "w");
  if (!f) return;
  std::fprintf(f,
               "# RE4OGCabbyCodes\n"
               "#\n"
               "#   ToggleKey        = 0x76  ; virtual-key code that hides/shows the panel (0x76 = F7)\n"
               "#   GodMode          = 0     ; the cheats; the panel switches them, and the mod writes\n"
               "#   AshleyGodMode    = 0     ; every change back here, so they stay as they were left\n"
               "#   OneHitKills      = 0     ; from one launch to the next\n"
               "#   InfiniteAmmo     = 0\n"
               "#   FreezePlaytime   = 0\n"
               "#   FreezeCountdown  = 0     ; the on-screen countdown (the Mercenaries' clock and the game's timed\n"
               "#                            ; scenes) stands still\n"
               "#   NoSaveCount      = 0     ; a save does not add to the save count the load and save lists show\n"
               "#   InfiniteMoney    = 0     ; the merchant takes no pesetas for a purchase or a tune-up\n"
               "#   NoMissCount      = 0     ; the results: a shot that misses is not counted as a shot fired\n"
               "#   NoDeathCount     = 0     ; the results: a death is not counted, for the chapter or the game\n"
               "#   AutoQTE          = 0     ; QTEs pass by themselves: dodge prompts at once, button mashing done\n"
               "#                            ; for you, grabs shaken off\n"
               "#   SteadyAim        = 0     ; the aim stays where you put it - no sway, Leon's aiming stance held\n"
               "#                            ; still, the laser and the shots hold their line through the recoil -\n"
               "#                            ; and shots fly straight down it (no random spread)\n"
               "#   AutoGallery      = 0     ; the merchant's shooting gallery plays itself for the most points:\n"
               "#                            ; every Ganado in the head, never Ashley, every bonus target\n"
               "#   EndlessCombo     = 0     ; the Mercenaries: a combo never runs out of time, and its bonus is\n"
               "#                            ; scored as it is earned\n"
               "#   AutoKill         = 0     ; the Mercenaries: every enemy dies as it appears, its kill scored\n"
               "#   BonusTime        = 0     ; the Mercenaries: a Bonus Points time runs from the start and never\n"
               "#                            ; runs out, its combo bonuses scored as they are earned\n"
               "#   AlwaysShow       = 0     ; debug: draw the panel everywhere, not only on its screens\n"
               "#   Trace            = 0     ; verbose diagnostics in RE4OGCabbyCodes.log\n"
               "#   Watch            =       ; debug: 0x846F9C:2,0x80A99:1 - log those values (offsets from\n"
               "#                            ; bio4.exe's base) as they change\n"
               "#   Disable          =       ; comma list of subsystems to turn off: overlay,tick,game,input\n"
               "ToggleKey = 0x76\n"
               "GodMode = 0\n"
               "AshleyGodMode = 0\n"
               "OneHitKills = 0\n"
               "InfiniteAmmo = 0\n"
               "FreezePlaytime = 0\n"
               "FreezeCountdown = 0\n"
               "NoSaveCount = 0\n"
               "InfiniteMoney = 0\n"
               "NoMissCount = 0\n"
               "NoDeathCount = 0\n"
               "AutoQTE = 0\n"
               "SteadyAim = 0\n"
               "AutoGallery = 0\n"
               "EndlessCombo = 0\n"
               "AutoKill = 0\n"
               "BonusTime = 0\n"
               "AlwaysShow = 0\n"
               "Trace = 0\n"
               "Watch =\n"
               "Disable =\n");
  std::fclose(f);
}

// "0x846F9C:2, 0x80A99:1" -> the watch list (sizes 1, 2 or 4; 4 when omitted).
void parse_watch(const char* val) {
  g_settings.nwatch = 0;
  const char* p = val;
  while (*p && g_settings.nwatch < kMaxWatch) {
    while (*p == ' ' || *p == '\t' || *p == ',') ++p;
    if (!*p) break;
    char* end = nullptr;
    const unsigned long rva = std::strtoul(p, &end, 0);
    if (end == p) break;
    p = end;
    unsigned size = 4;
    if (*p == ':') {
      size = static_cast<unsigned>(std::strtoul(p + 1, &end, 0));
      p = end;
    }
    if (rva && (size == 1 || size == 2 || size == 4)) {
      g_settings.watch[g_settings.nwatch].rva = static_cast<unsigned>(rva);
      g_settings.watch[g_settings.nwatch].size = size;
      ++g_settings.nwatch;
    }
    while (*p && *p != ',') ++p;
  }
}

// "  GodMode = 1 ; comment" -> whether the line sets `key` (case as the parser
// takes it: any, with spaces either side of the name).
bool line_sets(const char* line, const char* key) {
  while (*line == ' ' || *line == '\t') ++line;
  const size_t n = std::strlen(key);
  if (_strnicmp(line, key, n)) return false;
  line += n;
  while (*line == ' ' || *line == '\t') ++line;
  return *line == '=';
}

}  // namespace

bool save_cheats(const Cheats& c) {
  struct Item {
    const char* key;
    bool on;
    bool* setting;
    bool written;
  } items[] = {{"GodMode", c.god_mode, &g_settings.god_mode, false},
               {"AshleyGodMode", c.ashley_god, &g_settings.ashley_god, false},
               {"OneHitKills", c.one_hit_kills, &g_settings.one_hit_kills, false},
               {"InfiniteAmmo", c.infinite_ammo, &g_settings.infinite_ammo, false},
               {"FreezePlaytime", c.freeze_playtime, &g_settings.freeze_playtime, false},
               {"FreezeCountdown", c.freeze_countdown, &g_settings.freeze_countdown, false},
               {"NoSaveCount", c.no_save_count, &g_settings.no_save_count, false},
               {"InfiniteMoney", c.infinite_money, &g_settings.infinite_money, false},
               {"NoMissCount", c.no_miss_count, &g_settings.no_miss_count, false},
               {"NoDeathCount", c.no_death_count, &g_settings.no_death_count, false},
               {"AutoQTE", c.auto_qte, &g_settings.auto_qte, false},
               {"SteadyAim", c.steady_aim, &g_settings.steady_aim, false},
               {"AutoGallery", c.auto_gallery, &g_settings.auto_gallery, false},
               {"EndlessCombo", c.endless_combo, &g_settings.endless_combo, false},
               {"AutoKill", c.auto_kill, &g_settings.auto_kill, false},
               {"BonusTime", c.bonus_time, &g_settings.bonus_time, false}};
  if (!g_path[0]) return false;
  std::string out;
  if (FILE* f = std::fopen(g_path, "r")) {
    char line[512];
    while (std::fgets(line, sizeof(line), f)) {
      const char* p = line;
      while (*p == ' ' || *p == '\t') ++p;
      bool replaced = false;
      if (*p != '#' && *p != ';')
        for (Item& it : items)
          if (line_sets(p, it.key)) {
            // A second line for the same key would win on the next load, so it
            // gets the value too.
            out += it.key;
            out += it.on ? " = 1\n" : " = 0\n";
            it.written = true;
            replaced = true;
            break;
          }
      if (!replaced) {
        out += line;
        if (!out.empty() && out.back() != '\n') out += '\n';
      }
    }
    std::fclose(f);
  }
  for (Item& it : items) {
    *it.setting = it.on;
    if (it.written) continue;
    out += it.key;
    out += it.on ? " = 1\n" : " = 0\n";
  }
  // Written beside the ini and moved over it, so a crash halfway leaves the old
  // file rather than half of a new one.
  char tmp[MAX_PATH + 8];
  std::snprintf(tmp, sizeof(tmp), "%s.tmp", g_path);
  FILE* f = std::fopen(tmp, "wb");
  if (!f) {
    logf("config: cannot write %s - the cheat switches are not saved", tmp);
    return false;
  }
  const bool ok = std::fwrite(out.data(), 1, out.size(), f) == out.size();
  DWORD err = 0;
  if (std::fclose(f) != 0 || !ok || !files::replace(tmp, g_path, MOVEFILE_REPLACE_EXISTING, &err)) {
    DeleteFileA(tmp);
    logf("config: could not save the cheat switches to %s (error %lu)", g_path, static_cast<unsigned long>(err));
    return false;
  }
  return true;
}

const Settings& get() { return g_settings; }
const char* dir() { return g_dir; }

void load(const char* dir) {
  std::snprintf(g_dir, sizeof(g_dir), "%s", dir);
  std::snprintf(g_path, sizeof(g_path), "%sRE4OGCabbyCodes.ini", dir);
  FILE* f = std::fopen(g_path, "r");
  if (!f) {
    write_defaults();
    logf("config: no %s - wrote one with the defaults", g_path);
    return;
  }
  char line[512];
  while (std::fgets(line, sizeof(line), f)) {
    char* p = line;
    while (*p == ' ' || *p == '\t') ++p;
    if (*p == '#' || *p == ';' || *p == '[' || *p == '\n' || *p == '\r' || !*p) continue;
    char* eq = std::strchr(p, '=');
    if (!eq) continue;
    *eq = '\0';
    char* key = p;
    char* val = eq + 1;
    for (char* e = key + std::strlen(key); e > key && (e[-1] == ' ' || e[-1] == '\t');) *--e = '\0';
    while (*val == ' ' || *val == '\t') ++val;
    // A trailing "; comment" is the file's own documentation style.
    if (char* c = std::strchr(val, ';')) *c = '\0';
    for (char* e = val + std::strlen(val);
         e > val && (e[-1] == '\n' || e[-1] == '\r' || e[-1] == ' ' || e[-1] == '\t');)
      *--e = '\0';

    if (!_stricmp(key, "ToggleKey")) {
      const int v = static_cast<int>(std::strtol(val, nullptr, 0));
      if (v > 0 && v < 256) g_settings.toggle_key = v;
    } else if (!_stricmp(key, "GodMode")) {
      g_settings.god_mode = truthy(val);
    } else if (!_stricmp(key, "AshleyGodMode")) {
      g_settings.ashley_god = truthy(val);
    } else if (!_stricmp(key, "OneHitKills")) {
      g_settings.one_hit_kills = truthy(val);
    } else if (!_stricmp(key, "InfiniteAmmo")) {
      g_settings.infinite_ammo = truthy(val);
    } else if (!_stricmp(key, "FreezePlaytime")) {
      g_settings.freeze_playtime = truthy(val);
    } else if (!_stricmp(key, "FreezeCountdown")) {
      g_settings.freeze_countdown = truthy(val);
    } else if (!_stricmp(key, "NoSaveCount")) {
      g_settings.no_save_count = truthy(val);
    } else if (!_stricmp(key, "InfiniteMoney")) {
      g_settings.infinite_money = truthy(val);
    } else if (!_stricmp(key, "NoMissCount")) {
      g_settings.no_miss_count = truthy(val);
    } else if (!_stricmp(key, "NoDeathCount")) {
      g_settings.no_death_count = truthy(val);
    } else if (!_stricmp(key, "AutoQTE")) {
      g_settings.auto_qte = truthy(val);
    } else if (!_stricmp(key, "SteadyAim")) {
      g_settings.steady_aim = truthy(val);
    } else if (!_stricmp(key, "AutoGallery")) {
      g_settings.auto_gallery = truthy(val);
    } else if (!_stricmp(key, "EndlessCombo")) {
      g_settings.endless_combo = truthy(val);
    } else if (!_stricmp(key, "AutoKill")) {
      g_settings.auto_kill = truthy(val);
    } else if (!_stricmp(key, "BonusTime")) {
      g_settings.bonus_time = truthy(val);
    } else if (!_stricmp(key, "AlwaysShow")) {
      g_settings.always_show = truthy(val);
    } else if (!_stricmp(key, "Trace")) {
      g_settings.trace = truthy(val);
    } else if (!_stricmp(key, "Watch")) {
      parse_watch(val);
    } else if (!_stricmp(key, "Disable")) {
      g_settings.disable_overlay = std::strstr(val, "overlay") != nullptr;
      g_settings.disable_tick = std::strstr(val, "tick") != nullptr;
      g_settings.disable_game = std::strstr(val, "game") != nullptr;
      g_settings.disable_input = std::strstr(val, "input") != nullptr;
    } else {
      logf("config: unknown key '%s' ignored", key);
    }
  }
  std::fclose(f);
  logf("config: ToggleKey=0x%02X GodMode=%d AshleyGodMode=%d OneHitKills=%d InfiniteAmmo=%d FreezePlaytime=%d "
       "FreezeCountdown=%d NoSaveCount=%d InfiniteMoney=%d NoMissCount=%d NoDeathCount=%d AutoQTE=%d SteadyAim=%d "
       "AutoGallery=%d EndlessCombo=%d AutoKill=%d BonusTime=%d AlwaysShow=%d Trace=%d Watch=%d value(s) "
       "Disable=%s%s%s%s",
       g_settings.toggle_key, g_settings.god_mode, g_settings.ashley_god, g_settings.one_hit_kills,
       g_settings.infinite_ammo, g_settings.freeze_playtime, g_settings.freeze_countdown, g_settings.no_save_count,
       g_settings.infinite_money, g_settings.no_miss_count, g_settings.no_death_count, g_settings.auto_qte,
       g_settings.steady_aim, g_settings.auto_gallery, g_settings.endless_combo, g_settings.auto_kill,
       g_settings.bonus_time, g_settings.always_show, g_settings.trace, g_settings.nwatch,
       g_settings.disable_overlay ? "overlay " : "", g_settings.disable_tick ? "tick " : "",
       g_settings.disable_game ? "game " : "", g_settings.disable_input ? "input" : "");
}

}  // namespace re4cc::config
