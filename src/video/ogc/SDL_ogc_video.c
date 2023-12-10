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

	Tantric, 2009
*/
#include "SDL_config.h"

// Standard includes.
#include <math.h>

// SDL internal includes.
#include "../SDL_sysvideo.h"
#include "../SDL_pixels_c.h"
#include "SDL_timer.h"
#include "SDL_thread.h"

// SDL ogc specifics.
#include <gccore.h>
#include <ogcsys.h>
#include <malloc.h>
#include <wiiuse/wpad.h>
#include "SDL_ogc_video.h"

#ifdef __wii__
#include "../wii/SDL_wiievents_c.h"
#endif

#ifdef __gamecube__
#include "../gamecube/SDL_gamecube_events_c.h"
#endif


#include <ogc/machine/processor.h>

static const char	OGCVID_DRIVER_NAME[] = "ogc-video";
static lwp_t videothread = LWP_THREAD_NULL;
static SDL_mutex *videomutex = NULL;
static SDL_cond *videocond = NULL;
static ogcVideo *current = NULL;

int vresx=0, vresy=0;

/*** SDL ***/
static SDL_Rect mode_320, mode_640;
#ifdef __wii__
static SDL_Rect mode_848;
#endif

static SDL_Rect* modes_descending[] =
{
#ifdef __wii__
	&mode_848,
#endif
	&mode_640,
	&mode_320,
	NULL
};

typedef struct private_hwdata {
	void *pixels;
	void *texture;
	u32 texture_size;
	bool texture_is_outdated;
	/* Number of GX operations that has veen performed on this surface. This
	 * value can be used to set the Z coordinate for the next operation, as
	 * well as to decide whether we need to call GX_DrawDone() when the surface
	 * gets locked. */
	s16 gx_op_count;
} OGC_Surface;

/*** 2D Video ***/
#define HASPECT 			320
#define VASPECT 			240

unsigned char *xfb[2] = { NULL, NULL };
int fb_index = 0;
GXRModeObj* vmode = 0;
static GXTexObj texobj_a, texobj_b;
static GXTlutObj texpalette_a, texpalette_b;

/*** GX ***/
#define DEFAULT_FIFO_SIZE 256 * 1024
static unsigned char gp_fifo[DEFAULT_FIFO_SIZE] __attribute__((aligned(32)));

/* New texture based scaler */
typedef struct tagcamera
{
	guVector pos;
	guVector up;
	guVector view;
}
camera;

/*** Square Matrix
     This structure controls the size of the image on the screen.
	 Think of the output as a -80 x 80 by -60 x 60 graph.
***/
static s16 square[] ATTRIBUTE_ALIGN (32) =
{
  /*
   * X,   Y,  Z
   * Values set are for roughly 4:3 aspect
   */
	0, 0, 0,
	HASPECT * 2, 0, 0,
	HASPECT * 2, VASPECT * 2, 0,
	0, VASPECT * 2, 0,
};

static const f32 tex_pos[] ATTRIBUTE_ALIGN(32) = {
	0.0, 0.0,
	1.0, 0.0,
	1.0, 1.0,
	0.0, 1.0,
};

static camera cam = {
	{0.0F, 0.0F, 0.0F},
	{0.0F, -0.5F, 0.0F},
	{0.0F, 0.0F, 0.5F}
};

static struct SDL_PixelFormat OGC_displayformatalphaPixel = {
	NULL, 32, 4,
	0, 0, 0, 0,
	24, 16, 8, 0,
	0xFF000000, 0x00FF0000, 0x0000FF00, 0x000000FF,
	0, 0,
};

/****************************************************************************
 * Scaler Support Functions
 ***************************************************************************/
static int currentwidth;
static int currentheight;
static int currentbpp;

