// The panel's D3D9 hooks (src/overlay_dx9.cpp, src/adopt.cpp) with the Steam overlay's way of hooking, or
// Windows' compatibility shim, outside the game, under Wine:
//
//   i686-w64-mingw32-g++ -std=c++20 -O2 -Wall -DWIN32_LEAN_AND_MEAN -static tests/test_adopt.cpp -ld3d9 -ldxguid -luser32
//       -o tests/build/test_adopt.exe
//   cp build/steam_api.dll <dir>/; cp <the game's own steam_api.dll> <dir>/steam_api_orig.dll     (a scratch folder)
//   RE4CC_ADOPT=1 wine tests/build/test_adopt.exe 'Z:\<dir>\' overlay adopt
//   RE4CC_ADOPT=1 wine tests/build/test_adopt.exe 'Z:\<dir>\' shim adopt
//   RE4CC_ADOPT=1 wine tests/build/test_adopt.exe 'Z:\<dir>\' none adopt
//   wine tests/build/test_adopt.exe 'Z:\<dir>\' none class          (Wine's way, as before)
//
// One process per run (the proxy keeps its state; RE4CC_ADOPT makes it take
// the Windows path under Wine). A prefix with Wine's null graphics driver
// (HKCU\Software\Wine\Drivers, Graphics = null) keeps it off the display: it
// still makes devices. The exe plays bio4.exe: it imports Direct3DCreate9
// itself - the import slot the proxy hooks - and loads the proxy on its primary
// thread, then makes its window (class RE4HDWnd), its IDirect3D9 and its device
// on a render thread of its own, as bio4.exe's GX thread does, and presents
// through the device's Present (bio4.exe never presents through a swap chain).
// The proxy's ini draws the panel on every frame (AlwaysShow) with no game to
// look for.
//
// "overlay" plays the Steam overlay on Windows (gameoverlayrenderer.dll; its RTTI
// names IWrapIDirect3D9, and no wrapper of the device). Installed before the
// proxy, its Direct3DCreate9 hands out an IDirect3D9 of its own whose
// CreateDevice hooks every device as it is made - CreateAdditionalSwapChain,
// GetSwapChain, Reset, Present - and its GetSwapChain and
// CreateAdditionalSwapChain hooks hook the swap chain they hand out, its
// Present, on EVERY call: each by what the slot holds at that moment (here by
// writing its own function into the slot, which a caller of the slot meets the
// same way as the overlay's jump in the function), keeping ONE saved original
// per hook and skipping a slot it hooked already (Fear3CabbyCodes,
// RE1-1996CabbyCodes, RE3-1999CabbyCodes). With the proxy's functions in the
// classes' vtables the second device's pass makes the overlay take them for
// its originals, and the two call each other: a stack overflow on Windows.
// Here the overlay's hooks count their depth and give up past 4 (a "LOOP"
// failure): "overlay class" shows it. With the Windows path the device and its
// swap chains must come back with vtables of their own holding the proxy's
// hooks, the classes' vtables must hold only the overlay's, every present must
// pass through the overlay exactly once, and a device for a window that is not
// the game's must keep its class's vtable.
//
// "shim" plays Windows' compatibility shim (apphelp.dll; RE3-1999CabbyCodes met
// one for ResidentEvil3.exe, 2026-10-08). It is in the exe's Direct3DCreate9
// import before the proxy loads; every call writes its own CreateDevice into the
// IDirect3D9 class's vtable, over whatever is there, and that CreateDevice calls
// d3d9's own. The game's IDirect3D9 must have a vtable of its own, and every
// CreateDevice must go on through the shim.
#include <windows.h>
#include <d3d9.h>

#include <cstdio>
#include <cstring>
#include <string>

namespace {

int g_failures = 0;
HMODULE g_proxy = nullptr;
bool g_expect_adopt = false;
bool g_overlay = false;
bool g_shim = false;
std::string g_dir;

void check(bool ok, const char* what) {
  std::printf("  %s  %s\n", ok ? "ok  " : "FAIL", what);
  if (!ok) ++g_failures;
}

bool in_proxy(const void* fn) {
  HMODULE owner = nullptr;
  return GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            static_cast<LPCSTR>(fn), &owner) &&
         owner == g_proxy;
}

