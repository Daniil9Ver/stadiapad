# Builds bin\StadiaPad.exe (x64, Release, static CRT) with Visual Studio Build Tools.
# Usage:  powershell -ExecutionPolicy Bypass -File build.ps1
$ErrorActionPreference = 'Stop'
Set-Location $PSScriptRoot

$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vs) { throw 'Visual Studio Build Tools (C++) not found' }
$vcvars = Join-Path $vs 'VC\Auxiliary\Build\vcvars64.bat'

New-Item -ItemType Directory -Force bin, obj | Out-Null

$cflags = '/nologo /O2 /GL /MT /W3 /wd4828 /EHsc /std:c++17 /utf-8 /DWIN32 /DNDEBUG /D_UNICODE /DUNICODE /D_CRT_SECURE_NO_WARNINGS ' +
          '/Iinclude /Ithird_party\ViGEmClient\include /Ithird_party\ViGEmClient\src'
$sources = 'src\*.c third_party\ViGEmClient\src\ViGEmClient.cpp'
$libs = 'user32.lib shell32.lib advapi32.lib setupapi.lib hid.lib cfgmgr32.lib ole32.lib'

$cmd = "`"$vcvars`" >nul && " +
       "rc /nologo /fo obj\res.res res\res.rc && " +
       "cl $cflags /Foobj\ $sources obj\res.res /Febin\StadiaPad.exe /link /LTCG /SUBSYSTEM:WINDOWS $libs"
cmd /c $cmd
if ($LASTEXITCODE -ne 0) { throw "Build failed ($LASTEXITCODE)" }
Get-Item bin\StadiaPad.exe | Select-Object FullName, Length



