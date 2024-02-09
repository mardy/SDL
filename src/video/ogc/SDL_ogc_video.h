#include "SDL_config.h"

#ifndef _SDL_ogc_video_h
#define _SDL_ogc_video_h

/* SDL internal includes */
#include "../SDL_sysvideo.h"

/* OGC includes */
#include <ogc/gx_struct.h>

/* Hidden "this" pointer for the video functions */
#define _THIS   SDL_VideoDevice *this

/* Private display data */
typedef struct SDL_PrivateVideoData
{
    // 2x256x16bit palettes = 1x256x24(32)bit palette
    // first 256 entries are for Red/Green
    // last 256 entries are for Green
    Uint16 palette[2*256];

    Uint8* buffer;

    // these two fields MUST be in this order
    Uint8* texturemem;
    size_t texturemem_size;

    int    width;
    int    height;
    int    pitch;
} ogcVideo;

void OGC_InitVideoSystem();
void OGC_ChangeSquare(int xscale, int yscale, int xshift, int yshift);

#endif /* _SDL_wiivideo_h */
