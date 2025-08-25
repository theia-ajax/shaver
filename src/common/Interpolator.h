#pragma once

#include "Types.h"

typedef struct InterpolatorContext InterpolatorContext;
typedef struct Interpolator Interpolator;

enum InterpolatorFlags {
	InterpolatorFlags_None = 0,
	InterpolatorFlags_Destroy = 1 << 0,
};

DEFINE_HANDLE(InterpolatorHandle);

typedef float32 (*InterpolatorFunction)(float32, float32, float32, float32);
typedef void (*InterpolatorOnFinish)(InterpolatorContext*, InterpolatorHandle, void*);

InterpolatorContext* CreateInterpolatorContext();
void DestroyInterpolatorContext(InterpolatorContext* Context);
void InterpolatorContextUpdate(InterpolatorContext* Context, float32 DeltaTime);

InterpolatorHandle CreateInterpolator(
	InterpolatorContext* Context,
	InterpolatorFunction Func,
	float32 Start,
	float32 Final,
	float32 Duration,
	InterpolatorOnFinish OnFinish,
	void* UserData);
float32 EvalInterpolator(const InterpolatorContext* Context, InterpolatorHandle Handle);

float32 InterpFuncLinear(float32 A, float32 B, float32 Time, float32 Duration);
float32 InterpFuncEaseInQuad(float32 A, float32 B, float32 Time, float32 Duration);
float32 InterpFuncEaseOutQuad(float32 A, float32 B, float32 Time, float32 Duration);
float32 InterpFuncEaseInOutQuad(float32 A, float32 B, float32 Time, float32 Duration);