void** vtable_of(const void* obj) { return *static_cast<void** const*>(obj); }

void write_slot(void** vt, int slot, void* fn) {
  DWORD old = 0;
  VirtualProtect(&vt[slot], sizeof(void*), PAGE_EXECUTE_READWRITE, &old);
  vt[slot] = fn;
  VirtualProtect(&vt[slot], sizeof(void*), old, &old);
}

// The import slot of `fn` from `dll` in module `m`.
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

using Create9Fn = IDirect3D9*(WINAPI*)(UINT);
using CreateDeviceFn = HRESULT(STDMETHODCALLTYPE*)(IDirect3D9*, UINT, D3DDEVTYPE, HWND, DWORD, D3DPRESENT_PARAMETERS*,
                                                   IDirect3DDevice9**);
using ResetFn = HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice9*, D3DPRESENT_PARAMETERS*);
using Present9Fn = HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice9*, const RECT*, const RECT*, HWND, const RGNDATA*);
using GetSwapFn = HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice9*, UINT, IDirect3DSwapChain9**);
using CreateAddFn = HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice9*, D3DPRESENT_PARAMETERS*, IDirect3DSwapChain9**);
using SwapPresentFn = HRESULT(STDMETHODCALLTYPE*)(IDirect3DSwapChain9*, const RECT*, const RECT*, HWND, const RGNDATA*,
                                                  DWORD);

// --- the Steam overlay ----------------------------------------------------------------------------
struct OvHook {
  void* saved = nullptr;  // ONE original, whatever the slot held at the last pass
  int calls = 0;
  int depth = 0;
  bool looped = false;
};
OvHook g_reset, g_present9, g_get_swap, g_create_add, g_swap_present;
int g_ov_devices = 0;

template <typename Fn, typename... A>
HRESULT through(OvHook& h, A... a) {
  ++h.calls;
  if (h.depth >= 4) {
    h.looped = true;
    return E_FAIL;
  }
  ++h.depth;
  const HRESULT hr = reinterpret_cast<Fn>(h.saved)(a...);
  --h.depth;
  return hr;
}

// Each slot, unless it is the overlay's already.
void ov_hook(void** vt, int slot, void* hook, OvHook& h) {
  if (vt[slot] == hook) return;
  h.saved = vt[slot];
  write_slot(vt, slot, hook);
}

HRESULT STDMETHODCALLTYPE ov_reset(IDirect3DDevice9* d, D3DPRESENT_PARAMETERS* pp) { return through<ResetFn>(g_reset, d, pp); }
HRESULT STDMETHODCALLTYPE ov_present9(IDirect3DDevice9* d, const RECT* a, const RECT* b, HWND w, const RGNDATA* r) {
  return through<Present9Fn>(g_present9, d, a, b, w, r);
}
HRESULT STDMETHODCALLTYPE ov_swap_present(IDirect3DSwapChain9* sc, const RECT* a, const RECT* b, HWND w, const RGNDATA* r,
                                          DWORD f) {
  return through<SwapPresentFn>(g_swap_present, sc, a, b, w, r, f);
}
// D3D9Device_GetSwapChain / _CreateAdditionalSwapChain: the swap chain handed out gets its Present hooked, every time.
HRESULT STDMETHODCALLTYPE ov_get_swap(IDirect3DDevice9* d, UINT i, IDirect3DSwapChain9** out) {
  ++g_get_swap.calls;
  const HRESULT hr = reinterpret_cast<GetSwapFn>(g_get_swap.saved)(d, i, out);
  if (SUCCEEDED(hr) && out && *out) ov_hook(vtable_of(*out), 3, reinterpret_cast<void*>(&ov_swap_present), g_swap_present);
  return hr;
}
HRESULT STDMETHODCALLTYPE ov_create_add(IDirect3DDevice9* d, D3DPRESENT_PARAMETERS* pp, IDirect3DSwapChain9** out) {
  ++g_create_add.calls;
  const HRESULT hr = reinterpret_cast<CreateAddFn>(g_create_add.saved)(d, pp, out);
  if (SUCCEEDED(hr) && out && *out) ov_hook(vtable_of(*out), 3, reinterpret_cast<void*>(&ov_swap_present), g_swap_present);
  return hr;
}

