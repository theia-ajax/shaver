#include "Application.h"

#include <SDL3/SDL.h>
#include <sokol_time.h>
#include <stb_ds.h>
#include <stb_image.h>

#include "Debug.h"
#include "Display.h"
#include "Log.h"
#include "Math2D.h"
#include "Razor.h"

typedef struct ShaverDisplay {
	Display Display;
	SDL_DisplayID DisplayID;
	const SDL_DisplayMode* DisplayMode;
	SDL_Window* Window;
	SDL_Renderer* Renderer;
	SDL_Rect Bounds;
	SDL_Texture* ScreenshotTexture;
	SDL_Texture* RazorTexture;
	SDL_Texture* ShavedTexture;
	SDL_Surface* ShavedSurface;
	RazorState Razor;
	int ActivePattern;
} ShaverDisplay;

typedef struct ShaverApplication {
	ShaverDisplay* Displays;
	SDL_Surface* Screenshot;
	InterpolatorContext* InterpolatorContext;
	SDL_Surface** Patterns;
	int64 NextInterpolatorId;
	RazorConfig RazorConfig;
	bool RequestShutdown;
	bool EnableDebugDraw;
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
bool ApplicationIsRunning(ShaverApplication* App);
void ApplicationStopRunning(ShaverApplication* App);
bool ApplicationEventShouldExit(ShaverApplication* App, const SDL_Event* Event);
void ApplicationDebugKeyDown(ShaverApplication* App, SDL_Scancode scancode);

bool LoadImage(const char* FileName, SDL_Surface** OutSurface);
bool LoadImageFromMemory(const void* Data, size_t Bytes, SDL_Surface** OutSurface);

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
	App->InterpolatorContext = CreateInterpolatorContext();
	ApplicationTakeDesktopScreenshot(&App->Screenshot);

	App->RazorConfig.InterpolatorContext = App->InterpolatorContext;
	InitializeRazors((Application*)App, &App->RazorConfig);

	{
		// clang-format off
		static const char RazorImageData[] = {
			#embed "razor.png"
		};
		// clang-format on
		LoadImageFromMemory(RazorImageData, sizeof(RazorImageData), &App->RazorConfig.Image);
		App->RazorConfig.BladeBounds = (SDL_Rect){0, 0, 128, 32};
	}

	{
		// clang-format off
		static const char PatternData00[] = {
			#embed "pattern_00.png"
		};
		static const char PatternData01[] = {
			#embed "pattern_01.png"
		};
		static const char PatternData02[] = {
			#embed "pattern_02.png"
		};
		static const char PatternData03[] = {
			#embed "pattern_03.png"
		};
		static const char PatternData04[] = {
			#embed "pattern_04.png"
		};
		static const char PatternData05[] = {
			#embed "pattern_05.png"
		};
		static const char PatternData06[] = {
			#embed "pattern_06.png"
		};
		// clang-format on

		typedef struct PatternData {
			const char* Data;
			size_t Size;
		} PatternData;

		// clang-format off
#define PATTERN_DATA_ENTRY(Data) (PatternData) { Data, sizeof(Data) }
		// clang-format on

		const PatternData PatternImagesData[] = {
			PATTERN_DATA_ENTRY(PatternData04),
			PATTERN_DATA_ENTRY(PatternData00),
			PATTERN_DATA_ENTRY(PatternData02),
			PATTERN_DATA_ENTRY(PatternData05),
			PATTERN_DATA_ENTRY(PatternData01),
			PATTERN_DATA_ENTRY(PatternData03),
			PATTERN_DATA_ENTRY(PatternData06),
		};

#undef PATTERN_DATA_ENTRY

		for (int PatternIndex = 0; PatternIndex < SDL_arraysize(PatternImagesData); PatternIndex++) {
			SDL_Surface* PatternSurface;
			LoadImageFromMemory(
				PatternImagesData[PatternIndex].Data,
				PatternImagesData[PatternIndex].Size,
				&PatternSurface);
			arrput(App->Patterns, PatternSurface);
		}
	}

	ApplicationCreateDisplays(App);

#ifdef _DEBUG
	App->EnableConfusionPrevention = true;
	App->EnableDebugDraw = true;
#endif

	DebugInitialize(&(DebugConfig){
		.CanvasWidth = 800,
		.CanvasHeight = 600,
		.BackgroundColor = SDL_MapRGBA(SDL_GetPixelFormatDetails(SDL_PIXELFORMAT_RGBA32), NULL, 32, 32, 32, 196),
		.ForegroundColor = SDL_MapRGBA(SDL_GetPixelFormatDetails(SDL_PIXELFORMAT_RGBA32), NULL, 200, 255, 255, 255),
	});

	return (Application*)App;
}

