#Requires -Version 5.1
<#
.SYNOPSIS
Configure and build the Windows x64 application with Visual Studio 2022.
.DESCRIPTION
Builds Release by default. Requires CMake 3.24+, the Visual Studio 2022
C++ desktop workload, and the Vulkan SDK. The first build downloads dependencies.
Output: build-host-x64/<Configuration>/emergences.exe and adjacent shaders/.
.PARAMETER Configuration
Release (default) or Debug.
.PARAMETER Test
Build all targets and run tests that do not require a GPU or desktop.
.PARAMETER Run
Start the application after a successful build and any requested tests.
.PARAMETER Jobs
Maximum parallel build jobs. Defaults to at most eight logical processors.
.EXAMPLE
.\build.ps1
.EXAMPLE
.\build.ps1 -Configuration Debug -Test
.EXAMPLE
.\build.ps1 -Run -Jobs 4
#>
[CmdletBinding()]
param(
    [ValidateSet('Release', 'Debug')]
    [string] $Configuration = 'Release',
    [switch] $Test,
    [switch] $Run,
    [ValidateRange(1, 1024)]
    [int] $Jobs = [Math]::Max(1, [Math]::Min(8, [Environment]::ProcessorCount))
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$script:BuildExitCode = 1

function Invoke-BuildCommand {
    param(
        [string] $FilePath,
        [string[]] $Arguments,
        [string] $WorkingDirectory = $PSScriptRoot
    )

    Write-Host "`n> $([IO.Path]::GetFileName($FilePath)) $($Arguments -join ' ')"
    $startInfo = New-Object System.Diagnostics.ProcessStartInfo
    $startInfo.FileName = $FilePath
    $startInfo.WorkingDirectory = $WorkingDirectory
    $startInfo.UseShellExecute = $false
    # All arguments below are internal, space-free tokens. File paths are passed
    # through FileName/WorkingDirectory, so paths with spaces need no shell quoting.
    $startInfo.Arguments = $Arguments -join ' '

    # Some launchers supply both Path and PATH. Collapse Windows environment key
    # variants, and use the API available in both Windows PowerShell 5.1 and PS 7.
    $startInfo.EnvironmentVariables.Clear()
    $processEnvironment = [Environment]::GetEnvironmentVariables('Process')
    foreach ($key in $processEnvironment.Keys) {
        $startInfo.EnvironmentVariables[$key] = [Environment]::GetEnvironmentVariable($key, 'Process')
    }
    $startInfo.EnvironmentVariables['MSBUILDDISABLENODEREUSE'] = '1'

    $process = [System.Diagnostics.Process]::Start($startInfo)
    try {
        $process.WaitForExit()
        if ($process.ExitCode -ne 0) {
            $script:BuildExitCode = $process.ExitCode
            throw "$([IO.Path]::GetFileName($FilePath)) failed (exit code $($process.ExitCode)). See output above."
        }
    } finally {
        $process.Dispose()
    }
}

try {
    if ([Environment]::OSVersion.Platform -ne [PlatformID]::Win32NT) {
        throw 'This script uses the Windows / Visual Studio 2022 preset.'
    }

    # On .NET Framework, duplicate environment names can make the first
    # ProcessStartInfo.EnvironmentVariables access return null. Repair only
    # duplicate names in this process, preserving their effective Windows value.
    if ($PSVersionTable.PSVersion.Major -le 5) {
        $environmentKeys = [Environment]::GetEnvironmentVariables('Process').Keys
        $duplicates = $environmentKeys | Group-Object { $_.ToUpperInvariant() } |
            Where-Object { $_.Count -gt 1 }
        foreach ($group in $duplicates) {
            $name = [string] $group.Group[0]
            $value = [Environment]::GetEnvironmentVariable($name, 'Process')
            foreach ($alias in $group.Group) {
                [Environment]::SetEnvironmentVariable([string] $alias, $null, 'Process')
            }
            [Environment]::SetEnvironmentVariable($name, $value, 'Process')
        }
    }

    $cmakeCommand = Get-Command cmake.exe -CommandType Application -ErrorAction SilentlyContinue
    if (-not $cmakeCommand) {
        throw 'CMake was not found. Install CMake 3.24 or newer, add its bin directory to PATH, and reopen the terminal.'
    }
    $cmake = $cmakeCommand.Source
    $ctest = Join-Path (Split-Path -Parent $cmake) 'ctest.exe'
    if ($Test -and -not (Test-Path -LiteralPath $ctest -PathType Leaf)) {
        throw "CTest was not found alongside CMake: $ctest. Repair the CMake installation."
    }

    # Refresh an older terminal after SDK installation, while respecting an
    # explicitly selected SDK in the current process.
    if (-not $env:VULKAN_SDK) {
        $env:VULKAN_SDK = [Environment]::GetEnvironmentVariable('VULKAN_SDK', 'User')
        if (-not $env:VULKAN_SDK) {
            $env:VULKAN_SDK = [Environment]::GetEnvironmentVariable('VULKAN_SDK', 'Machine')
        }
    }
    if (-not $env:VULKAN_SDK) {
        throw 'Install the LunarG Vulkan SDK and set VULKAN_SDK to its root directory. The GPU driver alone is not enough to build.'
    }
    foreach ($sdkFile in @('Include/vulkan/vulkan.h', 'Lib/vulkan-1.lib', 'Bin/glslc.exe')) {
        $sdkPath = Join-Path $env:VULKAN_SDK $sdkFile
        if (-not (Test-Path -LiteralPath $sdkPath -PathType Leaf)) {
            throw "Incomplete Vulkan SDK: $sdkPath is missing. Install the full x64 SDK or correct VULKAN_SDK."
        }
    }

    # Normalize casing so the output path and build preset agree.
    $Configuration = if ($Configuration -ieq 'Debug') { 'Debug' } else { 'Release' }
    $buildPreset = $Configuration.ToLowerInvariant()
    Write-Host "Building emergences ($Configuration, Windows x64)"
    Write-Host "Vulkan SDK: $env:VULKAN_SDK"
    Invoke-BuildCommand $cmake @('--preset', 'vs2022')

    $buildArguments = @('--build', '--preset', $buildPreset, '--parallel', "$Jobs")
    if (-not $Test) {
        $buildArguments += @('--target', 'emergences')
    }
    Invoke-BuildCommand $cmake $buildArguments

    if ($Test) {
        # GPU tests may already be enabled in the user's CMake cache. Keep this
        # switch usable without opening windows or requiring an active desktop.
        Invoke-BuildCommand $ctest @('--test-dir', 'build-host-x64', '-C', $Configuration,
            '--output-on-failure', '--no-tests=error', '-LE', 'gpu')
    }

    $outputDirectory = Join-Path $PSScriptRoot "build-host-x64/$Configuration"
    $executable = Join-Path $outputDirectory 'emergences.exe'
    if (-not (Test-Path -LiteralPath $executable -PathType Leaf)) {
        throw "Build finished but the executable is missing: $executable"
    }
    $configSample = Join-Path $outputDirectory 'emergences.conf.example'
    if (-not (Test-Path -LiteralPath $configSample -PathType Leaf)) {
        throw "Build finished but the configuration sample is missing: $configSample"
    }
    Write-Host "`nBuild succeeded: $executable" -ForegroundColor Green
    Write-Host "Keep the adjacent shaders directory with the executable: $outputDirectory\shaders"
    Write-Host "Include the annotated configuration sample: $configSample"
    if ($Run) {
        Invoke-BuildCommand -FilePath $executable -Arguments @() -WorkingDirectory $outputDirectory
    }
    exit 0
} catch {
    Write-Host "`nBuild failed: $($_.Exception.Message)" -ForegroundColor Red
    Write-Verbose $_.ScriptStackTrace
    exit $script:BuildExitCode
}