bool looped() { return g_reset.looped || g_present9.looped || g_swap_present.looped; }

// IWrapIDirect3D9: the IDirect3D9 the overlay hands the game, around d3d9's.
// Its CreateDevice makes the real device and hooks it.
struct OvD3D final : IDirect3D9 {
  IDirect3D9* in;
  LONG refs = 1;
  explicit OvD3D(IDirect3D9* real) : in(real) {}
  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** out) override {
    if (!out) return E_POINTER;
    if (riid == IID_IUnknown || riid == IID_IDirect3D9) {
      AddRef();
      *out = this;
      return S_OK;
    }
    *out = nullptr;
    return E_NOINTERFACE;
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
  HRESULT STDMETHODCALLTYPE RegisterSoftwareDevice(void* f) override { return in->RegisterSoftwareDevice(f); }
  UINT STDMETHODCALLTYPE GetAdapterCount() override { return in->GetAdapterCount(); }
  HRESULT STDMETHODCALLTYPE GetAdapterIdentifier(UINT a, DWORD f, D3DADAPTER_IDENTIFIER9* id) override {
    return in->GetAdapterIdentifier(a, f, id);
  }
  UINT STDMETHODCALLTYPE GetAdapterModeCount(UINT a, D3DFORMAT f) override { return in->GetAdapterModeCount(a, f); }
  HRESULT STDMETHODCALLTYPE EnumAdapterModes(UINT a, D3DFORMAT f, UINT m, D3DDISPLAYMODE* d) override {
    return in->EnumAdapterModes(a, f, m, d);
  }
  HRESULT STDMETHODCALLTYPE GetAdapterDisplayMode(UINT a, D3DDISPLAYMODE* d) override {
    return in->GetAdapterDisplayMode(a, d);
  }
  HRESULT STDMETHODCALLTYPE CheckDeviceType(UINT a, D3DDEVTYPE t, D3DFORMAT f, D3DFORMAT b, BOOL w) override {
    return in->CheckDeviceType(a, t, f, b, w);
  }
  HRESULT STDMETHODCALLTYPE CheckDeviceFormat(UINT a, D3DDEVTYPE t, D3DFORMAT f, DWORD u, D3DRESOURCETYPE r,
                                              D3DFORMAT c) override {
    return in->CheckDeviceFormat(a, t, f, u, r, c);
  }
  HRESULT STDMETHODCALLTYPE CheckDeviceMultiSampleType(UINT a, D3DDEVTYPE t, D3DFORMAT f, BOOL w, D3DMULTISAMPLE_TYPE m,
                                                       DWORD* q) override {
    return in->CheckDeviceMultiSampleType(a, t, f, w, m, q);
  }
  HRESULT STDMETHODCALLTYPE CheckDepthStencilMatch(UINT a, D3DDEVTYPE t, D3DFORMAT f, D3DFORMAT r, D3DFORMAT d) override {
    return in->CheckDepthStencilMatch(a, t, f, r, d);
  }
  HRESULT STDMETHODCALLTYPE CheckDeviceFormatConversion(UINT a, D3DDEVTYPE t, D3DFORMAT s, D3DFORMAT d) override {
    return in->CheckDeviceFormatConversion(a, t, s, d);
  }
  HRESULT STDMETHODCALLTYPE GetDeviceCaps(UINT a, D3DDEVTYPE t, D3DCAPS9* c) override { return in->GetDeviceCaps(a, t, c); }
  HMONITOR STDMETHODCALLTYPE GetAdapterMonitor(UINT a) override { return in->GetAdapterMonitor(a); }
  HRESULT STDMETHODCALLTYPE CreateDevice(UINT a, D3DDEVTYPE t, HWND w, DWORD fl, D3DPRESENT_PARAMETERS* pp,
                                         IDirect3DDevice9** out) override {
    const HRESULT hr = in->CreateDevice(a, t, w, fl, pp, out);
    if (SUCCEEDED(hr) && out && *out) {
      ++g_ov_devices;
      void** vt = vtable_of(*out);
      ov_hook(vt, 13, reinterpret_cast<void*>(&ov_create_add), g_create_add);
      ov_hook(vt, 14, reinterpret_cast<void*>(&ov_get_swap), g_get_swap);
      ov_hook(vt, 16, reinterpret_cast<void*>(&ov_reset), g_reset);
      ov_hook(vt, 17, reinterpret_cast<void*>(&ov_present9), g_present9);
    }
    return hr;
  }
};

