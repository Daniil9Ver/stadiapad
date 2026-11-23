/*
 * drivers.h -- Detection, installation and removal of the ViGEmBus and HidHide drivers.
 */

#ifndef DRIVERS_H
#define DRIVERS_H

#include <wtypes.h>

struct driver_status
{
    BOOL vigem_installed;   /* ViGEmBus service registered */
    BOOL hidhide_installed; /* HidHide service registered */
    BOOL hidhide_ready;     /* HidHide control device reachable (false right after install until reboot) */
};

void drivers_query(struct driver_status *status);

/* Command line switches handled by the elevated helper instance of this same exe. */
#define ARG_INSTALL_DRIVERS L"--install-drivers"
#define ARG_UNINSTALL_DRIVERS L"--uninstall-drivers"
/* Used by the uninstaller: removes this exe from the HidHide whitelist and un-hides Stadia devices. */
#define ARG_CLEANUP L"--cleanup"

/* Result codes of the elevated helper. */
#define DRV_RESULT_OK 0
#define DRV_RESULT_REBOOT 3010
#define DRV_RESULT_CANCELLED 1223
#define DRV_RESULT_FAILED 1

/* Runs this exe elevated with the given switch (shows UAC) and waits. Returns a DRV_RESULT_* code. */
DWORD drivers_run_elevated(HWND owner, LPCWSTR arg);

/* Executed inside the elevated helper. Return value becomes the process exit code. */
DWORD drivers_install_elevated(void);
DWORD drivers_uninstall_elevated(void);

/* Asks Windows to reboot now. */
BOOL drivers_reboot(void);

#endif /* DRIVERS_H */