static void
draw_init(void *palette, void *tex)
{
	Mtx m, mv, view;

	GX_ClearVtxDesc ();
	GX_SetVtxDesc (GX_VA_POS, GX_DIRECT);
	GX_SetVtxDesc (GX_VA_TEX0, GX_INDEX8);

	GX_SetVtxAttrFmt (GX_VTXFMT0, GX_VA_POS, GX_POS_XYZ, GX_S16, 0);
	GX_SetVtxAttrFmt (GX_VTXFMT0, GX_VA_TEX0, GX_TEX_ST, GX_F32, 0);

	GX_SetArray (GX_VA_TEX0, (void*)tex_pos, 2 * sizeof (f32));
	GX_SetNumTexGens (1);
	GX_SetNumChans (1);
	GX_SetChanCtrl(GX_COLOR0A0, GX_DISABLE, GX_SRC_VTX, GX_SRC_VTX, 0,
	               GX_DF_NONE, GX_AF_NONE);

	GX_SetTexCoordGen (GX_TEXCOORD0, GX_TG_MTX2x4, GX_TG_TEX0, GX_IDENTITY);

	GX_SetTevOp (GX_TEVSTAGE0, GX_REPLACE);
	GX_SetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD0, GX_TEXMAP0, GX_COLOR0A0);

	memset(&view, 0, sizeof (Mtx));
	guLookAt(view, &cam.pos, &cam.up, &cam.view);
	guMtxIdentity(m);
	guMtxTransApply(m, m, -HASPECT, -VASPECT, 1000);
	guMtxConcat(view, m, mv);
	GX_LoadPosMtxImm(mv, GX_PNMTX0);

	GX_InvVtxCache ();	// update vertex cache

	if (currentbpp == 8) {
		GX_InitTlutObj(&texpalette_a, palette, GX_TL_IA8, 256);
		GX_InitTlutObj(&texpalette_b, (Uint16*)palette+256, GX_TL_IA8, 256);
		DCStoreRange(palette, sizeof(512*sizeof(Uint16)));
		GX_LoadTlut(&texpalette_a, GX_TLUT0);
		GX_LoadTlut(&texpalette_b, GX_TLUT1);

		GX_InitTexObjCI(&texobj_a, tex, currentwidth, currentheight, GX_TF_CI8, GX_CLAMP, GX_CLAMP, 0, GX_TLUT0);
		GX_InitTexObjCI(&texobj_b, tex, currentwidth, currentheight, GX_TF_CI8, GX_CLAMP, GX_CLAMP, 0, GX_TLUT1);
		GX_LoadTexObj(&texobj_b, GX_TEXMAP1);

		// Setup TEV to combine Red+Green and Blue paletted images
		GX_SetTevColor(GX_TEVREG0, (GXColor){255, 255, 0, 0});
		GX_SetTevSwapModeTable(GX_TEV_SWAP1, GX_CH_RED, GX_CH_ALPHA, GX_CH_BLUE, GX_CH_ALPHA);
		GX_SetTevSwapModeTable(GX_TEV_SWAP2, GX_CH_ALPHA, GX_CH_ALPHA, GX_CH_BLUE, GX_CH_ALPHA);
		// first stage = red and green
		GX_SetTevSwapMode(GX_TEVSTAGE0, GX_TEV_SWAP0, GX_TEV_SWAP1);
		GX_SetTevColorIn(GX_TEVSTAGE0, GX_CC_ZERO, GX_CC_TEXC, GX_CC_C0, GX_CC_ZERO);
		// second stage = add blue (and opaque alpha)
		GX_SetTevOp(GX_TEVSTAGE1, GX_BLEND);
		GX_SetTevOrder(GX_TEVSTAGE1, GX_TEXCOORD0, GX_TEXMAP1, GX_COLORNULL);
		GX_SetTevSwapMode(GX_TEVSTAGE1, GX_TEV_SWAP0, GX_TEV_SWAP2);
		GX_SetTevColorIn(GX_TEVSTAGE1, GX_CC_TEXC, GX_CC_ZERO, GX_CC_ZERO, GX_CC_CPREV);
		GX_SetTevAlphaIn(GX_TEVSTAGE1, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO, GX_CA_KONST);

		GX_SetNumTevStages(2);
	}
	else if (currentbpp == 16)
		GX_InitTexObj(&texobj_a, tex, currentwidth, currentheight, GX_TF_RGB565, GX_CLAMP, GX_CLAMP, GX_FALSE);
	else
		GX_InitTexObj(&texobj_a, tex, currentwidth, currentheight, GX_TF_RGBA8, GX_CLAMP, GX_CLAMP, GX_FALSE);

	GX_LoadTexObj(&texobj_a, GX_TEXMAP0);	// load texture object so its ready to use
}

static inline void
draw_vert(u8 index, s16 z)
{
	GX_Position3s16 (square[index*3], square[index*3+1], z);
	GX_TexCoord1x8 (index);
}

static inline void
draw_square(s16 z)
{
	GX_Begin(GX_QUADS, GX_VTXFMT0, 4);
	draw_vert(0, z);
	draw_vert(1, z);
	draw_vert(2, z);
	draw_vert(3, z);
	GX_End();
}

static void
SetupGX()
{
	Mtx44 p;
	int df = 1; // deflicker on/off

	GX_SetCurrentGXThread();
	GX_SetViewport (0, 0, vmode->fbWidth, vmode->efbHeight, 0, 1);
	GX_SetDispCopyYScale ((f32) vmode->xfbHeight / (f32) vmode->efbHeight);
	GX_SetScissor (0, 0, vmode->fbWidth, vmode->efbHeight);

	GX_SetDispCopySrc(0, 0, vmode->fbWidth, vmode->efbHeight);
	GX_SetDispCopyDst(vmode->fbWidth, vmode->xfbHeight);
	GX_SetCopyFilter (vmode->aa, vmode->sample_pattern, (df == 1) ? GX_TRUE : GX_FALSE, vmode->vfilter);

	GX_SetFieldMode (vmode->field_rendering, ((vmode->viHeight == 2 * vmode->xfbHeight) ? GX_ENABLE : GX_DISABLE));
	GX_SetPixelFmt (GX_PF_RGB8_Z24, GX_ZC_LINEAR);
	GX_SetDispCopyGamma (GX_GM_1_0);
	GX_SetCullMode (GX_CULL_NONE);
	GX_SetBlendMode(GX_BM_BLEND, GX_BL_SRCALPHA, GX_BL_INVSRCALPHA, GX_LO_CLEAR);

	GX_SetZMode (GX_TRUE, GX_LEQUAL, GX_TRUE);
	GX_SetColorUpdate (GX_TRUE);
	GX_SetAlphaUpdate(GX_FALSE);

	guOrtho(p, VASPECT, -VASPECT, -HASPECT, HASPECT, 100, 1000); // matrix, t, b, l, r, n, f
	GX_LoadProjectionMtx (p, GX_ORTHOGRAPHIC);
	GX_Flush();
}

static u8 texture_format_from_SDL(const SDL_PixelFormat *format)
{
	switch (format->BitsPerPixel) {
	case 8:
		return GX_TF_CI8;
	case 16:
		return GX_TF_RGB565;
	case 32:
		return GX_TF_RGBA8;
	}
	return 0xff; // invalid
}

