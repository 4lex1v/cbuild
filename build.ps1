
$version_file = (Resolve-Path ".\version").Path  # Replace with your file path
$VERSION = [System.Text.Encoding]::ASCII.GetString([System.IO.File]::ReadAllBytes($version_file), 0, 8)

$CXX_FLAGS = @("-DPLATFORM_WIN32", "-DPLATFORM_X64", "-DVERSION=$VERSION", "-DDEV_BUILD", "-I..", "-std=c++20", "-O0", "-g", "-gcodeview", "-march=native", "-masm=intel", "-fno-exceptions", "-fdiagnostics-absolute-paths", "-Wno-switch", "-Wno-deprecated-declarations", "-Wno-inconsistent-dllimport")

if (!(Test-Path -Path ".\out")) {
    New-Item -ItemType Directory -Path ".\out" | Out-Null
}

Push-Location .\out

$compile_time = Measure-Command {
  Get-ChildItem -Path "..\code" -Filter "*.cpp" -Recurse | ForEach-Object {
      & clang++ $CXX_FLAGS -c $_.FullName
  }
}

Write-Host ("Compile: {0,10:F6} seconds" -f $compile_time.TotalSeconds)

$link_time = Measure-Command {
  & lld-link /def:..\cbuild.def *.o kernel32.lib libcmt.lib /out:cbuild.exe /debug:full /subsystem:console
}

Write-Host ("Link: {0,13:F6} seconds" -f $link_time.TotalSeconds)

$file = Get-Item "cbuild.exe"
$fileSize = $file.Length

if ($fileSize -ge 1GB) {
    Write-Host ("File size: {0:F2} GB" -f ($fileSize / 1GB))
} elseif ($fileSize -ge 1MB) {
    Write-Host ("File size: {0:F2} MB" -f ($fileSize / 1MB))
} elseif ($fileSize -ge 1KB) {
    Write-Host ("File size: {0:F2} KB" -f ($fileSize / 1KB))
} else {
    Write-Host ("File size: {0} bytes" -f $fileSize)
}

Pop-Location
