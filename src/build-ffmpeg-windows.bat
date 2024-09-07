cd /d "%~dp0"

call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat" x64

cd ..\libvpl
set VPL_INSTALL_DIR=%cd%\..\_vplinstall
@REM script\bootstrap.bat
cmake -B _build -DCMAKE_INSTALL_PREFIX=%VPL_INSTALL_DIR%
cmake --build _build --config Release
cmake --install _build --config Release

cd ..\src
C:\msys64\msys2_shell.cmd -defterm -no-start -msys -use-full-path -here -c "bash build-ffmpeg-msys.sh"

exit /b %ERRORLEVEL%
