// DisconnectOverlay.h: S_Disconnect Layered dialog (over DDraw).
#pragma once
#include <windows.h>

bool DisconnectOverlay_Start();

// Disconnect reason (S_Disconnect writeH); undefined -> "connection lost" text.
void DisconnectOverlay_Notify(unsigned short reason);

bool DisconnectOverlay_ShouldSwallowNative();
void DisconnectOverlay_ArmSwallow();

// Feed in the text the client's own native MessageBoxA/W resolved from
// string-c.tbl for the disconnect dialog we just swallowed, so the overlay
// shows the client's real (up to date) text instead of a hardcoded fallback.
// No-op if no dialog is currently shown/pending.
void DisconnectOverlay_SetLiveText(const wchar_t *text);

// Hides the overlay if currently shown. Call when a new login cycle starts
// (e.g. on DisconnectHook_ResetSession) since the overlay no longer has its
// own confirm button to dismiss itself -- clicks pass through to the native
// window underneath (WS_EX_TRANSPARENT).
void DisconnectOverlay_Hide();

// Closes the overlay window and unhooks WH_MOUSE_LL. Call as soon as the
// game's main window starts tearing down (WM_DESTROY), before the process
// itself begins exiting -- otherwise the OS suspends this hook's owning
// thread mid-teardown while the system-wide low-level mouse hook is still
// registered, which stalls mouse input everywhere until Windows times the
// unresponsive hook out (visible as a brief system-wide mouse-lag stutter
// on game close).
void DisconnectOverlay_Shutdown();
