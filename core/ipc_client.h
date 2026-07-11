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

// Batched deleter, mirroring the writer. Begin, add each target, then Commit.
// AddValue removes a single value; AddTree removes a whole subkey and its
// descendants. Commit reports how many targets actually existed and were
// removed via *outDeleted (may be NULL); missing targets are not errors.
DWORD IpcDeleteBegin(IpcWriteBatch* b);
void  IpcDeleteAddValue(IpcWriteBatch* b, LPCTSTR fullKeyPath, LPCTSTR valueName);
void  IpcDeleteAddTree(IpcWriteBatch* b, LPCTSTR fullKeyPath);
DWORD IpcDeleteCommit(IpcWriteBatch* b, DWORD* outDeleted);

void IpcFree(void* p);

#endif // BTREGKEY_CORE_IPC_CLIENT_H
