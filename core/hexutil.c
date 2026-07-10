#include "hexutil.h"

static __inline int HexNibble(TCHAR c)
{
	if (c >= TEXT('0') && c <= TEXT('9')) return c - TEXT('0');
	if (c >= TEXT('a') && c <= TEXT('f')) return c - TEXT('a') + 10;
	if (c >= TEXT('A') && c <= TEXT('F')) return c - TEXT('A') + 10;
	return -1;
}

void HexFromBytes(const BYTE* data, DWORD len, LPTSTR out)
{
	static const TCHAR digits[] = TEXT("0123456789abcdef");
	DWORD i;
	for (i = 0; i < len; i++)
	{
		out[i * 2] = digits[(data[i] >> 4) & 0x0f];
		out[i * 2 + 1] = digits[data[i] & 0x0f];
	}
	out[len * 2] = 0;
}

DWORD BytesFromHex(LPCTSTR text, BYTE* out, DWORD maxBytes)
{
	DWORD count = 0;
	int hi = -1;

	if (text[0] == TEXT('0') && (text[1] == TEXT('x') || text[1] == TEXT('X')))
		text += 2;

	for (; *text; text++)
	{
		int n = HexNibble(*text);
		if (n < 0) continue; // ignore separators (dash, space, colon)

		if (hi < 0)
		{
			hi = n;
		}
		else
		{
			if (count >= maxBytes) break;
			out[count++] = (BYTE)((hi << 4) | n);
			hi = -1;
		}
	}
	return count;
}

DWORD DwordFromHex(LPCTSTR text)
{
	DWORD value = 0;

	if (text[0] == TEXT('0') && (text[1] == TEXT('x') || text[1] == TEXT('X')))
		text += 2;

	for (; *text; text++)
	{
		int n = HexNibble(*text);
		if (n < 0) continue;
		value = (value << 4) | (DWORD)n;
	}
	return value;
}
