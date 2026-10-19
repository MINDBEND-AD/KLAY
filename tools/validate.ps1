# SPDX-License-Identifier: AGPL-3.0-only
# LICENSE-GPL: KLAY own source. See THIRD_PARTY_LICENSES.md (C-006).
<#
.SYNOPSIS
    Plugin validation (NFR-011): pluginval at strictness 8+ and the Steinberg VST3 validator on the built plugin.

.DESCRIPTION
    pluginval (pinned release, SHA-256 checked) is downloaded to tools/bin/pluginval on the first run. The VST3
    validator is built from the pinned VST3 SDK by tools/vst3-validator into build/vst3-validator.
    Exits 0 only if both validators pass.

.PARAMETER Config
    Build configuration of the plugin to validate (default Release).

.PARAMETER Plugin
    Path to the .vst3 bundle. Defaults to build/plugin/KLAY_artefacts/<Config>/VST3/Klaymore.vst3.

.PARAMETER Strictness
    pluginval strictness level, 8 or higher (default 8).

.PARAMETER Generator
    CMake generator for building the VST3 validator (default 'Visual Studio 18 2026'; CI passes what it detected).

.PARAMETER SkipGuiTests
    Pass --skip-gui-tests to pluginval (for headless machines).
#>
param(
    [string] $Config = 'Release',
    [string] $Plugin = '',
    [ValidateRange(8, 10)] [int] $Strictness = 8,
    [switch] $SkipGuiTests,
    [string] $Generator = 'Visual Studio 18 2026'
)

$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot

# Pinned tool versions (plan decision 18).
$pluginvalVersion = 'v1.0.4'
$pluginvalUrl = "https://github.com/Tracktion/pluginval/releases/download/$pluginvalVersion/pluginval_Windows.zip"
$pluginvalSha256 = 'C08E61CE3B96DB41636F8EC7E76F4C7E2C13EBDAC7FA1B5A1F52B4F32EC715AB'

if (-not $Plugin) {
    $Plugin = Join-Path $repo "build/plugin/KLAY_artefacts/$Config/VST3/Klaymore.vst3"
}
if (-not (Test-Path -LiteralPath $Plugin)) {
    Write-Output "ERROR: plugin not found at $Plugin. Build it first: cmake --build --preset $($Config.ToLower())"
    exit 2
}
$Plugin = (Resolve-Path -LiteralPath $Plugin).Path

function Write-Section([string] $title) {
    Write-Output ''
    Write-Output ('=' * 78)
    Write-Output $title
    Write-Output ('=' * 78)
}

