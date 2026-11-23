/*
 * settings.c -- Per-user settings (HKCU\Software\StadiaPad) and Windows autostart.
 */

#include "settings.h"

#include <windows.h>
#include <wchar.h>
#include <stdio.h>

#include "log.h"

#pragma comment(lib, "advapi32.lib")

#define SETTINGS_KEY L"Software\\StadiaPad"
#define RUN_KEY L"Software\\Microsoft\\Windows\\CurrentVersion\\Run"
#define RUN_VALUE L"StadiaPad"

static DWORD _get_dword(LPCWSTR name, DWORD def)
{
    DWORD value = def, size = sizeof(value);
    if (RegGetValueW(HKEY_CURRENT_USER, SETTINGS_KEY, name, RRF_RT_REG_DWORD, NULL, &value, &size) != ERROR_SUCCESS)
    {
        return def;
    }
    return value;
}

static void _set_dword(LPCWSTR name, DWORD value)
{
    RegSetKeyValueW(HKEY_CURRENT_USER, SETTINGS_KEY, name, REG_DWORD, &value, sizeof(value));
}

BOOL settings_get_hide_controller(void)
{
    return _get_dword(L"HideController", 1) != 0;
}

void settings_set_hide_controller(BOOL value)
{
    _set_dword(L"HideController", value ? 1 : 0);
}

void settings_remove_legacy_autostart(void)
{
    LSTATUS r = RegDeleteKeyValueW(HKEY_CURRENT_USER, RUN_KEY, RUN_VALUE);
    if (r == ERROR_SUCCESS)
    {
        log_write(L"removed legacy autostart entry");
    }
    RegDeleteKeyValueW(HKEY_CURRENT_USER, SETTINGS_KEY, L"FirstRunDone");
}
