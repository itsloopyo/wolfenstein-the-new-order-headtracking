#!/usr/bin/env pwsh
#Requires -Version 5.1
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 itsloopyo
# Manually refresh the vendored Ultimate ASI Loader. See AGENTS.md "Vendoring".
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

Import-Module (Join-Path $PSScriptRoot 'ModProject.psm1') -Force
$project = Get-ModProject
Import-Module (Join-Path $project.Root 'cameraunlock-core/powershell/ModLoaderSetup.psm1') -Force

# Fixed by the PE format: the offset of the PE header pointer in the DOS stub,
# the "PE\0\0" signature it points at, and IMAGE_FILE_MACHINE_AMD64.
$PeHeaderPointerOffset = 0x3C
$PeSignature = 0x00004550
$ImageFileMachineAmd64 = 0x8664
# The DOS stub a linker emits puts the PE header a few hundred bytes in, so a
# 4 KB prefix always covers it and there is no reason to read further.
$PeHeaderSearchBytes = 0x1000

$assetName = 'Ultimate-ASI-Loader_x64.zip'
$zipPath = Join-Path $project.VendorLoaderDir $assetName
$dllPath = $project.VendorLoaderDll

# Wolfenstein: The New Order ships a single 64-bit executable
# (WolfNewOrder_x64.exe), so we need the x64 Ultimate ASI Loader. The upstream
# asset is a zip; install.cmd and deploy.ps1 both copy a bare proxy DLL into the
# game root, so we extract the single DLL the zip contains rather than
# vendoring the zip itself.
$meta = Update-VendoredLoader `
    -Name 'ultimate-asi-loader' `
    -OutputDir $project.VendorLoaderDir `
    -OutputFileName $assetName `
    -Owner 'ThirteenAG' -Repo 'Ultimate-ASI-Loader' `
    -VersionPrefix 'v9.' `
    -AssetPattern '^Ultimate-ASI-Loader_x64\.zip$'

# Checked before anything below mutates the vendor directory.
$licensePath = Join-Path $project.VendorLoaderDir 'LICENSE'
if (-not (Test-Path -LiteralPath $licensePath)) {
    throw "Upstream LICENSE was not written to $($project.VendorLoaderDir) - refusing to vendor a loader binary without its licence."
}

Add-Type -AssemblyName System.IO.Compression.FileSystem

# Extracted under a temp name and vetted there, then moved into place. Writing
# ExtractToFile straight onto $dllPath leaves an unvetted binary - a truncated
# entry, or an x86 proxy that crashes an x64 game on launch - sitting at the
# exact path install.cmd and deploy.ps1 ship from whenever a check below fails.
# Update-VendoredLoader has already rewritten README.md with the new tag by
# then, so nothing but a human reading the diff would catch it.
$stagedDll = "$dllPath.incoming"
try {
    try {
        $zip = [System.IO.Compression.ZipFile]::OpenRead($zipPath)
        try {
            $entry = $zip.Entries | Where-Object { $_.Name -ieq 'dinput8.dll' } | Select-Object -First 1
            if (-not $entry) { throw "x64 Ultimate ASI Loader zip did not contain dinput8.dll" }
            [System.IO.Compression.ZipFileExtensions]::ExtractToFile($entry, $stagedDll, $true)
        } finally { $zip.Dispose() }
    } finally {
        # The zip is a download intermediate: not shipped, not committed, and *.zip
        # is not gitignored, so a run that left it behind would put an untracked
        # binary in the vendor directory. Cleanup must not mask an extraction
        # failure, hence the swallow here and nowhere else.
        Remove-Item -LiteralPath $zipPath -Force -ErrorAction SilentlyContinue
    }

    # Fail fast if we somehow extracted the wrong architecture - copying an x86 (or
    # zip) proxy into an x64 game crashes the game on launch before our code runs.
    # Only the header is read; the DLL itself is megabytes and none of it matters.
    $header = New-Object byte[] $PeHeaderSearchBytes
    $stream = [System.IO.File]::OpenRead($stagedDll)
    try {
        # Looped, because FileStream.Read may return fewer bytes than asked for.
        # Treating one short read as end-of-file rejects a sound loader with a
        # "no PE header" message that sends the next session hunting upstream.
        $headerLength = 0
        while ($headerLength -lt $header.Length) {
            $read = $stream.Read($header, $headerLength, $header.Length - $headerLength)
            if ($read -le 0) { break }
            $headerLength += $read
        }
    } finally { $stream.Dispose() }

    $peOffset = if ($headerLength -ge ($PeHeaderPointerOffset + 4)) {
        [BitConverter]::ToInt32($header, $PeHeaderPointerOffset)
    } else {
        -1
    }
    if ($peOffset -lt 0 -or ($peOffset + 6) -gt $headerLength) {
        throw "Extracted ASI Loader has no PE header within its first $PeHeaderSearchBytes bytes; refusing to vendor it."
    }
    if ([BitConverter]::ToUInt32($header, $peOffset) -ne $PeSignature) {
        throw "Extracted ASI Loader is not a PE image; refusing to vendor it."
    }
    $machine = [BitConverter]::ToUInt16($header, $peOffset + 4)
    if ($machine -ne $ImageFileMachineAmd64) {
        throw ("Extracted ASI Loader is not x64 (machine=0x{0:X4}); refusing to vendor it." -f $machine)
    }

    Move-Item -LiteralPath $stagedDll -Destination $dllPath -Force
} finally {
    # Reached with the staged file still present only when a check above threw.
    # The committed dinput8.dll is untouched until the move, so the vendor
    # directory is left holding the loader that was already vetted.
    if (Test-Path -LiteralPath $stagedDll) {
        Remove-Item -LiteralPath $stagedDll -Force -ErrorAction SilentlyContinue
    }
}

# Record the hash of the artifact we actually commit, so its provenance can be
# checked without keeping the upstream zip around. Update-VendoredLoader rewrote
# README.md from scratch above (its "unchanged, leave the tree alone" shortcut
# needs the zip on disk, and we never keep it), so this section is appended to a
# fresh file rather than stacking up a run at a time.
$dllHash = (Get-FileHash -LiteralPath $dllPath -Algorithm SHA256).Hash.ToLowerInvariant()
Add-Content -LiteralPath (Join-Path $project.VendorLoaderDir 'README.md') -Encoding utf8 -Value @"

## Committed artifact

Only ``dinput8.dll`` is committed; the upstream zip is a download intermediate
and is deleted after extraction.

- File: ``dinput8.dll`` (extracted from the asset above, unmodified)
- SHA-256: ``$dllHash``
"@

Write-Host "Vendored x64 Ultimate ASI Loader ($($meta.Tag)) extracted to vendor/ultimate-asi-loader/dinput8.dll" -ForegroundColor Green
