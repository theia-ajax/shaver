@echo off

setlocal EnableDelayedExpansion
set CONFIG="debug"
set RUN="false"

FOR %%A IN (%*) DO (
    IF "%%A"=="/r" (
		set RUN="true"
	) ELSE (
		set "CONFIG=%1"
	)
)

call .\build_mingw64.bat %CONFIG%

if %RUN%=="true" ( call .\run.bat %CONFIG% )
