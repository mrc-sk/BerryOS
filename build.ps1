# SPDX-License-Identifier: AGPL-3.0-or-later
#
# Copyright (C) mrc-sk and imjumping
#
# This program is free software: you can redistribute it and/or modify it under
# the terms of the GNU Affero General Public License as published by the Free
# Software Foundation, either version 3 of the License, or (at your option)
# any later version. See LICENSE for the full license text and the additional
# non-commercial restriction terms that apply to this software.
# BerryOS build script (no make required).
# Builds x86_64 bootloader + minimal kernel, packs a disk image, optionally runs QEMU.
#
# Usage (PowerShell):
#   .\build.ps1            # build disk.img only
#   .\build.ps1 -Run       # build + launch QEMU
#   .\build.ps1 -Clean     # remove build/ then build
#   .\build.ps1 -Vmdk      # additionally convert disk.img -> berryos.vmdk (VMware)
#
# Tool resolution: uses the tools found on PATH; if not found, tries common install
# locations (LLVM, GnuWin32, QEMU). Override with env vars:
#   $env:LLVM_BIN, $env:QEMU_DIR, $env:NASM_BIN

param(
    [switch]$Run,
    [switch]$Clean,
    [switch]$Vmdk
)

$ErrorActionPreference = "Stop"

# ---- locate tools ----
function Find-Tool {
    param([string]$Name, [string[]]$ExtraDirs)
    $dirs = @($ExtraDirs)
    if ($env:LLVM_BIN) { $dirs += $env:LLVM_BIN }
    if ($env:NASM_BIN) { $dirs += $env:NASM_BIN }
    if ($env:QEMU_DIR) { $dirs += $env:QEMU_DIR }
    $dirs += @(
        "$env:ProgramFiles\LLVM\bin",
        "${env:ProgramFiles(x86)}\LLVM\bin",
        "$env:ProgramFiles\NASM",
        "${env:ProgramFiles(x86)}\NASM",
        "$env:ProgramFiles\qemu",
        "${env:ProgramFiles(x86)}\qemu",
        "$env:ProgramFiles\GnuWin32\bin"
    )
    foreach ($d in $dirs) {
        if ($d -and (Test-Path $d)) {
            $p = Join-Path $d $Name
            if (Test-Path $p) { return $p }
        }
    }
    # fall back to PATH
    $onPath = Get-Command $Name -ErrorAction SilentlyContinue
    if ($onPath) { return $onPath.Source }
    return $null
}

$CLANG   = Find-Tool "clang.exe"      @()
$LDLD    = Find-Tool "ld.lld.exe"     @()
$OBJCOPY = Find-Tool "llvm-objcopy.exe" @()
$PYTHON  = Find-Tool "python.exe"     @()
$QEMU    = Find-Tool "qemu-system-x86_64.exe" @()
$QEMUIMG = Find-Tool "qemu-img.exe" @()
$NASM    = Find-Tool "nasm.exe"       @()

# nasm not strictly needed for M0 (LLVM assembles boot.S), but resolve if present
$missing = @()
if (-not $CLANG)   { $missing += "clang" }
if (-not $LDLD)    { $missing += "ld.lld" }
if (-not $OBJCOPY) { $missing += "llvm-objcopy" }
if (-not $PYTHON)  { $missing += "python" }
if (-not $QEMU -and $Run) { $missing += "qemu-system-x86_64" }
if (-not $QEMUIMG -and $Vmdk) { $missing += "qemu-img" }
if ($missing.Count) {
    Write-Error "Missing tools: $($missing -join ', '). Run: python tools/setup_toolchain.py"
    exit 1
}

Write-Host "Using:" -ForegroundColor Cyan
Write-Host "  clang   = $CLANG"
Write-Host "  ld.lld  = $LDLD"
Write-Host "  objcopy = $OBJCOPY"
Write-Host "  python  = $PYTHON"
if ($Run) { Write-Host "  qemu    = $QEMU" }

# ---- paths ----
$Root      = $PSScriptRoot
$Build     = Join-Path $Root "build"
$KernDir   = Join-Path $Root "src/kernel"
$BootDir   = Join-Path $Root "src/boot/x86_64"
$IncDir    = Join-Path $KernDir "include"

if ($Clean -and (Test-Path $Build)) { Remove-Item $Build -Recurse -Force }
New-Item -ItemType Directory -Force -Path $Build | Out-Null