Create9Fn g_real_create9 = nullptr;
void** g_ov_d3d_vt = nullptr;  // OvD3D's class vtable

IDirect3D9* WINAPI ov_create9(UINT sdk) {
  IDirect3D9* real = g_real_create9(sdk);
  if (!real) return nullptr;
  auto* wrap = new OvD3D(real);
  g_ov_d3d_vt = vtable_of(wrap);
  return wrap;
}

// --- Windows' compatibility shim ------------------------------------------------------------------
void* g_shim_real_cd = nullptr;  // d3d9's own, saved the first time
int g_shim_cd_calls = 0;

HRESULT STDMETHODCALLTYPE shim_create_device(IDirect3D9* d3d, UINT a, D3DDEVTYPE t, HWND w, DWORD fl,
                                             D3DPRESENT_PARAMETERS* pp, IDirect3DDevice9** out) {
  ++g_shim_cd_calls;
  return reinterpret_cast<CreateDeviceFn>(g_shim_real_cd)(d3d, a, t, w, fl, pp, out);
}

IDirect3D9* WINAPI shim_create9(UINT sdk) {
  IDirect3D9* d3d = g_real_create9(sdk);
  if (d3d) {
    void** vt = vtable_of(d3d);
    if (!g_shim_real_cd) g_shim_real_cd = vt[16];
    write_slot(vt, 16, reinterpret_cast<void*>(&shim_create_device));  // whatever was there
  }
  return d3d;
}

// --- windows --------------------------------------------------------------------------------------
HWND make_window(const char* cls, const char* title) {
  WNDCLASSA wc{};
  wc.lpfnWndProc = DefWindowProcA;
  wc.hInstance = GetModuleHandleA(nullptr);
  wc.lpszClassName = cls;
  RegisterClassA(&wc);  // a second registration fails harmlessly
  return CreateWindowA(cls, title, WS_OVERLAPPEDWINDOW | WS_VISIBLE, 0, 0, 640, 480, nullptr, nullptr, wc.hInstance,
                       nullptr);
}

void pump() {
  MSG m;
  while (PeekMessageA(&m, nullptr, 0, 0, PM_REMOVE)) {
    TranslateMessage(&m);
    DispatchMessageA(&m);
  }
}

IDirect3DDevice9* make_device(IDirect3D9* d3d, HWND w, D3DPRESENT_PARAMETERS* pp) {
  *pp = D3DPRESENT_PARAMETERS{};
  pp->Windowed = TRUE;
  pp->SwapEffect = D3DSWAPEFFECT_DISCARD;
  pp->BackBufferFormat = D3DFMT_UNKNOWN;
  pp->hDeviceWindow = w;
  IDirect3DDevice9* dev = nullptr;
  const HRESULT hr = d3d->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, w, D3DCREATE_SOFTWARE_VERTEXPROCESSING, pp, &dev);
  if (FAILED(hr)) std::printf("  CreateDevice -> 0x%08lX\n", hr);
  return SUCCEEDED(hr) ? dev : nullptr;
}

// A frame as bio4.exe draws one: scenes, then the device's Present.
bool frame(IDirect3DDevice9* dev) {
  pump();
  dev->Clear(0, nullptr, D3DCLEAR_TARGET, D3DCOLOR_XRGB(20, 20, 40), 1.0f, 0);
  bool ok = SUCCEEDED(dev->BeginScene()) && SUCCEEDED(dev->EndScene());
  ok = SUCCEEDED(dev->BeginScene()) && SUCCEEDED(dev->EndScene()) && ok;
  return SUCCEEDED(dev->Present(nullptr, nullptr, nullptr, nullptr)) && ok;
}

