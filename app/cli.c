#include "cli.h"
#include "keystore.h"
#include "../core/ipc_client.h"
#include "../core/console.h"

static void Usage(void)
{
	ConsolePuts(
		TEXT("btregkey - Bluetooth pairing key tool\n")
		TEXT("\n")
		TEXT("Usage:\n")
		TEXT("  btregkey                     list all Bluetooth keys\n")
		TEXT("  btregkey list                list all Bluetooth keys\n")
		TEXT("  btregkey export <file>       save all keys to a file\n")
		TEXT("  btregkey import <file>       write keys back from a file\n")
		TEXT("  btregkey set <adapter> <device> <key>\n")
		TEXT("                               set one classic link key (all hex)\n")
		TEXT("\n")
		TEXT("Move an exported file to another PC and import it there.\n"));
}

static BOOL EqI(LPCTSTR a, LPCTSTR b)
{
	return lstrcmpi(a, b) == 0;
}

int CliRun(int argc, LPTSTR* argv)
{
	LPCTSTR cmd = (argc > 1) ? argv[1] : NULL;
	DWORD st;
	LONG rc = ERROR_SUCCESS;

	// Help never needs the service.
	if (cmd && (EqI(cmd, TEXT("help")) || EqI(cmd, TEXT("-h")) ||
	            lstrcmp(cmd, TEXT("/?")) == 0))
	{
		Usage();
		return 0;
	}

	// Validate operands before touching the service.
	if (cmd && (EqI(cmd, TEXT("export")) || EqI(cmd, TEXT("import"))) && argc < 3)
	{
		ConsolePrintf(TEXT("'%s' needs a file path.\n\n"), cmd);
		Usage();
		return 1;
	}
	if (cmd && EqI(cmd, TEXT("set")) && argc < 5)
	{
		ConsolePuts(TEXT("'set' needs <adapter> <device> <key>.\n\n"));
		Usage();
		return 1;
	}

	// Install the SYSTEM helper service for the duration of the command.
	st = IpcSessionBegin();
	if (st != ERROR_SUCCESS)
	{
		ConsolePrintf(TEXT("Failed to install helper service. Error %u\n"), st);
		return 1;
	}

	if (!cmd || EqI(cmd, TEXT("list")))
	{
		rc = KeystoreList();
		if (!cmd) { ConsolePuts(TEXT("\n")); Usage(); }
	}
	else if (EqI(cmd, TEXT("export")))
	{
		rc = KeystoreExport(argv[2]);
		if (rc == ERROR_SUCCESS)
			ConsolePrintf(TEXT("Exported to %s\n"), argv[2]);
	}
	else if (EqI(cmd, TEXT("import")))
	{
		rc = KeystoreImport(argv[2]);
	}
	else if (EqI(cmd, TEXT("set")))
	{
		rc = KeystoreSetClassic(argv[2], argv[3], argv[4]);
		if (rc == ERROR_SUCCESS)
			ConsolePuts(TEXT("Key set.\n"));
	}
	else
	{
		ConsolePrintf(TEXT("Unknown command: %s\n\n"), cmd);
		Usage();
	}

	if (rc != ERROR_SUCCESS)
		ConsolePrintf(TEXT("Operation failed. Error %u\n"), (DWORD)rc);

	IpcSessionEnd();
	return (rc == ERROR_SUCCESS) ? 0 : 1;
}
