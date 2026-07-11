# btregkey

A small Windows command-line tool for **backing up, restoring, and transferring
Bluetooth pairing keys**.

Windows stores the link/pairing keys for every paired Bluetooth device in a
registry hive that **only the `SYSTEM` account may read**:

```
HKLM\SYSTEM\CurrentControlSet\Services\BTHPORT\Parameters\Keys
    <adapter-mac>                       value <device-mac> = classic BR/EDR link key
    <adapter-mac>\<device-mac>          values LTK / EDIV / ERand / IRK / ... = BLE pairing material
```

`btregkey` reads that subtree, dumps it to a plain text file, and can write it
back — on the same machine or a different one.

## Why you'd want this

When you pair a Bluetooth device (mouse, keyboard, headset, controller) it stores
a secret key that is tied to **one** host. The device forgets the old key when
you re-pair, which is why a peripheral shared between two systems has to be
re-paired every time you switch.

Typical uses:

- **Dual boot** — pair a device once, then copy its key into the other OS's
  Windows install so both boots work without re-pairing.
- **Reinstall / migration** — export before wiping Windows, import on the fresh
  install and every device keeps working.
- **Inspection / backup** — keep a copy of your pairing keys.

## Usage

```
btregkey                     list all Bluetooth keys (default)
btregkey list                list all Bluetooth keys
btregkey export <file>       save all keys to a file
btregkey import <file>       write keys back from a file
btregkey set <adapter> <device> <key>
                             set one classic link key (all values hex)
btregkey delete <adapter>            remove all keys for one adapter
btregkey delete <adapter> <device>   remove one paired device's key
                             (add -y to skip the confirmation prompt)
btregkey version                     print the version and exit
```

Move an exported file to another PC and `import` it there.

`delete` is for pruning stale entries — e.g. keys left behind by a Bluetooth
dongle you no longer use (each dongle appears as its own `<adapter>` block).
It is destructive and asks for confirmation unless you pass `-y`. When a
`<device>` is given it is removed whether it is stored as a classic value or a
BLE subkey; with no `<device>` the whole adapter block is removed. Export first
if you want a backup.

The tool requires administrator rights. If launched from a normal console it
automatically relaunches itself elevated.

### Export file format

UTF‑16LE, one tab-separated record per line, relative to the keys subtree:

```
<keyPath>\t<valueName>\t<TYPE>\t<hexdata>
```

- An empty value name is written `@`, empty data is written `-`.
- `<TYPE>` is a short name (`BINARY`, `DWORD`, `QWORD`, `SZ`, `MULTISZ`, `NONE`);
  any other registry type is written `RAW<n>` so the round-trip stays lossless.
- Lines starting with `#` are comments.

## How it works

The protected keys hive can't be read by a normal admin process — it needs the
`SYSTEM` account. Rather than shipping a separate service binary, `btregkey`
plays three roles from a single executable, chosen at startup:

1. **Non-elevated console** → relaunches itself elevated.
2. **Elevated console** → installs *this same exe* as a short-lived Windows
   service (which the SCM runs as `LocalSystem`), hands it one request over a
   named shared-memory block, waits for the result, then removes the service.
3. **SCM-launched service** → runs as `SYSTEM`, performs the registry
   enumerate/write, and reports status back through the shared memory.

Everything is torn down when the command finishes — no service is left
installed.

```
main.c                 role dispatch (service / elevated / relaunch)
app/
  cli.c                argument parsing and command dispatch
  keystore.c           list / export / import / set / delete operations
  keyfile.c            export-file reader & writer
  btkeys.c             keys subkey path + registry-type <-> text
core/
  ipc_client.c         client side: install service, send request, poll
  ipc_server.c         service side: handle ENUM / WRITE requests
  service_host.c       SCM service entry / dispatcher
  scm.c                install / start / stop / delete the helper service
  registry.c           recursive registry read/write
  elevation.c          elevation check + relaunch (runas)
  console.c            console I/O
  hexutil.c            hex <-> bytes
```

Client and service communicate through a named shared-memory mapping
(`IPC_MAPPING_NAME`), one request per service start: the client fills the
payload and sets the op code, the service does the work and writes a Win32
status back. See [`core/ipc.h`](core/ipc.h) for the protocol.

## Building

Requires the **MSVC C/C++ compiler** and the Windows SDK — install Visual
Studio 2022/2026 (or the standalone Build Tools) with the "Desktop development
with C++" workload. The project pins no specific compiler version: it uses
`<PlatformToolset>$(DefaultPlatformToolset)</PlatformToolset>`, which resolves
to whatever MSVC toolset the building install provides (v143 on VS 2022, v145
on VS 2026). Likewise `WindowsTargetPlatformVersion` is `10.0`, i.e. the latest
installed SDK. So it builds as-is on any recent MSVC.

```powershell
# from a Developer prompt, or point at MSBuild directly:
msbuild btregkey.vcxproj /p:Configuration=Release /p:Platform=x64

# e.g. with the VS 2026 Community MSBuild:
& "C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe" `
    btregkey.vcxproj /p:Configuration=Release /p:Platform=x64
```

Configurations `Debug`/`Release` for `x64` and `Win32` are provided. The project
uses a custom entry point (`btregkey`) and links as a console application.

The output is a single self-contained `btregkey.exe` — it imports only core
Windows DLLs (`kernel32`, `user32`, `advapi32`, `shell32`, `shlwapi`) and needs
no Visual C++ runtime redistributable.

### Versioning

The version lives in one place, [`version.h`](version.h) (three numeric
macros). Both the console output (`btregkey version`, and the banner) and the
Windows PE version resource ([`btregkey.rc`](btregkey.rc), visible under the
exe's Properties → Details) are derived from it, so bumping the version is a
one-line change.

## Notes & caveats

- Pairing keys are secrets. Treat exported files like passwords and delete them
  when done.
- Classic BR/EDR and BLE devices store keys differently; `export`/`import` copy
  the whole subtree verbatim, so both kinds transfer as-is. `set` only writes a
  single classic link key.
- Copying a key to another host does **not** update the peripheral, so the same
  key ends up valid on both hosts — that's exactly what makes dual-boot work.
- Windows only. Requires administrator rights.
