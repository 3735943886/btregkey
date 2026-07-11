#ifndef BTREGKEY_CORE_IPC_H
#define BTREGKEY_CORE_IPC_H

#include <Windows.h>

// Client <-> service transport.
//
// The console client cannot touch the SYSTEM-only registry keys directly, so it
// installs this same executable as a short-lived service (which runs as
// LocalSystem) and hands work to it through a named shared-memory block.
//
// One request per service start:
//   client: fill header + payload, set `op`, ServiceStart
//   service: read `op`, do the work, write `status`, set `op = IPC_OP_NONE`
//   client: poll until `op == IPC_OP_NONE`, read `status`, ServiceStop

#define IPC_SERVICE_NAME  TEXT("BTREGKEYSVC")
#define IPC_MAPPING_NAME  TEXT("Global\\{3B08CC59-233E-4742-AF17-7462891D9BA4}")

// Payload capacity (bytes) after the header. 1 MiB is far more than the whole
// Bluetooth key tree needs.
#define IPC_PAYLOAD_MAX   (1024u * 1024u)

typedef enum
{
	IPC_OP_NONE   = 0, // idle / completed
	IPC_OP_ENUM   = 1, // enumerate HKLM\<subkey> recursively into the payload
	IPC_OP_WRITE  = 2, // apply a packed list of registry values from the payload
	IPC_OP_DELETE = 3, // delete a packed list of registry values / subtrees
} IpcOp;

#pragma pack(push, 4)
typedef struct
{
	volatile LONG op;         // IpcOp; nonzero = request pending
	volatile LONG status;     // Win32 result written by the service
	DWORD         payloadLen; // valid bytes in payload (request in / response out)
	DWORD         reserved;
} IpcHeader;
#pragma pack(pop)

#define IPC_MAPPING_SIZE  ((DWORD)sizeof(IpcHeader) + IPC_PAYLOAD_MAX)

// Poll budget: IPC_TIMEOUT_TICKS * IPC_TICK_MS milliseconds.
#define IPC_TICK_MS        10
#define IPC_TIMEOUT_TICKS  1000

#endif // BTREGKEY_CORE_IPC_H
