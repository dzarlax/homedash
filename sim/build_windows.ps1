param(
    [switch]$Run
)

$ErrorActionPreference = "Stop"

$RepoRoot = Resolve-Path (Join-Path $PSScriptRoot "..")
$BuildDir = Join-Path $PSScriptRoot "build\windows"
$LvglDir = Join-Path $RepoRoot ".pio\libdeps\esp32-s3-touch-lcd-7b\lvgl"
$LvglSrc = Join-Path $LvglDir "src"
$OutExe = Join-Path $BuildDir "homedash_sim.exe"
$Rsp = Join-Path $BuildDir "homedash_sim.rsp"

if (!(Test-Path $LvglSrc)) {
    throw "LVGL sources not found at $LvglSrc. Run the firmware PlatformIO build first."
}

$vcvarsCandidates = @(
    "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat",
    "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat",
    "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat",
    "C:\Program Files (x86)\Microsoft Visual Studio\2019\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
)

$VcVars = $vcvarsCandidates | Where-Object { Test-Path $_ } | Select-Object -First 1
if (!$VcVars) {
    throw "Visual Studio vcvars64.bat was not found. Install Visual Studio Build Tools with C++ tools."
}

New-Item -ItemType Directory -Force -Path $BuildDir | Out-Null

$lvglDirs = @("core", "display", "draw", "font", "indev", "layouts", "misc", "osal", "stdlib", "themes", "tick", "widgets", "libs\bin_decoder")
$lvglSources = foreach ($dir in $lvglDirs) {
    Get-ChildItem -Path (Join-Path $LvglSrc $dir) -Recurse -Filter "*.c" | ForEach-Object { $_.FullName }
}

$sources = @(
    (Join-Path $PSScriptRoot "main_sim.cpp"),
    (Join-Path $PSScriptRoot "stubs.cpp"),
    (Join-Path $RepoRoot "src\ui_dashboard.cpp"),
    (Join-Path $RepoRoot "src\weather_icons.cpp"),
    (Join-Path $RepoRoot "src\fonts\font_montserrat_16_cyr.c"),
    (Join-Path $RepoRoot "src\fonts\font_montserrat_24_cyr.c"),
    (Join-Path $LvglSrc "lv_init.c")
) + $lvglSources

$includeFlags = @(
    "/I`"$RepoRoot\src`"",
    "/I`"$PSScriptRoot\esp_stubs`"",
    "/I`"$LvglDir`"",
    "/I`"$LvglSrc`""
)

$defines = @(
    "/DLV_CONF_INCLUDE_SIMPLE",
    "/DLV_TICK_PERIOD_MS=2",
    "/DDISPLAY_WIDTH=1024",
    "/DDISPLAY_HEIGHT=600",
    "/DLV_COLOR_16_SWAP=0",
    "/DSIMULATOR=1",
    "/D_CRT_SECURE_NO_WARNINGS",
    "/DNOMINMAX",
    "/DWIN32_LEAN_AND_MEAN"
)

$rspLines = @(
    "/nologo",
    "/O2",
    "/MD",
    "/EHsc",
    "/std:c++17",
    "/utf-8",
    "/W3",
    "/FI`"$PSScriptRoot\esp_stubs\sim_compat.h`"",
    "/Fe`"$OutExe`"",
    "/Fo$BuildDir\"
) + $defines + $includeFlags + ($sources | ForEach-Object { "`"$_`"" }) + @(
    "user32.lib",
    "gdi32.lib"
)

Set-Content -Path $Rsp -Value $rspLines -Encoding ASCII

$cmd = "call `"$VcVars`" >nul && cl @`"$Rsp`""
cmd.exe /c $cmd
if ($LASTEXITCODE -ne 0) {
    throw "Simulator build failed with exit code $LASTEXITCODE"
}

Write-Host "Built $OutExe"

if ($Run) {
    & $OutExe
}
