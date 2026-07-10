#include "keyfile.h"
#include "btkeys.h"
#include "../core/hexutil.h"
#include "../core/util.h"

// --- growable UTF-16 text buffer (writer) ------------------------------------

static BOOL TbEnsure(KeyFileWriter* w, SIZE_T extra)
{
	if (!w->ok) return FALSE;
	if (w->len + extra <= w->cap) return TRUE;

	{
		SIZE_T ncap = w->cap ? w->cap : 4096;
		BYTE* nbuf;
		while (ncap < w->len + extra) ncap *= 2;

		nbuf = w->buf
			? (BYTE*)HeapReAlloc(GetProcessHeap(), 0, w->buf, ncap)
			: (BYTE*)HeapAlloc(GetProcessHeap(), 0, ncap);
		if (!nbuf) { w->ok = FALSE; return FALSE; }

		w->buf = nbuf;
		w->cap = ncap;
	}
	return TRUE;
}

static void TbText(KeyFileWriter* w, LPCTSTR s)
{
	SIZE_T bytes = (SIZE_T)lstrlen(s) * sizeof(TCHAR);
	if (!TbEnsure(w, bytes)) return;
	MemCopy(w->buf + w->len, s, bytes);
	w->len += bytes;
}

BOOL KeyFileBegin(KeyFileWriter* w)
{
	MemZero(w, sizeof(*w));
	w->ok = TRUE;
	TbText(w, TEXT("# btregkey key export v1\r\n"));
	TbText(w, TEXT("# keyPath <TAB> value <TAB> type <TAB> hexdata\r\n"));
	return w->ok;
}

void KeyFilePutValue(KeyFileWriter* w, LPCTSTR relKeyPath, LPCTSTR valueName,
                     DWORD type, const BYTE* data, DWORD dataLen)
{
	TCHAR typeText[16];

	if (!w->ok) return;

	TbText(w, relKeyPath ? relKeyPath : TEXT(""));
	TbText(w, TEXT("\t"));
	TbText(w, (valueName && valueName[0]) ? valueName : TEXT("@"));
	TbText(w, TEXT("\t"));

	RegTypeToText(type, typeText);
	TbText(w, typeText);
	TbText(w, TEXT("\t"));

	if (dataLen)
	{
		LPTSTR hex = (LPTSTR)HeapAlloc(GetProcessHeap(), 0, ((SIZE_T)dataLen * 2 + 1) * sizeof(TCHAR));
		if (hex)
		{
			HexFromBytes(data, dataLen, hex);
			TbText(w, hex);
			HeapFree(GetProcessHeap(), 0, hex);
		}
	}
	else
	{
		TbText(w, TEXT("-"));
	}

	TbText(w, TEXT("\r\n"));
}

