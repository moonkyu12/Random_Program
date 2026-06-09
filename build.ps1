param(
    [string]$Output = "SchoolRandomProgram.exe"
)

$ErrorActionPreference = "Stop"
$ProjectDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$WebViewSource = Join-Path $ProjectDir "school_random_program_webview.cpp"
$CSource = Join-Path $ProjectDir "school_random_program.c"
$Source = if (Test-Path $WebViewSource) { $WebViewSource } else { $CSource }
$OutPath = Join-Path $ProjectDir $Output
$WebViewPackageVersion = "1.0.2903.40"
$WebViewPackageDir = Join-Path $ProjectDir "packages\microsoft.web.webview2\$WebViewPackageVersion"
$WebViewIncludeDir = Join-Path $WebViewPackageDir "build\native\include"
$WebViewLibDir = Join-Path $WebViewPackageDir "build\native\x64"
$WebViewLoaderDll = Join-Path $WebViewPackageDir "build\native\x64\WebView2Loader.dll"

if (-not (Test-Path $Source)) {
    throw "Source file not found: $Source"
}

function Ensure-WebView2Package {
    if ($Source -ne $WebViewSource) {
        return
    }
    if ((Test-Path (Join-Path $WebViewIncludeDir "WebView2.h")) -and
        (Test-Path (Join-Path $WebViewLibDir "WebView2LoaderStatic.lib"))) {
        return
    }

    $restoreProject = Join-Path $ProjectDir "webview2_restore.csproj"
    if (-not (Test-Path $restoreProject)) {
        throw "WebView2 restore project not found: $restoreProject"
    }

    Write-Host "Restoring WebView2 SDK package..."
    & dotnet restore $restoreProject --packages (Join-Path $ProjectDir "packages")
    if ($LASTEXITCODE -ne 0) {
        throw "WebView2 package restore failed with exit code $LASTEXITCODE"
    }
}

function Find-CommandPath {
    param([string]$Name)
    $cmd = Get-Command $Name -ErrorAction SilentlyContinue
    if ($cmd) {
        return $cmd.Source
    }
    return $null
}

function Find-VsDevCmd {
    $vswherePaths = @(
        "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe",
        "$env:ProgramFiles\Microsoft Visual Studio\Installer\vswhere.exe"
    )

    foreach ($path in $vswherePaths) {
        if (Test-Path $path) {
            $installPath = & $path -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
            if ($installPath) {
                $candidate = Join-Path $installPath "Common7\Tools\VsDevCmd.bat"
                if (Test-Path $candidate) {
                    return $candidate
                }
            }
        }
    }

    $commonRoots = @(
        "${env:ProgramFiles}\Microsoft Visual Studio\2022",
        "${env:ProgramFiles(x86)}\Microsoft Visual Studio\2022",
        "${env:ProgramFiles}\Microsoft Visual Studio\2019",
        "${env:ProgramFiles(x86)}\Microsoft Visual Studio\2019"
    )

    foreach ($root in $commonRoots) {
        if (-not (Test-Path $root)) {
            continue
        }
        $candidate = Get-ChildItem -Path $root -Filter "VsDevCmd.bat" -Recurse -ErrorAction SilentlyContinue |
            Select-Object -First 1 -ExpandProperty FullName
        if ($candidate) {
            return $candidate
        }
    }

    return $null
}

function Invoke-Compiler {
    param(
        [string]$Compiler,
        [string[]]$Arguments
    )

    Write-Host "Compiler: $Compiler"
    Write-Host "Output:   $OutPath"
    & $Compiler @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "Compilation failed with exit code $LASTEXITCODE"
    }
}

Ensure-WebView2Package

$cl = Find-CommandPath "cl.exe"
if ($cl) {
    if ($Source -eq $WebViewSource) {
        Invoke-Compiler $cl @(
            "/nologo",
            "/W4",
            "/O2",
            "/EHsc",
            "/std:c++17",
            "/D_CRT_SECURE_NO_WARNINGS",
            "/I$WebViewIncludeDir",
            $Source,
            "/Fe:$OutPath",
            "/link",
            "/LIBPATH:$WebViewLibDir",
            "WebView2LoaderStatic.lib",
            "winhttp.lib",
            "ws2_32.lib",
            "shell32.lib",
            "ole32.lib",
            "user32.lib",
            "gdi32.lib",
            "advapi32.lib"
        )
    } else {
        Invoke-Compiler $cl @(
            "/nologo",
            "/W4",
            "/O2",
            "/TC",
            "/D_CRT_SECURE_NO_WARNINGS",
            $Source,
            "/Fe:$OutPath",
            "/link",
            "winhttp.lib",
            "ws2_32.lib",
            "shell32.lib"
        )
    }
    Write-Host "Build complete: $OutPath"
    exit 0
}

