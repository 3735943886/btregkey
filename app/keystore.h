#ifndef BTREGKEY_APP_KEYSTORE_H
#define BTREGKEY_APP_KEYSTORE_H

#include <Windows.h>

// High-level Bluetooth key operations. Each expects an IPC session to be open
// (IpcSessionBegin) and returns a Win32 error code.

LONG KeystoreList(void);                 // print all keys to the console

// Dump all keys to a file. A BLE device's folder name can differ from its real
// identity address (the folder's `Address` value); `normalize` controls whether
// the export rewrites folder names to that identity: -1 ask (if any mismatch),
// 0 never, 1 always.
LONG KeystoreExport(LPCTSTR file, int normalize);

// Write keys from a file. If an incoming device already exists on this PC under
// a different folder name (same identity `Address`), `onto` decides: -1 ask per
// device, 0 keep the file's folder names, 1 write onto the existing folder.
LONG KeystoreImport(LPCTSTR file, int onto);

// Manually set one classic BR/EDR link key (adapter/device are 12-hex MACs,
// hexKey is the 16-byte key in hex). Kept for quick one-off use.
LONG KeystoreSetClassic(LPCTSTR adapter, LPCTSTR device, LPCTSTR hexKey);

// Delete keys. If device is NULL/empty, the whole adapter subtree is removed
// (e.g. leftovers from a retired dongle). Otherwise the one device is removed,
// whether it is stored as a classic value or a BLE subkey. Prints how many
// items were removed.
LONG KeystoreDelete(LPCTSTR adapter, LPCTSTR device);

#endif // BTREGKEY_APP_KEYSTORE_H
