/* Include the SDL main definition header */
#include "SDL_main.h"
#undef main

/* Standard includes */
#include <stdio.h>

/* SDL includes */
#include "../../video/ogc/SDL_ogc_video.h"

/* OGC includes */
#include <fat.h>
#include <ogcsys.h>
#include <wiiuse/wpad.h>
#include <ogc/usbmouse.h>
#include <wiikeyboard/keyboard.h>

bool TerminateRequested=false, ShutdownRequested=false, ResetRequested=false;

void SDL_Quit();
static void ShutdownCB()
{
	TerminateRequested = 1;
	ShutdownRequested = 1;
}
static void ResetCB()
{
	TerminateRequested = 1;
	ResetRequested = 1;
}
void ShutdownWii()
{
	TerminateRequested = 0;
	SDL_Quit();
	SYS_ResetSystem(SYS_POWEROFF, 0, 0);
}
void RestartHomebrewChannel()
{
	TerminateRequested = 0;
	SDL_Quit();
	exit(1);
}
void Terminate()
{
	if (ShutdownRequested) ShutdownWii();
	else if (ResetRequested) RestartHomebrewChannel();
}

/* Do initialisation which has to be done first for the console to work */
/* Entry point */
int main(int argc, char *argv[])
{
	L2Enhance();
	u32 version = IOS_GetVersion();
	s32 preferred = IOS_GetPreferredVersion();

	if(preferred > 0 && version != (u32)preferred)
		IOS_ReloadIOS(preferred);

	// Wii Power/Reset buttons
	WPAD_Init();
	WPAD_SetPowerButtonCallback((WPADShutdownCallback)ShutdownCB);
	SYS_SetPowerCallback(ShutdownCB);
	SYS_SetResetCallback(ResetCB);
	PAD_Init();
	OGC_InitVideoSystem();
	WPAD_SetDataFormat(WPAD_CHAN_ALL,WPAD_FMT_BTNS_ACC_IR);
	WPAD_SetVRes(WPAD_CHAN_ALL, 640, 480);

	MOUSE_Init();
	KEYBOARD_Init(NULL);
	fatInitDefault();
	/* Call the user's main function */
	return(SDL_main(argc, argv));
}