static inline void set_pixel_to_texture_32(int x, int y, u32 color, void *texture, int tex_width)
{
	u8 *tex = texture;
	u32 offset;

	offset = (((y >> 2) << 4) * tex_width) + ((x >> 2) << 6) + (((y % 4 << 2) + x % 4) << 1);

	*(tex + offset) = color;
	*(tex + offset + 1) = color >> 24;
	*(tex + offset + 32) = color >> 16;
	*(tex + offset + 33) = color >> 8;
}

static void pixels_to_texture_32(void *pixels, int16_t w, int16_t h,
								 int16_t pitch, void *texture)
{
	u32 *src = pixels;

	int tex_width = (w + 3) / 4 * 4;
	for (int y = 0; y < h; y++)
	{
		src = (u8*)pixels + pitch * y;
		for (int x = 0; x < w; x++)
		{
			set_pixel_to_texture_32(x, y, *src++, texture, tex_width);
		}
	}
}

static void pixels_to_texture_16(void *pixels, int16_t pitch, int16_t h,
                                 void *texture)
{
	long long int *dst = texture;
	long long int *src1 = pixels;
	long long int *src2 = (long long int *) ((char *)pixels + (pitch * 1));
	long long int *src3 = (long long int *) ((char *)pixels + (pitch * 2));
	long long int *src4 = (long long int *) ((char *)pixels + (pitch * 3));
	int rowpitch = (pitch >> 3) * 3;

	for (int y = 0; y < h; y += 4)
	{
		for (int x = 0; x < pitch; x += 8)
		{
			*dst++ = *src1++;
			*dst++ = *src2++;
			*dst++ = *src3++;
			*dst++ = *src4++;
		}

		src1 = src4;
		src2 += rowpitch;
		src3 += rowpitch;
		src4 += rowpitch;
	}
}

static void pixels_from_texture_16(void *pixels, int16_t pitch, int16_t h,
                                   void *texture)
{
	long long int *src = texture;
	long long int *dst1 = pixels;
	long long int *dst2 = (long long int *) ((char *)pixels + (pitch * 1));
	long long int *dst3 = (long long int *) ((char *)pixels + (pitch * 2));
	long long int *dst4 = (long long int *) ((char *)pixels + (pitch * 3));
	int rowpitch = (pitch >> 3) * 3;

	for (int y = 0; y < h; y += 4)
	{
		for (int x = 0; x < pitch; x += 8)
		{
			*dst1++ = *src++;
			*dst2++ = *src++;
			*dst3++ = *src++;
			*dst4++ = *src++;
		}

		dst1 = dst4;
		dst2 += rowpitch;
		dst3 += rowpitch;
		dst4 += rowpitch;
	}
}

static void pixels_to_texture(void *pixels, uint8_t gx_format,
							  int16_t w, int16_t h, int16_t pitch, void *texture)
{
	switch (gx_format) {
	case GX_TF_RGB565:
	case GX_TF_RGB5A3:
		pixels_to_texture_16(pixels, pitch, h, texture);
		break;
	case GX_TF_RGBA8:
		pixels_to_texture_32(pixels, w, h, pitch, texture);
		break;
	default:
		// TODO support more formats
	}
}

static void load_surface_texture(const SDL_Surface *surface)
{
	GXTexObj texobj_a, texobj_b;

	OGC_Surface *s = surface->hwdata;
	uint8_t gx_format = texture_format_from_SDL(surface->format);
	if (s->texture_is_outdated) {
		int16_t bytes_pp = surface->format->BytesPerPixel;
		int16_t bytes_per_pixel = bytes_pp > 2 ? 4 : bytes_pp;
		int16_t pitch = surface->pitch;
		pixels_to_texture(s->pixels, gx_format,
		                  surface->w, surface->h, pitch,
		                  s->texture);
		s->texture_is_outdated = false;
		DCStoreRange(s->texture, s->texture_size);
		GX_InvalidateTexAll();
	}

	int bpp = surface->format->BitsPerPixel;
	void *tex = surface->hwdata->texture;
	if (bpp == 8) {
		// TODO: handle palette
#if 0
		GX_InitTlutObj(&texpalette_a, palette, GX_TL_IA8, 256);
		GX_InitTlutObj(&texpalette_b, (Uint16*)palette+256, GX_TL_IA8, 256);
		DCStoreRange(palette, sizeof(512*sizeof(Uint16)));
		GX_LoadTlut(&texpalette_a, GX_TLUT0);
		GX_LoadTlut(&texpalette_b, GX_TLUT1);
#endif

		GX_InitTexObjCI(&texobj_a, tex, surface->w, surface->h, GX_TF_CI8, GX_CLAMP, GX_CLAMP, 0, GX_TLUT0);
		GX_InitTexObjCI(&texobj_b, tex, surface->w, surface->h, GX_TF_CI8, GX_CLAMP, GX_CLAMP, 0, GX_TLUT1);
		GX_LoadTexObj(&texobj_b, GX_TEXMAP1);

		// Setup TEV to combine Red+Green and Blue paletted images
		GX_SetTevColor(GX_TEVREG0, (GXColor){255, 255, 0, 0});
		GX_SetTevSwapModeTable(GX_TEV_SWAP1, GX_CH_RED, GX_CH_ALPHA, GX_CH_BLUE, GX_CH_ALPHA);
		GX_SetTevSwapModeTable(GX_TEV_SWAP2, GX_CH_ALPHA, GX_CH_ALPHA, GX_CH_BLUE, GX_CH_ALPHA);
		// first stage = red and green
		GX_SetTevSwapMode(GX_TEVSTAGE0, GX_TEV_SWAP0, GX_TEV_SWAP1);
		GX_SetTevColorIn(GX_TEVSTAGE0, GX_CC_ZERO, GX_CC_TEXC, GX_CC_C0, GX_CC_ZERO);
		// second stage = add blue (and opaque alpha)
		GX_SetTevOp(GX_TEVSTAGE1, GX_BLEND);
		GX_SetTevOrder(GX_TEVSTAGE1, GX_TEXCOORD0, GX_TEXMAP1, GX_COLORNULL);
		GX_SetTevSwapMode(GX_TEVSTAGE1, GX_TEV_SWAP0, GX_TEV_SWAP2);
		GX_SetTevColorIn(GX_TEVSTAGE1, GX_CC_TEXC, GX_CC_ZERO, GX_CC_ZERO, GX_CC_CPREV);
		GX_SetTevAlphaIn(GX_TEVSTAGE1, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO, GX_CA_KONST);

		GX_SetNumTevStages(2);
	} else if (bpp == 16) {
		GX_InitTexObj(&texobj_a, tex, surface->w, surface->h, GX_TF_RGB565, GX_CLAMP, GX_CLAMP, GX_FALSE);
	} else {
		GX_InitTexObj(&texobj_a, tex, surface->w, surface->h, GX_TF_RGBA8, GX_CLAMP, GX_CLAMP, GX_FALSE);
	}

	GX_InitTexObjLOD(&texobj_a, GX_NEAR, GX_NEAR, 0.0f, 0.0f, 0.0f, 0, 0, GX_ANISO_1);
	GX_LoadTexObj(&texobj_a, GX_TEXMAP0);	// load texture object so its ready to use
}

