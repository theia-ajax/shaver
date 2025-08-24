@echo off

setlocal EnableDelayedExpansion

set PKGDIR="screenshaver"
set TARGET="screenshaver"
set CONFIG="release"
set PLATFORM="win64"
set ARCHIVE="%TARGET%.zip"
set EXECUTABLE="%TARGET%.exe"
set EXECUTABLE_SRC="bin\%TARGET%\bin\%PLATFORM%\%CONFIG%\%EXECUTABLE%"
set EXECUTABLE_PKG="%PKGDIR%\%EXECUTABLE%"
set ASSETSDIR="assets\"
set ASSETSDIR_PKG="%PKGDIR%\%ASSETSDIR%"

@RD /S /Q %PKGDIR%
@RD /S /Q %ARCHIVE%
@mkdir %PKGDIR%

call .\build.bat release

@copy %EXECUTABLE_SRC% %EXECUTABLE_PKG%
@REM @xcopy /S /Y %ASSETSDIR% %ASSETSDIR_PKG%
@xcopy /Y *.dll %PKGDIR%\.

7z a %ARCHIVE% %PKGDIR%