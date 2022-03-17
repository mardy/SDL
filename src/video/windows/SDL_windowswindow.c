/*
  Simple DirectMedia Layer
  Copyright (C) 1997-2022 Sam Lantinga <slouken@libsdl.org>

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

#if SDL_VIDEO_DRIVER_WINDOWS

#include "../../core/windows/SDL_windows.h"

#include "../SDL_sysvideo.h"
#include "../SDL_pixels_c.h"
#include "../../events/SDL_keyboard_c.h"
#include "../../events/SDL_mouse_c.h"

#include "SDL_windowsvideo.h"
#include "SDL_windowswindow.h"
#include "SDL_hints.h"
#include "SDL_timer.h"

/* Dropfile support */
#include <shellapi.h>

/* This is included after SDL_windowsvideo.h, which includes windows.h */
#include "SDL_syswm.h"

/* Windows CE compatibility */
#ifndef SWP_NOCOPYBITS
#define SWP_NOCOPYBITS 0
#endif

/* Fake window to help with DirectInput events. */
HWND SDL_HelperWindow = NULL;
static const TCHAR *SDL_HelperWindowClassName = TEXT("SDLHelperWindowInputCatcher");
static const TCHAR *SDL_HelperWindowName = TEXT("SDLHelperWindowInputMsgWindow");
static ATOM SDL_HelperWindowClass = 0;

/* For borderless Windows, still want the following flag:
   - WS_MINIMIZEBOX: window will respond to Windows minimize commands sent to all windows, such as windows key + m, shaking title bar, etc.
   Additionally, non-fullscreen windows can add:
   - WS_CAPTION: this seems to enable the Windows minimize animation
   - WS_SYSMENU: enables system context menu on task bar
   This will also cause the task bar to overlap the window and other windowed behaviors, so only use this for windows that shouldn't appear to be fullscreen
 */

#define STYLE_BASIC         (WS_CLIPSIBLINGS | WS_CLIPCHILDREN)
#define STYLE_FULLSCREEN    (WS_POPUP | WS_MINIMIZEBOX)
#define STYLE_BORDERLESS    (WS_POPUP | WS_MINIMIZEBOX)
#define STYLE_BORDERLESS_WINDOWED (WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX)
#define STYLE_NORMAL        (WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX)
#define STYLE_RESIZABLE     (WS_THICKFRAME | WS_MAXIMIZEBOX)
#define STYLE_MASK          (STYLE_FULLSCREEN | STYLE_BORDERLESS | STYLE_NORMAL | STYLE_RESIZABLE)

static DWORD
GetWindowStyle(SDL_Window * window)
{
    DWORD style = 0;

    if (window->flags & SDL_WINDOW_FULLSCREEN) {
        style |= STYLE_FULLSCREEN;
    } else {
        if (window->flags & SDL_WINDOW_BORDERLESS) {
            /* SDL 2.1:
               This behavior more closely matches other platform where the window is borderless
               but still interacts with the window manager (e.g. task bar shows above it, it can
               be resized to fit within usable desktop area, etc.) so this should be the behavior
               for a future SDL release.

               If you want a borderless window the size of the desktop that looks like a fullscreen
               window, then you should use the SDL_WINDOW_FULLSCREEN_DESKTOP flag.
             */
            if (SDL_GetHintBoolean("SDL_BORDERLESS_WINDOWED_STYLE", SDL_FALSE)) {
                style |= STYLE_BORDERLESS_WINDOWED;
            } else {
                style |= STYLE_BORDERLESS;
            }
        } else {
            style |= STYLE_NORMAL;
        }

        if (window->flags & SDL_WINDOW_RESIZABLE) {
            /* You can have a borderless resizable window, but Windows doesn't always draw it correctly,
               see https://bugzilla.libsdl.org/show_bug.cgi?id=4466
             */
            if (!(window->flags & SDL_WINDOW_BORDERLESS) ||
                SDL_GetHintBoolean("SDL_BORDERLESS_RESIZABLE_STYLE", SDL_FALSE)) {
                style |= STYLE_RESIZABLE;
            }
        }

        /* Need to set initialize minimize style, or when we call ShowWindow with WS_MINIMIZE it will activate a random window */
        if (window->flags & SDL_WINDOW_MINIMIZED) {
            style |= WS_MINIMIZE;
        }
    }
    return style;
}

static void
WIN_AdjustWindowRectWithStyle(SDL_Window *window, DWORD style, BOOL menu, int *x, int *y, int *width, int *height, SDL_bool use_current)
{
    RECT rect;

    rect.left = 0;
    rect.top = 0;
    rect.right = (use_current ? window->w : window->windowed.w);
    rect.bottom = (use_current ? window->h : window->windowed.h);

    /* borderless windows will have WM_NCCALCSIZE return 0 for the non-client area. When this happens, it looks like windows will send a resize message
       expanding the window client area to the previous window + chrome size, so shouldn't need to adjust the window size for the set styles.
     */
    if (!(window->flags & SDL_WINDOW_BORDERLESS))
        AdjustWindowRectEx(&rect, style, menu, 0);

    *x = (use_current ? window->x : window->windowed.x) + rect.left;
    *y = (use_current ? window->y : window->windowed.y) + rect.top;
    *width = (rect.right - rect.left);
    *height = (rect.bottom - rect.top);
}

static void
WIN_AdjustWindowRect(SDL_Window *window, int *x, int *y, int *width, int *height, SDL_bool use_current)
{
    SDL_WindowData *data = (SDL_WindowData *)window->driverdata;
    HWND hwnd = data->hwnd;
    DWORD style;
    BOOL menu;

    style = GetWindowLong(hwnd, GWL_STYLE);
    menu = (style & WS_CHILDWINDOW) ? FALSE : (GetMenu(hwnd) != NULL);
    WIN_AdjustWindowRectWithStyle(window, style, menu, x, y, width, height, use_current);
}

