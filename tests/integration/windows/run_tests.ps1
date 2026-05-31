param(
    [Parameter(Mandatory = $true)]
    [string]$Kiln,

    [switch]$Keep
)

Set-StrictMode -Version 2.0
$ErrorActionPreference = "Stop"

function Resolve-KilnPath {
    param([string]$Path)

    $resolved = Resolve-Path -LiteralPath $Path -ErrorAction SilentlyContinue
    if (-not $resolved) {
        throw "Kiln binary not found: $Path"
    }

    return $resolved.ProviderPath
}

function New-SmokeFile {
    param(
        [string]$Path,
        [string]$Content
    )

    $directory = Split-Path -Parent $Path
    if ($directory -and -not (Test-Path -LiteralPath $directory)) {
        New-Item -ItemType Directory -Path $directory | Out-Null
    }

    $utf8NoBom = New-Object System.Text.UTF8Encoding $false
    [System.IO.File]::WriteAllText($Path, $Content.TrimStart("`r", "`n"), $utf8NoBom)
}

function Assert-PathExists {
    param(
        [string]$Path,
        [string]$Description
    )

    if (-not (Test-Path -LiteralPath $Path)) {
        throw "$Description not found: $Path"
    }
}

function Assert-Contains {
    param(
        [string]$Text,
        [string]$Expected,
        [string]$Description
    )

    if ($Text -notlike "*$Expected*") {
        throw "$Description did not contain '$Expected'. Actual output:`n$Text"
    }
}

function Invoke-KilnBuild {
    param([string]$CaseDir)

    Push-Location -LiteralPath $CaseDir
    try {
        $outputLines = & $script:KilnPath --config debug -j 1 2>&1 | ForEach-Object { $_.ToString() }
        $exitCode = $LASTEXITCODE
    } finally {
        Pop-Location
    }

    $output = $outputLines -join [Environment]::NewLine
    if ($exitCode -ne 0) {
        throw "Kiln failed with exit code $exitCode in $CaseDir`n$output"
    }

    return $output
}

function Invoke-SmokeExecutable {
    param(
        [string]$Path,
        [string]$ExpectedOutput,
        [string]$PathPrefix = ""
    )

    $oldPath = $env:PATH
    if ($PathPrefix) {
        $env:PATH = "$PathPrefix;$oldPath"
    }

    try {
        $outputLines = & $Path 2>&1 | ForEach-Object { $_.ToString() }
        $exitCode = $LASTEXITCODE
    } finally {
        $env:PATH = $oldPath
    }

    $output = $outputLines -join [Environment]::NewLine
    if ($exitCode -ne 0) {
        throw "Executable failed with exit code $exitCode`: $Path`n$output"
    }

    Assert-Contains $output $ExpectedOutput "Executable output"
}

function Add-BasicExecutableCase {
    param([string]$CaseDir)

    New-SmokeFile (Join-Path $CaseDir "CMakeLists.txt") @'
project(WindowsBasicExecutable)
add_executable(hello main.cpp)
'@
    New-SmokeFile (Join-Path $CaseDir "main.cpp") @'
#include <iostream>

int main() {
    std::cout << "hello-windows-smoke\n";
    return 0;
}
'@

    Invoke-KilnBuild $CaseDir | Out-Null
    $exe = Join-Path $CaseDir "build/debug/hello.exe"
    Assert-PathExists $exe "Executable"
    Invoke-SmokeExecutable $exe "hello-windows-smoke"
}

function Add-StaticLibraryCase {
    param([string]$CaseDir)

    New-SmokeFile (Join-Path $CaseDir "CMakeLists.txt") @'
project(WindowsStaticLibrary)
add_library(core STATIC core.cpp)
add_executable(app main.cpp)
target_link_libraries(app PRIVATE core)
'@
    New-SmokeFile (Join-Path $CaseDir "core.cpp") @'
const char* core_message() {
    return "static-library-smoke";
}
'@
    New-SmokeFile (Join-Path $CaseDir "main.cpp") @'
#include <iostream>

const char* core_message();

int main() {
    std::cout << core_message() << "\n";
    return 0;
}
'@

    Invoke-KilnBuild $CaseDir | Out-Null
    Assert-PathExists (Join-Path $CaseDir "build/debug/core.lib") "Static library"
    $exe = Join-Path $CaseDir "build/debug/app.exe"
    Assert-PathExists $exe "Executable"
    Invoke-SmokeExecutable $exe "static-library-smoke"
}

