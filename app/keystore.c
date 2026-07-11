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

// Column widths for the aligned `list` output. wsprintf offers no reliable
// field width for %s, so we measure the widest cell per column in a first pass
// (MeasureVisit) and pad by hand in the second (PrintVisit).
typedef struct
{
	int wKey;
	int wVal;
	int wType;
} ColWidths;

static LPCTSTR KeyText(LPCTSTR key)   { return (key && key[0]) ? key : TEXT("(root)"); }
static LPCTSTR NameText(LPCTSTR name) { return (name && name[0]) ? name : TEXT("@"); }

// Emit `n` copies of `ch` (n <= 0 prints nothing) in 32-char chunks.
static void Fill(TCHAR ch, int n)
{
	TCHAR buf[33];
	while (n > 0)
	{
		int k = (n > 32) ? 32 : n;
		int i;
		for (i = 0; i < k; i++) buf[i] = ch;
		buf[k] = 0;
		ConsolePuts(buf);
		n -= k;
	}
}

// Print `s` left-justified in `width`, followed by a two-space column gap.
static void PrintCol(LPCTSTR s, int width)
{
	ConsolePuts(s);
	Fill(TEXT(' '), width - lstrlen(s) + 2);
}

static void MeasureVisit(void* ctx, LPCTSTR key, LPCTSTR name, DWORD type,
                         const BYTE* data, DWORD dlen)
{
	ColWidths* c = (ColWidths*)ctx;
	TCHAR typeText[16];
	int n;
	(void)data; (void)dlen;

	n = lstrlen(KeyText(key));   if (n > c->wKey)  c->wKey = n;
	n = lstrlen(NameText(name)); if (n > c->wVal)  c->wVal = n;
	RegTypeToText(type, typeText);
	n = lstrlen(typeText);       if (n > c->wType) c->wType = n;
}

static void PrintVisit(void* ctx, LPCTSTR key, LPCTSTR name, DWORD type,
                       const BYTE* data, DWORD dlen)
{
	ColWidths* c = (ColWidths*)ctx;
	TCHAR typeText[16];

	RegTypeToText(type, typeText);
	PrintCol(KeyText(key),   c->wKey);
	PrintCol(NameText(name), c->wVal);
	PrintCol(typeText,       c->wType);

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

	{
		ColWidths cols;
		// Start at the header-label widths so the titles line up too.
		cols.wKey  = lstrlen(TEXT("keyPath"));
		cols.wVal  = lstrlen(TEXT("value"));
		cols.wType = lstrlen(TEXT("type"));
		WalkStream(stream, len, MeasureVisit, &cols);

		// Header row + underline, using the measured widths.
		PrintCol(TEXT("keyPath"), cols.wKey);
		PrintCol(TEXT("value"),   cols.wVal);
		PrintCol(TEXT("type"),    cols.wType);
		ConsolePuts(TEXT("data\n"));

		Fill(TEXT('-'), cols.wKey);  Fill(TEXT(' '), 2);
		Fill(TEXT('-'), cols.wVal);  Fill(TEXT(' '), 2);
		Fill(TEXT('-'), cols.wType); Fill(TEXT(' '), 2);
		Fill(TEXT('-'), 4);          ConsolePuts(TEXT("\n"));

		WalkStream(stream, len, PrintVisit, &cols);
	}

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

LONG KeystoreDelete(LPCTSTR adapter, LPCTSTR device)
{
	IpcWriteBatch batch;
	TCHAR adapterPath[512];
	DWORD deleted = 0;
	DWORD st;

	// BT_KEYS_SUBKEY\<adapter>
	lstrcpy(adapterPath, BT_KEYS_SUBKEY);
	lstrcat(adapterPath, TEXT("\\"));
	lstrcat(adapterPath, adapter);

	st = IpcDeleteBegin(&batch);
	if (st != ERROR_SUCCESS) return (LONG)st;

	if (device && device[0])
	{
		// A classic device lives as a *value* named <device> under the adapter
		// key; a BLE device lives as a *subkey* <adapter>\<device>. We don't
		// know which, so target both forms -- the missing one is a no-op.
		TCHAR devicePath[512];

		IpcDeleteAddValue(&batch, adapterPath, device);

		lstrcpy(devicePath, adapterPath);
		lstrcat(devicePath, TEXT("\\"));
		lstrcat(devicePath, device);
		IpcDeleteAddTree(&batch, devicePath);
	}
	else
	{
		// Whole adapter subtree (e.g. keys left behind by an old dongle).
		IpcDeleteAddTree(&batch, adapterPath);
	}

	st = IpcDeleteCommit(&batch, &deleted);
	if (st != ERROR_SUCCESS)
		return (LONG)st;

	if (deleted == 0)
	{
		ConsolePuts(TEXT("Nothing matched; no keys were deleted.\n"));
		return ERROR_FILE_NOT_FOUND;
	}

	ConsolePrintf(TEXT("Deleted %u item(s).\n"), deleted);
	return ERROR_SUCCESS;
}