$TARGET   = "x86_64-none-elf"
$KCFLAGS  = "-target $TARGET -ffreestanding -mno-red-zone -mcmodel=kernel -nostdlib -nostdinc -Wall -Wextra -O2 -I$IncDir -I$KernDir\drivers -mno-sse -mno-sse2 -mno-avx -mno-mmx"
$KASFLAGS = "-target $TARGET -c"
$BASFLAGS = "-target $TARGET -c"

function Obj { param([string]$rel); Join-Path $Build $rel }

# ---- user programs (M2: init + worker, C + minimal libc) ----
$UserDir = Join-Path $Root "user"
$UserLib = Join-Path $UserDir "lib"
$UCFLAGS = "-target $TARGET -ffreestanding -nostdlib -mno-sse -mno-sse2 -mno-avx -mno-mmx -Wall -Wextra -O2 -I$UserLib"
$userObjDir = Join-Path $Build "user"
New-Item -ItemType Directory -Force -Path $userObjDir | Out-Null

# libc objects (shared by both programs)
$libSources = @(
    (Join-Path $UserLib "syscall.c"),
    (Join-Path $UserLib "string.c"),
    (Join-Path $UserDir "crt0.S")
)
$libObjs = @()
foreach ($s in $libSources) {
    $base = [System.IO.Path]::GetFileNameWithoutExtension($s)
    $o = Join-Path $userObjDir ($base + ".o")
    $libObjs += $o
    Write-Host "CC  $s" -ForegroundColor Gray
    if ($s.EndsWith(".S")) {
        & $CLANG -target $TARGET -c $s -o $o
    } else {
        & $CLANG $UCFLAGS.Split(' ') -c $s -o $o
    }
    if ($LASTEXITCODE -ne 0) { Write-Error "user build failed: $s"; exit 1 }
}

function Build-User {
    param([string]$src, [string]$outElf)
    $o = Join-Path $userObjDir ([System.IO.Path]::GetFileNameWithoutExtension($src) + ".o")
    Write-Host "CC  $src" -ForegroundColor Gray
    & $CLANG $UCFLAGS.Split(' ') -c $src -o $o
    if ($LASTEXITCODE -ne 0) { Write-Error "user build failed: $src"; exit 1 }
    Write-Host "LD  $outElf" -ForegroundColor Gray
    & $LDLD -T (Join-Path $UserDir "user.ld") -o $outElf $o $libObjs
    if ($LASTEXITCODE -ne 0) { Write-Error "user link failed"; exit 1 }
}

$initElf   = Join-Path $Build "init.elf"
$workerElf = Join-Path $Build "worker.elf"
Build-User (Join-Path $UserDir "init.c")   $initElf
Build-User (Join-Path $UserDir "worker.c") $workerElf

$userC = Join-Path $KernDir "user/user_elf.c"
Write-Host "EMBED $initElf + $workerElf -> $userC" -ForegroundColor Gray
& $PYTHON (Join-Path $Root "tools/embed_elf.py") user_elf_init $initElf user_elf_worker $workerElf $userC
if ($LASTEXITCODE -ne 0) { Write-Error "embed failed"; exit 1 }

# ---- kernel C objects ----
$kCFiles = @(
    (Join-Path $KernDir "main.c"),
    (Join-Path $KernDir "mm/pmm.c"),
    (Join-Path $KernDir "mm/slab.c"),
    (Join-Path $KernDir "mm/paging.c"),
    (Join-Path $KernDir "mm/elf.c"),
    (Join-Path $KernDir "sched/sched.c"),
    (Join-Path $KernDir "syscall/syscall.c"),
    (Join-Path $KernDir "user/user_main.c"),
    (Join-Path $KernDir "user/user_elf.c"),
    (Join-Path $KernDir "arch/x86_64/serial.c"),
    (Join-Path $KernDir "arch/x86_64/vga.c"),
    (Join-Path $KernDir "arch/x86_64/gdt.c"),
    (Join-Path $KernDir "arch/x86_64/idt.c"),
    (Join-Path $KernDir "arch/x86_64/isr.c"),
    (Join-Path $KernDir "arch/x86_64/pic.c"),
    (Join-Path $KernDir "drivers/keyboard.c"),
    (Join-Path $KernDir "drivers/ata.c"),
    (Join-Path $KernDir "drivers/fb.c"),
    (Join-Path $KernDir "drivers/dev.c"),
    (Join-Path $KernDir "fs/berryfs.c"),
    (Join-Path $KernDir "drivers/fbcon.c"),
    (Join-Path $KernDir "drivers/font8x8.c"),
    (Join-Path $KernDir "arch/x86_64/timer.c")
)
$kObjs = @()
foreach ($c in $kCFiles) {
    $o = (Obj "kernel") + "_" + [System.IO.Path]::GetFileNameWithoutExtension($c) + ".o"
    $kObjs += $o
    Write-Host "CC  $c" -ForegroundColor Gray
    & $CLANG $KCFLAGS.Split(' ') -c $c -o $o
    if ($LASTEXITCODE -ne 0) { Write-Error "compile failed: $c"; exit 1 }
}

