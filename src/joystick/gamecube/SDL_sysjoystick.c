/*
  Simple DirectMedia Layer
  Copyright (C) 1997-2023 Sam Lantinga <slouken@libsdl.org>

  This software is provided 'as-is', without any express or implied
  warranty.  In no event will the authors be held liable for any damages
  arising from the use of this software.

  Permission is granted to anyone to use this software for any purpose,
  including commercial applications, and to alter it and redistribute it
  freely, subject to the following restrictions:

  1. The origin of this software must not be misrepresented; you must not
     claim that you wrote the original software. If you use this software
     in a product, an acknowledgment in the product documentation would be
     appreciated but is not required.
  2. Altered source versions must be plainly marked as such, and must not be
     misrepresented as being the original software.
  3. This notice may not be removed or altered from any source distribution.
*/

#include "../../SDL_internal.h"

#ifdef SDL_JOYSTICK_GAMECUBE

#include "../SDL_joystick_c.h"
#include "../SDL_sysjoystick.h"
#include "../usb_ids.h"
#include "SDL_events.h"
#include "SDL_joystick.h"

#include <gccore.h>
#include <math.h>
#include <unistd.h>

#define MAX_GC_JOYSTICKS 4
#define MAX_JOYSTICKS    MAX_GC_JOYSTICKS

#define MAX_GC_AXES    6
#define MAX_GC_BUTTONS 8
#define MAX_GC_HATS    1

#define JOYNAMELEN 10

#define AXIS_MIN -32767 /* minimum value for axis coordinate */
#define AXIS_MAX 32767  /* maximum value for axis coordinate */

#define debug(...) SDL_LogDebug(SDL_LOG_CATEGORY_INPUT, __VA_ARGS__)

typedef struct joystick_paddata_t
{
    u16 prev_buttons;
    s8 stickX;
    s8 stickY;
    s8 substickX;
    s8 substickY;
    u8 triggerL;
    u8 triggerR;
} joystick_paddata;

/* The private structure used to keep track of a joystick */
typedef struct joystick_hwdata
{
    int index;
    joystick_paddata gamecube;
} joystick_hwdata;

static const u16 sdl_buttons_gc[] = {
    PAD_BUTTON_A,
    PAD_BUTTON_B,
    0 /* 1 */,
    0 /* 2 */,
    0 /* - */,
    PAD_TRIGGER_Z,
    PAD_BUTTON_START,
    0 /* Z */,
    0 /* C */,
    PAD_BUTTON_X,
    PAD_BUTTON_Y,
    PAD_TRIGGER_L,
    PAD_TRIGGER_R
};

static SDL_JoystickID s_connected_instances[MAX_JOYSTICKS];
/* Value is 0 if controller is not present, otherwise 1 */
static char s_detected_devices[MAX_JOYSTICKS];
static u32 s_gc_last_scanpads = 0;
static char s_gc_failed_reads = 0;
static bool s_hardware_queried = false;

static int device_index_to_joypad_index(int device_index)
{
    int count = 0;

    for (int i = 0; i < MAX_JOYSTICKS; i++) {
        if (s_connected_instances[i] >= 0) {
            if (count == device_index)
                return i;
            count++;
        }
    }

    SDL_LogError(SDL_LOG_CATEGORY_INPUT,
                 "Cannot find device index %d", device_index);
    return -1;
}

static int device_index_to_instance(int device_index)
{
    int index = device_index_to_joypad_index(device_index);
    if (index < 0)
        return -1;
    return s_connected_instances[index];
}

static void scan_hardware(void)
{
    /* Scan the GameCube controllers, but only if this was not done
     * before during this update cycle.
     * The Detect() callback, resets the s_hardware_queried variable. */
    if (!s_hardware_queried) {
        s_gc_last_scanpads = PAD_ScanPads();
        s_hardware_queried = true;
    }
}