bool swap_frame(IDirect3DDevice9* dev, IDirect3DSwapChain9* sc) {
  pump();
  dev->Clear(0, nullptr, D3DCLEAR_TARGET, D3DCOLOR_XRGB(40, 20, 20), 1.0f, 0);
  const bool ok = SUCCEEDED(dev->BeginScene()) && SUCCEEDED(dev->EndScene());
  return SUCCEEDED(sc->Present(nullptr, nullptr, nullptr, nullptr, 0)) && ok;
}

void game_device(const char* name, IDirect3D9* d3d, HWND w, void** dev_cls, void** sc_cls) {
  char what[220];
  D3DPRESENT_PARAMETERS pp;
  IDirect3DDevice9* dev = make_device(d3d, w, &pp);
  std::snprintf(what, sizeof(what), "%s made", name);
  check(dev != nullptr, what);
  if (!dev) return;
  void** vt = vtable_of(dev);
  const int hooked[] = {13, 14, 16, 17, 42};
  bool dev_ours = true, cls_clean = true;
  if (g_expect_adopt) {
    for (int s : hooked) dev_ours = dev_ours && in_proxy(vt[s]);
    for (int s : hooked) cls_clean = cls_clean && !in_proxy(dev_cls[s]);
    std::snprintf(what, sizeof(what), "%s has a vtable of its own, its 5 hooks the proxy's", name);
    check(vt != dev_cls && dev_ours, what);
    std::snprintf(what, sizeof(what), "%s: the device class's vtable holds no function of the proxy's", name);
    check(cls_clean, what);
  } else {
    const int class_slots[] = {13, 16, 17, 42};
    for (int s : class_slots) dev_ours = dev_ours && in_proxy(dev_cls[s]);
    std::snprintf(what, sizeof(what), "%s shares the device class's vtable, the proxy's hooks in it", name);
    check(vt == dev_cls && dev_ours, what);
  }
  // The game asks for its swap chain (the overlay too, as bio4.exe never does):
  // on the Windows path the proxy answers with the swap chain it adopted, so
  // the overlay's GetSwapChain hook never reads its copy.
  IDirect3DSwapChain9* sc = nullptr;
  const int gets_before = g_get_swap.calls;
  check(SUCCEEDED(dev->GetSwapChain(0, &sc)) && sc, "GetSwapChain(0) succeeds");
  if (sc) {
    void** svt = vtable_of(sc);
    if (g_expect_adopt) {
      std::snprintf(what, sizeof(what), "%s: its implicit swap chain has a vtable of its own, Present the proxy's; the "
                    "swap chain class's holds no function of the proxy's", name);
      check(svt != sc_cls && in_proxy(svt[3]) && !in_proxy(sc_cls[3]), what);
      if (g_overlay) {
        std::snprintf(what, sizeof(what), "%s: GetSwapChain(0) did not reach the overlay's GetSwapChain", name);
        check(g_get_swap.calls == gets_before, what);
      }
    } else {
      std::snprintf(what, sizeof(what), "%s: its swap chain shares the class's vtable, Present the proxy's", name);
      check(svt == sc_cls && in_proxy(sc_cls[3]), what);
    }
    sc->Release();
  }
  if (g_overlay) {
    std::snprintf(what, sizeof(what), "%s: none of the overlay's saved originals is the proxy's", name);
    check(!in_proxy(g_reset.saved) && !in_proxy(g_present9.saved) && !in_proxy(g_get_swap.saved) &&
              !in_proxy(g_create_add.saved) && !in_proxy(g_swap_present.saved),
          what);
  }
  int before = g_present9.calls;
  bool ok = true;
  for (int i = 0; i < 3; ++i) ok = frame(dev) && ok;
  std::snprintf(what, sizeof(what), "%s: 3 frames presented through the device%s", name,
                g_overlay ? ", each through the overlay once" : "");
  check(ok && !looped() && (!g_overlay || g_present9.calls - before == 3), what);
  const int resets_before = g_reset.calls;
  const HRESULT hr = dev->Reset(&pp);
  std::snprintf(what, sizeof(what), "%s: Reset succeeds%s (0x%08lX)", name, g_overlay ? ", through the overlay once" : "", hr);
  check(SUCCEEDED(hr) && !looped() && (!g_overlay || g_reset.calls - resets_before == 1), what);
  before = g_present9.calls;
  std::snprintf(what, sizeof(what), "%s: a frame after the Reset%s", name, g_overlay ? ", through the overlay once" : "");
  check(frame(dev) && !looped() && (!g_overlay || g_present9.calls - before == 1), what);
  // An additional swap chain for the same window, presented through.
  D3DPRESENT_PARAMETERS pp2 = pp;
  IDirect3DSwapChain9* add = nullptr;
  const HRESULT ha = dev->CreateAdditionalSwapChain(&pp2, &add);
  std::snprintf(what, sizeof(what), "%s: an additional swap chain is made (0x%08lX)", name, ha);
  check(SUCCEEDED(ha) && add, what);
  if (add) {
    if (g_expect_adopt) {
      std::snprintf(what, sizeof(what), "%s: the additional swap chain has a vtable of its own, Present the proxy's", name);
      check(vtable_of(add) != sc_cls && in_proxy(vtable_of(add)[3]), what);
    }
    before = g_swap_present.calls;
    std::snprintf(what, sizeof(what), "%s: a frame through the additional swap chain%s", name,
                  g_overlay ? ", through the overlay once" : "");
    check(swap_frame(dev, add) && !looped() && (!g_overlay || g_swap_present.calls - before == 1), what);
    add->Release();
  }
  dev->Release();
}

