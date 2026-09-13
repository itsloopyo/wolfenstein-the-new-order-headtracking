# SPDX-License-Identifier: MIT
# Copyright (c) 2026 itsloopyo

<#
.SYNOPSIS
    Cuts a release: stamps the version, regenerates the changelog, builds and
    packages, then commits, tags and pushes.

.DESCRIPTION
    Runs unattended. `pixi run release minor` is the authorization; there is no
    second gate, and nothing here reads stdin. The preconditions (on main, clean
    tree, tag absent, resolvable version) are the safety net, and each one exits
    non-zero with a single line of diagnostic.

    The file mechanics live in ReleaseSupport.psm1; what is left here is the
    policy - the order of operations and what gets committed.

.PARAMETER Version
    major, minor, patch, or a literal X.Y.Z.

.PARAMETER Force
    Release even when every commit since the last tag is noise (writes a
    maintenance changelog entry instead of aborting).
#>
[CmdletBinding()]
param(
    # NOT Mandatory: PowerShell satisfies a missing mandatory parameter by
    # reading stdin, and `pixi run release` allocates no TTY - that prompt
    # dies with "IOException: The handle is invalid" instead of printing a
    # usage line. Validate it ourselves and fail fast.
    [Parameter(Position = 0)][string]$Version,
    [switch]$Force
)

$ErrorActionPreference = 'Stop'

if (-not $Version) {
    Write-Host "Usage: pixi run release <major|minor|patch|nightly|X.Y.Z> [-Force]" -ForegroundColor Red
    exit 1
}

if ($Version -eq 'nightly') {
    # No exit-code relay. release-nightly.ps1 runs with ErrorActionPreference
    # Stop and throws on any failure, which terminates this script too, so
    # reaching the next line means it published. $LASTEXITCODE at that point is
    # whichever native command Publish-NightlyBuild happened to run last (a
    # tolerated `gh release delete` miss among them), not a verdict.
    & (Join-Path $PSScriptRoot 'release-nightly.ps1')
    exit 0
}

Import-Module (Join-Path $PSScriptRoot 'ModProject.psm1') -Force
Import-Module (Join-Path $PSScriptRoot 'ReleaseSupport.psm1') -Force
$project = Get-ModProject
Import-Module (Join-Path $project.Root 'cameraunlock-core/powershell/ReleaseWorkflow.psm1') -Force

