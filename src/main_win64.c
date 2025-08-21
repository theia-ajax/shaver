#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <stdlib.h>
#include "common/Application.h"

#include <SDL3/SDL.h>

Application *GApp = NULL;

int main(int argc, char* argv[])
{
	Application* App = ApplicationInitialize(&(ApplicationConfig){});

	GApp = App;
	ApplicationRun(App);
	ApplicationShutdown(App);

	return 0;
}

SDL_NORETURN void PanicAndAbort(const char* Title, const char* Message)
{
	SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, Title, Message, GetApplicationWindow(GApp));
	exit(1);
}

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#pragma comment(lib, "gdi32.lib")

bool ApplicationTakeDesktopScreenshot(SDL_Surface **OutSurface)
{
	HDC hScreenDC = GetDC(NULL);
	HDC hMemoryDC = CreateCompatibleDC(hScreenDC);
	int screenX = GetSystemMetrics(SM_XVIRTUALSCREEN);
	int screenY = GetSystemMetrics(SM_YVIRTUALSCREEN);
	int width = GetSystemMetrics(SM_CXVIRTUALSCREEN);
	int height = GetSystemMetrics(SM_CYVIRTUALSCREEN);

	BITMAPINFO bi;
	ZeroMemory(&bi, sizeof(BITMAPINFO));
	bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
	bi.bmiHeader.biWidth = width;
	bi.bmiHeader.biHeight = height;
	bi.bmiHeader.biPlanes = 1;
	bi.bmiHeader.biBitCount = 32;

	BYTE* lpBitmapBits = NULL;

	HBITMAP hBitmap = CreateDIBSection(hScreenDC, &bi, DIB_RGB_COLORS, (LPVOID*)&lpBitmapBits, NULL, 0);
	HBITMAP hOldBitmap = SDL_static_cast(HBITMAP, SelectObject(hMemoryDC, hBitmap));
	BitBlt(hMemoryDC, 0, 0, width, height, hScreenDC, 0, 0, SRCCOPY);
	hBitmap = SDL_static_cast(HBITMAP, SelectObject(hMemoryDC, hOldBitmap));

	BITMAP bitmap;
	GetObject(hBitmap, sizeof(bitmap), (LPVOID)&bitmap);

	BITMAP oldBitmap;
	GetObject(hOldBitmap, sizeof(oldBitmap), (LPVOID)&oldBitmap);
	
	SDL_Surface *Result = SDL_CreateSurface(bitmap.bmWidth, bitmap.bmHeight, SDL_PIXELFORMAT_RGBA32);

	LONG bytesPerPixel = bitmap.bmWidthBytes / bitmap.bmWidth;
	for (LONG i = 0; i < bitmap.bmWidthBytes * bitmap.bmHeight; i += bytesPerPixel)
	{
		uint8* srcPixels = (uint8*)bitmap.bmBits;
		uint8* dstPixels = (uint8*)Result->pixels;
		dstPixels[i + 0] = srcPixels[i + 2];
		dstPixels[i + 1] = srcPixels[i + 1];
		dstPixels[i + 2] = srcPixels[i + 0];
		dstPixels[i + 3] = srcPixels[i + 3];
	}

	SDL_FlipSurface(Result, SDL_FLIP_VERTICAL);

	*OutSurface = Result;

	DeleteDC(hMemoryDC);
	ReleaseDC(NULL, hScreenDC);
}