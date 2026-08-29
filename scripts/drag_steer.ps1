param([int]$Tx = 1740, [int]$Ty = 1280)
Add-Type @'
using System;
using System.Runtime.InteropServices;
public class FD6 {
    [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
    [DllImport("user32.dll")] public static extern void mouse_event(uint f, int dx, int dy, uint d, UIntPtr e);
    [DllImport("user32.dll")] public static extern bool GetCursorPos(out POINT p);
    public struct POINT { public int X, Y; }
}
'@
[FD6]::SetCursorPos(2316, 704) | Out-Null
Start-Sleep -Milliseconds 300
[FD6]::mouse_event(0x0002, 0, 0, 0, [UIntPtr]::Zero)
Start-Sleep -Milliseconds 500
for ($i = 0; $i -lt 250; $i++) {
    $p = New-Object FD6+POINT
    [FD6]::GetCursorPos([ref]$p) | Out-Null
    $dx = $Tx - $p.X; $dy = $Ty - $p.Y
    if ([Math]::Abs($dx) -lt 3 -and [Math]::Abs($dy) -lt 3) { break }
    $sx = [Math]::Max(-6, [Math]::Min(6, [int]($dx / 1.4)))
    $sy = [Math]::Max(-6, [Math]::Min(6, [int]($dy / 1.4)))
    if ($sx -eq 0 -and $dx -ne 0) { $sx = [Math]::Sign($dx) }
    if ($sy -eq 0 -and $dy -ne 0) { $sy = [Math]::Sign($dy) }
    [FD6]::mouse_event(0x0001, $sx, $sy, 0, [UIntPtr]::Zero)
    Start-Sleep -Milliseconds 35
}
$f = New-Object FD6+POINT
[FD6]::GetCursorPos([ref]$f) | Out-Null
Write-Host "arrived=$($f.X),$($f.Y)"
Start-Sleep -Milliseconds 1200
[FD6]::mouse_event(0x0004, 0, 0, 0, [UIntPtr]::Zero)
Write-Host released
