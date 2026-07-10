#include "btkeys.h"

typedef struct
{
	DWORD   type;
	LPCTSTR name;
} TypeName;

static const TypeName g_types[] =
{
	{ REG_BINARY,    TEXT("BINARY") },
	{ REG_DWORD,     TEXT("DWORD")  },
	{ REG_QWORD,     TEXT("QWORD")  },
	{ REG_SZ,        TEXT("SZ")     },
	{ REG_MULTI_SZ,  TEXT("MULTISZ")},
	{ REG_NONE,      TEXT("NONE")   },
};

void RegTypeToText(DWORD type, LPTSTR out)
{
	int i;
	int n = sizeof(g_types) / sizeof(g_types[0]);
	for (i = 0; i < n; i++)
	{
		if (g_types[i].type == type)
		{
			lstrcpy(out, g_types[i].name);
			return;
		}
	}
	// Unknown type: keep it round-trippable.
	wsprintf(out, TEXT("RAW%u"), type);
}

DWORD RegTypeFromText(LPCTSTR text)
{
	int i;
	int n = sizeof(g_types) / sizeof(g_types[0]);
	for (i = 0; i < n; i++)
	{
		if (lstrcmpi(text, g_types[i].name) == 0)
			return g_types[i].type;
	}

	if ((text[0] == TEXT('R') || text[0] == TEXT('r')) &&
	    (text[1] == TEXT('A') || text[1] == TEXT('a')) &&
	    (text[2] == TEXT('W') || text[2] == TEXT('w')))
	{
		DWORD v = 0;
		const TCHAR* s = text + 3;
		for (; *s >= TEXT('0') && *s <= TEXT('9'); s++)
			v = v * 10 + (DWORD)(*s - TEXT('0'));
		return v;
	}

	return REG_BINARY; // safe default
}
