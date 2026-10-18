#requires -Version 5.1
<#
.SYNOPSIS
    Reproducible build / upload for AutoMeasureSort
    (Arduino Mega 2560 + arduino:avr 1.8.8 + AccelStepper 1.64).

.DESCRIPTION
    Arduino requires the sketch folder name to equal the .ino file name, so the two
    source files are staged into _build/AutoMeasureSort/ (gitignored) before compiling.
    Build artifacts land in _build/out/ (gitignored).

    NOTE: this file is intentionally ASCII-only. Windows PowerShell 5.1 reads BOM-less
    .ps1 files as ANSI; non-ASCII text here would break parsing.

.PARAMETER Upload
    Upload after a successful build (requires -Port).

.PARAMETER Port
    Serial port, e.g. COM5.

.PARAMETER Warnings
    none | default | more | all  (default: more = -Wall).
    Note: all (= -Wextra) also surfaces 5 upstream warnings from Arduino core new.cpp.

.EXAMPLE
    .\10_主控_Arduino\compile.ps1
    .\10_主控_Arduino\compile.ps1 -Upload -Port COM5
#>
param(
    [switch]$Upload,
    [string]$Port,
    [ValidateSet('none', 'default', 'more', 'all')]
    [string]$Warnings = 'more'
)

$ErrorActionPreference = 'Stop'

$src  = $PSScriptRoot
$repo = Split-Path -Parent $src
if (-not $repo) { throw 'Cannot locate repository root' }

$stage  = Join-Path $repo '_build\AutoMeasureSort'
$outDir = Join-Path $repo '_build\out'

# --- locate arduino-cli: PATH first, then default install location ---
$cli = $null
$cmd = Get-Command arduino-cli -ErrorAction SilentlyContinue
if ($cmd) { $cli = $cmd.Source }
if (-not $cli) {
    $cand = Join-Path $env:LOCALAPPDATA 'Programs\arduino-cli\arduino-cli.exe'
    if (Test-Path -LiteralPath $cand) { $cli = $cand }
}
if (-not $cli) {
    throw 'arduino-cli not found: add it to PATH or install to %LOCALAPPDATA%\Programs\arduino-cli\'
}

# --- reuse the core and user libraries already installed on this machine ---
$env:ARDUINO_DIRECTORIES_DATA = Join-Path $env:LOCALAPPDATA 'Arduino15'
$env:ARDUINO_DIRECTORIES_USER = Join-Path $env:USERPROFILE 'Documents\Arduino'

# --- stage sketch (hard requirement of the Arduino toolchain) ---
New-Item -ItemType Directory -Force -Path $stage, $outDir | Out-Null
Copy-Item -LiteralPath (Join-Path $src 'AutoMeasureSort.ino') -Destination (Join-Path $stage 'AutoMeasureSort.ino') -Force
Copy-Item -LiteralPath (Join-Path $src 'diaglog.h') -Destination (Join-Path $stage 'diaglog.h') -Force

$fqbn = 'arduino:avr:mega'
Write-Host "== compile $fqbn warnings=$Warnings" -ForegroundColor Cyan
& $cli compile --fqbn $fqbn --warnings $Warnings --output-dir $outDir $stage
if ($LASTEXITCODE -ne 0) { throw "compile failed exit=$LASTEXITCODE" }

if ($Upload) {
    if (-not $Port) { throw 'need -Port COMx to upload' }
    Write-Host "== upload $Port" -ForegroundColor Cyan
    & $cli upload -p $Port --fqbn $fqbn --input-dir $outDir
    if ($LASTEXITCODE -ne 0) { throw "upload failed exit=$LASTEXITCODE" }
}

Write-Host '== artifacts in _build\out\' -ForegroundColor Green
Get-ChildItem -LiteralPath $outDir -File | Select-Object Name, Length | Format-Table -AutoSize
