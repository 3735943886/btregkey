#ifndef BTREGKEY_CORE_CONSOLE_H
#define BTREGKEY_CORE_CONSOLE_H

#include <Windows.h>

// Thin console output layer. Allocates a console if the process was started
// without one (e.g. relaunched elevated), so output is always visible.

void ConsoleInit(void);

// Write a plain string to stdout. Returns FALSE on failure.
BOOL ConsolePuts(LPCTSTR text);

// printf-style output. Uses wvsprintf (user32), so it is CRT-free but limited
// to wsprintf format specifiers (%s %d %u %x %c ...). No %f, no width for %s.
DWORD ConsolePrintf(LPCTSTR fmt, ...);

// Drain the keyboard buffer, then block until the user presses a key.
void ConsoleWaitKey(void);

// Block until the user presses a character key and return that character
// (0 if input is unavailable). Used for y/n confirmation prompts.
TCHAR ConsoleReadChar(void);

#endif // BTREGKEY_CORE_CONSOLE_H
