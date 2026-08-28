# SPDX-License-Identifier: AGPL-3.0-or-later
#
# Copyright (C) mrc-sk and imjumping
#
# This program is free software: you can redistribute it and/or modify it under
# the terms of the GNU Affero General Public License as published by the Free
# Software Foundation, either version 3 of the License, or (at your option)
# any later version. See LICENSE for the full license text and the additional
# non-commercial restriction terms that apply to this software.
@echo off
rem BerryOS - one-click build & run in QEMU (Windows)
rem Double-click this file, or run from any directory:  run.bat

cd /d "%~dp0"

echo [BerryOS] Building disk image...
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0build.ps1"
if errorlevel 1 goto :fail

echo [BerryOS] Launching QEMU (close the QEMU window to exit)...
start "" "C:\Program Files\qemu\qemu-system-x86_64.exe" -cpu max ^
  -drive file=build\disk.img,format=raw,if=ide ^
  -serial stdio -vga std -display gtk -no-reboot -no-shutdown

goto :end

:fail
echo.
echo [BerryOS] BUILD FAILED - see messages above.
pause
exit /b 1

:end
echo.
echo [BerryOS] QEMU launched. The serial console shows kernel output.
echo If the window flashes and closes, run QEMU manually and send me the error:
echo   "C:\Program Files\qemu\qemu-system-x86_64.exe" -display help
pause
