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

#ifdef __gamecube__

#include "SDL_main.h"

#ifdef main
#undef main
#endif

/* Standard includes */
#include <stdio.h>

/* OGC includes */
#include <fat.h>
#include <ogcsys.h>

/* Do initialisation which has to be done first for the console to work */
/* Entry point */
int main(int argc, char *argv[])
{
    //	SYS_SetPowerCallback(ShutdownCB);
    //	SYS_SetResetCallback(ResetCB);
    fatInitDefault();
    /* Temporarily while developing SDL */
    SDL_LogSetAllPriority(SDL_LOG_PRIORITY_DEBUG);
    /* Call the user's main function */
    return (SDL_main(argc, argv));
}

#endif /* __gamecube__ */

/* vi: set sts=4 ts=4 sw=4 expandtab: */
