# SPDX-License-Identifier: AGPL-3.0-only
# LICENSE-GPL: KLAY own source. See THIRD_PARTY_LICENSES.md (C-006).
<#
.SYNOPSIS
    C-006 licence labelling check. Exits 0 when the tree is clean, 1 on violations, 2 on usage errors.

.DESCRIPTION
    Rules (see THIRD_PARTY_LICENSES.md):
      1. Every code file (C/C++/ObjC sources and headers, .cmake .ps1 .psm1 .py .sh .bat .cmd .yml .yaml,
         CMakeLists.txt) has an SPDX-License-Identifier in its first 10 lines.
      2. Every file whose SPDX header names a GPL-family licence (GPL, LGPL, AGPL) contains the marker LICENSE-GPL.
      3. Every such file matches a glob in the Files column of a GPL-family row of THIRD_PARTY_LICENSES.md, and
         every GPL-family licence ID in its SPDX expression appears in the Licence cell of such a covering row.
      4. Every dependency declared with FetchContent_Declare(<name> ...) has a row whose Component equals <name>
         (case-insensitive).

.PARAMETER Root
    Tree to check. Defaults to the repository root (the parent of this script's folder).

.PARAMETER Exclude
    Repo-relative path prefixes to skip, in addition to build output and git metadata.
#>
param(
    [string] $Root = (Split-Path -Parent $PSScriptRoot),
    [string[]] $Exclude = @('tests/fixtures/licenses/')
)

$ErrorActionPreference = 'Stop'

if (-not (Test-Path -LiteralPath $Root -PathType Container)) {
    Write-Output "ERROR: root '$Root' does not exist"
    exit 2
}
# Get-Item expands 8.3 short names (Resolve-Path keeps them); Get-ChildItem returns long names, so the relative
# paths below are only right if the root is long too.
$Root = (Get-Item -LiteralPath $Root -Force).FullName.TrimEnd('\', '/')

$licenceFile = Join-Path $Root 'THIRD_PARTY_LICENSES.md'
if (-not (Test-Path -LiteralPath $licenceFile)) {
    Write-Output "ERROR: $licenceFile not found"
    exit 2
}

$gplPattern = '(?i)\b(A|L)?GPL'
$codeExtensions = @(
    '.h', '.hh', '.hpp', '.hxx', '.c', '.cc', '.cpp', '.cxx', '.inl', '.ipp', '.m', '.mm',
    '.cmake', '.ps1', '.psm1', '.py', '.sh', '.bat', '.cmd', '.yml', '.yaml')

# GPL-family SPDX licence IDs in an expression or a Licence cell, e.g. 'AGPL-3.0-only', 'GPL-2.0-or-later'.
function Get-GplIds([string] $text) {
    return @([regex]::Matches($text, '[A-Za-z0-9.+-]+') | ForEach-Object { $_.Value } | Where-Object { $_ -match $gplPattern })
}
$skipPrefixes = @('.git/', 'build/', 'out/', '.vs/', 'tools/bin/') + $Exclude

function ConvertTo-GlobRegex([string] $glob) {
    $sb = New-Object System.Text.StringBuilder '^'
    for ($i = 0; $i -lt $glob.Length; $i++) {
        $c = $glob[$i]
        if ($c -eq '*') {
            if ($i + 1 -lt $glob.Length -and $glob[$i + 1] -eq '*') { [void]$sb.Append('.*'); $i++ }
            else { [void]$sb.Append('[^/]*') }
        }
        elseif ($c -eq '?') { [void]$sb.Append('[^/]') }
        else { [void]$sb.Append([regex]::Escape([string]$c)) }
    }
    [void]$sb.Append('$')
    return $sb.ToString()
}

# --- Parse THIRD_PARTY_LICENSES.md -----------------------------------------------------------------------------
$components = @()
$gplRows = @()   # GPL-family rows: component, licence IDs and file globs (as regexes)
foreach ($line in Get-Content -LiteralPath $licenceFile -Encoding UTF8) {
    if ($line -notmatch '^\s*\|') { continue }
    $cells = $line.Trim().Trim('|').Split('|') | ForEach-Object { $_.Trim() }
    if ($cells.Count -lt 4) { continue }
    if ($cells[0] -eq 'Component' -or $cells[0] -match '^-+$') { continue }

    $components += $cells[0]
    if ($cells[1] -match $gplPattern) {
        $globs = @([regex]::Matches($cells[3], '`([^`]+)`') | ForEach-Object { ConvertTo-GlobRegex $_.Groups[1].Value })
        $gplRows += [pscustomobject]@{ Component = $cells[0]; Licence = $cells[1]; Ids = Get-GplIds $cells[1]; Globs = $globs }
    }
}

# --- Walk the tree -----------------------------------------------------------------------------------------------
$violations = New-Object System.Collections.Generic.List[string]
$checked = 0

$files = Get-ChildItem -LiteralPath $Root -Recurse -File -Force
foreach ($file in $files) {
    $rel = $file.FullName.Substring($Root.Length + 1).Replace('\', '/')
    $skip = $false
    foreach ($prefix in $skipPrefixes) { if ($rel.StartsWith($prefix, [StringComparison]::OrdinalIgnoreCase)) { $skip = $true; break } }
    if ($skip) { continue }

    $isCode = ($codeExtensions -contains $file.Extension.ToLowerInvariant()) -or ($file.Name -eq 'CMakeLists.txt')
    if (-not $isCode) { continue }
    $checked++

    $text = [IO.File]::ReadAllText($file.FullName)

    # Rule 4: fetched dependencies need a row.
    if ($file.Extension -eq '.cmake' -or $file.Name -eq 'CMakeLists.txt') {
        foreach ($m in [regex]::Matches($text, '(?i)FetchContent_Declare\s*\(\s*([A-Za-z0-9_.+-]+)')) {
            $name = $m.Groups[1].Value
            $hasRow = $false
            foreach ($c in $components) { if ([string]::Equals($c, $name, [StringComparison]::OrdinalIgnoreCase)) { $hasRow = $true; break } }
            if (-not $hasRow) {
                $violations.Add("${rel}: dependency '$name' (FetchContent_Declare) has no row in THIRD_PARTY_LICENSES.md")
            }
        }
    }

    $head =($text -split "`n" | Select-Object -First 10) -join "`n"
    $spdx = [regex]::Match($head, 'SPDX-License-Identifier:\s*([^\r\n*]+)')

    if (-not $spdx.Success) {
        $violations.Add("${rel}: no SPDX-License-Identifier in the first 10 lines")
        continue
    }

    $expression = $spdx.Groups[1].Value.Trim()
    if ($expression -notmatch $gplPattern) { continue }

    if (-not $text.Contains('LICENSE-GPL')) {
        $violations.Add("${rel}: GPL-family licence ($expression) but missing LICENSE-GPL marker")
    }

    # Rule 3: covered by a GPL-family row whose Licence cell names every GPL-family ID of the file's expression.
    $covering = @($gplRows | Where-Object { $row = $_; @($row.Globs | Where-Object { $rel -match $_ }).Count -gt 0 })
    if ($covering.Count -eq 0) {
        $violations.Add("${rel}: GPL-family licence ($expression) but not listed in THIRD_PARTY_LICENSES.md")
        continue
    }
    $fileIds = Get-GplIds $expression
    $matched = $false
    foreach ($row in $covering) {
        $missing = @($fileIds | Where-Object { $id = $_; -not @($row.Ids | Where-Object { [string]::Equals($_, $id, [StringComparison]::OrdinalIgnoreCase) }).Count })
        if ($missing.Count -eq 0) { $matched = $true; break }
    }
    if (-not $matched) {
        $rows = ($covering | ForEach-Object { "'$($_.Component)' ($($_.Licence))" }) -join ', '
        $violations.Add("${rel}: SPDX licence ($expression) does not match the licence of its THIRD_PARTY_LICENSES.md row $rows")
    }
}

if ($violations.Count -gt 0) {
    foreach ($v in $violations) { Write-Output "FAIL: $v" }
    Write-Output "C-006 licence check failed: $($violations.Count) violation(s) in $checked code file(s)."
    exit 1
}

Write-Output "C-006 licence check passed: $checked code file(s), $($components.Count) component row(s)."
exit 0
