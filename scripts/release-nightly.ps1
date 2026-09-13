# SPDX-License-Identifier: MIT
# Copyright (c) 2026 itsloopyo

# Thin shim. Determine version, delegate to the shared publisher.
# See cameraunlock-core/powershell/NightlyRelease.psm1 for what it does.

[CmdletBinding()]
param(
    [switch]$AllowDirty
)

$ErrorActionPreference = 'Stop'

Import-Module (Join-Path $PSScriptRoot 'ModProject.psm1') -Force
$project = Get-ModProject
Import-Module (Join-Path $project.Root 'cameraunlock-core/powershell/NightlyRelease.psm1') -Force

# -NoNexusZip because this mod is installer-only: the payload sits beside the
# game exe and no mod manager can deploy it there. The default treats a missing
# Nexus ZIP as fatal, so without the switch every nightly fails. See the notes in
# package-release.ps1 for why the stage is gone.
#
# CMakeLists.txt is this mod's canonical version, and the string
# package-release.ps1 puts in the installer ZIP filename the publisher
# looks for. There is no constants.h version macro here.
$version = Get-ModVersion

Publish-NightlyBuild `
    -ModId $project.Id `
    -ModName $project.Name `
    -Version $version `
    -ProjectRoot $project.Root `
    -BuildCommand 'pixi run build' `
    -NoNexusZip `
    -AllowDirty:$AllowDirty
