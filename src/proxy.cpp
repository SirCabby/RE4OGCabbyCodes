// The mod ships as steam_api.dll, standing in front of the Steam API DLL the
// game ships with (renamed steam_api_orig.dll by `make install`). Every
// function the original exports (56 of them; bio4.exe imports 13) is a
// one-instruction thunk that jumps through a table of the real addresses - the
// stack and every register are untouched, so the callee sees exactly the call
// the game made. The one data export, g_pSteamClientGameServer, is a PE
// forwarder in the .def. The list of names lives in proxy_exports.inc,
// generated from the original DLL's export table by tools/gen_proxy.py.
//
// Any Steam DLL will do as the original if it has what the game imports - the
// names bio4.exe's import table asks steam_api.dll for, read as the proxy
// loads (13 in build 10112090). What the original lacks answers 0
// (steam_missing), and the log says how much that is and whether the original
// is the DLL the game ships (kStock). Only what the game itself needs is fatal
// - a function the original lacks (the game could not run with that DLL, mod or
// not), or one this proxy does not export at all (another version of the game)
// - and then DllMain shows failure() and refuses to load. RE2CabbyCodes' first
// release refused any original short of every export, and players whose Steam
// DLL was not the stock one could not start the game; its successors, and this
// one, never do.
//
// Why steam_api: of bio4.exe's imports it is the one DLL that ships with the
// game and is not a Wine builtin, so Proton loads ours with no WINEDLLOVERRIDES
// (d3d9, dinput8, xinput1_3, x3daudio1_7 and winmm are all builtins there). The
// exe imports it statically, so this DLL is loaded - and DllMain runs - before
// the game's own code starts, and dinput8.dll stays free for re4_tweaks.

#include "proxy.h"

#include <cstdio>
#include <cstring>
#include <ctime>

#include "log.h"
#include "mem.h"
#include "proxy_exports.inc"

// Plain C symbols so the assembly below can name them (with the 32-bit
// underscore).
extern "C" {
void* g_proxy_orig[PROXY_EXPORT_COUNT] = {};
void steam_missing();
}

