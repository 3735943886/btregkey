#ifndef BTREGKEY_APP_BTKEYS_H
#define BTREGKEY_APP_BTKEYS_H

#include <Windows.h>

// Where Windows keeps Bluetooth link/LE pairing keys, under HKLM. Only the
// SYSTEM account may read this subtree, which is why the helper service exists.
//   <adapter>            value <device>  = classic BR/EDR link key
//   <adapter>\<device>   values LTK/EDIV/ERand/IRK/... = BLE pairing material
#define BT_KEYS_SUBKEY \
	TEXT("SYSTEM\\CurrentControlSet\\Services\\BTHPORT\\Parameters\\Keys")

// Registry value type <-> text used in the export file.
// Known types map to a short name (BINARY/DWORD/...); anything else becomes
// "RAW<n>" so the round-trip stays lossless. `out` needs room for >= 16 TCHARs.
void  RegTypeToText(DWORD type, LPTSTR out);
DWORD RegTypeFromText(LPCTSTR text);

#endif // BTREGKEY_APP_BTKEYS_H
