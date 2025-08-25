#pragma once

#include <SDL3/SDL_rect.h>

#include "Interpolator.h"
#include "Math2D.h"
#include "Display.h"

typedef struct SDL_Surface SDL_Surface;
typedef struct Application Application;

typedef struct RazorConfig {
	SDL_Surface* Image;
	SDL_Rect BladeBounds;
	InterpolatorContext *InterpolatorContext;
} RazorConfig;

typedef enum RazorBehavior {
	RazorBehavior_Idle,
	RazorBehavior_Reposition,
	RazorBehavior_WaitToShave,
	RazorBehavior_Shave,
	RazorBehavior_WaitToReposition,
	RazorBehavior_Done,
	RazorBehavior_Count,
} RazorBehavior;

typedef struct RazorState {
	InterpolatorHandle InterpolatorId;
	Vec2 StartPosition;
	Vec2 TargetPosition;
	Vec2 Position;
	Vec2 LastPosition;
	int32 Behavior;
	int32 CycleIndex;
	int32 ShaveIndex;
} RazorState;


void InitializeRazors(Application *App, RazorConfig *Config);
const char *GetRazorBehaviorName(RazorBehavior Behavior);

void RazorSetPosition(RazorState* Razor, Vec2 Position);
void RazorWait(RazorState* Razor, float32 Duration, InterpolatorOnFinish OnFinish);
void RazorMoveTo(RazorState* Razor, Vec2 TargetPosition, float32 Duration, InterpolatorOnFinish OnFinish);
void RazorEaseTo(RazorState* Razor, Vec2 TargetPosition, float32 Duration, InterpolatorOnFinish OnFinish);
SDL_Rect PositionToRazorShaveBounds(const RazorConfig* Razor, Vec2 Position);
Vec2 GetRazorCenterDisplayPosition(const Display *Disp, const RazorConfig* Razor);
float32 RazorEvaluatePosition(RazorState* Razor);
void RazorMoveFinished(InterpolatorContext* Context, InterpolatorHandle Interp, void* UserData);