@echo off
set MSVC=F:/PY/msvc/VC/Tools/MSVC/14.44.35207
set PATH=%MSVC%\bin\Hostx64\x64;%PATH%
cd /d F:/PY/cs/.workbuddy/analysis
dumpbin /nologo /directives "%MSVC%\lib\x86\libcmt.lib" > dirs-libcmt.txt 2>&1
dumpbin /nologo /directives "%MSVC%\lib\x86\libvcruntime.lib" > dirs-libvcruntime.txt 2>&1
dumpbin /nologo /directives "%MSVC%\lib\x86\libcpmt.lib" > dirs-libcpmt.txt 2>&1
echo done
