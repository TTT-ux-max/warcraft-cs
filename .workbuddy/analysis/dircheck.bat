@echo off
set MSVC=F:/PY/msvc/VC/Tools/MSVC/14.44.35207
set PATH=%MSVC%\bin\Hostx64\x64;%PATH%
cd /d F:/PY/cs/.workbuddy/analysis
for %%L in (libcmt.lib libvcruntime.lib libcpmt.lib) do (
  echo === %%L ===
  dumpbin /nologo /directives "%MSVC%\lib\x86\%%L" 2>nul | findstr /i "defaultlib" | sort /unique
)