# --- pluginval ---------------------------------------------------------------------------------------------------
$pluginvalDir = Join-Path $repo "tools/bin/pluginval/$pluginvalVersion"
$pluginvalExe = Join-Path $pluginvalDir 'pluginval.exe'
if (-not (Test-Path -LiteralPath $pluginvalExe)) {
    New-Item -ItemType Directory -Force -Path $pluginvalDir | Out-Null
    $zip = Join-Path $pluginvalDir 'pluginval_Windows.zip'
    Write-Output "Downloading pluginval $pluginvalVersion ..."
    [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
    Invoke-WebRequest -UseBasicParsing -Uri $pluginvalUrl -OutFile $zip
    $hash = (Get-FileHash -LiteralPath $zip -Algorithm SHA256).Hash
    if ($hash -ne $pluginvalSha256) {
        Remove-Item -LiteralPath $zip -Force
        Write-Output "ERROR: pluginval download hash mismatch ($hash), expected $pluginvalSha256"
        exit 2
    }
    Expand-Archive -LiteralPath $zip -DestinationPath $pluginvalDir -Force
    Remove-Item -LiteralPath $zip -Force
}

Write-Section "pluginval $pluginvalVersion, strictness $Strictness : $Plugin"
$pluginvalArgs = @('--strictness-level', "$Strictness", '--timeout-ms', '600000')
if ($SkipGuiTests) { $pluginvalArgs += '--skip-gui-tests' }
$pluginvalArgs += @('--validate', $Plugin)

# Runs a native program with its stderr merged into the returned lines. Under Windows PowerShell 5.1 a redirected
# stderr line becomes an error record, which aborts the script while $ErrorActionPreference is 'Stop'; 'Continue'
# here lets a failing tool still reach the summary. Sets $LASTEXITCODE as usual.
function Invoke-Native([string] $exe, [string[]] $arguments) {
    $saved = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try { & $exe @arguments 2>&1 | ForEach-Object { "$_" } }
    finally { $ErrorActionPreference = $saved }
}

# pluginval is a GUI-subsystem executable; piping keeps PowerShell waiting for it and captures its output.
Invoke-Native $pluginvalExe $pluginvalArgs
$pluginvalExit = $LASTEXITCODE
Write-Output "pluginval exit code: $pluginvalExit"

# --- Steinberg VST3 validator ------------------------------------------------------------------------------------
# The SDK pin lives in tools/vst3-validator/CMakeLists.txt; a stamp file records the tag the validator was built
# from, so a changed pin rebuilds it.
$validatorSource = Join-Path $repo 'tools/vst3-validator'
$sdkTag = [regex]::Match((Get-Content -LiteralPath (Join-Path $validatorSource 'CMakeLists.txt') -Raw),
                         'GIT_TAG\s+(\S+)').Groups[1].Value
if (-not $sdkTag) { Write-Output 'ERROR: no GIT_TAG found in tools/vst3-validator/CMakeLists.txt'; exit 2 }
$validatorBuild = Join-Path $repo 'build/vst3-validator'
$validatorExe = Join-Path $validatorBuild 'bin/Release/validator.exe'
$validatorStamp = Join-Path $validatorBuild 'klay-vst3sdk-tag.txt'
$builtTag = if (Test-Path -LiteralPath $validatorStamp) { (Get-Content -LiteralPath $validatorStamp -Raw).Trim() } else { '' }
if (-not (Test-Path -LiteralPath $validatorExe) -or $builtTag -ne $sdkTag) {
    if ($builtTag -and $builtTag -ne $sdkTag) {
        Write-Output "VST3 SDK pin changed ($builtTag -> $sdkTag); rebuilding the validator from scratch ..."
        Remove-Item -LiteralPath $validatorBuild -Recurse -Force
    }
    else {
        Write-Output "Building the VST3 validator from the pinned VST3 SDK $sdkTag ..."
    }
    $buildLog = Invoke-Native 'cmake' @('-S', $validatorSource, '-B', $validatorBuild, '-G', $Generator, '-A', 'x64')
    if ($LASTEXITCODE -ne 0) { $buildLog; Write-Output 'ERROR: configuring the VST3 validator failed'; exit 2 }
    $buildLog = Invoke-Native 'cmake' @('--build', $validatorBuild, '--config', 'Release', '--target', 'validator', '--', '/m', '/v:minimal')
    if ($LASTEXITCODE -ne 0 -or -not (Test-Path -LiteralPath $validatorExe)) {
        $buildLog
        Write-Output 'ERROR: building the VST3 validator failed'
        exit 2
    }
    Set-Content -LiteralPath $validatorStamp -Value $sdkTag -Encoding ASCII
}

Write-Section "Steinberg VST3 validator (SDK $sdkTag) : $Plugin"
$validatorOutput = Invoke-Native $validatorExe @($Plugin)
$validatorExit = $LASTEXITCODE
$validatorOutput | Where-Object { $_ -match '^\[|ERROR|Result:' -and $_ -notmatch '^\[Succeeded\]$' }
$result = $validatorOutput | Where-Object { $_ -match 'Result: (\d+) tests passed, (\d+) tests failed' } | Select-Object -Last 1
$validatorPassed = ($validatorExit -eq 0) -and ($result -match ', 0 tests failed')
Write-Output "VST3 validator exit code: $validatorExit"

# --- Summary -----------------------------------------------------------------------------------------------------
Write-Section 'Summary'
$pluginvalStatus = if ($pluginvalExit -eq 0) { 'PASSED' } else { 'FAILED' }
$validatorStatus = if ($validatorPassed) { 'PASSED' } else { 'FAILED' }
Write-Output "pluginval (strictness $Strictness): $pluginvalStatus"
Write-Output "VST3 validator:              $validatorStatus  ($result)"

if ($pluginvalExit -ne 0 -or -not $validatorPassed) { exit 1 }
exit 0
