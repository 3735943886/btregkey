#include "keystore.h"
#include "btkeys.h"
#include "keyfile.h"
#include "../core/ipc_client.h"
#include "../core/registry.h"
#include "../core/console.h"
#include "../core/hexutil.h"
#include "../core/util.h"

// --- path builder: rebuilds relative key paths while walking the stream ------

typedef struct
{
	LPTSTR buf;
	DWORD  cap;   // TCHARs
	DWORD  len;   // TCHARs, excluding null
	DWORD* stack; // saved lengths for STEPOUT
	DWORD  scap;
	DWORD  depth;
	BOOL   ok;
} PathBuilder;

static BOOL PbInit(PathBuilder* pb)
{
	MemZero(pb, sizeof(*pb));
	pb->cap = 256;
	pb->scap = 32;
	pb->buf = (LPTSTR)HeapAlloc(GetProcessHeap(), 0, pb->cap * sizeof(TCHAR));
	pb->stack = (DWORD*)HeapAlloc(GetProcessHeap(), 0, pb->scap * sizeof(DWORD));
	pb->ok = (pb->buf && pb->stack);
	if (pb->buf) pb->buf[0] = 0;
	return pb->ok;
}

static void PbFree(PathBuilder* pb)
{
	if (pb->buf) HeapFree(GetProcessHeap(), 0, pb->buf);
	if (pb->stack) HeapFree(GetProcessHeap(), 0, pb->stack);
	pb->buf = NULL;
	pb->stack = NULL;
}

static void PbReserve(PathBuilder* pb, DWORD chars)
{
	if (!pb->ok) return;
	if (pb->len + chars + 1 <= pb->cap) return;

	{
		DWORD ncap = pb->cap;
		LPTSTR nbuf;
		while (ncap < pb->len + chars + 1) ncap *= 2;
		nbuf = (LPTSTR)HeapReAlloc(GetProcessHeap(), 0, pb->buf, ncap * sizeof(TCHAR));
		if (!nbuf) { pb->ok = FALSE; return; }
		pb->buf = nbuf;
		pb->cap = ncap;
	}
}

static void PbPush(PathBuilder* pb, LPCTSTR name)
{
	DWORD nameLen;
	if (!pb->ok) return;

	if (pb->depth >= pb->scap)
	{
		DWORD nscap = pb->scap * 2;
		DWORD* ns = (DWORD*)HeapReAlloc(GetProcessHeap(), 0, pb->stack, nscap * sizeof(DWORD));
		if (!ns) { pb->ok = FALSE; return; }
		pb->stack = ns;
		pb->scap = nscap;
	}
	pb->stack[pb->depth++] = pb->len;

	nameLen = lstrlen(name);
	PbReserve(pb, nameLen + 1);
	if (!pb->ok) return;

	if (pb->len > 0) pb->buf[pb->len++] = TEXT('\\');
	lstrcpy(pb->buf + pb->len, name);
	pb->len += nameLen;
}

static void PbPop(PathBuilder* pb)
{
	if (pb->depth > 0)
	{
		pb->len = pb->stack[--pb->depth];
		if (pb->buf) pb->buf[pb->len] = 0;
	}
}

// --- stream walk -------------------------------------------------------------

static void WalkStream(const BYTE* s, DWORD len, KeyFileValueFn visit, void* ctx)
{
	PathBuilder pb;
	const BYTE* p = s;
	const BYTE* end = s + len;
	LPCTSTR pending = TEXT("");

	if (!PbInit(&pb)) { PbFree(&pb); return; }

	while (p + sizeof(DWORD) <= end)
	{
		DWORD tag = *(const DWORD*)p; p += sizeof(DWORD);

		if (tag == REGSTREAM_KEY)
		{
			DWORD nb = *(const DWORD*)p; p += sizeof(DWORD);
			pending = (LPCTSTR)p; p += nb;
		}
		else if (tag == REGSTREAM_VALUE)
		{
			DWORD nb = *(const DWORD*)p; p += sizeof(DWORD);
			LPCTSTR name = (LPCTSTR)p; p += nb;
			DWORD type = *(const DWORD*)p; p += sizeof(DWORD);
			DWORD dlen = *(const DWORD*)p; p += sizeof(DWORD);
			const BYTE* data = p; p += dlen;
			while (((SIZE_T)(p - s) % sizeof(TCHAR)) != 0) p++;

			if (pb.ok) visit(ctx, pb.buf, name, type, data, dlen);
		}
		else if (tag == REGSTREAM_STEPIN)
		{
			PbPush(&pb, pending);
		}
		else if (tag == REGSTREAM_STEPOUT)
		{
			PbPop(&pb);
		}
		else break;
	}

	PbFree(&pb);
}

