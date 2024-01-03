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

/* Being a null driver, there's no event stream. We just define stubs for
   most of the API. */

#include "../../events/SDL_events_c.h"

#include "SDL_ogcevents_c.h"
#include "SDL_ogcvideo.h"

#include <ogc/system.h>

/* These variables can be set from the handlers registered in SDL_main() */
bool OGC_PowerOffRequested = false;
bool OGC_ResetRequested = false;

void OGC_PumpEvents(_THIS)
{
    if (OGC_ResetRequested || OGC_PowerOffRequested) {
        SDL_Event ev;
        ev.type = SDL_QUIT;
        SDL_PushEvent(&ev);
        if (OGC_PowerOffRequested) {
            SYS_ResetSystem(SYS_POWEROFF, 0, 0);
        }
    }
}

#endif /* SDL_VIDEO_DRIVER_OGC */

/* vi: set ts=4 sw=4 expandtab: */