static void report_joystick(int index, int connected)
{
    debug("Controller %d was %s (%d)\n",
          index, connected ? "connected" : "removed", connected);

    /* First, if the joystick was connected with a different expansion, remove
     * it */
    if (s_connected_instances[index] >= 0) {
        SDL_PrivateJoystickRemoved(s_connected_instances[index]);
        s_connected_instances[index] = -1;
    }

    if (connected) {
        s_connected_instances[index] = SDL_GetNextJoystickInstanceID();
        SDL_PrivateJoystickAdded(s_connected_instances[index]);
    }
}

static int GC_JoystickInit(void)
{
    PAD_Init();

    /* Initialize the needed variables */
    for (int i = 0; i < MAX_JOYSTICKS; i++) {
        s_connected_instances[i] = -1;
    }
    return 0;
}

static int GC_JoystickGetCount(void)
{
    int count = 0;

    for (int i = 0; i < MAX_JOYSTICKS; i++) {
        if (s_connected_instances[i] >= 0)
            count++;
    }
    return count;
}

static void GC_JoystickDetect(void)
{
    scan_hardware();

    /* Ignore individual disconnected statuses, since they might just
     * happen because the controller is not ready. */
    if (s_gc_last_scanpads == 0 && s_gc_failed_reads < 4) {
        s_gc_failed_reads++;
        s_hardware_queried = false;
        return;
    }
    s_gc_failed_reads = 0;

    for (int i = 0; i < MAX_JOYSTICKS; i++) {
        bool connected = s_gc_last_scanpads & (1 << i);
        bool was_connected = s_detected_devices[i];
        if (connected == was_connected)
            continue;

        report_joystick(i, connected);
        s_detected_devices[i] = connected;
    }

    /* This is to force a refresh, the next time that Update() or Detect() are
     * called. This relies on the fact that SDL calls Detect() after Update().
     */
    s_hardware_queried = false;
}

static char joy_name[64];

/* Function to get the device-dependent name of a joystick */
static const char *GC_JoystickGetDeviceName(int device_index)
{
    int index = device_index_to_joypad_index(device_index);
    if (index < 0)
        return NULL;

    if (index >= 0 && index < MAX_JOYSTICKS) {
        sprintf(joy_name, "Gamecube %d", index);
    } else {
        sprintf(joy_name, "Invalid device index: %d", device_index);
    }
    return joy_name;
}

static const char *GC_JoystickGetDevicePath(int index)
{
    return NULL;
}

static int GC_JoystickGetDevicePlayerIndex(int device_index)
{
    return -1;
}

static void GC_JoystickSetDevicePlayerIndex(int device_index, int player_index)
{
}

static SDL_JoystickGUID GC_JoystickGetDeviceGUID(int device_index)
{
    int index = device_index_to_joypad_index(device_index);
    Uint16 bus, product, version;
    Uint8 driver_signature, driver_data;
    const char *name;

    bus = SDL_HARDWARE_BUS_UNKNOWN;
    /* We invent our own product IDs, to tell our joysticks apart.
     * Since we want the gamepads to appear with the numeric ID in their
     * name, we make them unique by assigning a different product depending on
     * the port. */
    product = index << 8;
    version = 1;
    driver_signature = 0;
    driver_data = 0;

    name = GC_JoystickGetDeviceName(device_index);
    return SDL_CreateJoystickGUID(bus, USB_VENDOR_NINTENDO, product, version,
                                  name, driver_signature, driver_data);
}

static SDL_JoystickID GC_JoystickGetDeviceInstanceID(int device_index)
{
    return device_index_to_instance(device_index);
}

static int GC_JoystickOpen(SDL_Joystick *joystick, int device_index)
{
    int index = device_index_to_joypad_index(device_index);

    debug("Open joystick %d (our index: %d)\n", device_index, index);

    if (index < 0)
        return -1;

    /* allocate memory for system specific hardware data */
    joystick->hwdata = SDL_malloc(sizeof(joystick_hwdata));
    if (joystick->hwdata == NULL) {
        SDL_OutOfMemory();
        return (-1);
    }
    joystick->instance_id = s_connected_instances[index];

    SDL_memset(joystick->hwdata, 0, sizeof(joystick_hwdata));
    joystick->hwdata->index = index;
    if (index >= 0 && index < MAX_JOYSTICKS) {
        joystick->nbuttons = MAX_GC_BUTTONS;
        joystick->naxes = MAX_GC_AXES;
        joystick->nhats = MAX_GC_HATS;
    }
    return 0;
}

