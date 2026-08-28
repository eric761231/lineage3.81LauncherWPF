// MatchMakingHook.h: fixes MatchRegister_Window's Killer/Hunter/Talker
// registration so it actually fills the text the game sends to the server.
#pragma once

// Detours SetTypeLabel (VA 0x64F700) so selecting a category also writes the
// matching description string into Intro_Edit, not just the (display-only,
// hidden) Intro_Label. Call from a background thread after the game's code
// section is decrypted (see DelayedDetourThread in LauncherDll.cpp).
void InstallMatchMakingHook();
