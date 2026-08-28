# SPDX-License-Identifier: AGPL-3.0-or-later
#
# Copyright (C) mrc-sk and imjumping
#
# This program is free software: you can redistribute it and/or modify it under
# the terms of the GNU Affero General Public License as published by the Free
# Software Foundation, either version 3 of the License, or (at your option)
# any later version. See LICENSE for the full license text and the additional
# non-commercial restriction terms that apply to this software.
# Headless QEMU run: boot BerryOS, capture serial + QEMU diagnostics.
$root = "C:\Users\Administrator\CodeBuddy\BerryOS"
$out = "$root\build\qemu.out"
$err = "$root\build\qemu.err"
$log = "$root\build\serial.log"

if (Test-Path $log) { Remove-Item $log }
if (Test-Path $out) { Remove-Item $out }
if (Test-Path $err) { Remove-Item $err }

# sanity: does the binary run at all?
& "C:\Program Files\qemu\qemu-system-x86_64.exe" -version 2>&1 | Select-Object -First 2

$q = Start-Process -FilePath "C:\Program Files\qemu\qemu-system-x86_64.exe" `
    -WorkingDirectory $root `
    -ArgumentList "-cpu","max",`
        "-drive","file=$root\build\disk.img,format=raw,if=ide",`
        "-serial","file:$log",`
        "-vga","std","-display","none","-no-reboot","-no-shutdown" `
    -RedirectStandardOutput $out -RedirectStandardError $err -PassThru

Start-Sleep -Seconds 6
if (-not $q.HasExited) { Stop-Process -Id $q.Id -Force }

Write-Host "=== QEMU stdout ==="; if (Test-Path $out) { Get-Content $out }
Write-Host "=== QEMU stderr ==="; if (Test-Path $err) { Get-Content $err }
Write-Host "=== serial.log exists: $(Test-Path $log) ==="
if (Test-Path $log) { Get-Content $log }
