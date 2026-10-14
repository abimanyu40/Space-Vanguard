@echo off
title Build Space Vanguard 2.0
echo [Compiling Space Vanguard 2.0: Star Citadel...]
clang -O2 -std=c11 main.c -o game.exe -lwinmm -luser32
if %ERRORLEVEL% equ 0 (
    echo [OK] Build successful! game.exe is ready to play.
) else (
    echo [ERROR] Compilation failed.
)
pause