static void
WIN_SetWindowPositionInternal(_THIS, SDL_Window * window, UINT flags)
{
    SDL_WindowData *data = (SDL_WindowData *)window->driverdata;
    HWND hwnd = data->hwnd;
    HWND top;
    int x, y;
    int w, h;

    /* Figure out what the window area will be */
    if (SDL_ShouldAllowTopmost() && ((window->flags & (SDL_WINDOW_FULLSCREEN|SDL_WINDOW_INPUT_FOCUS)) == (SDL_WINDOW_FULLSCREEN|SDL_WINDOW_INPUT_FOCUS) || (window->flags & SDL_WINDOW_ALWAYS_ON_TOP))) {
        top = HWND_TOPMOST;
    } else {
        top = HWND_NOTOPMOST;
    }

    WIN_AdjustWindowRect(window, &x, &y, &w, &h, SDL_TRUE);

    data->expected_resize = SDL_TRUE;
    SetWindowPos(hwnd, top, x, y, w, h, flags);
    data->expected_resize = SDL_FALSE;
}

static int
SetupWindowData(_THIS, SDL_Window * window, HWND hwnd, HWND parent, SDL_bool created)
{
    SDL_VideoData *videodata = (SDL_VideoData *) _this->driverdata;
    SDL_WindowData *data;

    /* Allocate the window data */
    data = (SDL_WindowData *) SDL_calloc(1, sizeof(*data));
    if (!data) {
        return SDL_OutOfMemory();
    }
    data->window = window;
    data->hwnd = hwnd;
    data->parent = parent;
    data->hdc = GetDC(hwnd);
    data->hinstance = (HINSTANCE) GetWindowLongPtr(hwnd, GWLP_HINSTANCE);
    data->created = created;
    data->high_surrogate = 0;
    data->mouse_button_flags = (WPARAM)-1;
    data->last_pointer_update = (LPARAM)-1;
    data->videodata = videodata;
    data->initializing = SDL_TRUE;

    window->driverdata = data;

    /* Associate the data with the window */
    if (!SetProp(hwnd, TEXT("SDL_WindowData"), data)) {
        ReleaseDC(hwnd, data->hdc);
        SDL_free(data);
        return WIN_SetError("SetProp() failed");
    }

    /* Set up the window proc function */
#ifdef GWLP_WNDPROC
    data->wndproc = (WNDPROC) GetWindowLongPtr(hwnd, GWLP_WNDPROC);
    if (data->wndproc == WIN_WindowProc) {
        data->wndproc = NULL;
    } else {
        SetWindowLongPtr(hwnd, GWLP_WNDPROC, (LONG_PTR) WIN_WindowProc);
    }
#else
    data->wndproc = (WNDPROC) GetWindowLong(hwnd, GWL_WNDPROC);
    if (data->wndproc == WIN_WindowProc) {
        data->wndproc = NULL;
    } else {
        SetWindowLong(hwnd, GWL_WNDPROC, (LONG_PTR) WIN_WindowProc);
    }
#endif

    /* Fill in the SDL window with the window data */
    {
        RECT rect;
        if (GetClientRect(hwnd, &rect)) {
            int w = rect.right;
            int h = rect.bottom;
            if ((window->windowed.w && window->windowed.w != w) || (window->windowed.h && window->windowed.h != h)) {
                /* We tried to create a window larger than the desktop and Windows didn't allow it.  Override! */
                int x, y;
                /* Figure out what the window area will be */
                WIN_AdjustWindowRect(window, &x, &y, &w, &h, SDL_FALSE);
                SetWindowPos(hwnd, HWND_NOTOPMOST, x, y, w, h, SWP_NOCOPYBITS | SWP_NOZORDER | SWP_NOACTIVATE);
            } else {
                window->w = w;
                window->h = h;
            }
        }
    }
    {
        POINT point;
        point.x = 0;
        point.y = 0;
        if (ClientToScreen(hwnd, &point)) {
            window->x = point.x;
            window->y = point.y;
        }
    }
    {
        DWORD style = GetWindowLong(hwnd, GWL_STYLE);
        if (style & WS_VISIBLE) {
            window->flags |= SDL_WINDOW_SHOWN;
        } else {
            window->flags &= ~SDL_WINDOW_SHOWN;
        }
        if (style & WS_POPUP) {
            window->flags |= SDL_WINDOW_BORDERLESS;
        } else {
            window->flags &= ~SDL_WINDOW_BORDERLESS;
        }
        if (style & WS_THICKFRAME) {
            window->flags |= SDL_WINDOW_RESIZABLE;
        } else {
            window->flags &= ~SDL_WINDOW_RESIZABLE;
        }
#ifdef WS_MAXIMIZE
        if (style & WS_MAXIMIZE) {
            window->flags |= SDL_WINDOW_MAXIMIZED;
        } else
#endif
        {
            window->flags &= ~SDL_WINDOW_MAXIMIZED;
        }
#ifdef WS_MINIMIZE
        if (style & WS_MINIMIZE) {
            window->flags |= SDL_WINDOW_MINIMIZED;
        } else
#endif
        {
            window->flags &= ~SDL_WINDOW_MINIMIZED;
        }
    }
    if (GetFocus() == hwnd) {
        window->flags |= SDL_WINDOW_INPUT_FOCUS;
        SDL_SetKeyboardFocus(window);
        WIN_UpdateClipCursor(window);
    }

    /* Enable multi-touch */
    if (videodata->RegisterTouchWindow) {
        videodata->RegisterTouchWindow(hwnd, (TWF_FINETOUCH|TWF_WANTPALM));
    }

    data->initializing = SDL_FALSE;

    /* All done! */
    return 0;
}



