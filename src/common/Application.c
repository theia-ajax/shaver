#include "Application.h"

#include <SDL3/SDL.h>
#include <sokol_time.h>
#include <stb_ds.h>

#include "Log.h"

typedef struct ShaverDisplay {
	SDL_DisplayID DisplayID;
	const SDL_DisplayMode* DisplayMode;
	SDL_Window* Window;
	SDL_Renderer* Renderer;
	SDL_Rect Bounds;
	SDL_Texture* ScreenshotTexture;
} ShaverDisplay;

typedef struct ShaverApplication {
	ShaverDisplay* Displays;
	SDL_Surface* Screenshot;
	bool EnableConfusionPrevention; // When true make it obvious that the screen saver is running so I don't get
									// confused while deving
} ShaverApplication;

typedef struct GameTime {
	float64 ElapsedSeconds;
	float64 DeltaTime;
	float64 SimTimeMS;
	float64 RenderTimeMS;
	float32 DeltaTimeF;
} GameTime;

static const ApplicationConfig DefaultApplicationConfig = {};

ShaverApplication* ApplicationCreate();
void ApplicationDestroy(ShaverApplication* App);
void ApplicationCreateDisplays(ShaverApplication* App);
void ApplicationUpdate(ShaverApplication* App, const GameTime* Time);
void ApplicationRender(ShaverApplication* App);
bool ApplicationEventShouldExit(ShaverApplication* App, const SDL_Event* Event);

Application* ApplicationInitialize(const ApplicationConfig* Config)
{
	Config = (Config != NULL) ? Config : &DefaultApplicationConfig;

	if (!SDL_Init(SDL_INIT_VIDEO)) {
		PanicAndAbort("SDL Error", SDL_GetError());
	}
	SDL_SetHint(SDL_HINT_RENDER_VSYNC, "0");

	LoggingInitialize(LogLevel_Info);
	LogInfo(
		"System: Initialized SDL runtime v%d.%d.%d, compiled with v%d.%d.%d",
		SDL_VERSIONNUM_MAJOR(SDL_GetVersion()),
		SDL_VERSIONNUM_MINOR(SDL_GetVersion()),
		SDL_VERSIONNUM_MICRO(SDL_GetVersion()),
		SDL_VERSIONNUM_MAJOR(SDL_VERSION),
		SDL_VERSIONNUM_MINOR(SDL_VERSION),
		SDL_VERSIONNUM_MICRO(SDL_VERSION));

	stm_setup();

	ShaverApplication* App = ApplicationCreate();
	ApplicationTakeDesktopScreenshot(&App->Screenshot);
	ApplicationCreateDisplays(App);

#ifdef _DEBUG
	App->EnableConfusionPrevention = true;
#endif

	return (Application*)App;
}

void ApplicationShutdown(Application* App)
{
	ApplicationDestroy((ShaverApplication*)App);
	LoggingShutdown();
	SDL_Quit();
}

void ApplicationRun(Application* App)
{
	ShaverApplication* _App = (ShaverApplication*)App;

	const int KTargetFramesPerSecond = 60;
	double KTargetFrameRateSeconds = (KTargetFramesPerSecond != 0) ? (1.0 / KTargetFramesPerSecond) : 0.0;
	uint64 NowTicks = 0;
	uint64 DeltaTicks = 0;
	uint64 SimTimeTicks = 0;
	uint64 RenderTimeTicks = 0;
	double ElapsedSeconds = 0.0;

	bool IsRunning = true;

	while (IsRunning) {
		uint64 FrameStartTicks = stm_now();

		SDL_Event Event;
		while (SDL_PollEvent(&Event)) {
			if (ApplicationEventShouldExit(_App, &Event)) {
				IsRunning = false;
			}
		}

		DeltaTicks = stm_laptime(&NowTicks);
		double DeltaTimeSeconds = stm_sec(DeltaTicks);
		ElapsedSeconds += DeltaTimeSeconds;

		GameTime Time = {
			.DeltaTime = DeltaTimeSeconds,
			.DeltaTimeF = (float)DeltaTimeSeconds,
			.ElapsedSeconds = ElapsedSeconds,
			.SimTimeMS = stm_ms(SimTimeTicks),
			.RenderTimeMS = stm_ms(RenderTimeTicks),
		};

		uint64 SimStartTicks = stm_now();
		ApplicationUpdate(_App, &Time);
		SimTimeTicks = stm_since(SimStartTicks);

		uint64 RenderStartTicks = stm_now();
		ApplicationRender(_App);
		RenderTimeTicks = stm_since(RenderStartTicks);

		while (KTargetFramesPerSecond != 0 && stm_sec(stm_since(FrameStartTicks)) < KTargetFrameRateSeconds) {
			// Do nothing...
		};
	}
}