// --- the game's render thread ---------------------------------------------------------------------
DWORD WINAPI gx_thread(LPVOID) {
  // A device made by d3d9 alone, for a window of this thread that is not the
  // game's: it shows the classes' vtables, is never adopted, and presents
  // through the classes as they are.
  auto plain_create = reinterpret_cast<Create9Fn>(GetProcAddress(GetModuleHandleA("d3d9.dll"), "Direct3DCreate9"));
  IDirect3D9* plain = plain_create ? plain_create(D3D_SDK_VERSION) : nullptr;
  if (!plain) {
    std::printf("SKIP: no Direct3D 9\n");
    return 0;
  }
  void** d3d_cls = vtable_of(plain);
  // bio4.exe's Direct3DCreate9 call, through its import: the proxy's hook.
  IDirect3D9* d3d = Direct3DCreate9(D3D_SDK_VERSION);
  check(d3d != nullptr, "the game's Direct3DCreate9 (its import) made an IDirect3D9");
  if (!d3d) return 1;
  if (g_expect_adopt) {
    check(vtable_of(d3d) != d3d_cls && vtable_of(d3d) != g_ov_d3d_vt && in_proxy(vtable_of(d3d)[16]),
          "the game's IDirect3D9 has a vtable of its own, CreateDevice the proxy's");
    check(!in_proxy(d3d_cls[16]) && (!g_ov_d3d_vt || !in_proxy(g_ov_d3d_vt[16])),
          "the IDirect3D9 classes' vtables hold no function of the proxy's");
  } else {
    check(in_proxy(vtable_of(d3d)[16]), "IDirect3D9's CreateDevice is the proxy's (in place)");
  }
  if (g_shim) check(d3d_cls[16] == reinterpret_cast<void*>(&shim_create_device), "the class's CreateDevice is the shim's");
  HWND w = make_window("RE4HDWnd", "Resident Evil 4");
  HWND other_w = make_window("RE4CCOther", "a tool's window");
  D3DPRESENT_PARAMETERS pp;
  IDirect3DDevice9* other = make_device(d3d, other_w, &pp);
  check(other != nullptr, "a device for another window made");
  if (!other) return 1;
  IDirect3DSwapChain9* other_sc = nullptr;
  other->GetSwapChain(0, &other_sc);
  if (!other_sc) return 1;
  void** dev_cls = vtable_of(other);
  void** sc_cls = vtable_of(other_sc);
  if (g_expect_adopt)
    check(!in_proxy(dev_cls[42]) && !in_proxy(dev_cls[17]) && !in_proxy(sc_cls[3]),
          "the device for another window and its swap chain keep their classes' vtables");
  other_sc->Release();
  check(frame(other) && !looped(), "the device for another window presents");
  game_device("device 1", d3d, w, dev_cls, sc_cls);
  game_device("device 2", d3d, w, dev_cls, sc_cls);  // a second device: the overlay's device pass again
  check(frame(other) && !looped(), "the device for another window still presents");
  if (g_overlay) check(g_ov_devices == 3, "all 3 devices were made through the overlay's IDirect3D9");
  if (g_shim) check(g_shim_cd_calls == 3, "all 3 CreateDevice calls went on through the shim's");
  other->Release();
  d3d->Release();
  plain->Release();
  DestroyWindow(other_w);
  DestroyWindow(w);
  return 0;
}

