// LineageEncryption.cpp: see LineageEncryption.h for the public API.
//
// History (why this file looks like this):
//   1. First approach computed the client's expected initial Blowfish-
//      derived key pair and scanned process memory for it -- found
//      candidates but could never reliably tell the C2S key apart from the
//      S2C key, so it never actually resolved.
//   2. Found a hardware write-watchpoint (see the removed PacketBreakpoint
//      .cpp) on the client's static packet send buffer (0x00BDCA70), then
//      read the live call chain that writes to it. The wrapper around
//      0x005807ED does this immediately before a call to 0x00580640:
//        mask = *(DWORD*)(bodyPtr)   ; read PLAINTEXT body, first 4 bytes
//        call 0x00580640(bodyPtr, bodyLen)   ; the encrypt routine itself
//        EKEY[0] (global @ 0x00BDCA5C) ^= mask
//        EKEY[1] (global @ 0x00BDCA60) += 679411651
//      679411651 is exactly com.lineage.echo.encryptions.Encryption
//      .encrypt()'s EKEY[1] increment on the Java server side, confirming
//      0x00580640 is that same _encrypt() routine and that EKEY0/EKEY1 live
//      at those two fixed addresses (no ASLR on this client).
//   3. Tried calling 0x00580640 directly (__cdecl function pointer, then
//      with hand-written save/restore-every-register asm, then with the
//      body pointer given generous extra headroom) to avoid reimplementing
//      the cipher. Every attempt corrupted something: unrelated caller
//      locals turned into impossible values (out of BYTE range, one case
//      even landing on a known code address from elsewhere on the call
//      stack), or the whole call chain simply stopped producing log output
//      partway through -- consistent with hitting some sort of anti-tamper
//      guard on that code region rather than an ordinary bug, since even a
//      case that touched NO registers and NO stack beyond a read -- just
//      dumping 600 raw bytes starting at 0x00580640 inside a SEH __try --
//      also silently stopped producing any further log output at all,
//      never even reaching the __except branch. A clean access violation
//      would have been caught and logged; this didn't even do that.
//   4. This version: never touch 0x00580640 again, in any way (no calling
//      it, no reading its bytes). EKEY0/EKEY1 are ordinary DATA, not code --
//      reading/writing them directly hasn't shown any of the above
//      symptoms. The XOR-chain cipher itself was ported from Java's
//      Encryption._encrypt() early in this project and cross-verified
//      correct against the server (real forged packets decrypted fine
//      server-side when this port was in use) -- it was never the actual
//      problem; only "how do we find/track the live key state" was. Now
//      that the key's fixed address is known for certain, there's no
//      remaining reason to borrow the client's own routine at all.
#include <windows.h>
#include "LineageEncryption.h"

namespace {

// Live-verified via disassembly of the wrapper that calls 0x00580640 (see
// file header): EKEY[0]/EKEY[1] are ordinary global DWORDs at these fixed
// addresses (no ASLR on this client, confirmed stable across many restarts
// this session).
volatile DWORD *const kEKey0 = reinterpret_cast<volatile DWORD *>(0x00BDCA5Cu);
volatile DWORD *const kEKey1 = reinterpret_cast<volatile DWORD *>(0x00BDCA60u);
constexpr DWORD kEKey1Increment = 679411651u; // matches Java's EKEY[1] += 679411651

CRITICAL_SECTION &GetLock() {
  struct Holder {
    CRITICAL_SECTION cs;
    Holder() { InitializeCriticalSection(&cs); }
  };
  static Holder holder; // C++11 magic statics: thread-safe one-time init
  return holder.cs;
}

// Port of Encryption._encrypt(char[] buf) from the Java server (see
// com/lineage/echo/encryptions/Encryption.java). ek is EKEY expanded to 8
// bytes, little-endian (EKEY[0]'s 4 bytes first, then EKEY[1]'s).
void EncryptChain(BYTE *body, int len, DWORD ekey0, DWORD ekey1) {
  BYTE ek[8];
  ek[0] = (BYTE)(ekey0 & 0xFF);
  ek[1] = (BYTE)((ekey0 >> 8) & 0xFF);
  ek[2] = (BYTE)((ekey0 >> 16) & 0xFF);
  ek[3] = (BYTE)((ekey0 >> 24) & 0xFF);
  ek[4] = (BYTE)(ekey1 & 0xFF);
  ek[5] = (BYTE)((ekey1 >> 8) & 0xFF);
  ek[6] = (BYTE)((ekey1 >> 16) & 0xFF);
  ek[7] = (BYTE)((ekey1 >> 24) & 0xFF);

  body[0] ^= ek[0];
  for (int i = 1; i < len; i++)
    body[i] ^= (body[i - 1] ^ ek[i & 7]);
  body[3] ^= ek[2];
  body[2] ^= body[3] ^ ek[3];
  body[1] ^= body[2] ^ ek[4];
  body[0] ^= body[1] ^ ek[5];
}

} // namespace

bool LineageEncryption_EncryptBody(BYTE *body, int len) {
  if (body == nullptr || len < 4)
    return false;

  // mask must be computed from the PRE-encryption plaintext, matching
  // Java's Encryption.encrypt(): mask = ULong32.fromArray(buf) runs before
  // _encrypt() touches buf.
  DWORD mask = (DWORD)body[0] | ((DWORD)body[1] << 8) | ((DWORD)body[2] << 16) |
               ((DWORD)body[3] << 24);

  EnterCriticalSection(&GetLock());
  DWORD ekey0 = *kEKey0;
  DWORD ekey1 = *kEKey1;
  EncryptChain(body, len, ekey0, ekey1);
  *kEKey0 = ekey0 ^ mask;
  *kEKey1 = ekey1 + kEKey1Increment;
  LeaveCriticalSection(&GetLock());
  return true;
}
