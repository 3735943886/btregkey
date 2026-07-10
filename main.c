#include <Windows.h>
#include "core/ipc.h"
#include "core/ipc_server.h"
#include "core/service_host.h"
#include "core/elevation.h"
#include "core/console.h"
#include "app/cli.h"

// Custom entry point (see EntryPointSymbol in the project). The process runs in
// one of three roles depending on how it was started:
//
//   * SCM-launched service  -> ServiceHostDispatch connects and runs the worker
//   * elevated console       -> parse args and do the work
//   * non-elevated console    -> relaunch elevated
void WINAPI btregkey(void)
{
	if (IsProcessElevated())
	{
		// Elevated: this is either the service the SCM just started, or an
		// admin console run. The dispatcher tells us which.
		if (ServiceHostDispatch(IPC_SERVICE_NAME, IpcServerWorker))
			ExitProcess(0); // ran as the service; done

		ConsoleInit();
		{
			int argc = 0;
			LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
			int rc = CliRun(argc, argv);
			if (argv) LocalFree(argv);

			ConsolePuts(TEXT("\nPress any key to exit.\n"));
			ConsoleWaitKey();
			ExitProcess((UINT)rc);
		}
	}
	else
	{
		ConsoleInit();
		ConsolePuts(TEXT("Administrator privileges are required. Relaunching...\n"));
		RelaunchElevated();
		ExitProcess(0);
	}
}
