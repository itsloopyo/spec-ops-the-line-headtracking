#!/usr/bin/env pwsh
#Requires -Version 5.1
# Deploy the built SpecOpsTheLineHeadTracking.asi to the game's Binaries/Win32/
# directory for local testing.
#
# Usage: deploy.ps1 [Debug|Release] [GamePath]
# Defaults to Debug. An explicit GamePath wins over auto-detection
# (same contract as install.cmd).

param(
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Debug',
    [string]$GamePath
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$scriptDir  = Split-Path -Parent $MyInvocation.MyCommand.Path
$projectDir = Split-Path -Parent $scriptDir

$asi = Join-Path $projectDir "bin/$Configuration/SpecOpsTheLineHeadTracking.asi"
if (-not (Test-Path $asi)) {
    throw "Build output not found: $asi. Run 'pixi run build' or 'pixi run build-release' first."
}

if ($GamePath) {
    if (-not (Test-Path $GamePath)) {
        throw "Explicit game path does not exist: $GamePath"
    }
    $gamePath = $GamePath
} else {
    Import-Module (Join-Path $projectDir 'cameraunlock-core/powershell/GamePathDetection.psm1') -Force
    $gamePath = Find-GamePath -GameId 'spec-ops-the-line'
    if (-not $gamePath) {
        throw "Could not locate Spec Ops: The Line. Set SPEC_OPS_THE_LINE_PATH, install via Steam, or pass the game path: deploy.ps1 $Configuration <path>"
    }
}

$exeDir = Join-Path $gamePath 'Binaries\Win32'
if (-not (Test-Path $exeDir)) {
    throw "Expected exe directory not found: $exeDir"
}

Copy-Item $asi -Destination $exeDir -Force
Write-Host "Deployed: $asi -> $exeDir" -ForegroundColor Green
