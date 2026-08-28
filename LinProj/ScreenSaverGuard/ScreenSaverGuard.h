// ScreenSaverGuard.h: entry point called from dllmain.cpp's worker thread.
#pragma once
#include <windows.h>

// Waits for the game window, then (if a screen saver is actually configured
// on this machine) suppresses it while the window is visible/not minimized,
// and allows it again once the window is minimized (or restores the
// original setting once the window closes). Blocks until the game window
// closes; run this on its own thread.
void RunScreenSaverGuard();
