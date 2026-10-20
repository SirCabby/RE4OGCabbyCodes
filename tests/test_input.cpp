// The input guard (src/input.cpp) - the game's DirectInput mouse let go and its
// keyboard kept from what is typed into the panel - with the game's devices as
// Wine's dinput8 makes them, or as the Steam overlay hands them out on Windows,
// outside the game, under Wine:
//
//   i686-w64-mingw32-g++ -std=c++20 -O2 -Wall -DWIN32_LEAN_AND_MEAN -DNOMINMAX -static -Isrc tests/test_input.cpp
//       src/input.cpp src/config.cpp src/crash.cpp tests/log_stub.cpp -ldinput8 -ldxguid -luser32
//       -o tests/build/test_input.exe
//   wine tests/build/test_input.exe plain        (Wine's own dinput8: one class for every device)
//   wine tests/build/test_input.exe overlay      (the Steam overlay's wrappers: a class for the mouse, one for the keyboard)
//
// The exe plays bio4.exe: it imports DirectInput8Create itself - the import
// slot the guard hooks - and opens the mouse first, then the keyboard, as
// bio4.exe does. The panel's state is the test's (the overlay's three answers
// are stubbed below).
//
// "overlay" plays gameoverlayrenderer.dll on Windows, in front of the game's
// DirectInput8Create before the guard: its RTTI names IWrapDirectInput8,
// IWrapMouseDevice8 and IWrapKeyboardDevice8 - the game gets the overlay's
// objects, and its mouse and keyboard are of two classes. Their reads hand back
// a key and a button held, so what the guard takes away shows. Until
// 2026-10-19 the guard hooked only the first class it saw (the mouse's): the
// keyboard went unguarded beside the Steam overlay.
#define DIRECTINPUT_VERSION 0x0800
#include <windows.h>
#include <dinput.h>

#include <cstdio>
#include <cstring>

#include "input.h"
#include "overlay.h"

// The panel's state, as the overlay would give it.
namespace {
bool g_visible = false, g_capture_mouse = false, g_capture_keyboard = false;
}
namespace re4cc::overlay {
bool visible() { return g_visible; }
bool capturing_mouse() { return g_capture_mouse; }
bool capturing_keyboard() { return g_capture_keyboard; }
}  // namespace re4cc::overlay

