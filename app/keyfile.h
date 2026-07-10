#ifndef BTREGKEY_APP_KEYFILE_H
#define BTREGKEY_APP_KEYFILE_H

#include <Windows.h>

// Export/import file format (UTF-16LE, one tab-separated record per line):
//   <keyPath> \t <valueName> \t <TYPE> \t <hexdata>
// keyPath is relative to BT_KEYS_SUBKEY. An empty value name is written "@";
// empty data is written "-". Lines starting with '#' are comments.

// --- writer ---
typedef struct
{
	BYTE*  buf;   // UTF-16 bytes accumulated so far
	SIZE_T len;
	SIZE_T cap;
	BOOL   ok;
} KeyFileWriter;

BOOL KeyFileBegin(KeyFileWriter* w);
void KeyFilePutValue(KeyFileWriter* w, LPCTSTR relKeyPath, LPCTSTR valueName,
                     DWORD type, const BYTE* data, DWORD dataLen);
BOOL KeyFileSave(KeyFileWriter* w, LPCTSTR path);   // writes file, frees buffer
void KeyFileAbort(KeyFileWriter* w);                // frees buffer, no file

// --- reader ---
// Called once per value record parsed from the file. valueName is NULL for the
// default value. Data points into a temporary buffer valid only for the call.
typedef void (*KeyFileValueFn)(void* ctx, LPCTSTR relKeyPath, LPCTSTR valueName,
                               DWORD type, const BYTE* data, DWORD dataLen);

// Returns a Win32 error code for file access; per-line parse errors are skipped.
DWORD KeyFileParse(LPCTSTR path, KeyFileValueFn fn, void* ctx);

#endif // BTREGKEY_APP_KEYFILE_H
