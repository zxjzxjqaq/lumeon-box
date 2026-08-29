param([string]$Class = "LumenBoxGlass")
Add-Type @'
using System;
using System.Text;
using System.Runtime.InteropServices;
public class GR2 {
    public delegate bool EnumProc(IntPtr h, IntPtr l);
    [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc p, IntPtr l);
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern IntPtr FindWindowEx(IntPtr p, IntPtr a, string c, string t);
    public struct RECT { public int L, T, R, B; }
    public static IntPtr Glass = IntPtr.Zero;
    public static RECT Rect;
    public static bool Cb(IntPtr h, IntPtr l) {
        IntPtr f = FindWindowEx(h, IntPtr.Zero, "LumenBoxGlass", null);
        if (f != IntPtr.Zero) { Glass = f; GetWindowRect(f, out Rect); return false; }
        return true;
    }
}
'@
[GR2]::EnumWindows([GR2+EnumProc]{ param($h, $l) [GR2]::Cb($h, $l) }, [IntPtr]::Zero) | Out-Null
if ([GR2]::Glass -eq [IntPtr]::Zero) { Write-Host "not found"; exit 1 }
Write-Host ("hwnd=0x{0:X} rect={1},{2}-{3},{4}" -f [GR2]::Glass.ToInt64(), [GR2]::Rect.L, [GR2]::Rect.T, [GR2]::Rect.R, [GR2]::Rect.B)
