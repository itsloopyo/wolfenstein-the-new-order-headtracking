# SPDX-License-Identifier: MIT
# Copyright (c) 2026 itsloopyo

<#
.SYNOPSIS
    File-editing mechanics for release.ps1: encoding-safe writes, version
    stamping, and the maintenance changelog entry.

.DESCRIPTION
    Separated from release.ps1 so that script is only the release policy -
    preconditions, order of operations, what gets committed - and so the text
    manipulation below can be exercised without cutting a release.
#>

Set-StrictMode -Version Latest

<#
.SYNOPSIS
    Writes UTF-8 without a BOM, leaving the caller's line endings alone.
.DESCRIPTION
    Windows PowerShell 5.1's `-Encoding utf8` means UTF-8 WITH a BOM, and pixi
    rejects a pixi.toml that starts with one ("Missing table in manifest"). The
    release then aborts inside `pixi run package`, after the version has already
    been stamped and before the tag exists - a half-bumped tree with no way
    forward. Writing raw text through .NET also leaves each file's existing line
    endings alone (install.cmd is CRLF and must stay CRLF).
#>
function Set-TextFileNoBom {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)][AllowEmptyString()][string]$Text
    )
    [System.IO.File]::WriteAllText($Path, $Text, (New-Object System.Text.UTF8Encoding $false))
}

<#
.SYNOPSIS
    Rewrites a version string in place, failing when the stamp matched nothing.
.PARAMETER Path
    Full path to the file to stamp.
#>
function Update-VersionInFile {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)][string]$Pattern,
        [Parameter(Mandatory = $true)][string]$Replacement
    )
    $text = [System.IO.File]::ReadAllText($Path)
    $updated = $text -replace $Pattern, $Replacement
    if ($updated -eq $text) { throw "Version stamp did not match anything in $Path" }
    Set-TextFileNoBom -Path $Path -Text $updated
}

<#
.SYNOPSIS
    Inserts a "no user-facing changes" entry at the top of the changelog.
.DESCRIPTION
    Mirrors New-ChangelogFromCommits' insertion so a -Force maintenance entry
    lands in the same place with the same shape: straight after the "# Changelog"
    heading and the blank line that follows it.

    The anchor is pinned to the start of the file. A pattern free to match
    further down settles on the first blank line anywhere, which in a changelog
    whose heading is not followed by one is the blank line inside the newest
    entry - and the maintenance entry then lands between that entry's heading
    and its body.
.PARAMETER Path
    Full path to CHANGELOG.md. Read and written through the same path.
#>
function Add-MaintenanceChangelogEntry {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)][string]$NewVersion
    )
    $date = Get-Date -Format 'yyyy-MM-dd'
    $entry = "## [$NewVersion] - $date`n`n### Changed`n`n- Maintenance release (no user-facing changes).`n`n"

    # ReadAllText, not Get-Content -Raw: 5.1 reads a BOM-less file as ANSI,
    # which mojibakes any non-ASCII already in the changelog on the way through
    # this rewrite.
    $changelog = [System.IO.File]::ReadAllText($Path)
    $heading = [regex]::Match($changelog, '\A# Changelog\r?\n(?:\r?\n)?')
    if (-not $heading.Success) {
        throw "$Path does not start with a '# Changelog' heading; refusing to guess where the entry goes."
    }

    # Concatenated rather than -replace: the entry is literal text, and a
    # replacement string would re-expand any $ inside it.
    $updated = $heading.Value + $entry + $changelog.Substring($heading.Length)
    Set-TextFileNoBom -Path $Path -Text ($updated.TrimEnd() + "`n")
}

<#
.SYNOPSIS
    Copies one file by literal path, refusing to report a success it did not have.
.DESCRIPTION
    Copy-Item's -Path is wildcard-expanded. Under a directory whose name holds
    [ or ] - "D:\Games [SSD]" is the shape that turns up - the pattern matches
    nothing, and Copy-Item then copies nothing AND raises nothing. The packager
    stages an installer ZIP with no .asi in it and exits 0; the dev deploy
    prints "Deployed" in green having written no file. Both land on the user as
    "the mod does not load", with no failed step to point at.

    Destination may name an existing directory (the file keeps its own name) or
    the full target path.
.PARAMETER Path
    File to copy. Must exist.
.PARAMETER Destination
    Target directory or target file path.
#>
function Copy-FileLiteral {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)][string]$Destination
    )
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        throw "Copy source is not a file: $Path"
    }
    $target = if (Test-Path -LiteralPath $Destination -PathType Container) {
        Join-Path $Destination ([System.IO.Path]::GetFileName($Path))
    } else {
        $Destination
    }
    Copy-Item -LiteralPath $Path -Destination $target -Force
    if (-not (Test-Path -LiteralPath $target -PathType Leaf)) {
        throw "Copy of $Path returned without an error but $target was not written."
    }
}

<#
.SYNOPSIS
    Zips a staging directory's contents, entries rooted at that directory.
.DESCRIPTION
    Same wildcard trap as Copy-FileLiteral, one step further downstream:
    `Compress-Archive -Path "$dir\*"` under a path containing [ or ] writes no
    archive at all and raises nothing, so a release run finishes green with no
    ZIP on disk. Enumerating the children and passing them as -LiteralPath
    produces byte-identical entry names and cannot silently match nothing.

    An empty staging directory is refused rather than published: an archive
    with no payload in it is the one outcome nobody downstream checks for.
.PARAMETER SourceDir
    Directory whose children become the archive's top-level entries.
.PARAMETER DestinationPath
    Full path of the ZIP to write. Overwritten if it exists.
#>
function New-ZipFromDirectory {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory = $true)][string]$SourceDir,
        [Parameter(Mandatory = $true)][string]$DestinationPath
    )
    if (-not (Test-Path -LiteralPath $SourceDir -PathType Container)) {
        throw "Staging directory does not exist: $SourceDir"
    }
    $items = @(Get-ChildItem -LiteralPath $SourceDir -Force | Select-Object -ExpandProperty FullName)
    if ($items.Count -eq 0) {
        throw "Staging directory is empty, refusing to publish an archive with no payload: $SourceDir"
    }
    Compress-Archive -LiteralPath $items -DestinationPath $DestinationPath -Force
    if (-not (Test-Path -LiteralPath $DestinationPath -PathType Leaf)) {
        throw "Compress-Archive returned without an error but $DestinationPath was not written."
    }
}

Export-ModuleMember -Function @(
    'Set-TextFileNoBom',
    'Update-VersionInFile',
    'Add-MaintenanceChangelogEntry',
    'Copy-FileLiteral',
    'New-ZipFromDirectory'
)
