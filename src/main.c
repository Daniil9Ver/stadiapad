/*
 * main.c -- StadiaPad: Stadia controller -> virtual Xbox 360 controller, in one tray app.
 *
 * Based on Stadia-ViGEm (MIT, walkco / grayver). Adds:
 *  - first-run installation of the embedded ViGEmBus + HidHide drivers,
 *  - automatic HidHide configuration (whitelist self, hide Stadia over USB and Bluetooth,
 *    un-hide on exit so the controller is usable when StadiaPad isn't running),
 *  - install / uninstall drivers from the tray menu, menu in English. Started only manually (no autostart).
 */

#include <stdio.h>
#include <string.h>
#include <wchar.h>
#include <windows.h>
#include <shellapi.h>

#include <ViGEm/Client.h>

#include "tray.h"
#include "hid.h"
#include "stadia.h"
#include "utils.h"
#include "hidhide.h"
#include "drivers.h"
#include "settings.h"
#include "log.h"

#pragma comment(lib, "setupapi.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "user32.lib")

#define APP_NAME L"StadiaPad"
#define MAX_ACTIVE_DEVICE_COUNT 4

/* tray_post() codes */
#define POST_DEVICES_CHANGED 1

struct active_device
{
    struct hid_device *src_device;
    struct stadia_controller *controller;
    PVIGEM_TARGET tgt_device;
    XUSB_REPORT tgt_report;
    BOOL bluetooth;
};

static int active_device_count = 0;
static struct active_device *active_devices[MAX_ACTIVE_DEVICE_COUNT];
static SRWLOCK active_devices_lock = SRWLOCK_INIT;

static PVIGEM_CLIENT vigem_client = NULL;
static BOOL vigem_connected = FALSE;
static VIGEM_ERROR vigem_last_error = VIGEM_ERROR_NONE;

static struct driver_status drivers;
static BOOL hiding_enabled = TRUE;
static BOOL drivers_need_reboot = FALSE; /* set after install/uninstall in this session */

static LPWSTR stadia_hw_path_filters[3] = {STADIA_USB_HW_FILTER, STADIA_BLT_HW_FILTER, NULL};
static LPCWSTR stadia_instance_filters[3] = {L"VID_18D1&PID_9400", L"VID&0218D1_PID&9400", NULL};

/* ---------------------------------------------------------------- tray menu */

#define MENU_MAX 24
#define TEXT_MAX 128

static struct tray_menu menu_items[MENU_MAX];
static WCHAR menu_texts[MENU_MAX][TEXT_MAX];
static WCHAR tray_tip[128];

static struct tray tray = {
    .icon = L"APP_ICON",
    .tip = tray_tip,
    .menu = menu_items,
};

static void toggle_hide_cb(struct tray_menu *item);
static void refresh_cb(struct tray_menu *item);
static void install_drivers_cb(struct tray_menu *item);
static void reboot_cb(struct tray_menu *item);
static void uninstall_drivers_cb(struct tray_menu *item);
static void quit_cb(struct tray_menu *item);

static int _menu_add(int index, LPCWSTR text, BOOLEAN disabled, BOOLEAN checked, void (*cb)(struct tray_menu *))
{
    if (index >= MENU_MAX - 1)
    {
        return index;
    }
    wcsncpy_s(menu_texts[index], TEXT_MAX, text, _TRUNCATE);
    memset(&menu_items[index], 0, sizeof(struct tray_menu));
    menu_items[index].text = menu_texts[index];
    menu_items[index].disabled = disabled;
    menu_items[index].checked = checked;
    menu_items[index].cb = cb;
    return index + 1;
}

