/*
 * hidhide.h -- Direct control of the HidHide filter driver via its control device (\\.\HidHide).
 *
 * The IOCTL contract is taken from HidHide (Shared/HidHideIoctlContract.h, MIT license).
 * The control device is accessible to normal (non-admin) users.
 */

#ifndef HIDHIDE_H
#define HIDHIDE_H

#include <wtypes.h>

/* TRUE when the HidHide driver is loaded and its control device can be opened. */
BOOL hidhide_available(void);

/* Makes sure this executable is allowed to see hidden devices. */
BOOL hidhide_whitelist_self(void);

/* Removes this executable from the whitelist. */
BOOL hidhide_unwhitelist_self(void);

/* Removes the given DOS path (e.g. C:\SOFT\Stadia\stadia-vigem-x64.exe) from the whitelist. */
BOOL hidhide_whitelist_remove_path(LPCWSTR dos_path);

/* Adds a device instance path to the list of hidden devices. */
BOOL hidhide_blacklist_add(LPCWSTR instance_id);

/* Removes every hidden device whose instance path contains any of the (NULL-terminated) substrings. */
BOOL hidhide_blacklist_remove_matching(const LPCWSTR *substrings);

/* Global hiding switch. */
BOOL hidhide_get_active(BOOL *active);
BOOL hidhide_set_active(BOOL active);

#endif /* HIDHIDE_H */
