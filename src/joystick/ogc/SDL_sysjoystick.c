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

#ifdef SDL_JOYSTICK_OGC

#include "../SDL_joystick_c.h"
#include "../SDL_sysjoystick.h"
#include "../usb_ids.h"
#include "SDL_events.h"
#include "SDL_hints.h"
#include "../../SDL_hints_c.h"
#include "SDL_joystick.h"

#include <gccore.h>
#include <math.h>
#include <unistd.h>
#include <wiiuse/wpad.h>

#define PI 3.14159265f

#define MAX_GC_JOYSTICKS  4
#define MAX_WII_JOYSTICKS 4

#define GC_JOYSTICKS_START  0
#define GC_JOYSTICKS_END    MAX_GC_JOYSTICKS
#define WII_JOYSTICKS_START GC_JOYSTICKS_END
#define WII_WIIMOTES_START  WII_JOYSTICKS_START
#define WII_WIIMOTES_END    (WII_WIIMOTES_START + MAX_WII_JOYSTICKS)
#define WII_EXP_START       WII_WIIMOTES_END
#define WII_EXP_END         (WII_EXP_START + MAX_WII_JOYSTICKS)
#define WII_JOYSTICKS_END   WII_EXP_END

#define MAX_JOYSTICKS WII_EXP_END

#define MAX_GC_AXES    6
#define MAX_GC_BUTTONS 8
#define MAX_GC_HATS    1

#define MAX_WII_AXES    9
#define MAX_WII_BUTTONS 15
#define MAX_WII_HATS    1

#define JOYNAMELEN 10

#define AXIS_MIN -32767 /* minimum value for axis coordinate */
#define AXIS_MAX 32767  /* maximum value for axis coordinate */

#define MAX_RUMBLE 8

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

typedef struct joystick_wpaddata_t
{
    u32 prev_buttons;
    u32 exp;
    s16 nunchuk_stickX;
    s16 nunchuk_stickY;
    s16 classicL_stickX;
    s16 classicL_stickY;
    s16 classicR_stickX;
    s16 classicR_stickY;
    u8 classic_triggerL;
    u8 classic_triggerR;
    u8 classic_calibrated;
    s8 wiimote_pitch;
    s8 wiimote_roll;
    s8 wiimote_yaw;
    s16 classic_cal[4][3]; // 4x axes, min/center/max
} joystick_wpaddata;

/* The private structure used to keep track of a joystick */
typedef struct joystick_hwdata
{
    int index;
    char sensors_disabled;
    /*  This must be big enough for MAX_RUMBLE */
    char rumble_intensity;
    u16 rumble_loop;
    union
    {
        joystick_paddata gamecube;
        joystick_wpaddata wiimote;
    };
} joystick_hwdata;

#ifdef __wii__
static const u32 sdl_buttons_wii[] = {
    WPAD_BUTTON_A | WPAD_CLASSIC_BUTTON_A,
    WPAD_BUTTON_B | WPAD_CLASSIC_BUTTON_B,
    WPAD_BUTTON_1,
    WPAD_BUTTON_2,
    WPAD_BUTTON_MINUS | WPAD_CLASSIC_BUTTON_MINUS,
    WPAD_BUTTON_PLUS | WPAD_CLASSIC_BUTTON_PLUS,
    WPAD_BUTTON_HOME | WPAD_CLASSIC_BUTTON_HOME,
    WPAD_NUNCHUK_BUTTON_Z, /* 7 */
    WPAD_NUNCHUK_BUTTON_C, /* 8 */
    WPAD_CLASSIC_BUTTON_X, /* 9 */
    WPAD_CLASSIC_BUTTON_Y,
    WPAD_CLASSIC_BUTTON_FULL_L,
    WPAD_CLASSIC_BUTTON_FULL_R,
    WPAD_CLASSIC_BUTTON_ZL,
    WPAD_CLASSIC_BUTTON_ZR
};
#define SDL_WII_NUM_BUTTONS_WII \
    (sizeof(sdl_buttons_wii) / sizeof(sdl_buttons_wii[0]))

static const u32 sdl_buttons_wiimote[] = {
    WPAD_BUTTON_A,
    WPAD_BUTTON_B,
    WPAD_BUTTON_1,
    WPAD_BUTTON_2,
    WPAD_BUTTON_MINUS,
    WPAD_BUTTON_PLUS,
    WPAD_BUTTON_HOME,
};
#define SDL_WII_NUM_BUTTONS_WIIMOTE \
    (sizeof(sdl_buttons_wiimote) / sizeof(sdl_buttons_wiimote[0]))

static const u32 sdl_buttons_nunchuck[] = {
    WPAD_NUNCHUK_BUTTON_Z,
    WPAD_NUNCHUK_BUTTON_C,
};
#define SDL_WII_NUM_BUTTONS_NUNCHUCK \
    (sizeof(sdl_buttons_nunchuck) / sizeof(sdl_buttons_nunchuck[0]))

static const u32 sdl_buttons_classic[] = {
    WPAD_CLASSIC_BUTTON_A,
    WPAD_CLASSIC_BUTTON_B,
    WPAD_CLASSIC_BUTTON_X,
    WPAD_CLASSIC_BUTTON_Y,
    WPAD_CLASSIC_BUTTON_FULL_L,
    WPAD_CLASSIC_BUTTON_FULL_R,
    WPAD_CLASSIC_BUTTON_ZL,
    WPAD_CLASSIC_BUTTON_ZR,
    WPAD_CLASSIC_BUTTON_MINUS,
    WPAD_CLASSIC_BUTTON_PLUS,
    WPAD_CLASSIC_BUTTON_HOME,
};
#define SDL_WII_NUM_BUTTONS_CLASSIC \
    (sizeof(sdl_buttons_classic) / sizeof(sdl_buttons_classic[0]))
