#!/usr/bin/env pwsh
#Requires -Version 5.1
# Custom packaging for Spec Ops: The Line Head Tracking (C++ project, no .csproj).
# Produces two ZIPs:
#   - SpecOpsTheLineHeadTracking-v{version}-installer.zip (GitHub Release)
#   - SpecOpsTheLineHeadTracking-v{version}-nexus.zip     (Nexus, extract to game folder)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$ProgressPreference    = 'SilentlyContinue'

$scriptDir  = Split-Path -Parent $MyInvocation.MyCommand.Path
$projectDir = Split-Path -Parent $scriptDir

Import-Module (Join-Path $projectDir 'cameraunlock-core/powershell/ReleaseWorkflow.psm1') -Force

$cmakeLists = Get-Content (Join-Path $projectDir 'CMakeLists.txt') -Raw
if ($cmakeLists -notmatch 'project\(SpecOpsTheLineHeadTracking VERSION (\d+\.\d+\.\d+)') {
    throw "Could not parse version from CMakeLists.txt"
}
$version = $Matches[1]
$modName = 'SpecOpsTheLineHeadTracking'

Write-Host ""
Write-Host "=== Packaging $modName v$version ===" -ForegroundColor Magenta
Write-Host ""

$releaseDir = Join-Path $projectDir 'release'
if (-not (Test-Path $releaseDir)) { New-Item -ItemType Directory -Path $releaseDir -Force | Out-Null }

$asiPath = Join-Path $projectDir "bin/Release/$modName.asi"
if (-not (Test-Path $asiPath)) {
    throw "$modName.asi not found at: $asiPath. Run 'pixi run build-release' first."
}

$vendorAsiDir = Join-Path $projectDir 'vendor/ultimate-asi-loader'
$vendorAsiDll = Join-Path $vendorAsiDir 'dinput8.dll'
if (-not (Test-Path $vendorAsiDll)) {
    throw "Bundled ASI loader missing: $vendorAsiDll. Run 'pixi run update-deps' first."
}

# The installer ZIP redistributes that binary, and the upstream x86 loader
# carries binkw32.dll (RAD Game Tools, proprietary), wndmode.dll and
# vorbisfile.dll as RCDATA resources. None of the three is ours to ship, so a
# loader that still has them never reaches a release. See
# vendor/ultimate-asi-loader/README.md.
& (Join-Path $scriptDir 'strip-loader-payload.ps1') -Path $vendorAsiDll -VerifyOnly

# THIRD-PARTY-NOTICES.md is the attribution that travels with the redistributed
# dinput8.dll, so it has to name the binary this ZIP actually carries. update-deps.ps1
# rewrites vendor/ultimate-asi-loader/README.md and nothing else, so a loader bump
# leaves the notice quoting the previous tag and hash. Compare the recorded SHA-256
# against the file: that is the field a reader would verify with, and it moves on every
# bump.
$noticesPath = Join-Path $projectDir 'THIRD-PARTY-NOTICES.md'
if (-not (Test-Path $noticesPath)) {
    throw "THIRD-PARTY-NOTICES.md not found at: $noticesPath"
}
$noticesText = Get-Content $noticesPath -Raw
if ($noticesText -notmatch '(?m)^- \*\*dinput8\.dll SHA-256:\*\* `([0-9a-f]{64})`') {
    throw "THIRD-PARTY-NOTICES.md records no dinput8.dll SHA-256. The Ultimate ASI Loader notice must identify the binary being redistributed."
}
$notedLoaderSha = $Matches[1]
$actualLoaderSha = (Get-FileHash -Path $vendorAsiDll -Algorithm SHA256).Hash.ToLower()
if ($notedLoaderSha -ne $actualLoaderSha) {
    throw @"
THIRD-PARTY-NOTICES.md describes a different Ultimate ASI Loader than the one being packaged.
  notice: $notedLoaderSha
  vendor: $actualLoaderSha
Update the Ultimate ASI Loader version, commit and SHA-256 in THIRD-PARTY-NOTICES.md from
vendor/ultimate-asi-loader/README.md, which update-deps.ps1 rewrote with the real values.
"@
}

