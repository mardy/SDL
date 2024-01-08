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

#ifdef SDL_VIDEO_RENDER_OGC

#include "../SDL_sysrender.h"
#include "SDL_hints.h"

#include "../../video/ogc/SDL_ogcgxcommon.h"
#include "../../video/ogc/SDL_ogcvideo.h"

#include <malloc.h>
#include <ogc/gx.h>
#include <ogc/video.h>

typedef struct
{
    u32 drawColor;
} OGC_RenderData;

static void OGC_WindowEvent(SDL_Renderer *renderer, const SDL_WindowEvent *event)
{
}

static int OGC_CreateTexture(SDL_Renderer *renderer, SDL_Texture *texture)
{
    // TODO
    return 0;
}

static int OGC_LockTexture(SDL_Renderer *renderer, SDL_Texture *texture,
                           const SDL_Rect *rect, void **pixels, int *pitch)
{
    // TODO
    return 0;
}

static void OGC_UnlockTexture(SDL_Renderer *renderer, SDL_Texture *texture)
{
    // TODO
}

static int OGC_UpdateTexture(SDL_Renderer *renderer, SDL_Texture *texture,
                             const SDL_Rect *rect, const void *pixels, int pitch)
{
    // TODO
    return 0;
}

static void OGC_SetTextureScaleMode(SDL_Renderer *renderer, SDL_Texture *texture, SDL_ScaleMode scaleMode)
{
    // TODO
}

static int OGC_SetRenderTarget(SDL_Renderer *renderer, SDL_Texture *texture)
{
    return 0;
}

static int OGC_QueueSetViewport(SDL_Renderer *renderer, SDL_RenderCommand *cmd)
{
    return 0; /* nothing to do in this backend. */
}

static int OGC_QueueDrawPoints(SDL_Renderer *renderer, SDL_RenderCommand *cmd,
                               const SDL_FPoint *points, int count)
{
    size_t size = count * sizeof(SDL_FPoint);
    SDL_FPoint *vertices = SDL_AllocateRenderVertices(renderer, size,
                                                      4, &cmd->data.draw.first);
    if (!vertices) {
        return -1;
    }

    cmd->data.draw.count = count;
    SDL_memcpy(vertices, points, size);
    return 0;
}

static int OGC_QueueGeometry(SDL_Renderer *renderer, SDL_RenderCommand *cmd, SDL_Texture *texture,
                             const float *xy, int xy_stride, const SDL_Color *color, int color_stride, const float *uv, int uv_stride,
                             int num_vertices, const void *indices, int num_indices, int size_indices,
                             float scale_x, float scale_y)
{
    // TODO
    return 0;
}

static int OGC_RenderSetViewPort(SDL_Renderer *renderer, SDL_RenderCommand *cmd)
{
    // TODO
    return 0;
}

static int OGC_RenderSetClipRect(SDL_Renderer *renderer, SDL_RenderCommand *cmd)
{
    // TODO
    return 0;
}

static int OGC_RenderSetDrawColor(SDL_Renderer *renderer, SDL_RenderCommand *cmd)
{
    // TODO
    return 0;
}

static int OGC_RenderClear(SDL_Renderer *renderer, SDL_RenderCommand *cmd)
{
    // TODO
    return 0;
}

static void OGC_SetBlendMode(SDL_Renderer *renderer, int blendMode)
{
    // TODO
}

static int OGC_RenderGeometry(SDL_Renderer *renderer, void *vertices, SDL_RenderCommand *cmd)
{
    // TODO
    return 0;
}

int OGC_RenderPrimitive(SDL_Renderer *renderer, u8 primitive,
                        void *vertices, SDL_RenderCommand *cmd)
{
    const size_t count = cmd->data.draw.count;
    const SDL_FPoint *verts = (SDL_FPoint *)(vertices + cmd->data.draw.first);

    const Uint8 r = cmd->data.draw.r;
    const Uint8 g = cmd->data.draw.g;
    const Uint8 b = cmd->data.draw.b;
    const Uint8 a = cmd->data.draw.a;
    OGC_SetBlendMode(renderer, cmd->data.draw.blend);

    /* TODO: optimize state changes. We can probably avoid passing the color,
     * if we set it on a register using GX_SetTevColor(). */
    GX_SetTevOp(GX_TEVSTAGE0, GX_PASSCLR);

    GX_ClearVtxDesc();
    GX_SetVtxDesc(GX_VA_POS, GX_DIRECT);
    GX_SetVtxDesc(GX_VA_CLR0, GX_DIRECT);
    GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS, GX_POS_XY, GX_F32, 0);
    GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_CLR0, GX_CLR_RGBA, GX_RGBA8, 0);

    GX_Begin(primitive, GX_VTXFMT0, count);
    for (int i = 0; i < count; i++) {
        GX_Position2f32(verts[i].x, verts[i].y);
        GX_Color4u8(r, g, b, a);
    }
    GX_End();

    return 0;
}

