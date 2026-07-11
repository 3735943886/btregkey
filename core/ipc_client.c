#include "ipc_client.h"
#include "scm.h"
#include "util.h"

// --- shared mapping helpers --------------------------------------------------

static DWORD MapCreate(HANDLE* hOut, IpcHeader** hdrOut)
{
	HANDLE h = CreateFileMapping(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE,
	                             0, IPC_MAPPING_SIZE, IPC_MAPPING_NAME);
	IpcHeader* hdr;

	if (!h) return GetLastError();

	hdr = (IpcHeader*)MapViewOfFile(h, FILE_MAP_ALL_ACCESS, 0, 0, 0);
	if (!hdr)
	{
		DWORD err = GetLastError();
		CloseHandle(h);
		return err;
	}

	MemZero(hdr, sizeof(IpcHeader));
	*hOut = h;
	*hdrOut = hdr;
	return ERROR_SUCCESS;
}

static void MapDestroy(HANDLE h, IpcHeader* hdr)
{
	if (hdr) UnmapViewOfFile(hdr);
	if (h) CloseHandle(h);
}

// Arm the request, start the service, wait for it to clear `op`, then stop it.
static DWORD Invoke(IpcHeader* hdr, IpcOp op)
{
	UINT i;

	hdr->status = ERROR_SUCCESS;
	hdr->op = op; // must be armed before the service starts and reads it

	ServiceStart(IPC_SERVICE_NAME);

	for (i = 0; i < IPC_TIMEOUT_TICKS; i++)
	{
		if (hdr->op == IPC_OP_NONE)
		{
			ServiceStop(IPC_SERVICE_NAME);
			return (DWORD)hdr->status;
		}
		Sleep(IPC_TICK_MS);
	}

	ServiceStop(IPC_SERVICE_NAME);
	return WAIT_TIMEOUT;
}

// --- session -----------------------------------------------------------------

DWORD IpcSessionBegin(void)
{
	DWORD r = ServiceInstall(IPC_SERVICE_NAME);
	if (r == ERROR_SERVICE_EXISTS)
	{
		// Left behind by a crashed run: clean up and reinstall.
		ServiceStop(IPC_SERVICE_NAME);
		ServiceDelete(IPC_SERVICE_NAME);
		r = ServiceInstall(IPC_SERVICE_NAME);
	}
	return r;
}

void IpcSessionEnd(void)
{
	ServiceStop(IPC_SERVICE_NAME);
	ServiceDelete(IPC_SERVICE_NAME);
}

// --- enumerate ---------------------------------------------------------------

DWORD IpcEnum(LPCTSTR subKey, BYTE** outStream, DWORD* outLen)
{
	HANDLE h;
	IpcHeader* hdr;
	DWORD st = MapCreate(&h, &hdr);
	if (st != ERROR_SUCCESS) return st;

	{
		BYTE* pl = (BYTE*)(hdr + 1);
		DWORD keyBytes = (lstrlen(subKey) + 1) * sizeof(TCHAR);

		*(DWORD*)pl = keyBytes;
		MemCopy(pl + sizeof(DWORD), subKey, keyBytes);
		hdr->payloadLen = sizeof(DWORD) + keyBytes;

		st = Invoke(hdr, IPC_OP_ENUM);
		if (st == ERROR_SUCCESS)
		{
			DWORD n = hdr->payloadLen;
			BYTE* out = (BYTE*)HeapAlloc(GetProcessHeap(), 0, n ? n : 1);
			if (out)
			{
				MemCopy(out, pl, n);
				*outStream = out;
				*outLen = n;
			}
			else
			{
				st = ERROR_OUTOFMEMORY;
			}
		}
	}

	MapDestroy(h, hdr);
	return st;
}

// --- batched write -----------------------------------------------------------

DWORD IpcWriteBegin(IpcWriteBatch* b)
{
	MemZero(b, sizeof(*b));

	b->beginErr = MapCreate(&b->hMap, &b->hdr);
	if (b->beginErr != ERROR_SUCCESS)
	{
		b->ok = FALSE;
		return b->beginErr;
	}

	b->payload = (BYTE*)(b->hdr + 1);
	b->count = (DWORD*)b->payload;
	*b->count = 0;
	b->cursor = b->payload + sizeof(DWORD);
	b->ok = TRUE;
	return ERROR_SUCCESS;
}

static void PutBytes(IpcWriteBatch* b, const void* data, DWORD n)
{
	MemCopy(b->cursor, data, n);
	b->cursor += n;
}

static void PutDword(IpcWriteBatch* b, DWORD v)
{
	PutBytes(b, &v, sizeof(v));
}

static void PutPad4(IpcWriteBatch* b)
{
	while (((SIZE_T)(b->cursor - b->payload) % 4) != 0)
	{
		BYTE zero = 0;
		PutBytes(b, &zero, 1);
	}
}