static void rebuild_tray_menu(void)
{
    int i = 0;
    WCHAR line[TEXT_MAX];

    AcquireSRWLockShared(&active_devices_lock);
    int count = active_device_count;
    if (count == 0)
    {
        i = _menu_add(i, L"Stadia controller not connected", TRUE, FALSE, NULL);
    }
    for (int d = 0; d < count; d++)
    {
        swprintf(line, TEXT_MAX, L"Controller %d: Stadia (%s)%s", d + 1,
                 active_devices[d]->bluetooth ? L"Bluetooth" : L"USB",
                 vigem_connected ? L" → Xbox 360" : L"");
        i = _menu_add(i, line, TRUE, FALSE, NULL);
    }
    ReleaseSRWLockShared(&active_devices_lock);

    BOOL drivers_missing = !drivers.vigem_installed || !drivers.hidhide_installed;
    i = _menu_add(i, drivers_missing ? L"Drivers: not installed" : L"Drivers: installed", TRUE, FALSE, NULL);
    if (drivers_need_reboot)
    {
        i = _menu_add(i, L"⚠ Restart required", TRUE, FALSE, NULL);
    }
    else if (!drivers_missing && !vigem_connected)
    {
        i = _menu_add(i, L"⚠ ViGEmBus unavailable — please restart", TRUE, FALSE, NULL);
    }
    else if (!drivers_missing && !drivers.hidhide_ready)
    {
        i = _menu_add(i, L"⚠ HidHide not active yet — please restart", TRUE, FALSE, NULL);
    }

    i = _menu_add(i, L"-", FALSE, FALSE, NULL);
    i = _menu_add(i, L"Hide original controller", !drivers.hidhide_ready, (BOOLEAN)hiding_enabled,
                  toggle_hide_cb);
    i = _menu_add(i, L"Reconnect", FALSE, FALSE, refresh_cb);
    i = _menu_add(i, L"-", FALSE, FALSE, NULL);
    if (drivers_need_reboot)
    {
        i = _menu_add(i, L"Restart computer…", FALSE, FALSE, reboot_cb);
    }
    else if (drivers_missing)
    {
        i = _menu_add(i, L"Install drivers…", FALSE, FALSE, install_drivers_cb);
    }
    else
    {
        i = _menu_add(i, L"Uninstall drivers…", FALSE, FALSE, uninstall_drivers_cb);
    }
    i = _menu_add(i, L"Exit", FALSE, FALSE, quit_cb);
    memset(&menu_items[i], 0, sizeof(struct tray_menu)); /* terminator */

    if (count == 0)
    {
        swprintf(tray_tip, 128, L"%s — no controller connected", APP_NAME);
    }
    else
    {
        swprintf(tray_tip, 128, L"%s — connected: %d", APP_NAME, count);
    }
}

static void update_tray(void)
{
    rebuild_tray_menu();
    tray_update(&tray);
}

static void notify(UINT type, LPCWSTR text)
{
    tray_show_notification(type, (LPTSTR)APP_NAME, (LPTSTR)text);
}

/* ---------------------------------------------------------------- HidHide */

/* Hides all currently present Stadia HID devices from other applications (if enabled). */
static void apply_hiding(struct hid_device_info *present)
{
    if (!drivers.hidhide_ready)
    {
        return;
    }
    if (!hiding_enabled)
    {
        hidhide_blacklist_remove_matching(stadia_instance_filters);
        return;
    }

    hidhide_whitelist_self();
    for (struct hid_device_info *cur = present; cur != NULL; cur = cur->next)
    {
        if (cur->instance_id != NULL)
        {
            hidhide_blacklist_add(cur->instance_id);
        }
    }
    BOOL active = FALSE;
    if (hidhide_get_active(&active) && !active)
    {
        hidhide_set_active(TRUE);
    }
}

static void remove_hiding(void)
{
    if (drivers.hidhide_ready)
    {
        hidhide_blacklist_remove_matching(stadia_instance_filters);
    }
}

/* ---------------------------------------------------------------- ViGEm */

static void CALLBACK x360_notification_cb(PVIGEM_CLIENT client, PVIGEM_TARGET target, UCHAR large_motor,
                                          UCHAR small_motor, UCHAR led_number, LPVOID user_data);

static void vigem_try_connect(void)
{
    if (vigem_connected)
    {
        return;
    }
    if (vigem_client == NULL)
    {
        vigem_client = vigem_alloc();
        if (vigem_client == NULL)
        {
            return;
        }
    }
    vigem_last_error = vigem_connect(vigem_client);
    vigem_connected = VIGEM_SUCCESS(vigem_last_error);
}

static void attach_target(struct active_device *dev)
{
    if (!vigem_connected || dev->tgt_device != NULL)
    {
        return;
    }
    dev->tgt_device = vigem_target_x360_alloc();
    if (!VIGEM_SUCCESS(vigem_target_add(vigem_client, dev->tgt_device)))
    {
        vigem_target_free(dev->tgt_device);
        dev->tgt_device = NULL;
        return;
    }
    XUSB_REPORT_INIT(&dev->tgt_report);
    vigem_target_x360_register_notification(vigem_client, dev->tgt_device, x360_notification_cb, (LPVOID)dev);
}