function Add-SharedLibraryCase {
    param([string]$CaseDir)

    New-SmokeFile (Join-Path $CaseDir "CMakeLists.txt") @'
project(WindowsSharedLibrary)
add_library(core SHARED core.cpp)
add_executable(app main.cpp)
target_link_libraries(app PRIVATE core)
'@
    New-SmokeFile (Join-Path $CaseDir "core.cpp") @'
extern "C" __declspec(dllexport) const char* core_message() {
    return "shared-library-smoke";
}
'@
    New-SmokeFile (Join-Path $CaseDir "main.cpp") @'
#include <iostream>

extern "C" __declspec(dllimport) const char* core_message();

int main() {
    std::cout << core_message() << "\n";
    return 0;
}
'@

    Invoke-KilnBuild $CaseDir | Out-Null
    $buildDir = Join-Path $CaseDir "build/debug"
    Assert-PathExists (Join-Path $buildDir "core.dll") "Shared library runtime"
    Assert-PathExists (Join-Path $buildDir "core.lib") "Shared library import library"
    $exe = Join-Path $buildDir "app.exe"
    Assert-PathExists $exe "Executable"
    Invoke-SmokeExecutable $exe "shared-library-smoke" $buildDir
}

function Add-CustomCommandCase {
    param([string]$CaseDir)

    New-SmokeFile (Join-Path $CaseDir "CMakeLists.txt") @'
project(WindowsCustomCommand)

add_custom_command(
    OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/generated.cpp
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${CMAKE_CURRENT_SOURCE_DIR}/generated.cpp.in ${CMAKE_CURRENT_BINARY_DIR}/generated.cpp
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/generated.cpp.in
    COMMENT "Generating Windows smoke source"
)

add_executable(app main.cpp ${CMAKE_CURRENT_BINARY_DIR}/generated.cpp)
'@
    New-SmokeFile (Join-Path $CaseDir "generated.cpp.in") @'
const char* generated_message() {
    return "custom-command-smoke";
}
'@
    New-SmokeFile (Join-Path $CaseDir "main.cpp") @'
#include <iostream>

const char* generated_message();

int main() {
    std::cout << generated_message() << "\n";
    return 0;
}
'@

    Invoke-KilnBuild $CaseDir | Out-Null
    $buildDir = Join-Path $CaseDir "build/debug"
    Assert-PathExists (Join-Path $buildDir "generated.cpp") "Generated source"
    $exe = Join-Path $buildDir "app.exe"
    Assert-PathExists $exe "Executable"
    Invoke-SmokeExecutable $exe "custom-command-smoke"
}

function Add-ExecuteProcessCase {
    param([string]$CaseDir)

    New-SmokeFile (Join-Path $CaseDir "CMakeLists.txt") @'
project(WindowsExecuteProcess)

execute_process(COMMAND cmd /c echo execute-process-smoke OUTPUT_VARIABLE out)
if(NOT out MATCHES "execute-process-smoke")
    message(FATAL_ERROR "cmd echo did not produce expected output: '${out}'")
endif()

execute_process(COMMAND cmd /c exit /b 7 RESULT_VARIABLE res)
if(NOT res EQUAL 7)
    message(FATAL_ERROR "cmd exit did not produce expected result: '${res}'")
endif()

message(STATUS "Windows execute_process smoke passed")
'@

    $output = Invoke-KilnBuild $CaseDir
    Assert-Contains $output "Windows execute_process smoke passed" "Kiln output"
}