# ---- kernel assembly (entry + interrupt stubs) ----
$startS = Join-Path $KernDir "arch/x86_64/start.S"
$startO = Obj "start.o"
Write-Host "AS  $startS" -ForegroundColor Gray
& $CLANG $KASFLAGS.Split(' ') $startS -o $startO
if ($LASTEXITCODE -ne 0) { Write-Error "asm failed: $startS"; exit 1 }

$intS = Join-Path $KernDir "arch/x86_64/interrupt.S"
$intO = Obj "interrupt.o"
Write-Host "AS  $intS" -ForegroundColor Gray
& $CLANG $KASFLAGS.Split(' ') $intS -o $intO
if ($LASTEXITCODE -ne 0) { Write-Error "asm failed: $intS"; exit 1 }

$swS = Join-Path $KernDir "arch/x86_64/switch.S"
$swO = Obj "switch.o"
Write-Host "AS  $swS" -ForegroundColor Gray
& $CLANG $KASFLAGS.Split(' ') $swS -o $swO
if ($LASTEXITCODE -ne 0) { Write-Error "asm failed: $swS"; exit 1 }

$kAsmObjs = @($startO, $intO, $swO)

# ---- kernel link + strip ----
$kernElf = Obj "kernel.elf"
$kernBin = Obj "kernel.bin"
Write-Host "LD  $kernElf" -ForegroundColor Gray
& $LDLD -T (Join-Path $KernDir "arch/x86_64/kernel.ld") -o $kernElf $kAsmObjs $kObjs
if ($LASTEXITCODE -ne 0) { Write-Error "link failed"; exit 1 }
Write-Host "OBJCOPY $kernBin" -ForegroundColor Gray
& $OBJCOPY -O binary $kernElf $kernBin

# ---- bootloader ----
$bootS  = Join-Path $BootDir "boot.S"
$bootO  = Obj "boot.o"
$bootElf = Obj "boot.elf"
$bootBin = Obj "boot.bin"
Write-Host "AS  $bootS" -ForegroundColor Gray
& $CLANG $BASFLAGS.Split(' ') $bootS -o $bootO
if ($LASTEXITCODE -ne 0) { Write-Error "asm failed: $bootS"; exit 1 }
Write-Host "LD  $bootElf" -ForegroundColor Gray
& $LDLD -T (Join-Path $BootDir "boot.ld") -o $bootElf $bootO
if ($LASTEXITCODE -ne 0) { Write-Error "link failed"; exit 1 }
Write-Host "OBJCOPY $bootBin" -ForegroundColor Gray
& $OBJCOPY -O binary $bootElf $bootBin

# ---- disk image ----
$disk = Obj "disk.img"
Write-Host "PACK $disk" -ForegroundColor Gray
& $PYTHON (Join-Path $Root "tools/mkimage.py") $bootBin $kernBin $disk
if ($LASTEXITCODE -ne 0) { Write-Error "pack failed"; exit 1 }

Write-Host "Build OK -> $disk" -ForegroundColor Green

# ---- optional VMDK (VMware) ----
if ($Vmdk) {
    $vmdkPath = Obj "berryos.vmdk"
    Write-Host "VMDK $vmdkPath" -ForegroundColor Gray
    & $QEMUIMG convert -f raw -O vmdk $disk $vmdkPath
    if ($LASTEXITCODE -ne 0) { Write-Error "vmdk convert failed"; exit 1 }
    Write-Host "VMware image OK -> $vmdkPath" -ForegroundColor Green
}

# ---- run ----
if ($Run) {
    Write-Host "Launching QEMU..." -ForegroundColor Cyan
    $qargs = @("-cpu", "max",
               "-drive", "file=$disk,format=raw,if=ide",
               "-serial", "stdio",
               "-vga", "std",
               "-display", "gtk",
               "-no-reboot",
               "-no-shutdown")
    $p = Start-Process -FilePath $QEMU -ArgumentList $qargs -NoNewWindow -PassThru
    $p.WaitForExit()
}