static void detach_target(struct active_device *dev)
{
    if (dev->tgt_device == NULL)
    {
        return;
    }
    vigem_target_x360_unregister_notification(dev->tgt_device);
    vigem_target_remove(vigem_client, dev->tgt_device);
    vigem_target_free(dev->tgt_device);
    dev->tgt_device = NULL;
}

/* ---------------------------------------------------------------- devices */

SHORT FORCEINLINE _map_byte_to_short(BYTE value, BOOL inverted)
{
    INT centered = (INT)value - 128;
    if (centered < -127)
    {
        centered = -127;
    }
    if (inverted)
    {
        centered = -centered;
    }
    return (SHORT)(32767 * centered / 127);
}

static BOOL add_device(LPWSTR path)
{
    if (active_device_count == MAX_ACTIVE_DEVICE_COUNT)
    {
        notify(NT_TRAY_WARNING, L"Limit reached: up to 4 controllers");
        return FALSE;
    }

    struct hid_device *device = hid_open_device(path, TRUE, FALSE);
    if (device == NULL)
    {
        if (hid_reenable_device(path))
        {
            device = hid_open_device(path, TRUE, FALSE);
        }
        if (device == NULL)
        {
            device = hid_open_device(path, TRUE, TRUE);
        }
    }
    if (device == NULL)
    {
        notify(NT_TRAY_WARNING, L"Could not open the Stadia controller");
        return FALSE;
    }

    struct active_device *dev = (struct active_device *)calloc(1, sizeof(struct active_device));
    if (dev == NULL)
    {
        hid_close_device(device);
        hid_free_device(device);
        return FALSE;
    }
    dev->src_device = device;
    dev->bluetooth = _tcsistr(path, STADIA_BLT_HW_FILTER) != NULL;
    log_write(L"device added (%s): %s", dev->bluetooth ? L"bluetooth" : L"usb", path);
    attach_target(dev);

    /* Register before the input thread starts so the first reports find the device. */
    AcquireSRWLockExclusive(&active_devices_lock);
    active_devices[active_device_count++] = dev;
    ReleaseSRWLockExclusive(&active_devices_lock);

    struct stadia_controller *controller = stadia_controller_create(device);
    if (controller == NULL)
    {
        AcquireSRWLockExclusive(&active_devices_lock);
        active_devices[--active_device_count] = NULL;
        ReleaseSRWLockExclusive(&active_devices_lock);
        detach_target(dev);
        hid_close_device(device);
        hid_free_device(device);
        free(dev);
        notify(NT_TRAY_WARNING, L"Failed to initialize the Stadia controller");
        return FALSE;
    }
    AcquireSRWLockExclusive(&active_devices_lock);
    dev->controller = controller;
    ReleaseSRWLockExclusive(&active_devices_lock);
    return TRUE;
}

/* UI thread only. Removes the device at index from the list and releases everything. */
static void remove_device_at(int index)
{
    AcquireSRWLockExclusive(&active_devices_lock);
    struct active_device *dev = active_devices[index];
    if (index < active_device_count - 1)
    {
        memmove(&active_devices[index], &active_devices[index + 1],
                sizeof(struct active_device *) * (active_device_count - index - 1));
    }
    active_devices[--active_device_count] = NULL;
    ReleaseSRWLockExclusive(&active_devices_lock);

    detach_target(dev); /* stops vibration callbacks that reference dev */
    if (dev->controller != NULL)
    {
        stadia_controller_destroy(dev->controller);
    }
    hid_close_device(dev->src_device);
    hid_free_device(dev->src_device);
    free(dev);
}

static void remove_all_devices(void)
{
    while (active_device_count > 0)
    {
        remove_device_at(active_device_count - 1);
    }
}

static void free_device_info_list(struct hid_device_info *list)
{
    while (list != NULL)
    {
        struct hid_device_info *next = list->next;
        hid_free_device_info(list);
        list = next;
    }
}

/* UI thread only. */
static void refresh_devices(void)
{
    struct hid_device_info *present = hid_enumerate(stadia_hw_path_filters);

    /* drop devices that disappeared or whose reader thread died */
    for (int i = active_device_count - 1; i >= 0; i--)
    {
        struct active_device *dev = active_devices[i];
        BOOL found = FALSE;
        for (struct hid_device_info *cur = present; cur != NULL; cur = cur->next)
        {
            if (_wcsicmp(dev->src_device->path, cur->path) == 0)
            {
                found = TRUE;
                break;
            }
        }
        if (!found || (dev->controller != NULL && dev->controller->disconnected))
        {
            remove_device_at(i);
        }
    }

    /* hide before we (and others) open new devices */
    apply_hiding(present);

    for (struct hid_device_info *cur = present; cur != NULL; cur = cur->next)
    {
        BOOL found = FALSE;
        for (int i = 0; i < active_device_count; i++)
        {
            if (_wcsicmp(cur->path, active_devices[i]->src_device->path) == 0)
            {
                found = TRUE;
                break;
            }
        }
        if (!found)
        {
            add_device(cur->path);
        }
    }

    free_device_info_list(present);
    update_tray();
}

