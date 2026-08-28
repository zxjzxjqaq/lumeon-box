# measure.ps1 - sample lumenbox process resource usage
# Usage: powershell -ExecutionPolicy Bypass -File scripts\measure.ps1 [-Seconds 60]
param([string]$Name = "lumenbox", [int]$Seconds = 60)

$samples = @()
$end = (Get-Date).AddSeconds($Seconds)
while ((Get-Date) -lt $end) {
    $p = Get-Process -Name $Name -ErrorAction SilentlyContinue
    if ($p) {
        $samples += [pscustomobject]@{
            Time         = Get-Date -Format HH:mm:ss
            PrivateMB    = [math]::Round($p.PrivateMemorySize64 / 1MB, 1)
            WorkingSetMB = [math]::Round($p.WorkingSet64 / 1MB, 1)
            Handles      = $p.HandleCount
            CPUsec       = [math]::Round($p.TotalProcessorTime.TotalSeconds, 2)
        }
    }
    Start-Sleep -Seconds 5
}
$samples | Format-Table -AutoSize
$samples | Export-Csv -Path "$PSScriptRoot\..\docs\m0-measure.csv" -NoTypeInformation -Encoding UTF8
if ($samples.Count -ge 2) {
    $idleCpuPct = [math]::Round(($samples[-1].CPUsec - $samples[0].CPUsec) / $Seconds * 100, 2)
    Write-Host "idle CPU = $idleCpuPct %"
}