BOOL KeyFileSave(KeyFileWriter* w, LPCTSTR path)
{
	BOOL ok = FALSE;
	HANDLE h;

	if (!w->ok) { KeyFileAbort(w); return FALSE; }

	h = CreateFile(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	if (h != INVALID_HANDLE_VALUE)
	{
		DWORD written;
		WORD bom = 0xFEFF; // UTF-16LE byte order mark
		ok = WriteFile(h, &bom, sizeof(bom), &written, NULL);
		if (ok && w->len)
			ok = WriteFile(h, w->buf, (DWORD)w->len, &written, NULL);
		CloseHandle(h);
	}

	KeyFileAbort(w);
	return ok;
}

void KeyFileAbort(KeyFileWriter* w)
{
	if (w->buf) HeapFree(GetProcessHeap(), 0, w->buf);
	w->buf = NULL;
	w->len = w->cap = 0;
}

// --- parser ------------------------------------------------------------------

// Split a null-terminated line into up to 4 tab-separated fields (in place).
// Returns the field count; missing fields point at empty strings.
static int SplitFields(LPTSTR line, LPTSTR fields[4])
{
	int n = 0;
	LPTSTR p = line;

	fields[0] = fields[1] = fields[2] = fields[3] = line + lstrlen(line); // empty

	fields[n++] = p;
	while (*p && n < 4)
	{
		if (*p == TEXT('\t'))
		{
			*p = 0;
			fields[n++] = p + 1;
		}
		p++;
	}
	// Terminate the last field at a trailing tab if present.
	for (; *p; p++)
		if (*p == TEXT('\t')) { *p = 0; break; }

	return n;
}

static void ParseLine(LPTSTR line, KeyFileValueFn fn, void* ctx)
{
	LPTSTR fields[4];
	LPTSTR keyPath, valName, typeText, hexData;
	DWORD type, dataLen, maxBytes;
	BYTE* data;

	// Skip leading whitespace to test for comments / blank lines.
	while (*line == TEXT(' ') || *line == TEXT('\t')) line++;
	if (*line == 0 || *line == TEXT('#')) return;

	if (SplitFields(line, fields) < 3) return; // need at least key,value,type

	keyPath  = fields[0];
	valName  = fields[1];
	typeText = fields[2];
	hexData  = fields[3];

	type = RegTypeFromText(typeText);

	if (hexData[0] == TEXT('-') && hexData[1] == 0)
	{
		data = NULL;
		dataLen = 0;
	}
	else
	{
		maxBytes = (lstrlen(hexData) / 2) + 1;
		data = (BYTE*)HeapAlloc(GetProcessHeap(), 0, maxBytes ? maxBytes : 1);
		if (!data) return;
		dataLen = BytesFromHex(hexData, data, maxBytes);
	}

	// "@" denotes the key's default (unnamed) value.
	{
		LPCTSTR name = (valName[0] == TEXT('@') && valName[1] == 0) ? NULL : valName;
		fn(ctx, keyPath, name, type, data, dataLen);
	}

	if (data) HeapFree(GetProcessHeap(), 0, data);
}

DWORD KeyFileParse(LPCTSTR path, KeyFileValueFn fn, void* ctx)
{
	HANDLE h;
	DWORD size, read;
	BYTE* raw;
	LPWSTR text;
	DWORD count, i, lineStart;

	h = CreateFile(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
	               FILE_ATTRIBUTE_NORMAL, NULL);
	if (h == INVALID_HANDLE_VALUE) return GetLastError();

	size = GetFileSize(h, NULL);
	// Room for the bytes plus a guaranteed WCHAR terminator.
	raw = (BYTE*)HeapAlloc(GetProcessHeap(), 0, (SIZE_T)size + sizeof(WCHAR));
	if (!raw) { CloseHandle(h); return ERROR_OUTOFMEMORY; }

	if (!ReadFile(h, raw, size, &read, NULL))
	{
		DWORD err = GetLastError();
		HeapFree(GetProcessHeap(), 0, raw);
		CloseHandle(h);
		return err;
	}
	CloseHandle(h);

	// Interpret as UTF-16LE, skipping a BOM if present.
	{
		BYTE* start = raw;
		DWORD bytes = read;
		if (bytes >= 2 && raw[0] == 0xFF && raw[1] == 0xFE) { start += 2; bytes -= 2; }
		text = (LPWSTR)start;
		count = bytes / sizeof(WCHAR);
		text[count] = 0; // safe: allocated one extra WCHAR
	}

	// Walk lines, terminating each at CR/LF and parsing it.
	lineStart = 0;
	for (i = 0; i <= count; i++)
	{
		if (i == count || text[i] == L'\n' || text[i] == L'\r')
		{
			WCHAR saved = text[i];
			text[i] = 0;
			if (i > lineStart) ParseLine(&text[lineStart], fn, ctx);
			text[i] = saved;
			lineStart = i + 1;
		}
	}

	HeapFree(GetProcessHeap(), 0, raw);
	return ERROR_SUCCESS;
}
