#pragma once

#include <SDL3/SDL.h>

#include "Types.h"

typedef struct RazorState RazorState;
typedef struct Application Application;

typedef struct Display {
	int32 Width;
	int32 Height;
} Display;

Display *GetDisplayForRazor(Application *App, RazorState *Razor);