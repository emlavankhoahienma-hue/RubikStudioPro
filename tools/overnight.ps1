param(
    [int]$MaxRounds = 30,
    [int]$TimeoutMinutes = 360,
    [int]$RunsPerCube = 10
)

$ErrorActionPreference = "Stop"
$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$RootDir = Split-Path -Parent $ScriptDir
Set-Location $RootDir

$env:Path = [Environment]::GetEnvironmentVariable("Path", "User") + ";" + [Environment]::GetEnvironmentVariable("Path", "Machine") + ";C:\tools\llvm-mingw-20261020-ucrt-x86_64\bin"

if (-not (Test-Path "LOG")) { New-Item -ItemType Directory -Path "LOG" -Force | Out-Null }
if (-not (Test-Path "metrics")) { New-Item -ItemType Directory -Path "metrics" -Force | Out-Null }

Write-Host "================================================="
Write-Host "RUBIK STUDIO PRO - OVERNIGHT AUTONOMOUS HARNESS"
Write-Host "Max Rounds: $MaxRounds | Timeout: $TimeoutMinutes mins"
Write-Host "================================================="

# Compile Verify Engine
Write-Host "Compiling verify_engine..."
clang++ -std=c++20 -O3 -I./CoreCpp verify/verify_engine.cpp CoreCpp/Common/*.cpp CoreCpp/Cube2x2/*.cpp CoreCpp/Cube3x3/*.cpp CoreCpp/Cube4x4/*.cpp CoreCpp/Cube5x5/*.cpp -lpsapi -o verify_engine_runner.exe
if ($LASTEXITCODE -ne 0) {
    Write-Error "Failed to compile verify_engine_runner.exe"
    exit 1
}

$StartTime = Get-Date

function Run-VerificationRound([int]$roundNum, [uint32]$seed, [string]$label) {
    $Timestamp = (Get-Date).ToString("yyyyMMdd_HHmmss")
    $LogFile = "LOG/${label}_${roundNum}_${Timestamp}.log"
    $Commit = (git rev-parse --short HEAD)

    Write-Host "`n--- Running Round $roundNum ($label) | Seed: $seed | Commit: $Commit ---"
    .\verify_engine_runner.exe --seed $seed --runs $RunsPerCube | Tee-Object -FilePath $LogFile

    # Append to metrics/history.csv if output exists
    Write-Host "Saved raw log to $LogFile"
}

# Check for STOP.flag
if (Test-Path "STOP.flag") {
    Write-Host "STOP.flag detected. Aborting."
    exit 0
}

Write-Host "Harness ready."
