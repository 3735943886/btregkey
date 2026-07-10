#ifndef BTREGKEY_CORE_IPC_SERVER_H
#define BTREGKEY_CORE_IPC_SERVER_H

#include <Windows.h>

// Service-side worker. Matches ServiceWorkerFn: opens the shared mapping,
// handles exactly one request, then blocks until the stop event is signalled.
// Pass this to ServiceHostDispatch().
void IpcServerWorker(HANDLE hStopEvent);

#endif // BTREGKEY_CORE_IPC_SERVER_H
