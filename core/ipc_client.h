#ifndef BTREGKEY_CORE_IPC_CLIENT_H
#define BTREGKEY_CORE_IPC_CLIENT_H

#include <Windows.h>
#include "ipc.h"

// Install/remove the helper service around a batch of operations. Begin
// auto-recovers a service left behind by a previously crashed run.
DWORD IpcSessionBegin(void);
void  IpcSessionEnd(void);

// Enumerate HKLM\subKey recursively. On success, *outStream points to a
// heap block (free with IpcFree) holding the registry stream, and *outLen is
// its length. Returns a Win32 error code.
DWORD IpcEnum(LPCTSTR subKey, BYTE** outStream, DWORD* outLen);

// Batched value writer. Begin, add each value, then Commit (which sends the
// whole batch in one service round-trip and releases resources).
typedef struct
{
	HANDLE     hMap;
	IpcHeader* hdr;
	BYTE*      payload;
	BYTE*      cursor;
	DWORD*     count;
	BOOL       ok;      // FALSE once the payload overflows
	DWORD      beginErr;
} IpcWriteBatch;

DWORD IpcWriteBegin(IpcWriteBatch* b);
void  IpcWriteAddValue(IpcWriteBatch* b, LPCTSTR fullKeyPath, LPCTSTR valueName,
                       DWORD type, const BYTE* data, DWORD dataLen);
DWORD IpcWriteCommit(IpcWriteBatch* b);

void IpcFree(void* p);

#endif // BTREGKEY_CORE_IPC_CLIENT_H
