@echo off
setlocal

set /p VERSION=<version.txt
set "RELEASE=v%VERSION%"

call :header Pushing release %RELEASE%

rem Delete local tag if it already exists
git tag -d "%RELEASE%" >nul 2>&1

rem Create new tag
git tag "%RELEASE%"
if errorlevel 1 exit /b %errorlevel%

rem Push tag to origin
git push --force origin "%RELEASE%"
if errorlevel 1 exit /b %errorlevel%

goto :eof


:print_delim
echo --------------------------------------------------------------------------------
exit /b 0


:header
echo.
call :print_delim
echo %*
call :print_delim
echo.
exit /b 0
