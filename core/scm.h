#ifndef BTREGKEY_CORE_SCM_H
#define BTREGKEY_CORE_SCM_H

#include <Windows.h>

// Service Control Manager operations. Each returns a Win32 error code
// (ERROR_SUCCESS on success). The service binary is always this executable.

DWORD ServiceInstall(LPCTSTR name);
DWORD ServiceDelete(LPCTSTR name);
DWORD ServiceStart(LPCTSTR name);
DWORD ServiceStop(LPCTSTR name);

#endif // BTREGKEY_CORE_SCM_H
