param([string]$Class = "LumenBoxDragProbe")
Add-Type @'
using System;
using System.Text;
using System.Runtime.InteropServices;
public class PW {
    public delegate bool EnumProc(IntPtr h, IntPtr l);
    [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc p, IntPtr l);
    [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern int GetClassName(IntPtr h, StringBuilder s, int n);
    public struct RECT { public int L, T, R, B; }
    public static bool Cb(IntPtr h, IntPtr l) {
        StringBuilder c = new StringBuilder(64);
        GetClassName(h, c, 64);
        if (c.ToString() == "LumenBoxDragProbe") {
            RECT r; GetWindowRect(h, out r);
            Console.WriteLine("probe hwnd=0x" + h.ToString("X") + " visible=" + IsWindowVisible(h) + " rect=" + r.L + "," + r.T + "-" + r.R + "," + r.B);
        }
        return true;
    }
}
'@
[PW]::EnumWindows([PW+EnumProc]{ param($h, $l) [PW]::Cb($h, $l) }, [IntPtr]::Zero) | Out-Null
Write-Host scan-done