$vsDevCmd = Find-VsDevCmd
if ($vsDevCmd) {
    Write-Host "Compiler: MSVC via VsDevCmd"
    Write-Host "Output:   $OutPath"
    if ($Source -eq $WebViewSource) {
        $cmdLine = "`"$vsDevCmd`" -arch=x64 -host_arch=x64 && cl.exe /nologo /W4 /O2 /EHsc /std:c++17 /D_CRT_SECURE_NO_WARNINGS /I`"$WebViewIncludeDir`" `"$Source`" /Fe:`"$OutPath`" /link /LIBPATH:`"$WebViewLibDir`" WebView2LoaderStatic.lib winhttp.lib ws2_32.lib shell32.lib ole32.lib user32.lib gdi32.lib advapi32.lib"
    } else {
        $cmdLine = "`"$vsDevCmd`" -arch=x64 -host_arch=x64 && cl.exe /nologo /W4 /O2 /TC /D_CRT_SECURE_NO_WARNINGS `"$Source`" /Fe:`"$OutPath`" /link winhttp.lib ws2_32.lib shell32.lib"
    }
    & cmd.exe /d /s /c $cmdLine
    if ($LASTEXITCODE -ne 0) {
        throw "Compilation failed with exit code $LASTEXITCODE"
    }
    Write-Host "Build complete: $OutPath"
    exit 0
}

$clang = Find-CommandPath "clang.exe"
if ($clang) {
    $clangArgs = @(
        "-Wall",
        "-Wextra",
        "-O2",
        $Source,
        "-o",
        $OutPath,
        "-lwinhttp",
        "-lws2_32",
        "-lshell32"
    )
    if ($Source -eq $WebViewSource) {
        $clangArgs = @(
            "-Wall",
            "-Wextra",
            "-O2",
            "-std=c++17",
            "-I$WebViewIncludeDir",
            $Source,
            "-o",
            $OutPath,
            "-L$WebViewLibDir",
            "-lWebView2LoaderStatic",
            "-lwinhttp",
            "-lws2_32",
            "-lshell32",
            "-lole32",
            "-luser32",
            "-lgdi32",
            "-ladvapi32"
        )
    }
    Invoke-Compiler $clang $clangArgs
    Write-Host "Build complete: $OutPath"
    exit 0
}

$gcc = Find-CommandPath "gcc.exe"
if ($gcc) {
    $gccArgs = @(
        "-Wall",
        "-Wextra",
        "-O2",
        $Source,
        "-o",
        $OutPath,
        "-lwinhttp",
        "-lws2_32",
        "-lshell32"
    )
    if ($Source -eq $WebViewSource) {
        $gccArgs = @(
            "-Wall",
            "-Wextra",
            "-O2",
            "-std=c++17",
            "-I$WebViewIncludeDir",
            $Source,
            "-o",
            $OutPath,
            "-L$WebViewLibDir",
            "-lWebView2LoaderStatic",
            "-lwinhttp",
            "-lws2_32",
            "-lshell32",
            "-lole32",
            "-luser32",
            "-lgdi32",
            "-ladvapi32"
        )
    }
    Invoke-Compiler $gcc $gccArgs
    Write-Host "Build complete: $OutPath"
    exit 0
}

throw @"
No C compiler was found.

Fastest install option on Windows:
  winget install --id Microsoft.VisualStudio.2022.BuildTools -e

During installation, select:
  Desktop development with C++

Then reopen PowerShell and run:
  powershell -ExecutionPolicy Bypass -File .\build.ps1

Supported compilers:
  - Visual Studio Build Tools / MSVC: cl.exe
  - LLVM: clang.exe
  - MinGW-w64: gcc.exe
"@
