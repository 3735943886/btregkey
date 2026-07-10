#include "console.h"

static HANDLE g_hStdOut = INVALID_HANDLE_VALUE;
static HANDLE g_hStdIn = INVALID_HANDLE_VALUE;

#define CONSOLE_FMT_BUF_CCH 1024

void ConsoleInit(void)
{
	g_hStdOut = GetStdHandle(STD_OUTPUT_HANDLE);
	if (g_hStdOut == INVALID_HANDLE_VALUE || g_hStdOut == NULL)
	{
		if (AllocConsole())
		{
			g_hStdOut = GetStdHandle(STD_OUTPUT_HANDLE);
		}
	}
	g_hStdIn = GetStdHandle(STD_INPUT_HANDLE);
}

BOOL ConsolePuts(LPCTSTR text)
{
	if (g_hStdOut == INVALID_HANDLE_VALUE || g_hStdOut == NULL) return FALSE;
	return WriteConsole(g_hStdOut, text, lstrlen(text), NULL, NULL);
}

DWORD ConsolePrintf(LPCTSTR fmt, ...)
{
	TCHAR buf[CONSOLE_FMT_BUF_CCH];
	DWORD written = 0;
	va_list args;

	if (g_hStdOut == INVALID_HANDLE_VALUE || g_hStdOut == NULL) return 0;

	va_start(args, fmt);
	// wvsprintf caps at 1024 chars including the null terminator; it never
	// overruns a buffer of that size, matching CONSOLE_FMT_BUF_CCH.
	int len = wvsprintf(buf, fmt, args);
	va_end(args);

	if (len > 0)
	{
		WriteConsole(g_hStdOut, buf, (DWORD)len, &written, NULL);
	}
	return written;
}

void ConsoleWaitKey(void)
{
	INPUT_RECORD rec;
	DWORD pending = 0;
	DWORD read = 0;

	if (g_hStdIn == INVALID_HANDLE_VALUE || g_hStdIn == NULL) return;

	// Discard anything already queued so a stray keystroke doesn't skip the wait.
	if (GetNumberOfConsoleInputEvents(g_hStdIn, &pending))
	{
		while (pending)
		{
			if (!ReadConsoleInput(g_hStdIn, &rec, 1, &read)) break;
			pending--;
		}
	}

	// Block until an actual key-down arrives.
	for (;;)
	{
		if (!ReadConsoleInput(g_hStdIn, &rec, 1, &read)) break;
		if (rec.EventType == KEY_EVENT && rec.Event.KeyEvent.bKeyDown) break;
	}
}
