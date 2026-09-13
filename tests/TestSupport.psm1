# SPDX-License-Identifier: MIT
# Copyright (c) 2026 itsloopyo

<#
.SYNOPSIS
    The check harness both test scripts run on.

.DESCRIPTION
    No Pester: the Windows runner ships Pester 3.4, which cannot run Pester 5
    syntax. This is the same shape as cameraunlock-core/powershell/tests, minus
    the copy-paste - each test script previously carried its own Check,
    Get-ThrownMessage, sandbox setup and summary block.
#>

Set-StrictMode -Version Latest

$script:Failures = 0

<#
.SYNOPSIS
    Records one assertion. $Detail is only read when the check fails, so keep
    anything expensive out of the argument.
#>
function Check {
    param([string]$Name, [bool]$Condition, [string]$Detail)
    if ($Condition) {
        Write-Host "PASS  $Name" -ForegroundColor Green
    } else {
        Write-Host "FAIL  $Name - $Detail" -ForegroundColor Red
        $script:Failures++
    }
}

<#
.SYNOPSIS
    Runs $Action and returns its error message, or '' when it completed.
.DESCRIPTION
    A string either way, so Check's detail never dereferences $null under
    Set-StrictMode.
#>
function Get-ThrownMessage {
    param([scriptblock]$Action)
    try { & $Action | Out-Null; return '' } catch { return $_.Exception.Message }
}

<#
.SYNOPSIS
    Creates a uniquely named scratch directory under the user's temp path.
.DESCRIPTION
    Fixtures never go in the repo. The caller removes it in a finally, so an
    assertion that throws mid-run does not leave the directory behind.
#>
function New-TestSandbox {
    [OutputType([string])]
    param([Parameter(Mandatory = $true)][string]$Prefix)

    $path = Join-Path ([System.IO.Path]::GetTempPath()) "$Prefix-$([guid]::NewGuid().ToString('N'))"
    New-Item -ItemType Directory -Path $path -Force | Out-Null
    return $path
}

<#
.SYNOPSIS
    Prints the run summary and returns the process exit code.
.OUTPUTS
    Int, 0 when every check passed.
#>
function Complete-Checks {
    [OutputType([int])]
    param()

    Write-Host ''
    if ($script:Failures -gt 0) {
        Write-Host "$($script:Failures) check(s) failed" -ForegroundColor Red
        return 1
    }
    Write-Host 'all checks passed' -ForegroundColor Green
    return 0
}

Export-ModuleMember -Function @(
    'Check',
    'Get-ThrownMessage',
    'New-TestSandbox',
    'Complete-Checks'
)