static void draw_screen_surface()
{
	if (SDL_VideoSurface->hwdata->texture_is_outdated) {
		load_surface_texture(SDL_VideoSurface);
		s16 z = -SDL_VideoSurface->hwdata->gx_op_count - 1;
		draw_square(z); // render textured quad
	}
	SDL_VideoSurface->hwdata->gx_op_count = 0;
}

static inline void ensure_screen_ready_for_hw_op()
{
	OGC_Surface *s = SDL_VideoSurface->hwdata;
	if (s->texture_is_outdated) {
		draw_screen_surface();
	}
}

void OGC_VideoStart(ogcVideo *private)
{
	if (private==NULL) {
		if (current==NULL)
			return;
		private = current;
	}

	SetupGX();
	draw_init(private->palette, private->texturemem);
#ifdef __wii__
	WPAD_SetVRes(WPAD_CHAN_0, vresx+vresx/4, vresy+vresy/4);
#endif
	current = private;
}

void OGC_VideoStop()
{
	if(videothread == LWP_THREAD_NULL)
		return;

	SDL_LockMutex(videomutex);
	SDL_CondSignal(videocond);
	SDL_UnlockMutex(videomutex);

	LWP_JoinThread(videothread, NULL);
	videothread = LWP_THREAD_NULL;
}

static int OGC_VideoInit(_THIS, SDL_PixelFormat *vformat)
{
	// Set up the modes.
#ifdef __wii__
	mode_848.w = 848;
	mode_848.h = 480;
#endif
	mode_640.w = 640;
	mode_640.h = 480;
	mode_320.w = 320;
	mode_320.h = 240;

	// Set the current format.
	vformat->BitsPerPixel	= 16;
	vformat->BytesPerPixel	= 2;

	this->hidden->buffer = NULL;
	this->hidden->texturemem = NULL;
	this->hidden->width = 0;
	this->hidden->height = 0;
	this->hidden->pitch = 0;

	this->displayformatalphapixel = &OGC_displayformatalphaPixel;

	this->info.blit_fill = 1;
	this->info.blit_hw = 1;
	this->info.blit_hw_A = 1;

	/* We're done! */
	return 0;
}

static SDL_Rect **OGC_ListModes(_THIS, SDL_PixelFormat *format, Uint32 flags)
{
	return modes_descending;
}

