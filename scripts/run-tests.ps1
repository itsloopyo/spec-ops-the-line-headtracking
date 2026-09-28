[CmdletBinding()]
param(
    [string]$Config = 'Release',
    [ValidateSet('all', 'unit', 'differential')][string]$Suite = 'all'
)
$ErrorActionPreference = 'Stop'
$ProjectRoot = Resolve-Path (Join-Path $PSScriptRoot '..')
$BuildDir = Join-Path $ProjectRoot 'build'

if ($Suite -ne 'unit') {
    # The differential test is only as good as its claim about what it compiled. SHA256 through
    # .NET, because Get-FileHash is not found when a runner's pwsh runs this under Windows
    # PowerShell.
    $provenance = Join-Path $ProjectRoot 'tests/config_differential/provenance.txt'
    $sha256 = [System.Security.Cryptography.SHA256]::Create()
    foreach ($line in Get-Content $provenance) {
        if ($line -match '^\s*(#|$)') { continue }
        $hash, $path = ($line -split '\s+', 3)[0, 1]
        $bytes = [System.IO.File]::ReadAllBytes((Join-Path $ProjectRoot $path))
        $actual = -join ($sha256.ComputeHash($bytes) | ForEach-Object { $_.ToString('x2') })
        if ($actual -ne $hash) { throw "$path has changed: sha256 $actual, provenance.txt records $hash" }
    }
}

$labels = @{ all = @(); unit = @('-LE', 'differential'); differential = @('-L', 'differential') }[$Suite]
ctest --test-dir $BuildDir -C $Config --output-on-failure --no-tests=error @labels
if ($LASTEXITCODE -ne 0) { throw "Tests failed ($LASTEXITCODE)" }

Write-Host 'All tests passed' -ForegroundColor Green