namespace {

int g_failures = 0;
bool g_overlay = false;

void check(bool ok, const char* what) {
  std::printf("  %s  %s\n", ok ? "ok  " : "FAIL", what);
  if (!ok) ++g_failures;
}

void** vtable_of(const void* obj) { return *static_cast<void** const*>(obj); }

void write_slot(void** vt, int slot, void* fn) {
  DWORD old = 0;
  VirtualProtect(&vt[slot], sizeof(void*), PAGE_EXECUTE_READWRITE, &old);
  vt[slot] = fn;
  VirtualProtect(&vt[slot], sizeof(void*), old, &old);
}

void** import_slot(HMODULE m, const char* dll, const char* fn) {
  auto* base = reinterpret_cast<BYTE*>(m);
  auto* nt = reinterpret_cast<IMAGE_NT_HEADERS*>(base + reinterpret_cast<IMAGE_DOS_HEADER*>(base)->e_lfanew);
  const IMAGE_DATA_DIRECTORY& dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
  for (auto* d = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(base + dir.VirtualAddress); d->Name; ++d) {
    if (_stricmp(reinterpret_cast<char*>(base + d->Name), dll) != 0) continue;
    auto* names = reinterpret_cast<IMAGE_THUNK_DATA*>(base + (d->OriginalFirstThunk ? d->OriginalFirstThunk : d->FirstThunk));
    auto* slots = reinterpret_cast<void**>(base + d->FirstThunk);
    for (int i = 0; names[i].u1.AddressOfData; ++i) {
      if (IMAGE_SNAP_BY_ORDINAL(names[i].u1.Ordinal)) continue;
      auto* by_name = reinterpret_cast<IMAGE_IMPORT_BY_NAME*>(base + names[i].u1.AddressOfData);
      if (std::strcmp(reinterpret_cast<char*>(by_name->Name), fn) == 0) return &slots[i];
    }
  }
  return nullptr;
}

// --- the Steam overlay's DirectInput --------------------------------------------------------------
// A device of the overlay's, around dinput8's: everything goes on to it but the
// reads, which hand back a key held (A, and Escape) or a button held and a move.
struct OvDevice : IDirectInputDevice8A {
  IDirectInputDevice8A* in;
  LONG refs = 1;
  int acquires = 0, unacquires = 0;
  explicit OvDevice(IDirectInputDevice8A* real) : in(real) {}
  virtual ~OvDevice() = default;
  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** out) override {
    if (!out) return E_POINTER;
    if (riid == IID_IUnknown || riid == IID_IDirectInputDevice8A) {
      AddRef();
      *out = this;
      return S_OK;
    }
    return in->QueryInterface(riid, out);
  }
  ULONG STDMETHODCALLTYPE AddRef() override { return InterlockedIncrement(&refs); }
  ULONG STDMETHODCALLTYPE Release() override {
    const LONG n = InterlockedDecrement(&refs);
    if (!n) {
      in->Release();
      delete this;
    }
    return n;
  }
  HRESULT STDMETHODCALLTYPE GetCapabilities(LPDIDEVCAPS c) override { return in->GetCapabilities(c); }
  HRESULT STDMETHODCALLTYPE EnumObjects(LPDIENUMDEVICEOBJECTSCALLBACKA cb, LPVOID ref, DWORD fl) override {
    return in->EnumObjects(cb, ref, fl);
  }
  HRESULT STDMETHODCALLTYPE GetProperty(REFGUID g, LPDIPROPHEADER p) override { return in->GetProperty(g, p); }
  HRESULT STDMETHODCALLTYPE SetProperty(REFGUID g, LPCDIPROPHEADER p) override { return in->SetProperty(g, p); }
  HRESULT STDMETHODCALLTYPE Acquire() override {
    ++acquires;
    return DI_OK;
  }
  HRESULT STDMETHODCALLTYPE Unacquire() override {
    ++unacquires;
    return DI_OK;
  }
  HRESULT STDMETHODCALLTYPE GetDeviceData(DWORD size, LPDIDEVICEOBJECTDATA data, LPDWORD inout, DWORD) override {
    if (!inout) return DIERR_INVALIDPARAM;
    if (data && *inout && size >= sizeof(DIDEVICEOBJECTDATA)) {
      std::memset(data, 0, sizeof(DIDEVICEOBJECTDATA));
      data[0].dwData = 0x80;
      *inout = 1;
    } else {
      *inout = 0;
    }
    return DI_OK;
  }
  HRESULT STDMETHODCALLTYPE SetDataFormat(LPCDIDATAFORMAT f) override { return in->SetDataFormat(f); }
  HRESULT STDMETHODCALLTYPE SetEventNotification(HANDLE e) override { return in->SetEventNotification(e); }
  HRESULT STDMETHODCALLTYPE SetCooperativeLevel(HWND w, DWORD fl) override { return in->SetCooperativeLevel(w, fl); }
  HRESULT STDMETHODCALLTYPE GetObjectInfo(LPDIDEVICEOBJECTINSTANCEA o, DWORD obj, DWORD how) override {
    return in->GetObjectInfo(o, obj, how);
  }
  HRESULT STDMETHODCALLTYPE GetDeviceInfo(LPDIDEVICEINSTANCEA i) override { return in->GetDeviceInfo(i); }
  HRESULT STDMETHODCALLTYPE RunControlPanel(HWND w, DWORD fl) override { return in->RunControlPanel(w, fl); }
  HRESULT STDMETHODCALLTYPE Initialize(HINSTANCE h, DWORD v, REFGUID g) override { return in->Initialize(h, v, g); }
  HRESULT STDMETHODCALLTYPE CreateEffect(REFGUID g, LPCDIEFFECT e, LPDIRECTINPUTEFFECT* out, LPUNKNOWN outer) override {
    return in->CreateEffect(g, e, out, outer);
  }
  HRESULT STDMETHODCALLTYPE EnumEffects(LPDIENUMEFFECTSCALLBACKA cb, LPVOID ref, DWORD t) override {
    return in->EnumEffects(cb, ref, t);
  }
  HRESULT STDMETHODCALLTYPE GetEffectInfo(LPDIEFFECTINFOA i, REFGUID g) override { return in->GetEffectInfo(i, g); }
  HRESULT STDMETHODCALLTYPE GetForceFeedbackState(LPDWORD out) override { return in->GetForceFeedbackState(out); }
  HRESULT STDMETHODCALLTYPE SendForceFeedbackCommand(DWORD fl) override { return in->SendForceFeedbackCommand(fl); }
  HRESULT STDMETHODCALLTYPE EnumCreatedEffectObjects(LPDIENUMCREATEDEFFECTOBJECTSCALLBACK cb, LPVOID ref, DWORD fl) override {
    return in->EnumCreatedEffectObjects(cb, ref, fl);
  }
  HRESULT STDMETHODCALLTYPE Escape(LPDIEFFESCAPE e) override { return in->Escape(e); }
  HRESULT STDMETHODCALLTYPE Poll() override { return in->Poll(); }
  HRESULT STDMETHODCALLTYPE SendDeviceData(DWORD size, LPCDIDEVICEOBJECTDATA d, LPDWORD inout, DWORD fl) override {
    return in->SendDeviceData(size, d, inout, fl);
  }
  HRESULT STDMETHODCALLTYPE EnumEffectsInFile(LPCSTR f, LPDIENUMEFFECTSINFILECALLBACK cb, LPVOID ref, DWORD fl) override {
    return in->EnumEffectsInFile(f, cb, ref, fl);
  }
  HRESULT STDMETHODCALLTYPE WriteEffectToFile(LPCSTR f, DWORD n, LPDIFILEEFFECT e, DWORD fl) override {
    return in->WriteEffectToFile(f, n, e, fl);
  }
  HRESULT STDMETHODCALLTYPE BuildActionMap(LPDIACTIONFORMATA a, LPCSTR u, DWORD fl) override {
    return in->BuildActionMap(a, u, fl);
  }
  HRESULT STDMETHODCALLTYPE SetActionMap(LPDIACTIONFORMATA a, LPCSTR u, DWORD fl) override {
    return in->SetActionMap(a, u, fl);
  }
  HRESULT STDMETHODCALLTYPE GetImageInfo(LPDIDEVICEIMAGEINFOHEADERA h) override { return in->GetImageInfo(h); }
};