Push-Location $project.Root
try {
    $current = Get-ModVersion
    $new = Resolve-ReleaseVersion -Argument $Version -CurrentVersion $current
    if (-not (Test-SemanticVersion -Version $new)) { throw "Resolved version '$new' is not X.Y.Z." }

    # New-ReleaseTag pushes to `main`, so releasing from any other branch would
    # push commits the branch does not contain. Gate before anything mutates.
    # Subexpression-stringified, not .Trim() on the raw result: `git rev-parse`
    # outside a checkout prints to stderr and returns nothing, and $null.Trim()
    # then buries the real cause under a method-call error.
    $branch = "$(git rev-parse --abbrev-ref HEAD)".Trim()
    if (-not $branch) { throw 'Could not read the current branch. Is this a git checkout?' }
    if ($branch -ne 'main') { throw "Releases cut from 'main' only; currently on '$branch'." }
    if (-not (Test-CleanGitStatus)) { throw "Working tree is dirty - commit or stash first." }
    if (Test-GitTagExists -Tag "v$new") { throw "Tag v$new already exists." }


    # Generate CHANGELOG from commits since the last tag. This is the gate that
    # aborts when there are no user-facing commits, so run it BEFORE stamping
    # any version or building - a failure here then leaves a clean tree instead
    # of stranding a half-applied bump with no tag.
    try {
        New-ChangelogFromCommits -ChangelogPath 'CHANGELOG.md' -Version $new | Out-Null
    } catch {
        if (-not $Force) {
            Write-Host "Error: $($_.Exception.Message)" -ForegroundColor Red
            Write-Host "No user-facing changes to release. Re-run with -Force for a maintenance release." -ForegroundColor Yellow
            exit 1
        }
        # Printed even under -Force: this catch sees every failure of
        # New-ChangelogFromCommits, not only "all commits filtered as noise". A
        # missing CHANGELOG.md or a git failure would otherwise be relabelled
        # "no user-facing changes" and released.
        Write-Host "Changelog generation failed: $($_.Exception.Message)" -ForegroundColor Yellow
        Write-Host "Writing maintenance entry (-Force)." -ForegroundColor Yellow
        Add-MaintenanceChangelogEntry -Path (Join-Path $project.Root 'CHANGELOG.md') -NewVersion $new
    }

    # Re-point THIRD-PARTY-NOTICES.md at the pinned cameraunlock-core commit.
    # Copy-SharedBundle asserts the two agree and throws when they do not, and it
    # runs inside `pixi run package` below - so without this a release taken
    # after a submodule bump would stamp every version file, fail in the
    # packager, and strand a half-applied bump with no tag.
    #
    # Placed here, after the changelog gate and immediately before the first
    # stamp, because it REWRITES a tracked file. Run any earlier and an abort
    # from the changelog gate would leave the tree dirty, and the next release
    # attempt would fail its own clean-tree precondition pointing at a file the
    # previous run wrote.
    & (Join-Path $project.Root 'cameraunlock-core/scripts/sync-core-notices.ps1') -Repo $project.Root
    if ($LASTEXITCODE -ne 0) {
        throw "sync-core-notices.ps1 could not point THIRD-PARTY-NOTICES.md at the pinned cameraunlock-core commit (exit $LASTEXITCODE). The packager asserts the two agree, so fix the notices file before releasing."
    }

    # From here to the manifest check below, every step writes to the tree, and
    # the last two of them can still fail. The catch puts the tree back rather
    # than stranding a stamped, unreleased, uncommitted working copy whose only
    # symptom next time is this script's own clean-tree precondition failing on
    # files the previous run wrote.
    #
    # CMakeLists.txt is canonical; the other two are hand-kept copies that drift
    # silently when skipped (install.cmd prints MOD_VERSION to the user). One
    # table, so the set that gets stamped and the set that gets committed cannot
    # disagree.
    $projectName = [regex]::Escape($project.Name)
    $stamps = @(
        @{
            Path        = 'CMakeLists.txt'
            Pattern     = "project\($projectName VERSION [0-9.]+"
            Replacement = "project($($project.Name) VERSION $new"
        }
        @{
            Path        = 'pixi.toml'
            Pattern     = '(?m)^version = "[0-9.]+"'
            Replacement = "version = `"$new`""
        }
        @{
            Path        = 'scripts/install.cmd'
            Pattern     = '(?m)^set "MOD_VERSION=[0-9.]+"'
            Replacement = "set `"MOD_VERSION=$new`""
        }
        @{
            # The launcher reads this to decide whether an update is available,
            # so a stale version here pins every user on the release that
            # shipped it.
            Path        = 'launcher-manifest.json'
            Pattern     = '(?m)^(\s*)"version": "[0-9.]+"'
            Replacement = "`$1`"version`": `"$new`""
        }
    )
    $writtenFiles = @($stamps | ForEach-Object { $_.Path }) + 'CHANGELOG.md' + 'THIRD-PARTY-NOTICES.md'
    try {
        foreach ($stamp in $stamps) {
            Update-VersionInFile -Path (Join-Path $project.Root $stamp.Path) -Pattern $stamp.Pattern -Replacement $stamp.Replacement
        }

        # Build and package through the same pixi chain CI runs (setup -> build
        # -> package), not a bare `cmake --build`, which fails outright on a
        # checkout where build/ was never configured.
        & pixi run package
        if ($LASTEXITCODE -ne 0) { throw 'Build/packaging failed' }

        # The launcher's contract, checked against the ZIP that is about to be
        # published rather than against the source tree: every files[].source has
        # to exist inside the archive. A mismatch here is invisible until
        # someone's launcher install fails, which is why the skill asks for it in
        # the release flow. The ZIP is already built, so this only runs the
        # validator.
        & node (Join-Path $project.Root 'cameraunlock-core/scripts/validate-manifest.mjs')
        if ($LASTEXITCODE -ne 0) { throw 'launcher-manifest.json does not validate against the packaged ZIP' }
    } catch {
        git checkout -- $writtenFiles
        throw
    }

    # The same set the rollback above covers: THIRD-PARTY-NOTICES.md is in it
    # because the sync can have rewritten it, and leaving that out commits a
    # release whose shipped attribution names a different core commit from the
    # one it was built against.
    $versionFiles = $writtenFiles
    git add -- $versionFiles
    if ($LASTEXITCODE -ne 0) { throw "git add failed for: $($versionFiles -join ', ')" }
    # Subject matched by build.yml, which skips the duplicate build of the
    # commit release.yml is about to build from the tag.
    git commit -m "Release v$new"
    if ($LASTEXITCODE -ne 0) { throw 'Failed to commit the version bump' }

    New-ReleaseTag -Version $new -Message "Release v$new"
    Write-Host "Released v$new" -ForegroundColor Green
} finally {
    Pop-Location
}