// One thunk per export: `jmp [g_proxy_orig + i*4]`, defined at file scope in
// assembly so no prologue or epilogue is ever emitted around the jump.
#define PROXY_THUNK(i, name)                                   \
  asm(".text\n"                                                \
      ".globl _steam_" #i "\n"                                 \
      "_steam_" #i ":\n"                                       \
      "\tjmp *_g_proxy_orig+" #i "*4\n");
PROXY_EXPORTS(PROXY_THUNK)
#undef PROXY_THUNK

// What a function the original lacks answers: 0 in eax. The Steam API's flat
// functions are cdecl, so the caller cleans the stack. The game never calls one
// - whatever it imports the original must have - so only something resolving a
// name at run time can get here.
asm(".text\n"
    ".globl _steam_missing\n"
    "_steam_missing:\n"
    "\txorl %eax, %eax\n"
    "\tret\n");

namespace re4cc::proxy {
namespace {

constexpr const char* kName = "steam_api.dll";  // what the game loads this DLL as
constexpr const char* const kNames[] = {
#define PROXY_NAME(i, name) name,
    PROXY_EXPORTS(PROXY_NAME)
#undef PROXY_NAME
};

HMODULE g_orig = nullptr;
int g_missing = 0;
char g_failure[1024] = "";

// The way out of every failure but the game's version: the game's own DLL back
// from Steam, renamed, and the mod's copied in again. A steam_api_orig.dll
// already there has to go first: verifying leaves it alone (it is not one of the
// game's files), and a rename cannot replace it - Explorer offers
// "steam_api_orig (2).dll" instead, and the wrong file is loaded again.
constexpr char kFixSteps[] =
    "in Steam, right-click Resident Evil 4 (2005) and choose Properties > Installed Files > Verify integrity of "
    "game files. That puts the game's own steam_api.dll back, in place of the mod's. Then rename it to "
    "steam_api_orig.dll and copy the mod's steam_api.dll in beside it again.";

// The Steam DLL the game ships, known by size, link stamp and export count - the
// same file on every install (SHA-256 6756b377...bce). It has every function
// this proxy exports. Any other is described in the log against it, never called
// wrong for that alone: RE2CabbyCodes learned that a player with a working file
// takes "not the Steam DLL the mod was made for" for the fault.
struct Stock {
  unsigned long long size;
  DWORD stamp;
  unsigned names;
  const char* version;  // "<version>'s own Steam DLL"
};
constexpr Stock kStock[] = {
    {106408, 0x51E436D3, 57, "the game's"},  // build 10112090, linked 2013-07-15
};

// The module this code is in: the mod's DLL (a test's exe when linked into one).
HMODULE self() {
  HMODULE m = nullptr;
  GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                     reinterpret_cast<LPCSTR>(&self), &m);
  return m;
}

// True when a loaded module's export table has the name (a function or a forwarder).
bool exports_name(HMODULE m, const char* name) {
  const IMAGE_NT_HEADERS* nt = mem::nt_headers(m);
  if (!nt) return false;
  const IMAGE_DATA_DIRECTORY& dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
  if (!dir.VirtualAddress) return false;
  const auto base = reinterpret_cast<uintptr_t>(m);
  const auto* exports = reinterpret_cast<const IMAGE_EXPORT_DIRECTORY*>(base + dir.VirtualAddress);
  const auto* names = reinterpret_cast<const DWORD*>(base + exports->AddressOfNames);
  for (DWORD i = 0; i < exports->NumberOfNames; ++i)
    if (std::strcmp(reinterpret_cast<const char*>(base + names[i]), name) == 0) return true;
  return false;
}

// Something this DLL exports: a thunk, or the data forwarder of the .def.
bool ours(const char* name) {
  for (const char* n : kNames)
    if (std::strcmp(n, name) == 0) return true;
  return exports_name(self(), name);
}

// A link stamp as the date it was linked (UTC).
void date_of(DWORD stamp, char* out, size_t n) {
  std::snprintf(out, n, "?");
  const std::time_t t = stamp;
  if (const std::tm* tm = stamp ? std::gmtime(&t) : nullptr) std::strftime(out, n, "%Y-%m-%d", tm);
}

// The original, for the log: its size, the date its linker stamped, how many
// names it exports - and whether it is the game's own.
const Stock* describe(const char* path, HMODULE m, char* out, size_t n) {
  WIN32_FILE_ATTRIBUTE_DATA file{};
  const unsigned long long size = GetFileAttributesExA(path, GetFileExInfoStandard, &file)
                                      ? (static_cast<unsigned long long>(file.nFileSizeHigh) << 32) | file.nFileSizeLow
                                      : 0;
  const IMAGE_NT_HEADERS* nt = mem::nt_headers(m);
  const DWORD stamp = nt ? nt->FileHeader.TimeDateStamp : 0;
  char linked[16];
  date_of(stamp, linked, sizeof(linked));
  unsigned names = 0;
  if (nt) {
    const IMAGE_DATA_DIRECTORY& dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
    if (dir.VirtualAddress)
      names = reinterpret_cast<const IMAGE_EXPORT_DIRECTORY*>(reinterpret_cast<uintptr_t>(m) + dir.VirtualAddress)
                  ->NumberOfNames;
  }
  std::snprintf(out, n, "%llu bytes, linked %s, %u exports", size, linked, names);
  for (const Stock& s : kStock)
    if (size == s.size && stamp == s.stamp && names == s.names) return &s;
  return nullptr;
}

// The game's own, for the log line of an original that is not it.
void describe_stock(char* out, size_t n) {
  out[0] = '\0';
  for (const Stock& s : kStock) {
    char linked[16];
    date_of(s.stamp, linked, sizeof(linked));
    const size_t at = std::strlen(out);
    std::snprintf(out + at, n - at, "%s%s: %llu bytes, linked %s, %u exports", at ? "; " : "", s.version, s.size,
                  linked, s.names);
  }
}

// The name the program's file has, for the log (bio4.exe).
const char* program() {
  static char name[MAX_PATH] = "";
  if (!name[0]) {
    char path[MAX_PATH] = {};
    GetModuleFileNameA(nullptr, path, MAX_PATH);
    const char* slash = std::strrchr(path, '\\');
    std::snprintf(name, sizeof(name), "%s", slash ? slash + 1 : path);
  }
  return name;
}

}  // namespace

HMODULE original() { return g_orig; }
const char* failure() { return g_failure; }
int missing() { return g_missing; }

bool load_original(const char* dir) { return load_original_as(dir, mem::imported_names(GetModuleHandleA(nullptr), kName)); }

bool load_original_as(const char* dir, const std::vector<std::string>& imports) {
  g_orig = nullptr;
  g_missing = 0;
  g_failure[0] = '\0';

  char path[MAX_PATH] = {};
  std::snprintf(path, sizeof(path), "%ssteam_api_orig.dll", dir);
  if (GetFileAttributesA(path) == INVALID_FILE_ATTRIBUTES) {
    // Windows hides the ".dll" of a file's name unless told otherwise, so the
    // rename can come out as steam_api_orig.dll.dll - which Explorer then
    // shows as exactly the name asked for.
    char twice[MAX_PATH] = {};
    std::snprintf(twice, sizeof(twice), "%ssteam_api_orig.dll.dll", dir);
    if (GetFileAttributesA(twice) != INVALID_FILE_ATTRIBUTES) {
      logf("proxy: no steam_api_orig.dll, but a steam_api_orig.dll.dll (the .dll typed twice - Windows hides it) "
           "- using that");
      std::snprintf(path, sizeof(path), "%s", twice);
    }
  }
  HMODULE orig = LoadLibraryA(path);
  if (!orig) {
    const DWORD error = GetLastError();
    if (GetFileAttributesA(path) == INVALID_FILE_ATTRIBUTES) {
      logf("FATAL: there is no %s - the game's own steam_api.dll was not renamed to it", path);
      std::snprintf(g_failure, sizeof(g_failure),
                    "RE4OGCabbyCodes: the game's own Steam DLL is missing.\n\n"
                    "The mod takes the place of the game's steam_api.dll and passes the game's calls on to the "
                    "original, which has to sit beside it renamed to steam_api_orig.dll. There is no "
                    "steam_api_orig.dll in the game's Bin32 folder.\n\nTo fix it: %s",
                    kFixSteps);
    } else {
      logf("FATAL: could not load %s (GetLastError=%lu)", path, error);
      std::snprintf(g_failure, sizeof(g_failure),
                    "RE4OGCabbyCodes: steam_api_orig.dll could not be loaded (Windows error %lu).\n\n"
                    "It should be the game's own steam_api.dll, renamed: the mod passes the game's calls on to "
                    "it.\n\nTo fix it: delete steam_api_orig.dll. Then, %s",
                    error, kFixSteps);
    }
    return false;
  }
  // Pin it: nothing may drop the last reference while the game still uses it.
  HMODULE pinned = nullptr;
  GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_PIN, path, &pinned);
  char about[96];
  const Stock* stock = describe(path, orig, about, sizeof(about));

  // What the game will ask this DLL for: it must be ours to give, and the
  // original's to answer.
  const char* unknown = nullptr;  // the first the proxy does not export
  const char* lacking = nullptr;  // the first the original lacks
  int n_unknown = 0, n_lacking = 0;
  for (const std::string& name : imports) {
    if (!ours(name.c_str())) {
      if (!n_unknown++) unknown = name.c_str();
    } else if (!GetProcAddress(orig, name.c_str())) {
      if (!n_lacking++) lacking = name.c_str();
    }
  }
  if (n_unknown) {
    logf("FATAL: %s imports %s from %s, which this proxy does not export (%d such name(s)) - not a version of the "
         "game the mod was made for (build 10112090)",
         program(), unknown, kName, n_unknown);
    std::snprintf(g_failure, sizeof(g_failure),
                  "RE4OGCabbyCodes does not work with this version of Resident Evil 4.\n\n"
                  "The game asks its Steam DLL for %s, which the mod does not pass on: the mod was made for the "
                  "current Steam version of Resident Evil 4 (2005) (build 10112090), and this is another one.\n\n"
                  "To play without the mod: delete the mod's steam_api.dll and rename steam_api_orig.dll back to "
                  "steam_api.dll.",
                  unknown);
    return false;
  }
  if (n_lacking) {
    // Never the game's own DLL (it has everything the game imports): another
    // game's, or an older SDK's.
    logf("FATAL: %s lacks %s, which %s imports (%d of its %d Steam function(s) missing) - not the game's own "
         "Steam DLL (%s)",
         path, lacking, program(), n_lacking, static_cast<int>(imports.size()), about);
    std::snprintf(g_failure, sizeof(g_failure),
                  "RE4OGCabbyCodes: steam_api_orig.dll is not the game's own Steam DLL.\n\n"
                  "The game needs %s from it, which it does not have, so the game cannot run with this file - with "
                  "the mod or without it. It is probably from another game.\n\nTo fix it: delete "
                  "steam_api_orig.dll. Then, %s",
                  lacking, kFixSteps);
    return false;
  }

  // Every function the proxy exports goes to the original's, or answers 0.
  int n_missing = 0;
  char listed[200] = "";  // the first few missing, for the log
  for (int i = 0; i < PROXY_EXPORT_COUNT; ++i) {
    void* fn = reinterpret_cast<void*>(GetProcAddress(orig, kNames[i]));
    if (!fn) {
      fn = reinterpret_cast<void*>(&steam_missing);
      if (++n_missing <= 4) {
        const size_t at = std::strlen(listed);
        std::snprintf(listed + at, sizeof(listed) - at, "%s%s", at ? ", " : "", kNames[i]);
      }
    }
    g_proxy_orig[i] = fn;
  }
  g_orig = orig;
  g_missing = n_missing;
  // What is missing is only noted, never a fault: whatever the game imports was
  // checked above.
  if (!n_missing) {
    logf("forwarding %d Steam API functions -> %s (%s%s%s%s)", PROXY_EXPORT_COUNT, path, about, stock ? " - " : "",
         stock ? stock->version : "", stock ? " own" : "");
  } else {
    char known[200];
    describe_stock(known, sizeof(known));
    logf("proxy: %s is not the Steam DLL the game ships (%s; %s) - but it has every function the game imports: "
         "the game and the mod run with it",
         path, about, known);
    logf("proxy: %d of the %d functions this proxy passes on are not in it (%s%s) - the game imports none of them, "
         "and they answer 0; forwarding the other %d",
         n_missing, PROXY_EXPORT_COUNT, listed, n_missing > 4 ? ", ..." : "", PROXY_EXPORT_COUNT - n_missing);
  }
  logf("proxy: %s imports %d Steam function(s) by name - the original has them all", program(),
       static_cast<int>(imports.size()));
  return true;
}

}  // namespace re4cc::proxy
