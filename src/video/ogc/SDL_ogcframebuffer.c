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

#include "../SDL_sysvideo.h"
#include "SDL_ogcframebuffer_c.h"
#include "SDL_ogcvideo.h"

#include <ogc/gx.h>
#include <ogc/system.h>
#include <ogc/video.h>

int SDL_OGC_CreateWindowFramebuffer(_THIS, SDL_Window *window, Uint32 *format, void **pixels, int *pitch)
{
    const Uint32 surface_format = SDL_PIXELFORMAT_ARGB8888;
    int bytes_per_pixel = 4;

    puts("Creating window\n");
    /* Save the info and return! */
    *pitch = 1024 * bytes_per_pixel;
    *format = surface_format;
    *pixels = MEM_PHYSICAL_TO_K0(0x8000000);
    return 0;
}

int SDL_OGC_UpdateWindowFramebuffer(_THIS, SDL_Window *window, const SDL_Rect *rects, int numrects)
{
    SDL_VideoData *videodata = (SDL_VideoData *)_this->driverdata;

    GX_CopyDisp(videodata->xfb[0], GX_FALSE);
    GX_DrawDone();
    GX_Flush();

    VIDEO_Flush();
    VIDEO_WaitVSync();

    return 0;
}

void SDL_OGC_DestroyWindowFramebuffer(_THIS, SDL_Window *window)
{
    puts("Destroying window\n");
}

#endif /* SDL_VIDEO_DRIVER_OGC */

/* vi: set ts=4 sw=4 expandtab: */