// IWrapMouseDevice8 and IWrapKeyboardDevice8: a class each.
struct OvMouse final : OvDevice {
  using OvDevice::OvDevice;
  HRESULT STDMETHODCALLTYPE GetDeviceState(DWORD size, LPVOID data) override {
    if (!data || size < sizeof(DIMOUSESTATE)) return DIERR_INVALIDPARAM;
    std::memset(data, 0, size);
    auto* m = static_cast<DIMOUSESTATE*>(data);
    m->lX = 5;
    m->rgbButtons[0] = 0x80;
    return DI_OK;
  }
};
struct OvKeyboard final : OvDevice {
  using OvDevice::OvDevice;
  HRESULT STDMETHODCALLTYPE GetDeviceState(DWORD size, LPVOID data) override {
    if (!data || size < 256) return DIERR_INVALIDPARAM;
    std::memset(data, 0, size);
    static_cast<BYTE*>(data)[DIK_A] = 0x80;
    static_cast<BYTE*>(data)[DIK_ESCAPE] = 0x80;
    return DI_OK;
  }
};

// IWrapDirectInput8: the IDirectInput8 the overlay hands out, around dinput8's.
struct OvDI final : IDirectInput8A {
  IDirectInput8A* in;
  LONG refs = 1;
  explicit OvDI(IDirectInput8A* real) : in(real) {}
  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** out) override {
    if (!out) return E_POINTER;
    if (riid == IID_IUnknown || riid == IID_IDirectInput8A) {
      AddRef();
      *out = this;
      return S_OK;
    }
    return in->QueryInterface(riid, out);
  }
  ULONG STDMETHODCALLTYPE AddRef() override { return InterlockedIncrement(&refs); }
  ULONG STDMETHODCALLTYPE Release() override {
    const LONG n = InterlockedDecrement(&refs);
    if (!n) {
      in->Release();
      delete this;
    }
    return n;
  }
  HRESULT STDMETHODCALLTYPE CreateDevice(REFGUID g, LPDIRECTINPUTDEVICE8A* out, LPUNKNOWN outer) override {
    IDirectInputDevice8A* real = nullptr;
    const HRESULT hr = in->CreateDevice(g, &real, outer);
    if (FAILED(hr) || !out) return hr;
    if (g == GUID_SysMouse) *out = new OvMouse(real);
    else if (g == GUID_SysKeyboard) *out = new OvKeyboard(real);
    else *out = real;
    return hr;
  }
  HRESULT STDMETHODCALLTYPE EnumDevices(DWORD t, LPDIENUMDEVICESCALLBACKA cb, LPVOID ref, DWORD fl) override {
    return in->EnumDevices(t, cb, ref, fl);
  }
  HRESULT STDMETHODCALLTYPE GetDeviceStatus(REFGUID g) override { return in->GetDeviceStatus(g); }
  HRESULT STDMETHODCALLTYPE RunControlPanel(HWND w, DWORD fl) override { return in->RunControlPanel(w, fl); }
  HRESULT STDMETHODCALLTYPE Initialize(HINSTANCE h, DWORD v) override { return in->Initialize(h, v); }
  HRESULT STDMETHODCALLTYPE FindDevice(REFGUID g, LPCSTR name, LPGUID out) override { return in->FindDevice(g, name, out); }
  HRESULT STDMETHODCALLTYPE EnumDevicesBySemantics(LPCSTR u, LPDIACTIONFORMATA a, LPDIENUMDEVICESBYSEMANTICSCBA cb,
                                                   LPVOID ref, DWORD fl) override {
    return in->EnumDevicesBySemantics(u, a, cb, ref, fl);
  }
  HRESULT STDMETHODCALLTYPE ConfigureDevices(LPDICONFIGUREDEVICESCALLBACK cb, LPDICONFIGUREDEVICESPARAMSA p, DWORD fl,
                                             LPVOID ref) override {
    return in->ConfigureDevices(cb, p, fl, ref);
  }
};