int
WIN_CreateWindow(_THIS, SDL_Window * window)
{
    HWND hwnd, parent = NULL;
    DWORD style = STYLE_BASIC;
    int x, y;
    int w, h;

    if (window->flags & SDL_WINDOW_SKIP_TASKBAR) {
        parent = CreateWindow(SDL_Appname, TEXT(""), STYLE_BASIC, 0, 0, 32, 32, NULL, NULL, SDL_Instance, NULL);
    }

    style |= GetWindowStyle(window);

    /* Figure out what the window area will be */
    WIN_AdjustWindowRectWithStyle(window, style, FALSE, &x, &y, &w, &h, SDL_FALSE);

    hwnd =
        CreateWindow(SDL_Appname, TEXT(""), style, x, y, w, h, parent, NULL,
                     SDL_Instance, NULL);
    if (!hwnd) {
        return WIN_SetError("Couldn't create window");
    }

    WIN_PumpEvents(_this);

    if (SetupWindowData(_this, window, hwnd, parent, SDL_TRUE) < 0) {
        DestroyWindow(hwnd);
        if (parent) {
            DestroyWindow(parent);
        }
        return -1;
    }

    /* Inform Windows of the frame change so we can respond to WM_NCCALCSIZE */
    SetWindowPos(hwnd, NULL, 0, 0, 0, 0, SWP_FRAMECHANGED | SWP_NOSIZE | SWP_NOZORDER | SWP_NOMOVE | SWP_NOACTIVATE);

    if (window->flags & SDL_WINDOW_MINIMIZED) {
        ShowWindow(hwnd, SW_SHOWMINNOACTIVE);
    }

    if (!(window->flags & SDL_WINDOW_OPENGL)) {
        return 0;
    }

    /* The rest of this macro mess is for OpenGL or OpenGL ES windows */
#if SDL_VIDEO_OPENGL_ES2
    if (_this->gl_config.profile_mask == SDL_GL_CONTEXT_PROFILE_ES
#if SDL_VIDEO_OPENGL_WGL
        && (!_this->gl_data || WIN_GL_UseEGL(_this))
#endif /* SDL_VIDEO_OPENGL_WGL */
    ) {
#if SDL_VIDEO_OPENGL_EGL
        if (WIN_GLES_SetupWindow(_this, window) < 0) {
            WIN_DestroyWindow(_this, window);
            return -1;
        }
        return 0;
#else
        return SDL_SetError("Could not create GLES window surface (EGL support not configured)");
#endif /* SDL_VIDEO_OPENGL_EGL */
    }
#endif /* SDL_VIDEO_OPENGL_ES2 */

#if SDL_VIDEO_OPENGL_WGL
    if (WIN_GL_SetupWindow(_this, window) < 0) {
        WIN_DestroyWindow(_this, window);
        return -1;
    }
#else
    return SDL_SetError("Could not create GL window (WGL support not configured)");
#endif

    return 0;
}

int
WIN_CreateWindowFrom(_THIS, SDL_Window * window, const void *data)
{
    HWND hwnd = (HWND) data;
    LPTSTR title;
    int titleLen;
    SDL_bool isstack;

    /* Query the title from the existing window */
    titleLen = GetWindowTextLength(hwnd);
    title = SDL_small_alloc(TCHAR, titleLen + 1, &isstack);
    if (title) {
        titleLen = GetWindowText(hwnd, title, titleLen + 1);
    } else {
        titleLen = 0;
    }
    if (titleLen > 0) {
        window->title = WIN_StringToUTF8(title);
    }
    if (title) {
        SDL_small_free(title, isstack);
    }

    if (SetupWindowData(_this, window, hwnd, GetParent(hwnd), SDL_FALSE) < 0) {
        return -1;
    }

#if SDL_VIDEO_OPENGL_WGL
    {
        const char *hint = SDL_GetHint(SDL_HINT_VIDEO_WINDOW_SHARE_PIXEL_FORMAT);
        if (hint) {
            /* This hint is a pointer (in string form) of the address of
               the window to share a pixel format with
            */
            SDL_Window *otherWindow = NULL;
            SDL_sscanf(hint, "%p", (void**)&otherWindow);

            /* Do some error checking on the pointer */
            if (otherWindow != NULL && otherWindow->magic == &_this->window_magic) {
                /* If the otherWindow has SDL_WINDOW_OPENGL set, set it for the new window as well */
                if (otherWindow->flags & SDL_WINDOW_OPENGL) {
                    window->flags |= SDL_WINDOW_OPENGL;
                    if (!WIN_GL_SetPixelFormatFrom(_this, otherWindow, window)) {
                        return -1;
                    }
                }
            }
        } else if (window->flags & SDL_WINDOW_OPENGL) {
            /* Try to set up the pixel format, if it hasn't been set by the application */
            WIN_GL_SetupWindow(_this, window);
        }
    }
#endif
    return 0;
}

void
WIN_SetWindowTitle(_THIS, SDL_Window * window)
{
    HWND hwnd = ((SDL_WindowData *) window->driverdata)->hwnd;
    LPTSTR title = WIN_UTF8ToString(window->title);
    SetWindowText(hwnd, title);
    SDL_free(title);
}

