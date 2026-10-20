#define DIRECTINPUT_VERSION 0x0800

#include "input.h"

#include <dinput.h>

#include <cstring>

#include "config.h"
#include "crash.h"
#include "log.h"
#include "mem.h"
#include "overlay.h"

namespace re4cc::input {
namespace {

// The two device GUIDs, spelled out so the build needs no dxguid import
// library: {6F1D2B60-D5A0-11CF-BFC7-444553540000} mouse, ...61 keyboard.
constexpr GUID kSysMouse = {0x6F1D2B60, 0xD5A0, 0x11CF, {0xBF, 0xC7, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00}};
constexpr GUID kSysKeyboard = {0x6F1D2B61, 0xD5A0, 0x11CF, {0xBF, 0xC7, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00}};

using Create8Fn = HRESULT(WINAPI*)(HINSTANCE, DWORD, REFIID, LPVOID*, LPUNKNOWN);
using CreateDeviceFn = HRESULT(__stdcall*)(IDirectInput8A*, REFGUID, LPDIRECTINPUTDEVICE8A*, LPUNKNOWN);
using AcquireFn = HRESULT(__stdcall*)(IDirectInputDevice8A*);
using GetStateFn = HRESULT(__stdcall*)(IDirectInputDevice8A*, DWORD, LPVOID);
using GetDataFn = HRESULT(__stdcall*)(IDirectInputDevice8A*, DWORD, LPDIDEVICEOBJECTDATA, LPDWORD, DWORD);
using GetAsyncKeyStateFn = SHORT(WINAPI*)(int);

Create8Fn g_real_create8 = nullptr;
CreateDeviceFn g_orig_create_device = nullptr;
GetAsyncKeyStateFn g_orig_gaks = nullptr;
uintptr_t g_di_vt = 0;      // IDirectInput8 vtable (CreateDevice patched)
uintptr_t g_iat_slot = 0;
uintptr_t g_gaks_slot = 0;

// Each class the game's devices come in, with what its three slots held before
// the guard's went in. Under Wine and in Windows' own dinput8 every device
// shares one class. With the Steam overlay on Windows the game gets the
// overlay's wrappers instead (IWrapDirectInput8, and IWrapMouseDevice8 and
// IWrapKeyboardDevice8 for the devices - their names in its RTTI): two classes,
// and the keyboard is guarded only once its class is hooked too.
constexpr int kAcquire = 7, kGetState = 9, kGetData = 10;
struct DeviceClass {
  uintptr_t vt;
  AcquireFn acquire;
  GetStateFn get_state;
  GetDataFn get_data;
};
constexpr int kMaxClasses = 4;
DeviceClass g_classes[kMaxClasses];
volatile LONG g_nclasses = 0;

// Every DirectInput device the game opens, so the shared vtable hook can tell
// the mouse from the keyboard from a pad by the object it was called on.
enum Kind { kOther = 0, kMouse, kKeyboard };
struct Device {
  void* obj;
  Kind kind;
};
constexpr int kMaxDevices = 32;
Device g_devices[kMaxDevices];
volatile LONG g_ndevices = 0;
IDirectInputDevice8A* volatile g_mouse = nullptr;  // the game's mouse, the last one it opened
volatile LONG g_hold_mouse = 0;                    // the panel is up: the game may not have the mouse

Kind kind_of(void* obj) {
  const LONG n = g_ndevices;
  for (LONG i = 0; i < n && i < kMaxDevices; ++i)
    if (g_devices[i].obj == obj) return g_devices[i].kind;
  return kOther;
}

// A released device's address can come back as a new one, so an address that is
// already known is updated rather than appended.
void remember(void* obj, Kind kind) {
  const LONG n = g_ndevices;
  for (LONG i = 0; i < n && i < kMaxDevices; ++i)
    if (g_devices[i].obj == obj) {
      g_devices[i].kind = kind;
      return;
    }
  const LONG idx = InterlockedIncrement(&g_ndevices) - 1;
  if (idx >= kMaxDevices) return;
  g_devices[idx].obj = obj;
  g_devices[idx].kind = kind;
}

// What a hooked call on `obj` goes on to: its class's functions. An object of a
// hooked class has that class's vtable; one whose vtable is a copy another tool
// made of one later (the guard's functions in it) is known by the rest of its
// slots - QueryInterface to SetProperty, 0 to 6.
const DeviceClass* class_of(void* obj) {
  const LONG n = g_nclasses;
  if (n <= 0) return nullptr;
  const auto* vt = *static_cast<void* const* const*>(obj);
  for (LONG i = 0; i < n && i < kMaxClasses; ++i)
    if (g_classes[i].vt == reinterpret_cast<uintptr_t>(vt)) return &g_classes[i];
  for (LONG i = 0; i < n && i < kMaxClasses; ++i)
    if (!std::memcmp(reinterpret_cast<const void*>(g_classes[i].vt), vt, 7 * sizeof(void*))) return &g_classes[i];
  return &g_classes[0];
}

HRESULT __stdcall hk_acquire(IDirectInputDevice8A* self) {
  // While the panel has the pointer the game's exclusive mouse stays let go:
  // "another application has priority", the answer the game gets whenever its
  // window is not in front, and handles by trying again later.
  if (g_hold_mouse && kind_of(self) == kMouse) return DIERR_OTHERAPPHASPRIO;
  const DeviceClass* c = class_of(self);
  return c ? c->acquire(self) : DIERR_GENERIC;
}

HRESULT __stdcall hk_get_state(IDirectInputDevice8A* self, DWORD size, LPVOID data) {
  const DeviceClass* c = class_of(self);
  if (!c) return DIERR_GENERIC;
  const HRESULT hr = c->get_state(self, size, data);
  if (FAILED(hr) || !data || !size) return hr;
  const Kind kind = kind_of(self);
  if (kind == kMouse && (g_hold_mouse || overlay::capturing_mouse())) {
    std::memset(data, 0, size);  // no movement, no buttons while the panel has the pointer
  } else if (kind == kKeyboard && overlay::capturing_keyboard() && size >= 256) {
    // Escape and Alt stay the game's: they are how the pause is left.
    auto* keys = static_cast<unsigned char*>(data);
    const unsigned char esc = keys[DIK_ESCAPE], lalt = keys[DIK_LMENU], ralt = keys[DIK_RMENU];
    std::memset(data, 0, size);
    keys[DIK_ESCAPE] = esc;
    keys[DIK_LMENU] = lalt;
    keys[DIK_RMENU] = ralt;
  }
  return hr;
}

HRESULT __stdcall hk_get_data(IDirectInputDevice8A* self, DWORD obj_size, LPDIDEVICEOBJECTDATA data, LPDWORD inout,
                              DWORD flags) {
  const DeviceClass* c = class_of(self);
  if (!c) return DIERR_GENERIC;
  const HRESULT hr = c->get_data(self, obj_size, data, inout, flags);
  if (FAILED(hr) || !inout) return hr;
  const Kind kind = kind_of(self);
  // The buffer is still drained (the call went through), the game just does not
  // get told about it.
  if ((kind == kMouse && (g_hold_mouse || overlay::capturing_mouse())) ||
      (kind == kKeyboard && overlay::capturing_keyboard()))
    *inout = 0;
  return hr;
}

const char* kind_name(Kind kind) { return kind == kMouse ? "mouse" : kind == kKeyboard ? "keyboard" : "other device"; }

bool guards(const void* fn) {
  return fn == reinterpret_cast<const void*>(&hk_acquire) || fn == reinterpret_cast<const void*>(&hk_get_state) ||
         fn == reinterpret_cast<const void*>(&hk_get_data);
}

// The class of a device the game made gets the guard's three functions, the
// first time one of its devices is seen; its own go into g_classes first, so a
// call that reaches the guard always finds them.
void guard_class(uintptr_t vt, Kind kind) {
  if (!vt) return;
  const LONG n = g_nclasses;
  for (LONG i = 0; i < n && i < kMaxClasses; ++i)
    if (g_classes[i].vt == vt) return;
  auto* const* slots = reinterpret_cast<void* const*>(vt);
  char where[MAX_PATH + 32];
  describe_address(reinterpret_cast<void*>(vt), where, sizeof(where));
  if (guards(slots[kAcquire]) || guards(slots[kGetState]) || guards(slots[kGetData])) {
    logf("input: the game's %s's class (vtable %p, %s) holds the guard's functions already - a copy of a guarded "
         "class's", kind_name(kind), reinterpret_cast<void*>(vt), where);
    return;
  }
  if (n >= kMaxClasses) {
    logf("input: the game's %s is of a %dth class (vtable %p, %s) - its input is not guarded", kind_name(kind),
         static_cast<int>(n) + 1, reinterpret_cast<void*>(vt), where);
    return;
  }
  DeviceClass& c = g_classes[n];
  c.vt = vt;
  c.acquire = reinterpret_cast<AcquireFn>(slots[kAcquire]);
  c.get_state = reinterpret_cast<GetStateFn>(slots[kGetState]);
  c.get_data = reinterpret_cast<GetDataFn>(slots[kGetData]);
  InterlockedIncrement(&g_nclasses);
  mem::hook_vtable(vt, kAcquire, reinterpret_cast<void*>(&hk_acquire));
  mem::hook_vtable(vt, kGetState, reinterpret_cast<void*>(&hk_get_state));
  mem::hook_vtable(vt, kGetData, reinterpret_cast<void*>(&hk_get_data));
  logf("input: %s DirectInput device class (vtable %p, %s) hooked - Acquire/GetDeviceState/GetDeviceData, from the "
       "game's %s", n ? "another" : "the", reinterpret_cast<void*>(vt), where, kind_name(kind));
}

HRESULT __stdcall hk_create_device(IDirectInput8A* self, REFGUID guid, LPDIRECTINPUTDEVICE8A* out, LPUNKNOWN outer) {
  const HRESULT hr = g_orig_create_device(self, guid, out, outer);
  if (FAILED(hr) || !out || !*out) return hr;
  const Kind kind = !std::memcmp(&guid, &kSysMouse, sizeof(GUID))      ? kMouse
                    : !std::memcmp(&guid, &kSysKeyboard, sizeof(GUID)) ? kKeyboard
                                                                       : kOther;
  remember(*out, kind);
  if (kind == kMouse) g_mouse = *out;
  guard_class(mem::read<uintptr_t>(reinterpret_cast<uintptr_t>(*out)), kind);
  logf("input: game opened DirectInput device %p (%s)", static_cast<void*>(*out), kind_name(kind));
  return hr;
}

HRESULT WINAPI hk_create8(HINSTANCE inst, DWORD version, REFIID riid, LPVOID* out, LPUNKNOWN outer) {
  const HRESULT hr = g_real_create8(inst, version, riid, out, outer);
  if (FAILED(hr) || !out || !*out || g_orig_create_device) return hr;
  const uintptr_t vt = mem::read<uintptr_t>(reinterpret_cast<uintptr_t>(*out));
  if (!vt) return hr;
  g_orig_create_device =
      reinterpret_cast<CreateDeviceFn>(mem::hook_vtable(vt, 3, reinterpret_cast<void*>(&hk_create_device)));
  g_di_vt = vt;
  logf("input: DirectInput8Create intercepted (version 0x%X, thread %lu); hooked CreateDevice", version,
       GetCurrentThreadId());
  return hr;
}

SHORT WINAPI hk_get_async_key_state(int vk) {
  switch (vk) {
    case VK_ESCAPE:
    case VK_MENU:
    case VK_LMENU:
    case VK_RMENU:
      break;  // always the game's (Alt+Enter and Alt+F4 are read this way)
    default:
      if (overlay::capturing_keyboard()) return 0;
      break;
  }
  return g_orig_gaks(vk);
}

}  // namespace

bool install(HMODULE exe) {
  if (config::get().disable_input) {
    logf("input: guard disabled by config");
    return false;
  }
  bool ok = false;
  if (void* prev = mem::iat_hook(exe, "DINPUT8.dll", "DirectInput8Create", reinterpret_cast<void*>(&hk_create8))) {
    g_real_create8 = reinterpret_cast<Create8Fn>(prev);
    g_iat_slot = mem::iat_slot(exe, "DINPUT8.dll", "DirectInput8Create");
    logf("input: DirectInput8Create import hooked (original %p)", prev);
    ok = true;
  } else {
    logf("input: DirectInput8Create is not in the exe's import table - mouse clicks may reach the game through it");
  }
  if (void* prev = mem::iat_hook(exe, "USER32.dll", "GetAsyncKeyState", reinterpret_cast<void*>(&hk_get_async_key_state))) {
    g_orig_gaks = reinterpret_cast<GetAsyncKeyStateFn>(prev);
    g_gaks_slot = mem::iat_slot(exe, "USER32.dll", "GetAsyncKeyState");
    logf("input: GetAsyncKeyState import hooked (original %p)", prev);
    ok = true;
  }
  return ok;
}

void tick(bool panel_up) {
  if (g_nclasses <= 0) return;
  const bool hold = panel_up && overlay::visible();
  const bool was = g_hold_mouse != 0;
  InterlockedExchange(&g_hold_mouse, hold ? 1 : 0);
  if (hold && !was) {
    // Let go of the mouse now (the game's thread, which is the one that reads
    // it); the Acquire hook keeps the game from taking it back until the panel
    // goes, and the game re-acquires by itself after that.
    if (IDirectInputDevice8A* m = g_mouse) m->Unacquire();
    logf("input: the panel is up - the game's mouse is let go");
  } else if (!hold && was) {
    logf("input: the panel is gone - the game takes its mouse back");
  }
}

void uninstall() {
  InterlockedExchange(&g_hold_mouse, 0);
  // Each slot only while it still holds the guard's function: one another tool
  // put in since is left to it.
  auto put_back = [](uintptr_t vt, int slot, const void* hook, void* fn) {
    const uintptr_t at = vt + static_cast<uintptr_t>(slot) * sizeof(void*);
    if (fn && mem::readable(at, sizeof(void*)) && mem::read<const void*>(at) == hook) mem::write<void*>(at, fn);
  };
  const LONG n = g_nclasses;
  for (LONG i = 0; i < n && i < kMaxClasses; ++i) {
    const DeviceClass& c = g_classes[i];
    put_back(c.vt, kAcquire, reinterpret_cast<const void*>(&hk_acquire), reinterpret_cast<void*>(c.acquire));
    put_back(c.vt, kGetState, reinterpret_cast<const void*>(&hk_get_state), reinterpret_cast<void*>(c.get_state));
    put_back(c.vt, kGetData, reinterpret_cast<const void*>(&hk_get_data), reinterpret_cast<void*>(c.get_data));
  }
  if (g_di_vt && g_orig_create_device && mem::readable(reinterpret_cast<void*>(g_di_vt), 4 * sizeof(void*)))
    mem::write<void*>(g_di_vt + 3 * sizeof(void*), reinterpret_cast<void*>(g_orig_create_device));
  if (g_iat_slot && g_real_create8 && mem::read<uintptr_t>(g_iat_slot) == reinterpret_cast<uintptr_t>(&hk_create8))
    mem::write<uintptr_t>(g_iat_slot, reinterpret_cast<uintptr_t>(g_real_create8));
  if (g_gaks_slot && g_orig_gaks && mem::read<uintptr_t>(g_gaks_slot) == reinterpret_cast<uintptr_t>(&hk_get_async_key_state))
    mem::write<uintptr_t>(g_gaks_slot, reinterpret_cast<uintptr_t>(g_orig_gaks));
  g_orig_create_device = nullptr;
  g_di_vt = 0;
  logf("input hooks removed");
}

}  // namespace re4cc::input
