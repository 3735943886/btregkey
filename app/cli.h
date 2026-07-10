#ifndef BTREGKEY_APP_CLI_H
#define BTREGKEY_APP_CLI_H

#include <Windows.h>

// Parse the command line and run the requested command. Manages the helper
// service session around operations. Returns a process exit code (0 = success).
int CliRun(int argc, LPTSTR* argv);

#endif // BTREGKEY_APP_CLI_H
