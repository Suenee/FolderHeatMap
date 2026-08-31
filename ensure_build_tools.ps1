$ErrorActionPreference = 'Stop'

$Revision = '1.52-build-tools-bootstrap'
$VsWorkload = 'Microsoft.VisualStudio.Workload.VCTools'
$VsCMakeComponent = 'Microsoft.VisualStudio.Component.VC.CMake.Project'
$Vs2022WingetId = 'Microsoft.VisualStudio.2022.BuildTools'

function Write-Info([string]$Text) { Write-Host $Text -ForegroundColor Gray }
function Write-Warn([string]$Text) { Write-Host ('WARNING: ' + $Text) -ForegroundColor Yellow }
function Write-Fail([string]$Text) { Write-Host ('ERROR: ' + $Text) -ForegroundColor Red }

function Get-VsWhere {
    $candidate = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (Test-Path -LiteralPath $candidate) { return $candidate }
    return $null
}

function Get-BuildEnvironment {
    $cmakeCommand = Get-Command cmake.exe -ErrorAction SilentlyContinue
    if ($cmakeCommand) {
        return [pscustomobject]@{ Ready=$true; CMake=$cmakeCommand.Source; InstallPath=$null; VsWhere=$null }
    }

    $vswhere = Get-VsWhere
    if ($vswhere) {
        $cmake = & $vswhere -latest -products '*' -requires $VsCMakeComponent -find 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe' 2>$null | Select-Object -First 1
        if ($cmake -and (Test-Path -LiteralPath $cmake)) {
            return [pscustomobject]@{ Ready=$true; CMake=[string]$cmake; InstallPath=$null; VsWhere=$vswhere }
        }
        $installPath = & $vswhere -latest -products '*' -property installationPath 2>$null | Select-Object -First 1
        if ($installPath) {
            return [pscustomobject]@{ Ready=$false; CMake=$null; InstallPath=[string]$installPath; VsWhere=$vswhere }
        }
    }
    return [pscustomobject]@{ Ready=$false; CMake=$null; InstallPath=$null; VsWhere=$vswhere }
}

function Invoke-External {
    param([Parameter(Mandatory=$true)][string]$Exe,[Parameter(Mandatory=$true)][string[]]$Arguments)
    & $Exe @Arguments
    return $LASTEXITCODE
}

Write-Info ("[DEPENDENCIES] Build-tools bootstrap $Revision")
$state = Get-BuildEnvironment
if ($state.Ready) {
    Write-Info ("[DEPENDENCIES] C++/CMake build environment already available: $($state.CMake)")
    exit 0
}

if ($state.InstallPath) {
    $setup = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\setup.exe'
    if (-not (Test-Path -LiteralPath $setup)) {
        Write-Fail "Visual Studio is installed at '$($state.InstallPath)', but Visual Studio Installer setup.exe was not found."
        exit 11
    }
    Write-Warn 'Visual Studio/Build Tools exists, but the required C++/CMake workload is missing. Adding it automatically.'
    Write-Info '[DEPENDENCIES] Windows may request administrator approval.'
    $rc = Invoke-External -Exe $setup -Arguments @('modify','--installPath',$state.InstallPath,'--add',$VsWorkload,'--includeRecommended','--passive','--norestart')
    if ($rc -ne 0 -and $rc -ne 3010) {
        Write-Fail "Visual Studio Installer failed while adding the C++ workload (exit code $rc)."
        exit $rc
    }
} else {
    $winget = Get-Command winget.exe -ErrorAction SilentlyContinue
    if (-not $winget) {
        Write-Fail 'CMake/Visual Studio Build Tools are missing and winget is not available for automatic installation. Install Windows App Installer/winget, then run upgrade.cmd again.'
        exit 12
    }
    Write-Warn 'Visual Studio 2022 Build Tools with C++/CMake support are missing. Installing them automatically.'
    Write-Info '[DEPENDENCIES] Windows may request administrator approval. The installation can take several minutes.'
    $override = "--passive --wait --norestart --add $VsWorkload --includeRecommended"
    $rc = Invoke-External -Exe $winget.Source -Arguments @('install','--exact','--id',$Vs2022WingetId,'--source','winget','--accept-source-agreements','--accept-package-agreements','--override',$override)
    if ($rc -ne 0) {
        Write-Fail "winget could not install Visual Studio 2022 Build Tools (exit code $rc)."
        exit $rc
    }
}

$state = Get-BuildEnvironment
if (-not $state.Ready) {
    Write-Fail 'The dependency installer completed, but CMake with the Visual C++ build environment is still unavailable. Review the Visual Studio Installer result and run upgrade.cmd again.'
    exit 13
}

Write-Host ("[DEPENDENCIES] C++/CMake build environment installed and verified: $($state.CMake)") -ForegroundColor Green
exit 0
