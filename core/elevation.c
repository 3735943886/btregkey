#include "elevation.h"
#include <Shlwapi.h>

BOOL IsProcessElevated(void)
{
	BOOL elevated = FALSE;
	HANDLE hToken = NULL;

	if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &hToken))
	{
		TOKEN_ELEVATION info = { 0 };
		DWORD cb = sizeof(info);
		if (GetTokenInformation(hToken, TokenElevation, &info, sizeof(info), &cb))
		{
			elevated = info.TokenIsElevated;
		}
		CloseHandle(hToken);
	}
	return elevated;
}

BOOL RelaunchElevated(void)
{
	// Path names can exceed MAX_PATH, so use the extended limit.
	const DWORD pathMax = 32768;
	BOOL ok = FALSE;
	LPTSTR path;

	path = (LPTSTR)HeapAlloc(GetProcessHeap(), 0, pathMax * sizeof(TCHAR));
	if (!path) return FALSE;

	if (GetModuleFileName(NULL, path, pathMax))
	{
		LPCTSTR args = PathGetArgs(GetCommandLine());
		if (ShellExecute(NULL, TEXT("runas"), path, args, NULL, SW_SHOWNORMAL))
		{
			ok = TRUE;
		}
	}

	HeapFree(GetProcessHeap(), 0, path);
	return ok;
}
