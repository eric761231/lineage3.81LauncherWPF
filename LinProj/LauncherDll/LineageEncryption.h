// LineageEncryption.h: encrypts a C2S packet body using the native game
// client's own encrypt function (see LineageEncryption.cpp for how its
// address was found and why calling it directly is safe on this client).
#pragma once
#ifndef LINEAGEENCRYPTION_H_INCLUDED
#define LINEAGEENCRYPTION_H_INCLUDED
#include <windows.h>

// Encrypts body in place (len must be >= 4) and advances the live key state
// in game memory, exactly as the native client does for its own packets, so
// subsequent real packets the game sends stay in sync. Always returns true
// once len >= 4 and body != nullptr -- unlike the earlier key-guessing
// approach this replaced, there is no "key not resolved yet" failure case.
bool LineageEncryption_EncryptBody(BYTE *body, int len);

#endif // LINEAGEENCRYPTION_H_INCLUDED