void ApplicationShutdown(Application* App)
{
	DebugShutdown();
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

	while (ApplicationIsRunning(_App)) {
		uint64 FrameStartTicks = stm_now();

		SDL_Event Event;
		while (SDL_PollEvent(&Event)) {
			if (ApplicationEventShouldExit(_App, &Event)) {
				ApplicationStopRunning(_App);
			}
#ifdef _DEBUG
			if (Event.type == SDL_EVENT_KEY_DOWN) {
				ApplicationDebugKeyDown(_App, Event.key.scancode);
			}
#endif
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
		SDL_SetWindowPosition(Window, Bounds.x, Bounds.y);
		SDL_SetWindowFullscreen(Window, true);

		int WindowWidth, WindowHeight;
		SDL_GetWindowSizeInPixels(Window, &WindowWidth, &WindowHeight);

		arrput(
			App->Displays,
			((ShaverDisplay){
				.Display =
					{
						.Width = WindowWidth,
						.Height = WindowHeight,
					},
				.DisplayID = DisplayID,
				.DisplayMode = DisplayMode,
				.Window = Window,
				.Renderer = Renderer,
				.Bounds = Bounds,
				.ScreenshotTexture = SDL_CreateTextureFromSurface(Renderer, App->Screenshot),
				.RazorTexture = SDL_CreateTextureFromSurface(Renderer, App->RazorConfig.Image),
				.ShavedTexture = SDL_CreateTexture(
					Renderer,
					SDL_PIXELFORMAT_RGBA32,
					SDL_TEXTUREACCESS_STREAMING,
					WindowWidth,
					WindowHeight),
				.ShavedSurface = SDL_CreateSurface(WindowWidth, WindowHeight, SDL_PIXELFORMAT_RGBA32),
				.ActivePattern = 1,
			}));

		ShaverDisplay* NewDisplay = &arrlast(App->Displays);

		RazorSetPosition(&NewDisplay->Razor, GetRazorCenterDisplayPosition((Display*)NewDisplay, &App->RazorConfig));
		RazorWait(&NewDisplay->Razor, 1.0f, RazorMoveFinished);
	}
}

void ApplicationUpdate(ShaverApplication* App, const GameTime* Time)
{
	DebugNextFrame();

	const float32 TimeScale = 1.0f;

	InterpolatorContextUpdate(App->InterpolatorContext, Time->DeltaTimeF * TimeScale);

	for (int DisplayIndex = 0; DisplayIndex < arrlen(App->Displays); DisplayIndex++) {
		ShaverDisplay* Display = &App->Displays[DisplayIndex];

		float32 Value = RazorEvaluatePosition(&Display->Razor);

		if (Display->Razor.Behavior == RazorBehavior_Shave) {
			SDL_Rect LastShaveBounds = PositionToRazorShaveBounds(&App->RazorConfig, Display->Razor.LastPosition);
			SDL_Rect ShaveBounds = PositionToRazorShaveBounds(&App->RazorConfig, Display->Razor.Position);

			SDL_Surface* Pattern = App->Patterns[Display->Razor.CycleIndex % arrlen(App->Patterns)];

			int ShaveBottom = MIN((ShaveBounds.y + ShaveBounds.h) / Pattern->h * Pattern->h, Display->ShavedSurface->h);
			int ShaveRight = MIN(ShaveBounds.x + ShaveBounds.w, Display->ShavedSurface->w);

			for (int ShaveY = LastShaveBounds.y / Pattern->h * Pattern->h; ShaveY < ShaveBottom; ShaveY += Pattern->h) {
				for (int ShaveX = ShaveBounds.x; ShaveX < ShaveRight; ShaveX += Pattern->w) {
					SDL_BlitSurface(
						Pattern,
						NULL,
						Display->ShavedSurface,
						&(SDL_Rect){ShaveX, ShaveY, Pattern->w, Pattern->h});
				}
			}

			SDL_Surface* ShavedSurface;
			SDL_LockTextureToSurface(Display->ShavedTexture, NULL, &ShavedSurface);
			SDL_memcpy(
				ShavedSurface->pixels,
				Display->ShavedSurface->pixels,
				Display->ShavedSurface->pitch * Display->ShavedSurface->h);
			SDL_UnlockTexture(Display->ShavedTexture);
		}

		if (DisplayIndex == 0) {
			DebugPrintf("POS: %0.1f, %0.1f", Display->Razor.Position.X, Display->Razor.Position.Y);
			DebugPrintf("START: %0.1f, %0.1f", Display->Razor.StartPosition.X, Display->Razor.StartPosition.Y);
			DebugPrintf("TARGET: %0.1f, %0.1f", Display->Razor.TargetPosition.X, Display->Razor.TargetPosition.Y);
			DebugPrintf("STATE: %s", GetRazorBehaviorName(Display->Razor.Behavior));
			DebugPrintf("VALUE: %f", Value);
		}
	}
}

void ApplicationRender(ShaverApplication* App)
{
	for (int DisplayIndex = 0; DisplayIndex < arrlen(App->Displays); DisplayIndex++) {
		ShaverDisplay* Display = &App->Displays[DisplayIndex];

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
		SDL_RenderTexture(Display->Renderer, Display->ShavedTexture, NULL, NULL);

		SDL_RenderTexture(
			Display->Renderer,
			Display->RazorTexture,
			NULL,
			&(SDL_FRect){Display->Razor.Position.X,
						 Display->Razor.Position.Y,
						 App->RazorConfig.Image->w,
						 App->RazorConfig.Image->h});

#ifdef _DEBUG
		if (App->EnableDebugDraw) {
			if (DisplayIndex == 0) {
				DebugDraw(Display->Renderer);
			}
		}
#endif

		SDL_RenderPresent(Display->Renderer);
	}
}

bool ApplicationIsRunning(ShaverApplication* App)
{
	return !App->RequestShutdown;
}

void ApplicationStopRunning(ShaverApplication* App)
{
	App->RequestShutdown = true;
}

bool ApplicationEventShouldExit(ShaverApplication* App, const SDL_Event* Event)
{
	bool Result = false;
	switch (Event->type) {
		case SDL_EVENT_QUIT:
			// case SDL_EVENT_MOUSE_MOTION:
#ifdef _DEBUG
		case SDL_EVENT_KEY_DOWN:
			if (Event->key.scancode == SDL_SCANCODE_ESCAPE) {
				Result = true;
			}
			break;
#else
		case SDL_EVENT_MOUSE_BUTTON_DOWN:
		case SDL_EVENT_MOUSE_WHEEL:
		case SDL_EVENT_KEY_DOWN: Result = true; break;
		case SDL_EVENT_MOUSE_MOTION:
			if (Len(V2(Event->motion.xrel, Event->motion.yrel)) > 4.0f) {
				Result = true;
			}
			break;
#endif
	}
	return Result;
}

void ApplicationDebugKeyDown(ShaverApplication* App, SDL_Scancode scancode)
{
	switch (scancode) {
		case SDL_SCANCODE_F1: TOGGLE(App->EnableDebugDraw); break;
		default: break;
	}
}

bool LoadImage(const char* FileName, SDL_Surface** OutSurface)
{
	*OutSurface = NULL;

	int Width, Height, Channels;
	stbi_uc* ImageData = stbi_load(FileName, &Width, &Height, &Channels, 4);

	if (!ImageData) {
		return false;
	}

	SDL_Surface* Surface = SDL_CreateSurfaceFrom(Width, Height, SDL_PIXELFORMAT_RGBA32, ImageData, Width * 4);

	if (!Surface) {
		return false;
	}

	*OutSurface = Surface;
	return true;
}

bool LoadImageFromMemory(const void* Data, size_t Bytes, SDL_Surface** OutSurface)
{
	*OutSurface = NULL;

	int Width, Height, Channels;
	stbi_uc* ImageData = stbi_load_from_memory((stbi_uc*)Data, Bytes, &Width, &Height, &Channels, 4);

	if (!ImageData) {
		return false;
	}

	SDL_Surface* Surface = SDL_CreateSurfaceFrom(Width, Height, SDL_PIXELFORMAT_RGBA32, ImageData, Width * 4);

	if (!Surface) {
		return false;
	}

	*OutSurface = Surface;
	return true;
}

Display* GetDisplayForRazor(Application* App, RazorState* Razor)
{
	ShaverApplication* _App = (ShaverApplication*)App;
	for (int DisplayIndex = 0, DisplayCount = arrlen(_App->Displays); DisplayIndex < DisplayCount; DisplayIndex++) {
		if (&_App->Displays[DisplayIndex].Razor == Razor) {
			return (Display*)&_App->Displays[DisplayIndex];
		}
	}

	return NULL;
}