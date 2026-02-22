$versions = Get-Content (Resolve-Path ".\versions").Path | Select-Object -First 2
$TOOL_VERSION = $versions[0]
$API_VERSION  = $versions[1]

$CXX_FLAGS = "-DPLATFORM_WIN32 -DCPU_ARCH_X64 -DTOOL_VERSION=$TOOL_VERSION -DAPI_VERSION=$API_VERSION -DDEV_BUILD -I.. -I../libs -std=c++2b -O0 -g -gcodeview -march=native -masm=intel -fno-exceptions -fdiagnostics-absolute-paths -Wno-switch -Wno-deprecated-declarations -Wno-inconsistent-dllimport -nostdlib -nostdlib++"

if (!(Test-Path -Path ".\out")) {
    New-Item -ItemType Directory -Path ".\out" | Out-Null
}

$outDir = (Resolve-Path ".\out").Path

$compile_time = Measure-Command {
  $jobs = Get-ChildItem -Path ".\code" -Filter "*.cpp" | ForEach-Object {
    Start-Job -ScriptBlock {
      param($flags, $source, $workDir)
      Set-Location $workDir
      $output = cmd /c "clang++ $flags -c `"$source`" 2>&1"
      if ($LASTEXITCODE -ne 0) {
        throw "Failed to compile $source`n$output"
      }
    } -ArgumentList $CXX_FLAGS, $_.FullName, $outDir
  }

  $jobs | Wait-Job | Out-Null
  $failed = $jobs | Where-Object { $_.State -eq 'Failed' }
  if ($failed) {
    $failed | Receive-Job
    $jobs | Remove-Job -Force
    exit 1
  }
  $jobs | Remove-Job -Force
}

Write-Host ("Compile: {0,10:F6} seconds" -f $compile_time.TotalSeconds)

$link_time = Measure-Command {
  Push-Location $outDir
  & lld-link /def:..\cbuild.def *.o kernel32.lib shell32.lib Advapi32.lib /out:cbuild.exe /debug:full /subsystem:console
  $linkExit = $LASTEXITCODE
  Pop-Location
  if ($linkExit -ne 0) {
    Write-Error "Linking failed"
    exit 1
  }
}

Write-Host ("Link: {0,13:F6} seconds" -f $link_time.TotalSeconds)
