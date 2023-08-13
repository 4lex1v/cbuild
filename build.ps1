$versions = Get-Content (Resolve-Path ".\versions").Path | Select-Object -First 2
$TOOL_VERSION = $versions[0]
$API_VERSION  = $versions[1]

$CXX_FLAGS = @("-DPLATFORM_WIN32", "-DPLATFORM_X64", "-DTOOL_VERSION=$TOOL_VERSION", "-DAPI_VERSION=$API_VERSION", "-DDEV_BUILD", "-I..", "-std=c++20", "-O0", "-g", "-gcodeview", "-march=native", "-masm=intel", "-fno-exceptions", "-fdiagnostics-absolute-paths", "-Wno-switch", "-Wno-deprecated-declarations", "-Wno-inconsistent-dllimport")

if (!(Test-Path -Path ".\out")) {
    New-Item -ItemType Directory -Path ".\out" | Out-Null
}

Push-Location .\out

$compile_time = Measure-Command {
  Get-ChildItem -Path "..\code" -Filter "*.cpp" | ForEach-Object -Parallel {
    & clang++ $using:CXX_FLAGS -c $_.FullName
  }
}

Write-Host ("Compile: {0,10:F6} seconds" -f $compile_time.TotalSeconds)

$link_time = Measure-Command {
  & lld-link /def:..\cbuild.def *.o kernel32.lib libcmt.lib shell32.lib Advapi32.lib /out:cbuild.exe /debug:full /subsystem:console
}

Write-Host ("Link: {0,13:F6} seconds" -f $link_time.TotalSeconds)

Pop-Location
