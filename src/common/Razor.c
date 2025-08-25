#include "Razor.h"

#include <SDL3/SDL.h>
struct {
	Application* App;
	RazorConfig* Config;
} GRazor;

const char* RazorBehaviorNames[RazorBehavior_Count] =
	{"Idle", "Reposition", "WaitToShave", "Shave", "WaitToReposition", "Done,"};

void InitializeRazors(Application* App, RazorConfig* Config)
{
	GRazor.App = App;
	GRazor.Config = Config;
}

const char* GetRazorBehaviorName(RazorBehavior Behavior)
{
	SDL_assert(VALID_INDEX(Behavior, RazorBehavior_Count));
	return RazorBehaviorNames[Behavior];
}

void RazorSetPosition(RazorState* Razor, Vec2 Position)
{
	Razor->Position = Position;
	Razor->StartPosition = Position;
	Razor->TargetPosition = Position;
}

void RazorWait(RazorState* Razor, float32 Duration, InterpolatorOnFinish OnFinish)
{
	Razor->StartPosition = Razor->Position;
	Razor->TargetPosition = Razor->Position;
	Razor->InterpolatorId =
		CreateInterpolator(GRazor.Config->InterpolatorContext, InterpFuncLinear, 0.0f, 1.0f, Duration, OnFinish, Razor);
}

void RazorMoveTo(RazorState* Razor, Vec2 TargetPosition, float32 Duration, InterpolatorOnFinish OnFinish)
{
	Razor->StartPosition = Razor->Position;
	Razor->TargetPosition = TargetPosition;
	Razor->InterpolatorId =
		CreateInterpolator(GRazor.Config->InterpolatorContext, InterpFuncLinear, 0.0f, 1.0f, Duration, OnFinish, Razor);
}

void RazorEaseTo(RazorState* Razor, Vec2 TargetPosition, float32 Duration, InterpolatorOnFinish OnFinish)
{
	Razor->StartPosition = Razor->Position;
	Razor->TargetPosition = TargetPosition;
	Razor->InterpolatorId = CreateInterpolator(
		GRazor.Config->InterpolatorContext,
		InterpFuncEaseInOutQuad,
		0.0f,
		1.0f,
		Duration,
		OnFinish,
		Razor);
}

SDL_Rect PositionToRazorShaveBounds(const RazorConfig* Razor, Vec2 Position)
{
	return (SDL_Rect){

		.x = (int)round(Position.X) + Razor->BladeBounds.x,
		.y = (int)round(Position.Y) + Razor->BladeBounds.y,
		.w = Razor->BladeBounds.w,
		.h = Razor->BladeBounds.h,
	};
}

Vec2 GetRazorCenterDisplayPosition(const Display* RazorDisplay, const RazorConfig* Razor)
{
	return V2(RazorDisplay->Width / 2 - Razor->Image->w / 2, RazorDisplay->Height / 2 - Razor->Image->h / 2);
}

float32 RazorEvaluatePosition(RazorState* Razor)
{
	float32 Value = EvalInterpolator(GRazor.Config->InterpolatorContext, Razor->InterpolatorId);
	Razor->LastPosition = Razor->Position;
	Razor->Position = Lerp(Razor->StartPosition, Razor->TargetPosition, Value);
	return Value;
}

void RazorMoveFinished(InterpolatorContext* Context, InterpolatorHandle Interp, void* UserData)
{
	RazorState* Razor = (RazorState*)UserData;
	Display* RazorDisplay = GetDisplayForRazor(GRazor.App, Razor);

	RazorEvaluatePosition(Razor);

	const float32 IdleDuration = 0.75f;
	const float32 RepositionDuration = 0.33f;
	const float32 ShaveDuration = 1.0f;
	const float32 WaitToShaveDuration = 0.15f;
	const float32 WaitToRepositionDuration = 0.25f;

	switch (Razor->Behavior) {
		case RazorBehavior_Idle:
			Razor->ShaveIndex = 0;
			Razor->Behavior = RazorBehavior_Reposition;
			RazorEaseTo(Razor, V2(0, 0), IdleDuration, RazorMoveFinished);
			break;

		case RazorBehavior_Reposition:
			Razor->Behavior = RazorBehavior_WaitToShave;
			RazorWait(Razor, WaitToShaveDuration, RazorMoveFinished);
			break;

		case RazorBehavior_WaitToShave:
			Razor->Behavior = RazorBehavior_Shave;
			RazorMoveTo(Razor, V2(Razor->Position.X, RazorDisplay->Height), ShaveDuration, RazorMoveFinished);
			break;

		case RazorBehavior_Shave:
			Razor->Behavior = RazorBehavior_WaitToReposition;
			RazorWait(Razor, WaitToRepositionDuration, RazorMoveFinished);
			break;

		case RazorBehavior_WaitToReposition:
			Razor->ShaveIndex++;
			float32 NextX = Razor->ShaveIndex * GRazor.Config->BladeBounds.w;

			if (NextX < RazorDisplay->Width) {
				Razor->Behavior = RazorBehavior_Reposition;
				RazorEaseTo(Razor, V2(NextX, 0), RepositionDuration, RazorMoveFinished);
			} else {
				Razor->CycleIndex++;
				Razor->Behavior = RazorBehavior_Idle;
				RazorWait(Razor, IdleDuration, RazorMoveFinished);

				// Razor->Behavior = RazorBehavior_Done;
				// RazorEaseTo(Razor, GetRazorCenterDisplayPosition(RazorDisplay, GRazor.Config), 1.0f, RazorMoveFinished);
			}
			break;

		case RazorBehavior_Done: Razor->InterpolatorId = INVALID_HANDLE(InterpolatorHandle); break;
	}
}