static int GC_JoystickRumble(SDL_Joystick *joystick,
                             Uint16 low_frequency_rumble,
                             Uint16 high_frequency_rumble)
{
    return SDL_Unsupported();
}

static int GC_JoystickRumbleTriggers(SDL_Joystick *joystick,
                                     Uint16 left_rumble, Uint16 right_rumble)
{
    return SDL_Unsupported();
}

static Uint32 GC_JoystickGetCapabilities(SDL_Joystick *joystick)
{
    /* TODO */
    return 0;
}

static int GC_JoystickSetLED(SDL_Joystick *joystick,
                             Uint8 red, Uint8 green, Uint8 blue)
{
    return SDL_Unsupported();
}

static int GC_JoystickSendEffect(SDL_Joystick *joystick,
                                 const void *data, int size)
{
    return SDL_Unsupported();
}

static int GC_JoystickSetSensorsEnabled(SDL_Joystick *joystick, SDL_bool enabled)
{
    return SDL_Unsupported();
}

static void _HandleGCJoystickUpdate(SDL_Joystick *joystick)
{
    u16 buttons, prev_buttons, changed;
    int i;
    int axis;
    joystick_hwdata *prev_state;
    int index = joystick->hwdata->index;

    buttons = PAD_ButtonsHeld(index);
    prev_state = (joystick_hwdata *)joystick->hwdata;
    prev_buttons = prev_state->gamecube.prev_buttons;
    changed = buttons ^ prev_buttons;

    if (changed & (PAD_BUTTON_LEFT | PAD_BUTTON_RIGHT | PAD_BUTTON_DOWN | PAD_BUTTON_UP)) {
        int hat = SDL_HAT_CENTERED;
        if (buttons & PAD_BUTTON_UP)
            hat |= SDL_HAT_UP;
        if (buttons & PAD_BUTTON_DOWN)
            hat |= SDL_HAT_DOWN;
        if (buttons & PAD_BUTTON_LEFT)
            hat |= SDL_HAT_LEFT;
        if (buttons & PAD_BUTTON_RIGHT)
            hat |= SDL_HAT_RIGHT;
        SDL_PrivateJoystickHat(joystick, 0, hat);
    }

    for (i = 0; i < (sizeof(sdl_buttons_gc) / sizeof(sdl_buttons_gc[0])); i++) {
        if (changed & sdl_buttons_gc[i])
            SDL_PrivateJoystickButton(joystick, i,
                                      (buttons & sdl_buttons_gc[i]) ? SDL_PRESSED : SDL_RELEASED);
    }
    prev_state->gamecube.prev_buttons = buttons;
    axis = PAD_StickX(index);
    if (prev_state->gamecube.stickX != axis) {
        SDL_PrivateJoystickAxis(joystick, 0, axis << 8);
        prev_state->gamecube.stickX = axis;
    }

    axis = PAD_StickY(index);
    if (prev_state->gamecube.stickY != axis) {
        SDL_PrivateJoystickAxis(joystick, 1, (-axis) << 8);
        prev_state->gamecube.stickY = axis;
    }

    axis = PAD_SubStickX(index);
    if (prev_state->gamecube.substickX != axis) {
        SDL_PrivateJoystickAxis(joystick, 2, axis << 8);
        prev_state->gamecube.substickX = axis;
    }

    axis = PAD_SubStickY(index);
    if (prev_state->gamecube.substickY != axis) {
        SDL_PrivateJoystickAxis(joystick, 3, (-axis) << 8);
        prev_state->gamecube.substickY = axis;
    }

    axis = PAD_TriggerL(index);
    if (prev_state->gamecube.triggerL != axis) {
        SDL_PrivateJoystickAxis(joystick, 4, axis << 7);
        prev_state->gamecube.triggerL = axis;
    }

    axis = PAD_TriggerR(index);
    if (prev_state->gamecube.triggerR != axis) {
        SDL_PrivateJoystickAxis(joystick, 5, axis << 7);
        prev_state->gamecube.triggerR = axis;
    }
}

