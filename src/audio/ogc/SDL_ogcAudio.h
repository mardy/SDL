/*
    SDL - Simple DirectMedia Layer
    Copyright (C) 1997-2006 Sam Lantinga

    This library is free software; you can redistribute it and/or
    modify it under the terms of the GNU Lesser General Public
    License as published by the Free Software Foundation; either
    version 2.1 of the License, or (at your option) any later version.

    This library is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
    Lesser General Public License for more details.

    You should have received a copy of the GNU Lesser General Public
    License along with this library; if not, write to the Free Software
    Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA

    Sam Lantinga
    slouken@libsdl.org
*/
#include "SDL_config.h"

#ifndef _SDL_ogcAudio_h
#define _SDL_ogcAudio_h

#include <ogcsys.h>
#include <aesndlib.h>

#include "../SDL_sysaudio.h"

/* Hidden "this" pointer for the video functions */
#define _THIS   SDL_AudioDevice *this

#define AUDIOSTACK 8192
#define SAMPLES_PER_DMA_BUFFER (DSP_STREAMBUFFER_SIZE)

typedef struct SDL_PrivateAudioData {
    // these go first so they will be aligned
    Uint8 astack[AUDIOSTACK];
    short dma_buffers[2][SAMPLES_PER_DMA_BUFFER*2];

    AESNDPB* voice;
    u32 format;
    u32 freq;
    volatile bool stopaudio;
} ogcAudio;

#endif /* _SDL_dummyaudio_h */
