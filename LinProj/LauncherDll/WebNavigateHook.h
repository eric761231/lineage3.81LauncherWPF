// WebNavigateHook.h: redirects the game's built-in web browser (customer
// service page) to a custom URL instead of the original commercial site.
#pragma once

// Detours CWebWindow::Navigate (VA 0x610D70) so every navigation request is
// redirected to a fixed local URL, regardless of what the game itself asked
// to open. Call from a background thread after the game's code section is
// decrypted (see DelayedDetourThread in LauncherDll.cpp).
void InstallWebNavigateHook();
