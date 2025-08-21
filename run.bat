@echo off

set TARGET=screenshaver

if "%1"=="" (
	set "CONFIG=debug"
) else (
	set "CONFIG=%1"
)
rem copy_dlls %CONFIG%
.\bin\%TARGET%\bin\win64\%CONFIG%\%TARGET%.exe
