/*
 * hidhide.c -- Direct control of the HidHide filter driver via its control device (\\.\HidHide).
 */

#include "hidhide.h"

#include <windows.h>
#include <winioctl.h>
#include <wchar.h>
#include <stdlib.h>

/* From HidHide Shared/HidHideIoctlContract.h (MIT, (c) Eric Korff de Gidts) */
#define HH_IOCTL_DEVICE_TYPE 32769
#define IOCTL_GET_WHITELIST CTL_CODE(HH_IOCTL_DEVICE_TYPE, 2048, METHOD_BUFFERED, FILE_READ_DATA)
#define IOCTL_SET_WHITELIST CTL_CODE(HH_IOCTL_DEVICE_TYPE, 2049, METHOD_BUFFERED, FILE_READ_DATA)
#define IOCTL_GET_BLACKLIST CTL_CODE(HH_IOCTL_DEVICE_TYPE, 2050, METHOD_BUFFERED, FILE_READ_DATA)
#define IOCTL_SET_BLACKLIST CTL_CODE(HH_IOCTL_DEVICE_TYPE, 2051, METHOD_BUFFERED, FILE_READ_DATA)
#define IOCTL_GET_ACTIVE CTL_CODE(HH_IOCTL_DEVICE_TYPE, 2052, METHOD_BUFFERED, FILE_READ_DATA)
#define IOCTL_SET_ACTIVE CTL_CODE(HH_IOCTL_DEVICE_TYPE, 2053, METHOD_BUFFERED, FILE_READ_DATA)
#define IOCTL_GET_WLINVERSE CTL_CODE(HH_IOCTL_DEVICE_TYPE, 2054, METHOD_BUFFERED, FILE_READ_DATA)

#define HH_CONTROL_DEVICE L"\\\\.\\HidHide"

