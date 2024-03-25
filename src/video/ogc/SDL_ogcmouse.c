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

#if defined(SDL_VIDEO_DRIVER_OGC) && defined(__wii__)

#include "SDL_surface.h"
#include "SDL_hints.h"

#include "SDL_ogccursors.h"
#include "SDL_ogcgxcommon.h"
#include "SDL_ogcmouse.h"
#include "SDL_ogcpixels.h"

#include "../SDL_sysvideo.h"

#include <malloc.h>
#include <ogc/cache.h>
#include <ogc/gx.h>
#include <wiiuse/wpad.h>

typedef struct _OGC_CursorData
{
    void *texels;
    int hot_x, hot_y;
    int w, h;
} OGC_CursorData;

static void draw_cursor_rect(OGC_CursorData *curdata)
{
    GX_Begin(GX_QUADS, GX_VTXFMT0, 4);
    GX_Position2s16(-curdata->hot_x, -curdata->hot_y);
    GX_TexCoord2u8(0, 0);
    GX_Position2s16(curdata->w - curdata->hot_x, -curdata->hot_y);
    GX_TexCoord2u8(1, 0);
    GX_Position2s16(curdata->w - curdata->hot_x, curdata->h - curdata->hot_y);
    GX_TexCoord2u8(1, 1);
    GX_Position2s16(-curdata->hot_x, curdata->h - curdata->hot_y);
    GX_TexCoord2u8(0, 1);
    GX_End();
}

/* Create a cursor from a surface */
static SDL_Cursor *OGC_CreateCursor(SDL_Surface *surface, int hot_x, int hot_y)
{
    OGC_CursorData *curdata;
    SDL_Cursor *cursor;
    u32 texture_size;
    SDL_Rect rect;

    SDL_assert(surface->pitch == surface->w * 4);

    cursor = SDL_calloc(1, sizeof(*cursor));
    if (!cursor) {
        SDL_OutOfMemory();
        return NULL;
    }

    curdata = SDL_calloc(1, sizeof(*curdata));
    if (!curdata) {
        SDL_OutOfMemory();
        SDL_free(cursor);
        return NULL;
    }

    curdata->hot_x = hot_x;
    curdata->hot_y = hot_y;
    curdata->w = surface->w;
    curdata->h = surface->h;

    texture_size = GX_GetTexBufferSize(surface->w, surface->h, GX_TF_RGBA8,
                                       GX_FALSE, 0);
    curdata->texels = memalign(32, texture_size);
    if (!curdata->texels) {
        SDL_OutOfMemory();
        SDL_free(curdata);
        SDL_free(cursor);
        return NULL;
    }

    rect.x = rect.y = 0;
    rect.w = surface->w;
    rect.h = surface->h;
    OGC_pixels_to_texture(surface->pixels, surface->format->format, &rect,
                          surface->pitch, curdata->texels, surface->w);
    DCStoreRange(curdata->texels, texture_size);
    GX_InvalidateTexAll();

    cursor->driverdata = curdata;

    return cursor;
}

SDL_Cursor *OGC_CreateSystemCursor(SDL_SystemCursor id)
{
    const OGC_Cursor *cursor;
    SDL_Surface *surface;
    SDL_Cursor *c;

    switch (id) {
    case SDL_SYSTEM_CURSOR_ARROW:
        cursor = &OGC_cursor_arrow;
        break;
    case SDL_SYSTEM_CURSOR_HAND:
        cursor = &OGC_cursor_hand;
        break;
    default:
        SDL_LogWarn(SDL_LOG_CATEGORY_VIDEO,
                    "System cursor %d not implemented", id);
        return NULL;
    }
    surface =
        SDL_CreateRGBSurfaceWithFormatFrom((void*)cursor->pixel_data,
                                           cursor->width,
                                           cursor->height,
                                           cursor->bytes_per_pixel * 8,
                                           cursor->width * cursor->bytes_per_pixel,
                                           SDL_PIXELFORMAT_RGBA8888);
    c = OGC_CreateCursor(surface, cursor->hot_x, cursor->hot_y);
    SDL_FreeSurface(surface);
    return c;
}

/* Free a window manager cursor */
static void OGC_FreeCursor(SDL_Cursor *cursor)
{
    OGC_CursorData *curdata = cursor->driverdata;

    if (curdata) {
        if (curdata->texels) {
            free(curdata->texels);
        }
        SDL_free(curdata);
    }

    SDL_free(cursor);
}

void OGC_InitMouse(_THIS)
{
    SDL_Mouse *mouse = SDL_GetMouse();

    mouse->CreateCursor = OGC_CreateCursor;
    mouse->CreateSystemCursor = OGC_CreateSystemCursor;
    mouse->FreeCursor = OGC_FreeCursor;

    SDL_SetDefaultCursor(OGC_CreateSystemCursor(SDL_SYSTEM_CURSOR_HAND));
}

void OGC_QuitMouse(_THIS)
{
}

void OGC_draw_cursor(_THIS)
{
    SDL_Mouse *mouse = SDL_GetMouse();
    OGC_CursorData *curdata;
    Mtx mv;
    int screen_w, screen_h;

    if (!mouse || !mouse->cursor_shown ||
        !mouse->cur_cursor || !mouse->cur_cursor->driverdata) {
        return;
    }

    screen_w = _this->displays[0].current_mode.w;
    screen_h = _this->displays[0].current_mode.h;

    curdata = mouse->cur_cursor->driverdata;
    OGC_load_texture(curdata->texels, curdata->w, curdata->h, GX_TF_RGBA8,
                     SDL_ScaleModeNearest);

    guMtxIdentity(mv);
    guMtxScaleApply(mv, mv, screen_w / 640.0f, screen_h / 480.0f, 1.0f);
    /* If this is the default cursor, rotate it too */
    if (mouse->cur_cursor == mouse->def_cursor) {
        Mtx rot;
        float angle;
        WPADData *data = WPAD_Data(mouse->mouseID);

        angle = data->ir.angle;
        guMtxRotDeg(rot, 'z', angle);
        guMtxConcat(mv, rot, mv);
    }
    guMtxTransApply(mv, mv, mouse->x, mouse->y, 0);
    GX_LoadPosMtxImm(mv, GX_PNMTX1);

    GX_ClearVtxDesc();
    GX_SetVtxDesc(GX_VA_POS, GX_DIRECT);
    GX_SetVtxDesc(GX_VA_TEX0, GX_DIRECT);
    GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS, GX_POS_XY, GX_S16, 0);
    GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_TEX0, GX_TEX_ST, GX_U8, 0);
    GX_SetTexCoordGen(GX_TEXCOORD0, GX_TG_MTX2x4, GX_TG_TEX0, GX_IDENTITY);

    GX_SetTevOp(GX_TEVSTAGE0, GX_REPLACE);
    GX_SetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD0, GX_TEXMAP0, GX_COLOR0A0);
    GX_SetNumTevStages(1);
    GX_SetBlendMode(GX_BM_BLEND, GX_BL_SRCALPHA, GX_BL_INVSRCALPHA, GX_LO_CLEAR);

    GX_SetNumTexGens(1);
    GX_SetCurrentMtx(GX_PNMTX1);
    draw_cursor_rect(curdata);
    GX_SetCurrentMtx(GX_PNMTX0);
    GX_DrawDone();
}

#endif /* SDL_VIDEO_DRIVER_OGC */

/* vi: set ts=4 sw=4 expandtab: */
