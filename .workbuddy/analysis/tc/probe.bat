@echo off
call "F:/PY/msvc/VC/Auxiliary/Build/vcvars32.bat" >nul
cd /d F:/PY/cs/.workbuddy/analysis/tc
cl /nologo /LD /MT /std:c++17 /EHsc /W4 /O2 /DWIN32_LEAN_AND_MEAN /DNOMINMAX probe.cpp /link /OUT:probe.dll user32.lib gdi32.lib opengl32.lib version.lib winmm.lib ole32.lib windowscodecs.lib
echo PROBE_EXIT=%ERRORLEVEL%
