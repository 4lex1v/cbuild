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
      param($flags, $source, $workDir, $outName)
      Set-Location $workDir
      $output = cmd /c "clang++ $flags -c `"$source`" -o `"$outName`" 2>&1"
      if ($LASTEXITCODE -ne 0) {
        throw "Failed to compile $source`n$output"
      }
    } -ArgumentList $CXX_FLAGS, $_.FullName, $outDir, ("cbuild_" + $_.BaseName + ".o")
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
  & lld-link /def:..\cbuild.def cbuild_*.o kernel32.lib shell32.lib Advapi32.lib /out:cbuild.exe /debug:full /subsystem:console
  $linkExit = $LASTEXITCODE
  Pop-Location
  if ($linkExit -ne 0) {
    Write-Error "Linking failed"
    exit 1
  }
}

Write-Host ("Link: {0,13:F6} seconds" -f $link_time.TotalSeconds)

# Build tests target
$TESTS_CXX_FLAGS = "-DPLATFORM_WIN32 -DCPU_ARCH_X64 -DTOOL_VERSION=$TOOL_VERSION -DAPI_VERSION=$API_VERSION -DDEV_BUILD -DCBUILD_ENABLE_EXCEPTIONS -I.. -I../libs -std=c++2b -O0 -g -gcodeview -march=native -masm=intel -fdiagnostics-absolute-paths -Wno-switch -Wno-deprecated-declarations -Wno-inconsistent-dllimport -nostdlib -nostdlib++"

$tests_compile_time = Measure-Command {
  $testSources = @(Get-ChildItem -Path ".\tests" -Filter "*.cpp") + @(
    (Get-Item ".\code\cbuild_api.cpp"),
    (Get-Item ".\code\toolchain_win32.cpp"),
    (Get-Item ".\code\logger.cpp")
  )

  $jobs = $testSources | ForEach-Object {
    Start-Job -ScriptBlock {
      param($flags, $source, $workDir, $outName)
      Set-Location $workDir
      $output = cmd /c "clang++ $flags -c `"$source`" -o `"$outName`" 2>&1"
      if ($LASTEXITCODE -ne 0) {
        throw "Failed to compile $source`n$output"
      }
    } -ArgumentList $TESTS_CXX_FLAGS, $_.FullName, $outDir, ("tests_" + $_.BaseName + ".o")
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

Write-Host ("Tests Compile: {0,5:F6} seconds" -f $tests_compile_time.TotalSeconds)

$tests_link_time = Measure-Command {
  Push-Location $outDir
  & lld-link tests_*.o kernel32.lib shell32.lib Advapi32.lib libcmt.lib /out:tests.exe /debug:full /subsystem:console
  $linkExit = $LASTEXITCODE
  Pop-Location
  if ($linkExit -ne 0) {
    Write-Error "Tests linking failed"
    exit 1
  }
}

Write-Host ("Tests Link: {0,8:F6} seconds" -f $tests_link_time.TotalSeconds)
