@echo off
call "F:/PY/msvc/VC/Auxiliary/Build/vcvars32.bat" >nul
echo CL=%CL% 2>nul
where cl.exe
where link.exe
cd /d F:/PY/cs/.workbuddy/analysis/tc
cl /nologo /c /Fo:t_x86.obj t.c
echo CL_EXIT=%ERRORLEVEL%
