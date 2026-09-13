#!/usr/bin/env pwsh
#Requires -Version 5.1
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 itsloopyo
# ============================================================================
# Tests for scripts/ModProject.psm1
# ============================================================================
# Run: pixi run test
#
# These lock the behaviour the release scripts used to spell out inline: the
# CMakeLists version read (three copies of one regex), and the install.cmd
# ASI_LOADER_NAME parse the dev deploy depends on. The check harness is in
# TestSupport.psm1.
# ============================================================================

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$repoRoot = Split-Path -Parent $PSScriptRoot
Import-Module (Join-Path $PSScriptRoot 'TestSupport.psm1') -Force
Import-Module (Join-Path $repoRoot 'scripts/ModProject.psm1') -Force

# The regex the three release scripts each carried before ModProject.psm1
# existed. Kept here so the shared read can be shown to agree with it.
function Get-LegacyCMakeVersion {
    param([string]$Path)
    $line = Select-String -LiteralPath $Path -List -Pattern 'project\(WolfensteinTheNewOrderHeadTracking\s+VERSION\s+([0-9]+\.[0-9]+\.[0-9]+)'
    if (-not $line) { return '' }
    return $line.Matches[0].Groups[1].Value
}

$sandbox = New-TestSandbox -Prefix 'wtno-modproject'
try {
    # --- project identity and layout ---------------------------------------

    $project = Get-ModProject

    Check 'Get-ModProject names the ASI stem the packager and deploy both stage' `
        ($project.Name -eq 'WolfensteinTheNewOrderHeadTracking') "got '$($project.Name)'"

    Check 'Get-ModProject uses the games.json id install.cmd resolves the game with' `
        ($project.Id -eq 'wolfenstein-the-new-order') "got '$($project.Id)'"

    Check 'Get-ModProject resolves the repo root, not scripts/' `
        (Test-Path -LiteralPath (Join-Path $project.Root 'pixi.toml')) "no pixi.toml under '$($project.Root)'"

    Check 'Get-ModProject derives the ASI filename from the mod name' `
        ($project.AsiFileName -eq 'WolfensteinTheNewOrderHeadTracking.asi') "got '$($project.AsiFileName)'"

    Check 'Get-ModProject points at the Release build output CMake produces' `
        ($project.BuildOutputPath -eq (Join-Path $project.Root 'build/Release/WolfensteinTheNewOrderHeadTracking.asi')) `
        "got '$($project.BuildOutputPath)'"

    Check 'Get-ModProject points at the committed vendored loader DLL' `
        ($project.VendorLoaderDll -eq (Join-Path $project.Root 'vendor/ultimate-asi-loader/dinput8.dll')) `
        "got '$($project.VendorLoaderDll)'"

    foreach ($name in @('CMakeListsPath', 'InstallCmdPath', 'UninstallCmdPath', 'ReleaseDir', 'VendorLoaderDir')) {
        Check "Get-ModProject returns an absolute path for $name" `
            ([System.IO.Path]::IsPathRooted($project.$name)) "got '$($project.$name)'"
    }

    # --- version read ------------------------------------------------------

    $cmake = Join-Path $sandbox 'CMakeLists.txt'
    Set-Content -LiteralPath $cmake -Value @(
        'cmake_minimum_required(VERSION 3.21)'
        'project(WolfensteinTheNewOrderHeadTracking VERSION 1.4.2 LANGUAGES CXX)'
        'add_library(WolfensteinTheNewOrderHeadTracking SHARED src/dllmain.cpp)'
    )

    $version = Get-ModVersion -CMakeListsPath $cmake
    $legacyVersion = Get-LegacyCMakeVersion $cmake

    Check 'Get-ModVersion reads project(... VERSION x.y.z ...)' ($version -eq '1.4.2') "got '$version'"

    Check 'Get-ModVersion agrees with the inline regex it replaced' `
        ($version -eq $legacyVersion) "shared read '$version' vs legacy '$legacyVersion'"

    $twoPart = Join-Path $sandbox 'two-part.txt'
    Set-Content -LiteralPath $twoPart -Value 'project(WolfensteinTheNewOrderHeadTracking VERSION 1.4 LANGUAGES CXX)'
    $err = Get-ThrownMessage { Get-ModVersion -CMakeListsPath $twoPart }
    Check 'Get-ModVersion rejects a version that is not X.Y.Z' ($err -like '*is not X.Y.Z*') "threw '$err'"

    $noVersion = Join-Path $sandbox 'no-version.txt'
    Set-Content -LiteralPath $noVersion -Value 'project(WolfensteinTheNewOrderHeadTracking LANGUAGES CXX)'
    $err = Get-ThrownMessage { Get-ModVersion -CMakeListsPath $noVersion }
    Check 'Get-ModVersion fails loudly when there is no project VERSION' `
        ($err -like '*VERSION*') "threw '$err'"

    $err = Get-ThrownMessage { Get-ModVersion -CMakeListsPath (Join-Path $sandbox 'absent.txt') }
    Check 'Get-ModVersion fails loudly when the file is missing' `
        ($err -like '*absent.txt*') "threw '$err'"

    # --- install.cmd proxy name --------------------------------------------

    $installCmd = Join-Path $sandbox 'install.cmd'
    Set-Content -LiteralPath $installCmd -Value @(
        '@echo off'
        'set "MOD_VERSION=1.4.2"'
        'set "ASI_LOADER_NAME=dinput8.dll"'
        'set "MOD_NAME=WolfensteinTheNewOrderHeadTracking"'
    )

    $proxy = Get-AsiLoaderProxyName -InstallCmdPath $installCmd
    Check 'Get-AsiLoaderProxyName reads the proxy install.cmd deploys' ($proxy -eq 'dinput8.dll') "got '$proxy'"

    $indented = Join-Path $sandbox 'indented.cmd'
    Set-Content -LiteralPath $indented -Value @('@echo off', '    set "ASI_LOADER_NAME=winmm.dll"')
    $indentedProxy = Get-AsiLoaderProxyName -InstallCmdPath $indented
    Check 'Get-AsiLoaderProxyName tolerates a leading-indented set' `
        ($indentedProxy -eq 'winmm.dll') "got '$indentedProxy'"

    $err = Get-ThrownMessage { Get-AsiLoaderProxyName -InstallCmdPath (Join-Path $sandbox 'absent.cmd') }
    Check 'Get-AsiLoaderProxyName fails loudly when install.cmd is missing' `
        ($err -like '*not found at*') "threw '$err'"

    $noSetting = Join-Path $sandbox 'no-setting.cmd'
    Set-Content -LiteralPath $noSetting -Value @('@echo off', 'set "MOD_VERSION=1.4.2"')
    $err = Get-ThrownMessage { Get-AsiLoaderProxyName -InstallCmdPath $noSetting }
    Check 'Get-AsiLoaderProxyName fails loudly when the setting is absent' `
        ($err -like '*no ASI_LOADER_NAME*') "threw '$err'"
} finally {
    Remove-Item -LiteralPath $sandbox -Recurse -Force -ErrorAction SilentlyContinue
}

exit (Complete-Checks)
