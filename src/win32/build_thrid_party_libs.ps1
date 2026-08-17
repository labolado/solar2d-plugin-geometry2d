# Builds the WebM decode stack (libvpx, libwebm, opus) as 32-bit (Win32) static
# libraries for the Solar2D Windows plugin, into src\win32\third_party_libs\:
#   third_party_libs\opus.lib
#   third_party_libs\webm.lib
#   third_party_libs\vpx.lib
#
# opus + libwebm build directly via CMake's Visual Studio generator. libvpx has
# no CMake build; on Windows it is built through its own configure under MSYS2,
# which generates a Visual Studio solution — this script drives that if an MSYS2
# bash is found, otherwise it prints the manual steps.
#
# Everything is /MD (MultiThreadedDLL) to match Plugin.vcxproj.
#
# Prereqs: Visual Studio 2022 (Win32 toolset), CMake, and (for libvpx) MSYS2
# with yasm/nasm + perl. Run from a "x86 Native Tools Command Prompt" or any
# shell where cmake + msbuild are on PATH.  UNVERIFIED on this machine (authored
# on macOS); expect to adjust generator/toolset/paths for your setup.

$ErrorActionPreference = "Stop"
$root       = Split-Path -Parent $MyInvocation.MyCommand.Path
$thirdParty = Resolve-Path (Join-Path $root "..\..\third_party")
$libvpxDir  = Join-Path $thirdParty "libvpx"
$libwebmDir = Join-Path $thirdParty "libwebm"
$opusDir    = Join-Path $thirdParty "opus"
$out        = Join-Path $root "third_party_libs"
$generator  = "Visual Studio 17 2022"   # change for your VS (e.g. "Visual Studio 16 2019")

New-Item -ItemType Directory -Force -Path $out | Out-Null

function Build-CMakeLib($proj, $builtRelPath, $destName, $extraArgs) {
    $bdir = Join-Path $proj "build_win32"
    if (Test-Path $bdir) { Remove-Item -Recurse -Force $bdir }
    New-Item -ItemType Directory -Force -Path $bdir | Out-Null
    & cmake -S $proj -B $bdir -G $generator -A Win32 `
        -DBUILD_SHARED_LIBS=OFF `
        -DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreadedDLL `
        @extraArgs
    if ($LASTEXITCODE -ne 0) { throw "cmake configure failed for $proj" }
    & cmake --build $bdir --config Release
    if ($LASTEXITCODE -ne 0) { throw "cmake build failed for $proj" }
    Copy-Item (Join-Path $bdir $builtRelPath) (Join-Path $out $destName) -Force
    Write-Host "  -> $out\$destName"
}

Write-Host "==== opus ===="
Build-CMakeLib $opusDir "Release\opus.lib" "opus.lib" @(
    "-DOPUS_BUILD_PROGRAMS=OFF", "-DOPUS_DISABLE_INTRINSICS=ON")

Write-Host "==== libwebm ===="
Build-CMakeLib $libwebmDir "Release\webm.lib" "webm.lib" @(
    "-DENABLE_WEBMTS=OFF", "-DENABLE_WEBMINFO=OFF",
    "-DENABLE_TESTS=OFF", "-DENABLE_SAMPLE_PROGRAMS=OFF")

Write-Host "==== libvpx ===="
$msysBash = @("C:\msys64\usr\bin\bash.exe", "C:\msys32\usr\bin\bash.exe") |
    Where-Object { Test-Path $_ } | Select-Object -First 1
if ($msysBash) {
    $vpxBuild = Join-Path $libvpxDir "build_win32"
    if (Test-Path $vpxBuild) { Remove-Item -Recurse -Force $vpxBuild }
    New-Item -ItemType Directory -Force -Path $vpxBuild | Out-Null
    # libvpx configure must run under MSYS2. --target=x86-win32-vs17 generates a
    # VS solution, but vpx.sln is created (and built) by `make`, NOT by configure
    # (see solution.mk: `vpx.sln:` is a make target) — so run make, not a bare
    # `msbuild vpx.sln`. make needs msbuild + cl on PATH: the caller sets up vcvars
    # (x86) and passes MSYS2_PATH_TYPE=inherit so the MSYS2 shell sees them.
    # No --enable-static-msvcrt: that forces /MT (vpxmt.lib); we want /MD
    # (vpxmd.lib) to match opus/webm and Plugin.vcxproj (MultiThreadedDLL).
    $cfg = @"
cd "`$(cygpath -u '$vpxBuild')"
"`$(cygpath -u '$libvpxDir')/configure" --target=x86-win32-vs17 \
    --disable-examples --disable-tools --disable-docs --disable-unit-tests \
    --disable-webm-io --disable-libyuv \
    --enable-vp8 --enable-vp9 --disable-vp8-encoder --disable-vp9-encoder
make
"@
    & $msysBash -lc $cfg
    if ($LASTEXITCODE -ne 0) { throw "libvpx configure/make (msys2) failed" }
    $vpxLib = Get-ChildItem -Path $vpxBuild -Recurse -Filter "vpxmd.lib" |
        Select-Object -First 1
    # If make generated the solution but couldn't find msbuild (MSBUILD_TOOL not on
    # PATH), build vpx.sln here from PowerShell where vcvars is set. Put the MSYS2
    # bin dir on PATH first so the .vcxproj's yasm custom-build step still resolves.
    if (-not $vpxLib) {
        $sln = Get-ChildItem -Path $vpxBuild -Recurse -Filter "vpx.sln" | Select-Object -First 1
        if ($sln) {
            $env:PATH = "$(Split-Path $msysBash);$env:PATH"
            & msbuild $sln.FullName /p:Configuration=Release /p:Platform=Win32
            $vpxLib = Get-ChildItem -Path $vpxBuild -Recurse -Filter "vpxmd.lib" | Select-Object -First 1
        }
    }
    if (-not $vpxLib) { $vpxLib = Get-ChildItem -Path $vpxBuild -Recurse -Filter "vpx.lib" | Select-Object -First 1 }
    if (-not $vpxLib) {
        Write-Host "build_win32 contents:"; Get-ChildItem -Path $vpxBuild -Recurse -Filter "*.lib" | Select-Object -ExpandProperty FullName
        throw "could not find built vpx lib"
    }
    Copy-Item $vpxLib.FullName (Join-Path $out "vpx.lib") -Force
    Write-Host "  -> $out\vpx.lib"
} else {
    Write-Warning @"
MSYS2 not found. Build libvpx manually, then copy the /MD lib to third_party_libs\vpx.lib:

  # in an MSYS2 shell launched from an x86 Native Tools Command Prompt (so msbuild
  # + cl are on PATH), with yasm/nasm + perl available:
  mkdir -p build_win32 && cd build_win32
  ../../third_party/libvpx/configure --target=x86-win32-vs17 \
      --disable-examples --disable-tools --disable-docs --disable-unit-tests \
      --disable-webm-io --disable-libyuv --enable-vp8 --enable-vp9 \
      --disable-vp8-encoder --disable-vp9-encoder
  make   # generates AND builds vpx.sln -> Win32\Release\vpxmd.lib
  copy <build>\Win32\Release\vpxmd.lib  src\win32\third_party_libs\vpx.lib
"@
}

Write-Host ""
Write-Host "NOTE: also place OpenAL32.lib (from OpenAL-Soft, Win32) on the linker"
Write-Host "path, or add its folder to the project's Additional Library Directories."
Write-Host "third_party_libs:"; Get-ChildItem $out
