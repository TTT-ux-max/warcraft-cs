@echo off
set MSVC=F:/PY/msvc/VC/Tools/MSVC/14.44.35207
set SDK=F:/PY/msvc/Windows Kits\10
set INCLUDE=%MSVC%\include;%SDK%\Include\10.0.26100.0\ucrt;%SDK%\Include\10.0.26100.0\um;%SDK%\Include\10.0.26100.0\shared
set LIB=%MSVC%\lib\x64;%SDK%\Lib\10.0.26100.0\ucrt\x64;%SDK%\Lib\10.0.26100.0\um\x64
set PATH=%MSVC%\bin\Hostx64\x64;%PATH%
dumpbin.exe /nologo /disasm "D:/game/Warcraft III 1.27 Reforged/Game.dll" > "F:/PY/cs/.workbuddy/analysis/game127.disasm.txt" 2>&1
echo EXIT=%ERRORLEVEL%
