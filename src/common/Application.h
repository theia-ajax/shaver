#pragma once

#include "Log.h"
#include "Types.h"

typedef struct ApplicationConfig {

} ApplicationConfig;

typedef struct Application Application;
typedef struct SDL_Window SDL_Window;

Application* ApplicationInitialize(const ApplicationConfig* Config);
void ApplicationShutdown(Application *App);
void ApplicationRun(Application *App);

SDL_Window *GetApplicationWindow(Application *App);

typedef struct SDL_Surface SDL_Surface;
bool ApplicationTakeDesktopScreenshot(SDL_Surface **OutSurface);