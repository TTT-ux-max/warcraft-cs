@echo off
set MSVC=F:/PY/msvc/VC/Tools/MSVC/14.44.35207
set SDK=F:/PY/msvc/Windows Kits\10
set PATH=%MSVC%\bin\Hostx64\x64;%PATH%
echo === kernel32 (um/x86) ===
dumpbin /nologo /headers "%SDK%\Lib\10.0.26100.0\um\x86\kernel32.lib" | findstr /i "machine"
echo === gdi32 (um/x86) ===
dumpbin /nologo /headers "%SDK%\Lib\10.0.26100.0\um\x86\gdi32.lib" | findstr /i "machine"
echo === opengl32 (um/x86) ===
dumpbin /nologo /headers "%SDK%\Lib\10.0.26100.0\um\x86\opengl32.lib" | findstr /i "machine"
echo === ucrt/x86 ucrt.lib ===
dumpbin /nologo /headers "%SDK%\Lib\10.0.26100.0\ucrt\x86\ucrt.lib" | findstr /i "machine"