$scriptsDir = Join-Path $projectDir 'scripts'
foreach ($s in @('install.cmd', 'uninstall.cmd')) {
    if (-not (Test-Path (Join-Path $scriptsDir $s))) {
        throw "Required script not found: $s"
    }
}

$modManifestPath = Join-Path $projectDir 'launcher-manifest.json'
if (-not (Test-Path $modManifestPath)) {
    throw "launcher-manifest.json not found at: $modManifestPath"
}

# --- Installer ZIP -----------------------------------------------------
Write-Host '--- Installer ZIP ---' -ForegroundColor Yellow

$ghStaging = Join-Path $releaseDir 'staging-installer'
if (Test-Path $ghStaging) { Remove-Item -Recurse -Force $ghStaging }
New-Item -ItemType Directory -Path $ghStaging -Force | Out-Null

foreach ($s in @('install.cmd', 'uninstall.cmd')) {
    Copy-Item (Join-Path $scriptsDir $s) -Destination $ghStaging -Force
}

# install.cmd / uninstall.cmd resolve the game via shared/find-game.ps1.
# Bundle that shim alongside them so the release ZIP is self-contained.
Copy-SharedBundle -StagingDir $ghStaging

# Copy-SharedBundle ships the whole fleet's shared tree: every framework's install
# body, and the canonical games.json listing every title CameraUnlock targets. Both
# go out to anyone who downloads this mod. Reduce the bundle to what this ZIP's own
# scripts read, so the download describes this mod and nothing else.
$sharedDir = Join-Path $ghStaging 'shared'
$wrapperText = (Get-Content (Join-Path $ghStaging 'install.cmd') -Raw) +
               (Get-Content (Join-Path $ghStaging 'uninstall.cmd') -Raw)
foreach ($body in (Get-ChildItem $sharedDir -Filter '*-body*.cmd' -ErrorAction SilentlyContinue)) {
    # This mod's install.cmd / uninstall.cmd are self-contained. A future port to the
    # thin-wrapper shape sources its body by name, so keep whichever are referenced.
    if ($wrapperText -notmatch [regex]::Escape($body.Name)) {
        Remove-Item $body.FullName -Force
    }
}

# find-game.ps1 looks up exactly one id. Keep that entry; the loader's only
# structural requirement is a non-empty top-level `games` object.
$gamesPath = Join-Path $sharedDir 'games.json'
$games = Get-Content $gamesPath -Raw -Encoding UTF8 | ConvertFrom-Json
$gameId = 'spec-ops-the-line'
if (-not $games.games.PSObject.Properties.Name.Contains($gameId)) {
    throw "games.json has no '$gameId' entry, so the installer could not resolve the game. Add it in cameraunlock-core/data/games.json."
}
$games.games = [PSCustomObject]@{ $gameId = $games.games.$gameId }
# $comment is guidance for people editing the canonical file and names other repos.
# Nothing at install time reads it.
$games.PSObject.Properties.Remove('$comment')
[System.IO.File]::WriteAllText($gamesPath, ($games | ConvertTo-Json -Depth 10),
                               (New-Object System.Text.UTF8Encoding $false))
Write-Host "  shared/ trimmed to $gameId" -ForegroundColor Green

$pluginsDir = Join-Path $ghStaging 'plugins'
New-Item -ItemType Directory -Path $pluginsDir -Force | Out-Null
Copy-Item $asiPath -Destination $pluginsDir -Force

$ghVendorDir = Join-Path $ghStaging 'vendor/ultimate-asi-loader'
New-Item -ItemType Directory -Path $ghVendorDir -Force | Out-Null
# Ultimate ASI Loader is MIT: its notice has to travel with the dinput8.dll
# this ZIP installs, so a missing vendor LICENSE fails the package.
foreach ($vendorFile in @('dinput8.dll', 'LICENSE', 'README.md')) {
    $src = Join-Path $vendorAsiDir $vendorFile
    if (-not (Test-Path $src)) {
        throw "Required vendored file not found: vendor/ultimate-asi-loader/$vendorFile. The loader's licence must ship beside its binary."
    }
    Copy-Item $src -Destination $ghVendorDir -Force
}