// --- visitors ----------------------------------------------------------------

static void PrintVisit(void* ctx, LPCTSTR key, LPCTSTR name, DWORD type,
                       const BYTE* data, DWORD dlen)
{
	TCHAR typeText[16];
	(void)ctx;

	RegTypeToText(type, typeText);
	ConsolePrintf(TEXT("%s\t%s\t%s\t"),
	              (key && key[0]) ? key : TEXT("(root)"),
	              (name && name[0]) ? name : TEXT("@"),
	              typeText);

	if (dlen)
	{
		LPTSTR hex = (LPTSTR)HeapAlloc(GetProcessHeap(), 0, ((SIZE_T)dlen * 2 + 1) * sizeof(TCHAR));
		if (hex)
		{
			HexFromBytes(data, dlen, hex);
			ConsolePuts(hex);
			HeapFree(GetProcessHeap(), 0, hex);
		}
	}
	else
	{
		ConsolePuts(TEXT("-"));
	}
	ConsolePuts(TEXT("\n"));
}

static void ExportVisit(void* ctx, LPCTSTR key, LPCTSTR name, DWORD type,
                        const BYTE* data, DWORD dlen)
{
	KeyFilePutValue((KeyFileWriter*)ctx, key, name, type, data, dlen);
}

typedef struct
{
	IpcWriteBatch* batch;
	DWORD          count;
} ImportCtx;

static void ImportVisit(void* ctx, LPCTSTR key, LPCTSTR name, DWORD type,
                        const BYTE* data, DWORD dlen)
{
	ImportCtx* ic = (ImportCtx*)ctx;
	// Build the full HKLM-relative path: BT_KEYS_SUBKEY[\<relKeyPath>]
	TCHAR full[1024];

	lstrcpy(full, BT_KEYS_SUBKEY);
	if (key && key[0])
	{
		lstrcat(full, TEXT("\\"));
		lstrcat(full, key);
	}

	IpcWriteAddValue(ic->batch, full, name, type, data, dlen);
	ic->count++;
}

// --- operations --------------------------------------------------------------

LONG KeystoreList(void)
{
	BYTE* stream = NULL;
	DWORD len = 0;
	DWORD st = IpcEnum(BT_KEYS_SUBKEY, &stream, &len);

	if (st != ERROR_SUCCESS) return (LONG)st;

	ConsolePuts(TEXT("keyPath\tvalue\ttype\tdata\n"));
	ConsolePuts(TEXT("-------\t-----\t----\t----\n"));
	WalkStream(stream, len, PrintVisit, NULL);

	IpcFree(stream);
	return ERROR_SUCCESS;
}

LONG KeystoreExport(LPCTSTR file)
{
	BYTE* stream = NULL;
	DWORD len = 0;
	KeyFileWriter w;
	DWORD st = IpcEnum(BT_KEYS_SUBKEY, &stream, &len);

	if (st != ERROR_SUCCESS) return (LONG)st;

	if (!KeyFileBegin(&w)) { IpcFree(stream); return ERROR_OUTOFMEMORY; }
	WalkStream(stream, len, ExportVisit, &w);
	IpcFree(stream);

	return KeyFileSave(&w, file) ? ERROR_SUCCESS : (LONG)GetLastError();
}

LONG KeystoreImport(LPCTSTR file)
{
	IpcWriteBatch batch;
	ImportCtx ic;
	DWORD st;

	st = IpcWriteBegin(&batch);
	if (st != ERROR_SUCCESS) return (LONG)st;

	ic.batch = &batch;
	ic.count = 0;

	st = KeyFileParse(file, ImportVisit, &ic);
	if (st != ERROR_SUCCESS)
	{
		// Release the mapping without starting the service.
		batch.ok = FALSE;
		IpcWriteCommit(&batch);
		return (LONG)st;
	}

	st = IpcWriteCommit(&batch);
	if (st == ERROR_SUCCESS)
		ConsolePrintf(TEXT("Imported %u value(s).\n"), ic.count);
	return (LONG)st;
}

LONG KeystoreSetClassic(LPCTSTR adapter, LPCTSTR device, LPCTSTR hexKey)
{
	IpcWriteBatch batch;
	BYTE key[16];
	DWORD n;
	TCHAR full[1024];
	DWORD st;

	n = BytesFromHex(hexKey, key, sizeof(key));

	lstrcpy(full, BT_KEYS_SUBKEY);
	lstrcat(full, TEXT("\\"));
	lstrcat(full, adapter);

	st = IpcWriteBegin(&batch);
	if (st != ERROR_SUCCESS) return (LONG)st;

	IpcWriteAddValue(&batch, full, device, REG_BINARY, key, n);
	return (LONG)IpcWriteCommit(&batch);
}
