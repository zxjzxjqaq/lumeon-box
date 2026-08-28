# probe_desktop_tree.ps1 - dump Progman/WorkerW structure and DefView location
Add-Type @"
using System;
using System.Text;
using System.Collections.Generic;
using System.Runtime.InteropServices;
public class DT {
    public delegate bool EnumProc(IntPtr h, IntPtr l);
    [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc p, IntPtr l);
    [DllImport("user32.dll")] public static extern int GetClassName(IntPtr h, StringBuilder s, int n);
    [DllImport("user32.dll")] public static extern IntPtr FindWindowEx(IntPtr p, IntPtr a, string c, string t);
    public static List<string> Rows = new List<string>();
    public static bool Callback(IntPtr h, IntPtr l) {
        var sb = new StringBuilder(256);
        GetClassName(h, sb, 256);
        string cls = sb.ToString();
        if (cls == "Progman" || cls == "WorkerW") {
            IntPtr dv = FindWindowEx(h, IntPtr.Zero, "SHELLDLL_DefView", null);
            IntPtr w = FindWindowEx(h, IntPtr.Zero, "WorkerW", null);
            Rows.Add(string.Format("hwnd=0x{0:X} class={1} hasDefView={2} childWorkerW=0x{3:X}", h.ToInt64(), cls, dv != IntPtr.Zero, w.ToInt64()));
        }
        return true;
    }
}
"@
[DT]::EnumWindows([DT+EnumProc]{ param($h, $l) [DT]::Callback($h, $l) }, [IntPtr]::Zero) | Out-Null
[DT]::Rows | ForEach-Object { Write-Host $_ }