static void device_change_cb(UINT op, LPTSTR path)
{
    (void)op;
    (void)path;
    refresh_devices();
}

static void user_message_cb(WPARAM code)
{
    if (code == POST_DEVICES_CHANGED)
    {
        refresh_devices();
    }
}

/* ---------------------------------------------------------------- controller callbacks (worker threads) */

static void stadia_controller_update_cb(struct stadia_controller *controller, struct stadia_state *state)
{
    AcquireSRWLockShared(&active_devices_lock);
    struct active_device *dev = NULL;
    for (int i = 0; i < active_device_count; i++)
    {
        if (active_devices[i]->controller == controller)
        {
            dev = active_devices[i];
            break;
        }
    }
    if (dev != NULL && vigem_connected && dev->tgt_device != NULL)
    {
        XUSB_REPORT *r = &dev->tgt_report;
        r->wButtons = 0;
        r->wButtons |= (state->buttons & STADIA_BUTTON_UP) ? XUSB_GAMEPAD_DPAD_UP : 0;
        r->wButtons |= (state->buttons & STADIA_BUTTON_DOWN) ? XUSB_GAMEPAD_DPAD_DOWN : 0;
        r->wButtons |= (state->buttons & STADIA_BUTTON_LEFT) ? XUSB_GAMEPAD_DPAD_LEFT : 0;
        r->wButtons |= (state->buttons & STADIA_BUTTON_RIGHT) ? XUSB_GAMEPAD_DPAD_RIGHT : 0;
        r->wButtons |= (state->buttons & STADIA_BUTTON_MENU) ? XUSB_GAMEPAD_START : 0;
        r->wButtons |= (state->buttons & STADIA_BUTTON_OPTIONS) ? XUSB_GAMEPAD_BACK : 0;
        r->wButtons |= (state->buttons & STADIA_BUTTON_LS) ? XUSB_GAMEPAD_LEFT_THUMB : 0;
        r->wButtons |= (state->buttons & STADIA_BUTTON_RS) ? XUSB_GAMEPAD_RIGHT_THUMB : 0;
        r->wButtons |= (state->buttons & STADIA_BUTTON_LB) ? XUSB_GAMEPAD_LEFT_SHOULDER : 0;
        r->wButtons |= (state->buttons & STADIA_BUTTON_RB) ? XUSB_GAMEPAD_RIGHT_SHOULDER : 0;
        r->wButtons |= (state->buttons & STADIA_BUTTON_A) ? XUSB_GAMEPAD_A : 0;
        r->wButtons |= (state->buttons & STADIA_BUTTON_B) ? XUSB_GAMEPAD_B : 0;
        r->wButtons |= (state->buttons & STADIA_BUTTON_X) ? XUSB_GAMEPAD_X : 0;
        r->wButtons |= (state->buttons & STADIA_BUTTON_Y) ? XUSB_GAMEPAD_Y : 0;
        r->wButtons |= (state->buttons & STADIA_BUTTON_STADIA_BTN) ? XUSB_GAMEPAD_GUIDE : 0;
        r->bLeftTrigger = state->left_trigger;
        r->bRightTrigger = state->right_trigger;
        r->sThumbLX = _map_byte_to_short(state->left_stick_x, FALSE);
        r->sThumbLY = _map_byte_to_short(state->left_stick_y, TRUE);
        r->sThumbRX = _map_byte_to_short(state->right_stick_x, FALSE);
        r->sThumbRY = _map_byte_to_short(state->right_stick_y, TRUE);
        vigem_target_x360_update(vigem_client, dev->tgt_device, *r);
    }
    ReleaseSRWLockShared(&active_devices_lock);
}

static void stadia_controller_disconnect_cb(struct stadia_controller *controller)
{
    (void)controller;
    tray_post(POST_DEVICES_CHANGED);
}