void IpcWriteAddValue(IpcWriteBatch* b, LPCTSTR fullKeyPath, LPCTSTR valueName,
                      DWORD type, const BYTE* data, DWORD dataLen)
{
	DWORD keyBytes;
	DWORD valBytes;
	DWORD need;
	DWORD avail;
	static const TCHAR nul = 0;

	if (!b->ok) return;

	keyBytes = (lstrlen(fullKeyPath) + 1) * sizeof(TCHAR);
	valBytes = valueName ? ((lstrlen(valueName) + 1) * sizeof(TCHAR)) : sizeof(TCHAR);

	need = sizeof(DWORD) + Align4(keyBytes)
	     + sizeof(DWORD) + Align4(valBytes)
	     + sizeof(DWORD) + sizeof(DWORD) + Align4(dataLen);

	avail = (DWORD)((b->payload + IPC_PAYLOAD_MAX) - b->cursor);
	if (need > avail)
	{
		b->ok = FALSE; // batch too large to fit in one payload
		return;
	}

	PutDword(b, keyBytes);
	PutBytes(b, fullKeyPath, keyBytes);
	PutPad4(b);

	PutDword(b, valBytes);
	if (valueName) PutBytes(b, valueName, valBytes);
	else           PutBytes(b, &nul, sizeof(TCHAR));
	PutPad4(b);

	PutDword(b, type);
	PutDword(b, dataLen);
	if (dataLen) PutBytes(b, data, dataLen);
	PutPad4(b);

	(*b->count)++;
}

DWORD IpcWriteCommit(IpcWriteBatch* b)
{
	DWORD st;

	if (b->beginErr != ERROR_SUCCESS)
		return b->beginErr;

	if (!b->ok)
	{
		MapDestroy(b->hMap, b->hdr);
		return ERROR_INSUFFICIENT_BUFFER;
	}

	b->hdr->payloadLen = (DWORD)(b->cursor - b->payload);
	st = Invoke(b->hdr, IPC_OP_WRITE);

	MapDestroy(b->hMap, b->hdr);
	return st;
}

// --- batched delete ----------------------------------------------------------
// Same mapping/session plumbing as the writer, just a different record layout
// and op code. A delete record is:
//   DWORD kind, DWORD keyBytes, keyPath (pad4), DWORD valBytes, valName (pad4)

DWORD IpcDeleteBegin(IpcWriteBatch* b)
{
	return IpcWriteBegin(b); // identical map + counter setup
}

static void AddDeleteRecord(IpcWriteBatch* b, DWORD kind, LPCTSTR keyPath,
                            LPCTSTR valueName)
{
	DWORD keyBytes;
	DWORD valBytes;
	DWORD need;
	DWORD avail;
	static const TCHAR nul = 0;

	if (!b->ok) return;

	keyBytes = (lstrlen(keyPath) + 1) * sizeof(TCHAR);
	valBytes = valueName ? ((lstrlen(valueName) + 1) * sizeof(TCHAR)) : sizeof(TCHAR);

	need = sizeof(DWORD)                       // kind
	     + sizeof(DWORD) + Align4(keyBytes)
	     + sizeof(DWORD) + Align4(valBytes);

	avail = (DWORD)((b->payload + IPC_PAYLOAD_MAX) - b->cursor);
	if (need > avail)
	{
		b->ok = FALSE;
		return;
	}

	PutDword(b, kind);

	PutDword(b, keyBytes);
	PutBytes(b, keyPath, keyBytes);
	PutPad4(b);

	PutDword(b, valBytes);
	if (valueName) PutBytes(b, valueName, valBytes);
	else           PutBytes(b, &nul, sizeof(TCHAR));
	PutPad4(b);

	(*b->count)++;
}

void IpcDeleteAddValue(IpcWriteBatch* b, LPCTSTR fullKeyPath, LPCTSTR valueName)
{
	AddDeleteRecord(b, 0, fullKeyPath, valueName);
}

void IpcDeleteAddTree(IpcWriteBatch* b, LPCTSTR fullKeyPath)
{
	AddDeleteRecord(b, 1, fullKeyPath, NULL);
}

DWORD IpcDeleteCommit(IpcWriteBatch* b, DWORD* outDeleted)
{
	DWORD st;

	if (outDeleted) *outDeleted = 0;

	if (b->beginErr != ERROR_SUCCESS)
		return b->beginErr;

	if (!b->ok)
	{
		MapDestroy(b->hMap, b->hdr);
		return ERROR_INSUFFICIENT_BUFFER;
	}

	b->hdr->payloadLen = (DWORD)(b->cursor - b->payload);
	st = Invoke(b->hdr, IPC_OP_DELETE);
	if (st == ERROR_SUCCESS && outDeleted)
		*outDeleted = b->hdr->payloadLen; // service reports removed count here

	MapDestroy(b->hMap, b->hdr);
	return st;
}

void IpcFree(void* p)
{
	if (p) HeapFree(GetProcessHeap(), 0, p);
}
