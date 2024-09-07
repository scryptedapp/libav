cd /d "%~dp0"
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat" x64
C:\msys64\msys2_shell.cmd -defterm -no-start -msys -use-full-path -here -c "bash build-ffmpeg-msys.sh"
exit /b %ERRORLEVEL%