#endif /* __wii__ */

static const u16 sdl_buttons_gc[] = {
    PAD_BUTTON_A,
    PAD_BUTTON_B,
    PAD_BUTTON_X,
    PAD_BUTTON_Y,
    PAD_TRIGGER_L,
    PAD_TRIGGER_R,
    PAD_TRIGGER_Z,
    PAD_BUTTON_START,
};

static int split_joysticks = 0;

static SDL_JoystickID s_connected_instances[MAX_JOYSTICKS];
/* Value is 0 if controller is not present, otherwise 1 + extension enum */
static char s_detected_devices[MAX_JOYSTICKS];
static char s_gc_failed_reads = 0;
static u32 s_gc_last_scanpads = 0;
static bool s_hardware_queried = false;


#ifdef __wii__
static bool s_wii_has_new_data[MAX_WII_JOYSTICKS];
static bool s_accelerometers_as_axes = false;

static void SDLCALL
on_hint_accel_as_joystick_cb(void *userdata, const char *name,
                             const char *oldValue, const char *hint)
{
    s_accelerometers_as_axes = SDL_GetStringBoolean(hint, SDL_FALSE);
}

#endif

/* Joypad index is 0-11: 4 GC, 4 Wiimotes and (if split_joysticks) 4 expansions
 */
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
    /* Scan the GameCube and Wii controllers, but only if this was not done
     * before during this update cycle.
     * The Detect() callback, resets the s_hardware_queried variable. */
    if (!s_hardware_queried) {
        s_gc_last_scanpads = PAD_ScanPads();
#ifdef __wii__
        for (int i = 0; i < MAX_WII_JOYSTICKS; i++) {
            s_wii_has_new_data[i] = WPAD_ReadPending(i, NULL);
        }
#endif
        s_hardware_queried = true;
    }
}

