@echo off
setlocal
cd /d "%~dp0"
if not exist build\Mods mkdir build\Mods
if not exist build\tests mkdir build\tests
pushd src
cl /nologo /std:c++17 /O2 /MT /EHsc /DUNICODE /D_UNICODE /LD AlphaTemporal.cpp /Fe..\build\Mods\AlphaTAA.dll /link user32.lib
if errorlevel 1 exit /b 1
cl /nologo /std:c++17 /O2 /MT /EHsc /DUNICODE /D_UNICODE TemporalGpuTest.cpp /Fe..\build\tests\TemporalGpuTest.exe /link user32.lib d3d9.lib
set "result=%ERRORLEVEL%"
popd
exit /b %result%
