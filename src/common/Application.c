#include "Application.h"

#include <SDL3/SDL.h>
#include <sokol_time.h>
#include <stb_ds.h>
#include <stb_image.h>

#include "Debug.h"
#include "Log.h"
#include "Math2D.h"

float32 InterpFuncLinear(float32 A, float32 B, float32 Time, float32 Duration)
{
	return (B - A) * (Time / Duration) + A;
}

float32 InterpFuncEaseInQuad(float32 A, float32 B, float32 Time, float32 Duration)
{
	return (B - A) * (Time /= Duration) * Time + B;
}

float32 InterpFuncEaseOutQuad(float32 A, float32 B, float32 Time, float32 Duration)
{
	return (B - A) * (Time /= Duration) * (Time - 2) + B;
}

float32 InterpFuncEaseInOutQuad(float32 A, float32 B, float32 Time, float32 Duration)
{
	Time /= (Duration / 2.0f);
	if (Time < 1) {
		return (B - A) / 2 * Time * Time + A;
	} else {
		Time -= 1.0f;
		return -(B - A) / 2 * (Time * (Time - 2) - 1) + A;
	}
}

enum InterpolatorFlags {
	InterpolatorFlags_None = 0,
	InterpolatorFlags_Destroy = 1 << 0,
};

typedef struct ShaverApplication ShaverApplication;
typedef struct Interpolator Interpolator;

typedef float32 (*InterpolatorFunction)(float32, float32, float32, float32);
typedef void (*InterpolatorOnFinish)(ShaverApplication*, Interpolator*);

typedef struct Interpolator {
	int64 Id;
	InterpolatorFunction Function;
	InterpolatorOnFinish OnFinish;
	float32 Time;
	float32 Duration;
	float32 Start;
	float32 Final;
	float32 TimeScale;
	uint32 Flags;
} Interpolator;

typedef struct ShaverRazor {
	SDL_Surface* Image;
	SDL_Rect BladeBounds;
} ShaverRazor;

enum RazorBehavior {
	RazorBehavior_Idle,
	RazorBehavior_Reposition,
	RazorBehavior_WaitToShave,
	RazorBehavior_Shave,
	RazorBehavior_WaitToReposition,
	RazorBehavior_Done,
	RazorBehavior_Count,
};

const char* RazorBehaviorNames[RazorBehavior_Count] =
	{"Idle", "Reposition", "WaitToShave", "Shave", "WaitToReposition", "Done,"};

typedef struct RazorState {
	int64 InterpolatorId;
	Vec2 StartPosition;
	Vec2 TargetPosition;
	Vec2 Position;
	Vec2 LastPosition;
	int32 Behavior;
	int32 ShaveIndex;
} RazorState;

typedef struct ShaverDisplay {
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
	int32 WindowWidth;
	int32 WindowHeight;
} ShaverDisplay;