int run() {
  void** slot = import_slot(GetModuleHandleA(nullptr), "d3d9.dll", "Direct3DCreate9");
  check(slot != nullptr, "the exe's Direct3DCreate9 import found");
  if (!slot) return 1;
  g_real_create9 = reinterpret_cast<Create9Fn>(*slot);
  // The overlay or the shim is in the exe's import before the proxy loads.
  if (g_overlay) write_slot(slot, 0, reinterpret_cast<void*>(&ov_create9));
  if (g_shim) write_slot(slot, 0, reinterpret_cast<void*>(&shim_create9));
  g_proxy = LoadLibraryA((g_dir + "steam_api.dll").c_str());
  check(g_proxy != nullptr, "proxy loaded");
  if (!g_proxy) return 1;
  check(in_proxy(*slot), "the proxy hooked the exe's Direct3DCreate9 import");
  DWORD rc = 1;
  HANDLE t = CreateThread(nullptr, 0, gx_thread, nullptr, 0, nullptr);
  if (t) {
    WaitForSingleObject(t, 60000);
    GetExitCodeThread(t, &rc);
    CloseHandle(t);
  }
  // The proxy's log: the panel came up on the game's window, on the render thread.
  std::string log;
  if (FILE* f = std::fopen((g_dir + "RE4OGCabbyCodes.log").c_str(), "rb")) {
    char buf[4096];
    for (size_t n; (n = std::fread(buf, 1, sizeof(buf), f)) > 0;) log.append(buf, n);
    std::fclose(f);
  }
  // (The class path hooks the first device it sees, whatever its window: here the other window's, made first.)
  if (g_expect_adopt) {
    check(log.find("overlay ready (hwnd=") != std::string::npos &&
              log.find("class 'RE4HDWnd', scale") != std::string::npos,
          "the proxy's log: the panel ready on the game's window (RE4HDWnd)");
    check(log.find("is not the game's") != std::string::npos, "the proxy's log: the other window's device left alone");
  } else {
    check(log.find("overlay ready (hwnd=") != std::string::npos, "the proxy's log: the panel ready");
  }
  return static_cast<int>(rc);
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 4) {
    std::printf("usage: test_adopt.exe <dir with steam_api.dll + steam_api_orig.dll> overlay|shim|none adopt|class\n");
    return 2;
  }
  g_dir = argv[1];
  if (!g_dir.empty() && g_dir.back() != '\\' && g_dir.back() != '/') g_dir += '\\';
  g_overlay = std::strstr(argv[2], "overlay") != nullptr;
  g_shim = std::strstr(argv[2], "shim") != nullptr;
  g_expect_adopt = std::strcmp(argv[3], "adopt") == 0;
  // The proxy's ini: the panel drawn on every frame, no game to look for, no tick, no input guard.
  if (FILE* f = std::fopen((g_dir + "RE4OGCabbyCodes.ini").c_str(), "w")) {
    std::fputs("AlwaysShow = 1\nDisable = game,tick,input\n", f);
    std::fclose(f);
  }
  std::printf("D3D9 the way bio4.exe uses it (its render thread), %s%s, expecting %s:\n",
              g_overlay ? "the Steam overlay's way of hooking" : "no overlay",
              g_shim ? ", Windows' compatibility shim" : "", g_expect_adopt ? "vtables of their own" : "the class hooks");
  const int rc = run();
  if (looped()) std::printf("  LOOP: the overlay's hook and the proxy's called each other (a stack overflow in the game)\n");
  std::printf("%s (%d failure(s))\n", g_failures || rc ? "FAILED" : "PASSED", g_failures);
  std::fflush(stdout);
  return g_failures || rc ? 1 : 0;  // ExitProcess: the proxy's DllMain sees a process exit
}
