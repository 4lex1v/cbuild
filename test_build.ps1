
$versions = Get-Content (Resolve-Path ".\versions").Path | Select-Object -First 2
$API_VERSION = $versions[1]

$CXX_FLAGS = @("-DPLATFORM_WIN32", "-DPLATFORM_X64", "-DTEST_BUILD", "-DTOOL_VERSION=$TOOL_VERSION", "-DAPI_VERSION=$API_VERSION", "-DDEV_BUILD", "-I..", "-std=c++20", "-O0", "-g", "-gcodeview", "-march=native", "-masm=intel", "-fdiagnostics-absolute-paths", "-Wno-switch", "-Wno-deprecated-declarations", "-Wno-writable-strings", "-Wno-inconsistent-dllimport")

if (!(Test-Path -Path ".\out")) {
    New-Item -ItemType Directory -Path ".\out" | Out-Null
}

Push-Location .\out

rm ./*.o

$compile_time = Measure-Command {
  Get-ChildItem -Path "..\code" -Filter "*.cpp" -Recurse `
  | Where-Object { $_.Name -ne "main.cpp" } `
  | ForEach-Object { & clang++ $CXX_FLAGS -c $_.FullName }
}

Write-Host ("Compile: {0,10:F6} seconds" -f $compile_time.TotalSeconds)

$link_time = Measure-Command {
  & lld-link /def:..\cbuild.def *.o kernel32.lib libcmt.lib shell32.lib Advapi32.lib /out:test.exe /debug:full /subsystem:console
}

Pop-Location
