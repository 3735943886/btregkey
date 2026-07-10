#include "scm.h"

DWORD ServiceInstall(LPCTSTR name)
{
	SC_HANDLE scm;
	SC_HANDLE svc;
	TCHAR path[MAX_PATH];

	if (!GetModuleFileName(NULL, path, MAX_PATH))
		return GetLastError();

	scm = OpenSCManager(NULL, NULL, SC_MANAGER_ALL_ACCESS);
	if (!scm) return GetLastError();

	svc = CreateService(
		scm,
		name,                       // service key name
		name,                       // display name
		SERVICE_ALL_ACCESS,
		SERVICE_WIN32_OWN_PROCESS,
		SERVICE_DEMAND_START,       // started on demand by us
		SERVICE_ERROR_NORMAL,
		path,                       // this executable, run as the service
		NULL, NULL, NULL,
		NULL,                       // LocalSystem account
		NULL);

	if (svc)
	{
		CloseServiceHandle(svc);
		CloseServiceHandle(scm);
		return ERROR_SUCCESS;
	}

	{
		DWORD err = GetLastError();
		CloseServiceHandle(scm);
		return err;
	}
}

DWORD ServiceDelete(LPCTSTR name)
{
	SC_HANDLE scm;
	SC_HANDLE svc;
	DWORD err = ERROR_SUCCESS;

	scm = OpenSCManager(NULL, NULL, SC_MANAGER_ALL_ACCESS);
	if (!scm) return GetLastError();

	svc = OpenService(scm, name, DELETE);
	if (svc)
	{
		if (!DeleteService(svc)) err = GetLastError();
		CloseServiceHandle(svc);
	}
	else
	{
		err = GetLastError();
	}

	CloseServiceHandle(scm);
	return err;
}

DWORD ServiceStart(LPCTSTR name)
{
	SC_HANDLE scm;
	SC_HANDLE svc;
	DWORD err = ERROR_SUCCESS;

	scm = OpenSCManager(NULL, NULL, SC_MANAGER_ALL_ACCESS);
	if (!scm) return GetLastError();

	svc = OpenService(scm, name, SERVICE_ALL_ACCESS);
	if (svc)
	{
		if (!StartService(svc, 0, NULL)) err = GetLastError();
		CloseServiceHandle(svc);
	}
	else
	{
		err = GetLastError();
	}

	CloseServiceHandle(scm);
	return err;
}

DWORD ServiceStop(LPCTSTR name)
{
	SC_HANDLE scm;
	SC_HANDLE svc;
	SERVICE_STATUS status;
	DWORD err = ERROR_SUCCESS;

	scm = OpenSCManager(NULL, NULL, SC_MANAGER_ALL_ACCESS);
	if (!scm) return GetLastError();

	svc = OpenService(scm, name, SERVICE_ALL_ACCESS);
	if (svc)
	{
		if (!ControlService(svc, SERVICE_CONTROL_STOP, &status)) err = GetLastError();
		CloseServiceHandle(svc);
	}
	else
	{
		err = GetLastError();
	}

	CloseServiceHandle(scm);
	return err;
}