static void CALLBACK x360_notification_cb(PVIGEM_CLIENT client, PVIGEM_TARGET target, UCHAR large_motor,
                                          UCHAR small_motor, UCHAR led_number, LPVOID user_data)
{
    (void)client;
    (void)target;
    (void)led_number;
    struct active_device *dev = (struct active_device *)user_data;
    AcquireSRWLockShared(&active_devices_lock);
    if (dev->controller != NULL)
    {
        stadia_controller_set_vibration(dev->controller, small_motor, large_motor);
    }
    ReleaseSRWLockShared(&active_devices_lock);
}

/* ---------------------------------------------------------------- drivers install / uninstall */

static void ask_reboot(LPCWSTR message)
{
    if (MessageBoxW(tray_get_window(), message, APP_NAME, MB_YESNO | MB_ICONQUESTION | MB_SETFOREGROUND | MB_TOPMOST) == IDYES)
    {
        if (!drivers_reboot())
        {
            MessageBoxW(tray_get_window(), L"Could not restart the computer. Please restart it manually.",
                        APP_NAME, MB_OK | MB_ICONWARNING | MB_SETFOREGROUND | MB_TOPMOST);
        }
    }
}

/* Returns TRUE if the drivers got installed. */
static BOOL install_drivers(BOOL ask_first)
{
    if (ask_first &&
        MessageBoxW(tray_get_window(),
                    L"The Stadia controller needs two drivers:\n\n"
                    L"  • ViGEmBus — virtual Xbox 360 controller\n"
                    L"  • HidHide — hides the original Stadia from games (no double input)\n\n"
                    L"These are official signed drivers by Nefarius Software Solutions.\n"
                    L"Install them now? Windows will ask for administrator rights.",
                    APP_NAME, MB_YESNO | MB_ICONQUESTION | MB_SETFOREGROUND | MB_TOPMOST) != IDYES)
    {
        return FALSE;
    }

    DWORD result = drivers_run_elevated(tray_get_window(), ARG_INSTALL_DRIVERS);
    drivers_query(&drivers);
    log_write(L"install result %lu; vigem=%d hidhide=%d ready=%d", result, drivers.vigem_installed,
              drivers.hidhide_installed, drivers.hidhide_ready);
    vigem_try_connect();

    if (result == DRV_RESULT_CANCELLED)
    {
        notify(NT_TRAY_WARNING, L"Driver installation cancelled");
        return FALSE;
    }
    if (result != DRV_RESULT_OK && result != DRV_RESULT_REBOOT)
    {
        MessageBoxW(tray_get_window(), L"Failed to install the drivers.", APP_NAME, MB_OK | MB_ICONERROR | MB_SETFOREGROUND | MB_TOPMOST);
        return FALSE;
    }

    if (result == DRV_RESULT_REBOOT || !drivers.hidhide_ready)
    {
        drivers_need_reboot = TRUE;
        update_tray();
        ask_reboot(L"Drivers installed.\n\nA restart is required for HidHide to work.\n"
                   L"Restart the computer now?\n\n"
                   L"After the restart, launch StadiaPad again.");
    }
    return TRUE;
}

static void install_drivers_cb(struct tray_menu *item)
{
    (void)item;
    install_drivers(FALSE);
    refresh_devices();
}

static void reboot_cb(struct tray_menu *item)
{
    (void)item;
    ask_reboot(L"Restart the computer now?");
}

static void uninstall_drivers_cb(struct tray_menu *item)
{
    (void)item;
    if (MessageBoxW(tray_get_window(),
                    L"Uninstall the ViGEmBus and HidHide drivers?\n\n"
                    L"The Stadia controller will stop working as an Xbox controller until the drivers are installed again.\n"
                    L"Windows will ask for administrator rights.",
                    APP_NAME, MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2 | MB_SETFOREGROUND | MB_TOPMOST) != IDYES)
    {
        return;
    }

    remove_all_devices();
    remove_hiding();
    if (drivers.hidhide_ready)
    {
        hidhide_unwhitelist_self();
    }
    if (vigem_connected)
    {
        vigem_disconnect(vigem_client);
        vigem_connected = FALSE;
    }

    DWORD result = drivers_run_elevated(tray_get_window(), ARG_UNINSTALL_DRIVERS);
    log_write(L"uninstall result %lu", result);
    if (result == DRV_RESULT_CANCELLED)
    {
        vigem_try_connect();
        refresh_devices();
        return;
    }
    if (result != DRV_RESULT_OK && result != DRV_RESULT_REBOOT)
    {
        MessageBoxW(tray_get_window(), L"Failed to uninstall the drivers.", APP_NAME, MB_OK | MB_ICONERROR | MB_SETFOREGROUND | MB_TOPMOST);
        vigem_try_connect();
        refresh_devices();
        return;
    }

    drivers_query(&drivers);
    drivers_need_reboot = TRUE;
    update_tray();
    ask_reboot(L"Drivers uninstalled. A restart is required to finish.\nRestart the computer now?");
}

