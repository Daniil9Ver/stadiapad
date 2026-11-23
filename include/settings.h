/*
 * settings.h -- Per-user settings (HKCU\Software\StadiaPad).
 */

#ifndef SETTINGS_H
#define SETTINGS_H

#include <wtypes.h>

BOOL settings_get_hide_controller(void);
void settings_set_hide_controller(BOOL value);

/* Removes the HKCU Run entry that older builds created. */
void settings_remove_legacy_autostart(void);

#endif /* SETTINGS_H */
