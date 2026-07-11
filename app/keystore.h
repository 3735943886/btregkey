#ifndef BTREGKEY_APP_KEYSTORE_H
#define BTREGKEY_APP_KEYSTORE_H

#include <Windows.h>

// High-level Bluetooth key operations. Each expects an IPC session to be open
// (IpcSessionBegin) and returns a Win32 error code.

LONG KeystoreList(void);                 // print all keys to the console
LONG KeystoreExport(LPCTSTR file);       // dump all keys to a file
LONG KeystoreImport(LPCTSTR file);       // write keys from a file

// Manually set one classic BR/EDR link key (adapter/device are 12-hex MACs,
// hexKey is the 16-byte key in hex). Kept for quick one-off use.
LONG KeystoreSetClassic(LPCTSTR adapter, LPCTSTR device, LPCTSTR hexKey);

// Delete keys. If device is NULL/empty, the whole adapter subtree is removed
// (e.g. leftovers from a retired dongle). Otherwise the one device is removed,
// whether it is stored as a classic value or a BLE subkey. Prints how many
// items were removed.
LONG KeystoreDelete(LPCTSTR adapter, LPCTSTR device);

#endif // BTREGKEY_APP_KEYSTORE_H
