Add-Type @'
using System;
using System.Text;
using System.Runtime.InteropServices;
public class CAP {
    public delegate bool EnumProc(IntPtr h, IntPtr l);
    [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc p, IntPtr l);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern IntPtr FindWindowEx(IntPtr p, IntPtr a, string c, string t);
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
    [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr hdc, uint flags);
    public struct RECT { public int L, T, R, B; }

    public static IntPtr FindGlass() {
        IntPtr g = IntPtr.Zero;
        EnumWindows(delegate(IntPtr h, IntPtr l) {
            IntPtr f = FindWindowEx(h, IntPtr.Zero, "LumenBoxGlass", null);
            if (f != IntPtr.Zero) { g = f; return false; }
            return true;
        }, IntPtr.Zero);
        return g;
    }
    public static IntPtr FindNote() {
        return FindWindowEx(IntPtr.Zero, IntPtr.Zero, "LumenBoxNote", null);
    }
}
'@
Add-Type -AssemblyName System.Drawing
foreach ($pair in @(@('note', [CAP]::FindNote()), @('glass', [CAP]::FindGlass()))) {
    $name = $pair[0]; $hwnd = $pair[1]
    if ($hwnd -eq [IntPtr]::Zero) { Write-Host "$name not found"; continue }
    $r = New-Object CAP+RECT
    [CAP]::GetWindowRect($hwnd, [ref]$r) | Out-Null
    $w = $r.R - $r.L; $h = $r.B - $r.T
    if ($w -le 0 -or $h -le 0) { Write-Host "$name empty rect"; continue }
    $bmp = New-Object System.Drawing.Bitmap($w, $h)
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $hdc = $g.GetHdc()
    $ok = [CAP]::PrintWindow($hwnd, $hdc, 2)  # PW_RENDERFULLCONTENT
    $g.ReleaseHdc($hdc)
    $g.Dispose()
    $bmp.Save("$env:TEMP\cap_$name.png")
    $bmp.Dispose()
    Write-Host "$name ${w}x${h} saved pw=$ok"
}