static int OGC_RunCommandQueue(SDL_Renderer *renderer, SDL_RenderCommand *cmd, void *vertices, size_t vertsize)
{
    while (cmd) {
        switch (cmd->command) {
        case SDL_RENDERCMD_SETVIEWPORT:
            OGC_RenderSetViewPort(renderer, cmd);
            break;
        case SDL_RENDERCMD_SETCLIPRECT:
            OGC_RenderSetClipRect(renderer, cmd);
            break;
        case SDL_RENDERCMD_SETDRAWCOLOR:
            OGC_RenderSetDrawColor(renderer, cmd);
            break;
        case SDL_RENDERCMD_CLEAR:
            OGC_RenderClear(renderer, cmd);
            break;
        case SDL_RENDERCMD_DRAW_POINTS:
            OGC_RenderPrimitive(renderer, GX_POINTS, vertices, cmd);
            break;
        case SDL_RENDERCMD_DRAW_LINES:
            OGC_RenderPrimitive(renderer, GX_LINESTRIP, vertices, cmd);
            break;
        case SDL_RENDERCMD_FILL_RECTS: /* unused */
            break;
        case SDL_RENDERCMD_COPY: /* unused */
            break;
        case SDL_RENDERCMD_COPY_EX: /* unused */
            break;
        case SDL_RENDERCMD_GEOMETRY:
            OGC_RenderGeometry(renderer, vertices, cmd);
            break;
        case SDL_RENDERCMD_NO_OP:
            break;
        }
        cmd = cmd->next;
    }
    return 0;
}

static int OGC_RenderReadPixels(SDL_Renderer *renderer, const SDL_Rect *rect,
                                Uint32 format, void *pixels, int pitch)
{
    return SDL_Unsupported();
}

static int OGC_RenderPresent(SDL_Renderer *renderer)
{
    GX_DrawDone();

    OGC_VideoFlip(renderer->window);
    return 0;
}

static void OGC_DestroyTexture(SDL_Renderer *renderer, SDL_Texture *texture)
{
}

static void OGC_DestroyRenderer(SDL_Renderer *renderer)
{
    OGC_RenderData *data = renderer->driverdata;

    if (data) {
        SDL_free(data);
    }

    SDL_free(renderer);
}

static int OGC_SetVSync(SDL_Renderer *renderer, const int vsync)
{
    return 0;
}

static SDL_Renderer *OGC_CreateRenderer(SDL_Window *window, Uint32 flags)
{
    SDL_Renderer *renderer;
    OGC_RenderData *data;

    renderer = (SDL_Renderer *)SDL_calloc(1, sizeof(*renderer));
    if (!renderer) {
        SDL_OutOfMemory();
        return NULL;
    }

    data = (OGC_RenderData *)SDL_calloc(1, sizeof(*data));
    if (!data) {
        OGC_DestroyRenderer(renderer);
        SDL_OutOfMemory();
        return NULL;
    }

    renderer->WindowEvent = OGC_WindowEvent;
    renderer->CreateTexture = OGC_CreateTexture;
    renderer->UpdateTexture = OGC_UpdateTexture;
    renderer->LockTexture = OGC_LockTexture;
    renderer->UnlockTexture = OGC_UnlockTexture;
    renderer->SetTextureScaleMode = OGC_SetTextureScaleMode;
    renderer->SetRenderTarget = OGC_SetRenderTarget;
    renderer->QueueSetViewport = OGC_QueueSetViewport;
    renderer->QueueSetDrawColor = OGC_QueueSetViewport;
    renderer->QueueDrawPoints = OGC_QueueDrawPoints;
    renderer->QueueDrawLines = OGC_QueueDrawPoints;
    renderer->QueueGeometry = OGC_QueueGeometry;
    renderer->RunCommandQueue = OGC_RunCommandQueue;
    renderer->RenderReadPixels = OGC_RenderReadPixels;
    renderer->RenderPresent = OGC_RenderPresent;
    renderer->DestroyTexture = OGC_DestroyTexture;
    renderer->DestroyRenderer = OGC_DestroyRenderer;
    renderer->SetVSync = OGC_SetVSync;
    renderer->info = OGC_RenderDriver.info;
    renderer->driverdata = data;
    renderer->window = window;

    return renderer;
}

SDL_RenderDriver OGC_RenderDriver = {
    .CreateRenderer = OGC_CreateRenderer,
    .info = {
        .name = "ogc",
        .flags = SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC | SDL_RENDERER_TARGETTEXTURE,
        .num_texture_formats = 2,
        .texture_formats = {
            [0] = SDL_PIXELFORMAT_RGB565,
            [1] = SDL_PIXELFORMAT_ARGB8888,
            // TODO: add more
        },
        .max_texture_width = 1024,
        .max_texture_height = 1024,
    }
};

#endif /* SDL_VIDEO_RENDER_OGC */

/* vi: set ts=4 sw=4 expandtab: */