function Add-FindProgramCase {
    param([string]$CaseDir)

    $toolDir = Join-Path $CaseDir "tools"
    New-SmokeFile (Join-Path $toolDir "local_tool.cmd") @'
@echo off
echo find-program-smoke
'@
    New-SmokeFile (Join-Path $CaseDir "CMakeLists.txt") @'
project(WindowsFindProgram)

find_program(LOCAL_TOOL local_tool.cmd PATHS "${CMAKE_CURRENT_SOURCE_DIR}/tools" NO_DEFAULT_PATH REQUIRED)
execute_process(COMMAND "${LOCAL_TOOL}" OUTPUT_VARIABLE out)
if(NOT out MATCHES "find-program-smoke")
    message(FATAL_ERROR "local tool did not produce expected output: '${out}'")
endif()

message(STATUS "Windows find_program smoke passed")
'@

    $output = Invoke-KilnBuild $CaseDir
    Assert-Contains $output "Windows find_program smoke passed" "Kiln output"
}

function Add-FindLibraryCase {
    param([string]$CaseDir)

    $libDir = Join-Path $CaseDir "vendor/lib"
    New-SmokeFile (Join-Path $libDir "local.lib") @'
not a real library; this file is only for find_library path resolution
'@
    New-SmokeFile (Join-Path $CaseDir "CMakeLists.txt") @'
project(WindowsFindLibrary)

find_library(LOCAL_LIB NAMES local PATHS "${CMAKE_CURRENT_SOURCE_DIR}/vendor/lib" NO_DEFAULT_PATH REQUIRED)
if(NOT LOCAL_LIB MATCHES "local\\.lib$")
    message(FATAL_ERROR "find_library did not return the expected .lib path: '${LOCAL_LIB}'")
endif()

message(STATUS "Windows find_library smoke passed: ${LOCAL_LIB}")
'@

    $output = Invoke-KilnBuild $CaseDir
    Assert-Contains $output "Windows find_library smoke passed" "Kiln output"
}

$script:KilnPath = Resolve-KilnPath $Kiln
$tempRoot = Join-Path ([System.IO.Path]::GetTempPath()) ("kiln-windows-smoke-" + [System.Guid]::NewGuid().ToString("N"))
New-Item -ItemType Directory -Path $tempRoot | Out-Null

$cases = @(
    @{ Name = "basic executable"; Action = ${function:Add-BasicExecutableCase} },
    @{ Name = "static library"; Action = ${function:Add-StaticLibraryCase} },
    @{ Name = "shared library"; Action = ${function:Add-SharedLibraryCase} },
    @{ Name = "custom command"; Action = ${function:Add-CustomCommandCase} },
    @{ Name = "execute_process"; Action = ${function:Add-ExecuteProcessCase} },
    @{ Name = "find_program"; Action = ${function:Add-FindProgramCase} },
    @{ Name = "find_library"; Action = ${function:Add-FindLibraryCase} }
)

$total = 0
$passed = 0

Write-Host "Using kiln: $script:KilnPath"
Write-Host "Work directory: $tempRoot"

try {
    foreach ($case in $cases) {
        $total += 1
        $caseDir = Join-Path $tempRoot ("case-" + $total.ToString("00"))
        New-Item -ItemType Directory -Path $caseDir | Out-Null

        Write-Host ("[{0}/{1}] {2}" -f $total, $cases.Count, $case.Name)
        & $case.Action $caseDir
        $passed += 1
        Write-Host ("PASS: {0}" -f $case.Name)
    }

    Write-Host ("Windows smoke tests passed: {0} / {1}" -f $passed, $total)
} catch {
    Write-Host ("Windows smoke tests failed: {0} / {1} completed" -f $passed, $total)
    Write-Error $_
    exit 1
} finally {
    if ($Keep) {
        Write-Host "Keeping work directory: $tempRoot"
    } elseif (Test-Path -LiteralPath $tempRoot) {
        Remove-Item -LiteralPath $tempRoot -Recurse -Force
    }
}
