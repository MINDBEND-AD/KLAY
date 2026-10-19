# SPDX-License-Identifier: AGPL-3.0-only
# LICENSE-GPL: KLAY own source. See THIRD_PARTY_LICENSES.md (C-006).
<#
.SYNOPSIS
    Runs tools/check-licenses.ps1 with -Root given as an 8.3 short path (C-006, M0 review). Exits 77 (ctest skip)
    when the volume has no short name for the folder.
#>
param(
    [Parameter(Mandatory)] [string] $Fixture
)

$long = (Get-Item -LiteralPath $Fixture -Force).FullName
$short = (New-Object -ComObject Scripting.FileSystemObject).GetFolder($long).ShortPath
if ($short -eq $long) {
    Write-Output "SKIP: no 8.3 short name for $long (8.3 name generation off on this volume?)"
    exit 77
}

Write-Output "short root: $short"
& (Join-Path (Split-Path -Parent (Split-Path -Parent $PSScriptRoot)) 'tools/check-licenses.ps1') -Root $short
exit $LASTEXITCODE
