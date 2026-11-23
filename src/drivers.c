/*
 * drivers.c -- Detection, installation and removal of the ViGEmBus and HidHide drivers.
 *
 * The official, signed installers from Nefarius Software Solutions are embedded into
 * this exe as RCDATA resources. They are Advanced Installer packages and support
 * silent installation with "/exenoui /qn /norestart".
 */

#include "drivers.h"
#include "hidhide.h"
#include "resource.h"
#include "log.h"

#include <windows.h>
#include <shellapi.h>
#include <wchar.h>
#include <stdio.h>

#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "user32.lib")

#define VIGEM_SERVICE L"ViGEmBus"
#define HIDHIDE_SERVICE L"HidHide"
#define VIGEM_DISPLAY_NAME L"ViGEm Bus Driver"
#define HIDHIDE_DISPLAY_NAME L"HidHide"

static BOOL _service_exists(LPCWSTR name)
{
    SC_HANDLE scm = OpenSCManagerW(NULL, NULL, SC_MANAGER_CONNECT);
    if (scm == NULL)
    {
        return FALSE;
    }
    SC_HANDLE svc = OpenServiceW(scm, name, SERVICE_QUERY_STATUS);
    BOOL exists = svc != NULL;
    if (svc != NULL)
    {
        CloseServiceHandle(svc);
    }
    CloseServiceHandle(scm);
    return exists;
}

void drivers_query(struct driver_status *status)
{
    status->vigem_installed = _service_exists(VIGEM_SERVICE);
    status->hidhide_installed = _service_exists(HIDHIDE_SERVICE);
    status->hidhide_ready = hidhide_available();
}

/* ---- elevated helper launch ---- */

DWORD drivers_run_elevated(HWND owner, LPCWSTR arg)
{
    WCHAR self[MAX_PATH * 2];
    if (GetModuleFileNameW(NULL, self, MAX_PATH * 2) == 0)
    {
        return DRV_RESULT_FAILED;
    }

    SHELLEXECUTEINFOW sei = {0};
    sei.cbSize = sizeof(sei);
    sei.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_NOASYNC;
    sei.hwnd = owner;
    sei.lpVerb = L"runas";
    sei.lpFile = self;
    sei.lpParameters = arg;
    sei.nShow = SW_HIDE;

    if (!ShellExecuteExW(&sei))
    {
        return GetLastError() == ERROR_CANCELLED ? DRV_RESULT_CANCELLED : DRV_RESULT_FAILED;
    }
    if (sei.hProcess == NULL)
    {
        return DRV_RESULT_FAILED;
    }

    /* Keep pumping messages so the tray icon stays responsive while we wait. */
    for (;;)
    {
        DWORD r = MsgWaitForMultipleObjects(1, &sei.hProcess, FALSE, INFINITE, QS_ALLINPUT);
        if (r == WAIT_OBJECT_0)
        {
            break;
        }
        MSG msg;
        while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE))
        {
            if (msg.message == WM_QUIT)
            {
                PostQuitMessage((int)msg.wParam);
                WaitForSingleObject(sei.hProcess, INFINITE);
                goto done;
            }
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
done:;
    DWORD code = DRV_RESULT_FAILED;
    GetExitCodeProcess(sei.hProcess, &code);
    CloseHandle(sei.hProcess);
    return code;
}

/* ---- helpers for the elevated side ---- */

static BOOL _run_and_wait(LPWSTR cmdline, DWORD *exit_code)
{
    STARTUPINFOW si = {0};
    PROCESS_INFORMATION pi = {0};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    if (!CreateProcessW(NULL, cmdline, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi))
    {
        return FALSE;
    }
    WaitForSingleObject(pi.hProcess, 15 * 60 * 1000);
    GetExitCodeProcess(pi.hProcess, exit_code);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return TRUE;
}

/* Writes the resource to target and returns the still-open handle (others may only read it),
   so the file cannot be swapped before it is executed. */