void
WIN_SetWindowIcon(_THIS, SDL_Window * window, SDL_Surface * icon)
{
    HWND hwnd = ((SDL_WindowData *) window->driverdata)->hwnd;
    HICON hicon = NULL;
    BYTE *icon_bmp;
    int icon_len, mask_len, row_len, y;
    BITMAPINFOHEADER *bmi;
    Uint8 *dst;
    SDL_bool isstack;

    /* Create temporary buffer for ICONIMAGE structure */
    SDL_COMPILE_TIME_ASSERT(WIN_SetWindowIcon_uses_BITMAPINFOHEADER_to_prepare_an_ICONIMAGE, sizeof(BITMAPINFOHEADER) == 40);
    mask_len = (icon->h * (icon->w + 7)/8);
    icon_len = sizeof(BITMAPINFOHEADER) + icon->h * icon->w * sizeof(Uint32) + mask_len;
    icon_bmp = SDL_small_alloc(BYTE, icon_len, &isstack);

    /* Write the BITMAPINFO header */
    bmi = (BITMAPINFOHEADER *)icon_bmp;
    bmi->biSize = SDL_SwapLE32(sizeof(BITMAPINFOHEADER));
    bmi->biWidth = SDL_SwapLE32(icon->w);
    bmi->biHeight = SDL_SwapLE32(icon->h * 2);
    bmi->biPlanes = SDL_SwapLE16(1);
    bmi->biBitCount = SDL_SwapLE16(32);
    bmi->biCompression = SDL_SwapLE32(BI_RGB);
    bmi->biSizeImage = SDL_SwapLE32(icon->h * icon->w * sizeof(Uint32));
    bmi->biXPelsPerMeter = SDL_SwapLE32(0);
    bmi->biYPelsPerMeter = SDL_SwapLE32(0);
    bmi->biClrUsed = SDL_SwapLE32(0);
    bmi->biClrImportant = SDL_SwapLE32(0);

    /* Write the pixels upside down into the bitmap buffer */
    SDL_assert(icon->format->format == SDL_PIXELFORMAT_ARGB8888);
    dst = &icon_bmp[sizeof(BITMAPINFOHEADER)];
    row_len = icon->w * sizeof(Uint32);
    y = icon->h;
    while (y--) {
        Uint8 *src = (Uint8 *) icon->pixels + y * icon->pitch;
        SDL_memcpy(dst, src, row_len);
        dst += row_len;
    }

    /* Write the mask */
    SDL_memset(icon_bmp + icon_len - mask_len, 0xFF, mask_len);

    hicon = CreateIconFromResource(icon_bmp, icon_len, TRUE, 0x00030000);

    SDL_small_free(icon_bmp, isstack);

    /* Set the icon for the window */
    SendMessage(hwnd, WM_SETICON, ICON_SMALL, (LPARAM) hicon);

    /* Set the icon in the task manager (should we do this?) */
    SendMessage(hwnd, WM_SETICON, ICON_BIG, (LPARAM) hicon);
}

void
WIN_SetWindowPosition(_THIS, SDL_Window * window)
{
    WIN_SetWindowPositionInternal(_this, window, SWP_NOCOPYBITS | SWP_NOSIZE | SWP_NOACTIVATE);
}

void
WIN_SetWindowSize(_THIS, SDL_Window * window)
{
    WIN_SetWindowPositionInternal(_this, window, SWP_NOCOPYBITS | SWP_NOMOVE | SWP_NOACTIVATE);
}

int
WIN_GetWindowBordersSize(_THIS, SDL_Window * window, int *top, int *left, int *bottom, int *right)
{
    HWND hwnd = ((SDL_WindowData *) window->driverdata)->hwnd;
    RECT rcClient, rcWindow;
    POINT ptDiff;

    /* rcClient stores the size of the inner window, while rcWindow stores the outer size relative to the top-left
     * screen position; so the top/left values of rcClient are always {0,0} and bottom/right are {height,width} */
    GetClientRect(hwnd, &rcClient);
    GetWindowRect(hwnd, &rcWindow);

    /* convert the top/left values to make them relative to
     * the window; they will end up being slightly negative */
    ptDiff.y = rcWindow.top;
    ptDiff.x = rcWindow.left;

    ScreenToClient(hwnd, &ptDiff);

    rcWindow.top  = ptDiff.y;
    rcWindow.left = ptDiff.x;

    /* convert the bottom/right values to make them relative to the window,
     * these will be slightly bigger than the inner width/height */
    ptDiff.y = rcWindow.bottom;
    ptDiff.x = rcWindow.right;

    ScreenToClient(hwnd, &ptDiff);

    rcWindow.bottom = ptDiff.y;
    rcWindow.right  = ptDiff.x;

    /* Now that both the inner and outer rects use the same coordinate system we can substract them to get the border size.
     * Keep in mind that the top/left coordinates of rcWindow are negative because the border lies slightly before {0,0},
     * so switch them around because SDL2 wants them in positive. */
    *top    = rcClient.top    - rcWindow.top;
    *left   = rcClient.left   - rcWindow.left;
    *bottom = rcWindow.bottom - rcClient.bottom;
    *right  = rcWindow.right  - rcClient.right;

    return 0;
}

void
WIN_ShowWindow(_THIS, SDL_Window * window)
{
    DWORD style;
    HWND hwnd;
    int nCmdShow;

    hwnd = ((SDL_WindowData *)window->driverdata)->hwnd;
    nCmdShow = SDL_GetHintBoolean(SDL_HINT_WINDOW_NO_ACTIVATION_WHEN_SHOWN, SDL_FALSE) ? SW_SHOWNA : SW_SHOW;
    style = GetWindowLong(hwnd, GWL_EXSTYLE);
    if (style & WS_EX_NOACTIVATE) {
        nCmdShow = SW_SHOWNOACTIVATE;
    }
    ShowWindow(hwnd, nCmdShow);
}