SDL_Window* GetApplicationWindow(Application* App)
{
	const ShaverDisplay* Displays = ((ShaverApplication*)App)->Displays;
	if (arrlen(Displays) > 0) {
		return Displays[0].Window;
	} else {
		return NULL;
	}
}

ShaverApplication* ApplicationCreate()
{
	ShaverApplication* App = SDL_malloc(sizeof(ShaverApplication));
	SDL_zerop(App);
	arrsetcap(App->Displays, 4);
	return App;
}

void ApplicationDestroy(ShaverApplication* App)
{
	arrfree(App->Displays);
	SDL_free(App);
}

void ApplicationCreateDisplays(ShaverApplication* App)
{
	int DisplayCount = 0;
	SDL_DisplayID* Displays = SDL_GetDisplays(&DisplayCount);

	for (int DisplayIndex = 0; DisplayIndex < DisplayCount; DisplayIndex++) {
		SDL_DisplayID DisplayID = Displays[DisplayIndex];
		const SDL_DisplayMode* DisplayMode = SDL_GetDesktopDisplayMode(DisplayID);

		SDL_Window* Window;
		SDL_Renderer* Renderer;

		char WindowName[64];
		SDL_snprintf(WindowName, SDL_arraysize(WindowName), "ScreenShaver_%02d", DisplayIndex);

		LogInfo("Created display %d", DisplayIndex);

		int Width = DisplayMode->w;
		int Height = DisplayMode->h;
		SDL_WindowFlags Flags = SDL_WINDOW_TRANSPARENT;

		SDL_CreateWindowAndRenderer(WindowName, Width, Height, Flags, &Window, &Renderer);

		SDL_Rect Bounds;
		SDL_GetDisplayBounds(DisplayID, &Bounds);
		// SDL_SetWindowFullscreen(Window, true);
		// SDL_SetWindowFullscreenMode(Window, DisplayMode);
		SDL_SetWindowPosition(Window, Bounds.x, Bounds.y);
		SDL_SetWindowFullscreen(Window, true);

		arrput(
			App->Displays,
			((ShaverDisplay){
				.DisplayID = DisplayID,
				.DisplayMode = DisplayMode,
				.Window = Window,
				.Renderer = Renderer,
				.Bounds = Bounds,
				.ScreenshotTexture = SDL_CreateTextureFromSurface(Renderer, App->Screenshot),
			}));
	}
}

void ApplicationUpdate(ShaverApplication* App, const GameTime* Time)
{
}

void ApplicationRender(ShaverApplication* App)
{
	for (int ShaverIndex = 0; ShaverIndex < arrlen(App->Displays); ShaverIndex++) {
		ShaverDisplay* Display = &App->Displays[ShaverIndex];

		SDL_SetRenderDrawColor(Display->Renderer, 0, 0, 0, 255);
		SDL_RenderClear(Display->Renderer);

		if (App->EnableConfusionPrevention) {
			SDL_SetTextureColorModFloat(Display->ScreenshotTexture, 0.5f, 1.0f, 1.0f);
		} else {
			SDL_SetTextureColorModFloat(Display->ScreenshotTexture, 1.0f, 1.0f, 1.0f);
		}

		SDL_FRect DisplayBounds;
		SDL_RectToFRect(&Display->Bounds, &DisplayBounds);
		SDL_RenderTexture(Display->Renderer, Display->ScreenshotTexture, &DisplayBounds, NULL);

		SDL_RenderPresent(Display->Renderer);
	}
}

bool ApplicationEventShouldExit(ShaverApplication* App, const SDL_Event* Event)
{
	bool Result = false;
	switch (Event->type) {
		case SDL_EVENT_QUIT:
		// case SDL_EVENT_MOUSE_MOTION:
		case SDL_EVENT_KEY_DOWN: Result = true; break;
	}
	return Result;
}