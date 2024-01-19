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

#ifdef SDL_VIDEO_DRIVER_OGC

#include "../../events/SDL_events_c.h"
#include "../SDL_pixels_c.h"
#include "../SDL_sysvideo.h"
#include "SDL_mouse.h"
#include "SDL_video.h"

#include "SDL_hints.h"
#include "SDL_ogcevents_c.h"
#include "SDL_ogcframebuffer_c.h"
#include "SDL_ogcgxcommon.h"
#include "SDL_ogcmouse.h"
#include "SDL_ogcvideo.h"

#include <malloc.h>
#include <ogc/color.h>
#include <ogc/gx.h>
#include <ogc/system.h>
#include <ogc/video.h>

#define DEFAULT_FIFO_SIZE 256 * 1024

/* Initialization/Query functions */
static int OGC_VideoInit(_THIS);
static void OGC_VideoQuit(_THIS);

static void OGC_ShowWindow(_THIS, SDL_Window *window)
{
    SDL_SetMouseFocus(window);
    SDL_SetKeyboardFocus(window);
}

/* OGC driver bootstrap functions */

static void OGC_DeleteDevice(SDL_VideoDevice *device)
{
    SDL_free(device->driverdata);
    SDL_free(device);
}

static SDL_VideoDevice *OGC_CreateDevice(void)
{
    SDL_VideoDevice *device;
    SDL_VideoData *videodata;

    /* Initialize all variables that we clean on shutdown */
    device = (SDL_VideoDevice *)SDL_calloc(1, sizeof(SDL_VideoDevice));
    if (!device) {
        SDL_OutOfMemory();
        return NULL;
    }

    videodata = (SDL_VideoData *)SDL_calloc(1, sizeof(SDL_VideoData));
    if (!videodata) {
        SDL_OutOfMemory();
        SDL_free(device);
        return NULL;
    }

    device->driverdata = videodata;

    /* Set the function pointers */
    device->VideoInit = OGC_VideoInit;
    device->VideoQuit = OGC_VideoQuit;
    device->PumpEvents = OGC_PumpEvents;
    device->ShowWindow = OGC_ShowWindow;
    device->CreateWindowFramebuffer = SDL_OGC_CreateWindowFramebuffer;
    device->UpdateWindowFramebuffer = SDL_OGC_UpdateWindowFramebuffer;
    device->DestroyWindowFramebuffer = SDL_OGC_DestroyWindowFramebuffer;

    device->free = OGC_DeleteDevice;

    return device;
}

VideoBootStrap OGC_bootstrap = {
    "ogc-video", "ogc video driver",
    OGC_CreateDevice
};

int OGC_VideoInit(_THIS)
{
    SDL_VideoData *videodata = (SDL_VideoData *)_this->driverdata;
    SDL_DisplayMode mode;
    GXRModeObj *vmode;
    static const GXColor background = { 0, 0, 0, 255 };
    int h_aspect, v_aspect;

    VIDEO_Init();

    vmode = VIDEO_GetPreferredMode(NULL);
    VIDEO_Configure(vmode);

    /* Allocate the XFB */
    videodata->xfb[0] = MEM_K0_TO_K1(SYS_AllocateFramebuffer(vmode));
    videodata->xfb[1] = MEM_K0_TO_K1(SYS_AllocateFramebuffer(vmode));

    VIDEO_ClearFrameBuffer(vmode, videodata->xfb[0], COLOR_BLACK);
    VIDEO_SetNextFramebuffer(videodata->xfb[0]);
    VIDEO_SetBlack(false);
    VIDEO_Flush();

    videodata->gp_fifo = memalign(32, DEFAULT_FIFO_SIZE);
    memset(videodata->gp_fifo, 0, DEFAULT_FIFO_SIZE);
    GX_Init(videodata->gp_fifo, DEFAULT_FIFO_SIZE);

    /* Setup the EFB -> XFB copy operation */
    GX_SetDispCopySrc(0, 0, vmode->fbWidth, vmode->efbHeight);
    GX_SetDispCopyDst(vmode->fbWidth, vmode->xfbHeight);
    GX_SetDispCopyYScale((f32)vmode->xfbHeight / (f32)vmode->efbHeight);
    GX_SetCopyFilter(vmode->aa, vmode->sample_pattern, GX_FALSE, vmode->vfilter);
    GX_SetCopyClear(background, GX_MAX_Z24);

    GX_SetFieldMode(vmode->field_rendering,
                    ((vmode->viHeight == 2 * vmode->xfbHeight) ? GX_ENABLE : GX_DISABLE));
    GX_SetPixelFmt(GX_PF_RGB8_Z24, GX_ZC_LINEAR);
    GX_SetCullMode(GX_CULL_NONE);
    GX_SetBlendMode(GX_BM_NONE, GX_BL_SRCALPHA, GX_BL_INVSRCALPHA, GX_LO_CLEAR);

    GX_SetZMode(GX_TRUE, GX_LEQUAL, GX_TRUE);

    /* Should we need to adjust the aspect, this is the place to do it */
    h_aspect = 320;
    v_aspect = 240;
    OGC_draw_init(vmode->fbWidth, vmode->efbHeight, h_aspect, v_aspect);

    GX_Flush();

    /* Use a fake 32-bpp desktop mode */
    SDL_zero(mode);
    mode.format = SDL_PIXELFORMAT_ARGB8888;
    mode.w = vmode->fbWidth;
    mode.h = vmode->xfbHeight;
    mode.refresh_rate = 60;
    mode.driverdata = NULL;
    if (SDL_AddBasicVideoDisplay(&mode) < 0) {
        return -1;
    }

    SDL_AddDisplayMode(&_this->displays[0], &mode);

    videodata->vmode = vmode;

#ifdef __wii__
    OGC_InitMouse(_this);
#endif
    return 0;
}

void OGC_VideoQuit(_THIS)
{
    SDL_VideoData *videodata = (SDL_VideoData *)_this->driverdata;

#ifdef __wii__
    OGC_QuitMouse(_this);
#endif

    SDL_free(videodata->gp_fifo);
    if (videodata->xfb[0])
        free(MEM_K1_TO_K0(videodata->xfb[0]));
    if (videodata->xfb[1])
        free(MEM_K1_TO_K0(videodata->xfb[1]));
}

void *OGC_video_get_xfb(_THIS)
{
    SDL_VideoData *videodata = _this->driverdata;
    return videodata->xfb[videodata->fb_index];
}

void OGC_video_flip(_THIS, bool vsync)
{
    SDL_VideoData *videodata = _this->driverdata;
    void *xfb = OGC_video_get_xfb(_this);

#ifdef __wii__
    OGC_draw_cursor(_this);
#endif
    GX_CopyDisp(xfb, GX_TRUE);
    GX_DrawDone();
    GX_Flush();

    VIDEO_SetNextFramebuffer(xfb);
    VIDEO_Flush();
    if (vsync) {
        VIDEO_WaitVSync();
    }

    videodata->fb_index ^= 1;
}

#endif /* SDL_VIDEO_DRIVER_OGC */

/* vi: set ts=4 sw=4 expandtab: */
