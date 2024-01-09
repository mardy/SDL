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

#ifndef SDL_ogcpixels_h_
#define SDL_ogcpixels_h_

#include "SDL_pixels.h"

#include <gctypes.h>

void OGC_pixels_to_texture(void *pixels, SDL_PixelFormatEnum format,
                           int16_t w, int16_t h, int16_t pitch,
                           void *texels, u8 *gx_format);
void OGC_pixels_from_texture(void *pixels, SDL_PixelFormatEnum format,
                             int16_t w, int16_t h, int16_t pitch,
                             void *texels);

u8 OGC_texture_format_from_SDL(const SDL_PixelFormatEnum format);

#endif /* SDL_ogcpixels_h_ */

/* vi: set ts=4 sw=4 expandtab: */
