#ifndef BTREGKEY_CORE_ELEVATION_H
#define BTREGKEY_CORE_ELEVATION_H

#include <Windows.h>

// TRUE if the current process token is elevated (admin or SYSTEM).
BOOL IsProcessElevated(void);

// Relaunch this executable elevated ("runas"), forwarding the same arguments.
// Returns TRUE if the elevated process was launched.
BOOL RelaunchElevated(void);

#endif // BTREGKEY_CORE_ELEVATION_H
