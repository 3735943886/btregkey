#ifndef BTREGKEY_CORE_SERVICE_HOST_H
#define BTREGKEY_CORE_SERVICE_HOST_H

#include <Windows.h>

// The work a service performs while running. It receives an event that is
// signalled when the service is asked to stop.
typedef void (*ServiceWorkerFn)(HANDLE hStopEvent);

// Attempt to run as an SCM-launched service.
//   - Returns TRUE  if the SCM connected: the call blocks until the service
//     stops, running `worker` on a background thread meanwhile.
//   - Returns FALSE if the process was NOT started by the SCM (normal console
//     launch). In that case nothing was done and the caller proceeds as a client.
BOOL ServiceHostDispatch(LPCTSTR name, ServiceWorkerFn worker);

#endif // BTREGKEY_CORE_SERVICE_HOST_H