typedef struct ShaverApplication {
	ShaverDisplay* Displays;
	uint32 Colors[16];
	SDL_Surface* Screenshot;
	Interpolator* Interpolators;
	SDL_Surface* Pattern;
	SDL_Palette* PatternPalette;
	int64 NextInterpolatorId;
	ShaverRazor Razor;
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

int64 CreateInterpolator(
	ShaverApplication* App,
	InterpolatorFunction Func,
	float32 Start,
	float32 Final,
	float32 Duration,
	InterpolatorOnFinish OnFinish);
Interpolator* GetInterpolator(const ShaverApplication* App, int64 Id);
float32 EvalInterpolator(Interpolator* Interp);

void RazorSetPosition(RazorState* Razor, Vec2 Position);
void RazorWait(ShaverApplication* App, RazorState* Razor, float32 Duration, InterpolatorOnFinish OnFinish);
void RazorMoveTo(
	ShaverApplication* App,
	RazorState* Razor,
	Vec2 TargetPosition,
	float32 Duration,
	InterpolatorOnFinish OnFinish);
void RazorEaseTo(
	ShaverApplication* App,
	RazorState* Razor,
	Vec2 TargetPosition,
	float32 Duration,
	InterpolatorOnFinish OnFinish);
SDL_Rect PositionToRazorShaveBounds(const ShaverRazor* Razor, Vec2 Position);
Vec2 GetRazorCenterDisplayPosition(const ShaverRazor* Razor, const ShaverDisplay* Display);
float32 RazorEvaluatePosition(const ShaverApplication* App, RazorState* Razor);

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
	ApplicationTakeDesktopScreenshot(&App->Screenshot);

	{
		// clang-format off
		static const char RazorImageData[] = {
			#embed "razor.png"
		};
		// clang-format on
		LoadImageFromMemory(RazorImageData, sizeof(RazorImageData), &App->Razor.Image);
		App->Razor.BladeBounds = (SDL_Rect){0, 0, 118, 29};
	}

	{
		static const char Pattern[] = {
			0x0F,
			0x0F,
			0x0F,
			0x0F,
			0xF0,
			0xF0,
			0xF0,
			0xF0,
		};
		SDL_CreateSurfaceFrom(8, 8, SDL_PIXELFORMAT_INDEX1LSB, (void*)Pattern, 1);
		// clang-format off
		static const char PatternImageData[] = {
			#embed "pattern_00.png"
		};
		// clang-format on
		LoadImageFromMemory(PatternImageData, sizeof(PatternImageData), &App->Pattern);
		SDL_Color Colors[] = {
			(SDL_Color){0, 255, 255, 255},
			(SDL_Color){0, 0, 255, 255},
		};
		SDL_SetPaletteColors(SDL_GetSurfacePalette(App->Pattern), Colors, 0, 2);
	}

	ApplicationCreateDisplays(App);

	App->Colors[0] = SDL_MapSurfaceRGBA(App->Razor.Image, 0, 0, 255, 255);
	App->Colors[1] = SDL_MapSurfaceRGBA(App->Razor.Image, 0, 255, 255, 255);

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

void RazorMoveFinished(ShaverApplication* App, Interpolator* Interp)
{
	ShaverDisplay* RazorDisplay = NULL;
	RazorState* Razor = NULL;
	for (int DisplayIndex = 0; DisplayIndex < arrlen(App->Displays); DisplayIndex++) {
		ShaverDisplay* Display = &App->Displays[DisplayIndex];
		if (Display->Razor.InterpolatorId == Interp->Id) {
			RazorDisplay = Display;
			break;
		}
	}

	if (RazorDisplay == NULL) {
		return;
	}

	Razor = &RazorDisplay->Razor;

	RazorEvaluatePosition(App, Razor);

	const float32 IdleDuration = 2.0f;
	const float32 RepositionDuration = 1.0f;
	const float32 ShaveDuration = 2.0f;
	const float32 WaitToShaveDuration = 0.5f;
	const float32 WaitToRepositionDuration = 0.75f;

	switch (Razor->Behavior) {
		case RazorBehavior_Idle:
			Razor->Behavior = RazorBehavior_Reposition;
			RazorEaseTo(App, Razor, V2(0, 0), IdleDuration, RazorMoveFinished);
			break;

		case RazorBehavior_Reposition:
			Razor->Behavior = RazorBehavior_WaitToShave;
			RazorWait(App, Razor, WaitToShaveDuration, RazorMoveFinished);
			break;

		case RazorBehavior_WaitToShave:
			Razor->Behavior = RazorBehavior_Shave;
			RazorMoveTo(
				App,
				Razor,
				V2(Razor->Position.X, RazorDisplay->WindowHeight + 32),
				ShaveDuration,
				RazorMoveFinished);
			break;

		case RazorBehavior_Shave:
			Razor->Behavior = RazorBehavior_WaitToReposition;
			RazorWait(App, Razor, WaitToRepositionDuration, RazorMoveFinished);
			break;

		case RazorBehavior_WaitToReposition:
			Razor->ShaveIndex++;
			float32 NextX = Razor->ShaveIndex * App->Razor.BladeBounds.w;

			if (NextX < RazorDisplay->WindowWidth) {
				Razor->Behavior = RazorBehavior_Reposition;
				RazorEaseTo(App, Razor, V2(NextX, 0), RepositionDuration, RazorMoveFinished);
			} else {
				RazorEaseTo(App, Razor, GetRazorCenterDisplayPosition(&App->Razor, RazorDisplay), 1.0f, NULL);
			}
			break;
	}
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
				.DisplayID = DisplayID,
				.DisplayMode = DisplayMode,
				.Window = Window,
				.Renderer = Renderer,
				.Bounds = Bounds,
				.ScreenshotTexture = SDL_CreateTextureFromSurface(Renderer, App->Screenshot),
				.RazorTexture = SDL_CreateTextureFromSurface(Renderer, App->Razor.Image),
				.ShavedTexture = SDL_CreateTexture(
					Renderer,
					SDL_PIXELFORMAT_RGBA32,
					SDL_TEXTUREACCESS_STREAMING,
					WindowWidth,
					WindowHeight),
				.ShavedSurface = SDL_CreateSurface(WindowWidth, WindowHeight, SDL_PIXELFORMAT_RGBA32),
				.WindowWidth = WindowWidth,
				.WindowHeight = WindowHeight,
			}));

		ShaverDisplay* Display = &arrlast(App->Displays);

		RazorSetPosition(&Display->Razor, GetRazorCenterDisplayPosition(&App->Razor, Display));
		RazorWait(App, &Display->Razor, 1.0f, RazorMoveFinished);
	}
}