void
WIN_HideWindow(_THIS, SDL_Window * window)
{
    HWND hwnd = ((SDL_WindowData *) window->driverdata)->hwnd;
    ShowWindow(hwnd, SW_HIDE);
}

void
WIN_RaiseWindow(_THIS, SDL_Window * window)
{
    /* If desired, raise the window more forcefully.
     * Technique taken from http://stackoverflow.com/questions/916259/ .
     * Specifically, http://stackoverflow.com/a/34414846 .
     *
     * The issue is that Microsoft has gone through a lot of trouble to make it
     * nearly impossible to programmatically move a window to the foreground,
     * for "security" reasons. Apparently, the following song-and-dance gets
     * around their objections. */
    SDL_bool bForce = SDL_GetHintBoolean(SDL_HINT_FORCE_RAISEWINDOW, SDL_FALSE);

    HWND hCurWnd = NULL;
    DWORD dwMyID = 0u;
    DWORD dwCurID = 0u;

    HWND hwnd = ((SDL_WindowData *) window->driverdata)->hwnd;
    if(bForce)
    {
        hCurWnd = GetForegroundWindow();
        dwMyID = GetCurrentThreadId();
        dwCurID = GetWindowThreadProcessId(hCurWnd, NULL);
        ShowWindow(hwnd, SW_RESTORE);
        AttachThreadInput(dwCurID, dwMyID, TRUE);
        SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOSIZE | SWP_NOMOVE);
        SetWindowPos(hwnd, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOSIZE | SWP_NOMOVE);
    }
    SetForegroundWindow(hwnd);
    if (bForce)
    {
        AttachThreadInput(dwCurID, dwMyID, FALSE);
        SetFocus(hwnd);
        SetActiveWindow(hwnd);
    }
}

void
WIN_MaximizeWindow(_THIS, SDL_Window * window)
{
    SDL_WindowData *data = (SDL_WindowData *)window->driverdata;
    HWND hwnd = data->hwnd;
    data->expected_resize = SDL_TRUE;
    ShowWindow(hwnd, SW_MAXIMIZE);
    data->expected_resize = SDL_FALSE;
}

void
WIN_MinimizeWindow(_THIS, SDL_Window * window)
{
    HWND hwnd = ((SDL_WindowData *) window->driverdata)->hwnd;
    ShowWindow(hwnd, SW_MINIMIZE);
}

void
WIN_SetWindowBordered(_THIS, SDL_Window * window, SDL_bool bordered)
{
    SDL_WindowData *data = (SDL_WindowData *)window->driverdata;
    HWND hwnd = data->hwnd;
    DWORD style;

    style = GetWindowLong(hwnd, GWL_STYLE);
    style &= ~STYLE_MASK;
    style |= GetWindowStyle(window);

    data->in_border_change = SDL_TRUE;
    SetWindowLong(hwnd, GWL_STYLE, style);
    WIN_SetWindowPositionInternal(_this, window, SWP_NOCOPYBITS | SWP_FRAMECHANGED | SWP_NOZORDER | SWP_NOACTIVATE);
    data->in_border_change = SDL_FALSE;
}

void
WIN_SetWindowResizable(_THIS, SDL_Window * window, SDL_bool resizable)
{
    SDL_WindowData *data = (SDL_WindowData *)window->driverdata;
    HWND hwnd = data->hwnd;
    DWORD style;

    style = GetWindowLong(hwnd, GWL_STYLE);
    style &= ~STYLE_MASK;
    style |= GetWindowStyle(window);

    SetWindowLong(hwnd, GWL_STYLE, style);
}

