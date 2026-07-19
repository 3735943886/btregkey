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

// --- LE folder / address helpers ---------------------------------------------
// A BLE bond is a subkey "<adapter>\<device>"; the device folder name is a MAC.
// The folder's `Address` value stores the device's real identity address, which
// on some Windows versions differs from the folder name by the final byte.

#define FOLDER_MAX 64  // TCHARs, incl. null, for a "<adapter>\<device>" path

// Pointer to the last '\'-separated component ("<device>") of a key path.
static LPCTSTR LastComponent(LPCTSTR keyPath)
{
	LPCTSTR p, last = keyPath;
	for (p = keyPath; *p; p++)
		if (*p == TEXT('\\')) last = p + 1;
	return last;
}

// TRUE if keyPath is a device folder ("<adapter>\<device>", i.e. has a '\').
static BOOL IsDeviceFolder(LPCTSTR keyPath)
{
	LPCTSTR p;
	for (p = keyPath; *p; p++)
		if (*p == TEXT('\\')) return TRUE;
	return FALSE;
}

// TRUE if two device paths share the same "<adapter>\" prefix (case-insensitive).
// A bond is per-adapter, so identity matching must never cross adapters: the
// same physical device on a different dongle is a different bond.
static BOOL SameAdapter(LPCTSTR a, LPCTSTR b)
{
	int la = (int)(LastComponent(a) - a); // length of "<adapter>\" prefix
	int lb = (int)(LastComponent(b) - b);
	int i;
	if (la != lb) return FALSE;
	for (i = 0; i < la; i++)
		if (((a[i] | 0x20) & 0xff) != ((b[i] | 0x20) & 0xff)) return FALSE;
	return TRUE;
}

// Decode a BTHPORT `Address` REG_QWORD (8 bytes, little-endian) into a 12-hex
// MAC string (out needs >= 13 TCHARs). Returns FALSE unless dlen == 8.
static BOOL AddressToHex(const BYTE* data, DWORD dlen, LPTSTR out)
{
	BYTE mac[6];
	int i;
	if (dlen != 8) return FALSE;
	for (i = 0; i < 6; i++) mac[i] = data[5 - i];
	HexFromBytes(mac, 6, out);
	return TRUE;
}

// TRUE if two equal-length hex folder names differ only in the final byte
// (their last two chars). Purely cosmetic -- used to annotate `list`, never to
// decide identity (that is done by the Address value).
static BOOL DiffersInFinalByteOnly(LPCTSTR a, LPCTSTR b)
{
	int la = lstrlen(a), lb = lstrlen(b), i;
	if (la != lb || la < 2) return FALSE;
	for (i = 0; i < la - 2; i++)
		if (((a[i] | 0x20) & 0xff) != ((b[i] | 0x20) & 0xff)) return FALSE;
	return lstrcmpi(a + la - 2, b + la - 2) != 0;
}

// --- growable string list (device folder names) ------------------------------

typedef struct
{
	TCHAR* buf;   // cap * FOLDER_MAX TCHARs
	DWORD  count;
	DWORD  cap;
} StrList;

static BOOL SlInit(StrList* sl)
{
	sl->cap = 32;
	sl->count = 0;
	sl->buf = (TCHAR*)HeapAlloc(GetProcessHeap(), 0, (SIZE_T)sl->cap * FOLDER_MAX * sizeof(TCHAR));
	return sl->buf != NULL;
}

static void SlFree(StrList* sl)
{
	if (sl->buf) HeapFree(GetProcessHeap(), 0, sl->buf);
	sl->buf = NULL;
}

static LPCTSTR SlGet(StrList* sl, DWORD i)
{
	return sl->buf + (SIZE_T)i * FOLDER_MAX;
}

static BOOL SlHas(StrList* sl, LPCTSTR s)
{
	DWORD i;
	for (i = 0; i < sl->count; i++)
		if (lstrcmpi(SlGet(sl, i), s) == 0) return TRUE;
	return FALSE;
}