static void GC_JoystickUpdate(SDL_Joystick *joystick)
{
    if (!joystick || !joystick->hwdata)
        return;

    scan_hardware();

    _HandleGCJoystickUpdate(joystick);
}

/* Function to close a joystick after use */
static void GC_JoystickClose(SDL_Joystick *joystick)
{
    if (!joystick || !joystick->hwdata) // joystick already closed
        return;

    SDL_free(joystick->hwdata);
    joystick->hwdata = NULL;
}

/* Function to perform any system-specific joystick related cleanup */
static void GC_JoystickQuit(void)
{
}

static SDL_bool GC_JoystickGetGamepadMapping(int device_index,
                                             SDL_GamepadMapping *out)
{
    int index = device_index_to_joypad_index(device_index);

    if (index < 0 || index >= MAX_JOYSTICKS) {
        return SDL_FALSE;
    }

    *out = (SDL_GamepadMapping){
        .a = { EMappingKind_Button, 0 },
        .b = { EMappingKind_Button, 2 },
        .x = { EMappingKind_Button, 1 },
        .y = { EMappingKind_Button, 3 },
        .back = { EMappingKind_Button, 6 },
        .guide = { EMappingKind_None, 255 },
        .start = { EMappingKind_Button, 7 },
        .leftstick = { EMappingKind_None, 255 },
        .rightstick = { EMappingKind_None, 255 },
        .leftshoulder = { EMappingKind_Button, 4 },
        .rightshoulder = { EMappingKind_Button, 5 },
        .dpup = { EMappingKind_Hat, 0x01 },
        .dpdown = { EMappingKind_Hat, 0x04 },
        .dpleft = { EMappingKind_Hat, 0x08 },
        .dpright = { EMappingKind_Hat, 0x02 },
        .misc1 = { EMappingKind_None, 255 },
        .paddle1 = { EMappingKind_None, 255 },
        .paddle2 = { EMappingKind_None, 255 },
        .paddle3 = { EMappingKind_None, 255 },
        .paddle4 = { EMappingKind_None, 255 },
        .leftx = { EMappingKind_Axis, 0 },
        .lefty = { EMappingKind_Axis, 1 },
        .rightx = { EMappingKind_Axis, 2 },
        .righty = { EMappingKind_Axis, 3 },
        .lefttrigger = { EMappingKind_Axis, 4 },
        .righttrigger = { EMappingKind_Axis, 5 },
    };
    return SDL_TRUE;
}

SDL_JoystickDriver SDL_GAMECUBE_JoystickDriver = {
    .Init = GC_JoystickInit,
    .GetCount = GC_JoystickGetCount,
    .Detect = GC_JoystickDetect,
    .GetDeviceName = GC_JoystickGetDeviceName,
    .GetDevicePath = GC_JoystickGetDevicePath,
    .GetDevicePlayerIndex = GC_JoystickGetDevicePlayerIndex,
    .SetDevicePlayerIndex = GC_JoystickSetDevicePlayerIndex,
    .GetDeviceGUID = GC_JoystickGetDeviceGUID,
    .GetDeviceInstanceID = GC_JoystickGetDeviceInstanceID,
    .Open = GC_JoystickOpen,
    .Rumble = GC_JoystickRumble,
    .RumbleTriggers = GC_JoystickRumbleTriggers,
    .GetCapabilities = GC_JoystickGetCapabilities,
    .SetLED = GC_JoystickSetLED,
    .SendEffect = GC_JoystickSendEffect,
    .SetSensorsEnabled = GC_JoystickSetSensorsEnabled,
    .Update = GC_JoystickUpdate,
    .Close = GC_JoystickClose,
    .Quit = GC_JoystickQuit,
    .GetGamepadMapping = GC_JoystickGetGamepadMapping
};

#endif /* SDL_JOYSTICK_GAMECUBE */

/* vi: set sts=4 ts=4 sw=4 expandtab: */
