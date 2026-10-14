@echo off
call "F:/PY/msvc/VC/Auxiliary/Build/vcvars32.bat" >nul
cd /d F:/PY/cs/.workbuddy/analysis/tc
echo --- compile+link /MT exe ---
cl /nologo /MT /O2 t.c /Fe:t_x86.exe
echo EXE_EXIT=%ERRORLEVEL%
echo --- compile+link /MT dll ---
cl /nologo /LD /MT t.c /Fe:t_x86.dll /link /OUT:t_x86.dll
echo DLL_EXIT=%ERRORLEVEL%
