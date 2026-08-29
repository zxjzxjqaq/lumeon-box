$p = Get-Process lumenbox -ErrorAction SilentlyContinue
if (-not $p) { Write-Host "lumenbox not running"; exit 1 }
$privateWS = (Get-Counter "\Process(lumenbox)\Working Set - Private").CounterSamples[0].CookedValue / 1MB
Write-Host ("private working set = {0} MB" -f [math]::Round($privateWS, 1))
Write-Host ("private commit = {0} MB" -f [math]::Round($p.PrivateMemorySize64 / 1MB, 1))
Write-Host ("handles = {0}" -f $p.HandleCount)
