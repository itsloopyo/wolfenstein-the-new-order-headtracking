# SPDX-License-Identifier: MIT
# Copyright (c) 2026 itsloopyo

<#
.SYNOPSIS
    The one place that knows what this mod is called and where its files live.

.DESCRIPTION
    Every script under scripts/ needs some subset of the same handful of facts:
    the ASI name, the games.json id, where the build output lands, where the
    vendored loader sits, and which version is being shipped. Spelled out once
    per script those drift silently - a rename that misses one script leaves a
    packager staging a file the deploy loop never produced, and nothing fails
    until a release ZIP is opened.

    The version read delegates to core's Get-ProjectVersion, which is the same
    read release.yml performs (version-source: cmake). A hand-rolled regex here
    can disagree with the tag check that runs on the push, and the disagreement
    only surfaces after the tag exists.
#>

Set-StrictMode -Version Latest

# The module ships in scripts/, so the repo root is its parent.
$script:ProjectRoot = Split-Path -Parent $PSScriptRoot

Import-Module (Join-Path $script:ProjectRoot 'cameraunlock-core/powershell/ReleaseWorkflow.psm1') -Force

$script:ModName = 'WolfensteinTheNewOrderHeadTracking'
$script:ModId = 'wolfenstein-the-new-order'
$script:DisplayName = 'Wolfenstein: The New Order'

<#
.SYNOPSIS
    Names and paths for this mod, all relative to the repo root.
.OUTPUTS
    PSCustomObject.
#>
function Get-ModProject {
    [CmdletBinding()]
    [OutputType([pscustomobject])]
    param()

    $asiFileName = "$($script:ModName).asi"
    [pscustomobject]@{
        Name             = $script:ModName
        Id               = $script:ModId
        DisplayName      = $script:DisplayName
        Root             = $script:ProjectRoot
        AsiFileName      = $asiFileName
        CMakeListsPath   = Join-Path $script:ProjectRoot 'CMakeLists.txt'
        BuildOutputPath  = Join-Path $script:ProjectRoot "build/Release/$asiFileName"
        VendorLoaderDir  = Join-Path $script:ProjectRoot 'vendor/ultimate-asi-loader'
        VendorLoaderDll  = Join-Path $script:ProjectRoot 'vendor/ultimate-asi-loader/dinput8.dll'
        InstallCmdPath   = Join-Path $script:ProjectRoot 'scripts/install.cmd'
        UninstallCmdPath = Join-Path $script:ProjectRoot 'scripts/uninstall.cmd'
        ReleaseDir       = Join-Path $script:ProjectRoot 'release'
    }
}

<#
.SYNOPSIS
    Reads the shipped version out of CMakeLists.txt, the canonical source.
.PARAMETER CMakeListsPath
    Override for tests; defaults to this repo's CMakeLists.txt.
.OUTPUTS
    String, X.Y.Z.
#>
function Get-ModVersion {
    [CmdletBinding()]
    [OutputType([string])]
    param([string]$CMakeListsPath = (Get-ModProject).CMakeListsPath)

    $version = Get-ProjectVersion -Source cmake -Path $CMakeListsPath
    # Get-ProjectVersion accepts CMake's up-to-four components. Everything
    # downstream (tag names, ZIP names, Resolve-ReleaseVersion) assumes X.Y.Z,
    # so a two- or four-part version is caught here rather than halfway
    # through a release.
    if (-not (Test-SemanticVersion -Version $version)) {
        throw "Version '$version' in $CMakeListsPath is not X.Y.Z."
    }
    return $version
}

<#
.SYNOPSIS
    Reads back the proxy DLL filename install.cmd renames the ASI loader onto.
.DESCRIPTION
    install.cmd owns that choice, and reading it back is what keeps the dev
    loop testing the same proxy the installer deploys. A proxy the game does
    not import is never loaded, and the mod then produces no log at all to
    explain itself.
.PARAMETER InstallCmdPath
    Override for tests; defaults to this repo's scripts/install.cmd.
.OUTPUTS
    String, e.g. 'dinput8.dll'.
#>
function Get-AsiLoaderProxyName {
    [CmdletBinding()]
    [OutputType([string])]
    param([string]$InstallCmdPath = (Get-ModProject).InstallCmdPath)

    if (-not (Test-Path -LiteralPath $InstallCmdPath)) {
        throw "install.cmd not found at $InstallCmdPath; it declares ASI_LOADER_NAME, the proxy filename this deploy has to match."
    }
    # -List stops at the first hit: install.cmd sets this once, and a file that
    # somehow set it twice would otherwise pick a winner silently.
    $match = Select-String -LiteralPath $InstallCmdPath -List -Pattern '^\s*set\s+"ASI_LOADER_NAME=([^"]+)"'
    if (-not $match) {
        throw "no ASI_LOADER_NAME setting in $InstallCmdPath."
    }
    return $match.Matches[0].Groups[1].Value
}

Export-ModuleMember -Function @(
    'Get-ModProject',
    'Get-ModVersion',
    'Get-AsiLoaderProxyName'
)