static SDL_Surface *OGC_SetVideoMode(_THIS, SDL_Surface *current,
								   int width, int height, int bpp, Uint32 flags)
{
	SDL_Rect* 		mode;
	size_t			bytes_per_pixel;
	Uint32			r_mask = 0;
	Uint32			b_mask = 0;
	Uint32			g_mask = 0;

	// Find a mode big enough to store the requested resolution
	mode = modes_descending[0];
	while (mode)
	{
		if (mode->w == width && mode->h == height)
			break;
		else
			++mode;
	}

	// Didn't find a mode?
	if (!mode)
	{
		SDL_SetError("Display mode (%dx%d) is unsupported.",
			width, height);
		return NULL;
	}

	if(bpp != 8 && bpp != 16 && bpp != 24 && bpp != 32)
	{
		SDL_SetError("Resolution (%d bpp) is unsupported (8/16/24/32 bpp only).",
			bpp);
		return NULL;
	}

	bytes_per_pixel = bpp / 8;

	OGC_VideoStop();

	free(this->hidden->buffer);
	free(this->hidden->texturemem);
	if (current->hwdata) {
		SDL_free(current->hwdata);
	}

	// Allocate the new buffer.
	this->hidden->buffer = memalign(32, width * height * bytes_per_pixel);
	if (!this->hidden->buffer )
	{
		this->hidden->texturemem = NULL;
		SDL_SetError("Couldn't allocate buffer for requested mode");
		return(NULL);
	}

	// Allocate texture memory
	if (bytes_per_pixel > 2)
		this->hidden->texturemem_size = width * height * 4;
	else
		this->hidden->texturemem_size = width * height * bytes_per_pixel;

	this->hidden->texturemem = memalign(32, this->hidden->texturemem_size);
	if (this->hidden->texturemem == NULL)
	{
		free(this->hidden->buffer);
		this->hidden->buffer = NULL;
		SDL_SetError("Couldn't allocate memory for texture");
		return NULL;
	}

	// Allocate the new pixel format for the screen
	if (!SDL_ReallocFormat(current, bpp, r_mask, g_mask, b_mask, 0))
	{
		free(this->hidden->buffer);
		this->hidden->buffer = NULL;
		free(this->hidden->texturemem);
		this->hidden->texturemem = NULL;

		SDL_UnlockMutex(videomutex);
		SDL_SetError("Couldn't allocate new pixel format for requested mode");
		return NULL;
	}

	// Clear the buffer
	SDL_memset(this->hidden->buffer, 0, width * height * bytes_per_pixel);
	SDL_memset(this->hidden->texturemem, 0, this->hidden->texturemem_size);

	// Set up the new mode framebuffer
	current->flags = flags & (SDL_FULLSCREEN | SDL_HWPALETTE | SDL_NOFRAME);
	// Our surface is always double buffered
	current->flags |= SDL_PREALLOC | SDL_DOUBLEBUF | SDL_HWSURFACE;
	current->w = width;
	current->h = height;
	OGC_Surface *s = SDL_malloc(sizeof(OGC_Surface));
	s->pixels = this->hidden->buffer;
	s->texture = this->hidden->texturemem;
	s->texture_size = this->hidden->texturemem_size;
	s->texture_is_outdated = false;
	s->gx_op_count = 0;
	current->hwdata = s;

	/* Set the hidden data */
	this->hidden->width = current->w;
	this->hidden->height = current->h;
	this->hidden->pitch = current->w * (bytes_per_pixel > 2 ? 4 : bytes_per_pixel);

	currentwidth = current->w;
	currentheight = current->h;
	currentbpp = bpp;
	vresx = currentwidth;
	vresy = currentheight;

	OGC_VideoStart(this->hidden);

	return current;
}

/* We don't actually allow hardware surfaces other than the main one */
static int OGC_AllocHWSurface(_THIS, SDL_Surface *surface)
{
	if (surface->w < 8 || surface->h < 8)
		return -1;

	OGC_Surface *s = SDL_malloc(sizeof(OGC_Surface));
	if (!s) goto oom_ogc_surface;
	s->pixels = SDL_malloc(surface->h * surface->pitch);
	if (!s->pixels) goto oom_pixel_buffer;
	u8 texture_format = texture_format_from_SDL(surface->format);
	s->texture_size = GX_GetTexBufferSize(surface->w, surface->h,
	                                      texture_format, GX_FALSE, 0);
	s->texture = memalign(32, s->texture_size);
	if (!s->texture) goto oom_texture;
	s->texture_is_outdated = false;
	s->gx_op_count = 0;
	surface->hwdata = s;
	surface->flags |= SDL_HWSURFACE | SDL_PREALLOC;
	surface->pixels = NULL;
	return 0;

oom_texture:
	SDL_free(s->pixels);
oom_pixel_buffer:
	SDL_free(s);
oom_ogc_surface:
	SDL_OutOfMemory();
	return -1;
}

static int OGC_HWAccelBlit(SDL_Surface *src, SDL_Rect *srcrect,
                           SDL_Surface *dst, SDL_Rect *dstrect)
{
	ensure_screen_ready_for_hw_op();

	// TODO: set u and v to match srcrect
	load_surface_texture(src);

	dst->hwdata->gx_op_count++;
	s16 z = -dst->hwdata->gx_op_count;

	GX_Begin(GX_QUADS, GX_VTXFMT0, 4);
	GX_Position3s16(dstrect->x,
	                dstrect->y,
	                z);
	GX_TexCoord1x8(0);
	GX_Position3s16(dstrect->x + dstrect->w,
	                dstrect->y,
	                z);
	GX_TexCoord1x8(1);
	GX_Position3s16(dstrect->x + dstrect->w,
	                dstrect->y + dstrect->h,
	                z);
	GX_TexCoord1x8(2);
	GX_Position3s16(dstrect->x,
	                dstrect->y + dstrect->h,
	                z);
	GX_TexCoord1x8(3);
	GX_End();

	/* It's not clear why we need this, but without it some textures appear
	 * corrupted, when there are no calls to SDL_LockSurface/SDL_UnlockSurface.
	 */
	GX_DrawDone();
	return 0;
}

static int OGC_CheckHWBlit(_THIS, SDL_Surface *src, SDL_Surface *dst)
{
	/* For the time being, only accelerate blits to the screen surface */
	if (dst != SDL_VideoSurface) {
		return false;
	}

	src->flags |= SDL_HWACCEL;
	src->map->hw_blit = OGC_HWAccelBlit;
	return true;
}

