#include "ipc_server.h"
#include "ipc.h"
#include "registry.h"
#include "util.h"

// payload: DWORD keyBytes, keyPath TCHARs  ->  overwritten with the reg stream
static void HandleEnum(IpcHeader* hdr, BYTE* payload)
{
	DWORD keyBytes = *(DWORD*)payload;
	LPTSTR keyPath = (LPTSTR)HeapAlloc(GetProcessHeap(), 0, keyBytes);
	HKEY hKey;

	if (!keyPath)
	{
		hdr->status = ERROR_OUTOFMEMORY;
		return;
	}

	// Copy the request path out before the payload gets overwritten by output.
	MemCopy(keyPath, payload + sizeof(DWORD), keyBytes);

	if (RegOpenKeyEx(HKEY_LOCAL_MACHINE, keyPath, 0, KEY_READ, &hKey) == ERROR_SUCCESS)
	{
		DWORD n = RegistryEnumStream(hKey, payload, IPC_PAYLOAD_MAX, TRUE);
		RegCloseKey(hKey);
		hdr->payloadLen = n;
		hdr->status = ERROR_SUCCESS;
	}
	else
	{
		hdr->status = (LONG)GetLastError();
	}

	HeapFree(GetProcessHeap(), 0, keyPath);
}

// payload: DWORD count, then `count` records of
//   DWORD keyBytes, keyPath (pad4), DWORD valBytes, valName (pad4),
//   DWORD type, DWORD dataLen, data (pad4)
static void HandleWrite(IpcHeader* hdr, BYTE* payload)
{
	BYTE* p = payload;
	DWORD count = *(DWORD*)p; p += sizeof(DWORD);
	LONG result = ERROR_SUCCESS;
	DWORD i;

	for (i = 0; i < count; i++)
	{
		DWORD keyBytes = *(DWORD*)p; p += sizeof(DWORD);
		LPCTSTR keyPath = (LPCTSTR)p; p += Align4(keyBytes);

		DWORD valBytes = *(DWORD*)p; p += sizeof(DWORD);
		LPCTSTR valName = (LPCTSTR)p; p += Align4(valBytes);

		DWORD type = *(DWORD*)p; p += sizeof(DWORD);
		DWORD dataLen = *(DWORD*)p; p += sizeof(DWORD);
		const BYTE* data = p; p += Align4(dataLen);

		// A lone null (valBytes == sizeof(TCHAR)) means the default value.
		LPCTSTR name = (valBytes > sizeof(TCHAR)) ? valName : NULL;

		LONG r = RegistryWriteValue(keyPath, name, type, data, dataLen);
		if (r != ERROR_SUCCESS && result == ERROR_SUCCESS) result = r;
	}

	hdr->payloadLen = 0;
	hdr->status = result;
}

void IpcServerWorker(HANDLE hStopEvent)
{
	HANDLE hMap = OpenFileMapping(FILE_MAP_ALL_ACCESS, FALSE, IPC_MAPPING_NAME);
	if (hMap)
	{
		IpcHeader* hdr = (IpcHeader*)MapViewOfFile(hMap, FILE_MAP_ALL_ACCESS, 0, 0, 0);
		if (hdr)
		{
			BYTE* payload = (BYTE*)(hdr + 1);

			switch (hdr->op)
			{
			case IPC_OP_ENUM:  HandleEnum(hdr, payload);  break;
			case IPC_OP_WRITE: HandleWrite(hdr, payload); break;
			default:           hdr->status = ERROR_INVALID_FUNCTION; break;
			}

			// Signal completion to the client.
			hdr->op = IPC_OP_NONE;

			UnmapViewOfFile(hdr);
		}
		CloseHandle(hMap);
	}

	// Stay alive until the client stops the service.
	WaitForSingleObject(hStopEvent, INFINITE);
}
