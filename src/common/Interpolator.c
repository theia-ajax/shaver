#include "Interpolator.h"

#include <stb_ds.h>

#include "Debug.h"
#include "Log.h"

typedef struct InterpolatorContext {
	Interpolator* Interpolators;
	handint NextInterpolatorId;
} InterpolatorContext;

typedef struct Interpolator {
	InterpolatorFunction Function;
	InterpolatorOnFinish OnFinish;
	void* UserData;
	InterpolatorHandle Id;
	float32 Time;
	float32 Duration;
	float32 Start;
	float32 Final;
	float32 TimeScale;
	uint32 Flags;
} Interpolator;

const Interpolator* GetInterpolatorConst(const InterpolatorContext* Context, InterpolatorHandle Handle);
Interpolator* GetInterpolator(const InterpolatorContext* Context, InterpolatorHandle Handle);

InterpolatorContext* CreateInterpolatorContext()
{
	InterpolatorContext* Context = SDL_malloc(sizeof(InterpolatorContext));
	SDL_zerop(Context);

	Context->NextInterpolatorId = 1;
	arrsetcap(Context->Interpolators, 64);

	return Context;
}

void DestroyInterpolatorContext(InterpolatorContext* Context)
{
	arrfree(Context->Interpolators);
	SDL_free(Context);
}

void InterpolatorContextUpdate(InterpolatorContext* Context, float32 DeltaTime)
{
	for (int InterpolatorIndex = 0, InterpolatorCount = arrlen(Context->Interpolators);
		 InterpolatorIndex < InterpolatorCount;
		 InterpolatorIndex++)
	{
		Interpolator* Interp = &Context->Interpolators[InterpolatorIndex];
		Interp->Time += DeltaTime * Interp->TimeScale;

		if (Interp->Time >= Interp->Duration) {
			Interp->Time = Interp->Duration;
			Interp->TimeScale = 0.0f;
			Interp->Flags |= InterpolatorFlags_Destroy;
			if (Interp->OnFinish) {
				Interp->OnFinish(Context, Interp->Id, Interp->UserData);
			}
		}
	}

	for (int InterpolatorIndex = arrlen(Context->Interpolators) - 1; InterpolatorIndex >= 0; InterpolatorIndex--) {
		Interpolator* Interp = &Context->Interpolators[InterpolatorIndex];
		if ((Interp->Flags & InterpolatorFlags_Destroy) != 0) {
			arrdelswap(Context->Interpolators, InterpolatorIndex);
		}
		if (InterpolatorIndex == 0) {
			DebugPrintf("INTERP: %0.3f/%0.3f", Interp->Time, Interp->Duration);
		}
	}
}

const Interpolator* GetInterpolatorConst(const InterpolatorContext* Context, InterpolatorHandle Handle)
{
	return GetInterpolator(Context, Handle);
}

Interpolator* GetInterpolator(const InterpolatorContext* Context, InterpolatorHandle Handle)
{
	if (!VALID_HANDLE(Handle)) {
		return NULL;
	}

	for (int Index = 0; Index < arrlen(Context->Interpolators); Index++) {
		Interpolator* Interp = &Context->Interpolators[Index];
		if (HANDLE_EQ(Interp->Id, Handle)) {
			return Interp;
		}
	}
}

float32 EvalInterpolator(const InterpolatorContext* Context, InterpolatorHandle Handle)
{
	Interpolator* Interp = GetInterpolator(Context, Handle);
	if (Interp == NULL) {
		return 1.0f;
	}

	return Interp->Function(Interp->Start, Interp->Final, Interp->Time, Interp->Duration);
}

InterpolatorHandle CreateInterpolator(
	InterpolatorContext* Context,
	InterpolatorFunction Func,
	float32 Start,
	float32 Final,
	float32 Duration,
	InterpolatorOnFinish OnFinish,
	void* UserData)
{
	InterpolatorHandle Id = (InterpolatorHandle){Context->NextInterpolatorId++};
	arrput(
		Context->Interpolators,
		((Interpolator){
			.Id = Id,
			.Function = Func,
			.OnFinish = OnFinish,
			.Flags = InterpolatorFlags_None,
			.Start = Start,
			.Final = Final,
			.Duration = Duration,
			.TimeScale = 1.0f,
			.UserData = UserData,
		}));
	return Id;
}

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