static void SlAdd(StrList* sl, LPCTSTR s)
{
	if (!sl->buf || lstrlen(s) >= FOLDER_MAX) return;
	if (SlHas(sl, s)) return; // keep distinct
	if (sl->count >= sl->cap)
	{
		DWORD ncap = sl->cap * 2;
		TCHAR* nb = (TCHAR*)HeapReAlloc(GetProcessHeap(), 0, sl->buf,
		                                (SIZE_T)ncap * FOLDER_MAX * sizeof(TCHAR));
		if (!nb) return;
		sl->buf = nb;
		sl->cap = ncap;
	}
	lstrcpy(sl->buf + (SIZE_T)sl->count * FOLDER_MAX, s);
	sl->count++;
}

// --- folder <-> identity-address map -----------------------------------------
// The device's real identity is its `Address` value, not the folder name. This
// maps each device folder to the identity it stores, so import can recognise
// "same device under a different folder name" without guessing from the name.

typedef struct { TCHAR folder[FOLDER_MAX]; TCHAR id[16]; } FaEntry;

typedef struct
{
	FaEntry* items;
	DWORD    count;
	DWORD    cap;
} FaList;

static BOOL FaInit(FaList* fa)
{
	fa->cap = 16;
	fa->count = 0;
	fa->items = (FaEntry*)HeapAlloc(GetProcessHeap(), 0, (SIZE_T)fa->cap * sizeof(FaEntry));
	return fa->items != NULL;
}

static void FaFree(FaList* fa)
{
	if (fa->items) HeapFree(GetProcessHeap(), 0, fa->items);
	fa->items = NULL;
}

static void FaAdd(FaList* fa, LPCTSTR folder, LPCTSTR id)
{
	if (!fa->items || lstrlen(folder) >= FOLDER_MAX) return;
	if (fa->count >= fa->cap)
	{
		DWORD ncap = fa->cap * 2;
		FaEntry* ni = (FaEntry*)HeapReAlloc(GetProcessHeap(), 0, fa->items, (SIZE_T)ncap * sizeof(FaEntry));
		if (!ni) return;
		fa->items = ni;
		fa->cap = ncap;
	}
	lstrcpy(fa->items[fa->count].folder, folder);
	lstrcpy(fa->items[fa->count].id, id);
	fa->count++;
}

// Identity stored by `folder`, or NULL if unknown.
static LPCTSTR FaIdOf(FaList* fa, LPCTSTR folder)
{
	DWORD i;
	for (i = 0; i < fa->count; i++)
		if (lstrcmpi(fa->items[i].folder, folder) == 0) return fa->items[i].id;
	return NULL;
}

// A folder under the SAME adapter as `folder`, holding identity `id`, whose name
// differs from `folder` -- i.e. the same device paired here under another name.
// Returns NULL if none. Never crosses adapters (a different dongle is a
// different bond even for the same physical device).
static LPCTSTR FaFolderForId(FaList* fa, LPCTSTR id, LPCTSTR folder)
{
	DWORD i;
	for (i = 0; i < fa->count; i++)
		if (lstrcmpi(fa->items[i].id, id) == 0 &&
		    SameAdapter(fa->items[i].folder, folder) &&
		    lstrcmpi(fa->items[i].folder, folder) != 0)
			return fa->items[i].folder;
	return NULL;
}

// --- folder remap table (from-path -> to-path) -------------------------------

typedef struct { TCHAR from[FOLDER_MAX]; TCHAR to[FOLDER_MAX]; } Remap;

typedef struct
{
	Remap* items;
	DWORD  count;
	DWORD  cap;
} RemapTable;

static BOOL RtInit(RemapTable* rt)
{
	rt->cap = 16;
	rt->count = 0;
	rt->items = (Remap*)HeapAlloc(GetProcessHeap(), 0, (SIZE_T)rt->cap * sizeof(Remap));
	return rt->items != NULL;
}

static void RtFree(RemapTable* rt)
{
	if (rt->items) HeapFree(GetProcessHeap(), 0, rt->items);
	rt->items = NULL;
}

