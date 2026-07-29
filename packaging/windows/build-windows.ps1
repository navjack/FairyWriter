# SPDX-FileCopyrightText: 2026 Jack Mangano
# SPDX-License-Identifier: GPL-3.0-or-later

[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$QtPrefix,

    [string]$VcpkgRoot = $env:VCPKG_ROOT,

    [string]$SnesrecompRoot,

    [string]$Generator = "Visual Studio 17 2022"
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$SourceRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$Version = (Get-Content -LiteralPath (Join-Path $SourceRoot "VERSION") -Raw).Trim()
if ($Version -notmatch '^(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)$') {
    throw "VERSION must contain one MAJOR.MINOR.PATCH number."
}
if ([string]::IsNullOrWhiteSpace($VcpkgRoot)) {
    throw "Pass -VcpkgRoot or set VCPKG_ROOT."
}
if ([string]::IsNullOrWhiteSpace($SnesrecompRoot)) {
    $SnesrecompRoot = (Join-Path $SourceRoot "third_party\snesrecomp")
}
$QtPrefix = (Resolve-Path $QtPrefix).Path
$VcpkgRoot = (Resolve-Path $VcpkgRoot).Path
$SnesrecompRoot = (Resolve-Path $SnesrecompRoot).Path

$QtConfig = Join-Path $QtPrefix "lib\cmake\Qt6\Qt6Config.cmake"
$WinDeployQt = Join-Path $QtPrefix "bin\windeployqt.exe"
$Vcpkg = Join-Path $VcpkgRoot "vcpkg.exe"
$Toolchain = Join-Path $VcpkgRoot "scripts\buildsystems\vcpkg.cmake"
$SnesInterpreter = Join-Path $SnesrecompRoot "runner\src\snes\interp816.c"
foreach ($Required in @($QtConfig, $WinDeployQt, $Vcpkg, $Toolchain, $SnesInterpreter)) {
    if (-not (Test-Path -LiteralPath $Required)) {
        throw "Required Windows build input is missing: $Required"
    }
}

$BuildRoot = Join-Path $SourceRoot "build-windows-x64"
$ReleaseRoot = Join-Path $SourceRoot "release-windows-x64"
$StageRoot = Join-Path $ReleaseRoot "FairyWriter-$Version-windows-x64"
$Archive = Join-Path $ReleaseRoot "FairyWriter-$Version-windows-x64.zip"

& $Vcpkg install "zlib:x64-windows"
if ($LASTEXITCODE -ne 0) { throw "vcpkg failed to install zlib:x64-windows" }

& cmake -S $SourceRoot -B $BuildRoot -G $Generator -A x64 `
    "-DCMAKE_TOOLCHAIN_FILE=$Toolchain" `
    "-DVCPKG_TARGET_TRIPLET=x64-windows" `
    "-DCMAKE_PREFIX_PATH=$QtPrefix" `
    "-DFAIRYWRITER_SNESRECOMP_ROOT=$SnesrecompRoot" `
    "-DFAIRYWRITER_DEVELOPMENT_UI=OFF" `
    "-DFAIRYWRITER_DEVELOPER_TOOLS=OFF" `
    "-DBUILD_TESTING=ON"
if ($LASTEXITCODE -ne 0) { throw "CMake configure failed" }

& cmake --build $BuildRoot --config Release --parallel
if ($LASTEXITCODE -ne 0) { throw "Windows Release build failed" }

& ctest --test-dir $BuildRoot -C Release --output-on-failure
if ($LASTEXITCODE -ne 0) { throw "Windows production test gate failed" }

if (Test-Path -LiteralPath $StageRoot) {
    Remove-Item -LiteralPath $StageRoot -Recurse -Force
}
New-Item -ItemType Directory -Path $StageRoot -Force | Out-Null
& cmake --install $BuildRoot --config Release --prefix $StageRoot
if ($LASTEXITCODE -ne 0) { throw "Windows staging install failed" }

$Executable = Join-Path $StageRoot "fairywriter.exe"
if (-not (Test-Path -LiteralPath $Executable)) {
    throw "Staged tester executable is missing: $Executable"
}
& $WinDeployQt --release --compiler-runtime --no-translations $Executable
if ($LASTEXITCODE -ne 0) { throw "windeployqt failed" }

Copy-Item -LiteralPath (Join-Path $SourceRoot "COPYING") -Destination $StageRoot
Copy-Item -LiteralPath (Join-Path $SourceRoot "third_party\\cmark-gfm\\COPYING") `
    -Destination (Join-Path $StageRoot "CMARK-GFM-LICENSE")
Copy-Item -LiteralPath (Join-Path $SourceRoot "TESTING.md") -Destination $StageRoot

$VersionInfo = (Get-Item -LiteralPath $Executable).VersionInfo
if ($VersionInfo.ProductName -ne "FairyWriter" -or
        $VersionInfo.ProductVersion -ne $Version) {
    throw "Staged tester executable metadata reported '$($VersionInfo.ProductName) $($VersionInfo.ProductVersion)', expected 'FairyWriter $Version'"
}
$Forbidden = Get-ChildItem -LiteralPath $StageRoot -Recurse -File |
    Where-Object { $_.Extension -in @(".sfc", ".smc") -or $_.Name -eq "SCRATCH.MD" }
if ($Forbidden) {
    throw "Loose ROM or scratch data was found in the Windows tester staging tree"
}

if (Test-Path -LiteralPath $Archive) {
    Remove-Item -LiteralPath $Archive -Force
}
Compress-Archive -LiteralPath $StageRoot -DestinationPath $Archive -CompressionLevel Optimal
$Hash = (Get-FileHash -LiteralPath $Archive -Algorithm SHA256).Hash.ToLowerInvariant()
[IO.File]::WriteAllText("$Archive.sha256", "$Hash  $([IO.Path]::GetFileName($Archive))`n")
Write-Host "Created $Archive"