static int OGC_FillHWRect(_THIS, SDL_Surface *dst, SDL_Rect *rect,
                          Uint32 color)
{
	if (dst != SDL_VideoSurface) {
		/* Perform a software fill. Reinvoke SDL_FillRect() in this way is
		 * rather hacky, but it works. */

		this->info.blit_fill = 0;
		SDL_FillRect(dst, rect, color);
		this->info.blit_fill = 1;
		return 0;
	}

	ensure_screen_ready_for_hw_op();

	/* SDL tries to be helpful in passing us the color formatted according to
	 * the surface, but for us it's easier to work with the decomposed values
	 */
	u8 r, g, b;
	SDL_GetRGB(color, dst->format, &r, &g, &b);

	GX_SetTevOp(GX_TEVSTAGE0, GX_PASSCLR);

	GX_ClearVtxDesc();
	GX_SetVtxDesc(GX_VA_POS, GX_DIRECT);
	GX_SetVtxDesc(GX_VA_CLR0, GX_DIRECT);
	GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS, GX_POS_XYZ, GX_S16, 0);
	GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_CLR0, GX_CLR_RGB, GX_RGB8, 0);

	dst->hwdata->gx_op_count++;
	s16 z = -dst->hwdata->gx_op_count;

	GX_Begin(GX_QUADS, GX_VTXFMT0, 4);
	GX_Position3s16(rect->x, rect->y, z);
	GX_Color3u8(r, g, b);
	GX_Position3s16(rect->x + rect->w, rect->y, z);
	GX_Color3u8(r, g, b);
	GX_Position3s16(rect->x + rect->w, rect->y + rect->h, z);
	GX_Color3u8(r, g, b);
	GX_Position3s16(rect->x, rect->y + rect->h, z);
	GX_Color3u8(r, g, b);
	GX_End();

	/* Restore stuff as it was. TODO: make a function, or move it somewhere
	 * else (before blitting a texture; that could save some cycles) */
	GX_SetTevOp(GX_TEVSTAGE0, GX_REPLACE);
	GX_ClearVtxDesc();
	GX_SetVtxDesc(GX_VA_POS, GX_DIRECT);
	GX_SetVtxDesc(GX_VA_TEX0, GX_INDEX8);

	GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS, GX_POS_XYZ, GX_S16, 0);
	GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_TEX0, GX_TEX_ST, GX_F32, 0);
	return 0;
}

static void OGC_FreeHWSurface(_THIS, SDL_Surface *surface)
{
	OGC_Surface *s = surface->hwdata;
	if (s->pixels) {
		free(s->pixels);
	}
	if (s->texture) {
		free(s->texture);
	}
	SDL_free(s);
	surface->hwdata = NULL;
}

static int OGC_LockHWSurface(_THIS, SDL_Surface *surface)
{
	OGC_Surface *s = surface->hwdata;
	if (s->gx_op_count > 0) {
		if (surface != SDL_VideoSurface) exit(0);
		/* Flush the GX drawing done so far */
		GX_DrawDone();

		uint8_t *texture;
		texture = s->texture;
		/* Then copy the EFB onto the surface's texture */
		GX_SetTexCopySrc(0, 0, surface->w, surface->h);
		// TODO: use the appropriate format for the screen surface
		GX_SetTexCopyDst(surface->w, surface->h, GX_TF_RGB565, GX_FALSE);
		GX_SetCopyFilter (GX_FALSE, NULL, GX_FALSE, NULL);
		GX_CopyTex(texture, GX_TRUE);
		GX_PixModeSync(); // TODO: figure out if this is really needed
		GX_SetDrawDone();
		DCInvalidateRange(texture, s->texture_size);
		GX_WaitDrawDone();

		/* Finally, convert the texture data into the surface's pixels
		 * framebuffer. */
		// TODO: support other bit depths
		int16_t bytes_pp = surface->format->BytesPerPixel;
		int16_t bytes_per_pixel = bytes_pp > 2 ? 4 : bytes_pp;
		int16_t pitch = surface->w * bytes_per_pixel;
		pixels_from_texture_16(s->pixels, pitch, surface->h, texture);

		s->gx_op_count = 0;
	}

	surface->pixels = s->pixels;
	surface->pitch = surface->w * surface->format->BytesPerPixel;
	return 0;
}

static void OGC_UnlockHWSurface(_THIS, SDL_Surface *surface)
{
	surface->pixels = NULL;
	OGC_Surface *s = surface->hwdata;
	s->texture_is_outdated = true;
	return;
}

static inline void Set_RGBAPixel(_THIS, int x, int y, u32 color)
{
	u8 *truc = this->hidden->texturemem;
	int width = this->hidden->width;
	u32 offset;

	offset = (((y >> 2) << 4) * width) + ((x >> 2) << 6) + ((((y & 3) << 2) + (x & 3)) << 1);

	*(truc + offset) = color;
	*(truc + offset + 1) = color >> 24;
	*(truc + offset + 32) = color >> 16;
	*(truc + offset + 33) = color >> 8;
}

static inline void Set_RGB565Pixel(_THIS, int x, int y, u16 color)
{
	u8 *truc = this->hidden->texturemem;
	int width = this->hidden->width;
	u32 offset;

	offset = (((y >> 2) << 3) * width) + ((x >> 2) << 5) + ((((y & 3) << 2) + (x & 3)) << 1);

	*(truc + offset) = color >> 8;
	*(truc + offset + 1) = color;
}

static inline void Set_PalPixel(_THIS, int x, int y, u8 color)
{
	u8 *truc = this->hidden->texturemem;
	int width = this->hidden->pitch;
	u32 offset;

	offset = ((y & ~3) * width) + ((x & ~7) << 2) + ((y & 3) << 3) + (x & 7);

	truc[offset] = color;
}

static void UpdateRect_8(_THIS, SDL_Rect *rect)
{
	u8 *src;
	u8 color;
	int i, j;

	for (i = 0; i < rect->h; i++)
	{
		src = (this->hidden->buffer + (this->hidden->width * (i + rect->y)) + rect->x);
		for (j = 0; j < rect->w; j++)
		{
			color = src[j];
			Set_PalPixel(this, rect->x + j, rect->y + i, color);
		}
	}
}

