#pragma once

// VMProtect SDK Stub Header
// Used to compile projects that have VMProtect markers without requiring the SDK.

#ifndef VMP_STUB_H
#define VMP_STUB_H

// Markers
#define VMProtectBegin(...)
#define VMProtectBeginUltra(...)
#define VMProtectBeginVirtualization(...)
#define VMProtectBeginMutation(...)
#define VMProtectBeginUltraLockByKey(...)
#define VMProtectBeginVirtualizationLockByKey(...)
#define VMProtectEnd()

// Since some code might use these as statements without parentheses:
// e.g. VMProtectBegin;
#undef VMProtectBegin
#define VMProtectBegin

#undef VMProtectEnd
#define VMProtectEnd

// Actually, a better way to support both VMProtectBegin and VMProtectBegin("marker")
// is difficult in standard C/C++ without parentheses.
// But usually, these are used as:
// VMProtectBegin; 
// or
// VMProtectBegin(m);
//
// If we define as below, it should cover most cases:
#define VMProtectIsProtected() (false)
#define VMProtectIsDebuggerPresent(x) (false)
#define VMProtectIsVirtualMachinePresent() (false)
#define VMProtectIsValidImageCRC() (true)
#define VMProtectDecryptStringA(s) (s)
#define VMProtectDecryptStringW(s) (s)
#define VMProtectFreeString(s)
#define VMProtectSetSerialNumber(s) (0)
#define VMProtectGetSerialNumberState() (0)
#define VMProtectGetSerialNumberData(d, s) (false)
#define VMProtectGetCurrentHWID(b, s) (0)

#endif // VMP_STUB_H
