#ifndef BTREGKEY_CORE_UTIL_H
#define BTREGKEY_CORE_UTIL_H

#include <Windows.h>

// This project is built without the CRT (IgnoreAllDefaultLibraries), so the
// usual memcpy/memset intrinsics are not available as library symbols. These
// small inline helpers replace them using only what the compiler can inline.

static __inline void MemCopy(void* dst, const void* src, SIZE_T len)
{
	BYTE* d = (BYTE*)dst;
	const BYTE* s = (const BYTE*)src;
	while (len--) *d++ = *s++;
}

static __inline void MemZero(void* dst, SIZE_T len)
{
	// RtlSecureZeroMemory is a forceinline helper in winnt.h, safe without CRT.
	RtlSecureZeroMemory(dst, len);
}

// Round a byte count up so the next TCHAR write stays aligned.
static __inline SIZE_T AlignTChar(SIZE_T bytes)
{
	while (bytes % sizeof(TCHAR)) bytes++;
	return bytes;
}

// Round a byte count up to a 4-byte boundary (keeps DWORD fields aligned in
// the IPC record stream).
static __inline DWORD Align4(DWORD bytes)
{
	return (bytes + 3u) & ~3u;
}

#endif // BTREGKEY_CORE_UTIL_H
