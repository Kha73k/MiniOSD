@echo off
cd /d "%~dp0"
windres osd.rc -O coff -o osd.res || exit /b 1
gcc -Os -s -Wall -mwindows -municode osd.c osd.res -ladvapi32 -lshell32 -ldwmapi -o osd.exe
