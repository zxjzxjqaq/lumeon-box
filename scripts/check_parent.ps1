# check_parent.ps1 - verify where the lumenbox window is attached
Add-Type @"
using System;
using System.Text;
using System.Runtime.InteropServices;
public class W {
    public delegate bool EnumProc(IntPtr h, IntPtr l);
    [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc p, IntPtr l);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern IntPtr FindWindowEx(IntPtr p, IntPtr a, string c, string t);
    [DllImport("user32.dll")] public static extern IntPtr GetParent(IntPtr h);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern int GetClassName(IntPtr h, StringBuilder s, int n);
    [DllImport("user32.dll")] public static extern int GetWindowLong(IntPtr h, int i);
    public static IntPtr Glass = IntPtr.Zero;
    public static bool Callback(IntPtr h, IntPtr l) {
        IntPtr f = FindWindowEx(h, IntPtr.Zero, "LumenBoxGlass", null);
        if (f != IntPtr.Zero) { Glass = f; return false; }
        return true;
    }
}
"@
# 1) top-level search (Independent mode); [NullString]::Value marshals as real NULL
$h = [W]::FindWindowEx([IntPtr]::Zero, [IntPtr]::Zero, "LumenBoxGlass", [NullString]::Value)
# 2) child-of-WorkerW search (DesktopLayer mode)
if ($h -eq [IntPtr]::Zero) {
    [W]::EnumWindows([W+EnumProc]{ param($hh, $l) [W]::Callback($hh, $l) }, [IntPtr]::Zero) | Out-Null
    $h = [W]::Glass
}
if ($h -eq [IntPtr]::Zero) { Write-Host "window not found"; exit 1 }
$p = [W]::GetParent($h)
$style = [W]::GetWindowLong($h, -16)
$child = (($style -band 0x40000000) -ne 0)
if ($p -eq [IntPtr]::Zero) {
    Write-Host "mode=Independent (no parent, WS_CHILD=$child)"
} else {
    $sb = New-Object System.Text.StringBuilder 256
    [void][W]::GetClassName($p, $sb, 256)
    Write-Host "parent=0x$('{0:X}' -f $p.ToInt64()) class=$($sb.ToString()) WS_CHILD=$child"
}
