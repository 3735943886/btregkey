#ifndef BTREGKEY_CORE_HEXUTIL_H
#define BTREGKEY_CORE_HEXUTIL_H

#include <Windows.h>

// Convert bytes to lowercase hex text. Writes len*2 chars plus a null.
// The caller must provide out with room for len*2 + 1 TCHARs.
void HexFromBytes(const BYTE* data, DWORD len, LPTSTR out);

// Parse hex text into bytes. Skips a leading "0x". Any non-hex character
// (dashes, spaces, colons) is ignored so grouped input like "aa-bb-cc" works.
// Returns the number of bytes written, capped at maxBytes.
DWORD BytesFromHex(LPCTSTR text, BYTE* out, DWORD maxBytes);

// Parse an unsigned hex string into a DWORD (used for REG_DWORD data).
DWORD DwordFromHex(LPCTSTR text);

#endif // BTREGKEY_CORE_HEXUTIL_H
