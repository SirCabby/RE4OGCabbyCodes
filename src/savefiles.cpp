#include "savefiles.h"

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "config.h"
#include "log.h"
#include "mem.h"

namespace re4cc::savefiles {
namespace {

// The buffer's layout (game.h, and CLAUDE.md "The save card"), and a file's.
constexpr int kInUse = 0x1E74;    // a dword per file
constexpr int kFirst = 0x1EE8;    // file 0
constexpr int kVersion = 4;       // u32 0x402 in a file the game wrote
constexpr uint32_t kVersionNow = 0x402;
constexpr int kStamp = 8;         // ten dwords: second, minute, hour, day, month, year, ...
constexpr int kGlobal = 0x34;     // GLOBAL_WK's save region (+4F90 there) starts here
constexpr int kTailCrc = 0xCAF8;  // the whole file's checksum
constexpr int kDifficultyCopy = 0x3D;  // the save writes the difficulty here too, inside the head

int global(int off) { return kGlobal + (off - 0x4F90); }

CRITICAL_SECTION g_lock;
bool g_lock_ready = false;
View g_view;  // published under the lock

struct Request {
  enum Kind { kNone, kDelete, kCopy } kind = kNone;
  int from = -1, to = -1;
  Save from_seen, to_seen;
};
Request g_request;  // one at a time: the panel's dialogs ask one question at a time

// The card as the last tick saw it: the saves and loads it makes, for the log.
bool g_was_up = false;
int g_mode = -1;
uint32_t g_crc_at_open[kFiles] = {};  // each file's checksums as the screen came up (save mode)
int g_load_reported = -1;             // the load already logged
char g_note[240] = {};
bool g_note_error = false;

uintptr_t file_at(uintptr_t buffer, int i) { return buffer + kFirst + static_cast<uintptr_t>(i) * game::kFileSize; }

Save read_save(uintptr_t buffer, int i) {
  Save s;
  if (!buffer) return s;
  s.known = true;
  const uintptr_t f = file_at(buffer, i);
  const uint32_t status = game::card_status(i);
  s.used = mem::read<uint32_t>(buffer + kInUse + i * 4) != 0 && mem::read<uint32_t>(f + kVersion) == kVersionNow;
  s.broken = s.used && (status & 2);
  if (!s.used) return s;
  s.play_time = mem::read<int32_t>(f + global(0x4FA4));
  s.saves = mem::read<uint16_t>(f + global(0x4F9C));
  s.chapter = mem::read<uint8_t>(f + global(0x4F9A));
  s.difficulty = mem::read<uint8_t>(f + kDifficultyCopy);
  s.room = mem::read<uint16_t>(f + global(0x4FAC));
  s.character = mem::read<uint8_t>(f + global(0x4FC8));
  s.pesetas = mem::read<int32_t>(f + global(0x4FA8));
  s.minute = mem::read<int32_t>(f + kStamp + 4);
  s.hour = mem::read<int32_t>(f + kStamp + 8);
  s.day = mem::read<int32_t>(f + kStamp + 12);
  s.month = mem::read<int32_t>(f + kStamp + 16);
  s.year = mem::read<int32_t>(f + kStamp + 20);
  s.crc = mem::read<uint32_t>(f) ^ (mem::read<uint32_t>(f + kTailCrc) * 2654435761u);
  return s;
}

void note(bool error, const char* fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  std::vsnprintf(g_note, sizeof(g_note), fmt, ap);
  va_end(ap);
  g_note_error = error;
  logf("save files: %s", g_note);
}

// --- the file, through the game's ISteamRemoteStorage ------------------------------------
// The interface's first two functions, as bio4.exe calls them (thiscall):
// FileWrite(name, data, size) and FileRead(name, data, size).
using FileWriteFn = bool(__thiscall*)(void*, const char*, const void*, int);
using FileReadFn = int(__thiscall*)(void*, const char*, void*, int);
constexpr char kFileName[] = "savegame00.sav";

bool steam_write(const void* data, int size) {
  void* rs = game::remote_storage();
  if (!rs) return false;
  const auto vt = mem::read<uintptr_t>(reinterpret_cast<uintptr_t>(rs));
  return reinterpret_cast<FileWriteFn>(mem::read<uintptr_t>(vt))(rs, kFileName, data, size);
}

int steam_read(void* data, int size) {
  void* rs = game::remote_storage();
  if (!rs) return -1;
  const auto vt = mem::read<uintptr_t>(reinterpret_cast<uintptr_t>(rs));
  return reinterpret_cast<FileReadFn>(mem::read<uintptr_t>(vt + 4))(rs, kFileName, data, size);
}

// A copy of savegame00.sav as Steam has it now, beside the mod, before any
// change: RE4OGCabbyCodes.save-backup\savegame00-<date>-<time>.sav (the ten
// newest are kept).
bool backup(char* path_out, size_t n) {
  std::vector<uint8_t> data(game::kSaveSize + 16);
  const int got = steam_read(data.data(), static_cast<int>(data.size()));
  if (got <= 0) return false;
  char dir[MAX_PATH];
  std::snprintf(dir, sizeof(dir), "%sRE4OGCabbyCodes.save-backup", config::dir());
  CreateDirectoryA(dir, nullptr);
  SYSTEMTIME t;
  GetLocalTime(&t);
  std::snprintf(path_out, n, "%s\\savegame00-%04u%02u%02u-%02u%02u%02u.sav", dir, t.wYear, t.wMonth, t.wDay, t.wHour,
                t.wMinute, t.wSecond);
  FILE* f = std::fopen(path_out, "wb");
  if (!f) return false;
  const bool ok = std::fwrite(data.data(), 1, static_cast<size_t>(got), f) == static_cast<size_t>(got);
  if (std::fclose(f) != 0 || !ok) return false;
  // The names sort by date: the oldest beyond ten go.
  char pattern[MAX_PATH];
  std::snprintf(pattern, sizeof(pattern), "%s\\savegame00-*.sav", dir);
  std::vector<std::string> names;
  WIN32_FIND_DATAA fd;
  HANDLE h = FindFirstFileA(pattern, &fd);
  if (h != INVALID_HANDLE_VALUE) {
    do names.emplace_back(fd.cFileName);
    while (FindNextFileA(h, &fd));
    FindClose(h);
  }
  std::sort(names.begin(), names.end());
  for (size_t i = 0; i + 10 < names.size(); ++i) {
    char old[MAX_PATH];
    std::snprintf(old, sizeof(old), "%s\\%s", dir, names[i].c_str());
    DeleteFileA(old);
  }
  return true;
}

// The buffer written to Steam and read back: true when what is there now is
// exactly the buffer.
bool write_back(uintptr_t buffer) {
  if (!steam_write(reinterpret_cast<const void*>(buffer), game::kSaveSize)) return false;
  std::vector<uint8_t> check(game::kSaveSize);
  const int got = steam_read(check.data(), game::kSaveSize);
  return got == game::kSaveSize && std::memcmp(check.data(), reinterpret_cast<const void*>(buffer), game::kSaveSize) == 0;
}

void carry_out(const Request& r, const game::Card& c) {
  const uintptr_t b = c.buffer;
  // The files as they are now must be the ones the panel showed.
  const Save from_now = read_save(b, r.from);
  if (!same_save(from_now, r.from_seen)) {
    note(true, "file %d changed before the request could be carried out - nothing was done", r.from + 1);
    return;
  }
  if (r.kind == Request::kCopy && !same_save(read_save(b, r.to), r.to_seen)) {
    note(true, "file %d changed before the copy could be made - nothing was done", r.to + 1);
    return;
  }
  char bak[MAX_PATH] = "";
  if (!backup(bak, sizeof(bak))) {
    note(true, "savegame00.sav could not be backed up first - nothing was done");
    return;
  }
  // The buffer as it was, to put back if Steam does not take the change.
  std::vector<uint8_t> was(reinterpret_cast<const uint8_t*>(b), reinterpret_cast<const uint8_t*>(b) + game::kSaveSize);
  const uint32_t status_from = game::card_status(r.from);
  const uint32_t status_to = r.kind == Request::kCopy ? game::card_status(r.to) : 0;
  const bool exists_to = r.kind == Request::kCopy ? game::card_exists(r.to) : false;
  const bool exists_from = game::card_exists(r.from);
  if (r.kind == Request::kDelete) {
    *reinterpret_cast<uint32_t*>(b + kInUse + r.from * 4) = 0;
    *reinterpret_cast<uint32_t*>(file_at(b, r.from) + kVersion) = 0;
    game::set_card_status(r.from, 0);
    game::set_card_exists(r.from, false);
  } else {
    std::memcpy(reinterpret_cast<void*>(file_at(b, r.to)), reinterpret_cast<const void*>(file_at(b, r.from)),
                game::kFileSize);
    *reinterpret_cast<uint32_t*>(b + kInUse + r.to * 4) = mem::read<uint32_t>(b + kInUse + r.from * 4);
    game::set_card_status(r.to, status_from);
    game::set_card_exists(r.to, exists_from);
  }
  if (!write_back(b)) {
    std::memcpy(reinterpret_cast<void*>(b), was.data(), was.size());
    if (r.kind == Request::kDelete) {
      game::set_card_status(r.from, status_from);
      game::set_card_exists(r.from, exists_from);
    } else {
      game::set_card_status(r.to, status_to);
      game::set_card_exists(r.to, exists_to);
    }
    write_back(b);  // what was there before, in case half of it reached Steam
    note(true, "Steam did not take the change to savegame00.sav - nothing was changed (backup: %s)", bak);
    return;
  }
  if (r.kind == Request::kDelete)
    note(false, "file %d deleted (savegame00.sav backed up to %s)", r.from + 1, bak);
  else
    note(false, "file %d copied to file %d (savegame00.sav backed up to %s)", r.from + 1, r.to + 1, bak);
}

void publish(const View& v) {
  if (!g_lock_ready) return;
  EnterCriticalSection(&g_lock);
  g_view = v;
  LeaveCriticalSection(&g_lock);
}

}  // namespace

bool same_save(const Save& a, const Save& b) {
  return a.known == b.known && a.used == b.used && (!a.used || a.crc == b.crc);
}

const char* chapter_name(int chapter, char* out, size_t n) {
  // GLOBAL_WK's chapter byte counts the main game's chapters from 1 (1-1) to
  // 19 (the Final Chapter); Separate Ways' start at 20.
  static const char* const kMain[] = {"1-1", "1-2", "1-3", "2-1", "2-2", "2-3", "3-1", "3-2", "3-3", "3-4",
                                      "4-1", "4-2", "4-3", "4-4", "5-1", "5-2", "5-3", "5-4", "Final"};
  if (chapter >= 1 && chapter <= 19) std::snprintf(out, n, "Chapter %s", kMain[chapter - 1]);
  else if (chapter >= 20 && chapter <= 24) std::snprintf(out, n, "Separate Ways, chapter %d", chapter - 19);
  else std::snprintf(out, n, "chapter %d", chapter);
  return out;
}

const char* describe(const Save& s, char* out, size_t n) {
  if (!s.known) return std::snprintf(out, n, "?"), out;
  if (!s.used) return std::snprintf(out, n, "no data"), out;
  static const char* const kDiff[] = {"Amateur", "Amateur", "Easy", "Easy", "Normal", "Normal", "Professional"};
  char ch[48];
  std::snprintf(out, n, "%s, %d:%02d:%02d, %d save%s, %s - %04d-%02d-%02d %02d:%02d%s",
                chapter_name(s.chapter, ch, sizeof(ch)), s.play_time / 3600, (s.play_time / 60) % 60,
                s.play_time % 60, s.saves, s.saves == 1 ? "" : "s",
                s.difficulty >= 0 && s.difficulty <= 6 ? kDiff[s.difficulty] : "?", s.year, s.month, s.day, s.hour,
                s.minute, s.broken ? " (damaged)" : "");
  return out;
}

void init() {
  if (g_lock_ready) return;
  InitializeCriticalSection(&g_lock);
  g_lock_ready = true;
}

View view() {
  if (!g_lock_ready) return View{};
  EnterCriticalSection(&g_lock);
  const View v = g_view;
  LeaveCriticalSection(&g_lock);
  return v;
}

bool list_up() {
  game::Card c;
  return game::card(&c) && c.buffer && c.routine == 2 && c.step == 1 && (c.mode == 0 || c.mode == 1);
}

void request_delete(int file, const Save& seen) {
  if (!g_lock_ready) return;
  EnterCriticalSection(&g_lock);
  g_request = Request{Request::kDelete, file, -1, seen, Save{}};
  LeaveCriticalSection(&g_lock);
}

void request_copy(int from, int to, const Save& from_seen, const Save& to_seen) {
  if (!g_lock_ready) return;
  EnterCriticalSection(&g_lock);
  g_request = Request{Request::kCopy, from, to, from_seen, to_seen};
  LeaveCriticalSection(&g_lock);
}

void tick(const game::Run& run) {
  (void)run;
  View v;
  v.ready = game::card_known();
  game::Card c;
  const bool up = v.ready && game::card(&c) && c.buffer;
  v.up = up;
  if (up) {
    v.mode = c.mode;
    v.cursor = c.cursor;
    v.waiting = c.routine == 2 && c.step == 1 && (c.mode == 0 || c.mode == 1);
    for (int i = 0; i < kFiles; ++i) v.save[i] = read_save(c.buffer, i);
    if (!g_was_up || c.mode != g_mode) {
      // The screen came up: each file's checksums as they are, so a save the
      // player makes now is seen as the one file that changed.
      for (int i = 0; i < kFiles; ++i) g_crc_at_open[i] = v.save[i].used ? v.save[i].crc : 0;
      g_load_reported = -1;
      if (c.mode == 2) logf("save files: the start-up check is reading savegame00.sav");
      else logf("save files: the %s screen is up (cursor on file %d)", c.mode == 1 ? "save" : "load", c.cursor + 1);
    }
    g_mode = c.mode;
    if (c.mode == 1) {
      for (int i = 0; i < kFiles; ++i) {
        const uint32_t now = v.save[i].used ? v.save[i].crc : 0;
        if (now && now != g_crc_at_open[i]) {
          g_crc_at_open[i] = now;
          logf("save files: the game saved to file %d", i + 1);
        }
      }
    } else if (c.mode == 0 && c.routine == 3 && c.cursor >= 0 && c.cursor < kFiles && g_load_reported != c.cursor) {
      // The load itself: the screen's load/save routine running with the cursor
      // on the file the player picked.
      g_load_reported = c.cursor;
      logf("save files: the game is loading file %d", c.cursor + 1);
    }
    Request r;
    EnterCriticalSection(&g_lock);
    r = g_request;
    g_request = Request{};
    LeaveCriticalSection(&g_lock);
    if (r.kind != Request::kNone) {
      if (!v.waiting) note(true, "the game's list is busy - nothing was done; try again once it waits for you");
      else carry_out(r, c);
      for (int i = 0; i < kFiles; ++i) {
        v.save[i] = read_save(c.buffer, i);
        if (c.mode == 1) g_crc_at_open[i] = v.save[i].used ? v.save[i].crc : 0;
      }
    }
  } else if (g_was_up) {
    logf("save files: the screen is gone");
    g_mode = -1;
  }
  g_was_up = up;
  std::memcpy(v.note, g_note, sizeof(v.note));
  v.note_error = g_note_error;
  publish(v);
}

}  // namespace re4cc::savefiles
