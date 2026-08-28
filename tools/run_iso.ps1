# SPDX-License-Identifier: AGPL-3.0-or-later
#
# Copyright (C) mrc-sk and imjumping
#
# This program is free software: you can redistribute it and/or modify it under
# the terms of the GNU Affero General Public License as published by the Free
# Software Foundation, either version 3 of the License, or (at your option)
# any later version. See LICENSE for the full license text and the additional
# non-commercial restriction terms that apply to this software.
# Headless QEMU run of the BerryOS ISO (El Torito HDD-emulation boot).
$root = "C:\Users\Administrator\CodeBuddy\BerryOS"
$iso  = "$root\build\berryos.iso"
$out  = "$root\build\qemu_iso.out"
$err  = "$root\build\qemu_iso.err"
$log  = "$root\build\serial_iso.log"

if (Test-Path $log) { Remove-Item $log }
if (Test-Path $out) { Remove-Item $out }
if (Test-Path $err) { Remove-Item $err }

& "C:\Program Files\qemu\qemu-system-x86_64.exe" -version 2>&1 | Select-Object -First 2

$q = Start-Process -FilePath "C:\Program Files\qemu\qemu-system-x86_64.exe" `
    -WorkingDirectory $root `
    -ArgumentList "-cpu","max",`
        "-cdrom","$iso",`
        "-serial","file:$log",`
        "-boot","order=d",`
        "-debugcon","file:$root\build\bios.log","-global","isa-debugcon.iobase=0x402",`
        "-vga","std","-display","none","-no-reboot","-no-shutdown" `
    -RedirectStandardOutput $out -RedirectStandardError $err -PassThru

Start-Sleep -Seconds 6
if (-not $q.HasExited) { Stop-Process -Id $q.Id -Force }

Write-Host "=== QEMU stdout ==="; if (Test-Path $out) { Get-Content $out }
Write-Host "=== QEMU stderr ==="; if (Test-Path $err) { Get-Content $err }
Write-Host "=== serial_iso.log exists: $(Test-Path $log) ==="
if (Test-Path $log) { Get-Content $log }
