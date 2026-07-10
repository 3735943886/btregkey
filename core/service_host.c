#include "service_host.h"
#include "util.h"

// ServiceMain has a fixed signature and no user parameter, so the service name
// and worker are stashed here for the duration of a single dispatch call.
static LPCTSTR          g_serviceName = NULL;
static ServiceWorkerFn  g_worker = NULL;

static SERVICE_STATUS         g_status;
static SERVICE_STATUS_HANDLE  g_statusHandle = NULL;
static HANDLE                 g_stopEvent = INVALID_HANDLE_VALUE;

static void SetState(DWORD state, DWORD controls, DWORD checkpoint)
{
	g_status.dwCurrentState = state;
	g_status.dwControlsAccepted = controls;
	g_status.dwWin32ExitCode = 0;
	g_status.dwCheckPoint = checkpoint;
	SetServiceStatus(g_statusHandle, &g_status);
}

static void WINAPI HostCtrlHandler(DWORD code)
{
	if (code == SERVICE_CONTROL_STOP && g_status.dwCurrentState == SERVICE_RUNNING)
	{
		SetState(SERVICE_STOP_PENDING, 0, 4);
		SetEvent(g_stopEvent);
	}
}

static DWORD WINAPI HostWorkerThread(LPVOID param)
{
	(void)param;
	if (g_worker) g_worker(g_stopEvent);
	return ERROR_SUCCESS;
}

static void WINAPI HostServiceMain(DWORD argc, LPTSTR* argv)
{
	(void)argc; (void)argv;

	g_statusHandle = RegisterServiceCtrlHandler(g_serviceName, HostCtrlHandler);
	if (!g_statusHandle) return;

	MemZero(&g_status, sizeof(g_status));
	g_status.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
	SetState(SERVICE_START_PENDING, 0, 0);

	g_stopEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
	if (!g_stopEvent)
	{
		SetState(SERVICE_STOPPED, 0, 1);
		return;
	}

	SetState(SERVICE_RUNNING, SERVICE_ACCEPT_STOP, 0);

	{
		HANDLE hThread = CreateThread(NULL, 0, HostWorkerThread, NULL, 0, NULL);
		if (hThread)
		{
			WaitForSingleObject(hThread, INFINITE);
			CloseHandle(hThread);
		}
	}

	CloseHandle(g_stopEvent);
	g_stopEvent = INVALID_HANDLE_VALUE;
	SetState(SERVICE_STOPPED, 0, 3);
}

BOOL ServiceHostDispatch(LPCTSTR name, ServiceWorkerFn worker)
{
	SERVICE_TABLE_ENTRY table[] =
	{
		{ (LPTSTR)name, HostServiceMain },
		{ NULL, NULL }
	};

	g_serviceName = name;
	g_worker = worker;

	if (StartServiceCtrlDispatcher(table))
		return TRUE;

	// Not launched by the SCM: this is a normal console run.
	return FALSE;
}
