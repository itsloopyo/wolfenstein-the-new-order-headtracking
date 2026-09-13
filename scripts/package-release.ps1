# SPDX-License-Identifier: MIT
# Copyright (c) 2026 itsloopyo

<#
.SYNOPSIS
    Builds the installer ZIP for GitHub Releases from the current build output.

.NOTES
    There is no Nexus ZIP, deliberately. This mod's payload has to sit beside
    WolfNewOrder_x64.exe, and a mod manager deploys into one fixed subtree of the
    game folder. Vortex ships no extension for Wolfenstein: The New Order at all,
    so it cannot discover the game or deploy to it, and Mod Organizer 2 would
    need Root Builder. A ZIP that a manager installs "successfully" while the ASI
    never loads is the exact silent failure the fleet rule exists to prevent, so
    this mod is installer-only. Do not helpfully add the stage back.
#>
[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'

Import-Module (Join-Path $PSScriptRoot 'ModProject.psm1') -Force
Import-Module (Join-Path $PSScriptRoot 'ReleaseSupport.psm1') -Force
$project = Get-ModProject
Import-Module (Join-Path $project.Root 'cameraunlock-core/powershell/ReleaseWorkflow.psm1') -Force

# CMakeLists.txt is the canonical version, read the same way release.yml reads
# the shipped version (version-source: cmake). Anything else here could publish
# a ZIP whose name disagrees with the release it lands in.
$version = Get-ModVersion

if (-not (Test-Path -LiteralPath $project.BuildOutputPath)) {
    throw "Build output missing: $($project.BuildOutputPath). Run 'pixi run build' first."
}

# No -ErrorAction SilentlyContinue: a locked release/ (an open Explorer window,
# a running AV scan, a ZIP still held) would otherwise be swallowed, staging
# would proceed into a directory still holding the previous build's files, and
# Compress-Archive would package the lot as a new release.
if (Test-Path -LiteralPath $project.ReleaseDir) { Remove-Item -Recurse -Force -LiteralPath $project.ReleaseDir }
$stage = Join-Path $project.ReleaseDir 'installer'
$plugins = Join-Path $stage 'plugins'
$vendorDst = Join-Path $stage 'vendor/ultimate-asi-loader'
New-Item -ItemType Directory -Force $plugins, $vendorDst | Out-Null

# Copy-FileLiteral, not Copy-Item, for every stage below: Copy-Item's -Path is
# wildcard-expanded, so a checkout under a directory holding [ or ] copies
# nothing, reports nothing, and ships an installer ZIP with no payload in it.
Copy-FileLiteral -Path $project.BuildOutputPath -Destination $plugins

# Installer scripts + game-detection shim. Copy-SharedBundle stages the whole
# shim set (the install/uninstall bodies, find-game.ps1, GamePathDetection.psm1,
# games.json) that install.cmd calls into; a wrapper without its body aborts
# with "corrupt installer".
Copy-FileLiteral -Path $project.InstallCmdPath -Destination $stage
Copy-FileLiteral -Path $project.UninstallCmdPath -Destination $stage

# The launcher's contract, at the ZIP root where it looks for it. install.cmd
# stays beside it for the standalone path; the launcher deploys from the
# manifest alone and never runs it.
Copy-FileLiteral -Path (Join-Path $project.Root 'launcher-manifest.json') -Destination $stage
Copy-SharedBundle -StagingDir $stage

# The staged games.json is what the shipped installer resolves the game through,
# and find-game.ps1 looks the id up BEFORE it honours an explicit path argument -
# so a missing entry fails auto-detect, the environment variable AND
# `install.cmd "D:\Games\Wolfenstein" /y` alike. Nothing else in the release path
# notices: packaging never resolves a game, so `pixi run release` would stamp,
# commit, tag and publish a ZIP that cannot install anywhere. Asserted against
# the STAGED copy, because that is the one the user gets.
$stagedGames = Join-Path $stage 'shared/games.json'
if (-not (Test-Path -LiteralPath $stagedGames)) {
    throw "Shared bundle staged no games.json at $stagedGames."
}
$gameIds = (Get-Content -LiteralPath $stagedGames -Raw | ConvertFrom-Json).games.PSObject.Properties.Name
if ($gameIds -notcontains $project.Id) {
    throw @"
games.json in the staged installer has no '$($project.Id)' entry, so the shipped
install.cmd cannot resolve the game by any route - not auto-detect, not the
environment variable, not an explicit path argument. Add the entry to
cameraunlock-core/data/games.json, push core, then 'pixi run sync' here and
commit the moved submodule pointer.
"@
}

# Vendored loader, exactly as committed. Refreshing it from upstream belongs to
# `pixi run update-deps`, which lands a reviewable commit; doing it here would
# ship a loader binary that never appeared in a diff.
# Named one by one rather than copied recursively, so anything that appears in
# that directory later has to be added here deliberately before it ships. The
# proxy filename comes from ModProject so a rename lands in one place.
$vendorFiles = @([System.IO.Path]::GetFileName($project.VendorLoaderDll), 'LICENSE', 'README.md')
foreach ($file in $vendorFiles) {
    Copy-FileLiteral -Path (Join-Path $project.VendorLoaderDir $file) -Destination $vendorDst
}

Copy-LicenceNotices -StagingDir $stage -ProjectRoot $project.Root -Additional @('README.md', 'CHANGELOG.md')

$installerZip = Join-Path $project.ReleaseDir "$($project.Name)-v$version-installer.zip"
New-ZipFromDirectory -SourceDir $stage -DestinationPath $installerZip
Write-Host "Installer: $installerZip" -ForegroundColor Green