using Create8Fn = HRESULT(WINAPI*)(HINSTANCE, DWORD, REFIID, LPVOID*, LPUNKNOWN);
Create8Fn g_real_create8 = nullptr;

HRESULT WINAPI ov_create8(HINSTANCE inst, DWORD version, REFIID riid, LPVOID* out, LPUNKNOWN outer) {
  IDirectInput8A* real = nullptr;
  const HRESULT hr = g_real_create8(inst, version, riid, reinterpret_cast<void**>(&real), outer);
  if (FAILED(hr) || !out) return hr;
  *out = new OvDI(real);
  return hr;
}

// --- the game ------------------------------------------------------------------------------------
bool keyboard_shows(IDirectInputDevice8A* kb, bool a, bool esc) {
  BYTE keys[256] = {};
  return SUCCEEDED(kb->GetDeviceState(sizeof(keys), keys)) && (keys[DIK_A] != 0) == a && (keys[DIK_ESCAPE] != 0) == esc;
}
bool mouse_shows(IDirectInputDevice8A* m, bool moved) {
  DIMOUSESTATE s{};
  return SUCCEEDED(m->GetDeviceState(sizeof(s), &s)) && ((s.lX == 5 && s.rgbButtons[0]) == moved) &&
         (moved || (!s.lX && !s.rgbButtons[0]));
}
DWORD events(IDirectInputDevice8A* d) {
  DIDEVICEOBJECTDATA data[4] = {};
  DWORD n = 4;
  return SUCCEEDED(d->GetDeviceData(sizeof(DIDEVICEOBJECTDATA), data, &n, 0)) ? n : 99;
}