/* ---------------------------------------------------------------- menu callbacks */

static void toggle_hide_cb(struct tray_menu *item)
{
    (void)item;
    hiding_enabled = !hiding_enabled;
    settings_set_hide_controller(hiding_enabled);
    refresh_devices();
}


static void refresh_cb(struct tray_menu *item)
{
    (void)item;
    drivers_query(&drivers);
    vigem_try_connect();
    for (int i = 0; i < active_device_count; i++)
    {
        attach_target(active_devices[i]);
    }
    refresh_devices();
}

static void quit_cb(struct tray_menu *item)
{
    (void)item;
    tray_exit();
}

/* ---------------------------------------------------------------- entry */

static int run_helper_if_requested(void)
{
    int argc = 0;
    LPWSTR *argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    int result = -1;
    if (argv != NULL && argc >= 2)
    {
        if (_wcsicmp(argv[1], ARG_INSTALL_DRIVERS) == 0)
        {
            result = (int)drivers_install_elevated();
        }
        else if (_wcsicmp(argv[1], ARG_UNINSTALL_DRIVERS) == 0)
        {
            result = (int)drivers_uninstall_elevated();
        }
    }
    LocalFree(argv);
    return result;
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE prev, LPWSTR cmdline, int show)
{
    (void)instance;
    (void)prev;
    (void)cmdline;
    (void)show;

    log_write(L"start: %s", GetCommandLineW());
    int helper = run_helper_if_requested();
    if (helper >= 0)
    {
        log_write(L"helper finished, exit code %d", helper);
        return helper;
    }

    hiding_enabled = settings_get_hide_controller();
    drivers_query(&drivers);
    rebuild_tray_menu();

    log_write(L"drivers: vigem=%d hidhide=%d hidhide_ready=%d, hide=%d", drivers.vigem_installed,
              drivers.hidhide_installed, drivers.hidhide_ready, hiding_enabled);
    int init = tray_init(&tray);
    log_write(L"tray_init: %d", init);
    if (init == TRAY_ERROR_ALREADY_RUNNING)
    {
        MessageBoxW(NULL, L"StadiaPad is already running — see the tray icon (bottom right).", APP_NAME,
                    MB_OK | MB_ICONINFORMATION | MB_SETFOREGROUND | MB_TOPMOST);
        return 0;
    }
    if (init < 0)
    {
        MessageBoxW(NULL, L"Could not create the tray icon.", APP_NAME, MB_OK | MB_ICONERROR | MB_SETFOREGROUND | MB_TOPMOST);
        return 1;
    }

    /* Older builds registered autostart; this version is started manually only. */
    settings_remove_legacy_autostart();

    if (!drivers.vigem_installed || !drivers.hidhide_installed)
    {
        install_drivers(TRUE);
    }

    vigem_try_connect();
    log_write(L"vigem connected=%d (err 0x%X)", vigem_connected, (unsigned)vigem_last_error);
    if (drivers.vigem_installed && !vigem_connected)
    {
        notify(NT_TRAY_ERROR, vigem_last_error == VIGEM_ERROR_BUS_VERSION_MISMATCH
                                  ? L"Incompatible ViGEmBus version"
                                  : L"Could not connect to ViGEmBus");
    }
    else if (drivers.hidhide_installed && !drivers.hidhide_ready)
    {
        notify(NT_TRAY_WARNING, L"HidHide will start working after a restart");
    }

    stadia_update_callback = stadia_controller_update_cb;
    stadia_disconnect_callback = stadia_controller_disconnect_cb;
    tray_set_user_callback(user_message_cb);

    refresh_devices();
    tray_register_device_notification(hid_get_class(), device_change_cb);

    while (tray_loop(TRUE) == 0)
    {
        ;
    }

    /* Shutdown: release controllers, give the real controller back to Windows. */
    log_write(L"exiting");
    remove_all_devices();
    remove_hiding();
    if (vigem_client != NULL)
    {
        if (vigem_connected)
        {
            vigem_disconnect(vigem_client);
        }
        vigem_free(vigem_client);
    }
    return 0;
}
