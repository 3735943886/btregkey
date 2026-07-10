#ifndef BTREGKEY_CORE_REGISTRY_H
#define BTREGKEY_CORE_REGISTRY_H

#include <Windows.h>

// Serialized registry stream (produced by RegistryEnumStream, consumed by the
// client when flattening the tree). Every entry starts with a DWORD tag:
//
//   REGSTREAM_KEY:     tag, DWORD nameBytes, name (TCHARs, TCHAR-aligned)
//   REGSTREAM_VALUE:   tag, DWORD nameBytes, name, DWORD type,
//                      DWORD dataBytes, data (padded to TCHAR alignment)
//   REGSTREAM_STEPIN:  tag                      (descend into the last key)
//   REGSTREAM_STEPOUT: tag                      (ascend to the parent)
#define REGSTREAM_KEY      1
#define REGSTREAM_VALUE    2
#define REGSTREAM_STEPIN   3
#define REGSTREAM_STEPOUT  4

// Enumerate hKey into buf as the stream above. Returns the number of bytes
// written. Recursion stops early (never overflows) if buf fills up.
DWORD RegistryEnumStream(HKEY hKey, BYTE* buf, DWORD bufSize, BOOL recursive);

// Create/set a single value under HKLM\subKey (subKey path is created if
// needed). valueName may be NULL for the key's default value. Returns a Win32
// error code.
LONG RegistryWriteValue(LPCTSTR subKey, LPCTSTR valueName, DWORD type,
                        const BYTE* data, DWORD dataBytes);

#endif // BTREGKEY_CORE_REGISTRY_H