int run() {
  void** slot = import_slot(GetModuleHandleA(nullptr), "dinput8.dll", "DirectInput8Create");
  check(slot != nullptr, "the exe's DirectInput8Create import found");
  if (!slot) return 1;
  g_real_create8 = reinterpret_cast<Create8Fn>(*slot);
  if (g_overlay) write_slot(slot, 0, reinterpret_cast<void*>(&ov_create8));  // the overlay is in first
  check(re4cc::input::install(GetModuleHandleA(nullptr)), "the guard hooked the exe's imports");
  // bio4.exe's DirectInput8Create, through its import; the mouse, then the keyboard.
  IDirectInput8A* di = nullptr;
  HRESULT hr = DirectInput8Create(GetModuleHandleA(nullptr), DIRECTINPUT_VERSION, IID_IDirectInput8A,
                                  reinterpret_cast<void**>(&di), nullptr);
  check(SUCCEEDED(hr) && di, "DirectInput8Create made an IDirectInput8");
  if (!di) return 1;
  IDirectInputDevice8A *mouse = nullptr, *kb = nullptr;
  check(SUCCEEDED(di->CreateDevice(GUID_SysMouse, &mouse, nullptr)) && mouse, "the mouse made");
  check(SUCCEEDED(di->CreateDevice(GUID_SysKeyboard, &kb, nullptr)) && kb, "the keyboard made");
  if (!mouse || !kb) return 1;
  if (g_overlay) {
    check(vtable_of(mouse) != vtable_of(kb), "the overlay's mouse and keyboard are of two classes");
    auto* om = static_cast<OvMouse*>(mouse);
    auto* ok = static_cast<OvKeyboard*>(kb);
    check(keyboard_shows(kb, true, true) && mouse_shows(mouse, true), "nothing taken while the panel takes nothing");
    g_capture_keyboard = true;
    check(keyboard_shows(kb, false, true), "a panel field has the keyboard: the game's keyboard reads no key but Escape");
    check(events(kb) == 0, "... and its buffered keys none");
    check(mouse_shows(mouse, true), "... and its mouse reads as it is");
    g_capture_keyboard = false;
    check(keyboard_shows(kb, true, true) && events(kb) == 1, "the field let go: the keyboard reads again");
    g_capture_mouse = true;
    check(mouse_shows(mouse, false) && events(mouse) == 0, "the pointer over the panel: the game's mouse reads nothing");
    check(keyboard_shows(kb, true, true), "... and its keyboard reads as it is");
    g_capture_mouse = false;
    // The panel comes up: the mouse is let go and the game may not take it back.
    g_visible = true;
    const int un = om->unacquires;
    re4cc::input::tick(true);
    check(om->unacquires == un + 1, "the panel up: the game's mouse let go (Unacquire, once)");
    int acq = om->acquires;
    check(mouse->Acquire() == DIERR_OTHERAPPHASPRIO && om->acquires == acq,
          "the game's Acquire of its mouse is refused (another application has priority)");
    check(mouse_shows(mouse, false), "the panel up: the game's mouse reads nothing");
    const int kacq = ok->acquires;
    check(kb->Acquire() == DI_OK && ok->acquires == kacq + 1, "its keyboard's Acquire goes through");
    g_visible = false;
    re4cc::input::tick(false);
    acq = om->acquires;
    check(mouse->Acquire() == DI_OK && om->acquires == acq + 1 && mouse_shows(mouse, true),
          "the panel gone: the game takes its mouse back");
    re4cc::input::uninstall();
    g_capture_keyboard = g_capture_mouse = true;
    check(keyboard_shows(kb, true, true) && mouse_shows(mouse, true), "the guard taken out: both read as they are");
    g_capture_keyboard = g_capture_mouse = false;
  } else {
    check(vtable_of(mouse) == vtable_of(kb), "Wine's mouse and keyboard share one class");
    // dinput8's own devices read the real ones (nothing held here): what is
    // checked is that the guard's answers come and the rest goes through.
    g_visible = true;
    re4cc::input::tick(true);
    check(mouse->Acquire() == DIERR_OTHERAPPHASPRIO, "the panel up: the game's Acquire of its mouse is refused");
    BYTE keys[256];
    std::memset(keys, 0xAA, sizeof(keys));
    g_capture_keyboard = true;
    hr = kb->GetDeviceState(sizeof(keys), keys);
    check(FAILED(hr) || (keys[DIK_A] == 0 && keys[DIK_SPACE] == 0),
          "a panel field has the keyboard: the game's keyboard reads no key (or its own error)");
    g_capture_keyboard = false;
    g_visible = false;
    re4cc::input::tick(false);
    check(mouse->Acquire() != DIERR_OTHERAPPHASPRIO, "the panel gone: the mouse's Acquire is dinput8's again");
    re4cc::input::uninstall();
  }
  kb->Release();
  mouse->Release();
  di->Release();
  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) {
    std::printf("usage: test_input.exe plain|overlay\n");
    return 2;
  }
  g_overlay = std::strcmp(argv[1], "overlay") == 0;
  std::printf("the input guard, the game's devices %s:\n",
              g_overlay ? "as the Steam overlay hands them out (a class for the mouse, one for the keyboard)"
                        : "as Wine's dinput8 makes them");
  const int rc = run();
  std::printf("%s (%d failure(s))\n", g_failures || rc ? "FAILED" : "PASSED", g_failures);
  return g_failures || rc ? 1 : 0;
}
