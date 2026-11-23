/*
 * stadia.c -- Routines for interacting with a Stadia controller.
 */

#include "stadia.h"

#include "hid.h"

#include <stdio.h>
#include <synchapi.h>
#include <tchar.h>
#include <windows.h>

#pragma comment(lib, "kernel32.lib")

#define STADIA_READ_TIMEOUT 10

/*
 * Stadia controller vibration output report identifier.
 */
#define STADIA_VIBRATION_IDENTIFIER 0x05

static const BYTE init_vibration[5] = {STADIA_VIBRATION_IDENTIFIER, 0x00, 0x00, 0x00, 0x00};

static const DWORD dpad_map[8] =
    {
        STADIA_BUTTON_UP,
        STADIA_BUTTON_UP | STADIA_BUTTON_RIGHT,
        STADIA_BUTTON_RIGHT,
        STADIA_BUTTON_RIGHT | STADIA_BUTTON_DOWN,
        STADIA_BUTTON_DOWN,
        STADIA_BUTTON_DOWN | STADIA_BUTTON_LEFT,
        STADIA_BUTTON_LEFT,
        STADIA_BUTTON_LEFT | STADIA_BUTTON_UP};

static int last_error = 0;

void (*stadia_update_callback)(struct stadia_controller *, struct stadia_state *) = NULL;
void (*stadia_disconnect_callback)(struct stadia_controller *) = NULL;

static DWORD WINAPI _stadia_input_thread(LPVOID lparam)
{
    struct stadia_controller *controller = (struct stadia_controller *)lparam;
    INT bytes_read = 0;

    while (controller->active)
    {
        while (controller->active && (bytes_read = hid_get_input_report(controller->device, STADIA_READ_TIMEOUT)) == 0)
            ;

        if (!controller->active)
        {
            break;
        }

        if (bytes_read < 0)
        {
            /* Device is gone (unplugged / bluetooth dropped). Let the owner clean up. */
            controller->disconnected = TRUE;
            if (stadia_disconnect_callback != NULL)
            {
                stadia_disconnect_callback(controller);
            }
            break;
        }

        // check packet header
        if (controller->device->input_buffer[0] != 0x03)
        {
            continue;
        }

        AcquireSRWLockExclusive(&controller->state_lock);

        BYTE *buf = controller->device->input_buffer;

        controller->state.buttons = STADIA_BUTTON_NONE;

        controller->state.buttons |= buf[1] < 8 ? dpad_map[buf[1]] : 0;

        controller->state.buttons |= (buf[2] & (1 << 7)) != 0 ? STADIA_BUTTON_RS : 0;
        controller->state.buttons |= (buf[2] & (1 << 6)) != 0 ? STADIA_BUTTON_OPTIONS : 0;
        controller->state.buttons |= (buf[2] & (1 << 5)) != 0 ? STADIA_BUTTON_MENU : 0;
        controller->state.buttons |= (buf[2] & (1 << 4)) != 0 ? STADIA_BUTTON_STADIA_BTN : 0;
        controller->state.buttons |= (buf[2] & (1 << 1)) != 0 ? STADIA_BUTTON_ASSISTANT : 0;
        controller->state.buttons |= (buf[2] & (1 << 0)) != 0 ? STADIA_BUTTON_CAPTURE : 0;

        controller->state.buttons |= (buf[3] & (1 << 6)) != 0 ? STADIA_BUTTON_A : 0;
        controller->state.buttons |= (buf[3] & (1 << 5)) != 0 ? STADIA_BUTTON_B : 0;
        controller->state.buttons |= (buf[3] & (1 << 4)) != 0 ? STADIA_BUTTON_X : 0;
        controller->state.buttons |= (buf[3] & (1 << 3)) != 0 ? STADIA_BUTTON_Y : 0;
        controller->state.buttons |= (buf[3] & (1 << 2)) != 0 ? STADIA_BUTTON_LB : 0;
        controller->state.buttons |= (buf[3] & (1 << 1)) != 0 ? STADIA_BUTTON_RB : 0;
        controller->state.buttons |= (buf[3] & (1 << 0)) != 0 ? STADIA_BUTTON_LS : 0;

        controller->state.left_stick_x = buf[4];
        controller->state.left_stick_y = buf[5];

        controller->state.right_stick_x = buf[6];
        controller->state.right_stick_y = buf[7];

        controller->state.left_trigger = buf[8];
        controller->state.right_trigger = buf[9];

        ReleaseSRWLockExclusive(&controller->state_lock);

        if (stadia_update_callback != NULL)
        {
            stadia_update_callback(controller, &controller->state);
        }
    }

    return 0;
}

