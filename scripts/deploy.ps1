# SPDX-License-Identifier: MIT
# Copyright (c) 2026 itsloopyo

<#
.SYNOPSIS
    Copies the freshly built .asi and the vendored ASI loader into every local
    Wolfenstein: The New Order install.

.PARAMETER GamePath
    Install root to deploy into. Omitted, every copy of the game on this
    machine is located and written to - Steam and Game Pass both ship this
    title, owning it twice is ordinary, and a deploy that picks one silently
    leaves the other running whatever build was last dropped in it.
#>
[CmdletBinding()]
param([Parameter(Position = 0)][string]$GamePath)

$ErrorActionPreference = 'Stop'

Import-Module (Join-Path $PSScriptRoot 'ModProject.psm1') -Force
Import-Module (Join-Path $PSScriptRoot 'ReleaseSupport.psm1') -Force
$project = Get-ModProject
Import-Module (Join-Path $project.Root 'cameraunlock-core/powershell/GamePathDetection.psm1') -Force

# Every expected failure below is one red line and exit 1, so a `pixi run
# install` that cannot find something reads the same whichever step gave up.
# Unexpected errors are left to terminate with their own record.
function Stop-Deploy {
    param([Parameter(Mandatory = $true)][string]$Message)
    Write-Host "ERROR: $Message" -ForegroundColor Red
    exit 1
}

if (-not (Test-Path -LiteralPath $project.BuildOutputPath)) {
    Stop-Deploy "build output not found at $($project.BuildOutputPath). Run 'pixi run build' first."
}

# games.json is the only source of the game's location, exactly as it is for
# install.cmd. A literal copy here would be a second one, and the dev loop
# would then resolve the game while the shipped installer did not.
$catalog = Get-GameConfigs
if (-not $catalog.ContainsKey($project.Id)) {
    Stop-Deploy "cameraunlock-core/data/games.json has no '$($project.Id)' entry, so neither this script nor install.cmd can find the game. Add the entry in cameraunlock-core and run 'pixi run sync'."
}
$config = $catalog[$project.Id]

# A path passed on the command line wins over detection, exactly as it does
# for install.cmd's positional argument. Without one, every install on the
# machine is a target: Find-AllGamePaths, never Find-GamePath, which returns
# the highest-priority copy and hides the rest.
if ($GamePath) {
    if (-not (Test-Path -LiteralPath $GamePath -PathType Container)) {
        Stop-Deploy "supplied game path is not a directory: $GamePath"
    }
    $targets = @($GamePath)
} else {
    $targets = @(Find-AllGamePaths -Config $config)
}
if ($targets.Count -eq 0) {
    # env_var is optional in games.json; without this an entry that omits one
    # tells the user to "Set  or pass the path".
    $hint = if ($config.EnvVar) {
        "Set $($config.EnvVar), or pass the path as the first argument."
    } else {
        'Pass the path as the first argument.'
    }
    Stop-Deploy "$($project.DisplayName) install not found. $hint"
}

# install.cmd owns which import the loader is renamed onto; reading it back is
# what keeps the dev loop testing the same proxy the installer deploys.
try {
    $loaderName = Get-AsiLoaderProxyName -InstallCmdPath $project.InstallCmdPath
} catch {
    Stop-Deploy $_.Exception.Message
}

Write-Host "Deploying to $($targets.Count) install(s) of $($project.DisplayName)." -ForegroundColor Cyan

foreach ($target in $targets) {
    $exe = Join-Path $target $config.Executable
    if (-not (Test-Path -LiteralPath $exe)) {
        Stop-Deploy "game exe not found at $exe."
    }
    $exeDir = Split-Path -LiteralPath $exe

    # Copy-FileLiteral, not Copy-Item: Copy-Item's -Path is wildcard-expanded,
    # and a game installed under a directory holding [ or ] ("D:\Games [SSD]")
    # makes it copy nothing and raise nothing. The two green lines below then
    # claim a deploy that never happened, and the session goes looking for the
    # mod's log instead.
    $loader = Join-Path $exeDir $loaderName
    if (-not (Test-Path -LiteralPath $loader)) {
        Copy-FileLiteral -Path $project.VendorLoaderDll -Destination $loader
        Write-Host "Deployed Ultimate ASI Loader -> $loaderName in $exeDir" -ForegroundColor Green
    }

    Copy-FileLiteral -Path $project.BuildOutputPath -Destination (Join-Path $exeDir $project.AsiFileName)
    Write-Host "Deployed $($project.AsiFileName) to $exeDir" -ForegroundColor Green
}