static void report_joystick(int index, int connected)
{
    printf("Controller %d was %s (%d)\n",
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

static inline bool enable_rumble(int index, bool enable)
{
    if (index >= GC_JOYSTICKS_START && index < GC_JOYSTICKS_END) {
        PAD_ControlMotor(index - GC_JOYSTICKS_START,
                         enable ? PAD_MOTOR_RUMBLE : PAD_MOTOR_STOP);
        return true;
#ifdef __wii__
    } else if (index >= WII_WIIMOTES_START && index < WII_JOYSTICKS_END) {
        WPAD_Rumble(index - WII_WIIMOTES_START, enable);
        return true;
#endif
    } else {
        return false;
    }
}

static void update_rumble(SDL_Joystick *joystick)
{
    char intensity = joystick->hwdata->rumble_intensity;
    s16 loop;
    int rest_frames;
    bool rumble;
    if (intensity == 0 || intensity == MAX_RUMBLE - 1) return;

    loop = ++joystick->hwdata->rumble_loop;

    /* The rest_frames constant should probably be set according to the current
     * framerate; or we should rework the logic to be completely time-based.
     * It may also be that we need different values depending on the controller
     * type. */
    rest_frames = 2;
    if (loop == 1) {
        rumble = false;
    } else if (loop > (MAX_RUMBLE - 1 - intensity) * rest_frames) {
        rumble = true;
        joystick->hwdata->rumble_loop = 0;
    } else {
        /* Keep the engine stopped until our time comes again */
        return;
    }

    enable_rumble(joystick->hwdata->index, rumble);
}

/* Function to scan the system for joysticks.
 * This function should return the number of available
 * joysticks.  Joystick 0 should be the system default joystick.
 * It should return -1 on an unrecoverable fatal error.
 */
static int OGC_JoystickInit(void)
{
    const char *split_joystick_env = getenv("SDL_WII_JOYSTICK_SPLIT");
    split_joysticks = split_joystick_env && strcmp(split_joystick_env, "1") == 0;

    PAD_Init();
    /* We don't call WPAD_Init() here, since it's already been called by
     * SDL_main for the Wii */

#ifdef __wii__
    SDL_AddHintCallback(SDL_HINT_ACCELEROMETER_AS_JOYSTICK,
                        on_hint_accel_as_joystick_cb, NULL);
#endif

    /* Initialize the needed variables */
    for (int i = 0; i < MAX_JOYSTICKS; i++) {
        s_connected_instances[i] = -1;
    }
    return 0;
}

static int OGC_JoystickGetCount(void)
{
    int count = 0;

    for (int i = 0; i < MAX_JOYSTICKS; i++) {
        if (s_connected_instances[i] >= 0)
            count++;
    }
    return count;
}

static void OGC_JoystickDetect(void)
{
    scan_hardware();

    /* Ignore individual disconnected statuses, since they might just
     * happen because the controller is not ready. */
    if (s_gc_last_scanpads == 0 && s_gc_failed_reads < 4) {
        s_gc_failed_reads++;
        s_hardware_queried = false;
    } else {
        s_gc_failed_reads = 0;
        for (int i = 0; i < MAX_GC_JOYSTICKS; i++) {
            bool connected = s_gc_last_scanpads & (1 << i);
            bool was_connected = s_detected_devices[i];
            if (connected == was_connected)
                continue;

            report_joystick(i, connected);
            s_detected_devices[i] = connected;
        }
    }

#ifdef __wii__
    for (int i = 0; i < MAX_WII_JOYSTICKS; i++) {
        int connected, was_connected, index;
        WPADData *data;

        if (!s_wii_has_new_data[i])
            continue;

        data = WPAD_Data(i);
        index = WII_JOYSTICKS_START + i;
        connected = data->err != WPAD_ERR_NO_CONTROLLER &&
                    data->data_present != 0;
        if (split_joysticks) {
            int exp_index = WII_EXP_START + i;
            int exp_connected =
                (connected && data->exp.type != WPAD_EXP_NONE) ? (1 + data->exp.type) : 0;
            int exp_was_connected = s_detected_devices[exp_index];
            if (exp_connected != exp_was_connected) {
                s_detected_devices[exp_index] = exp_connected;
                report_joystick(exp_index, exp_connected);
            }
        } else if (connected) {
            connected += data->exp.type;
        }

        was_connected = s_detected_devices[index];
        if (connected != was_connected) {
            s_detected_devices[index] = connected;
            report_joystick(index, connected);
        }
    }
#endif

    /* This is to force a refresh, the next time that Update() or Detect() are
     * called. This relies on the fact that SDL calls Detect() after Update().
     */
    s_hardware_queried = false;
}

static char joy_name[128];

/* Function to get the device-dependent name of a joystick */
static const char *OGC_JoystickGetDeviceName(int device_index)
{
    int index = device_index_to_joypad_index(device_index);
    if (index < 0)
        return NULL;

    if (index >= GC_JOYSTICKS_START && index < GC_JOYSTICKS_END) {
        sprintf(joy_name, "Gamecube %d", index);
#ifdef __wii__
    } else if (index >= WII_WIIMOTES_START && index < WII_WIIMOTES_END) {
        char *name_ptr = joy_name;
        int expansion = s_detected_devices[index] - 1;
        name_ptr += sprintf(name_ptr, "Wiimote %d", index - WII_WIIMOTES_START);
        if (!split_joysticks) {
            // Add expansion information
            switch (expansion) {
            case WPAD_EXP_NUNCHUK:
                strcpy(name_ptr, " + Nunchuk");
                break;
            case WPAD_EXP_CLASSIC:
                strcpy(name_ptr, " + Classic");
                break;
            case WPAD_EXP_GUITARHERO3:
                strcpy(name_ptr, " + Guitar Hero 3");
                break;
            case WPAD_EXP_WIIBOARD:
                strcpy(name_ptr, " + Balance board");
                break;
            }
        }
    } else if (split_joysticks) {
        /* This is an expansion and we are using the split controllers
         * option: show only the expansion name, then. */
        int expansion = s_detected_devices[index] - 1;
        int idx = index - WII_EXP_START;
        switch (expansion) {
        case WPAD_EXP_NUNCHUK:
            sprintf(joy_name, "Nunchuk %d", idx);
            break;
        case WPAD_EXP_CLASSIC:
            sprintf(joy_name, "Classic %d", idx);
            break;
        case WPAD_EXP_GUITARHERO3:
            sprintf(joy_name, "Guitar Hero 3 %d", idx);
            break;
        case WPAD_EXP_WIIBOARD:
            sprintf(joy_name, "Balance board %d", idx);
            break;
        case WPAD_EXP_NONE:
            strcpy(joy_name, "Disconnected");
            break;
        default:
            sprintf(joy_name, "Unknown %d", idx);
            break;
        }
#endif
    } else {
        sprintf(joy_name, "Invalid device index: %d", device_index);
    }
    return joy_name;
}

static const char *OGC_JoystickGetDevicePath(int index)
{
    return NULL;
}

static int OGC_JoystickGetDevicePlayerIndex(int device_index)
{
    return -1;
}

static void OGC_JoystickSetDevicePlayerIndex(int device_index, int player_index)
{
}

static SDL_JoystickGUID OGC_JoystickGetDeviceGUID(int device_index)
{
    int index = device_index_to_joypad_index(device_index);
    Uint16 bus, product, version;
    Uint8 driver_signature, driver_data;
    const char *name;

    /* We invent our own product IDs, to tell our joysticks apart.
     * Since we want the gamepads to appear with the numeric ID in their
     * name, we make them unique by assigning a different product depending on
     * the port. */
    product = (index + 1) << 8;
    if (index >= GC_JOYSTICKS_START && index < GC_JOYSTICKS_END) {
        bus = SDL_HARDWARE_BUS_UNKNOWN;
    } else {
        bus = SDL_HARDWARE_BUS_BLUETOOTH;
        product += s_detected_devices[index];
    }
    version = 1;
    driver_signature = 0;
    driver_data = 0;

    name = OGC_JoystickGetDeviceName(device_index);
    return SDL_CreateJoystickGUID(bus, USB_VENDOR_NINTENDO, product, version,
                                  name, driver_signature, driver_data);
}

static SDL_JoystickID OGC_JoystickGetDeviceInstanceID(int device_index)
{
    return device_index_to_instance(device_index);
}

static int OGC_JoystickOpen(SDL_Joystick *joystick, int device_index)
{
    int index = device_index_to_joypad_index(device_index);

    printf("Open joystick %d (our index: %d)\n", device_index, index);

    if (index < 0)
        return -1;

    /* allocate memory for system specific hardware data */
    joystick->hwdata = SDL_malloc(sizeof(joystick_hwdata));
    if (joystick->hwdata == NULL) {
        SDL_OutOfMemory();
        return -1;
    }
    joystick->instance_id = s_connected_instances[index];

    SDL_memset(joystick->hwdata, 0, sizeof(joystick_hwdata));
    joystick->hwdata->index = index;
    if (index >= GC_JOYSTICKS_START && index < GC_JOYSTICKS_END) {
        joystick->nbuttons = MAX_GC_BUTTONS;
        joystick->naxes = MAX_GC_AXES;
        joystick->nhats = MAX_GC_HATS;
#ifdef __wii__
    } else {
        if (split_joysticks) {
            if (index < WII_WIIMOTES_END) {
                // wiimote
                joystick->nbuttons = SDL_WII_NUM_BUTTONS_WIIMOTE;
                joystick->naxes = 3;
                joystick->nhats = 1;
                SDL_PrivateJoystickAddSensor(joystick, SDL_SENSOR_ACCEL, 100.0f);
            } else {
                // expansion
                joystick->nbuttons = SDL_max(SDL_WII_NUM_BUTTONS_NUNCHUCK,
                                             SDL_WII_NUM_BUTTONS_CLASSIC);
                joystick->naxes = 6;
                joystick->nhats = 1;
                if (s_detected_devices[index] == 1 + WPAD_EXP_NUNCHUK) {
                    SDL_PrivateJoystickAddSensor(joystick, SDL_SENSOR_ACCEL_L, 100.0f);
                }
            }
        } else {
            joystick->nbuttons = MAX_WII_BUTTONS;
            joystick->naxes = MAX_WII_AXES;
            joystick->nhats = MAX_WII_HATS;
            /* Add the accelerometer only if there is no expansion connected */
            if (s_detected_devices[index] == 1) {
                SDL_PrivateJoystickAddSensor(joystick, SDL_SENSOR_ACCEL, 100.0f);
            } else if (s_detected_devices[index] == 1 + WPAD_EXP_NUNCHUK) {
                /* Or, if the nunchuck is connected, add the wiimote, and the
                 * nunchuk on the left */
                SDL_PrivateJoystickAddSensor(joystick, SDL_SENSOR_ACCEL, 100.0f);
                SDL_PrivateJoystickAddSensor(joystick, SDL_SENSOR_ACCEL_L, 100.0f);
            }
        }
#endif
    }
    return 0;
}

static int OGC_JoystickRumble(SDL_Joystick *joystick,
                              Uint16 low_frequency_rumble,
                              Uint16 high_frequency_rumble)
{
    int index = joystick->hwdata->index;
    /* The Wii and GameCube controllers do not support setting the frequency of
     * the rumble, so we use a hack where we periodically stop and start the
     * motors during Update(). */
    char intensity = MAX_RUMBLE *
        ((low_frequency_rumble + high_frequency_rumble) / 2) /
        0xffff;
    /* We don't accept MAX_RUMBLE itself */
    if (intensity >= MAX_RUMBLE)
        intensity = MAX_RUMBLE - 1;

    /* If it's the same as the current value, do nothing */
    if (intensity == joystick->hwdata->rumble_intensity) {
        return 0;
    }

    if (!enable_rumble(index, intensity > 0)) {
        return SDL_Unsupported();
    }

    /* Save the current rumble status, we need it in update_rumble() */
    joystick->hwdata->rumble_intensity = intensity;
    joystick->hwdata->rumble_loop = 0;

    return 0;
}

static int OGC_JoystickRumbleTriggers(SDL_Joystick *joystick,
                                      Uint16 left_rumble, Uint16 right_rumble)
{
    return SDL_Unsupported();
}

static Uint32 OGC_JoystickGetCapabilities(SDL_Joystick *joystick)
{
    Uint32 capabilities = 0;

    int index = joystick->hwdata->index;
    if ((index >= GC_JOYSTICKS_START && index < GC_JOYSTICKS_END) ||
        /* Rumble is supported on the wiimotes, but it makes sense only if no
         * expansion is attached, of if we are in split mode. */
        (index >= WII_WIIMOTES_START && index < WII_WIIMOTES_END &&
         (s_detected_devices[index] == 1 || split_joysticks))) {
        capabilities |= SDL_JOYCAP_RUMBLE;
    }
    return capabilities;
}

static int OGC_JoystickSetLED(SDL_Joystick *joystick,
                              Uint8 red, Uint8 green, Uint8 blue)
{
    return SDL_Unsupported();
}

static int OGC_JoystickSendEffect(SDL_Joystick *joystick,
                                  const void *data, int size)
{
    return SDL_Unsupported();
}

static int OGC_JoystickSetSensorsEnabled(SDL_Joystick *joystick, SDL_bool enabled)
{
    int index = joystick->hwdata->index;
    if (index >= WII_WIIMOTES_START && index < WII_WIIMOTES_END) {
        joystick->hwdata->sensors_disabled = !enabled;
        return 0;
    }
    return SDL_Unsupported();
}

#ifdef __wii__

static s16 WPAD_Orient(WPADData *data, int motion)
{
    float out;

    if (motion == 0)
        out = data->orient.pitch;
    else if (motion == 1)
        out = data->orient.roll;
    else
        out = data->orient.yaw;

    return (s16)((out / 180.0) * 128.0);
}

static s16 WPAD_Pitch(WPADData *data)
{
    return WPAD_Orient(data, 0);
}

static s16 WPAD_Roll(WPADData *data)
{
    return WPAD_Orient(data, 1);
}

static s16 WPAD_Yaw(WPADData *data)
{
    return WPAD_Orient(data, 2);
}

static s16 WPAD_Stick(s16 x, s16 min, s16 center, s16 max, int flip)
{
    s16 d;
    int ret;

    x -= center;

    if (x < 0)
        d = center - min;
    else
        d = max - center;

    if (center - min < 5)
        return 0;
    if (max - center < 5)
        return 0;

    if (d)
        ret = (x << 15) / d;
    else
        return 0;

    if (flip)
        ret = -ret;

    if (ret < AXIS_MIN)
        ret = AXIS_MIN;
    else if (ret > AXIS_MAX)
        ret = AXIS_MAX;

    return ret;
}

static const u32 _buttons[8] = {
    // wiimote
    WPAD_BUTTON_UP,
    WPAD_BUTTON_DOWN,
    WPAD_BUTTON_LEFT,
    WPAD_BUTTON_RIGHT,
    // classic
    WPAD_CLASSIC_BUTTON_UP,
    WPAD_CLASSIC_BUTTON_DOWN,
    WPAD_CLASSIC_BUTTON_LEFT,
    WPAD_CLASSIC_BUTTON_RIGHT
};

static void HandleWiiHats(SDL_Joystick *joystick,
                          const u32 changed, const u32 pressed,
                          const u32 *buttons)
{
    if (changed & (buttons[0] | buttons[1] | buttons[2] | buttons[3])) {
        int hat = SDL_HAT_CENTERED;

        if (pressed & buttons[0])
            hat |= SDL_HAT_UP;
        if (pressed & buttons[1])
            hat |= SDL_HAT_DOWN;
        if (pressed & buttons[2])
            hat |= SDL_HAT_LEFT;
        if (pressed & buttons[3])
            hat |= SDL_HAT_RIGHT;
        SDL_PrivateJoystickHat(joystick, 0, hat);
    }
}

/* Helpers to separate nunchuk vs classic buttons which share the
 * same scan codes. In particular, up on the classic controller is
 * the same as Z on the nunchuk. The numbers refer to the sdl_buttons_wii
 * list above. */
static int wii_button_is_nunchuk(int idx)
{
    return idx == 7 || idx == 8;
}

static int wii_button_is_classic(int idx)
{
    return idx >= 9;
}

static void HandleWiiButtons(SDL_Joystick *joystick,
                             const u32 changed,
                             const WPADData *data,
                             const u32 *buttons,
                             size_t num_buttons)
{
    for (int i = 0; i < num_buttons; i++) {
        if (changed & buttons[i]) {
            if (!split_joysticks &&
                ((data->exp.type == WPAD_EXP_CLASSIC && wii_button_is_nunchuk(i)) ||
                 (data->exp.type == WPAD_EXP_NUNCHUK && wii_button_is_classic(i))))
                continue;

            SDL_PrivateJoystickButton(joystick, i,
                                      (data->btns_d & buttons[i]) ? SDL_PRESSED : SDL_RELEASED);
        }
    }
}

static void HandleWiiMotion(SDL_Joystick *joystick,
                            joystick_hwdata *prev_state,
                            WPADData *data,
                            int start_index)
{
    int axis = WPAD_Pitch(data);
    if (prev_state->wiimote.wiimote_pitch != axis) {
        SDL_PrivateJoystickAxis(joystick, start_index, -(axis << 8));
        prev_state->wiimote.wiimote_pitch = axis;
    }
    axis = WPAD_Roll(data);
    if (prev_state->wiimote.wiimote_roll != axis) {
        SDL_PrivateJoystickAxis(joystick, start_index + 1, axis << 8);
        prev_state->wiimote.wiimote_roll = axis;
    }
    axis = WPAD_Yaw(data);
    if (prev_state->wiimote.wiimote_yaw != axis) {
        SDL_PrivateJoystickAxis(joystick, start_index + 2, axis << 8);
        prev_state->wiimote.wiimote_yaw = axis;
    }
}

static void HandleNunchuckSensors(SDL_Joystick *joystick,
                                  const nunchuk_t *data)
{
    float values[3];
    SDL_SensorType type;

    if (joystick->hwdata->sensors_disabled) return;

    type = split_joysticks ? SDL_SENSOR_ACCEL : SDL_SENSOR_ACCEL_L;
    values[0] = data->gforce.x * SDL_STANDARD_GRAVITY;
    values[1] = data->gforce.z * SDL_STANDARD_GRAVITY;
    values[2] = -data->gforce.y * SDL_STANDARD_GRAVITY;
    SDL_PrivateJoystickSensor(joystick, type, 0, values, 3);
}

static void HandleWiimoteSensors(SDL_Joystick *joystick,
                                 WPADData *data)
{
    float values[3];

    if (joystick->hwdata->sensors_disabled) return;

    values[0] = data->gforce.x * SDL_STANDARD_GRAVITY;
    values[1] = data->gforce.z * SDL_STANDARD_GRAVITY;
    values[2] = -data->gforce.y * SDL_STANDARD_GRAVITY;
    SDL_PrivateJoystickSensor(joystick, SDL_SENSOR_ACCEL, 0, values, 3);
}

static void _HandleWiiJoystickUpdate(SDL_Joystick *joystick)
{
    u32 changed, pressed;
    int axis, wpad_index;
    joystick_hwdata *prev_state;
    WPADData *data;
    bool update_wiimote, update_expansion;

    prev_state = joystick->hwdata;
    if (split_joysticks) {
        if (joystick->hwdata->index >= WII_EXP_START) {
            wpad_index = joystick->hwdata->index - WII_EXP_START;
            update_wiimote = false;
            update_expansion = true;
        } else {
            wpad_index = joystick->hwdata->index - WII_WIIMOTES_START;
            update_wiimote = true;
            update_expansion = false;
        }
    } else {
        wpad_index = joystick->hwdata->index - WII_WIIMOTES_START;
        update_wiimote = true;
        update_expansion = true;
    }

    if (update_wiimote) {
        update_rumble(joystick);
    }

    if (!s_wii_has_new_data[wpad_index])
        return;

    data = WPAD_Data(wpad_index);
    changed = data->btns_d | data->btns_u;
    pressed = data->btns_d | data->btns_h;

    if (update_wiimote) {
        HandleWiiHats(joystick, changed, pressed, _buttons);
    }
    if (update_expansion) {
        if (data->exp.type == WPAD_EXP_CLASSIC) {
            HandleWiiHats(joystick, changed, pressed, _buttons + 4);
        }
    }

    if (split_joysticks) {
        if (update_wiimote) {
            HandleWiiButtons(joystick, changed, data,
                             sdl_buttons_wiimote, SDL_WII_NUM_BUTTONS_WIIMOTE);
        }
        if (update_expansion) {
            if (data->exp.type == WPAD_EXP_CLASSIC) {
                HandleWiiButtons(joystick, changed, data,
                                 sdl_buttons_classic, SDL_WII_NUM_BUTTONS_CLASSIC);
            } else if (data->exp.type == WPAD_EXP_NUNCHUK) {
                HandleWiiButtons(joystick, changed, data,
                                 sdl_buttons_nunchuck, SDL_WII_NUM_BUTTONS_NUNCHUCK);
            }
        }
    } else {
        HandleWiiButtons(joystick, changed, data,
                         sdl_buttons_wii, SDL_WII_NUM_BUTTONS_WII);
    }

    if (update_expansion) {
        if (data->exp.type == WPAD_EXP_CLASSIC) {
            if (prev_state->wiimote.exp != WPAD_EXP_CLASSIC) {
                prev_state->wiimote.classic_calibrated = 0;
                prev_state->wiimote.classic_cal[0][0] = 5;  // left x min
                prev_state->wiimote.classic_cal[0][2] = 59; // left x max
                prev_state->wiimote.classic_cal[1][0] = 5;  // left y min
                prev_state->wiimote.classic_cal[1][2] = 59; // left y max
                prev_state->wiimote.classic_cal[2][0] = 5;  // right x min
                prev_state->wiimote.classic_cal[2][2] = 27; // right x max
                prev_state->wiimote.classic_cal[3][0] = 5;  // right y min
                prev_state->wiimote.classic_cal[3][2] = 27; // right y max
            }

            // max/min checking
            // left stick x
            if (data->exp.classic.ljs.pos.x < prev_state->wiimote.classic_cal[0][0])
                prev_state->wiimote.classic_cal[0][0] = data->exp.classic.ljs.pos.x;
            else if (data->exp.classic.ljs.pos.x > prev_state->wiimote.classic_cal[0][2])
                prev_state->wiimote.classic_cal[0][2] = data->exp.classic.ljs.pos.x;
            // left stick y
            if (data->exp.classic.ljs.pos.y < prev_state->wiimote.classic_cal[1][0])
                prev_state->wiimote.classic_cal[1][0] = data->exp.classic.ljs.pos.y;
            else if (data->exp.classic.ljs.pos.y > prev_state->wiimote.classic_cal[1][2])
                prev_state->wiimote.classic_cal[1][2] = data->exp.classic.ljs.pos.y;
            // right stick x
            if (data->exp.classic.rjs.pos.x < prev_state->wiimote.classic_cal[2][0])
                prev_state->wiimote.classic_cal[2][0] = data->exp.classic.rjs.pos.x;
            else if (data->exp.classic.rjs.pos.x > prev_state->wiimote.classic_cal[2][2])
                prev_state->wiimote.classic_cal[2][2] = data->exp.classic.rjs.pos.x;
            // right stick y
            if (data->exp.classic.rjs.pos.y < prev_state->wiimote.classic_cal[3][0])
                prev_state->wiimote.classic_cal[3][0] = data->exp.classic.rjs.pos.y;
            else if (data->exp.classic.rjs.pos.y > prev_state->wiimote.classic_cal[3][2])
                prev_state->wiimote.classic_cal[3][2] = data->exp.classic.rjs.pos.y;

            // calibrate center positions
            if (prev_state->wiimote.classic_calibrated < 5) {
                prev_state->wiimote.classic_cal[0][1] = data->exp.classic.ljs.pos.x;
                prev_state->wiimote.classic_cal[1][1] = data->exp.classic.ljs.pos.y;
                prev_state->wiimote.classic_cal[2][1] = data->exp.classic.rjs.pos.x;
                prev_state->wiimote.classic_cal[3][1] = data->exp.classic.rjs.pos.y;
                // this is zero if the expansion hasn't finished initializing
                if (data->exp.classic.ljs.max.x)
                    prev_state->wiimote.classic_calibrated++;
            }
        }

        if (data->exp.type != prev_state->wiimote.exp) {
            // Reset the expansion axes
            for (int i = 0; i < 6; i++)
                SDL_PrivateJoystickAxis(joystick, i, 0);
        }

        if (data->exp.type == WPAD_EXP_CLASSIC) {
            axis = WPAD_Stick(data->exp.classic.ljs.pos.x, prev_state->wiimote.classic_cal[0][0],
                              prev_state->wiimote.classic_cal[0][1], prev_state->wiimote.classic_cal[0][2], 0);
            if (prev_state->wiimote.classicL_stickX != axis) {
                SDL_PrivateJoystickAxis(joystick, 0, axis);
                prev_state->wiimote.classicL_stickX = axis;
            }
            // y axes are reversed
            axis = WPAD_Stick(data->exp.classic.ljs.pos.y, prev_state->wiimote.classic_cal[1][0],
                              prev_state->wiimote.classic_cal[1][1], prev_state->wiimote.classic_cal[1][2], 1);
            if (prev_state->wiimote.classicL_stickY != axis) {
                SDL_PrivateJoystickAxis(joystick, 1, axis);
                prev_state->wiimote.classicL_stickY = axis;
            }
            axis = WPAD_Stick(data->exp.classic.rjs.pos.x, prev_state->wiimote.classic_cal[2][0],
                              prev_state->wiimote.classic_cal[2][1], prev_state->wiimote.classic_cal[2][2], 0);
            if (prev_state->wiimote.classicR_stickX != axis) {
                SDL_PrivateJoystickAxis(joystick, 2, axis);
                prev_state->wiimote.classicR_stickX = axis;
            }
            axis = WPAD_Stick(data->exp.classic.rjs.pos.y, prev_state->wiimote.classic_cal[3][0],
                              prev_state->wiimote.classic_cal[3][1], prev_state->wiimote.classic_cal[3][2], 1);
            if (prev_state->wiimote.classicR_stickY != axis) {
                SDL_PrivateJoystickAxis(joystick, 3, axis);
                prev_state->wiimote.classicR_stickY = axis;
            }
            axis = data->exp.classic.r_shoulder;
            if (prev_state->wiimote.classic_triggerR != axis) {
                SDL_PrivateJoystickAxis(joystick, 4, axis << 8);
                prev_state->wiimote.classic_triggerR = axis;
            }
            axis = data->exp.classic.l_shoulder;
            if (prev_state->wiimote.classic_triggerL != axis) {
                SDL_PrivateJoystickAxis(joystick, 5, axis << 8);
                prev_state->wiimote.classic_triggerL = axis;
            }
        } else if (data->exp.type == WPAD_EXP_NUNCHUK) {
            axis = WPAD_Stick(data->exp.nunchuk.js.pos.x, data->exp.nunchuk.js.min.x,
                              data->exp.nunchuk.js.center.x, data->exp.nunchuk.js.max.x, 0);
            if (prev_state->wiimote.nunchuk_stickX != axis) {
                SDL_PrivateJoystickAxis(joystick, 0, axis);
                prev_state->wiimote.nunchuk_stickX = axis;
            }
            axis = WPAD_Stick(data->exp.nunchuk.js.pos.y, data->exp.nunchuk.js.min.y,
                              data->exp.nunchuk.js.center.y, data->exp.nunchuk.js.max.y, 1);
            if (prev_state->wiimote.nunchuk_stickY != axis) {
                SDL_PrivateJoystickAxis(joystick, 1, axis);
                prev_state->wiimote.nunchuk_stickY = axis;
            }

            HandleNunchuckSensors(joystick, &data->exp.nunchuk);
        }
    }

    prev_state->wiimote.exp = data->exp.type;

    if (update_wiimote) {
        if (s_accelerometers_as_axes) {
            int start_index = split_joysticks ? 0 : 6;
            HandleWiiMotion(joystick, prev_state, data, start_index);
        }
        HandleWiimoteSensors(joystick, data);
    }
}
#endif /* __wii__ */

static void _HandleGCJoystickUpdate(SDL_Joystick *joystick)
{
    u16 buttons, prev_buttons, changed;
    int i;
    int axis;
    joystick_hwdata *prev_state;
    int index = joystick->hwdata->index - GC_JOYSTICKS_START;

    update_rumble(joystick);

    buttons = PAD_ButtonsHeld(index);
    prev_state = joystick->hwdata;
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

static void OGC_JoystickUpdate(SDL_Joystick *joystick)
{
    if (!joystick || !joystick->hwdata)
        return;

    scan_hardware();

    if (joystick->hwdata->index >= GC_JOYSTICKS_START &&
        joystick->hwdata->index < GC_JOYSTICKS_END) {
        _HandleGCJoystickUpdate(joystick);
#ifdef __wii__
    } else {
        _HandleWiiJoystickUpdate(joystick);
#endif
    }
}

static void OGC_JoystickClose(SDL_Joystick *joystick)
{
    if (!joystick || !joystick->hwdata) // joystick already closed
        return;

    SDL_free(joystick->hwdata);
    joystick->hwdata = NULL;
}

void OGC_JoystickQuit(void)
{
#ifdef __wii__
    SDL_DelHintCallback(SDL_HINT_ACCELEROMETER_AS_JOYSTICK,
                        on_hint_accel_as_joystick_cb, NULL);
#endif
}

static SDL_bool OGC_JoystickGetGamepadMapping(int device_index,
                                              SDL_GamepadMapping *out)
{
    int index = device_index_to_joypad_index(device_index);
    SDL_bool is_gamepad = SDL_FALSE;

    if (index >= GC_JOYSTICKS_START && index < GC_JOYSTICKS_END) {
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
        is_gamepad = SDL_TRUE;
#ifdef __wii__
    } else if (index >= WII_WIIMOTES_START && index < WII_WIIMOTES_END) {
        int expansion = s_detected_devices[index] - 1;
        if (split_joysticks || expansion == 0) {
            /* Wiimote alone; assume it's being held sideways */
            *out = (SDL_GamepadMapping){
                .a = { EMappingKind_Button, 2 },
                .b = { EMappingKind_Button, 3 },
                .x = { EMappingKind_Button, 0 },
                .y = { EMappingKind_Button, 1 },
                .back = { EMappingKind_Button, 6 },
                .guide = { EMappingKind_Button, 4 },
                .start = { EMappingKind_Button, 5 },
                .leftstick = { EMappingKind_None, 255 },
                .rightstick = { EMappingKind_None, 255 },
                .leftshoulder = { EMappingKind_None, 255 },
                .rightshoulder = { EMappingKind_None, 255 },
                .dpup = { EMappingKind_Hat, 0x02 },
                .dpdown = { EMappingKind_Hat, 0x08 },
                .dpleft = { EMappingKind_Hat, 0x01 },
                .dpright = { EMappingKind_Hat, 0x04 },
                .misc1 = { EMappingKind_None, 255 },
                .paddle1 = { EMappingKind_None, 255 },
                .paddle2 = { EMappingKind_None, 255 },
                .paddle3 = { EMappingKind_None, 255 },
                .paddle4 = { EMappingKind_None, 255 },
                .leftx = { EMappingKind_Axis, 0 },
                .lefty = { EMappingKind_Axis, 1 },
                .rightx = { EMappingKind_None, 255 },
                .righty = { EMappingKind_None, 255 },
                .lefttrigger = { EMappingKind_None, 255 },
                .righttrigger = { EMappingKind_None, 255 },
            };
            is_gamepad = SDL_TRUE;
        } else if (expansion == WPAD_EXP_NUNCHUK) {
            /* Wiimote with nunchuck; assume nunchuck on left hand, wiimote
             * pointed at screen */
            *out = (SDL_GamepadMapping){
                .a = { EMappingKind_Button, 0 },
                .b = { EMappingKind_Button, 1 },
                .x = { EMappingKind_Button, 7 },
                .y = { EMappingKind_Button, 8 },
                .back = { EMappingKind_Button, 6 },
                .guide = { EMappingKind_Button, 4 },
                .start = { EMappingKind_Button, 5 },
                .leftstick = { EMappingKind_None, 255 },
                .rightstick = { EMappingKind_None, 255 },
                .leftshoulder = { EMappingKind_None, 255 },
                .rightshoulder = { EMappingKind_None, 255 },
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
                .lefttrigger = { EMappingKind_None, 255 },
                .righttrigger = { EMappingKind_None, 255 },
            };
            is_gamepad = SDL_TRUE;
        } else if (expansion == WPAD_EXP_CLASSIC) {
            *out = (SDL_GamepadMapping){
                .a = { EMappingKind_Button, 1 },
                .b = { EMappingKind_Button, 0 },
                .x = { EMappingKind_Button, 10 },
                .y = { EMappingKind_Button, 9 },
                .back = { EMappingKind_Button, 6 },
                .guide = { EMappingKind_Button, 4 },
                .start = { EMappingKind_Button, 5 },
                .leftstick = { EMappingKind_None, 255 },
                .rightstick = { EMappingKind_None, 255 },
                .leftshoulder = { EMappingKind_Button, 11 },
                .rightshoulder = { EMappingKind_Button, 12 },
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
                .lefttrigger = { EMappingKind_Button, 13 },
                .righttrigger = { EMappingKind_Button, 14 },
            };
            is_gamepad = SDL_TRUE;
        }
    } else if (index >= WII_EXP_START && index < WII_EXP_END) {
        /* Wiimote + Extension: only support the classic controller, any other
         * device is useless as a gamepad. */
        int expansion = s_detected_devices[index] - 1;
        if (expansion != WPAD_EXP_CLASSIC)
            return SDL_FALSE;
        *out = (SDL_GamepadMapping){
            .a = { EMappingKind_Button, 1 },
            .b = { EMappingKind_Button, 0 },
            .x = { EMappingKind_Button, 3 },
            .y = { EMappingKind_Button, 2 },
            .back = { EMappingKind_Button, 10 },
            .guide = { EMappingKind_Button, 8 },
            .start = { EMappingKind_Button, 9 },
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
            .lefttrigger = { EMappingKind_Button, 6 },
            .righttrigger = { EMappingKind_Button, 7 },
        };
        is_gamepad = SDL_TRUE;
#endif /* __wii__ */
    }
    return is_gamepad;
}

SDL_JoystickDriver SDL_OGC_JoystickDriver = {
    .Init = OGC_JoystickInit,
    .GetCount = OGC_JoystickGetCount,
    .Detect = OGC_JoystickDetect,
    .GetDeviceName = OGC_JoystickGetDeviceName,
    .GetDevicePath = OGC_JoystickGetDevicePath,
    .GetDevicePlayerIndex = OGC_JoystickGetDevicePlayerIndex,
    .SetDevicePlayerIndex = OGC_JoystickSetDevicePlayerIndex,
    .GetDeviceGUID = OGC_JoystickGetDeviceGUID,
    .GetDeviceInstanceID = OGC_JoystickGetDeviceInstanceID,
    .Open = OGC_JoystickOpen,
    .Rumble = OGC_JoystickRumble,
    .RumbleTriggers = OGC_JoystickRumbleTriggers,
    .GetCapabilities = OGC_JoystickGetCapabilities,
    .SetLED = OGC_JoystickSetLED,
    .SendEffect = OGC_JoystickSendEffect,
    .SetSensorsEnabled = OGC_JoystickSetSensorsEnabled,
    .Update = OGC_JoystickUpdate,
    .Close = OGC_JoystickClose,
    .Quit = OGC_JoystickQuit,
    .GetGamepadMapping = OGC_JoystickGetGamepadMapping
};

#endif /* SDL_JOYSTICK_OGC */

/* vi: set sts=4 ts=4 sw=4 expandtab: */