static void UpdateRect_16(_THIS, SDL_Rect *rect)
{
	u8 *src;
	u8 *ptr;
	u16 color;
	int i, j;
	for (i = 0; i < rect->h; i++)
	{
		src = (this->hidden->buffer + (this->hidden->width * 2 * (i + rect->y)) + (rect->x * 2));
		for (j = 0; j < rect->w; j++)
		{
			ptr = src + (j * 2);
			color = (ptr[0] << 8) | ptr[1];
			Set_RGB565Pixel(this, rect->x + j, rect->y + i, color);
		}
	}
}

static void UpdateRect_24(_THIS, SDL_Rect *rect)
{
	u8 *src;
	u8 *ptr;
	u32 color;
	int i, j;
	for (i = 0; i < rect->h; i++)
	{
		src = (this->hidden->buffer + (this->hidden->width * 3 * (i + rect->y)) + (rect->x * 3));
		for (j = 0; j < rect->w; j++)
		{
			ptr = src + (j * 3);
			color = (ptr[0] << 24) | (ptr[1] << 16) | (ptr[2] << 8) | 0xff;
			Set_RGBAPixel(this, rect->x + j, rect->y + i, color);
		}
	}
}

static void UpdateRect_32(_THIS, SDL_Rect *rect)
{
	u8 *src;
	u8 *ptr;
	u32 color;
	int i, j;
	for (i = 0; i < rect->h; i++)
	{
		src = (this->hidden->buffer + (this->hidden->width * 4 * (i + rect->y)) + (rect->x * 4));
		for (j = 0; j < rect->w; j++)
		{
			ptr = src + (j * 4);
			color = (ptr[1] << 24) | (ptr[2] << 16) | (ptr[3] << 8) | ptr[0];
			Set_RGBAPixel(this, rect->x + j, rect->y + i, color);
		}
	}
}

static void flipHWSurface_16_16(_THIS, const SDL_Surface* const surface)
{
	draw_screen_surface();

	// TODO: move df to *this
	int df = 1; // deflicker on/off
	GX_SetCopyFilter (vmode->aa, vmode->sample_pattern,
	                  (df == 1) ? GX_TRUE : GX_FALSE, vmode->vfilter);
	GX_DrawDone();
	GX_InvalidateTexAll();

	GX_CopyDisp(xfb[fb_index], GX_TRUE);
	GX_DrawDone();

	VIDEO_SetNextFramebuffer(xfb[fb_index]);
	VIDEO_Flush();
	VIDEO_WaitVSync();

	fb_index ^= 1;
}

static void OGC_UpdateRect(_THIS, SDL_Rect *rect)
{
	const SDL_Surface* const screen = this->screen;

	switch(screen->format->BytesPerPixel) {
	case 1:
		UpdateRect_8(this, rect);
		break;
	case 2:
		UpdateRect_16(this, rect);
		break;
	case 3:
		UpdateRect_24(this, rect);
		break;
	case 4:
		UpdateRect_32(this, rect);
		break;
	default:
		fprintf(stderr, "Invalid BPP %d\n", screen->format->BytesPerPixel);
		break;
	}
}

static void OGC_UpdateRects(_THIS, int numrects, SDL_Rect *rects)
{
	int i;

	// note that this function doesn't lock - we don't care if this isn't
	// rendered now, that's what Flip is for

	for (i = 0; i < numrects; i++)
	{
		OGC_UpdateRect(this, rects+i);
	}

	SDL_CondSignal(videocond);
}

static void flipHWSurface_24_16(_THIS, SDL_Surface *surface)
{
	SDL_Rect screen_rect = {0, 0, this->hidden->width, this->hidden->height};
	OGC_UpdateRects(this, 1, &screen_rect);
}

static void flipHWSurface_32_16(_THIS, SDL_Surface *surface)
{
	SDL_Rect screen_rect = {0, 0, this->hidden->width, this->hidden->height};
	OGC_UpdateRects(this, 1, &screen_rect);
}

static int OGC_FlipHWSurface(_THIS, SDL_Surface *surface)
{
	switch(surface->format->BytesPerPixel)
	{
		case 1:
		case 2:
			// 8 and 16 bit use the same tile format
			flipHWSurface_16_16(this, surface);
			break;
		case 3:
			flipHWSurface_24_16(this, surface);
			break;
		case 4:
			flipHWSurface_32_16(this, surface);
			break;
		default:
			return -1;
	}
	return 0;
}

static int OGC_SetColors(_THIS, int first_color, int color_count, SDL_Color *colors)
{
	const int last_color = first_color + color_count;
	Uint16* const palette = this->hidden->palette;
	int     component;

	SDL_LockMutex(videomutex);

	/* Build the RGB24 palette. */
	for (component = first_color; component != last_color; ++component, ++colors)
	{
		palette[component] = (colors->g << 8) | colors->r;
		palette[component+256] = colors->b;
	}

	DCStoreRangeNoSync(palette+first_color, color_count*sizeof(Uint16));
	DCStoreRange(palette+first_color+256, color_count*sizeof(Uint16));
	GX_LoadTlut(&texpalette_a, GX_TLUT0);
	GX_LoadTlut(&texpalette_b, GX_TLUT1);
	GX_LoadTexObj(&texobj_a, GX_TEXMAP0);
	GX_LoadTexObj(&texobj_b, GX_TEXMAP1);

	SDL_UnlockMutex(videomutex);

	return(1);
}

