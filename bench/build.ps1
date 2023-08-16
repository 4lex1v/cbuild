
if (!(Test-Path -Path ".\out")) {
    New-Item -ItemType Directory -Path ".\out" | Out-Null
}

Push-Location .\out

$compile_time = Measure-Command { clang++ -c ../code/main.cpp }
$link_time    = Measure-Command { lld-link *.o libcmt.lib /subsystem:console }

Write-Host ("Compile: {0,10:F6} millis" -f $compile_time.TotalMilliseconds)
Write-Host ("Link: {0,13:F6} millis" -f $link_time.TotalMilliseconds)
Write-Host ("Total: {0,13:F6} millis" -f ($compile_time.TotalMilliseconds + $link_time.TotalMilliseconds))

Pop-Location
