#include "cli.h"
#include "keystore.h"
#include "../core/ipc_client.h"
#include "../core/console.h"
#include "../version.h"

static void Usage(void)
{
	ConsolePuts(
		TEXT("btregkey ") BTREGKEY_VERSION_WSTR TEXT(" - Bluetooth pairing key tool\n")
		TEXT("\n")
		TEXT("Usage:\n")
		TEXT("  btregkey                     list all Bluetooth keys\n")
		TEXT("  btregkey list                list all Bluetooth keys\n")
		TEXT("  btregkey export <file>       save all keys to a file\n")
		TEXT("       [--normalize|--no-normalize]  fix BLE folder names to real address\n")
		TEXT("  btregkey import <file>       write keys back from a file\n")
		TEXT("       [--onto-existing|--as-is]     match devices already paired here\n")
		TEXT("  btregkey set <adapter> <device> <key>\n")
		TEXT("                               set one classic link key (all hex)\n")
		TEXT("  btregkey delete <adapter>            remove all keys for one adapter\n")
		TEXT("  btregkey delete <adapter> <device>   remove one paired device's key\n")
		TEXT("                               (add -y to skip the confirmation)\n")
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
	BOOL isDelete = cmd && (EqI(cmd, TEXT("delete")) || EqI(cmd, TEXT("del")));
	LPCTSTR delAdapter = NULL;
	LPCTSTR delDevice = NULL;

	// Help never needs the service.
	if (cmd && (EqI(cmd, TEXT("help")) || EqI(cmd, TEXT("-h")) ||
	            lstrcmp(cmd, TEXT("/?")) == 0))
	{
		Usage();
		return 0;
	}

	// Neither does version.
	if (cmd && (EqI(cmd, TEXT("version")) || EqI(cmd, TEXT("--version")) ||
	            EqI(cmd, TEXT("-v"))))
	{
		ConsolePuts(TEXT("btregkey ") BTREGKEY_VERSION_WSTR TEXT("\n"));
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

	// 'delete' is destructive: parse operands and confirm before we bother
	// installing the service or touching the registry.
	if (isDelete)
	{
		int k;
		BOOL assumeYes = FALSE;

		if (argc < 3)
		{
			ConsolePuts(TEXT("'delete' needs an <adapter> (and optional <device>).\n\n"));
			Usage();
			return 1;
		}

		delAdapter = argv[2];
		for (k = 3; k < argc; k++)
		{
			if (EqI(argv[k], TEXT("-y")) || lstrcmp(argv[k], TEXT("/y")) == 0)
				assumeYes = TRUE;
			else if (!delDevice)
				delDevice = argv[k];
		}

		if (delDevice)
			ConsolePrintf(TEXT("About to delete device %s under adapter %s.\n"),
			              delDevice, delAdapter);
		else
			ConsolePrintf(TEXT("About to delete ALL keys under adapter %s.\n"),
			              delAdapter);

		if (!assumeYes)
		{
			TCHAR c;
			ConsolePuts(TEXT("This cannot be undone. Type Y to continue: "));
			c = ConsoleReadChar();
			ConsolePuts(TEXT("\n"));
			if (c != TEXT('y') && c != TEXT('Y'))
			{
				ConsolePuts(TEXT("Cancelled.\n"));
				return 0;
			}
		}
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
		int normalize = -1; // ask
		int k;
		for (k = 3; k < argc; k++)
		{
			if (EqI(argv[k], TEXT("--normalize")))         normalize = 1;
			else if (EqI(argv[k], TEXT("--no-normalize"))) normalize = 0;
		}
		rc = KeystoreExport(argv[2], normalize);
		if (rc == ERROR_SUCCESS)
			ConsolePrintf(TEXT("Exported to %s\n"), argv[2]);
	}
	else if (EqI(cmd, TEXT("import")))
	{
		int onto = -1; // ask
		int k;
		for (k = 3; k < argc; k++)
		{
			if (EqI(argv[k], TEXT("--onto-existing"))) onto = 1;
			else if (EqI(argv[k], TEXT("--as-is")))    onto = 0;
		}
		rc = KeystoreImport(argv[2], onto);
	}
	else if (EqI(cmd, TEXT("set")))
	{
		rc = KeystoreSetClassic(argv[2], argv[3], argv[4]);
		if (rc == ERROR_SUCCESS)
			ConsolePuts(TEXT("Key set.\n"));
	}
	else if (isDelete)
	{
		rc = KeystoreDelete(delAdapter, delDevice);
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
