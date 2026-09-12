@echo off
setlocal
cl /nologo /EHsc /MD /O2 /Zi /Ibbmod/src tests/recovery.cpp /Fetests/recovery-tests.exe /Fotests/recovery-tests.obj /link bbmod/Release/*.obj libs/lua51.lib libs/libMinHook-x86-v140-mtd.lib user32.lib gdi32.lib imm32.lib advapi32.lib winmm.lib dxguid.lib dbghelp.lib /DEBUG /NODEFAULTLIB:libcmt /NODEFAULTLIB:libcmtd
if errorlevel 1 exit /b 1
tests\recovery-tests.exe
exit /b %errorlevel%