void ApplicationUpdate(ShaverApplication* App, const GameTime* Time)
{
	DebugNextFrame();

	const float32 TimeScale = 3.0f;

	for (int InterpolatorIndex = 0, InterpolatorCount = arrlen(App->Interpolators);
		 InterpolatorIndex < InterpolatorCount;
		 InterpolatorIndex++)
	{
		Interpolator* Interp = &App->Interpolators[InterpolatorIndex];
		Interp->Time += Time->DeltaTimeF * Interp->TimeScale * TimeScale;

		if (Interp->Time >= Interp->Duration) {
			Interp->Time = Interp->Duration;
			Interp->TimeScale = 0.0f;
			Interp->Flags |= InterpolatorFlags_Destroy;
			if (Interp->OnFinish) {
				Interp->OnFinish(App, Interp);
			}
		}
	}

	for (int InterpolatorIndex = arrlen(App->Interpolators) - 1; InterpolatorIndex >= 0; InterpolatorIndex--) {
		Interpolator* Interp = &App->Interpolators[InterpolatorIndex];
		if ((Interp->Flags & InterpolatorFlags_Destroy) != 0) {
			arrdelswap(App->Interpolators, InterpolatorIndex);
		}
		if (InterpolatorIndex == 0) {
			DebugPrintf("INTERP: %0.3f/%0.3f", Interp->Time, Interp->Duration);
		}
	}

	for (int ShaverIndex = 0; ShaverIndex < arrlen(App->Displays); ShaverIndex++) {
		ShaverDisplay* Display = &App->Displays[ShaverIndex];

		float32 Value = RazorEvaluatePosition(App, &Display->Razor);

		if (Display->Razor.Behavior == RazorBehavior_Shave) {
			// int ShaveX = App->Razor.BladeBounds.x + (int)Display->Razor.Position.X;
			// int ShaveY = App->Razor.BladeBounds.y + (int)Display->Razor.Position.Y;
			// SDL_Rect Bounds = App->Razor.BladeBounds;
			// Bounds.x += (int)Display->Razor.Position.X;
			// Bounds.y = 0;
			// Bounds.h = (int)Display->Razor.Position.Y + Bounds.h;
			// SDL_FillSurfaceRect(
			// 	Display->ShavedSurface,
			// 	&Bounds,
			// 	SDL_MapRGB(SDL_GetPixelFormatDetails(SDL_PIXELFORMAT_RGBA32), NULL, 0, 255, 255));

			SDL_Rect LastShaveBounds = PositionToRazorShaveBounds(&App->Razor, Display->Razor.LastPosition);
			SDL_Rect ShaveBounds = PositionToRazorShaveBounds(&App->Razor, Display->Razor.Position);

			for (int ShaveY = LastShaveBounds.y; ShaveY < MIN(ShaveBounds.y + ShaveBounds.h, Display->ShavedSurface->h);
				 ShaveY += App->Pattern->h)
			{
				// SDL_FillSurfaceRect(
				// 	Display->ShavedSurface,
				// 	&(SDL_Rect){ShaveBounds.x, ShaveY, ShaveBounds.w, 1},
				// 	SDL_MapRGB(
				// 		SDL_GetPixelFormatDetails(SDL_PIXELFORMAT_RGBA32),
				// 		NULL,
				// 		0,
				// 		(ShaveY % 2 == 0) ? 255 : 0,
				// 		255));
				for (int ShaveX = ShaveBounds.x; ShaveX < MIN(ShaveBounds.x + ShaveBounds.w, Display->ShavedSurface->w);
					 ShaveX += App->Pattern->w)
				{
					SDL_BlitSurface(
						App->Pattern,
						NULL,
						Display->ShavedSurface,
						&(SDL_Rect){ShaveX, ShaveY, App->Pattern->w, App->Pattern->h});
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

		if (ShaverIndex == 0) {
			DebugPrintf("POS: %0.1f, %0.1f", Display->Razor.Position.X, Display->Razor.Position.Y);
			DebugPrintf("START: %0.1f, %0.1f", Display->Razor.StartPosition.X, Display->Razor.StartPosition.Y);
			DebugPrintf("TARGET: %0.1f, %0.1f", Display->Razor.TargetPosition.X, Display->Razor.TargetPosition.Y);
			DebugPrintf("STATE: %s", RazorBehaviorNames[Display->Razor.Behavior]);
			DebugPrintf("VALUE: %f", Value);
		}
	}
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
		SDL_RenderTexture(Display->Renderer, Display->ShavedTexture, NULL, NULL);

		SDL_RenderTexture(
			Display->Renderer,
			Display->RazorTexture,
			NULL,
			&(SDL_FRect){Display->Razor.Position.X,
						 Display->Razor.Position.Y,
						 App->Razor.Image->w,
						 App->Razor.Image->h});

#ifdef _DEBUG
		if (App->EnableDebugDraw) {
			if (ShaverIndex == 0) {
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
		case SDL_EVENT_KEY_DOWN:
#ifdef _DEBUG
			if (Event->key.scancode == SDL_SCANCODE_ESCAPE) {
				Result = true;
			}
#else
			Result = true;
#endif
			break;
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

int64 CreateInterpolator(
	ShaverApplication* App,
	InterpolatorFunction Func,
	float32 Start,
	float32 Final,
	float32 Duration,
	InterpolatorOnFinish OnFinish)
{
	int64 Id = App->NextInterpolatorId++;

	arrput(
		App->Interpolators,
		((Interpolator){
			.Id = Id,
			.Function = Func,
			.OnFinish = OnFinish,
			.Flags = InterpolatorFlags_None,
			.Start = Start,
			.Final = Final,
			.Duration = Duration,
			.TimeScale = 1.0f,
		}));

	return Id;
}

Interpolator* GetInterpolator(const ShaverApplication* App, int64 Id)
{
	for (int Index = 0; Index < arrlen(App->Interpolators); Index++) {
		Interpolator* Interp = &App->Interpolators[Index];
		if (Interp->Id == Id) {
			return Interp;
		}
	}

	return NULL;
}

float32 EvalInterpolator(Interpolator* Interp)
{
	if (Interp == NULL) {
		return 1.0f;
	}

	return Interp->Function(Interp->Start, Interp->Final, Interp->Time, Interp->Duration);
}

void RazorSetPosition(RazorState* Razor, Vec2 Position)
{
	Razor->Position = Position;
	Razor->StartPosition = Position;
	Razor->TargetPosition = Position;
}

void RazorWait(ShaverApplication* App, RazorState* Razor, float32 Duration, InterpolatorOnFinish OnFinish)
{
	Razor->StartPosition = Razor->Position;
	Razor->TargetPosition = Razor->Position;
	Razor->InterpolatorId = CreateInterpolator(App, InterpFuncLinear, 0.0f, 1.0f, Duration, OnFinish);
}

void RazorMoveTo(
	ShaverApplication* App,
	RazorState* Razor,
	Vec2 TargetPosition,
	float32 Duration,
	InterpolatorOnFinish OnFinish)
{
	Razor->StartPosition = Razor->Position;
	Razor->TargetPosition = TargetPosition;
	Razor->InterpolatorId = CreateInterpolator(App, InterpFuncLinear, 0.0f, 1.0f, Duration, OnFinish);
}

void RazorEaseTo(
	ShaverApplication* App,
	RazorState* Razor,
	Vec2 TargetPosition,
	float32 Duration,
	InterpolatorOnFinish OnFinish)
{
	Razor->StartPosition = Razor->Position;
	Razor->TargetPosition = TargetPosition;
	Razor->InterpolatorId = CreateInterpolator(App, InterpFuncEaseInOutQuad, 0.0f, 1.0f, Duration, OnFinish);
}

SDL_Rect PositionToRazorShaveBounds(const ShaverRazor* Razor, Vec2 Position)
{
	return (SDL_Rect){

		.x = (int)round(Position.X) + Razor->BladeBounds.x,
		.y = (int)round(Position.Y) + Razor->BladeBounds.y,
		.w = Razor->BladeBounds.w,
		.h = Razor->BladeBounds.h,
	};
}

Vec2 GetRazorCenterDisplayPosition(const ShaverRazor* Razor, const ShaverDisplay* Display)
{
	return V2(Display->WindowWidth / 2 - Razor->Image->w / 2, Display->WindowHeight / 2 - Razor->Image->h / 2);
}

float32 RazorEvaluatePosition(const ShaverApplication* App, RazorState* Razor)
{
	Interpolator* Interp = GetInterpolator(App, Razor->InterpolatorId);
	float32 Value = EvalInterpolator(Interp);
	Razor->LastPosition = Razor->Position;
	Razor->Position = Lerp(Razor->StartPosition, Razor->TargetPosition, Value);
	return Value;
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