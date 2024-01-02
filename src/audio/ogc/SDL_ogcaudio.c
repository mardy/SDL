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

#ifdef SDL_AUDIO_DRIVER_OGC

#include "SDL_audio.h"

/* OGC Audio driver */

#include "../SDL_sysaudio.h"
#include "SDL_ogcaudio.h"
#include "SDL_timer.h"

#include <malloc.h>

#define OGCAUDIO_DRIVER_NAME "ogc"

/**
 * Cleans up all allocated memory, safe to call with null pointers
 */
static void FreePrivateData(_THIS)
{
    if (!this->hidden) {
        return;
    }

    SDL_free(this->hidden);
    this->hidden = NULL;
}

static int FindAudioFormat(_THIS)
{
    SDL_bool found_valid_format = SDL_FALSE;
    Uint16 test_format = SDL_FirstAudioFormat(this->spec.format);

    while (!found_valid_format && test_format) {
        this->spec.format = test_format;
        SDL_LogDebug(SDL_LOG_CATEGORY_AUDIO, "Trying format %x", test_format);
        switch (test_format) {
        case AUDIO_S8:
            this->hidden->format = VOICE_MONO8;
            this->hidden->bytes_per_sample = this->spec.channels;
            found_valid_format = SDL_TRUE;
            break;
        case AUDIO_U8:
            this->hidden->format = VOICE_MONO8_UNSIGNED;
            this->hidden->bytes_per_sample = this->spec.channels;
            found_valid_format = SDL_TRUE;
            break;
        case AUDIO_S16LSB:
        case AUDIO_S16MSB:
            this->hidden->format = VOICE_MONO16;
            this->hidden->bytes_per_sample = this->spec.channels * 2;
            this->spec.format = AUDIO_S16MSB;
            found_valid_format = SDL_TRUE;
            break;
        case AUDIO_U16LSB:
        case AUDIO_U16MSB:
            this->hidden->format = VOICE_MONO16_UNSIGNED;
            this->hidden->bytes_per_sample = this->spec.channels * 2;
            this->spec.format = AUDIO_U16MSB;
            found_valid_format = SDL_TRUE;
            break;
        default:
            test_format = SDL_NextAudioFormat();
            break;
        }
    }

    if (found_valid_format && this->spec.channels == 2) {
        this->hidden->format++;
    }

    return found_valid_format ? 0 : -1;
}

/* fully local functions related to the wavebufs / DSP, not the same as the SDL-wide mixer lock */
static SDL_INLINE void contextLock(_THIS)
{
    LWP_MutexLock(this->hidden->lock);
}

static SDL_INLINE void contextUnlock(_THIS)
{
    LWP_MutexUnlock(this->hidden->lock);
}

static void audio_frame_finished(AESNDPB *pb, u32 state, void *arg)
{
    SDL_AudioDevice *this = (SDL_AudioDevice *)arg;

    if (state == VOICE_STATE_STREAM) {
        LWP_CondBroadcast(this->hidden->cv);
    }
}

static int OGCAUDIO_OpenDevice(_THIS, const char *devname)
{
    struct SDL_PrivateAudioData *hidden =
        memalign(32, sizeof(struct SDL_PrivateAudioData));
    if (!hidden) {
        return SDL_OutOfMemory();
    }

    SDL_LogDebug(SDL_LOG_CATEGORY_AUDIO,
                 "OGCAUDIO_OpenDevice, name=%s, freq=%d, channels=%d\n",
                 devname, this->spec.freq, this->spec.channels);

    memset(hidden, 0, sizeof(*hidden));
    this->hidden = hidden;

    AESND_Init();
    AESND_Pause(1);

    /* Initialise internal state */
    LWP_MutexInit(&hidden->lock, false);
    LWP_CondInit(&hidden->cv);

    if (this->spec.freq <= 0 || this->spec.freq > 144000)
        this->spec.freq = DSP_DEFAULT_FREQ;

    if (this->spec.channels > 2) {
        this->spec.channels = 2;
    }

    /* Should not happen but better be safe. */
    if (FindAudioFormat(this) < 0) {
        return SDL_SetError("No supported audio format found.");
    }

    this->spec.samples = DMA_BUFFER_SIZE / this->hidden->bytes_per_sample;

    /* Update the fragment size as size in bytes */
    SDL_CalculateAudioSpec(&this->spec);

    hidden->voice = AESND_AllocateVoiceWithArg(audio_frame_finished, this);
    if (hidden->voice == NULL)
        return -1;

    // start audio
    AESND_SetVoiceFormat(hidden->voice, hidden->format);
    AESND_SetVoiceFrequency(hidden->voice, this->spec.freq);
    AESND_SetVoiceBuffer(hidden->voice, hidden->dma_buffers[0], DMA_BUFFER_SIZE);
    AESND_SetVoiceStream(hidden->voice, true);
    AESND_SetVoiceStop(hidden->voice, 0);
    AESND_Pause(0);

    return 0;
}

static void OGCAUDIO_PlayDevice(_THIS)
{
    void *buffer;
    size_t nextbuf;
    size_t buffer_size = DMA_BUFFER_SIZE;

    contextLock(this);

    nextbuf = this->hidden->nextbuf;
    this->hidden->nextbuf = (nextbuf + 1) % NUM_BUFFERS;

    contextUnlock(this);

    buffer = this->hidden->dma_buffers[nextbuf];
    DCStoreRange(buffer, buffer_size);
    AESND_SetVoiceBuffer(this->hidden->voice, buffer, buffer_size);
}

static void OGCAUDIO_WaitDevice(_THIS)
{
    contextLock(this);
    LWP_CondWait(this->hidden->cv, this->hidden->lock);
    contextUnlock(this);
}

static Uint8 *OGCAUDIO_GetDeviceBuf(_THIS)
{
    return this->hidden->dma_buffers[this->hidden->nextbuf];
}

static void OGCAUDIO_CloseDevice(_THIS)
{
    struct SDL_PrivateAudioData *hidden = this->hidden;

    if (hidden->voice) {
        AESND_SetVoiceStop(hidden->voice, true);
        AESND_FreeVoice(hidden->voice);
        hidden->voice = NULL;
    }

    AESND_Pause(1);
    FreePrivateData(this);
}

static void OGCAUDIO_ThreadInit(_THIS)
{
    LWP_SetThreadPriority(LWP_THREAD_NULL, 80);
}

static SDL_bool OGCAUDIO_Init(SDL_AudioDriverImpl *impl)
{
    /* Set the function pointers */
    impl->OpenDevice = OGCAUDIO_OpenDevice;
    impl->PlayDevice = OGCAUDIO_PlayDevice;
    impl->WaitDevice = OGCAUDIO_WaitDevice;
    impl->GetDeviceBuf = OGCAUDIO_GetDeviceBuf;
    impl->CloseDevice = OGCAUDIO_CloseDevice;
    impl->ThreadInit = OGCAUDIO_ThreadInit;
    impl->OnlyHasDefaultOutputDevice = SDL_TRUE;

    return SDL_TRUE; /* this audio target is available. */
}

AudioBootStrap OGCAUDIO_bootstrap = {
    OGCAUDIO_DRIVER_NAME,
    "SDL OGC audio driver",
    OGCAUDIO_Init,
    0
};

#endif /* SDL_AUDIO_DRIVER_OGC */

/* vi: set sts=4 ts=4 sw=4 expandtab: */
