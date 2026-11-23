/*
 * log.c -- Tiny append-only log in %LOCALAPPDATA%\StadiaPad\StadiaPad.log (UTF-8, rotated at 512 KB).
 */

#include "log.h"

#include <windows.h>
#include <shlobj.h>
#include <stdarg.h>
#include <stdio.h>
#include <wchar.h>

#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")

#define LOG_MAX_SIZE (512 * 1024)

static SRWLOCK log_lock = SRWLOCK_INIT;

static BOOL _log_path(WCHAR *path, DWORD cch)
{
    PWSTR base = NULL;
    if (FAILED(SHGetKnownFolderPath(&FOLDERID_LocalAppData, 0, NULL, &base)))
    {
        return FALSE;
    }
    swprintf(path, cch, L"%s\\StadiaPad", base);
    CreateDirectoryW(path, NULL);
    swprintf(path, cch, L"%s\\StadiaPad\\StadiaPad.log", base);
    CoTaskMemFree(base);
    return TRUE;
}

void log_write(const wchar_t *fmt, ...)
{
    WCHAR path[MAX_PATH * 2];
    WCHAR msg[1024];
    WCHAR line[1200];
    char utf8[3600];

    va_list args;
    va_start(args, fmt);
    vswprintf(msg, 1024, fmt, args);
    va_end(args);

    SYSTEMTIME t;
    GetLocalTime(&t);
    swprintf(line, 1200, L"%04u-%02u-%02u %02u:%02u:%02u.%03u [%lu] %s\r\n", t.wYear, t.wMonth, t.wDay, t.wHour,
             t.wMinute, t.wSecond, t.wMilliseconds, GetCurrentProcessId(), msg);
    int n = WideCharToMultiByte(CP_UTF8, 0, line, -1, utf8, sizeof(utf8), NULL, NULL);
    if (n <= 1)
    {
        return;
    }

    AcquireSRWLockExclusive(&log_lock);
    if (_log_path(path, MAX_PATH * 2))
    {
        WIN32_FILE_ATTRIBUTE_DATA info;
        if (GetFileAttributesExW(path, GetFileExInfoStandard, &info) && info.nFileSizeLow > LOG_MAX_SIZE)
        {
            WCHAR old[MAX_PATH * 2];
            swprintf(old, MAX_PATH * 2, L"%s.old", path);
            MoveFileExW(path, old, MOVEFILE_REPLACE_EXISTING);
        }
        HANDLE f = CreateFileW(path, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_ALWAYS,
                               FILE_ATTRIBUTE_NORMAL, NULL);
        if (f != INVALID_HANDLE_VALUE)
        {
            DWORD written = 0;
            WriteFile(f, utf8, (DWORD)(n - 1), &written, NULL);
            CloseHandle(f);
        }
    }
    ReleaseSRWLockExclusive(&log_lock);
}