static DWORD WINAPI _stadia_output_thread(LPVOID lparam)
{
    struct stadia_controller *controller = (struct stadia_controller *)lparam;

    BYTE vibration[5] = {STADIA_VIBRATION_IDENTIFIER, 0x0, 0x0, 0x0, 0x0};

    HANDLE wait_events[2] = {controller->output_event, controller->stopping_event};

    while (controller->active)
    {
        DWORD wait_result = WaitForMultipleObjects(2, wait_events, FALSE, INFINITE);
        if (wait_result != WAIT_OBJECT_0 || !controller->active)
        {
            break;
        }

        ResetEvent(controller->output_event);

        AcquireSRWLockShared(&controller->vibration_lock);

        vibration[2] = controller->big_motor;
        vibration[4] = controller->small_motor;

        ReleaseSRWLockShared(&controller->vibration_lock);

        hid_send_output_report(controller->device, vibration, sizeof(vibration), STADIA_READ_TIMEOUT);
    }

    if (!controller->disconnected)
    {
        hid_send_output_report(controller->device, init_vibration, sizeof(init_vibration), STADIA_READ_TIMEOUT);
    }

    return 0;
}

struct stadia_controller *stadia_controller_create(struct hid_device *device)
{
    if (hid_send_output_report(device, init_vibration, sizeof(init_vibration), STADIA_READ_TIMEOUT) <= 0)
    {
        // Not fatal: vibration may be unsupported (e.g. bluetooth).
        last_error = STADIA_ERROR_VIBRATION_INIT_FAILURE;
    }

    struct stadia_controller *controller = (struct stadia_controller *)calloc(1, sizeof(struct stadia_controller));
    if (controller == NULL)
    {
        return NULL;
    }
    controller->device = device;
    controller->active = TRUE;
    controller->disconnected = FALSE;

    InitializeSRWLock(&controller->state_lock);
    InitializeSRWLock(&controller->vibration_lock);

    controller->stopping_event = CreateEvent(NULL, TRUE, FALSE, NULL);
    controller->output_event = CreateEvent(NULL, TRUE, FALSE, NULL);

    if (controller->stopping_event != NULL && controller->output_event != NULL)
    {
        controller->input_thread = CreateThread(NULL, 0, _stadia_input_thread, controller, CREATE_SUSPENDED, NULL);
        controller->output_thread = CreateThread(NULL, 0, _stadia_output_thread, controller, CREATE_SUSPENDED, NULL);
    }

    if (controller->input_thread != NULL)
    {
        ResumeThread(controller->input_thread);
    }
    if (controller->output_thread != NULL)
    {
        ResumeThread(controller->output_thread);
    }

    if (controller->input_thread == NULL || controller->output_thread == NULL)
    {
        last_error = STADIA_ERROR_THREAD_CREATE_FAILURE;
        stadia_controller_destroy(controller);
        return NULL;
    }

    return controller;
}

void stadia_controller_set_vibration(struct stadia_controller *controller, BYTE small_motor, BYTE big_motor)
{
    AcquireSRWLockExclusive(&controller->vibration_lock);

    controller->small_motor = small_motor;
    controller->big_motor = big_motor;

    ReleaseSRWLockExclusive(&controller->vibration_lock);

    SetEvent(controller->output_event);
}

/*
 * Stops the worker threads and frees the controller. Must be called by the owner,
 * never from inside a controller callback. The hid_device is NOT closed here.
 */
void stadia_controller_destroy(struct stadia_controller *controller)
{
    controller->active = FALSE;
    if (controller->stopping_event != NULL)
    {
        SetEvent(controller->stopping_event);
    }
    CancelIoEx(controller->device->handle, &controller->device->input_ol);

    HANDLE threads[2];
    DWORD thread_count = 0;
    if (controller->input_thread != NULL)
    {
        threads[thread_count++] = controller->input_thread;
    }
    if (controller->output_thread != NULL)
    {
        threads[thread_count++] = controller->output_thread;
    }
    if (thread_count > 0)
    {
        WaitForMultipleObjects(thread_count, threads, TRUE, 2000);
    }
    for (DWORD i = 0; i < thread_count; i++)
    {
        CloseHandle(threads[i]);
    }

    if (controller->stopping_event != NULL)
    {
        CloseHandle(controller->stopping_event);
    }
    if (controller->output_event != NULL)
    {
        CloseHandle(controller->output_event);
    }

    free(controller);
}
