#Requires -Version 5.1
<#
.SYNOPSIS
    Adds PlatformIO Core (pio.exe) to the Windows user PATH.
.DESCRIPTION
    Searches common PlatformIO installation locations and appends the
    directory containing pio.exe to the current user's PATH environment
    variable. It also refreshes the PATH in the current PowerShell session.
.NOTES
    Run this script in PowerShell as a normal user (no admin required).
    If you installed PlatformIO in a custom location, update $SearchPaths.
#>

$ErrorActionPreference = "Stop"

# Common locations where pio.exe lives on Windows.
$SearchPaths = @(
    "$env:USERPROFILE\.platformio\penv\Scripts"
    "$env:USERPROFILE\.platformio\penv\bin"
    "$env:APPDATA\Python\Python3*\Scripts"
    "$env:LOCALAPPDATA\Programs\Python\Python3*\Scripts"
)

function Find-PioDirectory {
    foreach ($pattern in $SearchPaths) {
        # Resolve wildcards (e.g. Python3*)
        $resolved = Resolve-Path -Path $pattern -ErrorAction SilentlyContinue |
                    Select-Object -ExpandProperty Path

        foreach ($dir in $resolved) {
            $pioPath = Join-Path $dir "pio.exe"
            if (Test-Path -Path $pioPath -PathType Leaf) {
                return $dir
            }
        }
    }
    return $null
}

function Add-ToUserPath {
    param(
        [Parameter(Mandatory)]
        [string]$Directory
    )

    $target = "User"
    $currentPath = [Environment]::GetEnvironmentVariable("Path", $target)

    # Normalize by splitting and trimming trailing backslashes.
    $pathEntries = $currentPath -split ";" | ForEach-Object { $_.TrimEnd("\") }
    $normalizedDir = $Directory.TrimEnd("\")

    if ($pathEntries -contains $normalizedDir) {
        Write-Host "PlatformIO is already in your user PATH: $Directory" -ForegroundColor Cyan
        return
    }

    $newPath = if ($currentPath) {
        "$currentPath;$Directory"
    } else {
        $Directory
    }

    [Environment]::SetEnvironmentVariable("Path", $newPath, $target)
    Write-Host "Added PlatformIO to your user PATH: $Directory" -ForegroundColor Green
}

# --- Main ---

$pioDir = Find-PioDirectory

if (-not $pioDir) {
    Write-Host "ERROR: Could not find pio.exe in any of the expected locations." -ForegroundColor Red
    Write-Host "Expected locations searched:" -ForegroundColor Yellow
    $SearchPaths | ForEach-Object { Write-Host "  $_" -ForegroundColor Yellow }
    Write-Host "`nIf you installed PlatformIO elsewhere, edit the `$SearchPaths variable in this script." -ForegroundColor Yellow
    exit 1
}

Write-Host "Found PlatformIO at: $pioDir\pio.exe" -ForegroundColor Cyan

Add-ToUserPath -Directory $pioDir

# Refresh PATH in the current session so pio works immediately.
$env:Path = [Environment]::GetEnvironmentVariable("Path", "User") + ";" + [Environment]::GetEnvironmentVariable("Path", "Machine")

# Verify.
try {
    $pioVersion = & "$pioDir\pio.exe" --version 2>&1
    Write-Host "Verified: $pioVersion" -ForegroundColor Green
    Write-Host "`nYou can now run 'pio' from this PowerShell window." -ForegroundColor Green
    Write-Host "If you open a new terminal and 'pio' is not found, restart it." -ForegroundColor DarkGray
} catch {
    Write-Host "WARN: Added to PATH but could not verify pio.exe execution." -ForegroundColor Yellow
    Write-Host $_.Exception.Message -ForegroundColor Yellow
}