void
WIN_SetWindowAlwaysOnTop(_THIS, SDL_Window * window, SDL_bool on_top)
{
    SDL_WindowData *data = (SDL_WindowData *)window->driverdata;
    HWND hwnd = data->hwnd;
    if (on_top) {
        SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
    } else {
        SetWindowPos(hwnd, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
    }
}

void
WIN_RestoreWindow(_THIS, SDL_Window * window)
{
    SDL_WindowData *data = (SDL_WindowData *)window->driverdata;
    HWND hwnd = data->hwnd;
    data->expected_resize = SDL_TRUE;
    ShowWindow(hwnd, SW_RESTORE);
    data->expected_resize = SDL_FALSE;
}

void
WIN_SetWindowFullscreen(_THIS, SDL_Window * window, SDL_VideoDisplay * display, SDL_bool fullscreen)
{
    SDL_WindowData *data = (SDL_WindowData *) window->driverdata;
    HWND hwnd = data->hwnd;
    SDL_Rect bounds;
    DWORD style;
    HWND top;
    int x, y;
    int w, h;

    if (SDL_ShouldAllowTopmost() && ((window->flags & (SDL_WINDOW_FULLSCREEN|SDL_WINDOW_INPUT_FOCUS)) == (SDL_WINDOW_FULLSCREEN|SDL_WINDOW_INPUT_FOCUS) || window->flags & SDL_WINDOW_ALWAYS_ON_TOP)) {
        top = HWND_TOPMOST;
    } else {
        top = HWND_NOTOPMOST;
    }

    style = GetWindowLong(hwnd, GWL_STYLE);
    style &= ~STYLE_MASK;
    style |= GetWindowStyle(window);

    WIN_GetDisplayBounds(_this, display, &bounds);

    if (fullscreen) {
        x = bounds.x;
        y = bounds.y;
        w = bounds.w;
        h = bounds.h;

        /* Unset the maximized flag.  This fixes
           https://bugzilla.libsdl.org/show_bug.cgi?id=3215
        */
        if (style & WS_MAXIMIZE) {
            data->windowed_mode_was_maximized = SDL_TRUE;
            style &= ~WS_MAXIMIZE;
        }
    } else {
        BOOL menu;

        /* Restore window-maximization state, as applicable.
           Special care is taken to *not* do this if and when we're
           alt-tab'ing away (to some other window; as indicated by
           in_window_deactivation), otherwise
           https://bugzilla.libsdl.org/show_bug.cgi?id=3215 can reproduce!
        */
        if (data->windowed_mode_was_maximized && !data->in_window_deactivation) {
            style |= WS_MAXIMIZE;
            data->windowed_mode_was_maximized = SDL_FALSE;
        }

        menu = (style & WS_CHILDWINDOW) ? FALSE : (GetMenu(hwnd) != NULL);
        WIN_AdjustWindowRectWithStyle(window, style, menu, &x, &y, &w, &h, SDL_FALSE);
    }
    SetWindowLong(hwnd, GWL_STYLE, style);
    data->expected_resize = SDL_TRUE;
    SetWindowPos(hwnd, top, x, y, w, h, SWP_NOCOPYBITS | SWP_NOACTIVATE);
    data->expected_resize = SDL_FALSE;
}

int
WIN_SetWindowGammaRamp(_THIS, SDL_Window * window, const Uint16 * ramp)
{
    SDL_VideoDisplay *display = SDL_GetDisplayForWindow(window);
    SDL_DisplayData *data = (SDL_DisplayData *) display->driverdata;
    HDC hdc;
    BOOL succeeded = FALSE;

    hdc = CreateDCW(data->DeviceName, NULL, NULL, NULL);
    if (hdc) {
        succeeded = SetDeviceGammaRamp(hdc, (LPVOID)ramp);
        if (!succeeded) {
            WIN_SetError("SetDeviceGammaRamp()");
        }
        DeleteDC(hdc);
    }
    return succeeded ? 0 : -1;
}

void*
WIN_GetWindowICCProfile(_THIS, SDL_Window * window, size_t * size)
{
    SDL_VideoDisplay* display = SDL_GetDisplayForWindow(window);
    SDL_DisplayData* data = (SDL_DisplayData*)display->driverdata;
    HDC hdc;
    BOOL succeeded = FALSE;
    WCHAR filename[MAX_PATH];
    DWORD fileNameSize = MAX_PATH;
    void* iccProfileData = NULL;

    hdc = CreateDCW(data->DeviceName, NULL, NULL, NULL);
    if (hdc) {
        succeeded = GetICMProfileW(hdc, &fileNameSize, filename);
        DeleteDC(hdc);
    }

    if (succeeded) {
        iccProfileData = SDL_LoadFile(WIN_StringToUTF8(filename), size);
        if (!iccProfileData)
            SDL_SetError("Could not open ICC profile");
    }

    return iccProfileData;
}

int
WIN_GetWindowGammaRamp(_THIS, SDL_Window * window, Uint16 * ramp)
{
    SDL_VideoDisplay *display = SDL_GetDisplayForWindow(window);
    SDL_DisplayData *data = (SDL_DisplayData *) display->driverdata;
    HDC hdc;
    BOOL succeeded = FALSE;

    hdc = CreateDCW(data->DeviceName, NULL, NULL, NULL);
    if (hdc) {
        succeeded = GetDeviceGammaRamp(hdc, (LPVOID)ramp);
        if (!succeeded) {
            WIN_SetError("GetDeviceGammaRamp()");
        }
        DeleteDC(hdc);
    }
    return succeeded ? 0 : -1;
}

static void WIN_GrabKeyboard(SDL_Window *window)
{
    SDL_WindowData *data = (SDL_WindowData *)window->driverdata;
    HMODULE module;

    if (data->keyboard_hook) {
        return;
    }

    /* SetWindowsHookEx() needs to know which module contains the hook we
       want to install. This is complicated by the fact that SDL can be
       linked statically or dynamically. Fortunately XP and later provide
       this nice API that will go through the loaded modules and find the
       one containing our code.
    */
    if (!GetModuleHandleEx(GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT | GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
                           (LPTSTR)WIN_KeyboardHookProc,
                           &module)) {
        return;
    }

    /* Capture a snapshot of the current keyboard state before the hook */
    if (!GetKeyboardState(data->videodata->pre_hook_key_state)) {
        return;
    }

    /* To grab the keyboard, we have to install a low-level keyboard hook to
       intercept keys that would normally be captured by the OS. Intercepting
       all key events on the system is rather invasive, but it's what Microsoft
       actually documents that you do to capture these.
    */
    data->keyboard_hook = SetWindowsHookEx(WH_KEYBOARD_LL, WIN_KeyboardHookProc, module, 0);
}

void WIN_UngrabKeyboard(SDL_Window *window)
{
    SDL_WindowData *data = (SDL_WindowData *)window->driverdata;

    if (data->keyboard_hook) {
        UnhookWindowsHookEx(data->keyboard_hook);
        data->keyboard_hook = NULL;
    }
}

void
WIN_SetWindowMouseRect(_THIS, SDL_Window * window)
{
    WIN_UpdateClipCursor(window);
}

void
WIN_SetWindowMouseGrab(_THIS, SDL_Window * window, SDL_bool grabbed)
{
    WIN_UpdateClipCursor(window);

    if (grabbed &&
        (window->flags & SDL_WINDOW_FULLSCREEN) &&
        (window->flags & SDL_WINDOW_SHOWN)) {
        WIN_SetWindowPositionInternal(_this, window, SWP_NOCOPYBITS | SWP_NOMOVE | SWP_NOSIZE);
    }
}

void
WIN_SetWindowKeyboardGrab(_THIS, SDL_Window * window, SDL_bool grabbed)
{
    if (grabbed) {
        WIN_GrabKeyboard(window);
    } else {
        WIN_UngrabKeyboard(window);
    }
}

void
WIN_DestroyWindow(_THIS, SDL_Window * window)
{
    SDL_WindowData *data = (SDL_WindowData *) window->driverdata;

    if (data) {
        if (data->keyboard_hook) {
            UnhookWindowsHookEx(data->keyboard_hook);
        }
        ReleaseDC(data->hwnd, data->hdc);
        RemoveProp(data->hwnd, TEXT("SDL_WindowData"));
        if (data->created) {
            DestroyWindow(data->hwnd);
            if (data->parent) {
                DestroyWindow(data->parent);
            }
        } else {
            /* Restore any original event handler... */
            if (data->wndproc != NULL) {
#ifdef GWLP_WNDPROC
                SetWindowLongPtr(data->hwnd, GWLP_WNDPROC,
                                 (LONG_PTR) data->wndproc);
#else
                SetWindowLong(data->hwnd, GWL_WNDPROC,
                              (LONG_PTR) data->wndproc);
#endif
            }
        }
        SDL_free(data);
    }
    window->driverdata = NULL;
}

SDL_bool
WIN_GetWindowWMInfo(_THIS, SDL_Window * window, SDL_SysWMinfo * info)
{
    const SDL_WindowData *data = (const SDL_WindowData *) window->driverdata;
    if (info->version.major <= SDL_MAJOR_VERSION) {
        int versionnum = SDL_VERSIONNUM(info->version.major, info->version.minor, info->version.patch);

        info->subsystem = SDL_SYSWM_WINDOWS;
        info->info.win.window = data->hwnd;

        if (versionnum >= SDL_VERSIONNUM(2, 0, 4)) {
            info->info.win.hdc = data->hdc;
        }

        if (versionnum >= SDL_VERSIONNUM(2, 0, 5)) {
            info->info.win.hinstance = data->hinstance;
        }

        return SDL_TRUE;
    } else {
        SDL_SetError("Application not compiled with SDL %d.%d",
                     SDL_MAJOR_VERSION, SDL_MINOR_VERSION);
        return SDL_FALSE;
    }
}

/*
 * Creates a HelperWindow used for DirectInput.
 */
int
SDL_HelperWindowCreate(void)
{
    HINSTANCE hInstance = GetModuleHandle(NULL);
    WNDCLASS wce;

    /* Make sure window isn't created twice. */
    if (SDL_HelperWindow != NULL) {
        return 0;
    }

    /* Create the class. */
    SDL_zero(wce);
    wce.lpfnWndProc = DefWindowProc;
    wce.lpszClassName = SDL_HelperWindowClassName;
    wce.hInstance = hInstance;

    /* Register the class. */
    SDL_HelperWindowClass = RegisterClass(&wce);
    if (SDL_HelperWindowClass == 0 && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        return WIN_SetError("Unable to create Helper Window Class");
    }

    /* Create the window. */
    SDL_HelperWindow = CreateWindowEx(0, SDL_HelperWindowClassName,
                                      SDL_HelperWindowName,
                                      WS_OVERLAPPED, CW_USEDEFAULT,
                                      CW_USEDEFAULT, CW_USEDEFAULT,
                                      CW_USEDEFAULT, HWND_MESSAGE, NULL,
                                      hInstance, NULL);
    if (SDL_HelperWindow == NULL) {
        UnregisterClass(SDL_HelperWindowClassName, hInstance);
        return WIN_SetError("Unable to create Helper Window");
    }

    return 0;
}


/*
 * Destroys the HelperWindow previously created with SDL_HelperWindowCreate.
 */
void
SDL_HelperWindowDestroy(void)
{
    HINSTANCE hInstance = GetModuleHandle(NULL);

    /* Destroy the window. */
    if (SDL_HelperWindow != NULL) {
        if (DestroyWindow(SDL_HelperWindow) == 0) {
            WIN_SetError("Unable to destroy Helper Window");
            return;
        }
        SDL_HelperWindow = NULL;
    }

    /* Unregister the class. */
    if (SDL_HelperWindowClass != 0) {
        if ((UnregisterClass(SDL_HelperWindowClassName, hInstance)) == 0) {
            WIN_SetError("Unable to destroy Helper Window Class");
            return;
        }
        SDL_HelperWindowClass = 0;
    }
}

void WIN_OnWindowEnter(_THIS, SDL_Window * window)
{
    SDL_WindowData *data = (SDL_WindowData *) window->driverdata;

    if (!data || !data->hwnd) {
        /* The window wasn't fully initialized */
        return;
    }

    if (window->flags & SDL_WINDOW_ALWAYS_ON_TOP) {
        WIN_SetWindowPositionInternal(_this, window, SWP_NOCOPYBITS | SWP_NOSIZE | SWP_NOACTIVATE);
    }
}

void
WIN_UpdateClipCursor(SDL_Window *window)
{
    SDL_WindowData *data = (SDL_WindowData *) window->driverdata;
    SDL_Mouse *mouse = SDL_GetMouse();
    RECT rect, clipped_rect;

    if (data->in_title_click || data->focus_click_pending) {
        return;
    }
    if (data->skip_update_clipcursor) {
        return;
    }
    if (!GetClipCursor(&clipped_rect)) {
        return;
    }

    if ((mouse->relative_mode || (window->flags & SDL_WINDOW_MOUSE_GRABBED) ||
         (window->mouse_rect.w > 0 && window->mouse_rect.h > 0)) &&
        (window->flags & SDL_WINDOW_INPUT_FOCUS)) {
        if (mouse->relative_mode && !mouse->relative_mode_warp) {
            if (GetWindowRect(data->hwnd, &rect)) {
                LONG cx, cy;

                cx = (rect.left + rect.right) / 2;
                cy = (rect.top + rect.bottom) / 2;

                /* Make an absurdly small clip rect */
                rect.left = cx - 1;
                rect.right = cx + 1;
                rect.top = cy - 1;
                rect.bottom = cy + 1;

                if (SDL_memcmp(&rect, &clipped_rect, sizeof(rect)) != 0) {
                    if (ClipCursor(&rect)) {
                        data->cursor_clipped_rect = rect;
                    }
                }
            }
        } else {
            if (GetClientRect(data->hwnd, &rect)) {
                ClientToScreen(data->hwnd, (LPPOINT) & rect);
                ClientToScreen(data->hwnd, (LPPOINT) & rect + 1);
                if (window->mouse_rect.w > 0 && window->mouse_rect.h > 0) {
                    RECT mouse_rect, intersection;

                    mouse_rect.left = rect.left + window->mouse_rect.x;
                    mouse_rect.top = rect.top + window->mouse_rect.y;
                    mouse_rect.right = mouse_rect.left + window->mouse_rect.w - 1;
                    mouse_rect.bottom = mouse_rect.top + window->mouse_rect.h - 1;
                    if (IntersectRect(&intersection, &rect, &mouse_rect)) {
                        SDL_memcpy(&rect, &intersection, sizeof(rect));
                    } else if ((window->flags & SDL_WINDOW_MOUSE_GRABBED) != 0) {
                        /* Mouse rect was invalid, just do the normal grab */
                    } else {
                        SDL_zero(rect);
                    }
                }
                if (SDL_memcmp(&rect, &clipped_rect, sizeof(rect)) != 0) {
                    if (!IsRectEmpty(&rect)) {
                        if (ClipCursor(&rect)) {
                            data->cursor_clipped_rect = rect;
                        }
                    } else {
                        ClipCursor(NULL);
                        SDL_zero(data->cursor_clipped_rect);
                    }
                }
            }
        }
    } else {
        POINT first, second;

        first.x = clipped_rect.left;
        first.y = clipped_rect.top;
        second.x = clipped_rect.right - 1;
        second.y = clipped_rect.bottom - 1;
        if (PtInRect(&data->cursor_clipped_rect, first) &&
            PtInRect(&data->cursor_clipped_rect, second)) {
            ClipCursor(NULL);
            SDL_zero(data->cursor_clipped_rect);
        }
    }
    data->last_updated_clipcursor = SDL_GetTicks();
}

int
WIN_SetWindowHitTest(SDL_Window *window, SDL_bool enabled)
{
    return 0;  /* just succeed, the real work is done elsewhere. */
}

int
WIN_SetWindowOpacity(_THIS, SDL_Window * window, float opacity)
{
    const SDL_WindowData *data = (SDL_WindowData *) window->driverdata;
    const HWND hwnd = data->hwnd;
    const LONG style = GetWindowLong(hwnd, GWL_EXSTYLE);

    SDL_assert(style != 0);

    if (opacity == 1.0f) {
        /* want it fully opaque, just mark it unlayered if necessary. */
        if (style & WS_EX_LAYERED) {
            if (SetWindowLong(hwnd, GWL_EXSTYLE, style & ~WS_EX_LAYERED) == 0) {
                return WIN_SetError("SetWindowLong()");
            }
        }
    } else {
        const BYTE alpha = (BYTE) ((int) (opacity * 255.0f));
        /* want it transparent, mark it layered if necessary. */
        if ((style & WS_EX_LAYERED) == 0) {
            if (SetWindowLong(hwnd, GWL_EXSTYLE, style | WS_EX_LAYERED) == 0) {
                return WIN_SetError("SetWindowLong()");
            }
        }

        if (SetLayeredWindowAttributes(hwnd, 0, alpha, LWA_ALPHA) == 0) {
            return WIN_SetError("SetLayeredWindowAttributes()");
        }
    }

    return 0;
}

void
WIN_AcceptDragAndDrop(SDL_Window * window, SDL_bool accept)
{
    const SDL_WindowData *data = (SDL_WindowData *) window->driverdata;
    DragAcceptFiles(data->hwnd, accept ? TRUE : FALSE);
}

int
WIN_FlashWindow(_THIS, SDL_Window * window, SDL_FlashOperation operation)
{
    FLASHWINFO desc;

    SDL_zero(desc);
    desc.cbSize = sizeof(desc);
    desc.hwnd = ((SDL_WindowData *) window->driverdata)->hwnd;
    switch (operation) {
    case SDL_FLASH_CANCEL:
        desc.dwFlags = FLASHW_STOP;
        break;
    case SDL_FLASH_BRIEFLY:
        desc.dwFlags = FLASHW_TRAY;
        desc.uCount = 1;
        break;
    case SDL_FLASH_UNTIL_FOCUSED:
        desc.dwFlags = (FLASHW_TRAY | FLASHW_TIMERNOFG);
        break;
    default:
        return SDL_Unsupported();
    }

    FlashWindowEx(&desc);

    return 0;
}

#endif /* SDL_VIDEO_DRIVER_WINDOWS */

/* vi: set ts=4 sw=4 expandtab: */