static void RtAdd(RemapTable* rt, LPCTSTR from, LPCTSTR to)
{
	if (!rt->items || lstrlen(from) >= FOLDER_MAX || lstrlen(to) >= FOLDER_MAX) return;
	if (rt->count >= rt->cap)
	{
		DWORD ncap = rt->cap * 2;
		Remap* ni = (Remap*)HeapReAlloc(GetProcessHeap(), 0, rt->items, (SIZE_T)ncap * sizeof(Remap));
		if (!ni) return;
		rt->items = ni;
		rt->cap = ncap;
	}
	lstrcpy(rt->items[rt->count].from, from);
	lstrcpy(rt->items[rt->count].to, to);
	rt->count++;
}

static LPCTSTR RtLookup(RemapTable* rt, LPCTSTR from)
{
	DWORD i;
	if (!rt->items) return NULL;
	for (i = 0; i < rt->count; i++)
		if (lstrcmpi(rt->items[i].from, from) == 0) return rt->items[i].to;
	return NULL;
}

// Scans a reg stream or a key file, recording each device folder (into
// `folders`) and its identity address (into `fa`); either may be NULL.
typedef struct { StrList* folders; FaList* fa; } ScanCtx;

static void ScanVisit(void* ctx, LPCTSTR key, LPCTSTR name, DWORD type,
                      const BYTE* data, DWORD dlen)
{
	ScanCtx* sc = (ScanCtx*)ctx;
	(void)type;
	if (!IsDeviceFolder(key)) return;
	if (sc->folders) SlAdd(sc->folders, key);
	if (sc->fa && lstrcmpi(name, TEXT("Address")) == 0)
	{
		TCHAR id[13];
		if (AddressToHex(data, dlen, id)) FaAdd(sc->fa, key, id);
	}
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

// Notes any device folder whose name differs from its real (Address) identity.
typedef struct { BOOL headerShown; } NoteCtx;

static void NoteVisit(void* ctx, LPCTSTR key, LPCTSTR name, DWORD type,
                      const BYTE* data, DWORD dlen)
{
	NoteCtx* nc = (NoteCtx*)ctx;
	TCHAR idHex[13];
	(void)type;

	if (!IsDeviceFolder(key)) return;
	if (lstrcmpi(name, TEXT("Address")) != 0) return;
	if (!AddressToHex(data, dlen, idHex)) return;
	if (lstrcmpi(LastComponent(key), idHex) == 0) return; // folder == identity

	if (!nc->headerShown)
	{
		ConsolePuts(TEXT("\nnotes:\n"));
		nc->headerShown = TRUE;
	}
	ConsolePrintf(
		DiffersInFinalByteOnly(LastComponent(key), idHex)
			? TEXT("  %s : real address is %s (folder differs in final byte)\n")
			: TEXT("  %s : real address is %s (folder differs)\n"),
		key, idHex);
}

typedef struct
{
	KeyFileWriter* w;
	RemapTable*    remap;  // NULL, or folder -> normalized folder
} ExportCtx;

static void ExportVisit(void* ctx, LPCTSTR key, LPCTSTR name, DWORD type,
                        const BYTE* data, DWORD dlen)
{
	ExportCtx* ec = (ExportCtx*)ctx;
	LPCTSTR outKey = key;

	if (ec->remap && key && key[0])
	{
		LPCTSTR mapped = RtLookup(ec->remap, key);
		if (mapped) outKey = mapped;
	}
	KeyFilePutValue(ec->w, outKey, name, type, data, dlen);
}

typedef struct
{
	IpcWriteBatch* batch;
	DWORD          count;
	RemapTable*    remap;  // NULL, or file folder -> target folder
	StrList*       skip;   // NULL, or file folders to leave out entirely
} ImportCtx;

static void ImportVisit(void* ctx, LPCTSTR key, LPCTSTR name, DWORD type,
                        const BYTE* data, DWORD dlen)
{
	ImportCtx* ic = (ImportCtx*)ctx;
	// Build the full HKLM-relative path: BT_KEYS_SUBKEY[\<relKeyPath>]
	TCHAR full[1024];
	LPCTSTR useKey = key;

	if (ic->skip && key && key[0] && SlHas(ic->skip, key)) return;
	if (ic->remap && key && key[0])
	{
		LPCTSTR mapped = RtLookup(ic->remap, key);
		if (mapped) useKey = mapped;
	}

	lstrcpy(full, BT_KEYS_SUBKEY);
	if (useKey && useKey[0])
	{
		lstrcat(full, TEXT("\\"));
		lstrcat(full, useKey);
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

		// Informational: flag folders whose name != their real identity address.
		{
			NoteCtx nc;
			nc.headerShown = FALSE;
			WalkStream(stream, len, NoteVisit, &nc);
		}
	}

	IpcFree(stream);
	return ERROR_SUCCESS;
}

// normalize: -1 = ask if any mismatch, 0 = never, 1 = always.
LONG KeystoreExport(LPCTSTR file, int normalize)
{
	BYTE* stream = NULL;
	DWORD len = 0;
	KeyFileWriter w;
	FaList fa;
	RemapTable remap;
	ExportCtx ec;
	DWORD applied = 0;
	BOOL doNormalize;
	DWORD st = IpcEnum(BT_KEYS_SUBKEY, &stream, &len);

	if (st != ERROR_SUCCESS) return (LONG)st;

	// Find device folders whose name differs from their real identity address.
	if (!FaInit(&fa) || !RtInit(&remap)) { FaFree(&fa); RtFree(&remap); IpcFree(stream); return ERROR_OUTOFMEMORY; }
	{
		ScanCtx sc; sc.folders = NULL; sc.fa = &fa;
		WalkStream(stream, len, ScanVisit, &sc);
	}
	{
		DWORD i;
		for (i = 0; i < fa.count; i++)
		{
			LPCTSTR folder = fa.items[i].folder;      // "<adapter>\<device>"
			LPCTSTR id     = fa.items[i].id;          // real identity (12 hex)
			if (lstrcmpi(LastComponent(folder), id) == 0) continue; // already canonical
			{
				// Corrected path = "<adapter>\<id>".
				TCHAR corrected[FOLDER_MAX];
				int adapterChars = (int)(LastComponent(folder) - folder); // includes '\'
				if (adapterChars + (int)lstrlen(id) + 1 > FOLDER_MAX) continue;
				MemCopy(corrected, folder, (SIZE_T)adapterChars * sizeof(TCHAR));
				corrected[adapterChars] = 0;
				lstrcat(corrected, id);
				RtAdd(&remap, folder, corrected);
			}
		}
	}

	doNormalize = FALSE;
	if (remap.count > 0)
	{
		if (normalize == 1) doNormalize = TRUE;
		else if (normalize == 0) doNormalize = FALSE;
		else
		{
			TCHAR c;
			ConsolePrintf(TEXT("%u device folder(s) differ from their real address.\n"), remap.count);
			ConsolePuts(TEXT("Rename folders to the real address in the export? [y/N]: "));
			c = ConsoleReadChar();
			ConsolePuts(TEXT("\n"));
			doNormalize = (c == TEXT('y') || c == TEXT('Y'));
		}
	}
	if (doNormalize) applied = remap.count;

	if (!KeyFileBegin(&w)) { FaFree(&fa); RtFree(&remap); IpcFree(stream); return ERROR_OUTOFMEMORY; }
	ec.w = &w;
	ec.remap = doNormalize ? &remap : NULL;
	WalkStream(stream, len, ExportVisit, &ec);
	IpcFree(stream);
	FaFree(&fa);
	RtFree(&remap);

	if (!KeyFileSave(&w, file)) return (LONG)GetLastError();
	if (applied) ConsolePrintf(TEXT("Normalized %u folder name(s).\n"), applied);
	return ERROR_SUCCESS;
}

// onto: -1 = ask per near-match, 0 = keep file names, 1 = write onto existing.
LONG KeystoreImport(LPCTSTR file, int onto)
{
	BYTE* stream = NULL;
	DWORD len = 0;
	StrList tgtFolders, fileFolders;
	FaList  tgtFa, fileFa;
	RemapTable remap;
	StrList skip;
	IpcWriteBatch batch;
	ImportCtx ic;
	DWORD st;

	// Snapshot the current target: folders and their identity addresses.
	st = IpcEnum(BT_KEYS_SUBKEY, &stream, &len);
	if (st != ERROR_SUCCESS) return (LONG)st;
	if (!SlInit(&tgtFolders) || !FaInit(&tgtFa))
	{ SlFree(&tgtFolders); FaFree(&tgtFa); IpcFree(stream); return ERROR_OUTOFMEMORY; }
	{
		ScanCtx sc; sc.folders = &tgtFolders; sc.fa = &tgtFa;
		WalkStream(stream, len, ScanVisit, &sc);
	}
	IpcFree(stream);

	// Pre-scan the file the same way.
	if (!SlInit(&fileFolders) || !FaInit(&fileFa) || !RtInit(&remap) || !SlInit(&skip))
	{
		SlFree(&tgtFolders); FaFree(&tgtFa); SlFree(&fileFolders);
		FaFree(&fileFa); RtFree(&remap); SlFree(&skip);
		return ERROR_OUTOFMEMORY;
	}
	{
		ScanCtx sc; sc.folders = &fileFolders; sc.fa = &fileFa;
		st = KeyFileParse(file, ScanVisit, &sc);
	}
	if (st != ERROR_SUCCESS) goto cleanup;

	// For each incoming device: if the same identity already exists on the
	// target under a different folder name, offer to write onto that folder.
	{
		DWORD i;
		for (i = 0; i < fileFolders.count; i++)
		{
			LPCTSTR ff = SlGet(&fileFolders, i);
			LPCTSTR id, existing;
			int decision;

			if (SlHas(&tgtFolders, ff)) continue;      // exact folder -> overwrite as-is
			id = FaIdOf(&fileFa, ff);
			if (!id) continue;                         // no identity in file -> as-is
			existing = FaFolderForId(&tgtFa, id, ff);  // same identity, other folder
			if (!existing) continue;                   // brand new device -> as-is

			decision = onto;
			if (decision == -1)
			{
				TCHAR c;
				ConsolePrintf(TEXT("Device %s exists on this PC as %s (same identity).\n"), ff, existing);
				ConsolePuts(TEXT("Write onto [E]xisting, keep [F]ile name, or [S]kip? [E/F/S]: "));
				c = ConsoleReadChar();
				ConsolePuts(TEXT("\n"));
				if (c == TEXT('e') || c == TEXT('E'))      decision = 1;
				else if (c == TEXT('s') || c == TEXT('S')) decision = 2;
				else                                       decision = 0;
			}

			if (decision == 1)      RtAdd(&remap, ff, existing);
			else if (decision == 2) SlAdd(&skip, ff);
			// decision 0 -> keep file name, nothing to record
		}
	}

	st = IpcWriteBegin(&batch);
	if (st != ERROR_SUCCESS) goto cleanup;

	ic.batch = &batch;
	ic.count = 0;
	ic.remap = &remap;
	ic.skip  = &skip;

	st = KeyFileParse(file, ImportVisit, &ic);
	if (st != ERROR_SUCCESS)
	{
		batch.ok = FALSE;       // release the mapping without starting the service
		IpcWriteCommit(&batch);
		goto cleanup;
	}

	st = IpcWriteCommit(&batch);
	if (st == ERROR_SUCCESS)
		ConsolePrintf(TEXT("Imported %u value(s).\n"), ic.count);

cleanup:
	SlFree(&tgtFolders); FaFree(&tgtFa);
	SlFree(&fileFolders); FaFree(&fileFa);
	RtFree(&remap); SlFree(&skip);
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
