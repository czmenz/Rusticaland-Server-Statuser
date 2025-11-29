@echo off
setlocal
set "ROOT=%~dp0"
for /f "delims=" %%t in ('powershell -NoProfile -Command "(Get-Date).ToString('yyyy-MM-dd_HH-mm-ss')"') do set "BUILDSTAMP=%%t"
set "OUT=%ROOT%Builds\%BUILDSTAMP%"
if not exist "%ROOT%Builds" mkdir "%ROOT%Builds"
if not exist "%OUT%" mkdir "%OUT%"
set "RESDIR=%ROOT%External\res"
set "RESBUILD=%ROOT%res_build"
if not exist "%RESBUILD%" mkdir "%RESBUILD%"

rem Code signing config (override via env: CODESIGN_PFX, CODESIGN_PWD, CODESIGN_TS)
set "CERT_PFX=%CODESIGN_PFX%"
if "%CERT_PFX%"=="" set "CERT_PFX=%ROOT%Rusticaland.pfx"
set "CERT_PWD=%CODESIGN_PWD%"
if "%CERT_PWD%"=="" set "CERT_PWD=123"
set "TIMESTAMP_URL=%CODESIGN_TS%"
if "%TIMESTAMP_URL%"=="" set "TIMESTAMP_URL=http://timestamp.digicert.com"

echo =====================================
echo Build Debug
echo ROOT=%ROOT%
echo BUILDSTAMP=%BUILDSTAMP%
echo OUT=%OUT%
echo RESDIR=%RESDIR%
echo RESBUILD=%RESBUILD%
echo CERT_PFX=%CERT_PFX%
if "%CERT_PWD%"=="" (echo CERT_PWD=EMPTY) else (echo CERT_PWD=SET)
echo TIMESTAMP_URL=%TIMESTAMP_URL%
echo =====================================

rem Try MSVC first
where cl >nul 2>&1
if %errorlevel%==0 goto build_msvc

rem Attempt to locate vcvarsall via vswhere
set "VCVARS="
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if exist "%VSWHERE%" (
  for /f "delims=" %%i in ('"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -find **\vcvarsall.bat') do set "VCVARS=%%i"
  if not "%VCVARS%"=="" (
    call "%VCVARS%" x64
    where cl >nul 2>&1
    if %errorlevel%==0 goto build_msvc
  )
)

rem Fallback to MinGW
where g++ >nul 2>&1
if %errorlevel%==0 goto build_mingw

echo C/C++ toolchain not found. Install Visual Studio Build Tools or MinGW.
exit /b 1

:build_msvc
rem Compile resources
echo [MSVC] rc "%RESDIR%\checker.rc" -> "%RESBUILD%\checker.res"
pushd "%RESDIR%" && rc /nologo /fo "%RESBUILD%\checker.res" /d ICONPATH=\"%ROOT%ico.ico\" "checker.rc" && popd
if %errorlevel% neq 0 exit /b %errorlevel%
echo [MSVC] rc "%RESDIR%\downloader.rc" -> "%RESBUILD%\downloader.res"
pushd "%RESDIR%" && rc /nologo /fo "%RESBUILD%\downloader.res" /d ICONPATH=\"%ROOT%ico.ico\" "downloader.rc" && popd
if %errorlevel% neq 0 exit /b %errorlevel%

cl /nologo /EHsc /O2 /MT ^
  "%ROOT%Checker\main.cpp" ^
  "%ROOT%Checker\logging.cpp" ^
  "%ROOT%Checker\API\Battlemetrics.cpp" ^
  "%ROOT%Checker\GUI\gui.cpp" ^
  "%ROOT%Checker\Network\CheckInternet.cpp" ^
  "%ROOT%Checker\Network\CheckWebsite.cpp" ^
  "%ROOT%Checker\Network\CheckVPN.cpp" ^
  "%ROOT%Checker\Network\CheckServers.cpp" ^
  "%ROOT%Checker\Firewall\ReadFirewall.cpp" ^
  "%ROOT%Checker\Firewall\WriteFirewall.cpp" ^
  "%ROOT%Checker\Firewall\ForceAllowFirewall.cpp" ^
  "%ROOT%External\ImGUI\imgui.cpp" ^
  "%ROOT%External\ImGUI\imgui_draw.cpp" ^
  "%ROOT%External\ImGUI\imgui_widgets.cpp" ^
  "%ROOT%External\ImGUI\imgui_tables.cpp" ^
  "%ROOT%External\ImGUI\imgui_impl_win32.cpp" ^
  "%ROOT%External\ImGUI\imgui_impl_dx9.cpp" ^
  /link /out:"%OUT%\Rusticaland Server Checker.exe" user32.lib winhttp.lib ws2_32.lib iphlpapi.lib d3d9.lib gdi32.lib dwmapi.lib "%RESBUILD%\checker.res" /SUBSYSTEM:CONSOLE
if %errorlevel% neq 0 exit /b %errorlevel%
if %errorlevel% neq 0 exit /b %errorlevel%

cl /nologo /EHsc /O2 /MT ^
  "%ROOT%Downloader\main.cpp" ^
  "%ROOT%Downloader\gui.cpp" ^
  "%ROOT%External\ImGUI\imgui.cpp" ^
  "%ROOT%External\ImGUI\imgui_draw.cpp" ^
  "%ROOT%External\ImGUI\imgui_widgets.cpp" ^
  "%ROOT%External\ImGUI\imgui_tables.cpp" ^
  "%ROOT%External\ImGUI\imgui_impl_win32.cpp" ^
  "%ROOT%External\ImGUI\imgui_impl_dx9.cpp" ^
  /link /out:"%OUT%\Rusticaland Downloader.exe" winhttp.lib d3d9.lib user32.lib gdi32.lib dwmapi.lib wintrust.lib crypt32.lib ole32.lib shell32.lib uuid.lib "%RESBUILD%\downloader.res" /SUBSYSTEM:WINDOWS