static HANDLE _open(void)
{
    return CreateFileW(HH_CONTROL_DEVICE, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                       NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
}

BOOL hidhide_available(void)
{
    HANDLE h = _open();
    if (h == INVALID_HANDLE_VALUE)
    {
        return FALSE;
    }
    CloseHandle(h);
    return TRUE;
}

/* ---- multi-string helpers (REG_MULTI_SZ style: "a\0b\0\0") ---- */

/* Returns allocated multi-string (always at least "\0\0"), length in WCHARs via *len (incl. final terminator). */
static WCHAR *_get_list(HANDLE h, DWORD ioctl, DWORD *len)
{
    DWORD needed = 0;
    if (!DeviceIoControl(h, ioctl, NULL, 0, NULL, 0, &needed, NULL))
    {
        return NULL;
    }
    DWORD cch = needed / sizeof(WCHAR);
    WCHAR *buf = (WCHAR *)calloc(cch + 2, sizeof(WCHAR));
    if (buf == NULL)
    {
        return NULL;
    }
    if (needed > 0 && !DeviceIoControl(h, ioctl, NULL, 0, buf, needed, &needed, NULL))
    {
        free(buf);
        return NULL;
    }
    /* normalise: make sure the list ends with a double terminator */
    DWORD i = 0;
    while (i < cch && buf[i] != 0)
    {
        i += (DWORD)wcslen(buf + i) + 1;
    }
    if (i == 0)
    {
        buf[0] = 0;
        buf[1] = 0;
        *len = 2;
    }
    else
    {
        buf[i] = 0;
        *len = i + 1;
    }
    return buf;
}

static BOOL _set_list(HANDLE h, DWORD ioctl, const WCHAR *list, DWORD len)
{
    DWORD needed = 0;
    return DeviceIoControl(h, ioctl, (LPVOID)list, len * sizeof(WCHAR), NULL, 0, &needed, NULL);
}

static BOOL _list_contains(const WCHAR *list, LPCWSTR item)
{
    for (const WCHAR *p = list; *p != 0; p += wcslen(p) + 1)
    {
        if (_wcsicmp(p, item) == 0)
        {
            return TRUE;
        }
    }
    return FALSE;
}

static WCHAR *_list_append(const WCHAR *list, DWORD len, LPCWSTR item, DWORD *new_len)
{
    DWORD item_len = (DWORD)wcslen(item) + 1;
    DWORD used = (list[0] == 0) ? 0 : len - 1; /* drop final terminator (or the empty "\0\0") */
    WCHAR *out = (WCHAR *)calloc(used + item_len + 2, sizeof(WCHAR));
    if (out == NULL)
    {
        return NULL;
    }
    memcpy(out, list, used * sizeof(WCHAR));
    memcpy(out + used, item, item_len * sizeof(WCHAR));
    *new_len = used + item_len + 1;
    return out;
}

static BOOL _icontains(LPCWSTR haystack, LPCWSTR needle)
{
    size_t hl = wcslen(haystack), nl = wcslen(needle);
    if (nl == 0 || nl > hl)
    {
        return nl == 0;
    }
    for (size_t i = 0; i + nl <= hl; i++)
    {
        if (_wcsnicmp(haystack + i, needle, nl) == 0)
        {
            return TRUE;
        }
    }
    return FALSE;
}

typedef BOOL (*_match_fn)(LPCWSTR entry, const void *ctx);

/* Rebuilds the list without matching entries. Returns new list, sets *changed. */
static WCHAR *_list_filter(const WCHAR *list, DWORD len, _match_fn match, const void *ctx, DWORD *new_len,
                           BOOL *changed)
{
    WCHAR *out = (WCHAR *)calloc(len + 2, sizeof(WCHAR));
    if (out == NULL)
    {
        return NULL;
    }
    DWORD pos = 0;
    *changed = FALSE;
    for (const WCHAR *p = list; *p != 0; p += wcslen(p) + 1)
    {
        if (match(p, ctx))
        {
            *changed = TRUE;
            continue;
        }
        size_t l = wcslen(p) + 1;
        memcpy(out + pos, p, l * sizeof(WCHAR));
        pos += (DWORD)l;
    }
    out[pos] = 0;
    *new_len = pos == 0 ? 2 : pos + 1;
    return out;
}

/* ---- path conversion: C:\dir\app.exe -> \Device\HarddiskVolume3\dir\app.exe ---- */

static BOOL _to_full_image_name(LPCWSTR dos_path, WCHAR *out, DWORD cch)
{
    WCHAR mount[MAX_PATH];
    WCHAR volume[MAX_PATH];
    WCHAR device[MAX_PATH];

    if (!GetVolumePathNameW(dos_path, mount, MAX_PATH))
    {
        return FALSE;
    }
    if (!GetVolumeNameForVolumeMountPointW(mount, volume, MAX_PATH))
    {
        return FALSE;
    }
    /* volume = \\?\Volume{GUID}\  -> Volume{GUID} */
    size_t vl = wcslen(volume);
    if (vl < 6 || wcsncmp(volume, L"\\\\?\\", 4) != 0)
    {
        return FALSE;
    }
    if (volume[vl - 1] == L'\\')
    {
        volume[vl - 1] = 0;
    }
    if (QueryDosDeviceW(volume + 4, device, MAX_PATH) == 0)
    {
        return FALSE;
    }
    LPCWSTR rest = dos_path + wcslen(mount);
    return swprintf(out, cch, L"%s\\%s", device, rest) > 0;
}

static BOOL _self_full_image_name(WCHAR *out, DWORD cch)
{
    WCHAR path[MAX_PATH * 2];
    DWORD n = GetModuleFileNameW(NULL, path, MAX_PATH * 2);
    if (n == 0 || n >= MAX_PATH * 2)
    {
        return FALSE;
    }
    return _to_full_image_name(path, out, cch);
}

static BOOL _eq_match(LPCWSTR entry, const void *ctx)
{
    return _wcsicmp(entry, (LPCWSTR)ctx) == 0;
}

static BOOL _substr_match(LPCWSTR entry, const void *ctx)
{
    for (const LPCWSTR *s = (const LPCWSTR *)ctx; *s != NULL; s++)
    {
        if (_icontains(entry, *s))
        {
            return TRUE;
        }
    }
    return FALSE;
}

/* ---- whitelist ---- */

static BOOL _whitelist_add(LPCWSTR full_image)
{
    HANDLE h = _open();
    if (h == INVALID_HANDLE_VALUE)
    {
        return FALSE;
    }
    BOOL ok = FALSE;
    DWORD len = 0, new_len = 0;
    WCHAR *list = _get_list(h, IOCTL_GET_WHITELIST, &len);
    if (list != NULL)
    {
        if (_list_contains(list, full_image))
        {
            ok = TRUE;
        }
        else
        {
            WCHAR *nl = _list_append(list, len, full_image, &new_len);
            if (nl != NULL)
            {
                ok = _set_list(h, IOCTL_SET_WHITELIST, nl, new_len);
                free(nl);
            }
        }
        free(list);
    }
    CloseHandle(h);
    return ok;
}

static BOOL _whitelist_remove(LPCWSTR full_image)
{
    HANDLE h = _open();
    if (h == INVALID_HANDLE_VALUE)
    {
        return FALSE;
    }
    BOOL ok = FALSE, changed = FALSE;
    DWORD len = 0, new_len = 0;
    WCHAR *list = _get_list(h, IOCTL_GET_WHITELIST, &len);
    if (list != NULL)
    {
        WCHAR *nl = _list_filter(list, len, _eq_match, full_image, &new_len, &changed);
        if (nl != NULL)
        {
            ok = changed ? _set_list(h, IOCTL_SET_WHITELIST, nl, new_len) : TRUE;
            free(nl);
        }
        free(list);
    }
    CloseHandle(h);
    return ok;
}

BOOL hidhide_whitelist_self(void)
{
    WCHAR full[MAX_PATH * 2];
    if (!_self_full_image_name(full, MAX_PATH * 2))
    {
        return FALSE;
    }

    /* In "inverse" mode the whitelist acts as a blacklist of applications: then we must NOT be on it. */
    HANDLE h = _open();
    if (h == INVALID_HANDLE_VALUE)
    {
        return FALSE;
    }
    BOOLEAN inverse = FALSE;
    DWORD needed = 0;
    DeviceIoControl(h, IOCTL_GET_WLINVERSE, NULL, 0, &inverse, sizeof(inverse), &needed, NULL);
    CloseHandle(h);

    return inverse ? _whitelist_remove(full) : _whitelist_add(full);
}

BOOL hidhide_unwhitelist_self(void)
{
    WCHAR full[MAX_PATH * 2];
    if (!_self_full_image_name(full, MAX_PATH * 2))
    {
        return FALSE;
    }
    return _whitelist_remove(full);
}

BOOL hidhide_whitelist_remove_path(LPCWSTR dos_path)
{
    WCHAR full[MAX_PATH * 2];
    if (!_to_full_image_name(dos_path, full, MAX_PATH * 2))
    {
        return FALSE;
    }
    return _whitelist_remove(full);
}

/* ---- blacklist ---- */

BOOL hidhide_blacklist_add(LPCWSTR instance_id)
{
    HANDLE h = _open();
    if (h == INVALID_HANDLE_VALUE)
    {
        return FALSE;
    }
    BOOL ok = FALSE;
    DWORD len = 0, new_len = 0;
    WCHAR *list = _get_list(h, IOCTL_GET_BLACKLIST, &len);
    if (list != NULL)
    {
        if (_list_contains(list, instance_id))
        {
            ok = TRUE;
        }
        else
        {
            WCHAR *nl = _list_append(list, len, instance_id, &new_len);
            if (nl != NULL)
            {
                ok = _set_list(h, IOCTL_SET_BLACKLIST, nl, new_len);
                free(nl);
            }
        }
        free(list);
    }
    CloseHandle(h);
    return ok;
}

BOOL hidhide_blacklist_remove_matching(const LPCWSTR *substrings)
{
    HANDLE h = _open();
    if (h == INVALID_HANDLE_VALUE)
    {
        return FALSE;
    }
    BOOL ok = FALSE, changed = FALSE;
    DWORD len = 0, new_len = 0;
    WCHAR *list = _get_list(h, IOCTL_GET_BLACKLIST, &len);
    if (list != NULL)
    {
        WCHAR *nl = _list_filter(list, len, _substr_match, substrings, &new_len, &changed);
        if (nl != NULL)
        {
            ok = changed ? _set_list(h, IOCTL_SET_BLACKLIST, nl, new_len) : TRUE;
            free(nl);
        }
        free(list);
    }
    CloseHandle(h);
    return ok;
}

/* ---- active flag ---- */

BOOL hidhide_get_active(BOOL *active)
{
    HANDLE h = _open();
    if (h == INVALID_HANDLE_VALUE)
    {
        return FALSE;
    }
    BOOLEAN value = FALSE;
    DWORD needed = 0;
    BOOL ok = DeviceIoControl(h, IOCTL_GET_ACTIVE, NULL, 0, &value, sizeof(value), &needed, NULL);
    CloseHandle(h);
    if (ok)
    {
        *active = value != FALSE;
    }
    return ok;
}

BOOL hidhide_set_active(BOOL active)
{
    HANDLE h = _open();
    if (h == INVALID_HANDLE_VALUE)
    {
        return FALSE;
    }
    BOOLEAN value = active ? TRUE : FALSE;
    DWORD needed = 0;
    BOOL ok = DeviceIoControl(h, IOCTL_SET_ACTIVE, &value, sizeof(value), NULL, 0, &needed, NULL);
    CloseHandle(h);
    return ok;
}