# Same rule as the Nexus ZIP below: this is a binary distribution, so our own
# MIT LICENSE and the third-party notices ship at its root.
foreach ($doc in @('README.md', 'LICENSE', 'CHANGELOG.md', 'THIRD-PARTY-NOTICES.md')) {
    $p = Join-Path $projectDir $doc
    if (-not (Test-Path $p)) {
        throw "Required notice file not found: $doc. Every published ZIP is a binary distribution and must carry it."
    }
    Copy-Item -Path $p -Destination $ghStaging -Force
}

# Canonical launcher manifest. The launcher reads launcher-manifest.json from
# the ZIP root to ingest the package metadata (files, loader, dependencies).
# Stamp the version from the build so the shipped manifest can never disagree
# with the built .asi. The only "version": "X.Y.Z" string is mod_info.version;
# schema_version is a bare number and is left untouched by the regex.
$stagedManifest = Join-Path $ghStaging 'launcher-manifest.json'
$manifestText = Get-Content $modManifestPath -Raw
$manifestText = $manifestText -replace '("version":\s*")\d+\.\d+\.\d+(")', "`${1}$version`$2"
[System.IO.File]::WriteAllText($stagedManifest, $manifestText, (New-Object System.Text.UTF8Encoding $false))
Write-Host "  launcher-manifest.json (version $version)" -ForegroundColor Green

$installerZip = Join-Path $releaseDir "$modName-v$version-installer.zip"
if (Test-Path $installerZip) { Remove-Item $installerZip -Force }
Push-Location $ghStaging
try { Compress-Archive -Path '.\*' -DestinationPath $installerZip -Force } finally { Pop-Location }
Remove-Item -Recurse -Force $ghStaging

$installerKb = [math]::Round((Get-Item $installerZip).Length / 1KB, 1)
Write-Host ("  $installerZip ({0:N1} KB)" -f $installerKb) -ForegroundColor Green

# --- Nexus ZIP ---------------------------------------------------------
Write-Host ''
Write-Host '--- Nexus ZIP ---' -ForegroundColor Yellow

$nexusStaging = Join-Path $releaseDir 'staging-nexus'
if (Test-Path $nexusStaging) { Remove-Item -Recurse -Force $nexusStaging }

# Nexus users manage their own ASI loader, so the nexus ZIP ships only the
# mod's .asi - never the vendored dinput8.dll.
$nexusGameDir = Join-Path $nexusStaging 'Binaries\Win32'
New-Item -ItemType Directory -Path $nexusGameDir -Force | Out-Null

Copy-Item $asiPath -Destination $nexusGameDir -Force

$nexusZip = Join-Path $releaseDir "$modName-v$version-nexus.zip"
if (Test-Path $nexusZip) { Remove-Item $nexusZip -Force }
# The Nexus ZIP is a binary distribution too: the licences of everything
# compiled into or bundled with the payload require their notices to travel
# with it, so LICENSE and THIRD-PARTY-NOTICES.md ship at its root.
foreach ($noticeDoc in @('LICENSE', 'THIRD-PARTY-NOTICES.md', 'README.md')) {
    $noticeSrc = Join-Path $projectDir $noticeDoc
    if (-not (Test-Path $noticeSrc)) {
        throw "Required notice file not found: $noticeDoc. Every published ZIP is a binary distribution and must carry it."
    }
    Copy-Item $noticeSrc -Destination $nexusStaging -Force
    Write-Host "  $noticeDoc" -ForegroundColor Green
}
Push-Location $nexusStaging
try { Compress-Archive -Path '.\*' -DestinationPath $nexusZip -Force } finally { Pop-Location }
Remove-Item -Recurse -Force $nexusStaging

$nexusKb = [math]::Round((Get-Item $nexusZip).Length / 1KB, 1)
Write-Host ("  $nexusZip ({0:N1} KB)" -f $nexusKb) -ForegroundColor Green

Write-Host ''
Write-Host '=== Package Complete ===' -ForegroundColor Magenta

Write-Output $installerZip
Write-Output $nexusZip
