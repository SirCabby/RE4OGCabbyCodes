#pragma once

#include <windows.h>

// The input guard. The game does not read the mouse or the keyboard from the
// window message queue: it opens DirectInput 8 devices (GUID_SysMouse, in
// exclusive mode, and GUID_SysKeyboard) and reads them on its main thread.
// Swallowing WM_LBUTTONDOWN in the window procedure therefore does nothing, and
// a mouse held exclusively gets no pointer at all - Windows hides it and sends
// the window no mouse messages. So, as RE0CabbyCodes does it:
//
//   DirectInput8Create (exe import) -> IDirectInput8::CreateDevice (slot 3)
//   -> IDirectInputDevice8::Acquire (7) / GetDeviceState (9) / GetDeviceData (10)
//
// While the panel is up the game's mouse is let go (Unacquire, on the game's own
// thread) and the game's attempts to take it back are refused, so the pointer
// moves, the panel gets its messages and the game sees no mouse at all; once the
// panel goes the game takes it back as it does after any focus change. While a
// panel field has keyboard focus the keyboard reads as "nothing pressed" (but
// Escape and Alt, which stay the game's). `Disable = input` in the ini turns the
// whole guard off.
namespace re4cc::input {

bool install(HMODULE exe);  // from DllMain (import-table patches only)
void uninstall();
void tick(bool panel_up);   // main thread: lets the mouse go when the panel comes up

}  // namespace re4cc::input