if %errorlevel% neq 0 exit /b %errorlevel%

rem Sign binaries if possible
rem Signing moved to final step
rem Cleanup resource build folder
rd /s /q "%RESBUILD%" >nul 2>&1
echo Built with MSVC: %OUT%
goto end

:build_mingw
rem Compile resources
echo [MinGW] windres "%RESDIR%\checker.rc" -> "%RESBUILD%\checker.o"
pushd "%RESDIR%" && windres -D ICONPATH=\"%ROOT%ico.ico\" "checker.rc" -O coff -o "%RESBUILD%\checker.o" && popd
if %errorlevel% neq 0 exit /b %errorlevel%
echo [MinGW] windres "%RESDIR%\downloader.rc" -> "%RESBUILD%\downloader.o"
pushd "%RESDIR%" && windres -D ICONPATH=\"%ROOT%ico.ico\" "downloader.rc" -O coff -o "%RESBUILD%\downloader.o" && popd
if %errorlevel% neq 0 exit /b %errorlevel%

g++ -O2 -mconsole -static -static-libgcc -static-libstdc++ -s -o "%OUT%\Rusticaland Server Checker.exe" ^
  "%ROOT%Checker\main.cpp" ^
  "%ROOT%Checker\logging.cpp" ^
  "%ROOT%Checker\API\Battlemetrics.cpp" ^
  "%ROOT%Checker\GUI\gui.cpp" ^
  "%ROOT%Checker\Network\CheckInternet.cpp" ^
  "%ROOT%Checker\Network\CheckWebsite.cpp" ^
  "%ROOT%Checker\Network\CheckVPN.cpp" ^
  "%ROOT%Checker\Network\CheckServers.cpp" ^
  "%ROOT%Checker\Firewall\ReadFirewall.cpp" ^
  "%ROOT%Checker\Firewall\WriteFirewall.cpp" ^
  "%ROOT%Checker\Firewall\ForceAllowFirewall.cpp" ^
  "%ROOT%External\ImGUI\imgui.cpp" ^
  "%ROOT%External\ImGUI\imgui_draw.cpp" ^
  "%ROOT%External\ImGUI\imgui_widgets.cpp" ^
  "%ROOT%External\ImGUI\imgui_tables.cpp" ^
  "%ROOT%External\ImGUI\imgui_impl_win32.cpp" ^
  "%ROOT%External\ImGUI\imgui_impl_dx9.cpp" ^
  "%RESBUILD%\checker.o" -luser32 -lwinhttp -lws2_32 -liphlpapi -ld3d9 -lgdi32 -ldwmapi
if %errorlevel% neq 0 exit /b %errorlevel%
if %errorlevel% neq 0 exit /b %errorlevel%

g++ -O2 -municode -mwindows -static -static-libgcc -static-libstdc++ -s -o "%OUT%\Rusticaland Downloader.exe" ^
  "%ROOT%Downloader\main.cpp" ^
  "%ROOT%Downloader\gui.cpp" ^
  "%ROOT%External\ImGUI\imgui.cpp" ^
  "%ROOT%External\ImGUI\imgui_draw.cpp" ^
  "%ROOT%External\ImGUI\imgui_widgets.cpp" ^
  "%ROOT%External\ImGUI\imgui_tables.cpp" ^
  "%ROOT%External\ImGUI\imgui_impl_win32.cpp" ^
  "%ROOT%External\ImGUI\imgui_impl_dx9.cpp" ^
  "%RESBUILD%\downloader.o" -lwinhttp -ld3d9 -lgdi32 -luser32 -ldwmapi -lwintrust -lcrypt32 -lole32 -lshell32 -luuid
if %errorlevel% neq 0 exit /b %errorlevel%

rem Sign binaries if possible
rem Signing moved to final step
rd /s /q "%RESBUILD%" >nul 2>&1
echo Built with MinGW: %OUT%

:end
echo =====================================
echo Final Signing Step
set "SIGNTOOL=%ROOT%External\signtool.exe"
set "TIMESTAMP_URL=%TIMESTAMP_URL:`=%"
if exist "%SIGNTOOL%" if exist "%CERT_PFX%" if not "%CERT_PWD%"=="" (
  echo Using signtool: %SIGNTOOL%
  echo PFX: %CERT_PFX%
  echo Timestamp URL: %TIMESTAMP_URL%
  timeout /t 3 /nobreak >nul
  if exist "%OUT%\Rusticaland Server Checker.exe" (
    "%SIGNTOOL%" sign /fd SHA256 /f "%CERT_PFX%" /p "%CERT_PWD%" /t "%TIMESTAMP_URL%" "%OUT%\Rusticaland Server Checker.exe"
  ) else (
    echo Server Checker exe not found, skipping
  )
  if exist "%OUT%\Rusticaland Downloader.exe" (
    "%SIGNTOOL%" sign /fd SHA256 /f "%CERT_PFX%" /p "%CERT_PWD%" /t "%TIMESTAMP_URL%" "%OUT%\Rusticaland Downloader.exe"
  ) else (
    echo Downloader exe not found, skipping
  )
) else (
  echo Skipping final signing: missing signtool/cert/password
)
echo =====================================
echo Build finished. Output: %OUT%
echo Press any key to exit.
pause >NUL
exit /b 0
