#!/usr/bin/env pwsh
#Requires -Version 5.1
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 itsloopyo
# ============================================================================
# Tests for scripts/ReleaseSupport.psm1
# ============================================================================
# Run: pixi run test
#
# Characterization tests: these lock what release.ps1 already did before the
# helpers moved out of it. The encoding and line-ending checks are the ones
# that matter - a BOM on pixi.toml aborts `pixi run package` after the version
# has been stamped and before the tag exists, and install.cmd silently stops
# working if its CRLF endings are rewritten. The check harness is in
# TestSupport.psm1.
# ============================================================================

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$repoRoot = Split-Path -Parent $PSScriptRoot
Import-Module (Join-Path $PSScriptRoot 'TestSupport.psm1') -Force
Import-Module (Join-Path $repoRoot 'scripts/ReleaseSupport.psm1') -Force

function Test-HasBom {
    param([string]$Path)
    $bytes = [System.IO.File]::ReadAllBytes($Path)
    return ($bytes.Length -ge 3 -and $bytes[0] -eq 0xEF -and $bytes[1] -eq 0xBB -and $bytes[2] -eq 0xBF)
}

$sandbox = New-TestSandbox -Prefix 'wtno-releasesupport'
try {
    # --- Set-TextFileNoBom -------------------------------------------------

    $plain = Join-Path $sandbox 'plain.toml'
    Set-TextFileNoBom -Path $plain -Text "[workspace]`nversion = `"1.0.0`"`n"
    Check 'Set-TextFileNoBom writes no BOM' (-not (Test-HasBom $plain)) 'file starts with a UTF-8 BOM'
    Check 'Set-TextFileNoBom round-trips the text unchanged' `
        ([System.IO.File]::ReadAllText($plain) -eq "[workspace]`nversion = `"1.0.0`"`n") 'text came back different'

    $crlf = Join-Path $sandbox 'crlf.cmd'
    Set-TextFileNoBom -Path $crlf -Text "@echo off`r`nset `"MOD_VERSION=1.0.0`"`r`n"
    Check 'Set-TextFileNoBom leaves CRLF endings alone' `
        ([System.IO.File]::ReadAllText($crlf) -eq "@echo off`r`nset `"MOD_VERSION=1.0.0`"`r`n") 'line endings were rewritten'

    # --- Update-VersionInFile ----------------------------------------------

    $cmake = Join-Path $sandbox 'CMakeLists.txt'
    Set-TextFileNoBom -Path $cmake -Text "cmake_minimum_required(VERSION 3.21)`nproject(WolfensteinTheNewOrderHeadTracking VERSION 0.1.0 LANGUAGES CXX)`n"
    Update-VersionInFile -Path $cmake `
        -Pattern 'project\(WolfensteinTheNewOrderHeadTracking VERSION [0-9.]+' `
        -Replacement 'project(WolfensteinTheNewOrderHeadTracking VERSION 2.0.0'
    $cmakeText = [System.IO.File]::ReadAllText($cmake)
    Check 'Update-VersionInFile stamps the CMake project version' `
        ($cmakeText -match 'project\(WolfensteinTheNewOrderHeadTracking VERSION 2\.0\.0 LANGUAGES CXX\)') "got: $cmakeText"
    Check 'Update-VersionInFile leaves cmake_minimum_required(VERSION ...) alone' `
        ($cmakeText -match 'cmake_minimum_required\(VERSION 3\.21\)') "got: $cmakeText"

    $pixi = Join-Path $sandbox 'pixi.toml'
    Set-TextFileNoBom -Path $pixi -Text "[workspace]`nname = `"m`"`nversion = `"0.1.0`"`n`n[dependencies]`nsomething = `"version = 1`"`n"
    Update-VersionInFile -Path $pixi -Pattern '(?m)^version = "[0-9.]+"' -Replacement 'version = "2.0.0"'
    $pixiText = [System.IO.File]::ReadAllText($pixi)
    Check 'Update-VersionInFile stamps the pixi workspace version' `
        ($pixiText -match '(?m)^version = "2\.0\.0"$') "got: $pixiText"
    Check 'Update-VersionInFile leaves a mid-line version-like string alone' `
        ($pixiText -match 'something = "version = 1"') "got: $pixiText"
    Check 'Update-VersionInFile writes pixi.toml without a BOM' (-not (Test-HasBom $pixi)) 'pixi would reject this file'

    $installCmd = Join-Path $sandbox 'install.cmd'
    Set-TextFileNoBom -Path $installCmd -Text "@echo off`r`nset `"MOD_VERSION=0.1.0`"`r`n"
    Update-VersionInFile -Path $installCmd -Pattern '(?m)^set "MOD_VERSION=[0-9.]+"' -Replacement 'set "MOD_VERSION=2.0.0"'
    $installText = [System.IO.File]::ReadAllText($installCmd)
    Check 'Update-VersionInFile stamps MOD_VERSION in install.cmd' `
        ($installText -match 'MOD_VERSION=2\.0\.0') "got: $installText"
    Check 'Update-VersionInFile keeps install.cmd CRLF' `
        ($installText -eq "@echo off`r`nset `"MOD_VERSION=2.0.0`"`r`n") 'CRLF endings were lost'

    $err = Get-ThrownMessage { Update-VersionInFile -Path $pixi -Pattern 'nothing-matches-this' -Replacement 'x' }
    Check 'Update-VersionInFile fails loudly when the stamp matches nothing' `
        ($err -like '*did not match anything*') "threw '$err'"

    # --- Add-MaintenanceChangelogEntry -------------------------------------

    $today = Get-Date -Format 'yyyy-MM-dd'

    $changelog = Join-Path $sandbox 'CHANGELOG.md'
    Set-TextFileNoBom -Path $changelog -Text "# Changelog`n`n## [0.0.0] - 2026-09-02`n`n### Added`n- Initial release.`n"
    Add-MaintenanceChangelogEntry -Path $changelog -NewVersion '0.1.0'
    $text = [System.IO.File]::ReadAllText($changelog)

    Check 'Add-MaintenanceChangelogEntry keeps the Changelog heading first' `
        ($text.StartsWith("# Changelog`n`n## [0.1.0] - $today")) "got: $text"
    Check 'Add-MaintenanceChangelogEntry writes the maintenance body' `
        ($text -match '### Changed\n\n- Maintenance release \(no user-facing changes\)\.') "got: $text"
    Check 'Add-MaintenanceChangelogEntry inserts above the previous entry' `
        ($text.IndexOf('## [0.1.0]') -lt $text.IndexOf('## [0.0.0]')) "got: $text"
    Check 'Add-MaintenanceChangelogEntry preserves the previous entry' `
        ($text -match '## \[0\.0\.0\] - 2026-09-02\n\n### Added\n- Initial release\.') "got: $text"
    Check 'Add-MaintenanceChangelogEntry ends the file with exactly one newline' `
        ($text.EndsWith("release.`n") -and -not $text.EndsWith("`n`n")) "got: $text"
    Check 'Add-MaintenanceChangelogEntry writes no BOM' (-not (Test-HasBom $changelog)) 'file starts with a UTF-8 BOM'

    $emptyLog = Join-Path $sandbox 'EMPTY-CHANGELOG.md'
    Set-TextFileNoBom -Path $emptyLog -Text "# Changelog`n"
    Add-MaintenanceChangelogEntry -Path $emptyLog -NewVersion '0.1.0'
    $emptyText = [System.IO.File]::ReadAllText($emptyLog)
    Check 'Add-MaintenanceChangelogEntry seeds a changelog that has no entries yet' `
        ($emptyText.StartsWith("# Changelog`n## [0.1.0] - $today")) "got: $emptyText"

    # A heading with no blank line under it: the entry belongs above the newest
    # entry, not between that entry's heading and its body.
    $tightLog = Join-Path $sandbox 'TIGHT-CHANGELOG.md'
    Set-TextFileNoBom -Path $tightLog -Text "# Changelog`n## [0.0.0] - 2026-09-02`n`n### Added`n- Initial release.`n"
    Add-MaintenanceChangelogEntry -Path $tightLog -NewVersion '0.1.0'
    $tightText = [System.IO.File]::ReadAllText($tightLog)
    Check 'Add-MaintenanceChangelogEntry inserts above an entry the heading abuts' `
        ($tightText.StartsWith("# Changelog`n## [0.1.0] - $today")) "got: $tightText"
    Check 'Add-MaintenanceChangelogEntry leaves the abutting entry intact' `
        ($tightText -match '## \[0\.0\.0\] - 2026-09-02\n\n### Added\n- Initial release\.') "got: $tightText"

    # --- Copy-FileLiteral / New-ZipFromDirectory ---------------------------
    #
    # The regression these lock: Copy-Item -Path and Compress-Archive -Path are
    # wildcard-expanded, so under a directory whose name holds [ or ] they copy
    # nothing / write no archive AND raise nothing. That shipped an installer
    # ZIP with no .asi in it and a dev deploy that printed "Deployed" in green.
    # 'Games [SSD]' is the real-world shape.

    $bracket = Join-Path $sandbox 'Games [SSD]'
    New-Item -ItemType Directory -Path $bracket -Force | Out-Null
    $bracketSrc = Join-Path $bracket 'Mod.asi'
    Set-TextFileNoBom -Path $bracketSrc -Text 'ASI PAYLOAD'

    $copyDstDir = Join-Path $sandbox 'copy-dest'
    New-Item -ItemType Directory -Path $copyDstDir -Force | Out-Null

    # Characterize the bug itself, so this stays a regression test rather than
    # a test of Copy-FileLiteral in isolation.
    Copy-Item $bracketSrc $copyDstDir -Force -ErrorAction SilentlyContinue
    Check 'Copy-Item -Path silently copies nothing from a bracketed path' `
        (-not (Test-Path -LiteralPath (Join-Path $copyDstDir 'Mod.asi'))) `
        'Copy-Item now handles bracketed paths; the wrapper may no longer be needed'

    Copy-FileLiteral -Path $bracketSrc -Destination $copyDstDir
    Check 'Copy-FileLiteral copies out of a bracketed directory' `
        ([System.IO.File]::ReadAllText((Join-Path $copyDstDir 'Mod.asi')) -eq 'ASI PAYLOAD') `
        'payload did not arrive'

    $explicitTarget = Join-Path $copyDstDir 'renamed.asi'
    Copy-FileLiteral -Path $bracketSrc -Destination $explicitTarget
    Check 'Copy-FileLiteral honours an explicit target filename' `
        ([System.IO.File]::ReadAllText($explicitTarget) -eq 'ASI PAYLOAD') 'target file not written'

    $err = Get-ThrownMessage { Copy-FileLiteral -Path (Join-Path $sandbox 'absent.asi') -Destination $copyDstDir }
    Check 'Copy-FileLiteral fails loudly when the source is missing' `
        ($err -like '*Copy source is not a file*') "threw '$err'"

    $err = Get-ThrownMessage { Copy-FileLiteral -Path $bracket -Destination $copyDstDir }
    Check 'Copy-FileLiteral refuses a directory as the source' `
        ($err -like '*Copy source is not a file*') "threw '$err'"

    # Staging directory shaped like the packager's: a nested payload plus a
    # root-level file, under a bracketed path.
    $zipStage = Join-Path $bracket 'stage'
    New-Item -ItemType Directory -Path (Join-Path $zipStage 'plugins') -Force | Out-Null
    Set-TextFileNoBom -Path (Join-Path $zipStage 'plugins/Mod.asi') -Text 'ASI PAYLOAD'
    Set-TextFileNoBom -Path (Join-Path $zipStage 'install.cmd') -Text "@echo off`r`n"

    $wildZip = Join-Path $sandbox 'wildcard.zip'
    Compress-Archive -Path (Join-Path $zipStage '*') -DestinationPath $wildZip -Force -ErrorAction SilentlyContinue
    Check 'Compress-Archive -Path silently writes no ZIP from a bracketed stage' `
        (-not (Test-Path -LiteralPath $wildZip)) `
        'Compress-Archive now handles bracketed paths; the wrapper may no longer be needed'

    $goodZip = Join-Path $sandbox 'installer.zip'
    New-ZipFromDirectory -SourceDir $zipStage -DestinationPath $goodZip
    Check 'New-ZipFromDirectory writes a ZIP from a bracketed stage' `
        (Test-Path -LiteralPath $goodZip) 'no archive was written'

    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $archive = [System.IO.Compression.ZipFile]::OpenRead($goodZip)
    try {
        $entries = @($archive.Entries | ForEach-Object { $_.FullName })
    } finally { $archive.Dispose() }

    # Entry names are the packager's contract - install.cmd reads plugins\ and
    # the ZIP root - so the wrapper has to reproduce what the wildcard form
    # produced, separators included.
    Check 'New-ZipFromDirectory roots entries at the staging directory' `
        (($entries -contains 'plugins\Mod.asi') -and ($entries -contains 'install.cmd')) `
        "entries: $($entries -join ', ')"

    $emptyStage = Join-Path $sandbox 'empty-stage'
    New-Item -ItemType Directory -Path $emptyStage -Force | Out-Null
    $err = Get-ThrownMessage { New-ZipFromDirectory -SourceDir $emptyStage -DestinationPath (Join-Path $sandbox 'empty.zip') }
    Check 'New-ZipFromDirectory refuses to publish an archive with no payload' `
        ($err -like '*no payload*') "threw '$err'"
    Check 'New-ZipFromDirectory writes nothing when it refuses' `
        (-not (Test-Path -LiteralPath (Join-Path $sandbox 'empty.zip'))) 'an empty archive was written'

    $err = Get-ThrownMessage { New-ZipFromDirectory -SourceDir (Join-Path $sandbox 'absent-stage') -DestinationPath (Join-Path $sandbox 'x.zip') }
    Check 'New-ZipFromDirectory fails loudly when the stage is missing' `
        ($err -like '*Staging directory does not exist*') "threw '$err'"

    $headless = Join-Path $sandbox 'HEADLESS-CHANGELOG.md'
    Set-TextFileNoBom -Path $headless -Text "## [0.0.0] - 2026-09-02`n`n### Added`n- Initial release.`n"
    $err = Get-ThrownMessage { Add-MaintenanceChangelogEntry -Path $headless -NewVersion '0.1.0' }
    Check 'Add-MaintenanceChangelogEntry fails loudly with no Changelog heading' `
        ($err -like "*does not start with a '# Changelog' heading*") "threw '$err'"
} finally {
    Remove-Item -LiteralPath $sandbox -Recurse -Force -ErrorAction SilentlyContinue
}

exit (Complete-Checks)
