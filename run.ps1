# BerryOS -- build + run helper (x86_64 on QEMU).
#
#   .\run.ps1                 # build, make ISO, open a QEMU window (interactive)
#   .\run.ps1 -Headless       # build, make ISO, run with no window
#   .\run.ps1 -NoBuild        # skip rebuilding, just launch the existing ISO
#   .\run.ps1 -SkipRun        # build + make ISO only, do not launch QEMU
#
# NOTE: launch this via   powershell.exe -File .\run.ps1
# (dot-sourcing ".\run.ps1" directly does not execute in some sandboxed hosts).

param(
    [switch]$NoBuild,
    [switch]$Headless,
    [switch]$SkipRun
)

$Root  = Split-Path -Parent $MyInvocation.MyCommand.Path
$Build = Join-Path $Root "build"
$Iso   = Join-Path $Build "berryos.iso"

function Find-Tool($name, $dirs) {
    foreach ($d in $dirs) {
        $p = Join-Path $d $name
        if (Test-Path $p) { return $p }
    }
    $cmd = Get-Command $name -ErrorAction SilentlyContinue
    if ($cmd) { return $cmd.Source }
    return $null
}

$PYTHON = Find-Tool "python.exe" @("C:\Program Files\Python314", "C:\Program Files\PyManager")
$QEMU   = Find-Tool "qemu-system-x86_64.exe" @("$env:ProgramFiles\qemu", "${env:ProgramFiles(x86)}\qemu")

if (-not $PYTHON) { Write-Host "ERROR: python.exe not found"; exit 1 }
if (-not $QEMU)   { Write-Host "ERROR: qemu-system-x86_64.exe not found"; exit 1 }

Write-Host "python = $PYTHON"
Write-Host "qemu   = $QEMU"

# --- 1. build the kernel -----------------------------------------------------
if (-not $NoBuild) {
    Write-Host ""
    Write-Host "== building kernel =="
    & powershell.exe -NoProfile -ExecutionPolicy Bypass -File (Join-Path $Root "build.ps1")
    if ($LASTEXITCODE -ne 0) { Write-Host "ERROR: kernel build failed"; exit 1 }
}

# --- 2. pack the El Torito no-emulation ISO ----------------------------------
Write-Host ""
Write-Host "== building El Torito ISO =="
& $PYTHON (Join-Path $Root "tools\make_iso.py")
if ($LASTEXITCODE -ne 0) { Write-Host "ERROR: ISO build failed"; exit 1 }
if (-not (Test-Path $Iso)) { Write-Host "ERROR: $Iso missing"; exit 1 }
Write-Host "ISO -> $Iso"

if ($SkipRun) { Write-Host "== done (SkipRun) =="; exit 0 }

# --- 3. launch QEMU ----------------------------------------------------------
# The kernel prints to the debug-console port 0xE9, NOT to COM1 (0x3F8);
# bootloader markers are the only thing on the real serial port.
$klog = Join-Path $Build "kernel_run.log"
$slog = Join-Path $Build "serial_run.txt"

$qemuArgs = @(
    "-cpu", "max",
    "-m", "256",
    "-drive", "file=$Iso,format=raw,if=ide,media=cdrom",
    "-boot", "d",
    "-vga", "std",
    "-debugcon", "file:$klog",
    "-serial", "file:$slog",
    "-no-reboot"
)
if ($Headless) { $qemuArgs += @("-display", "none") }

Write-Host ""
Write-Host "== launching QEMU =="
if ($Headless) {
    Write-Host "headless: no window. Screenshot with: python build\grab_iso.py"
} else {
    Write-Host "a QEMU window will open -- click inside it to move the mouse,"
    Write-Host "then click the blue 'Click +1' button to see the counter change."
    Write-Host "Close the window to exit."
}

& $QEMU @qemuArgs

Write-Host ""
Write-Host "== kernel log (last 25 lines) =="
if (Test-Path $klog) {
    Get-Content $klog | Select-Object -Last 25
} else {
    Write-Host "(no kernel log produced)"
}
