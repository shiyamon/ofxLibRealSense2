# Builds librealsense for ofxLibRealSense2 on Windows: x64, Release, realsense2.dll with the
# native Windows (Media Foundation) backend - no libusb, no admin rights. Installs the headers
# and realsense2.lib / realsense2.dll into libs/librealsense2.
#
# One Release DLL serves Debug and Release apps: librealsense's C++ API (rs.hpp) is header-only
# on top of its C API, so no C++ types cross the DLL boundary. It also links the C runtime
# statically, so nothing else needs shipping.
#
# CUDA (-Cuda) is off by default: librealsense would only use it for colour format conversion
# (the addon does its own registration, align and point clouds, multi-threaded), and a CUDA
# build links cuBLAS / cuSPARSE, hundreds of MB of DLLs every app would have to ship.
#
# Keep the CMake options in sync with build_librealsense_macos.sh / _linux.sh.
#
# Requirements: Visual Studio 2022 or newer with the C++ workload (its CMake is used); for
# -Cuda, the CUDA Toolkit.
#
# Usage (PowerShell):
#   .\build_librealsense_windows.ps1 -Source C:\path\to\librealsense
#   .\build_librealsense_windows.ps1 -Source C:\path\to\librealsense -Cuda

param(
    [Parameter(Mandatory = $true)][string]$Source,
    [switch]$Cuda,
    # librealsense's own list leaves out desktop Maxwell / Pascal (e.g. GTX 1080 Ti, sm_61)
    [string[]]$ExtraCudaArchs = @("52", "61")
)

$ErrorActionPreference = "Continue"
$Source = (Resolve-Path $Source).Path
$Addon = (Resolve-Path "$PSScriptRoot\..").Path
$build = "$Source\build_ofx"

$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vs) { throw "Visual Studio with the C++ workload not found" }
Import-Module "$vs\Common7\Tools\Microsoft.VisualStudio.DevShell.dll"
Enter-VsDevShell -VsInstallPath $vs -SkipAutomaticLocation -DevCmdArguments "-arch=x64 -host_arch=x64" | Out-Null
$cmake = "$vs\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
Write-Host "Visual Studio: $vs"

$cmakeArgs = @(
    "-S", $Source, "-B", $build,
    "-G", "Ninja",
    "-DCMAKE_BUILD_TYPE=Release",
    "-DBUILD_SHARED_LIBS=ON",
    "-DBUILD_WITH_STATIC_CRT=ON",
    "-DFORCE_RSUSB_BACKEND=OFF",
    "-DBUILD_EXAMPLES=OFF",
    "-DBUILD_GRAPHICAL_EXAMPLES=OFF",
    "-DBUILD_GLSL_EXTENSIONS=OFF",
    "-DBUILD_TOOLS=OFF",
    "-DBUILD_UNIT_TESTS=OFF",
    "-DBUILD_WITH_OPENMP=OFF",
    "-DCHECK_FOR_UPDATES=OFF",
    "-DENABLE_STATS=OFF",
    "-DENABLE_CCACHE=OFF",
    "-DBUILD_WITH_DDS=OFF",
    "-DBUILD_ROSBAG2=OFF"
)
if ($Cuda) {
    $gencode = ($ExtraCudaArchs | ForEach-Object { "-gencode arch=compute_$_,code=sm_$_" }) -join " "
    $cmakeArgs += @("-DBUILD_WITH_CUDA=ON", "-DCMAKE_CUDA_FLAGS=$gencode")
} else {
    $cmakeArgs += "-DBUILD_WITH_CUDA=OFF"
}

Remove-Item "$build\CMakeCache.txt" -ErrorAction SilentlyContinue
& $cmake @cmakeArgs
if ($LASTEXITCODE -ne 0) { throw "CMake configure failed" }
& $cmake --build $build --target realsense2 --parallel
if ($LASTEXITCODE -ne 0) { throw "build failed" }

$incOut = "$Addon\libs\librealsense2\include"
$libOut = "$Addon\libs\librealsense2\lib\vs\x64"
Remove-Item -Recurse -Force $incOut, $libOut -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force $incOut, $libOut | Out-Null
Copy-Item -Recurse "$Source\include\librealsense2" "$incOut\"
$dll = Get-ChildItem -Recurse "$build" -Filter realsense2.dll | Select-Object -First 1
$lib = Get-ChildItem -Recurse "$build" -Filter realsense2.lib | Select-Object -First 1
Copy-Item $dll.FullName, $lib.FullName $libOut

Write-Host ""
Write-Host "librealsense $(git -C $Source describe --tags --always) installed into $Addon\libs\librealsense2 (vs x64$(if ($Cuda) { ', CUDA' }))"
Get-ChildItem $libOut | ForEach-Object { Write-Host "  $($_.Name)  $([math]::Round($_.Length / 1MB, 1)) MB" }