static HANDLE _extract_resource(int id, LPCWSTR target)
{
    HRSRC res = FindResourceW(NULL, MAKEINTRESOURCEW(id), (LPCWSTR)RT_RCDATA);
    if (res == NULL)
    {
        return INVALID_HANDLE_VALUE;
    }
    HGLOBAL data = LoadResource(NULL, res);
    DWORD size = SizeofResource(NULL, res);
    const void *bytes = LockResource(data);
    if (bytes == NULL || size == 0)
    {
        return INVALID_HANDLE_VALUE;
    }
    HANDLE f = CreateFileW(target, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (f == INVALID_HANDLE_VALUE)
    {
        return INVALID_HANDLE_VALUE;
    }
    DWORD written = 0;
    if (!WriteFile(f, bytes, size, &written, NULL) || written != size)
    {
        CloseHandle(f);
        DeleteFileW(target);
        return INVALID_HANDLE_VALUE;
    }
    CloseHandle(f);
    /* Re-open read-only and deny writers until the installer has finished. */
    return CreateFileW(target, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
}

static BOOL _is_success(DWORD code, BOOL *reboot)
{
    switch (code)
    {
    case ERROR_SUCCESS:
        return TRUE;
    case ERROR_SUCCESS_REBOOT_REQUIRED: /* 3010 */
    case ERROR_SUCCESS_REBOOT_INITIATED: /* 1641 */
        *reboot = TRUE;
        return TRUE;
    default:
        return FALSE;
    }
}

static BOOL _install_one(int resource_id, LPCWSTR file_name, LPCWSTR dir, BOOL *reboot)
{
    WCHAR path[MAX_PATH * 2];
    WCHAR cmd[MAX_PATH * 3];
    swprintf(path, MAX_PATH * 2, L"%s\\%s", dir, file_name);
    HANDLE lock = _extract_resource(resource_id, path);
    if (lock == INVALID_HANDLE_VALUE)
    {
        return FALSE;
    }
    swprintf(cmd, MAX_PATH * 3, L"\"%s\" /exenoui /qn /norestart", path);
    DWORD code = 1;
    BOOL ok = _run_and_wait(cmd, &code) && _is_success(code, reboot);
    log_write(L"installer %s exit code %lu", file_name, code);
    CloseHandle(lock);
    DeleteFileW(path);
    return ok;
}

DWORD drivers_install_elevated(void)
{
    struct driver_status st;
    drivers_query(&st);

    WCHAR temp[MAX_PATH];
    WCHAR dir[MAX_PATH * 2];
    GetTempPathW(MAX_PATH, temp);
    swprintf(dir, MAX_PATH * 2, L"%sStadiaPad-setup-%lu", temp, GetCurrentProcessId());
    CreateDirectoryW(dir, NULL);

    BOOL reboot = FALSE;
    BOOL ok = TRUE;

    if (!st.vigem_installed)
    {
        ok = _install_one(IDR_VIGEM_SETUP, L"ViGEmBus_setup.exe", dir, &reboot) && ok;
    }
    if (!st.hidhide_installed)
    {
        ok = _install_one(IDR_HIDHIDE_SETUP, L"HidHide_setup.exe", dir, &reboot) && ok;
        /* The HidHide filter only attaches to device stacks after a restart. */
        reboot = TRUE;
    }

    RemoveDirectoryW(dir);

    if (!ok)
    {
        return DRV_RESULT_FAILED;
    }
    return reboot ? DRV_RESULT_REBOOT : DRV_RESULT_OK;
}

/* Finds MSI product codes by display name in the 64- and 32-bit uninstall registry views. */
static BOOL _uninstall_by_display_name(LPCWSTR display_name, BOOL *found, BOOL *reboot)
{
    static const REGSAM views[2] = {KEY_WOW64_64KEY, KEY_WOW64_32KEY};
    BOOL ok = TRUE;

    for (int v = 0; v < 2; v++)
    {
        HKEY root;
        if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall", 0,
                          KEY_READ | views[v], &root) != ERROR_SUCCESS)
        {
            continue;
        }
        WCHAR sub[256];
        for (DWORD i = 0;; i++)
        {
            DWORD sub_len = 256;
            if (RegEnumKeyExW(root, i, sub, &sub_len, NULL, NULL, NULL, NULL) != ERROR_SUCCESS)
            {
                break;
            }
            if (sub[0] != L'{')
            {
                continue;
            }
            WCHAR name[256] = {0};
            DWORD name_size = sizeof(name) - sizeof(WCHAR);
            if (RegGetValueW(root, sub, L"DisplayName", RRF_RT_REG_SZ, NULL, name, &name_size) != ERROR_SUCCESS)
            {
                continue;
            }
            if (_wcsicmp(name, display_name) != 0)
            {
                continue;
            }
            *found = TRUE;
            WCHAR cmd[512];
            swprintf(cmd, 512, L"msiexec.exe /x %s /qn /norestart", sub);
            DWORD code = 1;
            BOOL ran = _run_and_wait(cmd, &code);
            log_write(L"uninstall %s %s -> ran=%d code %lu", display_name, sub, ran, code);
            if (!ran || !(_is_success(code, reboot) || code == ERROR_UNKNOWN_PRODUCT))
            {
                ok = FALSE;
            }
        }
        RegCloseKey(root);
    }
    return ok;
}

DWORD drivers_uninstall_elevated(void)
{
    BOOL reboot = FALSE, found = FALSE;
    BOOL ok = _uninstall_by_display_name(HIDHIDE_DISPLAY_NAME, &found, &reboot);
    ok = _uninstall_by_display_name(VIGEM_DISPLAY_NAME, &found, &reboot) && ok;
    if (!ok)
    {
        return DRV_RESULT_FAILED;
    }
    return found ? DRV_RESULT_REBOOT : DRV_RESULT_OK;
}

BOOL drivers_reboot(void)
{
    HANDLE token;
    if (OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &token))
    {
        TOKEN_PRIVILEGES tp = {0};
        tp.PrivilegeCount = 1;
        tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
        if (LookupPrivilegeValueW(NULL, SE_SHUTDOWN_NAME, &tp.Privileges[0].Luid))
        {
            AdjustTokenPrivileges(token, FALSE, &tp, 0, NULL, NULL);
        }
        CloseHandle(token);
    }
    return ExitWindowsEx(EWX_REBOOT, SHTDN_REASON_MAJOR_APPLICATION | SHTDN_REASON_MINOR_INSTALLATION |
                                         SHTDN_REASON_FLAG_PLANNED);
}
