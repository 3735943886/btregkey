#include "registry.h"
#include "util.h"

// --- bounded stream writer ---------------------------------------------------
// Never writes past `end`. If a write would not fit, the buffer is marked full
// (end pulled down to p) so the enumerator's remaining-space checks stop it.

typedef struct
{
	BYTE* buf;
	BYTE* p;
	BYTE* end;
} StreamWriter;

static DWORD SwRemaining(StreamWriter* w)
{
	return (w->p < w->end) ? (DWORD)(w->end - w->p) : 0;
}

static void SwBytes(StreamWriter* w, const void* data, DWORD n)
{
	if (w->p + n <= w->end)
	{
		MemCopy(w->p, data, n);
		w->p += n;
	}
	else
	{
		w->end = w->p; // mark full
	}
}

static void SwDword(StreamWriter* w, DWORD v)
{
	SwBytes(w, &v, sizeof(v));
}

static void SwAlignTChar(StreamWriter* w)
{
	while (((SIZE_T)(w->p - w->buf) % sizeof(TCHAR)) != 0)
	{
		BYTE zero = 0;
		SwBytes(w, &zero, 1);
	}
}

// --- enumeration -------------------------------------------------------------

#define REG_NAME_CCH   16384u          // max registry value-name length + slack
#define REG_DATA_BYTES 65536u          // plenty for Bluetooth key blobs

typedef struct
{
	StreamWriter w;
	LPTSTR       name;      // REG_NAME_CCH TCHARs
	BYTE*        data;      // REG_DATA_BYTES bytes
	BOOL         recursive;
} EnumCtx;

static void EnumInto(HKEY hKey, EnumCtx* ctx)
{
	DWORD i;

	// Values of this key.
	for (i = 0; ; i++)
	{
		DWORD nch = REG_NAME_CCH;
		DWORD dlen = REG_DATA_BYTES;
		DWORD type = 0;
		LONG r = RegEnumValue(hKey, i, ctx->name, &nch, NULL, &type, ctx->data, &dlen);

		if (r == ERROR_MORE_DATA) continue;   // oversized value, skip it
		if (r != ERROR_SUCCESS) break;        // ERROR_NO_MORE_ITEMS or error

		{
			DWORD nameBytes = (nch + 1) * sizeof(TCHAR); // include null
			if (SwRemaining(&ctx->w) < 16 + nameBytes + dlen + sizeof(TCHAR)) break;

			SwDword(&ctx->w, REGSTREAM_VALUE);
			SwDword(&ctx->w, nameBytes);
			SwBytes(&ctx->w, ctx->name, nameBytes);
			SwDword(&ctx->w, type);
			SwDword(&ctx->w, dlen);
			SwBytes(&ctx->w, ctx->data, dlen);
			SwAlignTChar(&ctx->w);
		}
	}

	// Subkeys of this key.
	for (i = 0; ; i++)
	{
		DWORD nch = REG_NAME_CCH;
		LONG r = RegEnumKeyEx(hKey, i, ctx->name, &nch, NULL, NULL, NULL, NULL);
		if (r != ERROR_SUCCESS) break;

		{
			DWORD nameBytes = (nch + 1) * sizeof(TCHAR);
			if (SwRemaining(&ctx->w) < 8 + nameBytes + sizeof(TCHAR)) break;

			SwDword(&ctx->w, REGSTREAM_KEY);
			SwDword(&ctx->w, nameBytes);
			SwBytes(&ctx->w, ctx->name, nameBytes);

			if (ctx->recursive)
			{
				HKEY hSub;
				// ctx->name is consumed here; recursion is free to reuse it.
				if (RegOpenKeyEx(hKey, ctx->name, 0, KEY_READ, &hSub) == ERROR_SUCCESS)
				{
					SwDword(&ctx->w, REGSTREAM_STEPIN);
					EnumInto(hSub, ctx);
					SwDword(&ctx->w, REGSTREAM_STEPOUT);
					RegCloseKey(hSub);
				}
			}
		}
	}
}

DWORD RegistryEnumStream(HKEY hKey, BYTE* buf, DWORD bufSize, BOOL recursive)
{
	EnumCtx ctx;
	DWORD written = 0;

	ctx.w.buf = buf;
	ctx.w.p = buf;
	ctx.w.end = buf + bufSize;
	ctx.recursive = recursive;

	// Scratch on the heap: large stack frames would need __chkstk, which the
	// CRT-free link does not provide.
	ctx.name = (LPTSTR)HeapAlloc(GetProcessHeap(), 0, REG_NAME_CCH * sizeof(TCHAR));
	ctx.data = (BYTE*)HeapAlloc(GetProcessHeap(), 0, REG_DATA_BYTES);

	if (ctx.name && ctx.data)
	{
		EnumInto(hKey, &ctx);
		written = (DWORD)(ctx.w.p - buf);
	}

	if (ctx.name) HeapFree(GetProcessHeap(), 0, ctx.name);
	if (ctx.data) HeapFree(GetProcessHeap(), 0, ctx.data);
	return written;
}

LONG RegistryWriteValue(LPCTSTR subKey, LPCTSTR valueName, DWORD type,
                        const BYTE* data, DWORD dataBytes)
{
	return RegSetKeyValue(HKEY_LOCAL_MACHINE, subKey, valueName, type, data, dataBytes);
}