static void OGC_VideoQuit(_THIS)
{
	OGC_VideoStop();
	GX_AbortFrame();
	GX_Flush();

	current = NULL;

	VIDEO_SetBlack(TRUE);
	VIDEO_Flush();
}

static void OGC_DeleteDevice(SDL_VideoDevice *device)
{
	free(device->hidden);
	SDL_free(device);

	SDL_DestroyCond(videocond);
	videocond = 0;
	SDL_DestroyMutex(videomutex);
	videomutex=0;
}

static SDL_VideoDevice *OGC_CreateDevice(int devindex)
{
	SDL_VideoDevice *device;

	/* Initialize all variables that we clean on shutdown */
	device = (SDL_VideoDevice *)SDL_malloc(sizeof(SDL_VideoDevice));
	if ( device ) {
		SDL_memset(device, 0, (sizeof *device));
		device->hidden = (struct SDL_PrivateVideoData *)
			memalign(32, sizeof(struct SDL_PrivateVideoData));
	}
	if ( (device == NULL) || (device->hidden == NULL) ) {
		SDL_OutOfMemory();
		if ( device ) {
			SDL_free(device);
		}
		return(0);
	}
	SDL_memset(device->hidden, 0, (sizeof *device->hidden));

	videomutex = SDL_CreateMutex();
	videocond = SDL_CreateCond();

	/* Set the function pointers */
	device->VideoInit = OGC_VideoInit;
	device->ListModes = OGC_ListModes;
	device->SetVideoMode = OGC_SetVideoMode;
	device->SetColors = OGC_SetColors;
	device->UpdateRects = OGC_UpdateRects;
	device->VideoQuit = OGC_VideoQuit;
	device->AllocHWSurface = OGC_AllocHWSurface;
	device->CheckHWBlit = OGC_CheckHWBlit;
	device->FillHWRect = OGC_FillHWRect;
	device->LockHWSurface = OGC_LockHWSurface;
	device->UnlockHWSurface = OGC_UnlockHWSurface;
	device->FlipHWSurface = OGC_FlipHWSurface;
	device->FreeHWSurface = OGC_FreeHWSurface;
#ifdef __wii__
	device->InitOSKeymap = WII_InitOSKeymap;
	device->PumpEvents = WII_PumpEvents;
#endif
#ifdef __gamecube__
	device->InitOSKeymap = GAMECUBE_InitOSKeymap;
	device->PumpEvents = GAMECUBE_PumpEvents;
#endif
	device->input_grab = SDL_GRAB_ON;

	device->free = OGC_DeleteDevice;

	OGC_InitVideoSystem();
	return device;
}

static int OGC_Available(void)
{
	return(1);
}

VideoBootStrap OGC_bootstrap = {
	OGCVID_DRIVER_NAME, "ogc video driver",
	OGC_Available, OGC_CreateDevice
};

void
OGC_InitVideoSystem()
{
	/* Initialise the video system */
	VIDEO_Init();
	vmode = VIDEO_GetPreferredMode(NULL);

	/* Set up the video system with the chosen mode */
	if (vmode == &TVPal528IntDf)
		vmode = &TVPal576IntDfScale;

	VIDEO_Configure(vmode);

	// Allocate the video buffer
	if (xfb[0]) free(MEM_K1_TO_K0(xfb[0]));
	if (xfb[1]) free(MEM_K1_TO_K0(xfb[1]));
	xfb[0] = (unsigned char*) MEM_K0_TO_K1(SYS_AllocateFramebuffer(vmode));
	xfb[1] = (unsigned char*) MEM_K0_TO_K1(SYS_AllocateFramebuffer(vmode));

	VIDEO_ClearFrameBuffer(vmode, xfb[0], COLOR_BLACK);
	VIDEO_SetNextFramebuffer(xfb[0]);

	// Show the screen.
	VIDEO_SetBlack(FALSE);
	VIDEO_Flush();
	VIDEO_WaitVSync(); VIDEO_WaitVSync();

	//CON_Init(xfb,20,20,vmode->fbWidth,vmode->xfbHeight,vmode->fbWidth*VI_DISPLAY_PIX_SZ);

	/*** Clear out FIFO area ***/
	memset(&gp_fifo, 0, DEFAULT_FIFO_SIZE);

	/*** Initialise GX ***/
	GX_Init(&gp_fifo, DEFAULT_FIFO_SIZE);

	GXColor background = { 0, 0, 0, 0xff };
	GX_SetCopyClear (background, GX_MAX_Z24);

	SetupGX();
}

void OGC_SetWidescreen(int wide)
{
	int width;
	if(wide) {
		width = 678;
	}
	else
		width = 640;

	vmode->viWidth = width;
	vmode->viXOrigin = (VI_MAX_WIDTH_NTSC - width) / 2;

	VIDEO_Configure (vmode);

	if (xfb[0])
		VIDEO_ClearFrameBuffer(vmode, xfb[0], COLOR_BLACK);

	VIDEO_Flush();

	VIDEO_WaitVSync(); VIDEO_WaitVSync();
}

void OGC_ChangeSquare(int xscale, int yscale, int xshift, int yshift)
{
	square[6] = square[3]  =  xscale + xshift;
	square[0] = square[9]  = -xscale + xshift;
	square[4] = square[1]  =  yscale - yshift;
	square[7] = square[10] = -yscale - yshift;
	DCFlushRange (square, 32); // update memory BEFORE the GPU accesses it!
	GX_InvVtxCache();
}
